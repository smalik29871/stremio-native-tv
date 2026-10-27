#ifndef SNTV_HTTP_H
#define SNTV_HTTP_H
#include <stddef.h>
#include <stdint.h>

/* HTTP client on top of libcurl loaded with dlopen. LG ships libcurl.so.5 while
 * the SDK ships .so.4, and Tizen ships yet another soname, so we never link it. */

typedef struct HttpResponse {
    long status;        /* 0 when the transport failed */
    char *body;         /* NUL-terminated, owned by the response */
    size_t len;
    char error[256];
    double seconds;     /* wall time of the transfer, connection included */
    uint64_t total_size; /* from Content-Range (range requests) or Content-Length; 0 unknown */
    int has_range;       /* answered with a Content-Range: */
    char final_url[1024]; /* where redirects led, when that is not the address asked; "" none */
    double first_byte_s;  /* until the first byte arrived (0 unknown): the rest of `seconds` is the transfer */
    uint64_t range_from; /* the offset its bytes start at */
    char content_type[64];
    int cut;            /* the body was cut at its budget (a server that ignored Range, or a runaway answer) */
    void *parsed;       /* set by HttpOptions.work on the worker; the callback may take it (set NULL) */
    void *user;         /* the request's user pointer, for HttpOptions.work to read (not to use) */
} HttpResponse;

/* Runs on the network thread after the transfer, before the main-thread callback:
 * parse JSON, write a cache file. Must not touch UI or g_app state. */
typedef void (*HttpWorkFn)(HttpResponse *r, const char *url);
typedef void (*HttpFreeFn)(void *parsed);

/* Extra knobs for http_request_async_ex; zero means the default. */
typedef struct HttpOptions {
    uint64_t range_bytes;   /* fetch only N bytes (Range: bytes=start-(start+N-1)); the body is cut there too,
                               as a server may ignore Range and send the whole file */
    uint64_t max_bytes;     /* the body's budget otherwise (0: HTTP_MAX_BODY) */
    uint64_t range_start;   /* from this offset (0 = the start of the file) */
    long timeout_s;         /* whole-transfer timeout */
    int no_json_accept;     /* don't ask for application/json */
    int background;         /* artwork: waits behind API requests, newest first */
    int deliver;            /* must reach the server: http_shutdown waits for it (watch progress) */
    const char *post_json;  /* POST this body (copied at enqueue time) */
    const char *bearer;     /* Authorization: Bearer <token> (copied) */
    const char *headers;    /* more request headers, "Name: value" lines joined by \n (copied); a Content-Type here replaces JSON's;
                               a User-Agent line replaces the app's */
    HttpWorkFn work;        /* off the main thread: see above */
    HttpFreeFn free_parsed; /* frees r->parsed when the callback left it (or was cancelled) */
    const volatile int *stop; /* blocking calls: give up once this is set */
    void **keep;            /* blocking calls from one thread: the connection stays open here for the next (http_keep_free) */
} HttpOptions;
void http_keep_free(void **keep);

int  http_init(void);          /* 0 on success */
/* Addresses answered inside the app instead of over the network (cuelight://...): `answer`
 * runs where the transfer would (a network thread, or the caller's for blocking calls).
 * Set once at start, before any request. */
typedef int (*HttpLocalMatch)(const char *url);
typedef int (*HttpLocalAnswer)(const char *url, HttpResponse *out);
void http_set_local(HttpLocalMatch handles, HttpLocalAnswer answer);
void http_shutdown(void);

/* Blocking. Pass post_json == NULL for GET. Caller frees with http_response_free. */
int  http_request(const char *url, const char *post_json, HttpResponse *out);
int  http_request_ex(const char *url, const HttpOptions *opt, HttpResponse *out);
void http_response_free(HttpResponse *r);
/* An answer to a range request that looks like the start of a video (or a playlist):
 * 200/206, not an HTML or JSON error page served with a 200. */
int  http_looks_like_media(const HttpResponse *r);
/* The answer holds len bytes of the file from `from`: a server that ignored the Range sent
 * the start instead, and a short or shifted answer is not that piece. */
int  http_has_bytes(const HttpResponse *r, uint64_t from, size_t len);
/* A long download handed on as it arrives, on the calling thread (the local relay).
 * on_head comes once, before any data, with the final answer's status, type, length
 * ((uint64_t)-1 unknown) and Content-Range ("" none); returning 0 stops it. on_data
 * returning 0 stops it too. range: "123-" or NULL. 0 when the source answered (whatever
 * its status), -1 when nothing came back at all. */
typedef struct {
    int (*on_head)(void *user, long status, const char *type, uint64_t length, const char *content_range);
    int (*on_data)(void *user, const char *data, size_t n);
    void *user;
} HttpStream;
int  http_stream(const char *url, const char *headers, const char *range, const HttpStream *cb);

/* Async: the request runs on a worker thread and `cb` is invoked on the main
 * thread from http_pump(), which the main loop calls once per frame. */
typedef void (*HttpCallback)(HttpResponse *resp, void *user);
unsigned http_request_async(const char *url, const char *post_json, HttpCallback cb, void *user);
unsigned http_request_async_ex(const char *url, const HttpOptions *opt, HttpCallback cb, void *user);
void http_cancel(unsigned id);   /* the callback will not fire */
void http_hold_background(int hold);   /* protect playback while its buffer is low */
/* Cancels it only while it waits for a worker (nothing downloaded yet): 1 if it did. */
int http_cancel_queued(unsigned id);
/* Long work on a network worker (several blocking requests, a file download): `task`
 * fills the response (status, body, error, parsed) and `cb` gets it on the main thread.
 * `label` names it in the perf log. */
typedef void (*HttpTaskFn)(HttpResponse *r, void *arg);
unsigned http_task_async(const char *label, HttpTaskFn task, void *arg, HttpCallback cb, void *user);
/* The same, with what frees r->parsed when the callback left it there (or never ran). */
unsigned http_task_async_ex(const char *label, HttpTaskFn task, void *arg, HttpFreeFn free_parsed, HttpCallback cb, void *user);
void http_pump(void);
int  http_pending_replies(void);   /* replies held for the next frame (keep drawing) */
int  http_online(void);            /* 0 when transport failures repeat and nothing got through for 15 s */

/* Convenience: percent-encode into dst, returns dst. */
char *http_url_encode(const char *src, char *dst, size_t dst_len);

#endif
