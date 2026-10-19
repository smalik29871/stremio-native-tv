/* Your lists (lists.h). */
#include "lists.h"
#include "store.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char *const LIST_NAMES[LIST_COUNT] = { "Favourites", "Watch later", "With the family", "Rewatch" };

static char g_key[160] = "lists";
static int g_version;

void lists_set_owner(const char *email)
{
    if (email && *email) snprintf(g_key, sizeof g_key, "lists:%s", email);
    else snprintf(g_key, sizeof g_key, "lists");
    g_version++;
}

int lists_version(void) { return g_version; }

/* the list's array; made when `make` */
static cJSON *list_arr(int list, int make)
{
    if (list < 0 || list >= LIST_COUNT) return NULL;
    cJSON *root = store_get_json(g_key);
    if (!cJSON_IsObject(root)) {
        if (!make) return NULL;
        store_set_json(g_key, cJSON_CreateObject());
        root = store_get_json(g_key);
        if (!root) return NULL;
    }
    cJSON *arr = cJSON_GetObjectItemCaseSensitive(root, LIST_NAMES[list]);
    if (!cJSON_IsArray(arr)) {
        if (!make) return NULL;
        cJSON_DeleteItemFromObjectCaseSensitive(root, LIST_NAMES[list]);
        arr = cJSON_AddArrayToObject(root, LIST_NAMES[list]);
    }
    return arr;
}

static cJSON *find(cJSON *arr, const char *id)
{
    cJSON *it;
    cJSON_ArrayForEach(it, arr) {
        const cJSON *v = cJSON_GetObjectItemCaseSensitive(it, "id");
        if (cJSON_IsString(v) && strcmp(v->valuestring, id) == 0) return it;
    }
    return NULL;
}

int lists_has(int list, const char *id)
{
    cJSON *arr = list_arr(list, 0);
    return arr && id && find(arr, id) != NULL;
}

int lists_count(int list)
{
    cJSON *arr = list_arr(list, 0);
    return arr ? cJSON_GetArraySize(arr) : 0;
}

int lists_toggle(int list, const MetaPreview *m)
{
    if (!m || !m->id) return -1;
    cJSON *arr = list_arr(list, 1);
    if (!arr) return -1;
    int added;
    cJSON *it = find(arr, m->id);
    if (it) { cJSON_DetachItemViaPointer(arr, it); cJSON_Delete(it); added = 0; }
    else {
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "id", m->id);
        if (m->type) cJSON_AddStringToObject(o, "type", m->type);
        if (m->name) cJSON_AddStringToObject(o, "name", m->name);
        if (m->poster) cJSON_AddStringToObject(o, "poster", m->poster);
        cJSON_InsertItemInArray(arr, 0, o);   /* newest first */
        while (cJSON_GetArraySize(arr) > LIST_MAX) cJSON_DeleteItemFromArray(arr, cJSON_GetArraySize(arr) - 1);
        added = 1;
    }
    store_save();
    g_version++;
    return added;
}

static char *dup_str(const cJSON *o, const char *k)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, k);
    return cJSON_IsString(v) ? strdup(v->valuestring) : NULL;
}

int lists_items(int list, MetaPreview **out)
{
    *out = NULL;
    cJSON *arr = list_arr(list, 0);
    int cap = arr ? cJSON_GetArraySize(arr) : 0;
    if (!cap) return 0;
    MetaPreview *m = calloc((size_t)cap, sizeof *m);
    int n = 0;
    const cJSON *it;
    cJSON_ArrayForEach(it, arr) {
        if (n >= cap) break;
        MetaPreview *p = &m[n];
        p->id = dup_str(it, "id"); p->type = dup_str(it, "type"); p->name = dup_str(it, "name"); p->poster = dup_str(it, "poster");
        if (p->id && p->type && p->name) n++; else meta_preview_free(p);
    }
    *out = m;
    return n;
}
