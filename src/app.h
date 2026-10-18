/* Application state shared by all screens: installed addons, the user's
 * library, auth, and a screen stack. Everything runs on the main thread;
 * network results arrive through http_pump() callbacks. */
#ifndef SNTV_APP_H
#define SNTV_APP_H
#include "addon/addon.h"
#include "addon/stremio_api.h"
#include "ui/input.h"
#include "ui/gfx.h"
#include "image.h"

typedef struct Screen {
    const char *name;
    void *st;
    void (*update)(struct Screen *s, float dt);
    void (*draw)(struct Screen *s);
    int  (*key)(struct Screen *s, KeyAction a);        /* 1 = consumed */
    void (*text)(struct Screen *s, const char *utf8);  /* optional */
    void (*resume)(struct Screen *s);                  /* becomes top again */
    void (*destroy)(struct Screen *s);
} Screen;

typedef enum { FX_AUTO = 0, FX_FULL = 1, FX_LIGHT = 2 } FxLevel;

/* One playable candidate handed from the stream picker to the player so it can
 * fall back to the next source when the pipeline rejects one. */
typedef struct PlayCandidate {
    char *url, *label, *binge_group, *filename;
    uint64_t video_size;
} PlayCandidate;

typedef struct App {
    Addon *addons; int n_addons;
    int addons_loading;
    char addons_error[320];

    LibraryItem *library; int n_library;
    int library_loading;
    int addons_version, library_version;   /* bumped on every change */

    char *auth_key, *user_email;
    char *streaming_server;   /* optional http://host:11470 for infoHash streams */

    Screen *stack[12]; int depth;
    int quit;
    float time;
    char toast[160]; float toast_until;

    /* preferences */
    FxLevel fx_setting;        /* what the user chose */
    int fx_light;              /* effective: 1 = light effects (resolved from Auto) */
    int spoiler_blur;          /* blur thumbnails of unwatched episodes */
    char pref_audio[8], pref_subs[8];   /* ISO 639-2/3 like "eng", or "" */
    int subs_size;             /* 0 small, 1 medium, 2 large */
    int autoplay_next;         /* Up Next at the credits */
    int skip_mode;             /* intro/recap skipping: 0 button, 1 automatic, 2 off */
    int subs_always;           /* 0: subtitles only when the audio is in another language */
    int clock_mode;            /* player clock: 0 12-hour, 1 24-hour, 2 off */
    int oled;                  /* pure black background */
    int ambient_minutes;       /* screensaver after this long idle; 0 off */
    float last_input;          /* g_app.time of the last key press */

    /* perf */
    int frames_measured; float frame_ms_sum, frame_ms_worst;
    int need_redraw;           /* set by anything that changed on screen */
} App;

extern App g_app;

void app_init(void);
void app_shutdown(void);

void app_push(Screen *s);
void app_pop(void);
void app_replace(Screen *s);
Screen *app_top(void);
void app_toast(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void app_draw_toast(void);

/* --- data --- */
void app_load_addons(void);                    /* from account or defaults; caches to disk */
void app_install_addon(const char *transport_url); /* fetches manifest, adds, pushes to account */
void app_remove_addon(int index);
void app_login(const char *email, const char *password, void (*done)(int ok, const char *msg));
void app_logout(void);
void app_load_library(void);
LibraryItem *app_library_find(const char *id);
/* Records playback progress locally and (when logged in) pushes it to the account. */
void app_library_progress(const MetaPreview *meta, const char *video_id, double position_s, double duration_s);
void app_library_toggle(const MetaPreview *meta);  /* add / remove from library */

/* Resolve a stream to a playable URL. Returns malloc'd string or NULL (with reason). */
char *app_stream_playable_url(const Stream *s, char *reason, size_t reason_len);
void play_candidates_free(PlayCandidate *c, int n);

/* Catalog JSON disk cache (keyed by URL) for instant Home on launch. */
char *app_cache_get(const char *url, long max_age_seconds);
void  app_cache_put(const char *url, const char *body, size_t len);

/* The detail screen registers the episode list it shows; the player uses it for Up Next. */
void app_remember_videos(const char *meta_id, const MetaVideo *videos, int n);
int  app_next_video(const char *meta_id, const char *video_id, char *next_id, size_t id_n, char *next_title, size_t title_n);
const char *app_video_thumbnail(const char *meta_id, const char *video_id);

/* Artwork with metahub fallbacks for IMDb ids (what the official apps use). */
const char *app_art_background(const MetaPreview *p, char *buf, size_t n);
const char *app_art_logo(const MetaPreview *p, char *buf, size_t n);
const char *app_art_poster(const MetaPreview *p, char *buf, size_t n);

void app_save_prefs(void);
void app_note_frame(float ms);          /* feeds the Auto effects level */
void app_redraw(void);                  /* request a frame even when idle */

/* --- screens --- */
Screen *screen_home_create(void);
Screen *screen_detail_create(const MetaPreview *preview);
Screen *screen_streams_create(const MetaPreview *meta, const char *video_id, const char *video_title);
/* The player takes ownership of `cands`. next_video_id (optional) enables Up Next. */
Screen *screen_player_create(const MetaPreview *meta, const char *video_id, const char *title,
                             PlayCandidate *cands, int n_cands, int start_index, double start_s,
                             const char *next_video_id, const char *next_title);
/* Opens the stream picker; with autoplay_group it plays the matching stream at once. */
Screen *screen_streams_create_auto(const MetaPreview *meta, const char *video_id, const char *video_title, const char *autoplay_group);
Screen *screen_login_create(void);
Screen *screen_search_create(void);
Screen *screen_settings_create(void);
Screen *screen_diagnostics_create(int playback_only);
Screen *screen_welcome_create(void);   /* first-run setup */
Screen *screen_ambient_create(void);   /* screensaver: drifting backdrops + clock */

/* Fetch an episode's streams ahead of time so Up Next starts without a spinner. */
void streams_prefetch(const MetaPreview *meta, const char *video_id);

/* Titles with artwork worth showing in the screensaver (library + catalog rows). */
typedef struct AmbientItem { char id[64], name[128], background[512]; } AmbientItem;
void app_ambient_offer(const MetaPreview *m);
int  app_ambient_pool(const AmbientItem **out);

/* --- shared widgets --- */
void ui_draw_poster(const char *url, float x, float y, float w, float h, int focused, const char *fallback_label);
void ui_draw_thumb(const char *url, float x, float y, float w, float h, int blur, Color placeholder);
float ui_ease(float from, float to, float *vel_unused, float dt, float speed);
void ui_draw_header(const char *title, const char *subtitle);
void ui_draw_spinner(float cx, float cy);
void ui_draw_button(const char *label, float x, float y, float w, float h, int focused, int accent);
void ui_format_time(double seconds, char *out, size_t n);
void meta_preview_copy(MetaPreview *dst, const MetaPreview *src);

#endif
