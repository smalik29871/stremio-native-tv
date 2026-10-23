/* Your rating of a title or an episode: five stars in half steps, a few quick tags, and
 * Save or Remove. Left/Right sets the stars, Down the tags and buttons; OK on the stars
 * saves; Back leaves the rating as it was. Sent to Trakt and SimKL when connected. */
#include "../app.h"
#include "../log.h"
#include "../ratings.h"
#include "../tracking.h"
#include "font.h"
#include "theme.h"
#include "widgets.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char id[96], type[16], title[160], sub[200], tags[256];   /* tags: as chosen here, saved with Save */
    int value, orig, row, tag, btn;   /* row: 0 the stars, 1 the tags, 2 the buttons */
    float t;
} Rate;

enum { HIT_STAR = 0x100, HIT_TAG = 0x200, HIT_SAVE = 0x300, HIT_REMOVE, HIT_OUTSIDE, HIT_PANEL };

/* a five-pointed star, filled from the left up to `frac` of its width */
static void star(float cx, float cy, float r, float frac, Color on, Color off)
{
    float pts[11][2];
    for (int i = 0; i <= 10; i++) {
        float a = -1.5708f + i * 0.6283f, rr = (i & 1) ? r * 0.42f : r;
        pts[i][0] = cx + cosf(a) * rr; pts[i][1] = cy + sinf(a) * rr;
    }
    /* opaque colours: the lines overlap, so a see-through one would build up in streaks */
    for (int pass = 0; pass < 2; pass++) {
        if (pass == 1) { if (frac <= 0) break; gfx_clip_push(cx - r, cy - r, 2 * r * frac, 2 * r); }
        Color c = pass ? on : off;
        for (int i = 0; i < 10; i++)   /* a fan of lines from the centre fills it */
            for (int k = 0; k <= 14; k++) {
                float u = k / 14.0f, px = pts[i][0] + (pts[i + 1][0] - pts[i][0]) * u, py = pts[i][1] + (pts[i + 1][1] - pts[i][1]) * u;
                gfx_line(cx, cy, px, py, r * 0.09f, c);
            }
        if (pass == 1) gfx_clip_pop();
    }
}

static void save(Rate *r, int value)
{
    MetaPreview m = { 0 };
    m.type = r->type;
    rating_set(&m, r->id, value);
    rating_set_tags(r->id, r->tags);
    if (value && r->tags[0]) LOGI("ratings: %s tagged %s", r->id, r->tags);
    char v[16];
    rating_text(value, v, sizeof v);
    int synced = tracking_connected(TRACK_TRAKT) || (tracking_connected(TRACK_SIMKL) && !strchr(r->id, ':'));
    if (value) app_toast("Rated %s of 5%s", v, synced ? ", sent to your tracker" : "");
    else app_toast("Rating removed");
    app_pop();
}

static void update(Screen *s, float dt)
{
    Rate *r = s->st;
    if (r->t < 1) { r->t += dt / 0.28f; if (r->t > 1) r->t = 1; app_redraw(); }
}

static void draw(Screen *s)
{
    Rate *r = s->st;
    float k = g_app.fx_light ? 1 : 1 - (1 - r->t) * (1 - r->t) * (1 - r->t);
    Screen *below = g_app.depth >= 2 ? g_app.stack[g_app.depth - 2] : NULL;
    if (below && below->draw) { ui_hits_begin(below); below->draw(below); ui_hits_begin(s); } else ui_background(0);
    gfx_rect(0, 0, SNTV_UI_W, SNTV_UI_H, RGBA(0, 0, 0, (int)(150 * k)));
    ui_hit(0, 0, SNTV_UI_W, SNTV_UI_H, HIT_OUTSIDE);
    float w = SNTV_UI_W - 40 < 860 ? SNTV_UI_W - 40 : 860, h = SNTV_PHONE ? 760 : 640;
    float x = (SNTV_UI_W - w) / 2, y = (SNTV_UI_H - h) / 2 + (1 - k) * 50, cx = SNTV_UI_W / 2;
    gfx_set_alpha(k);
    if (!g_app.fx_light) gfx_shadow(x, y + 16, w, h, R_PANEL, 40, RGBA(0, 0, 0, 0x90));
    ui_panel(x, y, w, h, R_PANEL, THEME_PANEL);
    ui_hit(x, y, w, h, HIT_PANEL);
    font_draw_center(r->title, cx, y + 36, FONT_M | FONT_B, THEME_TEXT);
    if (r->sub[0]) font_draw_center(r->sub, cx, y + 84, FONT_S, THEME_TEXT_DIM);
    /* the stars */
    float sr = 52, gap = 128, sy = y + 210;
    Color on = RGBA(0xFF, 0xC8, 0x3D, 0xFF), off = RGBA(0x4A, 0x4A, 0x50, 0xFF);
    if (r->row == 0) gfx_rrect_ring(cx - 2.5f * gap - 10, sy - sr - 20, 5 * gap + 20, 2 * sr + 40, R_PANEL, 3, THEME_FOCUS);
    for (int i = 0; i < 5; i++) {
        float scx = cx + (i - 2) * gap, f = r->value >= (i + 1) * 2 ? 1 : r->value == i * 2 + 1 ? 0.5f : 0;
        star(scx, sy, sr, f, on, off);
        ui_hit(scx - gap / 2, sy - sr - 10, gap / 2, 2 * sr + 20, HIT_STAR + i * 2 + 1);   /* the left half: a half star */
        ui_hit(scx, sy - sr - 10, gap / 2, 2 * sr + 20, HIT_STAR + i * 2 + 2);
    }
    char v[48];
    if (r->value) { char t[16]; rating_text(r->value, t, sizeof t); snprintf(v, sizeof v, "%s of 5", t); }
    else snprintf(v, sizeof v, "Not rated yet");
    font_draw_center(v, cx, sy + sr + 34, FONT_S | FONT_B, r->value ? THEME_TEXT : THEME_TEXT_MUTED);
    /* the quick tags */
    float ty = sy + sr + 110, tx0 = x + 44, tx = tx0, row_h = 64;
    for (int i = 0; i < RATING_TAGS_N; i++) {
        float tw = font_measure(RATING_TAGS[i], FONT_S | FONT_B) + 44;
        if (tx + tw > x + w - 44) { tx = tx0; ty += row_h; }
        int has = rating_tag_has(r->tags, RATING_TAGS[i]);
        int foc = r->row == 1 && r->tag == i;
        gfx_rrect(tx, ty, tw, 52, 26, foc ? THEME_FOCUS : has ? COLOR_ALPHA(THEME_ACCENT, 0x50) : COLOR_ALPHA(THEME_TEXT, 0x16));
        if (has && !foc) gfx_rrect_ring(tx, ty, tw, 52, 26, 2, THEME_ACCENT);
        font_draw(RATING_TAGS[i], tx + 22, ty + (52 - font_height(FONT_S | FONT_B)) / 2, FONT_S | FONT_B, foc ? THEME_ON_FOCUS : THEME_TEXT);
        ui_hit(tx, ty, tw, 52, HIT_TAG + i);
        tx += tw + 12;
    }
    /* Save, and Remove when it was rated */
    float by = y + h - 110;
    float bw = ui_pill("Save", 1, ICON_CHECK, x + 44, by, 64, PILL_PRIMARY, r->row == 2 && r->btn == 0);
    ui_hit(x + 44, by, bw, 64, HIT_SAVE);
    if (r->orig) {
        float rw = ui_pill("Remove rating", 1, ICON_CLOSE, x + 44 + bw + 16, by, 64, PILL_SECONDARY, r->row == 2 && r->btn == 1);
        ui_hit(x + 44 + bw + 16, by, rw, 64, HIT_REMOVE);
    }
    const char *hint = SNTV_PHONE ? "Tap the stars, a half for half a star" : "Left / Right: stars   \xC2\xB7   OK: save   \xC2\xB7   Back: leave it";
    font_draw(hint, x + w - 44 - font_measure(hint, FONT_XS), by + 22, FONT_XS, THEME_TEXT_MUTED);
    gfx_set_alpha(1);
}

static int key(Screen *s, KeyAction a)
{
    Rate *r = s->st;
    app_redraw();
    switch (a) {
    case KEY_LEFT:
        if (r->row == 0) { if (r->value > 1) r->value--; }
        else if (r->row == 1) { if (r->tag > 0) r->tag--; }
        else if (r->btn > 0) r->btn--;
        return 1;
    case KEY_RIGHT:
        if (r->row == 0) { if (r->value < 10) r->value++; }
        else if (r->row == 1) { if (r->tag < RATING_TAGS_N - 1) r->tag++; }
        else if (r->orig && r->btn < 1) r->btn++;
        return 1;
    case KEY_DOWN: if (r->row < 2) r->row++; return 1;
    case KEY_UP: if (r->row > 0) r->row--; return 1;
    case KEY_OK:
        if (r->row == 1) { rating_tag_toggle(r->tags, sizeof r->tags, RATING_TAGS[r->tag]); return 1; }
        if (r->row == 2 && r->btn == 1) { save(r, 0); return 1; }
        save(r, r->value);
        return 1;
    case KEY_BACK: app_pop(); return 1;
    default: return 1;
    }
}

static int tap(Screen *s, int id)
{
    Rate *r = s->st;
    if (g_touch_aim) return 1;
    if (id == HIT_OUTSIDE) { app_pop(); return 1; }
    if (id > HIT_STAR && id <= HIT_STAR + 10) { r->row = 0; r->value = id - HIT_STAR; app_redraw(); return 1; }
    if (id >= HIT_TAG && id < HIT_TAG + RATING_TAGS_N) { r->row = 1; r->tag = id - HIT_TAG; rating_tag_toggle(r->tags, sizeof r->tags, RATING_TAGS[r->tag]); app_redraw(); return 1; }
    if (id == HIT_SAVE) { save(r, r->value); return 1; }
    if (id == HIT_REMOVE) { save(r, 0); return 1; }
    return 1;
}

static void destroy(Screen *s) { free(s->st); free(s); }

Screen *screen_rate_create(const char *id, const char *type, const char *title, const char *subtitle)
{
    Screen *s = calloc(1, sizeof *s);
    Rate *r = calloc(1, sizeof *r);
    snprintf(r->id, sizeof r->id, "%s", id ? id : "");
    snprintf(r->type, sizeof r->type, "%s", type ? type : "movie");
    snprintf(r->title, sizeof r->title, "%s", title ? title : "Your rating");
    snprintf(r->sub, sizeof r->sub, "%s", subtitle ? subtitle : "");
    r->value = r->orig = rating_get(r->id);
    snprintf(r->tags, sizeof r->tags, "%s", rating_tags(r->id));
    if (!r->value) r->value = 8;   /* a start in the middle: four stars */
    LOGI("ratings: rating %s (now %d)", r->id, r->orig);
    s->name = "rate"; s->st = r;
    s->update = update; s->draw = draw; s->key = key; s->tap = tap; s->destroy = destroy;
    Screen *under = app_top();
    s->phone = under ? under->phone : 1;
    return s;
}
