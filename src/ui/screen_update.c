/* Updating the app, once the viewer said Update now (the popup, or Settings > About >
 * Update): the Homebrew Channel downloads, checks and installs the new version, and the
 * system closes the app while it does. An optional countdown comes first (OK skips it).
 * Where the TV refuses the app that installer, the screen shows how to update from the
 * Homebrew Channel itself instead, and remembers so it doesn't try again unasked. */
#include "../app.h"
#include "../log.h"
#include "../store.h"
#include "../platform/platform.h"
#include "../video/video.h"
#include "font.h"
#include "theme.h"
#include "widgets.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    float countdown;
    int started, state;   /* state: video_install_status */
    int by_hand;          /* this TV refused the installer: show the steps */
    char status[160];
    float done_at;
} Upd;

static void start(Upd *u)
{
    u->started = 1;
    app_library_flush(1);   /* everything on flash first: the app is closed mid-install */
    store_save();
    store_flush();
    LOGI("update: installing %s from %s", g_update_version, g_update_url);
    if (video_install_update(g_update_url, g_update_sha256, SNTV_APP_ID) != 0) {   /* refused on the spot: not a failed download */
        u->state = -1;
        if (video_install_status(u->status, sizeof u->status) != -1) snprintf(u->status, sizeof u->status, "This TV has no Homebrew Channel installer to ask");
        u->by_hand = 1;
        store_set_int("updateNoInstaller", 1); store_save();
    } else if (!app_update_installs_here()) { store_set_int("updateNoInstaller", 0); store_save(); }
}

/* Where to go instead: the Homebrew Channel's own Update, a step to a line */
static void draw_steps(const Upd *u, float cx, float y)
{
    static const char *const STEPS[] = {
        "Press Home on the remote and open the Homebrew Channel",
        "Choose Stremio (native), then Update",
        "Open Stremio again: your sign-in, settings and progress stay",
    };
    float x = cx - 430;
    for (int i = 0; i < 3; i++) {
        char n[4]; snprintf(n, sizeof n, "%d", i + 1);
        gfx_circle(x + 22, y + 22, 22, THEME_ACCENT);
        font_draw_center(n, x + 22, y + 22 - font_height(FONT_S | FONT_B) / 2, FONT_S | FONT_B, THEME_TEXT);
        font_draw(STEPS[i], x + 66, y + 22 - font_height(FONT_M) / 2, FONT_M, THEME_TEXT);
        y += 72;
    }
    y += 20;
    font_draw_center("This TV doesn't let apps install updates themselves", cx, y, FONT_XS, THEME_TEXT_MUTED);
    font_draw_center(u->started ? "Back: close" : "Back: close   \xC2\xB7   OK: try installing from here anyway", cx, y + 50, FONT_S | FONT_B, THEME_TEXT_MUTED);
}

static void update(Screen *s, float dt)
{
    Upd *u = s->st;
    app_redraw();
    if (!u->started) {
        if ((u->countdown -= dt) <= 0) start(u);
        return;
    }
    if (u->state != -1) u->state = video_install_status(u->status, sizeof u->status);
    if (u->state == 2 && !u->done_at) { u->done_at = g_app.time; LOGI("update: %s installed", g_update_version); }
    if (u->state == -1 && u->status[0]) { static int logged; if (!logged) { logged = 1; LOGW("update: %s", u->status); } }
}

static void draw(Screen *s)
{
    Upd *u = s->st;
    ui_background(0);
    float cx = SNTV_UI_W / 2.0f, y = 330;
    char title[96], line[300];
    int steps = u->by_hand && u->state != 1 && u->state != 2;
    snprintf(title, sizeof title, steps ? "Version %s is ready" : u->state == 2 ? "Updated to %s" : u->state == -1 ? "The update to %s did not install" : "Updating to %s", g_update_version);
    font_draw_center(title, cx, y - (steps ? 60 : 0), FONT_XL | FONT_B, THEME_TEXT);
    y += 110;
    if (steps) draw_steps(u, cx, y - 60);
    else if (!u->started) {
        snprintf(line, sizeof line, "Starting in %d s. The app closes while it installs; open it again from the Home screen in about half a minute.", (int)u->countdown + 1);
        font_draw_center(line, cx, y, FONT_S, THEME_TEXT_DIM);
        font_draw_center("OK: update now   \xC2\xB7   Back: not now (it asks again next time)", cx, y + 70, FONT_S | FONT_B, THEME_TEXT_MUTED);
    } else if (u->state == 2) {
        font_draw_center("Installed. If the app is still open, close it and open it again.", cx, y, FONT_S, THEME_TEXT_DIM);
        font_draw_center("OK: close the app", cx, y + 70, FONT_S | FONT_B, THEME_TEXT_MUTED);
    } else if (u->state == -1) {
        snprintf(line, sizeof line, "%.200s", u->status);
        font_draw_center(line, cx, y, FONT_S, THEME_DANGER);
        font_draw_center("The Homebrew Channel can install it: Stremio (native) > Update.", cx, y + 70, FONT_S, THEME_TEXT_DIM);
        font_draw_center("Open it from the Home screen   \xC2\xB7   Back: close", cx, y + 130, FONT_S | FONT_B, THEME_TEXT_MUTED);
    } else {
        ui_spinner(cx, y + 10, 26);
        font_draw_center(u->status[0] ? u->status : "Starting\xE2\x80\xA6", cx, y + 70, FONT_M | FONT_B, THEME_TEXT);
        font_draw_center("The app will close by itself when the new version is in place.", cx, y + 130, FONT_S, THEME_TEXT_DIM);
    }
}

static int key(Screen *s, KeyAction a)
{
    Upd *u = s->st;
    if (a == KEY_OK) {
        if (!u->started) start(u);
        else if (u->state == 2) g_app.quit = 1;
        else if (u->state == -1) app_toast("Open the Homebrew Channel from the Home screen to update");   /* webOS won't let an app open another */
        return 1;
    }
    if (a == KEY_BACK && (!u->started || u->state == 2 || u->state == -1)) {
        if (!u->started) LOGI("update: postponed by the viewer");
        app_pop();
        return 1;
    }
    return 1;   /* while it installs, the keys wait */
}

static void destroy(Screen *s)
{
    free(s->st);
    free(s);
}

Screen *screen_update_create(int countdown_s)
{
    Screen *s = calloc(1, sizeof *s);
    Upd *u = calloc(1, sizeof *u);
    const char *e = getenv("SNTV_UPDATE_COUNTDOWN");   /* tests */
    u->countdown = e ? (float)atof(e) : (float)countdown_s;
    if (!app_update_installs_here()) { u->by_hand = 1; u->countdown = 1e9f; }   /* refused before: the steps, until OK asks */
    s->name = "update"; s->st = u;
    s->update = update; s->draw = draw; s->key = key; s->destroy = destroy;
    return s;
}
