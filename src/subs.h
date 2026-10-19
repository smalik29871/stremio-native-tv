/* SRT and WebVTT parsing for addon subtitles, plus cue lookup by time. */
#ifndef SNTV_SUBS_H
#define SNTV_SUBS_H
#include <stddef.h>

typedef struct SubCue { double start, end; char *text; } SubCue;
#define SUBS_WINDOWS 24   /* ten-minute stretches with their own correction (four hours) */
typedef struct SubTrack {
    SubCue *cues; int n;
    double offset;                     /* seconds added to every cue (the viewer's nudge goes here too) */
    double scale;                      /* cue times are multiplied by this (0 or 1: as written) */
    float window_fix[SUBS_WINDOWS];    /* per ten minutes of the video, a further shift (cuts, inserts) */
} SubTrack;

/* Parses SRT or VTT (auto-detected). Tags like <i> and {\an8} are stripped,
 * entities decoded, Windows-1252 bytes (common in old SRTs) mapped to UTF-8.
 * Returns the number of cues, or -1. */
int  subs_parse(const char *data, size_t len, SubTrack *out);
void subs_free(SubTrack *t);
/* Cue text active at `t` seconds (with offset applied) or NULL. */
const char *subs_at(const SubTrack *t, double seconds);
/* The constant shift that lines `t` up with `ref` (seconds to add to t's times), found
 * by voting over cue start differences. 1 when enough cues agree, 0 when not. */
int  subs_align(const SubTrack *ref, const SubTrack *t, double *offset_out);
/* Lines `t` up with `ref` and sets its offset, speed and per-window shifts: subtitles
 * made for another frame rate (23.976 / 24 / 25) drift further off over the video, and
 * a cut or an inserted scene moves everything after it; both are measured here. 1 when
 * synced, 0 when the two do not share enough timing to tell. */
int  subs_sync(const SubTrack *ref, SubTrack *t);
/* How well a subtitle fits a video, 0..100, with the main reason in `why`: it should end
 * with the video (credits aside) rather than run past it or stop minutes early, have a
 * full film's worth of lines, and, given a file-matched `ref`, line up with it. */
int  subs_fit(const SubTrack *t, double duration, const SubTrack *ref, char *why, size_t wn);

#endif
