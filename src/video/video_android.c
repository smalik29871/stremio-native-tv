/* Android backend: ExoPlayer (Media3) under SDL's surface, in
 * deploy/android/app/src/main/java/app/cuelight/tv/CuelightPlayer.java. Every call goes to
 * its static methods over JNI; video_update reads the snapshot it keeps. */
#include "video.h"
#include "../log.h"
#include <SDL.h>
#include <SDL_system.h>
#include <jni.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { M_LOAD, M_PLAY, M_PAUSE, M_SEEK, M_STOP, M_RATE, M_VOLUME, M_MUTED, M_AWAKE, M_RECT, M_SELECT, M_STYLE,
       M_STATUS, M_ERROR, M_CODEC, M_WARNING, M_TRACKS, M_LANDSCAPE, M_DEV_VOL, M_SET_DEV_VOL, M_BRIGHT, M_SET_BRIGHT,
       M_INSTALL, M_INSTALL_STATE, M_INSTALL_TEXT, M_PIP_ALLOW, M_PIP_ENTER, M_IN_PIP, M_COUNT };
static const struct { const char *name, *sig; } METHODS[M_COUNT] = {
    { "load", "(Ljava/lang/String;D)V" }, { "play", "()V" }, { "pause", "()V" }, { "seek", "(D)V" }, { "stop", "()V" },
    { "setRate", "(D)V" }, { "setVolume", "(I)V" }, { "setMuted", "(Z)V" }, { "setKeepAwake", "(Z)V" }, { "setRect", "(IIII)V" },
    { "selectTrack", "(II)V" }, { "subtitleStyle", "(ILjava/lang/String;I)V" },
    { "status", "()[D" }, { "error", "()Ljava/lang/String;" }, { "codec", "()Ljava/lang/String;" }, { "warning", "()Ljava/lang/String;" },
    { "tracks", "(I)[Ljava/lang/String;" }, { "landscapeOnly", "(Z)V" },
    { "deviceVolume", "()F" }, { "setDeviceVolume", "(F)V" }, { "brightness", "()F" }, { "setBrightness", "(F)V" },
    { "installUpdate", "(Ljava/lang/String;Ljava/lang/String;)V" }, { "installState", "()I" }, { "installText", "()Ljava/lang/String;" },
    { "pipAllow", "(Z)V" }, { "pipEnter", "()Z" }, { "inPip", "()Z" },
};
static jclass g_cls;
static jmethodID g_m[M_COUNT];
static VideoInfo g;
static unsigned g_loads;
static int g_paused_on_purpose, g_warn_seen, g_tracks_seen = -1;

static JNIEnv *env(void) { return g_cls ? (JNIEnv *)SDL_AndroidGetJNIEnv() : NULL; }
static void clear(JNIEnv *e) { if ((*e)->ExceptionCheck(e)) { (*e)->ExceptionDescribe(e); (*e)->ExceptionClear(e); } }
#define CALL(m, ...) do { JNIEnv *e_ = env(); if (e_) { (*e_)->CallStaticVoidMethod(e_, g_cls, g_m[m], ##__VA_ARGS__); clear(e_); } } while (0)

int video_init(SDL_Window *win)
{
    (void)win;
    JNIEnv *e = (JNIEnv *)SDL_AndroidGetJNIEnv();
    jobject act = (jobject)SDL_AndroidGetActivity();
    if (!e || !act) return -1;
    /* the app's class loader: this thread was not started by Java, FindClass would not see it */
    jclass ac = (*e)->GetObjectClass(e, act);
    jobject loader = (*e)->CallObjectMethod(e, ac, (*e)->GetMethodID(e, (*e)->FindClass(e, "java/lang/Class"), "getClassLoader", "()Ljava/lang/ClassLoader;"));
    jclass lc = (*e)->FindClass(e, "java/lang/ClassLoader");
    jstring name = (*e)->NewStringUTF(e, "app.cuelight.tv.CuelightPlayer");
    jclass cls = (jclass)(*e)->CallObjectMethod(e, loader, (*e)->GetMethodID(e, lc, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;"), name);
    (*e)->DeleteLocalRef(e, name);
    (*e)->DeleteLocalRef(e, act);
    if ((*e)->ExceptionCheck(e) || !cls) { clear(e); LOGE("video: the player class is missing"); return -1; }
    for (int i = 0; i < M_COUNT; i++) {
        g_m[i] = (*e)->GetStaticMethodID(e, cls, METHODS[i].name, METHODS[i].sig);
        if (!g_m[i]) { clear(e); LOGE("video: no CuelightPlayer.%s", METHODS[i].name); return -1; }
    }
    g_cls = (jclass)(*e)->NewGlobalRef(e, cls);
    g.state = VIDEO_IDLE; g.buffer_ahead = -1;
    return 0;
}

void video_shutdown(void) { video_stop(); }
const char *video_backend_name(void) { return g_cls ? "ExoPlayer (Media3)" : "none (the player did not start)"; }

int video_load(const char *url, const char *title, double start)
{
    JNIEnv *e = env();
    g_loads++;
    LOGI("video: load (%s) from %.0fs", title ? title : "", start);
    memset(&g, 0, sizeof g);
    g.buffer_ahead = -1;
    g.position = start;
    g_paused_on_purpose = 0;
    if (!e) { g.state = VIDEO_ERROR; snprintf(g.error, sizeof g.error, "The video player did not start on this device"); return -1; }
    g.state = VIDEO_LOADING;
    jstring u = (*e)->NewStringUTF(e, url);
    (*e)->CallStaticVoidMethod(e, g_cls, g_m[M_LOAD], u, (jdouble)(start > 0 ? start : 0));
    (*e)->DeleteLocalRef(e, u);
    clear(e);
    return 0;
}

void video_play(void) { g_paused_on_purpose = 0; CALL(M_PLAY); if (g.state == VIDEO_PAUSED) g.state = VIDEO_PLAYING; }
void video_pause(void) { g_paused_on_purpose = 1; CALL(M_PAUSE); if (g.state == VIDEO_PLAYING) g.state = VIDEO_PAUSED; }
void video_seek(double s) { if (s < 0) s = 0; CALL(M_SEEK, (jdouble)s); g.position = s; }
void video_stop(void) { CALL(M_STOP); g.state = VIDEO_IDLE; }
void video_set_rate(double r) { CALL(M_RATE, (jdouble)r); }
void video_set_volume(int v) { CALL(M_VOLUME, (jint)v); }
void video_set_muted(int m) { CALL(M_MUTED, (jboolean)(m != 0)); }
unsigned video_load_count(void) { return g_loads; }
void video_set_display_rect(int x, int y, int w, int h) { CALL(M_RECT, (jint)x, (jint)y, (jint)w, (jint)h); }
void video_app_background(void) { video_pause(); }
void video_app_foreground(void) {}
void video_landscape_only(int on) { CALL(M_LANDSCAPE, (jboolean)(on != 0)); }
void video_pip_allow(int on) { static int was = -1; if (on != was) { was = on; CALL(M_PIP_ALLOW, (jboolean)(on != 0)); } }
static int get_bool(int m)
{
    JNIEnv *e = env();
    if (!e) return 0;
    jboolean v = (*e)->CallStaticBooleanMethod(e, g_cls, g_m[m]);
    if ((*e)->ExceptionCheck(e)) { clear(e); return 0; }
    return v != 0;
}
int video_pip_enter(void) { return get_bool(M_PIP_ENTER); }
int video_in_pip(void) { return get_bool(M_IN_PIP); }

static float get_float(int m)
{
    JNIEnv *e = env();
    if (!e) return -1;
    float v = (float)(*e)->CallStaticFloatMethod(e, g_cls, g_m[m]);
    if ((*e)->ExceptionCheck(e)) { clear(e); return -1; }
    return v;
}
float video_device_volume(void) { return get_float(M_DEV_VOL); }
int   video_set_device_volume(float v) { if (!env()) return 0; CALL(M_SET_DEV_VOL, (jfloat)v); return 1; }
float video_brightness(void) { return get_float(M_BRIGHT); }
int   video_set_brightness(float v) { if (!env()) return 0; CALL(M_SET_BRIGHT, (jfloat)v); return 1; }
void video_keep_awake(int on) { CALL(M_AWAKE, (jboolean)(on != 0)); }
int video_paused_on_purpose(void) { return g_paused_on_purpose; }

/* Updating itself comes in a later milestone (PackageInstaller); until then the release page. */
/* The update: CuelightPlayer downloads the APK, checks its SHA-256 and opens Android's
 * installer, which asks the viewer (and, the first time, whether Cuelight may install apps). */
int video_install_update(const char *url, const char *sha256, const char *app_id)
{
    (void)app_id;
    JNIEnv *e = env();
    if (!e || !url || !*url) return -1;
    jstring u = (*e)->NewStringUTF(e, url), h = (*e)->NewStringUTF(e, sha256 ? sha256 : "");
    (*e)->CallStaticVoidMethod(e, g_cls, g_m[M_INSTALL], u, h);
    (*e)->DeleteLocalRef(e, u);
    (*e)->DeleteLocalRef(e, h);
    clear(e);
    return 0;
}
int video_install_status(char *out, size_t n)
{
    JNIEnv *e = env();
    if (n) out[0] = 0;
    if (!e) return 0;
    int st = (int)(*e)->CallStaticIntMethod(e, g_cls, g_m[M_INSTALL_STATE]);
    clear(e);
    jstring t = (jstring)(*e)->CallStaticObjectMethod(e, g_cls, g_m[M_INSTALL_TEXT]);
    clear(e);
    if (t) { const char *c = (*e)->GetStringUTFChars(e, t, NULL); if (c) { snprintf(out, n, "%s", c); (*e)->ReleaseStringUTFChars(e, t, c); } (*e)->DeleteLocalRef(e, t); }
    return st;
}

static void copy_string(JNIEnv *e, int m, char *out, size_t n)
{
    jstring s = (jstring)(*e)->CallStaticObjectMethod(e, g_cls, g_m[m]);
    clear(e);
    out[0] = 0;
    if (!s) return;
    const char *c = (*e)->GetStringUTFChars(e, s, NULL);
    if (c) { snprintf(out, n, "%s", c); (*e)->ReleaseStringUTFChars(e, s, c); }
    (*e)->DeleteLocalRef(e, s);
}

void video_update(void)
{
    JNIEnv *e = env();
    if (!e) return;
    jdoubleArray a = (jdoubleArray)(*e)->CallStaticObjectMethod(e, g_cls, g_m[M_STATUS]);
    clear(e);
    if (!a) return;
    double s[14] = { 0 };
    jsize n = (*e)->GetArrayLength(e, a);
    (*e)->GetDoubleArrayRegion(e, a, 0, n < 14 ? n : 14, s);
    (*e)->DeleteLocalRef(e, a);
    VideoState was = g.state;
    if (!(g.state == VIDEO_LOADING && s[0] == VIDEO_IDLE))   /* the load not yet seen on the other side */
        g.state = (VideoState)(int)s[0];
    if (g.state != VIDEO_IDLE) {
        g.position = s[1]; g.duration = s[2]; g.buffering = s[3] != 0; g.buffer_percent = (int)s[4]; g.buffer_ahead = s[5];
        g.width = (int)s[6]; g.height = (int)s[7]; g.fps = (int)s[8]; g.bitrate = (long)s[9];
    }
    g.tracks_version = (int)s[10];
    if (g.state == VIDEO_ERROR && was != VIDEO_ERROR) { copy_string(e, M_ERROR, g.error, sizeof g.error); LOGW("video: %s", g.error); }
    if ((int)s[11] != g_warn_seen) {
        g_warn_seen = (int)s[11];
        copy_string(e, M_WARNING, g.warning, sizeof g.warning);
        if (g.warning[0]) g.warning_version++;
    }
    if (g.tracks_version != g_tracks_seen && g.width) { g_tracks_seen = g.tracks_version; copy_string(e, M_CODEC, g.codec, sizeof g.codec); }
}

void video_info(VideoInfo *out) { *out = g; }

int video_tracks(TrackKind kind, VideoTrack *out, int max)
{
    JNIEnv *e = env();
    if (!e) return 0;
    jobjectArray arr = (jobjectArray)(*e)->CallStaticObjectMethod(e, g_cls, g_m[M_TRACKS], (jint)kind);
    clear(e);
    if (!arr) return 0;
    int n = 0;
    jsize len = (*e)->GetArrayLength(e, arr);
    for (jsize i = 0; i < len && n < max; i++) {
        jstring js = (jstring)(*e)->GetObjectArrayElement(e, arr, i);
        const char *c = js ? (*e)->GetStringUTFChars(e, js, NULL) : NULL;
        if (c) {   /* index \t lang \t label \t codec \t selected */
            char row[256]; snprintf(row, sizeof row, "%s", c);
            char *f[5] = { 0 }; int k = 0;
            for (char *p = row, *t; k < 5 && (t = strsep(&p, "\t")); ) f[k++] = t;
            if (k == 5) {
                VideoTrack *t = &out[n++];
                t->index = atoi(f[0]);
                snprintf(t->lang, sizeof t->lang, "%s", f[1]);
                snprintf(t->label, sizeof t->label, "%s", f[2]);
                snprintf(t->codec, sizeof t->codec, "%s", f[3]);
                t->selected = f[4][0] == '1';
            }
            (*e)->ReleaseStringUTFChars(e, js, c);
        }
        if (js) (*e)->DeleteLocalRef(e, js);
    }
    (*e)->DeleteLocalRef(e, arr);
    return n;
}

void video_select_track(TrackKind kind, int index) { CALL(M_SELECT, (jint)kind, (jint)index); }

void video_subtitle_style(int size, int style, const char *colour, int position, int sync_ms)
{
    (void)style; (void)sync_ms;
    static const struct { const char *name, *hex; } C[] = { { "white", "#FFFFFF" }, { "yellow", "#FFE14D" }, { "green", "#5BE37D" }, { "cyan", "#4DD9FF" }, { "blue", "#5B8CFF" }, { "magenta", "#FF66D9" }, { "red", "#FF5B5B" }, { "black", "#000000" } };
    const char *hex = NULL;
    for (size_t i = 0; colour && i < sizeof C / sizeof C[0]; i++) if (!SDL_strcasecmp(colour, C[i].name)) hex = C[i].hex;
    JNIEnv *e = env();
    if (!e) return;
    jstring c = hex ? (*e)->NewStringUTF(e, hex) : NULL;
    (*e)->CallStaticVoidMethod(e, g_cls, g_m[M_STYLE], (jint)size, c, (jint)position);
    if (c) (*e)->DeleteLocalRef(e, c);
    clear(e);
}
