/* Tizen 5.5+ WebAssembly (.wgt) backend: an HTML5 <video> element behind the
 * transparent canvas. Samsung's web engine does the decoding. */
#include "video.h"
#include <emscripten.h>
#include <stdio.h>
#include <string.h>

static VideoInfo g_info;

EM_JS(void, js_video_load, (const char *url, double start), {
    var v = document.getElementById('sntv-video');
    if (!v) {
        v = document.createElement('video');
        v.id = 'sntv-video';
        v.style.cssText = 'position:fixed;left:0;top:0;width:100%;height:100%;z-index:-1;background:#000';
        v.autoplay = true;
        v.crossOrigin = 'anonymous';
        document.body.insertBefore(v, document.body.firstChild);
        var c = document.getElementById('canvas');
        if (c) c.style.background = 'transparent';
    }
    v.src = UTF8ToString(url);
    v.currentTime = start;
    v.play().catch(function () {});
});
EM_JS(void, js_video_play, (void), { var v = document.getElementById('sntv-video'); if (v) v.play(); });
EM_JS(void, js_video_pause, (void), { var v = document.getElementById('sntv-video'); if (v) v.pause(); });
EM_JS(void, js_video_seek, (double s), { var v = document.getElementById('sntv-video'); if (v) v.currentTime = s; });
EM_JS(void, js_video_rate, (double r), { var v = document.getElementById('sntv-video'); if (v) v.playbackRate = r; });
EM_JS(void, js_video_volume, (int v), { var e = document.getElementById('sntv-video'); if (e) e.volume = v / 100; });
EM_JS(void, js_video_mute, (int m), { var v = document.getElementById('sntv-video'); if (v) v.muted = !!m; });
EM_JS(void, js_video_stop, (void), { var v = document.getElementById('sntv-video'); if (v) { v.pause(); v.removeAttribute('src'); v.load(); } });
EM_JS(double, js_video_time, (void), { var v = document.getElementById('sntv-video'); return v ? v.currentTime : 0; });
EM_JS(double, js_video_duration, (void), { var v = document.getElementById('sntv-video'); return v && isFinite(v.duration) ? v.duration : 0; });
EM_JS(int, js_video_state, (void), {
    var v = document.getElementById('sntv-video');
    if (!v || !v.src) return 0;
    if (v.error) return 5;
    if (v.ended) return 4;
    if (v.readyState < 3) return 1;
    return v.paused ? 3 : 2;
});
EM_JS(int, js_track_count, (int kind), {
    var v = document.getElementById('sntv-video');
    if (!v) return 0;
    var list = kind === 0 ? v.audioTracks : v.textTracks;
    return list ? list.length : 0;
});
EM_JS(void, js_track_info, (int kind, int i, char *lang, char *label, int *selected), {
    var v = document.getElementById('sntv-video');
    var list = kind === 0 ? v.audioTracks : v.textTracks;
    var t = list[i];
    stringToUTF8(t.language || '', lang, 16);
    stringToUTF8(t.label || t.language || ('Track ' + (i + 1)), label, 64);
    setValue(selected, kind === 0 ? (t.enabled ? 1 : 0) : (t.mode === 'showing' ? 1 : 0), 'i32');
});
EM_JS(void, js_track_select, (int kind, int index), {
    var v = document.getElementById('sntv-video');
    if (!v) return;
    if (kind === 0) { for (var i = 0; i < v.audioTracks.length; i++) v.audioTracks[i].enabled = i === index; }
    else { for (var j = 0; j < v.textTracks.length; j++) v.textTracks[j].mode = j === index ? 'showing' : 'disabled'; }
});

int video_init(SDL_Window *win) { (void)win; return 0; }
void video_shutdown(void) { js_video_stop(); }
const char *video_backend_name(void) { return "HTML5 video (WebAssembly)"; }
static unsigned g_load_count;
int video_load(const char *url, const char *title, double start) { (void)title; g_load_count++; memset(&g_info, 0, sizeof g_info); g_info.buffer_ahead = -1; g_info.state = VIDEO_LOADING; js_video_load(url, start); js_video_mute(0); js_video_volume(100); return 0; }
void video_set_muted(int m) { js_video_mute(m); }
void video_set_volume(int v) { js_video_volume(v); }
unsigned video_load_count(void) { return g_load_count; }
void video_play(void) { js_video_play(); }
void video_pause(void) { js_video_pause(); }
void video_seek(double s) { js_video_seek(s); }
void video_set_rate(double r) { js_video_rate(r); }
void video_stop(void) { js_video_stop(); g_info.state = VIDEO_IDLE; }
void video_set_display_rect(int x, int y, int w, int h) { (void)x; (void)y; (void)w; (void)h; }
void video_app_background(void) { js_video_pause(); }
void video_app_foreground(void) {}
void video_keep_awake(int on) { (void)on; }
void video_hold_screensaver(int on) { (void)on; }
void video_keep_awake_charging(int on) { (void)on; }
void video_subtitle_lift(float fraction) { (void)fraction; }
void video_memory_stage(int stage) { (void)stage; }
unsigned video_frame_texture(int *w, int *h, float *aspect) { (void)w; (void)h; (void)aspect; return 0; }   /* the video is a plane of its own */
int video_subtitle_text(char *out, size_t n) { if (n) *out = 0; return 0; }
int video_paused_on_purpose(void) { return 1; }   /* a pause here is always asked for */

/* No installer on this platform: updates come from its store. */
int video_install_update(const char *ipk_url, const char *sha256, const char *app_id) { (void)ipk_url; (void)sha256; (void)app_id; return -1; }
int video_install_status(char *out, size_t n) { if (n) out[0] = 0; return 0; }
void video_update(void)
{
    g_info.position = js_video_time();
    g_info.duration = js_video_duration();
    int s = js_video_state();
    if (s == 5) { g_info.state = VIDEO_ERROR; snprintf(g_info.error, sizeof g_info.error, "media element error"); }
    else if (s) g_info.state = (VideoState)s;
}
void video_info(VideoInfo *out) { *out = g_info; }
int video_tracks(TrackKind kind, VideoTrack *out, int max)
{
    int n = js_track_count((int)kind);
    if (n > max) n = max;
    for (int i = 0; i < n; i++) {
        memset(&out[i], 0, sizeof out[i]);
        out[i].index = i;
        js_track_info((int)kind, i, out[i].lang, out[i].label, &out[i].selected);
    }
    return n;
}
void video_subtitle_style(int size, int style, const char *colour, int position, int sync_ms) { (void)size; (void)style; (void)colour; (void)position; (void)sync_ms; }
void video_select_track(TrackKind kind, int index) { js_track_select((int)kind, index); g_info.tracks_version++; }

void video_landscape_only(int on) { (void)on; }
int video_take_open_url(char *out, size_t n) { (void)out; (void)n; return 0; }   /* only Android is opened with links */   /* TVs and the desktop do not turn */
int video_share_text(const char *name, const char *text) { (void)name; (void)text; return 0; }
int video_pick_text_start(void) { return 0; }
char *video_pick_text_take(void) { return NULL; }
int video_open_browser(const char *url) { (void)url; return 0; }   /* no browser to hand a page to: the phone opens it */
int video_has_youtube_app(void) { return 0; }
int video_open_youtube(const char *id) { (void)id; return 0; }
int video_youtube_embed_url(const char *id, char *out, size_t n) { (void)id; (void)out; (void)n; return 0; }
void video_pip_allow(int on) { (void)on; }   /* no picture in picture on a TV or the desktop */
int  video_pip_enter(void) { return 0; }
int  video_in_pip(void) { return 0; }
float video_device_volume(void) { return -1; }   /* a TV's own remote has those */
int   video_set_device_volume(float v) { (void)v; return 0; }
float video_brightness(void) { return -1; }
int   video_set_brightness(float v) { (void)v; return 0; }
