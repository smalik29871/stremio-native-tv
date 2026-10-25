/* Installing Cuelight YouTube, the companion an LG opens for what YouTube refuses to the app
 * (deploy/webos-youtube), then opening the video that asked for it. The Homebrew Channel
 * installs it from the releases repository in a few seconds; Cuelight stays open meanwhile. */
#include "../app.h"
#include "../log.h"
#include "../video/video.h"
#include "font.h"
#include "theme.h"
#include "widgets.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char id[24], mode[8];   /* the video to open once it is in, or none (installed from Settings) */
    int started;            /* asked the installer (once its package is known: the releases list) */
    int state;              /* video_install_status */
    float done_at, asked_at, t;
    char status[200];
} YtWeb;

static void update(Screen *s, float dt)
{
    YtWeb *w = s->st;
    if (w->t < 1) { w->t += dt / 0.28f; if (w->t > 1) w->t = 1; }
    app_redraw();
    if (w->state == -1) return;
    if (!w->started) {
        if (!g_ytweb_url[0]) {
            if (w->t >= 1 && g_app.time - w->asked_at > 12) { w->state = -1; snprintf(w->status, sizeof w->status, "Its package was not found in Cuelight's releases. Check the connection and try again."); }
            return;
        }
        w->started = 1;
        if (video_install_update(g_ytweb_url, g_ytweb_sha256, SNTV_YTWEB_ID) != 0) { w->state = -1; snprintf(w->status, sizeof w->status, "This TV has no Homebrew Channel installer to ask."); }
        return;
    }
    w->state = video_install_status(w->status, sizeof w->status);
    if (w->state == -1) { LOGW("ytweb: the install failed: %s", w->status); return; }
    if (w->state == 2 && !w->done_at) { w->done_at = g_app.time; LOGI("ytweb: installed"); video_youtube_web_check(); }
    if (!w->done_at) return;
    if (video_youtube_web_state() == 1) {
        char id[24], mode[8];
        snprintf(id, sizeof id, "%s", w->id); snprintf(mode, sizeof mode, "%s", w->mode);
        app_pop();
        if (id[0] && video_youtube_web(id, 0, mode, app_ytweb_flags())) return;
        app_toast("Cuelight YouTube is installed: it opens by itself when YouTube refuses a video");
        return;
    }
    if (g_app.time - w->asked_at > 1.5f) { w->asked_at = g_app.time; video_youtube_web_check(); }   /* the TV lists it a moment after */
    if (g_app.time - w->done_at > 12) { w->state = -1; snprintf(w->status, sizeof w->status, "It installed, but the TV has not listed it yet. Try again in a moment."); }
}

static void draw(Screen *s)
{
    YtWeb *w = s->st;
    float k = g_app.fx_light ? 1 : 1 - (1 - w->t) * (1 - w->t) * (1 - w->t);
    Screen *below = g_app.depth >= 2 ? g_app.stack[g_app.depth - 2] : NULL;
    if (below && below->draw) { ui_hits_begin(below); below->draw(below); ui_hits_begin(s); } else ui_background(0);
    gfx_rect(0, 0, SNTV_UI_W, SNTV_UI_H, RGBA(0, 0, 0, (int)(170 * k)));
    float w_ = SNTV_PHONE ? SNTV_UI_W - 40 : 1000, h = 420, x = (SNTV_UI_W - w_) / 2, y = (SNTV_UI_H - h) / 2 + (1 - k) * 40, pad = 56;
    gfx_set_alpha(k);
    if (!g_app.fx_light) gfx_shadow(x, y + 16, w_, h, R_PANEL, 40, RGBA(0, 0, 0, 0x90));
    ui_panel(x, y, w_, h, R_PANEL, THEME_PANEL);
    font_draw_tracked("CUELIGHT YOUTUBE", x + pad, y + pad, FONT_XS | FONT_B, THEME_ACCENT, 1.2f);
    const char *title = w->state == -1 ? "It did not install" : w->done_at ? "Installed" : "Installing";
    font_draw(title, x + pad, y + pad + 40, FONT_L | FONT_B, THEME_TEXT);
    float ty = y + pad + 40 + font_height(FONT_L | FONT_B) + 30;
    if (w->state == -1) {
        font_draw_wrapped(w->status[0] ? w->status : "The Homebrew Channel did not answer.", x + pad, ty, FONT_S, THEME_DANGER, w_ - 2 * pad, 3);
        font_draw_wrapped("By hand: open the Homebrew Channel and install Cuelight YouTube from Cuelight's repository.", x + pad, ty + 110, FONT_S, THEME_TEXT_DIM, w_ - 2 * pad, 2);
    } else {
        ui_spinner(x + pad + 20, ty + 20, 20);
        font_draw_fit(w->status[0] ? w->status : "Asking the Homebrew Channel\xE2\x80\xA6", x + pad + 64, ty + 6, FONT_M | FONT_B, THEME_TEXT, w_ - 2 * pad - 64);
        font_draw_wrapped("YouTube's own TV client, in a small companion app. Back in it returns here.", x + pad, ty + 80, FONT_S, THEME_TEXT_DIM, w_ - 2 * pad, 2);
    }
    font_draw("Back closes", x + w_ - pad - font_measure("Back closes", FONT_XS), y + h - pad + 8, FONT_XS, THEME_TEXT_MUTED);
    gfx_set_alpha(1);
}

static int key(Screen *s, KeyAction a)
{
    (void)s;
    if (a == KEY_BACK) app_pop();
    return 1;
}

static void destroy(Screen *s) { free(s->st); free(s); }

Screen *screen_ytweb_install_create(const char *yt_id, const char *mode)
{
    Screen *s = calloc(1, sizeof *s);
    YtWeb *w = calloc(1, sizeof *w);
    snprintf(w->id, sizeof w->id, "%s", yt_id ? yt_id : "");
    snprintf(w->mode, sizeof w->mode, "%s", mode ? mode : "play");
    LOGI("ytweb: installing Cuelight YouTube");
    w->asked_at = g_app.time;
    if (!g_ytweb_url[0]) app_releases_fetch();
    s->name = "ytweb"; s->st = w;
    s->update = update; s->draw = draw; s->key = key; s->destroy = destroy;
    Screen *under = app_top();
    s->phone = under ? under->phone : 1;
    return s;
}
