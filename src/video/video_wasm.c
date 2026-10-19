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
int video_load(const char *url, const char *title, double start) { (void)title; memset(&g_info, 0, sizeof g_info); g_info.buffer_ahead = -1; g_info.state = VIDEO_LOADING; js_video_load(url, start); return 0; }
void video_play(void) { js_video_play(); }
void video_pause(void) { js_video_pause(); }
void video_seek(double s) { js_video_seek(s); }
void video_set_rate(double r) { js_video_rate(r); }
void video_stop(void) { js_video_stop(); g_info.state = VIDEO_IDLE; }
void video_set_display_rect(int x, int y, int w, int h) { (void)x; (void)y; (void)w; (void)h; }
void video_app_background(void) { js_video_pause(); }
void video_app_foreground(void) {}

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
void video_subtitle_style(int size, int style, int position, int sync_ms) { (void)size; (void)style; (void)position; (void)sync_ms; }
void video_select_track(TrackKind kind, int index) { js_track_select((int)kind, index); g_info.tracks_version++; }
