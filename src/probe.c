#include "probe.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>

/* ---- Matroska --------------------------------------------------------------- */

typedef struct { const uint8_t *p, *end; } Cur;

static uint64_t vint(Cur *c, int keep_marker, int *len_out, int *unknown)
{
    if (c->p >= c->end) { *len_out = 0; return 0; }
    uint8_t b = *c->p;
    int len = 1;
    while (len <= 8 && !(b & (0x80 >> (len - 1)))) len++;
    if (len > 8 || c->p + len > c->end) { *len_out = 0; return 0; }
    uint64_t v = keep_marker ? b : (uint64_t)(b & (0xFF >> len));
    int all_ones = (b & (0xFF >> len)) == (0xFF >> len);
    for (int i = 1; i < len; i++) { v = (v << 8) | c->p[i]; if (c->p[i] != 0xFF) all_ones = 0; }
    c->p += len;
    *len_out = len;
    if (unknown) *unknown = !keep_marker && all_ones;
    return v;
}

static uint64_t uint_at(const uint8_t *p, size_t n)
{
    uint64_t v = 0;
    for (size_t i = 0; i < n && i < 8; i++) v = (v << 8) | p[i];
    return v;
}

static double float_at(const uint8_t *p, size_t n)
{
    if (n == 4) { uint32_t u = (uint32_t)uint_at(p, 4); float f; memcpy(&f, &u, 4); return f; }
    if (n == 8) { uint64_t u = uint_at(p, 8); double d; memcpy(&d, &u, 8); return d; }
    return 0;
}

static void copy_str(char *dst, size_t dn, const uint8_t *p, size_t n)
{
    if (n >= dn) n = dn - 1;
    memcpy(dst, p, n);
    dst[n] = 0;
}

enum {
    ID_EBML = 0x1A45DFA3, ID_SEGMENT = 0x18538067, ID_INFO = 0x1549A966, ID_TIMESCALE = 0x2AD7B1, ID_DURATION = 0x4489,
    ID_TRACKS = 0x1654AE6B, ID_TRACKENTRY = 0xAE, ID_TRACKTYPE = 0x83, ID_CODECID = 0x86, ID_LANG = 0x22B59C, ID_LANGIETF = 0x22B59D,
    ID_NAME = 0x536E, ID_FORCED = 0x55AA, ID_DEFAULT = 0x88, ID_DEFDURATION = 0x23E383, ID_VIDEO = 0xE0, ID_PIXW = 0xB0, ID_PIXH = 0xBA,
    ID_COLOUR = 0x55B0, ID_TRANSFER = 0x55BA, ID_BITS = 0x55B2, ID_AUDIO = 0xE1, ID_CHANNELS = 0x9F, ID_BLOCKADDMAP = 0x41E4, ID_BLOCKADDTYPE = 0x41E7,
    ID_CHAPTERS = 0x1043A770, ID_EDITION = 0x45B9, ID_ATOM = 0xB6, ID_CHAPSTART = 0x91, ID_CHAPDISPLAY = 0x80, ID_CHAPSTRING = 0x85,
    ID_CLUSTER = 0x1F43B675, ID_CUES = 0x1C53BB6B
};

typedef struct { ProbeInfo *out; double timescale; ProbeTrack *track; int track_video_w, track_video_h; int track_dv, track_hdr, track_bits; double track_fps; int track_type; } MkvCtx;

static void mkv_walk(MkvCtx *x, Cur c, int depth);

static void mkv_element(MkvCtx *x, uint32_t id, const uint8_t *p, size_t n, int depth)
{
    ProbeInfo *o = x->out;
    Cur sub = { p, p + n };
    switch (id) {
    case ID_SEGMENT: case ID_INFO: case ID_TRACKS: case ID_VIDEO: case ID_AUDIO: case ID_COLOUR: case ID_BLOCKADDMAP:
    case ID_CHAPTERS: case ID_EDITION: case ID_CHAPDISPLAY:
        mkv_walk(x, sub, depth + 1);
        break;
    case ID_TRACKENTRY: {
        if (o->n_tracks >= PROBE_MAX_TRACKS) break;
        ProbeTrack *t = &o->tracks[o->n_tracks];
        memset(t, 0, sizeof *t);
        x->track = t; x->track_video_w = x->track_video_h = 0; x->track_dv = 0; x->track_hdr = 0; x->track_bits = 0; x->track_fps = 0; x->track_type = 0;
        mkv_walk(x, sub, depth + 1);
        t->kind = x->track_type == 1 ? 1 : x->track_type == 2 ? 2 : x->track_type == 17 ? 3 : 0;
        if (t->kind == 1 && !o->width) {
            o->width = x->track_video_w; o->height = x->track_video_h; o->fps = x->track_fps; o->bit_depth = x->track_bits;
            o->hdr = x->track_dv ? 2 : x->track_hdr ? 1 : 0;
            const char *cid = t->codec;
            o->vcodec = strstr(cid, "HEVC") ? CODEC_HEVC : strstr(cid, "AV1") ? CODEC_AV1 : strstr(cid, "AVC") ? CODEC_AVC :
                        (strstr(cid, "MPEG4") || strstr(cid, "MPEG2") || strstr(cid, "VC1") || strstr(cid, "MS/VFW")) ? CODEC_OLD : CODEC_UNKNOWN;
        }
        if (t->kind) o->n_tracks++;
        x->track = NULL;
        break;
    }
    case ID_ATOM: {
        if (o->n_chapters >= PROBE_MAX_CHAPTERS) break;
        ProbeChapter *ch = &o->chapters[o->n_chapters];
        memset(ch, 0, sizeof *ch);
        /* ChapterTimeStart and the display name live directly inside the atom */
        Cur a = sub;
        while (a.p < a.end) {
            int l1, l2, unk;
            uint32_t cid = (uint32_t)vint(&a, 1, &l1, NULL);
            uint64_t sz = vint(&a, 0, &l2, &unk);
            if (!l1 || !l2 || a.p + sz > a.end) break;
            if (cid == ID_CHAPSTART) ch->start_s = (double)uint_at(a.p, sz) / 1e9;
            else if (cid == ID_CHAPDISPLAY) {
                Cur d = { a.p, a.p + sz };
                while (d.p < d.end) {
                    int m1, m2;
                    uint32_t did = (uint32_t)vint(&d, 1, &m1, NULL);
                    uint64_t dsz = vint(&d, 0, &m2, NULL);
                    if (!m1 || !m2 || d.p + dsz > d.end) break;
                    if (did == ID_CHAPSTRING && !ch->name[0]) copy_str(ch->name, sizeof ch->name, d.p, dsz);
                    d.p += dsz;
                }
            }
            a.p += sz;
        }
        o->n_chapters++;
        break;
    }
    case ID_TIMESCALE: x->timescale = (double)uint_at(p, n); break;
    case ID_DURATION: o->duration_s = float_at(p, n) * (x->timescale > 0 ? x->timescale : 1e6) / 1e9; break;
    case ID_TRACKTYPE: x->track_type = (int)uint_at(p, n); break;
    case ID_CODECID: if (x->track) copy_str(x->track->codec, sizeof x->track->codec, p, n); break;
    case ID_LANG: if (x->track && !x->track->lang[0]) copy_str(x->track->lang, sizeof x->track->lang, p, n); break;
    case ID_LANGIETF: if (x->track) copy_str(x->track->lang, sizeof x->track->lang, p, n); break;
    case ID_NAME: if (x->track) copy_str(x->track->name, sizeof x->track->name, p, n); break;
    case ID_FORCED: if (x->track) x->track->forced = uint_at(p, n) != 0; break;
    case ID_DEFAULT: if (x->track) x->track->dflt = uint_at(p, n) != 0; break;
    case ID_DEFDURATION: if (uint_at(p, n)) x->track_fps = 1e9 / (double)uint_at(p, n); break;
    case ID_PIXW: x->track_video_w = (int)uint_at(p, n); break;
    case ID_PIXH: x->track_video_h = (int)uint_at(p, n); break;
    case ID_TRANSFER: { int tc = (int)uint_at(p, n); if (tc == 16 || tc == 18) x->track_hdr = 1; break; }   /* PQ or HLG */
    case ID_BITS: x->track_bits = (int)uint_at(p, n); break;
    case ID_CHANNELS: if (x->track) x->track->channels = (int)uint_at(p, n); break;
    case ID_BLOCKADDTYPE: { uint64_t v = uint_at(p, n); if (v == 0x64766343 || v == 0x64767643) x->track_dv = 1; break; }   /* 'dvcC' / 'dvvC' */
    default: break;
    }
}

static void mkv_walk(MkvCtx *x, Cur c, int depth)
{
    if (depth > 8) return;
    while (c.p < c.end) {
        int l1, l2, unknown = 0;
        uint32_t id = (uint32_t)vint(&c, 1, &l1, NULL);
        uint64_t size = vint(&c, 0, &l2, &unknown);
        if (!l1 || !l2) return;
        if (id == ID_CLUSTER) return;                         /* media data: the header is over */
        size_t avail = (size_t)(c.end - c.p);
        size_t n = unknown || size > avail ? avail : (size_t)size;
        int truncated = !unknown && size > avail;
        if (id == ID_CUES && truncated) return;
        mkv_element(x, id, c.p, n, depth);
        if (truncated && id != ID_SEGMENT) return;             /* a leaf cut off by the window: stop */
        c.p += n;
    }
}

static int parse_mkv(const uint8_t *d, size_t len, ProbeInfo *o)
{
    MkvCtx x = { o, 1e6, NULL, 0, 0, 0, 0, 0, 0, 0 };
    Cur c = { d, d + len };
    mkv_walk(&x, c, 0);
    return o->n_tracks > 0;
}

/* ---- MP4 -------------------------------------------------------------------- */

static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
static uint64_t be64(const uint8_t *p) { return (uint64_t)be32(p) << 32 | be32(p + 4); }
static uint16_t be16(const uint8_t *p) { return (uint16_t)(p[0] << 8 | p[1]); }

typedef struct { ProbeInfo *out; int handler; uint32_t mdhd_scale; } Mp4Ctx;

static void mp4_walk(Mp4Ctx *x, const uint8_t *p, const uint8_t *end, int depth);

static void mp4_sample_entries(Mp4Ctx *x, const uint8_t *p, const uint8_t *end)
{
    ProbeInfo *o = x->out;
    if (end - p < 8) return;
    uint32_t count = be32(p + 4);
    p += 8;
    for (uint32_t i = 0; i < count && p + 16 <= end; i++) {
        uint32_t sz = be32(p);
        if (sz < 16 || p + sz > end) sz = (uint32_t)(end - p);
        char fourcc[5];
        copy_str(fourcc, sizeof fourcc, p + 4, 4);
        if (o->n_tracks < PROBE_MAX_TRACKS) {
            ProbeTrack *t = &o->tracks[o->n_tracks];
            memset(t, 0, sizeof *t);
            t->kind = x->handler;
            snprintf(t->codec, sizeof t->codec, "%s", fourcc);
            if (x->handler == 1 && sz >= 34) {
                int w = be16(p + 32), h = be16(p + 34);
                int dv = !strncmp(fourcc, "dv", 2);
                if (!o->width) {
                    o->width = w; o->height = h;
                    o->vcodec = !strcmp(fourcc, "hvc1") || !strcmp(fourcc, "hev1") || !strcmp(fourcc, "dvh1") || !strcmp(fourcc, "dvhe") ? CODEC_HEVC :
                                !strcmp(fourcc, "av01") ? CODEC_AV1 :
                                !strcmp(fourcc, "avc1") || !strcmp(fourcc, "avc3") || !strcmp(fourcc, "dva1") || !strcmp(fourcc, "dvav") ? CODEC_AVC :
                                !strcmp(fourcc, "mp4v") ? CODEC_OLD : CODEC_UNKNOWN;
                    if (dv) o->hdr = 2;
                    /* child boxes after the 78-byte visual sample entry: dvcC/dvvC, colr, hvcC */
                    const uint8_t *q = p + 86, *qe = p + sz;
                    while (q + 8 <= qe) {
                        uint32_t bs = be32(q);
                        if (bs < 8 || q + bs > qe) break;
                        if (!memcmp(q + 4, "dvcC", 4) || !memcmp(q + 4, "dvvC", 4)) o->hdr = 2;
                        else if (!memcmp(q + 4, "colr", 4) && bs >= 19 && !memcmp(q + 8, "nclx", 4)) {
                            int tc = be16(q + 14);
                            if ((tc == 16 || tc == 18) && o->hdr == 0) o->hdr = 1;
                        } else if (!memcmp(q + 4, "hvcC", 4) && bs >= 21) {
                            o->bit_depth = 8 + (q[8 + 12] & 7);   /* bitDepthLumaMinus8 */
                        }
                        q += bs;
                    }
                }
            } else if (x->handler == 2 && sz >= 26) {
                t->channels = be16(p + 24);
            }
            o->n_tracks++;
        }
        p += sz;
    }
}

static void mp4_walk(Mp4Ctx *x, const uint8_t *p, const uint8_t *end, int depth)
{
    ProbeInfo *o = x->out;
    if (depth > 8) return;
    while (p + 8 <= end) {
        uint64_t size = be32(p);
        const uint8_t *body = p + 8;
        if (size == 1) { if (p + 16 > end) return; size = be64(p + 8); body = p + 16; }
        if (size == 0) size = (uint64_t)(end - p);
        if (size < 8) return;
        const uint8_t *bend = size > (uint64_t)(end - p) ? end : p + size;
        const uint8_t *type = p + 4;
        if (!memcmp(type, "moov", 4) || !memcmp(type, "trak", 4) || !memcmp(type, "mdia", 4) || !memcmp(type, "minf", 4) || !memcmp(type, "stbl", 4) || !memcmp(type, "udta", 4)) {
            if (!memcmp(type, "trak", 4)) x->handler = 0;
            mp4_walk(x, body, bend, depth + 1);
        } else if (!memcmp(type, "mvhd", 4) && bend - body >= 20) {
            int v = body[0];
            uint32_t scale = v == 1 ? be32(body + 20) : be32(body + 12);
            uint64_t dur = v == 1 ? be64(body + 24) : be32(body + 16);
            if (scale) o->duration_s = (double)dur / scale;
        } else if (!memcmp(type, "hdlr", 4) && bend - body >= 12) {
            x->handler = !memcmp(body + 8, "vide", 4) ? 1 : !memcmp(body + 8, "soun", 4) ? 2 :
                         (!memcmp(body + 8, "subt", 4) || !memcmp(body + 8, "sbtl", 4) || !memcmp(body + 8, "text", 4)) ? 3 : 0;
        } else if (!memcmp(type, "stsd", 4) && x->handler) {
            int before = o->n_tracks;
            mp4_sample_entries(x, body, bend);
            (void)before;
        } else if (!memcmp(type, "stts", 4) && x->handler == 1 && bend - body >= 16 && x->mdhd_scale) {
            uint32_t delta = be32(body + 12);
            if (delta) o->fps = (double)x->mdhd_scale / delta;
        } else if (!memcmp(type, "mdhd", 4) && bend - body >= 20) {
            int v = body[0];
            x->mdhd_scale = v == 1 ? be32(body + 20) : be32(body + 12);
            const uint8_t *lp = v == 1 ? body + 32 : body + 20;
            if (lp + 2 <= bend && o->n_tracks < PROBE_MAX_TRACKS) {
                uint16_t packed = be16(lp);
                char lang[4] = { (char)(((packed >> 10) & 31) + 0x60), (char)(((packed >> 5) & 31) + 0x60), (char)((packed & 31) + 0x60), 0 };
                /* the language belongs to the track whose stsd follows; stash it on the next slot */
                copy_str(o->tracks[o->n_tracks].lang, sizeof o->tracks[0].lang, (const uint8_t *)lang, 3);
            }
        } else if (!memcmp(type, "chpl", 4) && bend - body >= 9) {   /* Nero chapters */
            const uint8_t *q = body + 8;
            int count = q < bend ? *q : 0;
            q++;
            for (int i = 0; i < count && q + 9 <= bend && o->n_chapters < PROBE_MAX_CHAPTERS; i++) {
                ProbeChapter *ch = &o->chapters[o->n_chapters];
                ch->start_s = (double)be64(q) / 1e7;
                int nl = q[8];
                if (q + 9 + nl > bend) break;
                copy_str(ch->name, sizeof ch->name, q + 9, (size_t)nl);
                o->n_chapters++;
                q += 9 + nl;
            }
        } else if (!memcmp(type, "mdat", 4) && !o->n_tracks && depth == 0) {
            o->moov_missing = 1;   /* index after the data: can't be read from the front */
        }
        if (bend >= end) return;
        p = bend;
    }
}

static int parse_mp4(const uint8_t *d, size_t len, ProbeInfo *o)
{
    Mp4Ctx x = { o, 0, 0 };
    mp4_walk(&x, d, d + len, 0);
    if (o->n_tracks) o->moov_missing = 0;
    return o->n_tracks > 0;
}

/* ---- public ----------------------------------------------------------------- */

static AudioCodec audio_from(const char *codec)
{
    if (strstr(codec, "A_DTS") || !strncmp(codec, "dts", 3)) return AUDIO_DTS;
    if (strstr(codec, "TRUEHD") || strstr(codec, "MLP") || !strcmp(codec, "mlpa")) return AUDIO_TRUEHD;
    if (strstr(codec, "A_EAC3") || !strcmp(codec, "ec-3")) return AUDIO_DDP;
    if (strstr(codec, "A_AC3") || !strcmp(codec, "ac-3")) return AUDIO_AC3;
    if (strstr(codec, "A_AAC") || !strcmp(codec, "mp4a")) return AUDIO_AAC;
    if (codec[0]) return AUDIO_OTHER;
    return AUDIO_UNKNOWN;
}

int probe_parse(const uint8_t *data, size_t len, ProbeInfo *out)
{
    memset(out, 0, sizeof *out);
    if (!data || len < 16) return 0;
    int ok = 0;
    if (be32(data) == ID_EBML) { out->container = PROBE_MKV; ok = parse_mkv(data, len, out); }
    else if (!memcmp(data + 4, "ftyp", 4) || !memcmp(data + 4, "moov", 4) || !memcmp(data + 4, "mdat", 4) || !memcmp(data + 4, "free", 4) || !memcmp(data + 4, "wide", 4)) {
        out->container = PROBE_MP4; ok = parse_mp4(data, len, out);
    }
    /* primary audio: the default-flagged track, else the first */
    const ProbeTrack *a = NULL;
    for (int i = 0; i < out->n_tracks; i++) if (out->tracks[i].kind == 2 && (!a || (out->tracks[i].dflt && !a->dflt))) a = &out->tracks[i];
    if (a) { out->acodec = audio_from(a->codec); out->audio_channels = a->channels; }
    return ok;
}

void probe_apply(const ProbeInfo *p, StreamTags *t)
{
    if (!p->n_tracks) return;
    if (p->height >= 2000 || p->width >= 3800) t->res = 2160;
    else if (p->height >= 1400 || p->width >= 2500) t->res = 1440;
    else if (p->height >= 1000 || p->width >= 1900) t->res = 1080;
    else if (p->height >= 700 || p->width >= 1200) t->res = 720;
    else if (p->height > 0) t->res = 480;
    if (p->vcodec) t->codec = p->vcodec;
    if (p->hdr > t->hdr) t->hdr = p->hdr;   /* only up: many files don't say, and silence is not "no HDR" (a DV-only file would pass as SDR) */
    if (p->acodec) t->audio = p->acodec;
    if (p->acodec != AUDIO_TRUEHD) t->atmos = 0;
}

void probe_describe(const ProbeInfo *p, char *out, size_t n)
{
    static const char *CODECS[] = { "video", "H.264", "HEVC", "AV1", "old codec" };
    static const char *AUDIO[] = { "", "AAC", "Dolby Digital", "Dolby Digital+", "DTS", "TrueHD", "other audio" };
    if (!p->n_tracks) { snprintf(out, n, "%s", p->moov_missing ? "MP4 index at the end of the file: can't be checked up front" : "Header not recognised"); return; }
    char res[24] = "";
    if (p->height) snprintf(res, sizeof res, " %dp", p->height >= 2000 ? 2160 : p->height);
    int subs = 0, audios = 0;
    for (int i = 0; i < p->n_tracks; i++) { if (p->tracks[i].kind == 3) subs++; if (p->tracks[i].kind == 2) audios++; }
    int o = snprintf(out, n, "%s%s%s%s%s", CODECS[p->vcodec], res, p->bit_depth > 8 ? " 10-bit" : "",
                     p->hdr == 2 ? " Dolby Vision" : p->hdr == 1 ? " HDR" : "", p->fps > 0 && p->fps > 31 ? " 60 fps" : "");
    if (o < 0) return;
    size_t u = (size_t)o;
    if (p->acodec && u < n) {
        char ch[16] = "";
        if (p->audio_channels >= 8) snprintf(ch, sizeof ch, " 7.1"); else if (p->audio_channels >= 6) snprintf(ch, sizeof ch, " 5.1"); else if (p->audio_channels == 2) snprintf(ch, sizeof ch, " stereo");
        u += (size_t)snprintf(out + u, n - u, "  \xC2\xB7  %s%s%s", AUDIO[p->acodec], ch, audios > 1 ? " (+ more)" : "");
    }
    if (subs && u < n) snprintf(out + u, n - u, "  \xC2\xB7  %d subtitle track%s", subs, subs == 1 ? "" : "s");
}

double probe_mbits(const ProbeInfo *p, uint64_t file_bytes)
{
    if (p->duration_s <= 0 || !file_bytes) return 0;
    return (double)file_bytes * 8.0 / p->duration_s / 1e6;
}
