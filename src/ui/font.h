#ifndef SNTV_FONT_H
#define SNTV_FONT_H
#include "gfx.h"

enum { FONT_S = 24, FONT_M = 30, FONT_L = 40, FONT_XL = 56, FONT_TITLE = 72 };

int   font_init(const char *ttf_path);
void  font_shutdown(void);
float font_height(int size);
float font_measure(const char *utf8, int size);
void  font_draw(const char *utf8, float x, float y, int size, Color c);
/* Draws at most max_w wide, replacing the overflow with an ellipsis. */
void  font_draw_fit(const char *utf8, float x, float y, int size, Color c, float max_w);
/* Word-wraps; returns the number of lines drawn (or that would be drawn if c == 0). */
int   font_draw_wrapped(const char *utf8, float x, float y, int size, Color c, float max_w, int max_lines);

#endif
