#include "http.h"
#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(c) do { if (!(c)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

/* An answer as a check or the start race sees it: status, type and its first bytes. */
static int media(long status, const char *type, const char *body, size_t len)
{
    HttpResponse r = { 0 };
    r.status = status;
    snprintf(r.content_type, sizeof r.content_type, "%s", type);
    r.body = (char *)body;
    r.len = len;
    return http_looks_like_media(&r);
}
#define M(st, ct, lit) media(st, ct, lit, sizeof lit - 1)

int main(void)
{
    /* files */
    CHECK(M(206, "video/x-matroska", "\x1a\x45\xdf\xa3\x01\x00\x00\x00"));
    CHECK(M(206, "", "\x1a\x45\xdf\xa3\x01\x00\x00\x00"));   /* no type: the bytes are a file */
    CHECK(M(206, "application/octet-stream", "\x00\x00\x00\x20" "ftypisom"));
    CHECK(M(200, "video/mp4", "\x00\x00\x00\x20" "ftypisom"));
    /* manifests, whatever they are served as */
    CHECK(M(200, "application/vnd.apple.mpegurl", "#EXTM3U\n#EXT-X-VERSION:3\n"));
    CHECK(M(200, "text/plain", "#EXTM3U\n"));
    CHECK(M(200, "application/dash+xml", "<?xml version=\"1.0\"?><MPD>"));
    CHECK(M(206, "application/dash+xml", "<?"));   /* two bytes of one: its type says so */
    /* errors */
    CHECK(!M(200, "text/html", "<html><body>Expired</body></html>"));
    CHECK(!M(200, "", "<html><body>Expired</body></html>"));
    CHECK(!M(206, "", "<h"));   /* two bytes of a page with no type */
    CHECK(!M(200, "text/plain", "Not found"));
    CHECK(!M(200, "", "Link expired, generate a new one"));
    CHECK(!M(200, "application/octet-stream", "Error: file not available"));
    CHECK(!M(200, "application/json", "{\"error\":\"expired\"}"));
    CHECK(!M(200, "", "{\"error\":\"expired\"}"));
    CHECK(!M(404, "video/mp4", "\x00\x00\x00\x20" "ftypisom"));
    CHECK(!M(200, "video/mp4", ""));
    /* the subtitle hash's two 64 KB pieces: the start, and the end of a 1 MB file */
    static char piece[70000];
    const uint64_t size = 1 << 20, tail = size - 65536;
    HttpResponse h = { 0 };
    h.body = piece;
    h.status = 206; h.len = 65536; h.has_range = 1; h.range_from = 0;
    CHECK(http_has_bytes(&h, 0, 65536));                 /* the start, as asked */
    h.range_from = tail;
    CHECK(http_has_bytes(&h, tail, 65536));              /* the end, as asked */
    h.status = 200; h.has_range = 0; h.range_from = 0; h.len = 65536;
    CHECK(http_has_bytes(&h, 0, 65536));                 /* Range ignored: the start is still the start */
    CHECK(!http_has_bytes(&h, tail, 65536));             /* ...but not the end: the tail range was ignored */
    h.status = 206; h.has_range = 1; h.range_from = tail - 4096;
    CHECK(!http_has_bytes(&h, tail, 65536));             /* the wrong offset */
    h.range_from = tail; h.len = 8;
    CHECK(!http_has_bytes(&h, tail, 65536));             /* a short piece */
    h.len = 65535;
    CHECK(!http_has_bytes(&h, tail, 65536));
    h.len = 65536; h.has_range = 0;
    CHECK(!http_has_bytes(&h, tail, 65536));             /* 206 saying nothing of where: not the end */
    h.has_range = 1; h.status = 404;
    CHECK(!http_has_bytes(&h, tail, 65536));
    if (failures) { fprintf(stderr, "%d failure(s)\n", failures); return 1; }
    printf("http checks ok\n");
    return 0;
}
