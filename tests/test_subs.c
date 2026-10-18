#include "subs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
#define CHECK(c) do { if (!(c)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

static char *read_fixture(const char *name, size_t *len)
{
    char path[1024];
    snprintf(path, sizeof path, "%s/%s", SNTV_FIXTURES, name);
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "missing %s\n", path); exit(2); }
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    char *b = malloc((size_t)n + 1);
    if (fread(b, 1, (size_t)n, f) != (size_t)n) exit(2);
    b[n] = 0; fclose(f);
    *len = (size_t)n;
    return b;
}

int main(void)
{
    /* alignment: the same cues shifted by 2.3 s, with a few extra and missing ones */
    {
        SubTrack a = { 0 }, b = { 0 };
        a.cues = calloc(40, sizeof(SubCue)); b.cues = calloc(40, sizeof(SubCue));
        unsigned seed = 7; double t0 = 5;
        for (int i = 0; i < 30; i++) { seed = seed * 1103515245u + 12345u; t0 += 2 + (seed >> 16) % 900 / 100.0; a.cues[a.n].start = t0; a.cues[a.n].end = t0 + 2; a.cues[a.n].text = strdup("x"); a.n++; }
        for (int i = 0; i < 30; i++) { if (i % 7 == 0) continue; b.cues[b.n].start = a.cues[i].start - 2.3; b.cues[b.n].end = b.cues[b.n].start + 2; b.cues[b.n].text = strdup("y"); b.n++; }
        b.cues[b.n].start = 3.1; b.cues[b.n].end = 4; b.cues[b.n].text = strdup("extra"); b.n++;
        double off = 0;
        CHECK(subs_align(&a, &b, &off) == 1);
        CHECK(off > 2.2 && off < 2.4);
        SubTrack c = { 0 }; c.cues = calloc(10, sizeof(SubCue));
        for (int i = 0; i < 8; i++) { c.cues[c.n].start = 1000 + i * 13.1; c.cues[c.n].end = c.cues[c.n].start + 1; c.cues[c.n].text = strdup("z"); c.n++; }
        CHECK(subs_align(&a, &c, &off) == 0);   /* nothing in common: no guess */
        subs_free(&a); subs_free(&b); subs_free(&c);
    }
    /* UTF-16 LE with a BOM */
    {
        const char *src = "1\r\n00:00:01,000 --> 00:00:02,000\r\nCaf\xC3\xA9 \xE2\x82\xAC\r\n";
        unsigned char w[400]; size_t k = 0; w[k++] = 0xFF; w[k++] = 0xFE;
        for (const unsigned char *p = (const unsigned char *)src; *p;) {
            unsigned cp = *p;
            if (cp >= 0xE0) { cp = (p[0] & 0x0F) << 12 | (p[1] & 0x3F) << 6 | (p[2] & 0x3F); p += 3; }
            else if (cp >= 0xC0) { cp = (p[0] & 0x1F) << 6 | (p[1] & 0x3F); p += 2; }
            else p++;
            w[k++] = (unsigned char)(cp & 0xFF); w[k++] = (unsigned char)(cp >> 8);
        }
        SubTrack u16;
        CHECK(subs_parse((const char *)w, k, &u16) == 1 && strcmp(u16.cues[0].text, "Caf\xC3\xA9 \xE2\x82\xAC") == 0);
        subs_free(&u16);
    }
    size_t len;
    char *d = read_fixture("sample.srt", &len);
    SubTrack t;
    CHECK(subs_parse(d, len, &t) == 3);
    free(d);
    CHECK(strcmp(t.cues[0].text, "Hello there.") == 0);
    CHECK(t.cues[0].start == 1.0 && t.cues[0].end == 3.5);
    CHECK(strcmp(t.cues[1].text, "Second line\nwith two rows & an entity") == 0);
    CHECK(strcmp(t.cues[2].text, "Caf\xC3\xA9 au lait") == 0);   /* cp1252 -> UTF-8 */
    CHECK(subs_at(&t, 2.0) && strcmp(subs_at(&t, 2.0), "Hello there.") == 0);
    CHECK(subs_at(&t, 3.7) == NULL);
    CHECK(subs_at(&t, 62.3) && strncmp(subs_at(&t, 62.3), "Caf", 3) == 0);
    t.offset = 1.0;   /* positive offset delays cues: [1,3.5] now shows during [2,4.5] */
    CHECK(subs_at(&t, 2.5) && subs_at(&t, 4.2) && subs_at(&t, 1.5) == NULL);
    subs_free(&t);

    d = read_fixture("sample.vtt", &len);
    CHECK(subs_parse(d, len, &t) == 2);
    free(d);
    CHECK(strcmp(t.cues[0].text, "Styled cue") == 0 && t.cues[0].start == 1.0);
    CHECK(strcmp(t.cues[1].text, "Line A\nLine B") == 0 && t.cues[1].end == 12.0);
    subs_free(&t);

    CHECK(subs_parse("garbage without cues", 20, &t) == -1);
    if (failures) { fprintf(stderr, "%d failure(s)\n", failures); return 1; }
    printf("all subtitle tests passed\n");
    return 0;
}
