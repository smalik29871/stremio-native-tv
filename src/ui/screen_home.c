/* Home: cinematic hero for the focused title over its backdrop, ambient light
 * tinted by the artwork, then rows: Continue Watching (landscape cards with
 * progress), My Library, and one row per addon catalog. Everything eases;
 * rows are served from the disk cache first and refreshed in the background. */
#include "../app.h"
#include "../http.h"
#include "../log.h"
#include "font.h"
#include "theme.h"
#include "widgets.h"
#include "rail.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { ROW_CATALOG, ROW_CONTINUE, ROW_LIBRARY };
enum { NAV_SEARCH, NAV_SETTINGS, NAV_ACCOUNT, NAV_COUNT };

#define LX RAIL_LX           /* content starts right of the side rail */
#define ROWS_Y 610.0f          /* Cinematic; the other styles move it (rows_y) */
#define SHOWCASE_N 10         /* titles in the Showcase carousel */
#define SHOWCASE_EVERY 8.0f   /* seconds before the carousel moves by itself */
#define FOCUS_SHOWCASE (-2)   /* focus_row: the carousel View button */
#define POSTER_W 196.0f
#define POSTER_H 294.0f
#define LAND_W 384.0f
#define LAND_H 216.0f
#define GAP 30.0f
#define TITLE_H 58.0f

typedef struct Row {
    char key[200];
    char title[128];
    int kind;
    int addon, catalog;
    MetaPreview *metas; int n;
    int loading, failed, requested;
    unsigned req;
    int focus;
    float scroll_x;
    char url[1024];
} Row;

typedef struct {
    Row **rows; int n_rows;
    int focus_row;            /* -1 = nav bar */
    int focus_nav;
    float scroll_y;
    int addons_seen, library_seen, rows_seen, watched_seen;
    float focus_t;            /* 0..1 grow-in of the focused card */
    int prev_row, prev_col; float prev_t;
    char hero_id[256];
    char hero_bg[1024], hero_prev_bg[1024]; float hero_fade;
    float hero_text_t;        /* fade/slide of the hero text on change */
    int landed;               /* first focus went to the content */
    int feat;                 /* Showcase: which of the featured row's titles is up */
    float feat_timer;
} Home;

static void move_focus(Home *h, int row, int col);
static float rows_y(void) { return g_app.home_style == HOME_COMPACT ? 150.0f : g_app.home_style == HOME_SHOWCASE ? 660.0f : ROWS_Y; }

/* ---- row geometry ---- */

static float card_w(const Row *r) { return r->kind == ROW_CONTINUE ? LAND_W : POSTER_W; }
static float card_h(const Row *r) { return r->kind == ROW_CONTINUE ? LAND_H : POSTER_H; }
static float row_h(const Row *r) { return TITLE_H + card_h(r) + 64; }
static int visible_cards(const Row *r) { return (int)((SNTV_UI_W - LX) / (card_w(r) + GAP)); }

static void row_free(Row *r)
{
    if (!r) return;
    if (r->req) http_cancel(r->req);
    meta_previews_free(r->metas, r->n);
    free(r);
}

/* Library items arrive without a synopsis; the details fetched for the hero are
 * kept here so rebuilt rows and later visits have them at once. */
static Home *g_home_live;
enum { DESC_CACHE = 64 };
static struct { char id[64]; MetaPreview p; } g_desc[DESC_CACHE];
static int g_n_desc, g_desc_next;
static char g_desc_pending[64];

static const MetaPreview *desc_cached(const char *id)
{
    for (int i = 0; i < g_n_desc; i++) if (strcmp(g_desc[i].id, id) == 0) return &g_desc[i].p;
    return NULL;
}

static void fill_from(MetaPreview *m, const MetaPreview *src)
{
    if (!m->description && src->description) m->description = strdup(src->description);
    if (!m->release_info && src->release_info) m->release_info = strdup(src->release_info);
    if (!m->runtime && src->runtime) m->runtime = strdup(src->runtime);
    if (!m->imdb_rating && src->imdb_rating) m->imdb_rating = strdup(src->imdb_rating);
    if (!m->background && src->background) m->background = strdup(src->background);
    if (!m->logo && src->logo) m->logo = strdup(src->logo);
    if (!m->genres.n && src->genres.n) {
        m->genres.v = calloc((size_t)src->genres.n, sizeof(char *));
        for (int i = 0; i < src->genres.n; i++) m->genres.v[i] = strdup(src->genres.v[i]);
        m->genres.n = src->genres.n;
    }
}

static MetaPreview preview_from_item(const LibraryItem *li)
{
    MetaPreview p;
    memset(&p, 0, sizeof p);
    p.id = strdup(li->id);
    p.type = strdup(li->type);
    p.name = strdup(li->name);
    p.poster = li->poster ? strdup(li->poster) : NULL;
    p.poster_shape = li->poster_shape ? strdup(li->poster_shape) : NULL;
    const MetaPreview *c = desc_cached(li->id);
    if (c) fill_from(&p, c);
    return p;
}

static void build_library_row(Row *r)
{
    meta_previews_free(r->metas, r->n);
    r->metas = NULL; r->n = 0;
    if (!g_app.n_library) return;
    LibraryItem **sorted = malloc(sizeof *sorted * (size_t)g_app.n_library);
    int n = 0;
    for (int i = 0; i < g_app.n_library; i++) {
        LibraryItem *li = &g_app.library[i];
        int take = r->kind == ROW_CONTINUE ? (li->time_offset > 0 && (!li->removed || li->temp)) : (!li->removed && !li->temp);
        if (take) sorted[n++] = li;
    }
    for (int i = 1; i < n; i++)
        for (int j = i; j > 0; j--) {
            int64_t a = r->kind == ROW_CONTINUE ? sorted[j]->last_watched_ms : sorted[j]->mtime_ms;
            int64_t b = r->kind == ROW_CONTINUE ? sorted[j - 1]->last_watched_ms : sorted[j - 1]->mtime_ms;
            if (a <= b) break;
            LibraryItem *t = sorted[j]; sorted[j] = sorted[j - 1]; sorted[j - 1] = t;
        }
    if (n > 30) n = 30;
    r->metas = calloc((size_t)n + 1, sizeof(MetaPreview));
    for (int i = 0; i < n; i++) r->metas[i] = preview_from_item(sorted[i]);
    r->n = n;
    if (r->focus >= n) r->focus = n ? n - 1 : 0;
    free(sorted);
}

static Row *new_row(int kind, const char *title)
{
    Row *r = calloc(1, sizeof *r);
    r->kind = kind;
    snprintf(r->title, sizeof r->title, "%s", title);
    return r;
}

static void rebuild_rows(Home *h)
{
    Row **old = h->rows; int n_old = h->n_rows;
    HomeRowDef defs[160];
    int nd = app_home_rows(defs, 160);
    h->rows = calloc((size_t)nd + 1, sizeof(Row *));
    h->n_rows = 0;
    for (int d = 0; d < nd; d++) {
        if (defs[d].hidden) continue;
        Row *r = NULL;
        for (int i = 0; i < n_old; i++) if (old[i] && strcmp(old[i]->key, defs[d].key) == 0) { r = old[i]; old[i] = NULL; break; }
        if (!r) { r = new_row(defs[d].kind == 0 ? ROW_CONTINUE : defs[d].kind == 1 ? ROW_LIBRARY : ROW_CATALOG, defs[d].title); snprintf(r->key, sizeof r->key, "%s", defs[d].key); }
        if (r->kind == ROW_CATALOG) {
            /* the row title drops the addon name; it is drawn beside it */
            char *sep = strstr(defs[d].title, "  \xC2\xB7  ");
            snprintf(r->title, sizeof r->title, "%.*s", sep ? (int)(sep - defs[d].title) : (int)strlen(defs[d].title), defs[d].title);
            r->addon = defs[d].addon; r->catalog = defs[d].catalog;
        } else build_library_row(r);
        h->rows[h->n_rows++] = r;
    }
    for (int i = 0; i < n_old; i++) row_free(old[i]);
    free(old);
    if (h->focus_row >= h->n_rows) h->focus_row = h->n_rows - 1;
    h->addons_seen = g_app.addons_version;
    h->rows_seen = g_app.home_rows_version;
    h->library_seen = g_app.library_version;
}

/* Titles the library marks watched leave catalog rows when the viewer asked for it. */
static void drop_watched(MetaPreview **m, int *n)
{
    if (!g_app.hide_watched) return;
    int k = 0;
    for (int i = 0; i < *n; i++) {
        if (app_title_watched((*m)[i].id)) meta_preview_free(&(*m)[i]);
        else (*m)[k++] = (*m)[i];
    }
    *n = k;
}

static void apply_catalog(Row *row, const char *json)
{
    MetaPreview *m; int n;
    if (addon_parse_metas(json, &m, &n) != 0) return;
    drop_watched(&m, &n);
    if (g_app.low_memory && n > 40) { for (int i = 40; i < n; i++) meta_preview_free(&m[i]); n = 40; }   /* memory saver: shorter rows */
    char *focused = row->n && row->focus < row->n ? strdup(row->metas[row->focus].id) : NULL;
    meta_previews_free(row->metas, row->n);
    row->metas = m; row->n = n;
    for (int i = 0; i < n && i < 6; i++) app_ambient_offer(&m[i]);
    if (focused) { for (int i = 0; i < n; i++) if (strcmp(m[i].id, focused) == 0) row->focus = i; free(focused); }
    if (row->focus >= n) row->focus = n ? n - 1 : 0;
    app_redraw();
}

static void on_catalog(HttpResponse *r, void *user)
{
    Row *row = user;
    row->req = 0;
    row->loading = 0;
    if (r->status != 200) {
        if (!row->n) row->failed = 1;
        LOGW("catalog %s failed: %ld %s", row->title, r->status, r->error);
        app_redraw();
        return;
    }
    apply_catalog(row, r->body);
    if (row->n) app_cache_put(row->url, r->body, r->len);
}

static void fetch_row(Row *r)
{
    if (r->kind != ROW_CATALOG || r->requested || r->addon >= g_app.n_addons) return;
    const Addon *ad = &g_app.addons[r->addon];
    const AddonCatalog *cat = &ad->manifest.catalogs[r->catalog];
    addon_url_catalog(ad, cat->type, cat->id, NULL, r->url, sizeof r->url);
    r->requested = 1;
    char *cached = app_cache_get(r->url, 7 * 24 * 3600);
    if (cached) { apply_catalog(r, cached); free(cached); }
    r->loading = r->n == 0;
    r->req = http_request_async(r->url, NULL, on_catalog, r);
}

static int row_visible(const Row *r)
{
    if (r->kind != ROW_CATALOG) return r->n > 0;
    return r->n > 0 || r->loading || (!r->failed && !r->requested);
}

static int next_visible(const Home *h, int from, int dir)
{
    for (int i = from + dir; i >= 0 && i < h->n_rows; i += dir) if (row_visible(h->rows[i])) return i;
    return -2;
}

/* Showcase: the first catalog row with titles feeds the carousel. */
static const Row *featured_row(const Home *h)
{
    for (int i = 0; i < h->n_rows; i++) if (h->rows[i]->kind == ROW_CATALOG && h->rows[i]->n) return h->rows[i];
    return NULL;
}
static int featured_count(const Home *h) { const Row *r = featured_row(h); return r ? (r->n < SHOWCASE_N ? r->n : SHOWCASE_N) : 0; }

static const MetaPreview *hero_item(const Home *h)
{
    if (g_app.home_style == HOME_SHOWCASE) {
        const Row *r = featured_row(h);
        if (!r) return NULL;
        int n = featured_count(h);
        return &r->metas[h->feat % n];
    }
    int ri = h->focus_row >= 0 ? h->focus_row : next_visible(h, -1, 1);
    if (ri < 0) return NULL;
    const Row *r = h->rows[ri];
    return r->n ? &r->metas[r->focus < r->n ? r->focus : 0] : NULL;
}

/* ---- update ---- */

static void on_desc(HttpResponse *r, void *user)
{
    char *id = user;
    if (strcmp(g_desc_pending, id) == 0) g_desc_pending[0] = 0;
    MetaItem item;
    if (r->status == 200 && addon_parse_meta(r->body, &item) == 0) {
        int slot = g_n_desc < DESC_CACHE ? g_n_desc++ : g_desc_next;
        if (g_n_desc == DESC_CACHE) g_desc_next = (g_desc_next + 1) % DESC_CACHE;
        if (g_desc[slot].id[0]) meta_preview_free(&g_desc[slot].p);
        snprintf(g_desc[slot].id, sizeof g_desc[slot].id, "%s", id);
        meta_preview_copy(&g_desc[slot].p, &item.p);
        if (g_home_live) {   /* every row that shows this title */
            for (int ri = 0; ri < g_home_live->n_rows; ri++) {
                Row *row = g_home_live->rows[ri];
                for (int i = 0; i < row->n; i++) if (strcmp(row->metas[i].id, id) == 0) fill_from(&row->metas[i], &item.p);
            }
            g_home_live->hero_text_t = 0;
        }
        meta_item_free(&item);
        app_redraw();
    }
    free(id);
}

static void request_desc(Home *h, const MetaPreview *m)
{
    (void)h;
    for (int i = 0; i < g_app.n_addons; i++) {
        if (!addon_supports(&g_app.addons[i], "meta", m->type, m->id)) continue;
        char url[1024];
        addon_url_meta(&g_app.addons[i], m->type, m->id, url, sizeof url);
        snprintf(g_desc_pending, sizeof g_desc_pending, "%s", m->id);
        http_request_async(url, NULL, on_desc, strdup(m->id));
        return;
    }
}

static void update(Screen *s, float dt)
{
    Home *h = s->st;
    if (h->addons_seen != g_app.addons_version || h->rows_seen != g_app.home_rows_version) rebuild_rows(h);
    if (h->watched_seen != g_app.hide_watched) {   /* the toggle changed: catalogs reload from cache */
        h->watched_seen = g_app.hide_watched;
        for (int i = 0; i < h->n_rows; i++) if (h->rows[i]->kind == ROW_CATALOG) { h->rows[i]->requested = 0; h->rows[i]->req = 0; }
    }
    if (h->library_seen != g_app.library_version) {
        for (int i = 0; i < h->n_rows; i++) if (h->rows[i]->kind != ROW_CATALOG) build_library_row(h->rows[i]);
        h->library_seen = g_app.library_version;
    }
    int fetched = 0;
    for (int i = 0; i < h->n_rows && fetched < 4; i++) {
        int reach = g_app.low_memory ? 1 : 3, first = g_app.low_memory ? 3 : 5;
        int near = h->focus_row < 0 ? i < first : (i >= h->focus_row - 1 && i <= h->focus_row + reach);
        if (near && !h->rows[i]->requested && h->rows[i]->kind == ROW_CATALOG) { fetch_row(h->rows[i]); fetched++; }
    }
    if (h->focus_row >= 0 && (h->focus_row >= h->n_rows || !row_visible(h->rows[h->focus_row]))) {
        int n = next_visible(h, h->focus_row, 1);
        if (n < 0) n = next_visible(h, h->focus_row, -1);
        h->focus_row = n < 0 ? -1 : n;
    }

    if (!h->landed && h->focus_row == -1) {   /* open on the content; the rail is a Left press away */
        int loading = 0;
        for (int i = 0; i < h->n_rows; i++) if (h->rows[i]->kind == ROW_CATALOG && (h->rows[i]->loading || !h->rows[i]->requested)) loading = 1;
        if (g_app.home_style == HOME_SHOWCASE && featured_count(h)) { h->focus_row = FOCUS_SHOWCASE; h->landed = 1; }
        else if (g_app.home_style != HOME_SHOWCASE || !loading) { int n = next_visible(h, -1, 1); if (n >= 0) { move_focus(h, n, -1); h->landed = 1; } }
    }
    if (g_app.home_style == HOME_SHOWCASE && featured_count(h) > 1) {
        h->feat_timer += dt;
        if (h->feat_timer > SHOWCASE_EVERY && h->focus_row != FOCUS_SHOWCASE) { h->feat_timer = 0; h->feat = (h->feat + 1) % featured_count(h); app_redraw(); }
        if (h->focus_row == FOCUS_SHOWCASE) h->feat_timer = 0;
    } else if (h->focus_row == FOCUS_SHOWCASE) h->focus_row = -1;

    float y = 0;
    for (int i = 0; i < h->n_rows && i < h->focus_row; i++) if (row_visible(h->rows[i])) y += row_h(h->rows[i]);
    h->scroll_y = ui_ease(h->scroll_y, h->focus_row < 0 ? 0 : y, NULL, dt, 14);
    for (int i = 0; i < h->n_rows; i++) {
        Row *r = h->rows[i];
        int vis = visible_cards(r);
        int first = r->focus - (vis - 2);
        if (first < 0) first = 0;
        r->scroll_x = ui_ease(r->scroll_x, first * (card_w(r) + GAP), NULL, dt, 16);
    }
    h->focus_t = ui_ease(h->focus_t * 100, 100, NULL, dt, 16) / 100;
    h->prev_t = ui_ease(h->prev_t * 100, 0, NULL, dt, 16) / 100;
    h->hero_fade = ui_ease(h->hero_fade * 100, 100, NULL, dt, 7) / 100;
    h->hero_text_t = ui_ease(h->hero_text_t * 100, 100, NULL, dt, 10) / 100;

    const MetaPreview *hero = hero_item(h);
    if (hero && (!hero->description || !*hero->description) && strcmp(hero->id, g_desc_pending) != 0 && !desc_cached(hero->id)) request_desc(h, hero);
    if (hero && strcmp(hero->id, h->hero_id) != 0) {
        snprintf(h->hero_id, sizeof h->hero_id, "%s", hero->id);
        char buf[512];
        const char *bg = app_art_background(hero, buf, sizeof buf);
        snprintf(h->hero_prev_bg, sizeof h->hero_prev_bg, "%s", h->hero_bg);
        snprintf(h->hero_bg, sizeof h->hero_bg, "%s", bg ? bg : "");
        h->hero_fade = 0;
        h->hero_text_t = 0;
    }

    /* prefetch around the focus and the start of the next row */
    if (h->focus_row >= 0) {
        Row *r = h->rows[h->focus_row];
        char buf[512];
        for (int j = r->focus - 2; j < r->focus + visible_cards(r) + 2; j++) {
            if (j < 0 || j >= r->n) continue;
            image_prefetch(r->kind == ROW_CONTINUE ? app_art_background(&r->metas[j], buf, sizeof buf) : app_art_poster(&r->metas[j], buf, sizeof buf));
            if (j >= r->focus - 1 && j <= r->focus + 1) image_prefetch(app_art_background(&r->metas[j], buf, sizeof buf));
        }
        int nr = next_visible(h, h->focus_row, 1);
        if (nr >= 0 && !g_app.low_memory) { Row *n2 = h->rows[nr]; for (int j = 0; j < n2->n && j < 8; j++) image_prefetch(app_art_poster(&n2->metas[j], buf, sizeof buf)); }
    }
    if (g_app.addons_loading) app_redraw();
}

/* ---- drawing ---- */

static void draw_showcase_controls(const Home *h);

static void draw_hero(Home *h, const MetaPreview *p)
{
    float k = g_app.fx_light ? 1.0f : h->hero_text_t;
    int a = (int)(255 * k);
    float x = LX, y = 168 + (1 - k) * 18;
    float maxw = 860;
    char buf[512];
    const char *logo = app_art_logo(p, buf, sizeof buf);
    int lw = 0, lh = 0;
    unsigned ltex = logo ? image_get(logo, &lw, &lh) : 0;
    if (ltex && lh > 0) {
        float w = 600, hh = w * lh / lw;
        if (hh > 170) { hh = 170; w = hh * lw / lh; }
        gfx_tex(ltex, x, y + (170 - hh), w, hh, RGBA(255, 255, 255, a));
        y += 190;
    } else {
        int fs = font_measure(p->name, FONT_TITLE | FONT_B) > maxw ? (FONT_XL | FONT_B) : (FONT_TITLE | FONT_B);
        int lines = font_draw_wrapped(p->name, 0, 0, fs, 0, maxw, 2);
        float th = lines * font_height(fs);
        float ty = y + 170 - th;
        Color sh = RGBA(0, 0, 0, a / 2);
        for (int i = 0; i < lines; i++) (void)i;
        font_draw_wrapped(p->name, x + 3, ty + 3, fs, sh, maxw, 2);
        font_draw_wrapped(p->name, x, ty, fs, COLOR_ALPHA(THEME_TEXT, a), maxw, 2);
        y += 190;
    }
    /* meta line */
    float mx = x;
    if (p->imdb_rating && *p->imdb_rating) mx += ui_imdb(p->imdb_rating, mx, y) + 4;
    const char *parts[5]; int np = 0;
    if (p->release_info) parts[np++] = p->release_info;
    if (p->runtime) parts[np++] = p->runtime;
    if (p->type) parts[np++] = strcmp(p->type, "series") == 0 ? "Series" : strcmp(p->type, "movie") == 0 ? "Movie" : p->type;
    for (int i = 0; i < np; i++) {
        if (mx > x) mx = ui_dot_sep(mx, y, COLOR_ALPHA(THEME_TEXT_MUTED, a));
        font_draw(parts[i], mx, y + (36 - font_height(FONT_S | FONT_B)) / 2, FONT_S | FONT_B, COLOR_ALPHA(THEME_TEXT_DIM, a));
        mx += font_measure(parts[i], FONT_S | FONT_B);
    }
    y += 54;
    float gx = x;
    for (int g = 0; g < p->genres.n && g < 4; g++) gx += ui_chip_outline(p->genres.v[g], gx, y, RGBA(0xFF, 0xFF, 0xFF, (int)(0x50 * k)), COLOR_ALPHA(THEME_TEXT_DIM, a)) + 10;
    if (p->genres.n) y += 54;
    if (p->description) font_draw_wrapped(p->description, x, y, FONT_S, COLOR_ALPHA(THEME_TEXT_DIM, a), maxw, 4);
}

static void draw_showcase_controls(const Home *h)
{
    int n = featured_count(h);
    if (!n) return;
    int foc = h->focus_row == FOCUS_SHOWCASE;
    float y = rows_y() - 108;
    ui_pill("View title", 1, ICON_CHEVRON_RIGHT, LX, y, 60, PILL_PRIMARY, foc);
    /* page dots at the right, the current one stretched */
    float dw = 10, gap = 10, cur_w = 34, total = (n - 1) * (dw + gap) + cur_w;
    float x = SNTV_UI_W - THEME_MARGIN - total - 44, cy = y + 30;
    ui_icon(ICON_CHEVRON_LEFT, x - 26, cy, 16, foc ? THEME_TEXT : THEME_TEXT_MUTED);
    for (int i = 0; i < n; i++) {
        int on = i == h->feat % n;
        float w = on ? cur_w : dw;
        gfx_rrect(x, cy - 5, w, 10, 5, on ? THEME_TEXT : RGBA(0xFF, 0xFF, 0xFF, 0x60));
        x += w + gap;
    }
    ui_icon(ICON_CHEVRON_RIGHT, x + 14, cy, 16, foc ? THEME_TEXT : THEME_TEXT_MUTED);
    if (foc) font_draw("Left/Right: browse  \xC2\xB7  OK: open  \xC2\xB7  Down: rows", SNTV_UI_W - THEME_MARGIN - font_measure("Left/Right: browse  \xC2\xB7  OK: open  \xC2\xB7  Down: rows", FONT_XS), cy + 22, FONT_XS, THEME_TEXT_MUTED);
}

static void draw_row(Home *h, int ri, float y)
{
    Row *r = h->rows[ri];
    int focused_row = h->focus_row == ri;
    float rowk = focused_row ? 1.0f : 0.55f;
    font_draw(r->title, LX, y, FONT_M | FONT_B, COLOR_ALPHA(THEME_TEXT, (int)(255 * rowk)));
    if (r->kind == ROW_CATALOG && r->addon < g_app.n_addons && focused_row) {
        float tw = font_measure(r->title, FONT_M | FONT_B);
        font_draw(g_app.addons[r->addon].manifest.name, LX + tw + 18, y + 6, FONT_S, THEME_TEXT_MUTED);
    }
    float cy = y + TITLE_H, cw = card_w(r), ch = card_h(r);
    if (!r->n) {
        if (r->loading || !r->requested) for (int j = 0; j < 8; j++) ui_skeleton(LX + j * (cw + GAP), cy, cw, ch, R_CARD);
        else if (r->failed) font_draw("Couldn't load this catalog. It will retry next time.", LX, cy + 20, FONT_S, THEME_TEXT_MUTED);
        return;
    }
    int first = (int)(r->scroll_x / (cw + GAP)) - 1;
    if (first < 0) first = 0;
    char buf[512];
    /* draw unfocused first so the focused card's shadow and ring sit on top */
    for (int pass = 0; pass < 2; pass++)
        for (int j = first; j < r->n && j < first + visible_cards(r) + 3; j++) {
            float px = LX + j * (cw + GAP) - r->scroll_x;
            if (px > SNTV_UI_W + 40 || px + cw < -40) continue;
            int is_focus = focused_row && r->focus == j;
            int is_prev = h->prev_row == ri && h->prev_col == j && !is_focus;
            if ((pass == 1) != (is_focus || is_prev)) continue;
            float f = is_focus ? (g_app.fx_light ? 1.0f : h->focus_t) : is_prev ? h->prev_t : 0;
            float dim = focused_row ? 0 : 0.45f;
            const MetaPreview *m = &r->metas[j];
            if (r->kind == ROW_CONTINUE) {
                const char *bg = app_art_background(m, buf, sizeof buf);
                ui_card(bg, px, cy, cw, ch, f, dim, m->name, 0);
                float sc = ui_focus_scale(f), w2 = cw * sc, h2 = ch * sc, x2 = px - (w2 - cw) / 2, y2 = cy - (h2 - ch) / 2;
                /* label + progress inside the card */
                gfx_clip_push(x2, y2, w2, h2);
                gfx_gradient_v(x2, y2 + h2 * 0.45f, w2, h2 * 0.55f + 1, RGBA(0, 0, 0, 0), RGBA(0, 0, 0, 0xE0));
                gfx_clip_pop();
                LibraryItem *li = app_library_find(m->id);
                char sub[64] = "";
                if (li && li->video_id) {
                    int se = 0, ep = 0;
                    const char *c1 = strchr(li->video_id, ':');
                    if (c1 && sscanf(c1, ":%d:%d", &se, &ep) == 2) snprintf(sub, sizeof sub, "S%d  E%d", se, ep);
                }
                font_draw_fit(m->name, x2 + 20, y2 + h2 - 76, FONT_S | FONT_B, THEME_TEXT, w2 - 40);
                if (sub[0]) font_draw(sub, x2 + 20, y2 + h2 - 48, FONT_XS, THEME_TEXT_DIM);
                if (li && li->duration) ui_progress(x2 + 20, y2 + h2 - 18, w2 - 40, 6, (float)li->time_offset / (float)li->duration, THEME_ACCENT);
            } else {
                ui_card(app_art_poster(m, buf, sizeof buf), px, cy, cw, ch, f, dim, m->name, 0);
            }
            if (is_focus && r->kind != ROW_CONTINUE) {
                float sc = ui_focus_scale(f);
                font_draw_fit(m->name, px - (cw * sc - cw) / 2, cy + ch * sc - (ch * sc - ch) / 2 + 14, FONT_XS | FONT_B,
                              COLOR_ALPHA(THEME_TEXT, (int)(255 * f)), cw * sc);
            }
        }
}

static void draw(Screen *s)
{
    Home *h = s->st;
    const MetaPreview *hero = hero_item(h);
    ui_background(hero ? image_tint(h->hero_bg) : 0);

    if (hero && g_app.home_style != HOME_COMPACT) {
        int iw, ih, pw, ph;
        float fade = g_app.fx_light ? 1.0f : h->hero_fade;
        unsigned prev = h->hero_prev_bg[0] && fade < 1 ? image_get(h->hero_prev_bg, &pw, &ph) : 0;
        unsigned cur = h->hero_bg[0] ? image_get(h->hero_bg, &iw, &ih) : 0;
        float bh = SNTV_UI_H * (g_app.home_style == HOME_SHOWCASE ? 0.92f : 0.86f);
        ui_backdrop_fades(1180, 360, bh);
        if (prev) ui_backdrop_image(prev, pw, ph, 1 - fade, bh);
        if (cur) ui_backdrop_image(cur, iw, ih, fade, bh);
        ui_text_scrim(1250, bh);
        draw_hero(h, hero);
        if (g_app.home_style == HOME_SHOWCASE) draw_showcase_controls(h);
    } else if (hero) {   /* Compact: one line about the focused title, the rows take the screen */
        char line[300];
        const char *type = hero->type ? (strcmp(hero->type, "series") == 0 ? "Series" : strcmp(hero->type, "movie") == 0 ? "Movie" : hero->type) : "";
        snprintf(line, sizeof line, "%s%s%s%s%s%s%s", hero->name ? hero->name : "", hero->release_info ? "   \xC2\xB7   " : "", hero->release_info ? hero->release_info : "",
                 type[0] ? "   \xC2\xB7   " : "", type, hero->genres.n ? "   \xC2\xB7   " : "", hero->genres.n ? hero->genres.v[0] : "");
        font_draw_fit(line, LX, 108, FONT_S | FONT_B, THEME_TEXT_DIM, SNTV_UI_W - LX - THEME_MARGIN - 640);
    }

    gfx_clip_push(0, rows_y() - 24, SNTV_UI_W, SNTV_UI_H - rows_y() + 24);
    float y = rows_y() - h->scroll_y;
    for (int i = 0; i < h->n_rows; i++) {
        Row *r = h->rows[i];
        if (!row_visible(r)) continue;
        float rh = row_h(r);
        if (y + rh > rows_y() - 40 && y < SNTV_UI_H) draw_row(h, i, y);
        y += rh;
    }
    gfx_clip_pop();

    int any = 0;
    for (int i = 0; i < h->n_rows; i++) if (row_visible(h->rows[i])) any = 1;
    if (!any) {
        float cy = SNTV_UI_H / 2 + 60;
        if (g_app.addons_loading) { ui_spinner(SNTV_UI_W / 2, cy, 30); font_draw_center("Loading your addons", SNTV_UI_W / 2, cy + 60, FONT_M | FONT_B, THEME_TEXT_DIM); }
        else {
            ui_icon(ICON_LIBRARY, SNTV_UI_W / 2, cy - 40, 80, THEME_TEXT_MUTED);
            font_draw_center(g_app.addons_error[0] ? g_app.addons_error : "No catalogs yet", SNTV_UI_W / 2, cy + 30, FONT_L | FONT_B, THEME_TEXT);
            font_draw_center("Sign in, or install a catalog addon from Settings.", SNTV_UI_W / 2, cy + 90, FONT_S, THEME_TEXT_DIM);
        }
    }
    rail_draw(RAIL_HOME, h->focus_row == -1 ? h->focus_nav : -1);
}

/* ---- input ---- */

static void move_focus(Home *h, int row, int col)
{
    if (h->focus_row >= 0) { h->prev_row = h->focus_row; h->prev_col = h->rows[h->focus_row]->focus; h->prev_t = h->focus_t; }
    if (row >= 0 && col >= 0) h->rows[row]->focus = col;
    h->focus_row = row;
    h->focus_t = 0;
}

static int key(Screen *s, KeyAction a)
{
    Home *h = s->st;
    app_redraw();
    if (a == KEY_SEARCH) { app_push(screen_search_create()); return 1; }
    if (h->focus_row == FOCUS_SHOWCASE) {   /* the carousel View button */
        int n = featured_count(h);
        switch (a) {
        case KEY_LEFT: if (n) { h->feat = (h->feat + n - 1) % n; h->feat_timer = 0; } return 1;
        case KEY_RIGHT: if (n) { h->feat = (h->feat + 1) % n; h->feat_timer = 0; } return 1;
        case KEY_UP: h->focus_row = -1; return 1;
        case KEY_DOWN: { int r = next_visible(h, -1, 1); if (r >= 0) move_focus(h, r, -1); return 1; }
        case KEY_OK: { const MetaPreview *m = hero_item(h); if (m) app_push(screen_detail_create(m)); return 1; }
        case KEY_BACK: h->focus_row = -1; h->focus_nav = RAIL_HOME; return 1;
        default: return 1;
        }
    }
    if (h->focus_row == -1) {   /* the side rail */
        int r = rail_key(a, &h->focus_nav, RAIL_HOME);
        if (r == 2) {   /* Right: back into the content */
            if (g_app.home_style == HOME_SHOWCASE && featured_count(h)) h->focus_row = FOCUS_SHOWCASE;
            else { int n = next_visible(h, -1, 1); if (n >= 0) move_focus(h, n, -1); }
            return 1;
        }
        if (r == 1) return 1;
        if (a == KEY_BACK) { g_app.quit = 1; return 1; }
        return 0;
    }
    Row *r = h->rows[h->focus_row];
    switch (a) {
    case KEY_UP: {
        int n = next_visible(h, h->focus_row, -1);
        if (n < 0 && g_app.home_style == HOME_SHOWCASE && featured_count(h)) { h->prev_row = h->focus_row; h->prev_col = r->focus; h->prev_t = h->focus_t; h->focus_row = FOCUS_SHOWCASE; return 1; }
        move_focus(h, n >= 0 ? n : -1, -1); return 1;
    }
    case KEY_DOWN: { int n = next_visible(h, h->focus_row, 1); if (n >= 0) { move_focus(h, n, -1); return 1; } return 0; }
    case KEY_LEFT:
        if (r->focus > 0) move_focus(h, h->focus_row, r->focus - 1);
        else { h->focus_nav = RAIL_HOME; move_focus(h, -1, -1); }   /* first card: the rail */
        return 1;
    case KEY_RIGHT: if (r->focus < r->n - 1) move_focus(h, h->focus_row, r->focus + 1); return 1;
    case KEY_OK: if (r->focus < r->n) app_push(screen_detail_create(&r->metas[r->focus])); return 1;
    case KEY_BACK:
        if (h->focus_row != next_visible(h, -1, 1) || r->focus > 0) { r->focus = 0; move_focus(h, next_visible(h, -1, 1), 0); return 1; }
        h->focus_nav = RAIL_HOME; move_focus(h, -1, -1);   /* then the rail; Back there leaves the app */
        return 1;
    default: return 0;
    }
}

static void resume(Screen *s) { (void)s; app_redraw(); }

static void destroy(Screen *s)
{
    Home *h = s->st;
    if (g_home_live == h) g_home_live = NULL;
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
    h->prev_row = -1;
    h->focus_t = 1;
    h->hero_fade = 1;
    h->hero_text_t = 1;
    h->watched_seen = g_app.hide_watched;
    s->name = "home"; s->st = h;
    g_home_live = h;
    s->update = update; s->draw = draw; s->key = key; s->destroy = destroy; s->resume = resume;
    rebuild_rows(h);
    return s;
}
