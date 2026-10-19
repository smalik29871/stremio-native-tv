/* Stage 1 of "a break that interrupts other apps": can this TV bring Stremio to the
 * front at a set time, with no other device and no root? Settings > Take breaks >
 * Test TV interruption arms two ways at once, 60 s ahead:
 *   A  the TV's own scheduler (Activity Manager) launching Stremio with {"sntvAlarm":"A"}
 *   B  Stremio's own timer asking webOS to launch Stremio with {"sntvAlarm":"B"}
 * Every reply webOS gives, and every launch that carries an alarm (a fresh start or a
 * relaunch of the running app), is kept in the store ("alarmTest") and shown on screen,
 * so one photo tells what this TV allows. */
#include "alarmtest.h"
#include "app.h"
#include "log.h"
#include "store.h"
#include "video/video.h"
#include "ui/screen_options.h"
#include "ui/widgets.h"
#include "../vendor/cJSON.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define APP_ID "org.stremio.nativetv"
enum { T_AM = 0, T_PALM = 1, T_REG_NATIVE = 2, T_REG_APP = 3, T_CANCEL = 4, T_LAUNCH_B = 5 };

static int g_seen[LUNA_TAG_MAX];
static char g_fresh[512];   /* launch parameters this process started with */
static int g_fresh_done;

static cJSON *state(void)
{
    cJSON *s = cJSON_Duplicate(store_get_json("alarmTest"), 1);
    if (!cJSON_IsObject(s)) { cJSON_Delete(s); s = cJSON_CreateObject(); }
    return s;
}
static void save(cJSON *s) { store_set_json("alarmTest", s); store_save(); }   /* the store owns s afterwards */
static void set_str(cJSON *s, const char *k, const char *v) { cJSON_DeleteItemFromObjectCaseSensitive(s, k); cJSON_AddStringToObject(s, k, v ? v : ""); }
static void set_num(cJSON *s, const char *k, double v) { cJSON_DeleteItemFromObjectCaseSensitive(s, k); cJSON_AddNumberToObject(s, k, v); }
static double num(const cJSON *s, const char *k) { const cJSON *v = cJSON_GetObjectItemCaseSensitive(s, k); return cJSON_IsNumber(v) ? v->valuedouble : 0; }
static const char *str(const cJSON *s, const char *k) { const cJSON *v = cJSON_GetObjectItemCaseSensitive(s, k); return cJSON_IsString(v) ? v->valuestring : ""; }

/* an alarm in launch parameters: "A" or "B", else 0 */
static char alarm_path(const char *json)
{
    const char *p = json ? strstr(json, "sntvAlarm") : NULL;
    if (!p) return 0;
    p += 9;
    while (*p == '"' || *p == '\\' || *p == ':' || *p == ' ') p++;   /* past the key's quote (escaped too) to the value */
    return *p == 'A' || *p == 'B' ? *p : 0;
}

static void show_result(char path, const char *how, double late)
{
    char body[400];
    snprintf(body, sizeof body, "Stremio came to the front by itself.\n\nPath %c (%s), %s, %.0f s after the set time.\n\n"
             "Settings > Take breaks > Test TV interruption shows every reply.",
             path, path == 'A' ? "the TV's scheduler" : "Stremio's own timer", how, late);
    static const char *const items[] = { "Close" };
    static const int icons[] = { ICON_CLOSE };
    app_push(screen_options_create_ex("TV interruption works", "Silent test", body, items, icons, 1, NULL, NULL));
}

static void record_launch(char path, const char *how)
{
    cJSON *s = state();
    double due = num(s, "due"), now = (double)time(NULL);
    cJSON *list = cJSON_GetObjectItemCaseSensitive(s, "launches");
    if (!cJSON_IsArray(list)) { cJSON_DeleteItemFromObjectCaseSensitive(s, "launches"); list = cJSON_AddArrayToObject(s, "launches"); }
    char line[160];
    snprintf(line, sizeof line, "path %c, %s, %+.0f s from the set time", path, how, due > 0 ? now - due : 0);
    cJSON_AddItemToArray(list, cJSON_CreateString(line));
    while (cJSON_GetArraySize(list) > 6) cJSON_DeleteItemFromArray(list, 0);
    save(s);
    LOGI("alarmtest: launched by path %c (%s)", path, how);
    show_result(path, how, due > 0 ? now - due : 0);
}

void alarmtest_start(int argc, char **argv)
{
    if (argc > 1 && argv[1]) snprintf(g_fresh, sizeof g_fresh, "%s", argv[1]);
    else if (getenv("SNTV_LAUNCH_PARAMS")) snprintf(g_fresh, sizeof g_fresh, "%s", getenv("SNTV_LAUNCH_PARAMS"));   /* tests */
    LOGI("alarmtest: started with %s", g_fresh[0] ? g_fresh : "no launch parameters");
    /* relaunches of the running app come as events on these (which one this TV offers is part of the test) */
    video_luna_call("luna://com.webos.applicationManager/registerNativeApp", "{\"subscribe\":true}", T_REG_NATIVE);
    video_luna_call("luna://com.webos.applicationManager/registerApp", "{\"subscribe\":true}", T_REG_APP);
}

int alarmtest_armed(void)
{
    const cJSON *s = store_get_json("alarmTest");
    return num(s, "due") > (double)time(NULL) - 120 && !cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(s, "cancelled"));
}

void alarmtest_arm(int seconds)
{
    const char *t = getenv("SNTV_ALARM_SECONDS");   /* tests */
    if (t && atoi(t) > 0) seconds = atoi(t);
    time_t due = time(NULL) + seconds;
    struct tm u; gmtime_r(&due, &u);
    char start[32]; strftime(start, sizeof start, "%Y-%m-%d %H:%M:%S", &u);
    char act[900];
    snprintf(act, sizeof act,
        "{\"activity\":{\"name\":\"sntv-interruption-test\",\"description\":\"Stremio TV interruption test\","
        "\"type\":{\"foreground\":true,\"persist\":true},\"schedule\":{\"start\":\"%s\"},"
        "\"callback\":{\"method\":\"luna://com.webos.applicationManager/launch\",\"params\":{\"id\":\"" APP_ID "\",\"params\":{\"sntvAlarm\":\"A\",\"due\":%ld}}}},"
        "\"start\":true,\"replace\":true}", start, (long)due);
    cJSON *s = cJSON_CreateObject();
    set_num(s, "armed", (double)time(NULL)); set_num(s, "due", (double)due);
    set_str(s, "start_utc", start);
    save(s);
    int a = video_luna_call("luna://com.webos.service.activitymanager/create", act, T_AM);
    int p = video_luna_call("luna://com.palm.activitymanager/create", act, T_PALM);
    LOGI("alarmtest: armed for %s UTC (scheduler calls %s / %s)", start, a == 0 ? "sent" : "not sent", p == 0 ? "sent" : "not sent");
}

void alarmtest_cancel(void)
{
    cJSON *s = state();
    const char *id = str(s, "activityId");
    if (*id) { char pl[96]; snprintf(pl, sizeof pl, "{\"activityId\":%s}", id); video_luna_call("luna://com.webos.service.activitymanager/cancel", pl, T_CANCEL); }
    cJSON_DeleteItemFromObjectCaseSensitive(s, "cancelled"); cJSON_AddTrueToObject(s, "cancelled");
    save(s);
    LOGI("alarmtest: cancelled");
}

void alarmtest_tick(void)
{
    if (!g_fresh_done) {   /* started by an alarm */
        g_fresh_done = 1;
        char p = alarm_path(g_fresh);
        if (p) record_launch(p, "a fresh start");
    }
    char reply[1024];
    for (int tag = 0; tag < LUNA_TAG_MAX; tag++) {
        int n = video_luna_reply(tag, reply, sizeof reply);
        if (n == g_seen[tag]) continue;
        g_seen[tag] = n;
        cJSON *s = state();
        static const char *const KEY[] = { "reply_scheduler", "reply_scheduler_palm", "reply_register_native", "reply_register_app", "reply_cancel", "reply_launch_b" };
        if (tag < 6) set_str(s, KEY[tag], reply);
        if (tag == T_AM || tag == T_PALM) {
            cJSON *j = cJSON_Parse(reply);
            const cJSON *id = cJSON_GetObjectItemCaseSensitive(j, "activityId");
            if (cJSON_IsNumber(id)) { char b[32]; snprintf(b, sizeof b, "%.0f", id->valuedouble); set_str(s, "activityId", b); }
            cJSON_Delete(j);
        }
        save(s);
        if ((tag == T_REG_NATIVE || tag == T_REG_APP) && strstr(reply, "relaunch")) {
            char p = alarm_path(reply);
            if (p) record_launch(p, "a relaunch of the running app");
        }
    }
    /* path B: Stremio's own timer (runs only while webOS lets this process run) */
    const cJSON *s = store_get_json("alarmTest");
    double due = num(s, "due");
    if (due > 0 && time(NULL) >= due && !num(s, "b_sent") && !cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(s, "cancelled"))) {
        cJSON *w = state();
        set_num(w, "b_sent", (double)time(NULL));
        save(w);
        char pl[200]; snprintf(pl, sizeof pl, "{\"id\":\"" APP_ID "\",\"params\":{\"sntvAlarm\":\"B\",\"due\":%.0f}}", due);
        LOGI("alarmtest: path B fires");
        video_luna_call("luna://com.webos.applicationManager/launch", pl, T_LAUNCH_B);
    }
}

static void line(char *out, size_t n, size_t *o, const char *fmt, const char *a, const char *b)
{
    if (*o < n) *o += (size_t)snprintf(out + *o, n - *o, fmt, a, b);
}

void alarmtest_report(char *out, size_t n)
{
    const cJSON *s = store_get_json("alarmTest");
    size_t o = 0;
    out[0] = 0;
    if (!cJSON_IsObject(s) || !num(s, "armed")) { snprintf(out, n, "Not run yet. Arm it, then go to YouTube, live TV or HDMI and wait a minute."); return; }
    time_t armed = (time_t)num(s, "armed"), due = (time_t)num(s, "due"), bs = (time_t)num(s, "b_sent");
    char ta[16], td[16], tb[16];
    struct tm lt;
    localtime_r(&armed, &lt); strftime(ta, sizeof ta, "%H:%M:%S", &lt);
    localtime_r(&due, &lt); strftime(td, sizeof td, "%H:%M:%S", &lt);
    line(out, n, &o, "Armed %s, set for %s\n", ta, td);
    if (cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(s, "cancelled"))) line(out, n, &o, "%s%s(cancelled)\n", "", "");
    line(out, n, &o, "A scheduler: %.140s%s\n", *str(s, "reply_scheduler") ? str(s, "reply_scheduler") : "no reply", "");
    line(out, n, &o, "A (palm name): %.140s%s\n", *str(s, "reply_scheduler_palm") ? str(s, "reply_scheduler_palm") : "no reply", "");
    if (bs) { localtime_r(&bs, &lt); strftime(tb, sizeof tb, "%H:%M:%S", &lt); line(out, n, &o, "B timer fired at %s: %.120s\n", tb, *str(s, "reply_launch_b") ? str(s, "reply_launch_b") : "no reply"); }
    else line(out, n, &o, "B timer: %s%s\n", time(NULL) < due ? "waiting" : "did not fire (Stremio was not running)", "");
    line(out, n, &o, "Relaunch events: %.100s%s\n", *str(s, "reply_register_native") ? str(s, "reply_register_native") : *str(s, "reply_register_app") ? str(s, "reply_register_app") : "no reply", "");
    const cJSON *l = cJSON_GetObjectItemCaseSensitive(s, "launches"), *it;
    if (!cJSON_GetArraySize(l)) line(out, n, &o, "Launched by an alarm: %s%s", "not yet", "");
    cJSON_ArrayForEach(it, cJSON_GetObjectItemCaseSensitive(s, "launches")) if (cJSON_IsString(it)) line(out, n, &o, "Launched: %s%s\n", it->valuestring, "");
}
