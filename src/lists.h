/* Your lists: titles saved on this TV under a few names (Favourites, Watch later, With the
 * family, Rewatch). Each profile has its own, kept in the settings store as
 * "lists[:<email>]": {"<name>": [{id, type, name, poster}, newest first]}. */
#ifndef SNTV_LISTS_H
#define SNTV_LISTS_H
#include "addon/addon.h"

#define LIST_COUNT 4
#define LIST_MAX 200   /* titles a list keeps; the oldest go */
extern const char *const LIST_NAMES[LIST_COUNT];

/* The profile the lists belong to (the signed-in email, NULL when signed out). */
void lists_set_owner(const char *email);
int  lists_has(int list, const char *id);
/* Adds the title to the top of the list, or takes it out. 1 added, 0 removed, -1 bad list. */
int  lists_toggle(int list, const MetaPreview *m);
/* The list as previews, newest first (*out malloc'd; meta_previews_free). */
int  lists_items(int list, MetaPreview **out);
int  lists_count(int list);
/* Grows whenever a list changes (Home rebuilds its rows). */
int  lists_version(void);

#endif
