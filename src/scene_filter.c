#include "scene_filter.h"
#include "cJSON.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { PAD_BEFORE_MS = 1000, PAD_AFTER_MS = 500, MERGE_GAP_MS = 1500 };

int scene_filter_url(const char *base, const char *video_id, char *url, size_t n)
{
    if (!video_id || strncmp(video_id, "tt", 2) || !video_id[2] || strchr(video_id, ':') || strlen(video_id) > 20) return -1;
    const char *env = getenv("SNTV_SCENES_API");
    if (env && *env) base = env;
    if (!base || !*base) base = FILTER_DEFAULT_BASE;
    size_t bl = strlen(base);
    while (bl && base[bl - 1] == '/') bl--;
    if (!bl) return -1;
    snprintf(url, n, "%.*s/api/skips/%s", (int)bl, base, video_id);
    return 0;
}

static int category_of(const char *s)
{
    if (!s) return 0;
    if (!strcmp(s, "nudity") || !strcmp(s, "sex")) return FCAT_NUDITY;
    if (!strcmp(s, "violence") || !strcmp(s, "gore")) return FCAT_VIOLENCE;
    if (!strcmp(s, "language") || !strcmp(s, "profanity")) return FCAT_LANGUAGE;
    if (!strcmp(s, "drugs")) return FCAT_DRUGS;
    if (!strcmp(s, "fear")) return FCAT_FEAR;
    return 0;
}

static int severity_of(const char *s)
{
    if (s && !strcmp(s, "low")) return FSEV_LOW;
    if (s && !strcmp(s, "medium")) return FSEV_MEDIUM;
    return FSEV_HIGH;   /* high, or not said: the safe side */
}

static int by_start(const void *a, const void *b)
{
    double x = ((const FilterSeg *)a)->start, y = ((const FilterSeg *)b)->start;
    return x < y ? -1 : x > y;
}

int scene_filter_parse(const char *json, int cats, int min_sev, FilterSeg *out, int max)
{
    cJSON *root = json ? cJSON_Parse(json) : NULL;
    if (!root) return -1;
    cJSON *skips = cJSON_GetObjectItem(root, "skips");
    if (!cJSON_IsArray(skips)) { cJSON_Delete(root); return -1; }
    int n = 0;
    cJSON *it;
    cJSON_ArrayForEach(it, skips) {
        cJSON *s = cJSON_GetObjectItem(it, "startMs"), *e = cJSON_GetObjectItem(it, "endMs");
        cJSON *cat = cJSON_GetObjectItem(it, "category"), *sev = cJSON_GetObjectItem(it, "severity"), *ch = cJSON_GetObjectItem(it, "channel");
        if (!cJSON_IsNumber(s) || !cJSON_IsNumber(e) || e->valuedouble <= s->valuedouble) continue;
        int c = category_of(cJSON_IsString(cat) ? cat->valuestring : NULL);
        if (!(c & cats) || severity_of(cJSON_IsString(sev) ? sev->valuestring : NULL) < min_sev) continue;
        if (n >= max || n >= FILTER_MAX) break;
        double a = s->valuedouble - PAD_BEFORE_MS, b = e->valuedouble + PAD_AFTER_MS;
        out[n].start = (a < 0 ? 0 : a) / 1000.0;
        out[n].end = b / 1000.0;
        out[n].cat = c;
        out[n].audio_only = cJSON_IsString(ch) && !strcmp(ch->valuestring, "audio");
        n++;
    }
    cJSON_Delete(root);
    qsort(out, (size_t)n, sizeof *out, by_start);
    int m = 0;   /* overlapping or nearly touching ones become one (a skip then a skip a second later is one cut) */
    for (int i = 0; i < n; i++) {
        if (m && out[i].start <= out[m - 1].end + MERGE_GAP_MS / 1000.0 && out[i].audio_only == out[m - 1].audio_only) {
            if (out[i].end > out[m - 1].end) out[m - 1].end = out[i].end;
            out[m - 1].cat |= out[i].cat;
        } else out[m++] = out[i];
    }
    return m;
}

const FilterSeg *scene_filter_at(const FilterSeg *segs, int n, double pos)
{
    for (int i = 0; i < n; i++)
        if (pos >= segs[i].start && pos < segs[i].end) return &segs[i];
    return NULL;
}
