#include "srchealth.h"
#include "log.h"
#include "store.h"
#include "../vendor/cJSON.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int band_now(void)
{
    const char *e = getenv("SNTV_NOW_HOUR");   /* tests */
    int h;
    if (e && *e) h = atoi(e);
    else { time_t t = time(NULL); struct tm lt; localtime_r(&t, &lt); h = lt.tm_hour; }
    return (h / 6) & 3;
}

static void host_key(const char *url, char *out, size_t n)
{
    out[0] = 0;
    const char *h = url ? strstr(url, "://") : NULL;
    if (!h) return;
    h += 3;
    size_t l = strcspn(h, "/:?#");
    if (!l || l > 120) return;
    snprintf(out, n, "host:%.*s", (int)l, h);
}

static cJSON *entry(cJSON *root, const char *key)
{
    cJSON *e = cJSON_GetObjectItemCaseSensitive(root, key);
    if (cJSON_IsObject(e)) return e;
    if (e) cJSON_DeleteItemFromObjectCaseSensitive(root, key);
    e = cJSON_AddObjectToObject(root, key);
    for (int k = 0; k < 2; k++) {
        cJSON *a = cJSON_AddArrayToObject(e, k ? "fail" : "ok");
        for (int b = 0; b < 4; b++) cJSON_AddItemToArray(a, cJSON_CreateNumber(0));
    }
    return e;
}

static double count(const cJSON *e, const char *which, int band)
{
    const cJSON *v = cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(e, which), band);
    return cJSON_IsNumber(v) ? v->valuedouble : 0;
}

static void bump(cJSON *root, const char *key, int what)
{
    if (!key[0]) return;
    cJSON *e = entry(root, key);
    int band = band_now();
    cJSON *ok = cJSON_GetObjectItemCaseSensitive(e, "ok"), *fail = cJSON_GetObjectItemCaseSensitive(e, "fail");
    double total = 0;
    for (int b = 0; b < 4; b++) total += count(e, "ok", b) + count(e, "fail", b);
    for (int b = 0; total > 24 && b < 4; b++) {   /* old news fades */
        cJSON_SetNumberValue(cJSON_GetArrayItem(ok, b), count(e, "ok", b) / 2);
        cJSON_SetNumberValue(cJSON_GetArrayItem(fail, b), count(e, "fail", b) / 2);
    }
    cJSON *arr = what == HEALTH_OK ? ok : fail;
    cJSON *cell = cJSON_GetArrayItem(arr, band);
    cJSON_SetNumberValue(cell, cell->valuedouble + (what == HEALTH_STALL ? 0.6 : 1));
}

void srchealth_record(const char *url, const char *addon, int what)
{
    cJSON *root = cJSON_Duplicate(store_get_json("srcHealth"), 1);
    if (!cJSON_IsObject(root)) { cJSON_Delete(root); root = cJSON_CreateObject(); }
    while (cJSON_GetArraySize(root) > 200) cJSON_DeleteItemFromArray(root, 0);
    char key[160];
    host_key(url, key, sizeof key);
    bump(root, key, what);
    if (addon && *addon) { snprintf(key, sizeof key, "addon:%.120s", addon); bump(root, key, what); }
    host_key(url, key, sizeof key);
    LOGI("health: %s %s", key[0] ? key : "?", what == HEALTH_OK ? "played well" : what == HEALTH_FAIL ? "failed" : "stopped mid-way");
    store_set_json("srcHealth", root);
    store_save();
}

/* failures beyond the plays, this band weighing most */
static double badness(const cJSON *e, int band)
{
    double ok = 0, fail = 0;
    for (int b = 0; b < 4; b++) { double w = b == band ? 1 : 0.35; ok += w * count(e, "ok", b); fail += w * count(e, "fail", b); }
    if (fail < 1.5) return 0;   /* once is bad luck */
    return fail / (ok + fail + 1);
}

int srchealth_penalty(const char *url, const char *addon)
{
    const cJSON *root = store_get_json("srcHealth");
    if (!cJSON_IsObject(root)) return 0;
    int band = band_now();
    char key[160];
    host_key(url, key, sizeof key);
    double h = key[0] ? badness(cJSON_GetObjectItemCaseSensitive(root, key), band) : 0;
    double a = 0;
    if (addon && *addon) { snprintf(key, sizeof key, "addon:%.120s", addon); a = badness(cJSON_GetObjectItemCaseSensitive(root, key), band); }
    double worst = h > a * 0.6 ? h : a * 0.6;   /* the server says more than the addon */
    return (int)(worst * 900);
}

const char *srchealth_note(const char *url, const char *addon)
{
    static const char *const WHEN[4] = { "at night", "in the morning", "in the afternoon", "in the evening" };
    static char buf[64];
    if (srchealth_penalty(url, addon) < 300) return NULL;
    snprintf(buf, sizeof buf, "Often fails %s", WHEN[band_now()]);
    return buf;
}
