#include "mkvsubs.h"
#include "http.h"
#include "log.h"
#include "relay.h"
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    ID_EBML = 0x1A45DFA3, ID_SEGMENT = 0x18538067, ID_INFO = 0x1549A966, ID_SCALE = 0x2AD7B1,
    ID_TRACKS = 0x1654AE6B, ID_ENTRY = 0xAE, ID_NUMBER = 0xD7, ID_TYPE = 0x83, ID_CODEC = 0x86,
    ID_LANG = 0x22B59C, ID_LANG_BCP = 0x22B59D, ID_NAME = 0x536E, ID_DEFAULT = 0x88, ID_FORCED = 0x55AA,
    ID_CLUSTER = 0x1F43B675, ID_TIMECODE = 0xE7, ID_SIMPLE = 0xA3, ID_GROUP = 0xA0, ID_BLOCK = 0xA1, ID_DURATION = 0x9B,
};
#define UNKNOWN_SIZE UINT64_MAX
#define HEAD_BYTES (1u << 20)        /* the tracks sit in the first few KB; a long SeekHead or attachments first, rarely */
#define MAX_TEXT_BLOCK (256u << 10)  /* a subtitle line is never this big */
#define MAX_LINES 20000

/* One EBML element header at p: id and size; the bytes it took, 0 when more are needed, -1 when it is not one. */
static int ebml_header(const uint8_t *p, size_t n, uint32_t *id, uint64_t *size)
{
    if (!n) return 0;
    int il = p[0] & 0x80 ? 1 : p[0] & 0x40 ? 2 : p[0] & 0x20 ? 3 : p[0] & 0x10 ? 4 : 0;
    if (!il) return -1;
    if (n < (size_t)il + 1) return 0;
    uint32_t v = 0;
    for (int i = 0; i < il; i++) v = v << 8 | p[i];
    uint8_t b = p[il];
    int sl = 1;
    while (sl <= 8 && !(b & (0x80 >> (sl - 1)))) sl++;
    if (sl > 8) return -1;
    if (n < (size_t)(il + sl)) return 0;
    uint64_t s = b & (0xFF >> sl);
    int all = s == (uint64_t)(0xFF >> sl);
    for (int i = 1; i < sl; i++) { s = s << 8 | p[il + i]; if (p[il + i] != 0xFF) all = 0; }
    *id = v;
    *size = all ? UNKNOWN_SIZE : s;
    return il + sl;
}

static uint64_t be_uint(const uint8_t *p, size_t n)
{
    uint64_t v = 0;
    for (size_t i = 0; i < n && i < 8; i++) v = v << 8 | p[i];
    return v;
}

static void copy_str(char *out, size_t on, const uint8_t *p, size_t n)
{
    size_t k = n < on - 1 ? n : on - 1;
    memcpy(out, p, k);
    out[k] = 0;
    while (k && !out[k - 1]) k--;   /* padded with zeros */
    out[k] = 0;
}

static void parse_entry(const uint8_t *d, size_t n, MkvSubHead *out)
{
    MkvSubTrack t;
    memset(&t, 0, sizeof t);
    t.deflt = 1;
    int type = 0;
    char bcp[12] = "";
    for (size_t i = 0; i < n;) {
        uint32_t id; uint64_t size;
        int h = ebml_header(d + i, n - i, &id, &size);
        if (h <= 0 || size == UNKNOWN_SIZE || size > n - i - (size_t)h) break;
        const uint8_t *v = d + i + h;
        if (id == ID_NUMBER) t.number = (int)be_uint(v, size);
        else if (id == ID_TYPE) type = (int)be_uint(v, size);
        else if (id == ID_CODEC) copy_str(t.codec, sizeof t.codec, v, size);
        else if (id == ID_LANG) copy_str(t.lang, sizeof t.lang, v, size);
        else if (id == ID_LANG_BCP) copy_str(bcp, sizeof bcp, v, size);
        else if (id == ID_NAME) copy_str(t.name, sizeof t.name, v, size);
        else if (id == ID_DEFAULT) t.deflt = (int)be_uint(v, size);
        else if (id == ID_FORCED) t.forced = (int)be_uint(v, size);
        i += (size_t)h + size;
    }
    if (type != 0x11 || t.number <= 0 || out->n >= MKVSUBS_TRACKS) return;   /* subtitles only */
    if (!t.lang[0]) snprintf(t.lang, sizeof t.lang, "%s", bcp[0] ? bcp : "eng");   /* Matroska's default language */
    t.ass = !strcmp(t.codec, "S_TEXT/ASS") || !strcmp(t.codec, "S_TEXT/SSA") || !strcmp(t.codec, "S_ASS") || !strcmp(t.codec, "S_SSA");
    t.text = t.ass || !strcmp(t.codec, "S_TEXT/UTF8") || !strcmp(t.codec, "S_TEXT/ASCII") || !strcmp(t.codec, "S_TEXT/WEBVTT");
    out->t[out->n++] = t;
}

static void parse_children(const uint8_t *d, size_t n, MkvSubHead *out, int tracks)
{
    for (size_t i = 0; i < n;) {
        uint32_t id; uint64_t size;
        int h = ebml_header(d + i, n - i, &id, &size);
        if (h <= 0 || size == UNKNOWN_SIZE || size > n - i - (size_t)h) break;
        if (tracks && id == ID_ENTRY) parse_entry(d + i + h, (size_t)size, out);
        else if (!tracks && id == ID_SCALE) { uint64_t ns = be_uint(d + i + h, size); if (ns) out->tick_ms = ns / 1e6; }
        i += (size_t)h + size;
    }
}

int mkvsubs_parse_head(const uint8_t *d, size_t n, MkvSubHead *out)
{
    memset(out, 0, sizeof *out);
    out->tick_ms = 1;
    int ebml = 0, tracks = 0;
    for (size_t i = 0; i < n;) {
        uint32_t id; uint64_t size;
        int h = ebml_header(d + i, n - i, &id, &size);
        if (h <= 0) break;
        i += (size_t)h;
        if (id == ID_EBML) ebml = 1;
        else if (!ebml) break;
        if (id == ID_SEGMENT) continue;   /* its children follow */
        if (id == ID_CLUSTER || size == UNKNOWN_SIZE || size > n - i) break;
        if (id == ID_INFO) parse_children(d + i, (size_t)size, out, 0);
        else if (id == ID_TRACKS) { parse_children(d + i, (size_t)size, out, 1); tracks = 1; }
        i += (size_t)size;
    }
    return ebml && tracks;
}

/* ---- the lines, read from the bytes as they come ---- */

typedef struct { int number; SubCue *c; int n, cap; } Lines;
enum { M_SYNC, M_HEAD, M_SKIP, M_PAY };

static struct {
    SDL_mutex *lock;
    unsigned gen;
    MkvSubHead head; int have_head, any_text;
    Lines lines[MKVSUBS_TRACKS];
    uint64_t expect, read_from;   /* bytes [read_from, expect) were read, one stretch */
    int mode, probe;             /* probe: a block whose track is not known yet */
    uint32_t win;                /* the last four bytes, while looking for a cluster */
    uint8_t hdr[12]; int hlen;
    uint64_t left;               /* bytes of the element still to skip or to read */
    uint32_t cur;
    uint8_t *pay; size_t plen, pcap;
    uint64_t cluster_tc; int have_tc;
    uint64_t group_end;
    struct { int on, track, top; double start, dur; char *text; } pend;
} S;

static void ensure_lock(void) { if (!S.lock) S.lock = SDL_CreateMutex(); }

static Lines *lines_for(int number)
{
    for (int i = 0; i < S.head.n; i++) if (S.lines[i].number == number) return &S.lines[i];
    return NULL;
}

static int is_text(int number)
{
    for (int i = 0; i < S.head.n; i++) if (S.head.t[i].number == number) return S.head.t[i].text;
    return 0;
}

static int is_ass(int number)
{
    for (int i = 0; i < S.head.n; i++) if (S.head.t[i].number == number) return S.head.t[i].ass;
    return 0;
}

/* A line's text as it is shown: ASS fields and override tags, SRT/WebVTT tags out; ASS line
 * breaks in. *top for {\an7}..{\an9}. 0 when nothing is left to show. */
static size_t clean_text(const uint8_t *s, size_t n, int ass, int *top, char *out, size_t on)
{
    size_t i = 0, o = 0;
    *top = 0;
    if (ass) {   /* ReadOrder,Layer,Style,Name,MarginL,MarginR,MarginV,Effect,Text */
        int commas = 0;
        size_t j = 0;
        while (j < n && commas < 8) if (s[j++] == ',') commas++;
        if (commas == 8) i = j;
    }
    while (i < n && o + 4 < on) {
        uint8_t c = s[i];
        if (c == '{') {   /* override tags */
            size_t e = i;
            while (e < n && s[e] != '}') e++;
            for (size_t k = i; k + 3 < e; k++) if (s[k] == '\\' && s[k + 1] == 'a' && s[k + 2] == 'n' && s[k + 3] >= '7' && s[k + 3] <= '9') *top = 1;
            i = e < n ? e + 1 : n;
            continue;
        }
        if (c == '<' && !ass) {   /* <i>, <font ...>, WebVTT's voice tags */
            size_t e = i;
            while (e < n && s[e] != '>' && e - i < 64) e++;
            if (e < n && s[e] == '>') { i = e + 1; continue; }
        }
        if (c == '\\' && i + 1 < n && (s[i + 1] == 'N' || (ass && s[i + 1] == 'n'))) { out[o++] = '\n'; i += 2; continue; }
        if (c == '\\' && ass && i + 1 < n && s[i + 1] == 'h') { out[o++] = ' '; i += 2; continue; }
        if (c == '\r' || c == 0) { i++; continue; }
        out[o++] = (char)c;
        i++;
    }
    while (o && (out[o - 1] == '\n' || out[o - 1] == ' ')) o--;
    out[o] = 0;
    size_t k = 0;
    while (out[k] == '\n' || out[k] == ' ') k++;
    if (k) { memmove(out, out + k, o - k + 1); o -= k; }
    return o;
}

static void emit(int track, double start, double dur, const char *text, int top)
{
    Lines *l = lines_for(track);
    if (!l || !text[0] || l->n >= MAX_LINES) return;
    for (int i = l->n - 1; i >= 0 && i >= l->n - 48; i--)   /* read again after a jump back */
        if (l->c[i].start > start - 0.01 && l->c[i].start < start + 0.01 && !strcmp(l->c[i].text, text)) return;
    if (dur <= 0) {   /* a SimpleBlock has no duration: as long as it takes to read */
        dur = 1.5 + strlen(text) / 15.0;
        if (dur > 7) dur = 7;
    }
    if (l->n == l->cap) {
        int cap = l->cap ? l->cap * 2 : 256;
        SubCue *g = realloc(l->c, sizeof *g * (size_t)cap);
        if (!g) return;
        l->c = g; l->cap = cap;
    }
    SubCue *c = &l->c[l->n];
    c->start = start; c->end = start + dur; c->top = top;
    c->text = strdup(text);
    if (c->text) l->n++;
}

static void finish_pending(void)
{
    if (!S.pend.on) return;
    emit(S.pend.track, S.pend.start, S.pend.dur, S.pend.text, S.pend.top);
    free(S.pend.text);
    memset(&S.pend, 0, sizeof S.pend);
}

static void to_sync(void)
{
    finish_pending();
    S.mode = M_SYNC; S.win = 0; S.hlen = 0; S.group_end = 0; S.probe = 0;
}

static void block_done(int simple)
{
    const uint8_t *p = S.pay;
    size_t n = S.plen;
    int tl = n ? (p[0] & 0x80 ? 1 : p[0] & 0x40 ? 2 : p[0] & 0x20 ? 3 : p[0] & 0x10 ? 4 : 0) : 0;
    if (!tl || n < (size_t)tl + 3 || !S.have_tc) return;
    int track = p[0] & (0xFF >> tl);
    for (int i = 1; i < tl; i++) track = track << 8 | p[i];
    int16_t rel = (int16_t)(p[tl] << 8 | p[tl + 1]);
    uint8_t flags = p[tl + 2];
    if ((flags >> 1) & 3) return;   /* laced: never for subtitles in practice */
    if (!is_text(track)) return;
    double start = ((double)S.cluster_tc + rel) * S.head.tick_ms / 1000.0;
    char text[4096];
    int top = 0;
    if (!clean_text(p + tl + 3, n - (size_t)tl - 3, is_ass(track), &top, text, sizeof text)) return;
    if (simple) { emit(track, start, 0, text, top); return; }
    finish_pending();
    S.pend.on = 1; S.pend.track = track; S.pend.start = start; S.pend.top = top; S.pend.dur = 0;
    S.pend.text = strdup(text);
    if (!S.pend.text) S.pend.on = 0;
}

static void element_done(uint64_t at)
{
    if (S.group_end && at >= S.group_end) { finish_pending(); S.group_end = 0; }
    S.mode = M_HEAD;
}

static void payload_done(uint64_t at)
{
    if (S.cur == ID_TIMECODE) { S.cluster_tc = be_uint(S.pay, S.plen); S.have_tc = 1; }
    else if (S.cur == ID_DURATION) S.pend.dur = (double)be_uint(S.pay, S.plen) * S.head.tick_ms / 1000.0;
    else if (S.cur == ID_SIMPLE || S.cur == ID_BLOCK) block_done(S.cur == ID_SIMPLE);
    element_done(at);
}

static void element(uint32_t id, uint64_t size, uint64_t at)
{
    if (id == ID_SEGMENT || id == ID_CLUSTER) {   /* their children follow */
        if (id == ID_CLUSTER) { finish_pending(); S.have_tc = 0; S.group_end = 0; }
        S.mode = M_HEAD;
        return;
    }
    if (id == ID_GROUP) { finish_pending(); S.group_end = size == UNKNOWN_SIZE ? 0 : at + size; S.mode = M_HEAD; return; }
    if (size == UNKNOWN_SIZE || size > ((uint64_t)1 << 34)) { to_sync(); return; }   /* not a sane element: lost the thread */
    if (!size) { element_done(at); return; }
    S.cur = id; S.left = size; S.plen = 0; S.probe = 0;
    if (id == ID_TIMECODE || id == ID_DURATION) { if (size > 8) { S.mode = M_SKIP; return; } S.mode = M_PAY; return; }
    if ((id == ID_SIMPLE || id == ID_BLOCK) && size <= MAX_TEXT_BLOCK) { S.mode = M_PAY; S.probe = 1; return; }
    S.mode = M_SKIP;
}

static int grow_pay(size_t need)
{
    if (need <= S.pcap) return 1;
    size_t cap = S.pcap ? S.pcap : 4096;
    while (cap < need) cap *= 2;
    uint8_t *g = realloc(S.pay, cap);
    if (!g) return 0;
    S.pay = g; S.pcap = cap;
    return 1;
}

static void feed_locked(uint64_t off, const uint8_t *d, size_t n)
{
    int seen = S.expect != UINT64_MAX && off >= S.read_from && off < S.expect;   /* in the stretch read since the last jump */
    if (seen && off + n <= S.expect) return;   /* all of it read already (a replay met the download) */
    if (seen) { size_t k = (size_t)(S.expect - off); d += k; n -= k; off = S.expect; }   /* partly */
    if (off != S.expect) {   /* a jump: from the file's start, or the next cluster */
        finish_pending();
        S.read_from = off;
        if (off == 0) { S.mode = M_HEAD; S.hlen = 0; } else to_sync();
    }
    for (size_t i = 0; i < n;) {
        if (S.mode == M_SYNC) {
            for (; i < n; i++) {
                S.win = S.win << 8 | d[i];
                if (S.win == ID_CLUSTER) { i++; S.hdr[0] = 0x1F; S.hdr[1] = 0x43; S.hdr[2] = 0xB6; S.hdr[3] = 0x75; S.hlen = 4; S.mode = M_HEAD; break; }
            }
        } else if (S.mode == M_HEAD) {
            S.hdr[S.hlen++] = d[i++];
            uint32_t id; uint64_t size;
            int h = ebml_header(S.hdr, (size_t)S.hlen, &id, &size);
            if (h == 0) { if (S.hlen >= 12) to_sync(); continue; }
            if (h < 0) { to_sync(); continue; }
            S.hlen = 0;
            element(id, size, off + i);
        } else if (S.mode == M_SKIP) {
            size_t k = S.left < n - i ? (size_t)S.left : n - i;
            i += k; S.left -= k;
            if (!S.left) element_done(off + i);
        } else {   /* M_PAY */
            size_t k = S.left < n - i ? (size_t)S.left : n - i;
            if (S.probe) k = 1;   /* a byte at a time until the block's track is known */
            if (!grow_pay(S.plen + k)) { to_sync(); continue; }
            memcpy(S.pay + S.plen, d + i, k);
            S.plen += k; i += k; S.left -= k;
            if (S.probe) {
                int tl = S.pay[0] & 0x80 ? 1 : S.pay[0] & 0x40 ? 2 : S.pay[0] & 0x20 ? 3 : S.pay[0] & 0x10 ? 4 : 0;
                if (!tl) { S.mode = S.left ? M_SKIP : M_HEAD; S.probe = 0; if (!S.left) element_done(off + i); continue; }
                if (S.plen >= (size_t)tl) {
                    int track = S.pay[0] & (0xFF >> tl);
                    for (int b = 1; b < tl; b++) track = track << 8 | S.pay[b];
                    S.probe = 0;
                    if (!is_text(track)) { if (S.left) S.mode = M_SKIP; else element_done(off + i); continue; }
                }
            }
            if (!S.left && !S.probe) payload_done(off + i);
            else if (!S.left) element_done(off + i);
        }
    }
    S.expect = off + n;
}

void mkvsubs_feed(uint64_t off, const uint8_t *d, size_t n)
{
    if (!S.lock || !n) return;
    SDL_LockMutex(S.lock);
    if (S.have_head && S.any_text) feed_locked(off, d, n);
    SDL_UnlockMutex(S.lock);
}

static void clear_locked(void)
{
    for (int i = 0; i < MKVSUBS_TRACKS; i++) {
        for (int k = 0; k < S.lines[i].n; k++) free(S.lines[i].c[k].text);
        free(S.lines[i].c);
    }
    memset(S.lines, 0, sizeof S.lines);
    free(S.pend.text);
    memset(&S.pend, 0, sizeof S.pend);
    S.have_head = S.any_text = 0;
    S.expect = UINT64_MAX; S.mode = M_SYNC; S.win = 0; S.hlen = 0; S.left = 0; S.plen = 0; S.probe = 0;
    S.have_tc = 0; S.group_end = 0;
}

static void use_head_locked(const MkvSubHead *h)
{
    S.head = *h;
    S.have_head = 1;
    S.any_text = 0;
    for (int i = 0; i < h->n; i++) { S.lines[i].number = h->t[i].number; S.any_text |= h->t[i].text; }
}

void mkvsubs_reset(const MkvSubHead *head)
{
    ensure_lock();
    SDL_LockMutex(S.lock);
    S.gen++;
    clear_locked();
    if (head) use_head_locked(head);
    SDL_UnlockMutex(S.lock);
}

typedef struct { char *url, *headers; unsigned gen; } Open;

static int open_main(void *arg)
{
    Open *o = arg;
    HttpOptions opt = { .range_bytes = HEAD_BYTES, .timeout_s = 15, .headers = o->headers };
    HttpResponse r;
    http_request_ex(o->url, &opt, &r);
    MkvSubHead h;
    int mkv = (r.status == 200 || r.status == 206) && r.body && mkvsubs_parse_head((const uint8_t *)r.body, r.len, &h);
    http_response_free(&r);
    SDL_LockMutex(S.lock);
    if (o->gen == S.gen && mkv) {
        use_head_locked(&h);
        char list[300] = "";
        size_t L = 0;
        for (int i = 0; i < h.n && L + 40 < sizeof list; i++)
            L += (size_t)snprintf(list + L, sizeof list - L, "%s%s %s%s", i ? ", " : "", h.t[i].lang, h.t[i].codec, h.t[i].text ? "" : " (a picture: the TV's)");
        LOGI("mkvsubs: %d subtitle track%s in the file%s%s", h.n, h.n == 1 ? "" : "s", h.n ? ": " : "", list);
        if (S.any_text) {   /* what the buffer has already: its lines too, from where it starts */
            uint64_t lo, hi;
            static uint8_t chunk[256 << 10];
            if (relay_buffer_span(&lo, &hi))
                for (uint64_t at = lo; at < hi;) {
                    size_t k = hi - at < sizeof chunk ? (size_t)(hi - at) : sizeof chunk;
                    if (!relay_buffer_copy(at, chunk, k)) break;
                    feed_locked(at, chunk, k);
                    at += k;
                }
        }
    }
    SDL_UnlockMutex(S.lock);
    free(o->url); free(o->headers); free(o);
    return 0;
}

void mkvsubs_open(const char *url, const char *headers)
{
    ensure_lock();
    SDL_LockMutex(S.lock);
    S.gen++;
    clear_locked();
    unsigned gen = S.gen;
    SDL_UnlockMutex(S.lock);
    if (!url) return;
    Open *o = calloc(1, sizeof *o);
    if (!o) return;
    o->url = strdup(url); o->headers = headers ? strdup(headers) : NULL; o->gen = gen;
    SDL_Thread *t = SDL_CreateThread(open_main, "mkvsubs", o);
    if (t) SDL_DetachThread(t);
    else { free(o->url); free(o->headers); free(o); }
}

int mkvsubs_head(MkvSubHead *out)
{
    if (!S.lock) return 0;
    SDL_LockMutex(S.lock);
    int ok = S.have_head;
    if (ok && out) *out = S.head;
    SDL_UnlockMutex(S.lock);
    return ok;
}

int mkvsubs_count(int number)
{
    if (!S.lock) return 0;
    SDL_LockMutex(S.lock);
    Lines *l = lines_for(number);
    int n = l ? l->n : 0;
    SDL_UnlockMutex(S.lock);
    return n;
}

int mkvsubs_take(int number, int from, SubCue *out, int max)
{
    if (!S.lock) return 0;
    SDL_LockMutex(S.lock);
    Lines *l = lines_for(number);
    int k = 0;
    for (int i = from; l && i < l->n && k < max; i++) {
        out[k] = l->c[i];
        out[k].text = strdup(l->c[i].text);
        if (out[k].text) k++;
    }
    SDL_UnlockMutex(S.lock);
    return k;
}
