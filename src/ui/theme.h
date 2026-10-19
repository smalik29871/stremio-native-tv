#ifndef SNTV_THEME_H
#define SNTV_THEME_H
#include "gfx.h"
#include "../platform/platform.h"

/* Three looks share every layout and component; a theme is a token set plus a few
 * style switches the widgets read. All of them are flat on purpose: no blur and no
 * full-screen translucent layers, so they cost nothing on old TV GPUs.
 *   Signal  (default) broadcast / Swiss: graphite, one electric accent, sharp corners,
 *           a solid frame with a hard offset block as focus, mono metadata.
 *   Marquee cinema programme: warm ink, parchment text, projector amber, condensed
 *           display titles, a light bar under the focused card.
 *   Glass   frosted panels over blurred artwork, soft light edges, a white focus glow
 *           (the blur is a low mip of a texture already loaded, not a blur pass).
 *   Metro   flat colour tiles, big light type, square corners, a white tile border
 *           as focus, lowercase headings. */
enum { THEME_SIGNAL, THEME_MARQUEE, THEME_GLASS, THEME_METRO, THEME_COUNT };
enum { FOCUS_FRAME, FOCUS_LIGHTBAR, FOCUS_GLASS, FOCUS_TILE };
enum { ROWHEAD_NUMBERED, ROWHEAD_KICKER, ROWHEAD_PLAIN, ROWHEAD_METRO };
#define THEME_ACCENTS 6

typedef struct Theme {
    int id, accent_idx;
    Color bg, bg2, surface, surface_hi, panel, line;
    Color accent, accent2, accent_dim, on_accent;
    Color focus, on_focus;  /* fill and text of a focused control */
    Color text, text_dim, text_muted, on_light;
    Color danger, ok, scrim, poster_ph, shadow;
    float r_card, r_panel, r_key, r_pill;   /* r_pill: capsule when >= h/2 */
    int focus_style;      /* FOCUS_* */
    int row_header;       /* ROWHEAD_* */
    int upper_titles;     /* titles and labels in capitals */
    int mono_meta;        /* "2012 / 95 MIN" metadata in the mono face */
    int glass;            /* panels are frosted glass; screens sit on blurred artwork */
    int lower_headings;   /* row and section headings in lowercase (Metro) */
    int ticks;            /* timelines carry tick marks */
    int glow;             /* soft ambient light behind screens */
    float title_track;    /* extra letter spacing for capital titles, px per glyph at FONT_M */
} Theme;

extern Theme g_theme;
extern const char *const THEME_NAMES[THEME_COUNT];
extern const char *const THEME_ACCENT_NAMES[THEME_COUNT][THEME_ACCENTS];

void  theme_apply(int id, int accent_idx);
/* Loads the theme's typefaces (no-op when they are already loaded); -1 when no font works. */
int   theme_load_fonts(int id);
Color theme_accent_swatch(int id, int accent_idx);
/* Corner radius for a control of height h (pills are capsules only in themes that want them). */
float theme_pill_r(float h);

/* The names screens have always used, now read from the active theme. */
#define THEME_BG          (g_theme.bg)
#define THEME_BG2         (g_theme.bg2)
#define THEME_SURFACE     (g_theme.surface)
#define THEME_SURFACE_HI  (g_theme.surface_hi)
#define THEME_PANEL       (g_theme.panel)
#define THEME_LINE        (g_theme.line)
#define THEME_ACCENT      (g_theme.accent)
#define THEME_ACCENT2     (g_theme.accent2)
#define THEME_ACCENT_DIM  (g_theme.accent_dim)
#define THEME_ON_ACCENT   (g_theme.on_accent)
#define THEME_FOCUS       (g_theme.focus)
#define THEME_ON_FOCUS    (g_theme.on_focus)
#define THEME_TEXT        (g_theme.text)
#define THEME_TEXT_DIM    (g_theme.text_dim)
#define THEME_TEXT_MUTED  (g_theme.text_muted)
#define THEME_ON_LIGHT    (g_theme.on_light)
#define THEME_DANGER      (g_theme.danger)
#define THEME_OK          (g_theme.ok)
#define THEME_SCRIM       (g_theme.scrim)
#define THEME_POSTER_PH   (g_theme.poster_ph)
#define THEME_SHADOW      (g_theme.shadow)
#define THEME_IMDB        RGBA(0xF5, 0xC5, 0x18, 0xFF)
#define THEME_GOLD        RGBA(0xF5, 0x9E, 0x0B, 0xFF)
#define THEME_BLUE        RGBA(0x38, 0xBD, 0xF8, 0xFF)

#define THEME_MARGIN 96.0f
#define R_CARD  (g_theme.r_card)
#define R_PANEL (g_theme.r_panel)
#define R_KEY   (g_theme.r_key)

#endif
