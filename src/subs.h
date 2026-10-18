/* SRT and WebVTT parsing for addon subtitles, plus cue lookup by time. */
#ifndef SNTV_SUBS_H
#define SNTV_SUBS_H
#include <stddef.h>

typedef struct SubCue { double start, end; char *text; } SubCue;
typedef struct SubTrack { SubCue *cues; int n; double offset; } SubTrack;

/* Parses SRT or VTT (auto-detected). Tags like <i> and {\an8} are stripped,
 * entities decoded, Windows-1252 bytes (common in old SRTs) mapped to UTF-8.
 * Returns the number of cues, or -1. */
int  subs_parse(const char *data, size_t len, SubTrack *out);
void subs_free(SubTrack *t);
/* Cue text active at `t` seconds (with offset applied) or NULL. */
const char *subs_at(const SubTrack *t, double seconds);

#endif
