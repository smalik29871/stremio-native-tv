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
    ICON_PALETTE, ICON_EYE, ICON_MOON, ICON_SPARK, ICON_GLOBE, ICON_TV, ICON_KEY, ICON_BELL, ICON_CHEVRON_DOWN, ICON_PIP
} Icon;

void ui_icon(Icon icon, float cx, float cy, float size, Color c);

/* Full-screen background: base colour plus slow ambient glows tinted by `tint`
 * (pass 0 for the default accent). The tint eases between calls. */
void ui_background(Color tint);
/* One cropped artwork quad, using the existing bounded image cache. */
void ui_aperture_art(const char *url, float x, float y, float w, float h, float alpha);
/* From update(): the artwork windows pan slowly; this asks for the few frames that needs. */
void ui_aperture_drift_tick(void);
/* Aperture's editorial pieces. A title in the serif's capitals, tightly leaded, up to
 * `lines` lines, the last ending at `bottom`; `size` is the largest it may take (smaller
 * when it would not fit). Returns the top of the first line. */
float ui_serif_title(const char *text, float x, float bottom, float maxw, int size, int lines, Color c);
/* Capitals spaced out, in the serif ("AFTER HOURS") or the sans (kickers). Returns the width. */
float ui_spaced_caps(const char *text, float x, float y, int size, float track, Color c, float max_w);
/* "2026 / 1h 48m / Sci-fi" */
float ui_slash_line(const char *const *parts, int n, float x, float y, int size, Color c);
/* Ivory button with dark type (the primary action), or the text-only one beside it. Returns the width. */
float ui_aperture_button(const char *label, int icon, float x, float y, int primary, int focused);
/* Cover-fit backdrop anchored right, with cinematic left + bottom fades. alpha 0..1.
 * ui_backdrop = image + fades; the split versions allow cross-fading two images. */
void ui_backdrop(unsigned tex, int iw, int ih, float alpha, float left_fade_to, float bottom_fade_from);
void ui_backdrop_image(unsigned tex, int iw, int ih, float alpha, float height);
/* The next backdrop drawn (ui_backdrop_image, ui_aperture_art) pushes into the picture by
 * this depth map, k = how far (0 off): see gfx_depth. */
void ui_backdrop_depth(unsigned depth_tex, float k);
void ui_backdrop_fades(float left_fade_to, float bottom_fade_from, float height);
void ui_backdrop_video(float t, float height);
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
/* The content advisory a video opens with: a line per theme the title contains
 * ("Violence · Moderate"), strongest first, right-aligned against a white bar at x. level: nudity, violence, language,
 * drugs, frightening; 0 none to 3 severe. alpha 0..1 fades it. */
void ui_advisory(float x, float y, const int level[5], float alpha);
/* The same look for any list: "Name · value" lines beside a white bar; right = 1 puts
 * the bar at x with the lines right-aligned against it. */
void ui_facts(float x, float y, const char *const *name, const char *const *value, int n, float alpha, int right);
/* Score badges: each its own mark (a tomato, a popcorn bucket, ...), the score large
 * and what it is under it; a row right-aligned to right_x. Returns the row's height. */
enum { BADGE_TOMATOMETER, BADGE_POPCORNMETER, BADGE_METASCORE, BADGE_LETTERBOXD, BADGE_LIVE, BADGE_WATCHERS, BADGE_PLAYS,
       BADGE_EBERT, BADGE_TRAKT, BADGE_TMDB, BADGE_MDBLIST };
typedef struct { int kind; double score; const char *value, *label; } ScoreBadge;
float ui_score_badges(float right_x, float y, const ScoreBadge *b, int n, float alpha, float col_w);
/* A badge's own width, to line several rows up in columns (pass the widest as col_w). */
float ui_score_badge_width(const ScoreBadge *b);
/* The ratings card (the paused screen's right side): MDBList's ring on top when there is
 * one, the scores in two columns (mark, name, value on the right), then Trakt's people
 * under "On Trakt"; right edge at right_x, centred on center_y. Returns its height. */
float ui_score_card(float right_x, float center_y, const ScoreBadge *scores, int n, const ScoreBadge *people, int np, float alpha);
#define SCORE_CARD_W 660.0f   /* its width, for what sits beside it */
/* The title page's strip: a pill per score, its mark and value; returns the width. */
float ui_score_pills(float x, float y, const ScoreBadge *b, int n, float max_w, float alpha);
/* MDBList's scores for a title as badges, critics first, then audiences and MDBList's
 * blend (text holds their values); 0 while unknown or without an MDBList key. */
int ui_mdb_badges(const char *id, const char *type, ScoreBadge b[8], char text[8][16]);
/* The same marks small, each with its score beside it, in a 36-high line from x; returns the width. */
float ui_score_inline(float x, float y, const ScoreBadge *b, int n);
/* The advisory on one line from x ("Violence Severe · Profanity Moderate"); returns the width (0: nothing rated). */
float ui_advisory_inline(float x, float y, const int level[5], float max_w);
/* A person's photo cropped to the box (faces sit high), or their initials when there is none. */
void ui_portrait(const char *photo, const char *name, float x, float y, float w, float h, float r, int initials_font);
/* "New episode" ("3 new episodes") with a bell, its right edge at x: a series card whose show has aired more. */
void ui_new_episodes_badge(int count, float right_x, float y);
/* The same on a poster: a bell and the count in a small pill. */
void ui_new_episodes_dot(int count, float right_x, float y);
float ui_imdb(const char *rating, float x, float y);
/* Wall-clock time `from_now` seconds ahead in the viewer's 12/24-hour format (settings). */
void  ui_clock_text(double from_now, char *out, size_t n);
/* Width of the island when it shows the clock (0 when the clock is off), so screens can
 * keep their own corner buttons clear of it. */
float ui_clock_width(void);
void  ui_island_text(char *out, size_t n);   /* the clock island in its style (g_app.island_style) */
void  ui_mini_dial(float cx, float cy, float r, Color c);
/* "a  •  b  •  c" meta line; returns the x after it. */
float ui_dot_sep(float x, float y, Color c);

void ui_toggle(float x, float y, int on, int focused);

/* Subtitle text in the chosen size (0 small, 1 medium, 2 large) and style, centred,
 * with its last line ending at `bottom`. Used by the player and the Settings preview. */
enum { SUBS_STYLE_SHADOW, SUBS_STYLE_OUTLINE, SUBS_STYLE_YELLOW, SUBS_STYLE_BOXED, SUBS_STYLE_BRIGHT, SUBS_STYLE_RAISED,
       SUBS_STYLE_SOFT_BOX, SUBS_STYLE_CYAN, SUBS_STYLE_COUNT };   /* stored by number: add at the end */
extern const char *const SUBS_STYLE_NAMES[SUBS_STYLE_COUNT];
/* The caption colours TVs offer for accessibility (CEA-708), over any style */
enum { SUBS_COLOUR_STYLE, SUBS_COLOUR_WHITE, SUBS_COLOUR_YELLOW, SUBS_COLOUR_CYAN, SUBS_COLOUR_GREEN, SUBS_COLOUR_MAGENTA,
       SUBS_COLOUR_RED, SUBS_COLOUR_BLUE, SUBS_COLOUR_BLACK, SUBS_COLOUR_COUNT };
extern const char *const SUBS_COLOUR_NAMES[SUBS_COLOUR_COUNT];
/* the name the TV's caption setter takes ("yellow"), NULL for the style's own */
const char *ui_subs_colour_tv(int colour);
void ui_subtitle(const char *text, float bottom, int size_setting, int style);
int  ui_subtitle_font(int size_setting);   /* 0 small .. 3 extra large */
float ui_subtitle_line_h(int size_setting);   /* a line's height as drawn, at that size */
void ui_subtitle_ex(const char *text, float bottom, int size_setting, int style, float k, float width);   /* k times the size, lines up to width wide: a preview */
void ui_progress(float x, float y, float w, float h, float frac, Color fill);
void ui_skeleton(float x, float y, float w, float h, float r);
void ui_spinner(float cx, float cy, float radius);
/* Clockwise arc of `frac` of a circle, starting `from` turns past 12 o'clock (capsule segments). */
void ui_arc(float cx, float cy, float r, float thickness, float from, float frac, Color c);
void ui_brand(float x, float y, float size);
float ui_center_wrapped(const char *text, float cx, float y, int size, Color c, float max_w);

/* Poster / landscape cards. focus 0..1 animates scale, ring and shadow; dim 0..1 darkens. */
#define CARD_MIP  1   /* use the mipmapped texture variant */
#define CARD_BLUR 2   /* sample a blurred mip (spoiler protection), implies CARD_MIP */
#define CARD_TRUE_COLOUR 4   /* never printed in two tones (episode stills, people) */
/* Lazy rows: whether a card's picture is in (or failed, or there is none); asks for it if
 * not. A row shows its cards once the ones in view are, all together, not one by one. */
int  ui_art_ready(const char *url);
#define UI_ROW_WAIT 2.5f   /* ...or after this long, whatever has come */
/* A band of light sweeping across a tile still loading (asks for the frames it needs). */
void ui_shimmer(float x, float y, float w, float h);
void ui_card(const char *url, float x, float y, float w, float h, float focus, float dim,
             const char *fallback_title, int card_flags);
/* Off while drawing cards that are only pictures (Settings previews): screens open out of
 * the last focused card, which must be one the viewer can press. */
void ui_card_capture(int on);
/* Details on a focused card: after the card has been focused for PEEK_DWELL seconds the
 * card grows a panel beside it and its neighbours make room (ui_peek_room). Step from update() (it asks for the full details then),
 * draw from draw() (reads caches only). t is how far the panel is open, 0..1. */
#define PEEK_DWELL 1.5f   /* long enough that passing over a card never opens it */
typedef struct Peek { char id[128]; float since, t; int asked; } Peek;
struct MetaPreview;
void  ui_peek_step(Peek *p, const struct MetaPreview *m, float dt);
/* The card's rectangle as drawn (focus scale included); the panel opens right of it,
 * or left when it would cross max_x. */
void  ui_peek_draw(const Peek *p, const struct MetaPreview *m, float cx, float cy, float cw, float ch, float min_x, float max_x);
/* How far the open panel pushes the cards beside it, and to which side (*right: the
 * cards after it move right; else the cards before it move left). Same arguments. */
float ui_peek_room(const Peek *p, const struct MetaPreview *m, float cx, float cw, float ch, float min_x, float max_x, int *right);
/* While OK is held on a long-press screen: "Hold for options" with a bar that fills,
 * then "Release for options". Drawn in place of the line under the focused card;
 * returns 1 when it drew (the caller skips that line). */
int   ui_hold_hint(float x, float y, float w);
int   ui_hold_hint_ex(float x, float y, float w, const char *hold, const char *release);   /* the same, its own words */
float ui_focus_scale(float focus);
/* motion: a damped spring kicked at t0 (an offset, amp toward the press), and item i of a
 * group that appeared at t0 easing in (0..1, each a beat after the one before) */
float ui_spring(float t0, float amp);
float ui_enter(float t0, int i);
void  ui_list_enter(int n, int *seen_n, float *at);
void  ui_bump(int dx, int dy);   /* pressed past the end of a list: the focused card springs that way */   /* when a list of n arrived (for ui_enter) */
void  ui_cover_uv(int iw, int ih, float w, float h, float *u0, float *v0, float *u1, float *v1);
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
void  ui_mark(float x, float y, float size);   /* Cuelight's mark on its tile, unchanged by themes */
/* Touch: what can be tapped on the screen being drawn, recorded while it draws (the screen's
 * own ids; HIT_RAIL + item for the side menu). ui_hit_find gives the last one drawn at a
 * canvas point, of the screen on top. */
struct Screen;
enum { HIT_RAIL = 0x7F000000, HIT_TRAILER_SOUND = 0x7E000000, HIT_PIP = 0x7D000000, HIT_PIP_FULL, HIT_PIP_CLOSE, HIT_WIN_BACK = 0x7C000000, HIT_WIN_FULL };   /* the corner player: itself, its two buttons; a computer: back, full screen */
/* Set while a swipe aims: Screen.tap should only move the focus, never act (open, close, change). */
extern int g_touch_aim;
void ui_hit(float x, float y, float w, float h, int id);
void ui_hits_begin(struct Screen *drawing);   /* app.c, before each screen draws */
int  ui_hit_find(float x, float y, const struct Screen *top, int *id);

/* Touch scrolling: a page, list or row that follows the finger. The screen keeps a UiPan
 * per scroller, says where it is on screen while drawing (ui_pan_area: the visible window,
 * vertical or not) and moves its scroll with ui_pan instead of ui_ease. Under a finger the
 * scroll is where the finger puts it (stretching past the ends); let go, it glides on and
 * slows, springing back from an end, and stays there. A remote key, a typed key or a
 * tap that acts brings back focus-led scrolling (want). max: the furthest it scrolls. */
typedef struct UiPan { float vel; int free; unsigned epoch; float *at; float max; } UiPan;
void  ui_pan_area(UiPan *p, float x, float y, float w, float h, int vertical);
float ui_pan(UiPan *p, float *pos, float want, float max, float dt, float speed);
int   ui_pan_free(const UiPan *p);     /* moved by the finger, not following the focus */
int   ui_pan_held(const UiPan *p);     /* under a finger right now */
/* main.c, from the finger: canvas units */
int   ui_pan_catch(float x, float y);  /* down: stops a glide under the finger (1: it was gliding) */
int   ui_pan_grab(float x, float y, int vertical);   /* a drag starts there: 1 = a scroller has it */
void  ui_pan_drag(float d);            /* how far the finger has gone along it since the grab */
void  ui_pan_release(float v);         /* let go, moving at v canvas units a second */
void  ui_pan_keys(void);               /* a key: every scroller follows its focus again */
void  ui_pan_hold(void);
/* Driven by a pointer (a finger, or a mouse on a computer): pages and rows scroll by it,
 * not only following the focus. */
#ifdef SNTV_DESKTOP_PLAYER
#define ui_pointer() 1
#else
#define ui_pointer() gfx_handheld()
#endif
int   ui_pan_wheel(float x, float y, float dy, float dx);   /* a mouse wheel over a scroller: it moves (1), or there is none there (0) */
void  ui_pan_bars(float px, float py);   /* scrollbars over the top screen's scrollers (a computer); px, py: the pointer, for the sideways ones */               /* a mouse pointing: the top screen's scrollers stay where they are as the focus moves */

/* A QR code made by qrcodegen, dark modules on a white rounded square. */
void ui_qr(const uint8_t *qr, float x, float y, float size);

#endif
