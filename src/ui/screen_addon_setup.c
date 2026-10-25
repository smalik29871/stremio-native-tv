/* An addon's own settings, set on a phone: the TV has no browser for the addon's configure
 * page. The code opens this TV's phone page, which links to that page and takes back the
 * install link it ends with; the addon set up that way takes the old one's place (app.c).
 * Without the phone page (no home network), the code opens the addon's page itself. */
#include "../app.h"
#include "../log.h"
#include "../phone.h"
#include "font.h"
#include "qrcodegen.h"
#include "theme.h"
#include "widgets.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char name[96], id[160];
    uint8_t qr[qrcodegen_BUFFER_LEN_MAX];
    int qr_ready, via_phone;   /* via_phone: the code is this TV's page, not the addon's */
    int sent, seen_version;
    float sent_at, t;
    char status[200];
} Setup;

enum { HIT_OUTSIDE = 1, HIT_PANEL };

static void update(Screen *s, float dt)
{
    Setup *o = s->st;
    if (o->t < 1) { o->t += dt / 0.28f; if (o->t > 1) o->t = 1; app_redraw(); }
    char got[4096];
    if (phone_take(got, sizeof got)) {
        const char *g = got;
        while (*g == ' ') g++;
        if ((strncmp(g, "stremio://", 10) && strncmp(g, "http", 4)) || !strstr(g, "manifest.json")) {
            snprintf(o->status, sizeof o->status, "That is not an install link: it ends in manifest.json. Send it again from your phone.");
            phone_prompt("Send the new install link", "Paste the install link the addon's page gave you (stremio://\xE2\x80\xA6 or https://\xE2\x80\xA6/manifest.json).", "");
            app_redraw();
            return;
        }
        LOGI("addons: a new link for %s came from the phone", o->name);
        o->sent = 1; o->sent_at = g_app.time; o->seen_version = g_app.addons_version;
        snprintf(o->status, sizeof o->status, "Received. Updating %s\xE2\x80\xA6", o->name);
        app_install_addon(g);
        app_redraw();
    }
    if (o->sent && g_app.addons_version != o->seen_version) { app_pop(); return; }   /* installed: its toast says so */
    if (o->sent && g_app.time - o->sent_at > 15) {
        o->sent = 0;
        snprintf(o->status, sizeof o->status, "That link did not install. Check it on the addon's page and send it again.");
        phone_prompt("Send the new install link", "Paste the install link the addon's page gave you (stremio://\xE2\x80\xA6 or https://\xE2\x80\xA6/manifest.json).", "");
        app_redraw();
    }
    if (o->sent) app_redraw();
}

static void step(int n, const char *text, float x, float y, float w, float *out_h)
{
    char no[8]; snprintf(no, sizeof no, "%d", n);
    gfx_circle(x + 22, y + 22, 22, COLOR_ALPHA(THEME_ACCENT, 0x30));
    font_draw(no, x + 22 - font_measure(no, FONT_S | FONT_B) / 2, y + 22 - font_height(FONT_S | FONT_B) / 2, FONT_S | FONT_B, THEME_ACCENT);
    int lines = font_draw_wrapped(text, x + 68, y + 4, FONT_S, THEME_TEXT, w - 68, 4);
    *out_h = (lines < 1 ? 1 : lines) * font_height(FONT_S) + 30;
}

static void draw(Screen *s)
{
    Setup *o = s->st;
    float k = g_app.fx_light ? 1 : 1 - (1 - o->t) * (1 - o->t) * (1 - o->t);
    Screen *below = g_app.depth >= 2 ? g_app.stack[g_app.depth - 2] : NULL;
    if (below && below->draw) { ui_hits_begin(below); below->draw(below); ui_hits_begin(s); } else ui_background(0);
    gfx_rect(0, 0, SNTV_UI_W, SNTV_UI_H, RGBA(0, 0, 0, (int)(170 * k)));
    ui_hit(0, 0, SNTV_UI_W, SNTV_UI_H, HIT_OUTSIDE);
    int phone = SNTV_PHONE;
    float w = phone ? SNTV_UI_W - 40 : 1500, h = phone ? SNTV_UI_H - 160 : 760;
    float x = (SNTV_UI_W - w) / 2, y = (SNTV_UI_H - h) / 2 + (1 - k) * 50, pad = 56;
    gfx_set_alpha(k);
    if (!g_app.fx_light) gfx_shadow(x, y + 16, w, h, R_PANEL, 40, RGBA(0, 0, 0, 0x90));
    ui_panel(x, y, w, h, R_PANEL, THEME_PANEL);
    ui_hit(x, y, w, h, HIT_PANEL);
    float qs = phone ? w - 2 * pad - 120 : 420, qx = phone ? x + (w - qs) / 2 : x + pad, qy = y + pad;
    if (o->qr_ready) {
        gfx_rrect(qx - 18, qy - 18, qs + 36, qs + 36, 18, RGBA(0xFF, 0xFF, 0xFF, 0xFF));
        ui_qr(o->qr, qx, qy, qs);
    }
    const char *cap = o->via_phone ? "Scan with your phone" : "Scan to open its settings";
    font_draw(cap, qx + (qs - font_measure(cap, FONT_XS | FONT_B)) / 2, qy + qs + 34, FONT_XS | FONT_B, THEME_TEXT_DIM);
    float tx = phone ? x + pad : qx + qs + 80, ty = phone ? qy + qs + 100 : y + pad, tw = x + w - pad - tx;
    font_draw_tracked("ADDON SETTINGS", tx, ty, FONT_XS | FONT_B, THEME_ACCENT, 1.2f);
    ty += 40;
    font_draw_fit(o->name, tx, ty, FONT_L | FONT_B, THEME_TEXT, tw);
    ty += font_height(FONT_L | FONT_B) + 34;
    char a[300], b[400], c[300];
    float sh;
    if (o->via_phone) {
        snprintf(a, sizeof a, "Scan the code. Your phone opens a page from this TV.");
        snprintf(b, sizeof b, "Tap \xE2\x80\x9COpen %s's settings\xE2\x80\x9D, choose what you like, then copy the install link it gives you (press and hold its Install button, Copy link).", o->name);
        snprintf(c, sizeof c, "Paste it on the page and send. %s is updated here, in the same place in your list.", o->name);
    } else {
        snprintf(a, sizeof a, "Scan the code to open %s's settings on your phone and choose what you like.", o->name);
        snprintf(b, sizeof b, "Copy the install link it gives you (press and hold its Install button, Copy link).");
        snprintf(c, sizeof c, "On this TV, Settings > Addons > Install addon from URL takes that link: %s is updated in its place.", o->name);
    }
    step(1, a, tx, ty, tw, &sh); ty += sh;
    step(2, b, tx, ty, tw, &sh); ty += sh;
    step(3, c, tx, ty, tw, &sh); ty += sh;
    if (o->status[0]) {
        ty += 6;
        font_draw_wrapped(o->status, tx, ty, FONT_S | FONT_B, o->sent ? THEME_ACCENT : THEME_TEXT, tw, 3);
    } else if (o->via_phone) {
        font_draw_wrapped("Waiting for the link from your phone\xE2\x80\xA6", tx, ty + 6, FONT_XS, THEME_TEXT_MUTED, tw, 2);
    }
    font_draw("Back closes", x + w - pad - font_measure("Back closes", FONT_XS), y + h - pad + 8, FONT_XS, THEME_TEXT_MUTED);
    gfx_set_alpha(1);
}

static int key(Screen *s, KeyAction a)
{
    (void)s;
    if (a == KEY_BACK) { app_pop(); return 1; }
    return 1;
}

static int tap(Screen *s, int id)
{
    (void)s;
    if (g_touch_aim) return 1;
    if (id == HIT_OUTSIDE) app_pop();
    return 1;
}

static void destroy(Screen *s)
{
    phone_prompt(NULL, NULL, NULL);
    free(s->st); free(s);
}

Screen *screen_addon_setup_create(const Addon *a)
{
    Screen *s = calloc(1, sizeof *s);
    Setup *o = calloc(1, sizeof *o);
    snprintf(o->name, sizeof o->name, "%s", a->manifest.name ? a->manifest.name : "This addon");
    snprintf(o->id, sizeof o->id, "%s", a->manifest.id ? a->manifest.id : "");
    char page[2048], code[200];
    snprintf(page, sizeof page, "%s/configure", a->base_url);
    o->via_phone = phone_url(code, sizeof code);
    if (o->via_phone) {
        char label[160];
        snprintf(label, sizeof label, "Open %s's settings", o->name);
        phone_prompt("Send the new install link", "When you have saved your choices, paste the install link the addon's page gave you (stremio://\xE2\x80\xA6 or https://\xE2\x80\xA6/manifest.json).", "");
        phone_prompt_link(label, page);
    }
    uint8_t tmp[qrcodegen_BUFFER_LEN_MAX];
    o->qr_ready = qrcodegen_encodeText(o->via_phone ? code : page, tmp, o->qr, qrcodegen_Ecc_MEDIUM, qrcodegen_VERSION_MIN, 25, qrcodegen_Mask_AUTO, true);
    LOGI("addons: setting up %s %s", o->name, o->via_phone ? "through the phone page" : "on the addon's own page");   /* never the link: it can hold a key */
    s->name = "addon_setup"; s->st = o;
    s->update = update; s->draw = draw; s->key = key; s->tap = tap; s->destroy = destroy;
    Screen *under = app_top();
    s->phone = under ? under->phone : 1;
    return s;
}
