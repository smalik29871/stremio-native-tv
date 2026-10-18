/* Player: transparent hole for the platform video plane, overlay controls,
 * addon + embedded subtitles, audio/subtitle menu, auto-fallback across stream
 * candidates, Up Next at the credits, and progress sync to the Stremio library. */
#include "../app.h"
#include "../http.h"
#include "../log.h"
#include "../store.h"
#include "../subs.h"
#include "../video/video.h"
#include "../platform/platform.h"
#include "font.h"
#include "theme.h"
#include "widgets.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { MAX_ADDON_SUBS = 64, MAX_TRACKS = 16 };
enum { MENU_NONE, MENU_AUDIO, MENU_SUBS };

typedef struct AddonSub { char *url, *lang, *addon; } AddonSub;

typedef struct Player Player;
typedef struct { Player *owner; unsigned gen; int addon; } SubReq;

struct Player {
    MetaPreview meta;
    char *video_id, *title;
    PlayCandidate *cands; int n_cands, cur;
    int tries;
    char *next_video_id, *next_title;

    float overlay_until;
    float since_report;
    double last_reported;
    int finished, up_next_shown, up_next_accepted;
    int seek_steps; float seek_decay;
    double seek_target; float seek_commit;   /* debounced seek while the key repeats */
    float load_started;
    int error_handled;

    /* subtitles */
    AddonSub addon_subs[MAX_ADDON_SUBS]; int n_addon_subs;
    int subs_pending;
    unsigned gen;
    SubTrack track; int track_loaded; int active_addon_sub;   /* -1 none */
    unsigned sub_req;
    int embedded_text_sel;    /* -1 off */
    int auto_selected;        /* audio preference applied */
    int subs_auto_done;       /* subtitle preference applied (or given up) */
    int last_tracks_version;
    int last_warning_version;

    int menu, menu_focus;
    float play_flash;          /* big centre icon after play/pause */
    int flash_icon;
};

static Player *g_live;

static void show_overlay(Player *p) { p->overlay_until = g_app.time + 4.0f; app_redraw(); }

static void report(Player *p, int force)
{
    VideoInfo vi;
    video_info(&vi);
    if (vi.state == VIDEO_IDLE || vi.state == VIDEO_LOADING || vi.state == VIDEO_ERROR) return;
    if (!force && vi.position == p->last_reported) return;
    p->last_reported = vi.position;
    app_library_progress(&p->meta, p->video_id, vi.position, vi.duration);
}

/* ---- addon subtitles ---------------------------------------------------------- */

static void on_addon_subs(HttpResponse *r, void *user)
{
    SubReq *q = user;
    Player *p = q->owner;
    if (p != g_live || q->gen != p->gen) { free(q); return; }
    p->subs_pending--;
    Subtitle *subs; int n;
    if (r->status == 200 && addon_parse_subtitles(r->body, &subs, &n) == 0) {
        const char *addon_name = q->addon < g_app.n_addons ? g_app.addons[q->addon].manifest.name : "addon";
        for (int i = 0; i < n && p->n_addon_subs < MAX_ADDON_SUBS; i++) {
            AddonSub *as = &p->addon_subs[p->n_addon_subs++];
            as->url = strdup(subs[i].url);
            as->lang = strdup(subs[i].lang ? subs[i].lang : "und");
            as->addon = strdup(addon_name);
        }
        subtitles_free(subs, n);
        app_redraw();
    }
    free(q);
}

static void query_addon_subs(Player *p)
{
    const PlayCandidate *c = &p->cands[p->cur];
    char extra[700] = "";
    if (c->filename || c->video_size) {
        char size[32];
        snprintf(size, sizeof size, "%llu", (unsigned long long)c->video_size);
        const char *kv[6]; int k = 0;
        if (c->filename) { kv[k++] = "filename"; kv[k++] = c->filename; }
        if (c->video_size) { kv[k++] = "videoSize"; kv[k++] = size; }
        kv[k] = NULL;
        addon_extra_query(extra, sizeof extra, kv);
    }
    for (int i = 0; i < g_app.n_addons; i++) {
        if (!addon_supports(&g_app.addons[i], "subtitles", p->meta.type, p->video_id)) continue;
        char url[1400];
        addon_url_subtitles(&g_app.addons[i], p->meta.type, p->video_id, extra[0] ? extra : NULL, url, sizeof url);
        SubReq *q = malloc(sizeof *q);
        q->owner = p; q->gen = p->gen; q->addon = i;
        p->subs_pending++;
        http_request_async(url, NULL, on_addon_subs, q);
    }
}

static void on_sub_file(HttpResponse *r, void *user)
{
    SubReq *q = user;
    Player *p = q->owner;
    if (p != g_live || q->gen != p->gen) { free(q); return; }
    p->sub_req = 0;
    subs_free(&p->track);
    p->track_loaded = 0;
    if (r->status == 200 && subs_parse(r->body, r->len, &p->track) > 0) {
        p->track_loaded = 1;
        LOGI("subs: loaded %d cues", p->track.n);
    } else {
        app_toast("Could not load that subtitle file");
        p->active_addon_sub = -1;
    }
    app_redraw();
    free(q);
}

static void select_addon_sub(Player *p, int idx)
{
    p->active_addon_sub = idx;
    p->embedded_text_sel = -1;
    video_select_track(TRACK_TEXT, -1);   /* avoid double subtitles */
    subs_free(&p->track);
    p->track_loaded = 0;
    if (idx < 0) return;
    SubReq *q = malloc(sizeof *q);
    q->owner = p; q->gen = p->gen; q->addon = -1;
    p->sub_req = http_request_async(p->addon_subs[idx].url, NULL, on_sub_file, q);
}

static int lang_match(const char *a, const char *b)
{
    if (!a || !b || !*a || !*b) return 0;
    return strncmp(a, b, 2) == 0 || strcmp(a, b) == 0;
}

/* Preferred languages, applied once per stream. Audio as soon as the pipeline
 * reports tracks; subtitles prefer an embedded match, else wait (up to 15 s) for
 * subtitle addons to answer and take the first matching one. */
static void auto_select_tracks(Player *p)
{
    VideoTrack tr[MAX_TRACKS];
    if (!p->auto_selected) {
        int n = video_tracks(TRACK_AUDIO, tr, MAX_TRACKS);
        if (n > 0 || g_app.time - p->load_started > 10) {
            p->auto_selected = 1;
            if (g_app.pref_audio[0])
                for (int i = 0; i < n; i++)
                    if (lang_match(tr[i].lang, g_app.pref_audio)) { if (!tr[i].selected) video_select_track(TRACK_AUDIO, tr[i].index); break; }
        }
    }
    if (p->subs_auto_done) return;
    if (!g_app.pref_subs[0]) { p->subs_auto_done = 1; return; }
    int nt = video_tracks(TRACK_TEXT, tr, MAX_TRACKS);
    for (int i = 0; i < nt; i++)
        if (lang_match(tr[i].lang, g_app.pref_subs)) {
            video_select_track(TRACK_TEXT, tr[i].index);
            p->embedded_text_sel = tr[i].index;
            p->subs_auto_done = 1;
            return;
        }
    for (int i = 0; i < p->n_addon_subs; i++)
        if (lang_match(p->addon_subs[i].lang, g_app.pref_subs)) { select_addon_sub(p, i); p->subs_auto_done = 1; return; }
    if (p->subs_pending == 0 || g_app.time - p->load_started > 15) p->subs_auto_done = 1;
}

/* ---- playback / fallback ------------------------------------------------------ */

static void start_candidate(Player *p, int idx, double start)
{
    p->cur = idx;
    p->error_handled = 0;
    p->auto_selected = 0;
    p->subs_auto_done = 0;
    p->load_started = g_app.time;
    LOGI("player: candidate %d/%d %s @%.0fs -> %s", idx + 1, p->n_cands, p->cands[idx].label ? p->cands[idx].label : "", start, p->cands[idx].url);
    if (video_load(p->cands[idx].url, p->title, start) != 0) app_toast("Could not start playback");
    show_overlay(p);
}

static void fail_over(Player *p, const char *why)
{
    VideoInfo vi;
    video_info(&vi);
    double resume = vi.position > 5 ? vi.position : 0;
    if (p->cur + 1 < p->n_cands && p->tries < 3) {
        p->tries++;
        app_toast("%s. Trying the next source...", why);
        start_candidate(p, p->cur + 1, resume);
    } else {
        p->error_handled = 1;   /* stay on the error screen */
    }
}

static void go_up_next(Player *p)
{
    report(p, 1);
    video_stop();
    MetaPreview meta;
    meta_preview_copy(&meta, &p->meta);
    char *vid = strdup(p->next_video_id), *title = p->next_title ? strdup(p->next_title) : NULL;
    char *group = p->cands[p->cur].binge_group ? strdup(p->cands[p->cur].binge_group) : NULL;
    p->finished = 1;
    app_pop();   /* destroys this screen */
    app_replace(screen_streams_create_auto(&meta, vid, title, group));
    meta_preview_free(&meta);
    free(vid); free(title); free(group);
}

static void update(Screen *s, float dt)
{
    Player *p = s->st;
    if (p->finished) return;
    VideoInfo vi;
    video_info(&vi);
    if (vi.state == VIDEO_PLAYING) app_redraw();   /* progress bar / subtitles move */
    p->since_report += dt;
    if (p->since_report > 30.0f && vi.state == VIDEO_PLAYING) { p->since_report = 0; report(p, 0); }
    p->seek_decay -= dt;
    if (p->seek_decay <= 0) p->seek_steps = 0;
    if (p->seek_commit > 0) { p->seek_commit -= dt; if (p->seek_commit <= 0) video_seek(p->seek_target); }

    if (vi.tracks_version != p->last_tracks_version) { p->last_tracks_version = vi.tracks_version; app_redraw(); }
    if (vi.warning_version != p->last_warning_version) {
        p->last_warning_version = vi.warning_version;
        if (vi.warning[0]) app_toast("%s", vi.warning);
    }
    if (vi.state == VIDEO_PLAYING || vi.state == VIDEO_PAUSED) auto_select_tracks(p);

    if (vi.state == VIDEO_ERROR && !p->error_handled) { p->error_handled = 1; fail_over(p, vi.error[0] ? vi.error : "Playback failed"); }
    if (vi.state == VIDEO_LOADING && g_app.time - p->load_started > 45.0f && !p->error_handled) { p->error_handled = 1; fail_over(p, "Source is not responding"); }

    int near_end = vi.duration > 120 && vi.position > vi.duration - 45;
    if (p->next_video_id && near_end && !p->up_next_shown) { p->up_next_shown = 1; show_overlay(p); }
    if (vi.state == VIDEO_ENDED) {
        if (p->next_video_id && g_app.autoplay_next) { go_up_next(p); return; }
        p->finished = 1;
        report(p, 1);
        app_pop();
    }
}

/* ---- drawing ------------------------------------------------------------------ */

static void draw_subtitle(Player *p, const VideoInfo *vi)
{
    const char *text = NULL;
    if (p->track_loaded) text = subs_at(&p->track, vi->position);
    if (!text) return;
    int size = (g_app.subs_size == 0 ? FONT_M : g_app.subs_size == 2 ? FONT_XL : FONT_L) | FONT_B;
    float max_w = SNTV_UI_W * 0.78f, lh = font_height(size) * 1.04f;
    int lines = font_draw_wrapped(text, 0, 0, size, 0, max_w, 3);
    float bottom = g_app.time < p->overlay_until ? SNTV_UI_H - 250 : SNTV_UI_H - 96;
    float y = bottom - lines * lh;
    char tmp[512];
    snprintf(tmp, sizeof tmp, "%s", text);
    int i = 0;
    for (char *l = strtok(tmp, "\n"); l && i < lines; l = strtok(NULL, "\n"), i++) {
        float lw = font_measure(l, size);
        if (lw > max_w) lw = max_w;
        float lx = (SNTV_UI_W - lw) / 2;
        /* soft shadow + outline: readable on any picture without a box */
        font_draw_fit(l, lx + 3, y + i * lh + 3, size, RGBA(0, 0, 0, 0xB0), max_w);
        for (int dx = -2; dx <= 2; dx += 2) for (int dy = -2; dy <= 2; dy += 2)
            if (dx || dy) font_draw_fit(l, lx + dx, y + i * lh + dy, size, RGBA(0, 0, 0, 0x90), max_w);
        font_draw_fit(l, lx, y + i * lh, size, RGBA(255, 255, 255, 255), max_w);
    }
}

static void draw_menu(Player *p)
{
    float w = 600, x = SNTV_UI_W - w - 40, y = 40, h = SNTV_UI_H - 80;
    if (!g_app.fx_light) gfx_shadow(x, y, w, h, R_PANEL, 50, RGBA(0, 0, 0, 0xA0));
    gfx_rrect(x, y, w, h, R_PANEL, RGBA(0x14, 0x12, 0x2A, 0xF0));
    gfx_rrect_ring(x, y, w, h, R_PANEL, 1.5f, RGBA(0xFF, 0xFF, 0xFF, 0x1C));
    ui_icon(p->menu == MENU_AUDIO ? ICON_AUDIO : ICON_SUBS, x + 56, y + 62, 40, THEME_TEXT);
    font_draw(p->menu == MENU_AUDIO ? "Audio" : "Subtitles", x + 92, y + 40, FONT_L | FONT_B, THEME_TEXT);
    float ry = y + 120;
    int row = 0;
    VideoTrack tr[MAX_TRACKS];
    int n = video_tracks(p->menu == MENU_AUDIO ? TRACK_AUDIO : TRACK_TEXT, tr, MAX_TRACKS);
    #define SECTION(t) do { font_draw(t, x + 40, ry + 6, FONT_XS | FONT_B, THEME_TEXT_MUTED); ry += 40; } while (0)
    #define ROW(label, sub, on) do { \
        int foc_ = p->menu_focus == row; const char *sub_ = (sub); \
        if (ry + 70 < y + h - 70) { \
        if (foc_) gfx_rrect(x + 24, ry, w - 48, 70, 18, THEME_TEXT); \
        else if (on) gfx_rrect(x + 24, ry, w - 48, 70, 18, RGBA(0xFF, 0xFF, 0xFF, 0x14)); \
        Color fg_ = foc_ ? THEME_ON_LIGHT : THEME_TEXT; \
        font_draw_fit(label, x + 50, ry + (sub_[0] ? 10 : 20), FONT_S | FONT_B, fg_, w - 190); \
        if (sub_[0]) font_draw_fit(sub_, x + 50, ry + 40, FONT_XS, foc_ ? RGBA(0x0B, 0x0A, 0x16, 0xB0) : THEME_TEXT_MUTED, w - 190); \
        if (on) { gfx_circle(x + w - 70, ry + 35, 17, foc_ ? THEME_ON_LIGHT : THEME_ACCENT); ui_icon(ICON_CHECK, x + w - 70, ry + 35, 20, foc_ ? THEME_TEXT : THEME_TEXT); } } \
        ry += 78; row++; } while (0)
    if (p->menu == MENU_SUBS) ROW("Off", "", p->embedded_text_sel < 0 && p->active_addon_sub < 0);
    if (n) SECTION(p->menu == MENU_AUDIO ? "IN THIS FILE" : "EMBEDDED");
    for (int i = 0; i < n; i++) {
        char sub[80];
        snprintf(sub, sizeof sub, "%s%s%s", tr[i].codec, tr[i].codec[0] && tr[i].lang[0] ? "  \xC2\xB7  " : "", tr[i].lang);
        int on = p->menu == MENU_AUDIO ? tr[i].selected : p->embedded_text_sel == tr[i].index;
        ROW(tr[i].label, sub, on);
    }
    if (p->menu == MENU_AUDIO && !n) font_draw_wrapped("The stream reports a single audio track.", x + 40, ry, FONT_S, THEME_TEXT_MUTED, w - 80, 2);
    if (p->menu == MENU_SUBS) {
        if (p->n_addon_subs || p->subs_pending) SECTION(p->subs_pending ? "FROM ADDONS  (SEARCHING)" : "FROM ADDONS");
        for (int i = 0; i < p->n_addon_subs; i++) ROW(p->addon_subs[i].lang, p->addon_subs[i].addon, p->active_addon_sub == i);
        if (p->track_loaded) {
            char off[64];
            snprintf(off, sizeof off, "Sync  %+.1fs", p->track.offset);
            gfx_rrect(x + 24, y + h - 76, w - 48, 52, 16, RGBA(0xFF, 0xFF, 0xFF, 0x10));
            ui_icon(ICON_CHEVRON_LEFT, x + 60, y + h - 50, 22, THEME_TEXT_DIM);
            font_draw_center(off, x + w / 2, y + h - 66, FONT_S | FONT_B, THEME_TEXT);
            ui_icon(ICON_CHEVRON_RIGHT, x + w - 60, y + h - 50, 22, THEME_TEXT_DIM);
        }
    }
    #undef ROW
    #undef SECTION
}

static void draw_up_next(Player *p, const VideoInfo *vi, int overlay)
{
    float w = 640, h = 176, x = SNTV_UI_W - THEME_MARGIN - w, y = overlay ? SNTV_UI_H - 400 : SNTV_UI_H - 240;
    if (!g_app.fx_light) gfx_shadow(x, y, w, h, 24, 40, RGBA(0, 0, 0, 0xA0));
    gfx_rrect(x, y, w, h, 24, RGBA(0x16, 0x13, 0x2E, 0xF2));
    gfx_rrect_ring(x, y, w, h, 24, 1.5f, RGBA(0xFF, 0xFF, 0xFF, 0x20));
    const char *thumb = app_video_thumbnail(p->meta.id, p->next_video_id);
    ui_card(thumb, x + 16, y + 16, 256, 144, 0, 0, NULL, CARD_MIP);
    if (!thumb) ui_icon(ICON_NEXT, x + 16 + 128, y + 16 + 72, 50, THEME_TEXT_MUTED);
    float tx = x + 296;
    font_draw("UP NEXT", tx, y + 24, FONT_XS | FONT_B, THEME_ACCENT2);
    font_draw_wrapped(p->next_title ? p->next_title : "Next episode", tx, y + 54, FONT_S | FONT_B, THEME_TEXT, w - 320, 2);
    if (g_app.autoplay_next && vi->duration > 0) {
        double left = vi->duration - vi->position;
        char t[48]; snprintf(t, sizeof t, "Starts in %ds", (int)(left < 0 ? 0 : left));
        font_draw(t, tx, y + h - 54, FONT_XS, THEME_TEXT_DIM);
        ui_progress(tx, y + h - 22, w - 320, 6, (float)(1.0 - left / 45.0), THEME_ACCENT);
    }
    float gx = x + w - 140;
    gfx_rrect(gx, y + h - 64, 120, 40, 20, RGBA(0x22, 0xC5, 0x5E, 0xFF));
    font_draw_center("Play now", gx + 60, y + h - 64 + (40 - font_height(FONT_XS | FONT_B)) / 2, FONT_XS | FONT_B, THEME_ON_LIGHT);
}

static void draw(Screen *s)
{
    Player *p = s->st;
    VideoInfo vi;
    video_info(&vi);
#ifdef SNTV_PLATFORM_LINUX
    /* desktop preview: show the backdrop where the video would be */
    char buf[512];
    const char *bg = app_art_background(&p->meta, buf, sizeof buf);
    int iw, ih;
    unsigned bgt = bg ? image_get(bg, &iw, &ih) : 0;
    gfx_rect(0, 0, SNTV_UI_W, SNTV_UI_H, RGBA(0, 0, 0, 0xFF));
    if (bgt) {
        float ia = (float)iw / ih, sa = (float)SNTV_UI_W / SNTV_UI_H, u0 = 0, v0 = 0, u1 = 1, v1 = 1;
        if (ia > sa) { float k = sa / ia; u0 = (1 - k) / 2; u1 = 1 - u0; } else { float k = ia / sa; v0 = (1 - k) / 2; v1 = 1 - v0; }
        gfx_tex_uv(bgt, 0, 0, SNTV_UI_W, SNTV_UI_H, u0, v0, u1, v1, RGBA(255, 255, 255, 255));
    }
#else
    gfx_video_hole(0, 0, SNTV_UI_W, SNTV_UI_H);
#endif
    draw_subtitle(p, &vi);

    int overlay = g_app.time < p->overlay_until || vi.state == VIDEO_PAUSED || vi.state == VIDEO_LOADING;
    if (vi.state == VIDEO_ERROR && p->error_handled && (p->cur + 1 >= p->n_cands || p->tries >= 3)) {
        gfx_rect(0, 0, SNTV_UI_W, SNTV_UI_H, RGBA(0x08, 0x07, 0x12, 0xD8));
        float w = 900, h = 360, x = (SNTV_UI_W - w) / 2, y = (SNTV_UI_H - h) / 2;
        gfx_rrect(x, y, w, h, R_PANEL, THEME_PANEL);
        gfx_circle(x + w / 2, y + 70, 36, COLOR_ALPHA(THEME_DANGER, 0x40));
        gfx_line(x + w / 2 - 12, y + 58, x + w / 2 + 12, y + 82, 6, THEME_DANGER);
        gfx_line(x + w / 2 + 12, y + 58, x + w / 2 - 12, y + 82, 6, THEME_DANGER);
        font_draw_center("This source can't be played", x + w / 2, y + 128, FONT_L | FONT_B, THEME_TEXT);
        font_draw_wrapped(vi.error, x + 60, y + 190, FONT_S, THEME_TEXT_DIM, w - 120, 2);
        font_draw_center(p->n_cands > 1 ? "Every candidate failed. Press Back to pick another source." : "Press Back to pick another source.",
                         x + w / 2, y + h - 70, FONT_S | FONT_B, THEME_TEXT_MUTED);
        return;
    }

    if (vi.state == VIDEO_LOADING || vi.buffering) {
        ui_spinner(SNTV_UI_W / 2, SNTV_UI_H / 2, 34);
        const char *what = vi.state == VIDEO_LOADING ? "Starting" : "Buffering";
        char b[48];
        if (vi.buffer_percent > 0 && vi.buffer_percent < 100) snprintf(b, sizeof b, "%s  %d%%", what, vi.buffer_percent);
        else snprintf(b, sizeof b, "%s", what);
        font_draw_shadow(b, SNTV_UI_W / 2 - font_measure(b, FONT_S | FONT_B) / 2, SNTV_UI_H / 2 + 60, FONT_S | FONT_B, THEME_TEXT, RGBA(0, 0, 0, 0xA0));
    }

    /* centre play/pause flash */
    float flash = p->play_flash - g_app.time;
    if (vi.state == VIDEO_PAUSED || flash > 0) {
        float a = vi.state == VIDEO_PAUSED ? 1.0f : flash / 0.6f;
        float r = 70 + (1 - a) * 20 * (vi.state != VIDEO_PAUSED);
        gfx_circle(SNTV_UI_W / 2, SNTV_UI_H / 2, r, RGBA(0, 0, 0, (int)(0x80 * a)));
        gfx_rrect_ring(SNTV_UI_W / 2 - r, SNTV_UI_H / 2 - r, 2 * r, 2 * r, r, 2, RGBA(0xFF, 0xFF, 0xFF, (int)(0x40 * a)));
        int icon = vi.state == VIDEO_PAUSED ? ICON_PLAY : p->flash_icon;
        ui_icon((Icon)icon, SNTV_UI_W / 2 + (icon == ICON_PLAY ? 6 : 0), SNTV_UI_H / 2, 64, RGBA(255, 255, 255, (int)(255 * a)));
        if (flash > 0) app_redraw();
    }

    int bar_only = !overlay && p->seek_commit > 0;
    double shown_pos = p->seek_commit > 0 ? p->seek_target : vi.position;
    float bx = THEME_MARGIN, by = SNTV_UI_H - 150, bw = SNTV_UI_W - 2 * THEME_MARGIN;
    if (overlay) {
        gfx_gradient_v(0, SNTV_UI_H - 420, SNTV_UI_W, 420, RGBA(0, 0, 0, 0), RGBA(0, 0, 0, 0xE0));
        gfx_gradient_v(0, 0, SNTV_UI_W, 220, RGBA(0, 0, 0, 0xB0), RGBA(0, 0, 0, 0));
        font_draw_fit(p->title, THEME_MARGIN, 56, FONT_L | FONT_B, THEME_TEXT, SNTV_UI_W - 2 * THEME_MARGIN - 300);
        float cx = THEME_MARGIN;
        if (p->cands[p->cur].label) { font_draw_fit(p->cands[p->cur].label, cx, 114, FONT_S, THEME_TEXT_DIM, 900); cx += font_measure(p->cands[p->cur].label, FONT_S) + 20; }
        if (vi.codec[0] && cx < 1200) ui_chip(vi.codec, cx, 110, RGBA(0xFF, 0xFF, 0xFF, 0x22), THEME_TEXT);
        if (p->n_cands > 1) {
            char c[48]; snprintf(c, sizeof c, "Source %d of %d", p->cur + 1, p->n_cands);
            ui_chip(c, SNTV_UI_W - THEME_MARGIN - font_measure(c, FONT_XS | FONT_B) - 24, 62, RGBA(0xFF, 0xFF, 0xFF, 0x22), THEME_TEXT);
        }
    } else if (bar_only) {
        gfx_gradient_v(0, SNTV_UI_H - 240, SNTV_UI_W, 240, RGBA(0, 0, 0, 0), RGBA(0, 0, 0, 0xB0));
    }
    if (overlay || bar_only) {
        float th = p->seek_commit > 0 ? 12 : 8, ty = by - th / 2;
        gfx_rrect(bx, ty, bw, th, th / 2, RGBA(0xFF, 0xFF, 0xFF, 0x30));
        if (vi.duration > 0) {
            if (vi.buffer_percent > 0) gfx_rrect(bx, ty, bw * vi.buffer_percent / 100.0f, th, th / 2, RGBA(0xFF, 0xFF, 0xFF, 0x30));
            float fw = bw * (float)(shown_pos / vi.duration);
            if (fw > th) gfx_rrect_grad_h(bx, ty, fw, th, th / 2, THEME_ACCENT, THEME_ACCENT2);
            float kx = bx + fw;
            if (!g_app.fx_light) gfx_shadow(kx - 14, by - 14, 28, 28, 14, 14, COLOR_ALPHA(THEME_ACCENT2, 0xB0));
            gfx_circle(kx, by, p->seek_commit > 0 ? 16 : 12, THEME_TEXT);
            if (p->seek_commit > 0) {
                char t[32]; ui_format_time(shown_pos, t, sizeof t);
                float tw = font_measure(t, FONT_M | FONT_B) + 36;
                float tx = kx - tw / 2;
                if (tx < bx) tx = bx;
                if (tx + tw > bx + bw) tx = bx + bw - tw;
                gfx_rrect(tx, by - 82, tw, 52, 26, THEME_TEXT);
                font_draw(t, tx + 18, by - 82 + (52 - font_height(FONT_M | FONT_B)) / 2, FONT_M | FONT_B, THEME_ON_LIGHT);
            }
        }
        char pos[32], rem[48], t2[32];
        ui_format_time(shown_pos, pos, sizeof pos);
        ui_format_time(vi.duration - shown_pos, t2, sizeof t2);
        snprintf(rem, sizeof rem, vi.duration > 0 ? "-%s" : "--:--", t2);
        font_draw(pos, bx, by + 24, FONT_S | FONT_B, THEME_TEXT);
        font_draw(rem, bx + bw - font_measure(rem, FONT_S | FONT_B), by + 24, FONT_S | FONT_B, THEME_TEXT_DIM);
    }
    if (overlay) {
        /* hint row */
        float hx = bx, hy = by + 66;
        struct { Icon ic; const char *t; } hints[] = { { ICON_AUDIO, "Audio" }, { ICON_SUBS, "Subtitles" }, { ICON_SWAP, "Next source" } };
        const char *keys[] = { "Up", "Down", "Yellow" };
        for (int i = 0; i < 3; i++) {
            if (i == 2 && p->n_cands < 2) break;
            float w = font_measure(hints[i].t, FONT_XS | FONT_B) + font_measure(keys[i], FONT_XS) + 90;
            gfx_rrect(hx, hy, w, 40, 20, RGBA(0xFF, 0xFF, 0xFF, 0x18));
            ui_icon(hints[i].ic, hx + 26, hy + 20, 22, THEME_TEXT);
            font_draw(hints[i].t, hx + 48, hy + (40 - font_height(FONT_XS | FONT_B)) / 2, FONT_XS | FONT_B, THEME_TEXT);
            font_draw(keys[i], hx + 58 + font_measure(hints[i].t, FONT_XS | FONT_B), hy + (40 - font_height(FONT_XS)) / 2, FONT_XS, THEME_TEXT_MUTED);
            hx += w + 12;
        }
    }
    if (p->up_next_shown && p->next_video_id && !p->menu) draw_up_next(p, &vi, overlay);
    if (p->menu) draw_menu(p);
}

static int menu_rows(Player *p)
{
    VideoTrack tr[MAX_TRACKS];
    int n = video_tracks(p->menu == MENU_AUDIO ? TRACK_AUDIO : TRACK_TEXT, tr, MAX_TRACKS);
    return p->menu == MENU_AUDIO ? n : 1 + n + p->n_addon_subs;
}

static void menu_activate(Player *p)
{
    VideoTrack tr[MAX_TRACKS];
    int n = video_tracks(p->menu == MENU_AUDIO ? TRACK_AUDIO : TRACK_TEXT, tr, MAX_TRACKS);
    int f = p->menu_focus;
    if (p->menu == MENU_AUDIO) { if (f < n) video_select_track(TRACK_AUDIO, tr[f].index); return; }
    if (f == 0) { select_addon_sub(p, -1); p->embedded_text_sel = -1; video_select_track(TRACK_TEXT, -1); return; }
    f--;
    if (f < n) { select_addon_sub(p, -1); p->embedded_text_sel = tr[f].index; video_select_track(TRACK_TEXT, tr[f].index); return; }
    f -= n;
    if (f < p->n_addon_subs) select_addon_sub(p, f);
}

static void seek_by(Player *p, double base)
{
    VideoInfo vi;
    video_info(&vi);
    p->seek_steps++;
    p->seek_decay = 0.6f;
    double step = base * (p->seek_steps > 8 ? 6 : p->seek_steps > 4 ? 3 : 1);
    double from = p->seek_commit > 0 ? p->seek_target : vi.position;
    double target = from + step;
    if (target < 0) target = 0;
    if (vi.duration > 0 && target > vi.duration - 1) target = vi.duration - 1;
    p->seek_target = target;
    p->seek_commit = 0.35f;   /* coalesce rapid presses into one seek */
    app_redraw();
}

static int key(Screen *s, KeyAction a)
{
    Player *p = s->st;
    VideoInfo vi;
    video_info(&vi);
    if (p->menu) {
        int rows = menu_rows(p);
        switch (a) {
        case KEY_UP: if (p->menu_focus > 0) p->menu_focus--; app_redraw(); return 1;
        case KEY_DOWN: if (p->menu_focus < rows - 1) p->menu_focus++; app_redraw(); return 1;
        case KEY_LEFT: if (p->menu == MENU_SUBS && p->track_loaded) { p->track.offset -= 0.5; app_redraw(); } return 1;
        case KEY_RIGHT: if (p->menu == MENU_SUBS && p->track_loaded) { p->track.offset += 0.5; app_redraw(); } return 1;
        case KEY_OK: menu_activate(p); p->menu = MENU_NONE; app_redraw(); return 1;
        case KEY_BACK: p->menu = MENU_NONE; app_redraw(); return 1;
        default: return 1;
        }
    }
    switch (a) {
    case KEY_OK: case KEY_PLAY_PAUSE:
        if (vi.state == VIDEO_PLAYING) video_pause(); else { video_play(); p->flash_icon = ICON_PLAY; p->play_flash = g_app.time + 0.6f; }
        show_overlay(p);
        return 1;
    case KEY_PLAY: video_play(); show_overlay(p); return 1;
    case KEY_PAUSE: video_pause(); show_overlay(p); return 1;
    case KEY_LEFT: case KEY_REWIND: seek_by(p, -10); return 1;
    case KEY_RIGHT: case KEY_FORWARD: seek_by(p, 10); return 1;
    case KEY_UP: p->menu = MENU_AUDIO; p->menu_focus = 0; app_redraw(); return 1;
    case KEY_DOWN: p->menu = MENU_SUBS; p->menu_focus = 0; app_redraw(); return 1;
    case KEY_INFO: case KEY_MENU: show_overlay(p); return 1;
    case KEY_GREEN: if (p->next_video_id) go_up_next(p); return 1;
    case KEY_YELLOW: if (p->cur + 1 < p->n_cands) { app_toast("Switching source"); start_candidate(p, p->cur + 1, vi.position); } return 1;
    case KEY_STOP: case KEY_BACK:
        report(p, 1);
        video_stop();
        p->finished = 1;
        app_pop();
        return 1;
    default: return 0;
    }
}

static void destroy(Screen *s)
{
    Player *p = s->st;
    if (g_live == p) g_live = NULL;
    p->gen++;
    video_stop();
    subs_free(&p->track);
    for (int i = 0; i < p->n_addon_subs; i++) { free(p->addon_subs[i].url); free(p->addon_subs[i].lang); free(p->addon_subs[i].addon); }
    play_candidates_free(p->cands, p->n_cands);
    meta_preview_free(&p->meta);
    free(p->video_id); free(p->title); free(p->next_video_id); free(p->next_title);
    free(p);
    free(s);
}

Screen *screen_player_create(const MetaPreview *meta, const char *video_id, const char *title,
                             PlayCandidate *cands, int n_cands, int start_index, double start_s,
                             const char *next_video_id, const char *next_title)
{
    Screen *s = calloc(1, sizeof *s);
    Player *p = calloc(1, sizeof *p);
    meta_preview_copy(&p->meta, meta);
    p->video_id = strdup(video_id);
    p->title = strdup(title ? title : meta->name);
    p->cands = cands; p->n_cands = n_cands;
    p->next_video_id = next_video_id ? strdup(next_video_id) : NULL;
    p->next_title = next_title ? strdup(next_title) : NULL;
    p->active_addon_sub = -1;
    p->embedded_text_sel = -1;
    p->gen = 1;
    g_live = p;
    s->name = "player"; s->st = p;
    s->update = update; s->draw = draw; s->key = key; s->destroy = destroy;
    start_candidate(p, start_index < n_cands ? start_index : 0, start_s);
    query_addon_subs(p);
    return s;
}
