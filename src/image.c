#include "image.h"
#include "perfstat.h"
#include "src/webp/decode.h"
#include "http.h"
#include "log.h"
#include "platform/platform.h"
#include "ui/gfx.h"
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STBI_NO_PSD
#define STBI_NO_TGA
#define STBI_NO_PIC
#define STBI_NO_PNM
#include "stb_image.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <dirent.h>
#include <time.h>
#include <utime.h>
#include <SDL.h>

typedef enum { IMG_LOADING, IMG_DECODING, IMG_READY, IMG_FAILED, IMG_EVICTED } ImgState;

typedef struct Img {
    char *url;
    unsigned long long key;
    ImgState state;
    ImageFlags flags;
    unsigned tex;
    int w, h;
    size_t bytes;
    unsigned last_used, ready_at;
    unsigned tint;
    unsigned req;
    int from_disk;
    int need_net;          /* the decoder found no disk copy: fetch it */
    unsigned char *encoded; size_t encoded_len;
    unsigned char *pixels;
    struct Img *next;      /* hash chain */
    struct Img *qnext;     /* decode/done queues */
} Img;

enum { BUCKETS = 1024, DISK_CACHE_MAX_BYTES = 96 * 1024 * 1024, DISK_FILE_MAX = 2 * 1024 * 1024 };
static Img *g_buckets[BUCKETS];
static size_t g_total_bytes, g_budget = SNTV_IMAGE_CACHE_BYTES;
static unsigned g_clock;
static char g_cache_dir[1024];
static int g_pending_decodes;

enum { DECODERS = 2 };
static SDL_Thread *g_decoder[DECODERS];
static SDL_mutex *g_lock;
static SDL_cond *g_cond;
static Img *g_decode_q, *g_done_q;
static int g_quit;

static unsigned long long fnv64(const char *s)
{
    unsigned long long h = 1469598103934665603ull;
    for (; *s; s++) h = (h ^ (unsigned char)*s) * 1099511628211ull;
    return h;
}

/* Plain and mipmapped variants of the same URL are separate entries (different
 * texture layouts) but share the encoded bytes on disk. */
static unsigned bucket_of(const char *url, ImageFlags flags) { return (unsigned)((fnv64(url) ^ (unsigned long long)flags) % BUCKETS); }

static Img *find_flags(const char *url, ImageFlags flags)
{
    for (Img *i = g_buckets[bucket_of(url, flags)]; i; i = i->next)
        if (i->flags == flags && strcmp(i->url, url) == 0) return i;
    return NULL;
}

static Img *find(const char *url) { Img *i = find_flags(url, IMAGE_PLAIN); return i ? i : find_flags(url, IMAGE_MIPMAP); }

static unsigned average_tint(const unsigned char *px, int w, int h)
{
    unsigned long long acc[3] = { 0, 0, 0 }, n = 0;
    int step = (w * h) / 4096 + 1;
    for (int i = 0; i < w * h; i += step, n++) for (int k = 0; k < 3; k++) acc[k] += px[(size_t)i * 4 + k];
    if (!n) return 0;
    float c[3];
    for (int k = 0; k < 3; k++) c[k] = (float)acc[k] / n;
    float avg = (c[0] + c[1] + c[2]) / 3;
    float mx = c[0] > c[1] ? (c[0] > c[2] ? c[0] : c[2]) : (c[1] > c[2] ? c[1] : c[2]);
    for (int k = 0; k < 3; k++) {          /* boost saturation, then normalise brightness */
        c[k] = avg + (c[k] - avg) * 1.8f;
        if (mx > 1) c[k] = c[k] * 165.0f / (mx + 1);
        if (c[k] < 0) c[k] = 0;
        if (c[k] > 255) c[k] = 255;
    }
    return ((unsigned)c[0] << 24) | ((unsigned)c[1] << 16) | ((unsigned)c[2] << 8) | 0xFFu;
}

static void disk_path(const Img *i, char *out, size_t n)
{
    snprintf(out, n, "%s/%016llx.img", g_cache_dir, i->key);
}

/* Box-filter downscale by an integer factor so a 2000px backdrop never hits VRAM. */
static unsigned char *shrink(unsigned char *px, int *w, int *h, int max_dim)
{
    int f = 1;
    while ((*w / f) > max_dim || (*h / f) > max_dim) f++;
    if (f == 1) return px;
    int nw = *w / f, nh = *h / f;
    unsigned char *out = malloc((size_t)nw * nh * 4);
    for (int y = 0; y < nh; y++)
        for (int x = 0; x < nw; x++) {
            unsigned acc[4] = { 0, 0, 0, 0 };
            for (int dy = 0; dy < f; dy++)
                for (int dx = 0; dx < f; dx++) {
                    const unsigned char *p = px + (((size_t)(y * f + dy) * *w) + (x * f + dx)) * 4;
                    for (int k = 0; k < 4; k++) acc[k] += p[k];
                }
            unsigned char *o = out + ((size_t)y * nw + x) * 4;
            for (int k = 0; k < 4; k++) o[k] = (unsigned char)(acc[k] / (unsigned)(f * f));
        }
    free(px);
    *w = nw; *h = nh;
    return out;
}

/* Resample to a power-of-two canvas (bilinear) so GLES2 can build mipmaps. */
static unsigned char *to_pot(unsigned char *px, int *w, int *h)
{
    int pw = 1, ph = 1;
    while (pw < *w) pw <<= 1;
    while (ph < *h) ph <<= 1;
    if (pw > 512) pw = 512;
    if (ph > 512) ph = 512;
    if (pw == *w && ph == *h) return px;
    unsigned char *out = malloc((size_t)pw * ph * 4);
    for (int y = 0; y < ph; y++) {
        float sy = (y + 0.5f) * *h / ph - 0.5f;
        int y0 = sy < 0 ? 0 : (int)sy, y1 = y0 + 1 < *h ? y0 + 1 : y0;
        float fy = sy - y0; if (fy < 0) fy = 0;
        for (int x = 0; x < pw; x++) {
            float sx = (x + 0.5f) * *w / pw - 0.5f;
            int x0 = sx < 0 ? 0 : (int)sx, x1 = x0 + 1 < *w ? x0 + 1 : x0;
            float fx = sx - x0; if (fx < 0) fx = 0;
            for (int k = 0; k < 4; k++) {
                float a = px[((size_t)y0 * *w + x0) * 4 + k] * (1 - fx) + px[((size_t)y0 * *w + x1) * 4 + k] * fx;
                float b = px[((size_t)y1 * *w + x0) * 4 + k] * (1 - fx) + px[((size_t)y1 * *w + x1) * 4 + k] * fx;
                out[((size_t)y * pw + x) * 4 + k] = (unsigned char)(a * (1 - fy) + b * fy + 0.5f);
            }
        }
    }
    free(px);
    *w = pw; *h = ph;
    return out;
}

static void trim_disk_cache(void);
static int read_disk(Img *i);
static size_t g_disk_written;   /* bytes added to the disk cache since it was last trimmed */
/* Pictures this TV makes (seek previews): never on disk, they would push the posters out. */
static int is_local(const char *url) { return !strncmp(url, "http://127.0.0.1", 16); }
static const HttpOptions ARTWORK = { .no_json_accept = 1, .background = 1 };

static int decoder_main(void *arg)
{
    if (arg && g_cache_dir[0]) trim_disk_cache();   /* the first decoder tidies the disk cache, off the main thread */
    for (;;) {
        SDL_LockMutex(g_lock);
        while (!g_decode_q && !g_quit) SDL_CondWait(g_cond, g_lock);
        if (g_quit) { SDL_UnlockMutex(g_lock); return 0; }
        Img *i = g_decode_q;
        g_decode_q = i->qnext;
        SDL_UnlockMutex(g_lock);

        if (!i->encoded && !read_disk(i)) {   /* no disk copy: hand back for a download */
            i->need_net = 1;
            SDL_LockMutex(g_lock);
            i->qnext = g_done_q;
            g_done_q = i;
            SDL_UnlockMutex(g_lock);
            continue;
        }
        int w = 0, h = 0, n = 0;
        unsigned char *px = NULL;
        if (i->encoded_len > 12 && !memcmp(i->encoded, "RIFF", 4) && !memcmp(i->encoded + 8, "WEBP", 4)) {
            /* libwebp hands back its own buffer; copy so the rest of the pipeline can free() it */
            uint8_t *wp = WebPDecodeRGBA(i->encoded, i->encoded_len, &w, &h);
            if (wp) { px = malloc((size_t)w * h * 4); if (px) memcpy(px, wp, (size_t)w * h * 4); WebPFree(wp); }
        } else {
            px = stbi_load_from_memory(i->encoded, (int)i->encoded_len, &w, &h, &n, 4);
        }
        if (px) {
            if (!i->from_disk && g_cache_dir[0] && i->encoded_len <= DISK_FILE_MAX && !is_local(i->url)) {
                char path[1200], tmp[1210];
                disk_path(i, path, sizeof path);
                snprintf(tmp, sizeof tmp, "%s.tmp", path);
                FILE *f = fopen(tmp, "wb");
                if (f) { fwrite(i->encoded, 1, i->encoded_len, f); fclose(f); rename(tmp, path); }
                SDL_LockMutex(g_lock);
                g_disk_written += i->encoded_len;
                int trim = g_disk_written > DISK_CACHE_MAX_BYTES / 6;   /* a long session trims as it goes */
                if (trim) g_disk_written = 0;
                SDL_UnlockMutex(g_lock);
                if (trim) trim_disk_cache();
            }
            px = shrink(px, &w, &h, i->flags & IMAGE_MIPMAP ? 512 : SNTV_IMAGE_MAX_DIM);
            i->tint = average_tint(px, w, h);
            if (i->flags & IMAGE_MIPMAP) px = to_pot(px, &w, &h);
            i->pixels = px; i->w = w; i->h = h;
        } else {
            const unsigned char *e = i->encoded;
            const char *kind = i->encoded_len > 12 && !memcmp(e, "RIFF", 4) && !memcmp(e + 8, "WEBP", 4) ? "WebP, damaged or unsupported variant" :
                               i->encoded_len > 12 && !memcmp(e + 4, "ftyp", 4) ? "AVIF/HEIF, not supported" :
                               i->encoded_len > 1 && e[0] == '<' ? "HTML or SVG, not an image" : stbi_failure_reason();
            LOGW("image: cannot decode %s (%s, %zu bytes)", i->url, kind ? kind : "?", i->encoded_len);
            if (i->from_disk) {
                char path[1200];
                disk_path(i, path, sizeof path);
                remove(path);   /* corrupt cache entry */
            }
        }
        free(i->encoded);
        i->encoded = NULL;

        SDL_LockMutex(g_lock);
        i->qnext = g_done_q;
        g_done_q = i;
        SDL_UnlockMutex(g_lock);
    }
}

static void enqueue_decode(Img *i)
{
    i->state = IMG_DECODING;
    SDL_LockMutex(g_lock);
    i->qnext = g_decode_q;
    g_decode_q = i;
    g_pending_decodes++;
    SDL_CondSignal(g_cond);
    SDL_UnlockMutex(g_lock);
}

static void on_http(HttpResponse *r, void *user)
{
    Img *i = user;
    i->req = 0;
    if (r->status != 200 || r->len == 0) {
        LOGW("image: %s -> HTTP %ld %s", i->url, r->status, r->error);
        i->state = IMG_FAILED;
        return;
    }
    i->encoded = (uint8_t *)r->body;   /* taken over, not copied: up to a few MB on the main thread */
    i->encoded_len = r->len;
    r->body = NULL; r->len = 0;
    i->from_disk = 0;
    enqueue_decode(i);
}

/* Decoder thread: the cached file's bytes, 1 when there is one. */
static int read_disk(Img *i)
{
    if (!g_cache_dir[0]) return 0;
    char path[1200];
    disk_path(i, path, sizeof path);
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n <= 0 || n > DISK_FILE_MAX) { fclose(f); return 0; }
    i->encoded = malloc((size_t)n);
    i->encoded_len = fread(i->encoded, 1, (size_t)n, f);
    fclose(f);
    i->from_disk = 1;
    struct stat st;   /* used again: the trim goes by age, so it counts as new (once a day at most) */
    if (stat(path, &st) == 0 && time(NULL) - st.st_mtime > 24 * 3600) utime(path, NULL);
    return 1;
}

/* The disk lookup happens on a decoder thread (it used to read up to 2 MB inside
 * image_get, from draw); a miss comes back through image_pump as need_net. */
static void request(Img *i)
{
    i->state = IMG_LOADING;
    if (!g_cache_dir[0] || is_local(i->url)) { i->req = http_request_async_ex(i->url, &ARTWORK, on_http, i); return; }
    enqueue_decode(i);
}

static void evict_if_needed(void)
{
    while (g_total_bytes > g_budget) {
        Img *victim = NULL;
        for (int b = 0; b < BUCKETS; b++)
            for (Img *i = g_buckets[b]; i; i = i->next)
                if (i->state == IMG_READY && i->last_used + 2 < g_clock && (!victim || i->last_used < victim->last_used))
                    victim = i;
        if (!victim) return;
        gfx_texture_destroy(victim->tex);
        victim->tex = 0;
        g_total_bytes -= victim->bytes;
        victim->bytes = 0;
        victim->state = IMG_EVICTED;
    }
}

static Img *get_or_create(const char *url, ImageFlags flags)
{
    Img *i = find_flags(url, flags);
    if (!i) {
        i = calloc(1, sizeof *i);
        i->url = strdup(url);
        i->key = fnv64(url);
        i->flags = flags;
        unsigned b = bucket_of(url, flags);
        i->next = g_buckets[b];
        g_buckets[b] = i;
        request(i);
    } else if (i->state == IMG_EVICTED) {
        request(i);
    }
    return i;
}

unsigned image_tint(const char *url)
{
    if (!url || !*url) return 0;
    Img *i = find(url);
    return i && i->state == IMG_READY ? i->tint : 0;
}

unsigned image_get_ex(const char *url, ImageFlags flags, int *w, int *h, unsigned *age)
{
    if (!url || !*url) return 0;
    Img *i = get_or_create(url, flags);
    i->last_used = g_clock;
    if (i->state != IMG_READY) return 0;
    if (w) *w = i->w;
    if (h) *h = i->h;
    if (age) *age = g_clock - i->ready_at;
    return i->tex;
}

unsigned image_get(const char *url, int *w, int *h) { return image_get_ex(url, IMAGE_PLAIN, w, h, NULL); }

void image_prefetch(const char *url)
{
    if (!url || !*url) return;
    Img *i = get_or_create(url, IMAGE_PLAIN);
    if (i->last_used + 30 < g_clock) i->last_used = g_clock - 30;   /* keep alive briefly, but evictable */
}

int image_failed(const char *url)
{
    Img *i = find(url);
    return i && i->state == IMG_FAILED;
}

void image_pump(void)
{
    SDL_LockMutex(g_lock);
    Img *done = g_done_q;
    g_done_q = NULL;
    SDL_UnlockMutex(g_lock);
    size_t uploaded = 0;
    while (done) {
        Img *i = done;
        done = i->qnext;
        if (i->need_net) {
            i->need_net = 0;
            i->state = IMG_LOADING;
            SDL_LockMutex(g_lock); g_pending_decodes--; SDL_UnlockMutex(g_lock);
            i->req = http_request_async_ex(i->url, &ARTWORK, on_http, i);
            continue;
        }
        if (i->pixels) {
            i->tex = (i->flags & IMAGE_MIPMAP) ? gfx_texture_rgba_mip(i->pixels, i->w, i->h) : gfx_texture_rgba(i->pixels, i->w, i->h);
            perf_mark("first picture");
            i->bytes = (size_t)i->w * i->h * 4;
            if (i->flags & IMAGE_MIPMAP) i->bytes += i->bytes / 3;
            g_total_bytes += i->bytes;
            free(i->pixels);
            i->pixels = NULL;
            i->state = IMG_READY;
            i->ready_at = g_clock;
            uploaded += i->bytes;
        } else {
            i->state = IMG_FAILED;
        }
        SDL_LockMutex(g_lock); g_pending_decodes--; SDL_UnlockMutex(g_lock);
        /* Cap the bytes sent to the GPU per frame: a few posters, or one backdrop (3.7 MB
         * at 1280 px), so a burst of artwork doesn't drop frames on a TV. */
        if (uploaded >= (3u << 20) && done) {
            SDL_LockMutex(g_lock);
            Img *tail = done;
            while (tail->qnext) tail = tail->qnext;
            tail->qnext = g_done_q;
            g_done_q = done;
            SDL_UnlockMutex(g_lock);
            break;
        }
    }
    evict_if_needed();
}

void image_frame_tick(void) { g_clock++; }
size_t image_cache_bytes(void) { return g_total_bytes; }

int image_busy(void)
{
    SDL_LockMutex(g_lock);
    int busy = g_pending_decodes > 0 || g_done_q != NULL;
    SDL_UnlockMutex(g_lock);
    return busy;
}

/* Trim the disk cache to DISK_CACHE_MAX_BYTES by deleting the oldest files. */
static void trim_disk_cache(void)
{
    DIR *d = opendir(g_cache_dir);
    if (!d) return;
    struct { char name[64]; long long mtime; long size; } *files = NULL;
    int n = 0, cap = 0;
    long long total = 0;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (!strstr(e->d_name, ".img") || strlen(e->d_name) > 40) continue;
        char path[1300];
        snprintf(path, sizeof path, "%s/%s", g_cache_dir, e->d_name);
        struct stat st;
        if (stat(path, &st) != 0) continue;
        if (n == cap) { cap = cap ? cap * 2 : 256; files = realloc(files, sizeof *files * (size_t)cap); }
        snprintf(files[n].name, sizeof files[n].name, "%.40s", e->d_name);
        files[n].mtime = (long long)st.st_mtime;
        files[n].size = (long)st.st_size;
        total += st.st_size;
        n++;
    }
    closedir(d);
    while (total > DISK_CACHE_MAX_BYTES && n > 0) {
        int oldest = 0;
        for (int i = 1; i < n; i++) if (files[i].mtime < files[oldest].mtime) oldest = i;
        char path[1300];
        snprintf(path, sizeof path, "%s/%s", g_cache_dir, files[oldest].name);
        remove(path);
        total -= files[oldest].size;
        files[oldest] = files[--n];
    }
    free(files);
}

static void evict_if_needed(void);
void image_set_budget(size_t budget_bytes) { if (budget_bytes) g_budget = budget_bytes; evict_if_needed(); }

int image_init(const char *cache_dir, size_t budget_bytes)
{
    if (budget_bytes) g_budget = budget_bytes;
    if (cache_dir) {
        snprintf(g_cache_dir, sizeof g_cache_dir, "%s/img", cache_dir);
        mkdir(g_cache_dir, 0755);
    }
    g_lock = SDL_CreateMutex();
    g_cond = SDL_CreateCond();
    /* two decoders: a 1080p backdrop no longer holds up the posters behind it */
    for (int d = 0; d < DECODERS; d++) g_decoder[d] = SDL_CreateThread(decoder_main, "img-decode", d == 0 ? (void *)1 : NULL);
    LOGI("image: budget %zu MB, disk cache %s", g_budget >> 20, g_cache_dir[0] ? g_cache_dir : "off");
    return g_decoder[0] ? 0 : -1;
}

void image_shutdown(void)
{
    SDL_LockMutex(g_lock);
    g_quit = 1;
    SDL_CondBroadcast(g_cond);
    SDL_UnlockMutex(g_lock);
    for (int d = 0; d < DECODERS; d++) if (g_decoder[d]) SDL_WaitThread(g_decoder[d], NULL);
    for (int b = 0; b < BUCKETS; b++)
        for (Img *i = g_buckets[b]; i;) {
            Img *n = i->next;
            if (i->req) http_cancel(i->req);
            gfx_texture_destroy(i->tex);
            free(i->encoded); free(i->pixels); free(i->url); free(i);
            i = n;
        }
    memset(g_buckets, 0, sizeof g_buckets);
    SDL_DestroyCond(g_cond);
    SDL_DestroyMutex(g_lock);
}
