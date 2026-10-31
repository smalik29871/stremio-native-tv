#include <math.h>
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
    perf_playback_seek(0, 1, 200);
    perf_playback_sample(200, 1.0f / 60, 1, 0, 3, 2, 0);
    perf_report(report, sizeof report);
    CHECK(!strstr(report, "Seek recovery:"));
    perf_playback_sample(200, 1.0f / 60, 1, 0, 3, 2, 1);
    perf_report(report, sizeof report);
    CHECK(strstr(report, "(1 seeks)"));
    perf_playback_seek(1, 1, 100);
    perf_playback_sample(100, 1.0f / 60, 0, 0, 3, 2, 1);
    perf_playback_sample(101, 1.0f / 60, 1, 0, 3, 2, 2);
    perf_report(report, sizeof report);
    CHECK(strstr(report, "(1 seeks)"));
    perf_playback_seek(2, 1, 150);
    perf_playback_sample(150, 1.0f / 60, 0, 0, 3, 2, 3);
    perf_report(report, sizeof report);
    CHECK(strstr(report, "(1 seeks)"));
    /* an LG says nothing when a seek is done: it is done once it plays on from the target (the
       position stands there, buffering then playing, until the TV reports it moved; 50 Hz frames) */
    perf_playback_seek(3, 0, 600);
    for (int i = 0; i < 40; i++) perf_playback_sample(600, 0.021f, 1, 1, 3, 2, 3);   /* buffering at the target */
    for (int i = 0; i < 20; i++) perf_playback_sample(600, 0.021f, 1, 0, 3, 2, 3);   /* playing, no report yet */
    perf_report(report, sizeof report);
    CHECK(strstr(report, "(1 seeks)"));
    perf_playback_sample(600.42, 0.021f, 1, 0, 3, 2, 3);   /* the TV's next report: it plays on */
    perf_report(report, sizeof report);
    CHECK(strstr(report, "(2 seeks)"));
    perf_playback_seek(3, 0, 900);
    perf_playback_sample(120, 0.021f, 1, 0, 3, 2, 3);   /* somewhere else entirely (a source switch): not this seek landing */
    perf_report(report, sizeof report);
    CHECK(strstr(report, "(2 seeks)"));
    perf_playback_end();
    perf_report(report, sizeof report);
    CHECK(strstr(report, "Playback last session"));
    /* a frame step on a 90 Hz screen: timer noise around one refresh (11.1 ms) is one refresh; a missed refresh is two; a real pause is left alone */
    CHECK(fabsf(perf_snap_dt(0.0108f, 90) - 1.0f / 90) < 1e-6f);
    CHECK(fabsf(perf_snap_dt(0.0118f, 90) - 1.0f / 90) < 1e-6f);
    CHECK(fabsf(perf_snap_dt(0.0225f, 90) - 2.0f / 90) < 1e-6f);
    CHECK(perf_snap_dt(0.0166f, 90) == 0.0166f);   /* between one and two refreshes: kept as measured */
    CHECK(perf_snap_dt(0.05f, 0) == 0.05f);        /* no known refresh rate: untouched */
    perf_set_refresh(90);
    CHECK(fabsf(perf_frame_period_ms() - 11.1111f) < 0.01f);
    perf_set_refresh(0);
    if (failures) return 1;
    puts("playback diagnostic checks passed");
    return 0;
}
