#include "theme.h"
#include "font.h"
#include "../log.h"
#include <SDL.h>
#include <stdio.h>
#include <string.h>

Theme g_theme;

const char *const THEME_NAMES[THEME_COUNT] = { "Signal", "Marquee", "Glass", "Metro" };

const char *const THEME_ACCENT_NAMES[THEME_COUNT][THEME_ACCENTS] = {
    { "Lime", "Signal orange", "Cyan", "Magenta", "Yellow", "White" },
    { "Amber", "Ember", "Brass", "Rose", "Teal", "Ivory" },
    { "Ice", "Rose", "Mint", "Amber", "Violet", "Pearl" },
    { "Cobalt", "Teal", "Magenta", "Orange", "Lime", "Crimson" },
};

static const Color ACCENTS[THEME_COUNT][THEME_ACCENTS] = {
    { RGBA(0xC6, 0xFF, 0x3D, 0xFF), RGBA(0xFF, 0x6A, 0x2B, 0xFF), RGBA(0x3D, 0xE0, 0xFF, 0xFF),
      RGBA(0xFF, 0x3D, 0xA8, 0xFF), RGBA(0xFF, 0xD8, 0x3D, 0xFF), RGBA(0xF2, 0xF4, 0xF7, 0xFF) },
    { RGBA(0xFF, 0xB5, 0x47, 0xFF), RGBA(0xFF, 0x7A, 0x45, 0xFF), RGBA(0xD9, 0xB2, 0x6A, 0xFF),
      RGBA(0xFF, 0x8F, 0xA3, 0xFF), RGBA(0x4F, 0xD1, 0xC5, 0xFF), RGBA(0xF3, 0xED, 0xE4, 0xFF) },
    { RGBA(0x8A, 0xD4, 0xFF, 0xFF), RGBA(0xFF, 0x8F, 0xB1, 0xFF), RGBA(0x7B, 0xF1, 0xC8, 0xFF),
      RGBA(0xFF, 0xC5, 0x6B, 0xFF), RGBA(0xB9, 0xA3, 0xFF, 0xFF), RGBA(0xF4, 0xF6, 0xFA, 0xFF) },
    { RGBA(0x00, 0x78, 0xD7, 0xFF), RGBA(0x00, 0xAB, 0xA9, 0xFF), RGBA(0xD8, 0x00, 0x73, 0xFF),
      RGBA(0xFA, 0x68, 0x00, 0xFF), RGBA(0x60, 0xA9, 0x17, 0xFF), RGBA(0xE5, 0x14, 0x00, 0xFF) },
};

Color theme_accent_swatch(int id, int accent_idx)
{
    if (id < 0 || id >= THEME_COUNT) id = THEME_SIGNAL;
    if (accent_idx < 0 || accent_idx >= THEME_ACCENTS) accent_idx = 0;
    return ACCENTS[id][accent_idx];
}

/* Relative luminance, 0..1 (sRGB approximated with a 2.2 gamma: plenty for picking
 * dark or light text on the accent). */
static float luma(Color c)
{
    float r = ((c >> 24) & 255) / 255.0f, g = ((c >> 16) & 255) / 255.0f, b = ((c >> 8) & 255) / 255.0f;
    r *= r; g *= g; b *= b;
    return 0.2126f * r + 0.7152f * g + 0.0722f * b;
}

float theme_pill_r(float h) { return g_theme.r_pill < h / 2 ? g_theme.r_pill : h / 2; }

void theme_apply(int id, int accent_idx)
{
    if (id < 0 || id >= THEME_COUNT) id = THEME_SIGNAL;
    if (accent_idx < 0 || accent_idx >= THEME_ACCENTS) accent_idx = 0;
    Theme t = { 0 };
    t.id = id;
    t.accent_idx = accent_idx;
    t.accent = ACCENTS[id][accent_idx];
    t.danger = RGBA(0xFF, 0x4D, 0x5E, 0xFF);
    t.ok = RGBA(0x3D, 0xDC, 0x84, 0xFF);
    t.scrim = RGBA(0x00, 0x00, 0x00, 0xB0);
    t.shadow = RGBA(0x00, 0x00, 0x00, 0x90);

    switch (id) {
    case THEME_MARQUEE:
        t.bg = RGBA(0x0E, 0x0B, 0x09, 0xFF);
        t.bg2 = RGBA(0x18, 0x12, 0x0D, 0xFF);
        t.text = RGBA(0xF3, 0xED, 0xE4, 0xFF);
        t.panel = RGBA(0x1B, 0x15, 0x10, 0xF4);
        t.accent2 = color_lerp(t.accent, RGBA(0xFF, 0xF1, 0xD6, 0xFF), 0.35f);
        t.r_card = 6; t.r_panel = 10; t.r_key = 8; t.r_pill = 8;
        t.focus_style = FOCUS_LIGHTBAR;
        t.row_header = ROWHEAD_KICKER;
        t.upper_titles = 1;
        t.glow = 1;
        t.title_track = 1.0f;
        break;
    case THEME_GLASS:
        t.bg = RGBA(0x0A, 0x0C, 0x12, 0xFF);
        t.bg2 = RGBA(0x12, 0x15, 0x1F, 0xFF);
        t.text = RGBA(0xFF, 0xFF, 0xFF, 0xFF);
        t.panel = RGBA(0x1E, 0x23, 0x30, 0xC8);   /* tinted glass over the blurred art */
        t.accent2 = color_lerp(t.accent, RGBA(0xFF, 0xFF, 0xFF, 0xFF), 0.35f);
        t.r_card = 18; t.r_panel = 28; t.r_key = 18; t.r_pill = 999;
        t.focus_style = FOCUS_GLASS;
        t.row_header = ROWHEAD_PLAIN;
        t.glass = 1;
        t.glow = 1;
        break;
    case THEME_METRO:
        t.bg = RGBA(0x10, 0x10, 0x10, 0xFF);
        t.bg2 = RGBA(0x18, 0x18, 0x18, 0xFF);
        t.text = RGBA(0xFF, 0xFF, 0xFF, 0xFF);
        t.panel = RGBA(0x1F, 0x1F, 0x1F, 0xFF);
        t.accent2 = t.accent;
        t.r_card = 0; t.r_panel = 0; t.r_key = 0; t.r_pill = 0;
        t.focus_style = FOCUS_TILE;
        t.row_header = ROWHEAD_METRO;
        t.lower_headings = 1;
        break;
    default:   /* Signal */
        t.bg = RGBA(0x0B, 0x0C, 0x0E, 0xFF);
        t.bg2 = RGBA(0x11, 0x13, 0x16, 0xFF);
        t.text = RGBA(0xF2, 0xF4, 0xF7, 0xFF);
        t.panel = RGBA(0x15, 0x17, 0x1B, 0xF6);
        t.accent2 = t.accent;
        t.r_card = 4; t.r_panel = 6; t.r_key = 4; t.r_pill = 4;
        t.focus_style = FOCUS_FRAME;
        t.row_header = ROWHEAD_NUMBERED;
        t.upper_titles = 1;
        t.mono_meta = 1;
        t.ticks = 1;
        t.title_track = 1.5f;
        break;
    }
    t.text_dim = COLOR_ALPHA(t.text, 0xB8);
    t.text_muted = COLOR_ALPHA(t.text, 0x78);
    t.surface = COLOR_ALPHA(t.text, 0x12);
    t.surface_hi = COLOR_ALPHA(t.text, 0x24);
    t.line = COLOR_ALPHA(t.text, 0x1E);
    t.poster_ph = COLOR_ALPHA(t.text, 0x10);
    t.on_light = t.bg;
    t.accent_dim = COLOR_ALPHA(t.accent, 0x60);
    t.on_accent = luma(t.accent) > 0.35f ? RGBA(0x0A, 0x0A, 0x0C, 0xFF) : RGBA(0xFF, 0xFF, 0xFF, 0xFF);
    /* Marquee focuses in parchment (the amber is its light bar), Glass in frosted white;
     * the others in the accent */
    t.focus = id == THEME_MARQUEE ? t.text : id == THEME_GLASS ? RGBA(0xF4, 0xF6, 0xFA, 0xFF) : t.accent;
    t.on_focus = id == THEME_MARQUEE || id == THEME_GLASS ? t.bg : t.on_accent;
    if (id == THEME_GLASS) { t.surface = COLOR_ALPHA(t.text, 0x16); t.surface_hi = COLOR_ALPHA(t.text, 0x2A); }
    g_theme = t;
}

/* ---- type ---- */

static const struct { const char *regular, *bold, *alt; int alt_kind; } FONTS[THEME_COUNT] = {
    { "SpaceGrotesk-Medium.ttf", "SpaceGrotesk-Bold.ttf", "JetBrainsMono-Medium.ttf", FONT_ALT_MONO },
    { "Inter-Medium.otf", "Inter-Bold.otf", "BebasNeue-Regular.ttf", FONT_ALT_DISPLAY },
    { "Inter-Medium.otf", "Inter-Bold.otf", NULL, FONT_ALT_NONE },
    { "OpenSans-Medium.ttf", "OpenSans-Bold.ttf", "OpenSans-Light.ttf", FONT_ALT_DISPLAY },
};

static int g_fonts_loaded = -1;

static int asset_path(const char *name, char *out, size_t n)
{
    char *base = SDL_GetBasePath();
    const char *bases[3] = { base ? base : "", "", NULL };
    for (int b = 0; bases[b]; b++) {
        snprintf(out, n, "%sassets/fonts/%s", bases[b], name);
        FILE *f = fopen(out, "rb");
        if (f) { fclose(f); if (base) SDL_free(base); return 1; }
    }
    if (base) SDL_free(base);
    return 0;
}

int theme_load_fonts(int id)
{
    if (id < 0 || id >= THEME_COUNT) id = THEME_SIGNAL;
    if (g_fonts_loaded == id) return 0;
    char r[1200], b[1200], a[1200];
    int have_alt = FONTS[id].alt && asset_path(FONTS[id].alt, a, sizeof a);
    if (asset_path(FONTS[id].regular, r, sizeof r) && asset_path(FONTS[id].bold, b, sizeof b)
        && font_init(r, b, have_alt ? a : NULL, have_alt ? FONTS[id].alt_kind : FONT_ALT_NONE) == 0) {
        g_fonts_loaded = id;
        return 0;
    }
    LOGW("theme: %s fonts missing, trying Inter", THEME_NAMES[id]);
    if (asset_path("Inter-Medium.otf", r, sizeof r) && asset_path("Inter-Bold.otf", b, sizeof b) && font_init(r, b, NULL, 0) == 0) {
        g_fonts_loaded = id;
        return 0;
    }
    static const char *sys[][2] = {
        { "/usr/share/fonts/opentype/inter/Inter-Medium.otf", "/usr/share/fonts/opentype/inter/Inter-Bold.otf" },
        { "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf" },
        { "/usr/share/fonts/dejavu/DejaVuSans.ttf", "/usr/share/fonts/dejavu/DejaVuSans-Bold.ttf" },
    };
    for (size_t i = 0; i < sizeof sys / sizeof sys[0]; i++) {
        FILE *f = fopen(sys[i][0], "rb");
        if (f) { fclose(f); if (font_init(sys[i][0], sys[i][1], NULL, 0) == 0) { g_fonts_loaded = id; return 0; } }
    }
    return -1;
}
