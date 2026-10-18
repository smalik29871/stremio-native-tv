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
    if (failures) { fprintf(stderr, "%d failure(s)\n", failures); return 1; }
    printf("memwatch: all checks passed\n");
    return 0;
}
