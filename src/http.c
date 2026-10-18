#include "http.h"
#include "log.h"
#include "platform/platform.h"
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL.h>

/* Minimal libcurl ABI surface. The numeric values are part of curl's stable
 * ABI (curl/curl.h), reproduced here so the build needs no curl headers. */
typedef void CURL;
typedef int CURLcode;
struct curl_slist;
enum {
    OPT_WRITEDATA = 10001, OPT_URL = 10002, OPT_ERRORBUFFER = 10010,
    OPT_POSTFIELDS = 10015, OPT_USERAGENT = 10018, OPT_HTTPHEADER = 10023,
    OPT_WRITEFUNCTION = 20011, OPT_TIMEOUT = 13, OPT_LOW_SPEED_LIMIT = 19,
    OPT_LOW_SPEED_TIME = 20, OPT_POST = 47, OPT_FOLLOWLOCATION = 52,
    OPT_POSTFIELDSIZE = 60, OPT_SSL_VERIFYPEER = 64, OPT_CONNECTTIMEOUT = 78,
    OPT_NOSIGNAL = 99, OPT_ACCEPT_ENCODING = 10102, OPT_MAXREDIRS = 68,
    INFO_RESPONSE_CODE = 0x200002,
    GLOBAL_DEFAULT = 3,
};

static struct {
    void *lib;
    CURLcode (*global_init)(long);
    void (*global_cleanup)(void);
    CURL *(*easy_init)(void);
    CURLcode (*easy_setopt)(CURL *, int, ...);
    CURLcode (*easy_perform)(CURL *);
    void (*easy_cleanup)(CURL *);
    CURLcode (*easy_getinfo)(CURL *, int, ...);
    const char *(*easy_strerror)(CURLcode);
    struct curl_slist *(*slist_append)(struct curl_slist *, const char *);
    void (*slist_free_all)(struct curl_slist *);
} C;

static int load_curl(void)
{
    static const char *names[] = {
        "libcurl.so.5", "libcurl.so.4", "libcurl-gnutls.so.4", "libcurl.so", NULL
    };
    for (int i = 0; names[i] && !C.lib; i++)
        C.lib = dlopen(names[i], RTLD_NOW | RTLD_LOCAL);
    if (!C.lib) {
        LOGE("http: no libcurl found (%s)", dlerror());
        return -1;
    }
#define SYM(field, name) do { *(void **)&C.field = dlsym(C.lib, name); \
        if (!C.field) { LOGE("http: missing %s", name); return -1; } } while (0)
    SYM(global_init, "curl_global_init");
    SYM(global_cleanup, "curl_global_cleanup");
    SYM(easy_init, "curl_easy_init");
    SYM(easy_setopt, "curl_easy_setopt");
    SYM(easy_perform, "curl_easy_perform");
    SYM(easy_cleanup, "curl_easy_cleanup");
    SYM(easy_getinfo, "curl_easy_getinfo");
    SYM(easy_strerror, "curl_easy_strerror");
    SYM(slist_append, "curl_slist_append");
    SYM(slist_free_all, "curl_slist_free_all");
#undef SYM
    return 0;
}

/* ---- blocking request ---------------------------------------------------- */

typedef struct { char *buf; size_t len, cap; } Buf;

static size_t on_write(char *ptr, size_t size, size_t nmemb, void *user)
{
    Buf *b = user;
    size_t n = size * nmemb;
    if (b->len + n + 1 > b->cap) {
        size_t cap = b->cap ? b->cap * 2 : 16384;
        while (cap < b->len + n + 1) cap *= 2;
        char *nb = realloc(b->buf, cap);
        if (!nb) return 0;
        b->buf = nb;
        b->cap = cap;
    }
    memcpy(b->buf + b->len, ptr, n);
    b->len += n;
    b->buf[b->len] = 0;
    return n;
}

int http_request(const char *url, const char *post_json, HttpResponse *out)
{
    memset(out, 0, sizeof *out);
    if (!C.lib) { snprintf(out->error, sizeof out->error, "libcurl unavailable"); return -1; }

    CURL *h = C.easy_init();
    if (!h) { snprintf(out->error, sizeof out->error, "curl_easy_init failed"); return -1; }

    Buf b = {0};
    struct curl_slist *hdrs = NULL;
    hdrs = C.slist_append(hdrs, "Accept: application/json");
    if (post_json) hdrs = C.slist_append(hdrs, "Content-Type: application/json");

    C.easy_setopt(h, OPT_URL, url);
    C.easy_setopt(h, OPT_WRITEFUNCTION, on_write);
    C.easy_setopt(h, OPT_WRITEDATA, &b);
    C.easy_setopt(h, OPT_ERRORBUFFER, out->error);
    C.easy_setopt(h, OPT_USERAGENT, SNTV_USER_AGENT);
    C.easy_setopt(h, OPT_HTTPHEADER, hdrs);
    C.easy_setopt(h, OPT_FOLLOWLOCATION, 1L);
    C.easy_setopt(h, OPT_MAXREDIRS, 5L);
    C.easy_setopt(h, OPT_NOSIGNAL, 1L);
    C.easy_setopt(h, OPT_CONNECTTIMEOUT, 10L);
    C.easy_setopt(h, OPT_TIMEOUT, 30L);
    C.easy_setopt(h, OPT_LOW_SPEED_LIMIT, 1L);
    C.easy_setopt(h, OPT_LOW_SPEED_TIME, 15L);
    C.easy_setopt(h, OPT_ACCEPT_ENCODING, "");
    if (post_json) {
        C.easy_setopt(h, OPT_POST, 1L);
        C.easy_setopt(h, OPT_POSTFIELDS, post_json);
        C.easy_setopt(h, OPT_POSTFIELDSIZE, (long)strlen(post_json));
    }

    CURLcode rc = C.easy_perform(h);
    if (rc == 0) {
        C.easy_getinfo(h, INFO_RESPONSE_CODE, &out->status);
    } else if (!out->error[0]) {
        snprintf(out->error, sizeof out->error, "%s", C.easy_strerror(rc));
    }
    C.slist_free_all(hdrs);
    C.easy_cleanup(h);

    out->body = b.buf ? b.buf : calloc(1, 1);
    out->len = b.len;
    return rc == 0 ? 0 : -1;
}

void http_response_free(HttpResponse *r)
{
    free(r->body);
    r->body = NULL;
    r->len = 0;
}

/* ---- async worker pool --------------------------------------------------- */

typedef struct Job {
    unsigned id;
    char *url, *post;
    HttpCallback cb;
    void *user;
    HttpResponse resp;
    int cancelled;
    struct Job *next;
} Job;

enum { WORKERS = 4 };
static SDL_Thread *g_workers[WORKERS];
static SDL_mutex *g_qlock;
static SDL_cond *g_qcond;
static Job *g_pending, *g_pending_tail, *g_done, *g_inflight;
static int g_quit;
static unsigned g_next_id = 1;

static int worker_main(void *arg)
{
    (void)arg;
    for (;;) {
        SDL_LockMutex(g_qlock);
        while (!g_pending && !g_quit) SDL_CondWait(g_qcond, g_qlock);
        if (g_quit) { SDL_UnlockMutex(g_qlock); return 0; }
        Job *j = g_pending;
        g_pending = j->next;
        if (!g_pending) g_pending_tail = NULL;
        j->next = g_inflight;
        g_inflight = j;
        SDL_UnlockMutex(g_qlock);

        if (!j->cancelled) http_request(j->url, j->post, &j->resp);

        SDL_LockMutex(g_qlock);
        Job **pp = &g_inflight;
        while (*pp && *pp != j) pp = &(*pp)->next;
        if (*pp) *pp = j->next;
        j->next = g_done;
        g_done = j;
        SDL_UnlockMutex(g_qlock);
    }
}

unsigned http_request_async(const char *url, const char *post_json, HttpCallback cb, void *user)
{
    Job *j = calloc(1, sizeof *j);
    j->url = strdup(url);
    j->post = post_json ? strdup(post_json) : NULL;
    j->cb = cb;
    j->user = user;
    SDL_LockMutex(g_qlock);
    j->id = g_next_id++;
    if (g_pending_tail) g_pending_tail->next = j; else g_pending = j;
    g_pending_tail = j;
    SDL_CondSignal(g_qcond);
    SDL_UnlockMutex(g_qlock);
    return j->id;
}

void http_cancel(unsigned id)
{
    SDL_LockMutex(g_qlock);
    for (Job *j = g_pending; j; j = j->next) if (j->id == id) j->cancelled = 1;
    for (Job *j = g_inflight; j; j = j->next) if (j->id == id) j->cancelled = 1;
    for (Job *j = g_done; j; j = j->next) if (j->id == id) j->cancelled = 1;
    SDL_UnlockMutex(g_qlock);
}

static void job_free(Job *j)
{
    http_response_free(&j->resp);
    free(j->url);
    free(j->post);
    free(j);
}

void http_pump(void)
{
    SDL_LockMutex(g_qlock);
    Job *done = g_done;
    g_done = NULL;
    SDL_UnlockMutex(g_qlock);

    /* g_done is LIFO; reverse so callbacks fire in completion order. */
    Job *ordered = NULL;
    while (done) { Job *n = done->next; done->next = ordered; ordered = done; done = n; }

    while (ordered) {
        Job *j = ordered;
        ordered = j->next;
        if (!j->cancelled && j->cb) j->cb(&j->resp, j->user);
        job_free(j);
    }
}

int http_init(void)
{
    if (load_curl() != 0) return -1;
    C.global_init(GLOBAL_DEFAULT);
    g_qlock = SDL_CreateMutex();
    g_qcond = SDL_CreateCond();
    for (int i = 0; i < WORKERS; i++)
        g_workers[i] = SDL_CreateThread(worker_main, "http", NULL);
    return 0;
}

void http_shutdown(void)
{
    if (!g_qlock) return;
    SDL_LockMutex(g_qlock);
    g_quit = 1;
    SDL_CondBroadcast(g_qcond);
    SDL_UnlockMutex(g_qlock);
    for (int i = 0; i < WORKERS; i++) if (g_workers[i]) SDL_WaitThread(g_workers[i], NULL);
    for (Job *j = g_pending; j;) { Job *n = j->next; job_free(j); j = n; }
    for (Job *j = g_done; j;) { Job *n = j->next; job_free(j); j = n; }
    g_pending = g_done = NULL;
    SDL_DestroyCond(g_qcond);
    SDL_DestroyMutex(g_qlock);
    g_qlock = NULL;
    if (C.global_cleanup) C.global_cleanup();
    if (C.lib) dlclose(C.lib);
    memset(&C, 0, sizeof C);
}

char *http_url_encode(const char *src, char *dst, size_t dst_len)
{
    static const char hex[] = "0123456789ABCDEF";
    size_t o = 0;
    for (; *src && o + 4 < dst_len; src++) {
        unsigned char c = (unsigned char)*src;
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.' || c == '~') {
            dst[o++] = (char)c;
        } else {
            dst[o++] = '%';
            dst[o++] = hex[c >> 4];
            dst[o++] = hex[c & 15];
        }
    }
    dst[o] = 0;
    return dst;
}
