/* Shared visual building blocks so every screen speaks the same design language. */
#ifndef SNTV_WIDGETS_H
#define SNTV_WIDGETS_H
#include "gfx.h"
#include "theme.h"

typedef enum {
    ICON_PLAY, ICON_PAUSE, ICON_SEARCH, ICON_SETTINGS, ICON_USER, ICON_PLUS, ICON_CHECK,
    ICON_AUDIO, ICON_SUBS, ICON_NEXT, ICON_BACK, ICON_CHEVRON_LEFT, ICON_CHEVRON_RIGHT,
    ICON_HOME, ICON_LIBRARY, ICON_STAR, ICON_SWAP, ICON_LINK,
    ICON_PREV, ICON_RELOAD, ICON_ASPECT, ICON_LIST, ICON_CLOCK, ICON_SPEED, ICON_INFO, ICON_COMPASS, ICON_CALENDAR
} Icon;

void ui_icon(Icon icon, float cx, float cy, float size, Color c);

/* Full-screen background: base colour plus slow ambient glows tinted by `tint`
 * (pass 0 for the default accent). The tint eases between calls. */
void ui_background(Color tint);
/* Cover-fit backdrop anchored right, with cinematic left + bottom fades. alpha 0..1.
 * ui_backdrop = image + fades; the split versions allow cross-fading two images. */
void ui_backdrop(unsigned tex, int iw, int ih, float alpha, float left_fade_to, float bottom_fade_from);
void ui_backdrop_image(unsigned tex, int iw, int ih, float alpha, float height);
void ui_backdrop_fades(float left_fade_to, float bottom_fade_from, float height);
/* The same, blurred and dimmed (needs a mipmapped texture). */
/* Dark wash behind a left-aligned text column so copy stays readable on bright art. */
void ui_text_scrim(float width, float height);
void ui_backdrop_blur(unsigned tex, int iw, int ih, float alpha);

typedef enum { PILL_PRIMARY, PILL_SECONDARY, PILL_GHOST } PillStyle;
/* Returns the pill width. Focused pills turn white with dark text (TV convention). */
float ui_pill(const char *label, int has_icon, Icon icon, float x, float y, float h, PillStyle style, int focused);
float ui_pill_width(const char *label, int has_icon, float h);
float ui_icon_button(Icon icon, float cx, float cy, float radius, int focused);

/* Small rounded badges. Returns width. */
float ui_chip(const char *text, float x, float y, Color bg, Color fg);
float ui_chip_outline(const char *text, float x, float y, Color line, Color fg);
float ui_imdb(const char *rating, float x, float y);
/* Wall-clock time `from_now` seconds ahead in the viewer's 12/24-hour format (settings). */
void  ui_clock_text(double from_now, char *out, size_t n);
/* The clock pill in the top-right corner of every screen (nothing when the clock is off).
 * Returns the width it took, so screens can keep their own corner buttons clear of it. */
float ui_clock_pill(void);
float ui_clock_width(void);
/* "a  •  b  •  c" meta line; returns the x after it. */
float ui_dot_sep(float x, float y, Color c);

void ui_toggle(float x, float y, int on, int focused);

/* Subtitle text in the chosen size (0 small, 1 medium, 2 large) and style, centred,
 * with its last line ending at `bottom`. Used by the player and the Settings preview. */
enum { SUBS_STYLE_SHADOW, SUBS_STYLE_OUTLINE, SUBS_STYLE_YELLOW, SUBS_STYLE_BOXED, SUBS_STYLE_COUNT };
extern const char *const SUBS_STYLE_NAMES[SUBS_STYLE_COUNT];
void ui_subtitle(const char *text, float bottom, int size_setting, int style);
void ui_progress(float x, float y, float w, float h, float frac, Color fill);
void ui_skeleton(float x, float y, float w, float h, float r);
void ui_spinner(float cx, float cy, float radius);
void ui_brand(float x, float y, float size);

/* Poster / landscape cards. focus 0..1 animates scale, ring and shadow; dim 0..1 darkens. */
#define CARD_MIP  1   /* use the mipmapped texture variant */
#define CARD_BLUR 2   /* sample a blurred mip (spoiler protection), implies CARD_MIP */
void ui_card(const char *url, float x, float y, float w, float h, float focus, float dim,
             const char *fallback_title, int card_flags);
float ui_focus_scale(float focus);

#endif
