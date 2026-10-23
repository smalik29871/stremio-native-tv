/* One soundtrack album over the title page: its cover, every track with its length, and
 * a code to scan to open it on a phone (a tap opens it on a phone itself). Back closes. */
#include "../app.h"
#include "../log.h"
#include "../music.h"
#include "font.h"
#include "qrcodegen.h"
#include "theme.h"
#include "widgets.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    MusicAlbum a;
    char of[160];
    uint8_t qr[qrcodegen_BUFFER_LEN_MAX];
    int qr_ready;
    float t;
} Sound;

enum { HIT_OUTSIDE = 1, HIT_PANEL, HIT_OPEN };

static void update(Screen *s, float dt)
{
    Sound *o = s->st;
    if (o->t < 1) { o->t += dt / 0.28f; if (o->t > 1) o->t = 1; app_redraw(); }
}

static void draw(Screen *s)
{
    Sound *o = s->st;
    const MusicAlbum *a = &o->a;
    float k = g_app.fx_light ? 1 : 1 - (1 - o->t) * (1 - o->t) * (1 - o->t);
    Screen *below = g_app.depth >= 2 ? g_app.stack[g_app.depth - 2] : NULL;
    if (below && below->draw) { ui_hits_begin(below); below->draw(below); ui_hits_begin(s); } else ui_background(0);
    gfx_rect(0, 0, SNTV_UI_W, SNTV_UI_H, RGBA(0, 0, 0, (int)(170 * k)));
    ui_hit(0, 0, SNTV_UI_W, SNTV_UI_H, HIT_OUTSIDE);
    int phone = SNTV_PHONE;
    float w = phone ? SNTV_UI_W - 40 : 1640, h = phone ? SNTV_UI_H - 120 : 900;
    float x = (SNTV_UI_W - w) / 2, y = (SNTV_UI_H - h) / 2 + (1 - k) * 50, pad = 48;
    gfx_set_alpha(k);
    if (!g_app.fx_light) gfx_shadow(x, y + 16, w, h, R_PANEL, 40, RGBA(0, 0, 0, 0x90));
    ui_panel(x, y, w, h, R_PANEL, THEME_PANEL);
    ui_hit(x, y, w, h, HIT_PANEL);
    float cs = phone ? 260 : 380, cx = x + pad, cy = y + pad;
    ui_card(a->art[0] ? a->art : NULL, cx, cy, cs, cs, 0, 0, a->title, 0);
    float lx = phone ? cx + cs + 32 : cx, ly = phone ? cy : cy + cs + 28, lw = phone ? w - cs - 2 * pad - 32 : cs;
    font_draw_tracked("SOUNDTRACK", lx, ly, FONT_XS | FONT_B, THEME_ACCENT, 1.2f);
    ly += 34;
    ly += font_draw_wrapped(a->title, lx, ly, FONT_S | FONT_B, THEME_TEXT, lw, 3) * font_height(FONT_S | FONT_B) + 8;
    char by[160];
    if (a->year) snprintf(by, sizeof by, "%s  \xC2\xB7  %d", a->artist, a->year); else snprintf(by, sizeof by, "%s", a->artist);
    font_draw_fit(by, lx, ly, FONT_XS, THEME_TEXT_DIM, lw);
    ly += 44;
    if (o->qr_ready && !phone) {   /* the code, at the foot of the left column */
        float qs = 170, qy = y + h - pad - qs;
        if (qy > ly) {
            ui_qr(o->qr, cx, qy, qs);
            font_draw_wrapped("Scan to listen on your phone", cx + qs + 20, qy + qs / 2 - font_height(FONT_XS), FONT_XS | FONT_B, THEME_TEXT_DIM, cs - qs - 20, 3);
        }
    }
    if (phone && a->link[0]) {
        float bw = ui_pill("Listen", 1, ICON_PLAY, lx, ly, 64, PILL_PRIMARY, 0);
        ui_hit(lx, ly, bw, 64, HIT_OPEN);
    }
    /* the tracks, in two columns (one on a phone), as many as fit */
    float tx = phone ? x + pad : cx + cs + 56, ty = phone ? cy + cs + 40 : cy, tw = x + w - pad - tx;
    int cols = phone ? 1 : 2;
    float colw = (tw - (cols - 1) * 40) / cols, rh = 46;
    int per = (int)((y + h - pad - ty) / rh), multi_disc = a->n && a->tracks[a->n - 1].disc > 1;
    if (per < 1) per = 1;
    if (cols == 2 && a->n > 8 && (a->n + 1) / 2 < per) per = (a->n + 1) / 2;   /* the columns even */
    for (int i = 0; i < a->n && i < per * cols; i++) {
        const MusicTrack *t = &a->tracks[i];
        float rx = tx + (i / per) * (colw + 40), ry = ty + (i % per) * rh;
        char no[16], len[16] = "";
        if (multi_disc) snprintf(no, sizeof no, "%d.%d", t->disc, t->no); else snprintf(no, sizeof no, "%d", t->no);
        if (t->ms > 0) snprintf(len, sizeof len, "%d:%02d", t->ms / 60000, t->ms / 1000 % 60);
        font_draw(no, rx, ry, FONT_XS | FONT_B, THEME_TEXT_MUTED);
        float lw2 = font_measure(len, FONT_XS);
        font_draw_fit(t->name, rx + 56, ry, FONT_S, THEME_TEXT, colw - 56 - lw2 - 20);
        font_draw(len, rx + colw - lw2, ry + 2, FONT_XS, THEME_TEXT_MUTED);
        if (i % per != per - 1 && i != a->n - 1) gfx_rect(rx, ry + rh - 8, colw, 1, COLOR_ALPHA(THEME_TEXT, 0x12));
    }
    if (a->n > per * cols) {
        char more[48]; snprintf(more, sizeof more, "and %d more", a->n - per * cols);
        font_draw(more, tx, y + h - pad + 4, FONT_XS, THEME_TEXT_MUTED);
    }
    gfx_set_alpha(1);
}

static int key(Screen *s, KeyAction a)
{
    (void)s;
    if (a == KEY_BACK || a == KEY_OK) { app_pop(); return 1; }
    return 1;
}

static int tap(Screen *s, int id)
{
    Sound *o = s->st;
    if (g_touch_aim) return 1;
    if (id == HIT_OUTSIDE) app_pop();
    else if (id == HIT_OPEN && o->a.link[0]) { LOGI("music: opening %s", o->a.title); SDL_OpenURL(o->a.link); }
    return 1;
}

static void destroy(Screen *s) { free(s->st); free(s); }

Screen *screen_soundtrack_create(const MusicAlbum *album, const char *of)
{
    Screen *s = calloc(1, sizeof *s);
    Sound *o = calloc(1, sizeof *o);
    o->a = *album;
    snprintf(o->of, sizeof o->of, "%s", of ? of : "");
    char link[300];
    snprintf(link, sizeof link, "%s", album->link);
    link[strcspn(link, "?")] = 0;   /* the store's tracking tail makes the code denser */
    uint8_t tmp[qrcodegen_BUFFER_LEN_MAX];
    o->qr_ready = link[0] && qrcodegen_encodeText(link, tmp, o->qr, qrcodegen_Ecc_MEDIUM, qrcodegen_VERSION_MIN, 12, qrcodegen_Mask_AUTO, true);
    LOGI("music: album %s (%d tracks)", album->title, album->n);
    s->name = "soundtrack"; s->st = o;
    s->update = update; s->draw = draw; s->key = key; s->tap = tap; s->destroy = destroy;
    Screen *under = app_top();
    s->phone = under ? under->phone : 1;
    return s;
}
