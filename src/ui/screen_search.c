#include "../app.h"
#include "../http.h"
#include "../log.h"
#include "font.h"
#include "keyboard.h"
#include "theme.h"
#include "widgets.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

enum { COLS = 5, CELL_W = 196, CELL_H = 294, CELL_GAP = 28, GRID_ROWS_VISIBLE = 3 };

typedef struct Search Search;
typedef struct { Search *owner; unsigned generation; } Pending;

struct Search {
    Keyboard kb;
    int focus_grid;
    MetaPreview *results; int n;
    int grid_focus;
    float debounce;
    int dirty;
    unsigned generation;      /* increments per query; stale replies are dropped */
    float focus_t, grid_scroll;
    int pending;
    char last_query[256];
};

static void clear_results(Search *s)
{
    meta_previews_free(s->results, s->n);
    s->results = NULL; s->n = 0; s->grid_focus = 0;
}

static void on_result(HttpResponse *r, void *user)
{
    Pending *p = user;
    Search *s = p->owner;
    if (p->generation != s->generation) { free(p); return; }
    s->pending--;
    MetaPreview *metas; int n;
    if (r->status == 200 && addon_parse_metas(r->body, &metas, &n) == 0) {
        s->results = realloc(s->results, sizeof(MetaPreview) * (size_t)(s->n + n));
        for (int i = 0; i < n; i++) {
            int dup = 0;
            for (int j = 0; j < s->n; j++) if (strcmp(s->results[j].id, metas[i].id) == 0) { dup = 1; break; }
            if (dup) meta_preview_free(&metas[i]); else s->results[s->n++] = metas[i];
        }
        free(metas);
    }
    app_redraw();
    free(p);
}

static void run_query(Search *s)
{
    const char *q = s->kb.text;
    if (strcmp(q, s->last_query) == 0) return;
    snprintf(s->last_query, sizeof s->last_query, "%s", q);
    s->generation++;
    s->pending = 0;
    clear_results(s);
    if (strlen(q) < 2) return;
    char extra[600];
    const char *kv[] = { "search", q, NULL };
    addon_extra_query(extra, sizeof extra, kv);
    for (int a = 0; a < g_app.n_addons; a++) {
        const Addon *ad = &g_app.addons[a];
        for (int c = 0; c < ad->manifest.n_catalogs; c++) {
            const AddonCatalog *cat = &ad->manifest.catalogs[c];
            if (!addon_catalog_extra(cat, "search")) continue;
            char url[1200];
            addon_url_catalog(ad, cat->type, cat->id, extra, url, sizeof url);
            Pending *p = malloc(sizeof *p);
            p->owner = s; p->generation = s->generation;
            s->pending++;
            http_request_async(url, NULL, on_result, p);
        }
    }
}

static void update(Screen *sc, float dt)
{
    Search *s = sc->st;
    int row = s->grid_focus / COLS;
    float target = (row > 1 ? row - 1 : 0) * (float)(CELL_H + 80);
    s->grid_scroll = ui_ease(s->grid_scroll, target, NULL, dt, 14);
    s->focus_t = ui_ease(s->focus_t * 100, 100, NULL, dt, 16) / 100;
    if (s->dirty) { s->debounce -= dt; if (s->debounce <= 0) { s->dirty = 0; run_query(s); } }
    if (s->pending || s->dirty) app_redraw();
}

static void draw(Screen *sc)
{
    Search *s = sc->st;
    const MetaPreview *fp = s->focus_grid && s->grid_focus < s->n ? &s->results[s->grid_focus] : NULL;
    char buf[512];
    const char *bg = fp ? app_art_background(fp, buf, sizeof buf) : NULL;
    ui_background(bg ? image_tint(bg) : 0);
    font_draw("Search", THEME_MARGIN, 52, FONT_L | FONT_B, THEME_TEXT);

    float x = THEME_MARGIN, y = 140, fw = keyboard_width();
    /* field */
    gfx_rrect(x, y, fw, 76, 38, RGBA(0xFF, 0xFF, 0xFF, 0x18));
    if (!s->focus_grid) gfx_rrect_ring(x, y, fw, 76, 38, 2, RGBA(0xFF, 0xFF, 0xFF, 0x50));
    ui_icon(ICON_SEARCH, x + 42, y + 38, 30, THEME_TEXT_DIM);
    if (s->kb.text[0]) {
        font_draw_fit(s->kb.text, x + 78, y + (76 - font_height(FONT_M | FONT_B)) / 2, FONT_M | FONT_B, THEME_TEXT, fw - 120);
        if (!s->focus_grid && fmodf(g_app.time, 1.0f) < 0.55f) {
            float cx = x + 78 + font_measure(s->kb.text, FONT_M | FONT_B) + 4;
            if (cx < x + fw - 30) gfx_rrect(cx, y + 20, 3, 36, 1.5f, THEME_ACCENT2);
        }
        if (!s->focus_grid) app_redraw();
    } else font_draw("Movies, series, people\xE2\x80\xA6", x + 78, y + (76 - font_height(FONT_M)) / 2, FONT_M, THEME_TEXT_MUTED);
    keyboard_draw(&s->kb, x, y + 108, !s->focus_grid);

    float gx = x + fw + 80, gy = y;
    float grid_w = SNTV_UI_W - THEME_MARGIN - gx;
    if (s->pending && !s->n) {
        for (int i = 0; i < COLS * 2; i++) ui_skeleton(gx + (i % COLS) * (CELL_W + CELL_GAP), gy + 50 + (i / COLS) * (CELL_H + 80), CELL_W, CELL_H, R_CARD);
    } else if (!s->n) {
        float cx = gx + grid_w / 2, cy = gy + 280;
        gfx_circle(cx, cy, 80, RGBA(0xFF, 0xFF, 0xFF, 0x10));
        ui_icon(ICON_SEARCH, cx, cy, 70, THEME_TEXT_MUTED);
        font_draw_center(strlen(s->last_query) >= 2 ? "Nothing found" : "Find something to watch", cx, cy + 120, FONT_L | FONT_B, THEME_TEXT);
        font_draw_center(strlen(s->last_query) >= 2 ? "Try another title, or check your catalog addons." : "Results come from every catalog addon that supports search.",
                         cx, cy + 178, FONT_S, THEME_TEXT_DIM);
    } else {
        char hdr[96];
        snprintf(hdr, sizeof hdr, "%d result%s", s->n, s->n == 1 ? "" : "s");
        font_draw(hdr, gx, gy + 4, FONT_S | FONT_B, THEME_TEXT_MUTED);
        if (s->pending) ui_spinner(gx + font_measure(hdr, FONT_S | FONT_B) + 40, gy + 18, 14);
    }
    gfx_clip_push(gx - 30, gy + 40, grid_w + 60, SNTV_UI_H - gy - 40);
    float row_h = CELL_H + 80;
    for (int pass = 0; pass < 2; pass++)
        for (int i = 0; i < s->n; i++) {
            int r = i / COLS, c = i % COLS;
            float px = gx + c * (CELL_W + CELL_GAP), py = gy + 60 + r * row_h - s->grid_scroll;
            if (py > SNTV_UI_H || py + row_h < gy) continue;
            int foc = s->focus_grid && s->grid_focus == i;
            if ((pass == 1) != foc) continue;
            char pb[512];
            ui_card(app_art_poster(&s->results[i], pb, sizeof pb), px, py, CELL_W, CELL_H, foc ? (g_app.fx_light ? 1 : s->focus_t) : 0,
                    s->focus_grid && !foc ? 0.3f : 0, s->results[i].name, 0);
            font_draw_fit(s->results[i].name, px, py + CELL_H + (foc ? 24 : 12), FONT_XS | FONT_B, foc ? THEME_TEXT : THEME_TEXT_DIM, CELL_W);
            if (s->results[i].release_info) font_draw(s->results[i].release_info, px, py + CELL_H + (foc ? 50 : 38), FONT_XS, THEME_TEXT_MUTED);
        }
    gfx_clip_pop();
}

static int key(Screen *sc, KeyAction a)
{
    Search *s = sc->st;
    app_redraw();
    if (!s->focus_grid) {
        if (a == KEY_RIGHT && s->kb.col == 10 && s->n) { s->focus_grid = 1; return 1; }
        if (a == KEY_BACKSPACE) { keyboard_key(&s->kb, KEY_BACKSPACE); s->dirty = 1; s->debounce = 0.5f; return 1; }
        KeyboardResult r = keyboard_key(&s->kb, a);
        if (r == KB_CHANGED) { s->dirty = 1; s->debounce = 0.6f; }
        else if (r == KB_DONE) { s->dirty = 0; run_query(s); if (s->n) s->focus_grid = 1; }
        return a != KEY_BACK;
    }
    s->focus_t = 0;
    switch (a) {
    case KEY_LEFT: if (s->grid_focus % COLS == 0) s->focus_grid = 0; else s->grid_focus--; return 1;
    case KEY_RIGHT: if (s->grid_focus % COLS < COLS - 1 && s->grid_focus + 1 < s->n) s->grid_focus++; return 1;
    case KEY_UP: if (s->grid_focus >= COLS) s->grid_focus -= COLS; return 1;
    case KEY_DOWN: if (s->grid_focus + COLS < s->n) s->grid_focus += COLS; return 1;
    case KEY_OK: if (s->grid_focus < s->n) app_push(screen_detail_create(&s->results[s->grid_focus])); return 1;
    case KEY_BACK: s->focus_grid = 0; return 1;
    default: return 0;
    }
}

static void text(Screen *sc, const char *utf8)
{
    Search *s = sc->st;
    keyboard_text_input(&s->kb, utf8);
    s->dirty = 1; s->debounce = 0.6f;
    app_redraw();
}

static void destroy(Screen *sc)
{
    Search *s = sc->st;
    s->generation++;   /* orphan in-flight replies */
    clear_results(s);
    SDL_StopTextInput();
    free(s);
    free(sc);
}

Screen *screen_search_create(void)
{
    Screen *sc = calloc(1, sizeof *sc);
    Search *s = calloc(1, sizeof *s);
    keyboard_init(&s->kb, 0, NULL);
    sc->name = "search"; sc->st = s;
    sc->update = update; sc->draw = draw; sc->key = key; sc->text = text; sc->destroy = destroy;
    SDL_StartTextInput();
    return sc;
}
