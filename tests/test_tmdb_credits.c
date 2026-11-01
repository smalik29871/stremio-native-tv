/* Which of a person's credits are listed (src/tmdb.c tmdb_pick_credits). A prolific actor (24 credits or more) crashed the
 * phone: the 24th compared against a slot nothing had filled and could be dropped, leaving an empty one to read. */
#include "tmdb.h"
#include "app.h"
#include "cJSON.h"
#include <stdio.h>
#include <string.h>

/* tmdb.c reaches into the app for its cache and redraws; none of it runs here */
App g_app;
char *app_cache_get(const char *url, long max_age_seconds) { (void)url; (void)max_age_seconds; return NULL; }
void app_cache_put(const char *url, const char *body, size_t len) { (void)url; (void)body; (void)len; }
void app_redraw(void) {}

static int failures;
#define CHECK(c) do { if (!(c)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

static cJSON *credit(const char *type, double pop, int episodes, int genre)
{
    cJSON *c = cJSON_CreateObject();
    if (type) cJSON_AddStringToObject(c, "media_type", type);
    cJSON_AddNumberToObject(c, "popularity", pop);
    if (episodes >= 0) cJSON_AddNumberToObject(c, "episode_count", episodes);
    cJSON *g = cJSON_AddArrayToObject(c, "genre_ids");
    if (genre) cJSON_AddItemToArray(g, cJSON_CreateNumber(genre));
    return c;
}

static int sorted_and_set(const cJSON **pick, int n)
{
    for (int i = 0; i < n; i++) if (!pick[i]) return 0;
    for (int i = 1; i < n; i++) {
        double a = cJSON_GetObjectItem(pick[i - 1], "popularity")->valuedouble, b = cJSON_GetObjectItem(pick[i], "popularity")->valuedouble;
        if (a < b) return 0;
    }
    return 1;
}

int main(void)
{
    const cJSON *pick[24];

    /* exactly 24 qualifying credits, the last the least popular of them: every slot is filled */
    for (int fill = 0; fill < 3; fill++) {
        cJSON *cast = cJSON_CreateArray();
        for (int i = 0; i < 24; i++) cJSON_AddItemToArray(cast, credit("movie", fill == 0 ? 50 - i : fill == 1 ? i : 7, -1, 0));   /* falling, rising, all equal */
        memset(pick, 0, sizeof pick);
        int n = tmdb_pick_credits(cast, pick, 24);
        CHECK(n == 24);
        CHECK(sorted_and_set(pick, n));
        cJSON_Delete(cast);
    }

    /* more than 24: the 24 most popular stay, in order */
    cJSON *cast = cJSON_CreateArray();
    for (int i = 0; i < 60; i++) cJSON_AddItemToArray(cast, credit(i % 2 ? "movie" : "tv", (i * 37) % 101 + 0.5, 10, 0));
    memset(pick, 0, sizeof pick);
    int n = tmdb_pick_credits(cast, pick, 24);
    CHECK(n == 24 && sorted_and_set(pick, n));
    double least = cJSON_GetObjectItem(pick[23], "popularity")->valuedouble;
    int more_popular = 0;
    for (int i = 0; i < 60; i++) if (cJSON_GetObjectItem(cJSON_GetArrayItem(cast, i), "popularity")->valuedouble > least) more_popular++;
    CHECK(more_popular == 23);   /* nothing left out is more popular than the least popular kept */
    cJSON_Delete(cast);

    /* what is left out: talk shows, news and reality, one-off series appearances, other media types, entries with none */
    cast = cJSON_CreateArray();
    cJSON_AddItemToArray(cast, credit("movie", 10, -1, 0));
    cJSON_AddItemToArray(cast, credit("tv", 90, 1, 0));        /* a single episode */
    cJSON_AddItemToArray(cast, credit("tv", 80, 40, 10767));   /* a talk show */
    cJSON_AddItemToArray(cast, credit("movie", 70, -1, 10763)); /* news */
    cJSON_AddItemToArray(cast, credit("person", 60, -1, 0));   /* not a title */
    cJSON_AddItemToArray(cast, credit(NULL, 55, -1, 0));       /* no media type */
    cJSON_AddItemToArray(cast, credit("tv", 20, 62, 18));      /* a real series */
    memset(pick, 0, sizeof pick);
    n = tmdb_pick_credits(cast, pick, 24);
    CHECK(n == 2 && sorted_and_set(pick, n));
    CHECK(cJSON_GetObjectItem(pick[0], "popularity")->valuedouble == 20 && cJSON_GetObjectItem(pick[1], "popularity")->valuedouble == 10);
    cJSON_Delete(cast);

    /* nothing, or not a list */
    CHECK(tmdb_pick_credits(NULL, pick, 24) == 0);
    cast = cJSON_CreateArray();
    CHECK(tmdb_pick_credits(cast, pick, 24) == 0);
    cJSON_Delete(cast);

    /* a smaller limit, and the limit's own ceiling */
    cast = cJSON_CreateArray();
    for (int i = 0; i < 200; i++) cJSON_AddItemToArray(cast, credit("movie", i, -1, 0));
    const cJSON *big[100];
    n = tmdb_pick_credits(cast, big, 100);
    CHECK(n == 64 && sorted_and_set(big, n));
    CHECK(tmdb_pick_credits(cast, pick, 3) == 3 && cJSON_GetObjectItem(pick[0], "popularity")->valuedouble == 199);
    cJSON_Delete(cast);

    if (failures) return 1;
    puts("tmdb credit checks passed");
    return 0;
}
