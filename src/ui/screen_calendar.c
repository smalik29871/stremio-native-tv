/* Calendar: episodes of the series in the library that aired recently or air
 * soon, by day. Each series' details come from its meta addon (cached). */
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
#include <time.h>

#define LX RAIL_LX
#define LIST_Y 150.0f
#define ROW_H 118.0f
enum { DAYS_BACK = 14, DAYS_AHEAD = 30, MAX_SHOWS = 60 };

typedef struct {
    char show_id[96], show_name[128], show_type[16], poster[512];
    char video_id[128], title[160], thumb[512];
    int season, episode;
    long day;                 /* days since 1970-01-01, local */
} CalItem;

typedef struct Calendar Calendar;
typedef struct { Calendar *owner; unsigned gen; int show; char url[1024]; } Req;

struct Calendar {
    const LibraryItem *shows[MAX_SHOWS]; int n_shows;
    CalItem *items; int n, cap;
    int pending, asked;
    unsigned gen;
    int focus; float scroll;
    int rail_focus;
    long today;
};

/* days since epoch for a civil date (Howard Hinnant's algorithm) */
static long days_from_civil(int y, int m, int d)
{
    y -= m <= 2;
    long era = (y >= 0 ? y : y - 399) / 400;
    long yoe = y - era * 400;
    long doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

static long today_days(void)
{
    time_t t = time(NULL); struct tm tmv;
    if (!localtime_r(&t, &tmv)) return 0;
    return days_from_civil(tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday);
}

static int cmp_items(const void *a, const void *b)
{
    const CalItem *x = a, *y = b;
    if (x->day != y->day) return x->day < y->day ? -1 : 1;
    int c = strcasecmp(x->show_name, y->show_name);
    if (c) return c;
    if (x->season != y->season) return x->season - y->season;
    return x->episode - y->episode;
}

static void add_items(Calendar *c, const LibraryItem *li, const MetaItem *m)
{
    for (int i = 0; i < m->n_videos; i++) {
        const MetaVideo *v = &m->videos[i];
        int y, mo, d;
        if (!v->released || sscanf(v->released, "%d-%d-%d", &y, &mo, &d) != 3) continue;
        long day = days_from_civil(y, mo, d);
        if (day < c->today - DAYS_BACK || day > c->today + DAYS_AHEAD) continue;
        if (c->n == c->cap) { c->cap = c->cap ? c->cap * 2 : 64; c->items = realloc(c->items, (size_t)c->cap * sizeof(CalItem)); }
        CalItem *it = &c->items[c->n++];
        memset(it, 0, sizeof *it);
        snprintf(it->show_id, sizeof it->show_id, "%s", li->id);
        snprintf(it->show_name, sizeof it->show_name, "%s", li->name);
        snprintf(it->show_type, sizeof it->show_type, "%s", li->type);
        snprintf(it->poster, sizeof it->poster, "%s", m->p.poster ? m->p.poster : li->poster ? li->poster : "");
        snprintf(it->video_id, sizeof it->video_id, "%s", v->id ? v->id : "");
        snprintf(it->title, sizeof it->title, "%s", v->title ? v->title : "");
        snprintf(it->thumb, sizeof it->thumb, "%s", v->thumbnail ? v->thumbnail : "");
        it->season = v->season; it->episode = v->episode; it->day = day;
    }
}

static void on_meta(HttpResponse *r, void *user)
{
    Req *q = user;
    Calendar *c = q->owner;
    if (q->gen != c->gen) { free(q); return; }
    c->pending--;
    MetaItem m;
    if (r->status == 200 && addon_parse_meta(r->body, &m) == 0) {
        add_items(c, c->shows[q->show], &m);
        meta_item_free(&m);
        qsort(c->items, (size_t)c->n, sizeof(CalItem), cmp_items);
        app_cache_put(q->url, r->body, r->len);   /* next time the calendar opens at once */
    }
    free(q);
    app_redraw();
}

static void ask_all(Calendar *c)
{
    c->asked = 1;
    c->today = today_days();
    c->n_shows = 0;
    for (int i = 0; i < g_app.n_library && c->n_shows < MAX_SHOWS; i++) {
        const LibraryItem *li = &g_app.library[i];
        if (li->removed || li->temp || strcmp(li->type, "series") != 0) continue;
        c->shows[c->n_shows++] = li;
    }
    for (int sidx = 0; sidx < c->n_shows; sidx++) {
        const LibraryItem *li = c->shows[sidx];
        for (int a = 0; a < g_app.n_addons; a++) {
            if (!addon_supports(&g_app.addons[a], "meta", li->type, li->id)) continue;
            char url[1024];
            addon_url_meta(&g_app.addons[a], li->type, li->id, url, sizeof url);
            char *cached = app_cache_get(url, 12 * 3600);
            if (cached) {
                MetaItem m;
                if (addon_parse_meta(cached, &m) == 0) { add_items(c, li, &m); meta_item_free(&m); }
                free(cached);
            } else {
                Req *q = malloc(sizeof *q);
                q->owner = c; q->gen = c->gen; q->show = sidx;
                snprintf(q->url, sizeof q->url, "%s", url);
                c->pending++;
                http_request_async(url, NULL, on_meta, q);
            }
            break;
        }
    }
    if (c->n > 1) qsort(c->items, (size_t)c->n, sizeof(CalItem), cmp_items);
    /* start on today (or the first future day) */
    for (int i = 0; i < c->n; i++) if (c->items[i].day >= c->today) { c->focus = i; break; }
}

static void day_label(long day, long today, char *out, size_t n)
{
    static const char *WD[] = { "Thu", "Fri", "Sat", "Sun", "Mon", "Tue", "Wed" };   /* day 0 was a Thursday */
    static const char *MO[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
    /* civil from days (inverse of days_from_civil) */
    long z = day + 719468, era = (z >= 0 ? z : z - 146096) / 146097, doe = z - era * 146097;
    long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365, doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    long mp = (5 * doy + 2) / 153, d = doy - (153 * mp + 2) / 5 + 1, m = mp + (mp < 10 ? 3 : -9);
    const char *wd = WD[((day % 7) + 7) % 7];
    if (day == today) snprintf(out, n, "Today  \xC2\xB7  %s %ld %s", wd, d, MO[m - 1]);
    else if (day == today + 1) snprintf(out, n, "Tomorrow  \xC2\xB7  %s %ld %s", wd, d, MO[m - 1]);
    else if (day == today - 1) snprintf(out, n, "Yesterday  \xC2\xB7  %s %ld %s", wd, d, MO[m - 1]);
    else snprintf(out, n, "%s %ld %s", wd, d, MO[m - 1]);
}

static void update(Screen *s, float dt)
{
    Calendar *c = s->st;
    if (!c->asked && (g_app.n_addons || !g_app.addons_loading)) ask_all(c);
    /* rows plus a header per new day before the focus */
    float y = 0; long last = -1000000;
    for (int i = 0; i < c->n && i < c->focus; i++) { if (c->items[i].day != last) { y += 56; last = c->items[i].day; } y += ROW_H; }
    if (c->focus < c->n && c->items[c->focus].day != last) y += 56;
    float target = y > 300 ? y - 300 : 0;
    c->scroll = ui_ease(c->scroll, target, NULL, dt, 14);
    if (c->pending) app_redraw();
}

static void draw(Screen *s)
{
    Calendar *c = s->st;
    const CalItem *fi = c->focus < c->n ? &c->items[c->focus] : NULL;
    ui_background(fi && fi->poster[0] ? image_tint(fi->poster) : 0);
    font_draw("Calendar", LX, 52, FONT_L | FONT_B, THEME_TEXT);
    char sub[96];
    snprintf(sub, sizeof sub, "%d episode%s from %d series in your library%s", c->n, c->n == 1 ? "" : "s", c->n_shows, c->pending ? "  \xC2\xB7  updating" : "");
    font_draw(sub, LX + font_measure("Calendar", FONT_L | FONT_B) + 24, 52 + font_height(FONT_L | FONT_B) - font_height(FONT_S) - 6, FONT_S, THEME_TEXT_MUTED);
    if (c->pending) ui_spinner(SNTV_UI_W - THEME_MARGIN - 30, 76, 14);
    if (!c->n && !c->pending) {
        float cy = SNTV_UI_H / 2;
        ui_icon(ICON_CALENDAR, SNTV_UI_W / 2, cy - 60, 80, THEME_TEXT_MUTED);
        font_draw_center(c->n_shows ? "Nothing airing in the next weeks" : "No series in your library yet", SNTV_UI_W / 2, cy + 10, FONT_L | FONT_B, THEME_TEXT);
        font_draw_center(c->n_shows ? "Episodes from the last two weeks and the next month show here." : "Add a series to the library and its new episodes will show here by day.", SNTV_UI_W / 2, cy + 70, FONT_S, THEME_TEXT_DIM);
    }
    float w = SNTV_UI_W - LX - THEME_MARGIN;
    gfx_clip_push(LX - 20, LIST_Y - 10, w + 40, SNTV_UI_H - LIST_Y + 10);
    float y = LIST_Y - c->scroll; long last = -1000000;
    for (int i = 0; i < c->n; i++) {
        const CalItem *it = &c->items[i];
        if (it->day != last) {
            char dl[64]; day_label(it->day, c->today, dl, sizeof dl);
            if (y + 56 > LIST_Y - 10 && y < SNTV_UI_H) font_draw(dl, LX, y + 14, FONT_S | FONT_B, it->day == c->today ? THEME_ACCENT2 : THEME_TEXT_DIM);
            y += 56; last = it->day;
        }
        if (y + ROW_H > LIST_Y - 10 && y < SNTV_UI_H) {
            int foc = c->rail_focus < 0 && c->focus == i;
            if (foc) gfx_rrect(LX - 16, y, w + 32, ROW_H - 12, R_PANEL, THEME_FOCUS);
            else gfx_rrect(LX - 16, y, w + 32, ROW_H - 12, R_PANEL, COLOR_ALPHA(THEME_TEXT, 0x0C));
            Color fg = foc ? THEME_ON_FOCUS : THEME_TEXT, dim = foc ? COLOR_ALPHA(THEME_ON_FOCUS, 0xB0) : THEME_TEXT_MUTED;
            const char *art = it->thumb[0] ? it->thumb : it->poster;
            ui_draw_thumb(art, LX, y + 10, 152, 86, 0, COLOR_ALPHA(THEME_TEXT, 0x14));
            char ep[48];
            if (it->season >= 0 && it->episode >= 0) snprintf(ep, sizeof ep, "S%d E%d", it->season, it->episode); else ep[0] = 0;
            font_draw_fit(it->show_name, LX + 176, y + 18, FONT_S | FONT_B, fg, w - 420);
            char line[220];
            snprintf(line, sizeof line, "%s%s%s", ep, ep[0] && it->title[0] ? "  \xC2\xB7  " : "", it->title);
            font_draw_fit(line, LX + 176, y + 56, FONT_S, dim, w - 420);
            if (it->day > c->today) ui_chip("Upcoming", LX + w - 150, y + 34, foc ? COLOR_ALPHA(THEME_BG, 0x18) : COLOR_ALPHA(THEME_TEXT, 0x16), fg);
        }
        y += ROW_H;
    }
    gfx_clip_pop();
    rail_draw(RAIL_CALENDAR, c->rail_focus);
}

static int key(Screen *s, KeyAction a)
{
    Calendar *c = s->st;
    app_redraw();
    if (a == KEY_SEARCH) { app_push(screen_search_create()); return 1; }
    if (c->rail_focus >= 0) {
        int r = rail_key(a, &c->rail_focus, RAIL_CALENDAR);
        if (r == 2) { c->rail_focus = -1; return 1; }
        if (r == 1) return 1;
        if (a == KEY_BACK) { app_pop(); return 1; }
        return 0;
    }
    switch (a) {
    case KEY_UP: if (c->focus > 0) c->focus--; return 1;
    case KEY_DOWN: if (c->focus + 1 < c->n) { c->focus++; return 1; } return 0;
    case KEY_LEFT: c->rail_focus = RAIL_CALENDAR; return 1;
    case KEY_OK:
        if (c->focus < c->n) {
            const CalItem *it = &c->items[c->focus];
            MetaPreview p; memset(&p, 0, sizeof p);
            p.id = (char *)it->show_id; p.type = (char *)it->show_type; p.name = (char *)it->show_name; p.poster = it->poster[0] ? (char *)it->poster : NULL;
            app_push(screen_detail_create(&p));   /* the detail copies what it needs */
        }
        return 1;
    case KEY_BACK: c->rail_focus = RAIL_CALENDAR; return 1;   /* the rail; Back there goes Home */
    default: return 0;
    }
}

static void resume(Screen *s) { Calendar *c = s->st; c->rail_focus = -1; app_redraw(); }

static void destroy(Screen *s)
{
    Calendar *c = s->st;
    c->gen++;
    free(c->items); free(c); free(s);
}

Screen *screen_calendar_create(void)
{
    Screen *s = calloc(1, sizeof *s);
    Calendar *c = calloc(1, sizeof *c);
    c->rail_focus = -1; c->gen = 1;
    s->name = "calendar"; s->st = c;
    s->update = update; s->draw = draw; s->key = key; s->destroy = destroy; s->resume = resume;
    return s;
}
