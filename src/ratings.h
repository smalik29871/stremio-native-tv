/* Your own ratings of titles and episodes: half stars, stored as 1..10 (0: not rated),
 * kept on this TV for each profile (the account signed in) and sent to Trakt and SimKL
 * when they are connected. id: a title's ("tt0903747") or an episode's ("tt0903747:2:3"). */
#ifndef SNTV_RATINGS_H
#define SNTV_RATINGS_H
#include "addon/addon.h"

int  rating_get(const char *id);
/* m: the title (for its type); 0 removes the rating. */
void rating_set(const MetaPreview *m, const char *id, int value);
/* The quick tags given with it ("Great ending"), | between them; "" none. */
const char *rating_tags(const char *id);
void rating_set_tags(const char *id, const char *tags);   /* only for a rated id */
/* on a "|" list in hand: is the tag in it, add or take it out */
int  rating_tag_has(const char *tags, const char *tag);
void rating_tag_toggle(char *tags, size_t cap, const char *tag);
/* "4.5" for 9 */
void rating_text(int value, char *out, size_t n);

#define RATING_TAGS_N 6
extern const char *const RATING_TAGS[RATING_TAGS_N];
#endif
