/* Keeps the app's data folder from growing: reply caches, recordings nobody uses any
 * more, half-written files from a crash, font atlases and YouTube player data from
 * older versions. Pure file work, so it runs on a background thread at start-up and
 * after the break recordings change. The artwork cache trims itself (image.c). */
#ifndef SNTV_TIDY_H
#define SNTV_TIDY_H
#include <time.h>

enum { TIDY_KEEP_MAX = 8 };

typedef struct {
    int removed;
    long long freed;
} TidyReport;

/* `keep`: names (or name prefixes) in sounds/ still in use, e.g. "start.mp3" or
 * "yt-dQw4w9WgXcQ-". Everything else there older than an hour goes. */
void tidy_run(const char *data_dir, const char *const *keep, int n_keep, time_t now, TidyReport *out);

/* What the data folder holds in all, and how much of it is cache (comes back by itself). */
typedef struct { long long total, cache; } TidyUsage;
void tidy_usage(const char *data_dir, TidyUsage *out);
/* Clear cache: artwork, addon replies, font atlases, YouTube player data and old logs go;
 * settings, sign-in, the Library and the recordings stay. */
void tidy_clear(const char *data_dir, TidyReport *out);

#endif
