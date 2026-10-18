/* Skip markers (intro, recap, credits) from public, keyless services:
 *   - TheIntroDB (api.theintrodb.org/v4) for IMDb ids, movies and episodes
 *   - AniSkip (api.aniskip.com/v2) for anime ids (kitsu:/mal:), with Kitsu's
 *     own mappings turning a Kitsu id into the MyAnimeList id AniSkip uses
 * Only parsing and URL building live here so they can be unit-tested; the
 * player does the requests. Base URLs can be overridden with SNTV_INTRODB_API,
 * SNTV_ANISKIP_API and SNTV_KITSU_API (tests point them at the fake addon). */
#ifndef SNTV_SKIP_H
#define SNTV_SKIP_H
#include <stddef.h>

typedef enum { SKIP_INTRO, SKIP_RECAP, SKIP_CREDITS, SKIP_PREVIEW } SkipKind;

typedef struct SkipSeg {
    double start, end;   /* seconds; end 0 = until the end of the video */
    SkipKind kind;
} SkipSeg;

enum { SKIP_MAX = 8 };

typedef enum { SKIP_SRC_NONE, SKIP_SRC_IMDB, SKIP_SRC_KITSU, SKIP_SRC_MAL } SkipSource;

typedef struct SkipId {
    SkipSource src;
    char id[32];         /* tt123 / Kitsu or MAL number */
    int season, episode; /* -1 for movies */
} SkipId;

/* "tt0903747:1:2", "tt0111161", "kitsu:12:5", "mal:21:1". Returns 0 when usable. */
int skip_parse_video_id(const char *video_id, SkipId *out);

void skip_url_introdb(const SkipId *id, double duration, char *url, size_t n);
void skip_url_aniskip(long mal_id, int episode, double duration, char *url, size_t n);
void skip_url_kitsu_mappings(const char *kitsu_id, char *url, size_t n);

/* TheIntroDB: arrays of {start_ms, end_ms} under intro/recap/credits/preview,
 * null start = 0, null end = until the end. Returns the number of segments, or
 * -1 when the answer is for a different episode than asked. */
int skip_parse_introdb(const char *json, int season, int episode, SkipSeg *out, int max);

/* AniSkip: results[] of {interval:{startTime,endTime}, skipType op/ed/recap,
 * episodeLength}. Each type keeps the release closest to `duration` (when both
 * are known) and drops releases more than 10% longer or shorter. */
int skip_parse_aniskip(const char *json, double duration, SkipSeg *out, int max);

/* Kitsu mappings: the externalId of the myanimelist/anime mapping, or 0. */
long skip_parse_kitsu_mal(const char *json);

/* Segment that `pos` is inside, or NULL. */
const SkipSeg *skip_find(const SkipSeg *segs, int n, SkipKind kind, double pos, double duration);
/* Where the credits (or the next-episode preview) begin, or 0 if unknown. */
double skip_credits_start(const SkipSeg *segs, int n);

#endif
