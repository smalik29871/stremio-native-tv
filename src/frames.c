#include "frames.h"
#include "http.h"
#include "log.h"
#include <SDL.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef SNTV_HAVE_FFMPEG
#include <libavcodec/avcodec.h>
#include <libavutil/frame.h>
#include <libavutil/mem.h>
#endif
#ifdef SNTV_FFMPEG_DLOPEN
/* FFmpeg's own libraries in lib/ beside the app (LGPL: anyone can put their own build of the
 * same version there), loaded when first needed. Without them the TV has no previews of its
 * own, and the app runs on. */
#include <dlfcn.h>
#include <libavutil/version.h>
#include <unistd.h>
#define FF_STR2(x) #x
#define FF_STR(x) FF_STR2(x)
static struct {
    int tried, ok;
    const AVCodec *(*find_decoder)(enum AVCodecID);
    AVCodecContext *(*alloc_context3)(const AVCodec *);
    int (*open2)(AVCodecContext *, const AVCodec *, AVDictionary **);
    int (*send_packet)(AVCodecContext *, const AVPacket *);
    int (*receive_frame)(AVCodecContext *, AVFrame *);
    void (*free_context)(AVCodecContext **);
    AVPacket *(*packet_alloc)(void);
    void (*packet_free)(AVPacket **);
    int (*new_packet)(AVPacket *, int);
    void *(*mallocz)(size_t);
    AVFrame *(*frame_alloc)(void);
    void (*frame_free)(AVFrame **);
} FF;

static int ff_load_once(void);
static int ff_load(void)   /* the player's thread and the phone server's may both ask first */
{
    static SDL_SpinLock lock;
    SDL_AtomicLock(&lock);
    if (!FF.tried) { FF.tried = 1; FF.ok = ff_load_once(); }
    SDL_AtomicUnlock(&lock);
    return FF.ok;
}

static int ff_load_once(void)
{
    char dir[1024], path[1100];
    ssize_t n = readlink("/proc/self/exe", dir, sizeof dir - 1);
    if (n <= 0) return 0;
    dir[n] = 0;
    char *slash = strrchr(dir, '/');
    if (slash) *slash = 0;
    /* libavutil first and global: libavcodec needs it by name, and finds it loaded */
    snprintf(path, sizeof path, "%s/lib/libavutil.so." FF_STR(LIBAVUTIL_VERSION_MAJOR), dir);
    void *u = dlopen(path, RTLD_NOW | RTLD_GLOBAL);
    snprintf(path, sizeof path, "%s/lib/libavcodec.so." FF_STR(LIBAVCODEC_VERSION_MAJOR), dir);
    void *c = u ? dlopen(path, RTLD_NOW | RTLD_LOCAL) : NULL;
    if (!c) { LOGW("frames: FFmpeg's libraries did not load (%s): no previews decoded here", dlerror()); return 0; }
#define FF_SYM(lib, field, name) (*(void **)&FF.field = dlsym(lib, name))
    int ok = FF_SYM(c, find_decoder, "avcodec_find_decoder") && FF_SYM(c, alloc_context3, "avcodec_alloc_context3")
          && FF_SYM(c, open2, "avcodec_open2") && FF_SYM(c, send_packet, "avcodec_send_packet")
          && FF_SYM(c, receive_frame, "avcodec_receive_frame") && FF_SYM(c, free_context, "avcodec_free_context")
          && FF_SYM(c, packet_alloc, "av_packet_alloc") && FF_SYM(c, packet_free, "av_packet_free")
          && FF_SYM(c, new_packet, "av_new_packet") && FF_SYM(u, mallocz, "av_mallocz")
          && FF_SYM(u, frame_alloc, "av_frame_alloc") && FF_SYM(u, frame_free, "av_frame_free");
#undef FF_SYM
    if (!ok) { LOGW("frames: FFmpeg's libraries lack a function the previews use"); return 0; }
    LOGI("frames: FFmpeg loaded from %s/lib", dir);
    return 1;
}
#define avcodec_find_decoder FF.find_decoder
#define avcodec_alloc_context3 FF.alloc_context3
#define avcodec_open2 FF.open2
#define avcodec_send_packet FF.send_packet
#define avcodec_receive_frame FF.receive_frame
#define avcodec_free_context FF.free_context
#define av_packet_alloc FF.packet_alloc
#define av_packet_free FF.packet_free
#define av_new_packet FF.new_packet
#define av_mallocz FF.mallocz
#define av_frame_alloc FF.frame_alloc
#define av_frame_free FF.frame_free
#endif

enum { C_NONE, C_MKV, C_MP4 };
enum { V_NONE, V_H264, V_HEVC };
enum { HEAD_BYTES = 256 << 10, FIRST_READ = 192 << 10, MAX_ELEMENT = 32 << 20, MAX_KEYS = 20000, CACHED = 3 };

typedef struct { double t; uint64_t pos; uint32_t size; } Key;   /* mp4: the sample; mkv: its cluster (size 0) */

typedef struct {
    char key[700];
    int container, codec, track;
    uint8_t *extra; size_t n_extra;   /* avcC / hvcC */
    Key *keys; int n_keys;
    char err[160];                    /* why this video has no previews (kept, so it is not retried) */
} Index;

static Index g_cache[CACHED];
static int g_next;

int frames_supported(void)
{
#ifdef SNTV_FFMPEG_DLOPEN
    return ff_load();
#elif defined(SNTV_HAVE_FFMPEG)
    return 1;
#else
    return 0;
#endif
}

static int fail(char *err, size_t en, const char *msg)
{
    snprintf(err, en, "%s", msg);
    return 0;
}

static int add_key(Index *ix, double t, uint64_t pos, uint32_t size)
{
    if (ix->n_keys >= MAX_KEYS) return 0;
    if (ix->n_keys && t < ix->keys[ix->n_keys - 1].t + 0.5) return 1;   /* one a half second is plenty */
    if (!(ix->n_keys & 255)) ix->keys = realloc(ix->keys, sizeof *ix->keys * (size_t)(ix->n_keys + 256));
    ix->keys[ix->n_keys++] = (Key){ t, pos, size };
    return 1;
}

static void set_extra(Index *ix, const uint8_t *p, size_t n)
{
    free(ix->extra);
    ix->extra = malloc(n + 64);   /* FFmpeg wants padding after extradata */
    memcpy(ix->extra, p, n);
    memset(ix->extra + n, 0, 64);
    ix->n_extra = n;
}

/* ---- Matroska ------------------------------------------------------------------ */

/* A variable-length integer; the marker bit kept for element ids. */
static int vint(const uint8_t *p, const uint8_t *end, uint64_t *v, int keep_marker)
{
    if (p >= end) return 0;
    int len = 1;
    uint8_t mask = 0x80;
    while (len <= 8 && !(p[0] & mask)) { mask >>= 1; len++; }
    if (len > 8 || p + len > end) return 0;
    uint64_t x = keep_marker ? p[0] : (p[0] & (mask - 1));
    for (int i = 1; i < len; i++) x = (x << 8) | p[i];
    *v = x;
    return len;
}

/* An element header: id and payload size (UINT64_MAX when unknown). Returns its length. */
static int ebml(const uint8_t *p, const uint8_t *end, uint32_t *id, uint64_t *size)
{
    uint64_t i, s;
    int a = vint(p, end, &i, 1);
    if (!a) return 0;
    int b = vint(p + a, end, &s, 0);
    if (!b) return 0;
    if (s == (1ULL << (7 * b)) - 1) s = UINT64_MAX;
    *id = (uint32_t)i; *size = s;
    return a + b;
}

static uint64_t be_uint(const uint8_t *p, uint64_t n)
{
    uint64_t v = 0;
    for (uint64_t i = 0; i < n && i < 8; i++) v = (v << 8) | p[i];
    return v;
}

/* The whole element at `off` (header included) when it has id `want`, else NULL. */
static uint8_t *fetch_element(FramesRead rd, void *ctx, uint64_t off, uint32_t want, size_t *len, int *hdr)
{
    uint8_t *b; size_t got; uint64_t total;
    if (!rd(ctx, off, 64 << 10, &b, &got, &total)) return NULL;
    uint32_t id; uint64_t size;
    int h = ebml(b, b + got, &id, &size);
    if (!h || id != want || size == UINT64_MAX || size > MAX_ELEMENT) { free(b); return NULL; }
    if (h + size > got) {
        free(b);
        if (!rd(ctx, off, h + size, &b, &got, &total) || got < h + size) { free(b); return NULL; }
    }
    *len = (size_t)(h + size); *hdr = h;
    return b;
}

static int mkv_tracks(Index *ix, const uint8_t *p, const uint8_t *end)
{
    while (p < end) {
        uint32_t id; uint64_t size;
        int h = ebml(p, end, &id, &size);
        if (!h || size > (uint64_t)(end - p - h)) break;
        const uint8_t *q = p + h, *qe = q + size;
        if (id == 0xAE) {   /* TrackEntry */
            uint64_t number = 0, type = 0;
            char codec[64] = "";
            const uint8_t *priv = NULL; uint64_t n_priv = 0;
            while (q < qe) {
                uint32_t cid; uint64_t cs;
                int ch = ebml(q, qe, &cid, &cs);
                if (!ch || cs > (uint64_t)(qe - q - ch)) break;
                const uint8_t *v = q + ch;
                if (cid == 0xD7) number = be_uint(v, cs);
                else if (cid == 0x83) type = be_uint(v, cs);
                else if (cid == 0x86) snprintf(codec, sizeof codec, "%.*s", (int)(cs < 63 ? cs : 63), (const char *)v);
                else if (cid == 0x63A2) { priv = v; n_priv = cs; }
                q = v + cs;
            }
            int codec_id = !strcmp(codec, "V_MPEG4/ISO/AVC") ? V_H264 : !strcmp(codec, "V_MPEGH/ISO/HEVC") ? V_HEVC : V_NONE;
            if (type == 1 && !ix->track) {
                if (!codec_id) { snprintf(ix->err, sizeof ix->err, "no previews for %s video", codec[0] ? codec : "this"); return 0; }
                if (!priv) { snprintf(ix->err, sizeof ix->err, "the video track has no codec header"); return 0; }
                ix->track = (int)number; ix->codec = codec_id;
                set_extra(ix, priv, (size_t)n_priv);
            }
        }
        p += h + size;
    }
    return ix->track != 0;
}

static void mkv_cues(Index *ix, const uint8_t *p, const uint8_t *end, uint64_t seg0, double scale)
{
    while (p < end) {
        uint32_t id; uint64_t size;
        int h = ebml(p, end, &id, &size);
        if (!h || size > (uint64_t)(end - p - h)) break;
        if (id == 0xBB) {   /* CuePoint */
            const uint8_t *q = p + h, *qe = q + size;
            uint64_t time = 0, cluster = UINT64_MAX;
            while (q < qe) {
                uint32_t cid; uint64_t cs;
                int ch = ebml(q, qe, &cid, &cs);
                if (!ch || cs > (uint64_t)(qe - q - ch)) break;
                if (cid == 0xB3) time = be_uint(q + ch, cs);
                else if (cid == 0xB7) {   /* CueTrackPositions */
                    const uint8_t *r = q + ch, *re = r + cs;
                    uint64_t track = 0, pos = UINT64_MAX;
                    while (r < re) {
                        uint32_t rid; uint64_t rs;
                        int rh = ebml(r, re, &rid, &rs);
                        if (!rh || rs > (uint64_t)(re - r - rh)) break;
                        if (rid == 0xF7) track = be_uint(r + rh, rs);
                        else if (rid == 0xF1) pos = be_uint(r + rh, rs);
                        r += rh + rs;
                    }
                    if ((int)track == ix->track && pos != UINT64_MAX) cluster = pos;
                }
                q += ch + cs;
            }
            if (cluster != UINT64_MAX && !add_key(ix, (double)time * scale, seg0 + cluster, 0)) return;
        }
        p += h + size;
    }
}

static int mkv_index(FramesRead rd, void *ctx, Index *ix, const uint8_t *b, size_t got)
{
    const uint8_t *end = b + got;
    uint32_t id; uint64_t size;
    int h = ebml(b, end, &id, &size);   /* EBML header */
    if (!h || size > got) return 0;
    const uint8_t *p = b + h + size;
    h = ebml(p, end, &id, &size);
    if (!h || id != 0x18538067) { snprintf(ix->err, sizeof ix->err, "no Matroska segment"); return 0; }
    uint64_t seg0 = (uint64_t)(p + h - b), cues = 0, tracks = 0;
    double scale = 1e-3;   /* TimecodeScale 1 ms in seconds */
    p += h;
    while (p < end) {   /* the top level up to the first cluster */
        int eh = ebml(p, end, &id, &size);
        if (!eh || id == 0x1F43B675 || size == UINT64_MAX) break;
        const uint8_t *q = p + eh, *qe = size <= (uint64_t)(end - q) ? q + size : NULL;
        if (id == 0x114D9B74 && qe) {   /* SeekHead */
            while (q < qe) {
                uint32_t sid; uint64_t ss;
                int sh = ebml(q, qe, &sid, &ss);
                if (!sh || ss > (uint64_t)(qe - q - sh)) break;
                if (sid == 0x4DBB) {   /* Seek */
                    const uint8_t *r = q + sh, *re = r + ss;
                    uint64_t what = 0, where = 0;
                    while (r < re) {
                        uint32_t rid; uint64_t rs;
                        int rh = ebml(r, re, &rid, &rs);
                        if (!rh || rs > (uint64_t)(re - r - rh)) break;
                        if (rid == 0x53AB) what = be_uint(r + rh, rs);
                        else if (rid == 0x53AC) where = be_uint(r + rh, rs);
                        r += rh + rs;
                    }
                    if (what == 0x1C53BB6B) cues = where;
                    else if (what == 0x1654AE6B) tracks = where;
                }
                q += sh + ss;
            }
        } else if (id == 0x1549A966 && qe) {   /* Info: TimecodeScale */
            while (q < qe) {
                uint32_t sid; uint64_t ss;
                int sh = ebml(q, qe, &sid, &ss);
                if (!sh || ss > (uint64_t)(qe - q - sh)) break;
                if (sid == 0x2AD7B1) scale = (double)be_uint(q + sh, ss) * 1e-9;
                q += sh + ss;
            }
        } else if (id == 0x1654AE6B && qe) {
            if (!mkv_tracks(ix, q, qe) && ix->err[0]) return 0;
        } else if (id == 0x1C53BB6B && qe) {
            if (ix->track) mkv_cues(ix, q, qe, seg0, scale);
        }
        if (!qe) break;
        p = qe;
    }
    size_t n; int eh;
    uint8_t *el;
    if (!ix->track && tracks && (el = fetch_element(rd, ctx, seg0 + tracks, 0x1654AE6B, &n, &eh))) {
        mkv_tracks(ix, el + eh, el + n);
        free(el);
    }
    if (!ix->track) { if (!ix->err[0]) snprintf(ix->err, sizeof ix->err, "no video track found"); return 0; }
    if (!ix->n_keys && cues && (el = fetch_element(rd, ctx, seg0 + cues, 0x1C53BB6B, &n, &eh))) {
        mkv_cues(ix, el + eh, el + n, seg0, scale);
        free(el);
    }
    if (!ix->n_keys) { snprintf(ix->err, sizeof ix->err, "the file has no keyframe index (Cues)"); return 0; }
    ix->container = C_MKV;
    return 1;
}

/* The first video keyframe in the cluster at key->pos (malloc'ed frame data). */
static uint8_t *mkv_keyframe(FramesRead rd, void *ctx, const Index *ix, const Key *k, size_t *n, char *err, size_t en)
{
    uint8_t *b; size_t got; uint64_t total;
    if (!rd(ctx, k->pos, FIRST_READ, &b, &got, &total)) { fail(err, en, "the video did not answer"); return NULL; }   /* the cluster's start: its first block is the keyframe */
    const uint8_t *end = b + got;
    uint32_t id; uint64_t size;
    int h = ebml(b, end, &id, &size);
    if (!h || id != 0x1F43B675) { free(b); fail(err, en, "no cluster where the index points"); return NULL; }
    const uint8_t *p = b + h;
    while (p < end) {
        int eh = ebml(p, end, &id, &size);
        if (!eh || size == UINT64_MAX || id == 0x1F43B675) break;
        const uint8_t *blk = NULL; uint64_t blk_size = 0; int key = 0;
        if (id == 0xA3) { blk = p + eh; blk_size = size; }   /* SimpleBlock */
        else if (id == 0xA0 && size <= (uint64_t)(end - p - eh)) {   /* BlockGroup: a keyframe has no ReferenceBlock */
            const uint8_t *q = p + eh, *qe = q + size;
            key = 1;
            while (q < qe) {
                uint32_t gid; uint64_t gs;
                int gh = ebml(q, qe, &gid, &gs);
                if (!gh || gs > (uint64_t)(qe - q - gh)) break;
                if (gid == 0xA1) { blk = q + gh; blk_size = gs; }
                else if (gid == 0xFB) key = 0;
                q += gh + gs;
            }
        }
        if (blk && blk_size > 4) {
            uint64_t track;
            int tl = vint(blk, end, &track, 0);
            if (tl && (int)track == ix->track && blk + tl + 3 <= end) {
                uint8_t flags = blk[tl + 2];
                if (id == 0xA3) key = flags & 0x80;
                if (key) {
                    if (flags & 0x06) { free(b); fail(err, en, "laced video blocks are not supported"); return NULL; }
                    uint64_t at = k->pos + (uint64_t)(blk + tl + 3 - b), len = blk_size - (uint64_t)tl - 3;
                    uint8_t *frame;
                    if (blk + blk_size <= end) { frame = malloc(len + 64); memcpy(frame, blk + tl + 3, len); }
                    else {   /* a big keyframe: what came, then only the rest of its bytes */
                        size_t have = (size_t)(end - (blk + tl + 3)), g2;
                        uint8_t *rest;
                        if (len > MAX_ELEMENT || !rd(ctx, at + have, len - have, &rest, &g2, &total) || g2 < len - have) { free(b); fail(err, en, "the keyframe could not be read"); return NULL; }
                        frame = malloc(len + 64);
                        memcpy(frame, blk + tl + 3, have);
                        memcpy(frame + have, rest, len - have);
                        free(rest);
                    }
                    memset(frame + len, 0, 64);
                    free(b);
                    *n = (size_t)len;
                    return frame;
                }
            }
        }
        if (size > (uint64_t)(end - p - eh)) break;
        p += eh + size;
    }
    free(b);
    fail(err, en, "no keyframe in the first megabyte of the cluster");
    return NULL;
}

/* ---- MP4 ----------------------------------------------------------------------- */

static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
static uint64_t be64(const uint8_t *p) { return (uint64_t)be32(p) << 32 | be32(p + 4); }

/* A child box of [p, end) with this type: its payload and size, or NULL. */
static const uint8_t *box(const uint8_t *p, const uint8_t *end, const char *type, uint64_t *size)
{
    while (end - p >= 8) {
        uint64_t s = be32(p);
        int h = 8;
        if (s == 1) { if (end - p < 16) return NULL; s = be64(p + 8); h = 16; }
        else if (s == 0) s = (uint64_t)(end - p);
        if (s < (uint64_t)h || s > (uint64_t)(end - p)) return NULL;
        if (!memcmp(p + 4, type, 4)) { *size = s - h; return p + h; }
        p += s;
    }
    return NULL;
}

typedef struct { const uint8_t *p; uint32_t n; } Table;

static Table table(const uint8_t *stbl, const uint8_t *end, const char *type, int header)
{
    uint64_t s;
    const uint8_t *b = box(stbl, end, type, &s);
    Table t = { NULL, 0 };
    if (b && s >= (uint64_t)header) { t.n = be32(b + header - 4); t.p = b + header; }
    return t;
}

static int mp4_track(Index *ix, const uint8_t *moov, const uint8_t *mend)
{
    for (const uint8_t *p = moov; mend - p >= 8; ) {
        uint64_t s = be32(p);
        if (s < 8 || s > (uint64_t)(mend - p)) break;
        if (!memcmp(p + 4, "trak", 4)) {
            const uint8_t *trak = p + 8, *tend = p + s;
            uint64_t ms, hs, ns, ss;
            const uint8_t *mdia = box(trak, tend, "mdia", &ms);
            const uint8_t *hdlr = mdia ? box(mdia, mdia + ms, "hdlr", &hs) : NULL;
            if (hdlr && hs >= 12 && !memcmp(hdlr + 8, "vide", 4)) {
                const uint8_t *mdhd = box(mdia, mdia + ms, "mdhd", &ns);
                double timescale = mdhd && ns >= 24 ? (mdhd[0] == 1 ? be32(mdhd + 20) : be32(mdhd + 12)) : 0;
                const uint8_t *minf = box(mdia, mdia + ms, "minf", &ns);
                const uint8_t *stbl = minf ? box(minf, minf + ns, "stbl", &ss) : NULL;
                if (!stbl || timescale <= 0) { snprintf(ix->err, sizeof ix->err, "the video track has no sample tables"); return 0; }
                const uint8_t *send = stbl + ss;
                uint64_t ds;
                const uint8_t *stsd = box(stbl, send, "stsd", &ds);
                if (!stsd || ds < 16 + 78) { snprintf(ix->err, sizeof ix->err, "no sample description"); return 0; }
                const uint8_t *entry = stsd + 8;   /* version/flags, count */
                uint64_t es = be32(entry);
                if (es < 8 + 78 || es > ds - 8) { snprintf(ix->err, sizeof ix->err, "bad sample description"); return 0; }
                const char *fmt = (const char *)entry + 4;
                ix->codec = !memcmp(fmt, "avc1", 4) || !memcmp(fmt, "avc3", 4) ? V_H264 : !memcmp(fmt, "hvc1", 4) || !memcmp(fmt, "hev1", 4) ? V_HEVC : V_NONE;
                if (!ix->codec) { snprintf(ix->err, sizeof ix->err, "no previews for %.4s video", fmt); return 0; }
                uint64_t cs;
                const uint8_t *conf = box(entry + 8 + 78, entry + es, ix->codec == V_H264 ? "avcC" : "hvcC", &cs);
                if (!conf) { snprintf(ix->err, sizeof ix->err, "the video track has no codec header"); return 0; }
                set_extra(ix, conf, (size_t)cs);
                Table stts = table(stbl, send, "stts", 8), stss = table(stbl, send, "stss", 8), stsc = table(stbl, send, "stsc", 8);
                Table stco = table(stbl, send, "stco", 8), co64 = table(stbl, send, "co64", 8);
                uint64_t zs;
                const uint8_t *stsz = box(stbl, send, "stsz", &zs);
                if (!stts.p || !stsc.p || (!stco.p && !co64.p) || !stsz || zs < 12) { snprintf(ix->err, sizeof ix->err, "incomplete sample tables"); return 0; }
                uint32_t fixed = be32(stsz + 4), n_samples = be32(stsz + 8);
                if (!fixed && zs < 12 + 4ull * n_samples) { snprintf(ix->err, sizeof ix->err, "incomplete sample sizes"); return 0; }
                uint32_t n_chunks = stco.p ? stco.n : co64.n;
                /* one pass over the samples: time (stts), chunk and offset (stsc, stco), sync (stss) */
                uint32_t sample = 0, tt = 0, tt_left = stts.n ? be32(stts.p) : 0, sync = 0, sc = 0;
                uint64_t dts = 0;
                for (uint32_t chunk = 0; chunk < n_chunks && sample < n_samples; chunk++) {
                    while (sc + 1 < stsc.n && be32(stsc.p + 12 * (sc + 1)) <= chunk + 1) sc++;
                    uint32_t per = be32(stsc.p + 12 * sc + 4);
                    uint64_t off = stco.p ? be32(stco.p + 4 * chunk) : be64(co64.p + 8 * chunk);
                    for (uint32_t i = 0; i < per && sample < n_samples; i++, sample++) {
                        uint32_t size = fixed ? fixed : be32(stsz + 12 + 4 * sample);
                        int is_sync = !stss.p;
                        while (stss.p && sync < stss.n && be32(stss.p + 4 * sync) < sample + 1) sync++;
                        if (stss.p && sync < stss.n && be32(stss.p + 4 * sync) == sample + 1) is_sync = 1;
                        if (is_sync && !add_key(ix, (double)dts / timescale, off, size)) break;
                        off += size;
                        while (tt < stts.n && !tt_left) { tt++; tt_left = tt < stts.n ? be32(stts.p + 8 * tt) : 0; }
                        if (tt < stts.n) { dts += be32(stts.p + 8 * tt + 4); tt_left--; }
                    }
                }
                if (!ix->n_keys) { snprintf(ix->err, sizeof ix->err, "no keyframes in the sample tables"); return 0; }
                ix->container = C_MP4;
                return 1;
            }
        }
        p += s;
    }
    snprintf(ix->err, sizeof ix->err, "no video track found");
    return 0;
}

static int mp4_index(FramesRead rd, void *ctx, Index *ix, const uint8_t *b, size_t got)
{
    uint64_t off = 0, total = 0;
    for (int boxes = 0; boxes < 32; boxes++) {   /* the top-level boxes, reading only headers past the buffer */
        uint8_t hb[16]; const uint8_t *hp;
        uint8_t *tmp = NULL;
        if (off + 16 <= got) hp = b + off;
        else {
            size_t g; uint64_t tt;
            if (!rd(ctx, off, 16, &tmp, &g, &tt) || g < 8) { free(tmp); break; }
            memset(hb, 0, sizeof hb); memcpy(hb, tmp, g < 16 ? g : 16); free(tmp); tmp = NULL;
            hp = hb;
            if (tt) total = tt;
        }
        uint64_t s = be32(hp);
        if (s == 1) s = be64(hp + 8);
        else if (s == 0) break;
        if (s < 8) break;
        if (!memcmp(hp + 4, "moov", 4)) {
            if (s > MAX_ELEMENT) { snprintf(ix->err, sizeof ix->err, "the index is too large"); return 0; }
            const uint8_t *moov; uint8_t *own = NULL;
            if (off + s <= got) moov = b + off;
            else {
                size_t g; uint64_t tt;
                if (!rd(ctx, off, s, &own, &g, &tt) || g < s) { free(own); snprintf(ix->err, sizeof ix->err, "the index could not be read"); return 0; }
                moov = own;
            }
            int hdr = be32(moov) == 1 ? 16 : 8;
            int ok = mp4_track(ix, moov + hdr, moov + s);
            free(own);
            return ok;
        }
        if (!memcmp(hp + 4, "moof", 4)) { snprintf(ix->err, sizeof ix->err, "fragmented MP4 has no index up front"); return 0; }
        off += s;
        if (total && off >= total) break;
    }
    snprintf(ix->err, sizeof ix->err, "no MP4 index (moov) found");
    return 0;
}

/* ---- decoding ---------------------------------------------------------------- */

static void put16(uint8_t *p, unsigned v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put32(uint8_t *p, unsigned v) { put16(p, v & 0xFFFF); put16(p + 2, v >> 16); }

static uint8_t *bmp_new(int w, int h, size_t *len, int *stride)
{
    *stride = (w * 3 + 3) & ~3;
    *len = 54 + (size_t)*stride * h;
    uint8_t *b = calloc(1, *len);
    b[0] = 'B'; b[1] = 'M';
    put32(b + 2, (unsigned)*len); put32(b + 10, 54); put32(b + 14, 40);
    put32(b + 18, (unsigned)w); put32(b + 22, (unsigned)h);
    put16(b + 26, 1); put16(b + 28, 24); put32(b + 34, (unsigned)(*len - 54));
    return b;
}

#ifdef SNTV_HAVE_FFMPEG
static inline uint8_t clamp8(float v) { return v < 0 ? 0 : v > 255 ? 255 : (uint8_t)(v + 0.5f); }

/* Area-averaged luma and nearest chroma into a small RGB picture: BT.709 for HD, BT.601
 * for SD, BT.2020 for HDR, limited or full range, 8 or 10 bits. PQ/HLG gets a simple
 * lift so HDR frames do not look grey. */
static uint8_t *to_bmp(const AVFrame *f, int max_w, size_t *len, char *err, size_t en)
{
    int deep;
    switch (f->format) {
    case AV_PIX_FMT_YUV420P: case AV_PIX_FMT_YUVJ420P: deep = 0; break;
    case AV_PIX_FMT_YUV420P10LE: deep = 1; break;
    default: fail(err, en, "unsupported picture format"); return NULL;
    }
    int w = f->width < max_w ? f->width : max_w, h = (int)((double)w * f->height / f->width + 0.5);
    if (w < 2 || h < 2) { fail(err, en, "empty picture"); return NULL; }
    float kr = 0.2126f, kb = 0.0722f;
    if (f->colorspace == AVCOL_SPC_BT2020_NCL || f->colorspace == AVCOL_SPC_BT2020_CL) { kr = 0.2627f; kb = 0.0593f; }
    else if (f->colorspace == AVCOL_SPC_BT470BG || f->colorspace == AVCOL_SPC_SMPTE170M || (f->colorspace == AVCOL_SPC_UNSPECIFIED && f->height < 720)) { kr = 0.299f; kb = 0.114f; }
    int full = f->color_range == AVCOL_RANGE_JPEG || f->format == AV_PIX_FMT_YUVJ420P;
    int hdr = f->color_trc == AVCOL_TRC_SMPTE2084 || f->color_trc == AVCOL_TRC_ARIB_STD_B67;
    float ys = full ? 1.0f : 255.0f / 219.0f, yo = full ? 0 : 16, cs = full ? 1.0f : 255.0f / 224.0f;
    int stride;
    uint8_t *bmp = bmp_new(w, h, len, &stride);
    for (int oy = 0; oy < h; oy++) {
        int y0 = oy * f->height / h, y1 = (oy + 1) * f->height / h;
        if (y1 <= y0) y1 = y0 + 1;
        uint8_t *row = bmp + 54 + (size_t)stride * (h - 1 - oy);   /* bottom-up */
        for (int ox = 0; ox < w; ox++) {
            int x0 = ox * f->width / w, x1 = (ox + 1) * f->width / w;
            if (x1 <= x0) x1 = x0 + 1;
            unsigned sum = 0, cnt = 0;
            int step = (x1 - x0) > 8 ? 2 : 1;   /* 4K: every other sample is plenty */
            for (int sy = y0; sy < y1; sy += step)
                for (int sx = x0; sx < x1; sx += step, cnt++)
                    sum += deep ? ((const uint16_t *)(f->data[0] + (size_t)sy * f->linesize[0]))[sx] >> 2 : f->data[0][(size_t)sy * f->linesize[0] + sx];
            int cx = ((x0 + x1) / 2) >> 1, cy = ((y0 + y1) / 2) >> 1;
            float u, v;
            if (deep) { u = (float)(((const uint16_t *)(f->data[1] + (size_t)cy * f->linesize[1]))[cx] >> 2); v = (float)(((const uint16_t *)(f->data[2] + (size_t)cy * f->linesize[2]))[cx] >> 2); }
            else { u = f->data[1][(size_t)cy * f->linesize[1] + cx]; v = f->data[2][(size_t)cy * f->linesize[2] + cx]; }
            float Y = ((float)sum / cnt - yo) * ys, U = (u - 128) * cs, V = (v - 128) * cs;
            float r = Y + 2 * (1 - kr) * V, b = Y + 2 * (1 - kb) * U, g = (Y - kr * r - kb * b) / (1 - kr - kb);
            if (hdr) { r = 255 * powf(r < 0 ? 0 : r / 255, 0.6f); g = 255 * powf(g < 0 ? 0 : g / 255, 0.6f); b = 255 * powf(b < 0 ? 0 : b / 255, 0.6f); }
            row[ox * 3] = clamp8(b); row[ox * 3 + 1] = clamp8(g); row[ox * 3 + 2] = clamp8(r);
        }
    }
    return bmp;
}

static uint8_t *decode(const Index *ix, const uint8_t *data, size_t n, int max_w, size_t *len, char *err, size_t en)
{
#ifdef SNTV_FFMPEG_DLOPEN
    if (!ff_load()) { fail(err, en, "FFmpeg is not there"); return NULL; }
#endif
    const AVCodec *codec = avcodec_find_decoder(ix->codec == V_HEVC ? AV_CODEC_ID_HEVC : AV_CODEC_ID_H264);
    AVCodecContext *c = codec ? avcodec_alloc_context3(codec) : NULL;
    if (!c) { fail(err, en, "no decoder"); return NULL; }
    c->extradata = av_mallocz(ix->n_extra + AV_INPUT_BUFFER_PADDING_SIZE);
    memcpy(c->extradata, ix->extra, ix->n_extra);
    c->extradata_size = (int)ix->n_extra;
    c->thread_count = 2;
    c->thread_type = FF_THREAD_SLICE;        /* one picture: frame threads only add delay */
    c->skip_loop_filter = AVDISCARD_ALL;     /* faster, and invisible at preview size */
    c->flags2 |= AV_CODEC_FLAG2_FAST;
    uint8_t *out = NULL;
    AVPacket *pkt = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();
    if (avcodec_open2(c, codec, NULL) < 0) fail(err, en, "the decoder did not open");
    else if (av_new_packet(pkt, (int)n) < 0) fail(err, en, "out of memory");
    else {
        memcpy(pkt->data, data, n);
        pkt->flags = AV_PKT_FLAG_KEY;
        int rc = avcodec_send_packet(c, pkt);
        avcodec_send_packet(c, NULL);   /* end of stream: the picture comes out now */
        if (rc < 0) fail(err, en, "the keyframe did not decode");
        else if (avcodec_receive_frame(c, frame) < 0) fail(err, en, "no picture from the keyframe");
        else out = to_bmp(frame, max_w, len, err, en);
    }
    av_frame_free(&frame);
    av_packet_free(&pkt);
    avcodec_free_context(&c);
    return out;
}
#endif

/* ---- the index cache and the entry points --------------------------------------- */

static Index *index_for(FramesRead rd, void *ctx, const char *key)
{
    for (int i = 0; i < CACHED; i++) if (g_cache[i].key[0] && !strcmp(g_cache[i].key, key)) return &g_cache[i];
    Index *ix = &g_cache[g_next];
    g_next = (g_next + 1) % CACHED;
    free(ix->extra); free(ix->keys);
    memset(ix, 0, sizeof *ix);
    snprintf(ix->key, sizeof ix->key, "%s", key);
    uint8_t *b; size_t got; uint64_t total;
    Uint32 t0 = SDL_GetTicks();
    if (!rd(ctx, 0, HEAD_BYTES, &b, &got, &total) || got < 16) {
        if (got) free(b);
        snprintf(ix->err, sizeof ix->err, "the video did not answer");
        ix->key[0] = 0;   /* a network hiccup: try again next time */
        return ix;
    }
    if (be32(b) == 0x1A45DFA3) mkv_index(rd, ctx, ix, b, got);
    else if (!memcmp(b + 4, "ftyp", 4) || !memcmp(b + 4, "moov", 4) || !memcmp(b + 4, "free", 4)) mp4_index(rd, ctx, ix, b, got);
    else snprintf(ix->err, sizeof ix->err, "not a Matroska or MP4 file");
    free(b);
    if (ix->n_keys) LOGI("frames: %d keyframes indexed in %u ms (%s, %s)", ix->n_keys, SDL_GetTicks() - t0,
                         ix->container == C_MKV ? "Matroska" : "MP4", ix->codec == V_HEVC ? "HEVC" : "H.264");
    else LOGW("frames: no previews: %s", ix->err);
    return ix;
}

uint8_t *frames_picture(FramesRead rd, void *ctx, const char *key, double t, int max_w, size_t *len, char *err, size_t en)
{
#ifndef SNTV_HAVE_FFMPEG
    (void)rd; (void)ctx; (void)key; (void)t; (void)max_w; (void)len;
    fail(err, en, "built without a decoder");
    return NULL;
#else
    Index *ix = index_for(rd, ctx, key);
    if (!ix->n_keys) { fail(err, en, ix->err); return NULL; }
    int lo = 0;   /* the last keyframe at or before t */
    for (int a = 0, b = ix->n_keys - 1; a <= b; ) { int m = (a + b) / 2; if (ix->keys[m].t <= t + 0.05) { lo = m; a = m + 1; } else b = m - 1; }
    const Key *k = &ix->keys[lo];
    size_t n = 0;
    uint8_t *data = NULL;
    if (ix->container == C_MP4) {
        size_t got; uint64_t total;
        if (k->size > MAX_ELEMENT || !rd(ctx, k->pos, k->size, &data, &got, &total) || got < k->size) { free(data); fail(err, en, "the keyframe could not be read"); return NULL; }
        data = realloc(data, k->size + 64);
        memset(data + k->size, 0, 64);
        n = k->size;
    } else if (!(data = mkv_keyframe(rd, ctx, ix, k, &n, err, en))) return NULL;
    uint8_t *bmp = decode(ix, data, n, max_w, len, err, en);
    free(data);
    return bmp;
#endif
}

/* ---- over HTTP ---------------------------------------------------------------- */

static volatile int g_stop;
void frames_stop(void) { g_stop = 1; }

static void *g_conn;   /* one connection kept open for the reads (they all come from the frames thread) */

static int http_read(void *ctx, uint64_t off, uint64_t len, uint8_t **buf, size_t *got, uint64_t *total)
{
    HttpResponse r;
    HttpOptions o = { .range_start = off, .range_bytes = len, .timeout_s = 20, .no_json_accept = 1, .stop = &g_stop, .keep = &g_conn };
    http_request_ex((const char *)ctx, &o, &r);
    *buf = NULL; *got = 0; *total = r.total_size;
    if (r.status != 206 && !(r.status == 200 && off == 0)) { http_response_free(&r); return 0; }
    if (r.status == 200 && r.len > len) r.len = (size_t)len;   /* no Range support: keep what was asked */
    *buf = (uint8_t *)r.body; *got = r.len;
    r.body = NULL;
    return 1;
}

int frames_keyframes_url(const char *url, double from, double *out, int max)
{
    Index *ix = index_for(http_read, (void *)url, url);
    int n = 0;
    for (int i = 0; i < ix->n_keys && n < max; i++) if (ix->keys[i].t > from) out[n++] = ix->keys[i].t;
    return ix->n_keys ? n : -1;
}

uint8_t *frames_picture_url(const char *url, double t, int max_w, size_t *len, char *err, size_t en)
{
    Uint32 t0 = SDL_GetTicks();
    uint8_t *bmp = frames_picture(http_read, (void *)url, url, t, max_w, len, err, en);
    const char *host = strstr(url, "://");
    host = host ? host + 3 : url;
    if (bmp) LOGI("frames: %.0f s of %.*s in %u ms", t, (int)strcspn(host, "/:?"), host, SDL_GetTicks() - t0);
    return bmp;
}
