/* The launch animation (intro.h). */
#include "intro.h"
#include "../app.h"
#include "../log.h"
#include "font.h"
#include "rail.h"
#include "theme.h"
#include "widgets.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define GROW 0.45f    /* the mark grows in at the centre */
#define HOLD 0.30f
#define FLY 0.50f     /* to the rail, as the screen shows through */
#define MARK 168.0f   /* its size at the centre */

static int g_on;
static float g_t0 = -1;

void intro_start(void)
{
    const char *force = getenv("SNTV_INTRO");
    if (force && *force) { g_on = atoi(force) != 0; return; }
    /* scripted runs and screenshots go straight to the screen */
    g_on = g_app.transitions && !g_app.fx_light && !getenv("SNTV_KEYS") && !getenv("SNTV_NO_TRANSITIONS");
}

int intro_active(void) { return g_on; }
void intro_skip(void) { if (g_on) { g_on = 0; app_redraw(); } }

static float smooth(float t) { t = t < 0 ? 0 : t > 1 ? 1 : t; return t * t * (3 - 2 * t); }
static float back_out(float t) { t = t < 0 ? 0 : t > 1 ? 1 : t; float c = 1.6f; t -= 1; return 1 + (c + 1) * t * t * t + c * t * t; }

void intro_draw(void)
{
    if (!g_on) return;
    if (g_t0 < 0) g_t0 = g_app.time;   /* from the first frame drawn, not from the start of loading */
    float t = g_app.time - g_t0;
    if (t >= GROW + HOLD + FLY) { g_on = 0; LOGI("intro: done"); app_redraw(); return; }
    app_redraw();

    Screen *top = app_top();
    int to_rail = top && strcmp(top->name, "home") == 0;   /* Home has the mark in its rail; elsewhere it fades where it is */
    float fly = smooth((t - GROW - HOLD) / FLY);
    float cx = SNTV_UI_W / 2.0f, cy = SNTV_UI_H / 2.0f - 20;
    float s = MARK * (0.6f + 0.4f * back_out(t / GROW));
    float a = smooth(t / (GROW * 0.6f));
    float x = cx - s / 2, y = cy - s / 2;
    if (to_rail) {   /* the rail's mark: 48 at ((RAIL_W - 48) / 2, 44) */
        float tx = (RAIL_W - 48) / 2, ty = 44;
        x += (tx - x) * fly; y += (ty - y) * fly; s += (48 - s) * fly;
    } else a *= 1 - fly;

    /* the screen shows through as the mark leaves */
    gfx_rect(0, 0, SNTV_UI_W, SNTV_UI_H, COLOR_ALPHA(g_app.oled ? RGBA(0, 0, 0, 0xFF) : THEME_BG, (int)(255 * (1 - fly))));
    if (t < GROW + HOLD) {   /* a ring breathes out from the mark */
        float r = MARK * 0.55f + 160 * smooth(t / (GROW + HOLD)), ra = 1 - smooth(t / (GROW + HOLD));
        gfx_rrect_ring(cx - r, cy - r, 2 * r, 2 * r, r, 2, COLOR_ALPHA(THEME_TEXT, (int)(0x50 * ra * a)));
    }
    if (!g_app.fx_light && fly < 1) gfx_shadow(x, y + s * 0.08f, s, s, s * 0.28f, s * 0.3f, RGBA(0, 0, 0, (int)(0x90 * a * (1 - fly))));
    gfx_set_alpha(a);
    ui_mark(x, y, s);
    gfx_set_alpha(1);
    float wa = smooth((t - GROW * 0.5f) / GROW) * (1 - smooth(fly * 3));   /* the name, under the mark, gone as it flies */
    if (wa > 0.01f) font_draw_center("Stremio", cx, cy + MARK / 2 + 34, FONT_L | FONT_B, COLOR_ALPHA(THEME_TEXT, (int)(255 * wa)));
}
