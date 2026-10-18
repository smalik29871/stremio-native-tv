#pragma once
#include "../app.h"
typedef void (*OptionsFn)(int choice, void *user);
Screen *screen_options_create(const char *title, const char *subtitle, const char *const *items, int n, OptionsFn fn, void *user);
void screen_options_for_title(const MetaPreview *m);   /* watched / Continue Watching / Library */
