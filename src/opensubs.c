#include "builtin_keys.h"
#include "keyrelay.h"
#include "opensubs.h"
#include "app.h"
#include "http.h"
#include "lang.h"
#include "log.h"
#include "store.h"
#include "platform/platform.h"
#include "../vendor/cJSON.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *api(void) { const char *e = getenv("SNTV_OPENSUBS_API"); return e && *e ? e : "https://api.opensubtitles.com/api/v1"; }

static const char *api_key(void)
{
    const char *k = store_get_str("osApiKey", "");
    if (k && *k) return k;
#ifdef SNTV_OPENSUBTITLES_KEY
    return SNTV_OPENSUBTITLES_KEY;
#else
    return "";
#endif
}

/* Where a request goes: without a key of the viewer's own, through the key relay, which adds
   the app's (via set); otherwise to OpenSubtitles with the key. */
static const char *base(int *via)
{
    const char *k = store_get_str("osApiKey", "");
    const char *r = k && *k ? NULL : keyrelay_base(KR_OPENSUBS);
    *via = r != NULL;
    return r ? r : api();
}
int opensubs_enabled(void) { int via; base(&via); return via || api_key()[0] != 0; }
static const char *token(void) { const char *t = store_get_str("osToken", ""); return t ? t : ""; }
int opensubs_signed_in(void) { return token()[0] != 0; }
const char *opensubs_user(void) { const char *u = store_get_str("osUser", ""); return u ? u : ""; }

static void headers(char *out, size_t n, int via)
{
    if (via) snprintf(out, n, "User-Agent: StremioNativeTV v%s\nAccept: application/json", SNTV_APP_VERSION);
    else snprintf(out, n, "Api-Key: %s\nUser-Agent: StremioNativeTV v%s\nAccept: application/json", api_key(), SNTV_APP_VERSION);
}

int opensubs_login(const char *user, const char *password, char *err, size_t en)
{
    if (!user) { store_set_str("osToken", NULL); store_set_str("osUser", NULL); store_save(); return 1; }
    cJSON *b = cJSON_CreateObject();
    cJSON_AddStringToObject(b, "username", user);
    cJSON_AddStringToObject(b, "password", password ? password : "");
    char *body = cJSON_PrintUnformatted(b);
    cJSON_Delete(b);
    HttpResponse r;
    for (int again = 1; ; again = 0) {
        int via;
        char url[300], hdr[400];
        snprintf(url, sizeof url, "%s/login", base(&via));
        headers(hdr, sizeof hdr, via);
        HttpOptions o = { .post_json = body, .headers = hdr, .timeout_s = 15 };
        http_request_ex(url, &o, &r);
        if (!(again && via && keyrelay_failed(KR_OPENSUBS, r.status) && api_key()[0])) break;
        http_response_free(&r);   /* the relay can't: once more, direct */
    }
    free(body);
    cJSON *j = r.status == 200 ? cJSON_Parse(r.body) : NULL;
    const cJSON *t = cJSON_GetObjectItemCaseSensitive(j, "token");
    int ok = cJSON_IsString(t) && t->valuestring[0];
    if (ok) { store_set_str("osToken", t->valuestring); store_set_str("osUser", user); store_save(); }
    else snprintf(err, en, r.status == 401 ? "wrong username or password" : r.status ? "OpenSubtitles answered %ld" : "OpenSubtitles did not answer", r.status);
    LOGI("opensubs: sign-in %s", ok ? "ok" : "failed");
    cJSON_Delete(j);
    http_response_free(&r);
    return ok;
}

/* ---- search ---- */
typedef struct { OsSearchCb cb; void *user; int via; char path[500]; } SearchReq;

static void on_search(HttpResponse *r, void *user);
static void search_send(SearchReq *q)
{
    char url[800], hdr[400];
    snprintf(url, sizeof url, "%s%s", base(&q->via), q->path);
    headers(hdr, sizeof hdr, q->via);
    HttpOptions o = { .headers = hdr, .timeout_s = 15, .bearer = opensubs_signed_in() ? token() : NULL };
    http_request_async_ex(url, &o, on_search, q);
}

static void on_search(HttpResponse *r, void *user)
{
    SearchReq *q = user;
    if (q->via && keyrelay_failed(KR_OPENSUBS, r->status) && api_key()[0]) { search_send(q); return; }   /* again, direct */
    OsSub *subs = NULL; int n = 0;
    cJSON *j = r->status == 200 ? cJSON_Parse(r->body) : NULL;
    const cJSON *data = cJSON_GetObjectItemCaseSensitive(j, "data"), *it;
    if (cJSON_IsArray(data)) {
        subs = calloc((size_t)cJSON_GetArraySize(data) + 1, sizeof *subs);
        cJSON_ArrayForEach(it, data) {
            const cJSON *a = cJSON_GetObjectItemCaseSensitive(it, "attributes");
            const cJSON *files = cJSON_GetObjectItemCaseSensitive(a, "files");
            const cJSON *f0 = cJSON_IsArray(files) ? cJSON_GetArrayItem(files, 0) : NULL;
            const cJSON *fid = cJSON_GetObjectItemCaseSensitive(f0, "file_id");
            const cJSON *lg = cJSON_GetObjectItemCaseSensitive(a, "language"), *rel = cJSON_GetObjectItemCaseSensitive(a, "release");
            const cJSON *hm = cJSON_GetObjectItemCaseSensitive(a, "moviehash_match"), *dl = cJSON_GetObjectItemCaseSensitive(a, "download_count");
            if (!cJSON_IsNumber(fid) || !cJSON_IsString(lg)) continue;
            OsSub *s = &subs[n];
            s->file_id = (long)fid->valuedouble;
            const char *c = lang_canonical(lg->valuestring);
            snprintf(s->lang, sizeof s->lang, "%s", !strcasecmp(lg->valuestring, "pt-br") ? "pob" : c ? c : lg->valuestring);
            if (cJSON_IsString(rel)) snprintf(s->release, sizeof s->release, "%s", rel->valuestring);
            s->hash_match = cJSON_IsTrue(hm);
            s->downloads = cJSON_IsNumber(dl) ? dl->valueint : 0;
            n++;
        }
    } else if (r->status) LOGW("opensubs: search answered %ld", r->status);
    LOGI("opensubs: %d subtitles found", n);
    q->cb(subs, n, q->user);
    free(subs);
    cJSON_Delete(j);
    free(q);
}

void opensubs_search(const char *imdb, int season, int episode, const char *hash, const char *langs, OsSearchCb cb, void *user)
{
    if (!opensubs_enabled() || !imdb || strncmp(imdb, "tt", 2)) { cb(NULL, 0, user); return; }
    long id = atol(imdb + 2);
    SearchReq *q = malloc(sizeof *q);
    q->cb = cb; q->user = user;
    /* the API wants its parameters in alphabetical order (otherwise it redirects) */
    if (season >= 0)
        snprintf(q->path, sizeof q->path, "/subtitles?episode_number=%d&languages=%s%s%s&parent_imdb_id=%ld&season_number=%d",
                 episode, langs, hash && *hash ? "&moviehash=" : "", hash && *hash ? hash : "", id, season);
    else
        snprintf(q->path, sizeof q->path, "/subtitles?imdb_id=%ld&languages=%s%s%s", id, langs, hash && *hash ? "&moviehash=" : "", hash && *hash ? hash : "");
    search_send(q);
}

/* ---- download links ---- */
typedef struct { OsLinkCb cb; void *user; long id; int via; } LinkReq;
static struct { long id; char url[600]; } g_links[16];
static int g_link_next;

static void on_link(HttpResponse *r, void *user);
static void link_send(LinkReq *q)
{
    char url[300], hdr[400], body[64];
    snprintf(url, sizeof url, "%s/download", base(&q->via));
    snprintf(body, sizeof body, "{\"file_id\":%ld}", q->id);
    headers(hdr, sizeof hdr, q->via);
    HttpOptions o = { .post_json = body, .headers = hdr, .timeout_s = 15, .bearer = opensubs_signed_in() ? token() : NULL };
    http_request_async_ex(url, &o, on_link, q);
}

static void on_link(HttpResponse *r, void *user)
{
    LinkReq *q = user;
    if (q->via && keyrelay_failed(KR_OPENSUBS, r->status) && api_key()[0]) { link_send(q); return; }   /* again, direct */
    cJSON *j = r->status == 200 ? cJSON_Parse(r->body) : NULL;
    const cJSON *l = cJSON_GetObjectItemCaseSensitive(j, "link"), *rem = cJSON_GetObjectItemCaseSensitive(j, "remaining");
    if (cJSON_IsString(l)) {
        int k = g_link_next; g_link_next = (g_link_next + 1) % 16;
        g_links[k].id = q->id; snprintf(g_links[k].url, sizeof g_links[k].url, "%s", l->valuestring);
        if (cJSON_IsNumber(rem)) LOGI("opensubs: file %ld, %d downloads left today", q->id, rem->valueint);
        q->cb(l->valuestring, NULL, q->user);
    } else {
        const cJSON *m = cJSON_GetObjectItemCaseSensitive(j, "message");
        char why[200];
        if (r->status == 406 || r->status == 429) snprintf(why, sizeof why, "today's OpenSubtitles downloads are used up%s", opensubs_signed_in() ? "" : " (signing in to a free account gives more)");
        else snprintf(why, sizeof why, "%s", cJSON_IsString(m) ? m->valuestring : "OpenSubtitles refused the download");
        LOGW("opensubs: download %ld: %ld %s", q->id, r->status, why);
        q->cb(NULL, why, q->user);
    }
    cJSON_Delete(j);
    free(q);
}

void opensubs_link(long file_id, OsLinkCb cb, void *user)
{
    for (int i = 0; i < 16; i++) if (g_links[i].id == file_id) { cb(g_links[i].url, NULL, user); return; }   /* already counted today */
    LinkReq *q = malloc(sizeof *q);
    q->cb = cb; q->user = user; q->id = file_id;
    link_send(q);
}

/* ---- release names ---- */
static int tokens(const char *s, char out[][24], int max)
{
    int n = 0; char cur[24]; size_t l = 0;
    for (const char *c = s; ; c++) {
        if (*c && (isalnum((unsigned char)*c))) { if (l < sizeof cur - 1) cur[l++] = (char)tolower((unsigned char)*c); continue; }
        if (l) { cur[l] = 0; if (n < max) snprintf(out[n++], 24, "%s", cur); l = 0; }
        if (!*c) break;
    }
    return n;
}

static int has(char t[][24], int n, const char *w) { for (int i = 0; i < n; i++) if (!strcmp(t[i], w)) return 1; return 0; }

/* the group: after the last '-' of the name, before the extension */
static void group_of(const char *s, char *out, size_t n)
{
    out[0] = 0;
    if (!s) return;
    char b[200]; snprintf(b, sizeof b, "%s", s);
    char *dot = strrchr(b, '.');
    if (dot && strlen(dot) <= 5) *dot = 0;
    char *dash = strrchr(b, '-');
    if (!dash || !dash[1] || strchr(dash, ' ') || strlen(dash + 1) > 20) return;
    snprintf(out, n, "%s", dash + 1);
    for (char *c = out; *c; c++) *c = (char)tolower((unsigned char)*c);
}

int opensubs_name_score(const char *release, const char *filename)
{
    if (!release || !*release || !filename || !*filename) return 0;
    char a[40][24], b[40][24];
    int na = tokens(release, a, 40), nb = tokens(filename, b, 40);
    int score = 0;
    char ga[32], gb[32];
    group_of(release, ga, sizeof ga); group_of(filename, gb, sizeof gb);
    if (ga[0] && gb[0] && !strcmp(ga, gb)) score += 45;   /* the same group: the same encode */
    static const char *const SRC[][3] = { { "bluray", "bdrip", "brrip" }, { "web", "webdl", "webrip" }, { "hdtv", "hdtvrip", "pdtv" }, { "dvdrip", "dvd", "dvdscr" } };
    for (int k = 0; k < 4; k++) {
        int ia = 0, ib = 0;
        for (int m = 0; m < 3; m++) { ia |= has(a, na, SRC[k][m]); ib |= has(b, nb, SRC[k][m]); }
        if (ia && ib) { score += 20; break; }
    }
    static const char *const RES[] = { "2160p", "1080p", "720p", "480p" };
    for (int k = 0; k < 4; k++) if (has(a, na, RES[k]) && has(b, nb, RES[k])) { score += 5; break; }
    static const char *const SVC[] = { "amzn", "nf", "dsnp", "hmax", "atvp", "hulu", "pcok", "max" };
    for (int k = 0; k < 8; k++) if (has(a, na, SVC[k]) && has(b, nb, SVC[k])) { score += 15; break; }
    static const char *const ED[] = { "extended", "uncut", "directors", "theatrical", "remastered", "imax", "unrated" };
    for (int k = 0; k < 7; k++) { int ia = has(a, na, ED[k]), ib = has(b, nb, ED[k]); if (ia && ib) score += 15; else if (ia != ib) score -= 15; }
    return score < 0 ? 0 : score > 100 ? 100 : score;
}
