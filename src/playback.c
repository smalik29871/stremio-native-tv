#include "playback.h"
#include <math.h>

void playback_watch_update(PlaybackWatch *w, double position, double elapsed, double rate, int playing, int seeking)
{
    if (!isfinite(position) || position < 0) return;
    double delta = position - w->position;
    double allowed = elapsed > 0 && rate > 0 ? elapsed * rate : 0;
    if (!w->valid || seeking || delta < -0.5 || delta > 3 + allowed * 4) {
        w->valid = 1;
        w->jump_position = position;
        w->played = w->elapsed = 0;
    } else if (playing && allowed > 0) {
        w->elapsed += allowed;
        if (delta > 0) {
            w->played += delta < w->elapsed ? delta : w->elapsed;
            w->elapsed = 0;
        }
    } else {
        w->elapsed = 0;
    }
    w->position = position;
}

int playback_watch_through(const PlaybackWatch *w, double duration)
{
    double left = duration - w->jump_position;
    double need = left * 0.8 < 120 ? left * 0.8 : 120;
    return w->valid && left > 0 && w->played >= need;
}
