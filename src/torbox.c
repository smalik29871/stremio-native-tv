#include "torbox.h"
#include "http.h"
#include "log.h"
#include "store.h"
#include "cJSON.h"
#include "addon/addon.h"
#include "ai.h"
#include "quality.h"
#include <ctype.h>
#include <pthread.h>
#include <time.h>
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

static const char *resolve_key(const char *stored, const char *k, char *out, size_t n);
static char *direct_link(const char *permanent);

const char *torbox_resolve(const char *stored, char *out, size_t n)
{
    if (!torbox_is_link(stored) || !torbox_enabled()) return NULL;
    return resolve_key(stored, key(), out, n);
}

static const char *resolve_key(const char *stored, const char *k, char *out, size_t n)
{
    char type[16]; long item = 0, file = 0;
    if (sscanf(stored + 9, "%15[a-z]/%ld/%ld", type, &item, &file) != 3) return NULL;
    char enc[256];
    http_url_encode(k, enc, sizeof enc);
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
            const cJSON *in = cJSON_GetObjectItemCaseSensitive(item, "name");
            snprintf(t->item, sizeof t->item, "%s", cJSON_IsString(in) ? in->valuestring : "");
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

typedef struct { char hash[41]; int file_idx; EpisodeHint ep; AiSnap ai; TorboxOpenCb cb; void *user; } Open;

typedef struct { long id; double size; const char *name; int score; } PackFile;

/* The episode's file in a season pack: the one its name says is it, the AI's pick when
 * the names leave several (asked with the list, as a person would read it), else the
 * largest that is not plainly another episode. */
static long pick_episode(const Open *o, PackFile *f, int n)
{
    int best = -1;
    for (int i = 0; i < n; i++) {
        f[i].score = quality_episode_score(f[i].name, &o->ep);
        if (best < 0 || f[i].score > f[best].score || (f[i].score == f[best].score && f[i].size > f[best].size)) best = i;
    }
    if (best < 0) return -1;
    int tied = 0, others = 0;
    for (int i = 0; i < n; i++) { tied += f[i].score == f[best].score && f[i].size > f[best].size * 0.25; others += f[i].score < 0; }
    if (f[best].score > 0) {   /* named for it (two copies so named: the larger) */
        LOGI("torbox: the episode's file by its name: %s", f[best].name);
        return f[best].id;
    }
    /* the pack names its episodes and none is this one (or every file is another): not here;
     * one stray file named for another (a preview) does not make it such a pack */
    if (f[best].score < 0 || others >= 2) {
        LOGI("torbox: no file in this torrent is S%dE%d (it names other episodes)", o->ep.season, o->ep.episode);
        return -1;
    }
    if (tied == 1) {   /* nothing named: the one main file */
        LOGI("torbox: no episode named; its one main file plays: %s", f[best].name);
        return f[best].id;
    }
    /* the AI's answer for this torrent and episode, kept: standby sources re-open it now and then */
    static pthread_mutex_t memo_lock = PTHREAD_MUTEX_INITIALIZER;
    static struct { char hash[41]; int season, episode; long fid; } memo[8];
    static int memo_next;
    if (tied > 1 && o->ai.url[0]) {
        long known = -1;
        pthread_mutex_lock(&memo_lock);
        for (int m = 0; m < 8; m++) if (!strcmp(memo[m].hash, o->hash) && memo[m].season == o->ep.season && memo[m].episode == o->ep.episode) known = memo[m].fid;
        pthread_mutex_unlock(&memo_lock);
        for (int i = 0; i < n && known >= 0; i++) if (f[i].id == known) { LOGI("torbox: the episode's file, as the AI picked before: %s", f[i].name); return known; }
        char msg[6000];
        int len = snprintf(msg, sizeof msg, "Show: %s\nWanted: season %d, episode %d", o->ep.show, o->ep.season, o->ep.episode);
        if (o->ep.absolute) len += snprintf(msg + len, sizeof msg - (size_t)len, " (episode %d counting from the first)", o->ep.absolute);
        if (o->ep.title[0]) len += snprintf(msg + len, sizeof msg - (size_t)len, ", titled \"%s\"", o->ep.title);
        len += snprintf(msg + len, sizeof msg - (size_t)len, "\nFiles:\n");
        for (int i = 0; i < n && i < 60 && len < (int)sizeof msg - 200; i++)
            len += snprintf(msg + len, sizeof msg - (size_t)len, "%d. %.180s (%.1f GB)\n", i, f[i].name, f[i].size / 1e9);
        char *a = ai_ask_now(&o->ai, "You find which file in a torrent is a given TV episode. Reply with JSON only: "
                             "{\"file\": <the file's number>}, or {\"file\": -1} when no file is that episode.", msg, 8);
        cJSON *j = a ? cJSON_Parse(a) : NULL;
        const cJSON *k = cJSON_GetObjectItemCaseSensitive(j, "file");
        int pick = cJSON_IsNumber(k) ? (int)k->valuedouble : -2;
        cJSON_Delete(j); free(a);
        if (pick >= 0 && pick < n && f[pick].score == f[best].score) {   /* among the tied ones only, never a worse name */
            LOGI("torbox: the AI picked the episode's file: %s", f[pick].name);
            pthread_mutex_lock(&memo_lock);
            snprintf(memo[memo_next].hash, sizeof memo[memo_next].hash, "%s", o->hash);
            memo[memo_next].season = o->ep.season; memo[memo_next].episode = o->ep.episode; memo[memo_next].fid = f[pick].id;
            memo_next = (memo_next + 1) % 8;
            pthread_mutex_unlock(&memo_lock);
            return f[pick].id;
        }
        LOGI("torbox: the AI did not pick a file (%d)", pick);
    }
    if (tied == 2) {   /* two: one copy in two formats, or an episode in two parts: the larger, as always */
        LOGI("torbox: no episode named; the larger of two files plays: %s", f[best].name);
        return f[best].id;
    }
    /* several alike and nothing says which is the episode (a disc rip): another source, not a guess */
    LOGI("torbox: can't tell which of %d files is S%dE%d; leaving this torrent", tied, o->ep.season, o->ep.episode);
    return -1;
}

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
    PackFile pack[200]; int n_pack = 0;
    if (!cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(t, "download_present")) && !cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(t, "download_finished")))
        snprintf(out->error, sizeof out->error, "TorBox is still downloading it");
    else cJSON_ArrayForEach(f, cJSON_GetObjectItemCaseSensitive(t, "files")) {
        const cJSON *id = cJSON_GetObjectItemCaseSensitive(f, "id"), *sz = cJSON_GetObjectItemCaseSensitive(f, "size");
        const cJSON *nm = cJSON_GetObjectItemCaseSensitive(f, "name");   /* "Pack Folder/file.mkv", the folder can name the season */
        if (!cJSON_IsString(nm)) nm = cJSON_GetObjectItemCaseSensitive(f, "short_name");
        if (!cJSON_IsNumber(id) || !is_video(f)) continue;
        if (o->file_idx >= 0 && (long)id->valuedouble == o->file_idx) { fid = (long)id->valuedouble; n_pack = 0; break; }   /* the addon's file */
        double size = cJSON_IsNumber(sz) ? sz->valuedouble : 0;
        if (n_pack < 200) pack[n_pack++] = (PackFile){ (long)id->valuedouble, size, cJSON_IsString(nm) ? nm->valuestring : "", 0 };
        if (size > best) { best = size; fid = (long)id->valuedouble; }   /* else the largest video */
    }
    if (o->ep.episode > 0 && n_pack > 1) {
        fid = pick_episode(o, pack, n_pack);
        if (fid < 0 && !out->error[0]) snprintf(out->error, sizeof out->error, "No file in that torrent is this episode");
    }
    if (fid < 0 && !out->error[0]) snprintf(out->error, sizeof out->error, "No video in that torrent on TorBox");
    if (fid >= 0) {
        char k[160], enc[256], link[700];
        key_copy(k, sizeof k);
        http_url_encode(k, enc, sizeof enc);
        snprintf(link, sizeof link, "%s/torrents/requestdl?token=%s&torrent_id=%ld&file_id=%ld&redirect=true", base(), enc, tid, fid);
        char *direct = direct_link(link);
        out->status = 200;
        out->body = direct ? direct : strdup(link);
        out->len = strlen(out->body);
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

void torbox_open_hash(const char *link, const EpisodeHint *ep, TorboxOpenCb cb, void *user)
{
    Open *o = calloc(1, sizeof *o);
    o->cb = cb; o->user = user; o->file_idx = -1;
    if (ep) { o->ep = *ep; if (ep->episode > 0) ai_snapshot(&o->ai); }
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

/* ---- your TorBox as a source ----------------------------------------------------------- */

static struct { TorboxFile *files; int n; time_t at; char key[160]; } g_lib;   /* under g_lock */

/* every video in your TorBox, blocking (a network thread), from the list kept 5 minutes */
static int library(TorboxFile **out)
{
    char k[160]; key_copy(k, sizeof k);
    if (!k[0]) return 0;
    pthread_mutex_lock(&g_lock);
    if (g_lib.files && time(NULL) - g_lib.at < 300 && !strcmp(g_lib.key, k)) {
        *out = malloc(sizeof **out * (size_t)(g_lib.n ? g_lib.n : 1));
        memcpy(*out, g_lib.files, sizeof **out * (size_t)g_lib.n);
        int n = g_lib.n;
        pthread_mutex_unlock(&g_lock);
        return n;
    }
    pthread_mutex_unlock(&g_lock);
    TorboxFile *files = NULL; int n = 0, cap = 0, ok = 0;
    char hdr[220]; auth_header(hdr, sizeof hdr);
    for (int t = 0; t < 3; t++) {
        char url[300];
        snprintf(url, sizeof url, "%s/%s/mylist?bypass_cache=true&limit=1000", base(), TYPES[t]);
        HttpOptions o = { .timeout_s = 8, .headers = hdr };
        HttpResponse r;
        http_request_ex(url, &o, &r);
        if (r.status == 200 || r.status == 404) ok = 1;
        cJSON *root = r.status == 200 && r.body ? cJSON_Parse(r.body) : NULL;
        const cJSON *item, *file;
        cJSON_ArrayForEach(item, cJSON_GetObjectItemCaseSensitive(root, "data")) {
            const cJSON *id = cJSON_GetObjectItemCaseSensitive(item, "id"), *in = cJSON_GetObjectItemCaseSensitive(item, "name");
            if (!cJSON_IsNumber(id)) continue;
            cJSON_ArrayForEach(file, cJSON_GetObjectItemCaseSensitive(item, "files")) {
                const cJSON *fid = cJSON_GetObjectItemCaseSensitive(file, "id"), *nm = cJSON_GetObjectItemCaseSensitive(file, "short_name"), *sz = cJSON_GetObjectItemCaseSensitive(file, "size");
                if (!cJSON_IsNumber(fid) || !cJSON_IsString(nm) || !is_video(file)) continue;
                if (n == cap) { cap = cap ? cap * 2 : 32; files = realloc(files, sizeof *files * (size_t)cap); }
                TorboxFile *f = &files[n++];
                snprintf(f->name, sizeof f->name, "%s", nm->valuestring);
                snprintf(f->item, sizeof f->item, "%s", cJSON_IsString(in) ? in->valuestring : "");
                snprintf(f->link, sizeof f->link, "torbox://%s/%.0f/%.0f/%s", TYPES[t], id->valuedouble, fid->valuedouble, nm->valuestring);
                f->size_mb = cJSON_IsNumber(sz) ? sz->valuedouble / 1048576.0 : 0;
            }
        }
        cJSON_Delete(root);
        http_response_free(&r);
    }
    if (!ok) { free(files); return 0; }
    pthread_mutex_lock(&g_lock);
    free(g_lib.files);
    g_lib.files = files; g_lib.n = n; g_lib.at = time(NULL);
    snprintf(g_lib.key, sizeof g_lib.key, "%s", k);
    *out = malloc(sizeof **out * (size_t)(n ? n : 1));
    if (n) memcpy(*out, files, sizeof **out * (size_t)n);
    pthread_mutex_unlock(&g_lock);
    return n;
}

/* The file's own download link (requestdl without the redirect): the TV opens it directly,
 * one hop less than the permanent link. Blocking; NULL when TorBox did not give one. */
static char *direct_link(const char *permanent)
{
    char url[900];
    snprintf(url, sizeof url, "%s", permanent);
    char *r = strstr(url, "&redirect=true");
    if (r) memmove(r, r + 14, strlen(r + 14) + 1);
    HttpOptions o = { .timeout_s = 8 };
    HttpResponse resp;
    http_request_ex(url, &o, &resp);
    cJSON *root = resp.status == 200 && resp.body ? cJSON_Parse(resp.body) : NULL;
    const cJSON *d = cJSON_GetObjectItemCaseSensitive(root, "data");
    char *out = cJSON_IsString(d) && !strncmp(d->valuestring, "http", 4) ? strdup(d->valuestring) : NULL;
    cJSON_Delete(root);
    http_response_free(&resp);
    return out;
}

/* lowercase words separated by single spaces, with a space at each end: " the matrix 1999 " */
static void words(const char *in, char *out, size_t n)
{
    size_t o = 0;
    if (n < 3) { if (n) out[0] = 0; return; }
    out[o++] = ' ';
    for (const unsigned char *p = (const unsigned char *)in; *p && o + 2 < n; p++) {
        if (*p == '\'') continue;   /* "Schindler's" matches "Schindlers" */
        if (isalnum(*p) || *p >= 0x80) out[o++] = (char)tolower(*p);
        else if (out[o - 1] != ' ') out[o++] = ' ';
    }
    if (out[o - 1] != ' ') out[o++] = ' ';
    out[o] = 0;
}

/* S02E03, s2e3, 2x03: 1 when the text names this episode; season or episode -1 for any */
static int names_episode(const char *w, int season, int episode)
{
    for (const char *p = w; *p; p++) {
        int s, e, len = 0;
        if ((sscanf(p, "s%de%d%n", &s, &e, &len) == 2 || sscanf(p, "%dx%d%n", &s, &e, &len) == 2) && (p == w || p[-1] == ' ')
            && (season < 0 || s == season) && (episode < 0 || e == episode)) return 1;
    }
    return 0;
}

/* The title as the whole name, not the start of another: what follows it is a year, an
 * episode or season, a quality or source tag, or nothing. " ted " is not in
 * " ted lasso s01e05 ", but is in " ted 2024 s01e05 " and " ted s01e05 1080p ". */
static int title_in(const char *w, const char *title)
{
    static const char *const TAGS[] = { "2160p", "1080p", "720p", "576p", "480p", "4k", "uhd", "hdr", "dv", "bluray", "blu", "bdrip", "brrip",
        "web", "webrip", "webdl", "hdtv", "dvdrip", "dvd", "remux", "proper", "repack", "extended", "unrated", "directors", "complete",
        "multi", "internal", "imax", "season", "series", "x264", "x265", "h264", "h265", "hevc", "avc", "mkv", "mp4", NULL };
    size_t tl = strlen(title);
    for (const char *p = strstr(w, title); p; p = strstr(p + 1, title)) {
        const char *n = p + tl;   /* title ends with a space: n starts the next word */
        if (!*n) return 1;
        int len = 0, a, b;
        if ((n[0] == '1' || n[0] == '2') && isdigit((unsigned char)n[1]) && isdigit((unsigned char)n[2]) && isdigit((unsigned char)n[3]) && n[4] == ' ') return 1;   /* a year */
        if ((sscanf(n, "s%d%n", &a, &len) == 1 && (n[len] == ' ' || n[len] == 'e')) || sscanf(n, "%dx%d", &a, &b) == 2) return 1;   /* s01, s01e05, 1x05 */
        for (int t = 0; TAGS[t]; t++) { size_t k = strlen(TAGS[t]); if (!strncmp(n, TAGS[t], k) && n[k] == ' ') return 1; }
    }
    return 0;
}

/* the first year in the text (1900..2099), or 0 */
static int year_in(const char *w)
{
    for (const char *p = w; *p; p++)
        if ((p == w || p[-1] == ' ') && (p[0] == '1' || p[0] == '2') && isdigit((unsigned char)p[1]) && isdigit((unsigned char)p[2]) && isdigit((unsigned char)p[3]) && p[4] == ' ') {
            int y = atoi(p);
            if (y >= 1900 && y < 2100) return y;
        }
    return 0;
}

int torbox_library_streams(const char *type, const char *video_id, const char *name, const char *year,
                           const char *own, const char *const *others, int n_others, Stream **out)
{
    *out = NULL;
    if (!name || !*name || !video_id) return 0;
    int series = type && !strcmp(type, "series"), season = 0, episode = 0;
    if (series) { const char *c = strchr(video_id, ':'); if (!c || sscanf(c, ":%d:%d", &season, &episode) != 2) return 0; }
    TorboxFile *files = NULL;
    int n = library(&files), got = 0;
    char title[300];
    words(name, title, sizeof title);
    int want_year = year ? atoi(year) : 0;
    for (int i = 0; i < n; i++) {
        char fw[600], iw[600];
        words(files[i].name, fw, sizeof fw);
        words(files[i].item, iw, sizeof iw);
        if (!title_in(fw, title) && !title_in(iw, title)) continue;   /* the title's words, together, as the whole name */
        /* the file's own name decides when it names an episode: a season pack called
         * "S01E01-E08" names the first, and every file in it would pass for that one */
        if (series && (names_episode(fw, -1, -1) ? !names_episode(fw, season, episode) : !names_episode(iw, season, episode))) continue;
        if (series) {   /* "S01E05.Subway.Bicycles..." when episode 5 is another title: it is that other episode */
            char ow[200] = "", xw[200];
            if (own && strlen(own) >= 5) words(own, ow, sizeof ow);
            int other = -1;
            for (int k = 0; k < n_others && other < 0; k++) {
                if (!others[k] || strlen(others[k]) < 5) continue;
                words(others[k], xw, sizeof xw);
                if (strlen(xw) > 6 && strstr(fw, xw) && !(ow[0] && strstr(fw, ow))) other = k;
            }
            if (other >= 0) { LOGI("torbox: %s names \"%s\", another episode: left out", files[i].name, others[other]); continue; }
        }
        if (!series && want_year) {   /* a remake is not the film: when a year is written, it is this one (or one off) */
            int y = year_in(fw); if (!y) y = year_in(iw);
            if (y && (y < want_year - 1 || y > want_year + 1)) continue;
        }
        char url[800];
        char k[160]; key_copy(k, sizeof k);
        if (!resolve_key(files[i].link, k, url, sizeof url)) continue;
        *out = realloc(*out, sizeof **out * (size_t)(got + 1));
        Stream *s = &(*out)[got++];
        memset(s, 0, sizeof *s);
        char *direct = direct_link(url);
        s->url = direct ? direct : strdup(url);
        s->name = strdup("Your copy");
        s->title = strdup(files[i].name);
        s->filename = strdup(files[i].name);
        s->file_idx = -1;
        s->video_size = (uint64_t)(files[i].size_mb * 1048576.0);
        quality_parse(s->name, s->title, NULL, s->filename, s->video_size, &s->tags);
        s->tags.cached = 1;
    }
    free(files);
    if (got) LOGI("torbox: %d of your videos match%s %s", got, got == 1 ? "es" : "", video_id);
    return got;
}
