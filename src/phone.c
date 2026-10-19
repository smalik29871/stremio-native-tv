#include "phone.h"
#include "log.h"
#include "store.h"
#include "frames.h"
#include "http.h"
#include "../vendor/cJSON.h"
#include "youtube.h"
#include <sys/stat.h>
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#if !defined(_WIN32)
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

static int g_sock = -1, g_port;
static SDL_Thread *g_thread;
static SDL_mutex *g_lock;
static char g_title[96], g_hint[300], g_current[600];
static char g_pending[600]; static int g_has_pending;
static char g_file_slot[16];   /* "start" / "end" while a recording may be uploaded */
static volatile int g_quit;
static char *g_backup;            /* settings offered for download while the backup page is open */
static char *g_restore; static int g_restore_open;   /* settings sent back from the phone */

static void html_escape(const char *s, char *out, size_t n)
{
    size_t o = 0;
    for (; *s && o + 7 < n; s++) {
        if (*s == '<') o += (size_t)snprintf(out + o, n - o, "&lt;");
        else if (*s == '>') o += (size_t)snprintf(out + o, n - o, "&gt;");
        else if (*s == '&') o += (size_t)snprintf(out + o, n - o, "&amp;");
        else if (*s == '"') o += (size_t)snprintf(out + o, n - o, "&quot;");
        else out[o++] = *s;
    }
    out[o] = 0;
}

static int from_hex(char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; }
static void url_decode(const char *s, char *out, size_t n)
{
    size_t o = 0;
    for (; *s && o + 1 < n; s++) {
        if (*s == '+') out[o++] = ' ';
        else if (*s == '%' && from_hex(s[1]) >= 0 && from_hex(s[2]) >= 0) { out[o++] = (char)(from_hex(s[1]) * 16 + from_hex(s[2])); s += 2; }
        else out[o++] = *s;
    }
    out[o] = 0;
}

static const char *ci_find(const char *hay, const char *needle)   /* strcasestr is not in every libc */
{
    size_t n = strlen(needle);
    for (; *hay; hay++) if (strncasecmp(hay, needle, n) == 0) return hay;
    return NULL;
}

static void send_all(int fd, const char *s, size_t n) { while (n) { ssize_t w = send(fd, s, n, 0); if (w <= 0) return; s += w; n -= (size_t)w; } }

static void respond(int fd, const char *status, const char *body)
{
    char head[256];
    snprintf(head, sizeof head, "HTTP/1.1 %s\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: %zu\r\nConnection: close\r\nCache-Control: no-store\r\n\r\n", status, strlen(body));
    send_all(fd, head, strlen(head));
    send_all(fd, body, strlen(body));
}

static const char *PAGE_HEAD =
    "<!doctype html><meta charset=utf-8><meta name=viewport content='width=device-width,initial-scale=1'>"
    "<title>Stremio TV</title><style>body{font:17px/1.5 -apple-system,Segoe UI,Roboto,sans-serif;background:#0b0a16;color:#eee;margin:0;padding:28px 20px}"
    "h1{font-size:22px;margin:0 0 6px}p{color:#aaa;margin:0 0 18px}textarea,input{width:100%;box-sizing:border-box;font:17px monospace;padding:14px;border-radius:14px;border:1px solid #444;background:#17142c;color:#fff}"
    "button{margin-top:16px;width:100%;padding:16px;border:0;border-radius:999px;background:#a855f7;color:#fff;font-size:18px;font-weight:600}small{color:#777}.ok{color:#4ade80;font-weight:600}</style>";

static const char *mime_for(const char *name)
{
    const char *dot = strrchr(name, '.');
    if (!dot) return "application/octet-stream";
    if (!strcasecmp(dot, ".mp3")) return "audio/mpeg";
    if (!strcasecmp(dot, ".m4a") || !strcasecmp(dot, ".aac")) return "audio/mp4";
    if (!strcasecmp(dot, ".mp4") || !strcasecmp(dot, ".m4v")) return "video/mp4";
    if (!strcasecmp(dot, ".ogg") || !strcasecmp(dot, ".oga") || !strcasecmp(dot, ".opus")) return "audio/ogg";
    if (!strcasecmp(dot, ".wav")) return "audio/wav";
    if (!strcasecmp(dot, ".flac")) return "audio/flac";
    if (!strcasecmp(dot, ".mpd")) return "application/dash+xml";
    if (!strcasecmp(dot, ".m3u8")) return "application/vnd.apple.mpegurl";
    return "application/octet-stream";
}

static int safe_name(const char *s)   /* one path component: letters, digits, dot, dash, underscore */
{
    if (!*s || *s == '.') return 0;
    for (; *s; s++) if (!((*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z') || (*s >= '0' && *s <= '9') || *s == '.' || *s == '-' || *s == '_')) return 0;
    return 1;
}

/* GET /sounds/<name>: the stored recording, with Range so the media pipeline can seek. */
static void serve_sound(int fd, const char *req, const char *name)
{
    char path[1200];
    if (!safe_name(name)) { respond(fd, "404 Not Found", "<p>No such file"); return; }
    snprintf(path, sizeof path, "%s/sounds/%s", store_dir(), name);
    FILE *f = fopen(path, "rb");
    if (!f) { respond(fd, "404 Not Found", "<p>No such file"); return; }
    fseek(f, 0, SEEK_END); long size = ftell(f); long a = 0, b = size - 1;
    const char *rng = ci_find(req, "Range: bytes=");
    int partial = 0;
    if (rng) { long ra = -1, rb = -1; if (sscanf(rng + 13, "%ld-%ld", &ra, &rb) >= 1 && ra >= 0 && ra < size) { a = ra; if (rb >= a && rb < size) b = rb; partial = 1; } }
    char head[400];
    if (partial) snprintf(head, sizeof head, "HTTP/1.1 206 Partial Content\r\nContent-Type: %s\r\nAccept-Ranges: bytes\r\nContent-Range: bytes %ld-%ld/%ld\r\nContent-Length: %ld\r\nConnection: close\r\n\r\n", mime_for(name), a, b, size, b - a + 1);
    else snprintf(head, sizeof head, "HTTP/1.1 200 OK\r\nContent-Type: %s\r\nAccept-Ranges: bytes\r\nContent-Length: %ld\r\nConnection: close\r\n\r\n", mime_for(name), size);
    send_all(fd, head, strlen(head));
    if (strncmp(req, "HEAD", 4) != 0) {
        fseek(f, a, SEEK_SET);
        char buf[65536]; long left = b - a + 1;
        while (left > 0) { size_t r = fread(buf, 1, left > (long)sizeof buf ? sizeof buf : (size_t)left, f); if (!r) break; send_all(fd, buf, r); left -= (long)r; }
    }
    fclose(f);
}

/* POST /upload (multipart/form-data, one file field last): the body streams to a temp
 * file, then the file part is cut out of it. Keeps memory flat for a 20 MB recording. */
static int receive_upload(int fd, const char *req, const char *body0, long have, long clen, char *saved, size_t saved_n)
{
    const char *ct = ci_find(req, "Content-Type:");
    const char *bd = ct ? ci_find(ct, "boundary=") : NULL;
    if (!bd || clen <= 0 || clen > 40L * 1024 * 1024) return 0;
    char boundary[200]; snprintf(boundary, sizeof boundary, "%.190s", bd + 9);
    for (char *q = boundary; *q; q++) if (*q == '\r' || *q == '\n' || *q == ';' || *q == ' ') { *q = 0; break; }
    if (boundary[0] == '"') { memmove(boundary, boundary + 1, strlen(boundary)); char *e = strchr(boundary, '"'); if (e) *e = 0; }
    char dir[1100], tmp[1200];
    snprintf(dir, sizeof dir, "%s/sounds", store_dir()); mkdir(dir, 0755);
    snprintf(tmp, sizeof tmp, "%s/upload.tmp", dir);
    FILE *f = fopen(tmp, "wb");
    if (!f) return 0;
    long got = have;
    if (have > 0) fwrite(body0, 1, (size_t)have, f);
    char buf[65536];
    while (got < clen) { ssize_t m = recv(fd, buf, sizeof buf, 0); if (m <= 0) break; fwrite(buf, 1, (size_t)m, f); got += m; }
    fclose(f);
    if (got < clen) { remove(tmp); return 0; }
    /* the part header sits at the start: find the filename and the blank line */
    f = fopen(tmp, "rb");
    if (!f) return 0;
    char head[8192]; size_t hn = fread(head, 1, sizeof head - 1, f); head[hn] = 0;
    char *blank = strstr(head, "\r\n\r\n");
    const char *fn = ci_find(head, "filename=\"");
    if (!blank || !fn || fn > blank) { fclose(f); remove(tmp); return 0; }
    char ext[8] = ".mp3";
    { char name[200]; snprintf(name, sizeof name, "%.190s", fn + 10); char *q = strchr(name, '"'); if (q) *q = 0; const char *dot = strrchr(name, '.');
      if (dot && strlen(dot) <= 5) { snprintf(ext, sizeof ext, "%s", dot); for (char *e = ext; *e; e++) if (*e >= 'A' && *e <= 'Z') *e += 32; } }
    long data_start = (long)(blank - head) + 4;
    /* the data ends before "\r\n--boundary": walk back from the end to find it */
    fseek(f, 0, SEEK_END); long total = ftell(f);
    long tail_from = total - 512 > data_start ? total - 512 : data_start;
    fseek(f, tail_from, SEEK_SET);
    char tail[520]; size_t tn = fread(tail, 1, sizeof tail - 1, f); tail[tn] = 0;
    char mark[220]; snprintf(mark, sizeof mark, "\r\n--%s", boundary);
    long data_end = -1;
    for (long i = (long)tn - (long)strlen(mark); i >= 0; i--) if (memcmp(tail + i, mark, strlen(mark)) == 0) { data_end = tail_from + i; break; }
    if (data_end <= data_start) { fclose(f); remove(tmp); return 0; }
    char out[1200]; snprintf(out, sizeof out, "%s/%s%s", dir, g_file_slot, ext);
    FILE *o = fopen(out, "wb");
    if (!o) { fclose(f); remove(tmp); return 0; }
    fseek(f, data_start, SEEK_SET); long left = data_end - data_start;
    while (left > 0) { size_t r = fread(buf, 1, left > (long)sizeof buf ? sizeof buf : (size_t)left, f); if (!r) break; fwrite(buf, 1, r, o); left -= (long)r; }
    fclose(o); fclose(f); remove(tmp);
    /* clear other extensions of the same slot so one file per slot remains */
    { static const char *E[] = { ".mp3", ".m4a", ".aac", ".mp4", ".m4v", ".ogg", ".oga", ".opus", ".wav", ".flac" };
      for (size_t i = 0; i < sizeof E / sizeof E[0]; i++) if (strcmp(E[i], ext)) { char p2[1200]; snprintf(p2, sizeof p2, "%s/%s%s", dir, g_file_slot, E[i]); remove(p2); } }
    snprintf(saved, saved_n, "sntv-sound://%s%s", g_file_slot, ext);
    LOGI("phone: recording saved as %s (%ld bytes)", saved, data_end - data_start);
    return 1;
}

/* ---- seek previews: GET /frame?t=<s>&w=<px>&u=<video url>, from this TV only ------------
 * Decoding takes a few hundred ms, so the requests wait on their own thread, newest
 * first; when the viewer keeps seeking, the oldest waiting ones are turned away. */
typedef struct FrameJob { int fd; double t; int w; char url[2048]; struct FrameJob *next; } FrameJob;
static FrameJob *g_frames;   /* newest first */
static SDL_mutex *g_frames_lock;
static SDL_cond *g_frames_cond;
static SDL_Thread *g_frames_thread;
enum { FRAMES_WAITING = 3 };

static void respond_text(int fd, const char *status, const char *text)
{
    char head[256];
    snprintf(head, sizeof head, "HTTP/1.1 %s\r\nContent-Type: text/plain\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n", status, strlen(text));
    send_all(fd, head, strlen(head));
    send_all(fd, text, strlen(text));
}

static int frames_main(void *arg)
{
    (void)arg;
    for (;;) {
        SDL_LockMutex(g_frames_lock);
        while (!g_frames && !g_quit) SDL_CondWait(g_frames_cond, g_frames_lock);
        if (g_quit) {   /* closing: nobody is waiting for these pictures */
            while (g_frames) { FrameJob *o = g_frames; g_frames = o->next; close(o->fd); free(o); }
            SDL_UnlockMutex(g_frames_lock);
            return 0;
        }
        FrameJob *j = g_frames;
        g_frames = j->next;
        SDL_UnlockMutex(g_frames_lock);
        char err[200] = "";
        size_t len = 0;
        uint8_t *bmp = frames_picture_url(j->url, j->t, j->w, &len, err, sizeof err);
        if (bmp) {
            char head[200];
            snprintf(head, sizeof head, "HTTP/1.1 200 OK\r\nContent-Type: image/bmp\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n", len);
            send_all(j->fd, head, strlen(head));
            send_all(j->fd, (const char *)bmp, len);
            free(bmp);
        } else respond_text(j->fd, "404 Not Found", err);
        close(j->fd);
        free(j);
    }
}

/* Takes the connection (1) or leaves it to the caller (0). */
static int queue_frame(int fd, const char *req)
{
    const char *q = strchr(req, '?'), *end = q ? q + strcspn(q, " \r\n") : NULL;
    if (!q || !frames_supported()) return 0;
    FrameJob *j = calloc(1, sizeof *j);
    j->fd = fd; j->w = 384;
    for (const char *p = q + 1; p < end; ) {
        size_t len = strcspn(p, "& \r\n");
        char val[2048];
        snprintf(val, sizeof val, "%.*s", (int)(len > 2 ? len - 2 : 0), p + 2);
        if (!strncmp(p, "t=", 2)) j->t = atof(val);
        else if (!strncmp(p, "w=", 2)) { j->w = atoi(val); if (j->w < 64 || j->w > 960) j->w = 384; }
        else if (!strncmp(p, "u=", 2)) url_decode(val, j->url, sizeof j->url);
        p += len + (p[len] == '&');
    }
    if (strncmp(j->url, "http", 4) != 0) { free(j); return 0; }
    struct timeval tv = { 30, 0 };
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
    SDL_LockMutex(g_frames_lock);
    j->next = g_frames; g_frames = j;
    int n = 0;
    for (FrameJob **pp = &g_frames; *pp; ) {   /* past the newest few: turn away */
        if (++n > FRAMES_WAITING) { FrameJob *old = *pp; *pp = old->next; respond_text(old->fd, "503 Service Unavailable", "busy"); close(old->fd); free(old); }
        else pp = &(*pp)->next;
    }
    if (!g_frames_thread) g_frames_thread = SDL_CreateThread(frames_main, "frames", NULL);
    SDL_CondSignal(g_frames_cond);
    SDL_UnlockMutex(g_frames_lock);
    return 1;
}

/* ---- YouTube files, fetched the way Nuvio's player does (YoutubeChunkedDataSource) ----
 * GET /yt?u=<googlevideo url>, from this TV only. YouTube slows down and drops a
 * connection that reads a whole file at once, but serves "&range=a-b" pieces at full
 * speed, so the player's request is answered from 10 MB pieces fetched in turn. */
enum { YT_PIECE = 10 << 20 };
typedef struct { int fd; char url[4096]; uint64_t start, end; int ranged; } YtJob;   /* end: last byte asked for, 0 to the end */

static uint64_t url_clen(const char *url)
{
    const char *c = strstr(url, "clen=");
    return c && (c == url || c[-1] == '&' || c[-1] == '?') ? strtoull(c + 5, NULL, 10) : 0;
}

/* YouTube's link, as this TV's proxy serves it */
static void via_proxy(const char *abs, char *out, size_t n)
{
    char enc[4200], rel[4300];
    http_url_encode(abs, enc, sizeof enc);
    snprintf(rel, sizeof rel, "yt?u=%s", enc);
    if (!phone_local_url(rel, out, n)) snprintf(out, n, "%s", abs);
}

static void absolute(const char *base, const char *ref, char *out, size_t n)
{
    if (!strncmp(ref, "http", 4)) { snprintf(out, n, "%s", ref); return; }
    if (ref[0] == '/') {   /* from the host */
        const char *h = strstr(base, "://"), *slash = h ? strchr(h + 3, '/') : NULL;
        snprintf(out, n, "%.*s%s", slash ? (int)(slash - base) : (int)strlen(base), base, ref);
        return;
    }
    snprintf(out, n, "%s%s", base, ref);
}

/* HLS playlists: every link inside (qualities, audio tracks, pieces) is sent through
 * this proxy, so YouTube sees the User-Agent it issued them to; the master playlist keeps
 * only the tallest variants the TV takes: given a ladder, the player starts low and stays */
static void yt_playlist(YtJob *j)
{
    HttpResponse r;
    char hdr[300]; snprintf(hdr, sizeof hdr, "User-Agent: %s", youtube_ua_for(j->url));
    HttpOptions o = { .timeout_s = 15, .no_json_accept = 1, .headers = hdr };
    http_request_ex(j->url, &o, &r);
    if (r.status != 200 || !r.body) { LOGW("yt proxy: playlist %ld", r.status); respond_text(j->fd, "502 Bad Gateway", "playlist"); http_response_free(&r); return; }
    char base[4096]; snprintf(base, sizeof base, "%s", j->url);
    char *q = strchr(base, '?'); if (q) *q = 0;
    char *sl = strrchr(base, '/'); if (sl) sl[1] = 0;
    int best = 0, lowest = 0, cap_h = youtube_quality_cap();
    for (const char *c = strstr(r.body, "RESOLUTION="); c; c = strstr(c + 1, "RESOLUTION=")) {
        const char *x = strchr(c, 'x'); int h = x ? atoi(x + 1) : 0;
        if (h > 0 && h <= cap_h && h > best) best = h;
        if (h > 0 && (!lowest || h < lowest)) lowest = h;
    }
    if (!best) best = lowest;   /* nothing within the cap: the smallest there is */
    size_t cap = r.len * 3 + 65536, o2 = 0;
    char *out = malloc(cap);
    int skip_next = 0;
    char absu[4096], prox[4608], line[8192];
    for (char *p = r.body; out && p && *p; ) {
        char *nl = strchr(p, '\n');
        size_t l = nl ? (size_t)(nl - p) : strlen(p);
        snprintf(line, sizeof line, "%.*s", (int)(l < sizeof line - 1 ? l : sizeof line - 1), p);
        size_t ll = strlen(line); if (ll && line[ll - 1] == '\r') line[--ll] = 0;
        p = nl ? nl + 1 : NULL;
        if (best && !strncmp(line, "#EXT-X-STREAM-INF", 17)) {
            const char *res = strstr(line, "RESOLUTION="), *x = res ? strchr(res, 'x') : NULL;
            if (x && atoi(x + 1) != best) { skip_next = 1; continue; }
        }
        char emit[9000];
        if (!line[0]) emit[0] = 0;
        else if (line[0] != '#') {
            if (skip_next) { skip_next = 0; continue; }
            absolute(base, line, absu, sizeof absu); via_proxy(absu, prox, sizeof prox);
            snprintf(emit, sizeof emit, "%s", prox);
        } else {
            char *u = strstr(line, "URI=\"");   /* audio and subtitle renditions, keys */
            if (u) {
                char *e = strchr(u + 5, '"');
                if (e) {
                    char ref[4096]; snprintf(ref, sizeof ref, "%.*s", (int)(e - u - 5), u + 5);
                    absolute(base, ref, absu, sizeof absu); via_proxy(absu, prox, sizeof prox);
                    snprintf(emit, sizeof emit, "%.*s%s%s", (int)(u + 5 - line), line, prox, e);
                } else snprintf(emit, sizeof emit, "%s", line);
            } else snprintf(emit, sizeof emit, "%s", line);
        }
        size_t el = strlen(emit);
        if (o2 + el + 2 >= cap) break;
        memcpy(out + o2, emit, el); o2 += el; out[o2++] = '\n';
    }
    char head[200];
    snprintf(head, sizeof head, "HTTP/1.1 200 OK\r\nContent-Type: application/vnd.apple.mpegurl\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n", o2);
    send_all(j->fd, head, strlen(head));
    if (out) send_all(j->fd, out, o2);
    free(out);
    http_response_free(&r);
}

/* a whole small file (an HLS piece): fetched with the right User-Agent and passed on */
static void yt_whole(YtJob *j, const char *hdr)
{
    HttpResponse r;
    HttpOptions o = { .timeout_s = 30, .no_json_accept = 1, .headers = hdr };
    http_request_ex(j->url, &o, &r);
    if ((r.status == 200 || r.status == 206) && r.body) {
        char head[300];
        snprintf(head, sizeof head, "HTTP/1.1 200 OK\r\nContent-Type: %s\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n", r.content_type[0] ? r.content_type : "video/mp2t", r.len);
        send_all(j->fd, head, strlen(head));
        send_all(j->fd, r.body, r.len);
    } else { LOGW("yt proxy: %ld from YouTube", r.status); respond_text(j->fd, r.status ? "502 Bad Gateway" : "504 Gateway Timeout", "YouTube refused"); }
    http_response_free(&r);
}

static int yt_main(void *arg)
{
    YtJob *j = arg;
    if (strstr(j->url, ".m3u8") || strstr(j->url, "/manifest/hls")) { yt_playlist(j); close(j->fd); free(j); return 0; }
    uint64_t total = url_clen(j->url);
    char head[400], hdr[300];
    snprintf(hdr, sizeof hdr, "User-Agent: %s", youtube_ua_for(j->url));
    if (!total) yt_whole(j, hdr);   /* no length to cut pieces against: an HLS piece */
    else {
        if (j->start >= total) j->start = 0;
        uint64_t last = j->end && j->end < total ? j->end : total - 1;   /* a DASH player asks for small ranges: send just those */
        if (j->ranged)
            snprintf(head, sizeof head, "HTTP/1.1 206 Partial Content\r\nContent-Type: video/mp4\r\nAccept-Ranges: bytes\r\nContent-Range: bytes %llu-%llu/%llu\r\nContent-Length: %llu\r\nConnection: close\r\n\r\n",
                     (unsigned long long)j->start, (unsigned long long)last, (unsigned long long)total, (unsigned long long)(last + 1 - j->start));
        else
            snprintf(head, sizeof head, "HTTP/1.1 200 OK\r\nContent-Type: video/mp4\r\nAccept-Ranges: bytes\r\nContent-Length: %llu\r\nConnection: close\r\n\r\n", (unsigned long long)total);
        size_t ul = strlen(j->url) + 64;
        char *piece_url = malloc(ul);
        uint64_t stop = j->ranged ? last + 1 : total;
        int sent_head = 0;
        for (uint64_t pos = j->start; pos < stop && piece_url; ) {
            uint64_t end = pos + YT_PIECE - 1 < stop - 1 ? pos + YT_PIECE - 1 : stop - 1;
            snprintf(piece_url, ul, "%s&range=%llu-%llu", j->url, (unsigned long long)pos, (unsigned long long)end);
            HttpResponse r;
            HttpOptions o = { .timeout_s = 60, .no_json_accept = 1, .headers = hdr };
            http_request_ex(piece_url, &o, &r);
            int ok = (r.status == 200 || r.status == 206) && r.len > 0;
            if (!sent_head) {   /* the first piece decides: a refusal is reported, not a cut-off stream */
                if (!ok) { LOGW("yt proxy: YouTube answered %ld", r.status); respond_text(j->fd, r.status == 403 ? "403 Forbidden" : "502 Bad Gateway", "YouTube refused"); http_response_free(&r); break; }
                send_all(j->fd, head, strlen(head)); sent_head = 1;
            }
            if (ok) { ssize_t w = 0; size_t left = r.len; const char *b = r.body;
                      while (left && (w = send(j->fd, b, left, MSG_NOSIGNAL)) > 0) { b += w; left -= (size_t)w; }
                      if (left) ok = 0; }
            pos += r.len;
            http_response_free(&r);
            if (!ok) break;   /* the player stopped reading, or YouTube refused */
        }
        free(piece_url);
    }
    close(j->fd);
    free(j);
    return 0;
}

static int queue_yt(int fd, const char *req)
{
    const char *q = strstr(req, "?u="), *end = q ? q + 3 + strcspn(q + 3, " \r\n&") : NULL;
    if (!q) return 0;
    YtJob *j = calloc(1, sizeof *j);
    char raw[4096]; snprintf(raw, sizeof raw, "%.*s", (int)(end - q - 3), q + 3);
    url_decode(raw, j->url, sizeof j->url);
    if (!strstr(j->url, "googlevideo.com") && !getenv("SNTV_YT_BASE")) { free(j); return 0; }
    const char *rg = ci_find(req, "\nRange: bytes=");
    if (rg) {
        char *dash = NULL;
        j->start = strtoull(rg + 14, &dash, 10); j->ranged = 1;
        if (dash && *dash == '-' && dash[1] >= '0' && dash[1] <= '9') j->end = strtoull(dash + 1, NULL, 10);
    }
    j->fd = fd;
    SDL_Thread *t = SDL_CreateThread(yt_main, "yt", j);
    if (!t) { free(j); return 0; }
    SDL_DetachThread(t);
    return 1;
}

/* Settings backup: GET /backup.json while the TV offers one; POST /restore takes one back. */
static int handle_backup(int fd, char *req, ssize_t n)
{
    if (strncmp(req, "GET /backup.json", 16) == 0) {
        SDL_LockMutex(g_lock);
        char *b = g_backup ? strdup(g_backup) : NULL;
        SDL_UnlockMutex(g_lock);
        if (!b) { respond(fd, "404 Not Found", "<p>Open Settings > Back up or restore on the TV first."); return 1; }
        char head[300];
        snprintf(head, sizeof head, "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Disposition: attachment; filename=stremio-tv-settings.json\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n", strlen(b));
        send_all(fd, head, strlen(head));
        send_all(fd, b, strlen(b));
        free(b);
        return 1;
    }
    if (strncmp(req, "POST /restore", 13) != 0) return 0;
    char *body = strstr(req, "\r\n\r\n");
    const char *cl = ci_find(req, "Content-Length:");
    long clen = cl ? atol(cl + 15) : 0;
    SDL_LockMutex(g_lock); int open = g_restore_open; SDL_UnlockMutex(g_lock);
    if (!body || clen <= 0 || clen > 256 * 1024 || !open) { respond(fd, "409 Conflict", "<p>Open Settings > Back up or restore on the TV, then send it again."); return 1; }
    body += 4;
    long have = n - (body - req);
    char *buf = malloc((size_t)clen + 1);
    if (!buf) return 1;
    memcpy(buf, body, (size_t)(have < clen ? have : clen));
    while (have < clen) { ssize_t m = recv(fd, buf + have, (size_t)(clen - have), 0); if (m <= 0) break; have += m; }
    buf[have < clen ? have : clen] = 0;
    /* the form posts text=<urlencoded json> */
    char *v = strstr(buf, "text="), *json = NULL;
    if (v) { size_t L = strlen(v + 5) + 1; json = malloc(L); if (json) url_decode(v + 5, json, L); }
    free(buf);
    char page[1000];
    if (json && json[0] == '{') {
        SDL_LockMutex(g_lock); free(g_restore); g_restore = json; SDL_UnlockMutex(g_lock);
        snprintf(page, sizeof page, "%s<h1>Sent to the TV</h1><p class=ok>The TV is restoring these settings. You can close this page.</p>", PAGE_HEAD);
        respond(fd, "200 OK", page);
    } else {
        free(json);
        snprintf(page, sizeof page, "%s<h1>That is not a settings backup</h1><p>Choose the stremio-tv-settings.json file this page saved before.</p>", PAGE_HEAD);
        respond(fd, "400 Bad Request", page);
    }
    return 1;
}

static int handle(int fd, const struct sockaddr_in *peer)
{
    char req[8192];
    ssize_t n = recv(fd, req, sizeof req - 1, 0);
    if (n <= 0) return 0;
    req[n] = 0;
    if (handle_backup(fd, req, n)) return 0;
    if (strncmp(req, "GET /yt?", 8) == 0) {
        if (peer->sin_addr.s_addr == htonl(INADDR_LOOPBACK) && queue_yt(fd, req)) return 1;
        respond_text(fd, "404 Not Found", "no YouTube files here");
        return 0;
    }
    if (strncmp(req, "GET /frame?", 11) == 0) {
        if (peer->sin_addr.s_addr == htonl(INADDR_LOOPBACK) && queue_frame(fd, req)) return 1;
        respond_text(fd, "404 Not Found", "no previews here");
        return 0;
    }
    if (strncmp(req, "GET /sounds/", 12) == 0 || strncmp(req, "HEAD /sounds/", 13) == 0) {
        char name[200]; snprintf(name, sizeof name, "%.190s", strchr(req, '/') + 8);
        char *sp = strpbrk(name, " ?\r\n"); if (sp) *sp = 0;
        serve_sound(fd, req, name);
        return 0;
    }
    if (strncmp(req, "POST /upload", 12) == 0) {
        char *b = strstr(req, "\r\n\r\n");
        long clen = 0; const char *cl = ci_find(req, "Content-Length:"); if (cl) clen = atol(cl + 15);
        if (!b) { respond(fd, "400 Bad Request", "<p>Bad upload"); return 0; }
        b += 4;
        SDL_LockMutex(g_lock); int can = g_file_slot[0] != 0; SDL_UnlockMutex(g_lock);
        char saved[80] = "", page[1200];
        if (can && receive_upload(fd, req, b, n - (b - req), clen, saved, sizeof saved)) {
            SDL_LockMutex(g_lock);
            snprintf(g_pending, sizeof g_pending, "%s", saved); g_has_pending = 1;
            SDL_UnlockMutex(g_lock);
            snprintf(page, sizeof page, "%s<h1>Recording saved on the TV</h1><p class=ok>It plays at the start of each break. You can close this page.</p>", PAGE_HEAD);
            respond(fd, "200 OK", page);
        } else {
            snprintf(page, sizeof page, "%s<h1>That did not work</h1><p>Choose an audio file under 40 MB (MP3, M4A, OGG, WAV) while the recording field is open on the TV, then try again.</p>", PAGE_HEAD);
            respond(fd, can ? "400 Bad Request" : "409 Conflict", page);
        }
        return 0;
    }
    /* read the rest of a POST body when it did not arrive in one go */
    char *body = strstr(req, "\r\n\r\n");
    long clen = 0;
    const char *cl = ci_find(req, "Content-Length:");
    if (cl) clen = atol(cl + 15);
    if (body) {
        body += 4;
        long have = n - (body - req);
        while (have < clen && n < (ssize_t)sizeof req - 1) { ssize_t m = recv(fd, req + n, sizeof req - 1 - n, 0); if (m <= 0) break; n += m; req[n] = 0; have += m; }
    }
    int post = strncmp(req, "POST", 4) == 0;
    SDL_LockMutex(g_lock);
    char title[96], hint[300], current[600], slot[16];
    snprintf(title, sizeof title, "%s", g_title); snprintf(hint, sizeof hint, "%s", g_hint); snprintf(current, sizeof current, "%s", g_current); snprintf(slot, sizeof slot, "%s", g_file_slot);
    SDL_UnlockMutex(g_lock);
    char page[5000], et[256], eh[700], ec[1500];
    if (post && body && title[0]) {
        const char *v = strstr(body, "text=");
        char decoded[600] = "";
        if (v) { char raw[600]; snprintf(raw, sizeof raw, "%s", v + 5); char *amp = strchr(raw, '&'); if (amp) *amp = 0; url_decode(raw, decoded, sizeof decoded); }
        /* trim */
        char *s = decoded; while (*s == ' ' || *s == '\n' || *s == '\r' || *s == '\t') s++;
        size_t len = strlen(s); while (len && (s[len - 1] == ' ' || s[len - 1] == '\n' || s[len - 1] == '\r' || s[len - 1] == '\t')) s[--len] = 0;
        SDL_LockMutex(g_lock);
        snprintf(g_pending, sizeof g_pending, "%s", s); g_has_pending = 1;
        SDL_UnlockMutex(g_lock);
        html_escape(title, et, sizeof et);
        snprintf(page, sizeof page, "%s<h1>Sent to the TV</h1><p class=ok>%s is filled in. You can close this page.</p>", PAGE_HEAD, et);
        respond(fd, "200 OK", page);
        return 0;
    }
    SDL_LockMutex(g_lock); int backup_open = g_restore_open; SDL_UnlockMutex(g_lock);
    if (backup_open) {
        snprintf(page, sizeof page, "%s<h1>Back up your TV's settings</h1><p>Themes, home rows, subtitle and source choices, breaks. Not your Stremio sign-in.</p>"
                 "<a href=/backup.json download><button type=button>Save a backup to this phone</button></a>"
                 "<h1 style='margin-top:34px'>Restore a backup</h1><p>Choose a stremio-tv-settings.json saved before.</p>"
                 "<form method=post action=/restore id=f><input type=file accept='.json,application/json' id=file><textarea name=text id=t hidden></textarea><button type=submit>Restore on the TV</button></form>"
                 "<script>document.getElementById('file').onchange=function(e){var r=new FileReader();r.onload=function(){document.getElementById('t').value=r.result};r.readAsText(e.target.files[0])}</script>", PAGE_HEAD);
        respond(fd, "200 OK", page);
        return 0;
    }
    if (!title[0]) {
        snprintf(page, sizeof page, "%s<h1>Stremio TV</h1><p>Nothing to fill in right now. Open a text field on the TV (Settings, a key or a URL) and reload this page.</p>", PAGE_HEAD);
        respond(fd, "200 OK", page);
        return 0;
    }
    html_escape(title, et, sizeof et); html_escape(hint, eh, sizeof eh); html_escape(current, ec, sizeof ec);
    char upload[900] = "";
    if (slot[0]) snprintf(upload, sizeof upload, "<h1 style='margin-top:34px'>Or send a recording from this phone</h1><p>An MP3, M4A, OGG or WAV you have (an adhan, a dua, a bell). It is stored on the TV and plays even without internet.</p>"
                          "<form method=post action=/upload enctype=multipart/form-data><input type=file name=f accept='audio/*,video/mp4,.mp3,.m4a,.ogg,.wav'><button type=submit>Send this recording to the TV</button></form>");
    snprintf(page, sizeof page, "%s<h1>%s</h1><p>%s</p><form method=post action=/><textarea name=text rows=4 autofocus autocapitalize=off autocorrect=off spellcheck=false>%s</textarea><button type=submit>Send to the TV</button></form>%s<p><small>This page is served by your TV on your home network and only works while that field is open.</small></p>",
             PAGE_HEAD, et, eh, ec, upload);
    respond(fd, "200 OK", page);
    return 0;
}

static int serve(void *arg)
{
    (void)arg;
    while (!g_quit) {
        struct sockaddr_in peer; socklen_t pl = sizeof peer;
        int fd = accept(g_sock, (struct sockaddr *)&peer, &pl);
        if (fd < 0) { if (g_quit) break; SDL_Delay(50); continue; }
        struct timeval tv = { 5, 0 };
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
        if (!handle(fd, &peer)) close(fd);
    }
    return 0;
}

void phone_start(void)
{
    if (getenv("SNTV_NO_PHONE")) return;
    g_lock = SDL_CreateMutex();
    g_frames_lock = SDL_CreateMutex();
    g_frames_cond = SDL_CreateCond();
    g_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (g_sock < 0) return;
    int one = 1;
    setsockopt(g_sock, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    for (g_port = 11471; g_port < 11480; g_port++) {
        struct sockaddr_in a = { 0 };
        a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(INADDR_ANY); a.sin_port = htons((uint16_t)g_port);
        if (bind(g_sock, (struct sockaddr *)&a, sizeof a) == 0) break;
    }
    if (g_port >= 11480 || listen(g_sock, 4) != 0) { close(g_sock); g_sock = -1; LOGW("phone: could not listen"); return; }
    g_thread = SDL_CreateThread(serve, "phone", NULL);
    char url[96];
    if (phone_url(url, sizeof url)) LOGI("phone: type-on-your-phone page at %s", url);
}

void phone_stop(void)
{
    g_quit = 1;
    if (g_sock >= 0) { shutdown(g_sock, SHUT_RDWR); close(g_sock); g_sock = -1; }
    if (g_thread) SDL_WaitThread(g_thread, NULL);
    if (g_frames_lock) {
        SDL_LockMutex(g_frames_lock);
        SDL_CondSignal(g_frames_cond);
        SDL_UnlockMutex(g_frames_lock);
        if (g_frames_thread) SDL_WaitThread(g_frames_thread, NULL);   /* finishes the picture it is on */
    }
}

int phone_url(char *out, size_t n)
{
    if (g_sock < 0) return 0;
    const char *override = getenv("SNTV_PHONE_HOST");   /* tests */
    char ip[64] = "";
    if (override) snprintf(ip, sizeof ip, "%s", override);
    else {
        struct ifaddrs *ifa = NULL;
        if (getifaddrs(&ifa) == 0) {
            for (struct ifaddrs *p = ifa; p; p = p->ifa_next) {
                if (!p->ifa_addr || p->ifa_addr->sa_family != AF_INET) continue;
                struct sockaddr_in *sa = (struct sockaddr_in *)p->ifa_addr;
                uint32_t h = ntohl(sa->sin_addr.s_addr);
                if ((h >> 24) == 127) continue;
                inet_ntop(AF_INET, &sa->sin_addr, ip, sizeof ip);
                if ((h >> 24) == 192 || (h >> 24) == 10 || ((h >> 24) == 172 && ((h >> 16) & 0xF0) == 16)) break;   /* prefer the LAN address */
            }
            freeifaddrs(ifa);
        }
    }
    if (!ip[0]) return 0;
    snprintf(out, n, "http://%s:%d/", ip, g_port);
    return 1;
}

void phone_prompt(const char *title, const char *hint, const char *current)
{
    if (!g_lock) return;
    SDL_LockMutex(g_lock);
    snprintf(g_title, sizeof g_title, "%s", title ? title : "");
    snprintf(g_hint, sizeof g_hint, "%s", hint ? hint : "");
    snprintf(g_current, sizeof g_current, "%s", current ? current : "");
    g_has_pending = 0;
    g_file_slot[0] = 0;
    SDL_UnlockMutex(g_lock);
}

void phone_accept_file(const char *slot)
{
    if (!g_lock) return;
    SDL_LockMutex(g_lock);
    snprintf(g_file_slot, sizeof g_file_slot, "%s", slot ? slot : "");
    SDL_UnlockMutex(g_lock);
}

int phone_local_url(const char *rel, char *out, size_t n)
{
    if (g_sock < 0) return 0;
    snprintf(out, n, "http://127.0.0.1:%d/%s", g_port, rel);
    return 1;
}

const char *phone_resolve_sound(const char *url, char *out, size_t n)
{
    if (!url) return NULL;
    if (strncmp(url, "sntv-sound://", 13) == 0) {
        char rel[220]; snprintf(rel, sizeof rel, "sounds/%s", url + 13);
        if (!phone_local_url(rel, out, n)) return NULL;
        return out;
    }
    return url;
}

int phone_take(char *out, size_t n)
{
    if (!g_lock) return 0;
    int got = 0;
    SDL_LockMutex(g_lock);
    if (g_has_pending) { snprintf(out, n, "%s", g_pending); g_has_pending = 0; got = 1; }
    SDL_UnlockMutex(g_lock);
    return got;
}

void phone_offer_backup(const char *json)
{
    if (!g_lock) return;
    SDL_LockMutex(g_lock);
    free(g_backup); g_backup = json ? strdup(json) : NULL;
    g_restore_open = json != NULL;
    if (!json) { free(g_restore); g_restore = NULL; }
    SDL_UnlockMutex(g_lock);
}

char *phone_take_restore(void)
{
    if (!g_lock) return NULL;
    SDL_LockMutex(g_lock);
    char *r = g_restore; g_restore = NULL;
    SDL_UnlockMutex(g_lock);
    return r;
}

/* youtube.h: a YouTube file goes through /yt in 10 MB range pieces; a playlist as it is */
void youtube_player_url(const char *u, char *out, size_t n)
{
    snprintf(out, n, "%s", u);
    if (strlen(u) > 3000) return;   /* files go in pieces; playlists are served without their low variants */
    char enc[4200], rel[4300];
    http_url_encode(u, enc, sizeof enc);
    snprintf(rel, sizeof rel, "yt?u=%s", enc);
    if (!phone_local_url(rel, out, n)) snprintf(out, n, "%s", u);
}

/* ---- what the player opens for a YouTube video (youtube_stream's answer) ---- */
static void xml_attr(const char *in, char *out, size_t n)
{
    size_t o = 0;
    for (; *in && o + 6 < n; in++) {
        if (*in == '&') { memcpy(out + o, "&amp;", 5); o += 5; }
        else if (*in == '<') { memcpy(out + o, "&lt;", 4); o += 4; }
        else if (*in == '"') { memcpy(out + o, "&quot;", 6); o += 6; }
        else out[o++] = *in;
    }
    out[o] = 0;
}

static void dash_rep(char **buf, size_t *len, const cJSON *rp, int video, int id)
{
    const cJSON *u = cJSON_GetObjectItemCaseSensitive(rp, "url");
    char local[4600], esc[9000];
    youtube_player_url(u->valuestring, local, sizeof local);
    xml_attr(local, esc, sizeof esc);
    const char *codecs = cJSON_GetObjectItemCaseSensitive(rp, "codecs")->valuestring;
    const char *init = cJSON_GetObjectItemCaseSensitive(rp, "init")->valuestring, *index = cJSON_GetObjectItemCaseSensitive(rp, "index")->valuestring;
    double bw = cJSON_GetObjectItemCaseSensitive(rp, "bitrate")->valuedouble;
    char attrs[200];
    if (video) snprintf(attrs, sizeof attrs, "width=\"%d\" height=\"%d\" frameRate=\"%d\"", (int)cJSON_GetObjectItemCaseSensitive(rp, "width")->valuedouble,
                        (int)cJSON_GetObjectItemCaseSensitive(rp, "height")->valuedouble, (int)cJSON_GetObjectItemCaseSensitive(rp, "fps")->valuedouble);
    else snprintf(attrs, sizeof attrs, "audioSamplingRate=\"%d\"", (int)cJSON_GetObjectItemCaseSensitive(rp, "audioSampleRate")->valuedouble);
    size_t need = strlen(esc) + 600;
    *buf = realloc(*buf, *len + need);
    *len += (size_t)snprintf(*buf + *len, need,
        "   <Representation id=\"%c%d\" bandwidth=\"%.0f\" codecs=\"%s\" %s>\n    <BaseURL>%s</BaseURL>\n"
        "    <SegmentBase indexRange=\"%s\"><Initialization range=\"%s\"/></SegmentBase>\n   </Representation>\n",
        video ? 'v' : 'a', id, bw > 0 ? bw : 1000000, codecs, attrs, esc, index, init);
}

int youtube_player_urls(const char *answer, const char *id, char out[][4608], int max)
{
    cJSON *j = cJSON_Parse(answer);
    int n = 0;
    if (!j) { if (max > 0 && answer && !strncmp(answer, "http", 4)) { youtube_player_url(answer, out[0], 4608); n = 1; } return n; }
    const cJSON *d = cJSON_GetObjectItemCaseSensitive(j, "dash");
    const cJSON *h = cJSON_GetObjectItemCaseSensitive(j, "hls"), *f = cJSON_GetObjectItemCaseSensitive(j, "file");
    /* DASH failed twice running on this TV: the playlist first, DASH again every fifth time */
    int plays = store_get_int("ytPlays", 0) + 1;
    store_set_int("ytPlays", plays);
    int hls_first = cJSON_IsString(h) && store_get_int("ytDashFails", 0) >= 2 && plays % 5;
    if (hls_first && n < max) { youtube_player_url(h->valuestring, out[n++], 4608); LOGI("youtube: %s playlist first (DASH failed here before)", id); }
    if (d && n < max) {
        const cJSON *video = cJSON_GetObjectItemCaseSensitive(d, "video"), *audio = cJSON_GetObjectItemCaseSensitive(d, "audio"), *rp;
        double dur = cJSON_GetObjectItemCaseSensitive(d, "duration") ? cJSON_GetObjectItemCaseSensitive(d, "duration")->valuedouble : 0;
        size_t len = 0; char *buf = NULL;
        char head[600];
        snprintf(head, sizeof head, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\" profiles=\"urn:mpeg:dash:profile:isoff-on-demand:2011\" type=\"static\" minBufferTime=\"PT2S\" mediaPresentationDuration=\"PT%.1fS\">\n <Period>\n  <AdaptationSet mimeType=\"video/mp4\" segmentAlignment=\"true\" startWithSAP=\"1\">\n", dur);
        buf = strdup(head); len = strlen(buf);
        int k = 0;
        cJSON_ArrayForEach(rp, video) dash_rep(&buf, &len, rp, 1, k++);
        const char *mid = "  </AdaptationSet>\n  <AdaptationSet mimeType=\"audio/mp4\" segmentAlignment=\"true\" startWithSAP=\"1\">\n";
        buf = realloc(buf, len + strlen(mid) + 1); memcpy(buf + len, mid, strlen(mid) + 1); len += strlen(mid);
        dash_rep(&buf, &len, audio, 0, 0);
        const char *tail = "  </AdaptationSet>\n </Period>\n</MPD>\n";
        buf = realloc(buf, len + strlen(tail) + 1); memcpy(buf + len, tail, strlen(tail) + 1); len += strlen(tail);
        char path[700], name[64];
        snprintf(name, sizeof name, "yt-%.16s.mpd", id);
        snprintf(path, sizeof path, "%s/sounds", store_dir()); mkdir(path, 0755);
        snprintf(path, sizeof path, "%s/sounds/%s", store_dir(), name);
        FILE *f = fopen(path, "wb");
        if (f) {
            fwrite(buf, 1, len, f); fclose(f);
            char rel[96]; snprintf(rel, sizeof rel, "sounds/%s", name);
            const cJSON *v0 = cJSON_GetArrayItem(video, 0), *vh = cJSON_GetObjectItemCaseSensitive(v0, "height"), *vf = cJSON_GetObjectItemCaseSensitive(v0, "fps");
            if (phone_local_url(rel, out[n], 4608)) { LOGI("youtube: %s as DASH at %dp%s (%d video %s)", id, cJSON_IsNumber(vh) ? vh->valueint : 0, cJSON_IsNumber(vf) && vf->valueint > 30 ? "60" : "", k, k == 1 ? "quality" : "qualities"); n++; }
        }
        free(buf);
    }
    if (!hls_first && cJSON_IsString(h) && n < max) youtube_player_url(h->valuestring, out[n++], 4608);
    if (cJSON_IsString(f) && n < max) youtube_player_url(f->valuestring, out[n++], 4608);
    cJSON_Delete(j);
    return n;
}
