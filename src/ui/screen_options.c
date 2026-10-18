/* Options sheet: a short list of actions over the dimmed page (hold OK or Yellow on a title). */
#include "screen_options.h"
#include "../app.h"
#include "../ai.h"
#include "font.h"
#include "theme.h"
#include "widgets.h"
#include <stdlib.h>
#include <string.h>

#define MAX_ITEMS 8
typedef struct {
    char title[160], subtitle[200];
    char *body;
    char items[MAX_ITEMS][64]; int icons[MAX_ITEMS];
    int n, focus; OptionsFn fn; void *user; float t;
} Opt;

static void update(Screen *s, float dt)
{
    Opt *o = s->st;
    o->t += dt / 0.32f;   /* the sheet rises, then its items arrive one after another */
    if (o->t > 1 + o->n * 0.08f) o->t = 1 + o->n * 0.08f; else app_redraw();
}

static float ease_out(float t) { t = t < 0 ? 0 : t > 1 ? 1 : t; return 1 - (1 - t) * (1 - t) * (1 - t); }

static void draw(Screen *s)
{
    Opt *o = s->st;
    float k = g_app.fx_light ? 1 : ease_out(o->t);
    Screen *below = g_app.depth >= 2 ? g_app.stack[g_app.depth - 2] : NULL;
    if (below && below->draw) below->draw(below); else ui_background(0);
    gfx_rect(0, 0, SNTV_UI_W, SNTV_UI_H, RGBA(0, 0, 0, (int)(140 * k)));

    float w = 760, pad = 40, item_h = 74;
    int body_lines = o->body ? font_draw_wrapped(o->body, 0, 0, FONT_S, 0, w - 2 * pad, 12) : 0;
    float body_h = body_lines ? body_lines * font_height(FONT_S) + 24 : 0;
    float head = o->subtitle[0] ? 120 : 92;
    float h = head + body_h + o->n * item_h + 64;
    float x = (SNTV_UI_W - w) / 2, y = (SNTV_UI_H - h) / 2 + (1 - k) * 60;
    gfx_set_alpha(k);
    if (!g_app.fx_light) gfx_shadow(x, y + 16, w, h, R_PANEL, 40, RGBA(0, 0, 0, 0x90));
    gfx_rrect(x, y, w, h, R_PANEL, THEME_PANEL);
    if (g_theme.focus_style == FOCUS_FRAME) gfx_rect(x, y, 6, h, THEME_ACCENT);
    else if (g_theme.focus_style == FOCUS_LIGHTBAR) gfx_rect(x + R_PANEL, y, w - 2 * R_PANEL, 3, THEME_ACCENT);
    else gfx_rrect_ring(x, y, w, h, R_PANEL, 1.5f, THEME_LINE);
    ui_title(o->title, x + pad, y + 34, FONT_M | FONT_B, THEME_TEXT, w - 2 * pad);
    if (o->subtitle[0]) font_draw_fit(o->subtitle, x + pad, y + 80, FONT_XS, THEME_TEXT_DIM, w - 2 * pad);
    float ry = y + head;
    if (body_lines) { font_draw_wrapped(o->body, x + pad, ry, FONT_S, THEME_TEXT_DIM, w - 2 * pad, 12); ry += body_h; }
    for (int i = 0; i < o->n; i++, ry += item_h) {
        float ik = g_app.fx_light ? 1 : ease_out((o->t - 0.35f - i * 0.08f) / 0.5f);
        if (ik <= 0) continue;
        int foc = i == o->focus;
        float ix = x + 24 + (1 - ik) * 40, iw = w - 48;
        gfx_set_alpha(k * ik);
        if (foc) { ui_focus_under(ix, ry, iw, item_h - 10, R_KEY, 1); gfx_rrect(ix, ry, iw, item_h - 10, R_KEY, THEME_FOCUS); }
        Color fg = foc ? THEME_ON_FOCUS : THEME_TEXT;
        float tx = ix + 28;
        if (o->icons[i] >= 0) { ui_icon((Icon)o->icons[i], tx + 14, ry + (item_h - 10) / 2, 28, fg); tx += 50; }
        font_draw_fit(o->items[i], tx, ry + (item_h - 10 - font_height(FONT_S | FONT_B)) / 2, FONT_S | FONT_B, fg, ix + iw - tx - 20);
    }
    gfx_set_alpha(k);
    font_draw("Back: close", x + pad, y + h - 40, FONT_XS, THEME_TEXT_MUTED);
    gfx_set_alpha(1);
}

static int key(Screen *s, KeyAction a)
{
    Opt *o = s->st;
    app_redraw();
    switch (a) {
    case KEY_UP: if (o->focus > 0) o->focus--; return 1;
    case KEY_DOWN: if (o->focus < o->n - 1) o->focus++; return 1;
    case KEY_LONG_OK: return 1;   /* the hold that opened the sheet */
    case KEY_OK: { OptionsFn fn = o->fn; void *u = o->user; int c = o->focus; app_pop(); if (fn) fn(c, u); return 1; }
    case KEY_BACK: case KEY_YELLOW: app_pop(); return 1;
    default: return 1;
    }
}

static void destroy(Screen *s) { Opt *o = s->st; free(o->body); free(o); free(s); }

Screen *screen_options_create_ex(const char *title, const char *subtitle, const char *body,
                                 const char *const *items, const int *icons, int n, OptionsFn fn, void *user)
{
    Screen *s = calloc(1, sizeof *s);
    Opt *o = calloc(1, sizeof *o);
    snprintf(o->title, sizeof o->title, "%s", title ? title : "");
    snprintf(o->subtitle, sizeof o->subtitle, "%s", subtitle ? subtitle : "");
    o->body = body && *body ? strdup(body) : NULL;
    for (int i = 0; i < n && i < MAX_ITEMS; i++) { snprintf(o->items[o->n], 64, "%s", items[i]); o->icons[o->n++] = icons ? icons[i] : -1; }
    o->fn = fn; o->user = user;
    s->name = "options"; s->st = o;
    s->update = update; s->draw = draw; s->key = key; s->destroy = destroy;
    return s;
}

Screen *screen_options_create(const char *title, const char *subtitle, const char *const *items, int n, OptionsFn fn, void *user)
{
    return screen_options_create_ex(title, subtitle, NULL, items, NULL, n, fn, user);
}

/* The usual options for a title: watched state, Continue Watching, Library. */
typedef struct { MetaPreview m; } TitleOpt;
static void title_choice(int c, void *user)
{
    TitleOpt *t = user;
    LibraryItem *li = app_library_find(t->m.id);
    int watched = app_title_watched(t->m.id), in_cw = li && li->time_offset > 0;
    int k = 0;
    if (c == k++) app_library_mark_watched(&t->m, !watched);
    else if (in_cw && c == k++) app_library_dismiss_continue(&t->m);
    else if (c == k++) app_library_toggle(&t->m);
    else if (ai_enabled() && c == k++) screen_detail_ask(&t->m);
    meta_preview_free(&t->m); free(t);
}
void screen_options_for_title(const MetaPreview *m)
{
    if (!m || !m->id) return;
    LibraryItem *li = app_library_find(m->id);
    int watched = app_title_watched(m->id), in_cw = li && li->time_offset > 0, in_lib = li && !li->removed && !li->temp;
    const char *items[5]; int n = 0;
    items[n++] = watched ? "Mark as not watched" : "Mark as watched";
    if (in_cw) items[n++] = "Remove from Continue Watching";
    items[n++] = in_lib ? "Remove from Library" : "Add to Library";
    if (ai_enabled()) items[n++] = "Ask AI about it (spoiler-free)";
    TitleOpt *t = calloc(1, sizeof *t);
    meta_preview_copy(&t->m, m);
    app_push(screen_options_create(m->name ? m->name : "Options", "Hold OK (or press Yellow) on any title", items, n, title_choice, t));
}
