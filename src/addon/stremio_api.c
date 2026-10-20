#include "stremio_api.h"
#include "../../vendor/cJSON.h"
#include "../http.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

const char *const STREMIO_DEFAULT_ADDON_URLS[] = {
    STREMIO_CINEMETA_URL,
    STREMIO_OPENSUBS_URL,
    NULL
};

static const char *url_for(const char *path)
{
    static char buf[256];
    const char *base = getenv("SNTV_API_URL");   /* tests: the fake server */
    snprintf(buf, sizeof buf, "%s/api/%s", base && *base ? base : STREMIO_API_URL, path);
    return buf;
}
const char *api_url_login(void)                { return url_for("login"); }
const char *api_url_login_with_token(void)     { return url_for("loginWithToken"); }
const char *api_url_logout(void)               { return url_for("logout"); }
const char *api_url_addon_collection_get(void) { return url_for("addonCollectionGet"); }
const char *api_url_addon_collection_set(void) { return url_for("addonCollectionSet"); }
const char *api_url_datastore_get(void)        { return url_for("datastoreGet"); }
const char *api_url_datastore_put(void)        { return url_for("datastorePut"); }
const char *api_url_get_user(void)             { return url_for("getUser"); }
/* SNTV_LINK_API overrides the link service base (tests point it at the fake addon). */
static const char *link_base(void)
{
    const char *e = getenv("SNTV_LINK_API");
    return e && *e ? e : STREMIO_LINK_API_URL;
}

const char *api_url_link_create(void)
{
    static char buf[300];
    snprintf(buf, sizeof buf, "%s/create?type=Create", link_base());
    return buf;
}

const char *api_url_link_read(const char *code, char *buf, size_t n)
{
    char enc[128];
    http_url_encode(code, enc, sizeof enc);
    snprintf(buf, n, "%s/read?type=Read&code=%s", link_base(), enc);
    return buf;
}

static char *finish(cJSON *o)
{
    char *s = cJSON_PrintUnformatted(o);
    cJSON_Delete(o);
    return s;
}

char *api_body_login(const char *email, const char *password)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "type", "Login");
    cJSON_AddStringToObject(o, "email", email);
    cJSON_AddStringToObject(o, "password", password);
    cJSON_AddBoolToObject(o, "facebook", 0);
    return finish(o);
}

char *api_body_login_with_token(const char *token)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "type", "LoginWithToken");
    cJSON_AddStringToObject(o, "token", token);
    return finish(o);
}

char *api_body_logout(const char *auth_key)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "type", "Logout");
    cJSON_AddStringToObject(o, "authKey", auth_key);
    return finish(o);
}

char *api_body_addon_collection_get(const char *auth_key)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "type", "AddonCollectionGet");
    cJSON_AddStringToObject(o, "authKey", auth_key);
    cJSON_AddBoolToObject(o, "update", 1);
    return finish(o);
}

char *api_body_addon_collection_set(const char *auth_key, const Addon *addons, int n)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "type", "AddonCollectionSet");
    cJSON_AddStringToObject(o, "authKey", auth_key);
    cJSON *arr = cJSON_AddArrayToObject(o, "addons");
    for (int i = 0; i < n; i++) {
        /* The whole manifest: the server stores it as given, and every Stremio app reads
         * it back. A partial one breaks the account's addons everywhere, so none is sent. */
        cJSON *m = addons[i].manifest_json ? cJSON_Parse(addons[i].manifest_json) : NULL;
        if (!addon_manifest_complete(m)) { cJSON_Delete(m); cJSON_Delete(o); return NULL; }
        addon_manifest_fill(m);
        cJSON *d = cJSON_CreateObject();
        cJSON_AddStringToObject(d, "transportUrl", addons[i].transport_url);
        cJSON_AddItemToObject(d, "manifest", m);
        cJSON *f = cJSON_AddObjectToObject(d, "flags");
        cJSON_AddBoolToObject(f, "official", addons[i].flag_official);
        cJSON_AddBoolToObject(f, "protected", addons[i].flag_protected);
        cJSON_AddItemToArray(arr, d);
    }
    return finish(o);
}

char *api_body_get_user(const char *auth_key)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "type", "GetUser");
    cJSON_AddStringToObject(o, "authKey", auth_key);
    return finish(o);
}

char *api_body_datastore_get(const char *auth_key, const char *collection)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "type", "DatastoreGet");
    cJSON_AddStringToObject(o, "authKey", auth_key);
    cJSON_AddStringToObject(o, "collection", collection);
    cJSON_AddItemToObject(o, "ids", cJSON_CreateArray());
    cJSON_AddBoolToObject(o, "all", 1);
    return finish(o);
}

/* "2024-05-02T20:30:00.000Z": stremio-core reads _ctime, _mtime and lastWatched only as
 * ISO 8601 strings; an item with numbers there is unreadable to the other Stremio apps. */
static void add_time(cJSON *o, const char *key, int64_t ms)
{
    if (ms <= 0) { cJSON_AddNullToObject(o, key); return; }
    time_t sec = (time_t)(ms / 1000);
    struct tm tm;
    char buf[64];
    gmtime_r(&sec, &tm);
    snprintf(buf, sizeof buf, "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
             tm.tm_hour, tm.tm_min, tm.tm_sec, (int)(ms % 1000));
    cJSON_AddStringToObject(o, key, buf);
}

cJSON *library_item_to_json(const LibraryItem *it)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "_id", it->id);
    cJSON_AddStringToObject(o, "name", it->name ? it->name : "");
    cJSON_AddStringToObject(o, "type", it->type ? it->type : "movie");
    if (it->poster) cJSON_AddStringToObject(o, "poster", it->poster);
    cJSON_AddStringToObject(o, "posterShape", it->poster_shape ? it->poster_shape : "poster");
    cJSON_AddBoolToObject(o, "removed", it->removed);
    cJSON_AddBoolToObject(o, "temp", it->temp);
    add_time(o, "_ctime", it->ctime_ms);
    add_time(o, "_mtime", it->mtime_ms > 0 ? it->mtime_ms : (int64_t)time(NULL) * 1000);
    cJSON *st = cJSON_AddObjectToObject(o, "state");
    add_time(st, "lastWatched", it->last_watched_ms);
    cJSON_AddNumberToObject(st, "timeWatched", (double)it->time_watched);
    cJSON_AddNumberToObject(st, "timeOffset", (double)it->time_offset);
    cJSON_AddNumberToObject(st, "overallTimeWatched", (double)it->overall_time_watched);
    cJSON_AddNumberToObject(st, "timesWatched", it->times_watched);
    cJSON_AddNumberToObject(st, "flaggedWatched", it->flagged_watched);
    cJSON_AddNumberToObject(st, "duration", (double)it->duration);
    if (it->video_id) cJSON_AddStringToObject(st, "video_id", it->video_id);
    else cJSON_AddNullToObject(st, "video_id");
    cJSON_AddBoolToObject(st, "noNotif", it->no_notif);
    return o;
}

char *api_body_datastore_put(const char *auth_key, const char *collection, const LibraryItem *items, int n)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "type", "DatastorePut");
    cJSON_AddStringToObject(o, "authKey", auth_key);
    cJSON_AddStringToObject(o, "collection", collection);
    cJSON *arr = cJSON_AddArrayToObject(o, "changes");
    for (int i = 0; i < n; i++) cJSON_AddItemToArray(arr, library_item_to_json(&items[i]));
    return finish(o);
}

/* ---- responses ----------------------------------------------------------- */

/* Parses the envelope. On 0, *root_out owns the tree and *result_out points
 * inside it. On 1 the API error is filled. */
static int parse_envelope(const char *json, cJSON **root_out, cJSON **result_out, ApiError *err)
{
    memset(err, 0, sizeof *err);
    cJSON *root = cJSON_Parse(json);
    if (!root) { snprintf(err->message, sizeof err->message, "invalid JSON"); return -1; }
    cJSON *e = cJSON_GetObjectItemCaseSensitive(root, "error");
    if (cJSON_IsObject(e)) {
        cJSON *c = cJSON_GetObjectItemCaseSensitive(e, "code");
        cJSON *m = cJSON_GetObjectItemCaseSensitive(e, "message");
        err->code = cJSON_IsNumber(c) ? c->valueint : -1;
        snprintf(err->message, sizeof err->message, "%s", cJSON_IsString(m) ? m->valuestring : "API error");
        cJSON_Delete(root);
        return 1;
    }
    cJSON *r = cJSON_GetObjectItemCaseSensitive(root, "result");
    if (!r) { cJSON_Delete(root); snprintf(err->message, sizeof err->message, "missing result"); return -1; }
    *root_out = root;
    *result_out = r;
    return 0;
}

int api_parse_result(const char *json, cJSON **result_out, ApiError *err)
{
    cJSON *root;
    int rc = parse_envelope(json, &root, result_out, err);
    if (rc == 0) {
        /* Detach so the caller owns just the result subtree. */
        cJSON_DetachItemViaPointer(root, *result_out);
        cJSON_Delete(root);
    }
    return rc;
}

int api_parse_auth(const char *json, char **auth_key, char **email, ApiError *err)
{
    cJSON *root, *res;
    *auth_key = NULL; if (email) *email = NULL;
    int rc = parse_envelope(json, &root, &res, err);
    if (rc != 0) return rc;
    cJSON *k = cJSON_GetObjectItemCaseSensitive(res, "authKey");
    if (!cJSON_IsString(k)) { cJSON_Delete(root); snprintf(err->message, sizeof err->message, "no authKey"); return -1; }
    *auth_key = strdup(k->valuestring);
    cJSON *u = cJSON_GetObjectItemCaseSensitive(res, "user");
    cJSON *em = cJSON_GetObjectItemCaseSensitive(u, "email");
    if (email && cJSON_IsString(em)) *email = strdup(em->valuestring);
    cJSON_Delete(root);
    return 0;
}

int api_parse_addon_collection(const char *json, Addon **addons, int *n, ApiError *err)
{
    cJSON *root, *res;
    *addons = NULL; *n = 0;
    int rc = parse_envelope(json, &root, &res, err);
    if (rc != 0) return rc;
    cJSON *arr = cJSON_GetObjectItemCaseSensitive(res, "addons");
    if (!cJSON_IsArray(arr)) { cJSON_Delete(root); snprintf(err->message, sizeof err->message, "no addons"); return -1; }
    int cap = cJSON_GetArraySize(arr);
    Addon *out = calloc((size_t)cap + 1, sizeof *out);
    cJSON *it;
    cJSON_ArrayForEach(it, arr) {
        if (addon_from_descriptor_json(it, &out[*n]) == 0) (*n)++;
    }
    cJSON_Delete(root);
    *addons = out;
    return 0;
}

static int64_t jms(const cJSON *o, const char *key)
{
    /* The API emits either epoch milliseconds or ISO 8601 strings. */
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, key);
    if (cJSON_IsNumber(v)) return (int64_t)v->valuedouble;
    if (cJSON_IsString(v)) {
        int Y, M, D, h, m; double s;
        if (sscanf(v->valuestring, "%d-%d-%dT%d:%d:%lf", &Y, &M, &D, &h, &m, &s) == 6) {
            /* days-from-civil, good enough for ordering/recency */
            int y = Y - (M <= 2);
            int era = (y >= 0 ? y : y - 399) / 400;
            unsigned yoe = (unsigned)(y - era * 400);
            unsigned doy = (unsigned)((153 * (M + (M > 2 ? -3 : 9)) + 2) / 5 + D - 1);
            unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
            int64_t days = (int64_t)era * 146097 + (int64_t)doe - 719468;
            return ((days * 86400 + h * 3600 + m * 60) * 1000) + (int64_t)(s * 1000);
        }
    }
    return 0;
}

static char *jdup(const cJSON *o, const char *key)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, key);
    return cJSON_IsString(v) ? strdup(v->valuestring) : NULL;
}

static uint64_t ju64(const cJSON *o, const char *key)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, key);
    return cJSON_IsNumber(v) && v->valuedouble > 0 ? (uint64_t)v->valuedouble : 0;
}

int api_parse_library(const char *json, LibraryItem **items, int *n, ApiError *err)
{
    cJSON *root, *res;
    *items = NULL; *n = 0;
    int rc = parse_envelope(json, &root, &res, err);
    if (rc != 0) return rc;
    if (!cJSON_IsArray(res)) { cJSON_Delete(root); snprintf(err->message, sizeof err->message, "library not an array"); return -1; }
    int cap = cJSON_GetArraySize(res);
    LibraryItem *out = calloc((size_t)cap + 1, sizeof *out);
    cJSON *it;
    cJSON_ArrayForEach(it, res) {
        if (!cJSON_IsObject(it)) continue;
        LibraryItem *li = &out[*n];
        li->id = jdup(it, "_id");
        li->name = jdup(it, "name");
        li->type = jdup(it, "type");
        li->poster = jdup(it, "poster");
        li->poster_shape = jdup(it, "posterShape");
        li->removed = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(it, "removed"));
        li->temp = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(it, "temp"));
        li->ctime_ms = jms(it, "_ctime");
        li->mtime_ms = jms(it, "_mtime");
        const cJSON *st = cJSON_GetObjectItemCaseSensitive(it, "state");
        if (cJSON_IsObject(st)) {
            li->last_watched_ms = jms(st, "lastWatched");
            li->time_watched = ju64(st, "timeWatched");
            li->time_offset = ju64(st, "timeOffset");
            li->overall_time_watched = ju64(st, "overallTimeWatched");
            li->duration = ju64(st, "duration");
            li->times_watched = (int)ju64(st, "timesWatched");
            li->flagged_watched = (int)ju64(st, "flaggedWatched");
            li->no_notif = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(st, "noNotif"));
            li->video_id = jdup(st, "video_id");
        }
        if (li->id && li->name && li->type) (*n)++; else library_item_free(li);
    }
    cJSON_Delete(root);
    *items = out;
    return 0;
}

void library_item_free(LibraryItem *it)
{
    free(it->id); free(it->name); free(it->type); free(it->poster); free(it->poster_shape); free(it->video_id);
    memset(it, 0, sizeof *it);
}

void library_items_free(LibraryItem *items, int n)
{
    for (int i = 0; i < n; i++) library_item_free(&items[i]);
    free(items);
}

/* ---- link (QR) login -------------------------------------------------------- */

static const cJSON *unwrap(const cJSON *root)
{
    const cJSON *r = cJSON_GetObjectItemCaseSensitive(root, "result");
    return r ? r : root;
}

int api_parse_link_create(const char *json, char *code, size_t code_n, char *link, size_t link_n, char *qrcode, size_t qr_n, ApiError *err)
{
    memset(err, 0, sizeof *err);
    cJSON *root = cJSON_Parse(json);
    if (!root) { snprintf(err->message, sizeof err->message, "invalid JSON"); return -1; }
    const cJSON *r = unwrap(root);
    const cJSON *c = cJSON_GetObjectItemCaseSensitive(r, "code");
    const cJSON *l = cJSON_GetObjectItemCaseSensitive(r, "link");
    const cJSON *q = cJSON_GetObjectItemCaseSensitive(r, "qrcode");
    if (!cJSON_IsString(c)) {
        const cJSON *e = cJSON_GetObjectItemCaseSensitive(root, "error");
        const cJSON *m = cJSON_IsObject(e) ? cJSON_GetObjectItemCaseSensitive(e, "message") : NULL;
        snprintf(err->message, sizeof err->message, "%s", cJSON_IsString(m) ? m->valuestring : "no code in response");
        cJSON_Delete(root);
        return -1;
    }
    snprintf(code, code_n, "%s", c->valuestring);
    snprintf(link, link_n, "%s", cJSON_IsString(l) ? l->valuestring : "");
    snprintf(qrcode, qr_n, "%s", cJSON_IsString(q) ? q->valuestring : "");
    cJSON_Delete(root);
    return 0;
}

int api_parse_link_read(const char *json, char **auth_key, ApiError *err)
{
    memset(err, 0, sizeof *err);
    *auth_key = NULL;
    cJSON *root = cJSON_Parse(json);
    if (!root) { snprintf(err->message, sizeof err->message, "invalid JSON"); return -1; }
    const cJSON *r = unwrap(root);
    const cJSON *k = cJSON_GetObjectItemCaseSensitive(r, "authKey");
    if (cJSON_IsString(k) && k->valuestring[0]) { *auth_key = strdup(k->valuestring); cJSON_Delete(root); return 0; }
    const cJSON *e = cJSON_GetObjectItemCaseSensitive(root, "error");
    if (cJSON_IsObject(e)) {
        const cJSON *m = cJSON_GetObjectItemCaseSensitive(e, "message");
        const cJSON *c = cJSON_GetObjectItemCaseSensitive(e, "code");
        /* "not found"/pending codes mean keep polling; anything else is fatal */
        err->code = cJSON_IsNumber(c) ? c->valueint : 0;
        snprintf(err->message, sizeof err->message, "%s", cJSON_IsString(m) ? m->valuestring : "error");
        cJSON_Delete(root);
        return err->code == 0 || err->code == 102 || err->code == 404 ? 1 : -1;
    }
    cJSON_Delete(root);
    return 1;
}

int api_parse_user(const char *json, char **email, ApiError *err)
{
    cJSON *root, *res;
    *email = NULL;
    int rc = parse_envelope(json, &root, &res, err);
    if (rc != 0) return rc;
    const cJSON *em = cJSON_GetObjectItemCaseSensitive(res, "email");
    if (cJSON_IsString(em)) *email = strdup(em->valuestring);
    cJSON_Delete(root);
    return 0;
}
