#include "../app.h"
#include "../http.h"
#include "../log.h"
#include "../store.h"
#include "font.h"
#include "theme.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { ROW_H = 104, VISIBLE = 7 };

typedef struct Streams Streams;
typedef struct { Streams *owner; int addon; unsigned generation; } Pending;
static unsigned g_generation;        /* bumped when a streams screen dies */
static Streams *g_live;

struct Streams {
    MetaPreview meta;
    char *video_id, *video_title;
    Stream *items; int n;
    int pending, queried;
    unsigned *reqs; int n_reqs;
    int focus;
};

static void on_streams(HttpResponse *r, void *user)
{
    Pending *p = user;
    Streams *st = p->owner;
    if (st != g_live || p->generation != g_generation) { free(p); return; }
    st->pending--;
    Stream *got; int n;
    const char *addon_name = p->addon < g_app.n_addons ? g_app.addons[p->addon].manifest.name : "addon";
    if (r->status == 200 && addon_parse_streams(r->body, &got, &n) == 0) {
        st->items = realloc(st->items, sizeof(Stream) * (size_t)(st->n + n));
        for (int i = 0; i < n; i++) {
            got[i].addon_name = strdup(addon_name);
            st->items[st->n++] = got[i];
        }
        free(got);
    } else {
        LOGW("streams: %s failed (%ld %s)", addon_name, r->status, r->error);
    }
    free(p);
}

static void query(Streams *st)
{
    st->queried = 1;
    st->reqs = calloc((size_t)g_app.n_addons + 1, sizeof(unsigned));
    for (int i = 0; i < g_app.n_addons; i++) {
        if (!addon_supports(&g_app.addons[i], "stream", st->meta.type, st->video_id)) continue;
        char url[1024];
        addon_url_stream(&g_app.addons[i], st->meta.type, st->video_id, url, sizeof url);
        Pending *p = malloc(sizeof *p);
        p->owner = st; p->addon = i; p->generation = g_generation;
        st->pending++;
        st->reqs[st->n_reqs++] = http_request_async(url, NULL, on_streams, p);
    }
}

/* Stream names/titles carry newlines and emoji; show the first line of each. */
static void first_line(const char *s, char *out, size_t n)
{
    if (!s) { out[0] = 0; return; }
    size_t i = 0;
    for (; s[i] && s[i] != '\n' && i + 1 < n; i++) out[i] = s[i];
    out[i] = 0;
}

static void rest_lines(const char *s, char *out, size_t n)
{
    out[0] = 0;
    if (!s) return;
    const char *nl = strchr(s, '\n');
    if (!nl) return;
    size_t o = 0;
    for (const char *p = nl + 1; *p && o + 1 < n; p++) {
        if (*p == '\n') { if (o + 3 < n) { memcpy(out + o, " | ", 3); o += 3; } }
        else out[o++] = *p;
    }
    out[o] = 0;
}

static void draw(Screen *s)
{
    Streams *st = s->st;
    gfx_rect(0, 0, SNTV_UI_W, SNTV_UI_H, THEME_BG);
    int iw, ih;
    unsigned bg = st->meta.background ? image_get(st->meta.background, &iw, &ih) : 0;
    if (bg) gfx_tex(bg, 0, 0, SNTV_UI_W, SNTV_UI_H, RGBA(255, 255, 255, 40));
    ui_draw_header(st->meta.name, st->video_title);

    float x = THEME_MARGIN, y = 150, w = SNTV_UI_W - 2 * THEME_MARGIN;
    if (st->pending) {
        ui_draw_spinner(x + 30, y + 30);
        char msg[96];
        snprintf(msg, sizeof msg, "Searching %d addon%s...", st->pending, st->pending == 1 ? "" : "s");
        font_draw(msg, x + 80, y + 14, FONT_S, THEME_TEXT_DIM);
    } else if (!st->n) {
        font_draw(st->n_reqs ? "No streams found for this title." : "No installed addon provides streams. Add one in Settings.", x, y + 14, FONT_M, THEME_TEXT_DIM);
    }
    y += 70;

    int first = st->focus - (VISIBLE - 1);
    if (first < 0) first = 0;
    for (int i = first; i < st->n && i < first + VISIBLE; i++) {
        const Stream *sm = &st->items[i];
        int foc = st->focus == i;
        gfx_rect(x, y, w, ROW_H - 10, foc ? THEME_SURFACE_HI : THEME_SURFACE);
        if (foc) gfx_rect(x, y, 6, ROW_H - 10, THEME_ACCENT);
        char line1[200], line2[300], sub[300];
        first_line(sm->name, line1, sizeof line1);
        rest_lines(sm->name, sub, sizeof sub);
        const char *desc = sm->description ? sm->description : sm->title;
        first_line(desc, line2, sizeof line2);
        char rest[300];
        rest_lines(desc, rest, sizeof rest);
        if (rest[0]) { strncat(line2, "  ", sizeof line2 - strlen(line2) - 1); strncat(line2, rest, sizeof line2 - strlen(line2) - 1); }
        font_draw_fit(sm->addon_name, x + 24, y + 12, FONT_S, THEME_TEXT_MUTED, 260);
        if (sub[0]) font_draw_fit(sub, x + 24, y + 48, FONT_S, THEME_TEXT_DIM, 260);
        font_draw_fit(line1[0] ? line1 : "Stream", x + 300, y + 12, FONT_M, THEME_TEXT, w - 520);
        font_draw_fit(line2, x + 300, y + 52, FONT_S, THEME_TEXT_DIM, w - 520);
        const char *badge = sm->info_hash ? (g_app.streaming_server ? "torrent" : "needs server") : sm->yt_id ? "youtube" : sm->external_url ? "external" : NULL;
        if (badge) {
            float bw = font_measure(badge, FONT_S) + 24;
            gfx_rect(x + w - bw - 20, y + 30, bw, 36, sm->info_hash && !g_app.streaming_server ? THEME_DANGER : THEME_SURFACE_HI);
            font_draw(badge, x + w - bw - 8, y + 36, FONT_S, THEME_TEXT);
        }
        y += ROW_H;
    }
    if (st->n > VISIBLE) {
        char pos[48];
        snprintf(pos, sizeof pos, "%d / %d", st->focus + 1, st->n);
        font_draw(pos, x + w - font_measure(pos, FONT_S), y, FONT_S, THEME_TEXT_MUTED);
    }
}

static void remember_resume(Streams *st, const char *url)
{
    char key[160];
    snprintf(key, sizeof key, "resume:%s", st->meta.id);
    struct cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "url", url);
    cJSON_AddStringToObject(o, "video_id", st->video_id);
    store_set_json(key, o);
    store_save();
}

static int key(Screen *s, KeyAction a)
{
    Streams *st = s->st;
    switch (a) {
    case KEY_UP: if (st->focus > 0) st->focus--; return 1;
    case KEY_DOWN: if (st->focus < st->n - 1) st->focus++; return 1;
    case KEY_OK: {
        if (st->focus >= st->n) return 1;
        char reason[160];
        char *url = app_stream_playable_url(&st->items[st->focus], reason, sizeof reason);
        if (!url) { app_toast("%s", reason); return 1; }
        LibraryItem *li = app_library_find(st->meta.id);
        double start = (li && li->video_id && strcmp(li->video_id, st->video_id) == 0) ? li->time_offset / 1000.0 : 0;
        remember_resume(st, url);
        char title[300];
        snprintf(title, sizeof title, "%s%s%s", st->meta.name, st->video_title ? "  -  " : "", st->video_title ? st->video_title : "");
        app_push(screen_player_create(&st->meta, st->video_id, title, url, start));
        free(url);
        return 1;
    }
    default: return 0;
    }
}

static void destroy(Screen *s)
{
    Streams *st = s->st;
    /* Replies still in flight are dropped (and their Pending freed) by the generation check. */
    if (g_live == st) { g_live = NULL; g_generation++; }
    free(st->reqs);
    streams_free(st->items, st->n);
    meta_preview_free(&st->meta);
    free(st->video_id); free(st->video_title);
    free(st);
    free(s);
}

Screen *screen_streams_create(const MetaPreview *meta, const char *video_id, const char *video_title)
{
    Screen *s = calloc(1, sizeof *s);
    Streams *st = calloc(1, sizeof *st);
    meta_preview_copy(&st->meta, meta);
    st->video_id = strdup(video_id);
    st->video_title = video_title ? strdup(video_title) : NULL;
    s->name = "streams"; s->st = st;
    s->draw = draw; s->key = key; s->destroy = destroy;
    g_live = st;
    g_generation++;
    query(st);
    return s;
}
