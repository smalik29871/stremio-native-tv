/* Diagnostics: the app's own log on screen, for TVs where nobody can reach the
 * log file. Tabs filter to playback lines, problems (warnings and errors) or
 * everything, and Speed shows how fast this TV runs the app; Blue switches to the previous run (useful after a crash); OK
 * shows a QR code with the device details and the lines up to the cursor, so
 * the log can be scanned with a phone and pasted into a bug report. */
#include "../app.h"
#include "../log.h"
#include "../phone.h"
#include "../video/video.h"
#include "../platform/platform.h"
#include "../perfstat.h"
#include "../youtube.h"
#include "qrcodegen.h"
#include "font.h"
#include "theme.h"
#include "widgets.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { TAB_PLAYBACK, TAB_PROBLEMS, TAB_ALL, TAB_SPEED, TAB_YOUTUBE, TAB_COUNT };
/* Big Buck Bunny (Blender Foundation), up to 4K: always there, never age- or region-locked */
#define YT_TEST_ID "aqz-KE-bpKQ"
#define LIST_Y 300.0f
enum { HIT_TAB = 0x10000, HIT_SESSION = 0x20000, HIT_YT = 0x30000 };   /* touch; a log line is its place in the list */
#define ROW_H 36.0f
#define LIST_ROWS (SNTV_PHONE ? (int)((SNTV_UI_H - LIST_Y - 330) / ROW_H) : 15)   /* a phone upright: as many as fit */
#define QR_BUDGET 700    /* bytes of log text in the QR; denser codes are hard to scan off a TV */

typedef struct {
    LogLine *lines; int n;          /* source lines (current snapshot or previous file) */
    int *vis; int n_vis;            /* indexes of lines passing the tab filter */
    int tab, previous;
    int cursor;                     /* index into vis */
    int follow;                     /* cursor sticks to the newest line */
    float scroll;
    UiPan pan;
    unsigned seen_version;
    int show_qr;
    uint8_t qr[qrcodegen_BUFFER_LEN_MAX];
    int qr_ok, qr_lines;
    int qr_link;            /* the code is a link to the whole log on this TV, not the lines themselves */
    char system[256];
    int yt_state, yt_focus;          /* YouTube test: 0 not run, 1 running, 2 done; 0 Run, 1 Play */
    char *yt_answer;                 /* what the test found to play, for Play */
} Diag;

static Diag *g_diag_live;   /* the screen a YouTube test answers to */

static int contains_ci(const char *hay, const char *needle)
{
    size_t n = strlen(needle);
    for (const char *p = hay; *p; p++) {
        size_t i = 0;
        while (i < n && p[i] && tolower((unsigned char)p[i]) == tolower((unsigned char)needle[i])) i++;
        if (i == n) return 1;
    }
    return 0;
}

static int is_playback(const LogLine *l)
{
    static const char *keys[] = { "media", "acb", "video", "player", "lsregister", "ls2", "subs:", "stream", "luna", NULL };
    for (int i = 0; keys[i]; i++) if (contains_ci(l->text, keys[i])) return 1;
    return 0;
}

static int passes(const Diag *d, const LogLine *l)
{
    switch (d->tab) {
    case TAB_PLAYBACK: return is_playback(l) || l->level == 'E';
    case TAB_PROBLEMS: return l->level == 'E' || l->level == 'W';
    default: return 1;
    }
}

static void refilter(Diag *d)
{
    free(d->vis);
    d->vis = malloc(sizeof(int) * (size_t)(d->n + 1));
    d->n_vis = 0;
    for (int i = 0; i < d->n; i++) if (passes(d, &d->lines[i])) d->vis[d->n_vis++] = i;
    if (d->follow || d->cursor >= d->n_vis) d->cursor = d->n_vis ? d->n_vis - 1 : 0;
}

static void load_current(Diag *d)
{
    free(d->lines);
    d->lines = malloc(sizeof(LogLine) * LOG_RING_LINES);
    d->n = log_snapshot(d->lines, LOG_RING_LINES);
    d->seen_version = log_version();
}

static void load_previous(Diag *d)
{
    free(d->lines);
    d->lines = NULL; d->n = 0;
    const char *path = log_previous_path();
    FILE *f = path[0] ? fopen(path, "r") : NULL;
    if (!f) return;
    int cap = 256;
    d->lines = malloc(sizeof(LogLine) * (size_t)cap);
    char buf[2048];
    while (fgets(buf, sizeof buf, f)) {
        buf[strcspn(buf, "\r\n")] = 0;
        if (!buf[0]) continue;
        if (d->n == cap) { cap *= 2; d->lines = realloc(d->lines, sizeof(LogLine) * (size_t)cap); }
        LogLine *l = &d->lines[d->n++];
        /* "HH:MM:SS L message" */
        if (strlen(buf) > 11 && buf[8] == ' ' && buf[10] == ' ') {
            l->level = buf[9];
            snprintf(l->text, sizeof l->text, "%.8s %.*s", buf, LOG_LINE_MAX - 12, buf + 11);
        } else {
            l->level = 'I';
            snprintf(l->text, sizeof l->text, "%.*s", LOG_LINE_MAX - 1, buf);
        }
    }
    fclose(f);
    /* keep only the most recent lines, like the live view */
    if (d->n > LOG_RING_LINES) {
        memmove(d->lines, d->lines + (d->n - LOG_RING_LINES), sizeof(LogLine) * LOG_RING_LINES);
        d->n = LOG_RING_LINES;
    }
}

static void reload(Diag *d)
{
    if (d->previous) load_previous(d); else load_current(d);
    refilter(d);
}

static const char *TAB_NAMES[] = { "playback", "problems", "everything" };

/* The whole log shown, with what it is from above it (malloc'd) */
static char *log_text(const Diag *d)
{
    size_t cap = 600, len = 0;
    for (int k = 0; k < d->n_vis; k++) cap += strlen(d->lines[d->vis[k]].text) + 4;
    char *all = malloc(cap);
    if (!all) return NULL;
    len = (size_t)snprintf(all, cap, "Cuelight %s\n%s\n%s\n%s log, %s session\n---\n", SNTV_APP_VERSION, video_backend_name(), d->system,
                           TAB_NAMES[d->tab < 3 ? d->tab : 2], d->previous ? "previous" : "this");
    for (int k = 0; k < d->n_vis && len < cap; k++) len += (size_t)snprintf(all + len, cap - len, "%c %s\n", d->lines[d->vis[k]].level, d->lines[d->vis[k]].text);
    return all;
}

/* A phone cannot scan its own screen: the log goes to the clipboard, to paste into a message */
static void copy_log(const Diag *d)
{
    char *all = log_text(d);
    if (all && SDL_SetClipboardText(all) == 0) { LOGI("diagnostics: the log (%d lines) copied", d->n_vis); app_toast("Log copied (%d lines): paste it into a message", d->n_vis); }
    else app_toast("The log could not be copied");
    free(all);
}

/* QR: a short link to the whole log, served by this TV while the code shows (a phone on
 * the same Wi-Fi opens it and copies all of it); without the phone server, the lines
 * leading up to the cursor in the code itself, newest last. */
static void build_qr(Diag *d)
{
    char *all = log_text(d), url[160];
    if (all) {
        d->qr_link = phone_offer_log(all, url, sizeof url);
        free(all);
        if (d->qr_link) {
            uint8_t tmp[qrcodegen_BUFFER_LEN_MAX];
            d->qr_ok = qrcodegen_encodeText(url, tmp, d->qr, qrcodegen_Ecc_MEDIUM, qrcodegen_VERSION_MIN, 10, qrcodegen_Mask_AUTO, true);
            d->qr_lines = d->n_vis;
            LOGI("diagnostics: the log (%d lines) is at a link while the code shows", d->n_vis);
            if (d->qr_ok) return;
            phone_offer_log(NULL, NULL, 0);
            d->qr_link = 0;
        }
    }
    char *text = malloc(QR_BUDGET + 600);
    int budget = QR_BUDGET;
    for (int attempt = 0; attempt < 6; attempt++, budget = budget * 3 / 4) {
        int o = snprintf(text, 600, "Cuelight %s\n%s\n%s\n%s log, %s session\n---\n",
                         SNTV_APP_VERSION, video_backend_name(), d->system, TAB_NAMES[d->tab],
                         d->previous ? "previous" : "this");
        /* walk backwards from the cursor to find how many lines fit */
        int first = d->cursor + 1, used = 0;
        while (first > 0) {
            const LogLine *l = &d->lines[d->vis[first - 1]];
            int len = (int)strlen(l->text) + 3;
            if (used + len > budget) break;
            used += len;
            first--;
        }
        for (int k = first; k <= d->cursor && k < d->n_vis; k++) {
            const LogLine *l = &d->lines[d->vis[k]];
            o += snprintf(text + o, (size_t)(QR_BUDGET + 600 - o), "%c %s\n", l->level, l->text);
        }
        d->qr_lines = d->n_vis ? d->cursor - first + 1 : 0;
        uint8_t tmp[qrcodegen_BUFFER_LEN_MAX];
        d->qr_ok = qrcodegen_encodeText(text, tmp, d->qr, qrcodegen_Ecc_LOW, qrcodegen_VERSION_MIN, 16,
                                        qrcodegen_Mask_AUTO, true);
        if (d->qr_ok) break;
    }
    free(text);
}

/* ---- update / draw ---------------------------------------------------------- */

static void update(Screen *s, float dt)
{
    Diag *d = s->st;
    if (!d->previous && log_version() != d->seen_version) {
        int was_at_end = d->n_vis == 0 || d->cursor >= d->n_vis - 1;
        load_current(d);
        d->follow = was_at_end;
        refilter(d);
        app_redraw();
    }
    int first = d->cursor - (LIST_ROWS - 3);
    if (first < 0) first = 0;
    ui_pan(&d->pan, &d->scroll, first * ROW_H, (d->n_vis - LIST_ROWS + 1) * ROW_H, dt, 16);
}

static void draw_qr_overlay(Diag *d)
{
    gfx_rect(0, 0, SNTV_UI_W, SNTV_UI_H, COLOR_ALPHA(THEME_BG, 0xE8));
    float w = SNTV_PHONE ? SNTV_UI_W - 40 : 1500, h = SNTV_PHONE ? 1400 : 760, x = (SNTV_UI_W - w) / 2, y = (SNTV_UI_H - h) / 2;
    ui_panel(x, y, w, h, R_PANEL, THEME_PANEL);
    gfx_rrect_ring(x, y, w, h, R_PANEL, 1.5f, COLOR_ALPHA(THEME_TEXT, 0x20));
    float qs = SNTV_PHONE ? w - 80 : 680, qx = x + 40, qy = y + 40;
    gfx_rrect(qx, qy, qs, qs, R_PANEL, RGBA(255, 255, 255, 255));
    if (d->qr_ok) {
        int n = qrcodegen_getSize(d->qr);
        /* whole screen pixels per module (sharp edges), with a 4-module quiet zone */
        float sc = gfx_scale(), cell = (float)(int)(qs * sc / (n + 8)) / sc;
        if (cell <= 0) cell = qs / (n + 8);
        float off = (qs - cell * n) / 2;
        for (int ry = 0; ry < n; ry++)
            for (int rx = 0; rx < n; rx++)
                if (qrcodegen_getModule(d->qr, rx, ry))
                    gfx_rect(qx + off + rx * cell, qy + off + ry * cell, cell, cell, RGBA(0, 0, 0, 255));
    }
    float tx = qx + qs + 60, tw = x + w - tx - 40;
    if (SNTV_PHONE) { tx = qx; tw = w - 80; y = qy + qs - 20; h = 120 + 700 - 20 + 120; }   /* upright: the words under the code */
    if (d->qr_link) {
        font_draw("Scan to get this log", tx, y + 60, FONT_L | FONT_B, THEME_TEXT);
        char t[200];
        snprintf(t, sizeof t, "The code is a link to this TV: your phone opens the whole log (%d line%s) as text to copy.",
                 d->qr_lines, d->qr_lines == 1 ? "" : "s");
        font_draw_wrapped(t, tx, y + 130, FONT_S, THEME_TEXT_DIM, tw, 3);
        font_draw_wrapped("Your phone has to be on the same Wi-Fi as the TV. The link works while this is open, and a new one is made each time.",
                          tx, y + 260, FONT_S, THEME_TEXT_MUTED, tw, 4);
        ui_pill("Close", 0, ICON_BACK, tx, y + h - 120, 72, PILL_SECONDARY, 1);
        return;
    }
    font_draw("Scan to copy this log", tx, y + 60, FONT_L | FONT_B, THEME_TEXT);
    char t[200];
    snprintf(t, sizeof t, "The code holds your TV's details and the %d line%s up to the selected one.",
             d->qr_lines, d->qr_lines == 1 ? "" : "s");
    font_draw_wrapped(t, tx, y + 130, FONT_S, THEME_TEXT_DIM, tw, 3);
    font_draw_wrapped("Open your phone's camera, tap the text it finds, copy it, and paste it into your bug report or chat.",
                      tx, y + 240, FONT_S, THEME_TEXT_DIM, tw, 4);
    font_draw_wrapped("Need more? Close this, move the cursor up to an earlier line with Up, and scan again.",
                      tx, y + 380, FONT_S, THEME_TEXT_MUTED, tw, 3);
    if (!d->qr_ok) font_draw("These lines didn't fit in a QR code.", tx, y + 500, FONT_S | FONT_B, THEME_DANGER);
    ui_pill("Close", 0, ICON_BACK, tx, y + h - 120, 72, PILL_SECONDARY, 1);
}

static void draw(Screen *s)
{
    Diag *d = s->st;
    ui_background(0);
    float x = THEME_MARGIN, w = SNTV_UI_W - 2 * THEME_MARGIN;
    font_draw("Diagnostics", x, 60, FONT_XL | FONT_B, THEME_TEXT);
    char line[400];
    snprintf(line, sizeof line, "Version %s  \xC2\xB7  %s", SNTV_APP_VERSION, video_backend_name());
    font_draw_fit(line, x, 136, FONT_S | FONT_B, THEME_TEXT_DIM, w);
    font_draw_fit(d->system, x, 172, FONT_S, THEME_TEXT_MUTED, w);

    /* tabs (upright on a phone, smaller to fit across) */
    static const char *TABS[TAB_COUNT] = { "Playback", "Problems", "Everything", "Speed", "YouTube" };
    float tx = x;
    if (SNTV_PHONE) gfx_set_scale(0.72f, x, 222);
    for (int i = 0; i < TAB_COUNT; i++) {
        int sel = d->tab == i;
        float tw = font_measure(TABS[i], FONT_S | FONT_B) + 48;
        if (sel) gfx_rrect(tx, 222, tw, 54, theme_pill_r(54), THEME_FOCUS);
        ui_hit(tx, 222, tw, 54, HIT_TAB + i);
        font_draw(TABS[i], tx + 24, 222 + (54 - font_height(FONT_S | FONT_B)) / 2, FONT_S | FONT_B, sel ? THEME_ON_FOCUS : THEME_TEXT_DIM);
        tx += tw + 8;
    }
    const char *sess = d->previous ? "Previous session" : "This session";
    float sw = font_measure(sess, FONT_S | FONT_B) + 48;
    gfx_rrect(tx + 20, 222, sw, 54, R_PANEL, d->previous ? RGBA(0x3B, 0x82, 0xF6, 0xFF) : THEME_SURFACE);
    font_draw(sess, tx + 44, 222 + (54 - font_height(FONT_S | FONT_B)) / 2, FONT_S | FONT_B, THEME_TEXT);
    ui_hit(tx + 20, 222, sw, 54, HIT_SESSION);
    if (SNTV_PHONE) gfx_set_scale(1, 0, 0);
    const char *hint = d->tab == TAB_YOUTUBE ? "Left/Right tabs   \xC2\xB7   Up/Down buttons   \xC2\xB7   OK: run or play"
                                             : gfx_handheld() ? "Tap a line: copy the log to paste in a message"
                                             : ui_pointer() ? "Left/Right filter   \xC2\xB7   Click a line: copy the log to paste in a message"
                                             : "Left/Right filter   \xC2\xB7   Blue: other session   \xC2\xB7   OK: QR for your phone";
    if (!SNTV_PHONE) font_draw(hint, x + w - font_measure(hint, FONT_XS), 240, FONT_XS, THEME_TEXT_MUTED);

    float lh = ROW_H, list_h = LIST_ROWS * lh;
    if (d->tab == TAB_YOUTUBE) {   /* each way to get a YouTube video, tried on this TV and network */
        const YoutubeTest *t = youtube_test_report();
        static const char *const STEP[YT_TEST_STEPS] = { "visionOS app: asked for the video", "visionOS app: high-quality files",
            "TV app: asked for the video", "TV app: high-quality files", "YouTube's own playlist", "File with sound (last resort)" };
        gfx_rrect(x - 16, LIST_Y - 12, w + 32, list_h + 24, R_PANEL, RGBA(0x00, 0x00, 0x00, 0x40));
        float by = LIST_Y + 8, bx = x + 10;
        const char *run = d->yt_state == 1 ? "Testing\xE2\x80\xA6" : d->yt_state == 2 ? "Run the test again" : "Run the test";
        float pw = ui_pill(run, 1, ICON_RELOAD, bx, by, 60, PILL_SECONDARY, d->yt_focus == 0);
        ui_hit(bx, by, pw, 60, HIT_YT);
        bx += pw + 16;
        if (d->yt_answer) { pw = ui_pill("Play the test video", 1, ICON_PLAY, bx, by, 60, PILL_SECONDARY, d->yt_focus == 1); ui_hit(bx, by, pw, 60, HIT_YT + 1); }
        if (d->yt_state == 1) { ui_spinner(x + w - 30, by + 30, 14); app_redraw(); }
        float ry = by + 90;
        for (int i = 0; i < YT_TEST_STEPS; i++, ry += 52) {
            int ok = d->yt_state ? t->ok[i] : -1;
            Color c = ok == 1 ? THEME_OK : ok == 0 ? THEME_DANGER : THEME_TEXT_MUTED;
            if (ok == 1) ui_icon(ICON_CHECK, x + 22, ry + 14, 24, c);
            else if (ok == 0) ui_icon(ICON_CLOSE, x + 22, ry + 14, 22, c);
            else gfx_circle(x + 22, ry + 14, 5, c);
            font_draw(STEP[i], x + 52, ry, FONT_S | FONT_B, THEME_TEXT);
            const char *what = ok == -1 ? (d->yt_state == 1 ? "\xE2\x80\xA6" : "") : t->what[i];
            if (SNTV_PHONE) { font_draw_fit(what, x + 52, ry + 36, FONT_XS, ok == 0 ? THEME_DANGER : THEME_TEXT_DIM, w - 60); ry += 40; }   /* upright: under its step */
            else font_draw_fit(what, x + 560, ry + 2, FONT_S, ok == 0 ? THEME_DANGER : THEME_TEXT_DIM, w - 580);
        }
        if (d->yt_state == 2) {
            char best[160]; snprintf(best, sizeof best, "Best on this TV now: %s", t->best);
            font_draw(best, x + 10, ry + 16, FONT_M | FONT_B, THEME_TEXT);
        }
        font_draw("Tests the Big Buck Bunny video the way trailers are fetched. The result also goes to the log (Everything) for the QR code.",
                  x, LIST_Y + list_h + 30, FONT_XS, THEME_TEXT_MUTED);
        return;
    }
    if (d->tab == TAB_SPEED) {   /* the measurements, not the log */
        char rep[2048];
        perf_report(rep, sizeof rep);
        gfx_rrect(x - 16, LIST_Y - 12, w + 32, list_h + 24, R_PANEL, RGBA(0x00, 0x00, 0x00, 0x40));
        float ly = LIST_Y + 8;
        for (char *p = rep, *e; *p && ly < LIST_Y + list_h; p = e + (*e ? 1 : 0), ly += 44) {
            e = strchr(p, '\n'); if (!e) e = p + strlen(p);
            char one[300]; snprintf(one, sizeof one, "%.*s", (int)(e - p), p);
            font_draw_fit(one, x + 10, ly, (one[0] == ' ' ? FONT_S | FONT_ALT : FONT_S | FONT_B), one[0] == ' ' ? THEME_TEXT_DIM : THEME_TEXT, w - 20);
        }
        font_draw("A whole frame over 17 ms is a dropped frame at 60 Hz. The figures are also in the log (Everything) for the QR code.", x, LIST_Y + list_h + 30, FONT_XS, THEME_TEXT_MUTED);
        app_redraw();   /* the numbers keep moving */
        if (d->show_qr) draw_qr_overlay(d);
        return;
    }

    /* list */
    gfx_rrect(x - 16, LIST_Y - 12, w + 32, list_h + 24, R_PANEL, RGBA(0x00, 0x00, 0x00, 0x40));
    if (!d->n_vis) {
        const char *msg = d->previous ? "No log from a previous session." : d->tab == TAB_PROBLEMS ? "No warnings or errors so far." : "Nothing logged here yet.";
        font_draw_center(msg, SNTV_UI_W / 2, LIST_Y + list_h / 2 - 20, FONT_M | FONT_B, THEME_TEXT_DIM);
    }
    gfx_clip_push(x - 16, LIST_Y - 6, w + 32, list_h + 12);
    ui_pan_area(&d->pan, x - 16, LIST_Y - 6, w + 32, list_h + 12, 1);
    for (int k = 0; k < d->n_vis; k++) {
        float ry = LIST_Y + k * lh - d->scroll;
        if (ry + lh < LIST_Y - 6 || ry > LIST_Y + list_h) continue;
        const LogLine *l = &d->lines[d->vis[k]];
        int cur = k == d->cursor;
        if (cur) gfx_rrect(x - 8, ry - 2, w + 16, lh, R_KEY, COLOR_ALPHA(THEME_TEXT, 0x26));
        ui_hit(x - 8, ry - 2, w + 16, lh, k);
        Color dot = l->level == 'E' ? THEME_DANGER : l->level == 'W' ? THEME_GOLD : COLOR_ALPHA(THEME_TEXT, 0x40);
        gfx_circle(x + 8, ry + lh / 2 - 1, 5, dot);
        /* time, then message */
        char tm[9];
        snprintf(tm, sizeof tm, "%.8s", l->text);
        font_draw(tm, x + 26, ry + 4, FONT_XS, THEME_TEXT_MUTED);
        const char *msg = strlen(l->text) > 9 ? l->text + 9 : "";
        Color c = l->level == 'E' ? THEME_DANGER : l->level == 'W' ? THEME_GOLD : (cur ? THEME_TEXT : THEME_TEXT_DIM);
        font_draw_fit(msg, x + 130, ry + 4, FONT_XS | (cur ? FONT_B : 0), c, w - 140);
    }
    gfx_clip_pop();

    /* selected line in full */
    float py = LIST_Y + list_h + 30;
    if (d->n_vis) {
        const LogLine *l = &d->lines[d->vis[d->cursor]];
        char pos[48];
        snprintf(pos, sizeof pos, "Line %d of %d", d->cursor + 1, d->n_vis);
        font_draw(pos, x, py, FONT_XS | FONT_B, THEME_TEXT_MUTED);
        font_draw_wrapped(l->text, x, py + 30, FONT_S, THEME_TEXT, w, 4);
    }
    if (d->show_qr) draw_qr_overlay(d);
}

/* The Speed figures into the log, so the QR code on the other tabs carries them. */
static void log_speed(Diag *d)
{
    char rep[2048];
    perf_report(rep, sizeof rep);
    for (char *p = rep, *e; *p; p = e + (*e ? 1 : 0)) {
        e = strchr(p, '\n'); if (!e) e = p + strlen(p);
        LOGI("speed: %.*s", (int)(e - p), p);
    }
    (void)d;
}

static void on_yt_test(const char *answer, const char *error, void *user)
{
    (void)user;
    Diag *d = g_diag_live;
    if (!d) return;
    d->yt_state = 2;
    free(d->yt_answer);
    d->yt_answer = answer ? strdup(answer) : NULL;
    if (!answer) LOGW("youtube test: nothing to play (%s)", error ? error : "?");
    app_redraw();
}

static void yt_play(Diag *d)
{
    static char urls[YT_PLAY_URLS][4608];
    int nu = youtube_player_urls(d->yt_answer, YT_TEST_ID, urls, YT_PLAY_URLS);
    if (!nu) { app_toast("The test video could not be opened"); return; }
    PlayCandidate *c = calloc((size_t)nu, sizeof *c);
    for (int i = 0; i < nu; i++) {   /* the same order a trailer plays in */
        c[i].url = strdup(urls[i]);
        char lbl[48]; snprintf(lbl, sizeof lbl, "YouTube test - %s", i == 0 ? "best" : i == nu - 1 ? "last" : "next");
        c[i].label = strdup(lbl);
    }
    MetaPreview m; memset(&m, 0, sizeof m);
    m.id = (char *)"yttest"; m.type = (char *)"movie"; m.name = (char *)"YouTube test";
    app_push(screen_player_create(&m, "trailer:yttest", "YouTube test  -  Big Buck Bunny", c, nu, 0, 0, NULL, NULL));
}

/* A tab, the session switch, or a line (OK then makes its QR code). */
static int tap(Screen *s, int id)
{
    Diag *d = s->st;
    if (d->show_qr) return 0;
    if (id == HIT_YT || id == HIT_YT + 1) { d->yt_focus = id - HIT_YT; return g_touch_aim; }   /* then OK runs it */
    if (g_touch_aim && id >= HIT_TAB) return 1;
    if (id >= HIT_TAB && id < HIT_TAB + TAB_COUNT) { d->tab = id - HIT_TAB; d->follow = 1; refilter(d); if (d->tab == TAB_SPEED) log_speed(d); return 1; }
    if (id == HIT_SESSION) { d->previous = !d->previous; d->follow = 1; reload(d); return 1; }
    if (d->tab == TAB_YOUTUBE || id < 0 || id >= d->n_vis) return 1;
    d->cursor = id; d->follow = 0;
    return 0;
}

static int key(Screen *s, KeyAction a)
{
    Diag *d = s->st;
    app_redraw();
    if (d->tab == TAB_YOUTUBE && !d->show_qr) {
        switch (a) {
        case KEY_UP: d->yt_focus = 0; return 1;
        case KEY_DOWN: if (d->yt_answer) d->yt_focus = 1; return 1;
        case KEY_OK:
            if (d->yt_focus == 1 && d->yt_answer) { yt_play(d); return 1; }
            if (d->yt_state != 1) { d->yt_state = 1; free(d->yt_answer); d->yt_answer = NULL; LOGI("youtube test: starting"); youtube_test(YT_TEST_ID, on_yt_test, NULL); }
            return 1;
        default: break;
        }
    }
    if (d->show_qr) {
        if (a == KEY_BACK || a == KEY_OK) { d->show_qr = 0; if (d->qr_link) phone_offer_log(NULL, NULL, 0); d->qr_link = 0; return 1; }
        return 1;
    }
    switch (a) {
    case KEY_UP: if (d->cursor > 0) d->cursor--; d->follow = 0; return 1;
    case KEY_DOWN: if (d->cursor < d->n_vis - 1) d->cursor++; d->follow = d->cursor >= d->n_vis - 1; return 1;
    case KEY_REWIND: d->cursor = d->cursor > LIST_ROWS ? d->cursor - LIST_ROWS : 0; d->follow = 0; return 1;
    case KEY_FORWARD: d->cursor = d->cursor + LIST_ROWS < d->n_vis ? d->cursor + LIST_ROWS : (d->n_vis ? d->n_vis - 1 : 0); return 1;
    case KEY_LEFT: d->tab = (d->tab + TAB_COUNT - 1) % TAB_COUNT; d->follow = 1; refilter(d); if (d->tab == TAB_SPEED) log_speed(d); return 1;
    case KEY_RIGHT: d->tab = (d->tab + 1) % TAB_COUNT; d->follow = 1; refilter(d); if (d->tab == TAB_SPEED) log_speed(d); return 1;
    case KEY_BLUE: case KEY_YELLOW: d->previous = !d->previous; d->follow = 1; reload(d); return 1;
    case KEY_OK: if (d->n_vis) { if (gfx_handheld() || ui_pointer()) copy_log(d); else { build_qr(d); d->show_qr = 1; } } return 1;
    default: return 0;
    }
}

static void destroy(Screen *s)
{
    Diag *d = s->st;
    if (g_diag_live == d) g_diag_live = NULL;
    if (d->qr_link) phone_offer_log(NULL, NULL, 0);
    free(d->yt_answer);
    free(d->lines);
    free(d->vis);
    free(d);
    free(s);
}

Screen *screen_diagnostics_create(int playback_only)
{
    Screen *s = calloc(1, sizeof *s);
    Diag *d = calloc(1, sizeof *d);
    d->tab = playback_only ? TAB_PLAYBACK : TAB_ALL;
    d->follow = 1;
    log_system_info(d->system, sizeof d->system);
    LOGI("diagnostics: %s, %s, %s", SNTV_APP_VERSION, video_backend_name(), d->system);
    reload(d);
    s->name = "diagnostics"; s->st = d;
    g_diag_live = d;
    s->update = update; s->draw = draw; s->key = key; s->tap = tap; s->destroy = destroy; s->phone = 1;
    return s;
}
