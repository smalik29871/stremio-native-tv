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
