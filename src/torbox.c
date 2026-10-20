#include "torbox.h"
#include "http.h"
#include "log.h"
#include "store.h"
#include "cJSON.h"
#include "addon/addon.h"
#include <ctype.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static const char *base(void)
{
    const char *b = getenv("SNTV_TORBOX_API");   /* tests point this at the fake server */
    return b && *b ? b : "https://api.torbox.app/v1/api";
}
static const char *key(void) { const char *k = store_get_str("torboxKey", ""); return k ? k : ""; }

/* The key as the network threads see it (the store belongs to the main thread) */
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static char g_key[160];

int torbox_enabled(void)
{
    const char *k = key();
    pthread_mutex_lock(&g_lock);
    snprintf(g_key, sizeof g_key, "%s", k);
    pthread_mutex_unlock(&g_lock);
    return k[0] != 0;
}

static void key_copy(char *out, size_t n)
{
    pthread_mutex_lock(&g_lock);
    snprintf(out, n, "%s", g_key);
    pthread_mutex_unlock(&g_lock);
}
int torbox_is_link(const char *s) { return s && !strncmp(s, "torbox://", 9); }

static const char *id_param(const char *type)
{
    return !strcmp(type, "usenet") ? "usenet_id" : !strcmp(type, "webdl") ? "web_id" : "torrent_id";
}

const char *torbox_resolve(const char *stored, char *out, size_t n)
{
    if (!torbox_is_link(stored) || !torbox_enabled()) return NULL;
    char type[16]; long item = 0, file = 0;
    if (sscanf(stored + 9, "%15[a-z]/%ld/%ld", type, &item, &file) != 3) return NULL;
    char enc[256];
    http_url_encode(key(), enc, sizeof enc);
    snprintf(out, n, "%s/%s/requestdl?token=%s&%s=%ld&file_id=%ld&redirect=true", base(), type, enc, id_param(type), item, file);
    return out;
}

const char *torbox_name(const char *stored, char *out, size_t n)
{
    const char *p = torbox_is_link(stored) ? stored + 9 : NULL;
    for (int k = 0; p && k < 3; k++) { p = strchr(p, '/'); if (p) p++; }
    snprintf(out, n, "%s", p && *p ? p : "a TorBox file");
    return out;
}

/* ---- the list: torrents, then usenet, then web downloads ---- */

typedef struct { TorboxListCb cb; void *user; int stage; TorboxFile *files; int n, cap; char error[160]; } Job;
static const char *const TYPES[3] = { "torrents", "usenet", "webdl" };
static void ask(Job *j);

static int is_video(const cJSON *file)
{
    const cJSON *m = cJSON_GetObjectItemCaseSensitive(file, "mimetype"), *nm = cJSON_GetObjectItemCaseSensitive(file, "short_name");
    if (cJSON_IsString(m) && !strncmp(m->valuestring, "video/", 6)) return 1;
    const char *s = cJSON_IsString(nm) ? nm->valuestring : "", *dot = strrchr(s, '.');
    return dot && (!strcasecmp(dot, ".mp4") || !strcasecmp(dot, ".mkv") || !strcasecmp(dot, ".webm") || !strcasecmp(dot, ".mov") || !strcasecmp(dot, ".m4v"));
}

static void on_list(HttpResponse *r, void *user)
{
    Job *j = user;
    const char *type = TYPES[j->stage];
    cJSON *root = r->status == 200 && r->body ? cJSON_Parse(r->body) : NULL;
    if (r->status == 401 || r->status == 403) snprintf(j->error, sizeof j->error, "TorBox refused the API key (%ld)", r->status);
    else if (!root && r->status != 404) snprintf(j->error, sizeof j->error, "TorBox did not answer (%ld)", r->status);   /* 404: nothing of this kind */
    const cJSON *item, *file;
    cJSON_ArrayForEach(item, cJSON_GetObjectItemCaseSensitive(root, "data")) {
        const cJSON *id = cJSON_GetObjectItemCaseSensitive(item, "id");
        if (!cJSON_IsNumber(id)) continue;
        cJSON_ArrayForEach(file, cJSON_GetObjectItemCaseSensitive(item, "files")) {
            const cJSON *fid = cJSON_GetObjectItemCaseSensitive(file, "id"), *nm = cJSON_GetObjectItemCaseSensitive(file, "short_name");
            const cJSON *sz = cJSON_GetObjectItemCaseSensitive(file, "size");
            if (!cJSON_IsNumber(fid) || !cJSON_IsString(nm) || !is_video(file)) continue;
            if (j->n == j->cap) { j->cap = j->cap ? j->cap * 2 : 32; j->files = realloc(j->files, sizeof *j->files * (size_t)j->cap); }
            TorboxFile *t = &j->files[j->n++];
            snprintf(t->name, sizeof t->name, "%s", nm->valuestring);
            snprintf(t->link, sizeof t->link, "torbox://%s/%.0f/%.0f/%s", type, id->valuedouble, fid->valuedouble, nm->valuestring);
            t->size_mb = cJSON_IsNumber(sz) ? sz->valuedouble / 1048576.0 : 0;
        }
    }
    cJSON_Delete(root);
    if (++j->stage < 3 && !(r->status == 401 || r->status == 403)) { ask(j); return; }
    LOGI("torbox: %d video file%s%s%s", j->n, j->n == 1 ? "" : "s", j->error[0] ? "; " : "", j->error);
    j->cb(j->n ? j->files : NULL, j->n, j->n ? NULL : j->error[0] ? j->error : "No videos in your TorBox yet", j->user);
    free(j->files);
    free(j);
}

static void ask(Job *j)
{
    char url[300], hdr[300];
    snprintf(url, sizeof url, "%s/%s/mylist?bypass_cache=true&limit=1000", base(), TYPES[j->stage]);
    snprintf(hdr, sizeof hdr, "Authorization: Bearer %s", key());
    HttpOptions o = { .timeout_s = 15, .headers = hdr };
    http_request_async_ex(url, &o, on_list, j);
}

void torbox_list_videos(TorboxListCb cb, void *user)
{
    if (!torbox_enabled()) { cb(NULL, 0, "Add your TorBox API key first (Settings > API keys)", user); return; }
    Job *j = calloc(1, sizeof *j);
    j->cb = cb; j->user = user;
    ask(j);
}

/* ---- torrents from addons ---------------------------------------------------------- */

#define SEEN 256
static struct { char hash[41]; int cached; } g_seen[SEEN];   /* what the last checks said, newest replacing oldest */
static int g_seen_next;

static void norm_hash(const char *in, char out[41])
{
    int i = 0;
    for (; i < 40 && in[i]; i++) out[i] = (char)tolower((unsigned char)in[i]);
    out[i] = 0;
}

static void remember(const char *hash, int cached)
{
    for (int i = 0; i < SEEN; i++) if (!strcmp(g_seen[i].hash, hash)) { g_seen[i].cached = cached; return; }
    snprintf(g_seen[g_seen_next].hash, sizeof g_seen[0].hash, "%s", hash);
    g_seen[g_seen_next].cached = cached;
    g_seen_next = (g_seen_next + 1) % SEEN;
}

int torbox_hash_cached(const char *info_hash)
{
    if (!info_hash || !torbox_enabled()) return 0;
    char h[41]; norm_hash(info_hash, h);
    pthread_mutex_lock(&g_lock);
    int c = 0;
    for (int i = 0; i < SEEN; i++) if (!strcmp(g_seen[i].hash, h)) { c = g_seen[i].cached; break; }
    pthread_mutex_unlock(&g_lock);
    return c;
}

static void auth_header(char *out, size_t n)
{
    char k[160]; key_copy(k, sizeof k);
    snprintf(out, n, "Authorization: Bearer %s", k);
}

void torbox_mark_streams(Stream *s, int n)
{
    char k[160]; key_copy(k, sizeof k);
    if (!k[0] || n <= 0) return;
    char *q = malloc(64 + (size_t)n * 41 + 300);
    int len = sprintf(q, "%s/torrents/checkcached?format=list&list_files=false&hash=", base()), asked = 0;
    for (int i = 0; i < n && asked < 100; i++) {
        if (!s[i].info_hash || s[i].url || strlen(s[i].info_hash) < 40) continue;
        char h[41]; norm_hash(s[i].info_hash, h);
        if (strstr(q, h)) continue;
        len += sprintf(q + len, "%s%s", asked ? "," : "", h);
        asked++;
    }
    if (!asked) { free(q); return; }
    char hdr[220]; auth_header(hdr, sizeof hdr);
    HttpOptions o = { .timeout_s = 6, .headers = hdr };
    HttpResponse r;
    http_request_ex(q, &o, &r);
    free(q);
    cJSON *root = r.status == 200 && r.body ? cJSON_Parse(r.body) : NULL;
    const cJSON *data = cJSON_GetObjectItemCaseSensitive(root, "data"), *it;
    int cached = 0;
    pthread_mutex_lock(&g_lock);
    for (int i = 0; i < n; i++) {
        if (!s[i].info_hash || s[i].url) continue;
        char h[41]; norm_hash(s[i].info_hash, h);
        int hit = 0;
        cJSON_ArrayForEach(it, data) {   /* a list of the cached ones (an object keyed by hash in the older format) */
            const cJSON *hh = cJSON_IsObject(it) ? cJSON_GetObjectItemCaseSensitive(it, "hash") : NULL;
            const char *v = cJSON_IsString(hh) ? hh->valuestring : it->string;
            if (v && !strcasecmp(v, h)) { hit = 1; break; }
        }
        if (root) remember(h, hit);
        if (hit) { s[i].tags.cached = 1; cached++; }
    }
    pthread_mutex_unlock(&g_lock);
    if (r.status == 200) LOGI("torbox: %d of %d torrent%s cached", cached, asked, asked == 1 ? "" : "s");
    else LOGW("torbox: the cache check failed (%ld %s)", r.status, r.error);
    cJSON_Delete(root);
    http_response_free(&r);
}

int torbox_is_hash_link(const char *s) { return s && !strncmp(s, "torbox://hash/", 14); }

char *torbox_hash_link(const char *info_hash, int file_idx)
{
    char h[41]; norm_hash(info_hash, h);
    char buf[96]; snprintf(buf, sizeof buf, "torbox://hash/%s/%d", h, file_idx);
    return strdup(buf);
}

/* POST createtorrent (a form, as TorBox takes it): the torrent's id in your TorBox, or -1 */
static long add_torrent(const char *hash, char *err, size_t en)
{
    static const char *B = "----sntvTorBoxForm";
    char body[600], hdr[400], url[300];
    snprintf(body, sizeof body, "--%s\r\nContent-Disposition: form-data; name=\"magnet\"\r\n\r\nmagnet:?xt=urn:btih:%s\r\n"
             "--%s\r\nContent-Disposition: form-data; name=\"allow_zip\"\r\n\r\nfalse\r\n--%s--\r\n", B, hash, B, B);
    char auth[220]; auth_header(auth, sizeof auth);
    snprintf(hdr, sizeof hdr, "%s\nContent-Type: multipart/form-data; boundary=%s", auth, B);
    snprintf(url, sizeof url, "%s/torrents/createtorrent", base());
    HttpOptions o = { .post_json = body, .timeout_s = 15, .headers = hdr };
    HttpResponse r;
    http_request_ex(url, &o, &r);
    cJSON *root = r.body ? cJSON_Parse(r.body) : NULL;
    const cJSON *id = cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(root, "data"), "torrent_id");
    const cJSON *detail = cJSON_GetObjectItemCaseSensitive(root, "detail");
    long tid = cJSON_IsNumber(id) ? (long)id->valuedouble : -1;
    if (tid < 0) snprintf(err, en, "TorBox did not take the torrent (%ld%s%.100s)", r.status, cJSON_IsString(detail) ? ": " : "", cJSON_IsString(detail) ? detail->valuestring : "");
    cJSON_Delete(root);
    http_response_free(&r);
    return tid;
}

typedef struct { char hash[41]; int file_idx; TorboxOpenCb cb; void *user; } Open;

static void open_task(HttpResponse *out, void *arg)
{
    Open *o = arg;
    char err[200] = "";
    long tid = add_torrent(o->hash, err, sizeof err);
    if (tid < 0) { snprintf(out->error, sizeof out->error, "%s", err); return; }
    char url[300], hdr[220];
    snprintf(url, sizeof url, "%s/torrents/mylist?id=%ld&bypass_cache=true", base(), tid);
    auth_header(hdr, sizeof hdr);
    HttpOptions ho = { .timeout_s = 12, .headers = hdr };
    HttpResponse r;
    http_request_ex(url, &ho, &r);
    cJSON *root = r.body ? cJSON_Parse(r.body) : NULL;
    const cJSON *t = cJSON_GetObjectItemCaseSensitive(root, "data"), *f;
    if (cJSON_IsArray(t)) t = cJSON_GetArrayItem(t, 0);
    long fid = -1; double best = -1;
    if (!cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(t, "download_present")) && !cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(t, "download_finished")))
        snprintf(out->error, sizeof out->error, "TorBox is still downloading it");
    else cJSON_ArrayForEach(f, cJSON_GetObjectItemCaseSensitive(t, "files")) {
        const cJSON *id = cJSON_GetObjectItemCaseSensitive(f, "id"), *sz = cJSON_GetObjectItemCaseSensitive(f, "size");
        if (!cJSON_IsNumber(id) || !is_video(f)) continue;
        if (o->file_idx >= 0 && (long)id->valuedouble == o->file_idx) { fid = (long)id->valuedouble; break; }   /* the addon's file */
        double size = cJSON_IsNumber(sz) ? sz->valuedouble : 0;
        if (size > best) { best = size; fid = (long)id->valuedouble; }   /* else the largest video */
    }
    if (fid < 0 && !out->error[0]) snprintf(out->error, sizeof out->error, "No video in that torrent on TorBox");
    if (fid >= 0) {
        char k[160], enc[256], link[700];
        key_copy(k, sizeof k);
        http_url_encode(k, enc, sizeof enc);
        snprintf(link, sizeof link, "%s/torrents/requestdl?token=%s&torrent_id=%ld&file_id=%ld&redirect=true", base(), enc, tid, fid);
        out->status = 200;
        out->body = strdup(link);
        out->len = strlen(link);
    }
    cJSON_Delete(root);
    http_response_free(&r);
}

static void on_open(HttpResponse *r, void *user)
{
    Open *o = user;
    if (r->status == 200 && r->body) LOGI("torbox: %s plays from TorBox", o->hash);
    else LOGW("torbox: %s not from TorBox: %s", o->hash, r->error);
    o->cb(r->status == 200 ? r->body : NULL, r->status == 200 ? NULL : r->error[0] ? r->error : "TorBox did not answer", o->user);
    free(o);
}

void torbox_open_hash(const char *link, TorboxOpenCb cb, void *user)
{
    Open *o = calloc(1, sizeof *o);
    o->cb = cb; o->user = user; o->file_idx = -1;
    if (!torbox_is_hash_link(link) || !torbox_enabled() || sscanf(link + 14, "%40[0-9a-fA-F]/%d", o->hash, &o->file_idx) < 1) {
        free(o); cb(NULL, "Not a TorBox link, or no TorBox key", user); return;
    }
    http_task_async("torbox open", open_task, o, on_open, o);
}

static void queue_task(HttpResponse *out, void *arg)
{
    char err[200] = "";
    long tid = add_torrent(arg, err, sizeof err);
    if (tid >= 0) out->status = 200; else snprintf(out->error, sizeof out->error, "%s", err);
}
static void on_queued(HttpResponse *r, void *user)
{
    if (r->status == 200) LOGI("torbox: %s added to your TorBox; playing P2P until it is ready", (char *)user);
    else LOGW("torbox: could not add %s: %s", (char *)user, r->error);
    free(user);
}

void torbox_queue_hash(const char *info_hash)
{
    if (!info_hash || !torbox_enabled()) return;
    char *h = malloc(41); norm_hash(info_hash, h);
    http_task_async("torbox add", queue_task, h, on_queued, h);
}
