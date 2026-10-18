#include "image.h"
#include "http.h"
#include "log.h"
#include "platform/platform.h"
#include "ui/gfx.h"
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STBI_NO_PSD
#define STBI_NO_TGA
#define STBI_NO_PIC
#define STBI_NO_PNM
#include "stb_image.h"
#include <stdlib.h>
#include <string.h>
#include <SDL.h>

typedef enum { IMG_LOADING, IMG_DECODING, IMG_READY, IMG_FAILED, IMG_EVICTED } ImgState;

typedef struct Img {
    char *url;
    ImgState state;
    unsigned tex;
    int w, h;
    size_t bytes;
    unsigned last_used;
    unsigned req;
    /* decode handoff */
    unsigned char *encoded; size_t encoded_len;
    unsigned char *pixels;
    struct Img *next;      /* hash chain */
    struct Img *qnext;     /* decode/done queues */
} Img;

enum { BUCKETS = 512 };
static Img *g_buckets[BUCKETS];
static size_t g_total_bytes;
static unsigned g_clock;

static SDL_Thread *g_decoder;
static SDL_mutex *g_lock;
static SDL_cond *g_cond;
static Img *g_decode_q, *g_done_q;
static int g_quit;

static unsigned hash(const char *s)
{
    unsigned h = 2166136261u;
    for (; *s; s++) h = (h ^ (unsigned char)*s) * 16777619u;
    return h;
}

static Img *find(const char *url)
{
    for (Img *i = g_buckets[hash(url) % BUCKETS]; i; i = i->next)
        if (strcmp(i->url, url) == 0) return i;
    return NULL;
}

/* Box-filter downscale by an integer factor so a 2000px backdrop never hits VRAM. */
static unsigned char *shrink(unsigned char *px, int *w, int *h)
{
    int f = 1;
    while ((*w / f) > SNTV_IMAGE_MAX_DIM || (*h / f) > SNTV_IMAGE_MAX_DIM) f++;
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

static int decoder_main(void *arg)
{
    (void)arg;
    for (;;) {
        SDL_LockMutex(g_lock);
        while (!g_decode_q && !g_quit) SDL_CondWait(g_cond, g_lock);
        if (g_quit) { SDL_UnlockMutex(g_lock); return 0; }
        Img *i = g_decode_q;
        g_decode_q = i->qnext;
        SDL_UnlockMutex(g_lock);

        int w = 0, h = 0, n = 0;
        unsigned char *px = stbi_load_from_memory(i->encoded, (int)i->encoded_len, &w, &h, &n, 4);
        free(i->encoded);
        i->encoded = NULL;
        if (px) {
            px = shrink(px, &w, &h);
            i->pixels = px; i->w = w; i->h = h;
        }

        SDL_LockMutex(g_lock);
        i->qnext = g_done_q;
        g_done_q = i;
        SDL_UnlockMutex(g_lock);
    }
}

static void on_http(HttpResponse *r, void *user)
{
    Img *i = user;
    i->req = 0;
    if (r->status != 200 || r->len == 0) {
        i->state = IMG_FAILED;
        return;
    }
    i->encoded = malloc(r->len);
    memcpy(i->encoded, r->body, r->len);
    i->encoded_len = r->len;
    i->state = IMG_DECODING;
    SDL_LockMutex(g_lock);
    i->qnext = g_decode_q;
    g_decode_q = i;
    SDL_CondSignal(g_cond);
    SDL_UnlockMutex(g_lock);
}

static void evict_if_needed(void)
{
    while (g_total_bytes > SNTV_IMAGE_CACHE_BYTES) {
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
        victim->state = IMG_EVICTED;  /* re-requested lazily by image_get */
    }
}

static void request(Img *i)
{
    i->state = IMG_LOADING;
    i->req = http_request_async(i->url, NULL, on_http, i);
}

unsigned image_get(const char *url, int *w, int *h)
{
    if (!url || !*url) return 0;
    Img *i = find(url);
    if (!i) {
        i = calloc(1, sizeof *i);
        i->url = strdup(url);
        unsigned b = hash(url) % BUCKETS;
        i->next = g_buckets[b];
        g_buckets[b] = i;
        request(i);
    } else if (i->state == IMG_EVICTED) {
        request(i);
    }
    i->last_used = g_clock;
    if (i->state != IMG_READY) return 0;
    if (w) *w = i->w;
    if (h) *h = i->h;
    return i->tex;
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
    int uploads = 0;
    while (done) {
        Img *i = done;
        done = i->qnext;
        if (i->pixels) {
            i->tex = gfx_texture_rgba(i->pixels, i->w, i->h);
            i->bytes = (size_t)i->w * i->h * 4;
            g_total_bytes += i->bytes;
            free(i->pixels);
            i->pixels = NULL;
            i->state = IMG_READY;
            uploads++;
        } else {
            i->state = IMG_FAILED;
        }
        /* Cap uploads per frame so a burst of posters doesn't stall input on a slow GPU. */
        if (uploads >= 6 && done) {
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

int image_init(void)
{
    g_lock = SDL_CreateMutex();
    g_cond = SDL_CreateCond();
    g_decoder = SDL_CreateThread(decoder_main, "img-decode", NULL);
    return g_decoder ? 0 : -1;
}

void image_shutdown(void)
{
    SDL_LockMutex(g_lock);
    g_quit = 1;
    SDL_CondBroadcast(g_cond);
    SDL_UnlockMutex(g_lock);
    SDL_WaitThread(g_decoder, NULL);
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
