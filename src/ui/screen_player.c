/* Player: transparent hole for the platform video plane, overlay controls,
 * addon + embedded subtitles, audio/subtitle menu, auto-fallback across stream
 * candidates, intro/recap skipping, Up Next at the credits, and progress sync to
 * the Stremio library. */
#include "../app.h"
#include "../http.h"
#include "../image.h"
#include "../frames.h"
#include "../phone.h"
#include "../torrent.h"
#include "../lang.h"
#include "../log.h"
#include "../probe.h"
#include "../tmdb.h"
#include "../ai.h"
#include "../../vendor/cJSON.h"
#include "../skip.h"
#include "../store.h"
#include "../subs.h"
#include "../trackmem.h"
#include "../video/video.h"
#include "../platform/platform.h"
#include "font.h"
#include "theme.h"
#include "widgets.h"
#include "island.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

enum { MAX_ADDON_SUBS = 64, MAX_TRACKS = 16 };
enum { MENU_NONE, MENU_AUDIO, MENU_SUBS, MENU_SOURCES, MENU_EPISODES };
/* Subtitles menu rows: the controls first, then Off and the tracks */
enum { SUB_CHOOSE, SUB_TIMING, SUB_SIZE, SUB_STYLE, SUB_POS, SUB_SYNC, SUB_OFF, SUB_TRACKS };
enum { BTN_PREV, BTN_PLAY, BTN_NEXT, BTN_RELOAD, BTN_SUBS, BTN_AUDIO, BTN_ASPECT, BTN_SOURCES, BTN_EPISODES, BTN_STATS, BTN_COUNT };
enum { ASPECT_FIT, ASPECT_FILL, ASPECT_ZOOM, ASPECT_COUNT };
static const char *const ASPECT_NAMES[ASPECT_COUNT] = { "Fit", "Fill the screen", "Zoom" };
static const double RATES[] = { 0.5, 0.75, 1.0, 1.25, 1.5, 1.75, 2.0 };
enum { N_RATES = 7, RATE_NORMAL = 2 };

typedef struct AddonSub { char *url, *lang, *addon; int matched, sync_failed; } AddonSub;

typedef struct Player Player;
typedef struct { Player *owner; unsigned gen; int addon; int slot; int hashed; } SubReq;

struct Player {
    MetaPreview meta;
    char *video_id, *title;
    PlayCandidate *cands; int n_cands, cur;
    int tries;
    float p2p_recheck_in;     /* playing P2P: when to ask the addons again whether TorBox has it */
    float p2p_stats_in; unsigned p2p_stats_gen;
    char p2p_status[96];      /* "P2P  ·  14 peers  ·  3.2 MB/s" */
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
    double seek_origin;                      /* where the playhead was when the seek began (Back returns there) */
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
    /* OpenSubtitles-style file hash -> subtitles matched to this exact file -> auto-sync */
    int hash_state;           /* 0 not started, 1 head, 2 tail, 3 known, 4 unavailable */
    uint64_t os_sum, os_size; char os_hash[24];
    SubTrack ref; int ref_loaded, ref_idx;   /* a matched subtitle used as the timing reference */
    int sync_for, sync_state; double sync_offset;   /* sync_state: 0 idle, 1 aligning, 2 synced, 3 no match */
    /* AI translation of the active addon file into the preferred language, a batch at a time, playhead first */
    int xl_on, xl_hinted; char xl_lang[8]; unsigned char *xl_done; int xl_n_done, xl_fails; unsigned xl_req; unsigned xl_gen; int xl_from, xl_count; float xl_wait;
    float seek_flash; int seek_flash_dir;   /* the ±10 s mark flashes on the side that seeks */
    int seek_flash_step;                    /* seconds the last press moved (grows while the key is held) */
    float rest_t;                           /* paused and left alone: the controls fade, the title stays */
    int trailer;                            /* video_id "trailer:…": nothing is saved against the title */
    int lighter_to; int lighter_offered;    /* buffering keeps stopping: a lower resolution offered (-1 none) */
    int stalls_here;                        /* stalls since this source started */
    float menu_t; int menu_seen;           /* the side menu slides in */
    int embedded_text_sel;    /* -1 off */
    double embedded_offset;   /* timing for the TV's own subtitles, seconds (positive = later) */
    int auto_selected;        /* audio preference applied */
    int subs_auto_done;       /* subtitle preference applied (or given up) */
    int subs_user;            /* the viewer chose subtitles by hand this session */
    int sync_retries, sync_first;
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
    int bar_focus, bar_id;     /* Down moves from the seek bar to the button row; the button by BTN_* id */
    int skip_focus;            /* Up from the timeline focused the Skip button (the controls are up) */
    float play_flash;          /* big centre icon after play/pause */
    int flash_icon;
};

static Player *g_live;

static void show_overlay(Player *p) { p->overlay_until = g_app.time + 4.0f; app_redraw(); }
#define PAUSE_REST 6.0f   /* seconds paused and untouched before the controls step aside */
static void url_host(const char *url, char *out, size_t n);
static void go_up_next(Player *p);
static void push_sub_style(const Player *p);
static int pick_forced(Player *p);
static void autosync_tick(Player *p);
static void xl_stop(Player *p);
static int source_view(const Player *p, int *view);
static void cand_addon(const PlayCandidate *c, char *out, size_t n);
static void describe_track(const Player *p, int kind, const VideoTrack *tr, int i, int n, char *title, size_t tn, char *sub, size_t sn);
static int xl_available(Player *p);
static void xl_start(Player *p);
static int error_final(const Player *p, const VideoInfo *vi);

static void report(Player *p, int force)
{
    VideoInfo vi;
    video_info(&vi);
    if (vi.state == VIDEO_IDLE || vi.state == VIDEO_LOADING || vi.state == VIDEO_ERROR) return;
    if (!force && vi.position == p->last_reported) return;
    if (p->trailer) return;
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
        if (q->hashed) {
            /* the answer to the file-hash query: files the plain query did not know are
             * matched to this exact file; an addon that ignores the hash answers the same */
            int fresh = 0;
            for (int i = 0; i < n; i++) { int seen = 0; for (int k = 0; k < p->n_addon_subs; k++) if (strcmp(p->addon_subs[k].url, subs[i].url) == 0) seen = 1; if (!seen) fresh++; }
            if (fresh) {
                for (int i = 0; i < n; i++) {
                    int k; for (k = 0; k < p->n_addon_subs; k++) if (strcmp(p->addon_subs[k].url, subs[i].url) == 0) break;
                    if (k < p->n_addon_subs) { p->addon_subs[k].matched = 1; continue; }
                    if (p->n_addon_subs >= MAX_ADDON_SUBS) break;
                    AddonSub *as = &p->addon_subs[p->n_addon_subs++];
                    as->url = strdup(subs[i].url); as->lang = strdup(subs[i].lang ? subs[i].lang : "und"); as->addon = strdup(addon_name); as->matched = 1;
                }
                LOGI("subs: %d file-matched subtitle(s) from %s", fresh, addon_name);
                if (!p->subs_user) p->subs_auto_done = 0;   /* a matched file in the preferred language beats the earlier pick */
            }
        } else
        for (int i = 0; i < n && p->n_addon_subs < MAX_ADDON_SUBS; i++) {
            AddonSub *as = &p->addon_subs[p->n_addon_subs++];
            as->url = strdup(subs[i].url);
            as->lang = strdup(subs[i].lang ? subs[i].lang : "und");
            as->addon = strdup(addon_name);
            as->matched = 0;
        }
        subtitles_free(subs, n);
        app_redraw();
    }
    free(q);
}

static void query_addon_subs_ex(Player *p, int hashed)
{
    if (p->trailer) return;
    const PlayCandidate *c = &p->cands[p->cur];
    char extra[700] = "";
    char size[32];
    uint64_t vsize = hashed && p->os_size ? p->os_size : c->video_size;
    snprintf(size, sizeof size, "%llu", (unsigned long long)vsize);
    const char *kv[8]; int k = 0;
    if (c->filename) { kv[k++] = "filename"; kv[k++] = c->filename; }
    if (vsize) { kv[k++] = "videoSize"; kv[k++] = size; }
    if (hashed) { kv[k++] = "videoHash"; kv[k++] = p->os_hash; }
    kv[k] = NULL;
    if (k) addon_extra_query(extra, sizeof extra, kv);
    for (int i = 0; i < g_app.n_addons; i++) {
        if (!addon_supports(&g_app.addons[i], "subtitles", p->meta.type, p->video_id)) continue;
        char url[1400];
        addon_url_subtitles(&g_app.addons[i], p->meta.type, p->video_id, extra[0] ? extra : NULL, url, sizeof url);
        SubReq *q = malloc(sizeof *q);
        q->owner = p; q->gen = p->gen; q->addon = i; q->slot = 0; q->hashed = hashed;
        p->subs_pending++;
        http_request_async(url, NULL, on_addon_subs, q);
    }
}
static void query_addon_subs(Player *p) { query_addon_subs_ex(p, 0); }

/* ---- the file's OpenSubtitles hash: size + the first and last 64 KB summed as little-endian
 * 64-bit words. Subtitle addons answer a videoHash query with files timed for this exact file. */
static uint64_t sum64(const uint8_t *d, size_t n)
{
    uint64_t s = 0;
    for (size_t i = 0; i + 8 <= n; i += 8) {
        uint64_t w = 0;
        for (int b = 7; b >= 0; b--) w = (w << 8) | d[i + b];
        s += w;
    }
    return s;
}

static void on_hash_part(HttpResponse *r, void *user)
{
    SubReq *q = user;
    Player *p = q->owner;
    int tail = q->slot;
    free(q);
    if (p != g_live) return;
    if ((r->status != 200 && r->status != 206) || r->len < 8) { p->hash_state = 4; LOGI("subs: file hash unavailable (%ld)", r->status); return; }
    p->os_sum += sum64((const uint8_t *)r->body, r->len);
    if (!tail) {
        p->os_size = r->total_size ? r->total_size : (uint64_t)r->len;
        if (p->os_size < 131072) { p->hash_state = 4; return; }
        p->hash_state = 2;
        SubReq *q2 = malloc(sizeof *q2);
        q2->owner = p; q2->gen = p->gen; q2->addon = -1; q2->slot = 1; q2->hashed = 0;
        HttpOptions opt = { .range_bytes = 65536, .range_start = p->os_size - 65536, .timeout_s = 8, .no_json_accept = 1 };
        http_request_async_ex(p->cands[p->cur].url, &opt, on_hash_part, q2);
        return;
    }
    uint64_t h = p->os_size + p->os_sum;
    snprintf(p->os_hash, sizeof p->os_hash, "%016llx", (unsigned long long)h);
    p->hash_state = 3;
    LOGI("subs: file hash %s (%llu bytes), asking for matched subtitles", p->os_hash, (unsigned long long)p->os_size);
    query_addon_subs_ex(p, 1);
}

static void hash_start(Player *p)
{
    p->hash_state = 1; p->os_sum = 0; p->os_size = 0;
    if (strncmp(p->cands[p->cur].url, "http", 4) != 0) { p->hash_state = 4; return; }
    SubReq *q = malloc(sizeof *q);
    q->owner = p; q->gen = p->gen; q->addon = -1; q->slot = 0; q->hashed = 0;
    HttpOptions opt = { .range_bytes = 65536, .range_start = 0, .timeout_s = 8, .no_json_accept = 1 };
    http_request_async_ex(p->cands[p->cur].url, &opt, on_hash_part, q);
}

static void on_sub_file(HttpResponse *r, void *user)
{
    SubReq *q = user;
    Player *p = q->owner;
    if (p != g_live || q->gen != p->gen) { free(q); return; }
    SubTrack *t = q->slot == 2 ? &p->ref : q->slot ? &p->track2 : &p->track;
    int *loaded = q->slot == 2 ? &p->ref_loaded : q->slot ? &p->track2_loaded : &p->track_loaded;
    if (!q->slot) p->sub_req = 0;
    subs_free(t);
    *loaded = 0;
    if (r->status == 200 && subs_parse(r->body, r->len, t) > 0) {
        *loaded = 1;
        LOGI("subs: loaded %d cues%s", t->n, q->slot == 2 ? " (timing reference)" : q->slot ? " (second subtitles)" : "");
    } else if (q->slot == 2) {
        p->sync_state = 3;
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
        p->sync_for = -1; p->sync_state = 0;
        xl_stop(p);
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
    if (p->trailer) return NULL;
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
static int strcasestr_ci(const char *hay, const char *needle)
{
    if (!hay || !needle) return 0;
    size_t n = strlen(needle);
    for (; *hay; hay++) if (strncasecmp(hay, needle, n) == 0) return 1;
    return 0;
}

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
            for (int pass = 0; pass < 2; pass++)   /* a file matched to this video first */
                for (int i = 0; i < p->n_addon_subs; i++)
                    if (lang_same(p->addon_subs[i].lang, lang) && (pass || p->addon_subs[i].matched)) { select_addon_sub(p, i); return 1; }
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
        if (want[0]) {
            /* in that language, a track this TV surely plays (not DTS / TrueHD) first */
            int pick = -1;
            for (int pass = 0; pass < 2 && pick < 0; pass++)
                for (int i = 0; i < n; i++) {
                    if (!lang_same(tr[i].lang, want)) continue;
                    int risky = strcasestr_ci(tr[i].codec, "dts") || strcasestr_ci(tr[i].codec, "truehd") || strcasestr_ci(tr[i].label, "dts") || strcasestr_ci(tr[i].label, "truehd");
                    if (pass == 0 && risky) continue;
                    pick = i; break;
                }
            if (pick >= 0 && !tr[pick].selected) { video_select_track(TRACK_AUDIO, tr[pick].index); for (int k = 0; k < n; k++) tr[k].selected = k == pick; }
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
    if (p->subs_pending == 0 || g_app.time - p->load_started > 15) {
        p->subs_auto_done = 1;
        if (!has_mem && g_app.pref_subs[0] && xl_available(p) && !p->xl_hinted) {   /* nobody offers the language: translate, or say how */
            p->xl_hinted = 1;
            if (g_app.subs_autotranslate) xl_start(p);
            else app_toast("No %s subtitles from your addons. Subtitles menu: Translate with AI", lang_display(g_app.pref_subs));
        }
    }
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
            int ma = p->addon_subs[order[j]].matched, mb = p->addon_subs[order[j - 1]].matched;
            int cmp = strcasecmp(lang_display(la), lang_display(lb));
            int c = a > b || (a == b && (cmp < 0 || (cmp == 0 && ma > mb)));
            if (c) { int t = order[j]; order[j] = order[j - 1]; order[j - 1] = t; } else break;
        }
    return n;
}

/* ---- AI translation: the chosen file, cue by cue, into the language nobody offered -- */

static int xl_source(Player *p)
{
    int any = -1;
    for (int i = 0; i < p->n_addon_subs; i++) {
        if (lang_same(p->addon_subs[i].lang, "eng")) return i;
        if (any < 0) any = i;
    }
    return any;
}

/* 1 when the Subtitles menu should offer "Translate to X": AI on, a preferred language,
 * subtitle files to translate from, and nothing already in that language. */
static int xl_available(Player *p)
{
    if (!ai_enabled() || !lang_known(g_app.pref_subs) || p->n_addon_subs == 0) return 0;
    for (int i = 0; i < p->n_addon_subs; i++) if (lang_same(p->addon_subs[i].lang, g_app.pref_subs)) return 0;
    VideoTrack tr[MAX_TRACKS];
    int n = video_tracks(TRACK_TEXT, tr, MAX_TRACKS);
    for (int i = 0; i < n; i++) if (lang_same(tr[i].lang, g_app.pref_subs)) return 0;
    return 1;
}

static void xl_stop(Player *p)
{
    if (p->xl_req) ai_cancel(p->xl_req);
    p->xl_req = 0; p->xl_on = 0; p->xl_gen++;
    free(p->xl_done); p->xl_done = NULL; p->xl_n_done = 0; p->xl_fails = 0;
}

typedef struct { Player *owner; unsigned gen; int from, count; } XlReq;

static void on_xl_batch(const char *text, const char *error, void *user)
{
    XlReq *q = user;
    Player *p = q->owner;
    int from = q->from, count = q->count; unsigned gen = q->gen;
    free(q);
    if (p != g_live || p->xl_on != 1 || gen != p->xl_gen) return;
    if (!p->xl_done || from + count > p->track.n) return;
    p->xl_req = 0;
    cJSON *root = text ? cJSON_Parse(text) : NULL;
    const cJSON *lines = root ? (cJSON_IsArray(root) ? root : cJSON_GetObjectItemCaseSensitive(root, "lines")) : NULL;
    int got = cJSON_IsArray(lines) ? cJSON_GetArraySize(lines) : 0;
    if (got) {
        int k = 0;
        for (int i = from; i < from + count && k < got; i++) {
            if (p->xl_done[i]) continue;
            const cJSON *it = cJSON_GetArrayItem(lines, k++);
            if (cJSON_IsString(it) && it->valuestring[0]) { free(p->track.cues[i].text); p->track.cues[i].text = strdup(it->valuestring); }
            p->xl_done[i] = 1; p->xl_n_done++;
        }
        p->xl_fails = 0;
        p->xl_wait = 0.4f;   /* a breath between batches: free tiers count requests per minute */
        if (p->xl_n_done >= p->track.n) { app_toast("Subtitles translated to %s", lang_display(p->xl_lang)); p->xl_on = 2; }
    } else {
        p->xl_fails++;
        p->xl_wait = p->xl_fails < 3 ? 6.0f : 30.0f;
        if (p->xl_fails == 3) app_toast("%s", error ? error : "The translation stalled; retrying in a while");
        LOGI("subs: translation batch failed (%s)", error ? error : "unreadable answer");
    }
    if (root) cJSON_Delete(root);
    app_redraw();
}

static void xl_tick(Player *p, float dt, double position)
{
    if (p->xl_on != 1 || !p->track_loaded || p->xl_req) return;
    if (!p->xl_done) { p->xl_done = calloc((size_t)p->track.n + 1, 1); p->xl_n_done = 0; }
    if (p->xl_wait > 0) { p->xl_wait -= dt; app_redraw(); return; }
    /* the first untranslated cue at or after the playhead, else the first anywhere */
    int from = -1;
    for (int i = 0; i < p->track.n; i++) if (!p->xl_done[i] && p->track.cues[i].end + p->track.offset >= position - 2) { from = i; break; }
    if (from < 0) for (int i = 0; i < p->track.n; i++) if (!p->xl_done[i]) { from = i; break; }
    if (from < 0) { p->xl_on = 2; return; }
    cJSON *arr = cJSON_CreateArray();
    int count = 0, i = from;
    for (; i < p->track.n && count < 40; i++) { if (p->xl_done[i]) continue; cJSON_AddItemToArray(arr, cJSON_CreateString(p->track.cues[i].text ? p->track.cues[i].text : "")); count++; }
    count = i - from;
    cJSON *body = cJSON_CreateObject();
    cJSON_AddItemToObject(body, "lines", arr);
    char *msg = cJSON_PrintUnformatted(body);
    cJSON_Delete(body);
    char sys[400];
    snprintf(sys, sizeof sys, "You translate subtitle lines into %s for a TV viewer. Reply with JSON only, no markdown: {\"lines\": [...]} with exactly the same number of strings in the same order. "
             "Keep line breaks as \\n, keep names, numbers and songs, use a natural spoken register, and never merge or drop lines.", lang_display(g_app.pref_subs));
    XlReq *q = malloc(sizeof *q);
    q->owner = p; q->gen = p->xl_gen; q->from = from; q->count = count;
    p->xl_req = ai_ask(sys, msg, 2400, 1, on_xl_batch, q);
    free(msg);
    if (!p->xl_req) { free(q); p->xl_on = 0; }
}

static void xl_start(Player *p)
{
    int src = xl_source(p);
    if (src < 0) return;
    select_addon_sub(p, src);   /* loads the file; xl_tick translates it once it is here */
    p->xl_on = 1; snprintf(p->xl_lang, sizeof p->xl_lang, "%s", g_app.pref_subs);
    free(p->xl_done); p->xl_done = NULL; p->xl_n_done = 0; p->xl_fails = 0; p->xl_wait = 0;
    app_toast("Translating %s subtitles to %s\xE2\x80\xA6", lang_display(p->addon_subs[src].lang), lang_display(g_app.pref_subs));
}

/* ---- auto-sync: shift the chosen file onto a file-matched one's timing ---------- */

static int matched_reference(const Player *p)
{
    int best = -1;
    const char *lang = p->active_addon_sub >= 0 ? p->addon_subs[p->active_addon_sub].lang : NULL;
    for (int i = 0; i < p->n_addon_subs; i++) {
        if (!p->addon_subs[i].matched) continue;
        if (best < 0) best = i;
        if (lang && lang_same(p->addon_subs[i].lang, lang)) return i;   /* same language: best reference */
    }
    return best;
}

static void autosync_tick(Player *p)
{
    if (!g_app.subs_autosync || !p->track_loaded || p->active_addon_sub < 0) return;
    if (p->sync_for == p->active_addon_sub) return;   /* done or decided for this file */
    if (p->addon_subs[p->active_addon_sub].matched) { p->sync_for = p->active_addon_sub; p->sync_state = 2; p->sync_offset = 0; return; }
    int ref = matched_reference(p);
    if (ref < 0) { if (p->hash_state >= 3 && p->subs_pending == 0) { p->sync_for = p->active_addon_sub; p->sync_state = 3; } return; }
    if (p->ref_idx != ref || (!p->ref_loaded && p->sync_state != 1)) {   /* fetch the reference */
        p->ref_idx = ref; p->sync_state = 1; p->ref_loaded = 0;
        subs_free(&p->ref);
        SubReq *q = malloc(sizeof *q);
        q->owner = p; q->gen = p->gen; q->addon = -1; q->slot = 2; q->hashed = 0;
        http_request_async(p->addon_subs[ref].url, NULL, on_sub_file, q);
        return;
    }
    if (!p->ref_loaded) return;
    double off = 0;
    p->sync_for = p->active_addon_sub;
    if (subs_align(&p->ref, &p->track, &off)) {
        p->track.offset = off; p->sync_offset = off; p->sync_state = 2;
        LOGI("subs: auto-synced %+.2f s against %s (%s)", off, p->addon_subs[ref].addon, p->addon_subs[ref].lang);
        if (fabs(off) >= 0.15) app_toast("Subtitles synced to the file (%+.1f s)", off); else app_toast("Subtitles already in sync with the file");
    } else {
        p->sync_state = 3;
        AddonSub *cur = &p->addon_subs[p->active_addon_sub];
        cur->sync_failed = 1;
        LOGI("subs: could not align %s with the matched file", cur->lang);
        /* another file in the same language, up to three tries, then back to the first */
        if (!p->subs_user && p->sync_retries < 3) {
            for (int i = 0; i < p->n_addon_subs; i++)
                if (i != p->active_addon_sub && !p->addon_subs[i].matched && !p->addon_subs[i].sync_failed && lang_same(p->addon_subs[i].lang, cur->lang)) {
                    if (p->sync_first < 0) p->sync_first = p->active_addon_sub;
                    p->sync_retries++;
                    LOGI("subs: trying another %s file (%d)", cur->lang, p->sync_retries);
                    select_addon_sub(p, i);
                    return;
                }
        }
        if (p->sync_first >= 0 && p->sync_first != p->active_addon_sub && !p->subs_user) { int f = p->sync_first; p->sync_first = -2; select_addon_sub(p, f); }
        if (p->sync_retries) app_toast("Could not sync these subtitles automatically. Subtitle timing adjusts them by hand");
    }
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
    HttpOptions opt = { .range_bytes = 2u << 20, .range_start = 0, .timeout_s = 8, .no_json_accept = 1 };
    http_request_async_ex(url, &opt, on_player_probe, q);
}

/* Seek preview: the keyframe near `t` (rounded to 10 s), decoded on the TV itself
 * (frames.c, served by phone.c), or by the home helper (tools/thumbs) when one is set.
 * Returns the texture when it has arrived, 0 meanwhile. */
static unsigned seek_thumb(const Player *p, double t, int *w, int *h, int prefetch_neighbours)
{
    if (p->cur >= p->n_cands || strncmp(p->cands[p->cur].url, "http", 4) != 0) return 0;
    char enc[1600], url[2048];
    http_url_encode(p->cands[p->cur].url, enc, sizeof enc);
    long sec = (long)(t / 10) * 10;
    if (!g_app.thumb_server || !*g_app.thumb_server) {
        char rel[1800];
        snprintf(rel, sizeof rel, "frame?t=%ld&w=384&u=%s", sec, enc);
        if (!frames_supported() || !phone_local_url(rel, url, sizeof url)) return 0;
        return image_get(url, w, h);   /* one at a time: the TV decodes them in turn */
    }
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
    if (p->trailer) return;
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
    if (p->trailer) return;   /* "trailer:<id>" is not a video any addon knows */
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
    p->hash_state = 0; p->sync_for = -1; p->sync_state = 0; p->ref_idx = -1; p->ref_loaded = 0; subs_free(&p->ref);
    p->auto_selected = 0;
    p->subs_auto_done = 0;
    p->load_started = g_app.time;
    p->stalls_here = 0; p->lighter_to = -1;
    LOGI("player: candidate %d/%d %s @%.0fs -> %s", idx + 1, p->n_cands, p->cands[idx].label ? p->cands[idx].label : "", start, p->cands[idx].url);
    p->p2p_status[0] = 0;
    if (torrent_engine_url_is(p->cands[idx].url)) {
        if (!torrent_engine_start(3000)) LOGW("player: the P2P engine did not answer");
        const char *e = getenv("SNTV_P2P_RECHECK");   /* tests */
        p->p2p_recheck_in = e ? (float)atof(e) : 120;
        p->p2p_stats_in = 0;
        app_toast("No cached copy: playing P2P on this TV. It switches back when TorBox has it.");
    }
    if (video_load(p->cands[idx].url, p->title, start) != 0) app_toast("Could not start playback");
    push_sub_style(p);   /* re-sent by the backend once the new pipeline has loaded */
    show_overlay(p);
}

/* The source to try after this one fails, or -1. After two failures in a row (TorBox
 * down, its links dead) it goes straight to the P2P fallback when there is one. */
static int next_candidate(const Player *p)
{
    for (int i = p->cur + 1; p->tries >= 2 && i < p->n_cands; i++)
        if (torrent_engine_url_is(p->cands[i].url)) return i;
    if (p->cur + 1 < p->n_cands && (p->tries < 3 || torrent_engine_url_is(p->cands[p->cur + 1].url))) return p->cur + 1;
    return -1;
}

static void fail_over(Player *p, const char *why)
{
    VideoInfo vi;
    video_info(&vi);
    double resume = vi.position > 5 ? vi.position : 0;
    if (!p->learned) { app_tv_learn(p->cands[p->cur].needs, 0); p->learned = 1; }
    if (vi.position < 5) app_link_failed(p->cands[p->cur].url);   /* never started: sort it last for six hours */
    int next = next_candidate(p);
    if (next >= 0) {
        p->tries++;
        app_toast(torrent_engine_url_is(p->cands[next].url) ? "%s. Trying P2P on this TV..." : "%s. Trying the next source...", why);
        start_candidate(p, next, resume);
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

/* ---- P2P fallback: how it goes, and back to TorBox when it can ---------------------- */

typedef struct { Player *owner; unsigned gen; } P2pReq;

static void on_p2p_stats(HttpResponse *r, void *user)
{
    P2pReq *q = user;
    Player *p = q->owner;
    unsigned gen = q->gen;
    free(q);
    if (p != g_live || gen != p->p2p_stats_gen || p->cur >= p->n_cands || !torrent_engine_url_is(p->cands[p->cur].url)) return;
    cJSON *root = r->status == 200 ? cJSON_Parse(r->body) : NULL;
    const char *hash = p->cands[p->cur].url + strlen("http://127.0.0.1:11490/");
    const cJSON *t;
    cJSON_ArrayForEach(t, cJSON_GetObjectItemCaseSensitive(root, "torrents")) {
        const cJSON *h = cJSON_GetObjectItemCaseSensitive(t, "hash");
        if (!cJSON_IsString(h) || strncmp(h->valuestring, hash, 40)) continue;
        int peers = cJSON_GetObjectItemCaseSensitive(t, "peers") ? cJSON_GetObjectItemCaseSensitive(t, "peers")->valueint : 0;
        double bps = cJSON_GetObjectItemCaseSensitive(t, "downBps") ? cJSON_GetObjectItemCaseSensitive(t, "downBps")->valuedouble : 0;
        int info = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(t, "haveInfo"));
        if (!info) snprintf(p->p2p_status, sizeof p->p2p_status, "P2P  \xC2\xB7  finding peers (%d)", peers);
        else snprintf(p->p2p_status, sizeof p->p2p_status, "P2P  \xC2\xB7  %d peer%s  \xC2\xB7  %.1f MB/s", peers, peers == 1 ? "" : "s", bps / 1e6);
        static float logged;
        if (g_app.time - logged > 30) { logged = g_app.time; LOGI("p2p: %d peers, %.1f MB/s, %s", peers, bps / 1e6, info ? "streaming" : "waiting for the file list"); }
        app_redraw();
    }
    if (root) cJSON_Delete(root);
}

/* Switch to `s` (a cached copy) where the P2P one is now; P2P stays next in line. */
static void leave_p2p(Player *p, const Stream *s)
{
    PlayCandidate c;
    if (!streams_candidate(&p->meta, s, &c)) return;
    VideoInfo vi;
    video_info(&vi);
    char *old = strdup(p->cands[p->cur].url);
    p->cands = realloc(p->cands, sizeof *p->cands * (size_t)(p->n_cands + 1));
    memmove(&p->cands[p->cur + 1], &p->cands[p->cur], sizeof *p->cands * (size_t)(p->n_cands - p->cur));
    p->cands[p->cur] = c;
    p->n_cands++;
    LOGI("player: TorBox has it again, leaving P2P at %.0f s for %s", vi.position, c.label);
    app_toast("TorBox has it again: switched back to the cached copy");
    p->tries = 0;
    start_candidate(p, p->cur, vi.position);
    torrent_engine_drop(old);   /* its RAM goes now; it comes back if TorBox fails */
    free(old);
}

static void on_p2p_recheck(const Stream *items, int n, void *user)
{
    Player *p = user;
    if (p != g_live || p->finished || p->cur >= p->n_cands || !torrent_engine_url_is(p->cands[p->cur].url)) return;
    int want = p->cands[p->cur].res;
    for (int i = 0; i < n; i++) {   /* ranked best first: the first cached copy at least as sharp */
        const Stream *s = &items[i];
        if (!s->url || strncmp(s->url, "http", 4) || s->tags.cached <= 0 || app_link_dead(s->url)) continue;
        if (want && s->tags.res && s->tags.res < want) continue;
        leave_p2p(p, s);
        return;
    }
    LOGI("player: still no cached copy; staying on P2P");
}

static void p2p_tick(Player *p, const VideoInfo *vi, float dt)
{
    if (p->cur >= p->n_cands || !torrent_engine_url_is(p->cands[p->cur].url)) return;
    streams_lookup_tick();
    if ((p->p2p_stats_in -= dt) <= 0) {
        p->p2p_stats_in = 3;
        P2pReq *q = malloc(sizeof *q);
        q->owner = p; q->gen = ++p->p2p_stats_gen;
        HttpOptions o = { .timeout_s = 3 };
        http_request_async_ex("http://127.0.0.1:11490/stats.json", &o, on_p2p_stats, q);
    }
    if (vi->state != VIDEO_PLAYING && vi->state != VIDEO_PAUSED) return;
    if ((p->p2p_recheck_in -= dt) <= 0) {
        const char *e = getenv("SNTV_P2P_RECHECK");
        p->p2p_recheck_in = e ? (float)atof(e) : 120;
        streams_lookup(&p->meta, p->video_id, on_p2p_recheck, p);
    }
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
    {   /* paused and left alone for a while: lean back (the controls go, the title stays) */
        int rest = vi.state == VIDEO_PAUSED && !p->menu && p->seek_commit <= 0 && p->paused_since > 0
                && g_app.time - p->paused_since > PAUSE_REST && g_app.time > p->overlay_until + PAUSE_REST - 4;
        if (rest && p->rest_t == 0) LOGI("player: paused and idle, the controls step aside");
        if (g_app.fx_light) p->rest_t = (float)rest;
        else p->rest_t = ui_ease(p->rest_t * 100, rest * 100.0f, NULL, dt, 5) / 100;
        if (fabsf(p->rest_t - rest) > 0.005f) app_redraw(); else p->rest_t = (float)rest;
    }
    if (vi.state == VIDEO_PLAYING && !p->rate_applied && p->rate_idx != RATE_NORMAL) { video_set_rate(RATES[p->rate_idx]); p->rate_applied = 1; }
    p2p_tick(p, &vi, dt);
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
    if (!p->hash_state && (vi.state == VIDEO_PLAYING || vi.state == VIDEO_PAUSED)) hash_start(p);
    autosync_tick(p);
    xl_tick(p, dt, vi.position);
    skip_fetch(p, &vi);
    if (g_app.skip_mode == 1 && vi.state == VIDEO_PLAYING && !p->menu) { const SkipSeg *sk = skippable(p, &vi); if (sk) do_skip(p, sk); }

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
    if (p->up_next_dismissed && !at_credits && !near_end) p->up_next_dismissed = 0;   /* seeked back out of the window */
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
    if (vi.buffering && !p->was_buffering && vi.state != VIDEO_LOADING) { p->stalls++; p->stalls_here++; p->stall_since = g_app.time; }
    /* it keeps stopping to buffer: offer the best source below this one's resolution, once */
    if (!p->lighter_offered && !p->trailer && p->n_cands > 1 && vi.state != VIDEO_LOADING
        && (p->stalls_here >= 3 || (vi.buffering && p->stall_since > 0 && g_app.time - p->stall_since > 10))) {
        int cur = p->cands[p->cur].res ? p->cands[p->cur].res : 1081, best = -1;
        for (int i = 0; i < p->n_cands; i++)
            if (i != p->cur && p->cands[i].res > 0 && p->cands[i].res < cur && (best < 0 || p->cands[i].res > p->cands[best].res)) best = i;
        p->lighter_offered = 1;
        p->lighter_to = best;
        for (int i = 0; i < p->n_cands; i++) LOGI("player: source %d %s %dp", i + 1, p->cands[i].label ? p->cands[i].label : "", p->cands[i].res);
        if (best >= 0) { LOGI("player: %d stalls; offering source %d (%dp)", p->stalls_here, best + 1, p->cands[best].res); app_redraw(); }
        else LOGI("player: %d stalls; no source below %dp to offer", p->stalls_here, p->cands[p->cur].res);
    }
    if (!vi.buffering && p->was_buffering && p->stall_since > 0) { p->stall_s += g_app.time - p->stall_since; p->stall_since = 0; }
    p->was_buffering = vi.buffering;
    if (p->stats && (int)(g_app.time * 2) != (int)((g_app.time - dt) * 2)) app_redraw();
    if (p->up_next_shown && p->next_video_id && g_app.autoplay_next && p->next_at < vi.duration && vi.position >= p->next_at
        && vi.state == VIDEO_PLAYING && !p->menu) { go_up_next(p); return; }   /* never under an open menu */
    if (p->up_next_shown && !at_credits && !near_end) p->up_next_shown = 0;   /* seeked back out of its window (with or without markers) */
    if (vi.state == VIDEO_ENDED) {
        if (p->next_video_id && g_app.autoplay_next) { if (!p->menu) go_up_next(p); return; }
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
    float bottom = g_app.subs_pos == 2 ? 260 : g_app.subs_pos == 1 ? SNTV_UI_H - 220 : SNTV_UI_H - 96;
    if (g_app.subs_pos != 2 && g_app.time < p->overlay_until && bottom > SNTV_UI_H - 250) bottom = SNTV_UI_H - 250;   /* above the controls */
    if (text) ui_subtitle(text, bottom, g_app.subs_size, g_app.subs_style);
    if (text2) {   /* the second language sits above the first (or where it would be) */
        float lift = text ? font_height(ui_subtitle_font(g_app.subs_size)) * 1.04f * 2 + 24 : 0;
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
    ui_panel(x, y, w, h, R_PANEL, COLOR_ALPHA(THEME_PANEL, 0xF0));
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
    int src_sections = 0, view[64];
    if (p->menu == MENU_SOURCES) {
        source_view(p, view);
        char last[80] = "";
        for (int k = 0; k <= p->menu_focus && k < p->n_cands; k++) {
            char ad[80]; cand_addon(&p->cands[view[k]], ad, sizeof ad);
            if (ad[0] && strcmp(ad, last) != 0) { src_sections++; snprintf(last, sizeof last, "%s", ad); }
        }
    }
    int sections_before = p->menu == MENU_AUDIO ? 1 + ((n && p->menu_focus >= 1) ? 1 : 0)
                        : p->menu == MENU_SOURCES ? src_sections
                        : p->menu == MENU_EPISODES ? ep_sections
                        : p->menu == MENU_SUBS ? (p->menu_focus >= SUB_TIMING) + (p->menu_focus >= SUB_OFF) + ((p->menu_focus >= SUB_TRACKS && n) ? 1 : 0)
                                                 + ((p->menu_focus >= SUB_TRACKS + n && (ng || p->subs_pending)) ? 1 : 0)
                        : 0;
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
        for (int k = 0; k < p->n_cands; k++) {
            const PlayCandidate *c = &p->cands[view[k]];
            char host[80], addon[80], name[160];
            url_host(c->url, host, sizeof host);
            cand_addon(c, addon, sizeof addon);
            const char *lbl = c->label ? c->label : "Source", *sep = strstr(lbl, " - ");
            snprintf(name, sizeof name, "%s", sep ? sep + 3 : lbl);
            if (addon[0] && strcmp(addon, last_addon) != 0) {   /* one group per addon, in the user's addon order */
                snprintf(last_addon, sizeof last_addon, "%s", addon);
                for (char *ch = addon; *ch; ch++) if (*ch >= 'a' && *ch <= 'z') *ch -= 32;
                SECTION(addon);
            }
            ROW(c->detail ? c->detail : name, c->release ? c->release : host, view[k] == p->cur);
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
        static const char *SIZES[] = { "Small", "Medium", "Large", "Extra large" };
        static const char *POS[] = { "Bottom", "Raised", "Top" };
        char v[120];
        ROW("Choose for", p->assign_second ? "Second subtitles (above the main ones)  \xC2\xB7  Left/Right" : "Main subtitles  \xC2\xB7  Left/Right for a second language", 0);
        SECTION("LOOK AND TIMING  \xC2\xB7  LEFT/RIGHT CHANGES");
        if (p->track_loaded) snprintf(v, sizeof v, "%+.1f s  \xC2\xB7  earlier \xE2\x86\x90  \xE2\x86\x92 later  \xC2\xB7  OK resets", p->track.offset);
        else if (p->embedded_text_sel >= 0) snprintf(v, sizeof v, "%+.1f s  \xC2\xB7  earlier \xE2\x86\x90  \xE2\x86\x92 later  \xC2\xB7  OK resets", p->embedded_offset);
        else snprintf(v, sizeof v, "Turn on subtitles to adjust their timing");
        ROW("Timing", v, 0);
        snprintf(v, sizeof v, "%s", SIZES[g_app.subs_size]); ROW("Size", v, 0);
        snprintf(v, sizeof v, "%s", SUBS_STYLE_NAMES[g_app.subs_style]); ROW("Style", v, 0);
        snprintf(v, sizeof v, "%s", POS[g_app.subs_pos]); ROW("Position", v, 0);
        if (!g_app.subs_autosync) snprintf(v, sizeof v, "Off  \xC2\xB7  OK turns it on");
        else if (p->sync_state == 2 && p->active_addon_sub >= 0 && p->addon_subs[p->active_addon_sub].matched) snprintf(v, sizeof v, "On  \xC2\xB7  this file is matched to the video");
        else if (p->sync_state == 2) snprintf(v, sizeof v, "On  \xC2\xB7  synced %+.1f s to a file-matched subtitle", p->sync_offset);
        else if (p->sync_state == 1) snprintf(v, sizeof v, "On  \xC2\xB7  aligning\xE2\x80\xA6");
        else if (p->sync_state == 3) snprintf(v, sizeof v, "On  \xC2\xB7  no file-matched subtitle to align with");
        else snprintf(v, sizeof v, p->hash_state < 3 ? "On  \xC2\xB7  reading the file's fingerprint\xE2\x80\xA6" : "On  \xC2\xB7  waits for a matched subtitle");
        ROW("Auto-sync", v, 0);
        SECTION("SUBTITLES");
        ROW("Off", "", p->assign_second ? p->active_addon_sub2 < 0 : (p->embedded_text_sel < 0 && p->active_addon_sub < 0));
    }
    if (n) SECTION(p->menu == MENU_AUDIO ? "IN THIS FILE" : "IN THE VIDEO FILE");
    for (int i = 0; i < n; i++) {
        char title[96], sub[160];
        describe_track(p, p->menu == MENU_AUDIO ? 2 : 3, tr, i, n, title, sizeof title, sub, sizeof sub);
        int on = p->menu == MENU_AUDIO ? tr[i].selected : p->embedded_text_sel == tr[i].index;
        ROW(title, sub, on);
    }
    if (p->menu == MENU_AUDIO && !n) font_draw_wrapped("The stream reports a single audio track.", x + 40, ry, FONT_S, THEME_TEXT_MUTED, w - 80, 2);
    if (p->menu == MENU_SUBS) {
        if (ng || p->subs_pending) SECTION(p->subs_pending ? "DOWNLOADED BY YOUR ADDONS  (SEARCHING)" : "DOWNLOADED BY YOUR ADDONS");
        for (int g = 0; g < ng; g++) {
            int idx = order[g], nth = 0, of = 0;
            const char *ln = lang_display(p->addon_subs[idx].lang);
            for (int j = 0; j < ng; j++) if (strcmp(lang_display(p->addon_subs[order[j]].lang), ln) == 0) { of++; if (j < g) nth++; }
            char label[80], pick[40] = "";
            snprintf(label, sizeof label, "%s", ln);
            if (of > 1) snprintf(pick, sizeof pick, "  \xC2\xB7  option %d of %d", nth + 1, of);
            char who[160];
            snprintf(who, sizeof who, "%s%s%s%s", p->addon_subs[idx].addon, pick, p->addon_subs[idx].matched ? "  \xC2\xB7  matched to this file" : "", p->active_addon_sub2 == idx ? "  \xC2\xB7  second" : "");
            ROW(label, who, p->active_addon_sub == idx || p->active_addon_sub2 == idx);
        }
        if (xl_available(p)) {
            char tl[100], ts[120];
            snprintf(tl, sizeof tl, "Translate to %s", lang_display(g_app.pref_subs));
            if (p->xl_on == 1) snprintf(ts, sizeof ts, "AI translating\xE2\x80\xA6  %d of %d lines%s", p->xl_n_done, p->track.n, p->xl_fails ? "  \xC2\xB7  waiting for the service" : "");
            else if (p->xl_on == 2) snprintf(ts, sizeof ts, "Translated by AI  \xC2\xB7  %d lines", p->track.n);
            else snprintf(ts, sizeof ts, "AI, from the %s file  \xC2\xB7  nobody offers %s for this title", lang_display(p->addon_subs[xl_source(p)].lang), lang_display(g_app.pref_subs));
            ROW(tl, ts, p->xl_on != 0);
        }
    }
    gfx_clip_pop();
    gfx_set_translate(0, 0);
    #undef ROW
    #undef SECTION
}

/* "Buffering keeps stopping": the lighter source on offer, OK takes it. */
static void draw_lighter(const Player *p, int overlay)
{
    const PlayCandidate *c = &p->cands[p->lighter_to];
    float w = 640, h = 132, x = SNTV_UI_W - THEME_MARGIN - w, y = overlay ? SNTV_UI_H - 400 : SNTV_UI_H - 200;
    if (!g_app.fx_light) gfx_shadow(x, y, w, h, R_PANEL, 40, RGBA(0, 0, 0, 0xA0));
    ui_panel(x, y, w, h, R_PANEL, COLOR_ALPHA(THEME_PANEL, 0xF2));
    gfx_rrect_ring(x, y, w, h, R_PANEL, 1.5f, COLOR_ALPHA(THEME_TEXT, 0x20));
    font_draw("BUFFERING KEEPS STOPPING", x + 28, y + 22, FONT_XS | FONT_B, THEME_ACCENT2);
    char line[200], addon[96] = "";
    const char *lbl = c->label ? c->label : "", *sep = strstr(lbl, " - ");
    snprintf(addon, sizeof addon, "%.*s", sep ? (int)(sep - lbl) : (int)strlen(lbl), lbl);
    snprintf(line, sizeof line, "Try %dp%s%s, from where you are", c->res, addon[0] ? " from " : "", addon);
    font_draw_fit(line, x + 28, y + 54, FONT_S | FONT_B, THEME_TEXT, w - 56);
    font_draw("OK  switch   \xC2\xB7   Back  keep this one", x + 28, y + h - 40, FONT_XS, THEME_TEXT_DIM);
}

static void draw_up_next(Player *p, const VideoInfo *vi, int overlay)
{
    float w = 640, h = 176, x = SNTV_UI_W - THEME_MARGIN - w, y = overlay ? SNTV_UI_H - 400 : SNTV_UI_H - 240;
    if (!g_app.fx_light) gfx_shadow(x, y, w, h, R_PANEL, 40, RGBA(0, 0, 0, 0xA0));
    ui_panel(x, y, w, h, R_PANEL, COLOR_ALPHA(THEME_PANEL, 0xF2));
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
    /* OK is the button's key: framed like a focused control, with the key named beside it */
    gfx_rrect_ring(gx - 5, gy - 5, 130, 50, R_PANEL + 4, 2.5f, THEME_TEXT);
    font_draw("OK", gx - 16 - font_measure("OK", FONT_XS | FONT_B), gy + (40 - font_height(FONT_XS | FONT_B)) / 2, FONT_XS | FONT_B, THEME_TEXT_DIM);
}

static void draw_skip_button(Player *p, const SkipSeg *s, const VideoInfo *vi, int overlay)
{
    int lit = !overlay || p->skip_focus;   /* OK skips: hidden controls, or Up focused it */
    const char *label = s->kind == SKIP_RECAP ? "Skip recap" : "Skip intro";
    float h = 72, w = ui_pill_width(label, 1, h);
    float x = SNTV_UI_W - THEME_MARGIN - w, y = overlay ? SNTV_UI_H - 330 : SNTV_UI_H - 200;
    if (!g_app.fx_light) gfx_shadow(x, y, w, h, h / 2, 30, RGBA(0, 0, 0, 0x90));
    ui_pill(label, 1, ICON_NEXT, x, y, h, lit ? PILL_PRIMARY : PILL_SECONDARY, lit);
    /* time left in the segment, as a thin line under the button */
    float k = (float)((vi->position - s->start) / (s->end - s->start));
    if (k < 0) k = 0;
    if (k > 1) k = 1;
    gfx_rrect(x + h / 2, y + h + 10, (w - h) * (1 - k), 4, 2, COLOR_ALPHA(THEME_TEXT, 0x90));
    const char *keyname = lit ? "OK" : "Up";
    font_draw(keyname, x - font_measure(keyname, FONT_XS | FONT_B) - 16, y + (h - font_height(FONT_XS | FONT_B)) / 2, FONT_XS | FONT_B, THEME_TEXT_DIM);
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
    char host[128], l[18][160];
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
    if (p->hash_state == 3) snprintf(l[n++], 160, "File hash|%s%s", p->os_hash, p->sync_state == 2 ? "  \xC2\xB7  subtitles synced" : "");
    if (p->xl_on) snprintf(l[n++], 160, "AI translation|%d of %d lines  \xC2\xB7  %s", p->xl_n_done, p->track.n, ai_model());
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
    if (!p->trailer) { ids[k] = BTN_SOURCES; labels[k] = src_label; icons[k++] = ICON_SWAP; }   /* a trailer has no sources or episodes */
    if (n_eps > 1 && !p->trailer) { ids[k] = BTN_EPISODES; labels[k] = "Episodes"; icons[k++] = ICON_LIST; }
    ids[k] = BTN_STATS; labels[k] = p->stats ? "Hide stats" : "Stats"; icons[k++] = ICON_INFO;
    return k;
}

/* Where the focused button is in the row; Play when that button is gone (episode buttons come and go). */
static int bar_pos(const Player *p, const int *ids, int n)
{
    for (int i = 0; i < n; i++) if (ids[i] == p->bar_id) return i;
    for (int i = 0; i < n; i++) if (ids[i] == BTN_PLAY) return i;
    return 0;
}

/* Round icon buttons; the focused one opens into a labelled pill (like the TV apps people know). */
static void draw_bar(Player *p, float x, float y)
{
    int ids[BTN_COUNT]; const char *labels[BTN_COUNT]; Icon icons[BTN_COUNT]; char src[32];
    int n = bar_buttons(p, ids, labels, icons, src, sizeof src);
    int at = bar_pos(p, ids, n);
    const float d = 56, gap = 14;
    for (int i = 0; i < n; i++) {
        int foc = p->bar_focus && i == at;
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

/* The row of what is playing, so a menu opens on it (Off when no subtitles show). */
static int current_track_row(const Player *p, int menu)
{
    VideoTrack tr[MAX_TRACKS];
    int n = video_tracks(menu == MENU_AUDIO ? TRACK_AUDIO : TRACK_TEXT, tr, MAX_TRACKS);
    if (menu == MENU_AUDIO) {
        for (int i = 0; i < n; i++) if (tr[i].selected) return 1 + i;
        return 0;
    }
    if (p->active_addon_sub >= 0) {
        int order[MAX_ADDON_SUBS], ng = sorted_subs(p, order);
        for (int k = 0; k < ng; k++) if (order[k] == p->active_addon_sub) return SUB_OFF + 1 + n + k;
    }
    if (p->embedded_text_sel >= 0)
        for (int i = 0; i < n; i++) if (tr[i].index == p->embedded_text_sel) return SUB_OFF + 1 + i;
    return SUB_OFF;
}

/* Left/Right in the episodes menu: the first episode of the season before or after. */
static int season_jump(const Player *p, int dir)
{
    int n = 0;
    const MetaVideo *v = app_videos(p->meta.id, &n);
    int f = p->menu_focus;
    if (f < 0 || f >= n) return f;
    int s = v[f].season, i = f;
    if (dir > 0) { while (i < n && v[i].season == s) i++; return i < n ? i : f; }
    while (i > 0 && v[i - 1].season == s) i--;   /* this season's first */
    if (i == 0) return f;
    int prev = v[i - 1].season;
    while (i > 0 && v[i - 1].season == prev) i--;
    return i;
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
    if (!n) return;
    switch (ids[bar_pos(p, ids, n)]) {
    case BTN_PREV: if (p->prev_video_id) go_to_video(p, p->prev_video_id, p->prev_title); return;
    case BTN_PLAY:
        if (vi->state == VIDEO_PLAYING) video_pause(); else { video_play(); p->flash_icon = ICON_PLAY; p->play_flash = g_app.time + 0.6f; }
        break;
    case BTN_NEXT: go_up_next(p); return;
    case BTN_RELOAD: app_toast("Reloading"); start_candidate(p, p->cur, vi->position); break;
    case BTN_SUBS: p->menu = MENU_SUBS; p->menu_focus = current_track_row(p, MENU_SUBS); break;
    case BTN_AUDIO: p->menu = MENU_AUDIO; p->menu_focus = current_track_row(p, MENU_AUDIO); break;
    case BTN_ASPECT: p->aspect = (p->aspect + 1) % ASPECT_COUNT; apply_aspect(p); app_toast("Aspect: %s", ASPECT_NAMES[p->aspect]); break;
    case BTN_SOURCES: {
        int view[64]; int n = source_view(p, view);
        p->menu = MENU_SOURCES; p->menu_focus = 0;
        for (int k = 0; k < n; k++) if (view[k] == p->cur) p->menu_focus = k;
        break;
    }
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
    return vi->state == VIDEO_ERROR && p->error_handled && next_candidate(p) < 0;
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
        ui_panel(x, y, w, h, R_PANEL, THEME_PANEL);
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
        float a = vi.state == VIDEO_PAUSED ? 1.0f - p->rest_t : flash / 0.6f;
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
        char st[16];
        int sec = p->seek_flash_step > 0 ? p->seek_flash_step : 10;
        if (sec >= 60 && sec % 60 == 0) snprintf(st, sizeof st, "%dm", sec / 60); else snprintf(st, sizeof st, "%d", sec);
        font_draw(st, cx - font_measure(st, FONT_S | FONT_B) / 2, cy - font_height(FONT_S | FONT_B) / 2 + 1, FONT_S | FONT_B, fg);
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
            if (p->p2p_status[0]) { font_draw(p->p2p_status, cx, 114, FONT_S | FONT_B, THEME_ACCENT2); cx += font_measure(p->p2p_status, FONT_S | FONT_B) + 20; }
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
    if (p->rest_t > 0.001f) {   /* lean-back: what is paused and how much is left, where the bar was */
        char pos[32], rem[32], line[96];
        ui_format_time(shown_pos, pos, sizeof pos);
        ui_format_time(vi.duration - shown_pos, rem, sizeof rem);
        if (vi.duration > 0) snprintf(line, sizeof line, "%s  \xC2\xB7  %s left", pos, rem); else snprintf(line, sizeof line, "%s", pos);
        int ra = (int)(255 * p->rest_t);
        float ix = bx, iy = by - 4;
        gfx_rrect(ix, iy - 4, 6, 40, 3, COLOR_ALPHA(THEME_TEXT, ra));
        gfx_rrect(ix + 14, iy - 4, 6, 40, 3, COLOR_ALPHA(THEME_TEXT, ra));
        font_draw("Paused", ix + 40, iy - 2, FONT_M | FONT_B, COLOR_ALPHA(THEME_TEXT, ra));
        float lx = ix + 40 + font_measure("Paused", FONT_M | FONT_B) + 22;
        font_draw(line, lx, iy + (font_height(FONT_M | FONT_B) - font_height(FONT_S)) / 2 - 2, FONT_S, COLOR_ALPHA(THEME_TEXT_DIM, ra));
        const char *hint = "OK  play";
        font_draw(hint, bx + bw - font_measure(hint, FONT_S | FONT_B), iy + (font_height(FONT_M | FONT_B) - font_height(FONT_S | FONT_B)) / 2 - 2, FONT_S | FONT_B, COLOR_ALPHA(THEME_TEXT_MUTED, ra));
    }
    if (p->rest_t > 0.001f) gfx_set_alpha(1 - p->rest_t);   /* the controls step aside */
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
    gfx_set_alpha(1);
    if (p->stats && !p->menu) draw_stats(p, &vi);
    if (p->up_next_shown && p->next_video_id && !p->menu) draw_up_next(p, &vi, overlay);
    else if (p->lighter_to >= 0 && !p->menu) draw_lighter(p, overlay);
    const SkipSeg *sk = g_app.skip_mode == 0 && !p->menu ? skippable(p, &vi) : NULL;
    if (sk) draw_skip_button(p, sk, &vi, overlay);
    if (p->menu) draw_menu(p);
}

/* The fallback order decides what plays next; the menu lists sources by addon, in the
 * user's addon order. view[k] is the candidate shown on row k. */
static const char *format_name(const char *codec)
{
    static const struct { const char *id, *name; } F[] = {
        { "S_TEXT/UTF8", "Text (SRT)" }, { "S_TEXT/ASS", "Styled text (ASS)" }, { "S_TEXT/SSA", "Styled text (SSA)" },
        { "S_TEXT/WEBVTT", "Text (WebVTT)" }, { "S_HDMV/PGS", "Picture (PGS)" }, { "S_VOBSUB", "Picture (VobSub)" },
        { "S_DVBSUB", "Picture (DVB)" }, { "tx3g", "Text" }, { "wvtt", "Text (WebVTT)" },
        { "A_EAC3", "Dolby Digital+" }, { "A_AC3", "Dolby Digital" }, { "A_DTS", "DTS" }, { "A_TRUEHD", "Dolby TrueHD" },
        { "A_AAC", "AAC" }, { "A_OPUS", "Opus" }, { "A_FLAC", "FLAC" }, { "A_MPEG/L3", "MP3" }, { "ec-3", "Dolby Digital+" },
        { "ac-3", "Dolby Digital" }, { "mp4a", "AAC" }, { "dtsc", "DTS" }, { NULL, NULL } };
    for (int i = 0; F[i].id; i++) if (strncasecmp(codec, F[i].id, strlen(F[i].id)) == 0) return F[i].name;
    return codec;
}

/* A track as a viewer would name it: the language, then what the file says about it
 * (its name such as "SDH" or "Commentary", forced, default, the format). The TV's track
 * list and the header's list of the same kind are matched by position when they agree. */
static void track_base(const Player *p, int kind, const VideoTrack *tr, int i, int n, char *title, size_t tn, char *sub, size_t sn)
{
    const ProbeTrack *pt = NULL;
    if (p->probe) {
        int count = 0;
        for (int k = 0; k < p->probe->n_tracks; k++) if (p->probe->tracks[k].kind == kind) count++;
        if (count == n)
            for (int k = 0, seen = 0; k < p->probe->n_tracks; k++)
                if (p->probe->tracks[k].kind == kind && seen++ == i) { pt = &p->probe->tracks[k]; break; }
    }
    const char *lang = lang_known(tr[i].lang) ? tr[i].lang : pt && lang_known(pt->lang) ? pt->lang : NULL;
    if (lang) snprintf(title, tn, "%s", lang_display(lang));
    else if (tr[i].label[0]) snprintf(title, tn, "%s", tr[i].label);
    else snprintf(title, tn, "Track %d", i + 1);
    size_t o = 0;
    sub[0] = 0;
    #define PART(...) do { if (o < sn) { if (o) o += (size_t)snprintf(sub + o, sn - o, "  \xC2\xB7  "); if (o < sn) o += (size_t)snprintf(sub + o, sn - o, __VA_ARGS__); } } while (0)
    if (pt && pt->name[0] && strcasecmp(pt->name, title) != 0) PART("%s", pt->name);
    if (pt && pt->forced) PART("Forced");
    if (kind == 2 && pt && pt->channels) PART("%s %s", format_name(pt->codec), pt->channels == 6 ? "5.1" : pt->channels == 8 ? "7.1" : pt->channels == 2 ? "stereo" : pt->channels == 1 ? "mono" : "");
    else if (pt && pt->codec[0]) PART("%s", format_name(pt->codec));
    else if (tr[i].codec[0]) PART("%s", format_name(tr[i].codec));
    if (pt && pt->dflt) PART("Default");
    #undef PART
}

static void describe_track(const Player *p, int kind, const VideoTrack *tr, int i, int n, char *title, size_t tn, char *sub, size_t sn)
{
    track_base(p, kind, tr, i, n, title, tn, sub, sn);
    int same = 0;   /* the same description twice: number them so the rows can be told apart */
    for (int k = 0; k < i; k++) {
        char t2[96], s2[160];
        track_base(p, kind, tr, k, n, t2, sizeof t2, s2, sizeof s2);
        if (strcmp(t2, title) == 0 && strcmp(s2, sub) == 0) same++;
    }
    if (same) { size_t l = strlen(title); snprintf(title + l, tn - l, " (%d)", same + 1); }
}

static int source_view(const Player *p, int *view)
{
    int n = p->n_cands < 64 ? p->n_cands : 64;
    for (int i = 0; i < n; i++) view[i] = i;
    for (int i = 1; i < n; i++)
        for (int j = i; j > 0 && p->cands[view[j]].addon_order < p->cands[view[j - 1]].addon_order; j--) { int t = view[j]; view[j] = view[j - 1]; view[j - 1] = t; }
    return n;
}

static void cand_addon(const PlayCandidate *c, char *out, size_t n)
{
    const char *lbl = c->label ? c->label : "", *sep = strstr(lbl, " - ");
    snprintf(out, n, "%.*s", sep ? (int)(sep - lbl) : 0, lbl);
}

/* Size, style, position and timing reach the subtitles the TV draws from the file too
 * (the app draws addon subtitles itself, straight from g_app). */
static void push_sub_style(const Player *p)
{
    video_subtitle_style(g_app.subs_size, g_app.subs_style, g_app.subs_pos, (int)(p->embedded_offset * 1000));
}

static void adjust_subs(Player *p, int row, int dir)
{
    if (row == SUB_SIZE) g_app.subs_size = (g_app.subs_size + dir + 4) % 4;
    else if (row == SUB_STYLE) g_app.subs_style = (g_app.subs_style + dir + SUBS_STYLE_COUNT) % SUBS_STYLE_COUNT;
    else if (row == SUB_POS) g_app.subs_pos = (g_app.subs_pos + dir + 3) % 3;
    app_save_prefs();
    push_sub_style(p);
}

/* Timing moves whichever subtitles are showing: addon cues here, file tracks on the TV. */
static void shift_subs(Player *p, double by)
{
    if (p->track_loaded) { p->track.offset = by ? p->track.offset + by : 0; return; }
    if (p->embedded_text_sel < 0) return;
    p->embedded_offset = by ? p->embedded_offset + by : 0;
    push_sub_style(p);
}

static int menu_rows(Player *p)
{
    if (p->menu == MENU_SOURCES) return p->n_cands + 1;
    if (p->menu == MENU_EPISODES) { int n_eps = 0; app_videos(p->meta.id, &n_eps); return n_eps; }
    VideoTrack tr[MAX_TRACKS];
    int n = video_tracks(p->menu == MENU_AUDIO ? TRACK_AUDIO : TRACK_TEXT, tr, MAX_TRACKS);
    if (p->menu == MENU_AUDIO) return n + 1;
    return SUB_TRACKS + n + p->n_addon_subs + (xl_available(p) ? 1 : 0);
}

static void menu_activate(Player *p)
{
    if (p->menu == MENU_SOURCES) {
        VideoInfo vi;
        video_info(&vi);
        int view[64]; source_view(p, view);
        if (p->menu_focus < p->n_cands && view[p->menu_focus] != p->cur) { app_toast("Switching source"); start_candidate(p, view[p->menu_focus], vi.position); }
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
    if (f == SUB_CHOOSE) { p->assign_second = !p->assign_second; return; }
    if (f == SUB_TIMING) { if (p->track_loaded || p->embedded_text_sel >= 0) { shift_subs(p, 0); app_toast("Subtitle timing reset"); } return; }
    if (f >= SUB_SIZE && f <= SUB_POS) { adjust_subs(p, f, 1); return; }
    if (f == SUB_SYNC) {
        g_app.subs_autosync = !g_app.subs_autosync; app_save_prefs();
        if (!g_app.subs_autosync && p->track_loaded) { p->track.offset = 0; p->sync_state = 0; }
        p->sync_for = -1;
        return;
    }
    p->subs_auto_done = 1; p->subs_user = 1;   /* the viewer decided; stop auto-selecting */
    f -= SUB_OFF;
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
    f -= ng;
    if (xl_available(p) && f == 0) { if (p->xl_on) { int src = p->active_addon_sub; xl_stop(p); if (src >= 0) select_addon_sub(p, src); app_toast("Translation stopped"); } else xl_start(p); }
}

static void seek_by(Player *p, double base)
{
    VideoInfo vi;
    video_info(&vi);
    p->seek_steps++;
    p->seek_decay = 0.6f;
    /* held: bigger steps the longer the key stays down, never more than a 60th of the
     * video per press (a short episode is not crossed in three presses) */
    double step = base * (p->seek_steps > 16 ? 12 : p->seek_steps > 8 ? 6 : p->seek_steps > 4 ? 3 : 1);
    double cap = vi.duration > 0 ? vi.duration / 60 : 0;
    if (cap < fabs(base)) cap = fabs(base);
    if (step > cap) step = cap;
    if (step < -cap) step = -cap;
    p->seek_flash_step = (int)(fabs(step) + 0.5);
    if (p->seek_commit <= 0) p->seek_origin = vi.position;
    double from = p->seek_commit > 0 ? p->seek_target : vi.position;
    double target = from + step;
    if (target < 0) target = 0;
    if (vi.duration > 0 && target > vi.duration - 1) target = vi.duration - 1;
    p->seek_target = target;
    p->seek_commit = 0.35f;   /* coalesce rapid presses into one seek */
    if (g_app.time < p->overlay_until) show_overlay(p);   /* the controls stay while seeking */
    p->seek_flash = g_app.time + 0.35f; p->seek_flash_dir = base < 0 ? -1 : 1;
    if (g_app.live_scrub && g_app.time - p->last_live_seek > 0.33f) {   /* the picture follows while the key is held */
        p->last_live_seek = g_app.time;
        video_seek(target);
    }
    app_redraw();
}

/* Left mid-way: the island offers the title back for a while. */
static void offer_resume(Player *p, const VideoInfo *vi)
{
    if (p->trailer) return;
    if (vi->duration < 120 || vi->position < 60 || vi->position > vi->duration * 0.92) return;
    char line[200];
    if (p->title && p->meta.name && strncmp(p->title, p->meta.name, strlen(p->meta.name)) != 0)
        snprintf(line, sizeof line, "%s  \xC2\xB7  %s", p->meta.name, p->title);
    else snprintf(line, sizeof line, "%s", p->title ? p->title : p->meta.name ? p->meta.name : "");   /* the title already names the show */
    island_resume(&p->meta, line, (int)((vi->duration - vi->position) / 60 + 0.5));
}

static int key(Screen *s, KeyAction a)
{
    Player *p = s->st;
    VideoInfo vi;
    video_info(&vi);
    if (p->lighter_to >= 0 && !p->menu && p->seek_commit <= 0 && !(p->up_next_shown && p->next_video_id) && (a == KEY_OK || a == KEY_BACK)) {   /* the lighter source on offer */
        int to = p->lighter_to;
        p->lighter_to = -1;
        if (a == KEY_OK) { LOGI("player: switching to the lighter source %d", to + 1); app_toast("Switching to %dp", p->cands[to].res); start_candidate(p, to, vi.position); }
        app_redraw();
        return 1;
    }
    if (p->rest_t > 0.5f && !p->menu) {   /* leaning back: OK plays, anything else brings the controls back */
        if (a == KEY_OK || a == KEY_PLAY_PAUSE || a == KEY_PLAY) { LOGI("player: OK plays on from the lean-back screen"); p->bar_id = BTN_PLAY; video_play(); p->flash_icon = ICON_PLAY; p->play_flash = g_app.time + 0.6f; }
        show_overlay(p);
        return 1;
    }
    if (p->menu) {
        int rows = menu_rows(p);
        switch (a) {
        case KEY_UP: if (p->menu_focus > 0) p->menu_focus--; app_redraw(); return 1;
        case KEY_DOWN: if (p->menu_focus < rows - 1) p->menu_focus++; app_redraw(); return 1;
        case KEY_LEFT: case KEY_RIGHT: {
            int dir = a == KEY_RIGHT ? 1 : -1;
            if (p->menu == MENU_AUDIO && p->menu_focus == 0) { set_rate(p, p->rate_idx + dir); app_redraw(); return 1; }
            if (p->menu == MENU_EPISODES) { p->menu_focus = season_jump(p, dir); app_redraw(); return 1; }
            if (p->menu != MENU_SUBS) return 1;
            if (p->menu_focus == SUB_CHOOSE) p->assign_second = !p->assign_second;
            else if (p->menu_focus == SUB_TIMING) shift_subs(p, 0.1 * dir);
            else if (p->menu_focus >= SUB_SIZE && p->menu_focus <= SUB_POS) adjust_subs(p, p->menu_focus, dir);
            app_redraw();
            return 1;
        }
        case KEY_OK:
            if (p->menu == MENU_SOURCES && p->menu_focus == p->n_cands) { browse_sources(p); return 1; }
            { int was = p->menu; int keep = (was == MENU_AUDIO && p->menu_focus == 0) || (was == MENU_SUBS && (p->menu_focus < SUB_OFF || p->assign_second
                                             || (xl_available(p) && p->menu_focus == menu_rows(p) - 1)));
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
        case KEY_LEFT: { int i = bar_pos(p, ids, n); if (i > 0) p->bar_id = ids[i - 1]; show_overlay(p); return 1; }
        case KEY_RIGHT: { int i = bar_pos(p, ids, n); if (i < n - 1) p->bar_id = ids[i + 1]; show_overlay(p); return 1; }
        case KEY_UP: case KEY_BACK: p->bar_focus = 0; show_overlay(p); return 1;
        case KEY_DOWN:   /* below the buttons: hide the controls (paused, they stay; so does the focus) */
            if (vi.state == VIDEO_PAUSED) return 1;
            p->bar_focus = 0; p->overlay_until = 0; app_redraw(); return 1;
        case KEY_OK: bar_activate(p, &vi); return 1;
        default: break;   /* play/pause, colours and the rest work as usual */
        }
    }
    /* a seek waiting to land comes first: OK lands it now, Back puts the playhead back
     * (live scrub may already have moved the picture) */
    if (p->seek_commit > 0 && a == KEY_OK) { p->seek_commit = 0; video_seek(p->seek_target); show_overlay(p); return 1; }
    if (p->seek_commit > 0 && a == KEY_BACK) { p->seek_commit = 0; if (g_app.live_scrub) video_seek(p->seek_origin); app_toast("Seek cancelled"); show_overlay(p); return 1; }
    if (a == KEY_OK && p->up_next_shown && p->next_video_id) { go_up_next(p); return 1; }   /* the card's Play now */
    /* Skip: OK while the controls are hidden and the video plays, or once Up has focused the
     * button; with the controls up OK is play/pause like everywhere else */
    {
        const SkipSeg *sk = g_app.skip_mode == 0 ? skippable(p, &vi) : NULL;
        if (!sk || !overlay_on) p->skip_focus = 0;
        if (sk && a == KEY_OK && (p->skip_focus || (!overlay_on && vi.state == VIDEO_PLAYING))) { p->skip_focus = 0; do_skip(p, sk); show_overlay(p); return 1; }
        if (sk && overlay_on && !p->bar_focus && a == KEY_UP && !p->skip_focus) { p->skip_focus = 1; show_overlay(p); return 1; }
        if (p->skip_focus && (a == KEY_DOWN || a == KEY_BACK)) { p->skip_focus = 0; show_overlay(p); return 1; }
    }
    if (a == KEY_DOWN && p->up_next_shown && p->next_video_id && !p->bar_focus) {   /* put the card away for the controls */
        p->up_next_shown = 0; p->up_next_dismissed = 1; p->bar_focus = 1; show_overlay(p); return 1;
    }
    if (a == KEY_BACK && p->up_next_shown && p->next_video_id) {   /* dismiss the card, keep watching */
        p->up_next_shown = 0; p->up_next_dismissed = 1; app_redraw(); return 1;
    }
    switch (a) {
    case KEY_OK: case KEY_PLAY_PAUSE:
        p->bar_id = BTN_PLAY;
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
        if (g_app.pip_enabled && !p->trailer && (vi.state == VIDEO_PLAYING || vi.state == VIDEO_PAUSED || vi.state == VIDEO_LOADING) && g_app.depth > 1) { app_pip_enter(); return 1; }
        /* fall through */
    case KEY_STOP:
        report(p, 1);
        offer_resume(p, &vi);
        video_stop();
        p->finished = 1;
        app_pop();
        return 1;
    default: return 0;
    }
}

void player_save_progress(void)
{
    if (g_live) report(g_live, 1);
}

static void destroy(Screen *s)
{
    Player *p = s->st;
    report(p, 1);   /* the app closing with the film on: the last position still counts */
    streams_lookup_cancel();
    if (p->cur < p->n_cands) torrent_engine_drop(p->cands[p->cur].url);
    if (g_live == p) g_live = NULL;
    p->gen++;
    video_stop();
    subs_free(&p->track);
    subs_free(&p->track2);
    subs_free(&p->ref);
    xl_stop(p);
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
    p->bar_id = BTN_PLAY;
    p->lighter_to = -1;
    p->trailer = strncmp(video_id, "trailer:", 8) == 0;
    if (!p->trailer) island_resume(NULL, NULL, 0);   /* watching again: nothing to offer back */
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
    p->sync_for = -1; p->ref_idx = -1; p->sync_first = -1;
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
