/* Detail: cinematic header over the backdrop (logo or title, IMDb, meta, genres,
 * synopsis), a pill action row (Play / Resume with progress, Library), and for
 * series: season tabs and a horizontal episode rail with thumbnails, watched
 * badges, resume progress and spoiler blur for what you haven't seen yet. */
#include "../app.h"
#include "../tmdb.h"
#include "../ai.h"
#include "../image.h"
#include "../../vendor/cJSON.h"
#include "../http.h"
#include "../log.h"
#include "../store.h"
#include "font.h"
#include "theme.h"
#include "widgets.h"
#include "screen_options.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { AREA_ACTIONS, AREA_SEASONS, AREA_EPISODES };
enum { BTN_PLAY, BTN_RESTART, BTN_LIBRARY, BTN_ASK, BTN_COUNT };
#define EP_W 384.0f
#define EP_H 216.0f
#define EP_GAP 28.0f
#define RAIL_Y 700.0f

typedef struct {
    MetaPreview preview;
    MetaItem meta; int has_meta;
    int loading; unsigned req; char error[400];
    int *seasons; int n_seasons; int season_idx;
    int *ep_index; int n_eps; int ep_focus;
    int area, btn;
    float rail_x, focus_t;
    float intro;                 /* page entrance animation */
    char resume_label[96];
    char resume_url[1024], resume_video[128];
    double resume_pos, resume_frac;
    /* "Ask AI": a spoiler-free card about the title */
    int ask_open; unsigned ask_req; float ask_t;
    char *ask_nutshell, *ask_good_for, *ask_notes, *ask_similar; char ask_err[160];
} Detail;

/* the visible buttons in order: Play/Resume, [From the start], Library, [Ask AI] */
static int btn_at(const Detail *d, int pos)
{
    if (pos == 0) return BTN_PLAY;
    if (d->resume_label[0]) { if (pos == 1) return BTN_RESTART; pos--; }
    return pos == 1 ? BTN_LIBRARY : BTN_ASK;
}
static int n_buttons_d(const Detail *d) { return 2 + (d->resume_label[0] ? 1 : 0) + (ai_enabled() ? 1 : 0); }

static void ask_clear(Detail *d)
{
    free(d->ask_nutshell); free(d->ask_good_for); free(d->ask_notes); free(d->ask_similar);
    d->ask_nutshell = d->ask_good_for = d->ask_notes = d->ask_similar = NULL;
}

static int ask_parse(Detail *d, const char *json)
{
    cJSON *root = cJSON_Parse(json);
    if (!root) return 0;
    const cJSON *v;
    #define TAKE(field, key) do { v = cJSON_GetObjectItemCaseSensitive(root, key); if (cJSON_IsString(v) && v->valuestring[0]) d->field = strdup(v->valuestring); } while (0)
    TAKE(ask_nutshell, "nutshell"); TAKE(ask_good_for, "good_for"); TAKE(ask_notes, "notes");
    #undef TAKE
    v = cJSON_GetObjectItemCaseSensitive(root, "similar");
    if (cJSON_IsArray(v)) {
        char buf[400] = ""; const cJSON *it;
        cJSON_ArrayForEach(it, v) if (cJSON_IsString(it)) { size_t l = strlen(buf); snprintf(buf + l, sizeof buf - l, "%s%s", l ? "  \xC2\xB7  " : "", it->valuestring); }
        if (buf[0]) d->ask_similar = strdup(buf);
    } else if (cJSON_IsString(v)) d->ask_similar = strdup(v->valuestring);
    cJSON_Delete(root);
    return d->ask_nutshell != NULL;
}

static void on_ask(const char *text, const char *error, void *user)
{
    Detail *d = user;
    d->ask_req = 0;
    if (text && ask_parse(d, text)) {
        char key[200]; snprintf(key, sizeof key, "ai://about/%s", d->preview.id);
        app_cache_put(key, text, strlen(text));
    } else snprintf(d->ask_err, sizeof d->ask_err, "%s", error ? error : "The answer could not be read. Try again.");
    app_redraw();
}

static void ask_open(Detail *d)
{
    d->ask_open = 1; d->ask_t = 0; d->ask_err[0] = 0;
    if (d->ask_nutshell || d->ask_req) return;
    char key[200]; snprintf(key, sizeof key, "ai://about/%s", d->preview.id);
    char *cached = app_cache_get(key, 30L * 24 * 3600);
    if (cached) { int ok = ask_parse(d, cached); free(cached); if (ok) return; }
    const TmdbInfo *tm = tmdb_get(d->preview.id, d->preview.type);
    char genres[300] = ""; for (int i = 0; i < d->preview.genres.n && i < 6; i++) { size_t l = strlen(genres); snprintf(genres + l, sizeof genres - l, "%s%s", l ? ", " : "", d->preview.genres.v[i]); }
    char msg[2200];
    snprintf(msg, sizeof msg, "Title: %s%s%s\nType: %s\nGenres: %s\nIMDb: %s\nAge rating: %s\nSynopsis: %.1200s",
             d->preview.name, d->preview.release_info ? " " : "", d->preview.release_info ? d->preview.release_info : "",
             d->preview.type ? d->preview.type : "movie", genres[0] ? genres : "unknown", d->preview.imdb_rating ? d->preview.imdb_rating : "n/a",
             tm && tm->cert[0] ? tm->cert : "unknown", d->preview.description ? d->preview.description : "(none)");
    static const char *SYS = "You are a concise, warm film and TV guide shown on a television. Never reveal plot twists or endings. "
        "Reply with JSON only, no markdown: {\"nutshell\": \"what it is and why people love it, at most two short sentences\", "
        "\"good_for\": \"the mood and who will enjoy it, one sentence\", "
        "\"notes\": \"what a parent or a sensitive viewer should know (violence, language, sex, scares, heavy themes), one spoiler-free sentence\", "
        "\"similar\": [\"three well-known titles people who like this also like\"]}";
    d->ask_req = ai_ask(SYS, msg, 420, 1, on_ask, d);
    if (!d->ask_req) snprintf(d->ask_err, sizeof d->ask_err, "The AI assistant is off. Settings > API keys.");
}

static void draw_ask(Detail *d)
{
    float k = g_app.fx_light ? 1 : d->ask_t;
    gfx_rect(0, 0, SNTV_UI_W, SNTV_UI_H, RGBA(0, 0, 0, (int)(150 * k)));
    float w = 640, x = SNTV_UI_W - w * k, y = 0, pad = 44;
    gfx_rrect(x, y, w, SNTV_UI_H, 0, RGBA(0x14, 0x12, 0x22, 0xF4));
    gfx_rect(x, y, 4, SNTV_UI_H, THEME_ACCENT);
    ui_icon(ICON_STAR, x + pad + 18, 128, 34, THEME_ACCENT2);   /* below the clock pill */
    font_draw("Ask AI", x + pad + 50, 110, FONT_L | FONT_B, THEME_TEXT);
    font_draw_fit(d->preview.name, x + pad, 164, FONT_S | FONT_B, THEME_TEXT_DIM, w - 2 * pad);
    float ty = 226, cw = w - 2 * pad;
    if (d->ask_req) { ui_spinner(x + pad + 20, ty + 20, 18); font_draw("Thinking\xE2\x80\xA6", x + pad + 56, ty + 4, FONT_S, THEME_TEXT_DIM); ty += 60; }
    if (d->ask_err[0]) { ty += font_draw_wrapped(d->ask_err, x + pad, ty, FONT_S, THEME_DANGER, cw, 3) * font_height(FONT_S) + 20; }
    struct { const char *h; const char *t; } S[] = { { "IN A NUTSHELL", d->ask_nutshell }, { "YOU'LL LIKE IT IF", d->ask_good_for }, { "BEFORE YOU WATCH  \xC2\xB7  SPOILER-FREE", d->ask_notes }, { "IF YOU LIKE THIS", d->ask_similar } };
    for (size_t i = 0; i < sizeof S / sizeof S[0]; i++) {
        if (!S[i].t) continue;
        font_draw(S[i].h, x + pad, ty, FONT_XS | FONT_B, THEME_TEXT_MUTED); ty += 34;
        ty += font_draw_wrapped(S[i].t, x + pad, ty, FONT_S, THEME_TEXT, cw, 6) * font_height(FONT_S) + 30;
    }
    if (d->ask_nutshell) font_draw("Written by an AI model from the synopsis: a guide, not gospel.", x + pad, SNTV_UI_H - 96, FONT_XS, THEME_TEXT_MUTED);
    font_draw("Back: close", x + pad, SNTV_UI_H - 60, FONT_XS, THEME_TEXT_MUTED);
}

static void select_season(Detail *d, int idx)
{
    d->season_idx = idx;
    free(d->ep_index);
    d->ep_index = calloc((size_t)d->meta.n_videos + 1, sizeof(int));
    d->n_eps = 0;
    int season = d->n_seasons ? d->seasons[idx] : -1;
    for (int i = 0; i < d->meta.n_videos; i++)
        if (d->n_seasons == 0 || d->meta.videos[i].season == season) d->ep_index[d->n_eps++] = i;
    d->ep_focus = 0;
    d->focus_t = 0;
}

static void compute_seasons(Detail *d)
{
    free(d->seasons);
    d->seasons = calloc((size_t)d->meta.n_videos + 1, sizeof(int));
    d->n_seasons = 0;
    for (int i = 0; i < d->meta.n_videos; i++) {
        int s = d->meta.videos[i].season;
        if (s < 0) continue;
        int found = 0;
        for (int j = 0; j < d->n_seasons; j++) if (d->seasons[j] == s) found = 1;
        if (!found) d->seasons[d->n_seasons++] = s;
    }
    for (int i = 1; i < d->n_seasons; i++)
        for (int j = i; j > 0; j--) {
            int a = d->seasons[j - 1], b = d->seasons[j];
            int ka = a == 0 ? 1 << 20 : a, kb = b == 0 ? 1 << 20 : b;
            if (ka > kb) { d->seasons[j - 1] = b; d->seasons[j] = a; } else break;
        }
    int start = 0;
    LibraryItem *li = app_library_find(d->preview.id);
    if (li && li->video_id)
        for (int i = 0; i < d->meta.n_videos; i++)
            if (strcmp(d->meta.videos[i].id, li->video_id) == 0)
                for (int j = 0; j < d->n_seasons; j++) if (d->seasons[j] == d->meta.videos[i].season) start = j;
    select_season(d, start);
    if (li && li->video_id)
        for (int k = 0; k < d->n_eps; k++) if (strcmp(d->meta.videos[d->ep_index[k]].id, li->video_id) == 0) d->ep_focus = k;
}

static void load_resume(Detail *d)
{
    d->resume_label[0] = 0;
    LibraryItem *li = app_library_find(d->preview.id);
    if (!li || li->time_offset == 0) return;
    char key[160];
    snprintf(key, sizeof key, "resume:%s", d->preview.id);
    struct cJSON *o = store_get_json(key);
    const struct cJSON *u = o ? cJSON_GetObjectItemCaseSensitive(o, "url") : NULL;
    const struct cJSON *v = o ? cJSON_GetObjectItemCaseSensitive(o, "video_id") : NULL;
    if (!cJSON_IsString(u)) return;
    if (li->video_id && cJSON_IsString(v) && strcmp(v->valuestring, li->video_id) != 0) return;
    snprintf(d->resume_url, sizeof d->resume_url, "%s", u->valuestring);
    snprintf(d->resume_video, sizeof d->resume_video, "%s", li->video_id ? li->video_id : d->preview.id);
    d->resume_pos = li->time_offset / 1000.0;
    d->resume_frac = li->duration ? (double)li->time_offset / (double)li->duration : 0;
    char t[32];
    ui_format_time(d->resume_pos, t, sizeof t);
    int se = 0, ep = 0;
    const char *c1 = li->video_id ? strchr(li->video_id, ':') : NULL;
    if (c1 && sscanf(c1, ":%d:%d", &se, &ep) == 2) snprintf(d->resume_label, sizeof d->resume_label, "Resume S%d E%d", se, ep);
    else snprintf(d->resume_label, sizeof d->resume_label, "Resume from %s", t);
}

static void on_meta(HttpResponse *r, void *user)
{
    Detail *d = user;
    d->req = 0;
    d->loading = 0;
    if (r->status != 200 || addon_parse_meta(r->body, &d->meta) != 0) {
        snprintf(d->error, sizeof d->error, "Couldn't load details (%s)", r->error[0] ? r->error : "bad response");
        app_redraw();
        return;
    }
    d->has_meta = 1;
    app_remember_videos(d->preview.id, d->meta.videos, d->meta.n_videos);
    MetaPreview merged;
    meta_preview_copy(&merged, &d->meta.p);
    if (!merged.poster && d->preview.poster) merged.poster = strdup(d->preview.poster);
    if (!merged.background && d->preview.background) merged.background = strdup(d->preview.background);
    meta_preview_free(&d->preview);
    d->preview = merged;
    compute_seasons(d);
    load_resume(d);
    app_redraw();
}

static void fetch_meta(Detail *d)
{
    for (int i = 0; i < g_app.n_addons; i++) {
        if (!addon_supports(&g_app.addons[i], "meta", d->preview.type, d->preview.id)) continue;
        char url[1024];
        addon_url_meta(&g_app.addons[i], d->preview.type, d->preview.id, url, sizeof url);
        d->loading = 1;
        d->req = http_request_async(url, NULL, on_meta, d);
        return;
    }
    snprintf(d->error, sizeof d->error, "No installed addon provides details for this title");
}

static int ep_watched(const Detail *d, int k, const LibraryItem *li, int cur_idx)
{
    const MetaVideo *v = &d->meta.videos[d->ep_index[k]];
    if (!li) return 0;
    if (li->video_id && strcmp(li->video_id, v->id) == 0) return li->time_offset == 0 && li->times_watched > 0;
    return cur_idx >= 0 && k < cur_idx;
}

static void update(Screen *s, float dt)
{
    Detail *d = s->st;
    int first = d->ep_focus - 2;
    if (first < 0) first = 0;
    d->rail_x = ui_ease(d->rail_x, first * (EP_W + EP_GAP), NULL, dt, 14);
    d->focus_t = ui_ease(d->focus_t * 100, 100, NULL, dt, 16) / 100;
    d->intro = ui_ease(d->intro * 100, 100, NULL, dt, 9) / 100;
    if (d->ask_open) { d->ask_t = ui_ease(d->ask_t * 100, 100, NULL, dt, 14) / 100; if (d->ask_t < 0.999f || d->ask_req) app_redraw(); }
    if (d->loading) app_redraw();
    /* prefetch upcoming thumbnails */
    for (int k = d->ep_focus; k < d->n_eps && k < d->ep_focus + 6; k++) {
        const char *t = d->meta.videos[d->ep_index[k]].thumbnail;
        if (t) image_get_ex(t, IMAGE_MIPMAP, NULL, NULL, NULL);
    }
}

static void draw(Screen *s)
{
    Detail *d = s->st;
    char buf[512], lbuf[512];
    const char *bg = app_art_background(&d->preview, buf, sizeof buf);
    ui_background(image_tint(bg));
    int iw, ih;
    unsigned bgt = bg ? image_get(bg, &iw, &ih) : 0;
    int series = d->meta.n_videos > 0;
    float k = g_app.fx_light ? 1.0f : d->intro;
    if (bgt) {
        ui_backdrop_fades(1250, series ? 420 : 560, SNTV_UI_H);
        ui_backdrop_image(bgt, iw, ih, k, SNTV_UI_H);
        ui_text_scrim(1300, SNTV_UI_H);
    }

    /* movies sit lower and more cinematic; series leave room for the episode rail */
    float x = THEME_MARGIN, y = (series || d->loading ? 84 : 250) + (1 - k) * 24, col_w = 1000;
    int a = (int)(255 * k);
    const char *logo = app_art_logo(&d->preview, lbuf, sizeof lbuf);
    unsigned ltex = logo ? image_get(logo, &iw, &ih) : 0;
    if (ltex && ih > 0) {
        float w = 640, h = w * ih / iw;
        if (h > 180) { h = 180; w = h * iw / ih; }
        gfx_tex(ltex, x, y + (180 - h), w, h, RGBA(255, 255, 255, a));
        y += 200;
    } else {
        int fs = font_measure(d->preview.name, FONT_TITLE | FONT_B) > col_w ? (FONT_XL | FONT_B) : (FONT_TITLE | FONT_B);
        int lines = font_draw_wrapped(d->preview.name, 0, 0, fs, 0, col_w, 2);
        float ty = y + 180 - lines * font_height(fs);
        font_draw_wrapped(d->preview.name, x + 3, ty + 3, fs, RGBA(0, 0, 0, a / 2), col_w, 2);
        font_draw_wrapped(d->preview.name, x, ty, fs, COLOR_ALPHA(THEME_TEXT, a), col_w, 2);
        y += 200;
    }

    const TmdbInfo *tm = tmdb_get(d->preview.id, d->preview.type);
    float mx = x;
    if (d->preview.imdb_rating && *d->preview.imdb_rating) mx += ui_imdb(d->preview.imdb_rating, mx, y) + 4;
    if (tm && tm->cert[0]) mx += ui_chip_outline(tm->cert, mx, y, RGBA(0xFF, 0xFF, 0xFF, 0x70), THEME_TEXT) + 8;
    const char *parts[4]; int np = 0;
    char seasons_txt[32];
    if (d->preview.release_info) parts[np++] = d->preview.release_info;
    if (d->preview.runtime) parts[np++] = d->preview.runtime;
    if (d->n_seasons) { int real = 0; for (int i = 0; i < d->n_seasons; i++) if (d->seasons[i] > 0) real++;
        snprintf(seasons_txt, sizeof seasons_txt, "%d Season%s", real, real == 1 ? "" : "s"); parts[np++] = seasons_txt; }
    for (int i = 0; i < np; i++) {
        if (mx > x) mx = ui_dot_sep(mx, y, THEME_TEXT_MUTED);
        font_draw(parts[i], mx, y + (36 - font_height(FONT_S | FONT_B)) / 2, FONT_S | FONT_B, THEME_TEXT_DIM);
        mx += font_measure(parts[i], FONT_S | FONT_B);
    }
    y += 52;
    if (d->preview.genres.n) {
        float gx = x;
        for (int g = 0; g < d->preview.genres.n && g < 5; g++) gx += ui_chip_outline(d->preview.genres.v[g], gx, y, RGBA(0xFF, 0xFF, 0xFF, 0x50), THEME_TEXT_DIM) + 10;
        y += 54;
    }
    if (d->preview.description)
        y += font_draw_wrapped(d->preview.description, x, y, FONT_S, THEME_TEXT_DIM, col_w, series ? 2 : 5) * font_height(FONT_S) + 26;
    if (d->loading) { ui_spinner(x + 26, y + 30, 22); y += 70; }
    if (d->error[0]) { font_draw(d->error, x, y, FONT_S, THEME_DANGER); y += 44; }
    /* actions */
    LibraryItem *li = app_library_find(d->preview.id);
    int in_lib = li && !li->removed;
    const char *play_label = d->resume_label[0] ? d->resume_label : series ? "Episodes" : "Play";
    float by = y > 500 ? y : 500;
    if (series && by > 520) by = 520;
    float bw = ui_pill(play_label, 1, d->resume_label[0] || !series ? ICON_PLAY : ICON_CHEVRON_RIGHT, x, by, 72, PILL_PRIMARY,
                       d->area == AREA_ACTIONS && d->btn == 0);
    if (d->resume_label[0] && d->resume_frac > 0) ui_progress(x + 36, by + 82, bw - 72, 5, (float)d->resume_frac, THEME_ACCENT);
    float bx2 = x + bw + 18;
    int cur = d->area == AREA_ACTIONS ? btn_at(d, d->btn) : -1;
    if (d->resume_label[0]) bx2 += ui_pill("From the start", 1, ICON_PREV, bx2, by, 72, PILL_SECONDARY, cur == BTN_RESTART) + 18;
    bx2 += ui_pill(in_lib ? "In Library" : "Add to Library", 1, in_lib ? ICON_CHECK : ICON_PLUS, bx2, by, 72, PILL_SECONDARY, cur == BTN_LIBRARY) + 18;
    if (ai_enabled()) ui_pill("Ask AI", 1, ICON_STAR, bx2, by, 72, PILL_SECONDARY, cur == BTN_ASK);
    /* cast: TMDB photos when a key is set, else the names the addon gave */
    int n_cast = tm && tm->n_cast ? tm->n_cast : d->preview.cast.n;
    if (n_cast && !series) {
        y = by + 104;
        float cx = x;
        for (int i = 0; i < n_cast && i < 6; i++) {
            const char *name = tm && tm->n_cast ? tm->cast[i].name : d->preview.cast.v[i];
            const char *photo = tm && tm->n_cast && tm->cast[i].photo[0] ? tm->cast[i].photo : (i < d->preview.cast_photos.n && d->preview.cast_photos.v[i][0] ? d->preview.cast_photos.v[i] : NULL);
            float w = font_measure(name, FONT_XS | FONT_B) + 70;
            if (cx + w > x + col_w + 200) break;
            gfx_rrect(cx, y, w, 46, 23, RGBA(0xFF, 0xFF, 0xFF, 0x16));
            int pw = 0, ph = 0;
            unsigned ptex = photo ? image_get(photo, &pw, &ph) : 0;
            if (ptex && pw > 0) {
                float ia = (float)pw / ph, u0 = 0, v0 = 0, u1 = 1, v1 = 1;
                if (ia > 1) { u0 = (1 - 1 / ia) / 2; u1 = 1 - u0; } else { v0 = (1 - ia) / 2; v1 = 1 - v0; }
                gfx_rtex_uv(ptex, cx + 5, y + 5, 36, 36, 18, u0, v0, u1, v1, RGBA(255, 255, 255, a), 0);
            } else { gfx_circle(cx + 23, y + 23, 18, COLOR_ALPHA(THEME_ACCENT, a)); char ini[2] = { name[0], 0 }; font_draw(ini, cx + 23 - font_measure(ini, FONT_XS | FONT_B) / 2, y + 23 - font_height(FONT_XS | FONT_B) / 2, FONT_XS | FONT_B, THEME_TEXT); }
            font_draw(name, cx + 50, y + (46 - font_height(FONT_XS | FONT_B)) / 2, FONT_XS | FONT_B, COLOR_ALPHA(THEME_TEXT, a));
            cx += w + 10;
        }
    }


    if (!series) { if (d->ask_open) draw_ask(d); return; }

    /* season tabs */
    float sy = RAIL_Y - 84, sx = x;
    for (int i = 0; i < d->n_seasons; i++) {
        char lbl[32];
        if (d->seasons[i] == 0) snprintf(lbl, sizeof lbl, "Specials"); else snprintf(lbl, sizeof lbl, "Season %d", d->seasons[i]);
        int sel = d->season_idx == i, foc = d->area == AREA_SEASONS && sel;
        float w = font_measure(lbl, FONT_S | FONT_B) + 44;
        if (sx + w > SNTV_UI_W - THEME_MARGIN) break;
        if (foc) gfx_rrect(sx, sy, w, 52, 26, THEME_TEXT);
        else if (sel) gfx_rrect(sx, sy, w, 52, 26, RGBA(0xFF, 0xFF, 0xFF, 0x2C));
        font_draw(lbl, sx + 22, sy + (52 - font_height(FONT_S | FONT_B)) / 2, FONT_S | FONT_B, foc ? THEME_ON_LIGHT : sel ? THEME_TEXT : THEME_TEXT_MUTED);
        sx += w + 8;
    }

    /* episode rail */
    int cur_idx = -1;
    if (li && li->video_id)
        for (int kk = 0; kk < d->n_eps; kk++) if (strcmp(d->meta.videos[d->ep_index[kk]].id, li->video_id) == 0) cur_idx = kk;
    int first = (int)(d->rail_x / (EP_W + EP_GAP)) - 1;
    if (first < 0) first = 0;
    for (int pass = 0; pass < 2; pass++)
        for (int kk = first; kk < d->n_eps && kk < first + 7; kk++) {
            const MetaVideo *v = &d->meta.videos[d->ep_index[kk]];
            float px = x + kk * (EP_W + EP_GAP) - d->rail_x;
            if (px > SNTV_UI_W + 20 || px + EP_W < -20) continue;
            int foc = d->area == AREA_EPISODES && d->ep_focus == kk;
            if ((pass == 1) != foc) continue;
            float f = foc ? (g_app.fx_light ? 1.0f : d->focus_t) : 0;
            int watched = ep_watched(d, kk, li, cur_idx);
            int is_cur = kk == cur_idx;
            int blur = g_app.spoiler_blur && !watched && !is_cur && cur_idx >= 0 ? 1 : (g_app.spoiler_blur && cur_idx < 0 && kk > 0);
            ui_card(v->thumbnail, px, RAIL_Y, EP_W, EP_H, f, d->area == AREA_EPISODES ? 0 : 0.3f, NULL, blur ? CARD_BLUR : CARD_MIP);
            float sc = ui_focus_scale(f), w2 = EP_W * sc, h2 = EP_H * sc, x2 = px - (w2 - EP_W) / 2, y2 = RAIL_Y - (h2 - EP_H) / 2;
            if (!v->thumbnail) { char n[16]; snprintf(n, sizeof n, "%d", v->episode >= 0 ? v->episode : kk + 1);
                font_draw_center(n, x2 + w2 / 2, y2 + h2 / 2 - 40, FONT_TITLE | FONT_B, RGBA(0xFF, 0xFF, 0xFF, 0x40)); }
            if (watched) { gfx_circle(x2 + w2 - 30, y2 + 30, 18, THEME_OK); ui_icon(ICON_CHECK, x2 + w2 - 30, y2 + 30, 22, THEME_ON_LIGHT); }
            if (is_cur && li->duration && li->time_offset) ui_progress(x2 + 16, y2 + h2 - 18, w2 - 32, 6, (float)li->time_offset / (float)li->duration, THEME_ACCENT);
            if (foc) { gfx_circle(x2 + w2 / 2, y2 + h2 / 2, 34, RGBA(0, 0, 0, 0x80)); ui_icon(ICON_PLAY, x2 + w2 / 2 + 3, y2 + h2 / 2, 30, THEME_TEXT); }
            char lbl[256];
            if (v->episode >= 0) snprintf(lbl, sizeof lbl, "E%d  %s", v->episode, v->title ? v->title : "");
            else snprintf(lbl, sizeof lbl, "%s", v->title ? v->title : "Episode");
            float ly = RAIL_Y + EP_H + 16 + (foc ? 12 : 0);
            font_draw_fit(lbl, px, ly, FONT_S | FONT_B, foc || d->area != AREA_EPISODES ? THEME_TEXT : THEME_TEXT_DIM, EP_W);
            if (v->released && !foc) { char date[11]; snprintf(date, sizeof date, "%.10s", v->released); font_draw(date, px, ly + 34, FONT_XS, THEME_TEXT_MUTED); }
        }
    if (d->area == AREA_EPISODES && d->ep_focus < d->n_eps) {
        const MetaVideo *v = &d->meta.videos[d->ep_index[d->ep_focus]];
        char line[600], date[11] = "";
        if (v->released) snprintf(date, sizeof date, "%.10s", v->released);
        snprintf(line, sizeof line, "%s%s%s", date, date[0] && v->overview ? "  \xC2\xB7  " : "", v->overview ? v->overview : "");
        font_draw_fit(line, x, SNTV_UI_H - 64, FONT_S, THEME_TEXT_DIM, SNTV_UI_W - 2 * THEME_MARGIN - 160);
        char pos[32];
        snprintf(pos, sizeof pos, "%d / %d", d->ep_focus + 1, d->n_eps);
        font_draw(pos, SNTV_UI_W - THEME_MARGIN - font_measure(pos, FONT_S | FONT_B), SNTV_UI_H - 64, FONT_S | FONT_B, THEME_TEXT_MUTED);
    }
    if (d->ask_open) draw_ask(d);
}

static void open_streams_for_video(Detail *d, int video_idx)
{
    const MetaVideo *v = &d->meta.videos[video_idx];
    char title[200];
    if (v->season >= 0 && v->episode >= 0) snprintf(title, sizeof title, "S%d E%d  %s", v->season, v->episode, v->title ? v->title : "");
    else snprintf(title, sizeof title, "%s", v->title ? v->title : "");
    if (app_pip_claim(v->id)) return;   /* already playing in the corner */
    app_push(screen_streams_create(&d->preview, v->id, title));
}

static void focus_ep(Detail *d, int k) { if (k != d->ep_focus) { d->ep_focus = k; d->focus_t = 0; } }

static int key(Screen *s, KeyAction a)
{
    Detail *d = s->st;
    app_redraw();
    int series = d->meta.n_videos > 0;
    if (d->ask_open) {
        if (a == KEY_BACK || a == KEY_OK || a == KEY_LONG_OK) d->ask_open = 0;
        return 1;
    }
    if (a == KEY_LONG_OK || a == KEY_YELLOW || a == KEY_MENU || a == KEY_INFO) { screen_options_for_title(&d->preview); return 1; }
    if (d->btn >= n_buttons_d(d)) d->btn = n_buttons_d(d) - 1;
    switch (d->area) {
    case AREA_ACTIONS:
        switch (a) {
        case KEY_LEFT: if (d->btn > 0) d->btn--; return 1;
        case KEY_RIGHT: if (d->btn < n_buttons_d(d) - 1) d->btn++; return 1;
        case KEY_DOWN: if (series) { d->area = d->n_seasons > 1 ? AREA_SEASONS : AREA_EPISODES; d->focus_t = 0; return 1; } return 0;
        case KEY_OK:
            if (btn_at(d, d->btn) == BTN_LIBRARY) { app_library_toggle(&d->preview); return 1; }
            if (btn_at(d, d->btn) == BTN_ASK) { ask_open(d); return 1; }
            if (btn_at(d, d->btn) == BTN_RESTART) {   /* the sources for the resumed video, played from 0:00 */
                const char *vid = d->resume_video[0] ? d->resume_video : d->meta.default_video_id ? d->meta.default_video_id : d->preview.id;
                if (app_pip_claim(vid)) return 1;
                app_push(screen_streams_create(&d->preview, vid, NULL));
                return 1;
            }
            if (d->resume_label[0]) {
                if (app_pip_claim(d->resume_video)) return 1;
                PlayCandidate *c = calloc(1, sizeof *c);
                c->url = strdup(d->resume_url);
                c->label = strdup("Resume");
                char nid[128] = "", nt[200] = "";
                int has_next = app_next_video(d->preview.id, d->resume_video, nid, sizeof nid, nt, sizeof nt);
                app_push(screen_player_create(&d->preview, d->resume_video, d->preview.name, c, 1, 0, d->resume_pos, has_next ? nid : NULL, has_next ? nt : NULL));
                return 1;
            }
            if (series) { d->area = AREA_EPISODES; d->focus_t = 0; return 1; }
            if (!d->has_meta && d->loading) { app_toast("Still loading details..."); return 1; }
            if (app_pip_claim(d->meta.default_video_id ? d->meta.default_video_id : d->preview.id)) return 1;
            app_push(screen_streams_create(&d->preview, d->meta.default_video_id ? d->meta.default_video_id : d->preview.id, NULL));
            return 1;
        default: return 0;
        }
    case AREA_SEASONS:
        switch (a) {
        case KEY_LEFT: if (d->season_idx > 0) select_season(d, d->season_idx - 1); return 1;
        case KEY_RIGHT: if (d->season_idx < d->n_seasons - 1) select_season(d, d->season_idx + 1); return 1;
        case KEY_UP: d->area = AREA_ACTIONS; return 1;
        case KEY_DOWN: case KEY_OK: if (d->n_eps) { d->area = AREA_EPISODES; d->focus_t = 0; return 1; } return a == KEY_OK;
        case KEY_BACK: d->area = AREA_ACTIONS; return 1;
        default: return 0;
        }
    default:
        switch (a) {
        case KEY_LEFT: if (d->ep_focus > 0) focus_ep(d, d->ep_focus - 1); return 1;
        case KEY_RIGHT: if (d->ep_focus < d->n_eps - 1) focus_ep(d, d->ep_focus + 1); return 1;
        case KEY_UP: d->area = d->n_seasons > 1 ? AREA_SEASONS : AREA_ACTIONS; return 1;
        case KEY_DOWN: return 0;   /* bottom of the page: main.c may hand focus to the corner player */
        case KEY_OK: if (d->n_eps) open_streams_for_video(d, d->ep_index[d->ep_focus]); return 1;
        case KEY_BACK: d->area = AREA_ACTIONS; return 1;
        default: return 0;
        }
    }
}

static void resume(Screen *s) { Detail *d = s->st; load_resume(d); app_redraw(); }

static void destroy(Screen *s)
{
    Detail *d = s->st;
    if (d->req) http_cancel(d->req);
    if (d->ask_req) ai_cancel(d->ask_req);
    ask_clear(d);
    meta_preview_free(&d->preview);
    if (d->has_meta) meta_item_free(&d->meta);
    free(d->seasons); free(d->ep_index);
    free(d);
    free(s);
}

Screen *screen_detail_create(const MetaPreview *p)
{
    Screen *s = calloc(1, sizeof *s);
    Detail *d = calloc(1, sizeof *d);
    meta_preview_copy(&d->preview, p);
    d->focus_t = 1;
    s->name = "detail"; s->st = d; s->long_ok = 1;
    s->update = update; s->draw = draw; s->key = key; s->destroy = destroy; s->resume = resume;
    fetch_meta(d);
    load_resume(d);
    return s;
}
