/* Screensaver: backdrops from the library and the Home catalogs drift slowly
 * (Ken Burns) with a big clock, after the remote has been quiet for a while.
 * Everything on screen shifts a few pixels every minute so OLED panels never
 * hold one static image. Any key closes it and is swallowed. */
#include "../app.h"
#include "../image.h"
#include "../log.h"
#include "../video/video.h"
#include "../store.h"
#include "../youtube.h"
#include "../breaks.h"
#include "../memwatch.h"
#include "../http.h"
#include "../../vendor/cJSON.h"
#include "../torbox.h"
#include "../newep.h"
#include "ambient_scenes.h"
#include "clockface.h"
#include "../depth.h"
#include "font.h"
#include "gfx.h"
#include "theme.h"
#include "widgets.h"
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define SLOT_S 14.0f      /* one backdrop */
#define FADE_S 1.6f       /* crossfade into the next */
#define ZOOM 0.10f        /* how much a backdrop grows over its slot */
#define WAIT_S 5.0f       /* give up on an image that has not arrived */
#define DEPTH_SLOT_S 22.0f   /* Depth: one backdrop, pushed into slowly */

typedef struct {
    AmbientItem *items; int n;
    int cur, prev;
    float t;              /* seconds into the current slot */
    float fade;           /* 0..1 while the previous image fades out */
    float shown_at;       /* g_app.time the current image appeared */
    int cur_ready;
    float wall_y;         /* Poster wall: how far the wall has drifted up */
    float lx, ly, lvx, lvy;   /* Floating logo: position and velocity */
    /* Fireplace: heat per cell, spread up and cooled each step (the classic demo fire),
     * drawn through a palette into a small texture the GPU stretches */
    unsigned char *heat; uint8_t *px; unsigned fire_tex; float fire_acc, fire_t; unsigned seed;
    /* a real fireplace video (Settings > Interface), played full screen and looped */
    int fv_state;         /* 0 none, 1 asking YouTube, 2 playing, 3 gave up (the drawn fire), 4 ready under another screen */
    float fv_kick;        /* when it was last (re)started */
    int fv_restarts;      /* found stopped and started again; a few and it gives way to the drawn fire */
    int fv_web; unsigned fv_web_fg;   /* handed to Cuelight YouTube (an LG): back on screen, the screensaver is over */
    char fv_urls[YT_PLAY_URLS][4608]; int fv_n, fv_i;
    int depth_logged;     /* Depth: the item last logged (+1) */
    int depth_chosen;     /* Depth: the first backdrop was weighed against the list of maps */
    float drawn_at;       /* the last frame: it moves slowly, thirty a second is plenty (a TV left on for hours) */
    float born;           /* g_app.time the screensaver came up */
    int dim; float dim_at;   /* dimmed, and when (the dark comes over a few seconds) */
    int off;              /* the picture is off: black with a faint clock, until a key */
    int gal_state;        /* Art gallery: 0 not asked, 1 asking the museum, 2 works on the wall, 3 none came */
} Ambient;

static Ambient *g_amb_live;   /* the screensaver a YouTube answer is for */
static int g_style, g_face;   /* the screensaver in use: the chosen one, or what Shuffle picked */

static float env_sec(const char *name, float dflt) { const char *v = getenv(name); return v && *v ? (float)atof(v) : dflt; }   /* test hooks */

/* "Maghrib in 42 min", once breaks are on and the tradition has prayers; worked out once a minute. */
static const char *prayer_line_now(void)
{
    static char line[64]; static int at = -1;
    if (!g_app.breaks_on) return NULL;
    int now = breaks_minute_now();
    if (at != now) { at = now; breaks_next_prayer(line, sizeof line); }
    return line[0] ? line : NULL;
}

static void prayer_line(float x, float y, int align)   /* 0 left, 1 centre, 2 right */
{
    const char *np = prayer_line_now();
    if (!np) return;
    float w = font_measure(np, FONT_S);
    font_draw_shadow(np, align == 1 ? x - w / 2 : align == 2 ? x - w : x, y, FONT_S, RGBA(0xFF, 0xE0, 0xA0, 0xE0), RGBA(0, 0, 0, 0xA0));
}

/* Dimming and the picture going off, so a TV left on all night is spared. */
static void stage_tick(Ambient *a)
{
    float up = g_app.time - a->born;
    float dim_s = env_sec("SNTV_AMBIENT_DIM_S", g_app.ambient_dim_min * 60.0f), off_s = env_sec("SNTV_AMBIENT_OFF_S", g_app.ambient_off_min * 60.0f);
    if (!a->dim && dim_s > 0 && up >= dim_s) { a->dim = 1; a->dim_at = g_app.time; LOGI("ambient: dimmed after %.0f s", up); }
    if (!a->off && off_s > 0 && up >= off_s) {
        a->off = 1;
        if (a->fv_state == 2) { video_stop(); a->fv_state = 3; }
        video_hold_screensaver(0);   /* the TV may now run its own saver or sleep */
        LOGI("ambient: picture off after %.0f s; a key brings it back", up);
    }
}

static void fv_play(Ambient *a)
{
    if (a->fv_i >= a->fv_n) { a->fv_state = 3; LOGI("ambient: the fireplace video would not play; the drawn fire instead"); return; }
    LOGI("ambient: fireplace video %d/%d", a->fv_i + 1, a->fv_n);
    video_load(a->fv_urls[a->fv_i], "Fireplace", 0);
    video_play();
    a->fv_state = 2;
    a->fv_kick = g_app.time;
}

static void on_fv(const char *answer, const char *error, void *user)
{
    Ambient *a = user;
    if (a != g_amb_live) return;
    char id[16] = "";
    youtube_video_id(store_get_str("fireplaceVideo", ""), id);
    a->fv_n = answer ? youtube_player_urls(answer, id, a->fv_urls, YT_PLAY_URLS) : 0;
    if (!a->fv_n && video_youtube_embed_url(id, a->fv_urls[0], sizeof a->fv_urls[0])) { LOGW("ambient: YouTube refused the fireplace video (%s); its own player plays it", error ? error : "?"); a->fv_n = 1; }
    if (!a->fv_n && video_youtube_web(id, 0, "loop", app_ytweb_flags())) {
        a->fv_state = 3; a->fv_web = 1; a->fv_web_fg = g_app.foregrounds;
        LOGI("ambient: YouTube refused the fireplace video (%s); Cuelight YouTube plays it", error ? error : "?");
        return;
    }
    if (!a->fv_n) {
        a->fv_state = 3; LOGW("ambient: fireplace video unavailable: %s", error ? error : "?");
        store_set_str("fireplaceRefused", store_get_str("fireplaceVideo", "")); store_save();   /* Settings says so on the row */
        return;
    }
    if (*store_get_str("fireplaceRefused", "")) { store_set_str("fireplaceRefused", NULL); store_save(); }
    a->fv_i = 0;
    Screen *top = app_top();
    if (top && top->st != a) { a->fv_state = 4; return; }   /* a break is on top: its recording keeps the video plane */
    fv_play(a);
}

static void fv_start(Ambient *a)
{
    const char *url = store_get_str("fireplaceVideo", "");
    VideoInfo vi; video_info(&vi);
    if (!url || !*url || (vi.state != VIDEO_IDLE && vi.state != VIDEO_ENDED)) return;   /* a paused film keeps its picture */
    char id[16], tb[700];
    if (torbox_resolve(url, tb, sizeof tb)) {   /* your own video on TorBox: its permanent link */
        LOGI("ambient: fireplace video from TorBox");
        snprintf(a->fv_urls[0], sizeof a->fv_urls[0], "%s", tb);
        a->fv_n = 1; a->fv_i = 0;
        fv_play(a);
    } else if (torbox_is_link(url)) {
        LOGW("ambient: the fireplace video is on TorBox, but no TorBox key is set");
    } else if (youtube_video_id(url, id)) {
        a->fv_state = 1;
        youtube_set_quality(g_app.res_cap, quality_cap_ok(&g_app.tv, TVCAP_AV1), quality_cap_ok(&g_app.tv, TVCAP_4K));
        youtube_stream(url, on_fv, a);
    } else {
        snprintf(a->fv_urls[0], sizeof a->fv_urls[0], "%s", url);
        a->fv_n = 1; a->fv_i = 0;
        fv_play(a);
    }
}

enum { FIRE_W = 240, FIRE_H = 96, FIRE_MAX = 36 };

static unsigned fire_rand(Ambient *a) { a->seed = a->seed * 1103515245u + 12345u; return a->seed >> 16; }

/* black, ember red, orange, yellow, near white */
static void fire_colour(int h, uint8_t *o)
{
    float t = (float)h / FIRE_MAX;
    float r = t < 0.5f ? t * 2 : 1, g = t < 0.35f ? 0 : t < 0.85f ? (t - 0.35f) * 1.6f : 0.8f + (t - 0.85f) * 1.3f, b = t < 0.8f ? 0 : (t - 0.8f) * 3.5f;
    o[0] = (uint8_t)(255 * (r > 1 ? 1 : r)); o[1] = (uint8_t)(255 * (g > 1 ? 1 : g)); o[2] = (uint8_t)(255 * (b > 1 ? 1 : b));
    o[3] = (uint8_t)(h <= 1 ? 0 : t < 0.25f ? 255 * t * 4 : 255);
}

static void fire_step(Ambient *a)
{
    unsigned char *h = a->heat;
    a->fire_t += 1.0f / 24;
    float t = a->fire_t;
    /* the embers: hottest in the middle, fading to the sides, with slow swells so the
     * flames rise and fall in tongues instead of a flat wall */
    for (int x = 0; x < FIRE_W; x++) {
        float u = (float)x / FIRE_W;
        float edge = u < 0.18f ? u / 0.18f : u > 0.82f ? (1 - u) / 0.18f : 1;
        float swell = 0.62f + 0.22f * sinf(u * 17 + t * 1.3f) + 0.16f * sinf(u * 41 - t * 2.1f);
        float v = edge * swell * FIRE_MAX + (float)(fire_rand(a) % 5) - 2;
        h[(FIRE_H - 1) * FIRE_W + x] = (unsigned char)(v < 0 ? 0 : v > FIRE_MAX ? FIRE_MAX : v);
    }
    /* each cell rises into the one above, drifting a cell sideways and cooling a little */
    for (int y = 1; y < FIRE_H; y++)
        for (int x = 0; x < FIRE_W; x++) {
            int src = y * FIRE_W + x;
            unsigned r = fire_rand(a) & 3;
            int dst = src - (int)r + 1 - FIRE_W;
            if (dst < 0 || dst >= FIRE_W * FIRE_H) continue;
            int v = h[src] - (int)(r & 1);
            h[dst] = (unsigned char)(v < 0 ? 0 : v);
        }
    for (int i = 0; i < FIRE_W * FIRE_H; i++) fire_colour(h[i], a->px + i * 4);
}

static void draw_fire(Ambient *a, float sx, float sy, const char *clock, struct tm *tmv)
{
    /* the hearth: a warm glow on the back wall, the flames, then the logs in front */
    gfx_gradient_v(0, SNTV_UI_H * 0.35f, SNTV_UI_W, SNTV_UI_H * 0.65f, RGBA(0, 0, 0, 0), RGBA(0x5A, 0x1C, 0x04, 0xB0));
    if (a->fire_tex) gfx_rtex_uv(a->fire_tex, SNTV_UI_W * 0.12f, SNTV_UI_H * 0.28f, SNTV_UI_W * 0.76f, SNTV_UI_H * 0.62f, 0, 0, 0, 1, 1, RGBA(0xFF, 0xFF, 0xFF, 0xFF), 0);
    float ly = SNTV_UI_H * 0.86f;
    gfx_rrect(SNTV_UI_W * 0.20f, ly, SNTV_UI_W * 0.34f, 46, 23, RGBA(0x3A, 0x22, 0x14, 0xFF));
    gfx_rrect(SNTV_UI_W * 0.46f, ly + 10, SNTV_UI_W * 0.34f, 46, 23, RGBA(0x2E, 0x1B, 0x10, 0xFF));
    gfx_rrect(SNTV_UI_W * 0.32f, ly - 30, SNTV_UI_W * 0.36f, 40, 20, RGBA(0x45, 0x29, 0x18, 0xFF));
    gfx_rect(0, SNTV_UI_H * 0.93f, SNTV_UI_W, SNTV_UI_H * 0.07f, RGBA(0x12, 0x0C, 0x09, 0xFF));   /* the hearth floor */
    char date[64]; strftime(date, sizeof date, "%A, %B %-d", tmv);
    float cx = SNTV_UI_W / 2 + sx * 6, cy = 70 + sy * 4;
    font_draw_center(clock, cx, cy, FONT_HUGE | FONT_B, RGBA(0xF4, 0xE6, 0xD8, 0xF0));
    font_draw_center(date, cx, cy + font_height(FONT_HUGE | FONT_B) + 6, FONT_M, RGBA(0xC8, 0xA8, 0x90, 0xD0));
}

#define LOGO_S 150.0f

/* ---- Art gallery: public-domain works from the Art Institute of Chicago's open API, hung
 * in a frame on a dark wall with the artist's name, the way The Frame does it ---- */

static void on_gallery(HttpResponse *r, void *user)
{
    Ambient *a = user;
    if (a != g_amb_live) return;
    cJSON *root = r->status == 200 && r->body ? cJSON_Parse(r->body) : NULL;
    const cJSON *data = cJSON_GetObjectItemCaseSensitive(root, "data"), *w;
    const char *iiif = getenv("SNTV_ART_IIIF");
    int width = image_memory_low() ? 843 : 1686, n = cJSON_IsArray(data) ? cJSON_GetArraySize(data) : 0;
    AmbientItem *items = n ? calloc((size_t)n, sizeof *items) : NULL;
    int got = 0;
    cJSON_ArrayForEach(w, data) {
        const cJSON *img = cJSON_GetObjectItemCaseSensitive(w, "image_id"), *title = cJSON_GetObjectItemCaseSensitive(w, "title");
        const cJSON *artist = cJSON_GetObjectItemCaseSensitive(w, "artist_display"), *date = cJSON_GetObjectItemCaseSensitive(w, "date_display");
        if (!cJSON_IsString(img) || !*img->valuestring) continue;
        AmbientItem *it = &items[got++];
        snprintf(it->background, sizeof it->background, "%s/%s/full/%d,/0/default.jpg", iiif && *iiif ? iiif : "https://www.artic.edu/iiif/2", img->valuestring, width);
        snprintf(it->name, sizeof it->name, "%s", cJSON_IsString(title) ? title->valuestring : "Untitled");
        char who[96] = "";
        if (cJSON_IsString(artist)) { snprintf(who, sizeof who, "%s", artist->valuestring); char *nl = strchr(who, '\n'); if (nl) *nl = 0; }   /* the first line: the name */
        snprintf(it->caption, sizeof it->caption, "%s%s%s", who, *who && cJSON_IsString(date) && *date->valuestring ? ", " : "", cJSON_IsString(date) ? date->valuestring : "");
    }
    cJSON_Delete(root);
    if (got) {
        free(a->items);
        a->items = items; a->n = got; a->cur = 0; a->prev = 0; a->cur_ready = 0; a->fade = 0; a->t = 0; a->shown_at = g_app.time;
        a->gal_state = 2;
        LOGI("ambient: a gallery of %d works from the Art Institute of Chicago", got);
    } else {
        free(items);
        a->gal_state = 3;
        LOGW("ambient: the museum sent no works (HTTP %ld); your artwork hangs instead", r->status);
    }
    app_redraw();
}

static void gallery_ask(Ambient *a)
{
    const char *base = getenv("SNTV_ART_API");
    char url[400];
    unsigned page = 1 + ((unsigned)time(NULL) / 97) % 60;   /* a different room each day or so; relevance order keeps the paintings first */
    snprintf(url, sizeof url, "%s/api/v1/artworks/search?q=painting&query[term][is_public_domain]=true&fields=id,title,artist_display,date_display,image_id&limit=40&page=%u",
             base && *base ? base : "https://api.artic.edu", page);
    a->gal_state = 1;
    http_request_async(url, NULL, on_gallery, a);
}

static unsigned art_get(const char *url, int *w, int *h);

static void draw_gallery(Ambient *a, float sx, float sy)
{
    float W = SNTV_UI_W, H = SNTV_UI_H;
    gfx_gradient_v(0, 0, W, H, RGBA(0x26, 0x22, 0x1E, 0xFF), RGBA(0x0C, 0x0A, 0x09, 0xFF));
    gfx_shadow(W * 0.2f, -H * 0.3f, W * 0.6f, H * 0.7f, H * 0.3f, H * 0.3f, RGBA(0x60, 0x50, 0x40, 0x50));   /* a spotlight from above */
    if (!a->n) return;
    const AmbientItem *it = &a->items[a->cur];
    int iw, ih;
    unsigned tex = art_get(it->background, &iw, &ih);
    if (!tex) { font_draw_center(a->gal_state == 1 ? "Choosing from the museum\xE2\x80\xA6" : "Hanging the next work\xE2\x80\xA6", W / 2, H / 2, FONT_S, RGBA(0x80, 0x78, 0x70, 0xC0)); return; }
    float alpha = a->fade > 0 ? 1 - a->fade : 1;
    float maxw = W * (SNTV_PHONE ? 0.82f : 0.56f), maxh = H * (SNTV_PHONE ? 0.5f : 0.56f), ia = (float)iw / (ih ? ih : 1);
    float pw = maxw, ph = pw / ia;
    if (ph > maxh) { ph = maxh; pw = ph * ia; }
    float matte = ph * 0.055f, frame = ph * 0.018f;
    float px = W / 2 - pw / 2 + sx, py = H * (SNTV_PHONE ? 0.42f : 0.47f) - ph / 2 + sy;
    float fx = px - matte - frame, fy = py - matte - frame, fw = pw + 2 * (matte + frame), fh = ph + 2 * (matte + frame);
    int al = (int)(255 * alpha);
    gfx_shadow(fx, fy + 24, fw, fh, 2, 60, RGBA(0, 0, 0, (int)(0xB0 * alpha)));
    gfx_rect(fx, fy, fw, fh, RGBA(0x2A, 0x1E, 0x16, al));                                   /* the frame */
    gfx_rect_outline(fx + frame * 0.45f, fy + frame * 0.45f, fw - frame * 0.9f, fh - frame * 0.9f, frame * 0.12f + 0.5f, RGBA(0xC8, 0xA8, 0x60, (int)(0x90 * alpha)));   /* a gilt line */
    gfx_rect(px - matte, py - matte, pw + 2 * matte, ph + 2 * matte, RGBA(0xF2, 0xEE, 0xE6, al));   /* the matte */
    gfx_rect(px - 2, py - 2, pw + 4, ph + 4, RGBA(0x9A, 0x92, 0x88, al));                      /* its bevel */
    gfx_tex(tex, px, py, pw, ph, RGBA(255, 255, 255, al));
    float ty = fy + fh + 34;
    font_draw_center(it->name, W / 2 + sx, ty, FONT_M | FONT_B, RGBA(0xF0, 0xE8, 0xDC, al));
    if (it->caption[0]) font_draw_center(it->caption, W / 2 + sx, ty + font_height(FONT_M | FONT_B) + 6, FONT_S, RGBA(0xA8, 0xA0, 0x94, al));
    font_draw(a->gal_state == 2 ? "Art Institute of Chicago" : "From your library", THEME_MARGIN + sx, H - 110 + sy, FONT_XS | FONT_B, THEME_TEXT_DIM);
    if (a->fade > 0) app_redraw();
}

static void collect(Ambient *a)
{
    const AmbientItem *pool;
    int np = app_ambient_pool(&pool);
    a->items = calloc((size_t)(np + g_app.n_library + 1), sizeof *a->items);
    /* library first: that is what the viewer actually watches */
    for (int i = 0; i < g_app.n_library; i++) {
        const LibraryItem *li = &g_app.library[i];
        if (li->removed || !li->id || !li->name) continue;
        MetaPreview m = { .id = li->id, .type = li->type, .name = li->name, .poster = li->poster };
        char buf[512];
        const char *bg = app_art_background(&m, buf, sizeof buf);
        if (!bg) continue;
        AmbientItem *it = &a->items[a->n++];
        snprintf(it->id, sizeof it->id, "%s", li->id);
        snprintf(it->name, sizeof it->name, "%s", li->name);
        snprintf(it->background, sizeof it->background, "%s", bg);
        if (li->poster && *li->poster) snprintf(it->poster, sizeof it->poster, "%s", li->poster);
        else if (!strncmp(li->id, "tt", 2)) snprintf(it->poster, sizeof it->poster, "https://images.metahub.space/poster/medium/%s/img", li->id);
    }
    for (int i = 0; i < np; i++) {
        int dup = 0;
        for (int k = 0; k < a->n; k++) if (strcmp(a->items[k].id, pool[i].id) == 0) { dup = 1; break; }
        if (!dup) a->items[a->n++] = pool[i];
    }
    /* a different starting point each time, so it does not always open on the same title */
    if (a->n > 1) {
        int start = (int)(time(NULL) % a->n);
        for (int i = 0; i < a->n / 2; i++) {
            int j = (start + i) % a->n, k = (start + a->n - 1 - i) % a->n;
            if (j == k) continue;
            AmbientItem tmp = a->items[j]; a->items[j] = a->items[k]; a->items[k] = tmp;
        }
    }
}

/* the picture a style shows of a title: its poster in the cinema, else the backdrop */
static const char *item_url(const AmbientItem *it)
{
    return g_style == AMBIENT_MARQUEE && it->poster[0] ? it->poster : it->background;
}

static void advance(Ambient *a)
{
    if (a->n < 2) return;
    a->prev = a->cur;
    a->cur = (a->cur + 1) % a->n;
    if (g_style == AMBIENT_DEPTH)   /* the next one that has a depth map, if any does */
        for (int i = 0; i < a->n && !depth_info(a->items[a->cur].background); i++) a->cur = (a->cur + 1) % a->n;
    a->fade = 1;
    a->t = 0;
    a->cur_ready = 0;
    a->shown_at = g_app.time;
}

static unsigned art_get(const char *url, int *w, int *h);
static void update(Screen *s, float dt)
{
    Ambient *a = s->st;
    if (a->fv_web && g_app.foregrounds != a->fv_web_fg) { LOGI("ambient: back from Cuelight YouTube"); app_pop(); return; }   /* a key there ended it */
    stage_tick(a);
    if (a->off) { if (g_app.time - a->drawn_at >= 1) app_redraw(); return; }
    if (g_app.time - a->drawn_at >= 1.0f / 30) app_redraw();
    if (g_style == AMBIENT_LOGO) {   /* drifts and bounces off the edges */
        a->lx += a->lvx * dt; a->ly += a->lvy * dt;
        if (a->lx < 40) { a->lx = 40; a->lvx = -a->lvx; }
        if (a->ly < 40) { a->ly = 40; a->lvy = -a->lvy; }
        if (a->lx > SNTV_UI_W - 40 - LOGO_S) { a->lx = SNTV_UI_W - 40 - LOGO_S; a->lvx = -a->lvx; }
        if (a->ly > SNTV_UI_H - 40 - LOGO_S) { a->ly = SNTV_UI_H - 40 - LOGO_S; a->lvy = -a->lvy; }
        return;
    }
    if (g_style == AMBIENT_WALL) { a->wall_y += dt * 18; return; }
    if (g_style == AMBIENT_FIRE && a->fv_state == 4) fv_play(a);
    if (g_style == AMBIENT_FIRE && a->fv_state == 2) {   /* the real one: looped, or the next way to play it */
        VideoInfo vi; video_info(&vi);
        int settled = g_app.time - a->fv_kick > 2;   /* the backend takes a moment to say it is playing again */
        if (vi.state == VIDEO_ENDED && settled) { a->fv_kick = g_app.time; video_seek(0); video_play(); }
        else if (vi.state == VIDEO_ERROR) { a->fv_i++; fv_play(a); }
        else if (vi.state == VIDEO_IDLE && settled) {   /* stopped under us (a break): start it again, a few times at most */
            if (++a->fv_restarts > 5) { a->fv_state = 3; LOGW("ambient: the fireplace video keeps stopping; the drawn fire instead"); }
            else fv_play(a);
        }
        return;
    }
    if (g_style == AMBIENT_FIRE) {   /* 24 steps a second, whatever the frame rate */
        if (!a->heat) {
            a->heat = calloc(FIRE_W * FIRE_H, 1); a->px = calloc(FIRE_W * FIRE_H, 4); a->seed = (unsigned)time(NULL);
            if (!a->heat || !a->px) return;
            for (int i = 0; i < 60; i++) fire_step(a);   /* lit already */
            a->fire_tex = gfx_texture_rgba(a->px, FIRE_W, FIRE_H);
        }
        a->fire_acc += dt;
        if (a->fire_acc >= 1.0f / 24) {
            a->fire_acc = a->fire_acc > 0.2f ? 0 : a->fire_acc - 1.0f / 24;
            fire_step(a);
            gfx_texture_update(a->fire_tex, a->px, FIRE_W, FIRE_H);
        }
        return;
    }
    if (g_style == AMBIENT_CLOCK || g_style == AMBIENT_AURORA || g_style == AMBIENT_WARP || !a->n) return;
    a->t += dt;
    if (g_style == AMBIENT_DEPTH && a->n > 1) {
        const AmbientItem *next = &a->items[(a->cur + 1) % a->n];
        depth_texture(next->background);   /* its map on its way too */
        /* the list of maps came after the first was chosen: on to one that has a map */
        if (!a->depth_chosen && depth_ready()) {
            a->depth_chosen = 1;
            if (!depth_info(a->items[a->cur].background))
                for (int i = 1; i < a->n; i++) if (depth_info(a->items[(a->cur + i) % a->n].background)) { advance(a); return; }
        }
    }
    if (a->fade > 0) { a->fade -= dt / FADE_S; if (a->fade < 0) a->fade = 0; }
    int w, h;
    const char *url = item_url(&a->items[a->cur]);
    unsigned tex = image_get(url, &w, &h);
    if (tex && !a->cur_ready) { a->cur_ready = 1; a->shown_at = g_app.time; a->t = 0; }
    if (!tex && (image_failed(url) || g_app.time - a->shown_at > WAIT_S)) { advance(a); return; }
    if (a->n > 1) { int nw, nh; art_get(item_url(&a->items[(a->cur + 1) % a->n]), &nw, &nh); }   /* the next one's full-size copy on its way */
    if (a->cur_ready && a->t > (g_style == AMBIENT_RAIN ? SLOT_S * 3 : g_style == AMBIENT_GALLERY ? SLOT_S * 4 : g_style == AMBIENT_DEPTH ? DEPTH_SLOT_S : SLOT_S)) advance(a);   /* the rain lingers on one view */
}

/* The screensaver fills the screen: its artwork at full size (TMDB's original, the larger
 * metahub poster, up to 1920 px), the smaller copy until that comes or if it fails, and
 * only the smaller one while memory is short. */
static const char *hd_url(const char *url, char *buf, size_t n)
{
    const char *p;
    if (!url || image_memory_low()) return url;
    if ((p = strstr(url, "image.tmdb.org/t/p/w"))) {
        const char *rest = strchr(p + 20, '/');   /* p + 19: the "w" of "w1280" */
        int size = atoi(p + 20);
        if (!rest) return url;
        snprintf(buf, n, "%.*s%s%s", (int)(p + 19 - url), url, size >= 780 ? "original" : "w780", rest);
        return buf;
    }
    if ((p = strstr(url, "images.metahub.space/poster/")) && (!strncmp(p + 28, "small/", 6) || !strncmp(p + 28, "medium/", 7))) {
        snprintf(buf, n, "%.*slarge/%s", (int)(p + 28 - url), url, strchr(p + 28, '/') + 1);
        return buf;
    }
    if ((p = strstr(url, "images.metahub.space/background/small/"))) {
        snprintf(buf, n, "%.*smedium/%s", (int)(p + 32 - url), url, p + 38);
        return buf;
    }
    return url;
}

static unsigned art_get(const char *url, int *w, int *h)
{
    char buf[1024];
    const char *hd = hd_url(url, buf, sizeof buf);
    unsigned t = hd ? image_get_ex(hd, IMAGE_FULL, w, h, NULL) : 0;
    if (t) return t;
    return image_get(url, w, h);   /* meanwhile, or instead */
}

/* Cover-fit UVs, zoomed by (1 + ZOOM * k) around a point that slides across the image. */
static void draw_backdrop(const AmbientItem *it, float k, int dir, float alpha)
{
    int iw, ih;
    unsigned tex = art_get(it->background, &iw, &ih);
    if (!tex || alpha <= 0) return;
    float ia = (float)iw / (ih ? ih : 1), sa = (float)SNTV_UI_W / SNTV_UI_H;
    float u0 = 0, v0 = 0, u1 = 1, v1 = 1;
    if (ia > sa) { float f = sa / ia; u0 = (1 - f) / 2; u1 = 1 - u0; } else { float f = ia / sa; v0 = (1 - f) / 2; v1 = 1 - v0; }
    float z = 1 + ZOOM * k;
    float uw = (u1 - u0) / z, vh = (v1 - v0) / z;
    /* focus point drifts from one side to the other, alternating per slot */
    float fx = dir & 1 ? 0.35f + 0.3f * k : 0.65f - 0.3f * k;
    float fy = dir & 2 ? 0.40f + 0.2f * k : 0.60f - 0.2f * k;
    float cu = u0 + (u1 - u0) * fx, cv = v0 + (v1 - v0) * fy;
    float nu0 = cu - uw / 2, nv0 = cv - vh / 2;
    if (nu0 < u0) nu0 = u0;
    if (nv0 < v0) nv0 = v0;
    if (nu0 + uw > u1) nu0 = u1 - uw;
    if (nv0 + vh > v1) nv0 = v1 - vh;
    gfx_tex_uv(tex, 0, 0, SNTV_UI_W, SNTV_UI_H, nu0, nv0, nu0 + uw, nv0 + vh, RGBA(255, 255, 255, (int)(255 * alpha)));
}

static void clock_text(const struct tm *tmv, char *clock, size_t n)
{
    if (g_app.clock_mode == 1) snprintf(clock, n, "%02d:%02d", tmv->tm_hour, tmv->tm_min);
    else snprintf(clock, n, "%d:%02d", tmv->tm_hour % 12 ? tmv->tm_hour % 12 : 12, tmv->tm_min);
}

/* Depth: the backdrop pushes slowly into the picture, and the clock stands in it behind the
 * people (the place the depth map's maker chose, where they cover only a little of it). */
static void draw_depth(Ambient *a, const struct tm *tmv, const char *clock, float sx, float sy)
{
    const AmbientItem *cur = &a->items[a->cur];
    if (a->fade > 0 && a->prev != a->cur) draw_backdrop(&a->items[a->prev], 1, a->prev, 1);
    int iw, ih;
    unsigned tex = art_get(cur->background, &iw, &ih);
    float in = a->cur_ready ? 1 - a->fade : 0;
    if (!tex || in <= 0) return;
    const DepthInfo *di = g_app.fx_light ? NULL : depth_info(cur->background);
    unsigned dt = di ? depth_texture(cur->background) : 0;
    float W = SNTV_UI_W, H = SNTV_UI_H, ia = (float)iw / (ih ? ih : 1), sa = W / H;
    float u0 = 0, v0 = 0, u1 = 1, v1 = 1;
    if (ia > sa) { float f = sa / ia; u0 = (1 - f) / 2; u1 = 1 - u0; } else { float f = ia / sa; v0 = (1 - f) / 2; v1 = 1 - v0; }
    float iu = (u1 - u0) * 0.03f, iv = (v1 - v0) * 0.03f;   /* a little closer, so the far edge never shows */
    u0 += iu; u1 -= iu; v0 += iv; v1 -= iv;
    float k = a->t / DEPTH_SLOT_S; if (k > 1) k = 1;
    float push = 0.012f + 0.11f * k * k * (3 - 2 * k), cu = u0 + (u1 - u0) * 0.5f, cv = v0 + (v1 - v0) * 0.45f;
    Color c = RGBA(255, 255, 255, (int)(255 * in));
    if (dt) gfx_depth(dt, cu, cv, push, 0.3f, 0);
    else { float z = 1 + 0.06f * k, uw = (u1 - u0) / z, vh = (v1 - v0) / z; u0 = cu - uw / 2; v0 = cv - vh / 2; u1 = u0 + uw; v1 = v0 + vh; }
    gfx_tex_uv(tex, 0, 0, W, H, u0, v0, u1, v1, c);
    gfx_depth(0, 0, 0, 0, 0, 0);
    /* the clock's block, where the map's maker found the people cover a little of it */
    int behind = dt && di->fg > 0 && di->clock_x > 0;
    float bw = 0.38f * W, bx = (behind ? di->clock_x : 0.5f) * W - bw / 2 + sx, by = 0.06f * H + sy, bh = 0.30f * H;
    float px = 0.86f * bw * 100 / font_digits_measure(clock, 100), dh;
    if (px > 330) px = 330;
    dh = font_digits_height(px);
    float cw = font_digits_measure(clock, px), cx = bx + (bw - cw) / 2, top = by + (bh - dh) / 2 - 30;
    int al = (int)(255 * in);
    font_digits_draw(clock, cx + 3, top + 4, px, RGBA(0, 0, 0, al * 0x50 / 255));
    font_digits_draw(clock, cx, top, px, RGBA(0xFF, 0xF8, 0xEE, al * 0xF4 / 255));
    char date[64]; strftime(date, sizeof date, "%A %-d %B", tmv);
    for (char *p = date; *p; p++) if (*p >= 'a' && *p <= 'z') *p -= 32;
    float ly = top + dh + 34;
    font_draw_center(date, bx + bw / 2 + 2, ly + 2, FONT_S | FONT_B, RGBA(0, 0, 0, al * 0x60 / 255));
    font_draw_center(date, bx + bw / 2, ly, FONT_S | FONT_B, RGBA(0xFF, 0xFF, 0xFF, al * 0xD8 / 255));
    font_draw_center(cur->name, bx + bw / 2, ly + 40, FONT_XS | FONT_B, RGBA(0xFF, 0xFF, 0xFF, al * 0x90 / 255));
    if (behind) {   /* the people in front, over the block only */
        float x0 = bx - 40, x1 = bx + bw + 40, y0 = by - 40, y1 = by + bh + 90;
        if (x0 < 0) x0 = 0;
        if (y0 < 0) y0 = 0;
        if (x1 > W) x1 = W;
        gfx_depth(dt, cu, cv, push, 0.3f, di->fg);
        gfx_tex_uv(tex, x0, y0, x1 - x0, y1 - y0, u0 + (u1 - u0) * x0 / W, v0 + (v1 - v0) * y0 / H, u0 + (u1 - u0) * x1 / W, v0 + (v1 - v0) * y1 / H, c);
        gfx_depth(0, 0, 0, 0, 0, 0);
    }
    if (a->depth_logged != a->cur + 1 && (dt || a->t > 4)) {   /* once its map is in, or clearly is not coming */
        a->depth_logged = a->cur + 1;
        char key[96]; depth_key(cur->background, key, sizeof key);
        if (behind) LOGI("ambient: depth backdrop %s, the clock %.0f%% across behind them", key, di->clock_x * 100);
        else if (dt) LOGI("ambient: depth backdrop %s, the clock in front", key);
        else LOGI("ambient: %s has no depth map yet, shown flat", key);
    }
}

/* Poster wall: the artwork as tiles, four across, drifting slowly up and wrapping round. */
static void draw_wall(Ambient *a)
{
    if (!a->n) return;
    const float gap = 28, cols = 4, tw = (SNTV_UI_W - gap * (cols + 1)) / cols, th = tw * 0.5625f, step = th + gap;
    int first_row = (int)(a->wall_y / step);
    float off = a->wall_y - first_row * step;
    for (int r = 0; r < 6; r++)
        for (int c = 0; c < (int)cols; c++) {
            int idx = ((first_row + r) * (int)cols + c) % a->n;
            float x = gap + c * (tw + gap) + (r & 1 ? tw * 0.25f : 0) - tw * 0.12f, y = gap + r * step - off;
            int iw, ih;
            unsigned tex = image_get(a->items[idx].background, &iw, &ih);
            if (tex) gfx_rtex_uv(tex, x, y, tw, th, 10, 0, 0, 1, 1, RGBA(170, 170, 170, 255), 0);
            else if (!image_failed(a->items[idx].background)) gfx_rrect(x, y, tw, th, 10, RGBA(0x1A, 0x1A, 0x20, 0xFF));   /* still coming */
        }
    gfx_gradient_v(0, 0, SNTV_UI_W, 300, RGBA(0, 0, 0, 0xD0), RGBA(0, 0, 0, 0));
}

/* What the cinema says about a title: new episodes, where you are, or why it's here */
static void marquee_line(const Ambient *a, const AmbientItem *it, char *out, size_t n)
{
    const LibraryItem *li = a->cur < g_app.n_library ? app_library_find(it->id) : NULL;
    const NewEp *ne = newep_get(it->id);
    int se, ep;
    if (ne) snprintf(out, n, ne->count == 1 ? "A new episode is out" : "%d new episodes are out", ne->count);
    else if (li && li->time_offset > 0 && li->video_id && sscanf(strchr(li->video_id, ':') ? strchr(li->video_id, ':') : "", ":%d:%d", &se, &ep) == 2)
        snprintf(out, n, "Pick up at season %d, episode %d", se, ep);
    else if (li && li->time_offset > 0) snprintf(out, n, "Pick up where you left off");
    else snprintf(out, n, li ? "From your library" : "Popular right now");
}

static void draw_marquee(Ambient *a, float sx, float sy)
{
    float px, py, pw, ph;
    ambient_marquee(0, 0, SNTV_UI_W, SNTV_UI_H, g_app.time, &px, &py, &pw, &ph);
    if (!a->n) return;
    const AmbientItem *cur = &a->items[a->cur];
    int iw, ih;
    float in = a->cur_ready ? 1 - a->fade : 0;
    if (a->fade > 0 && a->prev != a->cur) {
        unsigned pt = art_get(item_url(&a->items[a->prev]), &iw, &ih);
        if (pt) gfx_rtex_uv(pt, px, py, pw, ph, 6, 0, 0, 1, 1, RGBA(255, 255, 255, 255), 0);
    }
    unsigned tex = art_get(item_url(cur), &iw, &ih);
    if (tex && in > 0) gfx_rtex_uv(tex, px, py, pw, ph, 6, 0, 0, 1, 1, RGBA(255, 255, 255, (int)(255 * in)), 0);
    float tx = px + pw + 150 + sx, ty = py + 120 + sy, tw = SNTV_UI_W - tx - THEME_MARGIN;
    if (SNTV_PHONE) { tx = 40 + sx; ty = py + ph + 60 + sy; tw = SNTV_UI_W - 80; }   /* upright: under the poster */
    font_draw("N O W   S H O W I N G", tx, ty, FONT_S | FONT_B, RGBA(0xE8, 0xC2, 0x6A, 0xFF));
    font_draw_fit(cur->name, tx, ty + 50, FONT_XL | FONT_B, RGBA(0xFF, 0xF6, 0xE8, (int)(255 * (0.3f + 0.7f * in))), tw);
    char line[96];
    marquee_line(a, cur, line, sizeof line);
    font_draw_fit(line, tx, ty + 50 + font_height(FONT_XL | FONT_B) + 14, FONT_M, RGBA(0xE0, 0xC8, 0xB0, 0xD0), tw);
}

static void draw_scene(Screen *s)
{
    Ambient *a = s->st;
    a->drawn_at = g_app.time;
    gfx_rect(0, 0, SNTV_UI_W, SNTV_UI_H, RGBA(0, 0, 0, 0xFF));
    struct timespec ts; clock_gettime(CLOCK_REALTIME, &ts);   /* one reading: time() runs a little behind this clock, and */
    time_t now = ts.tv_sec;                                   /* its second with this fraction sent the hands back a second */
    struct tm tmv;
    localtime_r(&now, &tmv);
    /* burn-in guard: a different offset every minute */
    float sx = (float)((tmv.tm_min % 5) - 2) * 7, sy = (float)(((tmv.tm_min / 5) % 3) - 1) * 7;
    char big[16];
    clock_text(&tmv, big, sizeof big);
    if (g_style == AMBIENT_CLOCK) {   /* a clock face on black, wandering a little every minute */
        clockface_draw(g_face, SNTV_UI_W / 2 + sx * 4, SNTV_UI_H / 2 + sy * 4, 1, &tmv, tmv.tm_sec + ts.tv_nsec / 1e9);
        if (g_face != CLOCKFACE_PRAYER && g_face != CLOCKFACE_HORIZON) prayer_line(SNTV_UI_W / 2 + sx * 4, SNTV_UI_H - 70 + sy * 4, 1);
        return;
    }
    if (g_style == AMBIENT_FIRE && a->fv_state == 2) {   /* the real fireplace, the clock over it */
        gfx_video_hole(0, 0, SNTV_UI_W, SNTV_UI_H);
        gfx_gradient_v(0, 0, SNTV_UI_W, 300, RGBA(0, 0, 0, 0x90), RGBA(0, 0, 0, 0));
        char date[64]; strftime(date, sizeof date, "%A, %B %-d", &tmv);
        float cx = SNTV_UI_W / 2 + sx * 6, cy = 70 + sy * 4;
        font_draw_center(big, cx, cy, FONT_HUGE | FONT_B, RGBA(0xF4, 0xEC, 0xE4, 0xF0));
        font_draw_center(date, cx, cy + font_height(FONT_HUGE | FONT_B) + 6, FONT_M, RGBA(0xE0, 0xD0, 0xC4, 0xD0));
        prayer_line(cx, cy + font_height(FONT_HUGE | FONT_B) + font_height(FONT_M) + 12, 1);
        return;
    }
    if (g_style == AMBIENT_FIRE) {
        draw_fire(a, sx, sy, big, &tmv);
        const char *hint = a->fv_state == 1 ? "Getting the fireplace video\xE2\x80\xA6" : !*store_get_str("fireplaceVideo", "") ? "For a real fireplace, add a video in Settings > Interface > Fireplace video" : NULL;
        if (hint) font_draw_center(hint, SNTV_UI_W / 2, SNTV_UI_H - 34, FONT_XS, RGBA(0x90, 0x80, 0x78, 0xC0));
        prayer_line(SNTV_UI_W / 2 + sx * 6, 70 + sy * 4 + font_height(FONT_HUGE | FONT_B) + font_height(FONT_M) + 12, 1);
        return;
    }
    if (g_style == AMBIENT_LOGO) {
        ui_mark(a->lx, a->ly, LOGO_S);
        float cw = font_measure(big, FONT_L | FONT_B);
        font_draw(big, SNTV_UI_W - THEME_MARGIN - cw + sx, SNTV_UI_H - 110 + sy, FONT_L | FONT_B, RGBA(0x90, 0x90, 0x98, 0xFF));
        return;
    }
    if (g_style == AMBIENT_WALL) draw_wall(a);
    if (g_style == AMBIENT_AURORA) ambient_aurora(0, 0, SNTV_UI_W, SNTV_UI_H, g_app.time);
    if (g_style == AMBIENT_WARP) ambient_warp(0, 0, SNTV_UI_W, SNTV_UI_H, g_app.time);
    if (g_style == AMBIENT_MARQUEE) draw_marquee(a, sx, sy);
    if (g_style == AMBIENT_RAIN) {
        int iw = 0, ih = 0;
        unsigned tex = a->n ? art_get(item_url(&a->items[a->cur]), &iw, &ih) : 0;
        ambient_rain(tex, iw, ih, 0, 0, SNTV_UI_W, SNTV_UI_H, g_app.time, a->cur_ready ? 1 - a->fade : 0.3f);
    }

    if (a->n && g_style == AMBIENT_DEPTH) { draw_depth(a, &tmv, big, sx, sy); prayer_line(SNTV_UI_W / 2 + sx, SNTV_UI_H - 70 + sy, 1); return; }

    if (g_style == AMBIENT_GALLERY) draw_gallery(a, sx, sy);
    if (a->n && g_style == AMBIENT_ARTWORK) {
        const AmbientItem *cur = &a->items[a->cur];
        float k = a->t / SLOT_S;
        if (k > 1) k = 1;
        if (a->fade > 0 && a->prev != a->cur) draw_backdrop(&a->items[a->prev], 1, a->prev, 1);
        draw_backdrop(cur, k, a->cur, a->cur_ready ? 1 - a->fade : 0);
        gfx_gradient_v(0, SNTV_UI_H - 420, SNTV_UI_W, 420, RGBA(0, 0, 0, 0), RGBA(0, 0, 0, 0xC0));
        gfx_gradient_v(0, 0, SNTV_UI_W, 260, RGBA(0, 0, 0, 0x70), RGBA(0, 0, 0, 0));
        font_draw_shadow(cur->name, THEME_MARGIN + sx, SNTV_UI_H - 150 + sy, FONT_M | FONT_B, THEME_TEXT, RGBA(0, 0, 0, 0xA0));
        font_draw(a->cur < g_app.n_library ? "From your library" : "Popular right now", THEME_MARGIN + sx, SNTV_UI_H - 104 + sy, FONT_XS | FONT_B, THEME_TEXT_DIM);
    }

    char clock[16], date[64];
    snprintf(clock, sizeof clock, "%s", big);
    strftime(date, sizeof date, "%A, %B %-d", &tmv);
    float cw = font_measure(clock, FONT_TITLE | FONT_B);
    float cx = SNTV_UI_W - THEME_MARGIN - cw + sx, cy = 64 + sy;
    font_draw_shadow(clock, cx, cy, FONT_TITLE | FONT_B, THEME_TEXT, RGBA(0, 0, 0, 0xA0));
    if (g_app.clock_mode != 1) font_draw_shadow(tmv.tm_hour < 12 ? "AM" : "PM", cx + cw + 12, cy + 14, FONT_M | FONT_B, THEME_TEXT_DIM, RGBA(0, 0, 0, 0xA0));
    font_draw_shadow(date, SNTV_UI_W - THEME_MARGIN - font_measure(date, FONT_S) + sx, cy + 88, FONT_S, THEME_TEXT_DIM, RGBA(0, 0, 0, 0xA0));
    prayer_line(SNTV_UI_W - THEME_MARGIN + sx, cy + 88 + font_height(FONT_S) + 6, 2);
    ui_brand(THEME_MARGIN + sx, 56 + sy, 44);
}

static void draw(Screen *s)
{
    Ambient *a = s->st;
    if (a->off) {   /* black, and a faint clock that moves about so the TV is known to be on */
        a->drawn_at = g_app.time;
        gfx_rect(0, 0, SNTV_UI_W, SNTV_UI_H, RGBA(0, 0, 0, 0xFF));
        time_t now = time(NULL); struct tm tmv; localtime_r(&now, &tmv);
        char big[16]; clock_text(&tmv, big, sizeof big);
        float x = SNTV_UI_W * (0.15f + 0.6f * ((tmv.tm_min * 7) % 10) / 9.0f), y = SNTV_UI_H * (0.2f + 0.6f * ((tmv.tm_min * 3) % 7) / 6.0f);
        font_draw(big, x, y, FONT_M, RGBA(0x48, 0x48, 0x50, 0xFF));
        return;
    }
    draw_scene(s);
    if (a->dim) {
        float p = (g_app.time - a->dim_at) / 8;
        if (p < 1) app_redraw(); else p = 1;
        gfx_rect(0, 0, SNTV_UI_W, SNTV_UI_H, RGBA(0, 0, 0, (int)(0xA8 * p)));
    }
}

static int key(Screen *s, KeyAction a)
{
    (void)s; (void)a;
    app_pop();   /* the key only wakes the screen */
    app_redraw();
    return 1;
}

static void destroy(Screen *s)
{
    Ambient *a = s->st;
    free(a->items);
    if (a->fire_tex) gfx_texture_destroy(a->fire_tex);
    font_digits_free();
    if (g_amb_live == a) g_amb_live = NULL;
    if (a->fv_state == 2) video_stop();
    video_hold_screensaver(0);
    free(a->heat); free(a->px);
    free(a);
    free(s);
}

Screen *screen_ambient_create(void)
{
    Screen *s = calloc(1, sizeof *s);
    Ambient *a = calloc(1, sizeof *a);
    g_style = g_app.ambient_style; g_face = g_app.clock_face;
    if (g_app.ambient_style == AMBIENT_SHUFFLE) {   /* a different one each time */
        unsigned r = (unsigned)time(NULL) * 2654435761u ^ (unsigned)(g_app.time * 1000);
        g_style = (int)env_sec("SNTV_AMBIENT_PICK", (float)(r % AMBIENT_SHUFFLE));
        g_face = (int)((r / 1000) % CLOCKFACE_COUNT);
        LOGI("ambient: shuffle picked %s%s%s", AMBIENT_STYLE_NAMES[g_style], g_style == AMBIENT_CLOCK ? ", face " : "", g_style == AMBIENT_CLOCK ? CLOCKFACE_NAMES[g_face] : "");
    }
    if (mem_stage() >= 2 && g_style != AMBIENT_CLOCK && g_style != AMBIENT_LOGO) {   /* tight: the plain clock costs nothing */
        LOGI("ambient: memory is tight; the plain clock instead of %s", AMBIENT_STYLE_NAMES[g_style]);
        g_style = AMBIENT_CLOCK; g_face = CLOCKFACE_DIGITAL;
    }
    collect(a);
    a->shown_at = a->born = g_app.time;
    a->lx = SNTV_UI_W * 0.3f; a->ly = SNTV_UI_H * 0.35f; a->lvx = 90; a->lvy = 64;
    g_amb_live = a;
    video_hold_screensaver(1);   /* this is the screensaver now: the TV's would cover it after its own wait */
    if (g_style == AMBIENT_FIRE) fv_start(a);
    if (g_style == AMBIENT_GALLERY) gallery_ask(a);
    if (g_style == AMBIENT_CLOCK || g_style == AMBIENT_DEPTH) font_digits_load();
    LOGI("ambient: %s screensaver with %d backdrops%s%s", AMBIENT_STYLE_NAMES[g_style], a->n,
         g_style == AMBIENT_CLOCK ? ", face " : "", g_style == AMBIENT_CLOCK ? CLOCKFACE_NAMES[g_face] : "");
    s->name = "ambient"; s->st = a;
    s->update = update; s->draw = draw; s->key = key; s->destroy = destroy; s->phone = 1;
    return s;
}
