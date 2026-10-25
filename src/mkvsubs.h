/* A film's own text subtitles, read by the app instead of drawn by the TV. An LG draws a
 * file's built-in subtitle track itself, under the app, and hides it while the app paints
 * (the living letterbox); taken from the bytes the buffer downloads ahead (relay.c), the
 * app draws them like any subtitle file, minutes before they are due.
 *
 * Matroska only, text codecs only (SRT/UTF-8, ASS/SSA, WebVTT): picture subtitles (PGS,
 * VobSub) stay the TV's. The tracks come from the file's start, asked for on its own; the
 * lines from wherever the buffer is, picked up again at the next cluster after a jump. */
#ifndef SNTV_MKVSUBS_H
#define SNTV_MKVSUBS_H
#include <stddef.h>
#include <stdint.h>
#include "subs.h"

#define MKVSUBS_TRACKS 24
typedef struct {
    int number;          /* the track's number in the file */
    int text;            /* 1: a text codec the app can draw */
    int ass;             /* ASS/SSA: lines carry their fields before the text */
    int forced, deflt;
    char lang[12], codec[24], name[64];
} MkvSubTrack;

typedef struct {
    MkvSubTrack t[MKVSUBS_TRACKS]; int n;   /* the subtitle tracks, in the file's order */
    double tick_ms;                          /* one timecode tick, in milliseconds */
} MkvSubHead;

/* The subtitle tracks from a file's first bytes: 1 for a Matroska file whose tracks were
 * found (none may be subtitles), 0 otherwise. */
int  mkvsubs_parse_head(const uint8_t *data, size_t n, MkvSubHead *out);

/* A new source: its start is fetched (with its request headers) and parsed off the main
 * thread; NULL forgets the last one. */
void mkvsubs_open(const char *url, const char *headers);
/* 1 once the tracks are known (copied to out); 0 while not, or for a file with none. */
int  mkvsubs_head(MkvSubHead *out);

/* Bytes of the file from `offset` on, as the buffer downloads them (any thread). */
void mkvsubs_feed(uint64_t offset, const uint8_t *data, size_t n);
/* The lines of track `number` found so far, from the `from`-th on (in the order found),
 * copied into out (their text malloc'd, the caller frees). The count copied. */
int  mkvsubs_take(int number, int from, SubCue *out, int max);
int  mkvsubs_count(int number);

/* tests: a parser run over bytes on its own, without a download */
void mkvsubs_reset(const MkvSubHead *head);

#endif
