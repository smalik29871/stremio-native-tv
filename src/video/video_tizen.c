/* Samsung Tizen native backend: capi-media-player rendering to an overlay
 * display beneath the EGL surface. */
#include "video.h"
#include "../log.h"
#include <player.h>
#include <stdio.h>
#include <string.h>
#include <SDL_syswm.h>

static player_h g_player;
static VideoInfo g_info;
static double g_pending_seek = -1;
static int g_autoplayed;
static void *g_win_handle;

static void on_prepared(void *user)
{
    (void)user;
    int ms = 0;
    player_get_duration(g_player, &ms);
    g_info.duration = ms / 1000.0;
    g_info.state = VIDEO_PAUSED;
    g_info.tracks_version++;
}
static void on_completed(void *user) { (void)user; g_info.state = VIDEO_ENDED; }
static void on_error(int code, void *user)
{
    (void)user;
    snprintf(g_info.error, sizeof g_info.error, "player error 0x%x", code);
    g_info.state = VIDEO_ERROR;
}
static void on_buffering(int percent, void *user) { (void)user; g_info.buffer_percent = percent; g_info.buffering = percent < 100; }

int video_init(SDL_Window *win)
{
    SDL_SysWMinfo wm;
    SDL_VERSION(&wm.version);
    if (SDL_GetWindowWMInfo(win, &wm)) {
#ifdef SDL_VIDEO_DRIVER_TIZEN
        g_win_handle = wm.info.tizen.window;
#else
        g_win_handle = NULL;
#endif
    }
    return 0;
}

const char *video_backend_name(void) { return "Tizen capi-media-player"; }
void video_shutdown(void) { video_stop(); }

static unsigned g_load_count;

int video_load(const char *url, const char *title, double start)
{
    (void)title;
    video_stop();
    g_load_count++;
    memset(&g_info, 0, sizeof g_info); g_info.buffer_ahead = -1;
    g_info.state = VIDEO_LOADING;
    g_pending_seek = start > 0 ? start : -1;
    g_autoplayed = 0;
    int rc = player_create(&g_player);
    if (rc != PLAYER_ERROR_NONE) { snprintf(g_info.error, sizeof g_info.error, "player_create %d", rc); g_info.state = VIDEO_ERROR; return -1; }
    player_set_uri(g_player, url);
    if (g_win_handle) player_set_display(g_player, PLAYER_DISPLAY_TYPE_OVERLAY, GET_DISPLAY(g_win_handle));
    player_set_display_mode(g_player, PLAYER_DISPLAY_MODE_LETTER_BOX);
    player_set_completed_cb(g_player, on_completed, NULL);
    player_set_error_cb(g_player, on_error, NULL);
    player_set_buffering_cb(g_player, on_buffering, NULL);
    rc = player_prepare_async(g_player, on_prepared, NULL);
    if (rc != PLAYER_ERROR_NONE) { snprintf(g_info.error, sizeof g_info.error, "prepare %d", rc); g_info.state = VIDEO_ERROR; return -1; }
    return 0;
}

void video_play(void) { if (g_player && player_start(g_player) == PLAYER_ERROR_NONE) g_info.state = VIDEO_PLAYING; }
void video_pause(void) { if (g_player && player_pause(g_player) == PLAYER_ERROR_NONE) g_info.state = VIDEO_PAUSED; }
void video_seek(double s) { if (g_player) { player_set_play_position(g_player, (int)(s * 1000), true, NULL, NULL); g_info.position = s; } }
void video_set_rate(double r) { if (g_player) player_set_playback_rate(g_player, (float)r); }
void video_set_muted(int m) { if (g_player) player_set_mute(g_player, m); }
unsigned video_load_count(void) { return g_load_count; }

void video_stop(void)
{
    if (!g_player) return;
    player_stop(g_player);
    player_unprepare(g_player);
    player_destroy(g_player);
    g_player = NULL;
    g_info.state = VIDEO_IDLE;
}

void video_set_display_rect(int x, int y, int w, int h) { if (g_player) player_set_display_roi_area(g_player, x, y, w, h); }
void video_app_background(void) { video_pause(); }
void video_app_foreground(void) {}

/* No installer on this platform: updates come from its store. */
int video_install_update(const char *ipk_url, const char *sha256, const char *app_id) { (void)ipk_url; (void)sha256; (void)app_id; return -1; }
int video_install_status(char *out, size_t n) { if (n) out[0] = 0; return 0; }

void video_update(void)
{
    if (!g_player) return;
    if (g_info.state == VIDEO_PAUSED && !g_autoplayed) {
        g_autoplayed = 1;
        if (g_pending_seek > 0) video_seek(g_pending_seek);
        video_play();
    }
    if (g_info.state == VIDEO_PLAYING || g_info.state == VIDEO_PAUSED) {
        int ms = 0;
        if (player_get_play_position(g_player, &ms) == PLAYER_ERROR_NONE) g_info.position = ms / 1000.0;
    }
}

void video_info(VideoInfo *out) { *out = g_info; }

int video_tracks(TrackKind kind, VideoTrack *out, int max)
{
    if (!g_player) return 0;
    player_stream_type_e type = kind == TRACK_AUDIO ? PLAYER_STREAM_TYPE_AUDIO : PLAYER_STREAM_TYPE_TEXT;
    int count = 0, cur = 0;
    if (player_get_track_count(g_player, type, &count) != PLAYER_ERROR_NONE) return 0;
    player_get_current_track(g_player, type, &cur);
    if (count > max) count = max;
    for (int i = 0; i < count; i++) {
        memset(&out[i], 0, sizeof out[i]);
        out[i].index = i;
        char *code = NULL;
        if (player_get_track_language_code(g_player, type, i, &code) == PLAYER_ERROR_NONE && code) {
            snprintf(out[i].lang, sizeof out[i].lang, "%s", code);
            free(code);
        }
        snprintf(out[i].label, sizeof out[i].label, "%s %d", out[i].lang[0] ? out[i].lang : (kind == TRACK_AUDIO ? "Audio" : "Subtitle"), i + 1);
        out[i].selected = i == cur;
    }
    return count;
}

void video_subtitle_style(int size, int style, int position, int sync_ms) { (void)size; (void)style; (void)position; (void)sync_ms; }

void video_select_track(TrackKind kind, int index)
{
    if (!g_player) return;
    player_stream_type_e type = kind == TRACK_AUDIO ? PLAYER_STREAM_TYPE_AUDIO : PLAYER_STREAM_TYPE_TEXT;
    if (index >= 0) player_select_track(g_player, type, index);
    g_info.tracks_version++;
}
