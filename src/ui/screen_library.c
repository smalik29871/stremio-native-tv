/* Library: everything in the account's library as a poster grid, with filters
 * (all, movies, series, continue watching) and a sort (recent, name). */
#include "../app.h"
#include "../image.h"
#include "font.h"
#include "rail.h"
#include "theme.h"
#include "widgets.h"
#include "screen_options.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { COLS = 7, CELL_W = 196, CELL_H = 294, CELL_GAP = 28 };
enum { F_ALL, F_MOVIES, F_SERIES, F_CONTINUE, F_COUNT };
enum { S_RECENT, S_NAME, S_COUNT };
enum { Z_FILTERS, Z_GRID };
#define LX RAIL_LX
#define GRID_Y 230.0f

static const char *const FILTERS[F_COUNT] = { "All", "Movies", "Series", "Continue watching" };
static const char *const SORTS[S_COUNT] = { "Recent", "A to Z" };

typedef struct {
    int filter, sort;
    MetaPreview *items; int n;
    const LibraryItem **src;      /* parallel to items */
    int zone, focus, pill_focus;  /* pill_focus: 0..F_COUNT-1 filters, F_COUNT = sort */
    float focus_t, scroll;
    Peek peek;                /* details beside the focused poster */
    int rail_focus;
    int seen_version, built_filter, built_sort;
} Library;

static MetaPreview preview_of(const LibraryItem *li)
{
    MetaPreview p; memset(&p, 0, sizeof p);
    p.id = strdup(li->id); p.type = strdup(li->type); p.name = strdup(li->name);
    p.poster = li->poster ? strdup(li->poster) : NULL;
    p.poster_shape = li->poster_shape ? strdup(li->poster_shape) : NULL;
    return p;
}

static void build(Library *l)
{
    meta_previews_free(l->items, l->n); free(l->src);
    l->items = NULL; l->src = NULL; l->n = 0;
    const LibraryItem **pick = calloc((size_t)g_app.n_library + 1, sizeof *pick);
    int n = 0;
    for (int i = 0; i < g_app.n_library; i++) {
        const LibraryItem *li = &g_app.library[i];
        int cont = li->time_offset > 0 && li->duration > 0 && (!li->removed || li->temp);
        if (l->filter == F_CONTINUE) { if (!cont) continue; }
        else {
            if (li->removed || li->temp) continue;
            if (l->filter == F_MOVIES && strcmp(li->type, "movie") != 0) continue;
            if (l->filter == F_SERIES && strcmp(li->type, "series") != 0) continue;
        }
        pick[n++] = li;
    }
    for (int i = 1; i < n; i++)
        for (int j = i; j > 0; j--) {
            int swap = l->sort == S_NAME ? strcasecmp(pick[j]->name, pick[j - 1]->name) < 0
                     : (l->filter == F_CONTINUE ? pick[j]->last_watched_ms > pick[j - 1]->last_watched_ms : pick[j]->mtime_ms > pick[j - 1]->mtime_ms);
            if (swap) { const LibraryItem *t = pick[j]; pick[j] = pick[j - 1]; pick[j - 1] = t; } else break;
        }
    l->items = calloc((size_t)n + 1, sizeof(MetaPreview));
    for (int i = 0; i < n; i++) l->items[i] = preview_of(pick[i]);
    l->src = pick; l->n = n;
    if (l->focus >= n) l->focus = n ? n - 1 : 0;
    l->seen_version = g_app.library_version; l->built_filter = l->filter; l->built_sort = l->sort;
}

static void update(Screen *s, float dt)
{
    Library *l = s->st;
    if (l->seen_version != g_app.library_version || l->built_filter != l->filter || l->built_sort != l->sort) build(l);
    float target = l->zone == Z_GRID ? (float)(l->focus / COLS) * (CELL_H + 80) : 0;
    l->scroll = ui_ease(l->scroll, target, NULL, dt, 14);
    l->focus_t = ui_ease(l->focus_t * 100, 100, NULL, dt, 16) / 100;
    ui_peek_step(&l->peek, l->zone == Z_GRID && l->rail_focus < 0 && l->focus < l->n ? &l->items[l->focus] : NULL, dt);
    char buf[512];
    for (int i = l->focus - COLS; i < l->focus + COLS * 3 && i < l->n; i++) if (i >= 0) image_prefetch(app_art_poster(&l->items[i], buf, sizeof buf));
}

static void draw(Screen *s)
{
    Library *l = s->st;
    const MetaPreview *fp = l->zone == Z_GRID && l->focus < l->n ? &l->items[l->focus] : NULL;
    char buf[512];
    const char *bg = fp ? app_art_background(fp, buf, sizeof buf) : NULL;
    ui_background(bg ? image_tint(bg) : 0);
    font_draw("Library", LX, 52, FONT_L | FONT_B, THEME_TEXT);
    char cnt[48];
    snprintf(cnt, sizeof cnt, "%d title%s", l->n, l->n == 1 ? "" : "s");
    font_draw(cnt, LX + font_measure("Library", FONT_L | FONT_B) + 24, 52 + font_height(FONT_L | FONT_B) - font_height(FONT_S) - 6, FONT_S, THEME_TEXT_MUTED);
    float x = LX, y = 134;
    for (int i = 0; i < F_COUNT; i++) {
        int foc = l->zone == Z_FILTERS && l->rail_focus < 0 && l->pill_focus == i;
        float w = ui_pill(FILTERS[i], 0, ICON_PLAY, x, y, 52, l->filter == i ? PILL_PRIMARY : PILL_GHOST, foc);
        if (l->filter == i && !foc) gfx_rrect(x + w / 2 - 10, y + 56, 20, 3, 1.5f, THEME_ACCENT2);
        x += w + 12;
    }
    {
        char sl[32]; snprintf(sl, sizeof sl, "Sort: %s", SORTS[l->sort]);
        float w = ui_pill_width(sl, 1, 52);
        ui_pill(sl, 1, ICON_SWAP, SNTV_UI_W - THEME_MARGIN - w, y, 52, PILL_SECONDARY, l->zone == Z_FILTERS && l->rail_focus < 0 && l->pill_focus == F_COUNT);
    }
    float gx = LX, gy = GRID_Y, row_h = CELL_H + 80;
    if (!l->n) {
        float cy = SNTV_UI_H / 2;
        ui_icon(ICON_LIBRARY, SNTV_UI_W / 2, cy - 60, 80, THEME_TEXT_MUTED);
        const char *t = !g_app.auth_key && !g_app.n_library ? "Sign in to see your library" : l->filter == F_CONTINUE ? "Nothing in progress" : "Your library is empty";
        const char *d = !g_app.auth_key && !g_app.n_library ? "Titles you add to the library on any device appear here." : "Open a title and press Add to Library.";
        font_draw_center(t, SNTV_UI_W / 2, cy + 10, FONT_L | FONT_B, THEME_TEXT);
        font_draw_center(d, SNTV_UI_W / 2, cy + 70, FONT_S, THEME_TEXT_DIM);
    }
    gfx_clip_push(gx - 30, gy - 20, SNTV_UI_W - gx, SNTV_UI_H - gy + 20);
    float peek_x = 0, peek_y = 0;
    for (int pass = 0; pass < 2; pass++)
        for (int i = 0; i < l->n; i++) {
            int r = i / COLS, c = i % COLS;
            float px = gx + c * (CELL_W + CELL_GAP), py = gy + r * row_h - l->scroll;
            if (py > SNTV_UI_H || py + row_h < gy - 20) continue;
            int foc = l->zone == Z_GRID && l->rail_focus < 0 && l->focus == i;
            if ((pass == 1) != foc) continue;
            char pb[512];
            float f = foc ? (g_app.fx_light ? 1 : l->focus_t) : 0;
            if (foc) { peek_x = px; peek_y = py; }
            ui_card(app_art_poster(&l->items[i], pb, sizeof pb), px, py, CELL_W, CELL_H, f, l->zone == Z_GRID && !foc ? 0.25f : 0, l->items[i].name, 0);
            const LibraryItem *li = l->src[i];
            if (li->duration && li->time_offset) {
                float sc = ui_focus_scale(f), w2 = CELL_W * sc, h2 = CELL_H * sc, x2 = px - (w2 - CELL_W) / 2, y2 = py - (h2 - CELL_H) / 2;
                ui_progress(x2 + 14, y2 + h2 - 14, w2 - 28, 5, (float)li->time_offset / (float)li->duration, THEME_ACCENT);
            }
            font_draw_fit(l->items[i].name, px, py + CELL_H + (foc ? 24 : 12), FONT_XS | FONT_B, foc ? THEME_TEXT : THEME_TEXT_DIM, CELL_W);
            if (foc && ui_hold_hint(px, py + CELL_H + 52, CELL_W)) {}
            else if (foc && li->video_id && strcmp(li->type, "series") == 0) {
                int se = 0, ep = 0; const char *c1 = strchr(li->video_id, ':'); char sub[32];
                if (c1 && sscanf(c1, ":%d:%d", &se, &ep) == 2) { snprintf(sub, sizeof sub, "S%d E%d", se, ep); font_draw(sub, px, py + CELL_H + 50, FONT_XS, THEME_TEXT_MUTED); }
            }
        }
    if (l->zone == Z_GRID && l->rail_focus < 0 && l->focus < l->n) {
        float f = g_app.fx_light ? 1 : l->focus_t, sc = ui_focus_scale(f), w2 = CELL_W * sc, h2 = CELL_H * sc;
        ui_peek_draw(&l->peek, &l->items[l->focus], peek_x - (w2 - CELL_W) / 2, peek_y - (h2 - CELL_H) / 2 - ui_focus_lift(f), w2, h2, gx, SNTV_UI_W - 40);
    }
    gfx_clip_pop();
    rail_draw(RAIL_LIBRARY, l->rail_focus);
}

static int key(Screen *s, KeyAction a)
{
    Library *l = s->st;
    app_redraw();
    if (a == KEY_SEARCH) { app_push(screen_search_create()); return 1; }
    if (a == KEY_LONG_OK && (l->rail_focus >= 0 || l->zone != Z_GRID)) return 0;
    if (l->rail_focus >= 0) {
        int r = rail_key(a, &l->rail_focus, RAIL_LIBRARY);
        if (r == 2) { l->rail_focus = -1; return 1; }
        if (r == 1) return 1;
        if (a == KEY_BACK) { app_pop(); return 1; }
        return 0;
    }
    switch (a) {
    case KEY_UP:
        if (l->zone == Z_GRID) { if (l->focus >= COLS) { l->focus -= COLS; l->focus_t = 0; } else { l->zone = Z_FILTERS; l->pill_focus = l->filter; } }
        return 1;
    case KEY_DOWN:
        if (l->zone == Z_FILTERS) { if (l->n) l->zone = Z_GRID; }
        else if (l->focus + COLS < l->n) { l->focus += COLS; l->focus_t = 0; }
        else return 0;
        return 1;
    case KEY_LEFT:
        if (l->zone == Z_FILTERS) { if (l->pill_focus > 0) l->pill_focus--; else l->rail_focus = RAIL_LIBRARY; }
        else if (l->focus % COLS) { l->focus--; l->focus_t = 0; } else l->rail_focus = RAIL_LIBRARY;
        return 1;
    case KEY_RIGHT:
        if (l->zone == Z_FILTERS) { if (l->pill_focus < F_COUNT) l->pill_focus++; }
        else if (l->focus % COLS < COLS - 1 && l->focus + 1 < l->n) { l->focus++; l->focus_t = 0; }
        return 1;
    case KEY_OK:
        if (l->zone == Z_FILTERS) { if (l->pill_focus == F_COUNT) l->sort = (l->sort + 1) % S_COUNT; else { l->filter = l->pill_focus; l->focus = 0; } }
        else if (l->focus < l->n) app_push(screen_detail_create(&l->items[l->focus]));
        return 1;
    case KEY_LONG_OK: case KEY_YELLOW: case KEY_MENU: case KEY_INFO: if (l->zone == Z_GRID && l->focus < l->n) { screen_options_for_title(&l->items[l->focus]); return 1; } return a != KEY_LONG_OK;
    case KEY_BACK:
        if (l->zone == Z_GRID) { l->zone = Z_FILTERS; l->pill_focus = l->filter; return 1; }
        app_pop();
        return 1;
    default: return 0;
    }
}

static void resume(Screen *s) { (void)s; app_redraw(); }

static void destroy(Screen *s)
{
    Library *l = s->st;
    meta_previews_free(l->items, l->n); free(l->src);
    free(l); free(s);
}

Screen *screen_library_create(void)
{
    Screen *s = calloc(1, sizeof *s);
    Library *l = calloc(1, sizeof *l);
    l->rail_focus = -1; l->focus_t = 1; l->seen_version = -1;
    l->zone = Z_GRID;
    s->name = "library"; s->st = l; s->long_ok = 1;
    s->update = update; s->draw = draw; s->key = key; s->destroy = destroy; s->resume = resume;
    build(l);
    if (!l->n) l->zone = Z_FILTERS;
    return s;
}
