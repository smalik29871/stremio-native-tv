/* Home: cinematic hero for the focused title over its backdrop, ambient light
 * tinted by the artwork, then rows: Continue Watching (landscape cards with
 * progress), My Library, and one row per addon catalog. Everything eases;
 * rows are served from the disk cache first and refreshed in the background. */
#include "../app.h"
#include "../perfstat.h"
#include "../lists.h"
#include "../tracking.h"
#include "../http.h"
#include "../log.h"
#include "../vendor/cJSON.h"
#include "../store.h"
#include "font.h"
#include "../newep.h"
#include "theme.h"
#include "bgtrailer.h"
#include "widgets.h"
#include "screen_options.h"
#include "rail.h"
#include "island.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { ROW_CATALOG, ROW_CONTINUE, ROW_LIBRARY, ROW_LIST, ROW_TRAKT };
enum { NAV_SEARCH, NAV_SETTINGS, NAV_ACCOUNT, NAV_COUNT };

#define LX RAIL_LX           /* content starts right of the side rail */
#define ROWS_Y 610.0f          /* Cinematic; the other styles move it (rows_y) */
#define SHOWCASE_N 10         /* titles in the Showcase carousel */
#define SHOWCASE_EVERY 8.0f   /* seconds before the carousel moves by itself */
#define HERO_REST 0.22f       /* the focus rests this long before the hero changes (not on every card scrolled past) */
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
    float scroll_x, scroll_xv;   /* position and spring velocity */
    float arrived_at;             /* when titles first replaced the placeholders (cards rise in) */
    char url[1024];
} Row;

typedef struct {
    Row **rows; int n_rows;
    int focus_row;            /* -1 = nav bar */
    int focus_nav;
    int rail_from;            /* the row (or FOCUS_SHOWCASE) the rail was opened from */
    float exit_ask;           /* time Back was pressed on the rail; a second Back soon after leaves */
    float scroll_y, scroll_yv;
    float nudge_at; int nudge_dir;   /* pressed past the end: the row (dir ±1) or the list (±2) nudges and settles */
    int addons_seen, library_seen, rows_seen, watched_seen, lists_seen, trakt_seen;
    float focus_t;            /* 0..1 grow-in of the focused card */
    int prev_row, prev_col; float prev_t;
    char hero_id[256];
    char want_id[256]; float want_since;   /* the focused title, and since when (HERO_REST) */
    char hero_bg[1024], hero_prev_bg[1024]; float hero_fade;
    float hero_text_t;        /* fade/slide of the hero text on change */
    int landed;               /* first focus went to the content */
    int feat;                 /* Showcase: which of the featured row's titles is up */
    float feat_timer;
    Peek peek;                /* details beside the focused poster */
} Home;

static void move_focus(Home *h, int row, int col);
static float rows_y(void) { return g_app.home_style == HOME_COMPACT ? 150.0f : g_theme.id == THEME_APERTURE ? 654.0f : g_app.home_style == HOME_SHOWCASE ? 660.0f : ROWS_Y; }

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

/* Continue Watching's order: when you last watched, or when a new episode aired */
static int64_t cw_when(const LibraryItem *li)
{
    const NewEp *ne = newep_get(li->id);
    return ne && ne->released_ms > li->last_watched_ms ? ne->released_ms : li->last_watched_ms;
}

static void build_library_row(Row *r)
{
    char keep[160] = "";   /* the focused title, found again after the re-sort */
    if (r->focus < r->n && r->metas[r->focus].id) snprintf(keep, sizeof keep, "%s", r->metas[r->focus].id);
    meta_previews_free(r->metas, r->n);
    r->metas = NULL; r->n = 0;
    if (r->kind == ROW_TRAKT) {   /* the Trakt watchlist or recommendations */
        r->n = tracking_row(r->catalog, &r->metas);
        if (keep[0]) for (int i = 0; i < r->n; i++) if (strcmp(r->metas[i].id, keep) == 0) { r->focus = i; break; }
        if (r->focus >= r->n) r->focus = r->n ? r->n - 1 : 0;
        return;
    }
    if (r->kind == ROW_LIST) {   /* a saved list, newest first */
        r->n = lists_items(r->catalog, &r->metas);
        if (keep[0]) for (int i = 0; i < r->n; i++) if (strcmp(r->metas[i].id, keep) == 0) { r->focus = i; break; }
        if (r->focus >= r->n) r->focus = r->n ? r->n - 1 : 0;
        return;
    }
    if (!g_app.n_library) return;
    LibraryItem **sorted = malloc(sizeof *sorted * (size_t)g_app.n_library);
    int n = 0;
    for (int i = 0; i < g_app.n_library; i++) {
        LibraryItem *li = &g_app.library[i];
        int take = r->kind == ROW_CONTINUE ? ((li->time_offset > 0 && (!li->removed || li->temp)) || newep_get(li->id)) : (!li->removed && !li->temp);
        if (take) sorted[n++] = li;
    }
    for (int i = 1; i < n; i++)
        for (int j = i; j > 0; j--) {
            int64_t a = r->kind == ROW_CONTINUE ? cw_when(sorted[j]) : sorted[j]->mtime_ms;
            int64_t b = r->kind == ROW_CONTINUE ? cw_when(sorted[j - 1]) : sorted[j - 1]->mtime_ms;
            if (a <= b) break;
            LibraryItem *t = sorted[j]; sorted[j] = sorted[j - 1]; sorted[j - 1] = t;
        }
    if (n > 30) n = 30;
    r->metas = calloc((size_t)n + 1, sizeof(MetaPreview));
    for (int i = 0; i < n; i++) r->metas[i] = preview_from_item(sorted[i]);
    r->n = n;
    if (n > 0) perf_mark("first row");
    if (keep[0]) for (int i = 0; i < n; i++) if (r->metas[i].id && strcmp(r->metas[i].id, keep) == 0) { r->focus = i; break; }
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
    /* rows can be reordered or hidden: the focus follows its row, not its position */
    char keep[200] = "", keep_rail[200] = "";
    if (h->focus_row >= 0 && h->focus_row < n_old) snprintf(keep, sizeof keep, "%s", old[h->focus_row]->key);
    if (h->rail_from >= 0 && h->rail_from < n_old) snprintf(keep_rail, sizeof keep_rail, "%s", old[h->rail_from]->key);
    HomeRowDef defs[160];
    int nd = app_home_rows(defs, 160);
    h->rows = calloc((size_t)nd + 1, sizeof(Row *));
    h->n_rows = 0;
    for (int d = 0; d < nd; d++) {
        if (defs[d].hidden) continue;
        Row *r = NULL;
        for (int i = 0; i < n_old; i++) if (old[i] && strcmp(old[i]->key, defs[d].key) == 0) { r = old[i]; old[i] = NULL; break; }
        if (!r) { r = new_row(defs[d].kind == 0 ? ROW_CONTINUE : defs[d].kind == 1 ? ROW_LIBRARY : defs[d].kind == 3 ? ROW_LIST : defs[d].kind == 4 ? ROW_TRAKT : ROW_CATALOG, defs[d].title); snprintf(r->key, sizeof r->key, "%s", defs[d].key); }
        if (r->kind == ROW_LIST || r->kind == ROW_TRAKT) r->catalog = defs[d].catalog;   /* which list */
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
    for (int i = 0; i < h->n_rows; i++) {
        if (keep[0] && strcmp(h->rows[i]->key, keep) == 0) h->focus_row = i;
        if (keep_rail[0] && strcmp(h->rows[i]->key, keep_rail) == 0) h->rail_from = i;
    }
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

static void apply_metas(Row *row, MetaPreview *m, int n)
{
    drop_watched(&m, &n);
    if (g_app.low_memory && n > 40) { for (int i = 40; i < n; i++) meta_preview_free(&m[i]); n = 40; }   /* memory saver: shorter rows */
    char *focused = row->n && row->focus < row->n ? strdup(row->metas[row->focus].id) : NULL;
    if (!row->n && n) row->arrived_at = g_app.time;
    meta_previews_free(row->metas, row->n);
    row->metas = m; row->n = n;
    if (n > 0) { perf_mark("first row"); perf_mark("first catalog"); }
    for (int i = 0; i < n && i < 6; i++) app_ambient_offer(&m[i]);
    if (focused) { for (int i = 0; i < n; i++) if (strcmp(m[i].id, focused) == 0) row->focus = i; free(focused); }
    if (row->focus >= n) row->focus = n ? n - 1 : 0;
    app_redraw();
}

static void apply_catalog(Row *row, const char *json)
{
    MetaPreview *m; int n;
    if (addon_parse_metas(json, &m, &n) != 0) return;
    apply_metas(row, m, n);
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
    ParsedMetas *pm = r->parsed;   /* parsed and cached on the network thread */
    if (pm) { r->parsed = NULL; apply_metas(row, pm->m, pm->n); free(pm); }
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
    r->req = app_fetch_metas(r->url, 1, on_catalog, r);
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

static const MetaPreview *hero_item(const Home *h);

/* The title the hero shows: the focused one once the focus has rested on it. */
static const MetaPreview *shown_hero(const Home *h)
{
    if (h->hero_id[0])
        for (int i = 0; i < h->n_rows; i++)
            for (int j = 0; j < h->rows[i]->n; j++) if (strcmp(h->rows[i]->metas[j].id, h->hero_id) == 0) return &h->rows[i]->metas[j];
    return hero_item(h);
}

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
        app_meta_put(item.p.type, id, r->body, r->len);
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

/* What Home shows first, kept so the next launch can read it from the disk cache while
 * the fonts load (main.c): the hero's backdrop and logo, the posters of the top rows. */
static void note_first_view(Home *h)
{
    static double last_saved = -100;
    if (g_app.time - last_saved < 30) return;
    cJSON *a = cJSON_CreateArray();
    char buf[1024];
    if (h->hero_bg[0]) cJSON_AddItemToArray(a, cJSON_CreateString(h->hero_bg));
    int rows = 0;
    for (int i = 0; i < h->n_rows && rows < 2; i++) {
        const Row *r = h->rows[i];
        if (!r->n) continue;
        rows++;
        for (int j = 0; j < r->n && j < visible_cards(r) + 1; j++) {
            const char *u = r->kind == ROW_CONTINUE ? app_art_background(&r->metas[j], buf, sizeof buf) : app_art_poster(&r->metas[j], buf, sizeof buf);
            if (u && *u) cJSON_AddItemToArray(a, cJSON_CreateString(u));
        }
    }
    if (cJSON_GetArraySize(a) < 3 || cJSON_Compare(a, store_get_json("homeFirstView"), 1)) { cJSON_Delete(a); return; }
    LOGI("home: first view kept (%d pictures)", cJSON_GetArraySize(a));
    store_set_json("homeFirstView", a);   /* the store owns it now */
    store_save();
    last_saved = g_app.time;
}

static void update(Screen *s, float dt)
{
    Home *h = s->st;
    if (g_app.time > 4) note_first_view(h);
    if (h->addons_seen != g_app.addons_version || h->rows_seen != g_app.home_rows_version) rebuild_rows(h);
    if (h->watched_seen != g_app.hide_watched) {   /* the toggle changed: catalogs reload from cache */
        h->watched_seen = g_app.hide_watched;
        for (int i = 0; i < h->n_rows; i++) if (h->rows[i]->kind == ROW_CATALOG) { h->rows[i]->requested = 0; h->rows[i]->req = 0; }
    }
    if (h->library_seen != g_app.library_version) {
        for (int i = 0; i < h->n_rows; i++) if (h->rows[i]->kind == ROW_CONTINUE || h->rows[i]->kind == ROW_LIBRARY) build_library_row(h->rows[i]);
        h->library_seen = g_app.library_version;
        if (g_app.last_input <= 0 && h->focus_row > 0) {   /* a row filled in above before any key (a new episode): start there */
            int n = next_visible(h, -1, 1);
            if (n >= 0 && n < h->focus_row) move_focus(h, n, -1);
        }
    }
    if (h->trakt_seen != tracking_version()) {   /* signed in or out of Trakt, or its rows arrived */
        h->trakt_seen = tracking_version();
        rebuild_rows(h);
    }
    if (h->lists_seen != lists_version()) {   /* saved to a list, or another profile's lists */
        for (int i = 0; i < h->n_rows; i++) if (h->rows[i]->kind == ROW_LIST) build_library_row(h->rows[i]);
        h->lists_seen = lists_version();
    }
    int fetched = 0, nth_catalog = 0;
    for (int i = 0; i < h->n_rows && fetched < 4; i++) {
        int reach = g_app.low_memory ? 1 : 3, first = g_app.low_memory ? 3 : 5;
        int cat = h->rows[i]->kind == ROW_CATALOG;
        /* nothing focused yet: the first catalogs, counted past empty library and list rows
         * (Showcase waits for them to place its focus) */
        int near = h->focus_row < 0 ? cat && nth_catalog < first : (i >= h->focus_row - 1 && i <= h->focus_row + reach);
        nth_catalog += cat;
        if (near && !h->rows[i]->requested && cat) { fetch_row(h->rows[i]); fetched++; }
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
        float was = h->feat_timer;
        if (!bgtrailer_shown(h->hero_id)) h->feat_timer += dt;   /* a trailer playing holds the carousel */
        if ((int)(was * 34 / SHOWCASE_EVERY) != (int)(h->feat_timer * 34 / SHOWCASE_EVERY) && !g_app.fx_light) app_redraw();   /* the dot's fill, a step a pixel */
        if (h->feat_timer > SHOWCASE_EVERY && h->focus_row != FOCUS_SHOWCASE) { h->feat_timer = 0; h->feat = (h->feat + 1) % featured_count(h); app_redraw(); }
        if (h->focus_row == FOCUS_SHOWCASE) h->feat_timer = 0;
    } else if (h->focus_row == FOCUS_SHOWCASE) h->focus_row = -1;

    float y = 0;
    for (int i = 0; i < h->n_rows && i < h->focus_row; i++) if (row_visible(h->rows[i])) y += row_h(h->rows[i]);
    if (g_app.home_style == HOME_COMPACT) {   /* no further than the last row resting on the bottom: no empty screen below it */
        float total = 0;
        for (int i = 0; i < h->n_rows; i++) if (row_visible(h->rows[i])) total += row_h(h->rows[i]);
        float most = total - (SNTV_UI_H - rows_y() - 24);
        if (y > most) y = most > 0 ? most : 0;
    }
    h->scroll_y = ui_ease(h->scroll_y, h->focus_row < 0 ? 0 : y, &h->scroll_yv, dt, 14);
    {
        const MetaPreview *pm = NULL;
        if (h->focus_row >= 0 && h->focus_row < h->n_rows) {
            const Row *r = h->rows[h->focus_row];
            if (r->kind != ROW_CONTINUE && r->focus < r->n) pm = &r->metas[r->focus];
        }
        ui_peek_step(&h->peek, pm, dt);
    }
    for (int i = 0; i < h->n_rows; i++) {
        Row *r = h->rows[i];
        int vis = visible_cards(r);
        int first = r->focus - (vis - 2);
        if (first < 0) first = 0;
        r->scroll_x = ui_ease(r->scroll_x, first * (card_w(r) + GAP), &r->scroll_xv, dt, 16);
    }
    h->focus_t = ui_ease(h->focus_t * 100, 100, NULL, dt, 16) / 100;
    h->prev_t = ui_ease(h->prev_t * 100, 0, NULL, dt, 16) / 100;
    h->hero_fade = ui_ease(h->hero_fade * 100, 100, NULL, dt, 7) / 100;
    h->hero_text_t = ui_ease(h->hero_text_t * 100, 100, NULL, dt, 10) / 100;

    const MetaPreview *hero = hero_item(h);
    if (hero && strcmp(hero->id, h->want_id) != 0) { snprintf(h->want_id, sizeof h->want_id, "%s", hero->id); h->want_since = g_app.time; }
    int settled = !h->hero_id[0] || g_app.home_style == HOME_SHOWCASE || g_app.fx_light || g_app.time - h->want_since >= HERO_REST;
    if (hero && !settled && strcmp(hero->id, h->hero_id) != 0) app_redraw();   /* look again when it has rested */
    if (hero && settled && (!hero->description || !*hero->description) && strcmp(hero->id, g_desc_pending) != 0 && !desc_cached(hero->id)) request_desc(h, hero);
    if (hero && settled && strcmp(hero->id, h->hero_id) != 0) {
        snprintf(h->hero_id, sizeof h->hero_id, "%s", hero->id);
        char buf[512];
        const char *bg = app_art_background(hero, buf, sizeof buf);
        snprintf(h->hero_prev_bg, sizeof h->hero_prev_bg, "%s", h->hero_bg);
        snprintf(h->hero_bg, sizeof h->hero_bg, "%s", bg ? bg : "");
        if (bg) ui_set_ambient_art(bg);   /* Glass blurs it behind every screen */
        h->hero_fade = 0;
        h->hero_text_t = 0;
    }
    if (g_theme.id != THEME_APERTURE && hero && settled && g_app.home_style != HOME_COMPACT && !strcmp(hero->id, h->hero_id))
        bgtrailer_want(h->hero_id, SNTV_UI_H * (g_app.home_style == HOME_SHOWCASE ? 0.92f : 0.86f), 4.0f);

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
    if (g_theme.id == THEME_APERTURE) {
        float k = g_app.fx_light ? 1 : h->hero_text_t;
        float ax = 930 - 20 * k;
        ui_aperture_art(h->hero_bg, ax, 124, SNTV_UI_W - THEME_MARGIN - ax, 442, g_app.fx_light ? 1 : h->hero_fade);
        font_draw_tracked("APERTURE  /  HOME", LX, 124, FONT_XS, THEME_TEXT_DIM, 1.2f);
        ui_hero_title(p->name, LX, 384, 680, THEME_TEXT);
        const char *parts[3] = { p->release_info, p->runtime, p->genres.n ? p->genres.v[0] : p->type };
        ui_meta_line(parts, 3, LX, 408, THEME_TEXT_DIM);
        if (p->description) font_draw_wrapped(p->description, LX, 460, FONT_S, THEME_TEXT_DIM, 660, 2);
        gfx_rect(LX, 558, 44, 3, THEME_ACCENT);
        font_draw("OK  Discover this title", LX + 62, 546, FONT_S, THEME_TEXT);
        return;
    }
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
        ui_hero_title(p->name, x, y + 170, maxw, COLOR_ALPHA(THEME_TEXT, a));
        y += 190;
    }
    /* meta line */
    float mx = x;
    if (p->imdb_rating && *p->imdb_rating) mx += ui_imdb(p->imdb_rating, mx, y) + 4;
    const char *parts[5]; int np = 0;
    if (p->release_info) parts[np++] = p->release_info;
    if (p->runtime) parts[np++] = p->runtime;
    if (p->type) parts[np++] = strcmp(p->type, "series") == 0 ? "Series" : strcmp(p->type, "movie") == 0 ? "Movie" : p->type;
    if (np) ui_meta_line(parts, np, mx > x ? mx + 8 : mx, y + (36 - font_height(FONT_S | FONT_B)) / 2, COLOR_ALPHA(THEME_TEXT_DIM, a));
    y += 54;
    float gx = x;
    for (int g = 0; g < p->genres.n && g < 4; g++) gx += ui_chip_outline(p->genres.v[g], gx, y, COLOR_ALPHA(THEME_TEXT, (int)(0x50 * k)), COLOR_ALPHA(THEME_TEXT_DIM, a)) + 10;
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
        if (on && !foc && n > 1 && !g_app.fx_light) {   /* it fills until the carousel moves on */
            float t = h->feat_timer / SHOWCASE_EVERY; if (t > 1) t = 1;
            gfx_rrect(x, cy - 5, w, 10, theme_pill_r(10), COLOR_ALPHA(THEME_ACCENT, 0x60));
            if (w * t > 10) gfx_rrect(x, cy - 5, w * t, 10, theme_pill_r(10), THEME_ACCENT);
        } else gfx_rrect(x, cy - 5, w, 10, theme_pill_r(10), on ? THEME_ACCENT : COLOR_ALPHA(THEME_TEXT, 0x60));
        x += w + gap;
    }
    ui_icon(ICON_CHEVRON_RIGHT, x + 14, cy, 16, foc ? THEME_TEXT : THEME_TEXT_MUTED);
    if (foc) font_draw("Left/Right: browse  \xC2\xB7  OK: open  \xC2\xB7  Down: rows", SNTV_UI_W - THEME_MARGIN - font_measure("Left/Right: browse  \xC2\xB7  OK: open  \xC2\xB7  Down: rows", FONT_XS), cy + 22, FONT_XS, THEME_TEXT_MUTED);
}

/* How far the nudge has things now: 18 units toward the press, back by 0.28 s. axis 1 rows, 2 the list. */
static float nudge(const Home *h, int axis)
{
    float t = g_app.time - h->nudge_at;
    if (h->nudge_dir == 0 || abs(h->nudge_dir) != axis || t < 0 || t > 0.28f || g_app.fx_light) return 0;
    app_redraw();
    float k = t < 0.07f ? t / 0.07f : 1 - (t - 0.07f) / 0.21f;
    return -18 * (h->nudge_dir > 0 ? 1 : -1) * k * k * (3 - 2 * k);
}

static void draw_row(Home *h, int ri, float y, float alpha)
{
    Row *r = h->rows[ri];
    int focused_row = h->focus_row == ri;
    int index = 0;
    for (int i = 0; i < ri; i++) if (row_visible(h->rows[i])) index++;
    const char *source = r->kind == ROW_CATALOG && r->addon < g_app.n_addons ? g_app.addons[r->addon].manifest.name : NULL;
    ui_row_header(index, r->title, source, r->n, LX, y, SNTV_UI_W - THEME_MARGIN, focused_row ? 1.0f : 0.0f);
    float cy = y + TITLE_H, cw = card_w(r), ch = card_h(r);
    if (!r->n) {
        if (r->loading || !r->requested) for (int j = 0; j < 8; j++) ui_skeleton(LX + j * (cw + GAP), cy, cw, ch, R_CARD);
        else if (r->failed) font_draw("Couldn't load this catalog. It will retry next time.", LX, cy + 20, FONT_S, THEME_TEXT_MUTED);
        return;
    }
    int first = (int)(r->scroll_x / (cw + GAP)) - 1;
    if (first < 0) first = 0;
    char buf[512];
    float px_peek = 0, f_peek = 0;
    const MetaPreview *m_peek = NULL;
    /* details open on the focused card: its neighbours slide aside to make room */
    float room = 0; int room_right = 1;
    if (focused_row && r->kind != ROW_CONTINUE && r->focus < r->n) {
        float fpx = LX + r->focus * (cw + GAP) - r->scroll_x + nudge(h, 1);
        float sc = ui_focus_scale(g_app.fx_light ? 1.0f : h->focus_t), w2 = cw * sc;
        room = ui_peek_room(&h->peek, &r->metas[r->focus], fpx - (w2 - cw) / 2, w2, ch * sc, LX, SNTV_UI_W - 40, &room_right);
    }
    /* draw unfocused first so the focused card's shadow and ring sit on top */
    for (int pass = 0; pass < 2; pass++)
        for (int j = first; j < r->n && j < first + visible_cards(r) + 3; j++) {
            float px = LX + j * (cw + GAP) - r->scroll_x + (focused_row ? nudge(h, 1) : 0);
            if (room > 0 && (room_right ? j > r->focus : j < r->focus)) px += room_right ? room : -room;
            if (px > SNTV_UI_W + 40 || px + cw < -40) continue;
            int is_focus = focused_row && r->focus == j;
            int is_prev = h->prev_row == ri && h->prev_col == j && !is_focus;
            if ((pass == 1) != (is_focus || is_prev)) continue;
            /* titles just arrived: each card rises and fades in a beat after the one before */
            float k = (g_app.time - r->arrived_at - (j - first) * 0.04f) / 0.3f, ce = 1;
            if (k < 1 && !g_app.fx_light) { ce = k < 0 ? 0 : 1 - (1 - k) * (1 - k) * (1 - k); app_redraw(); }
            gfx_set_alpha(alpha * ce);
            float ry = cy + 22 * (1 - ce);
            float f = is_focus ? (g_app.fx_light ? 1.0f : h->focus_t) : is_prev ? h->prev_t : 0;
            float dim = focused_row ? 0 : 0.45f;
            const MetaPreview *m = &r->metas[j];
            if (r->kind == ROW_CONTINUE) {
                const char *bg = app_art_background(m, buf, sizeof buf);
                ui_card(bg, px, ry, cw, ch, f, dim, m->name, 0);
                float sc = ui_focus_scale(f), w2 = cw * sc, h2 = ch * sc, x2 = px - (w2 - cw) / 2, y2 = ry - (h2 - ch) / 2 - ui_focus_lift(f);
                /* label + progress inside the card; the shade takes the card's own corners
                 * (a clipped square one showed past them) */
                gfx_rrect_grad_v(x2, y2 + h2 * 0.45f, w2, h2 * 0.55f, R_CARD, RGBA(0, 0, 0, 0), RGBA(0, 0, 0, 0xE0));
                LibraryItem *li = app_library_find(m->id);
                const NewEp *ne = newep_get(m->id);
                const char *vid = ne ? ne->video_id : li ? li->video_id : NULL;
                char sub[64] = "";
                if (vid) {
                    int se = 0, ep = 0;
                    const char *c1 = strchr(vid, ':');
                    if (c1 && sscanf(c1, ":%d:%d", &se, &ep) == 2) snprintf(sub, sizeof sub, "S%d  E%d", se, ep);
                }
                if (ne) ui_new_episodes_badge(ne->count, x2 + w2 - 16, y2 + 16);
                font_draw_fit(m->name, x2 + 20, y2 + h2 - 76, FONT_S | FONT_B, THEME_TEXT, w2 - 40);
                if (sub[0]) font_draw(sub, x2 + 20, y2 + h2 - 48, FONT_XS, THEME_TEXT_DIM);
                if (li && li->duration && !ne) ui_progress(x2 + 20, y2 + h2 - 18, w2 - 40, 6, (float)li->time_offset / (float)li->duration, THEME_ACCENT);
            } else {
                ui_card(app_art_poster(m, buf, sizeof buf), px, ry, cw, ch, f, dim, m->name, 0);
                const NewEp *ne = r->kind == ROW_LIBRARY ? newep_get(m->id) : NULL;
                if (ne) {
                    float sc = ui_focus_scale(f), w2 = cw * sc, x2 = px - (w2 - cw) / 2, y2 = ry - (ch * sc - ch) / 2 - ui_focus_lift(f);
                    ui_new_episodes_dot(ne->count, x2 + w2 - 10, y2 + 10);
                }
            }
            if (is_focus && r->kind != ROW_CONTINUE) { m_peek = m; px_peek = px; f_peek = f; }
            if (is_focus && g_app.ok_hold > 0) {   /* holding OK: the hint takes the line under the card */
                float sc = ui_focus_scale(f);
                ui_hold_hint(px - (cw * sc - cw) / 2, cy + ch * sc - (ch * sc - ch) / 2 + 14, cw * sc > 240 ? cw * sc : 240);
            } else if (is_focus && r->kind != ROW_CONTINUE && h->peek.t < 0.05f) {
                float sc = ui_focus_scale(f);
                font_draw_fit(m->name, px - (cw * sc - cw) / 2, cy + ch * sc - (ch * sc - ch) / 2 + 14, FONT_XS | FONT_B,
                              COLOR_ALPHA(THEME_TEXT, (int)(255 * f)), cw * sc);
            }
        }
    gfx_set_alpha(alpha);
    if (m_peek) {   /* in the room its neighbours made, after both passes */
        float sc = ui_focus_scale(f_peek), w2 = cw * sc, h2 = ch * sc;
        ui_peek_draw(&h->peek, m_peek, px_peek - (w2 - cw) / 2, cy - (h2 - ch) / 2 - ui_focus_lift(f_peek), w2, h2, LX, SNTV_UI_W - 40);
    }
}

static void draw(Screen *s)
{
    Home *h = s->st;
    const MetaPreview *hero = shown_hero(h);
    ui_background(hero ? image_tint(h->hero_bg) : 0);

    if (hero && g_app.home_style != HOME_COMPACT && g_theme.id == THEME_APERTURE) {
        draw_hero(h, hero);
        if (g_app.home_style == HOME_SHOWCASE) {
            /* The existing carousel remains reachable with Up and Left/Right. */
            gfx_rect(LX, 534, 690, 84, THEME_BG);
            draw_showcase_controls(h);
        }
    } else if (hero && g_app.home_style != HOME_COMPACT) {
        int iw, ih, pw, ph;
        float fade = g_app.fx_light ? 1.0f : h->hero_fade;
        unsigned prev = h->hero_prev_bg[0] && fade < 1 ? image_get(h->hero_prev_bg, &pw, &ph) : 0;
        unsigned cur = h->hero_bg[0] ? image_get(h->hero_bg, &iw, &ih) : 0;
        float bh = SNTV_UI_H * (g_app.home_style == HOME_SHOWCASE ? 0.92f : 0.86f);
        ui_backdrop_fades(1180, rows_y() - 140 > 360 ? rows_y() - 140 : 360, bh);   /* opaque down to the rows */
        float tv = bgtrailer_shown(h->hero_id);   /* the trailer playing behind the artwork */
        ui_backdrop_video(tv, bh);
        if (prev) ui_backdrop_image(prev, pw, ph, (1 - fade) * (1 - tv), bh);
        if (cur) ui_backdrop_image(cur, iw, ih, fade * (1 - tv), bh);
        ui_text_scrim(1100, bh);
        bgtrailer_draw_badge(h->hero_id);
        draw_hero(h, hero);
        if (g_app.home_style == HOME_SHOWCASE) draw_showcase_controls(h);
    } else if (hero && !g_app.card_details) {   /* Compact: one line about the focused title (the card details say it otherwise) */
        char line[300];
        const char *type = hero->type ? (strcmp(hero->type, "series") == 0 ? "Series" : strcmp(hero->type, "movie") == 0 ? "Movie" : hero->type) : "";
        snprintf(line, sizeof line, "%s%s%s%s%s%s%s", hero->name ? hero->name : "", hero->release_info ? "   \xC2\xB7   " : "", hero->release_info ? hero->release_info : "",
                 type[0] ? "   \xC2\xB7   " : "", type, hero->genres.n ? "   \xC2\xB7   " : "", hero->genres.n ? hero->genres.v[0] : "");
        font_draw_fit(line, LX, 108, FONT_S | FONT_B, THEME_TEXT_DIM, SNTV_UI_W - LX - THEME_MARGIN - 640);
    }

    gfx_clip_push(0, rows_y() - 24, SNTV_UI_W, SNTV_UI_H - rows_y() + 24);
    float y = rows_y() - h->scroll_y + nudge(h, 2);
    for (int i = 0; i < h->n_rows; i++) {
        Row *r = h->rows[i];
        if (!row_visible(r)) continue;
        float rh = row_h(r);
        /* A row sliding off the top fades as its cards reach the edge instead of being
         * sliced there (the last strip of a Continue card read as a floating bar). */
        float edge = rows_y() - 24, bottom = y + TITLE_H + card_h(r);
        float a = (bottom - edge) / card_h(r);
        a = a < 1 ? a * a : 1;   /* a thin strip left at the edge is all but invisible */
        if (a > 0.01f && y < SNTV_UI_H) {
            gfx_set_alpha(a);
            draw_row(h, i, y, a);
            gfx_set_alpha(1);
        }
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

/* Focus to the rail, remembering where it came from so Right (or coming back) returns there. */
static void open_rail(Home *h)
{
    h->rail_from = h->focus_row;
    h->focus_nav = RAIL_HOME;
    move_focus(h, -1, -1);
}

/* Back to the content after the rail: the row it was opened from when that is still there. */
static void close_rail(Home *h)
{
    if (h->rail_from == FOCUS_SHOWCASE && g_app.home_style == HOME_SHOWCASE && featured_count(h)) { h->focus_row = FOCUS_SHOWCASE; return; }
    int n = h->rail_from >= 0 && h->rail_from < h->n_rows && row_visible(h->rows[h->rail_from]) ? h->rail_from : -1;
    if (n < 0 && g_app.home_style == HOME_SHOWCASE && featured_count(h)) { h->focus_row = FOCUS_SHOWCASE; return; }
    if (n < 0) n = next_visible(h, -1, 1);
    if (n >= 0) move_focus(h, n, -1);
}

static int key(Screen *s, KeyAction a)
{
    Home *h = s->st;
    app_redraw();
    if (a == KEY_SEARCH) { app_push(screen_search_create()); return 1; }
    if (a == KEY_LONG_OK && (h->focus_row < 0 || h->focus_row >= h->n_rows)) return 0;   /* the rail or the carousel: a plain OK */
    if (h->focus_row == FOCUS_SHOWCASE) {   /* the carousel View button */
        int n = featured_count(h);
        switch (a) {
        case KEY_LEFT: if (n) { h->feat = (h->feat + n - 1) % n; h->feat_timer = 0; } return 1;
        case KEY_RIGHT: if (n) { h->feat = (h->feat + 1) % n; h->feat_timer = 0; } return 1;
        case KEY_UP: island_take_focus(); return 1;   /* the resume card, when it is up there */
        case KEY_DOWN: { int r = next_visible(h, -1, 1); if (r >= 0) move_focus(h, r, -1); return 1; }
        case KEY_OK: { const MetaPreview *m = hero_item(h); if (m) app_push(screen_detail_create(m)); return 1; }
        case KEY_BACK: open_rail(h); return 1;
        default: return 1;
        }
    }
    if (h->focus_row == -1) {   /* the side rail */
        int r = rail_key(a, &h->focus_nav, RAIL_HOME);
        if (r == 2) { close_rail(h); return 1; }   /* Right, or OK on Home: back into the content */
        if (r == 1) return 1;
        if (a == KEY_BACK) {   /* leaving the app takes a second Back */
            if (h->exit_ask > 0 && g_app.time - h->exit_ask < 2.5f) { g_app.quit = 1; return 1; }
            h->exit_ask = g_app.time;
            app_toast("Press Back again to leave");
            return 1;
        }
        return 0;
    }
    Row *r = h->rows[h->focus_row];
    switch (a) {
    case KEY_UP: {
        int n = next_visible(h, h->focus_row, -1);
        if (n < 0 && g_app.home_style == HOME_SHOWCASE && featured_count(h)) { h->prev_row = h->focus_row; h->prev_col = r->focus; h->prev_t = h->focus_t; h->focus_row = FOCUS_SHOWCASE; return 1; }
        if (n >= 0) move_focus(h, n, -1);   /* the top row is the top: the rail is to the left */
        else island_take_focus();           /* ...unless the resume card is up there */
        return 1;
    }
    case KEY_DOWN: { int n = next_visible(h, h->focus_row, 1); if (n >= 0) { move_focus(h, n, -1); return 1; } h->nudge_at = g_app.time; h->nudge_dir = 2; return 1; }
    case KEY_LEFT:
        if (r->focus > 0) move_focus(h, h->focus_row, r->focus - 1);
        else open_rail(h);   /* first card: the rail */
        return 1;
    case KEY_RIGHT:
        if (r->focus < r->n - 1) move_focus(h, h->focus_row, r->focus + 1);
        else { h->nudge_at = g_app.time; h->nudge_dir = 1; }   /* the end of the row: a nudge, not nothing */
        return 1;
    case KEY_OK: if (r->focus < r->n) app_push(screen_detail_create(&r->metas[r->focus])); return 1;
    case KEY_LONG_OK: case KEY_YELLOW: case KEY_MENU: case KEY_INFO: if (r->focus < r->n) screen_options_for_title(&r->metas[r->focus]); return 1;
    case KEY_BACK:
        if (h->focus_row != next_visible(h, -1, 1) || r->focus > 0) { r->focus = 0; move_focus(h, next_visible(h, -1, 1), 0); return 1; }
        open_rail(h);   /* then the rail; two Backs there leave the app */
        return 1;
    default: return 0;
    }
}

/* Back from a section or screen opened from the rail: the focus returns to the content. */
static void resume(Screen *s)
{
    Home *h = s->st;
    if (h->focus_row == -1 && h->landed) close_rail(h);
    h->focus_nav = RAIL_HOME;
    app_redraw();
}

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
    s->name = "home"; s->st = h; s->long_ok = 1;
    g_home_live = h;
    s->update = update; s->draw = draw; s->key = key; s->destroy = destroy; s->resume = resume;
    rebuild_rows(h);
    return s;
}
