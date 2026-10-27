#include "playback.h"
#include <math.h>
#include <stdio.h>

static int failures;
#define CHECK(c) do { if (!(c)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

int main(void)
{
    PlaybackWatch w = { 0 };
    playback_watch_update(&w, 5700, 0, 1, 0, 1);
    for (int i = 0; i < 1800; i++) playback_watch_update(&w, 5700, 0.1, 1, 0, 0);
    CHECK(!playback_watch_through(&w, 6000));
    CHECK(w.played == 0);

    for (int i = 1; i <= 1210; i++) playback_watch_update(&w, 5700 + i / 10.0, 0.1, 1, 1, 0);
    CHECK(playback_watch_through(&w, 6000));

    playback_watch_update(&w, 5900, 0.1, 1, 1, 1);
    for (int i = 0; i < 1800; i++) playback_watch_update(&w, 5900, 0.1, 1, 0, 0);
    CHECK(!playback_watch_through(&w, 6000));

    w = (PlaybackWatch){ 0 };
    playback_watch_update(&w, 0, 0, 1, 1, 0);
    for (int tick = 1; tick <= 7260; tick++) {
        double position = (tick / 6) / 10.0;
        playback_watch_update(&w, position, 1.0 / 60, 1, 1, 0);
    }
    CHECK(w.played > 120 && w.played <= 121.01);
    CHECK(playback_watch_through(&w, 600));

    playback_watch_update(&w, 500, 1.0 / 60, 1, 1, 0);
    CHECK(w.played == 0);
    CHECK(!playback_watch_through(&w, 600));
    playback_watch_update(&w, NAN, 1, 1, 1, 0);
    CHECK(isfinite(w.position));

    w = (PlaybackWatch){ 0 };
    playback_watch_update(&w, 0, 0, 2, 1, 0);
    for (int i = 1; i <= 610; i++) playback_watch_update(&w, i / 5.0, 0.1, 2, 1, 0);
    CHECK(playback_watch_through(&w, 600));
    if (failures) return 1;
    puts("playback progress checks passed");
    return 0;
}
