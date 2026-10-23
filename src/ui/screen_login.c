/* Sign in: QR / short code through link.stremio.com (no password typed with a
 * remote), with e-mail + password on an on-screen keyboard as the fallback. */
#include "../app.h"
#include "../http.h"
#include "../log.h"
#include "../store.h"
#include "qrcodegen.h"
#include "font.h"
#include "keyboard.h"
#include "theme.h"
#include "widgets.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { MODE_QR, MODE_PASSWORD };
enum { F_EMAIL, F_PASSWORD, F_KEYBOARD, F_SUBMIT, F_SKIP, F_SWITCH };

typedef struct Login Login;
typedef struct { Login *owner; unsigned gen; } Req;

struct Login {
    int mode;
    Keyboard kb[2];
    int editing, focus;
    int busy;
    char message[400];
    int ok;
    /* link flow */
    unsigned gen;
    char code[32], link[256];
    uint8_t qr[qrcodegen_BUFFER_LEN_MAX];
    int qr_ready;
    float next_poll, started;
    int polls;
    int logged_pending;       /* the "not approved yet" answer was logged once */
    char *pending_key;
};

static Login *g_active;

static void finish_login(Login *l, char *auth_key, const char *email)
{
    app_sync_begin();
    free(g_app.auth_key); free(g_app.user_email);
    g_app.auth_key = auth_key;
    g_app.user_email = email ? strdup(email) : NULL;
    store_set_str("authKey", auth_key);
    store_set_str("email", g_app.user_email);
    store_save();
    app_load_addons();
    app_load_library();
    app_toast("Signed in%s%s", email ? " as " : "", email ? email : "");
    l->ok = 1;
    app_pop();
}

static void on_user(HttpResponse *r, void *user)
{
    Req *q = user;
    Login *l = q->owner;
    free(q);
    if (l != g_active || !l->pending_key) return;
    char *email = NULL; ApiError err;
    if (r->status == 200) api_parse_user(r->body, &email, &err);
    char *key = l->pending_key;
    l->pending_key = NULL;
    finish_login(l, key, email);
    free(email);
}

static void start_link(Login *l);
#define LINK_CODE_LIFETIME_S 300.0f   /* ask for a fresh code after this long */

static void on_link_read(HttpResponse *r, void *user)
{
    Req *q = user;
    Login *l = q->owner;
    unsigned gen = q->gen;
    free(q);
    if (l != g_active || gen != l->gen) return;
    l->busy = 0;
    char *key = NULL; ApiError err;
    int rc = r->status == 200 ? api_parse_link_read(r->body, &key, &err) : 1;
    if (rc == 0) {
        l->pending_key = key;
        snprintf(l->message, sizeof l->message, "Linked! Fetching your account...");
        Req *q2 = malloc(sizeof *q2); q2->owner = l; q2->gen = l->gen;
        char *body = api_body_get_user(key);
        http_request_async(api_url_get_user(), body, on_user, q2);
        free(body);
    } else if (rc < 0) {
        /* The service answers with an error until the phone approves, so keep
         * polling; only a code older than the service's lifetime is renewed. */
        if (!l->logged_pending) { LOGI("link: not approved yet (%ld %s)", r->status, err.message); l->logged_pending = 1; }
        if (g_app.time - l->started > LINK_CODE_LIFETIME_S) { LOGI("link: code is %.0fs old, asking for a new one", g_app.time - l->started); start_link(l); }
    } else if (r->status != 200 && r->status != 0) {
        LOGW("link: read returned HTTP %ld", r->status);
    }
    app_redraw();
}

static void on_link_create(HttpResponse *r, void *user)
{
    Req *q = user;
    Login *l = q->owner;
    unsigned gen = q->gen;
    free(q);
    if (l != g_active || gen != l->gen) return;
    l->busy = 0;
    ApiError err;
    char qrtext[256];
    if (r->status != 200 || api_parse_link_create(r->body, l->code, sizeof l->code, l->link, sizeof l->link, qrtext, sizeof qrtext, &err) != 0) {
        snprintf(l->message, sizeof l->message, "Could not start QR sign-in (%s). Use e-mail and password instead.", r->error[0] ? r->error : err.message);
        app_redraw();
        return;
    }
    LOGI("link: code %s, link %s", l->code, l->link[0] ? l->link : "(none)");
    const char *content = l->link[0] ? l->link : l->code;
    uint8_t tmp[qrcodegen_BUFFER_LEN_MAX];
    l->qr_ready = qrcodegen_encodeText(content, tmp, l->qr, qrcodegen_Ecc_MEDIUM, qrcodegen_VERSION_MIN, 10, qrcodegen_Mask_AUTO, true);
    l->started = g_app.time;
    l->next_poll = g_app.time + 3.0f;
    l->polls = 0;
    l->logged_pending = 0;
    l->message[0] = 0;
    app_redraw();
}

static void start_link(Login *l)
{
    l->gen++;
    l->busy = 1;
    l->qr_ready = 0;
    l->code[0] = l->link[0] = 0;
    snprintf(l->message, sizeof l->message, "Requesting a sign-in code...");
    Req *q = malloc(sizeof *q); q->owner = l; q->gen = l->gen;
    http_request_async(api_url_link_create(), NULL, on_link_create, q);
}

static void poll_link(Login *l)
{
    if (l->busy || !l->code[0]) return;
    if (l->polls++ > 150) { LOGI("link: polled long enough, asking for a new code"); start_link(l); return; }
    l->busy = 1;
    char url[400];
    api_url_link_read(l->code, url, sizeof url);
    Req *q = malloc(sizeof *q); q->owner = l; q->gen = l->gen;
    http_request_async(url, NULL, on_link_read, q);
}

/* ---- password flow ---- */

static void on_done(int ok, const char *msg)
{
    if (!g_active) return;
    Login *l = g_active;
    l->busy = 0;
    l->ok = ok;
    snprintf(l->message, sizeof l->message, "%s", ok ? "Signed in" : msg);
    app_redraw();
    if (ok) { app_toast("Signed in as %s", msg); app_pop(); }
}

static void submit(Login *l)
{
    if (l->busy) return;
    if (!strchr(l->kb[F_EMAIL].text, '@') || !l->kb[F_PASSWORD].text[0]) {
        snprintf(l->message, sizeof l->message, "Enter your Stremio e-mail and password");
        return;
    }
    l->busy = 1;
    l->message[0] = 0;
    app_login(l->kb[F_EMAIL].text, l->kb[F_PASSWORD].text, on_done);
}

static void update(Screen *s, float dt)
{
    (void)dt;
    Login *l = s->st;
    if (l->mode == MODE_QR && l->code[0] && g_app.time >= l->next_poll) { l->next_poll = g_app.time + 2.5f; poll_link(l); }
    if (l->busy) app_redraw();
}

static void draw_field(const Login *l, int f, const char *label, float x, float y, float w)
{
    int foc = l->focus == f || (l->focus == F_KEYBOARD && l->editing == f);
    font_draw(label, x + 4, y, FONT_XS | FONT_B, THEME_TEXT_MUTED);
    gfx_rrect(x, y + 32, w, 72, R_PANEL, COLOR_ALPHA(THEME_TEXT, foc ? 0x22 : 0x12));
    if (l->focus == f) gfx_rrect_ring(x, y + 32, w, 72, R_PANEL, 3, THEME_TEXT);
    else if (foc) gfx_rrect_ring(x, y + 32, w, 72, R_PANEL, 2, COLOR_ALPHA(THEME_ACCENT2, 0xC0));
    const char *t = l->kb[f].text;
    char masked[256];
    if (f == F_PASSWORD) {
        size_t n = strlen(t), o = 0;
        for (size_t i = 0; i < n && o + 3 < sizeof masked; i++) { memcpy(masked + o, "\xE2\x80\xA2", 3); o += 3; }
        masked[o] = 0;
        t = masked;
    }
    if (!*t) font_draw(f == F_EMAIL ? "you@example.com" : "Password", x + 32, y + 32 + (72 - font_height(FONT_M)) / 2, FONT_M, THEME_TEXT_MUTED);
    else font_draw_fit(t, x + 32, y + 32 + (72 - font_height(FONT_M | FONT_B)) / 2, FONT_M | FONT_B, THEME_TEXT, w - 64);
}

static void draw_qr(const Login *l, float x, float y, float size)
{
    int n = qrcodegen_getSize(l->qr);
    float cell = size / (n + 6);
    float off = (size - cell * n) / 2;
    if (!g_app.fx_light) gfx_shadow(x - 10, y - 10, size + 20, size + 20, R_PANEL, 70, COLOR_ALPHA(THEME_ACCENT, 0x70));
    gfx_rrect(x, y, size, size, R_PANEL, RGBA(255, 255, 255, 255));
    for (int ry = 0; ry < n; ry++)
        for (int rx = 0; rx < n; rx++)
            if (qrcodegen_getModule(l->qr, rx, ry))
                gfx_rect(x + off + rx * cell, y + off + ry * cell, cell + 0.6f, cell + 0.6f, COLOR_ALPHA(THEME_BG, 255));
    /* brand mark in the centre (QR error correction covers it) */
    float m = size * 0.16f;
    gfx_rrect(x + (size - m) / 2 - 6, y + (size - m) / 2 - 6, m + 12, m + 12, m * 0.3f, RGBA(255, 255, 255, 255));
    gfx_rrect_grad_v(x + (size - m) / 2, y + (size - m) / 2, m, m, m * 0.28f, THEME_ACCENT2, THEME_ACCENT);
    gfx_play_triangle(x + (size - m) / 2 + m * 0.36f, y + (size - m) / 2 + m * 0.28f, m * 0.38f, m * 0.44f, THEME_TEXT);
}

static void draw(Screen *s)
{
    Login *l = s->st;
    ui_background(0);
    ui_brand(THEME_MARGIN, 44, 58);
    float x = THEME_MARGIN;
    if (l->mode == MODE_QR) {
        float qs = 460, qx = x + 40, qy = 210;
        if (l->qr_ready) draw_qr(l, qx, qy, qs);
        else { gfx_rrect(qx, qy, qs, qs, R_PANEL, COLOR_ALPHA(THEME_TEXT, 0x12)); if (l->busy) ui_spinner(qx + qs / 2, qy + qs / 2, 30);
               else ui_icon(ICON_LINK, qx + qs / 2, qy + qs / 2, 90, THEME_TEXT_MUTED); }
        float tx = qx + qs + 120, tw = SNTV_UI_W - tx - THEME_MARGIN;
        font_draw("Sign in with your phone", tx, 200, FONT_XL | FONT_B, THEME_TEXT);
        font_draw_wrapped("Your addons, Library and Continue Watching follow you here. No typing with the remote.", tx, 280, FONT_S, THEME_TEXT_DIM, tw, 2);
        /* step 2 shows the address the service itself handed us, never a guess */
        char open_step[400];
        snprintf(open_step, sizeof open_step, "Or open %s on any device", l->link[0] ? l->link : "the link page");
        const char *steps[3] = { "Scan the code with your phone's camera", open_step, "Approve, and this TV signs in by itself" };
        for (int i = 0; i < 3; i++) {
            float sy = 380 + i * 64;
            gfx_circle(tx + 22, sy + 22, 22, COLOR_ALPHA(THEME_TEXT, 0x16));
            char num[4]; snprintf(num, sizeof num, "%d", i + 1);
            font_draw_center(num, tx + 22, sy + (44 - font_height(FONT_S | FONT_B)) / 2, FONT_S | FONT_B, THEME_ACCENT2);
            font_draw_fit(steps[i], tx + 64, sy + (44 - font_height(FONT_S)) / 2, FONT_S, THEME_TEXT, SNTV_UI_W - THEME_MARGIN - tx - 64);
        }
        if (l->code[0]) {
            float cx = tx, cy = 590;
            for (const char *c = l->code; *c && cx < SNTV_UI_W - THEME_MARGIN - 80; c++) {
                gfx_rrect(cx, cy, 76, 96, R_PANEL, COLOR_ALPHA(THEME_TEXT, 0x18));
                gfx_rrect_ring(cx, cy, 76, 96, R_PANEL, 1.5f, COLOR_ALPHA(THEME_TEXT, 0x30));
                char ch[2] = { *c, 0 };
                font_draw_center(ch, cx + 38, cy + (96 - font_height(FONT_XL | FONT_B)) / 2, FONT_XL | FONT_B, THEME_TEXT);
                cx += 88;
            }
            ui_spinner(tx + 20, cy + 140, 12);
            font_draw("Waiting for approval", tx + 46, cy + 126, FONT_S, THEME_TEXT_MUTED);
        }
        float by = 820;
        float w1 = ui_pill("Use e-mail and password", 1, ICON_USER, tx, by, 72, PILL_SECONDARY, l->focus == F_SWITCH);
        ui_pill("Not now", 0, ICON_BACK, tx + w1 + 18, by, 72, PILL_SECONDARY, l->focus == F_SKIP);
        if (l->message[0]) font_draw_wrapped(l->message, tx, by + 100, FONT_S, l->ok ? THEME_OK : THEME_TEXT_DIM, tw, 2);
        return;
    }
    float w = keyboard_width();
    font_draw("Sign in", x, 140, FONT_XL | FONT_B, THEME_TEXT);
    draw_field(l, F_EMAIL, "E-MAIL", x, 230, w);
    draw_field(l, F_PASSWORD, "PASSWORD", x, 360, w);
    keyboard_draw(&l->kb[l->editing], x, 510, l->focus == F_KEYBOARD);
    float rx = x + w + 120;
    font_draw_wrapped("Only a session key is kept on this TV. Your password is sent once to Stremio and never stored.", rx, 262, FONT_S, THEME_TEXT_DIM, SNTV_UI_W - rx - THEME_MARGIN, 3);
    ui_pill(l->busy ? "Signing in\xE2\x80\xA6" : "Sign in", 1, ICON_CHEVRON_RIGHT, rx, 400, 72, PILL_PRIMARY, l->focus == F_SUBMIT);
    ui_pill("Scan a QR code instead", 1, ICON_LINK, rx, 490, 72, PILL_SECONDARY, l->focus == F_SWITCH);
    ui_pill("Not now", 0, ICON_BACK, rx, 580, 72, PILL_SECONDARY, l->focus == F_SKIP);
    if (l->busy) ui_spinner(rx + 300, 436, 18);
    if (l->message[0]) font_draw_wrapped(l->message, rx, 690, FONT_S, l->ok ? THEME_OK : THEME_DANGER, SNTV_UI_W - rx - THEME_MARGIN, 3);
}

static void switch_mode(Login *l)
{
    l->mode = l->mode == MODE_QR ? MODE_PASSWORD : MODE_QR;
    l->message[0] = 0;
    if (l->mode == MODE_QR) { l->focus = F_SWITCH; if (!l->code[0]) start_link(l); input_text(0); }
    else { l->focus = F_EMAIL; input_text(1); }
}

static int key(Screen *s, KeyAction a)
{
    Login *l = s->st;
    app_redraw();
    if (l->mode == MODE_QR) {
        switch (a) {
        case KEY_LEFT: case KEY_UP: l->focus = F_SWITCH; return 1;
        case KEY_RIGHT: case KEY_DOWN: l->focus = F_SKIP; return 1;
        case KEY_OK:
            if (l->focus == F_SKIP) { app_pop(); return 1; }
            if (l->focus == F_SWITCH) { switch_mode(l); return 1; }
            if (!l->code[0] && !l->busy) start_link(l);
            return 1;
        default: return 0;
        }
    }
    if (l->focus == F_KEYBOARD) {
        if (a == KEY_BACK) { l->focus = l->editing; return 1; }
        if (a == KEY_RIGHT && l->kb[l->editing].col == 10) { l->focus = F_SUBMIT; return 1; }
        KeyboardResult r = keyboard_key(&l->kb[l->editing], a);
        if (r == KB_LEAVE_UP) l->focus = F_PASSWORD;
        else if (r == KB_DONE) {
            if (l->editing == F_EMAIL) { l->editing = F_PASSWORD; l->focus = F_KEYBOARD; }
            else submit(l);
        }
        return 1;
    }
    switch (a) {
    case KEY_UP:
        if (l->focus == F_PASSWORD) l->focus = F_EMAIL;
        else if (l->focus == F_SKIP) l->focus = F_SWITCH;
        else if (l->focus == F_SWITCH) l->focus = F_SUBMIT;
        else if (l->focus == F_SUBMIT) l->focus = F_EMAIL;
        return 1;
    case KEY_DOWN:
        if (l->focus == F_EMAIL) l->focus = F_PASSWORD;
        else if (l->focus == F_PASSWORD) l->focus = F_KEYBOARD;
        else if (l->focus == F_SUBMIT) l->focus = F_SWITCH;
        else if (l->focus == F_SWITCH) l->focus = F_SKIP;
        return 1;
    case KEY_RIGHT: if (l->focus == F_EMAIL || l->focus == F_PASSWORD) l->focus = F_SUBMIT; return 1;
    case KEY_LEFT: if (l->focus >= F_SUBMIT) l->focus = F_KEYBOARD; return 1;
    case KEY_OK:
        if (l->focus == F_EMAIL || l->focus == F_PASSWORD) { l->editing = l->focus; l->focus = F_KEYBOARD; }
        else if (l->focus == F_SUBMIT) submit(l);
        else if (l->focus == F_SWITCH) switch_mode(l);
        else if (l->focus == F_SKIP) app_pop();
        return 1;
    case KEY_BACKSPACE: keyboard_key(&l->kb[l->editing], KEY_BACKSPACE); return 1;
    default: return 0;
    }
}

static void text(Screen *s, const char *utf8)
{
    Login *l = s->st;
    if (l->mode != MODE_PASSWORD) return;
    if (l->focus == F_EMAIL || l->focus == F_PASSWORD) l->editing = l->focus;   /* into the field on focus, not the one edited last */
    keyboard_text_input(&l->kb[l->editing], utf8);
    app_redraw();
}

static void destroy(Screen *s)
{
    Login *l = s->st;
    if (g_active == l) g_active = NULL;
    l->gen++;
    free(l->pending_key);
    input_text(0);
    free(l);
    free(s);
}

Screen *screen_login_create(void)
{
    Screen *s = calloc(1, sizeof *s);
    Login *l = calloc(1, sizeof *l);
    keyboard_init(&l->kb[F_EMAIL], 0, NULL);
    keyboard_init(&l->kb[F_PASSWORD], 1, NULL);
    l->mode = MODE_QR;
    l->focus = F_SWITCH;
    l->gen = 1;
    g_active = l;
    s->name = "login"; s->st = l;
    s->update = update; s->draw = draw; s->key = key; s->text = text; s->destroy = destroy;
    start_link(l);
    return s;
}
