/* Clock faces: digital, flip cards, a Braun-style dial, a word clock (QLOCKTWO's
 * English grid), polar rings, nixie tubes, and StandBy-style stacked digits. Plain
 * shapes and the big digit atlas only, so every face is sharp at any size. */
#include "clockface.h"
#include "font.h"
#include "gfx.h"
#include "theme.h"
#include "../app.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

const char *const CLOCKFACE_NAMES[CLOCKFACE_COUNT] = { "Digital", "Flip", "Analog", "Words", "Rings", "Nixie", "Stacked" };

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

void clockface_draw(int face, float cx, float cy, float k, const struct tm *t, double sec)
{
    switch (face) {
    case CLOCKFACE_FLIP: flip(cx, cy, k, t); break;
    case CLOCKFACE_ANALOG: analog(cx, cy, k, t, sec); break;
    case CLOCKFACE_WORDS: words(cx, cy, k, t); break;
    case CLOCKFACE_RINGS: rings(cx, cy, k, t, sec); break;
    case CLOCKFACE_NIXIE: nixie(cx, cy, k, t); break;
    case CLOCKFACE_STACKED: stacked(cx, cy, k, t); break;
    default: digital(cx, cy, k, t); break;
    }
}
