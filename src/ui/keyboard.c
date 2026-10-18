#include "keyboard.h"
#include "font.h"
#include "theme.h"
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
    for (int r = 0; r < 4; r++) {
        const char *row = (kb->symbols ? ROW_SYM : ROW_ALPHA)[r];
        for (int c = 0; c < COLS; c++) {
            float kx = x + c * (KEY_W + GAP), ky = y + r * (KEY_H + GAP);
            int sel = focused && kb->row == r && kb->col == c;
            gfx_rect(kx, ky, KEY_W, KEY_H, sel ? THEME_ACCENT : THEME_SURFACE);
            char ch[2] = { row[c], 0 };
            if (kb->shift && ch[0] >= 'a' && ch[0] <= 'z') ch[0] = (char)(ch[0] - 32);
            float tw = font_measure(ch, FONT_M);
            font_draw(ch, kx + (KEY_W - tw) / 2, ky + (KEY_H - font_height(FONT_M)) / 2, FONT_M, THEME_TEXT);
        }
    }
    float kx = x, ky = y + 4 * (KEY_H + GAP);
    for (int a = 0; a < ACT_COUNT; a++) {
        float w = ACT_SPAN[a] * (KEY_W + GAP) - GAP;
        int sel = focused && kb->row == 4 && action_at(kb->col) == a;
        int lit = (a == ACT_SHIFT && kb->shift) || (a == ACT_SYM && kb->symbols);
        gfx_rect(kx, ky, w, KEY_H, sel ? THEME_ACCENT : lit ? THEME_SURFACE_HI : THEME_SURFACE);
        float tw = font_measure(ACT_LABEL[a], FONT_S);
        font_draw(ACT_LABEL[a], kx + (w - tw) / 2, ky + (KEY_H - font_height(FONT_S)) / 2, FONT_S, THEME_TEXT);
        kx += w + GAP;
    }
}
