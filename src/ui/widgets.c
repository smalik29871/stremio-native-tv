#include "widgets.h"
#include "font.h"
#include "../app.h"
#include "../image.h"
#include <math.h>
#include <string.h>

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
    }
}

/* ---- background ---- */

static float g_tint[3] = { 0x8B, 0x5C, 0xF6 };

void ui_background(Color tint)
{
    if (!tint) tint = THEME_ACCENT;
    float tr = (float)((tint >> 24) & 255), tg = (float)((tint >> 16) & 255), tb = (float)((tint >> 8) & 255);
    float k = g_app.fx_light ? 1.0f : 0.06f;
    g_tint[0] += (tr - g_tint[0]) * k; g_tint[1] += (tg - g_tint[1]) * k; g_tint[2] += (tb - g_tint[2]) * k;
    if (fabsf(tr - g_tint[0]) + fabsf(tg - g_tint[1]) + fabsf(tb - g_tint[2]) > 3) app_redraw();
    Color t = RGBA((int)g_tint[0], (int)g_tint[1], (int)g_tint[2], 0xFF);

    if (g_app.oled) { gfx_rect(0, 0, SNTV_UI_W, SNTV_UI_H, RGBA(0, 0, 0, 0xFF)); return; }
    gfx_gradient_v(0, 0, SNTV_UI_W, SNTV_UI_H, THEME_BG2, THEME_BG);
    if (g_app.fx_light) {
        gfx_gradient_h(0, 0, SNTV_UI_W, SNTV_UI_H, COLOR_ALPHA(t, 0x30), COLOR_ALPHA(t, 0));
        return;
    }
    /* two large, very soft glows: tinted top-left, accent bottom-right */
    gfx_shadow(-300, -420, 1300, 1000, 500, 420, COLOR_ALPHA(t, 0x46));
    gfx_shadow(1250, 650, 1000, 800, 400, 380, COLOR_ALPHA(THEME_ACCENT2, 0x16));
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
    float xs[4] = { x0, mid, left, SNTV_UI_W }, ax[4] = { 0.0f, 0.42f, 1.0f, 1.0f };
    float ys[5] = { 0, 150, bottom, bm, height }, ay[5] = { 0.55f, 1.0f, 1.0f, 0.42f, 0.0f };
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
    gfx_gradient_h(0, 0, mid, height, COLOR_ALPHA(bg, 0xD0), COLOR_ALPHA(bg, 0x90));
    gfx_gradient_h(mid, 0, width - mid, height, COLOR_ALPHA(bg, 0x90), COLOR_ALPHA(bg, 0));
}

void ui_backdrop_blur(unsigned tex, int iw, int ih, float alpha)
{
    if (!tex) return;
    float u0, v0, u1, v1;
    cover_uv(iw, ih, SNTV_UI_W, SNTV_UI_H, &u0, &v0, &u1, &v1);
    gfx_tex_blur(tex, 0, 0, SNTV_UI_W, SNTV_UI_H, u0, v0, u1, v1, RGBA(150, 150, 165, (int)(255 * alpha)), 5.0f);
    gfx_rect(0, 0, SNTV_UI_W, SNTV_UI_H, RGBA(0x08, 0x07, 0x12, 0x90));
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
    if (focused) { bg = THEME_TEXT; fg = THEME_ON_LIGHT; }
    else if (style == PILL_PRIMARY) { bg = RGBA(0xFF, 0xFF, 0xFF, 0x2C); fg = THEME_TEXT; }
    else if (style == PILL_SECONDARY) { bg = THEME_SURFACE; fg = THEME_TEXT; }
    else { bg = RGBA(0, 0, 0, 0); fg = THEME_TEXT_DIM; }
    if (focused && !g_app.fx_light) gfx_shadow(x, y + 6, w, h, h / 2, 22, RGBA(0, 0, 0, 0x70));
    if (style != PILL_GHOST || focused) gfx_rrect(x, y, w, h, h / 2, bg);
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
    if (focused && !g_app.fx_light) gfx_shadow(cx - r, cy - r + 6, 2 * r, 2 * r, r, 20, RGBA(0, 0, 0, 0x70));
    gfx_circle(cx, cy, r, focused ? THEME_TEXT : THEME_SURFACE);
    ui_icon(icon, cx, cy, r * 0.95f, focused ? THEME_ON_LIGHT : THEME_TEXT);
    return 2 * r;
}

float ui_chip(const char *text, float x, float y, Color bg, Color fg)
{
    int fs = FONT_XS | FONT_B;
    float w = font_measure(text, fs) + 24, h = 36;
    gfx_rrect(x, y, w, h, 9, bg);
    font_draw(text, x + 12, y + (h - font_height(fs)) / 2, fs, fg);
    return w;
}

float ui_chip_outline(const char *text, float x, float y, Color line, Color fg)
{
    int fs = FONT_XS;
    float w = font_measure(text, fs) + 28, h = 36;
    gfx_rrect_ring(x + 1.5f, y + 1.5f, w - 3, h - 3, 18, 1.5f, line);
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

void ui_toggle(float x, float y, int on, int focused)
{
    float w = 76, h = 40;
    Color track = on ? THEME_ACCENT : RGBA(0xFF, 0xFF, 0xFF, 0x30);
    if (on) gfx_rrect_grad_h(x, y, w, h, h / 2, THEME_ACCENT, THEME_ACCENT2);
    else gfx_rrect(x, y, w, h, h / 2, track);
    if (focused) gfx_rrect_ring(x, y, w, h, h / 2, 3, THEME_TEXT);
    float kx = on ? x + w - h / 2 : x + h / 2;
    gfx_circle(kx, y + h / 2, h / 2 - 5, THEME_TEXT);
}

void ui_progress(float x, float y, float w, float h, float frac, Color fill)
{
    if (frac < 0) frac = 0;
    if (frac > 1) frac = 1;
    gfx_rrect(x, y, w, h, h / 2, RGBA(0xFF, 0xFF, 0xFF, 0x38));
    if (frac > 0) {
        float fw = w * frac;
        if (fw < h) fw = h;
        if (fill == THEME_ACCENT) gfx_rrect_grad_h(x, y, fw, h, h / 2, THEME_ACCENT, THEME_ACCENT2);
        else gfx_rrect(x, y, fw, h, h / 2, fill);
    }
}

void ui_skeleton(float x, float y, float w, float h, float r)
{
    gfx_rrect(x, y, w, h, r, RGBA(0xFF, 0xFF, 0xFF, 0x10));
    if (g_app.fx_light) return;
    /* a soft highlight sweeping left to right */
    float period = 1.6f, t = fmodf(g_app.time + x * 0.0006f, period) / period;
    float sw = w * 0.6f, sx = x - sw + (w + sw) * t;
    gfx_clip_push(x, y, w, h);
    gfx_gradient_h(sx, y, sw / 2, h, RGBA(0xFF, 0xFF, 0xFF, 0), RGBA(0xFF, 0xFF, 0xFF, 0x14));
    gfx_gradient_h(sx + sw / 2, y, sw / 2, h, RGBA(0xFF, 0xFF, 0xFF, 0x14), RGBA(0xFF, 0xFF, 0xFF, 0));
    gfx_clip_pop();
    app_redraw();
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

void ui_brand(float x, float y, float s)
{
    gfx_rrect_grad_v(x, y, s, s, s * 0.28f, THEME_ACCENT2, THEME_ACCENT);
    gfx_play_triangle(x + s * 0.36f, y + s * 0.28f, s * 0.38f, s * 0.44f, THEME_TEXT);
    font_draw("Stremio", x + s + 16, y + (s - font_height(FONT_L | FONT_B)) / 2, FONT_L | FONT_B, THEME_TEXT);
}

/* ---- cards ---- */

float ui_focus_scale(float focus) { return 1.0f + 0.09f * focus; }

void ui_card(const char *url, float x, float y, float w, float h, float focus, float dim, const char *title, int flags)
{
    int mip = flags & (CARD_MIP | CARD_BLUR), blur = flags & CARD_BLUR;
    float sc = ui_focus_scale(focus);
    float cw = w * sc, ch = h * sc, cx = x - (cw - w) / 2, cy = y - (ch - h) / 2;
    if (focus > 0.01f && !g_app.fx_light) gfx_shadow(cx, cy + 14 * focus, cw, ch, R_CARD, 34, RGBA(0, 0, 0, (int)(0xB0 * focus)));
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
        gfx_rrect_grad_v(cx, cy, cw, ch, R_CARD, RGBA(0xFF, 0xFF, 0xFF, 0x18), RGBA(0xFF, 0xFF, 0xFF, 0x08));
        if (title) {
            gfx_clip_push(cx + 14, cy, cw - 28, ch);
            font_draw_wrapped(title, cx + 18, cy + ch * 0.36f, FONT_S | FONT_B, THEME_TEXT_DIM, cw - 36, 3);
            gfx_clip_pop();
        }
    }
    if (focus > 0.01f) gfx_rrect_ring(cx, cy, cw, ch, R_CARD, 4.5f, COLOR_ALPHA(THEME_TEXT, (int)(255 * focus)));
}
