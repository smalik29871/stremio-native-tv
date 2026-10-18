#include "tidy.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utime.h>

static int failures;
#define CHECK(c) do { if (!(c)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

static char root[256];
static time_t now;

/* A file of `kb` KB, last changed `age` seconds ago. */
static void make(const char *rel, int kb, long age)
{
    char path[600];
    snprintf(path, sizeof path, "%s/%s", root, rel);
    FILE *f = fopen(path, "wb");
    static char block[1024];
    for (int i = 0; i < kb; i++) fwrite(block, 1, sizeof block, f);
    fclose(f);
    struct utimbuf t = { now - age, now - age };
    utime(path, &t);
}

static int exists(const char *rel)
{
    char path[600];
    struct stat st;
    snprintf(path, sizeof path, "%s/%s", root, rel);
    return stat(path, &st) == 0;
}

int main(void)
{
    now = time(NULL);
    snprintf(root, sizeof root, "/tmp/sntv-tidy-%d", (int)getpid());
    static const char *const DIRS[] = { "", "/cache", "/fontcache", "/yt", "/sounds", "/img" };
    for (int i = 0; i < 6; i++) { char d[300]; snprintf(d, sizeof d, "%s%s", root, DIRS[i]); mkdir(d, 0755); }
    const long DAY = 24 * 3600;

    make("cache/fresh.json", 10, 30);                     /* the newest reply */
    make("cache/week_old.json", 10, 8 * DAY);
    for (int i = 0; i < 20; i++) { char n[64]; snprintf(n, sizeof n, "cache/big%02d.json", i); make(n, 1024, 60 + i); }   /* 20 MB > 16 MB */
    make("cache/x.json.1234.tmp", 5, 2 * 3600);
    make("fontcache/used.bin", 50, 3 * DAY);
    make("fontcache/old_version.bin", 50, 90 * DAY);
    make("yt/abcd1234-0badf00d.solver.js", 20, 2 * DAY);
    make("yt/old0001-0badf00d.solver.js", 20, 40 * DAY);
    make("sounds/start.mp3", 100, 10 * DAY);              /* in use */
    make("sounds/end.m4a", 100, 10 * DAY);                /* replaced by a YouTube link */
    make("sounds/yt-dQw4w9WgXcQ-audio.m4a", 100, 2 * DAY);/* in use */
    make("sounds/yt-OLDOLDOLD01-video.mp4", 100, 2 * DAY);/* a link no longer set */
    make("sounds/yt-NEWNEWNEW01-audio.m4a.part", 10, 60); /* being downloaded right now */
    make("sounds/upload.tmp", 10, 3 * 3600);              /* an upload that died */
    make("settings.json", 1, 10 * DAY);
    make("settings.json.99.tmp", 1, 3 * 3600);
    make("library.json.98.tmp", 1, 30);                   /* being written right now */
    make("img/abc.img.tmp", 1, 2 * 3600);
    make("img/abc.img", 1, 50 * DAY);                     /* the artwork cache trims itself */

    const char *keep[] = { "start.mp3", "yt-dQw4w9WgXcQ-" };
    TidyReport r;
    tidy_run(root, keep, 2, now, &r);
    printf("tidy: removed %d files, %lld KB\n", r.removed, r.freed >> 10);

    CHECK(exists("cache/fresh.json") && !exists("cache/week_old.json") && !exists("cache/x.json.1234.tmp"));
    int big = 0;
    for (int i = 0; i < 20; i++) { char n[64]; snprintf(n, sizeof n, "cache/big%02d.json", i); big += exists(n); }
    CHECK(big == 15 && exists("cache/big00.json") && !exists("cache/big19.json"));   /* the newest fit in 16 MB */
    CHECK(exists("fontcache/used.bin") && !exists("fontcache/old_version.bin"));
    CHECK(exists("yt/abcd1234-0badf00d.solver.js") && !exists("yt/old0001-0badf00d.solver.js"));
    CHECK(exists("sounds/start.mp3") && exists("sounds/yt-dQw4w9WgXcQ-audio.m4a") && exists("sounds/yt-NEWNEWNEW01-audio.m4a.part"));
    CHECK(!exists("sounds/end.m4a") && !exists("sounds/yt-OLDOLDOLD01-video.mp4") && !exists("sounds/upload.tmp"));
    CHECK(exists("settings.json") && !exists("settings.json.99.tmp") && exists("library.json.98.tmp"));
    CHECK(!exists("img/abc.img.tmp") && exists("img/abc.img"));

    char cmd[300];
    snprintf(cmd, sizeof cmd, "rm -rf %s", root);
    if (system(cmd) != 0) fprintf(stderr, "could not remove %s\n", root);
    if (failures) fprintf(stderr, "%d failure(s)\n", failures);
    else printf("tidy: all checks passed\n");
    return failures != 0;
}
