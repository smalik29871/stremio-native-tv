#ifndef SNTV_WINTASK_H
#define SNTV_WINTASK_H
#include <SDL.h>

/* Windows: the taskbar button of a playing film shows its progress (green, yellow when paused) and
 * its thumbnail, hovered, has back / play-pause / forward buttons. Nowhere else this does anything. */
enum { WINTASK_NONE, WINTASK_PLAYPAUSE, WINTASK_BACK, WINTASK_FORWARD };

#ifdef _WIN32
void wintask_init(SDL_Window *win);
void wintask_update(int playing, int paused, double position, double duration);   /* playing 0: no progress shown */
int  wintask_message(const SDL_SysWMmsg *msg);   /* a thumbnail button pressed: WINTASK_PLAYPAUSE and so on */
void wintask_shutdown(void);
#else
static inline void wintask_init(SDL_Window *win) { (void)win; }
static inline void wintask_update(int playing, int paused, double position, double duration) { (void)playing; (void)paused; (void)position; (void)duration; }
static inline int  wintask_message(const SDL_SysWMmsg *msg) { (void)msg; return WINTASK_NONE; }
static inline void wintask_shutdown(void) {}
#endif

#endif
