/* Settings previews: small drawings of a screen showing what the focused setting
 * changes, redrawn from g_app every frame. Artwork is drawn (gradients and a few
 * shapes) so a preview never waits on the network; the theme preview uses library
 * posters when there are some. */
#include "settings_preview.h"
#include "../app.h"
#include "../lang.h"
#include "../quality.h"
#include "ambient_scenes.h"
#include "clockface.h"
#include "font.h"
#include "theme.h"
#include "widgets.h"
#include <math.h>
#include <time.h>
#include <stdio.h>
#include <string.h>

static const Color ART[][2] = {
    { RGBA(0xF4, 0x72, 0x5B, 0xFF), RGBA(0x6D, 0x28, 0xD9, 0xFF) }, { RGBA(0x38, 0xBD, 0xF8, 0xFF), RGBA(0x1E, 0x3A, 0x8A, 0xFF) },
    { RGBA(0xFB, 0xBF, 0x24, 0xFF), RGBA(0xB4, 0x53, 0x09, 0xFF) }, { RGBA(0x34, 0xD3, 0x99, 0xFF), RGBA(0x0F, 0x76, 0x6E, 0xFF) },
    { RGBA(0xF4, 0x72, 0xB6, 0xFF), RGBA(0x9D, 0x17, 0x4D, 0xFF) }, { RGBA(0xA7, 0x8B, 0xFA, 0xFF), RGBA(0x31, 0x2E, 0x81, 0xFF) },
};
#define N_ART ((int)(sizeof ART / sizeof ART[0]))

/* Drawn artwork: a gradient, a sun and a horizon; soft drops the shapes (a blurred still). */
static void art(float x, float y, float w, float h, float r, int i, int soft)
{
    const Color *c = ART[(i % N_ART + N_ART) % N_ART];
    gfx_rrect_grad_v(x, y, w, h, r, c[0], c[1]);
    if (soft) { gfx_rrect(x, y, w, h, r, COLOR_ALPHA(THEME_TEXT, 0x22)); return; }
    float s = (w < h ? w : h);
    gfx_circle(x + w * 0.7f, y + h * 0.32f, s * 0.16f, COLOR_ALPHA(RGBA(0xFF, 0xFF, 0xFF, 0xFF), 0x70));
    gfx_rrect_grad_v(x, y + h * 0.62f, w, h * 0.38f, r, COLOR_ALPHA(RGBA(0, 0, 0, 0xFF), 0x30), COLOR_ALPHA(RGBA(0, 0, 0, 0xFF), 0xA0));
}

/* A TV screen: dark glass with a hairline edge. */
static void screen(float x, float y, float w, float h)
{
    gfx_rrect(x, y, w, h, R_KEY, color_lerp(THEME_BG, RGBA(0, 0, 0, 0xFF), 0.35f));
    gfx_rrect_ring(x, y, w, h, R_KEY, 1.5f, THEME_LINE);
}

/* Text placeholders: a few rounded bars. */
static void bars(float x, float y, float w, int n, Color c)
{
    for (int i = 0; i < n; i++) gfx_rrect(x, y + i * 14, w * (i == 0 ? 1.0f : 0.72f - 0.12f * i), 7, 3.5f, c);
}

/* A label under an option; the chosen one sits on a filled pill. */
static void label(const char *s, float cx, float y, int chosen)
{
    float tw = font_measure(s, FONT_XS | FONT_B), h = 34;
    if (chosen) gfx_rrect(cx - tw / 2 - 14, y, tw + 28, h, theme_pill_r(h), THEME_FOCUS);
    font_draw_center(s, cx, y + (h - font_height(FONT_XS | FONT_B)) / 2, FONT_XS | FONT_B, chosen ? THEME_ON_FOCUS : THEME_TEXT_DIM);
}

static void chosen_ring(float x, float y, float w, float h) { gfx_rrect_ring(x - 4, y - 4, w + 8, h + 8, R_KEY + 4, 3, THEME_FOCUS); }

static void caption(const char *s, float x, float y, float w)
{
    font_draw_center(s, x + w / 2, y, FONT_XS, THEME_TEXT_DIM);
}

static void mini_rows(float x, float y, float w, int rows, int per, float ch)
{
    float gap = 6, cw = (w - gap * (per - 1)) / per;
    for (int r = 0; r < rows; r++)
        for (int i = 0; i < per; i++) art(x + i * (cw + gap), y + r * (ch + 10), cw, ch, 3, i + r * 2 + 1, 1);
}

static void home_layout(int style, float x, float y, float w, float h)
{
    screen(x, y, w, h);
    float m = 10;
    gfx_rrect(x + 4, y + 4, 6, h - 8, 3, THEME_LINE);   /* the rail */
    if (style == HOME_CINEMATIC) {
        art(x + 1, y + 1, w - 2, h * 0.62f, R_KEY, 0, 0);
        gfx_rrect_grad_h(x + 1, y + 1, w * 0.6f, h * 0.62f, R_KEY, COLOR_ALPHA(THEME_BG, 0xE0), COLOR_ALPHA(THEME_BG, 0));
        bars(x + m + 10, y + h * 0.18f, w * 0.34f, 3, COLOR_ALPHA(THEME_TEXT, 0xC0));
        mini_rows(x + m + 10, y + h * 0.70f, w - 2 * m - 10, 1, 5, h * 0.2f);
    } else if (style == HOME_SHOWCASE) {
        art(x + m + 10, y + m, w - 2 * m - 10, h * 0.42f, R_KEY * 0.6f, 4, 0);
        mini_rows(x + m + 10, y + h * 0.42f + m + 10, w - 2 * m - 10, 2, 5, h * 0.16f);
    } else {
        bars(x + m + 10, y + m + 2, w * 0.4f, 1, COLOR_ALPHA(THEME_TEXT, 0xC0));
        mini_rows(x + m + 10, y + m + 22, w - 2 * m - 10, 3, 6, h * 0.2f);
    }
}

static void lang_carousel(const char *code, Icon icon, float x, float y, float w, float h)
{
    int count = lang_count(), i = lang_index(code);
    ui_icon(icon, x + w / 2, y + h * 0.2f, 64, THEME_ACCENT);
    float cy = y + h * 0.48f;
    for (int d = -1; d <= 1; d++) {
        const char *c = LANG_CODES[(i + d + count) % count];
        const char *name = c[0] ? lang_display(c) : "Off";
        float cx = x + w / 2 + d * w * 0.32f;
        if (d == 0) {
            float tw = font_measure(name, FONT_M | FONT_B), bh = 64;
            gfx_rrect(cx - tw / 2 - 28, cy, tw + 56, bh, theme_pill_r(bh), THEME_FOCUS);
            font_draw_center(name, cx, cy + (bh - font_height(FONT_M | FONT_B)) / 2, FONT_M | FONT_B, THEME_ON_FOCUS);
        } else font_draw_center(name, cx, cy + (64 - font_height(FONT_S)) / 2, FONT_S, THEME_TEXT_MUTED);
    }
    ui_icon(ICON_CHEVRON_LEFT, x + 20, cy + 32, 24, THEME_TEXT_MUTED);
    ui_icon(ICON_CHEVRON_RIGHT, x + w - 20, cy + 32, 24, THEME_TEXT_MUTED);
}

/* Aperture's theme preview, as its mockup draws it: a small Home with the title over
 * the artwork and the tab bar under it, then the palettes, the one in use framed. */
static void aperture_live(float x, float y, float w, float h)
{
    font_draw("Live preview", x + 24, y + 4, FONT_M, THEME_TEXT);
    font_draw("See changes before you leave.", x + 24, y + 44, FONT_XS, THEME_TEXT_DIM);
    float cx = x + 24, cy = y + 88, cw = w - 48, bar = 70, ch = h - 88 - 80, ah = ch - bar;
    gfx_rect(cx, cy, cw, ch, THEME_BG);
    art(cx, cy, cw, ah, 0, 1, 0);
    gfx_gradient_h(cx, cy, cw * 0.6f, ah, COLOR_ALPHA(THEME_BG, 0xD0), COLOR_ALPHA(THEME_BG, 0));
    const char *name = "Lunar Tide";
    for (int i = 0; i < g_app.n_library; i++) if (!g_app.library[i].removed && g_app.library[i].name && *g_app.library[i].name) { name = g_app.library[i].name; break; }
    ui_serif_title(name, cx + 22, cy + ah - 22, cw * 0.62f, FONT_TITLE, 2, THEME_TEXT);
    static const Icon ICONS[] = { ICON_HOME, ICON_SEARCH, ICON_LIBRARY, ICON_SETTINGS };
    static const char *const TABS[] = { "Home", "Search", "Library", "Settings" };
    float tw = cw / 4;
    for (int i = 0; i < 4; i++) {
        float tx = cx + i * tw + tw / 2, ty = cy + ah + 10;
        Color c = i == 0 ? THEME_TEXT : THEME_TEXT_DIM;
        ui_icon(ICONS[i], tx, ty + 16, 24, c);
        font_draw_center(TABS[i], tx, ty + 32, FONT_XS, c);
        if (i == 0) gfx_rect(tx - 22, ty + 56, 44, 3, THEME_ACCENT);
    }
    gfx_rrect_ring(cx, cy, cw, ch, 2, 1.5f, THEME_LINE);
    int cur = theme_palette_index(g_app.theme_accent, g_app.theme_bg);
    float pw = 64, ph = 40, gap = 16, px = x + (w - THEME_PALETTES * pw - (THEME_PALETTES - 1) * gap) / 2, py = y + h - 56;
    for (int i = 0; i < THEME_PALETTES; i++, px += pw + gap) {
        if (i == cur) { gfx_rrect_ring(px - 4, py - 4, pw + 8, ph + 8, 5, 2.5f, THEME_TEXT); gfx_rect(px, py + ph + 8, pw, 3, THEME_ACCENT); }
        gfx_rrect(px, py, pw, ph, 3, theme_bg_swatch(THEME_APERTURE, THEME_PALETTE[i][1]));
        gfx_rrect(px + 10, py + ph / 2 - 4, pw - 20, 8, 4, theme_accent_swatch(THEME_APERTURE, THEME_PALETTE[i][0]));
        gfx_rrect_ring(px, py, pw, ph, 3, 1, COLOR_ALPHA(THEME_TEXT, 0x40));
    }
}

void settings_preview_draw(PreviewKind kind, float x, float y, float w, float h)
{
    float sx = x + 24, sw = w - 48, sh = h - 90, sy = y + 16;   /* the screen most previews draw on */
    float base = y + h - 52;                                    /* captions and labels */
    char t[96];
    switch (kind) {
    case PV_NONE: case PV_COUNT: return;
    case PV_THEME: {
        if (g_theme.id == THEME_APERTURE) { aperture_live(x, y, w, h); return; }
        screen(sx, sy, sw, sh);
        gfx_rrect(sx + 1, sy + 1, sw - 2, sh - 2, R_KEY, THEME_BG);   /* the chosen background */
        ui_row_header(1, "Popular", NULL, 24, sx + 24, sy + 22, sx + sw - 24, 1);
        float cw = (sw - 48 - 4 * 18) / 5, ch = cw * 1.5f, cy = sy + 80;
        int li = 0;
        for (int i = 0; i < 5; i++) {
            const LibraryItem *it = NULL;
            while (li < g_app.n_library && !it) {
                const LibraryItem *c = &g_app.library[li++];
                if (!c->removed && c->poster && *c->poster) it = c;
            }
            static const char *const NAMES[] = { "Dune", "Up", "Heat", "Her", "Jaws" };
            ui_card_capture(0);
            ui_card(it ? it->poster : NULL, sx + 24 + i * (cw + 18), cy, cw, ch, i == 1, 0, it ? NULL : NAMES[i], CARD_MIP);
            ui_card_capture(1);
        }
        snprintf(t, sizeof t, "%s  \xC2\xB7  %s  \xC2\xB7  %s", THEME_NAMES[g_app.theme], theme_accent_name(g_app.theme, g_app.theme_accent), THEME_BG_NAMES[g_app.theme][g_app.theme_bg]);
        label(t, x + w / 2, base, 1);
        return;
    }
    case PV_HOME_STYLE: {
        float gap = 18, mw = (sw - 2 * gap) / 3, mh = mw * 0.62f, my = sy + (sh - mh) / 2;
        for (int i = 0; i < HOME_STYLE_COUNT; i++) {
            float mx = sx + i * (mw + gap);
            home_layout(i, mx, my, mw, mh);
            if (i == g_app.home_style) chosen_ring(mx, my, mw, mh);
            label(HOME_STYLE_NAMES[i], mx + mw / 2, base, i == g_app.home_style);
        }
        return;
    }
    case PV_SUBS: {
        art(sx, sy, sw, sh, R_KEY, 1, 0);
        gfx_rrect_grad_v(sx, sy + sh * 0.4f, sw, sh * 0.6f, R_KEY, COLOR_ALPHA(RGBA(0, 0, 0, 0xFF), 0), COLOR_ALPHA(RGBA(0, 0, 0, 0xFF), 0x90));
        gfx_clip_push(sx, sy, sw, sh);
        gfx_set_translate(sx + sw / 2 - SNTV_UI_W / 2, 0);
        ui_subtitle("Subtitles look\nlike this.", sy + sh - 22, g_app.subs_size, g_app.subs_style);
        gfx_set_translate(0, 0);
        gfx_clip_pop();
        caption("Also for subtitles inside the video, in the TV's own lettering", x, base + 6, w);
        return;
    }
    case PV_CLOCK: {
        screen(sx, sy, sw, sh);
        mini_rows(sx + 24, sy + sh * 0.48f, sw - 48, 2, 5, sh * 0.18f);
        if (g_app.clock_mode != 2) {
            ui_island_text(t, sizeof t);
            float dial = g_app.island_style == ISLAND_DIAL ? 32 : 0;
            float tw = font_measure(t, FONT_S | FONT_B) + 36 + dial, ph = 46, px = sx + sw - 20 - tw;
            gfx_rrect(px, sy + 18, tw, ph, theme_pill_r(ph), THEME_PANEL);
            if (dial) ui_mini_dial(px + 18 + 12, sy + 18 + ph / 2, 12, THEME_TEXT);
            font_draw(t, px + 18 + dial, sy + 18 + (ph - font_height(FONT_S | FONT_B)) / 2, FONT_S | FONT_B, THEME_TEXT);
            if (g_app.island_style == ISLAND_SECONDS) app_redraw();
            chosen_ring(px, sy + 18, tw, ph);
        }
        caption(g_app.clock_mode == 2 ? "No clock in the corner" : "In the corner of every screen, hidden while a video plays", x, base + 6, w);
        return;
    }
    case PV_CARD_DETAILS: {
        screen(sx, sy, sw, sh);
        float cw = sw * 0.2f, ch = cw * 1.5f, cy = sy + (sh - ch) / 2;
        art(sx + 24, cy, cw, ch, R_CARD, 2, 1);
        art(sx + 24 + cw + 22, cy, cw, ch, R_CARD, 0, 0);
        chosen_ring(sx + 24 + cw + 22, cy, cw, ch);
        if (g_app.card_details) {
            float px = sx + 24 + 2 * cw + 44, pw = sw - (px - sx) - 20;
            gfx_rrect(px, cy, pw, ch, R_PANEL, THEME_PANEL);
            gfx_rrect(px + 18, cy + 18, 54, 24, 4, THEME_IMDB);
            bars(px + 18, cy + 58, pw - 36, 2, COLOR_ALPHA(THEME_TEXT, 0xC0));
            bars(px + 18, cy + 100, pw - 36, 4, COLOR_ALPHA(THEME_TEXT, 0x50));
        } else art(sx + 24 + 2 * (cw + 22), cy, cw, ch, R_CARD, 3, 1);
        caption(g_app.card_details ? "Rating, runtime and synopsis open beside the card" : "Cards stay as they are", x, base + 6, w);
        return;
    }
    case PV_HIDE_WATCHED: {
        screen(sx, sy, sw, sh);
        float gap = 16, cw = (sw - 48 - 4 * gap) / 5, ch = cw * 1.5f, cy = sy + (sh - ch) / 2;
        for (int i = 0; i < 5; i++) {
            float cx = sx + 24 + i * (cw + gap);
            int watched = i == 1 || i == 3;
            if (watched && g_app.hide_watched) { gfx_rrect_ring(cx, cy, cw, ch, R_CARD, 1.5f, COLOR_ALPHA(THEME_TEXT, 0x30)); continue; }
            art(cx, cy, cw, ch, R_CARD, i, 0);
            if (watched) { gfx_circle(cx + cw - 20, cy + 20, 14, THEME_OK); ui_icon(ICON_CHECK, cx + cw - 20, cy + 20, 18, RGBA(0x0A, 0x0A, 0x0C, 0xFF)); }
        }
        caption(g_app.hide_watched ? "Watched titles leave the catalog rows" : "Watched titles stay, marked with a tick", x, base + 6, w);
        return;
    }
    case PV_ADVISORY: {
        art(sx, sy, sw, sh, R_KEY, 5, 0);
        if (g_app.advisory) {
            gfx_clip_push(sx, sy, sw, sh);
            gfx_rrect_grad_h(sx, sy, sw * 0.8f, sh, R_KEY, COLOR_ALPHA(RGBA(0, 0, 0, 0xFF), 0xA0), COLOR_ALPHA(RGBA(0, 0, 0, 0xFF), 0));
            static const int lv[5] = { 1, 2, 2, 0, 0 };
            ui_advisory(sx + 26, sy + 26, lv, 1);
            gfx_clip_pop();
        }
        caption(g_app.advisory ? "For a few seconds as a video starts, from IMDb's parents guide" : "Videos start without it", x, base + 6, w);
        return;
    }
    case PV_BLUR: {
        float gap = 16, cw = (sw - 2 * gap) / 3, ch = cw * 0.5625f, cy = sy + (sh - ch) / 2 - 10;
        for (int i = 0; i < 3; i++) {
            float cx = sx + i * (cw + gap);
            art(cx, cy, cw, ch, R_CARD, i + 2, i > 0 && g_app.spoiler_blur);
            if (i == 0) { gfx_circle(cx + 22, cy + 22, 14, THEME_OK); ui_icon(ICON_CHECK, cx + 22, cy + 22, 18, RGBA(0x0A, 0x0A, 0x0C, 0xFF)); }
            snprintf(t, sizeof t, "Episode %d", i + 1);
            font_draw(t, cx, cy + ch + 12, FONT_XS | FONT_B, THEME_TEXT_DIM);
        }
        caption(g_app.spoiler_blur ? "Stills stay blurred until you watch the episode" : "Every still shows", x, base + 6, w);
        return;
    }
    case PV_FX: {
        int light = g_app.fx_setting == FX_LIGHT || (g_app.fx_setting == FX_AUTO && g_app.fx_light);
        float gap = 40, mw = (sw - gap) / 2, ch = sh * 0.62f, cw = ch / 1.5f, cy = sy + (sh - ch) / 2;
        for (int i = 0; i < 2; i++) {
            float cx = sx + i * (mw + gap) + (mw - cw) / 2;
            if (i == 0) { gfx_shadow(cx, cy + 10, cw, ch, R_CARD, 36, RGBA(0, 0, 0, 0xC0)); gfx_shadow(cx, cy, cw, ch, R_CARD, 28, COLOR_ALPHA(THEME_ACCENT, 0x60)); }
            art(cx, cy, cw, ch, R_CARD, 5, 0);
            if (i == light) chosen_ring(cx, cy, cw, ch);
            label(i ? "Light" : "Full", sx + i * (mw + gap) + mw / 2, base, i == light);
        }
        return;
    }
    case PV_OLED: {
        float half = sw / 2;
        gfx_rrect(sx, sy, sw, sh, R_KEY, RGBA(0, 0, 0, 0xFF));
        gfx_rrect(sx, sy, half, sh, R_KEY, color_lerp(RGBA(0x11, 0x13, 0x16, 0xFF), THEME_ACCENT, 0.04f));
        for (int i = 0; i < 2; i++) mini_rows(sx + i * half + 20, sy + sh * 0.5f, half - 40, 1, 3, sh * 0.3f);
        chosen_ring(sx + (g_app.oled ? half : 0), sy, half, sh);
        label("Graphite", sx + half / 2, base, !g_app.oled);
        label("Pure black", sx + half * 1.5f, base, g_app.oled);
        return;
    }
    case PV_RES_CAP: {
        static const struct { const char *name; float k; int px; } R[] = { { "4K", 1.0f, 2160 }, { "1080p", 0.66f, 1080 }, { "720p", 0.44f, 720 } };
        float bw = sh * 1.5f, bh = sh, bx = sx + (sw - bw) / 2;
        int cap = g_app.res_cap >= 2160 ? 0 : g_app.res_cap >= 1080 ? 1 : 2;
        for (int i = 0; i < 3; i++) {
            float w2 = bw * R[i].k, h2 = bh * R[i].k, y2 = sy + bh - h2;
            gfx_rrect(bx, y2, w2, h2, R_KEY, i >= cap ? COLOR_ALPHA(THEME_ACCENT, i == cap ? 0x50 : 0x28) : COLOR_ALPHA(THEME_TEXT, 0x08));
            gfx_rrect_ring(bx, y2, w2, h2, R_KEY, i == cap ? 3 : 1.5f, i == cap ? THEME_FOCUS : THEME_LINE);
            font_draw(R[i].name, bx + w2 - font_measure(R[i].name, FONT_XS | FONT_B) - 12, y2 + 10, FONT_XS | FONT_B, i >= cap ? THEME_TEXT : THEME_TEXT_MUTED);
        }
        caption("Recommended sources stay at or below the lit size", x, base + 6, w);
        return;
    }
    case PV_QUALITY: {
        static const char *const SRC[] = { "4K HDR  \xC2\xB7  38 GB", "1080p  \xC2\xB7  9 GB", "720p  \xC2\xB7  2 GB" };
        static const int ORDER[QUAL_MODE_COUNT][3] = { { 1, 0, 2 }, { 0, 1, 2 }, { 0, 1, 2 }, { 1, 0, 2 }, { 2, 1, 0 } };
        int m = g_app.quality_mode < 0 || g_app.quality_mode >= QUAL_MODE_COUNT ? 0 : g_app.quality_mode;
        float rh = 62, ry = sy + (sh - 3 * rh - 24) / 2;
        for (int i = 0; i < 3; i++) {
            float yy = ry + i * (rh + 12);
            gfx_rrect(sx + 30, yy, sw - 60, rh, R_PANEL, i == 0 ? THEME_FOCUS : COLOR_ALPHA(THEME_TEXT, 0x10));
            snprintf(t, sizeof t, "%d", i + 1);
            font_draw(t, sx + 54, yy + (rh - font_height(FONT_S | FONT_B)) / 2, FONT_S | FONT_B, i == 0 ? THEME_ON_FOCUS : THEME_TEXT_MUTED);
            font_draw(SRC[ORDER[m][i]], sx + 96, yy + (rh - font_height(FONT_S | FONT_B)) / 2, FONT_S | FONT_B, i == 0 ? THEME_ON_FOCUS : THEME_TEXT);
        }
        caption(m == QUAL_ADDON_ORDER ? "In the order your addons answered" : "The top one is recommended", x, base + 6, w);
        return;
    }
    case PV_SKIP: {
        art(sx, sy, sw, sh, R_KEY, 3, 0);
        float ty = sy + sh - 34, tx = sx + 30, tw = sw - 60, a = 0.12f, b = 0.3f, at = 0.18f;
        gfx_rrect(tx, ty, tw, 6, 3, COLOR_ALPHA(RGBA(0xFF, 0xFF, 0xFF, 0xFF), 0x50));
        gfx_rrect(tx + tw * a, ty, tw * (b - a), 6, 3, THEME_ACCENT);
        font_draw("INTRO", tx + tw * a, ty - 30, FONT_XS | FONT_B, RGBA(0xFF, 0xFF, 0xFF, 0xE0));
        if (g_app.skip_mode == 1) {
            ui_arc(tx + tw * (at + b) / 2, ty - 6, tw * (b - at) / 2, 3, 0.75f, 0.5f, RGBA(0xFF, 0xFF, 0xFF, 0xFF));
            gfx_circle(tx + tw * b, ty + 3, 9, RGBA(0xFF, 0xFF, 0xFF, 0xFF));
        } else gfx_circle(tx + tw * at, ty + 3, 9, RGBA(0xFF, 0xFF, 0xFF, 0xFF));
        if (g_app.skip_mode == 0) { float pw = ui_pill_width("Skip intro", 1, 48); ui_pill("Skip intro", 1, ICON_NEXT, sx + sw - 24 - pw, ty - 80, 48, PILL_PRIMARY, 1); }
        static const char *const C[] = { "A Skip button comes up; OK skips", "Jumps past the intro by itself", "Plays everything" };
        caption(C[g_app.skip_mode % 3], x, base + 6, w);
        return;
    }
    case PV_AUTOPLAY: {
        art(sx, sy, sw, sh, R_KEY, 5, 0);
        float cw = sw * 0.56f, chh = 84, cx = sx + sw - cw - 18, cy = sy + sh - chh - 18;
        gfx_rrect(cx, cy, cw, chh, R_PANEL, COLOR_ALPHA(THEME_PANEL, 0xF2));
        art(cx + 10, cy + 10, 112, chh - 20, R_CARD * 0.6f, 1, 0);
        font_draw("UP NEXT", cx + 136, cy + 14, FONT_XS | FONT_B, THEME_ACCENT2);
        if (g_app.autoplay_next) ui_progress(cx + 136, cy + chh - 24, cw - 156, 6, 0.6f, THEME_ACCENT);
        else { gfx_rrect(cx + cw - 110, cy + chh - 42, 96, 30, R_PANEL, THEME_OK); font_draw_center("Play now", cx + cw - 62, cy + chh - 42 + (30 - font_height(FONT_XS | FONT_B)) / 2, FONT_XS | FONT_B, THEME_ON_LIGHT); }
        caption(g_app.autoplay_next ? "The next episode starts by itself" : "Waits for you to press Play now", x, base + 6, w);
        return;
    }
    case PV_PIP: {
        screen(sx, sy, sw, sh);
        mini_rows(sx + 24, sy + sh * 0.42f, sw - 48, 2, 5, sh * 0.2f);
        bars(sx + 24, sy + 24, sw * 0.3f, 3, COLOR_ALPHA(THEME_TEXT, 0x90));
        if (g_app.pip_enabled) {
            float pw = sw * 0.34f, ph = pw * 0.5625f, px = sx + sw - pw - 16, py = sy + 16;
            art(px, py, pw, ph, R_CARD, 0, 0);
            chosen_ring(px, py, pw, ph);
            ui_icon(ICON_PLAY, px + pw / 2, py + ph / 2, 28, RGBA(0xFF, 0xFF, 0xFF, 0xE0));
        }
        caption(g_app.pip_enabled ? "Back keeps the video playing in a corner" : "Back stops the video", x, base + 6, w);
        return;
    }
    case PV_SCRUB: {
        art(sx, sy, sw, sh, R_KEY, g_app.live_scrub ? 4 : 1, 0);
        float ty = sy + sh - 34, tx = sx + 30, tw = sw - 60, at = 0.62f;
        gfx_rrect(tx, ty, tw, 6, 3, COLOR_ALPHA(RGBA(0xFF, 0xFF, 0xFF, 0xFF), 0x50));
        gfx_rrect(tx, ty, tw * at, 6, 3, THEME_ACCENT);
        gfx_circle(tx + tw * at, ty + 3, 10, RGBA(0xFF, 0xFF, 0xFF, 0xFF));
        if (!g_app.live_scrub) { gfx_rrect(sx, sy, sw, sh - 60, R_KEY, COLOR_ALPHA(RGBA(0, 0, 0, 0xFF), 0x60)); ui_icon(ICON_PAUSE, sx + sw / 2, sy + sh * 0.4f, 44, RGBA(0xFF, 0xFF, 0xFF, 0xC0)); }
        caption(g_app.live_scrub ? "The picture follows the timeline while you seek" : "The picture waits until you let go", x, base + 6, w);
        return;
    }
    case PV_AMBIENT: {
        ui_clock_text(0, t, sizeof t);
        gfx_rrect(sx, sy, sw, sh, R_KEY, RGBA(0, 0, 0, 0xFF));
        gfx_clip_push(sx, sy, sw, sh);
        if (g_app.ambient_style == AMBIENT_WALL) {
            float tw = (sw - 50) / 3, th = tw * 0.5625f;
            for (int r = 0; r < 4; r++) for (int c = 0; c < 3; c++) art(sx + 12 + c * (tw + 13) + (r & 1 ? tw * 0.25f : 0) - tw * 0.12f, sy + 12 + r * (th + 13) - 20, tw, th, 6, r * 3 + c, 0);
            gfx_rect(sx, sy, sw, sh, RGBA(0, 0, 0, 0x50));
        } else if (g_app.ambient_style == AMBIENT_ARTWORK) {
            art(sx, sy, sw, sh, R_KEY, 0, 0);
            gfx_rrect_grad_h(sx, sy, sw * 0.7f, sh, R_KEY, COLOR_ALPHA(RGBA(0, 0, 0, 0xFF), 0xB0), COLOR_ALPHA(RGBA(0, 0, 0, 0xFF), 0));
        } else if (g_app.ambient_style == AMBIENT_AURORA) { ambient_aurora(sx, sy, sw, sh, g_app.time); app_redraw(); }
        else if (g_app.ambient_style == AMBIENT_WARP) { ambient_warp(sx, sy, sw, sh, g_app.time); app_redraw(); }
        else if (g_app.ambient_style == AMBIENT_RAIN) { art(sx, sy, sw, sh, R_KEY, 4, 0); ambient_rain(0, 0, 0, sx, sy, sw, sh, g_app.time, 1); app_redraw(); }
        else if (g_app.ambient_style == AMBIENT_MARQUEE) {
            float px, py, pw, ph;
            ambient_marquee(sx, sy, sw, sh, g_app.time, &px, &py, &pw, &ph);
            art(px, py, pw, ph, 3, 2, 0);
            bars(px + pw + sw * 0.08f, py + ph * 0.3f, sw * 0.32f, 3, COLOR_ALPHA(RGBA(0xFF, 0xE8, 0xC0, 0xFF), 0xA0));
            app_redraw();
        }
        gfx_clip_pop();
        if (g_app.ambient_style == AMBIENT_CLOCK) {
            time_t now = time(NULL); struct tm tmv; localtime_r(&now, &tmv);
            struct timespec ts; clock_gettime(CLOCK_REALTIME, &ts);
            gfx_clip_push(sx, sy, sw, sh);
            clockface_draw(g_app.clock_face, sx + sw / 2, sy + sh / 2, sh / 1080 * 1.15f, &tmv, tmv.tm_sec + ts.tv_nsec / 1e9);
            gfx_clip_pop();
            app_redraw();
        }
        else if (g_app.ambient_style == AMBIENT_LOGO) {
            float p = fmodf(g_app.time * 0.15f, 1.0f), q = fmodf(g_app.time * 0.11f, 1.0f);
            float lx = sx + 20 + (sw - 110) * (p < 0.5f ? p * 2 : 2 - p * 2), ly = sy + 20 + (sh - 110) * (q < 0.5f ? q * 2 : 2 - q * 2);
            ui_mark(lx, ly, 70);
            app_redraw();
        } else font_draw(t, sx + 30, sy + sh - 30 - font_height(FONT_XL | FONT_B), FONT_XL | FONT_B, RGBA(0xFF, 0xFF, 0xFF, 0xFF));
        if (g_app.ambient_minutes) snprintf(t, sizeof t, "After %d min with the remote quiet", g_app.ambient_minutes);
        else snprintf(t, sizeof t, "Off: the screen stays as it is");
        caption(t, x, base + 6, w);
        return;
    }
    case PV_BREAK: {
        art(sx, sy, sw, sh, R_KEY, 2, 0);
        snprintf(t, sizeof t, "Break in %d min", g_app.break_lead_min > 0 ? g_app.break_lead_min : 5);
        float tw = font_measure(t, FONT_S | FONT_B) + 80, bh = 52, bx = sx + (sw - tw) / 2;
        gfx_rrect(bx, sy + 20, tw, bh, theme_pill_r(bh), COLOR_ALPHA(THEME_PANEL, 0xF4));
        ui_icon(ICON_BELL, bx + 32, sy + 20 + bh / 2, 24, THEME_ACCENT);
        font_draw(t, bx + 56, sy + 20 + (bh - font_height(FONT_S | FONT_B)) / 2, FONT_S | FONT_B, THEME_TEXT);
        caption(g_app.breaks_on ? "A banner warns first; playback pauses, then carries on" : "Breaks are off", x, base + 6, w);
        return;
    }
    case PV_LANG_AUDIO: lang_carousel(g_app.pref_audio, ICON_AUDIO, sx, sy, sw, sh); caption("Picked by itself when a video has it", x, base + 6, w); return;
    case PV_LANG_SUBS: lang_carousel(g_app.pref_subs, ICON_SUBS, sx, sy, sw, sh); caption("From the video first, then from your addons", x, base + 6, w); return;
    case PV_MEMORY: {   /* what stays in memory: a few rows of posters, or many */
        int low = g_app.memory_mode == 1 || (g_app.memory_mode == 0 && g_app.low_memory);
        float gap = 30, mw = (sw - gap) / 2, mh = sh * 0.86f;
        for (int i = 0; i < 2; i++) {
            float mx = sx + i * (mw + gap);
            screen(mx, sy, mw, mh);
            int rows = i ? 4 : 2, per = i ? 6 : 4;
            mini_rows(mx + 14, sy + 16, mw - 28, rows, per, (mh - 32 - (rows - 1) * 10) / rows - 4);
            if (i == !low) chosen_ring(mx, sy, mw, mh);
            label(i ? "Normal" : "Low", mx + mw / 2, base, i == !low);
        }
        return;
    }
    case PV_SUBS_SYNC: {   /* the subtitle lines against the speech in the video */
        float ty = sy + sh * 0.3f, lh = 34, gx = sx + 20, gw = sw - 40;
        font_draw("Speech", gx, ty - 34, FONT_XS | FONT_B, THEME_TEXT_DIM);
        static const float SP[][2] = { { 0.06f, 0.22f }, { 0.32f, 0.5f }, { 0.6f, 0.86f } };
        for (int i = 0; i < 3; i++) gfx_rrect(gx + gw * SP[i][0], ty, gw * (SP[i][1] - SP[i][0]), lh, 6, COLOR_ALPHA(THEME_TEXT, 0x40));
        float off = g_app.subs_autosync ? 0 : 0.09f, y2 = ty + lh + 70;
        font_draw("Subtitles", gx, y2 - 34, FONT_XS | FONT_B, THEME_TEXT_DIM);
        for (int i = 0; i < 3; i++) gfx_rrect(gx + gw * (SP[i][0] + off), y2, gw * (SP[i][1] - SP[i][0]), lh, 6, g_app.subs_autosync ? THEME_ACCENT : THEME_DANGER);
        caption(g_app.subs_autosync ? "Lines land on the speech" : "Lines from addons can come early or late", x, base + 6, w);
        return;
    }
    case PV_PREFLIGHT: case PV_AUTO_REC: case PV_SRC_AUDIO: {   /* the source list, as each switch changes it */
        static const char *const S[] = { "1080p  \xC2\xB7  English", "4K  \xC2\xB7  Italian", "720p  \xC2\xB7  English" };
        int order[3] = { 0, 1, 2 };
        if (kind == PV_SRC_AUDIO && !g_app.src_audio_lang) { order[0] = 1; order[1] = 0; }
        if (kind == PV_PREFLIGHT && g_app.preflight) { order[1] = 2; order[2] = 1; }   /* the dead link sinks */
        float rh = 62, ry = sy + (sh - 3 * rh - 24) / 2;
        for (int i = 0; i < 3; i++) {
            float yy = ry + i * (rh + 12);
            int top = i == 0;
            gfx_rrect(sx + 30, yy, sw - 60, rh, R_PANEL, top ? THEME_FOCUS : COLOR_ALPHA(THEME_TEXT, 0x10));
            Color fg = top ? THEME_ON_FOCUS : THEME_TEXT;
            font_draw(S[order[i]], sx + 56, yy + (rh - font_height(FONT_S | FONT_B)) / 2, FONT_S | FONT_B, fg);
            if (kind == PV_PREFLIGHT) {
                float cx = sx + sw - 64, cy = yy + rh / 2;
                if (!g_app.preflight) ui_icon(ICON_INFO, cx, cy, 24, top ? fg : THEME_TEXT_MUTED);
                else if (order[i] == 1) ui_icon(ICON_CLOSE, cx, cy, 22, THEME_DANGER);
                else ui_icon(ICON_CHECK, cx, cy, 24, top ? fg : THEME_OK);
            }
            if (kind == PV_AUTO_REC && top && g_app.auto_recommended) ui_icon(ICON_PLAY, sx + sw - 64, yy + rh / 2, 26, fg);
        }
        caption(kind == PV_PREFLIGHT ? (g_app.preflight ? "Links that answer, checked before they're recommended" : "Recommended without checking the link")
              : kind == PV_AUTO_REC ? (g_app.auto_recommended ? "The top source starts by itself" : "The list waits for you to choose")
              : (g_app.src_audio_lang ? "Sources in your audio language rise to the top" : "Language plays no part in the order"), x, base + 6, w);
        return;
    }
    case PV_ADDON_WAIT: {   /* addons answering over time; the wait line decides who is in */
        static const struct { const char *name; float at; } A[] = { { "Addon A", 1.5f }, { "Addon B", 4.0f }, { "Addon C", 11.0f } };
        float tx = sx + 150, tw = sw - 180, span = 16, ry = sy + 40;
        float line = g_app.addon_wait >= 99 ? span : (float)g_app.addon_wait;
        for (int i = 0; i < 3; i++) {
            float yy = ry + i * 72, ax = tx + tw * (A[i].at > span ? 1 : A[i].at / span);
            int in = A[i].at <= line;
            font_draw(A[i].name, sx + 20, yy + 6, FONT_XS | FONT_B, in ? THEME_TEXT : THEME_TEXT_MUTED);
            gfx_rrect(tx, yy + 12, ax - tx, 8, 4, in ? THEME_ACCENT : COLOR_ALPHA(THEME_TEXT, 0x30));
            gfx_circle(ax, yy + 16, 9, in ? THEME_ACCENT : COLOR_ALPHA(THEME_TEXT, 0x50));
        }
        float lx = tx + tw * (line / span > 1 ? 1 : line / span);
        gfx_rect(lx - 1.5f, ry - 20, 3, 3 * 72 + 10, THEME_FOCUS);
        snprintf(t, sizeof t, g_app.addon_wait >= 99 ? "Every addon" : "%d s", g_app.addon_wait);
        font_draw_center(t, lx, ry - 50, FONT_XS | FONT_B, THEME_TEXT);
        caption("Autoplay chooses from the sources in by then", x, base + 6, w);
        return;
    }
    case PV_P2P: {   /* TorBox's cache, or the TV fetching from peers */
        float cy = sy + sh / 2, tvx = sx + sw - 150;
        screen(tvx, cy - 50, 130, 90);
        font_draw_center("TV", tvx + 65, cy - 5 - font_height(FONT_XS | FONT_B) / 2, FONT_XS | FONT_B, THEME_TEXT_DIM);
        gfx_rrect(sx + 30, cy - 90, 180, 56, R_PANEL, COLOR_ALPHA(THEME_TEXT, 0x14));
        font_draw_center("TorBox cache", sx + 120, cy - 62 - font_height(FONT_XS | FONT_B) / 2, FONT_XS | FONT_B, THEME_TEXT);
        gfx_line(sx + 210, cy - 62, tvx, cy - 20, 3, THEME_ACCENT);
        if (g_app.p2p_fallback)
            for (int i = 0; i < 4; i++) {
                float px = sx + 50 + i * 46, py = cy + 70 + (i & 1) * 26;
                gfx_circle(px, py, 12, THEME_ACCENT2);
                gfx_line(px, py, tvx, cy + 10, 1.5f, COLOR_ALPHA(THEME_ACCENT2, 0x80));
            }
        caption(g_app.p2p_fallback ? "No cached copy: the TV plays it from peers (they see your IP)" : "No cached copy: the source is skipped", x, base + 6, w);
        return;
    }
    case PV_UPDATE: {   /* the island asking at a quiet moment */
        art(sx, sy, sw, sh, R_KEY, 5, 0);
        char v[64]; snprintf(v, sizeof v, "Version %s is ready", g_update_version[0] ? g_update_version : "9.9");
        float pw = font_measure(v, FONT_S | FONT_B) + 60, ph = 112, px = sx + (sw - pw) / 2, py = sy + 30;
        if (pw < 330) { px -= (330 - pw) / 2; pw = 330; }
        gfx_rrect(px, py, pw, ph, theme_pill_r(56), COLOR_ALPHA(THEME_PANEL, 0xF4));
        font_draw_center(v, px + pw / 2, py + 16, FONT_S | FONT_B, THEME_TEXT);
        float bw = 130, bx = px + pw / 2 - bw - 8, by = py + 58;
        gfx_rrect(bx, by, bw, 38, theme_pill_r(38), THEME_FOCUS);
        font_draw_center("Update now", bx + bw / 2, by + (38 - font_height(FONT_XS | FONT_B)) / 2, FONT_XS | FONT_B, THEME_ON_FOCUS);
        gfx_rrect(bx + bw + 16, by, bw, 38, theme_pill_r(38), COLOR_ALPHA(THEME_TEXT, 0x18));
        font_draw_center("Later", bx + bw * 1.5f + 16, by + (38 - font_height(FONT_XS | FONT_B)) / 2, FONT_XS | FONT_B, THEME_TEXT);
        caption(g_app.auto_update ? "Asked when nothing is playing" : "A notice only; Update here installs it", x, base + 6, w);
        return;
    }
    case PV_PARENTAL: {
        int set = g_app.parental_pin[0] != 0;
        ui_icon(set ? ICON_LOCK : ICON_UNLOCK, x + w / 2, sy + sh * 0.3f, 96, set ? THEME_ACCENT : THEME_TEXT_MUTED);
        for (int i = 0; i < 4; i++) {
            float dx = x + w / 2 - 84 + i * 56, dy = sy + sh * 0.62f;
            gfx_rrect(dx - 20, dy - 20, 40, 40, theme_pill_r(40), COLOR_ALPHA(THEME_TEXT, 0x14));
            if (set) gfx_circle(dx, dy, 8, THEME_TEXT);
        }
        caption(set ? "Settings and adult catalogs ask for the PIN" : "No PIN: everything is open", x, base + 6, w);
        return;
    }
    case PV_TRANSITIONS: {
        float mw = sw * 0.62f, mh = sh * 0.78f;
        screen(sx, sy + sh - mh, mw, mh);
        mini_rows(sx + 18, sy + sh - mh + mh * 0.5f, mw - 36, 2, 4, mh * 0.18f);
        float off = sw - mw;
        if (g_app.transitions) for (int i = 3; i >= 1; i--) gfx_rrect_ring(sx + off - i * 30, sy, mw, mh, R_KEY, 2, COLOR_ALPHA(THEME_ACCENT, 0x90 - i * 0x28));
        screen(sx + off, sy, mw, mh);
        art(sx + off + 1, sy + 1, mw - 2, mh * 0.55f, R_KEY, 4, 0);
        caption(g_app.transitions ? "Screens slide and fade in" : "Screens change at once", x, base + 6, w);
        return;
    }
    }
}
