#include "input.h"
#include "../platform/platform.h"

/* webOS remote scancodes from the webosbrew SDL fork (include/SDL_scancode.h,
 * branch webOS-2.30.x). They only arrive once the SDL_WEBOS_ACCESS_POLICY_KEYS_*
 * hints are set before the window exists. */
#define WEBOS_SC_CH_UP   480
#define WEBOS_SC_CH_DOWN 481
#define WEBOS_SC_BACK    482
#define WEBOS_SC_RED     486
#define WEBOS_SC_GREEN   487
#define WEBOS_SC_YELLOW  488
#define WEBOS_SC_BLUE    489
#define WEBOS_SC_GUIDE   495
#define WEBOS_SC_INFO    499   /* the standard LG remote's INFO (seen on a webOS TV with an AKB-style remote) */
#define WEBOS_SC_EXIT    505
#define WEBOS_SC_PLAY    450   /* LG's Play arrives as this, with no key symbol (seen on webOS 10.3) */

KeyAction input_map(const SDL_Event *e)
{
    if (e->type != SDL_KEYDOWN) return KEY_NONE;
    SDL_Keycode k = e->key.keysym.sym;
    SDL_Scancode sc = e->key.keysym.scancode;

    switch ((int)sc) {
    case WEBOS_SC_BACK:   return KEY_BACK;
    case WEBOS_SC_EXIT:   return KEY_EXIT;
    case WEBOS_SC_RED:    return KEY_RED;
    case WEBOS_SC_GREEN:  return KEY_GREEN;
    case WEBOS_SC_YELLOW: return KEY_YELLOW;
    case WEBOS_SC_BLUE:   return KEY_BLUE;
    case WEBOS_SC_CH_UP:  return KEY_UP;
    case WEBOS_SC_CH_DOWN:return KEY_DOWN;
    case WEBOS_SC_GUIDE:  return KEY_INFO;
    case WEBOS_SC_INFO:   return KEY_INFO;
    case WEBOS_SC_PLAY:   return KEY_PLAY;
    /* the media keys by position too: the TV's keymap can leave their symbol empty */
    case SDL_SCANCODE_AUDIOPLAY: return KEY_PLAY;
    case SDL_SCANCODE_PAUSE: return KEY_PAUSE;
    case SDL_SCANCODE_AUDIOSTOP: case SDL_SCANCODE_STOP: return KEY_STOP;   /* the standard LG remote's Stop is the plain one (120) */
    case SDL_SCANCODE_AUDIOPREV: case SDL_SCANCODE_AUDIOREWIND: return KEY_REWIND;
    case SDL_SCANCODE_AUDIONEXT: case SDL_SCANCODE_AUDIOFASTFORWARD: return KEY_FORWARD;
    default: break;
    }

    switch (k) {
    case SDLK_UP:    return KEY_UP;
    case SDLK_DOWN:  return KEY_DOWN;
    case SDLK_LEFT:  return KEY_LEFT;
    case SDLK_RIGHT: return KEY_RIGHT;
    case SDLK_RETURN: case SDLK_KP_ENTER: case SDLK_SELECT: case SDLK_SPACE: return KEY_OK;
    case SDLK_ESCAPE: case SDLK_AC_BACK: return KEY_BACK;    /* Tizen "Return" arrives as AC_BACK */
    case SDLK_BACKSPACE: return KEY_BACKSPACE;
    case SDLK_AUDIOPLAY: return KEY_PLAY;
    case SDLK_PAUSE: return KEY_PAUSE;
    case SDLK_AUDIOSTOP: case SDLK_STOP: return KEY_STOP;
    case SDLK_AUDIOPREV: case SDLK_AUDIOREWIND: return KEY_REWIND;
    case SDLK_AUDIONEXT: case SDLK_AUDIOFASTFORWARD: return KEY_FORWARD;
    case SDLK_MENU: case SDLK_APPLICATION: return KEY_MENU;
    case SDLK_HELP: return KEY_INFO;
    case SDLK_AC_SEARCH: case SDLK_F3: return KEY_SEARCH;
    case SDLK_F1: return KEY_RED;
    case SDLK_F2: return KEY_GREEN;
    case SDLK_F4: return KEY_BLUE;
    case SDLK_F5: return KEY_YELLOW;
    case SDLK_p:  return KEY_PLAY_PAUSE;  /* desktop convenience */
    default: return KEY_NONE;
    }
}

void input_text_system(int on)
{
    SDL_StopTextInput();   /* off first: on again brings it back if it was put away */
    if (on) SDL_StartTextInput();
}

void input_text(int on)
{
#if defined(SNTV_PLATFORM_WEBOS) || defined(SNTV_PLATFORM_TIZEN)
    (void)on;
#else
    if (on) SDL_StartTextInput(); else SDL_StopTextInput();
#endif
}
