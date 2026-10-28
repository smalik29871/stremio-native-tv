/* Cue Party: start a party (its code and a QR code for a phone), join one with a code, and see
 * who is in. While it is open over the player, the player keeps time with the party. */
#include "../app.h"
#include "../keyrelay.h"
#include "../log.h"
#include "../party.h"
#include "../platform/platform.h"
#include "font.h"
#include "gfx.h"
#include "qrcodegen.h"
#include "theme.h"
#include "widgets.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { V_CHOOSE, V_JOIN, V_PARTY };
enum { PAD_COLS = 8, PAD_KEYS = 32, PAD_DEL = 32, PAD_JOIN = 33, CODE_LEN = 6 };
static const char ALPHABET[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";   /* as the relay draws codes: no 0/O or 1/I */

typedef struct {
    int view, focus, keyed;
    float intro, focus_t;
    char code[CODE_LEN + 1];
    uint8_t qr[qrcodegen_BUFFER_LEN_MAX]; int qr_ready; char qr_for[8];
    Screen *player;   /* the player under this screen, kept in time */
} Party;

void player_party_tick(Screen *player);   /* screen_player.c */

/* What the others see this device as: its kind ("LG TV", "Phone"); SNTV_PARTY_NAME for tests. */
static const char *my_name(void)
{
    const char *e = getenv("SNTV_PARTY_NAME");
    return e && *e ? e : app_device_name();
}

static void begin(Party *s, int join)
{
    if (join) { party_join(s->code, my_name(), app_device_name()); LOGI("party: joining %s", s->code); }
    else { party_start(my_name(), app_device_name()); LOGI("party: starting one"); }
    s->view = V_PARTY;
    s->focus = 0;
}

static void type_char(Party *s, char c)
{
    size_t n = strlen(s->code);
    if (c >= 'a' && c <= 'z') c = (char)(c - 32);
    if (!strchr(ALPHABET, c) || !c) return;
    if (n < CODE_LEN) { s->code[n] = c; s->code[n + 1] = 0; }
    if (strlen(s->code) == CODE_LEN) s->focus = PAD_JOIN;
    app_redraw();
}

static void qr_for(Party *s, const char *code)
{
    if (!strcmp(s->qr_for, code)) return;
    snprintf(s->qr_for, sizeof s->qr_for, "%s", code);
    const char *base = keyrelay_base(KR_PARTY);
    char url[300];
    snprintf(url, sizeof url, "%s/%s", base ? base : "", code);
    uint8_t tmp[qrcodegen_BUFFER_LEN_MAX];
    s->qr_ready = base && qrcodegen_encodeText(url, tmp, s->qr, qrcodegen_Ecc_MEDIUM, qrcodegen_VERSION_MIN, 10, qrcodegen_Mask_AUTO, true);
}

static void update(Screen *sc, float dt)
{
    Party *s = sc->st;
    s->intro = ui_ease(s->intro * 100, 100, NULL, dt, 9) / 100;
    s->focus_t = ui_ease(s->focus_t * 100, 100, NULL, dt, 16) / 100;
    if (s->player) player_party_tick(s->player);
    PartyView v;
    party_view(&v);
    static unsigned seen;
    if (v.v != seen || v.state == PARTY_CONNECTING) { seen = v.v; app_redraw(); }
    if (s->view == V_PARTY && v.state == PARTY_OFF) s->view = V_CHOOSE;
}

/* "7CK PVV" */
static void spaced(const char *code, char *out, size_t n) { snprintf(out, n, "%.3s %.3s", code, strlen(code) > 3 ? code + 3 : ""); }

static void button(const char *label, Icon icon, float x, float y, float h, PillStyle st, int focused, int id, float *w_out)
{
    float w = ui_pill(label, 1, icon, x, y, h, st, focused);
    ui_hit(x, y, w, h, id);
    if (w_out) *w_out = w;
}

static void draw_choose(Party *s, float k)
{
    float W = SNTV_UI_W, H = SNTV_UI_H, cx = W / 2;
    int a = (int)(255 * k), foc = !gfx_handheld() || s->keyed;
    float y = SNTV_PHONE ? 200 : 230;
    font_draw_center("Cue Party", cx, y, (SNTV_PHONE ? FONT_XL : FONT_TITLE) | FONT_B, COLOR_ALPHA(THEME_TEXT, a));
    ui_center_wrapped("Watch with friends in other homes, each on their own screen and from their own sources. "
                      "Play, pause and seeking stay together; nothing but the title and the time goes between you.",
                      cx, y + (SNTV_PHONE ? 90 : 120), FONT_S, THEME_TEXT_DIM, SNTV_PHONE ? W - 100 : 1100);
    float h = 84, by = y + (SNTV_PHONE ? 330 : 330);
    float w1 = ui_pill_width("Start a party", 1, h), w2 = ui_pill_width("Join with a code", 1, h), gap = 28;
    if (SNTV_PHONE) {
        button("Start a party", ICON_PLAY, cx - w1 / 2, by, h, PILL_PRIMARY, foc && s->focus == 0, 10, NULL);
        button("Join with a code", ICON_KEY, cx - w2 / 2, by + h + gap, h, PILL_SECONDARY, foc && s->focus == 1, 11, NULL);
    } else {
        float x = cx - (w1 + gap + w2) / 2;
        button("Start a party", ICON_PLAY, x, by, h, PILL_PRIMARY, foc && s->focus == 0, 10, NULL);
        button("Join with a code", ICON_KEY, x + w1 + gap, by, h, PILL_SECONDARY, foc && s->focus == 1, 11, NULL);
    }
    if (!gfx_handheld()) font_draw_center("OK  choose   \xC2\xB7   Back  close", cx, H - 80, FONT_XS, THEME_TEXT_MUTED);
}

static void draw_join(Party *s, float k)
{
    float W = SNTV_UI_W, H = SNTV_UI_H, cx = W / 2;
    int a = (int)(255 * k), foc = !gfx_handheld() || s->keyed;
    float y = SNTV_PHONE ? 140 : 110;
    font_draw_center("Join a Cue Party", cx, y, (SNTV_PHONE ? FONT_L : FONT_XL) | FONT_B, COLOR_ALPHA(THEME_TEXT, a));
    ui_center_wrapped("Type the code the party's screen shows.", cx, y + (SNTV_PHONE ? 66 : 80), FONT_S, THEME_TEXT_DIM, W - 100);
    float bs = SNTV_PHONE ? 92 : 96, bg = SNTV_PHONE ? 14 : 18, bx = cx - (CODE_LEN * bs + (CODE_LEN - 1) * bg + 30) / 2, byy = y + (SNTV_PHONE ? 140 : 160);
    for (int i = 0; i < CODE_LEN; i++) {
        float xx = bx + i * (bs + bg) + (i >= 3 ? 30 : 0);
        gfx_rrect(xx, byy, bs, bs * 1.15f, 16, COLOR_ALPHA(THEME_TEXT, 0x12));
        if ((int)strlen(s->code) == i) gfx_rrect(xx + bs * 0.25f, byy + bs * 1.15f - 14, bs * 0.5f, 5, 2, THEME_ACCENT);
        if (s->code[i]) { char d[2] = { s->code[i], 0 }; font_draw_center(d, xx + bs / 2, byy + bs * 0.575f - font_height(FONT_XL | FONT_B) / 2, FONT_XL | FONT_B, THEME_TEXT); }
    }
    int cols = SNTV_PHONE ? 6 : PAD_COLS;
    float ks = SNTV_PHONE ? 104 : 96, kh = SNTV_PHONE ? 88 : 72, kg = 12, kx = cx - (cols * ks + (cols - 1) * kg) / 2, ky = byy + bs * 1.15f + (SNTV_PHONE ? 50 : 44);
    for (int i = 0; i < PAD_KEYS; i++) {
        float xx = kx + (i % cols) * (ks + kg), yy = ky + (i / cols) * (kh + kg);
        int f = foc && s->focus == i;
        if (f) ui_focus_under(xx, yy, ks, kh, 14, g_app.fx_light ? 1 : s->focus_t);
        gfx_rrect(xx, yy, ks, kh, 14, f ? THEME_FOCUS : COLOR_ALPHA(THEME_TEXT, 0x14));
        char d[2] = { ALPHABET[i], 0 };
        font_draw_center(d, xx + ks / 2, yy + kh / 2 - font_height(FONT_M | FONT_B) / 2, FONT_M | FONT_B, f ? THEME_ON_FOCUS : THEME_TEXT);
        ui_hit(xx, yy, ks, kh, 100 + i);
    }
    int rows = (PAD_KEYS + cols - 1) / cols;
    float ry = ky + rows * (kh + kg) + 20, h = 76;
    float wd = ui_pill_width("Delete", 1, h), wj = ui_pill_width("Join", 1, h), x = cx - (wd + 24 + wj) / 2;
    button("Delete", ICON_BACK, x, ry, h, PILL_SECONDARY, foc && s->focus == PAD_DEL, 100 + PAD_DEL, NULL);
    button("Join", ICON_CHECK, x + wd + 24, ry, h, strlen(s->code) == CODE_LEN ? PILL_PRIMARY : PILL_GHOST, foc && s->focus == PAD_JOIN, 100 + PAD_JOIN, NULL);
    if (!gfx_handheld()) font_draw_center("OK  type   \xC2\xB7   Back  return", cx, H - 60, FONT_XS, THEME_TEXT_MUTED);
}

static const char *member_state(const PartyView *v, const PartyMember *m)
{
    if (!v->has_title) return "In the party";
    if (m->buffering) return "Catching up\xE2\x80\xA6";
    return m->ready ? "Ready" : "Loading\xE2\x80\xA6";
}

static void draw_party(Party *s, float k)
{
    float W = SNTV_UI_W, H = SNTV_UI_H;
    int a = (int)(255 * k), foc = !gfx_handheld() || s->keyed;
    PartyView v;
    party_view(&v);
    if (v.state != PARTY_IN) {
        float cx = W / 2, y = H / 2 - 120;
        font_draw_center("Cue Party", cx, y - 120, FONT_XL | FONT_B, COLOR_ALPHA(THEME_TEXT, a));
        if (v.state == PARTY_CONNECTING) {
            ui_spinner(cx, y + 30, 26);
            font_draw_center(s->code[0] ? "Joining the party\xE2\x80\xA6" : "Starting the party\xE2\x80\xA6", cx, y + 90, FONT_M, THEME_TEXT_DIM);
        } else {
            ui_center_wrapped(v.error[0] ? v.error : "The party has ended", cx, y, FONT_M, THEME_TEXT, W - 160);
            float h = 80, w = ui_pill_width("Back", 1, h);
            button("Back", ICON_BACK, cx - w / 2, y + 140, h, PILL_PRIMARY, foc, 30, NULL);
        }
        return;
    }
    qr_for(s, v.code);
    char code[12];
    spaced(v.code, code, sizeof code);
    char line[200];
    if (SNTV_PHONE) {
        float cx = W / 2, y = 130;
        font_draw_center("Cue Party", cx, y, FONT_L | FONT_B, COLOR_ALPHA(THEME_TEXT, a));
        font_draw_center(code, cx, y + 70, FONT_HUGE | FONT_B, THEME_ACCENT);
        ui_center_wrapped("On another device: Cue Party, Join with a code. Or scan this with a phone's camera.", cx, y + 200, FONT_S, THEME_TEXT_DIM, W - 100);
        float qs = 300, qy = y + 300;
        if (s->qr_ready) { ui_panel(cx - qs / 2 - 24, qy - 24, qs + 48, qs + 48, R_PANEL, RGBA(255, 255, 255, 0xFF)); ui_qr(s->qr, cx - qs / 2, qy, qs); }
        float ly = qy + qs + 80;
        snprintf(line, sizeof line, "%d watching", v.n_members);
        font_draw(line, 60, ly, FONT_S | FONT_B, THEME_TEXT);
        for (int i = 0; i < v.n_members; i++) {
            const PartyMember *m = &v.members[i];
            float yy = ly + 56 + i * 64;
            gfx_circle(80, yy + 20, 8, m->ready || !v.has_title ? THEME_OK : THEME_TEXT_MUTED);
            snprintf(line, sizeof line, "%s%s", m->name, strcmp(m->id, v.me) ? "" : " (this one)");
            font_draw_fit(line, 110, yy, FONT_S, THEME_TEXT, W - 420);
            font_draw(member_state(&v, m), W - 300, yy, FONT_S, THEME_TEXT_DIM);
        }
        float h = 76, w = ui_pill_width("Leave the party", 1, h);
        button("Leave the party", ICON_CLOSE, cx - w / 2, H - 200, h, PILL_SECONDARY, foc && s->focus == 0, 20, NULL);
        return;
    }
    float x = THEME_MARGIN, y = 150, qs = 360, qx = W - THEME_MARGIN - qs - 60, qy = 220;
    font_draw("Cue Party", x, y, FONT_XL | FONT_B, COLOR_ALPHA(THEME_TEXT, a));
    font_draw("Party code", x, y + 110, FONT_XS, THEME_TEXT_MUTED);
    font_draw(code, x, y + 140, FONT_HUGE | FONT_B, THEME_ACCENT);
    font_draw_wrapped("On another TV: Cue Party, Join with a code, then type this code. On a phone, scan the QR code.", x, y + 270, FONT_S, THEME_TEXT_DIM, qx - x - 120, 2);
    if (v.has_title) {
        snprintf(line, sizeof line, "Watching  %s", v.title[0] ? v.title : v.video);
        font_draw_fit(line, x, y + 370, FONT_S | FONT_B, THEME_TEXT, qx - x - 120);
    } else font_draw_fit("Play something: everyone in the party starts it together.", x, y + 370, FONT_S, THEME_TEXT, qx - x - 120);
    float ly = y + 450;
    snprintf(line, sizeof line, "%d watching", v.n_members);
    font_draw(line, x, ly, FONT_XS | FONT_B, THEME_TEXT_MUTED);
    for (int i = 0; i < v.n_members; i++) {
        const PartyMember *m = &v.members[i];
        float yy = ly + 44 + i * 52;
        gfx_circle(x + 10, yy + 18, 8, m->ready || !v.has_title ? THEME_OK : THEME_TEXT_MUTED);
        snprintf(line, sizeof line, "%s%s", m->name, strcmp(m->id, v.me) ? "" : "  (this TV)");
        font_draw_fit(line, x + 36, yy, FONT_S, THEME_TEXT, 350);
        font_draw(member_state(&v, m), x + 400, yy, FONT_S, THEME_TEXT_DIM);
    }
    if (s->qr_ready) {
        ui_panel(qx - 30, qy - 30, qs + 60, qs + 60, R_PANEL, RGBA(255, 255, 255, 0xFF));
        ui_qr(s->qr, qx, qy, qs);
        font_draw_center("A phone with Cuelight: scan to join", qx + qs / 2, qy + qs + 56, FONT_XS, THEME_TEXT_MUTED);
    }
    float h = 76;
    button("Leave the party", ICON_CLOSE, x, H - 190, h, PILL_SECONDARY, foc && s->focus == 0, 20, NULL);
    if (v.reconnecting) font_draw("The party isn't answering; trying again\xE2\x80\xA6", x + 420, H - 170, FONT_S, THEME_DANGER);
    font_draw_center("Back  return   \xC2\xB7   OK  leave the party", W / 2, H - 60, FONT_XS, THEME_TEXT_MUTED);
}

static void draw(Screen *sc)
{
    Party *s = sc->st;
    if (s->player) gfx_rect(0, 0, SNTV_UI_W, SNTV_UI_H, RGBA(0, 0, 0, 0xC8));   /* over the video: it goes on behind */
    else ui_background(0);
    float k = g_app.fx_light ? 1 : s->intro;
    if (s->view == V_CHOOSE) draw_choose(s, k);
    else if (s->view == V_JOIN) draw_join(s, k);
    else draw_party(s, k);
}

static int key(Screen *sc, KeyAction a)
{
    Party *s = sc->st;
    app_redraw();
    if (a == KEY_LEFT || a == KEY_RIGHT || a == KEY_UP || a == KEY_DOWN) { s->keyed = 1; s->focus_t = 0; }
    if (s->view == V_CHOOSE) {
        if (a == KEY_BACK) { app_pop(); return 1; }
        if (a == KEY_LEFT || a == KEY_UP) s->focus = 0;
        else if (a == KEY_RIGHT || a == KEY_DOWN) s->focus = 1;
        else if (a == KEY_OK) { if (s->focus == 0) begin(s, 0); else { s->view = V_JOIN; s->focus = 0; s->code[0] = 0; } }
        return 1;
    }
    if (s->view == V_JOIN) {
        int cols = SNTV_PHONE ? 6 : PAD_COLS, rows = (PAD_KEYS + cols - 1) / cols;
        if (a == KEY_BACK) { s->view = V_CHOOSE; s->focus = 1; return 1; }
        if (a == KEY_BACKSPACE) { size_t n = strlen(s->code); if (n) s->code[n - 1] = 0; return 1; }
        int f = s->focus;
        if (f < PAD_KEYS) {
            int c = f % cols, r = f / cols;
            if (a == KEY_LEFT && c > 0) f--;
            else if (a == KEY_RIGHT && c < cols - 1 && f + 1 < PAD_KEYS) f++;
            else if (a == KEY_UP && r > 0) f -= cols;
            else if (a == KEY_DOWN) f = r < rows - 1 && f + cols < PAD_KEYS ? f + cols : (c < cols / 2 ? PAD_DEL : PAD_JOIN);
        } else {
            if (a == KEY_LEFT) f = PAD_DEL;
            else if (a == KEY_RIGHT) f = PAD_JOIN;
            else if (a == KEY_UP) f = (rows - 1) * cols + (f == PAD_DEL ? 1 : cols - 2);
        }
        s->focus = f;
        if (a == KEY_OK) {
            if (f < PAD_KEYS) type_char(s, ALPHABET[f]);
            else if (f == PAD_DEL) { size_t n = strlen(s->code); if (n) s->code[n - 1] = 0; }
            else if (strlen(s->code) == CODE_LEN) begin(s, 1);
        }
        return 1;
    }
    PartyView v;
    party_view(&v);
    if (a == KEY_BACK) {
        if (v.state == PARTY_ENDED) party_leave();   /* (clears it) */
        app_pop();
        return 1;
    }
    if (a == KEY_OK) {
        if (v.state == PARTY_ENDED) { party_leave(); s->view = V_CHOOSE; s->focus = 0; }
        else if (v.state == PARTY_IN) { party_leave(); app_toast("You left the party"); app_pop(); }
    }
    return 1;
}

static int tap(Screen *sc, int id)
{
    Party *s = sc->st;
    if (s->view == V_CHOOSE && (id == 10 || id == 11)) { s->focus = id - 10; return 0; }
    if (s->view == V_JOIN && id >= 100 && id <= 100 + PAD_JOIN) { s->focus = id - 100; return 0; }
    if (s->view == V_PARTY && (id == 20 || id == 30)) { s->focus = 0; return 0; }
    return 1;
}

static void text(Screen *sc, const char *utf8)
{
    Party *s = sc->st;
    if (s->view != V_JOIN) return;
    for (const char *p = utf8; *p; p++) type_char(s, *p);
}

static void destroy(Screen *sc) { free(sc->st); free(sc); }

/* code: join that party at once (a link); NULL: start, join, or show the party this one is in. */
Screen *screen_party_create(const char *code)
{
    Screen *sc = calloc(1, sizeof *sc);
    Party *s = calloc(1, sizeof *s);
    s->focus_t = 1;
    Screen *under = app_top();
    if (under && under->name && !strcmp(under->name, "player")) s->player = under;
    if (party_active()) s->view = V_PARTY;
    else if (code && *code) {
        for (const char *p = code; *p && strlen(s->code) < CODE_LEN; p++) type_char(s, *p);
        if (strlen(s->code) == CODE_LEN) begin(s, 1); else s->view = V_JOIN;
    }
    sc->name = "party"; sc->st = s;
    sc->update = update; sc->draw = draw; sc->key = key; sc->text = text; sc->tap = tap; sc->destroy = destroy; sc->phone = 1;
    return sc;
}
