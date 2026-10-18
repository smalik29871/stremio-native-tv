/* Screensaver: backdrops from the library and the Home catalogs drift slowly
 * (Ken Burns) with a big clock, after the remote has been quiet for a while.
 * Everything on screen shifts a few pixels every minute so OLED panels never
 * hold one static image. Any key closes it and is swallowed. */
#include "../app.h"
#include "../image.h"
#include "../log.h"
#include "font.h"
#include "theme.h"
#include "widgets.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define SLOT_S 14.0f      /* one backdrop */
#define FADE_S 1.6f       /* crossfade into the next */
#define ZOOM 0.10f        /* how much a backdrop grows over its slot */
#define WAIT_S 5.0f       /* give up on an image that has not arrived */

typedef struct {
    AmbientItem *items; int n;
    int cur, prev;
    float t;              /* seconds into the current slot */
    float fade;           /* 0..1 while the previous image fades out */
    float shown_at;       /* g_app.time the current image appeared */
    int cur_ready;
} Ambient;

static void collect(Ambient *a)
{
    const AmbientItem *pool;
    int np = app_ambient_pool(&pool);
    a->items = calloc((size_t)(np + g_app.n_library + 1), sizeof *a->items);
    /* library first: that is what the viewer actually watches */
    for (int i = 0; i < g_app.n_library; i++) {
        const LibraryItem *li = &g_app.library[i];
        if (li->removed || !li->id || !li->name) continue;
        MetaPreview m = { .id = li->id, .type = li->type, .name = li->name, .poster = li->poster };
        char buf[512];
        const char *bg = app_art_background(&m, buf, sizeof buf);
        if (!bg) continue;
        AmbientItem *it = &a->items[a->n++];
        snprintf(it->id, sizeof it->id, "%s", li->id);
        snprintf(it->name, sizeof it->name, "%s", li->name);
        snprintf(it->background, sizeof it->background, "%s", bg);
    }
    for (int i = 0; i < np; i++) {
        int dup = 0;
        for (int k = 0; k < a->n; k++) if (strcmp(a->items[k].id, pool[i].id) == 0) { dup = 1; break; }
        if (!dup) a->items[a->n++] = pool[i];
    }
    /* a different starting point each time, so it does not always open on the same title */
    if (a->n > 1) {
        int start = (int)(time(NULL) % a->n);
        for (int i = 0; i < a->n / 2; i++) {
            int j = (start + i) % a->n, k = (start + a->n - 1 - i) % a->n;
            if (j == k) continue;
            AmbientItem tmp = a->items[j]; a->items[j] = a->items[k]; a->items[k] = tmp;
        }
    }
}

static void advance(Ambient *a)
{
    if (a->n < 2) return;
    a->prev = a->cur;
    a->cur = (a->cur + 1) % a->n;
    a->fade = 1;
    a->t = 0;
    a->cur_ready = 0;
    a->shown_at = g_app.time;
}

static void update(Screen *s, float dt)
{
    Ambient *a = s->st;
    app_redraw();
    if (!a->n) return;
    a->t += dt;
    if (a->fade > 0) { a->fade -= dt / FADE_S; if (a->fade < 0) a->fade = 0; }
    int w, h;
    unsigned tex = image_get(a->items[a->cur].background, &w, &h);
    if (tex && !a->cur_ready) { a->cur_ready = 1; a->shown_at = g_app.time; a->t = 0; }
    if (!tex && (image_failed(a->items[a->cur].background) || g_app.time - a->shown_at > WAIT_S)) { advance(a); return; }
    if (a->n > 1) image_prefetch(a->items[(a->cur + 1) % a->n].background);
    if (a->cur_ready && a->t > SLOT_S) advance(a);
}

/* Cover-fit UVs, zoomed by (1 + ZOOM * k) around a point that slides across the image. */
static void draw_backdrop(const AmbientItem *it, float k, int dir, float alpha)
{
    int iw, ih;
    unsigned tex = image_get(it->background, &iw, &ih);
    if (!tex || alpha <= 0) return;
    float ia = (float)iw / (ih ? ih : 1), sa = (float)SNTV_UI_W / SNTV_UI_H;
    float u0 = 0, v0 = 0, u1 = 1, v1 = 1;
    if (ia > sa) { float f = sa / ia; u0 = (1 - f) / 2; u1 = 1 - u0; } else { float f = ia / sa; v0 = (1 - f) / 2; v1 = 1 - v0; }
    float z = 1 + ZOOM * k;
    float uw = (u1 - u0) / z, vh = (v1 - v0) / z;
    /* focus point drifts from one side to the other, alternating per slot */
    float fx = dir & 1 ? 0.35f + 0.3f * k : 0.65f - 0.3f * k;
    float fy = dir & 2 ? 0.40f + 0.2f * k : 0.60f - 0.2f * k;
    float cu = u0 + (u1 - u0) * fx, cv = v0 + (v1 - v0) * fy;
    float nu0 = cu - uw / 2, nv0 = cv - vh / 2;
    if (nu0 < u0) nu0 = u0;
    if (nv0 < v0) nv0 = v0;
    if (nu0 + uw > u1) nu0 = u1 - uw;
    if (nv0 + vh > v1) nv0 = v1 - vh;
    gfx_tex_uv(tex, 0, 0, SNTV_UI_W, SNTV_UI_H, nu0, nv0, nu0 + uw, nv0 + vh, RGBA(255, 255, 255, (int)(255 * alpha)));
}

static void draw(Screen *s)
{
    Ambient *a = s->st;
    gfx_rect(0, 0, SNTV_UI_W, SNTV_UI_H, RGBA(0, 0, 0, 0xFF));
    time_t now = time(NULL);
    struct tm tmv;
    localtime_r(&now, &tmv);
    /* burn-in guard: a different offset every minute */
    float sx = (float)((tmv.tm_min % 5) - 2) * 7, sy = (float)(((tmv.tm_min / 5) % 3) - 1) * 7;

    if (a->n) {
        const AmbientItem *cur = &a->items[a->cur];
        float k = a->t / SLOT_S;
        if (k > 1) k = 1;
        if (a->fade > 0 && a->prev != a->cur) draw_backdrop(&a->items[a->prev], 1, a->prev, 1);
        draw_backdrop(cur, k, a->cur, a->cur_ready ? 1 - a->fade : 0);
        gfx_gradient_v(0, SNTV_UI_H - 420, SNTV_UI_W, 420, RGBA(0, 0, 0, 0), RGBA(0, 0, 0, 0xC0));
        gfx_gradient_v(0, 0, SNTV_UI_W, 260, RGBA(0, 0, 0, 0x70), RGBA(0, 0, 0, 0));
        font_draw_shadow(cur->name, THEME_MARGIN + sx, SNTV_UI_H - 150 + sy, FONT_M | FONT_B, THEME_TEXT, RGBA(0, 0, 0, 0xA0));
        font_draw(a->cur < g_app.n_library ? "From your library" : "Popular right now", THEME_MARGIN + sx, SNTV_UI_H - 104 + sy, FONT_XS | FONT_B, THEME_TEXT_DIM);
    }

    char clock[16], date[64];
    if (g_app.clock_mode == 1) snprintf(clock, sizeof clock, "%02d:%02d", tmv.tm_hour, tmv.tm_min);
    else snprintf(clock, sizeof clock, "%d:%02d", tmv.tm_hour % 12 ? tmv.tm_hour % 12 : 12, tmv.tm_min);
    strftime(date, sizeof date, "%A, %B %-d", &tmv);
    float cw = font_measure(clock, FONT_TITLE | FONT_B);
    float cx = SNTV_UI_W - THEME_MARGIN - cw + sx, cy = 64 + sy;
    font_draw_shadow(clock, cx, cy, FONT_TITLE | FONT_B, THEME_TEXT, RGBA(0, 0, 0, 0xA0));
    if (g_app.clock_mode != 1) font_draw_shadow(tmv.tm_hour < 12 ? "AM" : "PM", cx + cw + 12, cy + 14, FONT_M | FONT_B, THEME_TEXT_DIM, RGBA(0, 0, 0, 0xA0));
    font_draw_shadow(date, SNTV_UI_W - THEME_MARGIN - font_measure(date, FONT_S) + sx, cy + 88, FONT_S, THEME_TEXT_DIM, RGBA(0, 0, 0, 0xA0));
    ui_brand(THEME_MARGIN + sx, 56 + sy, 44);
}

static int key(Screen *s, KeyAction a)
{
    (void)s; (void)a;
    app_pop();   /* the key only wakes the screen */
    app_redraw();
    return 1;
}

static void destroy(Screen *s)
{
    Ambient *a = s->st;
    free(a->items);
    free(a);
    free(s);
}

Screen *screen_ambient_create(void)
{
    Screen *s = calloc(1, sizeof *s);
    Ambient *a = calloc(1, sizeof *a);
    collect(a);
    a->shown_at = g_app.time;
    LOGI("ambient: screensaver with %d backdrops", a->n);
    s->name = "ambient"; s->st = a;
    s->update = update; s->draw = draw; s->key = key; s->destroy = destroy;
    return s;
}
