#include "../app.h"
#include "../log.h"
#include "../video/video.h"
#include "../platform/platform.h"
#include "font.h"
#include "theme.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    MetaPreview meta;
    char *video_id, *title, *url;
    float overlay_until;
    float since_report;
    double last_reported;
    int finished;
    int seek_steps; float seek_decay;   /* accelerating seek on held keys */
} Player;

static void report(Player *p, int force)
{
    VideoInfo vi;
    video_info(&vi);
    if (vi.state == VIDEO_IDLE || vi.state == VIDEO_LOADING || vi.state == VIDEO_ERROR) return;
    if (!force && vi.position == p->last_reported) return;
    p->last_reported = vi.position;
    app_library_progress(&p->meta, p->video_id, vi.position, vi.duration);
}

static void show_overlay(Player *p) { p->overlay_until = g_app.time + 4.0f; }

static void update(Screen *s, float dt)
{
    Player *p = s->st;
    VideoInfo vi;
    video_info(&vi);
    p->since_report += dt;
    if (p->since_report > 30.0f && vi.state == VIDEO_PLAYING) { p->since_report = 0; report(p, 0); }
    p->seek_decay -= dt;
    if (p->seek_decay <= 0) p->seek_steps = 0;
    if (vi.state == VIDEO_ENDED && !p->finished) {
        p->finished = 1;
        report(p, 1);
        app_pop();
    }
}

static void draw(Screen *s)
{
    Player *p = s->st;
    VideoInfo vi;
    video_info(&vi);
#ifdef SNTV_PLATFORM_LINUX
    gfx_rect(0, 0, SNTV_UI_W, SNTV_UI_H, RGBA(0x08, 0x08, 0x0C, 0xFF));
    font_draw("Desktop preview: no video decoding. The TV builds play through the platform pipeline.",
              THEME_MARGIN, SNTV_UI_H / 2 - 20, FONT_S, THEME_TEXT_MUTED);
#else
    gfx_video_hole(0, 0, SNTV_UI_W, SNTV_UI_H);
#endif
    int overlay = g_app.time < p->overlay_until || vi.state != VIDEO_PLAYING;
    if (vi.state == VIDEO_LOADING || vi.buffering) ui_draw_spinner(SNTV_UI_W / 2, SNTV_UI_H / 2);
    if (vi.state == VIDEO_ERROR) {
        gfx_rect(0, 0, SNTV_UI_W, SNTV_UI_H, THEME_SCRIM);
        font_draw("Playback failed", THEME_MARGIN, SNTV_UI_H / 2 - 60, FONT_L, THEME_DANGER);
        font_draw_wrapped(vi.error, THEME_MARGIN, SNTV_UI_H / 2, FONT_M, THEME_TEXT, SNTV_UI_W - 2 * THEME_MARGIN, 3);
        font_draw("Press Back to choose another stream", THEME_MARGIN, SNTV_UI_H / 2 + 120, FONT_S, THEME_TEXT_DIM);
        return;
    }
    if (!overlay) return;

    gfx_gradient_v(0, SNTV_UI_H - 320, SNTV_UI_W, 320, RGBA(0, 0, 0, 0), RGBA(0, 0, 0, 0xD0));
    gfx_gradient_v(0, 0, SNTV_UI_W, 160, RGBA(0, 0, 0, 0xA0), RGBA(0, 0, 0, 0));
    font_draw_fit(p->title, THEME_MARGIN, 40, FONT_L, THEME_TEXT, SNTV_UI_W - 2 * THEME_MARGIN);

    float bx = THEME_MARGIN, by = SNTV_UI_H - 120, bw = SNTV_UI_W - 2 * THEME_MARGIN;
    gfx_rect(bx, by, bw, 10, RGBA(255, 255, 255, 60));
    if (vi.duration > 0) gfx_rect(bx, by, bw * (float)(vi.position / vi.duration), 10, THEME_ACCENT);
    char pos[32], dur[32], line[80];
    ui_format_time(vi.position, pos, sizeof pos);
    ui_format_time(vi.duration, dur, sizeof dur);
    snprintf(line, sizeof line, "%s  /  %s", pos, vi.duration > 0 ? dur : "--:--");
    font_draw(line, bx, by + 24, FONT_M, THEME_TEXT);
    const char *state = vi.state == VIDEO_PAUSED ? "Paused" : vi.state == VIDEO_LOADING ? "Loading" : vi.buffering ? "Buffering" : "Playing";
    font_draw(state, bx + bw - font_measure(state, FONT_M), by + 24, FONT_M, THEME_TEXT_DIM);
    font_draw("OK: play/pause    Left/Right: seek    Back: stop", bx, by - 50, FONT_S, THEME_TEXT_MUTED);
}

static void seek_by(Player *p, double base)
{
    VideoInfo vi;
    video_info(&vi);
    p->seek_steps++;
    p->seek_decay = 0.6f;
    double step = base * (p->seek_steps > 6 ? 6 : p->seek_steps > 3 ? 3 : 1);
    double target = vi.position + step;
    if (target < 0) target = 0;
    if (vi.duration > 0 && target > vi.duration - 1) target = vi.duration - 1;
    video_seek(target);
    show_overlay(p);
}

static int key(Screen *s, KeyAction a)
{
    Player *p = s->st;
    VideoInfo vi;
    video_info(&vi);
    switch (a) {
    case KEY_OK: case KEY_PLAY_PAUSE:
        if (vi.state == VIDEO_PLAYING) video_pause(); else video_play();
        show_overlay(p);
        return 1;
    case KEY_PLAY: video_play(); show_overlay(p); return 1;
    case KEY_PAUSE: video_pause(); show_overlay(p); return 1;
    case KEY_LEFT: case KEY_REWIND: seek_by(p, -10); return 1;
    case KEY_RIGHT: case KEY_FORWARD: seek_by(p, 10); return 1;
    case KEY_UP: seek_by(p, 60); return 1;
    case KEY_DOWN: seek_by(p, -60); return 1;
    case KEY_INFO: case KEY_MENU: show_overlay(p); return 1;
    case KEY_STOP: case KEY_BACK:
        report(p, 1);
        video_stop();
        app_pop();
        return 1;
    default: return 0;
    }
}

static void destroy(Screen *s)
{
    Player *p = s->st;
    video_stop();
    meta_preview_free(&p->meta);
    free(p->video_id); free(p->title); free(p->url);
    free(p);
    free(s);
}

Screen *screen_player_create(const MetaPreview *meta, const char *video_id, const char *title, const char *url, double start_s)
{
    Screen *s = calloc(1, sizeof *s);
    Player *p = calloc(1, sizeof *p);
    meta_preview_copy(&p->meta, meta);
    p->video_id = strdup(video_id);
    p->title = strdup(title ? title : meta->name);
    p->url = strdup(url);
    s->name = "player"; s->st = p;
    s->update = update; s->draw = draw; s->key = key; s->destroy = destroy;
    LOGI("player: %s @%.0fs -> %s", p->title, start_s, url);
    if (video_load(url, p->title, start_s) != 0) app_toast("Could not start playback");
    show_overlay(p);
    return s;
}
