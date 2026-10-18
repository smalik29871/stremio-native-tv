#include "skip.h"
#include "cJSON.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *base(const char *env, const char *def)
{
    const char *v = getenv(env);
    return v && *v ? v : def;
}

int skip_parse_video_id(const char *video_id, SkipId *out)
{
    memset(out, 0, sizeof *out);
    out->season = out->episode = -1;
    if (!video_id) return -1;
    char buf[96];
    snprintf(buf, sizeof buf, "%s", video_id);
    char *parts[4] = { 0 };
    int n = 0;
    for (char *s = buf; s && n < 4; ) {
        parts[n++] = s;
        s = strchr(s, ':');
        if (s) *s++ = 0;
    }
    if (n >= 1 && !strncmp(parts[0], "tt", 2) && parts[0][2]) {
        out->src = SKIP_SRC_IMDB;
        if (strlen(parts[0]) >= sizeof out->id) return -1;
        snprintf(out->id, sizeof out->id, "%.31s", parts[0]);
        if (n == 3) { out->season = atoi(parts[1]); out->episode = atoi(parts[2]); if (out->episode < 1) return -1; }
        else if (n != 1) return -1;
        return 0;
    }
    if (n >= 2 && (!strcmp(parts[0], "kitsu") || !strcmp(parts[0], "mal")) && atol(parts[1]) > 0) {
        out->src = parts[0][0] == 'k' ? SKIP_SRC_KITSU : SKIP_SRC_MAL;
        if (strlen(parts[1]) >= sizeof out->id) return -1;
        snprintf(out->id, sizeof out->id, "%.31s", parts[1]);
        /* kitsu:<id>:<episode>; a bare kitsu:<id> is a movie or a single special */
        out->season = 1;
        out->episode = n >= 3 ? atoi(parts[n - 1]) : 1;
        return out->episode >= 1 ? 0 : -1;
    }
    return -1;
}

void skip_url_introdb(const SkipId *id, double duration, char *url, size_t n)
{
    int k = snprintf(url, n, "%s/media?imdb_id=%s", base("SNTV_INTRODB_API", "https://api.theintrodb.org/v4"), id->id);
    if (k > 0 && (size_t)k < n && id->episode > 0) k += snprintf(url + k, n - (size_t)k, "&season=%d&episode=%d", id->season, id->episode);
    if (k > 0 && (size_t)k < n && duration > 1) snprintf(url + k, n - (size_t)k, "&duration_ms=%.0f", duration * 1000.0);
}

void skip_url_aniskip(long mal_id, int episode, double duration, char *url, size_t n)
{
    snprintf(url, n, "%s/skip-times/%ld/%d?types[]=op&types[]=ed&types[]=recap&episodeLength=%.0f",
             base("SNTV_ANISKIP_API", "https://api.aniskip.com/v2"), mal_id, episode, duration > 1 ? duration : 0.0);
}

void skip_url_kitsu_mappings(const char *kitsu_id, char *url, size_t n)
{
    snprintf(url, n, "%s/anime/%s/mappings?filter[externalSite]=myanimelist/anime",
             base("SNTV_KITSU_API", "https://kitsu.io/api/edge"), kitsu_id);
}

/* ms value: number, or null/missing -> -1 */
static double ms(const cJSON *o, const char *key)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, key);
    return cJSON_IsNumber(v) ? v->valuedouble / 1000.0 : -1;
}

static int add_introdb(const cJSON *it, SkipKind kind, SkipSeg *out, int n, int max)
{
    double a = ms(it, "start_ms"), b = ms(it, "end_ms");
    if (a <= 0 && b <= 0) return n;          /* no information */
    if (a < 0) a = 0;
    if (b >= 0 && b <= a) return n;          /* empty or reversed */
    if (n < max) out[n++] = (SkipSeg){ a, b < 0 ? 0 : b, kind };
    return n;
}

int skip_parse_introdb(const char *json, int season, int episode, SkipSeg *out, int max)
{
    cJSON *root = cJSON_Parse(json);
    if (!cJSON_IsObject(root)) { cJSON_Delete(root); return 0; }
    if (episode > 0) {
        /* the service can map to a different episode; never trust a mismatch */
        const cJSON *s = cJSON_GetObjectItemCaseSensitive(root, "season"), *e = cJSON_GetObjectItemCaseSensitive(root, "episode");
        if ((cJSON_IsNumber(s) && s->valueint != season) || (cJSON_IsNumber(e) && e->valueint != episode)) { cJSON_Delete(root); return -1; }
    }
    static const struct { const char *key; SkipKind kind; } KEYS[] = {
        { "intro", SKIP_INTRO }, { "recap", SKIP_RECAP }, { "credits", SKIP_CREDITS }, { "preview", SKIP_PREVIEW } };
    int n = 0;
    for (size_t k = 0; k < sizeof KEYS / sizeof KEYS[0]; k++) {
        const cJSON *v = cJSON_GetObjectItemCaseSensitive(root, KEYS[k].key);
        if (cJSON_IsObject(v)) n = add_introdb(v, KEYS[k].kind, out, n, max);   /* tolerate a single object */
        else { const cJSON *it; cJSON_ArrayForEach(it, v) n = add_introdb(it, KEYS[k].kind, out, n, max); }
    }
    cJSON_Delete(root);
    return n;
}

int skip_parse_aniskip(const char *json, double duration, SkipSeg *out, int max)
{
    cJSON *root = cJSON_Parse(json);
    const cJSON *res = cJSON_GetObjectItemCaseSensitive(root, "results"), *it;
    static const struct { const char *type; SkipKind kind; } T[] = { { "op", SKIP_INTRO }, { "recap", SKIP_RECAP }, { "ed", SKIP_CREDITS } };
    SkipSeg best[3];
    double best_diff[3] = { -1, -1, -1 };
    cJSON_ArrayForEach(it, res) {
        const cJSON *iv = cJSON_GetObjectItemCaseSensitive(it, "interval");
        const cJSON *ty = cJSON_GetObjectItemCaseSensitive(it, "skipType");
        const cJSON *a = cJSON_GetObjectItemCaseSensitive(iv, "startTime"), *b = cJSON_GetObjectItemCaseSensitive(iv, "endTime");
        const cJSON *len = cJSON_GetObjectItemCaseSensitive(it, "episodeLength");
        if (!cJSON_IsString(ty) || !cJSON_IsNumber(a) || !cJSON_IsNumber(b) || b->valuedouble <= a->valuedouble || a->valuedouble < 0) continue;
        double l = cJSON_IsNumber(len) ? len->valuedouble : 0;
        double diff = duration > 1 && l > 1 ? fabs(l - duration) / duration : 0;
        if (diff > 0.10) continue;   /* marked on a different cut */
        for (int k = 0; k < 3; k++) {
            if (strcmp(ty->valuestring, T[k].type)) continue;
            if (best_diff[k] < 0 || diff < best_diff[k]) {
                best_diff[k] = diff;
                /* an ending marked to the last half second means "until the end" */
                double end = l > 1 && b->valuedouble >= l - 0.5 ? 0 : b->valuedouble;
                best[k] = (SkipSeg){ a->valuedouble, end, T[k].kind };
            }
        }
    }
    int n = 0;
    for (int k = 0; k < 3 && n < max; k++) if (best_diff[k] >= 0) out[n++] = best[k];
    cJSON_Delete(root);
    return n;
}

long skip_parse_kitsu_mal(const char *json)
{
    cJSON *root = cJSON_Parse(json);
    const cJSON *data = cJSON_GetObjectItemCaseSensitive(root, "data"), *it;
    long mal = 0;
    cJSON_ArrayForEach(it, data) {
        const cJSON *at = cJSON_GetObjectItemCaseSensitive(it, "attributes");
        const cJSON *site = cJSON_GetObjectItemCaseSensitive(at, "externalSite"), *ext = cJSON_GetObjectItemCaseSensitive(at, "externalId");
        if (cJSON_IsString(site) && !strcmp(site->valuestring, "myanimelist/anime") && cJSON_IsString(ext)) { mal = atol(ext->valuestring); break; }
    }
    cJSON_Delete(root);
    return mal;
}

const SkipSeg *skip_find(const SkipSeg *segs, int n, SkipKind kind, double pos, double duration)
{
    for (int i = 0; i < n; i++) {
        if (segs[i].kind != kind) continue;
        double end = segs[i].end > 0 ? segs[i].end : duration;
        if (end <= 0) continue;
        if (pos >= segs[i].start && pos < end) return &segs[i];
    }
    return NULL;
}

double skip_credits_start(const SkipSeg *segs, int n)
{
    double credits = 0, preview = 0;
    for (int i = 0; i < n; i++) {
        if (segs[i].kind == SKIP_CREDITS && segs[i].start > 1 && (credits == 0 || segs[i].start < credits)) credits = segs[i].start;
        if (segs[i].kind == SKIP_PREVIEW && segs[i].start > 1 && (preview == 0 || segs[i].start < preview)) preview = segs[i].start;
    }
    return credits > 0 ? credits : preview;
}
