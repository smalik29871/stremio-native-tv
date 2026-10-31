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
    /* where a slow frame's time went: at 120 Hz (8.3 ms) a frame past 12.5 ms is slow */
    perf_set_refresh(120);
    perf_phases(1, 2, 3);     /* 6 ms: kept up */
    perf_phases(14, 2, 1);    /* slow, in the update */
    perf_phases(2, 20, 1);    /* slow, in the drawing */
    perf_phases(2, 2, 25);    /* slow, in the swap (the worst) */
    perf_report(report, sizeof report);
    CHECK(strstr(report, "Slow frames (past 1.5 refreshes): 3 of 4"));
    CHECK(strstr(report, "the update 1 times") && strstr(report, "the drawing 1, the swap 1"));
    CHECK(strstr(report, "the worst one: 29 ms (update 2, draw 2, swap 25)"));
    CHECK(strstr(report, "from the display"));
    /* the slowest steps of the update, worst first; a step that never passed 3 ms is not listed */
    perf_stage("http", 1.0f); perf_stage("http", 14.0f); perf_stage("http", 4.0f);
    perf_stage("pictures", 6.0f); perf_stage("video", 2.0f); perf_stage("screen draw", 9.0f);
    perf_report(report, sizeof report);
    CHECK(strstr(report, "Slowest steps: http: worst 14 ms, over 3 ms 2 times; screen draw: worst 9 ms, over 3 ms 1 times; pictures: worst 6 ms, over 3 ms 1 times"));
    CHECK(!strstr(report, "video: worst"));
    /* the frame-rate governor at 120 Hz (8.3 ms; slow is past 12.5): a screen that keeps up stays; one that does not drops to a steady 60,
     * is tried at full rate again after 120 s, and a failed try waits twice as long */
    {
        float t = 10;
        int d = 1;
        for (int i = 0; i < 400; i++) d = perf_governor(8.4f, t += 0.01f, 120);   /* 400 frames at the refresh: stays */
        CHECK(d == 1);
        for (int i = 0; i < 119; i++) d = perf_governor(17.0f, t += 0.01f, 120);  /* every frame late, but a window is not over */
        CHECK(d == 1);
        d = perf_governor(17.0f, t += 0.01f, 120);                                /* the first slow window: one is not enough */
        CHECK(d == 1);
        for (int i = 0; i < 120; i++) d = perf_governor(17.0f, t += 0.01f, 120);
        CHECK(d == 2);                                                            /* the second: a steady 60 */
        float dropped = t;
        CHECK(perf_governor(40.0f, dropped + 119, 120) == 2);                     /* frames at half rate are not judged */
        CHECK(perf_governor(16.7f, dropped + 121, 120) == 1);                     /* tried again */
        for (int i = 0; i < 120; i++) d = perf_governor(17.0f, t = dropped + 121 + i * 0.01f, 120);
        CHECK(d == 2);                                                            /* a trial that is slow once is over */
        float second = t;
        CHECK(perf_governor(16.7f, second + 121, 120) == 2);                      /* now it waits twice as long */
        CHECK(perf_governor(16.7f, second + 241, 120) == 1);
        for (int w = 0; w < 7; w++) for (int i = 0; i < 120; i++) d = perf_governor(8.4f, t = second + 241 + (w * 120 + i) * 0.01f, 120);
        CHECK(d == 1);                                                            /* a trial that holds is kept */
        CHECK(perf_governor(50.0f, t + 1, 60) == 1);                              /* a 60 Hz screen has nothing to halve to */
        perf_report(report, sizeof report);
        CHECK(!strstr(report, "Frame rate: a steady"));
    }
    /* the cadence the screen really delivers: a phone that offers 120 Hz and runs the app at 90 */
    perf_cadence_reset();
    CHECK(perf_live_hz() == 0);
    for (int i = 0; i < 64; i++) perf_cadence(i % 9 == 0 ? 13.5f : 11.2f);   /* mostly one refresh at 90, now and then a late one */
    CHECK(perf_live_hz() == 90);
    CHECK(fabsf(perf_frame_period_ms() - 1000.0f / 90) < 0.01f);
    perf_cadence_reset();
    for (int i = 0; i < 64; i++) perf_cadence(8.4f + (i % 5) * 0.1f);
    CHECK(perf_live_hz() == 120);
    perf_cadence_reset();
    for (int i = 0; i < 64; i++) perf_cadence(3.5f);                          /* frames that do not wait for the display say nothing */
    CHECK(perf_live_hz() == 0);
    for (int i = 0; i < 64; i++) perf_cadence(15.0f);                         /* between two standard rates: unknown */
    CHECK(perf_live_hz() == 0);
    for (int i = 0; i < 64; i++) perf_cadence(16.7f);                         /* a steady 60 (also what the governor's half rate looks like) */
    CHECK(perf_live_hz() == 60);
    perf_cadence_reset();
    CHECK(perf_governor(50.0f, 5000, 90) == 1);                               /* 90 is not worth halving */
    perf_set_refresh(0);
    if (failures) return 1;
    puts("playback diagnostic checks passed");
    return 0;
}
