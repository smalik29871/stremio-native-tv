/* Zap: your watchlist as TV channels. ▲ and ▼ (the remote's channel keys too) flip from one
 * title to the next, each already playing a little way into its first act, the way channel
 * surfing used to land you mid-scene; OK offers it from the start or on from here; Back
 * leaves. Channels keep going while you are away: coming back finds one further on.
 * Nothing watched on a channel is saved against the title or sent to Trakt. */
#include "../app.h"
#include "../http.h"
#include "../image.h"
#include "../log.h"
#include "font.h"
#include "theme.h"
#include "widgets.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_CH 24
#define BANNER_S 3.2f      /* the channel's banner, after each change */

enum { TUNE, LOOKING, ON, LEAVE, HANDED, START };

typedef struct {
    MetaPreview meta;
    char vid[256];
    int mine;              /* from your library (else a popular title) */
} Channel;

typedef struct {
    Channel ch[MAX_CH]; int n, cur;
    int state;
    float epoch;           /* the channels "started" here */
    float banner_at;
    int card, card_sel; double card_pos;
    int skips;             /* channels in a row with nothing that plays */
    unsigned gen;
    int cat_state;         /* 0 not asked, 1 asking, 2 answered (or nothing to ask) */
    int built;             /* the channels are listed (once the library is in) */
} Zap;

static Zap *g_z;

static unsigned long long fnv64(const char *s)
{
    unsigned long long h = 1469598103934665603ull;
    for (; *s; s++) h = (h ^ (unsigned char)*s) * 1099511628211ull;
    return h;
}

static int has(const Zap *z, const char *id)
{
    for (int i = 0; i < z->n; i++) if (!strcmp(z->ch[i].meta.id, id)) return 1;
    return 0;
}

static void add(Zap *z, const MetaPreview *m, const char *vid, int mine)
{
    if (z->n >= MAX_CH || !m->id || !m->type || !m->name || has(z, m->id)) return;
    Channel *c = &z->ch[z->n++];
    meta_preview_copy(&c->meta, m);
    snprintf(c->vid, sizeof c->vid, "%s", vid);
    c->mine = mine;
}

/* A series plays the episode you are on, else its first. */
static void series_vid(const char *id, const char *at, char *out, size_t n)
{
    if (at && *at && strchr(at, ':')) snprintf(out, n, "%s", at);
    else snprintf(out, n, "%s:1:1", id);
}

/* Your library: what you are part way through, then what you saved, newest first; not
 * what you have finished. */
static void from_library(Zap *z)
{
    int order[512], k = 0;
    for (int i = 0; i < g_app.n_library && k < 512; i++) {
        const LibraryItem *li = &g_app.library[i];
        if (!li->id || !li->name || !li->type || (li->removed && !li->temp)) continue;
        if (strcmp(li->type, "movie") && strcmp(li->type, "series")) continue;
        if (!strcmp(li->type, "movie") && (li->flagged_watched || li->times_watched > 0)) continue;
        order[k++] = i;
    }
    for (int a = 1; a < k; a++)
        for (int b = a; b > 0; b--) {
            const LibraryItem *x = &g_app.library[order[b]], *y = &g_app.library[order[b - 1]];
            int64_t wx = x->time_offset > 0 ? x->last_watched_ms : x->mtime_ms - 1000000000000LL;
            int64_t wy = y->time_offset > 0 ? y->last_watched_ms : y->mtime_ms - 1000000000000LL;
            if (wx <= wy) break;
            int t = order[b]; order[b] = order[b - 1]; order[b - 1] = t;
        }
    for (int i = 0; i < k; i++) {
        const LibraryItem *li = &g_app.library[order[i]];
        MetaPreview m; memset(&m, 0, sizeof m);
        m.id = li->id; m.type = li->type; m.name = li->name; m.poster = li->poster;
        char vid[256];
        if (!strcmp(li->type, "series")) series_vid(li->id, li->video_id, vid, sizeof vid);
        else snprintf(vid, sizeof vid, "%s", li->id);
        add(z, &m, vid, 1);
    }
}

static void on_catalog(HttpResponse *r, void *user)
{
    Zap *z = g_z;
    if (!z || (uintptr_t)user != z->gen) { ParsedMetas *pm = r->parsed; if (pm) { r->parsed = NULL; for (int i = 0; i < pm->n; i++) meta_preview_free(&pm->m[i]); free(pm->m); free(pm); } return; }
    z->cat_state = 2;
    ParsedMetas *pm = r->parsed;
    int before = z->n;
    if (pm) {
        r->parsed = NULL;
        for (int i = 0; i < pm->n; i++) {
            const MetaPreview *m = &pm->m[i];
            char vid[256];
            if (m->type && !strcmp(m->type, "series")) series_vid(m->id, NULL, vid, sizeof vid);
            else snprintf(vid, sizeof vid, "%s", m->id ? m->id : "");
            if (m->id) add(z, m, vid, 0);
        }
        for (int i = 0; i < pm->n; i++) meta_preview_free(&pm->m[i]);
        free(pm->m); free(pm);
    }
    LOGI("zap: %d channels, %d from your library", z->n, before);
    app_redraw();
}

/* Popular titles after yours: Home's first catalog row. */
static void ask_catalog(Zap *z)
{
    HomeRowDef rows[64];
    int n = app_home_rows(rows, 64);
    for (int i = 0; i < n; i++) {
        if (rows[i].kind != 2 || rows[i].hidden || rows[i].addon >= g_app.n_addons) continue;
        const Addon *ad = &g_app.addons[rows[i].addon];
        if (rows[i].catalog >= ad->manifest.n_catalogs) continue;
        const AddonCatalog *cat = &ad->manifest.catalogs[rows[i].catalog];
        char url[1024];
        addon_url_catalog(ad, cat->type, cat->id, NULL, url, sizeof url);
        z->cat_state = 1;
        app_fetch_metas(url, 1, on_catalog, (void *)(uintptr_t)z->gen);
        return;
    }
    z->cat_state = 2;
}

static const MetaPreview *meta_of(const Channel *c)
{
    const MetaPreview *d = app_details_cached(c->meta.type, c->meta.id);
    return d ? d : &c->meta;
}

/* "142 min", "1h 22m", "45": in seconds; 0 unknown */
static double runtime_s(const MetaPreview *m)
{
    if (!m->runtime) return 0;
    int a = 0, b = 0;
    if (sscanf(m->runtime, "%dh %dm", &a, &b) == 2 || sscanf(m->runtime, "%dh%dm", &a, &b) == 2) return (a * 60 + b) * 60.0;
    if (sscanf(m->runtime, "%d", &a) == 1) return a * 60.0;
    return 0;
}

/* Where a channel is: a moment in its first act, the same each time for a title, moved on
 * by how long Zap has been going. */
static float frac_of(const Channel *c) { return 0.10f + 0.10f * (float)(fnv64(c->meta.id) % 1000) / 1000.0f; }

static void episode_label(const char *vid, char *out, size_t n)
{
    int s = 0, e = 0;
    const char *p = strchr(vid, ':');
    if (p && sscanf(p, ":%d:%d", &s, &e) == 2) snprintf(out, n, "S%d E%d", s, e);
    else out[0] = 0;
}

static void on_sources(const Stream *items, int n, void *user)
{
    Zap *z = g_z;
    if (!z || (uintptr_t)user != z->gen || z->state != LOOKING) return;
    Channel *c = &z->ch[z->cur];
    const MetaPreview *m = meta_of(c);
    PlayCandidate *cands = calloc(4, sizeof *cands);
    int k = 0;
    for (int i = 0; i < n && k < 4; i++) if (streams_candidate(m, &items[i], &cands[k])) k++;
    if (!k) {
        free(cands);
        LOGI("zap: nothing plays on %s, on to the next", c->meta.name);
        if (++z->skips >= z->n) { app_toast("Nothing on your channels plays right now"); z->state = LEAVE; return; }
        z->cur = (z->cur + 1) % z->n;
        z->state = TUNE;
        return;
    }
    z->skips = 0;
    double rt = runtime_s(m), start = rt > 0 ? rt * frac_of(c) + (g_app.time - z->epoch) : 0;
    char ep[32], title[400];
    episode_label(c->vid, ep, sizeof ep);
    snprintf(title, sizeof title, "%s%s%s", m->name, ep[0] ? "  -  " : "", ep);
    LOGI("zap: channel %d of %d, %s%s%s from %.0f s", z->cur + 1, z->n, m->name, ep[0] ? " " : "", ep, start);
    player_zap_next(frac_of(c));
    z->state = ON;
    app_push(screen_player_create(m, c->vid, title, cands, k, 0, start, NULL, NULL));
    if (z->n > 1) { const Channel *nx = &z->ch[(z->cur + 1) % z->n]; streams_prefetch(meta_of(nx), nx->vid); }   /* ▲ next starts sooner */
}

static void tune(Zap *z)
{
    Channel *c = &z->ch[z->cur];
    z->state = LOOKING;
    z->gen++;
    z->card = 0;
    streams_lookup_auto(meta_of(c), c->vid, on_sources, (void *)(uintptr_t)z->gen);
}

/* ---- what the player hands over ---- */

static void flip(Zap *z, int dir)
{
    if (z->n < 2) return;
    z->cur = (z->cur + dir + z->n) % z->n;
    z->banner_at = g_app.time;
    z->state = TUNE;
    z->card = 0;
    streams_lookup_cancel();
    app_pop();   /* the channel's player: Zap tunes the next */
}

void zap_channel_over(void)
{
    Zap *z = g_z;
    if (!z || z->state != ON) return;
    LOGI("zap: %s is over or will not play, on to the next", z->ch[z->cur].meta.name);
    flip(z, 1);
}

int zap_key(KeyAction a, double position)
{
    Zap *z = g_z;
    if (!z || z->state != ON) return 0;
    if (a == KEY_PLAY || a == KEY_PAUSE || a == KEY_PLAY_PAUSE || a == KEY_EXIT) return 0;
    z->banner_at = g_app.time;
    app_redraw();
    if (z->card) {
        Channel *c = &z->ch[z->cur];
        if (a == KEY_UP || a == KEY_DOWN || a == KEY_LEFT || a == KEY_RIGHT) z->card_sel = !z->card_sel;
        else if (a == KEY_BACK) z->card = 0;
        else if (a == KEY_OK && z->card_sel == 0) {
            LOGI("zap: watching %s from the start", c->meta.name);
            z->state = START;
            app_pop();
        } else if (a == KEY_OK) {
            LOGI("zap: watching %s on from %.0f s", c->meta.name, z->card_pos);
            z->state = HANDED;
            z->card = 0;
            player_zap_end(app_top());
        }
        return 1;
    }
    switch (a) {
    case KEY_UP: flip(z, 1); return 1;
    case KEY_DOWN: flip(z, -1); return 1;
    case KEY_OK: z->card = 1; z->card_sel = 0; z->card_pos = position; return 1;
    case KEY_BACK: LOGI("zap: left"); z->state = LEAVE; streams_lookup_cancel(); app_pop(); return 1;
    default: return 1;   /* seeking and the rest: not on a channel */
    }
}

static void clock_of(double s, char *out, size_t n)
{
    int t = (int)(s + 0.5);
    if (t >= 3600) snprintf(out, n, "%d:%02d:%02d", t / 3600, t / 60 % 60, t % 60);
    else snprintf(out, n, "%d:%02d", t / 60, t % 60);
}

/* A remote key in a box, then what it does, ending at x_right: OK, or the channel arrows. */
static void hint(float x_right, float y, int arrows, const char *what, float a)
{
    int fs = FONT_S | FONT_B;
    Color c = RGBA(0xFF, 0xFF, 0xFF, (int)(0xD8 * a)), line = RGBA(0xFF, 0xFF, 0xFF, (int)(0x70 * a));
    float tw = font_measure(what, fs), kw = 56, kh = 40, kx = x_right - tw - 16 - kw, ky = y + (font_height(fs) - kh) / 2;
    gfx_rrect_ring(kx, ky, kw, kh, 10, 1.5f, line);
    if (arrows) {   /* up and down, side by side */
        float cx = kx + kw / 2, cy = ky + kh / 2;
        gfx_line(cx - 18, cy + 4, cx - 11, cy - 4, 3, c); gfx_line(cx - 11, cy - 4, cx - 4, cy + 4, 3, c);
        gfx_line(cx + 4, cy - 4, cx + 11, cy + 4, 3, c); gfx_line(cx + 11, cy + 4, cx + 18, cy - 4, 3, c);
    } else font_draw_center("OK", kx + kw / 2, ky + (kh - font_height(FONT_XS | FONT_B)) / 2, FONT_XS | FONT_B, c);
    font_draw(what, x_right - tw, y, fs, c);
}

static void banner(Zap *z, float a)
{
    const Channel *c = &z->ch[z->cur];
    const MetaPreview *m = meta_of(c);
    float W = SNTV_UI_W, H = SNTV_UI_H;
    /* the channel's number, top left, always */
    char pill[48]; snprintf(pill, sizeof pill, "ZAP  \xC2\xB7  %d of %d", z->cur + 1, z->n);
    float pw = font_measure(pill, FONT_XS | FONT_B) + 48;
    gfx_rrect(60, 48, pw, 52, 26, RGBA(0, 0, 0, 0x90));
    font_draw(pill, 84, 48 + (52 - font_height(FONT_XS | FONT_B)) / 2, FONT_XS | FONT_B, RGBA(0xFF, 0xD2, 0x78, 0xF0));
    if (a <= 0.01f) return;
    int al = (int)(255 * a);
    gfx_gradient_v(0, H - 300, W, 300, RGBA(0, 0, 0, 0), RGBA(0, 0, 0, (int)(0xD0 * a)));
    char num[8]; snprintf(num, sizeof num, "%d", z->cur + 1);
    float bx = THEME_MARGIN, by = H - 200, bs = 120;
    gfx_rrect(bx, by, bs, bs, 26, RGBA(0xFF, 0xFF, 0xFF, (int)(0xEE * a)));
    font_draw_center(num, bx + bs / 2, by + (bs - font_height(FONT_HERO | FONT_B)) / 2, FONT_HERO | FONT_B, RGBA(0x0A, 0x0A, 0x0C, al));
    float tx = bx + bs + 36, tw = W - tx - 560;
    font_draw_fit(m->name, tx, by + 2, FONT_XL | FONT_B, RGBA(0xFF, 0xFF, 0xFF, al), tw);
    char line[300], ep[32];
    episode_label(c->vid, ep, sizeof ep);
    const char *parts[4] = { m->release_info, m->type && !strcmp(m->type, "series") ? "Series" : "Film", ep[0] ? ep : m->runtime,
                             c->mine ? "From your library" : "Popular right now" };
    line[0] = 0;
    for (int i = 0; i < 4; i++) {
        if (!parts[i] || !*parts[i]) continue;
        size_t l = strlen(line);
        snprintf(line + l, sizeof line - l, "%s%s", l ? "  \xC2\xB7  " : "", parts[i]);
    }
    font_draw_fit(line, tx, by + 78, FONT_S, RGBA(0xFF, 0xFF, 0xFF, (int)(0xC8 * a)), tw);
    hint(W - THEME_MARGIN, by + 14, 1, "Change channel", a);
    hint(W - THEME_MARGIN, by + 70, 0, "Watch it properly", a);
}

static void card(Zap *z)
{
    const MetaPreview *m = meta_of(&z->ch[z->cur]);
    float W = SNTV_UI_W, H = SNTV_UI_H, w = 760, h = 330, x = (W - w) / 2, y = (H - h) / 2;
    gfx_rect(0, 0, W, H, RGBA(0, 0, 0, 0x80));
    ui_panel(x, y, w, h, R_PANEL, THEME_PANEL);
    font_draw_fit(m->name, x + 48, y + 40, FONT_L | FONT_B, THEME_TEXT, w - 96);
    char keep[80], at[24];
    clock_of(z->card_pos, at, sizeof at);
    snprintf(keep, sizeof keep, "Keep watching from %s", at);
    const char *opt[2] = { "Watch from the start", keep };
    for (int i = 0; i < 2; i++) {
        float oy = y + 120 + i * 92;
        int f = z->card_sel == i;
        Color fg = f ? THEME_ON_FOCUS : THEME_TEXT;
        gfx_rrect(x + 48, oy, w - 96, 72, 18, f ? THEME_FOCUS : COLOR_ALPHA(THEME_TEXT, 0x16));
        float tw = font_measure(opt[i], FONT_M | FONT_B), ic = i == 0 ? 40 : 0, tx = x + (w - tw - ic) / 2 + ic;
        if (i == 0) ui_icon(ICON_PLAY, tx - 24, oy + 36, 26, fg);
        font_draw(opt[i], tx, oy + (72 - font_height(FONT_M | FONT_B)) / 2, FONT_M | FONT_B, fg);
    }
}

void zap_draw(int playing, double position)
{
    Zap *z = g_z;
    if (!z || z->cur >= z->n) return;
    (void)position;
    float t = g_app.time - z->banner_at;
    float a = !playing ? 1 : t < BANNER_S ? 1 : t < BANNER_S + 0.6f ? 1 - (t - BANNER_S) / 0.6f : 0;
    banner(z, a);
    if (a > 0 && a < 1) app_redraw();
    else if (t < BANNER_S + 0.7f) app_redraw();
    if (z->card) card(z);
}

/* ---- the screen under the channels: tuning in ---- */

static void update(Screen *s, float dt)
{
    (void)dt;
    Zap *z = s->st;
    if (z->state == LEAVE || z->state == HANDED) { app_pop(); return; }   /* left, or the player was handed on and is gone */
    if (z->state == START) {
        const Channel *c = &z->ch[z->cur];
        char ep[32];
        episode_label(c->vid, ep, sizeof ep);
        app_replace(screen_streams_create_from_start(meta_of(c), c->vid, ep[0] ? ep : NULL));
        return;
    }
    if (z->state == ON) { z->state = LEAVE; app_pop(); return; }   /* its player went without asking Zap (a break, a crash): out */
    for (int d = -1; d <= 1 && z->n; d++) { const Channel *c = &z->ch[(z->cur + d + z->n) % z->n]; app_details_want(c->meta.type, c->meta.id); }
    if (!z->built && (!g_app.library_loading || g_app.time - z->epoch > 4)) {   /* your titles first: a moment for the library */
        z->built = 1;
        from_library(z);
        if (z->n < 12) ask_catalog(z);
        else z->cat_state = 2;
        LOGI("zap: %d channels from your library%s", z->n, z->cat_state == 1 ? ", popular titles on their way" : "");
    }
    if (z->state == TUNE && z->built) {
        if (z->n && (z->n >= 12 || z->cat_state == 2)) tune(z);
        else if (!z->n && z->cat_state == 2) { app_toast("Add films and series to your Library to zap through them"); app_pop(); return; }
    }
    if (z->state == LOOKING) streams_lookup_tick();
    app_redraw();
}

static void draw(Screen *s)
{
    Zap *z = s->st;
    float W = SNTV_UI_W, H = SNTV_UI_H;
    gfx_rect(0, 0, W, H, RGBA(0, 0, 0, 0xFF));
    if (!z->n) { ui_spinner(W / 2, H / 2, 34); return; }
    char buf[512];
    const char *bg = app_art_background(meta_of(&z->ch[z->cur]), buf, sizeof buf);
    int iw, ih;
    unsigned tex = bg ? image_get(bg, &iw, &ih) : 0;
    if (tex && iw > 0 && ih > 0) {   /* the channel's picture while it tunes in */
        float ia = (float)iw / ih, sa = W / H, u0 = 0, v0 = 0, u1 = 1, v1 = 1;
        if (ia > sa) { float f = sa / ia; u0 = (1 - f) / 2; u1 = 1 - u0; } else { float f = ia / sa; v0 = (1 - f) / 2; v1 = 1 - v0; }
        gfx_tex_uv(tex, 0, 0, W, H, u0, v0, u1, v1, RGBA(0x90, 0x90, 0x90, 0xFF));
    }
    ui_spinner(W / 2, H / 2 - 40, 34);
    font_draw_center("Tuning in\xE2\x80\xA6", W / 2, H / 2 + 20, FONT_S | FONT_B, RGBA(0xFF, 0xFF, 0xFF, 0xD0));
    banner(z, 1);
}

static int key(Screen *s, KeyAction a)
{
    Zap *z = s->st;
    if (a == KEY_BACK) { LOGI("zap: left"); streams_lookup_cancel(); z->state = LEAVE; return 1; }
    if ((a == KEY_UP || a == KEY_DOWN) && z->n > 1) {   /* flipping on before this one is in */
        streams_lookup_cancel();
        z->cur = (z->cur + (a == KEY_UP ? 1 : -1) + z->n) % z->n;
        z->banner_at = g_app.time;
        z->state = TUNE;
        return 1;
    }
    return 1;
}

static void destroy(Screen *s)
{
    Zap *z = s->st;
    for (int i = 0; i < z->n; i++) meta_preview_free(&z->ch[i].meta);
    if (g_z == z) g_z = NULL;
    free(z);
    free(s);
}

Screen *screen_zap_create(void)
{
    Screen *s = calloc(1, sizeof *s);
    Zap *z = calloc(1, sizeof *z);
    g_z = z;
    z->epoch = g_app.time;
    z->banner_at = g_app.time;
    z->gen = 1;
    z->state = TUNE;
    s->name = "zap"; s->st = z;
    s->update = update; s->draw = draw; s->key = key; s->destroy = destroy;
    return s;
}
