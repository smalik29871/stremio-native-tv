/* Browse addons (Settings > Addons): Stremio's own addon catalogue, the official addons and the
 * community ones it lists (api.strem.io, as the Stremio app shows them), by kind: catalogs,
 * subtitles, tools; and, each as a list of its own, the addon catalogues of addons the viewer
 * installed (Stremio's addon_catalog resource), as the Stremio app shows those too. OK installs
 * one; one that must be set up first opens its settings on a phone (screen_addon_setup).
 * Never shown: adult addons (flagged so, or named, described or with catalogs so), P2P ones,
 * and links to a streaming server on the device (Local Files). The lists are kept a day. */
#include "../app.h"
#include "../http.h"
#include "../image.h"
#include "../log.h"
#include "../../vendor/cJSON.h"
#include "font.h"
#include "gfx.h"
#include "theme.h"
#include "widgets.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define OFFICIAL_URL "https://api.strem.io/addonsofficialcollection.json"
#define COMMUNITY_URL "https://api.strem.io/addonscollection.json"
#define MAX_ENTRIES 1200

enum { K_OFFICIAL, K_CATALOGS, K_SUBTITLES, K_TOOLS, K_COUNT, K_MAX = K_COUNT + 4 };   /* then up to four installed addon catalogues */
static const char *const KIND_NAMES[K_COUNT] = { "Official", "Catalogs", "Subtitles", "Tools and more" };

typedef struct { char *name, *desc, *url, *logo; int kind, setup; } Entry;

typedef struct {
    Entry e[MAX_ENTRIES]; int n;
    char kname[K_MAX][64]; int nk;
    char src[K_MAX - K_COUNT + 2][1024]; int nsrc;   /* the lists: official, community, then the installed catalogues */
    int pending, failed;
    int kind, zone, focus;      /* zone 0 the kinds, 1 the list */
    float scroll;
    int list[MAX_ENTRIES], nl;  /* this kind's entries */
} Store;

static Store *g_live;

/* An addon's own words, as the fonts can draw them: no HTML tags (some descriptions carry links),
 * no emoji (four-byte characters), no line breaks, no spaces at the ends */
static char *clean(const char *in)
{
    char *out = malloc(strlen(in) + 1), *o = out;
    int tag = 0;
    for (const unsigned char *p = (const unsigned char *)in; *p; p++) {
        if (*p == '<') { tag = 1; continue; }
        if (tag) { if (*p == '>') tag = 0; continue; }
        if (*p >= 0xF0) { while (p[1] >= 0x80 && p[1] < 0xC0) p++; continue; }
        if (!strncmp((const char *)p, "&amp;", 5)) { *o++ = '&'; p += 4; continue; }
        *o++ = *p == '\n' || *p == '\r' || *p == '\t' ? ' ' : (char)*p;
        if (o > out + 1 && o[-1] == ' ' && o[-2] == ' ') o--;
    }
    *o = 0;
    char *a = out;
    while (*a == ' ') a++;
    memmove(out, a, strlen(a) + 1);
    for (size_t n = strlen(out); n && out[n - 1] == ' '; n--) out[n - 1] = 0;
    return out;
}

/* Adult: the addon says so, or its name, description, id or a catalog's name or id is one of
 * these words ("adult animation" alone, cartoons for grown-ups, is not) */
static int adult(const cJSON *m)
{
    static const char *const W[] = { "adult", "porn", "porno", "pornhub", "xxx", "xxxclub", "nsfw", "18+", "sex", "sexy", "erotic", "erotica", "hentai",
        "h_anime", "cam4", "camgirl", "camgirls", "onlyfans", "onlyporn", "nude", "nudity", "milf", "jav", "pimpbunny", "brazzers", "xvideos",
        "xhamster", "eporner", "stripchat", "chaturbate", NULL };
    if (cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(m, "behaviorHints"), "adult"))) return 1;
    size_t cap = 4096, len = 0;
    char *t = malloc(cap);
    t[0] = 0;
#define PUT(s) do { const char *s_ = (s); if (s_) { size_t l_ = strlen(s_); if (len + l_ + 4 < cap) { memcpy(t + len, s_, l_); len += l_; t[len++] = ' '; t[len++] = '|'; t[len++] = ' '; t[len] = 0; } } } while (0)
    PUT(cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(m, "name")));
    PUT(cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(m, "description")));
    PUT(cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(m, "id")));
    const cJSON *c;
    cJSON_ArrayForEach(c, cJSON_GetObjectItemCaseSensitive(m, "catalogs")) {
        PUT(cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(c, "name")));
        PUT(cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(c, "id")));
    }
#undef PUT
    for (char *p = t; *p; p++) if (*p >= 'A' && *p <= 'Z') *p += 32;
    for (char *p = t; (p = strstr(p, "adult animation")); ) memcpy(p, "grown animation", 15);
    int hit = 0;
    for (char *p = t; *p && !hit; ) {   /* each word: letters, digits, _ and + */
        while (*p && !((*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') || *p == '_' || *p == '+')) p++;
        char *w = p;
        while ((*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') || *p == '_' || *p == '+') p++;
        size_t wl = (size_t)(p - w);
        for (int i = 0; W[i] && wl; i++) if (strlen(W[i]) == wl && !strncmp(w, W[i], wl)) hit = 1;
    }
    free(t);
    return hit;
}

static int has(const char *const *a, int n, const char *s) { for (int i = 0; i < n; i++) if (!strcmp(a[i], s)) return 1; return 0; }

static void filter(Store *st)
{
    st->nl = 0;
    for (int i = 0; i < st->n; i++) if (st->e[i].kind == st->kind) st->list[st->nl++] = i;
    if (st->focus >= st->nl) st->focus = st->nl ? st->nl - 1 : 0;
}

static void add_collection(Store *st, const char *body, int src)
{
    cJSON *root = cJSON_Parse(body), *it;
    const cJSON *list = cJSON_IsArray(root) ? root : cJSON_GetObjectItemCaseSensitive(root, "addons");   /* a collection, or an addon_catalog answer */
    int added = 0, left = 0, official = src == 0;
    cJSON_ArrayForEach(it, list) {
        const cJSON *m = cJSON_GetObjectItemCaseSensitive(it, "manifest");
        const char *url = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(it, "transportUrl"));
        const char *name = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(m, "name"));
        if (!url || !name || st->n >= MAX_ENTRIES) continue;
        const cJSON *bh = cJSON_GetObjectItemCaseSensitive(m, "behaviorHints");
        if (strncmp(url, "http", 4) || strstr(url, ":11470/") || !strstr(url, "manifest.json")   /* :11470: a streaming server on the device */
            || adult(m) || cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(bh, "p2p"))) { left++; continue; }
        const char *res[16]; int nr = 0;
        const cJSON *r;
        cJSON_ArrayForEach(r, cJSON_GetObjectItemCaseSensitive(m, "resources")) {
            const char *v = cJSON_IsString(r) ? r->valuestring : cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(r, "name"));
            if (v && nr < 16) res[nr++] = v;
        }
        int kind = src >= 2 ? K_COUNT + src - 2 : official ? K_OFFICIAL : has(res, nr, "catalog") ? K_CATALOGS : has(res, nr, "subtitles") ? K_SUBTITLES : K_TOOLS;
        int dup = -1;
        for (int i = 0; i < st->n && dup < 0; i++) if (!strcmp(st->e[i].url, url)) dup = i;
        if (dup >= 0) {   /* in more than one list: under the most official one (the lists come in any order) */
            int rank_new = src >= 2 ? 2 : src, rank_old = st->e[dup].kind >= K_COUNT ? 2 : st->e[dup].kind == K_OFFICIAL ? 0 : 1;
            if (rank_new < rank_old) st->e[dup].kind = kind;
            continue;
        }
        char *nm = clean(name);
        int readable = 0;   /* a name in a script the fonts do not have would read as nothing */
        for (const char *c = nm; *c && !readable; c++) readable = (*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z') || (*c >= '0' && *c <= '9');
        if (!readable) { free(nm); left++; continue; }
        Entry *e = &st->e[st->n++];
        e->name = nm;
        const char *d = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(m, "description"));
        e->desc = clean(d ? d : "");
        e->url = strdup(url);
        const char *logo = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(m, "logo"));
        e->logo = logo && !strncmp(logo, "http", 4) ? strdup(logo) : NULL;
        e->setup = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(bh, "configurationRequired"));
        e->kind = kind;
        added++;
    }
    cJSON_Delete(root);
    if (src >= 2) LOGI("addon store: %d addons in %s", added, st->kname[K_COUNT + src - 2]);
    else LOGI("addon store: %d %s addons", added, official ? "official" : "community");
    if (left) LOGI("addon store: %d left out (adult, P2P, or a link that needs a streaming server)", left);
}

/* Stremio's two lists, then each addon_catalog an installed addon offers */
static void sources(Store *st)
{
    const char *e = getenv("SNTV_ADDON_COLLECTIONS");   /* tests: "official,community" */
    const char *comma = e ? strchr(e, ',') : NULL;
    if (e) { snprintf(st->src[0], sizeof st->src[0], "%.*s", comma ? (int)(comma - e) : (int)strlen(e), e); snprintf(st->src[1], sizeof st->src[1], "%s", comma ? comma + 1 : ""); }
    else { snprintf(st->src[0], sizeof st->src[0], "%s", OFFICIAL_URL); snprintf(st->src[1], sizeof st->src[1], "%s", COMMUNITY_URL); }
    st->nsrc = 2;
    for (int k = 0; k < K_COUNT; k++) snprintf(st->kname[k], sizeof st->kname[k], "%s", KIND_NAMES[k]);
    st->nk = K_COUNT;
    for (int i = 0; i < g_app.n_addons && st->nk < K_MAX; i++) {
        const Addon *a = &g_app.addons[i];
        if (!a->manifest_json || !a->base_url || !strstr(a->manifest_json, "addon_catalog")) continue;
        cJSON *m = cJSON_Parse(a->manifest_json), *c;
        cJSON_ArrayForEach(c, cJSON_GetObjectItemCaseSensitive(m, "addonCatalogs")) {
            const char *type = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(c, "type")), *id = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(c, "id"));
            const char *name = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(c, "name"));
            if (!type || !id || st->nk >= K_MAX || adult(m)) continue;
            char *nm = clean(name && *name ? name : a->manifest.name ? a->manifest.name : "Addons");
            snprintf(st->kname[st->nk++], sizeof st->kname[0], "%s", nm);
            free(nm);
            snprintf(st->src[st->nsrc++], sizeof st->src[0], "%s/addon_catalog/%s/%s.json", a->base_url, type, id);
        }
        cJSON_Delete(m);
    }
    if (st->nk > K_COUNT) LOGI("addon store: %d addon catalogue%s from installed addons", st->nk - K_COUNT, st->nk - K_COUNT == 1 ? "" : "s");
}

static void on_list(HttpResponse *r, void *user)
{
    int src = (int)(intptr_t)user;
    Store *st = g_live;
    if (!st || src >= st->nsrc) return;
    if (r->status == 200 && r->body) app_cache_put(st->src[src], r->body, strlen(r->body));
    st->pending--;
    if (r->status == 200 && r->body) add_collection(st, r->body, src);
    else { st->failed++; LOGW("addon store: list %d did not come (%ld)", src + 1, r->status); }
    filter(st);
    app_redraw();
}

static void load(Store *st)
{
    sources(st);
    for (int i = 0; i < st->nsrc; i++) {
        char *cached = app_cache_get(st->src[i], 86400);
        if (cached) { add_collection(st, cached, i); free(cached); continue; }
        st->pending++;
        http_request_async(st->src[i], NULL, on_list, (void *)(intptr_t)i);
    }
    filter(st);
}

static void install(Store *st)
{
    if (st->focus >= st->nl) return;
    Entry *e = &st->e[st->list[st->focus]];
    if (app_addon_index(e->url) >= 0) { app_toast("%s is installed", e->name); return; }
    if (e->setup) {   /* its own page sets it up first: on a phone */
        Addon a; memset(&a, 0, sizeof a);
        char base[2048]; snprintf(base, sizeof base, "%s", e->url);
        char *m = strstr(base, "/manifest.json");
        if (m) *m = 0;
        a.manifest.name = e->name; a.base_url = base;
        LOGI("addon store: %s is set up first", e->name);
        app_push(screen_addon_setup_create(&a));
        return;
    }
    LOGI("addon store: installing %s", e->name);
    app_install_addon(e->url);
}

/* ---- drawing ---- */

static void draw_row(const Entry *e, float x, float y, float w, float h, int foc)
{
    gfx_rrect(x, y, w, h, 22, foc ? COLOR_ALPHA(THEME_ACCENT, 0x30) : COLOR_ALPHA(THEME_TEXT, 0x0D));
    if (foc) gfx_rrect_ring(x, y, w, h, 22, 3, THEME_ACCENT);
    float ls = h - 40, lx = x + 20, ly = y + 20;
    gfx_rrect(lx, ly, ls, ls, 14, COLOR_ALPHA(THEME_TEXT, 0x14));
    int tw, th;
    unsigned t = e->logo ? image_get(e->logo, &tw, &th) : 0;
    if (t && tw > 0 && th > 0) {
        float k = tw > th ? ls / tw : ls / th, dw = tw * k, dh = th * k;
        gfx_tex(t, lx + (ls - dw) / 2, ly + (ls - dh) / 2, dw, dh, RGBA(0xFF, 0xFF, 0xFF, 0xFF));
    } else {
        const char *f = e->name;   /* its first letter or digit, as the logo */
        while (*f && !((*f >= 'a' && *f <= 'z') || (*f >= 'A' && *f <= 'Z') || (*f >= '0' && *f <= '9'))) f++;
        char c[2] = { (char)(*f >= 'a' && *f <= 'z' ? *f - 32 : *f ? *f : '?'), 0 };
        font_draw(c, lx + (ls - font_measure(c, FONT_M | FONT_B)) / 2, ly + (ls - font_height(FONT_M | FONT_B)) / 2, FONT_M | FONT_B, THEME_TEXT_DIM);
    }
    int installed = app_addon_index(e->url) >= 0;
    const char *state = installed ? "Installed" : e->setup ? (SNTV_PHONE ? "Set up first" : "OK: set up on your phone") : SNTV_PHONE ? "Install" : "OK to install";
    float sw = font_measure(state, FONT_XS | FONT_B), tx = lx + ls + 24, txw = x + w - tx - sw - 40;
    font_draw_fit(e->name, tx, y + 22, FONT_S | FONT_B, THEME_TEXT, txw);
    font_draw_wrapped(e->desc, tx, y + 22 + font_height(FONT_S) + 6, FONT_XS, THEME_TEXT_DIM, txw, 2);
    font_draw(state, x + w - sw - 24, y + h / 2 - font_height(FONT_XS) / 2, FONT_XS | FONT_B, installed ? THEME_TEXT_MUTED : THEME_ACCENT);
}

static void draw_list(Store *st, float x, float y, float w, float h)
{
    if (!st->nl) {
        const char *msg = st->pending ? "Getting Stremio's addon catalogue\xE2\x80\xA6" : st->failed ? "The addon catalogue did not come. Check the connection and open this again." : "Nothing here.";
        font_draw_wrapped(msg, x, y + 10, FONT_S, THEME_TEXT_DIM, w, 3);
        return;
    }
    float rh = SNTV_PHONE ? 150 : 132, gap = 16;
    float want = st->focus * (rh + gap);   /* keep the focus in view */
    if (want - st->scroll > h - rh) st->scroll = want - (h - rh);
    if (want < st->scroll) st->scroll = want;
    gfx_clip_push(x - 8, y - 8, w + 16, h + 16);
    for (int i = 0; i < st->nl; i++) {
        float ry = y + i * (rh + gap) - st->scroll;
        if (ry + rh < y - 8 || ry > y + h + 8) continue;
        ui_hit(x, ry, w, rh, 100 + i);
        draw_row(&st->e[st->list[i]], x, ry, w, rh, st->zone == 1 && st->focus == i);
    }
    gfx_clip_pop();
}

static int count(const Store *st, int kind) { int n = 0; for (int i = 0; i < st->n; i++) n += st->e[i].kind == kind; return n; }

static void draw(Screen *s)
{
    Store *st = s->st;
    gfx_rect(0, 0, SNTV_UI_W, SNTV_UI_H, THEME_BG);
    char label[48];
    if (SNTV_PHONE) {   /* upright: the kinds as chips over the list */
        float x = 40, w = SNTV_UI_W - 80;
        font_draw("Browse addons", x, 90, FONT_L | FONT_B, THEME_TEXT);
        float cx = x, cy = 170;   /* the chips wrap onto more lines */
        for (int k = 0; k < st->nk; k++) {
            snprintf(label, sizeof label, "%.30s", st->kname[k]);
            float cw = font_measure(label, FONT_XS | FONT_B) + 44;
            if (cx + cw > x + w && cx > x) { cx = x; cy += 78; }
            ui_hit(cx, cy, cw, 64, k);
            gfx_rrect(cx, cy, cw, 64, 32, st->kind == k ? THEME_ACCENT : COLOR_ALPHA(THEME_TEXT, 0x14));
            font_draw(label, cx + 22, cy + 32 - font_height(FONT_XS | FONT_B) / 2, FONT_XS | FONT_B, st->kind == k ? THEME_ON_ACCENT : THEME_TEXT);
            cx += cw + 14;
        }
        draw_list(st, x, cy + 100, w, SNTV_UI_H - cy - 160);
        return;
    }
    float lx = 120, lw = 440, rx = lx + lw + 80, rw = SNTV_UI_W - rx - 120;
    ui_brand(lx, 64, 48);
    font_draw("Browse addons", lx, 150, FONT_L | FONT_B, THEME_TEXT);
    font_draw_wrapped("Stremio's addon catalogue: its own addons and the community's.", lx, 216, FONT_S, THEME_TEXT_DIM, lw, 2);
    float step = st->nk > 5 ? (SNTV_UI_H - 330 - 150) / (float)st->nk : 86;
    for (int k = 0; k < st->nk; k++) {
        float y = 330 + k * step;
        int foc = st->kind == k;
        ui_hit(lx, y, lw, 70, k);
        if (foc) gfx_rrect(lx, y, lw, 70, 35, st->zone == 0 ? THEME_ACCENT : COLOR_ALPHA(THEME_TEXT, 0x1A));
        Color c = foc && st->zone == 0 ? THEME_ON_ACCENT : THEME_TEXT;
        font_draw_fit(st->kname[k], lx + 32, y + 35 - font_height(FONT_S) / 2, FONT_S | FONT_B, c, lw - 120);
        int n = count(st, k);
        if (n) { snprintf(label, sizeof label, "%d", n); font_draw(label, lx + lw - 32 - font_measure(label, FONT_XS), y + 35 - font_height(FONT_XS) / 2, FONT_XS, foc && st->zone == 0 ? THEME_ON_ACCENT : THEME_TEXT_MUTED); }
    }
    font_draw_fit(st->kname[st->kind], rx, 150, FONT_L | FONT_B, THEME_TEXT, rw);
    draw_list(st, rx, 250, rw, SNTV_UI_H - 250 - 130);
    font_draw(st->zone ? "OK: install   \xC2\xB7   Left: kinds   \xC2\xB7   Back: close" : "Up / Down: kinds   \xC2\xB7   Right: the addons   \xC2\xB7   Back: close", lx, SNTV_UI_H - 80, FONT_XS, THEME_TEXT_MUTED);
}

static void pick_kind(Store *st, int k) { st->kind = k; st->focus = 0; st->scroll = 0; filter(st); }

static int key(Screen *s, KeyAction a)
{
    Store *st = s->st;
    app_redraw();
    if (st->zone == 1) {
        switch (a) {
        case KEY_UP: if (st->focus > 0) st->focus--; else if (SNTV_PHONE) st->zone = 0; return 1;
        case KEY_DOWN: if (st->focus + 1 < st->nl) st->focus++; return 1;
        case KEY_LEFT: if (!SNTV_PHONE) st->zone = 0; return 1;
        case KEY_OK: install(st); return 1;
        case KEY_BACK: st->zone = 0; return 1;
        default: return 1;
        }
    }
    switch (a) {
    case KEY_UP: if (!SNTV_PHONE && st->kind > 0) pick_kind(st, st->kind - 1); return 1;
    case KEY_DOWN: if (SNTV_PHONE) { if (st->nl) st->zone = 1; } else if (st->kind + 1 < st->nk) pick_kind(st, st->kind + 1); return 1;
    case KEY_LEFT: if (SNTV_PHONE && st->kind > 0) pick_kind(st, st->kind - 1); return 1;
    case KEY_RIGHT: if (SNTV_PHONE) { if (st->kind + 1 < st->nk) pick_kind(st, st->kind + 1); } else if (st->nl) st->zone = 1; return 1;
    case KEY_OK: if (st->nl) st->zone = 1; return 1;
    case KEY_BACK: app_pop(); return 1;
    default: return 1;
    }
}

static int tap(Screen *s, int id)
{
    Store *st = s->st;
    app_redraw();
    if (g_touch_aim) return 1;
    if (id >= 100) { st->zone = 1; st->focus = id - 100; install(st); return 1; }
    if (id >= 0 && id < st->nk) { pick_kind(st, id); st->zone = 0; }
    return 1;
}

static void destroy(Screen *s)
{
    Store *st = s->st;
    if (g_live == st) g_live = NULL;
    for (int i = 0; i < st->n; i++) { free(st->e[i].name); free(st->e[i].desc); free(st->e[i].url); free(st->e[i].logo); }
    free(st); free(s);
}

Screen *screen_addon_store_create(void)
{
    Screen *s = calloc(1, sizeof *s);
    Store *st = calloc(1, sizeof *st);
    g_live = st;
    load(st);
    s->name = "addon_store"; s->st = st;
    s->draw = draw; s->key = key; s->tap = tap; s->destroy = destroy; s->phone = 1;
    return s;
}
