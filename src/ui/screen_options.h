#pragma once
#include "../app.h"
typedef void (*OptionsFn)(int choice, void *user);
Screen *screen_options_create(const char *title, const char *subtitle, const char *const *items, int n, OptionsFn fn, void *user);
/* With a paragraph above the items (may be NULL) and an Icon per item (NULL, or -1 for none). */
Screen *screen_options_create_ex(const char *title, const char *subtitle, const char *body,
                                 const char *const *items, const int *icons, int n, OptionsFn fn, void *user);
void screen_options_for_title(const MetaPreview *m);   /* watched / Continue Watching / Library / lists */
void screen_options_lists(const MetaPreview *m);       /* Save to a list */
