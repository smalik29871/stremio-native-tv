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
#define SIMKL_FORM "Content-Type: application/x-www-form-urlencoded"
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
typedef struct { int active, v2; char device[128], user_code[32]; double next, until; int interval; unsigned inflight; } SignIn;   /* v2: SimKL's OAuth 2.0 device flow */
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
        const cJSON *rt = cJSON_GetObjectItemCaseSensitive(j, "refresh_token"), *ex = cJSON_GetObjectItemCaseSensitive(j, "expires_in");
        if (svc == TRACK_TRAKT || si->v2) {   /* SimKL V2 tokens last a week; the refresh token renews them */
            if (cJSON_IsString(rt)) store_set_str(svc == TRACK_TRAKT ? "traktRefresh" : "simklRefresh", rt->valuestring);
            if (cJSON_IsNumber(ex)) {
                store_set_int(svc == TRACK_TRAKT ? "traktExpires" : "simklExpires", (int)(now_s() + ex->valuedouble));
                store_set_int(svc == TRACK_TRAKT ? "traktLife" : "simklLife", (int)ex->valuedouble);
            }
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
    else if (si->v2 && r->status >= 400) {   /* RFC 8628: pending and slow_down keep polling */
        cJSON *e = cJSON_Parse(r->body ? r->body : "");
        const cJSON *err = cJSON_GetObjectItemCaseSensitive(e, "error");
        const char *code = cJSON_IsString(err) ? err->valuestring : "";
        if (!strcmp(code, "slow_down")) si->interval += 5;
        else if (strcmp(code, "authorization_pending")) {
            si->active = 0;
            LOGW("tracking: SimKL sign-in stopped: HTTP %ld %.200s", r->status, r->body ? r->body : "");
            app_toast(!strcmp(code, "access_denied") ? "SimKL sign-in was declined" : !strcmp(code, "expired_token") ? "The SimKL code expired; try again" : "SimKL sign-in failed");
        }
        cJSON_Delete(e);
    }
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
    } else if (si->v2) {
        snprintf(url, sizeof url, "%s/oauth2/token?app-name=stremio-native-tv&app-version=" SNTV_APP_VERSION, simkl_api());
        char body[400], hdr[400];
        snprintf(body, sizeof body, "grant_type=urn%%3Aietf%%3Aparams%%3Aoauth%%3Agrant-type%%3Adevice_code&client_id=%s&device_code=%s", tracking_app_id(svc), si->device);
        headers_for(svc, hdr, sizeof hdr);
        strncat(hdr, "\n" SIMKL_FORM, sizeof hdr - strlen(hdr) - 1);
        HttpOptions o = { .post_json = body, .headers = hdr, .timeout_s = 20 };
        si->inflight = http_request_async_ex(url, &o, on_token, (void *)(intptr_t)svc);
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
        /* AUTH V2 (OAuth 2.0 device flow); apps registered before it only know the PIN */
        snprintf(u, sizeof u, "%s/oauth2/device?app-name=stremio-native-tv&app-version=" SNTV_APP_VERSION, simkl_api());
        char body[300], fh[400];
        snprintf(body, sizeof body, "client_id=%s&scope=media%%3Aread%%20media%%3Awrite", tracking_app_id(svc));
        snprintf(fh, sizeof fh, "%s\n" SIMKL_FORM, hdr);
        HttpOptions o = { .post_json = body, .headers = fh, .timeout_s = 15 };
        http_request_ex(u, &o, &r);
        cJSON *v = r.status == 200 ? cJSON_Parse(r.body) : NULL;
        si->v2 = cJSON_IsString(cJSON_GetObjectItemCaseSensitive(v, "device_code"));
        cJSON_Delete(v);
        if (!si->v2) {
            LOGI("tracking: SimKL V2 sign-in not offered (HTTP %ld), trying the PIN", r.status);
            http_response_free(&r);
            snprintf(u, sizeof u, "%s/oauth/pin?client_id=%s&redirect=http%%3A%%2F%%2Flocalhost" SIMKL_APP, simkl_api(), tracking_app_id(svc));
            HttpOptions o1 = { .headers = hdr, .timeout_s = 15 };
            http_request_ex(u, &o1, &r);
        }
    }
    cJSON *j = r.status == 200 ? cJSON_Parse(r.body) : NULL;
    const cJSON *dc = cJSON_GetObjectItemCaseSensitive(j, "device_code"), *uc = cJSON_GetObjectItemCaseSensitive(j, "user_code");
    const cJSON *vu = cJSON_GetObjectItemCaseSensitive(j, "verification_url"), *iv = cJSON_GetObjectItemCaseSensitive(j, "interval");
    if (!cJSON_IsString(vu)) vu = cJSON_GetObjectItemCaseSensitive(j, "verification_uri");   /* OAuth 2.0's spelling */
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
    if (svc == TRACK_TRAKT) { store_set_str("traktRefresh", NULL); store_set_int("traktExpires", 0); store_set_int("traktLife", 0); }
    else { store_set_str("simklRefresh", NULL); store_set_int("simklExpires", 0); store_set_int("simklLife", 0); }
    store_save();
    g_version++;
}

/* ---- Trakt token refresh, rows ---- */
static MetaPreview *g_rows[TROW_COUNT]; static int g_n_rows[TROW_COUNT];
static double g_rows_at = -1; static int g_rows_inflight, g_refreshing;

/* Renewing: a day before the token runs out, or halfway through one that lasts two days
 * or less. A renewal that fails waits 2 min, then 4, 8 ... up to an hour (offline, the
 * service down), never once a frame. */
static double g_renew_retry[TRACK_SERVICES]; static int g_renew_fails[TRACK_SERVICES];
static void renew_done(int svc, int ok)
{
    if (ok) { g_renew_fails[svc] = 0; g_renew_retry[svc] = now_s() + 60; return; }
    if (g_renew_fails[svc] < 6) g_renew_fails[svc]++;
    double wait = 60.0 * (1 << g_renew_fails[svc]);
    g_renew_retry[svc] = now_s() + (wait > 3600 ? 3600 : wait);
}

static int renew_due(int svc, double t)
{
    int exp = store_get_int(svc == TRACK_TRAKT ? "traktExpires" : "simklExpires", 0);
    int life = store_get_int(svc == TRACK_TRAKT ? "traktLife" : "simklLife", 0);
    if (!exp || t < g_renew_retry[svc]) return 0;
    int margin = life > 0 && life <= 2 * 86400 ? life / 2 : 86400;
    return t > exp - margin;
}

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
        double life = cJSON_IsNumber(ex) ? ex->valuedouble : 86400;
        store_set_int("traktExpires", (int)(now_s() + life));
        store_set_int("traktLife", (int)life);
        store_save();
        LOGI("tracking: Trakt token renewed");
    } else if (r->status == 400 || r->status == 401) { LOGW("tracking: Trakt sign-in no longer valid"); tracking_disconnect(TRACK_TRAKT); }
    else LOGW("tracking: Trakt token renewal failed (%ld); trying again later", r->status);
    renew_done(TRACK_TRAKT, cJSON_IsString(at));
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

/* SimKL V2: a refresh replaces the token (the old one stops at once) */
static int g_simkl_refreshing;
static void on_simkl_refresh(HttpResponse *r, void *user)
{
    (void)user;
    g_simkl_refreshing = 0;
    cJSON *j = r->status == 200 ? cJSON_Parse(r->body) : NULL;
    const cJSON *at = cJSON_GetObjectItemCaseSensitive(j, "access_token"), *rt = cJSON_GetObjectItemCaseSensitive(j, "refresh_token");
    const cJSON *ex = cJSON_GetObjectItemCaseSensitive(j, "expires_in");
    if (cJSON_IsString(at)) {
        store_set_str("simklToken", at->valuestring);
        if (cJSON_IsString(rt)) store_set_str("simklRefresh", rt->valuestring);
        double life = cJSON_IsNumber(ex) ? ex->valuedouble : 604800;
        store_set_int("simklExpires", (int)(now_s() + life));
        store_set_int("simklLife", (int)life);
        store_save();
        LOGI("tracking: SimKL token renewed");
    } else if (r->status == 400 || r->status == 401) { LOGW("tracking: SimKL sign-in no longer valid"); tracking_disconnect(TRACK_SIMKL); }
    else LOGW("tracking: SimKL token renewal failed (%ld); trying again later", r->status);
    renew_done(TRACK_SIMKL, cJSON_IsString(at));
    cJSON_Delete(j);
}

static void refresh_simkl(void)
{
    const char *rt = store_get_str("simklRefresh", "");
    if (!rt || !*rt || g_simkl_refreshing) return;
    char url[300], body[600], hdr[400];
    snprintf(url, sizeof url, "%s/oauth2/token?app-name=stremio-native-tv&app-version=" SNTV_APP_VERSION, simkl_api());
    snprintf(body, sizeof body, "grant_type=refresh_token&client_id=%s&refresh_token=%s", tracking_app_id(TRACK_SIMKL), rt);
    headers_for(TRACK_SIMKL, hdr, sizeof hdr);
    strncat(hdr, "\n" SIMKL_FORM, sizeof hdr - strlen(hdr) - 1);
    HttpOptions o = { .post_json = body, .headers = hdr, .timeout_s = 20, .deliver = 1 };
    g_simkl_refreshing = 1;
    http_request_async_ex(url, &o, on_simkl_refresh, NULL);
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

/* ---- resume points from Trakt (another app or device paused there) ---- */
static int g_playback_inflight;

static int64_t iso_ms(const char *s)   /* "2026-10-19T21:40:00.000Z" */
{
    struct tm tmv = {0};
    if (!s || sscanf(s, "%d-%d-%dT%d:%d:%d", &tmv.tm_year, &tmv.tm_mon, &tmv.tm_mday, &tmv.tm_hour, &tmv.tm_min, &tmv.tm_sec) != 6) return 0;
    tmv.tm_year -= 1900; tmv.tm_mon -= 1;
    return (int64_t)timegm(&tmv) * 1000;
}

static void on_playback(HttpResponse *r, void *user)
{
    (void)user;
    g_playback_inflight = 0;
    cJSON *j = r->status == 200 ? cJSON_Parse(r->body) : NULL, *e;
    int taken = 0, seen = 0;
    cJSON_ArrayForEach(e, j) {
        const cJSON *type = cJSON_GetObjectItemCaseSensitive(e, "type"), *pr = cJSON_GetObjectItemCaseSensitive(e, "progress");
        int64_t when = iso_ms(cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(e, "paused_at")));
        if (!cJSON_IsString(type) || !cJSON_IsNumber(pr) || !when) continue;
        int episode = !strcmp(type->valuestring, "episode");
        const cJSON *top = cJSON_GetObjectItemCaseSensitive(e, episode ? "show" : "movie"), *ep = cJSON_GetObjectItemCaseSensitive(e, "episode");
        const char *imdb = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(top, "ids"), "imdb"));
        if (!imdb || strncmp(imdb, "tt", 2)) continue;
        const cJSON *rt = cJSON_GetObjectItemCaseSensitive(episode ? ep : top, "runtime");
        if (!cJSON_IsNumber(rt) && episode) rt = cJSON_GetObjectItemCaseSensitive(top, "runtime");
        double dur = cJSON_IsNumber(rt) ? rt->valuedouble * 60 : 0;
        char vid[160];
        if (episode) {
            const cJSON *s = cJSON_GetObjectItemCaseSensitive(ep, "season"), *n = cJSON_GetObjectItemCaseSensitive(ep, "number");
            if (!cJSON_IsNumber(s) || !cJSON_IsNumber(n)) continue;
            snprintf(vid, sizeof vid, "%s:%d:%d", imdb, s->valueint, n->valueint);
        } else snprintf(vid, sizeof vid, "%s", imdb);
        seen++;
        taken += app_library_remote_progress(imdb, episode ? "series" : "movie", cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(top, "title")),
                                             vid, dur * pr->valuedouble / 100, dur, when);
    }
    if (j) LOGI("tracking: Trakt has %d resume point%s, %d newer than this TV's", seen, seen == 1 ? "" : "s", taken);
    cJSON_Delete(j);
}

void tracking_pull_playback(void)
{
    if (!tracking_connected(TRACK_TRAKT) || g_playback_inflight) return;
    char url[300]; snprintf(url, sizeof url, "%s/sync/playback?extended=full&limit=30", trakt_api());
    g_playback_inflight = 1;
    request(TRACK_TRAKT, url, NULL, on_playback, NULL);
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
    tracking_pull_playback();   /* with the rows: on sign-in, at launch and every 30 min */
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
    if (tracking_connected(TRACK_SIMKL) && renew_due(TRACK_SIMKL, t)) refresh_simkl();
    if (!tracking_connected(TRACK_TRAKT)) { if (g_n_rows[0] || g_n_rows[1]) { for (int i = 0; i < TROW_COUNT; i++) { meta_previews_free(g_rows[i], g_n_rows[i]); g_rows[i] = NULL; g_n_rows[i] = 0; } g_version++; } g_rows_at = -1; return; }
    if (renew_due(TRACK_TRAKT, t)) refresh_trakt();
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

void tracking_progress(const MetaPreview *m, const char *video_id, double pos, double dur, int final, int played_through)
{
    (void)m;
    if (!video_id || dur <= 0) return;
    char imdb[32]; int season, episode;
    if (!split_id(video_id, imdb, sizeof imdb, &season, &episode)) return;
    double pct = pos / dur * 100;
    if (pct > 100) pct = 100;
    double done = !played_through && pct >= 80 ? 79 : pct;   /* sought to the end: not watched (Trakt's stop and SimKL count 80%) */
    if (strcmp(g_play.vid, video_id) != 0) { memset(&g_play, 0, sizeof g_play); snprintf(g_play.vid, sizeof g_play.vid, "%s", video_id); }
    if (tracking_connected(TRACK_TRAKT)) {
        if (final) { if (g_play.started) scrobble(done >= 80 ? "stop" : "pause", imdb, season, episode, done); g_play.started = 0; }
        else if (!g_play.started) { scrobble("start", imdb, season, episode, pct); g_play.started = 1; }
    }
    if (done >= 80 && !g_play.simkl_done && tracking_connected(TRACK_SIMKL)) { g_play.simkl_done = 1; simkl_watched(imdb, season, episode); }
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

static struct { char imdb[24]; int state; char line[200]; double score[MDB_SOURCES]; } g_mdb[8];
static int g_mdb_next;

static void mdb_parse(int slot, const char *json)
{
    cJSON *j = cJSON_Parse(json);
    const cJSON *rs = cJSON_GetObjectItemCaseSensitive(j, "ratings"), *it;
    char *o = g_mdb[slot].line; size_t n = sizeof g_mdb[slot].line, l = 0;
    o[0] = 0;
    memset(g_mdb[slot].score, 0, sizeof g_mdb[slot].score);
    /* in MDB_* order; the audience score has gone by both names (Kometa reads it as "tomatoesaudience") */
    static const struct { const char *src, *alt, *name, *fmt; } S[] = {
        { "tomatoes", NULL, "Tomatometer", "%.0f%%" }, { "tomatoesaudience", "popcorn", "Popcornmeter", "%.0f%%" },
        { "metacritic", NULL, "Metacritic", "%.0f" }, { "letterboxd", NULL, "Letterboxd", "%.1f" }, { "tmdb", NULL, "TMDB", "%.0f%%" },
        { "trakt", NULL, "Trakt", "%.0f%%" }, { "rogerebert", NULL, "Roger Ebert", "%.1f" } };
    for (size_t k = 0; k < sizeof S / sizeof S[0]; k++)
        if (cJSON_IsArray(rs)) cJSON_ArrayForEach(it, rs) {
            const cJSON *src = cJSON_GetObjectItemCaseSensitive(it, "source"), *v = cJSON_GetObjectItemCaseSensitive(it, "value");
            if (!cJSON_IsString(src) || !cJSON_IsNumber(v) || v->valuedouble <= 0) continue;
            if (strcmp(src->valuestring, S[k].src) && (!S[k].alt || strcmp(src->valuestring, S[k].alt))) continue;
            char val[16]; snprintf(val, sizeof val, S[k].fmt, v->valuedouble);
            if (l < n) l += (size_t)snprintf(o + l, n - l, "%s%s %s", l ? "  \xC2\xB7  " : "", S[k].name, val);
            g_mdb[slot].score[k] = v->valuedouble;
            break;
        }
    const cJSON *all = cJSON_GetObjectItemCaseSensitive(j, "score");   /* MDBList's own blend of them */
    if (cJSON_IsNumber(all) && all->valuedouble > 0) g_mdb[slot].score[MDB_SCORE] = all->valuedouble;
    for (int k = MDB_TMDB; k <= MDB_TRAKT; k++)   /* out of 100, whichever way the answer put it */
        if (g_mdb[slot].score[k] > 0 && g_mdb[slot].score[k] <= 10) g_mdb[slot].score[k] *= 10;
    g_mdb[slot].state = o[0] || g_mdb[slot].score[MDB_SCORE] > 0 ? 2 : 3;
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

int mdblist_scores(const char *imdb_id, const char *type, double out[MDB_SOURCES])
{
    if (!mdblist_line(imdb_id, type)) return 0;   /* asks once, like the line */
    for (int i = 0; i < 8; i++)
        if (!strcmp(g_mdb[i].imdb, imdb_id)) { memcpy(out, g_mdb[i].score, sizeof g_mdb[i].score); return 1; }
    return 0;
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

/* ---- Trakt: how many watch a title, and how many right now ---- */
static struct { char imdb[24]; int state, watching; long watchers, plays; } g_tks[8];   /* state: 1 asked, 2 known, 3 none */
static int g_tks_next;

void tracking_count_text(long v, char *o, size_t n)
{
    if (v >= 1000000) snprintf(o, n, "%.1fM", v / 1e6);
    else if (v >= 10000) snprintf(o, n, "%ldK", v / 1000);
    else if (v >= 1000) snprintf(o, n, "%ld,%03ld", v / 1000, v % 1000);
    else snprintf(o, n, "%ld", v);
}

static void on_tk_stats(HttpResponse *r, void *user)
{
    int slot = (int)(intptr_t)user;
    cJSON *j = r->status == 200 && r->body ? cJSON_Parse(r->body) : NULL;
    const cJSON *w = cJSON_GetObjectItemCaseSensitive(j, "watchers"), *p = cJSON_GetObjectItemCaseSensitive(j, "plays");
    if (cJSON_IsNumber(w) || cJSON_IsNumber(p)) {
        g_tks[slot].watchers = cJSON_IsNumber(w) ? (long)w->valuedouble : 0;
        g_tks[slot].plays = cJSON_IsNumber(p) ? (long)p->valuedouble : 0;
        g_tks[slot].state = 2;
    } else if (g_tks[slot].state != 2) g_tks[slot].state = 3;
    cJSON_Delete(j);
    app_redraw();
}

static void on_tk_watching(HttpResponse *r, void *user)
{
    int slot = (int)(intptr_t)user;
    cJSON *j = r->status == 200 && r->body ? cJSON_Parse(r->body) : NULL;
    if (cJSON_IsArray(j)) g_tks[slot].watching = cJSON_GetArraySize(j);   /* the people watching it now */
    cJSON_Delete(j);
    app_redraw();
}

/* ---- comments: a few people's words on what is playing ---- */
typedef struct { char key[48]; int state, n; TraktComment c[8]; } TkComments;
static TkComments g_tkc[4];
static int g_tkc_next;

static void on_tk_comments(HttpResponse *r, void *user)
{
    TkComments *t = &g_tkc[(int)(intptr_t)user];
    cJSON *j = r->status == 200 && r->body ? cJSON_Parse(r->body) : NULL, *it;
    t->n = 0;
    if (cJSON_IsArray(j)) cJSON_ArrayForEach(it, j) {
        if (t->n >= 8) break;
        const cJSON *text = cJSON_GetObjectItemCaseSensitive(it, "comment"), *sp = cJSON_GetObjectItemCaseSensitive(it, "spoiler");
        const cJSON *likes = cJSON_GetObjectItemCaseSensitive(it, "likes"), *u = cJSON_GetObjectItemCaseSensitive(it, "user");
        const cJSON *name = u ? cJSON_GetObjectItemCaseSensitive(u, "username") : NULL;
        if (!cJSON_IsString(text) || cJSON_IsTrue(sp) || strstr(text->valuestring, "[spoiler]")) continue;
        size_t len = strlen(text->valuestring);
        if (len < 16 || len > 290) continue;   /* a word, or an essay: neither fits the panel */
        TraktComment *c = &t->c[t->n++];
        snprintf(c->text, sizeof c->text, "%s", text->valuestring);
        for (char *q = c->text; *q; q++) if (*q == '\n' || *q == '\r' || *q == '\t') *q = ' ';
        snprintf(c->user, sizeof c->user, "%s", cJSON_IsString(name) ? name->valuestring : "");
        c->likes = cJSON_IsNumber(likes) ? likes->valueint : 0;
    }
    t->state = j ? 2 : 3;
    LOGI("trakt: %d comment%s for %s", t->n, t->n == 1 ? "" : "s", t->key);
    cJSON_Delete(j);
    app_redraw();
}

int trakt_comments(const char *imdb_id, const char *type, int season, int episode, const TraktComment **out, int *n)
{
    *n = 0; *out = NULL;
    if (!tracking_configured(TRACK_TRAKT) || !imdb_id || strncmp(imdb_id, "tt", 2)) return 0;
    char id[24], key[48];
    snprintf(id, sizeof id, "%.*s", (int)strcspn(imdb_id, ":"), imdb_id);
    snprintf(key, sizeof key, "%s:%d:%d", id, season, episode);
    for (int i = 0; i < 4; i++) {
        if (strcmp(g_tkc[i].key, key)) continue;
        if (g_tkc[i].state != 2) return 0;
        *out = g_tkc[i].c; *n = g_tkc[i].n;
        return 1;
    }
    int slot = g_tkc_next; g_tkc_next = (g_tkc_next + 1) % 4;
    memset(&g_tkc[slot], 0, sizeof g_tkc[slot]);
    snprintf(g_tkc[slot].key, sizeof g_tkc[slot].key, "%s", key);
    g_tkc[slot].state = 1;
    char url[260];
    if (type && !strcmp(type, "series") && season >= 0 && episode > 0)
        snprintf(url, sizeof url, "%s/shows/%s/seasons/%d/episodes/%d/comments/likes?limit=20", trakt_api(), id, season, episode);
    else snprintf(url, sizeof url, "%s/%s/%s/comments/likes?limit=20", trakt_api(), type && !strcmp(type, "series") ? "shows" : "movies", id);
    request(TRACK_TRAKT, url, NULL, on_tk_comments, (void *)(intptr_t)slot);
    return 0;
}

int trakt_stats(const char *imdb_id, const char *type, long *watching, long *watchers, long *plays)
{
    if (!tracking_configured(TRACK_TRAKT) || !imdb_id || strncmp(imdb_id, "tt", 2)) return 0;
    char id[24]; snprintf(id, sizeof id, "%.*s", (int)strcspn(imdb_id, ":"), imdb_id);
    for (int i = 0; i < 8; i++) {
        if (strcmp(g_tks[i].imdb, id)) continue;
        if (g_tks[i].state != 2) return 0;
        *watching = g_tks[i].watching; *watchers = g_tks[i].watchers; *plays = g_tks[i].plays;
        return 1;
    }
    int slot = g_tks_next; g_tks_next = (g_tks_next + 1) % 8;
    memset(&g_tks[slot], 0, sizeof g_tks[slot]);
    snprintf(g_tks[slot].imdb, sizeof g_tks[slot].imdb, "%s", id);
    g_tks[slot].state = 1;
    const char *kind = type && !strcmp(type, "series") ? "shows" : "movies";
    char url[200];
    snprintf(url, sizeof url, "%s/%s/%s/stats", trakt_api(), kind, id);
    request(TRACK_TRAKT, url, NULL, on_tk_stats, (void *)(intptr_t)slot);
    snprintf(url, sizeof url, "%s/%s/%s/watching", trakt_api(), kind, id);
    request(TRACK_TRAKT, url, NULL, on_tk_watching, (void *)(intptr_t)slot);
    return 0;
}
