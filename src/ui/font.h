#ifndef SNTV_FONT_H
#define SNTV_FONT_H
#include "gfx.h"

/* Sizes; OR with FONT_B for the bold face, e.g. FONT_L | FONT_B, or with FONT_ALT for the
 * theme's alternate face (mono metadata in Signal, condensed display titles in Marquee);
 * without one FONT_ALT falls back to the regular/bold face. */
enum { FONT_XS = 20, FONT_S = 24, FONT_M = 30, FONT_L = 40, FONT_XL = 56, FONT_TITLE = 72, FONT_HUGE = 96, FONT_HERO = 132 };
#define FONT_B 0x1000
#define FONT_ALT 0x2000
enum { FONT_ALT_NONE, FONT_ALT_MONO, FONT_ALT_DISPLAY, FONT_ALT_SERIF };   /* SERIF: Aperture's titles, up to FONT_HERO (ASCII only at that size) */

/* Baked atlases are cached under dir/fontcache so later starts skip the bake. */
void  font_set_cache_dir(const char *dir);
/* (Re)loads the faces; alt may be NULL. Safe to call again to switch families. */
int   font_init(const char *regular_ttf, const char *bold_ttf, const char *alt_ttf, int alt_kind);
void  font_shutdown(void);
float font_height(int size);
float font_ascent(int size);
float font_measure(const char *utf8, int size);
void  font_draw(const char *utf8, float x, float y, int size, Color c);
/* With extra letter spacing (canvas units after every glyph), for capital titles. */
float font_measure_tracked(const char *utf8, int size, float track);
void  font_draw_tracked(const char *utf8, float x, float y, int size, Color c, float track);
/* Draws at most max_w wide, replacing the overflow with an ellipsis. */
void  font_draw_fit(const char *utf8, float x, float y, int size, Color c, float max_w);
/* Word-wraps; returns the number of lines drawn (or that would be drawn if c == 0). */
int   font_draw_wrapped(const char *utf8, float x, float y, int size, Color c, float max_w, int max_lines);
/* Text with a soft drop shadow, for captions over video and hero titles. */
void  font_draw_shadow(const char *utf8, float x, float y, int size, Color c, Color shadow);
void  font_draw_center(const char *utf8, float cx, float y, int size, Color c);

/* Big clock digits (0-9 and ':', sharp up to 400 px; px is the line height, the digits about 0.6 of it): load while a clock face shows,
 * free after. Without the atlas they fall back to the scaled bold face. y is the top
 * of the digits; font_digits_height is their cap height. */
int   font_digits_load(void);
void  font_digits_free(void);
float font_digits_measure(const char *digits, float px);
void  font_digits_draw(const char *digits, float x, float y, float px, Color c);
float font_digits_height(float px);

#endif
