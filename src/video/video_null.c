/* Desktop preview backend: no decoding, just a simulated clock so the player
 * UI, progress reporting and library sync can be exercised on a dev machine. */
#include "video.h"
#include "../log.h"
#include <string.h>

static VideoInfo g;
static Uint32 g_last;

int video_init(SDL_Window *win) { (void)win; g.state = VIDEO_IDLE; return 0; }
void video_shutdown(void) {}
const char *video_backend_name(void) { return "null (desktop preview)"; }

int video_load(const char *url, const char *title, double start)
{
    LOGI("video(null): load %s (%s) from %.0fs", url, title ? title : "", start);
    memset(&g, 0, sizeof g);
    g.state = VIDEO_LOADING;
    g.position = start;
    g.duration = 2 * 3600.0;   /* pretend */
    g_last = SDL_GetTicks();
    return 0;
}

void video_play(void) { if (g.state == VIDEO_PAUSED || g.state == VIDEO_LOADING) g.state = VIDEO_PLAYING; }
void video_pause(void) { if (g.state == VIDEO_PLAYING) g.state = VIDEO_PAUSED; }
void video_seek(double s) { g.position = s < 0 ? 0 : s; if (g.duration && g.position > g.duration) g.position = g.duration; }
void video_stop(void) { g.state = VIDEO_IDLE; }
void video_set_display_rect(int x, int y, int w, int h) { (void)x; (void)y; (void)w; (void)h; }

void video_update(void)
{
    Uint32 now = SDL_GetTicks();
    double dt = (now - g_last) / 1000.0;
    g_last = now;
    if (g.state == VIDEO_LOADING && g.position >= 0) { g.state = VIDEO_PLAYING; }
    if (g.state == VIDEO_PLAYING) {
        g.position += dt;
        if (g.duration && g.position >= g.duration) { g.position = g.duration; g.state = VIDEO_ENDED; }
    }
}

void video_info(VideoInfo *out) { *out = g; }
