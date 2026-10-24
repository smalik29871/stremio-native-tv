/* Player: transparent hole for the platform video plane, overlay controls,
 * addon + embedded subtitles, audio/subtitle menu, auto-fallback across stream
 * candidates, intro/recap skipping, Up Next at the credits, and progress sync to
 * the Stremio library. */
#include "../relay.h"
#include "../quality.h"
#include "../perfstat.h"
#include "../app.h"
#include "../http.h"
#include "../image.h"
#include "../frames.h"
#include "../phone.h"
#include "../torrent.h"
#include "../torbox.h"
#include "../lang.h"
#include "../log.h"
#include "../probe.h"
#include "../tmdb.h"
#include "../ai.h"
#include "../../vendor/cJSON.h"
#include "../skip.h"
#include "../store.h"
#include "../youtube.h"
#include "../subs.h"
#include "../trackmem.h"
#include "../tracking.h"
#include "../opensubs.h"
#include "../srchealth.h"
#include "../video/video.h"
#include "../platform/platform.h"
#include "font.h"
#include "theme.h"
#include "widgets.h"
#include "island.h"
#include "../advisory.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

enum { MAX_ADDON_SUBS = 64, MAX_TRACKS = 16 };
#define MAX_MARKS 20
enum { MENU_NONE, MENU_AUDIO, MENU_SUBS, MENU_SOURCES, MENU_EPISODES };
/* Subtitles menu rows: the controls first, then Off and the tracks */
enum { SUB_CHOOSE, SUB_TIMING, SUB_SIZE, SUB_STYLE, SUB_POS, SUB_SYNC, SUB_OFF, SUB_TRACKS };
enum { BTN_PREV, BTN_PLAY, BTN_NEXT, BTN_RELOAD, BTN_SUBS, BTN_AUDIO, BTN_ASPECT, BTN_SOURCES, BTN_EPISODES, BTN_SCENES, BTN_STATS, BTN_CAST, BTN_COUNT };
enum { ASPECT_FIT, ASPECT_FILL, ASPECT_ZOOM, ASPECT_COUNT };
static const char *const ASPECT_NAMES[ASPECT_COUNT] = { "Fit", "Fill the screen", "Zoom" };
static const double RATES[] = { 0.5, 0.75, 1.0, 1.25, 1.5, 1.75, 2.0 };
enum { N_RATES = 7, RATE_NORMAL = 2 };

typedef struct AddonSub { char *url, *lang, *addon, *release; int matched, sync_failed; int fit; char fit_why[48]; int name; } AddonSub;   /* name: release-name match 0..100 (OpenSubtitles) */   /* fit: 0 not checked, else 1..101 */

typedef struct Player Player;
typedef struct { Player *owner; unsigned gen; int addon; int slot; int hashed; unsigned attempt; unsigned seq; } SubReq;   /* attempt: 0, or the source start it is about; seq: which subtitle download, for files */

struct Player {
    MetaPreview meta;
    char *video_id, *title;
    PlayCandidate *cands; int n_cands, cur;
    int tries;
    uint64_t tried;                         /* sources already started here (bit per index, first 64) */
    int started;                            /* this source has shown a picture (the clock moved) */
    float veil;                             /* the title's artwork over the screen until the first picture, then fading */
    double away_s;                          /* how long since this title was last watched (0 unknown) */
    float catchup_until;                    /* the "previously" card shows until then (g_app.time); -1 waits for the picture */
    char catchup_head[200], catchup_text[600];
    int cast_open, cast_focus;              /* Info: who's in this, over the picture */
    int scenes_open, scenes_focus;          /* the Scenes rail: card 0 marks the moment, then chapters / every 5 min / marks */
    double marks[MAX_MARKS]; int n_marks;   /* moments the viewer marked in this video, seconds */
    double replay_until; int replay_subs;   /* Blue replayed the last 10 s, until here; subtitles went on for it */
    char tb_error[160];                     /* TorBox could not open the current source (nothing was loaded to fail) */
    unsigned own_load;                      /* video_load_count() of this player's last load */
    int thumbs_warm;                        /* a first seek preview asked for, so the keyframe index is ready before the first seek */
    int p2p_peers, p2p_info;                /* the P2P engine's last word on this torrent */
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
    /* a phone (always sideways): the controls over the picture, a list at the right */
    int ph_sheet, ph_focus;   /* the list open (PH_*), its focused row */
    float ph_scroll, ph_t;    /* the list's scroll, and when it last drew */
    UiPan ph_pan;
    float ph_pic[4], fs_barr[4];   /* where the picture and the bar are, in canvas units */
    float fs_until;           /* sideways on a phone: the controls show until */
    int tch, tch_seeking, tch_moved;   /* a finger: 1 on the bar, 2 on the picture; seeking with it; it moved */
    float tch_x0, tch_y0; double tch_pos0;
    int tap_pending, tap_side; float tap_at;            /* a tap on the picture waiting to see if a second comes */
    int ripple_side, ripple_secs; float ripple_at;      /* double taps: which side, how far so far */
    int tch_adjust; float tch_level0, adj_at;           /* a finger up or down the picture: brightness (1) or volume (2), from where; when last */
    float bright, vol;        /* 0..1, the last set here (-1: not yet); where the device can't, the picture dims / the player's loudness */
    int soft_dim, adj_last;   /* brightness drawn as a veil over the picture; which level was shown last */
    float since_report;
    double last_reported;
    int finished, up_next_shown, up_next_accepted;
    float seek_last_press;                  /* g_app.time of the last seek press (not a repeat) */
    double seek_target; float seek_commit;   /* debounced seek while the key repeats */
    double seek_origin;                      /* where the playhead was when the seek began (Back returns there) */
    float load_started;
    int error_handled;

    /* subtitles */
    AddonSub addon_subs[MAX_ADDON_SUBS]; int n_addon_subs;
    int subs_pending;
    unsigned gen;
    unsigned attempt;         /* bumps at every source start: late answers about an earlier one are dropped */
    unsigned sub_seq[3];      /* the newest download per subtitle slot (track, second, timing reference): an older answer is dropped */
    SubTrack track; int track_loaded; int active_addon_sub;   /* -1 none */
    unsigned sub_req;
    SubTrack track2; int track2_loaded; int active_addon_sub2;   /* second subtitles, drawn above the main ones */
    int assign_second;        /* the subtitles menu picks for the second slot */
    int subs_col, subs_group, subs_row;   /* the subtitles sheet: column (0 languages, 1 options), language, option */
    char subs_group_name[48];            /* the language focused, so files arriving meanwhile cannot move it */
    int subs_row_kind, subs_row_ref;     /* and the choice in it */
    void *sg; int sg_n; unsigned sg_key; /* the sheet's groups, rebuilt when the subtitles change */
    int second_done;          /* the show's second language put on (or nothing to do) */
    int forced_checked;       /* looked for a forced track in the audio language */
    /* OpenSubtitles-style file hash -> subtitles matched to this exact file -> auto-sync */
    int hash_state;           /* 0 not started, 1 head, 2 tail, 3 known, 4 unavailable */
    uint64_t os_sum, os_size; char os_hash[24];
    SubTrack ref; int ref_loaded, ref_idx;   /* a matched subtitle used as the timing reference */
    /* best match: the files in the chosen language, fetched and scored once (subs_fit) */
    int bm_state;                            /* 0 not yet, 1 fetching, 2 done */
    int os_asked;                            /* OpenSubtitles.com searched for this stream */
    int bm_idx[6], bm_n, bm_pending; SubTrack bm_track[6]; int bm_ok[6];
    int sync_for, sync_state; double sync_offset;   /* sync_state: 0 idle, 1 aligning, 2 synced, 3 no match */
    /* AI translation of the active addon file into the preferred language, a batch at a time, playhead first */
    int xl_on, xl_hinted; char xl_lang[8]; unsigned char *xl_done; int xl_n_done, xl_fails; unsigned xl_req; unsigned xl_gen; int xl_from, xl_count; float xl_wait;
    float seek_flash; int seek_flash_dir;   /* the ±10 s mark flashes on the side that seeks */
    int seek_flash_step;                    /* seconds the last press moved (double on a double press) */
    float rest_t;                           /* paused and left alone: the controls fade, the title stays */
    int trailer;                            /* video_id "trailer:…": nothing is saved against the title */
    int tracked;                            /* Trakt was told this one started */
    /* fallback: what the viewer had on, carried to the next source */
    char keep_audio[16]; int keep_sub_kind, keep_sub_idx; char keep_sub_lang[16];   /* kind: 0 none, 1 addon file, 2 file track, 3 off */
    unsigned char dead[16];                 /* sources found not to answer */
    int more_state;                         /* every addon asked from the Sources panel: 0 not yet, 1 asking, 2 answered */
    /* the next two sources, checked while this one plays (state 0 -, 1 asking, 2 answers) */
    struct { int idx, state; float at, s; } warm[2];
    struct { char *url, *link; float at; } ready[2];   /* TorBox torrents among them (their torbox:// links), already turned into their files' links */
    float drain_at[6], drain_v[6]; int drain_n; float drain_t;   /* seconds buffered ahead, sampled */
    int reask_count; float reask_at; double reask_pos;   /* every source failed: the addons asked again */
    double start_at;                        /* where this source was asked to start (a failed one reports 0) */
    int pause_when_started;                 /* reloaded after a break over a paused film: paused again once it shows */
    int viewer_paused;                      /* the pause is the viewer's (not a stall the TV reports as one) */
    float good_since; int good_recorded;   /* this source has played well for a while */
    int lighter_to; int lighter_offered;    /* buffering keeps stopping: a lower resolution offered (-1 none) */
    int stalls_here;                        /* stalls since this source started */
    double pos_seen; float jumped_at;       /* the playhead jumped (a seek): the buffering after it is not the connection's fault */
    double known_dur;                       /* the duration last reported (0 while a source loads) */
    double jump_pos;                        /* where the last jump landed */
    float menu_t; int menu_seen;           /* the side menu slides in */
    float tune_t; int tune_on;             /* subtitles drawer (0) to the Look and timing panel (1) */
    int embedded_text_sel;    /* -1 off */
    double embedded_offset;   /* timing for the TV's own subtitles, seconds (positive = later) */
    int auto_selected;        /* audio preference applied */
    int subs_auto_done;       /* subtitle preference applied (or given up) */
    int subs_user;            /* the viewer chose subtitles by hand this session */
    int sync_retries, sync_first;
    int last_tracks_version;
    int last_warning_version;
    unsigned audio_bad;       /* audio tracks this TV said it can't decode, on this source (bit per index) */
    int audio_req; float audio_req_at;   /* the audio track last asked for (the TV reports the switch later) */
    char audio_lang[16]; int audio_lang_known;
    time_t clock_minute;
    int prefetched;           /* next episode's streams requested */
    int next_warmed;          /* and the start of its likely file fetched */
    int night, night_applied; long night_minute; unsigned night_load;   /* night mode on; the volume set for this load */
    int stinger_noted;        /* "stay for the scene after the credits" said */
    float played_at, adv_at;  /* when playback first ran; when the content advisory came up (0 not yet) */
    int corner_logged;        /* lines in the paused corner last logged */
    float prefetch_tried;     /* when; a stale or empty prefetch is retried after a while */
    int prefetch_renewed;     /* asked again near the end, once */
    int net_retries; float net_retry_at; double net_retry_pos;   /* the same source again after a dropped connection */
    int net_reconnecting;     /* this load is such a retry: failing before it plays still counts as the dropped connection */
    int net_played;           /* this source has played (a link that never did gets one retry, not three) */
    int card_logged;          /* the error card was put up (once in the log) */
    int start_timed;          /* its first frame closed the start timing (perfstat) */
    int final_used;           /* this load opened where the link's redirects led (found moments ago) */
    unsigned char final_off[16];   /* that address failed for this source: its link from now on */
    int relay_used;           /* this load went through the local relay (headers, or the buffer ahead) */
    double relay_dur;         /* the length last given to the relay's buffer */
    int tb_pending;           /* TorBox is preparing the current source: no re-ask, no error card meanwhile */
    int tb_held; double tb_held_start;   /* TorBox answered while a break or dialog covered the player: it starts once back on top */
    unsigned char relay_off[16];   /* the relay did not get it playing: the direct link from now on */
    float next_prog;          /* shown share of the prefetch (eases toward the real one) */
    float next_ready_at;      /* when it reached 1, for the ready pulse */
    int pip, pip_done;        /* parked in a corner; finished there */
    int learned;              /* this candidate's outcome was recorded in the TV profile */
    ProbeInfo *probe;         /* the file's header: chapters, real codecs */
    int probe_asked;
    int yt_noted;             /* this candidate's start was reported (youtube_note_result) */
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
    int next_wanted;          /* OK on the card while the next episode was still being prepared: it starts when ready */
    int still_asking;         /* "Still watching?": episodes went on by themselves for a while with nobody pressing a key */

    int menu, menu_focus;
    int src_filter, src_chip, src_on_chips;   /* Sources sheet: addon tab (0 all), focused button (0 find more, 1 close), focus zone (0 list, 1 tabs, 2 buttons) */
    int bar_focus, bar_id;     /* Down moves from the seek bar to the button row; the button by BTN_* id */
    float play_flash;          /* big centre icon after play/pause */
    int flash_icon;
};

static Player *g_live;

static void show_overlay(Player *p) { p->overlay_until = g_app.time + 4.0f; app_redraw(); }
#define PAUSE_REST 6.0f   /* seconds paused and untouched before the controls step aside */
#define ADVISORY_S 7.0f   /* seconds the content advisory stays when a video starts */
static void url_host(const char *url, char *out, size_t n);
static void go_up_next(Player *p);

/* Resume on the title page opens the source that played, not the one first picked (which
 * may have lost the race or died since). */
static void remember_playing(const Player *p)
{
    if (p->trailer || !p->meta.id || p->cur < 0 || !p->cands[p->cur].url) return;
    char key[160];
    snprintf(key, sizeof key, "resume:%s", p->meta.id);
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "url", p->cands[p->cur].url);
    cJSON_AddStringToObject(o, "video_id", p->video_id);
    store_set_json(key, o);
    store_save();
}

/* Where this video goes on from: the position while it plays; while a source loads,
 * reconnects or fails it reports 0, so where it was headed (a reconnect's spot, the
 * position it was started at). */
static double resume_spot(const Player *p, const VideoInfo *vi)
{
    if (vi->position > 5) return vi->position;
    if (p->net_retry_pos > 5) return p->net_retry_pos;
    return p->start_at;
}
static void instant_replay(Player *p, double pos);
static void night_tick(Player *p, const VideoInfo *vi);
static int scenes_list(const Player *p, double duration, double *t, int *kind, int n);
static int scene_now(const Player *p, double pos);
static void marks_load(Player *p);
static void push_sub_style(const Player *p);
/* Night mode draws subtitles a size larger */
static int sub_size(const Player *p) { return g_app.subs_size + (p->night && g_app.subs_size < 3); }
static int pick_forced(Player *p);
static void autosync_tick(Player *p);
static void xl_stop(Player *p);
static int source_view(const Player *p, int *view);
enum { SRC_ADDONS = 12 };
static int source_addons(const Player *p, char names[SRC_ADDONS][80]);
static int source_rows(const Player *p, int *rows);
static int cand_res(const PlayCandidate *c);
static int cand_group(const PlayCandidate *c);
static void cand_addon(const PlayCandidate *c, char *out, size_t n);
static void describe_track(const Player *p, int kind, const VideoTrack *tr, int i, int n, char *title, size_t tn, char *sub, size_t sn);
static int xl_available(Player *p);
static void draw_subs_panel(Player *p);
static int subs_tuning(const Player *p);
static int cast_n(const Player *p, const TmdbInfo **tm);
static int open_cast(Player *p);
static const char *emb_lang(const Player *p, const VideoTrack *tr, int i, int n);

static int subs_key(Player *p, KeyAction a);
static void full_draw(Player *p, const VideoInfo *vi);
static void picture_tap_settle(Player *p);
static void subs_open(Player *p);
static void xl_start(Player *p);
static int error_final(const Player *p, const VideoInfo *vi);
static int recovering(const Player *p, const VideoInfo *vi);

static double resume_spot(const Player *p, const VideoInfo *vi);

/* The position to the library and to Trakt/SimKL. played_through: it got here by playing
 * (two minutes since the last seek), so past 90% it counts as watched; a seek to the end
 * to peek does not. scrobble: a final report ends the scrobble (not for an overlay). */
static void report_ex(Player *p, int force, int scrobble)
{
    VideoInfo vi;
    video_info(&vi);
    if (p->trailer) return;
    if (vi.duration > 0) p->known_dur = vi.duration;
    if (vi.state == VIDEO_IDLE || vi.state == VIDEO_LOADING || vi.state == VIDEO_ERROR) {   /* between sources: where it was headed */
        double at = resume_spot(p, &vi);
        if (!force || at <= 5 || p->known_dur <= 0) return;
        app_library_progress(&p->meta, p->video_id, at, p->known_dur, 0);
        if (scrobble) tracking_progress(&p->meta, p->video_id, at, p->known_dur, 1, 0);
        return;
    }
    if (!force && vi.position == p->last_reported) return;
    p->last_reported = vi.position;
    double left = vi.duration - p->jump_pos, need = left * 0.8 < 120 ? left * 0.8 : 120;   /* played since the last jump: 2 min, or most of what was left */
    int through = g_app.time - p->jumped_at >= need;
    app_library_progress(&p->meta, p->video_id, vi.position, vi.duration, through);
    tracking_progress(&p->meta, p->video_id, vi.position, vi.duration, (force && scrobble) || vi.state == VIDEO_PAUSED, through);
}

static void report(Player *p, int force) { report_ex(p, force, 1); }

/* ---- addon subtitles ---------------------------------------------------------- */

static void on_addon_subs(HttpResponse *r, void *user)
{
    SubReq *q = user;
    Player *p = q->owner;
    if (p != g_live || q->gen != p->gen) { free(q); return; }
    p->subs_pending--;
    if (q->attempt && q->attempt != p->attempt) { free(q); return; }   /* matched to an earlier source's file */
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
        SubReq *q = calloc(1, sizeof *q);
        q->owner = p; q->gen = p->gen; q->addon = i; q->slot = 0; q->hashed = hashed; q->attempt = hashed ? p->attempt : 0;
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
    unsigned qa = q->attempt, qg = q->gen;
    free(q);
    if (p != g_live || qa != p->attempt || qg != p->gen) return;   /* the player closed, or about an earlier source */
    /* both 64 KB pieces whole, and the end really from the end: a server that ignored the
     * Range sends the start again, and that hash would look right and match nothing */
    if (!http_has_bytes(r, tail ? p->os_size - 65536 : 0, 65536)) {
        p->hash_state = 4;
        LOGI("subs: file hash unavailable (%ld, %zu bytes%s)", r->status, r->len, tail && r->status == 200 ? ", the end was not sent" : "");
        return;
    }
    if (r->len > 65536) r->len = 65536;   /* the hash reads 64 KB of each end, never more */
    p->os_sum += sum64((const uint8_t *)r->body, r->len);
    if (!tail) {
        p->os_size = r->total_size ? r->total_size : (uint64_t)r->len;
        if (p->os_size < 131072) { p->hash_state = 4; return; }
        p->hash_state = 2;
        SubReq *q2 = calloc(1, sizeof *q2);
        q2->owner = p; q2->gen = p->gen; q2->addon = -1; q2->slot = 1; q2->hashed = 0; q2->attempt = p->attempt;
        HttpOptions opt = { .range_bytes = 65536, .range_start = p->os_size - 65536, .timeout_s = 8, .no_json_accept = 1, .headers = p->cands[p->cur].headers };
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
    const PlayCandidate *c = &p->cands[p->cur];
    if (c->video_hash && strlen(c->video_hash) == 16 && strspn(c->video_hash, "0123456789abcdefABCDEF") == 16) {   /* the addon knows it */
        snprintf(p->os_hash, sizeof p->os_hash, "%s", c->video_hash);
        p->os_size = c->video_size;
        p->hash_state = 3;
        LOGI("subs: file hash %s from the addon, asking for matched subtitles", p->os_hash);
        query_addon_subs_ex(p, 1);
        return;
    }
    p->hash_state = 1; p->os_sum = 0; p->os_size = 0;
    if (strncmp(p->cands[p->cur].url, "http", 4) != 0) { p->hash_state = 4; return; }
    SubReq *q = calloc(1, sizeof *q);
    q->owner = p; q->gen = p->gen; q->addon = -1; q->slot = 0; q->hashed = 0; q->attempt = p->attempt;
    HttpOptions opt = { .range_bytes = 65536, .range_start = 0, .timeout_s = 8, .no_json_accept = 1, .headers = p->cands[p->cur].headers };
    http_request_async_ex(p->cands[p->cur].url, &opt, on_hash_part, q);
}

static double sub_fix_get(const char *url);

static int sub_take(HttpResponse *r, SubTrack *out);

static void on_sub_file(HttpResponse *r, void *user)
{
    SubReq *q = user;
    Player *p = q->owner;
    if (p != g_live || q->gen != p->gen) { free(q); return; }
    if (q->seq != p->sub_seq[q->slot] || (q->slot == 2 && q->attempt != p->attempt)) {   /* replaced by a newer choice (or Off), or a reference for an earlier source */
        LOGI("subs: a %s download that was replaced is dropped", q->slot == 2 ? "timing reference" : q->slot ? "second subtitles" : "subtitle");
        free(q); return;
    }
    SubTrack *t = q->slot == 2 ? &p->ref : q->slot ? &p->track2 : &p->track;
    int *loaded = q->slot == 2 ? &p->ref_loaded : q->slot ? &p->track2_loaded : &p->track_loaded;
    if (!q->slot) p->sub_req = 0;
    subs_free(t);
    *loaded = 0;
    if (sub_take(r, t)) {
        *loaded = 1;
        LOGI("subs: loaded %d cues%s", t->n, q->slot == 2 ? " (timing reference)" : q->slot ? " (second subtitles)" : "");
        if (!q->slot && p->active_addon_sub >= 0) {
            double fix = sub_fix_get(p->addon_subs[p->active_addon_sub].url);
            if (fix != 0) { t->offset = fix; LOGI("subs: remembered timing fix %+.2f s", fix); app_toast("Your timing fix for these subtitles is back (%+.1f s)", fix); }
        }
    } else if (q->slot == 2) {
        p->sync_state = 3;
    } else {
        app_toast("Could not load that subtitle file");
        if (q->slot) p->active_addon_sub2 = -1; else p->active_addon_sub = -1;
    }
    app_redraw();
    free(q);
}

/* A subtitle file: an addon's URL, or "os://<file id>" from OpenSubtitles.com, whose
 * download link is asked for first (that is when a download counts). */
/* Parsed on the network thread, so a big file never holds up drawing. */
static void sub_parse_work(HttpResponse *r, const char *url)
{
    (void)url;
    if (r->status != 200 || !r->body) return;
    SubTrack *t = calloc(1, sizeof *t);
    if (t && subs_parse(r->body, r->len, t) > 0) r->parsed = t;
    else { if (t) subs_free(t); free(t); }
}
static void sub_parse_free(void *parsed) { subs_free(parsed); free(parsed); }
static unsigned sub_fetch(const char *url, HttpCallback cb, SubReq *q)
{
    HttpOptions o = { .work = sub_parse_work, .free_parsed = sub_parse_free };
    return http_request_async_ex(url, &o, cb, q);
}
/* the parsed file, taken from the response (0 when it did not parse) */
static int sub_take(HttpResponse *r, SubTrack *out)
{
    SubTrack *t = r->parsed;
    if (!t) return 0;
    *out = *t; free(t); r->parsed = NULL;
    return out->n > 0;
}

typedef struct { HttpCallback cb; SubReq *q; } OsGet;
static void on_os_link(const char *url, const char *error, void *user)
{
    OsGet *g = user;
    if (url) sub_fetch(url, g->cb, g->q);
    else {
        HttpResponse fake; memset(&fake, 0, sizeof fake);
        snprintf(fake.error, sizeof fake.error, "%s", error ? error : "refused");
        if (error && g->q->slot == 0) app_toast("%s", error);
        g->cb(&fake, g->q);
    }
    free(g);
}

static unsigned sub_get(const char *url, HttpCallback cb, SubReq *q)
{
    if (strncmp(url, "os://", 5) != 0) return sub_fetch(url, cb, q);
    OsGet *g = malloc(sizeof *g);
    g->cb = cb; g->q = q;
    opensubs_link(atol(url + 5), on_os_link, g);
    return 0;
}

static void on_os_results(const OsSub *subs, int n, void *user)
{
    SubReq *q = user;
    Player *p = q->owner;
    if (p != g_live || q->gen != p->gen) { free(q); return; }
    unsigned qa = q->attempt;
    free(q);
    p->subs_pending--;
    if (qa != p->attempt) return;   /* matched to an earlier source's file */
    int fresh_match = 0;
    for (int i = 0; i < n && p->n_addon_subs < MAX_ADDON_SUBS; i++) {
        char url[48]; snprintf(url, sizeof url, "os://%ld", subs[i].file_id);
        int seen = 0; for (int k = 0; k < p->n_addon_subs; k++) if (!strcmp(p->addon_subs[k].url, url)) seen = 1;
        if (seen) continue;
        AddonSub *as = &p->addon_subs[p->n_addon_subs++];
        memset(as, 0, sizeof *as);
        as->url = strdup(url); as->lang = strdup(subs[i].lang); as->addon = strdup("OpenSubtitles");
        as->release = subs[i].release[0] ? strdup(subs[i].release) : NULL;
        as->matched = subs[i].hash_match;
        { int a1 = opensubs_name_score(subs[i].release, p->cands[p->cur].filename), a2 = opensubs_name_score(subs[i].release, p->cands[p->cur].release); as->name = a1 > a2 ? a1 : a2; }
        if (as->matched && lang_same(as->lang, g_app.pref_subs)) fresh_match = 1;
    }
    LOGI("subs: %d from OpenSubtitles.com%s", n, fresh_match ? ", one made for this file" : "");
    if (fresh_match && !p->subs_user) p->subs_auto_done = 0;   /* made for this file: beats the earlier pick */
    app_redraw();
}

static void opensubs_tick(Player *p)
{
    if (p->os_asked || p->trailer || !opensubs_enabled() || p->hash_state < 3 || !p->video_id || strncmp(p->video_id, "tt", 2)) return;
    p->os_asked = 1;
    char imdb[24]; int season = -1, episode = -1;
    const char *c = strchr(p->video_id, ':');
    snprintf(imdb, sizeof imdb, "%.*s", c ? (int)(c - p->video_id) : (int)strlen(p->video_id), p->video_id);
    if (c && sscanf(c + 1, "%d:%d", &season, &episode) != 2) season = episode = -1;
    const char *l1 = lang_iso1(g_app.pref_subs);
    char langs[32]; snprintf(langs, sizeof langs, "%s", l1 ? l1 : "en");
    SubReq *q = calloc(1, sizeof *q);
    q->owner = p; q->gen = p->gen; q->addon = -1; q->slot = 0; q->hashed = 0; q->attempt = p->attempt;   /* searched with this file's hash */
    p->subs_pending++;
    opensubs_search(imdb, season, episode, p->hash_state == 3 ? p->os_hash : "", langs, on_os_results, q);
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
    if (idx < 0) { p->sub_seq[slot]++; return; }   /* off: a download still coming is dropped */
    SubReq *q = calloc(1, sizeof *q);
    q->owner = p; q->gen = p->gen; q->addon = -1; q->slot = slot; q->seq = ++p->sub_seq[slot];
    unsigned req = sub_get(p->addon_subs[idx].url, on_sub_file, q);
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
 * an addon file (or the other way round). The video's own track is always in sync; with
 * "Subtitle files over the video's own" on and no source asked for (SUBS_SRC_ANY), a file
 * matched to this video comes first instead: the app draws it, so every subtitle setting
 * applies, but such files can be a little off. 1 when picked. */
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
    if (prefer == SUBS_SRC_ANY && g_app.subs_prefer_files)
        for (int i = 0; i < p->n_addon_subs; i++)
            if (p->addon_subs[i].matched && lang_same(p->addon_subs[i].lang, lang)) {
                if (p->active_addon_sub != i) { LOGI("player: subtitles: the file matched to this video, before its own track"); select_addon_sub(p, i); }
                return 1;
            }
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
            /* a file whose one subtitle track has no language ("und") nearly always has it in
             * the release's own language, English for most: for English (or "und" remembered
             * for a series) it wins over another file from an addon */
            int unl = -1, unlabelled = 0, labelled = 0;
            for (int i = 0; i < nt; i++) { if (lang_known(tr[i].lang)) labelled++; else { unlabelled++; if (unl < 0) unl = i; } }
            if (unl >= 0 && (strstr(tr[unl].label, "orced") || strstr(tr[unl].label, "ORCED"))) unlabelled = 0;   /* foreign parts only */
            if (unlabelled == 1 && !labelled && lang && (!strcmp(lang, "und") || lang_same(lang, "eng"))) {
                select_addon_sub(p, -1);
                video_select_track(TRACK_TEXT, tr[unl].index);
                p->embedded_text_sel = tr[unl].index;
                LOGI("player: subtitles: the file's own track (no language given)");
                return 1;
            }
        } else {
            for (int pass = 0; pass < 3; pass++) {   /* made for this file, then for the same release (its name), then any */
                int best = -1;
                for (int i = 0; i < p->n_addon_subs; i++) {
                    const AddonSub *as = &p->addon_subs[i];
                    if (!lang_same(as->lang, lang)) continue;
                    if (pass == 0 && !as->matched) continue;
                    if (pass == 1 && as->name < 60) continue;
                    if (best < 0 || (pass == 1 && as->name > p->addon_subs[best].name)) best = i;
                    if (pass != 1) break;
                }
                if (best >= 0) { if (pass == 1) LOGI("subs: %s is the same release by name (%d)", p->addon_subs[best].release, p->addon_subs[best].name); select_addon_sub(p, best); return 1; }
            }
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
        const char *want = p->keep_audio[0] ? p->keep_audio : has_mem && mem.audio[0] ? mem.audio : g_app.pref_audio;   /* a switch keeps what was on */
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
            if (pick >= 0 && !tr[pick].selected) { video_select_track(TRACK_AUDIO, tr[pick].index); p->audio_req = tr[pick].index; p->audio_req_at = g_app.time; for (int k = 0; k < n; k++) tr[k].selected = k == pick; }
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
    if (p->keep_sub_kind) {   /* moved to another source: the same subtitles as before */
        int kind = p->keep_sub_kind;
        p->keep_sub_kind = 0;
        if (kind == 1 && p->keep_sub_idx < p->n_addon_subs) { select_addon_sub(p, p->keep_sub_idx); p->subs_auto_done = 1; LOGI("player: subtitles kept across the switch"); return; }
        if (kind == 2 && pick_subs(p, p->keep_sub_lang, SUBS_SRC_EMBEDDED)) { p->subs_auto_done = 1; LOGI("player: subtitles %s kept across the switch", p->keep_sub_lang); return; }
        if (kind == 3) { p->subs_auto_done = 1; return; }
    }
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
        if (pick_subs(p, g_app.pref_subs, SUBS_SRC_ANY)) { LOGI("player: subtitles %s (preference)", g_app.pref_subs); p->subs_auto_done = 1; return; }
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

/* Second subtitles stay on for the rest of the show (learning a language with both on
 * screen): {"secondSubs": {"<series>": "fr"}}. NULL forgets. */
static void remember_second(Player *p, const char *lang)
{
    const char *key = memory_key(p);
    if (!key) return;
    cJSON *all = cJSON_Duplicate(store_get_json("secondSubs"), 1);
    if (!cJSON_IsObject(all)) { cJSON_Delete(all); all = cJSON_CreateObject(); }
    cJSON_DeleteItemFromObjectCaseSensitive(all, key);
    if (lang && lang_known(lang)) cJSON_AddStringToObject(all, key, lang);
    store_set_json("secondSubs", all);
    store_save();
    p->second_done = 1;
}

/* The show has second subtitles on: the same language again, once its files arrive. */
static void second_auto(Player *p)
{
    if (p->second_done || !p->subs_auto_done) return;
    const char *key = memory_key(p);
    const cJSON *lang = key ? cJSON_GetObjectItemCaseSensitive(store_get_json("secondSubs"), key) : NULL;
    if (!cJSON_IsString(lang)) { p->second_done = 1; return; }
    for (int i = 0; i < p->n_addon_subs; i++)
        if (i != p->active_addon_sub && lang_same(p->addon_subs[i].lang, lang->valuestring)) {
            p->second_done = 1;
            select_addon_sub_slot(p, i, 1);
            LOGI("player: second subtitles %s (on for this show)", lang->valuestring);
            return;
        }
    if (p->subs_pending == 0 || g_app.time - p->load_started > 20) p->second_done = 1;
}

static void remember_subs(Player *p, const char *lang, SubsSource src)
{
    if (lang && !lang_known(lang) && src == SUBS_SRC_EMBEDDED) lang = "und";   /* the file's own track, no language given: kept as that */
    if (!memory_key(p) || (lang && !lang_known(lang) && strcmp(lang, "und"))) return;
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

static void select_addon_sub(Player *p, int idx);

static void on_bm_file(HttpResponse *r, void *user)
{
    SubReq *q = user;
    Player *p = q->owner;
    if (p != g_live || q->gen != p->gen) { free(q); return; }
    if (q->attempt != p->attempt) { LOGI("subs: a best-fit file for an earlier source is dropped"); free(q); return; }
    int k = q->slot;
    p->bm_ok[k] = sub_take(r, &p->bm_track[k]);
    p->bm_pending--;
    free(q);
}

/* Several files in the language that was picked, none matched to this video: the one
 * that fits it best (ends with it, a full set of lines, lines up with the file-matched
 * subtitle when there is one) is worth more than the addon's order. */
static void bestmatch_tick(Player *p, const VideoInfo *vi)
{
    if (p->bm_state == 2 || p->trailer) return;
    if (p->bm_state == 0) {
        if (p->subs_user || p->active_addon_sub < 0 || p->subs_pending > 0 || vi->duration <= 60) return;
        if ((p->hash_state < 3 || (opensubs_enabled() && !p->os_asked)) && g_app.time - p->load_started < 20) return;   /* files made for this one may still come */
        const AddonSub *cur = &p->addon_subs[p->active_addon_sub];
        if (cur->matched || !strncmp(cur->url, "os://", 5)) { p->bm_state = 2; return; }   /* made for this file, or chosen by its release name */
        int ref = matched_reference(p);
        if (ref >= 0 && !p->ref_loaded && p->sync_state != 3 && g_app.time - p->load_started < 30) return;   /* score against it */
        p->bm_n = 0;
        p->bm_idx[p->bm_n++] = p->active_addon_sub;
        for (int i = 0; i < p->n_addon_subs && p->bm_n < 6; i++)
            if (i != p->active_addon_sub && !p->addon_subs[i].matched && strncmp(p->addon_subs[i].url, "os://", 5) && lang_same(p->addon_subs[i].lang, cur->lang)) p->bm_idx[p->bm_n++] = i;
        if (p->bm_n < 2) { p->bm_state = 2; return; }
        p->bm_state = 1; p->bm_pending = p->bm_n;
        for (int k = 0; k < p->bm_n; k++) {
            memset(&p->bm_track[k], 0, sizeof p->bm_track[k]); p->bm_ok[k] = 0;
            SubReq *q = calloc(1, sizeof *q);
            q->owner = p; q->gen = p->gen; q->addon = -1; q->slot = k; q->hashed = 0; q->attempt = p->attempt;
            sub_get(p->addon_subs[p->bm_idx[k]].url, on_bm_file, q);
        }
        LOGI("subs: comparing %d %s files for the best fit", p->bm_n, cur->lang);
        return;
    }
    if (p->bm_pending > 0 && g_app.time - p->load_started < 60) return;
    p->bm_state = 2;
    const SubTrack *ref = p->ref_loaded ? &p->ref : NULL;
    int best = -1, cur_score = 0, best_score = -1;
    for (int k = 0; k < p->bm_n; k++) {
        AddonSub *as = &p->addon_subs[p->bm_idx[k]];
        int sc = p->bm_ok[k] ? subs_fit(&p->bm_track[k], vi->duration, ref, as->fit_why, sizeof as->fit_why) : 0;
        as->fit = sc + 1;
        LOGI("subs: %s file %d fits %d (%s)", as->lang, p->bm_idx[k], sc, as->fit_why[0] ? as->fit_why : "-");
        if (k == 0) cur_score = sc;
        if (sc > best_score) { best_score = sc; best = p->bm_idx[k]; }
        subs_free(&p->bm_track[k]);
    }
    if (best >= 0 && best != p->active_addon_sub && best_score >= cur_score + 15 && !p->subs_user) {
        LOGI("subs: switching to file %d, it fits better (%d vs %d)", best, best_score, cur_score);
        select_addon_sub(p, best);
        const AddonSub *was = &p->addon_subs[p->bm_idx[0]], *now = &p->addon_subs[best];
        if (now->fit_why[0]) app_toast("Switched to subtitles that fit this video better: this one %s", now->fit_why);
        else if (was->fit_why[0]) app_toast("Switched to subtitles that fit this video better: the first one %s", was->fit_why);
        else app_toast("Switched to subtitles that fit this video better");
    }
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
        SubReq *q = calloc(1, sizeof *q);
        q->owner = p; q->gen = p->gen; q->addon = -1; q->slot = 2; q->hashed = 0; q->attempt = p->attempt; q->seq = ++p->sub_seq[2];
        sub_get(p->addon_subs[ref].url, on_sub_file, q);
        return;
    }
    if (!p->ref_loaded) return;
    if (p->bm_state == 1) return;   /* the best file is being chosen; sync that one */
    p->sync_for = p->active_addon_sub;
    if (subs_sync(&p->ref, &p->track)) {   /* offset, frame-rate drift and cuts */
        double off = p->track.offset;
        p->track.offset += sub_fix_get(p->addon_subs[p->active_addon_sub].url);
        int cuts = 0;
        for (int w = 0; w < SUBS_WINDOWS; w++) if (p->track.window_fix[w] != 0 && (!w || p->track.window_fix[w] != p->track.window_fix[w - 1])) cuts++;
        int drift = p->track.scale > 0 && fabs(p->track.scale - 1) > 0.001;
        p->sync_offset = off; p->sync_state = 2;
        LOGI("subs: auto-synced %+.2f s scale %.6f cuts %d against %s (%s)", off, p->track.scale, cuts, p->addon_subs[ref].addon, p->addon_subs[ref].lang);
        if (drift || cuts) app_toast("Subtitles synced to the file (%+.1f s%s%s)", off, drift ? ", frame rate fixed" : "", cuts ? ", cuts fixed" : "");
        else if (fabs(off) >= 0.15) app_toast("Subtitles synced to the file (%+.1f s)", off); else app_toast("Subtitles already in sync with the file");
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
    if (p != g_live || q->gen != p->gen || q->attempt != p->attempt) { free(q); return; }   /* the file of an earlier source */
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
    SubReq *q = calloc(1, sizeof *q);
    q->owner = p; q->gen = p->gen; q->addon = -1; q->slot = 0; q->attempt = p->attempt;
    HttpOptions opt = { .range_bytes = 2u << 20, .range_start = 0, .timeout_s = 8, .no_json_accept = 1, .headers = p->cands[p->cur].headers };
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
        /* The TV decodes one at a time (a few hundred ms each). While the knob moves, ask
         * for the next picture only when the last one is in, or once the knob rests; until
         * then show the nearest picture already decoded rather than nothing. */
        static struct { char src[1600]; long secs[24]; int n; long pending; double moved_at; long last; } T;
        if (strcmp(T.src, enc) != 0) { memset(&T, 0, sizeof T); snprintf(T.src, sizeof T.src, "%s", enc); T.pending = -1; }
        if (!frames_supported()) return 0;
        #define THUMB_URL(s) (snprintf(rel, sizeof rel, "frame?t=%ld&w=384&u=%s", (long)(s), enc), phone_local_url(rel, url, sizeof url))
        char rel[1800];
        if (sec != T.last) { T.last = sec; T.moved_at = g_app.time; }
        int have = 0;
        for (int i = 0; i < T.n; i++) if (T.secs[i] == sec) have = 1;
        int pending_open = T.pending >= 0 && THUMB_URL(T.pending) && image_pending(url);
        int ready_here = THUMB_URL(sec) && image_peek(url, NULL, NULL);
        /* ask for this spot: new, or turned away earlier ("busy") and now resting on it */
        if (!ready_here && !pending_open && (!have || g_app.time - T.moved_at > 0.25) && THUMB_URL(sec)) {
            int pw, ph;
            image_get(url, &pw, &ph);
            if (!have) { if (T.n == 24) { memmove(T.secs, T.secs + 1, sizeof T.secs[0] * 23); T.n--; } T.secs[T.n++] = sec; }
            T.pending = sec;
        }
        unsigned best = 0; long best_d = -1;
        for (int i = 0; i < T.n; i++) {
            if (!THUMB_URL(T.secs[i])) continue;
            int pw = 0, ph = 0;
            unsigned tex = image_peek(url, &pw, &ph);   /* never asks: one decode at a time */
            long dd = labs(T.secs[i] - sec);
            if (tex && (best_d < 0 || dd < best_d)) { best = tex; best_d = dd; *w = pw; *h = ph; }
        }
        #undef THUMB_URL
        return best;
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

/* Neighbouring frames must already be cached. Only seek_thumb at the selected
 * time may schedule a decode: scrubbing never launches five TV decoders. */
static unsigned aperture_cached_frame(const Player *p, double t, int *w, int *h)
{
    if (p->cur >= p->n_cands || !p->cands[p->cur].url || strncmp(p->cands[p->cur].url, "http", 4)) return 0;
    char enc[1600], url[2048], rel[1800];
    http_url_encode(p->cands[p->cur].url, enc, sizeof enc);
    long sec = (long)(t / 10) * 10;
    if (g_app.thumb_server && *g_app.thumb_server)
        snprintf(url, sizeof url, "%s/frame?url=%s&t=%ld", g_app.thumb_server, enc, sec);
    else {
        snprintf(rel, sizeof rel, "frame?t=%ld&w=384&u=%s", sec, enc);
        if (!phone_local_url(rel, url, sizeof url)) return 0;
    }
    return image_peek(url, w, h);
}

static void draw_aperture_filmstrip(Player *p, const VideoInfo *vi, double pos, float by)
{
    int span = g_app.low_memory ? 0 : 2;
    float total = 344 + span * 2 * (230 + 16), x = (SNTV_UI_W - total) / 2;
    float bottom = by - 96;
    gfx_gradient_v(0, by - 410, SNTV_UI_W, 350, RGBA(0, 0, 0, 0), RGBA(0, 0, 0, 0xD0));
    for (int i = -span; i <= span; i++) {
        double t = pos + i * 10;
        float w = i == 0 ? 344 : 230, h = w * 9 / 16;
        if (t < 0 || t > vi->duration) { x += w + 16; continue; }
        int iw = 0, ih = 0;
        unsigned tex = i == 0 ? seek_thumb(p, t, &iw, &ih, 0) : aperture_cached_frame(p, t, &iw, &ih);
        gfx_rect(x, bottom - h, w, h, THEME_BG2);
        if (tex && iw > 0 && ih > 0) {
            /* Letterbox previews; never stretch a decoded frame. */
            float dw = w, dh = dw * ih / iw;
            if (dh > h) { dh = h; dw = dh * iw / ih; }
            gfx_tex(tex, x + (w - dw) / 2, bottom - h + (h - dh) / 2, dw, dh, RGBA(255, 255, 255, i == 0 ? 255 : 150));
        } else if (i == 0) font_draw_center("Preview unavailable", x + w / 2, bottom - h / 2, FONT_XS, THEME_TEXT_DIM);
        char label[32]; ui_format_time(t, label, sizeof label);
        font_draw_center(label, x + w / 2, bottom + 18, FONT_XS, i == 0 ? THEME_TEXT : THEME_TEXT_DIM);
        if (i == 0) {
            gfx_rect(x, bottom + 5, w, 3, THEME_ACCENT);
            int ch = chapter_at(p, t);
            if (ch >= 0) font_draw_fit(p->probe->chapters[ch].name, x, bottom - h - 34, FONT_XS, THEME_TEXT, w);
        }
        x += w + 16;
    }
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
    SubReq *q = calloc(1, sizeof *q);
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
    SubReq *q = calloc(1, sizeof *q);
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
    p->viewer_paused = 0;
    video_play();   /* paused when Skip was pressed: the episode goes on from there all the same */
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

static void start_candidate(Player *p, int idx, double start);
static void buffer_alternates(const Player *p, int idx);
static void fail_over(Player *p, const char *why);

/* A torrent TorBox has cached: its link comes from TorBox when the source's turn comes */
typedef struct { Player *owner; int idx; double start; char *link; unsigned attempt; } TbReq;

static void on_torbox_open(const char *url, const char *error, void *user)
{
    TbReq *q = user;
    Player *p = q->owner;
    int current = p == g_live && !p->finished && q->attempt == p->attempt && q->idx == p->cur && !strcmp(p->cands[q->idx].url, q->link);
    if (current) p->tb_pending = 0;
    const Screen *top = app_top();
    int on_top = (top && top->st == p) || (g_app.pip && g_app.pip->st == p);
    if (current && url) {
        free(p->cands[q->idx].url);
        p->cands[q->idx].url = strdup(url);
        if (on_top) start_candidate(p, q->idx, q->start);
        else { p->tb_held = 1; p->tb_held_start = q->start; LOGI("player: TorBox has source %d; it starts when the player is back on top", q->idx + 1); }   /* never under a break */
    } else if (current) {
        LOGW("player: source %d not from TorBox: %s", q->idx + 1, error ? error : "?");
        snprintf(p->tb_error, sizeof p->tb_error, "TorBox: %s", error ? error : "it could not open it");
        fail_over(p, p->tb_error);
    }
    free(q->link);
    free(q);
}

/* Subtitles that come with the stream ("subtitles" in its answer): made for this file. */
static void add_stream_subs(Player *p, const PlayCandidate *c)
{
    if (!c->subs) return;
    int added = 0;
    char addon[96];
    snprintf(addon, sizeof addon, "%.*s", c->label ? (int)strcspn(c->label, "-") : 0, c->label ? c->label : "");
    while (addon[0] && addon[strlen(addon) - 1] == ' ') addon[strlen(addon) - 1] = 0;
    for (const char *line = c->subs; *line && p->n_addon_subs < MAX_ADDON_SUBS; ) {
        const char *tab = strchr(line, '\t'), *end = strchr(line, '\n');
        if (!tab || !end || tab > end) break;
        char lang[16], url[1024];
        snprintf(lang, sizeof lang, "%.*s", (int)(tab - line), line);
        snprintf(url, sizeof url, "%.*s", (int)(end - tab - 1), tab + 1);
        line = end + 1;
        int seen = 0;
        for (int k = 0; k < p->n_addon_subs && !seen; k++) if (!strcmp(p->addon_subs[k].url, url)) { seen = 1; p->addon_subs[k].matched = 1; }
        if (seen) continue;
        AddonSub *as = &p->addon_subs[p->n_addon_subs++];
        memset(as, 0, sizeof *as);
        as->url = strdup(url); as->lang = strdup(lang[0] ? lang : "und");
        as->addon = strdup(addon[0] ? addon : "This source");
        as->matched = 1;   /* sent with the file itself */
        added++;
    }
    if (added) { LOGI("subs: %d subtitle(s) came with the source", added); if (!p->subs_user) p->subs_auto_done = 0; }
}

static void start_candidate(Player *p, int idx, double start)
{
    if (idx != p->cur) {   /* another source: its own count, and its own file */
        p->net_retries = 0; p->net_played = 0;
        p->os_asked = 0;
        for (int k = 0; k < p->n_addon_subs; k++) p->addon_subs[k].matched = 0;   /* matched to the old file; this one's hash re-marks them */
    }
    p->attempt++;
    p->tb_pending = 0; p->tb_held = 0;   /* an earlier TorBox open is not this start's */
    p->audio_bad = 0; p->audio_req_at = -100;
    p->net_retry_at = 0; p->net_reconnecting = 0;   /* a source chosen meanwhile, or Reload, replaces a pending reconnect */
    for (int r = 0; r < 2; r++)
        if (torbox_is_hash_link(p->cands[idx].url) && p->ready[r].url && p->ready[r].link && !strcmp(p->ready[r].link, p->cands[idx].url)
            && g_app.time - p->ready[r].at < 1800) {
            LOGI("player: source %d was made ready on TorBox ahead", idx + 1);   /* no wait for TorBox now */
            free(p->cands[idx].url);
            p->cands[idx].url = p->ready[r].url;
            p->ready[r].url = NULL;
            free(p->ready[r].link); p->ready[r].link = NULL;
        }
    p->cur = idx;
    p->start_at = start;
    p->error_handled = 0;
    p->learned = 0;
    p->probe_asked = 0;
    p->yt_noted = 0;
    free(p->probe);
    p->probe = NULL;
    p->rate_applied = 0;
    p->hash_state = 0; p->sync_for = -1; p->sync_state = 0; p->ref_idx = -1; p->ref_loaded = 0; subs_free(&p->ref);
    p->auto_selected = 0;
    p->subs_auto_done = 0;
    p->bm_state = 0;
    p->load_started = g_app.time;
    if (idx < 64) p->tried |= 1ull << idx;
    p->started = 0; p->p2p_peers = -1; p->p2p_info = 0; p->thumbs_warm = 0;
    p->stalls_here = 0; p->lighter_to = -1;
    for (int w = 0; w < 2; w++) { p->warm[w].state = 0; p->warm[w].idx = -1; p->warm[w].at = g_app.time; }
    p->drain_n = 0; p->drain_t = 0;
    p->good_since = 0; p->good_recorded = 0; p->lighter_offered = 0;
    LOGI("player: candidate %d/%d %s @%.0fs -> %s", idx + 1, p->n_cands, p->cands[idx].label ? p->cands[idx].label : "", start, p->cands[idx].url);
    add_stream_subs(p, &p->cands[idx]);
    /* a file over HTTP plays through the local relay, with a few minutes loaded ahead (its
     * addon's headers too, when it asks for them); not P2P, a playlist or a trailer */
    const char *u = p->cands[idx].url;
    int bufferable = !p->trailer && !strncmp(u, "http", 4) && !torrent_engine_url_is(u) && !torbox_is_hash_link(u) && !strstr(u, ".m3u8") && !strstr(u, ".mpd")
                     && relay_buffer_enabled();
    char *relayed = NULL;
    if (idx < 16 && !p->relay_off[idx]) {
        if (bufferable) {
            double mins = quality_runtime_minutes(p->meta.runtime);
            double rate = p->cands[idx].video_size && mins > 0 ? p->cands[idx].video_size / (mins * 60) : 0;   /* until the file's own length is known */
            relayed = relay_url_buffered(streams_final_url(u) ? streams_final_url(u) : u, p->cands[idx].headers, rate);
            if (relayed) buffer_alternates(p, idx);
        } else if (p->cands[idx].headers) relayed = relay_url(u, p->cands[idx].headers);
    }
    if (!(bufferable && relayed)) relay_buffer_close();   /* this one isn't buffered: the last one's memory goes */
    if (relayed && p->cands[idx].headers) LOGI("player: source %d asks for request headers; it plays through the local relay, which sends them", idx + 1);
    else if (relayed) LOGI("player: source %d plays through the local relay, loaded ahead", idx + 1);
    else if (p->cands[idx].headers) LOGI("player: source %d asks for request headers; the TV's player can't send them (its checks did), so it may not play", idx + 1);
    p->p2p_status[0] = 0;
    p->tb_error[0] = 0;
    if (torbox_is_hash_link(p->cands[idx].url)) {
        if (video_load_count() != p->own_load) video_stop();   /* a trailer still on the plane; this player's own source plays on meanwhile */
        p->error_handled = 1;   /* nothing to watch over until TorBox answers */
        p->tb_pending = 1;
        TbReq *q = malloc(sizeof *q);
        q->owner = p; q->idx = idx; q->start = start; q->link = strdup(p->cands[idx].url); q->attempt = p->attempt;
        EpisodeHint ep;
        app_episode_hint(p->meta.id, p->meta.name, p->video_id, &ep);
        torbox_open_hash(p->cands[idx].url, &ep, on_torbox_open, q);
        show_overlay(p);
        return;
    }
    if (torrent_engine_url_is(p->cands[idx].url)) {
        if (!torrent_engine_start(3000)) LOGW("player: the P2P engine did not answer");
        const char *e = getenv("SNTV_P2P_RECHECK");   /* tests */
        p->p2p_recheck_in = e ? (float)atof(e) : 120;
        p->p2p_stats_in = 0;
        app_toast("No cached copy: playing P2P on this TV. It switches back when TorBox has it.");
        torbox_queue_hash(p->cands[idx].url + strlen("http://127.0.0.1:11490/"));   /* your TorBox fetches it meanwhile */
    }
    const char *load = p->cands[idx].url, *final = !relayed && idx < 16 && !p->final_off[idx] ? streams_final_url(load) : NULL;
    p->final_used = final != NULL;
    p->relay_used = relayed != NULL;
    p->relay_dur = 0;
    if (relayed) load = relayed;
    if (final) {   /* checked moments ago: straight to the file, without the redirects */
        const char *hs = strstr(final, "://"); hs = hs ? hs + 3 : final;
        LOGI("player: source %d opens where its link led a moment ago (%.*s)", idx + 1, (int)strcspn(hs, "/:?"), hs);
        load = final;
    }
    if (video_load(load, p->title, start) != 0) app_toast("Could not start playback");
    free(relayed);
    p->own_load = video_load_count();
    push_sub_style(p);   /* re-sent by the backend once the new pipeline has loaded */
    show_overlay(p);
}

/* The source to try after this one fails, or -1. After two failures in a row (TorBox
 * down, its links dead) it goes straight to the P2P fallback when there is one. */
/* Links that may lead to the very file playing, for the buffer to carry on from if its
 * source stops partway: the link itself (where it led may expire), then other sources
 * with the same file name, size or hash. The relay proves a copy byte for byte first. */
static void buffer_alternates(const Player *p, int idx)
{
    const PlayCandidate *c = &p->cands[idx];
    const char *urls[6], *headers[6];
    int n = 0;
    if (streams_final_url(c->url)) { urls[n] = c->url; headers[n++] = c->headers; }
    for (int i = 0; i < p->n_cands && n < 6; i++) {
        const PlayCandidate *o = &p->cands[i];
        if (i == idx || (i < 16 && p->dead[i]) || strncmp(o->url, "http", 4) || torrent_engine_url_is(o->url) || torbox_is_hash_link(o->url)) continue;
        int same = (c->video_size && o->video_size == c->video_size) || (c->filename && o->filename && !strcmp(c->filename, o->filename))
                   || (c->video_hash && o->video_hash && !strcasecmp(c->video_hash, o->video_hash));
        if (same) { urls[n] = streams_final_url(o->url) ? streams_final_url(o->url) : o->url; headers[n++] = o->headers; }
    }
    if (n) LOGI("player: %d other link%s may be the same file, for the buffer to carry on from", n, n == 1 ? "" : "s");
    relay_buffer_alternates(urls, headers, n);
}

static int next_candidate(const Player *p)
{
    for (int i = p->cur + 1; p->tries >= 2 && i < p->n_cands; i++)
        if (torrent_engine_url_is(p->cands[i].url)) return i;
    /* the next one not already found dead (checked while this one played) */
    for (int i = p->cur + 1; i < p->n_cands; i++) {
        if (i < 16 && p->dead[i]) continue;
        if (p->tries < 3 || torrent_engine_url_is(p->cands[i].url)) return i;
        break;
    }
    /* then any above it not tried yet (a source picked from the panel can sit last) */
    for (int i = 0; i < p->cur && i < 64 && p->tries < 3; i++)
        if (!(p->tried >> i & 1) && !(i < 16 && p->dead[i])) return i;
    return -1;
}

/* The sources standing by: 0 the next, 1 the one after it (not dead, not the one on) */
static int standby(const Player *p, int slot)
{
    int a = next_candidate(p);
    if (slot == 0 || a < 0) return a;
    for (int i = a + 1; i < p->n_cands; i++)
        if (i != p->cur && !(i < 16 && p->dead[i]) && !torrent_engine_url_is(p->cands[i].url)) return i;
    return -1;
}

/* What is on now, so the next source opens the same way. */
static void keep_choices(Player *p)
{
    VideoTrack tr[MAX_TRACKS];
    int n = video_tracks(TRACK_AUDIO, tr, MAX_TRACKS);
    const char *a = current_audio_lang(tr, n);
    snprintf(p->keep_audio, sizeof p->keep_audio, "%s", a ? a : p->audio_lang);
    p->keep_sub_kind = 0;
    if (p->active_addon_sub >= 0) { p->keep_sub_kind = 1; p->keep_sub_idx = p->active_addon_sub; }
    else if (p->embedded_text_sel >= 0) {
        int nt = video_tracks(TRACK_TEXT, tr, MAX_TRACKS);
        for (int i = 0; i < nt; i++) if (tr[i].index == p->embedded_text_sel) { p->keep_sub_kind = 2; snprintf(p->keep_sub_lang, sizeof p->keep_sub_lang, "%s", tr[i].lang); }
    } else if (p->subs_auto_done) p->keep_sub_kind = 3;
}

static void health_record(Player *p, int what)
{
    char addon[96];
    cand_addon(&p->cands[p->cur], addon, sizeof addon);
    srchealth_record(p->cands[p->cur].url, addon, what);
}

/* Silent rescue: the next source, at the same moment, with the same audio and subtitles. */
static void rescue(Player *p, const char *why, double at)
{
    int next = next_candidate(p);
    if (next < 0) return;
    keep_choices(p);
    p->tries++;
    char addon[96];
    cand_addon(&p->cands[next], addon, sizeof addon);
    LOGI("player: rescue (%s) -> source %d at %.0fs", why, next + 1, at);
    app_toast("Carrying on with %s from the same moment", addon[0] ? addon : "another source");
    start_candidate(p, next, at);
}

static float env_s(const char *name, float def) { const char *e = getenv(name); return e && *e ? (float)atof(e) : def; }

/* A connection that dropped after the video played (webOS: code 300, or 40000-49999) is
 * not a dead source: the same one again from the same moment, after 3, 10 and 25 s,
 * before giving it up. 1 when a retry is set. */
static int net_retry(Player *p, const VideoInfo *vi)
{
    const char *c = strstr(vi->error, "(code ");
    int code = c ? atoi(c + 6) : 0;
    if (p->net_retries >= 3 || !(code == 300 || (code >= 40000 && code < 50000))) return 0;
    if (!p->net_played && p->net_retries >= 1) return 0;   /* never played: one more try, then it is the source */
    static const float WAIT[] = { 3, 10, 25 };
    float scale = env_s("SNTV_NET_RETRY_SCALE", 1);
    if (!p->net_reconnecting) p->net_retry_pos = vi->position > 5 ? vi->position : p->start_at;   /* a failed reconnect keeps the first drop's place */
    p->net_retry_at = g_app.time + WAIT[p->net_retries] * scale;
    p->net_retries++;
    LOGI("player: the connection dropped (%s); the same source again in %.0f s", vi->error, WAIT[p->net_retries - 1] * scale);
    return 1;
}

/* The TV can't decode the audio track on: another one, the same language first (a
 * stereo AAC next to the DTS), else any other; none left: another source, from here. */
static void audio_repair(Player *p, const VideoInfo *vi)
{
    VideoTrack tr[MAX_TRACKS];
    int n = video_tracks(TRACK_AUDIO, tr, MAX_TRACKS), cur = -1;
    for (int i = 0; i < n; i++) if (tr[i].selected) cur = i;
    if (g_app.time - p->audio_req_at < 3)   /* just asked for: that is the one the warning is about */
        for (int i = 0; i < n; i++) if (tr[i].index == p->audio_req) cur = i;
    if (cur >= 0 && tr[cur].index < 32) p->audio_bad |= 1u << tr[cur].index;
    int pick = -1;
    for (int pass = 0; pass < 2 && pick < 0; pass++)
        for (int i = 0; i < n && pick < 0; i++) {
            if (i == cur || (tr[i].index < 32 && (p->audio_bad >> tr[i].index & 1))) continue;
            if (pass == 0 && !(cur >= 0 && lang_same(tr[i].lang, tr[cur].lang))) continue;
            pick = i;
        }
    if (pick >= 0) {
        LOGI("player: audio track %d can't be played here; switching to %s %s", cur, tr[pick].lang, tr[pick].codec);
        video_select_track(TRACK_AUDIO, tr[pick].index);
        p->audio_req = tr[pick].index; p->audio_req_at = g_app.time;
        app_toast("This TV can't play that audio. Switched to %s", tr[pick].label[0] ? tr[pick].label : lang_display(tr[pick].lang));
        return;
    }
    if (next_candidate(p) >= 0) {
        LOGI("player: no audio track of source %d plays here; another source", p->cur + 1);
        keep_choices(p);
        rescue(p, "This TV can't play this source's audio", vi->position > 5 ? vi->position : p->start_at);
        return;
    }
    app_toast("This TV can't play any audio track of this source");
}

/* Only a failure the TV's decoders could cause teaches "this TV can't play that": the
 * pipeline refused the file (an error with a code) after opening it, not a network or
 * expired-link code, not TorBox, not a stall (no error at all). */
static int decoder_failure(const Player *p, const VideoInfo *vi)
{
    if (vi->state != VIDEO_ERROR || p->tb_error[0]) return 0;
    if (strstr(vi->error, "player error")) return 1;   /* Tizen's player: the file */
    if (strstr(vi->error, "refused this stream")) return 0;   /* webOS would not even load it: the link, not the decoders */
    const char *c = strstr(vi->error, "(code ");
    int code = c ? atoi(c + 6) : 0;
    if (!code || code == 300 || (code >= 40000 && code < 50000)) return 0;   /* network, or no code: nothing about decoding */
    if (strstr(vi->error, "xpired") || strstr(vi->error, "403") || strstr(vi->error, "404")) return 0;
    return 1;
}

static void fail_over(Player *p, const char *why)
{
    VideoInfo vi;
    video_info(&vi);
    double resume = vi.position > 5 ? vi.position : 0;
    if (!p->learned && decoder_failure(p, &vi)) {   /* the error never names the decoder: only a source needing one feature says which failed */
        unsigned needs = p->cands[p->cur].needs;
        for (int c = 0; c < TVCAP_COUNT; c++) if (g_app.tv.ok[c] > g_app.tv.fail[c]) needs &= ~(1u << c);   /* played fine here before: not the cause */
        if (needs && !(needs & (needs - 1))) app_tv_learn(needs, 0);
        else if (needs) LOGI("tv profile: a source needing several features failed; none is blamed");
        p->learned = 1;
    }
    if (vi.position < 5) app_link_failed(p->cands[p->cur].url);   /* never started: sort it last for six hours */
    if (vi.position < 5) youtube_note_result(p->cands[p->cur].url, 0);   /* this TV and that way of playing YouTube */
    health_record(p, HEALTH_FAIL);
    int next = next_candidate(p);
    if (next >= 0) {
        if (resume > 0) { rescue(p, why, resume); return; }   /* mid-film: carry on as it was */
        p->tries++;
        LOGI("player: source %d failed (%s)", p->cur + 1, why);
        app_toast(torrent_engine_url_is(p->cands[next].url) ? "Trying P2P on this TV\xE2\x80\xA6" : "Trying another source\xE2\x80\xA6");
        start_candidate(p, next, p->start_at);   /* a resumed start keeps its place */
    } else {
        p->error_handled = 1;   /* stay on the error screen */
    }
}

/* ---- fallback while watching ------------------------------------------------------ */


typedef struct { Player *owner; unsigned gen, attempt; int idx, slot; } WarmReq;
static void on_warm(HttpResponse *r, void *user)
{
    WarmReq *q = user;
    Player *p = q->owner;
    if (p == g_live && q->gen == p->gen && q->attempt == p->attempt && q->idx == p->warm[q->slot].idx) {
        p->warm[q->slot].at = g_app.time;
        if (http_looks_like_media(r)) {
            p->warm[q->slot].state = 2; p->warm[q->slot].s = (float)r->seconds;
            LOGI("player: next source %d answers (%.1f s)", q->idx + 1, r->seconds);
        } else {
            p->warm[q->slot].state = 0;
            if (q->idx < 16) p->dead[q->idx] = 1;
            app_link_failed(p->cands[q->idx].url);
            LOGI("player: next source %d does not answer (%ld); the one after it stands by", q->idx + 1, r->status);
        }
    }
    free(q);
}

/* the next source is a TorBox torrent: its file's link, asked for while this one plays */
static void on_warm_tb(const char *url, const char *error, void *user)
{
    WarmReq *q = user;
    Player *p = q->owner;
    if (p == g_live && q->gen == p->gen && q->attempt == p->attempt && q->idx == p->warm[q->slot].idx) {
        p->warm[q->slot].at = g_app.time;
        if (url && torbox_is_hash_link(p->cands[q->idx].url)) {
            const char *key = p->cands[q->idx].url;
            int r = p->ready[0].link && strcmp(p->ready[0].link, key) && (!p->ready[1].link || !strcmp(p->ready[1].link, key));   /* its own place, else a free one */
            free(p->ready[r].url); free(p->ready[r].link);
            p->ready[r].url = strdup(url); p->ready[r].link = strdup(key); p->ready[r].at = g_app.time;
            p->warm[q->slot].state = 2;
            LOGI("player: next source %d is ready on TorBox", q->idx + 1);
        } else {
            p->warm[q->slot].state = 0;
            p->warm[q->slot].at = g_app.time + 120;   /* TorBox may still be fetching it: ask again later */
            LOGI("player: next source %d not ready on TorBox yet (%s)", q->idx + 1, error ? error : "?");
        }
    }
    free(q);
}

/* Every source failed: ask the addons again (debrid links often come back). */
static void on_reask(const Stream *items, int n, void *user)
{
    Player *p = user;
    if (p != g_live || p->finished) return;
    PlayCandidate *c = calloc(7, sizeof *c);
    int k = 0;
    for (int i = 0; i < n && k < 6; i++) if (streams_candidate(&p->meta, &items[i], &c[k])) k++;
    p->reask_count++;
    p->reask_at = 0;
    if (!k) { free(c); LOGI("player: asked again, still nothing to play"); app_toast("Still no source plays this. Try again in a little while"); return; }
    if (p->reask_pos <= 0) p->reask_pos = p->start_at;   /* it never played: where it was meant to start */
    LOGI("player: asked the addons again: %d sources, resuming at %.0fs", k, p->reask_pos);
    play_candidates_free(p->cands, p->n_cands);
    for (int r = 0; r < 2; r++) { free(p->ready[r].url); free(p->ready[r].link); p->ready[r].url = p->ready[r].link = NULL; }   /* the old list's */
    p->cands = c; p->n_cands = k; p->tries = 0;
    memset(p->dead, 0, sizeof p->dead);
    memset(p->relay_off, 0, sizeof p->relay_off); memset(p->final_off, 0, sizeof p->final_off);   /* about the old list's sources */
    p->cur = -1;   /* a new list: source 1 is a new source (its own retries, its own file) */
    app_toast("Found sources again. Carrying on from where it stopped");
    start_candidate(p, 0, p->reask_pos);
}

static void cand_drop(PlayCandidate *c)
{
    free(c->url); free(c->label); free(c->binge_group); free(c->filename); free(c->detail); free(c->release);
    free(c->headers); free(c->video_hash); free(c->subs);
}

/* "Find more sources": every addon's answer joins the panel's list while the video plays on. */
static void on_more_sources(const Stream *items, int n, void *user)
{
    Player *p = user;
    if (p != g_live || p->finished || p->more_state != 1) return;
    p->more_state = 2;
    int first = p->n_cands, added = 0;
    for (int i = 0; i < n && p->n_cands < 64; i++) {
        PlayCandidate c;
        if (!streams_candidate(&p->meta, &items[i], &c)) continue;
        int dup = 0;
        for (int j = 0; j < p->n_cands && !dup; j++) dup = strcmp(p->cands[j].url, c.url) == 0;
        if (dup) { cand_drop(&c); continue; }
        PlayCandidate *grown = realloc(p->cands, sizeof *grown * (size_t)(p->n_cands + 1));
        if (!grown) { cand_drop(&c); break; }
        p->cands = grown; p->cands[p->n_cands++] = c; added++;
    }
    LOGI("player: every addon asked from the Sources panel: %d more source%s", added, added == 1 ? "" : "s");
    if (!added) { app_toast("No other sources found"); app_redraw(); return; }
    if (p->menu == MENU_SOURCES && p->src_on_chips == 2 && p->src_chip == 0) {   /* the focus moves from Find more to the first new one */
        p->src_filter = 0; p->src_on_chips = 0;
        int rows[64], nr = source_rows(p, rows);
        for (int k = 0; k < nr; k++) if (rows[k] == first) { p->menu_focus = k; break; }
    }
    app_redraw();
}

static void find_more_sources(Player *p)
{
    if (p->trailer || p->more_state == 1) return;
    if (p->reask_at < 0) { app_toast("Already asking the addons"); return; }
    p->more_state = 1;
    LOGI("player: asking every addon for more sources, playback continues");
    streams_lookup(&p->meta, p->video_id, on_more_sources, p);
}

static void fallback_tick(Player *p, const VideoInfo *vi)
{
    if (p->trailer || p->finished) return;
    float now = g_app.time;
    int playing = vi->state == VIDEO_PLAYING;
    if (playing && vi->position > 5) p->reask_pos = vi->position;
    /* 0. the buffer's source stopped partway and no copy of the same file took over: the
     * next source while the buffer still plays, so the switch comes before the picture stops */
    if (playing && p->relay_used && relay_buffer_failing() && next_candidate(p) >= 0) {
        double more = relay_buffer_ahead(), ahead = (vi->buffer_ahead > 0 ? vi->buffer_ahead : 0) + (more > 0 ? more : 0);
        if (ahead < env_s("SNTV_SWITCH_AHEAD", 20)) {
            health_record(p, HEALTH_STALL);
            LOGI("player: the source stopped sending; %.0f s of video left, switching now", ahead);
            rescue(p, "The source stopped sending video", vi->position);
            return;
        }
    }
    /* 1. a source that stopped sending: the next one, from the same moment */
    float dead_after = env_s("SNTV_RESCUE_AFTER", p->warm[0].state == 2 ? 12 : 20);   /* sooner when the next one is known to work */
    if (playing && vi->buffering && p->stall_since > 0 && now - p->stall_since > dead_after && now - p->jumped_at > dead_after && next_candidate(p) >= 0) {
        health_record(p, HEALTH_STALL);
        p->stall_s += now - p->stall_since; p->stall_since = 0; p->was_buffering = 0;
        rescue(p, "The source stopped sending video", vi->position);
        return;
    }
    /* 2. a while of good playing counts for this server */
    if (playing && !vi->buffering) { if (!p->good_since) p->good_since = now; }
    else if (vi->buffering) p->good_since = 0;
    if (p->good_since && !p->good_recorded && now - p->good_since > env_s("SNTV_GOOD_AFTER", 600)) { p->good_recorded = 1; p->tries = 0; health_record(p, HEALTH_OK); }   /* later trouble starts the count afresh */
    /* 3. the next two sources checked while this one plays, so a switch is quick; the
     * second only once the first answered, never both at once */
    int room = !vi->buffering && (vi->buffer_ahead < 0 || vi->buffer_ahead > 30);   /* never competes with a tight connection */
    for (int w = 0; w < 2; w++) {
        int idx = standby(p, w);
        if (idx != p->warm[w].idx) { p->warm[w].idx = idx; p->warm[w].state = 0; }
        if (p->warm[w].state == 2 && now - p->warm[w].at > 600) p->warm[w].state = 0;   /* links age: check again */
        if (w == 1 && p->warm[0].state != 2) break;
        if (!playing || !room || idx < 0 || p->warm[w].state != 0 || now - p->load_started < env_s("SNTV_WARM_AFTER", 30) || now - p->warm[w].at < 5) continue;
        if (torbox_is_hash_link(p->cands[idx].url)) {
            p->warm[w].state = 1; p->warm[w].at = now;
            WarmReq *q = malloc(sizeof *q);
            q->owner = p; q->gen = p->gen; q->attempt = p->attempt; q->idx = idx; q->slot = w;
            EpisodeHint ep;
            app_episode_hint(p->meta.id, p->meta.name, p->video_id, &ep);
            torbox_open_hash(p->cands[idx].url, &ep, on_warm_tb, q);
        } else if (!strncmp(p->cands[idx].url, "http", 4) && !torrent_engine_url_is(p->cands[idx].url)) {
            p->warm[w].state = 1; p->warm[w].at = now;
            WarmReq *q = malloc(sizeof *q);
            q->owner = p; q->gen = p->gen; q->attempt = p->attempt; q->idx = idx; q->slot = w;
            HttpOptions o = { .range_bytes = 65536, .timeout_s = 12, .no_json_accept = 1 };   /* as the TV's player will ask: no headers */
            http_request_async_ex(p->cands[idx].url, &o, on_warm, q);
        } else p->warm[w].at = now + 60;
    }
    /* 4. the buffer is draining: a lighter copy before playback stops */
    if (playing && !vi->buffering && vi->buffer_ahead >= 0 && now - p->jumped_at > 8 && now - p->drain_t >= env_s("SNTV_DRAIN_EVERY", 5)) {
        p->drain_t = now;
        if (p->drain_n == 6) { memmove(p->drain_at, p->drain_at + 1, 5 * sizeof(float)); memmove(p->drain_v, p->drain_v + 1, 5 * sizeof(float)); p->drain_n = 5; }
        double more = p->relay_used ? relay_buffer_ahead() : -1;   /* with the buffer ahead, the whole of what is ready */
        p->drain_at[p->drain_n] = now; p->drain_v[p->drain_n] = (float)(vi->buffer_ahead + (more > 0 ? more : 0)); p->drain_n++;
        if (p->drain_n >= 4 && !p->lighter_offered) {
            float span = p->drain_at[p->drain_n - 1] - p->drain_at[0];
            float slope = span > 0 ? (p->drain_v[p->drain_n - 1] - p->drain_v[0]) / span : 0;
            float left = p->drain_v[p->drain_n - 1];
            if (slope < -0.2f && left < 25) {
                int cur = p->cands[p->cur].res ? p->cands[p->cur].res : 1081, best = -1;
                const char *grp = p->cands[p->cur].binge_group;
                for (int pass = 0; pass < 2 && best < 0; pass++)   /* the same release first */
                    for (int i = 0; i < p->n_cands; i++) {
                        if (i == p->cur || (i < 16 && p->dead[i]) || p->cands[i].res <= 0 || p->cands[i].res >= cur) continue;
                        if (pass == 0 && !(grp && p->cands[i].binge_group && !strcmp(grp, p->cands[i].binge_group))) continue;
                        if (best < 0 || p->cands[i].res > p->cands[best].res) best = i;
                    }
                p->lighter_offered = 1;
                if (best >= 0) {
                    LOGI("player: buffer draining (%.1f s left, %.2f s/s); switching to source %d (%dp) before it stops", left, slope, best + 1, p->cands[best].res);
                    keep_choices(p);
                    app_toast("The connection is falling behind: switching to %dp before it stops", p->cands[best].res);
                    start_candidate(p, best, vi->position);
                    return;
                }
                LOGI("player: buffer draining (%.1f s left) and no lighter source", left);
            }
        }
    }
    /* 5. nothing left to try: ask the addons again, twice at most */
    if ((vi->state == VIDEO_ERROR || p->tb_error[0]) && p->error_handled && !p->net_retry_at && next_candidate(p) < 0 && p->reask_count < 2 && !p->tb_pending && !p->tb_held) {   /* TorBox still on it: not yet */
        if (p->reask_at == 0) { p->reask_at = now + env_s("SNTV_REASK_AFTER", 2); LOGI("player: every source failed; asking the addons again for fresh links"); }
        else if (p->reask_at > 0 && now >= p->reask_at) { p->reask_at = -1; if (p->more_state == 1) p->more_state = 0; streams_lookup_auto(&p->meta, p->video_id, on_reask, p); }
    }
    if (p->reask_at < 0 || p->more_state == 1) streams_lookup_tick();
}

static void go_to_video(Player *p, const char *video_id, const char *video_title)
{
    float ready = streams_prefetch_progress(video_id);   /* for Diagnostics: was it fetched ahead? */
    if (ready < 0) LOGI("player: on to %s, its sources not fetched yet", video_id);
    else LOGI("player: on to %s, %.0f%% of its addons answered ahead%s", video_id, ready * 100, p->next_warmed ? ", its file warmed" : "");
    report(p, 1);
    if (p->up_next_shown && p->known_dur > 0 && !p->trailer)   /* on from the credits: this one is finished (it stopped short of 90%) */
        app_library_progress(&p->meta, p->video_id, p->known_dur, p->known_dur, 1);
    video_stop();
    MetaPreview meta;
    meta_preview_copy(&meta, &p->meta);
    char *vid = strdup(video_id), *title = video_title ? strdup(video_title) : NULL;
    /* the next one plays from the source most like this one: same release, addon, quality */
    const PlayCandidate *c = &p->cands[p->cur];
    ChainHint hint = { .addon_order = c->addon_order, .res = c->res, .needs = c->needs };
    if (c->binge_group) snprintf(hint.binge_group, sizeof hint.binge_group, "%s", c->binge_group);
    quality_release_group(c->filename, hint.group, sizeof hint.group);
    if (!hint.group[0]) quality_release_group(c->release, hint.group, sizeof hint.group);
    p->finished = 1;
    app_pop();   /* destroys this screen */
    const Screen *under = app_top();
    Screen *next = screen_streams_create_chain(&meta, vid, title, &hint);
    if (under && !strcmp(under->name, "streams")) app_replace(next);   /* the last episode's sources give way */
    else app_push(next);   /* started from the title page (Resume): it stays under, for Back */
    meta_preview_free(&meta);
    free(vid); free(title);
}

static void go_up_next(Player *p) { go_to_video(p, p->next_video_id, p->next_title); }

/* Episodes started by themselves in a row with no key pressed: after this many, the next
 * one waits for "Still watching?" (a whole season must not play to someone asleep). */
static int g_auto_streak;
static float g_auto_streak_input = -1;
static int still_watching_due(void)
{
    if (g_app.last_input != g_auto_streak_input) g_auto_streak = 0;   /* a key since: someone is there */
    return g_auto_streak >= env_s("SNTV_STILL_WATCHING_AFTER", 3);
}

static void go_up_next_auto(Player *p)   /* the countdown or the episode's end, nobody asked */
{
    if (g_app.last_input != g_auto_streak_input) g_auto_streak = 0;
    g_auto_streak++;
    g_auto_streak_input = g_app.last_input;
    go_up_next(p);
}

/* The next episode can start: its sources are in and the best is checked (or nothing was
 * fetched ahead at all, so there is nothing to wait for). */
static int next_ready(const Player *p)
{
    float k = p->next_video_id ? streams_prefetch_progress(p->next_video_id) : -1;
    return k < 0 || k >= 1;
}

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
        p->p2p_peers = peers; p->p2p_info = info;
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
    for (int i = 15; i > p->cur && i > 0; i--) { p->dead[i] = p->dead[i - 1]; p->relay_off[i] = p->relay_off[i - 1]; p->final_off[i] = p->final_off[i - 1]; }   /* the rest moved down one */
    if (p->cur < 16) p->dead[p->cur] = p->relay_off[p->cur] = p->final_off[p->cur] = 0;
    LOGI("player: TorBox has it again, leaving P2P at %.0f s for %s", vi.position, c.label);
    app_toast("TorBox has it again: switched back to the cached copy");
    p->tries = 0;
    start_candidate(p, p->cur, vi.position);
    if (!torbox_is_hash_link(p->cands[p->cur].url)) torrent_engine_drop(old);   /* its RAM goes now; it comes back if TorBox fails (a TorBox link: P2P plays on until it answers) */
    free(old);
}

static void on_p2p_recheck(const Stream *items, int n, void *user)
{
    Player *p = user;
    if (p != g_live || p->finished || p->cur >= p->n_cands || !torrent_engine_url_is(p->cands[p->cur].url)) return;
    int want = p->cands[p->cur].res;
    for (int i = 0; i < n; i++) {   /* ranked best first: the first cached copy at least as sharp */
        const Stream *s = &items[i];
        int tb = !s->url && torbox_hash_cached(s->info_hash);   /* your TorBox finished the torrent */
        if (!tb && (!s->url || strncmp(s->url, "http", 4) || s->tags.cached <= 0 || app_link_dead(s->url))) continue;
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
    if ((p->p2p_recheck_in -= dt) <= 0 && p->more_state != 1) {   /* not over the Sources panel's own lookup */
        const char *e = getenv("SNTV_P2P_RECHECK");
        p->p2p_recheck_in = e ? (float)atof(e) : 120;
        streams_lookup_auto(&p->meta, p->video_id, on_p2p_recheck, p);
    }
}

static void update(Screen *s, float dt)
{
    Player *p = s->st;
    if (p->finished) return;
    if (p->tb_held) { p->tb_held = 0; start_candidate(p, p->cur, p->tb_held_start); }   /* back on top: TorBox's answer starts now */
    VideoInfo vi;
    video_info(&vi);
    if (gfx_handheld()) video_pip_allow(app_top() == s && !p->pip && vi.state == VIDEO_PLAYING);   /* Home takes a playing video along */
    picture_tap_settle(p);
    if (p->tap_pending) app_redraw();
    if (vi.state == VIDEO_PLAYING) app_redraw();   /* progress bar / subtitles move */
    if (vi.state == VIDEO_PLAYING && !p->start_timed) { p->start_timed = 1; perf_start_done(1); }   /* the first frame: the start is over */
    if (vi.state == VIDEO_PAUSED && p->paused_since == 0) { p->paused_since = g_app.time; app_redraw(); }
    if (vi.state != VIDEO_PAUSED && p->paused_since != 0) p->paused_since = 0;
    if (vi.state == VIDEO_PLAYING || vi.state == VIDEO_LOADING) p->viewer_paused = 0;
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
    if (vi.state == VIDEO_PLAYING && vi.position > 2 && !p->yt_noted) { p->yt_noted = 1; youtube_note_result(p->cands[p->cur].url, 1); }
    if (!p->tracked && !p->trailer && vi.state == VIDEO_PLAYING && vi.duration > 0) { p->tracked = 1; tracking_progress(&p->meta, p->video_id, vi.position, vi.duration, 0, 0); }
    if (p->seek_commit > 0) { p->seek_commit -= dt; if (p->seek_commit <= 0) video_seek(p->seek_target); }

    if (vi.tracks_version != p->last_tracks_version) { p->last_tracks_version = vi.tracks_version; app_redraw(); }
    if (vi.warning_version != p->last_warning_version) {
        p->last_warning_version = vi.warning_version;
        int audio = vi.warning[0] && strstr(vi.warning, "audio");
        if (vi.warning[0] && !audio) app_toast("%s", vi.warning);
        if (audio && !p->learned) {   /* "can't play this audio": remember the audio codec */
            app_tv_learn(p->cands[p->cur].needs & ((1u << TVCAP_DTS) | (1u << TVCAP_TRUEHD)), 0);
            p->learned = 1;
        }
        if (audio) audio_repair(p, &vi);
    }
    if (vi.state == VIDEO_PLAYING || vi.state == VIDEO_PAUSED) { auto_select_tracks(p); second_auto(p); }
    if (p->relay_used && vi.duration > 0 && vi.duration != p->relay_dur) { p->relay_dur = vi.duration; relay_set_duration(vi.duration); }   /* the buffer's two minutes, from the file's size over its length */
    night_tick(p, &vi);

    if (vi.state == VIDEO_ERROR && !p->error_handled && p->relay_used && !p->net_played && p->cur < 16 && !relay_buffer_source_failed()) {   /* the relay did not get it going: the link directly, as before it existed (not when the source itself refused it) */
        p->relay_off[p->cur] = 1;
        LOGI("player: source %d did not start through the relay; opening its link directly", p->cur + 1);
        start_candidate(p, p->cur, p->start_at);
        video_info(&vi);
    }
    if (vi.state == VIDEO_ERROR && !p->error_handled && p->final_used && !p->net_played && p->cur < 16) {   /* the address it led to may have expired: the link itself */
        p->final_off[p->cur] = 1;
        LOGI("player: the address source %d led to did not play; opening its link", p->cur + 1);
        start_candidate(p, p->cur, p->start_at);
        video_info(&vi);
    }
    if (vi.state == VIDEO_ERROR && !p->error_handled) { p->error_handled = 1; if (!net_retry(p, &vi)) fail_over(p, vi.error[0] ? vi.error : "Playback failed"); }
    if (p->net_retry_at > 0 && g_app.time >= p->net_retry_at) {
        p->net_retry_at = 0;
        LOGI("player: reconnecting to source %d at %.0fs (try %d of 3)", p->cur + 1, p->net_retry_pos, p->net_retries);
        keep_choices(p);
        start_candidate(p, p->cur, p->net_retry_pos);
        p->net_reconnecting = 1;
    }
    if (p->net_reconnecting && p->started && vi.state == VIDEO_PLAYING) p->net_reconnecting = 0;
    if (p->started && vi.state == VIDEO_PLAYING && !p->net_played) { p->net_played = 1; remember_playing(p); }
    if (p->net_retries && vi.state == VIDEO_PLAYING && vi.position > p->net_retry_pos + 10) p->net_retries = 0;   /* it holds again */
    /* never got going: still loading, or loaded but buffering at the start, past its time.
     * A torrent nobody seeds goes sooner; one with peers gets longer to fetch its start. */
    if (vi.state == VIDEO_PLAYING && !vi.buffering && vi.position > 0) p->started = 1;   /* not the position alone: a resume reports it before any picture */
    if (p->started && p->pause_when_started) { p->pause_when_started = 0; video_pause(); p->viewer_paused = 1; LOGI("player: paused again at %.0fs, as before the break", vi.position); }
    if (p->catchup_until < 0 && p->started) p->catchup_until = g_app.time + 12;
    if (p->replay_until > 0 && vi.state == VIDEO_PLAYING && vi.position >= p->replay_until + 1) {
        if (p->replay_subs) { select_addon_sub(p, -1); video_select_track(TRACK_TEXT, -1); p->embedded_text_sel = -1; LOGI("player: replay over, subtitles off again"); }
        p->replay_until = 0; p->replay_subs = 0;
    }
    if (p->veil > 0 && (p->started || error_final(p, &vi))) { p->veil -= dt * 2.5f; if (p->veil < 0) p->veil = 0; app_redraw(); }
    else if (p->veil > 0) app_redraw();   /* its progress line moves */
    /* the first seek would wait seconds for the keyframe index: read it now, once playback
     * has settled, with one preview a minute ahead (not for P2P: the index sits at the end
     * of the file, which a torrent would then fetch first) */
    if (!p->thumbs_warm && p->started && vi.state == VIDEO_PLAYING && !vi.buffering && !p->trailer && vi.duration > 120
        && g_app.time - p->load_started > env_s("SNTV_THUMBS_WARM_AFTER", 15) && p->cur < p->n_cands && !torrent_engine_url_is(p->cands[p->cur].url)) {
        p->thumbs_warm = 1;
        int tw, th;
        if (!seek_thumb(p, vi.position + 60, &tw, &th, 0)) LOGI("player: seek previews warming up");
    }
    if (!p->started && !p->error_handled && !p->trailer && (vi.state == VIDEO_LOADING || vi.state == VIDEO_IDLE || vi.buffering)) {
        int p2p = p->cur < p->n_cands && torrent_engine_url_is(p->cands[p->cur].url);
        float waited = g_app.time - p->load_started;
        float limit = env_s("SNTV_START_TIMEOUT", 45);
        if (p2p) limit = p->p2p_peers == 0 ? env_s("SNTV_P2P_NO_PEERS", 25) : p->p2p_info ? env_s("SNTV_P2P_START", 90) : env_s("SNTV_P2P_START", 90) * 2 / 3;
        if (waited > limit) {
            LOGI("player: source %d did not start in %.0fs%s", p->cur + 1, waited, p2p ? (p->p2p_peers == 0 ? " (P2P, no peers)" : " (P2P, too slow)") : "");
            p->error_handled = 1;
            p->learned = 1;   /* slow or unseeded, not a format this TV cannot play */
            fail_over(p, p2p && p->p2p_peers == 0 ? "Nobody is sharing this torrent" : "Source is not responding");
        }
    }

    if (!p->learned && vi.state == VIDEO_PLAYING && vi.position > 0 && g_app.time - p->load_started > 90 && p->stalls == 0) {
        app_tv_learn(p->cands[p->cur].needs, 1);
        p->learned = 1;
    }
    if (!p->probe_asked && (vi.state == VIDEO_PLAYING || vi.state == VIDEO_PAUSED)) probe_current(p);
    if (!p->hash_state && (vi.state == VIDEO_PLAYING || vi.state == VIDEO_PAUSED)) hash_start(p);
    opensubs_tick(p);
    bestmatch_tick(p, &vi);
    fallback_tick(p, &vi);
    autosync_tick(p);
    xl_tick(p, dt, vi.position);
    skip_fetch(p, &vi);
    if (g_app.skip_mode == 1 && vi.state == VIDEO_PLAYING && !p->menu) { const SkipSeg *sk = skippable(p, &vi); if (sk) do_skip(p, sk); }

    if (p->pip) {   /* no Up Next card in the corner: finish quietly and hand the plane back */
        if (vi.state == VIDEO_ENDED || error_final(p, &vi)) {
            report(p, 1);
            if (vi.state == VIDEO_ENDED) app_toast("Finished: %s", p->title); else app_toast("Couldn't keep playing: %s", p->tb_error[0] ? p->tb_error : vi.error);
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
    const TmdbInfo *tmi = p->trailer ? NULL : tmdb_get(p->meta.id, p->meta.type);
    /* the rating and what it contains, once the starting controls step aside, if IMDb
     * answered within the first twenty seconds */
    if (vi.state == VIDEO_PLAYING && !p->played_at) p->played_at = g_app.time;
    if (p->played_at && !p->adv_at && !p->trailer && g_app.advisory && g_app.time - p->played_at > 1.5f && g_app.time - p->played_at < 20
        && g_app.time >= p->overlay_until && !p->menu) {
        const Advisory *adv = advisory_get(p->meta.id);
        char d[160] = "";
        if (adv) advisory_describe(adv, d, sizeof d);
        if (d[0]) { p->adv_at = g_app.time; LOGI("player: advisory shown: %s", d); }
    }
    int stinger = tmi ? tmi->stinger : 0;
    if (stinger && !p->stinger_noted && vi.state == VIDEO_PLAYING && vi.duration > 600
        && (credits > 60 ? vi.position >= credits : vi.position > vi.duration - 420)) {   /* the credits start (or roughly, without markers) */
        p->stinger_noted = 1;
        LOGI("player: the film has a scene %s the credits", stinger == 1 ? "during" : stinger == 2 ? "after" : "during and after");
        app_toast(stinger == 1 ? "Keep watching: there is a scene during the credits" : "Keep watching: there is a scene after the credits");
    }
    if (stinger) at_credits = 0;   /* Up Next waits for the very end, after the scene */
    if (p->up_next_dismissed && !at_credits && !near_end) p->up_next_dismissed = 0;   /* seeked back out of the window */
    if (p->next_video_id && (at_credits || near_end) && !p->up_next_shown && !p->up_next_dismissed) {
        p->up_next_shown = 1;
        p->next_at = at_credits && !near_end ? credits + 20 : vi.duration;
        if (p->next_at > vi.duration) p->next_at = vi.duration;
        LOGI("player: Up Next at %.0fs (%s), plays at %.0fs", vi.position, at_credits ? "credits" : "end", p->next_at);
        show_overlay(p);
    }
    /* the next episode's sources, asked for once this one has played half a minute, so
     * Up Next, an early Next or a source that dies at the end all find them ready; asked
     * again in the last 12 minutes when they are over half an hour old (links age), before
     * the next file is warmed from them */
    int want_next = p->up_next_shown || p->bar_focus || (p->started && g_app.time - p->load_started > env_s("SNTV_PREFETCH_AFTER", 30))
                    || (vi.duration > 120 && (vi.position > vi.duration - 900 || vi.position > vi.duration * 0.6));   /* started late in the episode */
    float pre_age = p->next_video_id ? streams_prefetch_age(p->next_video_id) : -1;
    if (p->next_video_id && !p->prefetch_renewed && pre_age > env_s("SNTV_PREFETCH_RENEW", 1800) && vi.duration > 120 && vi.position > vi.duration - 720
        && vi.state == VIDEO_PLAYING && streams_prefetch_progress(p->next_video_id) >= 1) {
        p->prefetch_renewed = 1;
        p->next_warmed = 0;   /* the warm goes to the new answers */
        streams_prefetch_again(&p->meta, p->next_video_id);
    }
    if (p->next_video_id && want_next && vi.state != VIDEO_LOADING && streams_prefetch_progress(p->next_video_id) < 0
        && (!p->prefetched || g_app.time - p->prefetch_tried > 30)) {
        p->prefetched = 1;
        p->prefetch_tried = g_app.time;
        streams_prefetch(&p->meta, p->next_video_id);
    }
    if (p->next_video_id && !p->next_warmed && vi.duration > 0 && vi.position > vi.duration - 600 && vi.state == VIDEO_PLAYING && !vi.buffering
        && (vi.buffer_ahead < 0 || vi.buffer_ahead > 30))   /* never at this video's expense */
        p->next_warmed = streams_prefetch_warm(p->next_video_id, p->cands[p->cur].binge_group);
    next_progress_update(p, dt);
    if (fabs(vi.position - p->pos_seen) > 3 + dt * 4 || p->seek_commit > 0) { p->jumped_at = g_app.time; p->jump_pos = vi.position; }   /* seeking, or just sought */
    p->pos_seen = vi.position;
    int settled = g_app.time - p->jumped_at > 8 && vi.state == VIDEO_PLAYING;
    if (vi.buffering && !p->was_buffering && vi.state != VIDEO_LOADING) { p->stalls++; if (settled) p->stalls_here++; p->stall_since = g_app.time; }
    /* it keeps stopping to buffer: offer the best source below this one's resolution, once */
    if (!p->lighter_offered && !p->trailer && p->n_cands > 1 && vi.state != VIDEO_LOADING
        && (p->stalls_here >= 3 || (settled && vi.buffering && p->stall_since > 0 && g_app.time - p->stall_since > 10))) {
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
    if (p->up_next_shown && p->next_video_id && ((g_app.autoplay_next && p->next_at < vi.duration && vi.position >= p->next_at && !still_watching_due()) || p->next_wanted)
        && vi.state == VIDEO_PLAYING && !p->menu && next_ready(p)) {   /* never under an open menu, and not before it's ready: the credits play on meanwhile */
        LOGI("player: the next episode is ready%s", p->next_wanted ? " (asked for on the card)" : "");
        if (p->next_wanted) go_up_next(p); else go_up_next_auto(p);
        return;
    }
    if (p->up_next_shown && !at_credits && !near_end) { p->up_next_shown = 0; p->next_wanted = 0; }   /* seeked back out of its window (with or without markers) */
    if (vi.state == VIDEO_ENDED) {
        if (p->next_video_id && (g_app.autoplay_next || p->next_wanted)) {   /* asked for on the card counts too */
            if (p->menu) { p->menu = 0; LOGI("player: the episode ended with a menu open; closing it for the next one"); }   /* nobody left to close it */
            if (p->next_wanted) { go_up_next(p); return; }
            if (!still_watching_due()) { go_up_next_auto(p); return; }
            if (!p->still_asking) { p->still_asking = 1; LOGI("player: %d episodes went on by themselves; asking if anyone is still watching", g_auto_streak); app_redraw(); }
            return;   /* the card waits for an answer */
        }
        p->finished = 1;
        report(p, 1);
        app_pop();
    }
}

/* ---- drawing ------------------------------------------------------------------ */

static void draw_subtitle(Player *p, const VideoInfo *vi)
{
    static char b1[2048], b2[2048], up[2048];   /* every line on screen now: a sign over the dialogue shows with it */
    const char *text = p->track_loaded && subs_text_at(&p->track, vi->position, 0, b1, sizeof b1) ? b1 : NULL;
    const char *text2 = p->track2_loaded && subs_text_at(&p->track2, vi->position, 0, b2, sizeof b2) ? b2 : NULL;
    const char *top = p->track_loaded && subs_text_at(&p->track, vi->position, 1, up, sizeof up) ? up : NULL;
    float bottom = g_app.subs_pos == 2 ? 260 : g_app.subs_pos == 1 ? SNTV_UI_H - 220 : SNTV_UI_H - 96;
    if (top) {   /* the file asks for the top ({\an8}): over a credit or a sign at the bottom */
        int lines = 1;
        for (const char *c = top; *c; c++) lines += *c == '\n';
        float lh = ui_subtitle_line_h(sub_size(p)), tb = (g_app.time < p->overlay_until && !subs_tuning(p) ? 150 : 72) + lines * lh;   /* under the title while it shows */
        if (g_app.subs_pos == 2 && bottom < tb + 24 + lh) bottom = tb + 24 + lh * 2;   /* yours at the top too: under it */
        ui_subtitle(top, tb, sub_size(p), g_app.subs_style);
    }
    if (g_app.subs_pos != 2 && g_app.time < p->overlay_until && !subs_tuning(p) && bottom > SNTV_UI_H - 250) bottom = SNTV_UI_H - 250;   /* above the controls */
    int r = subs_tuning(p) ? p->subs_row : -1;
    if (!text && !text2 && p->embedded_text_sel < 0 && (r == SUB_SIZE || r == SUB_STYLE || r == SUB_POS))
        text = "Subtitles will look like this";   /* nothing said right now: a sample to judge by (not over the TV's own subtitles) */
    float k = p->menu == MENU_SUBS ? 1 - (1 - p->tune_t) * (1 - p->tune_t) : 0;
    gfx_set_translate(-250 * k, 0);   /* clear of the Look and timing panel */
    if (text) ui_subtitle(text, bottom, sub_size(p), g_app.subs_style);
    if (text2) {   /* the second language sits above the first (or where it would be) */
        float lift = text ? ui_subtitle_line_h(sub_size(p)) * 2 + 24 : 0;
        ui_subtitle(text2, bottom - lift, sub_size(p) > 0 ? sub_size(p) - 1 : 0, g_app.subs_style);
    }
    gfx_set_translate(0, 0);
}

/* The Sources sheet during playback, after nuvio-native-legacy's: a tall card on the right,
 * the video still playing beside it under a side veil. Quality leads: the source this TV
 * should play first, then 4K / 1080p / 720p / the rest (HDR before SDR), the largest file
 * first, with its size in big figures; the playing one carries a moving equaliser. */
static void draw_sources_panel(Player *p)
{
    const float SW = 820, SX = SNTV_UI_W - 64 - SW, SY = 32, SH = SNTV_UI_H - SY - 48, PAD = 40;
    const float ROW_H = 112, ROW_GAP = 4, EXTRA = 34, HEAD_H = 52;
    if (p->menu_seen != p->menu) { p->menu_seen = p->menu; p->menu_t = g_app.fx_light ? 1 : 0; }
    if (p->menu_t < 1) { p->menu_t += 0.1f; if (p->menu_t > 1) p->menu_t = 1; app_redraw(); }
    float e = 1 - (1 - p->menu_t) * (1 - p->menu_t);
    gfx_gradient_h(SX - 700, 0, 700 + SW + 64, SNTV_UI_H, RGBA(0, 0, 0, 0), RGBA(0, 0, 0, (int)(0x6B * e)));   /* a side veil, no full one */
    gfx_set_alpha(e);
    gfx_set_translate(40 * (1 - e), 0);
    gfx_rrect(SX, SY, SW, SH, 36, RGBA(0x15, 0x16, 0x1A, 0xF6));
    float cx = SX + PAD, cw = SW - 2 * PAD;

    /* kicker: what is playing, and whether more are coming */
    char kick[200];
    ui_upper(p->title ? p->title : "", kick, sizeof kick);
    font_draw_tracked(kick, cx, SY + 34, FONT_XS | FONT_B, COLOR_ALPHA(THEME_TEXT, 0x73), 1.5f);
    if (p->more_state == 1) font_draw("ASKING EVERY ADDON\xE2\x80\xA6", cx + cw - font_measure("ASKING EVERY ADDON\xE2\x80\xA6", FONT_XS | FONT_B), SY + 34, FONT_XS | FONT_B, THEME_ACCENT);

    /* title row, its buttons at the right: Find more, Close */
    font_draw("Sources", cx, SY + 70, FONT_L | FONT_B, THEME_TEXT);
    const char *find = p->more_state == 1 ? "Asking\xE2\x80\xA6" : p->more_state == 2 ? "Sources page" : "Find more";
    float bh = 56, bw0 = font_measure(find, FONT_XS | FONT_B) + 84, bw1 = 56, bx1 = cx + cw - bw1, bx0 = bx1 - 10 - bw0, by = SY + 64;
    for (int b = 0; b < 2; b++) {
        int foc = p->src_on_chips == 2 && p->src_chip == b;
        float bx = b ? bx1 : bx0, bw = b ? bw1 : bw0;
        gfx_rrect(bx, by, bw, bh, bh / 2, foc ? THEME_ACCENT : COLOR_ALPHA(THEME_TEXT, 0x14));
        Color fg = foc ? THEME_ON_ACCENT : THEME_TEXT;
        if (b) ui_icon(ICON_CLOSE, bx + bw / 2, by + bh / 2, 22, fg);
        else {
            if (p->more_state == 1) ui_spinner(bx + 32, by + bh / 2, 10); else ui_icon(ICON_RELOAD, bx + 32, by + bh / 2, 22, fg);
            font_draw(find, bx + 56, by + (bh - font_height(FONT_XS | FONT_B)) / 2, FONT_XS | FONT_B, fg);
        }
    }

    /* addon tabs with their counts, in a segmented track */
    char names[SRC_ADDONS][80], label[120];
    int na = source_addons(p, names), rows[64], n = source_rows(p, rows);
    float ty = SY + 146, th = 56;
    gfx_rrect(cx, ty, cw, th, th / 2, COLOR_ALPHA(THEME_TEXT, 0x0D));
    float tw[SRC_ADDONS + 1], tx[SRC_ADDONS + 1], at = 0;
    for (int t = 0; t <= na; t++) {
        int count = 0;
        for (int i = 0; i < p->n_cands; i++) { char ad[80]; cand_addon(&p->cands[i], ad, sizeof ad); count += !t || !strcmp(ad, names[t - 1]); }
        snprintf(label, sizeof label, "%s  %d", t ? names[t - 1] : "All", count);
        tw[t] = font_measure(label, FONT_XS | FONT_B) + 40; tx[t] = at; at += tw[t] + 6;
    }
    if (p->src_filter > na) p->src_filter = 0;   /* the sources were replaced while the panel was open */
    float shift = tx[p->src_filter] + tw[p->src_filter] > cw - 12 ? tx[p->src_filter] + tw[p->src_filter] - (cw - 12) : 0;
    gfx_clip_push(cx + 6, ty, cw - 12, th);
    for (int t = 0; t <= na; t++) {
        int count = 0;
        for (int i = 0; i < p->n_cands; i++) { char ad[80]; cand_addon(&p->cands[i], ad, sizeof ad); count += !t || !strcmp(ad, names[t - 1]); }
        snprintf(label, sizeof label, "%s  %d", t ? names[t - 1] : "All", count);
        int sel = p->src_filter == t, foc = sel && p->src_on_chips == 1;
        float x = cx + 6 + tx[t] - shift;
        if (sel) gfx_rrect(x, ty + 6, tw[t], 44, 22, foc ? THEME_ACCENT : COLOR_ALPHA(THEME_TEXT, 0x22));
        font_draw(label, x + 20, ty + 6 + (44 - font_height(FONT_XS | FONT_B)) / 2, FONT_XS | FONT_B, foc ? THEME_ON_ACCENT : sel ? THEME_TEXT : THEME_TEXT_DIM);
    }
    gfx_clip_pop();

    /* the list */
    float ly = ty + th + 22, lh = SY + SH - 24 - ly;
    if (!n) {
        font_draw_center(p->more_state == 1 ? "Asking the addons for sources\xE2\x80\xA6" : "No sources from this addon", SX + SW / 2, ly + 140, FONT_S | FONT_B, THEME_TEXT_DIM);
        gfx_set_translate(0, 0); gfx_set_alpha(1);
        return;
    }
    /* where each row sits: a heading before each new group (not before the pinned pick) */
    float ys[64], acc = 0;
    int heads[64];
    for (int k = 0; k < n; k++) {
        int pinned = k == 0 && p->cands[rows[0]].recommended;
        heads[k] = !pinned && (k == 0 || (k == 1 && p->cands[rows[0]].recommended) || cand_group(&p->cands[rows[k]]) != cand_group(&p->cands[rows[k - 1]]));
        if (heads[k]) acc += HEAD_H;
        ys[k] = acc;
        acc += ROW_H + ROW_GAP + (!p->src_on_chips && p->menu_focus == k ? EXTRA : 0);
    }
    float bottom = p->src_on_chips ? 0 : ys[p->menu_focus] + ROW_H + EXTRA;   /* the focused row (and its heading) in view */
    float scroll = bottom > lh ? bottom - lh + 8 : 0;
    gfx_clip_push(SX + 8, ly - 4, SW - 16, lh + 8);
    for (int k = 0; k < n; k++) {
        const PlayCandidate *c = &p->cands[rows[k]];
        float y = ly + ys[k] - scroll;
        int foc = !p->src_on_chips && p->menu_focus == k, on = rows[k] == p->cur;
        float h = ROW_H + (foc ? EXTRA : 0);
        if (heads[k] && y - HEAD_H < ly + lh && y > ly - HEAD_H) {   /* "1080P  ·  HDR ...... 3 sources" */
            int g = cand_group(c), cnt = 0;
            for (int j = k; j < n && cand_group(&p->cands[rows[j]]) == g; j++) cnt++;
            static const char *const RES[] = { "4K ULTRA HD", "1080P", "720P", "OTHER" };
            char head[64], right[32];
            snprintf(head, sizeof head, "%s  \xC2\xB7  %s", RES[g / 2], g % 2 ? "SDR" : "HDR");
            snprintf(right, sizeof right, "%d source%s", cnt, cnt == 1 ? "" : "s");
            font_draw_tracked(head, cx, y - HEAD_H + 14, FONT_XS | FONT_B, COLOR_ALPHA(THEME_TEXT, 0x8C), 1.2f);
            font_draw(right, cx + cw - font_measure(right, FONT_XS), y - HEAD_H + 14, FONT_XS, THEME_TEXT_MUTED);
            gfx_rect(cx, y - 12, cw, 1, RGBA(255, 255, 255, 0x14));
        }
        if (y > ly + lh || y + h < ly) continue;
        if (foc) gfx_rrect(cx - 16, y, cw + 32, h, 22, RGBA(255, 255, 255, 0x1F));
        float tx0 = cx, rw = 170, tw0 = cw - rw - 12, yy = y + 14;
        /* the mark line */
        const char *mark = on ? "PLAYING NOW" : (k == 0 && c->recommended) ? "BEST FOR THIS TV" : (rows[k] < 16 && p->dead[rows[k]]) ? "FAILED HERE" : NULL;
        if (mark) {
            float mx = tx0;
            if (on) {   /* a small equaliser that moves while it plays */
                for (int b = 0; b < 4; b++) { float v = 0.35f + 0.65f * fabsf(sinf((float)g_app.time * (3.1f + b * 1.3f) + b)); gfx_rect(mx + b * 5, yy + 16 - 14 * v, 3, 14 * v, THEME_ACCENT); }
                mx += 28;
                app_redraw();
            }
            font_draw_tracked(mark, mx, yy, FONT_XS | FONT_B, mark[0] == 'F' ? THEME_DANGER : THEME_ACCENT, 1.2f);
            yy += 26;
        } else yy += 8;
        /* the name, a bolt before it when the addon has it cached */
        char addon[80], name[160];
        cand_addon(c, addon, sizeof addon);
        const char *lbl = c->label ? c->label : "Source", *sep = strstr(lbl, " - ");
        snprintf(name, sizeof name, "%s", sep ? sep + 3 : lbl);
        int cached = strstr(c->badges, "CACHED") != NULL;
        float nx = tx0;
        if (cached) { ui_icon(ICON_SPARK, nx + 10, yy + 16, 20, THEME_GOLD); nx += 28; }
        font_draw_fit(name, nx, yy, FONT_S | FONT_B, foc ? THEME_TEXT : COLOR_ALPHA(THEME_TEXT, 0xCD), tx0 + tw0 - nx);
        yy += 38;
        /* the quality line: "4K · HDR" in words, then the other badges */
        char rest[96] = "", q[48];
        int r = cand_res(c);
        snprintf(q, sizeof q, "%s", r >= 2160 ? "4K" : r >= 1080 ? "1080p" : r >= 720 ? "720p" : r ? "SD" : "");
        float qx = tx0;
        if (q[0]) { font_draw(q, qx, yy, FONT_XS | FONT_B, THEME_TEXT); qx += font_measure(q, FONT_XS | FONT_B) + 10; }
        const char *hdr = strstr(c->badges, "DOLBY") ? "DOLBY VISION" : strstr(c->badges, "HDR10+") ? "HDR10+" : strstr(c->badges, "HDR") ? "HDR" : "SDR";
        font_draw(hdr, qx, yy, FONT_XS | FONT_B, strcmp(hdr, "SDR") ? color_lerp(THEME_ACCENT, RGBA(0xC8, 0xC0, 0xB0, 0xFF), 0.45f) : THEME_TEXT_MUTED);
        qx += font_measure(hdr, FONT_XS | FONT_B) + 14;
        for (const char *t = c->badges; *t; ) {   /* codec and audio as badges; resolution, HDR, cached and size are said elsewhere */
            size_t len = strcspn(t, "|");
            char one[32]; snprintf(one, sizeof one, "%.*s", (int)len, t);
            int said = !strcmp(one, "4K") || strstr(one, "p") == one + strlen(one) - 1 || !strcmp(one, "SD") || !strcmp(one, "CACHED")
                     || strstr(one, "HDR") || strstr(one, "DOLBY") || strstr(one, " GB") || strstr(one, " MB");
            if (!said && strlen(rest) + len + 2 < sizeof rest) { if (rest[0]) strcat(rest, "|"); strcat(rest, one); }
            t += len + (t[len] == '|');
        }
        if (rest[0]) streams_draw_badges(rest, qx, yy - 4, tx0 + tw0 - qx, 0);
        /* the focused row opens to its file */
        if (foc) {
            const char *file = c->filename ? c->filename : c->release ? c->release : c->detail;
            if (file) font_draw_fit(file, tx0, yy + 34, FONT_XS, RGBA(0x7D, 0x7D, 0x7D, 0xFF), tw0 + rw);
        }
        /* the right column: the size in big figures, the addon under it */
        float rx = cx + cw;
        if (c->video_size) {
            char num[16];
            double gb = c->video_size / 1073741824.0;
            const char *unit = gb >= 1 ? "GB" : "MB";
            snprintf(num, sizeof num, gb >= 1 ? "%.1f" : "%.0f", gb >= 1 ? gb : gb * 1024);
            float uw = font_measure(unit, FONT_XS | FONT_B), nw2 = font_measure(num, FONT_M | FONT_B);
            font_draw(unit, rx - uw, y + 30, FONT_XS | FONT_B, THEME_TEXT_MUTED);
            font_draw(num, rx - uw - 6 - nw2, y + 20, FONT_M | FONT_B, THEME_TEXT);
        }
        char under[120];
        snprintf(under, sizeof under, "%s%s", cached ? "Cached  \xC2\xB7  " : "", addon);
        float uw2 = font_measure(under, FONT_XS);
        if (uw2 > rw) uw2 = rw;
        font_draw_fit(under, rx - uw2, y + 66, FONT_XS, THEME_TEXT_DIM, rw);
    }
    gfx_clip_pop();
    gfx_set_translate(0, 0);
    gfx_set_alpha(1);
}

static void draw_menu(Player *p)
{
    if (p->menu == MENU_SOURCES) { draw_sources_panel(p); return; }
    if (p->menu == MENU_SUBS) { draw_subs_panel(p); return; }
    float w = 600, x = SNTV_UI_W - w - 40, y = 40, h = SNTV_UI_H - 80;
    int aperture = g_theme.id == THEME_APERTURE;
    if (aperture) { w = 640; x = SNTV_UI_W - w; y = 0; h = SNTV_UI_H; }
    if (p->menu_seen != p->menu) {
        p->menu_seen = p->menu; p->menu_t = g_app.fx_light ? 1 : 0;
        if (getenv("SNTV_SCREENSHOT")) LOGI("UI check: player drawer=%d", p->menu);
    }
    if (p->menu_t < 1) { p->menu_t += 0.09f; if (p->menu_t > 1) p->menu_t = 1; app_redraw(); }
    { float e = 1 - (1 - p->menu_t) * (1 - p->menu_t); gfx_set_translate((w + 60) * (1 - e), 0); }
    if (!g_app.fx_light && !aperture) gfx_shadow(x, y, w, h, R_PANEL, 50, RGBA(0, 0, 0, 0xA0));
    ui_panel(x, y, w, h, R_PANEL, aperture ? THEME_PANEL : COLOR_ALPHA(THEME_PANEL, 0xF0));
    gfx_rrect_ring(x, y, w, h, R_PANEL, 1.5f, COLOR_ALPHA(THEME_TEXT, 0x1C));
    static const char *TITLES[] = { "", "Audio", "Subtitles", "Sources", "Episodes" };   /* by MENU_ */
    static const Icon ICONS[] = { ICON_PLAY, ICON_AUDIO, ICON_SUBS, ICON_SWAP, ICON_LIST };
    if (aperture) {
        font_draw(TITLES[p->menu], x + 40, y + 34, FONT_XL | FONT_ALT, THEME_TEXT);
        font_draw("Back  Close", x + 40, y + h - 40, FONT_XS, THEME_TEXT_DIM);
    } else {
        ui_icon(ICONS[p->menu], x + 56, y + 62, 40, THEME_TEXT);
        font_draw(TITLES[p->menu], x + 92, y + 40, FONT_L | FONT_B, THEME_TEXT);
    }
    VideoTrack tr[MAX_TRACKS];
    int n = video_tracks(p->menu == MENU_AUDIO ? TRACK_AUDIO : TRACK_TEXT, tr, MAX_TRACKS);
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
                        : 0;
    float step = p->menu == MENU_SOURCES ? 100 : 78;   /* a source row carries its badges and file name */
    float focus_y = 120 + sections_before * 40 + p->menu_focus * step;
    float bottom = h - (aperture ? 100 : 60);
    float scroll = focus_y + step > bottom ? focus_y + step - bottom : 0;
    float ry = y + 120 - scroll;
    int row = 0;
    gfx_clip_push(x, y + 104, w, h - (aperture ? 180 : 120));
    #define SECTION(t) do { font_draw(t, x + 40, ry + 6, FONT_XS | FONT_B, THEME_TEXT_MUTED); ry += 40; } while (0)
    const char *row_thumb = NULL;   /* set before a ROW to show a picture at its left */
    #define ROW(label, sub, on) do { \
        int foc_ = p->menu_focus == row; const char *sub_ = (sub); float lx_ = x + 50 + (row_thumb ? 118 : 0); \
        if (ry + 70 > y + 100 && ry < y + h - 16) { \
        if (foc_ && aperture) { gfx_rrect_ring(x + 24, ry, w - 48, 70, 3, 2, THEME_TEXT); gfx_rect(x + 26, ry + 66, w - 52, 3, THEME_ACCENT); } \
        else if (foc_) gfx_rrect(x + 24, ry, w - 48, 70, R_KEY, THEME_FOCUS); \
        else if ((on) && !aperture) gfx_rrect(x + 24, ry, w - 48, 70, R_PANEL, COLOR_ALPHA(THEME_TEXT, 0x14)); \
        if (row_thumb) ui_draw_thumb(row_thumb, x + 40, ry + 6, 104, 58, 0, COLOR_ALPHA(THEME_TEXT, 0x14)); \
        Color fg_ = foc_ && !aperture ? THEME_ON_FOCUS : THEME_TEXT; \
        font_draw_fit(label, lx_, ry + (sub_[0] ? 10 : 20), FONT_S | FONT_B, fg_, w - 190 - (lx_ - x - 50)); \
        if (sub_[0]) font_draw_fit(sub_, lx_, ry + 40, FONT_XS, foc_ && !aperture ? COLOR_ALPHA(THEME_BG, 0xB0) : THEME_TEXT_MUTED, w - 190 - (lx_ - x - 50)); \
        if ((on) && aperture) ui_icon(ICON_CHECK, x + w - 70, ry + 35, 26, THEME_TEXT); \
        else if (on) { gfx_circle(x + w - 70, ry + 35, 17, foc_ ? THEME_ON_FOCUS : THEME_ACCENT); ui_icon(ICON_CHECK, x + w - 70, ry + 35, 20, foc_ ? THEME_FOCUS : THEME_ON_ACCENT); } } \
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
            /* as on the Sources page: its badges (and its pick), the file under them */
            int foc = p->menu_focus == row, on = view[k] == p->cur;
            if (ry + 92 > y + 100 && ry < y + h - 16) {
                if (foc) gfx_rrect(x + 24, ry, w - 48, 92, R_KEY, THEME_FOCUS);
                else if (on) gfx_rrect(x + 24, ry, w - 48, 92, R_PANEL, COLOR_ALPHA(THEME_TEXT, 0x14));
                float bx = x + 44, room = w - 88 - (on ? 50 : 0);
                if (c->recommended) bx = streams_draw_badges("RECOMMENDED", bx, ry + 12, room, foc);
                if (c->badges[0]) bx = streams_draw_badges(c->badges, bx, ry + 12, x + 44 + room - bx, foc);
                else font_draw_fit(c->detail ? c->detail : name, bx, ry + 14, FONT_S | FONT_B, foc ? THEME_ON_FOCUS : THEME_TEXT, x + 44 + room - bx);
                const char *file = c->filename ? c->filename : c->release ? c->release : host;
                font_draw_fit(file, x + 50, ry + 56, FONT_XS, foc ? COLOR_ALPHA(THEME_BG, 0xB0) : THEME_TEXT_DIM, room);
                if (on) { gfx_circle(x + w - 70, ry + 46, 17, foc ? THEME_ON_FOCUS : THEME_ACCENT); ui_icon(ICON_CHECK, x + w - 70, ry + 46, 20, foc ? THEME_FOCUS : THEME_ON_ACCENT); }
            }
            ry += 100; row++;
        }
        if (p->more_state == 2) ROW("Open the Sources page", "Every source with its details; this video stops", 0);
        else if (p->more_state == 1) ROW("Asking every addon\xE2\x80\xA6", "The video keeps playing", 0);
        else ROW("Find more sources", "Asks every addon; the video keeps playing", 0);
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
    if (n) SECTION("IN THIS FILE");
    for (int i = 0; i < n; i++) {
        char title[96], sub[160];
        describe_track(p, p->menu == MENU_AUDIO ? 2 : 3, tr, i, n, title, sizeof title, sub, sizeof sub);
        int on = p->menu == MENU_AUDIO ? tr[i].selected : p->embedded_text_sel == tr[i].index;
        ROW(title, sub, on);
    }
    if (p->menu == MENU_AUDIO && !n) font_draw_wrapped("The stream reports a single audio track.", x + 40, ry, FONT_S, THEME_TEXT_MUTED, w - 80, 2);
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
    if (g_theme.id == THEME_APERTURE) {
        h = 200; y -= 24;
        ui_panel(x, y, w, h, 2, COLOR_ALPHA(THEME_PANEL, 0xF4));
        const char *thumb = app_video_thumbnail(p->meta.id, p->next_video_id);
        ui_card(thumb, x + 20, y + 20, 256, 144, 0, 0, NULL, CARD_MIP);
        float tx = x + 300, tw = w - 324;
        ui_spaced_caps("Up next", tx, y + 22, FONT_XS | FONT_B, 4, THEME_ACCENT, tw);
        font_draw_wrapped(p->next_title ? p->next_title : "Next episode", tx, y + 50, FONT_M | FONT_ALT, THEME_TEXT, tw, 2);
        if (g_app.autoplay_next && vi->duration > 0) {
            double at = p->next_at > 0 ? p->next_at : vi->duration, window = at < vi->duration ? 20.0 : 45.0, left = at - vi->position;
            char t[48]; if (left > 0.5 && !p->next_wanted) snprintf(t, sizeof t, "Starts in %ds", (int)left); else snprintf(t, sizeof t, "Starts once ready");
            font_draw(t, x + 20, y + h - 30, FONT_XS, THEME_TEXT_DIM);
            float lx = x + 20 + font_measure(t, FONT_XS) + 16, lw = 256 - (lx - x - 20);
            if (lw > 20) { gfx_rect(lx, y + h - 20, lw, 2, COLOR_ALPHA(THEME_TEXT, 0x30)); gfx_rect(lx, y + h - 20, lw * (float)(1.0 - left / window), 2, THEME_ACCENT); }
        }
        float k = next_progress(p), by = y + h - 84;
        if (k >= 0 && k < 1) {   /* the next episode's sources still arriving */
            float bw = ui_aperture_button("Preparing", -1, tx, by, 0, 1);
            gfx_rect(tx, by + 58, bw * k, 3, THEME_ACCENT);
        } else ui_aperture_button("Play now", ICON_PLAY, tx, by, 1, 1);
        return;
    }
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
        char t[48]; if (left > 0.5 && !p->next_wanted) snprintf(t, sizeof t, "Starts in %ds", (int)left); else snprintf(t, sizeof t, "Starts once ready");
        font_draw_fit(t, tx, y + h - 54, FONT_XS, THEME_TEXT_DIM, (x + w - 140) - 64 - tx);   /* clear of the OK beside the button */
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
    if (!p->next_wanted) font_draw("OK", gx - 16 - font_measure("OK", FONT_XS | FONT_B), gy + (40 - font_height(FONT_XS | FONT_B)) / 2, FONT_XS | FONT_B, THEME_TEXT_DIM);   /* pressed already: it waits */
}

static void draw_skip_button(Player *p, const SkipSeg *s, const VideoInfo *vi, int overlay, float card_top)
{
    int lit = !p->bar_focus;   /* OK skips, unless the controls' buttons have it */
    (void)overlay;
    const char *label = s->kind == SKIP_RECAP ? "Skip recap" : "Skip intro";
    float h = 72, w = ui_pill_width(label, 1, h);
    float x = SNTV_UI_W - THEME_MARGIN - w, y = overlay ? SNTV_UI_H - 330 : SNTV_UI_H - 200;
    if (card_top > 0 && y + h + 32 > card_top) y = card_top - h - 32;   /* above an Up Next or lighter-source card */
    if (!g_app.fx_light) gfx_shadow(x, y, w, h, h / 2, 30, RGBA(0, 0, 0, 0x90));
    ui_pill(label, 1, ICON_NEXT, x, y, h, lit ? PILL_PRIMARY : PILL_SECONDARY, lit);
    /* time left in the segment, as a thin line under the button */
    float k = (float)((vi->position - s->start) / (s->end - s->start));
    if (k < 0) k = 0;
    if (k > 1) k = 1;
    gfx_rrect(x + h / 2, y + h + 10, (w - h) * (1 - k), 4, 2, COLOR_ALPHA(THEME_TEXT, 0x90));
    const char *keyname = lit ? "OK" : "Up";   /* from the buttons, Up leaves them */
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
    if (!p->trailer && vi.duration > 0) { ids[k] = BTN_SCENES; labels[k] = "Scenes"; icons[k++] = ICON_CLOCK; }
    ids[k] = BTN_STATS; labels[k] = p->stats ? "Hide stats" : "Stats"; icons[k++] = ICON_INFO;
    const TmdbInfo *tm;   /* who's in it: most LG remotes have no Info key, so a button too */
    if (!p->trailer && cast_n(p, &tm) > 0) { ids[k] = BTN_CAST; labels[k] = "Cast"; icons[k++] = ICON_USER; }
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
    if (g_theme.id == THEME_APERTURE) {
        const float step = 112;
        float x0 = (SNTV_UI_W - (n - 1) * step) / 2;
        for (int i = 0; i < n; i++) {
            int foc = p->bar_focus && i == at;
            float cx = x0 + i * step;
            ui_icon(icons[i], cx, y + 24, 30, foc ? THEME_TEXT : THEME_TEXT_DIM);
            if (ids[i] == BTN_NEXT && next_progress(p) >= 0 && next_progress(p) < 1)
                ui_progress(cx - 22, y + 48, 44, 2, next_progress(p), THEME_TEXT_DIM);
            if (foc) {
                float lw = font_measure(labels[i], FONT_XS);
                font_draw(labels[i], cx - lw / 2, y + 50, FONT_XS, THEME_TEXT);
                gfx_rect(cx - lw / 2 - 4, y + 80, lw + 8, 3, THEME_ACCENT);
            }
        }
        if (!p->bar_focus) font_draw("Down  Controls", SNTV_UI_W - THEME_MARGIN - font_measure("Down  Controls", FONT_XS), y + 12, FONT_XS, THEME_TEXT_MUTED);
        return;
    }
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
        if (vi->state == VIDEO_PLAYING) { video_pause(); p->viewer_paused = 1; } else { video_play(); p->flash_icon = ICON_PLAY; p->play_flash = g_app.time + 0.6f; }
        break;
    case BTN_NEXT: go_up_next(p); return;
    case BTN_RELOAD: app_toast("Reloading"); start_candidate(p, p->cur, resume_spot(p, vi)); break;   /* mid-hiccup it reports 0: its real spot */
    case BTN_SUBS: p->menu = MENU_SUBS; subs_open(p); break;
    case BTN_AUDIO: p->menu = MENU_AUDIO; p->menu_focus = current_track_row(p, MENU_AUDIO); break;
    case BTN_ASPECT: p->aspect = (p->aspect + 1) % ASPECT_COUNT; apply_aspect(p); app_toast("Aspect: %s", ASPECT_NAMES[p->aspect]); break;
    case BTN_SOURCES: {   /* opens on All, on the source playing */
        p->menu = MENU_SOURCES; p->menu_focus = 0; p->src_filter = 0; p->src_chip = 0; p->src_on_chips = 0;
        int rows[64], n = source_rows(p, rows);
        for (int k = 0; k < n; k++) if (rows[k] == p->cur) p->menu_focus = k;
        if (p->n_cands <= 1 && p->more_state == 0) find_more_sources(p);   /* resumed from its saved link alone: the others, while it plays */
        break;
    }
    case BTN_EPISODES: p->menu = MENU_EPISODES; p->menu_focus = episodes_focus(p); break;
    case BTN_SCENES: p->scenes_open = 1; p->scenes_focus = scene_now(p, vi->position); p->overlay_until = 0; LOGI("player: scenes rail (%d)", scenes_list(p, vi->duration, NULL, NULL, 0)); break;
    case BTN_STATS: p->stats = !p->stats; break;
    case BTN_CAST: open_cast(p); break;
    }
    show_overlay(p);
}

/* How the source is holding up: where it comes from and how far ahead it has loaded,
 * amber when that runs low. */
static void draw_health(const Player *p, const VideoInfo *vi, float x)
{
    if (x > 1300 || p->trailer || vi->buffer_ahead < 0 || !p->started) return;
    const PlayCandidate *c = &p->cands[p->cur];
    const char *from = p->p2p_status[0] ? NULL : strstr(c->badges, "CACHED") ? "Cached" : !strncmp(c->url, "http", 4) ? "Direct" : NULL;
    char text[48];
    double more = p->relay_used ? relay_buffer_ahead() : -1, ahead;   /* the TV's own plus the relay's (asked once a frame) */
    if (more < 0) more = 0;
    if (vi->buffering) snprintf(text, sizeof text, "%s%sLoading", from ? from : "", from ? "  \xC2\xB7  " : "");
    else {
        ahead = vi->buffer_ahead + more;
        if (ahead >= 90) snprintf(text, sizeof text, "%s%s%d:%02d ahead", from ? from : "", from ? "  \xC2\xB7  " : "", (int)ahead / 60, (int)ahead % 60);
        else snprintf(text, sizeof text, "%s%s%.0f s ahead", from ? from : "", from ? "  \xC2\xB7  " : "", ahead);
    }
    int low = vi->buffering || vi->buffer_ahead + more < 10;
    Color amber = RGBA(0xF5, 0xA6, 0x23, 0xFF);
    ui_chip(text, x, 110, low ? COLOR_ALPHA(amber, 0x40) : COLOR_ALPHA(THEME_TEXT, 0x22), low ? amber : THEME_TEXT);
}

/* "1080p" and "Dolby Digital+ 5.1" from what the file (or the pipeline) says. */
static void media_badges(const Player *p, const VideoInfo *vi, char *res, size_t rn, char *aud, size_t an)
{
    res[0] = aud[0] = 0;
    int h = vi->height > 0 ? vi->height : p->probe ? p->probe->height : 0;
    int w = vi->height > 0 ? vi->width : p->probe ? p->probe->width : 0;
    /* by width too: a cinema-wide 1080p film is 1920x800, not 720p (as probe.c reads it) */
    if (h >= 2000 || w >= 3800) snprintf(res, rn, "4K");
    else if (h >= 1400 || w >= 2500) snprintf(res, rn, "1440p");
    else if (h >= 1000 || w >= 1900) snprintf(res, rn, "1080p");
    else if (h >= 700 || w >= 1200) snprintf(res, rn, "720p");
    else if (h > 0) snprintf(res, rn, "%dp", h);
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

/* "Previously": the episode before this one, while the picture starts and a little after;
 * OK or Back puts it away. */
/* ---- scenes and marked moments ------------------------------------------------- */

/* Marked moments live in the settings, per video: {"marks": {"<video id>": [secs, ...]}} */
static void marks_load(Player *p)
{
    const cJSON *all = store_get_json("marks"), *a = cJSON_GetObjectItemCaseSensitive(all, p->video_id), *v;
    p->n_marks = 0;
    cJSON_ArrayForEach(v, a) if (cJSON_IsNumber(v) && p->n_marks < MAX_MARKS) p->marks[p->n_marks++] = v->valuedouble;
}

static void marks_save(Player *p)
{
    cJSON *all = cJSON_Duplicate(store_get_json("marks"), 1);
    if (!cJSON_IsObject(all)) { cJSON_Delete(all); all = cJSON_CreateObject(); }
    cJSON_DeleteItemFromObjectCaseSensitive(all, p->video_id);
    while (cJSON_GetArraySize(all) >= 100) cJSON_DeleteItemFromArray(all, 0);   /* the oldest videos go */
    if (p->n_marks) {
        cJSON *a = cJSON_CreateArray();
        for (int i = 0; i < p->n_marks; i++) cJSON_AddItemToArray(a, cJSON_CreateNumber((int)p->marks[i]));
        cJSON_AddItemToObject(all, p->video_id, a);
    }
    store_set_json("marks", all);
    store_save();
}

static void mark_moment(Player *p, double t)
{
    for (int i = 0; i < p->n_marks; i++) if (fabs(p->marks[i] - t) < 5) { app_toast("Already marked"); return; }
    if (p->n_marks == MAX_MARKS) { memmove(p->marks, p->marks + 1, sizeof p->marks[0] * (MAX_MARKS - 1)); p->n_marks--; }
    int i = p->n_marks++;
    for (; i > 0 && p->marks[i - 1] > t; i--) p->marks[i] = p->marks[i - 1];
    p->marks[i] = t;
    marks_save(p);
    char at[16]; ui_format_time(t, at, sizeof at);
    LOGI("player: moment marked at %.0f s", t);
    app_toast("Marked %s. It is in Scenes", at);
}

/* The rail's cards after the first ("Mark this moment"): the chapters when the file has
 * them, else every 5 minutes, with the marked moments among them, in time order. Fills
 * t/kind (0 scene, 1 marked) up to n; returns how many there are. */
#define MAX_SCENES 64
static int scenes_list(const Player *p, double duration, double *t, int *kind, int n)
{
    double tt[MAX_SCENES]; int kk[MAX_SCENES], c = 0;
    if (p->probe && p->probe->n_chapters >= 2)
        for (int i = 0; i < p->probe->n_chapters && c < MAX_SCENES - MAX_MARKS; i++) { tt[c] = p->probe->chapters[i].start_s; kk[c++] = 0; }
    else
        for (double x = 0; x < duration - 30 && c < MAX_SCENES - MAX_MARKS; x += 300) { tt[c] = x; kk[c++] = 0; }
    for (int i = 0; i < p->n_marks; i++) { tt[c] = p->marks[i]; kk[c++] = 1; }
    for (int i = 1; i < c; i++)
        for (int j = i; j > 0 && tt[j] < tt[j - 1]; j--) { double a = tt[j]; tt[j] = tt[j - 1]; tt[j - 1] = a; int b = kk[j]; kk[j] = kk[j - 1]; kk[j - 1] = b; }
    for (int i = 0; i < c && i < n; i++) { t[i] = tt[i]; kind[i] = kk[i]; }
    return c;
}

/* The card the rail opens on: the scene playing now */
static int scene_now(const Player *p, double pos)
{
    double t[MAX_SCENES]; int kind[MAX_SCENES];
    VideoInfo vi; video_info(&vi);
    int n = scenes_list(p, vi.duration, t, kind, MAX_SCENES), k = 0;
    for (int i = 0; i < n; i++) if (t[i] <= pos + 0.5) k = i + 1;
    return k;
}

static void draw_scenes(Player *p, const VideoInfo *vi)
{
    if (!p->scenes_open || p->menu) return;
    double t[MAX_SCENES]; int kind[MAX_SCENES];
    int n = scenes_list(p, vi->duration, t, kind, MAX_SCENES) + 1;
    if (p->scenes_focus >= n) p->scenes_focus = n - 1;
    const float cw = 320, ch = 180, step = 344, h = 380;
    float x = 0, y = SNTV_UI_H - h, w = SNTV_UI_W;
    gfx_gradient_v(x, y - 120, w, 120, RGBA(0, 0, 0, 0), RGBA(0, 0, 0, 0xD8));
    gfx_rect(x, y, w, h, RGBA(0, 0, 0, 0xD8));
    font_draw("Scenes", THEME_MARGIN, y + 20, FONT_M | FONT_B, THEME_TEXT);
    const char *hint = "OK  Go there   /   Back  Close";
    font_draw(hint, SNTV_UI_W - THEME_MARGIN - font_measure(hint, FONT_XS), y + 28, FONT_XS, THEME_TEXT_MUTED);
    int fit = (int)((w - 2 * THEME_MARGIN + 24) / step), first = p->scenes_focus - fit / 2;
    if (first > n - fit) first = n - fit;
    if (first < 0) first = 0;
    for (int i = first; i < n && i < first + fit; i++) {
        float cx = THEME_MARGIN + (i - first) * step, cy = y + 80;
        int foc = i == p->scenes_focus;
        if (foc) gfx_rrect_ring(cx - 6, cy - 6, cw + 12, ch + 12, R_CARD + 6, 4, THEME_FOCUS);
        char label[64], at[16];
        if (i == 0) {
            gfx_rrect(cx, cy, cw, ch, R_CARD, COLOR_ALPHA(THEME_TEXT, 0x18));
            ui_icon(ICON_PLUS, cx + cw / 2, cy + ch / 2 - 10, 44, THEME_TEXT);
            ui_format_time(vi->position, at, sizeof at);
            font_draw_center("Mark this moment", cx + cw / 2, cy + ch + 14, FONT_XS | FONT_B, foc ? THEME_TEXT : THEME_TEXT_DIM);
            font_draw_center(at, cx + cw / 2, cy + ch + 44, FONT_XS, THEME_TEXT_MUTED);
            continue;
        }
        double st = t[i - 1];
        int iw = 0, ih = 0;
        unsigned tex = foc ? seek_thumb(p, st + 2, &iw, &ih, 0) : aperture_cached_frame(p, st + 2, &iw, &ih);
        gfx_rrect(cx, cy, cw, ch, R_CARD, THEME_BG2);
        if (tex && iw > 0 && ih > 0) {
            float dw = cw, dh = dw * ih / iw;
            if (dh > ch) { dh = ch; dw = dh * iw / ih; }
            gfx_tex(tex, cx + (cw - dw) / 2, cy + (ch - dh) / 2, dw, dh, RGBA(255, 255, 255, foc ? 255 : 170));
        }
        if (kind[i - 1]) { gfx_circle(cx + 24, cy + 24, 9, THEME_ACCENT); }
        int playing = vi->position >= st && (i == n - 1 || vi->position < t[i]);
        if (playing) gfx_rect(cx, cy + ch - 4, cw, 4, THEME_ACCENT);
        ui_format_time(st, at, sizeof at);
        int k = chapter_at(p, st);
        if (kind[i - 1]) snprintf(label, sizeof label, "Marked");
        else if (k >= 0 && p->probe->chapters[k].name[0]) snprintf(label, sizeof label, "%s", p->probe->chapters[k].name);
        else snprintf(label, sizeof label, "Scene %d", i);
        float lw = font_measure(label, FONT_XS | FONT_B); if (lw > cw) lw = cw;
        font_draw_fit(label, cx + (cw - lw) / 2, cy + ch + 14, FONT_XS | FONT_B, foc ? THEME_TEXT : THEME_TEXT_DIM, cw);
        font_draw_center(at, cx + cw / 2, cy + ch + 44, FONT_XS, playing ? THEME_ACCENT : THEME_TEXT_MUTED);
    }
}

static int scenes_key(Player *p, KeyAction a, const VideoInfo *vi)
{
    double t[MAX_SCENES]; int kind[MAX_SCENES];
    int n = scenes_list(p, vi->duration, t, kind, MAX_SCENES) + 1;
    switch (a) {
    case KEY_LEFT: if (p->scenes_focus > 0) p->scenes_focus--; break;
    case KEY_RIGHT: if (p->scenes_focus + 1 < n) p->scenes_focus++; break;
    case KEY_OK:
        if (p->scenes_focus == 0) { mark_moment(p, vi->position); break; }
        p->scenes_open = 0;
        LOGI("player: scene at %.0f s", t[p->scenes_focus - 1]);
        video_seek(t[p->scenes_focus - 1]);
        show_overlay(p);
        break;
    case KEY_YELLOW:   /* a marked moment goes */
        if (p->scenes_focus > 0 && kind[p->scenes_focus - 1]) {
            for (int i = 0; i < p->n_marks; i++)
                if (p->marks[i] == t[p->scenes_focus - 1]) { memmove(p->marks + i, p->marks + i + 1, sizeof p->marks[0] * (p->n_marks - i - 1)); p->n_marks--; break; }
            marks_save(p);
            app_toast("Mark removed");
        }
        break;
    case KEY_UP: break;
    default: p->scenes_open = 0; break;
    }
    app_redraw();
    return 1;
}

/* The cast for the drawer: TMDB's (photos and characters) when there is a key, else the
 * names the addon listed. */
/* This episode's guest stars (TMDB), who come first in the drawer. */
static const TmdbCast *ep_guests(const Player *p, int *n)
{
    int se = -1, ep = -1;
    const char *c = p->video_id ? strchr(p->video_id, ':') : NULL;
    *n = 0;
    if (!c || sscanf(c + 1, "%d:%d", &se, &ep) != 2) return NULL;
    return tmdb_episode_guests(p->meta.id, se, ep, n);
}

static int cast_regulars(const Player *p, const TmdbInfo **tm)
{
    *tm = tmdb_get(p->meta.id, p->meta.type);
    if (*tm && (*tm)->n_cast) return (*tm)->n_cast;
    *tm = NULL;
    return p->meta.cast.n > 12 ? 12 : p->meta.cast.n;
}

static int cast_n(const Player *p, const TmdbInfo **tm)
{
    int ng;
    ep_guests(p, &ng);
    return ng + cast_regulars(p, tm);
}

/* Who's in it, over the picture (Info, or the Cast button). 1 when there is a cast to show. */
static int open_cast(Player *p)
{
    const TmdbInfo *tm;
    int n = p->trailer ? 0 : cast_n(p, &tm);
    if (n <= 0) return 0;
    p->cast_open = 1; p->cast_focus = 0; p->overlay_until = 0; p->bar_focus = 0;
    LOGI("player: cast drawer (%d people)", n);
    app_redraw();
    return 1;
}

static void cast_person(const Player *p, const TmdbInfo *tm, int i, const char **name, const char **role, const char **photo)
{
    int ng;
    const TmdbCast *g = ep_guests(p, &ng);
    if (i < ng) {   /* a guest in this episode */
        *name = g[i].name; *role = g[i].character[0] ? g[i].character : "Guest star"; *photo = g[i].photo[0] ? g[i].photo : NULL;
        return;
    }
    i -= ng;
    if (tm) { *name = tm->cast[i].name; *role = tm->cast[i].character; *photo = tm->cast[i].photo[0] ? tm->cast[i].photo : NULL; return; }
    *name = p->meta.cast.v[i];
    *role = i < p->meta.cast_roles.n ? p->meta.cast_roles.v[i] : "";
    *photo = i < p->meta.cast_photos.n && p->meta.cast_photos.v[i][0] ? p->meta.cast_photos.v[i] : NULL;
}

static void fit_center(const char *t, float cx, float y, int fs, Color c, float max_w)
{
    float w = font_measure(t, fs);
    if (w > max_w) w = max_w;
    font_draw_fit(t, cx - w / 2, y, fs, c, max_w);
}

static void draw_cast(Player *p)
{
    const TmdbInfo *tm;
    int n = cast_n(p, &tm);
    if (!p->cast_open || !n || p->menu) return;
    if (p->cast_focus >= n) p->cast_focus = n - 1;
    const float d = 132, step = 214, h = 360;
    float x = THEME_MARGIN, y = SNTV_UI_H - h - 40, w = SNTV_UI_W - 2 * THEME_MARGIN;
    ui_panel(x, y, w, h, R_PANEL, COLOR_ALPHA(THEME_PANEL, 0xF0));
    int ng; ep_guests(p, &ng);
    const char *head = ng ? "Who's in this episode" : "Who's in this";
    font_draw(head, x + 40, y + 28, FONT_M | FONT_B, THEME_TEXT);
    float tw = font_measure(head, FONT_M | FONT_B);
    if (ng) { char gl[48]; snprintf(gl, sizeof gl, "%d guest star%s first", ng, ng == 1 ? "" : "s"); font_draw(gl, x + 40 + tw + 24, y + 36, FONT_XS, THEME_TEXT_MUTED); }
    const char *hint = "Left / Right  Browse   /   Info  Close";
    font_draw(hint, x + w - 40 - font_measure(hint, FONT_XS), y + 36, FONT_XS, THEME_TEXT_MUTED);
    int fit = (int)((w - 80) / step), first = p->cast_focus - fit + 1 > 0 ? p->cast_focus - fit + 1 : 0;
    for (int i = first; i < n && i < first + fit; i++) {
        const char *name, *role, *photo;
        cast_person(p, tm, i, &name, &role, &photo);
        int foc = i == p->cast_focus;
        float cx = x + 40 + (i - first) * step + step / 2, cy = y + 96 + d / 2, r = d / 2;
        if (foc) gfx_rrect_ring(cx - r - 6, cy - r - 6, 2 * r + 12, 2 * r + 12, r + 6, 4, THEME_FOCUS);
        ui_portrait(photo, name, cx - r, cy - r, 2 * r, 2 * r, r, FONT_L | FONT_B);
        fit_center(name, cx, cy + r + 18, FONT_XS | FONT_B, foc ? THEME_TEXT : THEME_TEXT_DIM, step - 16);
        if (role && *role) fit_center(role, cx, cy + r + 50, FONT_XS, THEME_TEXT_MUTED, step - 16);
    }
}

static void draw_catchup(Player *p)
{
    float left = p->catchup_until - g_app.time;
    if (left <= 0 || p->menu) return;
    float k = left < 0.6f ? left / 0.6f : 1;
    float x = THEME_MARGIN, y = 150, w = 820;
    int lines = font_draw_wrapped(p->catchup_text, 0, 0, FONT_S, 0, w - 64, 4);   /* measure only */
    float h = 112 + lines * font_height(FONT_S);
    gfx_set_alpha(k);
    ui_panel(x, y, w, h, R_PANEL, COLOR_ALPHA(THEME_PANEL, 0xE8));
    font_draw_fit(p->catchup_head, x + 32, y + 26, FONT_XS | FONT_B, THEME_ACCENT, w - 64);
    font_draw_wrapped(p->catchup_text, x + 32, y + 62, FONT_S, THEME_TEXT, w - 64, 4);
    font_draw("OK  Hide", x + 32, y + h - 38, FONT_XS, THEME_TEXT_MUTED);
    gfx_set_alpha(1);
    app_redraw();
}

/* every candidate has failed and the error card is up */
static int error_final(const Player *p, const VideoInfo *vi)
{
    return recovering(p, vi) == 0 && (vi->state == VIDEO_ERROR || p->tb_error[0]) && p->error_handled && !p->tb_pending && !p->tb_held;
}

/* After a failure, what the player is still doing about it: 0 nothing (the error card),
 * 1 reconnecting to the same source, 2 asking the addons for fresh links, 3 another source. */
static int recovering(const Player *p, const VideoInfo *vi)
{
    if (!((vi->state == VIDEO_ERROR || p->tb_error[0]) && p->error_handled)) return 0;
    if (p->net_retry_at > 0) return 1;
    if (next_candidate(p) >= 0) return 3;
    if (p->reask_at != 0 || p->reask_count < 2) return 2;
    return 0;
}

/* Until the first picture: the title's artwork washed into its colours, and in the middle
 * the picture format, the logo (or the name), a ring that turns, the episode and what is
 * happening; it fades into the video once that plays. */
static void veil_ring(float cx, float cy, float r, float k)
{
    Color track = COLOR_ALPHA(THEME_TEXT, (int)(0x38 * k)), arc = COLOR_ALPHA(THEME_ACCENT, (int)(255 * k));
    for (int i = 0; i < 48; i++) {   /* the faint circle */
        float a0 = i * 6.2832f / 48, a1 = (i + 1) * 6.2832f / 48;
        gfx_line(cx + cosf(a0) * r, cy + sinf(a0) * r, cx + cosf(a1) * r, cy + sinf(a1) * r, 3, track);
    }
    float head = g_app.time * 5.2f, len = 1.3f + 0.5f * sinf(g_app.time * 2.1f);   /* the arc runs round, breathing */
    for (int i = 0; i < 16; i++) {
        float a0 = head + len * i / 16, a1 = head + len * (i + 1) / 16;
        gfx_line(cx + cosf(a0) * r, cy + sinf(a0) * r, cx + cosf(a1) * r, cy + sinf(a1) * r, 4, arc);
    }
    app_redraw();
}

static void draw_veil(Player *p, const VideoInfo *vi)
{
    float k = p->veil, W = SNTV_UI_W, H = SNTV_UI_H, cx = W / 2;
    char buf[512], lbuf[512];
    const char *bg = app_art_background(&p->meta, buf, sizeof buf);
    int iw, ih;
    gfx_rect(0, 0, W, H, RGBA(0, 0, 0, (int)(255 * k)));
    unsigned bgt = bg ? image_get_ex(bg, IMAGE_MIPMAP, &iw, &ih, NULL) : 0;
    if (bgt) {   /* the backdrop as a wash of its colours, pushing in slowly */
        float u0, v0, u1, v1;
        float ia = (float)iw / ih, sa = W / H;
        u0 = 0; v0 = 0; u1 = 1; v1 = 1;
        if (ia > sa) { float c = sa / ia; u0 = (1 - c) / 2; u1 = 1 - u0; } else { float c = ia / sa; v0 = (1 - c) / 2; v1 = 1 - v0; }
        float z = 1.0f + 0.06f * (g_app.time - p->load_started < 10 ? (g_app.time - p->load_started) / 10 : 1);
        float cu = (u0 + u1) / 2, cv = (v0 + v1) / 2; u0 = cu - (cu - u0) / z; u1 = cu + (u1 - cu) / z; v0 = cv - (cv - v0) / z; v1 = cv + (v1 - cv) / z;
        gfx_tex_blur(bgt, 0, 0, W, H, u0, v0, u1, v1, RGBA(225, 225, 225, (int)(255 * k)), 4.5f);
    }
    /* a soft dark pool in the middle, for the words */
    gfx_shadow(cx - 520, H / 2 - 300, 1040, 600, 300, 260, RGBA(0, 0, 0, (int)(0xB0 * k)));
    gfx_gradient_v(0, H * 0.7f, W, H * 0.3f + 1, RGBA(0, 0, 0, 0), RGBA(0, 0, 0, (int)(0x90 * k)));
    Color fg = COLOR_ALPHA(THEME_TEXT, (int)(255 * k)), dim = COLOR_ALPHA(RGBA(0xE6, 0xE6, 0xE6, 0xFF), (int)(235 * k));

    /* the picture format, as the source says it, between two thin lines */
    const char *badges = p->cur < p->n_cands ? p->cands[p->cur].badges : "";
    const char *fmt = strstr(badges, "DOLBY VISION") ? "DOLBY VISION" : strstr(badges, "HDR10+") ? "HDR10+" : strstr(badges, "HDR") ? "HDR"
                    : strstr(badges, "4K") ? "4K" : strstr(badges, "1080") ? "1080p" : NULL;
    if (p->probe && p->probe->hdr) fmt = p->probe->hdr == 2 ? "DOLBY VISION" : "HDR";   /* the file itself, once read */
    float top = H / 2 - 230;
    if (fmt) {
        float tw = font_measure(fmt, FONT_XS | FONT_B) + 2 * (float)(strlen(fmt) - 1);   /* tracked 2 apart */
        font_draw_tracked(fmt, cx - tw / 2, top, FONT_XS | FONT_B, fg, 2);
        float ly = top + font_height(FONT_XS | FONT_B) / 2;
        gfx_rect(cx - tw / 2 - 110, ly, 80, 2, COLOR_ALPHA(THEME_TEXT, (int)(0x70 * k)));
        gfx_rect(cx + tw / 2 + 34, ly, 80, 2, COLOR_ALPHA(THEME_TEXT, (int)(0x70 * k)));
    }
    /* the logo, or the name in big type */
    const char *logo = app_art_logo(&p->meta, lbuf, sizeof lbuf);
    unsigned lt = logo ? image_get(logo, &iw, &ih) : 0;
    float ly0 = top + 56, lh = 230;
    if (lt && ih > 0) {
        float w = 760, h = w * ih / iw;
        if (h > lh) { h = lh; w = h * iw / ih; }
        gfx_tex(lt, cx - w / 2, ly0 + (lh - h) / 2, w, h, RGBA(255, 255, 255, (int)(255 * k)));
    } else {
        const char *name = p->meta.name ? p->meta.name : p->title ? p->title : "";
        float h = ui_center_wrapped(name, cx, ly0 + 40, FONT_HUGE | FONT_B, fg, 1100);
        (void)h;
    }
    float ry = ly0 + lh + 70;
    veil_ring(cx, ry, 30, k);

    /* the episode; after a while a word that it is coming; what is happening now */
    float ty = ry + 62;
    char ep[200] = "";   /* "Show  -  S2 E3  Title" (or "S2E3  Title") -> "S2E3  ·  Title" */
    const char *et = p->title ? strstr(p->title, "  -  ") : NULL;
    et = et ? et + 5 : p->title;
    if (et && et[0] == 'S' && et[1] >= '0' && et[1] <= '9') {
        char code[24]; size_t n = 0;
        const char *q = et;
        for (; *q && n < sizeof code - 1 && !(q[0] == ' ' && q[1] == ' '); q++) if (*q != ' ') code[n++] = *q;
        code[n] = 0;
        while (*q == ' ') q++;
        snprintf(ep, sizeof ep, *q ? "%s  \xC2\xB7  %s" : "%s", code, q);
    }
    if (ep[0]) { font_draw_center(ep, cx, ty, FONT_S | FONT_B, fg); ty += 46; }
    float t = g_app.time - p->load_started;
    const char *wait = t > 20 ? "Still working on it\xE2\x80\xA6" : t > 8 ? "Almost there\xE2\x80\xA6" : t > 4 ? "Just a moment more\xE2\x80\xA6" : NULL;
    if (wait) { font_draw_center(wait, cx, ty, FONT_S, dim); ty += 42; }
    const char *url = p->cur < p->n_cands ? p->cands[p->cur].url : "";
    int rec = recovering(p, vi);
    const char *what = rec == 1 ? "Reconnecting" : rec == 2 ? "Finding a fresh link" : rec == 3 ? "Trying another source"
                     : torbox_is_hash_link(url) ? "Getting it from TorBox" : torrent_engine_url_is(url) ? "Finding peers"
                     : vi->buffering || vi->state == VIDEO_PLAYING ? "Buffering" : "Opening the source";
    char line[200];
    if ((vi->buffering || vi->state == VIDEO_PLAYING) && vi->buffer_percent > 0 && vi->buffer_percent < 100) snprintf(line, sizeof line, "%s  %d%%", what, vi->buffer_percent);
    else snprintf(line, sizeof line, "%s", what);
    font_draw_center(line, cx, ty, FONT_S, dim);
}

/* What is on: title, episode, year and runtime, synopsis, cast. Shown once playback has
 * been paused for a moment, at the left, centred on the screen. */
static void draw_paused_info(Player *p, const VideoInfo *vi, float k)
{
    if (g_theme.id == THEME_APERTURE) {
        int a = (int)(255 * k);
        float x = THEME_MARGIN, y = 520;
        gfx_gradient_h(0, 300, 1300, SNTV_UI_H - 300, RGBA(0, 0, 0, (int)(0xC8 * k)), RGBA(0, 0, 0, 0));
        float lh = font_height(FONT_HUGE | FONT_ALT);
        font_draw_fit(p->meta.name ? p->meta.name : p->title, x, y - lh * 0.15f, FONT_HUGE | FONT_ALT, COLOR_ALPHA(THEME_TEXT, a), 1100);
        float ty = y + lh * 0.82f;
        const char *ep = p->title ? strstr(p->title, "  -  ") : NULL;
        font_draw(ep ? ep + 5 : "Paused", x, ty, FONT_S, COLOR_ALPHA(THEME_TEXT, a));
        if (ep) { font_draw("Paused", x, ty + 36, FONT_S, COLOR_ALPHA(THEME_TEXT_DIM, a)); ty += 36; }
        char pos[32];
        ui_format_time(vi->position, pos, sizeof pos);
        font_draw(pos, x, ty + 44, FONT_S, COLOR_ALPHA(THEME_TEXT_DIM, a));
        float by = ty + 98, bw = 220, bh = 62;
        gfx_rrect_ring(x, by, bw, bh, 4, 2.5f, COLOR_ALPHA(THEME_TEXT, a));
        gfx_rect(x + 8, by + bh + 6, bw - 16, 3, COLOR_ALPHA(THEME_ACCENT, a));
        ui_icon(ICON_PLAY, x + 44, by + bh / 2, 24, COLOR_ALPHA(THEME_TEXT, a));
        font_draw("Resume", x + 72, by + (bh - font_height(FONT_S | FONT_B)) / 2, FONT_S | FONT_B, COLOR_ALPHA(THEME_TEXT, a));
        gfx_rrect_ring(x + bw + 24, by, 220, bh, 4, 1.5f, COLOR_ALPHA(THEME_TEXT, a / 3));
        ui_icon(ICON_INFO, x + bw + 24 + 40, by + bh / 2, 24, COLOR_ALPHA(THEME_TEXT_DIM, a));
        font_draw("Info  Cast", x + bw + 24 + 68, by + (bh - font_height(FONT_S)) / 2, FONT_S, COLOR_ALPHA(THEME_TEXT_DIM, a));
        return;
    }
    int a = (int)(255 * k);
    float x = THEME_MARGIN, maxw = 1000;
    char buf[512];
    const char *logo = app_art_logo(&p->meta, buf, sizeof buf);
    int lw = 0, lh = 0;
    unsigned ltex = logo ? image_get(logo, &lw, &lh) : 0;
    float logo_w = 520, logo_h = ltex && lh > 0 ? logo_w * lh / lw : 0;
    if (logo_h > 150) { logo_h = 150; logo_w = logo_h * lw / lh; }
    int title_fs = font_measure(p->meta.name, FONT_XL | FONT_B) > maxw ? (FONT_L | FONT_B) : (FONT_XL | FONT_B);
    const char *ep = strstr(p->title, "  -  ");
    const TmdbInfo *tm = tmdb_get(p->meta.id, p->meta.type);
    int n_cast = tm && tm->n_cast ? tm->n_cast : p->meta.cast.n;
    int has_meta = (p->meta.imdb_rating && *p->meta.imdb_rating) || (tm && tm->cert[0]) || p->meta.release_info || p->meta.runtime;
    const char *about = p->meta.description;
    int nv;
    const MetaVideo *vids = app_videos(p->meta.id, &nv);
    for (int i = 0; i < nv && p->video_id; i++)
        if (!strcmp(vids[i].id, p->video_id) && vids[i].overview && *vids[i].overview) { about = vids[i].overview; break; }   /* this episode, not the show */
    const char *next = p->next_video_id && p->next_title && !p->trailer ? p->next_title : NULL;
    /* what someone on Trakt said about it, one at a time, most liked first */
    int cse = -1, cep = -1, ncm = 0;
    { const char *c = p->video_id ? strchr(p->video_id, ':') : NULL; if (c) sscanf(c + 1, "%d:%d", &cse, &cep); }
    const TraktComment *cm = NULL;
    if (!p->trailer) trakt_comments(p->meta.id, p->meta.type, cse, cep, &cm, &ncm);
    float per = 9, cyc = fmodf(g_app.time, per);
    const TraktComment *say = ncm ? &cm[(int)(g_app.time / per) % ncm] : NULL;
    char quote[320] = "";
    if (say) snprintf(quote, sizeof quote, "\"%.300s\"", say->text);
    float say_k = cyc < 0.5f ? cyc / 0.5f : cyc > per - 0.5f ? (per - cyc) / 0.5f : 1;
    int say_lines = 0;   /* room for the longest of them, so the block holds still as they change */
    for (int i = 0; i < ncm; i++) { char qb[320]; snprintf(qb, sizeof qb, "\"%.300s\"", cm[i].text); int l = font_draw_wrapped(qb, x + 22, 0, FONT_S, 0, maxw - 22, 3); if (l > say_lines) say_lines = l; }
    if (say) app_redraw();
    /* measured first, so the block ends just above the paused line whatever it holds */
    float need = 44 + (logo_h > 0 ? logo_h + 22 : font_draw_wrapped(p->meta.name, x, 0, title_fs, 0, maxw, 2) * font_height(title_fs) + 14)
               + (ep ? font_height(FONT_M | FONT_B) + 12 : 0) + (has_meta ? 50 : 0)
               + (about ? font_draw_wrapped(about, x, 0, FONT_S, 0, maxw, 4) * font_height(FONT_S) + 26 : 0)
               + (say ? say_lines * font_height(FONT_S) + 40 + 26 : 0)
               + (n_cast ? 36 + 64 : 0) + (next ? 58 : 0);
    float y = (SNTV_UI_H - need) / 2 - 20;   /* centred, but never down on the paused line */
    if (y > SNTV_UI_H - 230 - need) y = SNTV_UI_H - 230 - need;
    if (y < 110) y = 110;
    float block_mid = y + need / 2, right_edge = SNTV_UI_W - 600;
    y += (1 - k) * 16;
    gfx_gradient_h(0, 0, 1300, SNTV_UI_H, RGBA(0, 0, 0, (int)(0xB8 * k)), RGBA(0, 0, 0, 0));
    gfx_gradient_v(0, y - 120, SNTV_UI_W, SNTV_UI_H - y + 120, RGBA(0, 0, 0, 0), RGBA(0, 0, 0, (int)(0x60 * k)));
    font_draw("YOU'RE WATCHING", x, y, FONT_XS | FONT_B, COLOR_ALPHA(THEME_TEXT_MUTED, a));
    y += 44;
    if (logo_h > 0) {
        float w = logo_w, hh = logo_h;
        gfx_tex(ltex, x, y, w, hh, RGBA(255, 255, 255, a));
        y += hh + 22;
    } else y += font_draw_wrapped(p->meta.name, x, y, title_fs, COLOR_ALPHA(THEME_TEXT, a), maxw, 2) * font_height(title_fs) + 14;
    if (ep) { font_draw_fit(ep + 5, x, y, FONT_M | FONT_B, COLOR_ALPHA(THEME_TEXT, a), maxw); y += font_height(FONT_M | FONT_B) + 12; }
    float mx = x;
    if (p->meta.imdb_rating && *p->meta.imdb_rating) mx += ui_imdb(p->meta.imdb_rating, mx, y) + 6;
    if (tm && tm->cert[0]) mx += ui_chip_outline(tm->cert, mx, y, COLOR_ALPHA(THEME_TEXT, (int)(0x70 * k)), COLOR_ALPHA(THEME_TEXT, a)) + 8;
    const char *parts[3]; int np = 0;
    char genres[96] = "";
    for (int i = 0; i < p->meta.genres.n && i < 2; i++)
        snprintf(genres + strlen(genres), sizeof genres - strlen(genres), "%s%s", i ? " / " : "", p->meta.genres.v[i]);
    if (p->meta.release_info) parts[np++] = p->meta.release_info;
    if (p->meta.runtime) parts[np++] = p->meta.runtime;
    if (genres[0]) parts[np++] = genres;
    for (int i = 0; i < np; i++) {
        if (mx > x) mx = ui_dot_sep(mx, y, COLOR_ALPHA(THEME_TEXT_MUTED, a));
        font_draw(parts[i], mx, y + (36 - font_height(FONT_S | FONT_B)) / 2, FONT_S | FONT_B, COLOR_ALPHA(THEME_TEXT_DIM, a));
        mx += font_measure(parts[i], FONT_S | FONT_B);
    }
    /* what is on screen: the picture's size, and HDR when the source is */
    const char *res = vi->height >= 2000 ? "4K" : vi->height >= 1000 ? "1080p" : vi->height >= 700 ? "720p" : NULL;
    const char *badges = p->n_cands ? p->cands[p->cur].badges : "";
    const char *hdr = strstr(badges, "DOLBY") || strstr(badges, "DV") ? "Dolby Vision" : strstr(badges, "HDR") ? "HDR" : NULL;
    for (int i = 0; i < 2; i++) {
        const char *q = i ? hdr : res;
        if (!q) continue;
        mx += 14;
        mx += ui_chip_outline(q, mx, y, COLOR_ALPHA(THEME_TEXT, (int)(0x70 * k)), COLOR_ALPHA(THEME_TEXT, a)) - 6;
    }
    if (np || mx > x) y += 50;
    if (about) y += font_draw_wrapped(about, x, y, FONT_S, COLOR_ALPHA(THEME_TEXT_DIM, a), maxw, 4) * font_height(FONT_S) + 26;
    if (say) {   /* a line from Trakt, fading between them */
        int ca = (int)(a * say_k);
        gfx_rect(x, y + 4, 4, 2 * font_height(FONT_S) - 8, COLOR_ALPHA(THEME_ACCENT, ca));
        int lines = font_draw_wrapped(quote, x + 22, y, FONT_S, COLOR_ALPHA(THEME_TEXT, ca), maxw - 22, 3);
        char by[96];
        snprintf(by, sizeof by, "%s on Trakt%s", say->user[0] ? say->user : "Someone", say->likes > 0 ? "" : "");
        if (say->likes > 0) snprintf(by + strlen(by), sizeof by - strlen(by), "  \xC2\xB7  %d like%s", say->likes, say->likes == 1 ? "" : "s");
        font_draw(by, x + 22, y + lines * font_height(FONT_S) + 6, FONT_XS, COLOR_ALPHA(THEME_TEXT_MUTED, ca));
        y += say_lines * font_height(FONT_S) + 40 + 26;
    }
    /* the right-hand corner: what critics and audiences make of it, then Trakt's viewers */
    {
        /* critics first, then audiences closing on MDBList's blend of them all */
        static const struct { int src, kind; const char *fmt, *label, *log; double scale; } R[] = {
            { MDB_TOMATOMETER, BADGE_TOMATOMETER, "%.0f%%", "Tomatometer", "Tomatometer", 1 },
            { MDB_POPCORNMETER, BADGE_POPCORNMETER, "%.0f%%", "Popcornmeter", "Popcornmeter", 1 },
            { MDB_METACRITIC, BADGE_METASCORE, "%.0f", "Metascore", "Metacritic", 1 },
            { MDB_ROGEREBERT, BADGE_EBERT, "%.1f", "Roger Ebert", "Roger Ebert", 1 },
            { MDB_LETTERBOXD, BADGE_LETTERBOXD, "%.1f", "Letterboxd", "Letterboxd", 1 },
            { MDB_TRAKT, BADGE_TRAKT, "%.0f%%", "Trakt", "Trakt", 1 },
            { MDB_TMDB, BADGE_TMDB, "%.0f%%", "TMDB", "TMDB", 1 },
            { MDB_SCORE, BADGE_MDBLIST, "%.1f", "MDBList", "MDBList", 0.1 } };
        ScoreBadge rb[2][4], tb[3];
        char text[11][16], all[500];
        int nr[2] = { 0, 0 }, nt = 0, l = 0, used = 0;
        double sc[MDB_SOURCES];
        all[0] = 0;
        if (mdblist_scores(p->meta.id, p->meta.type, sc))
            for (int i = 0; i < 8; i++) {
                if (sc[R[i].src] <= 0) continue;
                int row = i < 4 ? 0 : 1;
                snprintf(text[used], 16, R[i].fmt, sc[R[i].src] * R[i].scale);
                rb[row][nr[row]++] = (ScoreBadge){ R[i].kind, sc[R[i].src], text[used], R[i].label };
                l += snprintf(all + l, sizeof all - l, "%s%s %s", l ? ", " : "", R[i].log, text[used]);
                used++;
            }
        long tv[3] = { 0, 0, 0 };
        if (trakt_stats(p->meta.id, p->meta.type, &tv[0], &tv[1], &tv[2])) {
            static const struct { int kind; const char *label, *log; } T[3] = {
                { BADGE_LIVE, "Watching now", "Watching now" }, { BADGE_WATCHERS, "Watched it", "Trakt watchers" }, { BADGE_PLAYS, "Plays", "Trakt plays" } };
            for (int i = 0; i < 3; i++) {
                if (tv[i] <= 0) continue;
                tracking_count_text(tv[i], text[used], 16);
                tb[nt++] = (ScoreBadge){ T[i].kind, (double)tv[i], text[used], T[i].label };
                l += snprintf(all + l, sizeof all - l, "%s%s %s", l ? ", " : "", T[i].log, text[used]);
                used++;
            }
        }
        ScoreBadge cardrows[8];
        int nc = 0;
        for (int r = 0; r < 2; r++) for (int i = 0; i < nr[r]; i++) cardrows[nc++] = rb[r][i];
        if (ui_score_card(SNTV_UI_W - THEME_MARGIN, block_mid, cardrows, nc, tb, nt, k) > 0)   /* level with the details on the left */
            right_edge = SNTV_UI_W - THEME_MARGIN - SCORE_CARD_W - 40;   /* the cast stops short of it */
        if (used > p->corner_logged) { LOGI("player: paused corner: %s", all); p->corner_logged = used; }   /* once more is known */
    }
    if (n_cast && y < SNTV_UI_H - 300) {
        font_draw("CAST", x, y, FONT_XS | FONT_B, COLOR_ALPHA(THEME_TEXT_MUTED, a));
        y += 36;
        float cx = x;
        int shown = 0;
        char more[48];
        snprintf(more, sizeof more, "+%d more  \xC2\xB7  INFO or Cast for all", n_cast);   /* widest it can be, room kept for it */
        float more_w = font_measure(more, FONT_XS | FONT_B) + 24;
        for (int i = 0; i < n_cast && i < 6; i++) {
            const char *name = tm && tm->n_cast ? tm->cast[i].name : p->meta.cast.v[i];
            const char *role = tm && tm->n_cast ? tm->cast[i].character : (i < p->meta.cast_roles.n ? p->meta.cast_roles.v[i] : "");
            const char *photo = tm && tm->n_cast && tm->cast[i].photo[0] ? tm->cast[i].photo : (i < p->meta.cast_photos.n && p->meta.cast_photos.v[i][0] ? p->meta.cast_photos.v[i] : NULL);
            float w = (font_measure(name, FONT_XS | FONT_B) > font_measure(role, FONT_XS) ? font_measure(name, FONT_XS | FONT_B) : font_measure(role, FONT_XS)) + (photo ? 90 : 74);
            if (cx + w > right_edge - (i + 1 < n_cast ? more_w : 0)) break;
            shown++;
            float hh = role[0] ? 64 : 48;
            gfx_rrect(cx, y, w, hh, hh / 2, COLOR_ALPHA(THEME_TEXT, (int)(0x1C * k)));
            int pw = 0, ph = 0;
            unsigned ptex = photo ? image_get(photo, &pw, &ph) : 0;
            float r = hh / 2 - 6;
            if (ptex && pw > 0 && ph > 0) {   /* a round photo, cropped to a square, faces high as elsewhere */
                float ia = (float)pw / ph, u0 = 0, v0 = 0, u1 = 1, v1 = 1;
                if (ia > 1) { u0 = (1 - 1 / ia) / 2; u1 = 1 - u0; } else { v0 = (1 - ia) / 3; v1 = v0 + ia; }
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
        if (shown < n_cast) {   /* the rest are in the cast drawer */
            snprintf(more, sizeof more, "+%d more  \xC2\xB7  INFO or Cast for all", n_cast - shown);
            font_draw(more, cx + 12, y + (64 - font_height(FONT_XS | FONT_B)) / 2, FONT_XS | FONT_B, COLOR_ALPHA(THEME_TEXT_DIM, a));
        }
        y += 64;
    }
    if (next) {
        y += 22;
        font_draw("UP NEXT", x, y + 4, FONT_XS | FONT_B, COLOR_ALPHA(THEME_TEXT_MUTED, a));
        font_draw_fit(next, x + font_measure("UP NEXT", FONT_XS | FONT_B) + 18, y, FONT_S | FONT_B, COLOR_ALPHA(THEME_TEXT, a), maxw - 140);
    }
}

static void draw(Screen *s)
{
    Player *p = s->st;
    VideoInfo vi;
    video_info(&vi);
    if (SNTV_PHONE && !p->pip) { gfx_rect(0, 0, SNTV_UI_W, SNTV_UI_H, RGBA(0, 0, 0, 255)); app_redraw(); return; }   /* a phone still turning sideways */
    if (gfx_handheld() && !p->pip) {   /* a phone sideways: its own touch controls over the picture */
        draw_video_area(p, 0, 0, SNTV_UI_W, SNTV_UI_H);
        draw_subtitle(p, &vi);
        if (!video_in_pip()) full_draw(p, &vi);   /* in picture in picture: the picture alone */
        return;
    }
    ui_hit(0, 0, SNTV_UI_W, SNTV_UI_H, 0x7FFFFF00);   /* a tap anywhere is OK, as before */
    draw_video_area(p, 0, 0, SNTV_UI_W, SNTV_UI_H);
    if (p->veil > 0 && !p->pip) { draw_veil(p, &vi); if (p->veil > 0.5f && !p->menu) return; }
    draw_subtitle(p, &vi);

    int overlay = g_app.time < p->overlay_until || vi.state == VIDEO_PAUSED || vi.state == VIDEO_LOADING;
    float adv_t = p->adv_at ? g_app.time - p->adv_at : -1;
    if (adv_t >= 0 && adv_t < ADVISORY_S && !p->menu && !overlay && !p->pip) {   /* fades in, stays, fades out */
        float k = adv_t < 0.5f ? adv_t / 0.5f : adv_t > ADVISORY_S - 0.8f ? (ADVISORY_S - adv_t) / 0.8f : 1;
        const Advisory *adv = advisory_get(p->meta.id);
        if (adv) ui_advisory(SNTV_UI_W - THEME_MARGIN, 120, adv->level, k);   /* the top-right corner, under the clock */
        app_redraw();
    }
    if ((p->scenes_open || p->cast_open) && !p->menu) overlay = 0;   /* the rail or the cast has the bottom of the screen */
    if (subs_tuning(p)) overlay = 0;   /* the subtitles being changed have the screen */
    if (error_final(p, &vi)) {
        if (!p->card_logged) { p->card_logged = 1; p->menu = 0; perf_start_done(0); LOGI("player: error card (%s): every source and fresh link tried", p->tb_error[0] ? p->tb_error : vi.error); }
        gfx_rect(0, 0, SNTV_UI_W, SNTV_UI_H, COLOR_ALPHA(THEME_BG, 0xD8));
        float w = 900, h = 360, x = (SNTV_UI_W - w) / 2, y = (SNTV_UI_H - h) / 2;
        ui_panel(x, y, w, h, R_PANEL, THEME_PANEL);
        gfx_circle(x + w / 2, y + 70, 36, COLOR_ALPHA(THEME_DANGER, 0x40));
        gfx_line(x + w / 2 - 12, y + 58, x + w / 2 + 12, y + 82, 6, THEME_DANGER);
        gfx_line(x + w / 2 + 12, y + 58, x + w / 2 - 12, y + 82, 6, THEME_DANGER);
        font_draw_center("This source can't be played", x + w / 2, y + 128, FONT_L | FONT_B, THEME_TEXT);
        font_draw_wrapped(p->tb_error[0] ? p->tb_error : vi.error, x + 60, y + 190, FONT_S, THEME_TEXT_DIM, w - 120, 2);
        font_draw_center(p->n_cands > 1 ? "Every candidate failed. Press Back to pick another source." : "Press Back to pick another source.",
                         x + w / 2, y + h - 86, FONT_S | FONT_B, THEME_TEXT_MUTED);
        font_draw_center("Press OK to see what happened (Diagnostics)", x + w / 2, y + h - 46, FONT_XS, THEME_TEXT_MUTED);
        return;
    }

    int rec = recovering(p, &vi);
    if (rec) {   /* not an error yet: what is being done about it */
        static const char *const WHAT[] = { "", "Reconnecting\xE2\x80\xA6", "Finding a fresh link\xE2\x80\xA6", "Trying another source\xE2\x80\xA6" };
        ui_spinner(SNTV_UI_W / 2, SNTV_UI_H / 2, 34);
        font_draw_shadow(WHAT[rec], SNTV_UI_W / 2 - font_measure(WHAT[rec], FONT_S | FONT_B) / 2, SNTV_UI_H / 2 + 60, FONT_S | FONT_B, THEME_TEXT, RGBA(0, 0, 0, 0xA0));
        app_redraw();
    } else if (vi.state == VIDEO_LOADING || vi.buffering) {
        ui_spinner(SNTV_UI_W / 2, SNTV_UI_H / 2, 34);
        const char *what = vi.state == VIDEO_LOADING ? "Starting" : "Buffering";
        char b[48];
        if (vi.buffer_percent > 0 && vi.buffer_percent < 100) snprintf(b, sizeof b, "%s  %d%%", what, vi.buffer_percent);
        else snprintf(b, sizeof b, "%s", what);
        font_draw_shadow(b, SNTV_UI_W / 2 - font_measure(b, FONT_S | FONT_B) / 2, SNTV_UI_H / 2 + 60, FONT_S | FONT_B, THEME_TEXT, RGBA(0, 0, 0, 0xA0));
    }

    draw_catchup(p);
    draw_cast(p);
    draw_scenes(p, &vi);

    /* centre play/pause flash */
    float flash = p->play_flash - g_app.time;
    if ((vi.state == VIDEO_PAUSED && g_theme.id != THEME_APERTURE) || flash > 0) {
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
    int ap = g_theme.id == THEME_APERTURE;
    if (ap) { bx = THEME_MARGIN + 130; bw = SNTV_UI_W - 2 * THEME_MARGIN - 130 - 230; by = SNTV_UI_H - 170; }   /* "32:18 ──●──── 1:15:42 left" on one line */
    if (overlay) {
        gfx_gradient_v(0, SNTV_UI_H - 420, SNTV_UI_W, 420, RGBA(0, 0, 0, 0), RGBA(0, 0, 0, 0xE0));
        gfx_gradient_v(0, 0, SNTV_UI_W, 220, RGBA(0, 0, 0, 0xB0), RGBA(0, 0, 0, 0));
        int info = vi.state == VIDEO_PAUSED && p->paused_since > 0 && g_app.time - p->paused_since > 0.4f && !p->menu;
        float info_k = info ? (g_app.time - p->paused_since - 0.4f) / 0.5f : 0;
        if (info_k > 1) info_k = 1;
        if (!info && g_theme.id == THEME_APERTURE) {
            ui_spaced_caps(p->meta.name ? p->meta.name : p->title, THEME_MARGIN, 52, FONT_L | FONT_ALT, 6, THEME_TEXT, SNTV_UI_W - 2 * THEME_MARGIN - 420);
            const char *ep = p->title ? strstr(p->title, "  -  ") : NULL;
            if (ep) font_draw_fit(ep + 5, THEME_MARGIN, 108, FONT_S, THEME_TEXT_DIM, SNTV_UI_W - 2 * THEME_MARGIN - 420);
        } else if (!info) {
            font_draw_fit(p->title, THEME_MARGIN, 56, FONT_L | FONT_B, THEME_TEXT, SNTV_UI_W - 2 * THEME_MARGIN - 420);
            float cx = THEME_MARGIN;
            if (p->cands[p->cur].label) { font_draw_fit(p->cands[p->cur].label, cx, 114, FONT_S, THEME_TEXT_DIM, 700); cx += font_measure(p->cands[p->cur].label, FONT_S) + 20; }
            if (p->p2p_status[0]) { font_draw(p->p2p_status, cx, 114, FONT_S | FONT_B, THEME_ACCENT2); cx += font_measure(p->p2p_status, FONT_S | FONT_B) + 20; }
            char res[32], aud[48];
            media_badges(p, &vi, res, sizeof res, aud, sizeof aud);
            if (res[0]) cx += ui_chip(res, cx, 110, COLOR_ALPHA(THEME_TEXT, 0x22), THEME_TEXT) + 10;
            if (aud[0]) cx += ui_chip(aud, cx, 110, COLOR_ALPHA(THEME_TEXT, 0x22), THEME_TEXT) + 10;
            if (!res[0] && !aud[0] && vi.codec[0] && cx < 1200) cx += ui_chip(vi.codec, cx, 110, COLOR_ALPHA(THEME_TEXT, 0x22), THEME_TEXT) + 10;
            draw_health(p, &vi, cx);
        } else draw_paused_info(p, &vi, info_k);
        float chip_y = 62;
        if (g_app.clock_mode != 2) {   /* "2:52 PM  |  ends at 3:40" */
            char now[24], end[24], text[80];
            ui_clock_text(0, now, sizeof now);
            if (vi.duration > 0 && vi.state != VIDEO_PAUSED) { ui_clock_text((vi.duration - shown_pos) / RATES[p->rate_idx], end, sizeof end); snprintf(text, sizeof text, "%s   |   ends at %s", now, end); }
            else snprintf(text, sizeof text, "%s", now);
            float tw = font_measure(text, FONT_S | FONT_B) + 44, th2 = 52, tx = SNTV_UI_W - THEME_MARGIN - tw;
            if (!ap) gfx_rrect(tx, 47, tw, th2, th2 / 2, COLOR_ALPHA(THEME_BG, 0x8C));
            font_draw(text, tx + 22, 47 + (th2 - font_height(FONT_S | FONT_B)) / 2, FONT_S | FONT_B, THEME_TEXT);
            chip_y = 112;
        }
        if (p->n_cands > 1) {   /* every theme: which of the sources this is */
            char c[48]; snprintf(c, sizeof c, "Source %d of %d", p->cur + 1, p->n_cands);
            ui_chip(c, SNTV_UI_W - THEME_MARGIN - font_measure(c, FONT_XS | FONT_B) - 24, chip_y, COLOR_ALPHA(THEME_TEXT, 0x22), THEME_TEXT);
        }
    } else if (bar_only) {
        gfx_gradient_v(0, SNTV_UI_H - 240, SNTV_UI_W, 240, RGBA(0, 0, 0, 0), RGBA(0, 0, 0, 0xB0));
    }
    if (p->rest_t > 0.001f) {   /* lean-back: what is paused and how much is left, where the bar was */
        char pos[32], rem[32], line[128];
        ui_format_time(shown_pos, pos, sizeof pos);
        ui_format_time(vi.duration - shown_pos, rem, sizeof rem);
        if (vi.duration > 0) {   /* and when it would end if played on now */
            time_t end = time(NULL) + (time_t)(vi.duration - shown_pos);
            struct tm et; localtime_r(&end, &et);
            char at[16];
            if (g_app.clock_mode == 1) snprintf(at, sizeof at, "%02d:%02d", et.tm_hour, et.tm_min);
            else snprintf(at, sizeof at, "%d:%02d %s", et.tm_hour % 12 ? et.tm_hour % 12 : 12, et.tm_min, et.tm_hour < 12 ? "AM" : "PM");
            snprintf(line, sizeof line, "%s  \xC2\xB7  %s left  \xC2\xB7  ends at %s", pos, rem, at);
        } else snprintf(line, sizeof line, "%s", pos);
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
        float th = ap ? 4 : p->seek_commit > 0 ? 12 : 8, ty = by - th / 2;
        gfx_rrect(bx, ty, bw, th, th / 2, COLOR_ALPHA(THEME_TEXT, 0x30));
        if (vi.duration > 0) {
            if (vi.buffer_percent > 0) gfx_rrect(bx, ty, bw * vi.buffer_percent / 100.0f, th, th / 2, COLOR_ALPHA(THEME_TEXT, 0x30));
            for (int i = 0; i < p->n_skip; i++) {   /* intro / credits ranges */
                double e = p->skip[i].end > 0 ? p->skip[i].end : vi.duration;
                if (p->skip[i].kind == SKIP_PREVIEW || e <= p->skip[i].start) continue;
                gfx_rect(bx + bw * (float)(p->skip[i].start / vi.duration), ty + th + 6, bw * (float)((e - p->skip[i].start) / vi.duration), 3,
                         p->skip[i].kind == SKIP_CREDITS ? COLOR_ALPHA(THEME_TEXT, 0x50) : COLOR_ALPHA(THEME_ACCENT2, 0xC0));
            }
            for (int i = 0; i < p->n_marks; i++)
                gfx_circle(bx + bw * (float)(p->marks[i] / vi.duration), ty - 12, 5, THEME_ACCENT);
            if (p->probe && ap)   /* chapters as dots on the line, the current one in coral */
                for (int i = 0; i < p->probe->n_chapters; i++) {
                    double cs = p->probe->chapters[i].start_s;
                    gfx_circle(bx + bw * (float)(cs / vi.duration), by, 5, chapter_at(p, shown_pos) == i ? THEME_ACCENT : COLOR_ALPHA(THEME_TEXT, 0xB0));
                }
            else if (p->probe)   /* chapter marks */
                for (int i = 1; i < p->probe->n_chapters; i++) {
                    float cxp = bx + bw * (float)(p->probe->chapters[i].start_s / vi.duration);
                    gfx_rect(cxp - 1.5f, ty - 6, 3, th + 12, COLOR_ALPHA(THEME_TEXT, 0x70));
                }
            float fw = bw * (float)(shown_pos / vi.duration);
            if (fw > th) gfx_rrect_grad_h(bx, ty, fw, th, th / 2, THEME_ACCENT, ap ? THEME_ACCENT : THEME_ACCENT2);
            float kx = bx + fw;
            if (!g_app.fx_light && g_theme.id != THEME_APERTURE) gfx_shadow(kx - 14, by - 14, 28, 28, R_PANEL, 14, COLOR_ALPHA(THEME_ACCENT2, 0xB0));
            gfx_circle(kx, by, ap ? 9 : p->seek_commit > 0 ? 16 : 12, THEME_TEXT);
            if (p->seek_commit > 0 && g_theme.id == THEME_APERTURE) {
                draw_aperture_filmstrip(p, &vi, shown_pos, by);
            } else if (p->seek_commit > 0) {
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
        float rw = font_measure(rem, FONT_S | FONT_B);
        if (ap) {
            char left[56]; snprintf(left, sizeof left, "%s left", vi.duration > 0 ? t2 : "--:--");
            float ty2 = by - font_height(FONT_S) / 2;
            font_draw(pos, THEME_MARGIN, ty2, FONT_S, THEME_TEXT);
            font_draw(left, SNTV_UI_W - THEME_MARGIN - font_measure(left, FONT_S), ty2, FONT_S, THEME_TEXT);
        } else {
            font_draw(pos, bx, by + 24, FONT_S | FONT_B, THEME_TEXT);
            font_draw(rem, bx + bw - rw, by + 24, FONT_S | FONT_B, THEME_TEXT_DIM);
        }
        if (p->rate_idx != RATE_NORMAL && overlay) {
            char sp[16]; snprintf(sp, sizeof sp, "%gx", RATES[p->rate_idx]);
            float cw2 = font_measure(sp, FONT_XS | FONT_B) + 24;
            ui_chip(sp, ap ? bx + bw - cw2 : bx + bw - rw - 28 - cw2, ap ? by - 60 : by + 20, COLOR_ALPHA(THEME_ACCENT2, 0xC0), THEME_TEXT);
        }
    }
    if (overlay && !(ap && vi.state == VIDEO_PAUSED && !p->bar_focus && p->paused_since > 0 && g_app.time - p->paused_since > 0.4f)) {   /* Aperture's pause card has the bottom alone */
        draw_bar(p, bx, by + (ap ? 46 : 66));
    }
    gfx_set_alpha(1);
    if (p->stats && !p->menu) draw_stats(p, &vi);
    float card_top = 0;
    if (p->still_asking) {
        float w = 760, h = 230, x = (SNTV_UI_W - w) / 2, y = (SNTV_UI_H - h) / 2;
        if (!g_app.fx_light) gfx_shadow(x, y, w, h, R_PANEL, 40, RGBA(0, 0, 0, 0xA0));
        ui_panel(x, y, w, h, R_PANEL, COLOR_ALPHA(THEME_PANEL, 0xF4));
        font_draw_center("Still watching?", x + w / 2, y + 40, FONT_L | FONT_B, THEME_TEXT);
        char line[200]; snprintf(line, sizeof line, "Up next: %s", p->next_title ? p->next_title : "the next episode");
        font_draw_center(line, x + w / 2, y + 100, FONT_S, THEME_TEXT_DIM);
        font_draw_center("OK  plays it      \xC2\xB7      Back  stops here", x + w / 2, y + 160, FONT_S | FONT_B, THEME_TEXT);
    }
    if (p->up_next_shown && p->next_video_id && !p->menu && !p->still_asking) { draw_up_next(p, &vi, overlay); card_top = (overlay ? SNTV_UI_H - 400 : SNTV_UI_H - 240) - (g_theme.id == THEME_APERTURE ? 24 : 0); }   /* Aperture's card is taller */
    else if (p->lighter_to >= 0 && !p->menu) { draw_lighter(p, overlay); card_top = overlay ? SNTV_UI_H - 400 : SNTV_UI_H - 200; }
    const SkipSeg *sk = g_app.skip_mode == 0 && !p->menu && !p->cast_open && !p->scenes_open ? skippable(p, &vi) : NULL;
    if (sk) draw_skip_button(p, sk, &vi, overlay, card_top);
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

/* Where an addon sits in the user's list (the end when it isn't there) */
static int addon_rank(const char *name)
{
    for (int i = 0; i < g_app.n_addons; i++) if (g_app.addons[i].manifest.name && !strcmp(g_app.addons[i].manifest.name, name)) return i;
    return g_app.n_addons;
}

/* The addons with sources here, in the user's addon order (the Sources panel's chips) */
static int source_addons(const Player *p, char names[SRC_ADDONS][80])
{
    int view[64], n = source_view(p, view), k = 0;
    for (int i = 0; i < n; i++) {
        char ad[80];
        cand_addon(&p->cands[view[i]], ad, sizeof ad);
        if (!ad[0] || (k && !strcmp(names[k - 1], ad))) continue;
        int seen = 0;
        for (int j = 0; j < k; j++) seen |= !strcmp(names[j], ad);
        if (!seen && k < SRC_ADDONS) snprintf(names[k++], 80, "%s", ad);
    }
    for (int i = 1; i < k; i++)
        for (int j = i; j > 0 && addon_rank(names[j]) < addon_rank(names[j - 1]); j--) {
            char t[80]; memcpy(t, names[j], 80); memcpy(names[j], names[j - 1], 80); memcpy(names[j - 1], t, 80);
        }
    return k;
}

static int cand_res(const PlayCandidate *c) { return c->res ? c->res : strstr(c->badges, "4K") ? 2160 : 0; }
static int cand_hdr(const PlayCandidate *c) { return strstr(c->badges, "HDR") || strstr(c->badges, "DOLBY") ? 1 : 0; }
/* The panel's groups: 4K, 1080p, 720p, the rest; HDR before SDR in each */
static int cand_group(const PlayCandidate *c)
{
    int r = cand_res(c), bucket = r >= 2160 ? 0 : r >= 1080 ? 1 : r >= 720 ? 2 : 3;
    return bucket * 2 + !cand_hdr(c);
}

/* The panel's list under its addon filter, in the order it is drawn: the source
 * recommended for this TV first, then by group, the largest file first in each */
static int source_rows(const Player *p, int *rows)
{
    int view[64], n = source_view(p, view), k = 0, best = -1;
    char names[SRC_ADDONS][80];
    int na = source_addons(p, names);
    const char *only = p->src_filter > 0 && p->src_filter <= na ? names[p->src_filter - 1] : NULL;
    for (int i = 0; i < n; i++) {
        char ad[80];
        cand_addon(&p->cands[view[i]], ad, sizeof ad);
        if (only && strcmp(ad, only)) continue;
        if (best < 0 && p->cands[view[i]].recommended) { best = view[i]; continue; }
        rows[k++] = view[i];
    }
    for (int i = 1; i < k; i++)
        for (int j = i; j > 0; j--) {
            const PlayCandidate *a = &p->cands[rows[j]], *b = &p->cands[rows[j - 1]];
            int ga = cand_group(a), gb = cand_group(b);
            if (ga > gb || (ga == gb && a->video_size <= b->video_size)) break;
            int t = rows[j]; rows[j] = rows[j - 1]; rows[j - 1] = t;
        }
    if (best >= 0) { memmove(rows + 1, rows, sizeof *rows * (size_t)k); rows[0] = best; k++; }
    return k;
}

/* Size, style, position and timing reach the subtitles the TV draws from the file too
 * (the app draws addon subtitles itself, straight from g_app). */
static void push_sub_style(const Player *p)
{
    video_subtitle_style(sub_size(p), g_app.subs_style, ui_subs_colour_tv(g_app.subs_colour), g_app.subs_pos, (int)(p->embedded_offset * 1000));
}

static void adjust_subs(Player *p, int row, int dir)
{
    if (row == SUB_SIZE) g_app.subs_size = (g_app.subs_size + dir + 4) % 4;
    else if (row == SUB_STYLE) g_app.subs_style = (g_app.subs_style + dir + SUBS_STYLE_COUNT) % SUBS_STYLE_COUNT;
    else if (row == SUB_POS) g_app.subs_pos = (g_app.subs_pos + dir + 3) % 3;
    app_save_prefs();
    push_sub_style(p);
}

/* A hand-made timing fix, remembered per subtitle file and put back next time. */
static void sub_fix_key(const char *url, char *out, size_t n)
{
    unsigned long long h = 1469598103934665603ULL;
    for (const char *c = url; *c; c++) { h ^= (unsigned char)*c; h *= 1099511628211ULL; }
    snprintf(out, n, "%016llx", h);
}

static double sub_fix_get(const char *url)
{
    char k[24]; sub_fix_key(url, k, sizeof k);
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(store_get_json("sub_fixes"), k);
    return cJSON_IsNumber(v) ? v->valuedouble : 0;
}

static void sub_fix_set(const char *url, double fix)
{
    char k[24]; sub_fix_key(url, k, sizeof k);
    cJSON *m = cJSON_Duplicate(store_get_json("sub_fixes"), 1);
    if (!cJSON_IsObject(m)) { cJSON_Delete(m); m = cJSON_CreateObject(); }
    cJSON_DeleteItemFromObjectCaseSensitive(m, k);
    if (fabs(fix) >= 0.05) {
        while (cJSON_GetArraySize(m) >= 300) cJSON_DeleteItemFromArray(m, 0);   /* oldest first */
        cJSON_AddNumberToObject(m, k, fix);
    }
    store_set_json("sub_fixes", m);
    store_save();
}

/* Timing moves whichever subtitles are showing: addon cues here, file tracks on the TV. */
static void shift_subs(Player *p, double by)
{
    if (p->track_loaded) {
        p->track.offset = by ? p->track.offset + by : (p->sync_state == 2 ? p->sync_offset : 0);
        if (p->active_addon_sub >= 0) sub_fix_set(p->addon_subs[p->active_addon_sub].url, p->track.offset - (p->sync_state == 2 ? p->sync_offset : 0));
        return;
    }
    if (p->embedded_text_sel < 0) return;
    p->embedded_offset = by ? p->embedded_offset + by : 0;
    push_sub_style(p);
}

static void menu_activate(Player *p);

/* The Sources sheet during playback, laid out like nuvio-native-legacy's: buttons (Find
 * more, Close) over addon tabs over the list. Focus zones: 0 the list, 1 the tabs,
 * 2 the buttons. Left/Right in the list or the tabs change the addon; Up from the top
 * row climbs to the tabs, then the buttons; OK on a source switches to it at the same
 * moment, the video playing meanwhile. */
static int sources_key(Player *p, KeyAction a)
{
    char names[SRC_ADDONS][80];
    int na = source_addons(p, names), rows[64], n = source_rows(p, rows);
    switch (a) {
    case KEY_LEFT: case KEY_RIGHT: {
        int dir = a == KEY_RIGHT ? 1 : -1;
        if (p->src_on_chips == 2) { int c = p->src_chip + dir; if (c >= 0 && c <= 1) p->src_chip = c; }
        else {
            int f = p->src_filter + dir;
            if (f < 0 || f > na) return 1;
            p->src_filter = f; p->menu_focus = 0;
            LOGI("player: sources shown for %s", f ? names[f - 1] : "every addon");
        }
        app_redraw();
        return 1;
    }
    case KEY_UP:
        if (!p->src_on_chips && p->menu_focus > 0) p->menu_focus--;
        else if (p->src_on_chips < 2) { p->src_on_chips++; p->src_chip = 0; }
        app_redraw();
        return 1;
    case KEY_DOWN:
        if (p->src_on_chips == 2) p->src_on_chips = 1;
        else if (p->src_on_chips == 1) { if (n) { p->src_on_chips = 0; p->menu_focus = 0; } }
        else if (p->menu_focus < n - 1) p->menu_focus++;
        app_redraw();
        return 1;
    case KEY_OK:
        if (p->src_on_chips == 2) {
            if (p->src_chip == 1) { p->menu = MENU_NONE; show_overlay(p); return 1; }
            if (p->more_state == 2) browse_sources(p); else find_more_sources(p);
            app_redraw();
            return 1;
        }
        if (p->src_on_chips == 1) { if (n) { p->src_on_chips = 0; p->menu_focus = 0; } app_redraw(); return 1; }
        menu_activate(p);
        p->menu = MENU_NONE;
        show_overlay(p);
        return 1;
    case KEY_BACK: p->menu = MENU_NONE; show_overlay(p); return 1;
    default: return 1;
    }
}

static int menu_rows(Player *p)
{
    if (p->menu == MENU_SOURCES) { int rows[64]; return source_rows(p, rows); }
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
        int rows[64], n = source_rows(p, rows);
        if (p->menu_focus < n && rows[p->menu_focus] != p->cur) { app_toast("Switching source"); keep_choices(p); start_candidate(p, rows[p->menu_focus], resume_spot(p, &vi)); }
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
        if (f < n) { video_select_track(TRACK_AUDIO, tr[f].index); p->audio_req = tr[f].index; p->audio_req_at = g_app.time; remember_audio(p, &tr[f]); }
        return;
    }
    if (f == SUB_CHOOSE) { p->assign_second = !p->assign_second; return; }
    if (f == SUB_TIMING) { if (p->track_loaded || p->embedded_text_sel >= 0) { shift_subs(p, 0); app_toast("Subtitle timing reset"); } return; }
    if (f >= SUB_SIZE && f <= SUB_POS) { adjust_subs(p, f, 1); return; }
    if (f == SUB_SYNC) {
        g_app.subs_autosync = !g_app.subs_autosync; app_save_prefs();
        if (!g_app.subs_autosync && p->track_loaded) { p->track.offset = p->active_addon_sub >= 0 ? sub_fix_get(p->addon_subs[p->active_addon_sub].url) : 0; p->track.scale = 1; memset(p->track.window_fix, 0, sizeof p->track.window_fix); p->sync_state = 0; }
        p->sync_for = -1;
        return;
    }
    p->subs_auto_done = 1; p->subs_user = 1;   /* the viewer decided; stop auto-selecting */
    p->replay_subs = 0;                         /* and a replay leaves them as they are */
    f -= SUB_OFF;
    if (f == 0) {
        if (p->assign_second) { select_addon_sub_slot(p, -1, 1); remember_second(p, NULL); return; }
        select_addon_sub(p, -1); p->embedded_text_sel = -1; video_select_track(TRACK_TEXT, -1); remember_subs(p, NULL, SUBS_SRC_ANY); return;
    }
    f--;
    if (f < n) {
        if (p->assign_second) { app_toast("Embedded tracks are drawn by the TV; pick an addon file as the second subtitles"); return; }
        select_addon_sub(p, -1); p->embedded_text_sel = tr[f].index; video_select_track(TRACK_TEXT, tr[f].index);
        const char *known = emb_lang(p, tr, f, n);   /* the probe's language when the pipeline gave none */
        remember_subs(p, known ? known : tr[f].lang, SUBS_SRC_EMBEDDED);
        return;
    }
    f -= n;
    int order[MAX_ADDON_SUBS];
    int ng = sorted_subs(p, order);
    if (f < ng) {
        int idx = order[f];
        if (p->assign_second) { int on = p->active_addon_sub2 != idx; select_addon_sub_slot(p, on ? idx : -1, 1); remember_second(p, on ? p->addon_subs[idx].lang : NULL); return; }
        select_addon_sub(p, idx);
        remember_subs(p, p->addon_subs[idx].lang, SUBS_SRC_ADDON);
        return;
    }
    f -= ng;
    if (xl_available(p) && f == 0) { if (p->xl_on) { int src = p->active_addon_sub; xl_stop(p); if (src >= 0) select_addon_sub(p, src); app_toast("Translation stopped"); } else xl_start(p); }
}

/* ---- the subtitles sheet: languages on the left, that language's choices on the right -- */

enum { SI_OFF, SI_EMB, SI_ADDON, SI_XL, SI_SET };
typedef struct { int kind, flat, ref; } SubItem;   /* flat: the row menu_activate knows; ref: track or file */
enum { SUB_GROUPS = 40, SUB_ITEMS = MAX_ADDON_SUBS + MAX_TRACKS + 2 };
typedef struct { char name[48]; int n, in_video, files; SubItem it[SUB_ITEMS]; } SubGroup;

/* the language a track of the file is in (its own tag, else the probe's), or NULL */
static const char *emb_lang(const Player *p, const VideoTrack *tr, int i, int n)
{
    if (lang_known(tr[i].lang)) return tr[i].lang;
    if (p->probe) {
        int count = 0;
        for (int k = 0; k < p->probe->n_tracks; k++) count += p->probe->tracks[k].kind == 3;
        if (count == n)
            for (int k = 0, seen = 0; k < p->probe->n_tracks; k++)
                if (p->probe->tracks[k].kind == 3 && seen++ == i) return lang_known(p->probe->tracks[k].lang) ? p->probe->tracks[k].lang : NULL;
    }
    return NULL;
}

enum { SG_LOOK = 0, SG_OFF = 1, SG_FIRST_LANG = 2 };   /* the drawer's groups, top down */

static SubGroup *group_for(SubGroup *g, int *ng, const char *name)
{
    for (int i = SG_FIRST_LANG; i < *ng; i++) if (!strcmp(g[i].name, name)) return &g[i];
    if (*ng >= SUB_GROUPS - 1) return NULL;
    SubGroup *x = &g[(*ng)++];
    memset(x, 0, sizeof *x);
    snprintf(x->name, sizeof x->name, "%s", name);
    return x;
}

/* Look and timing on top (one press up from Off, not under a long list), Off, then a group
 * per language (yours first, then those in the file, then by name). Returns the number. */
static int subs_groups(Player *p, SubGroup *g, const VideoTrack *tr, int n)
{
    int ng = SG_FIRST_LANG;
    memset(&g[SG_LOOK], 0, sizeof g[SG_LOOK]);
    snprintf(g[SG_LOOK].name, sizeof g[SG_LOOK].name, "Look and timing");
    for (int r = SUB_CHOOSE; r <= SUB_SYNC; r++) g[SG_LOOK].it[g[SG_LOOK].n++] = (SubItem){ SI_SET, r, r };
    memset(&g[SG_OFF], 0, sizeof g[SG_OFF]);
    snprintf(g[SG_OFF].name, sizeof g[SG_OFF].name, "Off");
    g[SG_OFF].it[g[SG_OFF].n++] = (SubItem){ SI_OFF, SUB_OFF, -1 };
    for (int i = 0; i < n; i++) {
        const char *l = emb_lang(p, tr, i, n);
        SubGroup *x = group_for(g, &ng, l ? lang_display(l) : "No language given");
        if (x && x->n < SUB_ITEMS) { x->it[x->n++] = (SubItem){ SI_EMB, SUB_OFF + 1 + i, i }; x->in_video = 1; }
    }
    int order[MAX_ADDON_SUBS], nf = sorted_subs(p, order);
    for (int k = 0; k < nf; k++) {
        const char *l = p->addon_subs[order[k]].lang;
        SubGroup *x = group_for(g, &ng, lang_known(l) ? lang_display(l) : "No language given");
        if (x && x->n < SUB_ITEMS) { x->it[x->n++] = (SubItem){ SI_ADDON, SUB_OFF + 1 + n + k, order[k] }; x->files++; }
    }
    if (xl_available(p)) {
        SubGroup *x = group_for(g, &ng, lang_display(g_app.pref_subs));
        if (x && x->n < SUB_ITEMS) x->it[x->n++] = (SubItem){ SI_XL, SUB_OFF + 1 + n + nf, -1 };
    }
    /* yours first, then the file's own languages, then by name */
    const char *mine = g_app.pref_subs[0] ? lang_display(g_app.pref_subs) : "";
    for (int i = SG_FIRST_LANG + 1; i < ng; i++)
        for (int j = i; j > SG_FIRST_LANG; j--) {
            SubGroup *a = &g[j], *b = &g[j - 1];
            int ka = !strcmp(a->name, mine) * 4 + a->in_video * 2 + (strcmp(a->name, "No language given") != 0);
            int kb = !strcmp(b->name, mine) * 4 + b->in_video * 2 + (strcmp(b->name, "No language given") != 0);
            if (ka > kb || (ka == kb && strcasecmp(a->name, b->name) < 0)) { SubGroup t = *a; *a = *b; *b = t; } else break;
        }
    return ng;
}

/* the choice showing now (the second slot's while choosing for it) */
static int item_on(const Player *p, const VideoTrack *tr, const SubItem *it)
{
    switch (it->kind) {
    case SI_OFF: return p->assign_second ? p->active_addon_sub2 < 0 : (p->embedded_text_sel < 0 && p->active_addon_sub < 0);
    case SI_EMB: return !p->assign_second && p->embedded_text_sel == tr[it->ref].index;
    case SI_ADDON: return p->assign_second ? p->active_addon_sub2 == it->ref : (p->active_addon_sub == it->ref || p->active_addon_sub2 == it->ref);
    case SI_XL: return p->xl_on != 0;
    default: return 0;
    }
}

static int group_on(const Player *p, const VideoTrack *tr, const SubGroup *g)
{
    for (int i = 0; i < g->n; i++) if (g->it[i].kind != SI_SET && item_on(p, tr, &g->it[i])) return i;
    return -1;
}

/* The groups, rebuilt only when what they are made of changed; the focus stays on its
 * language by name. tr gets the file's tracks (the same list the groups were made from). */
static SubGroup *subs_view(Player *p, VideoTrack *tr, int *nt, int *ng)
{
    *nt = video_tracks(TRACK_TEXT, tr, MAX_TRACKS);
    VideoInfo vi;
    video_info(&vi);
    unsigned key = 2166136261u;
    #define MIX(v) (key = (key ^ (unsigned)(v)) * 16777619u)
    MIX(*nt); MIX(vi.tracks_version); MIX(p->n_addon_subs); MIX(xl_available(p)); MIX(p->assign_second);
    MIX(p->probe != NULL);
    for (int i = 0; i < p->n_addon_subs; i++) { MIX(p->addon_subs[i].matched); for (const char *c = p->addon_subs[i].lang ? p->addon_subs[i].lang : ""; *c; c++) MIX(*c); }
    for (const char *c = g_app.pref_subs; *c; c++) MIX(*c);
    for (int i = 0; i < *nt; i++) { MIX(tr[i].index); for (const char *c = tr[i].lang; *c; c++) MIX(*c); }
    #undef MIX
    if (!p->sg) { p->sg = malloc(sizeof(SubGroup) * SUB_GROUPS); p->sg_key = key + 1; }
    if (!p->sg) { *ng = 0; return NULL; }
    SubGroup *g = p->sg;
    if (key != p->sg_key) {
        p->sg_key = key;
        p->sg_n = subs_groups(p, g, tr, *nt);
        if (p->subs_group_name[0])
            for (int i = 0; i < p->sg_n; i++) if (!strcmp(g[i].name, p->subs_group_name)) {
                p->subs_group = i;
                for (int r = 0; r < g[i].n; r++) if (g[i].it[r].kind == p->subs_row_kind && g[i].it[r].ref == p->subs_row_ref) { p->subs_row = r; break; }
                break;
            }
    }
    *ng = p->sg_n;
    if (p->subs_group >= *ng) p->subs_group = *ng - 1;
    if (p->subs_group < 0) p->subs_group = 0;
    return g;
}

/* Opens on what shows now: its language, and the choice in it. */
static void subs_open(Player *p)
{
    VideoTrack tr[MAX_TRACKS];
    int nt, ng;
    p->subs_group_name[0] = 0;
    SubGroup *g = subs_view(p, tr, &nt, &ng);
    if (!g) return;
    p->subs_col = 0; p->subs_group = SG_OFF; p->subs_row = 0;
    for (int i = SG_FIRST_LANG; i < ng; i++) { int r = group_on(p, tr, &g[i]); if (r >= 0) { p->subs_group = i; p->subs_row = r; break; } }
    snprintf(p->subs_group_name, sizeof p->subs_group_name, "%s", g[p->subs_group].name);
    p->subs_row_kind = g[p->subs_group].it[p->subs_row].kind; p->subs_row_ref = g[p->subs_group].it[p->subs_row].ref;
    LOGI("player: subtitles in %d language%s, on %s", ng - 2, ng - 2 == 1 ? "" : "s", g[p->subs_group].name);
}

static void subs_setting(const Player *p, int row, char *v, size_t n)
{
    static const char *SIZES[] = { "Small", "Medium", "Large", "Extra large" };
    static const char *POS[] = { "Bottom", "Raised", "Top" };
    switch (row) {
    case SUB_CHOOSE: snprintf(v, n, "%s", p->assign_second ? "Second subtitles" : "Main subtitles"); break;
    case SUB_TIMING:
        if (p->track_loaded) snprintf(v, n, "%+.1f s", p->track.offset);
        else if (p->embedded_text_sel >= 0) snprintf(v, n, "%+.1f s", p->embedded_offset);
        else snprintf(v, n, "Turn subtitles on first");
        break;
    case SUB_SIZE: snprintf(v, n, "%s", SIZES[g_app.subs_size]); break;
    case SUB_STYLE: snprintf(v, n, "%s%s%s", SUBS_STYLE_NAMES[g_app.subs_style], g_app.subs_colour ? "  \xC2\xB7  " : "", g_app.subs_colour ? SUBS_COLOUR_NAMES[g_app.subs_colour] : ""); break;
    case SUB_POS: snprintf(v, n, "%s", POS[g_app.subs_pos]); break;
    default:
        if (!g_app.subs_autosync) snprintf(v, n, "Off");
        else if (p->sync_state == 2 && p->active_addon_sub >= 0 && p->addon_subs[p->active_addon_sub].matched) snprintf(v, n, "On  \xC2\xB7  matched to the video");
        else if (p->sync_state == 2) snprintf(v, n, "On  \xC2\xB7  synced %+.1f s", p->sync_offset);
        else if (p->sync_state == 1) snprintf(v, n, "On  \xC2\xB7  aligning\xE2\x80\xA6");
        else if (p->sync_state == 3) snprintf(v, n, "On  \xC2\xB7  no matched file to align with");
        else snprintf(v, n, p->hash_state < 3 ? "On  \xC2\xB7  reading the file\xE2\x80\x99s fingerprint\xE2\x80\xA6" : "On  \xC2\xB7  waits for a matched subtitle");
    }
}

static const char *const SET_LABEL[] = { "Choose for", "Timing", "Size", "Style", "Position", "Auto-sync" };
static const char *const SET_HINT[] = { "Left/Right: a second language above the main one", "Left/Right: earlier or later  \xC2\xB7  OK resets",
    "", "Colour in Settings > Audio & subtitles", "", "OK turns it on or off: lines addon files up with the video" };

/* "SRT" from a file's address */
static void sub_format(const char *url, char *out, size_t n)
{
    out[0] = 0;
    if (!url) return;
    const char *q = strchr(url, '?'), *end = q ? q : url + strlen(url), *dot = end;
    while (dot > url && *dot != '.' && *dot != '/') dot--;
    if (*dot != '.' || end - dot < 3 || end - dot > 4) return;
    size_t k = 0;
    for (const char *c = dot + 1; c < end && k + 1 < n; c++) out[k++] = (char)toupper((unsigned char)*c);
    out[k] = 0;
    if (strcmp(out, "SRT") && strcmp(out, "VTT") && strcmp(out, "ASS") && strcmp(out, "SSA") && strcmp(out, "SUB")) out[0] = 0;
}

static float sheet_chip(const char *t, float x, float y, int foc, int accent)
{
    float w = font_measure(t, FONT_XS | FONT_B) + 20;
    gfx_rrect(x, y, w, 30, 8, foc ? COLOR_ALPHA(THEME_ON_FOCUS, 0x22) : accent ? COLOR_ALPHA(THEME_ACCENT, 0x40) : COLOR_ALPHA(THEME_TEXT, 0x18));
    font_draw(t, x + 10, y + (30 - font_height(FONT_XS | FONT_B)) / 2, FONT_XS | FONT_B, foc ? THEME_ON_FOCUS : accent ? THEME_ACCENT : THEME_TEXT_DIM);
    return w + 8;
}

/* Changing a look or timing setting: the drawer steps aside so the subtitles can be seen */
static int subs_tuning(const Player *p)
{
    return p->menu == MENU_SUBS && p->subs_col == 1 && p->subs_group == SG_LOOK;
}

/* The Look and timing panel: narrow, on the right, every setting in it, so the picture and
 * its subtitles (moved a little left meanwhile) show each change as it is made. */
static void draw_subs_tuner(Player *p, float e)
{
    const float W = 460, RH = 84, H = 110 + (SUB_SYNC + 1) * RH + 56, X = SNTV_UI_W - 48 - W, Y = (SNTV_UI_H - H) / 2, PAD = 28;
    gfx_set_alpha(e);
    gfx_set_translate(60 * (1 - e), 0);
    gfx_rrect(X, Y, W, H, 32, RGBA(0x15, 0x16, 0x1A, 0xE8));
    font_draw_tracked("LOOK AND TIMING", X + PAD, Y + 30, FONT_XS | FONT_B, COLOR_ALPHA(THEME_TEXT, 0x73), 1.5f);
    font_draw("Subtitles", X + PAD, Y + 56, FONT_M | FONT_B, THEME_TEXT);
    for (int r = 0; r <= SUB_SYNC; r++) {
        float y = Y + 110 + r * RH, cy = y + (RH - 8) / 2;
        int foc = r == p->subs_row, adj = r != SUB_SYNC;
        if (foc) gfx_rrect(X + 14, y, W - 28, RH - 8, R_KEY, THEME_FOCUS);
        Color fg = foc ? THEME_ON_FOCUS : THEME_TEXT, vc = foc ? THEME_ON_FOCUS : THEME_ACCENT;
        char v[160];
        subs_setting(p, r, v, sizeof v);
        if (r == SUB_SYNC) { char *d = strstr(v, "  \xC2\xB7"); if (d) *d = 0; }
        font_draw(SET_LABEL[r], X + PAD + 6, cy - font_height(FONT_S | FONT_B) / 2, FONT_S | FONT_B, fg);
        float vr = X + W - PAD - (foc && adj ? 26 : 0), vw = font_measure(v, FONT_S | FONT_B);
        font_draw(v, vr - vw, cy - font_height(FONT_S | FONT_B) / 2, FONT_S | FONT_B, vc);
        if (foc && adj) { ui_icon(ICON_CHEVRON_LEFT, vr - vw - 18, cy, 18, vc); ui_icon(ICON_CHEVRON_RIGHT, vr + 14, cy, 18, vc); }
    }
    font_draw_fit("Up/Down  setting   \xC2\xB7   Left/Right  change   \xC2\xB7   Back", X + PAD, Y + H - 44, FONT_XS, THEME_TEXT_MUTED, W - 2 * PAD);
    gfx_set_alpha(1);
    gfx_set_translate(0, 0);
}

static void draw_subs_panel(Player *p)
{
    const float SW = 960, SX = SNTV_UI_W - 64 - SW, SY = 32, SH = SNTV_UI_H - SY - 48, PAD = 36;
    const float LW = 300, ROW = 76, OROW = 92;
    if (p->menu_seen != p->menu) { p->menu_seen = p->menu; p->menu_t = g_app.fx_light ? 1 : 0; if (getenv("SNTV_SCREENSHOT")) LOGI("UI check: player drawer=%d", p->menu); }
    if (p->menu_t < 1) { p->menu_t += 0.1f; if (p->menu_t > 1) p->menu_t = 1; app_redraw(); }
    float e = 1 - (1 - p->menu_t) * (1 - p->menu_t);
    float want = subs_tuning(p) ? 1 : 0;   /* the drawer gives way to the panel, and back */
    if ((int)want != p->tune_on) { p->tune_on = (int)want; LOGI("player: subtitles %s", want ? "Look and timing panel, the drawer aside" : "drawer"); }
    if (g_app.fx_light || p->menu_t < 1) p->tune_t = want;
    else if (p->tune_t != want) { p->tune_t += want > p->tune_t ? 0.125f : -0.125f; if (p->tune_t < 0) p->tune_t = 0; if (p->tune_t > 1) p->tune_t = 1; app_redraw(); }
    float k = 1 - (1 - p->tune_t) * (1 - p->tune_t);
    if (k > 0) draw_subs_tuner(p, e * k);
    if (k >= 1) return;
    e *= 1 - k;
    gfx_gradient_h(SX - 700, 0, 700 + SW + 64, SNTV_UI_H, RGBA(0, 0, 0, 0), RGBA(0, 0, 0, (int)(0x6B * e)));
    gfx_set_alpha(e);
    gfx_set_translate(40 * (1 - e), 0);
    gfx_rrect(SX, SY, SW, SH, 36, RGBA(0x15, 0x16, 0x1A, 0xF6));
    VideoTrack tr[MAX_TRACKS];
    int nt, ng;
    SubGroup *g = subs_view(p, tr, &nt, &ng);
    if (!g || !ng) { gfx_set_alpha(1); gfx_set_translate(0, 0); return; }
    const SubGroup *cur = &g[p->subs_group];
    if (p->subs_row >= cur->n) p->subs_row = cur->n - 1;
    float cx = SX + PAD, top = SY + 150, bottom = SY + SH - 64;

    char kick[200];
    ui_upper(p->title ? p->title : "", kick, sizeof kick);
    font_draw_tracked(kick, cx, SY + 34, FONT_XS | FONT_B, COLOR_ALPHA(THEME_TEXT, 0x73), 1.5f);
    font_draw("Subtitles", cx, SY + 70, FONT_L | FONT_B, THEME_TEXT);
    if (p->assign_second) sheet_chip("CHOOSING THE SECOND", cx + font_measure("Subtitles", FONT_L | FONT_B) + 24, SY + 82, 0, 1);
    if (p->subs_pending) {
        const char *t = "ASKING YOUR ADDONS\xE2\x80\xA6";
        font_draw(t, SX + SW - PAD - font_measure(t, FONT_XS | FONT_B), SY + 34, FONT_XS | FONT_B, THEME_ACCENT);
    }

    /* languages */
    float lscroll = 0, fy = p->subs_group * ROW + (p->subs_group > SG_LOOK ? 14 : 0);
    if (fy + ROW > bottom - top) lscroll = fy + ROW - (bottom - top);
    gfx_clip_push(SX, top - 4, LW + PAD + 8, bottom - top + 8);
    for (int i = 0; i < ng; i++) {
        float y = top + i * ROW - lscroll + (i > SG_LOOK ? 14 : 0);
        if (y + ROW < top - 4 || y > bottom) continue;
        if (i == SG_OFF) gfx_rect(cx + 12, y - 10, LW - 24, 1, COLOR_ALPHA(THEME_TEXT, 0x18));   /* the settings above, the choices below */
        int sel = i == p->subs_group, foc = sel && p->subs_col == 0;
        if (foc) gfx_rrect(cx, y, LW, ROW - 8, R_KEY, THEME_FOCUS);
        else if (sel) gfx_rrect(cx, y, LW, ROW - 8, R_KEY, COLOR_ALPHA(THEME_TEXT, 0x12));
        Color fg = foc ? THEME_ON_FOCUS : sel ? THEME_TEXT : THEME_TEXT_DIM;
        int on = i != SG_LOOK && group_on(p, tr, &g[i]) >= 0;
        font_draw_fit(g[i].name, cx + 24, y + (ROW - 8 - font_height(FONT_S | FONT_B)) / 2, FONT_S | FONT_B, on && !foc ? THEME_ACCENT : fg, LW - 120);
        if (i >= SG_FIRST_LANG) {
            char c[16]; snprintf(c, sizeof c, "%d", g[i].n);
            font_draw(c, cx + LW - 64 - font_measure(c, FONT_XS | FONT_B) / 2, y + (ROW - 8 - font_height(FONT_XS | FONT_B)) / 2, FONT_XS | FONT_B, foc ? COLOR_ALPHA(THEME_ON_FOCUS, 0xA0) : THEME_TEXT_MUTED);
        }
        if (on) ui_icon(ICON_CHECK, cx + LW - 28, y + (ROW - 8) / 2, 22, foc ? THEME_ON_FOCUS : THEME_ACCENT);
        else if (i == SG_LOOK) ui_icon(ICON_SUBS, cx + LW - 28, y + (ROW - 8) / 2, 22, fg);
    }
    gfx_clip_pop();
    float ox = cx + LW + 36, ow = SX + SW - PAD - ox;
    gfx_rect(ox - 18, top, 1, bottom - top, COLOR_ALPHA(THEME_TEXT, 0x14));

    /* that language's choices */
    char head[96];
    if (p->subs_group == SG_OFF) snprintf(head, sizeof head, "NO SUBTITLES");
    else if (p->subs_group == SG_LOOK) snprintf(head, sizeof head, "LOOK AND TIMING  \xC2\xB7  LEFT/RIGHT CHANGES");
    else { char up[64]; ui_upper(cur->name, up, sizeof up); snprintf(head, sizeof head, "%s  \xC2\xB7  %d CHOICE%s", up, cur->n, cur->n == 1 ? "" : "S"); }
    font_draw_tracked(head, ox, top - 40, FONT_XS | FONT_B, THEME_TEXT_MUTED, 1.2f);
    float oscroll = 0, oy = p->subs_row * OROW;
    if (p->subs_col == 1 && oy + OROW > bottom - top) oscroll = oy + OROW - (bottom - top);
    gfx_clip_push(ox - 8, top - 4, ow + 16, bottom - top + 8);
    for (int r = 0; r < cur->n; r++) {
        const SubItem *it = &cur->it[r];
        float y = top + r * OROW - oscroll, rh = OROW - 10;
        if (y + OROW < top - 4 || y > bottom) continue;
        int foc = p->subs_col == 1 && r == p->subs_row, on = item_on(p, tr, it);
        if (foc) gfx_rrect(ox, y, ow, rh, R_KEY, THEME_FOCUS);
        else if (on) gfx_rrect(ox, y, ow, rh, R_KEY, COLOR_ALPHA(THEME_TEXT, 0x12));
        Color fg = foc ? THEME_ON_FOCUS : THEME_TEXT, dim = foc ? COLOR_ALPHA(THEME_ON_FOCUS, 0xB0) : THEME_TEXT_MUTED;
        char title[160] = "", sub[200] = "", fmt[8] = "";
        float tx = ox + 24, room = ow - 48 - (on ? 50 : 0);
        if (it->kind == SI_SET) {
            subs_setting(p, it->ref, sub, sizeof sub);
            int adj = it->ref != SUB_SYNC;
            Color vc = foc ? THEME_ON_FOCUS : THEME_ACCENT;
            float vw = font_measure(sub, FONT_S | FONT_B), vr = ox + ow - 24 - (adj ? 30 : 0), vy = y + rh / 2;
            font_draw(sub, vr - vw, vy - font_height(FONT_S | FONT_B) / 2, FONT_S | FONT_B, vc);
            if (adj) { ui_icon(ICON_CHEVRON_LEFT, vr - vw - 22, vy, 20, vc); ui_icon(ICON_CHEVRON_RIGHT, vr + 16, vy, 20, vc); }
            font_draw(SET_LABEL[it->ref], tx, y + (SET_HINT[it->ref][0] ? 14 : (rh - font_height(FONT_S | FONT_B)) / 2), FONT_S | FONT_B, fg);
            if (SET_HINT[it->ref][0]) font_draw_fit(SET_HINT[it->ref], tx, y + 50, FONT_XS, dim, vr - vw - (adj ? 50 : 20) - tx);
            continue;
        }
        if (it->kind == SI_OFF) { snprintf(title, sizeof title, "No subtitles"); snprintf(sub, sizeof sub, p->assign_second ? "Only the main subtitles show" : "The picture alone"); }
        else if (it->kind == SI_EMB) {
            char t2[96], s2[160];
            describe_track(p, 3, tr, it->ref, nt, t2, sizeof t2, s2, sizeof s2);
            snprintf(title, sizeof title, "%s", tr[it->ref].label[0] && strcasecmp(tr[it->ref].label, t2) ? tr[it->ref].label : t2);
            char up[8]; ui_upper(s2, up, sizeof up);   /* a bare format ("SRT") reads as a chip, as for files */
            if (s2[0] && !strchr(s2, ' ') && strlen(s2) <= 6) snprintf(fmt, sizeof fmt, "%s", up); else snprintf(sub, sizeof sub, "%s", s2);
        } else if (it->kind == SI_ADDON) {
            const AddonSub *a = &p->addon_subs[it->ref];
            snprintf(title, sizeof title, "%s", a->addon);
            if (!a->matched && a->fit && a->fit_why[0]) snprintf(sub, sizeof sub, "%s", a->fit_why);
            else if (a->release) snprintf(sub, sizeof sub, "%s%s", a->release, a->name >= 60 ? "  \xC2\xB7  same release" : "");
            else {   /* nothing else to tell them apart: which of the addon's files in this language */
                int nth = 0, of = 0;
                for (int k = 0; k < cur->n; k++) if (cur->it[k].kind == SI_ADDON && !strcmp(p->addon_subs[cur->it[k].ref].addon, a->addon)) { of++; if (k < r) nth++; }
                if (of > 1) snprintf(sub, sizeof sub, "File %d of %d from this addon", nth + 1, of);
            }
            sub_format(a->url, fmt, sizeof fmt);
        } else {   /* SI_XL */
            snprintf(title, sizeof title, "Translated by AI");
            if (p->xl_on == 1) snprintf(sub, sizeof sub, "Translating\xE2\x80\xA6  %d of %d lines", p->xl_n_done, p->track.n);
            else if (p->xl_on == 2) snprintf(sub, sizeof sub, "%d lines", p->track.n);
            else snprintf(sub, sizeof sub, "From the %s file: nobody offers %s for this title", lang_display(p->addon_subs[xl_source(p)].lang), lang_display(g_app.pref_subs));
        }
        float w0 = font_measure(title, FONT_S | FONT_B), bx = tx + (w0 < room - 260 ? w0 : room - 260) + 14;
        float ty = sub[0] ? y + 12 : y + (rh - font_height(FONT_S | FONT_B)) / 2, cy = ty + 1;   /* one line: centred */
        font_draw_fit(title, tx, ty, FONT_S | FONT_B, on && !foc ? THEME_ACCENT : fg, room - 260);
        if (it->kind == SI_EMB) bx += sheet_chip("IN THE VIDEO", bx, cy, foc, 0);
        if (fmt[0]) bx += sheet_chip(fmt, bx, cy, foc, 0);
        if (it->kind == SI_ADDON && p->addon_subs[it->ref].matched) bx += sheet_chip("MATCHED", bx, cy, foc, 1);
        if (it->kind == SI_ADDON && p->active_addon_sub2 == it->ref) bx += sheet_chip("SECOND", bx, cy, foc, 1);
        if (sub[0]) font_draw_fit(sub, tx, y + 50, FONT_XS, dim, room);
        if (on) { gfx_circle(ox + ow - 40, y + rh / 2, 17, foc ? THEME_ON_FOCUS : THEME_ACCENT); ui_icon(ICON_CHECK, ox + ow - 40, y + rh / 2, 20, foc ? THEME_FOCUS : THEME_ON_ACCENT); }
    }
    gfx_clip_pop();
    const char *hint = p->subs_col == 0 ? "Up/Down  language   \xC2\xB7   OK  its first choice   \xC2\xB7   Right  all its choices   \xC2\xB7   Back  close"
                                        : p->subs_group == SG_LOOK ? "Left/Right  change   \xC2\xB7   OK  reset or switch   \xC2\xB7   Back  languages"
                                        : "OK  choose   \xC2\xB7   Left  languages   \xC2\xB7   Back  languages";
    font_draw_fit(hint, cx, SY + SH - 44, FONT_XS, THEME_TEXT_MUTED, SW - 2 * PAD);
    gfx_set_alpha(1);
    gfx_set_translate(0, 0);
}

/* chooses item it as menu_activate would its row; 1 when the sheet should stay open */
static int subs_choose(Player *p, const SubItem *it, const char *group)
{
    int keep = it->kind == SI_SET || it->kind == SI_XL || p->assign_second;
    static const char *const KIND[] = { "off", "the video's track", "a file from ", "AI translation", "setting" };
    if (it->kind != SI_SET)
        LOGI("player: subtitles sheet: %s%s in %s%s", KIND[it->kind], it->kind == SI_ADDON ? p->addon_subs[it->ref].addon : "",
             group, p->assign_second ? ", as the second" : "");
    p->menu_focus = it->flat;
    menu_activate(p);
    return keep;
}

static int subs_key(Player *p, KeyAction a)
{
    VideoTrack tr[MAX_TRACKS];
    int nt, ng, close = 0;
    SubGroup *g = subs_view(p, tr, &nt, &ng);
    if (!g || !ng) return 1;
    SubGroup *cur = &g[p->subs_group];
    if (p->subs_row >= cur->n) p->subs_row = cur->n ? cur->n - 1 : 0;
    const SubItem *it = cur->n ? &cur->it[p->subs_row] : NULL;
    int adjustable = p->subs_col == 1 && it && it->kind == SI_SET && it->ref != SUB_SYNC;
    switch (a) {
    case KEY_UP: case KEY_DOWN: {
        int d = a == KEY_DOWN ? 1 : -1;
        if (p->subs_col == 0) {
            int ngp = p->subs_group + d;
            if (ngp >= 0 && ngp < ng) { p->subs_group = ngp; int r = group_on(p, tr, &g[ngp]); p->subs_row = r > 0 ? r : 0; }
        } else if (p->subs_row + d >= 0 && p->subs_row + d < cur->n) p->subs_row += d;
        break;
    }
    case KEY_LEFT: case KEY_RIGHT: {
        int dir = a == KEY_RIGHT ? 1 : -1;
        if (adjustable) {
            if (it->ref == SUB_CHOOSE) p->assign_second = !p->assign_second;
            else if (it->ref == SUB_TIMING) shift_subs(p, 0.1 * dir);
            else adjust_subs(p, it->ref, dir);
            char v[160]; subs_setting(p, it->ref, v, sizeof v);
            LOGI("player: subtitles sheet: %s %s", SET_LABEL[it->ref], v);
        } else if (p->subs_col == 0 && dir > 0 && p->subs_group != SG_OFF && cur->n) {
            p->subs_col = 1; int r = group_on(p, tr, cur); p->subs_row = r > 0 ? r : 0;
        } else if (p->subs_col == 1 && dir < 0) p->subs_col = 0;
        break;
    }
    case KEY_OK:
        if (!it) break;
        if (p->subs_col == 0 && p->subs_group == SG_LOOK) { p->subs_col = 1; p->subs_row = 0; break; }
        if (p->subs_col == 0) {   /* a language: its first choice (the file's own, or the best-matched file) */
            int first = 0;   /* the second slot takes files only: the TV draws one track at a time */
            while (p->assign_second && first < cur->n - 1 && cur->it[first].kind == SI_EMB) first++;
            LOGI("player: subtitles: %s, its first choice", cur->name);
            close = !subs_choose(p, &cur->it[first], cur->name);
            if (!close) { p->subs_col = 1; p->subs_row = first; }
            break;
        }
        close = !subs_choose(p, it, cur->name);
        if (it->kind == SI_SET) { char v[160]; subs_setting(p, it->ref, v, sizeof v); LOGI("player: subtitles sheet: %s %s", SET_LABEL[it->ref], v); }
        break;
    case KEY_BACK:
        if (p->subs_col == 1) p->subs_col = 0; else close = 1;
        break;
    default: break;
    }
    if (p->subs_group < ng) {
        snprintf(p->subs_group_name, sizeof p->subs_group_name, "%s", g[p->subs_group].name);
        if (p->subs_row < g[p->subs_group].n) { p->subs_row_kind = g[p->subs_group].it[p->subs_row].kind; p->subs_row_ref = g[p->subs_group].it[p->subs_row].ref; }
    }
    if (close) p->menu = MENU_NONE;
    show_overlay(p);
    return 1;
}

static void seek_by(Player *p, double base)
{
    VideoInfo vi;
    video_info(&vi);
    /* a steady step a press, held or not; a quick second press goes twice as far */
    double step = base;
    if (!g_app.key_repeat) {
        if (g_app.time - p->seek_last_press < 0.4f && p->seek_flash_dir == (base < 0 ? -1 : 1)) step = base * 2;
        p->seek_last_press = g_app.time;
    }
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


/* ---- touch, on a phone, which turns sideways to play: the picture fills the screen and a
 * tap brings the controls over it for a few seconds (Subtitles, Audio, Speed, Sources and
 * Episodes open a list at the right; on Android, picture in picture). A double tap on a
 * side of the picture skips 10 s (again: further), a drag across it or along the bar seeks
 * (the time and a preview follow the finger), and up or down a side sets the brightness
 * (left) or the volume (right). ---- */
enum { PH_NONE, PH_AUDIO, PH_SUBS, PH_SOURCES, PH_EPISODES, PH_SPEED };
enum { PHH_BACK = 0x7FFF0000, PHH_PLAY, PHH_BACK10, PHH_FWD10, PHH_SKIP, PHH_NEXT, PHH_STILL_ON, PHH_STILL_STOP, PHH_CLOSE,
       PHH_PREV_EP, PHH_NEXT_EP, PHH_ASPECT, PHH_PANEL, PHH_SHEET = 0x7FFE0000, PHH_ROW = 0x7FFD0000 };
enum { PHB_SUBS, PHB_AUDIO, PHB_SPEED, PHB_SOURCES, PHB_EPISODES, PHB_PIP, PHB_COUNT };
#define PH_ROWS 72
#define PH_SWIPE_SECS 200.0   /* a drag across the whole picture seeks this far */

typedef struct { char label[120], sub[120]; int on, act; } PhRow;

static int ph_rows(Player *p, PhRow *r)
{
    int n = 0;
    VideoTrack tr[MAX_TRACKS];
    if (p->ph_sheet == PH_SPEED) {
        for (int i = 0; i < N_RATES && n < PH_ROWS; i++, n++) { snprintf(r[n].label, sizeof r[n].label, i == RATE_NORMAL ? "Normal" : "%gx", RATES[i]); r[n].sub[0] = 0; r[n].on = p->rate_idx == i; r[n].act = i; }
        return n;
    }
    if (p->ph_sheet == PH_AUDIO) {
        int nt = video_tracks(TRACK_AUDIO, tr, MAX_TRACKS);
        for (int i = 0; i < nt && n < PH_ROWS; i++, n++) { describe_track(p, 2, tr, i, nt, r[n].label, sizeof r[n].label, r[n].sub, sizeof r[n].sub); r[n].on = tr[i].selected; r[n].act = 1 + i; }
        return n;
    }
    if (p->ph_sheet == PH_SUBS) {
        int nt = video_tracks(TRACK_TEXT, tr, MAX_TRACKS);
        snprintf(r[n].label, sizeof r[n].label, "Off"); r[n].sub[0] = 0; r[n].on = p->embedded_text_sel < 0 && p->active_addon_sub < 0; r[n].act = SUB_OFF; n++;
        for (int i = 0; i < nt && n < PH_ROWS; i++, n++) { describe_track(p, 3, tr, i, nt, r[n].label, sizeof r[n].label, r[n].sub, sizeof r[n].sub); r[n].on = p->embedded_text_sel == tr[i].index; r[n].act = SUB_OFF + 1 + i; }
        int order[MAX_ADDON_SUBS], nf = sorted_subs(p, order);
        for (int k = 0; k < nf && n < PH_ROWS; k++, n++) {
            const AddonSub *as = &p->addon_subs[order[k]];
            snprintf(r[n].label, sizeof r[n].label, "%s%s%s", lang_known(as->lang) ? lang_display(as->lang) : "Subtitles", as->addon ? "  \xC2\xB7  " : "", as->addon ? as->addon : "");
            snprintf(r[n].sub, sizeof r[n].sub, "%s%s", as->matched ? "Matched to this file  \xC2\xB7  " : "", as->release ? as->release : "");
            r[n].on = p->active_addon_sub == order[k]; r[n].act = SUB_OFF + 1 + nt + k;
        }
        if (xl_available(p) && n < PH_ROWS) { snprintf(r[n].label, sizeof r[n].label, "Translate with AI"); snprintf(r[n].sub, sizeof r[n].sub, "Into %s", lang_display(g_app.pref_subs)); r[n].on = p->xl_on != 0; r[n].act = SUB_OFF + 1 + nt + nf; n++; }
        return n;
    }
    if (p->ph_sheet == PH_SOURCES) {
        int rows[64], k = source_rows(p, rows);
        for (int i = 0; i < k && n < PH_ROWS; i++, n++) {
            const PlayCandidate *c = &p->cands[rows[i]];
            char ad[80]; cand_addon(c, ad, sizeof ad);
            snprintf(r[n].label, sizeof r[n].label, "%s%s%s", c->recommended ? "Recommended  \xC2\xB7  " : "", c->detail ? c->detail : c->label ? c->label : "Source", "");
            snprintf(r[n].sub, sizeof r[n].sub, "%s%s%s", ad, ad[0] ? "  \xC2\xB7  " : "", c->filename ? c->filename : c->release ? c->release : "");
            r[n].on = rows[i] == p->cur; r[n].act = i;
        }
        return n;
    }
    if (p->ph_sheet == PH_EPISODES) {
        int n_eps = 0;
        const MetaVideo *eps = app_videos(p->meta.id, &n_eps);
        for (int i = 0; i < n_eps && n < PH_ROWS; i++, n++) {
            if (eps[i].season >= 0 && eps[i].episode >= 0) snprintf(r[n].label, sizeof r[n].label, "S%d E%d  %s", eps[i].season, eps[i].episode, eps[i].title ? eps[i].title : "");
            else snprintf(r[n].label, sizeof r[n].label, "%s", eps[i].title ? eps[i].title : "Episode");
            int cur = strcmp(eps[i].id, p->video_id) == 0;
            snprintf(r[n].sub, sizeof r[n].sub, "%s", cur ? "Now playing" : eps[i].released && strlen(eps[i].released) >= 10 ? eps[i].released : "");
            if (!cur && r[n].sub[0]) r[n].sub[10] = 0;
            r[n].on = cur; r[n].act = i;
        }
        return n;
    }
    return 0;
}

static void ph_pick(Player *p, const PhRow *r)
{
    int sheet = p->ph_sheet;
    if (sheet == PH_SPEED) { set_rate(p, r->act); return; }
    p->menu = sheet == PH_AUDIO ? MENU_AUDIO : sheet == PH_SUBS ? MENU_SUBS : sheet == PH_SOURCES ? MENU_SOURCES : MENU_EPISODES;
    if (sheet == PH_SOURCES) p->src_filter = 0;
    p->menu_focus = r->act;
    menu_activate(p);
    p->menu = MENU_NONE;
    if (sheet == PH_SOURCES || sheet == PH_EPISODES) p->ph_sheet = PH_NONE;   /* something else starts */
}

static void ph_open(Player *p, int sheet)
{
    if (p->ph_sheet == sheet) { p->ph_sheet = PH_NONE; return; }
    p->ph_sheet = sheet; p->ph_scroll = 0; p->ph_focus = 0; p->ph_pan.free = 0;
    PhRow r[PH_ROWS];
    int n = ph_rows(p, r);
    for (int i = 0; i < n; i++) if (r[i].on) { p->ph_focus = i; break; }
}

static int ph_has_eps(const Player *p) { int n = 0; app_videos(p->meta.id, &n); return n > 1; }

static void ph_round(Icon icon, const char *label, float cx, float cy, float rad, int primary, int hit)
{
    gfx_circle(cx, cy, rad, primary ? RGBA(255, 255, 255, 255) : RGBA(0, 0, 0, 0x60));
    if (!primary) gfx_rrect_ring(cx - rad, cy - rad, 2 * rad, 2 * rad, rad, 2, COLOR_ALPHA(THEME_TEXT, 0x30));
    if (label) font_draw_center(label, cx, cy - font_height(FONT_S | FONT_B) / 2, FONT_S | FONT_B, primary ? RGBA(0x11, 0x11, 0x11, 255) : THEME_TEXT);
    else ui_icon(icon, cx + (icon == ICON_PLAY ? 4 : 0), cy, rad * 0.9f, primary ? RGBA(0x11, 0x11, 0x11, 255) : THEME_TEXT);
    ui_hit(cx - rad - 10, cy - rad - 10, 2 * rad + 20, 2 * rad + 20, hit);
}

static void ph_time(double t, char *out, size_t n)
{
    int s = (int)(t + 0.5); if (s < 0) s = 0;
    if (s >= 3600) snprintf(out, n, "%d:%02d:%02d", s / 3600, s / 60 % 60, s % 60); else snprintf(out, n, "%d:%02d", s / 60, s % 60);
}

static double ph_dur(const Player *p, const VideoInfo *vi) { return vi->duration > 0 ? vi->duration : p->known_dur; }

/* Full screen on a phone: the controls show for a while after a tap; always while paused,
 * starting, a list is open or the source failed. */
static int fs_shown(const Player *p, const VideoInfo *vi)
{
    return p->ph_sheet || g_app.time < p->fs_until || vi->state != VIDEO_PLAYING || p->tch || error_final(p, vi);
}

static void toggle_play(Player *p, const VideoInfo *vi)
{
    if (vi->state == VIDEO_PLAYING) { video_pause(); p->viewer_paused = 1; p->flash_icon = ICON_PAUSE; }
    else { video_play(); p->flash_icon = ICON_PLAY; }
    p->play_flash = g_app.time + 0.6f;
    app_redraw();
}

/* A touch seek: shown at once, made after a moment (a run of double taps or a drag is one seek). */
static void touch_seek(Player *p, double t, float after)
{
    VideoInfo vi;
    video_info(&vi);
    double dur = ph_dur(p, &vi);
    if (t < 0) t = 0;
    if (dur > 0 && t > dur - 1) t = dur - 1;
    if (p->seek_commit <= 0) p->seek_origin = vi.position;
    p->seek_target = t;
    p->seek_commit = after;
    if (g_app.live_scrub && g_app.time - p->last_live_seek > 0.33f) { p->last_live_seek = g_app.time; video_seek(t); }
    app_redraw();
}

static void ph_neighbour(Player *p, int dir)
{
    if (dir > 0 && p->next_video_id) { go_to_video(p, p->next_video_id, p->next_title); return; }
    if (dir < 0 && p->prev_video_id) { go_to_video(p, p->prev_video_id, p->prev_title); return; }
}

/* The open list in a panel: rows to tap, dragged to scroll. */
static void ph_sheet_draw(Player *p, float x, float top, float w, float bottom, int titled)
{
    static const char *const NAMES[] = { "", "Audio", "Subtitles", "Sources", "Episodes", "Speed" };
    PhRow r[PH_ROWS];
    int n = ph_rows(p, r);
    float rh = 112, W = SNTV_UI_W;
    gfx_rrect(x - 20, top, w + 40, bottom - top, R_PANEL, COLOR_ALPHA(THEME_PANEL, 0xF4));
    ui_hit(x - 20, top, w + 40, bottom - top, PHH_PANEL);   /* the panel itself: a tap there is not a tap on the picture */
    if (titled) {
        font_draw(NAMES[p->ph_sheet], x + 12, top + 28, FONT_M | FONT_B, THEME_TEXT);
        ui_icon(ICON_CLOSE, x + w - 30, top + 52, 30, THEME_TEXT_DIM);
        ui_hit(x + w - 90, top + 8, 100, 90, PHH_CLOSE);
        top += 100;
    }
    if (p->ph_focus >= n) p->ph_focus = n ? n - 1 : 0;
    float view = bottom - top - 26, want = p->ph_scroll;
    if (p->ph_focus * rh + rh > want + view) want = p->ph_focus * rh + rh - view;
    if (p->ph_focus * rh < want) want = p->ph_focus * rh;
    float dt = g_app.time - p->ph_t;
    p->ph_t = g_app.time;
    ui_pan(&p->ph_pan, &p->ph_scroll, want, n * rh - view, dt < 0 ? 0 : dt > 0.1f ? 0.1f : dt, 16);
    ui_pan_area(&p->ph_pan, x - 20, top, w + 40, bottom - top, 1);
    gfx_clip_push(x - 20, top + 10, w + 40, bottom - top - 20);
    float ry = top + 16 - p->ph_scroll;
    if (!n) font_draw_center(p->ph_sheet == PH_SUBS ? "No subtitles for this video" : p->ph_sheet == PH_AUDIO ? "One audio track" : "Nothing here", x + w / 2, top + 60, FONT_S, THEME_TEXT_MUTED);
    for (int i = 0; i < n; i++, ry += rh) {
        if (ry + rh < top || ry > bottom) continue;
        int foc = i == p->ph_focus;
        if (foc) gfx_rrect(x - 8, ry, w + 16, rh - 10, R_KEY, COLOR_ALPHA(THEME_TEXT, 0x18));
        font_draw_fit(r[i].label, x + 12, ry + (r[i].sub[0] ? 16 : (rh - 10 - font_height(FONT_S | FONT_B)) / 2), FONT_S | FONT_B, THEME_TEXT, w - 90);
        if (r[i].sub[0]) font_draw_fit(r[i].sub, x + 12, ry + 58, FONT_XS, THEME_TEXT_MUTED, w - 90);
        if (r[i].on) { gfx_circle(x + w - 30, ry + (rh - 10) / 2, 18, THEME_ACCENT); ui_icon(ICON_CHECK, x + w - 30, ry + (rh - 10) / 2, 22, THEME_ON_ACCENT); }
        ui_hit(x - 8, ry, w + 16, rh - 10, PHH_ROW + i);
    }
    gfx_clip_pop();
    (void)W;
}

/* The scrub bar: the buffered part, where it is, and while a finger has it a larger knob
 * with the time (and a preview frame when there is one) above it. */
static void ph_bar(Player *p, const VideoInfo *vi, float x, float y, float w, float *rect)
{
    double dur = ph_dur(p, vi), pos = p->seek_commit > 0 ? p->seek_target : vi->position;
    float frac = dur > 0 ? (float)(pos / dur) : 0;
    if (frac < 0) frac = 0;
    if (frac > 1) frac = 1;
    int held = p->tch == 1 || p->tch_seeking;
    float th = held ? 12 : 8;
    gfx_rrect(x, y - th / 2, w, th, th / 2, COLOR_ALPHA(THEME_TEXT, 0x38));
    if (vi->buffer_ahead > 0 && dur > 0) { float b = (float)((vi->position + vi->buffer_ahead) / dur); if (b > 1) b = 1; gfx_rrect(x, y - th / 2, w * b, th, th / 2, COLOR_ALPHA(THEME_TEXT, 0x58)); }
    gfx_rrect(x, y - th / 2, w * frac, th, th / 2, THEME_ACCENT);
    gfx_circle(x + w * frac, y, held ? 24 : 16, RGBA(255, 255, 255, 255));
    rect[0] = x - 30; rect[1] = y - 60; rect[2] = w + 60; rect[3] = 120;   /* a finger's width either side */
    if (!held || dur <= 0) return;
    float kx = x + w * frac;
    char t[24];
    ph_time(pos, t, sizeof t);
    int iw = 0, ih = 0;
    unsigned tex = seek_thumb(p, pos, &iw, &ih, 0);
    float pw = tex && iw > 0 && ih > 0 ? 320 : 0, ph = pw ? pw * ih / iw : 0;
    float bw = (pw > font_measure(t, FONT_M | FONT_B) + 40 ? pw : font_measure(t, FONT_M | FONT_B) + 40) + 12, bh = ph + 70;
    float bx = kx - bw / 2, by = y - 48 - bh;
    if (bx < 12) bx = 12;
    if (bx + bw > SNTV_UI_W - 12) bx = SNTV_UI_W - 12 - bw;
    gfx_rrect(bx, by, bw, bh, R_KEY, RGBA(0x10, 0x10, 0x10, 0xE8));
    if (pw) gfx_tex(tex, bx + (bw - pw) / 2, by + 6, pw, ph, RGBA(255, 255, 255, 255));
    font_draw_center(t, bx + bw / 2, by + bh - 58, FONT_M | FONT_B, THEME_TEXT);
}

/* Over the picture: the double taps' skip (a ripple at that side with how far), a drag's
 * seek (how far, and to where), the play/pause flash. */
static void ph_feedback(Player *p, const VideoInfo *vi, float x, float y, float w, float h)
{
    float k = 1 - (g_app.time - p->ripple_at) / 0.7f;
    if (p->ripple_side && k > 0) {
        float cx = p->ripple_side < 0 ? x + w * 0.18f : x + w * 0.82f, cy = y + h / 2, r = h * 0.38f;
        gfx_circle(cx, cy, r * (1.1f - 0.1f * k), RGBA(255, 255, 255, (int)(0x30 * k)));
        char t[24]; snprintf(t, sizeof t, "%s%d s", p->ripple_side < 0 ? "-" : "+", p->ripple_secs);
        ui_icon(p->ripple_side < 0 ? ICON_PREV : ICON_NEXT, cx, cy - 30, 40, COLOR_ALPHA(THEME_TEXT, (int)(255 * (k < 0.5f ? k * 2 : 1))));
        font_draw_center(t, cx, cy + 10, FONT_S | FONT_B, COLOR_ALPHA(THEME_TEXT, (int)(255 * (k < 0.5f ? k * 2 : 1))));
        app_redraw();
    }
    if (p->tch_seeking) {
        double d = p->seek_target - p->tch_pos0;
        char a[24], b[24], line[64];
        ph_time(fabs(d), a, sizeof a);
        ph_time(p->seek_target, b, sizeof b);
        snprintf(line, sizeof line, "%s%s", d < 0 ? "-" : "+", a);
        float bw = 360, bh = 130, bx = x + (w - bw) / 2, by = y + (h - bh) / 2;
        gfx_rrect(bx, by, bw, bh, R_PANEL, RGBA(0, 0, 0, 0xB0));
        font_draw_center(line, x + w / 2, by + 16, FONT_L | FONT_B, THEME_TEXT);
        font_draw_center(b, x + w / 2, by + 80, FONT_S, THEME_TEXT_DIM);
    }
    if (p->soft_dim && p->bright >= 0 && p->bright < 1) gfx_rect(x, y, w, h, RGBA(0, 0, 0, (int)(0xD8 * (1 - p->bright))));
    float ak = p->tch_adjust ? 1 : 1 - (g_app.time - p->adj_at - 0.6f) / 0.3f;
    if (p->adj_at > 0 && ak > 0) {   /* the level, at the side being changed */
        int what = p->tch_adjust ? p->tch_adjust : p->adj_last;
        float v = what == 1 ? p->bright : p->vol, bh = h * 0.8f, bw = 72;
        float bx = what == 1 ? x + w * 0.1f - bw / 2 : x + w * 0.9f - bw / 2, by = y + (h - bh) / 2;
        if (ak > 1) ak = 1;
        float fy = by + 64, fh = bh - 128;   /* the level between the icon and the number */
        gfx_rrect(bx, by, bw, bh, bw / 2, RGBA(0x20, 0x20, 0x20, (int)(0xD0 * ak)));
        gfx_rrect(bx + 14, fy, bw - 28, fh, (bw - 28) / 2, COLOR_ALPHA(THEME_TEXT, (int)(0x30 * ak)));
        if (v > 0.01f) gfx_rrect(bx + 14, fy + fh * (1 - v), bw - 28, fh * v, (bw - 28) / 2, COLOR_ALPHA(THEME_TEXT, (int)(0xF0 * ak)));
        Color ic = COLOR_ALPHA(THEME_TEXT, (int)(255 * ak));
        if (what == 1) {   /* a sun */
            float cx = bx + bw / 2, cy = by + 34;
            gfx_circle(cx, cy, 8, ic);
            for (int k = 0; k < 8; k++) { float a = k * 3.14159265f / 4, c = cosf(a), sn = sinf(a); gfx_line(cx + c * 12, cy + sn * 12, cx + c * 17, cy + sn * 17, 3, ic); }
        } else ui_icon(ICON_AUDIO, bx + bw / 2, by + 32, 28, ic);
        char t[8]; snprintf(t, sizeof t, "%d", (int)(v * 100 + 0.5f));
        font_draw_center(t, bx + bw / 2, by + bh - 52, FONT_XS | FONT_B, COLOR_ALPHA(THEME_TEXT, (int)(255 * ak)));
        if (p->tch_adjust) p->adj_last = p->tch_adjust;
        app_redraw();
    }
    float flash = p->play_flash - g_app.time;
    if (flash > 0 && !p->tch_seeking) {
        float a = flash / 0.6f;
        gfx_circle(x + w / 2, y + h / 2, 70, RGBA(0, 0, 0, (int)(0x90 * a)));
        ui_icon(p->flash_icon, x + w / 2 + (p->flash_icon == ICON_PLAY ? 4 : 0), y + h / 2, 56, COLOR_ALPHA(THEME_TEXT, (int)(255 * a)));
        app_redraw();
    }
    (void)vi;
}

/* Skip intro / recap, the next episode, or "Still watching?": one wide button. */
static int ph_offer(Player *p, const VideoInfo *vi, float x, float y, float w, float hh)
{
    const SkipSeg *sk = g_app.skip_mode == 0 ? skippable(p, vi) : NULL;
    if (!sk && !(p->up_next_shown && p->next_video_id) && !p->still_asking) return 0;
    const char *t = p->still_asking ? "Still watching?  Tap to go on" : sk ? (sk->kind == SKIP_RECAP ? "Skip recap" : "Skip intro") : next_ready(p) ? "Next episode" : "Next episode (preparing\xE2\x80\xA6)";
    gfx_rrect(x, y, w, hh, hh / 2, THEME_ACCENT);
    ui_icon(ICON_NEXT, x + hh * 0.62f, y + hh / 2, 32, THEME_ON_ACCENT);
    font_draw_fit(t, x + hh * 1.05f, y + (hh - font_height(FONT_M | FONT_B)) / 2, FONT_M | FONT_B, THEME_ON_ACCENT, w - hh * 1.3f);
    ui_hit(x, y, w, hh, p->still_asking ? PHH_STILL_ON : sk ? PHH_SKIP : PHH_NEXT);
    return 1;
}

/* play/pause in the middle, 10 s either side, the episodes either side of those */
static void ph_transport(Player *p, const VideoInfo *vi, float cx, float cy, float big, float mid, float gap)
{
    ph_round(vi->state == VIDEO_PLAYING ? ICON_PAUSE : ICON_PLAY, NULL, cx, cy, big, 1, PHH_PLAY);
    ph_round(ICON_PLAY, "-10", cx - gap, cy, mid, 0, PHH_BACK10);
    ph_round(ICON_PLAY, "+10", cx + gap, cy, mid, 0, PHH_FWD10);
    if (p->prev_video_id) ph_round(ICON_PREV, NULL, cx - 2 * gap + mid * 0.3f, cy, mid * 0.8f, 0, PHH_PREV_EP);
    if (p->next_video_id) ph_round(ICON_NEXT, NULL, cx + 2 * gap - mid * 0.3f, cy, mid * 0.8f, 0, PHH_NEXT_EP);
}

/* Sideways on a phone: the picture is the whole screen; the controls over it. */
static void full_draw(Player *p, const VideoInfo *vi)
{
    float W = SNTV_UI_W, H = SNTV_UI_H;
    p->ph_pic[0] = 0; p->ph_pic[1] = 0; p->ph_pic[2] = W; p->ph_pic[3] = H;
    int rec = recovering(p, vi), shown = fs_shown(p, vi), failed = error_final(p, vi);
    if (failed) {
        gfx_rect(0, 0, W, H, COLOR_ALPHA(THEME_BG, 0xD8));
        font_draw_center("This source can't be played", W / 2, H / 2 - 120, FONT_L | FONT_B, THEME_TEXT);
        font_draw_wrapped(p->tb_error[0] ? p->tb_error : vi->error, W / 2 - 500, H / 2 - 50, FONT_S, THEME_TEXT_DIM, 1000, 2);
        ui_pill("Other sources", 1, ICON_SWAP, W / 2 - 200, H / 2 + 60, 80, PILL_PRIMARY, 0);
        ui_hit(W / 2 - 200, H / 2 + 60, 400, 80, PHH_SHEET + PHB_SOURCES);
    } else if (rec || vi->state == VIDEO_LOADING || vi->buffering) {
        ui_spinner(W / 2, H / 2, 34);
        app_redraw();
    }
    ph_feedback(p, vi, 0, 0, W, H);
    p->fs_barr[2] = 0;
    float sheet_x = p->ph_sheet ? W - 820 : W;
    if (!shown) {   /* only the skip button stays, at the bottom right */
        ph_offer(p, vi, W - 80 - 520, H - 150, 520, 96);
        return;
    }
    if (g_app.time < p->fs_until) app_redraw();   /* to hide them on time */
    if (!failed) {
        gfx_gradient_v(0, 0, W, 240, RGBA(0, 0, 0, 0xB8), RGBA(0, 0, 0, 0));
        gfx_gradient_v(0, H - 320, W, 320, RGBA(0, 0, 0, 0), RGBA(0, 0, 0, 0xC8));
    }
    /* top: back and the title; the lists at the right */
    ph_round(ICON_CHEVRON_LEFT, NULL, 96, 84, 44, 0, PHH_BACK);
    float tw = sheet_x - 180 - 7 * 112;
    font_draw_fit(p->meta.name ? p->meta.name : "", 170, 48, FONT_M | FONT_B, THEME_TEXT, tw);
    if (p->title && *p->title && (!p->meta.name || strcmp(p->title, p->meta.name))) font_draw_fit(p->title, 170, 94, FONT_XS, THEME_TEXT_DIM, tw);
    static const Icon BI[PHB_COUNT] = { ICON_SUBS, ICON_AUDIO, ICON_SPEED, ICON_SWAP, ICON_LIST, ICON_PIP };
    static const int BS[PHB_COUNT] = { PH_SUBS, PH_AUDIO, PH_SPEED, PH_SOURCES, PH_EPISODES, 0 };
    float bx = sheet_x - 96;
    for (int i = PHB_COUNT - 1; i >= -1; i--) {   /* right to left: picture in picture, episodes, sources, speed, audio, subtitles, aspect */
        if (i == PHB_EPISODES && !ph_has_eps(p)) continue;
#ifndef __ANDROID__
        if (i == PHB_PIP) continue;   /* Android's alone */
#endif
        int on = i >= 0 && BS[i] && p->ph_sheet == BS[i];
        gfx_circle(bx, 84, 44, on ? COLOR_ALPHA(THEME_TEXT, 0x40) : RGBA(0, 0, 0, 0x60));
        ui_icon(i < 0 ? ICON_ASPECT : BI[i], bx, 84, 38, on ? THEME_ACCENT : THEME_TEXT);
        if (i == PHB_SPEED && p->rate_idx != RATE_NORMAL) { char sp[16]; snprintf(sp, sizeof sp, "%gx", RATES[p->rate_idx]); font_draw_center(sp, bx, 136, FONT_XS | FONT_B, THEME_ACCENT); }
        ui_hit(bx - 54, 30, 108, 108, i < 0 ? PHH_ASPECT : PHH_SHEET + i);
        bx -= 112;
    }
    if (!failed) {
        float cx = sheet_x / 2, cy = H / 2;
        ph_transport(p, vi, cx, cy, 84, 60, sheet_x < W ? 190 : 240);
        /* the bar and the times along the bottom */
        double pos = p->seek_commit > 0 ? p->seek_target : vi->position, dur = ph_dur(p, vi);
        char a[24], b[24];
        ph_time(pos, a, sizeof a);
        if (dur > 0) { char t[24]; ph_time(dur - pos, t, sizeof t); snprintf(b, sizeof b, "-%.20s", t); } else b[0] = 0;
        float lx = 80, rx = sheet_x - 80, by = H - 70;
        font_draw(a, lx, by - font_height(FONT_S) / 2, FONT_S | FONT_B, THEME_TEXT);
        font_draw(b, rx - font_measure(b, FONT_S | FONT_B), by - font_height(FONT_S) / 2, FONT_S | FONT_B, THEME_TEXT);
        float bl = lx + font_measure("00:00:00", FONT_S | FONT_B) + 30, br = rx - font_measure("-00:00:00", FONT_S | FONT_B) - 30;
        ph_bar(p, vi, bl, by, br - bl, p->fs_barr);
        ph_offer(p, vi, rx - 520, by - 160, 520, 92);
    }
    if (p->ph_sheet) ph_sheet_draw(p, W - 800, 20, 760, H - 20, 1);
}

static int phone_tap(Player *p, int id)
{
    VideoInfo vi;
    video_info(&vi);
    p->fs_until = g_app.time + 4.5f;   /* the controls stay while they're used */
    if (id >= PHH_ROW && id < PHH_ROW + PH_ROWS) {
        p->ph_focus = id - PHH_ROW;
        if (g_touch_aim) return 1;
        PhRow r[PH_ROWS];
        int n = ph_rows(p, r);
        if (p->ph_focus < n) ph_pick(p, &r[p->ph_focus]);
        app_redraw();
        return 1;
    }
    if (g_touch_aim) return 1;
    if (id >= PHH_SHEET && id < PHH_SHEET + PHB_COUNT) {
        int i = id - PHH_SHEET;
        if (i == PHB_PIP) {   /* the controls go first, so the small window shows the picture alone */
            p->ph_sheet = PH_NONE; p->fs_until = 0;
            if (video_pip_enter()) LOGI("player: picture in picture");
            else app_toast("This phone can't show picture in picture");
            return 1;
        }
        static const int BS[PHB_COUNT] = { PH_SUBS, PH_AUDIO, PH_SPEED, PH_SOURCES, PH_EPISODES, 0 };
        ph_open(p, BS[i]);
        return 1;
    }
    switch (id) {
    case PHH_BACK: return -1;   /* as Back */
    case PHH_PLAY: toggle_play(p, &vi); return 1;
    case PHH_BACK10: touch_seek(p, (p->seek_commit > 0 ? p->seek_target : vi.position) - 10, 0.4f); return 1;
    case PHH_FWD10: touch_seek(p, (p->seek_commit > 0 ? p->seek_target : vi.position) + 10, 0.4f); return 1;
    case PHH_PREV_EP: ph_neighbour(p, -1); return 1;
    case PHH_NEXT_EP: ph_neighbour(p, 1); return 1;
    case PHH_ASPECT: p->aspect = (p->aspect + 1) % ASPECT_COUNT; apply_aspect(p); app_toast("Aspect: %s", ASPECT_NAMES[p->aspect]); return 1;
    case PHH_CLOSE: p->ph_sheet = PH_NONE; return 1;
    case PHH_SKIP: { const SkipSeg *sk = skippable(p, &vi); if (sk) do_skip(p, sk); return 1; }
    case PHH_NEXT: if (next_ready(p)) go_up_next(p); else p->next_wanted = 1; return 1;
    case PHH_STILL_ON: return -3;   /* as OK on "Still watching?" */
    default: return 1;
    }
}

static int key(Screen *s, KeyAction a);
/* Touch on a phone: what was tapped (the picture itself is drag()'s). On a tablet the TV's
 * player, where a tap anywhere is OK. */
static int tap(Screen *s, int id)
{
    Player *p = s->st;
    if (!gfx_handheld() || p->pip) return 0;
    int r = phone_tap(p, id);
    if (r == -1) { key(s, KEY_BACK); return 1; }
    if (r == -3) { key(s, KEY_OK); return 1; }
    app_redraw();
    return 1;
}

/* A tap on the picture, after a moment to see whether a second follows: upright it plays or
 * pauses, sideways it shows or hides the controls. Two on a side skip 10 s, each more 10 s further. */
static void picture_tap(Player *p, float x)
{
    VideoInfo vi;
    video_info(&vi);
    float l = p->ph_pic[0], w = p->ph_pic[2];
    int side = x < l + w * 0.38f ? -1 : x > l + w * 0.62f ? 1 : 0;
    int again = side && p->ripple_side == side && g_app.time - p->ripple_at < 0.7f;   /* still skipping that way */
    if (side && (again || (p->tap_pending && p->tap_side == side && g_app.time - p->tap_at < 0.32f))) {
        p->tap_pending = 0;
        p->ripple_secs = again ? p->ripple_secs + 10 : 10;
        p->ripple_side = side; p->ripple_at = g_app.time;
        double from = p->seek_commit > 0 ? p->seek_target : vi.position;
        touch_seek(p, from + side * 10, 0.6f);
        LOGI("player: double tap: %s%d s", side < 0 ? "-" : "+", p->ripple_secs);
        return;
    }
    p->tap_pending = 1; p->tap_at = g_app.time; p->tap_side = side;
}

static void picture_tap_settle(Player *p)   /* update(): a single tap that stayed single */
{
    if (!p->tap_pending || g_app.time - p->tap_at < 0.32f) return;
    p->tap_pending = 0;
    VideoInfo vi;
    video_info(&vi);
    if (fs_shown(p, &vi) && vi.state == VIDEO_PLAYING && !p->ph_sheet) p->fs_until = 0;
    else { p->fs_until = g_app.time + 4.5f; if (p->ph_sheet && vi.state == VIDEO_PLAYING) p->ph_sheet = PH_NONE; }
    app_redraw();
}

/* Brightness and volume as they are now: the device's where it says, else what was set here. */
static float ph_bright(Player *p)
{
    if (p->bright >= 0) return p->bright;
    float b = video_brightness();
    return b >= 0 ? b : 1;
}
static float ph_vol(Player *p)
{
    float v = video_device_volume();
    return v >= 0 ? v : p->vol >= 0 ? p->vol : 1;
}
static void ph_adjust(Player *p, int what, float v)
{
    if (v < 0) v = 0;
    if (v > 1) v = 1;
    p->adj_at = g_app.time;
    if (what == 1) {
        p->bright = v;
        p->soft_dim = !video_set_brightness(v);   /* no screen control: a veil over the picture (darker only) */
    } else {
        p->vol = v;
        if (!video_set_device_volume(v)) video_set_volume((int)(v * 100 + 0.5f));
    }
    app_redraw();
}

static int in_rect(const float *r, float x, float y) { return r[2] > 0 && x >= r[0] && x < r[0] + r[2] && y >= r[1] && y < r[1] + r[3]; }

/* A finger on the player (a phone): along the bar it scrubs, across the picture it seeks,
 * up or down the picture it goes to full screen or back; one that barely moved is a tap. */
static int drag(Screen *s, int phase, float x, float y)
{
    Player *p = s->st;
    if (!gfx_handheld() || p->pip) return 0;
    VideoInfo vi;
    video_info(&vi);
    double dur = ph_dur(p, &vi);
    float *bar = p->fs_barr;
    if (phase == 0) {
        p->tch = 0; p->tch_seeking = 0; p->tch_moved = 0;
        if (error_final(p, &vi)) return 0;
        int id;
        if (dur > 0 && in_rect(bar, x, y)) p->tch = 1;   /* the bar: the knob jumps to the finger */
        else if (ui_hit_find(x, y, s, &id)) return 0;   /* a button or the open list */
        else if (in_rect(p->ph_pic, x, y)) p->tch = 2;
        else return 0;
        p->tch_x0 = x; p->tch_y0 = y;
        p->tch_pos0 = p->seek_commit > 0 ? p->seek_target : vi.position;
        if (p->tch == 1) { float bl = bar[0] + 30, bw = bar[2] - 60; touch_seek(p, dur * (x - bl) / bw, 60); }
        return 1;
    }
    if (!p->tch) return 0;
    float dx = x - p->tch_x0, dy = y - p->tch_y0;
    if (fabsf(dx) > 24 || fabsf(dy) > 24) p->tch_moved = 1;
    if (p->tch == 1) {
        float bl = bar[0] + 30, bw = bar[2] - 60;
        double t = dur * (x - bl) / bw;
        if (phase == 1) touch_seek(p, t, 60);
        else { touch_seek(p, t, 0.01f); LOGI("player: scrubbed to %.0f s", p->seek_target); p->tch = 0; }
        return 1;
    }
    if (phase == 1) {
        if (!p->tch_seeking && !p->tch_adjust && dur > 0 && fabsf(dx) > 40 && fabsf(dx) > fabsf(dy) * 1.2f) { p->tch_seeking = 1; p->tch_x0 = x; }
        if (!p->tch_seeking && !p->tch_adjust && fabsf(dy) > 40 && fabsf(dy) > fabsf(dx) * 1.2f) {   /* up or down: the left half's brightness, the right half's volume */
            p->tch_adjust = p->tch_x0 < p->ph_pic[0] + p->ph_pic[2] / 2 ? 1 : 2;
            p->tch_level0 = p->tch_adjust == 1 ? ph_bright(p) : ph_vol(p);
            p->tch_y0 = y;
        }
        if (p->tch_seeking) touch_seek(p, p->tch_pos0 + (x - p->tch_x0) / p->ph_pic[2] * PH_SWIPE_SECS, 60);
        if (p->tch_adjust) ph_adjust(p, p->tch_adjust, p->tch_level0 - (y - p->tch_y0) / (p->ph_pic[3] * 0.8f));
        return 1;
    }
    /* lifted */
    if (p->tch_seeking) { touch_seek(p, p->seek_target, 0.01f); LOGI("player: dragged to %.0f s", p->seek_target); }
    else if (p->tch_adjust) LOGI("player: %s %d%%", p->tch_adjust == 1 ? "brightness" : "volume", (int)((p->tch_adjust == 1 ? p->bright : p->vol) * 100 + 0.5f));
    else if (!p->tch_moved) picture_tap(p, x);
    p->tch = 0; p->tch_seeking = 0; p->tch_adjust = 0;
    return 1;
}

static int key(Screen *s, KeyAction a)
{
    Player *p = s->st;
    VideoInfo vi;
    video_info(&vi);
    if (gfx_handheld() && !p->pip && p->ph_sheet) {   /* the open list takes Up, Down, OK and Back */
        PhRow r[PH_ROWS];
        int n = ph_rows(p, r);
        app_redraw();
        if (a == KEY_BACK) { p->ph_sheet = PH_NONE; return 1; }
        if (a == KEY_UP) { if (p->ph_focus > 0) p->ph_focus--; return 1; }
        if (a == KEY_DOWN) { if (p->ph_focus < n - 1) p->ph_focus++; return 1; }
        if (a == KEY_OK) { if (p->ph_focus < n) ph_pick(p, &r[p->ph_focus]); return 1; }
    }
    if (p->still_asking) {   /* "Still watching?": OK goes on, Back stops; nothing else */
        if (a == KEY_OK || a == KEY_PLAY || a == KEY_PLAY_PAUSE) { p->still_asking = 0; g_auto_streak = 0; LOGI("player: still watching: on to the next episode"); go_up_next(p); }
        else if (a == KEY_BACK || a == KEY_STOP) { p->still_asking = 0; p->finished = 1; report(p, 1); app_pop(); }
        return 1;
    }
    if (p->lighter_to >= 0 && !p->menu && p->seek_commit <= 0 && !(p->up_next_shown && p->next_video_id) && (a == KEY_OK || a == KEY_BACK)) {   /* the lighter source on offer */
        int to = p->lighter_to;
        p->lighter_to = -1;
        if (a == KEY_OK) { LOGI("player: switching to the lighter source %d", to + 1); app_toast("Switching to %dp", p->cands[to].res); keep_choices(p); start_candidate(p, to, resume_spot(p, &vi)); }
        app_redraw();
        return 1;
    }
    if (p->rest_t > 0.5f && !p->menu) {   /* leaning back: OK plays, anything else brings the controls back */
        if (a == KEY_OK || a == KEY_PLAY_PAUSE || a == KEY_PLAY) { LOGI("player: OK plays on from the lean-back screen"); p->bar_id = BTN_PLAY; video_play(); p->flash_icon = ICON_PLAY; p->play_flash = g_app.time + 0.6f; }
        show_overlay(p);
        return 1;
    }
    if (p->scenes_open && !p->menu) return scenes_key(p, a, &vi);
    if (p->cast_open && !p->menu) {
        const TmdbInfo *tm;
        int n = cast_n(p, &tm);
        if (a == KEY_LEFT && p->cast_focus > 0) p->cast_focus--;
        else if (a == KEY_RIGHT && p->cast_focus + 1 < n) p->cast_focus++;
        else if (a == KEY_INFO || a == KEY_BACK || a == KEY_OK || a == KEY_DOWN || !n) p->cast_open = 0;
        else if (a != KEY_LEFT && a != KEY_RIGHT && a != KEY_UP) { p->cast_open = 0; goto keys; }
        app_redraw();
        return 1;
    }
keys:
    if (p->catchup_until > g_app.time && !p->menu && (a == KEY_OK || a == KEY_BACK)) { p->catchup_until = 0; app_redraw(); return 1; }   /* puts "Previously" away */
    if (p->menu == MENU_SOURCES) return sources_key(p, a);
    if (p->menu == MENU_SUBS) return subs_key(p, a);
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
            if (p->menu == MENU_SOURCES && p->menu_focus == p->n_cands) { if (p->more_state == 2) browse_sources(p); else find_more_sources(p); return 1; }
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
    if (a == KEY_OK && p->up_next_shown && p->next_video_id) {   /* the card's Play now; still preparing: it starts once ready */
        if (next_ready(p)) go_up_next(p);
        else if (!p->next_wanted) { p->next_wanted = 1; LOGI("player: next episode asked for while it's prepared"); app_redraw(); }
        return 1;
    }
    /* Skip: while its button shows, OK skips (controls up or not, playing or paused); the
     * controls' buttons have OK only once Down has gone into them. Play/Pause still pauses. */
    {
        const SkipSeg *sk = g_app.skip_mode == 0 ? skippable(p, &vi) : NULL;
        if (sk && a == KEY_OK) { do_skip(p, sk); show_overlay(p); return 1; }
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
        if (vi.state == VIDEO_PLAYING) { video_pause(); p->viewer_paused = 1; } else { video_play(); p->flash_icon = ICON_PLAY; p->play_flash = g_app.time + 0.6f; }
        show_overlay(p);
        return 1;
    case KEY_PLAY: video_play(); show_overlay(p); return 1;
    case KEY_PAUSE: video_pause(); p->viewer_paused = 1; show_overlay(p); return 1;
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
    case KEY_INFO:
        if (!open_cast(p)) show_overlay(p);
        return 1;
    case KEY_MENU: show_overlay(p); return 1;
    case KEY_GREEN: if (p->next_video_id) go_up_next(p); return 1;
    case KEY_RED: p->stats = !p->stats; app_redraw(); return 1;
    case KEY_BLUE: instant_replay(p, vi.position); return 1;
    case KEY_YELLOW: if (p->cur + 1 < p->n_cands) { app_toast("Switching source"); keep_choices(p); start_candidate(p, p->cur + 1, resume_spot(p, &vi)); } return 1;
    case KEY_BACK:
        if (g_app.pip_enabled && !p->trailer && !gfx_handheld() &&   /* a phone: no corner player (yet) */ (vi.state == VIDEO_PLAYING || vi.state == VIDEO_PAUSED || vi.state == VIDEO_LOADING) && g_app.depth > 1) { app_pip_enter(); return 1; }
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

void player_save_progress_quiet(void)   /* an overlay over the app: saved, but Trakt is not told it stopped */
{
    if (g_live) report_ex(g_live, 1, 0);
}

static void destroy(Screen *s)
{
    Player *p = s->st;
    if (!p->start_timed) perf_start_cancel();   /* left before its first frame: not a start */
    if (p->bright >= 0) video_set_brightness(-1);   /* the screen's brightness is the system's again */
    video_pip_allow(0);
    relay_buffer_close();   /* its memory goes with it */
    report(p, 1);   /* the app closing with the film on: the last position still counts */
    streams_lookup_cancel();
    if (p->cur < p->n_cands) torrent_engine_drop(p->cands[p->cur].url);
    if (g_live == p) g_live = NULL;
    p->gen++;
    video_stop();
    subs_free(&p->track);
    subs_free(&p->track2);
    subs_free(&p->ref);
    for (int k = 0; k < 6; k++) subs_free(&p->bm_track[k]);
    xl_stop(p);
    free(p->probe);
    for (int i = 0; i < p->n_addon_subs; i++) { free(p->addon_subs[i].url); free(p->addon_subs[i].lang); free(p->addon_subs[i].addon); free(p->addon_subs[i].release); }
    for (int r = 0; r < 2; r++) { free(p->ready[r].url); free(p->ready[r].link); }
    free(p->sg);
    play_candidates_free(p->cands, p->n_cands);
    meta_preview_free(&p->meta);
    free(p->video_id); free(p->title); free(p->next_video_id); free(p->next_title); free(p->prev_video_id); free(p->prev_title);
    free(p);
    free(s);
}

/* Coming back after a while: the film resumes a little earlier (10 s after a day, 30 s
 * after a week) so the moment comes back with it, and a series opens with a card about
 * the episode before this one (after two days). SNTV_AWAY_S (tests) stands in for the gap. */
/* Blue: "what did they say?" Ten seconds back, with subtitles on for those ten seconds
 * when they were off; they go again once the playhead passes where Blue was pressed. */
static void instant_replay(Player *p, double pos)
{
    if (p->active_addon_sub < 0 && p->embedded_text_sel < 0 && !p->replay_subs) {
        const char *lang = g_app.pref_subs[0] ? g_app.pref_subs : p->audio_lang_known ? p->audio_lang : "en";
        p->replay_subs = pick_subs(p, lang, SUBS_SRC_ANY) || (p->audio_lang_known && pick_subs(p, p->audio_lang, SUBS_SRC_ANY));
    }
    if (pos > p->replay_until) p->replay_until = pos;   /* pressed again: back to where the first press was */
    video_seek(pos > 10 ? pos - 10 : 0);
    LOGI("player: replay from %.0f s%s", pos, p->replay_subs ? " with subtitles" : "");
    app_toast(p->replay_subs ? "Replaying with subtitles" : "Replaying the last 10 seconds");
    show_overlay(p);
}

static int night_now(void)
{
    if (g_app.night_mode != 1) return g_app.night_mode == 2;
    time_t t = time(NULL);
    struct tm tm;
    localtime_r(&t, &tm);
    return tm.tm_hour >= 23 || tm.tm_hour < 6;
}

/* Night mode: quieter and larger subtitles. Checked each minute, and the volume again
 * after every load (each starts at full volume). */
static void night_tick(Player *p, const VideoInfo *vi)
{
    long minute = (long)(time(NULL) / 60);
    if (minute != p->night_minute) {
        p->night_minute = minute;
        int n = !p->trailer && night_now();
        if (n != p->night) {
            p->night = n;
            p->night_load = 0;
            push_sub_style(p);
            LOGI("player: night mode %s", n ? "on" : "off");
            if (p->started) app_toast(n ? "Night mode: quieter, with larger subtitles" : "Night mode off");
        }
    }
    if ((p->night || p->night_applied) && p->night_load != video_load_count() && vi->state != VIDEO_IDLE) {
        p->night_load = video_load_count();
        p->night_applied = p->night;
        video_set_volume(p->night ? 60 : 100);
    }
}

static double welcome_back(Player *p, double start)
{
    const LibraryItem *li = app_library_find(p->meta.id);
    const char *fake = getenv("SNTV_AWAY_S");
    if (fake) p->away_s = atof(fake);
    else if (li && li->last_watched_ms > 0) p->away_s = (double)time(NULL) - li->last_watched_ms / 1000.0;
    double back = p->away_s >= 7 * 86400 ? 30 : p->away_s >= 86400 ? 10 : 0;
    if (back && start > back + 5) {
        LOGI("player: away %.0f days, resuming %.0f s earlier", p->away_s / 86400, back);
        start -= back;
    }
    if (p->away_s >= 2 * 86400 && p->prev_video_id) {
        int n = 0;
        const MetaVideo *eps = app_videos(p->meta.id, &n);
        for (int i = 0; i < n; i++) {
            if (strcmp(eps[i].id, p->prev_video_id) || !eps[i].overview || !*eps[i].overview) continue;
            snprintf(p->catchup_head, sizeof p->catchup_head, "Previously  \xC2\xB7  %s", p->prev_title ? p->prev_title : "the last episode");
            snprintf(p->catchup_text, sizeof p->catchup_text, "%s", eps[i].overview);
            p->catchup_until = -1;   /* shown once the picture is up */
            LOGI("player: previously card for %s", p->prev_video_id);
            break;
        }
    }
    return start;
}

Screen *screen_player_create(const MetaPreview *meta, const char *video_id, const char *title,
                             PlayCandidate *cands, int n_cands, int start_index, double start_s,
                             const char *next_video_id, const char *next_title)
{
    Screen *s = calloc(1, sizeof *s);
    Player *p = calloc(1, sizeof *p);
    p->bright = p->vol = -1;
    p->veil = 1;
    p->bar_id = BTN_PLAY;
    p->lighter_to = -1;
    p->jumped_at = -100;
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
    s->update = update; s->draw = draw; s->key = key; s->tap = tap; s->drag = drag; s->destroy = destroy; s->phone = 0;   /* a phone turns sideways to play */
    if (!p->trailer) { start_s = welcome_back(p, start_s); marks_load(p); }
    if (!p->trailer && g_app.advisory) advisory_fetch(meta->id);
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
double player_resume_point(Screen *s)
{
    VideoInfo vi;
    video_info(&vi);
    return resume_spot(s->st, &vi);
}

void player_reload_at(Screen *s, double position_s, int paused) { Player *p = s->st; p->pause_when_started = paused; start_candidate(p, p->cur, position_s); }
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

int player_holds_screen(void)
{
    if (!g_live || g_live->finished) return 0;
    VideoInfo vi;
    video_info(&vi);
    if (vi.buffering || vi.state == VIDEO_PLAYING || vi.state == VIDEO_LOADING) return 1;
    if (vi.state == VIDEO_PAUSED) return !g_live->viewer_paused && !video_paused_on_purpose();   /* a stall the TV reports as a pause; not the TV's Home or a break pausing it */
    if (vi.state == VIDEO_ENDED) return 0;
    /* between sources (TorBox opening the next one, the addons asked again, reconnecting):
     * held; the final error card is not */
    return g_live->reask_at != 0 || !error_final(g_live, &vi);
}
