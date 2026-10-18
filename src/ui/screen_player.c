/* Player: transparent hole for the platform video plane, overlay controls,
 * addon + embedded subtitles, audio/subtitle menu, auto-fallback across stream
 * candidates, intro/recap skipping, Up Next at the credits, and progress sync to
 * the Stremio library. */
#include "../app.h"
#include "../http.h"
#include "../image.h"
#include "../lang.h"
#include "../log.h"
#include "../skip.h"
#include "../store.h"
#include "../subs.h"
#include "../trackmem.h"
#include "../video/video.h"
#include "../platform/platform.h"
#include "font.h"
#include "theme.h"
#include "widgets.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

enum { MAX_ADDON_SUBS = 64, MAX_TRACKS = 16 };
enum { MENU_NONE, MENU_AUDIO, MENU_SUBS };

typedef struct AddonSub { char *url, *lang, *addon; } AddonSub;

typedef struct Player Player;
typedef struct { Player *owner; unsigned gen; int addon; } SubReq;

struct Player {
    MetaPreview meta;
    char *video_id, *title;
    PlayCandidate *cands; int n_cands, cur;
    int tries;
    char *next_video_id, *next_title;

    float overlay_until;
    float since_report;
    double last_reported;
    int finished, up_next_shown, up_next_accepted;
    int seek_steps; float seek_decay;
    double seek_target; float seek_commit;   /* debounced seek while the key repeats */
    float load_started;
    int error_handled;

    /* subtitles */
    AddonSub addon_subs[MAX_ADDON_SUBS]; int n_addon_subs;
    int subs_pending;
    unsigned gen;
    SubTrack track; int track_loaded; int active_addon_sub;   /* -1 none */
    unsigned sub_req;
    int embedded_text_sel;    /* -1 off */
    int auto_selected;        /* audio preference applied */
    int subs_auto_done;       /* subtitle preference applied (or given up) */
    int last_tracks_version;
    int last_warning_version;
    char audio_lang[16]; int audio_lang_known;
    time_t clock_minute;
    int prefetched;           /* next episode's streams requested */
    int pip, pip_done;        /* parked in a corner; finished there */

    /* stats for nerds (Red) */
    int stats;
    int stalls; float stall_s, stall_since; int was_buffering;

    /* skip markers (TheIntroDB / AniSkip) */
    SkipId skip_id;
    SkipSeg skip[SKIP_MAX]; int n_skip;
    int skip_state;           /* 0 not asked, 1 asking, 2 answered (or gave up) */
    unsigned skip_used;       /* bit per segment: skipped or played through */
    double next_at;           /* position where Up Next starts by itself */
    int up_next_dismissed;

    int menu, menu_focus;
    float play_flash;          /* big centre icon after play/pause */
    int flash_icon;
};

static Player *g_live;

static void show_overlay(Player *p) { p->overlay_until = g_app.time + 4.0f; app_redraw(); }
static int error_final(const Player *p, const VideoInfo *vi);

static void report(Player *p, int force)
{
    VideoInfo vi;
    video_info(&vi);
    if (vi.state == VIDEO_IDLE || vi.state == VIDEO_LOADING || vi.state == VIDEO_ERROR) return;
    if (!force && vi.position == p->last_reported) return;
    p->last_reported = vi.position;
    app_library_progress(&p->meta, p->video_id, vi.position, vi.duration);
}

/* ---- addon subtitles ---------------------------------------------------------- */

static void on_addon_subs(HttpResponse *r, void *user)
{
    SubReq *q = user;
    Player *p = q->owner;
    if (p != g_live || q->gen != p->gen) { free(q); return; }
    p->subs_pending--;
    Subtitle *subs; int n;
    if (r->status == 200 && addon_parse_subtitles(r->body, &subs, &n) == 0) {
        const char *addon_name = q->addon < g_app.n_addons ? g_app.addons[q->addon].manifest.name : "addon";
        for (int i = 0; i < n && p->n_addon_subs < MAX_ADDON_SUBS; i++) {
            AddonSub *as = &p->addon_subs[p->n_addon_subs++];
            as->url = strdup(subs[i].url);
            as->lang = strdup(subs[i].lang ? subs[i].lang : "und");
            as->addon = strdup(addon_name);
        }
        subtitles_free(subs, n);
        app_redraw();
    }
    free(q);
}

static void query_addon_subs(Player *p)
{
    const PlayCandidate *c = &p->cands[p->cur];
    char extra[700] = "";
    if (c->filename || c->video_size) {
        char size[32];
        snprintf(size, sizeof size, "%llu", (unsigned long long)c->video_size);
        const char *kv[6]; int k = 0;
        if (c->filename) { kv[k++] = "filename"; kv[k++] = c->filename; }
        if (c->video_size) { kv[k++] = "videoSize"; kv[k++] = size; }
        kv[k] = NULL;
        addon_extra_query(extra, sizeof extra, kv);
    }
    for (int i = 0; i < g_app.n_addons; i++) {
        if (!addon_supports(&g_app.addons[i], "subtitles", p->meta.type, p->video_id)) continue;
        char url[1400];
        addon_url_subtitles(&g_app.addons[i], p->meta.type, p->video_id, extra[0] ? extra : NULL, url, sizeof url);
        SubReq *q = malloc(sizeof *q);
        q->owner = p; q->gen = p->gen; q->addon = i;
        p->subs_pending++;
        http_request_async(url, NULL, on_addon_subs, q);
    }
}

static void on_sub_file(HttpResponse *r, void *user)
{
    SubReq *q = user;
    Player *p = q->owner;
    if (p != g_live || q->gen != p->gen) { free(q); return; }
    p->sub_req = 0;
    subs_free(&p->track);
    p->track_loaded = 0;
    if (r->status == 200 && subs_parse(r->body, r->len, &p->track) > 0) {
        p->track_loaded = 1;
        LOGI("subs: loaded %d cues", p->track.n);
    } else {
        app_toast("Could not load that subtitle file");
        p->active_addon_sub = -1;
    }
    app_redraw();
    free(q);
}

static void select_addon_sub(Player *p, int idx)
{
    p->active_addon_sub = idx;
    p->embedded_text_sel = -1;
    video_select_track(TRACK_TEXT, -1);   /* avoid double subtitles */
    subs_free(&p->track);
    p->track_loaded = 0;
    if (idx < 0) return;
    SubReq *q = malloc(sizeof *q);
    q->owner = p; q->gen = p->gen; q->addon = -1;
    p->sub_req = http_request_async(p->addon_subs[idx].url, NULL, on_sub_file, q);
}

/* Series remember their own track choices; movies use the global preferences. */
static const char *memory_key(const Player *p)
{
    return p->meta.type && strcmp(p->meta.type, "movie") ? p->meta.id : NULL;
}

/* The selected audio track's language, or NULL while unknown. */
static const char *current_audio_lang(VideoTrack *tr, int n)
{
    for (int i = 0; i < n; i++) if (tr[i].selected) return lang_known(tr[i].lang) ? tr[i].lang : NULL;
    return n > 0 && lang_known(tr[0].lang) ? tr[0].lang : NULL;
}

/* Turn on subtitles in `lang`: embedded first unless the remembered choice was
 * an addon file (or the other way round). 1 when something was picked. */
static int pick_subs(Player *p, const char *lang, SubsSource prefer)
{
    VideoTrack tr[MAX_TRACKS];
    int nt = video_tracks(TRACK_TEXT, tr, MAX_TRACKS);
    /* a remembered addon file: give the addons their 15 s before settling for an embedded track */
    int passes = prefer == SUBS_SRC_ADDON && p->subs_pending > 0 && g_app.time - p->load_started <= 15 ? 1 : 2;
    for (int pass = 0; pass < passes; pass++) {
        int embedded = (pass == 0) != (prefer == SUBS_SRC_ADDON);
        if (embedded) {
            for (int i = 0; i < nt; i++)
                if (lang_same(tr[i].lang, lang)) {
                    select_addon_sub(p, -1);
                    video_select_track(TRACK_TEXT, tr[i].index);
                    p->embedded_text_sel = tr[i].index;
                    return 1;
                }
        } else {
            for (int i = 0; i < p->n_addon_subs; i++)
                if (lang_same(p->addon_subs[i].lang, lang)) { select_addon_sub(p, i); return 1; }
        }
    }
    return 0;
}

/* Applied once per stream, as soon as the pipeline reports tracks:
 *   audio      remembered for this series, else the preferred language
 *   subtitles  remembered for this series (including "off"); else the preferred
 *              language, unless the audio already is that language and subtitles
 *              are set to "only for other languages". Addon subtitles can arrive
 *              late, so a missing language is retried for up to 15 s. */
static void auto_select_tracks(Player *p)
{
    VideoTrack tr[MAX_TRACKS];
    TrackMem mem;
    int has_mem = trackmem_get(memory_key(p), &mem);
    int n = video_tracks(TRACK_AUDIO, tr, MAX_TRACKS);
    if (!p->auto_selected) {
        if (n == 0 && g_app.time - p->load_started <= 10) return;   /* wait for the track list */
        p->auto_selected = 1;
        const char *want = has_mem && mem.audio[0] ? mem.audio : g_app.pref_audio;
        if (want[0])
            for (int i = 0; i < n; i++)
                if (lang_same(tr[i].lang, want)) {
                    if (!tr[i].selected) { video_select_track(TRACK_AUDIO, tr[i].index); for (int k = 0; k < n; k++) tr[k].selected = k == i; }
                    break;
                }
        const char *audio = current_audio_lang(tr, n);
        LOGI("player: audio %s (%s)", audio ? audio : "unknown", has_mem && mem.audio[0] ? "remembered for this series" : "preference");
        p->audio_lang_known = audio != NULL;
        snprintf(p->audio_lang, sizeof p->audio_lang, "%s", audio ? audio : "");
    }
    if (p->subs_auto_done) return;
    if (has_mem && mem.has_subs) {
        if (!mem.subs[0] || pick_subs(p, mem.subs, mem.subs_src)) {
            LOGI("player: subtitles %s (remembered for this series)", mem.subs[0] ? mem.subs : "off");
            p->subs_auto_done = 1;
            return;
        }
    } else {
        if (!g_app.pref_subs[0]) { p->subs_auto_done = 1; return; }
        if (!g_app.subs_always && p->audio_lang_known && lang_same(p->audio_lang, g_app.pref_subs)) {
            LOGI("player: subtitles off, the audio is already %s", p->audio_lang);
            p->subs_auto_done = 1;
            return;
        }
        if (pick_subs(p, g_app.pref_subs, SUBS_SRC_EMBEDDED)) { LOGI("player: subtitles %s (preference)", g_app.pref_subs); p->subs_auto_done = 1; return; }
    }
    if (p->subs_pending == 0 || g_app.time - p->load_started > 15) p->subs_auto_done = 1;
}

/* Remember what the viewer picked in the menu for the rest of the series. */
static void remember_audio(Player *p, const VideoTrack *t)
{
    if (!memory_key(p) || !lang_known(t->lang)) return;
    trackmem_set(memory_key(p), t->lang, NULL, SUBS_SRC_ANY);
    snprintf(p->audio_lang, sizeof p->audio_lang, "%s", t->lang);
    p->audio_lang_known = 1;
}

static void remember_subs(Player *p, const char *lang, SubsSource src)
{
    if (!memory_key(p) || (lang && !lang_known(lang))) return;
    trackmem_set(memory_key(p), NULL, lang ? lang : "", src);
}

/* ---- skip markers ------------------------------------------------------------- */

static void skip_done(Player *p, int n, const char *from)
{
    p->n_skip = n > 0 ? n : 0;
    p->skip_state = 2;
    LOGI("player: %d skip marker%s from %s for %s", p->n_skip, p->n_skip == 1 ? "" : "s", from, p->video_id);
    for (int i = 0; i < p->n_skip; i++)
        LOGI("player: marker %d %.0f-%.0fs", (int)p->skip[i].kind, p->skip[i].start, p->skip[i].end);
    app_redraw();
}

static void on_introdb(HttpResponse *r, void *user)
{
    SubReq *q = user;
    Player *p = q->owner;
    if (p != g_live || q->gen != p->gen) { free(q); return; }
    free(q);
    int n = 0;
    if (r->status == 200 && r->body) {
        n = skip_parse_introdb(r->body, p->skip_id.season, p->skip_id.episode, p->skip, SKIP_MAX);
        if (n < 0) LOGW("player: TheIntroDB answered for another episode, markers ignored");
    } else if (r->status != 204 && r->status != 404) {
        LOGW("player: TheIntroDB status %ld %s", r->status, r->error);
    }
    skip_done(p, n, "TheIntroDB");
}

static void on_aniskip(HttpResponse *r, void *user)
{
    SubReq *q = user;
    Player *p = q->owner;
    if (p != g_live || q->gen != p->gen) { free(q); return; }
    free(q);
    VideoInfo vi;
    video_info(&vi);
    int n = 0;
    if (r->status == 200 && r->body) n = skip_parse_aniskip(r->body, vi.duration, p->skip, SKIP_MAX);
    else if (r->status != 404) LOGW("player: AniSkip status %ld %s", r->status, r->error);
    skip_done(p, n, "AniSkip");
}

static void ask_aniskip(Player *p, long mal)
{
    VideoInfo vi;
    video_info(&vi);
    char url[400];
    skip_url_aniskip(mal, p->skip_id.episode, vi.duration, url, sizeof url);
    SubReq *q = malloc(sizeof *q);
    q->owner = p; q->gen = p->gen; q->addon = -1;
    http_request_async(url, NULL, on_aniskip, q);
}

static void on_kitsu(HttpResponse *r, void *user)
{
    SubReq *q = user;
    Player *p = q->owner;
    if (p != g_live || q->gen != p->gen) { free(q); return; }
    free(q);
    long mal = r->status == 200 && r->body ? skip_parse_kitsu_mal(r->body) : 0;
    if (mal > 0) ask_aniskip(p, mal);
    else skip_done(p, 0, "Kitsu (no MyAnimeList id)");
}

/* Once per episode, when the duration is known (the services match markers to
 * the cut by its length) or after 10 s without one. */
static void skip_fetch(Player *p, const VideoInfo *vi)
{
    if (p->skip_state || (g_app.skip_mode == 2 && !p->next_video_id)) return;
    if (vi->state != VIDEO_PLAYING && vi->state != VIDEO_PAUSED) return;
    if (vi->duration <= 0 && g_app.time - p->load_started < 10) return;
    p->skip_state = 1;
    if (skip_parse_video_id(p->video_id, &p->skip_id) != 0) { p->skip_state = 2; return; }
    char url[400];
    SubReq *q = malloc(sizeof *q);
    q->owner = p; q->gen = p->gen; q->addon = -1;
    switch (p->skip_id.src) {
    case SKIP_SRC_IMDB:
        skip_url_introdb(&p->skip_id, vi->duration, url, sizeof url);
        http_request_async(url, NULL, on_introdb, q);
        break;
    case SKIP_SRC_KITSU:
        skip_url_kitsu_mappings(p->skip_id.id, url, sizeof url);
        http_request_async(url, NULL, on_kitsu, q);
        break;
    default:
        free(q);
        ask_aniskip(p, atol(p->skip_id.id));
        break;
    }
}

/* The intro or recap the playhead is in, if it can still be skipped. */
static const SkipSeg *skippable(Player *p, const VideoInfo *vi)
{
    if (g_app.skip_mode == 2 || (vi->state != VIDEO_PLAYING && vi->state != VIDEO_PAUSED)) return NULL;
    for (int i = 0; i < p->n_skip; i++) {
        const SkipSeg *s = &p->skip[i];
        if ((s->kind != SKIP_INTRO && s->kind != SKIP_RECAP) || s->end <= 0 || (p->skip_used & (1u << i))) continue;
        if (vi->position >= s->start && vi->position < s->end - 1.5) return s;
        if (vi->position >= s->end) p->skip_used |= 1u << i;   /* played through it */
    }
    return NULL;
}

static void do_skip(Player *p, const SkipSeg *s)
{
    p->skip_used |= 1u << (int)(s - p->skip);
    LOGI("player: skipping %s %.0f-%.0fs", s->kind == SKIP_RECAP ? "recap" : "intro", s->start, s->end);
    video_seek(s->end);
    if (g_app.skip_mode == 1) app_toast(s->kind == SKIP_RECAP ? "Skipped the recap" : "Skipped the intro");
    app_redraw();
}

/* ---- playback / fallback ------------------------------------------------------ */

static void start_candidate(Player *p, int idx, double start)
{
    p->cur = idx;
    p->error_handled = 0;
    p->auto_selected = 0;
    p->subs_auto_done = 0;
    p->load_started = g_app.time;
    LOGI("player: candidate %d/%d %s @%.0fs -> %s", idx + 1, p->n_cands, p->cands[idx].label ? p->cands[idx].label : "", start, p->cands[idx].url);
    if (video_load(p->cands[idx].url, p->title, start) != 0) app_toast("Could not start playback");
    show_overlay(p);
}

static void fail_over(Player *p, const char *why)
{
    VideoInfo vi;
    video_info(&vi);
    double resume = vi.position > 5 ? vi.position : 0;
    if (p->cur + 1 < p->n_cands && p->tries < 3) {
        p->tries++;
        app_toast("%s. Trying the next source...", why);
        start_candidate(p, p->cur + 1, resume);
    } else {
        p->error_handled = 1;   /* stay on the error screen */
    }
}

static void go_up_next(Player *p)
{
    report(p, 1);
    video_stop();
    MetaPreview meta;
    meta_preview_copy(&meta, &p->meta);
    char *vid = strdup(p->next_video_id), *title = p->next_title ? strdup(p->next_title) : NULL;
    char *group = p->cands[p->cur].binge_group ? strdup(p->cands[p->cur].binge_group) : NULL;
    p->finished = 1;
    app_pop();   /* destroys this screen */
    app_replace(screen_streams_create_auto(&meta, vid, title, group));
    meta_preview_free(&meta);
    free(vid); free(title); free(group);
}

static void update(Screen *s, float dt)
{
    Player *p = s->st;
    if (p->finished) return;
    VideoInfo vi;
    video_info(&vi);
    if (vi.state == VIDEO_PLAYING) app_redraw();   /* progress bar / subtitles move */
    else if (g_app.clock_mode != 2 && time(NULL) / 60 != p->clock_minute) { p->clock_minute = time(NULL) / 60; app_redraw(); }
    p->since_report += dt;
    if (p->since_report > 30.0f && vi.state == VIDEO_PLAYING) { p->since_report = 0; report(p, 0); }
    p->seek_decay -= dt;
    if (p->seek_decay <= 0) p->seek_steps = 0;
    if (p->seek_commit > 0) { p->seek_commit -= dt; if (p->seek_commit <= 0) video_seek(p->seek_target); }

    if (vi.tracks_version != p->last_tracks_version) { p->last_tracks_version = vi.tracks_version; app_redraw(); }
    if (vi.warning_version != p->last_warning_version) {
        p->last_warning_version = vi.warning_version;
        if (vi.warning[0]) app_toast("%s", vi.warning);
    }
    if (vi.state == VIDEO_PLAYING || vi.state == VIDEO_PAUSED) auto_select_tracks(p);

    if (vi.state == VIDEO_ERROR && !p->error_handled) { p->error_handled = 1; fail_over(p, vi.error[0] ? vi.error : "Playback failed"); }
    if (vi.state == VIDEO_LOADING && g_app.time - p->load_started > 45.0f && !p->error_handled) { p->error_handled = 1; fail_over(p, "Source is not responding"); }

    skip_fetch(p, &vi);
    if (g_app.skip_mode == 1) { const SkipSeg *sk = skippable(p, &vi); if (sk) do_skip(p, sk); }

    if (p->pip) {   /* no Up Next card in the corner: finish quietly and hand the plane back */
        if (vi.state == VIDEO_ENDED || error_final(p, &vi)) {
            report(p, 1);
            if (vi.state == VIDEO_ENDED) app_toast("Finished: %s", p->title); else app_toast("Couldn't keep playing: %s", vi.error);
            p->finished = 1;
            p->pip_done = 1;
        }
        return;
    }

    /* Up Next: at the credits when the markers say where they start (and the
     * next episode begins 20 s later), else 45 s before the end */
    double credits = skip_credits_start(p->skip, p->n_skip);
    int at_credits = credits > 60 && vi.duration > 0 && credits < vi.duration - 10 && vi.position >= credits && vi.position < vi.duration - 1;
    int near_end = vi.duration > 120 && vi.position > vi.duration - 45;
    if (p->next_video_id && (at_credits || near_end) && !p->up_next_shown && !p->up_next_dismissed) {
        p->up_next_shown = 1;
        p->next_at = at_credits && !near_end ? credits + 20 : vi.duration;
        if (p->next_at > vi.duration) p->next_at = vi.duration;
        LOGI("player: Up Next at %.0fs (%s), plays at %.0fs", vi.position, at_credits ? "credits" : "end", p->next_at);
        show_overlay(p);
    }
    /* fetch the next episode's sources now, so Up Next starts without a wait */
    if (p->next_video_id && !p->prefetched && (p->up_next_shown || (vi.duration > 120 && vi.position > vi.duration - 90))) {
        p->prefetched = 1;
        streams_prefetch(&p->meta, p->next_video_id);
    }
    if (vi.buffering && !p->was_buffering && vi.state != VIDEO_LOADING) { p->stalls++; p->stall_since = g_app.time; }
    if (!vi.buffering && p->was_buffering && p->stall_since > 0) { p->stall_s += g_app.time - p->stall_since; p->stall_since = 0; }
    p->was_buffering = vi.buffering;
    if (p->stats && (int)(g_app.time * 2) != (int)((g_app.time - dt) * 2)) app_redraw();
    if (p->up_next_shown && p->next_video_id && g_app.autoplay_next && p->next_at < vi.duration && vi.position >= p->next_at
        && vi.state == VIDEO_PLAYING) { go_up_next(p); return; }
    if (p->up_next_shown && vi.position < credits - 5 && credits > 60 && !near_end) p->up_next_shown = 0;   /* seeked back */
    if (vi.state == VIDEO_ENDED) {
        if (p->next_video_id && g_app.autoplay_next) { go_up_next(p); return; }
        p->finished = 1;
        report(p, 1);
        app_pop();
    }
}

/* ---- drawing ------------------------------------------------------------------ */

static void draw_subtitle(Player *p, const VideoInfo *vi)
{
    const char *text = NULL;
    if (p->track_loaded) text = subs_at(&p->track, vi->position);
    if (!text) return;
    float bottom = g_app.time < p->overlay_until ? SNTV_UI_H - 250 : SNTV_UI_H - 96;
    ui_subtitle(text, bottom, g_app.subs_size, g_app.subs_style);
}

static void draw_menu(Player *p)
{
    float w = 600, x = SNTV_UI_W - w - 40, y = 40, h = SNTV_UI_H - 80;
    if (!g_app.fx_light) gfx_shadow(x, y, w, h, R_PANEL, 50, RGBA(0, 0, 0, 0xA0));
    gfx_rrect(x, y, w, h, R_PANEL, RGBA(0x14, 0x12, 0x2A, 0xF0));
    gfx_rrect_ring(x, y, w, h, R_PANEL, 1.5f, RGBA(0xFF, 0xFF, 0xFF, 0x1C));
    ui_icon(p->menu == MENU_AUDIO ? ICON_AUDIO : ICON_SUBS, x + 56, y + 62, 40, THEME_TEXT);
    font_draw(p->menu == MENU_AUDIO ? "Audio" : "Subtitles", x + 92, y + 40, FONT_L | FONT_B, THEME_TEXT);
    float ry = y + 120;
    int row = 0;
    VideoTrack tr[MAX_TRACKS];
    int n = video_tracks(p->menu == MENU_AUDIO ? TRACK_AUDIO : TRACK_TEXT, tr, MAX_TRACKS);
    #define SECTION(t) do { font_draw(t, x + 40, ry + 6, FONT_XS | FONT_B, THEME_TEXT_MUTED); ry += 40; } while (0)
    #define ROW(label, sub, on) do { \
        int foc_ = p->menu_focus == row; const char *sub_ = (sub); \
        if (ry + 70 < y + h - 70) { \
        if (foc_) gfx_rrect(x + 24, ry, w - 48, 70, 18, THEME_TEXT); \
        else if (on) gfx_rrect(x + 24, ry, w - 48, 70, 18, RGBA(0xFF, 0xFF, 0xFF, 0x14)); \
        Color fg_ = foc_ ? THEME_ON_LIGHT : THEME_TEXT; \
        font_draw_fit(label, x + 50, ry + (sub_[0] ? 10 : 20), FONT_S | FONT_B, fg_, w - 190); \
        if (sub_[0]) font_draw_fit(sub_, x + 50, ry + 40, FONT_XS, foc_ ? RGBA(0x0B, 0x0A, 0x16, 0xB0) : THEME_TEXT_MUTED, w - 190); \
        if (on) { gfx_circle(x + w - 70, ry + 35, 17, foc_ ? THEME_ON_LIGHT : THEME_ACCENT); ui_icon(ICON_CHECK, x + w - 70, ry + 35, 20, foc_ ? THEME_TEXT : THEME_TEXT); } } \
        ry += 78; row++; } while (0)
    if (p->menu == MENU_SUBS) ROW("Off", "", p->embedded_text_sel < 0 && p->active_addon_sub < 0);
    if (n) SECTION(p->menu == MENU_AUDIO ? "IN THIS FILE" : "EMBEDDED");
    for (int i = 0; i < n; i++) {
        char sub[80];
        const char *ln = lang_known(tr[i].lang) ? lang_display(tr[i].lang) : "";
        snprintf(sub, sizeof sub, "%s%s%s", tr[i].codec, tr[i].codec[0] && ln[0] ? "  \xC2\xB7  " : "", ln);
        int on = p->menu == MENU_AUDIO ? tr[i].selected : p->embedded_text_sel == tr[i].index;
        ROW(tr[i].label, sub, on);
    }
    if (p->menu == MENU_AUDIO && !n) font_draw_wrapped("The stream reports a single audio track.", x + 40, ry, FONT_S, THEME_TEXT_MUTED, w - 80, 2);
    if (p->menu == MENU_SUBS) {
        if (p->n_addon_subs || p->subs_pending) SECTION(p->subs_pending ? "FROM ADDONS  (SEARCHING)" : "FROM ADDONS");
        for (int i = 0; i < p->n_addon_subs; i++) ROW(lang_display(p->addon_subs[i].lang), p->addon_subs[i].addon, p->active_addon_sub == i);
        if (p->track_loaded) {
            char off[64];
            snprintf(off, sizeof off, "Sync  %+.1fs", p->track.offset);
            gfx_rrect(x + 24, y + h - 76, w - 48, 52, 16, RGBA(0xFF, 0xFF, 0xFF, 0x10));
            ui_icon(ICON_CHEVRON_LEFT, x + 60, y + h - 50, 22, THEME_TEXT_DIM);
            font_draw_center(off, x + w / 2, y + h - 66, FONT_S | FONT_B, THEME_TEXT);
            ui_icon(ICON_CHEVRON_RIGHT, x + w - 60, y + h - 50, 22, THEME_TEXT_DIM);
        }
    }
    #undef ROW
    #undef SECTION
}

static void draw_up_next(Player *p, const VideoInfo *vi, int overlay)
{
    float w = 640, h = 176, x = SNTV_UI_W - THEME_MARGIN - w, y = overlay ? SNTV_UI_H - 400 : SNTV_UI_H - 240;
    if (!g_app.fx_light) gfx_shadow(x, y, w, h, 24, 40, RGBA(0, 0, 0, 0xA0));
    gfx_rrect(x, y, w, h, 24, RGBA(0x16, 0x13, 0x2E, 0xF2));
    gfx_rrect_ring(x, y, w, h, 24, 1.5f, RGBA(0xFF, 0xFF, 0xFF, 0x20));
    const char *thumb = app_video_thumbnail(p->meta.id, p->next_video_id);
    ui_card(thumb, x + 16, y + 16, 256, 144, 0, 0, NULL, CARD_MIP);
    if (!thumb) ui_icon(ICON_NEXT, x + 16 + 128, y + 16 + 72, 50, THEME_TEXT_MUTED);
    float tx = x + 296;
    font_draw("UP NEXT", tx, y + 24, FONT_XS | FONT_B, THEME_ACCENT2);
    font_draw_wrapped(p->next_title ? p->next_title : "Next episode", tx, y + 54, FONT_S | FONT_B, THEME_TEXT, w - 320, 2);
    if (g_app.autoplay_next && vi->duration > 0) {
        double at = p->next_at > 0 ? p->next_at : vi->duration;
        double window = at < vi->duration ? 20.0 : 45.0;
        double left = at - vi->position;
        char t[48]; snprintf(t, sizeof t, "Starts in %ds", (int)(left < 0 ? 0 : left));
        font_draw(t, tx, y + h - 54, FONT_XS, THEME_TEXT_DIM);
        ui_progress(tx, y + h - 22, w - 320, 6, (float)(1.0 - left / window), THEME_ACCENT);
    }
    float gx = x + w - 140;
    gfx_rrect(gx, y + h - 64, 120, 40, 20, RGBA(0x22, 0xC5, 0x5E, 0xFF));
    font_draw_center("Play now", gx + 60, y + h - 64 + (40 - font_height(FONT_XS | FONT_B)) / 2, FONT_XS | FONT_B, THEME_ON_LIGHT);
}

static void draw_skip_button(Player *p, const SkipSeg *s, const VideoInfo *vi, int overlay)
{
    (void)p;
    const char *label = s->kind == SKIP_RECAP ? "Skip recap" : "Skip intro";
    float h = 72, w = ui_pill_width(label, 1, h);
    float x = SNTV_UI_W - THEME_MARGIN - w, y = overlay ? SNTV_UI_H - 330 : SNTV_UI_H - 200;
    if (!g_app.fx_light) gfx_shadow(x, y, w, h, h / 2, 30, RGBA(0, 0, 0, 0x90));
    ui_pill(label, 1, ICON_NEXT, x, y, h, PILL_PRIMARY, 1);
    /* time left in the segment, as a thin line under the button */
    float k = (float)((vi->position - s->start) / (s->end - s->start));
    if (k < 0) k = 0;
    if (k > 1) k = 1;
    gfx_rrect(x + h / 2, y + h + 10, (w - h) * (1 - k), 4, 2, RGBA(0xFF, 0xFF, 0xFF, 0x90));
    font_draw("OK", x - font_measure("OK", FONT_XS | FONT_B) - 16, y + (h - font_height(FONT_XS | FONT_B)) / 2, FONT_XS | FONT_B, THEME_TEXT_DIM);
}

static void url_host(const char *url, char *out, size_t n)
{
    const char *s = strstr(url, "://");
    s = s ? s + 3 : url;
    size_t i = 0;
    while (s[i] && s[i] != '/' && s[i] != ':' && s[i] != '?' && i + 1 < n) { out[i] = s[i]; i++; }
    out[i] = 0;
}

static void draw_stats(Player *p, const VideoInfo *vi)
{
    char host[128], l[12][160];
    int n = 0;
    url_host(p->cands[p->cur].url, host, sizeof host);
    snprintf(l[n++], 160, "Source|%s", p->cands[p->cur].label ? p->cands[p->cur].label : "");
    snprintf(l[n++], 160, "Host|%.80s  \xC2\xB7  candidate %d of %d, %d retr%s", host, p->cur + 1, p->n_cands, p->tries, p->tries == 1 ? "y" : "ies");
    if (vi->width) snprintf(l[n++], 160, "Video|%s  \xC2\xB7  %dx%d @ %d fps", vi->codec, vi->width, vi->height, vi->fps);
    else snprintf(l[n++], 160, "Video|%s", vi->codec[0] ? vi->codec : "not reported yet");
    if (vi->bitrate > 0) snprintf(l[n++], 160, "Bitrate|%.1f Mbit/s", vi->bitrate / 1e6);
    snprintf(l[n++], 160, "Buffer|%d%%%s  \xC2\xB7  %d stall%s, %.1f s total", vi->buffer_percent, vi->buffering ? " (buffering now)" : "",
             p->stalls, p->stalls == 1 ? "" : "s", p->stall_s + (p->stall_since > 0 ? g_app.time - p->stall_since : 0));
    char a[32], b[32];
    ui_format_time(vi->position, a, sizeof a);
    ui_format_time(vi->duration, b, sizeof b);
    snprintf(l[n++], 160, "Position|%s / %s", a, vi->duration > 0 ? b : "unknown");
    if (p->active_addon_sub >= 0) snprintf(l[n++], 160, "Subtitles|%s from %s, %d cues, offset %+.1f s", p->addon_subs[p->active_addon_sub].lang, p->addon_subs[p->active_addon_sub].addon, p->track.n, p->track.offset);
    else if (p->embedded_text_sel >= 0) snprintf(l[n++], 160, "Subtitles|embedded track %d", p->embedded_text_sel);
    else snprintf(l[n++], 160, "Subtitles|off");
    snprintf(l[n++], 160, "Audio|%s", p->audio_lang[0] ? p->audio_lang : "default track");
    snprintf(l[n++], 160, "Skip markers|%d%s", p->n_skip, p->skip_state == 1 ? " (asking)" : p->skip_state == 0 ? " (not asked yet)" : "");
    snprintf(l[n++], 160, "App|%s  \xC2\xB7  %d draw calls  \xC2\xB7  %zu MB artwork", video_backend_name(), gfx_draw_calls(), image_cache_bytes() >> 20);
    if (g_app.frames_measured) snprintf(l[n++], 160, "UI frame|%.1f ms average, %.0f ms worst", g_app.frame_ms_sum / g_app.frames_measured, g_app.frame_ms_worst);

    float w = 980, lh = 36, h = 70 + n * lh, x = THEME_MARGIN, y = 190;
    gfx_rrect(x, y, w, h, 20, RGBA(0x08, 0x07, 0x12, 0xD8));
    gfx_rrect_ring(x, y, w, h, 20, 1.5f, RGBA(0xFF, 0xFF, 0xFF, 0x20));
    font_draw("Stats for nerds", x + 28, y + 20, FONT_S | FONT_B, THEME_ACCENT2);
    font_draw("Red to close", x + w - 28 - font_measure("Red to close", FONT_XS), y + 24, FONT_XS, THEME_TEXT_MUTED);
    for (int i = 0; i < n; i++) {
        char *bar = strchr(l[i], '|');
        if (!bar) continue;
        *bar = 0;
        float ly = y + 62 + i * lh;
        font_draw(l[i], x + 28, ly, FONT_XS | FONT_B, THEME_TEXT_MUTED);
        font_draw_fit(bar + 1, x + 190, ly, FONT_XS, THEME_TEXT, w - 220);
    }
}

/* Wall-clock time `from_now` seconds ahead, in the chosen 12/24-hour format. */
static void clock_text(double from_now, char *out, size_t n)
{
    time_t t = time(NULL) + (time_t)(from_now > 0 ? from_now : 0);
    struct tm tmv;
    if (!localtime_r(&t, &tmv)) { snprintf(out, n, "--:--"); return; }
    if (g_app.clock_mode == 1) snprintf(out, n, "%02d:%02d", tmv.tm_hour, tmv.tm_min);
    else snprintf(out, n, "%d:%02d %s", tmv.tm_hour % 12 ? tmv.tm_hour % 12 : 12, tmv.tm_min, tmv.tm_hour < 12 ? "AM" : "PM");
}

/* Where the video shows: a hole for the hardware plane on TVs; on the desktop
 * preview the title's backdrop stands in for it. */
static void draw_video_area(Player *p, float x, float y, float w, float h)
{
#ifdef SNTV_PLATFORM_LINUX
    char buf[512];
    const char *bg = app_art_background(&p->meta, buf, sizeof buf);
    int iw, ih;
    unsigned bgt = bg ? image_get(bg, &iw, &ih) : 0;
    gfx_rect(x, y, w, h, RGBA(0, 0, 0, 0xFF));
    if (bgt) {
        float ia = (float)iw / ih, sa = w / h, u0 = 0, v0 = 0, u1 = 1, v1 = 1;
        if (ia > sa) { float k = sa / ia; u0 = (1 - k) / 2; u1 = 1 - u0; } else { float k = ia / sa; v0 = (1 - k) / 2; v1 = 1 - v0; }
        gfx_tex_uv(bgt, x, y, w, h, u0, v0, u1, v1, RGBA(255, 255, 255, 255));
    }
#else
    (void)p;
    gfx_video_hole(x, y, w, h);
#endif
}

/* every candidate has failed and the error card is up */
static int error_final(const Player *p, const VideoInfo *vi)
{
    return vi->state == VIDEO_ERROR && p->error_handled && (p->cur + 1 >= p->n_cands || p->tries >= 3);
}

static void draw(Screen *s)
{
    Player *p = s->st;
    VideoInfo vi;
    video_info(&vi);
    draw_video_area(p, 0, 0, SNTV_UI_W, SNTV_UI_H);
    draw_subtitle(p, &vi);

    int overlay = g_app.time < p->overlay_until || vi.state == VIDEO_PAUSED || vi.state == VIDEO_LOADING;
    if (error_final(p, &vi)) {
        gfx_rect(0, 0, SNTV_UI_W, SNTV_UI_H, RGBA(0x08, 0x07, 0x12, 0xD8));
        float w = 900, h = 360, x = (SNTV_UI_W - w) / 2, y = (SNTV_UI_H - h) / 2;
        gfx_rrect(x, y, w, h, R_PANEL, THEME_PANEL);
        gfx_circle(x + w / 2, y + 70, 36, COLOR_ALPHA(THEME_DANGER, 0x40));
        gfx_line(x + w / 2 - 12, y + 58, x + w / 2 + 12, y + 82, 6, THEME_DANGER);
        gfx_line(x + w / 2 + 12, y + 58, x + w / 2 - 12, y + 82, 6, THEME_DANGER);
        font_draw_center("This source can't be played", x + w / 2, y + 128, FONT_L | FONT_B, THEME_TEXT);
        font_draw_wrapped(vi.error, x + 60, y + 190, FONT_S, THEME_TEXT_DIM, w - 120, 2);
        font_draw_center(p->n_cands > 1 ? "Every candidate failed. Press Back to pick another source." : "Press Back to pick another source.",
                         x + w / 2, y + h - 86, FONT_S | FONT_B, THEME_TEXT_MUTED);
        font_draw_center("Press OK to see what happened (Diagnostics)", x + w / 2, y + h - 46, FONT_XS, THEME_TEXT_MUTED);
        return;
    }

    if (vi.state == VIDEO_LOADING || vi.buffering) {
        ui_spinner(SNTV_UI_W / 2, SNTV_UI_H / 2, 34);
        const char *what = vi.state == VIDEO_LOADING ? "Starting" : "Buffering";
        char b[48];
        if (vi.buffer_percent > 0 && vi.buffer_percent < 100) snprintf(b, sizeof b, "%s  %d%%", what, vi.buffer_percent);
        else snprintf(b, sizeof b, "%s", what);
        font_draw_shadow(b, SNTV_UI_W / 2 - font_measure(b, FONT_S | FONT_B) / 2, SNTV_UI_H / 2 + 60, FONT_S | FONT_B, THEME_TEXT, RGBA(0, 0, 0, 0xA0));
    }

    /* centre play/pause flash */
    float flash = p->play_flash - g_app.time;
    if (vi.state == VIDEO_PAUSED || flash > 0) {
        float a = vi.state == VIDEO_PAUSED ? 1.0f : flash / 0.6f;
        float r = 70 + (1 - a) * 20 * (vi.state != VIDEO_PAUSED);
        gfx_circle(SNTV_UI_W / 2, SNTV_UI_H / 2, r, RGBA(0, 0, 0, (int)(0x80 * a)));
        gfx_rrect_ring(SNTV_UI_W / 2 - r, SNTV_UI_H / 2 - r, 2 * r, 2 * r, r, 2, RGBA(0xFF, 0xFF, 0xFF, (int)(0x40 * a)));
        int icon = vi.state == VIDEO_PAUSED ? ICON_PLAY : p->flash_icon;
        ui_icon((Icon)icon, SNTV_UI_W / 2 + (icon == ICON_PLAY ? 6 : 0), SNTV_UI_H / 2, 64, RGBA(255, 255, 255, (int)(255 * a)));
        if (flash > 0) app_redraw();
    }

    int bar_only = !overlay && p->seek_commit > 0;
    double shown_pos = p->seek_commit > 0 ? p->seek_target : vi.position;
    float bx = THEME_MARGIN, by = SNTV_UI_H - 150, bw = SNTV_UI_W - 2 * THEME_MARGIN;
    if (overlay) {
        gfx_gradient_v(0, SNTV_UI_H - 420, SNTV_UI_W, 420, RGBA(0, 0, 0, 0), RGBA(0, 0, 0, 0xE0));
        gfx_gradient_v(0, 0, SNTV_UI_W, 220, RGBA(0, 0, 0, 0xB0), RGBA(0, 0, 0, 0));
        font_draw_fit(p->title, THEME_MARGIN, 56, FONT_L | FONT_B, THEME_TEXT, SNTV_UI_W - 2 * THEME_MARGIN - 300);
        float cx = THEME_MARGIN;
        if (p->cands[p->cur].label) { font_draw_fit(p->cands[p->cur].label, cx, 114, FONT_S, THEME_TEXT_DIM, 900); cx += font_measure(p->cands[p->cur].label, FONT_S) + 20; }
        if (vi.codec[0] && cx < 1200) ui_chip(vi.codec, cx, 110, RGBA(0xFF, 0xFF, 0xFF, 0x22), THEME_TEXT);
        float chip_y = 62;
        if (g_app.clock_mode != 2) {
            char now[24]; clock_text(0, now, sizeof now);
            font_draw_shadow(now, SNTV_UI_W - THEME_MARGIN - font_measure(now, FONT_L | FONT_B), 50, FONT_L | FONT_B, THEME_TEXT, RGBA(0, 0, 0, 0x80));
            chip_y = 122;
        }
        if (p->n_cands > 1) {
            char c[48]; snprintf(c, sizeof c, "Source %d of %d", p->cur + 1, p->n_cands);
            ui_chip(c, SNTV_UI_W - THEME_MARGIN - font_measure(c, FONT_XS | FONT_B) - 24, chip_y, RGBA(0xFF, 0xFF, 0xFF, 0x22), THEME_TEXT);
        }
    } else if (bar_only) {
        gfx_gradient_v(0, SNTV_UI_H - 240, SNTV_UI_W, 240, RGBA(0, 0, 0, 0), RGBA(0, 0, 0, 0xB0));
    }
    if (overlay || bar_only) {
        float th = p->seek_commit > 0 ? 12 : 8, ty = by - th / 2;
        gfx_rrect(bx, ty, bw, th, th / 2, RGBA(0xFF, 0xFF, 0xFF, 0x30));
        if (vi.duration > 0) {
            if (vi.buffer_percent > 0) gfx_rrect(bx, ty, bw * vi.buffer_percent / 100.0f, th, th / 2, RGBA(0xFF, 0xFF, 0xFF, 0x30));
            for (int i = 0; i < p->n_skip; i++) {   /* intro / credits ranges */
                double e = p->skip[i].end > 0 ? p->skip[i].end : vi.duration;
                if (p->skip[i].kind == SKIP_PREVIEW || e <= p->skip[i].start) continue;
                gfx_rect(bx + bw * (float)(p->skip[i].start / vi.duration), ty + th + 6, bw * (float)((e - p->skip[i].start) / vi.duration), 3,
                         p->skip[i].kind == SKIP_CREDITS ? RGBA(0xFF, 0xFF, 0xFF, 0x50) : COLOR_ALPHA(THEME_ACCENT2, 0xC0));
            }
            float fw = bw * (float)(shown_pos / vi.duration);
            if (fw > th) gfx_rrect_grad_h(bx, ty, fw, th, th / 2, THEME_ACCENT, THEME_ACCENT2);
            float kx = bx + fw;
            if (!g_app.fx_light) gfx_shadow(kx - 14, by - 14, 28, 28, 14, 14, COLOR_ALPHA(THEME_ACCENT2, 0xB0));
            gfx_circle(kx, by, p->seek_commit > 0 ? 16 : 12, THEME_TEXT);
            if (p->seek_commit > 0) {
                char t[32]; ui_format_time(shown_pos, t, sizeof t);
                float tw = font_measure(t, FONT_M | FONT_B) + 36;
                float tx = kx - tw / 2;
                if (tx < bx) tx = bx;
                if (tx + tw > bx + bw) tx = bx + bw - tw;
                gfx_rrect(tx, by - 82, tw, 52, 26, THEME_TEXT);
                font_draw(t, tx + 18, by - 82 + (52 - font_height(FONT_M | FONT_B)) / 2, FONT_M | FONT_B, THEME_ON_LIGHT);
            }
        }
        char pos[32], rem[48], t2[32];
        ui_format_time(shown_pos, pos, sizeof pos);
        ui_format_time(vi.duration - shown_pos, t2, sizeof t2);
        snprintf(rem, sizeof rem, vi.duration > 0 ? "-%s" : "--:--", t2);
        font_draw(pos, bx, by + 24, FONT_S | FONT_B, THEME_TEXT);
        float rw = font_measure(rem, FONT_S | FONT_B);
        font_draw(rem, bx + bw - rw, by + 24, FONT_S | FONT_B, THEME_TEXT_DIM);
        if (g_app.clock_mode != 2 && vi.duration > 0 && overlay) {
            char end[24], ends[40];
            clock_text(vi.duration - shown_pos, end, sizeof end);
            snprintf(ends, sizeof ends, "Ends at %s", end);
            float ew = font_measure(ends, FONT_S | FONT_B);
            font_draw(ends, bx + bw - rw - 28 - ew, by + 24, FONT_S | FONT_B, THEME_TEXT);
            gfx_circle(bx + bw - rw - 14, by + 24 + font_height(FONT_S | FONT_B) / 2, 3, THEME_TEXT_MUTED);
        }
    }
    if (overlay) {
        /* hint row */
        float hx = bx, hy = by + 66;
        struct { Icon ic; const char *t; } hints[] = { { ICON_AUDIO, "Audio" }, { ICON_SUBS, "Subtitles" }, { ICON_SWAP, "Next source" } };
        const char *keys[] = { "Up", "Down", "Yellow" };
        for (int i = 0; i < 3; i++) {
            if (i == 2 && p->n_cands < 2) break;
            float w = font_measure(hints[i].t, FONT_XS | FONT_B) + font_measure(keys[i], FONT_XS) + 90;
            gfx_rrect(hx, hy, w, 40, 20, RGBA(0xFF, 0xFF, 0xFF, 0x18));
            ui_icon(hints[i].ic, hx + 26, hy + 20, 22, THEME_TEXT);
            font_draw(hints[i].t, hx + 48, hy + (40 - font_height(FONT_XS | FONT_B)) / 2, FONT_XS | FONT_B, THEME_TEXT);
            font_draw(keys[i], hx + 58 + font_measure(hints[i].t, FONT_XS | FONT_B), hy + (40 - font_height(FONT_XS)) / 2, FONT_XS, THEME_TEXT_MUTED);
            hx += w + 12;
        }
    }
    if (p->stats && !p->menu) draw_stats(p, &vi);
    if (p->up_next_shown && p->next_video_id && !p->menu) draw_up_next(p, &vi, overlay);
    const SkipSeg *sk = g_app.skip_mode == 0 && !p->menu ? skippable(p, &vi) : NULL;
    if (sk) draw_skip_button(p, sk, &vi, overlay);
    if (p->menu) draw_menu(p);
}

static int menu_rows(Player *p)
{
    VideoTrack tr[MAX_TRACKS];
    int n = video_tracks(p->menu == MENU_AUDIO ? TRACK_AUDIO : TRACK_TEXT, tr, MAX_TRACKS);
    return p->menu == MENU_AUDIO ? n : 1 + n + p->n_addon_subs;
}

static void menu_activate(Player *p)
{
    VideoTrack tr[MAX_TRACKS];
    int n = video_tracks(p->menu == MENU_AUDIO ? TRACK_AUDIO : TRACK_TEXT, tr, MAX_TRACKS);
    int f = p->menu_focus;
    if (p->menu == MENU_AUDIO) { if (f < n) { video_select_track(TRACK_AUDIO, tr[f].index); remember_audio(p, &tr[f]); } return; }
    p->subs_auto_done = 1;   /* the viewer decided; stop auto-selecting */
    if (f == 0) { select_addon_sub(p, -1); p->embedded_text_sel = -1; video_select_track(TRACK_TEXT, -1); remember_subs(p, NULL, SUBS_SRC_ANY); return; }
    f--;
    if (f < n) {
        select_addon_sub(p, -1); p->embedded_text_sel = tr[f].index; video_select_track(TRACK_TEXT, tr[f].index);
        remember_subs(p, tr[f].lang, SUBS_SRC_EMBEDDED);
        return;
    }
    f -= n;
    if (f < p->n_addon_subs) { select_addon_sub(p, f); remember_subs(p, p->addon_subs[f].lang, SUBS_SRC_ADDON); }
}

static void seek_by(Player *p, double base)
{
    VideoInfo vi;
    video_info(&vi);
    p->seek_steps++;
    p->seek_decay = 0.6f;
    double step = base * (p->seek_steps > 8 ? 6 : p->seek_steps > 4 ? 3 : 1);
    double from = p->seek_commit > 0 ? p->seek_target : vi.position;
    double target = from + step;
    if (target < 0) target = 0;
    if (vi.duration > 0 && target > vi.duration - 1) target = vi.duration - 1;
    p->seek_target = target;
    p->seek_commit = 0.35f;   /* coalesce rapid presses into one seek */
    app_redraw();
}

static int key(Screen *s, KeyAction a)
{
    Player *p = s->st;
    VideoInfo vi;
    video_info(&vi);
    if (p->menu) {
        int rows = menu_rows(p);
        switch (a) {
        case KEY_UP: if (p->menu_focus > 0) p->menu_focus--; app_redraw(); return 1;
        case KEY_DOWN: if (p->menu_focus < rows - 1) p->menu_focus++; app_redraw(); return 1;
        case KEY_LEFT: if (p->menu == MENU_SUBS && p->track_loaded) { p->track.offset -= 0.5; app_redraw(); } return 1;
        case KEY_RIGHT: if (p->menu == MENU_SUBS && p->track_loaded) { p->track.offset += 0.5; app_redraw(); } return 1;
        case KEY_OK: menu_activate(p); p->menu = MENU_NONE; app_redraw(); return 1;
        case KEY_BACK: p->menu = MENU_NONE; app_redraw(); return 1;
        default: return 1;
        }
    }
    if (error_final(p, &vi) && a == KEY_OK) { app_push(screen_diagnostics_create(1)); return 1; }
    if (a == KEY_OK && g_app.skip_mode == 0) { const SkipSeg *sk = skippable(p, &vi); if (sk) { do_skip(p, sk); show_overlay(p); return 1; } }
    if (a == KEY_BACK && p->up_next_shown && p->next_video_id) {   /* dismiss the card, keep watching */
        p->up_next_shown = 0; p->up_next_dismissed = 1; app_redraw(); return 1;
    }
    switch (a) {
    case KEY_OK: case KEY_PLAY_PAUSE:
        if (vi.state == VIDEO_PLAYING) video_pause(); else { video_play(); p->flash_icon = ICON_PLAY; p->play_flash = g_app.time + 0.6f; }
        show_overlay(p);
        return 1;
    case KEY_PLAY: video_play(); show_overlay(p); return 1;
    case KEY_PAUSE: video_pause(); show_overlay(p); return 1;
    case KEY_LEFT: case KEY_REWIND: seek_by(p, -10); return 1;
    case KEY_RIGHT: case KEY_FORWARD: seek_by(p, 10); return 1;
    case KEY_UP: p->menu = MENU_AUDIO; p->menu_focus = 0; app_redraw(); return 1;
    case KEY_DOWN: p->menu = MENU_SUBS; p->menu_focus = 0; app_redraw(); return 1;
    case KEY_INFO: case KEY_MENU: show_overlay(p); return 1;
    case KEY_GREEN: if (p->next_video_id) go_up_next(p); return 1;
    case KEY_RED: p->stats = !p->stats; app_redraw(); return 1;
    case KEY_YELLOW: if (p->cur + 1 < p->n_cands) { app_toast("Switching source"); start_candidate(p, p->cur + 1, vi.position); } return 1;
    case KEY_BACK:
        if ((vi.state == VIDEO_PLAYING || vi.state == VIDEO_PAUSED || vi.state == VIDEO_LOADING) && g_app.depth > 1) { app_pip_enter(); return 1; }
        /* fall through */
    case KEY_STOP:
        report(p, 1);
        video_stop();
        p->finished = 1;
        app_pop();
        return 1;
    default: return 0;
    }
}

static void destroy(Screen *s)
{
    Player *p = s->st;
    if (g_live == p) g_live = NULL;
    p->gen++;
    video_stop();
    subs_free(&p->track);
    for (int i = 0; i < p->n_addon_subs; i++) { free(p->addon_subs[i].url); free(p->addon_subs[i].lang); free(p->addon_subs[i].addon); }
    play_candidates_free(p->cands, p->n_cands);
    meta_preview_free(&p->meta);
    free(p->video_id); free(p->title); free(p->next_video_id); free(p->next_title);
    free(p);
    free(s);
}

Screen *screen_player_create(const MetaPreview *meta, const char *video_id, const char *title,
                             PlayCandidate *cands, int n_cands, int start_index, double start_s,
                             const char *next_video_id, const char *next_title)
{
    Screen *s = calloc(1, sizeof *s);
    Player *p = calloc(1, sizeof *p);
    meta_preview_copy(&p->meta, meta);
    p->video_id = strdup(video_id);
    p->title = strdup(title ? title : meta->name);
    p->cands = cands; p->n_cands = n_cands;
    p->next_video_id = next_video_id ? strdup(next_video_id) : NULL;
    p->next_title = next_title ? strdup(next_title) : NULL;
    p->active_addon_sub = -1;
    p->embedded_text_sel = -1;
    p->gen = 1;
    g_live = p;
    s->name = "player"; s->st = p;
    s->update = update; s->draw = draw; s->key = key; s->destroy = destroy;
    start_candidate(p, start_index < n_cands ? start_index : 0, start_s);
    query_addon_subs(p);
    return s;
}

/* ---- picture-in-picture ------------------------------------------------------- */

#define PIP_W 560.0f
#define PIP_H 315.0f

static void pip_rect(float *x, float *y, float *w, float *h)
{
    *w = PIP_W; *h = PIP_H;
    *x = SNTV_UI_W - 72 - PIP_W;
    *y = SNTV_UI_H - 60 - PIP_H;
}

void player_set_pip(Screen *s, int on)
{
    Player *p = s->st;
    p->pip = on;
    p->menu = MENU_NONE;
    p->stats = 0;
    if (on) {
        float x, y, w, h;
        int px, py, pw, ph;
        pip_rect(&x, &y, &w, &h);
        gfx_ui_to_pixels(x, y, w, h, &px, &py, &pw, &ph);
        video_set_display_rect(px, py, pw, ph);
        LOGI("player: picture-in-picture %dx%d at %d,%d", pw, ph, px, py);
    } else {
        int w, h;
        gfx_drawable_size(&w, &h);
        video_set_display_rect(0, 0, w, h);
        show_overlay(p);
        LOGI("player: back to full screen");
    }
}

void player_pip_update(Screen *s, float dt) { update(s, dt); }
int player_pip_done(Screen *s) { return ((Player *)s->st)->pip_done; }
const char *player_video_id(Screen *s) { return ((Player *)s->st)->video_id; }

void player_pip_draw(Screen *s)
{
    Player *p = s->st;
    VideoInfo vi;
    video_info(&vi);
    float x, y, w, h;
    pip_rect(&x, &y, &w, &h);
    if (!g_app.fx_light) gfx_shadow(x, y, w, h, 4, 44, RGBA(0, 0, 0, 0xC0));
    draw_video_area(p, x, y, w, h);
    gfx_rrect_ring(x, y, w, h, 2, 2, RGBA(0xFF, 0xFF, 0xFF, 0x50));
    gfx_gradient_v(x, y + h - 96, w, 96, RGBA(0, 0, 0, 0), RGBA(0, 0, 0, 0xC8));
    font_draw_fit(p->title, x + 18, y + h - 64, FONT_XS | FONT_B, THEME_TEXT, w - 36);
    if (vi.duration > 0) ui_progress(x + 18, y + h - 20, w - 36, 4, (float)(vi.position / vi.duration), THEME_ACCENT);
    float cx = x + w / 2, cy = y + h / 2 - 10;
    if (vi.state == VIDEO_PAUSED) { gfx_circle(cx, cy, 34, RGBA(0, 0, 0, 0x90)); ui_icon(ICON_PLAY, cx + 3, cy, 30, THEME_TEXT); }
    else if (vi.state == VIDEO_LOADING || vi.buffering) ui_spinner(cx, cy, 20);
    ui_chip("Play/Pause: full screen", x + 12, y + 12, RGBA(0, 0, 0, 0x90), THEME_TEXT_DIM);
    if (vi.state == VIDEO_PLAYING) app_redraw();
}
