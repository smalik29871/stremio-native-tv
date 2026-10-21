#ifndef SNTV_ISLAND_H
#define SNTV_ISLAND_H
#include "../app.h"

/* The island: one pill in the corner (top centre while a video plays) that is the clock
 * when nothing is going on and grows into whatever is: a notice (app_toast), a job in
 * progress, or the title left mid-way (Blue opens it). It morphs between them on a spring
 * instead of stacking separate pop-ups. */

/* A job in progress; call every frame it runs (it goes after half a second without one).
 * frac < 0 for a job with no measurable progress. */
void island_progress(const char *text, float frac);
/* The title the viewer just left mid-way: "Resume" stays in the island for a while. */
void island_resume(const MetaPreview *meta, const char *line, int minutes_left);
/* Blue on a browsing screen: opens the title being offered for resuming. 1 when it did. */
int  island_key_blue(void);
/* The resume card as a focus stop (Up from the top of Home): 1 when it took the focus. */
int  island_take_focus(void);
int  island_has_focus(void);
void island_drop_focus(void);   /* a screen opened over it: keys go to that screen */
/* Keys while it has the focus: OK opens the title, Down or Back hand the focus back. */
void island_focus_key(int ok);
void island_draw(int in_player, int show_clock);
/* The bottom edge of the island in the corner as last drawn (0 when none), so things
 * placed under it move down as it grows into a notice instead of hiding behind it. */
float island_bottom(void);

#endif
