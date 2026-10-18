/* Settings: categories down the left, their rows on the right. Left/Right change a
 * choice, OK toggles or opens, Back returns to the categories. Home rows can be
 * reordered and hidden; addons can be reordered (synced to the account). */
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

enum { CAT_ACCOUNT, CAT_PLAYBACK, CAT_LANG, CAT_SOURCES, CAT_INTERFACE, CAT_ROWS, CAT_ADDONS, CAT_KEYS, CAT_PARENTAL, CAT_ABOUT, CAT_COUNT };
static const char *const CAT_NAMES[CAT_COUNT] = { "Account", "Playback", "Audio & subtitles", "Sources", "Interface", "Home rows", "Addons", "API keys", "Parental", "About" };
static const char *const CAT_DESC[CAT_COUNT] = {
    "Your Stremio account and the streaming server for torrents.",
    "What happens while a video plays.",
    "Preferred languages and how subtitles look.",
    "How sources are ranked and checked for this TV.",
    "The look of the app.",
    "Which rows Home shows, and in what order. OK shows or hides a row, Left/Right moves it.",
    "Installed addons, in the order they answer. Left/Right moves an addon; the order is kept in your account.",
    "Your own keys for services that need one. Each unlocks a feature; nothing is asked without a key.",
    "A PIN that keeps Settings closed and adult addons out of the catalogs.",
    "Logs, device information and setup." };
static const Icon CAT_ICONS[CAT_COUNT] = { ICON_USER, ICON_PLAY, ICON_SUBS, ICON_SWAP, ICON_HOME, ICON_LIST, ICON_LIBRARY, ICON_LINK, ICON_LOCK, ICON_INFO };

enum { IT_ACCOUNT, IT_SERVER, IT_AUTOPLAY, IT_SKIP, IT_CLOCK, IT_SCRUB, IT_PREF_AUDIO, IT_PREF_SUBS, IT_SUBS_WHEN, IT_SUBS_SIZE, IT_SUBS_STYLE,
       IT_QUALITY, IT_RES_CAP, IT_PREFLIGHT, IT_AUTO_REC, IT_HOME_STYLE, IT_HIDE_WATCHED, IT_FX, IT_OLED, IT_AMBIENT, IT_BLUR,
       IT_INSTALL, IT_DIAG, IT_SETUP, IT_THUMBS, IT_PIN, IT_PIN_SETTINGS, IT_HIDE_ADULT, IT_TMDB, IT_MEMORY, IT_TRANSITIONS,
       IT_ROWS_BASE = 1000, IT_ADDON_BASE = 2000, IT_ACCT_BASE = 3000 };
enum { EDIT_NONE, EDIT_SERVER, EDIT_INSTALL, EDIT_THUMBS, EDIT_TMDB };
enum { MAX_ROWS = 160 };
#define ROW_H 88.0f
#define COL_X 560.0f
#define ROWS_TOP 230.0f

typedef struct {
    int cat;                 /* the category shown */
    int zone;                /* 0 categories, 1 rows */
    int focus;               /* row index within the category */
    int confirm_remove;      /* addon index awaiting the second OK */
    int edit;
    Keyboard kb;
    float scroll, scroll_target;
    float pane_t; int pane_cat;   /* rows slide in when the category changes */
    HomeRowDef rows[MAX_ROWS]; int n_rows; int rows_seen;
    char accounts[8][128]; int n_accounts;
} Settings;

static int rows_for(Settings *st, int cat, int *ids, int max)
{
    int n = 0;
    switch (cat) {
    case CAT_ACCOUNT:
        ids[n++] = IT_ACCOUNT;
        st->n_accounts = app_saved_accounts(st->accounts, 8);
        for (int i = 0; i < st->n_accounts && n < max; i++) ids[n++] = IT_ACCT_BASE + i;
        ids[n++] = IT_SERVER;
        break;
    case CAT_PLAYBACK: ids[n++] = IT_AUTOPLAY; ids[n++] = IT_SKIP; ids[n++] = IT_CLOCK; ids[n++] = IT_SCRUB; ids[n++] = IT_THUMBS; break;
    case CAT_PARENTAL: ids[n++] = IT_PIN; if (g_app.parental_pin[0]) { ids[n++] = IT_PIN_SETTINGS; ids[n++] = IT_HIDE_ADULT; } break;
    case CAT_LANG: ids[n++] = IT_PREF_AUDIO; ids[n++] = IT_PREF_SUBS; ids[n++] = IT_SUBS_WHEN; ids[n++] = IT_SUBS_SIZE; ids[n++] = IT_SUBS_STYLE; break;
    case CAT_SOURCES: ids[n++] = IT_QUALITY; ids[n++] = IT_RES_CAP; ids[n++] = IT_PREFLIGHT; ids[n++] = IT_AUTO_REC; break;
    case CAT_INTERFACE: ids[n++] = IT_HOME_STYLE; ids[n++] = IT_HIDE_WATCHED; ids[n++] = IT_FX; ids[n++] = IT_TRANSITIONS; ids[n++] = IT_OLED; ids[n++] = IT_AMBIENT; ids[n++] = IT_BLUR; ids[n++] = IT_MEMORY; break;
    case CAT_KEYS: ids[n++] = IT_TMDB; break;
    case CAT_ROWS:
        if (st->rows_seen != g_app.home_rows_version + g_app.addons_version * 1000) { st->n_rows = app_home_rows(st->rows, MAX_ROWS); st->rows_seen = g_app.home_rows_version + g_app.addons_version * 1000; }
        for (int i = 0; i < st->n_rows && n < max; i++) ids[n++] = IT_ROWS_BASE + i;
        break;
    case CAT_ADDONS: ids[n++] = IT_INSTALL; for (int i = 0; i < g_app.n_addons && n < max; i++) ids[n++] = IT_ADDON_BASE + i; break;
    case CAT_ABOUT: ids[n++] = IT_DIAG; ids[n++] = IT_SETUP; break;
    }
    return n;
}

static const char *lang_name(const char *code) { return code[0] ? lang_display(code) : "Off"; }

static void cycle_lang(char *code, size_t n, int dir)
{
    int count = lang_count(), i = (lang_index(code) + dir + count) % count;
    snprintf(code, n, "%s", LANG_CODES[i]);
}

typedef enum { KIND_ACTION, KIND_TOGGLE, KIND_CHOICE, KIND_MOVABLE } RowKind;

static void draw_row(float y, Icon icon, const char *title, const char *value, int focused, RowKind kind, int on)
{
    float x = COL_X, w = SNTV_UI_W - THEME_MARGIN - COL_X, h = ROW_H - 12;
    if (focused && !g_app.fx_light) gfx_shadow(x, y + 6, w, h, 20, 24, RGBA(0, 0, 0, 0x80));
    gfx_rrect(x, y, w, h, 20, focused ? THEME_TEXT : RGBA(0xFF, 0xFF, 0xFF, 0x0F));
    Color fg = focused ? THEME_ON_LIGHT : THEME_TEXT, dim = focused ? RGBA(0x0B, 0x0A, 0x16, 0xA8) : THEME_TEXT_MUTED;
    gfx_circle(x + 46, y + h / 2, 24, focused ? RGBA(0x0B, 0x0A, 0x16, 0x14) : RGBA(0xFF, 0xFF, 0xFF, 0x10));
    ui_icon(icon, x + 46, y + h / 2, 26, fg);
    font_draw_fit(title, x + 92, y + (h - font_height(FONT_S | FONT_B)) / 2, FONT_S | FONT_B, fg, w * 0.5f);
    float rx = x + w - 30;
    if (kind == KIND_TOGGLE) { ui_toggle(rx - 76, y + (h - 40) / 2, on, 0); return; }
    if (kind == KIND_MOVABLE) {   /* shown/hidden toggle plus move arrows */
        ui_toggle(rx - 76, y + (h - 40) / 2, on, 0);
        ui_icon(ICON_CHEVRON_LEFT, rx - 130, y + h / 2, 22, dim);
        ui_icon(ICON_CHEVRON_RIGHT, rx - 100, y + h / 2, 22, dim);
        if (value) font_draw_fit(value, x + 92 + w * 0.5f + 10, y + (h - font_height(FONT_XS)) / 2, FONT_XS, dim, w * 0.5f - 260);
        return;
    }
    if (kind == KIND_CHOICE) {
        ui_icon(ICON_CHEVRON_RIGHT, rx - 10, y + h / 2, 22, dim);
        float vw = font_measure(value, FONT_S | FONT_B);
        font_draw(value, rx - 36 - vw, y + (h - font_height(FONT_S | FONT_B)) / 2, FONT_S | FONT_B, fg);
        ui_icon(ICON_CHEVRON_LEFT, rx - 60 - vw, y + h / 2, 22, dim);
        return;
    }
    if (value) font_draw_fit(value, x + w * 0.52f, y + (h - font_height(FONT_S)) / 2, FONT_S, dim, w * 0.48f - 40);
}

static void update(Screen *s, float dt)
{
    Settings *st = s->st;
    int first = st->focus - 7;
    st->scroll_target = (first < 0 ? 0 : first) * (float)ROW_H;
    st->scroll = ui_ease(st->scroll, st->scroll_target, NULL, dt, 12);
    if (st->pane_cat != st->cat) { st->pane_cat = st->cat; st->pane_t = g_app.fx_light ? 1 : 0; }
    if (st->pane_t < 1) { st->pane_t += dt * 5; if (st->pane_t > 1) st->pane_t = 1; app_redraw(); }
}

static void draw_categories(const Settings *st)
{
    font_draw("Settings", THEME_MARGIN, 100, FONT_XL | FONT_B, THEME_TEXT);
    for (int i = 0; i < CAT_COUNT; i++) {
        float y = ROWS_TOP + i * 76, w = COL_X - THEME_MARGIN - 70, h = 64;
        int foc = st->zone == 0 && st->cat == i, act = st->cat == i;
        if (foc) { if (!g_app.fx_light) gfx_shadow(THEME_MARGIN, y + 6, w, h, 32, 20, RGBA(0, 0, 0, 0x70)); gfx_rrect(THEME_MARGIN, y, w, h, 32, THEME_TEXT); }
        else if (act) gfx_rrect(THEME_MARGIN, y, w, h, 32, RGBA(0xFF, 0xFF, 0xFF, 0x16));
        Color fg = foc ? THEME_ON_LIGHT : act ? THEME_TEXT : THEME_TEXT_DIM;
        ui_icon(CAT_ICONS[i], THEME_MARGIN + 36, y + h / 2, 26, fg);
        font_draw(CAT_NAMES[i], THEME_MARGIN + 70, y + (h - font_height(FONT_S | FONT_B)) / 2, FONT_S | FONT_B, fg);
        if (i == CAT_ADDONS) { char c[16]; snprintf(c, sizeof c, "%d", g_app.n_addons); font_draw(c, THEME_MARGIN + w - 24 - font_measure(c, FONT_XS | FONT_B), y + (h - font_height(FONT_XS | FONT_B)) / 2, FONT_XS | FONT_B, foc ? RGBA(0x0B, 0x0A, 0x16, 0xA8) : THEME_TEXT_MUTED); }
    }
}

static void draw(Screen *s)
{
    Settings *st = s->st;
    ui_background(0);
    draw_categories(st);

    if (st->edit != EDIT_NONE) {
        const char *title = st->edit == EDIT_SERVER ? "Streaming server URL" : st->edit == EDIT_THUMBS ? "Seek preview server" : st->edit == EDIT_TMDB ? "TMDB API key" : "Addon manifest URL";
        const char *hint = st->edit == EDIT_SERVER ? "Any machine on your network running Stremio, e.g. http://192.168.1.10:11470"
                         : st->edit == EDIT_THUMBS ? "tools/thumbs/server.py on a computer or NAS with ffmpeg, e.g. http://192.168.1.10:11480. Empty turns previews off."
                         : st->edit == EDIT_TMDB ? "Free at themoviedb.org > Settings > API (the v3 key, 32 characters). Empty removes it."
                         : "Paste or type the addon's manifest.json address";
        float x = COL_X;
        font_draw(title, x, 100, FONT_L | FONT_B, THEME_TEXT);
        font_draw(hint, x, 156, FONT_S, THEME_TEXT_DIM);
        gfx_rrect(x, 210, keyboard_width(), 76, 38, RGBA(0xFF, 0xFF, 0xFF, 0x18));
        font_draw_fit(st->kb.text[0] ? st->kb.text : " ", x + 32, 210 + (76 - font_height(FONT_M | FONT_B)) / 2, FONT_M | FONT_B, THEME_TEXT, keyboard_width() - 64);
        keyboard_draw(&st->kb, x, 320, 1);
        font_draw("Done saves  \xC2\xB7  Back cancels", x, 320 + keyboard_height() + 30, FONT_S, THEME_TEXT_MUTED);
        return;
    }

    /* pane header */
    font_draw(CAT_NAMES[st->cat], COL_X, 100, FONT_L | FONT_B, THEME_TEXT);
    font_draw_wrapped(CAT_DESC[st->cat], COL_X, 156, FONT_S, THEME_TEXT_DIM, SNTV_UI_W - THEME_MARGIN - COL_X, 2);
    if (st->cat == CAT_ABOUT) {
        char info[300];
        snprintf(info, sizeof info, "Version %s  \xC2\xB7  %s  \xC2\xB7  %zu MB artwork in memory", SNTV_APP_VERSION, video_backend_name(), image_cache_bytes() >> 20);
        font_draw_fit(info, COL_X, SNTV_UI_H - 110, FONT_XS, THEME_TEXT_MUTED, SNTV_UI_W - THEME_MARGIN - COL_X);
        char tv[300];
        snprintf(tv, sizeof tv, "TV profile: %s", "learned from what played here; Diagnostics shows it");
        font_draw_fit(tv, COL_X, SNTV_UI_H - 80, FONT_XS, THEME_TEXT_MUTED, SNTV_UI_W - THEME_MARGIN - COL_X);
    }

    static const char *FX_NAMES[] = { "Automatic", "Full", "Light" };
    static const char *SIZE_NAMES[] = { "Small", "Medium", "Large" };
    static const char *CLOCK_NAMES[] = { "12-hour", "24-hour", "Off" };
    static const char *SKIP_NAMES[] = { "Show a Skip button", "Skip automatically", "Off" };
    int ids[MAX_ROWS + 8];
    int n = rows_for(st, st->cat, ids, MAX_ROWS + 8);
    if (st->focus >= n) st->focus = n ? n - 1 : 0;
    gfx_clip_push(COL_X - 40, ROWS_TOP - 20, SNTV_UI_W - COL_X + 40, SNTV_UI_H - ROWS_TOP + 20);
    { float e = 1 - (1 - st->pane_t) * (1 - st->pane_t); gfx_set_translate(40 * (1 - e), 0); }
    float y = ROWS_TOP - st->scroll;
    for (int i = 0; i < n; i++, y += ROW_H) {
        if (y + ROW_H < ROWS_TOP - 20 || y > SNTV_UI_H) continue;
        int foc = st->zone == 1 && st->focus == i;
        int id = ids[i];
        char v[300];
        switch (id) {
        case IT_ACCOUNT:
            draw_row(y, ICON_USER, g_app.auth_key ? "Sign out" : "Sign in to Stremio",
                     g_app.auth_key ? (g_app.user_email ? g_app.user_email : "Signed in") : "Sync addons, Library and Continue Watching", foc, KIND_ACTION, 0);
            break;
        case IT_SERVER:
            draw_row(y, ICON_LINK, "Streaming server", g_app.streaming_server && *g_app.streaming_server ? g_app.streaming_server : "Not set  \xC2\xB7  needed for torrents", foc, KIND_ACTION, 0);
            break;
        case IT_PREF_AUDIO: draw_row(y, ICON_AUDIO, "Preferred audio", lang_name(g_app.pref_audio), foc, KIND_CHOICE, 0); break;
        case IT_PREF_SUBS: draw_row(y, ICON_SUBS, "Preferred subtitles", lang_name(g_app.pref_subs), foc, KIND_CHOICE, 0); break;
        case IT_SUBS_SIZE: draw_row(y, ICON_SUBS, "Subtitle size", SIZE_NAMES[g_app.subs_size], foc, KIND_CHOICE, 0); break;
        case IT_SUBS_STYLE: draw_row(y, ICON_SUBS, "Subtitle style", SUBS_STYLE_NAMES[g_app.subs_style], foc, KIND_CHOICE, 0); break;
        case IT_AUTOPLAY: draw_row(y, ICON_NEXT, "Play next episode automatically", NULL, foc, KIND_TOGGLE, g_app.autoplay_next); break;
        case IT_SKIP: draw_row(y, ICON_NEXT, "Skip intros and recaps", SKIP_NAMES[g_app.skip_mode], foc, KIND_CHOICE, 0); break;
        case IT_SUBS_WHEN: draw_row(y, ICON_SUBS, "Turn subtitles on", g_app.subs_always ? "Always" : "Only for other languages", foc, KIND_CHOICE, 0); break;
        case IT_CLOCK: draw_row(y, ICON_CLOCK, "Clock (every screen; hidden while a video plays)", CLOCK_NAMES[g_app.clock_mode], foc, KIND_CHOICE, 0); break;
        case IT_QUALITY: draw_row(y, ICON_SWAP, "Sort sources by", QUAL_MODE_NAMES[g_app.quality_mode], foc, KIND_CHOICE, 0); break;
        case IT_RES_CAP: {
            const char *cap = g_app.res_cap >= 2160 ? "Up to 4K" : g_app.res_cap >= 1080 ? "Up to 1080p" : "Up to 720p";
            draw_row(y, ICON_STAR, "Resolution for recommendations", cap, foc, KIND_CHOICE, 0);
            break;
        }
        case IT_PREFLIGHT: draw_row(y, ICON_CHECK, "Check sources before recommending", NULL, foc, KIND_TOGGLE, g_app.preflight); break;
        case IT_SCRUB: draw_row(y, ICON_PLAY, "Show the picture while seeking", NULL, foc, KIND_TOGGLE, g_app.live_scrub); break;
        case IT_THUMBS: draw_row(y, ICON_ASPECT, "Seek preview server", g_app.thumb_server ? g_app.thumb_server : "Not set  \xC2\xB7  frames while seeking, from your own ffmpeg server", foc, KIND_ACTION, 0); break;
        case IT_PIN: draw_row(y, ICON_LOCK, g_app.parental_pin[0] ? "Parental PIN" : "Set a parental PIN", g_app.parental_pin[0] ? "Set  \xC2\xB7  OK to remove it (asks for the PIN)" : "Four digits; needed for the switches below", foc, KIND_ACTION, 0); break;
        case IT_PIN_SETTINGS: draw_row(y, ICON_SETTINGS, "Ask for the PIN to open Settings", NULL, foc, KIND_TOGGLE, g_app.pin_settings); break;
        case IT_HIDE_ADULT: draw_row(y, ICON_LIBRARY, "Hide catalogs of addons flagged adult", NULL, foc, KIND_TOGGLE, g_app.hide_adult); break;
        case IT_TMDB: {
            char masked[96] = "Not set  \xC2\xB7  cast photos, age ratings (PG-13, TV-MA)";
            if (g_app.tmdb_key) snprintf(masked, sizeof masked, "%.4s\xE2\x80\xA6%s  \xC2\xB7  cast photos, age ratings on", g_app.tmdb_key, strlen(g_app.tmdb_key) > 4 ? g_app.tmdb_key + strlen(g_app.tmdb_key) - 4 : "");
            draw_row(y, ICON_LINK, "The Movie Database (TMDB)", masked, foc, KIND_ACTION, 0);
            break;
        }
        case IT_MEMORY: {
            static const char *MEM[] = { "Automatic", "Low", "Normal" };
            snprintf(v, sizeof v, "%s%s", MEM[g_app.memory_mode], g_app.memory_mode == 0 ? (g_app.low_memory ? " (Low)" : " (Normal)") : "");
            draw_row(y, ICON_SETTINGS, "Memory use (rows and artwork kept)", v, foc, KIND_CHOICE, 0);
            break;
        }
        case IT_TRANSITIONS: draw_row(y, ICON_STAR, "Animated screen changes", NULL, foc, KIND_TOGGLE, g_app.transitions); break;
        case IT_AUTO_REC: draw_row(y, ICON_PLAY, "Play the recommended source automatically", NULL, foc, KIND_TOGGLE, g_app.auto_recommended); break;
        case IT_HOME_STYLE: draw_row(y, ICON_HOME, "Home screen", HOME_STYLE_NAMES[g_app.home_style], foc, KIND_CHOICE, 0); break;
        case IT_HIDE_WATCHED: draw_row(y, ICON_CHECK, "Hide watched titles in catalogs", NULL, foc, KIND_TOGGLE, g_app.hide_watched); break;
        case IT_FX:
            snprintf(v, sizeof v, "%s%s", FX_NAMES[g_app.fx_setting], g_app.fx_setting == FX_AUTO ? (g_app.fx_light ? " (Light)" : g_app.frames_measured >= 240 ? " (Full)" : "") : "");
            draw_row(y, ICON_STAR, "Visual effects", v, foc, KIND_CHOICE, 0);
            break;
        case IT_OLED: draw_row(y, ICON_STAR, "Pure black background (OLED)", NULL, foc, KIND_TOGGLE, g_app.oled); break;
        case IT_AMBIENT:
            if (g_app.ambient_minutes) snprintf(v, sizeof v, "After %d min", g_app.ambient_minutes); else snprintf(v, sizeof v, "Off");
            draw_row(y, ICON_STAR, "Screensaver", v, foc, KIND_CHOICE, 0);
            break;
        case IT_BLUR: draw_row(y, ICON_LIBRARY, "Blur unwatched episodes", NULL, foc, KIND_TOGGLE, g_app.spoiler_blur); break;
        case IT_DIAG: draw_row(y, ICON_SETTINGS, "Diagnostics", "Log, device info, QR export", foc, KIND_ACTION, 0); break;
        case IT_SETUP: draw_row(y, ICON_CHECK, "Run quick setup again", "Languages, skipping, account", foc, KIND_ACTION, 0); break;
        case IT_INSTALL: draw_row(y, ICON_PLUS, "Install addon from URL", "manifest.json", foc, KIND_ACTION, 0); break;
        default:
            if (id >= IT_ACCT_BASE) {
                char t[160]; snprintf(t, sizeof t, "Switch to %s", st->accounts[id - IT_ACCT_BASE]);
                draw_row(y, ICON_USER, t, "Saved sign-in  \xC2\xB7  OK switches  \xC2\xB7  Left forgets it", foc, KIND_ACTION, 0);
            } else if (id >= IT_ADDON_BASE) {
                const Addon *a = &g_app.addons[id - IT_ADDON_BASE];
                char title[200];
                snprintf(title, sizeof title, "%d.  %s", id - IT_ADDON_BASE + 1, a->manifest.name);
                if (st->confirm_remove == id - IT_ADDON_BASE) snprintf(v, sizeof v, "Press OK again to remove");
                else {
                    snprintf(v, sizeof v, "v%s  \xC2\xB7  ", a->manifest.version ? a->manifest.version : "?");
                    for (int r = 0; r < a->manifest.n_resources; r++) snprintf(v + strlen(v), sizeof v - strlen(v), "%s%s", r ? ", " : "", a->manifest.resources[r].name);
                    if (a->flag_protected) strncat(v, "  \xC2\xB7  built-in", sizeof v - strlen(v) - 1);
                }
                draw_row(y, ICON_LIBRARY, title, v, foc, KIND_ACTION, 0);
                if (foc) { ui_icon(ICON_CHEVRON_LEFT, SNTV_UI_W - THEME_MARGIN - 90, y + (ROW_H - 12) / 2, 22, RGBA(0x0B, 0x0A, 0x16, 0xA8)); ui_icon(ICON_CHEVRON_RIGHT, SNTV_UI_W - THEME_MARGIN - 56, y + (ROW_H - 12) / 2, 22, RGBA(0x0B, 0x0A, 0x16, 0xA8)); }
            } else if (id >= IT_ROWS_BASE) {
                const HomeRowDef *d = &st->rows[id - IT_ROWS_BASE];
                char pos[32]; snprintf(pos, sizeof pos, "%d of %d", id - IT_ROWS_BASE + 1, st->n_rows);
                draw_row(y, d->kind == 2 ? ICON_LIBRARY : d->kind == 0 ? ICON_PLAY : ICON_STAR, d->title, pos, foc, KIND_MOVABLE, !d->hidden);
            }
        }
    }
    gfx_set_translate(0, 0);
    gfx_clip_pop();
    if (st->zone == 1 && st->cat != CAT_ABOUT) font_draw("Back: categories", SNTV_UI_W - THEME_MARGIN - font_measure("Back: categories", FONT_XS), 110, FONT_XS, THEME_TEXT_MUTED);

    /* live preview of the subtitle look while those rows are focused */
    if (st->zone == 1 && n && (ids[st->focus] == IT_SUBS_SIZE || ids[st->focus] == IT_SUBS_STYLE)) {
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
    keyboard_init(&st->kb, 0, which == EDIT_SERVER ? g_app.streaming_server : which == EDIT_THUMBS ? (g_app.thumb_server ? g_app.thumb_server : "http://") : which == EDIT_TMDB ? (g_app.tmdb_key ? g_app.tmdb_key : "") : "https://");
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
        } else if (st->edit == EDIT_TMDB) {
            char *v = st->kb.text;
            while (*v == ' ') v++;
            size_t n = strlen(v);
            while (n && v[n - 1] == ' ') v[--n] = 0;
            free(g_app.tmdb_key);
            g_app.tmdb_key = n >= 20 ? strdup(v) : NULL;
            app_save_prefs();
            app_toast(g_app.tmdb_key ? "TMDB key saved: cast photos and age ratings on" : n ? "That does not look like a TMDB key" : "TMDB key removed");
        } else if (st->edit == EDIT_THUMBS) {
            char *v = st->kb.text;
            size_t n = strlen(v);
            while (n && v[n - 1] == '/') v[--n] = 0;
            free(g_app.thumb_server);
            g_app.thumb_server = n > 8 ? strdup(v) : NULL;
            app_save_prefs();
            app_toast(g_app.thumb_server ? "Seek previews on" : "Seek previews off");
        } else if (strlen(st->kb.text) > 10) {
            app_install_addon(st->kb.text);
        }
    }
    st->edit = EDIT_NONE;
    SDL_StopTextInput();
}

/* Left/Right on a row. Returns 1 when the row took it. */
static int adjust(Settings *st, int id, int dir)
{
    switch (id) {
    case IT_PREF_AUDIO: cycle_lang(g_app.pref_audio, sizeof g_app.pref_audio, dir); break;
    case IT_PREF_SUBS: cycle_lang(g_app.pref_subs, sizeof g_app.pref_subs, dir); break;
    case IT_SUBS_SIZE: g_app.subs_size = (g_app.subs_size + dir + 3) % 3; break;
    case IT_SUBS_STYLE: g_app.subs_style = (g_app.subs_style + dir + SUBS_STYLE_COUNT) % SUBS_STYLE_COUNT; break;
    case IT_FX: g_app.fx_setting = (FxLevel)(((int)g_app.fx_setting + dir + 3) % 3); break;
    case IT_AUTOPLAY: g_app.autoplay_next = !g_app.autoplay_next; break;
    case IT_SKIP: g_app.skip_mode = (g_app.skip_mode + dir + 3) % 3; break;
    case IT_SUBS_WHEN: g_app.subs_always = !g_app.subs_always; break;
    case IT_CLOCK: g_app.clock_mode = (g_app.clock_mode + dir + 3) % 3; break;
    case IT_QUALITY: g_app.quality_mode = (g_app.quality_mode + dir + QUAL_MODE_COUNT) % QUAL_MODE_COUNT; break;
    case IT_RES_CAP: {
        static const int CAPS[] = { 2160, 1080, 720 };
        int k = g_app.res_cap <= 720 ? 2 : g_app.res_cap <= 1080 ? 1 : 0;
        g_app.res_cap = CAPS[(k + dir + 3) % 3];
        break;
    }
    case IT_AUTO_REC: g_app.auto_recommended = !g_app.auto_recommended; break;
    case IT_PREFLIGHT: g_app.preflight = !g_app.preflight; break;
    case IT_SCRUB: g_app.live_scrub = !g_app.live_scrub; break;
    case IT_HOME_STYLE: g_app.home_style = (g_app.home_style + dir + HOME_STYLE_COUNT) % HOME_STYLE_COUNT; break;
    case IT_HIDE_WATCHED: g_app.hide_watched = !g_app.hide_watched; break;
    case IT_TRANSITIONS: g_app.transitions = !g_app.transitions; break;
    case IT_MEMORY: {
        g_app.memory_mode = (g_app.memory_mode + dir + 3) % 3;
        FILE *f = fopen("/proc/meminfo", "r"); long kb = 0;
        if (f) { char line[128]; while (fgets(line, sizeof line, f)) if (sscanf(line, "MemTotal: %ld kB", &kb) == 1) break; fclose(f); }
        g_app.low_memory = g_app.memory_mode == 1 || (g_app.memory_mode == 0 && kb > 0 && kb < 2000 * 1024);
        image_set_budget(g_app.low_memory ? 48u << 20 : 0);
        app_toast(g_app.low_memory ? "Memory saver on: fewer rows and posters kept" : "Normal memory use");
        break;
    }
    case IT_TMDB: return 1;
    case IT_PIN_SETTINGS: case IT_HIDE_ADULT: case IT_PIN: case IT_THUMBS: return 1;   /* OK (with the PIN) changes these */
    case IT_OLED: g_app.oled = !g_app.oled; break;
    case IT_AMBIENT: {
        static const int STEPS[] = { 0, 2, 5, 10, 20 };
        int k = 0;
        for (int i = 0; i < 5; i++) if (STEPS[i] == g_app.ambient_minutes) k = i;
        g_app.ambient_minutes = STEPS[(k + dir + 5) % 5];
        break;
    }
    case IT_BLUR: g_app.spoiler_blur = !g_app.spoiler_blur; break;
    default:
        if (id >= IT_ACCT_BASE) {
            if (dir < 0) { app_forget_account(id - IT_ACCT_BASE); app_toast("Saved sign-in forgotten"); }
            return 1;
        }
        if (id >= IT_ADDON_BASE) {
            int i = id - IT_ADDON_BASE;
            if (app_move_addon(i, dir)) st->focus += dir;
            st->confirm_remove = -1;
            return 1;
        }
        if (id >= IT_ROWS_BASE) {
            app_home_row_move(st->rows[id - IT_ROWS_BASE].key, dir);
            int nf = st->focus + dir;
            if (nf >= 0 && nf < st->n_rows) st->focus = nf;
            return 1;
        }
        return 0;
    }
    app_save_prefs();
    return 1;
}

static void after_pin_clear(int ok, void *user) { (void)user; if (ok) { app_set_parental_pin(NULL); app_toast("PIN removed"); } }
static void after_pin_toggle(int ok, void *user) { if (!ok) return; int *flag = user; *flag = !*flag; app_save_prefs(); }

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
    int ids[MAX_ROWS + 8];
    int n = rows_for(st, st->cat, ids, MAX_ROWS + 8);
    if (st->zone == 0) {
        switch (a) {
        case KEY_UP: if (st->cat > 0) { st->cat--; st->focus = 0; st->scroll = 0; } return 1;
        case KEY_DOWN: if (st->cat < CAT_COUNT - 1) { st->cat++; st->focus = 0; st->scroll = 0; } return 1;
        case KEY_RIGHT: case KEY_OK: if (n) { st->zone = 1; st->focus = 0; } return 1;
        case KEY_BACK: app_pop(); return 1;
        default: return 0;
        }
    }
    if (!n) { st->zone = 0; return 1; }
    int id = ids[st->focus];
    switch (a) {
    case KEY_UP: if (st->focus > 0) st->focus--; st->confirm_remove = -1; return 1;
    case KEY_DOWN: if (st->focus < n - 1) { st->focus++; st->confirm_remove = -1; return 1; } return 0;
    case KEY_LEFT: if (!adjust(st, id, -1)) st->zone = 0; return 1;
    case KEY_RIGHT: adjust(st, id, 1); return 1;
    case KEY_BACK: st->zone = 0; st->confirm_remove = -1; return 1;
    case KEY_OK:
        if (id == IT_ACCOUNT) { if (g_app.auth_key) { app_logout(); app_toast("Signed out"); } else app_push(screen_login_create()); }
        else if (id == IT_SERVER) begin_edit(st, EDIT_SERVER);
        else if (id == IT_INSTALL) begin_edit(st, EDIT_INSTALL);
        else if (id == IT_DIAG) app_push(screen_diagnostics_create(0));
        else if (id == IT_THUMBS) begin_edit(st, EDIT_THUMBS);
        else if (id == IT_TMDB) begin_edit(st, EDIT_TMDB);
        else if (id == IT_PIN) { if (g_app.parental_pin[0]) app_push(screen_pin_create("Remove the parental PIN", 0, after_pin_clear, NULL)); else app_push(screen_pin_create("Parental PIN", 1, NULL, NULL)); }
        else if (id == IT_PIN_SETTINGS) app_push(screen_pin_create("Parental PIN", 0, after_pin_toggle, &g_app.pin_settings));
        else if (id == IT_HIDE_ADULT) app_push(screen_pin_create("Parental PIN", 0, after_pin_toggle, &g_app.hide_adult));
        else if (id >= IT_ACCT_BASE) app_switch_account(id - IT_ACCT_BASE);
        else if (id == IT_SETUP) app_push(screen_welcome_create());
        else if (id >= IT_ADDON_BASE) {
            int idx = id - IT_ADDON_BASE;
            if (st->confirm_remove == idx) { app_remove_addon(idx); st->confirm_remove = -1; }
            else if (g_app.addons[idx].flag_protected) app_toast("Built-in addons cannot be removed");
            else st->confirm_remove = idx;
        }
        else if (id >= IT_ROWS_BASE) app_home_row_toggle(st->rows[id - IT_ROWS_BASE].key);
        else adjust(st, id, 1);
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
    st->rows_seen = -1;
    s->name = "settings"; s->st = st;
    s->update = update; s->draw = draw; s->key = key; s->text = text; s->destroy = destroy;
    return s;
}
