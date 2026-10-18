/* Desktop preview backend: no decoding, just a simulated clock so the player
 * UI, track menu, progress reporting and library sync can be exercised. */
#include "video.h"
#include "../log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static VideoInfo g;
static Uint32 g_last;
static int g_audio_sel = 0, g_text_sel = -1;
static double g_rate = 1.0;

int video_init(SDL_Window *win) { (void)win; g.state = VIDEO_IDLE; return 0; }
void video_shutdown(void) {}
const char *video_backend_name(void) { return "null (desktop preview)"; }

int video_load(const char *url, const char *title, double start)
{
    LOGI("video(null): load %s (%s) from %.0fs", url, title ? title : "", start);
    memset(&g, 0, sizeof g);
    g.state = VIDEO_LOADING;
    g.position = start;
    g.duration = 2 * 3600.0;
    /* test hooks: SNTV_FAKE_DURATION / SNTV_FAKE_START (seconds) */
    const char *fd = getenv("SNTV_FAKE_DURATION"), *fs = getenv("SNTV_FAKE_START");
    if (fd) g.duration = atof(fd);
    if (fs && start <= 0) g.position = atof(fs);
    snprintf(g.codec, sizeof g.codec, "H264 1920x1080 / AAC 5.1");
    g.width = 1920; g.height = 1080; g.fps = 24; g.bitrate = 8200000;
    g_last = SDL_GetTicks();
    g_audio_sel = 0; g_text_sel = -1; g_rate = 1.0;
    g.tracks_version = 1;
    return 0;
}

void video_play(void) { if (g.state == VIDEO_PAUSED || g.state == VIDEO_LOADING) g.state = VIDEO_PLAYING; }
void video_pause(void) { if (g.state == VIDEO_PLAYING) g.state = VIDEO_PAUSED; }
void video_seek(double s) { g.position = s < 0 ? 0 : s; if (g.duration && g.position > g.duration) g.position = g.duration; }
void video_stop(void) { g.state = VIDEO_IDLE; }
void video_set_rate(double r) { g_rate = r; }
void video_set_display_rect(int x, int y, int w, int h) { (void)x; (void)y; (void)w; (void)h; }
void video_app_background(void) { video_pause(); }
void video_app_foreground(void) {}

void video_update(void)
{
    Uint32 now = SDL_GetTicks();
    double dt = (now - g_last) / 1000.0;
    g_last = now;
    if (g.state == VIDEO_LOADING) g.state = VIDEO_PLAYING;
    if (g.state == VIDEO_PLAYING) {
        g.position += dt * g_rate;
        if (g.duration && g.position >= g.duration) { g.position = g.duration; g.state = VIDEO_ENDED; }
    }
}

void video_info(VideoInfo *out) { *out = g; }

int video_tracks(TrackKind kind, VideoTrack *out, int max)
{
    if (kind == TRACK_TEXT && getenv("SNTV_FAKE_NO_TEXT_TRACKS")) return 0;
    static const struct { const char *lang, *label, *codec; } A[] = { { "eng", "English", "AAC" }, { "spa", "Español", "AC3" } };
    static const struct { const char *lang, *label, *codec; } T[] = { { "eng", "English", "SRT" }, { "por", "Português", "ASS" } };
    int n = 0;
    for (int i = 0; i < 2 && n < max; i++, n++) {
        VideoTrack *t = &out[n];
        t->index = i;
        snprintf(t->lang, sizeof t->lang, "%s", kind == TRACK_AUDIO ? A[i].lang : T[i].lang);
        snprintf(t->label, sizeof t->label, "%s", kind == TRACK_AUDIO ? A[i].label : T[i].label);
        snprintf(t->codec, sizeof t->codec, "%s", kind == TRACK_AUDIO ? A[i].codec : T[i].codec);
        t->selected = kind == TRACK_AUDIO ? g_audio_sel == i : g_text_sel == i;
    }
    return n;
}

void video_subtitle_style(int size, int style, int position, int sync_ms) { (void)size; (void)style; (void)position; (void)sync_ms; }

void video_select_track(TrackKind kind, int index)
{
    if (kind == TRACK_AUDIO) g_audio_sel = index; else g_text_sel = index;
    g.tracks_version++;
}
