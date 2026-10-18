/* Explore: every catalog addon's lists by type, catalog and genre, as a poster
 * grid that loads more as you scroll (catalogs that take `skip`). */
#include "../app.h"
#include "../http.h"
#include "../image.h"
#include "../log.h"
#include "font.h"
#include "rail.h"
#include "theme.h"
#include "widgets.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { COLS = 7, CELL_W = 196, CELL_H = 294, CELL_GAP = 28, MAX_TYPES = 8, MAX_CATS = 64, PAGE = 100 };
enum { Z_TYPES, Z_CATS, Z_GENRES, Z_GRID };
#define LX RAIL_LX
#define GRID_Y 352.0f

typedef struct { int addon, catalog; } CatRef;
typedef struct Explore Explore;
typedef struct { Explore *owner; unsigned gen; int append; } Req;

struct Explore {
    char types[MAX_TYPES][24]; int n_types, type_i;
    CatRef cats[MAX_CATS]; int n_cats, cat_i;
    int genre_i;              /* 0 = all (or the first option when the catalog requires one) */
    MetaPreview *items; int n;
    int loading, done, failed, requested;
    unsigned gen;
    int zone, focus;          /* grid focus index */
    int cat_focus, genre_focus;
    float focus_t, scroll, genre_scroll, cat_scroll;
    int rail_focus;           /* -1: content has the focus */
    int addons_seen;
    char url[1024];
};

static const char *type_label(const char *t)
{
    if (!strcmp(t, "movie")) return "Movies";
    if (!strcmp(t, "series")) return "Series";
    if (!strcmp(t, "channel")) return "Channels";
    if (!strcmp(t, "tv")) return "TV";
    if (!strcmp(t, "anime")) return "Anime";
    return t;
}

static const AddonCatalog *cur_cat(const Explore *e, const Addon **ad)
{
    if (e->cat_i >= e->n_cats) return NULL;
    const Addon *a = &g_app.addons[e->cats[e->cat_i].addon];
    if (ad) *ad = a;
    return &a->manifest.catalogs[e->cats[e->cat_i].catalog];
}

/* genre options of the current catalog; `required` tells whether "All" is a choice */
static const AddonExtra *genres(const Explore *e, int *required)
{
    const AddonCatalog *c = cur_cat(e, NULL);
    *required = 0;
    if (!c) return NULL;
    const AddonExtra *g = addon_catalog_extra(c, "genre");
    if (!g || !g->options.n) return NULL;
    *required = g->is_required;
    return g;
}
static int n_genre_choices(const Explore *e) { int req; const AddonExtra *g = genres(e, &req); return g ? g->options.n + !req : 0; }

/* A catalog belongs here when its only required extra (if any) is a genre with options. */
static int usable(const AddonCatalog *c)
{
    for (int i = 0; i < c->n_extra; i++)
        if (c->extra[i].is_required && !(strcmp(c->extra[i].name, "genre") == 0 && c->extra[i].options.n)) return 0;
    return 1;
}

static void build_types(Explore *e)
{
    e->n_types = 0;
    for (int a = 0; a < g_app.n_addons; a++)
        for (int c = 0; c < g_app.addons[a].manifest.n_catalogs; c++) {
            const AddonCatalog *cat = &g_app.addons[a].manifest.catalogs[c];
            if (!usable(cat) || (g_app.hide_adult && g_app.addons[a].manifest.bh_adult)) continue;
            int k; for (k = 0; k < e->n_types; k++) if (!strcmp(e->types[k], cat->type)) break;
            if (k == e->n_types && e->n_types < MAX_TYPES) snprintf(e->types[e->n_types++], 24, "%s", cat->type);
        }
    /* movies, then series, then the rest */
    for (int i = 0; i < e->n_types; i++)
        for (int j = i; j > 0; j--) {
            int ra = !strcmp(e->types[j], "movie") ? 0 : !strcmp(e->types[j], "series") ? 1 : 2;
            int rb = !strcmp(e->types[j - 1], "movie") ? 0 : !strcmp(e->types[j - 1], "series") ? 1 : 2;
            if (ra < rb) { char t[24]; memcpy(t, e->types[j], 24); memcpy(e->types[j], e->types[j - 1], 24); memcpy(e->types[j - 1], t, 24); } else break;
        }
    if (e->type_i >= e->n_types) e->type_i = 0;
    e->addons_seen = g_app.addons_version;
}

static void build_cats(Explore *e)
{
    e->n_cats = 0;
    if (!e->n_types) return;
    for (int a = 0; a < g_app.n_addons; a++)
        for (int c = 0; c < g_app.addons[a].manifest.n_catalogs && e->n_cats < MAX_CATS; c++) {
            const AddonCatalog *cat = &g_app.addons[a].manifest.catalogs[c];
            if (strcmp(cat->type, e->types[e->type_i]) != 0 || !usable(cat) || (g_app.hide_adult && g_app.addons[a].manifest.bh_adult)) continue;
            e->cats[e->n_cats].addon = a; e->cats[e->n_cats].catalog = c; e->n_cats++;
        }
    if (e->cat_i >= e->n_cats) e->cat_i = 0;
    e->genre_i = 0;
}

static void reset_items(Explore *e)
{
    e->gen++;
    meta_previews_free(e->items, e->n);
    e->items = NULL; e->n = 0;
    e->loading = e->done = e->failed = e->requested = 0;
    e->focus = 0; e->scroll = 0;
}

static void on_page(HttpResponse *r, void *user)
{
    Req *q = user;
    Explore *e = q->owner;
    int append = q->append;
    if (q->gen != e->gen) { free(q); return; }
    free(q);
    e->loading = 0;
    MetaPreview *m; int n;
    if (r->status != 200 || addon_parse_metas(r->body, &m, &n) != 0) {
        if (!e->n) e->failed = 1;
        e->done = 1;
        LOGW("explore: %s -> %ld %s", e->url, r->status, r->error);
        app_redraw();
        return;
    }
    if (!append) app_cache_put(e->url, r->body, r->len);
    if (g_app.hide_watched) {
        int k = 0;
        for (int i = 0; i < n; i++) { if (app_title_watched(m[i].id)) meta_preview_free(&m[i]); else m[k++] = m[i]; }
        n = k;
    }
    if (n == 0) { e->done = 1; app_redraw(); return; }
    MetaPreview *all = realloc(e->items, (size_t)(e->n + n + 1) * sizeof(MetaPreview));
    if (!all) { meta_previews_free(m, n); return; }
    e->items = all;
    memcpy(e->items + e->n, m, (size_t)n * sizeof(MetaPreview));
    free(m);   /* the previews moved */
    e->n += n;
    if (n < 20) e->done = 1;
    app_redraw();
}

static void fetch(Explore *e, int append)
{
    const Addon *ad;
    const AddonCatalog *c = cur_cat(e, &ad);
    if (!c || e->loading) return;
    int required;
    const AddonExtra *g = genres(e, &required);
    const char *kv[8]; int k = 0;
    char skip[16];
    if (g && (required || e->genre_i > 0)) { kv[k++] = "genre"; kv[k++] = g->options.v[required ? e->genre_i : e->genre_i - 1]; }
    if (append) {
        if (!addon_catalog_extra(c, "skip")) { e->done = 1; return; }
        snprintf(skip, sizeof skip, "%d", e->n);
        kv[k++] = "skip"; kv[k++] = skip;
    }
    kv[k] = NULL;
    char extra[1024];
    addon_extra_query(extra, sizeof extra, kv);
    char url[1024];
    addon_url_catalog(ad, c->type, c->id, extra, url, sizeof url);
    if (!append) {
        snprintf(e->url, sizeof e->url, "%s", url);
        char *cached = app_cache_get(url, 24 * 3600);
        if (cached) {
            MetaPreview *m; int n;
            if (addon_parse_metas(cached, &m, &n) == 0) { e->items = m; e->n = n; }
            free(cached);
        }
    }
    e->requested = 1;
    e->loading = 1;
    Req *q = malloc(sizeof *q);
    q->owner = e; q->gen = e->gen; q->append = append;
    http_request_async(url, NULL, on_page, q);
}

static void update(Screen *s, float dt)
{
    Explore *e = s->st;
    if (e->addons_seen != g_app.addons_version) { build_types(e); build_cats(e); reset_items(e); }
    if (!e->requested && e->n_cats) fetch(e, 0);
    /* more when the focus nears the end */
    if (e->zone == Z_GRID && !e->done && !e->loading && e->n && e->focus >= e->n - COLS * 2 && !(g_app.low_memory && e->n >= 140)) fetch(e, 1);
    float row = e->zone == Z_GRID ? (float)(e->focus / COLS) : 0;
    float target = row * (CELL_H + 80);
    e->scroll = ui_ease(e->scroll, target, NULL, dt, 14);
    e->focus_t = ui_ease(e->focus_t * 100, 100, NULL, dt, 16) / 100;
    if (e->loading) app_redraw();
    char buf[512];
    for (int i = e->focus - COLS; i < e->focus + COLS * 3 && i < e->n; i++) if (i >= 0) image_prefetch(app_art_poster(&e->items[i], buf, sizeof buf));
}

/* a horizontal row of pills; returns nothing, scrolls so the focused one shows */
static void draw_pills(float x, float y, float maxw, const char **labels, int n, int sel, int focused, int focus_i, float *scroll, float dt_unused)
{
    (void)dt_unused;
    float px = 0, fx = 0, fw = 0;
    for (int i = 0; i < n; i++) { float w = ui_pill_width(labels[i], 0, 52); if (i == focus_i) { fx = px; fw = w; } px += w + 12; }
    float want = 0;
    if (focused && fx + fw - *scroll > maxw) want = fx + fw - maxw;
    if (focused && fx - *scroll < 0) want = fx;
    if (focused) *scroll = want;
    gfx_clip_push(x - 20, y - 10, maxw + 40, 72);
    px = x - *scroll;
    for (int i = 0; i < n; i++) {
        int foc = focused && i == focus_i;
        float w = ui_pill_width(labels[i], 0, 52);
        if (px + w > x - 20 && px < x + maxw + 20) ui_pill(labels[i], 0, ICON_PLAY, px, y, 52, i == sel ? PILL_PRIMARY : PILL_GHOST, foc);
        if (i == sel && !foc) gfx_rrect(px + w / 2 - 10, y + 52 + 4, 20, 3, 1.5f, THEME_ACCENT2);
        px += w + 12;
    }
    gfx_clip_pop();
}

static void draw(Screen *s)
{
    Explore *e = s->st;
    const MetaPreview *fp = e->zone == Z_GRID && e->focus < e->n ? &e->items[e->focus] : NULL;
    char buf[512];
    const char *bg = fp ? app_art_background(fp, buf, sizeof buf) : NULL;
    ui_background(bg ? image_tint(bg) : 0);
    font_draw("Explore", LX, 52, FONT_L | FONT_B, THEME_TEXT);
    float maxw = SNTV_UI_W - LX - THEME_MARGIN - 220;
    if (!e->n_types) {
        font_draw_center(g_app.addons_loading ? "Loading your addons" : "No catalog addons installed", SNTV_UI_W / 2, SNTV_UI_H / 2, FONT_L | FONT_B, THEME_TEXT);
        font_draw_center("Catalog addons (Cinemeta, TMDB, …) appear here by type, list and genre.", SNTV_UI_W / 2, SNTV_UI_H / 2 + 60, FONT_S, THEME_TEXT_DIM);
        rail_draw(RAIL_EXPLORE, e->rail_focus);
        return;
    }
    const char *tl[MAX_TYPES];
    for (int i = 0; i < e->n_types; i++) tl[i] = type_label(e->types[i]);
    float dummy = 0;
    draw_pills(LX, 124, maxw, tl, e->n_types, e->type_i, e->zone == Z_TYPES && e->rail_focus < 0, e->type_i, &dummy, 0);
    /* catalogs */
    const char *cl[MAX_CATS]; char cn[MAX_CATS][96];
    for (int i = 0; i < e->n_cats; i++) {
        const Addon *a = &g_app.addons[e->cats[i].addon];
        snprintf(cn[i], sizeof cn[i], "%s  \xC2\xB7  %s", a->manifest.catalogs[e->cats[i].catalog].name, a->manifest.name);
        cl[i] = cn[i];
    }
    font_draw("LIST", LX, 206, FONT_XS | FONT_B, THEME_TEXT_MUTED);
    draw_pills(LX + 70, 196, maxw - 70, cl, e->n_cats, e->cat_i, e->zone == Z_CATS && e->rail_focus < 0, e->cat_focus, &e->cat_scroll, 0);
    int required;
    const AddonExtra *g = genres(e, &required);
    if (g) {
        const char **gl = malloc((size_t)(g->options.n + 1) * sizeof(char *));
        int k = 0;
        if (!required) gl[k++] = "All";
        for (int i = 0; i < g->options.n; i++) gl[k++] = g->options.v[i];
        font_draw("GENRE", LX, 278, FONT_XS | FONT_B, THEME_TEXT_MUTED);
        draw_pills(LX + 86, 268, maxw - 86, gl, k, e->genre_i, e->zone == Z_GENRES && e->rail_focus < 0, e->genre_focus, &e->genre_scroll, 0);
        free(gl);
    }
    /* grid */
    float gx = LX, gy = GRID_Y;
    gfx_clip_push(gx - 30, gy - 20, SNTV_UI_W - gx, SNTV_UI_H - gy + 20);
    if (!e->n && (e->loading || !e->requested)) {
        for (int i = 0; i < COLS * 2; i++) ui_skeleton(gx + (i % COLS) * (CELL_W + CELL_GAP), gy + (i / COLS) * (CELL_H + 80), CELL_W, CELL_H, R_CARD);
    } else if (!e->n) {
        font_draw(e->failed ? "This list could not be loaded. It will retry when you come back." : "Nothing in this list.", gx, gy + 40, FONT_S, THEME_TEXT_DIM);
    }
    float row_h = CELL_H + 80;
    for (int pass = 0; pass < 2; pass++)
        for (int i = 0; i < e->n; i++) {
            int r = i / COLS, c = i % COLS;
            float px = gx + c * (CELL_W + CELL_GAP), py = gy + r * row_h - e->scroll;
            if (py > SNTV_UI_H || py + row_h < gy - 20) continue;
            int foc = e->zone == Z_GRID && e->rail_focus < 0 && e->focus == i;
            if ((pass == 1) != foc) continue;
            char pb[512];
            ui_card(app_art_poster(&e->items[i], pb, sizeof pb), px, py, CELL_W, CELL_H, foc ? (g_app.fx_light ? 1 : e->focus_t) : 0, e->zone == Z_GRID && !foc ? 0.25f : 0, e->items[i].name, 0);
            font_draw_fit(e->items[i].name, px, py + CELL_H + (foc ? 24 : 12), FONT_XS | FONT_B, foc ? THEME_TEXT : THEME_TEXT_DIM, CELL_W);
            if (e->items[i].release_info) font_draw(e->items[i].release_info, px, py + CELL_H + (foc ? 50 : 38), FONT_XS, THEME_TEXT_MUTED);
        }
    gfx_clip_pop();
    if (e->loading && e->n) ui_spinner(SNTV_UI_W - THEME_MARGIN - 30, GRID_Y - 40, 14);
    char cnt[48];
    if (e->n) { snprintf(cnt, sizeof cnt, "%d title%s%s", e->n, e->n == 1 ? "" : "s", e->done ? "" : "+"); font_draw(cnt, SNTV_UI_W - THEME_MARGIN - font_measure(cnt, FONT_XS | FONT_B) - (e->loading ? 60 : 0), GRID_Y - 50, FONT_XS | FONT_B, THEME_TEXT_MUTED); }
    rail_draw(RAIL_EXPLORE, e->rail_focus);
}

static int key(Screen *s, KeyAction a)
{
    Explore *e = s->st;
    app_redraw();
    if (a == KEY_SEARCH) { app_push(screen_search_create()); return 1; }
    if (e->rail_focus >= 0) {
        int r = rail_key(a, &e->rail_focus, RAIL_EXPLORE);
        if (r == 2) { e->rail_focus = -1; return 1; }
        if (r == 1) return 1;
        if (a == KEY_BACK) { app_pop(); return 1; }
        return 0;
    }
    int ng = n_genre_choices(e);
    switch (a) {
    case KEY_UP:
        if (e->zone == Z_GRID && e->focus >= COLS) { e->focus -= COLS; e->focus_t = 0; return 1; }
        if (e->zone == Z_GRID) e->zone = ng ? Z_GENRES : Z_CATS;
        else if (e->zone == Z_GENRES) e->zone = Z_CATS;
        else if (e->zone == Z_CATS) e->zone = Z_TYPES;
        return 1;
    case KEY_DOWN:
        if (e->zone == Z_TYPES) e->zone = e->n_cats ? Z_CATS : Z_GRID;
        else if (e->zone == Z_CATS) e->zone = ng ? Z_GENRES : (e->n ? Z_GRID : Z_CATS);
        else if (e->zone == Z_GENRES) { if (e->n) e->zone = Z_GRID; }
        else if (e->focus + COLS < e->n) { e->focus += COLS; e->focus_t = 0; }
        else if (e->n && !e->done) { e->focus = e->n - 1; e->focus_t = 0; }
        if (e->zone == Z_CATS) e->cat_focus = e->cat_i;
        if (e->zone == Z_GENRES) e->genre_focus = e->genre_i;
        return 1;
    case KEY_LEFT:
        if (e->zone == Z_TYPES) { if (e->type_i > 0) { e->type_i--; build_cats(e); reset_items(e); } else e->rail_focus = RAIL_EXPLORE; }
        else if (e->zone == Z_CATS) { if (e->cat_focus > 0) e->cat_focus--; else e->rail_focus = RAIL_EXPLORE; }
        else if (e->zone == Z_GENRES) { if (e->genre_focus > 0) e->genre_focus--; else e->rail_focus = RAIL_EXPLORE; }
        else if (e->focus % COLS) { e->focus--; e->focus_t = 0; } else e->rail_focus = RAIL_EXPLORE;
        return 1;
    case KEY_RIGHT:
        if (e->zone == Z_TYPES) { if (e->type_i + 1 < e->n_types) { e->type_i++; build_cats(e); reset_items(e); } }
        else if (e->zone == Z_CATS) { if (e->cat_focus + 1 < e->n_cats) e->cat_focus++; }
        else if (e->zone == Z_GENRES) { if (e->genre_focus + 1 < ng) e->genre_focus++; }
        else if (e->focus % COLS < COLS - 1 && e->focus + 1 < e->n) { e->focus++; e->focus_t = 0; }
        return 1;
    case KEY_OK:
        if (e->zone == Z_CATS) { if (e->cat_focus != e->cat_i) { e->cat_i = e->cat_focus; e->genre_i = 0; e->genre_focus = 0; reset_items(e); } }
        else if (e->zone == Z_GENRES) { if (e->genre_focus != e->genre_i) { e->genre_i = e->genre_focus; reset_items(e); } }
        else if (e->zone == Z_GRID) { if (e->focus < e->n) app_push(screen_detail_create(&e->items[e->focus])); }
        else e->zone = Z_CATS;
        return 1;
    case KEY_BACK:
        if (e->zone == Z_GRID) { e->zone = Z_TYPES; return 1; }
        app_pop();
        return 1;
    default: return 0;
    }
}

static void resume(Screen *s) { (void)s; app_redraw(); }

static void destroy(Screen *s)
{
    Explore *e = s->st;
    e->gen++;
    meta_previews_free(e->items, e->n);
    free(e);
    free(s);
}

Screen *screen_explore_create(void)
{
    Screen *s = calloc(1, sizeof *s);
    Explore *e = calloc(1, sizeof *e);
    e->rail_focus = -1;
    e->zone = Z_TYPES;
    e->focus_t = 1;
    e->addons_seen = -1;
    s->name = "explore"; s->st = e;
    s->update = update; s->draw = draw; s->key = key; s->destroy = destroy; s->resume = resume;
    return s;
}
