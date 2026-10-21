#include "advisory.h"
#include "http.h"
#include "log.h"
#include "../vendor/cJSON.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CACHE 32

const char *const ADVISORY_THEMES[ADV_THEMES] = { "nudity", "violence", "language", "drugs", "frightening scenes" };

static struct { char id[24]; Advisory a; int asked; } g_cache[CACHE];
static int g_next;

static int imdb_part(const char *id, char *out, size_t n)
{
    if (!id || strncmp(id, "tt", 2) != 0) return 0;
    snprintf(out, n, "%.*s", (int)strcspn(id, ":"), id);
    return 1;
}

static int slot_of(const char *imdb)
{
    for (int i = 0; i < CACHE; i++) if (g_cache[i].asked && !strcmp(g_cache[i].id, imdb)) return i;
    return -1;
}

/* IMDb's category names, matched loosely so a reworded label still lands */
static int theme_of(const char *text)
{
    if (!text) return -1;
    if (strstr(text, "Sex") || strstr(text, "Nudity")) return 0;
    if (strstr(text, "Violence")) return 1;
    if (strstr(text, "Profanity")) return 2;
    if (strstr(text, "Alcohol") || strstr(text, "Drugs")) return 3;
    if (strstr(text, "Frightening")) return 4;
    return -1;
}

static int level_of(const char *text)
{
    if (!text) return 0;
    return !strcmp(text, "Severe") ? 3 : !strcmp(text, "Moderate") ? 2 : !strcmp(text, "Mild") ? 1 : 0;
}

static void on_guide(HttpResponse *r, void *user)
{
    int slot = (int)(intptr_t)user;
    Advisory *a = &g_cache[slot].a;
    cJSON *root = r->status == 200 && r->body ? cJSON_Parse(r->body) : NULL;
    const cJSON *cats = cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(
                            cJSON_GetObjectItemCaseSensitive(root, "data"), "title"), "parentsGuide"), "categories");
    const cJSON *c;
    int rated = 0;
    cJSON_ArrayForEach(c, cats) {
        const cJSON *cat = cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(c, "category"), "text");
        const cJSON *sev = cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(c, "severity"), "text");
        int t = theme_of(cJSON_IsString(cat) ? cat->valuestring : NULL);
        if (t < 0) continue;
        a->level[t] = level_of(cJSON_IsString(sev) ? sev->valuestring : NULL);
        rated += a->level[t] > 0;
    }
    a->ready = 1;
    char d[160];
    advisory_describe(a, d, sizeof d);
    LOGI("advisory: %s -> %s (%ld)", g_cache[slot].id, rated ? d : "nothing rated", r->status);
    cJSON_Delete(root);
}

void advisory_fetch(const char *id)
{
    char imdb[24];
    if (!imdb_part(id, imdb, sizeof imdb) || slot_of(imdb) >= 0) return;
    int slot = g_next;
    g_next = (g_next + 1) % CACHE;
    memset(&g_cache[slot], 0, sizeof g_cache[slot]);
    snprintf(g_cache[slot].id, sizeof g_cache[slot].id, "%s", imdb);
    g_cache[slot].asked = 1;
    char body[300];
    snprintf(body, sizeof body, "{\"query\":\"query($id: ID!){title(id:$id){parentsGuide{categories{category{text} severity{text}}}}}\",\"variables\":{\"id\":\"%s\"}}", imdb);
    const char *api = getenv("SNTV_IMDB_API");
    HttpOptions o = { .post_json = body, .timeout_s = 12,
                      .headers = "User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/135.0.0.0 Safari/537.36\nReferer: https://www.imdb.com/\nOrigin: https://www.imdb.com" };
    http_request_async_ex(api && *api ? api : "https://graphql.prod.api.imdb.a2z.com/", &o, on_guide, (void *)(intptr_t)slot);
}

const Advisory *advisory_get(const char *id)
{
    char imdb[24];
    if (!imdb_part(id, imdb, sizeof imdb)) return NULL;
    int slot = slot_of(imdb);
    return slot >= 0 && g_cache[slot].a.ready ? &g_cache[slot].a : NULL;
}

void advisory_describe(const Advisory *a, char *out, int n)
{
    static const char *const WORD[4] = { "", "mild", "moderate", "severe" };
    int len = 0;
    out[0] = 0;
    for (int lvl = 3; lvl >= 1; lvl--)
        for (int t = 0; t < ADV_THEMES; t++)
            if (a->level[t] == lvl && len < n)
                len += snprintf(out + len, (size_t)(n - len), "%s%s %s", len ? ", " : "", WORD[lvl], ADVISORY_THEMES[t]);
    if (len) out[0] = (char)(out[0] - 'a' + 'A');
}
