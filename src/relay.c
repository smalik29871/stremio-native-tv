/* See relay.h. One listening thread on 127.0.0.1; each request the player makes (the
 * first one, and one per seek) gets its own thread, which asks the source for the same
 * bytes with the addon's headers and hands them on until either side stops. Nothing
 * here touches the screen or the player: when a request can't be served it answers
 * 502, and the player's usual fallbacks take it from there.
 *
 * The buffer ahead (relay_url_buffered): for the source that is playing, a download up to
 * two minutes ahead of the TV's player, in a ring in memory sized from the RAM free (a TV
 * may have under 1 GB of storage in all, so none of it is used). The player's reads come
 * out of it; a download that drops picks up again from the byte it stopped at. When the
 * buffer can't go on (memory, a source that keeps failing), the connection carries on
 * straight from the source: the picture never depends on it. */
#include "relay.h"
#include "mkvsubs.h"

/* What this home's connection carries, learnt from the sources (app.c); absent when the library stands alone. */
void app_home_mbps_note(double mbps) __attribute__((weak));
#include "http.h"
#include "log.h"
#include "memwatch.h"
#include "store.h"
#include "platform/compat.h"
#include <SDL.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>



enum { MAX_ROUTES = 8, MAX_CONNS = 12 };   /* seeks open new requests while old ones close */
typedef struct { char token[33]; char *url, *headers; char name[64]; } Route;

static Route g_routes[MAX_ROUTES];
static int g_next_route;
static SDL_mutex *g_lock;
static int g_sock = -1, g_port, g_failed;
static SDL_atomic_t g_conns;

/* ---- the buffer ahead ---- */
/* Bytes in blocks of memory taken as the download fills them and given back once the
 * player is past them, so it holds only what is ahead (and a little behind for a step
 * back). How far ahead it may go is a budget from the TV's free memory, looked at again
 * every few seconds: more room, further ahead (up to two minutes); less, it holds less;
 * too little, it lets go and the source plays on directly. */
#ifdef SNTV_DESKTOP_PLAYER   /* a computer has memory to spare: up to 2 GB, 20 minutes ahead, more kept behind for a step back */
enum { AHEAD_S = 20 * 60, BACKLOG = 64 << 20, NBLK = 512 };
#define BUDGET_MAX ((uint64_t)2048 << 20)
#else
enum { AHEAD_S = 120, BACKLOG = 8 << 20, NBLK = 64 };
#define BUDGET_MAX ((uint64_t)256 << 20)   /* never more than this much memory */
#endif
enum { VLEN = 64 << 10, JUMP = 2 << 20, TAIL = 32 << 20, CHUNK = 256 << 10, READERS = 16,
       HEAD = 8 << 20, GAP = 48 << 20 };   /* JUMP: further past what is in, it starts again there; HEAD: the file's start, read again on a seek;
                                              GAP: a step back this far behind what is in comes from the source, the rest still from the buffer */
#define BUDGET_SMALL ((uint64_t)64 << 20)  /* on a 1 GB TV */
#define BUDGET_MIN ((uint64_t)48 << 20)    /* the budget can't reach this: no buffer (or it lets go) */
#define MIN_AHEAD ((uint64_t)16 << 20)   /* at least this much ahead, whatever the rate */
#define MEM_STOP ((uint64_t)120 << 20)     /* the TV's free memory fell under this: the buffer lets go at once */
typedef struct { int used; uint64_t at; Uint32 seen; } Reader;   /* a connection reading out of it */
typedef struct {
    char token[33];          /* "" none */
    char *url, *headers;
    char *blk[NBLK];         /* block n of the file sits in slot n % NBLK while tag says n */
    uint64_t tag[NBLK];
    uint64_t blk_size, held; /* bytes in blocks taken */
    uint64_t budget, max_budget;   /* what it may hold now, and ever */
    uint64_t size;           /* the file's, 0 until the source said */
    char type[96];
    uint64_t lo, hi;         /* bytes [lo, hi) of the file are in */
    uint64_t pos;            /* where the reader that last moved is */
    double rate;             /* bytes a second of video (size / duration, or the estimate) */
    unsigned route, gen;     /* route: a new source; gen: a jump outside what is in */
    Reader rd[READERS];
    int stopped, fails, running, got_any, dead;   /* got_any: the source sent some of the file; dead: it refused or kept failing before that */
    int failing, gave_up;    /* failing: it stopped sending partway (what is in plays on); gave_up: it refused the rest */
    struct { char *url, *headers; int tried; } alt[6];   /* other links to perhaps the same file, for a handoff */
    int n_alt;
    Uint32 mem_at;           /* when free memory was last looked at */
    Uint32 asked_at;         /* when the player first asked for it (0 not yet) */
    int slow;                /* the source sends slower than the video plays, with the buffer going down */
    double in_rate;          /* bytes a second coming in lately (while there was room) */
    Uint32 slow_since, win_at, hungry_at; uint64_t win_bytes, hungry_bytes;   /* measuring: a window of 5 s; a hungry stretch (never held) for the home speed */
    int handoff_for_slow;    /* a slow source was handed off once already */
    SDL_cond *cond;
} Buf;
static Buf g_buf;

static uint64_t free_memory(void)
{
    const char *fake = getenv("SNTV_FAKE_FREE_MB");   /* tests: a TV with this little memory free */
    if (fake && *fake) return (uint64_t)strtoull(fake, NULL, 10) << 20;
    MemSample m;
    return mem_sample(&m) && m.avail_kb > 0 ? (uint64_t)m.avail_kb << 10 : 0;
}

static uint64_t budget_for(uint64_t free_mem, uint64_t held, uint64_t max)   /* a fifth of the memory the TV has (counting the buffer's own) */
{
    uint64_t b = (free_mem + held) / 5;
    return b < max ? b : max;
}

static uint64_t ahead_now(void)   /* bytes in past the reader (none while it waits beyond them) */
{
    return g_buf.hi > g_buf.pos ? g_buf.hi - g_buf.pos : 0;
}

static uint64_t backlog(void)   /* kept behind the reader for a small step back: a quarter of the budget at most */
{
    return g_buf.budget / 4 < BACKLOG ? g_buf.budget / 4 : BACKLOG;
}

static uint64_t ahead_limit(void)   /* bytes to keep ahead of the reader now */
{
    uint64_t want = g_buf.rate > 0 ? (uint64_t)(g_buf.rate * AHEAD_S) : g_buf.budget;
    if (want < MIN_AHEAD) want = MIN_AHEAD;   /* a low guess at the rate never starves the player's first reads */
    uint64_t room = g_buf.budget > backlog() + g_buf.blk_size ? g_buf.budget - backlog() - g_buf.blk_size : 0;   /* a block of slack: a write can start a new one */
    return want < room ? want : room;
}

static void free_blocks_locked(void)
{
    for (int i = 0; i < NBLK; i++) if (g_buf.blk[i]) { free(g_buf.blk[i]); g_buf.blk[i] = NULL; }
    g_buf.held = 0;
}

static void let_go_locked(const char *why)   /* the buffer stops; connections go on from the source */
{
    if (g_buf.stopped) return;
    g_buf.stopped = 1;
    free_blocks_locked();
    SDL_CondBroadcast(g_buf.cond);
    LOGW("relay: %s; the buffer lets go and the source plays on directly", why);
}

/* The reader that moved last is the one playing: a probe or a step back that ends hands
 * it back to the one still streaming. */
static void pos_update_locked(void)
{
    int best = -1;
    for (int i = 0; i < READERS; i++)
        if (g_buf.rd[i].used && (best < 0 || (Sint32)(g_buf.rd[i].seen - g_buf.rd[best].seen) > 0)) best = i;
    if (best >= 0) g_buf.pos = g_buf.rd[best].at;
}

/* Blocks wholly behind the reader's step-back room go back to the TV. */
static void trim_locked(void)
{
    uint64_t floor = g_buf.pos > backlog() ? g_buf.pos - backlog() : 0, bs = g_buf.blk_size;
    for (int i = 0; i < NBLK; i++)
        if (g_buf.blk[i] && (g_buf.tag[i] + 1) * bs <= floor) { free(g_buf.blk[i]); g_buf.blk[i] = NULL; g_buf.held -= bs; }
    uint64_t kept = floor / bs * bs;
    if (kept > g_buf.lo) g_buf.lo = kept < g_buf.hi ? kept : g_buf.hi;
}

static int put_locked(uint64_t at, const char *data, size_t n)   /* 0: no memory for a block */
{
    uint64_t bs = g_buf.blk_size;
    for (size_t done = 0; done < n; ) {
        uint64_t off = at + done, bn = off / bs;
        int slot = (int)(bn % NBLK);
        if (!g_buf.blk[slot]) {
            if (!(g_buf.blk[slot] = malloc((size_t)bs))) return 0;
            g_buf.held += bs;
        } else if (g_buf.tag[slot] != bn && (g_buf.tag[slot] + 1) * bs > g_buf.lo) g_buf.lo = (g_buf.tag[slot] + 1) * bs;   /* an old block reused */
        g_buf.tag[slot] = bn;
        size_t k = (size_t)(bs - off % bs);
        if (k > n - done) k = n - done;
        memcpy(g_buf.blk[slot] + off % bs, data + done, k);
        done += k;
    }
    return 1;
}

static int get_locked(uint64_t at, char *out, size_t n)   /* 0: not in after all */
{
    uint64_t bs = g_buf.blk_size;
    for (size_t done = 0; done < n; ) {
        uint64_t off = at + done, bn = off / bs;
        int slot = (int)(bn % NBLK);
        if (!g_buf.blk[slot] || g_buf.tag[slot] != bn) return 0;
        size_t k = (size_t)(bs - off % bs);
        if (k > n - done) k = n - done;
        memcpy(out + done, g_buf.blk[slot] + off % bs, k);
        done += k;
    }
    return 1;
}

/* Every few seconds: the budget from what the TV has free now. */
static void budget_tick(void)
{
    SDL_LockMutex(g_lock);
    int due = g_buf.token[0] && !g_buf.stopped && SDL_GetTicks() - g_buf.mem_at > 3000;
    uint64_t held = g_buf.held, max = g_buf.max_budget, was = g_buf.budget;
    if (due) g_buf.mem_at = SDL_GetTicks();
    SDL_UnlockMutex(g_lock);
    if (!due) return;
    uint64_t mem = free_memory(), b = budget_for(mem, held, max);
    const char *later = getenv("SNTV_FAKE_FREE_MB_LATER");   /* tests: the TV's memory runs short while it plays */
    if (later && *later) { mem = (uint64_t)strtoull(later, NULL, 10) << 20; b = budget_for(mem, held, max); }
    const char *tiny = getenv("SNTV_FAKE_BUFFER_KB");
    if (tiny && *tiny) return;   /* tests: the size they set */
    SDL_LockMutex(g_lock);
    if (g_buf.token[0] && !g_buf.stopped) {
        if (mem < MEM_STOP || b < BUDGET_MIN) let_go_locked("the TV is short of memory");
        else {
            g_buf.budget = b;
            if (b + b / 4 < was || was + was / 4 < b) LOGI("relay: the buffer may hold %.0f MB now (%.0f MB free on the TV)", b / 1048576.0, mem / 1048576.0);
            trim_locked();
            SDL_CondBroadcast(g_buf.cond);
        }
    }
    SDL_UnlockMutex(g_lock);
}

int relay_enabled(void)
{
    return !getenv("SNTV_NO_RELAY") && store_get_int("relay", 1);
}

int relay_buffer_enabled(void)
{
    const char *off = getenv("SNTV_NO_BUFFER");   /* tests: set (non-empty) leaves it out */
    return relay_enabled() && !(off && *off) && store_get_int("bufferAhead", 1);
}

static void host_of(const char *url, char *out, size_t n)   /* the host only: a link can carry a key */
{
    const char *hs = strstr(url, "://"); hs = hs ? hs + 3 : url;
    snprintf(out, n, "%.*s", (int)strcspn(hs, "/:?"), hs);
}

static int send_all(int fd, const char *p, size_t n)
{
    while (n) {
        ssize_t k = send(fd, p, n, MSG_NOSIGNAL);
        if (k < 0 && errno == EINTR) continue;
        if (k <= 0) return 0;
        p += k; n -= (size_t)k;
    }
    return 1;
}

typedef struct { int fd, head_only, sent_head; long status; uint64_t sent; } Conn;

static int on_head(void *user, long status, const char *type, uint64_t length, const char *content_range)
{
    Conn *c = user;
    char h[512];
    int n = snprintf(h, sizeof h, "HTTP/1.1 %ld %s\r\nContent-Type: %s\r\nAccept-Ranges: bytes\r\nConnection: close\r\n",
                     status, status == 206 ? "Partial Content" : status == 200 ? "OK" : "Upstream", type && type[0] ? type : "application/octet-stream");
    if (length != (uint64_t)-1) n += snprintf(h + n, sizeof h - (size_t)n, "Content-Length: %llu\r\n", (unsigned long long)length);
    if (content_range && content_range[0]) n += snprintf(h + n, sizeof h - (size_t)n, "Content-Range: %s\r\n", content_range);
    n += snprintf(h + n, sizeof h - (size_t)n, "\r\n");
    c->sent_head = 1;
    c->status = status;
    return send_all(c->fd, h, (size_t)n) && !c->head_only;
}

static int on_data(void *user, const char *data, size_t n)
{
    Conn *c = user;
    if (!send_all(c->fd, data, n)) return 0;
    c->sent += n;
    return 1;
}

static void answer(int fd, const char *status)
{
    char h[160];
    int n = snprintf(h, sizeof h, "HTTP/1.1 %s\r\nContent-Length: 0\r\nConnection: close\r\n\r\n", status);
    send_all(fd, h, (size_t)n);
}

/* ---- the download ahead ---- */
typedef struct { unsigned route, gen; uint64_t from, got; long status; int bad, held; } Dl;   /* held: stopped by us (room, a jump) */

/* How fast the source sends while the buffer wants more: a 5 s window for the rate the
 * player's fallback logic looks at, and a longer hungry stretch (never held back) that
 * says what this home's connection carries, for choosing sources that fit it. */
static void rate_note_locked(size_t took)
{
    Uint32 now = SDL_GetTicks();
    if (!g_buf.win_at) { g_buf.win_at = now; g_buf.win_bytes = 0; }
    g_buf.win_bytes += took;
    if (now - g_buf.win_at >= 5000) {
        double r = g_buf.win_bytes * 1000.0 / (now - g_buf.win_at);
        g_buf.in_rate = g_buf.in_rate > 0 ? g_buf.in_rate * 0.5 + r * 0.5 : r;
        g_buf.win_at = now; g_buf.win_bytes = 0;
        int hungry = ahead_now() < ahead_limit() / 2;
        double ahead_s = g_buf.rate > 0 ? ahead_now() / g_buf.rate : 999;
        if (g_buf.rate > 0 && hungry && g_buf.in_rate < g_buf.rate * 0.9 && ahead_s < 30) {
            if (!g_buf.slow_since) g_buf.slow_since = now;
            else if (now - g_buf.slow_since > 15000 && !g_buf.slow) { g_buf.slow = 1; LOGW("relay: the source sends %.1f Mbit/s, the video needs %.1f; %.0f s left in the buffer", g_buf.in_rate * 8 / 1e6, g_buf.rate * 8 / 1e6, ahead_s); SDL_CondBroadcast(g_buf.cond); }
        } else if (g_buf.in_rate >= g_buf.rate) { if (g_buf.slow) LOGI("relay: the source keeps up again (%.1f Mbit/s)", g_buf.in_rate * 8 / 1e6); g_buf.slow = 0; g_buf.slow_since = 0; }
    }
    if (!g_buf.hungry_at) { g_buf.hungry_at = now; g_buf.hungry_bytes = 0; }
    g_buf.hungry_bytes += took;
    static uint64_t sample_bytes;
    if (!sample_bytes) { const char *e = getenv("SNTV_SPEED_SAMPLE_KB"); sample_bytes = e && *e ? (uint64_t)strtoull(e, NULL, 10) << 10 : 8u << 20; }   /* tests: small files */
    if (g_buf.hungry_bytes >= sample_bytes && now - g_buf.hungry_at >= (sample_bytes < (1u << 20) ? 0u : 2000u)) {   /* 8 MB that nothing held back: what the line carries */
        Uint32 ms = now - g_buf.hungry_at > 0 ? now - g_buf.hungry_at : 1;
        if (app_home_mbps_note) app_home_mbps_note(g_buf.hungry_bytes * 8.0 / 1e6 / (ms / 1000.0));   /* the app's, absent in the unit tests */
        g_buf.hungry_at = now; g_buf.hungry_bytes = 0;
    }
}

static int dl_head(void *user, long status, const char *type, uint64_t length, const char *content_range)
{
    Dl *d = user;
    d->status = status;
    uint64_t total = 0;
    const char *slash = content_range ? strchr(content_range, '/') : NULL;
    if (status == 206 && slash && slash[1] != '*') total = strtoull(slash + 1, NULL, 10);
    else if (status == 200 && d->from == 0 && length != (uint64_t)-1) total = length;
    if (status == 200 && d->from > 0) { d->bad = 1; return 0; }   /* the whole file again: it doesn't serve byte ranges */
    if (status >= 400 && status < 500 && status != 408 && status != 429) { d->bad = 1; return 0; }   /* gone, refused: not coming back */
    if (!total || (status == 206 && content_range && strtoull(content_range + strcspn(content_range, "0123456789"), NULL, 10) != d->from)) return 0;   /* a passing error: again later */
    SDL_LockMutex(g_lock);
    int ours = d->route == g_buf.route && d->gen == g_buf.gen;
    if (ours && !g_buf.size) {
        g_buf.size = total;
        snprintf(g_buf.type, sizeof g_buf.type, "%s", type && type[0] ? type : "application/octet-stream");
        SDL_CondBroadcast(g_buf.cond);
    }
    SDL_UnlockMutex(g_lock);
    return ours;
}

static int dl_data(void *user, const char *data, size_t n)
{
    Dl *d = user;
    SDL_LockMutex(g_lock);
    int ours = d->route == g_buf.route && d->gen == g_buf.gen && !g_buf.stopped;
    uint64_t at = g_buf.hi, floor = g_buf.pos > backlog() ? g_buf.pos - backlog() : 0;
    if (floor < g_buf.lo) floor = g_buf.lo;
    uint64_t used = at > floor ? at - floor : 0;   /* what nobody needs any more (behind the step-back room) is free */
    uint64_t room = used < g_buf.budget ? g_buf.budget - used : 0;
    if (ours && ahead_now() >= ahead_limit()) {   /* far enough ahead: the connection waits for room rather than being dropped and made again */
        static int hold_ms = -1;
        if (hold_ms < 0) { const char *e = getenv("SNTV_RELAY_HOLD_MS"); hold_ms = e && *e ? atoi(e) : 20000; }
        g_buf.hungry_at = 0;   /* no longer measuring the home speed: the source is being held back */
        for (int waited = 0; ours && ahead_now() >= ahead_limit() && waited < hold_ms; waited += 250) {
            SDL_CondWaitTimeout(g_buf.cond, g_lock, 250);
            SDL_UnlockMutex(g_lock); budget_tick(); SDL_LockMutex(g_lock);
            ours = d->route == g_buf.route && d->gen == g_buf.gen && !g_buf.stopped;
            at = g_buf.hi; floor = g_buf.pos > backlog() ? g_buf.pos - backlog() : 0; if (floor < g_buf.lo) floor = g_buf.lo;
            used = at > floor ? at - floor : 0; room = used < g_buf.budget ? g_buf.budget - used : 0;
        }
    }
    size_t take = !ours || ahead_now() >= ahead_limit() ? 0 : room < n ? (size_t)room : n;   /* as much as fits */
    if (!take) { SDL_UnlockMutex(g_lock); d->held = 1; return 0; }   /* a jump, a stop, or far enough ahead: picked up again from here later */
    rate_note_locked(take);
    if (!put_locked(at, data, take)) { let_go_locked("no memory for its next block"); SDL_UnlockMutex(g_lock); d->held = 1; return 0; }
    g_buf.hi = at + take;
    g_buf.got_any = 1;
    trim_locked();
    SDL_CondBroadcast(g_buf.cond);
    SDL_UnlockMutex(g_lock);
    mkvsubs_feed(at, (const uint8_t *)data, take);   /* the film's own subtitle lines, as they pass */
    d->got += take;
    if (take < n) { d->held = 1; return 0; }   /* full for now: the rest later */
    return 1;
}

/* ---- carrying on from an identical copy ---- */
/* The source stopped partway: another link to the same file (the addon's own link again,
 * where it led last time having expired; or the same release from another service) takes
 * over at the very byte, and the player never knows. "The same file" is proved, not
 * guessed: the same size, and the last bytes in the buffer the same bytes there. */
typedef struct { const char *mine; size_t len, at; uint64_t size, from; int ok; } Same;

static int same_head(void *user, long status, const char *type, uint64_t length, const char *content_range)
{
    Same *c = user;
    (void)type; (void)length;
    const char *slash = content_range ? strchr(content_range, '/') : NULL;
    uint64_t start = content_range ? strtoull(content_range + strcspn(content_range, "0123456789"), NULL, 10) : 0;
    return status == 206 && slash && strtoull(slash + 1, NULL, 10) == c->size && start == c->from;
}

static int same_data(void *user, const char *data, size_t n)
{
    Same *c = user;
    size_t k = n < c->len - c->at ? n : c->len - c->at;
    if (memcmp(data, c->mine + c->at, k)) return 0;   /* another file */
    c->at += k;
    if (c->at < c->len) return 1;
    c->ok = 1;
    return 0;   /* all of it the same: enough */
}

static int handoff(unsigned route, char *host, size_t host_len)
{
    SDL_LockMutex(g_lock);
    uint64_t hi = g_buf.hi, from = hi > VLEN ? hi - VLEN : 0;
    if (route != g_buf.route || g_buf.stopped || !g_buf.size || from < g_buf.lo || hi - from < 4096) { SDL_UnlockMutex(g_lock); return 0; }   /* too little in hand to compare */
    Same c = { NULL, (size_t)(hi - from), 0, g_buf.size, from, 0 };
    char *mine = malloc(c.len);
    if (!mine || !get_locked(from, mine, c.len)) { SDL_UnlockMutex(g_lock); free(mine); return 0; }
    c.mine = mine;
    SDL_UnlockMutex(g_lock);
    char range[64];
    snprintf(range, sizeof range, "%llu-%llu", (unsigned long long)from, (unsigned long long)(hi - 1));
    for (int i = 0;; i++) {
        SDL_LockMutex(g_lock);
        while (i < g_buf.n_alt && (g_buf.alt[i].tried || !strcmp(g_buf.alt[i].url, g_buf.url))) i++;
        if (route != g_buf.route || g_buf.stopped || i >= g_buf.n_alt) { SDL_UnlockMutex(g_lock); break; }
        char *url = strdup(g_buf.alt[i].url), *headers = g_buf.alt[i].headers ? strdup(g_buf.alt[i].headers) : NULL;
        g_buf.alt[i].tried = 1;
        SDL_UnlockMutex(g_lock);
        c.at = 0; c.ok = 0;
        HttpStream cb = { same_head, same_data, &c };
        http_stream(url, headers, range, &cb);
        char other[96];
        host_of(url, other, sizeof other);
        SDL_LockMutex(g_lock);
        if (c.ok && route == g_buf.route && !g_buf.stopped) {
            LOGI("relay: %s stopped sending at %.1f MB; carrying on from the same file at %s", host, hi / 1048576.0, other);
            free(g_buf.url); free(g_buf.headers);
            g_buf.url = url; g_buf.headers = headers;
            g_buf.fails = 0; g_buf.failing = g_buf.gave_up = 0;
            SDL_UnlockMutex(g_lock);
            snprintf(host, host_len, "%s", other);
            free(mine);
            return 1;
        }
        SDL_UnlockMutex(g_lock);
        LOGI("relay: %s is not the same file; not carrying on from it", other);
        free(url); free(headers);
    }
    free(mine);
    return 0;
}

static int dl_thread(void *arg)
{
    unsigned route = (unsigned)(uintptr_t)arg;
    char host[96] = "";
    for (int logged = 0;;) {
        budget_tick();
        SDL_LockMutex(g_lock);
        for (;;) {   /* wait while far enough ahead (or the whole file is in) */
            if (route != g_buf.route || g_buf.stopped) break;
            int done = g_buf.size && g_buf.hi >= g_buf.size;
            if (!done && ahead_now() < ahead_limit() * 8 / 10) break;   /* room again: some way under the limit, so not a reconnect per chunk */
            SDL_CondWaitTimeout(g_buf.cond, g_lock, 1000);
            SDL_UnlockMutex(g_lock);
            budget_tick();   /* the TV's memory, even while it waits */
            SDL_LockMutex(g_lock);
        }
        if (route != g_buf.route || g_buf.stopped) { if (route == g_buf.route) g_buf.running = 0; SDL_UnlockMutex(g_lock); break; }
        Dl d = { route, g_buf.gen, g_buf.hi, 0, 0, 0, 0 };
        g_buf.hungry_at = 0; g_buf.win_at = 0;
        char *url = strdup(g_buf.url), *headers = g_buf.headers ? strdup(g_buf.headers) : NULL;
        SDL_UnlockMutex(g_lock);
        if (!host[0]) host_of(url, host, sizeof host);
        SDL_LockMutex(g_lock);
        int slow_now = g_buf.slow && !g_buf.handoff_for_slow && g_buf.n_alt;
        if (slow_now) g_buf.handoff_for_slow = 1;
        SDL_UnlockMutex(g_lock);
        if (slow_now) {   /* too slow: the same file from another link, from this very byte */
            LOGI("relay: %s is too slow; trying an identical copy", host);
            if (handoff(route, host, sizeof host)) { SDL_LockMutex(g_lock); g_buf.slow = 0; g_buf.slow_since = 0; g_buf.in_rate = 0; SDL_UnlockMutex(g_lock); free(url); free(headers); continue; }
        }
        char range[32];
        snprintf(range, sizeof range, "%llu-", (unsigned long long)d.from);
        HttpStream cb = { dl_head, dl_data, &d };
        http_stream(url, headers, range, &cb);
        free(url); free(headers);
        SDL_LockMutex(g_lock);
        if (route == g_buf.route && d.gen == g_buf.gen && !g_buf.stopped) {
            if (d.got) {
                g_buf.fails = 0;
                if (!logged++) LOGI("relay: buffering %s ahead (up to %.0f MB now)", host, g_buf.budget / 1048576.0);
                if (!d.held && g_buf.size && g_buf.hi < g_buf.size) LOGI("relay: %s dropped at %.1f MB; going on from there", host, g_buf.hi / 1048576.0);
                if (g_buf.failing) LOGI("relay: %s is sending again", host);
                g_buf.failing = 0;
            } else if (d.bad || (!d.held && !(g_buf.size && g_buf.hi >= g_buf.size))) {   /* refused, or nothing came */
                if (!d.bad) g_buf.fails++;
                if (g_buf.got_any && (d.bad || g_buf.fails >= 2)) {   /* partway through: the same file elsewhere? */
                    SDL_UnlockMutex(g_lock);
                    int moved = handoff(route, host, sizeof host);
                    SDL_LockMutex(g_lock);
                    if (moved) { SDL_UnlockMutex(g_lock); continue; }
                }
                if (route != g_buf.route || g_buf.stopped) {
                } else if (!g_buf.got_any && (d.bad || g_buf.fails >= 6)) {   /* never sent any of it: the source is down */
                    g_buf.dead = 1;
                    char why[160];
                    snprintf(why, sizeof why, "%s %s", host, !d.bad ? "keeps failing" : d.status == 200 ? "does not serve byte ranges" : "refused the file");
                    let_go_locked(why);
                } else if (d.bad) {   /* the rest refused: what is in plays on, and the player moves on before it runs out */
                    if (!g_buf.failing) LOGW("relay: %s refused the rest of the file at %.1f MB; what is in plays on", host, g_buf.hi / 1048576.0);
                    g_buf.failing = g_buf.gave_up = 1;
                    g_buf.running = 0;
                    SDL_CondBroadcast(g_buf.cond);
                    SDL_UnlockMutex(g_lock);
                    break;
                } else if (g_buf.got_any && g_buf.fails >= 3 && !g_buf.failing) {   /* stopped partway: it keeps trying, what is in plays on */
                    g_buf.failing = 1;
                    LOGW("relay: %s stopped sending at %.1f MB; what is in plays on while it tries again", host, g_buf.hi / 1048576.0);
                }
            }
        }
        int fails = g_buf.fails;
        SDL_UnlockMutex(g_lock);
        if (!d.got && fails) SDL_Delay(fails < 10 ? 500u * (unsigned)fails : 5000u);   /* a dropped source: again, a little later each time */
    }
    return 0;
}

static void dl_start_locked(void)
{
    if (g_buf.running || g_buf.stopped || g_buf.gave_up || !g_buf.token[0]) return;
    g_buf.running = 1;
    SDL_Thread *t = SDL_CreateThread(dl_thread, "relay-ahead", (void *)(uintptr_t)g_buf.route);
    if (t) SDL_DetachThread(t);
    else { g_buf.running = 0; let_go_locked("no thread for the download"); }
}

static int rest_head(void *user, long status, const char *type, uint64_t length, const char *content_range)
{
    (void)user; (void)type; (void)length; (void)content_range;
    return status == 206;   /* the head went out already: only those bytes, and only if they are those */
}

static void passthrough_rest(int fd, const char *url, const char *headers, uint64_t from, uint64_t to)
{
    char range[64];
    snprintf(range, sizeof range, "%llu-%llu", (unsigned long long)from, (unsigned long long)to);
    Conn c = { .fd = fd, .sent_head = 1 };
    HttpStream cb = { rest_head, on_data, &c };
    http_stream(url, headers, range, &cb);
}

static uint64_t head_bytes(void)   /* HEAD; tests make it small to suit their small files */
{
    const char *e = getenv("SNTV_FAKE_HEAD_KB");
    return e && atoi(e) > 0 ? (uint64_t)atoi(e) << 10 : (uint64_t)HEAD;
}

/* The player's request, from the buffer when its route is the buffered one. 0: not
 * handled here (the plain relay answers it). */
static int buffered_serve(int fd, const char *token, const char *range, int ranged, int head_only)
{
    SDL_LockMutex(g_lock);
    if (!g_buf.token[0] || strcmp(token, g_buf.token) || g_buf.stopped) { SDL_UnlockMutex(g_lock); return 0; }
    if (!g_buf.asked_at) g_buf.asked_at = SDL_GetTicks();
    dl_start_locked();
    for (int i = 0; i < 150 && !g_buf.size && !g_buf.stopped && !strcmp(token, g_buf.token); i++) SDL_CondWaitTimeout(g_buf.cond, g_lock, 100);   /* the source's size, from the download's first answer */
    if (!g_buf.size || g_buf.stopped || strcmp(token, g_buf.token)) { SDL_UnlockMutex(g_lock); return 0; }
    uint64_t size = g_buf.size, from = 0, to = size - 1;
    if (range[0] == '-') {   /* the last N bytes */
        uint64_t last = strtoull(range + 1, NULL, 10);
        from = last < size ? size - last : 0;
    } else if (range[0]) {
        from = strtoull(range, NULL, 10);
        const char *dash = strchr(range, '-');
        if (dash && dash[1]) { uint64_t e = strtoull(dash + 1, NULL, 10); if (e < to) to = e; }
    }
    if (from >= size || to < from) { SDL_UnlockMutex(g_lock); answer(fd, "416 Range Not Satisfiable"); return 1; }
    /* the index near the end that players read first goes straight to the source: the buffer stays where the film is */
    if (size > 4 * (uint64_t)TAIL && from >= size - TAIL) { SDL_UnlockMutex(g_lock); return 0; }
    /* so does the file's start, which some players read again at every seek: what is buffered ahead is not thrown away for it */
    uint64_t head = head_bytes();
    if (from < head && from < g_buf.lo && g_buf.pos >= 4 * head) { SDL_UnlockMutex(g_lock); LOGI("relay: the file's start asked again; from the source, the buffer stays"); return 0; }
    char type[96], *url = strdup(g_buf.url), *headers = g_buf.headers ? strdup(g_buf.headers) : NULL;
    snprintf(type, sizeof type, "%s", g_buf.type);
    unsigned route = g_buf.route;
    int me = -1;
    if (!head_only) {   /* a HEAD reads nothing: it doesn't move the buffer */
        for (int i = 0; i < READERS && me < 0; i++) if (!g_buf.rd[i].used) me = i;
        if (me < 0) { SDL_UnlockMutex(g_lock); free(url); free(headers); return 0; }
        int near = from < g_buf.lo && g_buf.lo - from <= GAP && g_buf.hi > g_buf.lo;   /* just behind it (a seek lands on an earlier keyframe) */
        if (!(from >= g_buf.lo && from <= g_buf.hi + JUMP) && !near) {   /* a jump outside it: the buffer starts again there */
            LOGI("relay: a jump to %.1f MB, outside what is in (%.1f-%.1f MB); the buffer starts again there", from / 1048576.0, g_buf.lo / 1048576.0, g_buf.hi / 1048576.0);
            g_buf.gen++;
            g_buf.lo = g_buf.hi = from;
            g_buf.fails = 0;
        }
        g_buf.rd[me] = (Reader){ 1, from, SDL_GetTicks() };
        pos_update_locked();
        SDL_CondBroadcast(g_buf.cond);
    }
    SDL_UnlockMutex(g_lock);

    char h[512];
    int n = snprintf(h, sizeof h, "HTTP/1.1 %s\r\nContent-Type: %s\r\nAccept-Ranges: bytes\r\nContent-Length: %llu\r\n", ranged ? "206 Partial Content" : "200 OK",
                     type, (unsigned long long)(to - from + 1));
    if (ranged) n += snprintf(h + n, sizeof h - (size_t)n, "Content-Range: bytes %llu-%llu/%llu\r\n", (unsigned long long)from, (unsigned long long)to, (unsigned long long)size);
    n += snprintf(h + n, sizeof h - (size_t)n, "Connection: close\r\n\r\n");
    char host[96];
    host_of(url, host, sizeof host);
    if (!head_only) LOGI("relay: %s from %llu-%llu, %d (from the buffer)", host, (unsigned long long)from, (unsigned long long)to, ranged ? 206 : 200);
    char *chunk = !head_only && send_all(fd, h, (size_t)n) ? malloc(CHUNK) : NULL;
    if (head_only) send_all(fd, h, (size_t)n);
    uint64_t at = from;
    int waited = 0;
    while (chunk && at <= to) {
        SDL_LockMutex(g_lock);
        int gone = route != g_buf.route || g_buf.stopped;
        if (!gone && at < g_buf.lo && g_buf.lo - at <= GAP && g_buf.lo <= to) {   /* the step back before what is in: from the source, then the buffer */
            uint64_t lo = g_buf.lo;
            SDL_UnlockMutex(g_lock);
            Conn c = { .fd = fd, .sent_head = 1 };
            char gap[64];
            snprintf(gap, sizeof gap, "%llu-%llu", (unsigned long long)at, (unsigned long long)(lo - 1));
            HttpStream cb = { rest_head, on_data, &c };
            if (http_stream(url, headers, gap, &cb) != 0 || c.sent != lo - at) break;   /* the source or the player stopped: the player asks again */
            at = lo;
            continue;
        }
        if (!gone && (at < g_buf.lo || at >= g_buf.hi)) {   /* not in yet: the download is on its way */
            if (at < g_buf.lo || at > g_buf.hi + JUMP || g_buf.gave_up || ++waited > 60) gone = 1;   /* someone jumped elsewhere, or it is not coming: from the source */
            else { dl_start_locked(); SDL_CondWaitTimeout(g_buf.cond, g_lock, 1000); SDL_UnlockMutex(g_lock); continue; }
        }
        if (gone) { SDL_UnlockMutex(g_lock); passthrough_rest(fd, url, headers, at, to); break; }   /* straight from the source for the rest */
        waited = 0;
        uint64_t k = g_buf.hi - at;
        if (k > CHUNK) k = CHUNK;
        if (k > to - at + 1) k = to - at + 1;
        int ok = get_locked(at, chunk, (size_t)k);
        SDL_UnlockMutex(g_lock);
        if (!ok) { passthrough_rest(fd, url, headers, at, to); break; }
        if (!send_all(fd, chunk, (size_t)k)) break;   /* the player closed it (a seek, a stop) */
        at += k;
        SDL_LockMutex(g_lock);
        if (route == g_buf.route) { g_buf.rd[me].at = at; g_buf.rd[me].seen = SDL_GetTicks(); pos_update_locked(); trim_locked(); SDL_CondBroadcast(g_buf.cond); }
        SDL_UnlockMutex(g_lock);
    }
    if (me >= 0) {
        SDL_LockMutex(g_lock);
        if (route == g_buf.route) { g_buf.rd[me].used = 0; pos_update_locked(); }
        SDL_UnlockMutex(g_lock);
    }
    free(chunk); free(url); free(headers);
    return 1;
}

static int serve_one(void *arg)
{
    int fd = (int)(intptr_t)arg;
    sock_timeout(fd, SO_RCVTIMEO, 5);
    sock_timeout(fd, SO_SNDTIMEO, 300);   /* paused: the player stops reading; after 5 min it reconnects when played again */
    char req[4096];
    size_t got = 0;
    while (got < sizeof req - 1) {
        ssize_t k = recv(fd, req + got, sizeof req - 1 - got, 0);
        if (k <= 0) break;
        got += (size_t)k;
        req[got] = 0;
        if (strstr(req, "\r\n\r\n")) break;
    }
    req[got] = 0;
    char token[33] = "", range[64] = "";
    int head_only = !strncmp(req, "HEAD ", 5), ranged = 0;
    const char *path = strchr(req, ' ');
    if (path && !strncmp(path + 1, "/r/", 3)) snprintf(token, sizeof token, "%.*s", (int)strcspn(path + 4, " ?/\r\n"), path + 4);   /* /r/<token>/<the file's name> */
    for (const char *l = strstr(req, "\r\n"); l && l[2]; l = strstr(l + 2, "\r\n"))
        if (!strncasecmp(l + 2, "Range:", 6)) {
            const char *v = l + 8;
            while (*v == ' ') v++;
            if (!strncasecmp(v, "bytes=", 6)) snprintf(range, sizeof range, "%.*s", (int)strcspn(v + 6, "\r\n,"), v + 6);   /* one range: what players ask */
            ranged = 1;
        }
    char *url = NULL, *headers = NULL;
    SDL_LockMutex(g_lock);
    for (int i = 0; token[0] && i < MAX_ROUTES; i++)
        if (g_routes[i].url && !strcmp(g_routes[i].token, token)) { url = strdup(g_routes[i].url); headers = g_routes[i].headers ? strdup(g_routes[i].headers) : NULL; }
    SDL_UnlockMutex(g_lock);
    if (!url) answer(fd, "404 Not Found");
    else if (buffered_serve(fd, token, range, ranged, head_only)) { /* came out of the buffer */ }
    else {
        Conn c = { .fd = fd, .head_only = head_only };
        HttpStream cb = { on_head, on_data, &c };
        char host[96];
        host_of(url, host, sizeof host);
        int rc = http_stream(url, headers, range[0] ? range : NULL, &cb);
        if (rc != 0 && !c.sent_head) { answer(fd, "502 Bad Gateway"); LOGW("relay: %s did not answer", host); }
        else if (c.status >= 400) LOGW("relay: %s answered %ld", host, c.status);
        else LOGI("relay: %s%s%s, %ld", host, range[0] ? " from " : "", range, c.status);
    }
    free(url); free(headers);
    shutdown(fd, SHUT_RDWR);
    sock_close(fd);
    SDL_AtomicAdd(&g_conns, -1);
    return 0;
}

static int listen_loop(void *arg)
{
    (void)arg;
    for (;;) {
        int fd = (int)accept(g_sock, NULL, NULL);
        if (fd < 0) { if (errno == EINTR) continue; SDL_Delay(50); continue; }
        if (SDL_AtomicGet(&g_conns) >= MAX_CONNS) { answer(fd, "503 Busy"); sock_close(fd); continue; }   /* old requests still closing */
        SDL_AtomicAdd(&g_conns, 1);
        SDL_Thread *t = SDL_CreateThread(serve_one, "relay", (void *)(intptr_t)fd);
        if (t) SDL_DetachThread(t);
        else { answer(fd, "503 Busy"); sock_close(fd); SDL_AtomicAdd(&g_conns, -1); }
    }
    return 0;
}

static int start(void)
{
    if (g_sock >= 0) return 1;
    if (g_failed) return 0;   /* tried once this run: the direct link from now on */
    g_failed = 1;
    if (!g_lock && !(g_lock = SDL_CreateMutex())) return 0;
    sock_startup();
    int s = (int)socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) { LOGW("relay: no socket; sources open directly"); return 0; }
#ifndef _WIN32   /* Windows would let two apps share the port */
    int one = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
#endif
    int port;
    for (port = 11481; port < 11490; port++) {   /* 11471-79 the phone page, 11490 the P2P engine */
        struct sockaddr_in a = { 0 };
        a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(INADDR_LOOPBACK); a.sin_port = htons((uint16_t)port);   /* this TV only */
        if (bind(s, (struct sockaddr *)&a, sizeof a) == 0) break;
    }
    if (port >= 11490 || listen(s, 8) != 0) { sock_close(s); LOGW("relay: could not listen; sources open directly"); return 0; }
    g_sock = s; g_port = port;
    SDL_Thread *t = SDL_CreateThread(listen_loop, "relay-listen", NULL);
    if (!t) { sock_close(s); g_sock = -1; LOGW("relay: no thread; sources open directly"); return 0; }
    SDL_DetachThread(t);
    g_failed = 0;
    LOGI("relay: listening on 127.0.0.1:%d", port);
    return 1;
}

static void new_token(char out[33])
{
    unsigned char b[16];
    FILE *f = fopen("/dev/urandom", "rb");
    size_t got = f ? fread(b, 1, sizeof b, f) : 0;
    if (f) fclose(f);
    if (got != sizeof b) {   /* no urandom: time and counter; still only reachable from this TV */
        Uint64 x = SDL_GetPerformanceCounter() ^ ((Uint64)SDL_GetTicks() << 32);
        for (size_t i = 0; i < sizeof b; i++) { x = x * 6364136223846793005ULL + 1442695040888963407ULL; b[i] = (unsigned char)(x >> 56); }
    }
    for (size_t i = 0; i < sizeof b; i++) snprintf(out + i * 2, 3, "%02x", b[i]);
}

static void name_of(const char *url, char *out, size_t n)   /* "Film.2024.mkv": the TV's player can tell the kind of file from it */
{
    const char *q = url + strcspn(url, "?#"), *b = q;
    while (b > url && b[-1] != '/') b--;
    size_t k = 0;
    for (const char *c = b; c < q && k + 1 < n; c++) if (isalnum((unsigned char)*c) || *c == '.' || *c == '-' || *c == '_') out[k++] = *c;
    out[k] = 0;
    if (!strchr(out, '.')) snprintf(out, n, "video");
}

char *relay_url(const char *url, const char *headers)
{
    if (!url || strncmp(url, "http", 4) || !relay_enabled() || !start()) return NULL;
    SDL_LockMutex(g_lock);
    Route *r = NULL;
    for (int i = 0; i < MAX_ROUTES; i++)   /* the same source again (a reconnect): its address again */
        if (g_routes[i].url && !strcmp(g_routes[i].url, url) && !strcmp(g_routes[i].headers ? g_routes[i].headers : "", headers ? headers : "")) r = &g_routes[i];
    if (!r) {
        r = &g_routes[g_next_route];
        g_next_route = (g_next_route + 1) % MAX_ROUTES;
        free(r->url); free(r->headers);
        r->url = strdup(url);
        r->headers = headers ? strdup(headers) : NULL;
        new_token(r->token);
    }
    name_of(url, r->name, sizeof r->name);
    char *out = malloc(160);
    snprintf(out, 160, "http://127.0.0.1:%d/r/%s/%s", g_port, r->token, r->name);
    SDL_UnlockMutex(g_lock);
    return out;
}

static void buf_drop_locked(void)
{
    g_buf.route++; g_buf.gen++;
    g_buf.token[0] = 0;
    free(g_buf.url); free(g_buf.headers); g_buf.url = g_buf.headers = NULL;
    for (int i = 0; i < g_buf.n_alt; i++) { free(g_buf.alt[i].url); free(g_buf.alt[i].headers); }
    g_buf.n_alt = 0;
    g_buf.failing = g_buf.gave_up = 0;
    free_blocks_locked();
    g_buf.size = g_buf.lo = g_buf.hi = g_buf.pos = 0; g_buf.budget = g_buf.max_budget = 0; g_buf.rate = 0;
    g_buf.stopped = g_buf.fails = g_buf.running = g_buf.got_any = g_buf.dead = 0;
    g_buf.asked_at = g_buf.slow_since = g_buf.win_at = g_buf.hungry_at = 0; g_buf.slow = g_buf.handoff_for_slow = 0; g_buf.in_rate = 0; g_buf.win_bytes = g_buf.hungry_bytes = 0;
    memset(g_buf.rd, 0, sizeof g_buf.rd);
    if (g_buf.cond) SDL_CondBroadcast(g_buf.cond);
}

char *relay_url_buffered(const char *url, const char *headers, double est_rate)
{
    char *out = relay_url(url, headers);
    if (!out || !relay_buffer_enabled()) return out;
    uint64_t mem = free_memory(), max = mem_small_tv() ? BUDGET_SMALL : BUDGET_MAX, bs = 4 << 20;
    const char *tiny = getenv("SNTV_FAKE_BUFFER_KB"), *blk = getenv("SNTV_FAKE_BLOCK_KB");   /* tests: a small one, in small blocks, to go round */
    if (tiny && *tiny) max = (uint64_t)strtoull(tiny, NULL, 10) << 10;
    if (blk && *blk) bs = (uint64_t)strtoull(blk, NULL, 10) << 10;
    uint64_t budget = tiny && *tiny ? max : budget_for(mem, 0, max);
    SDL_LockMutex(g_lock);
    buf_drop_locked();
    if (!g_buf.cond) g_buf.cond = SDL_CreateCond();
    if (budget < BUDGET_MIN && !(tiny && *tiny)) {
        SDL_UnlockMutex(g_lock);
        LOGI("relay: %.0f MB of memory free on the TV: no buffer ahead", mem / 1048576.0);
        return out;
    }
    const char *t = strstr(out, "/r/") + 3;
    snprintf(g_buf.token, sizeof g_buf.token, "%.*s", (int)strcspn(t, "/"), t);
    g_buf.url = strdup(url);
    g_buf.headers = headers ? strdup(headers) : NULL;
    g_buf.blk_size = bs; g_buf.max_budget = max; g_buf.budget = budget;
    g_buf.rate = est_rate > 0 ? est_rate : 0;
    g_buf.mem_at = SDL_GetTicks();
    SDL_UnlockMutex(g_lock);
    LOGI("relay: a buffer in memory, up to %.0f MB for now (%.0f MB free on the TV), for up to %d minutes ahead", budget / 1048576.0, mem / 1048576.0, AHEAD_S / 60);
    return out;
}

void relay_set_duration(double seconds)
{
    if (seconds <= 0 || !g_lock) return;
    SDL_LockMutex(g_lock);
    if (g_buf.token[0] && g_buf.size) g_buf.rate = g_buf.size / seconds;
    SDL_UnlockMutex(g_lock);
}

double relay_buffer_ahead(void)
{
    if (!g_lock) return -1;
    SDL_LockMutex(g_lock);
    double s = g_buf.token[0] && !g_buf.stopped && g_buf.rate > 0 && g_buf.hi > g_buf.pos ? (g_buf.hi - g_buf.pos) / g_buf.rate : -1;
    SDL_UnlockMutex(g_lock);
    return s;
}

void relay_buffer_alternates(const char *const *urls, const char *const *headers, int n)
{
    if (!g_lock) return;
    SDL_LockMutex(g_lock);
    for (int i = 0; i < n && g_buf.token[0] && g_buf.n_alt < (int)(sizeof g_buf.alt / sizeof g_buf.alt[0]); i++) {
        if (!urls[i] || !strcmp(urls[i], g_buf.url)) continue;
        g_buf.alt[g_buf.n_alt].url = strdup(urls[i]);
        g_buf.alt[g_buf.n_alt].headers = headers && headers[i] ? strdup(headers[i]) : NULL;
        g_buf.alt[g_buf.n_alt++].tried = 0;
    }
    SDL_UnlockMutex(g_lock);
}

int relay_buffer_failing(void)
{
    if (!g_lock) return 0;
    SDL_LockMutex(g_lock);
    int failing = g_buf.token[0] && !g_buf.stopped && (g_buf.failing || (g_buf.slow && (g_buf.handoff_for_slow || !g_buf.n_alt)));
    SDL_UnlockMutex(g_lock);
    return failing;
}

int relay_buffer_source_failed(void)
{
    if (!g_lock) return 0;
    SDL_LockMutex(g_lock);
    int failed = g_buf.dead;
    SDL_UnlockMutex(g_lock);
    return failed;
}

double relay_buffer_waiting_s(void)
{
    if (!g_lock) return -1;
    SDL_LockMutex(g_lock);
    double s = g_buf.token[0] && !g_buf.stopped && g_buf.asked_at && !g_buf.got_any && !g_buf.size ? (SDL_GetTicks() - g_buf.asked_at) / 1000.0 : -1;
    SDL_UnlockMutex(g_lock);
    return s;
}

int relay_buffer_span(uint64_t *lo, uint64_t *hi)
{
    if (!g_lock) return 0;
    SDL_LockMutex(g_lock);
    int ok = g_buf.token[0] && !g_buf.stopped && g_buf.hi > g_buf.lo;
    *lo = g_buf.lo; *hi = g_buf.hi;
    SDL_UnlockMutex(g_lock);
    return ok;
}

int relay_buffer_copy(uint64_t at, void *out, size_t n)
{
    if (!g_lock) return 0;
    SDL_LockMutex(g_lock);
    int ok = at >= g_buf.lo && at + n <= g_buf.hi && get_locked(at, out, n);
    SDL_UnlockMutex(g_lock);
    return ok;
}

int relay_buffer_read(const char *url, uint64_t at, void *out, size_t n, uint64_t *total)
{
    if (!g_lock || !url) return 0;
    SDL_LockMutex(g_lock);
    int ok = g_buf.token[0] && !g_buf.stopped && g_buf.url && !strcmp(g_buf.url, url) && at >= g_buf.lo && at + n <= g_buf.hi && get_locked(at, out, n);
    if (ok && total) *total = g_buf.size;
    SDL_UnlockMutex(g_lock);
    return ok;
}

void relay_buffer_close(void)
{
    if (!g_lock) return;
    SDL_LockMutex(g_lock);
    if (g_buf.token[0] || g_buf.held) buf_drop_locked();
    SDL_UnlockMutex(g_lock);
}
