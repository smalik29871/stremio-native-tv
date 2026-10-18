#include "../app.h"
#include "font.h"
#include "keyboard.h"
#include "theme.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { F_EMAIL, F_PASSWORD, F_KEYBOARD, F_SUBMIT, F_SKIP };

typedef struct {
    Keyboard kb[2];
    int editing;      /* which field the keyboard writes to */
    int focus;
    int busy;
    char message[160];
    int ok;
} Login;

static Login *g_active;   /* the login screen awaiting a callback */

static void on_done(int ok, const char *msg)
{
    if (!g_active) return;
    g_active->busy = 0;
    g_active->ok = ok;
    snprintf(g_active->message, sizeof g_active->message, "%s", ok ? "Signed in" : msg);
    if (ok) { app_toast("Signed in as %s", msg); app_pop(); }
}

static void submit(Login *l)
{
    if (l->busy) return;
    if (!strchr(l->kb[F_EMAIL].text, '@') || !l->kb[F_PASSWORD].text[0]) {
        snprintf(l->message, sizeof l->message, "Enter your Stremio e-mail and password");
        return;
    }
    l->busy = 1;
    l->message[0] = 0;
    g_active = l;
    app_login(l->kb[F_EMAIL].text, l->kb[F_PASSWORD].text, on_done);
}

static void draw_field(const Login *l, int f, const char *label, float x, float y, float w)
{
    int foc = l->focus == f;
    font_draw(label, x, y, FONT_S, THEME_TEXT_DIM);
    gfx_rect(x, y + 36, w, 64, foc ? THEME_SURFACE_HI : THEME_SURFACE);
    if (foc || (l->focus == F_KEYBOARD && l->editing == f)) gfx_rect_outline(x, y + 36, w, 64, 3, THEME_ACCENT);
    const char *t = l->kb[f].text;
    char masked[256];
    if (f == F_PASSWORD) {
        size_t n = strlen(t);
        if (n > sizeof masked - 1) n = sizeof masked - 1;
        memset(masked, '*', n);
        masked[n] = 0;
        t = masked;
    }
    if (!*t) font_draw(f == F_EMAIL ? "you@example.com" : "password", x + 20, y + 52, FONT_M, THEME_TEXT_MUTED);
    else font_draw_fit(t, x + 20, y + 52, FONT_M, THEME_TEXT, w - 40);
}

static void draw(Screen *s)
{
    Login *l = s->st;
    gfx_rect(0, 0, SNTV_UI_W, SNTV_UI_H, THEME_BG);
    ui_draw_header("Sign in to Stremio", NULL);
    float x = THEME_MARGIN, y = 170, w = 760;
    draw_field(l, F_EMAIL, "E-mail", x, y, w);
    draw_field(l, F_PASSWORD, "Password", x, y + 130, w);
    keyboard_draw(&l->kb[l->editing], x, y + 290, l->focus == F_KEYBOARD);

    float rx = x + w + 100;
    font_draw_wrapped("Your addons, library and Continue Watching come from your Stremio account, "
                      "exactly as in the official apps. Nothing is stored on this TV except the session key.",
                      rx, y, FONT_S, THEME_TEXT_DIM, SNTV_UI_W - rx - THEME_MARGIN, 6);
    ui_draw_button(l->busy ? "Signing in..." : "Sign in", rx, y + 260, 320, 64, l->focus == F_SUBMIT, 1);
    ui_draw_button("Continue without account", rx, y + 344, 420, 64, l->focus == F_SKIP, 0);
    if (l->busy) ui_draw_spinner(rx + 360, y + 292);
    if (l->message[0]) font_draw_wrapped(l->message, rx, y + 440, FONT_S, l->ok ? THEME_OK : THEME_DANGER, SNTV_UI_W - rx - THEME_MARGIN, 3);
}

static int key(Screen *s, KeyAction a)
{
    Login *l = s->st;
    if (l->focus == F_KEYBOARD) {
        if (a == KEY_BACK) { l->focus = l->editing; return 1; }
        if (a == KEY_RIGHT && l->kb[l->editing].col == 10) { l->focus = F_SUBMIT; return 1; }
        KeyboardResult r = keyboard_key(&l->kb[l->editing], a);
        if (r == KB_LEAVE_UP) l->focus = F_PASSWORD;
        else if (r == KB_DONE) {
            if (l->editing == F_EMAIL) { l->editing = F_PASSWORD; l->focus = F_KEYBOARD; }
            else submit(l);
        }
        return 1;
    }
    switch (a) {
    case KEY_UP:
        if (l->focus == F_PASSWORD) l->focus = F_EMAIL;
        else if (l->focus == F_SKIP) l->focus = F_SUBMIT;
        else if (l->focus == F_SUBMIT) l->focus = F_EMAIL;
        return 1;
    case KEY_DOWN:
        if (l->focus == F_EMAIL) l->focus = F_PASSWORD;
        else if (l->focus == F_PASSWORD) { l->focus = F_KEYBOARD; }
        else if (l->focus == F_SUBMIT) l->focus = F_SKIP;
        return 1;
    case KEY_RIGHT: if (l->focus == F_EMAIL || l->focus == F_PASSWORD) l->focus = F_SUBMIT; return 1;
    case KEY_LEFT: if (l->focus == F_SUBMIT || l->focus == F_SKIP) l->focus = F_KEYBOARD; return 1;
    case KEY_OK:
        if (l->focus == F_EMAIL || l->focus == F_PASSWORD) { l->editing = l->focus; l->focus = F_KEYBOARD; }
        else if (l->focus == F_SUBMIT) submit(l);
        else if (l->focus == F_SKIP) app_pop();
        return 1;
    case KEY_BACKSPACE:
        keyboard_key(&l->kb[l->editing], KEY_BACKSPACE);
        return 1;
    default: return 0;
    }
}

static void text(Screen *s, const char *utf8)
{
    Login *l = s->st;
    keyboard_text_input(&l->kb[l->editing], utf8);
    if (l->focus != F_KEYBOARD && l->focus != F_SUBMIT && l->focus != F_SKIP) l->editing = l->focus;
}

static void destroy(Screen *s)
{
    Login *l = s->st;
    if (g_active == l) g_active = NULL;
    SDL_StopTextInput();
    free(l);
    free(s);
}

Screen *screen_login_create(void)
{
    Screen *s = calloc(1, sizeof *s);
    Login *l = calloc(1, sizeof *l);
    keyboard_init(&l->kb[F_EMAIL], 0, NULL);
    keyboard_init(&l->kb[F_PASSWORD], 1, NULL);
    l->focus = F_EMAIL;
    s->name = "login"; s->st = l;
    s->draw = draw; s->key = key; s->text = text; s->destroy = destroy;
    SDL_StartTextInput();
    return s;
}
