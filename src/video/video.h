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
    int buffering;     /* 1 while the pipeline is stalled for data */
    int buffer_percent;
    char error[160];
    char codec[48];    /* informational, e.g. "H264 1920x1080 / AAC" */
    int tracks_version; /* bumps when the track lists change */
    char warning[160];  /* non-fatal problem worth telling the viewer (e.g. unsupported audio) */
    int warning_version;
} VideoInfo;

typedef enum { TRACK_AUDIO = 0, TRACK_TEXT = 1 } TrackKind;

typedef struct VideoTrack {
    int index;          /* backend index to pass to video_select_track */
    char lang[16];      /* ISO 639 when known */
    char label[64];
    char codec[32];
    int selected;
} VideoTrack;

int  video_init(SDL_Window *win);
void video_shutdown(void);
const char *video_backend_name(void);

/* start_seconds > 0 seeks once the pipeline reports loaded. */
int  video_load(const char *url, const char *title, double start_seconds);
void video_play(void);
void video_pause(void);
void video_seek(double seconds);
void video_stop(void);
void video_set_rate(double rate);
void video_update(void);                 /* poll backend events; main thread */
void video_info(VideoInfo *out);

/* Embedded tracks reported by the pipeline. Returns count copied (<= max). */
int  video_tracks(TrackKind kind, VideoTrack *out, int max);
void video_select_track(TrackKind kind, int index);  /* index -1 disables text tracks */

/* Window-pixel rectangle the video should cover (full window by default). */
void video_set_display_rect(int x, int y, int w, int h);
/* App went to background / came back (webOS pauses the pipeline itself, but we keep state honest). */
void video_app_background(void);
void video_app_foreground(void);

#endif
