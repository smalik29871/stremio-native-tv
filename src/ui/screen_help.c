/* Help (Settings > About > Help): a short guide for someone new, its controls written for
 * this device (a remote, a phone's touch, a computer's keyboard and mouse), and where to get
 * Cuelight for the other kinds of device, each with a code a phone can scan and, where the
 * device has a browser, a link that opens. Up/Down pick a topic; on the last, Right reaches
 * the downloads. On a phone held upright the topics are a list and a topic its own page. */
#include "../app.h"
#include "../log.h"
#include "../video/video.h"
#include "../platform/platform.h"
#include "font.h"
#include "gfx.h"
#include "qrcodegen.h"
#include "theme.h"
#include "widgets.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RELEASES "https://github.com/smalik29871/cuelight-releases"
#define LG_REPO RELEASES "/releases/latest/download/repo.json"
#define LATEST RELEASES "/releases/latest"

enum { DEV_TV, DEV_ANDROID_TV, DEV_PHONE, DEV_COMPUTER };
enum { MAX_PARAS = 8, N_GET = 4 };

typedef struct { const char *title; const char *paras[MAX_PARAS]; } Topic;

static const struct { const char *name, *what, *how, *url; } GET[N_GET] = {
    { "LG TV", "webOS 3 and newer", "Homebrew Channel > Settings > Add repository, then install Cuelight", LG_REPO },
    { "Android", "Phones, tablets, Android TV, Fire TV", "Download the .apk on the device and open it", LATEST },
    { "Windows", "Windows 10 and 11 (preview)", "Download the setup .exe and open it: it installs and updates itself", LATEST },
    { "The guide", "Install steps and answers", "Install steps for each device, on the releases page", RELEASES "#readme" },
};

typedef struct {
    int dev;
    Topic topics[8]; int n;
    int focus;          /* the topic */
    int zone;           /* 0 topics, 1 the downloads (last topic) */
    int card;
    int page;           /* a phone upright: a topic open on its own page */
    uint8_t qr[N_GET][qrcodegen_BUFFER_LEN_MAX]; int qr_ok[N_GET];
} Help;

static int device(void)
{
    const char *d = app_device_name();
    if (!strcmp(d, "Computer")) return DEV_COMPUTER;
    if (!strcmp(d, "Phone")) return DEV_PHONE;
    if (!strcmp(d, "Android TV")) return DEV_ANDROID_TV;
    return DEV_TV;
}

static void add(Help *h, const char *title, const char *const *paras)
{
    Topic *t = &h->topics[h->n++];
    t->title = title;
    for (int i = 0; i < MAX_PARAS - 1 && paras[i]; i++) t->paras[i] = paras[i];
}

static void build(Help *h)
{
    static const char *const START[] = {
        "Cuelight plays films and series from addons, the open add-on format Stremio uses. Out of the box it has its own catalog (from TMDB) to browse and search.",
        "To play, a title needs an addon that gives sources. Settings > Addons lists recommended ones you can add with one press, and takes any other addon's link.",
        "Pick a title and press Play: Cuelight asks every addon, checks the sources and starts the best one for this device. Sources lists them all, best first.",
        "Signing in to Stremio, Trakt or SimKL (Settings > Accounts) is optional. It brings your addons, Library and Continue Watching from the other apps.",
        NULL };
    add(h, "Getting started", START);
    static const char *const ADDONS[] = {
        "Settings > Addons > Browse addons has Stremio's catalogue of addons by kind (catalogs, subtitles, tools): OK on one installs it. The recommended ones below it add a catalog, subtitles or free films with one press. Install addon from URL takes any addon's manifest.json link.",
        "On a TV, every text field shows a code: scan it with your phone and paste the link there; it lands on the TV.",
        "OK on an addon configures it (when it has settings) or removes it. Left and Right move it up and down the list: the order decides which sources come first.",
        "Addons are made by others. Cuelight plays what they return: install the ones you trust, and only watch what you have the right to.",
        NULL };
    add(h, "Addons", ADDONS);
    static const char *const REMOTE[] = {
        "Arrows move, OK opens, Back goes back. Hold OK on a title for its options (Library, watched, trailer, sources).",
        "In the player: OK pauses, Left and Right seek (faster while held). Down reaches the row of buttons: subtitles (with their timing), sound tracks, speed, sources and episodes.",
        "Red shows the stream's details, Green plays the next episode now, Yellow tries the next source. Blue resumes what you watched last, from anywhere.",
        "Back stops and keeps your place; with picture-in-picture on, the video waits in a corner instead.",
        NULL };
    static const char *const TOUCH[] = {
        "Tap to open, swipe to scroll rows and pages, your phone's Back to go back. The bar at the bottom has Home, Explore, Search, Library and Settings.",
        "Press and hold a title for its options (Library, watched, trailer, sources).",
        "The player turns sideways and fills the screen. Tap for the controls (subtitles, audio, speed, sources, episodes); double-tap a side to skip 10 seconds; drag across to seek.",
        "Up and down on the left of the picture sets the brightness, on the right the volume. Picture-in-picture keeps the video playing over other apps.",
        NULL };
    static const char *const KEYS[] = {
        "The pointer picks what it rests on and a click opens it; a right-click (or holding Enter) shows a title's options. The wheel scrolls what is under the pointer (with Shift, a row).",
        "The arrow at the top left, Esc or the mouse's back button go back; the corners at the top right (F11, Alt+Enter) switch full screen. Arrow keys and Enter work everywhere too. F3: search, P: play or pause.",
        "In the player: move the mouse for the controls (subtitles, audio, speed, sources, episodes), click the film to pause, double-click for full screen, drag the bar to seek. Space or Enter pauses and Left and Right seek too.",
        "F1 shows the stream's details, F2 plays the next episode now, F5 tries the next source, F4 resumes what you watched last.",
        NULL };
    add(h, h->dev == DEV_COMPUTER ? "Keyboard and mouse" : h->dev == DEV_PHONE ? "Touch" : "Your remote",
        h->dev == DEV_COMPUTER ? KEYS : h->dev == DEV_PHONE ? TOUCH : REMOTE);
    static const char *const PLAY[] = {
        "Subtitles in your language come on by themselves (Settings > Audio & subtitles); the subtitle menu offers every other one, with timing you can shift.",
        "Intros, recaps and credits show a Skip button when their times are known (IntroDB, AniSkip); Settings > Playback can skip them by itself.",
        "When a source stalls or this device cannot play it, Cuelight moves to the next one by itself and says so.",
        "Your place is kept for every film and episode, and the next episode is ready before the credits end.",
        NULL };
    add(h, "Watching", PLAY);
    static const char *const SYNC[] = {
        "Settings > Accounts > Sync with another device copies your addons, sign-ins and settings to another Cuelight: scan its code or type its 4-digit number.",
        "Stremio sync keeps your Library and Continue Watching the same as Stremio's own apps; Trakt and SimKL keep what you watched.",
        "Settings > Accounts > Back up or restore settings saves them to your phone, to bring back later or on another device.",
        NULL };
    add(h, "Your other devices", SYNC);
    const char *const PROBLEMS[] = {   /* the last line is for this device */
        "A source will not play: open Sources and pick another; a cached or smaller one usually starts at once.",
        "Nothing to play: the title needs an addon with sources for it. Check Settings > Addons.",
        "Something wrong: Settings > About > Diagnostics shows what happened, with a code to send the log from your phone.",
        h->dev == DEV_COMPUTER ? "Your settings are kept in your user folder (%APPDATA%\\stremio\\nativetv on Windows): a new version unpacked anywhere keeps them."
                               : "Updates: Settings > About > Update checks for a new version; your sign-in, settings and progress stay.",
        NULL };
    add(h, "When something goes wrong", PROBLEMS);
    static const char *const GETP[] = { NULL };
    add(h, "Get Cuelight on other devices", GETP);
}

static int can_open(const Help *h) { return h->dev == DEV_COMPUTER || h->dev == DEV_PHONE; }
static int is_get(const Help *h) { return h->focus == h->n - 1; }

static void draw_get(Help *h, float x, float y, float w)
{
    font_draw_wrapped(can_open(h) ? "Scan a code with the device you want it on, or open the link here."
                                  : "Scan a code with your phone: it opens the page for that device.",
                      x, y, FONT_S, THEME_TEXT_DIM, w, 2);
    y += font_height(FONT_S) * 2 + 24;
    int cols = SNTV_PHONE ? 1 : 2;
    float gap = 28, cw = (w - gap * (cols - 1)) / cols, ch = SNTV_PHONE ? 220 : 250, qs = ch - 48;
    for (int i = 0; i < N_GET; i++) {
        float cx = x + (i % cols) * (cw + gap), cy = y + (i / cols) * (ch + gap);
        int foc = h->zone == 1 && h->card == i;
        ui_hit(cx, cy, cw, ch, 100 + i);
        gfx_rrect(cx, cy, cw, ch, 24, foc ? COLOR_ALPHA(THEME_ACCENT, 0x30) : COLOR_ALPHA(THEME_TEXT, 0x0D));
        if (foc) gfx_rrect_ring(cx, cy, cw, ch, 24, 3, THEME_ACCENT);
        gfx_rrect(cx + 24, cy + 24, qs, qs, 12, RGBA(0xFF, 0xFF, 0xFF, 0xFF));
        if (h->qr_ok[i]) ui_qr(h->qr[i], cx + 34, cy + 34, qs - 20);
        float tx = cx + qs + 48, tw = cw - qs - 72;
        font_draw(GET[i].name, tx, cy + 34, FONT_M | FONT_B, THEME_TEXT);
        font_draw_wrapped(GET[i].what, tx, cy + 34 + font_height(FONT_M) + 8, FONT_XS, THEME_TEXT_DIM, tw, 2);
        font_draw_wrapped(GET[i].how, tx, cy + 34 + font_height(FONT_M) + 8 + font_height(FONT_XS) * 2 + 14, FONT_XS, THEME_TEXT, tw, 3);
        if (can_open(h) && foc) font_draw(SNTV_PHONE ? "Tap: open" : "Enter: open the page", tx, cy + ch - 24 - font_height(FONT_XS), FONT_XS | FONT_B, THEME_ACCENT);
    }
}

static void draw_topic(Help *h, int t, float x, float y, float w)
{
    if (t == h->n - 1) { draw_get(h, x, y, w); return; }
    for (int i = 0; h->topics[t].paras[i]; i++) {
        int lines = font_draw_wrapped(h->topics[t].paras[i], x, y, FONT_M, THEME_TEXT, w, SNTV_PHONE ? 8 : 4);
        y += (lines ? lines : 1) * font_height(FONT_M) + 26;
    }
}

static void draw(Screen *s)
{
    Help *h = s->st;
    gfx_rect(0, 0, SNTV_UI_W, SNTV_UI_H, THEME_BG);
    if (SNTV_PHONE) {   /* upright: the list, or one topic on its own page */
        float x = 40, w = SNTV_UI_W - 80;
        font_draw(h->page ? h->topics[h->focus].title : "Help", x, 90, FONT_L | FONT_B, THEME_TEXT);
        if (h->page) { draw_topic(h, h->focus, x, 200, w); return; }
        font_draw("Cuelight " SNTV_APP_VERSION " (beta)", x, 150, FONT_S, THEME_TEXT_DIM);
        for (int i = 0; i < h->n; i++) {
            float y = 220 + i * 104;
            ui_hit(x, y, w, 88, i);
            gfx_rrect(x, y, w, 88, 22, h->focus == i ? COLOR_ALPHA(THEME_ACCENT, 0x30) : COLOR_ALPHA(THEME_TEXT, 0x0D));
            font_draw(h->topics[i].title, x + 28, y + 44 - font_height(FONT_M) / 2, FONT_M | FONT_B, THEME_TEXT);
        }
        return;
    }
    float lx = 120, lw = 520, rx = lx + lw + 80, rw = SNTV_UI_W - rx - 120;
    ui_brand(lx, 64, 48);
    font_draw("Help", lx, 150, FONT_L | FONT_B, THEME_TEXT);
    font_draw("Cuelight " SNTV_APP_VERSION " (beta)", lx, 216, FONT_S, THEME_TEXT_DIM);
    for (int i = 0; i < h->n; i++) {
        float y = 290 + i * 86;
        int foc = h->focus == i;
        ui_hit(lx, y, lw, 70, i);
        if (foc) gfx_rrect(lx, y, lw, 70, 35, h->zone == 0 ? THEME_ACCENT : COLOR_ALPHA(THEME_TEXT, 0x1A));
        font_draw(h->topics[i].title, lx + 32, y + 35 - font_height(FONT_S) / 2, FONT_S | FONT_B, foc && h->zone == 0 ? THEME_ON_ACCENT : THEME_TEXT);
    }
    font_draw(h->topics[h->focus].title, rx, 150, FONT_L | FONT_B, THEME_TEXT);
    draw_topic(h, h->focus, rx, 290, rw);
    const char *hint = is_get(h) ? (h->zone ? (can_open(h) ? "OK: open the page   \xC2\xB7   Left: topics   \xC2\xB7   Back: close" : "Left: topics   \xC2\xB7   Back: close")
                                            : "Right: the downloads   \xC2\xB7   Back: close")
                                 : "Up / Down: topics   \xC2\xB7   Back: close";
    font_draw(hint, lx, SNTV_UI_H - 80, FONT_XS, THEME_TEXT_MUTED);
}

static void open_card(Help *h)
{
    if (!can_open(h)) { app_toast("Scan the code with your phone"); return; }
    if (video_open_browser(GET[h->card].url)) LOGI("help: opened the page for %s", GET[h->card].name);
    else app_toast("No browser here: scan the code instead");
}

static int key(Screen *s, KeyAction a)
{
    Help *h = s->st;
    app_redraw();
    int cols = SNTV_PHONE ? 1 : 2;
    if (h->zone == 1) {
        switch (a) {
        case KEY_LEFT: if (h->card % cols) h->card--; else h->zone = 0; return 1;
        case KEY_RIGHT: if (h->card % cols < cols - 1 && h->card + 1 < N_GET) h->card++; return 1;
        case KEY_UP: if (h->card >= cols) h->card -= cols; else if (SNTV_PHONE) h->zone = 0; return 1;
        case KEY_DOWN: if (h->card + cols < N_GET) h->card += cols; return 1;
        case KEY_OK: open_card(h); return 1;
        case KEY_BACK: if (SNTV_PHONE) { h->zone = 0; h->page = 0; } else h->zone = 0; return 1;
        default: return 1;
        }
    }
    switch (a) {
    case KEY_UP: if (!h->page && h->focus > 0) h->focus--; return 1;
    case KEY_DOWN: if (!h->page && h->focus + 1 < h->n) h->focus++; return 1;
    case KEY_RIGHT: if (is_get(h) && !SNTV_PHONE) { h->zone = 1; h->card = 0; } return 1;
    case KEY_OK:
        if (SNTV_PHONE && !h->page) { h->page = 1; if (is_get(h)) { h->zone = 1; h->card = 0; } }
        else if (is_get(h)) { h->zone = 1; h->card = 0; }
        return 1;
    case KEY_BACK: if (SNTV_PHONE && h->page) h->page = 0; else app_pop(); return 1;
    default: return 1;
    }
}

static int tap(Screen *s, int id)
{
    Help *h = s->st;
    app_redraw();
    if (id >= 100) { h->zone = 1; h->card = id - 100; if (!g_touch_aim) open_card(h); return 1; }
    h->focus = id; h->zone = 0;
    if (SNTV_PHONE && !g_touch_aim) { h->page = 1; if (is_get(h)) { h->zone = 1; h->card = 0; } }
    return 1;
}

static void destroy(Screen *s) { free(s->st); free(s); }

Screen *screen_help_create(int get)
{
    Screen *s = calloc(1, sizeof *s);
    Help *h = calloc(1, sizeof *h);
    h->dev = device();
    build(h);
    if (get) h->focus = h->n - 1;
    LOGI("help: %d topics for %s", h->n, app_device_name());
    for (int i = 0; i < N_GET; i++) {
        uint8_t tmp[qrcodegen_BUFFER_LEN_MAX];
        h->qr_ok[i] = qrcodegen_encodeText(GET[i].url, tmp, h->qr[i], qrcodegen_Ecc_LOW, qrcodegen_VERSION_MIN, 10, qrcodegen_Mask_AUTO, true);
    }
    s->name = "help"; s->st = h;
    s->draw = draw; s->key = key; s->tap = tap; s->destroy = destroy; s->phone = 1;
    return s;
}
