#include "../app.h"
#include "../http.h"
#include "../log.h"
#include "font.h"
#include "keyboard.h"
#include "theme.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { COLS = 4, CELL_W = 190, CELL_H = 285, CELL_GAP = 20, GRID_ROWS_VISIBLE = 3 };

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
    if (s->dirty) { s->debounce -= dt; if (s->debounce <= 0) { s->dirty = 0; run_query(s); } }
}

static void draw(Screen *sc)
{
    Search *s = sc->st;
    gfx_rect(0, 0, SNTV_UI_W, SNTV_UI_H, THEME_BG);
    ui_draw_header("Search", NULL);
    float x = THEME_MARGIN, y = 150;
    gfx_rect(x, y, keyboard_width(), 64, THEME_SURFACE);
    if (s->kb.text[0]) font_draw_fit(s->kb.text, x + 20, y + 16, FONT_M, THEME_TEXT, keyboard_width() - 40);
    else font_draw("Type a title...", x + 20, y + 16, FONT_M, THEME_TEXT_MUTED);
    keyboard_draw(&s->kb, x, y + 90, !s->focus_grid);

    float gx = x + keyboard_width() + 60, gy = y;
    float grid_w = COLS * (CELL_W + CELL_GAP) - CELL_GAP;
    if (s->pending) ui_draw_spinner(gx + grid_w / 2, gy + 200);
    else if (!s->n && strlen(s->last_query) >= 2) font_draw("No results", gx, gy, FONT_M, THEME_TEXT_DIM);
    int first_row = s->grid_focus / COLS - (GRID_ROWS_VISIBLE - 1);
    if (first_row < 0) first_row = 0;
    gfx_clip_push(gx - 20, gy - 20, grid_w + 40, SNTV_UI_H - gy);
    for (int i = first_row * COLS; i < s->n && i < (first_row + GRID_ROWS_VISIBLE) * COLS; i++) {
        int r = i / COLS - first_row, c = i % COLS;
        float px = gx + c * (CELL_W + CELL_GAP), py = gy + r * (CELL_H + CELL_GAP);
        int foc = s->focus_grid && s->grid_focus == i;
        ui_draw_poster(s->results[i].poster, px, py, CELL_W, CELL_H, foc, s->results[i].name);
    }
    gfx_clip_pop();
    if (s->focus_grid && s->grid_focus < s->n) {
        const MetaPreview *p = &s->results[s->grid_focus];
        char line[300];
        snprintf(line, sizeof line, "%s%s%s%s%s", p->name, p->release_info ? "  (" : "", p->release_info ? p->release_info : "", p->release_info ? ")" : "", p->type && strcmp(p->type, "series") == 0 ? "  Series" : "");
        font_draw_fit(line, gx, SNTV_UI_H - 90, FONT_M, THEME_TEXT, SNTV_UI_W - gx - THEME_MARGIN);
    }
}

static int key(Screen *sc, KeyAction a)
{
    Search *s = sc->st;
    if (!s->focus_grid) {
        if (a == KEY_RIGHT && s->kb.col == 10 && s->n) { s->focus_grid = 1; return 1; }
        if (a == KEY_BACKSPACE) { keyboard_key(&s->kb, KEY_BACKSPACE); s->dirty = 1; s->debounce = 0.5f; return 1; }
        KeyboardResult r = keyboard_key(&s->kb, a);
        if (r == KB_CHANGED) { s->dirty = 1; s->debounce = 0.6f; }
        else if (r == KB_DONE) { s->dirty = 0; run_query(s); if (s->n) s->focus_grid = 1; }
        return a != KEY_BACK;
    }
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
