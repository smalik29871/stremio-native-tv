#define SDL_MAIN_HANDLED
#include "ui/input.h"
#include <stdio.h>

int g_ui_portrait, g_ui_mobile;
int gfx_handheld(void) { return 0; }

static int failures;
#define CHECK(c) do { if (!(c)) { failures++; fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #c); } } while (0)

int main(void)
{
    SDL_Event e = { 0 };
    e.type = SDL_KEYDOWN; e.key.keysym.sym = SDLK_f;
    CHECK(input_player_shortcut(&e) == KEY_FULLSCREEN);
    e.key.keysym.mod = KMOD_CTRL;
    CHECK(input_player_shortcut(&e) == KEY_NONE);
    CHECK(input_map(&e) == KEY_SEARCH);
    e.key.keysym.mod = KMOD_ALT;
    CHECK(input_player_shortcut(&e) == KEY_NONE);
    e.key.keysym.mod = KMOD_GUI;
    CHECK(input_player_shortcut(&e) == KEY_NONE);
    e.key.keysym.mod = 0; e.key.keysym.sym = SDLK_m;
    CHECK(input_player_shortcut(&e) == KEY_MUTE);
    e.key.repeat = 1;
    CHECK(input_player_shortcut(&e) == KEY_NONE);
    e.key.repeat = 0;
    input_text(1);
    CHECK(input_player_shortcut(&e) == KEY_NONE);
    e.key.keysym.sym = SDLK_v; e.key.keysym.mod = KMOD_CTRL;
    CHECK(input_map(&e) == KEY_PASTE);
    input_text(0);
    CHECK(input_map(&e) == KEY_NONE);

    SDL_MouseWheelEvent wheel = { 0 };
    float dx, dy;
    wheel.preciseX = 0.25f; wheel.preciseY = -0.125f;
    input_wheel_delta(&wheel, 0, &dx, &dy);
    CHECK(dx == -0.25f && dy == -0.125f);
    wheel.direction = SDL_MOUSEWHEEL_FLIPPED;
    input_wheel_delta(&wheel, 0, &dx, &dy);
    CHECK(dx == 0.25f && dy == 0.125f);
    input_wheel_delta(&wheel, KMOD_SHIFT, &dx, &dy);
    CHECK(dx == 0.125f && dy == 0);
    wheel.preciseX = wheel.preciseY = 0;
    wheel.x = 1; wheel.y = -2; wheel.direction = SDL_MOUSEWHEEL_NORMAL;
    input_wheel_delta(&wheel, 0, &dx, &dy);
    CHECK(dx == -1 && dy == -2);
    float remainder = 0;
    for (int i = 0; i < 7; i++) CHECK(input_wheel_steps(0.125f, &remainder) == 0);
    CHECK(input_wheel_steps(0.125f, &remainder) == 1);
    CHECK(remainder == 0);
    CHECK(input_wheel_steps(0.25f, &remainder) == 0);
    CHECK(input_wheel_steps(-0.75f, &remainder) == 0);
    CHECK(input_wheel_steps(-0.25f, &remainder) == -1);
    CHECK(input_wheel_steps(20, &remainder) == 4 && remainder == 0);
    if (failures) return 1;
    puts("desktop input checks passed");
    return 0;
}
