/* Home: cinematic hero for the focused title over its backdrop, ambient light
 * tinted by the artwork, then rows: Continue Watching (landscape cards with
 * progress), My Library, and one row per addon catalog. Everything eases;
 * rows are served from the disk cache first and refreshed in the background. */
#include "../app.h"
#include "../memwatch.h"
#include "../perfstat.h"
#include "../lists.h"
#include "../tracking.h"
#include "../http.h"
#include "../log.h"
#include "../vendor/cJSON.h"
#include "../store.h"
#include "font.h"
#include "../newep.h"
#include "../tmdb.h"
#include "../depth.h"
#include "theme.h"
#include "bgtrailer.h"
#include "widgets.h"
#include "screen_options.h"
#include "rail.h"
#include "island.h"
#include <math.h>
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
    UiPan pan;                    /* ...a finger dragging it */
    float arrived_at;             /* when titles first replaced the placeholders */
    float shown_at;               /* when the pictures in view were in and the cards rose in (0: not yet) */
    ParsedMetas *held;            /* a newer list that came while Home was away: put in once the way back has finished */
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
    int held_logged;          /* changes waiting for the way back (said once) */
    float focus_t;            /* 0..1 grow-in of the focused card */
    int prev_row, prev_col; float prev_t;
    char hero_id[256];
    char want_id[256]; float want_since;   /* the focused title, and since when (HERO_REST) */
    char hero_bg[1024], hero_prev_bg[1024]; float hero_fade;
    float depth_t0; int depth_step, depth_logged;   /* the backdrop pushing into the picture: since when */
    float hero_text_t;        /* fade/slide of the hero text on change */
    int landed;               /* first focus went to the content */
    int feat;                 /* Showcase: which of the featured row's titles is up */
    float feat_timer;
    Peek peek;                /* details beside the focused poster */
    float phone_y, phone_yv;  /* upright on a phone: the page's scroll */
    UiPan phone_pan;
    UiPan hero_pan; float hero_x;   /* upright on a phone: the hero's pages, swiped across */
    int hero_logged;
    int ph_keyed;                     /* ...and arrow keys used (a keyboard, a remote): only then a focus shows and the page follows it */
    int phone_btn;            /* ...and on its feature card, Play (0) or Library (1) */
} Home;

static void move_focus(Home *h, int row, int col);
/* Touch: a card is its row and column; the Showcase button its own */
#define HIT_CARD(row, col) (((row) << 12) | (col))
#define HIT_SHOWCASE 0x7FFFFFF
#define HIT_HERO 0x7FFFFFE
#define HIT_HERO_ADD 0x7FFFFFD

/* Upright on a phone (src/ui/gfx.c: 780 units across): a feature card, rows of cards
 * under it, the tab bar at the bottom. */
#define PH_X 40.0f
#define PH_HERO_Y 132.0f
#define PH_HERO_H 640.0f
#define PH_POSTER_W 224.0f
#define PH_POSTER_H 336.0f
#define PH_GAP 24.0f
#define PH_TITLE_H 64.0f
/* A screen shorter than 1080 units (a phone, drawn larger): the hero gives up most of the
 * difference so a whole row of cards still shows. */
static float short_by(void) { return SNTV_UI_H < SNTV_UI_BASE_H ? (float)(SNTV_UI_BASE_H - SNTV_UI_H) : 0; }
#define ROWS_LIFT (short_by() * 0.62f)
#define HERO_LIFT (short_by() * 0.46f)
static float rows_y(void) { return g_app.home_style == HOME_COMPACT ? 150.0f : (g_theme.id == THEME_APERTURE ? 654.0f : g_app.home_style == HOME_SHOWCASE ? 660.0f : ROWS_Y) - ROWS_LIFT; }

/* ---- row geometry ---- */

/* Aperture shows every row as landscape stills, titled underneath */
static int landscape(const Row *r) { return r->kind == ROW_CONTINUE || g_theme.id == THEME_APERTURE; }
static float card_w(const Row *r) { return landscape(r) ? LAND_W : POSTER_W; }
static float card_h(const Row *r) { return landscape(r) ? LAND_H : POSTER_H; }
static float row_h(const Row *r) { return TITLE_H + card_h(r) + (g_theme.id == THEME_APERTURE ? 96 : 64); }
static int visible_cards(const Row *r) { return (int)((SNTV_UI_W - LX) / (card_w(r) + GAP)); }
static int ph_land(const Row *r);
static float ph_cw(const Row *r);

/* The Library row ends in a See all card, which opens the whole Library. */
static int see_all(const Row *r) { return r->kind == ROW_LIBRARY && r->n >= 4; }
static void see_all_card(float x, float y, float w, float h, float f, int count, int hit)
{
    float sc = ui_focus_scale(f), cw = w * sc, ch = h * sc, cx = x - (cw - w) / 2, cy = y - (ch - h) / 2 - ui_focus_lift(f);
    ui_focus_under(cx, cy, cw, ch, R_CARD, f);
    gfx_rrect(cx, cy, cw, ch, R_CARD, COLOR_ALPHA(THEME_TEXT, f > 0.5f ? 0x2A : 0x16));
    if (f > 0.5f) gfx_rrect_ring(cx - 4, cy - 4, cw + 8, ch + 8, R_CARD + 4, 4, THEME_FOCUS);
    ui_icon(ICON_LIBRARY, cx + cw / 2, cy + ch / 2 - 46, 44, THEME_TEXT);
    font_draw_center("See all", cx + cw / 2, cy + ch / 2, FONT_M | FONT_B, THEME_TEXT);
    char n[32]; snprintf(n, sizeof n, "%d titles", count);
    font_draw_center(n, cx + cw / 2, cy + ch / 2 + 44, FONT_XS, THEME_TEXT_DIM);
    ui_hit(cx, cy, cw, ch, hit);
}

/* The picture a card of this row shows (as its draw picks it). */
static const char *card_art(const Row *r, const MetaPreview *m, char *buf, size_t n)
{
    int wide = SNTV_PHONE ? ph_land(r) : g_theme.id == THEME_APERTURE || r->kind == ROW_CONTINUE;
    return wide ? app_art_background(m, buf, n) : app_art_poster(m, buf, n);
}

/* Lazy rows: the pictures of the cards in view (and a few past the edge) are asked for, and
 * the row shows its cards once those are in, all together; rows near the focus get theirs
 * before they are scrolled to. Until then the row's placeholders stay. */
static void row_pictures(Home *h, int ri)
{
    Row *r = h->rows[ri];
    if (!r->n) return;
    float step = SNTV_PHONE ? ph_cw(r) + 24 : card_w(r) + GAP;
    int first = (int)(r->scroll_x / step), vis = SNTV_PHONE ? (int)(SNTV_UI_W / step) + 1 : visible_cards(r) + 1;
    if (first < 0) first = 0;
    char buf[1024];
    int ready = 1;
    for (int j = first; j < r->n && j < first + vis + 6; j++) {
        int in = ui_art_ready(card_art(r, &r->metas[j], buf, sizeof buf));
        if (j < first + vis) ready &= in;
    }
    if (!r->shown_at && (ready || g_app.time - r->arrived_at > UI_ROW_WAIT || g_app.fx_light)) { r->shown_at = g_app.time; app_redraw(); }
    if (!r->shown_at) app_redraw();   /* to see them come */
}

static void row_free(Row *r)
{
    if (!r) return;
    if (r->req) http_cancel(r->req);
    meta_previews_free(r->metas, r->n);
    if (r->held) { meta_previews_free(r->held->m, r->held->n); free(r->held); }
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
    if (!row->n && n) { row->arrived_at = g_app.time; row->shown_at = 0; }
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
    if (!pm) return;
    r->parsed = NULL;
    Screen *top = app_top();
    if (row->n && (app_transition_active() || !top || top->st != g_home_live)) {   /* away, or on the way back: the card Back returns to stays where it was */
        if (row->held) { meta_previews_free(row->held->m, row->held->n); free(row->held); }
        row->held = pm;
        return;
    }
    apply_metas(row, pm->m, pm->n); free(pm);
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

/* ---- upright on a phone ---- */

/* A Continue Watching card's label, inside it at the foot: the name, the episode, how far
 * (or how many new episodes); x2..h2 the card as drawn (focus scale included). The TV's and
 * the phone's are the same card. */
static void cw_label(const MetaPreview *m, float x2, float y2, float w2, float h2)
{
    /* the shade takes the card's own corners (a clipped square one showed past them) */
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
    else if (li && li->duration > li->time_offset && li->time_offset > 0) {   /* how long is left, at the top right */
        int min = (int)((li->duration - li->time_offset) / 60000) + 1;
        char left[32];
        if (min >= 60) snprintf(left, sizeof left, "%dh %dm left", min / 60, min % 60);
        else snprintf(left, sizeof left, "%d min left", min);
        ui_chip(left, x2 + w2 - 16 - font_measure(left, FONT_XS | FONT_B) - 24, y2 + 16, RGBA(0, 0, 0, 0xA0), THEME_TEXT);
    }
    font_draw_fit(m->name, x2 + 20, y2 + h2 - 76, FONT_S | FONT_B, THEME_TEXT, w2 - 40);
    if (sub[0]) font_draw(sub, x2 + 20, y2 + h2 - 48, FONT_XS, THEME_TEXT_DIM);
    if (li && li->duration && !ne) ui_progress(x2 + 20, y2 + h2 - 18, w2 - 40, 6, (float)li->time_offset / (float)li->duration, THEME_ACCENT);
}

static int ph_land(const Row *r) { return r->kind == ROW_CONTINUE; }
static float ph_cw(const Row *r) { return ph_land(r) ? (SNTV_UI_W - PH_X) * 0.62f : PH_POSTER_W; }   /* a card and a half across, as the TV's row shows more to come */
static float ph_ch(const Row *r) { return ph_land(r) ? ph_cw(r) * 9 / 16 : PH_POSTER_H; }
static float ph_row_h(const Row *r) { return PH_TITLE_H + ph_ch(r) + 56; }
/* Where the hero ends: Cinematic a band under the header, Showcase most of the screen; Compact has none. */
static float ph_hero_bottom(void) { return g_app.home_style == HOME_SHOWCASE ? SNTV_UI_H * 0.7f : PH_HERO_Y + PH_HERO_H; }
static const MetaPreview *ph_hero(const Home *h)
{
    const Row *r = featured_row(h);
    if (g_app.home_style == HOME_COMPACT) return NULL;
    return r && r->n ? &r->metas[h->feat % featured_count(h)] : NULL;
}
/* No hero yet, but the catalog it comes from is on its way: its room is kept (no jump). */
static int ph_hero_coming(const Home *h)
{
    if (g_app.home_style == HOME_COMPACT || ph_hero(h)) return 0;
    for (int i = 0; i < h->n_rows; i++) if (h->rows[i]->kind == ROW_CATALOG && (h->rows[i]->loading || !h->rows[i]->requested)) return 1;
    return 0;
}
static float ph_rows_top(const Home *h) { return ph_hero(h) || ph_hero_coming(h) ? ph_hero_bottom() + 48 : PH_HERO_Y; }

/* The focused card kept in view (the row stays put while it already is); a finger moves it freely. */
static void phone_scroll_row(Row *r, float dt)
{
    float step = ph_cw(r) + PH_GAP, view = SNTV_UI_W - 2 * PH_X;
    float max = (r->n + see_all(r)) * step - PH_GAP - view, want = r->scroll_x;
    if (r->focus * step + ph_cw(r) - view > want) want = r->focus * step + ph_cw(r) - view;
    if (r->focus * step < want) want = r->focus * step;
    if (want > max) want = max;
    if (want < 0) want = 0;
    ui_pan(&r->pan, &r->scroll_x, want, max, dt, 16);
}

/* The focused row kept on screen above the tab bar; the feature card at the top. */
static void phone_scroll_page(Home *h, float dt)
{
    float y = ph_rows_top(h), want = 0, room = SNTV_UI_H - PHONE_TABS_H;
    if (h->focus_row >= 0)
        for (int i = 0; i < h->n_rows; i++) {
            if (!row_visible(h->rows[i])) continue;
            float rh = ph_row_h(h->rows[i]);
            if (i == h->focus_row) { want = y + rh > room + h->phone_y ? y + rh - room + 20 : y - 40 < h->phone_y ? y - 40 : h->phone_y; break; }
            y += rh;
        }
    if (!h->ph_keyed) want = h->phone_y;   /* by touch alone the page stays where the finger left it */
    if (want < 0 || h->focus_row == FOCUS_SHOWCASE) want = 0;
    float end = ph_rows_top(h);   /* the whole page, to the last row above the tab bar */
    for (int i = 0; i < h->n_rows; i++) if (row_visible(h->rows[i])) end += ph_row_h(h->rows[i]);
    ui_pan(&h->phone_pan, &h->phone_y, want, end + 20 - room, dt, 14);
}

/* The hero's pages follow a finger across; let go, it settles on the page it was thrown
 * towards. Otherwise it slides to the page the carousel is on. */
static void phone_scroll_hero(Home *h, float dt)
{
    int n = featured_count(h);
    float w = SNTV_UI_W;
    if (n < 1) { h->hero_x = 0; return; }
    if (!ui_pan_held(&h->hero_pan) && ui_pan_free(&h->hero_pan)) {
        int page = (int)floorf((h->hero_x + h->hero_pan.vel * 0.2f) / w + 0.5f);
        if (page < 0) page = 0;
        if (page > n - 1) page = n - 1;
        if (page != h->feat % n) { h->feat = page; LOGI("home: hero swiped to %d of %d", page + 1, n); }
        h->feat_timer = 0;
        h->hero_pan.free = 0;   /* it eases onto the page from here */
    }
    float want = (float)(h->feat % n) * w;
    ui_pan(&h->hero_pan, &h->hero_x, want, (n - 1) * w, dt, 12);
    if (fabsf(h->hero_x - want) > 0.5f) app_redraw();
}

static float ph_button(const char *label, int icon, float x, float y, float w, float hh, int primary, int foc)
{
    if (foc) ui_focus_under(x, y, w, hh, hh / 2, 1);
    gfx_rrect(x, y, w, hh, hh / 2, primary ? RGBA(255, 255, 255, 255) : COLOR_ALPHA(THEME_TEXT, 0x2E));
    if (foc) gfx_rrect_ring(x - 6, y - 6, w + 12, hh + 12, hh / 2 + 6, 4, THEME_ACCENT);
    Color fg = primary ? RGBA(0x11, 0x11, 0x11, 255) : THEME_TEXT;
    float tw = label ? font_measure(label, FONT_M | FONT_B) : 0, iw = icon >= 0 ? 34 : 0, gap = label && icon >= 0 ? 14 : 0;
    float cx = x + (w - tw - iw - gap) / 2;
    if (icon >= 0) ui_icon((Icon)icon, cx + iw / 2, y + hh / 2, 30, fg);
    if (label) font_draw(label, cx + iw + gap, y + (hh - font_height(FONT_M | FONT_B)) / 2, FONT_M | FONT_B, fg);
    return w;
}

static void phone_row(Home *h, int ri, float y)
{
    Row *r = h->rows[ri];
    int focused_row = h->focus_row == ri;
    font_draw_fit(r->title, PH_X, y, FONT_L | FONT_B, THEME_TEXT, SNTV_UI_W - 2 * PH_X);
    float cy = y + PH_TITLE_H, cw = ph_cw(r), ch = ph_ch(r);
    if (r->n) ui_pan_area(&r->pan, 0, cy, SNTV_UI_W, ch, 0);
    if (!r->n) {
        if (r->loading || !r->requested) for (int j = 0; j < 4; j++) ui_skeleton(PH_X + j * (cw + PH_GAP), cy, cw, ch, R_CARD);
        else if (r->failed) font_draw("Couldn't load this catalog.", PH_X, cy + 20, FONT_S, THEME_TEXT_MUTED);
        return;
    }
    if (!r->shown_at) {   /* the pictures in view aren't in yet: the placeholders a moment longer */
        row_pictures(h, ri);
        if (!r->shown_at) { for (int j = 0; j < 4; j++) ui_skeleton(PH_X + j * (cw + PH_GAP) - r->scroll_x, cy, cw, ch, R_CARD); return; }
    }
    float rise = (g_app.time - r->shown_at) / 0.3f;   /* then they come in together */
    if (rise < 1 && !g_app.fx_light) { gfx_set_alpha(rise); app_redraw(); }
    char buf[512];
    for (int pass = 0; pass < 2; pass++)
        for (int j = 0; j < r->n; j++) {
            float px = PH_X + j * (cw + PH_GAP) - r->scroll_x;
            if (px > SNTV_UI_W || px + cw < 0) continue;
            int foc = h->ph_keyed && focused_row && r->focus == j;
            if ((pass == 1) != foc) continue;
            const MetaPreview *m = &r->metas[j];
            const char *art = ph_land(r) ? app_art_background(m, buf, sizeof buf) : app_art_poster(m, buf, sizeof buf);
            ui_card(art, px, cy, cw, ch, foc ? (g_app.fx_light ? 1 : h->focus_t) : 0, 0, m->name, ph_land(r) ? CARD_TRUE_COLOUR : 0);
            ui_hit(px, cy, cw, ch, HIT_CARD(ri, j));
            if (ph_land(r)) {   /* as on the TV: the label inside the card */
                float f = foc ? (g_app.fx_light ? 1 : h->focus_t) : 0, sc = ui_focus_scale(f);
                float w2 = cw * sc, h2 = ch * sc;
                cw_label(m, px - (w2 - cw) / 2, cy - (h2 - ch) / 2 - ui_focus_lift(f), w2, h2);
            }
        }
    if (see_all(r)) see_all_card(PH_X + r->n * (cw + PH_GAP) - r->scroll_x, cy, cw, ch, h->ph_keyed && focused_row && r->focus == r->n ? 1 : 0, r->n, HIT_CARD(ri, r->n));
    gfx_set_alpha(1);
}

/* A title for the hero, centred: on one line, or broken at the space nearest its middle. */
static void ph_center_title(const char *t, float cx, float y, float maxw)
{
    int fs = FONT_XL | FONT_B;
    if (font_measure(t, fs) <= maxw) { font_draw_center(t, cx, y + font_height(fs), fs, THEME_TEXT); return; }
    size_t n = strlen(t), best = 0;
    for (size_t i = 1; i < n && i < 255; i++)
        if (t[i] == ' ' && (!best || labs((long)i - (long)n / 2) < labs((long)best - (long)n / 2))) best = i;
    if (!best) { font_draw_fit(t, cx - maxw / 2, y + font_height(fs), fs, THEME_TEXT, maxw); return; }
    char a[256];
    snprintf(a, sizeof a, "%.*s", (int)best, t);
    font_draw_center(a, cx, y, fs, THEME_TEXT);
    font_draw_center(t + best + 1, cx, y + font_height(fs), fs, THEME_TEXT);
}

/* One page of a phone's hero: the backdrop edge to edge (cropped to fill it, moving at a
 * third of the page's speed as it scrolls), its logo or name and facts centred low. */
static void ph_hero_page(const MetaPreview *p, float x, float drift, float w, float hh)
{
    char buf[512];
    int iw = 0, ih = 0;
    const char *bg = app_art_background(p, buf, sizeof buf);
    unsigned tex = bg ? image_get(bg, &iw, &ih) : 0;
    if (tex && iw > 0 && ih > 0) {
        float ar = (float)iw / ih, shape = w / hh, u0 = 0, u1 = 1, v0 = 0, v1 = 1;
        if (ar > shape) { float k = shape / ar; u0 = (1 - k) / 2; u1 = u0 + k; }
        else { float k = ar / shape; v0 = (1 - k) / 2; v1 = v0 + k; }
        gfx_tex_uv(tex, x, drift, w, hh, u0, v0, u1, v1, RGBA(255, 255, 255, 255));
    } else gfx_rect(x, 0, w, hh, THEME_BG2);
    gfx_gradient_v(x, hh * 0.35f, w, hh * 0.65f, RGBA(0, 0, 0, 0), RGBA(0, 0, 0, 0xC8));
    float cx = x + w / 2, ty = hh - 430;
    const char *logo = app_art_logo(p, buf, sizeof buf);
    int lw = 0, lh = 0;
    unsigned ltex = logo ? image_get(logo, &lw, &lh) : 0;
    if (ltex && lw > 0 && lh > 0) {
        float bw = w * 0.62f, bh = 150, lw2 = bw, lh2 = bw * lh / lw;
        if (lh2 > bh) { lh2 = bh; lw2 = bh * lw / lh; }
        gfx_tex(ltex, cx - lw2 / 2, ty + (bh - lh2), lw2, lh2, RGBA(255, 255, 255, 255));
    } else ph_center_title(p->name ? p->name : "", cx, ty + 150 - 2 * font_height(FONT_XL | FONT_B), w - 120);
    char meta[200] = "";
    const char *type = p->type && !strcmp(p->type, "series") ? "Series" : p->type && !strcmp(p->type, "movie") ? "Film" : NULL;
    const char *parts[4] = { p->release_info, type, p->genres.n ? p->genres.v[0] : NULL, NULL };
    size_t n = 0;
    for (int k = 0; k < 3; k++) if (parts[k] && *parts[k] && n < sizeof meta) n += (size_t)snprintf(meta + n, sizeof meta - n, "%s%s", n ? "  \xC2\xB7  " : "", parts[k]);
    if (p->imdb_rating && *p->imdb_rating && n < sizeof meta) snprintf(meta + n, sizeof meta - n, "%sIMDb %s", n ? "  \xC2\xB7  " : "", p->imdb_rating);
    float mw = font_measure(meta, FONT_S);
    if (mw > w - 80) mw = w - 80;
    font_draw_fit(meta, cx - mw / 2, ty + 172, FONT_S, COLOR_ALPHA(THEME_TEXT, 0xE0), w - 80);
}

/* A phone's hero, as Nuvio and the like have it: edge to edge from the top, under the
 * header, fading into the page; swiped across for the next title, the dots saying where. */
static void ph_hero_draw(Home *h)
{
    const Row *r = featured_row(h);
    int n = featured_count(h);
    float W = SNTV_UI_W, H = ph_hero_bottom();
    if (!h->hero_logged) { h->hero_logged = 1; LOGI("home: the hero shows %d titles", n); }
    float drift = h->phone_y > 0 ? h->phone_y * 0.3f : 0;
    ui_pan_area(&h->hero_pan, 0, 0, W, ph_hero_bottom() - 170, 0);
    gfx_clip_push(0, 0, W, H);
    int i0 = (int)floorf(h->hero_x / W);
    for (int i = i0; i <= i0 + 1; i++) {
        if (i < 0 || i >= n) continue;
        float x = i * W - h->hero_x;
        if (x < W && x + W > 0) ph_hero_page(&r->metas[i], x, drift, W, H);
    }
    gfx_clip_pop();
    gfx_gradient_v(0, 0, W, 240, RGBA(0, 0, 0, 0x99), RGBA(0, 0, 0, 0));   /* the header reads over any picture */
    gfx_gradient_v(0, H - 150, W, 152, COLOR_ALPHA(THEME_BG, 0), THEME_BG);   /* into the page, no edge */
    const MetaPreview *p = ph_hero(h);
    int foc = h->ph_keyed && h->focus_row == FOCUS_SHOWCASE;
    float bh = 92, pw = W - 2 * PH_X - 80 - bh - 20;
    if (pw > 420) pw = 420;
    float bx = (W - pw - 20 - bh) / 2, by = H - 196;
    ui_hit(0, 130, W, by - 150, HIT_HERO);   /* the picture opens it too */
    ph_button("Play", ICON_PLAY, bx, by, pw, bh, 1, foc && h->phone_btn == 0);
    ui_hit(bx, by, pw, bh, HIT_HERO);
    LibraryItem *li = app_library_find(p->id);
    int in_lib = li && !li->removed && !li->temp;
    ph_button(NULL, in_lib ? ICON_CHECK : ICON_PLUS, bx + pw + 20, by, bh, bh, 0, foc && h->phone_btn == 1);
    ui_hit(bx + pw + 20, by, bh, bh, HIT_HERO_ADD);
    if (n > 1) {   /* the dots: the current one stretched, filling until the next comes */
        float dw = 12, gap = 12, cur = 44, total = (n - 1) * (dw + gap) + cur, x = (W - total) / 2, cy = by + bh + 40;
        for (int i = 0; i < n; i++) {
            int on = i == h->feat % n;
            float w = on ? cur : dw;
            gfx_rrect(x, cy - dw / 2, w, dw, dw / 2, on ? COLOR_ALPHA(THEME_TEXT, 0x50) : COLOR_ALPHA(THEME_TEXT, 0x40));
            if (on && !g_app.fx_light) {
                float t = h->feat_timer / SHOWCASE_EVERY;
                if (t > 1) t = 1;
                if (w * t > dw) gfx_rrect(x, cy - dw / 2, w * t, dw, dw / 2, THEME_TEXT);
            } else if (on) gfx_rrect(x, cy - dw / 2, w, dw, dw / 2, THEME_TEXT);
            x += w + gap;
        }
    }
}

static void phone_draw(Home *h)
{
    gfx_rect(0, 0, SNTV_UI_W, SNTV_UI_H, THEME_BG);
    ui_pan_area(&h->phone_pan, 0, 0, SNTV_UI_W, SNTV_UI_H - PHONE_TABS_H, 1);
    float top = -h->phone_y;
    gfx_set_translate(0, top);
    if (ph_hero(h)) ph_hero_draw(h);
    else if (ph_hero_coming(h)) {   /* its room, shimmering until it comes */
        float H = ph_hero_bottom();
        gfx_rect(0, 0, SNTV_UI_W, H, THEME_BG2);
        ui_shimmer(0, 0, SNTV_UI_W, H);
        gfx_gradient_v(0, H - 150, SNTV_UI_W, 152, COLOR_ALPHA(THEME_BG, 0), THEME_BG);
    }
    /* header, over the hero: the mark and name; the account at the right */
    ui_mark(PH_X, 36, 60);
    font_draw(SNTV_APP_NAME, PH_X + 80, 36 + (60 - font_height(FONT_L | FONT_B)) / 2, FONT_L | FONT_B, THEME_TEXT);
    float ax = SNTV_UI_W - PH_X - 72;
    gfx_circle(ax + 36, 66, 36, ph_hero(h) ? RGBA(0, 0, 0, 0x60) : COLOR_ALPHA(THEME_TEXT, 0x22));
    ui_icon(ICON_USER, ax + 36, 66, 34, THEME_TEXT);
    ui_hit(ax - 10, 20, 92, 92, HIT_RAIL + RAIL_ACCOUNT);
    float y = ph_rows_top(h);
    int any = 0;
    for (int i = 0; i < h->n_rows; i++) {
        Row *r = h->rows[i];
        if (!row_visible(r)) continue;
        any = 1;
        float rh = ph_row_h(r);
        if (y + top < SNTV_UI_H && y + rh + top > 0) phone_row(h, i, y);
        y += rh;
    }
    gfx_set_translate(0, 0);
    if (!any) {
        float cy = SNTV_UI_H / 2;
        if (g_app.addons_loading) { ui_spinner(SNTV_UI_W / 2, cy, 30); font_draw_center("Loading your addons", SNTV_UI_W / 2, cy + 60, FONT_M | FONT_B, THEME_TEXT_DIM); }
        else font_draw_center(g_app.addons_error[0] ? g_app.addons_error : "No catalogs yet", SNTV_UI_W / 2, cy, FONT_L | FONT_B, THEME_TEXT);
    }
    rail_draw(RAIL_HOME, -1);
}

static int phone_key(Home *h, KeyAction a)
{
    if (a == KEY_UP || a == KEY_DOWN || a == KEY_LEFT || a == KEY_RIGHT) h->ph_keyed = 1;   /* touch never sends these on a phone */
    if (h->focus_row == FOCUS_SHOWCASE || h->focus_row < 0) {
        const MetaPreview *p = ph_hero(h);
        switch (a) {
        case KEY_LEFT: h->phone_btn = 0; return 1;
        case KEY_RIGHT: h->phone_btn = 1; return 1;
        case KEY_DOWN: { int r = next_visible(h, -1, 1); if (r >= 0) move_focus(h, r, -1); return 1; }
        case KEY_OK: if (!p) return 1; if (h->phone_btn) app_library_toggle(p); else app_push(screen_detail_create(p)); return 1;
        case KEY_LONG_OK: if (p) screen_options_for_title(p); return 1;
        case KEY_BACK:
            if (h->exit_ask > 0 && g_app.time - h->exit_ask < 2.5f) { g_app.quit = 1; return 1; }
            h->exit_ask = g_app.time;
            app_toast("Press Back again to leave");
            return 1;
        default: return 1;
        }
    }
    Row *r = h->rows[h->focus_row];
    switch (a) {
    case KEY_UP: { int n = next_visible(h, h->focus_row, -1); if (n >= 0) move_focus(h, n, -1); else if (ph_hero(h)) { h->focus_row = FOCUS_SHOWCASE; h->phone_btn = 0; } return 1; }
    case KEY_DOWN: { int n = next_visible(h, h->focus_row, 1); if (n >= 0) move_focus(h, n, -1); return 1; }
    case KEY_LEFT: if (r->focus > 0) move_focus(h, h->focus_row, r->focus - 1); return 1;
    case KEY_RIGHT: if (r->focus < r->n - 1 + see_all(r)) move_focus(h, h->focus_row, r->focus + 1); return 1;
    case KEY_OK: if (r->focus < r->n) app_push(screen_detail_create(&r->metas[r->focus])); else if (see_all(r)) app_push(screen_library_create()); return 1;
    case KEY_LONG_OK: case KEY_YELLOW: case KEY_MENU: case KEY_INFO: if (r->focus < r->n) screen_options_for_title(&r->metas[r->focus]); return 1;
    case KEY_BACK: h->focus_row = ph_hero(h) ? FOCUS_SHOWCASE : next_visible(h, -1, 1); h->phone_btn = 0; return 1;
    default: return 0;
    }
}

static void update(Screen *s, float dt)
{
    if (g_theme.id == THEME_APERTURE) ui_aperture_drift_tick();
    Home *h = s->st;
    if (g_app.time > 4) note_first_view(h);
    /* on the way back from a title page: the rows stay as they were, so the page shrinks into
     * its card where it still is; what changed meanwhile goes in when it has finished */
    if (app_transition_active()) {
        int changed = h->addons_seen != g_app.addons_version || h->rows_seen != g_app.home_rows_version || h->library_seen != g_app.library_version
                      || h->trakt_seen != tracking_version() || h->lists_seen != lists_version();
        for (int i = 0; i < h->n_rows && !changed; i++) changed = h->rows[i]->held != NULL;
        if (changed && !h->held_logged) { h->held_logged = 1; LOGI("home: changes wait for the way back to finish"); }
        return;
    }
    if (h->held_logged) { h->held_logged = 0; LOGI("home: the way back finished; the changes go in"); }
    for (int i = 0; i < h->n_rows; i++) if (h->rows[i]->held) { ParsedMetas *pm = h->rows[i]->held; h->rows[i]->held = NULL; apply_metas(h->rows[i], pm->m, pm->n); free(pm); }
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
    float ph_at = SNTV_PHONE ? ph_rows_top(h) : 0;   /* a phone: where each row sits on the page */
    for (int i = 0; i < h->n_rows && fetched < 4; i++) {
        int reach = g_app.low_memory ? 1 : 3, first = g_app.low_memory ? 3 : 5;
        int cat = h->rows[i]->kind == ROW_CATALOG;
        /* nothing focused yet: the first catalogs, counted past empty library and list rows
         * (Showcase waits for them to place its focus) */
        int near = h->focus_row < 0 ? cat && nth_catalog < first : (i >= h->focus_row - 1 && i <= h->focus_row + reach);
        if (SNTV_PHONE) {   /* a finger scrolls without moving the focus: the rows in view and a screen below, and the hero's */
            float room = SNTV_UI_H - PHONE_TABS_H;
            near |= (cat && nth_catalog == 0) || (ph_at < h->phone_y + 2 * room && ph_at + 400 > h->phone_y);
            if (row_visible(h->rows[i])) ph_at += ph_row_h(h->rows[i]);
        }
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
    if ((g_app.home_style == HOME_SHOWCASE || SNTV_PHONE) && featured_count(h) > 1) {
        float was = h->feat_timer;
        if (!bgtrailer_shown(h->hero_id)) h->feat_timer += dt;   /* a trailer playing holds the carousel */
        if ((int)(was * 34 / SHOWCASE_EVERY) != (int)(h->feat_timer * 34 / SHOWCASE_EVERY) && !g_app.fx_light) app_redraw();   /* the dot's fill, a step a pixel */
        int resting = SNTV_PHONE ? !g_app.finger_on && !ui_pan_held(&h->hero_pan) : h->focus_row != FOCUS_SHOWCASE;   /* a phone's: not while a finger is down */
        if (h->feat_timer > SHOWCASE_EVERY && resting) { h->feat_timer = 0; h->feat = (h->feat + 1) % featured_count(h); app_redraw(); }
        if (!resting) h->feat_timer = 0;
    } else if (h->focus_row == FOCUS_SHOWCASE && !SNTV_PHONE) h->focus_row = -1;

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
    for (int i = 0; i < h->n_rows; i++) {   /* the rows in sight and a few either side of the focus get their pictures */
        int near = h->focus_row < 0 ? i < 4 : i >= h->focus_row - 1 && i <= h->focus_row + 3;
        if (near || (h->rows[i]->n && !h->rows[i]->shown_at && !g_app.low_memory && i < 12)) row_pictures(h, i);
    }
    for (int i = 0; i < h->n_rows; i++) {
        Row *r = h->rows[i];
        if (SNTV_PHONE) { phone_scroll_row(r, dt); continue; }
        int vis = visible_cards(r);
        int first = r->focus - (vis - 2);
        if (first < 0) first = 0;
        ui_pan(&r->pan, &r->scroll_x, first * (card_w(r) + GAP), (r->n + see_all(r)) * (card_w(r) + GAP) - GAP - (SNTV_UI_W - LX - THEME_MARGIN), dt, 16);
    }
    if (SNTV_PHONE) { phone_scroll_page(h, dt); phone_scroll_hero(h, dt); }
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
        h->depth_t0 = g_app.time; h->depth_logged = 0;
    }
    if (hero && settled && g_app.home_style != HOME_COMPACT && !strcmp(hero->id, h->hero_id))
        bgtrailer_want(h->hero_id, g_theme.id == THEME_APERTURE ? rows_y() + 60 : SNTV_UI_H * (g_app.home_style == HOME_SHOWCASE ? 0.92f : 0.86f), 4.0f);

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
        if (nr >= 0 && !g_app.low_memory && mem_stage() < 1) { Row *n2 = h->rows[nr];   /* the next row's posters ahead, while memory allows */ for (int j = 0; j < n2->n && j < 8; j++) image_prefetch(app_art_poster(&n2->metas[j], buf, sizeof buf)); }
    }
    if (g_app.addons_loading) app_redraw();
}

/* ---- drawing ---- */

static void draw_showcase_controls(const Home *h);

/* A backdrop with a depth map pushes slowly into the picture and back, 40 s each way, the
 * near things coming towards you; redrawn 15 times a second, enough for a move this slow. */
static void hero_depth(Home *h)
{
    ui_backdrop_depth(0, 0);
    if (!g_app.depth_motion || g_app.fx_light || g_app.low_memory || !h->hero_bg[0]) return;
    unsigned dt = depth_texture(h->hero_bg);
    if (!dt) return;
    float ph = (g_app.time - h->depth_t0) / 40.0f;
    ui_backdrop_depth(dt, 0.004f + 0.09f * (0.5f - 0.5f * cosf(ph * 3.14159265f)));
    if (!h->depth_logged) {
        char key[96]; depth_key(h->hero_bg, key, sizeof key);
        LOGI("home: the backdrop moves in depth (%s)", key);
        h->depth_logged = 1;
    }
    int step = (int)(g_app.time * 15);
    if (step != h->depth_step) { h->depth_step = step; app_redraw(); }
}

static void draw_hero(Home *h, const MetaPreview *p)
{
    if (g_theme.id == THEME_APERTURE) {
        float k = g_app.fx_light ? 1 : h->hero_text_t;
        float ax = 700 - 20 * k, ah = rows_y() + 60;
        float tv = bgtrailer_shown(h->hero_id);   /* its trailer plays in the artwork window, as behind the other themes' heroes */
        ui_backdrop_fades(ax + 460, ah - 300, ah);
        ui_backdrop_video(tv, ah);
        if (tv < 1) hero_depth(h);   /* not under a trailer that hides it */
        ui_aperture_art(h->hero_bg, ax, 0, SNTV_UI_W - ax, ah, (g_app.fx_light ? 1 : h->hero_fade) * (1 - tv));
        ui_backdrop_depth(0, 0);
        gfx_gradient_h(ax - 1, 0, 460, ah, THEME_BG, COLOR_ALPHA(THEME_BG, 0));
        gfx_gradient_v(0, ah - 300, SNTV_UI_W, 300, COLOR_ALPHA(THEME_BG, 0), THEME_BG);
        bgtrailer_draw_badge(h->hero_id, 1);
        int a = (int)(255 * k);
        ui_spaced_caps("Your evening, in frame", LX, 56, FONT_XS, 7, COLOR_ALPHA(THEME_TEXT_DIM, a), 0);
        const Row *fr = h->focus_row >= 0 && h->focus_row < h->n_rows ? h->rows[h->focus_row] : NULL;
        ui_spaced_caps(fr && fr->kind == ROW_CONTINUE ? "Pick up where you left off" : fr ? fr->title : "Tonight's feature", LX, 132, FONT_XS | FONT_B, 5, COLOR_ALPHA(THEME_ACCENT, a), 760);
        float tdx, tdy;
        gfx_get_translate(&tdx, &tdy);
        gfx_set_translate(tdx, tdy - ROWS_LIFT);   /* the title and buttons ride up with the rows */
        ui_serif_title(p->name, LX, 404, 780, FONT_HERO, 2, COLOR_ALPHA(THEME_TEXT, a));
        const char *type = p->type && !strcmp(p->type, "series") ? "Series" : p->type && !strcmp(p->type, "movie") ? "Film" : p->type;
        const char *parts[3] = { p->release_info, p->runtime, p->genres.n ? p->genres.v[0] : type };
        ui_slash_line(parts, 3, LX, 424, FONT_S, COLOR_ALPHA(THEME_TEXT, a));
        const TmdbInfo *tm = tmdb_get(p->id, p->type);
        if (tm && tm->tagline[0]) font_draw_wrapped(tm->tagline, LX, 472, FONT_M, COLOR_ALPHA(THEME_TEXT, a), 640, 2);
        else if (p->description) font_draw_wrapped(p->description, LX, 476, FONT_S, COLOR_ALPHA(THEME_TEXT_DIM, a), 640, 2);
        int series = p->type && !strcmp(p->type, "series");
        float bw = ui_aperture_button(series ? "Watch series" : "Watch film", ICON_PLAY, LX, 562, 1, 0);
        float mw = ui_aperture_button("More details", -1, LX + bw + 46, 562, 0, 0);
        ui_hit(LX, 562, bw + 46 + mw, 64, HIT_HERO);   /* both open the title in focus */
        gfx_set_translate(tdx, tdy);
        return;
    }
    float k = g_app.fx_light ? 1.0f : h->hero_text_t;
    int a = (int)(255 * k);
    float x = LX, y = 168 - HERO_LIFT + (1 - k) * 18;
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
    char genres[96] = "";
    for (int g = 0; g < p->genres.n && g < 3; g++)   /* the genres in the facts line, as on title pages */
        snprintf(genres + strlen(genres), sizeof genres - strlen(genres), "%s%s", g ? ", " : "", p->genres.v[g]);
    if (p->release_info) parts[np++] = p->release_info;
    if (p->runtime) parts[np++] = p->runtime;
    if (p->type) parts[np++] = strcmp(p->type, "series") == 0 ? "Series" : strcmp(p->type, "movie") == 0 ? "Movie" : p->type;
    if (genres[0]) parts[np++] = genres;
    if (np) ui_meta_line(parts, np, mx > x ? mx + 8 : mx, y + (36 - font_height(FONT_S | FONT_B)) / 2, COLOR_ALPHA(THEME_TEXT_DIM, a));
    y += 54;
    /* what critics and audiences make of it (with an MDBList key) */
    ScoreBadge sb[8];
    char sv[8][16];
    int ns = ui_mdb_badges(p->id, p->type, sb, sv);
    if (ns) { ui_score_pills(x, y, sb, ns, maxw + 200, k); y += 60; }
    if (p->description) font_draw_wrapped(p->description, x, y, FONT_S, COLOR_ALPHA(THEME_TEXT_DIM, a), maxw, short_by() > 0 ? 2 : 4);
}

static void draw_showcase_controls(const Home *h)
{
    int n = featured_count(h);
    if (!n) return;
    int foc = h->focus_row == FOCUS_SHOWCASE;
    float y = rows_y() - 108;
    float vw = ui_pill("View title", 1, ICON_CHEVRON_RIGHT, LX, y, 60, PILL_PRIMARY, foc);
    ui_hit(LX, y, vw, 60, HIT_SHOWCASE);
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
    if (foc && !gfx_handheld()) font_draw("Left/Right: browse  \xC2\xB7  OK: open  \xC2\xB7  Down: rows", SNTV_UI_W - THEME_MARGIN - font_measure("Left/Right: browse  \xC2\xB7  OK: open  \xC2\xB7  Down: rows", FONT_XS), cy + 22, FONT_XS, THEME_TEXT_MUTED);
}

/* Pressed past the end: things spring toward the press and settle. axis 1 rows, 2 the list. */
static float nudge(const Home *h, int axis)
{
    if (h->nudge_dir == 0 || abs(h->nudge_dir) != axis) return 0;
    return ui_spring(h->nudge_at, -16 * (h->nudge_dir > 0 ? 1 : -1));
}

static void draw_row(Home *h, int ri, float y, float alpha)
{
    Row *r = h->rows[ri];
    int focused_row = h->focus_row == ri;
    int index = 0;
    for (int i = 0; i < ri; i++) if (row_visible(h->rows[i])) index++;
    const char *source = r->kind == ROW_CATALOG && r->addon < g_app.n_addons ? g_app.addons[r->addon].manifest.name : NULL;
    if (g_theme.id == THEME_APERTURE) {
        font_draw_fit(r->title, LX, y - 8, FONT_L | FONT_ALT, focused_row ? THEME_TEXT : THEME_TEXT_DIM, SNTV_UI_W - LX - THEME_MARGIN - 200);
        if (focused_row && r->n) {
            char at[24]; snprintf(at, sizeof at, "%02d  /  %02d", r->focus + 1, r->n);
            font_draw(at, SNTV_UI_W - THEME_MARGIN - font_measure(at, FONT_S), y + 4, FONT_S, THEME_TEXT_DIM);
        }
    } else ui_row_header(index, r->title, source, r->n, LX, y, SNTV_UI_W - THEME_MARGIN, focused_row ? 1.0f : 0.0f);
    float cy = y + TITLE_H, cw = card_w(r), ch = card_h(r);
    if (!r->n) {
        if (r->loading || !r->requested) for (int j = 0; j < 8; j++) ui_skeleton(LX + j * (cw + GAP), cy, cw, ch, R_CARD);
        else if (r->failed) font_draw("Couldn't load this catalog. It will retry next time.", LX, cy + 20, FONT_S, THEME_TEXT_MUTED);
        return;
    }
    ui_pan_area(&r->pan, 0, cy, SNTV_UI_W, ch, 0);
    if (!r->shown_at) {   /* the pictures in view aren't in yet: the placeholders a moment longer */
        row_pictures(h, ri);
        if (!r->shown_at) { for (int j = 0; j < visible_cards(r) + 1; j++) ui_skeleton(LX + j * (cw + GAP) - r->scroll_x, cy, cw, ch, R_CARD); return; }
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
            float k = (g_app.time - r->shown_at - (j - first) * 0.04f) / 0.3f, ce = 1;
            if (k < 1 && !g_app.fx_light) { ce = k < 0 ? 0 : 1 - (1 - k) * (1 - k) * (1 - k); app_redraw(); }
            gfx_set_alpha(alpha * ce);
            float ry = cy + 22 * (1 - ce);
            float f = is_focus ? (g_app.fx_light ? 1.0f : h->focus_t) : is_prev ? h->prev_t : 0;
            float dim = focused_row ? 0 : 0.45f;
            const MetaPreview *m = &r->metas[j];
            if (g_theme.id == THEME_APERTURE) {
                ui_card(app_art_background(m, buf, sizeof buf), px, ry, cw, ch, f, dim, m->name, 0);
                LibraryItem *li = r->kind == ROW_CONTINUE ? app_library_find(m->id) : NULL;
                const NewEp *ne = r->kind == ROW_CONTINUE || r->kind == ROW_LIBRARY ? newep_get(m->id) : NULL;
                float frac = li && li->duration && !ne ? (float)li->time_offset / (float)li->duration : 0;
                if (frac > 0) gfx_rect(px, ry + ch + 4, cw, 3, COLOR_ALPHA(THEME_TEXT, 0x30));
                if (frac > 0) gfx_rect(px, ry + ch + 4, cw * frac, 3, THEME_ACCENT);   /* the focus marker sits under it otherwise */
                ui_spaced_caps(m->name, px + 4, ry + ch + 24, FONT_S | FONT_ALT, 4, is_focus ? THEME_TEXT : THEME_TEXT_DIM, cw - 8);
                if (ne) ui_new_episodes_badge(ne->count, px + cw - 12, ry + 12);
            } else if (r->kind == ROW_CONTINUE) {
                const char *bg = app_art_background(m, buf, sizeof buf);
                ui_card(bg, px, ry, cw, ch, f, dim, m->name, 0);
                float sc = ui_focus_scale(f), w2 = cw * sc, h2 = ch * sc, x2 = px - (w2 - cw) / 2, y2 = ry - (h2 - ch) / 2 - ui_focus_lift(f);
                cw_label(m, x2, y2, w2, h2);   /* label + progress inside the card */
            } else {
                ui_card(app_art_poster(m, buf, sizeof buf), px, ry, cw, ch, f, dim, m->name, 0);
                const NewEp *ne = r->kind == ROW_LIBRARY ? newep_get(m->id) : NULL;
                if (ne) {
                    float sc = ui_focus_scale(f), w2 = cw * sc, x2 = px - (w2 - cw) / 2, y2 = ry - (ch * sc - ch) / 2 - ui_focus_lift(f);
                    ui_new_episodes_dot(ne->count, x2 + w2 - 10, y2 + 10);
                }
            }
            ui_hit(px, ry, cw, ch, HIT_CARD(ri, j));
            if (is_focus && r->kind != ROW_CONTINUE) { m_peek = m; px_peek = px; f_peek = f; }
            if (is_focus && g_app.ok_hold > 0) {   /* holding OK: the hint takes the line under the card */
                float sc = ui_focus_scale(f);
                ui_hold_hint(px - (cw * sc - cw) / 2, cy + ch * sc - (ch * sc - ch) / 2 + 14, cw * sc > 240 ? cw * sc : 240);
            } else if (is_focus && r->kind != ROW_CONTINUE && h->peek.t < 0.05f && g_theme.id != THEME_APERTURE) {   /* Aperture titles every card */
                float sc = ui_focus_scale(f);
                font_draw_fit(m->name, px - (cw * sc - cw) / 2, cy + ch * sc - (ch * sc - ch) / 2 + 14, FONT_XS | FONT_B,
                              COLOR_ALPHA(THEME_TEXT, (int)(255 * f)), cw * sc);
            }
        }
    gfx_set_alpha(alpha);
    if (see_all(r)) {
        float px = LX + r->n * (cw + GAP) - r->scroll_x + (focused_row ? nudge(h, 1) : 0);
        if (px < SNTV_UI_W + 40) see_all_card(px, cy, cw, ch, focused_row && r->focus == r->n ? (g_app.fx_light ? 1.0f : h->focus_t) : 0, r->n, HIT_CARD(ri, r->n));
    }
    if (m_peek) {   /* in the room its neighbours made, after both passes */
        float sc = ui_focus_scale(f_peek), w2 = cw * sc, h2 = ch * sc;
        ui_peek_draw(&h->peek, m_peek, px_peek - (w2 - cw) / 2, cy - (h2 - ch) / 2 - ui_focus_lift(f_peek), w2, h2, LX, SNTV_UI_W - 40);
    }
}

static void draw(Screen *s)
{
    Home *h = s->st;
    if (SNTV_PHONE) { phone_draw(h); return; }
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
        if (tv < 1) hero_depth(h);
        if (cur) ui_backdrop_image(cur, iw, ih, fade * (1 - tv), bh);
        ui_backdrop_depth(0, 0);
        ui_text_scrim(1100, bh);
        bgtrailer_draw_badge(h->hero_id, 1);
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

/* A tapped card takes the focus; OK follows (it opens the title). */
static int tap(Screen *s, int id)
{
    Home *h = s->st;
    if (id == HIT_TOP) {
        if (g_touch_aim) return 1;
        h->focus_row = FOCUS_SHOWCASE;   /* the top of the page (Down from there goes to the first row) */
        h->phone_btn = 0;
        ui_pan_keys();   /* the page follows the focus up again */
        LOGI("home: back to the top");
        app_redraw();
        return 1;
    }
    if (SNTV_PHONE && (id == HIT_HERO || id == HIT_HERO_ADD)) { h->focus_row = FOCUS_SHOWCASE; h->phone_btn = id == HIT_HERO_ADD; return 0; }
    if (id == HIT_SHOWCASE) { h->focus_row = FOCUS_SHOWCASE; return 0; }
    if (id == HIT_HERO) return h->focus_row >= 0 ? 0 : 1;
    int row = id >> 12, col = id & 0xFFF;
    if (row < 0 || row >= h->n_rows || col >= h->rows[row]->n + see_all(h->rows[row])) return 1;
    if (h->focus_row != row || h->rows[row]->focus != col) move_focus(h, row, col);
    return 0;
}

static int key(Screen *s, KeyAction a)
{
    Home *h = s->st;
    app_redraw();
    if (SNTV_PHONE && a != KEY_SEARCH) return phone_key(h, a);
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
        if (r->focus < r->n - 1 + see_all(r)) move_focus(h, h->focus_row, r->focus + 1);
        else { h->nudge_at = g_app.time; h->nudge_dir = 1; }   /* the end of the row: a nudge, not nothing */
        return 1;
    case KEY_OK: if (r->focus < r->n) app_push(screen_detail_create(&r->metas[r->focus])); else if (see_all(r)) app_push(screen_library_create()); return 1;
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
    s->update = update; s->draw = draw; s->key = key; s->tap = tap; s->destroy = destroy; s->resume = resume; s->phone = 1;
    rebuild_rows(h);
    return s;
}
