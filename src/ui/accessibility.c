#include "accessibility.h"
#include "../app.h"
#include "../platform/platform.h"
#include "gfx.h"
#ifdef SNTV_PLATFORM_ANDROID
#include "cJSON.h"
#include <SDL_system.h>
#include <jni.h>
#include <stdint.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

enum { MAX_NODES = 512, MAX_LABELS = 1024 };
static struct { float x, y, w, h; char text[256]; int priority; } g_labels[MAX_LABELS];
static struct { float x, y, w, h; int id; } g_nodes[MAX_NODES];
static int g_nlabels, g_nnodes, g_enabled, g_recording;
static Screen *g_owner;
static unsigned g_version;
static char *g_previous;
static jclass g_class;
static jmethodID g_enabled_method, g_update_method;
int ui_access_enabled(void) { return g_enabled; }

static JNIEnv *bridge(void)
{
    JNIEnv *e = (JNIEnv *)SDL_AndroidGetJNIEnv();
    if (e && !g_class) {
        jobject activity = SDL_AndroidGetActivity();
        if (!activity) return NULL;
        jclass cls = (*e)->GetObjectClass(e, activity);
        g_class = (*e)->NewGlobalRef(e, cls);
        (*e)->DeleteLocalRef(e, cls); (*e)->DeleteLocalRef(e, activity);
        g_enabled_method = (*e)->GetStaticMethodID(e, g_class, "accessibilityEnabled", "()Z");
        g_update_method = (*e)->GetStaticMethodID(e, g_class, "accessibilityUpdate", "([BJLjava/lang/String;)V");
        if ((*e)->ExceptionCheck(e)) { (*e)->ExceptionClear(e); return NULL; }
    }
    return e && g_enabled_method && g_update_method ? e : NULL;
}

void ui_access_begin(Screen *screen)
{
    g_recording = screen == app_top();
    if (!g_recording) return;
    JNIEnv *e = bridge();
    g_enabled = e && (*e)->CallStaticBooleanMethod(e, g_class, g_enabled_method);
    g_nlabels = g_nnodes = 0;
    if (g_owner != screen) { g_owner = screen; free(g_previous); g_previous = NULL; g_version++; }
}

void ui_access_text(const char *text, size_t n, float x, float y, float w, float h, int priority)
{
    if (!g_enabled || !g_recording || !text || !*text || g_nlabels == MAX_LABELS || !gfx_visible_rect(&x, &y, &w, &h)) return;
    if (!n) n = strlen(text);
    if (n >= sizeof g_labels[0].text) n = sizeof g_labels[0].text - 1;
    while (n && ((unsigned char)text[n] & 0xC0) == 0x80) n--;
    int i = g_nlabels++;
    memcpy(g_labels[i].text, text, n); g_labels[i].text[n] = 0;
    g_labels[i].x = x; g_labels[i].y = y; g_labels[i].w = w; g_labels[i].h = h; g_labels[i].priority = priority;
}

void ui_access_hit(float x, float y, float w, float h, int id)
{
    if (!g_enabled || !g_recording || g_nnodes == MAX_NODES || !gfx_visible_rect(&x, &y, &w, &h)) return;
    float r = fminf(x + w, SNTV_UI_W), b = fminf(y + h, SNTV_UI_H);
    x = fmaxf(x, 0); y = fmaxf(y, 0); w = r - x; h = b - y;
    if (w <= 0 || h <= 0) return;
    int i = g_nnodes++;
    g_nodes[i].x = x; g_nodes[i].y = y; g_nodes[i].w = w; g_nodes[i].h = h; g_nodes[i].id = id;
}

void ui_access_publish(void)
{
    if (!g_enabled || g_owner != app_top()) return;
    cJSON *all = cJSON_CreateArray();
    for (int i = 0; i < g_nnodes; i++) {
        float x = g_nodes[i].x, y = g_nodes[i].y, w = g_nodes[i].w, h = g_nodes[i].h;
        if (w * h > SNTV_UI_W * SNTV_UI_H * 0.6f) continue;
        char label[512] = "";
        for (int priority = 2; priority >= 0 && !label[0]; priority--)
            for (int j = 0; j < g_nlabels; j++) {
                float cx = g_labels[j].x + g_labels[j].w / 2, cy = g_labels[j].y + g_labels[j].h / 2;
                if (g_labels[j].priority != priority || cx < x || cx > x + w || cy < y || cy > y + h || strstr(label, g_labels[j].text)) continue;
                size_t n = strlen(label);
                SDL_snprintf(label + n, sizeof label - n, "%s%s", n ? ", " : "", g_labels[j].text);
            }
        if (!label[0]) continue;
        int px, py, pw, ph; gfx_ui_to_pixels(x, y, w, h, &px, &py, &pw, &ph);
        cJSON *node = cJSON_CreateObject();
        cJSON_AddNumberToObject(node, "id", g_nodes[i].id);
        cJSON_AddStringToObject(node, "label", label);
        int bounds[] = { px, py, px + pw, py + ph };
        cJSON_AddItemToObject(node, "bounds", cJSON_CreateIntArray(bounds, 4));
        cJSON_AddBoolToObject(node, "longClick", g_owner->long_ok);
        cJSON_AddBoolToObject(node, "clickable", 1);
        cJSON_AddItemToArray(all, node);
    }
    if (strcmp(g_owner->name, "player")) for (int j = 0; j < g_nlabels && cJSON_GetArraySize(all) < MAX_NODES; j++) {
        if (g_labels[j].priority != 1) continue;
        float x = g_labels[j].x, y = g_labels[j].y, w = g_labels[j].w, h = g_labels[j].h, cx = x + w / 2, cy = y + h / 2;
        if (cx < 0 || cx > SNTV_UI_W || cy < 0 || cy > SNTV_UI_H) continue;
        int covered = 0;
        for (int i = 0; i < g_nnodes && !covered; i++) covered = cx >= g_nodes[i].x && cx <= g_nodes[i].x + g_nodes[i].w && cy >= g_nodes[i].y && cy <= g_nodes[i].y + g_nodes[i].h;
        for (int i = 0; i < j && !covered; i++) covered = !strcmp(g_labels[i].text, g_labels[j].text) && fabsf(g_labels[i].x - x) < 8 && fabsf(g_labels[i].y - y) < 8;
        if (covered) continue;
        int px, py, pw, ph; gfx_ui_to_pixels(x, y, w, h, &px, &py, &pw, &ph);
        cJSON *node = cJSON_CreateObject();
        cJSON_AddNumberToObject(node, "id", -2 - j);
        cJSON_AddStringToObject(node, "label", g_labels[j].text);
        int bounds[] = { px, py, px + pw, py + ph };
        cJSON_AddItemToObject(node, "bounds", cJSON_CreateIntArray(bounds, 4));
        cJSON_AddBoolToObject(node, "clickable", 0);
        cJSON_AddItemToArray(all, node);
    }
    char *json = cJSON_PrintUnformatted(all); cJSON_Delete(all);
    if (!json) return;
    if (!g_previous || strcmp(json, g_previous)) {
        JNIEnv *e = bridge();
        if (e) {
            size_t n = strlen(json);
            jbyteArray bytes = (*e)->NewByteArray(e, (jsize)n);
            if (bytes) {
                (*e)->SetByteArrayRegion(e, bytes, 0, (jsize)n, (const jbyte *)json);
                jstring screen = (*e)->NewStringUTF(e, g_owner->name);
                (*e)->CallStaticVoidMethod(e, g_class, g_update_method, bytes, (jlong)++g_version, screen);
                (*e)->DeleteLocalRef(e, screen);
                (*e)->DeleteLocalRef(e, bytes);
            }
            if ((*e)->ExceptionCheck(e)) (*e)->ExceptionClear(e);
        }
        free(g_previous); g_previous = json;
    } else free(json);
}

int ui_access_target(unsigned version, int id)
{
    if (!g_enabled || version != g_version || g_owner != app_top()) return 0;
    if (id == -1) return 1;
    for (int i = 0; i < g_nnodes; i++) if (g_nodes[i].id == id) return 1;
    return 0;
}

JNIEXPORT void JNICALL Java_app_cuelight_tv_CuelightActivity_nativeAccessibilityAction(JNIEnv *e, jclass cls, jlong version, jint id, jint action)
{
    (void)e; (void)cls;
    SDL_Event event; SDL_zero(event); event.type = SDL_USEREVENT;
    event.user.code = SNTV_ACCESS_EVENT;
    event.user.data1 = (void *)(intptr_t)id;
    event.user.data2 = (void *)(uintptr_t)version;
    event.user.windowID = (Uint32)action;
    SDL_PushEvent(&event);
}
#endif
