/* LG webOS backend: drives the TV's media pipeline (uMediaServer) over LS2.
 *
 * Everything platform-specific is loaded at run time with dlopen/dlsym, so one
 * binary runs on webOS 3 through current firmware and never fails to start
 * because a library or symbol is missing:
 *   - libluna-service2.so.3 and libglib-2.0.so.0 for the LS2 bus.
 *   - SDL_webOS* exported-window functions from the process's own libSDL2
 *     (webOS 5+). They place the hardware video plane behind our GL surface.
 *   - libAcbAPI.so.1 (webOS 2-4; LG removed it in webOS 5) binds the plane.
 *
 * Facts this code relies on, as documented by the Nuvio native client from
 * testing on real TVs (github.com/iqui27/nuvio-native-legacy, src/video.c):
 *   - LSRegister must use a name matching the media-client role pattern
 *     "com.webos.media.client.*"; any other name is refused by the hub. After
 *     an unclean exit the hub can hold the name for a while, so a ".<pid>"
 *     suffix (same pattern) is the fallback.
 *   - The load payload is {"payload":{"option":{useSeekableRanges, appId,
 *     bufferControl, windowId}}, "uri", "type":"media"}. windowId is the real
 *     exported window on webOS 5+, and any non-empty placeholder on the ACB path.
 *   - SDL_webOSSetExportedWindow must get a real source rect {0,0,w,h}: some
 *     TVs pass a NULL straight to Wayland and the app is killed.
 *   - ACB: create -> initialize(id, playerType 0 = VIDEO for URI sources,
 *     appId, callback) -> setSinkType(0). After EVERY loadCompleted the plane
 *     must be bound again (setMediaId, state LOADED, connectDass,
 *     setMediaVideoData with "context":mediaId, setDisplayWindow, state
 *     PLAYING); reusing the previous binding gives black video with sound.
 *     playerType 10 / sink 1 routes video to the PIP plane (black screen).
 *   - Events: currentTime arrives as {"currentTime":{"currentTime":ms}};
 *     error code 200 means the TV cannot decode the audio track and the video
 *     keeps playing, so it is a warning, not a failure.
 *
 * The UI draws a transparent hole (gfx_video_hole) so the plane shows through.
 * app.log records every LS2 reply, which is what to read on a TV that misbehaves. */
#include "video.h"
#include "../log.h"
#include "../http.h"
#include "../sha256.h"
#include "../store.h"
#include "../sysinfo.h"
#include "../platform/platform.h"
#include "../../vendor/cJSON.h"
#include <dlfcn.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

/* ---- LS2 / glib ABI (from luna-service2/lunaservice.h in the webOS SDK) ---- */
typedef struct LSHandle LSHandle;
typedef struct LSMessage LSMessage;
typedef unsigned long LSMessageToken;
typedef int (*LSFilterFunc)(LSHandle *sh, LSMessage *msg, void *ctx);
/* LSError is caller-allocated; sized generously and only touched through the
 * library's own LSErrorInit/LSErrorFree, plus the leading error_code/message. */
typedef struct { int error_code; char *message; const char *file; int line; const char *func; void *pad[8]; } LSError;

static struct {
    void *ls, *glib, *acb, *self;
    int  (*Register)(const char *, LSHandle **, LSError *);
    int  (*Unregister)(LSHandle *, LSError *);
    int  (*GmainContextAttach)(LSHandle *, void *, LSError *);
    int  (*Call)(LSHandle *, const char *, const char *, LSFilterFunc, void *, LSMessageToken *, LSError *);
    int  (*CallOneReply)(LSHandle *, const char *, const char *, LSFilterFunc, void *, LSMessageToken *, LSError *);
    const char *(*MessageGetPayload)(LSMessage *);
    void (*ErrorInit)(LSError *);
    void (*ErrorFree)(LSError *);
    void *(*main_context_new)(void);
    void *(*main_loop_new)(void *, int);
    void (*main_loop_run)(void *);
    void (*main_loop_quit)(void *);
    /* SDL exported window (webOS 5+) */
    const char *(*ExpCreate)(int type);
    int  (*ExpSet)(const char *id, SDL_Rect *src, SDL_Rect *dst);
    void (*ExpDestroy)(const char *id);
    /* ACB (webOS 2-4) */
    long (*AcbCreate)(void);
    int  (*AcbInitialize)(long, int, const char *, void *);
    int  (*AcbSetSinkType)(long, int);
    int  (*AcbSetMediaId)(long, const char *);
    int  (*AcbSetState)(long, int, int, long *);
    int  (*AcbSetDisplayWindow)(long, long, long, long, long, int, long *);
    int  (*AcbConnectDass)(long, int, long *);
    int  (*AcbSetMediaVideoData)(long, const char *, long *);
    void (*AcbDestroy)(long);
    int  (*AcbFinalize)(long);
} L;

enum { ACB_PLAYER_VIDEO = 0, ACB_SINK_MAIN = 0, ACB_APP_FOREGROUND = 1, ACB_APP_BACKGROUND = 2,
       ACB_STATE_LOADED = 1, ACB_STATE_PLAYING = 2, ACB_STATE_PAUSED = 3 };
enum { MAX_TRACKS = 16 };

static LSHandle *g_bus;
static void *g_loop;
static pthread_t g_loop_thread;
static int g_loop_running;
static SDL_mutex *g_lock;
static char g_media_id[96];
static unsigned g_session;            /* bumps on every load/stop; guards late replies */
static char g_window_id[96];
static long g_acb;
static VideoInfo g_info;
static double g_pending_seek = -1;
static int g_autoplayed, g_paused_by_user;
static int g_muted;
static int g_volume = 100;
static unsigned g_load_count;
static int g_screen_w = 1920, g_screen_h = 1080;   /* drawable / panel pixels */
static SDL_Rect g_disp = { 0, 0, 1920, 1080 };
static int g_vid_w = 1920, g_vid_h = 1080, g_vid_fps = 24;
static long g_vid_bitrate;
static char g_vid_scan[24] = "progressive";
static char g_url[2048];
static Uint32 g_load_started;
static VideoTrack g_audio[MAX_TRACKS], g_text[MAX_TRACKS];
static int g_n_audio, g_n_text;
static int g_background;
static int g_webos_major;
static int g_bind_running;
static int g_ready;

#define SYM(lib, field, name, required) do { *(void **)&L.field = lib ? dlsym(lib, name) : NULL; \
    if (!L.field && (required)) { LOGW("video(webos): missing %s", name); ok = 0; } } while (0)

static int webos_major(void)
{
    if (g_webos_major) return g_webos_major;
    char line[256]; const char *src;
    g_webos_major = sysinfo_webos_release(line, sizeof line, &src);
    LOGI("video(webos): '%s' from %s -> major %d", line, src[0] ? src : "nowhere", g_webos_major);
    return g_webos_major;
}

/* ---- LS2 helpers ---------------------------------------------------------- */

static void ls_error_log(const char *where, LSError *e)
{
    LOGE("video(webos): %s: %s (%d)", where, e->message ? e->message : "?", e->error_code);
    L.ErrorFree(e);
    L.ErrorInit(e);
}

static int on_reply_log(LSHandle *sh, LSMessage *msg, void *ctx)
{
    (void)sh;
    LOGI("media/%s -> %.300s", (const char *)ctx, L.MessageGetPayload(msg));
    return 1;
}

static void media_call(const char *method, const char *payload)
{
    if (!g_bus) return;
    char uri[128];
    snprintf(uri, sizeof uri, "luna://com.webos.media/%s", method);
    LSError e; L.ErrorInit(&e);
    if (!L.CallOneReply(g_bus, uri, payload, on_reply_log, (void *)method, NULL, &e)) ls_error_log(uri, &e);
    L.ErrorFree(&e);
}

static void media_call_id(const char *method, const char *extra);

/* ---- subtitles the TV draws ------------------------------------------------------
 * The media service renders embedded subtitle tracks itself, so the app's size,
 * style, position and timing reach them only through its subtitle setters. A fresh
 * pipeline starts at every load, so the style is sent again after each loadCompleted
 * and each track selection. Ranges as the service takes them: fontSize 0..4,
 * position -3..4 (0 = default), sync in ms, colours by name, opacities 0..255. */
static struct { int size, style, position, sync_ms, have, pending; char colour[16]; } g_sub;

static void subtitle_style_send(void)
{
    if (!g_sub.have) return;
    static const int FONT[] = { 1, 2, 3, 4 };   /* the app's own subtitles are drawn as big as these (ui_subtitle_font) */
    static const int POS[] = { 0, 2, 4 };
    int st = g_sub.style;
    /* SUBS_STYLE_*: shadow, outline, yellow, boxed, bright, raised, soft box, cyan */
    const char *colour = g_sub.colour[0] ? g_sub.colour : st == 2 ? "yellow" : st == 7 ? "cyan" : "white";
    int dark = !strcmp(colour, "black") || !strcmp(colour, "blue");   /* a light edge and box behind them */
    const char *edge = st == 0 ? "dropShadow" : st == 3 || st == 6 ? "none" : st == 5 ? "raised" : "uniform";
    int box = st == 3 ? 190 : st == 6 ? 110 : 0;
    char x[96];
    snprintf(x, sizeof x, "\"fontSize\":%d", FONT[g_sub.size < 0 ? 0 : g_sub.size > 3 ? 3 : g_sub.size]);
    media_call_id("setSubtitleFontSize", x);
    snprintf(x, sizeof x, "\"charColor\":\"%s\"", colour);
    media_call_id("setSubtitleCharacterColor", x);
    media_call_id("setSubtitleCharacterOpacity", "\"charOpacity\":255");   /* full white: some TVs start them dimmed */
    snprintf(x, sizeof x, "\"charEdgeType\":\"%s\"", edge);
    media_call_id("setSubtitleCharacterEdge", x);
    media_call_id("setSubtitleCharacterEdgeColor", dark ? "\"charEdgeColor\":\"white\"" : "\"charEdgeColor\":\"black\"");
    media_call_id("setSubtitleBackgroundColor", dark ? "\"bgColor\":\"white\"" : "\"bgColor\":\"black\"");
    snprintf(x, sizeof x, "\"bgOpacity\":%d", box);
    media_call_id("setSubtitleBackgroundOpacity", x);
    snprintf(x, sizeof x, "\"position\":%d", POS[g_sub.position < 0 ? 0 : g_sub.position > 2 ? 2 : g_sub.position]);
    media_call_id("setSubtitlePosition", x);
    snprintf(x, sizeof x, "\"sync\":%d", g_sub.sync_ms);
    media_call_id("setSubtitleSync", x);
}

void video_subtitle_style(int size, int style, const char *colour, int position, int sync_ms)
{
    if (!colour) colour = "";
    int same = g_sub.have && g_sub.size == size && g_sub.style == style && g_sub.position == position && g_sub.sync_ms == sync_ms && !strcmp(g_sub.colour, colour);
    g_sub.size = size; g_sub.style = style; g_sub.position = position; g_sub.sync_ms = sync_ms; g_sub.have = 1;
    snprintf(g_sub.colour, sizeof g_sub.colour, "%s", colour);
    if (!same) subtitle_style_send();
}

static void media_call_id(const char *method, const char *extra)
{
    char id[96];
    SDL_LockMutex(g_lock);
    snprintf(id, sizeof id, "%s", g_media_id);
    SDL_UnlockMutex(g_lock);
    if (!id[0]) return;
    char p[512];
    snprintf(p, sizeof p, "{\"mediaId\":\"%s\"%s%s}", id, extra ? "," : "", extra ? extra : "");
    media_call(method, p);
}

/* ---- JSON helpers for uMS events ----------------------------------------- */

static const cJSON *find_key(const cJSON *n, const char *key, int depth)
{
    if (!n || depth > 6) return NULL;
    const cJSON *c;
    cJSON_ArrayForEach(c, n) {
        if (c->string && strcmp(c->string, key) == 0) return c;
        if (cJSON_IsObject(c) || cJSON_IsArray(c)) {
            const cJSON *r = find_key(c, key, depth + 1);
            if (r) return r;
        }
    }
    return NULL;
}

static double find_number(const cJSON *n, const char *key, double def)
{
    const cJSON *v = find_key(n, key, 0);
    while (v && cJSON_IsObject(v)) v = cJSON_GetObjectItem(v, key);   /* {"currentTime":{"currentTime":ms}} */
    return cJSON_IsNumber(v) ? v->valuedouble : def;
}

static void parse_tracks(const cJSON *root)
{
    const cJSON *arr = find_key(root, "audioTrackInfo", 0);
    if (cJSON_IsArray(arr)) {
        g_n_audio = 0;
        const cJSON *it;
        cJSON_ArrayForEach(it, arr) {
            if (g_n_audio >= MAX_TRACKS) break;
            VideoTrack *t = &g_audio[g_n_audio];
            memset(t, 0, sizeof *t);
            t->index = g_n_audio;
            const cJSON *l = cJSON_GetObjectItem(it, "language"), *c = cJSON_GetObjectItem(it, "codec");
            const cJSON *ch = cJSON_GetObjectItem(it, "channels"), *imm = cJSON_GetObjectItem(it, "immersive");
            if (cJSON_IsString(l) && strcmp(l->valuestring, "(null)")) snprintf(t->lang, sizeof t->lang, "%s", l->valuestring);
            if (cJSON_IsString(c)) snprintf(t->codec, sizeof t->codec, "%s", c->valuestring);
            const char *chs = !cJSON_IsNumber(ch) ? "" : ch->valueint >= 8 ? " 7.1" : ch->valueint >= 6 ? " 5.1" : ch->valueint == 2 ? " stereo" : "";
            snprintf(t->label, sizeof t->label, "%s%s%s", t->lang[0] ? t->lang : "Audio", chs,
                     cJSON_IsString(imm) && strcasecmp(imm->valuestring, "ATMOS") == 0 ? " Atmos" : "");
            t->selected = g_n_audio == 0;
            g_n_audio++;
        }
    }
    arr = find_key(root, "subtitleTrackInfo", 0);
    if (cJSON_IsArray(arr)) {
        g_n_text = 0;
        const cJSON *it;
        cJSON_ArrayForEach(it, arr) {
            if (g_n_text >= MAX_TRACKS) break;
            VideoTrack *t = &g_text[g_n_text];
            memset(t, 0, sizeof *t);
            t->index = g_n_text;
            const cJSON *l = cJSON_GetObjectItem(it, "language"), *c = cJSON_GetObjectItem(it, "codec");
            if (cJSON_IsString(l) && strcmp(l->valuestring, "(null)")) snprintf(t->lang, sizeof t->lang, "%s", l->valuestring);
            if (cJSON_IsString(c)) snprintf(t->codec, sizeof t->codec, "%s", c->valuestring);
            snprintf(t->label, sizeof t->label, "%s", t->lang[0] ? t->lang : "Subtitle");
            g_n_text++;
        }
    }
    g_info.tracks_version++;
}

/* ---- display binding -------------------------------------------------------- */

static void exported_window_apply(void)
{
    if (!g_window_id[0] || !L.ExpSet) return;
    SDL_Rect src = { 0, 0, g_vid_w > 0 ? g_vid_w : 1920, g_vid_h > 0 ? g_vid_h : 1080 };   /* never NULL */
    SDL_Rect dst = g_disp;
    int r = L.ExpSet(g_window_id, &src, &dst);
    LOGI("video(webos): exported window %dx%d -> %d,%d %dx%d = %d", src.w, src.h, dst.x, dst.y, dst.w, dst.h, r);
}

typedef struct { unsigned session; char media[96]; } Bind;

static int bind_valid(const Bind *b)
{
    SDL_LockMutex(g_lock);
    int ok = b->session == g_session && strcmp(b->media, g_media_id) == 0;
    SDL_UnlockMutex(g_lock);
    return ok;
}

static void *acb_bind_thread(void *arg)
{
    Bind *b = arg;
    long task = 0;
    if (!bind_valid(b)) goto out;
    LOGI("acb: setMediaId=%d", L.AcbSetMediaId(g_acb, b->media)); usleep(200000);
    if (!bind_valid(b)) goto out;
    LOGI("acb: loaded=%d", L.AcbSetState(g_acb, ACB_APP_FOREGROUND, ACB_STATE_LOADED, &task)); usleep(200000);
    if (!bind_valid(b)) goto out;
    if (L.AcbConnectDass) { LOGI("acb: connectDass=%d", L.AcbConnectDass(g_acb, ACB_SINK_MAIN, &task)); usleep(200000); }
    if (!bind_valid(b)) goto out;
    if (L.AcbSetMediaVideoData) {
        /* "context" must carry the mediaId: ACB forwards the JSON as is and
         * tv.display answers "Invalid argument" without it. webOS < 5 names the
         * colour blocks mediaSei/mediaVui. */
        int old = webos_major() > 0 && webos_major() < 5;
        char vd[1400];
        snprintf(vd, sizeof vd,
            "{\"content\":\"movie\",\"context\":\"%s\",\"video\":{\"adaptive\":false,\"bitRate\":%ld,"
            "\"data3D\":{\"currentPattern\":\"2d\",\"originalPattern\":\"2d\",\"typeLR\":\"LR\"},"
            "\"frameRate\":%d.0,\"hdrType\":\"none\",\"height\":%d,\"width\":%d,"
            "\"%s\":{\"displayPrimariesX0\":0,\"displayPrimariesX1\":0,\"displayPrimariesX2\":0,\"displayPrimariesY0\":0,"
            "\"displayPrimariesY1\":0,\"displayPrimariesY2\":0,\"maxContentLightLevel\":0,\"maxDisplayMasteringLuminance\":0,"
            "\"maxPicAverageLightLevel\":0,\"minDisplayMasteringLuminance\":0,\"whitePointX\":0,\"whitePointY\":0},"
            "\"%s\":{\"colorPrimaries\":2,\"matrixCoeffs\":2,\"transferCharacteristics\":2,\"videoFullRangeFlag\":false},"
            "\"hfr\":false,\"path\":\"network\",\"pixelAspectRatio\":{\"height\":1,\"width\":1},"
            "\"rotation\":\"0\",\"scanType\":\"%s\",\"specificRendering\":\"none\"}}",
            b->media, g_vid_bitrate, g_vid_fps > 0 ? g_vid_fps : 24, g_vid_h, g_vid_w,
            old ? "mediaSei" : "sei", old ? "mediaVui" : "vui",
            strstr(g_vid_scan, "inter") ? "VIDEO_INTERLACED" : "VIDEO_PROGRESSIVE");
        LOGI("acb: videoData=%d", L.AcbSetMediaVideoData(g_acb, vd, &task));
        usleep(300000);
    }
    if (!bind_valid(b)) goto out;
    {
        int full = g_disp.x == 0 && g_disp.y == 0 && g_disp.w == g_screen_w && g_disp.h == g_screen_h;
        LOGI("acb: window=%d", L.AcbSetDisplayWindow(g_acb, g_disp.x, g_disp.y, g_disp.w, g_disp.h, full, &task));
    }
    if (!bind_valid(b)) goto out;
    LOGI("acb: playing=%d", L.AcbSetState(g_acb, ACB_APP_FOREGROUND, g_paused_by_user ? ACB_STATE_PAUSED : ACB_STATE_PLAYING, &task));
out:
    if (!bind_valid(b)) LOGI("acb: bind abandoned (session changed)");
    free(b);
    __atomic_store_n(&g_bind_running, 0, __ATOMIC_RELEASE);
    return NULL;
}

static void acb_bind_start(void)
{
    if (!g_acb || __atomic_load_n(&g_bind_running, __ATOMIC_ACQUIRE)) return;
    Bind *b = calloc(1, sizeof *b);
    SDL_LockMutex(g_lock);
    b->session = g_session;
    snprintf(b->media, sizeof b->media, "%s", g_media_id);
    SDL_UnlockMutex(g_lock);
    __atomic_store_n(&g_bind_running, 1, __ATOMIC_RELEASE);
    pthread_t t;
    if (pthread_create(&t, NULL, acb_bind_thread, b) != 0) { free(b); __atomic_store_n(&g_bind_running, 0, __ATOMIC_RELEASE); return; }
    pthread_detach(t);
}

static void acb_state(int state)
{
    if (!g_acb) return;
    long task = 0;
    L.AcbSetState(g_acb, g_background ? ACB_APP_BACKGROUND : ACB_APP_FOREGROUND, state, &task);
}

static void acb_notify(long acb_id, long task_id, long event, long app_state, long play_state, const char *reply)
{
    LOGI("acb: notify id=%ld task=%ld event=%ld app=%ld play=%ld %s", acb_id, task_id, event, app_state, play_state, reply ? reply : "");
}

/* ---- subscription events (glib thread) ----------------------------------- */

static int on_event(LSHandle *sh, LSMessage *msg, void *ctx)
{
    (void)sh;
    unsigned session = (unsigned)(uintptr_t)ctx;
    const char *payload = L.MessageGetPayload(msg);
    cJSON *root = cJSON_Parse(payload);
    if (!root) return 1;
    int start_bind = 0;
    SDL_LockMutex(g_lock);
    if (session != g_session) goto done;
    if (!cJSON_GetObjectItem(root, "currentTime") && !cJSON_GetObjectItem(root, "bufferRange")) LOGI("media event: %.400s", payload);

    if (cJSON_GetObjectItem(root, "loadCompleted")) {
        g_info.state = VIDEO_PAUSED;
        g_info.buffering = 0;
        start_bind = 1;
        g_sub.pending = 1;   /* the new pipeline has default subtitles: re-style from the main thread */
    }
    if (cJSON_GetObjectItem(root, "currentTime")) {
        double t = find_number(root, "currentTime", -1);
        if (t >= 0) g_info.position = t / 1000.0;
    }
    if (cJSON_GetObjectItem(root, "sourceInfo")) {
        double d = find_number(root, "duration", -1);
        if (d > 0) g_info.duration = d / 1000.0;
        parse_tracks(root);
    }
    const cJSON *vi = cJSON_GetObjectItem(root, "videoInfo");
    if (vi) {
        int w = (int)find_number(vi, "width", 0), h = (int)find_number(vi, "height", 0);
        double fps = find_number(vi, "frameRate", 0), br = find_number(vi, "bitRate", 0);
        const cJSON *sc = find_key(vi, "scanType", 0), *codec = find_key(vi, "codec", 0), *hdr = find_key(vi, "hdrType", 0);
        int changed = (w > 0 && w != g_vid_w) || (h > 0 && h != g_vid_h);
        if (w > 0) g_vid_w = w;
        if (h > 0) g_vid_h = h;
        if (fps > 0) g_vid_fps = (int)(fps + 0.5);
        if (br > 0) g_vid_bitrate = (long)br;
        g_info.width = g_vid_w; g_info.height = g_vid_h; g_info.fps = g_vid_fps; g_info.bitrate = g_vid_bitrate;
        if (cJSON_IsString(sc)) snprintf(g_vid_scan, sizeof g_vid_scan, "%s", sc->valuestring);
        int has_hdr = cJSON_IsString(hdr) && strcmp(hdr->valuestring, "none") != 0;
        snprintf(g_info.codec, sizeof g_info.codec, "%s%s%dx%d%s%s", cJSON_IsString(codec) ? codec->valuestring : "",
                 cJSON_IsString(codec) ? " " : "", g_vid_w, g_vid_h, has_hdr ? " " : "", has_hdr ? hdr->valuestring : "");
        if (changed) exported_window_apply();
    }
    const cJSON *br = cJSON_GetObjectItem(root, "bufferRange");
    if (br) {
        double end = find_number(br, "endTime", -1), pct = find_number(br, "percent", -1);
        double left = find_number(br, "remainingTime", -1);   /* seconds, as LG reports it */
        g_info.buffer_ahead = left >= 0 ? left : end > 0 && end >= g_info.position ? end - g_info.position : -1;
        if (pct >= 0) g_info.buffer_percent = (int)pct;
        else if (end >= 0 && g_info.duration > 0) g_info.buffer_percent = (int)(100.0 * end / g_info.duration);
    }
    if (cJSON_GetObjectItem(root, "bufferingStart")) g_info.buffering = 1;
    if (cJSON_GetObjectItem(root, "bufferingEnd")) g_info.buffering = 0;
    if (cJSON_GetObjectItem(root, "endOfStream")) g_info.state = VIDEO_ENDED;
    if (cJSON_GetObjectItem(root, "playing")) { g_info.state = VIDEO_PLAYING; g_info.buffering = 0; }
    if (cJSON_GetObjectItem(root, "paused") && g_info.state == VIDEO_PLAYING) g_info.state = VIDEO_PAUSED;
    {
        const cJSON *err = cJSON_GetObjectItem(root, "error");
        const cJSON *code = find_key(err ? err : root, "errorCode", 0), *text = find_key(err ? err : root, "errorText", 0);
        int c = cJSON_IsNumber(code) ? code->valueint : 0;
        const char *tx = cJSON_IsString(text) ? text->valuestring : "";
        if ((err || cJSON_IsString(text)) && c != 0 && strcmp(tx, "No Error") != 0) {
            if (c == 200) {
                snprintf(g_info.warning, sizeof g_info.warning, "This TV can't play this audio track. Try another one from the audio menu.");
                g_info.warning_version++;
            } else {
                snprintf(g_info.error, sizeof g_info.error, "%s (code %d)", tx[0] ? tx : "Playback error", c);
                g_info.state = VIDEO_ERROR;
            }
        }
    }
    const cJSON *ts = cJSON_GetObjectItem(root, "trackSelected");
    if (ts) {
        const cJSON *type = cJSON_GetObjectItem(ts, "type"), *idx = cJSON_GetObjectItem(ts, "index");
        if (cJSON_IsString(type) && cJSON_IsNumber(idx)) {
            VideoTrack *arr = strcmp(type->valuestring, "audio") == 0 ? g_audio : g_text;
            int n = arr == g_audio ? g_n_audio : g_n_text;
            for (int i = 0; i < n; i++) arr[i].selected = arr[i].index == idx->valueint;
            g_info.tracks_version++;
        }
    }
done:
    SDL_UnlockMutex(g_lock);
    cJSON_Delete(root);
    if (start_bind) acb_bind_start();
    return 1;
}

static int on_load_reply(LSHandle *sh, LSMessage *msg, void *ctx)
{
    (void)sh;
    unsigned session = (unsigned)(uintptr_t)ctx;
    const char *payload = L.MessageGetPayload(msg);
    LOGI("media/load -> %s", payload);
    cJSON *root = cJSON_Parse(payload);
    const cJSON *ok = cJSON_GetObjectItem(root, "returnValue"), *mid = cJSON_GetObjectItem(root, "mediaId");
    SDL_LockMutex(g_lock);
    if (session != g_session) { SDL_UnlockMutex(g_lock); cJSON_Delete(root); return 1; }
    if (cJSON_IsTrue(ok) && cJSON_IsString(mid) && mid->valuestring[0] && strcmp(mid->valuestring, "<invalid mediaId>") != 0) {
        snprintf(g_media_id, sizeof g_media_id, "%s", mid->valuestring);
        char sub[200];
        snprintf(sub, sizeof sub, "{\"mediaId\":\"%s\"}", g_media_id);
        SDL_UnlockMutex(g_lock);
        LSError e; L.ErrorInit(&e);
        if (!L.Call(g_bus, "luna://com.webos.media/subscribe", sub, on_event, (void *)(uintptr_t)session, NULL, &e)) ls_error_log("subscribe", &e);
        L.ErrorFree(&e);
    } else {
        const cJSON *et = cJSON_GetObjectItem(root, "errorText"), *ec = cJSON_GetObjectItem(root, "errorCode");
        snprintf(g_info.error, sizeof g_info.error, "The TV refused this stream: %s (code %d)",
                 cJSON_IsString(et) ? et->valuestring : "load failed", cJSON_IsNumber(ec) ? ec->valueint : 0);
        g_info.state = VIDEO_ERROR;
        SDL_UnlockMutex(g_lock);
    }
    cJSON_Delete(root);
    return 1;
}

static void json_escape(const char *in, char *out, size_t n)
{
    size_t o = 0;
    for (; *in && o + 7 < n; in++) {
        unsigned char c = (unsigned char)*in;
        if (c == '"' || c == '\\') { out[o++] = '\\'; out[o++] = (char)c; }
        else if (c < 0x20) o += (size_t)snprintf(out + o, n - o, "\\u%04x", c);
        else out[o++] = (char)c;
    }
    out[o] = 0;
}

static void send_load(void)
{
    char url[4096], payload[5000];
    unsigned session;
    SDL_LockMutex(g_lock);
    json_escape(g_url, url, sizeof url);
    session = g_session;
    SDL_UnlockMutex(g_lock);
    int hls = strstr(url, ".m3u8") != NULL;
    snprintf(payload, sizeof payload,
        "{\"payload\":{\"option\":{\"useSeekableRanges\":true,\"appId\":\"%s\",%s"
        "\"bufferControl\":{\"userBufferCtrl\":false},\"windowId\":\"%s\"}},"
        "\"uri\":\"%s\",\"type\":\"media\"}",
        SNTV_APP_ID, hls ? "\"mediaTransportType\":\"HLS\"," : "",
        g_window_id[0] ? g_window_id : "window_id_dummy", url);
    LOGI("media/load: %.600s", payload);
    LSError e; L.ErrorInit(&e);
    if (!L.CallOneReply(g_bus, "luna://com.webos.media/load", payload, on_load_reply, (void *)(uintptr_t)session, NULL, &e)) {
        SDL_LockMutex(g_lock);
        snprintf(g_info.error, sizeof g_info.error, "Couldn't reach the TV's media service: %s", e.message ? e.message : "LS2 call failed");
        g_info.state = VIDEO_ERROR;
        SDL_UnlockMutex(g_lock);
        ls_error_log("media/load", &e);
    }
    L.ErrorFree(&e);
}

/* ---- lifecycle ------------------------------------------------------------- */

static void *loop_main(void *arg) { (void)arg; L.main_loop_run(g_loop); return NULL; }

static int load_libraries(void)
{
    int ok = 1;
    L.ls = dlopen("libluna-service2.so.3", RTLD_NOW);
    if (!L.ls) L.ls = dlopen("libluna-service2.so", RTLD_NOW);
    L.glib = dlopen("libglib-2.0.so.0", RTLD_NOW);
    if (!L.ls || !L.glib) { LOGE("video(webos): dlopen LS2/glib failed: %s", dlerror()); return 0; }
    SYM(L.ls, Register, "LSRegister", 1);
    SYM(L.ls, Unregister, "LSUnregister", 0);
    SYM(L.ls, GmainContextAttach, "LSGmainContextAttach", 1);
    SYM(L.ls, Call, "LSCall", 1);
    SYM(L.ls, CallOneReply, "LSCallOneReply", 1);
    SYM(L.ls, MessageGetPayload, "LSMessageGetPayload", 1);
    SYM(L.ls, ErrorInit, "LSErrorInit", 1);
    SYM(L.ls, ErrorFree, "LSErrorFree", 1);
    SYM(L.glib, main_context_new, "g_main_context_new", 1);
    SYM(L.glib, main_loop_new, "g_main_loop_new", 1);
    SYM(L.glib, main_loop_run, "g_main_loop_run", 1);
    SYM(L.glib, main_loop_quit, "g_main_loop_quit", 1);
    /* The process's own libSDL2: a second dlopen'ed copy would have separate state. */
    L.self = dlopen(NULL, RTLD_NOW);
    SYM(L.self, ExpCreate, "SDL_webOSCreateExportedWindow", 0);
    SYM(L.self, ExpSet, "SDL_webOSSetExportedWindow", 0);
    SYM(L.self, ExpDestroy, "SDL_webOSDestroyExportedWindow", 0);
    return ok;
}

static int load_acb(void)
{
    int ok = 1;
    L.acb = dlopen("libAcbAPI.so.1", RTLD_NOW);
    if (!L.acb) L.acb = dlopen("libAcbAPI.so", RTLD_NOW);
    if (!L.acb) return 0;
    SYM(L.acb, AcbCreate, "AcbAPI_create", 1);
    SYM(L.acb, AcbInitialize, "AcbAPI_initialize", 1);
    SYM(L.acb, AcbSetSinkType, "AcbAPI_setSinkType", 1);
    SYM(L.acb, AcbSetMediaId, "AcbAPI_setMediaId", 1);
    SYM(L.acb, AcbSetState, "AcbAPI_setState", 1);
    SYM(L.acb, AcbSetDisplayWindow, "AcbAPI_setDisplayWindow", 1);
    SYM(L.acb, AcbConnectDass, "AcbAPI_connectDass", 0);
    SYM(L.acb, AcbSetMediaVideoData, "AcbAPI_setMediaVideoData", 0);
    SYM(L.acb, AcbDestroy, "AcbAPI_destroy", 1);
    SYM(L.acb, AcbFinalize, "AcbAPI_finalize", 0);
    if (!ok) return 0;
    g_acb = L.AcbCreate();
    if (!g_acb || !L.AcbInitialize(g_acb, ACB_PLAYER_VIDEO, SNTV_APP_ID, (void *)acb_notify)) {
        LOGE("video(webos): AcbAPI initialize failed");
        if (g_acb) { if (L.AcbFinalize) L.AcbFinalize(g_acb); L.AcbDestroy(g_acb); }
        g_acb = 0;
        return 0;
    }
    LOGI("video(webos): ACB id=%ld sink=%d", g_acb, L.AcbSetSinkType(g_acb, ACB_SINK_MAIN));
    return 1;
}

/* ---- the TV's own screensaver ------------------------------------------------------
 * Video in a hole under the app's window is not "a video playing" to the TV's power
 * service, so its screensaver came on in the middle of films. The service asks first:
 * registerScreenSaverRequest (subscribed) says when it is about to start, and the app
 * answers on responseScreenSaverRequest with the same timestamp, ack false to hold it
 * off. Held off while a video plays, stalls, loads, or is paused by the pipeline (a
 * stall LG reports as a pause); let through when the viewer paused, it ended or failed,
 * or nothing plays. */
static float g_saver_registered_at = -1e9f;
static int g_saver_ok;   /* the TV took the subscription: no new one at each load (they would pile up) */

static volatile int g_keep_awake;
void video_keep_awake(int on) { g_keep_awake = on; }
static volatile int g_saver_held;   /* the app's own screensaver is up */
static void saver_register(void);
void video_memory_stage(int stage) { (void)stage; }

void video_hold_screensaver(int on)
{
    if (on != g_saver_held) LOGI("video(webos): the app's screensaver %s", on ? "is up: the TV's held off" : "closed");
    g_saver_held = on;
    if (on) saver_register();   /* a TV that dropped the subscription would not ask */
}
int video_paused_on_purpose(void) { return g_paused_by_user; }

static int saver_hold(void)
{
    if (g_keep_awake) return 1;   /* the next episode is on its way */
    if (g_saver_held) return 1;   /* the app's fireplace or clock is already on screen */
    if (g_paused_by_user) return 0;
    if (g_info.state == VIDEO_LOADING) return 1;   /* no media id yet */
    if (!g_media_id[0]) return 0;
    return g_info.state == VIDEO_PLAYING || g_info.state == VIDEO_PAUSED || g_info.buffering;
}

static int on_saver(LSHandle *sh, LSMessage *msg, void *ctx)
{
    (void)ctx;
    const char *payload = L.MessageGetPayload(msg);
    if (payload && (strstr(payload, "\"returnValue\":true") || strstr(payload, "\"returnValue\": true"))) g_saver_ok = 1;
    if (!payload || !strstr(payload, "\"Active\"")) return 1;
    const char *t = strstr(payload, "\"timestamp\"");
    char stamp[48] = "";
    if (t && (t = strchr(t, ':')) != NULL) {   /* copied as written: a large number must not be rounded */
        t++;
        while (*t == ' ') t++;
        size_t k = 0;
        while (t[k] && t[k] != ',' && t[k] != '}' && t[k] != ' ' && k + 1 < sizeof stamp) k++;
        memcpy(stamp, t, k); stamp[k] = 0;
    }
    SDL_LockMutex(g_lock);
    int hold = saver_hold(), buffering = g_info.buffering, paused = g_info.state == VIDEO_PAUSED;
    SDL_UnlockMutex(g_lock);
    char reply[200];
    snprintf(reply, sizeof reply, "{\"clientName\":\"%s\",\"ack\":%s,\"timestamp\":%s}", SNTV_APP_ID, hold ? "false" : "true", stamp[0] ? stamp : "0");
    LSError e; L.ErrorInit(&e);
    if (!L.CallOneReply(sh, "luna://com.webos.service.tvpower/power/responseScreenSaverRequest", reply, NULL, NULL, NULL, &e)) ls_error_log("responseScreenSaverRequest", &e);
    L.ErrorFree(&e);
    LOGI("video(webos): the TV's screensaver %s (%s)", hold ? "held off" : "let through",
         hold ? (g_keep_awake ? "the next video is starting" : g_saver_held ? "the app's screensaver is up" : buffering ? "stalled" : paused ? "paused by the pipeline" : "a video is on") : g_paused_by_user ? "paused by the viewer" : "no video playing");
    return 1;
}

static void saver_register(void)
{
    if (!g_bus || !L.Call) return;
    float now = (float)SDL_GetTicks() / 1000.0f;
    if (now - g_saver_registered_at < (g_saver_ok ? 1800 : 60)) return;   /* taken: renewed every half hour, in case the TV dropped it */
    g_saver_ok = 0;
    g_saver_registered_at = now;
    char p[160];
    snprintf(p, sizeof p, "{\"subscribe\":true,\"clientName\":\"%s\"}", SNTV_APP_ID);
    LSError e; L.ErrorInit(&e);
    if (!L.Call(g_bus, "luna://com.webos.service.tvpower/power/registerScreenSaverRequest", p, on_saver, NULL, NULL, &e)) ls_error_log("registerScreenSaverRequest", &e);
    L.ErrorFree(&e);
}

int video_init(SDL_Window *win)
{
    g_lock = SDL_CreateMutex();
    SDL_GL_GetDrawableSize(win, &g_screen_w, &g_screen_h);
    g_disp.w = g_screen_w; g_disp.h = g_screen_h;
    webos_major();
    if (!load_libraries()) return -1;

    LSError e; L.ErrorInit(&e);
    const char *base = "com.webos.media.client." SNTV_APP_ID;
    if (!L.Register(base, &g_bus, &e)) {
        ls_error_log("LSRegister", &e);
        char alt[160];
        snprintf(alt, sizeof alt, "%s.%d", base, (int)getpid());
        g_bus = NULL;
        if (!L.Register(alt, &g_bus, &e)) { ls_error_log("LSRegister (pid fallback)", &e); L.ErrorFree(&e); g_bus = NULL; return -1; }
        LOGI("video(webos): registered on LS2 as %s", alt);
    } else LOGI("video(webos): registered on LS2 as %s", base);
    void *ctx = L.main_context_new();
    g_loop = L.main_loop_new(ctx, 0);
    if (!L.GmainContextAttach(g_bus, ctx, &e)) {
        ls_error_log("LSGmainContextAttach", &e);
        if (L.Unregister) L.Unregister(g_bus, &e);   /* release the name for the next attempt */
        L.ErrorFree(&e);
        g_bus = NULL;
        return -1;
    }
    L.ErrorFree(&e);
    if (pthread_create(&g_loop_thread, NULL, loop_main, NULL) == 0) g_loop_running = 1;

    if (L.ExpCreate) {
        const char *id = L.ExpCreate(0 /* SDL_WEBOS_EXPORED_WINDOW_TYPE_VIDEO */);
        if (id && *id) {
            snprintf(g_window_id, sizeof g_window_id, "%s", id);
            exported_window_apply();
        }
    }
    if (!g_window_id[0] && !load_acb())
        LOGW("video(webos): neither exported window nor ACB available; expect audio without picture");
    LOGI("video(webos): %s", video_backend_name());
    g_ready = 1;
    saver_register();
    return 0;
}

const char *video_backend_name(void)
{
    return g_window_id[0] ? "webOS media pipeline, exported window" : g_acb ? "webOS media pipeline, ACB" : "webOS media pipeline";
}

void video_shutdown(void)
{
    if (!g_ready) return;
    video_stop();
    if (g_window_id[0] && L.ExpDestroy) L.ExpDestroy(g_window_id);
    if (g_acb) { if (L.AcbFinalize) L.AcbFinalize(g_acb); L.AcbDestroy(g_acb); g_acb = 0; }
    if (g_loop_running) { L.main_loop_quit(g_loop); pthread_join(g_loop_thread, NULL); g_loop_running = 0; }
    LSError e; L.ErrorInit(&e);
    if (g_bus && L.Unregister) L.Unregister(g_bus, &e);   /* frees the bus name for the next launch */
    L.ErrorFree(&e);
    g_bus = NULL;
    g_ready = 0;
}

int video_load(const char *url, const char *title, double start)
{
    (void)title;
    if (!g_ready || !g_bus) {
        if (!g_lock) g_lock = SDL_CreateMutex();
        SDL_LockMutex(g_lock);
        memset(&g_info, 0, sizeof g_info); g_info.buffer_ahead = -1;
        snprintf(g_info.error, sizeof g_info.error, "The TV's media service isn't available to this app. See app.log.");
        g_info.state = VIDEO_ERROR;
        SDL_UnlockMutex(g_lock);
        return -1;
    }
    video_stop();
    g_load_count++;
    g_muted = 0;
    g_volume = 100;
    SDL_LockMutex(g_lock);
    int tv = g_info.tracks_version, wv = g_info.warning_version;
    memset(&g_info, 0, sizeof g_info); g_info.buffer_ahead = -1;
    g_info.tracks_version = tv + 1;
    g_info.warning_version = wv;
    g_info.state = VIDEO_LOADING;
    g_pending_seek = start > 0 ? start : -1;
    g_autoplayed = 0;
    g_paused_by_user = 0;
    g_n_audio = g_n_text = 0;
    g_vid_w = 1920; g_vid_h = 1080; g_vid_fps = 24; g_vid_bitrate = 0;
    snprintf(g_url, sizeof g_url, "%s", url);
    g_load_started = SDL_GetTicks();
    g_session++;
    SDL_UnlockMutex(g_lock);
    send_load();
    saver_register();   /* again now and then: a TV that dropped the subscription asks once more */
    return 0;
}

void video_play(void)  { g_paused_by_user = 0; media_call_id("play", NULL); acb_state(ACB_STATE_PLAYING); }
void video_pause(void) { g_paused_by_user = 1; media_call_id("pause", NULL); acb_state(ACB_STATE_PAUSED); }

void video_seek(double seconds)
{
    char extra[64];
    snprintf(extra, sizeof extra, "\"position\":%lld", (long long)(seconds * 1000));
    media_call_id("seek", extra);
    SDL_LockMutex(g_lock);
    g_info.position = seconds;
    SDL_UnlockMutex(g_lock);
}

void video_set_rate(double rate)
{
    char extra[64];
    snprintf(extra, sizeof extra, "\"playRate\":%.2f,\"audioOutput\":true", rate);
    media_call_id("setPlayRate", extra);
}

void video_set_muted(int muted)
{
    g_muted = muted;
    char v[24]; snprintf(v, sizeof v, "\"volume\":%d", muted ? 0 : g_volume);
    media_call_id("setVolume", v);
}

void video_set_volume(int percent)
{
    g_volume = percent < 0 ? 0 : percent > 100 ? 100 : percent;
    if (!g_muted) video_set_muted(0);
}

unsigned video_load_count(void) { return g_load_count; }

void video_stop(void)
{
    if (!g_lock) return;
    char id[96];
    SDL_LockMutex(g_lock);
    snprintf(id, sizeof id, "%s", g_media_id);
    g_media_id[0] = 0;
    g_session++;              /* late events and an in-flight bind for the old media are ignored */
    if (g_info.state != VIDEO_ERROR) g_info.state = VIDEO_IDLE;
    SDL_UnlockMutex(g_lock);
    if (id[0]) {
        char p[160];
        snprintf(p, sizeof p, "{\"mediaId\":\"%s\"}", id);
        media_call("unload", p);
    }
}

void video_set_display_rect(int x, int y, int w, int h)
{
    if (w > 0 && h > 0 && x == 0 && y == 0) { g_screen_w = w; g_screen_h = h; }
    g_disp.x = x; g_disp.y = y; g_disp.w = w; g_disp.h = h;
    exported_window_apply();
    if (g_acb && L.AcbSetDisplayWindow) {
        long task = 0;
        L.AcbSetDisplayWindow(g_acb, x, y, w, h, x == 0 && y == 0 && w == g_screen_w && h == g_screen_h, &task);
    }
}

void video_app_background(void) { g_background = 1; video_pause(); }
void video_app_foreground(void) { g_background = 0; acb_state(ACB_STATE_PAUSED); }

/* ---- updating the app through the Homebrew Channel ---------------------------------
 * Its service (org.webosbrew.hbchannel.service/install) downloads the package, checks
 * the SHA-256 and installs it, answering with progress on the way. Replies arrive on
 * the bus thread; the app reads the latest one from its own. */
static pthread_mutex_t g_install_lock = PTHREAD_MUTEX_INITIALIZER;
static char g_install_text[160];
static int g_install_state;
/* The app's own bus call can be refused (the service is not among those its
 * permissions name). The same request then goes out through luna-send-pub, the
 * public command any app may run, with its replies written to a file. */
#define PUB_TOOL "/usr/bin/luna-send-pub"
#define PUB_LOG "/tmp/sntv-install.log"
static int g_install_moved;   /* progress seen: a refusal now is a real failure */
static int g_pub_wanted, g_pub;   /* the public way: to start / started */
static char g_pub_payload[900];
static int g_pub_logged;
/* No Homebrew Channel (a TV in Developer Mode): the TV's own installer, as ares-install asks
 * it. It takes a package from a path, so the app downloads the update itself and checks it
 * against the release's SHA-256 first. */
#define DEV_URI "luna://com.webos.appInstallService/dev/install"
#define DEV_LOG "/tmp/sntv-devinstall.log"
static int g_dev_wanted, g_dev;   /* g_dev: 1 downloading, 2 asked on our bus, 3 asked through luna-send-pub */
static int g_dev_pub_wanted, g_dev_moved, g_dev_logged;
static char g_dev_url[1024], g_dev_sha[72], g_dev_id[128], g_dev_path[600];

/* The Homebrew Channel said no before anything moved: the TV's own installer instead. */
static void no_hbchannel(void)
{
    if (g_dev) return;
    g_dev_wanted = 1;
    snprintf(g_install_text, sizeof g_install_text, "No Homebrew Channel: using the TV's own installer\xE2\x80\xA6");
}

static int on_install(LSHandle *sh, LSMessage *msg, void *ctx)
{
    (void)sh; (void)ctx;
    const char *payload = L.MessageGetPayload(msg);
    cJSON *r = cJSON_Parse(payload ? payload : "");
    const cJSON *status = cJSON_GetObjectItemCaseSensitive(r, "statusText"), *pct = cJSON_GetObjectItemCaseSensitive(r, "progress");
    const cJSON *err = cJSON_GetObjectItemCaseSensitive(r, "errorText"), *ok = cJSON_GetObjectItemCaseSensitive(r, "returnValue");
    pthread_mutex_lock(&g_install_lock);
    if ((cJSON_IsFalse(ok) || cJSON_IsString(err)) && !g_install_moved && !g_pub) {
        g_pub_wanted = 1;   /* refused before anything moved: the public way, from the app's thread */
        snprintf(g_install_text, sizeof g_install_text, "Asking the Homebrew Channel another way\xE2\x80\xA6");
    } else if ((cJSON_IsFalse(ok) || cJSON_IsString(err)) && !g_install_moved) {
        /* luna-send-pub is asking now: its answer decides */
    } else if (cJSON_IsFalse(ok) || cJSON_IsString(err)) {
        g_install_state = -1;
        snprintf(g_install_text, sizeof g_install_text, "%.150s", cJSON_IsString(err) ? err->valuestring : "the Homebrew Channel refused the update");
    } else if (cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(r, "finished"))) {
        g_install_state = 2;
        snprintf(g_install_text, sizeof g_install_text, "Installed");
    } else if (cJSON_IsString(status)) {
        g_install_moved = 1;
        if (cJSON_IsNumber(pct)) snprintf(g_install_text, sizeof g_install_text, "%.100s  %d%%", status->valuestring, (int)pct->valuedouble);
        else snprintf(g_install_text, sizeof g_install_text, "%.140s", status->valuestring);
    }
    pthread_mutex_unlock(&g_install_lock);
    LOGI("update: installer says %.200s", payload ? payload : "");
    if (r) cJSON_Delete(r);
    return 1;
}

/* a request through luna-send-pub, its replies written to log; 0 when it went out */
static int send_pub(const char *uri, const char *payload, const char *log)
{
    if (access(PUB_TOOL, X_OK) != 0) return -1;
    for (const char *c = payload; *c; c++) if (*c == '\'') return -1;   /* it goes in single quotes */
    unlink(log);
    char cmd[1600];
    snprintf(cmd, sizeof cmd, "nohup " PUB_TOOL " -i -f %s '%s' > %s 2>&1 &", uri, payload, log);
    return system(cmd) == 0 ? 0 : -1;
}

static int install_pub(void)
{
    g_pub_logged = 0;
    LOGI("update: asking the Homebrew Channel through luna-send-pub");
    return send_pub("luna://org.webosbrew.hbchannel.service/install", g_pub_payload, PUB_LOG);
}

/* the latest of each field in the replies written so far */
static void pub_read(void)
{
    FILE *f = fopen(PUB_LOG, "rb");
    if (!f) return;
    static char buf[16384];
    if (fseek(f, 0, SEEK_END) == 0) { long size = ftell(f); if (size > (long)sizeof buf - 1) fseek(f, size - (long)(sizeof buf - 1), SEEK_SET); else rewind(f); }   /* the latest replies */
    size_t n = fread(buf, 1, sizeof buf - 1, f);
    fclose(f);
    buf[n] = 0;
    const char *p, *last;
    char status[100] = "";
    double pct = -1;
    for (p = buf, last = NULL; (p = strstr(p, "\"progress\"")) != NULL; p++) last = p;
    if (last) { const char *c = strchr(last, ':'); if (c) pct = atof(c + 1); }
    for (p = buf, last = NULL; (p = strstr(p, "\"statusText\"")) != NULL; p++) last = p;
    if (last) { const char *a = strchr(last + 12, '"'); const char *b = a ? strchr(a + 1, '"') : NULL; if (a && b && b - a - 1 < (long)sizeof status) { memcpy(status, a + 1, (size_t)(b - a - 1)); status[b - a - 1] = 0; } }
    int done = strstr(buf, "\"finished\": true") || strstr(buf, "\"finished\":true");
    const char *err = strstr(buf, "\"errorText\"");
    int refused = !err && (strstr(buf, "\"returnValue\": false") || strstr(buf, "\"returnValue\":false"));
    pthread_mutex_lock(&g_install_lock);
    if (status[0]) g_install_moved = 1;
    if (done) { g_install_state = 2; snprintf(g_install_text, sizeof g_install_text, "Installed"); }
    else if ((refused || err) && !g_install_moved) no_hbchannel();   /* no Homebrew Channel to answer */
    else if (refused) { g_install_state = -1; snprintf(g_install_text, sizeof g_install_text, "the Homebrew Channel refused the update"); }
    else if (err) {
        const char *a = strchr(err + 11, '"'); const char *b = a ? strchr(a + 1, '"') : NULL;
        g_install_state = -1;
        snprintf(g_install_text, sizeof g_install_text, "%.*s", a && b ? (int)(b - a - 1 > 150 ? 150 : b - a - 1) : 40, a && b ? a + 1 : "the Homebrew Channel refused the update");
    } else if (status[0]) {
        if (pct >= 0) snprintf(g_install_text, sizeof g_install_text, "%s  %d%%", status, (int)pct);
        else snprintf(g_install_text, sizeof g_install_text, "%s", status);
    }
    int st = g_install_state;
    pthread_mutex_unlock(&g_install_lock);
    if (st != 1 && !g_pub_logged) { g_pub_logged = 1; LOGI("update: installer (luna-send-pub) says %.200s", buf); }
}

static void dev_fail(const char *why)
{
    pthread_mutex_lock(&g_install_lock);
    g_install_state = -1;
    snprintf(g_install_text, sizeof g_install_text, "%.150s", why);
    pthread_mutex_unlock(&g_install_lock);
    LOGW("update: %s", why);
}

/* The installer's answers: {"details":{"state":"installing"...}}, at the end "installed", or
 * "errorCode"/"reason" when it refused (not in Developer Mode, a package it would not take). */
static int on_dev(LSHandle *sh, LSMessage *msg, void *ctx)
{
    (void)sh; (void)ctx;
    const char *payload = L.MessageGetPayload(msg);
    cJSON *r = cJSON_Parse(payload ? payload : "");
    const cJSON *d = cJSON_GetObjectItemCaseSensitive(r, "details");
    const cJSON *state = cJSON_GetObjectItemCaseSensitive(d, "state"), *code = cJSON_GetObjectItemCaseSensitive(d, "errorCode");
    const cJSON *reason = cJSON_GetObjectItemCaseSensitive(d, "reason"), *ok = cJSON_GetObjectItemCaseSensitive(r, "returnValue");
    const cJSON *err = cJSON_GetObjectItemCaseSensitive(r, "errorText");
    pthread_mutex_lock(&g_install_lock);
    if ((cJSON_IsFalse(ok) || cJSON_IsString(err)) && !g_dev_moved && g_dev == 2) g_dev_pub_wanted = 1;   /* not for our bus: the public way */
    else if (cJSON_IsNumber(code) || (cJSON_IsString(state) && strstr(state->valuestring, "fail"))) {
        g_install_state = -1;
        snprintf(g_install_text, sizeof g_install_text, "The TV's installer refused it: %.110s", cJSON_IsString(reason) ? reason->valuestring : cJSON_IsString(state) ? state->valuestring : "an error");
    } else if (cJSON_IsString(state) && !strcmp(state->valuestring, "installed")) {
        g_install_state = 2;
        snprintf(g_install_text, sizeof g_install_text, "Installed");
    } else if (cJSON_IsString(state)) {
        g_dev_moved = 1;
        snprintf(g_install_text, sizeof g_install_text, "Installing (%.100s)", state->valuestring);
    }
    pthread_mutex_unlock(&g_install_lock);
    LOGI("update: the TV's installer says %.200s", payload ? payload : "");
    if (r) cJSON_Delete(r);
    return 1;
}

static void dev_payload(char *out, size_t n)
{
    cJSON *p = cJSON_CreateObject();
    cJSON_AddStringToObject(p, "id", g_dev_id);
    cJSON_AddStringToObject(p, "ipkUrl", g_dev_path);
    cJSON_AddTrueToObject(p, "subscribe");
    char *txt = cJSON_PrintUnformatted(p);
    cJSON_Delete(p);
    snprintf(out, n, "%s", txt ? txt : "{}");
    free(txt);
}

static void dev_ask_pub(void)
{
    char payload[900];
    dev_payload(payload, sizeof payload);
    LOGI("update: asking the TV's installer through luna-send-pub");
    if (send_pub(DEV_URI, payload, DEV_LOG) == 0) g_dev = 3;
    else dev_fail("This TV did not let the app install the update (no Homebrew Channel, and its installer refused)");
}

static void dev_ask(void)
{
    char payload[900];
    dev_payload(payload, sizeof payload);
    pthread_mutex_lock(&g_install_lock);
    g_dev = 2;
    snprintf(g_install_text, sizeof g_install_text, "Installing with the TV's installer\xE2\x80\xA6");
    pthread_mutex_unlock(&g_install_lock);
    LOGI("update: asking the TV's installer for %s", g_dev_path);
    LSError e; L.ErrorInit(&e);
    int sent = g_bus && L.Call && L.Call(g_bus, DEV_URI, payload, on_dev, NULL, NULL, &e);
    if (!sent) { if (g_bus && L.Call) ls_error_log("dev install", &e); dev_ask_pub(); }
    L.ErrorFree(&e);
}

/* The latest state in the installer's replies through luna-send-pub. */
static void dev_read(void)
{
    FILE *f = fopen(DEV_LOG, "rb");
    if (!f) return;
    static char buf[16384];
    if (fseek(f, 0, SEEK_END) == 0) { long size = ftell(f); if (size > (long)sizeof buf - 1) fseek(f, size - (long)(sizeof buf - 1), SEEK_SET); else rewind(f); }
    size_t n = fread(buf, 1, sizeof buf - 1, f);
    fclose(f);
    buf[n] = 0;
    char state[64] = "", reason[120] = "";
    const char *p, *last = NULL;
    for (p = buf; (p = strstr(p, "\"state\"")) != NULL; p++) last = p;
    if (last) { const char *a = strchr(last + 7, '"'); const char *b = a ? strchr(a + 1, '"') : NULL; if (a && b && b - a - 1 < (long)sizeof state) { memcpy(state, a + 1, (size_t)(b - a - 1)); state[b - a - 1] = 0; } }
    if ((p = strstr(buf, "\"reason\"")) != NULL) { const char *a = strchr(p + 8, '"'); const char *b = a ? strchr(a + 1, '"') : NULL; if (a && b && b - a - 1 < (long)sizeof reason) { memcpy(reason, a + 1, (size_t)(b - a - 1)); reason[b - a - 1] = 0; } }
    int refused = !state[0] && (strstr(buf, "\"returnValue\": false") || strstr(buf, "\"returnValue\":false"));
    pthread_mutex_lock(&g_install_lock);
    if (!strcmp(state, "installed")) { g_install_state = 2; snprintf(g_install_text, sizeof g_install_text, "Installed"); }
    else if (strstr(buf, "\"errorCode\"") || strstr(state, "fail") || refused) {
        g_install_state = -1;
        snprintf(g_install_text, sizeof g_install_text, "The TV's installer refused it: %.110s", reason[0] ? reason : state[0] ? state : "is this TV in Developer Mode?");
    } else if (state[0]) snprintf(g_install_text, sizeof g_install_text, "Installing (%.100s)", state);
    int st = g_install_state;
    pthread_mutex_unlock(&g_install_lock);
    if (st != 1 && !g_dev_logged) { g_dev_logged = 1; LOGI("update: the TV's installer (luna-send-pub) says %.200s", buf); }
}

/* On a network worker: the package, checked against the release's hash, saved where the
 * installer can read it. */
static void dev_fetch(HttpResponse *r, void *arg)
{
    (void)arg;
    HttpResponse got;
    memset(&got, 0, sizeof got);
    http_request(g_dev_url, NULL, &got);
    if (got.status != 200 || !got.body || got.len < 1024) {
        r->status = -1;
        snprintf(r->error, sizeof r->error, "The update did not download (%ld %.180s)", got.status, got.error);
    } else {
        char hex[65];
        sha256_hex(got.body, got.len, hex);
        FILE *f = NULL;
        if (g_dev_sha[0] && strcasecmp(hex, g_dev_sha)) { r->status = -1; snprintf(r->error, sizeof r->error, "The download did not match the release (SHA-256)"); }
        else if (!(f = fopen(g_dev_path, "wb")) || fwrite(got.body, 1, got.len, f) != got.len) { r->status = -1; snprintf(r->error, sizeof r->error, "No room to keep the update (%.200s)", g_dev_path); }
        else r->status = 200;
        if (f && fclose(f) != 0 && r->status == 200) { r->status = -1; snprintf(r->error, sizeof r->error, "No room to keep the update (%.200s)", g_dev_path); }
    }
    http_response_free(&got);
}

static void dev_fetched(HttpResponse *r, void *user)
{
    (void)user;
    if (r->status != 200) { unlink(g_dev_path); dev_fail(r->error); return; }
    LOGI("update: downloaded and checked, %s", g_dev_path);
    dev_ask();
}

/* Where the package waits for the installer: the developer partition's temp folder, as
 * ares-install uses, else the app's own data folder. */
static void dev_download(void)
{
    const char *dirs[] = { "/media/developer/temp", store_dir() };
    g_dev_path[0] = 0;
    for (size_t i = 0; i < sizeof dirs / sizeof dirs[0] && !g_dev_path[0]; i++) {
        if (!dirs[i] || !*dirs[i]) continue;
        char test[600];
        snprintf(test, sizeof test, "%s/%s-update.ipk", dirs[i], g_dev_id);
        FILE *f = fopen(test, "wb");
        if (f) { fclose(f); snprintf(g_dev_path, sizeof g_dev_path, "%s", test); }
    }
    if (!g_dev_path[0]) { dev_fail("No folder the update could be saved in"); return; }
    pthread_mutex_lock(&g_install_lock);
    g_dev = 1;
    snprintf(g_install_text, sizeof g_install_text, "Downloading the update\xE2\x80\xA6");
    pthread_mutex_unlock(&g_install_lock);
    LOGI("update: no Homebrew Channel here; downloading for the TV's own installer");
    http_task_async("update download", dev_fetch, NULL, dev_fetched, NULL);
}

int video_install_update(const char *ipk_url, const char *sha256, const char *app_id)
{
    cJSON *p = cJSON_CreateObject();
    cJSON_AddStringToObject(p, "ipkUrl", ipk_url);
    cJSON_AddStringToObject(p, "ipkHash", sha256);
    cJSON_AddStringToObject(p, "id", app_id);
    cJSON_AddTrueToObject(p, "subscribe");   /* progress comes as further replies */
    char *payload = cJSON_PrintUnformatted(p);
    cJSON_Delete(p);
    snprintf(g_pub_payload, sizeof g_pub_payload, "%s", payload);
    snprintf(g_dev_url, sizeof g_dev_url, "%s", ipk_url);
    snprintf(g_dev_sha, sizeof g_dev_sha, "%s", sha256 ? sha256 : "");
    snprintf(g_dev_id, sizeof g_dev_id, "%s", app_id);
    pthread_mutex_lock(&g_install_lock);
    g_install_state = 1; g_install_moved = 0; g_pub = 0; g_pub_wanted = 0;
    g_dev = 0; g_dev_wanted = 0; g_dev_pub_wanted = 0; g_dev_moved = 0; g_dev_logged = 0;
    snprintf(g_install_text, sizeof g_install_text, "Asking the Homebrew Channel\xE2\x80\xA6");
    pthread_mutex_unlock(&g_install_lock);
    if (!g_bus || !L.Call) {   /* no bus of our own: the public way only */
        free(payload);
        if (install_pub() != 0) return -1;
        g_pub = 1;
        return 0;
    }
    LSError e; L.ErrorInit(&e);
    int sent = L.Call(g_bus, "luna://org.webosbrew.hbchannel.service/install", payload, on_install, NULL, NULL, &e);
    if (!sent) {
        ls_error_log("hbchannel install", &e);
        pthread_mutex_lock(&g_install_lock);
        g_pub_wanted = 1;   /* the public way, next time the screen asks */
        pthread_mutex_unlock(&g_install_lock);
    }
    L.ErrorFree(&e);
    free(payload);
    return 0;
}

int video_install_status(char *out, size_t n)
{
    pthread_mutex_lock(&g_install_lock);
    int start_pub = g_pub_wanted;
    g_pub_wanted = 0;
    pthread_mutex_unlock(&g_install_lock);
    if (start_pub) {
        LOGI("update: the app's own call was refused; trying luna-send-pub");
        if (install_pub() == 0) g_pub = 1;
        else {
            pthread_mutex_lock(&g_install_lock);
            no_hbchannel();
            pthread_mutex_unlock(&g_install_lock);
        }
    }
    pthread_mutex_lock(&g_install_lock);
    int start_dev = g_dev_wanted, dev_pub = g_dev_pub_wanted;
    g_dev_wanted = g_dev_pub_wanted = 0;
    pthread_mutex_unlock(&g_install_lock);
    if (start_dev) dev_download();
    if (dev_pub) dev_ask_pub();
    static unsigned polls;
    if (g_pub && !g_dev && polls % 15 == 0) pub_read();   /* a few times a second */
    if (g_dev == 3 && polls % 15 == 0) dev_read();
    polls++;
    pthread_mutex_lock(&g_install_lock);
    snprintf(out, n, "%s", g_install_text);
    int s = g_install_state;
    pthread_mutex_unlock(&g_install_lock);
    return s;
}

void video_update(void)
{
    if (!g_ready) return;
    SDL_LockMutex(g_lock);
    int loaded = g_info.state == VIDEO_PAUSED && g_media_id[0] && !g_autoplayed;
    double seek = g_pending_seek;
    int loading = g_info.state == VIDEO_LOADING;
    Uint32 started = g_load_started;
    SDL_UnlockMutex(g_lock);
    SDL_LockMutex(g_lock);
    int restyle = g_sub.pending;
    g_sub.pending = 0;
    SDL_UnlockMutex(g_lock);
    if (restyle) subtitle_style_send();
    if (loaded) {
        g_autoplayed = 1;
        if (g_muted || g_volume < 100) video_set_muted(g_muted);   /* before the first sound */
        if (seek > 0) video_seek(seek);
        if (g_paused_by_user || g_background) LOGI("video(webos): loaded while paused (the TV's Home, a break): it waits");   /* paused while it loaded: stays so */
        else video_play();
    } else if (loading && SDL_GetTicks() - started > 40000) {
        SDL_LockMutex(g_lock);
        snprintf(g_info.error, sizeof g_info.error, "The TV didn't start this stream within 40 seconds");
        g_info.state = VIDEO_ERROR;
        SDL_UnlockMutex(g_lock);
    }
}

void video_info(VideoInfo *out)
{
    if (!g_lock) { memset(out, 0, sizeof *out); return; }
    SDL_LockMutex(g_lock);
    *out = g_info;
    SDL_UnlockMutex(g_lock);
}

int video_tracks(TrackKind kind, VideoTrack *out, int max)
{
    if (!g_lock) return 0;
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
        media_call_id("setSubtitleEnable", extra);
        if (index < 0) return;
    }
    snprintf(extra, sizeof extra, "\"type\":\"%s\",\"index\":%d", kind == TRACK_AUDIO ? "audio" : "text", index);
    media_call_id("selectTrack", extra);
    if (kind == TRACK_TEXT) subtitle_style_send();
}

void video_landscape_only(int on) { (void)on; }
int video_take_open_url(char *out, size_t n) { (void)out; (void)n; return 0; }   /* only Android is opened with links */   /* TVs and the desktop do not turn */
void video_pip_allow(int on) { (void)on; }   /* no picture in picture on a TV or the desktop */
int  video_pip_enter(void) { return 0; }
int  video_in_pip(void) { return 0; }
float video_device_volume(void) { return -1; }   /* a TV's own remote has those */
int   video_set_device_volume(float v) { (void)v; return 0; }
float video_brightness(void) { return -1; }
int   video_set_brightness(float v) { (void)v; return 0; }
