/* Desktop backend (Windows, Linux): FFmpeg reads and decodes, SDL plays the sound, and the
 * picture is a texture the UI draws where a TV would show its video plane (gfx_video_hole).
 *
 * Three threads per video: one reads packets into two queues (picture, sound), one decodes
 * the picture, one decodes the sound into a FIFO that SDL's audio callback drains. The picture
 * is decoded on the graphics card where it can be (D3D11 or DXVA2 on Windows) and copied back;
 * the common YUV layouts go to the GPU as they are and a shader turns them into colours, the
 * rest become RGBA by swscale. The sound is the clock; without
 * sound, the wall clock from the first frame. A seek or a load bumps a serial number, and
 * whatever carries an older one is dropped on the way. Text subtitles in the file are
 * decoded on the reading thread into cues the player screen draws (video_subtitle_text). */
#include "video.h"
#include "../log.h"
#include "../youtube.h"
#include "../audio.h"
#include "../platform/platform.h"
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/channel_layout.h>
#include <libavutil/hwcontext.h>
#include <libavutil/pixdesc.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libavutil/time.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
#include <GLES2/gl2.h>
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_TRACKS 32
#define QUEUE_BYTES (48 * 1024 * 1024)   /* read ahead at most this much ... */
#define QUEUE_SECONDS 40.0               /* ... or this long */
#define FRAMES 3                         /* decoded pictures waiting to be shown */
#define MAX_PIC_W 2560                   /* wider pictures are scaled down as they are converted */

typedef struct Pkt { AVPacket *p; int serial; struct Pkt *next; } Pkt;
typedef struct {
    Pkt *head, *tail; int n; int64_t bytes; double last_pts; int serial;
    SDL_mutex *m; SDL_cond *c;
} Queue;

typedef struct { uint8_t *rgba; int w, h, cap; AVFrame *yuv; double pts; int serial; } Frame;   /* yuv: planes for the shader, else rgba */
typedef struct { double start, end; char text[512]; } Cue;

typedef struct {
    /* fixed for one video */
    char *url; double start; unsigned gen;
    AVFormatContext *fmt;
    int vi, ai, si;            /* stream indexes (-1 none) */
    AVCodecContext *vdec, *adec, *sdec;
    enum AVPixelFormat hw_fmt; /* the decoder's frames on the graphics card (AV_PIX_FMT_NONE: none) */
    SDL_Thread *reader, *vthread, *athread;
    volatile int abort;
    /* reading */
    Queue vq, aq;
    int serial;                /* bumps on a seek */
    volatile int seek_req; double seek_to;
    volatile int eof;
    volatile int switch_audio; /* a different sound track chosen (stream + 1): the reader moves to it */
    volatile int reopen_audio; /* ... and the sound's thread opens its decoder */
    volatile int text_req;     /* a text track chosen (stream + 2, 1 for none): the reader opens it */
    /* pictures */
    SDL_mutex *fm; SDL_cond *fc;
    Frame fr[FRAMES]; int f_r, f_n;
    struct SwsContext *sws; int sws_w, sws_h, sws_fmt, sws_ow, sws_oh;
    /* sound */
    SDL_AudioDeviceID dev; SDL_AudioSpec spec;
    SDL_mutex *am;
    uint8_t *fifo; int f_cap, f_len, f_off;
    double fifo_pts;            /* the time of the first byte in the FIFO */
    int a_serial;               /* the serial the FIFO's sound belongs to */
    double clock_pts; Uint64 clock_at;   /* the sound's time when the callback last ran, and when */
    int vdone, adone;           /* the decoders drained after the end */
    /* text */
    SDL_mutex *sm;
    Cue cues[256]; int ncue;
} Player;

static Player *P;              /* the video now (one at a time) */
static VideoInfo g_info;
static unsigned g_loads;
static int g_paused, g_paused_on_purpose, g_muted, g_volume = 100;
static double g_rate = 1.0;
static double g_wall_base = -1; static Uint64 g_wall_at;   /* no sound: the wall clock */
static double g_shown_pts = -1;  /* the picture on screen */
static GLuint g_tex; static int g_tex_w, g_tex_h, g_tex_serial = -1; static double g_tex_pts = -2;
static int g_sar_num = 1, g_sar_den = 1;
static VideoTrack g_atracks[MAX_TRACKS], g_ttracks[MAX_TRACKS]; static int g_na, g_nt;
static int g_text_sel = -1;    /* index into g_ttracks, -1 none */
static int g_sub_sync_ms;
static SDL_Window *g_win;
static volatile int g_yuv_ok;  /* the YUV shader built (1) or not (0: swscale for everything) */
static AVBufferRef *g_hw_dev; static enum AVHWDeviceType g_hw_type = AV_HWDEVICE_TYPE_NONE; static int g_hw_tried;

/* ---- packet queues ------------------------------------------------------------------- */

static void q_init(Queue *q) { memset(q, 0, sizeof *q); q->m = SDL_CreateMutex(); q->c = SDL_CreateCond(); q->last_pts = -1; }
static void q_flush(Queue *q)
{
    SDL_LockMutex(q->m);
    for (Pkt *k = q->head, *nx; k; k = nx) { nx = k->next; av_packet_free(&k->p); free(k); }
    q->head = q->tail = NULL; q->n = 0; q->bytes = 0; q->last_pts = -1;
    SDL_CondBroadcast(q->c);
    SDL_UnlockMutex(q->m);
}
static void q_free(Queue *q) { q_flush(q); SDL_DestroyMutex(q->m); SDL_DestroyCond(q->c); }
static void q_put(Queue *q, AVPacket *p, int serial, double pts)   /* takes p (NULL: the end) */
{
    Pkt *k = calloc(1, sizeof *k);
    if (!k) { av_packet_free(&p); return; }
    k->p = p; k->serial = serial;
    SDL_LockMutex(q->m);
    if (q->tail) q->tail->next = k; else q->head = k;
    q->tail = k; q->n++; q->bytes += p ? p->size : 0;
    if (pts >= 0) q->last_pts = pts;
    SDL_CondSignal(q->c);
    SDL_UnlockMutex(q->m);
}
/* Blocks for the next packet; 0 when told to stop. *p NULL is the end of the stream. */
static int q_get(Player *pl, Queue *q, AVPacket **p, int *serial)
{
    SDL_LockMutex(q->m);
    while (!q->head && !pl->abort) SDL_CondWaitTimeout(q->c, q->m, 100);
    if (pl->abort) { SDL_UnlockMutex(q->m); return 0; }
    Pkt *k = q->head;
    q->head = k->next; if (!q->head) q->tail = NULL;
    q->n--; q->bytes -= k->p ? k->p->size : 0;
    SDL_UnlockMutex(q->m);
    *p = k->p; *serial = k->serial; free(k);
    return 1;
}

static double ts(AVStream *st, int64_t t) { return t == AV_NOPTS_VALUE ? -1 : t * av_q2d(st->time_base); }

/* ---- the clock ----------------------------------------------------------------------- */

static double clock_now(void)
{
    Player *pl = P;
    if (!pl) return 0;
    if (pl->ai >= 0 && pl->dev) {
        SDL_LockMutex(pl->am);
        double c = pl->clock_pts;
        Uint64 at = pl->clock_at;
        SDL_UnlockMutex(pl->am);
        if (c < 0) return g_shown_pts >= 0 ? g_shown_pts : pl->start;
        if (!g_paused) c += (double)(SDL_GetPerformanceCounter() - at) / SDL_GetPerformanceFrequency();
        return c;
    }
    if (g_wall_base < 0) return g_shown_pts >= 0 ? g_shown_pts : pl->start;
    return g_wall_base + (g_paused ? 0 : (double)(SDL_GetPerformanceCounter() - g_wall_at) / SDL_GetPerformanceFrequency() * g_rate);
}

static int open_decoder(Player *pl, AVStream *st, AVCodecContext **out, int threads);

/* ---- sound --------------------------------------------------------------------------- */

static void audio_cb(void *user, Uint8 *out, int len)
{
    Player *pl = user;
    SDL_LockMutex(pl->am);
    int bps = pl->spec.channels * 2 * pl->spec.freq;
    int got = 0;
    if (!g_paused && pl->a_serial == pl->serial) {
        got = pl->f_len < len ? pl->f_len : len;
        memcpy(out, pl->fifo + pl->f_off, (size_t)got);
        double first = pl->fifo_pts;
        pl->f_off += got; pl->f_len -= got;
        if (!pl->f_len) pl->f_off = 0;
        pl->fifo_pts += (double)got / bps;
        if (got) {   /* the sound leaving now started at `first`; the device plays it a buffer later */
            pl->clock_pts = first - (double)pl->spec.samples / pl->spec.freq;
            pl->clock_at = SDL_GetPerformanceCounter();
        }
    }
    SDL_UnlockMutex(pl->am);
    if (got && (g_muted || g_volume < 100)) {
        Sint16 *s = (Sint16 *)out;
        int k = g_muted ? 0 : g_volume * 256 / 100;
        for (int i = 0; i < got / 2; i++) s[i] = (Sint16)((s[i] * k) >> 8);
    }
    if (got < len) memset(out + got, 0, (size_t)(len - got));
}

static int audio_open(Player *pl)
{
    SDL_AudioSpec want = { 0 };
    want.freq = 48000; want.format = AUDIO_S16SYS; want.channels = 2; want.samples = 1024;
    want.callback = audio_cb; want.userdata = pl;
    if (!SDL_WasInit(SDL_INIT_AUDIO)) SDL_InitSubSystem(SDL_INIT_AUDIO);
    pl->dev = SDL_OpenAudioDevice(NULL, 0, &want, &pl->spec, 0);
    if (!pl->dev && audio_available()) {   /* a system with one device at a time: the film's sound before the break chime */
        LOGW("video(ffmpeg): sound device busy (%s); the chime gives it up", SDL_GetError());
        audio_shutdown();
        pl->dev = SDL_OpenAudioDevice(NULL, 0, &want, &pl->spec, 0);
    }
    if (!pl->dev) { LOGW("video(ffmpeg): no sound device: %s", SDL_GetError()); return -1; }
    pl->f_cap = pl->spec.freq * pl->spec.channels * 2;   /* a second */
    pl->fifo = malloc((size_t)pl->f_cap);
    if (!pl->fifo) return -1;
    SDL_PauseAudioDevice(pl->dev, 0);
    return 0;
}

static int audio_thread(void *arg)
{
    Player *pl = arg;
    AVFrame *f = av_frame_alloc();
    SwrContext *swr = NULL; int swr_rate = 0, swr_fmt = -1; AVChannelLayout swr_layout = { 0 };
    int serial = -1;
    uint8_t *buf = NULL; int buf_cap = 0;
    while (!pl->abort) {
        AVPacket *p; int ps;
        if (!q_get(pl, &pl->aq, &p, &ps)) break;
        if (pl->reopen_audio) {   /* another sound track: its own decoder, before its first packet */
            avcodec_free_context(&pl->adec);
            if (open_decoder(pl, pl->fmt->streams[pl->ai], &pl->adec, 1) < 0) LOGW("video(ffmpeg): no decoder for that sound track");
            pl->reopen_audio = 0; serial = -1;
        }
        if (!pl->adec) { av_packet_free(&p); continue; }
        if (ps != serial) { avcodec_flush_buffers(pl->adec); serial = ps; }
        if (ps != pl->serial) { av_packet_free(&p); continue; }
        int r = avcodec_send_packet(pl->adec, p);   /* p NULL drains it */
        av_packet_free(&p);
        if (r < 0 && r != AVERROR(EAGAIN) && r != AVERROR_EOF) continue;
        while (!pl->abort && avcodec_receive_frame(pl->adec, f) == 0) {
            if (!swr || f->sample_rate != swr_rate || f->format != swr_fmt || av_channel_layout_compare(&f->ch_layout, &swr_layout)) {
                swr_free(&swr);
                AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
                if (swr_alloc_set_opts2(&swr, &stereo, AV_SAMPLE_FMT_S16, pl->spec.freq, &f->ch_layout, f->format, f->sample_rate, 0, NULL) < 0 || swr_init(swr) < 0) { swr_free(&swr); break; }
                swr_rate = f->sample_rate; swr_fmt = f->format;
                av_channel_layout_uninit(&swr_layout); av_channel_layout_copy(&swr_layout, &f->ch_layout);
            }
            int outn = (int)av_rescale_rnd(swr_get_delay(swr, swr_rate) + f->nb_samples, pl->spec.freq, swr_rate, AV_ROUND_UP);
            if (outn * 4 > buf_cap) { buf_cap = outn * 4; free(buf); buf = malloc((size_t)buf_cap); if (!buf) { buf_cap = 0; break; } }
            uint8_t *o[1] = { buf };
            int n = swr_convert(swr, o, outn, (const uint8_t **)f->extended_data, f->nb_samples);
            double pts = ts(pl->fmt->streams[pl->ai], f->best_effort_timestamp);
            av_frame_unref(f);
            if (n <= 0) continue;
            if (pts >= 0 && pl->seek_to > 0 && pts + (double)n / pl->spec.freq < pl->seek_to) continue;   /* before the seek point */
            int bytes = n * 4;
            for (;;) {   /* room in the FIFO, or wait for the callback to drain it */
                SDL_LockMutex(pl->am);
                if (pl->serial != serial || pl->abort) { SDL_UnlockMutex(pl->am); break; }
                if (pl->a_serial != serial) {   /* the first sound after a load or a seek */
                    pl->a_serial = serial; pl->f_len = 0; pl->f_off = 0;
                    pl->fifo_pts = pts >= 0 ? pts : pl->seek_to; pl->clock_pts = -1;
                }
                if (pl->f_len + bytes <= pl->f_cap / 2) {
                    if (pl->f_off + pl->f_len + bytes > pl->f_cap) { memmove(pl->fifo, pl->fifo + pl->f_off, (size_t)pl->f_len); pl->f_off = 0; }
                    if (!pl->f_len && pts >= 0) pl->fifo_pts = pts;
                    memcpy(pl->fifo + pl->f_off + pl->f_len, buf, (size_t)bytes);
                    pl->f_len += bytes;
                    SDL_UnlockMutex(pl->am);
                    break;
                }
                SDL_UnlockMutex(pl->am);
                SDL_Delay(10);
            }
        }
        if (pl->eof && pl->aq.n == 0) pl->adone = 1;
    }
    free(buf); swr_free(&swr); av_channel_layout_uninit(&swr_layout); av_frame_free(&f);
    return 0;
}

/* ---- pictures ------------------------------------------------------------------------ */

/* The layouts the shader reads: 0 three 8-bit planes, 1 NV12, 2 three 10-bit planes, 3 P010; -1 others */
static int yuv_layout(int fmt)
{
    switch (fmt) {
    case AV_PIX_FMT_YUV420P: case AV_PIX_FMT_YUVJ420P: return 0;
    case AV_PIX_FMT_NV12: return 1;
    case AV_PIX_FMT_YUV420P10LE: return 2;
    case AV_PIX_FMT_P010LE: return 3;
    default: return -1;
    }
}

/* The graphics card's format when it was offered, else the first the processor decodes to
 * (FFmpeg asks again without it when the card turns this video down). */
static enum AVPixelFormat pick_format(AVCodecContext *d, const enum AVPixelFormat *fmts)
{
    Player *pl = d->opaque;
    for (const enum AVPixelFormat *p = fmts; *p != AV_PIX_FMT_NONE; p++) if (*p == pl->hw_fmt) return *p;
    for (const enum AVPixelFormat *p = fmts; *p != AV_PIX_FMT_NONE; p++) {
        const AVPixFmtDescriptor *ds = av_pix_fmt_desc_get(*p);
        if (ds && !(ds->flags & AV_PIX_FMT_FLAG_HWACCEL)) return *p;
    }
    return AV_PIX_FMT_NONE;
}

/* Decoding on the graphics card for this decoder, where the card and FFmpeg both can: one
 * device for the app's life (made on the first video), shared by every decoder. */
static void use_hardware(Player *pl, AVCodecContext *d, const AVCodec *c)
{
#ifdef _WIN32
    static const enum AVHWDeviceType types[] = { AV_HWDEVICE_TYPE_D3D11VA, AV_HWDEVICE_TYPE_DXVA2 };
#else
    static const enum AVHWDeviceType types[] = { AV_HWDEVICE_TYPE_VAAPI };
#endif
    if (getenv("SNTV_NO_HWDEC")) return;
    if (!g_hw_tried) {
        g_hw_tried = 1;
        for (unsigned t = 0; t < sizeof types / sizeof *types && !g_hw_dev; t++) {
            int compiled = 0;   /* an FFmpeg without it says so for every codec */
            for (enum AVHWDeviceType i = AV_HWDEVICE_TYPE_NONE; (i = av_hwdevice_iterate_types(i)) != AV_HWDEVICE_TYPE_NONE;) if (i == types[t]) compiled = 1;
            if (compiled && av_hwdevice_ctx_create(&g_hw_dev, types[t], NULL, NULL, 0) == 0) g_hw_type = types[t];
        }
        LOGI("video(ffmpeg): graphics card decoding: %s", g_hw_dev ? av_hwdevice_get_type_name(g_hw_type) : "none");
    }
    if (!g_hw_dev) return;
    for (int i = 0;; i++) {
        const AVCodecHWConfig *cfg = avcodec_get_hw_config(c, i);
        if (!cfg) return;
        if ((cfg->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX) && cfg->device_type == g_hw_type) {
            d->hw_device_ctx = av_buffer_ref(g_hw_dev);
            pl->hw_fmt = cfg->pix_fmt;
            d->opaque = pl;
            d->get_format = pick_format;
            return;
        }
    }
}

static int video_thread(void *arg)
{
    Player *pl = arg;
    AVFrame *dec = av_frame_alloc(), *back = av_frame_alloc(), *f = dec;   /* back: a picture copied from the graphics card */
    int serial = -1, said_hw = 0;
    while (!pl->abort) {
        AVPacket *p; int ps;
        if (!q_get(pl, &pl->vq, &p, &ps)) break;
        if (ps != serial) { avcodec_flush_buffers(pl->vdec); serial = ps; }
        if (ps != pl->serial) { av_packet_free(&p); continue; }
        int end = p == NULL;
        int r = avcodec_send_packet(pl->vdec, p);
        av_packet_free(&p);
        if (r < 0 && r != AVERROR(EAGAIN) && r != AVERROR_EOF) continue;
        while (!pl->abort && (f = dec, avcodec_receive_frame(pl->vdec, f) == 0)) {
            double pts = ts(pl->fmt->streams[pl->vi], f->best_effort_timestamp);
            if (pl->serial != serial) { av_frame_unref(f); break; }
            if (pts >= 0 && pts < pl->seek_to - 0.05 && pl->seek_to > 0) { av_frame_unref(f); continue; }   /* decoded up to the seek point, not shown */
            AVFrame *pic = f;
            if (pl->hw_fmt != AV_PIX_FMT_NONE && f->format == pl->hw_fmt) {   /* decoded on the graphics card: copied back */
                if (!said_hw) { said_hw = 1; LOGI("video(ffmpeg): decoding on the graphics card (%s)", av_hwdevice_get_type_name(g_hw_type)); }
                av_frame_unref(back);
                if (av_hwframe_transfer_data(back, f, 0) < 0) { av_frame_unref(f); continue; }
                av_frame_copy_props(back, f);
                av_frame_unref(f);
                pic = back;
            }
            if (pic->sample_aspect_ratio.num > 0 && pic->sample_aspect_ratio.den > 0) { g_sar_num = pic->sample_aspect_ratio.num; g_sar_den = pic->sample_aspect_ratio.den; }
            if (g_yuv_ok && yuv_layout(pic->format) >= 0) {   /* the planes as they are: the shader makes the colours */
                SDL_LockMutex(pl->fm);
                while (pl->f_n == FRAMES && !pl->abort && pl->serial == serial) SDL_CondWaitTimeout(pl->fc, pl->fm, 50);
                if (pl->abort || pl->serial != serial) { SDL_UnlockMutex(pl->fm); av_frame_unref(pic); break; }
                Frame *fr = &pl->fr[(pl->f_r + pl->f_n) % FRAMES];
                SDL_UnlockMutex(pl->fm);
                if (!fr->yuv && !(fr->yuv = av_frame_alloc())) { av_frame_unref(pic); continue; }
                av_frame_unref(fr->yuv);
                fr->w = pic->width; fr->h = pic->height; fr->pts = pts; fr->serial = serial;
                av_frame_move_ref(fr->yuv, pic);
                SDL_LockMutex(pl->fm); pl->f_n++; SDL_UnlockMutex(pl->fm);
                continue;
            }
            f = pic;   /* swscale reads it from here on */
            int ow = f->width, oh = f->height;
            if (ow > MAX_PIC_W) { oh = (int)((int64_t)oh * MAX_PIC_W / ow) & ~1; ow = MAX_PIC_W; }
            if (!pl->sws || pl->sws_w != f->width || pl->sws_h != f->height || pl->sws_fmt != f->format || pl->sws_ow != ow) {
                sws_freeContext(pl->sws);
                pl->sws = sws_getContext(f->width, f->height, f->format, ow, oh, AV_PIX_FMT_RGBA, SWS_BILINEAR, NULL, NULL, NULL);
                pl->sws_w = f->width; pl->sws_h = f->height; pl->sws_fmt = f->format; pl->sws_ow = ow; pl->sws_oh = oh;
                if (pl->sws) {   /* the picture's own colours: BT.709 for HD, limited range unless it says full */
                    int cs = f->colorspace == AVCOL_SPC_BT709 || (f->colorspace == AVCOL_SPC_UNSPECIFIED && f->height >= 720) ? SWS_CS_ITU709
                           : f->colorspace == AVCOL_SPC_BT2020_NCL ? SWS_CS_BT2020 : SWS_CS_ITU601;
                    sws_setColorspaceDetails(pl->sws, sws_getCoefficients(cs), f->color_range == AVCOL_RANGE_JPEG, sws_getCoefficients(SWS_CS_DEFAULT), 1, 0, 1 << 16, 1 << 16);
                }
            }
            if (!pl->sws) { av_frame_unref(f); continue; }
            SDL_LockMutex(pl->fm);   /* a free slot, or wait for the screen to show one */
            while (pl->f_n == FRAMES && !pl->abort && pl->serial == serial) SDL_CondWaitTimeout(pl->fc, pl->fm, 50);
            if (pl->abort || pl->serial != serial) { SDL_UnlockMutex(pl->fm); av_frame_unref(f); break; }
            Frame *fr = &pl->fr[(pl->f_r + pl->f_n) % FRAMES];
            SDL_UnlockMutex(pl->fm);
            int need = ow * oh * 4;
            if (fr->cap < need) { free(fr->rgba); fr->rgba = malloc((size_t)need); fr->cap = fr->rgba ? need : 0; }
            if (fr->rgba) {
                uint8_t *dst[1] = { fr->rgba }; int ls[1] = { ow * 4 };
                sws_scale(pl->sws, (const uint8_t *const *)f->data, f->linesize, 0, f->height, dst, ls);
                fr->w = ow; fr->h = oh; fr->pts = pts; fr->serial = serial;
                if (fr->yuv) av_frame_unref(fr->yuv);
                SDL_LockMutex(pl->fm); pl->f_n++; SDL_UnlockMutex(pl->fm);
            }
            av_frame_unref(f);
        }
        if (end) pl->vdone = 1;
    }
    av_frame_free(&dec); av_frame_free(&back);
    return 0;
}

/* ---- text subtitles ------------------------------------------------------------------ */

/* ASS dialogue to plain lines: the text after the eighth comma, {tags} dropped, \N a new line */
static void ass_plain(const char *in, char *out, size_t n)
{
    const char *t = in;
    if (!strncmp(t, "Dialogue:", 9) || strchr(t, ',')) {
        int commas = !strncmp(t, "Dialogue:", 9) ? 9 : 8;
        for (const char *c = t; *c && commas; c++) if (*c == ',' && !--commas) t = c + 1;
    }
    size_t o = 0; int tag = 0;
    for (; *t && o + 2 < n; t++) {
        if (*t == '{') { tag = 1; continue; }
        if (*t == '}') { tag = 0; continue; }
        if (tag) continue;
        if (t[0] == '\\' && (t[1] == 'N' || t[1] == 'n')) { out[o++] = '\n'; t++; continue; }
        if (t[0] == '\\' && t[1] == 'h') { out[o++] = ' '; t++; continue; }
        out[o++] = *t;
    }
    while (o && (out[o - 1] == '\n' || out[o - 1] == '\r' || out[o - 1] == ' ')) o--;
    out[o] = 0;
}

static void subtitle_packet(Player *pl, AVPacket *pk)
{
    AVSubtitle s; int got = 0;
    if (avcodec_decode_subtitle2(pl->sdec, &s, &got, pk) < 0 || !got) return;
    AVStream *st = pl->fmt->streams[pl->si];
    double start = ts(st, pk->pts) + s.start_display_time / 1000.0;
    double end = pk->duration > 0 ? ts(st, pk->pts + pk->duration) : start + (s.end_display_time ? (s.end_display_time - s.start_display_time) / 1000.0 : 4);
    char text[512] = "";
    for (unsigned i = 0; i < s.num_rects; i++) {
        const AVSubtitleRect *r = s.rects[i];
        char line[512] = "";
        if (r->type == SUBTITLE_ASS && r->ass) ass_plain(r->ass, line, sizeof line);
        else if (r->type == SUBTITLE_TEXT && r->text) snprintf(line, sizeof line, "%s", r->text);
        if (*line) { size_t l = strlen(text); snprintf(text + l, sizeof text - l, "%s%s", l ? "\n" : "", line); }
    }
    avsubtitle_free(&s);
    if (!*text || start < 0) return;
    SDL_LockMutex(pl->sm);
    if (pl->ncue == 256) { memmove(pl->cues, pl->cues + 64, sizeof(Cue) * 192); pl->ncue = 192; }
    Cue *c = &pl->cues[pl->ncue++];
    c->start = start; c->end = end; snprintf(c->text, sizeof c->text, "%s", text);
    SDL_UnlockMutex(pl->sm);
}

static int open_decoder(Player *pl, AVStream *st, AVCodecContext **out, int threads)
{
    const AVCodec *c = avcodec_find_decoder(st->codecpar->codec_id);
    if (!c) return -1;
    AVCodecContext *d = avcodec_alloc_context3(c);
    if (!d) return -1;
    avcodec_parameters_to_context(d, st->codecpar);
    d->pkt_timebase = st->time_base;
    if (threads) d->thread_count = 0;   /* as many as the machine has */
    if (pl && st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) { use_hardware(pl, d, c); LOGI("video(ffmpeg): picture by %s%s", c->name, d->hw_device_ctx ? ", the graphics card where it can" : ""); }
    if (avcodec_open2(d, c, NULL) < 0) { avcodec_free_context(&d); return -1; }
    *out = d;
    return 0;
}

static void open_text(Player *pl, int stream)
{
    if (pl->sdec) avcodec_free_context(&pl->sdec);
    pl->si = -1;
    SDL_LockMutex(pl->sm); pl->ncue = 0; SDL_UnlockMutex(pl->sm);
    if (stream >= 0 && open_decoder(pl, pl->fmt->streams[stream], &pl->sdec, 0) == 0) pl->si = stream;
}

/* ---- reading ------------------------------------------------------------------------- */

static int interrupted(void *arg) { return ((Player *)arg)->abort; }

static void set_error(Player *pl, const char *what, int err)
{
    char e[96]; av_strerror(err, e, sizeof e);
    if (pl != P) return;
    snprintf(g_info.error, sizeof g_info.error, "%s: %s", what, e);
    g_info.state = VIDEO_ERROR;
    LOGW("video(ffmpeg): %s", g_info.error);
}

static void list_tracks(Player *pl)
{
    g_na = g_nt = 0;
    for (unsigned i = 0; i < pl->fmt->nb_streams; i++) {
        AVStream *st = pl->fmt->streams[i];
        int audio = st->codecpar->codec_type == AVMEDIA_TYPE_AUDIO, text = st->codecpar->codec_type == AVMEDIA_TYPE_SUBTITLE;
        if (!audio && !text) continue;
        if (text && avcodec_descriptor_get(st->codecpar->codec_id) && !(avcodec_descriptor_get(st->codecpar->codec_id)->props & AV_CODEC_PROP_TEXT_SUB)) continue;   /* pictures (PGS): not drawn here */
        VideoTrack *t = audio ? &g_atracks[g_na] : &g_ttracks[g_nt];
        if ((audio ? g_na : g_nt) >= MAX_TRACKS) continue;
        memset(t, 0, sizeof *t);
        t->index = (int)i;
        AVDictionaryEntry *l = av_dict_get(st->metadata, "language", NULL, 0), *ti = av_dict_get(st->metadata, "title", NULL, 0);
        snprintf(t->lang, sizeof t->lang, "%s", l ? l->value : "");
        snprintf(t->label, sizeof t->label, "%s", ti ? ti->value : l ? l->value : audio ? "Audio" : "Subtitles");
        const AVCodecDescriptor *d = avcodec_descriptor_get(st->codecpar->codec_id);
        snprintf(t->codec, sizeof t->codec, "%s", d ? d->name : "?");
        for (char *c = t->codec; *c; c++) if (*c >= 'a' && *c <= 'z') *c -= 32;
        t->selected = audio ? (int)i == pl->ai : 0;
        if (audio) g_na++; else g_nt++;
    }
    g_text_sel = -1;
    g_info.tracks_version++;
}

static int reader_thread(void *arg)
{
    Player *pl = arg;
    AVDictionary *o = NULL;
    av_dict_set(&o, "user_agent", SNTV_USER_AGENT, 0);
    av_dict_set(&o, "reconnect", "1", 0);
    av_dict_set(&o, "reconnect_streamed", "1", 0);
    av_dict_set(&o, "reconnect_on_network_error", "1", 0);
    av_dict_set(&o, "reconnect_delay_max", "8", 0);
    av_dict_set(&o, "rw_timeout", "20000000", 0);
    pl->fmt = avformat_alloc_context();
    pl->fmt->interrupt_callback.callback = interrupted;
    pl->fmt->interrupt_callback.opaque = pl;
    pl->fmt->probesize = 8 * 1024 * 1024;
    int r = avformat_open_input(&pl->fmt, pl->url, NULL, &o);
    av_dict_free(&o);
    if (r < 0) { pl->fmt = NULL; if (!pl->abort) set_error(pl, "Could not open the video", r); return 0; }
    if ((r = avformat_find_stream_info(pl->fmt, NULL)) < 0) { if (!pl->abort) set_error(pl, "Could not read the video", r); return 0; }
    pl->vi = av_find_best_stream(pl->fmt, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    pl->ai = av_find_best_stream(pl->fmt, AVMEDIA_TYPE_AUDIO, -1, pl->vi, NULL, 0);
    if (pl->vi >= 0 && open_decoder(pl, pl->fmt->streams[pl->vi], &pl->vdec, 1) < 0) {
        LOGW("video(ffmpeg): no decoder for the picture (%s)", avcodec_get_name(pl->fmt->streams[pl->vi]->codecpar->codec_id));
        pl->vi = -1;
    }
    if (pl->ai >= 0 && (open_decoder(pl, pl->fmt->streams[pl->ai], &pl->adec, 1) < 0 || audio_open(pl) < 0)) {
        if (pl->adec) avcodec_free_context(&pl->adec);
        if (pl == P) { snprintf(g_info.warning, sizeof g_info.warning, "This computer can't play this video's sound."); g_info.warning_version++; }
        pl->ai = -1;
    }
    if (pl->vi < 0 && pl->ai < 0) { if (!pl->abort) set_error(pl, "Nothing in it can be played", AVERROR_DECODER_NOT_FOUND); return 0; }
    if (pl == P) {   /* what the info panel shows */
        char vc[24] = "", ac[24] = "";
        if (pl->vi >= 0) { AVCodecParameters *c = pl->fmt->streams[pl->vi]->codecpar; snprintf(vc, sizeof vc, "%s %dx%d", avcodec_get_name(c->codec_id), c->width, c->height); g_info.width = c->width; g_info.height = c->height;
            AVRational fr = pl->fmt->streams[pl->vi]->avg_frame_rate; g_info.fps = fr.den ? (int)(av_q2d(fr) + 0.5) : 0; }
        if (pl->ai >= 0) snprintf(ac, sizeof ac, "%s", avcodec_get_name(pl->fmt->streams[pl->ai]->codecpar->codec_id));
        snprintf(g_info.codec, sizeof g_info.codec, "%s%s%s", vc, *vc && *ac ? " / " : "", ac);
        for (char *c = g_info.codec; *c; c++) if (*c >= 'a' && *c <= 'z') *c -= 32;
        g_info.duration = pl->fmt->duration > 0 ? pl->fmt->duration / (double)AV_TIME_BASE : 0;
        g_info.bitrate = pl->fmt->bit_rate;
        list_tracks(pl);
    }
    for (unsigned i = 0; i < pl->fmt->nb_streams; i++)   /* only what is played is read */
        pl->fmt->streams[i]->discard = (int)i == pl->vi || (int)i == pl->ai || pl->fmt->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_SUBTITLE ? AVDISCARD_DEFAULT : AVDISCARD_ALL;
    if (pl->vi >= 0) pl->vthread = SDL_CreateThread(video_thread, "ff-video", pl);
    if (pl->ai >= 0) pl->athread = SDL_CreateThread(audio_thread, "ff-audio", pl);
    if (pl->start > 0) { pl->seek_to = pl->start; pl->seek_req = 1; }
    LOGI("video(ffmpeg): opened %s (%s)", pl->fmt->iformat ? pl->fmt->iformat->name : "?", g_info.codec);

    AVPacket *pk = av_packet_alloc();
    while (!pl->abort) {
        if (pl->seek_req) {
            int64_t t = (int64_t)(pl->seek_to * AV_TIME_BASE);
            if (pl->fmt->start_time != AV_NOPTS_VALUE) t += pl->fmt->start_time;
            r = avformat_seek_file(pl->fmt, -1, INT64_MIN, t, t, 0);
            if (r < 0) r = avformat_seek_file(pl->fmt, -1, INT64_MIN, t, INT64_MAX, 0);
            pl->serial++;
            q_flush(&pl->vq); q_flush(&pl->aq);
            SDL_LockMutex(pl->fm); pl->f_n = 0; SDL_CondBroadcast(pl->fc); SDL_UnlockMutex(pl->fm);
            SDL_LockMutex(pl->am); pl->f_len = 0; pl->f_off = 0; pl->clock_pts = -1; SDL_UnlockMutex(pl->am);
            SDL_LockMutex(pl->sm); pl->ncue = 0; SDL_UnlockMutex(pl->sm);
            if (pl->sdec) avcodec_flush_buffers(pl->sdec);
            pl->eof = 0; pl->vdone = pl->adone = 0;
            pl->seek_req = 0;
            if (r < 0) LOGW("video(ffmpeg): seek to %.0fs failed", pl->seek_to);
        }
        if (pl->switch_audio) {   /* another sound track, from where the picture is */
            int want = pl->switch_audio - 1;
            pl->fmt->streams[pl->ai]->discard = AVDISCARD_ALL;
            pl->ai = want;
            pl->fmt->streams[want]->discard = AVDISCARD_DEFAULT;
            pl->reopen_audio = 1;
            pl->seek_to = clock_now(); pl->seek_req = 1;
            pl->switch_audio = 0;
            continue;
        }
        if (pl->text_req) {
            open_text(pl, pl->text_req - 2);
            if (pl->si >= 0) { pl->seek_to = clock_now() > 2 ? clock_now() - 2 : 0; pl->seek_req = 1; }   /* the cues before now, read again */
            pl->text_req = 0;
            continue;
        }
        double ahead = 0;
        if (pl->vi >= 0 && pl->vq.last_pts >= 0) ahead = pl->vq.last_pts - clock_now();
        else if (pl->ai >= 0 && pl->aq.last_pts >= 0) ahead = pl->aq.last_pts - clock_now();
        if (pl == P) g_info.buffer_ahead = ahead < 0 ? 0 : ahead;
        if (pl->eof || pl->vq.bytes + pl->aq.bytes > QUEUE_BYTES || ahead > QUEUE_SECONDS) { SDL_Delay(10); continue; }
        r = av_read_frame(pl->fmt, pk);
        if (r < 0) {
            if (pl->abort) break;
            if (r == AVERROR_EOF || avio_feof(pl->fmt->pb)) {   /* the end: drain the decoders */
                if (pl->vi >= 0) q_put(&pl->vq, NULL, pl->serial, -1);
                if (pl->ai >= 0) q_put(&pl->aq, NULL, pl->serial, -1);
                pl->eof = 1;
                continue;
            }
            set_error(pl, "The video stopped", r);
            break;
        }
        AVStream *st = pl->fmt->streams[pk->stream_index];
        double pts = ts(st, pk->pts != AV_NOPTS_VALUE ? pk->pts : pk->dts);
        if (pk->stream_index == pl->vi || pk->stream_index == pl->ai) {
            Queue *q = pk->stream_index == pl->vi ? &pl->vq : &pl->aq;   /* before the move resets it */
            AVPacket *c = av_packet_alloc();
            av_packet_move_ref(c, pk);
            q_put(q, c, pl->serial, pts);
        } else {
            if (pk->stream_index == pl->si && pl->sdec) subtitle_packet(pl, pk);
            av_packet_unref(pk);
        }
    }
    av_packet_free(&pk);
    return 0;
}

static void player_free(Player *pl)
{
    if (!pl) return;
    pl->abort = 1;
    SDL_CondBroadcast(pl->vq.c); SDL_CondBroadcast(pl->aq.c); SDL_CondBroadcast(pl->fc);
    if (pl->reader) SDL_WaitThread(pl->reader, NULL);
    if (pl->vthread) SDL_WaitThread(pl->vthread, NULL);
    if (pl->athread) SDL_WaitThread(pl->athread, NULL);
    if (pl->dev) SDL_CloseAudioDevice(pl->dev);
    q_free(&pl->vq); q_free(&pl->aq);
    avcodec_free_context(&pl->vdec); avcodec_free_context(&pl->adec); avcodec_free_context(&pl->sdec);
    if (pl->fmt) avformat_close_input(&pl->fmt);
    sws_freeContext(pl->sws);
    for (int i = 0; i < FRAMES; i++) { free(pl->fr[i].rgba); av_frame_free(&pl->fr[i].yuv); }
    free(pl->fifo); free(pl->url);
    SDL_DestroyMutex(pl->fm); SDL_DestroyCond(pl->fc); SDL_DestroyMutex(pl->am); SDL_DestroyMutex(pl->sm);
    free(pl);
}

/* Closing a stream can wait on the network (a read in flight): done on a thread of its own,
 * so stopping a video never holds the screen. */
static SDL_atomic_t g_closing_n;
#define g_closing SDL_AtomicGet(&g_closing_n)
static int free_thread(void *arg) { player_free(arg); SDL_AtomicAdd(&g_closing_n, -1); return 0; }
static void player_drop(void)
{
    Player *pl = P;
    P = NULL;
    if (!pl) return;
    pl->abort = 1;
    SDL_AtomicAdd(&g_closing_n, 1);
    SDL_Thread *t = SDL_CreateThread(free_thread, "ff-close", pl);
    if (t) SDL_DetachThread(t); else { player_free(pl); SDL_AtomicAdd(&g_closing_n, -1); }
}

/* ---- the picture on the screen ------------------------------------------------------- */

/* YUV planes to colours, drawn into the picture's texture: the planes are 8-bit textures (a
 * 16-bit sample is two bytes, low and high, put back together here), cropped to the picture
 * (u_cy, u_cc: a plane's rows are wider than the picture), then the video's own matrix. */
static const char *YUV_VS =
    "attribute vec2 a_pos;\n varying vec2 v_uv;\n"
    "void main(){ v_uv=a_pos*0.5+0.5; gl_Position=vec4(a_pos,0.0,1.0); }\n";
static const char *YUV_FS =
    "#ifdef GL_FRAGMENT_PRECISION_HIGH\n precision highp float;\n#else\n precision mediump float;\n#endif\n"
    "varying vec2 v_uv;\n uniform sampler2D u_y, u_u, u_v;\n uniform vec2 u_cy, u_cc, u_k;\n uniform float u_mode;\n"
    "uniform mat3 u_m;\n uniform vec3 u_off;\n"
    "float w16(vec2 lh){ return lh.y*u_k.y+lh.x*u_k.x; }\n"
    "void main(){\n"
    "  vec2 a=v_uv*u_cy, b=v_uv*u_cc; vec3 c;\n"
    "  if (u_mode<0.5) c=vec3(texture2D(u_y,a).r, texture2D(u_u,b).r, texture2D(u_v,b).r);\n"
    "  else if (u_mode<1.5) { vec4 q=texture2D(u_u,b); c=vec3(texture2D(u_y,a).r, q.r, q.a); }\n"
    "  else if (u_mode<2.5) c=vec3(w16(texture2D(u_y,a).ra), w16(texture2D(u_u,b).ra), w16(texture2D(u_v,b).ra));\n"
    "  else { vec4 q=texture2D(u_u,b); c=vec3(w16(texture2D(u_y,a).ra), w16(q.rg), w16(q.ba)); }\n"
    "  gl_FragColor=vec4(clamp(u_m*(c-u_off),0.0,1.0),1.0);\n"
    "}\n";
static GLuint g_yuv_prog, g_yuv_vbo, g_fbo, g_planes[3];
static int g_pl_w[3], g_pl_h[3]; static GLenum g_pl_f[3];
static GLint g_u_cy, g_u_cc, g_u_k, g_u_mode, g_u_m, g_u_off;

static GLuint yuv_shader(GLenum type, const char *src)
{
    GLuint sh = glCreateShader(type); GLint ok = 0;
    glShaderSource(sh, 1, &src, NULL); glCompileShader(sh);
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) { char log[512]; glGetShaderInfoLog(sh, sizeof log, NULL, log); LOGW("video(ffmpeg): YUV shader: %s", log); glDeleteShader(sh); return 0; }
    return sh;
}

/* On the GL thread, once: without it every picture goes through swscale */
static void yuv_init(void)
{
    if (getenv("SNTV_NO_GPU_YUV")) return;
    GLuint vs = yuv_shader(GL_VERTEX_SHADER, YUV_VS), fs = yuv_shader(GL_FRAGMENT_SHADER, YUV_FS);
    GLint ok = 0;
    if (vs && fs) {
        g_yuv_prog = glCreateProgram();
        glAttachShader(g_yuv_prog, vs); glAttachShader(g_yuv_prog, fs);
        glBindAttribLocation(g_yuv_prog, 0, "a_pos");
        glLinkProgram(g_yuv_prog);
        glGetProgramiv(g_yuv_prog, GL_LINK_STATUS, &ok);
    }
    if (vs) glDeleteShader(vs);
    if (fs) glDeleteShader(fs);
    if (!ok) { if (g_yuv_prog) glDeleteProgram(g_yuv_prog); g_yuv_prog = 0; LOGW("video(ffmpeg): colours by swscale (no YUV shader)"); return; }
    glUseProgram(g_yuv_prog);
    glUniform1i(glGetUniformLocation(g_yuv_prog, "u_y"), 0);
    glUniform1i(glGetUniformLocation(g_yuv_prog, "u_u"), 1);
    glUniform1i(glGetUniformLocation(g_yuv_prog, "u_v"), 2);
    g_u_cy = glGetUniformLocation(g_yuv_prog, "u_cy"); g_u_cc = glGetUniformLocation(g_yuv_prog, "u_cc");
    g_u_k = glGetUniformLocation(g_yuv_prog, "u_k"); g_u_mode = glGetUniformLocation(g_yuv_prog, "u_mode");
    g_u_m = glGetUniformLocation(g_yuv_prog, "u_m"); g_u_off = glGetUniformLocation(g_yuv_prog, "u_off");
    static const float quad[8] = { -1, -1, 1, -1, -1, 1, 1, 1 };
    glGenBuffers(1, &g_yuv_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, g_yuv_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof quad, quad, GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glGenFramebuffers(1, &g_fbo);
    g_yuv_ok = 1;
}

static void tex_params(void)
{
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

/* The picture's texture at w x h (made or resized) and bound */
static void pic_texture(int w, int h, const void *rgba)
{
    if (!g_tex) { glGenTextures(1, &g_tex); glBindTexture(GL_TEXTURE_2D, g_tex); tex_params(); }
    else glBindTexture(GL_TEXTURE_2D, g_tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    if (w != g_tex_w || h != g_tex_h) { glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba); g_tex_w = w; g_tex_h = h; }
    else if (rgba) glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
}

/* One plane into texture unit i: a row's bytes over bpp texels of format fmt */
static void plane(int i, const uint8_t *data, int linesize, int rows, int bpp, GLenum fmt)
{
    int tw = linesize / bpp;
    glActiveTexture(GL_TEXTURE0 + i);
    if (!g_planes[i]) { glGenTextures(1, &g_planes[i]); glBindTexture(GL_TEXTURE_2D, g_planes[i]); tex_params(); }
    else glBindTexture(GL_TEXTURE_2D, g_planes[i]);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    if (g_pl_w[i] != tw || g_pl_h[i] != rows || g_pl_f[i] != fmt) {
        glTexImage2D(GL_TEXTURE_2D, 0, (GLint)fmt, tw, rows, 0, fmt, GL_UNSIGNED_BYTE, data);
        g_pl_w[i] = tw; g_pl_h[i] = rows; g_pl_f[i] = fmt;
    } else glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, tw, rows, fmt, GL_UNSIGNED_BYTE, data);
}

/* The planes of f drawn into the picture's texture, no bigger than the window */
static void yuv_draw(const AVFrame *f)
{
    int layout = yuv_layout(f->format), deep = layout >= 2;
    int w = f->width, h = f->height, cw = (w + 1) / 2, ch = (h + 1) / 2;
    if (layout < 0 || f->linesize[0] <= 0 || f->linesize[1] <= 0) return;
    switch (layout) {
    case 0: plane(0, f->data[0], f->linesize[0], h, 1, GL_LUMINANCE); plane(1, f->data[1], f->linesize[1], ch, 1, GL_LUMINANCE); plane(2, f->data[2], f->linesize[2], ch, 1, GL_LUMINANCE); break;
    case 1: plane(0, f->data[0], f->linesize[0], h, 1, GL_LUMINANCE); plane(1, f->data[1], f->linesize[1], ch, 2, GL_LUMINANCE_ALPHA); break;
    case 2: plane(0, f->data[0], f->linesize[0], h, 2, GL_LUMINANCE_ALPHA); plane(1, f->data[1], f->linesize[1], ch, 2, GL_LUMINANCE_ALPHA); plane(2, f->data[2], f->linesize[2], ch, 2, GL_LUMINANCE_ALPHA); break;
    default: plane(0, f->data[0], f->linesize[0], h, 2, GL_LUMINANCE_ALPHA); plane(1, f->data[1], f->linesize[1], ch, 4, GL_RGBA); break;
    }
    /* no bigger than the window: the shader's own filtering does the shrinking once */
    int ow = w, oh = h, dw = 0, dh = 0;
    if (g_win) SDL_GL_GetDrawableSize(g_win, &dw, &dh);
    if (dw > 0 && dh > 0 && (ow > dw || oh > dh)) {
        double k = (double)dw / ow < (double)dh / oh ? (double)dw / ow : (double)dh / oh;
        ow = (int)(ow * k + 0.5); oh = (int)(oh * k + 0.5);
        if (ow < 2) ow = 2;
        if (oh < 2) oh = 2;
    }
    glActiveTexture(GL_TEXTURE0);
    pic_texture(ow, oh, NULL);
    GLint prev_fb = 0; glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prev_fb);
    glBindFramebuffer(GL_FRAMEBUFFER, g_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, g_tex, 0);
    glBindTexture(GL_TEXTURE_2D, g_planes[0]);
    glViewport(0, 0, ow, oh);
    glDisable(GL_BLEND); glDisable(GL_SCISSOR_TEST);
    glUseProgram(g_yuv_prog);
    /* the video's matrix (BT.709 for HD, BT.2020, else BT.601) and range, at its bit depth */
    int hd = f->colorspace == AVCOL_SPC_BT709 || (f->colorspace == AVCOL_SPC_UNSPECIFIED && h >= 720);
    float kr = hd ? 0.2126f : 0.299f, kb = hd ? 0.0722f : 0.114f;
    if (f->colorspace == AVCOL_SPC_BT2020_NCL || f->colorspace == AVCOL_SPC_BT2020_CL) { kr = 0.2627f; kb = 0.0593f; }
    float kg = 1 - kr - kb, top = deep ? 1023.0f : 255.0f, sh = deep ? 4.0f : 1.0f;
    int full = f->color_range == AVCOL_RANGE_JPEG || f->format == AV_PIX_FMT_YUVJ420P;
    float oy = full ? 0 : 16 * sh / top, sy = full ? 1 : top / (219 * sh), oc = 128 * sh / top, sc = full ? 1 : top / (224 * sh);
    float m[9] = {   /* column-major: Y, Cb, Cr columns */
        sy, sy, sy,
        0, -2 * kb * (1 - kb) / kg * sc, 2 * (1 - kb) * sc,
        2 * (1 - kr) * sc, -2 * kr * (1 - kr) / kg * sc, 0 };
    glUniformMatrix3fv(g_u_m, 1, GL_FALSE, m);
    glUniform3f(g_u_off, oy, oc, oc);
    glUniform1f(g_u_mode, (float)layout);
    /* a 16-bit sample from its bytes: 10 bits low-aligned (yuv420p10) or high-aligned (P010) */
    if (layout == 2) glUniform2f(g_u_k, 255.0f / 1023, 255.0f * 256 / 1023);
    else glUniform2f(g_u_k, 255.0f / 65472, 255.0f * 256 / 65472);
    int bpp0 = deep ? 2 : 1, bpp1 = layout == 1 ? 2 : layout == 3 ? 4 : bpp0;
    glUniform2f(g_u_cy, (float)w / (f->linesize[0] / bpp0), 1);
    glUniform2f(g_u_cc, (float)cw / (f->linesize[1] / bpp1), 1);
    glBindBuffer(GL_ARRAY_BUFFER, g_yuv_vbo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, NULL);
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, g_planes[0]);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glDisableVertexAttribArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prev_fb);
    glEnable(GL_BLEND);   /* the rest of the state is the screen's to set: gfx_begin_frame does */
}

/* The picture due now into the texture, on the GL thread (before the frame is drawn) */
static void picture_upload(Player *pl)
{
    SDL_LockMutex(pl->fm);
    Frame *f = pl->f_n > 0 ? &pl->fr[pl->f_r] : NULL;
    if (f && f->serial == pl->serial && (f->pts != g_tex_pts || g_tex_serial != f->serial)) {
        if (f->yuv && f->yuv->data[0]) yuv_draw(f->yuv);
        else if (f->rgba) { glActiveTexture(GL_TEXTURE0); pic_texture(f->w, f->h, f->rgba); }
        g_tex_pts = f->pts; g_tex_serial = f->serial; g_shown_pts = f->pts;
    }
    SDL_UnlockMutex(pl->fm);
}

/* ---- the interface ------------------------------------------------------------------- */

int video_init(SDL_Window *win)
{
    g_win = win;
    yuv_init();
    avformat_network_init();
    av_log_set_level(AV_LOG_ERROR);
    g_info.state = VIDEO_IDLE;
    return 0;
}
void video_shutdown(void) { Player *pl = P; P = NULL; player_free(pl); while (g_closing) SDL_Delay(5); if (g_tex) glDeleteTextures(1, &g_tex); g_tex = 0;
    if (g_planes[0]) glDeleteTextures(3, g_planes);
    if (g_yuv_prog) { glDeleteProgram(g_yuv_prog); glDeleteBuffers(1, &g_yuv_vbo); glDeleteFramebuffers(1, &g_fbo); }
    av_buffer_unref(&g_hw_dev); avformat_network_deinit(); }
const char *video_backend_name(void) { return "ffmpeg (desktop)"; }

int video_load(const char *url, const char *title, double start)
{
    player_drop();
    g_loads++;
    memset(&g_info, 0, sizeof g_info);
    g_info.state = VIDEO_LOADING; g_info.buffer_ahead = -1; g_info.position = start > 0 ? start : 0;
    g_paused = 0; g_paused_on_purpose = 0; g_muted = 0; g_volume = 100; g_rate = 1.0;
    g_wall_base = -1; g_shown_pts = -1; g_tex_serial = -1; g_tex_pts = -2;
    g_na = g_nt = 0; g_text_sel = -1;
    Player *pl = calloc(1, sizeof *pl);
    if (!pl || !(pl->url = strdup(url))) { free(pl); g_info.state = VIDEO_ERROR; snprintf(g_info.error, sizeof g_info.error, "Out of memory"); return -1; }
    pl->start = start > 0 ? start : 0; pl->gen = g_loads;
    pl->vi = pl->ai = pl->si = -1; pl->hw_fmt = AV_PIX_FMT_NONE;
    q_init(&pl->vq); q_init(&pl->aq);
    pl->fm = SDL_CreateMutex(); pl->fc = SDL_CreateCond(); pl->am = SDL_CreateMutex(); pl->sm = SDL_CreateMutex();
    pl->clock_pts = -1; pl->a_serial = -1;
    P = pl;
    LOGI("video(ffmpeg): load %s (%s) from %.0fs", url, title ? title : "", start);
    pl->reader = SDL_CreateThread(reader_thread, "ff-read", pl);
    return pl->reader ? 0 : -1;
}

void video_play(void)
{
    if (!P) return;
    if (g_paused && P->ai < 0) { g_wall_base = clock_now() >= 0 ? g_info.position : 0; g_wall_at = SDL_GetPerformanceCounter(); }
    if (g_paused && P->ai >= 0) { SDL_LockMutex(P->am); P->clock_at = SDL_GetPerformanceCounter(); SDL_UnlockMutex(P->am); }
    g_paused = 0; g_paused_on_purpose = 0;
    if (g_info.state == VIDEO_PAUSED) g_info.state = VIDEO_PLAYING;
}
void video_pause(void)
{
    if (!P) return;
    double now = clock_now();
    if (P->ai >= 0) { SDL_LockMutex(P->am); if (P->clock_pts >= 0) P->clock_pts = now; SDL_UnlockMutex(P->am); }
    else if (g_wall_base >= 0) { g_wall_base = now; g_wall_at = SDL_GetPerformanceCounter(); }
    g_paused = 1; g_paused_on_purpose = 1;
    if (g_info.state == VIDEO_PLAYING) g_info.state = VIDEO_PAUSED;
}
void video_seek(double s)
{
    if (!P) return;
    if (s < 0) s = 0;
    if (g_info.duration > 0 && s > g_info.duration - 1) s = g_info.duration - 1;
    P->seek_to = s; P->seek_req = 1;
    g_info.position = s; g_shown_pts = s; g_wall_base = -1;
    if (g_info.state == VIDEO_ENDED) g_info.state = g_paused ? VIDEO_PAUSED : VIDEO_PLAYING;
}
void video_stop(void) { player_drop(); g_info.state = VIDEO_IDLE; g_tex_serial = -1; }
void video_set_rate(double r) { if (r > 0) g_rate = r; if (r != 1.0) LOGI("video(ffmpeg): speed %.2fx is not supported here; normal speed", r); g_rate = 1.0; }
void video_set_muted(int m) { g_muted = m; }
void video_set_volume(int v) { g_volume = v < 0 ? 0 : v > 100 ? 100 : v; }
unsigned video_load_count(void) { return g_loads; }

void video_update(void)
{
    Player *pl = P;
    if (!pl || g_info.state == VIDEO_ERROR || g_info.state == VIDEO_IDLE) return;
    double now = clock_now();
    /* the picture due now: drop the late ones while a newer is waiting */
    if (pl->vi >= 0) {
        SDL_LockMutex(pl->fm);
        while (pl->f_n > 1 && pl->fr[(pl->f_r + 1) % FRAMES].pts <= now) { pl->f_r = (pl->f_r + 1) % FRAMES; pl->f_n--; SDL_CondSignal(pl->fc); }
        int have = pl->f_n > 0;
        SDL_UnlockMutex(pl->fm);
        picture_upload(pl);
        if (have && pl->ai < 0 && g_wall_base < 0 && !g_paused) { g_wall_base = pl->fr[pl->f_r].pts >= 0 ? pl->fr[pl->f_r].pts : 0; g_wall_at = SDL_GetPerformanceCounter(); }
    }
    int started = pl->vi >= 0 ? (g_tex_serial == pl->serial || pl->f_n > 0) : pl->clock_pts >= 0;
    if (g_info.state == VIDEO_LOADING && started) { g_info.state = g_paused ? VIDEO_PAUSED : VIDEO_PLAYING; LOGI("video(ffmpeg): playing"); }
    if (g_info.state == VIDEO_PLAYING || g_info.state == VIDEO_PAUSED) {
        if (!pl->seek_req) g_info.position = now < 0 ? 0 : now;
        int waiting = !pl->eof && !g_paused && ((pl->vi >= 0 && pl->f_n == 0 && pl->vq.n == 0) || (pl->ai >= 0 && pl->f_len == 0 && pl->aq.n == 0));
        g_info.buffering = waiting;
        g_info.buffer_percent = waiting ? 0 : 100;
        int done = pl->eof && (pl->vi < 0 || (pl->vdone && pl->f_n <= 1)) && (pl->ai < 0 || (pl->adone && pl->f_len == 0));
        if (done) { g_info.state = VIDEO_ENDED; LOGI("video(ffmpeg): ended"); }
    }
}

void video_info(VideoInfo *out) { *out = g_info; }

/* The picture now: its texture and shape, 0 while there is none (TV backends always 0: their
 * video is a plane of its own). video_update brings it up to date. */
unsigned video_frame_texture(int *w, int *h, float *aspect)
{
    Player *pl = P;
    if (!pl || pl->vi < 0 || !g_tex || g_tex_serial != pl->serial) return 0;
    *w = g_tex_w; *h = g_tex_h;
    *aspect = (float)g_tex_w * g_sar_num / ((float)g_tex_h * g_sar_den);
    return g_tex;
}

int video_subtitle_text(char *out, size_t n)
{
    Player *pl = P;
    if (n) *out = 0;
    if (!pl || pl->si < 0) return 0;
    double now = g_info.position - g_sub_sync_ms / 1000.0;
    SDL_LockMutex(pl->sm);
    for (int i = pl->ncue - 1; i >= 0; i--)
        if (pl->cues[i].start <= now && now < pl->cues[i].end) {
            size_t l = strlen(out);
            snprintf(out + l, n - l, "%s%s", l ? "\n" : "", pl->cues[i].text);
        }
    SDL_UnlockMutex(pl->sm);
    return *out != 0;
}

int video_tracks(TrackKind kind, VideoTrack *out, int max)
{
    int n = kind == TRACK_AUDIO ? g_na : g_nt;
    if (n > max) n = max;
    for (int i = 0; i < n; i++) {
        out[i] = kind == TRACK_AUDIO ? g_atracks[i] : g_ttracks[i];
        if (kind == TRACK_AUDIO) out[i].selected = P && out[i].index == P->ai;
        else out[i].selected = P && out[i].index == P->si;
    }
    return n;
}

void video_select_track(TrackKind kind, int index)
{
    Player *pl = P;
    if (!pl || !pl->fmt) return;
    if (kind == TRACK_AUDIO) {
        if (index < 0 || index >= (int)pl->fmt->nb_streams || index == pl->ai || pl->ai < 0) return;
        pl->switch_audio = index + 1;
    } else pl->text_req = (index < 0 ? -1 : index) + 2;
    g_info.tracks_version++;
}

void video_subtitle_style(int size, int style, const char *colour, int position, int sync_ms) { (void)size; (void)style; (void)colour; (void)position; g_sub_sync_ms = sync_ms; }

/* ---- the rest of the interface: what a TV or a phone has and a computer does not ------- */

void video_landscape_only(int on) { (void)on; }
int video_take_open_url(char *out, size_t n) { (void)out; (void)n; return 0; }
int video_open_browser(const char *url)
{
    if (strncmp(url, "http://", 7) && strncmp(url, "https://", 8)) return 0;
    return SDL_OpenURL(url) == 0;
}
int video_has_youtube_app(void) { return 1; }   /* the browser stands in for it */
int video_open_youtube(const char *id)
{
    if (!youtube_id_valid(id)) return 0;
    char u[80]; snprintf(u, sizeof u, "https://www.youtube.com/watch?v=%s", id);
    LOGI("video(ffmpeg): YouTube in the browser for %s", id);
    return SDL_OpenURL(u) == 0;
}
int video_youtube_embed_url(const char *id, char *out, size_t n) { (void)id; (void)out; (void)n; return 0; }
int video_youtube_web_state(void) { return -1; }
void video_youtube_web_check(void) {}
int video_youtube_web(const char *id, double start, const char *mode, int flags) { (void)id; (void)start; (void)mode; (void)flags; return 0; }
int video_youtube_web_result(char *why, size_t n) { if (why && n) *why = 0; return 0; }
void video_pip_allow(int on) { (void)on; }
int video_pip_enter(void) { return 0; }
int video_in_pip(void) { return 0; }
float video_device_volume(void) { return -1; }
int video_set_device_volume(float v) { (void)v; return 0; }
float video_brightness(void) { return -1; }
int video_set_brightness(float v) { (void)v; return 0; }
void video_set_display_rect(int x, int y, int w, int h) { (void)x; (void)y; (void)w; (void)h; }
void video_app_background(void) {}
void video_app_foreground(void) {}
void video_keep_awake(int on) { if (on) SDL_DisableScreenSaver(); else if (!P) SDL_EnableScreenSaver(); }
void video_hold_screensaver(int on) { if (on) SDL_DisableScreenSaver(); else SDL_EnableScreenSaver(); }
int video_paused_on_purpose(void) { return g_paused_on_purpose; }
void video_memory_stage(int stage) { (void)stage; }
int video_install_update(const char *url, const char *sha256, const char *app_id) { (void)url; (void)sha256; (void)app_id; return -1; }
int video_install_status(char *out, size_t n) { if (n) *out = 0; return 0; }
