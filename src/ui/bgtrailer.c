/* Trailers behind the artwork. The video plane sits under the UI; the screen fades its
 * backdrop's shape toward transparent (ui_backdrop_video) and draws the artwork over it
 * at what is left, so the trailer blends into the same edges the artwork had. */
#include "bgtrailer.h"
#include "../app.h"
#include "../platform/platform.h"
#include "../http.h"
#include "../log.h"
#include "../video/video.h"
#include "../../vendor/cJSON.h"
#include "gfx.h"
#include "font.h"
#include "theme.h"
#include "widgets.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- IMDb's trailer files ------------------------------------------------------
 * IMDb's public GraphQL API, as the IMDb Trailers add-ons for Kodi ask it: direct MP4s
 * up to 1080p and an HLS playlist, with no YouTube in the way. */

static int imdb_rank(const char *def, const char *mime)
{
    int mp4 = mime && !strcmp(mime, "MP4");
    if (!def) return 0;
    if (!strcmp(def, "DEF_1080p")) return mp4 ? 60 : 0;
    if (!strcmp(def, "DEF_AUTO") || (mime && !strcmp(mime, "M3U8"))) return 50;
    if (!strcmp(def, "DEF_720p")) return mp4 ? 40 : 0;
    if (!strcmp(def, "DEF_480p")) return mp4 ? 30 : 0;
    if (!strcmp(def, "DEF_SD")) return mp4 ? 20 : 0;
    return 0;
}

int imdb_trailer_res(int rank) { return rank == 60 ? 1080 : rank == 40 ? 720 : rank == 30 ? 480 : 0; }
const char *imdb_trailer_label(int rank) { return rank == 60 ? "1080p" : rank == 50 ? "adaptive" : rank == 40 ? "720p" : rank == 30 ? "480p" : "SD"; }

typedef struct { ImdbTrailerFn cb; void *user; } Fetch;

static void on_imdb(HttpResponse *r, void *user)
{
    Fetch f = *(Fetch *)user;
    free(user);
    cJSON *root = r->status == 200 && r->body ? cJSON_Parse(r->body) : NULL;
    const cJSON *video = cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(
                             cJSON_GetObjectItemCaseSensitive(root, "data"), "title"), "latestTrailer"), "playbackURLs");
    ImdbTrailer got[8]; int n = 0;
    const cJSON *u;
    cJSON_ArrayForEach(u, video) {
        const cJSON *def = cJSON_GetObjectItemCaseSensitive(u, "videoDefinition"), *mime = cJSON_GetObjectItemCaseSensitive(u, "videoMimeType"), *url = cJSON_GetObjectItemCaseSensitive(u, "url");
        int rk = imdb_rank(cJSON_IsString(def) ? def->valuestring : NULL, cJSON_IsString(mime) ? mime->valuestring : NULL);
        if (rk && cJSON_IsString(url) && n < 8) { got[n].url = url->valuestring; got[n].rank = rk; n++; }
    }
    for (int i = 1; i < n; i++) for (int j = i; j > 0 && got[j].rank > got[j - 1].rank; j--) { ImdbTrailer t = got[j]; got[j] = got[j - 1]; got[j - 1] = t; }
    f.cb(got, n, r->status, f.user);
    cJSON_Delete(root);
}

unsigned imdb_trailer_fetch(const char *id, ImdbTrailerFn cb, void *user)
{
    char imdb[32]; snprintf(imdb, sizeof imdb, "%.*s", (int)strcspn(id, ":"), id);
    char body[400];
    snprintf(body, sizeof body, "{\"query\":\"query($id: ID!){title(id:$id){latestTrailer{id playbackURLs{videoDefinition videoMimeType url}}}}\",\"variables\":{\"id\":\"%s\"}}", imdb);
    const char *api = getenv("SNTV_IMDB_API");
    HttpOptions o = { .post_json = body, .timeout_s = 12,
                      .headers = "User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/135.0.0.0 Safari/537.36\nReferer: https://www.imdb.com/\nOrigin: https://www.imdb.com" };
    Fetch *f = malloc(sizeof *f);
    f->cb = cb; f->user = user;
    return http_request_async_ex(api && *api ? api : "https://graphql.prod.api.imdb.a2z.com/", &o, on_imdb, f);
}

/* ---- the trailer behind the artwork ---------------------------------------------- */

enum { BT_IDLE, BT_WAIT, BT_FETCH, BT_LOAD, BT_PLAY, BT_DONE };

static struct {
    int state, wanted;
    float unwanted;       /* seconds nobody asked: a screen opening skips a frame or two */
    char id[128];
    float height, dwell, waited;
    float t;              /* how far the video has replaced the artwork */
    unsigned load;        /* video_load_count() of our video, 0 none */
    unsigned loads_at_fetch;   /* video_load_count() when IMDb was asked: another video since means hands off */
    uintptr_t fetch_gen;       /* the IMDb request whose answer counts; older ones are dropped when they land */
    int muted;
} B;

static int ours(void) { return B.load && video_load_count() == B.load; }

static void full_screen(void)
{
    int w, h;
    gfx_drawable_size(&w, &h);
    video_set_display_rect(0, 0, w, h);
}

static void finish(int next_state)
{
    if (ours()) { video_stop(); full_screen(); }
    else if (B.load) full_screen();   /* someone else's video now: it fills the screen */
    B.fetch_gen++;   /* an answer still on its way is dropped when it lands */
    B.load = 0; B.t = 0;
    B.state = next_state;
    app_redraw();
}

/* The plane, 16:9, covers the backdrop's height from the right edge */
static void place(void)
{
    float vh = B.height, vw = vh * 16 / 9;
    if (vw > SNTV_UI_W) { vw = SNTV_UI_W; vh = vw * 9 / 16; }
    int px, py, pw, ph;
    gfx_ui_to_pixels(SNTV_UI_W - vw, 0, vw, vh, &px, &py, &pw, &ph);
    video_set_display_rect(px, py, pw, ph);
}

static void on_trailer(const ImdbTrailer *t, int n, long status, void *user)
{
    /* the latest request, still on screen, and no other video started meanwhile (Play
     * pressed while IMDb was answering) */
    int same = B.state == BT_FETCH && (uintptr_t)user == B.fetch_gen;
    int current = same && B.unwanted < 0.1f && video_load_count() == B.loads_at_fetch;
    if (!current) { if (same) B.state = BT_DONE; return; }
    if (!n) { LOGI("trailer: none from IMDb for %s (%ld)", B.id, status); B.state = BT_DONE; return; }
    place();
    video_load(t[0].url, "Trailer", 0);
    B.muted = g_app.auto_trailers != 2;
    if (B.muted) video_set_muted(1);
    B.load = video_load_count();
    B.state = BT_LOAD;
    LOGI("trailer: %s plays behind the artwork (%s%s)", B.id, imdb_trailer_label(t[0].rank), g_app.auto_trailers != 2 ? ", muted" : "");
}

void bgtrailer_want(const char *id, float height, float dwell)
{
    if (!g_app.auto_trailers || !id || strncmp(id, "tt", 2) != 0 || g_app.pip) return;
    B.wanted = 1;
    if (strcmp(id, B.id) != 0) {
        if (B.state != BT_IDLE) finish(BT_IDLE);
        snprintf(B.id, sizeof B.id, "%s", id);
        B.state = BT_WAIT;
        B.waited = 0;
    }
    if (height != B.height && ours()) { B.height = height; place(); }   /* Home's hero, then the title page */
    B.height = height;
    B.dwell = dwell;
}

float bgtrailer_shown(const char *id) { return id && B.t > 0 && !strcmp(id, B.id) ? B.t : 0; }

void bgtrailer_frame(float dt)
{
    int wanted = B.wanted;
    B.wanted = 0;
    if ((B.state == BT_LOAD || B.state == BT_PLAY) && !ours()) { LOGI("trailer: %s gives way to another video", B.id); finish(BT_DONE); }
    if (!wanted) {
        B.unwanted += dt;
        if (B.state != BT_IDLE && B.unwanted > 0.2f) { if (B.state >= BT_LOAD && B.state <= BT_PLAY) LOGI("trailer: %s stops (left the title)", B.id); finish(BT_IDLE); B.id[0] = 0; }
        return;
    }
    B.unwanted = 0;
    switch (B.state) {
    case BT_WAIT:
        B.waited += dt;
        if (B.waited >= B.dwell) {
            B.state = BT_FETCH;
            B.loads_at_fetch = video_load_count();
            imdb_trailer_fetch(B.id, on_trailer, (void *)++B.fetch_gen);
        }
        break;
    case BT_LOAD: case BT_PLAY: {
        VideoInfo vi;
        video_info(&vi);
        if (vi.state == VIDEO_ERROR) { LOGW("trailer: %s did not play: %s", B.id, vi.error); finish(BT_DONE); break; }
        if (B.state == BT_LOAD && vi.state == VIDEO_PLAYING && !vi.buffering && vi.position > 0.3) B.state = BT_PLAY;
        int ending = vi.state == VIDEO_ENDED || (vi.duration > 0 && vi.position > vi.duration - 0.8);
        float to = B.state == BT_PLAY && !ending ? 1 : 0, was = B.t;
        B.t += (to - B.t) * (dt * 3 > 1 ? 1 : dt * 3);
        if (to > 0 && B.t > 0.995f) B.t = 1;
        if (B.t != was) app_redraw();
        if (ending && B.t < 0.01f) { LOGI("trailer: %s ended; the artwork is back", B.id); finish(BT_DONE); }
        break;
    }
    default: break;
    }
}

int bgtrailer_key_sound(void)
{
    if (B.state != BT_PLAY || B.t < 0.5f || !ours()) return 0;
    B.muted = !B.muted;
    video_set_muted(B.muted);
    LOGI("trailer: %s sound %s", B.id, B.muted ? "off" : "on");
    app_redraw();
    return 1;
}

void bgtrailer_draw_badge(const char *id)
{
    float t = bgtrailer_shown(id);
    if (t <= 0) return;
    const char *label = B.muted ? "Sound off  \xC2\xB7  Play/Pause turns it on" : "Sound on";
    int fs = FONT_XS | FONT_B;
    float h = 48, w = 24 + 28 + 12 + font_measure(label, fs) + 24, x = SNTV_UI_W - THEME_MARGIN - w, y = 96;
    Color fg = COLOR_ALPHA(THEME_TEXT, (int)(255 * t));
    gfx_rrect(x, y, w, h, theme_pill_r(h), COLOR_ALPHA(RGBA(0, 0, 0, 0xFF), (int)(0x99 * t)));
    float ix = x + 24 + 14, iy = y + h / 2;
    ui_icon(ICON_AUDIO, ix, iy, 26, fg);
    if (B.muted) gfx_line(ix - 14, iy - 14, ix + 14, iy + 14, 3, fg);
    font_draw(label, x + 24 + 28 + 12, y + (h - font_height(fs)) / 2, fs, fg);
}
