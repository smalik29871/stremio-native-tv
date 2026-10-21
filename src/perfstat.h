/* How fast the app is on this TV, measured where it runs: startup milestones, the
 * work in each frame per screen (without the wait for the display), and how long a
 * key takes to show on screen. Diagnostics > Speed shows it. */
#ifndef SNTV_PERFSTAT_H
#define SNTV_PERFSTAT_H
#include <stddef.h>

void perf_mark(const char *name);                   /* a startup milestone; only the first of each name counts */
/* One drawn frame: work_ms is update + draw before the swap (CPU), total_ms adds the
 * swap, where the GPU work and the wait for the display land. */
void perf_frame(const char *screen, float work_ms, float total_ms);
void perf_key(void);                                /* a key arrived */
void perf_shown(void);                              /* a frame reached the display */
/* A stream start, from the press to the first frame: begin names how it was asked for
 * ("Play", "chosen", "next episode"); each phase closes the time since the last one
 * ("sources", "check", "race", "player"); done ends it (ok 0: it failed). A begin while
 * one runs starts over. The last few are on the Speed tab and in the log. */
void perf_start_begin(const char *how);
void perf_start_phase(const char *phase);
void perf_start_done(int ok);
void perf_start_cancel(void);                       /* left before it played: not a start */
void perf_report(char *out, size_t n);              /* lines for the screen and the log */
#endif
