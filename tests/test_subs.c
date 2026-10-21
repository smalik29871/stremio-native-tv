#include "subs.h"
#include "opensubs.h"
#include <math.h>
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

/* n cues, every `step` seconds from `from`, each 2 s long, the i-th one moved by jitter */
static SubTrack synth(int n, double from, double step)
{
    SubTrack t; memset(&t, 0, sizeof t);
    t.cues = calloc((size_t)n, sizeof *t.cues); t.n = n;
    for (int i = 0; i < n; i++) {
        double j = (i * 7919 % 13) * 0.37;   /* uneven gaps, so offsets are tellable */
        t.cues[i].start = from + i * step + j; t.cues[i].end = t.cues[i].start + 2; t.cues[i].text = strdup("x");
    }
    return t;
}

static void test_fit(void)
{
    char why[64];
    double dur = 6000;
    SubTrack good = synth(800, 60, 7.0);      /* ends ~5660: credits after */
    SubTrack early = synth(500, 60, 7.0);     /* ends ~3560: a shorter cut */
    SubTrack past = synth(900, 60, 7.2);      /* ends ~6540: past the video */
    SubTrack forced = synth(40, 60, 140.0);   /* a few lines across the film */
    int g = subs_fit(&good, dur, NULL, why, sizeof why);
    CHECK(strcmp(why, "ends with the video") == 0);
    int e = subs_fit(&early, dur, NULL, why, sizeof why);
    CHECK(strstr(why, "min before the end") != NULL);
    int p2 = subs_fit(&past, dur, NULL, why, sizeof why);
    CHECK(strcmp(why, "runs past the end of the video") == 0);
    int f = subs_fit(&forced, dur, NULL, why, sizeof why);
    CHECK(strcmp(why, "has too few lines for the whole video") == 0);
    CHECK(g > e && g > p2 && g > f);
    /* with the file-matched subtitle: the same timing, shifted 2 s, wins over a reshuffled one */
    SubTrack ref = synth(800, 62, 7.0);
    SubTrack other = synth(800, 60, 7.0);
    for (int i = 0; i < other.n; i++) { double j = (i * 104729 % 17) * 0.53; other.cues[i].start = 60 + i * 7.0 + j; other.cues[i].end = other.cues[i].start + 2; }
    int gr = subs_fit(&good, dur, &ref, why, sizeof why);
    CHECK(strcmp(why, "matches the file's timing") == 0);
    int orr = subs_fit(&other, dur, &ref, why, sizeof why);
    CHECK(gr > orr);
    printf("fit: good %d early %d past %d forced %d | with ref %d vs %d\n", g, e, p2, f, gr, orr);
    subs_free(&good); subs_free(&early); subs_free(&past); subs_free(&forced); subs_free(&ref); subs_free(&other);
}

static void test_names(void)
{
    const char *file = "The.Movie.2024.1080p.AMZN.WEB-DL.DDP5.1.H.264-FLUX.mkv";
    int same = opensubs_name_score("The.Movie.2024.1080p.AMZN.WEB-DL.DDP5.1.H.264-FLUX", file);
    int web = opensubs_name_score("The.Movie.2024.1080p.WEBRip.x264-RARBG", file);
    int bd = opensubs_name_score("The.Movie.2024.1080p.BluRay.x264-SPARKS", file);
    int ext = opensubs_name_score("The.Movie.2024.Extended.1080p.AMZN.WEB-DL-FLUX", file);
    printf("names: same %d web %d bluray %d extended %d\n", same, web, bd, ext);
    CHECK(same >= 80 && same > web && web > bd && same > ext);
    CHECK(opensubs_name_score("", file) == 0 && opensubs_name_score("x", NULL) == 0);
}

int main(void)
{
    test_names();
    test_fit();
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
    /* drift and a cut: subtitles timed for 25 fps (sped up 25/23.976) and 1.5 s early,
     * and from minute 30 a scene the subtitles' version lacks moves the rest 20 s later */
    {
        SubTrack ref = { 0 }, t = { 0 };
        int N = 700;
        ref.cues = calloc((size_t)N, sizeof(SubCue)); t.cues = calloc((size_t)N, sizeof(SubCue));
        unsigned seed = 11; double v = 4;
        char txt[16];
        for (int i = 0; i < N; i++) {
            seed = seed * 1103515245u + 12345u; v += 1.5 + (seed >> 16) % 600 / 100.0;
            double shown = v >= 1800 ? v + 20 : v;   /* the video has 20 s more from minute 30 */
            snprintf(txt, sizeof txt, "c%d", i);
            ref.cues[i].start = shown; ref.cues[i].end = shown + 1.2; ref.cues[i].text = strdup(txt);
            t.cues[i].start = v * 23.976 / 25.0 - 1.5; t.cues[i].end = t.cues[i].start + 1.1; t.cues[i].text = strdup(txt);
        }
        ref.n = t.n = N;
        CHECK(subs_sync(&ref, &t) == 1);
        CHECK(fabs(t.scale - 25.0 / 23.976) < 1e-6);
        int right = 0, asked = 0;
        for (int i = 5; i < N; i += 7) {   /* mid-cue on the video's clock shows the same line */
            double at = ref.cues[i].start + 0.5;
            const char *got = subs_at(&t, at);
            asked++;
            if (got && strcmp(got, ref.cues[i].text) == 0) right++;
        }
        CHECK(right >= asked * 9 / 10);
        subs_free(&ref); subs_free(&t);
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

    {   /* tags written escaped are dropped once decoded; a lone "<" stays */
        const char *esc = "1\n00:00:01,000 --> 00:00:02,000\n&lt;i&gt;Hello&lt;/i&gt; &lt;font color=\"#fff\"&gt;there&lt;/font&gt; <3\n";
        CHECK(subs_parse(esc, strlen(esc), &t) == 1 && strcmp(t.cues[0].text, "Hello there <3") == 0);
        subs_free(&t);
    }
    CHECK(subs_parse("garbage without cues", 20, &t) == -1);
    if (failures) { fprintf(stderr, "%d failure(s)\n", failures); return 1; }
    printf("all subtitle tests passed\n");
    return 0;
}
