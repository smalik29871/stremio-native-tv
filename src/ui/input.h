/* Maps SDL events from keyboards and TV remotes to UI actions. */
#ifndef SNTV_INPUT_H
#define SNTV_INPUT_H
#include <SDL.h>

typedef enum {
    KEY_NONE = 0, KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT, KEY_OK, KEY_BACK,
    KEY_PLAY_PAUSE, KEY_PLAY, KEY_PAUSE, KEY_STOP, KEY_REWIND, KEY_FORWARD,
    KEY_MENU, KEY_INFO, KEY_RED, KEY_GREEN, KEY_YELLOW, KEY_BLUE, KEY_SEARCH,
    KEY_BACKSPACE, KEY_EXIT,
} KeyAction;

KeyAction input_map(const SDL_Event *e);

#endif
