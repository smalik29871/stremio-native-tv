#define SDL_MAIN_HANDLED
#include "ui/input.h"
#include "ui/gfx.h"
#include <SDL.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>

static int system_keyboard = 1, keyboard_shown, text_active;
int g_ui_mobile, g_ui_portrait, g_ui_w = 600, g_ui_h = 1200;
int gfx_handheld(void) { return 1; }
SDL_Keymod SDL_GetModState(void) { return KMOD_NONE; }
SDL_bool SDL_SetHint(const char *name, const char *value)
{
    assert(!strcmp(name, SDL_HINT_ENABLE_SCREEN_KEYBOARD));
    system_keyboard = !strcmp(value, "1");
    return SDL_TRUE;
}
void SDL_StartTextInput(void) { text_active = 1; if (system_keyboard) keyboard_shown = 1; }
void SDL_StopTextInput(void) { text_active = 0; if (system_keyboard) keyboard_shown = 0; }

int main(void)
{
    input_text_system(1);
    assert(keyboard_shown && text_active);
    input_text(0);
    assert(!keyboard_shown && !text_active);
    input_text_system(1);
    input_text(1);
    assert(!keyboard_shown && text_active);
    input_text_system(1);
    assert(keyboard_shown && text_active);
    input_text_system(0);
    assert(!keyboard_shown && !text_active);
    puts("mobile keyboard handoff: passed");
    return 0;
}
