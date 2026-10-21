#include "quality.h"
#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(c) do { if (!(c)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

#define GB 1e9
#define PICK(files, addon) quality_pick_episode_file(&e5, files, (int)(sizeof files / sizeof files[0]), addon)

int main(void)
{
    EpisodeHint e5 = { .show = "Breaking Bad", .season = 1, .episode = 5 };

    PackFile named[] = { { 1, 1.2 * GB, "Breaking Bad S01/Breaking.Bad.S01E04.1080p.mkv", 0 },
                               { 2, 1.1 * GB, "Breaking Bad S01/Breaking.Bad.S01E05.1080p.mkv", 0 },
                               { 3, 1.3 * GB, "Breaking Bad S01/Breaking.Bad.S01E06.1080p.mkv", 0 } };
    CHECK(PICK(named, -1) == 2);                       /* named for it, not the largest */
    CHECK(PICK(named, 1) == PICK_NONE);         /* the addon's file is another episode */
    CHECK(PICK(named, 2) == 2);                        /* the addon's file, and it is this one */

    PackFile alike[] = { { 1, 1.20 * GB, "Show/video_a.mkv", 0 }, { 2, 1.15 * GB, "Show/video_b.mkv", 0 } };
    CHECK(PICK(alike, -1) == PICK_ASK);         /* two unnamed look-alikes: no guess, only the AI may say */

    PackFile disc[] = { { 1, 1.2 * GB, "D1/title_t00.mkv", 0 }, { 2, 1.1 * GB, "D1/title_t01.mkv", 0 }, { 3, 1.0 * GB, "D1/title_t02.mkv", 0 } };
    CHECK(PICK(disc, -1) == PICK_ASK);

    PackFile wrong_one[] = { { 7, 1.2 * GB, "Breaking.Bad.S01E04.1080p.WEB.mkv", 0 } };
    CHECK(PICK(wrong_one, -1) == PICK_NONE);    /* alone, named for another episode */
    CHECK(PICK(wrong_one, 7) == PICK_NONE);     /* and the addon pointing at it changes nothing */

    PackFile one[] = { { 7, 1.2 * GB, "Breaking Bad 105.mkv", 0 } };
    CHECK(PICK(one, -1) == 7);                         /* alone and not named otherwise: it plays */

    PackFile main_and_sample[] = { { 1, 1.2 * GB, "Release/release.mkv", 0 }, { 2, 0.05 * GB, "Release/sample.mkv", 0 } };
    CHECK(PICK(main_and_sample, -1) == 1);             /* a sample is not a look-alike */

    PackFile unnamed_addon[] = { { 1, 1.2 * GB, "Show/video_a.mkv", 0 }, { 2, 1.15 * GB, "Show/video_b.mkv", 0 } };
    CHECK(PICK(unnamed_addon, 2) == 2);                /* the addon said which, and nothing says otherwise */

    PackFile others[] = { { 1, 1.2 * GB, "S01E02.mkv", 0 }, { 2, 1.2 * GB, "S01E03.mkv", 0 }, { 3, 1.5 * GB, "extras.mkv", 0 } };
    CHECK(PICK(others, -1) == PICK_NONE);       /* a pack naming other episodes: not its extra either */

    if (failures) { fprintf(stderr, "%d failure(s)\n", failures); return 1; }
    printf("torbox picks ok\n");
    return 0;
}
