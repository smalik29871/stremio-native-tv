/* Per-series track memory: the audio language and subtitle choice someone
 * picked in the player menu, applied to the next episodes of that series ahead
 * of the global preferences. Kept in settings.json, newest 300 series. */
#ifndef SNTV_TRACKMEM_H
#define SNTV_TRACKMEM_H

typedef enum { SUBS_SRC_ANY = 0, SUBS_SRC_EMBEDDED, SUBS_SRC_ADDON } SubsSource;

typedef struct TrackMem {
    char audio[16];      /* "" = no audio choice remembered */
    int has_subs;        /* a subtitle choice is remembered */
    char subs[16];       /* "" with has_subs = subtitles off */
    SubsSource subs_src;
} TrackMem;

int  trackmem_get(const char *series_id, TrackMem *out);   /* 1 when something is remembered */
/* audio NULL keeps the remembered audio; subs NULL keeps the remembered subtitles. */
void trackmem_set(const char *series_id, const char *audio, const char *subs, SubsSource src);

#endif
