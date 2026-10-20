#include "torbox.h"
#include "http.h"
#include "log.h"
#include "store.h"
#include "cJSON.h"
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

int torbox_enabled(void) { return key()[0] != 0; }
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
