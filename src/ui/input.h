/* Maps SDL events from keyboards and TV remotes to UI actions. */
#ifndef SNTV_INPUT_H
#define SNTV_INPUT_H
#include <SDL.h>

typedef enum {
    KEY_NONE = 0, KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT, KEY_OK, KEY_BACK,
    KEY_PLAY_PAUSE, KEY_PLAY, KEY_PAUSE, KEY_STOP, KEY_REWIND, KEY_FORWARD,
    KEY_MENU, KEY_INFO, KEY_RED, KEY_GREEN, KEY_YELLOW, KEY_BLUE, KEY_SEARCH,
    KEY_BACKSPACE, KEY_EXIT,
    KEY_LONG_OK,   /* OK held for half a second (screens with Screen.long_ok only) */
} KeyAction;

KeyAction input_map(const SDL_Event *e);
/* Text input from a real keyboard (desktop). On TVs it would open the system keyboard,
 * which takes the remote's focus; the app's own keyboard and the phone page are used there. */
void input_text(int on);
/* The system's own keyboard (LG's on webOS) for typing now: on shows it (again), off hides it.
 * Only while a field is being typed in, so the remote's OK stays the app's otherwise. */
void input_text_system(int on);

#endif
