/* Your TorBox videos: to watch one, or to pick one as the fireplace video or a break's recording. */
#include "../app.h"
#include "../log.h"
#include "../store.h"
#include "../torbox.h"
#include "font.h"
#include "theme.h"
#include "widgets.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { ROW_H = 84, ROWS = 8 };

typedef struct {
    int target;              /* TORBOX_FOR_* */
    TorboxFile *files; int n;
    int loading, focus;
    float scroll;
    char error[200];
} Pick;

static Pick *g_live;

static void on_files(const TorboxFile *files, int n, const char *error, void *user)
{
    (void)user;
    Pick *p = g_live;
    if (!p) return;
    p->loading = 0;
    if (files) { p->files = malloc(sizeof *files * (size_t)n); memcpy(p->files, files, sizeof *files * (size_t)n); p->n = n; }
    else snprintf(p->error, sizeof p->error, "%s", error ? error : "Nothing to show");
    app_redraw();
}

static void update(Screen *s, float dt)
{
    Pick *p = s->st;
    float target = p->focus > ROWS - 3 ? (float)(p->focus - (ROWS - 3)) * ROW_H : 0;
    p->scroll = ui_ease(p->scroll, target, NULL, dt, 16);
    if (p->loading) app_redraw();
}

static void draw(Screen *s)
{
    Pick *p = s->st;
    ui_background(0);
    float x = THEME_MARGIN, w = SNTV_UI_W - 2 * THEME_MARGIN, y = 150;
    font_draw("Your TorBox videos", x, 60, FONT_XL | FONT_B, THEME_TEXT);
    font_draw(p->target == TORBOX_FOR_PLAY ? "Everything you keep on TorBox: torrents, usenet and web downloads. OK plays it here at full quality." : p->target == TORBOX_FOR_FIREPLACE ? "Pick one for the fireplace screensaver. It plays from TorBox at full quality, always through a fresh link."
                                                : "Pick one to play at the start of each break. It plays from TorBox at full quality, always through a fresh link.",
              x, 120, FONT_S, THEME_TEXT_DIM);
    if (p->loading) { ui_spinner(SNTV_UI_W / 2, 480, 26); font_draw_center("Asking TorBox\xE2\x80\xA6", SNTV_UI_W / 2, 530, FONT_M | FONT_B, THEME_TEXT_DIM); return; }
    if (!p->n) { font_draw_center(p->error, SNTV_UI_W / 2, 470, FONT_M | FONT_B, THEME_TEXT_DIM); return; }
    gfx_clip_push(x - 20, y + 30, w + 40, ROWS * ROW_H + 10);
    for (int i = 0; i < p->n; i++) {
        float ry = y + 40 + i * ROW_H - p->scroll;
        if (ry + ROW_H < y || ry > y + 40 + ROWS * ROW_H) continue;
        int foc = i == p->focus;
        if (foc) gfx_rrect(x - 12, ry, w + 24, ROW_H - 10, R_KEY, THEME_FOCUS);
        ui_icon(ICON_PLAY, x + 22, ry + (ROW_H - 10) / 2, 26, foc ? THEME_ON_FOCUS : THEME_TEXT_MUTED);
        font_draw_fit(p->files[i].name, x + 60, ry + 12, FONT_S | FONT_B, foc ? THEME_ON_FOCUS : THEME_TEXT, w - 260);
        char sz[32];
        if (p->files[i].size_mb >= 1024) snprintf(sz, sizeof sz, "%.1f GB", p->files[i].size_mb / 1024); else snprintf(sz, sizeof sz, "%.0f MB", p->files[i].size_mb);
        font_draw(sz, x + w - 20 - font_measure(sz, FONT_XS), ry + 26, FONT_XS, foc ? THEME_ON_FOCUS : THEME_TEXT_MUTED);
        const char *kind = strstr(p->files[i].link, "://usenet/") ? "Usenet" : strstr(p->files[i].link, "://webdl/") ? "Web download" : "Torrent";
        font_draw(kind, x + 60, ry + 46, FONT_XS, foc ? COLOR_ALPHA(THEME_ON_FOCUS, 0xB0) : THEME_TEXT_MUTED);
    }
    gfx_clip_pop();
}

static int key(Screen *s, KeyAction a)
{
    Pick *p = s->st;
    app_redraw();
    switch (a) {
    case KEY_UP: if (p->focus > 0) p->focus--; return 1;
    case KEY_DOWN: if (p->focus < p->n - 1) p->focus++; return 1;
    case KEY_OK:
        if (p->focus >= p->n) return 1;
        if (p->target == TORBOX_FOR_PLAY) {
            char url[800];
            if (!torbox_resolve(p->files[p->focus].link, url, sizeof url)) { app_toast("Add your TorBox API key first"); return 1; }
            LOGI("torbox: playing %s", p->files[p->focus].link);
            app_play_named(url, p->files[p->focus].name);
            return 1;
        }
        if (p->target == TORBOX_FOR_FIREPLACE) {
            store_set_str("fireplaceVideo", p->files[p->focus].link); store_save();
            app_toast("Fireplace video: %s", p->files[p->focus].name);
        } else {
            free(g_app.break_audio);
            g_app.break_audio = strdup(p->files[p->focus].link);
            g_app.break_sound = 1;
            app_save_prefs();
            app_toast("Break recording: %s", p->files[p->focus].name);
        }
        LOGI("torbox: %s set to %s", p->target == TORBOX_FOR_FIREPLACE ? "fireplace video" : "break recording", p->files[p->focus].link);
        app_pop();
        return 1;
    case KEY_BACK: app_pop(); return 1;
    default: return 1;
    }
}

static void destroy(Screen *s)
{
    Pick *p = s->st;
    if (g_live == p) g_live = NULL;
    free(p->files);
    free(p);
    free(s);
}

Screen *screen_torbox_create(int target)
{
    Screen *s = calloc(1, sizeof *s);
    Pick *p = calloc(1, sizeof *p);
    p->target = target;
    p->loading = 1;
    g_live = p;
    s->name = "torbox"; s->st = p;
    s->update = update; s->draw = draw; s->key = key; s->destroy = destroy;
    torbox_list_videos(on_files, NULL);
    return s;
}
