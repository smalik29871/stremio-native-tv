#include "widgets.h"
#include "../http.h"
#include "font.h"
#include "../app.h"
#include "../image.h"
#include "qrcodegen.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

/* ---- icons: built from capsules, rings and the SDF triangle so they stay crisp at any size ---- */

void ui_icon(Icon icon, float cx, float cy, float s, Color c)
{
    float t = s * 0.11f;            /* stroke */
    if (t < 2.5f) t = 2.5f;
    float h = s * 0.5f;
    switch (icon) {
    case ICON_PLAY: gfx_play_triangle(cx - s * 0.32f, cy - s * 0.4f, s * 0.78f, s * 0.8f, c); break;
    case ICON_PAUSE:
        gfx_rrect(cx - s * 0.32f, cy - s * 0.38f, s * 0.22f, s * 0.76f, s * 0.06f, c);
        gfx_rrect(cx + s * 0.10f, cy - s * 0.38f, s * 0.22f, s * 0.76f, s * 0.06f, c);
        break;
    case ICON_SEARCH:
        gfx_rrect_ring(cx - s * 0.30f, cy - s * 0.30f, s * 0.42f, s * 0.42f, s * 0.21f, t, c);
        gfx_line(cx + s * 0.14f, cy + s * 0.14f, cx + s * 0.36f, cy + s * 0.36f, t * 1.15f, c);
        break;
    case ICON_SETTINGS:   /* sliders */
        for (int i = 0; i < 3; i++) {
            float y = cy - s * 0.3f + i * s * 0.3f;
            gfx_line(cx - h * 0.85f, y, cx + h * 0.85f, y, t, c);
            float kx = cx + (i == 1 ? -s * 0.18f : s * 0.16f);
            gfx_circle(kx, y, t * 1.6f, c);
        }
        break;
    case ICON_USER:
        gfx_circle(cx, cy - s * 0.17f, s * 0.19f, c);
        gfx_rrect(cx - s * 0.34f, cy + s * 0.1f, s * 0.68f, s * 0.32f, s * 0.16f, c);
        break;
    case ICON_PLUS:
        gfx_line(cx - h * 0.7f, cy, cx + h * 0.7f, cy, t, c);
        gfx_line(cx, cy - h * 0.7f, cx, cy + h * 0.7f, t, c);
        break;
    case ICON_CHECK:
        gfx_line(cx - h * 0.62f, cy + s * 0.02f, cx - h * 0.15f, cy + h * 0.5f, t, c);
        gfx_line(cx - h * 0.15f, cy + h * 0.5f, cx + h * 0.7f, cy - h * 0.5f, t, c);
        break;
    case ICON_AUDIO:      /* speaker */
        gfx_rrect(cx - s * 0.38f, cy - s * 0.14f, s * 0.18f, s * 0.28f, s * 0.03f, c);
        gfx_line(cx - s * 0.22f, cy - s * 0.12f, cx + s * 0.02f, cy - s * 0.34f, t, c);
        gfx_line(cx - s * 0.22f, cy + s * 0.12f, cx + s * 0.02f, cy + s * 0.34f, t, c);
        gfx_line(cx + s * 0.02f, cy - s * 0.34f, cx + s * 0.02f, cy + s * 0.34f, t, c);
        gfx_line(cx + s * 0.18f, cy - s * 0.12f, cx + s * 0.24f, cy, t * 0.9f, c);
        gfx_line(cx + s * 0.24f, cy, cx + s * 0.18f, cy + s * 0.12f, t * 0.9f, c);
        gfx_line(cx + s * 0.32f, cy - s * 0.24f, cx + s * 0.42f, cy, t * 0.9f, c);
        gfx_line(cx + s * 0.42f, cy, cx + s * 0.32f, cy + s * 0.24f, t * 0.9f, c);
        break;
    case ICON_SUBS:       /* caption box */
        gfx_rrect_ring(cx - s * 0.40f + t, cy - s * 0.28f + t, s * 0.80f - 2 * t, s * 0.56f - 2 * t, s * 0.08f, t, c);
        gfx_line(cx - s * 0.22f, cy + s * 0.06f, cx - s * 0.04f, cy + s * 0.06f, t, c);
        gfx_line(cx + s * 0.06f, cy + s * 0.06f, cx + s * 0.22f, cy + s * 0.06f, t, c);
        gfx_line(cx - s * 0.22f, cy - s * 0.08f, cx + s * 0.10f, cy - s * 0.08f, t, c);
        break;
    case ICON_NEXT:
        gfx_play_triangle(cx - s * 0.36f, cy - s * 0.32f, s * 0.52f, s * 0.64f, c);
        gfx_rrect(cx + s * 0.20f, cy - s * 0.32f, s * 0.14f, s * 0.64f, s * 0.04f, c);
        break;
    case ICON_BACK: case ICON_CHEVRON_LEFT:
        gfx_line(cx + s * 0.12f, cy - s * 0.30f, cx - s * 0.16f, cy, t, c);
        gfx_line(cx - s * 0.16f, cy, cx + s * 0.12f, cy + s * 0.30f, t, c);
        break;
    case ICON_CHEVRON_RIGHT:
        gfx_line(cx - s * 0.12f, cy - s * 0.30f, cx + s * 0.16f, cy, t, c);
        gfx_line(cx + s * 0.16f, cy, cx - s * 0.12f, cy + s * 0.30f, t, c);
        break;
    case ICON_CHEVRON_DOWN:
        gfx_line(cx - s * 0.30f, cy - s * 0.12f, cx, cy + s * 0.16f, t, c);
        gfx_line(cx, cy + s * 0.16f, cx + s * 0.30f, cy - s * 0.12f, t, c);
        break;
    case ICON_HOME:
        gfx_line(cx - s * 0.38f, cy - s * 0.02f, cx, cy - s * 0.38f, t, c);
        gfx_line(cx, cy - s * 0.38f, cx + s * 0.38f, cy - s * 0.02f, t, c);
        gfx_rrect(cx - s * 0.26f, cy - s * 0.06f, s * 0.52f, s * 0.42f, s * 0.05f, c);
        break;
    case ICON_LIBRARY:
        for (int i = 0; i < 3; i++) gfx_rrect(cx - s * 0.36f + i * s * 0.26f, cy - s * 0.34f, s * 0.18f, s * 0.68f, s * 0.04f, c);
        break;
    case ICON_STAR: {
        float pts[10][2];
        for (int i = 0; i < 10; i++) {
            float a = -1.5708f + i * 0.6283f, r = (i & 1) ? s * 0.2f : s * 0.45f;
            pts[i][0] = cx + cosf(a) * r; pts[i][1] = cy + sinf(a) * r;
        }
        for (int i = 0; i < 10; i++) gfx_line(pts[i][0], pts[i][1], pts[(i + 1) % 10][0], pts[(i + 1) % 10][1], t * 0.8f, c);
        gfx_circle(cx, cy, s * 0.2f, c);
        break;
    }
    case ICON_SWAP:
        gfx_line(cx - s * 0.36f, cy - s * 0.14f, cx + s * 0.30f, cy - s * 0.14f, t, c);
        gfx_line(cx + s * 0.30f, cy - s * 0.14f, cx + s * 0.14f, cy - s * 0.32f, t, c);
        gfx_line(cx + s * 0.36f, cy + s * 0.14f, cx - s * 0.30f, cy + s * 0.14f, t, c);
        gfx_line(cx - s * 0.30f, cy + s * 0.14f, cx - s * 0.14f, cy + s * 0.32f, t, c);
        break;
    case ICON_LINK:
        gfx_rrect_ring(cx - s * 0.40f, cy - s * 0.14f, s * 0.42f, s * 0.28f, s * 0.14f, t, c);
        gfx_rrect_ring(cx - s * 0.02f, cy - s * 0.14f, s * 0.42f, s * 0.28f, s * 0.14f, t, c);
        break;
    case ICON_PREV:   /* |<  (a bar and a left-pointing wedge) */
        gfx_rrect(cx - s * 0.36f, cy - s * 0.32f, s * 0.14f, s * 0.64f, s * 0.04f, c);
        gfx_line(cx + s * 0.30f, cy - s * 0.30f, cx - s * 0.10f, cy, t * 1.3f, c);
        gfx_line(cx - s * 0.10f, cy, cx + s * 0.30f, cy + s * 0.30f, t * 1.3f, c);
        gfx_line(cx + s * 0.30f, cy - s * 0.30f, cx + s * 0.30f, cy + s * 0.30f, t * 1.3f, c);
        break;
    case ICON_RELOAD:   /* open ring with an arrow head */
        gfx_rrect_ring(cx - s * 0.34f, cy - s * 0.34f, s * 0.68f, s * 0.68f, s * 0.34f, t, c);
        gfx_line(cx + s * 0.02f, cy - s * 0.52f, cx + s * 0.30f, cy - s * 0.36f, t * 1.2f, c);
        gfx_line(cx + s * 0.30f, cy - s * 0.36f, cx + s * 0.06f, cy - s * 0.16f, t * 1.2f, c);
        gfx_circle(cx + s * 0.10f, cy - s * 0.36f, t * 1.3f, c);
        break;
    case ICON_ASPECT:   /* a frame with a smaller frame inside */
        gfx_rrect_ring(cx - s * 0.42f, cy - s * 0.30f, s * 0.84f, s * 0.60f, s * 0.08f, t, c);
        gfx_rrect(cx + s * 0.04f, cy + s * 0.02f, s * 0.26f, s * 0.18f, s * 0.03f, c);
        break;
    case ICON_LIST:   /* three lines, the last with a play mark */
        gfx_line(cx - s * 0.38f, cy - s * 0.28f, cx + s * 0.38f, cy - s * 0.28f, t, c);
        gfx_line(cx - s * 0.38f, cy, cx + s * 0.38f, cy, t, c);
        gfx_line(cx - s * 0.38f, cy + s * 0.28f, cx + s * 0.02f, cy + s * 0.28f, t, c);
        gfx_play_triangle(cx + s * 0.16f, cy + s * 0.14f, s * 0.26f, s * 0.28f, c);
        break;
    case ICON_CLOCK:
        gfx_rrect_ring(cx - s * 0.40f, cy - s * 0.40f, s * 0.80f, s * 0.80f, s * 0.40f, t, c);
        gfx_line(cx, cy, cx, cy - s * 0.26f, t, c);
        gfx_line(cx, cy, cx + s * 0.20f, cy + s * 0.12f, t, c);
        break;
    case ICON_SPEED:   /* a gauge: arc and needle */
        gfx_rrect_ring(cx - s * 0.40f, cy - s * 0.30f, s * 0.80f, s * 0.80f, s * 0.40f, t, c);
        gfx_rect(cx - s * 0.50f, cy + s * 0.12f, s, s * 0.5f, RGBA(0, 0, 0, 0));
        gfx_line(cx, cy + s * 0.10f, cx + s * 0.24f, cy - s * 0.16f, t * 1.2f, c);
        gfx_circle(cx, cy + s * 0.10f, t * 1.4f, c);
        break;
    case ICON_INFO:
        gfx_rrect_ring(cx - s * 0.40f, cy - s * 0.40f, s * 0.80f, s * 0.80f, s * 0.40f, t, c);
        gfx_circle(cx, cy - s * 0.20f, t * 0.9f, c);
        gfx_line(cx, cy - s * 0.04f, cx, cy + s * 0.24f, t, c);
        break;
    case ICON_COMPASS:   /* ring with a needle */
        gfx_rrect_ring(cx - s * 0.42f, cy - s * 0.42f, s * 0.84f, s * 0.84f, s * 0.42f, t, c);
        gfx_line(cx + s * 0.18f, cy - s * 0.18f, cx - s * 0.18f, cy + s * 0.18f, t * 1.4f, c);
        gfx_line(cx + s * 0.18f, cy - s * 0.18f, cx + s * 0.02f, cy + s * 0.02f, t * 2.2f, c);
        break;
    case ICON_CLOSE:
        gfx_line(cx - h * 0.62f, cy - h * 0.62f, cx + h * 0.62f, cy + h * 0.62f, t, c);
        gfx_line(cx - h * 0.62f, cy + h * 0.62f, cx + h * 0.62f, cy - h * 0.62f, t, c);
        break;
    case ICON_LOCK: case ICON_UNLOCK:   /* padlock; open when unlocked */
        gfx_rrect(cx - s * 0.30f, cy - s * 0.04f, s * 0.60f, s * 0.46f, s * 0.08f, c);
        if (icon == ICON_LOCK) gfx_rrect_ring(cx - s * 0.19f, cy - s * 0.42f, s * 0.38f, s * 0.46f, s * 0.19f, t, c);
        else gfx_rrect_ring(cx - s * 0.02f, cy - s * 0.46f, s * 0.38f, s * 0.46f, s * 0.19f, t, c);
        gfx_circle(cx, cy + s * 0.18f, t * 0.9f, THEME_BG);
        break;
    case ICON_CALENDAR:
        gfx_rrect_ring(cx - s * 0.38f, cy - s * 0.30f, s * 0.76f, s * 0.70f, s * 0.10f, t, c);
        gfx_line(cx - s * 0.38f, cy - s * 0.08f, cx + s * 0.38f, cy - s * 0.08f, t, c);
        gfx_rrect(cx - s * 0.22f, cy - s * 0.46f, t, s * 0.22f, t / 2, c);
        gfx_rrect(cx + s * 0.22f - t, cy - s * 0.46f, t, s * 0.22f, t / 2, c);
        gfx_rrect(cx - s * 0.20f, cy + s * 0.06f, s * 0.16f, s * 0.14f, s * 0.03f, c);
        break;
    case ICON_PALETTE:   /* contrast: a ring with its left half filled */
        gfx_rrect_ring(cx - s * 0.40f, cy - s * 0.40f, s * 0.80f, s * 0.80f, s * 0.40f, t, c);
        ui_arc(cx, cy, s * 0.17f, s * 0.34f, 0.5f, 0.5f, c);
        break;
    case ICON_EYE:
        gfx_rrect_ring(cx - s * 0.44f, cy - s * 0.24f, s * 0.88f, s * 0.48f, s * 0.24f, t, c);
        gfx_circle(cx, cy, s * 0.13f, c);
        break;
    case ICON_MOON:   /* a crescent: most of a thick ring */
        ui_arc(cx + s * 0.02f, cy, s * 0.26f, s * 0.2f, 0.42f, 0.66f, c);
        break;
    case ICON_SPARK:   /* a four-point sparkle and a small one */
        gfx_line(cx - s * 0.06f, cy - s * 0.40f, cx - s * 0.06f, cy + s * 0.32f, t * 1.2f, c);
        gfx_line(cx - s * 0.40f, cy - s * 0.04f, cx + s * 0.28f, cy - s * 0.04f, t * 1.2f, c);
        gfx_circle(cx - s * 0.06f, cy - s * 0.04f, s * 0.12f, c);
        gfx_line(cx + s * 0.30f, cy + s * 0.16f, cx + s * 0.30f, cy + s * 0.40f, t * 0.8f, c);
        gfx_line(cx + s * 0.18f, cy + s * 0.28f, cx + s * 0.42f, cy + s * 0.28f, t * 0.8f, c);
        break;
    case ICON_GLOBE:
        gfx_rrect_ring(cx - s * 0.40f, cy - s * 0.40f, s * 0.80f, s * 0.80f, s * 0.40f, t, c);
        gfx_rrect_ring(cx - s * 0.16f, cy - s * 0.40f, s * 0.32f, s * 0.80f, s * 0.16f, t * 0.8f, c);
        gfx_line(cx - s * 0.38f, cy, cx + s * 0.38f, cy, t * 0.8f, c);
        break;
    case ICON_TV:
        gfx_rrect_ring(cx - s * 0.42f, cy - s * 0.32f, s * 0.84f, s * 0.54f, s * 0.06f, t, c);
        gfx_line(cx - s * 0.18f, cy + s * 0.36f, cx + s * 0.18f, cy + s * 0.36f, t, c);
        break;
    case ICON_KEY:
        gfx_rrect_ring(cx - s * 0.42f, cy - s * 0.18f, s * 0.36f, s * 0.36f, s * 0.18f, t, c);
        gfx_line(cx - s * 0.06f, cy, cx + s * 0.40f, cy, t, c);
        gfx_line(cx + s * 0.24f, cy, cx + s * 0.24f, cy + s * 0.16f, t, c);
        gfx_line(cx + s * 0.38f, cy, cx + s * 0.38f, cy + s * 0.20f, t, c);
        break;
    case ICON_BELL:
        gfx_rrect(cx - s * 0.28f, cy - s * 0.32f, s * 0.56f, s * 0.56f, s * 0.26f, c);
        gfx_rrect(cx - s * 0.38f, cy + s * 0.12f, s * 0.76f, s * 0.14f, s * 0.05f, c);
        gfx_circle(cx, cy + s * 0.36f, s * 0.08f, c);
        break;
    }
}

/* ---- background ---- */

static float g_tint[3] = { 0x8B, 0x5C, 0xF6 };
static char g_ambient_art[1024];
void ui_set_ambient_art(const char *url) { snprintf(g_ambient_art, sizeof g_ambient_art, "%s", url ? url : ""); }

void ui_background(Color tint)
{
    if (!tint) tint = g_theme.art_accent ? g_theme.base_accent : THEME_ACCENT;   /* no artwork: the theme's own colour */
    float tr = (float)((tint >> 24) & 255), tg = (float)((tint >> 16) & 255), tb = (float)((tint >> 8) & 255);
    float k = g_app.fx_light ? 1.0f : 0.06f;
    g_tint[0] += (tr - g_tint[0]) * k; g_tint[1] += (tg - g_tint[1]) * k; g_tint[2] += (tb - g_tint[2]) * k;
    if (fabsf(tr - g_tint[0]) + fabsf(tg - g_tint[1]) + fabsf(tb - g_tint[2]) > 3) app_redraw();
    Color t = RGBA((int)g_tint[0], (int)g_tint[1], (int)g_tint[2], 0xFF);
    theme_art_accent(t);   /* "From the artwork": the accent eases with the background's tint */

    /* The frame is cleared to the theme background (main.c), so a flat theme draws
     * nothing here; the others add one plain gradient tinted by the artwork. Soft
     * SDF glows used to shade more than a screen's worth of pixels every frame. */
    if (g_app.oled || !g_theme.glow) return;
    if (g_theme.glass && !g_app.fx_light && g_ambient_art[0]) {
        /* Glass sits on the last artwork, blurred by sampling a low mip (one textured quad) */
        int iw = 0, ih = 0;
        unsigned tex = image_get_ex(g_ambient_art, IMAGE_MIPMAP, &iw, &ih, NULL);
        if (tex) { ui_backdrop_blur(tex, iw, ih, 1); return; }
    }
    gfx_gradient_v(0, 0, SNTV_UI_W, SNTV_UI_H * 0.6f, COLOR_ALPHA(t, g_app.fx_light ? 0x18 : 0x26), COLOR_ALPHA(t, 0));
    /* the haze rises from the bottom: faded in from nothing, so no edge crosses the screen */
    if (g_theme.glass || g_theme.frost) gfx_gradient_v(0, SNTV_UI_H * 0.3f, SNTV_UI_W, SNTV_UI_H * 0.7f, COLOR_ALPHA(color_lerp(t, g_theme.haze, 0.5f), 0), COLOR_ALPHA(color_lerp(t, g_theme.haze, 0.5f), g_theme.id == THEME_SUNSET ? 0x40 : 0x22));
}

void ui_panel(float x, float y, float w, float h, float r, Color fill)
{
    gfx_rrect(x, y, w, h, r, fill);
    if (g_theme.frost) {   /* Mist: one light line along the top, no gradient or rim */
        float in = r < w / 2 ? r * 0.8f : w / 4;
        gfx_rect(x + in, y + 1, w - 2 * in, 1.5f, COLOR_ALPHA(RGBA(0xFF, 0xFF, 0xFF, 0xFF), (int)(0x38 * COLOR_A(fill) / 255.0f)));
        return;
    }
    if (!g_theme.glass) return;
    float a = COLOR_A(fill) / 255.0f;   /* frosted glass: a sheen at the top and a light rim */
    gfx_rrect_grad_v(x, y, w, h * 0.5f < 120 ? h * 0.5f : 120, r, COLOR_ALPHA(RGBA(0xFF, 0xFF, 0xFF, 0xFF), (int)(0x14 * a)), COLOR_ALPHA(RGBA(0xFF, 0xFF, 0xFF, 0xFF), 0));
    gfx_rrect_ring(x, y, w, h, r, 1.5f, COLOR_ALPHA(RGBA(0xFF, 0xFF, 0xFF, 0xFF), (int)(0x30 * a)));
}

static void cover_uv(int iw, int ih, float w, float h, float *u0, float *v0, float *u1, float *v1)
{
    float ia = (float)iw / (float)(ih ? ih : 1), pa = w / h;
    *u0 = 0; *v0 = 0; *u1 = 1; *v1 = 1;
    if (ia > pa) { float k = pa / ia; *u0 = (1 - k) / 2; *u1 = 1 - *u0; }
    else { float k = ia / pa; *v0 = (1 - k) / 2; *v1 = 1 - *v0; }
}

#define BD_W (SNTV_UI_W * 0.80f)

static float g_fade_left = 1150, g_fade_bottom = 330;

/* The image itself fades out (per-vertex alpha on a small grid), so the ambient
 * light behind it shows through with no seams. */
void ui_backdrop_image(unsigned tex, int iw, int ih, float alpha, float height)
{
    if (!tex || alpha <= 0) return;
    float u0, v0, u1, v1, w = BD_W, x0 = SNTV_UI_W - w;
    cover_uv(iw, ih, w, height, &u0, &v0, &u1, &v1);
    float shift = (v0 > 0) ? -v0 * 0.35f : 0;
    v0 += shift; v1 += shift;
    float left = g_fade_left, bottom = g_fade_bottom;
    float mid = x0 + (left - x0) * 0.45f, bm = bottom + (height - bottom) * 0.5f;
    float xs[4] = { x0, mid, left, SNTV_UI_W }, ax[4] = { 0.0f, 0.75f, 1.0f, 1.0f };
    float ys[5] = { 0, 150, bottom, bm, height }, ay[5] = { 0.55f, 1.0f, 1.0f, 0.8f, 0.0f };
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 4; j++) {
            float cx = xs[i], cw = xs[i + 1] - xs[i], cy = ys[j], ch = ys[j + 1] - ys[j];
            if (cw <= 0 || ch <= 0) continue;
            float cu0 = u0 + (u1 - u0) * (cx - x0) / w, cu1 = u0 + (u1 - u0) * (cx + cw - x0) / w;
            float cv0 = v0 + (v1 - v0) * cy / height, cv1 = v0 + (v1 - v0) * (cy + ch) / height;
#define A(ii, jj) RGBA(255, 255, 255, (int)(255 * alpha * ax[ii] * ay[jj]))
            gfx_tex_uv4(tex, cx, cy, cw, ch, cu0, cv0, cu1, cv1, A(i, j), A(i + 1, j), A(i, j + 1), A(i + 1, j + 1));
#undef A
        }
}

/* The video plane through the backdrop's shape, `t` of the way (a trailer behind the
 * artwork): call after ui_backdrop_fades, before the artwork drawn at (1 - t). */
void ui_backdrop_video(float t, float height)
{
    if (t <= 0) return;
    float w = BD_W, x0 = SNTV_UI_W - w;
    float left = g_fade_left, bottom = g_fade_bottom;
    float mid = x0 + (left - x0) * 0.45f, bm = bottom + (height - bottom) * 0.5f;
    float xs[4] = { x0, mid, left, SNTV_UI_W }, ax[4] = { 0.0f, 0.75f, 1.0f, 1.0f };
    float ys[5] = { 0, 150, bottom, bm, height }, ay[5] = { 0.55f, 1.0f, 1.0f, 0.8f, 0.0f };
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 4; j++) {
            float cx = xs[i], cw = xs[i + 1] - xs[i], cy = ys[j], ch = ys[j + 1] - ys[j];
            if (cw <= 0 || ch <= 0) continue;
            gfx_video_fade(cx, cy, cw, ch, t * ax[i] * ay[j], t * ax[i + 1] * ay[j], t * ax[i] * ay[j + 1], t * ax[i + 1] * ay[j + 1]);
        }
}

void ui_backdrop_fades(float left_to, float bottom_from, float height)
{
    (void)height;
    g_fade_left = left_to;
    g_fade_bottom = bottom_from;
}

void ui_backdrop(unsigned tex, int iw, int ih, float alpha, float left_to, float bottom_from)
{
    if (!tex) return;
    float h = SNTV_UI_H;
    ui_backdrop_fades(left_to, bottom_from, h);
    ui_backdrop_image(tex, iw, ih, alpha, h);
}

void ui_text_scrim(float width, float height)
{
    Color bg = THEME_BG;
    float mid = width * 0.55f;
    gfx_gradient_h(0, 0, mid, height, COLOR_ALPHA(bg, 0xC8), COLOR_ALPHA(bg, 0x78));
    gfx_gradient_h(mid, 0, width - mid, height, COLOR_ALPHA(bg, 0x78), COLOR_ALPHA(bg, 0));
}

void ui_backdrop_blur(unsigned tex, int iw, int ih, float alpha)
{
    if (!tex) return;
    float u0, v0, u1, v1;
    cover_uv(iw, ih, SNTV_UI_W, SNTV_UI_H, &u0, &v0, &u1, &v1);
    gfx_tex_blur(tex, 0, 0, SNTV_UI_W, SNTV_UI_H, u0, v0, u1, v1, RGBA(150, 150, 165, (int)(255 * alpha)), 5.0f);
    gfx_rect(0, 0, SNTV_UI_W, SNTV_UI_H, COLOR_ALPHA(THEME_BG, 0x90));
}

/* ---- theme language: focus, titles, rows ---- */

/* Under a focused control: Signal's hard offset block, Marquee's light bar, Glass's
 * soft white glow, nothing for Metro's flat tiles. `a` is the focus amount 0..1. */
void ui_focus_under(float x, float y, float w, float h, float r, float a)
{
    if (a <= 0.01f) return;
    switch (g_theme.focus_style) {
    case FOCUS_APERTURE:
        gfx_rect(x, y + h + 5, w < 96 ? w : 96, 4, COLOR_ALPHA(THEME_ACCENT, (int)(255 * a)));
        break;
    case FOCUS_FRAME:
        gfx_rrect(x + 10 * a, y + 10 * a, w, h, r, COLOR_ALPHA(THEME_ACCENT, (int)(0x66 * a)));
        break;
    case FOCUS_LIGHTBAR:
        gfx_rrect(x + w * 0.08f, y + h + 8, w * 0.84f, 4, 2, COLOR_ALPHA(THEME_ACCENT, (int)(255 * a)));
        if (!g_app.fx_light) gfx_gradient_v(x + w * 0.08f, y + h + 12, w * 0.84f, 26, COLOR_ALPHA(THEME_ACCENT, (int)(0x38 * a)), COLOR_ALPHA(THEME_ACCENT, 0));
        break;
    case FOCUS_OUTLINE:
        if (!g_app.fx_light) gfx_shadow(x, y + 10 * a, w, h, r, 28, RGBA(0, 0, 0, (int)(0x90 * a)));
        break;
    case FOCUS_GLASS:
        if (!g_app.fx_light) {
            gfx_shadow(x, y + 12 * a, w, h, r, 30, RGBA(0, 0, 0, (int)(0x80 * a)));
            gfx_shadow(x, y, w, h, r, 22, COLOR_ALPHA(RGBA(0xFF, 0xFF, 0xFF, 0xFF), (int)(0x30 * a)));
        }
        break;
    default:
        break;
    }
}

/* Over a focused card: the theme's outline. */
static void focus_over(float x, float y, float w, float h, float r, float a)
{
    if (a <= 0.01f) return;
    switch (g_theme.focus_style) {
    case FOCUS_APERTURE:
        gfx_rrect_ring(x - 2, y - 2, w + 4, h + 4, r + 1, 2.5f, COLOR_ALPHA(THEME_TEXT, (int)(0xF0 * a)));
        break;
    case FOCUS_FRAME: gfx_rrect_ring(x, y, w, h, r, 5, COLOR_ALPHA(THEME_ACCENT, (int)(255 * a))); break;
    case FOCUS_LIGHTBAR: gfx_rrect_ring(x, y, w, h, r, 2, COLOR_ALPHA(THEME_TEXT, (int)(0xC0 * a))); break;
    case FOCUS_MIST: gfx_rrect_ring(x, y, w, h, r, 3, COLOR_ALPHA(g_theme.id == THEME_SUNSET ? THEME_ACCENT : RGBA(0xFF, 0xFF, 0xFF, 0xFF), (int)(0xF0 * a))); break;   /* just a bright rim */
    case FOCUS_GLASS:   /* a bright rim and a sheen across the top */
        gfx_rrect_grad_v(x, y, w, h * 0.45f, r, COLOR_ALPHA(RGBA(0xFF, 0xFF, 0xFF, 0xFF), (int)(0x30 * a)), COLOR_ALPHA(RGBA(0xFF, 0xFF, 0xFF, 0xFF), 0));
        gfx_rrect_ring(x, y, w, h, r, 2.5f, COLOR_ALPHA(RGBA(0xFF, 0xFF, 0xFF, 0xFF), (int)(0xE0 * a)));
        break;
    case FOCUS_OUTLINE:  /* a white outline just outside the card */
        gfx_rrect_ring(x - 6, y - 6, w + 12, h + 12, r + 5, 4, COLOR_ALPHA(RGBA(0xFF, 0xFF, 0xFF, 0xFF), (int)(255 * a)));
        break;
    default:            /* Metro: a white border inside the tile */
        gfx_rrect_ring(x + 4, y + 4, w - 8, h - 8, 0, 4, COLOR_ALPHA(RGBA(0xFF, 0xFF, 0xFF, 0xFF), (int)(255 * a)));
        break;
    }
}

/* Capitals for the Latin-1 and Latin Extended-A letters the atlases carry. */
static int upper_cp(int c)
{
    if (c >= 'a' && c <= 'z') return c - 32;
    if (c >= 0xE0 && c <= 0xFE && c != 0xF7) return c - 32;
    if (c == 0xFF) return 0x178;
    if ((c >= 0x100 && c <= 0x137) || (c >= 0x14A && c <= 0x177)) return c & 1 ? c - 1 : c;
    if ((c >= 0x139 && c <= 0x148) || (c >= 0x179 && c <= 0x17E)) return c & 1 ? c : c - 1;
    return c;
}

void ui_upper(const char *in, char *out, size_t n)
{
    size_t o = 0;
    const unsigned char *p = (const unsigned char *)in;
    while (*p && o + 4 < n) {
        int c, len;
        if (p[0] < 0x80) { c = p[0]; len = 1; }
        else if ((p[0] & 0xE0) == 0xC0 && (p[1] & 0xC0) == 0x80) { c = ((p[0] & 0x1F) << 6) | (p[1] & 0x3F); len = 2; }
        else { /* copy longer sequences unchanged */
            len = (p[0] & 0xF0) == 0xE0 ? 3 : (p[0] & 0xF8) == 0xF0 ? 4 : 1;
            for (int i = 0; i < len && p[i]; i++) out[o++] = (char)p[i];
            p += len;
            continue;
        }
        c = upper_cp(c);
        if (c < 0x80) out[o++] = (char)c;
        else { out[o++] = (char)(0xC0 | (c >> 6)); out[o++] = (char)(0x80 | (c & 0x3F)); }
        p += len;
    }
    out[o] = 0;
}

void ui_lower(const char *in, char *out, size_t n)
{
    size_t o = 0;
    const unsigned char *p = (const unsigned char *)in;
    while (*p && o + 4 < n) {   /* ASCII and Latin-1 capitals; anything else is copied */
        if (p[0] >= 'A' && p[0] <= 'Z') { out[o++] = (char)(p[0] + 32); p++; continue; }
        if (p[0] == 0xC3 && p[1] >= 0x80 && p[1] <= 0x9E && p[1] != 0x97) { out[o++] = (char)0xC3; out[o++] = (char)(p[1] + 32); p += 2; continue; }
        out[o++] = (char)*p++;
    }
    out[o] = 0;
}

/* The face, case and spacing a title takes in this theme. */
static int title_font(int size)
{
    if ((g_theme.id == THEME_MARQUEE || g_theme.id == THEME_METRO) && (size & 0xFFF) >= FONT_L) return (size & 0xFFF) | FONT_ALT;
    return size;
}

static float title_track(int size) { return g_theme.upper_titles ? g_theme.title_track * (float)(size & 0xFFF) / FONT_M : 0; }

float ui_title_width(const char *text, int size)
{
    if (!text) return 0;
    if (!g_theme.upper_titles) return font_measure(text, size);
    char up[512];
    ui_upper(text, up, sizeof up);
    return font_measure_tracked(up, title_font(size), title_track(size));
}

float ui_title(const char *text, float x, float y, int size, Color c, float max_w)
{
    if (!text) return 0;
    if (!g_theme.upper_titles) {
        if (max_w > 0) { font_draw_fit(text, x, y, size, c, max_w); return font_measure(text, size) < max_w ? font_measure(text, size) : max_w; }
        font_draw(text, x, y, size, c);
        return font_measure(text, size);
    }
    char up[512];
    ui_upper(text, up, sizeof up);
    int f = title_font(size);
    float tr = title_track(size), w = font_measure_tracked(up, f, tr);
    /* a display face is taller for its size: sit it on the same baseline */
    float dy = (f & FONT_ALT) ? font_ascent(size) - font_ascent(f) : 0;
    if (max_w > 0 && w > max_w) { font_draw_fit(up, x, y + dy, f, c, max_w); return max_w; }
    font_draw_tracked(up, x, y + dy, f, c, tr);
    return w;
}

/* A big title (hero, detail): the theme's case and face, up to two lines, the last
 * one ending at `bottom`. Returns the number of lines. */
int ui_hero_title(const char *text, float x, float bottom, float maxw, Color c)
{
    if (!text) return 0;
    char up[512];
    if (g_theme.upper_titles) ui_upper(text, up, sizeof up); else snprintf(up, sizeof up, "%s", text);
    int big = title_font((g_theme.id == THEME_APERTURE ? FONT_HUGE : FONT_TITLE) | FONT_B), small = title_font(FONT_XL | FONT_B);
    int fs = font_measure(up, big) > maxw ? small : big;
    int lines = font_draw_wrapped(up, 0, 0, fs, 0, maxw, 2);
    float y = bottom - lines * font_height(fs);
    font_draw_wrapped(up, x + 3, y + 3, fs, RGBA(0, 0, 0, COLOR_A(c) / 2), maxw, 2);
    font_draw_wrapped(up, x, y, fs, c, maxw, 2);
    return lines;
}

/* "a / b / c" in mono capitals (Signal), "a · b · c" otherwise. Returns the x after it. */
float ui_meta_line(const char *const *parts, int n, float x, float y, Color c)
{
    int mono = g_theme.mono_meta;
    int fs = mono ? (FONT_S | FONT_ALT) : (FONT_S | FONT_B);
    for (int i = 0, k = 0; i < n; i++) {
        if (!parts[i] || !*parts[i]) continue;
        if (k++) {
            if (mono) { font_draw("/", x + 10, y, fs, THEME_TEXT_MUTED); x += 10 + font_measure("/", fs) + 10; }
            else { gfx_circle(x + 12, y + font_height(fs) * 0.52f, 3, THEME_TEXT_MUTED); x += 24; }
        }
        char up[256];
        if (mono) ui_upper(parts[i], up, sizeof up); else snprintf(up, sizeof up, "%s", parts[i]);
        font_draw(up, x, y, fs, c);
        x += font_measure(up, fs);
    }
    return x;
}

/* A row's heading: "01 ── TITLE ──── 24 ›" (Signal), an amber kicker over the title
 * (Marquee), big light lowercase (Metro) or the plain title (Glass). `k` is the row's
 * emphasis 0..1. */
void ui_row_header(int index, const char *title, const char *source, int count, float x, float y, float right, float k)
{
    Color fg = COLOR_ALPHA(THEME_TEXT, (int)(255 * (0.55f + 0.45f * k)));
    switch (g_theme.row_header) {
    case ROWHEAD_NUMBERED: {
        char num[12], cnt[16];
        snprintf(num, sizeof num, "%02d", index + 1);
        int mf = FONT_S | FONT_ALT;
        float my = y + (font_height(FONT_M | FONT_B) - font_height(mf)) / 2;
        font_draw(num, x, my, mf, k > 0.5f ? THEME_ACCENT : THEME_TEXT_MUTED);
        float tx = x + font_measure(num, mf) + 18;
        gfx_rect(tx, y + font_height(FONT_M | FONT_B) / 2, 28, 2, THEME_LINE);
        tx += 44;
        float tw = ui_title(title, tx, y, FONT_M | FONT_B, fg, right - tx - 200);
        float lx = tx + tw + 18, rx = right;
        if (count > 0) {
            snprintf(cnt, sizeof cnt, "%d", count);
            float cwid = font_measure(cnt, mf);
            font_draw(cnt, right - cwid, my, mf, THEME_TEXT_MUTED);
            rx = right - cwid - 18;
        }
        if (k > 0.5f && source && *source) {
            char up[160];
            ui_upper(source, up, sizeof up);
            float sw = font_measure(up, FONT_XS | FONT_ALT);
            if (lx + sw + 40 < rx) { font_draw(up, rx - sw, my + 2, FONT_XS | FONT_ALT, THEME_TEXT_MUTED); rx -= sw + 18; }
        }
        if (rx > lx) gfx_rect(lx, y + font_height(FONT_M | FONT_B) / 2, rx - lx, 2, THEME_LINE);
        break;
    }
    case ROWHEAD_KICKER:
        if (source && *source) {
            char up[160];
            ui_upper(source, up, sizeof up);
            font_draw_tracked(up, x, y - 22, FONT_XS | FONT_B, COLOR_ALPHA(THEME_ACCENT, (int)(255 * (0.6f + 0.4f * k))), 2);
        }
        ui_title(title, x, y, FONT_M | FONT_B, fg, right - x);
        break;
    case ROWHEAD_METRO: {
        char low[200];
        ui_lower(title, low, sizeof low);
        int f = FONT_L | FONT_ALT;
        float dy = font_height(FONT_M | FONT_B) - font_height(f);
        font_draw_fit(low, x - 2, y + dy, f, k > 0.5f ? THEME_TEXT : THEME_TEXT_MUTED, right - x - 120);
        if (k > 0.5f && count > 0) {
            char cnt[16]; snprintf(cnt, sizeof cnt, "%d", count);
            font_draw(cnt, x + font_measure(low, f) + 16, y + dy + font_ascent(f) - font_ascent(FONT_S), FONT_S, THEME_ACCENT);
        }
        break;
    }
    default: {
        float tw = font_measure(title, FONT_M | FONT_B);
        font_draw_fit(title, x, y, FONT_M | FONT_B, fg, right - x - 120);
        if (k > 0.5f && source && *source) font_draw(source, x + tw + 18, y + 6, FONT_S, THEME_TEXT_MUTED);
        break;
    }
    }
}

/* ---- hold OK for options ---- */

int ui_hold_hint(float x, float y, float w)
{
    float k = g_app.ok_hold;
    if (k <= 0) return 0;
    int armed = k >= 1;
    const char *label = armed ? "Release for options" : "Hold for options";
    char up[64];
    if (g_theme.upper_titles) { ui_upper(label, up, sizeof up); label = up; }
    /* the label slides up a touch and brightens as the hold fills */
    float a = k < 0.25f ? k / 0.25f : 1;
    font_draw_fit(label, x, y + (1 - a) * 8, FONT_XS | FONT_B, COLOR_ALPHA(armed ? THEME_ACCENT : THEME_TEXT, (int)(255 * a)), w);
    float by = y + font_height(FONT_XS | FONT_B) + 6, bw = w < 240 ? w : 240;
    gfx_rrect(x, by, bw, 4, theme_pill_r(4), COLOR_ALPHA(THEME_TEXT, 0x30));
    gfx_rrect(x, by, bw * (k > 1 ? 1 : k), 4, theme_pill_r(4), THEME_ACCENT);
    if (armed && !g_app.fx_light) {   /* a soft beat while it waits for the release */
        float p = 0.5f + 0.5f * sinf(g_app.time * 9);
        gfx_rrect(x - 2, by - 2, bw + 4, 8, theme_pill_r(8), COLOR_ALPHA(THEME_ACCENT, (int)(0x50 * p)));
    }
    return 1;
}

/* ---- details on a focused card ---- */

void ui_peek_step(Peek *p, const MetaPreview *m, float dt)
{
    if (g_theme.id == THEME_APERTURE || !g_app.card_details || !m || !m->id) { p->id[0] = 0; p->t = 0; return; }
    if (strcmp(p->id, m->id) != 0) {
        snprintf(p->id, sizeof p->id, "%s", m->id);
        p->since = g_app.time;
        p->t = 0;
        p->asked = 0;
    }
    if (g_app.time - p->since < PEEK_DWELL) { app_redraw(); return; }   /* a frame lands at the dwell */
    if (!p->asked) { app_details_want(m->type, m->id); p->asked = 1; }
    if (p->t >= 1) return;
    p->t = g_app.fx_light ? 1 : p->t + (1 - p->t) * (dt * 9 > 1 ? 1 : dt * 9);
    if (p->t > 0.995f) p->t = 1;
    app_redraw();
}

/* The panel's full width and side for a card drawn at (cx, cw) */
static float peek_geom(float cw, float ch, float cx, float min_x, float max_x, float *gap, int *right)
{
    float pw = ch * 1.7f;
    if (pw < 440) pw = 440;
    if (pw > 620) pw = 620;
    *gap = g_theme.focus_style == FOCUS_FRAME ? 18 : 12;
    *right = cx + cw + *gap + pw <= max_x || cx - *gap - pw < min_x;
    (void)cw;
    return pw;
}

float ui_peek_room(const Peek *p, const MetaPreview *m, float cx, float cw, float ch, float min_x, float max_x, int *right)
{
    *right = 1;
    if (p->t <= 0.01f || !m || strcmp(p->id, m->id ? m->id : "") != 0) return 0;
    float gap, pw = peek_geom(cw, ch, cx, min_x, max_x, &gap, right);
    float e = 1 - (1 - p->t) * (1 - p->t) * (1 - p->t);
    return (gap + pw) * e;
}

void ui_peek_draw(const Peek *p, const MetaPreview *m, float cx, float cy, float cw, float ch, float min_x, float max_x)
{
    if (p->t <= 0.01f || !m || strcmp(p->id, m->id ? m->id : "") != 0) return;
    const MetaPreview *d = app_details_cached(m->type, m->id);
    #define PICK(f) (d && d->f && *d->f ? d->f : m->f)
    const char *name = PICK(name), *year = PICK(release_info), *runtime = PICK(runtime), *rating = PICK(imdb_rating), *desc = PICK(description);
    #undef PICK
    const MetaPreview *g = d && d->genres.n ? d : m;

    float e = 1 - (1 - p->t) * (1 - p->t) * (1 - p->t);
    float gap; int right;
    float pw = peek_geom(cw, ch, cx, min_x, max_x, &gap, &right);
    float w = pw * e, x = right ? cx + cw + gap : cx - gap - w, y = cy, h = ch;

    /* the panel, then its edge in the theme's language */
    if (g_theme.focus_style == FOCUS_GLASS && !g_app.fx_light) gfx_shadow(x, y + 8, w, h, R_PANEL, 24, RGBA(0, 0, 0, (int)(0x90 * e)));
    ui_panel(x, y, w, h, R_PANEL, COLOR_ALPHA(THEME_PANEL, (int)(0xFF * e)));
    if (g_theme.focus_style == FOCUS_FRAME) gfx_rect(right ? x : x + w - 4, y, 4, h, COLOR_ALPHA(THEME_ACCENT, (int)(255 * e)));
    else if (g_theme.focus_style == FOCUS_LIGHTBAR) gfx_rect(x + R_PANEL, y, w - 2 * R_PANEL > 0 ? w - 2 * R_PANEL : 0, 3, COLOR_ALPHA(THEME_ACCENT, (int)(255 * e)));
    else gfx_rrect_ring(x, y, w, h, R_PANEL, 1.5f, COLOR_ALPHA(THEME_LINE, (int)(255 * e)));

    /* text slides in with the panel and is cut at its edge */
    int a = (int)(255 * (e < 0.4f ? 0 : (e - 0.4f) / 0.6f));
    if (a <= 0) return;
    float pad = 28, tx = (right ? x : x + w - pw) + pad, tw = pw - 2 * pad, ty = y + 26;
    gfx_clip_push(x, y, w, h);
    ui_title(name, tx, ty, FONT_M | FONT_B, COLOR_ALPHA(THEME_TEXT, a), tw);
    ty += font_height(FONT_M | FONT_B) + 10;

    float mx = tx;
    if (rating && *rating) mx += ui_imdb(rating, mx, ty) + 14;
    const char *type = m->type ? (strcmp(m->type, "series") == 0 ? "Series" : strcmp(m->type, "movie") == 0 ? "Movie" : m->type) : NULL;
    const char *parts[3] = { year, runtime, type };
    ui_meta_line(parts, 3, mx, ty + (36 - font_height(FONT_S | FONT_B)) / 2, COLOR_ALPHA(THEME_TEXT_DIM, a));
    ty += 50;

    if (g->genres.n) {
        char gl[200] = "";
        for (int i = 0; i < g->genres.n && i < 3; i++) {
            if (i) strncat(gl, "  \xC2\xB7  ", sizeof gl - strlen(gl) - 1);
            strncat(gl, g->genres.v[i], sizeof gl - strlen(gl) - 1);
        }
        if (g_theme.upper_titles) { char up[200]; ui_upper(gl, up, sizeof up); snprintf(gl, sizeof gl, "%s", up); }
        font_draw_fit(gl, tx, ty, FONT_XS | FONT_B, COLOR_ALPHA(THEME_ACCENT, a), tw);
        ty += 38;
    }

    /* the library line sits at the bottom; the synopsis fills what is left */
    const LibraryItem *li = app_library_find(m->id);
    float foot = y + h - 22 - font_height(FONT_XS | FONT_B);
    if (desc && *desc) {
        float lh = font_height(FONT_S);
        int lines = (int)((foot - 16 - ty) / lh);
        if (lines > 0) font_draw_wrapped(desc, tx, ty, FONT_S, COLOR_ALPHA(THEME_TEXT_DIM, a), tw, lines);
    } else if (!d && p->asked) {
        ui_skeleton(tx, ty + 6, tw, 18, 4);
        ui_skeleton(tx, ty + 40, tw * 0.7f, 18, 4);
    }
    const char *state = NULL;
    float frac = -1;
    if (li && li->duration && li->time_offset > 0 && !li->flagged_watched) frac = (float)li->time_offset / (float)li->duration;
    if (li && li->flagged_watched) state = "Watched";
    else if (frac >= 0) state = "Continue watching";
    else if (li && !li->removed) state = "In your Library";
    if (state) {
        font_draw(state, tx, foot, FONT_XS | FONT_B, COLOR_ALPHA(li->flagged_watched ? THEME_OK : THEME_TEXT, a));
        if (frac >= 0) {
            float sx = tx + font_measure(state, FONT_XS | FONT_B) + 16;
            ui_progress(sx, foot + font_height(FONT_XS | FONT_B) / 2 - 3, tx + tw - sx - 110 > 40 ? tx + tw - sx - 110 : 40, 6, frac, THEME_ACCENT);
        }
    }
    const char *hint = "OK  open";
    font_draw(hint, tx + tw - font_measure(hint, FONT_XS), foot, FONT_XS, COLOR_ALPHA(THEME_TEXT_MUTED, a));
    gfx_clip_pop();
}

/* ---- pills, chips, toggles ---- */

float ui_pill_width(const char *label, int has_icon, float h)
{
    int fs = h >= 72 ? (FONT_M | FONT_B) : (FONT_S | FONT_B);
    float w = h * 0.9f + (label ? font_measure(label, fs) : 0);
    if (has_icon) w += h * 0.45f + (label && *label ? 12 : 0);
    if (!label || !*label) w = h;
    return w;
}

float ui_pill(const char *label, int has_icon, Icon icon, float x, float y, float h, PillStyle style, int focused)
{
    int fs = h >= 72 ? (FONT_M | FONT_B) : (FONT_S | FONT_B);
    float w = ui_pill_width(label, has_icon, h);
    Color bg, fg;
    float r = theme_pill_r(h);
    if (focused) { bg = THEME_FOCUS; fg = THEME_ON_FOCUS; }
    else if (style == PILL_PRIMARY) { bg = THEME_SURFACE_HI; fg = THEME_TEXT; }
    else if (style == PILL_SECONDARY) { bg = THEME_SURFACE; fg = THEME_TEXT; }
    else { bg = RGBA(0, 0, 0, 0); fg = THEME_TEXT_DIM; }
    if (focused) ui_focus_under(x, y, w, h, r, 1);
    if (!focused && style == PILL_SECONDARY && g_theme.focus_style == FOCUS_FRAME) gfx_rrect_ring(x + 1.5f, y + 1.5f, w - 3, h - 3, r, 1.5f, THEME_LINE);
    else if (style != PILL_GHOST || focused) gfx_rrect(x, y, w, h, r, bg);
    float cx = (!label || !*label) ? x + w / 2 : x + h * 0.45f;
    if (has_icon) {
        float isz = h * 0.44f;
        ui_icon(icon, (!label || !*label) ? cx : cx + isz / 2, y + h / 2, isz, fg);
        cx += isz + 12;
    }
    if (label && *label) font_draw(label, cx, y + (h - font_height(fs)) / 2, fs, fg);
    return w;
}

float ui_icon_button(Icon icon, float cx, float cy, float r, int focused)
{
    float rr = theme_pill_r(2 * r);
    if (focused) ui_focus_under(cx - r, cy - r, 2 * r, 2 * r, rr, 1);
    gfx_rrect(cx - r, cy - r, 2 * r, 2 * r, rr, focused ? THEME_FOCUS : THEME_SURFACE);
    ui_icon(icon, cx, cy, r * 0.95f, focused ? THEME_ON_FOCUS : THEME_TEXT);
    return 2 * r;
}

void ui_clock_text(double from_now, char *out, size_t n)
{
    time_t t = time(NULL) + (time_t)(from_now > 0 ? from_now : 0);
    struct tm tmv;
    if (!localtime_r(&t, &tmv)) { snprintf(out, n, "--:--"); return; }
    if (g_app.clock_mode == 1) snprintf(out, n, "%02d:%02d", tmv.tm_hour, tmv.tm_min);
    else snprintf(out, n, "%d:%02d %s", tmv.tm_hour % 12 ? tmv.tm_hour % 12 : 12, tmv.tm_min, tmv.tm_hour < 12 ? "AM" : "PM");
}

/* The clock island's text in its style: "6:51 PM", "Mon 6:51 PM", "6:51:09 PM". */
void ui_island_text(char *out, size_t n)
{
    time_t t = time(NULL);
    struct tm tmv;
    if (!localtime_r(&t, &tmv)) { snprintf(out, n, "--:--"); return; }
    int h = g_app.clock_mode == 1 ? tmv.tm_hour : (tmv.tm_hour % 12 ? tmv.tm_hour % 12 : 12);
    const char *ap = g_app.clock_mode == 1 ? "" : tmv.tm_hour < 12 ? " AM" : " PM";
    char day[8] = "";
    if (g_app.island_style == ISLAND_DAY) strftime(day, sizeof day, "%a ", &tmv);
    if (g_app.island_style == ISLAND_SECONDS) snprintf(out, n, g_app.clock_mode == 1 ? "%02d:%02d:%02d%s" : "%d:%02d:%02d%s", h, tmv.tm_min, tmv.tm_sec, ap);
    else snprintf(out, n, g_app.clock_mode == 1 ? "%s%02d:%02d%s" : "%s%d:%02d%s", day, h, tmv.tm_min, ap);
}

/* A small dial for the island's Small dial style. */
void ui_mini_dial(float cx, float cy, float r, Color c)
{
    time_t t = time(NULL);
    struct tm tmv;
    if (!localtime_r(&t, &tmv)) return;
    gfx_rrect_ring(cx - r, cy - r, 2 * r, 2 * r, r, 2, c);
    float m = tmv.tm_min / 60.0f * 6.2831853f, h = ((tmv.tm_hour % 12) + tmv.tm_min / 60.0f) / 12 * 6.2831853f;
    gfx_line(cx, cy, cx + sinf(h) * r * 0.5f, cy - cosf(h) * r * 0.5f, 3, c);
    gfx_line(cx, cy, cx + sinf(m) * r * 0.78f, cy - cosf(m) * r * 0.78f, 2, THEME_ACCENT);
}

float ui_clock_width(void)
{
    if (g_app.clock_mode == 2) return 0;
    char now[32];
    ui_island_text(now, sizeof now);
    return font_measure(now, FONT_S | FONT_B) + 44 + (g_app.island_style == ISLAND_DIAL ? 34 : 0);
}

float ui_chip(const char *text, float x, float y, Color bg, Color fg)
{
    int fs = FONT_XS | FONT_B;
    float w = font_measure(text, fs) + 24, h = 36;
    gfx_rrect(x, y, w, h, g_theme.r_key < 9 ? g_theme.r_key / 2 : 9, bg);
    font_draw(text, x + 12, y + (h - font_height(fs)) / 2, fs, fg);
    return w;
}

void ui_portrait(const char *photo, const char *name, float x, float y, float w, float h, float r, int initials_font)
{
    int iw = 0, ih = 0;
    unsigned tex = photo ? image_get(photo, &iw, &ih) : 0;
    if (tex && iw > 0 && ih > 0) {
        float ia = (float)iw / ih, ta = w / h, u0 = 0, v0 = 0, u1 = 1, v1 = 1;
        if (ia > ta) { u0 = (1 - ta / ia) / 2; u1 = 1 - u0; } else { v0 = (1 - ia / ta) / 3; v1 = v0 + ia / ta; }
        gfx_rtex_uv(tex, x, y, w, h, r, u0, v0, u1, v1, RGBA(255, 255, 255, 255), 0);
        return;
    }
    gfx_rrect(x, y, w, h, r, COLOR_ALPHA(THEME_TEXT, 0x1C));
    char ini[3] = { name && *name ? name[0] : '?', 0, 0 };
    const char *sp = name ? strrchr(name, ' ') : NULL;
    if (sp && sp[1]) ini[1] = sp[1];
    font_draw_center(ini, x + w / 2, y + h / 2 - font_height(initials_font) / 2, initials_font, THEME_TEXT_DIM);
}

void ui_new_episodes_badge(int count, float right_x, float y)
{
    char text[32];
    if (count > 1) snprintf(text, sizeof text, "%d new episodes", count);
    else snprintf(text, sizeof text, "New episode");
    int fs = FONT_XS | FONT_B;
    float h = 40, w = 14 + 24 + 8 + font_measure(text, fs) + 16, x = right_x - w;
    gfx_rrect(x, y, w, h, theme_pill_r(h), THEME_ACCENT);
    ui_icon(ICON_BELL, x + 14 + 12, y + h / 2, 22, THEME_ON_ACCENT);
    font_draw(text, x + 14 + 24 + 8, y + (h - font_height(fs)) / 2, fs, THEME_ON_ACCENT);
}

void ui_new_episodes_dot(int count, float right_x, float y)
{
    char n[8]; snprintf(n, sizeof n, "%d", count > 99 ? 99 : count);
    int fs = FONT_XS | FONT_B;
    float h = 40, w = 12 + 22 + 6 + font_measure(n, fs) + 14, x = right_x - w;
    gfx_rrect(x, y, w, h, theme_pill_r(h), THEME_ACCENT);
    ui_icon(ICON_BELL, x + 12 + 11, y + h / 2, 22, THEME_ON_ACCENT);
    font_draw(n, x + 12 + 22 + 6, y + (h - font_height(fs)) / 2, fs, THEME_ON_ACCENT);
}

float ui_chip_outline(const char *text, float x, float y, Color line, Color fg)
{
    int fs = FONT_XS;
    float w = font_measure(text, fs) + 28, h = 36;
    gfx_rrect_ring(x + 1.5f, y + 1.5f, w - 3, h - 3, theme_pill_r(h - 3), 1.5f, line);
    font_draw(text, x + 14, y + (h - font_height(fs)) / 2, fs, fg);
    return w;
}

float ui_imdb(const char *rating, float x, float y)
{
    int fs = FONT_XS | FONT_B;
    float lw = font_measure("IMDb", fs) + 18;
    float rw = font_measure(rating, FONT_S | FONT_B);
    gfx_rrect(x, y, lw, 36, 8, THEME_IMDB);
    font_draw("IMDb", x + 9, y + (36 - font_height(fs)) / 2, fs, RGBA(0, 0, 0, 0xFF));
    font_draw(rating, x + lw + 10, y + (36 - font_height(FONT_S | FONT_B)) / 2, FONT_S | FONT_B, THEME_TEXT);
    return lw + 10 + rw;
}

float ui_dot_sep(float x, float y, Color c)
{
    gfx_circle(x + 12, y + 18, 3, c);
    return x + 24;
}

const char *const SUBS_STYLE_NAMES[SUBS_STYLE_COUNT] = { "Soft shadow", "Bold outline", "Classic yellow", "Black box" };

int ui_subtitle_font(int size_setting)
{
    static const int F[] = { FONT_M, FONT_L, FONT_XL, FONT_TITLE };
    return F[size_setting < 0 ? 0 : size_setting > 3 ? 3 : size_setting] | FONT_B;
}

void ui_subtitle(const char *text, float bottom, int size_setting, int style)
{
    if (!text || !*text) return;
    int size = ui_subtitle_font(size_setting);
    float max_w = SNTV_UI_W * 0.78f, lh = font_height(size) * 1.04f;
    int lines = font_draw_wrapped(text, 0, 0, size, 0, max_w, 3);
    float y = bottom - lines * lh;
    Color fg = style == SUBS_STYLE_YELLOW ? RGBA(0xFF, 0xE1, 0x3C, 0xFF) : RGBA(255, 255, 255, 255);
    char tmp[512];
    snprintf(tmp, sizeof tmp, "%s", text);
    int i = 0;
    for (char *l = strtok(tmp, "\n"); l && i < lines; l = strtok(NULL, "\n"), i++) {
        float lw = font_measure(l, size);
        if (lw > max_w) lw = max_w;
        float lx = (SNTV_UI_W - lw) / 2, ly = y + i * lh;
        switch (style) {
        case SUBS_STYLE_BOXED:
            gfx_rrect(lx - 18, ly - 2, lw + 36, lh + 4, 8, RGBA(0, 0, 0, 0xB8));
            break;
        case SUBS_STYLE_OUTLINE:
        case SUBS_STYLE_YELLOW:
            /* a thick ring of dark copies: holds up on bright, busy pictures */
            for (int dx = -3; dx <= 3; dx += 3) for (int dy = -3; dy <= 3; dy += 3)
                if (dx || dy) font_draw_fit(l, lx + dx, ly + dy, size, RGBA(0, 0, 0, 0xE0), max_w);
            font_draw_fit(l, lx, ly + 4, size, RGBA(0, 0, 0, 0xE0), max_w);
            break;
        default:
            /* soft shadow + thin outline: readable on any picture without a box */
            font_draw_fit(l, lx + 3, ly + 3, size, RGBA(0, 0, 0, 0xB0), max_w);
            for (int dx = -2; dx <= 2; dx += 2) for (int dy = -2; dy <= 2; dy += 2)
                if (dx || dy) font_draw_fit(l, lx + dx, ly + dy, size, RGBA(0, 0, 0, 0x90), max_w);
            break;
        }
        font_draw_fit(l, lx, ly, size, fg, max_w);
    }
}

void ui_toggle(float x, float y, int on, int focused)
{
    /* focused: drawn on a focused row, so in the row's own fill and text colours */
    float w = 76, h = 40, r = theme_pill_r(h);
    Color track = focused ? (on ? THEME_ON_FOCUS : COLOR_ALPHA(THEME_ON_FOCUS, 0x30)) : on ? THEME_ACCENT : THEME_SURFACE_HI;
    Color knob = focused ? (on ? THEME_FOCUS : THEME_ON_FOCUS) : on ? THEME_ON_ACCENT : THEME_TEXT_DIM;
    if (on && !focused && THEME_ACCENT2 != THEME_ACCENT) gfx_rrect_grad_h(x, y, w, h, r, THEME_ACCENT, THEME_ACCENT2);
    else gfx_rrect(x, y, w, h, r, track);
    if (!on && !focused) gfx_rrect_ring(x, y, w, h, r, 2, THEME_LINE);
    float k = h / 2 - 6, kx = on ? x + w - h / 2 : x + h / 2;
    gfx_rrect(kx - k, y + h / 2 - k, 2 * k, 2 * k, theme_pill_r(2 * k) * 0.8f, knob);
}

void ui_progress(float x, float y, float w, float h, float frac, Color fill)
{
    if (frac < 0) frac = 0;
    if (frac > 1) frac = 1;
    float r = theme_pill_r(h);
    gfx_rrect(x, y, w, h, r, COLOR_ALPHA(THEME_TEXT, 0x30));
    if (frac > 0) {
        float fw = w * frac;
        if (fw < h) fw = h;
        if (fill == THEME_ACCENT && THEME_ACCENT2 != THEME_ACCENT) gfx_rrect_grad_h(x, y, fw, h, r, THEME_ACCENT, THEME_ACCENT2);
        else gfx_rrect(x, y, fw, h, r, fill);
    }
}

void ui_skeleton(float x, float y, float w, float h, float r)
{
    gfx_rrect(x, y, w, h, r, THEME_POSTER_PH);
    if (g_app.fx_light) return;
    /* a soft highlight sweeping left to right */
    float period = 1.6f, t = fmodf(g_app.time + x * 0.0006f, period) / period;
    float sw = w * 0.6f, sx = x - sw + (w + sw) * t;
    gfx_clip_push(x, y, w, h);
    gfx_gradient_h(sx, y, sw / 2, h, COLOR_ALPHA(THEME_TEXT, 0), COLOR_ALPHA(THEME_TEXT, 0x14));
    gfx_gradient_h(sx + sw / 2, y, sw / 2, h, COLOR_ALPHA(THEME_TEXT, 0x14), COLOR_ALPHA(THEME_TEXT, 0));
    gfx_clip_pop();
    app_redraw();
}

void ui_arc(float cx, float cy, float r, float thickness, float from, float frac, Color c)
{
    if (frac <= 0) return;
    if (frac > 1) frac = 1;
    int n = (int)(48 * frac) + 1;
    float a0 = (from - 0.25f) * 6.2832f, span = frac * 6.2832f;
    float px = cx + cosf(a0) * r, py = cy + sinf(a0) * r;
    for (int i = 1; i <= n; i++) {
        float a = a0 + span * i / n, x = cx + cosf(a) * r, y = cy + sinf(a) * r;
        gfx_line(px, py, x, y, thickness, c);
        px = x; py = y;
    }
}

void ui_spinner(float cx, float cy, float r)
{
    int n = 12;
    float head = g_app.time * 9.0f;
    for (int i = 0; i < n; i++) {
        float a = i * 6.2832f / n;
        float age = fmodf(head - i + n * 100, (float)n) / n;   /* 0 = newest */
        int alpha = (int)(255 * (1.0f - age));
        Color c = color_lerp(THEME_ACCENT2, THEME_ACCENT, age);
        gfx_circle(cx + cosf(a) * r, cy + sinf(a) * r, r * 0.13f + (1 - age) * r * 0.05f, COLOR_ALPHA(c, alpha < 30 ? 30 : alpha));
    }
    app_redraw();
}

/* The Stremio logo stays exactly as it was in every theme: its own purple gradient
 * tile and play triangle, never recoloured by the accent. */
#define LOGO_TOP    RGBA(0xD9, 0x46, 0xEF, 0xFF)
#define LOGO_BOTTOM RGBA(0x8B, 0x5C, 0xF6, 0xFF)
#define LOGO_GLYPH  RGBA(0xF7, 0xF7, 0xFB, 0xFF)

void ui_mark(float x, float y, float s)
{
    gfx_rrect_grad_v(x, y, s, s, s * 0.28f, LOGO_TOP, LOGO_BOTTOM);
    gfx_play_triangle(x + s * 0.36f, y + s * 0.28f, s * 0.38f, s * 0.44f, LOGO_GLYPH);
}

void ui_brand(float x, float y, float s)
{
    ui_mark(x, y, s);
    font_draw("Stremio", x + s + 16, y + (s - font_height(FONT_L | FONT_B)) / 2, FONT_L | FONT_B, THEME_TEXT);
}

/* ---- cards ---- */

/* How much a focused card grows and rises: Signal barely moves (the frame says it),
 * Marquee lifts toward its light bar, Glass swells most, Metro barely. */
float ui_focus_scale(float focus)
{
    float k = g_theme.focus_style == FOCUS_APERTURE ? 0.015f : g_theme.focus_style == FOCUS_GLASS || g_theme.focus_style == FOCUS_OUTLINE ? 0.08f : g_theme.focus_style == FOCUS_MIST ? 0.05f : g_theme.focus_style == FOCUS_LIGHTBAR ? 0.05f : 0.04f;
    float s = 1.0f + k * focus, t = g_app.time - g_app.press_at;
    if (focus > 0.5f && t >= 0 && t < 0.2f && !g_app.fx_light) {   /* OK pressed: a quick dip and back */
        s *= 1.0f - 0.05f * sinf(3.14159f * t / 0.2f);
        app_redraw();
    }
    return s;
}

void ui_aperture_art(const char *url, float x, float y, float w, float h, float alpha)
{
    int iw = 0, ih = 0;
    unsigned tex = url && *url ? image_get(url, &iw, &ih) : 0;
    gfx_rect(x, y, w, h, THEME_BG2);
    if (tex && iw > 0 && ih > 0) {
        float u0, v0, u1, v1;
        cover_uv(iw, ih, w, h, &u0, &v0, &u1, &v1);
        gfx_rtex_uv(tex, x, y, w, h, 2, u0, v0, u1, v1, RGBA(255, 255, 255, (int)(255 * alpha)), 0);
    }
}

static int is_ascii(const char *s) { for (; *s; s++) if ((unsigned char)*s >= 0x80) return 0; return 1; }

/* The words from *p that fit in maxw at fs, into out; advances *p past them. */
static int take_line(const char **p, int fs, float maxw, char *out, size_t n)
{
    const char *s = *p;
    while (*s == ' ') s++;
    size_t len = 0;
    const char *end = s;
    while (*end) {
        const char *w = end;
        while (*w && *w != ' ') w++;
        size_t l = (size_t)(w - s);
        if (l >= n) break;
        memcpy(out, s, l); out[l] = 0;
        if (len && font_measure(out, fs) > maxw) break;
        len = l; end = w;
        while (*end == ' ') end++;
    }
    if (!len) return 0;
    out[len] = 0;
    *p = end;
    return 1;
}

float ui_serif_title(const char *text, float x, float bottom, float maxw, int size, int lines, Color c)
{
    if (!text || !*text) return bottom;
    char up[512];
    ui_upper(text, up, sizeof up);
    static const int TRY[] = { FONT_HERO, FONT_HUGE, FONT_TITLE, FONT_XL, FONT_L };
    char row[4][256]; int n = 0, fs = FONT_L | FONT_ALT;
    for (int t = 0; t < 5; t++) {
        if (TRY[t] > size || (TRY[t] >= FONT_HERO && !is_ascii(up))) continue;   /* the hero size has ASCII only */
        fs = TRY[t] | FONT_ALT;
        const char *p = up;
        n = 0;
        while (n < 4 && take_line(&p, fs, maxw, row[n], sizeof row[n])) n++;
        if (!*p && n <= lines) break;
    }
    if (n > lines) n = lines;
    float lh = (fs & 0xFFF) * 0.9f, top = bottom - n * lh, dy = font_ascent(fs) - (fs & 0xFFF) * 0.74f;
    for (int i = 0; i < n; i++) {
        if (i == n - 1 && n == lines) font_draw_fit(row[i], x, top + i * lh - dy, fs, c, maxw);
        else font_draw(row[i], x, top + i * lh - dy, fs, c);
    }
    return top;
}

float ui_spaced_caps(const char *text, float x, float y, int size, float track, Color c, float max_w)
{
    if (!text) return 0;
    char up[256];
    ui_upper(text, up, sizeof up);
    float w = font_measure_tracked(up, size, track);
    if (max_w > 0 && w > max_w) { font_draw_fit(up, x, y, size, c, max_w); return max_w; }
    font_draw_tracked(up, x, y, size, c, track);
    return w;
}

float ui_slash_line(const char *const *parts, int n, float x, float y, int size, Color c)
{
    float x0 = x;
    for (int i = 0, k = 0; i < n; i++) {
        if (!parts[i] || !*parts[i]) continue;
        if (k++) { font_draw("/", x + 18, y, size, THEME_TEXT_MUTED); x += 18 + font_measure("/", size) + 18; }
        font_draw(parts[i], x, y, size, c);
        x += font_measure(parts[i], size);
    }
    return x - x0;
}

float ui_aperture_button(const char *label, int icon, float x, float y, int primary, int focused)
{
    int fs = FONT_S | FONT_B;
    float h = 64, iw = icon >= 0 ? 34 : 0, w = (primary ? 44 : 0) + iw + font_measure(label, fs) + (primary ? 52 : 0);
    if (primary) {
        gfx_rrect(x, y, w, h, 4, focused ? THEME_TEXT : COLOR_ALPHA(THEME_TEXT, 0xE6));
        if (focused) gfx_rect(x + 10, y + h + 6, w - 20, 3, THEME_ACCENT);
        if (icon >= 0) ui_icon(icon, x + 44 + 10, y + h / 2, 26, THEME_BG);
        font_draw(label, x + 44 + iw, y + (h - font_height(fs)) / 2, fs, THEME_BG);
    } else {
        Color fg = focused ? THEME_TEXT : THEME_TEXT_DIM;
        if (icon >= 0) ui_icon(icon, x + 12, y + h / 2, 24, fg);
        font_draw(label, x + iw, y + (h - font_height(FONT_S)) / 2, FONT_S, fg);
        if (focused) gfx_rect(x, y + h - 8, w, 3, THEME_ACCENT);
    }
    return w;
}

static int g_card_capture = 1;
void ui_card_capture(int on) { g_card_capture = on; }

float ui_focus_lift(float focus) { return g_theme.focus_style == FOCUS_LIGHTBAR ? 10 * focus : 0; }

void ui_card(const char *url, float x, float y, float w, float h, float focus, float dim, const char *title, int flags)
{
    int mip = flags & (CARD_MIP | CARD_BLUR), blur = flags & CARD_BLUR;
    float sc = ui_focus_scale(focus);
    float cw = w * sc, ch = h * sc, cx = x - (cw - w) / 2, cy = y - (ch - h) / 2 - ui_focus_lift(focus);
    if (focus > 0.95f && g_card_capture && !app_transition_active()) {
        float tx, ty; gfx_get_translate(&tx, &ty);   /* where it is on screen (a scrolled page) */
        g_app.card_rect[0] = cx + tx; g_app.card_rect[1] = cy + ty; g_app.card_rect[2] = cw; g_app.card_rect[3] = ch; g_app.card_at = g_app.time;
    }
    ui_focus_under(cx, cy, cw, ch, R_CARD, focus);
    int iw = 0, ih = 0;
    unsigned age = 0;
    unsigned tex = url ? image_get_ex(url, mip ? IMAGE_MIPMAP : IMAGE_PLAIN, &iw, &ih, &age) : 0;
    if (tex) {
        float u0, v0, u1, v1;
        cover_uv(iw, ih, cw, ch, &u0, &v0, &u1, &v1);
        float a = g_app.fx_light ? 1.0f : (age >= 10 ? 1.0f : age / 10.0f);
        if (a < 1) { gfx_rrect(cx, cy, cw, ch, R_CARD, THEME_POSTER_PH); app_redraw(); }
        int shade = (int)(255 - dim * 90);
        gfx_rtex_uv(tex, cx, cy, cw, ch, R_CARD, u0, v0, u1, v1, RGBA(shade, shade, shade, (int)(255 * a)), blur ? 4.5f : 0);
    } else {
        gfx_rrect_grad_v(cx, cy, cw, ch, R_CARD, COLOR_ALPHA(THEME_TEXT, 0x18), COLOR_ALPHA(THEME_TEXT, 0x08));
        if (title) {
            gfx_clip_push(cx + 14, cy, cw - 28, ch);
            font_draw_wrapped(title, cx + 18, cy + ch * 0.36f, FONT_S | FONT_B, THEME_TEXT_DIM, cw - 36, 3);
            gfx_clip_pop();
        }
    }
    focus_over(cx, cy, cw, ch, R_CARD, focus);
}

void ui_qr(const uint8_t *qr, float x, float y, float size)
{
    int n = qrcodegen_getSize(qr);
    float cell = size / (n + 4), off = (size - cell * n) / 2;
    gfx_rrect(x, y, size, size, R_PANEL, RGBA(255, 255, 255, 255));
    for (int ry = 0; ry < n; ry++) for (int rx = 0; rx < n; rx++)
        if (qrcodegen_getModule(qr, rx, ry)) gfx_rect(x + off + rx * cell, y + off + ry * cell, cell + 0.6f, cell + 0.6f, COLOR_ALPHA(THEME_BG, 255));
}
