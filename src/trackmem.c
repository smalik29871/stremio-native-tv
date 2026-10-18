#include "trackmem.h"
#include "store.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

enum { TRACKMEM_MAX = 300 };

int trackmem_get(const char *series_id, TrackMem *out)
{
    memset(out, 0, sizeof *out);
    const cJSON *e = series_id ? cJSON_GetObjectItemCaseSensitive(store_get_json("trackMemory"), series_id) : NULL;
    if (!cJSON_IsObject(e)) return 0;
    const cJSON *a = cJSON_GetObjectItemCaseSensitive(e, "a"), *s = cJSON_GetObjectItemCaseSensitive(e, "s");
    const cJSON *src = cJSON_GetObjectItemCaseSensitive(e, "src");
    if (cJSON_IsString(a)) snprintf(out->audio, sizeof out->audio, "%s", a->valuestring);
    if (cJSON_IsString(s)) { out->has_subs = 1; snprintf(out->subs, sizeof out->subs, "%s", s->valuestring); }
    if (cJSON_IsNumber(src) && src->valueint >= SUBS_SRC_ANY && src->valueint <= SUBS_SRC_ADDON) out->subs_src = (SubsSource)src->valueint;
    return out->audio[0] || out->has_subs;
}

static void set_str(cJSON *o, const char *key, const char *v)
{
    cJSON_DeleteItemFromObjectCaseSensitive(o, key);
    cJSON_AddStringToObject(o, key, v);
}

void trackmem_set(const char *series_id, const char *audio, const char *subs, SubsSource src)
{
    if (!series_id || !*series_id || (!audio && !subs)) return;
    cJSON *all = store_get_json("trackMemory");
    if (!cJSON_IsObject(all)) { store_set_json("trackMemory", cJSON_CreateObject()); all = store_get_json("trackMemory"); }
    cJSON *e = cJSON_GetObjectItemCaseSensitive(all, series_id);
    if (!cJSON_IsObject(e)) {
        cJSON_DeleteItemFromObjectCaseSensitive(all, series_id);
        e = cJSON_AddObjectToObject(all, series_id);
    }
    if (audio && *audio) set_str(e, "a", audio);
    if (subs) {
        set_str(e, "s", subs);
        cJSON_DeleteItemFromObjectCaseSensitive(e, "src");
        cJSON_AddNumberToObject(e, "src", src);
    }
    cJSON_DeleteItemFromObjectCaseSensitive(e, "t");
    cJSON_AddNumberToObject(e, "t", (double)time(NULL));
    /* drop the series not touched for longest */
    while (cJSON_GetArraySize(all) > TRACKMEM_MAX) {
        cJSON *it, *oldest = NULL;
        double ot = 0;
        cJSON_ArrayForEach(it, all) {
            const cJSON *t = cJSON_GetObjectItemCaseSensitive(it, "t");
            double tv = cJSON_IsNumber(t) ? t->valuedouble : 0;
            if (!oldest || tv < ot) { oldest = it; ot = tv; }
        }
        cJSON_Delete(cJSON_DetachItemViaPointer(all, oldest));
    }
    store_save();
}
