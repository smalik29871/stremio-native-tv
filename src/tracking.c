#include "tracking.h"
#include "app.h"
#include "http.h"
#include "log.h"
#include "store.h"
#include "platform/platform.h"
#include "../vendor/cJSON.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

const char *const TRACK_NAMES[TRACK_SERVICES] = { "Trakt", "SimKL" };
const char *const TROW_NAMES[TROW_COUNT] = { "Trakt watchlist", "Recommended on Trakt" };

static const char *env_or(const char *env, const char *def) { const char *e = getenv(env); return e && *e ? e : def; }
static const char *trakt_api(void) { return env_or("SNTV_TRAKT_API", "https://api.trakt.tv"); }
static const char *simkl_api(void) { return env_or("SNTV_SIMKL_API", "https://api.simkl.com"); }
/* SimKL wants every call to name the app and its version */
#define SIMKL_APP "&app-name=stremio-native-tv&app-version=" SNTV_APP_VERSION
static char g_last_err[200];
const char *tracking_last_error(void) { return g_last_err; }
static const char *mdb_api(void) { return env_or("SNTV_MDBLIST_API", "https://mdblist.com/api"); }

static const char *built_in(int svc, int secret)
{
#ifdef SNTV_TRAKT_CLIENT_ID
    if (svc == TRACK_TRAKT && !secret) return SNTV_TRAKT_CLIENT_ID;
#endif
#ifdef SNTV_TRAKT_CLIENT_SECRET
    if (svc == TRACK_TRAKT && secret) return SNTV_TRAKT_CLIENT_SECRET;
#endif
#ifdef SNTV_SIMKL_CLIENT_ID
    if (svc == TRACK_SIMKL && !secret) return SNTV_SIMKL_CLIENT_ID;
#endif
    (void)svc; (void)secret;
    return "";
}

static const char *K_ID[] = { "traktClientId", "simklClientId" };
static const char *K_TOKEN[] = { "traktToken", "simklToken" };

const char *tracking_app_id(int svc)
{
    const char *v = store_get_str(K_ID[svc], "");
    return v && *v ? v : built_in(svc, 0);
}
static const char *trakt_secret(void)
{
    const char *v = store_get_str("traktClientSecret", "");
    return v && *v ? v : built_in(TRACK_TRAKT, 1);
}
static const char *token(int svc) { const char *t = store_get_str(K_TOKEN[svc], ""); return t ? t : ""; }

int tracking_configured(int svc) { return tracking_app_id(svc)[0] != 0; }   /* Trakt apps made today have no secret (PKCE) */
int tracking_connected(int svc) { return tracking_configured(svc) && token(svc)[0]; }

void tracking_set_app(int svc, const char *id, const char *secret)
{
    store_set_str(K_ID[svc], id && *id ? id : NULL);
    if (svc == TRACK_TRAKT) store_set_str("traktClientSecret", secret && *secret ? secret : NULL);
    store_save();
}

static int g_version;
int tracking_version(void) { return g_version; }

/* ---- requests ---- */
static void headers_for(int svc, char *out, size_t n)
{
    if (svc == TRACK_TRAKT) snprintf(out, n, "trakt-api-version: 2\ntrakt-api-key: %s", tracking_app_id(svc));
    else snprintf(out, n, "simkl-api-key: %s", tracking_app_id(svc));
}

static void request(int svc, const char *url, const char *post, HttpCallback cb, void *user)
{
    char hdr[300];
    headers_for(svc, hdr, sizeof hdr);
    const char *t = token(svc);
    HttpOptions o = { .post_json = post, .bearer = t[0] ? t : NULL, .headers = hdr, .timeout_s = 20, .deliver = post != NULL };
    http_request_async_ex(url, &o, cb, user);
}

/* ---- signing in ---- */
typedef struct { int active; char device[128], user_code[32]; double next, until; int interval; unsigned inflight; } SignIn;
static SignIn g_sign[TRACK_SERVICES];

int tracking_pending(int svc) { return g_sign[svc].active; }

static double now_s(void) { return (double)time(NULL); }

static void on_token(HttpResponse *r, void *user)
{
    int svc = (int)(intptr_t)user;
    SignIn *si = &g_sign[svc];
    si->inflight = 0;
    if (!si->active) return;
    cJSON *j = r->status == 200 ? cJSON_Parse(r->body) : NULL;
    const cJSON *at = cJSON_GetObjectItemCaseSensitive(j, "access_token");
    if (cJSON_IsString(at) && at->valuestring[0]) {
        store_set_str(K_TOKEN[svc], at->valuestring);
        if (svc == TRACK_TRAKT) {
            const cJSON *rt = cJSON_GetObjectItemCaseSensitive(j, "refresh_token"), *ex = cJSON_GetObjectItemCaseSensitive(j, "expires_in");
            if (cJSON_IsString(rt)) store_set_str("traktRefresh", rt->valuestring);
            if (cJSON_IsNumber(ex)) store_set_int("traktExpires", (int)(now_s() + ex->valuedouble));
        }
        store_save();
        si->active = 0;
        g_version++;
        LOGI("tracking: %s connected", TRACK_NAMES[svc]);
        app_toast("%s connected", TRACK_NAMES[svc]);
    } else if (svc == TRACK_TRAKT && (r->status == 404 || r->status == 409 || r->status == 410 || r->status == 418)) {
        si->active = 0;
        app_toast(r->status == 418 ? "Trakt sign-in was declined" : "The Trakt code expired; try again");
    } else if (svc == TRACK_TRAKT && r->status == 429) si->interval += 1;
    cJSON_Delete(j);
    app_redraw();
}

static void poll(int svc)
{
    SignIn *si = &g_sign[svc];
    char url[400];
    if (svc == TRACK_TRAKT) {
        snprintf(url, sizeof url, "%s/oauth/device/token", trakt_api());
        cJSON *b = cJSON_CreateObject();
        cJSON_AddStringToObject(b, "code", si->device);
        cJSON_AddStringToObject(b, "client_id", tracking_app_id(svc));
        if (trakt_secret()[0]) cJSON_AddStringToObject(b, "client_secret", trakt_secret());
        char *body = cJSON_PrintUnformatted(b);
        cJSON_Delete(b);
        char hdr[300]; headers_for(svc, hdr, sizeof hdr);
        HttpOptions o = { .post_json = body, .headers = hdr, .timeout_s = 20 };
        si->inflight = http_request_async_ex(url, &o, on_token, (void *)(intptr_t)svc);
        free(body);
    } else {
        snprintf(url, sizeof url, "%s/oauth/pin/%s?client_id=%s" SIMKL_APP, simkl_api(), si->user_code, tracking_app_id(svc));
        char hdr[300]; headers_for(svc, hdr, sizeof hdr);
        HttpOptions o = { .headers = hdr, .timeout_s = 20 };
        si->inflight = http_request_async_ex(url, &o, on_token, (void *)(intptr_t)svc);
    }
}

int tracking_connect(int svc, char *code, size_t cn, char *url, size_t un)
{
    if (!tracking_configured(svc)) return 0;
    SignIn *si = &g_sign[svc];
    memset(si, 0, sizeof *si);
    HttpResponse r;
    char u[400], hdr[300];
    headers_for(svc, hdr, sizeof hdr);
    if (svc == TRACK_TRAKT) {
        snprintf(u, sizeof u, "%s/oauth/device/code", trakt_api());
        char body[300]; snprintf(body, sizeof body, "{\"client_id\":\"%s\"}", tracking_app_id(svc));
        HttpOptions o = { .post_json = body, .headers = hdr, .timeout_s = 15 };
        http_request_ex(u, &o, &r);
    } else {
        snprintf(u, sizeof u, "%s/oauth/pin?client_id=%s&redirect=http%%3A%%2F%%2Flocalhost" SIMKL_APP, simkl_api(), tracking_app_id(svc));
        HttpOptions o = { .headers = hdr, .timeout_s = 15 };
        http_request_ex(u, &o, &r);
    }
    cJSON *j = r.status == 200 ? cJSON_Parse(r.body) : NULL;
    const cJSON *dc = cJSON_GetObjectItemCaseSensitive(j, "device_code"), *uc = cJSON_GetObjectItemCaseSensitive(j, "user_code");
    const cJSON *vu = cJSON_GetObjectItemCaseSensitive(j, "verification_url"), *iv = cJSON_GetObjectItemCaseSensitive(j, "interval");
    const cJSON *ex = cJSON_GetObjectItemCaseSensitive(j, "expires_in");
    int ok = cJSON_IsString(uc) && (svc != TRACK_TRAKT || cJSON_IsString(dc));
    if (ok) {
        if (cJSON_IsString(dc)) snprintf(si->device, sizeof si->device, "%s", dc->valuestring);
        snprintf(si->user_code, sizeof si->user_code, "%s", uc->valuestring);
        si->interval = cJSON_IsNumber(iv) && iv->valueint > 0 ? iv->valueint : 5;
        si->until = now_s() + (cJSON_IsNumber(ex) ? ex->valuedouble : 600);
        si->next = now_s() + si->interval;
        si->active = 1;
        snprintf(code, cn, "%s", si->user_code);
        snprintf(url, un, "%s", cJSON_IsString(vu) ? vu->valuestring : svc == TRACK_TRAKT ? "https://trakt.tv/activate" : "https://simkl.com/pin");
        LOGI("tracking: %s sign-in code %s at %s", TRACK_NAMES[svc], code, url);
    } else {
        /* what the service said, so the screen can show it */
        const cJSON *m = cJSON_GetObjectItemCaseSensitive(j, "message"), *e = cJSON_GetObjectItemCaseSensitive(j, "error");
        cJSON *jb = j ? NULL : cJSON_Parse(r.body ? r.body : "");
        if (!m && !e && jb) { m = cJSON_GetObjectItemCaseSensitive(jb, "message"); e = cJSON_GetObjectItemCaseSensitive(jb, "error"); }
        if (!r.status) snprintf(g_last_err, sizeof g_last_err, "no connection (%.120s)", r.error);
        else snprintf(g_last_err, sizeof g_last_err, "HTTP %ld%s%.140s", r.status, cJSON_IsString(m) || cJSON_IsString(e) ? ": " : "",
                      cJSON_IsString(m) ? m->valuestring : cJSON_IsString(e) ? e->valuestring : "");
        LOGW("tracking: %s sign-in failed: %s  body: %.300s", TRACK_NAMES[svc], g_last_err, r.body ? r.body : "");
        cJSON_Delete(jb);
    }
    cJSON_Delete(j);
    http_response_free(&r);
    return ok;
}

void tracking_disconnect(int svc)
{
    g_sign[svc].active = 0;
    store_set_str(K_TOKEN[svc], NULL);
    if (svc == TRACK_TRAKT) { store_set_str("traktRefresh", NULL); store_set_int("traktExpires", 0); }
    store_save();
    g_version++;
}

/* ---- Trakt token refresh, rows ---- */
static MetaPreview *g_rows[TROW_COUNT]; static int g_n_rows[TROW_COUNT];
static double g_rows_at = -1; static int g_rows_inflight, g_refreshing;

static void on_refresh(HttpResponse *r, void *user)
{
    (void)user;
    g_refreshing = 0;
    cJSON *j = r->status == 200 ? cJSON_Parse(r->body) : NULL;
    const cJSON *at = cJSON_GetObjectItemCaseSensitive(j, "access_token"), *rt = cJSON_GetObjectItemCaseSensitive(j, "refresh_token");
    const cJSON *ex = cJSON_GetObjectItemCaseSensitive(j, "expires_in");
    if (cJSON_IsString(at)) {
        store_set_str("traktToken", at->valuestring);
        if (cJSON_IsString(rt)) store_set_str("traktRefresh", rt->valuestring);
        store_set_int("traktExpires", (int)(now_s() + (cJSON_IsNumber(ex) ? ex->valuedouble : 86400)));
        store_save();
        LOGI("tracking: Trakt token renewed");
    } else if (r->status == 400 || r->status == 401) { LOGW("tracking: Trakt sign-in no longer valid"); tracking_disconnect(TRACK_TRAKT); }
    cJSON_Delete(j);
}

static void refresh_trakt(void)
{
    const char *rt = store_get_str("traktRefresh", "");
    if (!rt || !*rt || g_refreshing) return;
    cJSON *b = cJSON_CreateObject();
    cJSON_AddStringToObject(b, "refresh_token", rt);
    cJSON_AddStringToObject(b, "client_id", tracking_app_id(TRACK_TRAKT));
    if (trakt_secret()[0]) cJSON_AddStringToObject(b, "client_secret", trakt_secret());
    cJSON_AddStringToObject(b, "redirect_uri", "http://localhost");   /* what the app registers; the device sign-in itself needs none */
    cJSON_AddStringToObject(b, "grant_type", "refresh_token");
    char *body = cJSON_PrintUnformatted(b);
    cJSON_Delete(b);
    char url[300]; snprintf(url, sizeof url, "%s/oauth/token", trakt_api());
    g_refreshing = 1;
    request(TRACK_TRAKT, url, body, on_refresh, NULL);
    free(body);
}

/* {title, year, ids:{imdb}} as a preview; NULL id when Trakt has no IMDb id for it */
static int preview_from(const cJSON *o, const char *type, MetaPreview *m)
{
    const cJSON *ids = cJSON_GetObjectItemCaseSensitive(o, "ids");
    const cJSON *imdb = cJSON_GetObjectItemCaseSensitive(ids, "imdb"), *t = cJSON_GetObjectItemCaseSensitive(o, "title");
    const cJSON *y = cJSON_GetObjectItemCaseSensitive(o, "year");
    if (!cJSON_IsString(imdb) || strncmp(imdb->valuestring, "tt", 2) || !cJSON_IsString(t)) return 0;
    memset(m, 0, sizeof *m);
    m->id = strdup(imdb->valuestring);
    m->type = strdup(type);
    m->name = strdup(t->valuestring);
    if (cJSON_IsNumber(y)) { char b[8]; snprintf(b, sizeof b, "%d", y->valueint); m->release_info = strdup(b); }
    return 1;
}

typedef struct { int row; int part; } RowReq;   /* recommendations come as movies, then shows */

static void on_row(HttpResponse *r, void *user)
{
    RowReq *q = user;
    g_rows_inflight--;
    cJSON *j = r->status == 200 ? cJSON_Parse(r->body) : NULL;
    if (r->status == 401) refresh_trakt();
    if (cJSON_IsArray(j)) {
        int n = cJSON_GetArraySize(j);
        MetaPreview *m = realloc(g_rows[q->row], sizeof *m * (size_t)(g_n_rows[q->row] + n + 1));
        if (m) {
            g_rows[q->row] = m;
            const cJSON *it;
            cJSON_ArrayForEach(it, j) {
                const cJSON *mv = cJSON_GetObjectItemCaseSensitive(it, "movie"), *sh = cJSON_GetObjectItemCaseSensitive(it, "show");
                const cJSON *o = mv ? mv : sh ? sh : it;   /* watchlist items wrap; recommendations do not */
                const char *type = mv ? "movie" : sh ? "series" : q->part ? "series" : "movie";
                if (preview_from(o, type, &m[g_n_rows[q->row]])) g_n_rows[q->row]++;
            }
            g_version++;
        }
    }
    LOGI("tracking: %s has %d titles", TROW_NAMES[q->row], g_n_rows[q->row]);
    cJSON_Delete(j);
    free(q);
    app_redraw();
}

static void fetch_rows(void)
{
    for (int i = 0; i < TROW_COUNT; i++) { meta_previews_free(g_rows[i], g_n_rows[i]); g_rows[i] = NULL; g_n_rows[i] = 0; }
    static const struct { int row, part; const char *path; } R[] = {
        { TROW_WATCHLIST, 0, "/sync/watchlist?sort=added" },
        { TROW_RECOMMENDED, 0, "/recommendations/movies?limit=20&ignore_collected=true" },
        { TROW_RECOMMENDED, 1, "/recommendations/shows?limit=20&ignore_collected=true" } };
    for (size_t i = 0; i < sizeof R / sizeof R[0]; i++) {
        RowReq *q = malloc(sizeof *q);
        q->row = R[i].row; q->part = R[i].part;
        char url[300]; snprintf(url, sizeof url, "%s%s", trakt_api(), R[i].path);
        g_rows_inflight++;
        request(TRACK_TRAKT, url, NULL, on_row, q);
    }
    g_version++;
}

int tracking_row(int which, MetaPreview **out)
{
    *out = NULL;
    if (which < 0 || which >= TROW_COUNT || !g_n_rows[which]) return 0;
    MetaPreview *m = calloc((size_t)g_n_rows[which] + 1, sizeof *m);
    for (int i = 0; i < g_n_rows[which]; i++) meta_preview_copy(&m[i], &g_rows[which][i]);
    *out = m;
    return g_n_rows[which];
}

void tracking_tick(void)
{
    double t = now_s();
    for (int s = 0; s < TRACK_SERVICES; s++) {
        SignIn *si = &g_sign[s];
        if (!si->active || si->inflight || t < si->next) continue;
        if (t > si->until) { si->active = 0; app_toast("The %s code expired; try again", TRACK_NAMES[s]); continue; }
        si->next = t + si->interval;
        poll(s);
    }
    if (!tracking_connected(TRACK_TRAKT)) { if (g_n_rows[0] || g_n_rows[1]) { for (int i = 0; i < TROW_COUNT; i++) { meta_previews_free(g_rows[i], g_n_rows[i]); g_rows[i] = NULL; g_n_rows[i] = 0; } g_version++; } g_rows_at = -1; return; }
    int exp = store_get_int("traktExpires", 0);
    if (exp && t > exp - 86400) refresh_trakt();
    if (!g_rows_inflight && (g_rows_at < 0 || t - g_rows_at > 1800)) { g_rows_at = t; fetch_rows(); }
}

/* ---- playback ---- */
static struct { char vid[160]; int started, simkl_done; double last_pct; } g_play;

/* tt123:2:5 -> imdb, season, episode */
static int split_id(const char *video_id, char *imdb, size_t n, int *season, int *episode)
{
    *season = *episode = -1;
    if (strncmp(video_id, "tt", 2) != 0) return 0;
    const char *c = strchr(video_id, ':');
    snprintf(imdb, n, "%.*s", c ? (int)(c - video_id) : (int)strlen(video_id), video_id);
    if (c && sscanf(c + 1, "%d:%d", season, episode) != 2) *season = *episode = -1;
    return 1;
}

static void on_ignored(HttpResponse *r, void *user)
{
    (void)user;
    if (r->status == 401) refresh_trakt();
    else if (r->status >= 300 && r->status != 409) LOGW("tracking: answered %ld", r->status);
}

static void scrobble(const char *action, const char *imdb, int season, int episode, double pct)
{
    cJSON *b = cJSON_CreateObject();
    if (season >= 0) {
        cJSON_AddStringToObject(cJSON_AddObjectToObject(cJSON_AddObjectToObject(b, "show"), "ids"), "imdb", imdb);
        cJSON *ep = cJSON_AddObjectToObject(b, "episode");
        cJSON_AddNumberToObject(ep, "season", season);
        cJSON_AddNumberToObject(ep, "number", episode);
    } else cJSON_AddStringToObject(cJSON_AddObjectToObject(cJSON_AddObjectToObject(b, "movie"), "ids"), "imdb", imdb);
    cJSON_AddNumberToObject(b, "progress", pct);
    char *body = cJSON_PrintUnformatted(b);
    cJSON_Delete(b);
    char url[300]; snprintf(url, sizeof url, "%s/scrobble/%s", trakt_api(), action);
    LOGI("tracking: Trakt %s %s %.0f%%", action, imdb, pct);
    request(TRACK_TRAKT, url, body, on_ignored, NULL);
    free(body);
}

static void simkl_watched(const char *imdb, int season, int episode)
{
    cJSON *b = cJSON_CreateObject();
    if (season >= 0) {
        cJSON *show = cJSON_CreateObject();
        cJSON_AddStringToObject(cJSON_AddObjectToObject(show, "ids"), "imdb", imdb);
        cJSON *se = cJSON_CreateObject();
        cJSON_AddNumberToObject(se, "number", season);
        cJSON *ep = cJSON_CreateObject();
        cJSON_AddNumberToObject(ep, "number", episode);
        cJSON_AddItemToArray(cJSON_AddArrayToObject(se, "episodes"), ep);
        cJSON_AddItemToArray(cJSON_AddArrayToObject(show, "seasons"), se);
        cJSON_AddItemToArray(cJSON_AddArrayToObject(b, "shows"), show);
    } else {
        cJSON *mv = cJSON_CreateObject();
        cJSON_AddStringToObject(cJSON_AddObjectToObject(mv, "ids"), "imdb", imdb);
        cJSON_AddItemToArray(cJSON_AddArrayToObject(b, "movies"), mv);
    }
    char *body = cJSON_PrintUnformatted(b);
    cJSON_Delete(b);
    char url[400]; snprintf(url, sizeof url, "%s/sync/history?client_id=%s" SIMKL_APP, simkl_api(), tracking_app_id(TRACK_SIMKL));
    LOGI("tracking: SimKL watched %s", imdb);
    request(TRACK_SIMKL, url, body, on_ignored, NULL);
    free(body);
}

void tracking_progress(const MetaPreview *m, const char *video_id, double pos, double dur, int final)
{
    (void)m;
    if (!video_id || dur <= 0) return;
    char imdb[32]; int season, episode;
    if (!split_id(video_id, imdb, sizeof imdb, &season, &episode)) return;
    double pct = pos / dur * 100;
    if (pct > 100) pct = 100;
    if (strcmp(g_play.vid, video_id) != 0) { memset(&g_play, 0, sizeof g_play); snprintf(g_play.vid, sizeof g_play.vid, "%s", video_id); }
    if (tracking_connected(TRACK_TRAKT)) {
        if (final) { if (g_play.started) scrobble(pct >= 80 ? "stop" : "pause", imdb, season, episode, pct); g_play.started = 0; }
        else if (!g_play.started) { scrobble("start", imdb, season, episode, pct); g_play.started = 1; }
    }
    if (pct >= 80 && !g_play.simkl_done && tracking_connected(TRACK_SIMKL)) { g_play.simkl_done = 1; simkl_watched(imdb, season, episode); }
}

/* ---- MDBList ---- */
static const char *mdb_key(void)
{
    const char *v = store_get_str("mdblistKey", "");
    if (v && *v) return v;
#ifdef SNTV_MDBLIST_KEY
    return SNTV_MDBLIST_KEY;
#else
    return "";
#endif
}
int mdblist_enabled(void) { return mdb_key()[0] != 0; }

static struct { char imdb[24]; int state; char line[200]; } g_mdb[8];
static int g_mdb_next;

static void mdb_parse(int slot, const char *json)
{
    cJSON *j = cJSON_Parse(json);
    const cJSON *rs = cJSON_GetObjectItemCaseSensitive(j, "ratings"), *it;
    char *o = g_mdb[slot].line; size_t n = sizeof g_mdb[slot].line, l = 0;
    o[0] = 0;
    static const struct { const char *src, *name, *fmt; } S[] = {
        { "tomatoes", "Rotten Tomatoes", "%.0f%%" }, { "metacritic", "Metacritic", "%.0f" },
        { "letterboxd", "Letterboxd", "%.1f" }, { "tmdb", "TMDB", "%.0f%%" } };
    for (size_t k = 0; k < sizeof S / sizeof S[0]; k++)
        if (cJSON_IsArray(rs)) cJSON_ArrayForEach(it, rs) {
            const cJSON *src = cJSON_GetObjectItemCaseSensitive(it, "source"), *v = cJSON_GetObjectItemCaseSensitive(it, "value");
            if (!cJSON_IsString(src) || strcmp(src->valuestring, S[k].src) || !cJSON_IsNumber(v) || v->valuedouble <= 0) continue;
            char val[16]; snprintf(val, sizeof val, S[k].fmt, v->valuedouble);
            if (l < n) l += (size_t)snprintf(o + l, n - l, "%s%s %s", l ? "  \xC2\xB7  " : "", S[k].name, val);
            break;
        }
    g_mdb[slot].state = o[0] ? 2 : 3;
    cJSON_Delete(j);
}

static void on_mdb(HttpResponse *r, void *user)
{
    int slot = (int)(intptr_t)user;
    if (r->status == 200 && r->body) {
        char key[200]; snprintf(key, sizeof key, "mdblist://%s", g_mdb[slot].imdb);
        app_cache_put(key, r->body, r->len);
        mdb_parse(slot, r->body);
    } else g_mdb[slot].state = 3;
    app_redraw();
}

const char *mdblist_line(const char *imdb_id, const char *type)
{
    (void)type;
    if (!mdblist_enabled() || !imdb_id || strncmp(imdb_id, "tt", 2)) return NULL;
    for (int i = 0; i < 8; i++) if (strcmp(g_mdb[i].imdb, imdb_id) == 0) return g_mdb[i].state == 2 ? g_mdb[i].line : NULL;
    int slot = g_mdb_next; g_mdb_next = (g_mdb_next + 1) % 8;
    snprintf(g_mdb[slot].imdb, sizeof g_mdb[slot].imdb, "%s", imdb_id);
    g_mdb[slot].state = 1;
    char key[200]; snprintf(key, sizeof key, "mdblist://%s", imdb_id);
    char *cached = app_cache_get(key, 3 * 24 * 3600);
    if (cached) { mdb_parse(slot, cached); free(cached); return g_mdb[slot].state == 2 ? g_mdb[slot].line : NULL; }
    char url[300]; snprintf(url, sizeof url, "%s/?apikey=%s&i=%s", mdb_api(), mdb_key(), imdb_id);
    http_request_async(url, NULL, on_mdb, (void *)(intptr_t)slot);
    return NULL;
}
