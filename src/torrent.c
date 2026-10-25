#include "torrent.h"
#include "http.h"
#include "log.h"
#include "store.h"
#include <SDL.h>
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

static int engine_port(void)   /* 11490; tests running apps side by side give each its own */
{
    static int port;
    if (!port) { const char *e = getenv("SNTV_ENGINE_PORT"); port = e && atoi(e) > 1024 ? atoi(e) : 11490; }
    return port;
}
static const char *engine_base(void)   /* "http://127.0.0.1:11490/" */
{
    static char base[40];
    if (!base[0]) snprintf(base, sizeof base, "http://127.0.0.1:%d/", engine_port());
    return base;
}

static pid_t g_pid;
static int g_cache_mb = 160;

static const char *engine_path(void)
{
    static char path[600];
    static int done;
    if (!done) {
        done = 1;
        const char *env = getenv("SNTV_TORRENTD");   /* tests and desktop builds */
        char *base = SDL_GetBasePath();
        if (env && *env) snprintf(path, sizeof path, "%s", env);
        else snprintf(path, sizeof path, "%storrentd", base ? base : "");
        if (base) SDL_free(base);
        if (access(path, X_OK) != 0) path[0] = 0;
    }
    return path;
}

int torrent_engine_available(void) { return engine_path()[0] != 0; }

static void on_cache(HttpResponse *r, void *user) { (void)r; (void)user; }

/* The engine's RAM for pieces: for the next start, and resized now while it runs. */
void torrent_engine_set_cache_mb(int mb)
{
    if (mb < 32 || mb == g_cache_mb) return;
    g_cache_mb = mb;
    if (g_pid > 0) {
        char url[80]; snprintf(url, sizeof url, "%scache?mb=%d", engine_base(), mb);
        http_request_async(url, NULL, on_cache, NULL);
        LOGI("torrent: the engine's cache set to %d MB", mb);
    }
}

char *torrent_engine_url(const char *info_hash, int file_idx, const char *trackers)
{
    if (!info_hash || !torrent_engine_available()) return NULL;
    size_t n = strlen(engine_base()) + 1 + strlen(info_hash) + (trackers ? strlen(trackers) : 0) + 32;
    char *u = malloc(n);
    snprintf(u, n, "%s%s/%d%s%s", engine_base(), info_hash, file_idx < 0 ? -1 : file_idx, trackers ? "?" : "", trackers ? trackers : "");
    for (char *c = u + strlen(engine_base()); *c && *c != '/'; c++) if (*c >= 'A' && *c <= 'F') *c += 32;   /* the hash, lower case */
    return u;
}

int torrent_engine_url_is(const char *url) { return url && !strncmp(url, engine_base(), strlen(engine_base())); }

static int answers(void)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return 0;
    struct sockaddr_in a = { 0 };
    a.sin_family = AF_INET; a.sin_port = htons((uint16_t)engine_port()); a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    int ok = connect(fd, (struct sockaddr *)&a, sizeof a) == 0;
    close(fd);
    return ok;
}

static int alive(void)
{
    if (g_pid <= 0) return 0;
    int st;
    if (waitpid(g_pid, &st, WNOHANG) == 0) return 1;
    g_pid = 0;   /* it stopped (idle) and is reaped now */
    return 0;
}

int torrent_engine_start(int wait_ms)
{
    if (!torrent_engine_available()) return 0;
    if (!alive() && !answers()) {
        char log_path[1100], cache[16], ppid[16], addr[32];
        snprintf(log_path, sizeof log_path, "%s/torrentd.log", store_dir());
        snprintf(cache, sizeof cache, "%d", g_cache_mb);
        snprintf(ppid, sizeof ppid, "%d", (int)getpid());
        snprintf(addr, sizeof addr, "127.0.0.1:%d", engine_port());
        const char *path = engine_path();
        pid_t pid = fork();
        if (pid == 0) {   /* only exec-safe calls from here */
            int fd = open(log_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (fd >= 0) { dup2(fd, 1); dup2(fd, 2); close(fd); }
            execl(path, "torrentd", "-addr", addr, "-cache", cache, "-ppid", ppid, (char *)NULL);
            _exit(127);
        }
        if (pid < 0) { LOGW("torrent: could not start the engine (%s)", strerror(errno)); return 0; }
        g_pid = pid;
        LOGI("torrent: engine started (pid %d, %d MB of RAM for pieces)", (int)pid, g_cache_mb);
    }
    for (int waited = 0; ; waited += 25) {
        if (answers()) return 1;
        if (waited >= wait_ms || !alive()) return 0;
        SDL_Delay(25);
    }
}

void torrent_engine_drop(const char *url)
{
    if (!torrent_engine_url_is(url) || !alive()) return;
    char drop[200];
    const char *h = url + strlen(engine_base());
    snprintf(drop, sizeof drop, "%s%.40s/drop", engine_base(), h);
    http_request_async(drop, NULL, NULL, NULL);
}

void torrent_engine_stop(void)
{
    if (!alive()) return;
    kill(g_pid, SIGTERM);
    for (int i = 0; i < 20; i++) { int st; if (waitpid(g_pid, &st, WNOHANG) == g_pid) { g_pid = 0; return; } SDL_Delay(25); }
    kill(g_pid, SIGKILL);
    waitpid(g_pid, NULL, 0);
    g_pid = 0;
}
