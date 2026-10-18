/* LG webOS backend: drives the TV's media pipeline over LS2 (com.webos.media).
 *
 * Why not StarfishMediaAPIs: it calls exit(0) when the process does not match the
 * exeName in its LS2 role, so homebrew apps talk to the media server directly.
 *
 * Two output paths, picked at startup (the Nuvio port does the same):
 *   - webOS 5+: SDL's exported window (SDL_webOSCreateExportedWindow, declared in
 *     the webosbrew fork's SDL_system.h) gives a windowId the pipeline renders into
 *     behind the GL surface.
 *   - webOS 2-4: libAcbAPI (dlopen) binds the pipeline's mediaId to the app's sink.
 * The UI draws a transparent hole (gfx_video_hole) so the plane shows through.
 *
 * Payloads: com.webos.media load/play/pause/seek/unload/subscribe/selectTrack/
 * setSubtitleEnable/setPlayRate as in the webOS OSE LS2 reference and in
 * stremio-video's WebOsVideo (which uses the same calls for tracks and subtitles).
 * The load option block is tried in two shapes because firmwares differ. */
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
#include <SDL_system.h>

#ifndef __WEBOS__
/* Allow compiling against a stock SDL header set; the symbols resolve at runtime. */
typedef enum { SDL_WEBOS_EXPORTED_WINDOW_TYPE_VIDEO = 0 } SDL_webOSExportedWindowType;
#endif
extern const char *SDL_webOSCreateExportedWindow(SDL_webOSExportedWindowType type) __attribute__((weak));
extern SDL_bool SDL_webOSSetExportedWindow(const char *windowId, SDL_Rect *src, SDL_Rect *dst) __attribute__((weak));
extern void SDL_webOSDestroyExportedWindow(const char *windowId) __attribute__((weak));
extern SDL_bool SDL_webOSGetPanelResolution(int *width, int *height) __attribute__((weak));

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
enum { ACB_PLAYER_TYPE_MSE = 2, ACB_SINK_MAIN = 1, ACB_APPSTATE_FOREGROUND = 1, ACB_APPSTATE_BACKGROUND = 2,
       ACB_PLAYSTATE_UNLOADED = 0, ACB_PLAYSTATE_LOADED = 2, ACB_PLAYSTATE_PLAYING = 3, ACB_PLAYSTATE_PAUSED = 4 };

enum { MAX_TRACKS = 16 };

static LSHandle *g_ls;
static GMainLoop *g_loop;
static SDL_Thread *g_loop_thread;
static SDL_mutex *g_lock;
static char g_media_id[64];
static const char *g_window_id;
static VideoInfo g_info;
static double g_pending_seek = -1;
static int g_autoplayed;
static SDL_Rect g_disp = { 0, 0, 1920, 1080 };
static int g_use_acb;
static int g_load_variant;
static char g_url[2048];
static Uint32 g_load_started;
static VideoTrack g_audio[MAX_TRACKS], g_text[MAX_TRACKS];
static int g_n_audio, g_n_text;
static int g_background;

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
    if (ACB.id > 0) ACB.setSinkType(ACB.id, ACB_SINK_MAIN);
    LOGI("acb: initialized id=%ld", ACB.id);
    return ACB.id > 0 ? 0 : -1;
}

static void acb_state(long play_state)
{
    if (!g_use_acb || ACB.id <= 0) return;
    long task = 0;
    ACB.setState(ACB.id, g_background ? ACB_APPSTATE_BACKGROUND : ACB_APPSTATE_FOREGROUND, play_state, &task);
}

/* ---- LS2 helpers (callbacks run on the glib thread; state is under g_lock) ---- */

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

static void call_media(const char *method, const char *extra_json_fields)
{
    if (!g_media_id[0]) return;
    char p[512];
    snprintf(p, sizeof p, "{\"mediaId\":\"%s\"%s%s}", g_media_id, extra_json_fields ? "," : "", extra_json_fields ? extra_json_fields : "");
    call(method, p);
}

static void parse_tracks(const cJSON *program)
{
    g_n_audio = g_n_text = 0;
    const cJSON *arr = cJSON_GetObjectItem(program, "audioTrackInfo");
    const cJSON *it;
    int i = 0;
    cJSON_ArrayForEach(it, arr) {
        if (g_n_audio >= MAX_TRACKS) break;
        VideoTrack *t = &g_audio[g_n_audio++];
        memset(t, 0, sizeof *t);
        t->index = i++;
        const cJSON *l = cJSON_GetObjectItem(it, "language");
        const cJSON *c = cJSON_GetObjectItem(it, "codec");
        const cJSON *ch = cJSON_GetObjectItem(it, "channels");
        if (cJSON_IsString(l)) snprintf(t->lang, sizeof t->lang, "%s", l->valuestring);
        if (cJSON_IsString(c)) snprintf(t->codec, sizeof t->codec, "%s", c->valuestring);
        snprintf(t->label, sizeof t->label, "%s%s%s", t->lang[0] ? t->lang : "Audio", cJSON_IsNumber(ch) ? " " : "", cJSON_IsNumber(ch) ? (ch->valueint >= 6 ? "5.1" : ch->valueint == 2 ? "stereo" : "") : "");
        t->selected = i == 1;
    }
    arr = cJSON_GetObjectItem(program, "subtitleTrackInfo");
    i = 0;
    cJSON_ArrayForEach(it, arr) {
        if (g_n_text >= MAX_TRACKS) break;
        VideoTrack *t = &g_text[g_n_text++];
        memset(t, 0, sizeof *t);
        t->index = i++;
        const cJSON *l = cJSON_GetObjectItem(it, "language");
        const cJSON *c = cJSON_GetObjectItem(it, "codec");
        if (cJSON_IsString(l)) snprintf(t->lang, sizeof t->lang, "%s", l->valuestring);
        if (cJSON_IsString(c)) snprintf(t->codec, sizeof t->codec, "%s", c->valuestring);
        snprintf(t->label, sizeof t->label, "%s", t->lang[0] ? t->lang : "Subtitle");
    }
    g_info.tracks_version++;
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
        g_info.buffering = 0;
        if (g_use_acb && ACB.id > 0) {
            long task = 0;
            ACB.setMediaId(ACB.id, g_media_id);
            acb_state(ACB_PLAYSTATE_LOADED);
            ACB.setDisplayWindow(ACB.id, g_disp.x, g_disp.y, g_disp.w, g_disp.h, 1, &task);
        }
    } else if ((v = cJSON_GetObjectItem(root, "currentTime"))) {
        if (cJSON_IsNumber(v)) g_info.position = v->valuedouble / 1000.0;
    } else if ((v = cJSON_GetObjectItem(root, "sourceInfo"))) {
        cJSON *d = cJSON_GetObjectItem(v, "duration");
        if (cJSON_IsNumber(d)) g_info.duration = d->valuedouble / 1000.0;
        cJSON *progs = cJSON_GetObjectItem(v, "programInfo");
        cJSON *p0 = cJSON_IsArray(progs) ? cJSON_GetArrayItem(progs, 0) : NULL;
        if (p0) {
            d = cJSON_GetObjectItem(p0, "duration");
            if (cJSON_IsNumber(d)) g_info.duration = d->valuedouble / 1000.0;
            parse_tracks(p0);
            cJSON *vt = cJSON_GetObjectItem(p0, "videoTrackInfo");
            cJSON *v0 = cJSON_IsArray(vt) ? cJSON_GetArrayItem(vt, 0) : NULL;
            if (v0) {
                cJSON *c = cJSON_GetObjectItem(v0, "codec"), *w = cJSON_GetObjectItem(v0, "width"), *h = cJSON_GetObjectItem(v0, "height");
                snprintf(g_info.codec, sizeof g_info.codec, "%s %dx%d", cJSON_IsString(c) ? c->valuestring : "video",
                         cJSON_IsNumber(w) ? w->valueint : 0, cJSON_IsNumber(h) ? h->valueint : 0);
            }
        }
    } else if ((v = cJSON_GetObjectItem(root, "bufferRange"))) {
        cJSON *pct = cJSON_GetObjectItem(v, "percent");
        if (cJSON_IsNumber(pct)) g_info.buffer_percent = pct->valueint;
    } else if ((v = cJSON_GetObjectItem(root, "bufferingStart"))) {
        g_info.buffering = 1;
    } else if ((v = cJSON_GetObjectItem(root, "bufferingEnd"))) {
        g_info.buffering = 0;
    } else if ((v = cJSON_GetObjectItem(root, "endOfStream"))) {
        g_info.state = VIDEO_ENDED;
    } else if ((v = cJSON_GetObjectItem(root, "error"))) {
        cJSON *t = cJSON_GetObjectItem(v, "errorText");
        cJSON *c = cJSON_GetObjectItem(v, "errorCode");
        snprintf(g_info.error, sizeof g_info.error, "%s (%d)", cJSON_IsString(t) ? t->valuestring : "pipeline error", cJSON_IsNumber(c) ? c->valueint : 0);
        g_info.state = VIDEO_ERROR;
        LOGE("media error: %s", payload);
    } else if ((v = cJSON_GetObjectItem(root, "playing"))) {
        g_info.state = VIDEO_PLAYING;
        g_info.buffering = 0;
    } else if ((v = cJSON_GetObjectItem(root, "paused"))) {
        if (g_info.state == VIDEO_PLAYING) g_info.state = VIDEO_PAUSED;
    } else if ((v = cJSON_GetObjectItem(root, "trackSelected"))) {
        cJSON *type = cJSON_GetObjectItem(v, "type"), *idx = cJSON_GetObjectItem(v, "index");
        if (cJSON_IsString(type) && cJSON_IsNumber(idx)) {
            VideoTrack *arr = strcmp(type->valuestring, "audio") == 0 ? g_audio : g_text;
            int n = arr == g_audio ? g_n_audio : g_n_text;
            for (int i = 0; i < n; i++) arr[i].selected = arr[i].index == idx->valueint;
            g_info.tracks_version++;
        }
    }
    SDL_UnlockMutex(g_lock);
    cJSON_Delete(root);
    return true;
}

static void send_load(void);

static bool on_load_reply(LSHandle *sh, LSMessage *msg, void *ctx)
{
    (void)sh; (void)ctx;
    const char *payload = LSMessageGetPayload(msg);
    LOGI("media/load -> %s", payload);
    cJSON *root = cJSON_Parse(payload);
    cJSON *ok = cJSON_GetObjectItem(root, "returnValue");
    cJSON *mid = cJSON_GetObjectItem(root, "mediaId");
    SDL_LockMutex(g_lock);
    if (cJSON_IsTrue(ok) && cJSON_IsString(mid) && strcmp(mid->valuestring, "<invalid mediaId>") != 0) {
        snprintf(g_media_id, sizeof g_media_id, "%s", mid->valuestring);
        char sub[160];
        snprintf(sub, sizeof sub, "{\"mediaId\":\"%s\",\"subscribe\":true}", g_media_id);
        LSError e; LSErrorInit(&e);
        if (!LSCall(g_ls, "luna://com.webos.media/subscribe", sub, on_subscription, NULL, NULL, &e)) {
            LOGE("media/subscribe failed: %s", e.message); LSErrorFree(&e);
        }
    } else if (g_load_variant == 0) {
        /* Retry once with the minimal option block some firmwares want. */
        g_load_variant = 1;
        SDL_UnlockMutex(g_lock);
        cJSON_Delete(root);
        send_load();
        return true;
    } else {
        cJSON *et = cJSON_GetObjectItem(root, "errorText");
        snprintf(g_info.error, sizeof g_info.error, "load failed: %s", cJSON_IsString(et) ? et->valuestring : payload);
        g_info.state = VIDEO_ERROR;
    }
    SDL_UnlockMutex(g_lock);
    cJSON_Delete(root);
    return true;
}

static void send_load(void)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "uri", g_url);
    cJSON_AddStringToObject(root, "type", "media");
    cJSON *payload = cJSON_AddObjectToObject(root, "payload");
    cJSON *option = cJSON_AddObjectToObject(payload, "option");
    cJSON_AddStringToObject(option, "appId", SNTV_APP_ID);
    if (g_window_id) cJSON_AddStringToObject(option, "windowId", g_window_id);
    if (g_load_variant == 0) {
        cJSON_AddBoolToObject(option, "needAudio", 1);
        cJSON_AddStringToObject(option, "mediaTransportType", "URI");
        cJSON *fmt = cJSON_AddObjectToObject(option, "mediaFormat");
        cJSON_AddStringToObject(fmt, "type", "video");
        cJSON *transmission = cJSON_AddObjectToObject(option, "transmission");
        cJSON_AddStringToObject(transmission, "playTime", "0");
    }
    char *txt = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    LOGI("media/load variant %d: %s", g_load_variant, txt);
    LSError e; LSErrorInit(&e);
    if (!LSCallOneReply(g_ls, "luna://com.webos.media/load", txt, on_load_reply, NULL, NULL, &e)) {
        LOGE("media/load call failed: %s", e.message);
        LSErrorFree(&e);
        SDL_LockMutex(g_lock);
        snprintf(g_info.error, sizeof g_info.error, "LS2 call failed: %s", e.message);
        g_info.state = VIDEO_ERROR;
        SDL_UnlockMutex(g_lock);
    }
    free(txt);
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

    int pw = 0, ph = 0;
    if (SDL_webOSGetPanelResolution && SDL_webOSGetPanelResolution(&pw, &ph) && pw > 0) { g_disp.w = pw; g_disp.h = ph; }
    if (SDL_webOSCreateExportedWindow) {
        g_window_id = SDL_webOSCreateExportedWindow(SDL_WEBOS_EXPORTED_WINDOW_TYPE_VIDEO);
        LOGI("video(webos): exported window %s (%dx%d)", g_window_id ? g_window_id : "(null)", g_disp.w, g_disp.h);
        if (g_window_id && SDL_webOSSetExportedWindow) SDL_webOSSetExportedWindow(g_window_id, NULL, &g_disp);
    }
    if (!g_window_id) {
        g_use_acb = acb_load() == 0;
        LOGI("video(webos): using %s", g_use_acb ? "ACB" : "no video binding (audio only expected)");
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
    g_autoplayed = 0;
    g_load_variant = 0;
    g_n_audio = g_n_text = 0;
    snprintf(g_url, sizeof g_url, "%s", url);
    g_load_started = SDL_GetTicks();
    SDL_UnlockMutex(g_lock);
    send_load();
    return 0;
}

void video_play(void)  { call_media("play", NULL); acb_state(ACB_PLAYSTATE_PLAYING); }
void video_pause(void) { call_media("pause", NULL); acb_state(ACB_PLAYSTATE_PAUSED); }

void video_seek(double seconds)
{
    char extra[64];
    snprintf(extra, sizeof extra, "\"position\":%lld", (long long)(seconds * 1000));
    call_media("seek", extra);
    SDL_LockMutex(g_lock);
    g_info.position = seconds;   /* optimistic, corrected by the next currentTime */
    SDL_UnlockMutex(g_lock);
}

void video_set_rate(double rate)
{
    char extra[64];
    snprintf(extra, sizeof extra, "\"playRate\":%.2f,\"audioOutput\":true", rate);
    call_media("setPlayRate", extra);
}

void video_stop(void)
{
    if (!g_media_id[0]) return;
    call_media("unload", NULL);
    acb_state(ACB_PLAYSTATE_UNLOADED);
    SDL_LockMutex(g_lock);
    g_media_id[0] = 0;
    g_info.state = VIDEO_IDLE;
    SDL_UnlockMutex(g_lock);
}

void video_set_display_rect(int x, int y, int w, int h)
{
    g_disp.x = x; g_disp.y = y; g_disp.w = w; g_disp.h = h;
    if (g_window_id && SDL_webOSSetExportedWindow) SDL_webOSSetExportedWindow(g_window_id, NULL, &g_disp);
}

void video_app_background(void) { g_background = 1; video_pause(); }
void video_app_foreground(void) { g_background = 0; acb_state(ACB_PLAYSTATE_PAUSED); }

void video_update(void)
{
    SDL_LockMutex(g_lock);
    int loaded = g_info.state == VIDEO_PAUSED && g_media_id[0] && !g_autoplayed;
    double seek = g_pending_seek;
    int loading = g_info.state == VIDEO_LOADING;
    Uint32 started = g_load_started;
    SDL_UnlockMutex(g_lock);
    if (loaded) {
        g_autoplayed = 1;
        if (seek > 0) video_seek(seek);
        video_play();
    } else if (loading && SDL_GetTicks() - started > 40000) {
        SDL_LockMutex(g_lock);
        snprintf(g_info.error, sizeof g_info.error, "Timed out waiting for the TV pipeline");
        g_info.state = VIDEO_ERROR;
        SDL_UnlockMutex(g_lock);
    }
}

void video_info(VideoInfo *out)
{
    SDL_LockMutex(g_lock);
    *out = g_info;
    SDL_UnlockMutex(g_lock);
}

int video_tracks(TrackKind kind, VideoTrack *out, int max)
{
    SDL_LockMutex(g_lock);
    VideoTrack *src = kind == TRACK_AUDIO ? g_audio : g_text;
    int n = kind == TRACK_AUDIO ? g_n_audio : g_n_text;
    if (n > max) n = max;
    memcpy(out, src, sizeof(VideoTrack) * (size_t)n);
    SDL_UnlockMutex(g_lock);
    return n;
}

void video_select_track(TrackKind kind, int index)
{
    char extra[96];
    if (kind == TRACK_TEXT) {
        snprintf(extra, sizeof extra, "\"enable\":%s", index >= 0 ? "true" : "false");
        call_media("setSubtitleEnable", extra);
        if (index < 0) return;
    }
    snprintf(extra, sizeof extra, "\"type\":\"%s\",\"index\":%d", kind == TRACK_AUDIO ? "audio" : "text", index);
    call_media("selectTrack", extra);
}
