#include "phone.h"
#include "log.h"
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

static void handle(int fd)
{
    char req[8192];
    ssize_t n = recv(fd, req, sizeof req - 1, 0);
    if (n <= 0) return;
    req[n] = 0;
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
    char title[96], hint[300], current[600];
    snprintf(title, sizeof title, "%s", g_title); snprintf(hint, sizeof hint, "%s", g_hint); snprintf(current, sizeof current, "%s", g_current);
    SDL_UnlockMutex(g_lock);
    char page[4096], et[256], eh[700], ec[1500];
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
    snprintf(page, sizeof page, "%s<h1>%s</h1><p>%s</p><form method=post action=/><textarea name=text rows=4 autofocus autocapitalize=off autocorrect=off spellcheck=false>%s</textarea><button type=submit>Send to the TV</button></form><p><small>This page is served by your TV on your home network and only works while that field is open.</small></p>",
             PAGE_HEAD, et, eh, ec);
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
    SDL_UnlockMutex(g_lock);
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
