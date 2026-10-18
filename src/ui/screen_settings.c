#include "../app.h"
#include "../image.h"
#include "../lang.h"
#include "../log.h"
#include "../store.h"
#include "../platform/platform.h"
#include "../video/video.h"
#include "font.h"
#include "keyboard.h"
#include "theme.h"
#include "widgets.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { IT_ACCOUNT, IT_SERVER, IT_PLAYBACK_HEADER, IT_PREF_AUDIO, IT_PREF_SUBS, IT_SUBS_WHEN, IT_SUBS_SIZE, IT_SUBS_STYLE, IT_AUTOPLAY, IT_SKIP,
       IT_CLOCK, IT_UI_HEADER, IT_FX, IT_OLED, IT_AMBIENT, IT_BLUR, IT_DIAG, IT_SETUP, IT_INSTALL, IT_ADDONS_HEADER, IT_FIXED_COUNT };
enum { EDIT_NONE, EDIT_SERVER, EDIT_INSTALL };
#define ROW_H 88.0f
#define COL_X 420.0f

typedef struct {
    int focus;
    int confirm_remove;
    int edit;
    Keyboard kb;
    float scroll, scroll_target;
} Settings;

static int item_count(void) { return IT_FIXED_COUNT + g_app.n_addons; }
static int is_header(int i) { return i == IT_PLAYBACK_HEADER || i == IT_UI_HEADER || i == IT_ADDONS_HEADER; }

static const char *lang_name(const char *code) { return code[0] ? lang_display(code) : "Off"; }

static void cycle_lang(char *code, size_t n, int dir)
{
    int count = lang_count(), i = (lang_index(code) + dir + count) % count;
    snprintf(code, n, "%s", LANG_CODES[i]);
}

typedef enum { KIND_ACTION, KIND_TOGGLE, KIND_CHOICE, KIND_HEADER } RowKind;

static void draw_row(float y, Icon icon, const char *title, const char *value, int focused, RowKind kind, int on)
{
    float x = COL_X, w = SNTV_UI_W - THEME_MARGIN - COL_X, h = ROW_H - 12;
    if (kind == KIND_HEADER) { font_draw(title, x + 8, y + 30, FONT_XS | FONT_B, THEME_ACCENT2); return; }
    if (focused && !g_app.fx_light) gfx_shadow(x, y + 6, w, h, 20, 24, RGBA(0, 0, 0, 0x80));
    gfx_rrect(x, y, w, h, 20, focused ? THEME_TEXT : RGBA(0xFF, 0xFF, 0xFF, 0x0F));
    Color fg = focused ? THEME_ON_LIGHT : THEME_TEXT, dim = focused ? RGBA(0x0B, 0x0A, 0x16, 0xA8) : THEME_TEXT_MUTED;
    gfx_circle(x + 46, y + h / 2, 24, focused ? RGBA(0x0B, 0x0A, 0x16, 0x14) : RGBA(0xFF, 0xFF, 0xFF, 0x10));
    ui_icon(icon, x + 46, y + h / 2, 26, fg);
    font_draw_fit(title, x + 92, y + (h - font_height(FONT_S | FONT_B)) / 2, FONT_S | FONT_B, fg, w * 0.48f);
    float rx = x + w - 30;
    if (kind == KIND_TOGGLE) { ui_toggle(rx - 76, y + (h - 40) / 2, on, 0); return; }
    if (kind == KIND_CHOICE) {
        ui_icon(ICON_CHEVRON_RIGHT, rx - 10, y + h / 2, 22, dim);
        float vw = font_measure(value, FONT_S | FONT_B);
        font_draw(value, rx - 36 - vw, y + (h - font_height(FONT_S | FONT_B)) / 2, FONT_S | FONT_B, fg);
        ui_icon(ICON_CHEVRON_LEFT, rx - 60 - vw, y + h / 2, 22, dim);
        return;
    }
    if (value) font_draw_fit(value, x + w * 0.5f, y + (h - font_height(FONT_S)) / 2, FONT_S, dim, w * 0.5f - 40);
}

static void update(Screen *s, float dt)
{
    Settings *st = s->st;
    int first = st->focus - 8;
    st->scroll_target = (first < 0 ? 0 : first) * (float)ROW_H;
    st->scroll = ui_ease(st->scroll, st->scroll_target, NULL, dt, 12);
}

static void draw(Screen *s)
{
    Settings *st = s->st;
    ui_background(0);
    /* left rail */
    font_draw("Settings", THEME_MARGIN, 120, FONT_XL | FONT_B, THEME_TEXT);
    char sub[160];
    snprintf(sub, sizeof sub, "Version %s", SNTV_APP_VERSION);
    font_draw(sub, THEME_MARGIN, 196, FONT_S, THEME_TEXT_DIM);
    font_draw_wrapped(video_backend_name(), THEME_MARGIN, 236, FONT_XS, THEME_TEXT_MUTED, COL_X - THEME_MARGIN - 40, 2);
    snprintf(sub, sizeof sub, "%zu MB artwork in memory", image_cache_bytes() >> 20);
    font_draw(sub, THEME_MARGIN, 296, FONT_XS, THEME_TEXT_MUTED);
    if (g_app.auth_key) {
        float ay = 380;
        gfx_rrect_grad_v(THEME_MARGIN, ay, 72, 72, 36, THEME_ACCENT2, THEME_ACCENT);
        char ini[4] = { g_app.user_email && g_app.user_email[0] ? (char)(g_app.user_email[0] >= 'a' && g_app.user_email[0] <= 'z' ? g_app.user_email[0] - 32 : g_app.user_email[0]) : 'S', 0 };
        font_draw_center(ini, THEME_MARGIN + 36, ay + (72 - font_height(FONT_L | FONT_B)) / 2, FONT_L | FONT_B, THEME_TEXT);
        font_draw_wrapped(g_app.user_email ? g_app.user_email : "Signed in", THEME_MARGIN, ay + 90, FONT_XS | FONT_B, THEME_TEXT_DIM, COL_X - THEME_MARGIN - 40, 2);
    }

    if (st->edit != EDIT_NONE) {
        const char *title = st->edit == EDIT_SERVER ? "Streaming server URL" : "Addon manifest URL";
        const char *hint = st->edit == EDIT_SERVER ? "Any machine on your network running Stremio, e.g. http://192.168.1.10:11470" : "Paste or type the addon's manifest.json address";
        float x = COL_X;
        font_draw(title, x, 110, FONT_L | FONT_B, THEME_TEXT);
        font_draw(hint, x, 166, FONT_S, THEME_TEXT_DIM);
        gfx_rrect(x, 220, keyboard_width(), 76, 38, RGBA(0xFF, 0xFF, 0xFF, 0x18));
        font_draw_fit(st->kb.text[0] ? st->kb.text : " ", x + 32, 220 + (76 - font_height(FONT_M | FONT_B)) / 2, FONT_M | FONT_B, THEME_TEXT, keyboard_width() - 64);
        keyboard_draw(&st->kb, x, 330, 1);
        font_draw("Done saves  \xC2\xB7  Back cancels", x, 330 + keyboard_height() + 30, FONT_S, THEME_TEXT_MUTED);
        return;
    }

    static const char *FX_NAMES[] = { "Automatic", "Full", "Light" };
    static const char *SIZE_NAMES[] = { "Small", "Medium", "Large" };
    int n = item_count();
    gfx_clip_push(0, 90, SNTV_UI_W, SNTV_UI_H - 90);
    float y = 110 - st->scroll;
    for (int i = 0; i < n; i++, y += ROW_H) {
        if (y + ROW_H < 90 || y > SNTV_UI_H) continue;
        int foc = st->focus == i;
        char v[300];
        switch (i) {
        case IT_ACCOUNT:
            draw_row(y, ICON_USER, g_app.auth_key ? "Sign out" : "Sign in to Stremio",
                     g_app.auth_key ? (g_app.user_email ? g_app.user_email : "Signed in") : "Sync addons, Library and Continue Watching", foc, KIND_ACTION, 0);
            break;
        case IT_SERVER:
            draw_row(y, ICON_LINK, "Streaming server", g_app.streaming_server && *g_app.streaming_server ? g_app.streaming_server : "Not set  \xC2\xB7  needed for torrents", foc, KIND_ACTION, 0);
            break;
        case IT_PLAYBACK_HEADER: draw_row(y, ICON_PLAY, "PLAYBACK", NULL, 0, KIND_HEADER, 0); break;
        case IT_PREF_AUDIO: draw_row(y, ICON_AUDIO, "Preferred audio", lang_name(g_app.pref_audio), foc, KIND_CHOICE, 0); break;
        case IT_PREF_SUBS: draw_row(y, ICON_SUBS, "Preferred subtitles", lang_name(g_app.pref_subs), foc, KIND_CHOICE, 0); break;
        case IT_SUBS_SIZE: draw_row(y, ICON_SUBS, "Subtitle size", SIZE_NAMES[g_app.subs_size], foc, KIND_CHOICE, 0); break;
        case IT_SUBS_STYLE: draw_row(y, ICON_SUBS, "Subtitle style", SUBS_STYLE_NAMES[g_app.subs_style], foc, KIND_CHOICE, 0); break;
        case IT_AUTOPLAY: draw_row(y, ICON_NEXT, "Play next episode automatically", NULL, foc, KIND_TOGGLE, g_app.autoplay_next); break;
        case IT_SKIP: {
            static const char *SKIP_NAMES[] = { "Show a Skip button", "Skip automatically", "Off" };
            draw_row(y, ICON_NEXT, "Skip intros and recaps", SKIP_NAMES[g_app.skip_mode], foc, KIND_CHOICE, 0);
            break;
        }
        case IT_SUBS_WHEN:
            draw_row(y, ICON_SUBS, "Turn subtitles on", g_app.subs_always ? "Always" : "Only for other languages", foc, KIND_CHOICE, 0);
            break;
        case IT_CLOCK: {
            static const char *CLOCK_NAMES[] = { "12-hour", "24-hour", "Off" };
            draw_row(y, ICON_PLAY, "Clock and end time in the player", CLOCK_NAMES[g_app.clock_mode], foc, KIND_CHOICE, 0);
            break;
        }
        case IT_UI_HEADER: draw_row(y, ICON_HOME, "INTERFACE", NULL, 0, KIND_HEADER, 0); break;
        case IT_FX:
            snprintf(v, sizeof v, "%s%s", FX_NAMES[g_app.fx_setting], g_app.fx_setting == FX_AUTO ? (g_app.fx_light ? " (Light)" : g_app.frames_measured >= 240 ? " (Full)" : "") : "");
            draw_row(y, ICON_STAR, "Visual effects", v, foc, KIND_CHOICE, 0);
            break;
        case IT_OLED: draw_row(y, ICON_STAR, "Pure black background (OLED)", NULL, foc, KIND_TOGGLE, g_app.oled); break;
        case IT_AMBIENT: {
            char v[32];
            if (g_app.ambient_minutes) snprintf(v, sizeof v, "After %d min", g_app.ambient_minutes); else snprintf(v, sizeof v, "Off");
            draw_row(y, ICON_STAR, "Screensaver", v, foc, KIND_CHOICE, 0);
            break;
        }
        case IT_BLUR: draw_row(y, ICON_LIBRARY, "Blur unwatched episodes", NULL, foc, KIND_TOGGLE, g_app.spoiler_blur); break;
        case IT_DIAG: draw_row(y, ICON_SETTINGS, "Diagnostics", "Log and device info", foc, KIND_ACTION, 0); break;
        case IT_SETUP: draw_row(y, ICON_CHECK, "Run quick setup again", "Languages, skipping, account", foc, KIND_ACTION, 0); break;
        case IT_INSTALL: draw_row(y, ICON_PLUS, "Install addon from URL", "manifest.json", foc, KIND_ACTION, 0); break;
        case IT_ADDONS_HEADER:
            snprintf(v, sizeof v, "ADDONS  \xC2\xB7  %d INSTALLED%s", g_app.n_addons, g_app.addons_loading ? "  \xC2\xB7  SYNCING" : "");
            draw_row(y, ICON_LIBRARY, v, NULL, 0, KIND_HEADER, 0);
            break;
        default: {
            const Addon *a = &g_app.addons[i - IT_FIXED_COUNT];
            char title[200];
            snprintf(title, sizeof title, "%s", a->manifest.name);
            if (st->confirm_remove == i - IT_FIXED_COUNT) snprintf(v, sizeof v, "Press OK again to remove");
            else {
                snprintf(v, sizeof v, "v%s  \xC2\xB7  ", a->manifest.version ? a->manifest.version : "?");
                for (int r = 0; r < a->manifest.n_resources; r++) snprintf(v + strlen(v), sizeof v - strlen(v), "%s%s", r ? ", " : "", a->manifest.resources[r].name);
                if (a->flag_protected) strncat(v, "  \xC2\xB7  built-in", sizeof v - strlen(v) - 1);
            }
            draw_row(y, ICON_LIBRARY, title, v, foc, KIND_ACTION, 0);
        }
        }
    }
    gfx_clip_pop();

    /* live preview of the subtitle look while those rows are focused */
    if (st->edit == EDIT_NONE && (st->focus == IT_SUBS_SIZE || st->focus == IT_SUBS_STYLE)) {
        float band = 300;
        gfx_gradient_v(0, SNTV_UI_H - band, SNTV_UI_W, band, RGBA(0x08, 0x07, 0x12, 0), RGBA(0x08, 0x07, 0x12, 0xF4));
        gfx_rect(0, SNTV_UI_H - 200, SNTV_UI_W, 200, RGBA(0x08, 0x07, 0x12, 0xF4));
        font_draw_center("PREVIEW  \xC2\xB7  applies to subtitles from addons; the TV draws embedded ones itself",
                         SNTV_UI_W / 2, SNTV_UI_H - 190, FONT_XS | FONT_B, THEME_TEXT_MUTED);
        ui_subtitle("Subtitles will look like this.\nTwo lines, the way films do it.", SNTV_UI_H - 40, g_app.subs_size, g_app.subs_style);
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

static void adjust(Settings *st, int dir)
{
    switch (st->focus) {
    case IT_PREF_AUDIO: cycle_lang(g_app.pref_audio, sizeof g_app.pref_audio, dir); break;
    case IT_PREF_SUBS: cycle_lang(g_app.pref_subs, sizeof g_app.pref_subs, dir); break;
    case IT_SUBS_SIZE: g_app.subs_size = (g_app.subs_size + dir + 3) % 3; break;
    case IT_SUBS_STYLE: g_app.subs_style = (g_app.subs_style + dir + SUBS_STYLE_COUNT) % SUBS_STYLE_COUNT; break;
    case IT_FX: g_app.fx_setting = (FxLevel)(((int)g_app.fx_setting + dir + 3) % 3); break;
    case IT_AUTOPLAY: g_app.autoplay_next = !g_app.autoplay_next; break;
    case IT_SKIP: g_app.skip_mode = (g_app.skip_mode + dir + 3) % 3; break;
    case IT_SUBS_WHEN: g_app.subs_always = !g_app.subs_always; break;
    case IT_CLOCK: g_app.clock_mode = (g_app.clock_mode + dir + 3) % 3; break;
    case IT_OLED: g_app.oled = !g_app.oled; break;
    case IT_AMBIENT: {
        static const int STEPS[] = { 0, 2, 5, 10, 20 };
        int k = 0;
        for (int i = 0; i < 5; i++) if (STEPS[i] == g_app.ambient_minutes) k = i;
        g_app.ambient_minutes = STEPS[(k + dir + 5) % 5];
        break;
    }
    case IT_BLUR: g_app.spoiler_blur = !g_app.spoiler_blur; break;
    default: return;
    }
    app_save_prefs();
}

static int key(Screen *s, KeyAction a)
{
    Settings *st = s->st;
    app_redraw();
    if (st->edit != EDIT_NONE) {
        if (a == KEY_BACK) { finish_edit(st, 0); return 1; }
        if (a == KEY_BACKSPACE) { keyboard_key(&st->kb, KEY_BACKSPACE); return 1; }
        if (keyboard_key(&st->kb, a) == KB_DONE) finish_edit(st, 1);
        return 1;
    }
    int n = item_count();
    switch (a) {
    case KEY_UP: do { if (st->focus > 0) st->focus--; } while (is_header(st->focus) && st->focus > 0); st->confirm_remove = -1; return 1;
    case KEY_DOWN: {
        int f = st->focus;
        do { if (f < n - 1) f++; else break; } while (is_header(f));
        st->confirm_remove = -1;
        if (is_header(f) || f == st->focus) return 0;
        st->focus = f;
        return 1;
    }
    case KEY_LEFT: adjust(st, -1); return 1;
    case KEY_RIGHT: adjust(st, 1); return 1;
    case KEY_OK:
        if (st->focus == IT_ACCOUNT) { if (g_app.auth_key) { app_logout(); app_toast("Signed out"); } else app_push(screen_login_create()); }
        else if (st->focus == IT_SERVER) begin_edit(st, EDIT_SERVER);
        else if (st->focus == IT_INSTALL) begin_edit(st, EDIT_INSTALL);
        else if (st->focus == IT_DIAG) app_push(screen_diagnostics_create(0));
        else if (st->focus == IT_SETUP) app_push(screen_welcome_create());
        else if (st->focus == IT_AUTOPLAY || st->focus == IT_SKIP || st->focus == IT_SUBS_WHEN || st->focus == IT_CLOCK || st->focus == IT_OLED || st->focus == IT_AMBIENT || st->focus == IT_BLUR || st->focus == IT_FX || st->focus == IT_SUBS_SIZE || st->focus == IT_SUBS_STYLE || st->focus == IT_PREF_AUDIO || st->focus == IT_PREF_SUBS) adjust(st, 1);
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
    if (st->edit != EDIT_NONE) { keyboard_text_input(&st->kb, utf8); app_redraw(); }
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
    s->update = update; s->draw = draw; s->key = key; s->text = text; s->destroy = destroy;
    return s;
}
