/* Samsung Tizen native backend: capi-media-player rendering to an overlay
 * display beneath the EGL surface. SDL on Tizen creates an Elementary window;
 * we fetch it with SDL_SysWMinfo-equivalent via the exported window handle. */
#include "video.h"
#include "../log.h"
#include <player.h>
#include <string.h>
#include <SDL_syswm.h>

static player_h g_player;
static VideoInfo g_info;
static double g_pending_seek = -1;
static void *g_win_handle;

static void on_prepared(void *user)
{
    (void)user;
    int ms = 0;
    player_get_duration(g_player, &ms);
    g_info.duration = ms / 1000.0;
    g_info.state = VIDEO_PAUSED;
}
static void on_completed(void *user) { (void)user; g_info.state = VIDEO_ENDED; }
static void on_error(int code, void *user)
{
    (void)user;
    snprintf(g_info.error, sizeof g_info.error, "player error 0x%x", code);
    g_info.state = VIDEO_ERROR;
}
static void on_buffering(int percent, void *user) { (void)user; g_info.buffering = percent < 100; }

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

int video_load(const char *url, const char *title, double start)
{
    (void)title;
    video_stop();
    memset(&g_info, 0, sizeof g_info);
    g_info.state = VIDEO_LOADING;
    g_pending_seek = start > 0 ? start : -1;
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
void video_seek(double s) { if (g_player) player_set_play_position(g_player, (int)(s * 1000), true, NULL, NULL); }

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

void video_update(void)
{
    if (!g_player) return;
    if (g_info.state == VIDEO_PAUSED && g_pending_seek != -2) {
        if (g_pending_seek > 0) video_seek(g_pending_seek);
        g_pending_seek = -2;
        video_play();
    }
    if (g_info.state == VIDEO_PLAYING || g_info.state == VIDEO_PAUSED) {
        int ms = 0;
        if (player_get_play_position(g_player, &ms) == PLAYER_ERROR_NONE) g_info.position = ms / 1000.0;
    }
}

void video_info(VideoInfo *out) { *out = g_info; }
