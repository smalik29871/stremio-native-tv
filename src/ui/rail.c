#include "rail.h"
#include "intro.h"
#include "../app.h"
#include "font.h"
#include "theme.h"
#include "widgets.h"
#include <stdio.h>
#include <string.h>

static const char *const LABELS[RAIL_COUNT] = { "Home", "Explore", "Search", "Library", "Calendar", "Settings", "Account" };
static const Icon ICONS[RAIL_COUNT] = { ICON_HOME, ICON_COMPASS, ICON_SEARCH, ICON_LIBRARY, ICON_CALENDAR, ICON_SETTINGS, ICON_USER };

#define RAIL_STEP 88.0f

/* The sections centred on the screen's middle, the account apart at the bottom */
static float item_y(int i)
{
    if (i == RAIL_ACCOUNT) return SNTV_UI_H - 120;
    return SNTV_UI_H / 2.0f - (RAIL_ACCOUNT - 1) * RAIL_STEP / 2 + i * RAIL_STEP;
}

static int g_rail_active;   /* the section of the rail drawn last, for a tap */

/* A phone held upright: the sections as tabs in a pill floating over the bottom of the page,
 * the page fading out under it; the highlight behind the open tab glides to a new one. */
static void draw_tabs(int active)
{
    static const int TABS[] = { RAIL_HOME, RAIL_EXPLORE, RAIL_SEARCH, RAIL_LIBRARY, RAIL_SETTINGS };
    static float glide = -1, glide_v, last_t;
    float W = SNTV_UI_W, m = 28, ph = 116, pw = W - 2 * m, px = m, py = SNTV_UI_H - ph - 14;
    float tw = (pw - 16) / 5;
    gfx_gradient_v(0, SNTV_UI_H - PHONE_TABS_H - 30, W, PHONE_TABS_H + 30, COLOR_ALPHA(THEME_BG, 0), COLOR_ALPHA(THEME_BG, 0xF0));
    ui_panel(px, py, pw, ph, ph / 2, COLOR_ALPHA(THEME_BG2, 0xF4));
    gfx_rrect_ring(px, py, pw, ph, ph / 2, 2, COLOR_ALPHA(THEME_TEXT, 0x16));
    int sel = -1;
    for (int i = 0; i < 5; i++) if (TABS[i] == active || (TABS[i] == RAIL_LIBRARY && active == RAIL_CALENDAR)) sel = i;
    if (sel >= 0) {
        float want = px + 8 + tw * sel, dt = g_app.time - last_t;
        if (dt < 0 || dt > 0.05f) dt = 0.05f;
        last_t = g_app.time;
        if (glide < 0 || g_app.fx_light) { glide = want; glide_v = 0; }
        else glide = ui_ease(glide, want, &glide_v, dt, 16);
        if (SDL_fabsf(glide - want) > 0.5f) app_redraw();
        gfx_rrect(glide, py + 8, tw, ph - 16, (ph - 16) / 2, COLOR_ALPHA(THEME_TEXT, 0x1C));
    }
    for (int i = 0; i < 5; i++) {
        int t = TABS[i], on = i == sel;
        Color c = on ? THEME_TEXT : THEME_TEXT_MUTED;
        float cx = px + 8 + tw * i + tw / 2;
        ui_icon(ICONS[t], cx, py + 44, 40, c);
        font_draw_center(LABELS[t], cx, py + 74, FONT_XS | FONT_B, c);
        ui_hit(px + 8 + tw * i, py, tw, ph, HIT_RAIL + t);
    }
}

void rail_draw(int active, int focus)
{
    g_rail_active = active;
    if (SNTV_PHONE) { draw_tabs(active); return; }
    int open = focus >= 0;
    float w = open ? 340 : RAIL_W;
    if (open) {
        gfx_rect(0, 0, SNTV_UI_W, SNTV_UI_H, COLOR_ALPHA(THEME_BG, 0x9C));   /* the content steps back */
        gfx_gradient_h(0, 0, w + 260, SNTV_UI_H, COLOR_ALPHA(THEME_BG, 0xF4), COLOR_ALPHA(THEME_BG, 0));
    }
    else gfx_gradient_h(0, 0, RAIL_W + 60, SNTV_UI_H, COLOR_ALPHA(THEME_BG, 0xB8), COLOR_ALPHA(THEME_BG, 0));
    /* brand */
    float bs = 48, bx = (RAIL_W - bs) / 2;
    if (!intro_active()) ui_mark(bx, 44, bs);   /* the launch animation is still bringing it here */
    if (open) font_draw(SNTV_APP_NAME, RAIL_W, 44 + (bs - font_height(FONT_M | FONT_ALT)) / 2, FONT_M | FONT_ALT, THEME_TEXT);

    const char *acc = g_app.auth_key ? (g_app.user_email ? g_app.user_email : "Account") : "Stremio sync";
    for (int i = 0; i < RAIL_COUNT; i++) {
        float cy = item_y(i), cx = RAIL_W / 2;
        int foc = focus == i, act = active == i;
        const char *label = i == RAIL_ACCOUNT ? acc : LABELS[i];
        if (foc) {
            float pw = open ? font_measure(label, FONT_S | FONT_B) + 96 + 28 : 64;
            ui_focus_under(cx - 32, cy - 32, pw, 64, theme_pill_r(64), 1);
            gfx_rrect(cx - 32, cy - 32, pw, 64, theme_pill_r(64), THEME_FOCUS);
        } else if (act) gfx_rrect(0, cy - 16, 5, 32, 2.5f, THEME_ACCENT);
        Color fg = foc ? THEME_ON_FOCUS : act ? THEME_TEXT : THEME_TEXT_DIM;
        ui_icon(ICONS[i], cx, cy, 28, fg);
        if (open) font_draw_fit(label, RAIL_W + 8, cy - font_height(FONT_S | FONT_B) / 2, FONT_S | FONT_B, fg, 220);
        ui_hit(0, cy - RAIL_STEP / 2, open ? w : RAIL_W, RAIL_STEP, HIT_RAIL + i);
    }
}

void rail_tap(int target)
{
    Screen *top = app_top();
    if (SNTV_PHONE && target == g_rail_active && top && !strcmp(top->name, "home") && top->tap && top->tap(top, HIT_TOP)) return;   /* Home again: back to its top */
    rail_go(g_rail_active, target);
}

int rail_key(KeyAction a, int *focus, int active)
{
    switch (a) {
    case KEY_UP: if (*focus > 0) (*focus)--; app_redraw(); return 1;
    case KEY_DOWN: if (*focus < RAIL_COUNT - 1) (*focus)++; app_redraw(); return 1;
    case KEY_RIGHT: app_redraw(); return 2;
    case KEY_BACK: return 0;
    case KEY_OK: if (*focus == active) { app_redraw(); return 2; } rail_go(active, *focus); return 1;   /* OK on this section: back to it */
    case KEY_LEFT: return 1;
    default: return 0;
    }
}

void rail_go(int active, int target)
{
    Screen *s = NULL;
    switch (target) {
    case RAIL_HOME: if (active != RAIL_HOME) app_pop(); return;
    case RAIL_EXPLORE: if (active == RAIL_EXPLORE) return; s = screen_explore_create(); break;
    case RAIL_LIBRARY: if (active == RAIL_LIBRARY) return; s = screen_library_create(); break;
    case RAIL_CALENDAR: if (active == RAIL_CALENDAR) return; s = screen_calendar_create(); break;
    case RAIL_SEARCH: app_push(screen_search_create()); return;
    case RAIL_SETTINGS: app_open_settings(); return;
    case RAIL_ACCOUNT: if (g_app.auth_key) app_open_settings(); else app_push(screen_login_create()); return;
    default: return;
    }
    if (active == RAIL_HOME) app_push(s); else app_replace(s);
}
