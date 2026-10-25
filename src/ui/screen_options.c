/* Options sheet: a short list of actions over the dimmed page (hold OK or Yellow on a title). */
#include "../newep.h"
#include "screen_options.h"
#include "../app.h"
#include "../ai.h"
#include "../lists.h"
#include "../log.h"
#include "../video/video.h"
#include "font.h"
#include "theme.h"
#include "widgets.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_ITEMS 8
typedef struct {
    char title[160], subtitle[200];
    char *body;
    char items[MAX_ITEMS][64]; int icons[MAX_ITEMS];
    int n, focus; OptionsFn fn; void *user; float t;
    float opened_at, grace;   /* opened by itself: an OK within `grace` s was meant for the screen beneath */
} Opt;

static void update(Screen *s, float dt)
{
    Opt *o = s->st;
    o->t += dt / 0.32f;   /* the sheet rises, then its items arrive one after another */
    if (o->t > 1 + o->n * 0.08f) o->t = 1 + o->n * 0.08f; else app_redraw();
}

enum { HIT_OUTSIDE = -1, HIT_PANEL = -2 };   /* touch: beside the sheet, on it; a choice is its index */
static float ease_out(float t) { t = t < 0 ? 0 : t > 1 ? 1 : t; return 1 - (1 - t) * (1 - t) * (1 - t); }

static void draw(Screen *s)
{
    Opt *o = s->st;
    float k = g_app.fx_light ? 1 : ease_out(o->t);
    Screen *below = g_app.depth >= 2 ? g_app.stack[g_app.depth - 2] : NULL;
    if (below && below->draw) { ui_hits_begin(below); below->draw(below); ui_hits_begin(s); } else ui_background(0);   /* what is below is not this sheet's to tap */
    gfx_rect(0, 0, SNTV_UI_W, SNTV_UI_H, RGBA(0, 0, 0, (int)(140 * k)));
    ui_hit(0, 0, SNTV_UI_W, SNTV_UI_H, HIT_OUTSIDE);

    float w = SNTV_UI_W - 40 < 760 ? SNTV_UI_W - 40 : 760, pad = 40, item_h = SNTV_PHONE ? 96 : 74;
    int body_lines = o->body ? font_draw_wrapped(o->body, 0, 0, FONT_S, 0, w - 2 * pad, 12) : 0;
    float body_h = body_lines ? body_lines * font_height(FONT_S) + 24 : 0;
    float head = o->subtitle[0] ? 120 : 92;
    float h = head + body_h + o->n * item_h + 64;
    float x = (SNTV_UI_W - w) / 2, y = (SNTV_UI_H - h) / 2 + (1 - k) * 60;
    gfx_set_alpha(k);
    if (!g_app.fx_light) gfx_shadow(x, y + 16, w, h, R_PANEL, 40, RGBA(0, 0, 0, 0x90));
    ui_panel(x, y, w, h, R_PANEL, THEME_PANEL);
    ui_hit(x, y, w, h, HIT_PANEL);
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
        ui_hit(ix, ry, iw, item_h - 10, i);
        gfx_set_alpha(k * ik);
        if (foc) { ui_focus_under(ix, ry, iw, item_h - 10, R_KEY, 1); gfx_rrect(ix, ry, iw, item_h - 10, R_KEY, THEME_FOCUS); }
        Color fg = foc ? THEME_ON_FOCUS : THEME_TEXT;
        float tx = ix + 28;
        if (o->icons[i] >= 0) { ui_icon((Icon)o->icons[i], tx + 14, ry + (item_h - 10) / 2, 28, fg); tx += 50; }
        font_draw_fit(o->items[i], tx, ry + (item_h - 10 - font_height(FONT_S | FONT_B)) / 2, FONT_S | FONT_B, fg, ix + iw - tx - 20);
    }
    gfx_set_alpha(k);
    if (!gfx_handheld()) font_draw("Back: close", x + pad, y + h - 40, FONT_XS, THEME_TEXT_MUTED);
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
    case KEY_OK: {
        if (g_app.time - o->opened_at < o->grace) return 1;   /* an OK meant for the screen beneath, as this opened: not an answer */
        OptionsFn fn = o->fn; void *u = o->user; int c = o->focus; app_pop(); if (fn) fn(c, u); return 1; }
    case KEY_BACK: case KEY_YELLOW: app_pop(); return 1;
    default: return 1;
    }
}

/* A tapped choice is taken; a tap beside the sheet closes it. */
static int tap(Screen *s, int id)
{
    Opt *o = s->st;
    if (id == HIT_OUTSIDE) { if (!g_touch_aim) app_pop(); return 1; }
    if (id < 0 || id >= o->n) return 1;
    o->focus = id;
    return 0;
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
    o->opened_at = g_app.time;
    s->name = "options"; s->st = o;
    s->update = update; s->draw = draw; s->key = key; s->tap = tap; s->destroy = destroy;
    Screen *under = app_top();
    s->phone = under ? under->phone : 1;   /* it fits any width; upright only when what it covers is too */
    return s;
}

Screen *screen_options_unasked(Screen *s)
{
    ((Opt *)s->st)->grace = 0.6f;
    return s;
}

Screen *screen_options_create(const char *title, const char *subtitle, const char *const *items, int n, OptionsFn fn, void *user)
{
    return screen_options_create_ex(title, subtitle, NULL, items, NULL, n, fn, user);
}

/* Save to a list: the lists with a tick where the title is, OK puts it in or takes it out. */
static MetaPreview g_list_title;   /* the title the sheet is about (one sheet at a time) */
static void list_choice(int c, void *user)
{
    (void)user;
    if (c < 0 || c >= LIST_COUNT || !g_list_title.id) return;
    int added = lists_toggle(c, &g_list_title);
    if (added >= 0) app_toast(added ? "Saved to %s" : "Taken out of %s", LIST_NAMES[c]);
}
void screen_options_lists(const MetaPreview *m)
{
    if (!m || !m->id) return;
    meta_preview_free(&g_list_title);
    meta_preview_copy(&g_list_title, m);
    const char *items[LIST_COUNT]; int icons[LIST_COUNT]; char labels[LIST_COUNT][64];
    for (int i = 0; i < LIST_COUNT; i++) {
        int has = lists_has(i, m->id), n = lists_count(i);
        if (n) snprintf(labels[i], sizeof labels[i], "%s  \xC2\xB7  %d title%s", LIST_NAMES[i], n, n == 1 ? "" : "s");
        else snprintf(labels[i], sizeof labels[i], "%s", LIST_NAMES[i]);
        items[i] = labels[i]; icons[i] = has ? ICON_CHECK : ICON_PLUS;
    }
    app_push(screen_options_create_ex("Save to a list", m->name, "Each list is a row on Home. A tick: the title is in it; OK takes it out.", items, icons, LIST_COUNT, list_choice, NULL));
}

/* The usual options for a title: watched state, Continue Watching, Library, lists. */
typedef struct { MetaPreview m; } TitleOpt;
static void title_choice(int c, void *user)
{
    TitleOpt *t = user;
    LibraryItem *li = app_library_find(t->m.id);
    int watched = app_title_watched(t->m.id), in_cw = (li && li->time_offset > 0) || newep_get(t->m.id);
    int k = 0;
    if (c == k++) app_library_mark_watched(&t->m, !watched);
    else if (in_cw && c == k++) {
        if (newep_get(t->m.id) && !li->time_offset) { newep_dismiss(t->m.id); app_toast("Removed from Continue Watching"); }
        else app_library_dismiss_continue(&t->m);
    }
    else if (c == k++) app_library_toggle(&t->m);
    else if (ai_enabled() && c == k++) screen_detail_ask(&t->m);
    else if (c == k++) screen_options_lists(&t->m);
    meta_preview_free(&t->m); free(t);
}
void screen_options_for_title(const MetaPreview *m)
{
    if (!m || !m->id) return;
    LibraryItem *li = app_library_find(m->id);
    int watched = app_title_watched(m->id), in_cw = (li && li->time_offset > 0) || newep_get(m->id), in_lib = li && !li->removed && !li->temp;
    const char *items[6]; int n = 0;
    items[n++] = watched ? "Mark as not watched" : "Mark as watched";
    if (in_cw) items[n++] = "Remove from Continue Watching";
    items[n++] = in_lib ? "Remove from Library" : "Add to Library";
    if (ai_enabled()) items[n++] = "Ask AI about it (spoiler-free)";
    items[n++] = "Save to a list\xE2\x80\xA6";
    TitleOpt *t = calloc(1, sizeof *t);
    meta_preview_copy(&t->m, m);
    app_push(screen_options_create(m->name ? m->name : "Options", "Hold OK (or press Yellow) on any title", items, n, title_choice, t));
}

/* YouTube refused this device (it asks to sign in to prove it is no bot). An LG has Cuelight
 * YouTube, YouTube's own TV client in a companion app, and opens it straight away, or offers to
 * install it; the device's YouTube app is the other way. Back in either returns here. */
enum { YT_INSTALL, YT_APP };
static struct { char id[24]; int act[3]; } g_yt_pick;
static void yt_app_choice(int c, void *user)
{
    (void)user;
    if (c < 0 || c > 2) return;
    if (g_yt_pick.act[c] == YT_INSTALL) { app_push(screen_ytweb_install_create(g_yt_pick.id, "play")); return; }
    if (g_yt_pick.act[c] != YT_APP) return;
    LOGI("youtube: handed %s to the YouTube app", g_yt_pick.id);
    if (!video_open_youtube(g_yt_pick.id)) app_toast("The YouTube app did not open");
}
void screen_options_youtube_app(const char *yt_id, const char *error)
{
    if (yt_id && *yt_id && video_youtube_web(yt_id, 0, "play", app_ytweb_flags())) { app_toast("Playing it in Cuelight YouTube"); return; }
    int install = yt_id && *yt_id && video_youtube_web_state() == 0, app = yt_id && *yt_id && video_has_youtube_app();
    if (!install && !app) { app_toast("YouTube would not play this one%s%s", error ? ": " : "", error ? error : ""); return; }
    const char *items[3]; int icons[3], n = 0;
    snprintf(g_yt_pick.id, sizeof g_yt_pick.id, "%s", yt_id);
    if (install) { items[n] = "Install Cuelight YouTube and play it"; icons[n] = ICON_PLAY; g_yt_pick.act[n++] = YT_INSTALL; }
    if (app) { items[n] = "Open in the YouTube app"; icons[n] = install ? ICON_LINK : ICON_PLAY; g_yt_pick.act[n++] = YT_APP; }
    items[n] = "Not now"; icons[n] = ICON_CLOSE; g_yt_pick.act[n++] = -1;
    char body[400];
    if (install) snprintf(body, sizeof body, "YouTube is asking this TV to sign in to prove it isn't a bot. Cuelight YouTube, a small companion app, plays it in YouTube's own TV client, where you can sign in. It installs in a few seconds; Back in it returns here.");
    else snprintf(body, sizeof body, "YouTube is asking this %s to sign in to prove it isn't a bot, which only its own app can do. The YouTube app plays it; Back there brings you here again.", gfx_handheld() ? "phone" : "TV");
    app_push(screen_options_create_ex("YouTube would not play this here", NULL, body, items, icons, n, yt_app_choice, NULL));
}
