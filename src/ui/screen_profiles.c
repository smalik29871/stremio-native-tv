/* Who's watching: the accounts saved on this TV as profiles, a tile each with its
 * initials in a colour of its own, at start when several are saved (Settings > Account
 * turns that off). Choosing one signs it in: its library, addons and progress. */
#include "../app.h"
#include "../log.h"
#include "font.h"
#include "theme.h"
#include "widgets.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_PROFILES 9
#define TILE 176.0f
#define STEP 260.0f

typedef struct {
    char emails[MAX_PROFILES][128]; int n;   /* [0] is the one signed in */
    int focus; float focus_t, intro;
} Profiles;

static const Color PALETTE[] = { RGBA(0x7B, 0x5B, 0xF5, 0xFF), RGBA(0xFF, 0x6A, 0x5C, 0xFF), RGBA(0x2E, 0xC4, 0xB6, 0xFF), RGBA(0xFF, 0xB5, 0x47, 0xFF),
                                 RGBA(0x3D, 0x9B, 0xFF, 0xFF), RGBA(0xE0, 0x5C, 0xB3, 0xFF), RGBA(0x6B, 0xCB, 0x5A, 0xFF), RGBA(0xF2, 0x8C, 0x38, 0xFF) };

static Color colour_of(const char *email)
{
    unsigned h = 5381;
    for (const char *p = email; *p; p++) h = h * 33 + (unsigned char)*p;
    return PALETTE[h % (sizeof PALETTE / sizeof PALETTE[0])];
}

/* "anna.lee@x.com" -> "Anna Lee" and "AL" */
static void name_of(const char *email, char *name, size_t n, char ini[3])
{
    size_t o = 0; int up = 1;
    ini[0] = ini[1] = ini[2] = 0;
    for (const char *p = email; *p && *p != '@' && o + 1 < n; p++) {
        char c = *p;
        if (c == '.' || c == '_' || c == '-' || c == '+') { if (o && name[o - 1] != ' ') name[o++] = ' '; up = 1; continue; }
        if (up && isalpha((unsigned char)c)) { c = (char)toupper((unsigned char)c); if (!ini[0]) ini[0] = c; else if (!ini[1]) ini[1] = c; }
        up = 0;
        name[o++] = c;
    }
    name[o] = 0;
    if (!ini[0]) ini[0] = '?';
}

int profiles_at_start(void)
{
    char e[1][128];
    return g_app.auth_key && g_app.user_email && g_app.ask_profile && app_saved_accounts(e, 1) > 0;
}

static void update(Screen *s, float dt)
{
    Profiles *p = s->st;
    p->focus_t = ui_ease(p->focus_t * 100, 100, NULL, dt, 16) / 100;
    p->intro = ui_ease(p->intro * 100, 100, NULL, dt, 8) / 100;
}

static void draw(Screen *s)
{
    Profiles *p = s->st;
    ui_background(p->n ? colour_of(p->emails[p->focus]) : 0);
    float k = g_app.fx_light ? 1 : p->intro;
    int a = (int)(255 * k);
    font_draw_center("Who's watching?", SNTV_UI_W / 2, 250 - (1 - k) * 20, FONT_XL | FONT_B, COLOR_ALPHA(THEME_TEXT, a));
    font_draw_center("Each profile is a Stremio account saved on this TV: its own library, addons and progress.", SNTV_UI_W / 2, 336, FONT_S, COLOR_ALPHA(THEME_TEXT_DIM, a));
    float step = (SNTV_UI_W - 2 * THEME_MARGIN) / p->n;   /* many profiles: closer together, all on screen */
    if (step > STEP) step = STEP;
    float total = (p->n - 1) * step, x0 = SNTV_UI_W / 2 - total / 2, cy = 560;
    float tile = step - 40 < TILE ? step - 40 : TILE;
    for (int i = 0; i < p->n; i++) {
        int foc = p->focus == i;
        float f = foc ? (g_app.fx_light ? 1 : p->focus_t) : 0, sc = 1 + 0.10f * f, r = tile / 2 * sc, cx = x0 + i * step;
        char name[128], ini[3];
        name_of(p->emails[i], name, sizeof name, ini);
        Color c = colour_of(p->emails[i]);
        ui_hit(cx - step / 2, cy - tile / 2, step, tile + 120, i);
        if (foc && !g_app.fx_light) gfx_shadow(cx - r, cy - r + 10, 2 * r, 2 * r, r, 40, COLOR_ALPHA(c, 0x90));
        gfx_set_alpha(k * (foc ? 1.0f : 0.72f));
        gfx_rrect_grad_v(cx - r, cy - r, 2 * r, 2 * r, r * 0.42f, c, COLOR_ALPHA(c, 0xB0));
        int fi = tile < 150 ? FONT_XL | FONT_B : FONT_TITLE | FONT_B;
        font_draw_center(ini, cx, cy - font_height(fi) / 2, fi, RGBA(255, 255, 255, 0xF0));
        if (foc) gfx_rrect_ring(cx - r - 6, cy - r - 6, 2 * r + 12, 2 * r + 12, r * 0.42f + 6, 4, THEME_TEXT);
        float ny = cy + tile / 2 + 34;
        float nw = font_measure(name, FONT_S | FONT_B);
        if (nw > step - 20) font_draw_fit(name, cx - (step - 20) / 2, ny, FONT_S | FONT_B, foc ? THEME_TEXT : THEME_TEXT_DIM, step - 20);
        else font_draw(name, cx - nw / 2, ny, FONT_S | FONT_B, foc ? THEME_TEXT : THEME_TEXT_DIM);
        if (i == 0) font_draw_center("Signed in", cx, ny + 40, FONT_XS, THEME_TEXT_MUTED);
        gfx_set_alpha(1);
    }
    font_draw_center("OK  choose   \xC2\xB7   Back  keep the one signed in   \xC2\xB7   Settings > Account adds or forgets profiles", SNTV_UI_W / 2, SNTV_UI_H - 90, FONT_XS, THEME_TEXT_MUTED);
}

static int tap(Screen *s, int id)   /* a profile: chosen */
{
    Profiles *p = s->st;
    if (id < 0 || id >= p->n) return 1;
    p->focus = id;
    return 0;
}

static int key(Screen *s, KeyAction a)
{
    Profiles *p = s->st;
    app_redraw();
    switch (a) {
    case KEY_LEFT: if (p->focus > 0) { p->focus--; p->focus_t = 0; } return 1;
    case KEY_RIGHT: if (p->focus < p->n - 1) { p->focus++; p->focus_t = 0; } return 1;
    case KEY_OK:
        LOGI("profiles: %s", p->focus ? "switching" : "keeping the one signed in");
        if (p->focus > 0) app_switch_account(p->focus - 1);   /* the saved list leaves out the one signed in */
        app_pop();
        return 1;
    case KEY_BACK: app_pop(); return 1;
    default: return 1;
    }
}

static void destroy(Screen *s) { free(s->st); free(s); }

Screen *screen_profiles_create(void)
{
    Screen *s = calloc(1, sizeof *s);
    Profiles *p = calloc(1, sizeof *p);
    if (g_app.user_email) snprintf(p->emails[p->n++], 128, "%s", g_app.user_email);
    p->n += app_saved_accounts(p->emails + p->n, MAX_PROFILES - p->n);
    p->focus_t = 1;
    s->name = "profiles"; s->st = p;
    s->update = update; s->draw = draw; s->key = key; s->tap = tap; s->destroy = destroy;
    return s;
}
