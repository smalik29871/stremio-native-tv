/* Scene filter: stretches of a film to skip or mute (nudity, sex, violence, language, drugs, fear), from a CleanStream server
 * (https://github.com/ameen-roayan/stremio-cleanstream) by IMDb id:
 *
 *   GET <base>/api/skips/tt0111161  ->  {"skips":[{"startMs":351930,"endMs":376200,"category":"sex",
 *                                        "severity":"high","channel":"both",...}, ...]}
 *
 * Only parsing and URL building live here, so they can be unit-tested; the player makes the request and does the
 * skipping (src/ui/screen_player.c). SNTV_SCENES_API points a test at the fake addon. */
#ifndef SNTV_SCENE_FILTER_H
#define SNTV_SCENE_FILTER_H
#include <stddef.h>

enum { FCAT_NUDITY = 1, FCAT_VIOLENCE = 2, FCAT_LANGUAGE = 4, FCAT_DRUGS = 8, FCAT_FEAR = 16, FCAT_SEX = 32 };
enum { FSEV_LOW, FSEV_MEDIUM, FSEV_HIGH };
enum { FILTER_MAX = 64 };
#define FILTER_DEFAULT_BASE "https://cleanstream.elfhosted.com"

typedef struct FilterSeg {
    double start, end;    /* seconds, padded a little so a slightly different cut is still covered */
    int cat;              /* FCAT_*: what the scene is */
    int audio_only;       /* the sound only (a word): muted, never skipped */
} FilterSeg;

/* "tt0111161" -> "<base>/api/<what>/tt0111161", what "skips" (the server's own picks: nudity and sex) or "filters" (the full list,
 * every kind). Only a movie's id (no ":season:episode") has a list. Returns 0 when usable. */
int scene_filter_url(const char *base, const char *video_id, const char *what, char *url, size_t n);

/* The segments of a response that are of a kind in `cats` (FCAT_ bits) and at least `min_sev`, in order, those that
 * overlap (or nearly touch) merged. Returns how many, or -1 when it is not a skips answer. */
int scene_filter_parse(const char *json, int cats, int min_sev, FilterSeg *out, int max);

/* Sorts segs[0..n) and joins those that overlap or nearly touch; the new count. */
int scene_filter_merge(FilterSeg *segs, int n);

/* The segment `pos` (seconds) is in, or NULL. */
const FilterSeg *scene_filter_at(const FilterSeg *segs, int n, double pos);

#endif
