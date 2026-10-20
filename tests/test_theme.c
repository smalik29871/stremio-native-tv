/* Theme contracts without creating a GL context. Fonts are not baked here. */
#include "ui/theme.h"
#include "ui/font.h"
#include <stdio.h>
#include <string.h>

static int failures, font_calls;
#define CHECK(c) do { if (!(c)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

int font_init(const char *regular, const char *bold, const char *alt, int kind)
{
    CHECK(regular && *regular && bold && *bold);
    font_calls++;
    return 0;
}

int main(void)
{
    /* Appending the theme must not reinterpret existing profiles. */
    CHECK(THEME_SIGNAL == 0 && THEME_STREAM == 6 && THEME_APERTURE == 7);
    CHECK(!strcmp(THEME_NAMES[THEME_APERTURE], "Aperture"));
    for (int id = 0; id < THEME_COUNT; id++) {
        CHECK(THEME_NAMES[id] && *THEME_NAMES[id]);
        for (int a = 0; a < THEME_ACCENTS; a++) {
            for (int b = 0; b < THEME_BGS; b++) {
                theme_apply(id, a, b);
                CHECK(g_theme.id == id && COLOR_A(THEME_BG) == 255);
                CHECK(COLOR_A(theme_accent_swatch(id, a)) == 255);
                CHECK(THEME_BG == theme_bg_swatch(id, b));
                CHECK(THEME_ACCENT_NAMES[id][a] && THEME_BG_NAMES[id][b]);
                CHECK(theme_pill_r(56) >= 0 && theme_pill_r(56) <= 28);
            }
        }
        CHECK(theme_load_fonts(id) == 0);
    }
    CHECK(font_calls == THEME_COUNT);
    theme_apply(THEME_APERTURE, 0, 0);
    CHECK(THEME_BG == RGBA(0x18, 0x19, 0x1B, 255));
    CHECK(THEME_TEXT == RGBA(0xF2, 0xED, 0xE3, 255));
    CHECK(THEME_ACCENT == RGBA(0xEF, 0x83, 0x73, 255));
    CHECK(THEME_FOCUS == THEME_TEXT && THEME_ON_FOCUS == THEME_BG);
    CHECK(g_theme.focus_style == FOCUS_APERTURE);
    CHECK(!g_theme.glass && !g_theme.glow && !g_theme.frost);
    Color fixed = THEME_ACCENT;
    theme_art_accent(RGBA(0, 255, 0, 255));
    CHECK(THEME_ACCENT == fixed);
    theme_apply(THEME_APERTURE, THEME_ACCENT_ART, 0);
    theme_art_accent(RGBA(0, 255, 0, 255));
    CHECK(THEME_FOCUS == THEME_TEXT && THEME_ON_FOCUS == THEME_BG);
    theme_apply(-1, -1, -1);
    CHECK(g_theme.id == THEME_SIGNAL);
    theme_apply(THEME_COUNT, THEME_ACCENTS + 1, THEME_BGS);
    CHECK(g_theme.id == THEME_SIGNAL);
    puts(failures ? "Theme contracts failed" : "Theme contracts passed");
    return failures ? 1 : 0;
}
