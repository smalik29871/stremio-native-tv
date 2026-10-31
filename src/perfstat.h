/* How fast the app is on this TV, measured where it runs: startup milestones, the
 * work in each frame per screen (without the wait for the display), and how long a
 * key takes to show on screen. Diagnostics > Speed shows it. */
#ifndef SNTV_PERFSTAT_H
#define SNTV_PERFSTAT_H
#include <stddef.h>

void perf_mark(const char *name);                   /* a startup milestone; only the first of each name counts */
/* One drawn frame: work_ms is update + draw before the swap (CPU), total_ms adds the
 * swap, where the GPU work and the wait for the display land; fill, the screens of
 * pixels it painted (the GPU's work: 1 is every pixel once). */
void perf_frame(const char *screen, float work_ms, float total_ms, float fill);
void perf_key(void);                                /* a key arrived */
/* A frame that sent pictures to the GPU: how long the sending took on the main thread (ms) and how much went. */
void perf_upload(float ms, size_t bytes);
/* The screen's refresh period in ms, from the frames: the most common whole-frame time between
 * 12 and 34 ms (a frame that kept up waited for the display that long); 16.7 until it is known. */
float perf_frame_period_ms(void);
float perf_snap_dt(float dt, int hz);   /* a frame step that landed on whole refreshes (within 30%) is exactly that long */
void  perf_set_refresh(float hz);   /* the display's own rate where the platform says it (Android); else it is read from the frame times */
void perf_shown(void);                              /* a frame reached the display */
/* A stream start, from the press to the first frame: begin names how it was asked for
 * ("Play", "chosen", "next episode"); each phase closes the time since the last one
 * ("sources", "check", "race", "player"); done ends it (ok 0: it failed). A begin while
 * one runs starts over. The last few are on the Speed tab and in the log. */
void perf_start_begin(const char *how);
void perf_start_phase(const char *phase);
void perf_start_done(int ok);
void perf_start_cancel(void);                       /* left before it played: not a start */
void perf_playback_begin(void);
void perf_playback_seek(unsigned presented, int supported, double target);   /* target: where it goes (timed by it playing on from there where the player can't say) */
void perf_playback_sample(double position, float dt, int playing, int buffering, unsigned dropped, unsigned load, unsigned presented);
void perf_playback_end(void);
void perf_report(char *out, size_t n);              /* lines for the screen and the log */
#endif
