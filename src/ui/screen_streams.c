#include "../app.h"
#include "../lang.h"
#include "../http.h"
#include "../log.h"
#include "../youtube.h"
#include "../srchealth.h"
#include "../probe.h"
#include "../perfstat.h"
#include "../video/video.h"
#include "../store.h"
#include "../torrent.h"
#include "../torbox.h"
#include "font.h"
#include "theme.h"
#include "widgets.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define ROW_H 142.0f
#define LIST_X 620.0f
#define LIST_Y 200.0f
enum { VISIBLE = 7 };

typedef struct Streams Streams;
typedef struct { Streams *owner; int addon; unsigned generation; } Pending;
static unsigned g_generation;
static Streams *g_live;

struct Streams {
    MetaPreview meta;
    char *video_id, *video_title;
    EpisodeHint ep; int ep_known;   /* what video_id is, for the episode check (worked out once) */
    int chaining;             /* from Up Next: plays the source most like `chain` by itself */
    ChainHint chain;
    Stream *items; int n;
    int pending, n_reqs;
    int focus;
    int moved;                /* the viewer has moved the focus: from then on it follows its source */
    float scroll, scroll_target;
    int autoplayed;
    float started;
    int from_prefetch;        /* answers still arriving belong to the prefetch */
    int check_autoplay;
    int auto_rec_done;        /* the recommended source was started by itself */
    int sources_timed;        /* the start timing has its "sources" phase */
    int launched;             /* a player was opened from here (coming back to it is Back from that player) */
    int play_now;             /* opened to play: the recommended source starts by itself, whatever the setting */
    int probes_started, probe_inflight;
    int ranks_logged;
    unsigned probe_gen;
};
#define PROBE_BYTES (2u << 20)   /* enough header for Tracks, Chapters or a faststart moov */
#define PROBE_MAX 3               /* candidates checked, best first, one at a time */
typedef struct ProbeReq { Streams *owner; unsigned gen; char *url; } ProbeReq;
#define REC_H 150.0f          /* the recommendation card above the list */

/* Streams fetched ahead of time for the next episode (the player asks when the
 * credits start). A Streams screen opened for that episode adopts them and the
 * requests still in flight, so Up Next starts without the usual wait. */
static struct {
    char *video_id;
    Stream *items; int n;
    int pending, total;
    unsigned generation;
    float started;
    int addons_version;   /* the addons it was asked of: new ones make it stale */
    MetaPreview meta; int has_meta;   /* the title, to rank what came in (its best is checked ahead) */
} g_pre;
#define PREFETCH_FRESH_S 10800.0f   /* the answers hold for hours; near the end of the episode they are asked again (streams_prefetch_again) */

static int playable(const Stream *s)
{
    char reason[8];
    char *u = app_stream_playable_url(s, reason, sizeof reason);
    if (!u) return 0;
    free(u);
    return 1;
}

/* The user's addon order (as in the Stremio apps), with unplayable sources last
 * and, while chaining episodes, the previous binge group on top so Up Next stays
 * on the same release. Answers arrive in any order, so this is applied on every
 * arrival. */
/* Rows some addons add that are notices, not videos ("Support the project", "Join our Discord"). */
static int is_notice(const Stream *s)
{
    static const char *const W[] = { "discord", "patreon", "donate", "support us", "support the", "join our", "telegram", "buy me a coffee", "ko-fi", "not available", "unavailable", "no streams", "sign up", "upgrade to", NULL };
    char text[400];
    snprintf(text, sizeof text, "%s %s", s->name ? s->name : "", s->title ? s->title : (s->description ? s->description : ""));
    for (char *c = text; *c; c++) if (*c >= 'A' && *c <= 'Z') *c += 32;
    for (int i = 0; W[i]; i++) if (strstr(text, W[i]) && !s->tags.res && !s->video_size) return 1;
    return 0;
}

/* Settings > Sources > Rules: the reason a source breaks one, or NULL. Broken rules keep
 * the source in the list but never recommended or auto-played. */
static const char *rule_broken(const Stream *s)
{
    double gb = s->tags.size_gb > 0 ? s->tags.size_gb : s->video_size ? s->video_size / 1073741824.0 : 0;
    if (g_app.src_max_gb && gb > g_app.src_max_gb) return "larger than your size limit";
    if (g_app.src_hdr == 1 && s->tags.hdr == 2) return "Dolby Vision, which your rules avoid";
    if (g_app.src_hdr == 2 && s->tags.hdr) return "HDR, which your rules avoid";
    if (g_app.src_cached_only && s->tags.cached < 0) return "not cached, and your rules want cached only";
    if (g_app.src_cached_only && s->info_hash && !s->tags.cached) return "a torrent, and your rules want cached only";
    if (g_app.src_exclude[0]) {
        char text[600], words[sizeof g_app.src_exclude];
        snprintf(text, sizeof text, "%s %s %s", s->name ? s->name : "", s->title ? s->title : "", s->description ? s->description : "");
        for (char *c = text; *c; c++) if (*c >= 'A' && *c <= 'Z') *c += 32;
        snprintf(words, sizeof words, "%s", g_app.src_exclude);
        for (char *c = words; *c; c++) if (*c >= 'A' && *c <= 'Z') *c += 32;
        char *save = NULL;
        for (char *w = strtok_r(words, ",", &save); w; w = strtok_r(NULL, ",", &save)) {
            while (*w == ' ') w++;
            size_t n = strlen(w); while (n && w[n - 1] == ' ') w[--n] = 0;
            if (n && strstr(text, w)) return "named with a word your rules exclude";
        }
    }
    return NULL;
}

/* The one gate for every automatic choice (the recommendation, Play, the next episode,
 * the fallbacks behind a source, the start race, asking again): the reason this source
 * must not be played without the viewer picking it, or NULL. Ranking only orders what
 * passes. A source picked by hand from the list still plays. */
static const char *ineligible(const Streams *st, const Stream *s)
{
    if (is_notice(s)) return "a notice, not a video";
    if (!playable(s)) return "it cannot play on this TV";
    if (s->probe_state == 4) return "its link does not answer";
    /* a link that failed a moment ago only goes last (rank): asked again, debrid links often come back */
    const char *rule = rule_broken(s);
    if (rule) return rule;
    if (st && st->video_id) {   /* named for another episode: "S01E04" when this is E05 */
        Streams *m = (Streams *)st;
        if (!m->ep_known) { app_episode_hint(m->meta.id, m->meta.name, m->video_id, &m->ep); m->ep_known = 1; }
        if (m->ep.episode > 0) {
            char line[300];
            const char *nl;
            if (s->filename && quality_episode_score(s->filename, &m->ep) < 0) return "it is another episode";
            const char *t = s->title ? s->title : s->description;
            if (t) {
                nl = strchr(t, '\n');
                snprintf(line, sizeof line, "%.*s", nl ? (int)(nl - t) : (int)strlen(t), t);
                if (quality_episode_score(line, &m->ep) < 0 && !(s->filename && quality_episode_score(s->filename, &m->ep) > 0)) return "it is another episode";
            }
        }
    }
    return NULL;
}

/* The source's name mentions the preferred audio language (or MULTI / DUAL). */
static int has_audio_lang(const Stream *s)
{
    if (!g_app.src_audio_lang || !lang_known(g_app.pref_audio)) return 0;
    char text[600];
    snprintf(text, sizeof text, " %s %s %s ", s->name ? s->name : "", s->title ? s->title : "", s->description ? s->description : "");
    for (char *c = text; *c; c++) { if (*c >= 'A' && *c <= 'Z') *c += 32; if (strchr(".-_[]()|/,", *c)) *c = ' '; }
    const char *canon = lang_canonical(g_app.pref_audio);
    if (!canon) return 0;
    char name[48], code[16];
    snprintf(name, sizeof name, " %s ", lang_display_english(canon));
    snprintf(code, sizeof code, " %s ", canon);
    if (strstr(text, name) || strstr(text, code)) return 2;
    if (strstr(text, " multi ") || strstr(text, " dual ")) return 1;
    return 0;
}

/* A torrent this TV plays itself (no streaming server set): the fallback. */
static int is_p2p(const Stream *s)
{
    return s->info_hash && !s->url && !torbox_hash_cached(s->info_hash) && !(g_app.streaming_server && *g_app.streaming_server) && g_app.p2p_fallback && torrent_engine_available();
}

/* 2: the addon's own tag for the same release; 1: the same release group; 0 */
static int same_release(const Streams *st, const Stream *s)
{
    if (!st->chaining) return 0;
    if (st->chain.binge_group[0] && s->binge_group && !strcmp(st->chain.binge_group, s->binge_group)) return 2;
    if (!st->chain.group[0]) return 0;
    char g[24] = "";
    /* a season pack's file can be "Show - S01E02.mkv": the torrent's name then holds the group */
    const char *from[] = { s->filename, s->title, s->description };
    for (int i = 0; i < 3 && !g[0]; i++) quality_release_group(from[i], g, sizeof g);
    return g[0] && !strcasecmp(g, st->chain.group);   /* any addon: the same encode wherever it is listed */
}

/* a video in your own TorBox (it heads the list) */
static int is_mine(const Stream *s) { return s->addon_order == -1 && s->addon_name && !strcmp(s->addon_name, "My TorBox"); }
/* a torrent TorBox has cached (it plays from there, right after your own) */
static int from_torbox(const Stream *s) { return !s->url && s->info_hash && torbox_hash_cached(s->info_hash); }

static long rank(const Streams *st, const Stream *s)
{
    long r = g_app.quality_mode == QUAL_ADDON_ORDER ? s->addon_order * 10L : (200000L - s->score) * 10L + s->addon_order;
    if (is_mine(s)) r -= 80000000L;
    else if (from_torbox(s)) r -= 40000000L;
    if (is_p2p(s)) r += 5000000L;                      /* after every cached or direct link */
    if (s->tags.cached < 0) r += 6000000L;             /* needs a debrid download first: after P2P */
    if (!playable(s)) r += 100000000L;
    if (is_notice(s)) r += 200000000L;
    if (s->headers) r += 3000000L;   /* it asks for request headers the TV's player can't send: after the others */
    { const char *g = ineligible(st, s); if (g && !strcmp(g, "it is another episode")) r += 150000000L; }   /* below every source that is this episode */
    if (s->url && app_link_dead(s->url)) r += 20000000L;
    if (st->chaining) {   /* the next episode: like the one just played, below the working-link penalties */
        int m = same_release(st, s);
        if (m == 2) r -= 50000000L;
        else {
            long b = m ? 4000000L : 0;
            if (st->chain.addon_order >= 0 && s->addon_order == st->chain.addon_order) b += 2000000L;
            if (st->chain.res && s->tags.res == st->chain.res) b += 1000000L;
            if (st->chain.needs && quality_needs(&s->tags) == st->chain.needs) b += 500000L;
            r -= b;
        }
    }
    return r;
}

static void sort_items(Streams *st);

/* ---- checks kept a while ----
 * A link's check (its first 2 MB) holds for minutes: the file behind it doesn't change.
 * The title page checks the best of the sources it fetched ahead, so Play has nothing
 * left to check; a Sources page opened again reuses its checks. Where a link's redirects
 * ended is kept a minute: the player opens that and skips the hops (it may expire, so
 * the link itself stays the fallback). */
#define CHECK_FRESH_S 600.0f
#define FINAL_FRESH_S 60.0f
#define RACE_SKIP_S 60.0f      /* checked this recently: no race to see whether it answers */
typedef struct { char *url; float at; int state; ProbeInfo *info; double measured_mbps, file_mbps; char final_url[1024]; float final_at; } LinkCheck;
static LinkCheck g_checks[16];
static int g_checks_next;

static LinkCheck *check_find(const char *url, float fresh_s)
{
    for (int i = 0; i < 16; i++)
        if (g_checks[i].url && !strcmp(g_checks[i].url, url) && g_app.time - g_checks[i].at < fresh_s) return &g_checks[i];
    return NULL;
}

static LinkCheck *check_slot(const char *url)
{
    for (int i = 0; i < 16; i++) if (g_checks[i].url && !strcmp(g_checks[i].url, url)) return &g_checks[i];
    LinkCheck *c = &g_checks[g_checks_next];
    g_checks_next = (g_checks_next + 1) % 16;
    free(c->url); free(c->info);
    memset(c, 0, sizeof *c);
    c->url = strdup(url);
    return c;
}

static void host_of(const char *url, char *out, size_t n)   /* the host only: a link can carry a key */
{
    const char *hs = strstr(url, "://"); hs = hs ? hs + 3 : url;
    snprintf(out, n, "%.*s", (int)strcspn(hs, "/:?"), hs);
}

static void final_note(const char *url, const HttpResponse *r)
{
    if (!r->final_url[0] || strncmp(r->final_url, "http", 4)) return;
    LinkCheck *c = check_slot(url);
    snprintf(c->final_url, sizeof c->final_url, "%s", r->final_url);
    c->final_at = g_app.time;
}

/* What a check found, kept for the link. */
static const LinkCheck *check_record(const char *url, const HttpResponse *r)
{
    char host[96];
    host_of(url, host, sizeof host);
    LinkCheck *c = check_slot(url);
    c->at = g_app.time;
    free(c->info); c->info = NULL;
    c->measured_mbps = c->file_mbps = 0;
    if (!http_looks_like_media(r)) {   /* not answering, or answering with a page instead of the file */
        c->state = 4;
        LOGW("probe: %s -> HTTP %ld %s", host, r->status, r->error);
        return c;
    }
    final_note(url, r);
    c->measured_mbps = r->seconds > 0.05 ? (double)r->len * 8.0 / r->seconds / 1e6 : 0;
    ProbeInfo *info = malloc(sizeof *info);
    char d[200];
    if (probe_parse((const uint8_t *)r->body, r->len, info)) {
        c->state = 2;
        c->info = info;
        c->file_mbps = probe_mbits(info, r->total_size);
        probe_describe(info, d, sizeof d);
        LOGI("probe: %s -> %s; file %.0f Mbit/s, link %.0f Mbit/s, %d chapters", host, d, c->file_mbps, c->measured_mbps, info->n_chapters);
    } else {
        c->state = 3;
        probe_describe(info, d, sizeof d);
        LOGI("probe: %s -> alive, %s (%s), link %.0f Mbit/s", host, d, r->content_type, c->measured_mbps);
        free(info);
    }
    return c;
}

static void check_apply(const LinkCheck *c, Stream *s)
{
    s->probe_state = c->state;
    s->measured_mbps = c->measured_mbps;
    s->file_mbps = c->file_mbps;
    if (c->info) {
        free(s->probe);
        s->probe = malloc(sizeof *s->probe);
        *s->probe = *c->info;
        probe_apply(s->probe, &s->tags);
    }
}

const char *streams_final_url(const char *url)
{
    for (int i = 0; url && i < 16; i++)
        if (g_checks[i].url && !strcmp(g_checks[i].url, url) && g_checks[i].final_url[0] && g_app.time - g_checks[i].final_at < FINAL_FRESH_S) return g_checks[i].final_url;
    return NULL;
}

static void on_probe(HttpResponse *r, void *user)
{
    ProbeReq *q = user;
    Streams *st = q->owner;
    const LinkCheck *c = check_record(q->url, r);   /* kept even when the screen is gone */
    if (st != g_live || q->gen != st->probe_gen) { free(q->url); free(q); return; }
    st->probe_inflight = 0;
    for (int i = 0; i < st->n; i++) {
        Stream *s = &st->items[i];
        if (!s->url || strcmp(s->url, q->url) != 0) continue;
        check_apply(c, s);
        break;
    }
    free(q->url); free(q);
    sort_items(st);
    app_redraw();
}

/* Fetch the header of the next-best unchecked candidate. */
static void probe_next(Streams *st)
{
    if (!g_app.preflight || st->probe_inflight || st->probes_started >= PROBE_MAX) return;
    int reused = 0;
    for (int i = 0; i < st->n; i++) {   /* checked ahead or a moment ago: no need to ask again */
        Stream *s = &st->items[i];
        if (s->probe_state || !s->url) continue;
        const LinkCheck *c = check_find(s->url, CHECK_FRESH_S);
        if (!c || !c->state || (c->state == 4 && g_app.time - c->at > 120)) continue;   /* a dead link is asked again soon: debrid links come back */
        check_apply(c, s);
        reused++;
    }
    if (reused) {
        st->probes_started += reused;
        LOGI("streams: %d source%s checked already (ahead, or moments ago)", reused, reused == 1 ? "" : "s");
        sort_items(st);
        if (st->probes_started >= PROBE_MAX) return;
    }
    int best = -1;
    for (int i = 0; i < st->n; i++) {
        Stream *s = &st->items[i];
        if (s->probe_state || !s->url || strncmp(s->url, "http", 4) != 0 || !playable(s)) continue;
        if (best < 0 || s->score > st->items[best].score) best = i;
    }
    if (best < 0) return;
    Stream *s = &st->items[best];
    s->probe_state = 1;
    st->probes_started++;
    st->probe_inflight = 1;
    ProbeReq *q = malloc(sizeof *q);
    q->owner = st; q->gen = st->probe_gen; q->url = strdup(s->url);
    HttpOptions opt = { .range_bytes = PROBE_BYTES, .range_start = 0, .timeout_s = 5, .no_json_accept = 1 };   /* no headers: the TV's player can't send them either */
    http_request_async_ex(s->url, &opt, on_probe, q);
}

/* Best-scoring playable source, or -1 when nothing looks safe for this TV. */
/* When a sharper source lost, say why: the viewer sees 360p recommended and 1080p in the list. */
static void why_not_sharper(const Streams *st, int rec, char *out, size_t n)
{
    out[0] = 0;
    const Stream *r = &st->items[rec];
    int better = -1;
    for (int i = 0; i < st->n; i++) {
        const Stream *s = &st->items[i];
        if (s->tags.res <= (r->tags.res ? r->tags.res : 1080) || (better >= 0 && s->score <= st->items[better].score)) continue;
        better = i;
    }
    if (better < 0) return;
    const Stream *b = &st->items[better];
    char res[16]; if (b->tags.res >= 2160) snprintf(res, sizeof res, "4K"); else snprintf(res, sizeof res, "%dp", b->tags.res);
    const char *warn[1];
    QualityPrefs qp = { g_app.quality_mode, g_app.res_cap };
    if (!playable(b)) snprintf(out, n, "The %s sources need a torrent streaming server", res);
    else if (b->tags.cached < 0) snprintf(out, n, "The best %s source is not cached yet: it needs a debrid download first", res);
    else if (b->probe_state == 4) snprintf(out, n, "The best %s link is not responding", res);
    else if (b->probe_state == 2 && b->measured_mbps > 0 && b->file_mbps > b->measured_mbps * 0.85) snprintf(out, n, "The best %s file is heavier than this connection carries", res);
    else if (quality_warnings(&b->tags, &g_app.tv, &qp, warn, 1)) snprintf(out, n, "The best %s source: %s", res, warn[0]);
    else if (b->tags.res > g_app.res_cap && g_app.res_cap) snprintf(out, n, "%s is above your resolution limit in Settings > Sources", res);
}

static int recommended(const Streams *st)
{
    int best = -1;
    for (int pass = 0; pass < 2 && best < 0; pass++)   /* P2P only when nothing else plays */
        for (int i = 0; i < st->n; i++)
            if (!ineligible(st, &st->items[i]) && st->items[i].score > -500 && is_p2p(&st->items[i]) == pass
                && (best < 0 || st->items[i].score > st->items[best].score)) best = i;
    return best;
}

typedef struct { long rank; int at; } RankKey;

/* by rank; equal ranks keep the order they had (the sort stays stable) */
static int cmp_rank(const void *x, const void *y)
{
    const RankKey *a = x, *b = y;
    if (a->rank != b->rank) return a->rank < b->rank ? -1 : 1;
    return a->at - b->at;
}

static void sort_items(Streams *st)
{
    Uint32 t0 = SDL_GetTicks();
    QualityPrefs qp = { g_app.quality_mode, g_app.res_cap };
    double runtime = quality_runtime_minutes(st->meta.runtime);
    for (int i = 0; i < st->n; i++) {
        Stream *s = &st->items[i];
        s->score = quality_score(&s->tags, &g_app.tv, &qp, runtime);
        { int al = has_audio_lang(s); if (al == 2) s->score += 250; else if (al == 1) s->score += 120; }
        if (is_mine(s)) s->score += 1500;           /* your TorBox first: it is what you chose to keep */
        else if (from_torbox(s)) s->score += 600;
        if (rule_broken(s)) s->score -= 2000;                            /* Settings > Sources > Rules */
        if (s->url) s->score -= srchealth_penalty(s->url, s->addon_name);   /* servers that keep failing here, at this hour */
        if (s->probe_state == 4) s->score -= 3000;                       /* the link does not answer */
        else if (s->probe_state == 2) {
            s->score += 60;                                              /* alive and understood */
            if (s->measured_mbps > 0 && s->file_mbps > s->measured_mbps * 0.85) s->score -= 400 + (long)((s->file_mbps / s->measured_mbps - 0.85) * 400);
            if (s->probe && s->probe->bit_depth > 8 && s->probe->vcodec == CODEC_AVC) s->score -= 500;   /* 10-bit H.264: no TV decodes it */
        }
    }
    /* the focused source stays focused while answers and checks re-order the list */
    int keep = st->moved && st->focus >= 0 && st->focus < st->n;   /* untouched, the focus stays on the top */
    const char *keep_url = keep ? st->items[st->focus].url : NULL;
    const char *keep_hash = keep ? st->items[st->focus].info_hash : NULL;
    /* each rank worked out once (it looks through the TorBox and dead-link lists), then one
     * O(n log n) sort: with a dozen addons answering a few thousand sources, comparing
     * by rank in an insertion sort held a TV's main thread for seconds */
    RankKey *keys = st->n > 1 ? malloc(sizeof *keys * (size_t)st->n) : NULL;
    Stream *sorted = keys ? malloc(sizeof *sorted * (size_t)st->n) : NULL;
    if (keys && sorted) {
        for (int i = 0; i < st->n; i++) { keys[i].rank = rank(st, &st->items[i]); keys[i].at = i; }
        qsort(keys, (size_t)st->n, sizeof *keys, cmp_rank);
        for (int i = 0; i < st->n; i++) sorted[i] = st->items[keys[i].at];
        memcpy(st->items, sorted, sizeof *sorted * (size_t)st->n);
    }
    free(keys); free(sorted);
    if (keep_url || keep_hash)
        for (int i = 0; i < st->n; i++) if (st->items[i].url == keep_url && st->items[i].info_hash == keep_hash) { st->focus = i; break; }
    Uint32 took = SDL_GetTicks() - t0;
    if (took > 20) LOGW("perf: sorting %d sources took %u ms", st->n, took);
}

static void play_from(Streams *st, int index);

/* Parse one addon's answer into `items`; returns how many were added. */
static int absorb(Stream **items, int *count, HttpResponse *r, int addon)
{
    const char *addon_name = addon < 0 ? "My TorBox" : addon < g_app.n_addons ? g_app.addons[addon].manifest.name : "addon";
    ParsedStreams *ps = r->parsed;   /* parsed and tagged on the network thread */
    if (r->status != 200 || !ps) {
        LOGW("streams: %s failed (%ld %s)", addon_name, r->status, r->error);
        return 0;
    }
    r->parsed = NULL;
    Stream *got = ps->s; int n = ps->n;
    free(ps);
    *items = realloc(*items, sizeof(Stream) * (size_t)(*count + n + 1));
    for (int i = 0; i < n; i++) {
        got[i].addon_name = strdup(addon_name);
        got[i].addon_order = addon;
        (*items)[(*count)++] = got[i];
    }
    free(got);
    return n;
}

/* Autoplay: as soon as a same-group stream shows up, or when everyone answered, or the
 * wait is over (20 s at most, even set to wait for every addon). The best row that can
 * play here: a notice or a link this TV cannot open at the top does not stop it. */
static void autoplay_check(Streams *st)
{
    if (!st->chaining || st->autoplayed || !st->n) return;
    int best = -1;
    for (int i = 0; i < st->n && best < 0; i++) if (!ineligible(st, &st->items[i])) best = i;
    if (best < 0) return;
    int match = same_release(st, &st->items[best]) > 0;
    float waited_s = g_app.time - st->started;
    int waited = (g_app.addon_wait < 99 && waited_s > g_app.addon_wait) || waited_s > 20;
    if (match || st->pending == 0 || waited) {
        LOGI("streams: next episode from %s%s", match ? "the same release" : "the closest source (no same release)",
             st->pending && !match ? ", done waiting for the other addons" : "");
        st->autoplayed = 1;
        play_from(st, best);
    }
}

static void on_streams(HttpResponse *r, void *user)
{
    Pending *p = user;
    Streams *st = p->owner;
    if (st != g_live || p->generation != g_generation) { free(p); return; }
    st->pending--;
    if (absorb(&st->items, &st->n, r, p->addon)) { sort_items(st); app_redraw(); }
    if (!st->pending && !st->sources_timed) { st->sources_timed = 1; perf_start_phase("sources"); }
    free(p);
    autoplay_check(st);
}

/* Your TorBox, asked like one more addon: the videos there that are this title */
typedef struct { char type[16], video_id[160], name[256], year[8], own[96], others[40][96]; int n_others; } LibAsk;

static void library_task(HttpResponse *r, void *arg)
{
    LibAsk *a = arg;
    ParsedStreams *ps = calloc(1, sizeof *ps);
    const char *others[40];
    for (int i = 0; i < a->n_others; i++) others[i] = a->others[i];
    ps->n = torbox_library_streams(a->type, a->video_id, a->name, a->year[0] ? a->year : NULL, a->own, others, a->n_others, &ps->s);
    r->status = 200;
    r->parsed = ps;
    free(a);
}

static void query_addons(const MetaPreview *meta, const char *video_id, Streams *owner, unsigned generation, HttpCallback cb, int *pending, int *n_reqs)
{
    const char *type = meta->type;
    if (torbox_enabled() && meta->name && type) {
        LibAsk *a = calloc(1, sizeof *a);
        snprintf(a->type, sizeof a->type, "%s", type);
        snprintf(a->video_id, sizeof a->video_id, "%s", video_id);
        snprintf(a->name, sizeof a->name, "%s", meta->name);
        if (meta->release_info) snprintf(a->year, sizeof a->year, "%.4s", meta->release_info);
        int nv = 0, season = -1, episode = -1;   /* this episode's title and its season's others (the episode list is loaded) */
        const MetaVideo *v = app_videos(meta->id, &nv);
        for (int i = 0; i < nv; i++) if (v[i].id && !strcmp(v[i].id, video_id)) { season = v[i].season; episode = v[i].episode; if (v[i].title) snprintf(a->own, sizeof a->own, "%s", v[i].title); }
        for (int i = 0; i < nv && a->n_others < 40 && season >= 0; i++)
            if (v[i].season == season && v[i].episode != episode && v[i].title && v[i].title[0]) snprintf(a->others[a->n_others++], sizeof a->others[0], "%s", v[i].title);
        Pending *p = malloc(sizeof *p);
        p->owner = owner; p->addon = -1; p->generation = generation;
        (*pending)++;
        if (n_reqs) (*n_reqs)++;
        http_task_async_ex("torbox library", library_task, a, app_free_streams, cb, p);
    }
    for (int i = 0; i < g_app.n_addons; i++) {
        if (!addon_supports(&g_app.addons[i], "stream", type, video_id)) continue;
        char url[1024];
        addon_url_stream(&g_app.addons[i], type, video_id, url, sizeof url);
        Pending *p = malloc(sizeof *p);
        p->owner = owner; p->addon = i; p->generation = generation;
        (*pending)++;
        if (n_reqs) (*n_reqs)++;
        app_fetch_streams(url, cb, p);
    }
}

static void query(Streams *st) { query_addons(&st->meta, st->video_id, st, g_generation, on_streams, &st->pending, &st->n_reqs); }

static void prefetch_clear(void)
{
    g_pre.generation++;   /* answers still in flight are dropped */
    streams_free(g_pre.items, g_pre.n);
    free(g_pre.video_id);
    if (g_pre.has_meta) meta_preview_free(&g_pre.meta);
    memset(&g_pre, 0, sizeof g_pre);
    g_pre.generation = g_generation + 1000;
}

static void on_ahead(HttpResponse *r, void *user)
{
    char *url = user;
    check_record(url, r);
    free(url);
}

/* Every answer is in: the source Play would start is checked now, while the viewer reads
 * the page, so Play has nothing left to wait for. */
static void check_ahead(void)
{
    if (!g_app.preflight || !g_pre.n || !g_pre.has_meta || getenv("SNTV_NO_CHECK_AHEAD")) return;
    Streams tmp;
    memset(&tmp, 0, sizeof tmp);
    tmp.items = g_pre.items; tmp.n = g_pre.n; tmp.meta = g_pre.meta; tmp.video_id = g_pre.video_id;   /* borrowed: ranked as the screen would */
    sort_items(&tmp);
    int r = recommended(&tmp);
    if (r < 0) return;
    const Stream *s = &tmp.items[r];
    if (!s->url || strncmp(s->url, "http", 4) || torrent_engine_url_is(s->url) || check_find(s->url, CHECK_FRESH_S)) return;
    LOGI("streams: checking the best of %d prefetched sources ahead", g_pre.n);
    HttpOptions opt = { .range_bytes = PROBE_BYTES, .range_start = 0, .timeout_s = 5, .no_json_accept = 1 };   /* as the screen checks it */
    http_request_async_ex(s->url, &opt, on_ahead, strdup(s->url));
}

static void on_prefetch(HttpResponse *r, void *user)
{
    Pending *p = user;
    if (p->generation != g_pre.generation) { free(p); return; }
    g_pre.pending--;
    Streams *st = g_live && g_live->from_prefetch && g_live->video_id && g_pre.video_id && strcmp(g_live->video_id, g_pre.video_id) == 0 ? g_live : NULL;
    if (st) {   /* the screen for this episode is already open: feed it directly */
        st->pending--;
        if (absorb(&st->items, &st->n, r, p->addon)) { sort_items(st); app_redraw(); }
        free(p);
        autoplay_check(st);
        return;
    }
    absorb(&g_pre.items, &g_pre.n, r, p->addon);
    free(p);
    if (g_pre.pending == 0) { LOGI("streams: prefetched %d sources for %s", g_pre.n, g_pre.video_id); check_ahead(); }
}

void streams_prefetch(const MetaPreview *meta, const char *video_id)
{
    if (!video_id || !meta->type) return;
    if (g_pre.video_id && strcmp(g_pre.video_id, video_id) == 0 && g_app.time - g_pre.started < PREFETCH_FRESH_S && g_pre.addons_version == g_app.addons_version) return;
    prefetch_clear();
    g_pre.video_id = strdup(video_id);
    meta_preview_copy(&g_pre.meta, meta); g_pre.has_meta = 1;
    g_pre.started = g_app.time;
    g_pre.addons_version = g_app.addons_version;
    query_addons(meta, video_id, NULL, g_pre.generation, on_prefetch, &g_pre.pending, NULL);
    g_pre.total = g_pre.pending;
    if (g_pre.pending) LOGI("streams: prefetching %s from %d addon%s", video_id, g_pre.pending, g_pre.pending == 1 ? "" : "s");
}

void streams_prefetch_again(const MetaPreview *meta, const char *video_id)
{
    if (g_live && g_live->from_prefetch && g_live->video_id && video_id && !strcmp(g_live->video_id, video_id)) return;   /* already taken over */
    LOGI("streams: asking again for %s, the answers are %.0f min old", video_id, g_pre.video_id ? (g_app.time - g_pre.started) / 60 : 0);
    prefetch_clear();
    streams_prefetch(meta, video_id);
}

float streams_prefetch_age(const char *video_id)
{
    if (!video_id || !g_pre.video_id || strcmp(g_pre.video_id, video_id) != 0) return -1;
    return g_app.time - g_pre.started;
}

float streams_prefetch_progress(const char *video_id)
{
    if (!video_id || !g_pre.video_id || strcmp(g_pre.video_id, video_id) != 0 || g_pre.total <= 0) return -1;
    if (g_app.time - g_pre.started > PREFETCH_FRESH_S) return -1;
    return (float)(g_pre.total - g_pre.pending) / (float)g_pre.total;
}

static void on_warm_next(HttpResponse *r, void *user)
{
    (void)user;
    LOGI("streams: next episode's file warmed (%ld, %zu KB)", r->status, r->len / 1024);
}

int streams_prefetch_warm(const char *video_id, const char *binge_group)
{
    if (streams_prefetch_progress(video_id) < 1 || !g_pre.n) return 0;
    Streams st;
    memset(&st, 0, sizeof st);
    st.items = g_pre.items; st.n = g_pre.n; st.video_id = g_pre.video_id;
    sort_items(&st);   /* the order the Sources screen will have */
    int pick = -1;
    for (int pass = 0; pass < 2 && pick < 0; pass++)
        for (int i = 0; i < g_pre.n && pick < 0; i++) {
            const Stream *s = &g_pre.items[i];
            if (!s->url || strncmp(s->url, "http", 4) || ineligible(&st, s)) continue;
            if (pass == 0 && !(binge_group && s->binge_group && !strcmp(binge_group, s->binge_group))) continue;
            pick = i;
        }
    if (pick < 0) return 0;
    LOGI("streams: warming the next episode's file (%s)", g_pre.items[pick].addon_name ? g_pre.items[pick].addon_name : "addon");
    HttpOptions o = { .range_bytes = 2u << 20, .timeout_s = 20, .no_json_accept = 1 };
    http_request_async_ex(g_pre.items[pick].url, &o, on_warm_next, NULL);
    return 1;
}

/* Take over a prefetch for this episode. 1 when there was one. */
static int adopt_prefetch(Streams *st)
{
    if (!g_pre.video_id || strcmp(g_pre.video_id, st->video_id) != 0 || (!g_pre.n && !g_pre.pending)) return 0;   /* nobody was asked: the screen asks itself */
    if (g_app.time - g_pre.started > PREFETCH_FRESH_S) { prefetch_clear(); return 0; }
    st->items = g_pre.items; st->n = g_pre.n;
    g_pre.items = NULL; g_pre.n = 0;
    st->pending = g_pre.pending;
    st->n_reqs = g_pre.pending + st->n;
    st->from_prefetch = 1;
    st->check_autoplay = 1;
    sort_items(st);
    LOGI("streams: using %d prefetched sources for %s (%d answer%s still coming)", st->n, st->video_id, st->pending, st->pending == 1 ? "" : "s");
    if (!st->pending) prefetch_clear();
    return 1;
}

static void first_line(const char *s, char *out, size_t n)
{
    if (!s) { out[0] = 0; return; }
    size_t i = 0;
    for (; s[i] && s[i] != '\n' && i + 1 < n; i++) out[i] = s[i];
    out[i] = 0;
}

static void rest_lines(const char *s, char *out, size_t n)
{
    out[0] = 0;
    if (!s) return;
    const char *nl = strchr(s, '\n');
    if (!nl) return;
    size_t o = 0;
    for (const char *p = nl + 1; *p && o + 1 < n; p++) {
        if (*p == '\n') { if (o + 3 < n) { memcpy(out + o, " | ", 3); o += 3; } }
        else out[o++] = *p;
    }
    out[o] = 0;
}

/* The five best by score, once every addon answered: Diagnostics shows why a pick was made. */
static void log_ranking(Streams *st)
{
    int top[5], n = 0;
    for (int i = 0; i < st->n; i++) {
        int k;
        if (n < 5) k = n++;
        else if (st->items[i].score > st->items[top[4]].score) k = 4;
        else continue;
        top[k] = i;
        for (; k > 0 && st->items[top[k]].score > st->items[top[k - 1]].score; k--) { int t = top[k]; top[k] = top[k - 1]; top[k - 1] = t; }
    }
    for (int j = 0; j < n; j++) {
        const Stream *s = &st->items[top[j]];
        char l1[80]; first_line(s->name, l1, sizeof l1);
        LOGI("streams: #%d score %ld  %s %s  res %d cached %d probe %d  %.0f/%.0f Mbit/s", j + 1, s->score, s->addon_name, l1,
             s->tags.res, s->tags.cached, s->probe_state, s->file_mbps, s->measured_mbps);
    }
}

/* Starting by itself, no video yet: the next episode, or Play's pick still being made. */
static int starting(const Streams *st)
{
    return !st->launched && ((st->chaining && !st->autoplayed) || ((g_app.auto_recommended || st->play_now) && !st->auto_rec_done));
}

int streams_starting(void) { return g_live && starting(g_live); }

/* While slow addons are still answering: the best so far is already what they could
 * bring (ready on debrid or your TorBox, at your quality cap, its check passed), so the
 * start does not wait for them. Their answers still join the list. */
static int good_enough(const Streams *st)
{
    if (!st->pending) return 0;
    int r = recommended(st);
    if (r < 0) return 0;
    const Stream *s = &st->items[r];
    if (s->tags.res < g_app.res_cap) return 0;
    if (s->tags.cached <= 0 && !is_mine(s) && !from_torbox(s)) return 0;
    if (g_app.preflight && s->probe_state != 2 && s->probe_state != 3) return 0;
    return 1;
}

static void update(Screen *s, float dt)
{
    Streams *st = s->st;
    video_keep_awake(starting(st));   /* the TV's screensaver must not take the handoff */
    if (!st->ranks_logged && st->n && st->pending == 0) { st->ranks_logged = 1; log_ranking(st); }
    if (st->check_autoplay || (st->chaining && !st->autoplayed)) {   /* every frame too: the wait can run out with no answer arriving */
        st->check_autoplay = 0; autoplay_check(st); if (app_top() != s) return;
    }
    if (st->n && !st->chaining && (st->pending == 0 || g_app.time - st->started > 3)) probe_next(st);
    if (st->probe_inflight) app_redraw();
    int checks_done = !g_app.preflight || (!st->probe_inflight && (st->probes_started >= PROBE_MAX || g_app.time - st->started > 12));
    if (st->play_now)   /* asked to play: the best-ranked link's check is enough */
        checks_done = !g_app.preflight || (!st->probe_inflight && st->probes_started >= 1) || g_app.time - st->started > 5;
    int waited = st->pending == 0 || g_app.time - st->started > (g_app.addon_wait < 6 ? g_app.addon_wait : 6);
    if ((g_app.auto_recommended || st->play_now) && !st->chaining && !st->auto_rec_done && st->n && checks_done && (waited || good_enough(st))) {
        int r = recommended(st);
        if (!waited) LOGI("streams: the best so far is ready at %dp; not waiting for %d slower addon%s", st->items[r].tags.res, st->pending, st->pending == 1 ? "" : "s");
        for (int i = 0; r < 0 && st->play_now && i < st->n; i++) if (playable(&st->items[i])) r = i;   /* asked to play: the top source that can, the player falls back from there */
        if (r >= 0) {
            st->auto_rec_done = 1;
            if (!st->play_now) app_toast("Playing the recommended source. Back returns to the list.");   /* asked to play: no surprise to explain */
            play_from(st, r);
            if (app_top() != s) return;
        }
    }
    int first = st->focus - (VISIBLE - 3);
    st->scroll_target = (first < 0 ? 0 : first) * ROW_H;
    st->scroll = ui_ease(st->scroll, st->scroll_target, NULL, dt, 14);
    if (st->pending) app_redraw();
}

/* ---- quality badges parsed from the stream's text ---- */

static int has_ci(const char *hay, const char *needle)
{
    if (!hay) return 0;
    size_t n = strlen(needle);
    for (const char *p = hay; *p; p++) {
        size_t i = 0;
        while (i < n && p[i] && tolower((unsigned char)p[i]) == tolower((unsigned char)needle[i])) i++;
        if (i == n) {
            /* word-ish boundaries for short tokens like "dv" */
            int left_ok = p == hay || !isalnum((unsigned char)p[-1]);
            int right_ok = !isalnum((unsigned char)p[n]);
            if (n > 3 || (left_ok && right_ok)) return 1;
        }
    }
    return 0;
}

static void find_size(const char *t, char *out, size_t n)
{
    out[0] = 0;
    if (!t) return;
    for (const char *p = t; *p; p++) {
        if (!isdigit((unsigned char)*p) || (p > t && (isdigit((unsigned char)p[-1]) || p[-1] == '.'))) continue;
        char *end;
        double v = strtod(p, &end);
        while (*end == ' ') end++;
        if ((end[0] == 'G' || end[0] == 'g' || end[0] == 'M' || end[0] == 'm') && (end[1] == 'B' || end[1] == 'b')) {
            snprintf(out, n, "%.*f %cB", v < 10 ? 1 : 0, v, toupper((unsigned char)end[0]));
            return;
        }
    }
}

/* The badges of a source, best known first: resolution, HDR, codec, audio, cached, size. */
static int badge_list(const Stream *sm, char out[][24], int max)
{
    char text[1200];
    snprintf(text, sizeof text, "%s %s %s %s", sm->name ? sm->name : "", sm->title ? sm->title : "",
             sm->description ? sm->description : "", sm->filename ? sm->filename : "");
    const char *res = has_ci(text, "2160p") || has_ci(text, "4k") || has_ci(text, "uhd") ? "4K" :
                      has_ci(text, "1440p") ? "1440p" : has_ci(text, "1080p") ? "1080p" : has_ci(text, "720p") ? "720p" :
                      has_ci(text, "480p") ? "SD" : NULL;
    const char *hdr = has_ci(text, "dolby vision") || has_ci(text, "dovi") || sm->tags.hdr == 2 ? "DOLBY VISION" :
                      has_ci(text, "hdr10+") ? "HDR10+" : has_ci(text, "hdr") ? "HDR" : NULL;
    const char *codec = has_ci(text, "av1") ? "AV1" : has_ci(text, "hevc") || has_ci(text, "x265") || has_ci(text, "h265") || has_ci(text, "h.265") ? "HEVC" : NULL;
    const char *audio = has_ci(text, "atmos") ? "ATMOS" : has_ci(text, "truehd") ? "TRUEHD" : has_ci(text, "dts") ? "DTS" :
                        has_ci(text, "7.1") ? "7.1" : has_ci(text, "5.1") ? "5.1" : NULL;
    const char *cached = has_ci(text, "[rd+]") || has_ci(text, "[tb+]") || has_ci(text, "[ad+]") || has_ci(text, "[pm+]") || has_ci(text, "\xE2\x9A\xA1") || sm->tags.cached > 0 ? "CACHED" : NULL;
    char size[24];
    find_size(text, size, sizeof size);
    if (!size[0] && sm->video_size) snprintf(size, sizeof size, "%.1f GB", sm->video_size / 1073741824.0);
    const char *all[6] = { res, hdr, codec, audio, cached, size[0] ? size : NULL };
    int n = 0;
    for (int i = 0; i < 6 && n < max; i++) if (all[i]) snprintf(out[n++], 24, "%s", all[i]);
    return n;
}

/* A badge's colours, by what it says: the same on the Sources page and in the player. */
static void badge_colors(const char *t, int foc, Color *bg, Color *fg)
{
    int res = !strcmp(t, "4K") || !strcmp(t, "1440p") || !strcmp(t, "1080p") || !strcmp(t, "720p") || !strcmp(t, "SD");
    int size = strstr(t, " GB") || strstr(t, " MB");
    if (res) { *bg = !strcmp(t, "4K") ? THEME_GOLD : COLOR_ALPHA(THEME_TEXT, 0xE8); *fg = THEME_ON_LIGHT; }
    else if (!strcmp(t, "DOLBY VISION") || !strncmp(t, "HDR", 3)) { *bg = RGBA(0x8B, 0x5C, 0xF6, 0xFF); *fg = THEME_TEXT; }
    else if (!strcmp(t, "CACHED")) { *bg = THEME_OK; *fg = THEME_ON_LIGHT; }
    else if (!strcmp(t, "RECOMMENDED")) { *bg = THEME_ACCENT; *fg = THEME_ON_ACCENT; }
    else if (size) { *bg = COLOR_ALPHA(THEME_TEXT, 0x12); *fg = THEME_TEXT_DIM; }
    else { *bg = COLOR_ALPHA(THEME_TEXT, 0x22); *fg = THEME_TEXT; }
    /* translucent chips vanish on the lit focused row: give them a solid ground there */
    if (foc && (*bg & 0xFF) < 0x80) { *bg = COLOR_ALPHA(THEME_PANEL, 0xF0); if (*fg == THEME_TEXT_DIM) *fg = THEME_TEXT; }
}

static float draw_badges(const Stream *sm, float right_x, float y, int foc)
{
    char b[8][24];
    int n = badge_list(sm, b, 8);
    float x = right_x;
    for (int i = n - 1; i >= 0; i--) {
        float w = font_measure(b[i], FONT_XS | FONT_B) + 24;
        x -= w;
        Color bg, fg;
        badge_colors(b[i], foc, &bg, &fg);
        ui_chip(b[i], x, y, bg, fg);
        x -= 8;
    }
    return x;
}

float streams_draw_badges(const char *list, float x, float y, float max_w, int foc)
{
    char t[24];
    float right = x + max_w;
    for (const char *p = list; p && *p; ) {
        size_t l = strcspn(p, "|");
        snprintf(t, sizeof t, "%.*s", (int)(l < 23 ? l : 23), p);
        p += l + (p[l] == '|');
        float w = font_measure(t, FONT_XS | FONT_B) + 24;
        if (x + w > right) break;   /* what does not fit is left out, never cut */
        Color bg, fg;
        badge_colors(t, foc, &bg, &fg);
        ui_chip(t, x, y, bg, fg);
        x += w + 8;
    }
    return x;
}

static void aperture_source_summary(const Stream *sm, double runtime, char *out, size_t n)
{
    char desc[180];
    if (sm->probe_state == 2 && sm->probe) {
        probe_describe(sm->probe, desc, sizeof desc);
        snprintf(out, n, "Checked file  /  %s", desc);
    } else {
        quality_summary(&sm->tags, runtime, desc, sizeof desc);
        snprintf(out, n, "%s  /  %s", sm->probe_state == 1 ? "Checking file" : "From source name", desc[0] ? desc : "Format unknown");
    }
}

static void draw_aperture(Streams *st)
{
    char buf[512], line[300];
    ui_background(0);
    font_draw_tracked("APERTURE  /  SOURCES", THEME_MARGIN, 50, FONT_XS, THEME_TEXT_DIM, 1.5f);
    ui_aperture_art(app_art_poster(&st->meta, buf, sizeof buf), THEME_MARGIN, 120, 360, 540, 1);
    font_draw_wrapped(st->meta.name, THEME_MARGIN, 696, FONT_L | FONT_B, THEME_TEXT, 420, 2);
    if (st->video_title) font_draw_fit(st->video_title, THEME_MARGIN, 812, FONT_S, THEME_TEXT_DIM, 420);
    font_draw("Back  Return to title", THEME_MARGIN, 960, FONT_XS, THEME_TEXT_DIM);
    float x = LIST_X, w = SNTV_UI_W - THEME_MARGIN - x;
    font_draw("Choose a source", x, 92, FONT_L | FONT_B, THEME_TEXT);
    snprintf(line, sizeof line, "%d sources  /  %d addons searching", st->n, st->pending);
    font_draw(st->n || st->pending ? line : "No sources. Check your stream addons in Settings.", x, 150, FONT_S, THEME_TEXT_DIM);
    if (st->pending) ui_spinner(x + w - 28, 140, 18);
    if (!st->n && st->pending) {
        for (int i = 0; i < 4; i++) ui_skeleton(x, LIST_Y + i * ROW_H, w, ROW_H - 14, R_PANEL);
        return;
    }
    int rec = recommended(st);
    double runtime = quality_runtime_minutes(st->meta.runtime);
    float list_y = LIST_Y;
    if (rec >= 0) {
        const Stream *sm = &st->items[rec];
        int foc = st->focus == -1;
        gfx_rect(x, LIST_Y, w, REC_H - 16, foc ? THEME_FOCUS : THEME_BG2);
        if (foc) ui_focus_under(x, LIST_Y, w, REC_H - 16, R_PANEL, 1);
        Color fg = foc ? THEME_ON_FOCUS : THEME_TEXT;
        font_draw_tracked("RECOMMENDED FOR THIS TV", x + 28, LIST_Y + 18, FONT_XS, fg, 1);
        first_line(sm->name, line, sizeof line);
        font_draw_fit(line[0] ? line : sm->addon_name, x + 28, LIST_Y + 49, FONT_M | FONT_B, fg, w - 230);
        aperture_source_summary(sm, runtime, line, sizeof line);
        font_draw_fit(line, x + 28, LIST_Y + 96, FONT_XS, fg, w - 230);
        ui_icon(ICON_PLAY, x + w - 66, LIST_Y + 67, 28, fg);
        list_y += REC_H;
    }
    gfx_clip_push(x - 8, list_y - 8, w + 16, SNTV_UI_H - list_y - 44);
    QualityPrefs qp = { g_app.quality_mode, g_app.res_cap };
    for (int i = 0; i < st->n; i++) {
        float y = list_y + i * ROW_H - st->scroll;
        if (y + ROW_H < list_y || y > SNTV_UI_H) continue;
        const Stream *sm = &st->items[i];
        int foc = st->focus == i, ok = playable(sm);
        gfx_rect(x, y + ROW_H - 10, w, 1, THEME_LINE);
        if (foc) { gfx_rect(x, y, w, ROW_H - 18, THEME_SURFACE); gfx_rect(x - 7, y + 20, 3, 40, THEME_ACCENT); }
        first_line(sm->name, line, sizeof line);
        font_draw_fit(line[0] ? line : "Stream", x + 24, y + 16, FONT_M | FONT_B, ok ? THEME_TEXT : THEME_TEXT_MUTED, w - 270);
        font_draw_fit(sm->addon_name, x + w - 230, y + 22, FONT_XS, THEME_TEXT_DIM, 210);
        {   /* the same badges as the other themes' Sources page: quality, size, cached */
            char b[8][24], list[200] = "";
            int nb = badge_list(sm, b, 8);
            for (int k = 0; k < nb; k++) { size_t u = strlen(list); snprintf(list + u, sizeof list - u, "%s%s", u ? "|" : "", b[k]); }
            if (list[0]) streams_draw_badges(list, x + w - 230, y + 54, 210, 0);
        }
        aperture_source_summary(sm, runtime, line, sizeof line);
        const char *warn[1];
        char why[160] = "";
        if (!ok) { char *u = app_stream_playable_url(sm, why, sizeof why); free(u); }   /* the reason it cannot play here */
        const char *gate = ok ? ineligible(st, sm) : NULL;
        const char *note = !ok ? (why[0] ? why : "Cannot play on this TV") : gate && !strcmp(gate, "it is another episode") ? "Another episode" : rule_broken(sm) ? "Against your rules" : sm->probe_state == 4 ? "Link not responding"
            : sm->probe_state == 2 && sm->measured_mbps > 0 && sm->file_mbps > sm->measured_mbps * 0.85 ? "Heavier than this connection"
            : quality_warnings(&sm->tags, &g_app.tv, &qp, warn, 1) ? warn[0] : sm->url ? srchealth_note(sm->url, sm->addon_name) : NULL;
        const char *flag = is_mine(sm) ? "In your TorBox" : from_torbox(sm) ? "From your TorBox" : is_p2p(sm) ? "P2P on this TV" : NULL;
        if (!note && flag) { font_draw_fit(flag, x + 24, y + 62, FONT_XS | FONT_B, THEME_OK, w - 280); }
        else font_draw_fit(note ? note : line, x + 24, y + 62, FONT_XS, note ? THEME_DANGER : THEME_TEXT_DIM, w - 280);
        if (foc) {
            const char *file = sm->filename ? sm->filename : sm->description ? sm->description : sm->title;
            first_line(file, line, sizeof line);
            font_draw_fit(line, x + 24, y + 96, FONT_XS, THEME_TEXT_MUTED, w - 48);
        }
    }
    gfx_clip_pop();
}

static void draw(Screen *s)
{
    Streams *st = s->st;
    if (g_theme.id == THEME_APERTURE) { draw_aperture(st); return; }
    char buf[512];
    const char *bg = app_art_background(&st->meta, buf, sizeof buf);
    ui_background(image_tint(bg));
    int iw, ih;
    unsigned bgt = bg ? image_get_ex(bg, IMAGE_MIPMAP, &iw, &ih, NULL) : 0;
    if (bgt && !g_app.fx_light) ui_backdrop_blur(bgt, iw, ih, 0.85f);

    /* left column: poster + titles */
    float lx = THEME_MARGIN, ly = 120;
    char pbuf[512];
    ui_card(app_art_poster(&st->meta, pbuf, sizeof pbuf), lx, ly, 360, 540, 0, 0, st->meta.name, 0);
    font_draw_wrapped(st->meta.name, lx, ly + 570, FONT_L | FONT_B, THEME_TEXT, 420, 2);
    if (st->video_title) font_draw_wrapped(st->video_title, lx, ly + 570 + font_draw_wrapped(st->meta.name, 0, 0, FONT_L | FONT_B, 0, 420, 2) * font_height(FONT_L | FONT_B) + 8,
                                           FONT_S, THEME_TEXT_DIM, 420, 2);

    /* header */
    float x = LIST_X, w = SNTV_UI_W - THEME_MARGIN - LIST_X;
    font_draw("Choose a source", x, 104, FONT_L | FONT_B, THEME_TEXT);
    char msg[128];
    if (st->chaining && !st->autoplayed) snprintf(msg, sizeof msg, "Finding the next episode on the same release  ·  Up or Down to choose");
    else if ((st->play_now || g_app.auto_recommended) && !st->auto_rec_done && st->n) snprintf(msg, sizeof msg, "Starting the best source  ·  Up or Down to choose");
    else if (st->pending) snprintf(msg, sizeof msg, "%d found  ·  %d addon%s still searching", st->n, st->pending, st->pending == 1 ? "" : "s");
    else if (!st->n) snprintf(msg, sizeof msg, "%s", st->n_reqs ? "No sources found for this title" : "No stream addons installed. Add one in Settings.");
    else snprintf(msg, sizeof msg, "%d source%s", st->n, st->n == 1 ? "" : "s");
    font_draw(msg, x, 156, FONT_S, THEME_TEXT_MUTED);
    if (st->pending || (st->chaining && !st->autoplayed)) ui_spinner(x + w - 30, 140, 20);

    if (!st->n && st->pending) {
        for (int i = 0; i < 5; i++) ui_skeleton(x, LIST_Y + i * ROW_H, w, ROW_H - 14, 20);
        return;
    }

    int rec = recommended(st);
    QualityPrefs qp = { g_app.quality_mode, g_app.res_cap };
    double runtime = quality_runtime_minutes(st->meta.runtime);
    float list_y = LIST_Y;
    if (rec >= 0) {   /* the recommendation card */
        const Stream *sm = &st->items[rec];
        int foc = st->focus == -1;
        float h = REC_H - 16;
        if (foc) ui_focus_under(x, LIST_Y, w, h, R_PANEL, 1);
        gfx_rrect_grad_h(x, LIST_Y, w, h, R_PANEL, foc ? COLOR_ALPHA(THEME_TEXT, 0x2E) : COLOR_ALPHA(THEME_ACCENT, 0x38), foc ? COLOR_ALPHA(THEME_TEXT, 0x2E) : COLOR_ALPHA(THEME_ACCENT2, 0x30));
        if (foc) gfx_rrect_ring(x, LIST_Y, w, h, R_PANEL, 3, g_theme.focus_style == FOCUS_FRAME ? THEME_ACCENT : THEME_TEXT);
        gfx_circle(x + 44, LIST_Y + h / 2, 26, foc ? THEME_FOCUS : THEME_ACCENT2);
        ui_icon(ICON_STAR, x + 44, LIST_Y + h / 2, 26, foc ? THEME_ON_FOCUS : THEME_ON_ACCENT);
        static const char *HEAD[QUAL_MODE_COUNT] = { "Recommended for this TV", "Recommended for this TV", "Highest quality", "Balanced pick", "Smallest download" };
        font_draw(HEAD[g_app.quality_mode], x + 90, LIST_Y + 18, FONT_XS | FONT_B, THEME_ACCENT2);
        char l1[200], summary[200];
        first_line(sm->name, l1, sizeof l1);
        char head[300];
        snprintf(head, sizeof head, "%s%s%s", sm->addon_name, l1[0] ? "  \xC2\xB7  " : "", l1);
        font_draw_fit(head, x + 90, LIST_Y + 46, FONT_M | FONT_B, THEME_TEXT, w - 320);
        if (sm->probe_state == 2 && sm->probe) {
            char d[200];
            probe_describe(sm->probe, d, sizeof d);
            if (sm->file_mbps > 0 && sm->measured_mbps > 0) snprintf(summary, sizeof summary, "Verified: %.120s  \xC2\xB7  %.0f Mbit/s (this link gives %.0f)", d, sm->file_mbps, sm->measured_mbps);
            else if (sm->measured_mbps > 0) snprintf(summary, sizeof summary, "Verified: %.120s  \xC2\xB7  link %.0f Mbit/s", d, sm->measured_mbps);
            else snprintf(summary, sizeof summary, "Verified: %.150s", d);
        } else {
            quality_summary(&sm->tags, runtime, summary, sizeof summary);
            if (sm->probe_state == 1 || (g_app.preflight && !sm->probe_state && st->probes_started < PROBE_MAX)) {
                size_t u = strlen(summary);
                snprintf(summary + u, sizeof summary - u, "%s", summary[0] ? "  \xC2\xB7  checking the file..." : "Checking the file...");
            }
        }
        font_draw_fit(summary[0] ? summary : "Playable on this TV", x + 90, LIST_Y + 88, FONT_XS, THEME_TEXT_DIM, w - 320);
        char why[200];
        why_not_sharper(st, rec, why, sizeof why);
        if (why[0]) font_draw_fit(why, x + 90, LIST_Y + 114, FONT_XS, THEME_TEXT_MUTED, w - 320);
        ui_pill("Play", 1, ICON_PLAY, x + w - 170, LIST_Y + (h - 56) / 2, 56, foc ? PILL_PRIMARY : PILL_SECONDARY, foc);
        list_y += REC_H;
    }

    gfx_clip_push(0, list_y - 20, SNTV_UI_W, SNTV_UI_H - list_y + 20);
    float ry = list_y - st->scroll;
    for (int i = 0; i < st->n; i++, ry += ROW_H) {
        if (ry + ROW_H < LIST_Y - 20 || ry > SNTV_UI_H) continue;
        const Stream *sm = &st->items[i];
        int foc = st->focus == i;
        int ok = playable(sm);
        float h = ROW_H - 14;
        if (foc) ui_focus_under(x, ry, w, h, R_PANEL, 1);
        gfx_rrect(x, ry, w, h, R_PANEL, foc ? COLOR_ALPHA(THEME_TEXT, 0x2A) : COLOR_ALPHA(THEME_TEXT, 0x0E));
        if (foc) gfx_rrect_ring(x, ry, w, h, R_PANEL, 3, g_theme.focus_style == FOCUS_FRAME ? THEME_ACCENT : THEME_TEXT);
        char line1[200], line2[600], sub[300], rest[300];
        first_line(sm->name, line1, sizeof line1);
        rest_lines(sm->name, sub, sizeof sub);
        const char *desc = sm->description ? sm->description : sm->title;
        first_line(desc, line2, sizeof line2);
        rest_lines(desc, rest, sizeof rest);
        if (rest[0]) { size_t used = strlen(line2); snprintf(line2 + used, sizeof line2 - used, "  %s", rest); }
        /* addon badge */
        float ax = x + 24, ay = ry + 18;
        float aw = font_measure(sm->addon_name, FONT_XS | FONT_B) + 24;
        if (aw > 220) aw = 220;
        gfx_rrect_grad_h(ax, ay, aw, 32, theme_pill_r(32), COLOR_ALPHA(THEME_ACCENT, 0x60), COLOR_ALPHA(THEME_ACCENT2, 0x60));
        font_draw_fit(sm->addon_name, ax + 12, ay + (32 - font_height(FONT_XS | FONT_B)) / 2, FONT_XS | FONT_B, THEME_TEXT, aw - 24);
        if (sub[0]) font_draw_fit(sub, ax + aw + 12, ay + 4, FONT_XS | FONT_B, THEME_TEXT_DIM, 160);
        float bx = draw_badges(sm, x + w - 24, ry + 18, foc);
        font_draw_fit(line1[0] ? line1 : "Stream", ax, ry + 60, FONT_M | FONT_B, ok ? THEME_TEXT : THEME_TEXT_MUTED, w - 48);
        font_draw_fit(line2, ax, ry + 98, FONT_XS, ok ? THEME_TEXT_DIM : THEME_TEXT_MUTED, w - 48);
        char p2p[48];
        if (is_p2p(sm)) { if (sm->tags.seeders > 0) snprintf(p2p, sizeof p2p, "P2P \xC2\xB7 %d seeders", sm->tags.seeders); else snprintf(p2p, sizeof p2p, "P2P on this TV"); }
        const char *flag = is_mine(sm) ? "In your TorBox" : sm->info_hash ? (from_torbox(sm) ? "From your TorBox" : g_app.streaming_server ? NULL : is_p2p(sm) ? p2p : "Needs streaming server") : sm->yt_id ? "YouTube" : sm->external_url ? "Opens externally" : NULL;
        if (same_release(st, sm)) flag = "Same release";
        if (flag) {
            Color fc = !ok ? THEME_DANGER : THEME_OK;
            float fw = font_measure(flag, FONT_XS | FONT_B) + 24;
            bx -= fw + 8;
            ui_chip(flag, bx, ry + 18, foc ? COLOR_ALPHA(THEME_PANEL, 0xF0) : COLOR_ALPHA(fc, 0x30), fc);
        }
        const char *warn[3];
        int nw = ok ? quality_warnings(&sm->tags, &g_app.tv, &qp, warn, 3) : 0;
        const char *sick = sm->url ? srchealth_note(sm->url, sm->addon_name) : NULL;
        if (sick && !rule_broken(sm)) {
            float fw = font_measure(sick, FONT_XS | FONT_B) + 24;
            bx -= fw + 8;
            ui_chip(sick, bx, ry + 18, foc ? COLOR_ALPHA(THEME_PANEL, 0xF0) : COLOR_ALPHA(THEME_GOLD, 0x30), THEME_GOLD);
        } else if (rule_broken(sm)) {
            float fw = font_measure("Against your rules", FONT_XS | FONT_B) + 24;
            bx -= fw + 8;
            ui_chip("Against your rules", bx, ry + 18, foc ? COLOR_ALPHA(THEME_PANEL, 0xF0) : COLOR_ALPHA(THEME_TEXT_MUTED, 0x30), THEME_TEXT_MUTED);
        } else if (sm->probe_state == 4) {
            float fw = font_measure("Link not responding", FONT_XS | FONT_B) + 24;
            bx -= fw + 8;
            ui_chip("Link not responding", bx, ry + 18, foc ? COLOR_ALPHA(THEME_PANEL, 0xF0) : COLOR_ALPHA(THEME_DANGER, 0x30), THEME_DANGER);
        } else if (sm->probe_state == 2 && sm->measured_mbps > 0 && sm->file_mbps > sm->measured_mbps * 0.85) {
            float fw = font_measure("Heavier than this connection", FONT_XS | FONT_B) + 24;
            bx -= fw + 8;
            ui_chip("Heavier than this connection", bx, ry + 18, foc ? COLOR_ALPHA(THEME_PANEL, 0xF0) : COLOR_ALPHA(THEME_GOLD, 0x30), THEME_GOLD);
        } else if (sm->probe_state == 2 && !nw) {
            float fw = font_measure("Verified", FONT_XS | FONT_B) + 24;
            bx -= fw + 8;
            ui_chip("Verified", bx, ry + 18, foc ? COLOR_ALPHA(THEME_PANEL, 0xF0) : COLOR_ALPHA(THEME_OK, 0x30), THEME_OK);
        }
        if (nw) {
            float fw = font_measure(warn[0], FONT_XS | FONT_B) + 24;
            bx -= fw + 8;
            ui_chip(warn[0], bx, ry + 18, foc ? COLOR_ALPHA(THEME_PANEL, 0xF0) : COLOR_ALPHA(THEME_GOLD, 0x30), THEME_GOLD);
        } else if (i == rec) {
            float fw = font_measure("Recommended", FONT_XS | FONT_B) + 24;
            bx -= fw + 8;
            ui_chip("Recommended", bx, ry + 18, COLOR_ALPHA(THEME_ACCENT2, 0x40), THEME_TEXT);
        }
    }
    gfx_clip_pop();
    if (st->n > VISIBLE) {
        char pos[48];
        snprintf(pos, sizeof pos, "%d / %d", st->focus + 1, st->n);
        font_draw(pos, x + w - font_measure(pos, FONT_S | FONT_B), 156, FONT_S | FONT_B, THEME_TEXT_MUTED);
    }
}

static void remember_resume(Streams *st, const char *url)
{
    char key[160];
    snprintf(key, sizeof key, "resume:%s", st->meta.id);
    struct cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "url", url);
    cJSON_AddStringToObject(o, "video_id", st->video_id);
    store_set_json(key, o);
    store_save();
}

int streams_candidate(const MetaPreview *meta, const Stream *s, PlayCandidate *c)
{
    char why[8];
    char *u = app_stream_playable_url(s, why, sizeof why);
    if (!u) return 0;
    char label[200], l1[120], detail[200], rel[200];
    first_line(s->name, l1, sizeof l1);
    snprintf(label, sizeof label, "%s%s%s", s->addon_name ? s->addon_name : "", l1[0] ? " - " : "", l1);
    quality_summary(&s->tags, quality_runtime_minutes(meta->runtime), detail, sizeof detail);
    first_line(s->title ? s->title : s->description ? s->description : s->filename, rel, sizeof rel);
    memset(c, 0, sizeof *c);
    c->url = u;
    c->label = strdup(label);
    c->binge_group = s->binge_group ? strdup(s->binge_group) : NULL;
    c->filename = s->filename ? strdup(s->filename) : NULL;
    c->detail = detail[0] ? strdup(detail) : NULL;
    c->release = rel[0] ? strdup(rel) : NULL;
    c->addon_order = s->addon_order;
    c->video_size = s->video_size;
    c->headers = s->headers ? strdup(s->headers) : NULL;
    c->video_hash = s->video_hash ? strdup(s->video_hash) : NULL;
    c->subs = s->subs ? strdup(s->subs) : NULL;
    c->needs = quality_needs(&s->tags);
    c->res = s->tags.res;
    char b[8][24];
    int nb = badge_list(s, b, 8);
    for (int i = 0; i < nb; i++) { size_t u = strlen(c->badges); snprintf(c->badges + u, sizeof c->badges - u, "%s%s", u ? "|" : "", b[i]); }
    return 1;
}

/* "Never fails" Play: the top three sources are asked at once and the first that
 * answers starts; the rest stay behind it as the player's fallbacks. */
typedef struct {
    int busy, pending, done;
    PlayCandidate *c; int n; double start;
    MetaPreview meta; char video_id[160], title[300], next_id[128], next_title[200]; int has_next;
} Race;
static Race g_race;

static void race_launch(int first)   /* -2: no race, the first plays as chosen */
{
    Race *r = &g_race;
    if (first > 0) { PlayCandidate t = r->c[first]; memmove(r->c + 1, r->c, sizeof *r->c * (size_t)first); r->c[0] = t; }
    if (first != -2) LOGI("streams: %s", first >= 0 ? "the quickest source starts" : "no source answered the race; trying them in order");
    perf_start_phase("race");
    app_push(screen_player_create(&r->meta, r->video_id, r->title, r->c, r->n, 0, r->start, r->has_next ? r->next_id : NULL, r->has_next ? r->next_title : NULL));
    meta_preview_free(&r->meta);
    r->c = NULL; r->busy = 0;
}

typedef struct { int idx; unsigned round; } RaceReq;
static unsigned g_race_round;

/* The viewer took over (moved, chose, left): a race still running starts nothing. */
static void race_cancel(void)
{
    Race *r = &g_race;
    if (!r->busy) return;
    g_race_round++;
    LOGI("streams: race called off, the viewer chose");
    play_candidates_free(r->c, r->n);
    meta_preview_free(&r->meta);
    r->c = NULL; r->busy = 0;
}
static void on_race(HttpResponse *resp, void *user)
{
    RaceReq *q = user;
    Race *r = &g_race;
    if (q->round == g_race_round && r->busy && !r->done) {
        r->pending--;
        int ok = http_looks_like_media(resp);   /* a 200 with an error page is no video */
        if (!ok && r->c && q->idx < r->n) { app_link_failed(r->c[q->idx].url); LOGI("streams: source %d did not answer the race%s", q->idx + 1, resp->status == 200 || resp->status == 206 ? " with a video (an error page)" : ""); }   /* the player skips it */
        if (ok) { r->done = 1; LOGI("streams: source %d answered first (%.2f s)", q->idx + 1, resp->seconds); final_note(r->c[q->idx].url, resp); race_launch(q->idx); }
        else if (r->pending == 0) { r->done = 1; race_launch(-1); }
    }
    free(q);
}

static void play_raced(const Streams *st, PlayCandidate *c, int n, double start, const char *title, int has_next, const char *next_id, const char *next_title, int race)
{
    Race *r = &g_race;
    int racers = 0;
    for (int i = 0; i < n && i < 3; i++) if (!strncmp(c[i].url, "http", 4) && !torrent_engine_url_is(c[i].url)) racers++;
    if (r->busy) { play_candidates_free(c, n); app_toast("Finding the quickest source\xE2\x80\xA6"); return; }
    memset(r, 0, sizeof *r);
    r->c = c; r->n = n; r->start = start; r->has_next = has_next;
    meta_preview_copy(&r->meta, &st->meta);
    snprintf(r->video_id, sizeof r->video_id, "%s", st->video_id);
    snprintf(r->title, sizeof r->title, "%s", title);
    if (has_next) { snprintf(r->next_id, sizeof r->next_id, "%s", next_id); snprintf(r->next_title, sizeof r->next_title, "%s", next_title); }
    r->busy = 1;
    if (!race || racers < 2 || getenv("SNTV_NO_RACE")) { race_launch(-2); return; }   /* a source the viewer chose plays as chosen */
    g_race_round++;
    for (int i = 0; i < n && i < 3; i++) {
        if (strncmp(c[i].url, "http", 4) || torrent_engine_url_is(c[i].url)) continue;
        RaceReq *q = malloc(sizeof *q);
        q->idx = i; q->round = g_race_round;
        r->pending++;
        HttpOptions o = { .range_bytes = 4096, .timeout_s = 3, .no_json_accept = 1 };   /* enough to tell a page from a file; none answering costs 3 s at most, then they play in order */
        http_request_async_ex(c[i].url, &o, on_race, q);
    }
    LOGI("streams: racing %d sources", r->pending);
}

/* Build the fallback list: the chosen stream first, then other playable ones (same
 * binge group preferred), capped so a bad addon can't stall for ages. The best P2P
 * source rides along last, so TorBox being down still ends in something that plays. */
/* A YouTube video from an addon (YouTubio, trailer addons): streamed the way Nuvio
 * plays YouTube, then opened like any other source. */
static struct { int busy; MetaPreview meta; char video_id[160], title[300], yt[16]; } g_yt;
static void on_yt_stream(const char *url, const char *error, void *user)
{
    (void)user;
    g_yt.busy = 0;
    if (!url) { app_toast("YouTube would not play this one%s%s", error ? ": " : "", error ? error : ""); meta_preview_free(&g_yt.meta); return; }
    static char urls[YT_PLAY_URLS][4608];
    int nu = youtube_player_urls(url, g_yt.yt, urls, YT_PLAY_URLS);
    if (!nu) { app_toast("YouTube would not play this one"); meta_preview_free(&g_yt.meta); return; }
    PlayCandidate *c = calloc((size_t)nu, sizeof *c);
    for (int i = 0; i < nu; i++) { c[i].url = strdup(urls[i]); c[i].label = strdup("YouTube"); }
    LOGI("streams: YouTube video for %s streaming", g_yt.video_id);
    app_push(screen_player_create(&g_yt.meta, g_yt.video_id, g_yt.title, c, nu, 0, 0, NULL, NULL));
    meta_preview_free(&g_yt.meta);
}

static void play_yt(Streams *st, const Stream *s)
{
    if (g_yt.busy) { app_toast("Getting the video from YouTube\xE2\x80\xA6"); return; }
    g_yt.busy = 1;
    meta_preview_copy(&g_yt.meta, &st->meta);
    snprintf(g_yt.video_id, sizeof g_yt.video_id, "%s", st->video_id);
    snprintf(g_yt.title, sizeof g_yt.title, "%s%s%s", st->meta.name, st->video_title ? "  -  " : "", st->video_title ? st->video_title : "");
    char url[80]; snprintf(url, sizeof url, "https://www.youtube.com/watch?v=%s", s->yt_id);
    snprintf(g_yt.yt, sizeof g_yt.yt, "%.15s", s->yt_id);
    youtube_set_quality(g_app.res_cap, quality_cap_ok(&g_app.tv, TVCAP_AV1), quality_cap_ok(&g_app.tv, TVCAP_4K));
    app_toast("Getting the video from YouTube\xE2\x80\xA6");
    youtube_stream(url, on_yt_stream, NULL);
}

static void play_from(Streams *st, int index)
{
    if (app_pip_claim(st->video_id)) return;
    if (!st->sources_timed) { st->sources_timed = 1; perf_start_phase("sources"); }
    perf_start_phase("check");
    if (st->items[index].yt_id && !st->items[index].url) { play_yt(st, &st->items[index]); return; }
    char reason[160];
    char *url = app_stream_playable_url(&st->items[index], reason, sizeof reason);
    if (!url) { app_toast("%s", reason); return; }
    st->launched = 1;
    video_keep_awake(0);   /* the player's own loading holds the TV's screensaver from here */
    PlayCandidate *c = calloc((size_t)st->n + 2, sizeof *c);
    int n = 0, p2p = 0, rec = recommended(st);
    const char *group = st->items[index].binge_group;
    /* the one picked, the recommended one (the surest fallback, and the player marks it),
     * the rest of its binge group, then the others in the list's order */
    for (int pass = 0; pass < 4 && n < 6; pass++)
        for (int i = 0; i < st->n && n < 6; i++) {
            if (pass == 0 && i != index) continue;
            if (pass > 0 && (i == index || (pass > 1 && i == rec))) continue;
            if (pass == 1 && i != rec) continue;
            if (pass == 2 && !(group && st->items[i].binge_group && strcmp(group, st->items[i].binge_group) == 0)) continue;
            if (pass == 3 && group && st->items[i].binge_group && strcmp(group, st->items[i].binge_group) == 0) continue;
            if (pass > 0 && ineligible(st, &st->items[i])) continue;   /* behind the picked one: only what may play by itself */
            if (streams_candidate(&st->meta, &st->items[i], &c[n])) { p2p |= is_p2p(&st->items[i]); c[n].recommended = i == rec; n++; }
        }
    for (int i = 0; i < st->n && !p2p; i++)
        if (is_p2p(&st->items[i]) && !ineligible(st, &st->items[i]) && streams_candidate(&st->meta, &st->items[i], &c[n])) { p2p = 1; n++; }
    if (torrent_engine_url_is(c[0].url)) torrent_engine_start(0);   /* warm up while the player opens */
    LibraryItem *li = app_library_find(st->meta.id);
    double start = (li && li->video_id && strcmp(li->video_id, st->video_id) == 0) ? li->time_offset / 1000.0 : 0;
    remember_resume(st, url);
    free(url);
    char title[300];
    snprintf(title, sizeof title, "%s%s%s", st->meta.name, st->video_title ? "  -  " : "", st->video_title ? st->video_title : "");
    char next_id[128] = "", next_title[200] = "";
    int has_next = app_next_video(st->meta.id, st->video_id, next_id, sizeof next_id, next_title, sizeof next_title);
    /* no race for the next episode's same-release source: it plays as matched */
    const LinkCheck *just = st->items[index].url ? check_find(st->items[index].url, RACE_SKIP_S) : NULL;
    int fresh = just && (just->state == 2 || just->state == 3);   /* its check passed moments ago: it answers, no race to find out */
    if (fresh) LOGI("streams: source checked %.0f s ago; no race", g_app.time - just->at);
    play_raced(st, c, n, start, title, has_next, next_id, next_title, !fresh && index == recommended(st) && !same_release(st, &st->items[index]) && !is_mine(&st->items[index]) && !from_torbox(&st->items[index]));   /* TorBox's copy: no race either */
}

/* ---- a quiet lookup for the player ---- */

static struct {
    MetaPreview meta;
    Stream *items; int n, pending;
    unsigned generation;
    float started;
    StreamsLookupCb cb; void *user;
    char *video_id; int auto_only;   /* for the player asking by itself: only what may play by itself */
} g_look;

static void lookup_finish(void)
{
    StreamsLookupCb cb = g_look.cb;
    void *user = g_look.user;
    Streams tmp;   /* ranked exactly like the Sources screen */
    memset(&tmp, 0, sizeof tmp);
    tmp.meta = g_look.meta; tmp.items = g_look.items; tmp.n = g_look.n; tmp.video_id = g_look.video_id;
    sort_items(&tmp);
    int n = g_look.n;
    if (g_look.auto_only) {   /* the eligible ones first, in their order, and only they are handed over */
        int k = 0;
        for (int i = 0; i < g_look.n; i++)
            if (!ineligible(&tmp, &g_look.items[i])) { Stream t = g_look.items[k]; g_look.items[k] = g_look.items[i]; g_look.items[i] = t; k++; }
        if (k < g_look.n) LOGI("streams: %d of %d sources may play by themselves", k, g_look.n);
        n = k;
    }
    g_look.cb = NULL;
    if (cb) cb(g_look.items, n, user);
    streams_lookup_cancel();
}

static void on_lookup(HttpResponse *r, void *user)
{
    Pending *p = user;
    if (p->generation != g_look.generation || !g_look.cb) { free(p); return; }
    g_look.pending--;
    absorb(&g_look.items, &g_look.n, r, p->addon);
    free(p);
    if (!g_look.pending) lookup_finish();
}

void streams_lookup_cancel(void)
{
    g_look.generation++;
    streams_free(g_look.items, g_look.n);
    g_look.items = NULL; g_look.n = 0; g_look.pending = 0; g_look.cb = NULL;
    meta_preview_free(&g_look.meta);
    memset(&g_look.meta, 0, sizeof g_look.meta);
    free(g_look.video_id); g_look.video_id = NULL; g_look.auto_only = 0;
}

static void lookup_start(const MetaPreview *meta, const char *video_id, StreamsLookupCb cb, void *user, int auto_only);

void streams_lookup_auto(const MetaPreview *meta, const char *video_id, StreamsLookupCb cb, void *user) { lookup_start(meta, video_id, cb, user, 1); }
void streams_lookup(const MetaPreview *meta, const char *video_id, StreamsLookupCb cb, void *user) { lookup_start(meta, video_id, cb, user, 0); }

static void lookup_start(const MetaPreview *meta, const char *video_id, StreamsLookupCb cb, void *user, int auto_only)
{
    streams_lookup_cancel();
    g_look.auto_only = auto_only;   /* before anything can finish it */
    meta_preview_copy(&g_look.meta, meta);
    g_look.video_id = video_id ? strdup(video_id) : NULL;
    g_look.cb = cb; g_look.user = user;
    g_look.started = g_app.time;
    g_look.generation += 1000;
    query_addons(meta, video_id, NULL, g_look.generation, on_lookup, &g_look.pending, NULL);
    if (!g_look.pending) lookup_finish();
}

/* Called from the player's update: an addon that never answers does not hold the rest. */
void streams_lookup_tick(void)
{
    if (g_look.cb && g_app.time - g_look.started > 15) lookup_finish();
}

static int key(Screen *s, KeyAction a)
{
    Streams *st = s->st;
    if (a == KEY_UP || a == KEY_DOWN || a == KEY_OK) race_cancel();
    switch (a) {
    case KEY_UP:
        st->moved = 1; st->auto_rec_done = 1; st->autoplayed = 1;   /* choosing: nothing starts by itself any more */
        if (st->focus > 0) st->focus--;
        else if (st->focus == 0 && recommended(st) >= 0) st->focus = -1;
        app_redraw();
        return 1;
    case KEY_DOWN: st->moved = 1; st->auto_rec_done = 1; st->autoplayed = 1; if (st->focus < st->n - 1) { st->focus++; app_redraw(); return 1; } return 0;
    case KEY_OK:   /* a choice: timed from here, the sources are in */
        perf_start_begin("chosen"); st->sources_timed = 1;
        if (st->focus == -1) { int r = recommended(st); if (r >= 0) play_from(st, r); return 1; }
        if (st->focus < st->n) play_from(st, st->focus);
        return 1;
    default: return 0;
    }
}

static void resume(Screen *s)
{
    Streams *st = s->st;
    /* Back from the player it opened: don't start it again. Back on top for another reason
     * (the screensaver closing, a dialog) changes nothing: a start under way goes on. */
    if (st->launched) { st->autoplayed = 1; st->auto_rec_done = 1; }
    app_redraw();
}


static void destroy(Screen *s)
{
    Streams *st = s->st;
    if (g_live == st) { g_live = NULL; g_generation++; }
    race_cancel();   /* left with Back while it ran: only the screen on top races */
    video_keep_awake(0);
    perf_start_cancel();   /* left before anything played (after a start, nothing is running) */
    st->probe_gen++;
    if (st->from_prefetch && st->pending) prefetch_clear();   /* nobody left to receive the rest */
    streams_free(st->items, st->n);
    meta_preview_free(&st->meta);
    free(st->video_id); free(st->video_title);
    free(st);
    free(s);
}

Screen *screen_streams_create_chain(const MetaPreview *meta, const char *video_id, const char *video_title, const ChainHint *hint)
{
    Screen *s = calloc(1, sizeof *s);
    Streams *st = calloc(1, sizeof *st);
    meta_preview_copy(&st->meta, meta);
    st->video_id = strdup(video_id);
    st->video_title = video_title ? strdup(video_title) : NULL;
    st->chaining = hint != NULL;
    if (hint) perf_start_begin("next episode");
    else if (g_app.auto_recommended) perf_start_begin("recommended");
    if (hint) {
        st->chain = *hint;
        LOGI("streams: next episode like %s%s%s%s, addon %d, %dp", hint->group[0] ? "group " : "", hint->group,
             hint->binge_group[0] ? " binge " : "", hint->binge_group, hint->addon_order, hint->res);
    } else st->autoplayed = 1;
    st->started = g_app.time;
    s->name = "streams"; s->st = st;
    s->update = update; s->draw = draw; s->key = key; s->destroy = destroy; s->resume = resume;
    g_live = st;
    g_generation++;
    if (!adopt_prefetch(st)) query(st);
    return s;
}

Screen *screen_streams_create_auto(const MetaPreview *meta, const char *video_id, const char *video_title, const char *autoplay_group)
{
    if (!autoplay_group) return screen_streams_create_chain(meta, video_id, video_title, NULL);
    ChainHint h = { .addon_order = -1 };
    snprintf(h.binge_group, sizeof h.binge_group, "%s", autoplay_group);
    return screen_streams_create_chain(meta, video_id, video_title, &h);
}

Screen *screen_streams_create(const MetaPreview *meta, const char *video_id, const char *video_title)
{
    return screen_streams_create_chain(meta, video_id, video_title, NULL);
}

Screen *screen_streams_create_play(const MetaPreview *meta, const char *video_id, const char *video_title)
{
    Screen *s = screen_streams_create_chain(meta, video_id, video_title, NULL);
    Streams *st = s->st;
    st->play_now = 1;
    perf_start_begin("Play");
    if (!st->pending && st->n) { st->sources_timed = 1; perf_start_phase("sources"); }   /* all in from the prefetch */
    return s;
}
