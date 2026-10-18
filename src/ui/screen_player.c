/* Player: transparent hole for the platform video plane, overlay controls,
 * addon + embedded subtitles, audio/subtitle menu, auto-fallback across stream
 * candidates, intro/recap skipping, Up Next at the credits, and progress sync to
 * the Stremio library. */
#include "../app.h"
#include "../http.h"
#include "../image.h"
#include "../lang.h"
#include "../log.h"
#include "../probe.h"
#include "../tmdb.h"
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
enum { MENU_NONE, MENU_AUDIO, MENU_SUBS, MENU_STYLE, MENU_SOURCES, MENU_EPISODES };
enum { BTN_PREV, BTN_PLAY, BTN_NEXT, BTN_RELOAD, BTN_SUBS, BTN_AUDIO, BTN_ASPECT, BTN_SOURCES, BTN_EPISODES, BTN_STATS, BTN_COUNT };
enum { ASPECT_FIT, ASPECT_FILL, ASPECT_ZOOM, ASPECT_COUNT };
static const char *const ASPECT_NAMES[ASPECT_COUNT] = { "Fit", "Fill the screen", "Zoom" };
static const double RATES[] = { 0.5, 0.75, 1.0, 1.25, 1.5, 1.75, 2.0 };
enum { N_RATES = 7, RATE_NORMAL = 2 };

typedef struct AddonSub { char *url, *lang, *addon; } AddonSub;

typedef struct Player Player;
typedef struct { Player *owner; unsigned gen; int addon; int slot; } SubReq;

struct Player {
    MetaPreview meta;
    char *video_id, *title;
    PlayCandidate *cands; int n_cands, cur;
    int tries;
    char *next_video_id, *next_title;
    char *prev_video_id, *prev_title;

    float overlay_until;
    float paused_since;       /* the paused info panel comes after a moment */
    int rate_idx;             /* index into RATES */
    int rate_applied;         /* the pipeline got the rate for this load */
    int aspect;               /* ASPECT_* */
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
    SubTrack track2; int track2_loaded; int active_addon_sub2;   /* second subtitles, drawn above the main ones */
    int assign_second;        /* the subtitles menu picks for the second slot */
    int forced_checked;       /* looked for a forced track in the audio language */
    float seek_flash; int seek_flash_dir;   /* the ±10 s mark flashes on the side that seeks */
    float menu_t; int menu_seen;           /* the side menu slides in */
    int embedded_text_sel;    /* -1 off */
    int auto_selected;        /* audio preference applied */
    int subs_auto_done;       /* subtitle preference applied (or given up) */
    int last_tracks_version;
    int last_warning_version;
    char audio_lang[16]; int audio_lang_known;
    time_t clock_minute;
    int prefetched;           /* next episode's streams requested */
    float prefetch_tried;     /* when; a stale or empty prefetch is retried after a while */
    float next_prog;          /* shown share of the prefetch (eases toward the real one) */
    float next_ready_at;      /* when it reached 1, for the ready pulse */
    int pip, pip_done;        /* parked in a corner; finished there */
    int learned;              /* this candidate's outcome was recorded in the TV profile */
    ProbeInfo *probe;         /* the file's header: chapters, real codecs */
    int probe_asked;
    float last_live_seek;     /* live scrub: the video follows the key every third of a second */

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
    int bar_focus, bar_index;  /* Down moves from the seek bar to the button row */
    float play_flash;          /* big centre icon after play/pause */
    int flash_icon;
};

static Player *g_live;

static void show_overlay(Player *p) { p->overlay_until = g_app.time + 4.0f; app_redraw(); }
static void url_host(const char *url, char *out, size_t n);
static void go_up_next(Player *p);
static int pick_forced(Player *p);
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
        q->owner = p; q->gen = p->gen; q->addon = i; q->slot = 0;
        p->subs_pending++;
        http_request_async(url, NULL, on_addon_subs, q);
    }
}

static void on_sub_file(HttpResponse *r, void *user)
{
    SubReq *q = user;
    Player *p = q->owner;
    if (p != g_live || q->gen != p->gen) { free(q); return; }
    SubTrack *t = q->slot ? &p->track2 : &p->track;
    int *loaded = q->slot ? &p->track2_loaded : &p->track_loaded;
    if (!q->slot) p->sub_req = 0;
    subs_free(t);
    *loaded = 0;
    if (r->status == 200 && subs_parse(r->body, r->len, t) > 0) {
        *loaded = 1;
        LOGI("subs: loaded %d cues%s", t->n, q->slot ? " (second subtitles)" : "");
    } else {
        app_toast("Could not load that subtitle file");
        if (q->slot) p->active_addon_sub2 = -1; else p->active_addon_sub = -1;
    }
    app_redraw();
    free(q);
}

static void select_addon_sub_slot(Player *p, int idx, int slot)
{
    if (slot) { p->active_addon_sub2 = idx; subs_free(&p->track2); p->track2_loaded = 0; }
    else {
        p->active_addon_sub = idx;
        p->embedded_text_sel = -1;
        video_select_track(TRACK_TEXT, -1);   /* avoid double subtitles */
        subs_free(&p->track);
        p->track_loaded = 0;
    }
    if (idx < 0) return;
    SubReq *q = malloc(sizeof *q);
    q->owner = p; q->gen = p->gen; q->addon = -1; q->slot = slot;
    unsigned req = http_request_async(p->addon_subs[idx].url, NULL, on_sub_file, q);
    if (!slot) p->sub_req = req;
}

static void select_addon_sub(Player *p, int idx) { select_addon_sub_slot(p, idx, 0); }

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
    /* nothing chosen: a forced track in the audio language still goes on, once the header is read */
    #define SUBS_OFF_DONE() do { \
        if (!p->forced_checked) { if (!p->probe && g_app.time - p->load_started < 12) return; p->forced_checked = 1; pick_forced(p); } \
        p->subs_auto_done = 1; return; } while (0)
    if (has_mem && mem.has_subs) {
        if (!mem.subs[0] || pick_subs(p, mem.subs, mem.subs_src)) {
            LOGI("player: subtitles %s (remembered for this series)", mem.subs[0] ? mem.subs : "off");
            if (!mem.subs[0]) SUBS_OFF_DONE();
            p->subs_auto_done = 1;
            return;
        }
    } else {
        if (!g_app.pref_subs[0]) SUBS_OFF_DONE();
        if (!g_app.subs_always && p->audio_lang_known && lang_same(p->audio_lang, g_app.pref_subs)) {
            LOGI("player: subtitles off, the audio is already %s", p->audio_lang);
            SUBS_OFF_DONE();
        }
        if (pick_subs(p, g_app.pref_subs, SUBS_SRC_EMBEDDED)) { LOGI("player: subtitles %s (preference)", g_app.pref_subs); p->subs_auto_done = 1; return; }
    }
    if (p->subs_pending == 0 || g_app.time - p->load_started > 15) p->subs_auto_done = 1;
    #undef SUBS_OFF_DONE
}

/* A forced subtitle track (signs, foreign lines) in the audio's language, when
 * the file header lists one: the k-th text track of the pipeline. 1 when picked. */
static int pick_forced(Player *p)
{
    if (!p->probe || !p->audio_lang_known) return 0;
    VideoTrack tt[MAX_TRACKS];
    int nt = video_tracks(TRACK_TEXT, tt, MAX_TRACKS), k = 0;
    for (int i = 0; i < p->probe->n_tracks; i++) {
        const ProbeTrack *t = &p->probe->tracks[i];
        if (t->kind != 3) continue;
        if (t->forced && lang_same(t->lang, p->audio_lang) && k < nt) {
            p->embedded_text_sel = tt[k].index;
            video_select_track(TRACK_TEXT, tt[k].index);
            LOGI("player: forced subtitles on (%s, track %d)", t->lang, k + 1);
            app_toast("Forced subtitles on");
            return 1;
        }
        k++;
    }
    return 0;
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

/* Addon subtitles grouped by language: one row per language, the files behind it
 * reachable with Left/Right. Preferred language first, then by name. */
/* Addon subtitle files in menu order: the preferred language first, then by language
 * name, then as the addons listed them. Returns the count written to `order`. */
static int sorted_subs(const Player *p, int *order)
{
    int n = p->n_addon_subs;
    for (int i = 0; i < n; i++) order[i] = i;
    for (int i = 1; i < n; i++)
        for (int j = i; j > 0; j--) {
            const char *la = p->addon_subs[order[j]].lang, *lb = p->addon_subs[order[j - 1]].lang;
            int a = lang_same(la, g_app.pref_subs), b = lang_same(lb, g_app.pref_subs);
            int c = a > b || (a == b && strcasecmp(lang_display(la), lang_display(lb)) < 0);
            if (c) { int t = order[j]; order[j] = order[j - 1]; order[j - 1] = t; } else break;
        }
    return n;
}

/* ---- file header: chapters and what the file really contains ------------------ */

static void on_player_probe(HttpResponse *r, void *user)
{
    SubReq *q = user;
    Player *p = q->owner;
    if (p != g_live || q->gen != p->gen) { free(q); return; }
    free(q);
    if ((r->status != 200 && r->status != 206) || r->len == 0) return;
    ProbeInfo *info = malloc(sizeof *info);
    if (probe_parse((const uint8_t *)r->body, r->len, info)) {
        free(p->probe);
        p->probe = info;
        char d[200];
        probe_describe(info, d, sizeof d);
        LOGI("player: file is %s, %d chapter%s", d, info->n_chapters, info->n_chapters == 1 ? "" : "s");
        app_redraw();
    } else free(info);
}

static void probe_current(Player *p)
{
    p->probe_asked = 1;
    const char *url = p->cands[p->cur].url;
    if (strncmp(url, "http", 4) != 0) return;
    SubReq *q = malloc(sizeof *q);
    q->owner = p; q->gen = p->gen; q->addon = -1; q->slot = 0;
    HttpOptions opt = { 2u << 20, 8, 1 };
    http_request_async_ex(url, &opt, on_player_probe, q);
}

/* Seek preview from the viewer's own ffmpeg server (tools/thumbs): the frame at `t`
 * rounded to 10 s. Returns the texture when it has arrived, 0 meanwhile. */
static unsigned seek_thumb(const Player *p, double t, int *w, int *h, int prefetch_neighbours)
{
    if (!g_app.thumb_server || p->cur >= p->n_cands || strncmp(p->cands[p->cur].url, "http", 4) != 0) return 0;
    char enc[1600], url[2048];
    http_url_encode(p->cands[p->cur].url, enc, sizeof enc);
    long sec = (long)(t / 10) * 10;
    if (prefetch_neighbours)
        for (int d = -2; d <= 2; d++) {
            if (!d || sec + d * 10 < 0) continue;
            snprintf(url, sizeof url, "%s/frame?url=%s&t=%ld", g_app.thumb_server, enc, sec + d * 10);
            image_prefetch(url);
        }
    snprintf(url, sizeof url, "%s/frame?url=%s&t=%ld", g_app.thumb_server, enc, sec);
    return image_get(url, w, h);
}

/* Chapter containing `pos` (-1 none) */
static int chapter_at(const Player *p, double pos)
{
    if (!p->probe) return -1;
    int k = -1;
    for (int i = 0; i < p->probe->n_chapters; i++) if (p->probe->chapters[i].start_s <= pos + 0.01) k = i;
    return k;
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

/* Back to the sources list for this episode; the player goes away with its video. */
static void browse_sources(Player *p)
{
    report(p, 1);
    MetaPreview meta;
    meta_preview_copy(&meta, &p->meta);
    char *vid = strdup(p->video_id);
    const char *sep = strstr(p->title, "  -  ");
    char *vt = sep ? strdup(sep + 5) : NULL;
    p->finished = 1;
    app_replace(screen_streams_create(&meta, vid, vt));   /* destroys this player */
    meta_preview_free(&meta);
    free(vid); free(vt);
}

/* ---- playback / fallback ------------------------------------------------------ */

static void start_candidate(Player *p, int idx, double start)
{
    p->cur = idx;
    p->error_handled = 0;
    p->learned = 0;
    p->probe_asked = 0;
    free(p->probe);
    p->probe = NULL;
    p->rate_applied = 0;
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
    if (!p->learned) { app_tv_learn(p->cands[p->cur].needs, 0); p->learned = 1; }
    if (p->cur + 1 < p->n_cands && p->tries < 3) {
        p->tries++;
        app_toast("%s. Trying the next source...", why);
        start_candidate(p, p->cur + 1, resume);
    } else {
        p->error_handled = 1;   /* stay on the error screen */
    }
}

static void go_to_video(Player *p, const char *video_id, const char *video_title)
{
    report(p, 1);
    video_stop();
    MetaPreview meta;
    meta_preview_copy(&meta, &p->meta);
    char *vid = strdup(video_id), *title = video_title ? strdup(video_title) : NULL;
    char *group = p->cands[p->cur].binge_group ? strdup(p->cands[p->cur].binge_group) : NULL;
    p->finished = 1;
    app_pop();   /* destroys this screen */
    app_replace(screen_streams_create_auto(&meta, vid, title, group));
    meta_preview_free(&meta);
    free(vid); free(title); free(group);
}

static void go_up_next(Player *p) { go_to_video(p, p->next_video_id, p->next_title); }

/* ---- next episode readiness ------------------------------------------------------- */

#define READY_PULSE_S 0.6f

/* -1 while nothing is being fetched, else the eased share of addons that answered. */
static float next_progress(const Player *p)
{
    return p->next_video_id && streams_prefetch_progress(p->next_video_id) >= 0 ? p->next_prog : -1;
}

static void next_progress_update(Player *p, float dt)
{
    float target = p->next_video_id ? streams_prefetch_progress(p->next_video_id) : -1;
    if (target < 0) { p->next_prog = 0; p->next_ready_at = 0; return; }
    float before = p->next_prog;
    p->next_prog += (target - p->next_prog) * (dt * 10 > 1 ? 1 : dt * 10);
    if (target - p->next_prog < 0.004f) p->next_prog = target;
    if (before < 1 && p->next_prog >= 1) p->next_ready_at = g_app.time;
    if (p->next_prog != before || (p->next_ready_at > 0 && g_app.time - p->next_ready_at < READY_PULSE_S)) app_redraw();
}

/* The Next button as a circle: a ring fills while the sources arrive, then pulses once. */
static void draw_next_circle(const Player *p, float cx, float cy, float r)
{
    float k = next_progress(p);
    int loading = k >= 0 && k < 1;
    gfx_rrect(cx - r, cy - r, 2 * r, 2 * r, theme_pill_r(2 * r), COLOR_ALPHA(THEME_TEXT, loading ? 0x14 : 0x2C));
    ui_icon(ICON_NEXT, cx, cy, 26, loading ? THEME_TEXT_MUTED : THEME_TEXT);
    if (loading) {
        ui_arc(cx, cy, r + 5, 3, 0, 1, COLOR_ALPHA(THEME_TEXT, 0x22));
        ui_arc(cx, cy, r + 5, 3, 0, k, THEME_ACCENT2);
    } else if (k >= 1 && p->next_ready_at > 0) {
        float t = (g_app.time - p->next_ready_at) / READY_PULSE_S;
        if (t < 1) gfx_rrect_ring(cx - r - t * 14, cy - r - t * 14, 2 * (r + t * 14), 2 * (r + t * 14), r + t * 14, 3,
                                  COLOR_ALPHA(THEME_ACCENT2, (int)(255 * (1 - t))));
    }
}

/* A focused or primary pill for the next episode: it fills left to right while the
 * sources arrive and reads fully lit once the episode can start at once. */
static void draw_next_pill(const Player *p, const char *label, const char *loading_label, float x, float y, float w, float h,
                           Color fill, Color fg)
{
    float k = next_progress(p);
    if (k < 0 || k >= 1) {
        gfx_rrect(x, y, w, h, theme_pill_r(h), fill);
        if (k >= 1 && p->next_ready_at > 0) {
            float t = (g_app.time - p->next_ready_at) / READY_PULSE_S;
            if (t < 1) gfx_rrect_ring(x - t * 10, y - t * 10, w + t * 20, h + t * 20, theme_pill_r(h) + t * 10, 3, COLOR_ALPHA(fill, (int)(200 * (1 - t))));
        }
    } else {
        gfx_rrect(x, y, w, h, theme_pill_r(h), COLOR_ALPHA(fill, 0x66));
        gfx_clip_push(x, y, w * k, h);
        gfx_rrect(x, y, w, h, theme_pill_r(h), fill);
        gfx_clip_pop();
        label = loading_label;
    }
    ui_icon(ICON_NEXT, x + h / 2 + 2, y + h / 2, h * 0.46f, fg);
    font_draw_fit(label, x + h + 2, y + (h - font_height(FONT_S | FONT_B)) / 2, FONT_S | FONT_B, fg, w - h - 16);
}

/* ---- aspect ratio and speed ----------------------------------------------------- */

/* How much larger than the screen the video plane is drawn for the chosen aspect mode. */
static float aspect_scale(const Player *p, const VideoInfo *vi)
{
    if (p->aspect == ASPECT_FIT || p->pip) return 1;
    if (p->aspect == ASPECT_ZOOM) return 1.15f;
    float ar = 0;
    if (vi->width > 0 && vi->height > 0) ar = (float)vi->width / vi->height;
    else if (p->probe && p->probe->width > 0 && p->probe->height > 0) ar = (float)p->probe->width / p->probe->height;
    const float screen = 16.0f / 9.0f;
    if (ar <= 0) return 1.33f;                  /* unknown: assume a scope film in a 16:9 frame */
    if (ar > screen + 0.04f) return ar / screen; /* wider than the screen: bars above and below */
    if (ar < screen - 0.04f) return screen / ar; /* narrower (4:3): bars at the sides */
    return 1.33f;                                /* 16:9 already: the bars are inside the picture */
}

static void apply_aspect(Player *p)
{
    if (p->pip) return;
    VideoInfo vi;
    video_info(&vi);
    int w, h;
    gfx_drawable_size(&w, &h);
    float k = aspect_scale(p, &vi);
    int zw = (int)(w * k), zh = (int)(h * k);
    video_set_display_rect((w - zw) / 2, (h - zh) / 2, zw, zh);
    LOGI("player: aspect %s (scale %.2f)", ASPECT_NAMES[p->aspect], k);
}

static void set_rate(Player *p, int idx)
{
    if (idx < 0) idx = 0;
    if (idx >= N_RATES) idx = N_RATES - 1;
    p->rate_idx = idx;
    video_set_rate(RATES[idx]);
    p->rate_applied = 1;
    app_toast(idx == RATE_NORMAL ? "Normal speed" : "Speed %gx", RATES[idx]);
}

static void update(Screen *s, float dt)
{
    Player *p = s->st;
    if (p->finished) return;
    VideoInfo vi;
    video_info(&vi);
    if (vi.state == VIDEO_PLAYING) app_redraw();   /* progress bar / subtitles move */
    if (vi.state == VIDEO_PAUSED && p->paused_since == 0) { p->paused_since = g_app.time; app_redraw(); }
    if (vi.state != VIDEO_PAUSED && p->paused_since != 0) p->paused_since = 0;
    if (vi.state == VIDEO_PAUSED && g_app.time - p->paused_since < 1.3f) app_redraw();   /* the info panel fades in */
    if (vi.state == VIDEO_PLAYING && !p->rate_applied && p->rate_idx != RATE_NORMAL) { video_set_rate(RATES[p->rate_idx]); p->rate_applied = 1; }
    if (p->bar_focus && g_app.time >= p->overlay_until && vi.state != VIDEO_PAUSED) p->bar_focus = 0;
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
        if (vi.warning[0] && !p->learned) {   /* "can't play this audio": remember the audio codec */
            app_tv_learn(p->cands[p->cur].needs & ((1u << TVCAP_DTS) | (1u << TVCAP_TRUEHD)), 0);
            p->learned = 1;
        }
    }
    if (vi.state == VIDEO_PLAYING || vi.state == VIDEO_PAUSED) auto_select_tracks(p);

    if (vi.state == VIDEO_ERROR && !p->error_handled) { p->error_handled = 1; fail_over(p, vi.error[0] ? vi.error : "Playback failed"); }
    if (vi.state == VIDEO_LOADING && g_app.time - p->load_started > 45.0f && !p->error_handled) { p->error_handled = 1; fail_over(p, "Source is not responding"); }

    if (!p->learned && vi.state == VIDEO_PLAYING && vi.position > 0 && g_app.time - p->load_started > 90 && p->stalls == 0) {
        app_tv_learn(p->cands[p->cur].needs, 1);
        p->learned = 1;
    }
    if (!p->probe_asked && (vi.state == VIDEO_PLAYING || vi.state == VIDEO_PAUSED)) probe_current(p);
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
    /* fetch the next episode's sources now, so Up Next starts without a wait; the
     * control bar asks too, so its Next button can show them arriving */
    int want_next = p->up_next_shown || p->bar_focus || (vi.duration > 120 && vi.position > vi.duration - 90);
    if (p->next_video_id && want_next && vi.state != VIDEO_LOADING && streams_prefetch_progress(p->next_video_id) < 0
        && (!p->prefetched || g_app.time - p->prefetch_tried > 30)) {
        p->prefetched = 1;
        p->prefetch_tried = g_app.time;
        streams_prefetch(&p->meta, p->next_video_id);
    }
    next_progress_update(p, dt);
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
    const char *text = p->track_loaded ? subs_at(&p->track, vi->position) : NULL;
    const char *text2 = p->track2_loaded ? subs_at(&p->track2, vi->position) : NULL;
    float bottom = g_app.time < p->overlay_until ? SNTV_UI_H - 250 : SNTV_UI_H - 96;
    if (text) ui_subtitle(text, bottom, g_app.subs_size, g_app.subs_style);
    if (text2) {   /* the second language sits above the first (or where it would be) */
        int size = g_app.subs_size == 0 ? FONT_M : g_app.subs_size == 2 ? FONT_XL : FONT_L;
        float lift = text ? font_height(size | FONT_B) * 1.04f * 2 + 24 : 0;
        ui_subtitle(text2, bottom - lift, g_app.subs_size > 0 ? g_app.subs_size - 1 : 0, g_app.subs_style);
    }
}

static void draw_menu(Player *p)
{
    float w = 600, x = SNTV_UI_W - w - 40, y = 40, h = SNTV_UI_H - 80;
    if (p->menu_seen != p->menu) { p->menu_seen = p->menu; p->menu_t = g_app.fx_light ? 1 : 0; }
    if (p->menu_t < 1) { p->menu_t += 0.09f; if (p->menu_t > 1) p->menu_t = 1; app_redraw(); }
    { float e = 1 - (1 - p->menu_t) * (1 - p->menu_t); gfx_set_translate((w + 60) * (1 - e), 0); }
    if (!g_app.fx_light) gfx_shadow(x, y, w, h, R_PANEL, 50, RGBA(0, 0, 0, 0xA0));
    gfx_rrect(x, y, w, h, R_PANEL, COLOR_ALPHA(THEME_PANEL, 0xF0));
    gfx_rrect_ring(x, y, w, h, R_PANEL, 1.5f, COLOR_ALPHA(THEME_TEXT, 0x1C));
    static const char *TITLES[] = { "", "Audio", "Subtitles", "Subtitle style", "Sources", "Episodes" };
    static const Icon ICONS[] = { ICON_PLAY, ICON_AUDIO, ICON_SUBS, ICON_SUBS, ICON_SWAP, ICON_LIST };
    ui_icon(ICONS[p->menu], x + 56, y + 62, 40, THEME_TEXT);
    font_draw(TITLES[p->menu], x + 92, y + 40, FONT_L | FONT_B, THEME_TEXT);
    VideoTrack tr[MAX_TRACKS];
    int n = video_tracks(p->menu == MENU_AUDIO ? TRACK_AUDIO : TRACK_TEXT, tr, MAX_TRACKS);
    int order[MAX_ADDON_SUBS];
    int ng = p->menu == MENU_SUBS ? sorted_subs(p, order) : 0;   /* every file, one row each */
    /* keep the focused row inside the panel: rows are 78 px, section labels 40 px */
    int n_eps = 0;
    const MetaVideo *eps = p->menu == MENU_EPISODES ? app_videos(p->meta.id, &n_eps) : NULL;
    int ep_sections = 0;
    for (int i = 0; i <= p->menu_focus && i < n_eps; i++) if (i == 0 || eps[i].season != eps[i - 1].season) ep_sections++;
    int src_sections = 0;
    if (p->menu == MENU_SOURCES) {
        char last[80] = "";
        for (int i = 0; i <= p->menu_focus && i < p->n_cands; i++) {
            const char *lbl = p->cands[i].label ? p->cands[i].label : "", *sep = strstr(lbl, " - ");
            char ad[80] = ""; if (sep) snprintf(ad, sizeof ad, "%.*s", (int)(sep - lbl) < 79 ? (int)(sep - lbl) : 79, lbl);
            if (ad[0] && strcmp(ad, last) != 0) { src_sections++; snprintf(last, sizeof last, "%s", ad); }
        }
    }
    int sections_before = p->menu == MENU_AUDIO ? 1 + ((n && p->menu_focus >= 1) ? 1 : 0)
                        : p->menu == MENU_SOURCES ? src_sections
                        : p->menu == MENU_EPISODES ? ep_sections
                        : p->menu == MENU_SUBS ? ((p->menu_focus >= 2 && n) ? 1 : 0) + ((p->menu_focus >= 2 + n && (ng || p->subs_pending)) ? 1 : 0)
                        : p->menu == MENU_STYLE ? 1 + (p->menu_focus >= SUBS_STYLE_COUNT ? 1 : 0) : 0;
    float focus_y = 120 + sections_before * 40 + p->menu_focus * 78.0f;
    float scroll = focus_y + 78 > h - 60 ? focus_y + 78 - (h - 60) : 0;
    float ry = y + 120 - scroll;
    int row = 0;
    gfx_clip_push(x, y + 104, w, h - 120);
    #define SECTION(t) do { font_draw(t, x + 40, ry + 6, FONT_XS | FONT_B, THEME_TEXT_MUTED); ry += 40; } while (0)
    const char *row_thumb = NULL;   /* set before a ROW to show a picture at its left */
    #define ROW(label, sub, on) do { \
        int foc_ = p->menu_focus == row; const char *sub_ = (sub); float lx_ = x + 50 + (row_thumb ? 118 : 0); \
        if (ry + 70 > y + 100 && ry < y + h - 16) { \
        if (foc_) gfx_rrect(x + 24, ry, w - 48, 70, R_KEY, THEME_FOCUS); \
        else if (on) gfx_rrect(x + 24, ry, w - 48, 70, R_PANEL, COLOR_ALPHA(THEME_TEXT, 0x14)); \
        if (row_thumb) ui_draw_thumb(row_thumb, x + 40, ry + 6, 104, 58, 0, COLOR_ALPHA(THEME_TEXT, 0x14)); \
        Color fg_ = foc_ ? THEME_ON_FOCUS : THEME_TEXT; \
        font_draw_fit(label, lx_, ry + (sub_[0] ? 10 : 20), FONT_S | FONT_B, fg_, w - 190 - (lx_ - x - 50)); \
        if (sub_[0]) font_draw_fit(sub_, lx_, ry + 40, FONT_XS, foc_ ? COLOR_ALPHA(THEME_BG, 0xB0) : THEME_TEXT_MUTED, w - 190 - (lx_ - x - 50)); \
        if (on) { gfx_circle(x + w - 70, ry + 35, 17, foc_ ? THEME_ON_FOCUS : THEME_ACCENT); ui_icon(ICON_CHECK, x + w - 70, ry + 35, 20, foc_ ? THEME_FOCUS : THEME_ON_ACCENT); } } \
        ry += 78; row++; } while (0)
    if (p->menu == MENU_STYLE) {
        static const char *SIZES[] = { "Small", "Medium", "Large" };
        SECTION("STYLE  (addon subtitles)");
        for (int i = 0; i < SUBS_STYLE_COUNT; i++) ROW(SUBS_STYLE_NAMES[i], "", g_app.subs_style == i);
        SECTION("SIZE");
        for (int i = 0; i < 3; i++) ROW(SIZES[i], "", g_app.subs_size == i);
        gfx_clip_pop();
        gfx_set_translate(0, 0);
        return;
    }
    if (p->menu == MENU_EPISODES) {
        for (int i = 0; i < n_eps; i++) {
            if (i == 0 || eps[i].season != eps[i - 1].season) {
                char sec[32];
                if (eps[i].season > 0) snprintf(sec, sizeof sec, "SEASON %d", eps[i].season); else snprintf(sec, sizeof sec, eps[i].season == 0 ? "SPECIALS" : "EPISODES");
                SECTION(sec);
            }
            char label[160], sub[64] = "";
            if (eps[i].episode >= 0) snprintf(label, sizeof label, "E%d  %s", eps[i].episode, eps[i].title ? eps[i].title : ""); else snprintf(label, sizeof label, "%s", eps[i].title ? eps[i].title : "Episode");
            if (eps[i].released && strlen(eps[i].released) >= 10) snprintf(sub, sizeof sub, "%.10s", eps[i].released);
            int cur = strcmp(eps[i].id, p->video_id) == 0;
            if (cur) snprintf(sub, sizeof sub, "Now playing");
            row_thumb = eps[i].thumbnail;
            ROW(label, sub, cur);
            row_thumb = NULL;
        }
        gfx_clip_pop();
        gfx_set_translate(0, 0);
        return;
    }
    if (p->menu == MENU_SOURCES) {
        char last_addon[80] = "";
        for (int i = 0; i < p->n_cands; i++) {
            char host[80], addon[80] = "", name[160];
            url_host(p->cands[i].url, host, sizeof host);
            const char *lbl = p->cands[i].label ? p->cands[i].label : "Source", *sep = strstr(lbl, " - ");
            if (sep) { snprintf(addon, sizeof addon, "%.*s", (int)(sep - lbl) < 79 ? (int)(sep - lbl) : 79, lbl); snprintf(name, sizeof name, "%s", sep + 3); }
            else snprintf(name, sizeof name, "%s", lbl);
            if (addon[0] && strcmp(addon, last_addon) != 0) {   /* one group per addon, in the order they answer */
                char sec[96]; for (char *c = addon; *c; c++) if (*c >= 'a' && *c <= 'z') *c -= 32;
                snprintf(sec, sizeof sec, "%s", addon); SECTION(sec);
                snprintf(last_addon, sizeof last_addon, "%s", addon);
            }
            ROW(name, host, i == p->cur);
        }
        ROW("Browse all sources", "Every addon again, with the recommendation for this TV", 0);
        gfx_clip_pop();
        gfx_set_translate(0, 0);
        return;
    }
    if (p->menu == MENU_AUDIO) {
        char sp[80];
        snprintf(sp, sizeof sp, p->rate_idx == RATE_NORMAL ? "Normal  \xC2\xB7  Left/Right to change" : "%gx  \xC2\xB7  Left/Right to change", RATES[p->rate_idx]);
        SECTION("PLAYBACK");
        ROW("Speed", sp, 0);
    }
    if (p->menu == MENU_SUBS) {
        ROW("Choose for", p->assign_second ? "Second subtitles (above the main ones)  \xC2\xB7  Left/Right" : "Main subtitles  \xC2\xB7  Left/Right for a second language", 0);
        ROW("Off", "", p->assign_second ? p->active_addon_sub2 < 0 : (p->embedded_text_sel < 0 && p->active_addon_sub < 0));
    }
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
        if (ng || p->subs_pending) SECTION(p->subs_pending ? "FROM ADDONS  (SEARCHING)" : "FROM ADDONS");
        for (int g = 0; g < ng; g++) {
            int idx = order[g], dup = 0;
            for (int j = 0; j < g; j++) if (strcmp(lang_display(p->addon_subs[order[j]].lang), lang_display(p->addon_subs[idx].lang)) == 0) dup++;
            char label[80];
            if (dup) snprintf(label, sizeof label, "%s (%d)", lang_display(p->addon_subs[idx].lang), dup + 1);
            else snprintf(label, sizeof label, "%s", lang_display(p->addon_subs[idx].lang));
            char who[120];
            snprintf(who, sizeof who, "%s%s", p->addon_subs[idx].addon, p->active_addon_sub2 == idx ? "  \xC2\xB7  second" : "");
            ROW(label, who, p->active_addon_sub == idx || p->active_addon_sub2 == idx);
        }
        if (p->track_loaded) {
            char off[80];
            snprintf(off, sizeof off, "Left/Right shifts by 0.5 s  \xC2\xB7  now %+.1f s", p->track.offset);
            ROW("Subtitle timing", off, 0);
        }
        char st[80];
        static const char *SIZES[] = { "Small", "Medium", "Large" };
        snprintf(st, sizeof st, "%s  \xC2\xB7  %s", SUBS_STYLE_NAMES[g_app.subs_style], SIZES[g_app.subs_size]);
        ROW("Style and size", st, 0);
    }
    gfx_clip_pop();
    gfx_set_translate(0, 0);
    #undef ROW
    #undef SECTION
}

static void draw_up_next(Player *p, const VideoInfo *vi, int overlay)
{
    float w = 640, h = 176, x = SNTV_UI_W - THEME_MARGIN - w, y = overlay ? SNTV_UI_H - 400 : SNTV_UI_H - 240;
    if (!g_app.fx_light) gfx_shadow(x, y, w, h, R_PANEL, 40, RGBA(0, 0, 0, 0xA0));
    gfx_rrect(x, y, w, h, R_PANEL, COLOR_ALPHA(THEME_PANEL, 0xF2));
    gfx_rrect_ring(x, y, w, h, R_PANEL, 1.5f, COLOR_ALPHA(THEME_TEXT, 0x20));
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
    float k = next_progress(p), gx = x + w - 140, gy = y + h - 64;
    Color go = THEME_OK;
    const char *what = k >= 0 && k < 1 ? "Preparing" : "Play now";
    if (k >= 0 && k < 1) {   /* fills as the next episode's sources arrive */
        gfx_rrect(gx, gy, 120, 40, R_PANEL, COLOR_ALPHA(go, 0x55));
        gfx_clip_push(gx, gy, 120 * k, 40);
        gfx_rrect(gx, gy, 120, 40, R_PANEL, go);
        gfx_clip_pop();
    } else {
        gfx_rrect(gx, gy, 120, 40, R_PANEL, go);
        float t = p->next_ready_at > 0 ? (g_app.time - p->next_ready_at) / READY_PULSE_S : 1;
        if (t < 1) gfx_rrect_ring(gx - t * 8, gy - t * 8, 120 + t * 16, 40 + t * 16, 20 + t * 8, 2.5f, COLOR_ALPHA(go, (int)(220 * (1 - t))));
    }
    font_draw_center(what, gx + 60, gy + (40 - font_height(FONT_XS | FONT_B)) / 2, FONT_XS | FONT_B, THEME_ON_LIGHT);
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
    gfx_rrect(x + h / 2, y + h + 10, (w - h) * (1 - k), 4, 2, COLOR_ALPHA(THEME_TEXT, 0x90));
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
    char host[128], l[16][160];
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
    if (p->probe) { char d[160]; probe_describe(p->probe, d, sizeof d); snprintf(l[n++], 160, "File header|%.100s  \xC2\xB7  %d chapters", d, p->probe->n_chapters); }
    snprintf(l[n++], 160, "Speed / aspect|%gx  \xC2\xB7  %s%s", RATES[p->rate_idx], ASPECT_NAMES[p->aspect], p->track2_loaded ? "  \xC2\xB7  two subtitle languages" : "");
    snprintf(l[n++], 160, "App|%s  \xC2\xB7  %d draw calls  \xC2\xB7  %zu MB artwork", video_backend_name(), gfx_draw_calls(), image_cache_bytes() >> 20);
    if (g_app.frames_measured) snprintf(l[n++], 160, "UI frame|%.1f ms average, %.0f ms worst", g_app.frame_ms_sum / g_app.frames_measured, g_app.frame_ms_worst);

    float w = 980, lh = 36, h = 70 + n * lh, x = THEME_MARGIN, y = 190;
    gfx_rrect(x, y, w, h, R_PANEL, COLOR_ALPHA(THEME_BG, 0xD8));
    gfx_rrect_ring(x, y, w, h, R_PANEL, 1.5f, COLOR_ALPHA(THEME_TEXT, 0x20));
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

/* The button row under the seek bar. Returns the count; ids map to BTN_*. */
static int bar_buttons(const Player *p, int *ids, const char **labels, Icon *icons, char *src_label, size_t n)
{
    VideoInfo vi;
    video_info(&vi);
    int k = 0, n_eps = 0;
    app_videos(p->meta.id, &n_eps);
    if (p->prev_video_id) { ids[k] = BTN_PREV; labels[k] = "Previous episode"; icons[k++] = ICON_PREV; }
    ids[k] = BTN_PLAY; labels[k] = vi.state == VIDEO_PLAYING ? "Pause" : "Play"; icons[k++] = vi.state == VIDEO_PLAYING ? ICON_PAUSE : ICON_PLAY;
    if (p->next_video_id) { ids[k] = BTN_NEXT; labels[k] = "Next episode"; icons[k++] = ICON_NEXT; }
    ids[k] = BTN_RELOAD; labels[k] = "Reload"; icons[k++] = ICON_RELOAD;
    ids[k] = BTN_SUBS; labels[k] = "Subtitles"; icons[k++] = ICON_SUBS;
    ids[k] = BTN_AUDIO; labels[k] = "Audio and speed"; icons[k++] = ICON_AUDIO;
    ids[k] = BTN_ASPECT; labels[k] = "Aspect ratio"; icons[k++] = ICON_ASPECT;
    snprintf(src_label, n, p->n_cands > 1 ? "Sources (%d)" : "Sources", p->n_cands);
    ids[k] = BTN_SOURCES; labels[k] = src_label; icons[k++] = ICON_SWAP;
    if (n_eps > 1) { ids[k] = BTN_EPISODES; labels[k] = "Episodes"; icons[k++] = ICON_LIST; }
    ids[k] = BTN_STATS; labels[k] = p->stats ? "Hide stats" : "Stats"; icons[k++] = ICON_INFO;
    return k;
}

/* Round icon buttons; the focused one opens into a labelled pill (like the TV apps people know). */
static void draw_bar(Player *p, float x, float y)
{
    int ids[BTN_COUNT]; const char *labels[BTN_COUNT]; Icon icons[BTN_COUNT]; char src[32];
    int n = bar_buttons(p, ids, labels, icons, src, sizeof src);
    if (p->bar_index >= n) p->bar_index = n - 1;
    const float d = 56, gap = 14;
    for (int i = 0; i < n; i++) {
        int foc = p->bar_focus && i == p->bar_index;
        int primary = ids[i] == BTN_PLAY || ids[i] == BTN_NEXT;
        if (foc && ids[i] == BTN_NEXT) {
            float w = font_measure("Getting it ready", FONT_S | FONT_B) + d + 28;
            ui_focus_under(x, y, w, d, theme_pill_r(d), 1);
            draw_next_pill(p, labels[i], "Getting it ready", x, y, w, d, THEME_FOCUS, THEME_ON_FOCUS);
            x += w + gap;
        } else if (ids[i] == BTN_NEXT) {
            draw_next_circle(p, x + d / 2, y + d / 2, d / 2);
            x += d + gap;
        } else if (foc) {
            float w = font_measure(labels[i], FONT_S | FONT_B) + d + 28;
            ui_focus_under(x, y, w, d, theme_pill_r(d), 1);
            gfx_rrect(x, y, w, d, theme_pill_r(d), THEME_FOCUS);
            ui_icon(icons[i], x + d / 2 + 2, y + d / 2, 26, THEME_ON_FOCUS);
            font_draw(labels[i], x + d + 2, y + (d - font_height(FONT_S | FONT_B)) / 2, FONT_S | FONT_B, THEME_ON_FOCUS);
            x += w + gap;
        } else {
            gfx_rrect(x, y, d, d, theme_pill_r(d), COLOR_ALPHA(THEME_TEXT, primary ? 0x2C : 0x18));
            ui_icon(icons[i], x + d / 2 + (icons[i] == ICON_PLAY ? 2 : 0), y + d / 2, 26, THEME_TEXT);
            x += d + gap;
        }
    }
    const char *hint = p->bar_focus ? "OK: open  \xC2\xB7  Up: back to the timeline" : "Down: options";
    font_draw(hint, SNTV_UI_W - THEME_MARGIN - font_measure(hint, FONT_XS), y + (d - font_height(FONT_XS)) / 2, FONT_XS, THEME_TEXT_MUTED);
}

static int episodes_focus(const Player *p)
{
    int n = 0;
    const MetaVideo *v = app_videos(p->meta.id, &n);
    for (int i = 0; i < n; i++) if (strcmp(v[i].id, p->video_id) == 0) return i;
    return 0;
}

static void bar_activate(Player *p, const VideoInfo *vi)
{
    int ids[BTN_COUNT]; const char *labels[BTN_COUNT]; Icon icons[BTN_COUNT]; char src[32];
    int n = bar_buttons(p, ids, labels, icons, src, sizeof src);
    if (p->bar_index >= n) return;
    switch (ids[p->bar_index]) {
    case BTN_PREV: if (p->prev_video_id) go_to_video(p, p->prev_video_id, p->prev_title); return;
    case BTN_PLAY:
        if (vi->state == VIDEO_PLAYING) video_pause(); else { video_play(); p->flash_icon = ICON_PLAY; p->play_flash = g_app.time + 0.6f; }
        break;
    case BTN_NEXT: go_up_next(p); return;
    case BTN_RELOAD: app_toast("Reloading"); start_candidate(p, p->cur, vi->position); break;
    case BTN_SUBS: p->menu = MENU_SUBS; p->menu_focus = 0; break;
    case BTN_AUDIO: p->menu = MENU_AUDIO; p->menu_focus = 0; break;
    case BTN_ASPECT: p->aspect = (p->aspect + 1) % ASPECT_COUNT; apply_aspect(p); app_toast("Aspect: %s", ASPECT_NAMES[p->aspect]); break;
    case BTN_SOURCES: p->menu = MENU_SOURCES; p->menu_focus = p->cur; break;
    case BTN_EPISODES: p->menu = MENU_EPISODES; p->menu_focus = episodes_focus(p); break;
    case BTN_STATS: p->stats = !p->stats; break;
    }
    show_overlay(p);
}

/* "1080p" and "Dolby Digital+ 5.1" from what the file (or the pipeline) says. */
static void media_badges(const Player *p, const VideoInfo *vi, char *res, size_t rn, char *aud, size_t an)
{
    res[0] = aud[0] = 0;
    int h = vi->height > 0 ? vi->height : p->probe ? p->probe->height : 0;
    if (h >= 2000) snprintf(res, rn, "4K"); else if (h > 0) snprintf(res, rn, "%dp", h >= 1000 ? 1080 : h >= 700 ? 720 : h);
    if (p->probe && p->probe->hdr) snprintf(res + strlen(res), rn - strlen(res), "%s%s", res[0] ? "  " : "", p->probe->hdr == 2 ? "Dolby Vision" : "HDR");
    if (p->probe && p->probe->acodec != AUDIO_UNKNOWN) {
        static const char *A[] = { "", "AAC", "Dolby Digital", "Dolby Digital+", "DTS", "TrueHD", "Audio" };
        int ch = p->probe->audio_channels;
        snprintf(aud, an, "%s%s", A[p->probe->acodec], ch >= 8 ? " 7.1" : ch >= 6 ? " 5.1" : ch == 2 ? " stereo" : ch == 1 ? " mono" : "");
    }
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
        VideoInfo vi; video_info(&vi);
        float z = aspect_scale(p, &vi);   /* the zoom the TV's video plane would get */
        if (z > 1) { float cu = (u0 + u1) / 2, cv = (v0 + v1) / 2; u0 = cu - (cu - u0) / z; u1 = cu + (u1 - cu) / z; v0 = cv - (cv - v0) / z; v1 = cv + (v1 - cv) / z; }
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

/* What is on: title, episode, year and runtime, synopsis, cast. Shown once playback has
 * been paused for a moment, in the top-left where the title normally sits. */
static void draw_paused_info(Player *p, const VideoInfo *vi, float k)
{
    (void)vi;
    int a = (int)(255 * k);
    float x = THEME_MARGIN, y = 150 - (1 - k) * 16, maxw = 1000;
    gfx_gradient_h(0, 0, 1300, SNTV_UI_H, RGBA(0, 0, 0, (int)(0xB8 * k)), RGBA(0, 0, 0, 0));
    font_draw("YOU'RE WATCHING", x, y, FONT_XS | FONT_B, COLOR_ALPHA(THEME_TEXT_MUTED, a));
    y += 44;
    char buf[512];
    const char *logo = app_art_logo(&p->meta, buf, sizeof buf);
    int lw = 0, lh = 0;
    unsigned ltex = logo ? image_get(logo, &lw, &lh) : 0;
    if (ltex && lh > 0) {
        float w = 520, hh = w * lh / lw;
        if (hh > 150) { hh = 150; w = hh * lw / lh; }
        gfx_tex(ltex, x, y, w, hh, RGBA(255, 255, 255, a));
        y += hh + 22;
    } else {
        int fs = font_measure(p->meta.name, FONT_XL | FONT_B) > maxw ? (FONT_L | FONT_B) : (FONT_XL | FONT_B);
        y += font_draw_wrapped(p->meta.name, x, y, fs, COLOR_ALPHA(THEME_TEXT, a), maxw, 2) * font_height(fs) + 14;
    }
    const char *ep = strstr(p->title, "  -  ");
    if (ep) { font_draw_fit(ep + 5, x, y, FONT_M | FONT_B, COLOR_ALPHA(THEME_TEXT, a), maxw); y += font_height(FONT_M | FONT_B) + 12; }
    const TmdbInfo *tm = tmdb_get(p->meta.id, p->meta.type);
    float mx = x;
    if (p->meta.imdb_rating && *p->meta.imdb_rating) mx += ui_imdb(p->meta.imdb_rating, mx, y) + 6;
    if (tm && tm->cert[0]) mx += ui_chip_outline(tm->cert, mx, y, COLOR_ALPHA(THEME_TEXT, (int)(0x70 * k)), COLOR_ALPHA(THEME_TEXT, a)) + 8;
    const char *parts[3]; int np = 0;
    if (p->meta.release_info) parts[np++] = p->meta.release_info;
    if (p->meta.runtime) parts[np++] = p->meta.runtime;
    for (int i = 0; i < np; i++) {
        if (mx > x) mx = ui_dot_sep(mx, y, COLOR_ALPHA(THEME_TEXT_MUTED, a));
        font_draw(parts[i], mx, y + (36 - font_height(FONT_S | FONT_B)) / 2, FONT_S | FONT_B, COLOR_ALPHA(THEME_TEXT_DIM, a));
        mx += font_measure(parts[i], FONT_S | FONT_B);
    }
    if (np || mx > x) y += 50;
    if (p->meta.description) y += font_draw_wrapped(p->meta.description, x, y, FONT_S, COLOR_ALPHA(THEME_TEXT_DIM, a), maxw, 4) * font_height(FONT_S) + 26;
    int n_cast = tm && tm->n_cast ? tm->n_cast : p->meta.cast.n;
    if (n_cast && y < SNTV_UI_H - 330) {
        font_draw("CAST", x, y, FONT_XS | FONT_B, COLOR_ALPHA(THEME_TEXT_MUTED, a));
        y += 36;
        float cx = x;
        for (int i = 0; i < n_cast && i < 6; i++) {
            const char *name = tm && tm->n_cast ? tm->cast[i].name : p->meta.cast.v[i];
            const char *role = tm && tm->n_cast ? tm->cast[i].character : (i < p->meta.cast_roles.n ? p->meta.cast_roles.v[i] : "");
            const char *photo = tm && tm->n_cast && tm->cast[i].photo[0] ? tm->cast[i].photo : (i < p->meta.cast_photos.n && p->meta.cast_photos.v[i][0] ? p->meta.cast_photos.v[i] : NULL);
            float w = (font_measure(name, FONT_XS | FONT_B) > font_measure(role, FONT_XS) ? font_measure(name, FONT_XS | FONT_B) : font_measure(role, FONT_XS)) + (photo ? 90 : 74);
            if (cx + w > SNTV_UI_W - 600) break;
            float hh = role[0] ? 64 : 48;
            gfx_rrect(cx, y, w, hh, hh / 2, COLOR_ALPHA(THEME_TEXT, (int)(0x1C * k)));
            int pw = 0, ph = 0;
            unsigned ptex = photo ? image_get(photo, &pw, &ph) : 0;
            float r = hh / 2 - 6;
            if (ptex && pw > 0) {   /* a round photo, cropped to a square */
                float ia = (float)pw / ph, u0 = 0, v0 = 0, u1 = 1, v1 = 1;
                if (ia > 1) { u0 = (1 - 1 / ia) / 2; u1 = 1 - u0; } else { v0 = (1 - ia) / 2; v1 = 1 - v0; }
                gfx_rtex_uv(ptex, cx + 6, y + 6, 2 * r, 2 * r, r, u0, v0, u1, v1, RGBA(255, 255, 255, a), 0);
            } else {
                gfx_circle(cx + 6 + r, y + 6 + r, r, COLOR_ALPHA(THEME_ACCENT, a));
                char ini[2] = { name[0], 0 };
                font_draw(ini, cx + 6 + r - font_measure(ini, FONT_XS | FONT_B) / 2, y + 6 + r - font_height(FONT_XS | FONT_B) / 2, FONT_XS | FONT_B, COLOR_ALPHA(THEME_TEXT, a));
            }
            float tx = cx + 2 * r + 18;
            if (role[0]) { font_draw(name, tx, y + 11, FONT_XS | FONT_B, COLOR_ALPHA(THEME_TEXT, a)); font_draw(role, tx, y + 35, FONT_XS, COLOR_ALPHA(THEME_TEXT_DIM, a)); }
            else font_draw(name, tx, y + (48 - font_height(FONT_XS | FONT_B)) / 2, FONT_XS | FONT_B, COLOR_ALPHA(THEME_TEXT, a));
            cx += w + 12;
        }
    }
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
        gfx_rect(0, 0, SNTV_UI_W, SNTV_UI_H, COLOR_ALPHA(THEME_BG, 0xD8));
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
        gfx_rrect_ring(SNTV_UI_W / 2 - r, SNTV_UI_H / 2 - r, 2 * r, 2 * r, r, 2, COLOR_ALPHA(THEME_TEXT, (int)(0x40 * a)));
        int icon = vi.state == VIDEO_PAUSED ? ICON_PLAY : p->flash_icon;
        ui_icon((Icon)icon, SNTV_UI_W / 2 + (icon == ICON_PLAY ? 6 : 0), SNTV_UI_H / 2, 64, RGBA(255, 255, 255, (int)(255 * a)));
        if (flash > 0) app_redraw();
    }

    if (p->seek_flash > g_app.time && !p->menu) {   /* -10 s / +10 s feedback on the side that seeks */
        float k = (p->seek_flash - g_app.time) / 0.35f, cx = SNTV_UI_W / 2 + p->seek_flash_dir * 260, cy = SNTV_UI_H / 2;
        int a8 = (int)(255 * (k > 1 ? 1 : k));
        gfx_circle(cx, cy, 56, RGBA(0, 0, 0, (int)(0x80 * k)));
        Color fg = COLOR_ALPHA(THEME_TEXT, a8);
        gfx_rrect_ring(cx - 26, cy - 26, 52, 52, R_PANEL, 3, fg);
        float ax = cx + p->seek_flash_dir * 9, ay = cy - 28;
        gfx_line(ax, ay, ax - p->seek_flash_dir * 10, ay - 9, 3, fg);
        gfx_line(ax, ay, ax - p->seek_flash_dir * 10, ay + 9, 3, fg);
        font_draw("10", cx - font_measure("10", FONT_S | FONT_B) / 2, cy - font_height(FONT_S | FONT_B) / 2 + 1, FONT_S | FONT_B, fg);
        app_redraw();
    }
    int bar_only = !overlay && p->seek_commit > 0;
    double shown_pos = p->seek_commit > 0 ? p->seek_target : vi.position;
    float bx = THEME_MARGIN, by = SNTV_UI_H - 150, bw = SNTV_UI_W - 2 * THEME_MARGIN;
    if (overlay) {
        gfx_gradient_v(0, SNTV_UI_H - 420, SNTV_UI_W, 420, RGBA(0, 0, 0, 0), RGBA(0, 0, 0, 0xE0));
        gfx_gradient_v(0, 0, SNTV_UI_W, 220, RGBA(0, 0, 0, 0xB0), RGBA(0, 0, 0, 0));
        int info = vi.state == VIDEO_PAUSED && p->paused_since > 0 && g_app.time - p->paused_since > 0.4f && !p->menu;
        float info_k = info ? (g_app.time - p->paused_since - 0.4f) / 0.5f : 0;
        if (info_k > 1) info_k = 1;
        if (!info) {
            font_draw_fit(p->title, THEME_MARGIN, 56, FONT_L | FONT_B, THEME_TEXT, SNTV_UI_W - 2 * THEME_MARGIN - 420);
            float cx = THEME_MARGIN;
            if (p->cands[p->cur].label) { font_draw_fit(p->cands[p->cur].label, cx, 114, FONT_S, THEME_TEXT_DIM, 700); cx += font_measure(p->cands[p->cur].label, FONT_S) + 20; }
            char res[32], aud[48];
            media_badges(p, &vi, res, sizeof res, aud, sizeof aud);
            if (res[0]) cx += ui_chip(res, cx, 110, COLOR_ALPHA(THEME_TEXT, 0x22), THEME_TEXT) + 10;
            if (aud[0]) cx += ui_chip(aud, cx, 110, COLOR_ALPHA(THEME_TEXT, 0x22), THEME_TEXT) + 10;
            if (!res[0] && !aud[0] && vi.codec[0] && cx < 1200) ui_chip(vi.codec, cx, 110, COLOR_ALPHA(THEME_TEXT, 0x22), THEME_TEXT);
        } else draw_paused_info(p, &vi, info_k);
        float chip_y = 62;
        if (g_app.clock_mode != 2) {   /* "2:52 PM  |  ends at 3:40" */
            char now[24], end[24], text[80];
            ui_clock_text(0, now, sizeof now);
            if (vi.duration > 0 && vi.state != VIDEO_PAUSED) { ui_clock_text((vi.duration - shown_pos) / RATES[p->rate_idx], end, sizeof end); snprintf(text, sizeof text, "%s   |   ends at %s", now, end); }
            else snprintf(text, sizeof text, "%s", now);
            float tw = font_measure(text, FONT_S | FONT_B) + 44, th2 = 52, tx = SNTV_UI_W - THEME_MARGIN - tw;
            gfx_rrect(tx, 47, tw, th2, th2 / 2, COLOR_ALPHA(THEME_BG, 0x8C));
            font_draw(text, tx + 22, 47 + (th2 - font_height(FONT_S | FONT_B)) / 2, FONT_S | FONT_B, THEME_TEXT);
            chip_y = 112;
        }
        if (p->n_cands > 1) {
            char c[48]; snprintf(c, sizeof c, "Source %d of %d", p->cur + 1, p->n_cands);
            ui_chip(c, SNTV_UI_W - THEME_MARGIN - font_measure(c, FONT_XS | FONT_B) - 24, chip_y, COLOR_ALPHA(THEME_TEXT, 0x22), THEME_TEXT);
        }
    } else if (bar_only) {
        gfx_gradient_v(0, SNTV_UI_H - 240, SNTV_UI_W, 240, RGBA(0, 0, 0, 0), RGBA(0, 0, 0, 0xB0));
    }
    if (overlay || bar_only) {
        float th = p->seek_commit > 0 ? 12 : 8, ty = by - th / 2;
        gfx_rrect(bx, ty, bw, th, th / 2, COLOR_ALPHA(THEME_TEXT, 0x30));
        if (vi.duration > 0) {
            if (vi.buffer_percent > 0) gfx_rrect(bx, ty, bw * vi.buffer_percent / 100.0f, th, th / 2, COLOR_ALPHA(THEME_TEXT, 0x30));
            for (int i = 0; i < p->n_skip; i++) {   /* intro / credits ranges */
                double e = p->skip[i].end > 0 ? p->skip[i].end : vi.duration;
                if (p->skip[i].kind == SKIP_PREVIEW || e <= p->skip[i].start) continue;
                gfx_rect(bx + bw * (float)(p->skip[i].start / vi.duration), ty + th + 6, bw * (float)((e - p->skip[i].start) / vi.duration), 3,
                         p->skip[i].kind == SKIP_CREDITS ? COLOR_ALPHA(THEME_TEXT, 0x50) : COLOR_ALPHA(THEME_ACCENT2, 0xC0));
            }
            if (p->probe)   /* chapter marks */
                for (int i = 1; i < p->probe->n_chapters; i++) {
                    float cxp = bx + bw * (float)(p->probe->chapters[i].start_s / vi.duration);
                    gfx_rect(cxp - 1.5f, ty - 6, 3, th + 12, COLOR_ALPHA(THEME_TEXT, 0x70));
                }
            float fw = bw * (float)(shown_pos / vi.duration);
            if (fw > th) gfx_rrect_grad_h(bx, ty, fw, th, th / 2, THEME_ACCENT, THEME_ACCENT2);
            float kx = bx + fw;
            if (!g_app.fx_light) gfx_shadow(kx - 14, by - 14, 28, 28, R_PANEL, 14, COLOR_ALPHA(THEME_ACCENT2, 0xB0));
            gfx_circle(kx, by, p->seek_commit > 0 ? 16 : 12, THEME_TEXT);
            if (p->seek_commit > 0) {
                char t[32]; ui_format_time(shown_pos, t, sizeof t);
                int ch = chapter_at(p, shown_pos);
                const char *cname = ch >= 0 && p->probe->chapters[ch].name[0] ? p->probe->chapters[ch].name : NULL;
                float tw = font_measure(t, FONT_M | FONT_B) + 36, cw = cname ? font_measure(cname, FONT_XS | FONT_B) + 36 : 0;
                if (cw > tw) tw = cw;
                float bh = cname ? 78 : 52;
                float tx = kx - tw / 2;
                if (tx < bx) tx = bx;
                if (tx + tw > bx + bw) tx = bx + bw - tw;
                int pw = 0, ph = 0;
                unsigned ptex = seek_thumb(p, shown_pos, &pw, &ph, 1);
                if (ptex && pw > 0) {   /* the frame sits above the bubble */
                    float fw2 = 360, fh2 = fw2 * ph / pw, fx = kx - fw2 / 2;
                    if (fh2 > 240) { fh2 = 240; fw2 = fh2 * pw / ph; fx = kx - fw2 / 2; }
                    if (fx < bx) fx = bx;
                    if (fx + fw2 > bx + bw) fx = bx + bw - fw2;
                    float fy = by - 30 - bh - 16 - fh2;
                    if (!g_app.fx_light) gfx_shadow(fx, fy + 6, fw2, fh2, R_PANEL, 30, RGBA(0, 0, 0, 0xA0));
                    gfx_rtex_uv(ptex, fx, fy, fw2, fh2, 16, 0, 0, 1, 1, RGBA(255, 255, 255, 255), 0);
                    gfx_rrect_ring(fx, fy, fw2, fh2, R_PANEL, 2, THEME_TEXT);
                }
                gfx_rrect(tx, by - 30 - bh, tw, bh, theme_pill_r(bh), THEME_FOCUS);
                font_draw(t, tx + (tw - font_measure(t, FONT_M | FONT_B)) / 2, by - 30 - bh + 10, FONT_M | FONT_B, THEME_ON_FOCUS);
                if (cname) font_draw(cname, tx + (tw - font_measure(cname, FONT_XS | FONT_B)) / 2, by - 30 - bh + 48, FONT_XS | FONT_B, COLOR_ALPHA(THEME_ON_FOCUS, 0xB0));
            }
        }
        char pos[32], rem[48], t2[32];
        ui_format_time(shown_pos, pos, sizeof pos);
        ui_format_time(vi.duration - shown_pos, t2, sizeof t2);
        snprintf(rem, sizeof rem, vi.duration > 0 ? "-%s" : "--:--", t2);
        font_draw(pos, bx, by + 24, FONT_S | FONT_B, THEME_TEXT);
        float rw = font_measure(rem, FONT_S | FONT_B);
        font_draw(rem, bx + bw - rw, by + 24, FONT_S | FONT_B, THEME_TEXT_DIM);
        if (p->rate_idx != RATE_NORMAL && overlay) {
            char sp[16]; snprintf(sp, sizeof sp, "%gx", RATES[p->rate_idx]);
            ui_chip(sp, bx + bw - rw - 28 - font_measure(sp, FONT_XS | FONT_B) - 24, by + 20, COLOR_ALPHA(THEME_ACCENT2, 0xC0), THEME_TEXT);
        }
    }
    if (overlay) {
        draw_bar(p, bx, by + 66);
    }
    if (p->stats && !p->menu) draw_stats(p, &vi);
    if (p->up_next_shown && p->next_video_id && !p->menu) draw_up_next(p, &vi, overlay);
    const SkipSeg *sk = g_app.skip_mode == 0 && !p->menu ? skippable(p, &vi) : NULL;
    if (sk) draw_skip_button(p, sk, &vi, overlay);
    if (p->menu) draw_menu(p);
}

static int menu_rows(Player *p)
{
    if (p->menu == MENU_STYLE) return SUBS_STYLE_COUNT + 3;
    if (p->menu == MENU_SOURCES) return p->n_cands + 1;
    if (p->menu == MENU_EPISODES) { int n_eps = 0; app_videos(p->meta.id, &n_eps); return n_eps; }
    VideoTrack tr[MAX_TRACKS];
    int n = video_tracks(p->menu == MENU_AUDIO ? TRACK_AUDIO : TRACK_TEXT, tr, MAX_TRACKS);
    if (p->menu == MENU_AUDIO) return n + 1;
    return 2 + n + p->n_addon_subs + (p->track_loaded ? 1 : 0) + 1;
}

static void menu_activate(Player *p)
{
    if (p->menu == MENU_STYLE) {
        if (p->menu_focus < SUBS_STYLE_COUNT) g_app.subs_style = p->menu_focus; else g_app.subs_size = p->menu_focus - SUBS_STYLE_COUNT;
        app_save_prefs();
        return;
    }
    if (p->menu == MENU_SOURCES) {
        VideoInfo vi;
        video_info(&vi);
        if (p->menu_focus != p->cur && p->menu_focus < p->n_cands) { app_toast("Switching source"); start_candidate(p, p->menu_focus, vi.position); }
        return;
    }
    if (p->menu == MENU_EPISODES) {
        int n_eps = 0;
        const MetaVideo *eps = app_videos(p->meta.id, &n_eps);
        if (p->menu_focus < n_eps && strcmp(eps[p->menu_focus].id, p->video_id) != 0) {
            char t[200];
            if (eps[p->menu_focus].season >= 0 && eps[p->menu_focus].episode >= 0) snprintf(t, sizeof t, "S%d E%d - %s", eps[p->menu_focus].season, eps[p->menu_focus].episode, eps[p->menu_focus].title ? eps[p->menu_focus].title : "");
            else snprintf(t, sizeof t, "%s", eps[p->menu_focus].title ? eps[p->menu_focus].title : "");
            go_to_video(p, eps[p->menu_focus].id, t);
        }
        return;
    }
    VideoTrack tr[MAX_TRACKS];
    int n = video_tracks(p->menu == MENU_AUDIO ? TRACK_AUDIO : TRACK_TEXT, tr, MAX_TRACKS);
    int f = p->menu_focus;
    if (p->menu == MENU_AUDIO) {
        if (f == 0) { set_rate(p, p->rate_idx + 1 < N_RATES ? p->rate_idx + 1 : 0); return; }
        f--;
        if (f < n) { video_select_track(TRACK_AUDIO, tr[f].index); remember_audio(p, &tr[f]); }
        return;
    }
    p->subs_auto_done = 1;   /* the viewer decided; stop auto-selecting */
    if (f == 0) { p->assign_second = !p->assign_second; return; }
    f--;
    if (f == 0) {
        if (p->assign_second) { select_addon_sub_slot(p, -1, 1); return; }
        select_addon_sub(p, -1); p->embedded_text_sel = -1; video_select_track(TRACK_TEXT, -1); remember_subs(p, NULL, SUBS_SRC_ANY); return;
    }
    f--;
    if (f < n) {
        if (p->assign_second) { app_toast("Embedded tracks are drawn by the TV; pick an addon file as the second subtitles"); return; }
        select_addon_sub(p, -1); p->embedded_text_sel = tr[f].index; video_select_track(TRACK_TEXT, tr[f].index);
        remember_subs(p, tr[f].lang, SUBS_SRC_EMBEDDED);
        return;
    }
    f -= n;
    int order[MAX_ADDON_SUBS];
    int ng = sorted_subs(p, order);
    if (f < ng) {
        int idx = order[f];
        if (p->assign_second) { select_addon_sub_slot(p, p->active_addon_sub2 == idx ? -1 : idx, 1); return; }
        select_addon_sub(p, idx);
        remember_subs(p, p->addon_subs[idx].lang, SUBS_SRC_ADDON);
        return;
    }
    f -= ng + (p->track_loaded ? 1 : 0);
    if (f == 0) { p->menu = MENU_STYLE; p->menu_focus = g_app.subs_style; }   /* "Style and size" */
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
    p->seek_flash = g_app.time + 0.35f; p->seek_flash_dir = base < 0 ? -1 : 1;
    if (g_app.live_scrub && g_app.time - p->last_live_seek > 0.33f) {   /* the picture follows while the key is held */
        p->last_live_seek = g_app.time;
        video_seek(target);
    }
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
        case KEY_LEFT: case KEY_RIGHT: {
            int dir = a == KEY_RIGHT ? 1 : -1;
            if (p->menu == MENU_AUDIO && p->menu_focus == 0) { set_rate(p, p->rate_idx + dir); app_redraw(); return 1; }
            if (p->menu != MENU_SUBS) return 1;
            VideoTrack tt[MAX_TRACKS];
            int nt = video_tracks(TRACK_TEXT, tt, MAX_TRACKS);
            if (p->menu_focus == 0) { p->assign_second = !p->assign_second; app_redraw(); return 1; }
            int g = p->menu_focus - 2 - nt;
            if (g == p->n_addon_subs && p->track_loaded) p->track.offset += 0.5 * dir;   /* the timing row */
            app_redraw();
            return 1;
        }
        case KEY_OK:
            if (p->menu == MENU_SOURCES && p->menu_focus == p->n_cands) { browse_sources(p); return 1; }
            { int was = p->menu; int keep = (was == MENU_AUDIO && p->menu_focus == 0) || (was == MENU_SUBS && (p->menu_focus == 0 || p->assign_second));
              menu_activate(p); if (p->menu == was && !keep) p->menu = MENU_NONE; show_overlay(p); return 1; }
        case KEY_BACK: p->menu = MENU_NONE; show_overlay(p); return 1;
        default: return 1;
        }
    }
    if (error_final(p, &vi) && a == KEY_OK) { app_push(screen_diagnostics_create(1)); return 1; }
    int overlay_on = g_app.time < p->overlay_until || vi.state == VIDEO_PAUSED;
    if (p->bar_focus && overlay_on) {   /* the button row has the focus */
        int ids[BTN_COUNT]; const char *labels[BTN_COUNT]; Icon icons[BTN_COUNT]; char src[32];
        int n = bar_buttons(p, ids, labels, icons, src, sizeof src);
        switch (a) {
        case KEY_LEFT: if (p->bar_index > 0) p->bar_index--; show_overlay(p); return 1;
        case KEY_RIGHT: if (p->bar_index < n - 1) p->bar_index++; show_overlay(p); return 1;
        case KEY_UP: case KEY_BACK: p->bar_focus = 0; show_overlay(p); return 1;
        case KEY_DOWN: show_overlay(p); return 1;
        case KEY_OK: bar_activate(p, &vi); return 1;
        default: break;   /* play/pause, colours and the rest work as usual */
        }
    }
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
    case KEY_LEFT: seek_by(p, -10); return 1;
    case KEY_RIGHT: seek_by(p, 10); return 1;
    case KEY_REWIND: case KEY_FORWARD:
        if (p->probe && p->probe->n_chapters >= 2) {   /* chapter to chapter */
            int k = chapter_at(p, vi.position - 3), next = a == KEY_FORWARD ? k + 1 : k;
            if (a == KEY_REWIND && k >= 0 && p->probe->chapters[k].start_s > vi.position - 3) next = k - 1;
            if (next < 0) { video_seek(0); } else if (next < p->probe->n_chapters) video_seek(p->probe->chapters[next].start_s);
            else return 1;
            show_overlay(p);
            if (next >= 0 && next < p->probe->n_chapters && p->probe->chapters[next].name[0]) app_toast("%s", p->probe->chapters[next].name);
            return 1;
        }
        seek_by(p, a == KEY_FORWARD ? 60 : -60);
        return 1;
    case KEY_UP: p->bar_focus = 0; show_overlay(p); return 1;
    case KEY_DOWN: p->bar_focus = 1; show_overlay(p); return 1;
    case KEY_INFO: case KEY_MENU: show_overlay(p); return 1;
    case KEY_GREEN: if (p->next_video_id) go_up_next(p); return 1;
    case KEY_RED: p->stats = !p->stats; app_redraw(); return 1;
    case KEY_YELLOW: if (p->cur + 1 < p->n_cands) { app_toast("Switching source"); start_candidate(p, p->cur + 1, vi.position); } return 1;
    case KEY_BACK:
        if (g_app.pip_enabled && (vi.state == VIDEO_PLAYING || vi.state == VIDEO_PAUSED || vi.state == VIDEO_LOADING) && g_app.depth > 1) { app_pip_enter(); return 1; }
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
    subs_free(&p->track2);
    free(p->probe);
    for (int i = 0; i < p->n_addon_subs; i++) { free(p->addon_subs[i].url); free(p->addon_subs[i].lang); free(p->addon_subs[i].addon); }
    play_candidates_free(p->cands, p->n_cands);
    meta_preview_free(&p->meta);
    free(p->video_id); free(p->title); free(p->next_video_id); free(p->next_title); free(p->prev_video_id); free(p->prev_title);
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
    {
        char pid[128], pt[200];
        if (app_adjacent_video(meta->id, video_id, -1, pid, sizeof pid, pt, sizeof pt)) { p->prev_video_id = strdup(pid); p->prev_title = strdup(pt); }
    }
    p->rate_idx = RATE_NORMAL;
    p->active_addon_sub = -1;
    p->active_addon_sub2 = -1;
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
        if (p->aspect != ASPECT_FIT) apply_aspect(p);
        show_overlay(p);
        LOGI("player: back to full screen");
    }
}

void player_pip_update(Screen *s, float dt) { update(s, dt); }
void player_reload_at(Screen *s, double position_s) { Player *p = s->st; start_candidate(p, p->cur, position_s); }
int player_pip_done(Screen *s) { return ((Player *)s->st)->pip_done; }
const char *player_video_id(Screen *s) { return ((Player *)s->st)->video_id; }

void player_pip_draw(Screen *s, int focused)
{
    Player *p = s->st;
    VideoInfo vi;
    video_info(&vi);
    float x, y, w, h;
    pip_rect(&x, &y, &w, &h);
    if (!g_app.fx_light) gfx_shadow(x, y, w, h, 4, 44, RGBA(0, 0, 0, 0xC0));
    draw_video_area(p, x, y, w, h);
    if (focused) { if (!g_app.fx_light) gfx_shadow(x - 4, y - 4, w + 8, h + 8, 6, 30, COLOR_ALPHA(THEME_ACCENT2, 0xA0)); gfx_rrect_ring(x, y, w, h, 2, 4, THEME_TEXT); }
    else gfx_rrect_ring(x, y, w, h, 2, 2, COLOR_ALPHA(THEME_TEXT, 0x50));
    if (focused) {   /* two choices, for remotes without a Stop key */
        float bw1 = ui_pill_width("Full screen", 1, 52), bw2 = ui_pill_width("Close", 1, 52);
        float bx = x + (w - bw1 - bw2 - 16) / 2, by = y + h / 2 - 26;
        ui_pill("Full screen", 1, ICON_PLAY, bx, by, 52, PILL_SECONDARY, focused == 1);
        ui_pill("Close", 1, ICON_BACK, bx + bw1 + 16, by, 52, PILL_SECONDARY, focused == 2);
    }
    gfx_gradient_v(x, y + h - 96, w, 96, RGBA(0, 0, 0, 0), RGBA(0, 0, 0, 0xC8));
    font_draw_fit(p->title, x + 18, y + h - 64, FONT_XS | FONT_B, THEME_TEXT, w - 36);
    if (vi.duration > 0) ui_progress(x + 18, y + h - 20, w - 36, 4, (float)(vi.position / vi.duration), THEME_ACCENT);
    float cx = x + w / 2, cy = y + h / 2 - 10;
    if (focused) { /* the buttons take the middle */ }
    else if (vi.state == VIDEO_PAUSED) { gfx_circle(cx, cy, 34, RGBA(0, 0, 0, 0x90)); ui_icon(ICON_PLAY, cx + 3, cy, 30, THEME_TEXT); }
    else if (vi.state == VIDEO_LOADING || vi.buffering) ui_spinner(cx, cy, 20);
    ui_chip(focused ? "Left/Right: choose  \xC2\xB7  OK: select  \xC2\xB7  Up: back" : "Down: select  \xC2\xB7  Play/Pause: full screen", x + 12, y + 12, RGBA(0, 0, 0, 0x90), focused ? THEME_TEXT : THEME_TEXT_DIM);
    if (vi.state == VIDEO_PLAYING) app_redraw();
}
