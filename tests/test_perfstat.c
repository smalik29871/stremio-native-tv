#include "perfstat.h"
#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(c) do { if (!(c)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

int main(void)
{
    char report[8192];
    perf_playback_begin();
    for (int i = 0; i < 60; i++) perf_playback_sample(0, 1.0f / 60, 1, 1, 0, 1, 0);
    perf_report(report, sizeof report);
    CHECK(!strstr(report, "Playback now"));
    for (int i = 1; i <= 60; i++) perf_playback_sample((i / 6 + 1) / 10.0, 1.0f / 60, 1, 0, 2, 1, 0);
    for (int i = 0; i < 60; i++) perf_playback_sample(1.1, 1.0f / 60, 1, 1, 2, 1, 0);
    for (int i = 0; i < 120; i++) perf_playback_sample(1.1, 1.0f / 60, 0, 0, 2, 1, 0);
    perf_report(report, sizeof report);
    CHECK(strstr(report, "1 s playing, 1.0 s buffering (50.00%), 1 stalls, 2 dropped frames"));
    perf_playback_sample(1.2, 1.0f / 60, 1, 0, 3, 2, 0);
    perf_report(report, sizeof report);
    CHECK(strstr(report, "5 dropped frames"));
    perf_playback_seek(0, 1);
    perf_playback_sample(200, 1.0f / 60, 1, 0, 3, 2, 0);
    perf_report(report, sizeof report);
    CHECK(!strstr(report, "Seek recovery:"));
    perf_playback_sample(200, 1.0f / 60, 1, 0, 3, 2, 1);
    perf_report(report, sizeof report);
    CHECK(strstr(report, "(1 seeks)"));
    perf_playback_seek(1, 1);
    perf_playback_sample(100, 1.0f / 60, 0, 0, 3, 2, 1);
    perf_playback_sample(101, 1.0f / 60, 1, 0, 3, 2, 2);
    perf_report(report, sizeof report);
    CHECK(strstr(report, "(1 seeks)"));
    perf_playback_seek(2, 1);
    perf_playback_sample(150, 1.0f / 60, 0, 0, 3, 2, 3);
    perf_report(report, sizeof report);
    CHECK(strstr(report, "(1 seeks)"));
    perf_playback_end();
    perf_report(report, sizeof report);
    CHECK(strstr(report, "Playback last session"));
    if (failures) return 1;
    puts("playback diagnostic checks passed");
    return 0;
}
