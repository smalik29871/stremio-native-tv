/* Content advisories, as streaming services show when a video starts ("TV-MA ·
 * violence, language"): IMDb's parents guide rates five themes (sex and nudity,
 * violence and gore, profanity, alcohol and drugs, frightening scenes) None, Mild,
 * Moderate or Severe from viewers' votes. Kept per title for the session. */
#ifndef SNTV_ADVISORY_H
#define SNTV_ADVISORY_H

enum { ADV_THEMES = 5 };

typedef struct Advisory {
    int ready;                 /* the answer is in (themes may still all be 0) */
    int level[ADV_THEMES];     /* 0 none or unknown, 1 mild, 2 moderate, 3 severe */
} Advisory;

/* Short names for the themes, in the order of Advisory.level ("nudity", "violence", ...) */
extern const char *const ADVISORY_THEMES[ADV_THEMES];

/* Asks IMDb once per title (an IMDb id, "tt123" or "tt123:1:2"); later calls are free. */
void advisory_fetch(const char *id);
/* The answer for the title, or NULL while it is on its way (or not an IMDb title). */
const Advisory *advisory_get(const char *id);
/* "Severe violence, moderate language, mild nudity": the rated themes, strongest
 * first; "" when IMDb rated none of them. */
void advisory_describe(const Advisory *a, char *out, int n);

#endif
