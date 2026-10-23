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
    /* Touch: a tap landed on what the screen drew with ui_hit(..., id). Move the focus there
     * and return 0 for OK to follow (as if the remote had moved there and pressed it), or
     * 1 when the tap did everything itself. Screens without it get OK on their focus. */
    int  (*tap)(struct Screen *s, int id);
    /* Touch, optional: a finger went down at canvas x, y (phase 0); return 1 to have the whole
     * gesture, its moves (1) and its lifting (2), instead of a tap or a scroll. */
    int  (*drag)(struct Screen *s, int phase, float x, float y);
    int phone;   /* it has a layout for a phone held upright; without one the phone turns sideways */
} Screen;

typedef enum { FX_AUTO = 0, FX_FULL = 1, FX_LIGHT = 2 } FxLevel;

/* One playable candidate handed from the stream picker to the player so it can
 * fall back to the next source when the pipeline rejects one. */
typedef struct PlayCandidate {
    char *url, *label, *binge_group, *filename;
    char *detail, *release;     /* "1080p HEVC · DD+ · cached", the release name; NULL when unknown */
    int addon_order;            /* the addon's place in the user's list */
    uint64_t video_size;
    unsigned needs;             /* TVCAP_* bits this source depends on (quality.h) */
    int res;                    /* vertical resolution from the source's name, 0 unknown */
    char badges[96];            /* "4K|DOLBY VISION|HEVC|CACHED|2.1 GB", as the Sources page shows them */
    int recommended;            /* the Sources page's pick */
    char *headers, *video_hash, *subs;   /* from the stream (Stream.headers, .video_hash, .subs), NULL none */
} PlayCandidate;

/* The source an episode played from, so the next one can match it (Up Next) */
typedef struct ChainHint {
    char binge_group[160];      /* the addon's own tag for one release, "" none */
    char group[24];             /* release group from the file name ("NTb"), "" unknown */
    int addon_order;            /* -1 unknown */
    int res;                    /* 0 unknown */
    unsigned needs;             /* TVCAP_* bits: codec, HDR, audio */
} ChainHint;

enum { HOME_CINEMATIC, HOME_SHOWCASE, HOME_COMPACT, HOME_STYLE_COUNT };
enum { ISLAND_TIME, ISLAND_DAY, ISLAND_SECONDS, ISLAND_DIAL, ISLAND_STYLE_COUNT };   /* the clock island's styles */
extern const char *const ISLAND_STYLE_NAMES[ISLAND_STYLE_COUNT];
enum { AMBIENT_ARTWORK, AMBIENT_WALL, AMBIENT_CLOCK, AMBIENT_LOGO, AMBIENT_FIRE, AMBIENT_AURORA, AMBIENT_RAIN, AMBIENT_MARQUEE,
       AMBIENT_WARP, AMBIENT_STYLE_COUNT };
extern const char *const AMBIENT_STYLE_NAMES[AMBIENT_STYLE_COUNT];
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
    float press_at;            /* when OK was last pressed: the focused card dips */
    float card_rect[4]; float card_at;   /* the focused card as last drawn, and when: screens open out of it */
    char toast[160]; float toast_until;

    /* preferences */
    FxLevel fx_setting;        /* what the user chose */
    int fx_light;              /* effective: 1 = light effects (resolved from Auto) */
    int spoiler_blur;          /* blur thumbnails of unwatched episodes */
    int advisory;              /* the rating and content advisory when a video starts */
    char pref_audio[8], pref_subs[8];   /* ISO 639-2/3 like "eng", or "" */
    int subs_size;             /* 0 small, 1 medium, 2 large, 3 extra large */
    int subs_pos;              /* 0 bottom, 1 raised, 2 top */
    int subs_style;            /* SUBS_STYLE_* for addon subtitles */
    int subs_colour;           /* SUBS_COLOUR_*: the letters' colour, 0 the style's own */
    int autoplay_next;         /* Up Next at the credits */
    int skip_mode;             /* intro/recap skipping: 0 button, 1 automatic, 2 off */
    int night_mode;            /* quieter, larger subtitles: 0 off, 1 from 11 pm to 6 am, 2 always */
    int subs_always;           /* 0: subtitles only when the audio is in another language */
    int clock_mode;            /* player clock: 0 12-hour, 1 24-hour, 2 off */
    int island_style;          /* ISLAND_*: how the clock in the corner looks */
    int clock_face;            /* CLOCKFACE_* for the Clock screensaver */
    int oled;                  /* pure black background */
    int theme, theme_accent, theme_bg;   /* THEME_*, its accent and background indexes (ui/theme.h) */
    int card_details;          /* a focused card opens a details panel after a moment */
    int auto_trailers;         /* trailers play behind the artwork: 0 off, 1 muted, 2 with sound */
    float ok_hold;             /* OK held on a long-press screen: 0 not held, else 0..1 toward options (1 = release opens them) */
    int ambient_minutes;       /* screensaver after this long idle; 0 off */
    int ambient_style;         /* AMBIENT_ARTWORK, _WALL, _CLOCK, _LOGO */
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
    int ask_profile;           /* "Who's watching?" at start when several accounts are saved */
    int hide_adult;            /* catalogs of addons flagged adult are skipped */
    char *tmdb_key;            /* Settings > Accounts: cast photos and age ratings */
    int   ai_provider;         /* AI_* in ai.h */
    char *ai_key, *ai_base, *ai_model;
    int memory_mode;           /* 0 automatic, 1 low, 2 normal */
    int low_memory;            /* effective: fewer rows and posters kept */
    int transitions;           /* animated screen changes */
    int pip_enabled;           /* Back in the player parks it in a corner instead of stopping */
    int subs_autosync;         /* shift addon subtitles to match a file-matched subtitle */
    int subs_prefer_files;     /* a subtitle file matched to the video over the video's own track (your style; may be a little off) */
    int subs_autotranslate;    /* AI: translate when nobody offers the preferred language */
    int addon_wait;            /* seconds auto-play waits for slow addons; 99 = all of them */
    int src_max_gb;            /* source rules: largest file to pick, 0 any */
    int src_hdr;               /* 0 any, 1 no Dolby Vision, 2 no HDR at all */
    int src_cached_only;       /* only debrid-cached or direct links */
    char src_exclude[128];     /* comma-separated words a source name must not contain */
    int src_audio_lang;        /* rank sources that mention the preferred audio language higher */
    int p2p_fallback;          /* torrents play on this TV (torrent.h) when no streaming server is set */
    int auto_update;           /* ask when a new release is out (else a notice only) */
    int break_video;           /* show the video of a break recording full screen */
    /* take breaks (breaks.h) */
    int breaks_on, break_sched, break_method, break_asr_hanafi;   /* break_method -1: by location */
    char break_country[4];     /* where the location lookup placed the TV ("CA"), for the method */
    int break_lead_min, break_len_min, break_sound;   /* sound: 0 chime, 1 custom audio, 2 silent */
    char *break_audio, *break_end;                    /* URLs played at the start and the end */
    int break_health_min, break_health_len, break_bed, break_custom[3], breaks_everywhere;
    double break_lat, break_lon; char break_city[64];
    int home_rows_version;     /* bumped when rows are reordered or hidden */
    TvProfile tv;              /* what this TV can decode, learned over time */
    float last_input;          /* g_app.time of the last key press */
    int key_repeat;            /* the key being handled is auto-repeat from a held key */

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
    int kind;                  /* 0 continue watching, 1 library, 2 catalog, 3 a saved list (catalog = which), 4 a Trakt row */
    int addon, catalog;
    int hidden;
} HomeRowDef;
int  app_home_rows(HomeRowDef *out, int max);   /* ordered; hidden ones flagged */
void app_home_row_move(const char *key, int dir);
void app_home_row_toggle(const char *key);
void app_login(const char *email, const char *password, void (*done)(int ok, const char *msg));
void app_logout(void);      /* stops syncing with Stremio: addons, library and lists stay on this TV */
void app_sync_begin(void);  /* about to sign in: this TV's own addons join the account once its list arrives */
void app_load_library(void);
LibraryItem *app_library_find(const char *id);
/* Records playback progress locally and (when logged in) pushes it to the account. */
void app_library_progress(const MetaPreview *meta, const char *video_id, double position_s, double duration_s, int played_through);   /* played_through: got there by playing (past 90% it is watched); a seek there is not */
int  app_library_remote_progress(const char *id, const char *type, const char *name, const char *video_id,
                                 double position_s, double duration_s, int64_t when_ms);   /* newer than ours: taken, sent on */
void app_library_foreground(void);   /* back on screen: the account's library again (at most once a minute) */
void app_library_tick(int playing);  /* every 15 min while nothing plays */
void app_library_toggle(const MetaPreview *meta);  /* add / remove from library */
void app_library_mark_watched(const MetaPreview *meta, int watched);
/* Episode marks set by hand (they override what the resume point implies):
 * -1 none, 0 not watched, 1 watched. Kept on this TV. */
int  app_episode_mark(const char *series_id, const char *video_id);
void app_episode_set_marks(const char *series_id, const char *const *video_ids, int n, int watched);
int  app_episode_marks_any(const char *series_id);   /* any episode recorded as watched */
void app_library_dismiss_continue(const MetaPreview *meta);   /* drop from Continue Watching, keep in Library */
/* Links that failed to play are remembered for six hours and sorted last. */
void app_link_failed(const char *url);
/* 20 s after start, then every 6 hours: is there a newer release in the Homebrew
 * Channel repo? At the next quiet moment (nothing playing) the viewer is asked; it
 * installs only on "Update now" (screen_update_create), or from Settings > About. */
void app_update_check(float dt);
void app_update_check_now(void);
void app_update_foreground(void);   /* back on screen: an opening, so it checks again */
extern char g_update_version[16];   /* "" or the newer version */
extern char g_update_url[600], g_update_sha256[80];   /* the package and its checksum */
Screen *screen_update_create(int countdown_s);
/* 0 once this TV refused the app the Homebrew Channel installer: updates are done there by hand */
int app_update_installs_here(void);
/* Your TorBox videos: pick one as the fireplace video or the break recording. */
enum { TORBOX_FOR_FIREPLACE, TORBOX_FOR_BREAK, TORBOX_FOR_PLAY };
Screen *screen_torbox_create(int target);
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
/* What a season pack's file may call this episode: its numbers, title and number
 * counted from the first (from the loaded episode list, when it is this series'). */
void app_episode_hint(const char *meta_id, const char *show, const char *video_id, EpisodeHint *h);
const char *app_video_thumbnail(const char *meta_id, const char *video_id);

/* Artwork with metahub fallbacks for IMDb ids (what the official apps use). */
const char *app_art_background(const MetaPreview *p, char *buf, size_t n);
const char *app_art_logo(const MetaPreview *p, char *buf, size_t n);
const char *app_art_poster(const MetaPreview *p, char *buf, size_t n);

void app_save_prefs(void);
/* A source with these needs played fine (worked=1) or failed; persists the TV profile. */
void app_tv_learn(unsigned needs, int worked);
/* Settings backup without the sign-in or what this TV learned (malloc'd JSON), and back:
 * returns how many settings were restored, -1 when the text is not a backup. */
char *app_backup_settings(void);
int  app_restore_settings(const char *json);
/* Plays a direct video link (sent from a phone); nothing is saved against a title. */
void app_play_link(const char *url);
void app_play_named(const char *url, const char *name);   /* a link with a name to show */
void app_note_frame(float ms);          /* feeds the Auto effects level */
void app_redraw(void);                  /* request a frame even when idle */
void app_library_flush(int force);
/* Raw meta JSON for a title, shared by Home (synopsis) and Detail; get returns a copy or NULL. */
void  app_meta_put(const char *type, const char *id, const char *body, size_t len);
char *app_meta_get(const char *type, const char *id);
/* Full details for a card: _want (from update) loads them from the caches or the first
 * meta addon; _cached (safe in draw) returns them once parsed, else NULL. */
void app_details_want(const char *type, const char *id);
const MetaPreview *app_details_cached(const char *type, const char *id);      /* write the library cache if it changed (deferred while playing) */

/* --- screens --- */
Screen *screen_home_create(void);
Screen *screen_explore_create(void);   /* catalogs by type, catalog and genre */
Screen *screen_library_create(void);   /* the account's library as a grid */
Screen *screen_calendar_create(void);  /* upcoming and recent episodes of library series */
Screen *screen_detail_create(const MetaPreview *preview);
void    screen_detail_ask(const MetaPreview *preview);   /* the title page with its Ask AI panel open */
Screen *screen_streams_create(const MetaPreview *meta, const char *video_id, const char *video_title);
/* The same, starting the recommended source by itself (Back returns to the list). */
Screen *screen_streams_create_play(const MetaPreview *meta, const char *video_id, const char *video_title);
Screen *screen_streams_create_from_start(const MetaPreview *meta, const char *video_id, const char *video_title);   /* Restart: 0:00 */
/* The player takes ownership of `cands`. next_video_id (optional) enables Up Next. */
/* 1 while a video is on that nothing but a break may cover: playing, stalled, between
 * sources; 0 with no video, or paused by the viewer */
int player_holds_screen(void);
int streams_starting(void);   /* a Sources screen is starting a video by itself (the next episode, Play) */
Screen *screen_player_create(const MetaPreview *meta, const char *video_id, const char *title,
                             PlayCandidate *cands, int n_cands, int start_index, double start_s,
                             const char *next_video_id, const char *next_title);
/* Opens the stream picker; with autoplay_group it plays the matching stream at once. */
Screen *screen_streams_create_auto(const MetaPreview *meta, const char *video_id, const char *video_title, const char *autoplay_group);
/* Up Next: picks and plays the source most like `hint` by itself (NULL: the viewer chooses) */
Screen *screen_streams_create_chain(const MetaPreview *meta, const char *video_id, const char *video_title, const ChainHint *hint);
Screen *screen_login_create(void);
Screen *screen_search_create(void);
Screen *screen_search_create_query(const char *q);   /* already searching for q (a person on a title page) */
Screen *screen_search_create_person(const char *name, int tmdb_person_id);   /* their films and series (TMDB), else a search for the name */
Screen *screen_settings_create(void);
Screen *screen_diagnostics_create(int playback_only);
Screen *screen_welcome_create(void);   /* first-run setup */
Screen *screen_whatsnew_create(int about);  /* this version's notes (CHANGELOG.md); about: what Cuelight is first */
void app_whatsnew_check(void);         /* shows them once after an update */
Screen *screen_pin_create(const char *title, int setting, void (*done)(int ok, void *user), void *user);
Screen *screen_break_create(const char *name, const char *detail, int kind);
Screen *screen_ambient_create(void);   /* screensaver: drifting backdrops + clock */
Screen *screen_profiles_create(void);  /* Who's watching: the saved accounts as profiles */
int     profiles_at_start(void);       /* signed in, others saved and the picker wanted */

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
void player_save_progress(void);
void player_save_progress_quiet(void);   /* an overlay over the app: saved locally, Trakt not told it stopped */
/* Clear old caches and unused recordings from the data folder, on a thread (tidy.h). */
void app_tidy(int delay_ms);   /* now, e.g. as the app goes to the background */
/* What the app keeps on this device (bytes; -1 until counted) and how much is cache;
 * app_clear_cache empties the cache on a background thread and counts again. */
void app_storage(long long *total, long long *cache);
void app_clear_cache(void);
double player_resume_point(Screen *s);   /* where it plays from: the position, or (loading, reconnecting) where it is headed */
void player_reload_at(Screen *s, double position_s, int paused);   /* the same source again from `position_s` (after a break), paused again if it was */

/* Fetch an episode's streams ahead of time so Up Next starts without a spinner. */
void streams_prefetch(const MetaPreview *meta, const char *video_id);
/* Where this link's redirects ended, found by a check moments ago; NULL none (open the link). */
const char *streams_final_url(const char *url);
/* Share of addons that have answered that prefetch, 0..1 (1 = ready to start at once);
 * -1 when none is running or it went stale. */
float streams_prefetch_progress(const char *video_id);
/* Asks every addon again for that episode (fresher links), and how old the answers are
 * in seconds (-1 when none). */
void streams_prefetch_again(const MetaPreview *meta, const char *video_id);
float streams_prefetch_age(const char *video_id);
/* The prefetch has every answer: the first megabytes of the source Up Next will most
 * likely play (the same binge group first) are fetched, so the server and any debrid
 * link are warm when it starts. 1 when a file was asked for. */
int streams_prefetch_warm(const char *video_id, const char *binge_group);
/* Every addon's sources for this video, ranked as the Sources screen would, handed to
 * `cb` once all answered (or after 15 s). One lookup at a time: a new one, or cancel,
 * drops the previous. The player uses it to leave P2P when TorBox has the title again. */
typedef void (*StreamsLookupCb)(const Stream *items, int n, void *user);
void streams_lookup(const MetaPreview *meta, const char *video_id, StreamsLookupCb cb, void *user);
/* The same, for the player asking by itself: only the sources that may play without the
 * viewer picking them (no notices, other episodes, dead links, broken rules). */
void streams_lookup_auto(const MetaPreview *meta, const char *video_id, StreamsLookupCb cb, void *user);
void streams_lookup_cancel(void);
void streams_lookup_tick(void);
/* A player candidate for one of those sources (0 when it cannot play here). */
int  streams_candidate(const MetaPreview *meta, const Stream *s, PlayCandidate *out);
/* Badges from PlayCandidate.badges, left to right from x, the ones that fit in max_w; returns the end. */
float streams_draw_badges(const char *list, float x, float y, float max_w, int foc);

/* Titles with artwork worth showing in the screensaver (library + catalog rows). */
typedef struct AmbientItem { char id[64], name[128], background[512], poster[512]; } AmbientItem;
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
