#define SDL_MAIN_HANDLED
#include "ui/gfx.h"
#include "platform/platform.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

void log_msg(const char *level, const char *fmt, ...) { (void)level; (void)fmt; }

int main(void)
{
    unsetenv("SNTV_PHONE_UNITS");
    setenv("SNTV_DENSITY", "2", 1);
    gfx_set_handheld(1);
    gfx_update_layout(720, 1600);
    assert(SNTV_PHONE && gfx_portrait() && SNTV_UI_W == 600);
    gfx_update_layout(900, 1600);
    assert(SNTV_UI_W > 600 && SNTV_UI_W < 780);
    gfx_update_layout(1600, 720);
    assert(!SNTV_PHONE && !gfx_portrait());
    gfx_set_handheld(2);
    gfx_update_layout(1600, 2400);
    int portrait_width = SNTV_UI_W;
    assert(SNTV_PHONE && gfx_tablet() && gfx_grid_columns() > 3);
    gfx_update_layout(2400, 1600);
    assert(SNTV_PHONE && !gfx_portrait() && SNTV_UI_W > portrait_width);
    gfx_update_layout(1000, 1600);
    assert(SNTV_UI_W < portrait_width && gfx_grid_columns() == 3);
    setenv("SNTV_FONT_SCALE", "1.5", 1);
    assert(gfx_font_scale() == 1.5f);
    gfx_set_handheld(0);
    gfx_update_layout(1920, 1080);
    assert(!SNTV_PHONE && !gfx_tablet() && SNTV_UI_W == 1920 && SNTV_UI_H == 1080 && gfx_font_scale() == 1);
    puts("mobile layout: passed");
    return 0;
}
