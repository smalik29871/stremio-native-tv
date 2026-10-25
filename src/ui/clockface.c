/* Clock faces: digital, flip cards, a Braun-style dial, a word clock (QLOCKTWO's
 * English grid), polar rings, nixie tubes, StandBy-style stacked digits, a red LED
 * alarm clock, an amber dot-matrix board, a neon sign, odometer drums, planets in
 * orbit, a sky that follows the day, a Swiss station clock, liquid digits that flow
 * into the next, the weather outside, an aurora, a solar system telling the time, the day as
 * an orbit with the prayers on it, the horizon of the hour, and the planets as they stand. Plain shapes and the big digit atlas only,
 * so every face is sharp at any size. */
#include "clockface.h"
#include "font.h"
#include "gfx.h"
#include "theme.h"
#include "../app.h"
#include "../weather.h"
#include "../breaks.h"
#include "../suntimes.h"
#include "../platform/platform.h"
#include <time.h>
#include <stdlib.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

const char *const CLOCKFACE_NAMES[CLOCKFACE_COUNT] = { "Digital", "Flip", "Analog", "Words", "Rings", "Nixie", "Stacked",
                                                           "Segment", "Dot matrix", "Neon", "Rolling", "Orbit", "Daylight",
                                                           "Station", "Liquid", "Weather", "Aurora", "Planets", "Prayer orbit", "Horizon", "Orrery" };

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

/* ---- station: the Swiss railway clock. The red second hand goes round in 58.5 s and
 * waits at twelve; then the minute hand jumps, with a little kick. ---- */

static void station(float cx, float cy, float k, const struct tm *t, double sec)
{
    float R = 380 * k;
    Color ink = RGBA(0x16, 0x16, 0x18, 0xFF), red = RGBA(0xE2, 0x1E, 0x26, 0xFF);
    gfx_shadow(cx - R, cy - R + 18 * k, 2 * R, 2 * R, R, 60 * k, RGBA(0, 0, 0, 0xA0));
    gfx_circle(cx, cy, R + 16 * k, RGBA(0x2C, 0x2C, 0x32, 0xFF));
    gfx_circle(cx, cy, R, RGBA(0xF5, 0xF5, 0xF1, 0xFF));
    for (int i = 0; i < 60; i++) {
        float a = i * PI_F / 30, sn = sinf(a), c = -cosf(a);
        int big = i % 5 == 0;
        float r0 = R * (big ? 0.70f : 0.87f), r1 = R * 0.95f;
        gfx_line(cx + sn * r0, cy + c * r0, cx + sn * r1, cy + c * r1, (big ? 26 : 8) * k, ink);
    }
    float since = (float)sec;   /* into the minute */
    float kick = since < 0.6f ? 0.03f * sinf(since * 26) * expf(-since * 9) : 0;
    float ma = (t->tm_min / 60.0f) * 2 * PI_F + kick;
    float ha = ((t->tm_hour % 12) + t->tm_min / 60.0f) / 12 * 2 * PI_F;
    float sa = since < 58.5f ? since / 58.5f * 2 * PI_F : 0;
    hand(cx, cy, ha, R * 0.14f, R * 0.62f, R * 0.085f, ink);
    hand(cx, cy, ma, R * 0.14f, R * 0.92f, R * 0.062f, ink);
    hand(cx, cy, sa, R * 0.30f, R * 0.60f, R * 0.02f, red);
    gfx_circle(cx + sinf(sa) * R * 0.60f, cy - cosf(sa) * R * 0.60f, R * 0.085f, red);
    gfx_circle(cx, cy, R * 0.03f, red);
    date_line(t, cx, cy + R + 60 * k, k, DIM);
    app_redraw();   /* the second hand sweeps */
}

/* ---- liquid: each digit a string of beads that flows into the next number ---- */

typedef struct { int n; float p[18][2]; } LqPath;   /* in a 0.6 x 1 box */
static const LqPath LQ_PATH[10] = {
    { 17, { {.30f,0}, {.45f,.04f}, {.55f,.16f}, {.58f,.33f}, {.58f,.5f}, {.58f,.67f}, {.55f,.84f}, {.45f,.96f}, {.30f,1}, {.15f,.96f}, {.05f,.84f}, {.02f,.67f}, {.02f,.5f}, {.02f,.33f}, {.05f,.16f}, {.15f,.04f}, {.30f,0} } },
    { 3, { {.12f,.2f}, {.36f,0}, {.36f,1} } },
    { 8, { {.04f,.22f}, {.14f,.06f}, {.30f,0}, {.46f,.06f}, {.55f,.22f}, {.50f,.42f}, {.04f,1}, {.58f,1} } },
    { 11, { {.04f,.08f}, {.28f,0}, {.50f,.08f}, {.55f,.24f}, {.45f,.41f}, {.24f,.47f}, {.46f,.53f}, {.58f,.71f}, {.52f,.9f}, {.30f,1}, {.04f,.92f} } },
    { 4, { {.46f,1}, {.46f,0}, {.02f,.68f}, {.60f,.68f} } },
    { 9, { {.55f,0}, {.10f,0}, {.06f,.42f}, {.30f,.37f}, {.50f,.45f}, {.58f,.66f}, {.52f,.88f}, {.30f,1}, {.04f,.92f} } },
    { 12, { {.50f,.04f}, {.30f,0}, {.12f,.12f}, {.04f,.45f}, {.06f,.80f}, {.20f,.97f}, {.40f,.98f}, {.56f,.82f}, {.56f,.62f}, {.40f,.50f}, {.20f,.50f}, {.05f,.62f} } },
    { 3, { {.02f,0}, {.60f,0}, {.24f,1} } },
    { 17, { {.30f,.47f}, {.10f,.38f}, {.08f,.20f}, {.18f,.04f}, {.30f,0}, {.42f,.04f}, {.52f,.20f}, {.50f,.38f}, {.30f,.47f}, {.10f,.58f}, {.04f,.78f}, {.14f,.95f}, {.30f,1}, {.46f,.95f}, {.56f,.78f}, {.50f,.58f}, {.30f,.47f} } },
    { 12, { {.55f,.38f}, {.40f,.50f}, {.20f,.50f}, {.05f,.38f}, {.05f,.18f}, {.20f,.02f}, {.40f,.02f}, {.55f,.18f}, {.56f,.50f}, {.46f,.85f}, {.26f,1}, {.08f,.95f} } },
};
enum { LQ_N = 40 };

/* n beads spread evenly along a digit's path */
static void lq_beads(int d, float out[LQ_N][2])
{
    const LqPath *pa = &LQ_PATH[d];
    float len[18], total = 0;
    for (int i = 1; i < pa->n; i++) { float dx = pa->p[i][0] - pa->p[i - 1][0], dy = pa->p[i][1] - pa->p[i - 1][1]; len[i] = sqrtf(dx * dx + dy * dy); total += len[i]; }
    int seg = 1; float acc = 0;
    for (int j = 0; j < LQ_N; j++) {
        float want = total * j / (LQ_N - 1);
        while (seg < pa->n - 1 && acc + len[seg] < want) { acc += len[seg]; seg++; }
        float u = len[seg] > 0 ? (want - acc) / len[seg] : 0;
        if (u > 1) u = 1;
        out[j][0] = pa->p[seg - 1][0] + (pa->p[seg][0] - pa->p[seg - 1][0]) * u;
        out[j][1] = pa->p[seg - 1][1] + (pa->p[seg][1] - pa->p[seg - 1][1]) * u;
    }
}

static struct { int cur[4], old[4]; float at[4]; int init; } LQ;

static void liquid(float cx, float cy, float k, const struct tm *t)
{
    int h = hour_shown(t), want[4] = { h >= 10 || g_app.clock_mode == 1 ? h / 10 : -1, h % 10, t->tm_min / 10, t->tm_min % 10 };
    if (!LQ.init) { for (int i = 0; i < 4; i++) { LQ.cur[i] = want[i]; LQ.old[i] = -1; LQ.at[i] = -100; } LQ.init = 1; }
    float H = 360 * k, dw = H * 0.6f, gap = H * 0.16f, colon = H * 0.3f;
    float total = 4 * dw + 3 * gap + colon, x0 = cx - total / 2, y0 = cy - H / 2 - 40 * k, T = g_app.time;
    Color body = RGBA(0x86, 0xDC, 0xFF, 0xFF), deep = RGBA(0x2E, 0x7C, 0xC8, 0xFF);
    for (int i = 0; i < 4; i++) {
        if (want[i] != LQ.cur[i]) { LQ.old[i] = LQ.cur[i]; LQ.cur[i] = want[i]; LQ.at[i] = T; }
        float p = (T - LQ.at[i]) / 1.1f; if (p > 1) p = 1; if (p < 0) p = 0;
        float bx = x0 + i * (dw + gap) + (i >= 2 ? colon : 0);
        float a[LQ_N][2], b[LQ_N][2];
        int from = p < 1 ? LQ.old[i] : LQ.cur[i], to = LQ.cur[i];
        if (to >= 0) lq_beads(to, b);
        if (from >= 0) lq_beads(from, a);
        if (from < 0 && to < 0) continue;
        float P[LQ_N][3];   /* where each bead is now, and its size */
        for (int j = 0; j < LQ_N; j++) {
            float q = (p - (float)j / LQ_N * 0.35f) / 0.65f; q = q < 0 ? 0 : q > 1 ? 1 : q * q * (3 - 2 * q);
            float ux, uy, r = H * 0.07f;
            if (from < 0) { ux = b[j][0]; uy = b[j][1]; r *= q; }
            else if (to < 0) { ux = a[j][0]; uy = a[j][1]; r *= 1 - q; }
            else { ux = a[j][0] + (b[j][0] - a[j][0]) * q; uy = a[j][1] + (b[j][1] - a[j][1]) * q; r *= 1 + 0.35f * sinf(PI_F * q); }
            float wob = 0.005f * sinf(T * 1.6f + j * 0.5f + i * 1.3f);
            P[j][0] = bx + (ux + wob) * dw / 0.6f; P[j][1] = y0 + (uy + wob * 0.6f) * H; P[j][2] = r;
        }
        /* one tube through the beads: its shade, its body, a streak of shine along the top */
        for (int pass = 0; pass < 3; pass++)
            for (int j = 0; j + 1 < LQ_N; j++) {
                float r = (P[j][2] + P[j + 1][2]) / 2;
                if (r < 0.5f) continue;
                float dx = pass == 0 ? r * 0.12f : pass == 2 ? -r * 0.28f : 0, dy = pass == 0 ? r * 0.2f : pass == 2 ? -r * 0.32f : 0;
                float th = pass == 0 ? r * 2.16f : pass == 1 ? r * 2 : r * 0.5f;
                Color c = pass == 0 ? deep : pass == 1 ? body : RGBA(0xD4, 0xF2, 0xFF, 0xFF);   /* opaque: overlapping caps would bead */
                gfx_line(P[j][0] + dx, P[j][1] + dy, P[j + 1][0] + dx, P[j + 1][1] + dy, th, c);
            }
    }
    float ccx = x0 + 2 * dw + gap + colon / 2 + gap / 2, pulse = 1 + 0.08f * sinf(T * 3.1f);
    for (int d = -1; d <= 1; d += 2) {
        float py = y0 + H * (0.5f + d * 0.18f);
        gfx_circle(ccx, py, H * 0.05f * pulse, body);
        gfx_circle(ccx - H * 0.016f, py - H * 0.018f, H * 0.017f, RGBA(0xFF, 0xFF, 0xFF, 0x70));
    }
    date_line(t, cx, y0 + H + 70 * k, k, DIM);
    app_redraw();   /* it never quite stops moving */
}

/* ---- weather: the time over the sky outside, as it is now ---- */

static void cloud(float x, float y, float s, Color c)
{
    gfx_circle(x, y, 60 * s, c); gfx_circle(x + 70 * s, y - 30 * s, 80 * s, c); gfx_circle(x + 160 * s, y - 10 * s, 70 * s, c);
    gfx_circle(x + 230 * s, y + 10 * s, 50 * s, c); gfx_rrect(x - 40 * s, y, 320 * s, 60 * s, 30 * s, c);
}

static void weather_face(float cx, float cy, float k, const struct tm *t)
{
    const Weather *w = weather_now();
    int day = w->known ? w->is_day : t->tm_hour >= 6 && t->tm_hour < 19, kind = w->known ? w->kind : WX_CLEAR;
    static const unsigned SKY[8][4] = {   /* day top, day bottom, night top, night bottom */
        { 0x2F7FD8FF, 0x9FD2F4FF, 0x050A1EFF, 0x16224AFF }, { 0x4A86C8FF, 0xB8D4EAFF, 0x0A1026FF, 0x222C4EFF },
        { 0x6E7C8CFF, 0xB4BCC4FF, 0x14181EFF, 0x2A3038FF }, { 0x9AA4ACFF, 0xC8CCCEFF, 0x22262AFF, 0x3A3E42FF },
        { 0x56636FFF, 0x939DA8FF, 0x0E1218FF, 0x232A34FF }, { 0x4E5C6CFF, 0x8A96A2FF, 0x0C1016FF, 0x202630FF },
        { 0x8E9EB0FF, 0xDCE4ECFF, 0x1A2030FF, 0x3A4458FF }, { 0x2E3440FF, 0x5A6270FF, 0x07090EFF, 0x1A1E28FF } };
    float W = 960 * k, Hh = 540 * k, x0 = cx - W, y0 = cy - Hh, T = g_app.time, bleed = 60 * k;   /* past the edges: the face drifts against burn-in */
    gfx_gradient_v(x0 - bleed, y0 - bleed, 2 * (W + bleed), 2 * (Hh + bleed), SKY[kind][day ? 0 : 2], SKY[kind][day ? 1 : 3]);
    if (kind <= WX_PARTLY) {
        if (day) {   /* the sun, its rays turning slowly */
            float sx = cx + 560 * k, sy = cy - 300 * k;
            gfx_shadow(sx - 90 * k, sy - 90 * k, 180 * k, 180 * k, 90 * k, 160 * k, RGBA(0xFF, 0xE6, 0x9A, 0x80));
            for (int i = 0; i < 12; i++) { float a = i * PI_F / 6 + T * 0.05f; gfx_line(sx + cosf(a) * 120 * k, sy + sinf(a) * 120 * k, sx + cosf(a) * 165 * k, sy + sinf(a) * 165 * k, 8 * k, RGBA(0xFF, 0xEE, 0xB0, 0xB0)); }
            gfx_circle(sx, sy, 90 * k, RGBA(0xFF, 0xF2, 0xC4, 0xFF));
        } else {
            for (int s = 0; s < 90; s++)
                gfx_circle(x0 + (s * 233 % 1920) * k, y0 + (s * 137 % 700) * k, (1 + s % 3 * 0.5f) * k, RGBA(0xFF, 0xFF, 0xFF, 0x40 + (int)(0x60 * (0.5f + 0.5f * sinf(T * 1.3f + s)))));
            float mx = cx + 560 * k, my = cy - 300 * k;
            gfx_shadow(mx - 60 * k, my - 60 * k, 120 * k, 120 * k, 60 * k, 90 * k, RGBA(0xD8, 0xE4, 0xFF, 0x40));
            gfx_circle(mx, my, 60 * k, RGBA(0xEE, 0xF0, 0xF6, 0xFF));
            gfx_circle(mx + 24 * k, my - 14 * k, 54 * k, SKY[kind][2]);
        }
    }
    int nc = kind == WX_CLEAR ? 0 : kind == WX_PARTLY ? 3 : kind == WX_FOG ? 0 : 6;
    Color cc = kind >= WX_DRIZZLE && kind != WX_SNOW ? RGBA(0x9A, 0xA2, 0xAC, day ? 0xE8 : 0x60) : RGBA(0xFF, 0xFF, 0xFF, day ? 0xD8 : 0x48);
    for (int i = 0; i < nc; i++) {   /* clouds drifting across, the far ones slower */
        float sp = 14 + i * 5, span = 2 * W + 700 * k;
        float x = fmodf(i * 431 * k + T * sp * k, span) + x0 - 400 * k, y = y0 + (90 + (i * 97) % 300) * k;
        cloud(x, y, (0.8f + (i % 3) * 0.25f) * k, cc);
    }
    if (kind == WX_FOG)
        for (int i = 0; i < 6; i++) {
            float y = y0 + (260 + i * 120) * k, x = fmodf(T * (20 + i * 6) * k, 400 * k);
            gfx_rrect(x0 - 400 * k + x, y, 2 * W + 400 * k, 90 * k, 45 * k, RGBA(0xFF, 0xFF, 0xFF, day ? 0x30 : 0x18));
        }
    if (kind == WX_RAIN || kind == WX_DRIZZLE || kind == WX_STORM) {
        int n = kind == WX_DRIZZLE ? 70 : 150;
        float spd = kind == WX_DRIZZLE ? 520 : 980, len = kind == WX_DRIZZLE ? 16 : 34;
        for (int i = 0; i < n; i++) {
            float x = x0 + ((i * 97) % 1920) * k, y = y0 + fmodf(i * 53.0f + T * spd, 1080) * k;
            gfx_line(x, y, x - 8 * k, y + len * k, 2.5f * k, RGBA(0xD8, 0xE4, 0xF0, 0x70));
        }
    }
    if (kind == WX_SNOW)
        for (int i = 0; i < 120; i++) {
            float x = x0 + (((i * 97) % 1920) + 30 * sinf(T * 0.8f + i)) * k, y = y0 + fmodf(i * 61.0f + T * (60 + i % 5 * 18), 1080) * k;
            gfx_circle(x, y, (3 + i % 3) * k, RGBA(0xFF, 0xFF, 0xFF, 0xD0));
        }
    if (kind == WX_STORM && fmodf(T, 7.3f) < 0.14f) gfx_rect(x0, y0, 2 * W, 2 * Hh, RGBA(0xFF, 0xFF, 0xFF, 0x50));   /* lightning */
    char s[8]; snprintf(s, sizeof s, g_app.clock_mode == 1 ? "%02d:%02d" : "%d:%02d", hour_shown(t), t->tm_min);
    float px = 320 * k, dh = font_digits_height(px), y = cy - dh / 2 - 80 * k, wdt = font_digits_measure(s, px);
    font_digits_draw(s, cx - wdt / 2 + 5 * k, y + 7 * k, px, RGBA(0, 0, 0, 0x48));
    font_digits_draw(s, cx - wdt / 2, y, px, RGBA(0xFF, 0xFF, 0xFF, 0xF6));
    ampm(t, cx + wdt / 2 + 18 * k, y + 6 * k, k, RGBA(0xFF, 0xFF, 0xFF, 0xC0));
    char line[160];
    if (w->known) snprintf(line, sizeof line, "%.0f\xC2\xB0  %s%s%s", w->temp, weather_kind_name(kind), w->place[0] ? "   \xC2\xB7   " : "", w->place);
    else snprintf(line, sizeof line, "Finding the weather\xE2\x80\xA6");
    int fs = (int)(44 * k + 0.5f) | FONT_B;
    font_draw_center(line, cx + 2 * k, y + dh + 42 * k, fs, RGBA(0, 0, 0, 0x50));
    font_draw_center(line, cx, y + dh + 40 * k, fs, RGBA(0xFF, 0xFF, 0xFF, 0xF0));
    date_line(t, cx, y + dh + 110 * k, k, RGBA(0xFF, 0xFF, 0xFF, 0xC8));
    app_redraw();   /* the sky moves */
}

/* ---- aurora: curtains of light drifting over a dark sky ---- */

static void aurora(float cx, float cy, float k, const struct tm *t)
{
    float W = 960 * k, Hh = 540 * k, x0 = cx - W, y0 = cy - Hh, T = g_app.time, bleed = 60 * k;   /* past the edges: the face drifts against burn-in */
    gfx_gradient_v(x0 - bleed, y0 - bleed, 2 * (W + bleed), 2 * (Hh + bleed), RGBA(0x02, 0x04, 0x0E, 0xFF), RGBA(0x06, 0x12, 0x24, 0xFF));
    for (int s = 0; s < 80; s++)
        gfx_circle(x0 + (s * 233 % 1920) * k, y0 + (s * 137 % 800) * k, (1 + s % 3 * 0.5f) * k, RGBA(0xFF, 0xFF, 0xFF, 0x30 + (int)(0x50 * (0.5f + 0.5f * sinf(T * 1.1f + s)))));
    static const unsigned COL[3] = { 0x3CFFA0u, 0x3CD8FFu, 0xA070FFu };
    enum { STRIPS = 160 };
    float sw = 2 * (W + bleed) / STRIPS;
    x0 -= bleed;
    for (int l = 0; l < 3; l++) {
        Color c = (Color)(COL[l] << 8);
        for (int i = 0; i < STRIPS; i++) {
            float fi = (float)i;
            float base = y0 + Hh * (0.62f + 0.10f * l) + Hh * 0.16f * sinf(fi * 0.05f + T * 0.21f + l * 1.7f) + Hh * 0.06f * sinf(fi * 0.13f - T * 0.33f + l);
            float hgt = Hh * (0.55f + 0.2f * sinf(fi * 0.03f + T * 0.27f + l * 2.1f));
            float a = 0.25f + 0.45f * sinf(fi * 0.04f + T * 0.31f + l * 1.3f) + 0.12f * sinf(fi * 0.17f - T * 0.5f);   /* rays, gently */
            if (a <= 0.02f) continue;
            if (a > 1) a = 1;
            int al = (int)(a * (l == 2 ? 0x46 : 0x60));
            gfx_gradient_v(x0 + i * sw, base - hgt, sw + 1, hgt, COLOR_ALPHA(c, 0), COLOR_ALPHA(c, al));
            gfx_gradient_v(x0 + i * sw, base, sw + 1, hgt * 0.12f, COLOR_ALPHA(c, al), COLOR_ALPHA(c, 0));
        }
    }
    for (int x = 0; x < 240; x++) {   /* the hills, black against it */
        float u = x / 240.0f, hh = Hh * (0.16f + 0.05f * sinf(u * 7 + 1) + 0.03f * sinf(u * 19));
        gfx_rect(x0 + u * 2 * (W + bleed), y0 + 2 * Hh - hh, 2 * (W + bleed) / 240 + 1, hh + bleed, RGBA(0x01, 0x02, 0x05, 0xFF));
    }
    char s[8]; snprintf(s, sizeof s, g_app.clock_mode == 1 ? "%02d:%02d" : "%d:%02d", hour_shown(t), t->tm_min);
    float px = 280 * k, dh = font_digits_height(px), y = cy - dh / 2 - 140 * k, wdt = font_digits_measure(s, px);
    font_digits_draw(s, cx - wdt / 2, y, px, RGBA(0xF0, 0xFF, 0xF8, 0xE8));
    ampm(t, cx + wdt / 2 + 18 * k, y + 6 * k, k, RGBA(0xE0, 0xFF, 0xF0, 0xA0));
    date_line(t, cx, y + dh + 40 * k, k, RGBA(0xE0, 0xFF, 0xF0, 0xB0));
    app_redraw();   /* the lights drift */
}

/* ---- the sky's numbers, shared by the faces below ---- */

static double days_j2000(const struct tm *t)   /* days since J2000.0, from the local time shown */
{
    struct tm c = *t; c.tm_isdst = -1;
    return (mktime(&c) / 86400.0 + 2440587.5) - 2451545.0;
}

static float moon_phase(const struct tm *t)   /* 0 new, 0.5 full, 1 new again */
{
    double p = fmod((days_j2000(t) - 5.1) / 29.530588853, 1.0);   /* a new moon on 2000-01-06 18:14 UTC */
    return (float)(p < 0 ? p + 1 : p);
}

typedef struct { int stamp; int ok; SunDay d; int n; BreakEvent ev[8]; char next[64]; } SkyDay;
static SkyDay g_sky = { .stamp = -1 };

/* Today's sun and prayer times, worked out again once a minute. */
static const SkyDay *sky_today(const struct tm *t)
{
    int stamp = t->tm_yday * 1440 + t->tm_hour * 60 + t->tm_min;
    if (g_sky.stamp == stamp) return &g_sky;
    g_sky.stamp = stamp;
    g_sky.ok = breaks_location_known();
    if (g_sky.ok) sun_compute(t->tm_year + 1900, t->tm_mon + 1, t->tm_mday, g_app.break_lat, g_app.break_lon, breaks_tz_hours(), breaks_method(), g_app.break_asr_hanafi, &g_sky.d);
    BreakEvent all[16]; int n = breaks_today(all, 16);
    g_sky.n = 0;
    for (int i = 0; i < n && g_sky.n < 8; i++) if (all[i].kind == BRK_KIND_PRAYER) g_sky.ev[g_sky.n++] = all[i];
    breaks_next_prayer(g_sky.next, sizeof g_sky.next);
    return &g_sky;
}

/* A world lit by a sun at (sx, sy): the night side, the day side, a highlight. */
static void world(float px, float py, float r, Color c, float sx, float sy)
{
    float dx = sx - px, dy = sy - py, d = sqrtf(dx * dx + dy * dy);
    if (d < 1) d = 1;
    dx /= d; dy /= d;
    gfx_circle(px, py, r, color_lerp(c, RGBA(0x06, 0x08, 0x14, 0xFF), 0.72f));
    gfx_circle(px + dx * r * 0.2f, py + dy * r * 0.2f, r * 0.78f, c);
    gfx_circle(px + dx * r * 0.42f, py + dy * r * 0.42f, r * 0.36f, RGBA(0xFF, 0xFF, 0xFF, 0x48));
}

static void far_stars(float cx, float cy, float k, int n)
{
    for (int i = 0; i < n; i++) {
        float a = i * 2.39996f, d = (300 + (i * 97 % 640)) * k, tw = 0.5f + 0.5f * sinf(g_app.time * (0.7f + i % 5 * 0.3f) + i);
        gfx_circle(cx + cosf(a) * d * 1.5f, cy + sinf(a) * d * 0.9f, (1 + i % 3 * 0.6f) * k + 0.4f, RGBA(0xC8, 0xD0, 0xE8, 0x30 + (int)(tw * (0x40 + i % 4 * 0x18))));
    }
}

/* The sun: a pulsing corona, flares that lengthen and shorten, the bright disc. */
static void sun_disc(float cx, float cy, float r, float k)
{
    float T = g_app.time;
    for (int l = 0; l < 3; l++) {
        float e = r * (1.6f + l * 0.7f) * (1 + 0.04f * sinf(T * 0.8f + l));
        gfx_shadow(cx - e, cy - e, 2 * e, 2 * e, e, e * 0.9f, RGBA(0xFF, 0xA8, 0x30, 0x58 - l * 0x18));
    }
    for (int i = 0; i < 10; i++) {
        float a = i * 2 * PI_F / 10 + T * 0.05f, len = r * (0.9f + 0.6f * sinf(T * 0.7f + i * 1.9f));
        gfx_line(cx + cosf(a) * r, cy + sinf(a) * r, cx + cosf(a) * (r + len), cy + sinf(a) * (r + len), 1.5f * k, RGBA(0xFF, 0xC8, 0x60, 0x1C));
    }
    gfx_circle(cx, cy, r, RGBA(0xFF, 0xB8, 0x40, 0xFF));
    gfx_circle(cx - r * 0.2f, cy - r * 0.2f, r * 0.62f, RGBA(0xFF, 0xD8, 0x80, 0xFF));
}

/* The time and date beside a face that fills the middle: a column at the right on a TV, under it on a phone. */
static void side_time(float cx, float cy, float k, const struct tm *t, const char *line)
{
    if (!SNTV_PHONE) cx += 50 * k;
    char s[8]; snprintf(s, sizeof s, g_app.clock_mode == 1 ? "%02d:%02d" : "%d:%02d", hour_shown(t), t->tm_min);
    float px = 110 * k, dh = font_digits_height(px), w = font_digits_measure(s, px);
    float x = SNTV_PHONE ? cx : cx + 600 * k, y = SNTV_PHONE ? cy + 500 * k : cy - dh / 2 - 20 * k;
    font_digits_draw(s, x - w / 2, y, px, WHITE);
    ampm(t, x + w / 2 + 12 * k, y + 4 * k, k * 0.8f, DIM);
    char d[32]; strftime(d, sizeof d, "%a %-d %b", t);
    font_draw_center(d, x, y + dh + 14 * k, (int)(28 * k + 0.5f), DIM);
    if (line && *line) font_draw_center(line, x, y + dh + 52 * k, (int)(26 * k + 0.5f), RGBA(0xFF, 0xD0, 0x80, 0xE0));
}

/* ---- planets: time as a solar system seen from a little above. The hour planet and its moon
 * on the outer orbit, the minute planet inside, a comet for the seconds; the hours ticked round
 * the outer orbit; an asteroid belt; every world lit on the side that faces the sun. ---- */

#define TILT 0.56f   /* the orbits seen from above at an angle */

static void tilted(float cx, float cy, float r, float a, float *x, float *y) { *x = cx + sinf(a) * r; *y = cy - cosf(a) * r * TILT; }

static void planets(float cx, float cy, float k, const struct tm *t, double sec)
{
    float T = g_app.time;
    far_stars(cx, cy, k, 110);
    double m = t->tm_min + sec / 60.0, h = (t->tm_hour % 12) + m / 60.0;
    float ha = (float)(h / 12 * 2 * PI_F), ma = (float)(m / 60 * 2 * PI_F), sa = (float)(sec / 60 * 2 * PI_F);
    float RH = 420 * k, RM = 275 * k, RS = 165 * k, RB = 345 * k;
    for (int o = 0; o < 3; o++) {   /* the orbits, dotted, fainter at the back */
        float r = o == 0 ? RH : o == 1 ? RM : RS;
        for (int d = 0; d < 144; d++) {
            float a = d * 2 * PI_F / 144, x, y; tilted(cx, cy, r, a, &x, &y);
            gfx_circle(x, y, (1.2f + 0.6f * (1 - cosf(a)) / 2) * k + 0.3f, RGBA(0x50, 0x58, 0x70, 0x60 + (int)(0x40 * (1 - cosf(a)) / 2)));
        }
    }
    for (int j = 0; j < 12; j++) {   /* the hours round the outer orbit, the current one lit */
        float a = j * PI_F / 6, x, y; tilted(cx, cy, RH, a, &x, &y);
        int big = j % 3 == 0, now = j == t->tm_hour % 12;
        gfx_circle(x, y, (big ? 5 : 3) * k, now ? RGBA(0x9C, 0xC8, 0xFF, 0xFF) : RGBA(0x80, 0x90, 0xB8, big ? 0xD0 : 0x90));
        if (big) { char n[4]; snprintf(n, sizeof n, "%d", j ? j : 12); font_draw_center(n, x, y - (j == 0 ? 42 : j == 6 ? 14 : 28) * k, (int)(22 * k + 0.5f), RGBA(0x80, 0x90, 0xB8, 0xC0)); }
    }
    for (int i = 0; i < 170; i++) {   /* the asteroid belt turns, slowly */
        float a = i * 2.39996f + T * 0.012f, r = RB + (i * 37 % 40 - 20) * k, x, y; tilted(cx, cy, r, a, &x, &y);
        gfx_circle(x, y, (0.8f + i % 3 * 0.6f) * k + 0.3f, RGBA(0xA0, 0x98, 0x90, 0x50 + i % 4 * 0x18));
    }
    /* the worlds: whatever is on the far half goes behind the sun */
    struct { float x, y, r; Color c; int moon; float mx, my; } W[2];
    tilted(cx, cy, RH, ha, &W[0].x, &W[0].y); W[0].r = 30 * k * (0.78f + 0.44f * (1 - cosf(ha)) / 2); W[0].c = RGBA(0x5C, 0x9C, 0xFF, 0xFF); W[0].moon = 1;
    tilted(cx, cy, RM, ma, &W[1].x, &W[1].y); W[1].r = 21 * k * (0.78f + 0.44f * (1 - cosf(ma)) / 2); W[1].c = RGBA(0x4C, 0xE0, 0xB8, 0xFF); W[1].moon = 0;
    float mo = (float)(m * 2 * PI_F / 10);   /* the moon goes round in ten minutes */
    W[0].mx = W[0].x + sinf(mo) * W[0].r * 2.1f; W[0].my = W[0].y - cosf(mo) * W[0].r * 2.1f * TILT;
    for (int pass = 0; pass < 2; pass++) {
        if (pass == 1) sun_disc(cx, cy, 64 * k, k);
        for (int i = 0; i < 2; i++) {
            if ((W[i].y < cy) != (pass == 0)) continue;
            float trail_a = i == 0 ? ha : ma, trail_r = i == 0 ? RH : RM;
            for (int j = 1; j <= 22; j++) {
                float x, y; tilted(cx, cy, trail_r, trail_a - j * 0.02f, &x, &y);
                gfx_circle(x, y, W[i].r * 0.3f * (1 - j / 24.0f), COLOR_ALPHA(W[i].c, 0x60 - j * 2));
            }
            if (W[i].moon && cosf(mo) > 0) world(W[i].mx, W[i].my, W[i].r * 0.3f, RGBA(0xD8, 0xD8, 0xE0, 0xFF), cx, cy);
            gfx_shadow(W[i].x - W[i].r, W[i].y - W[i].r, 2 * W[i].r, 2 * W[i].r, W[i].r, W[i].r * 0.9f, COLOR_ALPHA(W[i].c, 0x48));
            world(W[i].x, W[i].y, W[i].r, W[i].c, cx, cy);
            if (W[i].moon && cosf(mo) <= 0) world(W[i].mx, W[i].my, W[i].r * 0.3f, RGBA(0xD8, 0xD8, 0xE0, 0xFF), cx, cy);
        }
    }
    /* the comet: a bright head on the inner orbit, its tail streaming away from the sun */
    float hx, hy; tilted(cx, cy, RS, sa, &hx, &hy);
    for (int j = 1; j <= 36; j++) {
        float x, y; tilted(cx, cy, RS + j * 2.2f * k, sa - j * 0.028f, &x, &y);
        gfx_circle(x, y, (7 - j * 0.16f) * k, RGBA(0xC0, 0xE0, 0xFF, 0x70 - j * 0x1D / 10));
    }
    gfx_shadow(hx - 14 * k, hy - 14 * k, 28 * k, 28 * k, 14 * k, 12 * k, RGBA(0xC0, 0xE0, 0xFF, 0x70));
    gfx_circle(hx, hy, 7 * k, RGBA(0xF4, 0xF8, 0xFF, 0xFF));
    side_time(cx, cy, k, t, NULL);
    app_redraw();
}

/* ---- prayer orbit: the day as one turn round the sun. The earth goes round once in 24 hours
 * on a ring lit from sunrise to sunset; the prayers of the day are stations on it, the next one
 * a beacon; inside, the hour and minute planets and the comet of the seconds. ---- */

static float day_angle(float minute) { return minute / 1440 * 2 * PI_F + PI_F; }   /* midnight at the bottom, noon at the top */

static void prayer_orbit(float cx, float cy, float k, const struct tm *t, double sec)
{
    const SkyDay *sky = sky_today(t);
    float K = SNTV_PHONE ? k * 0.8f : k, T = g_app.time, R = 330 * K;
    far_stars(cx, cy, k, 90);
    float now = t->tm_hour * 60 + t->tm_min + (float)sec / 60;
    float rise = sky->ok ? (float)sky->d.sunrise * 60 : 6 * 60, set = sky->ok ? (float)sky->d.sunset * 60 : 18 * 60;
    float dawn = sky->ok ? (float)sky->d.dawn6 * 60 : rise - 30, dusk = sky->ok ? (float)sky->d.dusk6 * 60 : set + 30;
    for (int d = 0; d < 288; d++) {   /* the ring: night, twilight, day */
        float mn = d * 5.0f, a = day_angle(mn);
        Color c = !sky->ok ? RGBA(0x60, 0x68, 0x80, 0xA0)
                : mn >= rise && mn <= set ? RGBA(0xFF, 0xC8, 0x58, 0xE0)
                : (mn >= dawn && mn < rise) || (mn > set && mn <= dusk) ? RGBA(0xF0, 0x80, 0x70, 0xC0) : RGBA(0x48, 0x58, 0x90, 0x90);
        gfx_circle(cx + sinf(a) * R, cy - cosf(a) * R, 2.4f * K, c);
    }
    for (int o = 0; o < 3; o++) {   /* the inner orbits */
        float r = (o == 0 ? 205 : o == 1 ? 140 : 85) * K;
        for (int d = 0; d < 96; d++) { float a = d * 2 * PI_F / 96; gfx_circle(cx + sinf(a) * r, cy - cosf(a) * r, 1.2f * K + 0.3f, RGBA(0x50, 0x58, 0x70, 0x70)); }
    }
    sun_disc(cx, cy, 36 * K, K);
    double m = t->tm_min + sec / 60.0, h = (t->tm_hour % 12) + m / 60.0;
    float ha = (float)(h / 12 * 2 * PI_F), ma = (float)(m / 60 * 2 * PI_F), sa = (float)(sec / 60 * 2 * PI_F);
    world(cx + sinf(ha) * 205 * K, cy - cosf(ha) * 205 * K, 15 * K, RGBA(0x5C, 0x9C, 0xFF, 0xFF), cx, cy);
    world(cx + sinf(ma) * 140 * K, cy - cosf(ma) * 140 * K, 11 * K, RGBA(0x4C, 0xE0, 0xB8, 0xFF), cx, cy);
    for (int j = 1; j <= 20; j++) gfx_circle(cx + sinf(sa - j * 0.03f) * (85 + j * 1.2f) * K, cy - cosf(sa - j * 0.03f) * (85 + j * 1.2f) * K, (4 - j * 0.15f) * K, RGBA(0xC0, 0xE0, 0xFF, 0x60 - j * 2));
    gfx_circle(cx + sinf(sa) * 85 * K, cy - cosf(sa) * 85 * K, 4.5f * K, RGBA(0xF4, 0xF8, 0xFF, 0xFF));
    /* the stations */
    int next = -1;
    for (int i = 0; i < sky->n; i++) if (sky->ev[i].minute > now) { next = i; break; }
    if (next < 0 && sky->n) next = 0;
    for (int i = 0; i < sky->n; i++) {
        float a = day_angle(sky->ev[i].minute), x = cx + sinf(a) * R, y = cy - cosf(a) * R;
        int past = sky->ev[i].minute <= now && i != next;
        if (i == next) {
            float pulse = 0.5f + 0.5f * sinf(T * 2.2f), e = (26 + 10 * pulse) * K;
            gfx_shadow(x - e, y - e, 2 * e, 2 * e, e, e, RGBA(0xFF, 0xE0, 0x90, 0x60 + (int)(0x40 * pulse)));
            gfx_circle(x, y, 9 * K, RGBA(0xFF, 0xF0, 0xC0, 0xFF));
        } else gfx_circle(x, y, 6 * K, past ? RGBA(0x80, 0x88, 0xA0, 0xA0) : RGBA(0xFF, 0xD8, 0x90, 0xE0));
        float lx = cx + sinf(a) * (R + 46 * K), ly = cy - cosf(a) * (R + 46 * K);
        font_draw_center(sky->ev[i].name, lx, ly - 13 * K, (int)(24 * K + 0.5f) | (i == next ? FONT_B : 0), i == next ? RGBA(0xFF, 0xF0, 0xC0, 0xFF) : past ? RGBA(0x80, 0x88, 0xA0, 0xC0) : RGBA(0xE0, 0xD0, 0xB0, 0xE0));
    }
    /* the earth, where the day has got to */
    float ea = day_angle(now), ex = cx + sinf(ea) * R, ey = cy - cosf(ea) * R;
    for (int j = 1; j <= 18; j++) gfx_circle(cx + sinf(ea - j * 0.012f) * R, cy - cosf(ea - j * 0.012f) * R, 5 * K * (1 - j / 20.0f), RGBA(0x6C, 0xB0, 0xFF, 0x60 - j * 3));
    gfx_shadow(ex - 18 * K, ey - 18 * K, 36 * K, 36 * K, 18 * K, 16 * K, RGBA(0x6C, 0xB0, 0xFF, 0x50));
    world(ex, ey, 13 * K, RGBA(0x4C, 0x90, 0xF0, 0xFF), cx, cy);
    gfx_circle(ex - 4 * K, ey - 3 * K, 4 * K, RGBA(0x50, 0xC0, 0x70, 0xA0));   /* a continent */
    side_time(cx, cy, k, t, sky->n ? sky->next : sky->ok ? "No prayer times for this tradition" : "Set your location in Settings > Take breaks");
    app_redraw();
}

/* ---- horizon: the sky of the hour over water, the sun on its real arc with the prayers of
 * the day marked along it, the moon as it looks tonight, clouds, birds, a shooting star. ---- */

static void sky_colours(float hr, const SkyDay *sky, Color *top, Color *bot, int *night)
{
    static const struct { float h; Color top, bottom; } SKY[] = {
        { 0, RGBA(0x04, 0x06, 0x12, 0xFF), RGBA(0x0C, 0x14, 0x30, 0xFF) }, { 5, RGBA(0x16, 0x1A, 0x44, 0xFF), RGBA(0x5A, 0x3C, 0x6A, 0xFF) },
        { 6.5f, RGBA(0x3C, 0x50, 0x9A, 0xFF), RGBA(0xF4, 0x9A, 0x74, 0xFF) }, { 9, RGBA(0x4A, 0x9C, 0xE8, 0xFF), RGBA(0xC8, 0xE4, 0xF4, 0xFF) },
        { 15, RGBA(0x2C, 0x7C, 0xD8, 0xFF), RGBA(0xA4, 0xD4, 0xF4, 0xFF) }, { 18, RGBA(0x36, 0x4A, 0x9C, 0xFF), RGBA(0xF6, 0xA0, 0x58, 0xFF) },
        { 19.5f, RGBA(0x24, 0x1C, 0x50, 0xFF), RGBA(0xD8, 0x5A, 0x5C, 0xFF) }, { 21, RGBA(0x08, 0x0A, 0x22, 0xFF), RGBA(0x1C, 0x22, 0x4C, 0xFF) },
        { 24, RGBA(0x04, 0x06, 0x12, 0xFF), RGBA(0x0C, 0x14, 0x30, 0xFF) } };
    /* the table is drawn for a six-to-six day; the real day is stretched onto it */
    float v = hr;
    if (sky->ok && sky->d.sunrise > 1 && sky->d.sunset < 23 && sky->d.sunset > sky->d.sunrise) {
        float rise = (float)sky->d.sunrise, set = (float)sky->d.sunset;
        v = hr < rise ? hr * 6 / rise : hr < set ? 6 + 12 * (hr - rise) / (set - rise) : 18 + 6 * (hr - set) / (24 - set);
    }
    int i = 0;
    while (i < 7 && v >= SKY[i + 1].h) i++;
    float f = (v - SKY[i].h) / (SKY[i + 1].h - SKY[i].h);
    *top = color_lerp(SKY[i].top, SKY[i + 1].top, f); *bot = color_lerp(SKY[i].bottom, SKY[i + 1].bottom, f);
    *night = v < 6 || v >= 19.5f;
}

static void moon_disc(float x, float y, float r, float phase, Color skyc)
{
    gfx_shadow(x - r, y - r, 2 * r, 2 * r, r, r * 1.4f, RGBA(0xD8, 0xE4, 0xFF, 0x40));
    gfx_circle(x, y, r, RGBA(0xEE, 0xF0, 0xF6, 0xFF));
    gfx_circle(x - r * 0.3f, y + r * 0.2f, r * 0.16f, RGBA(0xC8, 0xCC, 0xD8, 0xFF));   /* a sea or two */
    gfx_circle(x + r * 0.25f, y - r * 0.3f, r * 0.11f, RGBA(0xC8, 0xCC, 0xD8, 0xFF));
    float d = phase <= 0.5f ? -4 * r * phase : 2 * r * (2 - 2 * phase);   /* the shadow slides off to the left, then back from the right */
    if (fabsf(d) < 2 * r - 0.5f) gfx_circle(x + d, y, r + 0.5f, skyc);
}

static void horizon(float cx, float cy, float k, const struct tm *t, double sec)
{
    const SkyDay *sky = sky_today(t);
    float T = g_app.time, hr = t->tm_hour + t->tm_min / 60.0f + (float)sec / 3600;
    Color top, bot; int night;
    sky_colours(hr, sky, &top, &bot, &night);
    float W = 1100 * k, H = 640 * k, horizon_y = cy + H * 0.52f, bottom = cy + H + 60 * k, sky_top = cy - H - 60 * k;
    float X0 = cx - W, XW = 2 * W;   /* on a phone the scene runs to the edges */
    if (SNTV_PHONE) { X0 = 0; XW = SNTV_UI_W; sky_top = 0; bottom = SNTV_UI_H; }
    gfx_gradient_v(X0, sky_top, XW, horizon_y - sky_top, top, bot);
    if (night) for (int s = 0; s < 90; s++)
        gfx_circle(X0 + (s * 233 % 1920) * k, sky_top + (s * 137 % 620) * k, (1 + s % 3 * 0.5f) * k, RGBA(0xFF, 0xFF, 0xFF, 0x40 + (int)(0x60 * (0.5f + 0.5f * sinf(T * 1.3f + s)))));
    /* the sun's arc for today: from where it rises to where it sets; the moon takes the night arc */
    float rise = sky->ok ? (float)sky->d.sunrise : 6, set = sky->ok ? (float)sky->d.sunset : 18;
    if (!(set > rise + 1 && set < 23.5f && rise > 0.5f)) { rise = 6; set = 18; }
    float day_len = set - rise, night_len = 24 - day_len;
    float p = night ? fmodf(hr - set + 24, 24) / night_len : (hr - rise) / day_len;
    if (p < 0) p = 0;
    if (p > 1) p = 1;
    float ARC = 1.18f * H, bx = cx + (p - 0.5f) * W * 1.5f, by = horizon_y - sinf(PI_F * p) * ARC;
    if (!night) {   /* the arc, dotted, the prayers along it */
        for (int d = 0; d <= 60; d++) {
            float q = d / 60.0f;
            gfx_circle(cx + (q - 0.5f) * W * 1.5f, horizon_y - sinf(PI_F * q) * ARC, 2 * k, RGBA(0xFF, 0xFF, 0xFF, 0x30));
        }
    }
    if (night) moon_disc(bx, by, 40 * k, moon_phase(t), color_lerp(top, bot, (by - sky_top) / (horizon_y - sky_top)));
    else {
        gfx_shadow(bx - 60 * k, by - 60 * k, 120 * k, 120 * k, 60 * k, 140 * k, RGBA(0xFF, 0xE0, 0x90, 0x70));
        gfx_circle(bx, by, 56 * k, RGBA(0xFF, 0xF2, 0xC8, 0xFF));
    }
    for (int i = 0; i < sky->n; i++) {   /* the stations: on the arc by day, at its ends by night */
        float h = sky->ev[i].minute / 60.0f, q = (h - rise) / day_len, x, y;
        int on_arc = q > 0.02f && q < 0.98f;
        if (on_arc && night) continue;
        if (!on_arc) { q = h < rise ? 0 : 1; }
        x = cx + (q - 0.5f) * W * 1.5f; y = on_arc ? horizon_y - sinf(PI_F * q) * ARC : horizon_y - 8 * k;
        int next = sky->ev[i].minute > hr * 60 && (i == 0 || sky->ev[i - 1].minute <= hr * 60);
        gfx_circle(x, y, (next ? 7 : 5) * k, next ? RGBA(0xFF, 0xF0, 0xC0, 0xFF) : RGBA(0xFF, 0xFF, 0xFF, 0xA0));
        font_draw_center(sky->ev[i].name, x, y - 36 * k, (int)(22 * k + 0.5f) | (next ? FONT_B : 0), next ? RGBA(0xFF, 0xF0, 0xC0, 0xFF) : RGBA(0xFF, 0xFF, 0xFF, 0xB0));
    }
    for (int c = 0; c < 4; c++) {   /* clouds drift across */
        float u = fmodf(T * (6 + c * 2) * k + c * 700 * k, XW + 500 * k) - 250 * k, s = (0.5f + c % 3 * 0.25f) * k;
        Color cc = night ? RGBA(0x30, 0x38, 0x58, 0x70) : RGBA(0xFF, 0xFF, 0xFF, 0x60 + c * 0x10);
        cloud(X0 + u, cy - H * (0.55f - c * 0.18f), s, cc);
    }
    if (!night) for (int b = 0; b < 5; b++) {   /* birds */
        float x = X0 + fmodf(T * (34 + b * 7) * k + b * 410 * k, XW + 100 * k), y = cy - H * 0.3f + b * 26 * k + 12 * k * sinf(T * 0.6f + b), f = 6 * k * sinf(T * 5 + b * 2);
        gfx_line(x - 10 * k, y + f, x, y - 2 * k, 2 * k, RGBA(0x20, 0x20, 0x30, 0xC0)); gfx_line(x, y - 2 * k, x + 10 * k, y + f, 2 * k, RGBA(0x20, 0x20, 0x30, 0xC0));
    }
    if (night) {   /* a shooting star now and then */
        float cycle = 41, ph = fmodf(T, cycle); int n = (int)(T / cycle);
        if (ph < 1.1f) {
            float x0 = cx - W * 0.8f + (n * 613 % 1000) / 1000.0f * W * 1.6f, y0 = cy - H * 0.9f + (n * 271 % 300) / 300.0f * H * 0.4f, len = 160 * k;
            float x = x0 + ph * 420 * k, y = y0 + ph * 180 * k;
            gfx_line(x - len * 0.92f, y - len * 0.39f, x, y, 2.5f * k, RGBA(0xFF, 0xFF, 0xFF, (int)(0xC0 * (1 - ph / 1.1f))));
        }
    }
    /* hills, a skyline, then the water */
    for (int l = 0; l < 2; l++) {
        Color c = color_lerp(bot, RGBA(0x04, 0x06, 0x0A, 0xFF), 0.5f + l * 0.25f);
        for (int x = 0; x < 64; x++) {
            float u = x / 64.0f, hh = H * (0.16f + 0.05f * l + 0.05f * sinf(u * (5 + l * 3) + l * 2) + 0.03f * sinf(u * 13 + l));
            gfx_rect(X0 + u * XW, horizon_y - hh + l * 30 * k, XW / 64 + 1, hh - l * 30 * k, c);
        }
    }
    Color ink = RGBA(0x03, 0x04, 0x08, 0xFF);
    if (g_app.break_sched == BRK_ISLAM) {   /* a mosque on the shore */
        float mx = cx + 300 * k, base = horizon_y;
        gfx_rect(mx - 120 * k, base - 70 * k, 240 * k, 70 * k, ink);
        gfx_circle(mx, base - 70 * k, 72 * k, ink);
        gfx_line(mx, base - 142 * k, mx, base - 168 * k, 3 * k, ink); gfx_circle(mx, base - 172 * k, 5 * k, ink);
        for (int s = -1; s <= 1; s += 2) {
            float tx = mx + s * 165 * k;
            gfx_rect(tx - 9 * k, base - 200 * k, 18 * k, 200 * k, ink);
            gfx_rect(tx - 16 * k, base - 150 * k, 32 * k, 10 * k, ink);
            gfx_circle(tx, base - 206 * k, 11 * k, ink);
            gfx_line(tx, base - 214 * k, tx, base - 236 * k, 2.5f * k, ink);
        }
    }
    for (int b = 0; b < 14; b++) {   /* a town along the water, its windows lit at night */
        float bw = (40 + b * 29 % 50) * k, bh = (40 + b * 53 % 110) * k, bx0 = X0 + b * 150 * k + (b * 17 % 40) * k;
        if (g_app.break_sched == BRK_ISLAM && bx0 > cx + 150 * k && bx0 < cx + 480 * k) continue;
        gfx_rect(bx0, horizon_y - bh, bw, bh, ink);
        if (night) for (int wy = 0; wy < (int)(bh / (18 * k)); wy++) for (int wx = 0; wx < (int)(bw / (16 * k)); wx++)
            if ((b * 7 + wy * 3 + wx * 5) % 5 < 2) gfx_rect(bx0 + 5 * k + wx * 16 * k, horizon_y - bh + 6 * k + wy * 18 * k, 6 * k, 8 * k, RGBA(0xFF, 0xE0, 0xA0, 0xB0));
    }
    Color water = color_lerp(bot, RGBA(0x02, 0x04, 0x10, 0xFF), 0.6f);
    gfx_gradient_v(X0, horizon_y, XW, bottom - horizon_y, water, RGBA(0x02, 0x03, 0x08, 0xFF));
    Color glint = night ? RGBA(0xD8, 0xE4, 0xFF, 0xFF) : RGBA(0xFF, 0xF0, 0xC0, 0xFF);
    for (int r = 0; r < (SNTV_PHONE ? 60 : 26); r++) {   /* the light on the water, broken by the waves */
        float y = horizon_y + 6 * k + r * 9 * k, spread = (20 + r * 9) * k, a = 0.9f - r * 0.03f;
        for (int s = 0; s < 3; s++) {
            float w = spread * (0.4f + 0.3f * sinf(T * 1.7f + r * 1.3f + s * 2.1f)), x = bx + (s - 1) * spread * 0.5f * sinf(T * 0.9f + r + s);
            gfx_rect(x - w / 2, y, w, 2 * k, COLOR_ALPHA(glint, (int)(0x50 * a * (0.5f + 0.5f * sinf(T * 2.3f + r * 0.7f + s)))));
        }
        for (int s = 0; s < 6; s++) {   /* and the sky's shimmer everywhere else */
            float x = X0 + fmodf(s * 397 * k + r * 211 * k + T * 20 * k * (s % 2 ? 1 : -1), XW);
            gfx_rect(x, y, (30 + s * 7) * k, 1.5f * k, COLOR_ALPHA(bot, 0x18 + (int)(0x18 * sinf(T * 1.5f + s + r))));
        }
    }
    char s[8]; snprintf(s, sizeof s, g_app.clock_mode == 1 ? "%02d:%02d" : "%d:%02d", hour_shown(t), t->tm_min);
    float px = (SNTV_PHONE ? 200 : 280) * k, dh = font_digits_height(px), y = cy - dh / 2 - 110 * k, w = font_digits_measure(s, px);
    font_digits_draw(s, cx - w / 2 + 4 * k, y + 6 * k, px, RGBA(0, 0, 0, 0x40));
    font_digits_draw(s, cx - w / 2, y, px, RGBA(0xFF, 0xFF, 0xFF, 0xF4));
    ampm(t, cx + w / 2 + 18 * k, y + 6 * k, k, RGBA(0xFF, 0xFF, 0xFF, 0xC0));
    date_line(t, cx, y + dh + 40 * k, k, RGBA(0xFF, 0xFF, 0xFF, 0xD0));
    if (sky->next[0]) font_draw_center(sky->next, cx, y + dh + 84 * k, (int)(26 * k + 0.5f), RGBA(0xFF, 0xE8, 0xB0, 0xE0));
    app_redraw();
}

/* ---- orrery: the planets where they really are today, seen from above the sun, with
 * the moon round the earth and a line on which of them are up in the sky tonight. ---- */

static const struct { const char *name; double a, e, L, w, Ld; float size; Color c; } PL[8] = {
    { "Mercury", 0.38709927, 0.20563593, 252.25032350, 77.45779628, 149472.67411175, 5, RGBA(0xB8, 0xB0, 0xA8, 0xFF) },
    { "Venus", 0.72333566, 0.00677672, 181.97909950, 131.60246718, 58517.81538729, 8, RGBA(0xF0, 0xD8, 0xA8, 0xFF) },
    { "Earth", 1.00000261, 0.01671123, 100.46457166, 102.93768193, 35999.37244981, 9, RGBA(0x4C, 0x90, 0xF0, 0xFF) },
    { "Mars", 1.52371034, 0.09339410, -4.55343205, -23.94362959, 19140.30268499, 7, RGBA(0xE0, 0x70, 0x48, 0xFF) },
    { "Jupiter", 5.20288700, 0.04838624, 34.39644051, 14.72847983, 3034.74612775, 22, RGBA(0xD8, 0xB0, 0x88, 0xFF) },
    { "Saturn", 9.53667594, 0.05386179, 49.95424423, 92.59887831, 1222.49362201, 18, RGBA(0xE8, 0xD0, 0x98, 0xFF) },
    { "Uranus", 19.18916464, 0.04725744, 313.23810451, 170.95427630, 428.48202785, 13, RGBA(0x9C, 0xE0, 0xE8, 0xFF) },
    { "Neptune", 30.06992276, 0.00859048, -55.12002969, 44.96476227, 218.45945325, 12, RGBA(0x50, 0x70, 0xE8, 0xFF) } };

/* Heliocentric longitude (radians) and distance (AU) from the mean elements at J2000 (JPL's). */
static void helio(int i, double T, double *lon, double *r)
{
    double M = fmod(PL[i].L + PL[i].Ld * T - PL[i].w, 360.0) * M_PI / 180, E = M;
    for (int it = 0; it < 5; it++) E = M + PL[i].e * sin(E);
    double nu = 2 * atan2(sqrt(1 + PL[i].e) * sin(E / 2), sqrt(1 - PL[i].e) * cos(E / 2));
    *lon = nu + PL[i].w * M_PI / 180; *r = PL[i].a * (1 - PL[i].e * cos(E));
}

static float orrery_radius(int i, float k) { return (float)(60 + 380 * pow(PL[i].a / 30.07, 0.55)) * k; }

static void orrery(float cx, float cy, float k, const struct tm *t, double sec)
{
    (void)sec;
    float K = SNTV_PHONE ? k * 0.9f : k * 0.92f;
    double d = days_j2000(t), T = d / 36525.0, lon[8], dist[8];
    far_stars(cx, cy, k, 100);
    for (int i = 0; i < 8; i++) {
        helio(i, T, &lon[i], &dist[i]);
        float r = orrery_radius(i, K);
        for (int s = 0; s < 180; s++) { float a = s * 2 * PI_F / 180; gfx_circle(cx + cosf(a) * r, cy - sinf(a) * r, 1.1f * K + 0.3f, RGBA(0x50, 0x58, 0x70, 0x70)); }
    }
    sun_disc(cx, cy, 26 * K, K);
    double mlon = (218.316 + 13.176396 * d) * M_PI / 180;   /* the moon, by its mean longitude */
    float lx[8], ly[8]; int nl = 0;
    for (int i = 0; i < 8; i++) {
        float r = orrery_radius(i, K), x = cx + cosf((float)lon[i]) * r, y = cy - sinf((float)lon[i]) * r, sz = PL[i].size * K;
        if (i == 5) for (int s = 0; s < 40; s++) {   /* Saturn's ring, the far half first */
            float b = s * 2 * PI_F / 40; if (sinf(b) > 0) continue;
            float rx = sz * 1.9f * cosf(b), ry = sz * 0.7f * sinf(b);
            gfx_circle(x + rx * 0.9f - ry * 0.45f, y + rx * 0.45f + ry * 0.9f, 2 * K, RGBA(0xE8, 0xD8, 0xB0, 0xC0));
        }
        gfx_shadow(x - sz, y - sz, 2 * sz, 2 * sz, sz, sz * 0.8f, COLOR_ALPHA(PL[i].c, 0x40));
        world(x, y, sz, PL[i].c, cx, cy);
        if (i == 5) for (int s = 0; s < 40; s++) {
            float b = s * 2 * PI_F / 40; if (sinf(b) <= 0) continue;
            float rx = sz * 1.9f * cosf(b), ry = sz * 0.7f * sinf(b);
            gfx_circle(x + rx * 0.9f - ry * 0.45f, y + rx * 0.45f + ry * 0.9f, 2 * K, RGBA(0xE8, 0xD8, 0xB0, 0xC0));
        }
        if (i == 2) world(x + cosf((float)mlon) * 17 * K, y - sinf((float)mlon) * 17 * K, 3 * K, RGBA(0xD8, 0xD8, 0xE0, 0xFF), cx, cy);
        float tx = x + cosf((float)lon[i]) * (sz + 22 * K), ty = y - sinf((float)lon[i]) * (sz + 22 * K) - 10 * K;   /* the name outward, off the sun */
        int clear = 1;
        for (int j = 0; j < nl; j++) if (fabsf(lx[j] - tx) < 70 * K && fabsf(ly[j] - ty) < 24 * K) clear = 0;
        if (clear) { font_draw_center(PL[i].name, tx, ty, (int)(19 * K + 0.5f), RGBA(0xA0, 0xA8, 0xC0, i < 4 ? 0x90 : 0xC0)); lx[nl] = tx; ly[nl] = ty; nl++; }
    }
    /* which of the bright ones are in the sky: east of the sun means the evening, west the morning */
    char evening[96] = "", morning[96] = "";
    double ex = dist[2] * cos(lon[2]), ey = dist[2] * sin(lon[2]);
    for (int i = 0; i < 6; i++) {
        if (i == 2) continue;
        double px = dist[i] * cos(lon[i]) - ex, py = dist[i] * sin(lon[i]) - ey, sx = -ex, sy = -ey;
        double dot = (px * sx + py * sy) / (sqrt(px * px + py * py) * sqrt(sx * sx + sy * sy)), el = acos(dot < -1 ? -1 : dot > 1 ? 1 : dot) * 180 / M_PI;
        if (el < (i == 0 ? 18 : 22)) continue;
        char *into = sx * py - sy * px > 0 ? evening : morning;
        snprintf(into + strlen(into), 96 - strlen(into), "%s%s", *into ? ", " : "", PL[i].name);
    }
    char line[224];
    if (*evening && *morning) snprintf(line, sizeof line, "Evening: %s  \xC2\xB7  Morning: %s", evening, morning);
    else if (*evening) snprintf(line, sizeof line, "In the evening sky: %s", evening);
    else if (*morning) snprintf(line, sizeof line, "In the morning sky: %s", morning);
    else snprintf(line, sizeof line, "The bright planets are near the sun this week");
    side_time(cx, cy, k, t, NULL);
    font_draw_center(line, cx, cy + (SNTV_PHONE ? 720 : 425) * k, (int)(24 * k + 0.5f), RGBA(0xC0, 0xC8, 0xE0, 0xC0));
    app_redraw();
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
    case CLOCKFACE_STATION: station(cx, cy, k, t, sec); break;
    case CLOCKFACE_LIQUID: liquid(cx, cy, k, t); break;
    case CLOCKFACE_WEATHER: weather_face(cx, cy, k, t); break;
    case CLOCKFACE_AURORA: aurora(cx, cy, k, t); break;
    case CLOCKFACE_PLANETS: planets(cx, cy, k, t, sec); break;
    case CLOCKFACE_PRAYER: prayer_orbit(cx, cy, k, t, sec); break;
    case CLOCKFACE_HORIZON: horizon(cx, cy, k, t, sec); break;
    case CLOCKFACE_ORRERY: orrery(cx, cy, k, t, sec); break;
    default: digital(cx, cy, k, t); break;
    }
}
