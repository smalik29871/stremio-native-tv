#include "probe.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
#define CHECK(c) do { if (!(c)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

static uint8_t *load(const char *name, size_t *len, size_t cap)
{
    char path[1024];
    snprintf(path, sizeof path, "%s/media/%s", SNTV_FIXTURES, name);
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "missing %s\n", path); exit(2); }
    uint8_t *b = malloc(cap);
    *len = fread(b, 1, cap, f);
    fclose(f);
    return b;
}

int main(void)
{
    size_t n;
    ProbeInfo p;
    uint8_t *d = load("sample_h264_aac.mkv", &n, 1 << 20);
    CHECK(probe_parse(d, n, &p) == 1);
    CHECK(p.container == PROBE_MKV && p.vcodec == CODEC_AVC && p.width == 320 && p.height == 180 && p.acodec == AUDIO_AAC && p.audio_channels == 2);
    CHECK(fabs(p.duration_s - 1.0) < 0.1 && p.n_chapters == 3 && !strcmp(p.chapters[1].name, "Part 2") && fabs(p.chapters[1].start_s - 0.4) < 0.01);
    CHECK(fabs(p.fps - 24) < 0.5 && p.hdr == 0);
    int audio_lang_eng = 0;
    for (int i = 0; i < p.n_tracks; i++) if (p.tracks[i].kind == 2 && !strcmp(p.tracks[i].lang, "eng")) audio_lang_eng = 1;
    CHECK(audio_lang_eng);
    /* the window can cut the header: the first 600 bytes still give the tracks */
    ProbeInfo q;
    probe_parse(d, 600, &q);
    CHECK(q.n_tracks >= 1 && q.vcodec == CODEC_AVC);
    free(d);

    d = load("sample_hevc_eac3.mkv", &n, 1 << 20);
    CHECK(probe_parse(d, n, &p) == 1);
    CHECK(p.vcodec == CODEC_HEVC && p.acodec == AUDIO_DDP && p.audio_channels == 6);
    char desc[200];
    probe_describe(&p, desc, sizeof desc);
    CHECK(strstr(desc, "HEVC 180p") && strstr(desc, "Dolby Digital+ 5.1"));
    StreamTags t;
    quality_parse("x", "Movie.2160p.DTS.x264", NULL, NULL, 0, &t);   /* the name lies */
    probe_apply(&p, &t);
    CHECK(t.res == 480 && t.codec == CODEC_HEVC && t.audio == AUDIO_DDP);
    free(d);

    d = load("sample_h264_dts.mkv", &n, 1 << 20);
    CHECK(probe_parse(d, n, &p) == 1 && p.acodec == AUDIO_DTS);
    free(d);

    d = load("sample_h264_aac.mp4", &n, 1 << 20);
    CHECK(probe_parse(d, n, &p) == 1);
    CHECK(p.container == PROBE_MP4 && p.vcodec == CODEC_AVC && p.width == 320 && p.height == 180 && p.acodec == AUDIO_AAC && fabs(p.duration_s - 1.0) < 0.1);
    CHECK(fabs(p.fps - 24) < 0.5);
    CHECK(fabs(probe_mbits(&p, 1250000) - 10.0) < 0.5);
    free(d);

    d = load("sample_hevc_eac3.mp4", &n, 1 << 20);
    CHECK(probe_parse(d, n, &p) == 1 && p.vcodec == CODEC_HEVC && p.acodec == AUDIO_DDP && p.audio_channels == 6);
    free(d);

    /* an MP4 whose moov is at the end: only ftyp + mdat in the window */
    uint8_t tail[64] = { 0, 0, 0, 24, 'f', 't', 'y', 'p', 'i', 's', 'o', 'm', 0, 0, 0, 1, 'i', 's', 'o', 'm', 'a', 'v', 'c', '1', 0, 0, 0, 0, 'm', 'd', 'a', 't' };
    CHECK(probe_parse(tail, sizeof tail, &p) == 0 && p.container == PROBE_MP4 && p.moov_missing);
    probe_describe(&p, desc, sizeof desc);
    CHECK(strstr(desc, "index at the end"));
    /* garbage */
    uint8_t junk[32] = { 'h', 'e', 'l', 'l', 'o' };
    CHECK(probe_parse(junk, sizeof junk, &p) == 0 && p.container == PROBE_NONE);
    CHECK(probe_parse(NULL, 0, &p) == 0);

    if (failures) { fprintf(stderr, "%d failure(s)\n", failures); return 1; }
    printf("probe: all checks passed\n");
    return 0;
}
