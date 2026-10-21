/* A break: the picture pauses, a calm page names the moment (a prayer, a stretch,
 * bedtime, your own time), a chime or your recording plays, and playback
 * continues by itself when the break is over or when you press OK. */
#include "../app.h"
#include "../phone.h"
#include "../youtube.h"
#include "../audio.h"
#include "../breaks.h"
#include "../log.h"
#include "../video/video.h"
#include "font.h"
#include "theme.h"
#include "widgets.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char name[48], detail[64]; int kind;
    float elapsed; int len_s;
    int until_media;       /* the break lasts as long as the recording */
    int media_started;
    float closing_at;      /* elapsed when the closing recording started */
    Screen *player; int paused_video, was_paused, took_media, show_video; double resume_pos;
    int stage;             /* 0 opening sound, 1 the break, 2 closing sound, 3 done */
    int focus;             /* 0 continue, 1 skip today */
    int finished;
    float intro;
} Brk;

static Screen *find_player(void)
{
    if (g_app.pip) return g_app.pip;
    for (int i = g_app.depth - 1; i >= 0; i--) if (strcmp(g_app.stack[i]->name, "player") == 0) return g_app.stack[i];
    return NULL;
}

static void finish(Screen *s)
{
    Brk *b = s->st;
    if (b->finished) return;
    b->finished = 1;
    if (b->took_media) { video_stop(); if (b->player) player_reload_at(b->player, b->resume_pos, b->was_paused || b->kind == BRK_KIND_BEDTIME); }   /* bedtime: back, paused */
    else if (b->paused_video && b->kind != BRK_KIND_BEDTIME) video_play();
    if (b->kind == BRK_KIND_BEDTIME && b->player) app_toast("Bedtime: playback stays paused");
    app_pop();
}

static int play_url(const char *url, const char *title, int *is_video)
{
    char buf[1800];
    const char *real = breaks_media_url(url, buf, sizeof buf, is_video);
    char yt[16], saved[80];
    if (youtube_video_id(url, yt) && !youtube_saved(url, g_app.break_video, saved, sizeof saved, NULL)) {   /* the next break plays it from the TV */
        LOGI("break: %s is not saved on the TV yet; saving it for the next break", yt);
        youtube_save(url, g_app.break_video, NULL, NULL);
    }
    if (!real || !*real) return 0;
    video_load(real, title, 0);
    video_play();
    LOGI("break: playing %s%s", real, is_video && *is_video ? " (video)" : "");
    return 1;
}

static void update(Screen *s, float dt)
{
    Brk *b = s->st;
    if (b->finished) return;
    b->intro = ui_ease(b->intro * 100, 100, NULL, dt, 6) / 100;
    b->elapsed += dt;
    VideoInfo vi; video_info(&vi);
    if (b->stage == 0) {
        b->stage = 1;
    } else if (b->stage == 1) {
        if (vi.state == VIDEO_LOADING || vi.state == VIDEO_PLAYING || vi.state == VIDEO_PAUSED) b->media_started = 1;
        int stopped = vi.state == VIDEO_ENDED || vi.state == VIDEO_ERROR || vi.state == VIDEO_IDLE;
        int opening_done = !b->took_media || (b->media_started ? stopped : b->elapsed > 20);   /* never started: give up */
        int time_up = b->until_media || (b->len_s > 0 && b->elapsed >= b->len_s);
        if (time_up && opening_done) {
            if (b->took_media && g_app.break_end && *g_app.break_end) {
                b->stage = 2; b->closing_at = b->elapsed;
                int v = 0;
                if (!play_url(g_app.break_end, "Break", &v)) finish(s); else b->show_video = v;
            } else finish(s);
        }
    } else if (b->stage == 2) {
        if (vi.state == VIDEO_ENDED || vi.state == VIDEO_ERROR || vi.state == VIDEO_IDLE || b->elapsed - b->closing_at > 600) finish(s);
    }
    app_redraw();
}

/* The countdown under the break's name; returns how far along it is (0..1), or -1. */
static float status_line(const Brk *b, const VideoInfo *vi, char *line, size_t n)
{
    if (b->stage == 2) { snprintf(line, n, "Closing"); return -1; }
    if (b->took_media && vi->state == VIDEO_LOADING) { snprintf(line, n, "Loading the recording\xE2\x80\xA6"); return -1; }
    if (b->until_media) {
        if (vi->duration <= 0) { snprintf(line, n, "Playback continues when the recording ends"); return -1; }
        int left = (int)(vi->duration - vi->position + 0.5); if (left < 0) left = 0;
        snprintf(line, n, "Playback continues in %d:%02d", left / 60, left % 60);
        return (float)(vi->position / vi->duration);
    }
    if (b->len_s > 0) {
        int left = b->len_s - (int)b->elapsed; if (left < 0) left = 0;
        snprintf(line, n, "Playback continues in %d:%02d", left / 60, left % 60);
        return b->elapsed / b->len_s > 1 ? 1 : b->elapsed / b->len_s;
    }
    snprintf(line, n, "Press OK when you are ready");
    return -1;
}

static void draw(Screen *s)
{
    Brk *b = s->st;
    float k = g_app.fx_light ? 1 : b->intro;
    if (b->show_video) {   /* the recording's picture full screen, the break's words in a band below */
        VideoInfo vi; video_info(&vi);
        gfx_video_hole(0, 0, SNTV_UI_W, SNTV_UI_H);
        float by0 = SNTV_UI_H - 230;
        gfx_gradient_v(0, by0 - 120, SNTV_UI_W, 120, RGBA(0, 0, 0, 0), RGBA(0, 0, 0, 0xC8));
        gfx_rect(0, by0, SNTV_UI_W, SNTV_UI_H - by0, RGBA(0, 0, 0, 0xC8));
        const char *label = b->kind == BRK_KIND_PRAYER ? "TIME FOR PRAYER" : "BREAK";
        font_draw(label, THEME_MARGIN, by0 + 10, FONT_XS | FONT_B, THEME_ACCENT2);
        font_draw(b->name, THEME_MARGIN, by0 + 44, FONT_L | FONT_B, THEME_TEXT);
        char line[120];
        status_line(b, &vi, line, sizeof line);
        font_draw(line, THEME_MARGIN, by0 + 110, FONT_S, THEME_TEXT_DIM);
        float w1 = ui_pill_width("Continue now", 1, 60), w2 = ui_pill_width("Skip breaks today", 0, 60);
        float bx = SNTV_UI_W - THEME_MARGIN - w1 - w2 - 20;
        ui_pill("Continue now", 1, ICON_PLAY, bx, by0 + 60, 60, PILL_PRIMARY, b->focus == 0);
        ui_pill("Skip breaks today", 0, ICON_PLAY, bx + w1 + 20, by0 + 60, 60, PILL_SECONDARY, b->focus == 1);
        return;
    }
    ui_background(0);
    gfx_gradient_v(0, 0, SNTV_UI_W, SNTV_UI_H, COLOR_ALPHA(THEME_PANEL, 0xFF), RGBA(0x05, 0x05, 0x0E, 0xFF));
    float cx = SNTV_UI_W / 2, cy = 400;
    gfx_circle(cx, cy, 220 + 20 * k, COLOR_ALPHA(THEME_ACCENT, 0x18));
    gfx_circle(cx, cy, 150, COLOR_ALPHA(THEME_ACCENT2, 0x22));
    gfx_rrect_ring(cx - 118, cy - 118, 236, 236, 118, 3, COLOR_ALPHA(THEME_TEXT, 0x40));
    char now[24]; ui_clock_text(0, now, sizeof now);
    char *ampm = strchr(now, ' ');   /* "11:59 PM": the hour inside the ring, AM/PM small under it */
    if (ampm) *ampm++ = 0;
    font_draw_center(now, cx, cy - font_height(FONT_TITLE | FONT_B) / 2 - (ampm ? 14 : 0), FONT_TITLE | FONT_B, THEME_TEXT);
    if (ampm) font_draw_center(ampm, cx, cy + font_height(FONT_TITLE | FONT_B) / 2 - 6, FONT_S | FONT_B, THEME_TEXT_DIM);
    const char *label = b->kind == BRK_KIND_PRAYER ? "TIME FOR PRAYER" : b->kind == BRK_KIND_HEALTH ? "TAKE A BREAK" : b->kind == BRK_KIND_BEDTIME ? "BEDTIME" : "BREAK";
    font_draw_center(label, cx, cy + 170, FONT_XS | FONT_B, THEME_ACCENT2);
    font_draw_center(b->name, cx, cy + 204, FONT_XL | FONT_B, COLOR_ALPHA(THEME_TEXT, (int)(255 * k)));
    if (b->detail[0]) font_draw_center(b->detail, cx, cy + 280, FONT_S, THEME_TEXT_DIM);
    char line[120];
    VideoInfo vi; video_info(&vi);
    float done = status_line(b, &vi, line, sizeof line);
    font_draw_center(line, cx, cy + 340, FONT_S | FONT_B, THEME_TEXT_DIM);
    if (done >= 0) ui_progress(cx - 200, cy + 384, 400, 5, done, THEME_ACCENT);
    /* buttons */
    float w1 = ui_pill_width("Continue now", 1, 64), w2 = ui_pill_width("Skip breaks today", 0, 64);
    float bx = cx - (w1 + w2 + 20) / 2, by = SNTV_UI_H - 190;
    ui_pill("Continue now", 1, ICON_PLAY, bx, by, 64, PILL_PRIMARY, b->focus == 0);
    ui_pill("Skip breaks today", 0, ICON_PLAY, bx + w1 + 20, by, 64, PILL_SECONDARY, b->focus == 1);
    /* what comes later today */
    BreakEvent ev[16]; int n = breaks_today(ev, 16), now_m = breaks_minute_now();
    char next[200] = ""; size_t o = 0; int shown = 0;
    for (int i = 0; i < n && shown < 3; i++) if (ev[i].minute > now_m) { o += (size_t)snprintf(next + o, sizeof next - o, "%s%s %d:%02d", shown ? "  \xC2\xB7  " : "Later today:  ", ev[i].name, ev[i].minute / 60, ev[i].minute % 60); shown++; }
    if (next[0]) font_draw_center(next, cx, SNTV_UI_H - 80, FONT_XS, THEME_TEXT_MUTED);
}

static int key(Screen *s, KeyAction a)
{
    Brk *b = s->st;
    app_redraw();
    switch (a) {
    case KEY_LEFT: b->focus = 0; return 1;
    case KEY_RIGHT: b->focus = 1; return 1;
    case KEY_OK: if (b->focus == 1) breaks_skip_today(); finish(s); return 1;
    case KEY_BACK: case KEY_PLAY: case KEY_PLAY_PAUSE: finish(s); return 1;
    default: return 1;   /* the break holds the remote */
    }
}

static void destroy(Screen *s) { free(s->st); free(s); }

Screen *screen_break_create(const char *name, const char *detail, int kind)
{
    Screen *s = calloc(1, sizeof *s);
    Brk *b = calloc(1, sizeof *b);
    snprintf(b->name, sizeof b->name, "%s", name ? name : "Break");
    snprintf(b->detail, sizeof b->detail, "%s", detail ? detail : "");
    b->kind = kind;
    b->until_media = g_app.break_len_min < 0 && kind != BRK_KIND_HEALTH && kind != BRK_KIND_BEDTIME;
    b->len_s = kind == BRK_KIND_HEALTH ? g_app.break_health_len * 60 : b->until_media ? 0 : g_app.break_len_min * 60;
    if (kind == BRK_KIND_BEDTIME) b->len_s = 0;
    b->player = find_player();
    VideoInfo vi; video_info(&vi);
    if (b->player) b->resume_pos = player_resume_point(b->player);   /* loading or reconnecting reports 0: the player knows the spot */
    if (b->player && (vi.state == VIDEO_PLAYING || vi.state == VIDEO_LOADING)) { b->paused_video = 1; video_pause(); }
    else if (b->player && vi.state == VIDEO_PAUSED) b->was_paused = 1;   /* a recording replaces it: back to this spot, still paused */
    int custom = g_app.break_sound == 1 && g_app.break_audio && *g_app.break_audio && kind != BRK_KIND_HEALTH && !g_app.pip;
    if (custom && play_url(g_app.break_audio, name, &b->show_video)) b->took_media = 1;
    else if (g_app.break_sound != 2) audio_chime();
    if (b->until_media && !b->took_media) { b->until_media = 0; b->len_s = 60; }   /* no recording to follow: a minute */
    LOGI("break: %s (%s), %d s", b->name, b->detail, b->len_s);
    s->name = "break"; s->st = b;
    s->update = update; s->draw = draw; s->key = key; s->destroy = destroy;
    return s;
}
