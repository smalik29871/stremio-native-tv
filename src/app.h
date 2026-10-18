/* Application state shared by all screens: installed addons, the user's
 * library, auth, and a screen stack. Everything runs on the main thread;
 * network results arrive through http_pump() callbacks. */
#ifndef SNTV_APP_H
#define SNTV_APP_H
#include "addon/addon.h"
#include "http.h"
#include "quality.h"
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
    int long_ok;   /* OK acts on release here, and holding it sends KEY_LONG_OK (0 from key() = treat as OK) */
} Screen;

typedef enum { FX_AUTO = 0, FX_FULL = 1, FX_LIGHT = 2 } FxLevel;

/* One playable candidate handed from the stream picker to the player so it can
 * fall back to the next source when the pipeline rejects one. */
typedef struct PlayCandidate {
    char *url, *label, *binge_group, *filename;
    uint64_t video_size;
    unsigned needs;             /* TVCAP_* bits this source depends on (quality.h) */
} PlayCandidate;

enum { HOME_CINEMATIC, HOME_SHOWCASE, HOME_COMPACT, HOME_STYLE_COUNT };
extern const char *const HOME_STYLE_NAMES[HOME_STYLE_COUNT];

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
    Screen *pip;               /* the player, detached to a corner while browsing */
    int quit;
    float time;
    char toast[160]; float toast_until;

    /* preferences */
    FxLevel fx_setting;        /* what the user chose */
    int fx_light;              /* effective: 1 = light effects (resolved from Auto) */
    int spoiler_blur;          /* blur thumbnails of unwatched episodes */
    char pref_audio[8], pref_subs[8];   /* ISO 639-2/3 like "eng", or "" */
    int subs_size;             /* 0 small, 1 medium, 2 large */
    int subs_style;            /* SUBS_STYLE_* for addon subtitles */
    int autoplay_next;         /* Up Next at the credits */
    int skip_mode;             /* intro/recap skipping: 0 button, 1 automatic, 2 off */
    int subs_always;           /* 0: subtitles only when the audio is in another language */
    int clock_mode;            /* player clock: 0 12-hour, 1 24-hour, 2 off */
    int oled;                  /* pure black background */
    int theme, theme_accent;   /* THEME_* and its accent index (ui/theme.h) */
    int ambient_minutes;       /* screensaver after this long idle; 0 off */
    int quality_mode;          /* QUAL_*: how sources are ranked */
    int res_cap;               /* 2160, 1080 or 720 */
    int auto_recommended;      /* play the recommended source without asking */
    int preflight;             /* read the top candidates' headers before recommending */
    int live_scrub;            /* the video follows the seek bar while the key is held */
    int home_style;            /* HOME_CINEMATIC, HOME_SHOWCASE, HOME_COMPACT */
    int hide_watched;          /* catalogs skip titles the library marks watched */
    char *thumb_server;        /* optional seek preview server (tools/thumbs) */
    char parental_pin[8];      /* "" when none */
    int pin_settings;          /* ask for the PIN before Settings */
    int hide_adult;            /* catalogs of addons flagged adult are skipped */
    char *tmdb_key;            /* Settings > API keys: cast photos and age ratings */
    int   ai_provider;         /* AI_* in ai.h */
    char *ai_key, *ai_base, *ai_model;
    int memory_mode;           /* 0 automatic, 1 low, 2 normal */
    int low_memory;            /* effective: fewer rows and posters kept */
    int transitions;           /* animated screen changes */
    int pip_enabled;           /* Back in the player parks it in a corner instead of stopping */
    int subs_autosync;         /* shift addon subtitles to match a file-matched subtitle */
    int subs_autotranslate;    /* AI: translate when nobody offers the preferred language */
    int addon_wait;            /* seconds auto-play waits for slow addons; 99 = all of them */
    int src_audio_lang;        /* rank sources that mention the preferred audio language higher */
    int break_video;           /* show the video of a break recording full screen */
    /* take breaks (breaks.h) */
    int breaks_on, break_sched, break_method, break_asr_hanafi;
    int break_lead_min, break_len_min, break_sound;   /* sound: 0 chime, 1 custom audio, 2 silent */
    char *break_audio, *break_end;                    /* URLs played at the start and the end */
    int break_health_min, break_health_len, break_bed, break_custom[3], breaks_everywhere;
    double break_lat, break_lon; char break_city[64];
    int home_rows_version;     /* bumped when rows are reordered or hidden */
    TvProfile tv;              /* what this TV can decode, learned over time */
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
/* Draws the top screen, animating the change from the previous one when a transition runs. */
void app_draw_screen(Screen *top, float dt);
int  app_transition_active(void);

/* --- data --- */
void app_load_addons(void);                    /* from account or defaults; caches to disk */
void app_install_addon(const char *transport_url); /* fetches manifest, adds, pushes to account */
void app_remove_addon(int index);
int  app_move_addon(int index, int dir);
/* Saved accounts (every successful sign-in is kept, so switching needs no password). */
int  app_saved_accounts(char emails[][128], int max);   /* excludes the one signed in */
void app_switch_account(int index);
void app_forget_account(int index);
void app_set_parental_pin(const char *pin);              /* NULL or "" clears */
/* Opens Settings, asking for the parental PIN first when one guards it. */
void app_open_settings(void);       /* swaps with its neighbour; 1 when moved; synced to the account */
int  app_title_watched(const char *id);        /* the library says this title was watched */

/* Home rows: the order and visibility the viewer chose (Settings > Home rows). */
typedef struct HomeRowDef {
    char key[200];             /* "continue", "library", "catalog:<addon id>:<type>:<catalog id>" */
    char title[128];
    int kind;                  /* 0 continue watching, 1 library, 2 catalog */
    int addon, catalog;
    int hidden;
} HomeRowDef;
int  app_home_rows(HomeRowDef *out, int max);   /* ordered; hidden ones flagged */
void app_home_row_move(const char *key, int dir);
void app_home_row_toggle(const char *key);
void app_login(const char *email, const char *password, void (*done)(int ok, const char *msg));
void app_logout(void);
void app_load_library(void);
LibraryItem *app_library_find(const char *id);
/* Records playback progress locally and (when logged in) pushes it to the account. */
void app_library_progress(const MetaPreview *meta, const char *video_id, double position_s, double duration_s);
void app_library_toggle(const MetaPreview *meta);  /* add / remove from library */
void app_library_mark_watched(const MetaPreview *meta, int watched);
void app_library_dismiss_continue(const MetaPreview *meta);   /* drop from Continue Watching, keep in Library */
/* Links that failed to play are remembered for six hours and sorted last. */
void app_link_failed(const char *url);
/* Once a day, 20 s after start: is there a newer release in the Homebrew Channel repo? */
void app_update_check(float dt);
extern char g_update_version[16];   /* "" or the newer version */
int  app_link_dead(const char *url);

/* Resolve a stream to a playable URL. Returns malloc'd string or NULL (with reason). */
char *app_stream_playable_url(const Stream *s, char *reason, size_t reason_len);
void play_candidates_free(PlayCandidate *c, int n);

/* Catalog JSON disk cache (keyed by URL) for instant Home on launch. */
char *app_cache_get(const char *url, long max_age_seconds);
/* Worker-side parsers for http_request_async_ex: r->parsed becomes a ParsedMetas /
 * ParsedStreams. The _cached variant also writes the reply to the disk cache. */
typedef struct ParsedMetas { MetaPreview *m; int n; } ParsedMetas;
typedef struct ParsedStreams { Stream *s; int n; } ParsedStreams;
void app_work_metas(HttpResponse *r, const char *url);
void app_work_metas_cached(HttpResponse *r, const char *url);
void app_free_metas(void *parsed);
void app_work_streams(HttpResponse *r, const char *url);
void app_free_streams(void *parsed);
unsigned app_fetch_metas(const char *url, int cache, HttpCallback cb, void *user);
unsigned app_fetch_streams(const char *url, HttpCallback cb, void *user);
void  app_cache_put(const char *url, const char *body, size_t len);

/* The detail screen registers the episode list it shows; the player uses it for Up Next. */
void app_remember_videos(const char *meta_id, const MetaVideo *videos, int n);
int  app_next_video(const char *meta_id, const char *video_id, char *next_id, size_t id_n, char *next_title, size_t title_n);
/* dir +1 the following episode, -1 the one before. */
int  app_adjacent_video(const char *meta_id, const char *video_id, int dir, char *out_id, size_t id_n, char *out_title, size_t title_n);
/* The remembered episode list for this title (NULL when another title's list is held). */
const MetaVideo *app_videos(const char *meta_id, int *n);
const char *app_video_thumbnail(const char *meta_id, const char *video_id);

/* Artwork with metahub fallbacks for IMDb ids (what the official apps use). */
const char *app_art_background(const MetaPreview *p, char *buf, size_t n);
const char *app_art_logo(const MetaPreview *p, char *buf, size_t n);
const char *app_art_poster(const MetaPreview *p, char *buf, size_t n);

void app_save_prefs(void);
/* A source with these needs played fine (worked=1) or failed; persists the TV profile. */
void app_tv_learn(unsigned needs, int worked);
void app_note_frame(float ms);          /* feeds the Auto effects level */
void app_redraw(void);                  /* request a frame even when idle */
void app_library_flush(int force);
/* Raw meta JSON for a title, shared by Home (synopsis) and Detail; get returns a copy or NULL. */
void  app_meta_put(const char *type, const char *id, const char *body, size_t len);
char *app_meta_get(const char *type, const char *id);      /* write the library cache if it changed (deferred while playing) */

/* --- screens --- */
Screen *screen_home_create(void);
Screen *screen_explore_create(void);   /* catalogs by type, catalog and genre */
Screen *screen_library_create(void);   /* the account's library as a grid */
Screen *screen_calendar_create(void);  /* upcoming and recent episodes of library series */
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
Screen *screen_pin_create(const char *title, int setting, void (*done)(int ok, void *user), void *user);
Screen *screen_break_create(const char *name, const char *detail, int kind);
Screen *screen_ambient_create(void);   /* screensaver: drifting backdrops + clock */

/* Picture-in-picture: Back in the player parks the video in a corner and keeps it
 * playing under the other screens; Play/Pause brings it back, Stop closes it. */
void app_pip_enter(void);
void app_pip_restore(void);
void app_pip_close(void);
/* About to play `video_id`: restores the corner player if it is that video (1),
 * otherwise closes it so the new video can have the plane (0). */
int  app_pip_claim(const char *video_id);
void player_set_pip(Screen *s, int on);
void player_pip_update(Screen *s, float dt);
void player_pip_draw(Screen *s, int focused);
int  player_pip_done(Screen *s);
const char *player_video_id(Screen *s);
void player_reload_at(Screen *s, double position_s);   /* the same source again from `position_s` (after a break) */

/* Fetch an episode's streams ahead of time so Up Next starts without a spinner. */
void streams_prefetch(const MetaPreview *meta, const char *video_id);
/* Share of addons that have answered that prefetch, 0..1 (1 = ready to start at once);
 * -1 when none is running or it went stale. */
float streams_prefetch_progress(const char *video_id);

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
