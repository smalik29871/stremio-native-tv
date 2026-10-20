#include "keyboard.h"
#include "font.h"
#include "theme.h"
#include "widgets.h"
#include "../app.h"
#include <string.h>

enum { COLS = 11, ROWS = 5, KEY_W = 72, KEY_H = 64, GAP = 8 };

static const char *ROW_ALPHA[4] = { "1234567890@", "qwertyuiop.", "asdfghjkl-_", "zxcvbnm,:/!" };
static const char *ROW_SYM[4]   = { "1234567890@", "!#$%&*()+=.", "[]{}<>?;'\"~", "^|\\-_,:/! " };
/* row 4 is the action row: shift, symbols, space, backspace, done */
enum { ACT_SHIFT, ACT_SYM, ACT_SPACE, ACT_BACK, ACT_DONE, ACT_COUNT };
static const char *ACT_LABEL[ACT_COUNT] = { "Shift", "?123", "Space", "Del", "Done" };
static const int ACT_SPAN[ACT_COUNT] = { 2, 2, 3, 2, 2 };

float keyboard_width(void) { return COLS * (KEY_W + GAP) - GAP; }
float keyboard_height(void) { return ROWS * (KEY_H + GAP) - GAP; }

void keyboard_init(Keyboard *kb, int password, const char *initial)
{
    memset(kb, 0, sizeof *kb);
    kb->password = password;
    kb->row = 1;
    if (initial) snprintf(kb->text, sizeof kb->text, "%s", initial);
}

static void append(Keyboard *kb, const char *s)
{
    size_t n = strlen(kb->text), m = strlen(s);
    if (n + m < sizeof kb->text) memcpy(kb->text + n, s, m + 1);
}

static void backspace(Keyboard *kb)
{
    size_t n = strlen(kb->text);
    if (!n) return;
    n--;
    while (n && ((unsigned char)kb->text[n] & 0xC0) == 0x80) n--;
    kb->text[n] = 0;
}

static int action_at(int col)
{
    int c = 0;
    for (int a = 0; a < ACT_COUNT; a++) { c += ACT_SPAN[a]; if (col < c) return a; }
    return ACT_DONE;
}

KeyboardResult keyboard_key(Keyboard *kb, KeyAction a)
{
    app_redraw();
    switch (a) {
    case KEY_UP:
        if (kb->row == 0) return KB_LEAVE_UP;
        kb->row--;
        return KB_IGNORED;
    case KEY_DOWN: if (kb->row < ROWS - 1) kb->row++; return KB_IGNORED;
    case KEY_LEFT: kb->col = (kb->col + COLS - 1) % COLS; return KB_IGNORED;
    case KEY_RIGHT: kb->col = (kb->col + 1) % COLS; return KB_IGNORED;
    case KEY_BACKSPACE: backspace(kb); return KB_CHANGED;
    case KEY_OK: break;
    default: return KB_IGNORED;
    }
    if (kb->row < 4) {
        const char *row = (kb->symbols ? ROW_SYM : ROW_ALPHA)[kb->row];
        char ch[2] = { row[kb->col], 0 };
        if (kb->shift && ch[0] >= 'a' && ch[0] <= 'z') ch[0] = (char)(ch[0] - 32);
        append(kb, ch);
        kb->shift = 0;
        return KB_CHANGED;
    }
    switch (action_at(kb->col)) {
    case ACT_SHIFT: kb->shift = !kb->shift; return KB_IGNORED;
    case ACT_SYM: kb->symbols = !kb->symbols; return KB_IGNORED;
    case ACT_SPACE: append(kb, " "); return KB_CHANGED;
    case ACT_BACK: backspace(kb); return KB_CHANGED;
    default: return KB_DONE;
    }
}

void keyboard_text_input(Keyboard *kb, const char *utf8)
{
    append(kb, utf8);
}

void keyboard_draw(const Keyboard *kb, float x, float y, int focused)
{
    int aperture = g_theme.id == THEME_APERTURE;
    for (int r = 0; r < 4; r++) {
        const char *row = (kb->symbols ? ROW_SYM : ROW_ALPHA)[r];
        for (int c = 0; c < COLS; c++) {
            float kx = x + c * (KEY_W + GAP), ky = y + r * (KEY_H + GAP);
            int sel = focused && kb->row == r && kb->col == c;
            char ch[2] = { row[c], 0 };
            if (kb->shift && ch[0] >= 'a' && ch[0] <= 'z') ch[0] = (char)(ch[0] - 32);
            if (aperture) {
                if (sel) { gfx_rrect_ring(kx + 4, ky + 2, KEY_W - 8, KEY_H - 4, 3, 2, THEME_TEXT); gfx_rect(kx + 8, ky + KEY_H - 4, KEY_W - 16, 3, THEME_ACCENT); }
                float tw = font_measure(ch, FONT_M);
                font_draw(ch, kx + (KEY_W - tw) / 2, ky + (KEY_H - font_height(FONT_M)) / 2, FONT_M, sel ? THEME_TEXT : THEME_TEXT_DIM);
                continue;
            }
            if (sel && !g_app.fx_light) gfx_shadow(kx, ky + 4, KEY_W, KEY_H, R_KEY, 16, RGBA(0, 0, 0, 0x80));
            gfx_rrect(kx, ky, KEY_W, KEY_H, R_KEY, sel ? THEME_FOCUS : THEME_SURFACE);
            float tw = font_measure(ch, FONT_M | FONT_B);
            font_draw(ch, kx + (KEY_W - tw) / 2, ky + (KEY_H - font_height(FONT_M | FONT_B)) / 2, FONT_M | FONT_B, sel ? THEME_ON_FOCUS : THEME_TEXT);
        }
    }
    float kx = x, ky = y + 4 * (KEY_H + GAP);
    for (int a = 0; a < ACT_COUNT; a++) {
        float w = ACT_SPAN[a] * (KEY_W + GAP) - GAP;
        int sel = focused && kb->row == 4 && action_at(kb->col) == a;
        int lit = (a == ACT_SHIFT && kb->shift) || (a == ACT_SYM && kb->symbols);
        if (aperture) {
            int fs = FONT_S;
            float tw = font_measure(ACT_LABEL[a], fs);
            if (sel) { gfx_rrect_ring(kx + 4, ky + 2, w - 8, KEY_H - 4, 3, 2, THEME_TEXT); gfx_rect(kx + 8, ky + KEY_H - 4, w - 16, 3, THEME_ACCENT); }
            font_draw(ACT_LABEL[a], kx + (w - tw) / 2, ky + (KEY_H - font_height(fs)) / 2, fs, sel || lit ? THEME_TEXT : a == ACT_DONE ? THEME_ACCENT : THEME_TEXT_DIM);
            kx += w + GAP;
            continue;
        }
        if (sel && !g_app.fx_light) gfx_shadow(kx, ky + 4, w, KEY_H, R_KEY, 16, RGBA(0, 0, 0, 0x80));
        if (a == ACT_DONE && !sel) gfx_rrect_grad_h(kx, ky, w, KEY_H, R_KEY, THEME_ACCENT, THEME_ACCENT2);
        else gfx_rrect(kx, ky, w, KEY_H, R_KEY, sel ? THEME_FOCUS : lit ? THEME_SURFACE_HI : COLOR_ALPHA(THEME_TEXT, 0x1C));
        int fs = FONT_S | FONT_B;
        float tw = font_measure(ACT_LABEL[a], fs);
        font_draw(ACT_LABEL[a], kx + (w - tw) / 2, ky + (KEY_H - font_height(fs)) / 2, fs, sel ? THEME_ON_FOCUS : a == ACT_DONE ? THEME_ON_ACCENT : THEME_TEXT);
        kx += w + GAP;
    }
}
