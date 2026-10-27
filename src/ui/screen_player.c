/* Player: transparent hole for the platform video plane, overlay controls,
 * addon + embedded subtitles, audio/subtitle menu, auto-fallback across stream
 * candidates, intro/recap skipping, Up Next at the credits, and progress sync to
 * the Stremio library. */
#include "../relay.h"
#include "../mkvsubs.h"
#include "../quality.h"
#include "../perfstat.h"
#include "../playback.h"
#include "../app.h"
#include "../memwatch.h"
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
#include <ctype.h>
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
/* The touch controls over the picture: a phone or tablet's, and a computer's by the mouse
 * (it has no remote: play, seek, tracks and sources are buttons there too). */
#ifdef SNTV_DESKTOP_PLAYER
#define TOUCH_UI 1
#else
#define TOUCH_UI gfx_handheld()
#endif

static const char *const ASPECT_NAMES[ASPECT_COUNT] = { "Fit", "Fill the screen", "Zoom" };
static const double RATES[] = { 0.5, 0.75, 1.0, 1.25, 1.5, 1.75, 2.0 };
enum { N_RATES = 7, RATE_NORMAL = 2 };

typedef struct AddonSub { char *url, *lang, *addon, *release; int matched, sync_failed, load_failed; int fit; char fit_why[48]; int name; } AddonSub;   /* name: release-name match 0..100 (OpenSubtitles) */   /* fit: 0 not checked, else 1..101 */

typedef struct Player Player;
typedef struct { Player *owner; unsigned gen; int addon; int slot; int hashed; unsigned attempt; unsigned seq; int idx; } SubReq;   /* attempt: 0, or the source start it is about; seq: which subtitle download, for files */

struct Player {
    MetaPreview meta;
    char *video_id, *title;
    PlayCandidate *cands; int n_cands, cur;
    int tries;
    uint64_t tried;                         /* sources already started here (bit per index, first 64) */
    struct { char name[40]; int fails, down; } svc[8]; int n_svc;   /* services whose sources failed to start (TorBox, a debrid, a site) */
    int started;                            /* this source has shown a picture (the clock moved) */
    float veil;                             /* the title's artwork over the screen until the first picture, then fading */
    unsigned sub_sig; float drawn_at;       /* the subtitle lines last drawn, and when: a bare picture draws only when they change */
    int bridge; float bridge_t0;            /* on from the last episode: its still stands in for the loading card */
    int bridge_from; float bridge_rect[4];  /* ... grown out of the Up Next card's picture, where it was */
    float card_thumb[4], card_at;           /* where this player's Up Next card drew the next one's picture, and when */
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
    float fs_k, fs_kt;        /* how far they have come in (0..1), eased, and when it last moved */
    int ph_locked; float pinch_d, pinch_at, pinch_hint_at;   /* a phone: touches locked out (but the unlock); a pinch under way; the pinch hint shown at */
    float dk_nk, dk_nkt, dk_nke;   /* a computer: the credits' Up Next screen, how far in (dk_nke eased) */
    float dk_rk, dk_rkt;      /* a computer: the "You're watching" card a while after pausing, faded in this far */
    float dk_volr[4], dk_unmuted, dk_hk, dk_hkt;   /* a computer: where the volume slider is; the volume before muting; how far the timeline has grown under the pointer */
    float lb_k, lb_kt, lb_bar, lb_drawn; int lb_side, lb_logged, lb_none, lb_aside_logged;   /* living letterbox: how far it has faded in, the bar it fills (1 above and below, 2 at the sides) */
    char lb_last[2048];       /* the last frame it had, shown while the next one comes */
    char lb_prev[2048]; float lb_swap_at, lb_fade;   /* the one before, fading out as the last fades in (since then, over this long) */
    char lb_ahead[2048]; float lb_ahead_at;   /* the frame of a cut ahead being got ready now (asked then): one at a time */
    char lb_asked[2048]; float lb_ask_at;     /* the frame the bars wanted and did not have yet (asked then) */
    int lb_slow_n, lb_slow, lb_ready_n;       /* frames that did not come; 1: none come here, the artwork's colour instead; cuts made ready */
    float lb_took;                            /* how long a frame takes to come here (smoothed; 0 none yet) */
    int lb_late_n;                            /* frames that came after their cut (not shown) */
    double lb_next_t;                         /* where the bars change next (a redraw then) */
    float lb_ar; int lb_ar_cur;               /* the picture's shape the frames showed (bars baked in), for source lb_ar_cur - 1 */
    double lb_keys[64]; int lb_nkeys, lb_keys_state; double lb_keys_from;   /* the film's cuts ahead (keyframes): 0 not asked, 1 asking, 2 known, 3 none */
    double lb_shown;                       /* the time of the frame in the bars (-1 none) */
    int lb_cuts_logged;
    float ov_k, ov_kt;        /* the TV overlay the same way: it fades in, the bar rising into place */
    int tch, tch_seeking, tch_moved;   /* a finger: 1 on the bar, 2 on the picture; seeking with it; it moved */
    int tch_fast; float tch_at;        /* held still on the picture: playing at double speed until it lifts; when it went down */
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
    SubTrack emb; int emb_app, emb_for, emb_num, emb_taken, emb_tried, emb_buffered;   /* the film's own text subtitles, drawn by the app from the buffer (emb_app) instead of the TV */
    float emb_off_at; int emb_off_n;   /* the TV's drawing turned off again a few times after the takeover: its pipeline applies a selection late */
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
    int length_checked;                     /* this source's length looked at once it played (an addon's error clip) */
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
    PlaybackWatch watch;
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
    int file_bad;             /* the current source failed for its file (decoding, audio, an error clip), not its link: no same-file retry */
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
    float pp_since; int pp_logged;          /* the pipeline paused by itself (LG reports a stall so): since when, 0 none */
    int drain_relay;                        /* the buffer ahead counted in the last drain sample */
    PlayCandidate tb_wait; int tb_waiting;  /* TorBox has the file again while P2P plays well: switched at the next seek or stall */

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
static unsigned g_player_generation;
static void desk_skip_icon(float cx, float cy, float r, int dir, Color c, int fs);
#ifdef SNTV_DESKTOP_PLAYER
static float desk_next_k(Player *p);
static void desk_next_rect(float k, float *r);
#endif

static void show_overlay(Player *p) { p->overlay_until = g_app.time + 4.0f; app_redraw(); }
static void player_seek(double position)
{
    if (g_live) g_live->watch.valid = 0;
    VideoInfo vi; video_info(&vi);
    perf_playback_seek(vi.seek_version, vi.seek_supported && vi.first_frame);
    video_seek(position);
}
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
static int subs_timing_supported(const Player *p);
static int playback_needs_bandwidth(const VideoInfo *vi)
{
    if (vi->state == VIDEO_LOADING) return 1;
    if (vi->state != VIDEO_PLAYING) return 0;
    if (vi->buffering) return 1;
    if (!g_live || g_live->drain_n < 2) return 0;
    int last = g_live->drain_n - 1;
    double span = g_live->drain_at[last] - g_live->drain_at[0];
    return span > 0 && g_live->drain_v[last] < 15
        && (g_live->drain_v[last] - g_live->drain_v[0]) / span < -0.2;
}
static int cast_n(const Player *p, const TmdbInfo **tm);
static int open_cast(Player *p);
static const char *emb_lang(const Player *p, const VideoTrack *tr, int i, int n);

static int subs_key(Player *p, KeyAction a);
static void full_draw(Player *p, const VideoInfo *vi);
static void full_draw_loading(Player *p);
static void picture_tap_settle(Player *p);
static void ph_hold_fast(Player *p, const VideoInfo *vi);
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
    int through = playback_watch_through(&p->watch, vi.duration);
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
static int pick_subs(Player *p, const char *lang, SubsSource prefer);

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
        if (q->slot) p->active_addon_sub2 = -1; else p->active_addon_sub = -1;
        if (!q->slot && q->idx >= 0 && q->idx < p->n_addon_subs) {   /* as with a source that fails: the next best in its language */
            AddonSub *bad = &p->addon_subs[q->idx];
            bad->load_failed = 1;
            char lang[16]; snprintf(lang, sizeof lang, "%s", bad->lang ? bad->lang : "");
            LOGI("subs: a %s file from %s did not load (%s); trying the next", lang, bad->addon ? bad->addon : "an addon", r->status ? "not a subtitle file" : r->error[0] ? r->error : "no answer");
            if (lang[0] && pick_subs(p, lang, SUBS_SRC_ADDON)) {
                LOGI("subs: %s instead", p->active_addon_sub >= 0 ? "another file" : "the video's own track");
                app_toast("That subtitle file did not load: using another %s one", lang_display_english(lang));
            } else app_toast("Could not load that subtitle file, and there is no other %s one", lang_display_english(lang));
        } else app_toast("Could not load that subtitle file");
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
    q->owner = p; q->gen = p->gen; q->addon = -1; q->slot = slot; q->seq = ++p->sub_seq[slot]; q->idx = idx;
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
            if (p->addon_subs[i].matched && !p->addon_subs[i].load_failed && lang_same(p->addon_subs[i].lang, lang)) {
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
                    if (!(p->emb_app && p->emb_for == tr[i].index)) video_select_track(TRACK_TEXT, tr[i].index);   /* the app draws that one: the TV's stays off */
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
                if (!(p->emb_app && p->emb_for == tr[unl].index)) video_select_track(TRACK_TEXT, tr[unl].index);
                p->embedded_text_sel = tr[unl].index;
                LOGI("player: subtitles: the file's own track (no language given)");
                return 1;
            }
        } else {
            for (int pass = 0; pass < 3; pass++) {   /* made for this file, then for the same release (its name), then any */
                int best = -1;
                for (int i = 0; i < p->n_addon_subs; i++) {
                    const AddonSub *as = &p->addon_subs[i];
                    if (!lang_same(as->lang, lang) || as->load_failed) continue;
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

typedef struct { Player *owner; unsigned gen, lifetime; int from, count; } XlReq;

static void on_xl_batch(const char *text, const char *error, void *user)
{
    XlReq *q = user;
    Player *p = q->owner;
    int from = q->from, count = q->count; unsigned gen = q->gen, lifetime = q->lifetime;
    free(q);
    if (p != g_live || lifetime != p->gen || p->xl_on != 1 || gen != p->xl_gen) return;
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
    q->owner = p; q->gen = p->xl_gen; q->lifetime = p->gen; q->from = from; q->count = count;
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
    if (p->cur >= p->n_cands) return 0;
    char enc[1600], url[2048];
    if (p->cands[p->cur].youtube_id && youtube_storyboard_tile(p->cands[p->cur].youtube_id, t, url, sizeof url)) {   /* a YouTube video: YouTube's own sheets of frames */
        if (prefetch_neighbours) {
            char nb[2048];
            for (int d = -2; d <= 2; d++) if (d && t + d * 10 >= 0 && youtube_storyboard_tile(p->cands[p->cur].youtube_id, t + d * 10, nb, sizeof nb)) image_prefetch(nb);
        }
        static char logged[16];
        if (strcmp(logged, p->cands[p->cur].youtube_id) != 0) { snprintf(logged, sizeof logged, "%s", p->cands[p->cur].youtube_id); LOGI("player: seek previews from YouTube's storyboard (%.*s)", (int)strcspn(url, "?"), url); }
        return image_get(url, w, h);
    }
    if (strncmp(p->cands[p->cur].url, "http", 4) != 0) return 0;
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

/* ---- living letterbox: a film wider or narrower than the screen leaves black bars; the
 * picture's own edges, from a small frame decoded every ten seconds (as the seek
 * previews are), are stretched softly into them and dimmed toward the screen's edge,
 * fading from one frame to the next. A few redraws a second, never every frame. ---- */
#define LB_STEP 5        /* a frame of the film every 5 s, where the file lists no keyframes */

static int lb_frame_url(const Player *p, double sec, char *url, size_t n)
{
    if (p->cur >= p->n_cands || !p->cands[p->cur].url || strncmp(p->cands[p->cur].url, "http", 4) != 0) return 0;
    char enc[1600], rel[1800];
    http_url_encode(p->cands[p->cur].url, enc, sizeof enc);
    if (g_app.thumb_server && *g_app.thumb_server) { snprintf(url, n, "%s/frame?url=%s&t=%.2f", g_app.thumb_server, enc, sec); return 1; }
    if (!frames_supported()) return 0;
    snprintf(rel, sizeof rel, "frame?t=%.2f&w=160&u=%s", sec, enc);
    return phone_local_url(rel, url, n);
}

/* The film's keyframes after `from`: where its pictures start anew, at every cut among them. */
typedef struct { Player *owner; unsigned gen; int cur; } LbKeysReq;
static void on_lb_keys(HttpResponse *r, void *user)
{
    LbKeysReq *q = user;
    Player *p = q->owner;
    if (p != g_live || q->gen != p->gen || q->cur != p->cur) { free(q); return; }
    free(q);
    if (r->status != 200 || !r->body) { p->lb_keys_state = 3; LOGI("player: living letterbox every %d s (no keyframe list)", LB_STEP); return; }
    p->lb_nkeys = 0;
    for (char *s = r->body; *s && p->lb_nkeys < 64; ) {
        char *e; double t = strtod(s, &e);
        if (e == s) break;
        p->lb_keys[p->lb_nkeys++] = t;
        s = e; while (*s == '\n' || *s == '\r' || *s == ' ') s++;
    }
    if (p->lb_keys_state != 2) LOGI("player: living letterbox changes with the film's cuts (%d keyframes ahead)", p->lb_nkeys);
    p->lb_keys_state = 2;
}

static void lb_ask_keys(Player *p, double from)
{
    if (p->cur >= p->n_cands || !p->cands[p->cur].url || strncmp(p->cands[p->cur].url, "http", 4) != 0) { p->lb_keys_state = 3; return; }
    char enc[1600], rel[1800], url[2048];
    http_url_encode(p->cands[p->cur].url, enc, sizeof enc);
    int ok;
    if (g_app.thumb_server && *g_app.thumb_server) { snprintf(url, sizeof url, "%s/keyframes?url=%s&t=%.2f", g_app.thumb_server, enc, from); ok = 1; }
    else { snprintf(rel, sizeof rel, "keyframes?t=%.2f&u=%s", from, enc); ok = frames_supported() && phone_local_url(rel, url, sizeof url); }
    if (!ok) { p->lb_keys_state = 3; return; }
    LbKeysReq *q = calloc(1, sizeof *q);
    q->owner = p; q->gen = p->gen; q->cur = p->cur;
    p->lb_keys_state = 1; p->lb_keys_from = from;
    http_request_async(url, NULL, on_lb_keys, q);
}

/* The time of the frame the bars want now, and the one to fetch ahead: the last cut
 * reached (no sooner than `gap` after the frame they show) and the next one; on a 5 s
 * grid when the file has no keyframe list. */
static double lb_pick(Player *p, double pos, double gap, double *next)
{
    if (p->lb_keys_state == 2 && p->lb_nkeys && pos > p->lb_keys[p->lb_nkeys - 1] - 2 * gap && p->lb_nkeys == 64)
        lb_ask_keys(p, pos);   /* nearly through the list: the next stretch */
    if (p->lb_keys_state == 2 && p->lb_nkeys && pos >= p->lb_keys_from) {
        double at = -1, ahead = -1;
        for (int i = 0; i < p->lb_nkeys; i++) {
            double k = p->lb_keys[i];
            if (k <= pos + 0.05) { if (p->lb_shown < 0 || k < p->lb_shown || k - p->lb_shown >= gap || k == p->lb_shown) at = k; }
            else { if (at < 0 ? 1 : k - at >= gap) { ahead = k; break; } }
        }
        if (at < 0) at = p->lb_shown >= 0 && p->lb_shown <= pos + 0.05 ? p->lb_shown : pos;   /* before the first cut listed */
        *next = ahead >= 0 ? ahead : at + LB_STEP;
        return at;
    }
    if (p->lb_keys_state == 2 && pos < p->lb_keys_from) lb_ask_keys(p, pos > 1 ? pos - 1 : 0);   /* went back past the list */
    double s0 = (long)(pos / LB_STEP) * LB_STEP;
    *next = s0 + LB_STEP;
    return s0;
}

#define LB_FADE_CUT 0.2f    /* at a cut the new light comes at once, as the picture changed (any slower and it trails the cut) */
#define LB_FADE_DRIFT 1.5f  /* every few seconds (no list of cuts): it drifts rather than flicks */
#define LB_AHEAD_S 45.0     /* the frames of the cuts this far ahead are got ready while it plays */
#define LB_LAZY_S 1.0f      /* frames slower than this: only the cuts made ready ahead change the light */
#define LB_LATE_S 0.6f      /* a cut's frame that comes later than this after it is not shown */
#define LB_SLOW_S 12.0f     /* frames slower than this (or LB_SLOW_N that never come): the artwork's colour */
#define LB_SLOW_N 3
#define LB_BLUR 5.6f     /* mip levels of blur: its colours, none of its shapes */
#define LB_LIGHT 235     /* the light's brightness (of 255): nearly the picture's own, as a lamp's */
#define LB_FALLOFF 0.75f /* how dark it gets at the screen's edge */
#define LB_TINT 0.55f    /* One colour: the scene's colour at this strength */

/* One frame into the bars: its light only; the falloff and the edge's shadow go over both
 * frames at once in draw_letterbox. */
/* The picture's edge beside each bar, in the frame's coordinates: a thin band of it (its
 * colours there, not its shapes), first bar then second (top, bottom; or left, right). */
static void lb_edges(const float box[4], int side, float e[4])
{
    float cw = box[2] - box[0], ch = box[3] - box[1];
    if (side == 1) { e[0] = box[1] + 0.02f * ch; e[1] = box[1] + 0.09f * ch; e[2] = box[3] - 0.09f * ch; e[3] = box[3] - 0.02f * ch; }
    else { e[0] = box[0] + 0.02f * cw; e[1] = box[0] + 0.09f * cw; e[2] = box[2] - 0.09f * cw; e[3] = box[2] - 0.02f * cw; }
}

static void lb_paint(unsigned tex, const char *url, int side, float bar, float a)
{
    float W = SNTV_UI_W, H = SNTV_UI_H;
    if (g_app.living_letterbox == LB_COLOUR) {   /* the frame's main colour, flat: one hue */
        unsigned t = image_tint(url);
        if (!t) t = RGBA(40, 40, 40, 255);
        Color c = RGBA((t >> 24) * LB_TINT, ((t >> 16) & 0xFF) * LB_TINT, ((t >> 8) & 0xFF) * LB_TINT, (int)(255 * a));   /* quieter than the picture */
        if (side == 1) { gfx_rect(0, 0, W, bar, c); gfx_rect(0, H - bar, W, bar, c); }
        else { gfx_rect(0, 0, bar, H, c); gfx_rect(W - bar, 0, bar, H, c); }
        return;
    }
    /* as lights behind the screen (Philips Hue, Ambilight): each part of a bar takes the colour
     * of the picture's edge right beside it, so the bars are soft zones of light level with the
     * picture, brightest against it (the falloff in draw_letterbox darkens them outward) */
    float box[4] = { 0, 0, 1, 1 }, e[4];
    image_content_box(url, box);
    lb_edges(box, side, e);
    Color c = RGBA(LB_LIGHT, LB_LIGHT, LB_LIGHT, (int)(255 * a));
    if (side == 1) {
        gfx_tex_blur(tex, 0, 0, W, bar, box[0], e[0], box[2], e[1], c, LB_BLUR);
        gfx_tex_blur(tex, 0, H - bar, W, bar, box[0], e[2], box[2], e[3], c, LB_BLUR);
    } else {
        gfx_tex_blur(tex, 0, 0, bar, H, e[0], box[1], e[1], box[3], c, LB_BLUR);
        gfx_tex_blur(tex, W - bar, 0, bar, H, e[2], box[1], e[3], box[3], c, LB_BLUR);
    }
}

/* ---- the film's own text subtitles, drawn by the app ----
 * An LG draws a file's built-in subtitle track itself, under the app, and hides it while the
 * app paints (the living letterbox). Read from the bytes the buffer downloads (mkvsubs.c),
 * the app draws the track itself instead, the TV's own drawing off, and the letterbox stays.
 * Only what it can be sure of: a text track of a Matroska file playing through the buffer,
 * the same language as the TV lists in that place. Anything else stays the TV's. */
static void emb_stop(Player *p, int to_tv)
{
    if (!p->emb_app) return;
    p->emb_app = 0;
    subs_free(&p->emb);
    p->emb_taken = 0;
    if (to_tv && p->embedded_text_sel >= 0) { video_select_track(TRACK_TEXT, p->embedded_text_sel); push_sub_style(p); LOGI("player: the TV draws the film's own subtitles again"); }
}

static void emb_tick(Player *p)
{
    int want = p->embedded_text_sel >= 0 && g_app.living_letterbox != LB_OFF && p->emb_buffered && !p->pip;
    if (p->emb_app && (!want || p->emb_for != p->embedded_text_sel)) emb_stop(p, 1);
    if (want && !p->emb_app && p->emb_tried != p->embedded_text_sel + 1) {
        MkvSubHead h;
        if (!mkvsubs_head(&h)) return;   /* its tracks not known yet */
        p->emb_tried = p->embedded_text_sel + 1;
        VideoTrack tr[MAX_TRACKS];
        int nt = video_tracks(TRACK_TEXT, tr, MAX_TRACKS), k = -1;
        for (int i = 0; i < nt; i++) if (tr[i].index == p->embedded_text_sel) k = i;
        if (k < 0 || k >= h.n || nt != h.n) { LOGI("player: the TV lists %d subtitle tracks, the file %d: the TV draws them", nt, h.n); return; }
        const MkvSubTrack *t = &h.t[k];
        if (!t->text) { LOGI("player: the film's subtitle track %d is pictures (%s): the TV draws it", k + 1, t->codec); return; }
        if (tr[k].lang[0] && t->lang[0] && !lang_same(tr[k].lang, t->lang)) { LOGI("player: the film's subtitle track %d is %s here, %s in the file: the TV draws it", k + 1, tr[k].lang, t->lang); return; }
        p->emb_app = 1; p->emb_for = p->embedded_text_sel; p->emb_num = t->number; p->emb_taken = 0;
        p->emb_off_at = g_app.time; p->emb_off_n = 0;
        subs_free(&p->emb);
        video_select_track(TRACK_TEXT, -1);
        LOGI("player: the film's own subtitles (%s, %s) are drawn by the app, read from the buffer", t->lang, t->codec);
    }
    if (!p->emb_app) return;
    static float looked;
    if (g_app.time - looked > 0.5f || g_app.time < looked) {   /* the TV's drawing stays off, whatever selects the track again */
        looked = g_app.time;
        VideoTrack tr[MAX_TRACKS];
        int nt = video_tracks(TRACK_TEXT, tr, MAX_TRACKS);
        for (int i = 0; i < nt; i++) if (tr[i].selected) { video_select_track(TRACK_TEXT, -1); LOGI("player: the TV's drawing of the film's subtitles turned off again (the app draws them)"); break; }
    }
    /* and off again at 1, 3 and 6 s regardless: the TV's pipeline applies the track selection
     * made at the start after its own time, and that turns its drawing back on (both showed) */
    if (p->emb_off_n < 3 && g_app.time - p->emb_off_at > (p->emb_off_n == 0 ? 1 : p->emb_off_n == 1 ? 3 : 6)) {
        p->emb_off_n++;
        video_select_track(TRACK_TEXT, -1);
    }
    int have = mkvsubs_count(p->emb_num);
    if (have <= p->emb_taken) return;
    SubCue got[128];
    int n = mkvsubs_take(p->emb_num, p->emb_taken, got, 128);
    if (n <= 0) return;
    SubCue *grown = realloc(p->emb.cues, sizeof *grown * (size_t)(p->emb.n + n));
    if (!grown) { for (int i = 0; i < n; i++) free(got[i].text); return; }
    p->emb.cues = grown;
    for (int i = 0; i < n; i++) {   /* in time order: after a jump back, earlier lines come later */
        int at = p->emb.n++;
        while (at > 0 && p->emb.cues[at - 1].start > got[i].start) { p->emb.cues[at] = p->emb.cues[at - 1]; at--; }
        p->emb.cues[at] = got[i];
    }
    if (!p->emb_taken) LOGI("player: the film's own subtitles: %d line%s read so far", n, n == 1 ? "" : "s");
    p->emb_taken += n;
}

/* A frame came (took: how long), or did not (took < 0). A TV slow to get them still has the
 * light, from fewer cuts (lb_work_ahead looks further ahead); only where frames do not come
 * at all, or take ages, the bars take the film's artwork's colour instead. */
static void lb_timed(Player *p, float took)
{
    if (took >= 0) {
        float was = p->lb_took;
        p->lb_took = was > 0 ? was * 0.7f + took * 0.3f : took;
        if (p->lb_slow_n > 0) p->lb_slow_n--;
        if (was <= LB_LAZY_S && p->lb_took > LB_LAZY_S) LOGI("player: the bars' frames take %.1f s here: the light follows the cuts it can get ready", p->lb_took);
    } else p->lb_slow_n++;
    if (!p->lb_slow && (p->lb_slow_n >= LB_SLOW_N || p->lb_took > LB_SLOW_S)) {
        p->lb_slow = 1;
        LOGI("player: the living letterbox is too slow on this TV (frames take %.1f s, or do not come): the bars take the colour of the film's artwork", p->lb_took);
    }
}

/* Work ahead: the frames of the cuts coming in the next LB_AHEAD_S, one at a time, so at a
 * cut the bars have it already. From update(), every frame, whether or not it draws. */
static void lb_work_ahead(Player *p, const VideoInfo *vi)
{
    if (!p->lb_side || p->lb_slow || p->lb_keys_state != 2 || !p->lb_nkeys || p->seek_commit > 0 || vi->state != VIDEO_PLAYING) return;
    int w, h;
    if (p->lb_ahead[0]) {   /* one on its way: wait for it */
        if (image_peek_ex(p->lb_ahead, IMAGE_MIPMAP, &w, &h)) {
            double k = strtod(strstr(p->lb_ahead, "t=") + 2, NULL);
            lb_timed(p, g_app.time - p->lb_ahead_at);
            if (p->lb_ready_n++ < 3) LOGI("player: the bars' frame at %.2f s is ready ahead%s", k, vi->position > k ? " (late)" : "");
        }
        else if (!image_failed(p->lb_ahead) && g_app.time - p->lb_ahead_at < 20) { image_get_ex(p->lb_ahead, IMAGE_MIPMAP, &w, &h, NULL); return; }
        else lb_timed(p, -1);   /* it did not come */
        p->lb_ahead[0] = 0;
    }
    if (p->lb_asked[0]) return;   /* the frame wanted now comes first (draw_letterbox gives up on it) */
    double gap = vi->width >= 3000 ? 4.0 : 2.0, last = p->lb_shown >= 0 ? p->lb_shown : vi->position;
    /* frames slow to come here: no cut nearer than it takes to get one ready, nor closer to the
     * last asked than that, so each is ready when its cut comes (the light changes less often) */
    double lead = p->lb_took > LB_LAZY_S ? 1.5 * p->lb_took + 0.5 : 0, space = p->lb_took > gap ? p->lb_took : gap;
    char url[2048];
    for (int i = 0; i < p->lb_nkeys; i++) {
        double k = p->lb_keys[i];
        if (k <= last + 0.05 || k - last < space || k < vi->position + lead) continue;
        if (k > vi->position + LB_AHEAD_S) break;
        last = k;
        if (!lb_frame_url(p, k, url, sizeof url) || image_peek_ex(url, IMAGE_MIPMAP, &w, &h) || image_failed(url)) continue;
        image_get_ex(url, IMAGE_MIPMAP, &w, &h, NULL);
        snprintf(p->lb_ahead, sizeof p->lb_ahead, "%s", url);
        p->lb_ahead_at = g_app.time;
        return;
    }
}

/* Too slow here: the bars take the artwork's colour, flat, as One colour does with a scene's. */
static void lb_paint_art(Player *p, int side, float bar, float a)
{
    char buf[1024];
    const char *bg = app_art_background(&p->meta, buf, sizeof buf);
    int w, h;
    unsigned t = bg && image_get(bg, &w, &h) ? image_tint(bg) : 0;
    if (!t) { app_redraw(); return; }   /* the artwork is on its way */
    Color c = RGBA((t >> 24) * LB_TINT, ((t >> 16) & 0xFF) * LB_TINT, ((t >> 8) & 0xFF) * LB_TINT, (int)(255 * a));
    float W = SNTV_UI_W, H = SNTV_UI_H;
    if (side == 1) { gfx_rect(0, 0, W, bar, c); gfx_rect(0, H - bar, W, bar, c); }
    else { gfx_rect(0, 0, bar, H, c); gfx_rect(W - bar, 0, bar, H, c); }
}

/* A computer: the playing picture's own colours, a few texels eased over time, stretched
 * into the bars (as a lamp behind the screen; One colour: its middle, flat). */
static void lb_paint_glow(unsigned tex, int side, float bar, float a)
{
    float W = SNTV_UI_W, H = SNTV_UI_H, box[4] = { 0, 0, 1, 1 }, e[4];
    lb_edges(box, side, e);
    if (g_app.living_letterbox == LB_COLOUR) { e[0] = e[1] = e[2] = e[3] = 0.5f; }   /* One colour: the picture's middle, flat */
    Color c = g_app.living_letterbox == LB_COLOUR ? RGBA(255 * LB_TINT, 255 * LB_TINT, 255 * LB_TINT, (int)(255 * a)) : RGBA(LB_LIGHT, LB_LIGHT, LB_LIGHT, (int)(255 * a));
    float u0 = g_app.living_letterbox == LB_COLOUR ? 0.5f : 0, u1 = g_app.living_letterbox == LB_COLOUR ? 0.5f : 1;
    if (side == 1) {
        gfx_tex_uv(tex, 0, 0, W, bar, u0, e[0], u1, e[1], c);
        gfx_tex_uv(tex, 0, H - bar, W, bar, u0, e[2], u1, e[3], c);
    } else {
        gfx_tex_uv(tex, 0, 0, bar, H, e[0], u0, e[1], u1, c);
        gfx_tex_uv(tex, W - bar, 0, bar, H, e[2], u0, e[3], u1, c);
    }
}

static void draw_letterbox(Player *p, const VideoInfo *vi)
{
    p->lb_bar = 0; p->lb_side = 0;
    int paint = g_app.living_letterbox != LB_OFF;   /* off, the bars still say where subtitles may sit */
    if ((!paint && !g_app.subs_in_bars) || p->pip || p->aspect != ASPECT_FIT || (vi->state != VIDEO_PLAYING && vi->state != VIDEO_PAUSED)) return;
    /* the TV draws a film's own subtitle track itself, under the app, and an LG hides it
     * while the app paints during playback: unless the app draws it (emb_tick), the screen is
     * left as with the letterbox off */
    if (paint && p->embedded_text_sel >= 0 && !p->emb_app) {
        if (!p->lb_aside_logged) { p->lb_aside_logged = 1; LOGI("player: the living letterbox steps aside while the TV draws the film's own subtitles"); }
        return;
    }
    double pos = p->seek_commit > 0 ? p->seek_target : vi->position;
    char u0[2048], u1[2048];
    /* no frames while the film starts, nor for a source already seen to fill the screen, nor where it is too slow */
#if defined(SNTV_DESKTOP_PLAYER) || defined(SNTV_PLATFORM_ANDROID)
    unsigned glow = paint && !g_app.fx_light ? video_glow_texture() : 0;   /* the picture's own colours, live: a computer draws it, an Android copies it */
#else
    unsigned glow = 0;
#endif
    int starting = p->lb_none == p->cur + 1 || g_app.time - p->load_started <= 3, frames = paint && !glow && !p->lb_slow && !g_app.fx_light;
    if (frames && !starting && !p->lb_keys_state) lb_ask_keys(p, pos > 1 ? pos - 1 : 0);
    double gap = vi->width >= 3000 ? 4.0 : 2.0;   /* a 4K film's keyframes are big: not so often */
    double s1, s0 = lb_pick(p, pos, gap, &s1);
    p->lb_next_t = s1;
    int have_url = frames && !starting && lb_frame_url(p, s0, u0, sizeof u0) && lb_frame_url(p, s1, u1, sizeof u1);
    int w0 = 0, h0 = 0;
    unsigned t0 = 0;
    if (have_url) {
        int seeking = p->seek_commit > 0;   /* the seek previews have the TV's decoder then */
        /* mipmapped, so the GPU blurs them for free; only the frame of now is shown (the ones
         * ahead are got ready by lb_work_ahead), so the bars never show what has not happened yet */
        /* where frames are slow, a cut not made ready keeps the light it has (asking now would
         * hold up the ones ahead); the first after starting or a seek is asked all the same */
        int lazy = seeking || (p->lb_took > LB_LAZY_S && p->lb_last[0] && fabs(p->lb_shown - pos) < LB_AHEAD_S);
        unsigned now_t = lazy ? image_peek_ex(u0, IMAGE_MIPMAP, &w0, &h0) : image_get_ex(u0, IMAGE_MIPMAP, &w0, &h0, NULL);
        if (p->lb_asked[0] && (image_failed(p->lb_asked) || g_app.time - p->lb_ask_at > 20)) { p->lb_asked[0] = 0; lb_timed(p, -1); }   /* it will not come */
        if (!now_t && !lazy && !image_failed(u0) && strcmp(u0, p->lb_asked) != 0) { snprintf(p->lb_asked, sizeof p->lb_asked, "%s", u0); p->lb_ask_at = g_app.time; }   /* not ready: wanted from now */
        if (now_t && p->lb_asked[0] && !strcmp(u0, p->lb_asked)) { lb_timed(p, g_app.time - p->lb_ask_at); p->lb_asked[0] = 0; }
        if (now_t && strcmp(u0, p->lb_last) != 0 && p->lb_last[0] && p->lb_keys_state == 2 && pos - s0 > LB_LATE_S && !seeking) {
            /* it came after its cut: shown now it would trail the picture, so the light stays
             * as it is until the next cut that is ready in time */
            if (p->lb_late_n++ < 3) LOGI("player: the bars' frame for the cut at %.2f s came %.1f s late: not shown", s0, pos - s0);
            p->lb_shown = s0;
        }
        else if (now_t && strcmp(u0, p->lb_last) != 0) {   /* a new frame: it fades in over the last */
            snprintf(p->lb_prev, sizeof p->lb_prev, "%s", p->lb_last);
            snprintf(p->lb_last, sizeof p->lb_last, "%s", u0);
            p->lb_swap_at = g_app.time;
            p->lb_fade = p->lb_keys_state == 2 ? LB_FADE_CUT : LB_FADE_DRIFT;
            p->lb_shown = s0;
            if (p->lb_keys_state == 2 && p->lb_cuts_logged < 3) { p->lb_cuts_logged++; LOGI("player: the bars follow the cut at %.2f s", s0); }
        }
        if (p->lb_last[0]) t0 = image_peek_ex(p->lb_last, IMAGE_MIPMAP, &w0, &h0);
    }
    /* the picture's shape: the file's, narrowed by any black bars baked into it */
    float box[4] = { 0, 0, 1, 1 }, ar = 0;
    if (vi->width > 0 && vi->height > 0) ar = (float)vi->width / vi->height;
    else if (p->probe && p->probe->width > 0 && p->probe->height > 0) ar = (float)p->probe->width / p->probe->height;
    else if (t0 && image_peek_ex(p->lb_last, IMAGE_PLAIN, &w0, &h0) && h0 > 0) ar = (float)w0 / h0;   /* the mipmapped copy is padded square-ish */
    if (t0) image_content_box(p->lb_last, box);
    if (ar > 0 && box[3] - box[1] > 0.2f && box[2] - box[0] > 0.2f) ar *= (box[2] - box[0]) / (box[3] - box[1]);
    if (t0 && ar > 0) { p->lb_ar = ar; p->lb_ar_cur = p->cur + 1; }   /* kept: the bars stay their size when the frames stop (too slow) */
    else if (p->lb_ar_cur == p->cur + 1 && p->lb_ar > 0) ar = p->lb_ar;
    float W = SNTV_UI_W, H = SNTV_UI_H, A = W / H, bar;
    int side;
    if (ar > A * 1.04f) { side = 1; bar = (H - W / ar) / 2; }
    else if (ar > 0 && ar < A / 1.04f) { side = 2; bar = (W - H * ar) / 2; }
    else { if (t0) p->lb_none = p->cur + 1; return; }
    if (bar < 36) return;
    p->lb_bar = bar; p->lb_side = side;
    if (!p->lb_logged) { p->lb_logged = 1; LOGI("player: living letterbox, bars of %.0f %s (picture %.2f:1), %s%s", bar, side == 1 ? "above and below" : "at the sides", ar, LB_STYLE_NAMES[g_app.living_letterbox], g_app.subs_in_bars ? ", subtitles in them" : ""); }
    if (!paint || g_app.fx_light || (!have_url && !p->lb_slow && !glow)) return;   /* the bars stay black; subtitles still move into them */
    /* fade in once a frame is here, out when there is none */
    float step = g_app.time - p->lb_kt;
    p->lb_kt = g_app.time;
    if (step > 0.5f) step = 0.5f;
    float target = t0 || p->lb_slow || glow ? 1.0f : 0.0f;
    p->lb_k += (target - p->lb_k) * (step * 2.5f > 1 ? 1 : step * 2.5f);
    if (p->lb_k < 0.01f) return;
    float x = 1;
    if (glow) lb_paint_glow(glow, side, bar, p->lb_k);
    else if (p->lb_slow) lb_paint_art(p, side, bar, p->lb_k);
    else {
        x = (g_app.time - p->lb_swap_at) / (p->lb_fade > 0 ? p->lb_fade : LB_FADE_DRIFT);   /* the new frame over the last one */
        x = x < 0 ? 0 : x > 1 ? 1 : x * x * (3 - 2 * x);
        int pw0, ph0;
        unsigned tp = x < 1 && p->lb_prev[0] ? image_peek_ex(p->lb_prev, IMAGE_MIPMAP, &pw0, &ph0) : 0;
        if (tp) lb_paint(tp, p->lb_prev, side, bar, p->lb_k);
        if (t0) lb_paint(t0, p->lb_last, side, bar, p->lb_k * (tp ? x : 1));
    }
    /* brightest against the picture, dimmer away from it, as light thrown on a wall */
    Color dark = RGBA(0, 0, 0, (int)(255 * p->lb_k * LB_FALLOFF)), clear = RGBA(0, 0, 0, 0);
    if (side == 1) {
        gfx_gradient_v(0, 0, W, bar, dark, clear);
        gfx_gradient_v(0, H - bar, W, bar, clear, dark);
    } else {
        gfx_gradient_h(0, 0, bar, H, dark, clear);
        gfx_gradient_h(W - bar, 0, bar, H, clear, dark);
    }
    p->lb_drawn = g_app.time;
    if (fabsf(p->lb_k - target) > 0.01f || x < 1) app_redraw();   /* only while it fades */
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
    int span = g_app.low_memory || mem_stage() >= 1 ? 0 : 2;   /* the strip's neighbours cost decodes */
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
    player_seek(s->end);
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
static void emb_stop(Player *p, int to_tv);
static void buffer_alternates(const Player *p, int idx);
static void fail_over(Player *p, const char *why);

/* A torrent TorBox has cached: its link comes from TorBox when the source's turn comes */
typedef struct { Player *owner; int idx; double start; char *link; unsigned gen, attempt; } TbReq;

static void on_torbox_open(const char *url, const char *error, void *user)
{
    TbReq *q = user;
    Player *p = q->owner;
    int current = p == g_live && q->gen == p->gen && !p->finished && q->attempt == p->attempt && q->idx == p->cur && !strcmp(p->cands[q->idx].url, q->link);
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

static int same_file(const PlayCandidate *a, const PlayCandidate *b)
{
    return !strcmp(a->url, b->url) || (a->video_size && a->video_size == b->video_size
        && a->video_hash && b->video_hash && *a->video_hash && !strcasecmp(a->video_hash, b->video_hash));
}

static void start_candidate(Player *p, int idx, double start)
{
    int paused = p->viewer_paused || (p->own_load && video_load_count() == p->own_load && video_paused_on_purpose());
    if (idx != p->cur) {   /* another source: its own count, and its own file */
        if (p->cur < 0 || !same_file(&p->cands[p->cur], &p->cands[idx])) {   /* (no current one: a new list after asking the addons again) */
            p->file_bad = 0;
            p->embedded_offset = 0;
            p->forced_checked = 0;
            if (p->keep_sub_kind == 1 && !p->subs_user && p->keep_sub_idx >= 0 && p->keep_sub_idx < p->n_addon_subs) {   /* the app's pick was for the old file: the language, chosen again for this one; the viewer's own pick stays (and is synced to it) */
                snprintf(p->keep_sub_lang, sizeof p->keep_sub_lang, "%s", p->addon_subs[p->keep_sub_idx].lang);
                p->keep_sub_kind = 2;
            }
        }
        p->net_retries = 0; p->net_played = 0;
        p->os_asked = 0;
        for (int k = 0; k < p->n_addon_subs; k++) p->addon_subs[k].matched = 0;   /* matched to the old file; this one's hash re-marks them */
        p->lb_keys_state = 0; p->lb_nkeys = 0; p->lb_shown = -1;   /* its own keyframes */
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
    p->net_retry_pos = start;
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
    memset(&p->watch, 0, sizeof p->watch);
    if (idx < 64) p->tried |= 1ull << idx;
    p->started = 0; p->p2p_peers = -1; p->p2p_info = 0; p->thumbs_warm = 0; p->length_checked = 0;
    p->stalls_here = 0; p->lighter_to = -1;
    for (int w = 0; w < 2; w++) { p->warm[w].state = 0; p->warm[w].idx = -1; p->warm[w].at = g_app.time; }
    p->drain_n = 0; p->drain_t = 0;
    p->good_since = 0; p->good_recorded = 0; p->lighter_offered = 0;
    char res[16] = "";
    if (cand_res(&p->cands[idx])) snprintf(res, sizeof res, " (%dp)", cand_res(&p->cands[idx]));   /* the log keeps only the link's host: which copy, by its picture */
    LOGI("player: candidate %d/%d %s @%.0fs -> %s%s", idx + 1, p->n_cands, p->cands[idx].label ? p->cands[idx].label : "", start, p->cands[idx].url, res);
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
    emb_stop(p, 0);
    p->emb_tried = 0;
    p->emb_buffered = bufferable && relayed != NULL;
    if (p->emb_buffered) mkvsubs_open(streams_final_url(u) ? streams_final_url(u) : u, p->cands[idx].headers);   /* its own subtitle tracks, read as the buffer fills */
    else mkvsubs_open(NULL, NULL);
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
        q->owner = p; q->gen = p->gen; q->idx = idx; q->start = start; q->link = strdup(p->cands[idx].url); q->attempt = p->attempt;
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
        const char *hash = p->cands[idx].url + strlen("http://127.0.0.1:11490/");
        char h[41]; snprintf(h, sizeof h, "%.40s", hash);
        if (torbox_hash_cached(h)) app_toast("TorBox is not answering: playing P2P on this TV. It goes back to TorBox when TorBox answers.");   /* TorBox has it: it is down */
        else {
            app_toast("No cached copy: playing P2P on this TV. It switches back when TorBox has it.");
            torbox_queue_hash(hash);   /* your TorBox fetches it meanwhile */
        }
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
    if (paused) { video_pause(); p->viewer_paused = 1; }
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

static int svc_fails(const Player *p, int idx)
{
    char name[40];
    streams_service(p->cands[idx].url, p->cands[idx].label, name, sizeof name);
    for (int k = 0; k < p->n_svc; k++) if (!strcmp(p->svc[k].name, name)) return p->svc[k].fails;
    return 0;
}

/* A source that never started counts against its service; a second one from the same
 * service (or TorBox not answering at all) means the service is down: its other sources
 * are skipped, for other addons' sources and P2P. */
static void svc_failed(Player *p, int idx, const char *why)
{
    char name[40];
    streams_service(p->cands[idx].url, p->cands[idx].label, name, sizeof name);
    if (!strcmp(name, "P2P") || !name[0]) return;
    int k = 0;
    while (k < p->n_svc && strcmp(p->svc[k].name, name)) k++;
    if (k == p->n_svc) {
        if (k == 8) return;
        snprintf(p->svc[k].name, sizeof p->svc[k].name, "%s", name);
        p->svc[k].fails = p->svc[k].down = 0; p->n_svc++;
    }
    p->svc[k].fails++;
    int unreachable = why && (strstr(why, "did not take the torrent (0") || strstr(why, "did not take the torrent (5") || strstr(why, "did not answer") || strstr(why, "error video"));
    if (p->svc[k].fails < 2 && !unreachable) return;
    p->svc[k].down = 1;
    int skipped = 0;
    char other[40];
    for (int i = 0; i < p->n_cands && i < 16; i++) {
        if (i == idx || p->dead[i]) continue;
        streams_service(p->cands[i].url, p->cands[i].label, other, sizeof other);
        if (!strcmp(other, name)) { p->dead[i] = 1; skipped++; }
    }
    if (skipped) LOGI("player: %s looks down (%s); its %d other source%s skipped for other addons'", name, why ? why : "?", skipped, skipped == 1 ? " is" : "s are");
}

static int next_candidate(const Player *p)
{
    /* the link failed: the same file from another addon first, from the same moment; not when
       the file itself can't play here (its decoding or its audio), which another link won't change */
    for (int i = 0; p->cur >= 0 && !p->file_bad && p->tries < 3 && i < p->n_cands && i < 64; i++)
        if (i != p->cur && !(p->tried >> i & 1) && !(i < 16 && p->dead[i]) && same_file(&p->cands[p->cur], &p->cands[i])) return i;
    /* two failed: first a source from a service that has not failed (another addon's),
     * then P2P on this TV */
    for (int i = p->cur + 1; p->tries >= 2 && p->tries < 6 && i < p->n_cands; i++)
        if (!(i < 16 && p->dead[i]) && !torrent_engine_url_is(p->cands[i].url) && !svc_fails(p, i)) return i;
    for (int i = p->cur + 1; p->tries >= 2 && i < p->n_cands; i++)
        if (torrent_engine_url_is(p->cands[i].url)) return i;
    /* the next one not already found dead (checked while this one played) */
    for (int i = p->cur + 1; i < p->n_cands; i++) {
        if (i < 16 && p->dead[i]) continue;
        if (p->tries < 3 || torrent_engine_url_is(p->cands[i].url)) return i;
        break;
    }
    /* then any above it not tried yet (a source picked from the panel can sit last) */
    for (int i = 0; i < p->cur && i < 64; i++)
        if (!(p->tried >> i & 1) && !(i < 16 && p->dead[i]) && (p->tries < 3 || (p->tries < 6 && !svc_fails(p, i)))) return i;
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

static double candidate_mbps(const Player *p, int idx)
{
    double seconds = p->known_dur > 0 ? p->known_dur : quality_runtime_minutes(p->meta.runtime) * 60;
    return seconds > 0 && p->cands[idx].video_size ? p->cands[idx].video_size * 8.0 / seconds / 1e6 : 0;
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
    app_toast(same_file(&p->cands[p->cur], &p->cands[next]) ? "Carrying on with %s from the same moment" : "Trying %s near where you left off", addon[0] ? addon : "another source");
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
    if (p->net_retries >= 3 || !(vi->failure == VIDEO_FAILURE_NETWORK || code == 300 || (code >= 40000 && code < 50000))) return 0;
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
    p->file_bad = 1;   /* the same file elsewhere has the same audio */
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
    if (strstr(vi->error, "xpired") || strstr(vi->error, "403") || strstr(vi->error, "404")) return 0;   /* the link, whatever the player calls it */
    if (vi->failure != VIDEO_FAILURE_UNKNOWN) return vi->failure == VIDEO_FAILURE_DECODE;
    if (strstr(vi->error, "player error")) return 1;   /* Tizen's player: the file */
    if (strstr(vi->error, "refused this stream")) return 0;   /* webOS would not even load it: the link, not the decoders */
    const char *c = strstr(vi->error, "(code ");
    int code = c ? atoi(c + 6) : 0;
    if (!code || code == 300 || (code >= 40000 && code < 50000)) return 0;   /* network, or no code: nothing about decoding */
    return 1;
}

/* Under a minute and a half where a film or an episode runs longer than twenty: the clip an
 * addon (AIOStreams, Comet...) streams in place of the file when its debrid service is down
 * ("Debrid provider is currently unavailable"), or a sample. 1 with why filled. Not trailers,
 * YouTube, or titles that are short themselves. */
static int error_clip(const Player *p, double secs, char *why, size_t n)
{
    if (p->trailer || (p->cands[p->cur].label && !strcmp(p->cands[p->cur].label, "YouTube")) || getenv("SNTV_NO_CLIP_CHECK")) return 0;
    double mins = quality_runtime_minutes(p->meta.runtime);
    if (secs >= 90 || (mins > 0 && mins < 20)) return 0;
    if (mins > 0) snprintf(why, n, "a %.0f-second clip, not the %.0f-minute video (the addon's error video)", secs, mins);
    else snprintf(why, n, "a %.0f-second clip (the addon's error video)", secs);
    return 1;
}

static void fail_over(Player *p, const char *why)
{
    VideoInfo vi;
    video_info(&vi);
    int clip = strstr(why, "error video") != NULL;   /* it played, but was never the film */
    if (clip) vi.position = 0;
    double resume = vi.position > 5 ? vi.position : 0;
    if (clip || decoder_failure(p, &vi)) p->file_bad = 1;   /* the file, not the link: no other link to it */
    if (!p->learned && decoder_failure(p, &vi)) {   /* the error never names the decoder: only a source needing one feature says which failed */
        unsigned needs = p->cands[p->cur].needs;
        for (int c = 0; c < TVCAP_COUNT; c++) if (g_app.tv.ok[c] > g_app.tv.fail[c]) needs &= ~(1u << c);   /* played fine here before: not the cause */
        if (needs && !(needs & (needs - 1))) app_tv_learn(needs, 0);
        else if (needs) LOGI("tv profile: a source needing several features failed; none is blamed");
        p->learned = 1;
    }
    if (vi.position < 5) app_link_failed(p->cands[p->cur].url);   /* never started: sort it last for six hours */
    if (vi.position < 5) svc_failed(p, p->cur, why);
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

static void cand_drop(PlayCandidate *c);

/* Every source failed: ask the addons again (debrid links often come back). */
static void on_reask(const Stream *items, int n, void *user)
{
    Player *p = user;
    if (p != g_live || p->finished) return;
    PlayCandidate *c = calloc(7, sizeof *c);
    int k = 0, p2p = 0, debrid = 0;
    unsigned char used[64] = { 0 };
    for (int pass = 0; pass < 2; pass++) {   /* sources from a service found down come last */
        for (int i = 0; i < n && i < 64 && k < 6; i++) {
            int direct = streams_is_direct(&items[i]);
            if (pass == 0 && !direct && debrid >= 4) continue;   /* room for plain links: every debrid source may share the outage */
            if (used[i] || !streams_candidate(&p->meta, &items[i], &c[k])) continue;
            char name[40]; int down = 0;
            streams_service(c[k].url, c[k].label, name, sizeof name);
            for (int s = 0; s < p->n_svc; s++) down |= !strcmp(p->svc[s].name, name) && p->svc[s].down;
            if (pass == 0 && down) { cand_drop(&c[k]); memset(&c[k], 0, sizeof c[k]); continue; }
            p2p |= torrent_engine_url_is(c[k].url);
            debrid += !direct;
            used[i] = 1; k++;
        }
        /* before the down service's own: the best torrent from peers (TorBox had it cached,
         * so it only came as TorBox's link) */
        for (int i = 0; pass == 0 && !p2p && i < n && k < 6; i++)
            if (streams_p2p_candidate(&p->meta, &items[i], &c[k])) { p2p = 1; k++; LOGI("player: P2P of the same torrent next, the service being down"); }
    }
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
    free(c->url); free(c->label); free(c->binge_group); free(c->filename); free(c->detail); free(c->release); free(c->youtube_id);
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

/* LG reports some stalls as a pause nobody asked for: for the fallback logic that is
 * buffering while playing, not a pause. (The viewer's pause, a break's and the TV's own
 * Home all go through video_pause, which marks them on purpose.) */
static int pipeline_paused(Player *p, const VideoInfo *vi)
{
    int pp = vi->state == VIDEO_PAUSED && p->started && !p->viewer_paused && !video_paused_on_purpose() && !p->pause_when_started;
    if (pp && !p->pp_since) { p->pp_since = g_app.time; p->pp_logged = 0; }
    if (!pp) p->pp_since = 0;
    int stall = pp && g_app.time - p->pp_since > 2;   /* a moment of pause around a seek is not one */
    if (stall && !p->pp_logged) { p->pp_logged = 1; LOGI("player: the pipeline paused by itself at %.0fs: a stall", vi->position); }
    return stall;
}

static void fallback_tick(Player *p, const VideoInfo *vi_real)
{
    if (p->trailer || p->finished) return;
    VideoInfo fx = *vi_real; const VideoInfo *vi = &fx;
    if (pipeline_paused(p, vi_real)) { fx.state = VIDEO_PLAYING; fx.buffering = 1; }
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
            int via_relay = relay_enabled() && !(idx < 16 && p->relay_off[idx]);   /* as it will be played: the relay sends its headers, the TV's player can't */
            HttpOptions o = { .range_bytes = 65536, .timeout_s = 12, .no_json_accept = 1, .headers = via_relay ? p->cands[idx].headers : NULL };
            http_request_async_ex(p->cands[idx].url, &o, on_warm, q);
        } else p->warm[w].at = now + 60;
    }
    /* 4. the buffer is draining: a lighter copy before playback stops */
    if (playing && !vi->buffering && vi->buffer_ahead >= 0 && now - p->jumped_at > 8 && now - p->drain_t >= env_s("SNTV_DRAIN_EVERY", 5)) {
        p->drain_t = now;
        double more0 = p->relay_used ? relay_buffer_ahead() : -1;
        if ((more0 > 0) != p->drain_relay) { p->drain_n = 0; p->drain_relay = more0 > 0; }   /* the buffer let go (or came): not a drain */
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
                        double rate = candidate_mbps(p, i), current_rate = candidate_mbps(p, p->cur);
                        if ((rate > 0 && current_rate > 0 && rate >= current_rate * 0.8) || (i < 64 && (p->tried >> i & 1))) continue;
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

/* The subtitle lines on screen at pos, as one number: drawing again is needed when it changes. */
static unsigned sub_signature(Player *p, double pos)
{
    static char b[2048];
    unsigned h = 2166136261u;
    for (int k = 0; k < 3; k++) {
        const SubTrack *t = k == 1 ? &p->track2 : p->emb_app && !p->track_loaded ? &p->emb : &p->track;
        int loaded = k == 1 ? p->track2_loaded : p->track_loaded || p->emb_app;
        if (!loaded || !subs_text_at(t, pos, k == 2, b, sizeof b)) { h = (h ^ (unsigned)(k + 1)) * 16777619u; continue; }
        for (const unsigned char *c = (const unsigned char *)b; *c; c++) h = (h ^ *c) * 16777619u;
    }
    return h;
}

/* Playing with nothing over the picture but its subtitles: drawing it sixty times a second
 * only takes the TV's time from the keys and the network. */
static int bare_playback(Player *p, const VideoInfo *vi)
{
    return vi->state == VIDEO_PLAYING && !vi->buffering && p->started && p->veil <= 0 && !p->pip && !TOUCH_UI
        && g_app.time > p->overlay_until + 1 && !p->menu && !p->stats && !p->scenes_open && !p->cast_open && !p->still_asking
        && !p->up_next_shown && p->lighter_to < 0 && p->seek_commit <= 0 && p->seek_flash < g_app.time && p->play_flash < g_app.time
        && p->rest_t <= 0.001f && p->catchup_until <= g_app.time && !subs_tuning(p)
        && !(p->adv_at && g_app.time - p->adv_at < ADVISORY_S + 0.1f)
        && !(g_app.skip_mode == 0 && skippable(p, vi));
}

/* On to the next episode: the next player opens on that episode's still, grown out of the
 * Up Next card where it was, instead of the loading card; the picture dissolves in under it. */
static struct { char video_id[160]; float at; int from; float rect[4]; } g_bridge;

void player_bridge_cancel(void) { g_bridge.video_id[0] = 0; }

static void bridge_hand(const char *video_id, const float *card)
{
    snprintf(g_bridge.video_id, sizeof g_bridge.video_id, "%s", video_id);
    g_bridge.at = g_app.time;
    g_bridge.from = card != NULL;
    if (card) memcpy(g_bridge.rect, card, sizeof g_bridge.rect);
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
    if (p->next_video_id && !strcmp(video_id, p->next_video_id))   /* on to the next one (not Previous or one picked from the list) */
        bridge_hand(video_id, g_app.time - p->card_at < 0.5f ? p->card_thumb : NULL);
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

typedef struct { Player *owner; unsigned gen, lifetime; } P2pReq;

static void on_p2p_stats(HttpResponse *r, void *user)
{
    P2pReq *q = user;
    Player *p = q->owner;
    unsigned gen = q->gen, lifetime = q->lifetime;
    free(q);
    if (p != g_live || lifetime != p->gen || gen != p->p2p_stats_gen || p->cur >= p->n_cands || !torrent_engine_url_is(p->cands[p->cur].url)) return;
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
static void leave_p2p_to(Player *p, PlayCandidate c)
{
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

/* TorBox found down earlier and its copy listed again (its cache list can answer while the
 * rest is down): opened quietly first, and only once it answers is P2P left for it */
typedef struct { Player *owner; unsigned gen, attempt; PlayCandidate c; } TbBack;
static void on_tb_back(const char *url, const char *error, void *user)
{
    TbBack *b = user;
    Player *p = b->owner;
    int current = p == g_live && b->gen == p->gen && !p->finished && b->attempt == p->attempt && p->cur < p->n_cands && torrent_engine_url_is(p->cands[p->cur].url);
    if (current && url) {
        char name[40];
        streams_service(b->c.url, b->c.label, name, sizeof name);
        for (int k = 0; k < p->n_svc; k++) if (!strcmp(p->svc[k].name, name)) p->svc[k].down = p->svc[k].fails = 0;
        LOGI("player: %s answers again; switching at the next seek or stall", name);
        if (p->tb_waiting) cand_drop(&p->tb_wait);
        p->tb_wait = b->c; p->tb_waiting = 1;
        free(b);
        return;
    }
    if (current) LOGI("player: TorBox still not answering (%s); staying on P2P", error ? error : "?");
    cand_drop(&b->c);
    free(b);
}

static int svc_down(const Player *p, const char *url, const char *label)
{
    char name[40];
    streams_service(url, label, name, sizeof name);
    for (int k = 0; k < p->n_svc; k++) if (!strcmp(p->svc[k].name, name)) return p->svc[k].down;
    return 0;
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
        PlayCandidate c;
        if (!streams_candidate(&p->meta, s, &c)) continue;
        if (torbox_is_hash_link(c.url) && svc_down(p, c.url, c.label)) {
            TbBack *b = malloc(sizeof *b);
            b->owner = p; b->gen = p->gen; b->attempt = p->attempt; b->c = c;
            EpisodeHint ep;
            app_episode_hint(p->meta.id, p->meta.name, p->video_id, &ep);
            torbox_open_hash(c.url, &ep, on_tb_back, b);
            return;
        }
        VideoInfo vi; video_info(&vi);
        if (p->started && p->stalls_here == 0 && vi.state == VIDEO_PLAYING && !vi.buffering && !getenv("SNTV_P2P_SWITCH_NOW")) {   /* playing well: no reload now */
            if (p->tb_waiting) cand_drop(&p->tb_wait);
            p->tb_wait = c; p->tb_waiting = 1;
            LOGI("player: TorBox has it again; staying on P2P while it plays well, switching at the next seek or stall");
            return;
        }
        leave_p2p_to(p, c);
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
        q->owner = p; q->gen = ++p->p2p_stats_gen; q->lifetime = p->gen;
        HttpOptions o = { .timeout_s = 3 };
        http_request_async_ex("http://127.0.0.1:11490/stats.json", &o, on_p2p_stats, q);
    }
    if (vi->state != VIDEO_PLAYING && vi->state != VIDEO_PAUSED) return;
    if (p->tb_waiting && (g_app.time - p->jumped_at < 1 || (vi->buffering && p->stall_since > 0 && g_app.time - p->stall_since > 2) || vi->state == VIDEO_PAUSED)) {
        PlayCandidate c = p->tb_wait; p->tb_waiting = 0;   /* a seek, a stall or a pause: the moment to go back to the cached copy */
        leave_p2p_to(p, c);
        return;
    }
    if (p->tb_waiting) return;   /* nothing to ask again for */
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
    playback_watch_update(&p->watch, vi.position, dt, RATES[p->rate_idx],
                          vi.state == VIDEO_PLAYING && !vi.buffering && !video_paused_on_purpose(), p->seek_commit > 0);
    if (!p->trailer) perf_playback_sample(vi.position, dt, vi.state == VIDEO_PLAYING, vi.buffering, vi.dropped_frames, p->own_load, vi.seek_version);
    if (p->lb_side && g_app.living_letterbox != LB_OFF && vi.state == VIDEO_PLAYING) {   /* the bars change at the next cut: a frame then, none in between */
        if (p->lb_next_t > 0 && vi.position >= p->lb_next_t - 0.02) { p->lb_next_t = 0; app_redraw(); }
        if (!playback_needs_bandwidth(&vi)) lb_work_ahead(p, &vi);
    }
    if (gfx_handheld()) video_pip_allow(app_top() == s && !p->pip && vi.state == VIDEO_PLAYING);   /* Home takes a playing video along */
    picture_tap_settle(p);
    ph_hold_fast(p, &vi);
    emb_tick(p);
    if (p->tap_pending || (p->tch == 2 && !p->tch_moved)) app_redraw();
    if (vi.state == VIDEO_PLAYING) {   /* progress bar / subtitles move */
        if (!bare_playback(p, &vi)) app_redraw();
        else if (sub_signature(p, vi.position) != p->sub_sig || g_app.time - p->drawn_at >= 0.25f) app_redraw();   /* a new line now; anything else four times a second */
    }
    int first_frame = vi.first_frame;
#ifndef SNTV_PLATFORM_ANDROID
    first_frame |= vi.state == VIDEO_PLAYING && !vi.buffering && vi.position > p->start_at + 0.05;
#endif
    if (first_frame && !p->start_timed) { p->start_timed = 1; perf_start_done(1); }
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
    if (p->seek_commit > 0) { p->seek_commit -= dt; if (p->seek_commit <= 0) player_seek(p->seek_target); }

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
#ifdef SNTV_DESKTOP_PLAYER
    else if (vi.state == VIDEO_LOADING && !p->auto_selected) {   /* the file's list is out before its first picture: chosen now, it reads the right ones from the start (chosen after, it would start again) */
        VideoTrack t[MAX_TRACKS];
        if (video_tracks(TRACK_AUDIO, t, MAX_TRACKS) > 0 || video_tracks(TRACK_TEXT, t, MAX_TRACKS) > 0) auto_select_tracks(p);
    }
#endif
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
    if (p->started && !p->length_checked && vi.duration > 0 && !p->error_handled) {
        p->length_checked = 1;
        char why[160];
        if (error_clip(p, vi.duration, why, sizeof why)) {
            LOGW("player: source %d is %s; the next one", p->cur + 1, why);
            p->error_handled = 1;
            fail_over(p, why);
        }
    }
    if (p->bridge == 1 && p->started) { p->bridge = 2; LOGI("player: the picture came %.1f s after the still", g_app.time - p->bridge_t0); }
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
    if (!p->thumbs_warm && p->started && vi.state == VIDEO_PLAYING && !playback_needs_bandwidth(&vi) && !p->trailer && vi.duration > 120
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
        double silent = p->relay_used && !p2p ? relay_buffer_waiting_s() : -1;   /* asked for, and not a byte came */
        if (silent > env_s("SNTV_FIRST_BYTE", 12)) { LOGI("player: source %d sent nothing in %.0fs; the next one", p->cur + 1, silent); waited = limit + 1; }
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
    if (!playback_needs_bandwidth(&vi)) {
        if (!p->probe_asked && (vi.state == VIDEO_PLAYING || vi.state == VIDEO_PAUSED)) probe_current(p);
        if (!p->hash_state && (vi.state == VIDEO_PLAYING || vi.state == VIDEO_PAUSED)) hash_start(p);
        opensubs_tick(p);
        bestmatch_tick(p, &vi);
        autosync_tick(p);
        xl_tick(p, dt, vi.position);
    }
    fallback_tick(p, &vi);
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
    double credits = skip_credits_start(p->skip, p->n_skip, vi.duration);
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
    if (p->next_video_id && want_next && !playback_needs_bandwidth(&vi) && streams_prefetch_progress(p->next_video_id) < 0
        && (!p->prefetched || g_app.time - p->prefetch_tried > 30)) {
        p->prefetched = 1;
        p->prefetch_tried = g_app.time;
        streams_prefetch(&p->meta, p->next_video_id);
    }
    if (p->next_video_id && !p->next_warmed && vi.duration > 0 && vi.position > vi.duration - 600 && vi.state == VIDEO_PLAYING && !vi.buffering
        && (vi.buffer_ahead < 0 || vi.buffer_ahead > 30))   /* never at this video's expense */
        p->next_warmed = streams_prefetch_warm(p->next_video_id, p->cands[p->cur].binge_group);
    next_progress_update(p, dt);
    if (fabs(vi.position - p->pos_seen) > 3 + dt * 4 || p->seek_commit > 0) { if (p->drain_n) LOGI("player: a jump to %.0fs; the drain samples start afresh", vi.position); p->jumped_at = g_app.time; p->jump_pos = vi.position; p->drain_n = 0; p->drain_t = g_app.time; }   /* seeking, or just sought: the drain samples start afresh */
    p->pos_seen = vi.position;
    int pp = pipeline_paused(p, &vi), stalled = vi.buffering || pp;
    int settled = g_app.time - p->jumped_at > 8 && (vi.state == VIDEO_PLAYING || pp);
    if (stalled && !p->was_buffering && vi.state != VIDEO_LOADING) { p->stalls++; if (settled) p->stalls_here++; p->stall_since = g_app.time; }
    /* it keeps stopping to buffer: offer the best source below this one's resolution, once */
    if (!p->lighter_offered && !p->trailer && p->n_cands > 1 && vi.state != VIDEO_LOADING
        && (p->stalls_here >= 3 || (settled && stalled && p->stall_since > 0 && g_app.time - p->stall_since > 10))) {
        int cur = p->cands[p->cur].res ? p->cands[p->cur].res : 1081, best = -1;
        for (int i = 0; i < p->n_cands; i++)
            if (i != p->cur && p->cands[i].res > 0 && p->cands[i].res < cur && (best < 0 || p->cands[i].res > p->cands[best].res)) best = i;
        p->lighter_offered = 1;
        p->lighter_to = best;
        for (int i = 0; i < p->n_cands; i++) LOGI("player: source %d %s %dp", i + 1, p->cands[i].label ? p->cands[i].label : "", p->cands[i].res);
        if (best >= 0) { LOGI("player: %d stalls; offering source %d (%dp)", p->stalls_here, best + 1, p->cands[best].res); app_redraw(); }
        else LOGI("player: %d stalls; no source below %dp to offer", p->stalls_here, p->cands[p->cur].res);
    }
    if (!stalled && p->was_buffering && p->stall_since > 0) { p->stall_s += g_app.time - p->stall_since; p->stall_since = 0; }
    p->was_buffering = stalled;
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
    if (p->emb_app && !p->track_loaded) {   /* the film's own, drawn here (its timing nudged as the TV's would be) */
        p->emb.offset = p->embedded_offset;
        text = subs_text_at(&p->emb, vi->position, 0, b1, sizeof b1) ? b1 : NULL;
        top = subs_text_at(&p->emb, vi->position, 1, up, sizeof up) ? up : NULL;
    }
    static char vb[1024];
    if (!text && !p->track_loaded && video_subtitle_text(vb, sizeof vb)) text = vb;   /* the desktop player's, from inside the file */
    float bottom = g_app.subs_pos == 2 ? 260 : g_app.subs_pos == 1 ? SNTV_UI_H - 220 : SNTV_UI_H - 96;
    /* the controls up: a phone's, a computer's (their own) or the TV's; the lines go above them */
    int ctl = TOUCH_UI ? p->fs_k > 0.01f && !p->ph_locked : g_app.time < p->overlay_until && !subs_tuning(p);
#ifdef SNTV_DESKTOP_PLAYER
    float clear = SNTV_UI_H - 250;   /* the timeline and the row */
#else
    float clear = TOUCH_UI ? SNTV_UI_H - 360 : SNTV_UI_H - 250;   /* a phone: the timeline and the labelled row under it */
#endif
    video_subtitle_lift(TOUCH_UI && ctl && g_app.subs_pos != 2 ? (SNTV_UI_H - clear) / SNTV_UI_H : 0);   /* the file's own, where the player draws them (Android) */
    if (top) {   /* the file asks for the top ({\an8}): over a credit or a sign at the bottom */
        int lines = 1;
        for (const char *c = top; *c; c++) lines += *c == '\n';
        float lh = ui_subtitle_line_h(sub_size(p)), tb = (ctl ? 150 : 72) + lines * lh;   /* under the title while it shows */
        if (g_app.subs_pos == 2 && bottom < tb + 24 + lh) bottom = tb + 24 + lh * 2;   /* yours at the top too: under it */
        ui_subtitle(top, tb, sub_size(p), g_app.subs_style);
    }
    if (g_app.subs_pos == 0 && g_app.subs_in_bars && p->lb_side == 1 && p->lb_bar >= 90) bottom = SNTV_UI_H - p->lb_bar * 0.2f;   /* in the bar under a scope picture */
    if (g_app.subs_pos != 2 && ctl && bottom > clear) bottom = clear;   /* above the controls */
    int r = subs_tuning(p) ? p->subs_row : -1;
    if (r >= SUB_TIMING && !subs_timing_supported(p)) r++;
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
        const char *how = TOUCH_UI ? "" : "  \xC2\xB7  Left/Right to change";
        if (p->rate_idx == RATE_NORMAL) snprintf(sp, sizeof sp, "Normal%s", how);
        else snprintf(sp, sizeof sp, "%gx%s", RATES[p->rate_idx], how);
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
        p->card_thumb[0] = x + 20; p->card_thumb[1] = y + 20; p->card_thumb[2] = 256; p->card_thumb[3] = 144; p->card_at = g_app.time;
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
    p->card_thumb[0] = x + 16; p->card_thumb[1] = y + 16; p->card_thumb[2] = 256; p->card_thumb[3] = 144; p->card_at = g_app.time;
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
    if (!TOUCH_UI) font_draw(hint, SNTV_UI_W - THEME_MARGIN - font_measure(hint, FONT_XS), y + (d - font_height(FONT_XS)) / 2, FONT_XS, THEME_TEXT_MUTED);
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
    int tw, th; float ta;
    if (video_frame_texture(&tw, &th, &ta)) { gfx_video_hole(x, y, w, h); return; }   /* the desktop player's own picture */
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
    int fw, fh;
    unsigned ft = p->lb_side == 1 && p->lb_last[0] ? image_peek(p->lb_last, &fw, &fh) : 0;
    if (ft) {   /* a scope film: its own frame between black bars, as the TV shows it */
        float box[4] = { 0, 0, 1, 1 };
        image_content_box(p->lb_last, box);
        gfx_rect(x, y, w, h, RGBA(0, 0, 0, 0xFF));
        gfx_tex_uv(ft, x, y + p->lb_bar, w, h - 2 * p->lb_bar, box[0], box[1], box[2], box[3], RGBA(255, 255, 255, 255));
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
        player_seek(t[p->scenes_focus - 1]);
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

/* "Show  -  S2 E3 - Title" (or "S2E3  Title") -> "S2E3" and "Title"; 0 when it is no episode */
static int episode_parts(const Player *p, char *code, size_t cn, const char **name)
{
    const char *et = p->title ? strstr(p->title, "  -  ") : NULL;
    et = et ? et + 5 : p->title;
    if (!et || et[0] != 'S' || et[1] < '0' || et[1] > '9') return 0;
    const char *q = et + 1;
    while (*q >= '0' && *q <= '9') q++;
    size_t sn = (size_t)(q - et);
    while (*q == ' ') q++;
    if (*q != 'E' || q[1] < '0' || q[1] > '9') return 0;
    const char *e = q++;
    while (*q >= '0' && *q <= '9') q++;
    snprintf(code, cn, "%.*s%.*s", (int)sn, et, (int)(q - e), e);
    while (*q == ' ' || *q == '-' || *q == ':') q++;
    if (!strncmp(q, "\xC2\xB7", 2)) { q += 2; while (*q == ' ') q++; }
    *name = q;
    return 1;
}

static void draw_veil(Player *p, const VideoInfo *vi, float k)
{
    float W = SNTV_UI_W, H = SNTV_UI_H, cx = W / 2;
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
    char ep[200] = "", code[24];   /* "S2E3  ·  Title" */
    const char *q;
    if (episode_parts(p, code, sizeof code, &q)) snprintf(ep, sizeof ep, *q ? "%s  \xC2\xB7  %s" : "%s", code, q);
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

#define BRIDGE_GROW_S 0.45f   /* the card's picture opening to the screen */
#define BRIDGE_WAIT_S 6.0f    /* longer than this: the loading card's words, what is being done */

/* On from the last episode: this one's still, out of the Up Next card to the whole screen,
 * its name low at the left and a thin line moving under it. No spinner, no loading card:
 * the picture dissolves in under it (the veil fading). A long wait brings the card back. */
static void draw_bridge(Player *p, const VideoInfo *vi, float k)
{
    float W = SNTV_UI_W, H = SNTV_UI_H, t = g_app.time - p->bridge_t0;
    char buf[512];
    const char *still = app_video_thumbnail(p->meta.id, p->video_id);
    if (!still || image_failed(still)) still = app_art_background(&p->meta, buf, sizeof buf);
    int iw = 0, ih = 0;
    unsigned tex = still ? image_get_ex(still, IMAGE_MIPMAP, &iw, &ih, NULL) : 0;
    float g = p->bridge_from ? t / BRIDGE_GROW_S : 1;
    if (g > 1) g = 1;
    g = 1 - (1 - g) * (1 - g) * (1 - g);
    gfx_rect(0, 0, W, H, RGBA(0, 0, 0, (int)(255 * k)));
    if (tex && ih > 0) {
        float u0, v0, u1, v1;
        ui_cover_uv(iw, ih, W, H, &u0, &v0, &u1, &v1);
        if (g < 1) gfx_tex_blur(tex, 0, 0, W, H, u0, v0, u1, v1, RGBA(120, 120, 120, (int)(255 * k * g)), 5.0f);   /* its own colours fill in behind */
        float z = 1.0f + 0.035f * (t > BRIDGE_GROW_S ? (t - BRIDGE_GROW_S < 10 ? (t - BRIDGE_GROW_S) / 10 : 1) : 0);   /* then a slow push in */
        float cu = (u0 + u1) / 2, cv = (v0 + v1) / 2;
        u0 = cu - (cu - u0) / z; u1 = cu + (u1 - cu) / z; v0 = cv - (cv - v0) / z; v1 = cv + (v1 - cv) / z;
        const float *r = p->bridge_rect;
        float x = r[0] * (1 - g), y = r[1] * (1 - g), w = r[2] + (W - r[2]) * g, h = r[3] + (H - r[3]) * g;
        if (!p->bridge_from) { x = 0; y = 0; w = W; h = H; }
        gfx_rtex_uv(tex, x, y, w, h, R_CARD * (1 - g), u0, v0, u1, v1, RGBA(255, 255, 255, (int)(255 * k)), 0);
    }
    float c = (t - BRIDGE_GROW_S * (p->bridge_from ? 0.6f : 0)) / 0.4f;
    c = (c < 0 ? 0 : c > 1 ? 1 : c) * k;
    if (c > 0) {   /* which episode this is, low at the left */
        gfx_gradient_v(0, H * 0.5f, W, H * 0.5f + 1, RGBA(0, 0, 0, 0), RGBA(0, 0, 0, (int)(0xC0 * c)));
        float x = THEME_MARGIN, y = H - 250;
        char code[24];
        const char *name = NULL;
        if (episode_parts(p, code, sizeof code, &name)) {
            ui_spaced_caps(code, x, y, FONT_XS | FONT_B, 4, COLOR_ALPHA(THEME_ACCENT, (int)(255 * c)), 600);
            y += 40;
            if (*name) { font_draw_shadow(name, x, y, FONT_L | FONT_B, COLOR_ALPHA(THEME_TEXT, (int)(255 * c)), RGBA(0, 0, 0, (int)(0x90 * c))); y += 62; }
        }
        if (p->meta.name) { font_draw(p->meta.name, x, y, FONT_S, COLOR_ALPHA(RGBA(0xE6, 0xE6, 0xE6, 0xFF), (int)(220 * c))); y += 48; }
        float lw = 360, seg = 96, ph = fmodf(t * 0.7f, 1.0f);   /* coming: a short line running along a faint one */
        gfx_rect(x, y + 6, lw, 3, RGBA(255, 255, 255, (int)(0x26 * c)));
        float sx = x - seg + (lw + seg) * ph, s0 = sx < x ? x : sx, s1 = sx + seg > x + lw ? x + lw : sx + seg;
        if (s1 > s0) gfx_rect(s0, y + 6, s1 - s0, 3, COLOR_ALPHA(THEME_ACCENT, (int)(255 * c)));
    }
    if (t > BRIDGE_WAIT_S) {   /* it is taking a while: the card, with what is being done about it */
        float w = (t - BRIDGE_WAIT_S) / 0.6f;
        draw_veil(p, vi, k * (w > 1 ? 1 : w));
    }
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
    if (TOUCH_UI && !p->pip) {   /* a phone sideways: its own touch controls over the picture */
        float nk = 0;
#ifdef SNTV_DESKTOP_PLAYER
        nk = desk_next_k(p);
        if (nk > 0) {   /* the credits: the film steps back to the top left */
            float r[4];
            desk_next_rect(nk, r);
            gfx_rect(0, 0, SNTV_UI_W, SNTV_UI_H, RGBA(0, 0, 0, 0xFF));
            draw_video_area(p, r[0], r[1], r[2], r[3]);
        } else
#endif
        draw_video_area(p, 0, 0, SNTV_UI_W, SNTV_UI_H);
        if (!video_in_pip() && nk <= 0) draw_letterbox(p, &vi);
        if (p->veil > 0 && !video_in_pip()) {   /* opening: the loading card, as on a TV, and Back to leave */
            if (p->bridge) draw_bridge(p, &vi, p->veil); else draw_veil(p, &vi, p->veil);
            if (p->veil > 0.5f && !p->ph_sheet) { full_draw_loading(p); return; }
        }
        draw_subtitle(p, &vi);
        if (p->stats && !video_in_pip()) draw_stats(p, &vi);
        if (!video_in_pip()) full_draw(p, &vi);   /* in picture in picture: the picture alone */
        return;
    }
    p->drawn_at = g_app.time;
    p->sub_sig = sub_signature(p, vi.position);
    ui_hit(0, 0, SNTV_UI_W, SNTV_UI_H, 0x7FFFFF00);   /* a tap anywhere is OK, as before */
    draw_video_area(p, 0, 0, SNTV_UI_W, SNTV_UI_H);
    if (!p->pip) draw_letterbox(p, &vi);
    if (p->veil > 0 && !p->pip) {
        if (p->bridge) draw_bridge(p, &vi, p->veil); else draw_veil(p, &vi, p->veil);
        if (p->veil > 0.5f && !p->menu) return;
    }
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
        float t0 = p->seek_flash - 0.35f;   /* each press bounces it the way it seeks */
        cx += ui_spring(t0, 18 * p->seek_flash_dir);
        int a8 = (int)(255 * (k > 1 ? 1 : k));
        gfx_circle(cx, cy, 56 + ui_spring(t0, 7), RGBA(0, 0, 0, (int)(0x80 * k)));
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
    float oe = 1;
    if (!overlay || g_app.fx_light) p->ov_k = overlay;
    else {
        float step = g_app.time - p->ov_kt;
        if (p->ov_k < 1) { p->ov_k += step / 0.22f; if (p->ov_k > 1) p->ov_k = 1; app_redraw(); }
        oe = 1 - (1 - p->ov_k) * (1 - p->ov_k) * (1 - p->ov_k);
    }
    p->ov_kt = g_app.time;
    if (overlay) {
        gfx_set_alpha(oe);
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
    gfx_set_alpha(oe * (1 - p->rest_t));   /* the controls step aside */
    if (overlay) gfx_set_translate(0, 22 * (1 - oe));
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
    gfx_set_translate(0, 0);
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
    if (lang) snprintf(title, tn, "%s", lang_display_english(lang));
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
static int subs_timing_supported(const Player *p)
{
#ifdef SNTV_PLATFORM_ANDROID
    return p->track_loaded || p->emb_app;
#else
    (void)p;
    return 1;
#endif
}

static void shift_subs(Player *p, double by)
{
    if (!subs_timing_supported(p)) { app_toast("Choose a subtitle file to adjust its timing"); return; }
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
    for (int r = SUB_CHOOSE; r <= SUB_SYNC; r++) {
        if (r == SUB_TIMING && !subs_timing_supported(p)) continue;
        g[SG_LOOK].it[g[SG_LOOK].n++] = (SubItem){ SI_SET, r, r };
    }
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
    MIX(subs_timing_supported(p));
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
        if (!subs_timing_supported(p)) snprintf(v, n, "Choose a subtitle file");
        else if (p->track_loaded) snprintf(v, n, "%+.1f s", p->track.offset);
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
    int timing = subs_timing_supported(p);
    const float W = 460, RH = 84, H = 110 + (SUB_SYNC + timing) * RH + 56, X = SNTV_UI_W - 48 - W, Y = (SNTV_UI_H - H) / 2, PAD = 28;
    gfx_set_alpha(e);
    gfx_set_translate(60 * (1 - e), 0);
    gfx_rrect(X, Y, W, H, 32, RGBA(0x15, 0x16, 0x1A, 0xE8));
    font_draw_tracked("LOOK AND TIMING", X + PAD, Y + 30, FONT_XS | FONT_B, COLOR_ALPHA(THEME_TEXT, 0x73), 1.5f);
    font_draw("Subtitles", X + PAD, Y + 56, FONT_M | FONT_B, THEME_TEXT);
    for (int r = 0, shown = 0; r <= SUB_SYNC; r++) {
        if (r == SUB_TIMING && !timing) continue;
        float y = Y + 110 + shown * RH, cy = y + (RH - 8) / 2;
        int foc = shown++ == p->subs_row, adj = r != SUB_SYNC;
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
    if (!TOUCH_UI) font_draw_fit("Up/Down  setting   \xC2\xB7   Left/Right  change   \xC2\xB7   Back", X + PAD, Y + H - 44, FONT_XS, THEME_TEXT_MUTED, W - 2 * PAD);
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
    if (!TOUCH_UI) font_draw_fit(hint, cx, SY + SH - 44, FONT_XS, THEME_TEXT_MUTED, SW - 2 * PAD);
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
        player_seek(target);
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
enum { PH_NONE, PH_AUDIO, PH_SUBS, PH_SOURCES, PH_EPISODES, PH_SPEED, PH_TRACKS, PH_MORE, PH_SUBSET, PH_SCENES, PH_CAST };
/* what the More list and the subtitle settings do (their rows' act) */
enum { MO_PREV, MO_MARK, MO_SCENES, MO_CAST, MO_SUBSET, MO_RELOAD, MO_STATS };
enum { SS_FOR, SS_SIZE, SS_STYLE, SS_POS, SS_EARLIER, SS_LATER, SS_RESET, SS_SYNC };
#define TRACKS_SET 0x20000   /* PH_TRACKS: the subtitle settings row, at the end */
#define SRC_MORE (-2)        /* PH_SOURCES: find more sources (or browse them all) */
#define TRACKS_SUB 0x10000   /* PH_TRACKS: a subtitles row (the audio rows come first) */
enum { PHH_BACK = 0x7FFF0000, PHH_PLAY, PHH_BACK10, PHH_FWD10, PHH_SKIP, PHH_NEXT, PHH_STILL_ON, PHH_STILL_STOP, PHH_CLOSE,
       PHH_PREV_EP, PHH_NEXT_EP, PHH_ASPECT, PHH_PANEL, PHH_TRACKS, PHH_MUTE, PHH_SZ_DN, PHH_SZ_UP, PHH_TM_DN, PHH_TM_UP, PHH_CREDITS, PHH_LOCK, PHH_UNLOCK, PHH_MORE, PHH_SHEET = 0x7FFE0000, PHH_ROW = 0x7FFD0000 };
enum { PHB_SUBS, PHB_AUDIO, PHB_SPEED, PHB_SOURCES, PHB_EPISODES, PHB_PIP, PHB_COUNT };
#define PH_ROWS 72

typedef struct { char label[120], sub[120]; int on, act; } PhRow;

static void ph_time(double t, char *out, size_t n);
static double ph_dur(const Player *p, const VideoInfo *vi);
static void touch_seek(Player *p, double t, float after);
static void ph_neighbour(Player *p, int dir);
static void ph_open(Player *p, int sheet);

static int ph_rows(Player *p, PhRow *r)
{
    int n = 0;
    VideoTrack tr[MAX_TRACKS];
    if (p->ph_sheet == PH_TRACKS) {   /* a computer's one panel: the audio tracks, then the subtitles */
        PhRow sub[PH_ROWS];
        p->ph_sheet = PH_AUDIO; n = ph_rows(p, r);
        p->ph_sheet = PH_SUBS; int ns = ph_rows(p, sub);
        p->ph_sheet = PH_TRACKS;
        for (int i = 0; i < ns && n < PH_ROWS; i++, n++) { r[n] = sub[i]; r[n].act |= TRACKS_SUB; }
        if (n < PH_ROWS) { snprintf(r[n].label, sizeof r[n].label, "Subtitle settings"); snprintf(r[n].sub, sizeof r[n].sub, "Size, style, position, timing, auto-sync, a second language"); r[n].on = 0; r[n].act = TRACKS_SUB | TRACKS_SET; n++; }
        return n;
    }
    if (p->ph_sheet == PH_SUBSET) {   /* as the TV's subtitles menu has them */
        static const char *const NAME[] = { "Subtitles for", "Size", "Style", "Position", "Earlier by 0.1 s", "Later by 0.1 s", "Reset the timing", "Auto-sync" };
        static const int ROW[] = { SUB_CHOOSE, SUB_SIZE, SUB_STYLE, SUB_POS, SUB_TIMING, SUB_TIMING, SUB_TIMING, SUB_SYNC };
        for (int i = 0; i <= SS_SYNC; i++) {
            if ((i == SS_EARLIER || i == SS_LATER || i == SS_RESET) && !subs_timing_supported(p)) continue;
            snprintf(r[n].label, sizeof r[n].label, "%s", NAME[i]);
            subs_setting(p, ROW[i], r[n].sub, sizeof r[n].sub);
            if (i == SS_EARLIER || i == SS_LATER || i == SS_RESET) { char v[64]; snprintf(v, sizeof v, "%s", r[n].sub); snprintf(r[n].sub, sizeof r[n].sub, "Timing now %s", v); }
            r[n].on = i == SS_SYNC && g_app.subs_autosync; r[n].act = i;
            n++;
        }
        return n;
    }
    if (p->ph_sheet == PH_MORE) {   /* what the TV's row has beyond play, the lists and the shape */
        const TmdbInfo *tm;
        struct { int act, show; const char *label, *sub; } m[] = {
            { MO_PREV, p->prev_video_id != NULL, "Previous episode", p->prev_title ? p->prev_title : "" },
            { MO_MARK, 1, "Mark this moment", "It shows in Scenes, to come back to" },
            { MO_SCENES, !p->trailer, "Scenes", "The chapters and your marked moments" },
            { MO_CAST, !p->trailer && cast_n(p, &tm) > 0, "Cast", "Who is in it" },
            { MO_SUBSET, 1, "Subtitle settings", "Size, style, position, timing, auto-sync" },
            { MO_RELOAD, 1, "Reload the video", "From where it is" },
            { MO_STATS, 1, p->stats ? "Hide stats" : "Show stats", "The stream, the picture and the buffer" },
        };
        for (size_t i = 0; i < sizeof m / sizeof m[0] && n < PH_ROWS; i++) {
            if (!m[i].show) continue;
            snprintf(r[n].label, sizeof r[n].label, "%s", m[i].label); snprintf(r[n].sub, sizeof r[n].sub, "%s", m[i].sub);
            r[n].on = m[i].act == MO_STATS && p->stats; r[n].act = m[i].act; n++;
        }
        return n;
    }
    if (p->ph_sheet == PH_SCENES) {
        VideoInfo vi; video_info(&vi);
        double t[MAX_SCENES]; int kind[MAX_SCENES], ch = 0;
        int k = scenes_list(p, ph_dur(p, &vi), t, kind, MAX_SCENES), now = scene_now(p, vi.position) - 1;
        for (int i = 0; i < k && n < PH_ROWS; i++, n++) {
            char at[24]; ph_time(t[i], at, sizeof at);
            const char *cname = NULL;
            if (!kind[i]) { int c = chapter_at(p, t[i] + 0.01); cname = c >= 0 && p->probe->chapters[c].name[0] ? p->probe->chapters[c].name : NULL; ch++; }
            if (kind[i]) snprintf(r[n].label, sizeof r[n].label, "Marked moment");
            else if (cname) snprintf(r[n].label, sizeof r[n].label, "%s", cname);
            else snprintf(r[n].label, sizeof r[n].label, "Scene %d", ch);
            snprintf(r[n].sub, sizeof r[n].sub, "%s", at);
            r[n].on = i == now; r[n].act = i;
        }
        return n;
    }
    if (p->ph_sheet == PH_CAST) {
        const TmdbInfo *tm;
        int k = cast_n(p, &tm);
        for (int i = 0; i < k && n < PH_ROWS; i++, n++) {
            const char *name = "", *role = "", *photo = NULL;
            cast_person(p, tm, i, &name, &role, &photo);
            snprintf(r[n].label, sizeof r[n].label, "%s", name ? name : ""); snprintf(r[n].sub, sizeof r[n].sub, "%s", role ? role : "");
            r[n].on = 0; r[n].act = i;
        }
        return n;
    }
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
        if (!p->trailer && n < PH_ROWS) {   /* as the TV's Sources panel: ask every addon again, or see them all */
            snprintf(r[n].label, sizeof r[n].label, "%s", p->more_state == 2 ? "Browse every source" : p->more_state == 1 ? "Asking every addon\xE2\x80\xA6" : "Find more sources");
            snprintf(r[n].sub, sizeof r[n].sub, "%s", p->more_state == 2 ? "The full list, by addon" : "Ask every addon again");
            r[n].on = 0; r[n].act = SRC_MORE; n++;
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
    if (sheet == PH_TRACKS) {
        PhRow one = *r;
        p->ph_sheet = one.act & TRACKS_SUB ? PH_SUBS : PH_AUDIO;
        one.act &= ~TRACKS_SUB;
        ph_pick(p, &one);
        if (p->ph_sheet != PH_NONE) p->ph_sheet = PH_TRACKS;
        return;
    }
    if (sheet == PH_TRACKS && (r->act & TRACKS_SET)) { ph_open(p, PH_SUBSET); return; }
    if (sheet == PH_SPEED) { set_rate(p, r->act); return; }
    if (sheet == PH_SOURCES && r->act == SRC_MORE) {
        if (p->more_state == 2) { p->ph_sheet = PH_NONE; browse_sources(p); }
        else if (p->more_state == 0) find_more_sources(p);
        return;
    }
    if (sheet == PH_SUBSET) {
        static const int ROW[] = { SUB_CHOOSE, SUB_SIZE, SUB_STYLE, SUB_POS, 0, 0, SUB_TIMING, SUB_SYNC };
        if (r->act == SS_EARLIER || r->act == SS_LATER) { shift_subs(p, r->act == SS_LATER ? 0.1 : -0.1); return; }
        p->menu = MENU_SUBS; p->menu_focus = ROW[r->act];
        menu_activate(p);
        p->menu = MENU_NONE;
        return;
    }
    if (sheet == PH_CAST) return;   /* who is in it, to read */
    if (sheet == PH_SCENES) {
        VideoInfo vi; video_info(&vi);
        double t[MAX_SCENES]; int kind[MAX_SCENES];
        int k = scenes_list(p, ph_dur(p, &vi), t, kind, MAX_SCENES);
        if (r->act >= 0 && r->act < k) { touch_seek(p, t[r->act], 0.01f); LOGI("player: scene at %.0f s", t[r->act]); }
        p->ph_sheet = PH_NONE;
        return;
    }
    if (sheet == PH_MORE) {
        VideoInfo vi; video_info(&vi);
        switch (r->act) {
        case MO_PREV: p->ph_sheet = PH_NONE; ph_neighbour(p, -1); return;
        case MO_MARK: mark_moment(p, vi.position); return;
        case MO_SCENES: ph_open(p, PH_SCENES); return;
        case MO_CAST: ph_open(p, PH_CAST); return;
        case MO_SUBSET: ph_open(p, PH_SUBSET); return;
        case MO_RELOAD: p->ph_sheet = PH_NONE; app_toast("Reloading"); start_candidate(p, p->cur, resume_spot(p, &vi)); return;
        case MO_STATS: p->stats = !p->stats; p->ph_sheet = PH_NONE; return;
        }
        return;
    }
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
    static const char *const LIST[] = { "", "audio", "subtitles", "sources", "episodes", "speed", "audio & subtitles", "more", "subtitle settings", "scenes", "cast" };
    LOGI("player: the %s list", LIST[sheet]);
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

#ifdef SNTV_DESKTOP_PLAYER
/* A computer: paused for a few seconds with the mouse still, the controls step aside for a
 * card of what this is (as the streaming apps do); moving the mouse brings them back. */
static int desk_resting(const Player *p, const VideoInfo *vi)
{
    return vi->state == VIDEO_PAUSED && p->paused_since > 0 && g_app.time - p->paused_since > env_s("SNTV_REST_S", 5.0f) && g_app.time >= p->fs_until && g_app.time >= p->overlay_until
        && !p->ph_sheet && !p->tch && !p->still_asking && !error_final(p, vi);
}
#endif

/* Full screen on a phone: the controls show for a while after a tap; always while paused,
 * starting, a list is open or the source failed. */
static int fs_shown(const Player *p, const VideoInfo *vi)
{
#ifdef SNTV_DESKTOP_PLAYER
    if (g_app.time < p->overlay_until) return 1;   /* a key used: they show as they would for the mouse */
    if (desk_resting(p, vi)) return 0;   /* paused a while and nobody moving: the card instead */
#endif
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
    if (g_app.live_scrub && g_app.time - p->last_live_seek > 0.33f) { p->last_live_seek = g_app.time; player_seek(t); }
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
    static const char *const NAMES[] = { "", "Audio", "Subtitles", "Sources", "Episodes", "Speed", "Audio & subtitles", "More", "Subtitle settings", "Scenes", "Cast" };
    PhRow r[PH_ROWS];
    int n = ph_rows(p, r), na = 0;
    if (p->ph_sheet == PH_TRACKS) while (na < n && !(r[na].act & TRACKS_SUB)) na++;
    float rh = 112, hh = p->ph_sheet == PH_TRACKS ? 64 : 0, W = SNTV_UI_W;
#define ROW_Y(i) ((i) * rh + (hh > 0 ? (na > 0 ? hh : 0) + ((i) >= na ? hh : 0) : 0))   /* the audio and subtitles each under their heading */
    gfx_rrect(x - 20, top, w + 40, bottom - top, R_PANEL, COLOR_ALPHA(THEME_PANEL, 0xF4));
    ui_hit(x - 20, top, w + 40, bottom - top, PHH_PANEL);   /* the panel itself: a tap there is not a tap on the picture */
    if (titled) {
        font_draw(NAMES[p->ph_sheet], x + 12, top + 28, FONT_M | FONT_B, THEME_TEXT);
        ui_icon(ICON_CLOSE, x + w - 30, top + 52, 30, THEME_TEXT_DIM);
        ui_hit(x + w - 90, top + 8, 100, 90, PHH_CLOSE);
        top += 100;
    }
    if (p->ph_focus >= n) p->ph_focus = n ? n - 1 : 0;
    float view = bottom - top - 26, want = p->ph_scroll, fy = ROW_Y(p->ph_focus);
    if (fy + rh > want + view) want = fy + rh - view;
    if (fy < want) want = fy;
    float dt = g_app.time - p->ph_t;
    p->ph_t = g_app.time;
    ui_pan(&p->ph_pan, &p->ph_scroll, want, ROW_Y(n) - view, dt < 0 ? 0 : dt > 0.1f ? 0.1f : dt, 16);
    ui_pan_area(&p->ph_pan, x - 20, top, w + 40, bottom - top, 1);
    gfx_clip_push(x - 20, top + 10, w + 40, bottom - top - 20);
    float ry0 = top + 16 - p->ph_scroll, ry;
    if (hh > 0) {   /* the headings */
        if (na > 0) font_draw("Audio", x + 12, ry0 + 14, FONT_S | FONT_B, THEME_TEXT_DIM);
        font_draw("Subtitles", x + 12, ry0 + ROW_Y(na) - hh + 14, FONT_S | FONT_B, THEME_TEXT_DIM);
    }
    if (!n) font_draw_center(p->ph_sheet == PH_SUBS ? "No subtitles for this video" : p->ph_sheet == PH_AUDIO ? "One audio track" : "Nothing here", x + w / 2, top + 60, FONT_S, THEME_TEXT_MUTED);
    for (int i = 0; i < n; i++) {
        ry = ry0 + ROW_Y(i);
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
#undef ROW_Y
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
    rect[0] = x - 30; rect[1] = y - 56; rect[2] = w + 60; rect[3] = 100;   /* a finger's width either side, clear of the row under it */
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
        cx += ui_spring(p->ripple_at, 20 * p->ripple_side);   /* the arrow and the seconds bounce the way it went */
        ui_icon(p->ripple_side < 0 ? ICON_PREV : ICON_NEXT, cx, cy - 30, 40 + ui_spring(p->ripple_at, 8), COLOR_ALPHA(THEME_TEXT, (int)(255 * (k < 0.5f ? k * 2 : 1))));
        font_draw_center(t, cx, cy + 10, FONT_S | FONT_B, COLOR_ALPHA(THEME_TEXT, (int)(255 * (k < 0.5f ? k * 2 : 1))));
        app_redraw();
    }
    if (p->tch_fast) {   /* held: double speed, at the top */
        const char *t = "2x  \xE2\x96\xB8\xE2\x96\xB8";
        float tw = font_measure(t, FONT_S | FONT_B) + 48;
        gfx_rrect(x + (w - tw) / 2, y + 40, tw, 60, 30, RGBA(0, 0, 0, 0xA0));
        font_draw_center(t, x + w / 2, y + 40 + (60 - font_height(FONT_S | FONT_B)) / 2, FONT_S | FONT_B, THEME_TEXT);
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

/* play/pause in the middle, 10 s back and on either side */
static void ph_transport(Player *p, const VideoInfo *vi, float cx, float cy, float gap)
{
    gfx_circle(cx, cy, 110, RGBA(0, 0, 0, 0x70));
    ui_icon(vi->state == VIDEO_PLAYING ? ICON_PAUSE : ICON_PLAY, cx + (vi->state == VIDEO_PLAYING ? 0 : 8), cy, 100, THEME_TEXT);
    ui_hit(cx - 120, cy - 120, 240, 240, PHH_PLAY);
    for (int d = -1; d <= 1; d += 2) {
        float x = cx + d * gap;
        gfx_circle(x, cy, 80, RGBA(0, 0, 0, 0x48));
        desk_skip_icon(x, cy, 42, d, THEME_TEXT, FONT_S | FONT_B);
        ui_hit(x - 90, cy - 90, 180, 180, d < 0 ? PHH_BACK10 : PHH_FWD10);
    }
    (void)p;
}

/* The bottom row: what there is to change, each with its name */
static float ph_row_btn(Icon icon, const char *label, float x, float cy, int hit, int on, int measure)
{
    float w = label ? 22 + 52 + 16 + font_measure(label, FONT_S | FONT_B) + 30 : 112;   /* no label: the icon alone */
    if (measure) return w;
    if (on) gfx_rrect(x, cy - 50, w, 100, 50, COLOR_ALPHA(THEME_TEXT, 0x24));
    ui_icon(icon, label ? x + 22 + 26 : x + w / 2, cy, 50, on ? THEME_ACCENT : THEME_TEXT);
    if (label) font_draw(label, x + 22 + 52 + 16, cy - font_height(FONT_S | FONT_B) / 2, FONT_S | FONT_B, THEME_TEXT);
    ui_hit(x, cy - 54, w, 108, hit);
    return w;
}

static void ph_row(Player *p, float cx, float cy, float room)
{
    char sp[24];
    snprintf(sp, sizeof sp, p->rate_idx == RATE_NORMAL ? "Speed 1x" : "Speed %gx", RATES[p->rate_idx]);
    struct { Icon icon; const char *label; int hit, on, show; } b[] = {
        { ICON_SPEED, sp, PHH_SHEET + PHB_SPEED, p->ph_sheet == PH_SPEED, 1 },
        { ICON_SUBS, "Audio & subtitles", PHH_TRACKS, p->ph_sheet == PH_TRACKS, 1 },
        { ICON_SWAP, "Sources", PHH_SHEET + PHB_SOURCES, p->ph_sheet == PH_SOURCES, !p->trailer },
        { ICON_LIST, "Episodes", PHH_SHEET + PHB_EPISODES, p->ph_sheet == PH_EPISODES, ph_has_eps(p) && !p->trailer },
        { ICON_NEXT, "Next episode", PHH_NEXT_EP, 0, p->next_video_id != NULL },
        { ICON_SETTINGS, "More", PHH_MORE, p->ph_sheet >= PH_MORE, 1 },
    };
    enum { NB = sizeof b / sizeof b[0] };
    float total = 0, gap = 20;
    for (int i = 0; i < NB; i++) if (b[i].show) total += ph_row_btn(b[i].icon, b[i].label, 0, 0, 0, 0, 1) + gap;
    int bare = total - gap > room;   /* too wide (a list open beside it): the icons alone */
    if (bare) { total = 0; for (int i = 0; i < NB; i++) if (b[i].show) total += ph_row_btn(b[i].icon, NULL, 0, 0, 0, 0, 1) + gap; }
    float x = cx - (total - gap) / 2;
    for (int i = 0; i < NB; i++) if (b[i].show) x += ph_row_btn(b[i].icon, bare ? NULL : b[i].label, x, cy, b[i].hit, b[i].on, 0) + gap;
}


/* 10 s back or on: a circling arrow, the 10 inside */
static void desk_skip_icon(float cx, float cy, float r, int dir, Color c, int fs)
{
    const float a0 = -1.05f, a1 = 4.36f;   /* clockwise from upper right round to upper left: a gap at the top */
    float px = 0, py = 0;
    for (int k = 0; k <= 24; k++) {
        float a = a0 + (a1 - a0) * k / 24, x = cx + dir * r * cosf(a), y = cy + r * sinf(a);
        if (k) gfx_line(px, py, x, y, 3, c);
        px = x; py = y;
    }
    float tx = dir * -sinf(a1), ty = cosf(a1), al = r * 0.5f;   /* the head, the way the arrow goes */
    for (int s = -1; s <= 1; s += 2) {
        float ca = cosf(2.6f * s), sa = sinf(2.6f * s);
        gfx_line(px, py, px + al * (tx * ca - ty * sa), py + al * (tx * sa + ty * ca), 3, c);
    }
    font_draw_center("10", cx, cy - font_height(fs) / 2 + 1, fs, c);
}

#ifdef SNTV_DESKTOP_PLAYER
static void desk_full_icon(float cx, float cy, int fs, Color c)
{
    const float k = 13, l = 9;
    for (int sx = -1; sx <= 1; sx += 2)
        for (int sy = -1; sy <= 1; sy += 2) {   /* an L at each corner: pointing out, or in when full screen already */
            float d = fs ? k - l : k, x = cx + sx * d, y = cy + sy * d, ax = fs ? sx * l : -sx * l, ay = fs ? sy * l : -sy * l;
            gfx_line(x, y, x + ax, y, 3, c);
            gfx_line(x, y, x, y + ay, 3, c);
        }
}

/* A computer: the controls of the streaming apps people know. Along the bottom the timeline,
 * which grows under the pointer and shows the frame there; under it one row: play, 10 s back
 * and on, the volume, the title in the middle, the lists and full screen at the right, each
 * named when the pointer rests on it. The lists open in a panel above the row. */
static float g_ptr_x = -1, g_ptr_y = -1, g_ptr_t = -100;   /* the pointer (canvas units), when it last moved */
static const char *g_dk_tip; static float g_dk_tip_x;      /* the button under the pointer: its name, where */
static float g_dk_vol = 1;   /* the volume set here, kept from one video to the next as the player keeps it */
static float ph_vol(Player *p);

static int dk_over(float x, float y, float w, float h)
{
    return g_app.time - g_ptr_t < 3.0f && g_ptr_x >= x && g_ptr_x < x + w && g_ptr_y >= y && g_ptr_y < y + h;
}

static void desk_ep_code(char *code, size_t n)   /* S2E1 -> S2:E1 */
{
    char *e = strchr(code, 'E');
    if (e && strlen(code) + 1 < n) { memmove(e + 1, e, strlen(e) + 1); *e = ':'; }
}

static void desk_volume(Player *p, float v)
{
    if (v < 0) v = 0;
    if (v > 1) v = 1;
    p->vol = g_dk_vol = v;
    video_set_volume((int)(v * 100 + 0.5f));
    if (p->fs_until < g_app.time + 3.0f) p->fs_until = g_app.time + 3.0f;
    app_redraw();
}

/* One button of the row: lit under the pointer, which names it */
static int desk_btn(float cx, float cy, const char *tip, int hit)
{
    int over = dk_over(cx - 38, cy - 38, 76, 76);
    if (over) { gfx_circle(cx, cy, 36, RGBA(255, 255, 255, 0x26)); g_dk_tip = tip; g_dk_tip_x = cx; }
    ui_hit(cx - 38, cy - 38, 76, 76, hit);
    return over;
}

static void desk_icon_btn(Icon icon, float cx, float cy, const char *tip, int hit, int on)
{
    desk_btn(cx, cy, tip, hit);
    ui_icon(icon, cx + (icon == ICON_PLAY ? 3 : 0), cy, icon == ICON_PLAY || icon == ICON_PAUSE ? 44 : 36, on ? THEME_ACCENT : THEME_TEXT);
}

static void desk_row(const PhRow *r, float x, float y, float w, float rh, int foc, int hit)
{
    if (foc) gfx_rrect(x - 12, y, w + 24, rh - 8, 12, COLOR_ALPHA(THEME_TEXT, 0x16));
    if (r->on) ui_icon(ICON_CHECK, x + 16, y + (rh - 8) / 2, 26, THEME_ACCENT);
    float tx = x + 48, tw = w - 56;
    int fs = FONT_S | (r->on ? FONT_B : 0);
    font_draw_fit(r->label, tx, r->sub[0] ? y + 8 : y + (rh - 8 - font_height(fs)) / 2, fs, r->on ? THEME_TEXT : THEME_TEXT_DIM, tw);
    if (r->sub[0]) font_draw_fit(r->sub, tx, y + 42, FONT_XS, THEME_TEXT_MUTED, tw);
    ui_hit(x - 12, y, w + 24, rh - 8, hit);
}

/* A list's rows in a column; the focused one kept in view (rows first..last of r) */
static void desk_rows(Player *p, const PhRow *r, int first, int last, float x, float top, float w, float bottom, int scrolls)
{
    const float rh = 80;
    int n = last - first;
    float view = bottom - top, scroll = 0;
    if (scrolls) {
        int f = p->ph_focus - first;
        float want = p->ph_scroll;
        if (f >= 0 && f < n) {
            if (f * rh + rh > want + view) want = f * rh + rh - view;
            if (f * rh < want) want = f * rh;
        }
        float dt = g_app.time - p->ph_t;
        p->ph_t = g_app.time;
        ui_pan(&p->ph_pan, &p->ph_scroll, want, n * rh - view, dt < 0 ? 0 : dt > 0.1f ? 0.1f : dt, 16);
        ui_pan_area(&p->ph_pan, x - 12, top, w + 24, view, 1);
        scroll = p->ph_scroll;
    }
    gfx_clip_push(x - 16, top, w + 32, view);
    for (int i = 0; i < n; i++) {
        float ry = top + i * rh - scroll;
        if (ry + rh < top || ry > bottom) continue;
        desk_row(&r[first + i], x, ry, w, rh, first + i == p->ph_focus, PHH_ROW + first + i);
    }
    gfx_clip_pop();
}

/* "Size  ‹ Medium ›": a setting with a step either way */
static float desk_stepper(const char *label, const char *value, float x, float y, int dn, int up)
{
    font_draw(label, x, y + (48 - font_height(FONT_XS | FONT_B)) / 2, FONT_XS | FONT_B, THEME_TEXT_DIM);
    x += font_measure(label, FONT_XS | FONT_B) + 16;
    float vw = font_measure(value, FONT_XS | FONT_B) + 24;
    if (vw < 150) vw = 150;
    for (int k = 0; k < 2; k++) {
        float bx = k ? x + 48 + vw : x;
        if (dk_over(bx, y, 48, 48)) gfx_circle(bx + 24, y + 24, 24, RGBA(255, 255, 255, 0x26));
        ui_icon(k ? ICON_CHEVRON_RIGHT : ICON_CHEVRON_LEFT, bx + 24, y + 24, 24, THEME_TEXT);
        ui_hit(bx, y, 48, 48, k ? up : dn);
    }
    font_draw_center(value, x + 48 + vw / 2, y + (48 - font_height(FONT_XS | FONT_B)) / 2, FONT_XS | FONT_B, THEME_TEXT);
    return x + 96 + vw;
}

/* The open list, in a panel above the row at the right; audio and subtitles side by side */
static void desk_sheet(Player *p, float right, float bottom)
{
    static const char *const NAMES[] = { "", "Audio", "Subtitles", "Sources", "Episodes", "Speed", "Audio", "More", "Subtitle settings", "Scenes", "Cast" };
    PhRow r[PH_ROWS];
    int n = ph_rows(p, r), tracks = p->ph_sheet == PH_TRACKS, na = 0;
    if (tracks) while (na < n && !(r[na].act & TRACKS_SUB)) na++;
    if (p->ph_focus >= n) p->ph_focus = n ? n - 1 : 0;
    const float rh = 80, head = 100, foot = tracks ? 100 : 28;
    float w = tracks ? 1180 : p->ph_sheet == PH_SPEED ? 420 : p->ph_sheet == PH_SOURCES ? 980 : 760;
    int tall = tracks ? (na > n - na ? na : n - na) : n;
    if (tall < 1) tall = 1;
    float h = head + tall * rh + foot, maxh = bottom - 110;
    if (h > maxh) h = maxh;
    float x = right - w, y = bottom - h;
    if (!g_app.fx_light) gfx_shadow(x, y + 8, w, h, R_PANEL, 40, RGBA(0, 0, 0, 0xA0));
    gfx_rrect(x, y, w, h, R_PANEL, RGBA(0x16, 0x18, 0x1C, 0xF4));
    ui_hit(x, y, w, h, PHH_PANEL);   /* a click there is not a click on the film */
    if (dk_over(x + w - 76, y + 20, 56, 56)) gfx_circle(x + w - 48, y + 48, 28, RGBA(255, 255, 255, 0x26));
    ui_icon(ICON_CLOSE, x + w - 48, y + 48, 26, THEME_TEXT_DIM);
    ui_hit(x + w - 80, y + 16, 64, 64, PHH_CLOSE);
    float lt = y + head, lb = y + h - foot;
    if (!tracks) {
        font_draw(NAMES[p->ph_sheet], x + 40, y + 30, FONT_M | FONT_B, THEME_TEXT);
        if (!n) font_draw(p->ph_sheet == PH_EPISODES ? "No other episodes" : "Nothing here", x + 40, lt + 10, FONT_S, THEME_TEXT_MUTED);
        desk_rows(p, r, 0, n, x + 40, lt, w - 80, lb, 1);
        return;
    }
    float cw = (w - 120) / 2, ax = x + 40, sx = x + 80 + cw;
    font_draw("Audio", ax, y + 30, FONT_M | FONT_B, THEME_TEXT);
    font_draw("Subtitles", sx, y + 30, FONT_M | FONT_B, THEME_TEXT);
    gfx_rect(x + 60 + cw - 1, y + 36, 2, h - 72, COLOR_ALPHA(THEME_TEXT, 0x18));
    if (!na) font_draw("One audio track", ax, lt + 10, FONT_S, THEME_TEXT_MUTED);
    desk_rows(p, r, 0, na, ax, lt, cw, lb, 0);
    desk_rows(p, r, na, n, sx, lt, cw, lb, 1);
    /* how the subtitles look and when they come, under the lists */
    gfx_rect(x + 40, lb + 6, w - 80, 2, COLOR_ALPHA(THEME_TEXT, 0x18));
    char v[64];
    subs_setting(p, SUB_SIZE, v, sizeof v);
    float fx = desk_stepper("Subtitle size", v, x + 40, lb + 32, PHH_SZ_DN, PHH_SZ_UP);
    subs_setting(p, SUB_TIMING, v, sizeof v);
    desk_stepper("Timing", v, fx + 48, lb + 32, PHH_TM_DN, PHH_TM_UP);
}

/* Skip intro, the next episode or "Still watching?": a button at the right above the timeline */
static int desk_offer(Player *p, const VideoInfo *vi, float right, float y)
{
    const SkipSeg *sk = g_app.skip_mode == 0 ? skippable(p, vi) : NULL;
    if (!sk && !(p->up_next_shown && p->next_video_id) && !p->still_asking) return 0;
    const char *t = p->still_asking ? "Still watching?  Click to go on" : sk ? (sk->kind == SKIP_RECAP ? "Skip recap" : "Skip intro") : next_ready(p) ? "Next episode" : "Next episode (getting it ready\xE2\x80\xA6)";
    float h = 64, w = font_measure(t, FONT_S | FONT_B) + 64, x = right - w;
    int over = dk_over(x, y, w, h), filled = over || p->still_asking;
    gfx_rrect(x, y, w, h, 12, filled ? RGBA(255, 255, 255, 0xF0) : RGBA(0x14, 0x14, 0x18, 0xA0));
    if (!filled) gfx_rrect_ring(x, y, w, h, 12, 2, COLOR_ALPHA(THEME_TEXT, 0xC0));
    font_draw_center(t, x + w / 2, y + (h - font_height(FONT_S | FONT_B)) / 2, FONT_S | FONT_B, filled ? RGBA(0x11, 0x11, 0x11, 255) : THEME_TEXT);
    ui_hit(x, y, w, h, p->still_asking ? PHH_STILL_ON : sk ? PHH_SKIP : PHH_NEXT);
    return 1;
}

/* The timeline: what has loaded, the chapters as gaps, where it is; under the pointer it grows,
 * and the frame at the pointer shows above it with the time and the chapter. */
static void desk_timeline(Player *p, const VideoInfo *vi, float x, float y, float w)
{
    double dur = ph_dur(p, vi), pos = p->seek_commit > 0 ? p->seek_target : vi->position;
    int held = p->tch == 1, hover = held || (dur > 0 && dk_over(x - 12, y - 30, w + 24, 60) && !p->ph_sheet);
    float step = g_app.time - p->dk_hkt;
    p->dk_hkt = g_app.time;
    if (g_app.fx_light) p->dk_hk = hover;
    else { p->dk_hk += ((float)hover - p->dk_hk) * (step * 18 > 1 ? 1 : step * 18); if (fabsf(hover - p->dk_hk) < 0.02f) p->dk_hk = (float)hover; else app_redraw(); }
    float th = 6 + 6 * p->dk_hk, ty = y - th / 2, frac = dur > 0 ? (float)(pos / dur) : 0;
    if (frac < 0) frac = 0;
    if (frac > 1) frac = 1;
    gfx_rrect(x, ty, w, th, th / 2, COLOR_ALPHA(THEME_TEXT, 0x38));
    if (vi->buffer_ahead > 0 && dur > 0) { float b = (float)((vi->position + vi->buffer_ahead) / dur); if (b > 1) b = 1; gfx_rrect(x, ty, w * b, th, th / 2, COLOR_ALPHA(THEME_TEXT, 0x50)); }
    gfx_rrect(x, ty, w * frac, th, th / 2, THEME_ACCENT);
    float hx = held ? x + w * frac : g_ptr_x < x ? x : g_ptr_x > x + w ? x + w : g_ptr_x;
    if (hover && !held) {   /* from where it is to the pointer, lighter */
        float a = x + w * frac, b = hx;
        if (b < a) { float t = a; a = b; b = t; }
        gfx_rect(a, ty, b - a, th, COLOR_ALPHA(THEME_TEXT, 0x60));
    }
    if (p->probe && dur > 0)
        for (int i = 1; i < p->probe->n_chapters; i++)   /* chapters: small gaps in the line */
            gfx_rect(x + w * (float)(p->probe->chapters[i].start_s / dur) - 2, ty - 1, 4, th + 2, RGBA(0, 0, 0, 0xC0));
    for (int i = 0; i < p->n_marks && dur > 0; i++) gfx_circle(x + w * (float)(p->marks[i] / dur), ty - 10, 4, THEME_ACCENT);
    gfx_circle(x + w * frac, y, 9 + 5 * p->dk_hk, THEME_ACCENT);
    p->fs_barr[0] = x - 30; p->fs_barr[1] = y - 30; p->fs_barr[2] = w + 60; p->fs_barr[3] = 60;
    if (!hover || dur <= 0) return;
    double t = held ? pos : dur * (hx - x) / w;
    if (!held) gfx_rect(hx - 1.5f, ty - 6, 3, th + 12, THEME_TEXT);
    char ts[24];
    ph_time(t, ts, sizeof ts);
    int ch = chapter_at(p, t);
    const char *cname = ch >= 0 && p->probe->chapters[ch].name[0] ? p->probe->chapters[ch].name : NULL;
    int iw = 0, ih = 0;
    unsigned tex = seek_thumb(p, t, &iw, &ih, held);
    float pw = tex && iw > 0 && ih > 0 ? 384 : 0, ph = pw ? pw * ih / iw : 0;
    if (ph > 260) { ph = 260; pw = ph * iw / ih; }
    float bw = pw > 260 ? pw : 260, bx = hx - bw / 2;
    if (bx < 24) bx = 24;
    if (bx + bw > SNTV_UI_W - 24) bx = SNTV_UI_W - 24 - bw;
    float cy = y - 34 - (cname ? 30 : 0) - font_height(FONT_M | FONT_B);
    if (pw) {
        float py = cy - 14 - ph, px = bx + (bw - pw) / 2;
        if (!g_app.fx_light) gfx_shadow(px, py + 8, pw, ph, 12, 30, RGBA(0, 0, 0, 0xA0));
        gfx_rtex_uv(tex, px, py, pw, ph, 12, 0, 0, 1, 1, RGBA(255, 255, 255, 255), 0);
        gfx_rrect_ring(px, py, pw, ph, 12, 3, RGBA(255, 255, 255, 0xE6));
    }
    font_draw_shadow(ts, bx + (bw - font_measure(ts, FONT_M | FONT_B)) / 2, cy, FONT_M | FONT_B, THEME_TEXT, RGBA(0, 0, 0, 0xC0));
    if (cname) {
        char c[160];
        snprintf(c, sizeof c, "%s", cname);
        float cw = font_measure(c, FONT_XS);
        font_draw_shadow(c, bx + (bw - (cw < bw ? cw : bw)) / 2, cy + font_height(FONT_M | FONT_B) + 2, FONT_XS, THEME_TEXT_DIM, RGBA(0, 0, 0, 0xC0));
    }
}

/* The credits: the film steps back to the top left and the next episode takes the screen,
 * counting down to it; Watch credits puts it away, Next episode (or its picture) plays it. */
static float desk_next_k(Player *p)
{
    int on = p->up_next_shown && p->next_video_id && !p->still_asking && !p->pip && !p->ph_sheet;
    float step = g_app.time - p->dk_nkt;
    p->dk_nkt = g_app.time;
    if (g_app.fx_light) p->dk_nk = (float)on;
    else {
        p->dk_nk += ((float)on - p->dk_nk) * (step * 6 > 1 ? 1 : step * 6);
        if (fabsf((float)on - p->dk_nk) < 0.01f) p->dk_nk = (float)on; else app_redraw();
    }
    p->dk_nke = p->dk_nk * p->dk_nk * (3 - 2 * p->dk_nk);
    return p->dk_nke;
}

static void desk_next_rect(float k, float *r)
{
    const float to[4] = { 96, 108, 864, 486 };
    float from[4] = { 0, 0, (float)SNTV_UI_W, (float)SNTV_UI_H };
    for (int i = 0; i < 4; i++) r[i] = from[i] + (to[i] - from[i]) * k;
}

static void desk_arc(float cx, float cy, float r, float frac, float th, Color c)
{
    int n = (int)(48 * frac) + 1;
    float px = cx, py = cy - r;
    for (int i = 1; i <= n; i++) {
        float a = -1.5707963f + 6.2831853f * frac * i / n, x = cx + r * cosf(a), y = cy + r * sinf(a);
        gfx_line(px, py, x, y, th, c);
        px = x; py = y;
    }
}

static void desk_next_screen(Player *p, const VideoInfo *vi, float k)
{
    int a = (int)(255 * k);
    float W = SNTV_UI_W, H = SNTV_UI_H, x = 1050, w = W - 96 - x, y = 108;
    /* what is ending, under the small picture */
    font_draw_fit(p->meta.name ? p->meta.name : "", 96, 640, FONT_XS, COLOR_ALPHA(THEME_TEXT_MUTED, a), 864);
    char code[32]; const char *ename = NULL;
    if (episode_parts(p, code, sizeof code, &ename)) {
        desk_ep_code(code, sizeof code);
        char line[240]; snprintf(line, sizeof line, "%s%s%s", code, ename && *ename ? "  \xC2\xB7  " : "", ename ? ename : "");
        font_draw_fit(line, 96, 672, FONT_M | FONT_B, COLOR_ALPHA(THEME_TEXT, a), 864);
    }
    /* the next one: when it starts, its picture, its name, what happens */
    double at = p->next_at > 0 ? p->next_at : vi->duration, left = at - vi->position, window = at < vi->duration ? 20.0 : 45.0;
    int counting = g_app.autoplay_next && vi->duration > 0 && left > 0.5 && !p->next_wanted;
    char when[64];
    if (counting) snprintf(when, sizeof when, "Next episode in %d", (int)(left + 0.5));
    else snprintf(when, sizeof when, p->next_wanted || !next_ready(p) ? "Next episode, getting it ready\xE2\x80\xA6" : "Next episode");
    font_draw(when, x, y - 4, FONT_S | FONT_B, COLOR_ALPHA(THEME_TEXT_DIM, a));
    float ty = y + 48, th = w * 9 / 16;
    const char *thumb = app_video_thumbnail(p->meta.id, p->next_video_id);
    gfx_set_alpha(k);
    ui_card(thumb, x, ty, w, th, 0, 0, NULL, CARD_MIP);
    gfx_set_alpha(1);
    p->card_thumb[0] = x; p->card_thumb[1] = ty; p->card_thumb[2] = w; p->card_thumb[3] = th; p->card_at = g_app.time;   /* the next one zooms from here */
    int over = dk_over(x, ty, w, th);
    gfx_rrect_ring(x, ty, w, th, R_CARD, over ? 4 : 3, COLOR_ALPHA(THEME_TEXT, over ? a : (int)(0xD0 * k)));
    float cx = x + w / 2, cy = ty + th / 2;
    gfx_circle(cx, cy, 52, RGBA(0, 0, 0, (int)(0x90 * k)));
    ui_icon(ICON_PLAY, cx + 4, cy, 44, COLOR_ALPHA(THEME_TEXT, a));
    if (counting) {   /* the ring empties as it comes */
        desk_arc(cx, cy, 62, 1, 6, RGBA(255, 255, 255, (int)(0x40 * k)));
        float f = (float)(left / window);
        desk_arc(cx, cy, 62, f < 0 ? 0 : f > 1 ? 1 : f, 6, COLOR_ALPHA(THEME_TEXT, a));
        app_redraw();
    }
    ui_hit(x, ty, w, th, PHH_NEXT);
    float ny = ty + th + 28;
    int nv;
    const MetaVideo *vids = app_videos(p->meta.id, &nv);
    const char *about = NULL;
    char title[240];
    snprintf(title, sizeof title, "%s", p->next_title ? p->next_title : "Next episode");
    for (int i = 0; i < nv; i++)
        if (!strcmp(vids[i].id, p->next_video_id)) {
            if (vids[i].season >= 0 && vids[i].episode >= 0) snprintf(title, sizeof title, "S%d:E%d%s%s", vids[i].season, vids[i].episode, vids[i].title ? "  \xC2\xB7  " : "", vids[i].title ? vids[i].title : "");
            about = vids[i].overview;
            break;
        }
    font_draw_fit(title, x, ny, FONT_M | FONT_B, COLOR_ALPHA(THEME_TEXT, a), w);
    if (about && *about) font_draw_wrapped(about, x, ny + font_height(FONT_M | FONT_B) + 10, FONT_S, COLOR_ALPHA(THEME_TEXT_DIM, a), w, 3);
    /* the two choices, at the bottom right */
    const char *go = "Next episode", *stay = "Watch credits";
    float bh = 72, gw = font_measure(go, FONT_S | FONT_B) + 104, sw = font_measure(stay, FONT_S | FONT_B) + 64, by = H - 96 - bh, gx = W - 96 - gw, sx = gx - 20 - sw;
    int og = dk_over(gx, by, gw, bh), os = dk_over(sx, by, sw, bh);
    gfx_rrect(gx, by, gw, bh, 12, og ? RGBA(255, 255, 255, a) : RGBA(0xEE, 0xEE, 0xEE, (int)(0xF0 * k)));
    ui_icon(ICON_NEXT, gx + 44, by + bh / 2, 30, RGBA(0x11, 0x11, 0x11, a));
    font_draw(go, gx + 76, by + (bh - font_height(FONT_S | FONT_B)) / 2, FONT_S | FONT_B, RGBA(0x11, 0x11, 0x11, a));
    ui_hit(gx, by, gw, bh, PHH_NEXT);
    if (os) gfx_rrect(sx, by, sw, bh, 12, RGBA(255, 255, 255, (int)(0x24 * k)));
    gfx_rrect_ring(sx, by, sw, bh, 12, 2, COLOR_ALPHA(THEME_TEXT, (int)(0xC0 * k)));
    font_draw_center(stay, sx + sw / 2, by + (bh - font_height(FONT_S | FONT_B)) / 2, FONT_S | FONT_B, COLOR_ALPHA(THEME_TEXT, a));
    ui_hit(sx, by, sw, bh, PHH_CREDITS);
    /* and Back, as everywhere */
    gfx_circle(72, 80, 36, RGBA(255, 255, 255, (int)((dk_over(36, 44, 72, 72) ? 0x30 : 0x1C) * k)));
    ui_icon(ICON_CHEVRON_LEFT, 70, 80, 30, COLOR_ALPHA(THEME_TEXT, a));
    ui_hit(32, 40, 80, 80, PHH_BACK);
}

/* "You're watching": the picture dimmed from the left, the title, the episode, what happens in it */
static void desk_rest_card(Player *p, const VideoInfo *vi, float k)
{
    int a = (int)(255 * k);
    float W = SNTV_UI_W, H = SNTV_UI_H, x = 144, maxw = 860;
    gfx_gradient_h(0, 0, W, H, RGBA(0, 0, 0, (int)(0xE0 * k)), RGBA(0, 0, 0, (int)(0x40 * k)));
    const char *about = p->meta.description, *ename = NULL;
    int nv, season = -1, episode = -1;
    const MetaVideo *vids = app_videos(p->meta.id, &nv);
    for (int i = 0; i < nv && p->video_id; i++)
        if (!strcmp(vids[i].id, p->video_id)) {   /* this episode, not the show */
            if (vids[i].overview && *vids[i].overview) about = vids[i].overview;
            season = vids[i].season; episode = vids[i].episode; ename = vids[i].title;
            break;
        }
    char ep[200] = "", facts[160] = "", left[24];
    if (season >= 0 && episode >= 0) snprintf(ep, sizeof ep, "Season %d  \xC2\xB7  Episode %d%s%s", season, episode, ename && *ename ? "  \xC2\xB7  " : "", ename && *ename ? ename : "");
    double dur = ph_dur(p, vi);
    ph_time(dur - vi->position, left, sizeof left);
    const char *bits[3] = { p->meta.release_info, p->meta.runtime, dur > 0 ? left : NULL };   /* "2009  ·  47 min  ·  22:31 left" */
    for (int i = 0; i < 3; i++) {
        if (!bits[i] || !*bits[i]) continue;
        size_t l = strlen(facts);
        snprintf(facts + l, sizeof facts - l, "%s%s%s", l ? "  \xC2\xB7  " : "", bits[i], i == 2 ? " left" : "");
    }
    const char *name = p->meta.name && *p->meta.name ? p->meta.name : p->title ? p->title : "";
    int tfs = font_measure(name, FONT_TITLE | FONT_B) > maxw ? FONT_XL | FONT_B : FONT_TITLE | FONT_B;
    float need = font_height(FONT_S) + 16 + font_draw_wrapped(name, x, 0, tfs, 0, maxw, 2) * font_height(tfs) + 12
               + (ep[0] ? font_height(FONT_M | FONT_B) + 18 : 0) + (about ? font_draw_wrapped(about, x, 0, FONT_S, 0, maxw, 4) * font_height(FONT_S) * 1.15f + 20 : 0)
               + font_height(FONT_XS);
    float y = (H - need) / 2 - 30 + (1 - k) * 18;
    font_draw("You're watching", x, y, FONT_S, COLOR_ALPHA(THEME_TEXT_DIM, a));
    y += font_height(FONT_S) + 16;
    y += font_draw_wrapped(name, x, y, tfs, COLOR_ALPHA(THEME_TEXT, a), maxw, 2) * font_height(tfs) + 12;
    if (ep[0]) { font_draw_fit(ep, x, y, FONT_M | FONT_B, COLOR_ALPHA(THEME_TEXT, (int)(0xE6 * k)), maxw); y += font_height(FONT_M | FONT_B) + 18; }
    if (about) y += font_draw_wrapped(about, x, y, FONT_S, COLOR_ALPHA(THEME_TEXT_DIM, a), maxw, 4) * font_height(FONT_S) * 1.15f + 20;
    if (facts[0]) font_draw(facts, x, y, FONT_XS, COLOR_ALPHA(THEME_TEXT_MUTED, a));
    float px = W - 96 - font_measure("Paused", FONT_S | FONT_B), py = H - 96;
    ui_icon(ICON_PAUSE, px - 30, py + font_height(FONT_S | FONT_B) / 2, 30, COLOR_ALPHA(THEME_TEXT_DIM, a));
    font_draw("Paused", px, py, FONT_S | FONT_B, COLOR_ALPHA(THEME_TEXT_DIM, a));
}

/* The controls, when they show (or only the skip button when they don't) */
static void desk_controls(Player *p, const VideoInfo *vi, int shown, int failed)
{
    float W = SNTV_UI_W, H = SNTV_UI_H, lx = 72, by = H - 168, cy = H - 82;
    p->dk_volr[2] = 0;
    {   /* the card comes in slowly as the controls go, and goes at once when they come back */
        int rest = desk_resting(p, vi);
        float step = g_app.time - p->dk_rkt;
        p->dk_rkt = g_app.time;
        if (!rest || g_app.fx_light) p->dk_rk = rest;
        else if (p->dk_rk < 1) { p->dk_rk += step / 0.8f; if (p->dk_rk > 1) p->dk_rk = 1; app_redraw(); }
        if (rest && p->dk_rk < 1 && g_app.fx_light == 0) app_redraw();
        if (p->dk_rk > 0) desk_rest_card(p, vi, p->dk_rk * p->dk_rk * (3 - 2 * p->dk_rk));
        if (!rest && vi->state == VIDEO_PAUSED && p->paused_since > 0 && g_app.time < p->paused_since + env_s("SNTV_REST_S", 5.0f) + 0.2f) app_redraw();   /* to come in on time */
    }
    if (p->dk_nke > 0) { desk_next_screen(p, vi, p->dk_nke); if (p->ph_sheet) desk_sheet(p, W - lx, cy - 52); return; }
    if (!shown && p->fs_k <= 0) { if (!p->dk_rk) desk_offer(p, vi, W - lx, H - 150); return; }
    float fe = 1 - (1 - p->fs_k) * (1 - p->fs_k);
    gfx_set_alpha(fe);
    if (g_app.time < p->fs_until) app_redraw();   /* to hide them on time */
    g_dk_tip = NULL;
    if (!failed) {
        gfx_gradient_v(0, 0, W, 200, RGBA(0, 0, 0, 0xA0), RGBA(0, 0, 0, 0));
        gfx_gradient_v(0, H - 380, W, 380, RGBA(0, 0, 0, 0), RGBA(0, 0, 0, 0xD8));
    }
    /* the top: back at the left, what is playing at the right */
    if (dk_over(lx - 36, 44, 72, 72)) gfx_circle(lx, 80, 36, RGBA(255, 255, 255, 0x30));
    else gfx_circle(lx, 80, 36, RGBA(255, 255, 255, 0x1C));
    ui_icon(ICON_CHEVRON_LEFT, lx - 2, 80, 30, THEME_TEXT);
    ui_hit(lx - 40, 40, 80, 80, PHH_BACK);
    char res[32], aud[48];
    media_badges(p, vi, res, sizeof res, aud, sizeof aud);
    float chx = W - lx;
    if (aud[0]) { chx -= font_measure(aud, FONT_XS | FONT_B) + 24; ui_chip(aud, chx, 60, COLOR_ALPHA(THEME_TEXT, 0x24), THEME_TEXT); chx -= 12; }
    if (res[0]) { chx -= font_measure(res, FONT_XS | FONT_B) + 24; ui_chip(res, chx, 60, COLOR_ALPHA(THEME_TEXT, 0x24), THEME_TEXT); }
    if (failed) { gfx_set_alpha(1); return; }
    gfx_set_translate(0, 20 * (1 - fe));
    desk_offer(p, vi, W - lx, by - 120);
    double pos = p->seek_commit > 0 ? p->seek_target : vi->position, dur = ph_dur(p, vi);
    char a[24], b[24];
    ph_time(pos, a, sizeof a);
    if (dur > 0) { char t[24]; ph_time(dur - pos, t, sizeof t); snprintf(b, sizeof b, "\xE2\x88\x92%.20s", t); } else b[0] = 0;
    font_draw(a, lx, by + 20, FONT_XS | FONT_B, COLOR_ALPHA(THEME_TEXT, 0xD0));
    font_draw(b, W - lx - font_measure(b, FONT_XS | FONT_B), by + 20, FONT_XS | FONT_B, COLOR_ALPHA(THEME_TEXT, 0xD0));
    /* the row, from the left */
    float x = lx + 10;
    desk_icon_btn(vi->state == VIDEO_PLAYING ? ICON_PAUSE : ICON_PLAY, x, cy, vi->state == VIDEO_PLAYING ? "Pause (Space)" : "Play (Space)", PHH_PLAY, 0);
    x += 88;
    desk_btn(x, cy, "Back 10 s (Left)", PHH_BACK10);
    desk_skip_icon(x, cy, 20, -1, THEME_TEXT, FONT_XS | FONT_B);
    x += 88;
    desk_btn(x, cy, "On 10 s (Right)", PHH_FWD10);
    desk_skip_icon(x, cy, 20, 1, THEME_TEXT, FONT_XS | FONT_B);
    x += 88;
    float vol = ph_vol(p);
    desk_btn(x, cy, vol > 0.001f ? "Mute" : "Sound on", PHH_MUTE);
    ui_icon(ICON_AUDIO, x, cy, 34, THEME_TEXT);
    if (vol <= 0.001f) gfx_line(x - 18, cy - 18, x + 18, cy + 18, 4, THEME_TEXT);
    float sx = x + 50, sw = 150;
    int vh = dk_over(sx - 12, cy - 24, sw + 24, 48) || p->tch == 3;
    gfx_rrect(sx, cy - 3, sw, 6, 3, COLOR_ALPHA(THEME_TEXT, 0x40));
    gfx_rrect(sx, cy - 3, sw * vol, 6, 3, THEME_TEXT);
    if (vh) gfx_circle(sx + sw * vol, cy, 10, THEME_TEXT);
    p->dk_volr[0] = sx - 12; p->dk_volr[1] = cy - 24; p->dk_volr[2] = sw + 24; p->dk_volr[3] = 48;
    float left_end = sx + sw + 40;
    /* from the right: full screen, speed, aspect, audio and subtitles, sources, episodes, the next episode */
    float rx = W - lx - 10;
    int fs = window_fullscreen();
    desk_btn(rx, cy, fs ? "Leave full screen (F)" : "Full screen (F)", HIT_WIN_FULL);
    desk_full_icon(rx, cy, fs, THEME_TEXT);
    rx -= 88;
    desk_btn(rx, cy, "Speed", PHH_SHEET + PHB_SPEED);
    if (p->rate_idx != RATE_NORMAL) { char sp[16]; snprintf(sp, sizeof sp, "%gx", RATES[p->rate_idx]); font_draw_center(sp, rx, cy - font_height(FONT_XS | FONT_B) / 2, FONT_XS | FONT_B, THEME_ACCENT); }
    else ui_icon(ICON_SPEED, rx, cy, 36, p->ph_sheet == PH_SPEED ? THEME_ACCENT : THEME_TEXT);
    rx -= 88;
    desk_icon_btn(ICON_SETTINGS, rx, cy, "More: scenes, cast, subtitle settings, reload", PHH_MORE, p->ph_sheet >= PH_MORE);
    rx -= 88;
    char asp[48];
    snprintf(asp, sizeof asp, "Picture: %s", ASPECT_NAMES[p->aspect]);
    desk_icon_btn(ICON_ASPECT, rx, cy, asp, PHH_ASPECT, 0);
    rx -= 88;
    desk_icon_btn(ICON_SUBS, rx, cy, "Audio & subtitles", PHH_TRACKS, p->ph_sheet == PH_TRACKS);
    rx -= 88;
    if (!p->trailer) { desk_icon_btn(ICON_SWAP, rx, cy, "Sources", PHH_SHEET + PHB_SOURCES, p->ph_sheet == PH_SOURCES); rx -= 88; }
    if (ph_has_eps(p) && !p->trailer) { desk_icon_btn(ICON_LIST, rx, cy, "Episodes", PHH_SHEET + PHB_EPISODES, p->ph_sheet == PH_EPISODES); rx -= 88; }
    if (p->next_video_id) {
        desk_icon_btn(ICON_NEXT, rx, cy, "Next episode", PHH_NEXT_EP, 0);
        float k = next_progress(p);
        if (k >= 0 && k < 1) ui_progress(rx - 18, cy + 26, 36, 3, k, THEME_TEXT_DIM);   /* still getting it ready */
        rx -= 88;
    }
    /* what is playing, in the middle of what is left */
    float room = rx + 40 - left_end, mid = (left_end + rx + 40) / 2;
    char code[32], line[300];
    const char *ename = NULL, *name = p->meta.name && *p->meta.name ? p->meta.name : p->title ? p->title : "";
    if (episode_parts(p, code, sizeof code, &ename)) {
        desk_ep_code(code, sizeof code);
        snprintf(line, sizeof line, "%s%s%s", code, ename && *ename ? "  \xC2\xB7  " : "", ename ? ename : "");
    } else line[0] = 0;
    float nw = font_measure(name, FONT_S | FONT_B), ew = line[0] ? font_measure(line, FONT_S) + 20 : 0;
    if (nw + ew > room) { if (nw > room * 0.5f && ew) nw = room * 0.5f; if (nw + ew > room) ew = room - nw; if (ew < 0) ew = 0; }
    if (nw > room) nw = room;
    float tx = mid - (nw + ew) / 2, tyy = cy - font_height(FONT_S | FONT_B) / 2;
    font_draw_fit(name, tx, tyy, FONT_S | FONT_B, THEME_TEXT, nw);
    if (ew > 20) font_draw_fit(line, tx + nw + 20, tyy, FONT_S, THEME_TEXT_DIM, ew - 20);
    gfx_set_translate(0, 0);
    if (g_dk_tip && !p->ph_sheet) {   /* the name of the button under the pointer, above it */
        float tw = font_measure(g_dk_tip, FONT_XS | FONT_B) + 28, tx2 = g_dk_tip_x - tw / 2;
        if (tx2 < 12) tx2 = 12;
        if (tx2 + tw > W - 12) tx2 = W - 12 - tw;
        gfx_rrect(tx2, cy - 96, tw, 42, 10, RGBA(0x10, 0x10, 0x12, 0xF0));
        font_draw(g_dk_tip, tx2 + 14, cy - 96 + (42 - font_height(FONT_XS | FONT_B)) / 2, FONT_XS | FONT_B, THEME_TEXT);
    }
    desk_timeline(p, vi, lx, by, W - 2 * lx);
    gfx_set_alpha(1);
    if (p->ph_sheet) desk_sheet(p, W - lx, cy - 52);
}
#endif

/* Sideways on a phone: the picture is the whole screen; the controls over it. */
/* Under the loading card on a phone: no controls yet (nothing to pause or seek), only Back */
static void full_draw_loading(Player *p)
{
    p->ph_pic[0] = 0; p->ph_pic[1] = 0; p->ph_pic[2] = SNTV_UI_W; p->ph_pic[3] = SNTV_UI_H;
    ph_round(ICON_CHEVRON_LEFT, NULL, 96, 84, 52, 0, PHH_BACK);
}

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
    {   /* the controls fade in from the edges, and out again */
        float want = shown ? 1 : 0, step = g_app.time - p->fs_kt;
        p->fs_kt = g_app.time;
        if (g_app.fx_light) p->fs_k = want;
        else { p->fs_k += (want - p->fs_k) * (step * 16 > 1 ? 1 : step * 16); if (fabsf(want - p->fs_k) < 0.01f) p->fs_k = want; else app_redraw(); }
    }
#ifdef SNTV_DESKTOP_PLAYER
    desk_controls(p, vi, shown, failed);
    return;
#endif
    if (p->ph_locked) {   /* locked: only the way out, for a moment after a touch */
        if (g_app.time < p->fs_until) {
            const char *t = "Locked  \xC2\xB7  tap to unlock";
            float w = font_measure(t, FONT_S | FONT_B) + 130, h = 92, x = W - 48 - w, y = 36;
            gfx_rrect(x, y, w, h, h / 2, RGBA(0, 0, 0, 0xB0));
            ui_icon(ICON_LOCK, x + 50, y + h / 2, 38, THEME_TEXT);
            font_draw(t, x + 88, y + (h - font_height(FONT_S | FONT_B)) / 2, FONT_S | FONT_B, THEME_TEXT);
            ui_hit(x - 20, y - 20, w + 40, h + 40, PHH_UNLOCK);
            app_redraw();
        }
        return;
    }
    if (!shown && p->fs_k <= 0) {   /* only the skip button stays, at the bottom right */
        ph_offer(p, vi, W - 80 - 520, H - 150, 520, 96);
        return;
    }
    float fk = p->fs_k, fe = 1 - (1 - fk) * (1 - fk);
    gfx_set_alpha(fe);
    gfx_set_translate(0, -26 * (1 - fe));
    if (g_app.time < p->fs_until) app_redraw();   /* to hide them on time */
    if (!failed) {
        gfx_gradient_v(0, 0, W, 240, RGBA(0, 0, 0, 0xB8), RGBA(0, 0, 0, 0));
        gfx_gradient_v(0, H - 380, W, 380, RGBA(0, 0, 0, 0), RGBA(0, 0, 0, 0xD0));
    }
    /* top: back, what is playing, and at the right the lock, picture in picture and the picture's shape */
    ph_round(ICON_CHEVRON_LEFT, NULL, 96, 84, 52, 0, PHH_BACK);
    float rx = sheet_x - 96;
    int tops = 0;
    struct { Icon icon; int hit; } top[3];
    top[tops].icon = ICON_ASPECT; top[tops++].hit = PHH_ASPECT;
#ifdef __ANDROID__
    top[tops].icon = ICON_PIP; top[tops++].hit = PHH_SHEET + PHB_PIP;
#endif
    top[tops].icon = ICON_LOCK; top[tops++].hit = PHH_LOCK;
    for (int i = 0; i < tops; i++, rx -= 128) {
        gfx_circle(rx, 84, 52, RGBA(0, 0, 0, 0x60));
        ui_icon(top[i].icon, rx, 84, 46, THEME_TEXT);
        ui_hit(rx - 62, 22, 124, 124, top[i].hit);
    }
    float tw = rx + 64 - 180 - 24;
    char code[32], line[300];
    const char *ename = NULL;
    font_draw_fit(p->meta.name ? p->meta.name : "", 180, 40, FONT_L | FONT_B, THEME_TEXT, tw);
    if (episode_parts(p, code, sizeof code, &ename)) {
        char *e = strchr(code, 'E');
        if (e && strlen(code) + 1 < sizeof code) { memmove(e + 1, e, strlen(e) + 1); *e = ':'; }   /* S2E1 -> S2:E1 */
        snprintf(line, sizeof line, "%s%s%s", code, ename && *ename ? "  \xC2\xB7  " : "", ename ? ename : "");
        font_draw_fit(line, 180, 96, FONT_S, THEME_TEXT_DIM, tw);
    } else if (p->title && *p->title && (!p->meta.name || strcmp(p->title, p->meta.name))) font_draw_fit(p->title, 180, 96, FONT_S, THEME_TEXT_DIM, tw);
    gfx_set_translate(0, 0);
    if (!failed) {
        ph_transport(p, vi, sheet_x / 2, H / 2 - 40, sheet_x < W ? 250 : 330);
        gfx_set_translate(0, 26 * (1 - fe));
        /* the timeline with the times either side, and the row of what there is to change under it */
        double pos = p->seek_commit > 0 ? p->seek_target : vi->position, dur = ph_dur(p, vi);
        char a[24], b[24];
        ph_time(pos, a, sizeof a);
        if (dur > 0) { char t[24]; ph_time(dur - pos, t, sizeof t); snprintf(b, sizeof b, "-%.20s", t); } else b[0] = 0;
        float lx = 80, rx2 = sheet_x - 80, by = H - 200;
        font_draw(a, lx, by - font_height(FONT_S | FONT_B) / 2, FONT_S | FONT_B, THEME_TEXT);
        font_draw(b, rx2 - font_measure(b, FONT_S | FONT_B), by - font_height(FONT_S | FONT_B) / 2, FONT_S | FONT_B, THEME_TEXT);
        float bl = lx + font_measure("00:00:00", FONT_S | FONT_B) + 30, br = rx2 - font_measure("-00:00:00", FONT_S | FONT_B) - 30;
        ph_bar(p, vi, bl, by, br - bl, p->fs_barr);
        ph_row(p, sheet_x / 2, H - 80, sheet_x - 120);
        ph_offer(p, vi, rx2 - 520, by - 160, 520, 96);
    }
    gfx_set_translate(0, 0);
    gfx_set_alpha(1);
    if (p->pinch_hint_at > 0 && g_app.time - p->pinch_hint_at < 4) {   /* once, the first time: the picture can fill the screen */
        const char *t = "Pinch to fill the screen";
        float w = font_measure(t, FONT_S | FONT_B) + 120, h = 84, x = (sheet_x - w) / 2, y = 160;
        gfx_rrect(x, y, w, h, h / 2, RGBA(0, 0, 0, 0xA0));
        ui_icon(ICON_ASPECT, x + 48, y + h / 2, 36, THEME_TEXT);
        font_draw(t, x + 84, y + (h - font_height(FONT_S | FONT_B)) / 2, FONT_S | FONT_B, THEME_TEXT);
        app_redraw();
    } else if (!p->pinch_hint_at && shown && vi->state == VIDEO_PLAYING && !store_get_int("pinchHint", 0)) {
        p->pinch_hint_at = g_app.time;
        store_set_int("pinchHint", 1);
        app_redraw();
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
    case PHH_TRACKS: ph_open(p, PH_TRACKS); return 1;
    case PHH_MORE: ph_open(p, PH_MORE); return 1;
    case PHH_LOCK: p->ph_locked = 1; p->ph_sheet = PH_NONE; p->fs_until = g_app.time + 2.0f; LOGI("player: touches locked"); return 1;
    case PHH_UNLOCK: p->ph_locked = 0; p->fs_until = g_app.time + 4.5f; LOGI("player: touches unlocked"); return 1;
#ifdef SNTV_DESKTOP_PLAYER
    case PHH_MUTE: player_toggle_mute(); return 1;
    case PHH_SZ_DN: case PHH_SZ_UP: adjust_subs(p, SUB_SIZE, id == PHH_SZ_UP ? 1 : -1); return 1;
    case PHH_TM_DN: case PHH_TM_UP: shift_subs(p, id == PHH_TM_UP ? 0.1 : -0.1); return 1;
    case PHH_CREDITS: p->up_next_shown = 0; p->up_next_dismissed = 1; LOGI("player: watching the credits"); return 1;
#endif
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
    if (!TOUCH_UI || p->pip) return 0;
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
    if (p->ph_sheet) { p->ph_sheet = PH_NONE; p->tap_pending = 0; LOGI("player: a tap away closed the list"); app_redraw(); return; }   /* away from an open list: it goes, and that is all */
#ifdef SNTV_DESKTOP_PLAYER
    (void)x;   /* a computer: a click on the film plays or pauses (two are full screen); moving the mouse shows the controls */
    toggle_play(p, &vi);
    p->fs_until = g_app.time + 3.0f;
    app_redraw();
    return;
#endif
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
#ifdef SNTV_DESKTOP_PLAYER
    if (v < 0 && p->vol < 0) return g_dk_vol;   /* what was set in an episode before (the player keeps it) */
#endif
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

/* How far a drag across the whole picture seeks: a minute, more for longer videos. */
static double ph_swipe_secs(double dur) { return dur >= 3600 ? 120 : dur >= 1800 ? 90 : 60; }

/* A finger held still on a playing picture plays it at double speed until it lifts. */
static void ph_hold_fast(Player *p, const VideoInfo *vi)
{
    if (p->tch != 2 || p->tch_fast || p->tch_moved || p->tch_seeking || p->tch_adjust || g_app.time - p->tch_at < 0.5f) return;
    if (vi->state != VIDEO_PLAYING || p->ph_sheet) return;
    p->tch_fast = 1;
    p->tap_pending = 0;
    video_set_rate(2.0);
    LOGI("player: held: double speed while the finger stays");
    app_redraw();
}

static int in_rect(const float *r, float x, float y) { return r[2] > 0 && x >= r[0] && x < r[0] + r[2] && y >= r[1] && y < r[1] + r[3]; }

/* A finger on the player (a phone): along the bar it scrubs, across the picture it seeks,
 * up or down the picture it goes to full screen or back; one that barely moved is a tap. */
static int drag(Screen *s, int phase, float x, float y)
{
    Player *p = s->st;
    if (!TOUCH_UI || p->pip) return 0;
    if (p->ph_locked) {   /* locked: a touch shows the way out, and does nothing else */
        int id;
        if (phase == 0 && ui_hit_find(x, y, s, &id) && id == PHH_UNLOCK) return 0;   /* the unlock button's own tap */
        if (phase == 0) { p->fs_until = g_app.time + 3.0f; app_redraw(); }
        return 1;
    }
    VideoInfo vi;
    video_info(&vi);
    double dur = ph_dur(p, &vi);
    float *bar = p->fs_barr;
    if (phase == 0) {
        p->tch = 0; p->tch_seeking = 0; p->tch_moved = 0;
        if (error_final(p, &vi)) return 0;
#ifdef SNTV_DESKTOP_PLAYER
        if (in_rect(p->dk_volr, x, y)) { p->tch = 3; desk_volume(p, (x - p->dk_volr[0] - 12) / (p->dk_volr[2] - 24)); return 1; }
#endif
        int id;
        if (ui_hit_find(x, y, s, &id)) return 0;   /* a button or the open list (one over the timeline too) */
        else if (dur > 0 && in_rect(bar, x, y)) p->tch = 1;   /* the bar: the knob jumps to the finger */
        else if (in_rect(p->ph_pic, x, y)) p->tch = 2;
        else return 0;
        p->tch_x0 = x; p->tch_y0 = y; p->tch_at = g_app.time; p->tch_fast = 0;
        p->tch_pos0 = p->seek_commit > 0 ? p->seek_target : vi.position;
        if (p->tch == 1) { float bl = bar[0] + 30, bw = bar[2] - 60; touch_seek(p, dur * (x - bl) / bw, 60); }
        return 1;
    }
    if (!p->tch) return 0;
    float dx = x - p->tch_x0, dy = y - p->tch_y0;
    if (fabsf(dx) > 24 || fabsf(dy) > 24) p->tch_moved = 1;
#ifdef SNTV_DESKTOP_PLAYER
    if (p->tch == 3) {   /* the volume slider */
        desk_volume(p, (x - p->dk_volr[0] - 12) / (p->dk_volr[2] - 24));
        if (phase == 2) { p->tch = 0; LOGI("player: volume %d%%", (int)(ph_vol(p) * 100 + 0.5f)); }
        return 1;
    }
#endif
    if (p->tch == 1) {
        float bl = bar[0] + 30, bw = bar[2] - 60;
        double t = dur * (x - bl) / bw;
        if (phase == 1) touch_seek(p, t, 60);
        else { touch_seek(p, t, 0.01f); LOGI("player: scrubbed to %.0f s", p->seek_target); p->tch = 0; }
        return 1;
    }
    if (p->tch_fast) {   /* playing fast: only lifting the finger counts */
        if (phase == 2) {
            video_set_rate(RATES[p->rate_idx]);
            LOGI("player: let go: back to %gx", RATES[p->rate_idx]);
            p->tch = 0; p->tch_fast = 0;
            app_redraw();
        }
        return 1;
    }
    if (phase == 1) {
        if (!p->tch_seeking && !p->tch_adjust && dur > 0 && fabsf(dx) > 40 && fabsf(dx) > fabsf(dy) * 1.2f) { p->tch_seeking = 1; p->tch_x0 = x; }
        float edge = SNTV_UI_H * 0.08f;   /* not from the top or bottom edge: the system's own swipes start there */
        if (!p->tch_seeking && !p->tch_adjust && fabsf(dy) > 40 && fabsf(dy) > fabsf(dx) * 1.2f && p->tch_y0 > edge && p->tch_y0 < SNTV_UI_H - edge) {   /* up or down: the left half's brightness, the right half's volume */
            p->tch_adjust = p->tch_x0 < p->ph_pic[0] + p->ph_pic[2] / 2 ? 1 : 2;
            p->tch_level0 = p->tch_adjust == 1 ? ph_bright(p) : ph_vol(p);
            p->tch_y0 = y;
        }
        if (p->tch_seeking) touch_seek(p, p->tch_pos0 + (x - p->tch_x0) / p->ph_pic[2] * ph_swipe_secs(dur), 60);
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
    if (TOUCH_UI && !p->pip && p->ph_locked && a == KEY_BACK) { p->ph_locked = 0; p->fs_until = g_app.time + 4.5f; LOGI("player: touches unlocked (Back)"); app_redraw(); return 1; }   /* locked: Back unlocks first */
    if (TOUCH_UI && !p->pip && p->ph_sheet) {   /* the open list takes Up, Down, OK and Back */
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
#ifdef SNTV_DESKTOP_PLAYER
    if (!p->pip && (a == KEY_UP || a == KEY_DOWN)) { desk_volume(p, ph_vol(p) + (a == KEY_UP ? 0.1f : -0.1f)); return 1; }   /* the arrows up and down: louder, quieter */
#endif
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
    if (p->seek_commit > 0 && a == KEY_OK) { p->seek_commit = 0; player_seek(p->seek_target); show_overlay(p); return 1; }
    if (p->seek_commit > 0 && a == KEY_BACK) { p->seek_commit = 0; if (g_app.live_scrub) player_seek(p->seek_origin); app_toast("Seek cancelled"); show_overlay(p); return 1; }
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
            if (next < 0) { player_seek(0); } else if (next < p->probe->n_chapters) player_seek(p->probe->chapters[next].start_s);
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

int player_needs_bandwidth(void)
{
    static double since = -1;   /* when this stretch of need began */
    VideoInfo vi;
    if (!g_live || g_live->finished || g_live->pip) { since = -1; return 0; }   /* in the corner the app is being browsed: its pages come first */
    video_info(&vi);
    if (!playback_needs_bandwidth(&vi)) { since = -1; return 0; }
    if (since < 0) since = g_app.time;
    return g_app.time - since < 8;   /* past that holding the rest back isn't what's slowing it, and the pages would wait for nothing */
}

void player_save_progress_quiet(void)   /* an overlay over the app: saved, but Trakt is not told it stopped */
{
    if (g_live) report_ex(g_live, 1, 0);
}

static void destroy(Screen *s)
{
    Player *p = s->st;
    if (p->tb_waiting) { cand_drop(&p->tb_wait); p->tb_waiting = 0; }
    if (!p->start_timed) perf_start_cancel();   /* left before its first frame: not a start */
    if (p->bright >= 0) video_set_brightness(-1);   /* the screen's brightness is the system's again */
    if (p->tch_fast) video_set_rate(1.0);   /* closed with a finger still holding it fast */
    video_pip_allow(0);
    relay_buffer_close();   /* its memory goes with it */
    if (!p->finished) report(p, 1);
    streams_lookup_cancel();
    if (p->cur < p->n_cands) torrent_engine_drop(p->cands[p->cur].url);
    if (g_live == p) g_live = NULL;
    if (!p->trailer) perf_playback_end();
    p->gen++;
    video_stop();
    subs_free(&p->track);
    subs_free(&p->track2);
    subs_free(&p->emb);
    mkvsubs_open(NULL, NULL);
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
    player_seek(pos > 10 ? pos - 10 : 0);
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
    p->lb_shown = -1;
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
    if (!p->trailer && g_bridge.video_id[0] && !strcmp(g_bridge.video_id, video_id) && g_app.time - g_bridge.at < 30) {   /* the sources took their time: 30 s at most */
        p->bridge = 1; p->bridge_t0 = g_app.time; p->bridge_from = g_bridge.from;
        memcpy(p->bridge_rect, g_bridge.rect, sizeof p->bridge_rect);
        LOGI("player: %s opens on its still%s", video_id, g_bridge.from ? ", out of the Up Next card" : "");
    }
    g_bridge.video_id[0] = 0;
    {
        char pid[128], pt[200];
        if (app_adjacent_video(meta->id, video_id, -1, pid, sizeof pid, pt, sizeof pt)) { p->prev_video_id = strdup(pid); p->prev_title = strdup(pt); }
    }
    p->rate_idx = RATE_NORMAL;
    p->active_addon_sub = -1;
    p->active_addon_sub2 = -1;
    p->embedded_text_sel = -1;
    p->sync_for = -1; p->ref_idx = -1; p->sync_first = -1;
    p->gen = ++g_player_generation;
    if (!p->trailer) perf_playback_begin();
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
    ui_hit(x, y, w, h, HIT_PIP);   /* a click or a tap: full screen */
    if (focused) { if (!g_app.fx_light) gfx_shadow(x - 4, y - 4, w + 8, h + 8, 6, 30, COLOR_ALPHA(THEME_ACCENT2, 0xA0)); gfx_rrect_ring(x, y, w, h, 2, 4, THEME_TEXT); }
    else gfx_rrect_ring(x, y, w, h, 2, 2, COLOR_ALPHA(THEME_TEXT, 0x50));
    if (focused) {   /* two choices, for remotes without a Stop key */
        float bw1 = ui_pill_width("Full screen", 1, 52), bw2 = ui_pill_width("Close", 1, 52);
        float bx = x + (w - bw1 - bw2 - 16) / 2, by = y + h / 2 - 26;
        ui_pill("Full screen", 1, ICON_PLAY, bx, by, 52, PILL_SECONDARY, focused == 1);
        ui_pill("Close", 1, ICON_BACK, bx + bw1 + 16, by, 52, PILL_SECONDARY, focused == 2);
        ui_hit(bx, by, bw1, 52, HIT_PIP_FULL);
        ui_hit(bx + bw1 + 16, by, bw2, 52, HIT_PIP_CLOSE);
    }
    gfx_gradient_v(x, y + h - 96, w, 96, RGBA(0, 0, 0, 0), RGBA(0, 0, 0, 0xC8));
    font_draw_fit(p->title, x + 18, y + h - 64, FONT_XS | FONT_B, THEME_TEXT, w - 36);
    if (vi.duration > 0) ui_progress(x + 18, y + h - 20, w - 36, 4, (float)(vi.position / vi.duration), THEME_ACCENT);
    float cx = x + w / 2, cy = y + h / 2 - 10;
    if (focused) { /* the buttons take the middle */ }
    else if (vi.state == VIDEO_PAUSED) { gfx_circle(cx, cy, 34, RGBA(0, 0, 0, 0x90)); ui_icon(ICON_PLAY, cx + 3, cy, 30, THEME_TEXT); }
    else if (vi.state == VIDEO_LOADING || vi.buffering) ui_spinner(cx, cy, 20);
#ifdef SNTV_DESKTOP_PLAYER
    if (!focused) ui_chip("Click or P: full screen", x + 12, y + 12, RGBA(0, 0, 0, 0x90), THEME_TEXT_DIM);   /* a computer: the pointer shows the buttons */
#else
    if (!gfx_handheld()) ui_chip(focused ? "Left/Right: choose  \xC2\xB7  OK: select  \xC2\xB7  Up: back" : "Down: select  \xC2\xB7  Play/Pause: full screen", x + 12, y + 12, RGBA(0, 0, 0, 0x90), focused ? THEME_TEXT : THEME_TEXT_DIM);
#endif
    if (vi.state == VIDEO_PLAYING) app_redraw();
}

void player_pointer_moved(float x, float y)
{
#ifdef SNTV_DESKTOP_PLAYER
    g_ptr_x = x; g_ptr_y = y; g_ptr_t = g_app.time;
    app_redraw();   /* what is under it lights, the timeline grows */
#else
    (void)x; (void)y;
#endif
    if (!g_live || g_live->pip) return;
    if (g_live->fs_until < g_app.time + 2.9f) { g_live->fs_until = g_app.time + 3.0f; app_redraw(); }
}

/* Two fingers apart: the picture fills the screen (cropped); together: all of it again. */
void player_pinch(float d)
{
    Player *p = g_live;
    if (!p || p->pip || !TOUCH_UI || p->ph_locked) return;
    if (g_app.time - p->pinch_at > 0.4f) p->pinch_d = 0;   /* a new pinch */
    p->pinch_at = g_app.time;
    p->pinch_d += d;
    if (p->tch) { p->tch = 0; p->tch_seeking = 0; p->tch_adjust = 0; p->tch_fast = 0; video_set_rate(RATES[p->rate_idx]); if (p->seek_commit > 0) p->seek_commit = 0; }   /* the first finger's drag was the start of this */
    int want = p->pinch_d > 0.05f ? ASPECT_FILL : p->pinch_d < -0.05f ? ASPECT_FIT : -1;
    if (want < 0 || want == p->aspect) return;
    p->aspect = want;
    p->pinch_d = 0;
    apply_aspect(p);
    app_toast("%s", want == ASPECT_FILL ? "Filling the screen" : "The whole picture");
    LOGI("player: pinch: %s", ASPECT_NAMES[want]);
}

void player_toggle_mute(void)
{
#ifdef SNTV_DESKTOP_PLAYER
    if (!g_live || g_live->pip) return;
    float v = ph_vol(g_live);
    if (v > 0.001f) { g_live->dk_unmuted = v; desk_volume(g_live, 0); }
    else desk_volume(g_live, g_live->dk_unmuted > 0.01f ? g_live->dk_unmuted : 1);
    LOGI("player: %s", v > 0.001f ? "muted" : "sound on");
#endif
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
