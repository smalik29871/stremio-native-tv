/* Clock faces: digital, flip cards, a Braun-style dial, a word clock (QLOCKTWO's
 * English grid), polar rings, nixie tubes, StandBy-style stacked digits, a red LED
 * alarm clock, an amber dot-matrix board, a neon sign, odometer drums, planets in
 * orbit, and a sky that follows the day. Plain shapes and the big digit atlas only,
 * so every face is sharp at any size. */
#include "clockface.h"
#include "font.h"
#include "gfx.h"
#include "theme.h"
#include "../app.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

const char *const CLOCKFACE_NAMES[CLOCKFACE_COUNT] = { "Digital", "Flip", "Analog", "Words", "Rings", "Nixie", "Stacked",
                                                           "Segment", "Dot matrix", "Neon", "Rolling", "Orbit", "Daylight" };

#define WHITE RGBA(0xEC, 0xEC, 0xF0, 0xFF)
#define DIM   RGBA(0x8C, 0x8C, 0x96, 0xFF)
#define PI_F  3.14159265f

static int hour_shown(const struct tm *t) { return g_app.clock_mode == 1 ? t->tm_hour : (t->tm_hour % 12 ? t->tm_hour % 12 : 12); }

static void date_line(const struct tm *t, float cx, float y, float k, Color c)
{
    char d[64];
    strftime(d, sizeof d, "%A, %B %-d", t);
    font_draw_center(d, cx, y, (int)(34 * k + 0.5f), c);
}

static void ampm(const struct tm *t, float x, float y, float k, Color c)
{
    if (g_app.clock_mode != 1) font_draw(t->tm_hour < 12 ? "AM" : "PM", x, y, (int)(30 * k + 0.5f) | FONT_B, c);
}

static void digits_center(const char *s, float cx, float top, float px, Color c)
{
    font_digits_draw(s, cx - font_digits_measure(s, px) / 2, top, px, c);
}

/* ---- digital ---- */

static void digital(float cx, float cy, float k, const struct tm *t)
{
    char s[8]; snprintf(s, sizeof s, g_app.clock_mode == 1 ? "%02d:%02d" : "%d:%02d", hour_shown(t), t->tm_min);
    float px = 420 * k, h = font_digits_height(px), w = font_digits_measure(s, px), top = cy - h / 2 - 24 * k;
    font_digits_draw(s, cx - w / 2, top, px, WHITE);
    ampm(t, cx + w / 2 + 20 * k, top + 6 * k, k, DIM);
    date_line(t, cx, top + h + 56 * k, k, DIM);
}

/* ---- flip cards: the top flap falls over the old number, the new bottom drops in ---- */

static struct { char text[2][4]; char old[2][4]; float at[2]; } F;

static void flip_half(const char *s, float x, float y, float w, float h, float px, int top, float clip0, float clip1)
{
    if (clip1 - clip0 < 0.5f) return;
    float mid = y + h / 2;
    gfx_clip_push(x, clip0, w, clip1 - clip0);
    gfx_rrect(x, y, w, h, h * 0.07f, top ? RGBA(0x24, 0x24, 0x28, 0xFF) : RGBA(0x1C, 0x1C, 0x20, 0xFF));
    digits_center(s, x + w / 2, mid - font_digits_height(px) / 2, px, WHITE);
    gfx_clip_pop();
}

static void flip_card(int i, const char *now, float x, float y, float w, float h, float px)
{
    if (strcmp(F.text[i], now)) {
        snprintf(F.old[i], sizeof F.old[i], "%s", F.text[i][0] ? F.text[i] : now);
        snprintf(F.text[i], sizeof F.text[i], "%s", now);
        F.at[i] = g_app.time;
    }
    float mid = y + h / 2, p = (g_app.time - F.at[i]) / 0.6f;
    if (p > 1 || g_app.fx_light) p = 1;
    gfx_shadow(x, y + 14 * w / 330, w, h, h * 0.07f, 30 * w / 330, RGBA(0, 0, 0, 0xC0));
    flip_half(now, x, y, w, h, px, 1, y, mid);                       /* the new top, under the falling flap */
    flip_half(p < 1 ? F.old[i] : now, x, y, w, h, px, 0, mid, y + h); /* the old bottom until the flap lands */
    if (p < 0.5f) {   /* the old top folds down towards the hinge */
        float q = p * 2, top = mid - (h / 2) * (1 - q);
        flip_half(F.old[i], x, y, w, h, px, 1, top, mid);
        gfx_rect(x, top, w, mid - top, RGBA(0, 0, 0, (int)(0x90 * q)));
    } else if (p < 1) {   /* and opens as the new bottom */
        float q = (p - 0.5f) * 2, bot = mid + (h / 2) * q;
        flip_half(now, x, y, w, h, px, 0, mid, bot);
        gfx_rect(x, mid, w, bot - mid, RGBA(0, 0, 0, (int)(0x90 * (1 - q))));
    }
    gfx_rect(x, mid - 3 * w / 330, w, 6 * w / 330, RGBA(0, 0, 0, 0xFF));   /* the split */
    float hr = 9 * w / 330;
    gfx_rrect(x - hr * 0.6f, mid - hr * 1.6f, hr * 1.2f, hr * 3.2f, hr * 0.6f, RGBA(0x40, 0x40, 0x46, 0xFF));   /* hinges */
    gfx_rrect(x + w - hr * 0.6f, mid - hr * 1.6f, hr * 1.2f, hr * 3.2f, hr * 0.6f, RGBA(0x40, 0x40, 0x46, 0xFF));
    if (p < 1) app_redraw();
}

static void flip(float cx, float cy, float k, const struct tm *t)
{
    char hh[4], mm[4];
    snprintf(hh, sizeof hh, g_app.clock_mode == 1 ? "%02d" : "%d", hour_shown(t));
    snprintf(mm, sizeof mm, "%02d", t->tm_min);
    float w = 400 * k, h = 460 * k, gap = 44 * k, y = cy - h / 2 - 30 * k, px = 400 * k;
    flip_card(0, hh, cx - gap / 2 - w, y, w, h, px);
    flip_card(1, mm, cx + gap / 2, y, w, h, px);
    if (g_app.clock_mode != 1) font_draw(t->tm_hour < 12 ? "AM" : "PM", cx - gap / 2 - w + 22 * k, y + h - 52 * k, (int)(26 * k + 0.5f) | FONT_B, DIM);
    date_line(t, cx, y + h + 44 * k, k, DIM);
}

/* ---- analog: a plain dial, sweeping seconds in the accent ---- */

static void hand(float cx, float cy, float ang, float back, float len, float th, Color c)
{
    float s = sinf(ang), co = -cosf(ang);
    gfx_line(cx - s * back, cy - co * back, cx + s * len, cy + co * len, th, c);
}

static void analog(float cx, float cy, float k, const struct tm *t, double sec)
{
    float R = 360 * k;
    gfx_circle(cx, cy, R, RGBA(0x14, 0x14, 0x18, 0xFF));
    gfx_rrect_ring(cx - R, cy - R, 2 * R, 2 * R, R, 4 * k, RGBA(0x30, 0x30, 0x36, 0xFF));
    for (int i = 0; i < 60; i++) {
        float a = i * PI_F / 30, s = sinf(a), c = -cosf(a);
        int big = i % 5 == 0;
        float r0 = R * (big ? 0.84f : 0.9f), r1 = R * 0.94f;
        gfx_line(cx + s * r0, cy + c * r0, cx + s * r1, cy + c * r1, (big ? 9 : 3) * k, big ? WHITE : RGBA(0x50, 0x50, 0x58, 0xFF));
    }
    /* the day of the month in a window at three */
    char d[4]; snprintf(d, sizeof d, "%d", t->tm_mday);
    int fs = (int)(30 * k + 0.5f) | FONT_B;
    float dw = 62 * k, dh = 44 * k, dx = cx + R * 0.58f - dw / 2;
    gfx_rrect(dx, cy - dh / 2, dw, dh, 6 * k, RGBA(0x24, 0x24, 0x2A, 0xFF));
    font_draw_center(d, dx + dw / 2, cy - font_height(fs) / 2, fs, WHITE);
    double m = t->tm_min + sec / 60.0, h = (t->tm_hour % 12) + m / 60.0;
    hand(cx, cy, (float)(h / 12 * 2 * PI_F), 26 * k, R * 0.52f, 16 * k, WHITE);
    hand(cx, cy, (float)(m / 60 * 2 * PI_F), 26 * k, R * 0.8f, 10 * k, WHITE);
    hand(cx, cy, (float)(sec / 60 * 2 * PI_F), 60 * k, R * 0.86f, 4 * k, THEME_ACCENT);
    gfx_circle(cx, cy, 14 * k, THEME_ACCENT);
    gfx_circle(cx, cy, 5 * k, RGBA(0x14, 0x14, 0x18, 0xFF));
}

/* ---- words: QLOCKTWO's English grid; the minutes past the five in the corners ---- */

static const char *const GRID[10] = {
    "ITLISASAMPM", "ACQUARTERDC", "TWENTYFIVEX", "HALFSTENFTO", "PASTERUNINE",
    "ONESIXTHREE", "FOURFIVETWO", "EIGHTELEVEN", "SEVENTWELVE", "TENSEOCLOCK" };
typedef struct { signed char r, c, n; } Word;
static const Word W_IT = { 0, 0, 2 }, W_IS = { 0, 3, 2 }, W_A = { 1, 0, 1 }, W_QUARTER = { 1, 2, 7 }, W_TWENTY = { 2, 0, 6 },
                  W_FIVE = { 2, 6, 4 }, W_HALF = { 3, 0, 4 }, W_TEN = { 3, 5, 3 }, W_TO = { 3, 9, 2 }, W_PAST = { 4, 0, 4 },
                  W_OCLOCK = { 9, 5, 6 };
static const Word HOURS[12] = { { 8, 5, 6 }, { 5, 0, 3 }, { 6, 8, 3 }, { 5, 6, 5 }, { 6, 0, 4 }, { 6, 4, 4 },
                                { 5, 3, 3 }, { 8, 0, 5 }, { 7, 0, 5 }, { 4, 7, 4 }, { 9, 0, 3 }, { 7, 5, 6 } };   /* twelve, one .. eleven */

static void light(unsigned short lit[10], Word w) { for (int i = 0; i < w.n; i++) lit[w.r] |= (unsigned short)(1u << (w.c + i)); }

static void words(float cx, float cy, float k, const struct tm *t)
{
    unsigned short lit[10] = { 0 };
    int m5 = t->tm_min / 5, h = t->tm_hour % 12;
    light(lit, W_IT); light(lit, W_IS);
    switch (m5) {
    case 0: light(lit, W_OCLOCK); break;
    case 1: case 11: light(lit, W_FIVE); break;
    case 2: case 10: light(lit, W_TEN); break;
    case 3: case 9: light(lit, W_A); light(lit, W_QUARTER); break;
    case 4: case 8: light(lit, W_TWENTY); break;
    case 5: case 7: light(lit, W_TWENTY); light(lit, W_FIVE); break;
    case 6: light(lit, W_HALF); break;
    }
    if (m5 >= 1 && m5 <= 6) light(lit, W_PAST);
    if (m5 >= 7) { light(lit, W_TO); h = (h + 1) % 12; }
    light(lit, HOURS[h]);
    float cw = 76 * k, ch = 78 * k, x0 = cx - cw * 5.5f, y0 = cy - ch * 5;
    int fs = (int)(46 * k + 0.5f) | FONT_B;
    for (int r = 0; r < 10; r++)
        for (int c = 0; c < 11; c++) {
            char ch1[2] = { GRID[r][c], 0 };
            int on = lit[r] >> c & 1;
            font_draw_center(ch1, x0 + cw * (c + 0.5f), y0 + ch * r + (ch - font_height(fs)) / 2, fs, on ? WHITE : RGBA(0x2A, 0x2A, 0x30, 0xFF));
        }
    /* one dot for each minute past the five, clockwise from the top left */
    float m = 40 * k, dots[4][2] = { { x0 - m, y0 - m }, { x0 + cw * 11 + m, y0 - m }, { x0 + cw * 11 + m, y0 + ch * 10 + m }, { x0 - m, y0 + ch * 10 + m } };
    for (int i = 0; i < 4; i++) gfx_circle(dots[i][0], dots[i][1], 7 * k, i < t->tm_min % 5 ? WHITE : RGBA(0x2A, 0x2A, 0x30, 0xFF));
}

/* ---- rings: hours, minutes and seconds as arcs that close as each fills ---- */

static void arc(float cx, float cy, float r, float frac, float th, Color c)
{
    int n = (int)(120 * frac) + 1;
    float px = cx, py = cy - r;
    for (int i = 1; i <= n; i++) {
        float a = 2 * PI_F * frac * i / n, x = cx + sinf(a) * r, y = cy - cosf(a) * r;
        gfx_line(px, py, x, y, th, c);
        px = x; py = y;
    }
}

static void rings(float cx, float cy, float k, const struct tm *t, double sec)
{
    double m = t->tm_min + sec / 60.0, h = (t->tm_hour % 12) + m / 60.0;
    float th = 22 * k;
    struct { float r, frac; Color c; } R[3] = {
        { 380 * k, (float)(h / 12), THEME_ACCENT }, { 340 * k, (float)(m / 60), WHITE }, { 300 * k, (float)(sec / 60), THEME_ACCENT2 } };
    for (int i = 0; i < 3; i++) {
        arc(cx, cy, R[i].r, 1, th, RGBA(0x1C, 0x1C, 0x22, 0xFF));
        if (R[i].frac > 0.002f) arc(cx, cy, R[i].r, R[i].frac, th, R[i].c);
    }
    char s[8]; snprintf(s, sizeof s, g_app.clock_mode == 1 ? "%02d:%02d" : "%d:%02d", hour_shown(t), t->tm_min);
    float px = 220 * k, hh = font_digits_height(px);
    digits_center(s, cx, cy - hh / 2 - 22 * k, px, WHITE);
    char d[32]; strftime(d, sizeof d, "%a %-d %b", t);
    font_draw_center(d, cx, cy + hh / 2 + 6 * k, (int)(28 * k + 0.5f), DIM);
}

/* ---- nixie: orange numerals glowing in glass tubes, the unlit ones behind ---- */

static void tube(const char *d, float x, float y, float w, float h, float px, float k)
{
    gfx_rrect(x, y, w, h, w / 2, RGBA(0x10, 0x0C, 0x0A, 0xFF));
    gfx_rrect_ring(x, y, w, h, w / 2, 3 * k, RGBA(0x3A, 0x2C, 0x24, 0xFF));
    float top = y + (h - font_digits_height(px)) / 2;
    digits_center("8", x + w / 2, top, px, RGBA(0x2A, 0x1A, 0x12, 0xFF));   /* the cathodes behind */
    float dw = font_digits_measure(d, px), dh = font_digits_height(px);
    gfx_shadow(x + (w - dw) / 2, top, dw, dh, 20 * k, 60 * k, RGBA(0xFF, 0x48, 0x08, 0x50));   /* the glow in the glass */
    for (int i = 0; i < 8; i++) {   /* and a tight halo round the strokes */
        float a = i * PI_F / 4, o = 2.5f * k;
        digits_center(d, x + w / 2 + cosf(a) * o, top + sinf(a) * o, px, RGBA(0xFF, 0x60, 0x18, 0x30));
    }
    digits_center(d, x + w / 2, top, px, RGBA(0xFF, 0xA4, 0x48, 0xFF));
    digits_center(d, x + w / 2, top, px, RGBA(0xFF, 0xE0, 0xB0, 0x60));
    for (int i = 1; i < 6; i++) gfx_rect(x + w * 0.14f, y + h * i / 6, w * 0.72f, 1.5f * k, RGBA(0, 0, 0, 0x50));   /* the mesh */
    gfx_rrect(x - 8 * k, y + h - 10 * k, w + 16 * k, 36 * k, 8 * k, RGBA(0x22, 0x1C, 0x18, 0xFF));   /* the socket */
}

static void nixie(float cx, float cy, float k, const struct tm *t)
{
    char s[8]; snprintf(s, sizeof s, "%02d%02d", hour_shown(t), t->tm_min);
    float w = 200 * k, h = 360 * k, gap = 24 * k, colon = 74 * k, px = 330 * k;
    float x = cx - (4 * w + 2 * gap + colon) / 2, y = cy - h / 2 - 30 * k;
    for (int i = 0; i < 4; i++) {
        char d[2] = { s[i], 0 };
        float tx = x + i * (w + gap) + (i >= 2 ? colon - gap : 0);
        tube(d, tx, y, w, h, px, k);
    }
    float colx = x + 2 * w + gap + (colon - gap) / 2;
    int on = (int)g_app.time % 2 == 0 || g_app.fx_light;
    for (int i = 0; i < 2; i++) {
        float dy = y + h * (i ? 0.62f : 0.38f);
        gfx_circle(colx, dy, 14 * k, RGBA(0xFF, 0x50, 0x10, on ? 0x40 : 0x10));
        gfx_circle(colx, dy, 8 * k, on ? RGBA(0xFF, 0xA4, 0x48, 0xFF) : RGBA(0x3A, 0x22, 0x14, 0xFF));
    }
    ampm(t, x + 4 * w + gap + colon + 14 * k, y + h - 40 * k, k, RGBA(0xB0, 0x70, 0x40, 0xFF));
    date_line(t, cx, y + h + 56 * k, k, RGBA(0x90, 0x70, 0x5C, 0xFF));
}

/* ---- stacked: the hour over the minutes, huge, like iPhone's StandBy ---- */

static void stacked(float cx, float cy, float k, const struct tm *t)
{
    char hh[4], mm[4];
    snprintf(hh, sizeof hh, "%02d", hour_shown(t));
    snprintf(mm, sizeof mm, "%02d", t->tm_min);
    float px = 400 * k, h = font_digits_height(px), gap = 60 * k, top = cy - h - gap / 2;
    digits_center(hh, cx, top, px, THEME_ACCENT);
    digits_center(mm, cx, top + h + gap, px, WHITE);
    char d[32]; strftime(d, sizeof d, "%a %-d", t);
    float x = cx + font_digits_measure(hh, px) / 2 + 40 * k;
    font_draw(d, x, top + 10 * k, (int)(30 * k + 0.5f) | FONT_B, DIM);
    ampm(t, x, top + h + gap + h - 36 * k, k, DIM);
}

/* ---- segment: a red LED alarm clock, the unlit segments faintly there ---- */

static void seg_digit(int v, float x, float y, float w, float h, float th, float k)
{
    static const unsigned char MASK[10] = { 0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F };
    float hh = h / 2, inset = th * 0.6f;
    float r[7][4] = {   /* a b c d e f g: top, upper right, lower right, bottom, lower left, upper left, middle */
        { x + inset, y, w - 2 * inset, th }, { x + w - th, y + inset, th, hh - inset * 1.2f }, { x + w - th, y + hh + inset * 0.2f, th, hh - inset * 1.2f },
        { x + inset, y + h - th, w - 2 * inset, th }, { x, y + hh + inset * 0.2f, th, hh - inset * 1.2f }, { x, y + inset, th, hh - inset * 1.2f },
        { x + inset, y + hh - th / 2, w - 2 * inset, th } };
    for (int i = 0; i < 7; i++) {
        int on = v >= 0 && (MASK[v] >> i & 1);
        if (on) gfx_shadow(r[i][0], r[i][1], r[i][2], r[i][3], th / 2, 22 * k, RGBA(0xFF, 0x20, 0x10, 0x60));
        gfx_rrect(r[i][0], r[i][1], r[i][2], r[i][3], th / 2, on ? RGBA(0xFF, 0x30, 0x1C, 0xFF) : RGBA(0x30, 0x0A, 0x08, 0xFF));
    }
}

static void segment(float cx, float cy, float k, const struct tm *t)
{
    int h = hour_shown(t), d[4] = { h / 10, h % 10, t->tm_min / 10, t->tm_min % 10 };
    if (g_app.clock_mode != 1 && d[0] == 0) d[0] = -1;   /* no leading zero on a 12-hour clock */
    float w = 170 * k, dh = 300 * k, th = 30 * k, gap = 40 * k, colon = 70 * k;
    float total = 4 * w + 2 * gap + colon, x = cx - total / 2, y = cy - dh / 2 - 30 * k;
    gfx_rrect(x - 70 * k, y - 60 * k, total + 140 * k, dh + 120 * k, 36 * k, RGBA(0x0E, 0x0B, 0x0C, 0xFF));
    gfx_rrect_ring(x - 70 * k, y - 60 * k, total + 140 * k, dh + 120 * k, 36 * k, 3 * k, RGBA(0x24, 0x1C, 0x1E, 0xFF));
    for (int i = 0; i < 4; i++) seg_digit(d[i], x + i * (w + gap) + (i >= 2 ? colon - gap : 0), y, w, dh, th, k);
    float colx = x + 2 * w + gap + (colon - gap) / 2;
    int on = (int)g_app.time % 2 == 0 || g_app.fx_light;
    for (int i = 0; i < 2; i++) gfx_circle(colx, y + dh * (i ? 0.7f : 0.3f), th * 0.55f, on ? RGBA(0xFF, 0x30, 0x1C, 0xFF) : RGBA(0x30, 0x0A, 0x08, 0xFF));
    if (g_app.clock_mode != 1) {   /* the PM lamp, as on the real ones */
        int pm = t->tm_hour >= 12;
        float lx = x - 40 * k, ly = y + dh - 14 * k;
        gfx_circle(lx, ly, 9 * k, pm ? RGBA(0xFF, 0x30, 0x1C, 0xFF) : RGBA(0x30, 0x0A, 0x08, 0xFF));
        font_draw_center("PM", lx, ly - 46 * k, (int)(20 * k + 0.5f) | FONT_B, RGBA(0x80, 0x30, 0x28, 0xFF));
    }
    date_line(t, cx, y + dh + 96 * k, k, RGBA(0x90, 0x50, 0x48, 0xFF));
}

/* ---- dot matrix: amber LEDs, five by seven, like a departure board ---- */

static const unsigned char DOT_GLYPH[11][7] = {
    { 14, 17, 19, 21, 25, 17, 14 }, { 4, 12, 4, 4, 4, 4, 14 }, { 14, 17, 1, 2, 4, 8, 31 }, { 31, 2, 4, 2, 1, 17, 14 },
    { 2, 6, 10, 18, 31, 2, 2 }, { 31, 16, 30, 1, 1, 17, 14 }, { 6, 8, 16, 30, 17, 17, 14 }, { 31, 1, 2, 4, 8, 8, 8 },
    { 14, 17, 17, 14, 17, 17, 14 }, { 14, 17, 17, 15, 1, 2, 12 }, { 0, 0, 1, 0, 1, 0, 0 } };   /* 0-9, then the colon (one column) */

static void dots(float cx, float cy, float k, const struct tm *t)
{
    char s[8]; snprintf(s, sizeof s, g_app.clock_mode == 1 ? "%02d:%02d" : "%2d:%02d", hour_shown(t), t->tm_min);
    float p = 30 * k, r = 11.5f * k;
    int cols = 0;
    for (int i = 0; s[i]; i++) cols += (s[i] == ':' ? 1 : 5) + (s[i + 1] ? 1 : 0);
    float x0 = cx - cols * p / 2, y0 = cy - 3.5f * p - 40 * k;
    gfx_rrect(x0 - 60 * k, y0 - 60 * k, cols * p + 120 * k, 7 * p + 120 * k, 18 * k, RGBA(0x0A, 0x08, 0x05, 0xFF));
    int on_colon = (int)g_app.time % 2 == 0 || g_app.fx_light;
    float x = x0;
    for (int i = 0; s[i]; i++) {
        int g = s[i] == ':' ? 10 : s[i] == ' ' ? -1 : s[i] - '0', w = s[i] == ':' ? 1 : 5;
        for (int row = 0; row < 7; row++)
            for (int c = 0; c < w; c++) {
                int on = g >= 0 && (DOT_GLYPH[g][row] >> (w - 1 - c) & 1) && (g != 10 || on_colon);
                float dx = x + c * p + p / 2, dy = y0 + row * p + p / 2;
                if (on) gfx_circle(dx, dy, r * 1.7f, RGBA(0xFF, 0x90, 0x10, 0x22));
                gfx_circle(dx, dy, r, on ? RGBA(0xFF, 0xB4, 0x30, 0xFF) : RGBA(0x24, 0x18, 0x08, 0xFF));
            }
        x += (w + 1) * p;
    }
    /* the seconds run along the bottom, one lamp each */
    float sw = cols * p / 60;
    for (int i = 0; i < 60; i++)
        gfx_circle(x0 + sw * (i + 0.5f), y0 + 7 * p + 30 * k, 4 * k, i <= t->tm_sec ? RGBA(0xFF, 0xB4, 0x30, 0xFF) : RGBA(0x24, 0x18, 0x08, 0xFF));
    ampm(t, x0 + cols * p + 20 * k, y0 + 7 * p - 30 * k, k, RGBA(0xB0, 0x78, 0x20, 0xFF));
    date_line(t, cx, y0 + 7 * p + 100 * k, k, RGBA(0x98, 0x70, 0x30, 0xFF));
}

/* ---- neon: a pink sign on a brick wall, a tube that sometimes flickers ---- */

static void neon_text(const char *s, float cx, float top, float px, Color core, Color glow, float k, float level)
{
    if (level <= 0.05f) { digits_center(s, cx, top, px, RGBA(0x40, 0x24, 0x34, 0xFF)); return; }   /* an unlit tube */
    float w = font_digits_measure(s, px), h = font_digits_height(px);
    gfx_shadow(cx - w / 2, top, w, h, 30 * k, 90 * k, COLOR_ALPHA(glow, (int)(0x48 * level)));
    for (int i = 0; i < 8; i++) {
        float a = i * PI_F / 4, o = 4 * k;
        digits_center(s, cx + cosf(a) * o, top + sinf(a) * o, px, COLOR_ALPHA(glow, (int)(0x50 * level)));
    }
    digits_center(s, cx, top, px, COLOR_ALPHA(core, (int)(255 * level)));
}

static void neon(float cx, float cy, float k, const struct tm *t)
{
    float W = 1100 * k, H = 640 * k;
    gfx_rect(cx - W, cy - H, 2 * W, 2 * H, RGBA(0x10, 0x0A, 0x10, 0xFF));
    float bw = 120 * k, bh = 46 * k;
    for (int r = 0; r * bh < 2 * H; r++)
        for (int c = -1; c * bw < 2 * W; c++)
            gfx_rrect(cx - W + c * bw + (r & 1 ? bw / 2 : 0) + 3 * k, cy - H + r * bh + 3 * k, bw - 6 * k, bh - 6 * k, 3 * k, RGBA(0x1A, 0x10, 0x16, 0xFF));
    gfx_shadow(cx - 520 * k, cy - 260 * k, 1040 * k, 420 * k, 200 * k, 220 * k, RGBA(0xFF, 0x2A, 0x9A, 0x16));
    char hh[4], mm[4];
    snprintf(hh, sizeof hh, g_app.clock_mode == 1 ? "%02d" : "%d", hour_shown(t));
    snprintf(mm, sizeof mm, "%02d", t->tm_min);
    float px = 360 * k, h = font_digits_height(px), top = cy - h / 2 - 50 * k;
    /* now and then the minutes' tube stutters, as old ones do */
    float ph = fmodf(g_app.time, 11.0f), level = 1;
    if (!g_app.fx_light && ph > 10.2f) level = (int)(ph * 23) % 3 == 0 ? 0.15f : 0.9f;
    Color pink = RGBA(0xFF, 0x3C, 0xB4, 0xFF), core = RGBA(0xFF, 0xD8, 0xF0, 0xFF);
    float hw = font_digits_measure(hh, px), mw = font_digits_measure(mm, px), gap = 120 * k;
    float left = cx - (hw + gap + mw) / 2;
    neon_text(hh, left + hw / 2, top, px, core, pink, k, 1);
    neon_text(mm, left + hw + gap + mw / 2, top, px, core, pink, k, level);
    for (int i = 0; i < 2; i++) {
        float dy = top + h * (i ? 0.72f : 0.28f);
        gfx_circle(left + hw + gap / 2, dy, 26 * k, RGBA(0xFF, 0x3C, 0xB4, 0x30));
        gfx_circle(left + hw + gap / 2, dy, 13 * k, core);
    }
    char d[64]; strftime(d, sizeof d, "%A  %-d %B", t);
    int fs = (int)(40 * k + 0.5f) | FONT_B;
    float dy = top + h + 70 * k, dw = font_measure(d, fs);
    gfx_shadow(cx - dw / 2, dy, dw, font_height(fs), 10 * k, 40 * k, RGBA(0x30, 0xE0, 0xFF, 0x40));
    font_draw_center(d, cx, dy, fs, RGBA(0x9C, 0xF4, 0xFF, 0xFF));
    if (g_app.clock_mode != 1) font_draw(t->tm_hour < 12 ? "AM" : "PM", left + hw + gap + mw + 24 * k, top + 10 * k, (int)(34 * k + 0.5f) | FONT_B, RGBA(0x9C, 0xF4, 0xFF, 0xFF));
}

/* ---- rolling: odometer drums, each digit rolling over as the next one comes ---- */

static void roll(float cx, float cy, float k, const struct tm *t, double sec)
{
    struct tm nx = *t;
    nx.tm_sec += 1;
    mktime(&nx);   /* the time a second on */
    char a[8], b[8];
    const char *fmt = "%02d%02d%02d";
    snprintf(a, sizeof a, fmt, hour_shown(t), t->tm_min, t->tm_sec);
    snprintf(b, sizeof b, fmt, hour_shown(&nx), nx.tm_min, nx.tm_sec);
    float f = (float)(sec - floor(sec));
    f = f < 0.7f ? 0 : (f - 0.7f) / 0.3f;
    f = f * f * (3 - 2 * f);
    float big = 300 * k, small = 170 * k;
    float w[6], gap = 18 * k, sep = 60 * k, total = 0;
    for (int i = 0; i < 6; i++) { w[i] = (i < 4 ? big : small) * 0.62f; total += w[i] + (i == 1 || i == 3 ? sep : i < 5 ? gap : 0); }
    float x = cx - total / 2;
    for (int i = 0; i < 6; i++) {
        float px = i < 4 ? big : small, dh = font_digits_height(px), cell = dh * 1.5f;
        float top = cy - (i < 4 ? dh / 2 : dh / 2 - (big - small) * 0.26f) - 30 * k;
        float wy = top - (cell - dh) / 2;
        gfx_rrect(x - 8 * k, wy - 8 * k, w[i] + 16 * k, cell + 16 * k, 14 * k, RGBA(0x18, 0x18, 0x1C, 0xFF));
        gfx_clip_push(x - 8 * k, wy - 8 * k, w[i] + 16 * k, cell + 16 * k);
        char now[2] = { a[i], 0 }, nxt[2] = { b[i], 0 };
        float off = a[i] != b[i] ? f * cell : 0;
        Color c = i < 4 ? WHITE : THEME_ACCENT;
        digits_center(now, x + w[i] / 2, top - off, px, c);
        if (a[i] != b[i]) digits_center(nxt, x + w[i] / 2, top + cell - off, px, c);
        gfx_gradient_v(x - 8 * k, wy - 8 * k, w[i] + 16 * k, cell * 0.3f, RGBA(0x08, 0x08, 0x0A, 0xFF), RGBA(0x08, 0x08, 0x0A, 0));   /* the drum curves away */
        gfx_gradient_v(x - 8 * k, wy + cell * 0.7f + 8 * k, w[i] + 16 * k, cell * 0.3f, RGBA(0x08, 0x08, 0x0A, 0), RGBA(0x08, 0x08, 0x0A, 0xFF));
        gfx_clip_pop();
        x += w[i] + (i == 1 || i == 3 ? sep : gap);
        if (i == 1 || i == 3) {
            float colx = x - sep / 2, mid = cy - 30 * k;
            gfx_circle(colx, mid - 40 * k, 9 * k, DIM);
            gfx_circle(colx, mid + 40 * k, 9 * k, DIM);
        }
    }
    date_line(t, cx, cy + big * 0.7f, k, DIM);
}

/* ---- orbit: the hours, minutes and seconds as planets round a glowing sun ---- */

static void orbit(float cx, float cy, float k, const struct tm *t, double sec)
{
    for (int i = 0; i < 90; i++) {   /* a few far stars */
        float a = i * 2.39996f, d = (300 + (i * 97 % 640)) * k;
        gfx_circle(cx + cosf(a) * d * 1.5f, cy + sinf(a) * d * 0.9f, (1 + i % 3 * 0.6f) * k + 0.4f, RGBA(0xC8, 0xD0, 0xE8, 0x50 + i % 4 * 0x20));
    }
    double m = t->tm_min + sec / 60.0, h = (t->tm_hour % 12) + m / 60.0;
    struct { float r, ang, size; Color c; } P[3] = {
        { 400 * k, (float)(h / 12 * 2 * PI_F), 34 * k, RGBA(0x5C, 0x9C, 0xFF, 0xFF) },
        { 290 * k, (float)(m / 60 * 2 * PI_F), 24 * k, RGBA(0x4C, 0xE0, 0xB8, 0xFF) },
        { 190 * k, (float)(sec / 60 * 2 * PI_F), 12 * k, RGBA(0xF0, 0xF0, 0xFF, 0xFF) } };
    for (int i = 0; i < 3; i++) {
        for (int d = 0; d < 120; d++) {   /* the orbit, dotted */
            float a = d * PI_F / 60;
            gfx_circle(cx + sinf(a) * P[i].r, cy - cosf(a) * P[i].r, 1.6f * k + 0.3f, RGBA(0x50, 0x58, 0x70, 0x90));
        }
        for (int j = 1; j <= 24; j++) {   /* and a fading trail behind each planet */
            float a = P[i].ang - j * 0.022f;
            gfx_circle(cx + sinf(a) * P[i].r, cy - cosf(a) * P[i].r, P[i].size * 0.35f * (1 - j / 26.0f), COLOR_ALPHA(P[i].c, 0x70 - j * 4));
        }
    }
    gfx_shadow(cx - 70 * k, cy - 70 * k, 140 * k, 140 * k, 70 * k, 90 * k, RGBA(0xFF, 0xA8, 0x30, 0x70));
    gfx_circle(cx, cy, 70 * k, RGBA(0xFF, 0xB8, 0x40, 0xFF));
    gfx_circle(cx - 14 * k, cy - 14 * k, 44 * k, RGBA(0xFF, 0xD8, 0x80, 0xFF));
    for (int i = 0; i < 3; i++) {
        float px = cx + sinf(P[i].ang) * P[i].r, py = cy - cosf(P[i].ang) * P[i].r;
        gfx_shadow(px - P[i].size, py - P[i].size, 2 * P[i].size, 2 * P[i].size, P[i].size, P[i].size, COLOR_ALPHA(P[i].c, 0x50));
        gfx_circle(px, py, P[i].size, P[i].c);
        gfx_circle(px - P[i].size * 0.3f, py - P[i].size * 0.3f, P[i].size * 0.4f, RGBA(0xFF, 0xFF, 0xFF, 0x50));   /* lit from the sun */
    }
    char s[8]; snprintf(s, sizeof s, g_app.clock_mode == 1 ? "%02d:%02d" : "%d:%02d", hour_shown(t), t->tm_min);
    float px = 110 * k;
    digits_center(s, cx + 560 * k, cy - font_digits_height(px) / 2, px, WHITE);
    char d[32]; strftime(d, sizeof d, "%a %-d %b", t);
    font_draw_center(d, cx + 560 * k, cy + font_digits_height(px) / 2 + 14 * k, (int)(28 * k + 0.5f), DIM);
}

/* ---- daylight: the sky of the hour, the sun or the moon crossing it ---- */

static void day_sky(float cx, float cy, float k, const struct tm *t, double sec)
{
    static const struct { float h; Color top, bottom; } SKY[] = {
        { 0, RGBA(0x04, 0x06, 0x12, 0xFF), RGBA(0x0C, 0x14, 0x30, 0xFF) }, { 5, RGBA(0x16, 0x1A, 0x44, 0xFF), RGBA(0x5A, 0x3C, 0x6A, 0xFF) },
        { 6.5f, RGBA(0x3C, 0x50, 0x9A, 0xFF), RGBA(0xF4, 0x9A, 0x74, 0xFF) }, { 9, RGBA(0x4A, 0x9C, 0xE8, 0xFF), RGBA(0xC8, 0xE4, 0xF4, 0xFF) },
        { 15, RGBA(0x2C, 0x7C, 0xD8, 0xFF), RGBA(0xA4, 0xD4, 0xF4, 0xFF) }, { 18, RGBA(0x36, 0x4A, 0x9C, 0xFF), RGBA(0xF6, 0xA0, 0x58, 0xFF) },
        { 19.5f, RGBA(0x24, 0x1C, 0x50, 0xFF), RGBA(0xD8, 0x5A, 0x5C, 0xFF) }, { 21, RGBA(0x08, 0x0A, 0x22, 0xFF), RGBA(0x1C, 0x22, 0x4C, 0xFF) },
        { 24, RGBA(0x04, 0x06, 0x12, 0xFF), RGBA(0x0C, 0x14, 0x30, 0xFF) } };
    float hr = t->tm_hour + t->tm_min / 60.0f + (float)sec / 3600;
    int i = 0;
    while (i < 7 && hr >= SKY[i + 1].h) i++;
    float f = (hr - SKY[i].h) / (SKY[i + 1].h - SKY[i].h);
    Color top = color_lerp(SKY[i].top, SKY[i + 1].top, f), bot = color_lerp(SKY[i].bottom, SKY[i + 1].bottom, f);
    float W = 1100 * k, H = 640 * k, horizon = cy + H * 0.62f;
    gfx_gradient_v(cx - W, cy - H, 2 * W, horizon - (cy - H), top, bot);
    int night = hr < 6 || hr >= 19.5f;
    if (night) for (int s = 0; s < 80; s++)
        gfx_circle(cx - W + (s * 233 % 1920) * k, cy - H + (s * 137 % 560) * k, (1 + s % 3 * 0.5f) * k, RGBA(0xFF, 0xFF, 0xFF, 0x40 + (int)(0x60 * (0.5f + 0.5f * sinf(g_app.time * 1.3f + s)))));
    /* the sun from six to six, the moon through the night, on the same arc */
    float p = night ? fmodf(hr - 18 + 24, 24) / 12 : (hr - 6) / 12;
    if (p >= 0 && p <= 1) {
        float bx = cx + (p - 0.5f) * W * 1.6f, by = horizon - sinf(PI_F * p) * H * 1.05f;
        if (night) {
            gfx_shadow(bx - 40 * k, by - 40 * k, 80 * k, 80 * k, 40 * k, 60 * k, RGBA(0xD8, 0xE4, 0xFF, 0x40));
            gfx_circle(bx, by, 40 * k, RGBA(0xEE, 0xF0, 0xF6, 0xFF));
            gfx_circle(bx + 16 * k, by - 10 * k, 36 * k, top);   /* a crescent */
        } else {
            gfx_shadow(bx - 60 * k, by - 60 * k, 120 * k, 120 * k, 60 * k, 140 * k, RGBA(0xFF, 0xE0, 0x90, 0x70));
            gfx_circle(bx, by, 60 * k, RGBA(0xFF, 0xF2, 0xC8, 0xFF));
        }
    }
    /* soft hills, farther ones paler */
    for (int l = 0; l < 3; l++) {
        Color c = color_lerp(bot, RGBA(0x04, 0x06, 0x0A, 0xFF), 0.55f + l * 0.2f);
        for (int x = 0; x < 64; x++) {
            float u = x / 64.0f, hh = H * (0.14f + 0.05f * l + 0.05f * sinf(u * (5 + l * 3) + l * 2) + 0.03f * sinf(u * 13 + l));
            gfx_rect(cx - W + u * 2 * W, horizon - hh + l * 40 * k, 2 * W / 64 + 1, cy + H - (horizon - hh + l * 40 * k), c);
        }
    }
    char s[8]; snprintf(s, sizeof s, g_app.clock_mode == 1 ? "%02d:%02d" : "%d:%02d", hour_shown(t), t->tm_min);
    float px = 300 * k, dh = font_digits_height(px), y = cy - dh / 2 - 90 * k;
    float w = font_digits_measure(s, px);
    font_digits_draw(s, cx - w / 2 + 4 * k, y + 6 * k, px, RGBA(0, 0, 0, 0x40));
    font_digits_draw(s, cx - w / 2, y, px, RGBA(0xFF, 0xFF, 0xFF, 0xF4));
    ampm(t, cx + w / 2 + 18 * k, y + 6 * k, k, RGBA(0xFF, 0xFF, 0xFF, 0xC0));
    date_line(t, cx, y + dh + 40 * k, k, RGBA(0xFF, 0xFF, 0xFF, 0xD0));
}

void clockface_draw(int face, float cx, float cy, float k, const struct tm *t, double sec)
{
    switch (face) {
    case CLOCKFACE_FLIP: flip(cx, cy, k, t); break;
    case CLOCKFACE_ANALOG: analog(cx, cy, k, t, sec); break;
    case CLOCKFACE_WORDS: words(cx, cy, k, t); break;
    case CLOCKFACE_RINGS: rings(cx, cy, k, t, sec); break;
    case CLOCKFACE_NIXIE: nixie(cx, cy, k, t); break;
    case CLOCKFACE_STACKED: stacked(cx, cy, k, t); break;
    case CLOCKFACE_SEGMENT: segment(cx, cy, k, t); break;
    case CLOCKFACE_DOTS: dots(cx, cy, k, t); break;
    case CLOCKFACE_NEON: neon(cx, cy, k, t); break;
    case CLOCKFACE_ROLL: roll(cx, cy, k, t, sec); break;
    case CLOCKFACE_ORBIT: orbit(cx, cy, k, t, sec); break;
    case CLOCKFACE_DAYLIGHT: day_sky(cx, cy, k, t, sec); break;
    default: digital(cx, cy, k, t); break;
    }
}
