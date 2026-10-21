#include "quality.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(c) do { if (!(c)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

static long score_of(const char *name, const char *title, const TvProfile *tv, const QualityPrefs *p, double runtime)
{
    StreamTags t;
    quality_parse(name, title, NULL, NULL, 0, &t);
    return quality_score(&t, tv, p, runtime);
}

int main(void)
{
    StreamTags t;
    quality_parse("Torrentio TB\n1080p", "The.Show.S04E10.1080p.BluRay.x265.DTS-HD.MA.5.1-GRP.mkv\n\xF0\x9F\x91\xA4 12 \xF0\x9F\x92\xBE 2.83 GB \xE2\x9A\x99\xEF\xB8\x8F ThePirateBay", NULL, NULL, 0, &t);
    CHECK(t.res == 1080 && t.codec == CODEC_HEVC && t.audio == AUDIO_DTS && t.seeders == 12 && fabs(t.size_gb - 2.83) < 0.01 && t.tier == TIER_WEB && t.cached == 0);
    quality_parse("[TB+] Torrentio\n4k DV", "Movie.2024.2160p.WEB-DL.DV.HDR10.DDP5.1.Atmos.H.265\n\xE2\x9A\xA1 \xF0\x9F\x92\xBE 18.5 GB", NULL, NULL, 0, &t);
    CHECK(t.res == 2160 && t.hdr == 2 && t.audio == AUDIO_DDP && t.atmos && t.cached == 1 && fabs(t.size_gb - 18.5) < 0.01);
    quality_parse("HdHub", "[PixelDrain] [ 300.08 MB] Show.S04E10.1080p.10bit.BluRay.English.DDP.5.1.x265-PHOCiS.mkv", NULL, NULL, 0, &t);
    CHECK(t.res == 1080 && t.codec == CODEC_HEVC && t.audio == AUDIO_DDP && fabs(t.size_gb - 300.08 / 1024) < 0.001);
    quality_parse("Cam", "Movie 2025 HDCAM 720p AAC", NULL, NULL, 0, &t);
    CHECK(t.tier == TIER_CAM && t.audio == AUDIO_AAC);
    quality_parse("x", "Movie.DVDRip.XviD-GRP", NULL, NULL, 0, &t);
    CHECK(t.codec == CODEC_OLD && t.tier == TIER_TV && t.res == 0 && t.hdr == 0);   /* "dvdrip" is not Dolby Vision */
    quality_parse("[TB\xE2\x9A\xA1] Comet 360p", "HDRip DD 2.0 | 288 MB", NULL, "The.Middle.s02e14.WEB-DLRip.Ukr.Eng.avi", 0, &t);
    CHECK(t.hdr == 0 && t.res == 360 && t.cached == 1);   /* "HDRip" and "HDTV" are not HDR */
    quality_parse("Show.S01E01.2160p.WEB-DL.HDR10.HEVC", NULL, NULL, NULL, 0, &t);
    CHECK(t.hdr == 1);
    quality_parse("x", "Movie 2160p AV1 Opus", NULL, NULL, 3221225472ULL, &t);
    CHECK(t.codec == CODEC_AV1 && t.audio == AUDIO_OTHER && fabs(t.size_gb - 3.0) < 0.01);

    TvProfile old;   /* a 2021 LG: webOS 6, no DTS, no TrueHD, AV1 fine */
    quality_tv_init(&old, "webOS TV 6.3.0-1234 (kisscurl-kinglake)");
    CHECK(old.webos_major == 6);
    CHECK(!quality_cap_ok(&old, TVCAP_DTS) && !quality_cap_ok(&old, TVCAP_TRUEHD) && quality_cap_ok(&old, TVCAP_AV1) && quality_cap_ok(&old, TVCAP_DV));
    TvProfile ancient;
    quality_tv_init(&ancient, "webOS TV 3.5.0");
    CHECK(!quality_cap_ok(&ancient, TVCAP_AV1));
    TvProfile desktop;
    quality_tv_init(&desktop, NULL);
    CHECK(desktop.webos_major == 0 && quality_cap_ok(&desktop, TVCAP_DTS));

    QualityPrefs best = { QUAL_BEST, 2160 };
    /* a cached 1080p HEVC with DD+ beats a 4K remux with DTS on a TV that cannot do DTS */
    long a = score_of("[TB+] Torrentio\n1080p", "Show.1080p.WEB-DL.DDP5.1.x265 \xE2\x9A\xA1 4.1 GB", &old, &best, 45);
    long b = score_of("Torrentio\n4k", "Show.2160p.REMUX.DTS-HD.MA.7.1 \xF0\x9F\x91\xA4 80 \xF0\x9F\x92\xBE 40 GB", &old, &best, 45);
    CHECK(a > b);
    /* same two on a TV with DTS proven to work, highest-quality mode: the 4K wins */
    TvProfile proven = old;
    quality_tv_learn(&proven, 1u << TVCAP_DTS, 1);
    QualityPrefs highest = { QUAL_HIGHEST, 2160 };
    CHECK(score_of("Torrentio\n4k", "Show.2160p.REMUX.DTS-HD.MA.7.1 \xF0\x9F\x91\xA4 80 \xF0\x9F\x92\xBE 40 GB", &proven, &highest, 45)
          > score_of("[TB+] Torrentio\n1080p", "Show.1080p.WEB-DL.DDP5.1.x265 \xE2\x9A\xA1 4.1 GB", &proven, &highest, 45));
    /* a failure flips the belief back */
    quality_tv_learn(&proven, 1u << TVCAP_DTS, 0);
    quality_tv_learn(&proven, 1u << TVCAP_DTS, 0);
    CHECK(!quality_cap_ok(&proven, TVCAP_DTS));
    /* resolution cap and saver mode prefer the smaller file */
    QualityPrefs cap1080 = { QUAL_BEST, 1080 };
    CHECK(score_of("a", "Show.1080p.WEB-DL.x264 AAC \xE2\x9A\xA1", &old, &cap1080, 45) > score_of("b", "Show.2160p.WEB-DL.x265 AAC \xE2\x9A\xA1", &old, &cap1080, 45));
    QualityPrefs saver = { QUAL_SAVER, 2160 };
    CHECK(score_of("a", "Show.720p.WEBRip.x264 AAC \xE2\x9A\xA1 0.7 GB", &old, &saver, 45) > score_of("b", "Show.2160p.WEB-DL.x265 \xE2\x9A\xA1 12 GB", &old, &saver, 45));
    /* unplayable things fall below -500 */
    CHECK(score_of("a", "Movie.2160p.AV1.Opus \xE2\x9A\xA1", &ancient, &best, 100) < -500);
    CHECK(score_of("a", "Movie HDCAM 1080p", &old, &best, 100) < -500);
    CHECK(score_of("a", "Movie 1080p x265 \xE2\x8F\xB3 not cached", &old, &best, 100) < -300);

    const char *w[3];
    quality_parse("x", "Show.2160p.DTS.x265 \xE2\x8F\xB3", NULL, NULL, 0, &t);
    int nw = quality_warnings(&t, &old, &best, w, 3);
    CHECK(nw == 2 && strstr(w[0], "DTS") && strstr(w[1], "Not cached"));
    char sum[200];
    quality_parse("x", "Show.2160p.DV.HDR.DDP5.1.Atmos.x265 \xE2\x9A\xA1 9 GB", NULL, NULL, 0, &t);
    quality_summary(&t, 60, sum, sizeof sum);
    CHECK(strstr(sum, "4K HEVC Dolby Vision") && strstr(sum, "Dolby Digital+ Atmos") && strstr(sum, "cached") && strstr(sum, "20 Mbit/s"));
    CHECK(quality_needs(&t) == ((1u << TVCAP_DV) | (1u << TVCAP_HEVC) | (1u << TVCAP_4K)));

    CHECK(fabs(quality_runtime_minutes("49 min") - 49) < 0.01);
    CHECK(fabs(quality_runtime_minutes("1h 40min") - 100) < 0.01);
    CHECK(fabs(quality_runtime_minutes("95") - 95) < 0.01);
    CHECK(quality_runtime_minutes(NULL) == 0);

    /* release groups: episodes from one group match each other */
    char g[32];
    quality_release_group("The.Show.S01E02.1080p.WEB.h264-NTb.mkv", g, sizeof g); CHECK(!strcmp(g, "NTb"));
    quality_release_group("[SubsPlease] Show - 02 (1080p) [ABCD1234].mkv", g, sizeof g); CHECK(!strcmp(g, "SubsPlease"));
    quality_release_group("Movie.2024.1080p.WEB-DL", g, sizeof g); CHECK(!strcmp(g, ""));
    quality_release_group("The.Show.S04E10.1080p.BluRay.x265.DTS-HD.MA.5.1-GRP.mkv\n12 seeds", g, sizeof g); CHECK(!strcmp(g, "GRP"));
    quality_release_group("Show 2160p - 7", g, sizeof g); CHECK(!strcmp(g, ""));
    quality_release_group(NULL, g, sizeof g); CHECK(!strcmp(g, ""));

    /* which file in a season pack is the episode */
    EpisodeHint e = { .season = 1, .episode = 2, .absolute = 0 };
    snprintf(e.title, sizeof e.title, "Sanctuary");
    CHECK(quality_episode_score("Carrie S01/Carrie.S01E02.1080p.WEB.h264-NTb.mkv", &e) == 3);
    CHECK(quality_episode_score("Carrie S01E01-E08/Carrie.S01E01.1080p.mkv", &e) == -1);
    CHECK(quality_episode_score("Show.S01E01E02.720p.mkv", &e) == 3);
    CHECK(quality_episode_score("Show 1x02 Name.avi", &e) == 3);
    CHECK(quality_episode_score("Show S01 E02.mkv", &e) == 3);
    CHECK(quality_episode_score("Show/Season 1/02 - Something.mkv", &e) == 2);
    CHECK(quality_episode_score("Show/Season 2/02 - Something.mkv", &e) == 0);
    CHECK(quality_episode_score("Show/Season 1/Show - Sanctuary.mkv", &e) == 2);
    CHECK(quality_episode_score("Show Complete/Episode 2.mkv", &e) == 2);
    CHECK(quality_episode_score("Show.S01.DD5.1.x264.mkv", &(EpisodeHint){ .season = 1, .episode = 5 }) == 0);
    CHECK(quality_episode_score("[Group] Show - 14 [1080p].mkv", &(EpisodeHint){ .season = 2, .episode = 1, .absolute = 14 }) == 2);
    CHECK(quality_episode_score("Show.S01E02.mkv", &(EpisodeHint){ 0 }) == 0);

    if (failures) { fprintf(stderr, "%d failure(s)\n", failures); return 1; }
    printf("quality: all checks passed\n");
    return 0;
}
