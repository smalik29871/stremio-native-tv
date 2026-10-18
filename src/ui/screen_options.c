/* Options sheet: a short list of actions over the dimmed page (hold OK or Yellow on a title). */
#include "screen_options.h"
#include "../app.h"
#include "font.h"
#include "theme.h"
#include "widgets.h"
#include <stdlib.h>
#include <string.h>

typedef struct { char title[160], subtitle[200]; char items[8][64]; int n, focus; OptionsFn fn; void *user; float t; } Opt;

static void update(Screen *s, float dt) { Opt *o = s->st; o->t = ui_ease(o->t * 100, 100, NULL, dt, 16) / 100; if (o->t < 0.999f) app_redraw(); }

static void draw(Screen *s)
{
    Opt *o = s->st;
    float k = g_app.fx_light ? 1 : o->t;
    Screen *below = g_app.depth >= 2 ? g_app.stack[g_app.depth - 2] : NULL;
    if (below && below->draw) below->draw(below); else ui_background(0);
    gfx_rect(0, 0, SNTV_UI_W, SNTV_UI_H, RGBA(0, 0, 0, 120));
    float w = 720, h = 150 + o->n * 78.0f + 40, x = (SNTV_UI_W - w) / 2, y = (SNTV_UI_H - h) / 2 + (1 - k) * 40;
    gfx_rrect(x + 6, y + 14, w, h, 28, RGBA(0, 0, 0, 90));
    gfx_rrect(x, y, w, h, 28, RGBA(0x18, 0x15, 0x2A, (int)(250 * k)));
    font_draw_fit(o->title, x + 40, y + 36, FONT_M | FONT_B, THEME_TEXT, w - 80);
    if (o->subtitle[0]) font_draw_fit(o->subtitle, x + 40, y + 80, FONT_XS, THEME_TEXT_DIM, w - 80);
    float ry = y + 130;
    for (int i = 0; i < o->n; i++, ry += 78) {
        int foc = i == o->focus;
        if (foc) gfx_rrect(x + 24, ry, w - 48, 66, 18, THEME_TEXT);
        font_draw(o->items[i], x + 50, ry + (66 - font_height(FONT_S | FONT_B)) / 2, FONT_S | FONT_B, foc ? THEME_ON_LIGHT : THEME_TEXT);
    }
    font_draw("Back: close", x + 40, y + h - 34, FONT_XS, THEME_TEXT_MUTED);
}

static int key(Screen *s, KeyAction a)
{
    Opt *o = s->st;
    app_redraw();
    switch (a) {
    case KEY_UP: if (o->focus > 0) o->focus--; return 1;
    case KEY_DOWN: if (o->focus < o->n - 1) o->focus++; return 1;
    case KEY_OK: { OptionsFn fn = o->fn; void *u = o->user; int c = o->focus; app_pop(); if (fn) fn(c, u); return 1; }
    case KEY_BACK: case KEY_YELLOW: app_pop(); return 1;
    default: return 1;
    }
}

static void destroy(Screen *s) { free(s->st); free(s); }

Screen *screen_options_create(const char *title, const char *subtitle, const char *const *items, int n, OptionsFn fn, void *user)
{
    Screen *s = calloc(1, sizeof *s);
    Opt *o = calloc(1, sizeof *o);
    snprintf(o->title, sizeof o->title, "%s", title ? title : "");
    snprintf(o->subtitle, sizeof o->subtitle, "%s", subtitle ? subtitle : "");
    for (int i = 0; i < n && i < 8; i++) snprintf(o->items[o->n++], 64, "%s", items[i]);
    o->fn = fn; o->user = user;
    s->name = "options"; s->st = o;
    s->update = update; s->draw = draw; s->key = key; s->destroy = destroy;
    return s;
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
    meta_preview_free(&t->m); free(t);
}
void screen_options_for_title(const MetaPreview *m)
{
    if (!m || !m->id) return;
    LibraryItem *li = app_library_find(m->id);
    int watched = app_title_watched(m->id), in_cw = li && li->time_offset > 0, in_lib = li && !li->removed && !li->temp;
    const char *items[4]; int n = 0;
    items[n++] = watched ? "Mark as not watched" : "Mark as watched";
    if (in_cw) items[n++] = "Remove from Continue Watching";
    items[n++] = in_lib ? "Remove from Library" : "Add to Library";
    TitleOpt *t = calloc(1, sizeof *t);
    meta_preview_copy(&t->m, m);
    app_push(screen_options_create(m->name ? m->name : "Options", "Hold OK (or press Yellow) on any title", items, n, title_choice, t));
}
