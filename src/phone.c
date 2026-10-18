#include "phone.h"
#include "log.h"
#include "store.h"
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

static void handle(int fd)
{
    char req[8192];
    ssize_t n = recv(fd, req, sizeof req - 1, 0);
    if (n <= 0) return;
    req[n] = 0;
    if (strncmp(req, "GET /sounds/", 12) == 0 || strncmp(req, "HEAD /sounds/", 13) == 0) {
        char name[200]; snprintf(name, sizeof name, "%.190s", strchr(req, '/') + 8);
        char *sp = strpbrk(name, " ?\r\n"); if (sp) *sp = 0;
        serve_sound(fd, req, name);
        return;
    }
    if (strncmp(req, "POST /upload", 12) == 0) {
        char *b = strstr(req, "\r\n\r\n");
        long clen = 0; const char *cl = ci_find(req, "Content-Length:"); if (cl) clen = atol(cl + 15);
        if (!b) { respond(fd, "400 Bad Request", "<p>Bad upload"); return; }
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
        return;
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
        return;
    }
    if (!title[0]) {
        snprintf(page, sizeof page, "%s<h1>Stremio TV</h1><p>Nothing to fill in right now. Open a text field on the TV (Settings, a key or a URL) and reload this page.</p>", PAGE_HEAD);
        respond(fd, "200 OK", page);
        return;
    }
    html_escape(title, et, sizeof et); html_escape(hint, eh, sizeof eh); html_escape(current, ec, sizeof ec);
    char upload[900] = "";
    if (slot[0]) snprintf(upload, sizeof upload, "<h1 style='margin-top:34px'>Or send a recording from this phone</h1><p>An MP3, M4A, OGG or WAV you have (an adhan, a dua, a bell). It is stored on the TV and plays even without internet.</p>"
                          "<form method=post action=/upload enctype=multipart/form-data><input type=file name=f accept='audio/*,video/mp4,.mp3,.m4a,.ogg,.wav'><button type=submit>Send this recording to the TV</button></form>");
    snprintf(page, sizeof page, "%s<h1>%s</h1><p>%s</p><form method=post action=/><textarea name=text rows=4 autofocus autocapitalize=off autocorrect=off spellcheck=false>%s</textarea><button type=submit>Send to the TV</button></form>%s<p><small>This page is served by your TV on your home network and only works while that field is open.</small></p>",
             PAGE_HEAD, et, eh, ec, upload);
    respond(fd, "200 OK", page);
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
        handle(fd);
        close(fd);
    }
    return 0;
}

void phone_start(void)
{
    if (getenv("SNTV_NO_PHONE")) return;
    g_lock = SDL_CreateMutex();
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
