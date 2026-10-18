#include "../app.h"
#include "../log.h"
#include "../store.h"
#include "../platform/platform.h"
#include "../video/video.h"
#include "font.h"
#include "keyboard.h"
#include "theme.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { IT_ACCOUNT, IT_SERVER, IT_INSTALL, IT_ADDONS_HEADER, IT_FIXED_COUNT };
enum { EDIT_NONE, EDIT_SERVER, EDIT_INSTALL };
enum { ROW_H = 76, VISIBLE = 10 };

typedef struct {
    int focus;
    int confirm_remove;     /* addon index awaiting second OK, or -1 */
    int edit;
    Keyboard kb;
} Settings;

static int item_count(void) { return IT_FIXED_COUNT + g_app.n_addons; }

static void draw_row(float y, const char *title, const char *value, int focused, int header)
{
    float x = THEME_MARGIN, w = SNTV_UI_W - 2 * THEME_MARGIN;
    if (header) { font_draw(title, x, y + 24, FONT_M, THEME_TEXT_DIM); return; }
    gfx_rect(x, y, w, ROW_H - 8, focused ? THEME_SURFACE_HI : THEME_SURFACE);
    if (focused) gfx_rect(x, y, 6, ROW_H - 8, THEME_ACCENT);
    font_draw_fit(title, x + 24, y + 18, FONT_M, THEME_TEXT, w * 0.45f);
    if (value) font_draw_fit(value, x + w * 0.5f, y + 20, FONT_S, THEME_TEXT_DIM, w * 0.5f - 24);
}

static void draw(Screen *s)
{
    Settings *st = s->st;
    gfx_rect(0, 0, SNTV_UI_W, SNTV_UI_H, THEME_BG);
    char sub[128];
    snprintf(sub, sizeof sub, "v%s  -  %s", SNTV_APP_VERSION, video_backend_name());
    ui_draw_header("Settings", sub);

    if (st->edit != EDIT_NONE) {
        const char *title = st->edit == EDIT_SERVER ? "Streaming server URL (e.g. http://192.168.1.10:11470)" : "Addon manifest URL";
        font_draw(title, THEME_MARGIN, 160, FONT_M, THEME_TEXT);
        gfx_rect(THEME_MARGIN, 210, keyboard_width(), 64, THEME_SURFACE);
        font_draw_fit(st->kb.text[0] ? st->kb.text : " ", THEME_MARGIN + 20, 226, FONT_M, THEME_TEXT, keyboard_width() - 40);
        keyboard_draw(&st->kb, THEME_MARGIN, 300, 1);
        font_draw("Done saves, Back cancels", THEME_MARGIN, 300 + keyboard_height() + 30, FONT_S, THEME_TEXT_MUTED);
        return;
    }

    int n = item_count();
    int first = st->focus - (VISIBLE - 1);
    if (first < 0) first = 0;
    float y = 150;
    for (int i = first; i < n && i < first + VISIBLE; i++, y += ROW_H) {
        int foc = st->focus == i;
        switch (i) {
        case IT_ACCOUNT:
            draw_row(y, g_app.auth_key ? "Sign out" : "Sign in", g_app.user_email ? g_app.user_email : "Not signed in: using the default Cinemeta catalog", foc, 0);
            break;
        case IT_SERVER:
            draw_row(y, "Streaming server", g_app.streaming_server && *g_app.streaming_server ? g_app.streaming_server : "Not set (torrent streams disabled)", foc, 0);
            break;
        case IT_INSTALL:
            draw_row(y, "Install addon from URL", "Paste a manifest.json URL", foc, 0);
            break;
        case IT_ADDONS_HEADER: {
            char h[64];
            snprintf(h, sizeof h, "Installed addons (%d)%s", g_app.n_addons, g_app.addons_loading ? "  loading..." : "");
            draw_row(y, h, NULL, 0, 1);
            break;
        }
        default: {
            const Addon *a = &g_app.addons[i - IT_FIXED_COUNT];
            char title[200], value[300];
            snprintf(title, sizeof title, "%s  %s", a->manifest.name, a->manifest.version ? a->manifest.version : "");
            if (st->confirm_remove == i - IT_FIXED_COUNT) snprintf(value, sizeof value, "Press OK again to remove");
            else {
                value[0] = 0;
                for (int r = 0; r < a->manifest.n_resources; r++) snprintf(value + strlen(value), sizeof value - strlen(value), "%s%s", value[0] ? ", " : "", a->manifest.resources[r].name);
                if (a->flag_protected) strncat(value, "  (built-in)", sizeof value - strlen(value) - 1);
            }
            draw_row(y, title, value, foc, 0);
        }
        }
    }
}

static void begin_edit(Settings *st, int which)
{
    st->edit = which;
    keyboard_init(&st->kb, 0, which == EDIT_SERVER ? g_app.streaming_server : "https://");
    SDL_StartTextInput();
}

static void finish_edit(Settings *st, int save)
{
    if (save) {
        if (st->edit == EDIT_SERVER) {
            char *v = st->kb.text;
            size_t n = strlen(v);
            while (n && v[n - 1] == '/') v[--n] = 0;
            free(g_app.streaming_server);
            g_app.streaming_server = n ? strdup(v) : NULL;
            store_set_str("streamingServer", g_app.streaming_server);
            store_save();
            app_toast(n ? "Streaming server saved" : "Streaming server cleared");
        } else if (strlen(st->kb.text) > 10) {
            app_install_addon(st->kb.text);
        }
    }
    st->edit = EDIT_NONE;
    SDL_StopTextInput();
}

static int key(Screen *s, KeyAction a)
{
    Settings *st = s->st;
    if (st->edit != EDIT_NONE) {
        if (a == KEY_BACK) { finish_edit(st, 0); return 1; }
        if (a == KEY_BACKSPACE) { keyboard_key(&st->kb, KEY_BACKSPACE); return 1; }
        if (keyboard_key(&st->kb, a) == KB_DONE) finish_edit(st, 1);
        return 1;
    }
    int n = item_count();
    switch (a) {
    case KEY_UP: if (st->focus > 0) st->focus--; if (st->focus == IT_ADDONS_HEADER) st->focus--; st->confirm_remove = -1; return 1;
    case KEY_DOWN: if (st->focus < n - 1) st->focus++; if (st->focus == IT_ADDONS_HEADER && st->focus < n - 1) st->focus++; st->confirm_remove = -1; return 1;
    case KEY_OK:
        if (st->focus == IT_ACCOUNT) { if (g_app.auth_key) { app_logout(); app_toast("Signed out"); } else app_push(screen_login_create()); }
        else if (st->focus == IT_SERVER) begin_edit(st, EDIT_SERVER);
        else if (st->focus == IT_INSTALL) begin_edit(st, EDIT_INSTALL);
        else if (st->focus >= IT_FIXED_COUNT) {
            int idx = st->focus - IT_FIXED_COUNT;
            if (st->confirm_remove == idx) { app_remove_addon(idx); st->confirm_remove = -1; if (st->focus >= item_count()) st->focus = item_count() - 1; }
            else if (g_app.addons[idx].flag_protected) app_toast("Built-in addons cannot be removed");
            else st->confirm_remove = idx;
        }
        return 1;
    default: return 0;
    }
}

static void text(Screen *s, const char *utf8)
{
    Settings *st = s->st;
    if (st->edit != EDIT_NONE) keyboard_text_input(&st->kb, utf8);
}

static void destroy(Screen *s)
{
    Settings *st = s->st;
    if (st->edit != EDIT_NONE) SDL_StopTextInput();
    free(st);
    free(s);
}

Screen *screen_settings_create(void)
{
    Screen *s = calloc(1, sizeof *s);
    Settings *st = calloc(1, sizeof *st);
    st->confirm_remove = -1;
    s->name = "settings"; s->st = st;
    s->draw = draw; s->key = key; s->text = text; s->destroy = destroy;
    return s;
}
