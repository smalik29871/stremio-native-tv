/* The scene filter's parsing (src/scene_filter.c), on a real CleanStream answer. */
#include "scene_filter.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(c) do { if (!(c)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

/* from https://cleanstream.elfhosted.com/api/skips/tt0111161 */
static const char *REAL =
    "{\"version\":\"1.0.0\",\"imdbId\":\"tt0111161\",\"metadata\":{},\"generatedAt\":\"2026-10-31T20:58:14.821Z\",\"totalSkips\":5,\"totalSkipTime\":222150,\"skips\":["
    "{\"id\":\"a\",\"startMs\":351930,\"endMs\":376200,\"startTime\":\"5:51\",\"endTime\":\"6:16\",\"duration\":24270,\"category\":\"sex\",\"subcategory\":\"sex\",\"severity\":\"high\",\"channel\":\"both\",\"description\":\"Sexual content\"},"
    "{\"id\":\"b\",\"startMs\":854620,\"endMs\":950360,\"category\":\"nudity\",\"subcategory\":\"nudity\",\"severity\":\"high\",\"channel\":\"both\",\"description\":\"Nudity\"},"
    "{\"id\":\"c\",\"startMs\":1420050,\"endMs\":1462160,\"category\":\"nudity\",\"severity\":\"high\",\"channel\":\"both\"},"
    "{\"id\":\"d\",\"startMs\":1915420,\"endMs\":1919300,\"category\":\"sex\",\"severity\":\"high\",\"channel\":\"both\"},"
    "{\"id\":\"e\",\"startMs\":2598690,\"endMs\":2654840,\"category\":\"sex\",\"severity\":\"high\",\"channel\":\"both\"}]}";

int main(void)
{
    FilterSeg s[FILTER_MAX];
    char url[200];

    CHECK(scene_filter_url("https://cleanstream.elfhosted.com/", "tt0111161", url, sizeof url) == 0 && !strcmp(url, "https://cleanstream.elfhosted.com/api/skips/tt0111161"));
    CHECK(scene_filter_url("http://192.168.1.10:7000", "tt0111161", url, sizeof url) == 0 && !strcmp(url, "http://192.168.1.10:7000/api/skips/tt0111161"));
    CHECK(scene_filter_url(NULL, "tt0111161", url, sizeof url) == 0 && !strncmp(url, FILTER_DEFAULT_BASE, strlen(FILTER_DEFAULT_BASE)));   /* no source set: the public one */
    CHECK(scene_filter_url("http://x", "tt0903747:1:2", url, sizeof url) == -1);   /* an episode: no list */
    CHECK(scene_filter_url("http://x", "kitsu:12", url, sizeof url) == -1);
    CHECK(scene_filter_url("http://x", NULL, url, sizeof url) == -1);

    int n = scene_filter_parse(REAL, FCAT_NUDITY, FSEV_LOW, s, FILTER_MAX);
    CHECK(n == 5);   /* sex counts as nudity */
    CHECK(fabs(s[0].start - 350.93) < 1e-6 && fabs(s[0].end - 376.7) < 1e-6);   /* padded 1 s before and 0.5 s after */
    CHECK(s[0].cat == FCAT_NUDITY && !s[0].audio_only);
    CHECK(fabs(s[4].start - 2597.69) < 1e-6);
    CHECK(scene_filter_at(s, n, 360.0) == &s[0]);
    CHECK(scene_filter_at(s, n, 350.0) == NULL);
    CHECK(scene_filter_at(s, n, 350.95) == &s[0]);      /* the lead */
    CHECK(scene_filter_at(s, n, 376.7) == NULL);        /* the end is out */
    CHECK(scene_filter_at(s, n, 900.0) == &s[1]);

    CHECK(scene_filter_parse(REAL, FCAT_VIOLENCE, FSEV_LOW, s, FILTER_MAX) == 0);   /* nothing of another kind asked for */
    CHECK(scene_filter_parse(REAL, FCAT_NUDITY, FSEV_HIGH, s, 3) == 3);             /* the room given */

    /* severity, audio-only, merging, unknown categories, junk entries */
    const char *MIX =
        "{\"skips\":["
        "{\"startMs\":10000,\"endMs\":20000,\"category\":\"nudity\",\"severity\":\"low\",\"channel\":\"both\"},"
        "{\"startMs\":20500,\"endMs\":30000,\"category\":\"sex\",\"severity\":\"high\",\"channel\":\"both\"},"      /* within 1.5 s of the last: one cut */
        "{\"startMs\":50000,\"endMs\":51000,\"category\":\"language\",\"severity\":\"medium\",\"channel\":\"audio\"},"
        "{\"startMs\":60000,\"endMs\":59000,\"category\":\"nudity\",\"severity\":\"high\"},"                          /* ends before it starts */
        "{\"startMs\":70000,\"endMs\":80000,\"category\":\"mystery\",\"severity\":\"high\"},"                         /* a kind we do not know */
        "{\"startMs\":90000,\"endMs\":95000,\"category\":\"nudity\"},"                                                /* no severity: the safe side */
        "{\"endMs\":99000,\"category\":\"nudity\"}]}";
    n = scene_filter_parse(MIX, FCAT_NUDITY | FCAT_LANGUAGE, FSEV_LOW, s, FILTER_MAX);
    CHECK(n == 3);
    CHECK(fabs(s[0].start - 9.0) < 1e-6 && fabs(s[0].end - 30.5) < 1e-6);   /* the two merged */
    CHECK(s[1].audio_only && s[1].cat == FCAT_LANGUAGE);
    CHECK(fabs(s[2].start - 89.0) < 1e-6);
    n = scene_filter_parse(MIX, FCAT_NUDITY, FSEV_HIGH, s, FILTER_MAX);
    CHECK(n == 2);   /* the low one is under the bar, so the high one stands alone */
    CHECK(fabs(s[0].start - 19.5) < 1e-6);

    CHECK(scene_filter_parse("not json", FCAT_NUDITY, FSEV_LOW, s, FILTER_MAX) == -1);
    CHECK(scene_filter_parse("{\"error\":\"not found\"}", FCAT_NUDITY, FSEV_LOW, s, FILTER_MAX) == -1);
    CHECK(scene_filter_parse("{\"skips\":[]}", FCAT_NUDITY, FSEV_LOW, s, FILTER_MAX) == 0);
    CHECK(scene_filter_parse(NULL, FCAT_NUDITY, FSEV_LOW, s, FILTER_MAX) == -1);

    if (failures) return 1;
    puts("scene filter checks passed");
    return 0;
}
