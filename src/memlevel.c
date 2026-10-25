/* The pure part of the memory watch, kept apart so the unit test links without the app. */
#include "memwatch.h"

static int g_stage;   /* here, with the pure part, so a library user (the YouTube solver) can read it */
int  mem_stage(void) { return g_stage; }
void mem_stage_set(int st) { g_stage = st; }

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


int mem_stage_step(MemStageState *st, int level, float dt_s)
{
    if (level > st->stage) {
        st->down_for = 0;
        if ((st->up_for += dt_s) >= 10) { st->stage = level; st->up_for = 0; }
    } else if (level < st->stage) {
        st->up_for = 0;
        if ((st->down_for += dt_s) >= (st->stage == 2 ? 120 : 60)) { st->stage--; st->down_for = 0; }
    } else st->up_for = st->down_for = 0;
    return st->stage;
}
