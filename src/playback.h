#ifndef SNTV_PLAYBACK_H
#define SNTV_PLAYBACK_H

typedef struct {
    double position, jump_position, played, elapsed;
    int valid;
} PlaybackWatch;

void playback_watch_update(PlaybackWatch *w, double position, double elapsed, double rate, int playing, int seeking);
int playback_watch_through(const PlaybackWatch *w, double duration);

#endif
