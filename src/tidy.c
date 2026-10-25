#include "tidy.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>

#define DAY (24 * 3600)
enum {
    REPLY_MAX_AGE = 7 * DAY, REPLY_MAX_BYTES = 8 << 20,    /* cache/: addon replies (catalogs, searches) */
    FONT_MAX_AGE = 60 * DAY,                                 /* fontcache/: touched on every use */
    YT_MAX_AGE = 30 * DAY, YT_MAX_BYTES = 12 << 20,          /* yt/: player data, the downloaded solver */
    STRAY_AGE = 3600,                                        /* .tmp / .part / unused recordings */
};

typedef struct { char name[256]; time_t mtime; long long size; } Entry;

static int ends_with(const char *s, const char *suf)
{
    size_t n = strlen(s), m = strlen(suf);
    return n >= m && !strcmp(s + n - m, suf);
}

static void drop(const char *dir, const Entry *e, TidyReport *out)
{
    char path[1400];
    snprintf(path, sizeof path, "%s/%s", dir, e->name);
    if (remove(path) == 0) { out->removed++; out->freed += e->size; }
}

/* The plain files of a folder. */
static Entry *list(const char *dir, int *n)
{
    *n = 0;
    DIR *d = opendir(dir);
    if (!d) return NULL;
    Entry *v = NULL;
    int cap = 0;
    for (struct dirent *e; (e = readdir(d)); ) {
        if (e->d_name[0] == '.' || strlen(e->d_name) >= sizeof v->name) continue;
        char path[1400];
        struct stat st;
        snprintf(path, sizeof path, "%s/%s", dir, e->d_name);
        if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) continue;
        if (*n == cap) { cap = cap ? cap * 2 : 64; v = realloc(v, sizeof *v * (size_t)cap); }
        snprintf(v[*n].name, sizeof v[*n].name, "%s", e->d_name);
        v[*n].mtime = st.st_mtime; v[*n].size = st.st_size;
        (*n)++;
    }
    closedir(d);
    return v;
}

static int newest_first(const void *a, const void *b)
{
    const Entry *x = a, *y = b;
    return x->mtime < y->mtime ? 1 : x->mtime > y->mtime ? -1 : 0;
}

/* Older than max_age goes; then, newest first, whatever passes max_bytes. */
static void by_age_and_size(const char *dir, const char *suffix, time_t now, long max_age, long long max_bytes, TidyReport *out)
{
    int n;
    Entry *v = list(dir, &n);
    if (n > 1) qsort(v, (size_t)n, sizeof *v, newest_first);   /* an empty folder lists NULL */
    long long kept = 0;
    for (int i = 0; i < n; i++) {
        if (suffix && !ends_with(v[i].name, suffix)) continue;
        if (now - v[i].mtime > max_age || (max_bytes && kept + v[i].size > max_bytes)) drop(dir, &v[i], out);
        else kept += v[i].size;
    }
    free(v);
}

void tidy_run(const char *data_dir, const char *const *keep, int n_keep, time_t now, TidyReport *out)
{
    memset(out, 0, sizeof *out);
    char dir[1100];
    snprintf(dir, sizeof dir, "%s/cache", data_dir);
    by_age_and_size(dir, ".json", now, REPLY_MAX_AGE, REPLY_MAX_BYTES, out);
    snprintf(dir, sizeof dir, "%s/fontcache", data_dir);
    by_age_and_size(dir, NULL, now, FONT_MAX_AGE, 0, out);
    snprintf(dir, sizeof dir, "%s/yt", data_dir);
    by_age_and_size(dir, NULL, now, YT_MAX_AGE, YT_MAX_BYTES, out);

    int n;
    Entry *v;
    snprintf(dir, sizeof dir, "%s/sounds", data_dir);   /* recordings: the ones in use stay */
    v = list(dir, &n);
    for (int i = 0; i < n; i++) {
        if (now - v[i].mtime < STRAY_AGE) continue;   /* a download or upload may be writing it */
        int used = 0;
        for (int k = 0; k < n_keep && !used; k++) used = keep[k] && keep[k][0] && !strncmp(v[i].name, keep[k], strlen(keep[k]));
        if (!used) drop(dir, &v[i], out);
    }
    free(v);
    static const char *const STRAYS[] = { "", "/cache", "/img", "/fontcache" };
    for (size_t d = 0; d < sizeof STRAYS / sizeof *STRAYS; d++) {   /* half-written files a crash left behind */
        snprintf(dir, sizeof dir, "%s%s", data_dir, STRAYS[d]);
        v = list(dir, &n);
        for (int i = 0; i < n; i++)
            if ((ends_with(v[i].name, ".tmp") || ends_with(v[i].name, ".part")) && now - v[i].mtime >= STRAY_AGE) drop(dir, &v[i], out);
        free(v);
    }
}

/* The bytes under a folder, a few levels down. */
static long long folder_bytes(const char *dir, int depth)
{
    DIR *d = opendir(dir);
    if (!d) return 0;
    long long sum = 0;
    for (struct dirent *e; (e = readdir(d)); ) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        char path[1400];
        struct stat st;
        snprintf(path, sizeof path, "%s/%s", dir, e->d_name);
        if (lstat(path, &st) != 0) continue;
        if (S_ISREG(st.st_mode)) sum += st.st_size;
        else if (S_ISDIR(st.st_mode) && depth > 0) sum += folder_bytes(path, depth - 1);
    }
    closedir(d);
    return sum;
}

static const char *const CACHE_DIRS[] = { "img", "cache", "fontcache", "yt" };
static const char *const CACHE_FILES[] = { "torrentd.log" };   /* not app.prev.log: after a crash it is the only record of it */

void tidy_usage(const char *data_dir, TidyUsage *out)
{
    char path[1100];
    out->total = folder_bytes(data_dir, 3);
    out->cache = 0;
    struct statvfs vs;
    if (statvfs(data_dir, &vs) == 0) {
        out->disk_free = (long long)vs.f_bavail * (long long)vs.f_frsize;
        out->disk_size = (long long)vs.f_blocks * (long long)vs.f_frsize;
    } else out->disk_free = out->disk_size = -1;
    for (size_t i = 0; i < sizeof CACHE_DIRS / sizeof *CACHE_DIRS; i++) {
        snprintf(path, sizeof path, "%s/%s", data_dir, CACHE_DIRS[i]);
        out->cache += folder_bytes(path, 1);
    }
    for (size_t i = 0; i < sizeof CACHE_FILES / sizeof *CACHE_FILES; i++) {
        struct stat st;
        snprintf(path, sizeof path, "%s/%s", data_dir, CACHE_FILES[i]);
        if (stat(path, &st) == 0) out->cache += st.st_size;
    }
}

void tidy_clear(const char *data_dir, TidyReport *out)
{
    memset(out, 0, sizeof *out);
    char dir[1100];
    for (size_t i = 0; i < sizeof CACHE_DIRS / sizeof *CACHE_DIRS; i++) {
        snprintf(dir, sizeof dir, "%s/%s", data_dir, CACHE_DIRS[i]);
        int n;
        Entry *v = list(dir, &n);
        for (int k = 0; k < n; k++) drop(dir, &v[k], out);
        free(v);
    }
    for (size_t i = 0; i < sizeof CACHE_FILES / sizeof *CACHE_FILES; i++) {
        char path[1400];
        struct stat st;
        snprintf(path, sizeof path, "%s/%s", data_dir, CACHE_FILES[i]);
        if (stat(path, &st) == 0 && remove(path) == 0) { out->removed++; out->freed += st.st_size; }
    }
}
