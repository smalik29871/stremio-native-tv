/* Detail: cinematic header over the backdrop (logo or title, IMDb, meta, genres,
 * synopsis), a pill action row (Play / Resume with progress, Library), and for
 * series: season tabs and a horizontal episode rail with thumbnails, watched
 * badges, resume progress and spoiler blur for what you haven't seen yet.
 * Below the fold (Down) the page scrolls to About: the facts, a ratings card, the
 * cast with their photos and more titles of the same genre. */
#include "../app.h"
#include "../tmdb.h"
#include "../newep.h"
#include "../tracking.h"
#include "../ai.h"
#include "../image.h"
#include "../../vendor/cJSON.h"
#include "../http.h"
#include "../log.h"
#include "../store.h"
#include "../youtube.h"
#include "../phone.h"
#include "../video/video.h"
#include "font.h"
#include "theme.h"
#include "widgets.h"
#include "screen_options.h"
#include "island.h"
#include "bgtrailer.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { AREA_ACTIONS, AREA_SEASONS, AREA_EPISODES, AREA_ABOUT, AREA_CAST, AREA_FRANCHISE, AREA_SIMILAR };
enum { BTN_PLAY, BTN_RESTART, BTN_TRAILER, BTN_LIBRARY, BTN_COUNT };
#define EP_W (g_theme.id == THEME_APERTURE ? 456.0f : 384.0f)
#define EP_H (g_theme.id == THEME_APERTURE ? 256.0f : 216.0f)
#define EP_GAP 28.0f
#define RAIL_Y 700.0f
/* About, below the fold */
#define ABOUT_Y 1140.0f
#define CAST_D 132.0f      /* a cast photo's diameter */
#define CAST_STEP 204.0f
#define SIM_W 200.0f
#define SIM_H 300.0f
#define SIM_GAP 26.0f
#define MAX_SIMILAR 20

typedef struct {
    MetaPreview preview;
    MetaItem meta; int has_meta;
    int loading; unsigned req; char error[400];
    int *seasons; int n_seasons; int season_idx;
    int *ep_index; int n_eps; int ep_focus;
    int area, btn;
    float rail_x, focus_t;
    float intro;                 /* page entrance animation */
    char resume_label[96];
    char resume_url[1024], resume_video[128];
    double resume_pos, resume_frac;
    /* "Ask AI": a spoiler-free card about the title */
    int ask_open; unsigned ask_req; float ask_t;
    char *ask_nutshell, *ask_good_for, *ask_notes, *ask_similar; char ask_err[160];
    /* About */
    float scroll, scroll_v, age, facts_h;
    char pre_id[160]; float pre_since;
    int meta_wait, addons_seen;   /* details asked again once the addons have come in */   /* the video whose sources are asked ahead, and since when it has been the one */
    int cast_focus, sim_focus, fr_focus, fr_opening; unsigned fr_gen;
    float cast_x, sim_x, fr_x;
    MetaPreview *similar; int n_similar; unsigned sim_req; int sim_asked; char sim_genre[64];
} Detail;

/* A trailer from YouTube, or from IMDb for any title it knows (its id starts "tt") */
static int has_trailer(const Detail *d) { return d->meta.trailer_yt || !strncmp(d->preview.id, "tt", 2); }

/* the visible buttons in order: Play/Resume, [From the start], [Trailer], Library */
static int btn_at(const Detail *d, int pos)
{
    if (pos == 0) return BTN_PLAY;
    if (d->resume_label[0]) { if (pos == 1) return BTN_RESTART; pos--; }
    if (has_trailer(d)) { if (pos == 1) return BTN_TRAILER; pos--; }
    return BTN_LIBRARY;
}
static int n_buttons_d(const Detail *d) { return 2 + (d->resume_label[0] ? 1 : 0) + (has_trailer(d) ? 1 : 0); }

/* ---- the trailer: fetched from YouTube once, then played from the TV ---- */

static struct { char id[128]; char yt[32]; int busy; } g_trailer;   /* one fetch at a time, for the title `id` */

static void trailer_url(const char *yt, char *out, size_t n) { snprintf(out, n, "https://www.youtube.com/watch?v=%s", yt); }

static void trailer_play_saved(const char *name, const MetaPreview *meta)
{
    char snd[128], buf[800], vid[160];
    snprintf(snd, sizeof snd, "sntv-sound://%s", name);
    const char *u = phone_resolve_sound(snd, buf, sizeof buf);
    if (!u) { app_toast("The trailer could not be opened"); return; }
    PlayCandidate *c = calloc(1, sizeof *c);
    c->url = strdup(u);
    c->label = strdup("Trailer");
    snprintf(vid, sizeof vid, "trailer:%s", meta->id);
    char title[200]; snprintf(title, sizeof title, "%s  -  Trailer", meta->name ? meta->name : "");
    LOGI("detail: trailer for %s from %s", meta->id, name);
    app_push(screen_player_create(meta, vid, title, c, 1, 0, 0, NULL, NULL));
}

static void on_trailer(const char *name, const char *error, void *user)
{
    (void)user;
    g_trailer.busy = 0;
    if (!name) {
        LOGW("detail: trailer for %s failed: %s", g_trailer.id, error ? error : "?");
        app_toast("The trailer could not be fetched%s%s", error ? ": " : "", error ? error : "");
        return;
    }
    Screen *top = app_top();
    if (top && strcmp(top->name, "detail") == 0 && strcmp(((Detail *)top->st)->preview.id, g_trailer.id) == 0) trailer_play_saved(name, &((Detail *)top->st)->preview);
    else app_toast("The trailer is ready: Trailer on the title page plays it");
    app_redraw();
}

/* Streamed first, as Nuvio plays trailers; a refused stream falls back to the download. */
static void on_trailer_stream(const char *url, const char *error, void *user)
{
    (void)user;
    if (!url) {
        LOGW("detail: trailer stream for %s refused (%s); downloading it", g_trailer.id, error ? error : "?");
        char yurl[96]; trailer_url(g_trailer.yt, yurl, sizeof yurl);
        youtube_save(yurl, 1, on_trailer, NULL);
        return;
    }
    g_trailer.busy = 0;
    Screen *top = app_top();
    if (!top || strcmp(top->name, "detail") != 0 || strcmp(((Detail *)top->st)->preview.id, g_trailer.id) != 0) return;
    const MetaPreview *meta = &((Detail *)top->st)->preview;
    static char urls[YT_PLAY_URLS][4608];
    int nu = youtube_player_urls(url, g_trailer.yt, urls, YT_PLAY_URLS);
    if (!nu) { app_toast("The trailer could not be opened"); return; }
    PlayCandidate *c = calloc((size_t)nu, sizeof *c);
    for (int i = 0; i < nu; i++) {   /* sharpest first: the player falls back in this order */
        c[i].url = strdup(urls[i]);
        char lbl[48]; snprintf(lbl, sizeof lbl, "Trailer - %s", i == 0 ? "best" : i == nu - 1 ? "last" : "next");
        c[i].label = strdup(lbl);
    }
    char vid[160], title[200];
    snprintf(vid, sizeof vid, "trailer:%s", meta->id);
    snprintf(title, sizeof title, "%s  -  Trailer", meta->name ? meta->name : "");
    LOGI("detail: trailer for %s streaming", meta->id);
    app_push(screen_player_create(meta, vid, title, c, nu, 0, 0, NULL, NULL));
    app_redraw();
}

static void trailer_youtube(Detail *d)
{
    char url[96];
    trailer_url(d->meta.trailer_yt, url, sizeof url);
    snprintf(g_trailer.yt, sizeof g_trailer.yt, "%s", d->meta.trailer_yt);
    g_trailer.busy = 1;
    LOGI("detail: fetching the trailer for %s (%s)", d->preview.id, d->meta.trailer_yt);
    youtube_set_quality(g_app.res_cap, quality_cap_ok(&g_app.tv, TVCAP_AV1), quality_cap_ok(&g_app.tv, TVCAP_4K));
    youtube_stream(url, on_trailer_stream, NULL);
}

/* IMDb's own trailer first: sharp, plain files with no YouTube in the way */
static void on_imdb_trailer(const ImdbTrailer *t, int n, long status, void *user)
{
    (void)user;
    Screen *top = app_top();
    Detail *d = top && !strcmp(top->name, "detail") && !strcmp(((Detail *)top->st)->preview.id, g_trailer.id) ? top->st : NULL;
    if (!n) {
        LOGI("detail: no IMDb trailer for %s (%ld)%s", g_trailer.id, status, d && d->meta.trailer_yt ? "; YouTube's then" : "");
        if (d && d->meta.trailer_yt) trailer_youtube(d);
        else { g_trailer.busy = 0; if (d) app_toast("No trailer found for this title"); }
        return;
    }
    g_trailer.busy = 0;
    if (!d) return;
    PlayCandidate *c = calloc((size_t)n, sizeof *c);
    for (int i = 0; i < n; i++) {   /* sharpest first; the player falls back down the list */
        c[i].url = strdup(t[i].url);
        char lbl[48]; snprintf(lbl, sizeof lbl, "IMDb - %s", imdb_trailer_label(t[i].rank));
        c[i].label = strdup(lbl);
        c[i].res = imdb_trailer_res(t[i].rank);
    }
    char vid[160], title[200];
    snprintf(vid, sizeof vid, "trailer:%s", d->preview.id);
    snprintf(title, sizeof title, "%s  -  Trailer", d->preview.name ? d->preview.name : "");
    LOGI("detail: trailer for %s from IMDb (%d qualit%s, best %s)", d->preview.id, n, n == 1 ? "y" : "ies", c[0].label + 7);
    app_push(screen_player_create(&d->preview, vid, title, c, n, 0, 0, NULL, NULL));
    app_redraw();
}

static void trailer_play(Detail *d)
{
    if (!has_trailer(d)) return;
    char url[96], name[80]; int is_video = 0;
    if (d->meta.trailer_yt) {
        trailer_url(d->meta.trailer_yt, url, sizeof url);
        if (youtube_saved(url, 1, name, sizeof name, &is_video) && is_video) { trailer_play_saved(name, &d->preview); return; }
    }
    if (g_trailer.busy) { app_toast(strcmp(g_trailer.id, d->preview.id) == 0 ? "Getting the trailer\xE2\x80\xA6" : "Another trailer is on its way; one at a time"); return; }
    snprintf(g_trailer.id, sizeof g_trailer.id, "%s", d->preview.id);
    if (strncmp(d->preview.id, "tt", 2) != 0) { trailer_youtube(d); return; }
    /* IMDb first: sharp and plain files; YouTube when IMDb has none */
    g_trailer.busy = 1;
    LOGI("detail: asking IMDb for the trailer of %.*s", (int)strcspn(d->preview.id, ":"), d->preview.id);
    imdb_trailer_fetch(d->preview.id, on_imdb_trailer, NULL);
}

static void ask_clear(Detail *d)
{
    free(d->ask_nutshell); free(d->ask_good_for); free(d->ask_notes); free(d->ask_similar);
    d->ask_nutshell = d->ask_good_for = d->ask_notes = d->ask_similar = NULL;
}

static int ask_parse(Detail *d, const char *json)
{
    cJSON *root = cJSON_Parse(json);
    if (!root) return 0;
    const cJSON *v;
    #define TAKE(field, key) do { v = cJSON_GetObjectItemCaseSensitive(root, key); if (cJSON_IsString(v) && v->valuestring[0]) d->field = strdup(v->valuestring); } while (0)
    TAKE(ask_nutshell, "nutshell"); TAKE(ask_good_for, "good_for"); TAKE(ask_notes, "notes");
    #undef TAKE
    v = cJSON_GetObjectItemCaseSensitive(root, "similar");
    if (cJSON_IsArray(v)) {
        char buf[400] = ""; const cJSON *it;
        cJSON_ArrayForEach(it, v) if (cJSON_IsString(it)) { size_t l = strlen(buf); snprintf(buf + l, sizeof buf - l, "%s%s", l ? "  \xC2\xB7  " : "", it->valuestring); }
        if (buf[0]) d->ask_similar = strdup(buf);
    } else if (cJSON_IsString(v)) d->ask_similar = strdup(v->valuestring);
    cJSON_Delete(root);
    return d->ask_nutshell != NULL;
}

static void on_ask(const char *text, const char *error, void *user)
{
    Detail *d = user;
    d->ask_req = 0;
    if (text && ask_parse(d, text)) {
        char key[200]; snprintf(key, sizeof key, "ai://about/%s", d->preview.id);
        app_cache_put(key, text, strlen(text));
    } else snprintf(d->ask_err, sizeof d->ask_err, "%s", error ? error : "The answer could not be read. Try again.");
    app_redraw();
}

static void ask_open(Detail *d)
{
    d->ask_open = 1; d->ask_t = 0; d->ask_err[0] = 0;
    if (d->ask_nutshell || d->ask_req) return;
    char key[200]; snprintf(key, sizeof key, "ai://about/%s", d->preview.id);
    char *cached = app_cache_get(key, 30L * 24 * 3600);
    if (cached) { int ok = ask_parse(d, cached); free(cached); if (ok) return; }
    const TmdbInfo *tm = tmdb_get(d->preview.id, d->preview.type);
    char genres[300] = ""; for (int i = 0; i < d->preview.genres.n && i < 6; i++) { size_t l = strlen(genres); snprintf(genres + l, sizeof genres - l, "%s%s", l ? ", " : "", d->preview.genres.v[i]); }
    char msg[2200];
    snprintf(msg, sizeof msg, "Title: %s%s%s\nType: %s\nGenres: %s\nIMDb: %s\nAge rating: %s\nSynopsis: %.1200s",
             d->preview.name, d->preview.release_info ? " " : "", d->preview.release_info ? d->preview.release_info : "",
             d->preview.type ? d->preview.type : "movie", genres[0] ? genres : "unknown", d->preview.imdb_rating ? d->preview.imdb_rating : "n/a",
             tm && tm->cert[0] ? tm->cert : "unknown", d->preview.description ? d->preview.description : "(none)");
    static const char *SYS = "You are a concise, warm film and TV guide shown on a television. Never reveal plot twists or endings. "
        "Reply with JSON only, no markdown: {\"nutshell\": \"what it is and why people love it, at most two short sentences\", "
        "\"good_for\": \"the mood and who will enjoy it, one sentence\", "
        "\"notes\": \"what a parent or a sensitive viewer should know (violence, language, sex, scares, heavy themes), one spoiler-free sentence\", "
        "\"similar\": [\"three well-known titles people who like this also like\"]}";
    d->ask_req = ai_ask(SYS, msg, 420, 1, on_ask, d);
    if (!d->ask_req) snprintf(d->ask_err, sizeof d->ask_err, "The AI assistant is off. Settings > API keys.");
}

static void draw_ask(Detail *d)
{
    float k = g_app.fx_light ? 1 : d->ask_t;
    gfx_rect(0, 0, SNTV_UI_W, SNTV_UI_H, RGBA(0, 0, 0, (int)(150 * k)));
    float w = 640, x = SNTV_UI_W - w * k, y = 0, pad = 44;
    gfx_rrect(x, y, w, SNTV_UI_H, 0, COLOR_ALPHA(THEME_PANEL, 0xF4));
    gfx_rect(x, y, 4, SNTV_UI_H, THEME_ACCENT);
    ui_icon(ICON_STAR, x + pad + 18, 128, 34, THEME_ACCENT2);   /* below the clock pill */
    font_draw("Ask AI", x + pad + 50, 110, FONT_L | FONT_B, THEME_TEXT);
    font_draw_fit(d->preview.name, x + pad, 164, FONT_S | FONT_B, THEME_TEXT_DIM, w - 2 * pad);
    float ty = 226, cw = w - 2 * pad;
    if (d->ask_req) { ui_spinner(x + pad + 20, ty + 20, 18); font_draw("Thinking\xE2\x80\xA6", x + pad + 56, ty + 4, FONT_S, THEME_TEXT_DIM); ty += 60; }
    if (d->ask_err[0]) { ty += font_draw_wrapped(d->ask_err, x + pad, ty, FONT_S, THEME_DANGER, cw, 3) * font_height(FONT_S) + 20; }
    struct { const char *h; const char *t; } S[] = { { "IN A NUTSHELL", d->ask_nutshell }, { "YOU'LL LIKE IT IF", d->ask_good_for }, { "BEFORE YOU WATCH  \xC2\xB7  SPOILER-FREE", d->ask_notes }, { "IF YOU LIKE THIS", d->ask_similar } };
    for (size_t i = 0; i < sizeof S / sizeof S[0]; i++) {
        if (!S[i].t) continue;
        font_draw(S[i].h, x + pad, ty, FONT_XS | FONT_B, THEME_TEXT_MUTED); ty += 34;
        ty += font_draw_wrapped(S[i].t, x + pad, ty, FONT_S, THEME_TEXT, cw, 6) * font_height(FONT_S) + 30;
    }
    if (d->ask_nutshell) font_draw("Written by an AI model from the synopsis: a guide, not gospel.", x + pad, SNTV_UI_H - 96, FONT_XS, THEME_TEXT_MUTED);
    font_draw("Back: close", x + pad, SNTV_UI_H - 60, FONT_XS, THEME_TEXT_MUTED);
}

static void select_season(Detail *d, int idx)
{
    d->season_idx = idx;
    free(d->ep_index);
    d->ep_index = calloc((size_t)d->meta.n_videos + 1, sizeof(int));
    d->n_eps = 0;
    int season = d->n_seasons ? d->seasons[idx] : -1;
    for (int i = 0; i < d->meta.n_videos; i++)
        if (d->n_seasons == 0 || d->meta.videos[i].season == season) d->ep_index[d->n_eps++] = i;
    d->ep_focus = 0;
    d->focus_t = 0;
}

static void compute_seasons(Detail *d)
{
    free(d->seasons);
    d->seasons = calloc((size_t)d->meta.n_videos + 1, sizeof(int));
    d->n_seasons = 0;
    for (int i = 0; i < d->meta.n_videos; i++) {
        int s = d->meta.videos[i].season;
        if (s < 0) continue;
        int found = 0;
        for (int j = 0; j < d->n_seasons; j++) if (d->seasons[j] == s) found = 1;
        if (!found) d->seasons[d->n_seasons++] = s;
    }
    for (int i = 1; i < d->n_seasons; i++)
        for (int j = i; j > 0; j--) {
            int a = d->seasons[j - 1], b = d->seasons[j];
            int ka = a == 0 ? 1 << 20 : a, kb = b == 0 ? 1 << 20 : b;
            if (ka > kb) { d->seasons[j - 1] = b; d->seasons[j] = a; } else break;
        }
    int start = 0;
    LibraryItem *li = app_library_find(d->preview.id);
    const NewEp *ne = newep_get(d->preview.id);
    const char *at = ne ? ne->video_id : li ? li->video_id : NULL;   /* a new episode is where you are now */
    if (at)
        for (int i = 0; i < d->meta.n_videos; i++)
            if (strcmp(d->meta.videos[i].id, at) == 0)
                for (int j = 0; j < d->n_seasons; j++) if (d->seasons[j] == d->meta.videos[i].season) start = j;
    select_season(d, start);
    if (at)
        for (int k = 0; k < d->n_eps; k++) if (strcmp(d->meta.videos[d->ep_index[k]].id, at) == 0) d->ep_focus = k;
}

static void load_resume(Detail *d)
{
    d->resume_label[0] = 0;
    LibraryItem *li = app_library_find(d->preview.id);
    const NewEp *ne = li && li->time_offset == 0 ? newep_get(d->preview.id) : NULL;
    if (ne) {   /* caught up, and another episode is out */
        int se = 0, ep = 0;
        const char *c1 = strchr(ne->video_id, ':');
        d->resume_url[0] = 0;
        snprintf(d->resume_video, sizeof d->resume_video, "%s", ne->video_id);
        d->resume_pos = 0; d->resume_frac = 0;
        if (c1 && sscanf(c1, ":%d:%d", &se, &ep) == 2) snprintf(d->resume_label, sizeof d->resume_label, "Play S%d E%d  \xC2\xB7  New", se, ep);
        else snprintf(d->resume_label, sizeof d->resume_label, "Play the new episode");
        return;
    }
    if (!li || li->time_offset == 0) return;
    char key[160];
    snprintf(key, sizeof key, "resume:%s", d->preview.id);
    struct cJSON *o = store_get_json(key);
    const struct cJSON *u = o ? cJSON_GetObjectItemCaseSensitive(o, "url") : NULL;
    const struct cJSON *v = o ? cJSON_GetObjectItemCaseSensitive(o, "video_id") : NULL;
    /* the stream played last here, when it was this episode; else Resume opens its sources */
    int same = cJSON_IsString(u) && !(li->video_id && cJSON_IsString(v) && strcmp(v->valuestring, li->video_id) != 0);
    snprintf(d->resume_url, sizeof d->resume_url, "%s", same ? u->valuestring : "");
    snprintf(d->resume_video, sizeof d->resume_video, "%s", li->video_id ? li->video_id : d->preview.id);
    d->resume_pos = li->time_offset / 1000.0;
    d->resume_frac = li->duration ? (double)li->time_offset / (double)li->duration : 0;
    char t[32];
    ui_format_time(d->resume_pos, t, sizeof t);
    int se = 0, ep = 0;
    const char *c1 = li->video_id ? strchr(li->video_id, ':') : NULL;
    int fresh = li->time_offset <= 1000;   /* the next episode, not started yet (kept in Continue Watching at 1 ms) */
    if (c1 && sscanf(c1, ":%d:%d", &se, &ep) == 2) snprintf(d->resume_label, sizeof d->resume_label, "%s S%d E%d", fresh ? "Play" : "Resume", se, ep);
    else snprintf(d->resume_label, sizeof d->resume_label, "Resume from %s", t);
}

static void on_meta(HttpResponse *r, void *user)
{
    Detail *d = user;
    int was = btn_at(d, d->btn);   /* a trailer or a resume point can add buttons: keep the focused one */
    d->req = 0;
    d->loading = 0;
    if (r->status == 200 && r->len && r->seconds >= 0) app_meta_put(d->preview.type, d->preview.id, r->body, r->len);
    if (r->status != 200 || addon_parse_meta(r->body, &d->meta) != 0) {
        snprintf(d->error, sizeof d->error, "Couldn't load details (%s)", r->error[0] ? r->error : "bad response");
        app_redraw();
        return;
    }
    d->has_meta = 1;
    app_remember_videos(d->preview.id, d->meta.videos, d->meta.n_videos);
    MetaPreview merged;
    meta_preview_copy(&merged, &d->meta.p);
    if (!merged.poster && d->preview.poster) merged.poster = strdup(d->preview.poster);
    if (!merged.background && d->preview.background) merged.background = strdup(d->preview.background);
    meta_preview_free(&d->preview);
    d->preview = merged;
    compute_seasons(d);
    load_resume(d);
    for (int i = 0; i < n_buttons_d(d); i++) if (btn_at(d, i) == was) { d->btn = i; break; }
    app_redraw();
}

static void fetch_meta(Detail *d)
{
    char *cached = app_meta_get(d->preview.type, d->preview.id);
    if (cached) {   /* Home fetched it already: no wait */
        HttpResponse r = { 0 };
        r.status = 200; r.body = cached; r.len = strlen(cached); r.seconds = -1;   /* from the cache */
        on_meta(&r, d);
        free(cached);
        if (d->has_meta) return;
    }
    for (int i = 0; i < g_app.n_addons; i++) {
        if (!addon_supports(&g_app.addons[i], "meta", d->preview.type, d->preview.id)) continue;
        char url[1024];
        addon_url_meta(&g_app.addons[i], d->preview.type, d->preview.id, url, sizeof url);
        d->loading = 1;
        d->req = http_request_async(url, NULL, on_meta, d);
        return;
    }
    if (g_app.addons_loading || !g_app.n_addons) {   /* the addons are still on their way (or failed): ask again when they come */
        d->meta_wait = 1; d->addons_seen = g_app.addons_version; d->loading = 1;
        LOGI("detail: no addon for %s yet (%d loaded%s); waiting", d->preview.id, g_app.n_addons, g_app.addons_loading ? ", loading" : "");
    }
    if (!strncmp(d->preview.id, "tt", 2) && d->preview.type) {   /* Cinemeta knows every IMDb title: details still show */
        char url[512];
        snprintf(url, sizeof url, "https://v3-cinemeta.strem.io/meta/%s/%s.json", d->preview.type, d->preview.id);
        d->loading = 1;
        d->req = http_request_async(url, NULL, on_meta, d);
        LOGI("detail: no installed addon for %s's details; asking Cinemeta", d->preview.id);
        return;
    }
    if (!d->meta_wait) snprintf(d->error, sizeof d->error, "No installed addon provides details for this title");
}

static int ep_watched(const Detail *d, int k, const LibraryItem *li, int cur_idx)
{
    const MetaVideo *v = &d->meta.videos[d->ep_index[k]];
    int mark = app_episode_mark(d->preview.id, v->id);   /* set by hand: wins */
    if (mark >= 0) return mark;
    if (!li) return 0;
    if (li->video_id && strcmp(li->video_id, v->id) == 0) return li->time_offset == 0 && li->times_watched > 0;
    return cur_idx >= 0 && k < cur_idx;
}

/* ---- About ---- */

/* the cast: TMDB's when a key is set, else what the addon gave */
static int cast_count(const Detail *d, const TmdbInfo *tm)
{
    int n = tm && tm->n_cast ? tm->n_cast : d->preview.cast.n;
    return n > 15 ? 15 : n;
}

static void cast_at(const Detail *d, const TmdbInfo *tm, int i, const char **name, const char **role, const char **photo)
{
    if (tm && tm->n_cast) { *name = tm->cast[i].name; *role = tm->cast[i].character; *photo = tm->cast[i].photo[0] ? tm->cast[i].photo : NULL; return; }
    *name = d->preview.cast.v[i];
    *role = i < d->preview.cast_roles.n ? d->preview.cast_roles.v[i] : "";
    *photo = i < d->preview.cast_photos.n && d->preview.cast_photos.v[i][0] ? d->preview.cast_photos.v[i] : NULL;
}

/* where the About rows sit on the page; -1 for a row with nothing in it */
typedef struct { float facts, cast, franchise, similar; } AboutLayout;

/* The film's franchise in release order, when TMDB knows one. */
static const TmdbCollection *franchise(const Detail *d)
{
    const TmdbInfo *tm = tmdb_get(d->preview.id, d->preview.type);
    return tm && tm->collection_id ? tmdb_collection(tm->collection_id) : NULL;
}
static AboutLayout about_layout(const Detail *d, int n_cast)
{
    AboutLayout L;
    float y = ABOUT_Y + 64;
    L.facts = y; y += d->facts_h + 60;
    L.cast = n_cast ? y : -1; if (n_cast) y += 60 + CAST_D + 120;
    const TmdbCollection *fc = franchise(d);
    L.franchise = fc ? y : -1; if (fc) y += 60 + SIM_H + 110;
    L.similar = d->n_similar ? y : -1;
    return L;
}

static float scroll_target(const Detail *d, int n_cast)
{
    if (d->area < AREA_ABOUT) return 0;
    AboutLayout L = about_layout(d, n_cast);
    float top = ABOUT_Y - 150, bottom = L.facts + d->facts_h;
    if (d->area == AREA_CAST && L.cast >= 0) bottom = L.cast + 60 + CAST_D + 100;
    if (d->area == AREA_FRANCHISE && L.franchise >= 0) bottom = L.franchise + 60 + SIM_H + 80;
    if (d->area == AREA_SIMILAR && L.similar >= 0) bottom = L.similar + 60 + SIM_H + 40;
    float t = bottom + 60 - SNTV_UI_H;
    return t > top ? t : top;
}

/* a row's offset that keeps the focused item in view, a step of room each side */
static float row_offset(int focus, int n, float step, float view)
{
    float t = focus * step - (view - 2 * step);
    float max = n * step - view;
    if (t > max) t = max;
    return t > 0 ? t : 0;
}

static int shared_genres(const MetaPreview *a, const MetaPreview *b)
{
    int n = 0;
    for (int i = 0; i < a->genres.n; i++) if (strlist_contains(&b->genres, a->genres.v[i])) n++;
    return n;
}

static void on_similar(HttpResponse *r, void *user)
{
    Detail *d = user;
    d->sim_req = 0;
    MetaPreview *m = NULL; int n = 0;
    if (r->status != 200 || addon_parse_metas(r->body, &m, &n) != 0) return;
    /* the title itself out; the most genres in common first, else the catalog's order */
    int *idx = calloc((size_t)n + 1, sizeof(int)), *sc = calloc((size_t)n + 1, sizeof(int)), k = 0;
    for (int i = 0; i < n; i++) {
        if (!m[i].id || strcmp(m[i].id, d->preview.id) == 0) continue;
        int g = shared_genres(&d->preview, &m[i]), j = k++;
        for (; j > 0 && sc[j - 1] < g; j--) { idx[j] = idx[j - 1]; sc[j] = sc[j - 1]; }
        idx[j] = i; sc[j] = g;
    }
    d->similar = calloc(MAX_SIMILAR, sizeof *d->similar);
    for (int j = 0; j < k && d->n_similar < MAX_SIMILAR; j++) {
        d->similar[d->n_similar++] = m[idx[j]];
        memset(&m[idx[j]], 0, sizeof m[idx[j]]);   /* moved */
    }
    meta_previews_free(m, n);
    free(idx); free(sc);
    LOGI("detail: %d more like %s (%s)", d->n_similar, d->preview.id, d->sim_genre);
    app_redraw();
}

/* More like this: a catalog that filters by one of the title's genres */
static void fetch_similar(Detail *d)
{
    d->sim_asked = 1;
    if (!d->preview.type) return;
    for (int g = 0; g < d->preview.genres.n && g < 3; g++)
        for (int i = 0; i < g_app.n_addons; i++) {
            const Addon *a = &g_app.addons[i];
            for (int c = 0; c < a->manifest.n_catalogs; c++) {
                const AddonCatalog *cat = &a->manifest.catalogs[c];
                if (!cat->type || !cat->id || strcmp(cat->type, d->preview.type) != 0) continue;
                const AddonExtra *ge = addon_catalog_extra(cat, "genre");
                if (!ge || !strlist_contains(&ge->options, d->preview.genres.v[g])) continue;
                int other = 0;
                for (int e = 0; e < cat->n_extra; e++) if (cat->extra[e].is_required && strcmp(cat->extra[e].name, "genre") != 0) other = 1;
                if (other) continue;
                const char *kv[] = { "genre", d->preview.genres.v[g], NULL, NULL };
                char q[160], url[1024];
                addon_extra_query(q, sizeof q, kv);
                addon_url_catalog(a, cat->type, cat->id, q, url, sizeof url);
                snprintf(d->sim_genre, sizeof d->sim_genre, "%s", d->preview.genres.v[g]);
                d->sim_req = http_request_async(url, NULL, on_similar, d);
                return;
            }
        }
}

static float facts_height(const Detail *d);

static void update(Screen *s, float dt)
{
    Detail *d = s->st;
    const TmdbInfo *tm = tmdb_get(d->preview.id, d->preview.type);
    int n_cast = cast_count(d, tm);
    if (d->has_meta && !d->meta.trailer_yt && tm && tm->trailer_yt[0]) d->meta.trailer_yt = strdup(tm->trailer_yt);   /* TMDB's, when the addon has none */
    d->age += dt;
    if (d->has_meta) d->facts_h = facts_height(d);
    if (g_trailer.busy && strcmp(g_trailer.id, d->preview.id) == 0) island_progress("Getting the trailer from YouTube", -1);
    if (!d->sim_asked && d->has_meta && (d->age > 1.5f || d->area >= AREA_ABOUT)) fetch_similar(d);
    if (d->area == AREA_CAST && d->cast_focus >= n_cast) d->cast_focus = n_cast ? n_cast - 1 : 0;
    float view = SNTV_UI_W - 2 * THEME_MARGIN;
    const TmdbCollection *fc = franchise(d);
    float st = scroll_target(d, n_cast), ct = row_offset(d->cast_focus, n_cast, CAST_STEP, view), mt = row_offset(d->sim_focus, d->n_similar, SIM_W + SIM_GAP, view);
    float ft = row_offset(d->fr_focus, fc ? fc->n : 0, SIM_W + SIM_GAP, view);
    if (g_app.fx_light) { d->scroll = st; d->scroll_v = 0; d->cast_x = ct; d->sim_x = mt; d->fr_x = ft; }
    else {
        d->scroll = ui_ease(d->scroll, st, &d->scroll_v, dt, 11);
        d->cast_x = ui_ease(d->cast_x, ct, NULL, dt, 14);
        d->sim_x = ui_ease(d->sim_x, mt, NULL, dt, 14);
        d->fr_x = ui_ease(d->fr_x, ft, NULL, dt, 14);
        if (fabsf(d->scroll - st) > 0.5f || fabsf(d->cast_x - ct) > 0.5f || fabsf(d->sim_x - mt) > 0.5f || fabsf(d->fr_x - ft) > 0.5f) app_redraw();
    }
    d->rail_x = ui_ease(d->rail_x, d->ep_focus * (EP_W + EP_GAP), NULL, dt, 14);   /* the focused episode leads the row */
    d->focus_t = ui_ease(d->focus_t * 100, 100, NULL, dt, 16) / 100;
    d->intro = ui_ease(d->intro * 100, 100, NULL, dt, 9) / 100;
    if (d->ask_open) { d->ask_t = ui_ease(d->ask_t * 100, 100, NULL, dt, 14) / 100; if (d->ask_t < 0.999f || d->ask_req) app_redraw(); }
    if (d->loading) app_redraw();
    if (d->meta_wait && !d->has_meta && d->addons_seen != g_app.addons_version && !d->req) { d->meta_wait = 0; d->error[0] = 0; fetch_meta(d); }
    {   /* the sources Play would open, asked once the focus rests: they are in by the press */
        const char *pre = NULL;
        if (d->has_meta && d->age > 0.8f) {
            if (d->meta.n_videos > 0 && d->area == AREA_EPISODES && d->ep_focus < d->n_eps) pre = d->meta.videos[d->ep_index[d->ep_focus]].id;
            else if (d->resume_label[0] && !d->resume_url[0]) pre = d->resume_video;
            else if (d->meta.n_videos == 0) pre = d->meta.default_video_id ? d->meta.default_video_id : d->preview.id;
        }
        if (pre && strcmp(pre, d->pre_id)) { snprintf(d->pre_id, sizeof d->pre_id, "%s", pre); d->pre_since = g_app.time; }
        else if (pre && g_app.time - d->pre_since > 0.8f) streams_prefetch(&d->preview, pre);
    }
    if (g_theme.id != THEME_APERTURE && d->scroll < 60 && !d->ask_open) bgtrailer_want(d->preview.id, SNTV_UI_H, 2.5f);   /* Aperture uses a quiet artwork window */
    /* prefetch upcoming thumbnails */
    for (int k = d->ep_focus; k < d->n_eps && k < d->ep_focus + 6; k++) {
        const char *t = d->meta.videos[d->ep_index[k]].thumbnail;
        if (t) image_get_ex(t, IMAGE_MIPMAP, NULL, NULL, NULL);
    }
}

/* "2021-08-22" -> "22 Aug 2021" */
static void nice_date(const char *iso, char *out, size_t n)
{
    static const char *M[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
    int y, m, dd;
    if (iso && sscanf(iso, "%d-%d-%d", &y, &m, &dd) == 3 && m >= 1 && m <= 12) snprintf(out, n, "%d %s %d", dd, M[m - 1], y);
    else snprintf(out, n, "%s", iso ? iso : "");
}

/* What the focused episode is, where the show's synopsis sits while the rail has focus. */
static void draw_episode_info(const Detail *d, const LibraryItem *li, float x, float y, float w)
{
    const MetaVideo *v = &d->meta.videos[d->ep_index[d->ep_focus]];
    int a = (int)(255 * (g_app.fx_light ? 1.0f : d->focus_t));
    char kicker[96], date[32] = "", meta[160] = "";
    if (v->season > 0 && v->episode >= 0) snprintf(kicker, sizeof kicker, "SEASON %d  \xC2\xB7  EPISODE %d OF %d", v->season, v->episode, d->n_eps);
    else if (v->season == 0) snprintf(kicker, sizeof kicker, "SPECIAL %d OF %d", d->ep_focus + 1, d->n_eps);
    else snprintf(kicker, sizeof kicker, "EPISODE %d OF %d", d->ep_focus + 1, d->n_eps);
    font_draw(kicker, x, y, FONT_XS | FONT_B, COLOR_ALPHA(THEME_ACCENT, a));
    font_draw_fit(v->title && *v->title ? v->title : "Untitled episode", x, y + 30, g_theme.id == THEME_APERTURE ? FONT_L | FONT_ALT : FONT_L | FONT_B, COLOR_ALPHA(THEME_TEXT, a), w);
    nice_date(v->released, date, sizeof date);
    size_t o = (size_t)snprintf(meta, sizeof meta, "%s", date);
    if (d->preview.runtime && *d->preview.runtime) o += (size_t)snprintf(meta + o, sizeof meta - o, "%s%s", o ? "  \xC2\xB7  " : "", d->preview.runtime);
    int is_cur = li && li->video_id && strcmp(li->video_id, v->id) == 0;
    if (is_cur && li->duration && li->time_offset) {
        int left = (int)((li->duration - li->time_offset) / 60000);
        snprintf(meta + o, sizeof meta - o, "%s%d min left", o ? "  \xC2\xB7  " : "", left > 0 ? left : 1);
    } else if (li) {
        int cur_idx = -1;
        for (int k = 0; k < d->n_eps; k++) if (li->video_id && strcmp(d->meta.videos[d->ep_index[k]].id, li->video_id) == 0) cur_idx = k;
        if (ep_watched(d, d->ep_focus, li, cur_idx)) snprintf(meta + o, sizeof meta - o, "%sWatched", o ? "  \xC2\xB7  " : "");
    }
    font_draw(meta, x, y + 86, FONT_S | FONT_B, COLOR_ALPHA(THEME_TEXT_DIM, a));
    if (v->overview && *v->overview) font_draw_wrapped(v->overview, x, y + 128, FONT_S, COLOR_ALPHA(THEME_TEXT_DIM, a), w, 3);
    else font_draw("No synopsis for this episode yet.", x, y + 128, FONT_S, COLOR_ALPHA(THEME_TEXT_MUTED, a));
}

/* text centred under a point, cut to max_w */
static void draw_center_fit(const char *t, float cx, float y, int fs, Color c, float max_w)
{
    float w = font_measure(t, fs);
    if (w <= max_w) font_draw(t, cx - w / 2, y, fs, c);
    else font_draw_fit(t, cx - max_w / 2, y, fs, c, max_w);
}

/* "A, B" from the first two of a list */
static void join2(const StrList *l, char *out, size_t n)
{
    out[0] = 0;
    for (int i = 0; i < l->n && i < 2; i++) { size_t o = strlen(out); snprintf(out + o, n - o, "%s%s", i ? ", " : "", l->v[i]); }
}

/* the facts under the synopsis; the strings point into the struct */
typedef struct { const char *k[6], *v[6]; int n; char dir[160], wri[160], rel[32], runtime[64]; } Facts;
static void facts_get(const Detail *d, Facts *F)
{
    int series = d->meta.n_videos > 0;
    join2(&d->meta.directors, F->dir, sizeof F->dir);
    join2(&d->meta.writers, F->wri, sizeof F->wri);
    nice_date(d->meta.released, F->rel, sizeof F->rel);
    if (series) {
        int real = 0; for (int i = 0; i < d->meta.n_videos; i++) if (d->meta.videos[i].season > 0) real++;
        snprintf(F->runtime, sizeof F->runtime, "%d episode%s", real, real == 1 ? "" : "s");
    } else snprintf(F->runtime, sizeof F->runtime, "%s", d->preview.runtime ? d->preview.runtime : "");
    const char *k[] = { series ? "CREATED BY" : "DIRECTED BY", "WRITTEN BY", series ? "FIRST AIRED" : "RELEASED", "COUNTRY", series ? "EPISODES" : "RUNTIME", "YEARS" };
    const char *v[] = { F->dir, F->wri, F->rel, d->meta.country, F->runtime, series ? d->preview.release_info : NULL };
    F->n = 0;
    for (int i = 0; i < 6; i++) if (v[i] && v[i][0]) { F->k[F->n] = k[i]; F->v[F->n] = v[i]; F->n++; }
}

#define FACTS_PAD 36.0f
static float facts_width(void) { return (SNTV_UI_W - 2 * THEME_MARGIN) * 0.64f; }

/* the facts panel's height: what is in it, and room for the ratings card */
static float facts_height(const Detail *d)
{
    Facts F; facts_get(d, &F);
    const char *desc = d->preview.description;
    int lines = desc && *desc ? font_draw_wrapped(desc, 0, 0, FONT_S, 0, facts_width() - 2 * FACTS_PAD, 3) : 0;
    float h = 30 + (lines ? lines * font_height(FONT_S) + 24 : 0) + ((F.n + 2) / 3) * 74 + (d->meta.awards && *d->meta.awards ? 56 : 0) + 14;
    return h < 300 ? 300 : h;
}

static void draw_about(Detail *d, const TmdbInfo *tm, const LibraryItem *li, float x)
{
    if (ABOUT_Y - d->scroll > SNTV_UI_H) return;   /* not in view yet */
    int n_cast = cast_count(d, tm), series = d->meta.n_videos > 0;
    AboutLayout L = about_layout(d, n_cast);
    float right = SNTV_UI_W - THEME_MARGIN, f = g_app.fx_light ? 1.0f : d->focus_t;
    ui_row_header(0, "About", d->preview.type && strcmp(d->preview.type, "series") == 0 ? "Series" : "Film", 0, x, ABOUT_Y, right, d->area == AREA_ABOUT);

    /* the facts: the full synopsis, then who made it, when and where */
    float fw = facts_width(), fy = L.facts, pad = FACTS_PAD, FH = d->facts_h;
    ui_panel(x, fy, fw, FH, R_PANEL, COLOR_ALPHA(THEME_PANEL, 0xD0));
    if (d->area == AREA_ABOUT) gfx_rrect_ring(x, fy, fw, FH, R_PANEL, 3, THEME_FOCUS);
    float py = fy + 30, pw = fw - 2 * pad;
    if (d->preview.description && *d->preview.description)
        py += font_draw_wrapped(d->preview.description, x + pad, py, FONT_S, THEME_TEXT, pw, 3) * font_height(FONT_S) + 24;
    Facts F; facts_get(d, &F);
    float colw = pw / 3;
    for (int i = 0; i < F.n; i++) {
        float cx = x + pad + (i % 3) * colw, cy = py + (i / 3) * 74;
        font_draw_tracked(F.k[i], cx, cy, FONT_XS | FONT_B, THEME_TEXT_MUTED, 1.2f);
        font_draw_fit(F.v[i], cx, cy + 28, FONT_S | FONT_B, THEME_TEXT, colw - 24);
    }
    if (d->meta.awards && *d->meta.awards) {
        float ay = fy + FH - 50;
        ui_icon(ICON_STAR, x + pad + 12, ay + font_height(FONT_S) / 2, 24, THEME_IMDB);
        font_draw_fit(d->meta.awards, x + pad + 36, ay, FONT_S, THEME_TEXT_DIM, pw - 36);
    }

    /* the ratings card */
    float rx = x + fw + 28, rw = right - rx, ry = fy;
    ui_panel(rx, ry, rw, FH, R_PANEL, COLOR_ALPHA(THEME_PANEL, 0xD0));
    float cx = rx + pad, cy = ry + 30;
    font_draw_tracked("RATINGS", cx, cy, FONT_XS | FONT_B, THEME_TEXT_MUTED, 1.2f);
    cy += 40;
    double imdb = d->preview.imdb_rating ? atof(d->preview.imdb_rating) : 0;
    if (imdb > 0) {
        char big[16]; snprintf(big, sizeof big, "%.1f", imdb);
        font_draw(big, cx, cy, FONT_XL | FONT_B, THEME_TEXT);
        float bw = font_measure(big, FONT_XL | FONT_B);
        font_draw("/ 10", cx + bw + 10, cy + font_height(FONT_XL | FONT_B) - font_height(FONT_S) - 6, FONT_S, THEME_TEXT_MUTED);
        float lw = font_measure("IMDb", FONT_XS | FONT_B) + 18, lx = rx + rw - pad - lw;
        gfx_rrect(lx, cy + 10, lw, 34, 8, THEME_IMDB);
        font_draw("IMDb", lx + 9, cy + 10 + (34 - font_height(FONT_XS | FONT_B)) / 2, FONT_XS | FONT_B, RGBA(0, 0, 0, 0xFF));
        cy += font_height(FONT_XL | FONT_B) + 14;
        /* ten segments, the last one partly filled */
        float sw = (rw - 2 * pad - 9 * 6) / 10;
        for (int i = 0; i < 10; i++) {
            float sx = cx + i * (sw + 6), fill = (float)imdb - i;
            gfx_rrect(sx, cy, sw, 8, 4, COLOR_ALPHA(THEME_TEXT, 0x22));
            if (fill > 0) gfx_rrect(sx, cy, sw * (fill > 1 ? 1 : fill), 8, 4, THEME_IMDB);
        }
        cy += 36;
    } else { font_draw("No rating yet", cx, cy, FONT_S | FONT_B, THEME_TEXT_DIM); cy += 50; }
    const char *more = mdblist_line(d->preview.id, d->preview.type);   /* MDBList: the other critics */
    if (more) { font_draw_fit(more, cx, cy - 8, FONT_XS | FONT_B, THEME_TEXT_DIM, rw - 2 * pad); cy += 34; }
    if (tm && tm->cert[0]) {
        font_draw("Age rating", cx, cy + 6, FONT_S, THEME_TEXT_DIM);
        ui_chip_outline(tm->cert, rx + rw - pad - font_measure(tm->cert, FONT_XS | FONT_B) - 28, cy, COLOR_ALPHA(THEME_TEXT, 0x70), THEME_TEXT);
        cy += 50;
    }
    /* how far the viewer is */
    char prog[96] = "Not started";
    if (li && !li->removed) {
        int se = 0, ep = 0;
        const char *c1 = li->video_id ? strchr(li->video_id, ':') : NULL;
        if (series && c1 && sscanf(c1, ":%d:%d", &se, &ep) == 2) snprintf(prog, sizeof prog, "Up to S%d E%d", se, ep);
        else if (li->time_offset && li->duration) snprintf(prog, sizeof prog, "%d%% watched", (int)(100 * (double)li->time_offset / (double)li->duration));
        else if (li->times_watched) snprintf(prog, sizeof prog, "Watched");
        else snprintf(prog, sizeof prog, "In your Library");
    }
    font_draw("You", cx, cy + 6, FONT_S, THEME_TEXT_DIM);
    float pw2 = font_measure(prog, FONT_S | FONT_B);
    font_draw(prog, rx + rw - pad - pw2, cy + 6, FONT_S | FONT_B, THEME_TEXT);

    /* the cast */
    if (L.cast >= 0 && L.cast - d->scroll < SNTV_UI_H) {
        ui_row_header(1, "Cast", NULL, n_cast, x, L.cast, right, d->area == AREA_CAST);
        float rowy = L.cast + 60, r0 = CAST_D / 2;
        for (int pass = 0; pass < 2; pass++)
            for (int i = 0; i < n_cast; i++) {
                float ix = x + i * CAST_STEP - d->cast_x;
                if (ix > SNTV_UI_W || ix + CAST_STEP < 0) continue;
                int foc = d->area == AREA_CAST && d->cast_focus == i;
                if ((pass == 1) != foc) continue;
                const char *name, *role, *photo;
                cast_at(d, tm, i, &name, &role, &photo);
                float sc = ui_focus_scale(foc ? f : 0), r = r0 * sc, ccx = ix + r0 + 4, ccy = rowy + r0;
                if (foc) gfx_rrect_ring(ccx - r, ccy - r, 2 * r, 2 * r, r, 4, THEME_FOCUS);
                ui_portrait(photo, name, ccx - r, ccy - r, 2 * r, 2 * r, r, FONT_L | FONT_B);
                float ty = rowy + CAST_D + 16;
                draw_center_fit(name, ccx, ty, FONT_XS | FONT_B, foc ? THEME_TEXT : THEME_TEXT_DIM, CAST_STEP - 14);
                if (role && *role) draw_center_fit(role, ccx, ty + 30, FONT_XS, THEME_TEXT_MUTED, CAST_STEP - 14);
            }
    }

    /* the franchise, in release order; this film marked */
    const TmdbCollection *fc = franchise(d);
    if (fc && L.franchise >= 0 && L.franchise - d->scroll < SNTV_UI_H) {
        const TmdbInfo *tmi = tmdb_get(d->preview.id, d->preview.type);
        ui_row_header(2, "In order", fc->name, fc->n, x, L.franchise, right, d->area == AREA_FRANCHISE);
        float rowy = L.franchise + 60;
        for (int pass = 0; pass < 2; pass++)
            for (int i = 0; i < fc->n; i++) {
                float ix = x + i * (SIM_W + SIM_GAP) - d->fr_x;
                if (ix > SNTV_UI_W || ix + SIM_W < 0) continue;
                int foc = d->area == AREA_FRANCHISE && d->fr_focus == i;
                if ((pass == 1) != foc) continue;
                const TmdbPart *pt = &fc->parts[i];
                ui_card(pt->poster[0] ? pt->poster : NULL, ix, rowy, SIM_W, SIM_H, foc ? f : 0, 0, pt->title, 0);
                char lbl[32];
                snprintf(lbl, sizeof lbl, "%d%s%s", i + 1, pt->year[0] ? "  \xC2\xB7  " : "", pt->year);
                font_draw(lbl, ix, rowy + SIM_H + 14, FONT_XS | FONT_B, foc ? THEME_TEXT : THEME_TEXT_MUTED);
                if (tmi && pt->tmdb_id == tmi->tmdb_id) ui_chip("This film", ix + 10, rowy + 10, THEME_ACCENT, THEME_ON_ACCENT);
            }
    }

    /* more like this */
    if (L.similar >= 0 && L.similar - d->scroll < SNTV_UI_H) {
        char src[96]; snprintf(src, sizeof src, "%s", d->sim_genre);
        ui_row_header(L.franchise >= 0 ? 3 : 2, "More like this", src, d->n_similar, x, L.similar, right, d->area == AREA_SIMILAR);
        float rowy = L.similar + 60;
        for (int pass = 0; pass < 2; pass++)
            for (int i = 0; i < d->n_similar; i++) {
                float ix = x + i * (SIM_W + SIM_GAP) - d->sim_x;
                if (ix > SNTV_UI_W || ix + SIM_W < 0) continue;
                int foc = d->area == AREA_SIMILAR && d->sim_focus == i;
                if ((pass == 1) != foc) continue;
                char pb[512];
                const char *poster = app_art_poster(&d->similar[i], pb, sizeof pb);
                ui_card(poster, ix, rowy, SIM_W, SIM_H, foc ? f : 0, 0, d->similar[i].name, 0);
            }
    }
}

/* Aperture, films: the first of the cast as portraits under the actions */
static void draw_aperture_cast(const Detail *d, const TmdbInfo *tm, float x, float y)
{
    int n = tm && tm->n_cast ? tm->n_cast : d->preview.cast.n;
    if (n <= 0 || y > SNTV_UI_H - 200) return;
    if (n > 5) n = 5;
    font_draw("Cast", x, y, FONT_XS, THEME_TEXT_DIM);
    const float w = 168, h = 200, gap = 18;
    for (int i = 0; i < n; i++) {
        const char *name, *role, *photo;
        cast_at(d, tm, i, &name, &role, &photo);
        float px = x + i * (w + gap), py = y + 34;
        gfx_rect(px, py, w, h, THEME_BG2);
        ui_portrait(photo, name, px, py, w, h, 2, FONT_L | FONT_ALT);
        font_draw_fit(name, px, py + h + 12, FONT_XS, THEME_TEXT_DIM, w);
    }
}

static void draw_series(Detail *d, const LibraryItem *li, float x)
{
    /* season tabs */
    float sy = RAIL_Y - 84, sx = x, avail = SNTV_UI_W - THEME_MARGIN - x;
    /* many seasons: the strip starts late enough for the selected one to show */
    int start = 0;
    for (;;) {
        float used = 0;
        for (int i = start; i <= d->season_idx && i < d->n_seasons; i++) {
            char l2[32];
            if (d->seasons[i] == 0) snprintf(l2, sizeof l2, "Specials"); else snprintf(l2, sizeof l2, "Season %d", d->seasons[i]);
            used += font_measure(l2, FONT_S | FONT_B) + 52;
        }
        if (used <= avail - 60 || start >= d->season_idx) break;
        start++;
    }
    if (start > 0) { ui_icon(ICON_CHEVRON_LEFT, sx + 10, sy + 26, 22, THEME_TEXT_MUTED); sx += 34; }
    for (int i = start; i < d->n_seasons; i++) {
        char lbl[32];
        if (d->seasons[i] == 0) snprintf(lbl, sizeof lbl, "Specials"); else snprintf(lbl, sizeof lbl, "Season %d", d->seasons[i]);
        int sel = d->season_idx == i, foc = d->area == AREA_SEASONS && sel;
        float w = font_measure(lbl, FONT_S | FONT_B) + 44;
        if (sx + w > SNTV_UI_W - THEME_MARGIN) { ui_icon(ICON_CHEVRON_RIGHT, SNTV_UI_W - THEME_MARGIN - 10, sy + 26, 22, THEME_TEXT_MUTED); break; }
        if (g_theme.id == THEME_APERTURE) {
            float tw = font_measure(lbl, FONT_M);
            font_draw(lbl, sx, sy + (52 - font_height(FONT_M)) / 2, FONT_M, sel ? THEME_TEXT : THEME_TEXT_MUTED);
            if (sel) gfx_rect(sx, sy + 50, tw, foc ? 4 : 3, foc ? THEME_ACCENT : COLOR_ALPHA(THEME_ACCENT, 0xB0));
            if (foc) gfx_rect(sx - 14, sy + 6, 3, 40, THEME_TEXT);
            sx += tw + 56;
            continue;
        }
        if (foc) gfx_rrect(sx, sy, w, 52, theme_pill_r(52), THEME_FOCUS);
        else if (sel) gfx_rrect(sx, sy, w, 52, R_PANEL, COLOR_ALPHA(THEME_TEXT, 0x2C));
        font_draw(lbl, sx + 22, sy + (52 - font_height(FONT_S | FONT_B)) / 2, FONT_S | FONT_B, foc ? THEME_ON_FOCUS : sel ? THEME_TEXT : THEME_TEXT_MUTED);
        sx += w + 8;
    }

    /* episode rail */
    int cur_idx = -1;
    if (li && li->video_id)
        for (int kk = 0; kk < d->n_eps; kk++) if (strcmp(d->meta.videos[d->ep_index[kk]].id, li->video_id) == 0) cur_idx = kk;
    int first = (int)(d->rail_x / (EP_W + EP_GAP)) - 1;
    if (first < 0) first = 0;
    for (int pass = 0; pass < 2; pass++)
        for (int kk = first; kk < d->n_eps && kk < first + 7; kk++) {
            const MetaVideo *v = &d->meta.videos[d->ep_index[kk]];
            float px = x + kk * (EP_W + EP_GAP) - d->rail_x;
            if (px > SNTV_UI_W + 20 || px + EP_W < -20) continue;
            int foc = d->area == AREA_EPISODES && d->ep_focus == kk;
            if ((pass == 1) != foc) continue;
            float f = foc ? (g_app.fx_light ? 1.0f : d->focus_t) : 0;
            int watched = ep_watched(d, kk, li, cur_idx);
            int is_cur = kk == cur_idx;
            int blur = g_app.spoiler_blur && !watched && !is_cur && cur_idx >= 0 ? 1 : (g_app.spoiler_blur && cur_idx < 0 && kk > 0);
            ui_card(v->thumbnail, px, RAIL_Y, EP_W, EP_H, f, d->area == AREA_EPISODES ? 0 : 0.3f, NULL, (blur ? CARD_BLUR : CARD_MIP) | CARD_TRUE_COLOUR);
            float sc = ui_focus_scale(f), w2 = EP_W * sc, h2 = EP_H * sc, x2 = px - (w2 - EP_W) / 2, y2 = RAIL_Y - (h2 - EP_H) / 2;
            if (!v->thumbnail) { char n[16]; snprintf(n, sizeof n, "%d", v->episode >= 0 ? v->episode : kk + 1);
                font_draw_center(n, x2 + w2 / 2, y2 + h2 / 2 - 40, FONT_TITLE | FONT_B, COLOR_ALPHA(THEME_TEXT, 0x40)); }
            if (watched) { gfx_circle(x2 + w2 - 30, y2 + 30, 18, THEME_OK); ui_icon(ICON_CHECK, x2 + w2 - 30, y2 + 30, 22, THEME_ON_LIGHT); }
            if (is_cur && li->duration && li->time_offset) ui_progress(x2 + 16, y2 + h2 - 18, w2 - 32, 6, (float)li->time_offset / (float)li->duration, THEME_ACCENT);
            if (foc) { gfx_circle(x2 + w2 / 2, y2 + h2 / 2, 34, RGBA(0, 0, 0, 0x80)); ui_icon(ICON_PLAY, x2 + w2 / 2 + 3, y2 + h2 / 2, 30, THEME_TEXT); }
            char lbl[256];
            if (g_theme.id == THEME_APERTURE) {   /* "02  Dead air", its length under it, a line for how far you got */
                char num[8]; snprintf(num, sizeof num, "%02d", v->episode >= 0 ? v->episode : kk + 1);
                float ly = RAIL_Y + EP_H + 20;
                font_draw(num, px, ly, FONT_M, foc ? THEME_TEXT : THEME_TEXT_DIM);
                font_draw_fit(v->title && *v->title ? v->title : "Episode", px + font_measure(num, FONT_M) + 18, ly, FONT_M, foc ? THEME_TEXT : THEME_TEXT_DIM, EP_W - 60);
                char sub[40] = "";
                if (d->preview.runtime && *d->preview.runtime) snprintf(sub, sizeof sub, "%s", d->preview.runtime);
                else if (v->released) snprintf(sub, sizeof sub, "%.10s", v->released);
                font_draw(sub, px, ly + 44, FONT_XS, THEME_TEXT_MUTED);
                if (is_cur && li->duration && li->time_offset) {
                    float bx = px + 120, bw = EP_W - 140;
                    gfx_rect(bx, ly + 54, bw, 3, COLOR_ALPHA(THEME_TEXT, 0x30));
                    gfx_rect(bx, ly + 54, bw * (float)li->time_offset / (float)li->duration, 3, THEME_ACCENT);
                }
                continue;
            }
            if (v->episode >= 0) snprintf(lbl, sizeof lbl, "E%d  %s", v->episode, v->title ? v->title : "");
            else snprintf(lbl, sizeof lbl, "%s", v->title ? v->title : "Episode");
            float ly = RAIL_Y + EP_H + 16 + (foc ? 12 : 0);
            font_draw_fit(lbl, px, ly, FONT_S | FONT_B, foc || d->area != AREA_EPISODES ? THEME_TEXT : THEME_TEXT_DIM, EP_W);
            if (foc && ui_hold_hint(px, ly + 36, EP_W)) {}
            else if (v->released && !foc) { char date[11]; snprintf(date, sizeof date, "%.10s", v->released); font_draw(date, px, ly + 34, FONT_XS, THEME_TEXT_MUTED); }
        }
}

static void draw(Screen *s)
{
    Detail *d = s->st;
    char buf[512], lbuf[512];
    const char *bg = app_art_background(&d->preview, buf, sizeof buf);
    if (bg) ui_set_ambient_art(bg);
    ui_background(image_tint(bg));
    int iw, ih;
    unsigned bgt = bg && g_theme.id != THEME_APERTURE ? image_get(bg, &iw, &ih) : 0;
    int series = d->meta.n_videos > 0;
    float k = g_app.fx_light ? 1.0f : d->intro;
    float tv = bgtrailer_shown(d->preview.id);   /* the trailer playing behind the artwork */
    if (g_theme.id == THEME_APERTURE) {
        float ah = series ? RAIL_Y - 110 : SNTV_UI_H;
        ui_aperture_art(bg, 700, 0, SNTV_UI_W - 700, ah, k);
        gfx_gradient_h(699, 0, 520, ah, THEME_BG, COLOR_ALPHA(THEME_BG, 0));
        gfx_gradient_v(0, ah - 320, SNTV_UI_W, 320, COLOR_ALPHA(THEME_BG, 0), THEME_BG);
    } else if (bgt || tv > 0) {
        ui_backdrop_fades(1250, series ? 420 : 560, SNTV_UI_H);
        ui_backdrop_video(tv, SNTV_UI_H);
        ui_backdrop_image(bgt, iw, ih, k * (1 - tv), SNTV_UI_H);
        ui_text_scrim(1300, SNTV_UI_H);
        if (d->scroll < 60) bgtrailer_draw_badge(d->preview.id);
    }
    if (d->scroll > 1) {   /* About reads over a darker backdrop */
        float t = d->scroll / (ABOUT_Y - 150); if (t > 1) t = 1;
        gfx_rect(0, 0, SNTV_UI_W, SNTV_UI_H, COLOR_ALPHA(THEME_BG, (int)(0xD8 * t)));
    }
    gfx_set_translate(0, -d->scroll);

    /* movies sit lower and more cinematic; series leave room for the episode rail */
    float x = THEME_MARGIN, y = (series || d->loading ? 84 : 250) + (1 - k) * 24, col_w = 1000;
    if (g_theme.id == THEME_APERTURE) { y = series || d->loading ? 84 : 164; col_w = 820; }
    int a = (int)(255 * k);
    const char *logo = app_art_logo(&d->preview, lbuf, sizeof lbuf);
    unsigned ltex = logo && g_theme.id != THEME_APERTURE ? image_get(logo, &iw, &ih) : 0;
    if (g_theme.id == THEME_APERTURE) {
        ui_serif_title(d->preview.name, x, series ? 270 : 360, col_w, series ? FONT_HUGE : FONT_HERO, 2, COLOR_ALPHA(THEME_TEXT, a));
        y = series ? 296 : 386;
    } else if (ltex && ih > 0) {
        float w = 640, h = w * ih / iw;
        if (h > 180) { h = 180; w = h * iw / ih; }
        gfx_tex(ltex, x, y + (180 - h), w, h, RGBA(255, 255, 255, a));
        y += 200;
    } else {
        ui_hero_title(d->preview.name, x, y + 180, col_w, COLOR_ALPHA(THEME_TEXT, a));
        y += 200;
    }

    const TmdbInfo *tm = tmdb_get(d->preview.id, d->preview.type);
    LibraryItem *li = app_library_find(d->preview.id);
    if (series && d->area == AREA_EPISODES && d->ep_focus < d->n_eps) {
        draw_episode_info(d, li, x, y, col_w);
        y += 236;
    } else if (g_theme.id == THEME_APERTURE) {
        char seasons_txt[32] = "";
        if (d->n_seasons) { int real = 0; for (int i = 0; i < d->n_seasons; i++) if (d->seasons[i] > 0) real++;
            snprintf(seasons_txt, sizeof seasons_txt, "%d Season%s", real, real == 1 ? "" : "s"); }
        const char *parts[5] = { d->preview.release_info, d->preview.runtime, d->preview.genres.n ? d->preview.genres.v[0] : NULL, seasons_txt, d->preview.imdb_rating };
        char imdb[24] = "";
        if (d->preview.imdb_rating && *d->preview.imdb_rating) { snprintf(imdb, sizeof imdb, "IMDb %s", d->preview.imdb_rating); parts[4] = imdb; }
        ui_slash_line(parts, 5, x, y, FONT_S, THEME_TEXT);
        y += 52;
        if (d->preview.description && !series)
            y += font_draw_wrapped(d->preview.description, x, y, FONT_S, THEME_TEXT_DIM, 680, 3) * font_height(FONT_S) + 30;
    } else {
        float mx = x;
        if (d->preview.imdb_rating && *d->preview.imdb_rating) mx += ui_imdb(d->preview.imdb_rating, mx, y) + 4;
        if (tm && tm->cert[0]) mx += ui_chip_outline(tm->cert, mx, y, COLOR_ALPHA(THEME_TEXT, 0x70), THEME_TEXT) + 8;
        const char *parts[4]; int np = 0;
        char seasons_txt[32];
        if (d->preview.release_info) parts[np++] = d->preview.release_info;
        if (d->preview.runtime) parts[np++] = d->preview.runtime;
        if (d->n_seasons) { int real = 0; for (int i = 0; i < d->n_seasons; i++) if (d->seasons[i] > 0) real++;
            snprintf(seasons_txt, sizeof seasons_txt, "%d Season%s", real, real == 1 ? "" : "s"); parts[np++] = seasons_txt; }
        for (int i = 0; i < np; i++) {
            if (mx > x) mx = ui_dot_sep(mx, y, THEME_TEXT_MUTED);
            font_draw(parts[i], mx, y + (36 - font_height(FONT_S | FONT_B)) / 2, FONT_S | FONT_B, THEME_TEXT_DIM);
            mx += font_measure(parts[i], FONT_S | FONT_B);
        }
        y += 52;
        if (d->preview.genres.n) {
            float gx = x;
            for (int g = 0; g < d->preview.genres.n && g < 5; g++) gx += ui_chip_outline(d->preview.genres.v[g], gx, y, COLOR_ALPHA(THEME_TEXT, 0x50), THEME_TEXT_DIM) + 10;
            y += 54;
        }
        if (d->preview.description)
            y += font_draw_wrapped(d->preview.description, x, y, FONT_S, THEME_TEXT_DIM, col_w, series ? 2 : g_theme.id == THEME_APERTURE ? 3 : 5) * font_height(FONT_S) + 26;
    }
    if (d->loading) { ui_spinner(x + 26, y + 30, 22); y += 70; }
    if (d->error[0]) { font_draw(d->error, x, y, FONT_S, THEME_DANGER); y += 44; }
    /* actions */
    int in_lib = li && !li->removed;
    const char *play_label = d->resume_label[0] ? d->resume_label : series ? "Episodes" : "Play";
    float by = y > 500 ? y : 500;
    if (series && by > 520) by = 520;
    if (g_theme.id == THEME_APERTURE) {
        int cur = d->area == AREA_ACTIONS ? btn_at(d, d->btn) : -1;
        float bx = x + ui_aperture_button(!strcmp(play_label, "Play") ? "Watch film" : play_label, d->resume_label[0] || !series ? ICON_PLAY : ICON_CHEVRON_RIGHT, x, by, 1, cur == BTN_PLAY) + 48;
        if (d->resume_label[0] && d->resume_frac > 0) ui_progress(x, by + 76, 200, 3, (float)d->resume_frac, THEME_ACCENT);
        if (d->resume_label[0]) bx += ui_aperture_button("From the start", ICON_PREV, bx, by, 0, cur == BTN_RESTART) + 44;
        if (has_trailer(d)) {
            int getting = g_trailer.busy && strcmp(g_trailer.id, d->preview.id) == 0;
            bx += ui_aperture_button(getting ? "Getting trailer\xE2\x80\xA6" : "Trailer", ICON_TV, bx, by, 0, cur == BTN_TRAILER) + 44;
        }
        ui_aperture_button(in_lib ? "In your library" : "Add to library", in_lib ? ICON_CHECK : ICON_PLUS, bx, by, 0, cur == BTN_LIBRARY);
        if (!series) draw_aperture_cast(d, tm, x, by + 130);
    } else {
    float bw = ui_pill(play_label, 1, d->resume_label[0] || !series ? ICON_PLAY : ICON_CHEVRON_RIGHT, x, by, 72, PILL_PRIMARY,
                       d->area == AREA_ACTIONS && d->btn == 0);
    if (d->resume_label[0] && d->resume_frac > 0) ui_progress(x + 36, by + 82, bw - 72, 5, (float)d->resume_frac, THEME_ACCENT);
    float bx2 = x + bw + 18;
    int cur = d->area == AREA_ACTIONS ? btn_at(d, d->btn) : -1;
    if (d->resume_label[0]) bx2 += ui_pill("From the start", 1, ICON_PREV, bx2, by, 72, PILL_SECONDARY, cur == BTN_RESTART) + 18;
    if (has_trailer(d)) {
        int getting = g_trailer.busy && strcmp(g_trailer.id, d->preview.id) == 0;
        bx2 += ui_pill(getting ? "Getting trailer\xE2\x80\xA6" : "Trailer", 1, ICON_TV, bx2, by, 72, PILL_SECONDARY, cur == BTN_TRAILER) + 18;
    }
    ui_pill(in_lib ? "In Library" : "Add to Library", 1, in_lib ? ICON_CHECK : ICON_PLUS, bx2, by, 72, PILL_SECONDARY, cur == BTN_LIBRARY);
    }
    if (series) draw_series(d, li, x);
    if (d->has_meta && d->scroll < 160 && !(series && g_theme.id == THEME_APERTURE)) {   /* the way down to About; gone once the page moves (Aperture's episodes reach the bottom) */
        Color c = COLOR_ALPHA(THEME_TEXT_MUTED, (int)(((THEME_TEXT_MUTED) & 0xFF) * (1 - d->scroll / 160)));
        float cw = font_measure("About, cast and more", FONT_XS | FONT_B) + 36, cx = (SNTV_UI_W - cw) / 2, cy = SNTV_UI_H - 44;
        ui_icon(ICON_CHEVRON_DOWN, cx + 10, cy + font_height(FONT_XS | FONT_B) / 2, 22, c);
        font_draw("About, cast and more", cx + 36, cy, FONT_XS | FONT_B, c);
    }
    if (d->has_meta) draw_about(d, tm, li, x);
    gfx_set_translate(0, 0);
    if (d->ask_open) draw_ask(d);
}

static void open_streams_for_video(Detail *d, int video_idx)
{
    const MetaVideo *v = &d->meta.videos[video_idx];
    char title[200];
    if (v->season >= 0 && v->episode >= 0) snprintf(title, sizeof title, "S%d E%d  %s", v->season, v->episode, v->title ? v->title : "");
    else snprintf(title, sizeof title, "%s", v->title ? v->title : "");
    if (app_pip_claim(v->id)) return;   /* already playing in the corner */
    app_push(screen_streams_create(&d->preview, v->id, title));
}

static void focus_ep(Detail *d, int k) { if (k != d->ep_focus) { d->ep_focus = k; d->focus_t = 0; } }

/* ---- options for one episode (hold OK on it) ---- */

enum { EO_THIS, EO_UPTO, EO_FROM, EO_SEASON, EO_SOURCES, EO_SYNOPSIS };
typedef struct { Detail *d; int k; int acts[8]; } EpOpt;

static int resume_index(const Detail *d, const LibraryItem *li)
{
    if (li && li->video_id)
        for (int k = 0; k < d->n_eps; k++) if (strcmp(d->meta.videos[d->ep_index[k]].id, li->video_id) == 0) return k;
    return -1;
}

static void mark_range(Detail *d, int from, int to, int watched)
{
    const char *ids[512];
    int n = 0;
    for (int k = from; k <= to && k < d->n_eps && n < 512; k++) ids[n++] = d->meta.videos[d->ep_index[k]].id;
    app_episode_set_marks(d->preview.id, ids, n, watched);
}

static void ep_choice(int c, void *user)
{
    EpOpt *o = user;
    Detail *d = o->d;
    int k = o->k, act = c >= 0 && c < 8 ? o->acts[c] : -1;
    free(o);
    if (k >= d->n_eps) return;
    const LibraryItem *li = app_library_find(d->preview.id);
    const MetaVideo *v = &d->meta.videos[d->ep_index[k]];
    char what[64];
    if (v->episode >= 0) snprintf(what, sizeof what, "Episode %d", v->episode); else snprintf(what, sizeof what, "This episode");
    switch (act) {
    case EO_THIS: {
        int w = !ep_watched(d, k, li, resume_index(d, li));
        mark_range(d, k, k, w);
        app_toast("%s marked as %swatched", what, w ? "" : "not ");
        break;
    }
    case EO_UPTO: mark_range(d, 0, k, 1); app_toast("%d episode%s marked as watched", k + 1, k ? "s" : ""); break;
    case EO_FROM: mark_range(d, k, d->n_eps - 1, 0); app_toast("%d episode%s marked as unwatched", d->n_eps - k, d->n_eps - k == 1 ? "" : "s"); break;
    case EO_SEASON: {
        int all = 1;
        for (int i = 0; i < d->n_eps; i++) if (!ep_watched(d, i, li, resume_index(d, li))) all = 0;
        mark_range(d, 0, d->n_eps - 1, !all);
        app_toast("Season marked as %swatched", all ? "not " : "");
        break;
    }
    case EO_SOURCES: open_streams_for_video(d, d->ep_index[k]); break;
    case EO_SYNOPSIS: {
        static const char *const close_item[] = { "Close" };
        static const int close_icon[] = { ICON_CLOSE };
        char title[200];
        if (v->season >= 0 && v->episode >= 0) snprintf(title, sizeof title, "S%dE%d  %s", v->season, v->episode, v->title ? v->title : "");
        else snprintf(title, sizeof title, "%s", v->title ? v->title : "Episode");
        app_push(screen_options_create_ex(title, d->preview.name, v->overview, close_item, close_icon, 1, NULL, NULL));
        break;
    }
    default: break;
    }
}

static void episode_options(Detail *d)
{
    int k = d->ep_focus;
    if (k < 0 || k >= d->n_eps) return;
    const LibraryItem *li = app_library_find(d->preview.id);
    int cur = resume_index(d, li), watched = ep_watched(d, k, li, cur), all = 1;
    for (int i = 0; i < d->n_eps; i++) if (!ep_watched(d, i, li, cur)) all = 0;
    const MetaVideo *v = &d->meta.videos[d->ep_index[k]];
    EpOpt *o = calloc(1, sizeof *o);
    o->d = d; o->k = k;
    const char *items[8]; int icons[8], n = 0;
    char upto[64], from[64], season[64];
    #define ADD(act, label, icon) do { o->acts[n] = act; items[n] = label; icons[n] = icon; n++; } while (0)
    ADD(EO_THIS, watched ? "Mark as not watched" : "Mark as watched", watched ? ICON_CLOSE : ICON_CHECK);
    if (k > 0) { snprintf(upto, sizeof upto, "Mark as watched up to here (%d)", k + 1); ADD(EO_UPTO, upto, ICON_NEXT); }
    /* always offered: it is also how to undo a resume point that marked too much */
    snprintf(from, sizeof from, d->n_eps - k == 1 ? "Mark as unwatched from here on (1 episode)" : "Mark as unwatched from here on (%d)", d->n_eps - k);
    ADD(EO_FROM, from, ICON_RELOAD);
    snprintf(season, sizeof season, all ? "Whole season not watched" : "Whole season watched (%d episodes)", d->n_eps);
    ADD(EO_SEASON, season, ICON_LIST);
    ADD(EO_SOURCES, "Sources for this episode", ICON_SWAP);
    if (v->overview && *v->overview) ADD(EO_SYNOPSIS, "Read synopsis", ICON_INFO);
    #undef ADD
    char title[200], sub[200];
    if (v->season >= 0 && v->episode >= 0) snprintf(title, sizeof title, "S%dE%d  %s", v->season, v->episode, v->title ? v->title : "");
    else snprintf(title, sizeof title, "%s", v->title ? v->title : "Episode");
    snprintf(sub, sizeof sub, "%s%s", d->preview.name ? d->preview.name : "", watched ? "  \xC2\xB7  watched" : "");
    app_push(screen_options_create_ex(title, sub, NULL, items, icons, n, ep_choice, o));
}

static int about_key(Detail *d, KeyAction a);

static int key(Screen *s, KeyAction a)
{
    Detail *d = s->st;
    app_redraw();
    int series = d->meta.n_videos > 0;
    if (d->ask_open) {
        if (a == KEY_BACK || a == KEY_OK || a == KEY_LONG_OK) d->ask_open = 0;
        return 1;
    }
    if (a == KEY_LONG_OK || a == KEY_YELLOW || a == KEY_MENU || a == KEY_INFO) {
        if (d->area == AREA_EPISODES && d->n_eps) episode_options(d);
        else if (d->area == AREA_SIMILAR && d->sim_focus < d->n_similar) screen_options_for_title(&d->similar[d->sim_focus]);
        else screen_options_for_title(&d->preview);
        return 1;
    }
    if (d->area >= AREA_ABOUT) return about_key(d, a);
    if (d->btn >= n_buttons_d(d)) d->btn = n_buttons_d(d) - 1;
    switch (d->area) {
    case AREA_ACTIONS:
        switch (a) {
        case KEY_LEFT: if (d->btn > 0) d->btn--; return 1;
        case KEY_RIGHT: if (d->btn < n_buttons_d(d) - 1) d->btn++; return 1;
        case KEY_DOWN:
            if (series) { d->area = d->n_seasons > 1 ? AREA_SEASONS : AREA_EPISODES; d->focus_t = 0; return 1; }
            if (d->has_meta) { d->area = AREA_ABOUT; return 1; }
            return 0;
        case KEY_OK:
            if (btn_at(d, d->btn) == BTN_LIBRARY) { app_library_toggle(&d->preview); return 1; }
            if (btn_at(d, d->btn) == BTN_TRAILER) { trailer_play(d); return 1; }
            if (btn_at(d, d->btn) == BTN_RESTART) {   /* the sources for the resumed video, played from 0:00 */
                const char *vid = d->resume_video[0] ? d->resume_video : d->meta.default_video_id ? d->meta.default_video_id : d->preview.id;
                if (app_pip_claim(vid)) return 1;
                app_push(screen_streams_create(&d->preview, vid, NULL));
                return 1;
            }
            if (d->resume_label[0]) {
                if (app_pip_claim(d->resume_video)) return 1;
                if (!d->resume_url[0]) { app_push(screen_streams_create(&d->preview, d->resume_video, NULL)); return 1; }   /* starts at the saved position */
                PlayCandidate *c = calloc(1, sizeof *c);
                c->url = strdup(d->resume_url);
                c->label = strdup("Resume");
                char nid[128] = "", nt[200] = "";
                int has_next = app_next_video(d->preview.id, d->resume_video, nid, sizeof nid, nt, sizeof nt);
                app_push(screen_player_create(&d->preview, d->resume_video, d->preview.name, c, 1, 0, d->resume_pos, has_next ? nid : NULL, has_next ? nt : NULL));
                return 1;
            }
            if (series) { d->area = AREA_EPISODES; d->focus_t = 0; return 1; }
            if (!d->has_meta && d->loading) { app_toast("Still loading details..."); return 1; }
            if (app_pip_claim(d->meta.default_video_id ? d->meta.default_video_id : d->preview.id)) return 1;
            app_push(screen_streams_create(&d->preview, d->meta.default_video_id ? d->meta.default_video_id : d->preview.id, NULL));
            return 1;
        default: return 0;
        }
    case AREA_SEASONS:
        switch (a) {
        case KEY_LEFT: if (d->season_idx > 0) select_season(d, d->season_idx - 1); return 1;
        case KEY_RIGHT: if (d->season_idx < d->n_seasons - 1) select_season(d, d->season_idx + 1); return 1;
        case KEY_UP: d->area = AREA_ACTIONS; return 1;
        case KEY_DOWN: case KEY_OK: if (d->n_eps) { d->area = AREA_EPISODES; d->focus_t = 0; return 1; } return a == KEY_OK;
        case KEY_BACK: d->area = AREA_ACTIONS; return 1;
        default: return 0;
        }
    default:
        switch (a) {
        case KEY_LEFT: if (d->ep_focus > 0) focus_ep(d, d->ep_focus - 1); return 1;
        case KEY_RIGHT: if (d->ep_focus < d->n_eps - 1) focus_ep(d, d->ep_focus + 1); return 1;
        case KEY_UP: d->area = d->n_seasons > 1 ? AREA_SEASONS : AREA_ACTIONS; return 1;
        case KEY_DOWN: d->area = AREA_ABOUT; return 1;
        case KEY_OK: if (d->n_eps) open_streams_for_video(d, d->ep_index[d->ep_focus]); return 1;
        case KEY_BACK: d->area = AREA_ACTIONS; return 1;
        default: return 0;
        }
    }
}

/* Down from the title's buttons or episodes: About, the cast, more like this */
/* A franchise film opens once its IMDb id is known (one lookup, cached). */
typedef struct { Detail *owner; unsigned gen; TmdbPart part; } FranchiseOpen;
static Detail *g_detail_live[8];
static int detail_alive(const Detail *d) { for (int i = 0; i < 8; i++) if (g_detail_live[i] == d) return 1; return 0; }

static void on_franchise_imdb(const char *imdb, void *user)
{
    FranchiseOpen *o = user;
    Detail *d = o->owner;
    if (detail_alive(d) && d->fr_gen == o->gen) {
        d->fr_opening = 0;
        if (imdb && *imdb) {
            MetaPreview mp = { 0 };
            mp.id = (char *)imdb; mp.type = (char *)"movie"; mp.name = o->part.title; mp.poster = o->part.poster[0] ? o->part.poster : NULL;
            LOGI("detail: franchise -> %s (%s)", imdb, o->part.title);
            app_push(screen_detail_create(&mp));
        } else app_toast("Could not open %s", o->part.title);
    }
    free(o);
}

static int about_key(Detail *d, KeyAction a)
{
    const TmdbInfo *tm = tmdb_get(d->preview.id, d->preview.type);
    int n_cast = cast_count(d, tm), series = d->meta.n_videos > 0;
    int up = series && d->n_eps ? AREA_EPISODES : AREA_ACTIONS;
    if (a == KEY_BACK) { d->area = AREA_ACTIONS; return 1; }
    switch (d->area) {
    case AREA_ABOUT:
        switch (a) {
        case KEY_UP: d->area = up; d->focus_t = 0; return 1;
        case KEY_DOWN:
            if (n_cast) { d->area = AREA_CAST; d->focus_t = 0; return 1; }
            if (franchise(d)) { d->area = AREA_FRANCHISE; d->focus_t = 0; return 1; }
            if (d->n_similar) { d->area = AREA_SIMILAR; d->focus_t = 0; return 1; }
            return 0;   /* bottom of the page: main.c may hand focus to the corner player */
        case KEY_OK:
            if (d->preview.description && *d->preview.description) {
                static const char *const close_item[] = { "Close" };
                static const int close_icon[] = { ICON_CLOSE };
                app_push(screen_options_create_ex(d->preview.name, d->preview.release_info, d->preview.description, close_item, close_icon, 1, NULL, NULL));
            }
            return 1;
        default: return 1;
        }
    case AREA_CAST:
        switch (a) {
        case KEY_LEFT: if (d->cast_focus > 0) { d->cast_focus--; d->focus_t = 0; } return 1;
        case KEY_RIGHT: if (d->cast_focus < n_cast - 1) { d->cast_focus++; d->focus_t = 0; } return 1;
        case KEY_UP: d->area = AREA_ABOUT; return 1;
        case KEY_DOWN:
            if (franchise(d)) { d->area = AREA_FRANCHISE; d->focus_t = 0; return 1; }
            if (d->n_similar) { d->area = AREA_SIMILAR; d->focus_t = 0; return 1; }
            return 0;
        case KEY_OK: {   /* their other titles */
            if (d->cast_focus >= n_cast) return 1;
            const char *name, *role, *photo;
            cast_at(d, tm, d->cast_focus, &name, &role, &photo);
            app_push(screen_search_create_person(name, tm && tm->n_cast ? tm->cast[d->cast_focus].person_id : 0));
            return 1;
        }
        default: return 1;
        }
    case AREA_FRANCHISE: {
        const TmdbCollection *fc = franchise(d);
        int n = fc ? fc->n : 0;
        switch (a) {
        case KEY_LEFT: if (d->fr_focus > 0) { d->fr_focus--; d->focus_t = 0; } return 1;
        case KEY_RIGHT: if (d->fr_focus < n - 1) { d->fr_focus++; d->focus_t = 0; } return 1;
        case KEY_UP: d->area = n_cast ? AREA_CAST : AREA_ABOUT; d->focus_t = 0; return 1;
        case KEY_DOWN: if (d->n_similar) { d->area = AREA_SIMILAR; d->focus_t = 0; return 1; } return 0;
        case KEY_OK: {
            if (d->fr_focus >= n || d->fr_opening) return 1;
            const TmdbPart *pt = &fc->parts[d->fr_focus];
            if (tm && pt->tmdb_id == tm->tmdb_id) return 1;   /* already here */
            FranchiseOpen *o = calloc(1, sizeof *o);
            o->owner = d; o->gen = d->fr_gen = d->fr_gen + 1; o->part = *pt;
            d->fr_opening = 1;
            tmdb_imdb_for(pt->tmdb_id, on_franchise_imdb, o);
            return 1;
        }
        default: return 1;
        }
    }
    default:
        switch (a) {
        case KEY_LEFT: if (d->sim_focus > 0) { d->sim_focus--; d->focus_t = 0; } return 1;
        case KEY_RIGHT: if (d->sim_focus < d->n_similar - 1) { d->sim_focus++; d->focus_t = 0; } return 1;
        case KEY_UP: d->area = franchise(d) ? AREA_FRANCHISE : n_cast ? AREA_CAST : AREA_ABOUT; d->focus_t = 0; return 1;
        case KEY_DOWN: return 0;
        case KEY_OK:
            if (d->sim_focus >= d->n_similar) return 1;
            LOGI("detail: more like this -> %s", d->similar[d->sim_focus].id);
            app_push(screen_detail_create(&d->similar[d->sim_focus]));
            return 1;
        default: return 1;
        }
    }
}

static void resume(Screen *s)
{
    Detail *d = s->st;
    int was = btn_at(d, d->btn);   /* a resume point can add "From the start": keep the same button */
    load_resume(d);
    for (int i = 0; i < n_buttons_d(d); i++) if (btn_at(d, i) == was) { d->btn = i; break; }
    app_redraw();
}

static void destroy(Screen *s)
{
    Detail *d = s->st;
    if (d->req) http_cancel(d->req);
    if (d->ask_req) ai_cancel(d->ask_req);
    if (d->sim_req) http_cancel(d->sim_req);
    meta_previews_free(d->similar, d->n_similar);
    for (int i = 0; i < 8; i++) if (g_detail_live[i] == d) g_detail_live[i] = NULL;
    ask_clear(d);
    meta_preview_free(&d->preview);
    if (d->has_meta) meta_item_free(&d->meta);
    free(d->seasons); free(d->ep_index);
    free(d);
    free(s);
}

/* "Ask AI" from the options sheet: on this page when it is the title's, else a new page. */
void screen_detail_ask(const MetaPreview *p)
{
    Screen *top = app_top();
    if (!top || strcmp(top->name, "detail") != 0 || strcmp(((Detail *)top->st)->preview.id, p->id) != 0) {
        top = screen_detail_create(p);
        app_push(top);
    }
    ask_open(top->st);
}

Screen *screen_detail_create(const MetaPreview *p)
{
    Screen *s = calloc(1, sizeof *s);
    Detail *d = calloc(1, sizeof *d);
    for (int i = 0; i < 8; i++) if (!g_detail_live[i]) { g_detail_live[i] = d; break; }
    meta_preview_copy(&d->preview, p);
    d->focus_t = 1;
    s->name = "detail"; s->st = d; s->long_ok = 1;
    s->update = update; s->draw = draw; s->key = key; s->destroy = destroy; s->resume = resume;
    fetch_meta(d);
    load_resume(d);
    return s;
}
