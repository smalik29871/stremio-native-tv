/* Desktop preview backend: no decoding, just a simulated clock so the player
 * UI, track menu, progress reporting and library sync can be exercised. */
#include "../http.h"
#include "video.h"
#include "../log.h"
#include <SDL.h>
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

static VideoInfo g;
static Uint32 g_last;
static int g_audio_sel = 0, g_text_sel = -1;
static int g_bad_warned = -1;   /* SNTV_FAKE_BAD_AUDIO: the track already reported unplayable */
static double g_rate = 1.0;

int video_init(SDL_Window *win) { (void)win; g.state = VIDEO_IDLE; return 0; }
void video_shutdown(void) {}
const char *video_backend_name(void) { return "null (desktop preview)"; }

static int g_loads;          /* the test hooks below act on the first source only */
static double g_played_here;

static int g_muted;

int video_load(const char *url, const char *title, double start)
{
    g_loads++; g_played_here = 0; g_muted = 0; g_bad_warned = -1;
    LOGI("video(null): load %s (%s) from %.0fs", url, title ? title : "", start);
    memset(&g, 0, sizeof g);
    g.state = VIDEO_LOADING;
    g.buffer_ahead = -1;
    g.position = start;
    g.duration = 2 * 3600.0;
    /* test hooks: SNTV_FAKE_DURATION / SNTV_FAKE_START (seconds) */
    const char *fd = getenv("SNTV_FAKE_DURATION"), *fs = getenv("SNTV_FAKE_START");
    if (fd) g.duration = atof(fd);
    if (fs && start <= 0) g.position = atof(fs);
    snprintf(g.codec, sizeof g.codec, "H264 1920x1080 / AAC 5.1");
    g.width = 1920; g.height = 1080; g.fps = 24; g.bitrate = 8200000;
    g_last = SDL_GetTicks();
    g_audio_sel = 0; g_text_sel = -1; g_rate = 1.0;
    g.tracks_version = 1;
    /* test hook: SNTV_FAKE_FETCH=1 reads the first 64 KB from the address as a TV's player
     * would (no headers of its own) and fails the load when that is not a video */
    if (getenv("SNTV_FAKE_FETCH") && !strncmp(url, "http", 4)) {
        const char *from = getenv("SNTV_FAKE_FETCH_FROM");   /* a seek: the bytes from there */
        HttpOptions o = { .range_bytes = 65536, .range_start = from ? strtoull(from, NULL, 10) : 0, .timeout_s = 8, .no_json_accept = 1 };
        HttpResponse r;
        http_request_ex(url, &o, &r);
        int ok = from ? (r.status == 206 && r.has_range && r.range_from == o.range_start && r.len == 65536) : http_looks_like_media(&r);   /* mid-file bytes are no header */
        LOGI("video(null): fetched %zu bytes, status %ld%s", r.len, r.status, ok ? "" : ": not a video");
        if (!ok) { g.state = VIDEO_ERROR; snprintf(g.error, sizeof g.error, "The source refused the request (HTTP %ld)", r.status); }
        http_response_free(&r);
    }
    return 0;
}

void video_play(void) { if (g.state == VIDEO_PAUSED || g.state == VIDEO_LOADING) g.state = VIDEO_PLAYING; }
void video_pause(void) { if (g.state == VIDEO_PLAYING) g.state = VIDEO_PAUSED; }
void video_seek(double s) { g.position = s < 0 ? 0 : s; if (g.duration && g.position > g.duration) g.position = g.duration; }
void video_stop(void) { g.state = VIDEO_IDLE; }
void video_set_rate(double r) { g_rate = r; }
void video_set_volume(int v) { LOGI("video(null): volume %d", v); }
void video_set_muted(int m) { if (m != g_muted) LOGI("video(null): %s", m ? "muted" : "sound on"); g_muted = m; }
unsigned video_load_count(void) { return (unsigned)g_loads; }
void video_set_display_rect(int x, int y, int w, int h) { (void)x; (void)y; (void)w; (void)h; }
void video_app_background(void) { video_pause(); }
void video_app_foreground(void) {}
void video_keep_awake(int on) { (void)on; }
int video_paused_on_purpose(void) { return 1; }   /* a pause here is always asked for */

/* No installer here. SNTV_FAKE_INSTALL (tests) walks through the steps a TV shows. */
static double g_install_at;
int video_install_update(const char *ipk_url, const char *sha256, const char *app_id)
{
    (void)ipk_url; (void)sha256; (void)app_id;
    if (!getenv("SNTV_FAKE_INSTALL")) return -1;
    g_install_at = SDL_GetTicks() / 1000.0;
    return 0;
}
int video_install_status(char *out, size_t n)
{
    if (!g_install_at) { snprintf(out, n, "%s", ""); return 0; }
    double t = SDL_GetTicks() / 1000.0 - g_install_at;
    if (t < 1.5) { snprintf(out, n, "Downloading\xE2\x80\xA6  %d%%", (int)(t / 1.5 * 100)); return 1; }
    if (t < 2.5) { snprintf(out, n, "Installing\xE2\x80\xA6"); return 1; }
    snprintf(out, n, "Finished."); return 2;
}

void video_update(void)
{
    Uint32 now = SDL_GetTicks();
    double dt = (now - g_last) / 1000.0;
    g_last = now;
    /* test hook: SNTV_FAKE_FAIL_LOADS=n fails the first n sources opened */
    const char *fl = getenv("SNTV_FAKE_FAIL_LOADS");
    if (g.state == VIDEO_LOADING && fl && g_loads <= atoi(fl)) { g.state = VIDEO_ERROR; g.position = 0; snprintf(g.error, sizeof g.error, getenv("SNTV_FAKE_FAIL_CODE") ? "Network Error (code %s)" : "The link expired (SNTV_FAKE_FAIL_LOADS)", getenv("SNTV_FAKE_FAIL_CODE")); return; }
    /* test hook: SNTV_FAKE_STUCK=1 has the first source load and then wait for data for ever,
     * buffering from before it is ready (a torrent nobody seeds) */
    const char *stuck = getenv("SNTV_FAKE_STUCK");
    if (stuck && g_loads == 1 && (g.state == VIDEO_LOADING || g.state == VIDEO_PLAYING)) {
        if (!g.buffering) { LOGI("video(null): waiting for data (SNTV_FAKE_STUCK)"); g.buffering = 1; return; }   /* still loading */
        g.state = VIDEO_PLAYING; return;
    }
    if (g.state == VIDEO_LOADING) g.state = VIDEO_PLAYING;
    /* test hook: SNTV_FAKE_STALLS=1 buffers for 0.2 s after every 0.4 s played */
    static double played;
    static const char *stalls = (const char *)-1;
    if (stalls == (const char *)-1) stalls = getenv("SNTV_FAKE_STALLS");
    if (stalls && g.state == VIDEO_PLAYING) {
        played += dt;
        int was = g.buffering;
        g.buffering = fmod(played, 0.6) > 0.4;
        if (g.buffering && !was) LOGI("video(null): buffering (SNTV_FAKE_STALLS)");
        if (g.buffering) return;
    }
    /* test hooks, first source only: SNTV_FAKE_DEAD_AFTER=s stops delivering after s seconds;
     * SNTV_FAKE_DRAIN=1 has the buffer ahead shrink from 20 s by half a second a second */
    if (g.state == VIDEO_PLAYING && g_loads == 1) {
        const char *dead = getenv("SNTV_FAKE_DEAD_AFTER"), *drain = getenv("SNTV_FAKE_DRAIN"), *net = getenv("SNTV_FAKE_NET_ERROR_AFTER");
        g_played_here += dt;
        /* SNTV_FAKE_NET_ERROR_AFTER=s: the connection drops after s seconds (as webOS reports it) */
        if (net && g_played_here > atof(net)) { g.state = VIDEO_ERROR; snprintf(g.error, sizeof g.error, "Network error (code 300)"); LOGI("video(null): the connection dropped (SNTV_FAKE_NET_ERROR_AFTER)"); return; }
        if (dead && g_played_here > atof(dead)) { if (!g.buffering) LOGI("video(null): the source stopped (SNTV_FAKE_DEAD_AFTER)"); g.buffering = 1; return; }
        if (drain && g.duration > 0) {
            double ahead = 20 - g_played_here * 0.5;
            g.buffer_ahead = ahead > 0 ? ahead : 0;
            if (ahead <= 0) { g.buffering = 1; return; }
        }
    } else if (g_loads > 1) g.buffer_ahead = 120;
    /* test hook: SNTV_FAKE_BAD_AUDIO="0" or "0,1": those audio tracks can't be decoded here
     * (the warning webOS gives, code 200); "*" for every track of the first source */
    const char *bad = getenv("SNTV_FAKE_BAD_AUDIO");
    if (bad && g.state == VIDEO_PLAYING && g_bad_warned != g_audio_sel) {
        char k[4]; snprintf(k, sizeof k, "%d", g_audio_sel);
        if ((bad[0] == '*' && g_loads == 1) || strstr(bad, k)) {
            g_bad_warned = g_audio_sel;
            snprintf(g.warning, sizeof g.warning, "This TV can't play this audio track. Try another one from the audio menu.");
            g.warning_version++;
            LOGI("video(null): audio track %d can't be played here (SNTV_FAKE_BAD_AUDIO)", g_audio_sel);
        }
    }
    if (g.state == VIDEO_PLAYING) {
        g.position += dt * g_rate;
        if (g.duration && g.position >= g.duration) { g.position = g.duration; g.state = VIDEO_ENDED; }
    }
}

void video_info(VideoInfo *out) { *out = g; }

int video_tracks(TrackKind kind, VideoTrack *out, int max)
{
    if (kind == TRACK_TEXT && getenv("SNTV_FAKE_NO_TEXT_TRACKS")) return 0;
    static const struct { const char *lang, *label, *codec; } A[] = { { "eng", "English", "AAC" }, { "spa", "Español", "AC3" } };
    static const struct { const char *lang, *label, *codec; } T[] = { { "eng", "English", "SRT" }, { "por", "Português", "ASS" } };
    int n = 0;
    for (int i = 0; i < 2 && n < max; i++, n++) {
        VideoTrack *t = &out[n];
        t->index = i;
        snprintf(t->lang, sizeof t->lang, "%s", kind == TRACK_AUDIO ? A[i].lang : T[i].lang);
        snprintf(t->label, sizeof t->label, "%s", kind == TRACK_AUDIO ? A[i].label : T[i].label);
        snprintf(t->codec, sizeof t->codec, "%s", kind == TRACK_AUDIO ? A[i].codec : T[i].codec);
        t->selected = kind == TRACK_AUDIO ? g_audio_sel == i : g_text_sel == i;
    }
    return n;
}

void video_subtitle_style(int size, int style, const char *colour, int position, int sync_ms) { (void)size; (void)style; (void)colour; (void)position; (void)sync_ms; }

void video_select_track(TrackKind kind, int index)
{
    if (kind == TRACK_AUDIO) g_audio_sel = index; else g_text_sel = index;
    g.tracks_version++;
}
