/* LG webOS backend: drives the TV's media pipeline over LS2 (com.webos.media).
 *
 * Why not StarfishMediaAPIs: it exits the process silently on a role mismatch
 * for homebrew apps, so we speak to the media server directly, like the Nuvio
 * native client does. Two output paths exist and are picked at startup:
 *   - webOS 5+: SDL's exported window (SDL_webOSCreateExportedWindow) gives us a
 *     windowId the pipeline renders into behind the GL surface.
 *   - webOS 2-4: libAcbAPI (loaded with dlopen) binds the pipeline's mediaId to
 *     the app's video sink.
 * The UI draws a transparent hole (gfx_video_hole) so the plane shows through.
 *
 * Payload shapes follow the com.webos.media LS2 reference; the undocumented
 * option fields were taken from stremio-video's WebOsVideo and community notes.
 * Validate on real hardware: the webosose docs are not identical to TV firmware. */
#include "video.h"
#include "../log.h"
#include "../platform/platform.h"
#include "../../vendor/cJSON.h"
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <glib.h>
#include <luna-service2/lunaservice.h>

/* ---- SDL-webOS exported window API (weak: absent on older SDL builds) ---- */
extern const char *SDL_webOSCreateExportedWindow(int type) __attribute__((weak));
extern SDL_bool SDL_webOSSetExportedWindow(const char *windowId, SDL_Rect *src, SDL_Rect *dst) __attribute__((weak));
extern void SDL_webOSDestroyExportedWindow(const char *windowId) __attribute__((weak));

/* ---- libAcbAPI (webOS 2-4) ---- */
typedef void (*AcbCallback)(long acbId, long taskId, long eventType, long appState, long playState, const char *reply);
static struct {
    void *lib;
    long (*initialize)(long playerType, const char *appId, AcbCallback cb);
    long (*setSinkType)(long acbId, long sinkType);
    long (*setMediaId)(long acbId, const char *mediaId);
    long (*setState)(long acbId, long appState, long playState, long *taskId);
    long (*setDisplayWindow)(long acbId, long x, long y, long w, long h, long fullScreen, long *taskId);
    long (*finalize)(long acbId);
    long id;
} ACB;
enum { ACB_PLAYER_TYPE_MSE = 2, ACB_SINK_MAIN = 1, ACB_APPSTATE_FOREGROUND = 1,
       ACB_PLAYSTATE_LOADED = 2, ACB_PLAYSTATE_PLAYING = 3, ACB_PLAYSTATE_PAUSED = 4, ACB_PLAYSTATE_UNLOADED = 0 };

static LSHandle *g_ls;
static GMainLoop *g_loop;
static SDL_Thread *g_loop_thread;
static SDL_mutex *g_lock;
static char g_media_id[64];
static const char *g_window_id;
static VideoInfo g_info;
static double g_pending_seek = -1;
static SDL_Rect g_disp = { 0, 0, 1920, 1080 };
static int g_use_acb;

static int loop_main(void *arg) { (void)arg; g_main_loop_run(g_loop); return 0; }

static void acb_cb(long acbId, long taskId, long eventType, long appState, long playState, const char *reply)
{
    LOGI("acb: id=%ld task=%ld event=%ld app=%ld play=%ld %s", acbId, taskId, eventType, appState, playState, reply ? reply : "");
}

static int acb_load(void)
{
    ACB.lib = dlopen("libAcbAPI.so.1", RTLD_NOW);
    if (!ACB.lib) ACB.lib = dlopen("libAcbAPI.so", RTLD_NOW);
    if (!ACB.lib) return -1;
#define S(f, n) *(void **)&ACB.f = dlsym(ACB.lib, n); if (!ACB.f) { LOGW("acb: missing %s", n); return -1; }
    S(initialize, "AcbAPI_initialize"); S(setSinkType, "AcbAPI_setSinkType"); S(setMediaId, "AcbAPI_setMediaId");
    S(setState, "AcbAPI_setState"); S(setDisplayWindow, "AcbAPI_setDisplayWindow"); S(finalize, "AcbAPI_finalize");
#undef S
    ACB.id = ACB.initialize(ACB_PLAYER_TYPE_MSE, SNTV_APP_ID, acb_cb);
    LOGI("acb: initialized id=%ld", ACB.id);
    return ACB.id > 0 ? 0 : -1;
}

static bool on_subscription(LSHandle *sh, LSMessage *msg, void *ctx)
{
    (void)sh; (void)ctx;
    const char *payload = LSMessageGetPayload(msg);
    cJSON *root = cJSON_Parse(payload);
    if (!root) return true;
    SDL_LockMutex(g_lock);
    cJSON *v;
    if ((v = cJSON_GetObjectItem(root, "loadCompleted"))) {
        g_info.state = VIDEO_PAUSED;
        if (g_use_acb && ACB.id > 0) {
            long task = 0;
            ACB.setMediaId(ACB.id, g_media_id);
            ACB.setState(ACB.id, ACB_APPSTATE_FOREGROUND, ACB_PLAYSTATE_LOADED, &task);
            ACB.setDisplayWindow(ACB.id, g_disp.x, g_disp.y, g_disp.w, g_disp.h, 1, &task);
        }
    } else if ((v = cJSON_GetObjectItem(root, "currentTime"))) {
        if (cJSON_IsNumber(v)) g_info.position = v->valuedouble / 1000.0;
    } else if ((v = cJSON_GetObjectItem(root, "sourceInfo"))) {
        cJSON *d = cJSON_GetObjectItem(v, "duration");
        if (cJSON_IsNumber(d)) g_info.duration = d->valuedouble / 1000.0;
        cJSON *progs = cJSON_GetObjectItem(v, "programInfo");
        cJSON *p0 = cJSON_IsArray(progs) ? cJSON_GetArrayItem(progs, 0) : NULL;
        d = p0 ? cJSON_GetObjectItem(p0, "duration") : NULL;
        if (cJSON_IsNumber(d)) g_info.duration = d->valuedouble / 1000.0;
    } else if ((v = cJSON_GetObjectItem(root, "bufferRange"))) {
        cJSON *pct = cJSON_GetObjectItem(v, "percent");
        g_info.buffering = cJSON_IsNumber(pct) && pct->valueint < 100 && g_info.state == VIDEO_LOADING;
    } else if ((v = cJSON_GetObjectItem(root, "endOfStream"))) {
        g_info.state = VIDEO_ENDED;
    } else if ((v = cJSON_GetObjectItem(root, "error"))) {
        cJSON *t = cJSON_GetObjectItem(v, "errorText");
        cJSON *c = cJSON_GetObjectItem(v, "errorCode");
        snprintf(g_info.error, sizeof g_info.error, "%s (%d)", cJSON_IsString(t) ? t->valuestring : "pipeline error", cJSON_IsNumber(c) ? c->valueint : 0);
        g_info.state = VIDEO_ERROR;
    } else if ((v = cJSON_GetObjectItem(root, "playing"))) {
        g_info.state = VIDEO_PLAYING;
    } else if ((v = cJSON_GetObjectItem(root, "paused"))) {
        g_info.state = VIDEO_PAUSED;
    }
    SDL_UnlockMutex(g_lock);
    cJSON_Delete(root);
    return true;
}

static bool on_load_reply(LSHandle *sh, LSMessage *msg, void *ctx)
{
    (void)sh; (void)ctx;
    const char *payload = LSMessageGetPayload(msg);
    LOGI("media/load -> %s", payload);
    cJSON *root = cJSON_Parse(payload);
    cJSON *ok = cJSON_GetObjectItem(root, "returnValue");
    cJSON *mid = cJSON_GetObjectItem(root, "mediaId");
    SDL_LockMutex(g_lock);
    if (cJSON_IsTrue(ok) && cJSON_IsString(mid)) {
        snprintf(g_media_id, sizeof g_media_id, "%s", mid->valuestring);
        char sub[160];
        snprintf(sub, sizeof sub, "{\"mediaId\":\"%s\",\"subscribe\":true}", g_media_id);
        LSError e; LSErrorInit(&e);
        if (!LSCall(g_ls, "luna://com.webos.media/subscribe", sub, on_subscription, NULL, NULL, &e)) {
            LOGE("media/subscribe failed: %s", e.message); LSErrorFree(&e);
        }
    } else {
        cJSON *et = cJSON_GetObjectItem(root, "errorText");
        snprintf(g_info.error, sizeof g_info.error, "load failed: %s", cJSON_IsString(et) ? et->valuestring : payload);
        g_info.state = VIDEO_ERROR;
    }
    SDL_UnlockMutex(g_lock);
    cJSON_Delete(root);
    return true;
}

static bool on_generic_reply(LSHandle *sh, LSMessage *msg, void *ctx)
{
    (void)sh;
    LOGI("media/%s -> %s", (const char *)ctx, LSMessageGetPayload(msg));
    return true;
}

static void call(const char *method, const char *payload)
{
    char uri[128];
    snprintf(uri, sizeof uri, "luna://com.webos.media/%s", method);
    LSError e; LSErrorInit(&e);
    if (!LSCallOneReply(g_ls, uri, payload, on_generic_reply, (void *)method, NULL, &e)) {
        LOGE("%s failed: %s", uri, e.message);
        LSErrorFree(&e);
    }
}

int video_init(SDL_Window *win)
{
    (void)win;
    g_lock = SDL_CreateMutex();
    LSError e; LSErrorInit(&e);
    if (!LSRegister(NULL, &g_ls, &e)) { LOGE("LSRegister failed: %s", e.message); LSErrorFree(&e); return -1; }
    GMainContext *ctx = g_main_context_new();
    g_loop = g_main_loop_new(ctx, FALSE);
    if (!LSGmainContextAttach(g_ls, ctx, &e)) { LOGE("LSGmainContextAttach failed: %s", e.message); LSErrorFree(&e); return -1; }
    g_loop_thread = SDL_CreateThread(loop_main, "ls2", NULL);

    if (SDL_webOSCreateExportedWindow) {
        g_window_id = SDL_webOSCreateExportedWindow(0);
        LOGI("video(webos): exported window %s", g_window_id ? g_window_id : "(null)");
    }
    if (!g_window_id) {
        g_use_acb = acb_load() == 0;
        LOGI("video(webos): using %s", g_use_acb ? "ACB" : "no video binding (black video expected)");
    }
    return 0;
}

const char *video_backend_name(void) { return g_use_acb ? "webOS com.webos.media + ACB" : "webOS com.webos.media + exported window"; }

void video_shutdown(void)
{
    video_stop();
    if (g_window_id && SDL_webOSDestroyExportedWindow) SDL_webOSDestroyExportedWindow(g_window_id);
    if (ACB.id > 0) ACB.finalize(ACB.id);
    if (g_loop) g_main_loop_quit(g_loop);
    if (g_loop_thread) SDL_WaitThread(g_loop_thread, NULL);
    LSError e; LSErrorInit(&e);
    if (g_ls) LSUnregister(g_ls, &e);
    SDL_DestroyMutex(g_lock);
}

int video_load(const char *url, const char *title, double start)
{
    (void)title;
    video_stop();
    SDL_LockMutex(g_lock);
    memset(&g_info, 0, sizeof g_info);
    g_info.state = VIDEO_LOADING;
    g_pending_seek = start > 0 ? start : -1;
    SDL_UnlockMutex(g_lock);

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "uri", url);
    cJSON_AddStringToObject(root, "type", "media");
    cJSON *payload = cJSON_AddObjectToObject(root, "payload");
    cJSON *option = cJSON_AddObjectToObject(payload, "option");
    cJSON_AddStringToObject(option, "appId", SNTV_APP_ID);
    if (g_window_id) cJSON_AddStringToObject(option, "windowId", g_window_id);
    cJSON_AddBoolToObject(option, "needAudio", 1);
    cJSON *transmission = cJSON_AddObjectToObject(option, "transmission");
    cJSON_AddStringToObject(transmission, "contentsType", "WEBRTC"); /* keeps the pipeline unmanaged by the web engine */
    char *txt = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    LSError e; LSErrorInit(&e);
    bool ok = LSCallOneReply(g_ls, "luna://com.webos.media/load", txt, on_load_reply, NULL, NULL, &e);
    if (!ok) { LOGE("media/load call failed: %s", e.message); LSErrorFree(&e); }
    free(txt);
    return ok ? 0 : -1;
}

void video_play(void)
{
    if (!g_media_id[0]) return;
    char p[128];
    snprintf(p, sizeof p, "{\"mediaId\":\"%s\"}", g_media_id);
    call("play", p);
    if (g_use_acb && ACB.id > 0) { long t; ACB.setState(ACB.id, ACB_APPSTATE_FOREGROUND, ACB_PLAYSTATE_PLAYING, &t); }
}

void video_pause(void)
{
    if (!g_media_id[0]) return;
    char p[128];
    snprintf(p, sizeof p, "{\"mediaId\":\"%s\"}", g_media_id);
    call("pause", p);
    if (g_use_acb && ACB.id > 0) { long t; ACB.setState(ACB.id, ACB_APPSTATE_FOREGROUND, ACB_PLAYSTATE_PAUSED, &t); }
}

void video_seek(double seconds)
{
    if (!g_media_id[0]) return;
    char p[160];
    snprintf(p, sizeof p, "{\"mediaId\":\"%s\",\"position\":%lld}", g_media_id, (long long)(seconds * 1000));
    call("seek", p);
}

void video_stop(void)
{
    if (!g_media_id[0]) return;
    char p[128];
    snprintf(p, sizeof p, "{\"mediaId\":\"%s\"}", g_media_id);
    call("unload", p);
    if (g_use_acb && ACB.id > 0) { long t; ACB.setState(ACB.id, ACB_APPSTATE_FOREGROUND, ACB_PLAYSTATE_UNLOADED, &t); }
    g_media_id[0] = 0;
    SDL_LockMutex(g_lock);
    g_info.state = VIDEO_IDLE;
    SDL_UnlockMutex(g_lock);
}

void video_set_display_rect(int x, int y, int w, int h)
{
    g_disp.x = x; g_disp.y = y; g_disp.w = w; g_disp.h = h;
    if (g_window_id && SDL_webOSSetExportedWindow) SDL_webOSSetExportedWindow(g_window_id, NULL, &g_disp);
}

void video_update(void)
{
    SDL_LockMutex(g_lock);
    int loaded = g_info.state == VIDEO_PAUSED && g_media_id[0];
    double seek = g_pending_seek;
    SDL_UnlockMutex(g_lock);
    if (loaded && seek >= 0) {
        g_pending_seek = -1;
        video_seek(seek);
        video_play();
    } else if (loaded && seek < 0 && g_pending_seek == -1) {
        /* first loadCompleted with no resume point: autoplay once */
        g_pending_seek = -2;
        video_play();
    }
}

void video_info(VideoInfo *out)
{
    SDL_LockMutex(g_lock);
    *out = g_info;
    SDL_UnlockMutex(g_lock);
}
