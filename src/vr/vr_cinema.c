/* The headset's own controls: a sheet drawn over the screens (the left menu button opens it) with what
 * only a headset has: the screen's shape and size, how far away it is, whether it follows your head or
 * lets you lie down under it, a room to see or a dark one. What is chosen is kept in vr.cfg next to the
 * app's settings. Drawn by the app's own renderer into the panel, pointed at like any screen. */
#ifdef SNTV_VR

#include "vr.h"
#include "../log.h"
#include "../platform/platform.h"
#include "../ui/gfx.h"
#include "../ui/font.h"
#include <SDL.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

static int g_pref[VR_PREF_COUNT] = { 0, 1, 0, 0, 0, 22 };   /* flat, cinema size, stay, upright, dark room, 2.2 m */
static int g_loaded, g_open;
static float g_open_t;       /* 0 closed .. 1 open, eased */

static const char *PREF_NAME[VR_PREF_COUNT] = { "curved", "size", "follow", "bed", "pass", "dist" };

static void cfg_path(char *out, size_t n)
{
    char *p = SDL_GetPrefPath("stremio", "nativetv");
    snprintf(out, n, "%svr.cfg", p ? p : "");
    if (p) SDL_free(p);
}

static void load(void)
{
    if (g_loaded) return;
    g_loaded = 1;
    char path[1200]; cfg_path(path, sizeof path);
    FILE *f = fopen(path, "r");
    if (!f) return;
    char line[96];
    while (fgets(line, sizeof line, f)) {
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = 0;
        for (int i = 0; i < VR_PREF_COUNT; i++) if (!strcmp(line, PREF_NAME[i])) g_pref[i] = atoi(eq + 1);
    }
    fclose(f);
}

static void save(void)
{
    char path[1200]; cfg_path(path, sizeof path);
    FILE *f = fopen(path, "w");
    if (!f) return;
    for (int i = 0; i < VR_PREF_COUNT; i++) fprintf(f, "%s=%d\n", PREF_NAME[i], g_pref[i]);
    fclose(f);
}

int vr_pref(int which)
{
    load();
    return which >= 0 && which < VR_PREF_COUNT ? g_pref[which] : 0;
}

void vr_pref_set(int which, int value)
{
    load();
    if (which < 0 || which >= VR_PREF_COUNT || g_pref[which] == value) return;
    g_pref[which] = value;
    save();
}

int vr_cinema_is_open(void) { return g_open; }
void vr_cinema_toggle(void) { g_open = !g_open; }
void vr_cinema_close(void) { g_open = 0; }

typedef struct { const char *label; int pref; int n; const char *opts[5]; } Row;
static const Row ROWS[] = {
    { "Screen", VR_PREF_CURVED, 2, { "Flat", "Curved" } },
    { "Size", VR_PREF_SIZE, 5, { "Small", "Cinema", "Big", "Huge", "Fit the film" } },
    { "Follow my head", VR_PREF_FOLLOW, 3, { "Stay", "Lazy", "Always" } },
    { "Lying down", VR_PREF_BED, 2, { "Off", "On" } },
    { "Room", VR_PREF_PASS, 2, { "Dark", "See my room" } },
};
#define N_ROWS ((int)(sizeof ROWS / sizeof ROWS[0]))

/* where things were drawn this frame, so a tap can find them */
typedef struct { float x, y, w, h; int kind, a, b; } Hit;   /* kind 1 choose (a pref, b value), 2 dist minus, 3 dist plus, 4 view, 5 recenter, 6 close, 7 open */
static Hit g_hit[40];
static int g_n_hit;

static void hit(float x, float y, float w, float h, int kind, int a, int b)
{
    if (g_n_hit < (int)(sizeof g_hit / sizeof g_hit[0])) g_hit[g_n_hit++] = (Hit){ x, y, w, h, kind, a, b };
}

static void chip(float x, float y, float w, float h, const char *text, int on, int kind, int a, int b)
{
    gfx_rrect(x, y, w, h, h / 2, on ? RGBA(0x4F, 0x8C, 0xFF, 0xFF) : RGBA(0x2A, 0x2F, 0x3A, 0xFF));
    font_draw_center(text, x + w / 2, y + (h - font_height(FONT_S)) / 2, FONT_S | (on ? FONT_B : 0), on ? RGBA(0xFF, 0xFF, 0xFF, 0xFF) : RGBA(0xC8, 0xD0, 0xDC, 0xFF));
    hit(x, y, w, h, kind, a, b);
}

int vr_cinema_draw(float dt, int show_way_in)
{
    g_n_hit = 0;
    float target = g_open ? 1.0f : 0.0f;
    g_open_t += (target - g_open_t) * (dt > 0 ? (1 - expf(-14.0f * dt)) : 1);
    if (g_open_t < 0.01f && !g_open) g_open_t = 0;
    float W = (float)SNTV_UI_W, H = (float)SNTV_UI_H;

    if (g_open_t <= 0) {   /* a small way in, for hands with no menu button */
        if (show_way_in) chip(24, H - 76, 188, 52, "Cinema", 0, 7, 0, 0);
        return 0;
    }
    float a = g_open_t;
    float pw = 1240, rowh = 84, ph = 150 + (N_ROWS + 3) * rowh + 60;
    if (pw > W - 80) pw = W - 80;
    if (ph > H - 40) ph = H - 40;
    float px = (W - pw) / 2, py = (H - ph) / 2 + (1 - a) * 30;
    gfx_set_alpha(a);
    gfx_rect(0, 0, W, H, RGBA(0, 0, 0, 0x70));
    gfx_shadow(px, py, pw, ph, 36, 50, RGBA(0, 0, 0, 0xC0));
    gfx_rrect(px, py, pw, ph, 36, RGBA(0x14, 0x17, 0x1E, 0xF4));
    font_draw("Cinema", px + 48, py + 34, FONT_L | FONT_B, RGBA(0xFF, 0xFF, 0xFF, 0xFF));
    font_draw("Menu button closes this", px + 48, py + 84, FONT_XS, RGBA(0x8A, 0x94, 0xA6, 0xFF));
    chip(px + pw - 150, py + 30, 110, 56, "Done", 0, 6, 0, 0);

    float y = py + 140;
    float lx = px + 48, cx0 = px + 380;
    for (int i = 0; i < N_ROWS; i++) {
        const Row *r = &ROWS[i];
        if (r->pref == VR_PREF_PASS && !vr_passthrough_available()) continue;
        font_draw(r->label, lx, y + (64 - font_height(FONT_M)) / 2, FONT_M, RGBA(0xE6, 0xEA, 0xF2, 0xFF));
        float cw = (pw - 380 - 48 - (r->n - 1) * 14) / r->n;
        if (cw > 230) cw = 230;
        for (int k = 0; k < r->n; k++) chip(cx0 + k * (cw + 14), y, cw, 64, r->opts[k], vr_pref(r->pref) == k, 1, r->pref, k);
        y += rowh;
    }
    /* distance */
    font_draw("Distance", lx, y + (64 - font_height(FONT_M)) / 2, FONT_M, RGBA(0xE6, 0xEA, 0xF2, 0xFF));
    chip(cx0, y, 80, 64, "-", 0, 2, 0, 0);
    char dbuf[24]; snprintf(dbuf, sizeof dbuf, "%.1f m", vr_pref(VR_PREF_DIST) / 10.0);
    font_draw_center(dbuf, cx0 + 80 + 14 + 80, y + (64 - font_height(FONT_M)) / 2, FONT_M | FONT_B, RGBA(0xFF, 0xFF, 0xFF, 0xFF));
    chip(cx0 + 80 + 14 + 160 + 14, y, 80, 64, "+", 0, 3, 0, 0);
    y += rowh;
    /* the view of the film */
    font_draw("Film view", lx, y + (64 - font_height(FONT_M)) / 2, FONT_M, RGBA(0xE6, 0xEA, 0xF2, 0xFF));
    chip(cx0, y, 640, 64, vr_video_view_name(), 0, 4, 0, 0);
    y += rowh;
    chip(cx0, y, 300, 64, "Bring it in front of me", 0, 5, 0, 0);
    gfx_set_alpha(1);
    return fabsf(target - g_open_t) > 0.01f;
}

int vr_cinema_tap(float x, float y)
{
    int took = 0;
    for (int i = 0; i < g_n_hit; i++) {
        const Hit *h = &g_hit[i];
        if (x < h->x || x > h->x + h->w || y < h->y || y > h->y + h->h) continue;
        took = 1;
        switch (h->kind) {
        case 1: vr_pref_set(h->a, h->b); break;
        case 2: { int d = vr_pref(VR_PREF_DIST) - 2; vr_pref_set(VR_PREF_DIST, d < 8 ? 8 : d); } break;
        case 3: { int d = vr_pref(VR_PREF_DIST) + 2; vr_pref_set(VR_PREF_DIST, d > 80 ? 80 : d); } break;
        case 4: vr_video_cycle(); break;
        case 5: vr_recenter(); break;
        case 6: g_open = 0; break;
        case 7: g_open = 1; break;
        }
        break;
    }
    if (!took && g_open) { took = 1; g_open = 0; }   /* a tap outside the sheet closes it */
    return took;
}

#endif
