/* Sync with another device. Opened from Settings it shows this device's code: a phone's
 * camera opens the page behind it (served by phone.c on the home network), which hands over
 * to Cuelight on that phone with a cuelight://sync link. Opened with that link it is the
 * other side: it fetches the device's bundle and offers to sync both ways, or to make one
 * like the other. Nothing is removed on either side (app_sync_apply). */
#include "../app.h"
#include "../http.h"
#include "../log.h"
#include "../phone.h"
#include "../vendor/cJSON.h"
#include "font.h"
#include "gfx.h"
#include "qrcodegen.h"
#include "theme.h"
#include "widgets.h"
#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { C_LOADING, C_CHOOSE, C_WORKING, C_DONE, C_ERROR };
enum { DO_BOTH, DO_SEND, DO_BRING, DO_COUNT };

enum { H_CODE, H_FIND, H_PIN };   /* host: its code; looking for devices; typing another's number */

typedef struct {
    int host;   /* showing this device's code */
    int hmode, hfocus;   /* hfocus: the device row (H_FIND) or the pad key (H_PIN) */
    PhoneFound found[PHONE_FOUND_MAX]; int n_found, scan_done, pick;
    char pin[8], pin_msg[200]; int pairing;
    uint8_t qr[qrcodegen_BUFFER_LEN_MAX]; int qr_ready; char url[160];
    char done[400];   /* host: what the last sync brought */
    /* the other side */
    char base[200], key[40], device[40];
    char *theirs;     /* their bundle */
    char facts[300];
    int state, focus, keyed; float focus_t, intro;   /* keyed: arrows used, so the focus shows on a phone too */
    char msg[500];
} Sync;

static Sync *g_live;   /* answers for a screen already closed are dropped */

/* "the LG TV", "this phone", "the other phone" */
static void dev_ref(const char *name, int mine, char *out, size_t n)
{
    char low[40];
    snprintf(low, sizeof low, "%s", name && *name ? name : "device");
    if (!strcmp(low, "Phone") || !strcmp(low, "Computer")) low[0] = (char)tolower((unsigned char)low[0]);
    if (mine) snprintf(out, n, "this %s", low);
    else snprintf(out, n, "the %s%s", !strcmp(name ? name : "", app_device_name()) ? "other " : "", low);
}

static int from_hex(char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; }

static void link_param(const char *link, const char *name, char *out, size_t n)
{
    out[0] = 0;
    const char *q = strchr(link, '?');
    size_t nl = strlen(name);
    for (const char *p = q; p && *p; p = strchr(p + 1, '&')) {
        if (strncmp(p + 1, name, nl) || p[1 + nl] != '=') continue;
        size_t o = 0;
        for (const char *v = p + 2 + nl; *v && *v != '&' && o + 1 < n; v++) {
            if (*v == '%' && from_hex(v[1]) >= 0 && from_hex(v[2]) >= 0) { out[o++] = (char)(from_hex(v[1]) * 16 + from_hex(v[2])); v += 2; }
            else out[o++] = *v == '+' ? ' ' : *v;
        }
        out[o] = 0;
        return;
    }
}

/* Only a device on the home network: a link from a web page must not send sign-ins elsewhere. */
static int lan_address(const char *base)
{
    unsigned a, b, c, d, port = 0;
    int end = 0;
    if (strncmp(base, "http://", 7)) return 0;
    const char *h = base + 7;
    for (const char *p = h; *p; p++) if (!(isdigit((unsigned char)*p) || *p == '.' || *p == ':' || *p == '/')) return 0;   /* an IP, a port: no names, no user@ */
    if (sscanf(h, "%3u.%3u.%3u.%3u%n", &a, &b, &c, &d, &end) != 4 || a > 255 || b > 255 || c > 255 || d > 255) return 0;
    h += end;
    if (*h == ':') { int e2 = 0; if (sscanf(h + 1, "%5u%n", &port, &e2) != 1 || port == 0 || port > 65535) return 0; h += 1 + e2; }
    if (*h && strcmp(h, "/")) return 0;   /* nothing after the address */
    return a == 10 || a == 127 || (a == 192 && b == 168) || (a == 172 && b >= 16 && b <= 31);
}

static void host_offer(Sync *s)
{
    char *b = app_sync_bundle();
    s->qr_ready = 0;
    if (b && phone_offer_sync(b, app_device_name(), s->url, sizeof s->url)) {
        uint8_t tmp[qrcodegen_BUFFER_LEN_MAX];
        s->qr_ready = qrcodegen_encodeText(s->url, tmp, s->qr, qrcodegen_Ecc_MEDIUM, qrcodegen_VERSION_MIN, 10, qrcodegen_Mask_AUTO, true);
    }
    free(b);
}

static void fail(Sync *s, const char *fmt, const char *detail)
{
    s->state = C_ERROR;
    snprintf(s->msg, sizeof s->msg, fmt, detail ? detail : "");
    LOGW("sync: %s", s->msg);
    app_redraw();
}

static void on_bundle(HttpResponse *r, void *user)
{
    Sync *s = user;
    if (s != g_live) return;
    cJSON *b = r->status == 200 ? cJSON_Parse(r->body) : NULL;
    const cJSON *tag = cJSON_GetObjectItemCaseSensitive(b, "_sync");
    if (!cJSON_IsString(tag) || strcmp(tag->valuestring, "cuelight")) {
        cJSON_Delete(b);
        if (r->status == 404) fail(s, "That code has expired. Open Sync with another device on the other screen again and scan the new code.", NULL);
        else fail(s, "Could not reach it (%s). Both need to be on the same home network.", r->status ? "it answered something else" : r->error);
        return;
    }
    const cJSON *dev = cJSON_GetObjectItemCaseSensitive(b, "device"), *sign = cJSON_GetObjectItemCaseSensitive(b, "signin");
    snprintf(s->device, sizeof s->device, "%s", cJSON_IsString(dev) ? dev->valuestring : "device");
    int na = cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(b, "addons")), nl = cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(b, "library"));
    const cJSON *email = cJSON_GetObjectItemCaseSensitive(sign, "email");
    char who[160] = "";
    if (cJSON_IsString(cJSON_GetObjectItemCaseSensitive(sign, "authKey")) && cJSON_IsString(email)) snprintf(who, sizeof who, ", synced with Stremio as %s", email->valuestring);
    else if (cJSON_IsString(cJSON_GetObjectItemCaseSensitive(sign, "authKey"))) snprintf(who, sizeof who, ", synced with Stremio");
    int trk = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(b, "trakt")), smk = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(b, "simkl"));
    char trackers[160] = "";
    if (trk || smk) snprintf(trackers, sizeof trackers, " %s stay%s there: connect %s here too, in Settings > Accounts.", trk && smk ? "Trakt and SimKL" : trk ? "Trakt" : "SimKL",
                             trk && smk ? "" : "s", trk && smk ? "them" : "it");
    snprintf(s->facts, sizeof s->facts, "It has %d addon%s and %d title%s in its library%s.%s", na, na == 1 ? "" : "s", nl, nl == 1 ? "" : "s", who, trackers);
    cJSON_Delete(b);
    free(s->theirs);
    s->theirs = r->body; r->body = NULL;
    s->state = C_CHOOSE;
    LOGI("sync: found %s on the network", s->device);
    app_redraw();
}

/* The right number: the other device's key. From here this is the other side, as if its
 * code had been scanned. */
static void on_paired(HttpResponse *r, void *user)
{
    Sync *s = user;
    if (s != g_live) return;
    s->pairing = 0;
    cJSON *j = r->status == 200 ? cJSON_Parse(r->body) : NULL;
    const cJSON *k = cJSON_GetObjectItemCaseSensitive(j, "key");
    int key_ok = cJSON_IsString(k) && k->valuestring[0] && strlen(k->valuestring) < sizeof s->key;
    for (const char *p = key_ok ? k->valuestring : ""; *p; p++) if (!isalnum((unsigned char)*p)) key_ok = 0;
    if (key_ok) {
        snprintf(s->key, sizeof s->key, "%s", k->valuestring);
        snprintf(s->base, sizeof s->base, "%s", s->found[s->pick].base);
        cJSON_Delete(j);
        phone_offer_sync(NULL, NULL, NULL, 0);   /* this one's own code closes: it is the other side now */
        s->host = 0;
        s->state = C_LOADING; s->focus = 0; s->focus_t = 1;
        LOGI("sync: paired with %s by its number", s->found[s->pick].device);
        char url[300];
        snprintf(url, sizeof url, "%ssync/%s/bundle", s->base, s->key);
        http_request_async(url, NULL, on_bundle, s);
        app_redraw();
        return;
    }
    const cJSON *left = cJSON_GetObjectItemCaseSensitive(j, "left");
    s->pin[0] = 0;
    if (r->status == 403 && cJSON_IsNumber(left) && left->valueint > 0)
        snprintf(s->pin_msg, sizeof s->pin_msg, "That is not the number. %d tr%s left.", left->valueint, left->valueint == 1 ? "y" : "ies");
    else if (r->status == 403) snprintf(s->pin_msg, sizeof s->pin_msg, "Too many tries: the other device shows a new number now.");
    else if (r->status == 404) snprintf(s->pin_msg, sizeof s->pin_msg, "The other device closed its code. Open Sync with another device on it again.");
    else snprintf(s->pin_msg, sizeof s->pin_msg, "Could not reach it (%.120s).", r->status ? "it answered something else" : r->error);
    LOGI("sync: pairing failed (%ld)", r->status);
    cJSON_Delete(j);
    app_redraw();
}

static void pin_digit(Sync *s, char d)
{
    size_t L = strlen(s->pin);
    if (s->pairing || L >= 4) return;
    s->pin[L] = d; s->pin[L + 1] = 0;
    s->pin_msg[0] = 0;
    if (L + 1 < 4) return;
    s->pairing = 1;
    char url[200], body[16];
    snprintf(url, sizeof url, "%ssync/pair", s->found[s->pick].base);
    snprintf(body, sizeof body, "pin=%s", s->pin);
    HttpOptions o = { .post_json = body, .timeout_s = 8 };
    http_request_async_ex(url, &o, on_paired, s);
}

static void find_start(Sync *s)
{
    s->hmode = H_FIND; s->hfocus = 0; s->n_found = 0; s->scan_done = 0;
    phone_scan_start();
    LOGI("sync: finding devices on the network");
}

static void on_sent(HttpResponse *r, void *user)
{
    Sync *s = user;
    if (s != g_live) return;
    char there[64];
    dev_ref(s->device, 0, there, sizeof there);
    if (r->status != 200) { fail(s, "It did not go through (%s). Try again.", r->status ? "the code may have expired" : r->error); return; }
    char line[160], here[64];
    dev_ref(app_device_name(), 1, here, sizeof here);
    if (s->msg[0]) snprintf(line, sizeof line, "%s got what was only on %s.", there, here);   /* both ways */
    else snprintf(line, sizeof line, "%s now has %s's setup.", there, here);
    line[0] = (char)toupper((unsigned char)line[0]);
    size_t L = strlen(s->msg);
    snprintf(s->msg + L, sizeof s->msg - L, "%s%s", L ? " " : "", line);
    s->state = C_DONE;
    LOGI("sync: sent this device's bundle to %s", s->device);
    app_redraw();
}

static void send_mine(Sync *s, const char *mode)
{
    char *mine = app_sync_bundle();
    if (!mine) { fail(s, "This device's setup could not be read.", NULL); return; }
    size_t L = strlen(mine) + 160;
    char *body = malloc(L);
    snprintf(body, L, "{\"mode\":\"%s\",\"from\":\"%s\",\"bundle\":%s}", mode, app_device_name(), mine);
    free(mine);
    app_sync_shared();
    char url[300];
    snprintf(url, sizeof url, "%ssync/%s/apply", s->base, s->key);
    HttpOptions o = { .post_json = body, .timeout_s = 90 };
    http_request_async_ex(url, &o, on_sent, s);
    free(body);
}

static void act(Sync *s, int what)
{
    char summary[300] = "", here[64];
    dev_ref(app_device_name(), 1, here, sizeof here);
    s->state = C_WORKING;
    s->msg[0] = 0;
    LOGI("sync: %s", what == DO_BOTH ? "both ways" : what == DO_SEND ? "this device's setup to the other" : "the other's setup to this device");
    if (what != DO_SEND) {
        if (app_sync_apply(s->theirs, what == DO_BRING, summary, sizeof summary) < 0) { fail(s, "What it sent could not be read.", NULL); return; }
        snprintf(s->msg, sizeof s->msg, what == DO_BRING ? "%s got %s, with its look and choices." : "%s got %s.", here, summary);
        s->msg[0] = (char)toupper((unsigned char)s->msg[0]);
    }
    if (what == DO_BRING) { s->state = C_DONE; app_redraw(); return; }
    send_mine(s, what == DO_BOTH ? "merge" : "look");
}

/* host: a bundle sent from the other device */
static void take_in(Sync *s)
{
    char *in = phone_take_sync();
    if (!in) return;
    cJSON *w = cJSON_Parse(in);
    free(in);
    const cJSON *mode = cJSON_GetObjectItemCaseSensitive(w, "mode"), *from = cJSON_GetObjectItemCaseSensitive(w, "from"), *b = cJSON_GetObjectItemCaseSensitive(w, "bundle");
    char *txt = cJSON_IsObject(b) ? cJSON_PrintUnformatted(b) : NULL;
    char summary[300] = "", there[64];
    dev_ref(cJSON_IsString(from) ? from->valuestring : "device", 0, there, sizeof there);
    int look = cJSON_IsString(mode) && !strcmp(mode->valuestring, "look");
    if (txt && app_sync_apply(txt, look, summary, sizeof summary) >= 0) {
        snprintf(s->done, sizeof s->done, "Synced with %s: this %s got %s.", there, app_device_name(), summary);
        app_toast("Synced with %s", there);
        host_offer(s);   /* what it has now, for the next one */
    } else snprintf(s->done, sizeof s->done, "What came from %s could not be read.", there);
    free(txt);
    cJSON_Delete(w);
    app_redraw();
}

static void update(Screen *sc, float dt)
{
    Sync *s = sc->st;
    s->intro = ui_ease(s->intro * 100, 100, NULL, dt, 9) / 100;
    s->focus_t = ui_ease(s->focus_t * 100, 100, NULL, dt, 16) / 100;
    if (s->host) { if (phone_sync_sent()) app_sync_shared(); take_in(s); }
    if (s->host && s->hmode == H_FIND) {
        int was = s->n_found;
        s->n_found = phone_scan_results(s->found, PHONE_FOUND_MAX, &s->scan_done);
        if (!s->scan_done || s->n_found != was) app_redraw();
    }
    if (s->pairing) app_redraw();
}

static void wrap_center(const char *t, float cx, float y, int size, Color c, float w) { ui_center_wrapped(t, cx, y, size, c, w); }

static void draw_host(Sync *s, float k)
{
    float W = SNTV_UI_W, H = SNTV_UI_H;
    int a = (int)(255 * k);
    char me[64];
    dev_ref(app_device_name(), 1, me, sizeof me);
    static const char *const STEPS[3] = { "Scan the code with your phone's camera", "Tap Open in Cuelight", "Choose: sync both ways, or make one like the other" };
    if (SNTV_PHONE) {
        /* all of it on one screen, the button included: the code takes what room is left, down to a
         * size still easy to scan, and the steps go before the button does on a very short screen */
        float cx = W / 2, y = H < 1300 ? 110 : 150, step = 76, below = 130 + 3 * step + 34 + 30 + 72 + 50;
        float qs = H - y - 200 - below;
        if (qs < 300) { step = 64; below -= 3 * 12; qs += 3 * 12; }   /* the steps closer together first */
        int steps = qs >= 240;
        if (!steps) qs = H - y - 200 - (below - 3 * step - 34);
        qs = qs > 440 ? 440 : qs < 240 ? 240 : qs;
        font_draw_center("Sync with another device", cx, y, FONT_L | FONT_B, COLOR_ALPHA(THEME_TEXT, a));
        wrap_center("Sign-ins, addons, library, progress, lists and ratings. Nothing is removed on either side.", cx, y + 70, FONT_S, THEME_TEXT_DIM, W - 100);
        float py = y + 200;
        if (s->qr_ready) {
            ui_panel(cx - qs / 2 - 30, py - 30, qs + 60, qs + 60, R_PANEL, RGBA(255, 255, 255, 0xFF));
            ui_qr(s->qr, cx - qs / 2, py, qs);
        } else wrap_center("Connect to your home network to show the code.", cx, py + 150, FONT_M, THEME_TEXT_DIM, W - 100);
        float ty = py + qs + 80;
        char pin[8] = "";
        if (s->qr_ready && phone_sync_pin(pin, sizeof pin)) {   /* under the code */
            char sp[40]; snprintf(sp, sizeof sp, "Number for another device:  %c %c %c %c", pin[0], pin[1], pin[2], pin[3]);
            font_draw_center(sp, cx, py + qs + 44, FONT_S | FONT_B, THEME_TEXT);
            ty += 50;
        }
        float yy = ty;
        if (s->done[0]) { ui_icon(ICON_CHECK, cx, ty, 40, THEME_OK); wrap_center(s->done, cx, ty + 40, FONT_S | FONT_B, THEME_TEXT, W - 100); yy = ty + 3 * step + 34; }
        else if (steps) for (int i = 0; i < 3; i++) {
            char num[4]; snprintf(num, sizeof num, "%d", i + 1);
            gfx_circle(70, yy + 16, 22, COLOR_ALPHA(THEME_ACCENT, 0xD0));
            font_draw_center(num, 70, yy + 16 - font_height(FONT_XS | FONT_B) / 2, FONT_XS | FONT_B, THEME_ON_ACCENT);
            int lines = font_draw_wrapped(STEPS[i], 110, yy, FONT_S, THEME_TEXT, W - 150, 2);
            yy += step + (lines > 1 ? 34 : 0);
        }
        float by = yy + 30, bw = ui_pill_width("Find a device on my network", 1, 72);
        ui_pill("Find a device on my network", 1, ICON_SEARCH, cx - bw / 2, by, 72, PILL_SECONDARY, s->hfocus == 0 && s->keyed);
        ui_hit(cx - bw / 2, by, bw, 72, 10);
        return;
    }
    float x = THEME_MARGIN, y = 170, qs = 380, qx = W - THEME_MARGIN - qs - 60, qy = 250;
    font_draw("Sync with another device", x, y, FONT_XL | FONT_B, COLOR_ALPHA(THEME_TEXT, a));
    char line[300];
    snprintf(line, sizeof line, "Your sign-ins, addons, library, progress, lists and ratings, between %s and another device with " SNTV_APP_NAME ". Nothing is removed on either side, and no account is needed.", me);
    font_draw_wrapped(line, x, y + 90, FONT_S, THEME_TEXT_DIM, qx - x - 120, 3);
    float sy = y + 260;
    if (s->done[0]) {
        gfx_circle(x + 30, sy + 26, 30, COLOR_ALPHA(THEME_OK, 0x30));
        ui_icon(ICON_CHECK, x + 30, sy + 26, 34, THEME_OK);
        font_draw_wrapped(s->done, x + 84, sy + 6, FONT_M | FONT_B, THEME_TEXT, qx - x - 200, 3);
        font_draw("Scan again to sync another device.", x + 84, sy + 140, FONT_S, THEME_TEXT_MUTED);
    } else for (int i = 0; i < 3; i++) {
        float yy = sy + i * 96;
        char num[4]; snprintf(num, sizeof num, "%d", i + 1);
        gfx_circle(x + 26, yy + 20, 26, COLOR_ALPHA(THEME_ACCENT, 0xD0));
        font_draw_center(num, x + 26, yy + 20 - font_height(FONT_S | FONT_B) / 2, FONT_S | FONT_B, THEME_ON_ACCENT);
        font_draw_fit(STEPS[i], x + 76, yy + 2, FONT_M, THEME_TEXT, qx - x - 200);
    }
    /* no camera (another TV): it finds this one on the network and types the number */
    float fy = sy + 3 * 96 + 40;
    font_draw("Another TV, or no camera?", x, fy, FONT_S | FONT_B, THEME_TEXT);
    font_draw_wrapped("On the other device choose Find a device on my network, then type the number under the code.", x, fy + 44, FONT_S, THEME_TEXT_DIM, qx - x - 200, 2);
    float bw = ui_pill("Find a device on my network", 1, ICON_SEARCH, x, fy + 130, 76, PILL_SECONDARY, 1);
    ui_hit(x, fy + 130, bw, 76, 10);
    char pin[8] = "";
    if (s->qr_ready) {
        ui_panel(qx - 30, qy - 30, qs + 60, qs + 60, R_PANEL, RGBA(255, 255, 255, 0xFF));
        ui_qr(s->qr, qx, qy, qs);
        if (phone_sync_pin(pin, sizeof pin)) {
            char sp[16]; snprintf(sp, sizeof sp, "%c %c %c %c", pin[0], pin[1], pin[2], pin[3]);
            font_draw_center("Number for another device", qx + qs / 2, qy + qs + 58, FONT_XS, THEME_TEXT_MUTED);
            font_draw_center(sp, qx + qs / 2, qy + qs + 92, FONT_XL | FONT_B, THEME_TEXT);
        }
        font_draw_center("Works on your home network, while this is open", qx + qs / 2, qy + qs + 200, FONT_XS, THEME_TEXT_MUTED);
    } else {
        ui_panel(qx - 30, qy - 30, qs + 60, qs + 60, R_PANEL, COLOR_ALPHA(THEME_TEXT, 0x0C));
        ui_center_wrapped("Connect this device to your home network to show the code.", qx + qs / 2, qy + qs / 2 - 40, FONT_S, THEME_TEXT_DIM, qs - 40);
    }
    if (!gfx_handheld()) font_draw_center("OK  find a device   \xC2\xB7   Back  close", W / 2, H - 80, FONT_XS, THEME_TEXT_MUTED);
}

static void draw_client(Sync *s, float k)
{
    float W = SNTV_UI_W, H = SNTV_UI_H;
    int a = (int)(255 * k);
    float x = SNTV_PHONE ? 50 : THEME_MARGIN, w = SNTV_PHONE ? W - 100 : 1180, y = SNTV_PHONE ? 150 : 150;
    char there[64], here[64], t[160];
    dev_ref(s->device, 0, there, sizeof there);
    dev_ref(app_device_name(), 1, here, sizeof here);
    snprintf(t, sizeof t, "Sync with %s", s->state == C_LOADING ? "another device" : there);
    font_draw_fit(t, x, y, SNTV_PHONE ? FONT_L | FONT_B : FONT_XL | FONT_B, COLOR_ALPHA(THEME_TEXT, a), w);
    y += SNTV_PHONE ? 80 : 100;
    if (s->state == C_LOADING || s->state == C_WORKING) {
        ui_spinner(x + 24, y + 30, 22);
        font_draw(s->state == C_LOADING ? "Finding it on your network\xE2\x80\xA6" : "Syncing\xE2\x80\xA6", x + 70, y + 14, FONT_M, THEME_TEXT_DIM);
        app_redraw();
        return;
    }
    if (s->state == C_ERROR || s->state == C_DONE) {
        int ok = s->state == C_DONE;
        gfx_circle(x + 30, y + 30, 30, COLOR_ALPHA(ok ? THEME_OK : THEME_DANGER, 0x30));
        ui_icon(ok ? ICON_CHECK : ICON_CLOSE, x + 30, y + 30, 34, ok ? THEME_OK : THEME_DANGER);
        int lines = font_draw_wrapped(s->msg, x + 84, y + 8, FONT_M, THEME_TEXT, w - 84, 6);
        float by = y + 8 + (lines > 1 ? lines : 1) * font_height(FONT_M) + 50;
        float bw = ui_pill(ok ? "Done" : "Try again", 0, 0, x + 84, by, 72, PILL_PRIMARY, 1);
        ui_hit(x + 84, by, bw, 72, 100);
        return;
    }
    font_draw_wrapped(s->facts, x, y, FONT_S, THEME_TEXT_DIM, w, 3);
    y += SNTV_PHONE ? 120 : 90;
    char tt[DO_COUNT][160], dd[DO_COUNT][300];
    snprintf(tt[DO_BOTH], 160, "Sync both ways");
    snprintf(dd[DO_BOTH], 300, "Each gets what the other has: sign-ins, addons, titles, progress, lists. Each keeps its own look.");
    snprintf(tt[DO_SEND], 160, "Make %s like %s", there, here);
    snprintf(dd[DO_SEND], 300, "It gets everything here, with %s's look, choices and sign-ins.", here);
    snprintf(tt[DO_BRING], 160, "Make %s like %s", here, there);
    snprintf(dd[DO_BRING], 300, "This gets everything there, with its look, choices and sign-ins.");
    float ch = SNTV_PHONE ? 200 : 150, gap = 24;
    for (int i = 0; i < DO_COUNT; i++) {
        float cy = y + i * (ch + gap);
        int foc = s->focus == i && (!gfx_handheld() || s->keyed);
        float f = foc ? (g_app.fx_light ? 1 : s->focus_t) : 0;
        if (foc) ui_focus_under(x, cy, w, ch, R_PANEL, f);
        gfx_rrect(x, cy, w, ch, R_PANEL, foc ? THEME_FOCUS : COLOR_ALPHA(THEME_TEXT, 0x10));
        Color fg = foc ? THEME_ON_FOCUS : THEME_TEXT, dim = foc ? COLOR_ALPHA(THEME_ON_FOCUS, 0xB0) : THEME_TEXT_DIM;
        ui_icon(i == DO_BOTH ? ICON_SWAP : i == DO_SEND ? ICON_CHEVRON_RIGHT : ICON_CHEVRON_LEFT, x + 52, cy + ch / 2, 40, fg);
        font_draw_fit(tt[i], x + 100, cy + 26, FONT_M | FONT_B, fg, w - 130);
        font_draw_wrapped(dd[i], x + 100, cy + 76, FONT_S, dim, w - 130, SNTV_PHONE ? 3 : 2);
        if (i == DO_BOTH) font_draw("Recommended", x + w - 40 - font_measure("Recommended", FONT_XS | FONT_B), cy + 32, FONT_XS | FONT_B, foc ? dim : THEME_ACCENT);
        ui_hit(x, cy, w, ch, i);
    }
    if (!gfx_handheld()) font_draw_center("OK  choose   \xC2\xB7   Back  close", W / 2, H - 80, FONT_XS, THEME_TEXT_MUTED);
}

static const char *const PAD[12] = { "1", "2", "3", "4", "5", "6", "7", "8", "9", "<", "0", "" };   /* 9: delete (drawn as an arrow) */

/* Find a device on my network: the Cuelights showing their code nearby */
static void draw_find(Sync *s, float k)
{
    float W = SNTV_UI_W, H = SNTV_UI_H;
    int a = (int)(255 * k);
    float x = SNTV_PHONE ? 50 : THEME_MARGIN, w = SNTV_PHONE ? W - 100 : 1180, y = 150;
    font_draw_fit("Find a device on my network", x, y, SNTV_PHONE ? FONT_L | FONT_B : FONT_XL | FONT_B, COLOR_ALPHA(THEME_TEXT, a), w);
    y += SNTV_PHONE ? 76 : 96;
    font_draw_wrapped("On the other device, open Settings > Accounts > Sync with another device. It shows up here.", x, y, FONT_S, THEME_TEXT_DIM, w, 3);
    y += SNTV_PHONE ? 120 : 90;
    float rh = SNTV_PHONE ? 110 : 104, gap = 18;
    for (int i = 0; i < s->n_found; i++) {
        float ry = y + i * (rh + gap);
        int foc = s->hfocus == i && (!gfx_handheld() || s->keyed);
        float f = foc ? (g_app.fx_light ? 1 : s->focus_t) : 0;
        if (foc) ui_focus_under(x, ry, w, rh, R_PANEL, f);
        gfx_rrect(x, ry, w, rh, R_PANEL, foc ? THEME_FOCUS : COLOR_ALPHA(THEME_TEXT, 0x10));
        Color fg = foc ? THEME_ON_FOCUS : THEME_TEXT, dim = foc ? COLOR_ALPHA(THEME_ON_FOCUS, 0xB0) : THEME_TEXT_MUTED;
        ui_icon(ICON_TV, x + 52, ry + rh / 2, 38, fg);
        font_draw_fit(s->found[i].device, x + 100, ry + 18, FONT_M | FONT_B, fg, w - 140);
        const char *hs = strstr(s->found[i].base, "://");
        char addr[64]; snprintf(addr, sizeof addr, "%.*s", (int)strcspn(hs ? hs + 3 : s->found[i].base, ":/"), hs ? hs + 3 : s->found[i].base);
        font_draw(addr, x + 100, ry + 62, FONT_XS, dim);
        ui_hit(x, ry, w, rh, 200 + i);
    }
    float ey = y + s->n_found * (rh + gap) + 10;
    if (!s->scan_done) {
        ui_spinner(x + 24, ey + 26, 20);
        font_draw("Looking on your home network\xE2\x80\xA6", x + 64, ey + 10, FONT_S, THEME_TEXT_DIM);
        app_redraw();
    } else {
        if (!s->n_found) font_draw_wrapped("Nothing found. Check that the other device shows its code, and that both are on the same Wi-Fi or network.", x, ey, FONT_S, THEME_TEXT, w, 3);
        float by = ey + (s->n_found ? 0 : 110);
        int foc = s->hfocus == s->n_found && (!gfx_handheld() || s->keyed);
        float bw = ui_pill("Look again", 1, ICON_RELOAD, x, by, 72, PILL_SECONDARY, foc);
        ui_hit(x, by, bw, 72, 300);
    }
    if (!gfx_handheld()) font_draw_center("OK  choose   \xC2\xB7   Back  return", W / 2, H - 80, FONT_XS, THEME_TEXT_MUTED);
}

/* the number the chosen device shows under its code */
static void draw_pin(Sync *s, float k)
{
    float W = SNTV_UI_W, H = SNTV_UI_H;
    int a = (int)(255 * k);
    char there[64], t[120];
    dev_ref(s->found[s->pick].device, 0, there, sizeof there);
    snprintf(t, sizeof t, "Sync with %s", there);
    float cx = W / 2, y = SNTV_PHONE ? 150 : 140;
    font_draw_center(t, cx, y, SNTV_PHONE ? FONT_L | FONT_B : FONT_XL | FONT_B, COLOR_ALPHA(THEME_TEXT, a));
    snprintf(t, sizeof t, "Type the number %s shows under its code.", there);
    ui_center_wrapped(t, cx, y + (SNTV_PHONE ? 70 : 90), FONT_S, THEME_TEXT_DIM, SNTV_PHONE ? W - 100 : 1100);
    float bs = SNTV_PHONE ? 96 : 104, bg = 22, bx = cx - (4 * bs + 3 * bg) / 2, byy = y + (SNTV_PHONE ? 170 : 170);
    for (int i = 0; i < 4; i++) {
        float xx = bx + i * (bs + bg);
        int cur = (int)strlen(s->pin) == i && !s->pairing;
        gfx_rrect(xx, byy, bs, bs * 1.2f, 18, COLOR_ALPHA(THEME_TEXT, 0x12));
        if (cur) gfx_rrect(xx + bs * 0.25f, byy + bs * 1.2f - 14, bs * 0.5f, 5, 2, THEME_ACCENT);
        if (s->pin[i]) { char d[2] = { s->pin[i], 0 }; font_draw_center(d, xx + bs / 2, byy + bs * 0.6f - font_height(FONT_XL | FONT_B) / 2, FONT_XL | FONT_B, THEME_TEXT); }
    }
    float my = byy + bs * 1.2f + 30;
    if (s->pairing) { ui_spinner(cx - 120, my + 18, 16); font_draw("Checking\xE2\x80\xA6", cx - 92, my + 4, FONT_S, THEME_TEXT_DIM); }
    else if (s->pin_msg[0]) ui_center_wrapped(s->pin_msg, cx, my, FONT_S, THEME_DANGER, SNTV_PHONE ? W - 100 : 1000);
    float ks = SNTV_PHONE ? 130 : 104, kh = SNTV_PHONE ? 96 : 84, kg = 16, kx = cx - (3 * ks + 2 * kg) / 2, ky = my + 80;
    for (int i = 0; i < 12; i++) {
        if (!PAD[i][0]) continue;
        float xx = kx + (i % 3) * (ks + kg), yy = ky + (i / 3) * (kh + kg);
        int foc = s->hfocus == i && (!gfx_handheld() || s->keyed);
        if (foc) ui_focus_under(xx, yy, ks, kh, 16, g_app.fx_light ? 1 : s->focus_t);
        gfx_rrect(xx, yy, ks, kh, 16, foc ? THEME_FOCUS : COLOR_ALPHA(THEME_TEXT, 0x14));
        if (i == 9) ui_icon(ICON_BACK, xx + ks / 2, yy + kh / 2, 34, foc ? THEME_ON_FOCUS : THEME_TEXT);   /* delete */
        else font_draw_center(PAD[i], xx + ks / 2, yy + kh / 2 - font_height(FONT_M | FONT_B) / 2, FONT_M | FONT_B, foc ? THEME_ON_FOCUS : THEME_TEXT);
        ui_hit(xx, yy, ks, kh, 400 + i);
    }
    if (!gfx_handheld()) font_draw_center("OK  type   \xC2\xB7   Back  return", W / 2, H - 80, FONT_XS, THEME_TEXT_MUTED);
}

static void draw(Screen *sc)
{
    Sync *s = sc->st;
    ui_background(0);
    float k = g_app.fx_light ? 1 : s->intro;
    if (s->host) { if (s->hmode == H_FIND) draw_find(s, k); else if (s->hmode == H_PIN) draw_pin(s, k); else draw_host(s, k); }
    else draw_client(s, k);
}

static int key(Screen *sc, KeyAction a)
{
    Sync *s = sc->st;
    app_redraw();
    if (s->host && s->hmode == H_FIND) {
        int n = s->n_found + (s->scan_done ? 1 : 0);   /* the devices, then Look again */
        if (a == KEY_BACK) { s->hmode = H_CODE; return 1; }
        if (a == KEY_UP || a == KEY_DOWN) s->keyed = 1;
        if (a == KEY_UP && s->hfocus > 0) { s->hfocus--; s->focus_t = 0; }
        else if (a == KEY_DOWN && s->hfocus < n - 1) { s->hfocus++; s->focus_t = 0; }
        else if (a == KEY_OK && s->hfocus < s->n_found) { s->pick = s->hfocus; s->hmode = H_PIN; s->hfocus = 0; s->pin[0] = 0; s->pin_msg[0] = 0; }
        else if (a == KEY_OK && s->scan_done) find_start(s);
        return 1;
    }
    if (s->host && s->hmode == H_PIN) {
        if (a == KEY_BACK) { if (!s->pairing) { s->hmode = H_FIND; s->hfocus = s->pick; } return 1; }
        if (a == KEY_BACKSPACE) { size_t L = strlen(s->pin); if (L && !s->pairing) s->pin[L - 1] = 0; return 1; }
        int c = s->hfocus % 3, r = s->hfocus / 3;
        if (a == KEY_LEFT || a == KEY_RIGHT || a == KEY_UP || a == KEY_DOWN) { s->keyed = 1; s->focus_t = 0; }
        if (a == KEY_LEFT && c > 0) c--;
        else if (a == KEY_RIGHT && c < 2 && !(r == 3 && c == 1)) c++;
        else if (a == KEY_UP && r > 0) r--;
        else if (a == KEY_DOWN && r < 3) { r++; if (r == 3 && c == 2) c = 1; }
        s->hfocus = r * 3 + c;
        if (a == KEY_OK) {
            if (s->hfocus == 9) { size_t L = strlen(s->pin); if (L && !s->pairing) s->pin[L - 1] = 0; }
            else if (PAD[s->hfocus][0]) pin_digit(s, PAD[s->hfocus][0]);
        }
        return 1;
    }
    if (a == KEY_BACK) { app_pop(); return 1; }
    if (s->host) { if (a == KEY_OK || (a == KEY_DOWN && gfx_handheld())) { if (a == KEY_DOWN) s->keyed = 1; else find_start(s); } return 1; }
    if (s->state == C_DONE) { if (a == KEY_OK) app_pop(); return 1; }
    if (s->state == C_ERROR) {
        if (a == KEY_OK) {
            if (s->theirs) s->state = C_CHOOSE;
            else { char url[300]; snprintf(url, sizeof url, "%ssync/%s/bundle", s->base, s->key); s->state = C_LOADING; http_request_async(url, NULL, on_bundle, s); }
        }
        return 1;
    }
    if (s->state != C_CHOOSE) return 1;
    if (a == KEY_UP || a == KEY_DOWN) s->keyed = 1;
    if (a == KEY_UP && s->focus > 0) { s->focus--; s->focus_t = 0; }
    else if (a == KEY_DOWN && s->focus < DO_COUNT - 1) { s->focus++; s->focus_t = 0; }
    else if (a == KEY_OK) act(s, s->focus);
    return 1;
}

static int tap(Screen *sc, int id)
{
    Sync *s = sc->st;
    if (id == 100) return 0;   /* Done / Try again: as OK */
    if (s->host && s->hmode == H_CODE && id == 10) { if (!g_touch_aim) find_start(s); return 1; }
    if (s->host && s->hmode == H_FIND && id >= 200 && id < 200 + s->n_found) { s->hfocus = id - 200; return 0; }
    if (s->host && s->hmode == H_FIND && id == 300) { s->hfocus = s->n_found; return 0; }
    if (s->host && s->hmode == H_PIN && id >= 400 && id < 412) { s->hfocus = id - 400; return 0; }
    if (s->state == C_CHOOSE && id >= 0 && id < DO_COUNT) { s->focus = id; return 0; }
    return 1;
}

static void text(Screen *sc, const char *utf8)
{
    Sync *s = sc->st;
    if (!s->host || s->hmode != H_PIN) return;
    for (const char *p = utf8; *p; p++) if (*p >= '0' && *p <= '9') pin_digit(s, *p);
    app_redraw();
}

static void resume(Screen *sc) { Sync *s = sc->st; if (s->host) host_offer(s); }

static void destroy(Screen *sc)
{
    Sync *s = sc->st;
    if (s->host) phone_offer_sync(NULL, NULL, NULL, 0);
    if (g_live == s) g_live = NULL;
    free(s->theirs);
    free(s);
    free(sc);
}

Screen *screen_sync_create(const char *link)
{
    Screen *sc = calloc(1, sizeof *sc);
    Sync *s = calloc(1, sizeof *s);
    g_live = s;
    s->focus_t = 1;
    if (!link) {
        s->host = 1;
        host_offer(s);
        LOGI("sync: showing this device's code%s", s->qr_ready ? "" : " (no network address)");
    } else {
        link_param(link, "u", s->base, sizeof s->base);
        link_param(link, "k", s->key, sizeof s->key);
        size_t bl = strlen(s->base);
        if (bl && s->base[bl - 1] != '/' && bl + 1 < sizeof s->base) strcat(s->base, "/");
        int key_ok = s->key[0] != 0;
        for (const char *p = s->key; *p; p++) if (!isalnum((unsigned char)*p)) key_ok = 0;
        if (!lan_address(s->base) || !key_ok) {
            s->state = C_ERROR;
            snprintf(s->msg, sizeof s->msg, "That code is not from a device on your home network. Scan the code in Settings > Sync with another device.");
            LOGW("sync: a link that is not a device on the home network");
        } else {
            char url[300];
            snprintf(url, sizeof url, "%ssync/%s/bundle", s->base, s->key);
            s->state = C_LOADING;
            http_request_async(url, NULL, on_bundle, s);
        }
    }
    sc->name = "sync"; sc->st = s;
    sc->update = update; sc->draw = draw; sc->key = key; sc->text = text; sc->tap = tap; sc->resume = resume; sc->destroy = destroy; sc->phone = 1;
    return sc;
}
