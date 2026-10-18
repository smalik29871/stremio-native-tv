/* First-run setup: a few one-question steps (languages, when subtitles turn on,
 * intro skipping, autoplay, background, account) so nobody has to find these in
 * Settings. Shown once; Settings > Run quick setup again brings it back. */
#include "../app.h"
#include "../lang.h"
#include "../log.h"
#include "../store.h"
#include "font.h"
#include "theme.h"
#include "widgets.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { ST_AUDIO, ST_SUBS, ST_SUBS_WHEN, ST_SKIP, ST_NEXT, ST_LOOK, ST_ACCOUNT, ST_COUNT };
#define LIST_X 900.0f
#define LIST_Y 230.0f
#define ROW 84.0f
#define VISIBLE 9

typedef struct {
    int step, focus;
    float scroll;
    int sign_in;
} Welcome;

static int option_count(int step)
{
    switch (step) {
    case ST_AUDIO: case ST_SUBS: return lang_count();
    case ST_SKIP: return 3;
    default: return 2;
    }
}

static const char *option_label(int step, int i, const char **hint)
{
    *hint = NULL;
    switch (step) {
    case ST_AUDIO: if (!i) { *hint = "Use the stream's default track"; return "No preference"; } return LANG_NAMES[i];
    case ST_SUBS: return i ? LANG_NAMES[i] : "No subtitles";
    case ST_SUBS_WHEN:
        if (!i) { *hint = "Foreign films and anime get subtitles; your own language plays clean"; return "Only when the audio is in another language"; }
        return "Always";
    case ST_SKIP:
        if (i == 0) { *hint = "Press OK while it is on screen"; return "Show a Skip button"; }
        if (i == 1) return "Skip automatically";
        return "Don't skip";
    case ST_NEXT:
        if (!i) { *hint = "Starts 20 seconds into the credits"; return "Play the next episode automatically"; }
        return "Let me choose";
    case ST_LOOK:
        if (!i) return "Purple glow";
        *hint = "True black for OLED TVs";
        return "Pure black";
    default:
        if (!i) { *hint = "Scan a code with your phone; syncs your library and addons"; return "Sign in with a QR code"; }
        return "Not now";
    }
}

static int current_choice(int step)
{
    switch (step) {
    case ST_AUDIO: return lang_index(g_app.pref_audio);
    case ST_SUBS: return lang_index(g_app.pref_subs);
    case ST_SUBS_WHEN: return g_app.subs_always ? 1 : 0;
    case ST_SKIP: return g_app.skip_mode;
    case ST_NEXT: return g_app.autoplay_next ? 0 : 1;
    case ST_LOOK: return g_app.oled ? 1 : 0;
    default: return 0;
    }
}

static int step_applies(int step)
{
    if (step == ST_SUBS_WHEN) return g_app.pref_subs[0] != 0;
    if (step == ST_ACCOUNT) return g_app.auth_key == NULL;
    return 1;
}

static void enter_step(Welcome *w, int step)
{
    w->step = step;
    w->focus = current_choice(step);
    int first = w->focus - VISIBLE / 2;
    w->scroll = (first < 0 ? 0 : first) * ROW;
}

static void finish(Welcome *w)
{
    store_set_int("setupDone", 1);
    app_save_prefs();
    LOGI("setup: audio=%s subs=%s always=%d skip=%d autoplay=%d oled=%d", g_app.pref_audio, g_app.pref_subs,
         g_app.subs_always, g_app.skip_mode, g_app.autoplay_next, g_app.oled);
    int sign_in = w->sign_in;
    app_pop();   /* destroys w */
    if (sign_in) app_push(screen_login_create());
    else app_toast("All set. Change any of this in Settings.");
}

static void choose(Welcome *w)
{
    int i = w->focus;
    switch (w->step) {
    case ST_AUDIO: snprintf(g_app.pref_audio, sizeof g_app.pref_audio, "%s", LANG_CODES[i]); break;
    case ST_SUBS: snprintf(g_app.pref_subs, sizeof g_app.pref_subs, "%s", LANG_CODES[i]); break;
    case ST_SUBS_WHEN: g_app.subs_always = i == 1; break;
    case ST_SKIP: g_app.skip_mode = i; break;
    case ST_NEXT: g_app.autoplay_next = i == 0; break;
    case ST_LOOK: g_app.oled = i == 1; break;
    case ST_ACCOUNT: w->sign_in = i == 0; break;
    }
    int next = w->step + 1;
    while (next < ST_COUNT && !step_applies(next)) next++;
    if (next >= ST_COUNT) { finish(w); return; }
    enter_step(w, next);
}

static void update(Screen *s, float dt)
{
    Welcome *w = s->st;
    int first = w->focus - VISIBLE / 2, n = option_count(w->step);
    if (first > n - VISIBLE) first = n - VISIBLE;
    if (first < 0) first = 0;
    w->scroll = ui_ease(w->scroll, first * ROW, NULL, dt, 14);
}

static void draw(Screen *s)
{
    Welcome *w = s->st;
    static const char *TITLES[ST_COUNT] = {
        "Which audio language do you prefer?", "Subtitles in which language?", "When should subtitles turn on?",
        "Intros and recaps", "When an episode ends", "Pick a background", "Sign in to Stremio?" };
    static const char *BODY[ST_COUNT] = {
        "Picked automatically when a stream has several audio tracks. Anything you choose in the player is remembered per series.",
        "From the file itself or from your subtitle addons, whichever has your language.",
        "Watching something already in your subtitle language? Subtitles can stay off for it.",
        "Timings come from TheIntroDB and AniSkip, free community databases.",
        "The Up Next card appears when the credits start.",
        "Pure black saves power and looks deeper on OLED panels.",
        "Optional. Without an account you can still browse and play with the built-in addons." };
    ui_background(0);
    float x = THEME_MARGIN + 20;
    ui_brand(x, 110, 46);
    /* step dots */
    /* the subtitle-timing step counts until "No subtitles" has actually been picked */
    int total = 0, here = 0;
    for (int i = 0; i < ST_COUNT; i++) {
        int counts = i == w->step || step_applies(i) || (i == ST_SUBS_WHEN && w->step <= ST_SUBS);
        if (!counts) continue;
        if (i == w->step) here = total;
        total++;
    }
    for (int i = 0; i < total; i++)
        gfx_rrect(x + i * 34, 300, i == here ? 26 : 12, 12, 6, i == here ? THEME_ACCENT2 : RGBA(0xFF, 0xFF, 0xFF, 0x30));
    char counter[32];
    snprintf(counter, sizeof counter, "Step %d of %d", here + 1, total);
    font_draw(counter, x, 340, FONT_XS | FONT_B, THEME_TEXT_MUTED);
    font_draw_wrapped(TITLES[w->step], x, 390, FONT_XL | FONT_B, THEME_TEXT, LIST_X - x - 80, 3);
    font_draw_wrapped(BODY[w->step], x, 600, FONT_S, THEME_TEXT_DIM, LIST_X - x - 100, 4);
    font_draw(here == 0 ? "Back: skip setup" : "Back: previous step", x, SNTV_UI_H - 110, FONT_XS, THEME_TEXT_MUTED);

    int n = option_count(w->step), cur = current_choice(w->step);
    float lw = SNTV_UI_W - THEME_MARGIN - LIST_X;
    gfx_clip_push(LIST_X - 30, LIST_Y - 20, lw + 60, VISIBLE * ROW + 30);
    for (int i = 0; i < n; i++) {
        float y = LIST_Y + i * ROW - w->scroll;
        if (y + ROW < LIST_Y - 20 || y > LIST_Y + VISIBLE * ROW) continue;
        const char *hint, *label = option_label(w->step, i, &hint);
        int foc = i == w->focus;
        float h = ROW - 12;
        if (foc && !g_app.fx_light) gfx_shadow(LIST_X, y + 6, lw, h, 22, 24, RGBA(0, 0, 0, 0x80));
        gfx_rrect(LIST_X, y, lw, h, 22, foc ? THEME_TEXT : RGBA(0xFF, 0xFF, 0xFF, 0x10));
        Color fg = foc ? THEME_ON_LIGHT : THEME_TEXT;
        float ty = hint ? y + 9 : y + (h - font_height(FONT_S | FONT_B)) / 2;
        font_draw_fit(label, LIST_X + 32, ty, FONT_S | FONT_B, fg, lw - 130);
        if (hint) font_draw_fit(hint, LIST_X + 32, y + 41, FONT_XS, foc ? RGBA(0x0B, 0x0A, 0x16, 0xA8) : THEME_TEXT_MUTED, lw - 130);
        if (i == cur) {
            gfx_circle(LIST_X + lw - 46, y + h / 2, 17, foc ? THEME_ON_LIGHT : THEME_ACCENT);
            ui_icon(ICON_CHECK, LIST_X + lw - 46, y + h / 2, 20, THEME_TEXT);
        }
    }
    gfx_clip_pop();
}

static int key(Screen *s, KeyAction a)
{
    Welcome *w = s->st;
    int n = option_count(w->step);
    app_redraw();
    switch (a) {
    case KEY_UP: if (w->focus > 0) w->focus--; return 1;
    case KEY_DOWN: if (w->focus < n - 1) w->focus++; return 1;
    case KEY_OK: choose(w); return 1;
    case KEY_BACK: {
        int prev = w->step - 1;
        while (prev >= 0 && !step_applies(prev)) prev--;
        if (prev < 0) {
            store_set_int("setupDone", 1);
            store_save();
            app_pop();
            app_toast("Setup skipped. Run it any time from Settings.");
            return 1;
        }
        enter_step(w, prev);
        return 1;
    }
    default: return 1;   /* modal: nothing behind reacts */
    }
}

static void destroy(Screen *s)
{
    free(s->st);
    free(s);
}

Screen *screen_welcome_create(void)
{
    Screen *s = calloc(1, sizeof *s);
    Welcome *w = calloc(1, sizeof *w);
    enter_step(w, ST_AUDIO);
    s->name = "welcome"; s->st = w;
    s->update = update; s->draw = draw; s->key = key; s->destroy = destroy;
    return s;
}
