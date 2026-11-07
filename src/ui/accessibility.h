#ifndef SNTV_ACCESSIBILITY_H
#define SNTV_ACCESSIBILITY_H
#include <stddef.h>

struct Screen;
#define SNTV_ACCESS_EVENT 0x534E4158
/* A short buzz under the finger, as Android apps give: a long press, a step on a slider or a list moved, a choice made */
enum { HAPTIC_LONG_PRESS, HAPTIC_TICK, HAPTIC_CONFIRM };
void ui_haptic(int kind);
#ifdef SNTV_PLATFORM_ANDROID
void ui_access_begin(struct Screen *screen);
void ui_access_text(const char *text, size_t n, float x, float y, float w, float h, int priority);
void ui_access_hit(float x, float y, float w, float h, int id);
void ui_access_publish(void);
int ui_access_target(unsigned version, int id);
int ui_access_enabled(void);
#else
static inline void ui_access_begin(struct Screen *s) { (void)s; }
static inline void ui_access_text(const char *s, size_t n, float x, float y, float w, float h, int priority) { (void)s; (void)n; (void)x; (void)y; (void)w; (void)h; (void)priority; }
static inline void ui_access_hit(float x, float y, float w, float h, int id) { (void)x; (void)y; (void)w; (void)h; (void)id; }
static inline void ui_access_publish(void) {}
static inline int ui_access_target(unsigned version, int id) { (void)version; (void)id; return 0; }
static inline int ui_access_enabled(void) { return 0; }
#endif
#endif
