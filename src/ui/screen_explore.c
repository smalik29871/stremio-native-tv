/* Explore: every catalog addon's lists by type, catalog and genre, as a poster
 * grid that loads more as you scroll (catalogs that take `skip`). Moods, last in the
 * type row, mix three genres across films and series, the best matches first. */
#include "../app.h"
#include "../http.h"
#include "../image.h"
#include "../log.h"
#include "font.h"
#include "rail.h"
#include "theme.h"
#include "widgets.h"
#include "screen_options.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

enum { MAX_TYPES = 8, MAX_CATS = 64, PAGE = 100 };
/* upright on a phone: three across, above the tab bar */
#define COLS (SNTV_PHONE ? 3 : 7)
#define CELL_W (SNTV_PHONE ? 220 : 196)
#define CELL_H (SNTV_PHONE ? 330 : 294)
#define CELL_GAP (SNTV_PHONE ? 20 : 28)
enum { Z_TYPES, Z_CATS, Z_GENRES, Z_GRID };
#define LX (SNTV_PHONE ? 40.0f : RAIL_LX)
#define GRID_Y 352.0f

typedef struct { int addon, catalog; } CatRef;
typedef struct Explore Explore;
typedef struct { Explore *owner; unsigned gen; int append; } Req;

#define MOODS_TYPE "moods"   /* not an addon type: the Moods entry */
static const struct { const char *name; const char *genres[3]; } MOODS[] = {
    { "Feel-good", { "Comedy", "Family", "Romance" } },
    { "Edge of your seat", { "Thriller", "Action", "Crime" } },
    { "Mind-bending", { "Sci-Fi", "Mystery", "Fantasy" } },
    { "Date night", { "Romance", "Comedy", "Drama" } },
    { "Family night", { "Family", "Animation", "Adventure" } },
    { "Spooky", { "Horror", "Mystery", "Thriller" } },
    { "Epic", { "Adventure", "History", "War" } },
    { "True stories", { "Documentary", "Biography", "History" } },
};
#define N_MOODS ((int)(sizeof MOODS / sizeof MOODS[0]))

struct Explore {
    char types[MAX_TYPES][24]; int n_types, type_i;
    CatRef cats[MAX_CATS]; int n_cats, cat_i;
    int genre_i;              /* 0 = all (or the first option when the catalog requires one) */
    MetaPreview *items; int n;
    int enter_n; float enter_at;   /* when the grid filled: its posters ease in */
    int loading, done, failed, requested;
    unsigned gen;
    int zone, focus;          /* grid focus index */
    int cat_focus, genre_focus;
    float focus_t, scroll, genre_scroll, cat_scroll;
    float grid_y, type_scroll; UiPan pan, cat_pan, genre_pan, type_pan;   /* where the grid starts on screen; a finger on it */
    Peek peek;                /* details beside the focused poster */
    int rail_focus;           /* -1: content has the focus */
    int addons_seen;
    char url[1024];
    int mood_pending;         /* Moods: catalog requests still out */
};

/* Explore screens alive now: a reply checks its owner is still here before touching it
 * (Back from Explore while a mood's six lists are out), and generations are never reused. */
#define MAX_LIVE 8
static Explore *g_live[MAX_LIVE];
static unsigned g_next_gen = 1;
static int alive(const Req *q)
{
    for (int i = 0; i < MAX_LIVE; i++) if (g_live[i] == q->owner) return q->gen == q->owner->gen;
    return 0;
}

static int is_moods(const Explore *e) { return e->n_types && strcmp(e->types[e->type_i], MOODS_TYPE) == 0; }

static const char *type_label(const char *t)
{
    if (!strcmp(t, MOODS_TYPE)) return "Moods";
    if (!strcmp(t, "movie")) return "Movies";
    if (!strcmp(t, "series")) return "Series";
    if (!strcmp(t, "channel")) return "Channels";
    if (!strcmp(t, "tv")) return "TV";
    if (!strcmp(t, "anime")) return "Anime";
    return t;
}

static const AddonCatalog *cur_cat(const Explore *e, const Addon **ad)
{
    if (e->cat_i >= e->n_cats || is_moods(e)) return NULL;
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
    int genre_lists = 0;   /* Moods need a catalog that filters by genre */
    for (int a = 0; a < g_app.n_addons && !genre_lists; a++)
        for (int c = 0; c < g_app.addons[a].manifest.n_catalogs; c++) {
            const AddonCatalog *cat = &g_app.addons[a].manifest.catalogs[c];
            const AddonExtra *g = addon_catalog_extra(cat, "genre");
            int kind = !strcmp(cat->type, "movie") || !strcmp(cat->type, "series");   /* the ones a mood asks */
            if (kind && g && g->options.n && usable(cat) && !(g_app.hide_adult && g_app.addons[a].manifest.bh_adult)) { genre_lists = 1; break; }
        }
    if (genre_lists && e->n_types < MAX_TYPES) snprintf(e->types[e->n_types++], 24, "%s", MOODS_TYPE);
    if (e->type_i >= e->n_types) e->type_i = 0;
    e->addons_seen = g_app.addons_version;
}

static void build_cats(Explore *e)
{
    e->n_cats = 0;
    if (!e->n_types) return;
    if (is_moods(e)) {   /* the list row holds the moods */
        e->n_cats = N_MOODS;
        if (e->cat_i >= e->n_cats) e->cat_i = 0;
        if (e->cat_focus >= e->n_cats) e->cat_focus = e->cat_i;
        e->genre_i = e->genre_focus = 0;
        return;
    }
    for (int a = 0; a < g_app.n_addons; a++)
        for (int c = 0; c < g_app.addons[a].manifest.n_catalogs && e->n_cats < MAX_CATS; c++) {
            const AddonCatalog *cat = &g_app.addons[a].manifest.catalogs[c];
            if (strcmp(cat->type, e->types[e->type_i]) != 0 || !usable(cat) || (g_app.hide_adult && g_app.addons[a].manifest.bh_adult)) continue;
            e->cats[e->n_cats].addon = a; e->cats[e->n_cats].catalog = c; e->n_cats++;
        }
    if (e->cat_i >= e->n_cats) e->cat_i = 0;
    if (e->cat_focus >= e->n_cats) e->cat_focus = e->cat_i;
    e->genre_i = 0;
    if (e->genre_focus > 0) e->genre_focus = 0;
}

static void reset_items(Explore *e)
{
    e->gen = g_next_gen++;
    meta_previews_free(e->items, e->n);
    e->items = NULL; e->n = 0;
    e->loading = e->done = e->failed = e->requested = 0;
    e->focus = 0; e->scroll = 0;
}

static void on_page(HttpResponse *r, void *user)
{
    Req *q = user;
    Explore *e = q->owner;
    if (!alive(q)) { free(q); return; }
    free(q);
    e->loading = 0;
    ParsedMetas *pm = r->parsed;
    MetaPreview *m = pm ? pm->m : NULL; int n = pm ? pm->n : 0;
    if (pm) { r->parsed = NULL; free(pm); }
    if (r->status != 200 || !m) {
        if (!e->n) e->failed = 1;
        e->done = 1;
        LOGW("explore: %s -> %ld %s", e->url, r->status, r->error);
        app_redraw();
        return;
    }
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

/* "Sci-Fi" is "Science Fiction" in some addons */
static int genre_is(const char *option, const char *want)
{
    if (!strcasecmp(option, want)) return 1;
    return !strcasecmp(want, "Sci-Fi") && (!strcasecmp(option, "Science Fiction") || !strcasecmp(option, "Sci-Fi & Fantasy"));
}

static int mood_score(int mood, const MetaPreview *m)
{
    int n = 0;
    for (int g = 0; g < 3; g++) for (int i = 0; i < m->genres.n; i++) if (genre_is(m->genres.v[i], MOODS[mood].genres[g])) { n++; break; }
    return n;
}

/* best first: more of the mood's genres, then the rating */
static void mood_sort(Explore *e)
{
    int mood = e->cat_i;
    for (int i = 1; i < e->n; i++)
        for (int j = i; j > 0; j--) {
            int sa = mood_score(mood, &e->items[j]), sb = mood_score(mood, &e->items[j - 1]);
            double ra = e->items[j].imdb_rating ? atof(e->items[j].imdb_rating) : 0, rb = e->items[j - 1].imdb_rating ? atof(e->items[j - 1].imdb_rating) : 0;
            if (sa > sb || (sa == sb && ra > rb)) { MetaPreview t = e->items[j]; e->items[j] = e->items[j - 1]; e->items[j - 1] = t; } else break;
        }
}

static void on_mood_page(HttpResponse *r, void *user)
{
    Req *q = user;
    Explore *e = q->owner;
    if (!alive(q)) { free(q); return; }
    free(q);
    if (--e->mood_pending <= 0) { e->loading = 0; e->done = 1; }
    ParsedMetas *pm = r->parsed;
    MetaPreview *m = pm ? pm->m : NULL; int n = pm ? pm->n : 0;
    if (pm) { r->parsed = NULL; free(pm); }
    if (r->status != 200 || !m) { if (e->done && !e->n) e->failed = 1; app_redraw(); return; }
    MetaPreview *all = realloc(e->items, (size_t)(e->n + n + 1) * sizeof(MetaPreview));
    if (!all) { meta_previews_free(m, n); return; }
    e->items = all;
    for (int i = 0; i < n; i++) {   /* each title once, unwatched when asked */
        int dup = !m[i].id || (g_app.hide_watched && app_title_watched(m[i].id));
        for (int k = 0; k < e->n && !dup; k++) dup = strcmp(e->items[k].id, m[i].id) == 0;
        if (dup) meta_preview_free(&m[i]); else e->items[e->n++] = m[i];
    }
    free(m);
    {   /* sorted again, the focused title kept under the focus */
        char keep[160] = "";
        if (e->zone == Z_GRID && e->focus < e->n && e->items[e->focus].id) snprintf(keep, sizeof keep, "%s", e->items[e->focus].id);
        mood_sort(e);
        if (keep[0]) for (int i = 0; i < e->n; i++) if (!strcmp(e->items[i].id, keep)) { e->focus = i; break; }
    }
    LOGI("explore: mood %s has %d titles%s", MOODS[e->cat_i].name, e->n, e->done ? "" : " so far");
    app_redraw();
}

static void fetch_mood(Explore *e)
{
    static const char *const TYPES[] = { "movie", "series" };
    e->requested = 1; e->mood_pending = 0;
    snprintf(e->url, sizeof e->url, "mood:%s", MOODS[e->cat_i].name);
    for (int g = 0; g < 3; g++)
        for (int t = 0; t < 2; t++) {
            int sent = 0;
            for (int a = 0; a < g_app.n_addons && !sent; a++) {
                const Addon *ad = &g_app.addons[a];
                if (g_app.hide_adult && ad->manifest.bh_adult) continue;
                for (int c = 0; c < ad->manifest.n_catalogs && !sent; c++) {
                    const AddonCatalog *cat = &ad->manifest.catalogs[c];
                    if (strcmp(cat->type, TYPES[t]) != 0 || !usable(cat)) continue;
                    const AddonExtra *ge = addon_catalog_extra(cat, "genre");
                    for (int o = 0; ge && o < ge->options.n && !sent; o++) {
                        if (!genre_is(ge->options.v[o], MOODS[e->cat_i].genres[g])) continue;
                        const char *kv[] = { "genre", ge->options.v[o], NULL };
                        char extra[256], url[1024];
                        addon_extra_query(extra, sizeof extra, kv);
                        addon_url_catalog(ad, cat->type, cat->id, extra, url, sizeof url);
                        Req *q = malloc(sizeof *q);
                        q->owner = e; q->gen = e->gen; q->append = 0;
                        e->mood_pending++;
                        app_fetch_metas(url, 1, on_mood_page, q);
                        sent = 1;
                    }
                }
            }
        }
    e->loading = e->mood_pending > 0;
    if (!e->mood_pending) { e->done = 1; e->failed = 1; }
    LOGI("explore: mood %s asks %d lists", MOODS[e->cat_i].name, e->mood_pending);
}

static void fetch(Explore *e, int append)
{
    if (is_moods(e)) { if (append) e->done = 1; else if (!e->loading) fetch_mood(e); return; }
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
    app_fetch_metas(url, !append, on_page, q);
}

static void update(Screen *s, float dt)
{
    Explore *e = s->st;
    if (e->addons_seen != g_app.addons_version) { build_types(e); build_cats(e); reset_items(e); }
    if (!e->requested && e->n_cats) fetch(e, 0);
    ui_peek_step(&e->peek, e->zone == Z_GRID && e->rail_focus < 0 && e->focus < e->n ? &e->items[e->focus] : NULL, dt);
    /* more when the focus nears the end */
    if (e->zone == Z_GRID && !e->done && !e->loading && e->n && e->focus >= e->n - COLS * 2 && !(g_app.low_memory && e->n >= 140)) fetch(e, 1);
    float row = e->zone == Z_GRID ? (float)(e->focus / COLS) : 0;
    float target = row * (CELL_H + 80);
    float most = (((e->n + COLS - 1) / COLS) * (CELL_H + 80)) - (SNTV_UI_H - e->grid_y - (SNTV_PHONE ? PHONE_TABS_H : 0)) + 40;
    ui_pan(&e->pan, &e->scroll, target, most, dt, 14);
    {   /* the posters of the rows in view and two below are asked for before they are scrolled to */
        int r0 = (int)(e->scroll / (CELL_H + 80)), rows = (int)((SNTV_UI_H - e->grid_y) / (CELL_H + 80)) + 3;
        char pb[512];
        for (int i = (r0 < 0 ? 0 : r0) * COLS; i < e->n && i < (r0 + rows) * COLS; i++) ui_art_ready(app_art_poster(&e->items[i], pb, sizeof pb));
    }
    if (ui_pan_free(&e->pan) && e->scroll > most - 2 * (CELL_H + 80) && !e->done && !e->loading && e->n && !(g_app.low_memory && e->n >= 140)) fetch(e, 1);   /* dragged near the end */
    e->focus_t = ui_ease(e->focus_t * 100, 100, NULL, dt, 16) / 100;
    if (e->loading) app_redraw();
    char buf[512];
    for (int i = e->focus - COLS; i < e->focus + COLS * 3 && i < e->n; i++) if (i >= 0) image_prefetch(app_art_poster(&e->items[i], buf, sizeof buf));
}

/* a horizontal row of pills; returns nothing, scrolls so the focused one shows */
enum { HIT_TYPE = 0x1000, HIT_CAT = 0x2000, HIT_GENRE = 0x3000 };   /* touch: a pill by its row; a title is its index */

static void draw_pills(float x, float y, float maxw, const char **labels, int n, int sel, int focused, int focus_i, float *scroll, UiPan *pan, int hit)
{
    float px = 0, fx = 0, fw = 0;
    for (int i = 0; i < n; i++) { float w = ui_pill_width(labels[i], 0, 52); if (i == focus_i) { fx = px; fw = w; } px += w + 12; }
    float want = *scroll;   /* stays put while the focused pill is in view */
    if (focused && fx + fw - *scroll > maxw) want = fx + fw - maxw;
    if (focused && fx - *scroll < 0) want = fx;
    ui_pan(pan, scroll, want, px - 12 - maxw, 1 / 60.0f, 200);   /* a finger drags them too */
    gfx_clip_push(x - 20, y - 10, maxw + 40, 72);
    ui_pan_area(pan, x - 20, y - 10, maxw + 40, 72, 0);
    px = x - *scroll;
    for (int i = 0; i < n; i++) {
        int foc = focused && i == focus_i;
        float w = ui_pill_width(labels[i], 0, 52);
        if (px + w > x - 20 && px < x + maxw + 20) { ui_pill(labels[i], 0, ICON_PLAY, px, y, 52, i == sel ? PILL_PRIMARY : PILL_GHOST, foc); ui_hit(px, y, w, 52, hit + i); }
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
    float maxw = SNTV_PHONE ? SNTV_UI_W - 2 * LX : SNTV_UI_W - LX - THEME_MARGIN - 220;
    if (!e->n_types) {
        font_draw_center(g_app.addons_loading ? "Loading your addons" : "No catalog addons installed", SNTV_UI_W / 2, SNTV_UI_H / 2, FONT_L | FONT_B, THEME_TEXT);
        font_draw_center("Catalog addons (Cinemeta, TMDB, …) appear here by type, list and genre.", SNTV_UI_W / 2, SNTV_UI_H / 2 + 60, FONT_S, THEME_TEXT_DIM);
        rail_draw(RAIL_EXPLORE, e->rail_focus);
        return;
    }
    const char *tl[MAX_TYPES];
    for (int i = 0; i < e->n_types; i++) tl[i] = type_label(e->types[i]);
    draw_pills(LX, 124, maxw, tl, e->n_types, e->type_i, e->zone == Z_TYPES && e->rail_focus < 0, e->type_i, &e->type_scroll, &e->type_pan, HIT_TYPE);
    /* catalogs */
    const char *cl[MAX_CATS]; char cn[MAX_CATS][96];
    for (int i = 0; i < e->n_cats; i++) {
        if (is_moods(e)) { cl[i] = MOODS[i].name; continue; }
        const Addon *a = &g_app.addons[e->cats[i].addon];
        snprintf(cn[i], sizeof cn[i], "%s  \xC2\xB7  %s", a->manifest.catalogs[e->cats[i].catalog].name, a->manifest.name);
        cl[i] = cn[i];
    }
    font_draw(is_moods(e) ? "MOOD" : "LIST", LX, 206, FONT_XS | FONT_B, THEME_TEXT_MUTED);
    if (is_moods(e) && e->cat_i < N_MOODS) {   /* what the mood mixes, where the genres would be */
        char mix[160];
        snprintf(mix, sizeof mix, "%s, %s and %s from your catalogs, films and series together", MOODS[e->cat_i].genres[0], MOODS[e->cat_i].genres[1], MOODS[e->cat_i].genres[2]);
        font_draw(mix, LX + 86, 280, FONT_S, THEME_TEXT_DIM);
    }
    draw_pills(LX + 70, 196, maxw - 70, cl, e->n_cats, e->cat_i, e->zone == Z_CATS && e->rail_focus < 0, e->cat_focus, &e->cat_scroll, &e->cat_pan, HIT_CAT);
    int required;
    const AddonExtra *g = genres(e, &required);
    if (g) {
        const char **gl = malloc((size_t)(g->options.n + 1) * sizeof(char *));
        int k = 0;
        if (!required) gl[k++] = "All";
        for (int i = 0; i < g->options.n; i++) gl[k++] = g->options.v[i];
        font_draw("GENRE", LX, 278, FONT_XS | FONT_B, THEME_TEXT_MUTED);
        draw_pills(LX + 86, 268, maxw - 86, gl, k, e->genre_i, e->zone == Z_GENRES && e->rail_focus < 0, e->genre_focus, &e->genre_scroll, &e->genre_pan, HIT_GENRE);
        free(gl);
    }
    /* grid */
    float gx = LX, gy = GRID_Y;
    gfx_clip_push(gx - 30, gy - 20, SNTV_UI_W - gx, SNTV_UI_H - gy + 20);
    e->grid_y = gy;
    ui_pan_area(&e->pan, gx - 30, gy - 20, SNTV_UI_W - gx, SNTV_UI_H - gy + 20, 1);
    if (!e->n && (e->loading || !e->requested)) {
        for (int i = 0; i < COLS * 2; i++) ui_skeleton(gx + (i % COLS) * (CELL_W + CELL_GAP), gy + (i / COLS) * (CELL_H + 80), CELL_W, CELL_H, R_CARD);
    } else if (!e->n) {
        font_draw(e->failed ? "This list could not be loaded. It will retry when you come back." : "Nothing in this list.", gx, gy + 40, FONT_S, THEME_TEXT_DIM);
    }
    float row_h = CELL_H + 80, peek_x = 0, peek_y = 0;
    /* details open on the focused card: the others in its row slide aside to make room */
    float room = 0; int room_right = 1, room_row = -1;
    if (!SNTV_PHONE && e->zone == Z_GRID && e->rail_focus < 0 && e->focus < e->n) {
        float sc = ui_focus_scale(g_app.fx_light ? 1.0f : e->focus_t), w2 = CELL_W * sc;
        float fx = gx + (e->focus % COLS) * (CELL_W + CELL_GAP) - (w2 - CELL_W) / 2;
        room = ui_peek_room(&e->peek, &e->items[e->focus], fx, w2, CELL_H * sc, gx, SNTV_UI_W - 40, &room_right);
        room_row = e->focus / COLS;
    }
    ui_list_enter(e->n, &e->enter_n, &e->enter_at);
    int first_i = (int)(e->scroll / row_h) * COLS;
    for (int pass = 0; pass < 2; pass++)
        for (int i = 0; i < e->n; i++) {
            int r = i / COLS, c = i % COLS;
            float ce = ui_enter(e->enter_at, i - first_i);
            float px = gx + c * (CELL_W + CELL_GAP), py = gy + r * row_h - e->scroll + 26 * (1 - ce);
            if (room > 0 && r == room_row && (room_right ? i > e->focus : i < e->focus)) px += room_right ? room : -room;
            if (py > SNTV_UI_H || py + row_h < gy - 20) continue;
            int foc = e->zone == Z_GRID && e->rail_focus < 0 && e->focus == i;
            if ((pass == 1) != foc) continue;
            gfx_set_alpha(ce);
            char pb[512];
            if (foc) { peek_x = px; peek_y = py; }
            ui_card(app_art_poster(&e->items[i], pb, sizeof pb), px, py, CELL_W, CELL_H, foc ? (g_app.fx_light ? 1 : e->focus_t) : 0, e->zone == Z_GRID && !foc ? 0.25f : 0, e->items[i].name, 0);
            ui_hit(px, py, CELL_W, CELL_H, i);
            font_draw_fit(e->items[i].name, px, py + CELL_H + (foc ? 24 : 12), FONT_XS | FONT_B, foc ? THEME_TEXT : THEME_TEXT_DIM, CELL_W);
            if (foc && ui_hold_hint(px, py + CELL_H + 52, CELL_W)) {}
            else if (e->items[i].release_info) font_draw(e->items[i].release_info, px, py + CELL_H + (foc ? 50 : 38), FONT_XS, THEME_TEXT_MUTED);
        }
    gfx_set_alpha(1);
    if (!SNTV_PHONE && e->zone == Z_GRID && e->rail_focus < 0 && e->focus < e->n) {
        float f = g_app.fx_light ? 1 : e->focus_t, sc = ui_focus_scale(f), w2 = CELL_W * sc, h2 = CELL_H * sc;
        ui_peek_draw(&e->peek, &e->items[e->focus], peek_x - (w2 - CELL_W) / 2, peek_y - (h2 - CELL_H) / 2 - ui_focus_lift(f), w2, h2, gx, SNTV_UI_W - 40);
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
    if (a == KEY_LONG_OK || a == KEY_YELLOW || a == KEY_MENU || a == KEY_INFO) {
        if (e->rail_focus < 0 && e->zone == Z_GRID && e->focus < e->n) { screen_options_for_title(&e->items[e->focus]); return 1; }
        if (a == KEY_LONG_OK) return 0;
    }
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
        if (e->zone == Z_GRID) e->zone = ng ? Z_GENRES : e->n_cats ? Z_CATS : Z_TYPES;
        else if (e->zone == Z_GENRES) e->zone = Z_CATS;
        else if (e->zone == Z_CATS) e->zone = Z_TYPES;
        if (e->zone == Z_CATS) e->cat_focus = e->cat_i;   /* land on the applied pill, not the last one browsed */
        if (e->zone == Z_GENRES) e->genre_focus = e->genre_i;
        return 1;
    case KEY_DOWN:
        if (e->zone == Z_TYPES) e->zone = e->n_cats ? Z_CATS : e->n ? Z_GRID : Z_TYPES;
        else if (e->zone == Z_CATS) e->zone = ng ? Z_GENRES : (e->n ? Z_GRID : Z_CATS);
        else if (e->zone == Z_GENRES) { if (e->n) e->zone = Z_GRID; }
        else if (e->focus + COLS < e->n) { e->focus += COLS; e->focus_t = 0; }
        else if (e->n && e->focus / COLS < (e->n - 1) / COLS) { e->focus = e->n - 1; e->focus_t = 0; }   /* a short last row */
        else if (e->zone == Z_GRID) ui_bump(0, 1);
        if (e->zone == Z_CATS) e->cat_focus = e->cat_i;
        if (e->zone == Z_GENRES) e->genre_focus = e->genre_i;
        return 1;
    case KEY_LEFT:
        if (e->zone == Z_TYPES) { if (e->type_i > 0) { e->type_i--; e->cat_i = e->cat_focus = 0; build_cats(e); reset_items(e); } else e->rail_focus = RAIL_EXPLORE; }
        else if (e->zone == Z_CATS) { if (e->cat_focus > 0) e->cat_focus--; else e->rail_focus = RAIL_EXPLORE; }
        else if (e->zone == Z_GENRES) { if (e->genre_focus > 0) e->genre_focus--; else e->rail_focus = RAIL_EXPLORE; }
        else if (e->focus % COLS) { e->focus--; e->focus_t = 0; } else e->rail_focus = RAIL_EXPLORE;
        return 1;
    case KEY_RIGHT:
        if (e->zone == Z_TYPES) { if (e->type_i + 1 < e->n_types) { e->type_i++; e->cat_i = e->cat_focus = 0; build_cats(e); reset_items(e); } }   /* a new type has its own lists */
        else if (e->zone == Z_CATS) { if (e->cat_focus + 1 < e->n_cats) e->cat_focus++; }
        else if (e->zone == Z_GENRES) { if (e->genre_focus + 1 < ng) e->genre_focus++; }
        else if (e->focus % COLS < COLS - 1 && e->focus + 1 < e->n) { e->focus++; e->focus_t = 0; }
        else ui_bump(1, 0);
        return 1;
    case KEY_OK:
        if (e->zone == Z_CATS) { if (e->cat_focus != e->cat_i) { e->cat_i = e->cat_focus; e->genre_i = 0; e->genre_focus = 0; reset_items(e); } }
        else if (e->zone == Z_GENRES) { if (e->genre_focus != e->genre_i) { e->genre_i = e->genre_focus; reset_items(e); } }
        else if (e->zone == Z_GRID) { if (e->focus < e->n) app_push(screen_detail_create(&e->items[e->focus])); }
        else e->zone = Z_CATS;
        return 1;
    case KEY_BACK:   /* grid, then the filters, then the rail; Back there goes Home */
        if (e->zone == Z_GRID) { e->zone = Z_TYPES; return 1; }
        e->rail_focus = RAIL_EXPLORE;
        return 1;
    default: return 0;
    }
}

/* A tapped type shows its lists; a list, a genre or a title takes the focus and OK follows. */
static int tap(Screen *s, int id)
{
    Explore *e = s->st;
    e->rail_focus = -1;
    if (id >= HIT_TYPE && id < HIT_TYPE + e->n_types) {
        e->zone = Z_TYPES;
        if (id - HIT_TYPE != e->type_i && !g_touch_aim) { e->type_i = id - HIT_TYPE; e->cat_i = e->cat_focus = 0; build_cats(e); reset_items(e); }
        return 1;
    }
    if (id >= HIT_CAT && id < HIT_CAT + e->n_cats) { e->zone = Z_CATS; e->cat_focus = id - HIT_CAT; return 0; }
    if (id >= HIT_GENRE && id < HIT_GENRE + n_genre_choices(e)) { e->zone = Z_GENRES; e->genre_focus = id - HIT_GENRE; return 0; }
    if (id < 0 || id >= e->n) return 1;
    if (e->zone != Z_GRID || e->focus != id) { e->zone = Z_GRID; e->focus = id; e->focus_t = 0; }
    return 0;
}

static void resume(Screen *s) { Explore *e = s->st; e->rail_focus = -1; app_redraw(); }   /* back from a rail target: the content */

static void destroy(Screen *s)
{
    Explore *e = s->st;
    for (int i = 0; i < MAX_LIVE; i++) if (g_live[i] == e) g_live[i] = NULL;
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
    e->gen = g_next_gen++;
    for (int i = 0; i < MAX_LIVE; i++) if (!g_live[i]) { g_live[i] = e; break; }
    s->name = "explore"; s->st = e; s->long_ok = 1;
    s->update = update; s->draw = draw; s->key = key; s->tap = tap; s->destroy = destroy; s->resume = resume; s->phone = 1;
    return s;
}
