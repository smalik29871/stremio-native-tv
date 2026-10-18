#include "rail.h"
#include "../app.h"
#include "font.h"
#include "theme.h"
#include "widgets.h"
#include <stdio.h>
#include <string.h>

static const char *const LABELS[RAIL_COUNT] = { "Home", "Explore", "Search", "Library", "Calendar", "Settings", "Account" };
static const Icon ICONS[RAIL_COUNT] = { ICON_HOME, ICON_COMPASS, ICON_SEARCH, ICON_LIBRARY, ICON_CALENDAR, ICON_SETTINGS, ICON_USER };

static float item_y(int i)
{
    if (i == RAIL_ACCOUNT) return SNTV_UI_H - 120;
    return 180 + i * 88;
}

void rail_draw(int active, int focus)
{
    int open = focus >= 0;
    float w = open ? 340 : RAIL_W;
    if (open) {
        gfx_rect(0, 0, SNTV_UI_W, SNTV_UI_H, COLOR_ALPHA(THEME_BG, 0x9C));   /* the content steps back */
        gfx_gradient_h(0, 0, w + 260, SNTV_UI_H, COLOR_ALPHA(THEME_BG, 0xF4), COLOR_ALPHA(THEME_BG, 0));
    }
    else gfx_gradient_h(0, 0, RAIL_W + 60, SNTV_UI_H, COLOR_ALPHA(THEME_BG, 0xB8), COLOR_ALPHA(THEME_BG, 0));
    /* brand */
    float bs = 48, bx = (RAIL_W - bs) / 2;
    ui_mark(bx, 44, bs);
    if (open) font_draw("Stremio", RAIL_W, 44 + (bs - font_height(FONT_M | FONT_B)) / 2, FONT_M | FONT_B, THEME_TEXT);

    const char *acc = g_app.auth_key ? (g_app.user_email ? g_app.user_email : "Account") : "Sign in";
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
    }
}

int rail_key(KeyAction a, int *focus, int active)
{
    switch (a) {
    case KEY_UP: if (*focus > 0) (*focus)--; app_redraw(); return 1;
    case KEY_DOWN: if (*focus < RAIL_COUNT - 1) (*focus)++; app_redraw(); return 1;
    case KEY_RIGHT: app_redraw(); return 2;
    case KEY_BACK: return 0;
    case KEY_OK: rail_go(active, *focus); return 1;
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
