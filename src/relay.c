/* See relay.h. One listening thread on 127.0.0.1; each request the player makes (the
 * first one, and one per seek) gets its own thread, which asks the source for the same
 * bytes with the addon's headers and hands them on until either side stops. Nothing
 * here touches the screen or the player: when a request can't be served it answers
 * 502, and the player's usual fallbacks take it from there. */
#include "relay.h"
#include "http.h"
#include "log.h"
#include "store.h"
#include <SDL.h>
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

enum { MAX_ROUTES = 8, MAX_CONNS = 12 };   /* seeks open new requests while old ones close */
typedef struct { char token[33]; char *url, *headers; } Route;

static Route g_routes[MAX_ROUTES];
static int g_next_route;
static SDL_mutex *g_lock;
static int g_sock = -1, g_port, g_failed;
static SDL_atomic_t g_conns;

int relay_enabled(void)
{
    return !getenv("SNTV_NO_RELAY") && store_get_int("relay", 1);
}

static void host_of(const char *url, char *out, size_t n)   /* the host only: a link can carry a key */
{
    const char *hs = strstr(url, "://"); hs = hs ? hs + 3 : url;
    snprintf(out, n, "%.*s", (int)strcspn(hs, "/:?"), hs);
}

static int send_all(int fd, const char *p, size_t n)
{
    while (n) {
        ssize_t k = send(fd, p, n, MSG_NOSIGNAL);
        if (k < 0 && errno == EINTR) continue;
        if (k <= 0) return 0;
        p += k; n -= (size_t)k;
    }
    return 1;
}

typedef struct { int fd, head_only, sent_head; long status; } Conn;

static int on_head(void *user, long status, const char *type, uint64_t length, const char *content_range)
{
    Conn *c = user;
    char h[512];
    int n = snprintf(h, sizeof h, "HTTP/1.1 %ld %s\r\nContent-Type: %s\r\nAccept-Ranges: bytes\r\nConnection: close\r\n",
                     status, status == 206 ? "Partial Content" : status == 200 ? "OK" : "Upstream", type && type[0] ? type : "application/octet-stream");
    if (length != (uint64_t)-1) n += snprintf(h + n, sizeof h - (size_t)n, "Content-Length: %llu\r\n", (unsigned long long)length);
    if (content_range && content_range[0]) n += snprintf(h + n, sizeof h - (size_t)n, "Content-Range: %s\r\n", content_range);
    n += snprintf(h + n, sizeof h - (size_t)n, "\r\n");
    c->sent_head = 1;
    c->status = status;
    return send_all(c->fd, h, (size_t)n) && !c->head_only;
}

static int on_data(void *user, const char *data, size_t n)
{
    Conn *c = user;
    return send_all(c->fd, data, n);
}

static void answer(int fd, const char *status)
{
    char h[160];
    int n = snprintf(h, sizeof h, "HTTP/1.1 %s\r\nContent-Length: 0\r\nConnection: close\r\n\r\n", status);
    send_all(fd, h, (size_t)n);
}

static int serve_one(void *arg)
{
    int fd = (int)(intptr_t)arg;
    struct timeval tv = { 5, 0 };
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    struct timeval wt = { 300, 0 };   /* paused: the player stops reading; after 5 min it reconnects when played again */
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &wt, sizeof wt);
    char req[4096];
    size_t got = 0;
    while (got < sizeof req - 1) {
        ssize_t k = recv(fd, req + got, sizeof req - 1 - got, 0);
        if (k <= 0) break;
        got += (size_t)k;
        req[got] = 0;
        if (strstr(req, "\r\n\r\n")) break;
    }
    req[got] = 0;
    char token[33] = "", range[64] = "";
    int head_only = !strncmp(req, "HEAD ", 5);
    const char *path = strchr(req, ' ');
    if (path && !strncmp(path + 1, "/r/", 3)) snprintf(token, sizeof token, "%.*s", (int)strcspn(path + 4, " ?\r\n"), path + 4);
    for (const char *l = strstr(req, "\r\n"); l && l[2]; l = strstr(l + 2, "\r\n"))
        if (!strncasecmp(l + 2, "Range:", 6)) {
            const char *v = l + 8;
            while (*v == ' ') v++;
            if (!strncasecmp(v, "bytes=", 6)) snprintf(range, sizeof range, "%.*s", (int)strcspn(v + 6, "\r\n,"), v + 6);   /* one range: what players ask */
        }
    char *url = NULL, *headers = NULL;
    SDL_LockMutex(g_lock);
    for (int i = 0; token[0] && i < MAX_ROUTES; i++)
        if (g_routes[i].url && !strcmp(g_routes[i].token, token)) { url = strdup(g_routes[i].url); headers = g_routes[i].headers ? strdup(g_routes[i].headers) : NULL; }
    SDL_UnlockMutex(g_lock);
    if (!url) answer(fd, "404 Not Found");
    else {
        Conn c = { .fd = fd, .head_only = head_only };
        HttpStream cb = { on_head, on_data, &c };
        char host[96];
        host_of(url, host, sizeof host);
        int rc = http_stream(url, headers, range[0] ? range : NULL, &cb);
        if (rc != 0 && !c.sent_head) { answer(fd, "502 Bad Gateway"); LOGW("relay: %s did not answer", host); }
        else if (c.status >= 400) LOGW("relay: %s answered %ld", host, c.status);
        else LOGI("relay: %s%s%s, %ld", host, range[0] ? " from " : "", range, c.status);
    }
    free(url); free(headers);
    shutdown(fd, SHUT_RDWR);
    close(fd);
    SDL_AtomicAdd(&g_conns, -1);
    return 0;
}

static int listen_loop(void *arg)
{
    (void)arg;
    for (;;) {
        int fd = accept(g_sock, NULL, NULL);
        if (fd < 0) { if (errno == EINTR) continue; SDL_Delay(50); continue; }
        if (SDL_AtomicGet(&g_conns) >= MAX_CONNS) { answer(fd, "503 Busy"); close(fd); continue; }   /* old requests still closing */
        SDL_AtomicAdd(&g_conns, 1);
        SDL_Thread *t = SDL_CreateThread(serve_one, "relay", (void *)(intptr_t)fd);
        if (t) SDL_DetachThread(t);
        else { answer(fd, "503 Busy"); close(fd); SDL_AtomicAdd(&g_conns, -1); }
    }
    return 0;
}

static int start(void)
{
    if (g_sock >= 0) return 1;
    if (g_failed) return 0;   /* tried once this run: the direct link from now on */
    g_failed = 1;
    if (!g_lock && !(g_lock = SDL_CreateMutex())) return 0;
    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) { LOGW("relay: no socket; sources open directly"); return 0; }
    int one = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    int port;
    for (port = 11481; port < 11490; port++) {   /* 11471-79 the phone page, 11490 the P2P engine */
        struct sockaddr_in a = { 0 };
        a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(INADDR_LOOPBACK); a.sin_port = htons((uint16_t)port);   /* this TV only */
        if (bind(s, (struct sockaddr *)&a, sizeof a) == 0) break;
    }
    if (port >= 11490 || listen(s, 8) != 0) { close(s); LOGW("relay: could not listen; sources open directly"); return 0; }
    g_sock = s; g_port = port;
    SDL_Thread *t = SDL_CreateThread(listen_loop, "relay-listen", NULL);
    if (!t) { close(s); g_sock = -1; LOGW("relay: no thread; sources open directly"); return 0; }
    SDL_DetachThread(t);
    g_failed = 0;
    LOGI("relay: listening on 127.0.0.1:%d", port);
    return 1;
}

static void new_token(char out[33])
{
    unsigned char b[16];
    FILE *f = fopen("/dev/urandom", "rb");
    size_t got = f ? fread(b, 1, sizeof b, f) : 0;
    if (f) fclose(f);
    if (got != sizeof b) {   /* no urandom: time and counter; still only reachable from this TV */
        Uint64 x = SDL_GetPerformanceCounter() ^ ((Uint64)SDL_GetTicks() << 32);
        for (size_t i = 0; i < sizeof b; i++) { x = x * 6364136223846793005ULL + 1442695040888963407ULL; b[i] = (unsigned char)(x >> 56); }
    }
    for (size_t i = 0; i < sizeof b; i++) snprintf(out + i * 2, 3, "%02x", b[i]);
}

char *relay_url(const char *url, const char *headers)
{
    if (!url || strncmp(url, "http", 4) || !relay_enabled() || !start()) return NULL;
    SDL_LockMutex(g_lock);
    Route *r = NULL;
    for (int i = 0; i < MAX_ROUTES; i++)   /* the same source again (a reconnect): its address again */
        if (g_routes[i].url && !strcmp(g_routes[i].url, url) && !strcmp(g_routes[i].headers ? g_routes[i].headers : "", headers ? headers : "")) r = &g_routes[i];
    if (!r) {
        r = &g_routes[g_next_route];
        g_next_route = (g_next_route + 1) % MAX_ROUTES;
        free(r->url); free(r->headers);
        r->url = strdup(url);
        r->headers = headers ? strdup(headers) : NULL;
        new_token(r->token);
    }
    char *out = malloc(64);
    snprintf(out, 64, "http://127.0.0.1:%d/r/%s", g_port, r->token);
    SDL_UnlockMutex(g_lock);
    return out;
}
