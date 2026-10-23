/* Library: everything in the account's library as a poster grid, with filters
 * (all, movies, series, continue watching) and a sort (recent, name), and Your stats:
 * time watched, what was finished, the most watched titles and when you watch. */
#include "../newep.h"
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
#include <time.h>

enum { COLS = 7, CELL_W = 196, CELL_H = 294, CELL_GAP = 28 };
enum { F_ALL, F_MOVIES, F_SERIES, F_CONTINUE, F_STATS, F_COUNT };
enum { S_RECENT, S_NAME, S_COUNT };
enum { Z_FILTERS, Z_GRID };
#define LX RAIL_LX
#define GRID_Y 230.0f

static const char *const FILTERS[F_COUNT] = { "All", "Movies", "Series", "Continue watching", "Your stats" };
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

/* ---- Your stats ---- */

#define WEEKS 15
typedef struct {
    double hours, film_hours, series_hours;
    int started, finished, this_month;
    const LibraryItem *top[6]; int n_top;
    int days[WEEKS * 7];        /* titles last watched on each day, oldest first, the last is today */
    int busiest_wd, streak;     /* weekday with most (0 Monday), longest run of days in a row */
} Stats;

static void stats_build(Stats *st)
{
    memset(st, 0, sizeof *st);
    time_t now = time(NULL);
    struct tm today; localtime_r(&now, &today);
    today.tm_hour = 12; today.tm_min = today.tm_sec = 0;
    time_t noon = mktime(&today);
    int wd_count[7] = { 0 };
    for (int i = 0; i < g_app.n_library; i++) {
        const LibraryItem *li = &g_app.library[i];
        double h = li->overall_time_watched / 3600000.0;
        if (h <= 0 && !li->times_watched && !li->flagged_watched) continue;
        st->hours += h;
        if (li->type && strcmp(li->type, "series") == 0) st->series_hours += h; else st->film_hours += h;
        st->started++;
        if (li->times_watched > 0 || li->flagged_watched) st->finished++;
        if (li->last_watched_ms > 0) {
            time_t t = (time_t)(li->last_watched_ms / 1000);
            struct tm lt; localtime_r(&t, &lt);
            if (lt.tm_year == today.tm_year && lt.tm_mon == today.tm_mon) st->this_month++;
            lt.tm_hour = 12; lt.tm_min = lt.tm_sec = 0;
            long ago = (long)((noon - mktime(&lt) + 43200) / 86400);   /* rounded: a daylight-saving day is 23 or 25 hours */
            if (ago >= 0 && ago < WEEKS * 7) { st->days[WEEKS * 7 - 1 - ago]++; wd_count[(lt.tm_wday + 6) % 7]++; }
        }
        /* the most watched, by time */
        int k = st->n_top < 6 ? st->n_top++ : 5;
        if (k == 5 && st->top[5] && st->top[5]->overall_time_watched >= li->overall_time_watched) continue;
        st->top[k] = li;
        for (; k > 0 && st->top[k - 1]->overall_time_watched < st->top[k]->overall_time_watched; k--) { const LibraryItem *t = st->top[k]; st->top[k] = st->top[k - 1]; st->top[k - 1] = t; }
    }
    for (int d = 1; d < 7; d++) if (wd_count[d] > wd_count[st->busiest_wd]) st->busiest_wd = d;
    if (!wd_count[st->busiest_wd]) st->busiest_wd = -1;
    for (int i = 0, run = 0; i < WEEKS * 7; i++) { run = st->days[i] ? run + 1 : 0; if (run > st->streak) st->streak = run; }
}

static void stat_tile(float x, float y, float w, float h, const char *value, const char *label, const char *sub)
{
    ui_panel(x, y, w, h, R_PANEL, COLOR_ALPHA(THEME_PANEL, 0xC8));
    font_draw_tracked(label, x + 28, y + 24, FONT_XS | FONT_B, THEME_TEXT_MUTED, 1.2f);
    font_draw(value, x + 28, y + 60, FONT_XL | FONT_B, THEME_TEXT);
    if (sub) font_draw_fit(sub, x + 28, y + h - 44, FONT_XS, THEME_TEXT_DIM, w - 56);
}

static void draw_stats(void)
{
    Stats st; stats_build(&st);
    float x = LX, y = GRID_Y, W = SNTV_UI_W - THEME_MARGIN - LX, gap = 24, tw = (W - 3 * gap) / 4, th = 176;
    if (!st.started) {
        float cy = SNTV_UI_H / 2;
        ui_icon(ICON_CLOCK, SNTV_UI_W / 2, cy - 60, 80, THEME_TEXT_MUTED);
        font_draw_center("Nothing watched yet", SNTV_UI_W / 2, cy + 10, FONT_L | FONT_B, THEME_TEXT);
        font_draw_center("What you watch on any device signed in to your account adds up here.", SNTV_UI_W / 2, cy + 70, FONT_S, THEME_TEXT_DIM);
        return;
    }
    char v[32], sub[96];
    if (st.hours >= 10) snprintf(v, sizeof v, "%.0f h", st.hours); else snprintf(v, sizeof v, "%.1f h", st.hours);
    snprintf(sub, sizeof sub, "about %.0f films' worth", st.hours / 2 + 0.5);
    stat_tile(x, y, tw, th, v, "TIME WATCHED", sub);
    snprintf(v, sizeof v, "%d", st.started); snprintf(sub, sizeof sub, "%d this month", st.this_month);
    stat_tile(x + (tw + gap), y, tw, th, v, "TITLES STARTED", sub);
    snprintf(v, sizeof v, "%d", st.finished); snprintf(sub, sizeof sub, "%d%% of what you started", st.started ? 100 * st.finished / st.started : 0);
    stat_tile(x + 2 * (tw + gap), y, tw, th, v, "FINISHED", sub);
    {   /* films and series: the split as a bar */
        float fx = x + 3 * (tw + gap), tot = (float)(st.film_hours + st.series_hours);
        int fp = tot > 0 ? (int)(100 * st.film_hours / tot + 0.5) : 0;
        snprintf(v, sizeof v, "%d%% / %d%%", fp, 100 - fp);
        stat_tile(fx, y, tw, th, v, "FILMS / SERIES", NULL);
        float bw = tw - 56, by = y + th - 40;
        gfx_rrect(fx + 28, by, bw, 10, 5, COLOR_ALPHA(THEME_ACCENT2, 0x90));
        if (fp > 0) gfx_rrect(fx + 28, by, bw * fp / 100, 10, 5, THEME_ACCENT);
    }
    y += th + 52;

    /* the most watched */
    ui_row_header(0, "Most watched", NULL, 0, x, y, x + W, 0);
    float py = y + 56, pw = 150, ph = 225, pg = 26;
    for (int i = 0; i < st.n_top; i++) {
        const LibraryItem *li = st.top[i];
        float px = x + i * (pw + pg);
        MetaPreview m; memset(&m, 0, sizeof m);
        m.id = li->id; m.type = li->type; m.name = li->name; m.poster = li->poster;
        char pb[512];
        ui_card_capture(0);
        ui_card(app_art_poster(&m, pb, sizeof pb), px, py, pw, ph, 0, 0, li->name, 0);
        ui_card_capture(1);
        double h = li->overall_time_watched / 3600000.0;
        if (h >= 1) snprintf(v, sizeof v, "%.0f h", h); else snprintf(v, sizeof v, "%.0f min", h * 60);
        font_draw_fit(li->name, px, py + ph + 12, FONT_XS | FONT_B, THEME_TEXT, pw);
        font_draw(v, px, py + ph + 40, FONT_XS, THEME_TEXT_MUTED);
    }

    /* when: a square a day, the last WEEKS weeks, Monday at the top */
    float gx0 = x + 6 * (pw + pg) + 30, cell = 22, cg = 6;
    ui_row_header(1, "When you watch", NULL, 0, gx0, y, x + W, 0);
    float gy0 = py;
    time_t now = time(NULL); struct tm today; localtime_r(&now, &today);
    int today_wd = (today.tm_wday + 6) % 7, first_wd = (today_wd - (WEEKS * 7 - 1) % 7 + 7 * WEEKS) % 7;
    for (int i = 0; i < WEEKS * 7; i++) {
        int col = (i + first_wd) / 7, row = (i + first_wd) % 7;
        float cx = gx0 + col * (cell + cg), cy = gy0 + row * (cell + cg);
        int n = st.days[i];
        Color c = n == 0 ? COLOR_ALPHA(THEME_TEXT, 0x14) : n == 1 ? COLOR_ALPHA(THEME_ACCENT, 0x70) : n == 2 ? COLOR_ALPHA(THEME_ACCENT, 0xB0) : THEME_ACCENT;
        gfx_rrect(cx, cy, cell, cell, 5, c);
        if (i == WEEKS * 7 - 1) gfx_rrect_ring(cx, cy, cell, cell, 5, 2, THEME_TEXT);
    }
    static const char *const WD[7] = { "Mondays", "Tuesdays", "Wednesdays", "Thursdays", "Fridays", "Saturdays", "Sundays" };
    float ty = gy0 + 7 * (cell + cg) + 18;
    if (st.busiest_wd >= 0) { snprintf(sub, sizeof sub, "Most often on %s", WD[st.busiest_wd]); font_draw(sub, gx0, ty, FONT_S | FONT_B, THEME_TEXT); ty += 38; }
    snprintf(sub, sizeof sub, "Longest run: %d day%s in a row", st.streak, st.streak == 1 ? "" : "s");
    font_draw(sub, gx0, ty, FONT_XS, THEME_TEXT_DIM);
    font_draw_wrapped("A square is a day; brighter, more titles. Each title counts on the day you last watched it.", gx0, ty + 34, FONT_XS, THEME_TEXT_MUTED, x + W - gx0, 3);
}

static void build(Library *l)
{
    char keep[160] = "";   /* the focused title, found again after the re-sort */
    if (l->focus < l->n && l->items[l->focus].id) snprintf(keep, sizeof keep, "%s", l->items[l->focus].id);
    meta_previews_free(l->items, l->n); free(l->src);
    l->items = NULL; l->src = NULL; l->n = 0;
    const LibraryItem **pick = calloc((size_t)g_app.n_library + 1, sizeof *pick);
    int n = 0;
    for (int i = 0; i < g_app.n_library; i++) {
        const LibraryItem *li = &g_app.library[i];
        int cont = li->time_offset > 0 && li->duration > 0 && (!li->removed || li->temp);
        if (l->filter == F_STATS) continue;   /* no grid: the stats */
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
    if (keep[0] && l->built_filter == l->filter) for (int i = 0; i < n; i++) if (l->items[i].id && strcmp(l->items[i].id, keep) == 0) { l->focus = i; break; }
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

enum { HIT_PILL = 0x1000 };   /* touch: the filters, then the sort; a title is its index */

static void draw(Screen *s)
{
    Library *l = s->st;
    const MetaPreview *fp = l->zone == Z_GRID && l->focus < l->n ? &l->items[l->focus] : NULL;
    char buf[512];
    const char *bg = fp ? app_art_background(fp, buf, sizeof buf) : NULL;
    ui_background(bg ? image_tint(bg) : 0);
    font_draw("Library", LX, 52, FONT_L | FONT_B, THEME_TEXT);
    char cnt[48];
    if (l->filter == F_STATS) cnt[0] = 0;
    else snprintf(cnt, sizeof cnt, "%d title%s", l->n, l->n == 1 ? "" : "s");
    font_draw(cnt, LX + font_measure("Library", FONT_L | FONT_B) + 24, 52 + font_height(FONT_L | FONT_B) - font_height(FONT_S) - 6, FONT_S, THEME_TEXT_MUTED);
    float x = LX, y = 134;
    for (int i = 0; i < F_COUNT; i++) {
        int foc = l->zone == Z_FILTERS && l->rail_focus < 0 && l->pill_focus == i;
        float w = ui_pill(FILTERS[i], 0, ICON_PLAY, x, y, 52, l->filter == i ? PILL_PRIMARY : PILL_GHOST, foc);
        ui_hit(x, y, w, 52, HIT_PILL + i);
        if (l->filter == i && !foc) gfx_rrect(x + w / 2 - 10, y + 56, 20, 3, 1.5f, THEME_ACCENT2);
        x += w + 12;
    }
    {
        char sl[32]; snprintf(sl, sizeof sl, "Sort: %s", SORTS[l->sort]);
        float w = ui_pill_width(sl, 1, 52);
        ui_pill(sl, 1, ICON_SWAP, SNTV_UI_W - THEME_MARGIN - w, y, 52, PILL_SECONDARY, l->zone == Z_FILTERS && l->rail_focus < 0 && l->pill_focus == F_COUNT);
        ui_hit(SNTV_UI_W - THEME_MARGIN - w, y, w, 52, HIT_PILL + F_COUNT);
    }
    float gx = LX, gy = GRID_Y, row_h = CELL_H + 80;
    if (l->filter == F_STATS) { draw_stats(); rail_draw(RAIL_LIBRARY, l->rail_focus); return; }
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
    /* details open on the focused card: the others in its row slide aside to make room */
    float room = 0; int room_right = 1, room_row = -1;
    if (l->zone == Z_GRID && l->rail_focus < 0 && l->focus < l->n) {
        float sc = ui_focus_scale(g_app.fx_light ? 1.0f : l->focus_t), w2 = CELL_W * sc;
        float fx = gx + (l->focus % COLS) * (CELL_W + CELL_GAP) - (w2 - CELL_W) / 2;
        room = ui_peek_room(&l->peek, &l->items[l->focus], fx, w2, CELL_H * sc, gx, SNTV_UI_W - 40, &room_right);
        room_row = l->focus / COLS;
    }
    for (int pass = 0; pass < 2; pass++)
        for (int i = 0; i < l->n; i++) {
            int r = i / COLS, c = i % COLS;
            float px = gx + c * (CELL_W + CELL_GAP), py = gy + r * row_h - l->scroll;
            if (room > 0 && r == room_row && (room_right ? i > l->focus : i < l->focus)) px += room_right ? room : -room;
            if (py > SNTV_UI_H || py + row_h < gy - 20) continue;
            int foc = l->zone == Z_GRID && l->rail_focus < 0 && l->focus == i;
            if ((pass == 1) != foc) continue;
            char pb[512];
            float f = foc ? (g_app.fx_light ? 1 : l->focus_t) : 0;
            if (foc) { peek_x = px; peek_y = py; }
            ui_card(app_art_poster(&l->items[i], pb, sizeof pb), px, py, CELL_W, CELL_H, f, l->zone == Z_GRID && !foc ? 0.25f : 0, l->items[i].name, 0);
            ui_hit(px, py, CELL_W, CELL_H, i);
            const LibraryItem *li = l->src[i];
            const NewEp *ne = newep_get(li->id);
            if (ne) {
                float sc = ui_focus_scale(f), w2 = CELL_W * sc, x2 = px - (w2 - CELL_W) / 2, y2 = py - (CELL_H * sc - CELL_H) / 2 - ui_focus_lift(f);
                ui_new_episodes_dot(ne->count, x2 + w2 - 10, y2 + 10);
            }
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
        else if (l->focus / COLS < (l->n - 1) / COLS) { l->focus = l->n - 1; l->focus_t = 0; }   /* a short last row */
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
        l->rail_focus = RAIL_LIBRARY;   /* then the rail; Back there goes Home */
        return 1;
    default: return 0;
    }
}

/* A tapped filter, the sort, or a title: the focus goes there and OK follows. */
static int tap(Screen *s, int id)
{
    Library *l = s->st;
    l->rail_focus = -1;
    if (id >= HIT_PILL && id <= HIT_PILL + F_COUNT) { l->zone = Z_FILTERS; l->pill_focus = id - HIT_PILL; return 0; }
    if (id < 0 || id >= l->n) return 1;
    if (l->zone != Z_GRID || l->focus != id) { l->zone = Z_GRID; l->focus = id; l->focus_t = 0; }
    return 0;
}

static void resume(Screen *s) { Library *l = s->st; l->rail_focus = -1; app_redraw(); }

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
    s->update = update; s->draw = draw; s->key = key; s->tap = tap; s->destroy = destroy; s->resume = resume;
    build(l);
    if (!l->n) l->zone = Z_FILTERS;
    return s;
}
