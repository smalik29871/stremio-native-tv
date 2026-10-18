/* Application state shared by all screens: installed addons, the user's
 * library, auth, and a screen stack. Everything runs on the main thread;
 * network results arrive through http_pump() callbacks. */
#ifndef SNTV_APP_H
#define SNTV_APP_H
#include "addon/addon.h"
#include "addon/stremio_api.h"
#include "ui/input.h"
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

/* --- screens --- */
Screen *screen_home_create(void);
Screen *screen_detail_create(const MetaPreview *preview);
Screen *screen_streams_create(const MetaPreview *meta, const char *video_id, const char *video_title);
Screen *screen_player_create(const MetaPreview *meta, const char *video_id, const char *title, const char *url, double start_s);
Screen *screen_login_create(void);
Screen *screen_search_create(void);
Screen *screen_settings_create(void);

/* --- shared widgets --- */
void ui_draw_poster(const char *url, float x, float y, float w, float h, int focused, const char *fallback_label);
void ui_draw_header(const char *title, const char *subtitle);
void ui_draw_spinner(float cx, float cy);
void ui_draw_button(const char *label, float x, float y, float w, float h, int focused, int accent);
void ui_format_time(double seconds, char *out, size_t n);
void meta_preview_copy(MetaPreview *dst, const MetaPreview *src);

#endif
