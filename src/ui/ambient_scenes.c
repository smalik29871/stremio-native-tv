#include "ambient_scenes.h"
#include "gfx.h"
#include <math.h>

#define TAU 6.2831853f

/* 0..1, the same for the same i: where a star or a drop sits, without keeping a list */
static float h01(unsigned i)
{
    i ^= i >> 16; i *= 0x7feb352dU; i ^= i >> 15; i *= 0x846ca68bU; i ^= i >> 16;
    return (float)(i & 0xFFFFFF) / 16777216.0f;
}

static float wave(float v) { return 0.5f + 0.5f * sinf(v); }

static void stars(float x, float y, float w, float h, float t, int n, float top_frac)
{
    float k = h / 1080;
    for (int i = 0; i < n; i++) {
        float sx = x + h01(i * 3) * w, sy = y + h01(i * 3 + 1) * h * top_frac;
        float r = (0.8f + 1.8f * h01(i * 3 + 2) * h01(i * 3 + 2)) * k + 0.4f;
        int a = 0x40 + (int)(0x90 * wave(t * (0.6f + 1.4f * h01(i + 900)) + i));
        gfx_circle(sx, sy, r, RGBA(0xE8, 0xF0, 0xFF, a));
    }
}

void ambient_aurora(float x, float y, float w, float h, float t)
{
    gfx_gradient_v(x, y, w, h, RGBA(0x02, 0x04, 0x10, 0xFF), RGBA(0x05, 0x14, 0x22, 0xFF));
    stars(x, y, w, h, t, 150, 0.8f);
    static const Color BAND[3] = { RGBA(0x3C, 0xFF, 0x9A, 0), RGBA(0x2A, 0xD0, 0xE8, 0), RGBA(0xA8, 0x64, 0xFF, 0) };
    enum { STRIPS = 96 };
    float sw = w / STRIPS;
    for (int b = 0; b < 3; b++)
        for (int i = 0; i < STRIPS; i++) {
            float u = (float)i / STRIPS;
            float base = y + h * (0.34f + 0.07f * b) + h * 0.08f * sinf(u * 5 + t * 0.11f * (b + 1) + b * 2)
                       + h * 0.025f * sinf(u * 13 - t * 0.29f + b);
            float ch = h * (0.16f + 0.16f * wave(u * 7 + t * 0.19f + b * 1.7f));
            float glow = (0.25f + 0.75f * wave(u * 9 - t * 0.37f + b * 3)) * (b == 0 ? 0.62f : 0.42f);
            float edge = u < 0.1f ? u / 0.1f : u > 0.9f ? (1 - u) / 0.1f : 1;
            int a = (int)(255 * glow * edge);
            Color c = BAND[b] | (Color)a, clear = BAND[b];
            gfx_gradient_v(x + i * sw, base - ch, sw + 1, ch, clear, c);
            gfx_gradient_v(x + i * sw, base, sw + 1, h * 0.05f, c, clear);
        }
    /* the treeline: a low ridge, pines standing on it as stacked tiers */
    Color ink = RGBA(0x01, 0x02, 0x05, 0xFF);
    enum { RIDGE = 120, PINES = 46 };
    float rw = w / RIDGE;
    for (int i = 0; i < RIDGE; i++) {
        float u = (float)i / RIDGE, rh = h * (0.06f + 0.02f * sinf(u * 6 + 1) + 0.012f * sinf(u * 17));
        gfx_rect(x + i * rw, y + h - rh, rw + 1, rh, ink);
    }
    for (int i = 0; i < PINES; i++) {
        float u = h01(i + 5000), ph = h * (0.07f + 0.08f * h01(i + 6000)), pw = ph * 0.42f;
        float px = x + u * w, ground = y + h - h * (0.06f + 0.02f * sinf(u * 6 + 1) + 0.012f * sinf(u * 17)) + 2;
        for (int k = 0; k < 5; k++) {   /* wide at the foot, a point at the top */
            float tw = pw * (1 - k * 0.19f), th = ph / 5 + 1;
            gfx_rect(px - tw / 2, ground - (k + 1) * ph / 5, tw, th, ink);
        }
        gfx_rect(px - pw * 0.04f, ground - ph - ph * 0.08f, pw * 0.08f, ph * 0.1f, ink);
    }
}

void ambient_warp(float x, float y, float w, float h, float t)
{
    gfx_rect(x, y, w, h, RGBA(0, 0, 0, 0xFF));
    float k = h / 1080;
    float cx = x + w / 2 + w * 0.06f * sinf(t * 0.05f), cy = y + h / 2 + h * 0.05f * sinf(t * 0.037f);
    /* two far nebulae, drifting */
    gfx_shadow(cx - w * 0.25f + w * 0.05f * sinf(t * 0.02f), cy - h * 0.1f, w * 0.2f, h * 0.2f, h * 0.1f, h * 0.18f, RGBA(0x40, 0x20, 0x80, 0x30));
    gfx_shadow(cx + w * 0.1f, cy + h * 0.05f + h * 0.05f * sinf(t * 0.03f), w * 0.18f, h * 0.16f, h * 0.08f, h * 0.16f, RGBA(0x10, 0x40, 0x90, 0x28));
    float reach = (w > h ? w : h) * 0.5f;
    for (int i = 0; i < 260; i++) {
        float ang = h01(i) * TAU, lat = 0.04f + 0.96f * h01(i + 777);
        float speed = 0.10f + 0.10f * h01(i + 55);
        float z = fmodf(h01(i + 333) - t * speed, 1.0f);
        if (z < 0) z += 1;
        float z0 = z + 0.035f;
        float d1 = lat / (z + 0.04f) * 0.06f, d0 = lat / (z0 + 0.04f) * 0.06f;
        float x1 = cx + cosf(ang) * d1 * reach, y1 = cy + sinf(ang) * d1 * reach;
        float x0 = cx + cosf(ang) * d0 * reach, y0 = cy + sinf(ang) * d0 * reach;
        if (x1 < x || x1 > x + w || y1 < y || y1 > y + h) continue;
        float near = 1 - z;
        int a = (int)(255 * (near < 0.15f ? near / 0.15f : 1) * (0.35f + 0.65f * near));
        Color c = h01(i + 99) > 0.8f ? RGBA(0xB8, 0xD0, 0xFF, a) : RGBA(0xF4, 0xF6, 0xFF, a);
        gfx_line(x0, y0, x1, y1, (0.6f + 2.6f * near * near) * k + 0.5f, c);
    }
}

void ambient_rain(unsigned tex, int iw, int ih, float x, float y, float w, float h, float t, float alpha)
{
    float k = h / 1080;
    /* the view out: cover-fit, slowly drifting, blurred by the wet glass */
    float u0 = 0, v0 = 0, u1 = 1, v1 = 1;
    if (tex && iw > 0 && ih > 0) {
        float ia = (float)iw / ih, sa = w / h;
        if (ia > sa) { float f = sa / ia; u0 = (1 - f) / 2; u1 = 1 - u0; } else { float f = ia / sa; v0 = (1 - f) / 2; v1 = 1 - v0; }
        float pad = (u1 - u0) * 0.04f, du = pad * sinf(t * 0.03f);
        u0 += pad + du; u1 -= pad - du;
        gfx_tex_blur(tex, x, y, w, h, u0, v0, u1, v1, RGBA(140, 150, 170, (int)(255 * alpha)), 5.0f);
    }
    gfx_gradient_v(x, y, w, h, RGBA(0x08, 0x10, 0x20, 0x50), RGBA(0x02, 0x04, 0x0A, 0xA0));
    int ia = (int)(255 * alpha);
    /* a drop: the view through it upside down and sharp, a dark rim, a glint */
    #define DROP(dx, dy, rx, ry, fade) do { \
        float px_ = (dx), py_ = (dy), rx_ = (rx), ry_ = (ry); int fa_ = (int)(ia * (fade)); \
        if (tex && fa_ > 8) { \
            float cu = u0 + (u1 - u0) * (px_ - x) / w, cv = v0 + (v1 - v0) * (py_ - y) / h; \
            float su = (u1 - u0) * rx_ * 3 / w, sv = (v1 - v0) * ry_ * 3 / h; \
            gfx_rtex_uv(tex, px_ - rx_, py_ - ry_, 2 * rx_, 2 * ry_, rx_, cu + su, cv + sv, cu - su, cv - sv, RGBA(225, 232, 245, fa_), 0); \
        } else gfx_rrect(px_ - rx_, py_ - ry_, 2 * rx_, 2 * ry_, rx_, RGBA(0xC0, 0xD0, 0xE8, fa_ / 4)); \
        gfx_rrect_ring(px_ - rx_, py_ - ry_, 2 * rx_, 2 * ry_, rx_, 1.2f * k + 0.3f, RGBA(0, 0, 0, fa_ / 3)); \
        gfx_circle(px_ - rx_ * 0.35f, py_ - ry_ * 0.4f, rx_ * 0.22f + 0.5f, RGBA(0xFF, 0xFF, 0xFF, fa_ * 2 / 3)); \
    } while (0)
    /* beads that gather and dry, each on its own slow cycle */
    for (int i = 0; i < 90; i++) {
        float period = 18 + 24 * h01(i + 40), ph = fmodf(t / period + h01(i + 41), 1.0f);
        float fade = ph < 0.15f ? ph / 0.15f : ph > 0.85f ? (1 - ph) / 0.15f : 1;
        float r = (3 + 10 * h01(i + 42) * h01(i + 42)) * k + 0.6f;
        DROP(x + h01(i + 43) * w, y + h01(i + 44) * h, r, r * 1.05f, fade);
    }
    /* drops running down: stop, then go, leaving a trail of small beads */
    for (int i = 0; i < 14; i++) {
        float lane = x + (0.04f + 0.92f * h01(i + 200)) * w;
        float sp = 0.03f + 0.04f * h01(i + 201), s = t * sp + h01(i + 202) * 4;
        float step = floorf(s), f = s - step;
        float go = f < 0.6f ? 0 : (f - 0.6f) / 0.4f;   /* still for a while, then a run */
        float prog = fmodf((step + go * go * (3 - 2 * go)) * 0.23f, 1.3f) - 0.15f;
        float dy = y + prog * h, r = (7 + 6 * h01(i + 203)) * k + 0.8f;
        float dx = lane + 6 * k * sinf(prog * 23 + i);
        for (int j = 1; j <= 6; j++) {
            float ty = dy - j * 22 * k;
            if (ty < y) break;
            float tr = r * (0.45f - 0.05f * j);
            DROP(lane + 6 * k * sinf((prog - j * 22 * k / h) * 23 + i), ty, tr, tr, 0.9f - j * 0.12f);
        }
        if (dy > y - r && dy < y + h + r) DROP(dx, dy, r, r * 1.25f, 1.0f);
    }
    #undef DROP
}

void ambient_marquee(float x, float y, float w, float h, float t, float *px, float *py, float *pw, float *ph)
{
    float k = h / 1080;
    /* the curtain: folds of velvet, swaying a little */
    enum { FOLDS = 22 };
    float fw = w / FOLDS;
    for (int i = 0; i < FOLDS; i++) {
        float sway = 3 * k * sinf(t * 0.3f + i * 0.7f), fx = x + i * fw + sway;
        gfx_gradient_h(fx, y, fw / 2 + 1, h, RGBA(0x30, 0x04, 0x0A, 0xFF), RGBA(0x78, 0x0E, 0x1A, 0xFF));
        gfx_gradient_h(fx + fw / 2, y, fw / 2 + 1, h, RGBA(0x78, 0x0E, 0x1A, 0xFF), RGBA(0x30, 0x04, 0x0A, 0xFF));
    }
    gfx_gradient_v(x, y, w, h * 0.35f, RGBA(0, 0, 0, 0xB0), RGBA(0, 0, 0, 0));
    gfx_gradient_v(x, y + h * 0.55f, w, h * 0.45f, RGBA(0, 0, 0, 0), RGBA(0, 0, 0, 0xE0));
    /* the poster frame, a spotlight on it, bulbs chasing round it */
    *ph = h * 0.60f; *pw = *ph * 2 / 3;
    *px = x + w * 0.16f + w * 0.01f * sinf(t * 0.02f); *py = y + h * 0.20f;
    float m = 34 * k, fx = *px - m, fy = *py - m, fW = *pw + 2 * m, fH = *ph + 2 * m;
    gfx_shadow(fx, fy, fW, fH, 40 * k, 160 * k, RGBA(0xFF, 0xC8, 0x80, 0x38));
    gfx_rrect(fx, fy, fW, fH, 10 * k, RGBA(0xB8, 0x8E, 0x3E, 0xFF));
    gfx_rrect(fx + 8 * k, fy + 8 * k, fW - 16 * k, fH - 16 * k, 8 * k, RGBA(0x5A, 0x40, 0x18, 0xFF));
    gfx_rrect(*px - 6 * k, *py - 6 * k, *pw + 12 * k, *ph + 12 * k, 6 * k, RGBA(0x0A, 0x08, 0x06, 0xFF));
    float sp = 36 * k, per = 2 * (fW + fH);
    int n = (int)(per / sp), lit0 = (int)(t * 6);
    for (int i = 0; i < n; i++) {
        float d = i * per / n, bx, by;
        if (d < fW) { bx = fx + d; by = fy; }
        else if (d < fW + fH) { bx = fx + fW; by = fy + d - fW; }
        else if (d < 2 * fW + fH) { bx = fx + fW - (d - fW - fH); by = fy + fH; }
        else { bx = fx; by = fy + fH - (d - 2 * fW - fH); }
        int on = (i + lit0) % 3 != 0;
        if (on) gfx_shadow(bx - 6 * k, by - 6 * k, 12 * k, 12 * k, 6 * k, 10 * k, RGBA(0xFF, 0xD8, 0x90, 0x70));
        gfx_circle(bx, by, 6.5f * k + 0.5f, on ? RGBA(0xFF, 0xF3, 0xD0, 0xFF) : RGBA(0x6A, 0x52, 0x2C, 0xFF));
    }
}
