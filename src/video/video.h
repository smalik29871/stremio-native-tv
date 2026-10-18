/* Platform video backend. The UI never touches the player directly; it draws
 * a transparent hole where the video should show and polls video_info(). */
#ifndef SNTV_VIDEO_H
#define SNTV_VIDEO_H
#include <SDL.h>

typedef enum { VIDEO_IDLE, VIDEO_LOADING, VIDEO_PLAYING, VIDEO_PAUSED, VIDEO_ENDED, VIDEO_ERROR } VideoState;

typedef struct VideoInfo {
    VideoState state;
    double position;   /* seconds */
    double duration;   /* seconds, 0 when unknown */
    int buffering;
    char error[160];
} VideoInfo;

int  video_init(SDL_Window *win);
void video_shutdown(void);
const char *video_backend_name(void);

/* start_seconds > 0 seeks once the pipeline reports loaded. */
int  video_load(const char *url, const char *title, double start_seconds);
void video_play(void);
void video_pause(void);
void video_seek(double seconds);
void video_stop(void);
void video_update(void);                 /* poll backend events; main thread */
void video_info(VideoInfo *out);
/* Window-pixel rectangle the video should cover (full window by default). */
void video_set_display_rect(int x, int y, int w, int h);

#endif
