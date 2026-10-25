#include "memwatch.h"
#include <stdio.h>

static int failures;
#define CHECK(c) do { if (!(c)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

int main(void)
{
    long GB = 1024 * 1024;
    CHECK(mem_level(0, 0, 0) == 0);                       /* unknown: never panic */
    CHECK(mem_level(4 * GB, 2 * GB, 200 * 1024) == 0);     /* a roomy TV */
    CHECK(mem_level(4 * GB, 500 * 1024, 200 * 1024) == 1); /* getting low: 500 MB of 4 GB */
    CHECK(mem_level(4 * GB, 200 * 1024, 200 * 1024) == 2); /* tight: under 6% */
    CHECK(mem_level(1 * GB, 100 * 1024, 100 * 1024) == 2); /* tight: under the 120 MB floor */
    CHECK(mem_level(1 * GB, 400 * 1024, 100 * 1024) == 0); /* 40% free of a small TV is fine */
    CHECK(mem_level(1 * GB, 400 * 1024, 600 * 1024) == 2); /* the app itself has grown too big */
    CHECK(mem_level(2 * GB, 0, 100 * 1024) == 0);          /* no availability figure: rely on rss only */
    /* the stages: up after 10 s at a level, down a stage at a time after a while of room */
    MemStageState st = { 0 };
    CHECK(mem_stage_step(&st, 2, 5) == 0);                 /* one tight sample is not yet a stage */
    CHECK(mem_stage_step(&st, 2, 5) == 2);                 /* ten seconds: tight */
    CHECK(mem_stage_step(&st, 0, 60) == 2);                /* a minute of room is not enough from tight */
    CHECK(mem_stage_step(&st, 0, 60) == 1);                /* two minutes: down one */
    CHECK(mem_stage_step(&st, 1, 30) == 1);                /* getting low again keeps it there */
    CHECK(mem_stage_step(&st, 0, 30) == 1 && mem_stage_step(&st, 0, 30) == 0);   /* a minute of room: plenty again */
    CHECK(mem_stage_step(&st, 1, 5) == 0 && mem_stage_step(&st, 0, 5) == 0 && mem_stage_step(&st, 1, 5) == 0);   /* a blip does not count */
    if (failures) { fprintf(stderr, "%d failure(s)\n", failures); return 1; }
    printf("memwatch: all checks passed\n");
    return 0;
}
