/* The film's own text subtitles, read from its bytes: the tracks from its start, the lines
 * from the bytes as they would come from the buffer (in uneven pieces, and from a jump). */
#include "../src/mkvsubs.h"
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* the relay (linked for the buffer's replay) asks these of memwatch.c, which is not in the core */
#include "../src/memwatch.h"
int mem_sample(MemSample *out) { out->total_kb = 2048 * 1024; out->avail_kb = 1024 * 1024; out->rss_kb = 100 * 1024; return 1; }
int mem_small_tv(void) { return 0; }

static int fails;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static uint8_t *load(const char *path, size_t *n)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long L = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *b = malloc((size_t)L);
    *n = fread(b, 1, (size_t)L, f);
    fclose(f);
    return b;
}

static int lines(int number, SubCue *out, int max) { return mkvsubs_take(number, 0, out, max); }

static void drop(SubCue *c, int n) { for (int i = 0; i < n; i++) free(c[i].text); }

int main(void)
{
    SDL_Init(0);
    size_t n = 0;
    uint8_t *f = load(SNTV_FIXTURES "/media/subs_h264.mkv", &n);
    CHECK(f && n > 1000);
    if (!f) return 1;

    MkvSubHead h;
    CHECK(mkvsubs_parse_head(f, n, &h));
    CHECK(h.n == 2);
    CHECK(h.t[0].text && !h.t[0].ass && !strcmp(h.t[0].lang, "eng") && !strcmp(h.t[0].codec, "S_TEXT/UTF8"));
    CHECK(h.t[1].text && h.t[1].ass && !strcmp(h.t[1].lang, "por"));
    CHECK(h.tick_ms > 0.99 && h.tick_ms < 1.01);
    uint8_t not_mkv[64] = "RIFF....WEBP";
    MkvSubHead none;
    CHECK(!mkvsubs_parse_head(not_mkv, sizeof not_mkv, &none));

    /* the whole file, in uneven pieces */
    mkvsubs_reset(&h);
    for (size_t at = 0, step = 1; at < n; at += step, step = step * 7 % 4093 + 1)
        mkvsubs_feed(at, f + at, at + step < n ? step : n - at);
    SubCue c[16];
    int ne = lines(h.t[0].number, c, 16);
    CHECK(ne == 3);
    if (ne == 3) {
        CHECK(c[0].start > 0.99 && c[0].start < 1.01 && c[0].end > 3.49 && c[0].end < 3.51);
        CHECK(!strcmp(c[0].text, "Hello from the film itself."));
        CHECK(!strcmp(c[1].text, "Second line\non two rows."));
        CHECK(c[2].start > 7.99 && c[2].start < 8.01 && !c[0].top);
    }
    drop(c, ne);
    int np = lines(h.t[1].number, c, 16);
    CHECK(np == 2);
    if (np == 2) {
        CHECK(!strcmp(c[0].text, "Ol\xC3\xA1 do filme,\nlinha dois") && !c[0].top);
        CHECK(!strcmp(c[1].text, "Placa no topo") && c[1].top);
        CHECK(c[1].start > 4.99 && c[1].start < 5.01 && c[1].end > 6.49 && c[1].end < 6.51);
    }
    drop(c, np);

    /* from the middle (a seek): picked up at the next cluster; the same bytes again add nothing */
    mkvsubs_reset(&h);
    size_t mid = n / 3;
    for (size_t at = mid; at < n; at += 1000) mkvsubs_feed(at, f + at, at + 1000 < n ? 1000 : n - at);
    int after = mkvsubs_count(h.t[0].number);
    CHECK(after >= 1 && after <= 3);
    for (size_t at = 0; at < n; at += 3000) mkvsubs_feed(at, f + at, at + 3000 < n ? 3000 : n - at);
    CHECK(mkvsubs_count(h.t[0].number) == 3);

    /* garbage between (a broken download) does not crash or invent lines */
    mkvsubs_reset(&h);
    uint8_t junk[5000];
    for (size_t i = 0; i < sizeof junk; i++) junk[i] = (uint8_t)(i * 131 + 7);
    mkvsubs_feed(12345, junk, sizeof junk);
    CHECK(mkvsubs_count(h.t[0].number) == 0 && mkvsubs_count(h.t[1].number) == 0);

    free(f);
    if (fails) { fprintf(stderr, "%d failed\n", fails); return 1; }
    printf("mkvsubs: ok\n");
    return 0;
}
