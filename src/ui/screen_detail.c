#include "../app.h"
#include "../http.h"
#include "../log.h"
#include "../store.h"
#include "font.h"
#include "theme.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { AREA_ACTIONS, AREA_SEASONS, AREA_EPISODES };
enum { BTN_PLAY, BTN_LIBRARY, BTN_COUNT };
enum { EP_ROW_H = 84, EP_VISIBLE = 5 };

typedef struct {
    MetaPreview preview;
    MetaItem meta; int has_meta;
    int loading; unsigned req; char error[400];
    int *seasons; int n_seasons; int season_idx;
    int *ep_index; int n_eps; int ep_focus;   /* indexes into meta.videos for the selected season */
    int area, btn;
    char resume_label[96];
    char resume_url[1024], resume_video[128];
    double resume_pos;
} Detail;

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
    /* ascending, specials (0) last like Stremio */
    for (int i = 1; i < d->n_seasons; i++)
        for (int j = i; j > 0; j--) {
            int a = d->seasons[j - 1], b = d->seasons[j];
            int ka = a == 0 ? 1 << 20 : a, kb = b == 0 ? 1 << 20 : b;
            if (ka > kb) { d->seasons[j - 1] = b; d->seasons[j] = a; } else break;
        }
    /* jump to the season of the last watched episode */
    int start = 0;
    LibraryItem *li = app_library_find(d->preview.id);
    if (li && li->video_id) {
        for (int i = 0; i < d->meta.n_videos; i++)
            if (strcmp(d->meta.videos[i].id, li->video_id) == 0)
                for (int j = 0; j < d->n_seasons; j++) if (d->seasons[j] == d->meta.videos[i].season) start = j;
    }
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
    char t[32];
    ui_format_time(d->resume_pos, t, sizeof t);
    snprintf(d->resume_label, sizeof d->resume_label, "Resume  %s", t);
}

static void on_meta(HttpResponse *r, void *user)
{
    Detail *d = user;
    d->req = 0;
    d->loading = 0;
    if (r->status != 200 || addon_parse_meta(r->body, &d->meta) != 0) {
        snprintf(d->error, sizeof d->error, "Could not load details (%s)", r->error[0] ? r->error : "bad response");
        return;
    }
    d->has_meta = 1;
    /* richer fields from the full meta */
    MetaPreview merged;
    meta_preview_copy(&merged, &d->meta.p);
    if (!merged.poster && d->preview.poster) merged.poster = strdup(d->preview.poster);
    meta_preview_free(&d->preview);
    d->preview = merged;
    compute_seasons(d);
    load_resume(d);
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
    snprintf(d->error, sizeof d->error, "No installed addon provides details for this item");
}

static const char *video_label(const MetaVideo *v, char *buf, size_t n)
{
    if (v->season >= 0 && v->episode >= 0) snprintf(buf, n, "S%d E%d", v->season, v->episode);
    else if (v->episode >= 0) snprintf(buf, n, "E%d", v->episode);
    else snprintf(buf, n, "-");
    return buf;
}

static void draw(Screen *s)
{
    Detail *d = s->st;
    gfx_rect(0, 0, SNTV_UI_W, SNTV_UI_H, THEME_BG);
    int iw, ih;
    unsigned bg = d->preview.background ? image_get(d->preview.background, &iw, &ih) : 0;
    if (bg) gfx_tex(bg, 0, 0, SNTV_UI_W, SNTV_UI_H, RGBA(255, 255, 255, 255));
    gfx_gradient_h(0, 0, SNTV_UI_W * 0.7f, SNTV_UI_H, RGBA(0x0F, 0x0D, 0x20, 0xF0), RGBA(0x0F, 0x0D, 0x20, 0x20));
    gfx_gradient_v(0, SNTV_UI_H * 0.45f, SNTV_UI_W, SNTV_UI_H * 0.55f, RGBA(0x0F, 0x0D, 0x20, 0x00), RGBA(0x0F, 0x0D, 0x20, 0xFF));

    float x = THEME_MARGIN, y = 90, col_w = 1000;
    unsigned logo = d->preview.logo ? image_get(d->preview.logo, &iw, &ih) : 0;
    if (logo && ih > 0) {
        float lw = 480, lh = lw * ih / iw;
        if (lh > 180) { lh = 180; lw = lh * iw / ih; }
        gfx_tex(logo, x, y, lw, lh, RGBA(255, 255, 255, 255));
        y += lh + 24;
    } else {
        y += font_draw_wrapped(d->preview.name, x, y, FONT_TITLE, THEME_TEXT, col_w, 2) * font_height(FONT_TITLE) + 10;
    }
    char info[300] = "";
    const char *parts[4] = { d->preview.release_info, d->preview.runtime, NULL, NULL };
    char rating[32] = "";
    if (d->preview.imdb_rating) { snprintf(rating, sizeof rating, "IMDb %s", d->preview.imdb_rating); parts[2] = rating; }
    for (int i = 0; i < 3; i++) if (parts[i]) snprintf(info + strlen(info), sizeof info - strlen(info), "%s%s", info[0] ? "   " : "", parts[i]);
    for (int g = 0; g < d->preview.genres.n && g < 4; g++) snprintf(info + strlen(info), sizeof info - strlen(info), "%s%s", info[0] ? "   " : "", d->preview.genres.v[g]);
    font_draw_fit(info, x, y, FONT_S, THEME_TEXT_DIM, col_w);
    y += 48;
    if (d->preview.description) y += font_draw_wrapped(d->preview.description, x, y, FONT_S, THEME_TEXT, col_w, d->n_seasons || d->meta.n_videos ? 4 : 8) * font_height(FONT_S) + 24;
    if (d->loading) { ui_draw_spinner(x + 30, y + 30); y += 70; }
    if (d->error[0]) { font_draw(d->error, x, y, FONT_S, THEME_DANGER); y += 44; }

    /* actions */
    const char *play_label = d->resume_label[0] ? d->resume_label : (d->meta.n_videos ? "Episodes" : "Streams");
    LibraryItem *li = app_library_find(d->preview.id);
    int in_lib = li && !li->removed;
    float bx = x;
    ui_draw_button(play_label, bx, y, 300, 64, d->area == AREA_ACTIONS && d->btn == BTN_PLAY, 1);
    bx += 316;
    ui_draw_button(in_lib ? "Remove from Library" : "Add to Library", bx, y, 340, 64, d->area == AREA_ACTIONS && d->btn == BTN_LIBRARY, 0);
    y += 100;

    if (!d->meta.n_videos) return;

    /* seasons */
    float sy = y > 560 ? y : 560;
    if (d->n_seasons) {
        float sx = x;
        for (int i = 0; i < d->n_seasons; i++) {
            char lbl[32];
            if (d->seasons[i] == 0) snprintf(lbl, sizeof lbl, "Specials"); else snprintf(lbl, sizeof lbl, "Season %d", d->seasons[i]);
            float w = font_measure(lbl, FONT_S) + 36;
            if (sx + w > SNTV_UI_W - THEME_MARGIN) break;
            int sel = d->season_idx == i, foc = d->area == AREA_SEASONS && sel;
            gfx_rect(sx, sy, w, 48, foc ? THEME_ACCENT : sel ? THEME_SURFACE_HI : THEME_SURFACE);
            font_draw(lbl, sx + 18, sy + 10, FONT_S, THEME_TEXT);
            sx += w + 10;
        }
        sy += 70;
    }
    /* episodes */
    int first = d->ep_focus - (EP_VISIBLE - 1);
    if (first < 0) first = 0;
    float ey = sy;
    float list_w = SNTV_UI_W - 2 * THEME_MARGIN;
    for (int k = first; k < d->n_eps && k < first + EP_VISIBLE; k++) {
        const MetaVideo *v = &d->meta.videos[d->ep_index[k]];
        int foc = d->area == AREA_EPISODES && d->ep_focus == k;
        gfx_rect(x, ey, list_w, EP_ROW_H - 8, foc ? THEME_SURFACE_HI : RGBA(0x1E, 0x1B, 0x38, 0xB0));
        if (foc) gfx_rect(x, ey, 6, EP_ROW_H - 8, THEME_ACCENT);
        char tag[24];
        font_draw(video_label(v, tag, sizeof tag), x + 24, ey + 22, FONT_M, THEME_TEXT_DIM);
        font_draw_fit(v->title ? v->title : "Untitled", x + 150, ey + 22, FONT_M, THEME_TEXT, list_w - 420);
        if (v->released) { char date[11]; snprintf(date, sizeof date, "%.10s", v->released); font_draw(date, x + list_w - 160, ey + 26, FONT_S, THEME_TEXT_MUTED); }
        if (li && li->video_id && strcmp(li->video_id, v->id) == 0 && li->duration) {
            float frac = (float)li->time_offset / (float)li->duration;
            gfx_rect(x, ey + EP_ROW_H - 14, list_w, 6, RGBA(0, 0, 0, 120));
            gfx_rect(x, ey + EP_ROW_H - 14, list_w * frac, 6, THEME_ACCENT);
        }
        ey += EP_ROW_H;
    }
    if (d->n_eps > EP_VISIBLE) {
        char pos[48];
        snprintf(pos, sizeof pos, "%d / %d", d->ep_focus + 1, d->n_eps);
        font_draw(pos, x + list_w - font_measure(pos, FONT_S), ey + 4, FONT_S, THEME_TEXT_MUTED);
    }
}

static void open_streams_for_video(Detail *d, int video_idx)
{
    const MetaVideo *v = &d->meta.videos[video_idx];
    char title[200], tag[24];
    snprintf(title, sizeof title, "%s - %s", video_label(v, tag, sizeof tag), v->title ? v->title : "");
    app_push(screen_streams_create(&d->preview, v->id, title));
}

static int key(Screen *s, KeyAction a)
{
    Detail *d = s->st;
    switch (d->area) {
    case AREA_ACTIONS:
        switch (a) {
        case KEY_LEFT: if (d->btn > 0) d->btn--; return 1;
        case KEY_RIGHT: if (d->btn < BTN_COUNT - 1) d->btn++; return 1;
        case KEY_DOWN: if (d->meta.n_videos) d->area = d->n_seasons ? AREA_SEASONS : AREA_EPISODES; return 1;
        case KEY_OK:
            if (d->btn == BTN_LIBRARY) { app_library_toggle(&d->preview); return 1; }
            if (d->resume_label[0]) { app_push(screen_player_create(&d->preview, d->resume_video, d->preview.name, d->resume_url, d->resume_pos)); return 1; }
            if (d->meta.n_videos) { d->area = AREA_EPISODES; return 1; }
            if (!d->has_meta && d->loading) { app_toast("Still loading details..."); return 1; }
            app_push(screen_streams_create(&d->preview, d->meta.default_video_id ? d->meta.default_video_id : d->preview.id, NULL));
            return 1;
        default: return 0;
        }
    case AREA_SEASONS:
        switch (a) {
        case KEY_LEFT: if (d->season_idx > 0) select_season(d, d->season_idx - 1); return 1;
        case KEY_RIGHT: if (d->season_idx < d->n_seasons - 1) select_season(d, d->season_idx + 1); return 1;
        case KEY_UP: d->area = AREA_ACTIONS; return 1;
        case KEY_DOWN: case KEY_OK: if (d->n_eps) d->area = AREA_EPISODES; return 1;
        case KEY_BACK: d->area = AREA_ACTIONS; return 1;
        default: return 0;
        }
    default:
        switch (a) {
        case KEY_UP: if (d->ep_focus > 0) d->ep_focus--; else d->area = d->n_seasons ? AREA_SEASONS : AREA_ACTIONS; return 1;
        case KEY_DOWN: if (d->ep_focus < d->n_eps - 1) d->ep_focus++; return 1;
        case KEY_LEFT: if (d->n_seasons && d->season_idx > 0) select_season(d, d->season_idx - 1); return 1;
        case KEY_RIGHT: if (d->n_seasons && d->season_idx < d->n_seasons - 1) select_season(d, d->season_idx + 1); return 1;
        case KEY_OK: if (d->n_eps) open_streams_for_video(d, d->ep_index[d->ep_focus]); return 1;
        case KEY_BACK: d->area = AREA_ACTIONS; return 1;
        default: return 0;
        }
    }
}

static void resume(Screen *s) { Detail *d = s->st; load_resume(d); }

static void destroy(Screen *s)
{
    Detail *d = s->st;
    if (d->req) http_cancel(d->req);
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
    s->name = "detail"; s->st = d;
    s->draw = draw; s->key = key; s->destroy = destroy; s->resume = resume;
    fetch_meta(d);
    load_resume(d);
    return s;
}
