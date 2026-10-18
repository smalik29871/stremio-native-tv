#include "../app.h"
#include "../http.h"
#include "../log.h"
#include "../store.h"
#include "font.h"
#include "theme.h"
#include "widgets.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ROW_H 142.0f
#define LIST_X 620.0f
#define LIST_Y 200.0f
enum { VISIBLE = 7 };

typedef struct Streams Streams;
typedef struct { Streams *owner; int addon; unsigned generation; } Pending;
static unsigned g_generation;
static Streams *g_live;

struct Streams {
    MetaPreview meta;
    char *video_id, *video_title, *autoplay_group;
    Stream *items; int n;
    int pending, n_reqs;
    int focus;
    float scroll, scroll_target;
    int autoplayed;
    float started;
    int from_prefetch;        /* answers still arriving belong to the prefetch */
    int check_autoplay;
    int auto_rec_done;        /* the recommended source was started by itself */
};
#define REC_H 150.0f          /* the recommendation card above the list */

/* Streams fetched ahead of time for the next episode (the player asks when the
 * credits start). A Streams screen opened for that episode adopts them and the
 * requests still in flight, so Up Next starts without the usual wait. */
static struct {
    char *video_id;
    Stream *items; int n;
    int pending;
    unsigned generation;
    float started;
} g_pre;
#define PREFETCH_FRESH_S 600.0f

static int playable(const Stream *s)
{
    char reason[8];
    char *u = app_stream_playable_url(s, reason, sizeof reason);
    if (!u) return 0;
    free(u);
    return 1;
}

/* The user's addon order (as in the Stremio apps), with unplayable sources last
 * and, while chaining episodes, the previous binge group on top so Up Next stays
 * on the same release. Answers arrive in any order, so this is applied on every
 * arrival. */
static long rank(const Streams *st, const Stream *s)
{
    long r = g_app.quality_mode == QUAL_ADDON_ORDER ? s->addon_order * 10L : (200000L - s->score) * 10L + s->addon_order;
    if (!playable(s)) r += 100000000L;
    if (st->autoplay_group && s->binge_group && strcmp(st->autoplay_group, s->binge_group) == 0) r -= 50000000L;
    return r;
}

/* Best-scoring playable source, or -1 when nothing looks safe for this TV. */
static int recommended(const Streams *st)
{
    int best = -1;
    for (int i = 0; i < st->n; i++)
        if (playable(&st->items[i]) && (best < 0 || st->items[i].score > st->items[best].score)) best = i;
    return best >= 0 && st->items[best].score > -500 ? best : -1;
}

static void sort_items(Streams *st)
{
    QualityPrefs qp = { g_app.quality_mode, g_app.res_cap };
    double runtime = quality_runtime_minutes(st->meta.runtime);
    for (int i = 0; i < st->n; i++) st->items[i].score = quality_score(&st->items[i].tags, &g_app.tv, &qp, runtime);
    for (int i = 1; i < st->n; i++)
        for (int j = i; j > 0 && rank(st, &st->items[j]) < rank(st, &st->items[j - 1]); j--) {
            Stream t = st->items[j]; st->items[j] = st->items[j - 1]; st->items[j - 1] = t;
        }
}

static void play_from(Streams *st, int index);

/* Parse one addon's answer into `items`; returns how many were added. */
static int absorb(Stream **items, int *count, HttpResponse *r, int addon)
{
    const char *addon_name = addon < g_app.n_addons ? g_app.addons[addon].manifest.name : "addon";
    Stream *got; int n;
    if (r->status != 200 || addon_parse_streams(r->body, &got, &n) != 0) {
        LOGW("streams: %s failed (%ld %s)", addon_name, r->status, r->error);
        return 0;
    }
    *items = realloc(*items, sizeof(Stream) * (size_t)(*count + n));
    for (int i = 0; i < n; i++) {
        got[i].addon_name = strdup(addon_name);
        got[i].addon_order = addon;
        quality_parse(got[i].name, got[i].title, got[i].description, got[i].filename, got[i].video_size, &got[i].tags);
        (*items)[(*count)++] = got[i];
    }
    free(got);
    return n;
}

/* Autoplay: as soon as a same-group stream shows up, or when everyone answered. */
static void autoplay_check(Streams *st)
{
    if (!st->autoplay_group || st->autoplayed || !st->n) return;
    int match = st->items[0].binge_group && strcmp(st->items[0].binge_group, st->autoplay_group) == 0;
    if ((match || st->pending == 0) && playable(&st->items[0])) { st->autoplayed = 1; play_from(st, 0); }
}

static void on_streams(HttpResponse *r, void *user)
{
    Pending *p = user;
    Streams *st = p->owner;
    if (st != g_live || p->generation != g_generation) { free(p); return; }
    st->pending--;
    if (absorb(&st->items, &st->n, r, p->addon)) { sort_items(st); app_redraw(); }
    free(p);
    autoplay_check(st);
}

static void query_addons(const char *type, const char *video_id, Streams *owner, unsigned generation, HttpCallback cb, int *pending, int *n_reqs)
{
    for (int i = 0; i < g_app.n_addons; i++) {
        if (!addon_supports(&g_app.addons[i], "stream", type, video_id)) continue;
        char url[1024];
        addon_url_stream(&g_app.addons[i], type, video_id, url, sizeof url);
        Pending *p = malloc(sizeof *p);
        p->owner = owner; p->addon = i; p->generation = generation;
        (*pending)++;
        if (n_reqs) (*n_reqs)++;
        http_request_async(url, NULL, cb, p);
    }
}

static void query(Streams *st) { query_addons(st->meta.type, st->video_id, st, g_generation, on_streams, &st->pending, &st->n_reqs); }

static void prefetch_clear(void)
{
    g_pre.generation++;   /* answers still in flight are dropped */
    streams_free(g_pre.items, g_pre.n);
    free(g_pre.video_id);
    memset(&g_pre, 0, sizeof g_pre);
    g_pre.generation = g_generation + 1000;
}

static void on_prefetch(HttpResponse *r, void *user)
{
    Pending *p = user;
    if (p->generation != g_pre.generation) { free(p); return; }
    g_pre.pending--;
    Streams *st = g_live && g_live->from_prefetch && g_live->video_id && g_pre.video_id && strcmp(g_live->video_id, g_pre.video_id) == 0 ? g_live : NULL;
    if (st) {   /* the screen for this episode is already open: feed it directly */
        st->pending--;
        if (absorb(&st->items, &st->n, r, p->addon)) { sort_items(st); app_redraw(); }
        free(p);
        autoplay_check(st);
        return;
    }
    absorb(&g_pre.items, &g_pre.n, r, p->addon);
    free(p);
    if (g_pre.pending == 0) LOGI("streams: prefetched %d sources for %s", g_pre.n, g_pre.video_id);
}

void streams_prefetch(const MetaPreview *meta, const char *video_id)
{
    if (!video_id || !meta->type) return;
    if (g_pre.video_id && strcmp(g_pre.video_id, video_id) == 0 && g_app.time - g_pre.started < PREFETCH_FRESH_S) return;
    prefetch_clear();
    g_pre.video_id = strdup(video_id);
    g_pre.started = g_app.time;
    query_addons(meta->type, video_id, NULL, g_pre.generation, on_prefetch, &g_pre.pending, NULL);
    LOGI("streams: prefetching %s from %d addon%s", video_id, g_pre.pending, g_pre.pending == 1 ? "" : "s");
    if (!g_pre.pending) prefetch_clear();
}

/* Take over a prefetch for this episode. 1 when there was one. */
static int adopt_prefetch(Streams *st)
{
    if (!g_pre.video_id || strcmp(g_pre.video_id, st->video_id) != 0) return 0;
    if (g_app.time - g_pre.started > PREFETCH_FRESH_S) { prefetch_clear(); return 0; }
    st->items = g_pre.items; st->n = g_pre.n;
    g_pre.items = NULL; g_pre.n = 0;
    st->pending = g_pre.pending;
    st->n_reqs = g_pre.pending + st->n;
    st->from_prefetch = 1;
    st->check_autoplay = 1;
    sort_items(st);
    LOGI("streams: using %d prefetched sources for %s (%d answer%s still coming)", st->n, st->video_id, st->pending, st->pending == 1 ? "" : "s");
    if (!st->pending) prefetch_clear();
    return 1;
}

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

static void update(Screen *s, float dt)
{
    Streams *st = s->st;
    if (st->check_autoplay) { st->check_autoplay = 0; autoplay_check(st); if (app_top() != s) return; }
    if (g_app.auto_recommended && !st->autoplay_group && !st->auto_rec_done && st->n && (st->pending == 0 || g_app.time - st->started > 6)) {
        int r = recommended(st);
        if (r >= 0) {
            st->auto_rec_done = 1;
            app_toast("Playing the recommended source. Back returns to the list.");
            play_from(st, r);
            if (app_top() != s) return;
        }
    }
    int first = st->focus - (VISIBLE - 3);
    st->scroll_target = (first < 0 ? 0 : first) * ROW_H;
    st->scroll = ui_ease(st->scroll, st->scroll_target, NULL, dt, 14);
    if (st->pending) app_redraw();
}

/* ---- quality badges parsed from the stream's text ---- */

static int has_ci(const char *hay, const char *needle)
{
    if (!hay) return 0;
    size_t n = strlen(needle);
    for (const char *p = hay; *p; p++) {
        size_t i = 0;
        while (i < n && p[i] && tolower((unsigned char)p[i]) == tolower((unsigned char)needle[i])) i++;
        if (i == n) {
            /* word-ish boundaries for short tokens like "dv" */
            int left_ok = p == hay || !isalnum((unsigned char)p[-1]);
            int right_ok = !isalnum((unsigned char)p[n]);
            if (n > 3 || (left_ok && right_ok)) return 1;
        }
    }
    return 0;
}

static void find_size(const char *t, char *out, size_t n)
{
    out[0] = 0;
    if (!t) return;
    for (const char *p = t; *p; p++) {
        if (!isdigit((unsigned char)*p) || (p > t && (isdigit((unsigned char)p[-1]) || p[-1] == '.'))) continue;
        char *end;
        double v = strtod(p, &end);
        while (*end == ' ') end++;
        if ((end[0] == 'G' || end[0] == 'g' || end[0] == 'M' || end[0] == 'm') && (end[1] == 'B' || end[1] == 'b')) {
            snprintf(out, n, "%.*f %cB", v < 10 ? 1 : 0, v, toupper((unsigned char)end[0]));
            return;
        }
    }
}

static float draw_badges(const Stream *sm, float right_x, float y)
{
    char text[1200];
    snprintf(text, sizeof text, "%s %s %s %s", sm->name ? sm->name : "", sm->title ? sm->title : "",
             sm->description ? sm->description : "", sm->filename ? sm->filename : "");
    const char *res = has_ci(text, "2160p") || has_ci(text, "4k") || has_ci(text, "uhd") ? "4K" :
                      has_ci(text, "1440p") ? "1440p" : has_ci(text, "1080p") ? "1080p" : has_ci(text, "720p") ? "720p" :
                      has_ci(text, "480p") ? "SD" : NULL;
    const char *hdr = has_ci(text, "dolby vision") || has_ci(text, "dovi") || sm->tags.hdr == 2 ? "DOLBY VISION" :
                      has_ci(text, "hdr10+") ? "HDR10+" : has_ci(text, "hdr") ? "HDR" : NULL;
    const char *codec = has_ci(text, "av1") ? "AV1" : has_ci(text, "hevc") || has_ci(text, "x265") || has_ci(text, "h265") || has_ci(text, "h.265") ? "HEVC" : NULL;
    const char *audio = has_ci(text, "atmos") ? "ATMOS" : has_ci(text, "truehd") ? "TRUEHD" : has_ci(text, "dts") ? "DTS" :
                        has_ci(text, "7.1") ? "7.1" : has_ci(text, "5.1") ? "5.1" : NULL;
    const char *cached = has_ci(text, "[rd+]") || has_ci(text, "[tb+]") || has_ci(text, "[ad+]") || has_ci(text, "[pm+]") || has_ci(text, "â¡") ? "CACHED" : NULL;
    char size[24];
    find_size(text, size, sizeof size);
    if (!size[0] && sm->video_size) snprintf(size, sizeof size, "%.1f GB", sm->video_size / 1073741824.0);

    struct { const char *t; Color bg, fg; } b[8]; int n = 0;
    if (res) { b[n].t = res; b[n].bg = strcmp(res, "4K") == 0 ? THEME_GOLD : RGBA(0xFF, 0xFF, 0xFF, 0xE8); b[n].fg = THEME_ON_LIGHT; n++; }
    if (hdr) { b[n].t = hdr; b[n].bg = RGBA(0x8B, 0x5C, 0xF6, 0xFF); b[n].fg = THEME_TEXT; n++; }
    if (codec) { b[n].t = codec; b[n].bg = RGBA(0xFF, 0xFF, 0xFF, 0x22); b[n].fg = THEME_TEXT; n++; }
    if (audio) { b[n].t = audio; b[n].bg = RGBA(0xFF, 0xFF, 0xFF, 0x22); b[n].fg = THEME_TEXT; n++; }
    if (cached) { b[n].t = cached; b[n].bg = THEME_OK; b[n].fg = THEME_ON_LIGHT; n++; }
    if (size[0]) { b[n].t = size; b[n].bg = RGBA(0xFF, 0xFF, 0xFF, 0x12); b[n].fg = THEME_TEXT_DIM; n++; }
    float x = right_x;
    for (int i = n - 1; i >= 0; i--) {
        float w = font_measure(b[i].t, FONT_XS | FONT_B) + 24;
        x -= w;
        ui_chip(b[i].t, x, y, b[i].bg, b[i].fg);
        x -= 8;
    }
    return x;
}

static void draw(Screen *s)
{
    Streams *st = s->st;
    char buf[512];
    const char *bg = app_art_background(&st->meta, buf, sizeof buf);
    ui_background(image_tint(bg));
    int iw, ih;
    unsigned bgt = bg ? image_get_ex(bg, IMAGE_MIPMAP, &iw, &ih, NULL) : 0;
    if (bgt && !g_app.fx_light) ui_backdrop_blur(bgt, iw, ih, 0.85f);

    /* left column: poster + titles */
    float lx = THEME_MARGIN, ly = 120;
    char pbuf[512];
    ui_card(app_art_poster(&st->meta, pbuf, sizeof pbuf), lx, ly, 360, 540, 0, 0, st->meta.name, 0);
    font_draw_wrapped(st->meta.name, lx, ly + 570, FONT_L | FONT_B, THEME_TEXT, 420, 2);
    if (st->video_title) font_draw_wrapped(st->video_title, lx, ly + 570 + font_draw_wrapped(st->meta.name, 0, 0, FONT_L | FONT_B, 0, 420, 2) * font_height(FONT_L | FONT_B) + 8,
                                           FONT_S, THEME_TEXT_DIM, 420, 2);

    /* header */
    float x = LIST_X, w = SNTV_UI_W - THEME_MARGIN - LIST_X;
    font_draw("Choose a source", x, 104, FONT_L | FONT_B, THEME_TEXT);
    char msg[128];
    if (st->autoplay_group && !st->autoplayed) snprintf(msg, sizeof msg, "Finding the next episode on the same release");
    else if (st->pending) snprintf(msg, sizeof msg, "%d found  Â·  %d addon%s still searching", st->n, st->pending, st->pending == 1 ? "" : "s");
    else if (!st->n) snprintf(msg, sizeof msg, "%s", st->n_reqs ? "No sources found for this title" : "No stream addons installed. Add one in Settings.");
    else snprintf(msg, sizeof msg, "%d source%s", st->n, st->n == 1 ? "" : "s");
    font_draw(msg, x, 156, FONT_S, THEME_TEXT_MUTED);
    if (st->pending || (st->autoplay_group && !st->autoplayed)) ui_spinner(x + w - 30, 140, 20);

    if (!st->n && st->pending) {
        for (int i = 0; i < 5; i++) ui_skeleton(x, LIST_Y + i * ROW_H, w, ROW_H - 14, 20);
        return;
    }

    int rec = recommended(st);
    QualityPrefs qp = { g_app.quality_mode, g_app.res_cap };
    double runtime = quality_runtime_minutes(st->meta.runtime);
    float list_y = LIST_Y;
    if (rec >= 0) {   /* the recommendation card */
        const Stream *sm = &st->items[rec];
        int foc = st->focus == -1;
        float h = REC_H - 16;
        if (foc && !g_app.fx_light) gfx_shadow(x, LIST_Y + 8, w, h, 20, 26, RGBA(0, 0, 0, 0x90));
        gfx_rrect_grad_h(x, LIST_Y, w, h, 20, foc ? RGBA(0xFF, 0xFF, 0xFF, 0x2E) : COLOR_ALPHA(THEME_ACCENT, 0x38), foc ? RGBA(0xFF, 0xFF, 0xFF, 0x2E) : COLOR_ALPHA(THEME_ACCENT2, 0x30));
        if (foc) gfx_rrect_ring(x, LIST_Y, w, h, 20, 3, THEME_TEXT);
        gfx_circle(x + 44, LIST_Y + h / 2, 26, foc ? THEME_TEXT : THEME_ACCENT2);
        ui_icon(ICON_STAR, x + 44, LIST_Y + h / 2, 26, foc ? THEME_ON_LIGHT : THEME_TEXT);
        static const char *HEAD[QUAL_MODE_COUNT] = { "Recommended for this TV", "Recommended for this TV", "Highest quality", "Balanced pick", "Smallest download" };
        font_draw(HEAD[g_app.quality_mode], x + 90, LIST_Y + 18, FONT_XS | FONT_B, THEME_ACCENT2);
        char l1[200], summary[200];
        first_line(sm->name, l1, sizeof l1);
        char head[300];
        snprintf(head, sizeof head, "%s%s%s", sm->addon_name, l1[0] ? "  \xC2\xB7  " : "", l1);
        font_draw_fit(head, x + 90, LIST_Y + 46, FONT_M | FONT_B, THEME_TEXT, w - 320);
        quality_summary(&sm->tags, runtime, summary, sizeof summary);
        font_draw_fit(summary[0] ? summary : "Playable on this TV", x + 90, LIST_Y + 88, FONT_XS, THEME_TEXT_DIM, w - 320);
        ui_pill("Play", 1, ICON_PLAY, x + w - 170, LIST_Y + (h - 56) / 2, 56, foc ? PILL_PRIMARY : PILL_SECONDARY, foc);
        list_y += REC_H;
    }

    gfx_clip_push(0, list_y - 20, SNTV_UI_W, SNTV_UI_H - list_y + 20);
    float ry = list_y - st->scroll;
    for (int i = 0; i < st->n; i++, ry += ROW_H) {
        if (ry + ROW_H < LIST_Y - 20 || ry > SNTV_UI_H) continue;
        const Stream *sm = &st->items[i];
        int foc = st->focus == i;
        int ok = playable(sm);
        float h = ROW_H - 14;
        if (foc && !g_app.fx_light) gfx_shadow(x, ry + 8, w, h, 20, 26, RGBA(0, 0, 0, 0x90));
        gfx_rrect(x, ry, w, h, 20, foc ? RGBA(0xFF, 0xFF, 0xFF, 0x2A) : RGBA(0xFF, 0xFF, 0xFF, 0x0E));
        if (foc) gfx_rrect_ring(x, ry, w, h, 20, 3, THEME_TEXT);
        char line1[200], line2[600], sub[300], rest[300];
        first_line(sm->name, line1, sizeof line1);
        rest_lines(sm->name, sub, sizeof sub);
        const char *desc = sm->description ? sm->description : sm->title;
        first_line(desc, line2, sizeof line2);
        rest_lines(desc, rest, sizeof rest);
        if (rest[0]) { size_t used = strlen(line2); snprintf(line2 + used, sizeof line2 - used, "  %s", rest); }
        /* addon badge */
        float ax = x + 24, ay = ry + 18;
        float aw = font_measure(sm->addon_name, FONT_XS | FONT_B) + 24;
        if (aw > 220) aw = 220;
        gfx_rrect_grad_h(ax, ay, aw, 32, 16, COLOR_ALPHA(THEME_ACCENT, 0x60), COLOR_ALPHA(THEME_ACCENT2, 0x60));
        font_draw_fit(sm->addon_name, ax + 12, ay + (32 - font_height(FONT_XS | FONT_B)) / 2, FONT_XS | FONT_B, THEME_TEXT, aw - 24);
        if (sub[0]) font_draw_fit(sub, ax + aw + 12, ay + 4, FONT_XS | FONT_B, THEME_TEXT_DIM, 160);
        float bx = draw_badges(sm, x + w - 24, ry + 18);
        font_draw_fit(line1[0] ? line1 : "Stream", ax, ry + 60, FONT_M | FONT_B, ok ? THEME_TEXT : THEME_TEXT_MUTED, w - 48);
        font_draw_fit(line2, ax, ry + 98, FONT_XS, ok ? THEME_TEXT_DIM : THEME_TEXT_MUTED, w - 48);
        const char *flag = sm->info_hash ? (g_app.streaming_server ? NULL : "Needs streaming server") : sm->yt_id ? "YouTube" : sm->external_url ? "Opens externally" : NULL;
        if (st->autoplay_group && sm->binge_group && strcmp(sm->binge_group, st->autoplay_group) == 0) flag = "Same release";
        if (flag) {
            Color fc = !ok ? THEME_DANGER : THEME_OK;
            float fw = font_measure(flag, FONT_XS | FONT_B) + 24;
            bx -= fw + 8;
            ui_chip(flag, bx, ry + 18, COLOR_ALPHA(fc, 0x30), fc);
        }
        const char *warn[3];
        int nw = ok ? quality_warnings(&sm->tags, &g_app.tv, &qp, warn, 3) : 0;
        if (nw) {
            float fw = font_measure(warn[0], FONT_XS | FONT_B) + 24;
            bx -= fw + 8;
            ui_chip(warn[0], bx, ry + 18, COLOR_ALPHA(THEME_GOLD, 0x30), THEME_GOLD);
        } else if (i == rec) {
            float fw = font_measure("Recommended", FONT_XS | FONT_B) + 24;
            bx -= fw + 8;
            ui_chip("Recommended", bx, ry + 18, COLOR_ALPHA(THEME_ACCENT2, 0x40), THEME_TEXT);
        }
    }
    gfx_clip_pop();
    if (st->n > VISIBLE) {
        char pos[48];
        snprintf(pos, sizeof pos, "%d / %d", st->focus + 1, st->n);
        font_draw(pos, x + w - font_measure(pos, FONT_S | FONT_B), 156, FONT_S | FONT_B, THEME_TEXT_MUTED);
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

/* Build the fallback list: the chosen stream first, then other playable ones
 * (same binge group preferred), capped so a bad addon can't stall for ages. */
static void play_from(Streams *st, int index)
{
    if (app_pip_claim(st->video_id)) return;
    char reason[160];
    char *url = app_stream_playable_url(&st->items[index], reason, sizeof reason);
    if (!url) { app_toast("%s", reason); return; }
    PlayCandidate *c = calloc((size_t)st->n + 1, sizeof *c);
    int n = 0;
    const char *group = st->items[index].binge_group;
    for (int pass = 0; pass < 3 && n < 6; pass++)
        for (int i = 0; i < st->n && n < 6; i++) {
            if (pass == 0 && i != index) continue;
            if (pass > 0 && i == index) continue;
            if (pass == 1 && !(group && st->items[i].binge_group && strcmp(group, st->items[i].binge_group) == 0)) continue;
            if (pass == 2 && group && st->items[i].binge_group && strcmp(group, st->items[i].binge_group) == 0) continue;
            char why[8];
            char *u = i == index ? strdup(url) : app_stream_playable_url(&st->items[i], why, sizeof why);
            if (!u) continue;
            char label[200], l1[120];
            first_line(st->items[i].name, l1, sizeof l1);
            snprintf(label, sizeof label, "%s%s%s", st->items[i].addon_name, l1[0] ? " - " : "", l1);
            c[n].url = u;
            c[n].label = strdup(label);
            c[n].binge_group = st->items[i].binge_group ? strdup(st->items[i].binge_group) : NULL;
            c[n].filename = st->items[i].filename ? strdup(st->items[i].filename) : NULL;
            c[n].video_size = st->items[i].video_size;
            c[n].needs = quality_needs(&st->items[i].tags);
            n++;
        }
    LibraryItem *li = app_library_find(st->meta.id);
    double start = (li && li->video_id && strcmp(li->video_id, st->video_id) == 0) ? li->time_offset / 1000.0 : 0;
    remember_resume(st, url);
    free(url);
    char title[300];
    snprintf(title, sizeof title, "%s%s%s", st->meta.name, st->video_title ? "  -  " : "", st->video_title ? st->video_title : "");
    char next_id[128] = "", next_title[200] = "";
    int has_next = app_next_video(st->meta.id, st->video_id, next_id, sizeof next_id, next_title, sizeof next_title);
    app_push(screen_player_create(&st->meta, st->video_id, title, c, n, 0, start, has_next ? next_id : NULL, has_next ? next_title : NULL));
}

static int key(Screen *s, KeyAction a)
{
    Streams *st = s->st;
    switch (a) {
    case KEY_UP:
        if (st->focus > 0) st->focus--;
        else if (st->focus == 0 && recommended(st) >= 0) st->focus = -1;
        app_redraw();
        return 1;
    case KEY_DOWN: if (st->focus < st->n - 1) { st->focus++; app_redraw(); return 1; } return 0;
    case KEY_OK:
        if (st->focus == -1) { int r = recommended(st); if (r >= 0) play_from(st, r); return 1; }
        if (st->focus < st->n) play_from(st, st->focus);
        return 1;
    default: return 0;
    }
}

static void resume(Screen *s)
{
    Streams *st = s->st;
    /* Coming back from an auto-played episode: the user pressed Back, don't re-trigger. */
    st->autoplayed = 1;
    st->auto_rec_done = 1;
    app_redraw();
}

static void destroy(Screen *s)
{
    Streams *st = s->st;
    if (g_live == st) { g_live = NULL; g_generation++; }
    if (st->from_prefetch && st->pending) prefetch_clear();   /* nobody left to receive the rest */
    streams_free(st->items, st->n);
    meta_preview_free(&st->meta);
    free(st->video_id); free(st->video_title); free(st->autoplay_group);
    free(st);
    free(s);
}

Screen *screen_streams_create_auto(const MetaPreview *meta, const char *video_id, const char *video_title, const char *autoplay_group)
{
    Screen *s = calloc(1, sizeof *s);
    Streams *st = calloc(1, sizeof *st);
    meta_preview_copy(&st->meta, meta);
    st->video_id = strdup(video_id);
    st->video_title = video_title ? strdup(video_title) : NULL;
    st->autoplay_group = autoplay_group ? strdup(autoplay_group) : NULL;
    if (!autoplay_group) st->autoplayed = 1;
    st->started = g_app.time;
    s->name = "streams"; s->st = st;
    s->update = update; s->draw = draw; s->key = key; s->destroy = destroy; s->resume = resume;
    g_live = st;
    g_generation++;
    if (!adopt_prefetch(st)) query(st);
    return s;
}

Screen *screen_streams_create(const MetaPreview *meta, const char *video_id, const char *video_title)
{
    return screen_streams_create_auto(meta, video_id, video_title, NULL);
}
