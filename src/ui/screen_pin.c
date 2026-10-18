/* PIN pad: a 3x4 keypad for the parental PIN. Verifies against the saved PIN, or
 * sets a new one (entered twice). Calls back with the outcome and pops itself. */
#include "../app.h"
#include "font.h"
#include "theme.h"
#include "widgets.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char title[96], subtitle[160];
    void (*done)(int ok, void *user); void *user;
    int setting;             /* 1: choose a new PIN (twice) */
    char first[8], entry[8];
    int row, col;            /* keypad focus */
    int stage;               /* setting: 0 first entry, 1 confirm */
    float shake;             /* wrong PIN */
    int finished;
} Pin;

static const char *KEYS[4][3] = { { "1", "2", "3" }, { "4", "5", "6" }, { "7", "8", "9" }, { "<", "0", "OK" } };

static void finish(Screen *s, int ok)
{
    Pin *p = s->st;
    if (p->finished) return;
    p->finished = 1;
    void (*done)(int, void *) = p->done; void *user = p->user;
    app_pop();
    if (done) done(ok, user);
}

static void submit(Screen *s)
{
    Pin *p = s->st;
    if (strlen(p->entry) < 4) return;
    if (p->setting) {
        if (p->stage == 0) { snprintf(p->first, sizeof p->first, "%s", p->entry); p->entry[0] = 0; p->stage = 1; snprintf(p->subtitle, sizeof p->subtitle, "Enter the same PIN again"); return; }
        if (strcmp(p->first, p->entry) == 0) { app_set_parental_pin(p->entry); app_toast("PIN set"); finish(s, 1); return; }
        p->entry[0] = 0; p->first[0] = 0; p->stage = 0; p->shake = 0.5f;
        snprintf(p->subtitle, sizeof p->subtitle, "They did not match. Choose a PIN");
        return;
    }
    if (strcmp(p->entry, g_app.parental_pin) == 0) { finish(s, 1); return; }
    p->entry[0] = 0; p->shake = 0.5f;
    snprintf(p->subtitle, sizeof p->subtitle, "Wrong PIN. Try again");
}

static void update(Screen *s, float dt) { Pin *p = s->st; if (p->shake > 0) { p->shake -= dt; app_redraw(); } }

static void draw(Screen *s)
{
    Pin *p = s->st;
    ui_background(0);
    gfx_rect(0, 0, SNTV_UI_W, SNTV_UI_H, RGBA(0x08, 0x07, 0x12, 0xB0));
    float w = 760, h = 720, x = (SNTV_UI_W - w) / 2, y = (SNTV_UI_H - h) / 2;
    if (!g_app.fx_light) gfx_shadow(x, y, w, h, R_PANEL, 50, RGBA(0, 0, 0, 0xA0));
    gfx_rrect(x, y, w, h, R_PANEL, THEME_PANEL);
    gfx_rrect_ring(x, y, w, h, R_PANEL, 1.5f, RGBA(0xFF, 0xFF, 0xFF, 0x1C));
    ui_icon(ICON_LOCK, x + w / 2, y + 70, 44, THEME_ACCENT2);
    font_draw_center(p->title, x + w / 2, y + 112, FONT_L | FONT_B, THEME_TEXT);
    font_draw_center(p->subtitle, x + w / 2, y + 168, FONT_S, p->shake > 0 ? THEME_DANGER : THEME_TEXT_DIM);
    /* dots */
    float dx = x + w / 2 - 3 * 36 + (p->shake > 0 ? (float)((int)(p->shake * 40) % 2 ? 8 : -8) : 0);
    for (int i = 0; i < 4; i++) {
        int on = (int)strlen(p->entry) > i;
        if (on) gfx_circle(dx + i * 72 + 36, y + 240, 14, THEME_TEXT); else gfx_rrect_ring(dx + i * 72 + 22, y + 226, 28, 28, 14, 2, THEME_TEXT_MUTED);
    }
    float kx = x + w / 2 - (3 * 120 - 14) / 2, ky = y + 300;
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 3; c++) {
            float bx = kx + c * 120, by = ky + r * 92;
            int foc = p->row == r && p->col == c;
            const char *k = KEYS[r][c];
            int special = k[0] == '<' || k[0] == 'O';
            if (foc && !g_app.fx_light) gfx_shadow(bx, by + 4, 106, 78, 22, 18, RGBA(0, 0, 0, 0x70));
            gfx_rrect(bx, by, 106, 78, 22, foc ? THEME_TEXT : special ? RGBA(0xFF, 0xFF, 0xFF, 0x0C) : RGBA(0xFF, 0xFF, 0xFF, 0x16));
            Color fg = foc ? THEME_ON_LIGHT : THEME_TEXT;
            if (k[0] == '<') ui_icon(ICON_BACK, bx + 53, by + 39, 26, fg);
            else font_draw_center(k, bx + 53, by + (78 - font_height(FONT_M | FONT_B)) / 2, FONT_M | FONT_B, fg);
        }
    font_draw_center("Back cancels", x + w / 2, y + h - 56, FONT_XS, THEME_TEXT_MUTED);
}

static int key(Screen *s, KeyAction a)
{
    Pin *p = s->st;
    app_redraw();
    switch (a) {
    case KEY_UP: if (p->row > 0) p->row--; return 1;
    case KEY_DOWN: if (p->row < 3) p->row++; return 1;
    case KEY_LEFT: if (p->col > 0) p->col--; return 1;
    case KEY_RIGHT: if (p->col < 2) p->col++; return 1;
    case KEY_OK: {
        const char *k = KEYS[p->row][p->col];
        if (k[0] == '<') { size_t n = strlen(p->entry); if (n) p->entry[n - 1] = 0; }
        else if (k[0] == 'O') submit(s);
        else if (strlen(p->entry) < 4) { size_t n = strlen(p->entry); p->entry[n] = k[0]; p->entry[n + 1] = 0; if (n + 1 == 4) submit(s); }
        return 1;
    }
    case KEY_BACKSPACE: { size_t n = strlen(p->entry); if (n) p->entry[n - 1] = 0; return 1; }
    case KEY_BACK: finish(s, 0); return 1;
    default: return 1;   /* nothing leaks past the pad */
    }
}

static void text(Screen *s, const char *utf8)   /* a number key on the remote */
{
    Pin *p = s->st;
    if (utf8[0] >= '0' && utf8[0] <= '9' && !utf8[1] && strlen(p->entry) < 4) {
        size_t n = strlen(p->entry); p->entry[n] = utf8[0]; p->entry[n + 1] = 0;
        if (n + 1 == 4) submit(s);
        app_redraw();
    }
}

static void destroy(Screen *s) { free(s->st); free(s); }

Screen *screen_pin_create(const char *title, int setting, void (*done)(int ok, void *user), void *user)
{
    Screen *s = calloc(1, sizeof *s);
    Pin *p = calloc(1, sizeof *p);
    snprintf(p->title, sizeof p->title, "%s", title);
    snprintf(p->subtitle, sizeof p->subtitle, "%s", setting ? "Choose a 4-digit PIN" : "Enter the parental PIN");
    p->setting = setting; p->done = done; p->user = user;
    p->row = 0; p->col = 1;
    s->name = "pin"; s->st = p;
    s->update = update; s->draw = draw; s->key = key; s->text = text; s->destroy = destroy;
    return s;
}
