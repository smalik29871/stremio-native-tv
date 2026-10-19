/* The island: the clock pill that becomes notices, progress and a resume card. */
#include "island.h"
#include "../http.h"
#include "font.h"
#include "theme.h"
#include "widgets.h"
#include "../log.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

enum { IS_NONE, IS_CLOCK, IS_NOTICE, IS_PROGRESS, IS_RESUME };

#define RESUME_FOR 900.0f   /* the resume card stays 15 minutes */

static struct {
    float w, h, wv, hv;          /* the pill now, and its spring velocities */
    int kind; float kind_at;     /* what it shows, and since when (content fades in) */
    char progress[120]; float progress_frac, progress_at;
    MetaPreview resume; int has_resume; char resume_line[160]; int resume_min; float resume_at;
} I;

void island_progress(const char *text, float frac)
{
    snprintf(I.progress, sizeof I.progress, "%s", text ? text : "");
    I.progress_frac = frac;
    I.progress_at = g_app.time;
    app_redraw();
}

void island_resume(const MetaPreview *meta, const char *line, int minutes_left)
{
    if (I.has_resume) meta_preview_free(&I.resume);
    memset(&I.resume, 0, sizeof I.resume);
    I.has_resume = meta != NULL;
    if (!meta) return;
    meta_preview_copy(&I.resume, meta);
    snprintf(I.resume_line, sizeof I.resume_line, "%s", line ? line : (meta->name ? meta->name : ""));
    I.resume_min = minutes_left;
    I.resume_at = g_app.time;
    app_redraw();
}

static int g_focus;

int island_has_focus(void)
{
    if (g_focus && I.kind != IS_RESUME) g_focus = 0;   /* the card went: so does the focus */
    return g_focus;
}

int island_take_focus(void)
{
    if (I.kind != IS_RESUME || !I.has_resume) return 0;
    g_focus = 1;
    app_redraw();
    return 1;
}

void island_focus_key(int ok)
{
    g_focus = 0;
    if (ok) island_key_blue();
    app_redraw();
}

int island_key_blue(void)
{
    if (!I.has_resume || g_app.time - I.resume_at > RESUME_FOR || I.kind != IS_RESUME) return 0;   /* only what is on show */
    MetaPreview m = I.resume;
    memset(&I.resume, 0, sizeof I.resume);
    I.has_resume = 0;
    LOGI("island: resume %s", m.id ? m.id : "?");
    app_push(screen_detail_create(&m));   /* the title page, its Resume button focused */
    meta_preview_free(&m);
    return 1;
}

static int current_kind(int show_clock)
{
    if (g_app.toast[0] && g_app.time < g_app.toast_until) return IS_NOTICE;
    if (I.progress[0] && g_app.time - I.progress_at < 0.5f) return IS_PROGRESS;
    if (I.has_resume && g_app.time - I.resume_at < RESUME_FOR && show_clock) return IS_RESUME;
    if (show_clock && g_app.clock_mode != 2) return IS_CLOCK;
    return IS_NONE;
}

void island_draw(int in_player, int show_clock)
{
    int kind = current_kind(show_clock);
    if (kind != I.kind) { I.kind = kind; I.kind_at = g_app.time; }
    char now[32]; ui_island_text(now, sizeof now);
    float dial = g_app.island_style == ISLAND_DIAL ? 34 : 0;
    int fs = FONT_S | FONT_B;
    float tw = 0, th = 52;
    switch (kind) {
    case IS_CLOCK:
        tw = font_measure(now, fs) + 44 + dial;
        if (g_update_version[0]) { char up[32]; snprintf(up, sizeof up, "Update %s", g_update_version); tw += font_measure(up, FONT_XS | FONT_B) + 52; }
        break;
    case IS_NOTICE: tw = font_measure(g_app.toast, fs) + 84; if (tw > 1100) tw = 1100; th = 60; break;
    case IS_PROGRESS: tw = font_measure(I.progress, fs) + 220; th = 60; break;
    case IS_RESUME: {
        float a = font_measure(I.resume_line, fs), b = font_measure("RESUME", FONT_XS | FONT_B) + 200;
        tw = (a > b ? a : b) + 76; if (tw > 760) tw = 760; th = 82;
        break;
    }
    default: tw = 0; th = 0; break;
    }
    static float last;
    float dt = g_app.time - last; last = g_app.time;
    if (dt < 0 || dt > 0.1f) dt = 0.1f;
    if (g_app.fx_light) { I.w = tw; I.h = th; I.wv = I.hv = 0; }
    else { I.w = ui_ease(I.w, tw, &I.wv, dt, 20); I.h = ui_ease(I.h, th, &I.hv, dt, 20); }

    float right = SNTV_UI_W - THEME_MARGIN, y = 47;
    float x = in_player ? (SNTV_UI_W - I.w) / 2 : right - I.w;
    if (!in_player && !http_online()) {   /* offline, beside the island */
        const char *t = "No internet";
        float ow = font_measure(t, fs) + 74, oh = 52, ox = x - ow - (I.w > 1 ? 14 : 0);
        gfx_rrect(ox, y, ow, oh, oh / 2, RGBA(0x5A, 0x14, 0x1E, 0xE0));
        gfx_circle(ox + 30, y + oh / 2, 7, THEME_DANGER);
        font_draw(t, ox + 48, y + (oh - font_height(fs)) / 2, fs, THEME_TEXT);
    }
    if (I.w < 2 || I.h < 2) return;

    float r = theme_pill_r(I.h), busy = kind == IS_NOTICE || kind == IS_PROGRESS || kind == IS_RESUME;
    if (busy && !g_app.fx_light) gfx_shadow(x, y + 6, I.w, I.h, r, 24, RGBA(0, 0, 0, 0x80));
    ui_panel(x, y, I.w, I.h, r, busy ? COLOR_ALPHA(THEME_PANEL, 0xF4) : COLOR_ALPHA(THEME_BG, 0xB0));
    gfx_rrect_ring(x, y, I.w, I.h, r, 1, busy ? COLOR_ALPHA(THEME_TEXT, 0x22) : THEME_LINE);

    /* the content fades in once the pill is near its new size */
    float a = (g_app.time - I.kind_at) / 0.18f;
    if (fabsf(I.w - tw) > 24) a = 0;
    if (a > 1 || g_app.fx_light) a = 1;
    if (a < 1) app_redraw();
    if (g_focus && kind == IS_RESUME) gfx_rrect_ring(x - 5, y - 5, I.w + 10, I.h + 10, r + 5, 4, THEME_FOCUS);   /* outside the clip */
    gfx_clip_push(x, y, I.w, I.h);
    gfx_set_alpha(a);
    float cy = y + (I.h - font_height(fs)) / 2;
    switch (kind) {
    case IS_CLOCK:
        if (dial) ui_mini_dial(x + 22 + 13, y + I.h / 2, 13, THEME_TEXT);
        if (g_theme.focus_style == FOCUS_FRAME && !dial) { gfx_rect(x + 12, y + I.h / 2 - 5, 10, 10, THEME_ACCENT); font_draw(now, x + 30, cy, fs, THEME_TEXT); }
        else font_draw(now, x + 22 + dial, cy, fs, THEME_TEXT);
        if (g_update_version[0]) {   /* a newer version is out: Settings > About > Update */
            char up[32]; snprintf(up, sizeof up, "Update %s", g_update_version);
            float ux = x + I.w - 22 - font_measure(up, FONT_XS | FONT_B);
            gfx_circle(ux - 16, y + I.h / 2, 5, THEME_ACCENT);
            font_draw(up, ux, y + (I.h - font_height(FONT_XS | FONT_B)) / 2, FONT_XS | FONT_B, THEME_ACCENT);
        }
        break;
    case IS_NOTICE:
        gfx_circle(x + 32, y + I.h / 2, 7, THEME_ACCENT2);
        font_draw_fit(g_app.toast, x + 54, cy, fs, THEME_TEXT, I.w - 80);
        break;
    case IS_PROGRESS: {
        font_draw_fit(I.progress, x + 26, cy, fs, THEME_TEXT, I.w - 200);
        float bw = 140, bx = x + I.w - bw - 26, by = y + I.h / 2 - 3;
        if (I.progress_frac >= 0) ui_progress(bx, by, bw, 6, I.progress_frac, THEME_ACCENT);
        else {   /* no measure: a short bar that sweeps */
            float p = fmodf(g_app.time * 0.8f, 1.0f);
            gfx_rrect(bx, by, bw, 6, 3, COLOR_ALPHA(THEME_TEXT, 0x30));
            gfx_clip_push(bx, by, bw, 6);
            gfx_rrect(bx - 40 + (bw + 40) * p, by, 40, 6, 3, THEME_ACCENT);
            gfx_clip_pop();
            app_redraw();
        }
        break;
    }
    case IS_RESUME: {
        float lx = x + 26, ky = y + 12;
        font_draw_tracked("RESUME", lx, ky, FONT_XS | FONT_B, THEME_ACCENT, 1.5f);
        char left[32];
        snprintf(left, sizeof left, "%d min left", I.resume_min);
        float rx = lx + font_measure_tracked("RESUME", FONT_XS | FONT_B, 1.5f) + 14;
        font_draw(left, rx, ky, FONT_XS, THEME_TEXT_DIM);
        /* the remote's blue key opens it, or OK once Up has focused it */
        const char *k = g_focus ? "OK" : "Blue";
        float kw = font_measure(k, FONT_XS | FONT_B) + 18, kx = x + I.w - kw - 20;
        gfx_rrect(kx, ky - 2, kw, 28, 6, g_focus ? THEME_FOCUS : THEME_BLUE);
        font_draw(k, kx + 9, ky + (24 - font_height(FONT_XS | FONT_B)) / 2, FONT_XS | FONT_B, g_focus ? THEME_ON_FOCUS : RGBA(0x06, 0x12, 0x1E, 0xFF));
        font_draw_fit(I.resume_line, lx, y + I.h - 16 - font_height(fs), fs, THEME_TEXT, I.w - 52);
        break;
    }
    default: break;
    }
    gfx_set_alpha(1);
    gfx_clip_pop();
    if (fabsf(I.w - tw) > 0.5f || fabsf(I.h - th) > 0.5f) app_redraw();
}
