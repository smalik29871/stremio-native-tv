/* What's new: this version's notes from CHANGELOG.md (built in by tools/whatsnew.cmake).
 * Shown once after an update, and from Settings > About (there under what Cuelight is). Up/Down scroll, OK or Back
 * closes it. */
#include "../app.h"
#include "../log.h"
#include "../store.h"
#include "../platform/platform.h"
#include "whatsnew.h"
#include "font.h"
#include "theme.h"
#include "widgets.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { ITEMS_MAX = 40 };
#define TEXT_X 160.0f
#define TEXT_W 1240.0f
#define TOP 300.0f

typedef struct {
    char *items[ITEMS_MAX]; int n;
    int first;                /* the item at the top */
    int about;                /* Settings > About: what Cuelight is, above the notes */
} WhatsNew;

static const char *const ABOUT[] = {
    SNTV_APP_NAME " is a player made for TVs: written from scratch in C for the TV's own hardware, so it starts fast and stays smooth on a modest TV.",
    "It plays from addons in the open Stremio addon format, has its own catalog of films and series (from TMDB), and needs no account. "
    "Your addons, Library and progress stay on this TV, or sync with Stremio, Trakt or SimKL if you choose.",
    SNTV_APP_NAME " is not affiliated with or endorsed by Stremio. What you watch comes from the addons you install.",
    NULL,
};

/* "- a **bold** `word`\n  more" -> one plain line per item */
static void parse(WhatsNew *w)
{
    const char *p = WHATSNEW;
    while (*p && w->n < ITEMS_MAX) {
        size_t len = strcspn(p, "\n");
        const char *line = p;
        p += len + (p[len] == '\n');
        while (len && (*line == ' ' || *line == '\t')) { line++; len--; }
        if (!len) continue;
        int starts = len > 1 && line[0] == '-' && line[1] == ' ';
        if (starts) { line += 2; len -= 2; }
        if (starts || !w->n) {
            w->items[w->n] = calloc(1, 1);
            w->n++;
        }
        char *it = w->items[w->n - 1];
        size_t have = strlen(it);
        it = realloc(it, have + len + 2);
        size_t o = have;
        if (have) it[o++] = ' ';
        for (size_t i = 0; i < len; i++) {
            if (line[i] == '`' || (line[i] == '*' && i + 1 < len && line[i + 1] == '*')) { if (line[i] == '*') i++; continue; }
            it[o++] = line[i];
        }
        it[o] = 0;
        w->items[w->n - 1] = it;
    }
}

static void draw(Screen *s)
{
    WhatsNew *w = s->st;
    gfx_rect(0, 0, SNTV_UI_W, SNTV_UI_H, THEME_BG);
    ui_brand(TEXT_X, 64, 48);
    char title[96];
    snprintf(title, sizeof title, w->about ? "About " SNTV_APP_NAME : "What's new in %s", SNTV_APP_VERSION);
    if (g_theme.id == THEME_APERTURE) font_draw(title, TEXT_X, 160, FONT_XL | FONT_ALT, THEME_TEXT);
    else font_draw(title, TEXT_X, 170, FONT_L | FONT_B, THEME_TEXT);
    float y = TOP, lh = font_height(FONT_M) + 6;
    if (w->about) {
        font_draw("Version " SNTV_APP_VERSION, TEXT_X, 236, FONT_S, THEME_TEXT_DIM);
        y = TOP - 10;
        for (int k = 0; ABOUT[k]; k++) y += font_draw_wrapped(ABOUT[k], TEXT_X, y, FONT_S, THEME_TEXT_DIM, TEXT_W, 3) * font_height(FONT_S) + 18;
        y += 24;
        font_draw("What's new in " SNTV_APP_VERSION, TEXT_X, y, FONT_M | FONT_B, THEME_TEXT);
        y += font_height(FONT_M) + 24;
    } else font_draw(w->n ? "This update" : "Nothing listed for this version", TEXT_X, 236, FONT_S, THEME_TEXT_DIM);
    int i = w->first;
    for (; i < w->n && y < SNTV_UI_H - 170; i++) {
        gfx_circle(TEXT_X + 8, y + lh / 2, 6, THEME_ACCENT);
        int lines = font_draw_wrapped(w->items[i], TEXT_X + 36, y, FONT_M, THEME_TEXT, TEXT_W, 4);
        y += (lines ? lines : 1) * font_height(FONT_M) + 28;
    }
    const char *hint = i < w->n ? "Down: more   \xC2\xB7   OK: close" : "OK: close";   /* some still below */
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
    for (int i = 0; i < w->n; i++) free(w->items[i]);
    free(w);
    free(s);
}

Screen *screen_whatsnew_create(int about)
{
    Screen *s = calloc(1, sizeof *s);
    WhatsNew *w = calloc(1, sizeof *w);
    w->about = about;
    parse(w);
    s->name = "whatsnew"; s->st = w;
    s->draw = draw; s->key = key; s->destroy = destroy;
    return s;
}

/* After an update (not a first install): the notes once, at the first start of the new
 * version. The version seen last is kept either way. */
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
    LOGI("whatsnew: updated from %s to %s", seen ? seen : "an earlier version", SNTV_APP_VERSION);
    app_push(screen_whatsnew_create(0));
}
