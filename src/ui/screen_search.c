#include "../app.h"
#include "../http.h"
#include "../ai.h"
#include "../store.h"
#include "../../vendor/cJSON.h"
#include "../log.h"
#include "../phone.h"
#include "qrcodegen.h"
#include "font.h"
#include "keyboard.h"
#include "theme.h"
#include "widgets.h"
#include "screen_options.h"
#include <stdio.h>
#include <strings.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* Results sit beside the keyboard while typing and use the full width once it closes. */
enum { COLS_TYPING = 3, COLS = 7, CELL_W = 196, CELL_H = 294, CELL_GAP = 28, FIELD_Y = 140, FIELD_H = 76 };

typedef struct Search Search;
typedef struct { Search *owner; unsigned generation; int pick; int rank; char want[160]; } Pending;

struct Search {
    Keyboard kb;
    int typing;               /* the keyboard is open; OK on the search bar opens it */
    int focus_grid;           /* 0 the search bar (or the keyboard), 1 results, 2 recent searches */
    uint8_t qr[qrcodegen_BUFFER_LEN_MAX]; int qr_ready; char phone_url[96];   /* or type on the phone */
    MetaPreview *results; int n;
    int grid_focus;
    float debounce;
    int dirty;
    unsigned generation;      /* increments per query; stale replies are dropped */
    float focus_t, grid_scroll;
    Peek peek;                /* details beside the focused result */
    int pending;
    char last_query[256];
    unsigned ai_req; int ai_state;   /* 0 off, 1 asking, 2 answered, 3 failed */
    int n_picks;                     /* results at the front that the assistant suggested */
    int pick_rank[16];               /* the assistant's order for those, so they stay in it */
    char ai_note[160];
    char recent[10][96]; int n_recent, recent_focus;   /* shown while the field is empty */
};

static void recent_load(Search *s)
{
    s->n_recent = 0;
    const cJSON *arr = store_get_json("recentSearches"), *it;
    cJSON_ArrayForEach(it, arr) if (cJSON_IsString(it) && s->n_recent < 10) snprintf(s->recent[s->n_recent++], 96, "%s", it->valuestring);
}

static void recent_save(Search *s)
{
    cJSON *arr = cJSON_CreateArray();
    for (int i = 0; i < s->n_recent; i++) cJSON_AddItemToArray(arr, cJSON_CreateString(s->recent[i]));
    store_set_json("recentSearches", arr);
    store_save();
}

/* A search that found something goes to the top of the list (a repeat moves up). */
static void recent_add(Search *s, const char *q)
{
    if (strlen(q) < 2) return;
    int at = -1;
    for (int i = 0; i < s->n_recent; i++) if (strcasecmp(s->recent[i], q) == 0) at = i;
    if (at == 0) return;
    if (at < 0) at = s->n_recent < 10 ? s->n_recent++ : 9;
    memmove(s->recent[1], s->recent[0], (size_t)at * 96);
    snprintf(s->recent[0], 96, "%s", q);
    recent_save(s);
}

static void clear_results(Search *s)
{
    meta_previews_free(s->results, s->n);
    s->results = NULL; s->n = 0; s->grid_focus = 0; s->n_picks = 0;
}

static int same_title(const char *a, const char *b)
{
    while (*a && *b) {
        unsigned ca = (unsigned char)*a, cb = (unsigned char)*b;
        if (ca >= 'A' && ca <= 'Z') ca += 32;
        if (cb >= 'A' && cb <= 'Z') cb += 32;
        if (ca != cb) return 0;
        a++; b++;
    }
    return !*b && (!*a || *a == ' ' || *a == ':' || *a == '(');
}

static void on_result(HttpResponse *r, void *user)
{
    Pending *p = user;
    Search *s = p->owner;
    if (p->generation != s->generation) { free(p); return; }
    s->pending--;
    MetaPreview *metas; int n;
    if (p->pick) {   /* a title the assistant suggested: keep the one result that is that title, at the front, in the assistant's order */
        if (r->parsed) { ParsedMetas *pm = r->parsed; r->parsed = NULL; metas = pm->m; n = pm->n; free(pm);
            int took = 0;
            for (int i = 0; i < n; i++) {
                int dup = -1;
                for (int j = 0; j < s->n; j++) if (strcmp(s->results[j].id, metas[i].id) == 0) { dup = j; break; }
                int want = dup < 0 && !took && s->n_picks < 16 && same_title(metas[i].name, p->want);
                int promote = dup >= s->n_picks && s->n_picks < 16 && same_title(metas[i].name, p->want);
                if (want || promote) {
                    MetaPreview item = want ? metas[i] : s->results[dup];
                    if (promote) memmove(&s->results[s->n_picks + 1], &s->results[s->n_picks], sizeof item * (size_t)(dup - s->n_picks));
                    else { s->results = realloc(s->results, sizeof item * (size_t)(s->n + 1)); memmove(&s->results[s->n_picks + 1], &s->results[s->n_picks], sizeof item * (size_t)(s->n - s->n_picks)); s->n++; took = 1; }
                    int pos = s->n_picks;
                    while (pos > 0 && s->pick_rank[pos - 1] > p->rank) { s->results[pos] = s->results[pos - 1]; s->pick_rank[pos] = s->pick_rank[pos - 1]; pos--; }
                    s->results[pos] = item; s->pick_rank[pos] = p->rank; s->n_picks++;
                    if (s->focus_grid && want) s->grid_focus++;
                } else if (dup < 0) meta_preview_free(&metas[i]);
            }
            free(metas);
        }
        app_redraw();
        free(p);
        return;
    }
    if (r->parsed) { ParsedMetas *pm = r->parsed; r->parsed = NULL; metas = pm->m; n = pm->n; free(pm);
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

static void search_addons(Search *s, const char *q, int pick, int rank)
{
    char extra[600];
    const char *kv[] = { "search", q, NULL };
    addon_extra_query(extra, sizeof extra, kv);
    for (int a = 0; a < g_app.n_addons; a++) {
        const Addon *ad = &g_app.addons[a];
        if (g_app.hide_adult && ad->manifest.bh_adult) continue;
        for (int c = 0; c < ad->manifest.n_catalogs; c++) {
            const AddonCatalog *cat = &ad->manifest.catalogs[c];
            if (!addon_catalog_extra(cat, "search")) continue;
            char url[1200];
            addon_url_catalog(ad, cat->type, cat->id, extra, url, sizeof url);
            Pending *p = calloc(1, sizeof *p);
            p->owner = s; p->generation = s->generation; p->pick = pick; p->rank = rank;
            snprintf(p->want, sizeof p->want, "%s", q);
            s->pending++;
            app_fetch_metas(url, 0, on_result, p);
        }
    }
}

/* "something funny with robots": the assistant names titles, the addons find them */
static void on_ai_titles(const char *text, const char *error, void *user)
{
    Pending *p = user;
    Search *s = p->owner;
    if (p->generation != s->generation) { free(p); return; }
    s->ai_req = 0;
    cJSON *root = text ? cJSON_Parse(text) : NULL;
    const cJSON *titles = root ? cJSON_GetObjectItemCaseSensitive(root, "titles") : NULL;
    int n = 0;
    if (cJSON_IsArray(titles)) {
        const cJSON *it;
        cJSON_ArrayForEach(it, titles) {
            const cJSON *name = cJSON_IsObject(it) ? cJSON_GetObjectItemCaseSensitive(it, "name") : it;
            if (!cJSON_IsString(name) || !name->valuestring[0] || n >= 8) continue;
            search_addons(s, name->valuestring, 1, n);
            n++;
        }
    }
    s->ai_state = n ? 2 : 3;
    if (!n) snprintf(s->ai_note, sizeof s->ai_note, "%s", error ? error : "The assistant had no suggestions");
    if (root) cJSON_Delete(root);
    app_redraw();
    free(p);
}

static int looks_like_a_request(const char *q)
{
    int words = 1, len = 0;
    for (; *q; q++, len++) if (*q == ' ') words++;
    return words >= 3 && len >= 12;
}

static void run_query(Search *s)
{
    const char *q = s->kb.text;
    if (strcmp(q, s->last_query) == 0) return;
    snprintf(s->last_query, sizeof s->last_query, "%s", q);
    s->generation++;
    s->pending = 0;
    s->ai_state = 0; s->ai_req = 0; s->ai_note[0] = 0;
    clear_results(s);
    if (strlen(q) < 2) return;
    if (ai_enabled() && looks_like_a_request(q)) {
        Pending *p = calloc(1, sizeof *p);
        p->owner = s; p->generation = s->generation;
        static const char *SYS = "You recommend films and series to a viewer who describes what they feel like watching. "
            "Reply with JSON only, no markdown: {\"titles\": [{\"name\": \"exact title\", \"year\": 2010}]} with up to 8 well-known titles, best match first. "
            "If the text is just a title or a person's name, return titles that match it.";
        s->ai_req = ai_ask(SYS, q, 300, 1, on_ai_titles, p);
        s->ai_state = s->ai_req ? 1 : 0;
        if (!s->ai_req) free(p);
    }
    search_addons(s, q, 0, 0);
}

static void open_keyboard(Search *s)
{
    s->typing = 1; s->focus_grid = 0;
    input_text(1);
    phone_prompt("Search", "Type a title, a person, or what you feel like watching", s->kb.text);
    char url[96]; uint8_t tmp[qrcodegen_BUFFER_LEN_MAX];
    s->qr_ready = phone_url(url, sizeof url) && qrcodegen_encodeText(url, tmp, s->qr, qrcodegen_Ecc_MEDIUM, qrcodegen_VERSION_MIN, 6, qrcodegen_Mask_AUTO, true);
    snprintf(s->phone_url, sizeof s->phone_url, "%s", s->qr_ready ? url : "");
}

static void close_keyboard(Search *s)
{
    if (!s->typing) return;
    s->typing = 0;
    input_text(0);
    phone_prompt(NULL, NULL, NULL);
}

static int cols(const Search *s) { return s->typing ? COLS_TYPING : COLS; }

static void update(Screen *sc, float dt)
{
    Search *s = sc->st;
    char got[256];
    if (s->typing && phone_take(got, sizeof got)) {   /* sent from the phone: search it right away */
        keyboard_init(&s->kb, 0, got);
        close_keyboard(s);
        s->dirty = 0; run_query(s);
        app_toast("Received from your phone");
        app_redraw();
    }
    int row = s->grid_focus / cols(s);
    float target = (row > 1 ? row - 1 : 0) * (float)(CELL_H + 80);
    s->grid_scroll = ui_ease(s->grid_scroll, target, NULL, dt, 14);
    s->focus_t = ui_ease(s->focus_t * 100, 100, NULL, dt, 16) / 100;
    ui_peek_step(&s->peek, s->focus_grid == 1 && s->grid_focus < s->n ? &s->results[s->grid_focus] : NULL, dt);
    if (s->dirty) { s->debounce -= dt; if (s->debounce <= 0) { s->dirty = 0; run_query(s); } }
    if (s->pending || s->dirty || s->ai_state == 1) app_redraw();
}

static void draw_field(const Search *s, float x, float y, float fw)
{
    int foc = !s->focus_grid;
    gfx_rrect(x, y, fw, FIELD_H, R_PANEL, COLOR_ALPHA(THEME_TEXT, foc ? 0x22 : 0x18));
    if (foc) gfx_rrect_ring(x, y, fw, FIELD_H, R_PANEL, 2, s->typing ? COLOR_ALPHA(THEME_TEXT, 0x50) : THEME_ACCENT2);
    ui_icon(ICON_SEARCH, x + 42, y + FIELD_H / 2, 30, THEME_TEXT_DIM);
    const char *tip = foc && !s->typing ? "OK to type" : NULL;
    float tip_w = tip ? font_measure(tip, FONT_XS | FONT_B) + 40 : 0;
    if (tip) font_draw(tip, x + fw - tip_w, y + (FIELD_H - font_height(FONT_XS | FONT_B)) / 2, FONT_XS | FONT_B, THEME_TEXT_MUTED);
    if (s->kb.text[0]) {
        font_draw_fit(s->kb.text, x + 78, y + (FIELD_H - font_height(FONT_M | FONT_B)) / 2, FONT_M | FONT_B, THEME_TEXT, fw - 120 - tip_w);
        if (s->typing && fmodf(g_app.time, 1.0f) < 0.55f) {
            float cx = x + 78 + font_measure(s->kb.text, FONT_M | FONT_B) + 4;
            if (cx < x + fw - 30) gfx_rrect(cx, y + 20, 3, 36, 1.5f, THEME_ACCENT2);
        }
        if (s->typing) app_redraw();
    } else font_draw("Movies, series, people\xE2\x80\xA6", x + 78, y + (FIELD_H - font_height(FONT_M)) / 2, FONT_M, THEME_TEXT_MUTED);
}

/* Under the keyboard: scan to type on the phone instead. */
static void draw_phone(const Search *s, float x, float y)
{
    if (!s->qr_ready) return;
    float qs = 190;
    if (y + qs > SNTV_UI_H - 30) return;
    ui_qr(s->qr, x, y, qs);
    font_draw("Or type on your phone", x + qs + 30, y + 30, FONT_S | FONT_B, THEME_TEXT);
    font_draw_wrapped("Scan the code, type or paste, then Send. It searches straight away.", x + qs + 30, y + 72, FONT_XS, THEME_TEXT_DIM, keyboard_width() - qs - 30, 3);
    font_draw(s->phone_url, x + qs + 30, y + qs - 30, FONT_XS, THEME_TEXT_MUTED);
}

static void draw(Screen *sc)
{
    Search *s = sc->st;
    const MetaPreview *fp = s->focus_grid && s->grid_focus < s->n ? &s->results[s->grid_focus] : NULL;
    char buf[512];
    const char *bg = fp ? app_art_background(fp, buf, sizeof buf) : NULL;
    ui_background(bg ? image_tint(bg) : 0);
    font_draw("Search", THEME_MARGIN, 52, FONT_L | FONT_B, THEME_TEXT);

    float x = THEME_MARGIN, y = FIELD_Y; int nc = cols(s);
    float fw = s->typing ? keyboard_width() : nc * (CELL_W + CELL_GAP) - CELL_GAP;
    draw_field(s, x, y, fw);
    float gx = x, gy = y + FIELD_H + 40;
    if (s->typing) {
        keyboard_draw(&s->kb, x, y + 108, 1);
        font_draw("Done searches  \xC2\xB7  Back closes the keyboard", x, y + 108 + keyboard_height() + 20, FONT_XS, THEME_TEXT_MUTED);
        draw_phone(s, x, y + 108 + keyboard_height() + 70);
        gx = x + fw + 80; gy = y;
    }
    float grid_w = SNTV_UI_W - THEME_MARGIN - gx;
    if ((s->pending || s->ai_state == 1) && !s->n) {
        for (int i = 0; i < nc * 2; i++) ui_skeleton(gx + (i % nc) * (CELL_W + CELL_GAP), gy + 50 + (i / nc) * (CELL_H + 80), CELL_W, CELL_H, R_CARD);
    } else if (!s->n && !s->kb.text[0] && s->n_recent) {
        font_draw("Recent searches", gx, gy + 4, FONT_S | FONT_B, THEME_TEXT_MUTED);
        float ry = gy + 56;
        for (int i = 0; i <= s->n_recent; i++, ry += 66) {
            int foc = s->focus_grid == 2 && s->recent_focus == i;
            if (foc) gfx_rrect(gx - 16, ry - 6, 640, 58, R_KEY, THEME_FOCUS);
            if (i < s->n_recent) { ui_icon(ICON_CLOCK, gx + 14, ry + 23, 24, foc ? THEME_ON_FOCUS : THEME_TEXT_MUTED); font_draw_fit(s->recent[i], gx + 46, ry + (46 - font_height(FONT_S)) / 2, FONT_S, foc ? THEME_ON_FOCUS : THEME_TEXT, 560); }
            else font_draw("Clear recent searches", gx + 46, ry + (46 - font_height(FONT_S)) / 2, FONT_S, foc ? THEME_ON_FOCUS : THEME_TEXT_DIM);
        }
        if (s->focus_grid != 2) font_draw(s->typing ? "Right from the keyboard picks one" : "Down picks one", gx, ry + 10, FONT_XS, THEME_TEXT_MUTED);
    } else if (!s->n) {
        float cx = gx + grid_w / 2, cy = gy + (s->typing ? 280 : 220);
        gfx_circle(cx, cy, 80, COLOR_ALPHA(THEME_TEXT, 0x10));
        ui_icon(ICON_SEARCH, cx, cy, 70, THEME_TEXT_MUTED);
        font_draw_center(strlen(s->last_query) >= 2 ? "Nothing found" : "Find something to watch", cx, cy + 120, FONT_L | FONT_B, THEME_TEXT);
        font_draw_center(s->ai_note[0] ? s->ai_note : strlen(s->last_query) >= 2 ? "Try another title, or check your catalog addons." : ai_enabled() ? "Type a title, or describe it: \"something funny with robots\"." : "Results come from every catalog addon that supports search.",
                         cx, cy + 178, FONT_S, THEME_TEXT_DIM);
    } else {
        char hdr[160];
        if (s->n_picks) snprintf(hdr, sizeof hdr, "%d AI pick%s first  \xC2\xB7  %d result%s", s->n_picks, s->n_picks == 1 ? "" : "s", s->n, s->n == 1 ? "" : "s");
        else snprintf(hdr, sizeof hdr, "%d result%s%s", s->n, s->n == 1 ? "" : "s", s->ai_state == 1 ? "  \xC2\xB7  asking the assistant" : "");
        font_draw(hdr, gx, gy + 4, FONT_S | FONT_B, THEME_TEXT_MUTED);
        if (s->pending || s->ai_state == 1) ui_spinner(gx + font_measure(hdr, FONT_S | FONT_B) + 40, gy + 18, 14);
    }
    gfx_clip_push(gx - 30, gy + 40, grid_w + 60, SNTV_UI_H - gy - 40);
    float row_h = CELL_H + 80, peek_x = 0, peek_y = 0;
    for (int pass = 0; pass < 2; pass++)
        for (int i = 0; i < s->n; i++) {
            int r = i / nc, c = i % nc;
            float px = gx + c * (CELL_W + CELL_GAP), py = gy + 60 + r * row_h - s->grid_scroll;
            if (py > SNTV_UI_H || py + row_h < gy) continue;
            int foc = s->focus_grid == 1 && s->grid_focus == i;
            if ((pass == 1) != foc) continue;
            char pb[512];
            if (foc) { peek_x = px; peek_y = py; }
            ui_card(app_art_poster(&s->results[i], pb, sizeof pb), px, py, CELL_W, CELL_H, foc ? (g_app.fx_light ? 1 : s->focus_t) : 0,
                    s->focus_grid == 1 && !foc ? 0.3f : 0, s->results[i].name, 0);
            if (i < s->n_picks) { gfx_circle(px + CELL_W - 22, py + 22, 16, COLOR_ALPHA(THEME_PANEL, 0xD0)); ui_icon(ICON_STAR, px + CELL_W - 22, py + 22, 18, THEME_ACCENT2); }
            font_draw_fit(s->results[i].name, px, py + CELL_H + (foc ? 24 : 12), FONT_XS | FONT_B, foc ? THEME_TEXT : THEME_TEXT_DIM, CELL_W);
            if (foc && ui_hold_hint(px, py + CELL_H + 52, CELL_W)) {}
            else if (s->results[i].release_info) font_draw(s->results[i].release_info, px, py + CELL_H + (foc ? 50 : 38), FONT_XS, THEME_TEXT_MUTED);
        }
    if (s->focus_grid == 1 && s->grid_focus < s->n) {
        float f = g_app.fx_light ? 1 : s->focus_t, sc = ui_focus_scale(f), w2 = CELL_W * sc, h2 = CELL_H * sc;
        ui_peek_draw(&s->peek, &s->results[s->grid_focus], peek_x - (w2 - CELL_W) / 2, peek_y - (h2 - CELL_H) / 2 - ui_focus_lift(f), w2, h2, gx - 30, gx + grid_w + 30);
    }
    gfx_clip_pop();
}

/* The keyboard is open: type, Done searches, Back or Right off the edge closes it. */
static int key_typing(Search *s, KeyAction a)
{
    if (a == KEY_BACK) { close_keyboard(s); return 1; }
    if (a == KEY_RIGHT && s->kb.col == 10 && s->n) { close_keyboard(s); s->focus_grid = 1; s->grid_focus = 0; recent_add(s, s->kb.text); return 1; }
    if (a == KEY_RIGHT && s->kb.col == 10 && !s->kb.text[0] && s->n_recent) { close_keyboard(s); s->focus_grid = 2; s->recent_focus = 0; return 1; }
    if (a == KEY_BACKSPACE) { keyboard_key(&s->kb, KEY_BACKSPACE); s->dirty = 1; s->debounce = 0.5f; return 1; }
    KeyboardResult r = keyboard_key(&s->kb, a);
    if (r == KB_CHANGED) { s->dirty = 1; s->debounce = 0.6f; }
    else if (r == KB_DONE) {
        close_keyboard(s);
        s->dirty = 0; run_query(s);
        if (s->n) { s->focus_grid = 1; s->grid_focus = 0; recent_add(s, s->kb.text); }
    }
    return 1;
}

static int key(Screen *sc, KeyAction a)
{
    Search *s = sc->st;
    app_redraw();
    if (a == KEY_LONG_OK || a == KEY_YELLOW) {
        if (s->focus_grid == 1 && s->grid_focus < s->n) { screen_options_for_title(&s->results[s->grid_focus]); return 1; }
        if (a == KEY_LONG_OK) return 0;
    }
    if (s->typing) return key_typing(s, a);
    if (!s->focus_grid) {   /* the search bar */
        switch (a) {
        case KEY_OK: open_keyboard(s); return 1;
        case KEY_DOWN:
            if (s->n) { s->focus_grid = 1; s->grid_focus = 0; s->focus_t = 0; }
            else if (!s->kb.text[0] && s->n_recent) { s->focus_grid = 2; s->recent_focus = 0; }
            return 1;
        case KEY_BACKSPACE: open_keyboard(s); return key_typing(s, a);
        case KEY_BACK: return 0;
        default: return 1;
        }
    }
    s->focus_t = 0;
    if (s->focus_grid == 2) {   /* the recent searches list */
        switch (a) {
        case KEY_UP: if (s->recent_focus > 0) s->recent_focus--; else s->focus_grid = 0; return 1;
        case KEY_DOWN: if (s->recent_focus < s->n_recent) s->recent_focus++; return 1;   /* the last row clears */
        case KEY_BACK: s->focus_grid = 0; return 1;
        case KEY_OK:
            if (s->recent_focus == s->n_recent) { s->n_recent = 0; recent_save(s); s->focus_grid = 0; app_toast("Recent searches cleared"); return 1; }
            keyboard_init(&s->kb, 0, s->recent[s->recent_focus]);
            s->dirty = 0; s->focus_grid = 0; run_query(s);
            return 1;
        default: return 1;
        }
    }
    switch (a) {
    case KEY_LEFT: if (s->grid_focus % COLS) s->grid_focus--; return 1;
    case KEY_RIGHT: if (s->grid_focus % COLS < COLS - 1 && s->grid_focus + 1 < s->n) s->grid_focus++; return 1;
    case KEY_UP: if (s->grid_focus >= COLS) s->grid_focus -= COLS; else s->focus_grid = 0; return 1;
    case KEY_DOWN: if (s->grid_focus + COLS < s->n) { s->grid_focus += COLS; return 1; } return 0;
    case KEY_OK: if (s->grid_focus < s->n) { recent_add(s, s->last_query); app_push(screen_detail_create(&s->results[s->grid_focus])); } return 1;
    case KEY_BACK: s->focus_grid = 0; return 1;
    default: return 0;
    }
}

/* Typing on a real keyboard (or text from the test driver) opens the on-screen one too. */
static void text(Screen *sc, const char *utf8)
{
    Search *s = sc->st;
    if (!s->typing) open_keyboard(s);
    keyboard_text_input(&s->kb, utf8);
    s->dirty = 1; s->debounce = 0.6f;
    app_redraw();
}

static void destroy(Screen *sc)
{
    Search *s = sc->st;
    s->generation++;   /* orphan in-flight replies */
    if (s->ai_req) ai_cancel(s->ai_req);
    clear_results(s);
    close_keyboard(s);
    free(s);
    free(sc);
}

Screen *screen_search_create(void)
{
    Screen *sc = calloc(1, sizeof *sc);
    Search *s = calloc(1, sizeof *s);
    keyboard_init(&s->kb, 0, NULL);
    recent_load(s);
    sc->name = "search"; sc->st = s; sc->long_ok = 1;
    sc->update = update; sc->draw = draw; sc->key = key; sc->text = text; sc->destroy = destroy;
    return sc;
}
