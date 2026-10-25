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
#include "../phone.h"
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
    /* Fireplace (drawn): the flames and coals rebuilt each step, the hearth and logs drawn once */
    uint8_t *px, *glow_px, *coal_px, *coal_heat, *coal_alpha;
    unsigned fire_tex, glow_tex, bg_tex, logs_tex, crack_tex, coal_tex;
    float fire_acc, fire_t, fire_energy, fire_light; unsigned seed;
    /* a real fireplace video (Settings > Interface), played full screen and looped */
    int fv_state;         /* 0 none, 1 asking YouTube, 2 playing, 3 gave up (the drawn fire), 4 ready under another screen */
    float fv_kick;        /* when it was last (re)started */
    int fv_restarts;      /* found stopped and started again; a few and it gives way to the drawn fire */
    int fv_web; unsigned fv_web_fg;   /* handed to Cuelight YouTube (an LG): back on screen, the screensaver is over */
    int fv_free;          /* playing the free fireplace videos (none set, or the one set would not play) */
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

/* Fireplace videos under open licences, streamed (nothing is kept on the device), for when
 * none is set or the one set will not play; credited on screen as their licences ask */
static const struct { const char *url, *credit; } k_free_fire[] = {
    { "https://archive.org/download/betv-16515bcm-fireplace-plain/betv-16515bcm-fireplace-plain.mp4",
      "Holiday Fireplace  \xC2\xB7  Berkeley Community Media  \xC2\xB7  CC BY-SA 3.0  \xC2\xB7  Internet Archive" },
    { "https://archive.org/download/FireplaceStockFootage/FireplaceStockFootage.mp4",
      "Fireplace  \xC2\xB7  Vincent Wolf  \xC2\xB7  CC BY 3.0  \xC2\xB7  Internet Archive" },
};

static void fv_play(Ambient *a);

static int fv_free_start(Ambient *a, const char *why)
{
    if (a->fv_free || !store_get_int("fireplaceFree", 1)) return 0;
    const char *base = getenv("SNTV_FIRE_LIBRARY");   /* test hook: the same files from a local server */
    a->fv_n = 0;
    for (size_t i = 0; i < sizeof k_free_fire / sizeof *k_free_fire && a->fv_n < YT_PLAY_URLS; i++, a->fv_n++)
        if (base) snprintf(a->fv_urls[a->fv_n], sizeof a->fv_urls[a->fv_n], "%s/%s", base, strrchr(k_free_fire[i].url, '/') + 1);
        else snprintf(a->fv_urls[a->fv_n], sizeof a->fv_urls[a->fv_n], "%s", k_free_fire[i].url);
    a->fv_free = 1; a->fv_i = 0; a->fv_restarts = 0;
    LOGI("ambient: the free fireplace video (%s)", why);
    Screen *top = app_top();
    if (top && top->st != a) { a->fv_state = 4; return 1; }   /* a break is on top: its recording keeps the video plane */
    fv_play(a);
    return 1;
}

static void fv_play(Ambient *a)
{
    if (a->fv_i >= a->fv_n && fv_free_start(a, "the one set would not play")) return;
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
        fv_free_start(a, "YouTube refused the one set");
        return;
    }
    if (*store_get_str("fireplaceRefused", "")) { store_set_str("fireplaceRefused", NULL); store_save(); }
    a->fv_i = 0;
    Screen *top = app_top();
    if (top && top->st != a) { a->fv_state = 4; return; }   /* a break is on top: its recording keeps the video plane */
    fv_play(a);
}

/* A YouTube link already downloaded for Take breaks, with its picture: played from the TV, so
 * YouTube refusing to stream it does not matter. Nothing is saved for the fireplace itself. */
static int fv_saved_copy(const char *url, char *out, size_t n)
{
    char name[80], sound[120]; int is_video = 0;
    if (!youtube_saved(url, 1, name, sizeof name, &is_video) || !is_video) return 0;
    snprintf(sound, sizeof sound, "sntv-sound://%s", name);
    return phone_resolve_sound(sound, out, n) != NULL;
}

static void fv_start(Ambient *a)
{
    const char *url = store_get_str("fireplaceVideo", "");
    VideoInfo vi; video_info(&vi);
    if (vi.state != VIDEO_IDLE && vi.state != VIDEO_ENDED) return;   /* a paused film keeps its picture */
    if (!url || !*url) { fv_free_start(a, "none set"); return; }
    char id[16], tb[700];
    if (torbox_resolve(url, tb, sizeof tb)) {   /* your own video on TorBox: its permanent link */
        LOGI("ambient: fireplace video from TorBox");
        snprintf(a->fv_urls[0], sizeof a->fv_urls[0], "%s", tb);
        a->fv_n = 1; a->fv_i = 0;
        fv_play(a);
    } else if (torbox_is_link(url)) {
        LOGW("ambient: the fireplace video is on TorBox, but no TorBox key is set");
        fv_free_start(a, "no TorBox key");
    } else if (youtube_video_id(url, id) && fv_saved_copy(url, a->fv_urls[0], sizeof a->fv_urls[0])) {   /* saved on the TV already (a break's): no YouTube */
        LOGI("ambient: fireplace video from the copy saved on the TV");
        a->fv_n = 1; a->fv_i = 0;
        fv_play(a);
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

/* ---- The drawn fireplace: flames from moving noise over a few tongues, rebuilt on the CPU
 * twenty times a second into a small texture the GPU stretches, in a firebrick hearth whose
 * light breathes with them. Logs, the brick and the coals are drawn once into textures. ---- */

enum { FL_W = 128, FL_H = 104,   /* the flames */
       GL_W = 40, GL_H = 34,     /* their glow: the flames shrunk, a clear edge round them */
       BG_W = 480, BG_H = 270,   /* the brick and the hearth */
       LG_W = 384, LG_H = 112,   /* the logs */
       CO_W = 192, CO_H = 24 };  /* the coals */

static uint8_t g_perm[512]; static float g_val[256];

static void noise_init(unsigned seed)
{
    for (int i = 0; i < 256; i++) g_perm[i] = (uint8_t)i;
    for (int i = 255; i > 0; i--) {
        seed = seed * 1103515245u + 12345u;
        int j = (int)((seed >> 16) % (unsigned)(i + 1));
        uint8_t t = g_perm[i]; g_perm[i] = g_perm[j]; g_perm[j] = t;
    }
    for (int i = 0; i < 256; i++) { g_perm[256 + i] = g_perm[i]; seed = seed * 1103515245u + 12345u; g_val[i] = (float)((seed >> 16) & 0xFFFF) / 65535.0f; }
}

static float vnoise(float x, float y)
{
    float fx = floorf(x), fy = floorf(y);
    int xi = (int)fx & 255, yi = (int)fy & 255;
    float u = x - fx, v = y - fy;
    u = u * u * (3 - 2 * u); v = v * v * (3 - 2 * v);
    float a = g_val[g_perm[g_perm[xi] + yi]], b = g_val[g_perm[g_perm[xi + 1] + yi]];
    float c = g_val[g_perm[g_perm[xi] + yi + 1]], d = g_val[g_perm[g_perm[xi + 1] + yi + 1]];
    float ab = a + (b - a) * u, cd = c + (d - c) * u;
    return ab + (cd - ab) * v;
}

static float fbm(float x, float y, int oct)
{
    float s = 0, amp = 0.5f;
    for (int i = 0; i < oct; i++) { s += amp * vnoise(x, y); amp *= 0.5f; x *= 2.03f; y *= 2.03f; }
    return s;
}

/* cells round jittered points: the distance to the nearest and the next, and the nearest's
 * own random value (charred blocks on a log, lumps of coal) */
static void cells(float x, float y, float *f1, float *f2, float *id)
{
    int ix = (int)floorf(x), iy = (int)floorf(y);
    float d1 = 9, d2 = 9, v1 = 0;
    for (int dy = -1; dy <= 1; dy++)
        for (int dx = -1; dx <= 1; dx++) {
            int cx = ix + dx, cy = iy + dy;
            int h = g_perm[(g_perm[cx & 255] + cy) & 255];
            float px = cx + 0.15f + 0.7f * g_val[h], py = cy + 0.15f + 0.7f * g_val[(h + 101) & 255];
            float d = (px - x) * (px - x) + (py - y) * (py - y);
            if (d < d1) { d2 = d1; d1 = d; v1 = g_val[(h + 53) & 255]; } else if (d < d2) d2 = d;
        }
    *f1 = sqrtf(d1); *f2 = sqrtf(d2); *id = v1;
}

static float clamp01(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }
static uint8_t u8(float v) { return (uint8_t)(clamp01(v) * 255.0f + 0.5f); }

/* heat to colour: dark red at the edges, orange, yellow, near white in the core */
static void flame_colour(float I, uint8_t *o)
{
    o[0] = u8(I * 2.4f);
    float g = clamp01(I * 1.7f - 0.45f);
    o[1] = u8(g * sqrtf(sqrtf(g)) * 0.92f + g * 0.08f);
    o[2] = u8(I * 2.2f - 1.7f);
    float al = clamp01(I * 2.2f);
    o[3] = u8(al * (0.85f + 0.15f * al));
}

/* where each tongue rises: x, sway, phase, height */
#define TONGUES 9
static const float k_tongues[TONGUES][4] = {
    { 0.20f, 0.05f, 0.4f, 0.38f }, { 0.28f, 0.07f, 0.0f, 0.58f }, { 0.35f, 0.06f, 2.1f, 0.74f },
    { 0.43f, 0.06f, 1.0f, 0.95f }, { 0.51f, 0.07f, 3.3f, 1.00f }, { 0.58f, 0.06f, 4.4f, 0.82f },
    { 0.65f, 0.06f, 5.2f, 0.66f }, { 0.72f, 0.05f, 6.1f, 0.52f }, { 0.80f, 0.05f, 2.7f, 0.36f },
};

static void flame_step(Ambient *a)
{
    float t = a->fire_t;
    float hgt[TONGUES], amp[TONGUES], ph[TONGUES], tx[TONGUES];
    for (int k = 0; k < TONGUES; k++) {   /* each tongue leaps and drifts on its own, never in step */
        ph[k] = k_tongues[k][2];
        hgt[k] = k_tongues[k][3] * (0.55f + 0.60f * vnoise(t * 1.3f, k * 7.3f) + 0.08f * sinf(t * 6.0f + ph[k] * 3.1f));
        amp[k] = k_tongues[k][1];
        tx[k] = k_tongues[k][0] + 0.03f * (vnoise(t * 0.5f, k * 3.1f + 40) - 0.5f);
    }
    double sum = 0;
    for (int y = 0; y < FL_H; y++) {
        float Y = (float)y / (FL_H - 1);   /* 0 at the bottom */
        uint8_t *row = a->px + (size_t)(FL_H - 1 - y) * FL_W * 4;
        for (int x = 0; x < FL_W; x++) {
            float X0 = (float)x / (FL_W - 1);
            float X = X0 + (fbm(X0 * 4, Y * 2.5f - t * 1.8f, 2) - 0.5f) * 0.24f * Y;   /* the flames lean and curl */
            float base = 0;
            for (int k = 0; k < TONGUES; k++) {
                float h = hgt[k];
                if (Y >= h) continue;
                float sway = amp[k] * sinf(t * 1.7f + ph[k] + Y * 4.0f) * Y;
                float w = 0.07f * (1 - 0.85f * Y / h) + 0.005f;
                float d = (X - tx[k] - sway) / w;
                float v = (1 - d * d) * powf(1 - Y / h, 0.7f);
                if (v > base) base = v;
            }
            if (base <= 0.01f) { row[x * 4 + 3] = 0; row[x * 4] = row[x * 4 + 1] = row[x * 4 + 2] = 0; continue; }
            float warp = 0.3f * sinf(Y * 6 + t * 1.1f);
            float n = fbm(X * 9 + warp, Y * 3.2f - t * 2.6f, 3);
            float n2 = fbm(X * 20, Y * 7 - t * 4.8f, 2);
            /* the noise eats into the tongues more the higher they go: their tips tear off */
            float I = base * (0.35f + 1.3f * n) + (n2 - 0.5f) * 0.55f * base - 0.07f - Y * 0.30f * (1 - n);
            I = clamp01(I * 1.3f);
            if (Y < 0.08f) I *= Y / 0.08f;   /* the base melts into the coals */
            flame_colour(I, row + x * 4);
            sum += I;
        }
    }
    a->fire_energy = (float)(sum / (FL_W * FL_H)) * 4;
    /* the glow: the flames shrunk four times into the middle of a clear border, then
     * softened, so its stretched edges fade instead of ending in a line */
    static float acc[GL_H][GL_W][4];
    memset(acc, 0, sizeof acc);
    for (int gy = 0; gy < FL_H / 4; gy++)
        for (int gx = 0; gx < FL_W / 4; gx++) {
            float r = 0, g = 0, b = 0, al = 0;
            for (int yy = 0; yy < 4; yy++) {
                const uint8_t *p = a->px + ((size_t)(gy * 4 + yy) * FL_W + gx * 4) * 4;
                for (int xx = 0; xx < 4; xx++, p += 4) { r += p[0] * p[3]; g += p[1] * p[3]; b += p[2] * p[3]; al += p[3]; }
            }
            float *o = acc[gy + 4][gx + 4]; o[0] = r; o[1] = g; o[2] = b; o[3] = al;
        }
    for (int gy = 0; gy < GL_H; gy++)
        for (int gx = 0; gx < GL_W; gx++) {
            float r = 0, g = 0, b = 0, al = 0;
            for (int dy = -2; dy <= 2; dy++)
                for (int dx = -2; dx <= 2; dx++) {
                    int yy = gy + dy, xx = gx + dx;
                    if (yy < 0 || yy >= GL_H || xx < 0 || xx >= GL_W) continue;
                    r += acc[yy][xx][0]; g += acc[yy][xx][1]; b += acc[yy][xx][2]; al += acc[yy][xx][3];
                }
            uint8_t *o = a->glow_px + ((size_t)gy * GL_W + gx) * 4;
            o[0] = al > 0 ? (uint8_t)(r / al) : 0; o[1] = al > 0 ? (uint8_t)(g / al) : 0; o[2] = al > 0 ? (uint8_t)(b / al) : 0;
            o[3] = u8(al / (16 * 25 * 255.0f) * 2.2f);
        }
    /* the coals breathe: brighter and dimmer in slow patches */
    for (int y = 0; y < CO_H; y++)
        for (int x = 0; x < CO_W; x++) {
            size_t i = (size_t)y * CO_W + x;
            float hot = a->coal_heat[i] / 255.0f;
            float pulse = 0.55f + 0.45f * vnoise(x * 0.06f + t * 0.5f, y * 0.25f + t * 0.3f);
            float I = hot * pulse;
            uint8_t *o = a->coal_px + i * 4;
            o[0] = u8(0.06f + I * 1.5f); o[1] = u8(0.03f + (I - 0.3f) * 1.0f); o[2] = u8(0.025f + (I - 0.75f) * 1.0f);
            o[3] = a->coal_alpha[i];
        }
}

/* the hearth: firebrick in rows, darker mortar, soot above the fire, a stone slab in front,
 * all lit from the fire (drawn at full light; the frame dims it as the flames fall) */
static void make_hearth(uint8_t *px)
{
    for (int y = 0; y < BG_H; y++) {
        float v = (float)y / BG_H;
        for (int x = 0; x < BG_W; x++) {
            float u = (float)x / BG_W, al[3], lit;
            if (v > 0.83f) {   /* the hearth slab, lit near the fire, its front edge catching it */
                float s = 0.55f + 0.45f * fbm(x * 0.05f, y * 0.12f, 3);
                al[0] = 0.30f * s; al[1] = 0.25f * s; al[2] = 0.22f * s;
                float du = (u - 0.5f) / 0.28f, dv = (v - 0.83f) / 0.09f;
                lit = 0.02f + 1.0f * expf(-du * du - dv * dv);
                if (v < 0.835f) lit *= 1.8f;
            } else {
                int row = (int)(v * 18), off = row & 1;
                float bu = u * 7.5f + off * 0.5f, bv = v * 18;
                int col = (int)floorf(bu);
                float fu = bu - col, fv = bv - row;
                float mortar = (fu < 0.018f || fu > 0.982f || fv < 0.05f || fv > 0.95f) ? 1 : 0;
                float h = g_val[(g_perm[(col + 17) & 255] + row * 31) & 255];
                float grain = 0.75f + 0.25f * fbm(x * 0.09f, y * 0.09f, 3);
                if (mortar) { al[0] = 0.24f; al[1] = 0.21f; al[2] = 0.19f; }
                else { al[0] = (0.30f + 0.14f * h) * grain; al[1] = (0.14f + 0.05f * h) * grain; al[2] = (0.09f + 0.03f * h) * grain; }
                float su = (u - 0.5f) / 0.2f, sv = clamp01((0.75f - v) / 0.75f);
                float soot = expf(-su * su) * sv * 0.85f;
                for (int c = 0; c < 3; c++) al[c] *= 1 - soot;
                float du = (u - 0.5f) / 0.24f, dv = (v - 0.78f) / 0.36f;
                lit = 0.015f + 1.05f * expf(-du * du - dv * dv);
            }
            uint8_t *o = px + ((size_t)y * BG_W + x) * 4;
            o[0] = u8(al[0] * lit * 1.9f); o[1] = u8(al[1] * lit * 1.35f); o[2] = u8(al[2] * lit * 0.9f); o[3] = 255;
        }
    }
}

/* three charred logs, each a cylinder: black bark in alligator checks, its top lit orange by
 * the flames, its underside warm from the coals, a cut end with its rings. The cracks that
 * glow go into their own mask, drawn flickering over the logs. */
static const float k_logs[][5] = {   /* x0, x1, y at x0, radius, tilt (texture pixels) */
    { 50, 330, 44, 17, -0.03f }, { 26, 250, 64, 20, 0.07f }, { 150, 366, 88, 21, -0.055f },
};

static void make_logs(uint8_t *px, uint8_t *crack)
{
    memset(px, 0, (size_t)LG_W * LG_H * 4); memset(crack, 0, (size_t)LG_W * LG_H);
    for (int l = 0; l < 3; l++) {
        float x0 = k_logs[l][0], x1 = k_logs[l][1], y0 = k_logs[l][2], r = k_logs[l][3], tilt = k_logs[l][4];
        for (int x = (int)(x0 - r); x <= (int)(x1 + r * 0.5f); x++) {
            if (x < 0 || x >= LG_W) continue;
            float yc = y0 + (x - x0) * tilt;
            float half = r;
            if (x < x0) { float e = (x0 - x) / r; half = e < 1 ? r * sqrtf(1 - e * e) : 0; }
            if (half < 0.5f) continue;
            for (int y = (int)(yc - half - 1); y <= (int)(yc + half + 1); y++) {
                if (y < 0 || y >= LG_H) continue;
                float vv = (y - yc) / half;   /* -1 top .. 1 bottom */
                float cover = clamp01(half + 0.5f - fabsf(y - yc));
                if (cover <= 0) continue;
                float vc = vv < -1 ? -1 : vv > 1 ? 1 : vv;
                float along = (x - x0) * sqrtf(1 + tilt * tilt), around = asinf(vc) * r;   /* on the bark, in pixels */
                /* charred into blocks, longer than tall, split by deep cracks */
                float f1, f2, id;
                cells(along / (r * 0.80f) + l * 17, around / (r * 0.38f) + l * 5, &f1, &f2, &id);
                float seam = clamp01(1 - (f2 - f1) / 0.13f);
                float bark = 0.05f + 0.045f * id + 0.035f * fbm(along * 0.3f, around * 0.5f, 2);
                float c[3] = { bark * 1.05f, bark * 0.92f, bark * 0.82f };
                float nz = sqrtf(fmaxf(0, 1 - vc * vc));
                float top = clamp01(-vc * 1.3f - 0.15f) * (0.6f + 0.4f * nz), under = clamp01(vc * 1.4f - 0.3f);
                float rim = clamp01((-vc - 0.82f) * 6);   /* the top edge, catching the flames behind */
                c[0] += 0.40f * top * top + 0.45f * under * under + 0.35f * rim;
                c[1] += 0.15f * top * top + 0.12f * under * under + 0.12f * rim;
                c[2] += 0.04f * top * top + 0.02f * under * under + 0.02f * rim;
                for (int k = 0; k < 3; k++) c[k] *= (1 - 0.8f * seam) * (0.45f + 0.55f * nz);
                uint8_t *o = px + ((size_t)y * LG_W + x) * 4;
                float prev = o[3] / 255.0f, al = cover;
                for (int k = 0; k < 3; k++) o[k] = u8((o[k] / 255.0f) * (1 - al) * prev + c[k] * al);
                o[3] = u8(prev + al * (1 - prev));
                /* the seams glow where the fire reaches: underneath and along the inner side */
                float hot = seam * clamp01(vc * 1.5f - 0.05f) * clamp01(fbm(along * 0.04f + l * 5, 7, 2) * 2.4f - 0.6f);
                uint8_t *cr = crack + (size_t)y * LG_W + x;
                *cr = (uint8_t)(*cr * (1 - al) + 255 * clamp01(hot * 1.4f) * al);
            }
        }
        /* the cut end, facing out: charred rim, the rings, a little glow at the heart */
        float ex = x1, ey = y0 + (x1 - x0) * tilt, ax = r * 0.5f;
        for (int y = (int)(ey - r - 1); y <= (int)(ey + r + 1); y++)
            for (int x = (int)(ex - ax - 1); x <= (int)(ex + ax + 1); x++) {
                if (x < 0 || x >= LG_W || y < 0 || y >= LG_H) continue;
                float du = (x - ex) / ax, dv = (y - ey) / r, d = sqrtf(du * du + dv * dv);
                float cover = clamp01((1 - d) * r + 0.5f);
                if (cover <= 0) continue;
                float ring = 0.75f + 0.25f * sinf(d * 26 + fbm(du * 3, dv * 3, 2) * 4);
                float wood = d > 0.82f ? 0.08f : 0.30f * ring * (0.7f + 0.3f * (1 - d));
                float c[3] = { wood * 1.0f, wood * 0.62f, wood * 0.36f };
                float lit = 0.6f + 0.5f * clamp01(-dv);   /* lit from above */
                uint8_t *o = px + ((size_t)y * LG_W + x) * 4;
                for (int k = 0; k < 3; k++) o[k] = u8((o[k] / 255.0f) * (1 - cover) + c[k] * lit * cover);
                o[3] = u8(o[3] / 255.0f + cover * (1 - o[3] / 255.0f));
                uint8_t *cr = crack + (size_t)y * LG_W + x;
                float heart = d > 0.8f && d < 0.92f && dv > 0.3f ? 0.5f : 0;
                *cr = (uint8_t)(*cr * (1 - cover) + 255 * clamp01(heart) * cover);
            }
    }
}

/* the coal bed: lumps with glowing gaps between them, a ragged mound */
static void make_coals(uint8_t *heat, uint8_t *alpha)
{
    for (int y = 0; y < CO_H; y++)
        for (int x = 0; x < CO_W; x++) {
            float u = (float)x / CO_W, v = (float)y / CO_H;
            float mound = 0.85f * sqrtf(fmaxf(0, 1 - powf((u - 0.5f) / 0.5f, 2))) + 0.25f * (fbm(x * 0.15f, 3, 2) - 0.5f);
            float top = 1 - mound;
            float cover = clamp01((v - top) * CO_H * 0.7f);
            float f1, f2, id;
            float big = fbm(x * 0.08f, y * 0.2f, 2) > 0.5f ? 1 : 0;   /* lumps of two sizes */
            cells(x / (big ? 9.0f : 5.0f) + big * 50, y / (big ? 5.0f : 3.2f), &f1, &f2, &id);
            float gap = clamp01(1 - (f2 - f1) / 0.28f);   /* glowing between the lumps */
            float whole = id > 0.82f ? (id - 0.82f) * 4 : 0;   /* now and then a lump glows right through */
            float hot = gap * (0.45f + 0.55f * v) * (0.6f + 0.4f * id) + whole * (1 - f1) + 0.08f * v;
            heat[y * CO_W + x] = u8(hot); alpha[y * CO_W + x] = u8(cover);
        }
}

typedef struct { float x, y, sp; } Spark;
static Spark g_sparks[24];

static int fire_setup(Ambient *a)
{
    a->px = calloc((size_t)FL_W * FL_H, 4); a->glow_px = calloc((size_t)GL_W * GL_H, 4);
    a->coal_px = calloc((size_t)CO_W * CO_H, 4); a->coal_heat = malloc((size_t)CO_W * CO_H); a->coal_alpha = malloc((size_t)CO_W * CO_H);
    uint8_t *bg = malloc((size_t)BG_W * BG_H * 4), *logs = malloc((size_t)LG_W * LG_H * 4), *crack = malloc((size_t)LG_W * LG_H);
    int ok = a->px && a->glow_px && a->coal_px && a->coal_heat && a->coal_alpha && bg && logs && crack;
    if (ok) {
        a->seed = (unsigned)time(NULL);
        noise_init(0x5EED);   /* the brick and the logs are the same every time */
        make_hearth(bg); make_logs(logs, crack); make_coals(a->coal_heat, a->coal_alpha);
        a->bg_tex = gfx_texture_rgba(bg, BG_W, BG_H);
        a->logs_tex = gfx_texture_rgba(logs, LG_W, LG_H);
        a->crack_tex = gfx_texture_alpha(crack, LG_W, LG_H);
        noise_init(a->seed);
        a->fire_t = (float)(a->seed % 1000);
        flame_step(a);
        a->fire_tex = gfx_texture_rgba(a->px, FL_W, FL_H);
        a->glow_tex = gfx_texture_rgba(a->glow_px, GL_W, GL_H);
        a->coal_tex = gfx_texture_rgba(a->coal_px, CO_W, CO_H);
        for (int i = 0; i < 24; i++) {
            a->seed = a->seed * 1103515245u + 12345u; g_sparks[i].x = (a->seed >> 16) / 65535.0f;
            a->seed = a->seed * 1103515245u + 12345u; g_sparks[i].y = (a->seed >> 16) / 65535.0f;
            a->seed = a->seed * 1103515245u + 12345u; g_sparks[i].sp = (a->seed >> 16) / 65535.0f;
        }
    }
    free(bg); free(logs); free(crack);
    return ok;
}

static void fire_free(Ambient *a)
{
    unsigned *t[] = { &a->fire_tex, &a->glow_tex, &a->bg_tex, &a->logs_tex, &a->crack_tex, &a->coal_tex };
    for (size_t i = 0; i < sizeof t / sizeof *t; i++) if (*t[i]) { gfx_texture_destroy(*t[i]); *t[i] = 0; }
    free(a->px); free(a->glow_px); free(a->coal_px); free(a->coal_heat); free(a->coal_alpha);
    a->px = a->glow_px = a->coal_px = a->coal_heat = a->coal_alpha = NULL;
}

static void draw_fire(Ambient *a, float sx, float sy, const char *clock, struct tm *tmv)
{
    float W = SNTV_UI_W, H = SNTV_UI_H, t = a->fire_t;
    float s = fminf(W, H * 16 / 9) / 1280 * 1.15f, cx = W / 2, base = H * 0.83f;   /* base: the hearth's edge */
    /* the light on the brick rises and falls with the flames */
    float e = a->fire_light, li = 0.80f + 0.20f * (e > 1 ? 1 : e);
    uint8_t L = (uint8_t)(255 * li);
    if (a->bg_tex) gfx_tex(a->bg_tex, 0, 0, W, H, RGBA(L, L, L, 0xFF));
    float fw = 560 * s, fh = 470 * s, fx = cx - fw / 2, fy = base - 34 * s - fh;
    float gw = fw * GL_W / (FL_W / 4) * 1.25f, gh = fh * GL_H / (FL_H / 4) * 1.15f;   /* the glow, a little larger than the flames */
    if (a->glow_tex) gfx_tex(a->glow_tex, cx - gw / 2, fy + fh * 0.5f - gh * 0.5f, gw, gh, RGBA(0xFF, 0xFF, 0xFF, 0x90));
    if (a->fire_tex) gfx_tex(a->fire_tex, fx, fy, fw, fh, RGBA(0xFF, 0xFF, 0xFF, 0xFF));
    float lw = 560 * s, lh = lw * LG_H / LG_W, lx = cx - lw / 2 + 10 * s, ly = base + 6 * s - lh;
    if (a->coal_tex) gfx_tex(a->coal_tex, cx - 270 * s, base - 46 * s, 540 * s, 52 * s, RGBA(0xFF, 0xFF, 0xFF, 0xFF));
    if (a->logs_tex) gfx_tex(a->logs_tex, lx, ly, lw, lh, RGBA(0xFF, 0xFF, 0xFF, 0xFF));
    float flick = 0.70f + 0.30f * vnoise(t * 3.1f, 0.5f);
    if (a->crack_tex) {
        gfx_glyph(a->crack_tex, lx, ly, lw, lh, 0, 0, 1, 1, RGBA(0xFF, 0x6A, 0x14, (uint8_t)(255 * flick)));
        gfx_glyph(a->crack_tex, lx, ly, lw, lh, 0, 0, 1, 1, RGBA(0xFF, 0xC8, 0x60, (uint8_t)(110 * flick * flick)));
    }
    for (int i = 0; i < 24; i++) {   /* sparks rise, drift and fade */
        const Spark *k = &g_sparks[i];
        float life = fmodf(t * 0.32f * (0.6f + k->sp) + k->y, 1.0f);
        float px = cx - 120 * s + 240 * s * k->x + 45 * s * sinf(t * 2 + k->sp * 9) * life;
        float py = base - 110 * s - 380 * s * life;
        float al = (1 - life) * (1 - life) * (0.6f + 0.4f * sinf(t * 17 + i));
        if (al <= 0.02f) continue;
        gfx_circle(px, py, (2.2f + 1.5f * (1 - life)) * s, RGBA(0xFF, 0x80, 0x20, (uint8_t)(50 * al)));
        gfx_circle(px, py, (0.8f + 0.9f * (1 - life)) * s, RGBA(0xFF, 0xD0, 0x80, (uint8_t)(255 * al)));
    }
    gfx_gradient_v(0, 0, W, H * 0.40f, RGBA(0, 0, 0, 0xA0), RGBA(0, 0, 0, 0));   /* the clock reads over the dark */
    char date[64]; strftime(date, sizeof date, "%A, %B %-d", tmv);
    float tx = W / 2 + sx * 6, ty = 70 + sy * 4;
    font_draw_center(clock, tx, ty, FONT_HUGE | FONT_B, RGBA(0xF4, 0xE6, 0xD8, 0xF0));
    font_draw_center(date, tx, ty + font_height(FONT_HUGE | FONT_B) + 6, FONT_M, RGBA(0xC8, 0xA8, 0x90, 0xD0));
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
    if (a->fv_web) {   /* the TV would not open it: the fireplace without its video, and Settings says why */
        char why[200];
        if (video_youtube_web_result(why, sizeof why) < 0) {
            LOGW("ambient: the TV would not open Cuelight YouTube for the fireplace (%s)", why);
            store_set_str("fireplaceRefused", store_get_str("fireplaceVideo", "")); store_save();
            a->fv_web = 0;
            fv_free_start(a, "the TV would not open Cuelight YouTube");
        }
    }
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
    if (g_style == AMBIENT_FIRE) {   /* twenty steps a second (twelve on a light TV), whatever the frame rate */
        if (!a->px && !fire_setup(a)) { fire_free(a); return; }
        float step = g_app.fx_light ? 1.0f / 12 : 1.0f / 20;
        a->fire_acc += dt;
        if (a->fire_acc >= step) {
            a->fire_t += step;
            a->fire_acc = a->fire_acc > 0.2f ? 0 : a->fire_acc - step;
            flame_step(a);
            a->fire_light += (a->fire_energy - a->fire_light) * 0.35f;
            gfx_texture_update(a->fire_tex, a->px, FL_W, FL_H);
            gfx_texture_update(a->glow_tex, a->glow_px, GL_W, GL_H);
            gfx_texture_update(a->coal_tex, a->coal_px, CO_W, CO_H);
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
        if (a->fv_free && a->fv_i < (int)(sizeof k_free_fire / sizeof *k_free_fire))   /* the credit its licence asks for */
            font_draw_center(k_free_fire[a->fv_i].credit, SNTV_UI_W / 2, SNTV_UI_H - 34, FONT_XS, RGBA(0xB0, 0xA0, 0x96, 0xA0));
        return;
    }
    if (g_style == AMBIENT_FIRE) {
        draw_fire(a, sx, sy, big, &tmv);
        const char *hint = a->fv_state == 1 ? "Getting the fireplace video\xE2\x80\xA6" : !*store_get_str("fireplaceVideo", "") && !store_get_int("fireplaceFree", 1) ? "For a real fireplace, add a video in Settings > Interface > Fireplace video" : NULL;
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
    fire_free(a);
    font_digits_free();
    if (g_amb_live == a) g_amb_live = NULL;
    if (a->fv_state == 2) video_stop();
    video_hold_screensaver(0);
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
