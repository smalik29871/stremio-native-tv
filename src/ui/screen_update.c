/* Updating the app, once the viewer said Update now (the popup, or Settings > About >
 * Update): the Homebrew Channel downloads, checks and installs the new version (where there
 * is none, as in Developer Mode, the app downloads and checks it for the TV's own installer),
 * and the system closes the app while it does. An optional countdown comes first (OK skips it).
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
#ifdef SNTV_PLATFORM_ANDROID
    if (video_install_update(g_update_url, g_update_sha256, SNTV_APP_ID) != 0) { u->state = -1; snprintf(u->status, sizeof u->status, "The update could not start here"); }
    return;
#endif
    if (video_install_update(g_update_url, g_update_sha256, SNTV_APP_ID) != 0) {   /* refused on the spot: not a failed download */
        u->state = -1;
        if (video_install_status(u->status, sizeof u->status) != -1) snprintf(u->status, sizeof u->status, "This TV has no Homebrew Channel installer to ask");
        u->by_hand = 1;
        store_set_int("updateNoInstaller", 1); store_save();
    } else if (!app_update_installs_here()) { store_set_int("updateNoInstaller", 0); store_save(); }
}

/* A line centred on the screen; upright on a phone, wrapped to its width (later lines move down by what it took). */
static float g_extra;
static void center_line(const char *t, float cx, float y, int size, Color c)
{
    if (!SNTV_PHONE) { font_draw_center(t, cx, y, size, c); return; }
    float h = ui_center_wrapped(t, cx, y + g_extra, size, c, SNTV_UI_W - 80);
    g_extra += h - font_height(size);
}

/* What OK and Back do: by a remote, a line naming the keys; by a finger or a mouse, two buttons (1 OK, 2 Back). */
static void choices(const char *ok, const char *back, float cx, float y)
{
    if (!ui_pointer()) {
        char t[256];
        if (back) snprintf(t, sizeof t, "OK: %s   \xC2\xB7   Back: %s", ok, back); else snprintf(t, sizeof t, "OK: %s", ok);
        center_line(t, cx, y, FONT_S | FONT_B, THEME_TEXT_MUTED);
        return;
    }
    char a[64], c[64];
    snprintf(a, sizeof a, "%c%s", ok[0] >= 'a' && ok[0] <= 'z' ? ok[0] - 32 : ok[0], ok + 1);
    if (back) snprintf(c, sizeof c, "%c%s", back[0] >= 'a' && back[0] <= 'z' ? back[0] - 32 : back[0], back + 1);
    float h = 60, w1 = ui_pill_width(a, 0, h), w2 = back ? ui_pill_width(c, 0, h) : 0, gap = back ? 20 : 0;
    float x = cx - (w1 + gap + w2) / 2, py = y + g_extra - 10;
    ui_pill(a, 0, ICON_PLAY, x, py, h, PILL_PRIMARY, 1);
    ui_hit(x, py, w1, h, 1);
    if (back) { ui_pill(c, 0, ICON_BACK, x + w1 + gap, py, h, PILL_SECONDARY, 0); ui_hit(x + w1 + gap, py, w2, h, 2); }
}

/* Where to go instead: the Homebrew Channel's own Update, a step to a line */
static void draw_steps(const Upd *u, float cx, float y)
{
    static const char *const STEPS[] = {
        "Press Home on the remote and open the Homebrew Channel",
        "Choose Cuelight, then Update",
        "Open Cuelight again: your sign-in, settings and progress stay",
    };
    float x = SNTV_PHONE ? 40 : cx - 430;
    for (int i = 0; i < 3; i++) {
        char n[4]; snprintf(n, sizeof n, "%d", i + 1);
        gfx_circle(x + 22, y + 22, 22, THEME_ACCENT);
        font_draw_center(n, x + 22, y + 22 - font_height(FONT_S | FONT_B) / 2, FONT_S | FONT_B, THEME_TEXT);
        font_draw_fit(STEPS[i], x + 66, y + 22 - font_height(FONT_M) / 2, FONT_M, THEME_TEXT, SNTV_UI_W - x - 106);
        y += 72;
    }
    y += 20;
    center_line("This TV doesn't let apps install updates themselves", cx, y, FONT_XS, THEME_TEXT_MUTED);
    center_line(u->started ? "Back: close" : "Back: close   \xC2\xB7   OK: try installing from here anyway", cx, y + 50, FONT_S | FONT_B, THEME_TEXT_MUTED);
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
    g_extra = 0;
    ui_background(0);
    float cx = SNTV_UI_W / 2.0f, y = 330;
    char title[96], line[300];
    int steps = u->by_hand && u->state != 1 && u->state != 2;
#ifdef SNTV_PLATFORM_ANDROID
    snprintf(title, sizeof title, u->state == 2 ? "Version %s is downloaded" : u->state == -1 ? "The update to %s did not install" : "Updating to %s", g_update_version);
#else
    snprintf(title, sizeof title, steps ? "Version %s is ready" : u->state == 2 ? "Updated to %s" : u->state == -1 ? "The update to %s did not install" : "Updating to %s", g_update_version);
#endif
    center_line(title, cx, y - (steps ? 60 : 0), FONT_XL | FONT_B, THEME_TEXT);
    y += 110;
#ifdef SNTV_PLATFORM_ANDROID
    if (!u->started) {
        snprintf(line, sizeof line, "Starting in %d s. Cuelight downloads it, then Android asks you to confirm the install.", (int)u->countdown + 1);
        center_line(line, cx, y, FONT_S, THEME_TEXT_DIM);
        choices("update now", "not now", cx, y + 70);
    } else if (u->state == 2) {
        center_line(u->status, cx, y, FONT_S, THEME_TEXT_DIM);
        center_line("Android reopens Cuelight with the new version. Cancelled it?", cx, y + 70, FONT_S, THEME_TEXT_DIM);
        choices("ask again", "close", cx, y + 140);
    } else if (u->state == -1) {
        snprintf(line, sizeof line, "%.200s", u->status);
        center_line(line, cx, y, FONT_S, THEME_DANGER);
        center_line("The APK is also on the release page on GitHub.", cx, y + 70, FONT_S, THEME_TEXT_DIM);
        choices("try again", "close", cx, y + 130);
    } else {
        ui_spinner(cx, y + 10, 26);
        center_line(u->status[0] ? u->status : "Starting\xE2\x80\xA6", cx, y + 70, FONT_M | FONT_B, THEME_TEXT);
        center_line("Keep Cuelight open until Android asks you to install it.", cx, y + 130, FONT_S, THEME_TEXT_DIM);
    }
    (void)steps;
    return;
#endif
    if (steps) draw_steps(u, cx, y - 60);
    else if (!u->started) {
        snprintf(line, sizeof line, "Starting in %d s. The app closes while it installs; open it again from the Home screen in about half a minute.", (int)u->countdown + 1);
        center_line(line, cx, y, FONT_S, THEME_TEXT_DIM);
        choices("update now", "not now (it asks again next time)", cx, y + 70);
    } else if (u->state == 2) {
        center_line("Installed. If the app is still open, close it and open it again.", cx, y, FONT_S, THEME_TEXT_DIM);
        choices("close the app", NULL, cx, y + 70);
    } else if (u->state == -1) {
        snprintf(line, sizeof line, "%.200s", u->status);
        center_line(line, cx, y, FONT_S, THEME_DANGER);
        center_line("By hand: the Homebrew Channel (Cuelight > Update), or the release's .ipk in webOS Dev Manager.", cx, y + 70, FONT_S, THEME_TEXT_DIM);
        choices("try again", "close", cx, y + 130);
    } else {
        ui_spinner(cx, y + 10, 26);
        center_line(u->status[0] ? u->status : "Starting\xE2\x80\xA6", cx, y + 70, FONT_M | FONT_B, THEME_TEXT);
        center_line("The app will close by itself when the new version is in place.", cx, y + 130, FONT_S, THEME_TEXT_DIM);
    }
}

static int key(Screen *s, KeyAction a)
{
    Upd *u = s->st;
    if (a == KEY_OK) {
#ifdef SNTV_PLATFORM_ANDROID
        if (!u->started || u->state == 2 || u->state == -1) { u->state = 0; start(u); }   /* again: Android asks once more */
        return 1;
#endif
        if (!u->started) start(u);
        else if (u->state == 2) g_app.quit = 1;
        else if (u->state == -1 && u->by_hand) app_toast("Open the Homebrew Channel from the Home screen to update");   /* no installer here at all; webOS won't let an app open another */
        else if (u->state == -1) { u->state = 0; u->status[0] = 0; start(u); }   /* again: the Homebrew Channel, else the TV's own installer */
        return 1;
    }
    if (a == KEY_BACK && (!u->started || u->state == 2 || u->state == -1)) {
        if (!u->started) LOGI("update: postponed by the viewer");
        app_pop();
        return 1;
    }
    return 1;   /* while it installs, the keys wait */
}

static int tap(Screen *s, int id)   /* its two buttons: OK and Back */
{
    if (g_touch_aim || (id != 1 && id != 2)) return 1;
    key(s, id == 1 ? KEY_OK : KEY_BACK);
    return 1;
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
    s->update = update; s->draw = draw; s->key = key; s->tap = tap; s->destroy = destroy; s->phone = 1;
    return s;
}
