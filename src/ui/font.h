#ifndef SNTV_FONT_H
#define SNTV_FONT_H
#include "gfx.h"

/* Sizes; OR with FONT_B for the bold face (Inter Bold), e.g. FONT_L | FONT_B. */
enum { FONT_XS = 20, FONT_S = 24, FONT_M = 30, FONT_L = 40, FONT_XL = 56, FONT_TITLE = 72, FONT_HUGE = 96 };
#define FONT_B 0x1000

int   font_init(const char *regular_ttf, const char *bold_ttf);
void  font_shutdown(void);
float font_height(int size);
float font_ascent(int size);
float font_measure(const char *utf8, int size);
void  font_draw(const char *utf8, float x, float y, int size, Color c);
/* Draws at most max_w wide, replacing the overflow with an ellipsis. */
void  font_draw_fit(const char *utf8, float x, float y, int size, Color c, float max_w);
/* Word-wraps; returns the number of lines drawn (or that would be drawn if c == 0). */
int   font_draw_wrapped(const char *utf8, float x, float y, int size, Color c, float max_w, int max_lines);
/* Text with a soft drop shadow, for captions over video and hero titles. */
void  font_draw_shadow(const char *utf8, float x, float y, int size, Color c, Color shadow);
void  font_draw_center(const char *utf8, float cx, float y, int size, Color c);

#endif
