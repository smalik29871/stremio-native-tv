/* A title's soundtrack: the albums the iTunes store has for it (the songs, the score),
 * each with its tracks, its cover and a link to open it on a phone. No key needed. */
#ifndef SNTV_MUSIC_H
#define SNTV_MUSIC_H

#define MUSIC_ALBUMS 3
#define MUSIC_TRACKS 40

typedef struct { char name[96]; int no, disc, ms; } MusicTrack;
typedef struct MusicAlbum {
    char title[160], artist[96], art[300], link[300];
    int year, n;
    MusicTrack tracks[MUSIC_TRACKS];
} MusicAlbum;

/* The albums once known; NULL (and *n 0) while they are looked up or when there are
 * none. year: the title's ("2008" or "2008-2013"), to keep a remake's music out. */
const MusicAlbum *music_albums(const char *id, const char *title, const char *year, int *n);

#endif
