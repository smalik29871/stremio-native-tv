#include "log.h"
#include "sysinfo.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <SDL.h>
#ifdef __ANDROID__
#include <android/log.h>
#endif

static FILE *g_file;
static long g_written;
static char g_path[1024];
enum { LOG_FILE_MAX = 4 << 20 };
static SDL_mutex *g_lock;
static LogLine g_ring[LOG_RING_LINES];
static int g_head, g_count;
static unsigned g_version;
static char g_prev_path[1024];

void log_init(const char *dir)
{
    char path[1024];
    g_lock = SDL_CreateMutex();
    if (!dir) return;
    snprintf(path, sizeof path, "%s/app.log", dir);
    snprintf(g_path, sizeof g_path, "%s", path);
    /* Rotate once so a crash log from the previous run survives. */
    snprintf(g_prev_path, sizeof g_prev_path, "%s/app.prev.log", dir);
    rename(path, g_prev_path);
    g_file = fopen(path, "w");
}

/* Diagnostics are shareable: keep a link's host, never its credentials or path. */
static void redact(char *line)
{
    for (char *p = strstr(line, "://"); p; p = strstr(p, "://")) {
        char *host = p + 3;
        char *end = host + strcspn(host, " \t\r\n\"'<>\\)");
        char *authority = host;
        while (authority < end && *authority != '/' && *authority != '?' && *authority != '#') authority++;
        char *at = NULL;
        for (char *c = host; c < authority; c++) if (*c == '@') at = c;
        if (at) {
            size_t removed = (size_t)(at + 1 - host);
            memmove(host, at + 1, strlen(at + 1) + 1);
            authority -= removed; end -= removed;
        }
        if (authority < end) {
            *authority = '*';
            memmove(authority + 1, end, strlen(end) + 1);
            end = authority + 1;
        }
        p = end;
    }
    for (char *p = strstr(line, "token="); p; p = strstr(p, "token=")) {
        p += 6;
        size_t n = strcspn(p, "&\" \t\r\n)");
        if (!n) continue;
        memmove(p + 1, p + n, strlen(p + n) + 1);
        *p = '*';
    }
}

void log_msg(const char *level, const char *fmt, ...)
{
    char line[2048 + 8];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    redact(line);

    time_t now = time(NULL);
    struct tm tmv;
    localtime_r(&now, &tmv);
    char ts[32];
    strftime(ts, sizeof ts, "%H:%M:%S", &tmv);

    if (g_lock) SDL_LockMutex(g_lock);
#ifdef __ANDROID__
    __android_log_print(level[0] == 'E' ? ANDROID_LOG_ERROR : level[0] == 'W' ? ANDROID_LOG_WARN : ANDROID_LOG_INFO, "Cuelight", "%s", line);   /* adb logcat */
#endif
    fprintf(stderr, "%s %s %s\n", ts, level, line);
    if (g_file) {
        int n = fprintf(g_file, "%s %s %s\n", ts, level, line);
        fflush(g_file);
        if (n > 0) g_written += n;
        if (g_written > LOG_FILE_MAX) {   /* days of uptime: start the file again rather than fill the flash */
            g_file = freopen(g_path, "w", g_file);
            g_written = 0;
            if (g_file) fprintf(g_file, "%s I log: the file passed %d MB and was started again\n", ts, LOG_FILE_MAX >> 20);
        }
    }
    LogLine *l = &g_ring[g_head];
    l->level = level && level[0] ? level[0] : 'I';
    snprintf(l->text, sizeof l->text, "%.8s %.*s", ts, LOG_LINE_MAX - 12, line);
    g_head = (g_head + 1) % LOG_RING_LINES;
    if (g_count < LOG_RING_LINES) g_count++;
    g_version++;
    if (g_lock) SDL_UnlockMutex(g_lock);
}

int log_snapshot(LogLine *out, int max)
{
    if (g_lock) SDL_LockMutex(g_lock);
    int n = g_count < max ? g_count : max;
    int start = (g_head - n + LOG_RING_LINES) % LOG_RING_LINES;
    for (int i = 0; i < n; i++) out[i] = g_ring[(start + i) % LOG_RING_LINES];
    if (g_lock) SDL_UnlockMutex(g_lock);
    return n;
}

unsigned log_version(void)
{
    if (g_lock) SDL_LockMutex(g_lock);
    unsigned v = g_version;
    if (g_lock) SDL_UnlockMutex(g_lock);
    return v;
}

const char *log_previous_path(void) { return g_prev_path; }

/* The open log file, for a crash handler that must not lock or allocate */
int log_fd(void) { return g_file ? fileno(g_file) : -1; }

void log_system_info(char *out, size_t n)
{
    char rel[200]; const char *src;
    sysinfo_webos_release(rel, sizeof rel, &src);
    unsigned uid = (unsigned)getuid();
    const char *dec = sysinfo_decoders();
    snprintf(out, n, "%s | uid %u%s | decoders: %s", rel[0] ? rel : "unknown system", uid, uid == 0 ? " (root)" : " (not root)", dec ? dec : "checking");
}

void log_shutdown(void)
{
    if (g_file) fclose(g_file);
    g_file = NULL;
    if (g_lock) SDL_DestroyMutex(g_lock);
    g_lock = NULL;
}
