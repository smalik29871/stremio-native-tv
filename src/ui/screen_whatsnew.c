/* What's new: this version's notes from CHANGELOG.md (built in by tools/whatsnew.cmake), the ones for this kind
 * of device (a [phone], [tv] or [desktop] tag in the changelog; none means everyone: src/whatsnew_notes.h).
 * Shown once after an update, and from Settings > About (there under what Cuelight is). Up/Down scroll, OK or Back
 * closes it. */
#include "../app.h"
#include "../log.h"
#include "../store.h"
#include "../whatsnew_notes.h"
#include "../platform/platform.h"
#include "whatsnew.h"
#include "font.h"
#include "theme.h"
#include "widgets.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { ITEMS_MAX = 40 };
#define TEXT_X (SNTV_PHONE ? 40.0f : 160.0f)   /* a phone upright: the width of the screen */
#define TEXT_W (SNTV_PHONE ? SNTV_UI_W - 80.0f : 1240.0f)
#define TOP 300.0f

typedef struct {
    Note *notes; int n;
    int first;                /* the note at the top */
    int about;                /* Settings > About: what Cuelight is, above the notes */
} WhatsNew;

static const char *const ABOUT[] = {
    SNTV_APP_NAME " is a player made for TVs: written from scratch in C for the TV's own hardware, so it starts fast and stays smooth on a modest TV.",
    "It plays from addons in the open Stremio addon format, has its own catalog of films and series (from TMDB), and needs no account. "
    "Your addons, Library and progress stay on this TV, or sync with Stremio, Trakt or SimKL if you choose.",
    SNTV_APP_NAME " is not affiliated with or endorsed by Stremio. What you watch comes from the addons you install.",
    NULL,
};

/* the kind of device this is, for the notes that are only for some */
static int device(void)
{
    if (gfx_handheld()) return NOTE_PHONE;
#ifdef SNTV_DESKTOP_PLAYER
    return NOTE_DESKTOP;
#else
    return NOTE_TV;
#endif
}

static const char *device_name(int d) { return d == NOTE_PHONE ? "your phone" : d == NOTE_DESKTOP ? "your computer" : "your TV"; }

static void draw(Screen *s)
{
    WhatsNew *w = s->st;
    gfx_rect(0, 0, SNTV_UI_W, SNTV_UI_H, THEME_BG);
    ui_brand(TEXT_X, 64, 48);
    char title[96];
    snprintf(title, sizeof title, w->about ? "About " SNTV_APP_NAME : "What's new in %s", SNTV_APP_VERSION);
    if (g_theme.id == THEME_APERTURE) font_draw(title, TEXT_X, 160, FONT_XL | FONT_ALT, THEME_TEXT);
    else font_draw(title, TEXT_X, 170, FONT_L | FONT_B, THEME_TEXT);
    float y = TOP;
    if (w->about) {
        font_draw("Version " SNTV_APP_VERSION, TEXT_X, 236, FONT_S, THEME_TEXT_DIM);
        y = TOP - 10;
        for (int k = 0; ABOUT[k]; k++) y += font_draw_wrapped(ABOUT[k], TEXT_X, y, FONT_S, THEME_TEXT_DIM, TEXT_W, 3) * font_height(FONT_S) + 18;
        y += 24;
        font_draw("What's new in " SNTV_APP_VERSION, TEXT_X, y, FONT_M | FONT_B, THEME_TEXT);
        y += font_height(FONT_M) + 24;
    } else {
        char sub[96];
        snprintf(sub, sizeof sub, w->n ? "This update  \xC2\xB7  for %s" : "Nothing listed for this version", device_name(device()));
        font_draw(sub, TEXT_X, 236, FONT_S, THEME_TEXT_DIM);
    }
    const float pad = SNTV_PHONE ? 26 : 34, bar = 8;
    const int tf = FONT_M | FONT_B;
    float tw = TEXT_W - 2 * pad - bar;
    int i = w->first;
    for (; i < w->n; i++) {   /* one card a note: a bold title, then what it does */
        const Note *n = &w->notes[i];
        int tl = n->title[0] ? font_draw_wrapped(n->title, 0, 0, tf, 0, tw, 2) : 0;
        int bl = n->body[0] ? font_draw_wrapped(n->body, 0, 0, FONT_S, 0, tw, SNTV_PHONE ? 10 : 5) : 0;
        float h = 2 * pad + tl * font_height(tf) + (tl && bl ? 12 : 0) + bl * font_height(FONT_S);
        if (y + h > SNTV_UI_H - 150 && i > w->first) break;   /* the next one is below: Down for it */
        gfx_rrect(TEXT_X, y, TEXT_W, h, SNTV_PHONE ? 26 : R_PANEL, COLOR_ALPHA(THEME_TEXT, 0x12));
        gfx_rrect(TEXT_X + pad * 0.5f, y + pad * 0.7f, 5, h - pad * 1.4f, 2.5f, THEME_ACCENT);
        float tx = TEXT_X + pad + bar, ty = y + pad;
        if (tl) { font_draw_wrapped(n->title, tx, ty, tf, THEME_TEXT, tw, 2); ty += tl * font_height(tf) + (bl ? 12 : 0); }
        if (bl) font_draw_wrapped(n->body, tx, ty, FONT_S, tl ? THEME_TEXT_DIM : THEME_TEXT, tw, SNTV_PHONE ? 10 : 5);
        y += h + 18;
    }
    const char *hint = SNTV_PHONE ? (i < w->n ? "Swipe up: more   \xC2\xB7   Tap: close" : "Tap: close") : i < w->n ? "Down: more   \xC2\xB7   OK: close" : "OK: close";   /* some still below */
    font_draw(hint, TEXT_X, SNTV_UI_H - 110, FONT_S, THEME_TEXT_MUTED);
}

static int key(Screen *s, KeyAction a)
{
    WhatsNew *w = s->st;
    app_redraw();
    switch (a) {
    case KEY_DOWN: if (w->first + 1 < w->n) w->first++; return 1;
    case KEY_UP: if (w->first > 0) w->first--; return 1;
    case KEY_OK: case KEY_BACK: app_pop(); return 1;
    default: return 1;
    }
}

static void destroy(Screen *s)
{
    WhatsNew *w = s->st;
    free(w->notes);
    free(w);
    free(s);
}

Screen *screen_whatsnew_create(int about)
{
    Screen *s = calloc(1, sizeof *s);
    WhatsNew *w = calloc(1, sizeof *w);
    w->about = about;
    w->notes = calloc(ITEMS_MAX, sizeof *w->notes);
    w->n = notes_parse(WHATSNEW, device(), w->notes, ITEMS_MAX);
    s->name = "whatsnew"; s->st = w;
    s->draw = draw; s->key = key; s->destroy = destroy; s->phone = 1;
    return s;
}

/* After an update (not a first install): the notes once, at the first start of the new
 * version, when any of them are for this device. The version seen last is kept either way. */
void app_whatsnew_check(void)
{
    const char *s = store_get_str("seenVersion", NULL);
    char was[32];
    snprintf(was, sizeof was, "%s", s ? s : "");   /* kept: setting it below replaces the stored string */
    const char *seen = s ? was : NULL;
    int updated = seen ? strcmp(seen, SNTV_APP_VERSION) != 0 : store_get_int("setupDone", 0);   /* before 0.10.59 nothing was kept: set up means updated */
    store_set_str("seenVersion", SNTV_APP_VERSION);
    store_save();
    const char *quiet = getenv("SNTV_NO_WHATSNEW");   /* tests */
    if (!updated || (quiet && *quiet) || !WHATSNEW[0]) return;
    Note *probe = calloc(1, sizeof *probe);
    int any = probe && notes_parse(WHATSNEW, device(), probe, 1);   /* nothing for this device: not worth an interruption */
    free(probe);
    if (!any) { LOGI("whatsnew: updated from %s to %s, nothing in it for this device", seen ? seen : "an earlier version", SNTV_APP_VERSION); return; }
    LOGI("whatsnew: updated from %s to %s", seen ? seen : "an earlier version", SNTV_APP_VERSION);
    app_push(screen_whatsnew_create(0));
}
