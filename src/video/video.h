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
    double buffer_ahead;   /* seconds buffered past the position; -1 unknown */
    char error[160];
    char codec[48];    /* informational, e.g. "H264 1920x1080 / AAC" */
    int width, height, fps;   /* 0 when the pipeline has not said */
    long bitrate;             /* bits per second, 0 unknown */
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
/* Silence the video playing or loading now; every video_load starts with sound. */
void video_set_muted(int muted);
/* Loudness of the video, 0-100 (night mode); every video_load starts at 100. */
void video_set_volume(int percent);
/* Counts every video_load: whoever loaded can tell whether the video is still theirs. */
unsigned video_load_count(void);
void video_update(void);                 /* poll backend events; main thread */
void video_info(VideoInfo *out);

/* Embedded tracks reported by the pipeline. Returns count copied (<= max). */
int  video_tracks(TrackKind kind, VideoTrack *out, int max);
void video_select_track(TrackKind kind, int index);  /* index -1 disables text tracks */
/* The look and timing of subtitles the TV draws itself (tracks inside the file):
 * size 0..3, style SUBS_STYLE_*, position 0 bottom / 1 raised / 2 top, sync in ms
 * (positive shows them later). Kept and re-sent after every load and track change;
 * backends that cannot style the TV's own subtitles ignore it. */
void video_subtitle_style(int size, int style, const char *colour, int position, int sync_ms);   /* colour: a caption colour name, NULL for the style's */

/* A phone: only sideways (1) while the screen on top has no upright layout, else either way. */
void video_landscape_only(int on);
/* A link the app was opened with since the last call (a stremio:// addon), copied to out: 1, else 0. */
int  video_take_open_url(char *out, size_t n);
/* A web page (an addon's settings) in the device's browser: 1 when one opened, else 0. */
int  video_open_browser(const char *url);
/* A YouTube video in the device's own YouTube app (signed in, so YouTube's "are you a bot"
 * check is not ours to pass): 1 when it was handed over. has: there is such an app to try. */
int  video_has_youtube_app(void);
int  video_open_youtube(const char *id);
/* YouTube's own player inside the app, where the device has one (Android: a web view): a link
 * video_load plays for this video id (written to out, 1), for when YouTube refuses ours. */
int  video_youtube_embed_url(const char *id, char *out, size_t n);
/* Cuelight YouTube (deploy/webos-youtube): YouTube's own TV client in the TV's browser engine, a
 * companion app this one opens for what YouTube refuses to its own player, where there is no web
 * view inside the app (an LG). state: 1 installed, 0 not, -1 not known yet or none here; check asks
 * again (after installing it); open: mode "play", "loop" (the fireplace) or "break", 1 when sent. */
#define SNTV_YTWEB_ID "org.stremio.nativetv.yt"
enum { YTWEB_SKIP_SPONSORS = 1, YTWEB_NO_ADVERTS = 2 };   /* flags: what its script does in YouTube's page */
int  video_youtube_web_state(void);
void video_youtube_web_check(void);
int  video_youtube_web(const char *id, double start, const char *mode, int flags);
/* A phone: picture in picture, the video in a small window over other apps. allow: a video
 * is playing, so leaving the app (Home) takes it along; enter: now (1 if it went); in: it is
 * in that small window now (the app then draws no controls). 0 where there is none. */
void video_pip_allow(int on);
int  video_pip_enter(void);
int  video_in_pip(void);
/* A phone's own controls, 0..1: its media volume and the screen's brightness (-1 for the
 * brightness: the system's own again). The getters give -1 where the device has no such
 * control and the setters 0 (the player then uses its own loudness, or dims the picture). */
float video_device_volume(void);
int   video_set_device_volume(float v);
float video_brightness(void);
int   video_set_brightness(float v);

/* Window-pixel rectangle the video should cover (full window by default). */
void video_set_display_rect(int x, int y, int w, int h);
/* App went to background / came back (webOS pauses the pipeline itself, but we keep state honest). */
void video_app_background(void);
void video_app_foreground(void);
/* A start under way with no video loaded yet (the next episode's sources being found):
 * the TV's own screensaver waits, as it does for a playing video. */
void video_keep_awake(int on);
/* The app's own screensaver is up (the fireplace, a clock): the TV's must not take over
 * from it. Kept apart from video_keep_awake, which the sources list sets and clears. */
void video_hold_screensaver(int on);
/* Paused by a video_pause() call (the viewer, the app going to the background, a break),
 * not a stall that the TV reports as a pause (LG does). */
int video_paused_on_purpose(void);
/* The memory stage (memwatch.h): the backend sizes its buffers by it where it can (Android). */
void video_memory_stage(int stage);

/* Updating the app itself (here because the system bus lives in this backend). On webOS
 * the Homebrew Channel's service downloads the package, checks its SHA-256 and installs
 * it; the system closes this app while it does. 0 when the request went out. */
int  video_install_update(const char *ipk_url, const char *sha256, const char *app_id);
/* What the installer said last ("Downloading…  42%"), and its state:
 * 0 nothing asked, 1 working, 2 finished, -1 failed (out has the reason). */
int  video_install_status(char *out, size_t n);

#endif
