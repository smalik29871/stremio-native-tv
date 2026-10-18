/* The pure part of the memory watch, kept apart so the unit test links without the app. */
#include "memwatch.h"

int mem_level(long total_kb, long avail_kb, long rss_kb)
{
    if (total_kb <= 0) return 0;
    long tight = total_kb * 6 / 100, floor_kb = 120 * 1024;
    if (tight < floor_kb) tight = floor_kb;
    if (avail_kb > 0 && avail_kb < tight) return 2;
    if (rss_kb > 0 && rss_kb > total_kb * 45 / 100) return 2;
    long room = total_kb * 15 / 100; if (room < 300 * 1024) room = 300 * 1024;
    if (avail_kb > 0 && avail_kb < room) return 1;
    return 0;
}

