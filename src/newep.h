/* New episodes of the series you keep up with, as Stremio's own apps show them: a show
 * you had caught up on comes back to the front of Continue Watching with a badge when
 * an episode airs after you last watched it. Episode lists come from each show's meta
 * addon (cached for hours, the same cache as the Calendar). */
#ifndef SNTV_NEWEP_H
#define SNTV_NEWEP_H
#include <stdint.h>

typedef struct NewEp {
    char id[96];            /* the series */
    char video_id[128];     /* the first new episode */
    int count;              /* new episodes since you last watched */
    int64_t released_ms;    /* when the newest of them aired */
} NewEp;

/* Main loop: checks the library a little after start and every few hours. */
void newep_tick(void);
/* The series' new episodes, or NULL (none, played since, or put away). */
const NewEp *newep_get(const char *id);
/* "Remove from Continue Watching": hidden until a later episode airs. */
void newep_dismiss(const char *id);

#endif
