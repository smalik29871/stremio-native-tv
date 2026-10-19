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
    char content_type[64];
    void *parsed;       /* set by HttpOptions.work on the worker; the callback may take it (set NULL) */
} HttpResponse;

/* Runs on the network thread after the transfer, before the main-thread callback:
 * parse JSON, write a cache file. Must not touch UI or g_app state. */
typedef void (*HttpWorkFn)(HttpResponse *r, const char *url);
typedef void (*HttpFreeFn)(void *parsed);

/* Extra knobs for http_request_async_ex; zero means the default. */
typedef struct HttpOptions {
    uint64_t range_bytes;   /* fetch only N bytes (Range: bytes=start-(start+N-1)) */
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
} HttpOptions;

int  http_init(void);          /* 0 on success */
void http_shutdown(void);

/* Blocking. Pass post_json == NULL for GET. Caller frees with http_response_free. */
int  http_request(const char *url, const char *post_json, HttpResponse *out);
int  http_request_ex(const char *url, const HttpOptions *opt, HttpResponse *out);
void http_response_free(HttpResponse *r);

/* Async: the request runs on a worker thread and `cb` is invoked on the main
 * thread from http_pump(), which the main loop calls once per frame. */
typedef void (*HttpCallback)(HttpResponse *resp, void *user);
unsigned http_request_async(const char *url, const char *post_json, HttpCallback cb, void *user);
unsigned http_request_async_ex(const char *url, const HttpOptions *opt, HttpCallback cb, void *user);
void http_cancel(unsigned id);   /* the callback will not fire */
/* Long work on a network worker (several blocking requests, a file download): `task`
 * fills the response (status, body, error, parsed) and `cb` gets it on the main thread.
 * `label` names it in the perf log. */
typedef void (*HttpTaskFn)(HttpResponse *r, void *arg);
unsigned http_task_async(const char *label, HttpTaskFn task, void *arg, HttpCallback cb, void *user);
void http_pump(void);
int  http_pending_replies(void);   /* replies held for the next frame (keep drawing) */
int  http_online(void);            /* 0 when transport failures repeat and nothing got through for 15 s */

/* Convenience: percent-encode into dst, returns dst. */
char *http_url_encode(const char *src, char *dst, size_t dst_len);

#endif
