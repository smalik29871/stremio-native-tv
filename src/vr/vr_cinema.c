/* The headset's own controls: a box that floats in front of you when the left menu button is pressed (and goes
 * when it is pressed again), drawn by the app's own renderer into a layer of its own (vr_openxr.c places it and
 * points at it). Two pages: what to do now (back, home, search, play, seek, next episode, the screen's size and
 * distance) and how things are (the screen's shape and size, whether it follows your head or lets you lie down
 * under it, a room to see or a dark one, the room's light, the subtitles' depth, 3D eyes). What is chosen is kept
 * in vr.cfg next to the app's settings. */
#ifdef SNTV_VR

#include "vr.h"
#include "vr_internal.h"
#include "../log.h"
#include "../platform/platform.h"
#include "../ui/gfx.h"
#include "../ui/font.h"
#include "../ui/input.h"
#include "../app.h"
#include <SDL.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <GLES3/gl3.h>

static int g_pref[VR_PREF_COUNT] = { 0, 1, 0, 0, 0, 22, 1, 1, 0 };   /* flat, cinema size, stay, upright, dark room, 2.2 m, soft light, subtitles in front, eyes as they are */
static int g_loaded, g_open, g_tab, g_key;
static unsigned g_version = 1;

static const char *PREF_NAME[VR_PREF_COUNT] = { "curved", "size", "follow", "bed", "pass", "dist", "light", "subs", "eyes" };

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
    if (g_pref[VR_PREF_DIST] < 8) g_pref[VR_PREF_DIST] = 8;      /* a file edited by hand or half written: kept in range */
    if (g_pref[VR_PREF_DIST] > 80) g_pref[VR_PREF_DIST] = 80;
    if (g_pref[VR_PREF_SIZE] < 0 || g_pref[VR_PREF_SIZE] > 4) g_pref[VR_PREF_SIZE] = 1;
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
    g_version++;
    save();
}

int vr_cinema_is_open(void) { return g_open; }
void vr_cinema_toggle(void) { g_open = !g_open; g_version++; }
void vr_cinema_close(void) { if (g_open) { g_open = 0; g_version++; } }
unsigned vr_box_version(void) { return g_version + (g_update_version[0] ? 1u << 20 : 0); }   /* what the box shows changes when an update turns up */
void vr_box_set_tab(int t) { g_tab = t; g_version++; }
int vr_box_tab(void) { return g_tab; }

int vr_cinema_take_key(void) { int k = g_key; g_key = 0; return k; }

typedef struct { const char *label; int pref; int n; const char *opts[5]; } Row;
static const Row ROWS[] = {
    { "Screen", VR_PREF_CURVED, 2, { "Flat", "Curved" } },
    { "Size", VR_PREF_SIZE, 5, { "Small", "Cinema", "Big", "Huge", "Fit the film" } },
    { "Follow my head", VR_PREF_FOLLOW, 3, { "Stay", "Lazy", "Always" } },
    { "Lying down", VR_PREF_BED, 2, { "Off", "On" } },
    { "Room", VR_PREF_PASS, 2, { "Dark", "See my room" } },
    { "Room light", VR_PREF_LIGHT, 3, { "Off", "Soft", "Strong" } },
    { "Subtitles", VR_PREF_SUBS, 3, { "On the screen", "In front", "Close up" } },
    { "3D eyes", VR_PREF_EYES, 2, { "As they are", "Swapped" } },
};
#define N_ROWS ((int)(sizeof ROWS / sizeof ROWS[0]))

/* what each control does */
enum { K_CHOOSE = 1, K_KEY, K_STEP, K_VIEW, K_CENTER, K_CLOSE, K_TAB, K_HOME, K_CURVE, K_UPDATE };

typedef struct { float x, y, w, h; int kind, a, b; } Hit;
static Hit g_hit[64];
static int g_n_hit;
static float g_px = -1, g_py = -1;

static int hit_sig(const Hit *h) { return h->kind * 1000 + h->a * 10 + h->b + 1; }

static void chip_ex(float x, float y, float w, float h, const char *text, int on, int kind, int a, int b, int size)
{
    int hov = g_px >= x && g_px <= x + w && g_py >= y && g_py <= y + h;
    Color fill = on ? (hov ? RGBA(0x6F, 0xA0, 0xFF, 0xFF) : RGBA(0x4F, 0x8C, 0xFF, 0xFF)) : hov ? RGBA(0x3B, 0x44, 0x58, 0xFF) : RGBA(0x24, 0x29, 0x35, 0xFF);
    gfx_rrect(x, y, w, h, h / 2 > 40 ? 40 : h / 2, fill);
    Color fg = on || hov ? RGBA(0xFF, 0xFF, 0xFF, 0xFF) : RGBA(0xD2, 0xD8, 0xE4, 0xFF);
    font_draw_center(text, x + w / 2, y + (h - font_height(size)) / 2, size | (on ? FONT_B : 0), fg);
    if (g_n_hit < (int)(sizeof g_hit / sizeof g_hit[0])) g_hit[g_n_hit++] = (Hit){ x, y, w, h, kind, a, b };
}
static void chip(float x, float y, float w, float h, const char *text, int on, int kind, int a, int b) { chip_ex(x, y, w, h, text, on, kind, a, b, FONT_S); }
static void big(float x, float y, float w, float h, const char *text, int kind, int a, int b) { chip_ex(x, y, w, h, text, 0, kind, a, b, FONT_M); }

static void label(const char *t, float x, float y) { font_draw(t, x, y, FONT_XS | FONT_B, RGBA(0x8A, 0x94, 0xA6, 0xFF)); }

/* the box, in the middle band of the canvas (VR_BOX_W by VR_BOX_H); the pointer's place in canvas units shows what it is over */
void vr_box_draw(float px, float py)
{
    g_n_hit = 0; g_px = px; g_py = py;
    float W = (float)SNTV_UI_W, H = (float)SNTV_UI_H;
    float ox = (W - VR_BOX_W) / 2, oy = (H - VR_BOX_H) / 2;
    gfx_shadow(ox, oy, VR_BOX_W, VR_BOX_H, 48, 30, RGBA(0, 0, 0, 0xA0));
    gfx_rrect(ox, oy, VR_BOX_W, VR_BOX_H, 48, RGBA(0x12, 0x15, 0x1C, 0xF2));
    float lx = ox + 56, y = oy + 32;
    font_draw("Menu", lx, y + 6, FONT_L | FONT_B, RGBA(0xFF, 0xFF, 0xFF, 0xFF));
    chip(ox + 300, y, 230, 58, "Controls", g_tab == 0, K_TAB, 0, 0);
    chip(ox + 546, y, 290, 58, "Screen and room", g_tab == 1, K_TAB, 1, 0);
    chip(ox + VR_BOX_W - 56 - 150, y, 150, 58, "Close", 0, K_CLOSE, 0, 0);
    y += 100;
    float cw = VR_BOX_W - 112;

    if (g_tab == 0) {
        float bw3 = (cw - 2 * 16) / 3, bh = 104;
        label("GO TO", lx, y); y += 30;
        big(lx, y, bw3, bh, "Back", K_KEY, KEY_BACK, 0);
        big(lx + bw3 + 16, y, bw3, bh, "Home", K_HOME, 0, 0);
        big(lx + 2 * (bw3 + 16), y, bw3, bh, "Search", K_KEY, KEY_SEARCH, 0);
        y += bh + 34;
        label("PLAYING", lx, y); y += 30;
        float bw5 = (cw - 4 * 14) / 5;
        big(lx, y, bw5, bh, "-10 s", K_KEY, KEY_LEFT, 0);
        big(lx + (bw5 + 14), y, bw5 * 1.0f, bh, "Play / Pause", K_KEY, KEY_PLAY_PAUSE, 0);
        big(lx + 2 * (bw5 + 14), y, bw5, bh, "+10 s", K_KEY, KEY_RIGHT, 0);
        big(lx + 3 * (bw5 + 14), y, bw5, bh, "-1 min", K_KEY, KEY_REWIND, 0);
        big(lx + 4 * (bw5 + 14), y, bw5, bh, "+1 min", K_KEY, KEY_FORWARD, 0);
        y += bh + 14;
        big(lx, y, bw5 * 1.5f + 7, 84, "Next episode", K_KEY, KEY_GREEN, 0);
        big(lx + bw5 * 1.5f + 21, y, bw5 * 1.5f + 7, 84, "Other source", K_KEY, KEY_YELLOW, 0);
        big(lx + 3 * (bw5 + 14), y, bw5 * 2 + 14, 84, "Show the bar", K_KEY, KEY_MENU, 0);
        y += 84 + 34;
        label("THE SCREEN", lx, y); y += 30;
        float bw6 = (cw - 5 * 12) / 6, sh = 92;
        big(lx, y, bw6, sh, "Smaller", K_STEP, DOCK_SMALLER, 0);
        big(lx + (bw6 + 12), y, bw6, sh, "Bigger", K_STEP, DOCK_BIGGER, 0);
        big(lx + 2 * (bw6 + 12), y, bw6, sh, "Nearer", K_STEP, DOCK_NEARER, 0);
        big(lx + 3 * (bw6 + 12), y, bw6, sh, "Farther", K_STEP, DOCK_FARTHER, 0);
        big(lx + 4 * (bw6 + 12), y, bw6, sh, vr_pref(VR_PREF_CURVED) ? "Flat" : "Curve", K_CURVE, 0, 0);
        big(lx + 5 * (bw6 + 12), y, bw6, sh, "Center", K_CENTER, 0, 0);
        y += sh + 14;
        char vb[96]; snprintf(vb, sizeof vb, "Film view: %s  (tap for the next)", vr_video_view_name());
        big(lx, y, cw, 84, vb, K_VIEW, 0, 0);
        if (g_update_version[0]) {   /* a newer build is there: the way to take it is on the first page */
            char ub[96]; snprintf(ub, sizeof ub, "Update ready: %s  (tap to install)", g_update_version);
            chip_ex(lx, oy + VR_BOX_H - 100, cw, 66, ub, 1, K_UPDATE, 1, 0, FONT_S);
        } else font_draw("Right thumbstick: up and down for distance, left and right for size.  Grip: carry the screen.", lx, oy + VR_BOX_H - 56, FONT_XS, RGBA(0x8A, 0x94, 0xA6, 0xFF));
    } else {
        float rowh = 78, cx0 = ox + 420;
        for (int i = 0; i < N_ROWS; i++) {
            const Row *r = &ROWS[i];
            if (r->pref == VR_PREF_PASS && !vr_passthrough_available()) continue;
            font_draw(r->label, lx, y + (62 - font_height(FONT_M)) / 2, FONT_M, RGBA(0xE6, 0xEA, 0xF2, 0xFF));
            float w = (VR_BOX_W - 420 - 56 - (r->n - 1) * 14) / r->n;
            if (w > 240) w = 240;
            for (int k = 0; k < r->n; k++) chip(cx0 + k * (w + 14), y, w, 62, r->opts[k], vr_pref(r->pref) == k, K_CHOOSE, r->pref, k);
            y += rowh;
        }
        font_draw("Distance", lx, y + (62 - font_height(FONT_M)) / 2, FONT_M, RGBA(0xE6, 0xEA, 0xF2, 0xFF));
        chip(cx0, y, 90, 62, "-", 0, K_STEP, DOCK_NEARER, 0);
        char db[24]; snprintf(db, sizeof db, "%.1f m", vr_pref(VR_PREF_DIST) / 10.0);
        font_draw_center(db, cx0 + 90 + 14 + 80, y + (62 - font_height(FONT_M)) / 2, FONT_M | FONT_B, RGBA(0xFF, 0xFF, 0xFF, 0xFF));
        chip(cx0 + 90 + 14 + 160 + 14, y, 90, 62, "+", 0, K_STEP, DOCK_FARTHER, 0);
        y += rowh;
        font_draw("Updates", lx, y + (62 - font_height(FONT_M)) / 2, FONT_M, RGBA(0xE6, 0xEA, 0xF2, 0xFF));
        chip(cx0, y, 250, 62, "Check now", 0, K_UPDATE, 0, 0);
        if (g_update_version[0]) { char ub[64]; snprintf(ub, sizeof ub, "Install %s", g_update_version); chip(cx0 + 264, y, 340, 62, ub, 1, K_UPDATE, 1, 0); }
        char vb[80]; snprintf(vb, sizeof vb, "You have %s", app_update_running());
        font_draw(vb, cx0 + (g_update_version[0] ? 264 + 340 + 24 : 264), y + (62 - font_height(FONT_S)) / 2, FONT_S, RGBA(0x8A, 0x94, 0xA6, 0xFF));
    }
}

void vr_box_render(VrChain *c, float px, float py)
{
    if (!vr_chain_acquire(c)) return;
    gfx_begin_frame(RGBA(0, 0, 0, 0));
    glClearColor(0, 0, 0, 0); glClear(GL_COLOR_BUFFER_BIT);   /* see-through around the box */
    vr_box_draw(px, py);
    gfx_end_frame();
    vr_chain_release(c);
}

/* a number for the control at that place (0 over none), to know when the highlight moved */
int vr_box_hover(float x, float y)
{
    for (int i = 0; i < g_n_hit; i++) {
        const Hit *h = &g_hit[i];
        if (x >= h->x && x <= h->x + h->w && y >= h->y && y <= h->y + h->h) return hit_sig(h);
    }
    return 0;
}

int vr_box_tap(float x, float y)
{
    for (int i = 0; i < g_n_hit; i++) {
        const Hit *h = &g_hit[i];
        if (x < h->x || x > h->x + h->w || y < h->y || y > h->y + h->h) continue;
        switch (h->kind) {
        case K_CHOOSE: vr_pref_set(h->a, h->b); if (h->a == VR_PREF_SIZE || h->a == VR_PREF_DIST) vr_apply_prefs_again(); break;   /* a tap on the size already marked puts the screen back at it */
        case K_KEY: g_key = h->a; g_version++; break;
        case K_HOME: g_key = VR_KEY_HOME; g_version++; break;
        case K_STEP: vr_screen_step(h->a); g_version++; break;
        case K_VIEW: vr_video_cycle(); g_version++; break;
        case K_CENTER: vr_recenter(); break;
        case K_CURVE: vr_pref_set(VR_PREF_CURVED, !vr_pref(VR_PREF_CURVED)); break;
        case K_TAB: vr_box_set_tab(h->a); break;
        case K_UPDATE: g_key = h->a ? VR_KEY_UPDATE_INSTALL : VR_KEY_UPDATE_CHECK; g_version++; break;
        case K_CLOSE: g_open = 0; g_version++; break;
        }
        return 1;
    }
    return 0;
}

#endif
