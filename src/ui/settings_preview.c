/* Settings previews: small drawings of a screen showing what the focused setting
 * changes, redrawn from g_app every frame. Artwork is drawn (gradients and a few
 * shapes) so a preview never waits on the network; the theme preview uses library
 * posters when there are some. */
#include "settings_preview.h"
#include "../app.h"
#include "../lang.h"
#include "../quality.h"
#include "font.h"
#include "theme.h"
#include "widgets.h"
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

void settings_preview_draw(PreviewKind kind, float x, float y, float w, float h)
{
    float sx = x + 24, sw = w - 48, sh = h - 90, sy = y + 16;   /* the screen most previews draw on */
    float base = y + h - 52;                                    /* captions and labels */
    char t[96];
    switch (kind) {
    case PV_NONE: case PV_COUNT: return;
    case PV_THEME: {
        screen(sx, sy, sw, sh);
        ui_row_header(1, "Popular", NULL, 24, sx + 24, sy + 22, sx + sw - 24, 1);
        float cw = (sw - 48 - 4 * 18) / 5, ch = cw * 1.5f, cy = sy + 80;
        int li = 0;
        for (int i = 0; i < 5; i++) {
            const LibraryItem *it = NULL;
            while (li < g_app.n_library && !it) {
                const LibraryItem *c = &g_app.library[li++];
                if (!c->removed && c->poster && *c->poster) it = c;
            }
            static const char *const NAMES[] = { "Dune", "Up", "Heat", "Her", "Drive" };
            ui_card(it ? it->poster : NULL, sx + 24 + i * (cw + 18), cy, cw, ch, i == 1, 0, it ? NULL : NAMES[i], CARD_MIP);
        }
        snprintf(t, sizeof t, "%s  \xC2\xB7  %s", THEME_NAMES[g_app.theme], THEME_ACCENT_NAMES[g_app.theme][g_app.theme_accent]);
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
            ui_clock_text(0, t, sizeof t);
            float tw = font_measure(t, FONT_S | FONT_B) + 36, ph = 46, px = sx + sw - 20 - tw;
            gfx_rrect(px, sy + 18, tw, ph, theme_pill_r(ph), THEME_PANEL);
            font_draw_center(t, px + tw / 2, sy + 18 + (ph - font_height(FONT_S | FONT_B)) / 2, FONT_S | FONT_B, THEME_TEXT);
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
        art(sx, sy, sw, sh, R_KEY, 0, 0);
        gfx_rrect_grad_h(sx, sy, sw * 0.7f, sh, R_KEY, COLOR_ALPHA(RGBA(0, 0, 0, 0xFF), 0xB0), COLOR_ALPHA(RGBA(0, 0, 0, 0xFF), 0));
        ui_clock_text(0, t, sizeof t);
        font_draw(t, sx + 30, sy + sh - 30 - font_height(FONT_XL | FONT_B), FONT_XL | FONT_B, RGBA(0xFF, 0xFF, 0xFF, 0xFF));
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
