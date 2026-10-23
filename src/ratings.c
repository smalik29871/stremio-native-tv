#include "ratings.h"
#include "app.h"
#include "log.h"
#include "store.h"
#include "tracking.h"
#include "../vendor/cJSON.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

const char *const RATING_TAGS[RATING_TAGS_N] = { "Loved it", "Great ending", "Rewatchable", "Slow start", "Funny", "Not for me" };

/* settings: "ratings": { "<profile>": { "<id>": { "r": 9, "t": 1729700000, "g": "Loved it|Funny" } } } */
static cJSON *mine(int create)
{
    cJSON *all = store_get_json("ratings");
    if (!all) {
        if (!create) return NULL;
        store_set_json("ratings", cJSON_CreateObject());
        all = store_get_json("ratings");
    }
    const char *who = g_app.user_email && g_app.user_email[0] ? g_app.user_email : "-";
    cJSON *p = cJSON_GetObjectItemCaseSensitive(all, who);
    if (!p && create) { p = cJSON_CreateObject(); cJSON_AddItemToObject(all, who, p); }
    return p;
}

static cJSON *entry(const char *id, int create)
{
    cJSON *p = id ? mine(create) : NULL;
    cJSON *e = p ? cJSON_GetObjectItemCaseSensitive(p, id) : NULL;
    if (!e && create && p) { e = cJSON_CreateObject(); cJSON_AddItemToObject(p, id, e); }
    return e;
}

int rating_get(const char *id)
{
    const cJSON *r = cJSON_GetObjectItemCaseSensitive(entry(id, 0), "r");
    return cJSON_IsNumber(r) ? r->valueint : 0;
}

const char *rating_tags(const char *id)
{
    const cJSON *g = cJSON_GetObjectItemCaseSensitive(entry(id, 0), "g");
    return cJSON_IsString(g) ? g->valuestring : "";
}

static void set_field(cJSON *e, const char *k, cJSON *v)
{
    if (cJSON_GetObjectItemCaseSensitive(e, k)) cJSON_ReplaceItemInObjectCaseSensitive(e, k, v);
    else cJSON_AddItemToObject(e, k, v);
}

void rating_set(const MetaPreview *m, const char *id, int value)
{
    if (!id || !*id) return;
    if (value < 0) value = 0;
    if (value > 10) value = 10;
    int was = rating_get(id);
    if (value == 0) { cJSON *p = mine(0); if (p) cJSON_DeleteItemFromObjectCaseSensitive(p, id); }
    else {
        cJSON *e = entry(id, 1);
        set_field(e, "r", cJSON_CreateNumber(value));
        set_field(e, "t", cJSON_CreateNumber((double)time(NULL)));
    }
    store_save();
    LOGI("ratings: %s %s %d/10", id, value ? "rated" : "unrated", value);
    if (value == was) return;
    int se = -1, ep = -1;
    const char *c = strchr(id, ':');
    if (c) sscanf(c + 1, "%d:%d", &se, &ep);
    char imdb[24];
    snprintf(imdb, sizeof imdb, "%.*s", (int)strcspn(id, ":"), id);
    tracking_rate(imdb, m && m->type ? m->type : (c ? "series" : "movie"), se, ep, value);
    g_app.library_version++;   /* the cards that show it */
}

void rating_set_tags(const char *id, const char *tags)
{
    cJSON *e = id && rating_get(id) ? entry(id, 0) : NULL;   /* tags go with a rating */
    if (!e) return;
    set_field(e, "g", cJSON_CreateString(tags ? tags : ""));
    store_save();
}

int rating_tag_has(const char *tags, const char *tag)
{
    size_t n = strlen(tag);
    for (const char *t = tags; t && *t; ) {
        size_t len = strcspn(t, "|");
        if (len == n && !strncmp(t, tag, n)) return 1;
        t += len; if (*t) t++;
    }
    return 0;
}

void rating_tag_toggle(char *tags, size_t cap, const char *tag)
{
    char out[256] = "";
    int had = rating_tag_has(tags, tag);
    for (const char *t = tags; *t; ) {
        size_t len = strcspn(t, "|");
        if (!(had && len == strlen(tag) && !strncmp(t, tag, len)))
            snprintf(out + strlen(out), sizeof out - strlen(out), "%s%.*s", out[0] ? "|" : "", (int)len, t);
        t += len; if (*t) t++;
    }
    if (!had) snprintf(out + strlen(out), sizeof out - strlen(out), "%s%s", out[0] ? "|" : "", tag);
    snprintf(tags, cap, "%s", out);
}

void rating_text(int value, char *out, size_t n)
{
    if (value % 2) snprintf(out, n, "%d.5", value / 2);
    else snprintf(out, n, "%d", value / 2);
}
