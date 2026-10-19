/* Shared visual building blocks so every screen speaks the same design language. */
#ifndef SNTV_WIDGETS_H
#define SNTV_WIDGETS_H
#include "gfx.h"
#include "theme.h"
#include <stdint.h>

typedef enum {
    ICON_PLAY, ICON_PAUSE, ICON_SEARCH, ICON_SETTINGS, ICON_USER, ICON_PLUS, ICON_CHECK,
    ICON_AUDIO, ICON_SUBS, ICON_NEXT, ICON_BACK, ICON_CHEVRON_LEFT, ICON_CHEVRON_RIGHT,
    ICON_HOME, ICON_LIBRARY, ICON_STAR, ICON_SWAP, ICON_LINK,
    ICON_PREV, ICON_RELOAD, ICON_ASPECT, ICON_LIST, ICON_CLOCK, ICON_SPEED, ICON_INFO, ICON_COMPASS, ICON_CALENDAR, ICON_LOCK, ICON_UNLOCK, ICON_CLOSE,
    ICON_PALETTE, ICON_EYE, ICON_MOON, ICON_SPARK, ICON_GLOBE, ICON_TV, ICON_KEY, ICON_BELL
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
/* Width of the island when it shows the clock (0 when the clock is off), so screens can
 * keep their own corner buttons clear of it. */
float ui_clock_width(void);
/* "a  •  b  •  c" meta line; returns the x after it. */
float ui_dot_sep(float x, float y, Color c);

void ui_toggle(float x, float y, int on, int focused);

/* Subtitle text in the chosen size (0 small, 1 medium, 2 large) and style, centred,
 * with its last line ending at `bottom`. Used by the player and the Settings preview. */
enum { SUBS_STYLE_SHADOW, SUBS_STYLE_OUTLINE, SUBS_STYLE_YELLOW, SUBS_STYLE_BOXED, SUBS_STYLE_COUNT };
extern const char *const SUBS_STYLE_NAMES[SUBS_STYLE_COUNT];
void ui_subtitle(const char *text, float bottom, int size_setting, int style);
int  ui_subtitle_font(int size_setting);   /* 0 small .. 3 extra large */
void ui_progress(float x, float y, float w, float h, float frac, Color fill);
void ui_skeleton(float x, float y, float w, float h, float r);
void ui_spinner(float cx, float cy, float radius);
/* Clockwise arc of `frac` of a circle, starting `from` turns past 12 o'clock (capsule segments). */
void ui_arc(float cx, float cy, float r, float thickness, float from, float frac, Color c);
void ui_brand(float x, float y, float size);

/* Poster / landscape cards. focus 0..1 animates scale, ring and shadow; dim 0..1 darkens. */
#define CARD_MIP  1   /* use the mipmapped texture variant */
#define CARD_BLUR 2   /* sample a blurred mip (spoiler protection), implies CARD_MIP */
#define CARD_TRUE_COLOUR 4   /* never printed in two tones (episode stills, people) */
void ui_card(const char *url, float x, float y, float w, float h, float focus, float dim,
             const char *fallback_title, int card_flags);
/* Off while drawing cards that are only pictures (Settings previews): screens open out of
 * the last focused card, which must be one the viewer can press. */
void ui_card_capture(int on);
/* Details on a focused card: after the card has been focused for PEEK_DWELL seconds a
 * panel slides open beside it. Step from update() (it asks for the full details then),
 * draw from draw() (reads caches only). t is how far the panel is open, 0..1. */
#define PEEK_DWELL 0.6f
typedef struct Peek { char id[128]; float since, t; int asked; } Peek;
struct MetaPreview;
void  ui_peek_step(Peek *p, const struct MetaPreview *m, float dt);
/* The card's rectangle as drawn (focus scale included); the panel opens right of it,
 * or left when it would cross max_x. */
void  ui_peek_draw(const Peek *p, const struct MetaPreview *m, float cx, float cy, float cw, float ch, float min_x, float max_x);
/* While OK is held on a long-press screen: "Hold for options" with a bar that fills,
 * then "Release for options". Drawn in place of the line under the focused card;
 * returns 1 when it drew (the caller skips that line). */
int   ui_hold_hint(float x, float y, float w);
float ui_focus_scale(float focus);
float ui_focus_lift(float focus);   /* upward shift of a focused card, canvas units */

/* The theme's language for focus, titles, metadata and row headings. */
void  ui_focus_under(float x, float y, float w, float h, float r, float amount);
void  ui_upper(const char *in, char *out, size_t n);
void  ui_lower(const char *in, char *out, size_t n);
/* A panel in `fill`; Glass adds a sheen and a light rim (frosted glass). */
void  ui_panel(float x, float y, float w, float h, float r, Color fill);
/* The artwork Glass blurs behind every screen (the last hero or title backdrop). */
void  ui_set_ambient_art(const char *url);
/* A title in the theme's case, face and letter spacing; max_w > 0 cuts with an ellipsis.
 * Returns the width drawn. */
float ui_title(const char *text, float x, float y, int size, Color c, float max_w);
float ui_title_width(const char *text, int size);
int   ui_hero_title(const char *text, float x, float bottom, float maxw, Color c);
float ui_meta_line(const char *const *parts, int n, float x, float y, Color c);
void  ui_row_header(int index, const char *title, const char *source, int count, float x, float y, float right, float k);
void  ui_mark(float x, float y, float size);   /* the Stremio logo tile, unchanged by themes */

/* A QR code made by qrcodegen, dark modules on a white rounded square. */
void ui_qr(const uint8_t *qr, float x, float y, float size);

#endif
