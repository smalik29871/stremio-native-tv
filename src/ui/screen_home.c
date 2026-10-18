#include "../app.h"
#include "../http.h"
#include "../log.h"
#include "font.h"
#include "theme.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { POSTER_W = 200, POSTER_H = 300, POSTER_GAP = 24, ROW_H = 400, ROWS_TOP = 250, VISIBLE = 8 };
enum { TOP_SEARCH, TOP_SETTINGS, TOP_ACCOUNT, TOP_COUNT };

typedef struct Row {
    char title[128];
    int is_continue;
    int addon, catalog;            /* indexes into g_app.addons */
    MetaPreview *metas; int n;
    int loading, failed, requested;
    unsigned req;
    int focus;
} Row;

typedef struct {
    Row **rows; int n_rows;
    int focus_row;                 /* -1 = top bar */
    int focus_top;
    float scroll_y, scroll_target;
    int addons_seen, library_seen;
} Home;

static void row_free(Row *r)
{
    if (!r) return;
    if (r->req) http_cancel(r->req);
    meta_previews_free(r->metas, r->n);
    free(r);
}

static void build_continue_row(Row *r)
{
    /* Continue Watching = library entries with an in-progress offset, newest first. */
    meta_previews_free(r->metas, r->n);
    r->metas = NULL; r->n = 0;
    int cap = g_app.n_library;
    if (!cap) return;
    LibraryItem **sorted = malloc(sizeof *sorted * (size_t)cap);
    int n = 0;
    for (int i = 0; i < g_app.n_library; i++) {
        LibraryItem *li = &g_app.library[i];
        if (li->time_offset > 0 && (!li->removed || li->temp)) sorted[n++] = li;
    }
    for (int i = 1; i < n; i++)       /* insertion sort by lastWatched desc */
        for (int j = i; j > 0 && sorted[j]->last_watched_ms > sorted[j - 1]->last_watched_ms; j--) {
            LibraryItem *t = sorted[j]; sorted[j] = sorted[j - 1]; sorted[j - 1] = t;
        }
    if (n > 20) n = 20;
    r->metas = calloc((size_t)n + 1, sizeof(MetaPreview));
    for (int i = 0; i < n; i++) {
        MetaPreview *p = &r->metas[i];
        p->id = strdup(sorted[i]->id);
        p->type = strdup(sorted[i]->type);
        p->name = strdup(sorted[i]->name);
        p->poster = sorted[i]->poster ? strdup(sorted[i]->poster) : NULL;
        p->poster_shape = sorted[i]->poster_shape ? strdup(sorted[i]->poster_shape) : NULL;
    }
    r->n = n;
    free(sorted);
    r->loading = 0;
}

static void rebuild_rows(Home *h)
{
    /* Preserve fetched data for catalogs that still exist. */
    Row **old = h->rows; int n_old = h->n_rows;
    int cap = 1;
    for (int a = 0; a < g_app.n_addons; a++) cap += g_app.addons[a].manifest.n_catalogs;
    h->rows = calloc((size_t)cap, sizeof(Row *));
    h->n_rows = 0;

    Row *cw = NULL;
    if (n_old && old[0]->is_continue) { cw = old[0]; old[0] = NULL; }
    else { cw = calloc(1, sizeof *cw); snprintf(cw->title, sizeof cw->title, "Continue Watching"); cw->is_continue = 1; }
    h->rows[h->n_rows++] = cw;
    build_continue_row(cw);

    for (int a = 0; a < g_app.n_addons; a++) {
        const Addon *ad = &g_app.addons[a];
        for (int c = 0; c < ad->manifest.n_catalogs; c++) {
            const AddonCatalog *cat = &ad->manifest.catalogs[c];
            if (addon_catalog_has_required_extra(cat)) continue;   /* search-only etc. */
            char title[128];
            snprintf(title, sizeof title, "%s - %s", cat->name, ad->manifest.name);
            Row *r = NULL;
            for (int i = 0; i < n_old; i++) {
                if (!old[i] || old[i]->is_continue) continue;
                if (strcmp(old[i]->title, title) == 0) { r = old[i]; old[i] = NULL; break; }
            }
            if (!r) { r = calloc(1, sizeof *r); snprintf(r->title, sizeof r->title, "%s", title); }
            r->addon = a; r->catalog = c;
            h->rows[h->n_rows++] = r;
        }
    }
    for (int i = 0; i < n_old; i++) row_free(old[i]);
    free(old);
    if (h->focus_row >= h->n_rows) h->focus_row = h->n_rows - 1;
    h->addons_seen = g_app.addons_version;
    h->library_seen = g_app.library_version;
}

static void on_catalog(HttpResponse *r, void *user)
{
    Row *row = user;
    row->req = 0;
    row->loading = 0;
    if (r->status != 200 || addon_parse_metas(r->body, &row->metas, &row->n) != 0) {
        row->failed = 1;
        LOGW("catalog %s failed: %ld %s", row->title, r->status, r->error);
    }
}

static void fetch_row(Row *r)
{
    if (r->is_continue || r->requested) return;
    if (r->addon >= g_app.n_addons) return;
    const Addon *ad = &g_app.addons[r->addon];
    const AddonCatalog *cat = &ad->manifest.catalogs[r->catalog];
    char url[1024];
    addon_url_catalog(ad, cat->type, cat->id, NULL, url, sizeof url);
    r->requested = 1;
    r->loading = 1;
    r->req = http_request_async(url, NULL, on_catalog, r);
}

static int row_has_content(const Row *r) { return r->n > 0 || r->loading || (!r->is_continue && !r->failed && !r->requested); }

/* Rows that are empty (and finished) are skipped by navigation and collapsed in layout. */
static int row_visible(const Row *r)
{
    if (r->is_continue) return r->n > 0;
    return row_has_content(r);
}

static int next_visible(const Home *h, int from, int dir)
{
    for (int i = from + dir; i >= 0 && i < h->n_rows; i += dir) if (row_visible(h->rows[i])) return i;
    return -2;
}

static void ensure_focus_valid(Home *h)
{
    if (h->focus_row >= 0 && (h->focus_row >= h->n_rows || !row_visible(h->rows[h->focus_row]))) {
        int n = next_visible(h, h->focus_row, 1);
        if (n < 0) n = next_visible(h, h->focus_row, -1);
        h->focus_row = n < 0 ? -1 : n;
    }
}

static void update(Screen *s, float dt)
{
    Home *h = s->st;
    if (h->addons_seen != g_app.addons_version) rebuild_rows(h);
    if (h->library_seen != g_app.library_version) { build_continue_row(h->rows[0]); h->library_seen = g_app.library_version; }

    /* Fetch rows near the focus; the first few always. */
    int fetched = 0;
    for (int i = 0; i < h->n_rows; i++) {
        int near = h->focus_row < 0 ? i < 4 : (i >= h->focus_row - 1 && i <= h->focus_row + 3);
        if (near && !h->rows[i]->requested && !h->rows[i]->is_continue) { fetch_row(h->rows[i]); if (++fetched >= 4) break; }
    }
    ensure_focus_valid(h);

    /* layout target: focused row sits at ROWS_TOP */
    float y = 0;
    for (int i = 0; i < h->n_rows && i < h->focus_row; i++) if (row_visible(h->rows[i])) y += ROW_H;
    h->scroll_target = h->focus_row < 0 ? 0 : y;
    float k = 1 - SDL_powf(0.001f, dt);
    h->scroll_y += (h->scroll_target - h->scroll_y) * k;
}

static void draw_top_bar(const Home *h)
{
    const char *labels[TOP_COUNT] = { "Search", "Settings", g_app.user_email ? g_app.user_email : "Sign in" };
    font_draw("Stremio", THEME_MARGIN, 44, FONT_L, THEME_TEXT);
    float x = SNTV_UI_W - THEME_MARGIN;
    for (int i = TOP_COUNT - 1; i >= 0; i--) {
        float w = font_measure(labels[i], FONT_M) + 48;
        x -= w;
        ui_draw_button(labels[i], x, 44, w, 56, h->focus_row == -1 && h->focus_top == i, 0);
        x -= 16;
    }
}

static void draw(Screen *s)
{
    Home *h = s->st;
    gfx_rect(0, 0, SNTV_UI_W, SNTV_UI_H, THEME_BG);

    /* backdrop of the focused item */
    if (h->focus_row >= 0) {
        const Row *r = h->rows[h->focus_row];
        if (r->n) {
            const MetaPreview *p = &r->metas[r->focus < r->n ? r->focus : 0];
            int iw, ih;
            unsigned tex = p->background ? image_get(p->background, &iw, &ih) : 0;
            if (tex) gfx_tex(tex, 0, 0, SNTV_UI_W, SNTV_UI_H, RGBA(255, 255, 255, 70));
            gfx_gradient_v(0, 0, SNTV_UI_W, SNTV_UI_H, RGBA(0x0F, 0x0D, 0x20, 0x60), RGBA(0x0F, 0x0D, 0x20, 0xFF));
            char info[256] = "";
            if (p->release_info) snprintf(info, sizeof info, "%s", p->release_info);
            if (p->imdb_rating) snprintf(info + strlen(info), sizeof info - strlen(info), "%sIMDb %s", info[0] ? "   " : "", p->imdb_rating);
            for (int g = 0; g < p->genres.n && g < 3; g++) snprintf(info + strlen(info), sizeof info - strlen(info), "%s%s", (info[0] ? "   " : ""), p->genres.v[g]);
            font_draw_fit(p->name, THEME_MARGIN, 130, FONT_XL, THEME_TEXT, SNTV_UI_W - 2 * THEME_MARGIN);
            font_draw_fit(info, THEME_MARGIN, 196, FONT_S, THEME_TEXT_DIM, SNTV_UI_W - 2 * THEME_MARGIN);
        }
    }

    gfx_clip_push(0, ROWS_TOP, SNTV_UI_W, SNTV_UI_H - ROWS_TOP);
    float y = ROWS_TOP - h->scroll_y;
    for (int i = 0; i < h->n_rows; i++) {
        Row *r = h->rows[i];
        if (!row_visible(r)) continue;
        if (y > SNTV_UI_H || y + ROW_H < ROWS_TOP) { y += ROW_H; continue; }
        int focused_row = h->focus_row == i;
        font_draw(r->title, THEME_MARGIN, y, FONT_M, focused_row ? THEME_TEXT : THEME_TEXT_DIM);
        float py = y + 50;
        if (r->loading && !r->n) ui_draw_spinner(THEME_MARGIN + 100, py + POSTER_H / 2);
        if (!r->n && r->failed) font_draw("Could not load this catalog", THEME_MARGIN, py + 20, FONT_S, THEME_TEXT_MUTED);
        int first = r->focus - (VISIBLE - 1);
        if (first < 0) first = 0;
        for (int j = first; j < r->n && j < first + VISIBLE + 1; j++) {
            float px = THEME_MARGIN + (j - first) * (POSTER_W + POSTER_GAP);
            int foc = focused_row && r->focus == j;
            float grow = foc ? 10 : 0;
            ui_draw_poster(r->metas[j].poster, px - grow, py - grow, POSTER_W + 2 * grow, POSTER_H + 2 * grow, foc, r->metas[j].name);
            if (r->is_continue) {
                LibraryItem *li = app_library_find(r->metas[j].id);
                if (li && li->duration) {
                    float frac = (float)li->time_offset / (float)li->duration;
                    gfx_rect(px - grow, py + POSTER_H + grow - 8, POSTER_W + 2 * grow, 8, RGBA(0, 0, 0, 160));
                    gfx_rect(px - grow, py + POSTER_H + grow - 8, (POSTER_W + 2 * grow) * frac, 8, THEME_ACCENT);
                }
            }
        }
        y += ROW_H;
    }
    gfx_clip_pop();

    if (!h->n_rows || (h->n_rows == 1 && !h->rows[0]->n)) {
        if (g_app.addons_loading) { ui_draw_spinner(SNTV_UI_W / 2, SNTV_UI_H / 2); font_draw("Loading addons...", SNTV_UI_W / 2 - 100, SNTV_UI_H / 2 + 50, FONT_M, THEME_TEXT_DIM); }
        else if (g_app.addons_error[0]) font_draw(g_app.addons_error, THEME_MARGIN, SNTV_UI_H / 2, FONT_M, THEME_DANGER);
        else font_draw("No catalogs. Sign in or install a catalog addon in Settings.", THEME_MARGIN, SNTV_UI_H / 2, FONT_M, THEME_TEXT_DIM);
    }
    draw_top_bar(h);
}

static int key(Screen *s, KeyAction a)
{
    Home *h = s->st;
    if (a == KEY_SEARCH) { app_push(screen_search_create()); return 1; }
    if (h->focus_row == -1) {
        switch (a) {
        case KEY_LEFT: if (h->focus_top > 0) h->focus_top--; return 1;
        case KEY_RIGHT: if (h->focus_top < TOP_COUNT - 1) h->focus_top++; return 1;
        case KEY_DOWN: { int n = next_visible(h, -1, 1); if (n >= 0) h->focus_row = n; return 1; }
        case KEY_OK:
            if (h->focus_top == TOP_SEARCH) app_push(screen_search_create());
            else if (h->focus_top == TOP_SETTINGS) app_push(screen_settings_create());
            else app_push(g_app.auth_key ? screen_settings_create() : screen_login_create());
            return 1;
        case KEY_BACK: return 0;
        default: return 1;
        }
    }
    Row *r = h->rows[h->focus_row];
    switch (a) {
    case KEY_UP: { int n = next_visible(h, h->focus_row, -1); h->focus_row = n >= 0 ? n : -1; return 1; }
    case KEY_DOWN: { int n = next_visible(h, h->focus_row, 1); if (n >= 0) h->focus_row = n; return 1; }
    case KEY_LEFT: if (r->focus > 0) r->focus--; return 1;
    case KEY_RIGHT: if (r->focus < r->n - 1) r->focus++; return 1;
    case KEY_OK: if (r->focus < r->n) app_push(screen_detail_create(&r->metas[r->focus])); return 1;
    case KEY_BACK:
        if (h->focus_row > 0 || r->focus > 0) { r->focus = 0; h->focus_row = next_visible(h, -1, 1); return 1; }
        g_app.quit = 1;   /* Back on the first row exits, like the LG/Samsung launchers expect */
        return 1;
    default: return 0;
    }
}

static void destroy(Screen *s)
{
    Home *h = s->st;
    for (int i = 0; i < h->n_rows; i++) row_free(h->rows[i]);
    free(h->rows);
    free(h);
    free(s);
}

Screen *screen_home_create(void)
{
    Screen *s = calloc(1, sizeof *s);
    Home *h = calloc(1, sizeof *h);
    h->focus_row = -1;
    h->addons_seen = -1;
    s->name = "home"; s->st = h;
    s->update = update; s->draw = draw; s->key = key; s->destroy = destroy;
    rebuild_rows(h);
    return s;
}
