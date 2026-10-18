#include "app.h"
#include "http.h"
#include "log.h"
#include "store.h"
#include "image.h"
#include "ui/font.h"
#include "ui/theme.h"
#include "ui/widgets.h"
#include "cJSON.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>

App g_app;

void app_redraw(void) { g_app.need_redraw = 1; }

float ui_ease(float from, float to, float *vel_unused, float dt, float speed)
{
    (void)vel_unused;
    if (g_app.fx_light) return to;              /* light mode: no interpolation */
    float k = 1 - SDL_powf(0.5f, dt * speed);   /* half-life based, frame-rate independent */
    float v = from + (to - from) * k;
    if (SDL_fabsf(to - v) < 0.3f) v = to; else app_redraw();
    return v;
}

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

void app_pip_enter(void)
{
    Screen *s = app_top();
    if (!s || strcmp(s->name, "player") != 0 || g_app.depth < 2) return;
    if (g_app.pip) app_pip_close();
    g_app.depth--;               /* detach, keep alive */
    g_app.pip = s;
    player_set_pip(s, 1);
    Screen *top = app_top();
    if (top && strcmp(top->name, "streams") == 0 && g_app.depth > 1) app_pop();   /* land on the episodes, not the source list */
    else if (top && top->resume) top->resume(top);
    app_toast("Playing in the corner.  Play/Pause: full screen  \xC2\xB7  Stop: close");
    app_redraw();
}

void app_pip_restore(void)
{
    Screen *s = g_app.pip;
    if (!s) return;
    g_app.pip = NULL;
    g_app.toast_until = 0;   /* the "playing in the corner" note no longer applies */
    player_set_pip(s, 0);
    app_push(s);
    app_redraw();
}

void app_pip_close(void)
{
    Screen *s = g_app.pip;
    if (!s) return;
    g_app.pip = NULL;
    g_app.toast_until = 0;
    player_set_pip(s, 0);
    if (s->destroy) s->destroy(s);
    app_redraw();
}

int app_pip_claim(const char *video_id)
{
    if (!g_app.pip) return 0;
    const char *cur = player_video_id(g_app.pip);
    if (video_id && cur && strcmp(cur, video_id) == 0) { app_pip_restore(); return 1; }
    app_pip_close();   /* one video plane: the new video takes it */
    return 0;
}

void app_toast(const char *fmt, ...)
{
    app_redraw();
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
    /* slide up + fade in, fade out at the end */
    float age = 3.5f - (g_app.toast_until - g_app.time);
    float in = age < 0.25f ? age / 0.25f : 1.0f, out = g_app.toast_until - g_app.time < 0.3f ? (g_app.toast_until - g_app.time) / 0.3f : 1.0f;
    float k = g_app.fx_light ? 1.0f : (in < out ? in : out);
    int fs = FONT_S | FONT_B;
    float w = font_measure(g_app.toast, fs) + 100, h = 72;
    float x = (SNTV_UI_W - w) / 2, y = SNTV_UI_H - 150 + (1 - k) * 30;
    if (!g_app.fx_light) gfx_shadow(x, y + 8, w, h, h / 2, 30, RGBA(0, 0, 0, (int)(0x90 * k)));
    gfx_rrect(x, y, w, h, h / 2, RGBA(0x24, 0x20, 0x44, (int)(0xF4 * k)));
    gfx_rrect_ring(x, y, w, h, h / 2, 1.5f, RGBA(0xFF, 0xFF, 0xFF, (int)(0x22 * k)));
    gfx_circle(x + 40, y + h / 2, 7, COLOR_ALPHA(THEME_ACCENT2, (int)(255 * k)));
    font_draw(g_app.toast, x + 64, y + (h - font_height(fs)) / 2, fs, COLOR_ALPHA(THEME_TEXT, (int)(255 * k)));
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
    app_redraw();
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

int app_move_addon(int i, int dir)
{
    int j = i + dir;
    if (i < 0 || j < 0 || i >= g_app.n_addons || j >= g_app.n_addons) return 0;
    Addon t = g_app.addons[i]; g_app.addons[i] = g_app.addons[j]; g_app.addons[j] = t;
    g_app.addons_version++;
    if (g_app.auth_key) push_collection_to_account(); else persist_local_addon_urls();
    return 1;
}

int app_title_watched(const char *id)
{
    LibraryItem *li = app_library_find(id);
    return li && !li->removed && (li->flagged_watched || li->times_watched > 0) && li->time_offset == 0;
}

/* ---- saved accounts, parental PIN -------------------------------------------- */

static void accounts_remember(const char *email, const char *key)
{
    if (!email || !key) return;
    cJSON *arr = store_get_json("accounts");
    cJSON *copy = cJSON_CreateArray();
    if (cJSON_IsArray(arr)) {
        cJSON *it;
        cJSON_ArrayForEach(it, arr) {
            const cJSON *e = cJSON_GetObjectItemCaseSensitive(it, "email");
            if (cJSON_IsString(e) && strcmp(e->valuestring, email) == 0) continue;   /* replaced below */
            cJSON_AddItemToArray(copy, cJSON_Duplicate(it, 1));
        }
    }
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "email", email);
    cJSON_AddStringToObject(o, "key", key);
    cJSON_AddItemToArray(copy, o);
    store_set_json("accounts", copy);
    store_save();
}

int app_saved_accounts(char emails[][128], int max)
{
    cJSON *arr = store_get_json("accounts");
    int n = 0;
    if (!cJSON_IsArray(arr)) return 0;
    cJSON *it;
    cJSON_ArrayForEach(it, arr) {
        const cJSON *e = cJSON_GetObjectItemCaseSensitive(it, "email");
        if (!cJSON_IsString(e) || n >= max) continue;
        if (g_app.user_email && strcmp(e->valuestring, g_app.user_email) == 0) continue;
        snprintf(emails[n++], 128, "%s", e->valuestring);
    }
    return n;
}

static cJSON *saved_account_at(int index)
{
    cJSON *arr = store_get_json("accounts");
    if (!cJSON_IsArray(arr)) return NULL;
    int n = 0;
    cJSON *it;
    cJSON_ArrayForEach(it, arr) {
        const cJSON *e = cJSON_GetObjectItemCaseSensitive(it, "email");
        if (!cJSON_IsString(e)) continue;
        if (g_app.user_email && strcmp(e->valuestring, g_app.user_email) == 0) continue;
        if (n++ == index) return it;
    }
    return NULL;
}

void app_switch_account(int index)
{
    cJSON *it = saved_account_at(index);
    if (!it) return;
    const cJSON *e = cJSON_GetObjectItemCaseSensitive(it, "email"), *k = cJSON_GetObjectItemCaseSensitive(it, "key");
    if (!cJSON_IsString(e) || !cJSON_IsString(k)) return;
    free(g_app.auth_key); free(g_app.user_email);
    g_app.auth_key = strdup(k->valuestring);
    g_app.user_email = strdup(e->valuestring);
    store_set_str("authKey", g_app.auth_key);
    store_set_str("email", g_app.user_email);
    store_save();
    library_items_free(g_app.library, g_app.n_library);
    g_app.library = NULL; g_app.n_library = 0;
    g_app.library_version++;
    store_write_file("library.json", "{\"result\":[]}", 13);
    app_load_addons();
    app_load_library();
    app_toast("Switched to %s", g_app.user_email);
}

void app_forget_account(int index)
{
    cJSON *it = saved_account_at(index);
    if (!it) return;
    cJSON *arr = store_get_json("accounts");
    cJSON_DetachItemViaPointer(arr, it);
    cJSON_Delete(it);
    store_save();
}

void app_set_parental_pin(const char *pin)
{
    snprintf(g_app.parental_pin, sizeof g_app.parental_pin, "%.7s", pin ? pin : "");
    store_set_str("parentalPin", g_app.parental_pin[0] ? g_app.parental_pin : NULL);
    if (!g_app.parental_pin[0]) { g_app.pin_settings = 0; store_set_int("pinSettings", 0); }
    store_save();
}

static void settings_after_pin(int ok, void *user) { (void)user; if (ok) app_push(screen_settings_create()); }

void app_open_settings(void)
{
    if (g_app.parental_pin[0] && g_app.pin_settings) app_push(screen_pin_create("Settings are locked", 0, settings_after_pin, NULL));
    else app_push(screen_settings_create());
}

/* ---- home rows --------------------------------------------------------------- */

enum { HR_MAX = 160 };
static struct { char key[200]; int hidden; } g_hr[HR_MAX];
static int g_n_hr = -1;

static void home_rows_load(void)
{
    g_n_hr = 0;
    const char *s = store_get_str("homeRows", "");
    while (*s && g_n_hr < HR_MAX) {
        const char *e = strchr(s, '\n');
        size_t len = e ? (size_t)(e - s) : strlen(s);
        char line[232];
        snprintf(line, sizeof line, "%.*s", (int)(len < 231 ? len : 231), s);
        char *tab = strchr(line, '\t');
        if (tab) { *tab = 0; g_hr[g_n_hr].hidden = tab[1] == '1'; } else g_hr[g_n_hr].hidden = 0;
        snprintf(g_hr[g_n_hr].key, sizeof g_hr[g_n_hr].key, "%.199s", line);
        if (line[0]) g_n_hr++;
        s = e ? e + 1 : s + len;
    }
}

static void home_rows_save(void)
{
    char *buf = malloc((size_t)HR_MAX * 232 + 1);
    size_t o = 0;
    for (int i = 0; i < g_n_hr; i++) o += (size_t)snprintf(buf + o, 232, "%s\t%d\n", g_hr[i].key, g_hr[i].hidden);
    store_set_str("homeRows", buf);
    store_save();
    free(buf);
    g_app.home_rows_version++;
}

static int hr_find(const char *key) { for (int i = 0; i < g_n_hr; i++) if (strcmp(g_hr[i].key, key) == 0) return i; return -1; }

int app_home_rows(HomeRowDef *out, int max)
{
    if (g_n_hr < 0) home_rows_load();
    HomeRowDef *all = calloc((size_t)max, sizeof *all);
    int n = 0;
    if (n < max) { snprintf(all[n].key, sizeof all[n].key, "continue"); snprintf(all[n].title, sizeof all[n].title, "Continue Watching"); all[n].kind = 0; n++; }
    if (n < max) { snprintf(all[n].key, sizeof all[n].key, "library"); snprintf(all[n].title, sizeof all[n].title, "My Library"); all[n].kind = 1; n++; }
    for (int a = 0; a < g_app.n_addons; a++) {
        const Addon *ad = &g_app.addons[a];
        if (g_app.hide_adult && ad->manifest.bh_adult) continue;
        for (int c = 0; c < ad->manifest.n_catalogs && n < max; c++) {
            const AddonCatalog *cat = &ad->manifest.catalogs[c];
            if (addon_catalog_has_required_extra(cat)) continue;
            const char *type = strcmp(cat->type, "movie") == 0 ? "Movies" : strcmp(cat->type, "series") == 0 ? "Series" : cat->type;
            snprintf(all[n].key, sizeof all[n].key, "catalog:%s:%s:%s", ad->manifest.id ? ad->manifest.id : ad->transport_url, cat->type, cat->id);
            snprintf(all[n].title, sizeof all[n].title, "%s %s  \xC2\xB7  %s", cat->name, type, ad->manifest.name);
            all[n].kind = 2; all[n].addon = a; all[n].catalog = c;
            n++;
        }
    }
    /* saved order first, then anything new in its natural place */
    int k = 0;
    for (int i = 0; i < g_n_hr && k < n; i++) {
        for (int j = 0; j < n; j++) if (all[j].key[0] && strcmp(all[j].key, g_hr[i].key) == 0) { out[k] = all[j]; out[k].hidden = g_hr[i].hidden; k++; all[j].key[0] = 0; break; }
    }
    for (int j = 0; j < n; j++) if (all[j].key[0]) { out[k] = all[j]; out[k].hidden = 0; k++; }
    free(all);
    return k;
}

static void hr_sync(void)
{
    /* make the saved list the complete current one, in the order shown */
    HomeRowDef rows[HR_MAX];
    int n = app_home_rows(rows, HR_MAX);
    g_n_hr = n;
    for (int i = 0; i < n; i++) { snprintf(g_hr[i].key, sizeof g_hr[i].key, "%.199s", rows[i].key); g_hr[i].hidden = rows[i].hidden; }
}

void app_home_row_move(const char *key, int dir)
{
    hr_sync();
    int i = hr_find(key), j = i + dir;
    if (i < 0 || j < 0 || j >= g_n_hr) return;
    char tk[200]; int th;
    memcpy(tk, g_hr[i].key, sizeof tk); th = g_hr[i].hidden;
    memcpy(g_hr[i].key, g_hr[j].key, sizeof tk); g_hr[i].hidden = g_hr[j].hidden;
    memcpy(g_hr[j].key, tk, sizeof tk); g_hr[j].hidden = th;
    home_rows_save();
}

void app_home_row_toggle(const char *key)
{
    hr_sync();
    int i = hr_find(key);
    if (i < 0) return;
    g_hr[i].hidden = !g_hr[i].hidden;
    home_rows_save();
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
    accounts_remember(email, key);
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
    app_redraw();
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

/* ---- catalog disk cache ----------------------------------------------------- */

static void cache_name(const char *url, char *out, size_t n)
{
    unsigned long long h = 1469598103934665603ull;
    for (const char *p = url; *p; p++) h = (h ^ (unsigned char)*p) * 1099511628211ull;
    snprintf(out, n, "cache/%016llx.json", h);
}

char *app_cache_get(const char *url, long max_age)
{
    char name[64], path[1200];
    cache_name(url, name, sizeof name);
    snprintf(path, sizeof path, "%s/%s", store_dir(), name);
    struct stat st;
    if (stat(path, &st) != 0) return NULL;
    if (max_age > 0 && time(NULL) - st.st_mtime > max_age) return NULL;
    return store_read_file(name);
}

void app_cache_put(const char *url, const char *body, size_t len)
{
    char name[64], dir[1200];
    snprintf(dir, sizeof dir, "%s/cache", store_dir());
    mkdir(dir, 0755);
    cache_name(url, name, sizeof name);
    store_write_file(name, body, len);
}

/* ---- preferences & perf ----------------------------------------------------- */

/* ---- TV profile ------------------------------------------------------------- */

static void tv_profile_load(void)
{
    char rel[200] = "";
    FILE *f = fopen("/etc/starfish-release", "r");
    if (f) { if (!fgets(rel, sizeof rel, f)) rel[0] = 0; fclose(f); }
    quality_tv_init(&g_app.tv, rel);
    const cJSON *caps = store_get_json("tvCaps");
    for (int c = 0; c < TVCAP_COUNT; c++) {
        const cJSON *e = cJSON_GetObjectItemCaseSensitive(caps, TVCAP_NAMES[c]);
        const cJSON *ok = cJSON_GetArrayItem(e, 0), *fail = cJSON_GetArrayItem(e, 1);
        if (cJSON_IsNumber(ok)) g_app.tv.ok[c] = ok->valueint;
        if (cJSON_IsNumber(fail)) g_app.tv.fail[c] = fail->valueint;
    }
    LOGI("tv profile: webOS %d; dts %s, truehd %s, av1 %s, dv %s", g_app.tv.webos_major,
         quality_cap_ok(&g_app.tv, TVCAP_DTS) ? "ok" : "avoid", quality_cap_ok(&g_app.tv, TVCAP_TRUEHD) ? "ok" : "avoid",
         quality_cap_ok(&g_app.tv, TVCAP_AV1) ? "ok" : "avoid", quality_cap_ok(&g_app.tv, TVCAP_DV) ? "ok" : "avoid");
}

void app_tv_learn(unsigned needs, int worked)
{
    if (!needs) return;
    quality_tv_learn(&g_app.tv, needs, worked);
    cJSON *caps = cJSON_CreateObject();
    for (int c = 0; c < TVCAP_COUNT; c++) {
        cJSON *pair = cJSON_AddArrayToObject(caps, TVCAP_NAMES[c]);
        cJSON_AddItemToArray(pair, cJSON_CreateNumber(g_app.tv.ok[c]));
        cJSON_AddItemToArray(pair, cJSON_CreateNumber(g_app.tv.fail[c]));
    }
    store_set_json("tvCaps", caps);
    store_save();
    char what[128] = "";
    for (int c = 0; c < TVCAP_COUNT; c++) if (needs & (1u << c)) snprintf(what + strlen(what), sizeof what - strlen(what), "%s%s", what[0] ? "," : "", TVCAP_NAMES[c]);
    LOGI("tv profile: %s %s", what, worked ? "played fine" : "failed");
}

static void load_prefs(void)
{
    g_app.fx_setting = (FxLevel)store_get_int("fx", FX_AUTO);
    g_app.fx_light = g_app.fx_setting == FX_LIGHT;
    g_app.spoiler_blur = store_get_int("spoilerBlur", 1);
    g_app.subs_size = store_get_int("subsSize", 1);
    g_app.subs_style = store_get_int("subsStyle", 0);
    if (g_app.subs_style < 0 || g_app.subs_style > 3) g_app.subs_style = 0;
    g_app.autoplay_next = store_get_int("autoplayNext", 1);
    g_app.skip_mode = store_get_int("skipMode", 0);
    if (g_app.skip_mode < 0 || g_app.skip_mode > 2) g_app.skip_mode = 0;
    g_app.subs_always = store_get_int("subsAlways", 0);
    g_app.clock_mode = store_get_int("clockMode", 0);
    if (g_app.clock_mode < 0 || g_app.clock_mode > 2) g_app.clock_mode = 0;
    g_app.oled = store_get_int("oled", 0);
    g_app.ambient_minutes = store_get_int("ambientMinutes", 5);
    g_app.quality_mode = store_get_int("qualityMode", QUAL_ADDON_ORDER);
    if (g_app.quality_mode < 0 || g_app.quality_mode >= QUAL_MODE_COUNT) g_app.quality_mode = QUAL_ADDON_ORDER;
    g_app.res_cap = store_get_int("resCap", 2160);
    g_app.auto_recommended = store_get_int("autoRecommended", 0);
    g_app.preflight = store_get_int("preflight", 1);
    g_app.live_scrub = store_get_int("liveScrub", 1);
    g_app.home_style = store_get_int("homeStyle", HOME_CINEMATIC);
    g_app.hide_watched = store_get_int("hideWatched", 0);
    snprintf(g_app.parental_pin, sizeof g_app.parental_pin, "%.7s", store_get_str("parentalPin", ""));
    g_app.pin_settings = store_get_int("pinSettings", 0);
    g_app.hide_adult = store_get_int("hideAdult", 0);
    { const char *t = store_get_str("thumbServer", NULL); free(g_app.thumb_server); g_app.thumb_server = t && *t ? strdup(t) : NULL; }
    if (g_app.home_style < 0 || g_app.home_style >= HOME_STYLE_COUNT) g_app.home_style = HOME_CINEMATIC;
    tv_profile_load();
    snprintf(g_app.pref_audio, sizeof g_app.pref_audio, "%s", store_get_str("prefAudio", ""));
    snprintf(g_app.pref_subs, sizeof g_app.pref_subs, "%s", store_get_str("prefSubs", ""));
}

void app_save_prefs(void)
{
    store_set_int("fx", g_app.fx_setting);
    store_set_int("spoilerBlur", g_app.spoiler_blur);
    store_set_int("subsSize", g_app.subs_size);
    store_set_int("subsStyle", g_app.subs_style);
    store_set_int("autoplayNext", g_app.autoplay_next);
    store_set_int("skipMode", g_app.skip_mode);
    store_set_int("subsAlways", g_app.subs_always);
    store_set_int("clockMode", g_app.clock_mode);
    store_set_int("oled", g_app.oled);
    store_set_int("ambientMinutes", g_app.ambient_minutes);
    store_set_int("qualityMode", g_app.quality_mode);
    store_set_int("resCap", g_app.res_cap);
    store_set_int("autoRecommended", g_app.auto_recommended);
    store_set_int("preflight", g_app.preflight);
    store_set_int("liveScrub", g_app.live_scrub);
    store_set_int("homeStyle", g_app.home_style);
    store_set_int("hideWatched", g_app.hide_watched);
    store_set_int("pinSettings", g_app.pin_settings);
    store_set_int("hideAdult", g_app.hide_adult);
    store_set_str("thumbServer", g_app.thumb_server && *g_app.thumb_server ? g_app.thumb_server : NULL);
    store_set_str("prefAudio", g_app.pref_audio);
    store_set_str("prefSubs", g_app.pref_subs);
    store_save();
    g_app.fx_light = g_app.fx_setting == FX_LIGHT || (g_app.fx_setting == FX_AUTO && store_get_int("fxAutoLight", 0));
}

/* Auto effects: average the first 240 busy frames; if the TV can't hold ~40 fps
 * while scrolling posters, drop to light effects and remember it. */
void app_note_frame(float ms)
{
    if (g_app.fx_setting != FX_AUTO || g_app.frames_measured >= 240) return;
    if (g_app.frames_measured == 0 && g_app.time < 3.0f) return;   /* skip startup */
    g_app.frames_measured++;
    g_app.frame_ms_sum += ms;
    if (ms > g_app.frame_ms_worst) g_app.frame_ms_worst = ms;
    if (g_app.frames_measured == 240) {
        float avg = g_app.frame_ms_sum / 240.0f;
        int light = avg > 25.0f;
        LOGI("perf: avg %.1f ms, worst %.1f ms -> %s effects", avg, g_app.frame_ms_worst, light ? "light" : "full");
        if (light != store_get_int("fxAutoLight", 0)) { store_set_int("fxAutoLight", light); store_save(); }
        g_app.fx_light = light;
    }
}

static struct { char *meta_id; MetaVideo *videos; int n; } g_videos;

void app_remember_videos(const char *meta_id, const MetaVideo *videos, int n)
{
    free(g_videos.meta_id);
    for (int i = 0; i < g_videos.n; i++) { free(g_videos.videos[i].id); free(g_videos.videos[i].title); free(g_videos.videos[i].thumbnail); }
    free(g_videos.videos);
    g_videos.meta_id = strdup(meta_id);
    g_videos.videos = calloc((size_t)n + 1, sizeof(MetaVideo));
    g_videos.n = n;
    for (int i = 0; i < n; i++) {
        g_videos.videos[i].id = strdup(videos[i].id);
        g_videos.videos[i].title = videos[i].title ? strdup(videos[i].title) : NULL;
        g_videos.videos[i].thumbnail = videos[i].thumbnail ? strdup(videos[i].thumbnail) : NULL;
        g_videos.videos[i].season = videos[i].season;
        g_videos.videos[i].episode = videos[i].episode;
    }
}

const char *app_video_thumbnail(const char *meta_id, const char *video_id)
{
    if (!g_videos.meta_id || !video_id || strcmp(g_videos.meta_id, meta_id) != 0) return NULL;
    for (int i = 0; i < g_videos.n; i++) if (strcmp(g_videos.videos[i].id, video_id) == 0) return g_videos.videos[i].thumbnail;
    return NULL;
}

/* Next = the following entry in release order within regular seasons (specials skipped). */
const MetaVideo *app_videos(const char *meta_id, int *n)
{
    *n = 0;
    if (!g_videos.meta_id || !meta_id || strcmp(g_videos.meta_id, meta_id) != 0) return NULL;
    *n = g_videos.n;
    return g_videos.videos;
}

int app_adjacent_video(const char *meta_id, const char *video_id, int dir, char *out_id, size_t id_n, char *out_title, size_t title_n)
{
    if (!g_videos.meta_id || !video_id || strcmp(g_videos.meta_id, meta_id) != 0) return 0;
    int cur = -1;
    for (int i = 0; i < g_videos.n; i++) if (strcmp(g_videos.videos[i].id, video_id) == 0) { cur = i; break; }
    if (cur < 0) return 0;
    const MetaVideo *c = &g_videos.videos[cur];
    const MetaVideo *best = NULL;
    for (int i = 0; i < g_videos.n; i++) {
        const MetaVideo *v = &g_videos.videos[i];
        if (v == c || v->season == 0) continue;   /* specials never count */
        int after = v->season > c->season || (v->season == c->season && v->episode > c->episode);
        int before = v->season < c->season || (v->season == c->season && v->episode < c->episode);
        if (c->season < 0) { after = i > cur; before = i < cur; }   /* no season info: list order */
        if (dir > 0 ? !after : !before) continue;
        int closer = !best || (dir > 0 ? (v->season < best->season || (v->season == best->season && v->episode < best->episode))
                                       : (v->season > best->season || (v->season == best->season && v->episode > best->episode)));
        if (closer) best = v;
    }
    if (!best) return 0;
    snprintf(out_id, id_n, "%s", best->id);
    if (best->season >= 0 && best->episode >= 0) snprintf(out_title, title_n, "S%d E%d - %s", best->season, best->episode, best->title ? best->title : "");
    else snprintf(out_title, title_n, "%s", best->title ? best->title : (dir > 0 ? "Next" : "Previous"));
    return 1;
}

int app_next_video(const char *meta_id, const char *video_id, char *next_id, size_t id_n, char *next_title, size_t title_n)
{
    return app_adjacent_video(meta_id, video_id, 1, next_id, id_n, next_title, title_n);
}

static int imdb_id(const char *id) { return id && id[0] == 't' && id[1] == 't' && id[2] >= '0' && id[2] <= '9'; }

/* ---- screensaver pool ------------------------------------------------------- */

enum { AMBIENT_MAX = 48 };
static AmbientItem g_ambient[AMBIENT_MAX];
static int g_n_ambient, g_ambient_next;

void app_ambient_offer(const MetaPreview *m)
{
    char buf[512];
    const char *bg = app_art_background(m, buf, sizeof buf);
    if (!bg || !m->id || !m->name) return;
    for (int i = 0; i < g_n_ambient; i++) if (strcmp(g_ambient[i].id, m->id) == 0) return;
    AmbientItem *a;
    if (g_n_ambient < AMBIENT_MAX) a = &g_ambient[g_n_ambient++];
    else { a = &g_ambient[g_ambient_next]; g_ambient_next = (g_ambient_next + 1) % AMBIENT_MAX; }   /* rotate the pool */
    snprintf(a->id, sizeof a->id, "%s", m->id);
    snprintf(a->name, sizeof a->name, "%s", m->name);
    snprintf(a->background, sizeof a->background, "%s", bg);
}

int app_ambient_pool(const AmbientItem **out)
{
    *out = g_ambient;
    return g_n_ambient;
}

const char *app_art_background(const MetaPreview *p, char *buf, size_t n)
{
    if (p->background && *p->background && !(imdb_id(p->id) && image_failed(p->background))) return p->background;
    if (imdb_id(p->id)) { snprintf(buf, n, "https://images.metahub.space/background/medium/%s/img", p->id); return buf; }
    return p->poster;
}

const char *app_art_logo(const MetaPreview *p, char *buf, size_t n)
{
    if (p->logo && *p->logo) return p->logo;
    if (imdb_id(p->id)) { snprintf(buf, n, "https://images.metahub.space/logo/medium/%s/img", p->id); return buf; }
    return NULL;
}

const char *app_art_poster(const MetaPreview *p, char *buf, size_t n)
{
    /* a poster the TV cannot fetch or decode (WebP, dead link): metahub has a JPEG for IMDb ids */
    if (p->poster && *p->poster && !(imdb_id(p->id) && image_failed(p->poster))) return p->poster;
    if (imdb_id(p->id)) { snprintf(buf, n, "https://images.metahub.space/poster/medium/%s/img", p->id); return buf; }
    return NULL;
}

void play_candidates_free(PlayCandidate *c, int n)
{
    for (int i = 0; i < n; i++) { free(c[i].url); free(c[i].label); free(c[i].binge_group); free(c[i].filename); }
    free(c);
}

/* ---- shared widgets -------------------------------------------------------- */

const char *const HOME_STYLE_NAMES[HOME_STYLE_COUNT] = { "Cinematic", "Showcase", "Compact" };

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
    if (src->cast.n) {
        dst->cast.v = calloc((size_t)src->cast.n + 1, sizeof(char *));
        for (int i = 0; i < src->cast.n; i++) dst->cast.v[dst->cast.n++] = strdup(src->cast.v[i]);
    }
}

void ui_draw_thumb(const char *url, float x, float y, float w, float h, int blur, Color placeholder)
{
    (void)placeholder;
    ui_card(url, x, y, w, h, 0, 0, NULL, blur ? CARD_BLUR : CARD_MIP);
}

void ui_draw_poster(const char *url, float x, float y, float w, float h, int focused, const char *label)
{
    ui_card(url, x, y, w, h, focused ? 1.0f : 0.0f, 0, label, 0);
}

void ui_draw_header(const char *title, const char *subtitle)
{
    font_draw(title, THEME_MARGIN, 52, FONT_L | FONT_B, THEME_TEXT);
    if (subtitle) font_draw_fit(subtitle, THEME_MARGIN + font_measure(title, FONT_L | FONT_B) + 28, 64, FONT_S, THEME_TEXT_MUTED,
                                SNTV_UI_W - 2 * THEME_MARGIN - font_measure(title, FONT_L | FONT_B) - 28);
}

void ui_draw_spinner(float cx, float cy) { ui_spinner(cx, cy, 24); }

void ui_draw_button(const char *label, float x, float y, float w, float h, int focused, int accent)
{
    if (focused && !g_app.fx_light) gfx_shadow(x, y + 6, w, h, h / 2, 22, RGBA(0, 0, 0, 0x70));
    gfx_rrect(x, y, w, h, h / 2, focused ? THEME_TEXT : accent ? RGBA(0xFF, 0xFF, 0xFF, 0x2C) : THEME_SURFACE);
    int fs = FONT_S | FONT_B;
    float tw = font_measure(label, fs);
    float tx = x + (w - tw) / 2;
    if (tx < x + 24) tx = x + 24;
    font_draw_fit(label, tx, y + (h - font_height(fs)) / 2, fs, focused ? THEME_ON_LIGHT : THEME_TEXT, w - 48);
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
    load_prefs();
    g_app.fx_light = g_app.fx_setting == FX_LIGHT || (g_app.fx_setting == FX_AUTO && store_get_int("fxAutoLight", 0));
    app_load_addons();
    app_load_library();
    app_push(screen_home_create());
    if (!store_get_int("setupDone", 0) && !getenv("SNTV_SKIP_SETUP")) app_push(screen_welcome_create());
}

void app_shutdown(void)
{
    app_pip_close();
    while (g_app.depth) { Screen *s = g_app.stack[--g_app.depth]; if (s->destroy) s->destroy(s); }
    free_addons();
    library_items_free(g_app.library, g_app.n_library);
    free(g_app.auth_key); free(g_app.user_email); free(g_app.streaming_server);
}
