#include "log.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <SDL.h>

static FILE *g_file;
static SDL_mutex *g_lock;

void log_init(const char *dir)
{
    char path[1024];
    g_lock = SDL_CreateMutex();
    if (!dir) return;
    snprintf(path, sizeof path, "%s/app.log", dir);
    /* Rotate once so a crash log from the previous run survives. */
    char prev[1024];
    snprintf(prev, sizeof prev, "%s/app.prev.log", dir);
    rename(path, prev);
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
    if (g_lock) SDL_UnlockMutex(g_lock);
}

void log_shutdown(void)
{
    if (g_file) fclose(g_file);
    g_file = NULL;
    if (g_lock) SDL_DestroyMutex(g_lock);
    g_lock = NULL;
}
