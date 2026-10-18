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
    OPT_RANGE = 10007, OPT_HEADERFUNCTION = 20079, OPT_HEADERDATA = 10029,
    INFO_RESPONSE_CODE = 0x200002,
    GLOBAL_DEFAULT = 3,
};

static struct {
    void *lib;
    CURLcode (*global_init)(long);
    void (*global_cleanup)(void);
    CURL *(*easy_init)(void);
    CURLcode (*easy_setopt)(CURL *, int, ...);
    void (*easy_reset)(CURL *);
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
    SYM(easy_reset, "curl_easy_reset");
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

/* A handle per worker keeps TCP/TLS sessions alive between requests, which on
 * the TV's old libcurl is the difference between 80 ms and 600 ms per poster. */
static size_t on_header(char *ptr, size_t size, size_t nmemb, void *user)
{
    HttpResponse *out = user;
    size_t n = size * nmemb;
    char line[256];
    if (n >= sizeof line) n = sizeof line - 1;
    memcpy(line, ptr, n);
    line[n] = 0;
    if (!strncasecmp(line, "Content-Range:", 14)) {
        const char *slash = strrchr(line, '/');
        if (slash && slash[1] != '*') out->total_size = strtoull(slash + 1, NULL, 10);
    } else if (!strncasecmp(line, "Content-Length:", 15) && !out->total_size) {
        out->total_size = strtoull(line + 15, NULL, 10);
    } else if (!strncasecmp(line, "Content-Type:", 13)) {
        const char *v = line + 13;
        while (*v == ' ') v++;
        snprintf(out->content_type, sizeof out->content_type, "%.63s", v);
        out->content_type[strcspn(out->content_type, ";\r\n")] = 0;
    }
    return size * nmemb;
}

/* Offline only when requests keep failing at the transport level AND nothing at all got
 * through for 15 s: one dead addon host while others answer is not "no internet". */
static volatile int g_net_fail;            /* transport failures since the last success */
static volatile Uint32 g_last_ok;          /* ticks of the last request that got any answer */
int http_online(void)
{
    if (getenv("SNTV_FAKE_OFFLINE")) return 0;
    return g_net_fail < 3 || SDL_GetTicks() - g_last_ok < 15000;
}

static int request_with(CURL *h, const char *url, const char *post_json, const HttpOptions *opt, HttpResponse *out)
{
    memset(out, 0, sizeof *out);
    C.easy_reset(h);

    Buf b = {0};
    struct curl_slist *hdrs = NULL;
    if (!opt || !opt->no_json_accept) hdrs = C.slist_append(hdrs, "Accept: application/json");
    if (post_json) hdrs = C.slist_append(hdrs, "Content-Type: application/json");
    if (opt && opt->bearer && opt->bearer[0]) { char *auth = malloc(strlen(opt->bearer) + 32); sprintf(auth, "Authorization: Bearer %s", opt->bearer); hdrs = C.slist_append(hdrs, auth); free(auth); }
    char range[64];
    if (opt && opt->range_bytes) {
        snprintf(range, sizeof range, "%llu-%llu", (unsigned long long)opt->range_start, (unsigned long long)(opt->range_start + opt->range_bytes - 1));
        C.easy_setopt(h, OPT_RANGE, range);
    }
    C.easy_setopt(h, OPT_HEADERFUNCTION, on_header);
    C.easy_setopt(h, OPT_HEADERDATA, out);

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
    C.easy_setopt(h, OPT_TIMEOUT, opt && opt->timeout_s ? opt->timeout_s : 30L);
    C.easy_setopt(h, OPT_LOW_SPEED_LIMIT, 1L);
    C.easy_setopt(h, OPT_LOW_SPEED_TIME, 15L);
    C.easy_setopt(h, OPT_ACCEPT_ENCODING, "");
    if (post_json) {
        C.easy_setopt(h, OPT_POST, 1L);
        C.easy_setopt(h, OPT_POSTFIELDS, post_json);
        C.easy_setopt(h, OPT_POSTFIELDSIZE, (long)strlen(post_json));
    }

    Uint32 t0 = SDL_GetTicks();
    CURLcode rc = C.easy_perform(h);
    /* Any bytes back mean the network works, even when a slow addon or a header probe
     * then times out. Only resolve / connect / TLS failures with nothing received count. */
    if (rc == 0 || b.len > 0) { g_net_fail = 0; g_last_ok = SDL_GetTicks(); }
    else if (rc == 6 || rc == 7 || rc == 28 || rc == 35) { if (g_net_fail < 1000) g_net_fail++; }
    out->seconds = (SDL_GetTicks() - t0) / 1000.0;
    if (rc == 0) {
        C.easy_getinfo(h, INFO_RESPONSE_CODE, &out->status);
    } else if (!out->error[0]) {
        snprintf(out->error, sizeof out->error, "%s", C.easy_strerror(rc));
    }
    C.slist_free_all(hdrs);

    out->body = b.buf ? b.buf : calloc(1, 1);
    out->len = b.len;
    return rc == 0 ? 0 : -1;
}

int http_request(const char *url, const char *post_json, HttpResponse *out)
{
    if (!C.lib) { memset(out, 0, sizeof *out); snprintf(out->error, sizeof out->error, "libcurl unavailable"); return -1; }
    CURL *h = C.easy_init();
    if (!h) { memset(out, 0, sizeof *out); snprintf(out->error, sizeof out->error, "curl_easy_init failed"); return -1; }
    int rc = request_with(h, url, post_json, NULL, out);
    C.easy_cleanup(h);
    return rc;
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
    HttpOptions opt;
    int cancelled;
    struct Job *next;
} Job;

enum { WORKERS = 6 };
static SDL_Thread *g_workers[WORKERS];
static SDL_mutex *g_qlock;
static SDL_cond *g_qcond;
/* Two lanes: API/JSON requests in order, and artwork newest-first (what is on screen
 * now beats what scrolled past). Workers always drain the first lane before the second,
 * so pressing OK on a title isn't queued behind fifty posters. */
static Job *g_pending, *g_pending_tail, *g_artwork, *g_done, *g_inflight;
static int g_quit;
static unsigned g_next_id = 1;

static int worker_main(void *arg)
{
    (void)arg;
    CURL *h = C.easy_init();
    for (;;) {
        SDL_LockMutex(g_qlock);
        while (!g_pending && !g_artwork && !g_quit) SDL_CondWait(g_qcond, g_qlock);
        if (g_quit) { SDL_UnlockMutex(g_qlock); if (h) C.easy_cleanup(h); return 0; }
        Job *j;
        if (g_pending) {
            j = g_pending;
            g_pending = j->next;
            if (!g_pending) g_pending_tail = NULL;
        } else {
            j = g_artwork;
            g_artwork = j->next;
        }
        j->next = g_inflight;
        g_inflight = j;
        SDL_UnlockMutex(g_qlock);

        if (!j->cancelled) {
            if (h) request_with(h, j->url, j->post, &j->opt, &j->resp);
            else http_request(j->url, j->post, &j->resp);
            if (j->opt.work && !j->cancelled) j->opt.work(&j->resp, j->url);
        }

        SDL_LockMutex(g_qlock);
        Job **pp = &g_inflight;
        while (*pp && *pp != j) pp = &(*pp)->next;
        if (*pp) *pp = j->next;
        j->next = g_done;
        g_done = j;
        SDL_UnlockMutex(g_qlock);
    }
}

static unsigned enqueue(const char *url, const char *post_json, const HttpOptions *opt, HttpCallback cb, void *user)
{
    Job *j = calloc(1, sizeof *j);
    j->url = strdup(url);
    if (!post_json && opt && opt->post_json) post_json = opt->post_json;
    j->post = post_json ? strdup(post_json) : NULL;
    if (opt) j->opt = *opt;
    j->opt.post_json = NULL;
    j->opt.bearer = opt && opt->bearer ? strdup(opt->bearer) : NULL;
    j->cb = cb;
    j->user = user;
    SDL_LockMutex(g_qlock);
    j->id = g_next_id++;
    if (j->opt.background) { j->next = g_artwork; g_artwork = j; }
    else {
        if (g_pending_tail) g_pending_tail->next = j; else g_pending = j;
        g_pending_tail = j;
    }
    SDL_CondSignal(g_qcond);
    SDL_UnlockMutex(g_qlock);
    return j->id;
}

unsigned http_request_async(const char *url, const char *post_json, HttpCallback cb, void *user)
{
    return enqueue(url, post_json, NULL, cb, user);
}

unsigned http_request_async_ex(const char *url, const HttpOptions *opt, HttpCallback cb, void *user)
{
    return enqueue(url, NULL, opt, cb, user);
}

void http_cancel(unsigned id)
{
    SDL_LockMutex(g_qlock);
    for (Job *j = g_pending; j; j = j->next) if (j->id == id) j->cancelled = 1;
    for (Job *j = g_artwork; j; j = j->next) if (j->id == id) j->cancelled = 1;
    for (Job *j = g_inflight; j; j = j->next) if (j->id == id) j->cancelled = 1;
    for (Job *j = g_done; j; j = j->next) if (j->id == id) j->cancelled = 1;
    SDL_UnlockMutex(g_qlock);
}

static void job_free(Job *j)
{
    http_response_free(&j->resp);
    if (j->resp.parsed && j->opt.free_parsed) j->opt.free_parsed(j->resp.parsed);
    j->resp.parsed = NULL;
    free(j->url);
    free(j->post);
    free((char *)j->opt.bearer);
    free(j);
}

/* Replies are handed to the UI with a time budget per frame: a burst of answers
 * spreads over a few frames instead of stalling one. Slow callbacks are logged with
 * their host so a TV's Diagnostics shows what was heavy. */
static Job *g_carry;   /* ordered, left over from the last frame */

void http_pump(void)
{
    SDL_LockMutex(g_qlock);
    Job *done = g_done;
    g_done = NULL;
    SDL_UnlockMutex(g_qlock);

    /* g_done is LIFO; reverse so callbacks fire in completion order, after the carry-over. */
    Job *ordered = NULL;
    while (done) { Job *n = done->next; done->next = ordered; ordered = done; done = n; }
    if (g_carry) { Job *t = g_carry; while (t->next) t = t->next; t->next = ordered; ordered = g_carry; g_carry = NULL; }

    Uint32 start = SDL_GetTicks();
    while (ordered) {
        Job *j = ordered;
        ordered = j->next;
        if (!j->cancelled && j->cb) {
            Uint32 t0 = SDL_GetTicks();
            j->cb(&j->resp, j->user);
            Uint32 took = SDL_GetTicks() - t0;
            if (took > 12) {
                const char *hs = strstr(j->url, "://"); hs = hs ? hs + 3 : j->url;
                LOGW("perf: %u ms handling the reply from %.*s (%zu bytes)", took, (int)strcspn(hs, "/:?"), hs, j->resp.len);
            }
        }
        job_free(j);
        if (ordered && SDL_GetTicks() - start > 8) { g_carry = ordered; break; }
    }
}

int http_pending_replies(void) { return g_carry != NULL; }

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
    for (Job *j = g_artwork; j;) { Job *n = j->next; job_free(j); j = n; }
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
