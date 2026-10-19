/* The TV interruption test (Settings > Take breaks): can webOS bring Stremio to the
 * front at a set time? See alarmtest.c. */
#ifndef SNTV_ALARMTEST_H
#define SNTV_ALARMTEST_H
#include <stddef.h>

void alarmtest_start(int argc, char **argv);   /* at launch: its parameters, relaunch events */
void alarmtest_tick(void);                     /* once a frame */
void alarmtest_arm(int seconds);
void alarmtest_cancel(void);
int  alarmtest_armed(void);
void alarmtest_report(char *out, size_t n);    /* every reply so far, for the screen */
#endif
