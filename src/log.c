#include "log.h"
#include "sysinfo.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <SDL.h>

static FILE *g_file;
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
    /* Rotate once so a crash log from the previous run survives. */
    snprintf(g_prev_path, sizeof g_prev_path, "%s/app.prev.log", dir);
    rename(path, g_prev_path);
    g_file = fopen(path, "w");
}

void log_msg(const char *level, const char *fmt, ...)
{
    char line[2048];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);

    time_t now = time(NULL);
    struct tm tmv;
    localtime_r(&now, &tmv);
    char ts[32];
    strftime(ts, sizeof ts, "%H:%M:%S", &tmv);

    if (g_lock) SDL_LockMutex(g_lock);
    fprintf(stderr, "%s %s %s\n", ts, level, line);
    if (g_file) {
        fprintf(g_file, "%s %s %s\n", ts, level, line);
        fflush(g_file);
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

void log_system_info(char *out, size_t n)
{
    char rel[200]; const char *src;
    sysinfo_webos_release(rel, sizeof rel, &src);
    unsigned uid = (unsigned)getuid();
    snprintf(out, n, "%s | uid %u%s", rel[0] ? rel : "unknown system", uid, uid == 0 ? " (root)" : " (not root)");
}

void log_shutdown(void)
{
    if (g_file) fclose(g_file);
    g_file = NULL;
    if (g_lock) SDL_DestroyMutex(g_lock);
    g_lock = NULL;
}
