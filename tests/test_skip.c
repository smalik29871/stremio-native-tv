#include "skip.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
#define CHECK(c) do { if (!(c)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)
#define NEAR(a, b) (fabs((a) - (b)) < 0.01)

static char *fixture(const char *name)
{
    char path[1024];
    snprintf(path, sizeof path, "%s/skip/%s", SNTV_FIXTURES, name);
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "missing %s\n", path); exit(2); }
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    char *b = malloc((size_t)n + 1);
    if (fread(b, 1, (size_t)n, f) != (size_t)n) exit(2);
    b[n] = 0; fclose(f);
    return b;
}

int main(void)
{
    SkipId id;
    CHECK(skip_parse_video_id("tt0903747:1:2", &id) == 0 && id.src == SKIP_SRC_IMDB && !strcmp(id.id, "tt0903747") && id.season == 1 && id.episode == 2);
    CHECK(skip_parse_video_id("tt0111161", &id) == 0 && id.episode == -1);
    CHECK(skip_parse_video_id("kitsu:12:5", &id) == 0 && id.src == SKIP_SRC_KITSU && !strcmp(id.id, "12") && id.episode == 5);
    CHECK(skip_parse_video_id("mal:21:1", &id) == 0 && id.src == SKIP_SRC_MAL && id.episode == 1);
    CHECK(skip_parse_video_id("tmdb:1396:1:1", &id) != 0);
    CHECK(skip_parse_video_id("tt1:1", &id) != 0);
    CHECK(skip_parse_video_id(NULL, &id) != 0);

    char url[512];
    skip_parse_video_id("tt0903747:1:2", &id);
    skip_url_introdb(&id, 2820.4, url, sizeof url);
    CHECK(!strcmp(url, "https://api.theintrodb.org/v4/media?imdb_id=tt0903747&season=1&episode=2&duration_ms=2820400"));
    skip_parse_video_id("tt0111161", &id);
    skip_url_introdb(&id, 0, url, sizeof url);
    CHECK(!strcmp(url, "https://api.theintrodb.org/v4/media?imdb_id=tt0111161"));
    skip_url_aniskip(21, 3, 1440.2, url, sizeof url);
    CHECK(!strcmp(url, "https://api.aniskip.com/v2/skip-times/21/3?types[]=op&types[]=ed&types[]=recap&episodeLength=1440"));
    skip_url_kitsu_mappings("12", url, sizeof url);
    CHECK(!strcmp(url, "https://kitsu.io/api/edge/anime/12/mappings?filter[externalSite]=myanimelist/anime"));

    SkipSeg s[SKIP_MAX];
    char *j = fixture("introdb_episode.json");
    int n = skip_parse_introdb(j, 1, 2, s, SKIP_MAX);
    free(j);
    CHECK(n == 3);
    CHECK(s[0].kind == SKIP_INTRO && NEAR(s[0].start, 95) && NEAR(s[0].end, 131.5));
    CHECK(s[1].kind == SKIP_RECAP && NEAR(s[1].start, 0) && NEAR(s[1].end, 42));
    CHECK(s[2].kind == SKIP_CREDITS && NEAR(s[2].start, 2780) && s[2].end == 0);
    CHECK(skip_find(s, n, SKIP_INTRO, 100, 2820) == &s[0]);
    CHECK(skip_find(s, n, SKIP_INTRO, 131.5, 2820) == NULL);
    CHECK(skip_find(s, n, SKIP_CREDITS, 2800, 2820) == &s[2]);
    CHECK(skip_find(s, n, SKIP_CREDITS, 2800, 0) == NULL);   /* open end, unknown duration */
    CHECK(NEAR(skip_credits_start(s, n), 2780));

    j = fixture("introdb_movie.json");
    n = skip_parse_introdb(j, -1, -1, s, SKIP_MAX);
    free(j);
    CHECK(n == 2);   /* null/null and reversed entries dropped */
    CHECK(s[0].kind == SKIP_INTRO && NEAR(s[0].start, 0) && NEAR(s[0].end, 61));
    CHECK(s[1].kind == SKIP_CREDITS && NEAR(s[1].end, 8460));

    j = fixture("introdb_other_episode.json");
    CHECK(skip_parse_introdb(j, 1, 2, s, SKIP_MAX) == -1);
    free(j);
    CHECK(skip_parse_introdb("not json", 1, 1, s, SKIP_MAX) == 0);
    CHECK(skip_parse_introdb("{\"intro\":{\"start_ms\":1000,\"end_ms\":5000}}", -1, -1, s, SKIP_MAX) == 1);

    j = fixture("aniskip.json");
    n = skip_parse_aniskip(j, 1440, s, SKIP_MAX);
    CHECK(n == 2);   /* recap from an 1800 s cut rejected */
    CHECK(s[0].kind == SKIP_INTRO && NEAR(s[0].start, 90) && NEAR(s[0].end, 180));   /* closest release */
    CHECK(s[1].kind == SKIP_CREDITS && NEAR(s[1].start, 1330) && s[1].end == 0);    /* ends at the end */
    n = skip_parse_aniskip(j, 0, s, SKIP_MAX);
    CHECK(n == 3);   /* without our duration nothing is compared */
    free(j);

    j = fixture("kitsu_mappings.json");
    CHECK(skip_parse_kitsu_mal(j) == 21);
    free(j);
    CHECK(skip_parse_kitsu_mal("{\"data\":[]}") == 0);

    if (failures) { fprintf(stderr, "%d failure(s)\n", failures); return 1; }
    printf("skip: all checks passed\n");
    return 0;
}
