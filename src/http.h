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
} HttpResponse;

/* Extra knobs for http_request_async_ex; zero means the default. */
typedef struct HttpOptions {
    uint64_t range_bytes;   /* fetch only the first N bytes (Range: bytes=0-(N-1)) */
    long timeout_s;         /* whole-transfer timeout */
    int no_json_accept;     /* don't ask for application/json */
    int background;         /* artwork: waits behind API requests, newest first */
} HttpOptions;

int  http_init(void);          /* 0 on success */
void http_shutdown(void);

/* Blocking. Pass post_json == NULL for GET. Caller frees with http_response_free. */
int  http_request(const char *url, const char *post_json, HttpResponse *out);
void http_response_free(HttpResponse *r);

/* Async: the request runs on a worker thread and `cb` is invoked on the main
 * thread from http_pump(), which the main loop calls once per frame. */
typedef void (*HttpCallback)(HttpResponse *resp, void *user);
unsigned http_request_async(const char *url, const char *post_json, HttpCallback cb, void *user);
unsigned http_request_async_ex(const char *url, const HttpOptions *opt, HttpCallback cb, void *user);
void http_cancel(unsigned id);   /* the callback will not fire */
void http_pump(void);

/* Convenience: percent-encode into dst, returns dst. */
char *http_url_encode(const char *src, char *dst, size_t dst_len);

#endif
