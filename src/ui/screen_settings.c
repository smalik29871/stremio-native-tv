/* Settings: categories down the left, their rows on the right. Left/Right change a
 * choice, OK toggles or opens, Back returns to the categories. Home rows can be
 * reordered and hidden; addons can be reordered (synced to the account). */
#include "../app.h"
#include "../breaks.h"
#include "../tmdb.h"
#include "../ai.h"
#include "../http.h"
#include "../memwatch.h"
#include "../phone.h"
#include "../youtube.h"
#include "../torrent.h"
#include "qrcodegen.h"
#include "../suntimes.h"
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
#include "screen_options.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { CAT_ACCOUNT, CAT_PLAYBACK, CAT_BREAKS, CAT_LANG, CAT_SOURCES, CAT_INTERFACE, CAT_ROWS, CAT_ADDONS, CAT_KEYS, CAT_PARENTAL, CAT_ABOUT, CAT_COUNT };
static const char *const CAT_NAMES[CAT_COUNT] = { "Account", "Playback", "Take breaks", "Audio & subtitles", "Sources", "Interface", "Home rows", "Addons", "API keys", "Parental", "About" };
static const char *const CAT_DESC[CAT_COUNT] = {
    "Your Stremio account and the streaming server for torrents.",
    "What happens while a video plays.",
    "Pause for what matters: prayer times computed for your place, a stretch, bedtime, or your own times. A banner warns first; playback continues by itself.",
    "Preferred languages and how subtitles look.",
    "How sources are ranked and checked for this TV. P2P fallback plays a torrent here when TorBox has no cached copy (peers see your IP while it plays) and goes back to TorBox once it has one.",
    "The look of the app.",
    "Which rows Home shows, and in what order. OK shows or hides a row, Left/Right moves it.",
    "Installed addons, in the order they answer. Left/Right moves an addon; the order is kept in your account.",
    "Your own keys for services that need one. Each unlocks a feature; nothing is asked without a key.",
    "A PIN that keeps Settings closed and adult addons out of the catalogs.",
    "Logs, device information and setup." };
static const Icon CAT_ICONS[CAT_COUNT] = { ICON_USER, ICON_PLAY, ICON_CLOCK, ICON_SUBS, ICON_SWAP, ICON_HOME, ICON_LIST, ICON_LIBRARY, ICON_LINK, ICON_LOCK, ICON_INFO };

enum { IT_ACCOUNT, IT_SERVER, IT_AUTOPLAY, IT_SKIP, IT_CLOCK, IT_SCRUB, IT_PREF_AUDIO, IT_PREF_SUBS, IT_SUBS_WHEN, IT_SUBS_SIZE, IT_SUBS_STYLE,
       IT_QUALITY, IT_RES_CAP, IT_PREFLIGHT, IT_AUTO_REC, IT_HOME_STYLE, IT_HIDE_WATCHED, IT_FX, IT_OLED, IT_THEME, IT_ACCENT, IT_CARD_DETAILS, IT_AMBIENT, IT_BLUR,
       IT_INSTALL, IT_DIAG, IT_SETUP, IT_THUMBS, IT_PIN, IT_PIN_SETTINGS, IT_HIDE_ADULT, IT_TMDB, IT_MEMORY, IT_TRANSITIONS, IT_PIP, IT_SUBS_SYNC, IT_SUBS_XL, IT_BR_VIDEO, IT_ADDON_WAIT, IT_SRC_AUDIO, IT_P2P, IT_UPDATE, IT_AUTO_UPDATE, IT_AI_PROVIDER, IT_AI_KEY, IT_AI_BASE, IT_AI_MODEL,
       IT_BR_TEST, IT_BR_ON, IT_BR_SCHED, IT_BR_METHOD, IT_BR_ASR, IT_BR_LOCATION, IT_BR_DETECT, IT_BR_TODAY, IT_BR_LEAD, IT_BR_LEN, IT_BR_SOUND, IT_BR_AUDIO, IT_BR_END,
       IT_BR_HEALTH, IT_BR_HEALTH_LEN, IT_BR_BED, IT_BR_CUSTOM1, IT_BR_CUSTOM2, IT_BR_CUSTOM3, IT_BR_WHERE,
       IT_ROWS_BASE = 1000, IT_ADDON_BASE = 2000, IT_ACCT_BASE = 3000 };
enum { EDIT_NONE, EDIT_SERVER, EDIT_INSTALL, EDIT_THUMBS, EDIT_TMDB, EDIT_LOCATION, EDIT_BR_AUDIO, EDIT_BR_END, EDIT_AI_KEY, EDIT_AI_BASE, EDIT_AI_MODEL };
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
    uint8_t qr[qrcodegen_BUFFER_LEN_MAX]; int qr_ready; char phone_url[96];   /* type on your phone */
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
    case CAT_PLAYBACK: ids[n++] = IT_AUTOPLAY; ids[n++] = IT_SKIP; ids[n++] = IT_PIP; ids[n++] = IT_CLOCK; ids[n++] = IT_SCRUB; ids[n++] = IT_THUMBS; break;
    case CAT_PARENTAL: ids[n++] = IT_PIN; if (g_app.parental_pin[0]) { ids[n++] = IT_PIN_SETTINGS; ids[n++] = IT_HIDE_ADULT; } break;
    case CAT_LANG: ids[n++] = IT_PREF_AUDIO; ids[n++] = IT_PREF_SUBS; ids[n++] = IT_SUBS_WHEN; ids[n++] = IT_SUBS_SYNC; if (ai_enabled()) ids[n++] = IT_SUBS_XL; ids[n++] = IT_SUBS_SIZE; ids[n++] = IT_SUBS_STYLE; break;
    case CAT_SOURCES: ids[n++] = IT_QUALITY; ids[n++] = IT_RES_CAP; ids[n++] = IT_SRC_AUDIO; ids[n++] = IT_PREFLIGHT; ids[n++] = IT_AUTO_REC; ids[n++] = IT_ADDON_WAIT; ids[n++] = IT_P2P; break;
    case CAT_INTERFACE: ids[n++] = IT_THEME; ids[n++] = IT_ACCENT; ids[n++] = IT_HOME_STYLE; ids[n++] = IT_CARD_DETAILS; ids[n++] = IT_HIDE_WATCHED; ids[n++] = IT_FX; ids[n++] = IT_TRANSITIONS; ids[n++] = IT_OLED; ids[n++] = IT_AMBIENT; ids[n++] = IT_BLUR; ids[n++] = IT_MEMORY; break;
    case CAT_KEYS: ids[n++] = IT_TMDB; ids[n++] = IT_AI_PROVIDER;
        if (g_app.ai_provider) { ids[n++] = IT_AI_KEY; if (g_app.ai_provider == AI_CUSTOM) ids[n++] = IT_AI_BASE; ids[n++] = IT_AI_MODEL; }
        break;
    case CAT_BREAKS:
        ids[n++] = IT_BR_TEST; ids[n++] = IT_BR_ON; ids[n++] = IT_BR_SCHED;
        if (g_app.break_sched == BRK_ISLAM) { ids[n++] = IT_BR_METHOD; ids[n++] = IT_BR_ASR; }
        ids[n++] = IT_BR_LOCATION; ids[n++] = IT_BR_DETECT; ids[n++] = IT_BR_TODAY;
        ids[n++] = IT_BR_LEAD; ids[n++] = IT_BR_LEN; ids[n++] = IT_BR_SOUND;
        if (g_app.break_sound == 1) { ids[n++] = IT_BR_AUDIO; ids[n++] = IT_BR_END; ids[n++] = IT_BR_VIDEO; }
        ids[n++] = IT_BR_HEALTH; if (g_app.break_health_min) ids[n++] = IT_BR_HEALTH_LEN;
        ids[n++] = IT_BR_BED; ids[n++] = IT_BR_CUSTOM1; ids[n++] = IT_BR_CUSTOM2; ids[n++] = IT_BR_CUSTOM3; ids[n++] = IT_BR_WHERE;
        break;
    case CAT_ROWS:
        if (st->rows_seen != g_app.home_rows_version + g_app.addons_version * 1000) { st->n_rows = app_home_rows(st->rows, MAX_ROWS); st->rows_seen = g_app.home_rows_version + g_app.addons_version * 1000; }
        for (int i = 0; i < st->n_rows && n < max; i++) ids[n++] = IT_ROWS_BASE + i;
        break;
    case CAT_ADDONS: ids[n++] = IT_INSTALL; for (int i = 0; i < g_app.n_addons && n < max; i++) ids[n++] = IT_ADDON_BASE + i; break;
    case CAT_ABOUT: ids[n++] = IT_UPDATE; ids[n++] = IT_AUTO_UPDATE; ids[n++] = IT_DIAG; ids[n++] = IT_SETUP; break;
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
    if (focused && !g_app.fx_light) gfx_shadow(x, y + 6, w, h, R_PANEL, 24, RGBA(0, 0, 0, 0x80));
    if (focused) ui_focus_under(x, y, w, h, R_PANEL, 1);
    gfx_rrect(x, y, w, h, R_PANEL, focused ? THEME_FOCUS : COLOR_ALPHA(THEME_TEXT, 0x0F));
    Color fg = focused ? THEME_ON_FOCUS : THEME_TEXT, dim = focused ? COLOR_ALPHA(THEME_ON_FOCUS, 0xA8) : THEME_TEXT_MUTED;
    gfx_circle(x + 46, y + h / 2, 24, focused ? COLOR_ALPHA(THEME_BG, 0x14) : COLOR_ALPHA(THEME_TEXT, 0x10));
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

static void finish_edit(Settings *st, int save);
static const char *edit_title(int which);
static const char *edit_hint(int which);
static void update(Screen *s, float dt)
{
    Settings *st = s->st;
    if (st->edit != EDIT_NONE) {   /* text sent from the phone fills the field and saves */
        char got[600];
        if (phone_take(got, sizeof got)) { snprintf(st->kb.text, sizeof st->kb.text, "%.255s", got); app_toast("Received from your phone"); finish_edit(st, 1); app_redraw(); }
    }
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
        if (foc) { ui_focus_under(THEME_MARGIN, y, w, h, theme_pill_r(h), 1); gfx_rrect(THEME_MARGIN, y, w, h, theme_pill_r(h), THEME_FOCUS); }
        else if (act) gfx_rrect(THEME_MARGIN, y, w, h, R_PANEL, COLOR_ALPHA(THEME_TEXT, 0x16));
        Color fg = foc ? THEME_ON_FOCUS : act ? THEME_TEXT : THEME_TEXT_DIM;
        ui_icon(CAT_ICONS[i], THEME_MARGIN + 36, y + h / 2, 26, fg);
        font_draw(CAT_NAMES[i], THEME_MARGIN + 70, y + (h - font_height(FONT_S | FONT_B)) / 2, FONT_S | FONT_B, fg);
        if (i == CAT_ADDONS) { char c[16]; snprintf(c, sizeof c, "%d", g_app.n_addons); font_draw(c, THEME_MARGIN + w - 24 - font_measure(c, FONT_XS | FONT_B), y + (h - font_height(FONT_XS | FONT_B)) / 2, FONT_XS | FONT_B, foc ? COLOR_ALPHA(THEME_BG, 0xA8) : THEME_TEXT_MUTED); }
    }
}

static void draw(Screen *s)
{
    Settings *st = s->st;
    ui_background(0);
    draw_categories(st);

    if (st->edit != EDIT_NONE) {
        const char *title = edit_title(st->edit), *hint = edit_hint(st->edit);
        float x = COL_X;
        font_draw(title, x, 100, FONT_L | FONT_B, THEME_TEXT);
        font_draw(hint, x, 156, FONT_S, THEME_TEXT_DIM);
        gfx_rrect(x, 210, keyboard_width(), 76, R_PANEL, COLOR_ALPHA(THEME_TEXT, 0x18));
        font_draw_fit(st->kb.text[0] ? st->kb.text : " ", x + 32, 210 + (76 - font_height(FONT_M | FONT_B)) / 2, FONT_M | FONT_B, THEME_TEXT, keyboard_width() - 64);
        keyboard_draw(&st->kb, x, 320, 1);
        font_draw("Done saves  \xC2\xB7  Back cancels", x, 320 + keyboard_height() + 30, FONT_S, THEME_TEXT_MUTED);
        if (st->qr_ready) {   /* or type it on the phone */
            float qx = x + keyboard_width() + 70, qy = 320, qs = 250;
            if (qx + qs < SNTV_UI_W - 40) {
                ui_qr(st->qr, qx, qy, qs);
                font_draw("Or type it on your phone", qx, qy + qs + 22, FONT_S | FONT_B, THEME_TEXT);
                font_draw_wrapped("Scan, paste the text, Send. It lands here and saves.", qx, qy + qs + 56, FONT_XS, THEME_TEXT_DIM, 380, 2);
                font_draw(st->phone_url, qx, qy + qs + 112, FONT_XS, THEME_TEXT_MUTED);
            }
        }
        return;
    }

    /* pane header */
    font_draw(CAT_NAMES[st->cat], COL_X, 100, FONT_L | FONT_B, THEME_TEXT);
    font_draw_wrapped(CAT_DESC[st->cat], COL_X, 156, FONT_S, THEME_TEXT_DIM, SNTV_UI_W - THEME_MARGIN - COL_X, 2);
    if (st->cat == CAT_ABOUT) {
        char info[300];
        char mem[160]; mem_describe(mem, sizeof mem);
        snprintf(info, sizeof info, "Version %s  \xC2\xB7  %s  \xC2\xB7  %zu MB artwork in memory  \xC2\xB7  %s", SNTV_APP_VERSION, video_backend_name(), image_cache_bytes() >> 20, mem);
        font_draw_fit(info, COL_X, SNTV_UI_H - 110, FONT_XS, THEME_TEXT_MUTED, SNTV_UI_W - THEME_MARGIN - COL_X);
        char tv[300];
        if (g_update_version[0]) snprintf(tv, sizeof tv, "Version %s is available: Update above installs it", g_update_version);
        else snprintf(tv, sizeof tv, "TV profile: %s", "learned from what played here; Diagnostics shows it");
        font_draw_fit(tv, COL_X, SNTV_UI_H - 80, FONT_XS, THEME_TEXT_MUTED, SNTV_UI_W - THEME_MARGIN - COL_X);
    }

    static const char *FX_NAMES[] = { "Automatic", "Full", "Light" };
    static const char *SIZE_NAMES[] = { "Small", "Medium", "Large", "Extra large" };
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
        case IT_SUBS_SYNC: draw_row(y, ICON_SUBS, "Auto-sync subtitles to the file", NULL, foc, KIND_TOGGLE, g_app.subs_autosync); break;
        case IT_BR_VIDEO: draw_row(y, ICON_PLAY, "Show the video (YouTube and video recordings)", NULL, foc, KIND_TOGGLE, g_app.break_video); break;
        case IT_P2P: draw_row(y, ICON_SWAP, "P2P fallback on this TV", NULL, foc, KIND_TOGGLE, g_app.p2p_fallback); break;
        case IT_SRC_AUDIO: { char la[96]; snprintf(la, sizeof la, "Sources in my audio language first%s%s%s", lang_known(g_app.pref_audio) ? " (" : "", lang_known(g_app.pref_audio) ? lang_display(g_app.pref_audio) : "", lang_known(g_app.pref_audio) ? ")" : ""); draw_row(y, ICON_AUDIO, la, NULL, foc, KIND_TOGGLE, g_app.src_audio_lang); break; }
        case IT_ADDON_WAIT: if (g_app.addon_wait >= 99) snprintf(v, sizeof v, "Every addon"); else snprintf(v, sizeof v, "%d s", g_app.addon_wait); draw_row(y, ICON_CLOCK, "Autoplay waits for slow addons", v, foc, KIND_CHOICE, 0); break;
        case IT_SUBS_XL: draw_row(y, ICON_STAR, "Translate with AI when nobody offers my language", NULL, foc, KIND_TOGGLE, g_app.subs_autotranslate); break;
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
        case IT_THUMBS: draw_row(y, ICON_ASPECT, "Home helper (optional)", g_app.thumb_server ? g_app.thumb_server : "Not needed: the TV makes seek previews and saves YouTube links itself", foc, KIND_ACTION, 0); break;
        case IT_PIN: draw_row(y, ICON_LOCK, g_app.parental_pin[0] ? "Parental PIN" : "Set a parental PIN", g_app.parental_pin[0] ? "Set  \xC2\xB7  OK to remove it (asks for the PIN)" : "Four digits; needed for the switches below", foc, KIND_ACTION, 0); break;
        case IT_PIN_SETTINGS: draw_row(y, ICON_SETTINGS, "Ask for the PIN to open Settings", NULL, foc, KIND_TOGGLE, g_app.pin_settings); break;
        case IT_HIDE_ADULT: draw_row(y, ICON_LIBRARY, "Hide catalogs of addons flagged adult", NULL, foc, KIND_TOGGLE, g_app.hide_adult); break;
        case IT_TMDB: {
            char masked[96] = "Not set  \xC2\xB7  cast photos, age ratings (PG-13, TV-MA)";
            if (tmdb_builtin()) snprintf(masked, sizeof masked, "Built-in key in use  \xC2\xB7  yours replaces it");
            if (g_app.tmdb_key) snprintf(masked, sizeof masked, "%.4s\xE2\x80\xA6%s  \xC2\xB7  cast photos, age ratings on", g_app.tmdb_key, strlen(g_app.tmdb_key) > 4 ? g_app.tmdb_key + strlen(g_app.tmdb_key) - 4 : "");
            draw_row(y, ICON_LINK, "The Movie Database (TMDB)", masked, foc, KIND_ACTION, 0);
            break;
        }
        case IT_AI_PROVIDER: {
            static const char *WHAT = "AI assistant: ask about a title, describe what you feel like watching, translate subtitles";
            draw_row(y, ICON_STAR, g_app.ai_provider ? "AI assistant" : WHAT, g_app.ai_provider ? AI_PROVIDER_NAMES[g_app.ai_provider] : "Off  \xC2\xB7  free keys from Groq, OpenRouter or Google", foc, KIND_CHOICE, 0);
            break;
        }
        case IT_AI_KEY: {
            char masked[160];
            if (g_app.ai_key) snprintf(masked, sizeof masked, "%.4s\xE2\x80\xA6%s  \xC2\xB7  %s", g_app.ai_key, strlen(g_app.ai_key) > 4 ? g_app.ai_key + strlen(g_app.ai_key) - 4 : "", ai_enabled() ? "on" : "not enough to run");
            else snprintf(masked, sizeof masked, "%s", g_app.ai_provider == AI_CUSTOM ? "Not set (fine for Ollama)" : ai_key_hint(g_app.ai_provider));
            draw_row(y, ICON_LINK, "AI API key", masked, foc, KIND_ACTION, 0);
            break;
        }
        case IT_AI_BASE: draw_row(y, ICON_LINK, "AI endpoint", g_app.ai_base ? g_app.ai_base : ai_key_hint(AI_CUSTOM), foc, KIND_ACTION, 0); break;
        case IT_AI_MODEL: {
            char m[160];
            snprintf(m, sizeof m, "%s%s", ai_model()[0] ? ai_model() : "Not set", g_app.ai_model && g_app.ai_model[0] ? "" : "  \xC2\xB7  preset; OK to change");
            draw_row(y, ICON_SETTINGS, "AI model", m, foc, KIND_ACTION, 0);
            break;
        }
        case IT_MEMORY: {
            static const char *MEM[] = { "Automatic", "Low", "Normal" };
            snprintf(v, sizeof v, "%s%s%s%s", MEM[g_app.memory_mode], g_app.memory_mode == 0 ? (g_app.low_memory ? " (Low  \xC2\xB7  " : " (Normal  \xC2\xB7  ") : "", g_app.memory_mode == 0 ? mem_reason_text() : "", g_app.memory_mode == 0 ? ")" : "");
            draw_row(y, ICON_SETTINGS, "Memory use (rows and artwork kept)", v, foc, KIND_CHOICE, 0);
            break;
        }
        case IT_TRANSITIONS: draw_row(y, ICON_STAR, "Animated screen changes", NULL, foc, KIND_TOGGLE, g_app.transitions); break;
        case IT_PIP: draw_row(y, ICON_ASPECT, "Picture-in-picture on Back (keeps playing in a corner)", NULL, foc, KIND_TOGGLE, g_app.pip_enabled); break;
        case IT_BR_TEST: draw_row(y, ICON_PLAY, "Try a break now", "See and hear what a break looks like", foc, KIND_ACTION, 0); break;
        case IT_BR_ON: draw_row(y, ICON_CLOCK, "Take breaks", NULL, foc, KIND_TOGGLE, g_app.breaks_on); break;
        case IT_BR_SCHED: draw_row(y, ICON_CALENDAR, "Schedule", BRK_SCHED_NAMES[g_app.break_sched], foc, KIND_CHOICE, 0); break;
        case IT_BR_METHOD: draw_row(y, ICON_COMPASS, "Calculation method", SUN_METHOD_NAMES[g_app.break_method], foc, KIND_CHOICE, 0); break;
        case IT_BR_ASR: draw_row(y, ICON_COMPASS, "Asr", g_app.break_asr_hanafi ? "Hanafi (later)" : "Standard (Shafi, Maliki, Hanbali)", foc, KIND_CHOICE, 0); break;
        case IT_BR_LOCATION:
            if (breaks_location_known()) snprintf(v, sizeof v, "%.3f, %.3f%s%s", g_app.break_lat, g_app.break_lon, g_app.break_city[0] ? "  \xC2\xB7  " : "", g_app.break_city);
            else snprintf(v, sizeof v, "Not set  \xC2\xB7  needed for sun-based times");
            draw_row(y, ICON_COMPASS, "Location", v, foc, KIND_ACTION, 0);
            break;
        case IT_BR_DETECT: draw_row(y, ICON_LINK, breaks_detecting() ? "Finding your location\xE2\x80\xA6" : "Find my location from the internet", "Uses ip-api.com; usually right to the city", foc, KIND_ACTION, 0); break;
        case IT_BR_TODAY: { char t[300]; breaks_describe_today(t, sizeof t); draw_row(y, ICON_CALENDAR, "Today", t, foc, KIND_ACTION, 0); break; }
        case IT_BR_LEAD: snprintf(v, sizeof v, "%d min before", g_app.break_lead_min); draw_row(y, ICON_CLOCK, "Reminder banner", v, foc, KIND_CHOICE, 0); break;
        case IT_BR_LEN:
            if (g_app.break_len_min < 0) snprintf(v, sizeof v, "%s", g_app.break_sound == 1 ? "Same as the recording" : "Same as the recording (1 min)");
            else if (g_app.break_len_min) snprintf(v, sizeof v, "%d min", g_app.break_len_min);
            else snprintf(v, sizeof v, "Until I press OK");
            draw_row(y, ICON_CLOCK, "Break length", v, foc, KIND_CHOICE, 0);
            break;
        case IT_BR_SOUND: { static const char *S[] = { "Soft chime", "My own recording", "Silent" }; draw_row(y, ICON_AUDIO, "Sound", S[g_app.break_sound], foc, KIND_CHOICE, 0); break; }
        case IT_BR_AUDIO: draw_row(y, ICON_AUDIO, "Recording at the start", !g_app.break_audio ? "Not set  \xC2\xB7  send one from your phone, or a link (adhan, bell, chant)" : strncmp(g_app.break_audio, "sntv-sound://", 13) == 0 ? "Your recording, stored on the TV  \xC2\xB7  OK to replace" : g_app.break_audio, foc, KIND_ACTION, 0); break;
        case IT_BR_END: draw_row(y, ICON_AUDIO, "Recording at the end", !g_app.break_end ? "Not set  \xC2\xB7  played before playback continues (a dua, a closing bell)" : strncmp(g_app.break_end, "sntv-sound://", 13) == 0 ? "Your recording, stored on the TV  \xC2\xB7  OK to replace" : g_app.break_end, foc, KIND_ACTION, 0); break;
        case IT_BR_HEALTH: if (g_app.break_health_min) snprintf(v, sizeof v, "Every %d min of watching", g_app.break_health_min); else snprintf(v, sizeof v, "Off"); draw_row(y, ICON_STAR, "Stretch reminder", v, foc, KIND_CHOICE, 0); break;
        case IT_BR_HEALTH_LEN: snprintf(v, sizeof v, "%d min", g_app.break_health_len); draw_row(y, ICON_STAR, "Stretch length", v, foc, KIND_CHOICE, 0); break;
        case IT_BR_BED: if (g_app.break_bed >= 0) snprintf(v, sizeof v, "%d:%02d", g_app.break_bed / 60, g_app.break_bed % 60); else snprintf(v, sizeof v, "Off"); draw_row(y, ICON_CLOCK, "Bedtime (playback stops)", v, foc, KIND_CHOICE, 0); break;
        case IT_BR_CUSTOM1: case IT_BR_CUSTOM2: case IT_BR_CUSTOM3: {
            int m = g_app.break_custom[id - IT_BR_CUSTOM1]; char t[32];
            snprintf(t, sizeof t, "My time %d", id - IT_BR_CUSTOM1 + 1);
            if (m >= 0) snprintf(v, sizeof v, "%d:%02d", m / 60, m % 60); else snprintf(v, sizeof v, "Off");
            draw_row(y, ICON_CLOCK, t, v, foc, KIND_CHOICE, 0);
            break;
        }
        case IT_BR_WHERE: draw_row(y, ICON_HOME, "Breaks apply", g_app.breaks_everywhere ? "Everywhere in the app" : "Only while a video plays", foc, KIND_CHOICE, 0); break;
        case IT_AUTO_REC: draw_row(y, ICON_PLAY, "Play the recommended source automatically", NULL, foc, KIND_TOGGLE, g_app.auto_recommended); break;
        case IT_THEME: draw_row(y, ICON_STAR, "Theme", THEME_NAMES[g_app.theme], foc, KIND_CHOICE, 0); break;
        case IT_CARD_DETAILS: draw_row(y, ICON_INFO, "Show details when a card stays focused", NULL, foc, KIND_TOGGLE, g_app.card_details); break;
        case IT_ACCENT: {
            draw_row(y, ICON_STAR, "Accent colour", THEME_ACCENT_NAMES[g_app.theme][g_app.theme_accent], foc, KIND_CHOICE, 0);
            /* the six swatches, the current one framed */
            float sx = SNTV_UI_W - THEME_MARGIN - 470, sy = y + 23;
            for (int i = 0; i < THEME_ACCENTS; i++) {
                float bx = sx + i * 34;
                if (i == g_app.theme_accent) gfx_rrect_ring(bx - 3, sy - 3, 30, 30, theme_pill_r(30), 2.5f, foc ? THEME_ON_FOCUS : THEME_TEXT);
                gfx_rrect(bx, sy, 24, 24, theme_pill_r(24), theme_accent_swatch(g_app.theme, i));
            }
            break;
        }
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
        case IT_UPDATE: {
            char v[160];
            if (g_update_version[0]) snprintf(v, sizeof v, "Version %s is ready  \xC2\xB7  OK installs it now", g_update_version);
            else snprintf(v, sizeof v, "%s is the newest we know of  \xC2\xB7  OK checks", SNTV_APP_VERSION);
            draw_row(y, ICON_RELOAD, "Update", v, foc, KIND_ACTION, 0);
            break;
        }
        case IT_AUTO_UPDATE: draw_row(y, ICON_RELOAD, "Update automatically when nothing is playing", NULL, foc, KIND_TOGGLE, g_app.auto_update); break;
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
                if (foc) { ui_icon(ICON_CHEVRON_LEFT, SNTV_UI_W - THEME_MARGIN - 90, y + (ROW_H - 12) / 2, 22, COLOR_ALPHA(THEME_BG, 0xA8)); ui_icon(ICON_CHEVRON_RIGHT, SNTV_UI_W - THEME_MARGIN - 56, y + (ROW_H - 12) / 2, 22, COLOR_ALPHA(THEME_BG, 0xA8)); }
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
        gfx_gradient_v(0, SNTV_UI_H - band, SNTV_UI_W, band, COLOR_ALPHA(THEME_BG, 0), COLOR_ALPHA(THEME_BG, 0xF4));
        gfx_rect(0, SNTV_UI_H - 200, SNTV_UI_W, 200, COLOR_ALPHA(THEME_BG, 0xF4));
        font_draw_center("PREVIEW  \xC2\xB7  applies to subtitles from addons; the TV draws embedded ones itself",
                         SNTV_UI_W / 2, SNTV_UI_H - 190, FONT_XS | FONT_B, THEME_TEXT_MUTED);
        ui_subtitle("Subtitles will look like this.\nTwo lines, the way films do it.", SNTV_UI_H - 40, g_app.subs_size, g_app.subs_style);
    }
}

static const char *edit_title(int which)
{
    return which == EDIT_AI_KEY ? "AI API key" : which == EDIT_AI_BASE ? "AI endpoint (OpenAI-compatible)" : which == EDIT_AI_MODEL ? "AI model"
         : which == EDIT_SERVER ? "Streaming server URL" : which == EDIT_THUMBS ? "Home helper" : which == EDIT_TMDB ? "TMDB API key"
         : which == EDIT_LOCATION ? "Your location" : which == EDIT_BR_AUDIO ? "Audio at the start of a break" : which == EDIT_BR_END ? "Audio at the end of a break" : "Addon manifest URL";
}
static const char *edit_hint(int which)
{
    return which == EDIT_SERVER ? "Any machine on your network running Stremio, e.g. http://192.168.1.10:11470"
         : which == EDIT_THUMBS ? "Optional: tools/thumbs/server.py on a computer or NAS, e.g. http://192.168.1.10:11480. The TV already decodes seek previews and saves YouTube links; set this only to use the computer instead. Empty turns it off."
         : which == EDIT_TMDB ? "Free at themoviedb.org > Settings > API (the v3 key, 32 characters). Empty removes it."
         : which == EDIT_AI_KEY ? ai_key_hint(g_app.ai_provider)
         : which == EDIT_AI_BASE ? "The base URL that ends before /chat/completions, e.g. http://192.168.1.10:11434/v1 for Ollama."
         : which == EDIT_AI_MODEL ? "A model name the provider accepts. Empty goes back to the preset."
         : which == EDIT_LOCATION ? "Latitude, longitude  (e.g. 51.5074, -0.1278). Maps apps show them when you long-press a spot."
         : which == EDIT_BR_AUDIO ? "Scan the code to send an audio or video file from your phone, or paste a link: a direct MP3/M4A/MP4, or a YouTube link (the TV saves it once and plays it offline). Empty for none"
         : which == EDIT_BR_END ? "Played when the break ends, before playback continues (a dua, a closing bell). Send a file from your phone or paste a direct link. Empty for none"
         : "Paste or type the addon's manifest.json address";
}

static void phone_qr_make(Settings *st)
{
    st->qr_ready = 0;
    char url[96];
    if (!phone_url(url, sizeof url)) return;
    uint8_t tmp[qrcodegen_BUFFER_LEN_MAX];
    st->qr_ready = qrcodegen_encodeText(url, tmp, st->qr, qrcodegen_Ecc_MEDIUM, qrcodegen_VERSION_MIN, 6, qrcodegen_Mask_AUTO, true);
    snprintf(st->phone_url, sizeof st->phone_url, "%s", url);
}

/* A page link is fetched by the helper once, now, so the break starts instantly. */
static void on_warm(HttpResponse *r, void *user)
{
    (void)user;
    if (r->status == 200 || r->status == 206) app_toast("Ready: it plays at each break");
    else app_toast("The helper could not get it: %.120s", r->status && r->body && r->body[0] ? r->body : r->error);
}
static void warm_media(const char *stored)
{
    char url[1800];
    const char *u = breaks_media_url(stored, url, sizeof url, NULL);
    if (!u) return;
    HttpOptions o = { .range_bytes = 1, .timeout_s = 320, .no_json_accept = 1 };
    http_request_async_ex(u, &o, on_warm, NULL);
    app_toast("The helper is fetching it\xE2\x80\xA6");
}

/* A YouTube link is downloaded by the TV itself; the home helper, when set, is the fallback. */
static void on_youtube_saved(const char *name, const char *error, void *user)
{
    char *url = user;
    if (name) app_toast("Ready: saved on the TV, it plays at each break (offline too)");
    else if (g_app.thumb_server && *g_app.thumb_server) { app_toast("YouTube said no to the TV (%.80s). Trying the home helper\xE2\x80\xA6", error); warm_media(url); }
    else if (strstr(error, "not a bot")) app_toast("YouTube wants a sign-in from this TV right now. Send the file from your phone (scan the code), or try again later");
    else app_toast("Could not save it from YouTube: %.120s", error);
    free(url);
}

static void save_youtube(const char *url)
{
    app_toast("Saving it from YouTube on the TV\xE2\x80\xA6 the first link takes up to a minute");
    youtube_save(url, g_app.break_video, on_youtube_saved, strdup(url));
}

static void begin_edit(Settings *st, int which);

/* AI model: a list of what the key can use, with typing a name as the last choice. */
typedef struct { char ids[8][96]; int n; } ModelChoice;

static void on_model_choice(int c, void *user)
{
    ModelChoice *mc = user;
    Screen *top = app_top();
    if (c < mc->n) { free(g_app.ai_model); g_app.ai_model = strdup(mc->ids[c]); app_save_prefs(); app_toast("AI model: %s", mc->ids[c]); }
    else if (top && strcmp(top->name, "settings") == 0) begin_edit(top->st, EDIT_AI_MODEL);
    free(mc);
}

static void on_models(const char *const *ids, int n, const char *error, void *user)
{
    (void)user;
    Screen *top = app_top();
    if (!top || strcmp(top->name, "settings") != 0) return;
    if (!ids) { app_toast("%s", error ? error : "No models"); begin_edit(top->st, EDIT_AI_MODEL); return; }
    ModelChoice *mc = calloc(1, sizeof *mc);
    const char *items[8];
    for (int i = 0; i < n && mc->n < 7; i++) { snprintf(mc->ids[mc->n], 96, "%s", ids[i]); items[mc->n] = mc->ids[mc->n]; mc->n++; }
    items[mc->n] = "Type a model name\xE2\x80\xA6";
    app_push(screen_options_create("AI model", "The models your key can use, best first", items, mc->n + 1, on_model_choice, mc));
}

static void begin_edit(Settings *st, int which)
{
    st->edit = which;
    char loc[64] = "";
    if (breaks_location_known()) snprintf(loc, sizeof loc, "%.4f, %.4f", g_app.break_lat, g_app.break_lon);
    keyboard_init(&st->kb, 0, which == EDIT_SERVER ? g_app.streaming_server : which == EDIT_THUMBS ? (g_app.thumb_server ? g_app.thumb_server : "http://")
                  : which == EDIT_TMDB ? (g_app.tmdb_key ? g_app.tmdb_key : "")
                  : which == EDIT_AI_KEY ? (g_app.ai_key ? g_app.ai_key : "") : which == EDIT_AI_BASE ? (g_app.ai_base ? g_app.ai_base : "http://")
                  : which == EDIT_AI_MODEL ? (g_app.ai_model ? g_app.ai_model : "") : which == EDIT_LOCATION ? loc
                  : which == EDIT_BR_AUDIO ? (g_app.break_audio && strncmp(g_app.break_audio, "sntv-sound://", 13) ? g_app.break_audio : "https://") : which == EDIT_BR_END ? (g_app.break_end && strncmp(g_app.break_end, "sntv-sound://", 13) ? g_app.break_end : "https://") : "https://");
    input_text(1);
    phone_prompt(edit_title(which), edit_hint(which), st->kb.text);
    if (which == EDIT_BR_AUDIO || which == EDIT_BR_END) phone_accept_file(which == EDIT_BR_AUDIO ? "start" : "end");
    phone_qr_make(st);
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
        } else if (st->edit == EDIT_AI_KEY || st->edit == EDIT_AI_BASE || st->edit == EDIT_AI_MODEL) {
            char *v = st->kb.text;
            while (*v == ' ') v++;
            size_t n = strlen(v);
            while (n && (v[n - 1] == ' ' || (st->edit == EDIT_AI_BASE && v[n - 1] == '/'))) v[--n] = 0;
            char **dst = st->edit == EDIT_AI_KEY ? &g_app.ai_key : st->edit == EDIT_AI_BASE ? &g_app.ai_base : &g_app.ai_model;
            free(*dst);
            *dst = n && !(st->edit == EDIT_AI_BASE && n < 8) ? strdup(v) : NULL;
            app_save_prefs();
            app_toast(ai_enabled() ? "AI assistant ready" : st->edit == EDIT_AI_KEY && !n ? "AI key removed" : "Saved  \xC2\xB7  the assistant still needs its key or endpoint");
        } else if (st->edit == EDIT_LOCATION) {
            double la, lo;
            if (sscanf(st->kb.text, " %lf , %lf", &la, &lo) == 2 && la >= -90 && la <= 90 && lo >= -180 && lo <= 180) { breaks_set_location(la, lo, ""); app_toast("Location saved"); }
            else if (!st->kb.text[0]) { breaks_set_location(0, 0, ""); app_toast("Location cleared"); }
            else app_toast("Please enter latitude, longitude");
        } else if (st->edit == EDIT_BR_AUDIO || st->edit == EDIT_BR_END) {
            char **slot = st->edit == EDIT_BR_AUDIO ? &g_app.break_audio : &g_app.break_end;
            char *v = st->kb.text; while (*v == ' ') v++;
            int is_file = strncmp(v, "sntv-sound://", 13) == 0;
            int is_page = breaks_is_page_link(v);
            free(*slot);
            *slot = strlen(v) > 10 && strcmp(v, "https://") ? strdup(v) : NULL;
            app_save_prefs();
            if (g_app.break_sound != 1 && *slot) g_app.break_sound = 1;
            app_tidy(0);   /* the recording it replaced goes */
            char yt[16];
            if (*slot && youtube_video_id(*slot, yt)) save_youtube(*slot);
            else if (*slot && is_page && !(g_app.thumb_server && *g_app.thumb_server))
                app_toast("Saved. A page link like YouTube needs the home helper: Settings > Playback > Home helper");
            else if (*slot && is_page) warm_media(*slot);
            else app_toast(is_file ? "Recording saved on the TV" : *slot ? "Link saved" : "No recording set");
        } else if (st->edit == EDIT_THUMBS) {
            char *v = st->kb.text;
            size_t n = strlen(v);
            while (n && v[n - 1] == '/') v[--n] = 0;
            free(g_app.thumb_server);
            g_app.thumb_server = n > 8 ? strdup(v) : NULL;
            app_save_prefs();
            app_toast(g_app.thumb_server ? "Home helper set: previews come from it" : "Home helper off: the TV makes previews itself");
        } else if (strlen(st->kb.text) > 10) {
            app_install_addon(st->kb.text);
        }
    }
    st->edit = EDIT_NONE;
    input_text(0);
    phone_prompt(NULL, NULL, NULL);
}

/* Left/Right on a row. Returns 1 when the row took it. */
static int adjust(Settings *st, int id, int dir)
{
    switch (id) {
    case IT_PREF_AUDIO: cycle_lang(g_app.pref_audio, sizeof g_app.pref_audio, dir); break;
    case IT_PREF_SUBS: cycle_lang(g_app.pref_subs, sizeof g_app.pref_subs, dir); break;
    case IT_SUBS_SIZE: g_app.subs_size = (g_app.subs_size + dir + 4) % 4; break;
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
    case IT_THEME:
        g_app.theme = (g_app.theme + dir + THEME_COUNT) % THEME_COUNT;
        g_app.theme_accent = 0;
        theme_apply(g_app.theme, g_app.theme_accent);
        theme_load_fonts(g_app.theme);
        break;
    case IT_CARD_DETAILS: g_app.card_details = !g_app.card_details; break;
    case IT_ACCENT:
        g_app.theme_accent = (g_app.theme_accent + dir + THEME_ACCENTS) % THEME_ACCENTS;
        theme_apply(g_app.theme, g_app.theme_accent);
        break;
    case IT_HOME_STYLE: g_app.home_style = (g_app.home_style + dir + HOME_STYLE_COUNT) % HOME_STYLE_COUNT; break;
    case IT_HIDE_WATCHED: g_app.hide_watched = !g_app.hide_watched; break;
    case IT_TRANSITIONS: g_app.transitions = !g_app.transitions; break;
    case IT_PIP: g_app.pip_enabled = !g_app.pip_enabled; break;
    case IT_SUBS_SYNC: g_app.subs_autosync = !g_app.subs_autosync; break;
    case IT_SUBS_XL: g_app.subs_autotranslate = !g_app.subs_autotranslate; break;
    case IT_BR_VIDEO:
        g_app.break_video = !g_app.break_video;
        for (int i = 0; i < 2; i++) {   /* a YouTube recording saved as sound only: fetch the video too */
            const char *u = i ? g_app.break_end : g_app.break_audio;
            char name[80]; int vid = 0;
            if (g_app.break_video && youtube_saved(u, 1, name, sizeof name, &vid) && !vid) youtube_save(u, 1, NULL, NULL);
        }
        break;
    case IT_SRC_AUDIO: g_app.src_audio_lang = !g_app.src_audio_lang; break;
    case IT_P2P: g_app.p2p_fallback = !g_app.p2p_fallback; if (!g_app.p2p_fallback) torrent_engine_stop(); break;
    case IT_AUTO_UPDATE: g_app.auto_update = !g_app.auto_update; break;
    case IT_ADDON_WAIT: { static const int L[] = { 3, 5, 8, 15, 30, 99 }; int k = 2; for (int i = 0; i < 6; i++) if (L[i] == g_app.addon_wait) k = i; g_app.addon_wait = L[(k + dir + 6) % 6]; break; }
    case IT_AI_PROVIDER: g_app.ai_provider = (g_app.ai_provider + dir + AI_PROVIDER_COUNT) % AI_PROVIDER_COUNT; break;
    case IT_MEMORY: {
        g_app.memory_mode = (g_app.memory_mode + dir + 3) % 3;
        mem_apply_mode();
        app_toast(g_app.low_memory ? "Memory saver on: fewer rows and posters kept" : g_app.memory_mode == 0 ? "Automatic: the saver turns on by itself when memory gets tight" : "Normal memory use");
        break;
    }
    case IT_TMDB: case IT_AI_KEY: case IT_AI_BASE: case IT_AI_MODEL: return 1;
    case IT_BR_ON: g_app.breaks_on = !g_app.breaks_on; break;
    case IT_BR_SCHED: g_app.break_sched = (g_app.break_sched + dir + BRK_SCHED_COUNT) % BRK_SCHED_COUNT; break;
    case IT_BR_METHOD: g_app.break_method = (g_app.break_method + dir + SUN_METHOD_COUNT) % SUN_METHOD_COUNT; break;
    case IT_BR_ASR: g_app.break_asr_hanafi = !g_app.break_asr_hanafi; break;
    case IT_BR_LEAD: { static const int L[] = { 1, 2, 3, 5, 10, 15 }; int k = 3; for (int i = 0; i < 6; i++) if (L[i] == g_app.break_lead_min) k = i; g_app.break_lead_min = L[(k + dir + 6) % 6]; break; }
    case IT_BR_LEN: { static const int L[] = { -1, 0, 3, 5, 10, 15, 20, 30 }; int k = 4; for (int i = 0; i < 8; i++) if (L[i] == g_app.break_len_min) k = i; g_app.break_len_min = L[(k + dir + 8) % 8]; break; }
    case IT_BR_SOUND: g_app.break_sound = (g_app.break_sound + dir + 3) % 3; break;
    case IT_BR_HEALTH: { static const int L[] = { 0, 30, 45, 60, 90, 120 }; int k = 0; for (int i = 0; i < 6; i++) if (L[i] == g_app.break_health_min) k = i; g_app.break_health_min = L[(k + dir + 6) % 6]; break; }
    case IT_BR_HEALTH_LEN: { static const int L[] = { 2, 3, 5, 10 }; int k = 1; for (int i = 0; i < 4; i++) if (L[i] == g_app.break_health_len) k = i; g_app.break_health_len = L[(k + dir + 4) % 4]; break; }
    case IT_BR_BED: {   /* Off, 20:00 ... 23:30, 0:00, 0:30, 1:00 */
        static const int L[] = { -1, 1200, 1230, 1260, 1290, 1320, 1350, 1380, 1410, 0, 30, 60 };
        int k = 0; for (int i = 0; i < 12; i++) if (L[i] == g_app.break_bed) k = i;
        g_app.break_bed = L[(k + dir + 12) % 12];
        break;
    }
    case IT_BR_CUSTOM1: case IT_BR_CUSTOM2: case IT_BR_CUSTOM3: {
        int *m = &g_app.break_custom[id - IT_BR_CUSTOM1];
        *m = *m < 0 ? (dir > 0 ? 0 : 23 * 60 + 45) : *m + dir * 15;
        if (*m >= 24 * 60 || *m < 0) *m = -1;
        break;
    }
    case IT_BR_WHERE: g_app.breaks_everywhere = !g_app.breaks_everywhere; break;
    case IT_BR_TEST: case IT_BR_LOCATION: case IT_BR_DETECT: case IT_BR_TODAY: case IT_BR_AUDIO: case IT_BR_END: return 1;
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
    breaks_settings_changed();
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
        else if (id == IT_UPDATE) {
            if (g_update_version[0] && g_update_url[0] && g_update_sha256[0]) app_push(screen_update_create(0));
            else { app_toast("Checking for updates\xE2\x80\xA6"); app_update_check_now(); }
        }
        else if (id == IT_THUMBS) begin_edit(st, EDIT_THUMBS);
        else if (id == IT_TMDB) begin_edit(st, EDIT_TMDB);
        else if (id == IT_AI_KEY) begin_edit(st, EDIT_AI_KEY);
        else if (id == IT_AI_BASE) begin_edit(st, EDIT_AI_BASE);
        else if (id == IT_AI_MODEL) { app_toast("Asking %s for its models\xE2\x80\xA6", AI_PROVIDER_NAMES[g_app.ai_provider]); ai_list_models(on_models, NULL); }
        else if (id == IT_BR_TEST) breaks_fire_now(g_app.break_sched == BRK_ISLAM ? "Maghrib" : "Break", "This is how a break looks; OK continues", g_app.break_sched == BRK_ISLAM ? BRK_KIND_PRAYER : BRK_KIND_TEST);
        else if (id == IT_BR_LOCATION) begin_edit(st, EDIT_LOCATION);
        else if (id == IT_BR_DETECT) breaks_detect_location();
        else if (id == IT_BR_AUDIO) begin_edit(st, EDIT_BR_AUDIO);
        else if (id == IT_BR_END) begin_edit(st, EDIT_BR_END);
        else if (id == IT_BR_TODAY) { char t[300]; breaks_describe_today(t, sizeof t); app_toast("%s", t); }
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
    if (st->edit != EDIT_NONE) input_text(0);
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
