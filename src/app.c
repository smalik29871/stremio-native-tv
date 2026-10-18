#include "app.h"
#include "http.h"
#include "log.h"
#include "store.h"
#include "image.h"
#include "ui/font.h"
#include "ui/theme.h"
#include "cJSON.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

App g_app;

/* ---- screen stack ---------------------------------------------------------- */

void app_push(Screen *s)
{
    if (!s || g_app.depth >= (int)(sizeof g_app.stack / sizeof g_app.stack[0])) { if (s && s->destroy) s->destroy(s); return; }
    g_app.stack[g_app.depth++] = s;
}

void app_pop(void)
{
    if (g_app.depth <= 1) { g_app.quit = 1; return; }
    Screen *s = g_app.stack[--g_app.depth];
    if (s->destroy) s->destroy(s);
    Screen *top = app_top();
    if (top && top->resume) top->resume(top);
}

void app_replace(Screen *s)
{
    if (g_app.depth) { Screen *old = g_app.stack[--g_app.depth]; if (old->destroy) old->destroy(old); }
    app_push(s);
}

Screen *app_top(void) { return g_app.depth ? g_app.stack[g_app.depth - 1] : NULL; }

void app_toast(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(g_app.toast, sizeof g_app.toast, fmt, ap);
    va_end(ap);
    g_app.toast_until = g_app.time + 3.5f;
    LOGI("toast: %s", g_app.toast);
}

void app_draw_toast(void)
{
    if (g_app.time > g_app.toast_until || !g_app.toast[0]) return;
    float w = font_measure(g_app.toast, FONT_M) + 60, h = 70;
    float x = (SNTV_UI_W - w) / 2, y = SNTV_UI_H - 140;
    gfx_rect(x, y, w, h, RGBA(0x30, 0x2C, 0x58, 0xF0));
    gfx_rect(x, y, 6, h, THEME_ACCENT);
    font_draw(g_app.toast, x + 30, y + (h - font_height(FONT_M)) / 2, FONT_M, THEME_TEXT);
}

/* ---- addons ---------------------------------------------------------------- */

static void free_addons(void)
{
    for (int i = 0; i < g_app.n_addons; i++) addon_free(&g_app.addons[i]);
    free(g_app.addons);
    g_app.addons = NULL;
    g_app.n_addons = 0;
}

static void cache_addon_collection(const char *json)
{
    store_write_file("addons.json", json, strlen(json));
}

static void apply_collection_json(const char *json, int from_cache)
{
    Addon *addons; int n; ApiError err;
    int rc = api_parse_addon_collection(json, &addons, &n, &err);
    if (rc != 0) {
        snprintf(g_app.addons_error, sizeof g_app.addons_error, "%s", err.message);
        if (rc == 1 && err.code == 1) {   /* session expired */
            app_logout();
            app_toast("Session expired, please sign in again");
        }
        return;
    }
    free_addons();
    g_app.addons = addons;
    g_app.n_addons = n;
    g_app.addons_error[0] = 0;
    g_app.addons_version++;
    if (!from_cache) cache_addon_collection(json);
    LOGI("addons: %d loaded%s", n, from_cache ? " (cache)" : "");
}

static void on_collection(HttpResponse *r, void *user)
{
    (void)user;
    g_app.addons_loading = 0;
    if (r->status != 200) {
        snprintf(g_app.addons_error, sizeof g_app.addons_error, "addon collection: %s", r->error[0] ? r->error : "HTTP error");
        return;
    }
    apply_collection_json(r->body, 0);
}

/* Default (logged-out) addons: fetch each manifest, assemble a collection. */
typedef struct { int remaining; cJSON *addons; } DefaultsCtx;

static void on_default_manifest(HttpResponse *r, void *user)
{
    DefaultsCtx *ctx = ((void **)user)[0];
    char *url = ((void **)user)[1];
    free(user);
    cJSON *m = r->status == 200 ? cJSON_Parse(r->body) : NULL;
    if (m) {
        cJSON *d = cJSON_CreateObject();
        cJSON_AddStringToObject(d, "transportUrl", url);
        cJSON_AddItemToObject(d, "manifest", m);
        cJSON *f = cJSON_AddObjectToObject(d, "flags");
        cJSON_AddBoolToObject(f, "official", 1);
        cJSON_AddBoolToObject(f, "protected", 1);
        cJSON_AddItemToArray(ctx->addons, d);
    } else {
        LOGW("addons: default manifest %s failed (%ld %s)", url, r->status, r->error);
    }
    free(url);
    if (--ctx->remaining == 0) {
        cJSON *root = cJSON_CreateObject();
        cJSON *res = cJSON_AddObjectToObject(root, "result");
        cJSON_AddItemToObject(res, "addons", ctx->addons);
        char *txt = cJSON_PrintUnformatted(root);
        cJSON_Delete(root);
        g_app.addons_loading = 0;
        apply_collection_json(txt, 0);
        free(txt);
        free(ctx);
    }
}

static void load_default_addons(void)
{
    DefaultsCtx *ctx = calloc(1, sizeof *ctx);
    ctx->addons = cJSON_CreateArray();
    /* SNTV_DEFAULT_ADDONS="url1,url2" replaces the built-in defaults (used by tests/smoke.sh). */
    const char *override = getenv("SNTV_DEFAULT_ADDONS");
    if (override && *override) {
        char *copy = strdup(override);
        for (char *tok = strtok(copy, ","); tok; tok = strtok(NULL, ",")) ctx->remaining++;
        free(copy);
        copy = strdup(override);
        for (char *tok = strtok(copy, ","); tok; tok = strtok(NULL, ",")) {
            void **u = malloc(2 * sizeof(void *));
            u[0] = ctx; u[1] = strdup(tok);
            http_request_async(tok, NULL, on_default_manifest, u);
        }
        free(copy);
        return;
    }
    /* Keep user-installed addons from a previous logged-out session. */
    cJSON *extra = store_get_json("local_addons");
    int n_extra = cJSON_IsArray(extra) ? cJSON_GetArraySize(extra) : 0;
    for (int i = 0; STREMIO_DEFAULT_ADDON_URLS[i]; i++) ctx->remaining++;
    ctx->remaining += n_extra;
    for (int i = 0; STREMIO_DEFAULT_ADDON_URLS[i]; i++) {
        void **u = malloc(2 * sizeof(void *));
        u[0] = ctx; u[1] = strdup(STREMIO_DEFAULT_ADDON_URLS[i]);
        http_request_async(STREMIO_DEFAULT_ADDON_URLS[i], NULL, on_default_manifest, u);
    }
    for (int i = 0; i < n_extra; i++) {
        cJSON *e = cJSON_GetArrayItem(extra, i);
        if (!cJSON_IsString(e)) { ctx->remaining--; continue; }
        void **u = malloc(2 * sizeof(void *));
        u[0] = ctx; u[1] = strdup(e->valuestring);
        http_request_async(e->valuestring, NULL, on_default_manifest, u);
    }
}

void app_load_addons(void)
{
    g_app.addons_loading = 1;
    g_app.addons_error[0] = 0;
    /* Instant start from the last known collection, then refresh. */
    char *cached = store_read_file("addons.json");
    if (cached) { apply_collection_json(cached, 1); free(cached); }

    if (g_app.auth_key) {
        char *body = api_body_addon_collection_get(g_app.auth_key);
        http_request_async(api_url_addon_collection_get(), body, on_collection, NULL);
        free(body);
    } else {
        load_default_addons();
    }
}

static void push_collection_to_account(void)
{
    if (!g_app.auth_key) return;
    char *body = api_body_addon_collection_set(g_app.auth_key, g_app.addons, g_app.n_addons);
    http_request_async(api_url_addon_collection_set(), body, NULL, NULL);
    free(body);
}

static void persist_local_addon_urls(void)
{
    cJSON *arr = cJSON_CreateArray();
    for (int i = 0; i < g_app.n_addons; i++) {
        int is_default = 0;
        for (int d = 0; STREMIO_DEFAULT_ADDON_URLS[d]; d++)
            if (strcmp(STREMIO_DEFAULT_ADDON_URLS[d], g_app.addons[i].transport_url) == 0) is_default = 1;
        if (!is_default) cJSON_AddItemToArray(arr, cJSON_CreateString(g_app.addons[i].transport_url));
    }
    store_set_json("local_addons", arr);
    store_save();
}

static void on_install_manifest(HttpResponse *r, void *user)
{
    char *url = user;
    if (r->status != 200) { app_toast("Could not fetch addon: %s", r->error[0] ? r->error : "HTTP error"); free(url); return; }
    Addon a;
    if (addon_new(url, r->body, &a) != 0) { app_toast("Invalid addon manifest"); free(url); return; }
    for (int i = 0; i < g_app.n_addons; i++)
        if (strcmp(g_app.addons[i].transport_url, url) == 0) { app_toast("Addon already installed"); addon_free(&a); free(url); return; }
    g_app.addons = realloc(g_app.addons, sizeof(Addon) * (size_t)(g_app.n_addons + 1));
    g_app.addons[g_app.n_addons++] = a;
    g_app.addons_version++;
    app_toast("Installed %s", a.manifest.name);
    free(url);
    if (g_app.auth_key) push_collection_to_account(); else persist_local_addon_urls();
}

void app_install_addon(const char *transport_url)
{
    char url[1024];
    addon_url_manifest(transport_url, url, sizeof url);
    http_request_async(url, NULL, on_install_manifest, strdup(url));
}

void app_remove_addon(int i)
{
    if (i < 0 || i >= g_app.n_addons) return;
    if (g_app.addons[i].flag_protected) { app_toast("%s cannot be removed", g_app.addons[i].manifest.name); return; }
    addon_free(&g_app.addons[i]);
    memmove(&g_app.addons[i], &g_app.addons[i + 1], sizeof(Addon) * (size_t)(g_app.n_addons - i - 1));
    g_app.n_addons--;
    g_app.addons_version++;
    if (g_app.auth_key) push_collection_to_account(); else persist_local_addon_urls();
}

/* ---- auth ------------------------------------------------------------------ */

static void (*g_login_done)(int, const char *);

static void on_login(HttpResponse *r, void *user)
{
    (void)user;
    ApiError err;
    char *key = NULL, *email = NULL;
    if (r->status == 0) { if (g_login_done) g_login_done(0, r->error); return; }
    int rc = api_parse_auth(r->body, &key, &email, &err);
    if (rc != 0) { if (g_login_done) g_login_done(0, err.message); return; }
    free(g_app.auth_key); free(g_app.user_email);
    g_app.auth_key = key;
    g_app.user_email = email;
    store_set_str("authKey", key);
    store_set_str("email", email);
    store_save();
    app_load_addons();
    app_load_library();
    if (g_login_done) g_login_done(1, email ? email : "");
}

void app_login(const char *email, const char *password, void (*done)(int, const char *))
{
    g_login_done = done;
    char *body = api_body_login(email, password);
    http_request_async(api_url_login(), body, on_login, NULL);
    free(body);
}

void app_logout(void)
{
    if (g_app.auth_key) {
        char *body = api_body_logout(g_app.auth_key);
        http_request_async(api_url_logout(), body, NULL, NULL);
        free(body);
    }
    free(g_app.auth_key); free(g_app.user_email);
    g_app.auth_key = g_app.user_email = NULL;
    store_set_str("authKey", NULL);
    store_set_str("email", NULL);
    store_save();
    library_items_free(g_app.library, g_app.n_library);
    g_app.library = NULL; g_app.n_library = 0;
    g_app.library_version++;
    store_write_file("library.json", "{\"result\":[]}", 13);
    app_load_addons();
}

/* ---- library --------------------------------------------------------------- */

static void apply_library_json(const char *json, int from_cache)
{
    LibraryItem *items; int n; ApiError err;
    if (api_parse_library(json, &items, &n, &err) != 0) { LOGW("library: %s", err.message); return; }
    library_items_free(g_app.library, g_app.n_library);
    g_app.library = items;
    g_app.n_library = n;
    g_app.library_version++;
    if (!from_cache) store_write_file("library.json", json, strlen(json));
    LOGI("library: %d items%s", n, from_cache ? " (cache)" : "");
}

static void on_library(HttpResponse *r, void *user)
{
    (void)user;
    g_app.library_loading = 0;
    if (r->status == 200) apply_library_json(r->body, 0);
}

void app_load_library(void)
{
    char *cached = store_read_file("library.json");
    if (cached) { apply_library_json(cached, 1); free(cached); }
    if (!g_app.auth_key) return;
    g_app.library_loading = 1;
    char *body = api_body_datastore_get(g_app.auth_key, STREMIO_LIBRARY_COLLECTION);
    http_request_async(api_url_datastore_get(), body, on_library, NULL);
    free(body);
}

LibraryItem *app_library_find(const char *id)
{
    for (int i = 0; i < g_app.n_library; i++)
        if (strcmp(g_app.library[i].id, id) == 0) return &g_app.library[i];
    return NULL;
}

static int64_t now_ms(void) { return (int64_t)time(NULL) * 1000; }

static void save_library_cache(void)
{
    g_app.library_version++;
    cJSON *root = cJSON_CreateObject();
    cJSON *arr = cJSON_AddArrayToObject(root, "result");
    for (int i = 0; i < g_app.n_library; i++) cJSON_AddItemToArray(arr, library_item_to_json(&g_app.library[i]));
    char *txt = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    store_write_file("library.json", txt, strlen(txt));
    free(txt);
}

static LibraryItem *library_upsert(const MetaPreview *meta)
{
    LibraryItem *it = app_library_find(meta->id);
    if (it) return it;
    g_app.library = realloc(g_app.library, sizeof(LibraryItem) * (size_t)(g_app.n_library + 1));
    it = &g_app.library[g_app.n_library++];
    memset(it, 0, sizeof *it);
    it->id = strdup(meta->id);
    it->name = strdup(meta->name);
    it->type = strdup(meta->type);
    it->poster = meta->poster ? strdup(meta->poster) : NULL;
    it->poster_shape = strdup(meta->poster_shape ? meta->poster_shape : "poster");
    it->removed = 1;      /* progress-only entries are "temp" until the user adds them */
    it->temp = 1;
    it->ctime_ms = now_ms();
    return it;
}

static void push_library_item(const LibraryItem *it)
{
    if (!g_app.auth_key) return;
    char *body = api_body_datastore_put(g_app.auth_key, STREMIO_LIBRARY_COLLECTION, it, 1);
    http_request_async(api_url_datastore_put(), body, NULL, NULL);
    free(body);
}

void app_library_progress(const MetaPreview *meta, const char *video_id, double position_s, double duration_s)
{
    LibraryItem *it = library_upsert(meta);
    uint64_t pos = (uint64_t)(position_s * 1000), dur = (uint64_t)(duration_s * 1000);
    uint64_t prev = it->time_offset;
    it->time_offset = pos;
    if (dur) it->duration = dur;
    if (pos > prev) { it->time_watched += pos - prev; it->overall_time_watched += pos - prev; }
    it->last_watched_ms = now_ms();
    it->mtime_ms = it->last_watched_ms;
    free(it->video_id);
    it->video_id = video_id ? strdup(video_id) : NULL;
    /* Stremio marks an item watched past ~90%; reset offset so it leaves Continue Watching. */
    if (dur && pos > dur * 9 / 10) { it->times_watched++; it->flagged_watched = 1; it->time_offset = 0; }
    save_library_cache();
    push_library_item(it);
}

void app_library_toggle(const MetaPreview *meta)
{
    LibraryItem *it = library_upsert(meta);
    it->removed = !it->removed;
    it->temp = 0;
    it->mtime_ms = now_ms();
    save_library_cache();
    push_library_item(it);
    app_toast(it->removed ? "Removed from Library" : "Added to Library");
}

/* ---- streams --------------------------------------------------------------- */

char *app_stream_playable_url(const Stream *s, char *reason, size_t n)
{
    if (s->url) {
        if (strncmp(s->url, "http://", 7) == 0 || strncmp(s->url, "https://", 8) == 0) return strdup(s->url);
        if (strncmp(s->url, "magnet:", 7) == 0) { snprintf(reason, n, "Magnet links need a streaming server"); return NULL; }
        return strdup(s->url);   /* rtmp/ftp: let the pipeline decide */
    }
    if (s->info_hash) {
        if (!g_app.streaming_server || !*g_app.streaming_server) {
            snprintf(reason, n, "Torrent streams need a Stremio streaming server (Settings)");
            return NULL;
        }
        char buf[1024];
        /* server.js convention: /{infoHash}/{fileIdx}; -1 lets the server pick the largest file */
        snprintf(buf, sizeof buf, "%s/%s/%d", g_app.streaming_server, s->info_hash, s->file_idx < 0 ? -1 : s->file_idx);
        return strdup(buf);
    }
    if (s->yt_id) { snprintf(reason, n, "YouTube streams are not supported on this device yet"); return NULL; }
    if (s->external_url) { snprintf(reason, n, "This stream opens in an external app"); return NULL; }
    snprintf(reason, n, "Stream has no playable source");
    return NULL;
}

/* ---- shared widgets -------------------------------------------------------- */

void meta_preview_copy(MetaPreview *dst, const MetaPreview *src)
{
    memset(dst, 0, sizeof *dst);
#define D(f) dst->f = src->f ? strdup(src->f) : NULL
    D(id); D(type); D(name); D(poster); D(poster_shape); D(background); D(logo);
    D(description); D(release_info); D(imdb_rating); D(runtime);
#undef D
    if (src->genres.n) {
        dst->genres.v = calloc((size_t)src->genres.n + 1, sizeof(char *));
        for (int i = 0; i < src->genres.n; i++) dst->genres.v[dst->genres.n++] = strdup(src->genres.v[i]);
    }
}

void ui_draw_poster(const char *url, float x, float y, float w, float h, int focused, const char *label)
{
    int iw = 0, ih = 0;
    unsigned tex = image_get(url, &iw, &ih);
    if (focused) gfx_rect_outline(x, y, w, h, 5, THEME_ACCENT);
    if (tex) {
        /* cover-fit */
        float ia = (float)iw / (float)ih, pa = w / h;
        float u0 = 0, v0 = 0, u1 = 1, v1 = 1;
        if (ia > pa) { float k = pa / ia; u0 = (1 - k) / 2; u1 = 1 - u0; }
        else { float k = ia / pa; v0 = (1 - k) / 2; v1 = 1 - v0; }
        gfx_tex_uv(tex, x, y, w, h, u0, v0, u1, v1, RGBA(255, 255, 255, 255));
    } else {
        gfx_rect(x, y, w, h, THEME_POSTER_PH);
        if (label) {
            gfx_clip_push(x + 10, y, w - 20, h);
            font_draw_wrapped(label, x + 12, y + h * 0.4f, FONT_S, THEME_TEXT_DIM, w - 24, 3);
            gfx_clip_pop();
        }
    }
}

void ui_draw_header(const char *title, const char *subtitle)
{
    gfx_gradient_v(0, 0, SNTV_UI_W, 160, RGBA(0, 0, 0, 0xB0), RGBA(0, 0, 0, 0));
    font_draw(title, THEME_MARGIN, 40, FONT_L, THEME_TEXT);
    if (subtitle) font_draw(subtitle, THEME_MARGIN + font_measure(title, FONT_L) + 30, 52, FONT_M, THEME_TEXT_DIM);
}

void ui_draw_spinner(float cx, float cy)
{
    for (int i = 0; i < 8; i++) {
        float a = g_app.time * 6.0f + i * 0.785f;
        float px = cx + 26 * SDL_cosf(a), py = cy + 26 * SDL_sinf(a);
        int alpha = 40 + (i * 215) / 8;
        gfx_rect(px - 5, py - 5, 10, 10, COLOR_ALPHA(THEME_ACCENT, alpha));
    }
}

void ui_draw_button(const char *label, float x, float y, float w, float h, int focused, int accent)
{
    gfx_rect(x, y, w, h, focused ? THEME_ACCENT : accent ? THEME_SURFACE_HI : THEME_SURFACE);
    float tw = font_measure(label, FONT_M);
    font_draw_fit(label, x + (w - tw) / 2 > x + 16 ? x + (w - tw) / 2 : x + 16, y + (h - font_height(FONT_M)) / 2, FONT_M, THEME_TEXT, w - 32);
}

void ui_format_time(double s, char *out, size_t n)
{
    int t = (int)(s < 0 ? 0 : s);
    if (t >= 3600) snprintf(out, n, "%d:%02d:%02d", t / 3600, (t / 60) % 60, t % 60);
    else snprintf(out, n, "%d:%02d", t / 60, t % 60);
}

/* ---- lifecycle ------------------------------------------------------------- */

void app_init(void)
{
    memset(&g_app, 0, sizeof g_app);
    const char *k = store_get_str("authKey", NULL);
    const char *e = store_get_str("email", NULL);
    const char *srv = store_get_str("streamingServer", NULL);
    g_app.auth_key = k ? strdup(k) : NULL;
    g_app.user_email = e ? strdup(e) : NULL;
    g_app.streaming_server = srv ? strdup(srv) : NULL;
    app_load_addons();
    app_load_library();
    app_push(screen_home_create());
}

void app_shutdown(void)
{
    while (g_app.depth) { Screen *s = g_app.stack[--g_app.depth]; if (s->destroy) s->destroy(s); }
    free_addons();
    library_items_free(g_app.library, g_app.n_library);
    free(g_app.auth_key); free(g_app.user_email); free(g_app.streaming_server);
}
