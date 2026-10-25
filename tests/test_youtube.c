#include "youtube.h"
#include "cJSON.h"
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
#define CHECK(c) do { if (!(c)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

static int id_is(const char *url, const char *want)
{
    char id[16] = "";
    int ok = youtube_video_id(url, id);
    return want ? ok && !strcmp(id, want) : !ok;
}

/* yt-dlp's solver output for a request, as JSON (NULL when the engine itself failed). */
static char g_err[400];

static cJSON *solve(const char *player, const char *kind, const char *challenge, double *ms)
{
    cJSON *in = cJSON_CreateObject(), *reqs = cJSON_CreateArray(), *r = cJSON_CreateObject(), *ch = cJSON_CreateArray();
    cJSON_AddStringToObject(in, "type", "player");
    cJSON_AddStringToObject(in, "player", player);
    cJSON_AddTrueToObject(in, "output_preprocessed");
    cJSON_AddStringToObject(r, "type", kind);
    cJSON_AddItemToArray(ch, cJSON_CreateString(challenge));
    cJSON_AddItemToObject(r, "challenges", ch);
    cJSON_AddItemToArray(reqs, r);
    cJSON_AddItemToObject(in, "requests", reqs);
    char *text = cJSON_PrintUnformatted(in);
    g_err[0] = 0;
    cJSON_Delete(in);
    Uint64 t0 = SDL_GetPerformanceCounter();
    char *out = youtube_solve(text, g_err, sizeof g_err);
    *ms = (double)(SDL_GetPerformanceCounter() - t0) * 1000 / SDL_GetPerformanceFrequency();
    free(text);
    cJSON *res = out ? cJSON_Parse(out) : NULL;
    free(out);
    return res;
}

int main(void)
{
    /* only a real video id may go into a launch or a player: an addon's ytId is anything it likes */
    CHECK(youtube_id_valid("dQw4w9WgXcQ"));
    CHECK(youtube_id_valid("a-b_c-d_e-f"));
    CHECK(!youtube_id_valid("x\"},\"id\":\"evil"));
    CHECK(!youtube_id_valid("dQw4w9WgXc"));
    CHECK(!youtube_id_valid("dQw4w9WgXcQQ"));
    CHECK(!youtube_id_valid("dQw4w9 gXcQ"));
    CHECK(!youtube_id_valid(""));
    CHECK(!youtube_id_valid(NULL));

    CHECK(id_is("https://www.youtube.com/watch?v=dQw4w9WgXcQ", "dQw4w9WgXcQ"));
    CHECK(id_is("https://youtube.com/watch?feature=share&v=dQw4w9WgXcQ&t=10", "dQw4w9WgXcQ"));
    CHECK(id_is("https://youtu.be/dQw4w9WgXcQ?si=abc", "dQw4w9WgXcQ"));
    CHECK(id_is("https://m.youtube.com/shorts/dQw4w9WgXcQ", "dQw4w9WgXcQ"));
    CHECK(id_is("https://music.youtube.com/watch?v=dQw4w9WgXcQ&list=x", "dQw4w9WgXcQ"));
    CHECK(id_is("https://www.youtube-nocookie.com/embed/dQw4w9WgXcQ", "dQw4w9WgXcQ"));
    CHECK(id_is("https://www.youtube.com/live/dQw4w9WgXcQ", "dQw4w9WgXcQ"));
    CHECK(id_is("https://www.youtube.com/watch?v=short", NULL));
    CHECK(id_is("https://www.youtube.com/channel/UCxyz", NULL));
    CHECK(id_is("https://notyoutube.com/watch?v=dQw4w9WgXcQ", NULL));
    CHECK(id_is("https://example.com/adhan.mp3", NULL));

    /* The solver loads and runs in QuickJS: a script that is not YouTube's player is
     * rejected by the solver itself ("unexpected structure"), not by a missing engine. */
    double ms;
    cJSON *res = solve("var a = function(b) { return b.split('').reverse().join(''); };", "n", "abcdef", &ms);
    CHECK(!res && strstr(g_err, "unexpected structure"));
    printf("solver round trip: %.0f ms (%s)\n", ms, g_err);
    cJSON_Delete(res);

    /* SNTV_YT_BENCH=<big.js>: how long the solver takes to parse a player-sized script. */
    const char *bench = getenv("SNTV_YT_BENCH");
    if (bench) {
        FILE *f = fopen(bench, "rb");
        if (f) {
            fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
            char *js = malloc((size_t)n + 1);
            js[fread(js, 1, (size_t)n, f)] = 0; fclose(f);
            res = solve(js, "n", "abcdef", &ms);
            printf("bench: %ld KB parsed in %.0f ms (%.120s)\n", n >> 10, ms, res ? "answered" : g_err);
            cJSON_Delete(res); free(js);
        }
    }
    if (failures) fprintf(stderr, "%d failure(s)\n", failures);
    else printf("youtube: all checks passed\n");
    return failures != 0;
}
