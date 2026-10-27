#include "playback.h"
#include <math.h>
#include <stdio.h>

static int failures;
#define CHECK(c) do { if (!(c)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

/* The LG draws at 50 Hz (a frame every ~21 ms with the wait for the display) and its player
   reports the position in steps of its own, which don't line up with the frames. */
#define LG_FRAME 0.021
static int frames(double seconds) { return (int)(seconds / LG_FRAME + 0.5); }

int main(void)
{
    PlaybackWatch w = { 0 };
    playback_watch_update(&w, 5700, 0, 1, 0, 1);
    for (int i = 0; i < frames(180); i++) playback_watch_update(&w, 5700, LG_FRAME, 1, 0, 0);
    CHECK(!playback_watch_through(&w, 6000));
    CHECK(w.played == 0);

    for (int i = 1; i <= frames(121); i++) playback_watch_update(&w, 5700 + floor(i * LG_FRAME * 10) / 10, LG_FRAME, 1, 1, 0);
    CHECK(w.played > 120.5 && w.played <= 121.01);   /* every second played counts, though no report falls on a frame */
    CHECK(playback_watch_through(&w, 6000));

    playback_watch_update(&w, 5900, LG_FRAME, 1, 1, 1);
    for (int i = 0; i < frames(180); i++) playback_watch_update(&w, 5900, LG_FRAME, 1, 0, 0);
    CHECK(!playback_watch_through(&w, 6000));

    w = (PlaybackWatch){ 0 };   /* stalled with the player still "playing", then a jump: the stall pays for at most a second of it */
    playback_watch_update(&w, 100, 0, 1, 1, 0);
    for (int i = 0; i < frames(10); i++) playback_watch_update(&w, 100, LG_FRAME, 1, 1, 0);
    playback_watch_update(&w, 102.5, LG_FRAME, 1, 1, 0);
    CHECK(w.played <= 1.01 + LG_FRAME);

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
    playback_watch_update(&w, 0, 0, 2, 1, 0);   /* at 2x */
    for (int i = 1; i <= frames(61); i++) playback_watch_update(&w, floor(i * LG_FRAME * 2 * 4) / 4, LG_FRAME, 2, 1, 0);
    CHECK(playback_watch_through(&w, 600));
    if (failures) return 1;
    puts("playback progress checks passed");
    return 0;
}
