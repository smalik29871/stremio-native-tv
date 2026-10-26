#include "ui/island.h"
#include "app.h"
#include "perfstat.h"
#include "lists.h"
#include "memwatch.h"
#include "sysinfo.h"
#include "tidy.h"
#include "torrent.h"
#include "torbox.h"
#include "youtube.h"
#include "http.h"
#include "tracking.h"
#include "log.h"
#include "store.h"
#include "tmdb.h"
#include "opensubs.h"
#include "image.h"
#include "ui/clockface.h"
#include "ui/font.h"
#include "ui/theme.h"
#include "ui/widgets.h"
#include "ai.h"
#include "breaks.h"
#include "suntimes.h"
#include "ui/screen_options.h"
#include "video/video.h"
#include "cJSON.h"
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <sys/statvfs.h>
#endif
#include <time.h>
#include <sys/stat.h>

App g_app;

void app_redraw(void) { g_app.need_redraw = 1; }

float ui_ease(float from, float to, float *vel, float dt, float speed)
{
    if (g_app.fx_light) { if (vel) *vel = 0; return to; }   /* light mode: no interpolation */
    if (vel) {
        /* a critically damped spring, solved exactly for the step: it eases in as well as
         * out, never overshoots, and a new target mid-flight keeps the speed it had */
        float w = speed * 0.75f, x = from - to, step = dt;   /* exact for any step: slow TVs keep the timing */
        float e = SDL_expf(-w * step), c = (*vel + w * x) * step;
        float v = to + (x + c) * e, nv = (*vel - w * c) * e;
        if ((to > from && v > to) || (to < from && v < to)) { v = to; nv = 0; }
        if (SDL_fabsf(to - v) < 0.3f && SDL_fabsf(nv) < 2) { *vel = 0; return to; }
        *vel = nv;
        app_redraw();
        return v;
    }
    float k = 1 - SDL_powf(0.5f, dt * speed);   /* half-life based, frame-rate independent */
    float v = from + (to - from) * k;
    if (SDL_fabsf(to - v) < 0.3f) v = to; else app_redraw();
    return v;
}

/* ---- screen stack ---------------------------------------------------------- */

/* ---- screen transitions ----------------------------------------------------- */

static struct { Screen *from; int owned, dir; float t; int active; int zoom; float rect[4]; char url[512]; } g_trans;
/* The card each pushed screen opened out of (so Back can shrink it back into it). */
static struct { int has; float r[4]; char url[512]; } g_origin[sizeof g_app.stack / sizeof g_app.stack[0]];

static int transitional(const Screen *s)
{
    return s && strcmp(s->name, "player") != 0 && strcmp(s->name, "ambient") != 0 && strcmp(s->name, "pin") != 0 && strcmp(s->name, "options") != 0;
}

static void trans_finish(void)
{
    if (!g_trans.active) return;
    if (g_trans.owned && g_trans.from && g_trans.from->destroy) g_trans.from->destroy(g_trans.from);
    memset(&g_trans, 0, sizeof g_trans);
}

/* `from` leaves (owned: destroyed when the animation ends), the new top arrives. With a
 * rect, the page grows out of it (push) or shrinks back into it (pop). */
static void trans_begin(Screen *from, int owned, int dir, const float *rect, const char *url)
{
    trans_finish();
    Screen *top = app_top();
    if (!g_app.transitions || g_app.fx_light || !from || !transitional(from) || !transitional(top) || (getenv("SNTV_NO_TRANSITIONS") && *getenv("SNTV_NO_TRANSITIONS"))) {
        if (owned && from && from->destroy) from->destroy(from);
        return;
    }
    g_trans.from = from; g_trans.owned = owned; g_trans.dir = dir; g_trans.t = 0; g_trans.active = 1;
    g_trans.zoom = rect != NULL;
    if (rect) memcpy(g_trans.rect, rect, sizeof g_trans.rect);
    snprintf(g_trans.url, sizeof g_trans.url, "%s", rect && url ? url : "");
    app_redraw();
}

int app_transition_active(void) { return g_trans.active; }

/* The page growing out of a card: the card's rectangle opens to the screen (it reaches the
 * edges at 75%, after which the page underneath is no longer drawn), the page inside it
 * settles from 92% and fades in, the page behind dims and steps back a little. k: 0 card .. 1 open. */
static void draw_one(Screen *s) { ui_hits_begin(s); s->draw(s); }   /* what it draws can be tapped (as its own) */

static void draw_zoom(Screen *under, Screen *page, float k)
{
    float W = SNTV_UI_W, H = SNTV_UI_H, *r = g_trans.rect;
    float o = k / 0.8f > 1 ? 1 : k / 0.8f, oe = o * o * (3 - 2 * o);   /* k is eased already: an S here, not a second rush */
    float x = r[0] * (1 - oe), y = r[1] * (1 - oe), w = r[2] + (W - r[2]) * oe, h = r[3] + (H - r[3]) * oe;
    float cx = r[0] + r[2] / 2, cy = r[1] + r[3] / 2;
    if (o < 1 && under && under->draw) {
        gfx_set_layer(0, 0, 1 - 0.03f * k, W / 2, H / 2, 1);
        draw_one(under);
        gfx_set_layer(0, 0, 1, 0, 0, 1);
        gfx_rect(0, 0, W, H, COLOR_ALPHA(THEME_BG, (int)(0xB0 * k)));
    }
    gfx_clip_push(x, y, w, h);
    gfx_rect(x, y, w, h, COLOR_ALPHA(THEME_BG, (int)(255 * (o < 0.4f ? o / 0.4f : 1))));
    gfx_set_layer(0, 0, 0.92f + 0.08f * k, cx, cy, k * 1.6f);
    draw_one(page);
    gfx_set_layer(0, 0, 1, 0, 0, 1);
    /* the card's own picture grows with it and fades as the page shows through */
    float pa = o < 0.4f ? 1 : o > 0.9f ? 0 : 1 - (o - 0.4f) / 0.5f;
    int iw, ih;
    unsigned tex = pa > 0 && g_trans.url[0] ? image_peek(g_trans.url, &iw, &ih) : 0;
    if (tex) {
        float u0, v0, u1, v1;
        ui_cover_uv(iw, ih, w, h, &u0, &v0, &u1, &v1);
        gfx_rtex_uv(tex, x, y, w, h, R_CARD * (1 - oe), u0, v0, u1, v1, RGBA(255, 255, 255, (int)(255 * pa)), 0);
    }
    gfx_clip_pop();
}

void app_draw_screen(Screen *top, float dt)
{
    static int sideways = -1;   /* a phone: upright only where the screen on top has a layout for it */
    int want = gfx_handheld() && top && !top->phone;
    if (want != sideways) { sideways = want; video_landscape_only(want); }
    if (!g_trans.active) { if (top && top->draw) draw_one(top); return; }
    /* a slow frame (a page building, a picture going up) slows the move instead of jumping it */
    g_trans.t += (dt > 1 / 30.0f ? 1 / 30.0f : dt) / (g_theme.id == THEME_APERTURE ? 0.22f : g_trans.zoom ? 0.36f : 0.26f);
    if (g_trans.t >= 1 || !top) { trans_finish(); if (top && top->draw) draw_one(top); return; }
    float t = g_trans.t, e = 1 - (1 - t) * (1 - t) * (1 - t);
    float W = SNTV_UI_W;
    Screen *from = g_trans.from;
    if (g_trans.zoom) {
        if (g_trans.dir > 0) draw_zoom(from, top, e);
        else if (from->draw) draw_zoom(top, from, 1 - e);
        app_redraw();
        return;
    }
    /* The page underneath is clipped to the strip the sliding page has not covered yet,
     * so the two pages together shade one screen of pixels instead of two plus a dim. */
    if (g_trans.dir > 0) {   /* push: the old page slides back and dims, the new one slides in from the right */
        float edge = W * (1 - e);
        gfx_clip_push(0, 0, edge, SNTV_UI_H);
        gfx_set_layer(-0.22f * W * e, 0, 1, 0, 0, 1);
        if (from->draw) draw_one(from);
        gfx_set_layer(0, 0, 1, 0, 0, 1);
        gfx_rect(0, 0, edge, SNTV_UI_H, COLOR_ALPHA(THEME_BG, (int)(0xA0 * e)));
        gfx_clip_pop();
        gfx_set_layer(edge, 0, 1, 0, 0, 1);
        draw_one(top);
    } else {                 /* pop: the page underneath comes forward, the old one slides out to the right */
        float edge = W * e;
        gfx_clip_push(0, 0, edge, SNTV_UI_H);
        gfx_set_layer(-0.22f * W * (1 - e), 0, 1, 0, 0, 1);
        draw_one(top);
        gfx_set_layer(0, 0, 1, 0, 0, 1);
        gfx_rect(0, 0, edge, SNTV_UI_H, COLOR_ALPHA(THEME_BG, (int)(0xA0 * (1 - e))));
        gfx_clip_pop();
        gfx_set_layer(edge, 0, 1, 0, 0, 1);
        if (from->draw) draw_one(from);
    }
    gfx_set_layer(0, 0, 1, 0, 0, 1);
    app_redraw();
}

void app_push(Screen *s)
{
    if (!s || g_app.depth >= (int)(sizeof g_app.stack / sizeof g_app.stack[0])) { if (s && s->destroy) s->destroy(s); return; }
    Screen *old = app_top();
    int d = g_app.depth;
    island_drop_focus();   /* a dialog or break over the resume card: OK is for it */
    /* opened with OK on a card drawn this moment: grow out of that card */
    g_origin[d].has = g_app.time - g_app.card_at < 0.12f && g_app.time - g_app.press_at < 0.12f && g_app.card_rect[2] > 0 && g_app.card_rect[3] > 0;
    if (g_origin[d].has) { memcpy(g_origin[d].r, g_app.card_rect, sizeof g_origin[d].r); snprintf(g_origin[d].url, sizeof g_origin[d].url, "%s", g_app.card_url); }
    g_app.stack[g_app.depth++] = s;
    trans_begin(old, 0, 1, g_origin[d].has ? g_origin[d].r : NULL, g_origin[d].url);
}

void app_pop(void)
{
    if (g_app.depth <= 1) { g_app.quit = 1; return; }
    Screen *s = g_app.stack[--g_app.depth];
    int d = g_app.depth;
    Screen *top = app_top();
    if (top && top->resume) top->resume(top);
    trans_begin(s, 1, -1, g_origin[d].has ? g_origin[d].r : NULL, g_origin[d].url);
    g_origin[d].has = 0;
}

void app_replace(Screen *s)
{
    Screen *old = g_app.depth ? g_app.stack[--g_app.depth] : NULL;
    if (!s || g_app.depth >= (int)(sizeof g_app.stack / sizeof g_app.stack[0])) { if (s && s->destroy) s->destroy(s); if (old && old->destroy) old->destroy(old); return; }
    g_origin[g_app.depth].has = 0;
    g_app.stack[g_app.depth++] = s;
    trans_begin(old, 1, 1, NULL, NULL);
}

Screen *app_top(void) { return g_app.depth ? g_app.stack[g_app.depth - 1] : NULL; }

void app_pip_enter(void)
{
    Screen *s = app_top();
    if (!s || strcmp(s->name, "player") != 0 || g_app.depth < 2) return;
    if (g_app.pip) app_pip_close();
    g_app.depth--;               /* detach, keep alive */
    g_app.pip = s;
    player_set_pip(s, 1);
    Screen *top = app_top();
    if (top && strcmp(top->name, "streams") == 0 && g_app.depth > 1) app_pop();   /* land on the episodes, not the source list */
    else if (top && top->resume) top->resume(top);
#ifdef SNTV_DESKTOP_PLAYER
    app_toast("Playing in the corner.  Click it for full screen or to close it");
#else
    app_toast("Playing in the corner.  Play/Pause: full screen  \xC2\xB7  Stop: close");
#endif
    app_redraw();
}

void app_pip_restore(void)
{
    Screen *s = g_app.pip;
    if (!s) return;
    g_app.pip = NULL;
    g_app.toast_until = 0;   /* the "playing in the corner" note no longer applies */
    LOGI("pip: full screen again");
    player_set_pip(s, 0);
    app_push(s);
    app_redraw();
}

void app_pip_close(void)
{
    Screen *s = g_app.pip;
    if (!s) return;
    g_app.pip = NULL;
    g_app.toast_until = 0;
    LOGI("pip: closed");
    player_set_pip(s, 0);
    if (s->destroy) s->destroy(s);
    app_redraw();
}

int app_pip_claim(const char *video_id)
{
    if (!g_app.pip) return 0;
    const char *cur = player_video_id(g_app.pip);
    if (video_id && cur && strcmp(cur, video_id) == 0) { app_pip_restore(); return 1; }
    app_pip_close();   /* one video plane: the new video takes it */
    return 0;
}

void app_toast(const char *fmt, ...)
{
    app_redraw();
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(g_app.toast, sizeof g_app.toast, fmt, ap);
    va_end(ap);
    g_app.toast_until = g_app.time + 3.5f;
    LOGI("toast: %s", g_app.toast);
}

/* ---- addons ---------------------------------------------------------------- */

static void free_addons(void)
{
    for (int i = 0; i < g_app.n_addons; i++) addon_free(&g_app.addons[i]);
    free(g_app.addons);
    g_app.addons = NULL;
    g_app.n_addons = 0;
}

static void cache_addon_collection(const char *json)
{
    store_write_file("addons.json", json, strlen(json));
}

static void apply_collection_json(const char *json, int from_cache)
{
    Addon *addons; int n; ApiError err;
    int rc = api_parse_addon_collection(json, &addons, &n, &err);
    if (rc != 0) {
        snprintf(g_app.addons_error, sizeof g_app.addons_error, "%s", err.message);
        if (rc == 1 && err.code == 1) {   /* session expired */
            app_logout();
            app_toast("Session expired, please sign in again");
        }
        return;
    }
    if (!from_cache && n == 0 && g_app.n_addons > 0) {   /* an empty answer never wipes a working collection */
        LOGW("addons: the account answered with no addons; keeping the %d already here", g_app.n_addons);
        free(addons);
        return;
    }
    free_addons();
    g_app.addons = addons;
    g_app.n_addons = n;
    g_app.addons_error[0] = 0;
    g_app.addons_version++;
    app_redraw();
    if (!from_cache) cache_addon_collection(json);
    LOGI("addons: %d loaded%s", n, from_cache ? " (cache)" : "");
    perf_mark(from_cache ? "addons (cache)" : "addons");
}

/* Which account a reply was asked for: one signed in since then drops it (a profile picked
 * while the last one's library or addons were still on their way). */
static volatile uintptr_t g_account_gen = 1;   /* read by the library worker too */

/* ---- an account whose addons were saved without their manifests ----------------------
 * Up to 0.10.23 this app saved the collection (after installing, removing or moving an
 * addon) with only id, name and version in each manifest, and Stremio's own apps then
 * cannot read the account's addons at all ("result.addons[0].manifest: missing field").
 * Every addon's manifest is fetched again from its own URL and the collection saved back
 * whole. An addon that does not answer keeps its place with empty lists (and a note
 * saying so), so every app reads the account again; it is asked again on later starts,
 * behind the collection already in use, and saved once it answers. */
typedef struct { int remaining, unreadable, fixed, blanked; cJSON *root; uintptr_t gen; } Repair;
static void push_collection_to_account(void);
#define BLANK_NOTE "Did not answer when this account was repaired; reinstall it if it stays empty"

/* Stremio's own OpenSubtitles addon still sits in many accounts on the old protocol
 * (".../stremio/v1", no manifest.json to fetch); its manifest is written out here */
#define OPENSUBS_LEGACY "https://opensubtitles.strem.io/stremio/v1"
static int is_known(const char *transport_url) { return transport_url && !strncmp(transport_url, OPENSUBS_LEGACY, strlen(OPENSUBS_LEGACY)); }
static cJSON *known_manifest(const char *transport_url)
{
    if (!is_known(transport_url)) return NULL;
    return cJSON_Parse("{\"id\":\"org.stremio.opensubtitles\",\"version\":\"0.24.0\",\"name\":\"OpenSubtitles\","
                       "\"description\":\"The official add-on for subtitles from OpenSubtitles\","
                       "\"resources\":[{\"name\":\"subtitles\",\"types\":[\"movie\",\"series\"],\"idPrefixes\":[\"tt\"]}],"
                       "\"types\":[\"movie\",\"series\"],\"idPrefixes\":[\"tt\"],\"catalogs\":[]}");
}

/* 2: no Stremio app can read it; 1: left blank by an earlier repair (or 0.10.25's empty
 * OpenSubtitles); 0: fine */
static int descriptor_state(const cJSON *d)
{
    const cJSON *m = cJSON_GetObjectItemCaseSensitive(d, "manifest"), *t = cJSON_GetObjectItemCaseSensitive(d, "transportUrl");
    if (!addon_manifest_complete(m)) return 2;
    if (cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(m, "resources"))) return 0;
    const cJSON *note = cJSON_GetObjectItemCaseSensitive(m, "description");
    return (cJSON_IsString(note) && !strcmp(note->valuestring, BLANK_NOTE)) || (cJSON_IsString(t) && is_known(t->valuestring));
}
static uintptr_t g_repairing;   /* the account generation a repair runs for, 0 none */

static void on_repair_saved(HttpResponse *r, void *user)
{
    (void)user;
    cJSON *root = r->body ? cJSON_Parse(r->body) : NULL;
    int ok = r->status == 200 && root && !cJSON_GetObjectItemCaseSensitive(root, "error");
    if (ok) { LOGI("addons: the account's collection is repaired"); app_toast("Your account's addons were repaired"); }
    else {
        LOGW("addons: saving the repaired collection failed (%ld %.200s)", r->status, r->body ? r->body : r->error);
        app_toast("Your account's addons could not be saved (%ld); it tries again next start", r->status);
    }
    cJSON_Delete(root);
}

/* Only blank ones were asked again: the collection in use (and any change made since)
 * takes the manifests that came back, and is saved. */
static void repair_blanks_done(Repair *rp, cJSON *addons)
{
    const cJSON *d;
    int changed = 0;
    cJSON_ArrayForEach(d, addons) {
        const cJSON *t = cJSON_GetObjectItemCaseSensitive(d, "transportUrl"), *m = cJSON_GetObjectItemCaseSensitive(d, "manifest");
        if (!cJSON_IsString(t) || descriptor_state(d)) continue;   /* still blank */
        for (int i = 0; i < g_app.n_addons; i++) {
            Addon *a = &g_app.addons[i];
            if (strcmp(a->transport_url, t->valuestring) || a->manifest.n_resources) continue;   /* another addon, or not blank here */
            char *json = cJSON_PrintUnformatted(m);
            Addon fresh;
            if (json && addon_new(t->valuestring, json, &fresh) == 0) {
                fresh.flag_official = a->flag_official; fresh.flag_protected = a->flag_protected;
                addon_free(a);
                *a = fresh;
                changed++;
            }
            free(json);
        }
    }
    if (!changed) { LOGI("addons: %d addon%s still not answering", rp->blanked, rp->blanked == 1 ? "" : "s"); return; }
    g_app.addons_version++;
    LOGI("addons: %d addon%s answered again; saving to the account", changed, changed == 1 ? "" : "s");
    push_collection_to_account();
}

static void repair_done(Repair *rp)
{
    if (g_repairing == rp->gen) g_repairing = 0;
    cJSON *addons = cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(rp->root, "result"), "addons");
    if (rp->gen == g_account_gen && !rp->unreadable) repair_blanks_done(rp, addons);
    else if (rp->gen == g_account_gen) {   /* still the account it was asked for */
        char *txt = cJSON_PrintUnformatted(rp->root);
        apply_collection_json(txt, 0);   /* this app has them back at once */
        free(txt);
        if (g_app.auth_key && (rp->unreadable || rp->fixed)) {   /* nothing better to save when only blank ones stayed blank */
            cJSON *o = cJSON_CreateObject();
            cJSON_AddStringToObject(o, "type", "AddonCollectionSet");
            cJSON_AddStringToObject(o, "authKey", g_app.auth_key);
            cJSON_AddItemToObject(o, "addons", cJSON_Duplicate(addons, 1));
            char *body = cJSON_PrintUnformatted(o);
            cJSON_Delete(o);
            http_request_async(api_url_addon_collection_set(), body, on_repair_saved, NULL);
            free(body);
            LOGI("addons: saving the repaired collection (%d addons, %d fixed, %d still not answering) to the account", cJSON_GetArraySize(addons), rp->fixed, rp->blanked);
            if (rp->blanked) app_toast("%d addon%s did not answer; asked again next start", rp->blanked, rp->blanked == 1 ? "" : "s");
        } else LOGI("addons: %d addon%s still not answering; the account is left as it is", rp->blanked, rp->blanked == 1 ? "" : "s");
    }
    cJSON_Delete(rp->root);
    free(rp);
}

static void on_repair_manifest(HttpResponse *r, void *user)
{
    void **u = user;
    Repair *rp = u[0];
    cJSON *d = u[1];
    int was = (int)(intptr_t)u[2];
    free(u);
    cJSON *m = r->status == 200 && r->body ? cJSON_Parse(r->body) : NULL;
    const cJSON *t = cJSON_GetObjectItemCaseSensitive(d, "transportUrl");
    if (addon_manifest_complete(m)) {
        addon_manifest_fill(m);
        addon_json_put(d, "manifest", m);
        rp->fixed++;
    } else {
        cJSON_Delete(m);
        rp->blanked++;
        LOGW("addons: no manifest from %s (%ld)%s", cJSON_IsString(t) ? t->valuestring : "?", r->status, was == 2 ? "; keeping it with empty lists" : "");
        if (was == 2) {   /* unreadable: completed with empty lists so every app reads the account again */
            cJSON *old = cJSON_GetObjectItemCaseSensitive(d, "manifest");
            if (!cJSON_IsObject(old)) { old = cJSON_CreateObject(); addon_json_put(d, "manifest", old); }
            if (!cJSON_IsString(cJSON_GetObjectItemCaseSensitive(old, "id"))) addon_json_put(old, "id", cJSON_CreateString(cJSON_IsString(t) ? t->valuestring : "unknown.addon"));
            if (!cJSON_IsString(cJSON_GetObjectItemCaseSensitive(old, "name"))) addon_json_put(old, "name", cJSON_CreateString("Addon"));
            if (!cJSON_IsString(cJSON_GetObjectItemCaseSensitive(old, "version"))) addon_json_put(old, "version", cJSON_CreateString("0.0.0"));
            if (!cJSON_IsArray(cJSON_GetObjectItemCaseSensitive(old, "types"))) addon_json_put(old, "types", cJSON_CreateArray());
            if (!cJSON_IsArray(cJSON_GetObjectItemCaseSensitive(old, "resources"))) addon_json_put(old, "resources", cJSON_CreateArray());
            addon_json_put(old, "description", cJSON_CreateString(BLANK_NOTE));   /* what the official apps show for it; also how it is found again */
            addon_manifest_fill(old);
        }
    }
    if (--rp->remaining == 0) repair_done(rp);
}

/* 1 when the collection was taken care of here: unusable until the repair ends, or used
 * at once while blank ones are asked again behind it */
static int g_repair_holds;
static int repair_collection(const char *json)
{
    if (g_repairing == g_account_gen) return g_repair_holds;
    cJSON *root = cJSON_Parse(json);
    cJSON *addons = cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(root, "result"), "addons"), *d;
    int unreadable = 0, blank = 0;
    cJSON_ArrayForEach(d, addons) { int st = descriptor_state(d); unreadable += st == 2; blank += st == 1; }
    if (!unreadable && !blank) { cJSON_Delete(root); return 0; }
    if (unreadable) LOGW("addons: %d of the account's addons were saved without their manifest; fetching them again", unreadable);
    if (blank) LOGI("addons: %d addon%s had no answer last time; asking again", blank, blank == 1 ? "" : "s");
    Repair *rp = calloc(1, sizeof *rp);
    rp->root = root; rp->gen = g_account_gen; rp->unreadable = unreadable;
    g_repairing = g_account_gen;
    g_repair_holds = unreadable > 0;
    if (!unreadable) apply_collection_json(json, 0);   /* before any answer updates it */
    rp->remaining = 1;   /* held until every request is out: a known manifest completes at once */
    cJSON_ArrayForEach(d, addons) {
        int st = descriptor_state(d);
        if (!st) continue;
        const cJSON *t = cJSON_GetObjectItemCaseSensitive(d, "transportUrl");
        cJSON *known = cJSON_IsString(t) ? known_manifest(t->valuestring) : NULL;
        if (known) { addon_json_put(d, "manifest", known); rp->fixed++; continue; }
        void **u = malloc(3 * sizeof(void *));
        u[0] = rp; u[1] = d; u[2] = (void *)(intptr_t)st;
        rp->remaining++;
        if (!cJSON_IsString(t)) { HttpResponse none = { 0 }; on_repair_manifest(&none, u); continue; }
        char url[1024];
        addon_url_manifest(t->valuestring, url, sizeof url);
        HttpOptions o = { .timeout_s = 12 };
        http_request_async_ex(url, &o, on_repair_manifest, u);
    }
    if (--rp->remaining == 0) repair_done(rp);
    return 1;
}

/* A failed collection is asked again: a TV often starts the app before its network is up */
static float g_addons_retry_in = -1;
static int g_addons_tries;

/* Addons that did not answer at this start: still on the list this TV keeps (an addon
 * down for an hour is not uninstalled by the next change to the list) */
enum { UNLOADED_MAX = 32 };
static char *g_unloaded[UNLOADED_MAX]; static int g_n_unloaded;
static void unloaded_add(const char *url) { if (g_n_unloaded < UNLOADED_MAX) g_unloaded[g_n_unloaded++] = strdup(url); }
static void unloaded_clear(void) { while (g_n_unloaded) free(g_unloaded[--g_n_unloaded]); }

/* Signing in after using this TV on its own: the addons installed here join the account's
 * once its list arrives (the built-in catalog stays out: no other app could read it). */
enum { DEFAULTS_MAX_MERGE = 32 };
static char *g_merge[DEFAULTS_MAX_MERGE]; static int g_n_merge;
static void install_addon(const char *transport_url, int replace, int quiet);

void app_sync_begin(void)
{
    while (g_n_merge) free(g_merge[--g_n_merge]);
    if (g_app.auth_key) return;   /* one account to another: nothing of this TV's own */
    for (int i = 0; i < g_app.n_addons && g_n_merge < DEFAULTS_MAX_MERGE; i++) {
        const char *u = g_app.addons[i].transport_url;
        if (addon_is_builtin(u) || !strcmp(u, STREMIO_CINEMETA_URL) || !strcmp(u, STREMIO_OPENSUBS_URL)) continue;
        g_merge[g_n_merge++] = strdup(u);
    }
    for (int i = 0; i < g_n_unloaded && g_n_merge < DEFAULTS_MAX_MERGE; i++) g_merge[g_n_merge++] = strdup(g_unloaded[i]);   /* down right now, still this TV's */
    if (g_n_merge) LOGI("account: %d addon%s from this TV will join the account", g_n_merge, g_n_merge == 1 ? "" : "s");
}

static void merge_into_account(void)
{
    for (int m = 0; m < g_n_merge; m++) {
        int have = 0;
        for (int i = 0; i < g_app.n_addons; i++) if (!strcmp(g_app.addons[i].transport_url, g_merge[m])) have = 1;
        if (!have) install_addon(g_merge[m], 0, 0);   /* fetches it and saves the account's list */
        free(g_merge[m]);
    }
    g_n_merge = 0;
}

static void on_collection(HttpResponse *r, void *user)
{
    if ((uintptr_t)user != g_account_gen) { LOGI("addons: a reply for the previous account, dropped"); return; }
    g_app.addons_loading = 0;
    if (r->status != 200) {
        snprintf(g_app.addons_error, sizeof g_app.addons_error, "addon collection: %s", r->error[0] ? r->error : "HTTP error");
        g_addons_retry_in = g_addons_tries < 6 ? (float)(3 << g_addons_tries) : 120;   /* 3, 6, 12 ... seconds, then every 2 minutes */
        g_addons_tries++;
        LOGW("addons: the collection failed (%ld %s); asking again in %.0f s", r->status, r->error, g_addons_retry_in);
        app_redraw();
        return;
    }
    g_addons_tries = 0;
    if (r->body && repair_collection(r->body)) return;   /* applied once every manifest is back */
    apply_collection_json(r->body, 0);
    merge_into_account();
}

/* With no Stremio sync, the addons live on this TV (store "localAddons": every address,
 * in order). Their manifests are fetched at each start and assembled in that order,
 * whichever answers first (Home's rows and the Sources tabs follow it). */
enum { DEFAULTS_MAX = 64 };
typedef struct { int remaining, n; cJSON *slot[DEFAULTS_MAX]; } DefaultsCtx;
typedef struct { DefaultsCtx *ctx; int i; char *url; } DefaultReq;

static void on_default_manifest(HttpResponse *r, void *user)
{
    DefaultReq *q = user;
    DefaultsCtx *ctx = q->ctx;
    cJSON *m = r->status == 200 ? cJSON_Parse(r->body) : NULL;
    if (m) {
        cJSON *d = cJSON_CreateObject();
        cJSON_AddStringToObject(d, "transportUrl", q->url);
        cJSON_AddItemToObject(d, "manifest", m);
        cJSON *f = cJSON_AddObjectToObject(d, "flags");
        cJSON_AddBoolToObject(f, "official", addon_is_builtin(q->url));
        cJSON_AddBoolToObject(f, "protected", addon_is_builtin(q->url));   /* the built-in catalog stays; any other can go */
        ctx->slot[q->i] = d;
    } else {
        LOGW("addons: default manifest %s failed (%ld %s)", q->url, r->status, r->error);
        unloaded_add(q->url);
    }
    free(q->url);
    free(q);
    if (--ctx->remaining == 0) {
        cJSON *root = cJSON_CreateObject();
        cJSON *res = cJSON_AddObjectToObject(root, "result");
        cJSON *addons = cJSON_AddArrayToObject(res, "addons");
        for (int i = 0; i < ctx->n; i++) if (ctx->slot[i]) cJSON_AddItemToArray(addons, ctx->slot[i]);
        char *txt = cJSON_PrintUnformatted(root);
        cJSON_Delete(root);
        g_app.addons_loading = 0;
        apply_collection_json(txt, 0);
        free(txt);
        free(ctx);
    }
}

static void request_default(DefaultsCtx *ctx, const char *url)
{
    DefaultReq *q = malloc(sizeof *q);
    q->ctx = ctx; q->i = ctx->n++; q->url = strdup(url);
    http_request_async(url, NULL, on_default_manifest, q);
}

static void load_default_addons(void)
{
    DefaultsCtx *ctx = calloc(1, sizeof *ctx);
    const char *urls[DEFAULTS_MAX];
    int n = 0;
    char *copy = NULL;
    /* SNTV_DEFAULT_ADDONS="url1,url2" replaces the built-in defaults (used by tests/smoke.sh). */
    const char *override = getenv("SNTV_DEFAULT_ADDONS");
    if (override && *override) {
        copy = strdup(override);
        for (char *tok = strtok(copy, ","); tok && n < DEFAULTS_MAX; tok = strtok(NULL, ",")) urls[n++] = tok;
    } else {
        const char *catalog = tmdb_enabled() ? ADDON_BUILTIN_CATALOG : STREMIO_CINEMETA_URL;   /* TMDB needs a key: Cinemeta stands in */
        cJSON *mine = store_get_json("localAddons"), *e;
        if (cJSON_IsArray(mine) && cJSON_GetArraySize(mine) > 0) {
            cJSON_ArrayForEach(e, mine) {
                if (!cJSON_IsString(e) || n >= DEFAULTS_MAX) continue;
                const char *u = addon_is_builtin(e->valuestring) ? catalog : e->valuestring;
                int dup = 0;
                for (int k = 0; k < n; k++) if (!strcmp(urls[k], u)) dup = 1;
                if (!dup) urls[n++] = u;
            }
        } else {   /* the first start, or one from before this TV kept the whole list */
            urls[n++] = catalog;
            urls[n++] = STREAMING_CATALOGS_URL;   /* what each streaming service has, as rows */
            if (!opensubs_enabled()) urls[n++] = STREMIO_OPENSUBS_URL;   /* with a key the player asks OpenSubtitles itself */
            cJSON *extra = store_get_json("local_addons");   /* addons installed then: added to the defaults */
            cJSON_ArrayForEach(e, extra) if (cJSON_IsString(e) && n < DEFAULTS_MAX) urls[n++] = e->valuestring;
        }
    }
    unloaded_clear();
    if (!n) { free(copy); free(ctx); g_app.addons_loading = 0; return; }
    ctx->remaining = n;   /* all counted before the first answer can land */
    for (int i = 0; i < n; i++) request_default(ctx, urls[i]);
    free(copy);
}

void app_load_addons(void)
{
    g_app.addons_loading = 1;
    g_app.addons_error[0] = 0;
    /* Instant start from the last known collection, then refresh. */
    char *cached = store_read_file("addons.json");
    if (cached) { apply_collection_json(cached, 1); free(cached); }

    if (g_app.auth_key) {
        char *body = api_body_addon_collection_get(g_app.auth_key);
        http_request_async(api_url_addon_collection_get(), body, on_collection, (void *)g_account_gen);
        free(body);
    } else {
        load_default_addons();
    }
}

static void push_collection_to_account(void)
{
    if (!g_app.auth_key) return;
    char *body = api_body_addon_collection_set(g_app.auth_key, g_app.addons, g_app.n_addons);
    if (!body) { LOGW("addons: not saved to the account: a manifest is not complete here"); return; }
    http_request_async(api_url_addon_collection_set(), body, NULL, NULL);
    free(body);
}

static void persist_local_addon_urls(void)
{
    cJSON *arr = cJSON_CreateArray();
    for (int i = 0; i < g_app.n_addons; i++) cJSON_AddItemToArray(arr, cJSON_CreateString(g_app.addons[i].transport_url));
    for (int i = 0; i < g_n_unloaded; i++) {   /* down at this start, not removed */
        int have = 0;
        for (int k = 0; k < g_app.n_addons; k++) if (!strcmp(g_app.addons[k].transport_url, g_unloaded[i])) have = 1;
        if (!have) cJSON_AddItemToArray(arr, cJSON_CreateString(g_unloaded[i]));
    }
    store_set_json("localAddons", arr);
    store_set_json("local_addons", NULL);
    store_save();
}

/* replace: one the viewer installs takes the place of the same addon; a merge only adds.
 * quiet: a sync's, no toast for each. gen: the account it was asked under. */
typedef struct { char *url; int replace, quiet; uintptr_t gen; } Install;

static void on_install_manifest(HttpResponse *r, void *user)
{
    Install *in = user;
    char *url = in->url;
    int replace = in->replace, quiet = in->quiet, stale = in->gen != g_account_gen;
    free(in);
    if (stale) { LOGI("addons: an install for the previous account, dropped"); free(url); return; }
    const char *h = strstr(url, "://");   /* only the host in the log: the rest can carry the addon's settings and keys */
    h = h ? h + 3 : url;
    int hl = (int)strcspn(h, "/?#");
    if (r->status != 200) {
        LOGW("addons: %.*s did not install: HTTP %ld%s%s", hl, h, r->status, r->error[0] ? ", " : "", r->error);
        if (!quiet) { if (r->status > 0) app_toast("Could not add the addon: its server answered %ld", r->status); else app_toast("Could not add the addon: %s", r->error[0] ? r->error : "no answer"); }
        free(url); return;
    }
    Addon a;
    if (addon_new(url, r->body, &a) != 0) {
        const char *b = r->body ? r->body : "";
        while (*b == ' ' || *b == '\t' || *b == '\r' || *b == '\n') b++;
        int page = *b == '<' || strstr(r->content_type, "html");   /* a web page: the addon's own, or one a network puts in its place */
        LOGW("addons: %.*s did not install: what it sent is no addon manifest (%zu bytes, %s%s%s)", hl, h, r->len,
             r->content_type[0] ? r->content_type : "no type", page ? ", a web page" : "", r->final_url[0] ? ", after a redirect" : "");
        if (!quiet) {
            if (!*b) app_toast("Could not add the addon: its server sent nothing");
            else if (page) app_toast("Could not add the addon: its address shows a web page: it may have moved, or this network blocks it");
            else app_toast("Could not add the addon: what its server sent is no addon");
        }
        free(url); return;
    }
    for (int i = 0; i < g_app.n_addons; i++)
        if (strcmp(g_app.addons[i].transport_url, url) == 0) { if (!quiet) app_toast("Addon already installed"); addon_free(&a); free(url); return; }
    int at = -1;   /* the same addon set up again (its settings live in the link): it takes the old one's place */
    for (int i = 0; i < g_app.n_addons && at < 0 && replace; i++)
        if (a.manifest.id && g_app.addons[i].manifest.id && !strcmp(a.manifest.id, g_app.addons[i].manifest.id) && !g_app.addons[i].flag_protected) at = i;
    if (at >= 0) {
        a.flag_official = g_app.addons[at].flag_official;
        addon_free(&g_app.addons[at]);
        g_app.addons[at] = a;
        LOGI("addons: %s set up again, in its place (%d)", a.manifest.name, at + 1);
    } else {
        g_app.addons = realloc(g_app.addons, sizeof(Addon) * (size_t)(g_app.n_addons + 1));
        g_app.addons[g_app.n_addons++] = a;
    }
    g_app.addons_version++;
    if (!quiet) app_toast(at >= 0 ? "%s updated with its new settings" : "Installed %s", a.manifest.name);
    free(url);
    if (g_app.auth_key) push_collection_to_account(); else persist_local_addon_urls();
}

/* an install link as it is kept: an addon page's stremio:// is https, and it names the manifest */
static void addon_link(const char *link, char *url, size_t n)
{
    char in[4096];   /* a configured addon carries its settings in the link: some run to kilobytes */
    while (*link == ' ') link++;
    if (!strncmp(link, "stremio://", 10)) snprintf(in, sizeof in, "https://%s", link + 10);
    else snprintf(in, sizeof in, "%s", link);
    char *m = strstr(in, "/manifest.json");   /* anything after it (a query, a #) is no part of the address */
    if (m) m[14] = 0;
    else {   /* an addon's settings page: its address is the addon's, without /configure */
        size_t l = strcspn(in, "?#");
        in[l] = 0;
        while (l && in[l - 1] == '/') in[--l] = 0;
        if (l >= 10 && !strcmp(in + l - 10, "/configure")) in[l - 10] = 0;
    }
    addon_url_manifest(in, url, n);
}

static void install_addon(const char *transport_url, int replace, int quiet)
{
    char url[4096];
    addon_link(transport_url, url, sizeof url);
    Install *job = malloc(sizeof *job);
    job->url = strdup(url); job->replace = replace; job->quiet = quiet; job->gen = g_account_gen;
    http_request_async(url, NULL, on_install_manifest, job);
}

void app_install_addon(const char *transport_url) { install_addon(transport_url, 1, 0); }

int app_addon_index(const char *link)
{
    char url[4096];
    addon_link(link, url, sizeof url);
    for (int i = 0; i < g_app.n_addons; i++) if (!strcmp(g_app.addons[i].transport_url, url)) return i;
    return -1;
}

/* Opened with an addon's install link (stremio://host/.../manifest.json, as addon pages offer):
 * it installs, as Settings > Addons would. Only the host goes in the log: an addon's address
 * often carries its configuration, a debrid key among it. */
void app_open_links(void)
{
    static float looked;
    if (g_app.time - looked < 0.5f) return;
    looked = g_app.time;
    char link[4096];
    static int env_done;
    const char *env = env_done ? NULL : getenv("SNTV_OPEN_LINK");   /* tests: as if opened with this link */
    env_done = 1;
    if (env && *env) snprintf(link, sizeof link, "%s", env);
    else if (!video_take_open_url(link, sizeof link)) return;
    if (!strncmp(link, "cuelight://sync", 15)) {   /* a device's sync code, scanned on this phone */
        LOGI("app: opened with a sync code");
        app_push(screen_sync_create(strstr(link, "u=") ? link : NULL));   /* no device in it: this one's code */
        return;
    }
    char url[4200];
    if (!strncmp(link, "stremio://", 10)) snprintf(url, sizeof url, "https://%s", link + 10);
    else snprintf(url, sizeof url, "%s", link);
    const char *h = strstr(url, "://");
    h = h ? h + 3 : url;
    int hl = (int)strcspn(h, "/?#");
    if (strncmp(url, "http", 4) != 0 || !strstr(url, "manifest.json")) { LOGI("app: opened with a link that is no addon (%.*s)", hl, h); return; }
    LOGI("app: opened with an addon's install link (%.*s)", hl, h);
    app_toast("Installing the addon from the link");
    app_install_addon(url);
}

void app_remove_addon(int i)
{
    if (i < 0 || i >= g_app.n_addons) return;
    if (g_app.addons[i].flag_protected) { app_toast("%s cannot be removed", g_app.addons[i].manifest.name); return; }
    addon_free(&g_app.addons[i]);
    memmove(&g_app.addons[i], &g_app.addons[i + 1], sizeof(Addon) * (size_t)(g_app.n_addons - i - 1));
    g_app.n_addons--;
    g_app.addons_version++;
    if (g_app.auth_key) push_collection_to_account(); else persist_local_addon_urls();
}

int app_move_addon(int i, int dir)
{
    int j = i + dir;
    if (i < 0 || j < 0 || i >= g_app.n_addons || j >= g_app.n_addons) return 0;
    Addon t = g_app.addons[i]; g_app.addons[i] = g_app.addons[j]; g_app.addons[j] = t;
    g_app.addons_version++;
    if (g_app.auth_key) push_collection_to_account(); else persist_local_addon_urls();
    return 1;
}

int app_title_watched(const char *id)
{
    LibraryItem *li = app_library_find(id);
    return li && !li->removed && (li->flagged_watched || li->times_watched > 0) && li->time_offset == 0;
}

/* ---- saved accounts, parental PIN -------------------------------------------- */

static void accounts_remember(const char *email, const char *key)
{
    if (!email || !key) return;
    cJSON *arr = store_get_json("accounts");
    cJSON *copy = cJSON_CreateArray();
    if (cJSON_IsArray(arr)) {
        cJSON *it;
        cJSON_ArrayForEach(it, arr) {
            const cJSON *e = cJSON_GetObjectItemCaseSensitive(it, "email");
            if (cJSON_IsString(e) && strcmp(e->valuestring, email) == 0) continue;   /* replaced below */
            cJSON_AddItemToArray(copy, cJSON_Duplicate(it, 1));
        }
    }
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "email", email);
    cJSON_AddStringToObject(o, "key", key);
    cJSON_AddItemToArray(copy, o);
    store_set_json("accounts", copy);
    store_save();
}

int app_saved_accounts(char emails[][128], int max)
{
    cJSON *arr = store_get_json("accounts");
    int n = 0;
    if (!cJSON_IsArray(arr)) return 0;
    cJSON *it;
    cJSON_ArrayForEach(it, arr) {
        const cJSON *e = cJSON_GetObjectItemCaseSensitive(it, "email");
        if (!cJSON_IsString(e) || n >= max) continue;
        if (g_app.user_email && strcmp(e->valuestring, g_app.user_email) == 0) continue;
        snprintf(emails[n++], 128, "%s", e->valuestring);
    }
    return n;
}

static cJSON *saved_account_at(int index)
{
    cJSON *arr = store_get_json("accounts");
    if (!cJSON_IsArray(arr)) return NULL;
    int n = 0;
    cJSON *it;
    cJSON_ArrayForEach(it, arr) {
        const cJSON *e = cJSON_GetObjectItemCaseSensitive(it, "email");
        if (!cJSON_IsString(e)) continue;
        if (g_app.user_email && strcmp(e->valuestring, g_app.user_email) == 0) continue;
        if (n++ == index) return it;
    }
    return NULL;
}

void app_switch_account(int index)
{
    cJSON *it = saved_account_at(index);
    if (!it) return;
    const cJSON *e = cJSON_GetObjectItemCaseSensitive(it, "email"), *k = cJSON_GetObjectItemCaseSensitive(it, "key");
    if (!cJSON_IsString(e) || !cJSON_IsString(k)) return;
    free(g_app.auth_key); free(g_app.user_email);
    g_app.auth_key = strdup(k->valuestring);
    g_app.user_email = strdup(e->valuestring);
    lists_set_owner(g_app.user_email);
    g_account_gen++;
    store_set_str("authKey", g_app.auth_key);
    store_set_str("email", g_app.user_email);
    store_save();
    library_items_free(g_app.library, g_app.n_library);
    g_app.library = NULL; g_app.n_library = 0;
    g_app.library_version++;
    store_write_file("library.json", "{\"result\":[]}", 13);
    app_load_addons();
    app_load_library();
    app_toast("Switched to %s", g_app.user_email);
}

void app_forget_account(int index)
{
    cJSON *it = saved_account_at(index);
    if (!it) return;
    cJSON *arr = store_get_json("accounts");
    cJSON_DetachItemViaPointer(arr, it);
    cJSON_Delete(it);
    store_save();
}

void app_set_parental_pin(const char *pin)
{
    snprintf(g_app.parental_pin, sizeof g_app.parental_pin, "%.7s", pin ? pin : "");
    store_set_str("parentalPin", g_app.parental_pin[0] ? g_app.parental_pin : NULL);
    if (!g_app.parental_pin[0]) { g_app.pin_settings = 0; store_set_int("pinSettings", 0); }
    store_save();
}

static void settings_after_pin(int ok, void *user) { (void)user; if (ok) app_push(screen_settings_create()); }

void app_open_settings(void)
{
    if (g_app.parental_pin[0] && g_app.pin_settings) app_push(screen_pin_create("Settings are locked", 0, settings_after_pin, NULL));
    else app_push(screen_settings_create());
}

/* ---- home rows --------------------------------------------------------------- */

enum { HR_MAX = 160 };
static struct { char key[200]; int hidden; } g_hr[HR_MAX];
static int g_n_hr = -1;

static void home_rows_load(void)
{
    g_n_hr = 0;
    const char *s = store_get_str("homeRows", "");
    while (*s && g_n_hr < HR_MAX) {
        const char *e = strchr(s, '\n');
        size_t len = e ? (size_t)(e - s) : strlen(s);
        char line[232];
        snprintf(line, sizeof line, "%.*s", (int)(len < 231 ? len : 231), s);
        char *tab = strchr(line, '\t');
        if (tab) { *tab = 0; g_hr[g_n_hr].hidden = tab[1] == '1'; } else g_hr[g_n_hr].hidden = 0;
        snprintf(g_hr[g_n_hr].key, sizeof g_hr[g_n_hr].key, "%.199s", line);
        if (line[0]) g_n_hr++;
        s = e ? e + 1 : s + len;
    }
}

static void home_rows_save(void)
{
    char *buf = malloc((size_t)HR_MAX * 232 + 1);
    size_t o = 0;
    for (int i = 0; i < g_n_hr; i++) o += (size_t)snprintf(buf + o, 232, "%s\t%d\n", g_hr[i].key, g_hr[i].hidden);
    store_set_str("homeRows", buf);
    store_save();
    free(buf);
    g_app.home_rows_version++;
}

static int hr_find(const char *key) { for (int i = 0; i < g_n_hr; i++) if (strcmp(g_hr[i].key, key) == 0) return i; return -1; }

int app_home_rows(HomeRowDef *out, int max)
{
    if (g_n_hr < 0) home_rows_load();
    HomeRowDef *all = calloc((size_t)max, sizeof *all);
    int n = 0;
    if (n < max) { snprintf(all[n].key, sizeof all[n].key, "continue"); snprintf(all[n].title, sizeof all[n].title, "Continue Watching"); all[n].kind = 0; n++; }
    if (n < max) { snprintf(all[n].key, sizeof all[n].key, "library"); snprintf(all[n].title, sizeof all[n].title, "My Library"); all[n].kind = 1; n++; }
    for (int l = 0; l < LIST_COUNT && n < max; l++) {   /* the saved lists (a row shows once it has a title) */
        snprintf(all[n].key, sizeof all[n].key, "list:%d", l); snprintf(all[n].title, sizeof all[n].title, "%s", LIST_NAMES[l]);
        all[n].kind = 3; all[n].catalog = l; n++;
    }
    for (int t = 0; t < TROW_COUNT && n < max && tracking_connected(TRACK_TRAKT); t++) {   /* Trakt, once signed in */
        snprintf(all[n].key, sizeof all[n].key, "trakt:%d", t); snprintf(all[n].title, sizeof all[n].title, "%s", TROW_NAMES[t]);
        all[n].kind = 4; all[n].catalog = t; n++;
    }
    for (int a = 0; a < g_app.n_addons; a++) {
        const Addon *ad = &g_app.addons[a];
        if (g_app.hide_adult && ad->manifest.bh_adult) continue;
        for (int c = 0; c < ad->manifest.n_catalogs && n < max; c++) {
            const AddonCatalog *cat = &ad->manifest.catalogs[c];
            if (addon_catalog_has_required_extra(cat)) continue;
            const char *type = strcmp(cat->type, "movie") == 0 ? "Movies" : strcmp(cat->type, "series") == 0 ? "Series" : cat->type;
            snprintf(all[n].key, sizeof all[n].key, "catalog:%s:%s:%s", ad->manifest.id ? ad->manifest.id : ad->transport_url, cat->type, cat->id);
            snprintf(all[n].title, sizeof all[n].title, "%s %s  \xC2\xB7  %s", cat->name, type, ad->manifest.name);
            all[n].kind = 2; all[n].addon = a; all[n].catalog = c;
            n++;
        }
    }
    /* saved order first, then anything new in its natural place */
    int k = 0;
    for (int i = 0; i < g_n_hr && k < n; i++) {
        for (int j = 0; j < n; j++) if (all[j].key[0] && strcmp(all[j].key, g_hr[i].key) == 0) { out[k] = all[j]; out[k].hidden = g_hr[i].hidden; k++; all[j].key[0] = 0; break; }
    }
    for (int j = 0; j < n; j++) if (all[j].key[0]) { out[k] = all[j]; out[k].hidden = 0; k++; }
    free(all);
    return k;
}

static void hr_sync(void)
{
    /* make the saved list the complete current one, in the order shown */
    HomeRowDef rows[HR_MAX];
    int n = app_home_rows(rows, HR_MAX);
    g_n_hr = n;
    for (int i = 0; i < n; i++) { snprintf(g_hr[i].key, sizeof g_hr[i].key, "%.199s", rows[i].key); g_hr[i].hidden = rows[i].hidden; }
}

void app_home_row_move(const char *key, int dir)
{
    hr_sync();
    int i = hr_find(key), j = i + dir;
    if (i < 0 || j < 0 || j >= g_n_hr) return;
    char tk[200]; int th;
    memcpy(tk, g_hr[i].key, sizeof tk); th = g_hr[i].hidden;
    memcpy(g_hr[i].key, g_hr[j].key, sizeof tk); g_hr[i].hidden = g_hr[j].hidden;
    memcpy(g_hr[j].key, tk, sizeof tk); g_hr[j].hidden = th;
    home_rows_save();
}

void app_home_row_toggle(const char *key)
{
    hr_sync();
    int i = hr_find(key);
    if (i < 0) return;
    g_hr[i].hidden = !g_hr[i].hidden;
    home_rows_save();
}

/* ---- auth ------------------------------------------------------------------ */

static void (*g_login_done)(int, const char *);

static void on_login(HttpResponse *r, void *user)
{
    (void)user;
    ApiError err;
    char *key = NULL, *email = NULL;
    if (r->status == 0) { if (g_login_done) g_login_done(0, r->error); return; }
    int rc = api_parse_auth(r->body, &key, &email, &err);
    if (rc != 0) { if (g_login_done) g_login_done(0, err.message); return; }
    app_sync_begin();
    free(g_app.auth_key); free(g_app.user_email);
    g_app.auth_key = key;
    g_app.user_email = email;
    lists_set_owner(email);
    g_account_gen++;
    store_set_str("authKey", key);
    store_set_str("email", email);
    store_save();
    accounts_remember(email, key);
    app_load_addons();
    app_load_library();
    if (g_login_done) g_login_done(1, email ? email : "");
}

void app_login(const char *email, const char *password, void (*done)(int, const char *))
{
    g_login_done = done;
    char *body = api_body_login(email, password);
    http_request_async(api_url_login(), body, on_login, NULL);
    free(body);
}

static void save_library_cache(void);

/* Stops syncing with Stremio; nothing is lost. The account's addons stay on this TV (its
 * Cinemeta becomes the built-in catalog when TMDB is on), and so do the library, progress
 * and lists, kept here from now on. */
void app_logout(void)
{
    if (g_app.auth_key) {
        if (!store_get_int("signinsShared", 0)) {   /* a session synced to another device stays open for it */
            char *body = api_body_logout(g_app.auth_key);
            http_request_async(api_url_logout(), body, NULL, NULL);
            free(body);
        }
        if (g_app.n_addons) {
            cJSON *arr = cJSON_CreateArray(), *e;
            for (int i = 0; i < g_app.n_addons; i++) {
                const char *u = g_app.addons[i].transport_url;
                if (!strcmp(u, STREMIO_CINEMETA_URL) && tmdb_enabled()) u = ADDON_BUILTIN_CATALOG;
                if (strstr(u, "127.0.0.1:11470/local-addon")) continue;   /* Local Files: needs Stremio's own server on this TV */
                int dup = 0;
                cJSON_ArrayForEach(e, arr) if (!strcmp(e->valuestring, u)) dup = 1;
                if (!dup) cJSON_AddItemToArray(arr, cJSON_CreateString(u));
            }
            store_set_json("localAddons", arr);
        }
        char key[200];
        snprintf(key, sizeof key, "lists:%s", g_app.user_email ? g_app.user_email : "");
        cJSON *theirs = store_get_json(key);
        if (cJSON_IsObject(theirs)) store_set_json("lists", cJSON_Duplicate(theirs, 1));
        LOGI("account: stopped syncing; %d addons and %d library items stay on this TV", g_app.n_addons, g_app.n_library);
    }
    free(g_app.auth_key); free(g_app.user_email);
    g_app.auth_key = g_app.user_email = NULL;
    lists_set_owner(NULL);
    g_account_gen++;
    store_set_str("authKey", NULL);
    store_set_str("email", NULL);
    store_save();
    save_library_cache();   /* the library on the TV is the only copy now */
    app_library_flush(1);
    app_load_addons();
}

/* ---- library --------------------------------------------------------------- */

static void set_library(LibraryItem *items, int n, int from_cache)
{
    library_items_free(g_app.library, g_app.n_library);
    g_app.library = items;
    g_app.n_library = n;
    g_app.library_version++;
    app_redraw();
    LOGI("library: %d items%s", n, from_cache ? " (cache)" : "");
}

static void apply_library_json(const char *json, int from_cache)
{
    LibraryItem *items; int n; ApiError err;
    if (api_parse_library(json, &items, &n, &err) != 0) { LOGW("library: %s", err.message); return; }
    set_library(items, n, from_cache);
}

/* A big library is a few hundred KB of JSON: parse and cache it on the network thread. */
typedef struct { LibraryItem *items; int n; } ParsedLibrary;

static void work_library(HttpResponse *r, const char *url)
{
    (void)url;
    if (r->status != 200) return;
    ParsedLibrary *pl = calloc(1, sizeof *pl);
    ApiError err;
    if (api_parse_library(r->body, &pl->items, &pl->n, &err) != 0) { LOGW("library: %s", err.message); free(pl); return; }
    if ((uintptr_t)r->user == g_account_gen) store_write_file("library.json", r->body, r->len);   /* not the last profile's over this one's */
    r->parsed = pl;
}

static void free_library(void *parsed)
{
    ParsedLibrary *pl = parsed;
    library_items_free(pl->items, pl->n);
    free(pl);
}

static void push_library_item(const LibraryItem *it);

/* The account's library replaces ours, except items changed here later than there:
 * progress that never reached the server (the TV went off, the app was killed, no
 * network) stays, and is sent again. */
static int keep_newer_local(LibraryItem **items, int *n)
{
    int resent = 0;
    for (int i = 0; i < g_app.n_library; i++) {
        LibraryItem *mine = &g_app.library[i];
        if (!mine->id || !mine->mtime_ms) continue;
        LibraryItem *theirs = NULL;
        for (int k = 0; k < *n; k++) if (!strcmp((*items)[k].id, mine->id)) { theirs = &(*items)[k]; break; }
        if (theirs && theirs->mtime_ms >= mine->mtime_ms) continue;
        if (!theirs && !mine->last_watched_ms) continue;   /* only progress is worth bringing back */
        if (theirs) { LibraryItem t = *theirs; *theirs = *mine; *mine = t; }
        else {
            *items = realloc(*items, sizeof **items * (size_t)(*n + 1));
            theirs = &(*items)[(*n)++];
            *theirs = *mine;
            memset(mine, 0, sizeof *mine);
        }
        push_library_item(theirs);
        resent++;
    }
    if (resent) LOGI("library: %d item%s newer on this TV than on the account; sent again", resent, resent == 1 ? "" : "s");
    return resent;
}

static void save_library_cache(void);

static void on_library(HttpResponse *r, void *user)
{
    if ((uintptr_t)user != g_account_gen) { LOGI("library: a reply for the previous account, dropped"); return; }   /* its items are freed with the reply */
    g_app.library_loading = 0;
    ParsedLibrary *pl = r->parsed;
    if (!pl) return;
    r->parsed = NULL;
    int resent = keep_newer_local(&pl->items, &pl->n);
    set_library(pl->items, pl->n, 0);
    free(pl);
    if (resent) save_library_cache();   /* the file has the account's copy: keep this TV's newer progress on flash too */
}

void app_load_library(void)
{
    char *cached = store_read_file("library.json");
    if (cached) { apply_library_json(cached, 1); free(cached); }
    if (!g_app.auth_key) return;
    g_app.library_loading = 1;
    char *body = api_body_datastore_get(g_app.auth_key, STREMIO_LIBRARY_COLLECTION);
    HttpOptions o = { .post_json = body, .work = work_library, .free_parsed = free_library };
    http_request_async_ex(api_url_datastore_get(), &o, on_library, (void *)g_account_gen);
    free(body);
}

LibraryItem *app_library_find(const char *id)
{
    for (int i = 0; i < g_app.n_library; i++)
        if (strcmp(g_app.library[i].id, id) == 0) return &g_app.library[i];
    return NULL;
}

static int64_t now_ms(void) { return (int64_t)time(NULL) * 1000; }

/* Writing the whole library is 50-100 ms on a TV CPU, and progress lands every 30 s
 * while a video plays (the server copy is updated at once by push_library_item). So a
 * change only marks the cache dirty; it is written once nothing plays and the app has
 * been quiet for a moment, when it goes to the background, and at exit. */
static int g_library_dirty;
static float g_library_dirty_at;

static void save_library_cache(void)
{
    g_app.library_version++;
    if (!g_library_dirty) g_library_dirty_at = g_app.time;
    g_library_dirty = 1;
}

void app_library_flush(int force)
{
    static float written_at = -1000;
    if (!g_library_dirty) return;
    if (!force) {
        const Screen *top = app_top();
        int playing = g_app.pip || (top && strcmp(top->name, "player") == 0);
        if (g_app.time - g_library_dirty_at < 3) return;
        if (playing && g_app.time - written_at < 300) return;   /* while playing, every five minutes: with no account (or offline) it is the only copy */
    }
    g_library_dirty = 0;
    written_at = g_app.time;
    cJSON *root = cJSON_CreateObject();
    cJSON *arr = cJSON_AddArrayToObject(root, "result");
    for (int i = 0; i < g_app.n_library; i++) cJSON_AddItemToArray(arr, library_item_to_json(&g_app.library[i]));
    char *txt = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    store_write_file("library.json", txt, strlen(txt));
    free(txt);
}

static LibraryItem *library_upsert(const MetaPreview *meta)
{
    LibraryItem *it = app_library_find(meta->id);
    if (it) return it;
    g_app.library = realloc(g_app.library, sizeof(LibraryItem) * (size_t)(g_app.n_library + 1));
    it = &g_app.library[g_app.n_library++];
    memset(it, 0, sizeof *it);
    it->id = strdup(meta->id);
    it->name = strdup(meta->name);
    it->type = strdup(meta->type);
    it->poster = meta->poster ? strdup(meta->poster) : NULL;
    it->poster_shape = strdup(meta->poster_shape ? meta->poster_shape : "poster");
    it->removed = 1;      /* progress-only entries are "temp" until the user adds them */
    it->temp = 1;
    it->ctime_ms = now_ms();
    return it;
}

static void on_pushed(HttpResponse *r, void *user)
{
    (void)user;
    if (r->status != 200) LOGW("library: the account did not take the change (%ld %s)", r->status, r->status ? "" : r->error);
}

/* The account's copy: sent at once, and still sent if the app is closing (http_shutdown waits). */
static void push_library_item(const LibraryItem *it)
{
    if (!g_app.auth_key) return;
    char *body = api_body_datastore_put(g_app.auth_key, STREMIO_LIBRARY_COLLECTION, it, 1);
    HttpOptions o = { .post_json = body, .deliver = 1 };
    http_request_async_ex(api_url_datastore_put(), &o, on_pushed, NULL);
    free(body);
}

/* An episode finished. The first one recorded for a series also records those before it:
 * until now they were shown watched for coming first, and must stay so. */
static void record_finished(const char *series_id, const char *video_id)
{
    const char *ids[512];
    int n = 0;
    if (!app_episode_marks_any(series_id)) {
        int nv;
        const MetaVideo *v = app_videos(series_id, &nv), *c = NULL;
        for (int i = 0; i < nv; i++) if (!strcmp(v[i].id, video_id)) c = &v[i];
        for (int i = 0; c && i < nv && n < 511; i++)
            if (v[i].season > 0 && (v[i].season < c->season || (v[i].season == c->season && v[i].episode < c->episode))) ids[n++] = v[i].id;
        if (n) LOGI("library: %d earlier episodes recorded as watched with this one", n);
    }
    ids[n++] = video_id;
    app_episode_set_marks(series_id, ids, n, 1);
}

void app_library_progress(const MetaPreview *meta, const char *video_id, double position_s, double duration_s, int played_through)
{
    LibraryItem *it = library_upsert(meta);
    uint64_t pos = (uint64_t)(position_s * 1000), dur = (uint64_t)(duration_s * 1000);
    uint64_t prev = it->time_offset;
    it->time_offset = pos;
    if (dur) it->duration = dur;
    if (pos > prev) { it->time_watched += pos - prev; it->overall_time_watched += pos - prev; }
    it->last_watched_ms = now_ms();
    it->mtime_ms = it->last_watched_ms;
    free(it->video_id);
    it->video_id = video_id ? strdup(video_id) : NULL;
    /* Stremio marks an item watched past ~90%: a film leaves Continue Watching; a series
     * stays in it at the start of the next episode (1 ms, as Stremio does) */
    if (dur && pos > dur * 9 / 10 && played_through) {   /* a seek to the end to peek is not watching it */
        if (prev > 1 || !it->flagged_watched) it->times_watched++;   /* once per crossing, not per report after it */
        if (video_id && strchr(video_id, ':')) record_finished(meta->id, video_id);   /* this episode, by name: the title page shows what was watched, not what came before */
        it->flagged_watched = 1; it->time_offset = 0;
        char next[160];
        if (video_id && app_next_video(meta->id, video_id, next, sizeof next, NULL, 0)) {
            free(it->video_id);
            it->video_id = strdup(next);
            it->time_offset = 1;
        }
    }
    save_library_cache();
    push_library_item(it);
}

/* A resume point from elsewhere (Trakt: another app or device), when it is newer than
 * ours; the account gets it too, so every Stremio sees it. 1 when it was taken. */
int app_library_remote_progress(const char *id, const char *type, const char *name, const char *video_id,
                                double position_s, double duration_s, int64_t when_ms)
{
    if (!id || !*id || !type || duration_s <= 0 || position_s <= 0) return 0;
    LibraryItem *it = app_library_find(id);
    if (it && it->last_watched_ms >= when_ms) return 0;   /* ours is newer (or the same) */
    MetaPreview m = {0};
    m.id = (char *)id; m.type = (char *)type; m.name = (char *)(name ? name : "");
    it = library_upsert(&m);
    it->time_offset = (uint64_t)(position_s * 1000);
    it->duration = (uint64_t)(duration_s * 1000);
    it->last_watched_ms = when_ms;
    it->mtime_ms = when_ms;
    it->flagged_watched = 0;
    free(it->video_id);
    it->video_id = video_id ? strdup(video_id) : NULL;
    save_library_cache();
    push_library_item(it);
    return 1;
}

/* The account's library again: on the way back to the app and every 15 min while nothing
 * plays, so what was watched on a phone or another TV shows up here. */
static double g_library_pulled_at;
static void pull_library(const char *why)
{
    if (!g_app.auth_key || g_app.library_loading) return;
    g_library_pulled_at = g_app.time;
    g_app.library_loading = 1;
    LOGI("library: asking the account again (%s)", why);
    char *body = api_body_datastore_get(g_app.auth_key, STREMIO_LIBRARY_COLLECTION);
    HttpOptions o = { .post_json = body, .work = work_library, .free_parsed = free_library };
    http_request_async_ex(api_url_datastore_get(), &o, on_library, (void *)g_account_gen);
    free(body);
    tracking_pull_playback();
}

void app_library_foreground(void)
{
    if (g_app.time - g_library_pulled_at > 60) pull_library("back on screen");
}

void app_library_tick(int playing)
{
    if (g_library_pulled_at == 0) g_library_pulled_at = g_app.time;   /* the launch already asked */
    if (!playing && g_app.time - g_library_pulled_at > 15 * 60) pull_library("every 15 min");
}

/* ---- episode marks ---------------------------------------------------------- */

#define EPISODE_MARK_SERIES 200   /* series kept; the oldest go first */

int app_episode_mark(const char *series_id, const char *video_id)
{
    const cJSON *all = store_get_json("episodeMarks");
    const cJSON *series = series_id ? cJSON_GetObjectItemCaseSensitive(all, series_id) : NULL;
    const cJSON *m = video_id ? cJSON_GetObjectItemCaseSensitive(series, video_id) : NULL;
    return cJSON_IsBool(m) ? cJSON_IsTrue(m) : -1;
}

int app_episode_marks_any(const char *series_id)
{
    const cJSON *all = store_get_json("episodeMarks");
    const cJSON *series = series_id ? cJSON_GetObjectItemCaseSensitive(all, series_id) : NULL, *m;
    cJSON_ArrayForEach(m, series) if (cJSON_IsTrue(m)) return 1;
    return 0;
}

void app_episode_set_marks(const char *series_id, const char *const *video_ids, int n, int watched)
{
    if (!series_id || n <= 0) return;
    const cJSON *old = store_get_json("episodeMarks");
    cJSON *all = old ? cJSON_Duplicate(old, 1) : cJSON_CreateObject();
    cJSON *series = cJSON_DetachItemFromObjectCaseSensitive(all, series_id);
    if (!series) series = cJSON_CreateObject();
    for (int i = 0; i < n; i++) {
        if (!video_ids[i]) continue;
        cJSON_DeleteItemFromObjectCaseSensitive(series, video_ids[i]);
        cJSON_AddBoolToObject(series, video_ids[i], watched);
    }
    while (cJSON_GetArraySize(all) >= EPISODE_MARK_SERIES) cJSON_DeleteItemFromArray(all, 0);
    cJSON_AddItemToObject(all, series_id, series);   /* most recently touched last */
    store_set_json("episodeMarks", all);
    store_save();
    g_app.library_version++;   /* screens that show watched state redraw */
    app_redraw();
}

void app_library_mark_watched(const MetaPreview *meta, int watched)
{
    LibraryItem *it = library_upsert(meta);
    if (it->removed) it->temp = 1;   /* not in the Library, but its state is tracked (as the Stremio apps do) */
    it->flagged_watched = watched;
    if (watched && it->times_watched == 0) it->times_watched = 1;
    if (!watched) it->times_watched = 0;
    it->time_offset = 0;
    it->mtime_ms = now_ms();
    save_library_cache();
    push_library_item(it);
    app_toast(watched ? "Marked as watched" : "Marked as not watched");
}

void app_library_dismiss_continue(const MetaPreview *meta)
{
    LibraryItem *it = library_upsert(meta);
    it->time_offset = 0;
    it->mtime_ms = now_ms();
    save_library_cache();
    push_library_item(it);
    app_toast("Removed from Continue Watching");
}

/* ---- updates ---- */
char g_update_version[16], g_update_url[600], g_update_sha256[80];
static int g_update_offered;   /* the question was asked this run (Later means: at the next start) */
static int g_update_manual;    /* a check the viewer asked for: say the result */

static int version_newer(const char *a, const char *b)   /* a > b, dotted numbers */
{
    int x[3] = { 0 }, y[3] = { 0 };
    sscanf(a, "%d.%d.%d", &x[0], &x[1], &x[2]); sscanf(b, "%d.%d.%d", &y[0], &y[1], &y[2]);
    for (int i = 0; i < 3; i++) if (x[i] != y[i]) return x[i] > y[i];
    return 0;
}

static void on_repo(HttpResponse *r, void *user)
{
    int version_only = user != NULL;   /* the TV's list, asked by a computer: the version, nothing to install */
#ifdef SNTV_PLATFORM_WINDOWS
    if (r->status == 404 && !version_only && !getenv("SNTV_UPDATE_URL")) {   /* a release from before the installer */
        http_request_async("https://github.com/smalik29871/cuelight-releases/releases/latest/download/repo.json", NULL, on_repo, (void *)1);
        return;
    }
#endif
    int manual = g_update_manual;
    g_update_manual = 0;
    if (r->status != 200) { if (manual) app_toast("Could not check for updates (%ld)", r->status); return; }
    cJSON *root = cJSON_Parse(r->body);
    const cJSON *pk = root ? cJSON_GetObjectItemCaseSensitive(root, "packages") : NULL;
    const cJSON *p0 = cJSON_IsArray(pk) ? cJSON_GetArrayItem(pk, 0) : NULL;
    const cJSON *mf = p0 ? cJSON_GetObjectItemCaseSensitive(p0, "manifest") : NULL;
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(mf, "version");
    const cJSON *url = cJSON_GetObjectItemCaseSensitive(mf, "ipkUrl");
    const cJSON *sha = cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(mf, "ipkHash"), "sha256");
    if (cJSON_GetObjectItemCaseSensitive(root, "apkUrl")) {   /* Android's: {"version", "apkUrl", "sha256"} */
        v = cJSON_GetObjectItemCaseSensitive(root, "version");
        url = cJSON_GetObjectItemCaseSensitive(root, "apkUrl");
        sha = cJSON_GetObjectItemCaseSensitive(root, "sha256");
    }
    if (cJSON_GetObjectItemCaseSensitive(root, "setupUrl")) {   /* Windows': {"version", "setupUrl", "sha256"} */
        v = cJSON_GetObjectItemCaseSensitive(root, "version");
        url = cJSON_GetObjectItemCaseSensitive(root, "setupUrl");
        sha = cJSON_GetObjectItemCaseSensitive(root, "sha256");
    }
    if (version_only) url = sha = NULL;
    if (cJSON_IsString(v) && version_newer(v->valuestring, SNTV_APP_VERSION)) {
        snprintf(g_update_version, sizeof g_update_version, "%s", v->valuestring);
        snprintf(g_update_url, sizeof g_update_url, "%s", cJSON_IsString(url) ? url->valuestring : "");
        snprintf(g_update_sha256, sizeof g_update_sha256, "%s", cJSON_IsString(sha) ? sha->valuestring : "");
        LOGI("update: %s available (running %s)", v->valuestring, SNTV_APP_VERSION);
        if (manual) g_update_offered = 0;   /* asked for it: the question comes at once */
        if (!g_app.auto_update && strcmp(store_get_str("updateToldFor", ""), v->valuestring) != 0) {
            app_toast("Version %s is ready: Settings > About > Update", v->valuestring);
            store_set_str("updateToldFor", v->valuestring); store_save();
        }
    } else if (manual) app_toast(SNTV_APP_NAME " %s is the newest version", SNTV_APP_VERSION);
    if (root) cJSON_Delete(root);
}

static const char *repo_url(void)
{
    const char *url = getenv("SNTV_UPDATE_URL");
#ifdef SNTV_PLATFORM_ANDROID
    return url ? url : "https://github.com/smalik29871/cuelight-releases/releases/latest/download/android.json";   /* the APK of the same release */
#elif defined(SNTV_PLATFORM_WINDOWS)
    return url ? url : "https://github.com/smalik29871/cuelight-releases/releases/latest/download/windows.json";   /* the installer of the same release */
#else
    return url ? url : "https://github.com/smalik29871/cuelight-releases/releases/latest/download/repo.json";   /* the public releases repository (renamed; the old address still forwards here) */
#endif
}

void app_update_check_now(void)
{
    g_update_manual = 1;
    http_request_async(repo_url(), NULL, on_repo, NULL);
}


/* Somewhere it is fine for the app to close for a minute: nothing playing, no break. */
static int quiet_moment(void)
{
    const Screen *top = app_top();
    if (!top || g_app.pip) return 0;
    /* nor over a list starting a video, the screensaver, or typing */
    static const char *const BUSY[] = { "player", "break", "welcome", "login", "update", "pin", "options", "streams", "ambient", "search", "settings", NULL };
    for (int i = 0; BUSY[i]; i++) if (!strcmp(top->name, BUSY[i])) return 0;
    if (streams_starting()) return 0;
    return g_app.time - g_app.last_input > 2;   /* not in the middle of pressing keys */
}

#if defined(SNTV_PLATFORM_ANDROID) || defined(SNTV_DESKTOP_PLAYER)
int app_update_installs_here(void) { return 1; }   /* Android's own installer, or the app's own on a computer */
#else
int app_update_installs_here(void) { return !store_get_int("updateNoInstaller", 0); }
#endif

static void on_update_answer(int choice, void *user)
{
    (void)user;
    if (choice == 1) { LOGI(app_update_installs_here() ? "update: the viewer said update now" : "update: the viewer asked how"); app_push(screen_update_create(0)); }   /* Later is first: a stray OK never installs */
    else LOGI("update: the viewer said later");
}

/* Where this version can't install the update itself (a computer's update from before the
 * installer): the release page. 0 where there is no such way. */
int app_update_by_hand(void)
{
#ifdef SNTV_PLATFORM_WINDOWS
    if (video_open_browser("https://github.com/smalik29871/cuelight-releases/releases/latest")) { LOGI("update: the download page opened"); return 1; }
#endif
    return 0;
}

#ifdef SNTV_DESKTOP_PLAYER
static void on_update_download(int choice, void *user)
{
    (void)user;
    if (choice == 1) app_update_by_hand();
}
#endif

/* The question, at a quiet moment: nothing installs unless the viewer says so. */
static void ask_to_update(void)
{
    static const char *const ITEMS[] = { "Later", "Update now" };
    static const int ICONS[] = { ICON_CLOCK, ICON_RELOAD };
    char sub[80], body[300];
    snprintf(sub, sizeof sub, "Version %s  \xC2\xB7  you have %s", g_update_version, SNTV_APP_VERSION);
#ifdef SNTV_DESKTOP_PLAYER
    if (!g_update_url[0] || !g_update_sha256[0]) {   /* a release without the installer: its page */
        static const char *const GET[] = { "Later", "Open the download page" };
        snprintf(body, sizeof body, "Download the installer (or the zip) from the release page. Your sign-in, settings and progress are kept in your user folder, not with the app.");
        app_push(screen_options_unasked(screen_options_create_ex("An update is ready", sub, body, GET, ICONS, 2, on_update_download, NULL)));
        return;
    }
    snprintf(body, sizeof body, "It downloads here, then Cuelight closes, installs it and opens again by itself, in about half a minute. "
             "Your sign-in, settings and progress stay. Later asks again next time (Settings > About > Update any time).");
    app_push(screen_options_unasked(screen_options_create_ex("An update is ready", sub, body, ITEMS, ICONS, 2, on_update_answer, NULL)));
    return;
#endif
    if (!app_update_installs_here()) {   /* this TV won't let the app install: say once per version where it's done */
        if (!strcmp(store_get_str("updateHowToldFor", ""), g_update_version)) return;
        store_set_str("updateHowToldFor", g_update_version); store_save();
        static const char *const HOW[] = { "Later", "Show me how" };
        snprintf(body, sizeof body, "This TV doesn't let apps install updates themselves, so the Homebrew Channel does it: "
                 "a minute, and your sign-in, settings and progress stay.");
        app_push(screen_options_unasked(screen_options_create_ex("An update is ready", sub, body, HOW, ICONS, 2, on_update_answer, NULL)));
        return;
    }
#ifdef SNTV_PLATFORM_ANDROID
    snprintf(body, sizeof body, "It downloads here, then Android asks you to confirm the install (the first time, also to let "
             "Cuelight install apps). Your sign-in, settings and progress stay. Later asks again next time (Settings > About > Update any time).");
#else
    snprintf(body, sizeof body, "It takes about half a minute. The app closes while the update installs (by the Homebrew Channel, "
             "or the TV's own installer in Developer Mode); open it again afterwards. Later asks again next time (Settings > About > Update).");
#endif
    app_push(screen_options_unasked(screen_options_create_ex("An update is ready", sub, body, ITEMS, ICONS, 2, on_update_answer, NULL)));
}

static float g_update_t, g_update_next = -1, g_update_checked_at = -1000;

/* Opened again from the TV's Home (webOS keeps the app running behind it): that is an
 * opening too, so it checks again and a "Later" from before is asked once more. */
void app_update_foreground(void)
{
    if (g_update_t - g_update_checked_at < 60) return;
    g_update_offered = 0;
    g_update_next = g_update_t + 3;
    LOGI("update: back on screen, checking soon");
}

static void storage_watch(float dt);
void app_update_check(float dt)
{
    storage_watch(dt);
    if (g_addons_retry_in > 0 && (g_addons_retry_in -= dt) <= 0) {
        g_addons_retry_in = -1;
        if (g_app.auth_key) {
            g_app.addons_loading = 1;
            char *body = api_body_addon_collection_get(g_app.auth_key);
            http_request_async(api_url_addon_collection_get(), body, on_collection, (void *)g_account_gen);
            free(body);
        }
    }
    if (g_update_next < 0) g_update_next = getenv("SNTV_UPDATE_URL") ? 1 : 3;   /* soon after opening: Home is up by then */
    g_update_t += dt;
    float t = g_update_t, next = g_update_next;
#ifdef SNTV_DESKTOP_PLAYER
    int ready = g_update_version[0] != 0;   /* without an installer, its page */
#else
    int ready = g_update_version[0] && g_update_url[0] && g_update_sha256[0];
#endif
    if (ready && g_app.auto_update && !g_update_offered && quiet_moment()) {
        g_update_offered = 1;
        ask_to_update();
        return;
    }
    if (t < next) return;
    g_update_next = t + 6 * 3600;   /* then every six hours while it runs */
    g_update_checked_at = t;
    if (getenv("SNTV_SCREENSHOT") && !getenv("SNTV_UPDATE_URL")) return;   /* headless test runs stay offline */
    http_request_async(repo_url(), NULL, on_repo, NULL);
}

/* ---- links that failed to start, sorted last for six hours ---- */
#define DEAD_MAX 64
static struct { char *url; time_t when; } g_dead[DEAD_MAX];
static int g_n_dead;

static void dead_save(void)
{
    cJSON *arr = cJSON_CreateArray();
    for (int i = 0; i < g_n_dead; i++) { cJSON *o = cJSON_CreateObject(); cJSON_AddStringToObject(o, "u", g_dead[i].url); cJSON_AddNumberToObject(o, "t", (double)g_dead[i].when); cJSON_AddItemToArray(arr, o); }
    store_set_json("deadLinks", arr);
}

static void dead_load(void)
{
    const cJSON *arr = store_get_json("deadLinks"), *it;
    time_t now = time(NULL);
    cJSON_ArrayForEach(it, arr) {
        const cJSON *u = cJSON_GetObjectItemCaseSensitive(it, "u"), *t = cJSON_GetObjectItemCaseSensitive(it, "t");
        if (!cJSON_IsString(u) || !cJSON_IsNumber(t) || now - (time_t)t->valuedouble > 6 * 3600 || g_n_dead >= DEAD_MAX) continue;
        g_dead[g_n_dead].url = strdup(u->valuestring); g_dead[g_n_dead].when = (time_t)t->valuedouble; g_n_dead++;
    }
}

/* What this home's connection carries, learnt from the relay while films play: a
 * moving figure, kept with when it was last seen. Sources heavier than it sort lower. */
static double g_home_mbps = -1;
void app_home_mbps_note(double mbps)
{
    if (mbps <= 0.5) return;
    if (mbps > 2000) mbps = 2000;   /* a local file, or a line faster than any film needs */
    double old = app_home_mbps();
    g_home_mbps = old > 0 ? old * 0.7 + mbps * 0.3 : mbps;
    static double logged;
    if (fabs(g_home_mbps - logged) > logged * 0.15 + 1) { logged = g_home_mbps; LOGI("speed: this home carries about %.0f Mbit/s (saw %.0f)", g_home_mbps, mbps); }
    cJSON *o = cJSON_CreateObject();
    cJSON_AddNumberToObject(o, "mbps", (int)(g_home_mbps * 10) / 10.0);
    cJSON_AddNumberToObject(o, "at", (double)time(NULL));
    store_set_json("homeSpeed", o);
    store_save();
}
double app_home_mbps(void)
{
    if (g_home_mbps >= 0) return g_home_mbps;
    const cJSON *o = store_get_json("homeSpeed");
    const cJSON *m = cJSON_GetObjectItemCaseSensitive(o, "mbps"), *at = cJSON_GetObjectItemCaseSensitive(o, "at");
    g_home_mbps = cJSON_IsNumber(m) && cJSON_IsNumber(at) && time(NULL) - (time_t)at->valuedouble < 30 * 86400 ? m->valuedouble : 0;   /* a month old says little */
    return g_home_mbps;
}

void app_link_failed(const char *url)
{
    if (!url || strncmp(url, "http", 4) != 0) return;
    time_t now = time(NULL);
    for (int i = 0; i < g_n_dead; i++) if (strcmp(g_dead[i].url, url) == 0) { g_dead[i].when = now; dead_save(); return; }
    if (g_n_dead == DEAD_MAX) { free(g_dead[0].url); memmove(&g_dead[0], &g_dead[1], sizeof g_dead[0] * (DEAD_MAX - 1)); g_n_dead--; }
    g_dead[g_n_dead].url = strdup(url); g_dead[g_n_dead].when = now; g_n_dead++;
    dead_save();
}

int app_link_dead(const char *url)
{
    if (!url) return 0;
    time_t now = time(NULL);
    for (int i = 0; i < g_n_dead; i++) if (strcmp(g_dead[i].url, url) == 0) return now - g_dead[i].when <= 6 * 3600;
    return 0;
}

void app_library_toggle(const MetaPreview *meta)
{
    LibraryItem *it = library_upsert(meta);
    it->removed = !it->removed;
    it->temp = 0;
    it->mtime_ms = now_ms();
    save_library_cache();
    push_library_item(it);
    app_toast(it->removed ? "Removed from Library" : "Added to Library");
}

/* ---- streams --------------------------------------------------------------- */

char *app_stream_playable_url(const Stream *s, char *reason, size_t n)
{
    if (s->url) {
        if (strncmp(s->url, "http://", 7) == 0 || strncmp(s->url, "https://", 8) == 0) return strdup(s->url);
        if (strncmp(s->url, "magnet:", 7) == 0) { snprintf(reason, n, "Magnet links need a streaming server"); return NULL; }
        return strdup(s->url);   /* rtmp/ftp: let the pipeline decide */
    }
    if (s->info_hash) {
        if (torbox_hash_cached(s->info_hash)) return torbox_hash_link(s->info_hash, s->file_idx);   /* TorBox's copy, not peers */
        if (!g_app.streaming_server || !*g_app.streaming_server) {   /* P2P on this TV, the fallback */
            char *u = g_app.p2p_fallback ? torrent_engine_url(s->info_hash, s->file_idx, s->trackers) : NULL;
            if (!u) snprintf(reason, n, g_app.p2p_fallback ? "Torrent streams need a Stremio streaming server (Settings)" : "P2P on this TV is off (Settings > Sources)");
            return u;
        }
        char buf[1024];
        /* server.js convention: /{infoHash}/{fileIdx}; -1 lets the server pick the largest file */
        snprintf(buf, sizeof buf, "%s/%s/%d", g_app.streaming_server, s->info_hash, s->file_idx < 0 ? -1 : s->file_idx);
        return strdup(buf);
    }
    if (s->yt_id) { snprintf(reason, n, "YouTube streams are not supported on this device yet"); return NULL; }
    if (s->external_url) { snprintf(reason, n, "This stream opens in an external app"); return NULL; }
    snprintf(reason, n, "Stream has no playable source");
    return NULL;
}

/* ---- catalog disk cache ----------------------------------------------------- */

static void cache_name(const char *url, char *out, size_t n)
{
    unsigned long long h = 1469598103934665603ull;
    for (const char *p = url; *p; p++) h = (h ^ (unsigned char)*p) * 1099511628211ull;
    snprintf(out, n, "cache/%016llx.json", h);
}

char *app_cache_get(const char *url, long max_age)
{
    char name[64], path[1200];
    cache_name(url, name, sizeof name);
    snprintf(path, sizeof path, "%s/%s", store_dir(), name);
    struct stat st;
    if (stat(path, &st) != 0) return NULL;
    if (max_age > 0 && time(NULL) - st.st_mtime > max_age) return NULL;
    return store_read_file(name);
}

void app_work_metas(HttpResponse *r, const char *url)
{
    (void)url;
    if (r->status != 200) return;
    ParsedMetas *pm = calloc(1, sizeof *pm);
    if (addon_parse_metas(r->body, &pm->m, &pm->n) != 0) { free(pm); return; }
    r->parsed = pm;
}
void app_work_metas_cached(HttpResponse *r, const char *url)
{
    app_work_metas(r, url);
    ParsedMetas *pm = r->parsed;
    if (pm && pm->n) app_cache_put(url, r->body, r->len);
}
void app_free_metas(void *parsed) { ParsedMetas *pm = parsed; if (!pm) return; meta_previews_free(pm->m, pm->n); free(pm); }

void app_work_streams(HttpResponse *r, const char *url)
{
    (void)url;
    if (r->status != 200) return;
    ParsedStreams *ps = calloc(1, sizeof *ps);
    if (addon_parse_streams(r->body, &ps->s, &ps->n) != 0) { free(ps); return; }
    for (int i = 0; i < ps->n; i++)
        quality_parse(ps->s[i].name, ps->s[i].title, ps->s[i].description, ps->s[i].filename, ps->s[i].video_size, &ps->s[i].tags);
    torbox_mark_streams(ps->s, ps->n);   /* torrents TorBox has cached play from it */
    r->parsed = ps;
}
void app_free_streams(void *parsed) { ParsedStreams *ps = parsed; if (!ps) return; streams_free(ps->s, ps->n); free(ps); }

unsigned app_fetch_metas(const char *url, int cache, HttpCallback cb, void *user)
{
    HttpOptions o = { 0 };
    o.work = cache ? app_work_metas_cached : app_work_metas;
    o.free_parsed = app_free_metas;
    return http_request_async_ex(url, &o, cb, user);
}
unsigned app_fetch_streams(const char *url, HttpCallback cb, void *user)
{
    HttpOptions o = { 0 };
    o.work = app_work_streams;
    torbox_enabled();   /* hands the key to the network thread's TorBox check */
    o.free_parsed = app_free_streams;
    return http_request_async_ex(url, &o, cb, user);
}

void app_cache_put(const char *url, const char *body, size_t len)
{
    char name[64], dir[1200];
    snprintf(dir, sizeof dir, "%s/cache", store_dir());
    mkdir(dir, 0755);
    cache_name(url, name, sizeof name);
    store_write_file(name, body, len);
}

/* ---- meta cache ---------------------------------------------------------------
 * Home already asks for a title's meta to show its synopsis; Detail asked for the
 * same thing again and waited on it. The raw answer is kept in memory (and on disk
 * for an hour) so Detail opens with its episodes already there. */

#define META_CACHE 32
#define META_FRESH_S (30 * 60)
static struct { char key[200]; char *body; float at; } g_meta[META_CACHE];
static int g_meta_next;

static void meta_key(const char *type, const char *id, char *out, size_t n) { snprintf(out, n, "meta:%s:%s", type ? type : "", id ? id : ""); }

void app_meta_put(const char *type, const char *id, const char *body, size_t len)
{
    if (!body || !len) return;
    char key[200];
    meta_key(type, id, key, sizeof key);
    int slot = -1;
    for (int i = 0; i < META_CACHE; i++) if (strcmp(g_meta[i].key, key) == 0) slot = i;
    if (slot < 0) { slot = g_meta_next; g_meta_next = (g_meta_next + 1) % META_CACHE; }
    free(g_meta[slot].body);
    snprintf(g_meta[slot].key, sizeof g_meta[slot].key, "%s", key);
    g_meta[slot].body = malloc(len + 1);
    memcpy(g_meta[slot].body, body, len);
    g_meta[slot].body[len] = 0;
    g_meta[slot].at = g_app.time;
    app_cache_put(key, body, len);
}

char *app_meta_get(const char *type, const char *id)
{
    char key[200];
    meta_key(type, id, key, sizeof key);
    for (int i = 0; i < META_CACHE; i++)
        if (g_meta[i].body && strcmp(g_meta[i].key, key) == 0 && g_app.time - g_meta[i].at < META_FRESH_S) return strdup(g_meta[i].body);
    return app_cache_get(key, 3600);
}

/* ---- details for a focused card ---------------------------------------------
 * The card's catalog entry often lacks the synopsis, runtime or genres. A screen
 * asks for them from update() once a card has been focused for a moment; drawing
 * only reads what is already parsed here. */

#define DETAILS_CACHE 24
static struct { char key[200]; MetaPreview p; int have; } g_details[DETAILS_CACHE];
static int g_details_next;
static char g_details_pending[200];

static void details_store(const char *key, const MetaPreview *p)
{
    int slot = -1;
    for (int i = 0; i < DETAILS_CACHE; i++) if (g_details[i].have && strcmp(g_details[i].key, key) == 0) slot = i;
    if (slot < 0) { slot = g_details_next; g_details_next = (g_details_next + 1) % DETAILS_CACHE; }
    if (g_details[slot].have) meta_preview_free(&g_details[slot].p);
    snprintf(g_details[slot].key, sizeof g_details[slot].key, "%s", key);
    meta_preview_copy(&g_details[slot].p, p);
    g_details[slot].have = 1;
}

static int details_parse(const char *key, const char *body)
{
    MetaItem item;
    if (!body || addon_parse_meta(body, &item) != 0) return 0;
    details_store(key, &item.p);
    meta_item_free(&item);
    return 1;
}

static void on_details(HttpResponse *r, void *user)
{
    char *key = user;
    if (strcmp(g_details_pending, key) == 0) g_details_pending[0] = 0;
    if (r->status == 200 && r->len && details_parse(key, r->body)) {
        const char *type = key + 5, *colon = strchr(type, ':');   /* "meta:<type>:<id>" */
        if (colon) {
            char t[40];
            snprintf(t, sizeof t, "%.*s", (int)(colon - type), type);
            app_meta_put(t, colon + 1, r->body, r->len);
        }
        app_redraw();
    }
    free(key);
}

const MetaPreview *app_details_cached(const char *type, const char *id)
{
    char key[200];
    meta_key(type, id, key, sizeof key);
    for (int i = 0; i < DETAILS_CACHE; i++) if (g_details[i].have && strcmp(g_details[i].key, key) == 0) return &g_details[i].p;
    return NULL;
}

void app_details_want(const char *type, const char *id)
{
    if (!type || !id || app_details_cached(type, id)) return;
    char key[200];
    meta_key(type, id, key, sizeof key);
    if (strcmp(g_details_pending, key) == 0) return;
    char *body = app_meta_get(type, id);   /* Home or Detail may have fetched it already */
    int ok = details_parse(key, body);
    free(body);
    if (ok) { app_redraw(); return; }
    for (int i = 0; i < g_app.n_addons; i++) {
        if (!addon_supports(&g_app.addons[i], "meta", type, id)) continue;
        char url[1024];
        addon_url_meta(&g_app.addons[i], type, id, url, sizeof url);
        snprintf(g_details_pending, sizeof g_details_pending, "%s", key);
        http_request_async(url, NULL, on_details, strdup(key));
        return;
    }
}

/* ---- preferences & perf ----------------------------------------------------- */

/* ---- tidying the data folder ------------------------------------------------- */

static SDL_atomic_t g_tidying;
typedef struct { char keep[TIDY_KEEP_MAX][64]; int n, delay_ms, clear; } TidyJob;
static SDL_SpinLock g_usage_lock;
static TidyUsage g_usage = { -1, -1, -1, -1 };
static TidyReport g_cleared;
static int g_cleared_ready;   /* 1 asked for, 2 done by itself: the TV was low on storage */
#define LOW_DISK (600LL << 20)   /* less free than this on the TV's drive: the cache goes by itself */

void app_storage(long long *total, long long *cache, long long *disk_free)
{
    SDL_AtomicLock(&g_usage_lock);
    *total = g_usage.total; *cache = g_usage.cache; *disk_free = g_usage.disk_free;
    SDL_AtomicUnlock(&g_usage_lock);
}

/* Measured on its own once a minute (the tidy's look comes every half hour): room made
 * on the TV counts soon. Low here is the app's drive nearly out of room (150 MB): a TV
 * gives its apps a small drive that often sits under the cache's own limit (600 MB), and
 * what holds back on it (Depth's maps, a few MB) must not hold back for good. */
#define VERY_LOW_DISK (150LL << 20)
int app_storage_low(void)
{
    if (getenv("SNTV_FAKE_LOW_DISK")) return 1;
    static float looked = -100; static int low;
    if (g_app.time - looked < 60 && g_app.time >= looked) return low;
    looked = g_app.time;
    const char *fake = getenv("SNTV_FAKE_DISK_FREE_MB");   /* tests: the runner's own drive must not decide */
    struct statvfs vs;
    long long free_b = fake ? atoll(fake) << 20 : statvfs(store_dir(), &vs) == 0 ? (long long)vs.f_bavail * (long long)vs.f_frsize : -1;
    low = free_b >= 0 && free_b < VERY_LOW_DISK;
    if (low) LOGI("storage: %lld MB free on the app's drive", free_b >> 20);
    return low;
}

/* Each frame: the word from a clearing, and every half hour a look at the storage. */
static void storage_watch(float dt)
{
    static float since = 0;
    SDL_AtomicLock(&g_usage_lock);
    int told = g_cleared_ready;
    TidyReport r = g_cleared;
    g_cleared_ready = 0;
    SDL_AtomicUnlock(&g_usage_lock);
    long long mb = (r.freed + (1 << 19)) >> 20;
    if (told == 1) app_toast("Cache cleared: %lld MB freed", mb);
    if (told == 2) app_toast("The TV was low on storage: " SNTV_APP_NAME " cleared its cache (%lld MB) to make room", mb);
    if ((since += dt) > 1800) { since = 0; app_tidy(0); }
}

static int tidy_main(void *arg)
{
    TidyJob *j = arg;
    SDL_Delay((Uint32)j->delay_ms);
    const char *keep[TIDY_KEEP_MAX];
    for (int i = 0; i < j->n; i++) keep[i] = j->keep[i];
    TidyReport r;
    if (j->clear) {
        tidy_clear(store_dir(), &r);
        LOGI("tidy: cache cleared, %d file%s, %lld KB", r.removed, r.removed == 1 ? "" : "s", r.freed >> 10);
    } else {
        tidy_run(store_dir(), keep, j->n, time(NULL), &r);
        if (r.removed) LOGI("tidy: removed %d old file%s, %lld KB", r.removed, r.removed == 1 ? "" : "s", r.freed >> 10);
    }
    TidyUsage u;
    tidy_usage(store_dir(), &u);
    const char *min_env = getenv("SNTV_AUTO_CLEAR_MIN_MB");   /* tests */
    long long worth = (min_env ? atoll(min_env) : 64) << 20;
    int low_disk = !j->clear && u.disk_free >= 0 && (u.disk_free < LOW_DISK || getenv("SNTV_FAKE_LOW_DISK"));
    int low = low_disk && u.cache > worth;   /* a small cache would not make the room, and goes again at each start */
    if (low_disk && !low) LOGI("storage: only %lld MB free on the TV; the cache (%lld MB) is too small to make a difference, so it stays", u.disk_free >> 20, u.cache >> 20);
    if (low) {   /* the TV needs the room (an app to install or update): the cache goes */
        LOGW("storage: only %lld MB free on the TV; clearing the cache (%lld MB)", u.disk_free >> 20, u.cache >> 20);
        tidy_clear(store_dir(), &r);
        tidy_usage(store_dir(), &u);
    }
    LOGI("storage: %lld MB here, %lld MB of it cache, %lld MB free on the drive", u.total >> 20, u.cache >> 20, u.disk_free >> 20);
    SDL_AtomicLock(&g_usage_lock);
    g_usage = u;
    if (j->clear || low) { g_cleared = r; g_cleared_ready = low ? 2 : 1; }
    SDL_AtomicUnlock(&g_usage_lock);
    free(j);
    SDL_AtomicSet(&g_tidying, 0);
    return 0;
}

/* The recordings in use, named as they are stored in sounds/. */
static void keep_recording(TidyJob *j, const char *stored)
{
    char id[16];
    if (!stored || j->n >= TIDY_KEEP_MAX) return;
    if (!strncmp(stored, "sntv-sound://", 13)) snprintf(j->keep[j->n++], sizeof j->keep[0], "%.60s", stored + 13);
    else if (youtube_video_id(stored, id)) snprintf(j->keep[j->n++], sizeof j->keep[0], "yt-%s-", id);
}

void app_clear_cache(void)
{
    if (SDL_AtomicGet(&g_tidying)) { app_toast("Tidying up already: try again in a moment"); return; }
    app_tidy(-1);
}

void app_tidy(int delay_ms)   /* -1: clear the cache instead */
{
    if (!SDL_AtomicCAS(&g_tidying, 0, 1)) return;
    TidyJob *j = calloc(1, sizeof *j);
    j->delay_ms = delay_ms;
    keep_recording(j, g_app.break_audio);
    keep_recording(j, g_app.break_end);
    j->clear = delay_ms < 0;
    if (j->delay_ms < 0) j->delay_ms = 0;
    SDL_Thread *t = SDL_CreateThread(tidy_main, "tidy", j);
    if (t) SDL_DetachThread(t);
    else { free(j); SDL_AtomicSet(&g_tidying, 0); }
}

/* ---- TV profile ------------------------------------------------------------- */

/* Which video decoders the system lends to apps, for Diagnostics. Waits until start-up is over. */
static int probe_decoders(void *unused)
{
    (void)unused;
    SDL_Delay(8000);
    sysinfo_probe_decoders();
    LOGI("decoders: %s", sysinfo_decoders());
    return 0;
}

static void tv_profile_load(void)
{
    char rel[200]; const char *src;
    sysinfo_webos_release(rel, sizeof rel, &src);
    quality_tv_init(&g_app.tv, src[0] ? rel : NULL);
    LOGI("system: %s%s%s", rel, src[0] ? " from " : " (no webOS release found)", src);
    SDL_Thread *t = SDL_CreateThread(probe_decoders, "probe", NULL);
    if (t) SDL_DetachThread(t);
    const cJSON *caps = store_get_json("tvCaps");
    for (int c = 0; c < TVCAP_COUNT; c++) {
        const cJSON *e = cJSON_GetObjectItemCaseSensitive(caps, TVCAP_NAMES[c]);
        const cJSON *ok = cJSON_GetArrayItem(e, 0), *fail = cJSON_GetArrayItem(e, 1);
        if (cJSON_IsNumber(ok)) g_app.tv.ok[c] = ok->valueint;
        if (cJSON_IsNumber(fail)) g_app.tv.fail[c] = fail->valueint;
    }
    LOGI("tv profile: webOS %d; dts %s, truehd %s, av1 %s, dv %s", g_app.tv.webos_major,
         quality_cap_ok(&g_app.tv, TVCAP_DTS) ? "ok" : "avoid", quality_cap_ok(&g_app.tv, TVCAP_TRUEHD) ? "ok" : "avoid",
         quality_cap_ok(&g_app.tv, TVCAP_AV1) ? "ok" : "avoid", quality_cap_ok(&g_app.tv, TVCAP_DV) ? "ok" : "avoid");
}

void app_tv_learn(unsigned needs, int worked)
{
    if (!needs) return;
    char names[80] = "";
    for (int c = 0; c < TVCAP_COUNT; c++) if (needs >> c & 1) { size_t l = strlen(names); snprintf(names + l, sizeof names - l, "%s%s", l ? "," : "", TVCAP_NAMES[c]); }
    LOGI("tv profile: %s %s here", names, worked ? "plays" : "failed");
    quality_tv_learn(&g_app.tv, needs, worked);
    cJSON *caps = cJSON_CreateObject();
    for (int c = 0; c < TVCAP_COUNT; c++) {
        cJSON *pair = cJSON_AddArrayToObject(caps, TVCAP_NAMES[c]);
        cJSON_AddItemToArray(pair, cJSON_CreateNumber(g_app.tv.ok[c]));
        cJSON_AddItemToArray(pair, cJSON_CreateNumber(g_app.tv.fail[c]));
    }
    store_set_json("tvCaps", caps);
    store_save();
    char what[128] = "";
    for (int c = 0; c < TVCAP_COUNT; c++) if (needs & (1u << c)) snprintf(what + strlen(what), sizeof what - strlen(what), "%s%s", what[0] ? "," : "", TVCAP_NAMES[c]);
    LOGI("tv profile: %s %s", what, worked ? "played fine" : "failed");
}

static void load_prefs(void)
{
    g_app.fx_setting = (FxLevel)store_get_int("fx", FX_AUTO);
    g_app.fx_light = g_app.fx_setting == FX_LIGHT;
    g_app.spoiler_blur = store_get_int("spoilerBlur", 1);
    g_app.advisory = store_get_int("advisory", 1);
    int lb_was = store_get_int("livingLetterbox", -1);   /* -1: never set (it was Glow then); 3: the old "Black" (bars black, subtitles in them) */
    g_app.living_letterbox = lb_was == LB_GLOW || lb_was == LB_COLOUR ? lb_was : LB_OFF;
    g_app.subs_in_bars = store_get_int("subsInBars", lb_was != LB_OFF);   /* every style but Off put them there */
    if (lb_was == 3) { store_set_int("livingLetterbox", LB_OFF); store_set_int("subsInBars", g_app.subs_in_bars); }   /* the old "Black": the letterbox off, its subtitles kept in the bars */
    if (!store_get_int("letterboxOffOnce", 0)) {   /* 0.10.79: off for everyone once (it lags on TVs); whoever wants it turns it on again */
        if (g_app.living_letterbox != LB_OFF) LOGI("settings: the living letterbox is off now (it was %s); Settings turns it on again", LB_STYLE_NAMES[g_app.living_letterbox]);
        g_app.living_letterbox = LB_OFF;
        store_set_int("livingLetterbox", LB_OFF);
        store_set_int("letterboxOffOnce", 1);
    }
    g_app.depth_motion = store_get_int("depthMotion", 1);
    g_app.subs_size = store_get_int("subsSize", 1);
    g_app.subs_pos = store_get_int("subsPos", 0);
    g_app.subs_style = store_get_int("subsStyle", 0);
    if (g_app.subs_style < 0 || g_app.subs_style >= SUBS_STYLE_COUNT) g_app.subs_style = 0;
    g_app.subs_colour = store_get_int("subsColour", 0);
    if (g_app.subs_colour < 0 || g_app.subs_colour >= SUBS_COLOUR_COUNT) g_app.subs_colour = 0;
    g_app.autoplay_next = store_get_int("autoplayNext", 1);
    g_app.night_mode = store_get_int("nightMode", 0);
    if (g_app.night_mode < 0 || g_app.night_mode > 2) g_app.night_mode = 0;
    g_app.skip_mode = store_get_int("skipMode", 0);
    if (g_app.skip_mode < 0 || g_app.skip_mode > 2) g_app.skip_mode = 0;
    g_app.subs_always = store_get_int("subsAlways", 0);
    g_app.clock_mode = store_get_int("clockMode", 0);
    if (g_app.clock_mode < 0 || g_app.clock_mode > 2) g_app.clock_mode = 0;
    g_app.island_style = store_get_int("islandStyle", ISLAND_TIME);
    if (g_app.island_style < 0 || g_app.island_style >= ISLAND_STYLE_COUNT) g_app.island_style = ISLAND_TIME;
    g_app.clock_face = store_get_int("clockFace", CLOCKFACE_DIGITAL);
    if (g_app.clock_face < 0 || g_app.clock_face >= CLOCKFACE_COUNT) g_app.clock_face = CLOCKFACE_DIGITAL;
    g_app.oled = store_get_int("oled", 0);
    g_app.theme = store_get_int("theme", THEME_STREAM);
    if (g_app.theme < 0 || g_app.theme >= THEME_COUNT) g_app.theme = THEME_STREAM;
    g_app.theme_accent = store_get_int("themeAccent", 0);
    if (g_app.theme_accent < 0 || g_app.theme_accent > THEME_ACCENT_ART) g_app.theme_accent = 0;
    g_app.theme_bg = store_get_int("themeBg", 0);
    if (g_app.theme_bg < 0 || g_app.theme_bg >= THEME_BGS) g_app.theme_bg = 0;
    theme_apply(g_app.theme, g_app.theme_accent, g_app.theme_bg);
    g_app.card_details = store_get_int("cardDetails", 1);
    g_app.auto_trailers = store_get_int("autoTrailers", 1);
    g_app.ambient_minutes = store_get_int("ambientMinutes", 5);
    g_app.ambient_style = store_get_int("ambientStyle", AMBIENT_ARTWORK);
    g_app.ambient_dim_min = store_get_int("ambientDimMinV2", 0);   /* never dims unless asked; the old key held a 30 nobody chose */
    g_app.ambient_off_min = store_get_int("ambientOffMin", 0);
    if (g_app.ambient_style < 0 || g_app.ambient_style >= AMBIENT_STYLE_COUNT) g_app.ambient_style = AMBIENT_ARTWORK;
    g_app.quality_mode = store_get_int("qualityMode", QUAL_ADDON_ORDER);
    if (g_app.quality_mode < 0 || g_app.quality_mode >= QUAL_MODE_COUNT) g_app.quality_mode = QUAL_ADDON_ORDER;
    g_app.res_cap = store_get_int("resCap", 2160);
    g_app.auto_recommended = store_get_int("autoRecommended", 0);
    g_app.src_max_gb = store_get_int("srcMaxGb", 0);
    g_app.src_hdr = store_get_int("srcHdr", 0);
    g_app.src_cached_only = store_get_int("srcCachedOnly", 0);
    snprintf(g_app.src_exclude, sizeof g_app.src_exclude, "%s", store_get_str("srcExclude", ""));
    g_app.preflight = store_get_int("preflight", 1);
    g_app.live_scrub = store_get_int("liveScrub", 1);
    g_app.home_style = store_get_int("homeStyle", HOME_CINEMATIC);
    g_app.hide_watched = store_get_int("hideWatched", 0);
    snprintf(g_app.parental_pin, sizeof g_app.parental_pin, "%.7s", store_get_str("parentalPin", ""));
    g_app.pin_settings = store_get_int("pinSettings", 0);
    g_app.ask_profile = store_get_int("askProfile", 1);
    g_app.hide_adult = store_get_int("hideAdult", 0);
    { const char *t = store_get_str("thumbServer", NULL); free(g_app.thumb_server); g_app.thumb_server = t && *t ? strdup(t) : NULL; }
    tmdb_key_set(store_get_str("tmdbKey", NULL));
    g_app.ai_provider = store_get_int("aiProvider", 0);
    { const char *t = store_get_str("aiKey", NULL); free(g_app.ai_key); g_app.ai_key = t && *t ? strdup(t) : NULL; }
    { const char *t = store_get_str("aiBase", NULL); free(g_app.ai_base); g_app.ai_base = t && *t ? strdup(t) : NULL; }
    { const char *t = store_get_str("aiModel", NULL); free(g_app.ai_model); g_app.ai_model = t && *t ? strdup(t) : NULL; }
    g_app.memory_mode = store_get_int("memoryMode", 0);
    g_app.breaks_on = store_get_int("breaksOn", 0);
    g_app.break_sched = store_get_int("breakSched", 0);
    { int old = store_get_int("breakMethod", 0);   /* before Auto, 0 was just the default */
      g_app.break_method = store_get_int("breakMethodV2", old ? old : -1); }
    snprintf(g_app.break_country, sizeof g_app.break_country, "%s", store_get_str("breakCountry", ""));
    g_app.break_asr_hanafi = store_get_int("breakAsr", 0);
    g_app.break_lead_min = store_get_int("breakLead", 5);
    g_app.break_len_min = store_get_int("breakLen", 10);
    g_app.break_sound = store_get_int("breakSound", 0);
    /* an old or hand-edited settings file must not index past a list */
#define CLAMP_PREF(v, n, def) do { if ((v) < 0 || (v) >= (n)) (v) = (def); } while (0)
    CLAMP_PREF(g_app.fx_setting, 3, FX_AUTO);
    CLAMP_PREF(g_app.subs_size, 4, 1);
    CLAMP_PREF(g_app.home_style, HOME_STYLE_COUNT, HOME_CINEMATIC);
    CLAMP_PREF(g_app.ai_provider, AI_PROVIDER_COUNT, 0);
    CLAMP_PREF(g_app.memory_mode, 3, 0);
    CLAMP_PREF(g_app.break_sched, BRK_SCHED_COUNT, 0);
    CLAMP_PREF(g_app.break_sound, 3, 0);
#undef CLAMP_PREF
    g_app.break_health_min = store_get_int("breakHealth", 0);
    g_app.break_health_len = store_get_int("breakHealthLen", 3);
    g_app.break_bed = store_get_int("breakBed", -1);
    g_app.break_custom[0] = store_get_int("breakCustom1", -1);
    g_app.break_custom[1] = store_get_int("breakCustom2", -1);
    g_app.break_custom[2] = store_get_int("breakCustom3", -1);
    g_app.breaks_everywhere = store_get_int("breaksEverywhere", 0);
    g_app.break_lat = atof(store_get_str("breakLat", "0"));
    g_app.break_lon = atof(store_get_str("breakLon", "0"));
    snprintf(g_app.break_city, sizeof g_app.break_city, "%s", store_get_str("breakCity", ""));
    { const char *t = store_get_str("breakAudio", NULL); free(g_app.break_audio); g_app.break_audio = t && *t ? strdup(t) : NULL; }
    { const char *t = store_get_str("breakEnd", NULL); free(g_app.break_end); g_app.break_end = t && *t ? strdup(t) : NULL; }
    g_app.transitions = store_get_int("transitions", 1);
    g_app.pip_enabled = store_get_int("pipEnabled", 0);
    g_app.subs_autosync = store_get_int("subsAutosync", 1);
    g_app.subs_prefer_files = store_get_int("subsPreferFiles", 0);
    g_app.subs_autotranslate = store_get_int("subsAutotranslate", 0);
    g_app.addon_wait = store_get_int("addonWait", 8);
    g_app.break_video = store_get_int("breakVideo", 1);
    g_app.src_audio_lang = store_get_int("srcAudioLang", 1);
    g_app.p2p_fallback = store_get_int("p2pFallback", 1);
    g_app.auto_update = store_get_int("autoUpdate", 1);
    dead_load();
    g_app.low_memory = g_app.memory_mode == 1 || (g_app.memory_mode == 0 && mem_small_tv());   /* main.c calls mem_apply_mode() once images exist */
    torrent_engine_set_cache_mb(g_app.low_memory ? 96 : 192);   /* the P2P engine's RAM for pieces */
    if (g_app.home_style < 0 || g_app.home_style >= HOME_STYLE_COUNT) g_app.home_style = HOME_CINEMATIC;
    tv_profile_load();
    app_tidy(getenv("SNTV_TIDY_DELAY") ? atoi(getenv("SNTV_TIDY_DELAY")) : 12000);   /* after start-up, off the main thread */
    snprintf(g_app.pref_audio, sizeof g_app.pref_audio, "%s", store_get_str("prefAudio", ""));
    snprintf(g_app.pref_subs, sizeof g_app.pref_subs, "%s", store_get_str("prefSubs", ""));
}

void app_save_prefs(void)
{
    store_set_int("fx", g_app.fx_setting);
    store_set_int("spoilerBlur", g_app.spoiler_blur);
    store_set_int("advisory", g_app.advisory);
    store_set_int("livingLetterbox", g_app.living_letterbox);
    store_set_int("subsInBars", g_app.subs_in_bars);
    store_set_int("depthMotion", g_app.depth_motion);
    store_set_int("subsSize", g_app.subs_size);
    store_set_int("subsPos", g_app.subs_pos);
    store_set_int("subsStyle", g_app.subs_style);
    store_set_int("subsColour", g_app.subs_colour);
    store_set_int("autoplayNext", g_app.autoplay_next);
    store_set_int("skipMode", g_app.skip_mode);
    store_set_int("nightMode", g_app.night_mode);
    store_set_int("subsAlways", g_app.subs_always);
    store_set_int("clockMode", g_app.clock_mode);
    store_set_int("islandStyle", g_app.island_style);
    store_set_int("clockFace", g_app.clock_face);
    store_set_int("oled", g_app.oled);
    store_set_int("theme", g_app.theme);
    store_set_int("themeAccent", g_app.theme_accent);
    store_set_int("themeBg", g_app.theme_bg);
    store_set_int("cardDetails", g_app.card_details);
    store_set_int("autoTrailers", g_app.auto_trailers);
    store_set_int("ambientMinutes", g_app.ambient_minutes);
    store_set_int("ambientStyle", g_app.ambient_style);
    store_set_int("ambientDimMinV2", g_app.ambient_dim_min);
    store_set_int("ambientOffMin", g_app.ambient_off_min);
    store_set_int("qualityMode", g_app.quality_mode);
    store_set_int("resCap", g_app.res_cap);
    store_set_int("autoRecommended", g_app.auto_recommended);
    store_set_int("srcMaxGb", g_app.src_max_gb);
    store_set_int("srcHdr", g_app.src_hdr);
    store_set_int("srcCachedOnly", g_app.src_cached_only);
    store_set_str("srcExclude", g_app.src_exclude[0] ? g_app.src_exclude : NULL);
    store_set_int("preflight", g_app.preflight);
    store_set_int("liveScrub", g_app.live_scrub);
    store_set_int("homeStyle", g_app.home_style);
    store_set_int("hideWatched", g_app.hide_watched);
    store_set_int("pinSettings", g_app.pin_settings);
    store_set_int("askProfile", g_app.ask_profile);
    store_set_int("hideAdult", g_app.hide_adult);
    store_set_str("thumbServer", g_app.thumb_server && *g_app.thumb_server ? g_app.thumb_server : NULL);
    store_set_str("tmdbKey", g_app.tmdb_key && *g_app.tmdb_key ? g_app.tmdb_key : NULL);
    store_set_int("aiProvider", g_app.ai_provider);
    store_set_str("aiKey", g_app.ai_key && *g_app.ai_key ? g_app.ai_key : NULL);
    store_set_str("aiBase", g_app.ai_base && *g_app.ai_base ? g_app.ai_base : NULL);
    store_set_str("aiModel", g_app.ai_model && *g_app.ai_model ? g_app.ai_model : NULL);
    store_set_int("memoryMode", g_app.memory_mode);
    store_set_int("breaksOn", g_app.breaks_on);
    store_set_int("breakSched", g_app.break_sched);
    if (g_app.break_method < -1 || g_app.break_method >= SUN_METHOD_COUNT) g_app.break_method = -1;
    store_set_int("breakMethodV2", g_app.break_method);
    store_set_str("breakCountry", g_app.break_country[0] ? g_app.break_country : NULL);
    store_set_int("breakAsr", g_app.break_asr_hanafi);
    store_set_int("breakLead", g_app.break_lead_min);
    store_set_int("breakLen", g_app.break_len_min);
    store_set_int("breakSound", g_app.break_sound);
    store_set_int("breakHealth", g_app.break_health_min);
    store_set_int("breakHealthLen", g_app.break_health_len);
    store_set_int("breakBed", g_app.break_bed);
    store_set_int("breakCustom1", g_app.break_custom[0]);
    store_set_int("breakCustom2", g_app.break_custom[1]);
    store_set_int("breakCustom3", g_app.break_custom[2]);
    store_set_int("breaksEverywhere", g_app.breaks_everywhere);
    { char b[32]; snprintf(b, sizeof b, "%.5f", g_app.break_lat); store_set_str("breakLat", b); snprintf(b, sizeof b, "%.5f", g_app.break_lon); store_set_str("breakLon", b); }
    store_set_str("breakCity", g_app.break_city[0] ? g_app.break_city : NULL);
    store_set_str("breakAudio", g_app.break_audio && *g_app.break_audio ? g_app.break_audio : NULL);
    store_set_str("breakEnd", g_app.break_end && *g_app.break_end ? g_app.break_end : NULL);
    store_set_int("transitions", g_app.transitions);
    store_set_int("pipEnabled", g_app.pip_enabled);
    store_set_int("subsAutosync", g_app.subs_autosync);
    store_set_int("subsPreferFiles", g_app.subs_prefer_files);
    store_set_int("subsAutotranslate", g_app.subs_autotranslate);
    store_set_int("addonWait", g_app.addon_wait);
    store_set_int("breakVideo", g_app.break_video);
    store_set_int("srcAudioLang", g_app.src_audio_lang);
    store_set_int("p2pFallback", g_app.p2p_fallback);
    store_set_int("autoUpdate", g_app.auto_update);
    store_set_str("prefAudio", g_app.pref_audio);
    store_set_str("prefSubs", g_app.pref_subs);
    store_save();
    g_app.fx_light = g_app.fx_setting == FX_LIGHT || (g_app.fx_setting == FX_AUTO && store_get_int("fxAutoLight", 0));
}

/* Auto effects, all along: drawn frames averaged in windows of 120. Two slow windows in a
 * row (over 25 ms: the TV can't hold ~40 fps) drop to light effects; light ones that keep up
 * with the screen, for longer, bring full effects back (a frame includes the wait for the
 * display, so one that keeps up is ~17 ms at 60 Hz, never less: "keeps up" is hardly any
 * frame missing a refresh). The gap between the two, and a longer
 * wait after a return that failed, keep it from flipping back and forth; a change waits
 * for a pause in the keys so it never lands mid-scroll. The player and the screensaver
 * draw video and are not counted. */
static int fx_measure_window(void)
{
    static int w;
    if (!w) { const char *e = getenv("SNTV_FX_WINDOW"); w = e && atoi(e) > 0 ? atoi(e) : 120; }   /* tests: shorter */
    return w;
}

static float fake_frame_ms(float ms)   /* tests: SNTV_FAKE_FRAME_MS="40x200,6" (40 ms for 200 frames, then 6) */
{
    static int parsed, first_n; static float first = -1, then = -1; static int seen;
    if (!parsed) {
        parsed = 1;
        const char *e = getenv("SNTV_FAKE_FRAME_MS");
        if (e && *e) { first = (float)atof(e); const char *x = strchr(e, 'x'); first_n = x ? atoi(x + 1) : 1 << 30; const char *c = strchr(e, ','); then = c ? (float)atof(c + 1) : first; }
    }
    if (first < 0) return ms;
    return seen++ < first_n ? first : then;
}

void app_note_frame(float ms)
{
    static int n, late, slow_runs, fast_runs, need_fast = 4, want = -1, restored;
    static float sum, last_avg;
    static int adapt = -1;
    if (adapt < 0) { const char *e = getenv("SNTV_FX_ADAPT"); adapt = !(e && *e == '0'); }   /* tests: off, so a busy test machine does not change what a run sees */
    if (g_app.fx_setting != FX_AUTO || !adapt) { n = late = 0; sum = 0; slow_runs = fast_runs = 0; want = -1; return; }
    if (g_app.time < 3.0f) return;   /* skip startup */
    Screen *top = app_top();
    if (top && (!strcmp(top->name, "player") || !strcmp(top->name, "ambient"))) return;
    ms = fake_frame_ms(ms);
    g_app.frames_measured++;
    g_app.frame_ms_sum += ms;
    if (ms > g_app.frame_ms_worst) g_app.frame_ms_worst = ms;
    sum += ms;
    late += ms > 25.0f;   /* past a second refresh at 60 Hz */
    if (++n >= fx_measure_window()) {
        last_avg = sum / n;
        int keeps_up = late * 20 <= n && last_avg < 20.0f;   /* 1 in 20 at most missed a refresh */
        n = 0; sum = 0; late = 0;
        if (!g_app.fx_light) {
            fast_runs = 0;
            slow_runs = last_avg > 25.0f ? slow_runs + 1 : 0;
            if (slow_runs >= 2) want = 1;
        } else {
            slow_runs = 0;
            fast_runs = keeps_up ? fast_runs + 1 : 0;
            if (fast_runs >= need_fast && mem_stage() < 2) want = 0;   /* not while memory holds it light */
        }
    }
    if (want < 0 || want == g_app.fx_light || g_app.time - g_app.last_input < 1.5f) return;
    if (want) {
        if (restored && need_fast < 64) need_fast *= 2;   /* full effects did not last: longer before the next try */
        LOGI("perf: frames slow (%.1f ms average, twice in a row) -> light effects", last_avg);
    } else {
        restored = 1;
        LOGI("perf: light effects keep up with the screen (%.1f ms average, %d times) -> full effects", last_avg, fast_runs);
    }
    g_app.fx_light = want;
    want = -1; slow_runs = fast_runs = 0;
    if (g_app.fx_light != store_get_int("fxAutoLight", 0)) { store_set_int("fxAutoLight", g_app.fx_light); store_save(); }
    app_redraw();
}

/* The episode lists of the last few titles opened: the one playing (in the corner too)
 * keeps its own while another title page is browsed. */
typedef struct { char *meta_id; MetaVideo *videos; int n; unsigned used; } VideoList;
static VideoList g_vlists[4];
static unsigned g_vlist_clock;

static const VideoList *video_list(const char *meta_id)
{
    for (int i = 0; meta_id && i < 4; i++) if (g_vlists[i].meta_id && !strcmp(g_vlists[i].meta_id, meta_id)) return &g_vlists[i];
    return NULL;
}

void app_remember_videos(const char *meta_id, const MetaVideo *videos, int n)
{
    VideoList *l = (VideoList *)video_list(meta_id);
    if (!l) { l = &g_vlists[0]; for (int i = 1; i < 4; i++) if (g_vlists[i].used < l->used) l = &g_vlists[i]; }   /* the least recently used goes */
    free(l->meta_id);
    for (int i = 0; i < l->n; i++) { free(l->videos[i].id); free(l->videos[i].title); free(l->videos[i].thumbnail); free(l->videos[i].overview); }
    free(l->videos);
    l->meta_id = strdup(meta_id);
    l->videos = calloc((size_t)n + 1, sizeof(MetaVideo));
    l->n = n;
    l->used = ++g_vlist_clock;
    for (int i = 0; i < n; i++) {
        l->videos[i].id = strdup(videos[i].id);
        l->videos[i].title = videos[i].title ? strdup(videos[i].title) : NULL;
        l->videos[i].thumbnail = videos[i].thumbnail ? strdup(videos[i].thumbnail) : NULL;
        l->videos[i].overview = videos[i].overview ? strdup(videos[i].overview) : NULL;   /* the player's "Previously" card */
        l->videos[i].season = videos[i].season;
        l->videos[i].episode = videos[i].episode;
    }
}

const char *app_video_thumbnail(const char *meta_id, const char *video_id)
{
    const VideoList *l = video_list(meta_id);
    for (int i = 0; l && video_id && i < l->n; i++) if (strcmp(l->videos[i].id, video_id) == 0) return l->videos[i].thumbnail;
    return NULL;
}

/* Next = the following entry in release order within regular seasons (specials skipped). */
const MetaVideo *app_videos(const char *meta_id, int *n)
{
    const VideoList *l = video_list(meta_id);
    *n = l ? l->n : 0;
    return l ? l->videos : NULL;
}

void app_episode_hint(const char *meta_id, const char *show, const char *video_id, EpisodeHint *h)
{
    memset(h, 0, sizeof *h);
    /* the last two fields: "tt1:2:3" and "kitsu:12345:5" (no season: 1), "tmdb:99:2:3" */
    const char *last = video_id ? strrchr(video_id, ':') : NULL, *prev = last;
    while (prev && prev > video_id && prev[-1] != ':') prev--;
    int n1 = 0, n2 = 0;
    int colons = 0;
    for (const char *c = video_id ? video_id : ""; *c; c++) colons += *c == ':';
    if (!last || colons < 2 || sscanf(last + 1, "%d", &n2) != 1 || n2 <= 0) return;   /* "kitsu:12345" is a film */
    if (prev > video_id + 1 && sscanf(prev, "%d", &n1) == 1 && prev[-1] == ':' && strncmp(video_id, "kitsu:", 6) != 0) { h->season = n1; h->episode = n2; }
    else { h->season = strncmp(video_id, "kitsu:", 6) ? 1 : 0; h->episode = n2; }   /* kitsu: an id per season, numbers restart: season unknown */
    snprintf(h->show, sizeof h->show, "%s", show ? show : "");
    int n;
    const MetaVideo *v = app_videos(meta_id, &n);
    int before = 0, found = 0;
    for (int i = 0; i < n; i++) {
        if (v[i].season < 1 || v[i].episode < 1) continue;   /* specials do not count */
        if (v[i].season < h->season || (v[i].season == h->season && v[i].episode < h->episode)) before++;
        if (v[i].season == h->season && v[i].episode == h->episode) {
            found = 1;
            snprintf(h->title, sizeof h->title, "%s", v[i].title ? v[i].title : "");
        }
    }
    if (found && h->season > 1) h->absolute = before + 1;   /* in season 1 it is the episode itself */
}

int app_adjacent_video(const char *meta_id, const char *video_id, int dir, char *out_id, size_t id_n, char *out_title, size_t title_n)
{
    const VideoList *l = video_list(meta_id);
    if (!l || !video_id) return 0;
    int cur = -1;
    for (int i = 0; i < l->n; i++) if (strcmp(l->videos[i].id, video_id) == 0) { cur = i; break; }
    if (cur < 0) return 0;
    const MetaVideo *c = &l->videos[cur];
    const MetaVideo *best = NULL;
    for (int i = 0; i < l->n; i++) {
        const MetaVideo *v = &l->videos[i];
        if (v == c || v->season == 0) continue;   /* specials never count */
        int after = v->season > c->season || (v->season == c->season && v->episode > c->episode);
        int before = v->season < c->season || (v->season == c->season && v->episode < c->episode);
        if (c->season < 0) { after = i > cur; before = i < cur; }   /* no season info: list order */
        if (dir > 0 ? !after : !before) continue;
        int closer = !best || (dir > 0 ? (v->season < best->season || (v->season == best->season && v->episode < best->episode))
                                       : (v->season > best->season || (v->season == best->season && v->episode > best->episode)));
        if (closer) best = v;
    }
    if (!best) return 0;
    snprintf(out_id, id_n, "%s", best->id);
    if (best->season >= 0 && best->episode >= 0) snprintf(out_title, title_n, "S%d E%d - %s", best->season, best->episode, best->title ? best->title : "");
    else snprintf(out_title, title_n, "%s", best->title ? best->title : (dir > 0 ? "Next" : "Previous"));
    return 1;
}

int app_next_video(const char *meta_id, const char *video_id, char *next_id, size_t id_n, char *next_title, size_t title_n)
{
    return app_adjacent_video(meta_id, video_id, 1, next_id, id_n, next_title, title_n);
}

static int imdb_id(const char *id) { return id && id[0] == 't' && id[1] == 't' && id[2] >= '0' && id[2] <= '9'; }

/* ---- screensaver pool ------------------------------------------------------- */

enum { AMBIENT_MAX = 48 };
static AmbientItem g_ambient[AMBIENT_MAX];
static int g_n_ambient, g_ambient_next;

void app_ambient_offer(const MetaPreview *m)
{
    char buf[512];
    const char *bg = app_art_background(m, buf, sizeof buf);
    if (!bg || !m->id || !m->name) return;
    for (int i = 0; i < g_n_ambient; i++) if (strcmp(g_ambient[i].id, m->id) == 0) return;
    AmbientItem *a;
    if (g_n_ambient < AMBIENT_MAX) a = &g_ambient[g_n_ambient++];
    else { a = &g_ambient[g_ambient_next]; g_ambient_next = (g_ambient_next + 1) % AMBIENT_MAX; }   /* rotate the pool */
    snprintf(a->id, sizeof a->id, "%s", m->id);
    snprintf(a->name, sizeof a->name, "%s", m->name);
    snprintf(a->type, sizeof a->type, "%s", m->type ? m->type : "");
    snprintf(a->background, sizeof a->background, "%s", bg);
    snprintf(a->poster, sizeof a->poster, "%s", m->poster ? m->poster : "");
}

int app_ambient_pool(const AmbientItem **out)
{
    *out = g_ambient;
    return g_n_ambient;
}

/* A title's own artwork when its addon gave none (or the TV could not show it): from TMDB
 * through the built-in catalog, else metahub (Stremio's image service) */
static const char *art_fallback(const MetaPreview *p, const char *kind, char *buf, size_t n)
{
    if (tmdb_enabled()) snprintf(buf, n, "cuelight://catalog/art/%s/%s", kind, p->id);
    else snprintf(buf, n, "https://images.metahub.space/%s/medium/%s/img", kind, p->id);
    return buf;
}

const char *app_art_background(const MetaPreview *p, char *buf, size_t n)
{
    if (p->background && *p->background && !(imdb_id(p->id) && image_failed(p->background))) return p->background;
    if (imdb_id(p->id)) return art_fallback(p, "background", buf, n);
    return p->poster;
}

const char *app_art_logo(const MetaPreview *p, char *buf, size_t n)
{
    if (p->logo && *p->logo) return p->logo;
    if (imdb_id(p->id)) return art_fallback(p, "logo", buf, n);
    return NULL;
}

const char *app_art_poster(const MetaPreview *p, char *buf, size_t n)
{
    /* a poster the TV cannot fetch or decode (WebP, dead link): TMDB (or metahub) has a JPEG for IMDb ids */
    if (p->poster && *p->poster && !(imdb_id(p->id) && image_failed(p->poster))) return p->poster;
    if (imdb_id(p->id)) return art_fallback(p, "poster", buf, n);
    return NULL;
}

void play_candidates_free(PlayCandidate *c, int n)
{
    for (int i = 0; i < n; i++) { free(c[i].url); free(c[i].label); free(c[i].binge_group); free(c[i].filename); free(c[i].detail); free(c[i].release);
                                  free(c[i].headers); free(c[i].video_hash); free(c[i].subs); }
    free(c);
}

/* ---- shared widgets -------------------------------------------------------- */

const char *const HOME_STYLE_NAMES[HOME_STYLE_COUNT] = { "Cinematic", "Showcase", "Compact" };
const char *const ISLAND_STYLE_NAMES[ISLAND_STYLE_COUNT] = { "Time", "Day and time", "With seconds", "Small dial" };
const char *const LB_STYLE_NAMES[LB_STYLE_COUNT] = { "Off", "Glow", "One colour" };
const char *const AMBIENT_STYLE_NAMES[AMBIENT_STYLE_COUNT] = { "Artwork", "Poster wall", "Clock", "Floating logo", "Fireplace", "Aurora",
                                                                   "Rainy window", "Now showing", "Warp", "Depth", "Art gallery", "Shuffle" };

void meta_preview_copy(MetaPreview *dst, const MetaPreview *src)
{
    memset(dst, 0, sizeof *dst);
#define D(f) dst->f = src->f ? strdup(src->f) : NULL
    D(id); D(type); D(name); D(poster); D(poster_shape); D(background); D(logo);
    D(description); D(release_info); D(imdb_rating); D(runtime);
#undef D
    if (src->genres.n) {
        dst->genres.v = calloc((size_t)src->genres.n + 1, sizeof(char *));
        for (int i = 0; i < src->genres.n; i++) dst->genres.v[dst->genres.n++] = strdup(src->genres.v[i]);
    }
    if (src->cast.n) {
        dst->cast.v = calloc((size_t)src->cast.n + 1, sizeof(char *));
        for (int i = 0; i < src->cast.n; i++) dst->cast.v[dst->cast.n++] = strdup(src->cast.v[i]);
    }
    if (src->cast_photos.n) {
        dst->cast_photos.v = calloc((size_t)src->cast_photos.n + 1, sizeof(char *));
        for (int i = 0; i < src->cast_photos.n; i++) dst->cast_photos.v[dst->cast_photos.n++] = strdup(src->cast_photos.v[i]);
    }
    if (src->cast_roles.n) {
        dst->cast_roles.v = calloc((size_t)src->cast_roles.n + 1, sizeof(char *));
        for (int i = 0; i < src->cast_roles.n; i++) dst->cast_roles.v[dst->cast_roles.n++] = strdup(src->cast_roles.v[i]);
    }
}

void ui_draw_thumb(const char *url, float x, float y, float w, float h, int blur, Color placeholder)
{
    (void)placeholder;
    ui_card(url, x, y, w, h, 0, 0, NULL, blur ? CARD_BLUR : CARD_MIP);
}

void ui_draw_poster(const char *url, float x, float y, float w, float h, int focused, const char *label)
{
    ui_card(url, x, y, w, h, focused ? 1.0f : 0.0f, 0, label, 0);
}

void ui_draw_header(const char *title, const char *subtitle)
{
    font_draw(title, THEME_MARGIN, 52, FONT_L | FONT_B, THEME_TEXT);
    if (subtitle) font_draw_fit(subtitle, THEME_MARGIN + font_measure(title, FONT_L | FONT_B) + 28, 64, FONT_S, THEME_TEXT_MUTED,
                                SNTV_UI_W - 2 * THEME_MARGIN - font_measure(title, FONT_L | FONT_B) - 28);
}

void ui_draw_spinner(float cx, float cy) { ui_spinner(cx, cy, 24); }

void ui_draw_button(const char *label, float x, float y, float w, float h, int focused, int accent)
{
    if (focused && !g_app.fx_light) gfx_shadow(x, y + 6, w, h, h / 2, 22, RGBA(0, 0, 0, 0x70));
    if (focused) ui_focus_under(x, y, w, h, theme_pill_r(h), 1);
    gfx_rrect(x, y, w, h, theme_pill_r(h), focused ? THEME_FOCUS : accent ? THEME_SURFACE_HI : THEME_SURFACE);
    int fs = FONT_S | FONT_B;
    float tw = font_measure(label, fs);
    float tx = x + (w - tw) / 2;
    if (tx < x + 24) tx = x + 24;
    font_draw_fit(label, tx, y + (h - font_height(fs)) / 2, fs, focused ? THEME_ON_FOCUS : THEME_TEXT, w - 48);
}

void ui_format_time(double s, char *out, size_t n)
{
    int t = (int)(s < 0 ? 0 : s);
    if (t >= 3600) snprintf(out, n, "%d:%02d:%02d", t / 3600, (t / 60) % 60, t % 60);
    else snprintf(out, n, "%d:%02d", t / 60, t % 60);
}

/* ---- lifecycle ------------------------------------------------------------- */

void app_init(void)
{
    memset(&g_app, 0, sizeof g_app);
    g_app.press_at = g_app.card_at = -1e9f;   /* no OK and no focused card yet: the first screens slide */
    const char *k = store_get_str("authKey", NULL);
    const char *e = store_get_str("email", NULL);
    const char *srv = store_get_str("streamingServer", NULL);
    g_app.auth_key = k ? strdup(k) : NULL;
    g_app.user_email = e ? strdup(e) : NULL;
    lists_set_owner(g_app.user_email);
    g_app.streaming_server = srv ? strdup(srv) : NULL;
    load_prefs();
    g_app.fx_light = g_app.fx_setting == FX_LIGHT || (g_app.fx_setting == FX_AUTO && store_get_int("fxAutoLight", 0));
    app_load_addons();
    app_load_library();
    app_push(screen_home_create());
    if (!store_get_int("setupDone", 0) && !getenv("SNTV_SKIP_SETUP")) app_push(screen_welcome_create());
    app_whatsnew_check();   /* after an update: what changed, once */
}

void app_shutdown(void)
{
    app_pip_close();
    while (g_app.depth) { Screen *s = g_app.stack[--g_app.depth]; if (s->destroy) s->destroy(s); }
    torrent_engine_stop();
    app_library_flush(1);   /* after the player had its last say */
    free_addons();
    library_items_free(g_app.library, g_app.n_library);
    free(g_app.auth_key); free(g_app.user_email); free(g_app.streaming_server);
}

/* ---- settings backup ---- */
static int join_json(cJSON *mine, const cJSON *theirs);
/* A Stremio sign-in that came in the store (from another device or a backup): this device
 * follows it, as signing in here would. The key now in use, or NULL when signed out. */
static const char *follow_signin(const char *old_key, int *switched_out)
{
    const char *key = store_get_str("authKey", NULL), *email = store_get_str("email", NULL);
    int switched = key && (!old_key || strcmp(old_key, key) != 0);
    if (switched_out) *switched_out = switched;
    if (switched) {
        app_sync_begin();   /* signed out before: this device's own addons join the account's */
        if (old_key) {   /* another account: its library, not the last one's (as switching profiles does) */
            library_items_free(g_app.library, g_app.n_library);
            g_app.library = NULL; g_app.n_library = 0;
            g_app.library_version++;
            store_write_file("library.json", "{\"result\":[]}", 13);
        }
        free(g_app.auth_key); free(g_app.user_email);
        g_app.auth_key = strdup(key);
        g_app.user_email = email ? strdup(email) : NULL;
        lists_set_owner(g_app.user_email);
        g_account_gen++;
        if (email) accounts_remember(email, key);
    } else lists_set_owner(g_app.user_email);
    return key;
}

static int backup_skips(const char *key)
{
    static const char *const SKIP[] = { "user", "signinsShared", "tvCaps", "setupDone", "seenVersion", NULL };
    for (int i = 0; SKIP[i]; i++) if (strcmp(key, SKIP[i]) == 0) return 1;
    return 0;
}

char *app_backup_settings(void)
{
    app_save_prefs();   /* every preference in the store, not only the ones changed so far */
    cJSON *root = store_root(), *out = cJSON_CreateObject(), *it;
    if (!out) return NULL;
    cJSON_AddStringToObject(out, "_backup", "cuelight");
    cJSON_AddStringToObject(out, "_version", SNTV_APP_VERSION);
    cJSON_ArrayForEach(it, root) if (it->string && !backup_skips(it->string)) cJSON_AddItemToObject(out, it->string, cJSON_Duplicate(it, 1));
    char *j = cJSON_PrintUnformatted(out);
    cJSON_Delete(out);
    return j;
}

int app_restore_settings(const char *json)
{
    cJSON *in = cJSON_Parse(json), *it;
    const cJSON *tag = in ? cJSON_GetObjectItemCaseSensitive(in, "_backup") : NULL;
    if (!cJSON_IsString(tag) || (strcmp(tag->valuestring, "cuelight") != 0 && strcmp(tag->valuestring, "stremio-native-tv") != 0)) { cJSON_Delete(in); return -1; }
    char *old_key = g_app.auth_key ? strdup(g_app.auth_key) : NULL;
    int n = 0;
    cJSON_ArrayForEach(it, in) {
        if (!it->string || it->string[0] == '_' || backup_skips(it->string)) continue;
        cJSON *mine = store_get_json(it->string);
        if (!strcmp(it->string, "accounts") && cJSON_IsArray(mine) && cJSON_IsArray(it)) join_json(mine, it);   /* this TV's other accounts stay */
        else store_set_json(it->string, cJSON_Duplicate(it, 1));
        n++;
    }
    int signin = cJSON_GetObjectItemCaseSensitive(in, "authKey") != NULL;
    cJSON_Delete(in);
    store_save();
    load_prefs();
    if (signin && follow_signin(old_key, NULL)) {
        store_set_int("signinsShared", 1);   /* the device that saved it may still use that session */
        store_save();
        app_load_addons();
        app_load_library();
    }
    free(old_key);
    LOGI("settings: restored %d from a backup", n);
    return n;
}

/* ---- syncing with another device (Settings > Accounts > Sync with another device) ----
 * A device's bundle: its sign-ins, addons, library and personal data (lists, ratings,
 * marks), and apart from those its look and choices. Taking one in loses nothing here:
 * addons and data are joined, and each title keeps the newer of the two progresses.
 * With `with_look` its look and choices come too, and its sign-ins win where both have one. */

static const char *const SYNC_DEVICE[] = { "tvCaps", "setupDone", "seenVersion", "updateHowToldFor", "updateNoInstaller", "updateToldFor",
    "memoryMode", "fx", "fxAutoLight", "resCap", "ytPlays", "srcHealth", "deadLinks", "newEpSeen", "trackMemory", "homeFirstView",
    "localAddons", "local_addons", "user", "signinsShared",
    "streamingServer",   /* often 127.0.0.1: this device itself */
    /* Trakt and SimKL: each renewal replaces the refresh token, so two devices sharing one
     * would lock each other out. Each device connects on its own. */
    "traktToken", "traktRefresh", "traktExpires", "traktLife", "simklToken", "simklRefresh", "simklExpires", "simklLife",
    NULL };   /* this device's own */
static const char *const SYNC_SIGNIN[] = { "authKey", "email", "accounts", "traktClientSecret", "mdblistKey", "tmdbKey", "osApiKey", "osToken",
    "osUser", "torboxKey", "aiProvider", "aiKey", "aiBase", "aiModel", NULL };
static const char *const SYNC_DATA[] = { "lists", "ratings", "marks", "episodeMarks", NULL };

static int in_list(const char *const *l, const char *k) { for (int i = 0; l[i]; i++) if (!strcmp(l[i], k)) return 1; return 0; }
static int sync_is_data(const char *k) { return in_list(SYNC_DATA, k) || !strncmp(k, "lists:", 6); }

const char *app_device_name(void)
{
#if defined(SNTV_PLATFORM_WEBOS)
    return "LG TV";
#elif defined(SNTV_PLATFORM_TIZEN)
    return "Samsung TV";
#elif defined(SNTV_PLATFORM_ANDROID)
    return gfx_handheld() ? "Phone" : "Android TV";
#else
    return gfx_handheld() ? "Phone" : "Computer";
#endif
}

/* this device's sign-ins went to another: ending the Stremio session here (app_logout)
 * would end it there too */
void app_sync_shared(void) { store_set_int("signinsShared", 1); store_save(); }

char *app_sync_bundle(void)
{
    app_save_prefs();
    app_library_flush(1);
    cJSON *root = store_root(), *out = cJSON_CreateObject(), *it;
    if (!out) return NULL;
    cJSON_AddStringToObject(out, "_sync", "cuelight");
    cJSON_AddStringToObject(out, "_version", SNTV_APP_VERSION);
    cJSON_AddStringToObject(out, "device", app_device_name());
    cJSON_AddBoolToObject(out, "trakt", store_get_str("traktToken", NULL) != NULL);   /* to say: connect it there too */
    cJSON_AddBoolToObject(out, "simkl", store_get_str("simklToken", NULL) != NULL);
    cJSON *prefs = cJSON_AddObjectToObject(out, "prefs"), *signin = cJSON_AddObjectToObject(out, "signin"), *data = cJSON_AddObjectToObject(out, "data");
    cJSON_ArrayForEach(it, root) {
        if (!it->string || in_list(SYNC_DEVICE, it->string)) continue;
        cJSON *to = in_list(SYNC_SIGNIN, it->string) ? signin : sync_is_data(it->string) ? data : prefs;
        cJSON_AddItemToObject(to, it->string, cJSON_Duplicate(it, 1));
    }
    cJSON *addons = cJSON_AddArrayToObject(out, "addons");
    for (int i = 0; i < g_app.n_addons; i++) cJSON_AddItemToArray(addons, cJSON_CreateString(g_app.addons[i].transport_url));
    for (int i = 0; i < g_n_unloaded; i++) cJSON_AddItemToArray(addons, cJSON_CreateString(g_unloaded[i]));
    cJSON *lib = cJSON_AddArrayToObject(out, "library");
    for (int i = 0; i < g_app.n_library; i++) cJSON_AddItemToArray(lib, library_item_to_json(&g_app.library[i]));
    char *j = cJSON_PrintUnformatted(out);
    cJSON_Delete(out);
    return j;
}

/* the key that says a sign-in is there: a service's token, not its expiry */
static const char *signin_lead(const char *k)
{
    if (!strcmp(k, "email")) return "authKey";
    if (!strncmp(k, "os", 2)) return "osApiKey";
    if (!strncmp(k, "ai", 2)) return "aiProvider";
    return k;
}

static int has_value(const cJSON *v)
{
    return (cJSON_IsString(v) && v->valuestring[0]) || (cJSON_IsNumber(v) && v->valuedouble != 0) || cJSON_IsTrue(v) || ((cJSON_IsObject(v) || cJSON_IsArray(v)) && v->child);
}

/* a record's identity in a list: a title's id, a saved account's email */
static const cJSON *record_id(const cJSON *o)
{
    static const char *const K[] = { "id", "_id", "email", NULL };
    for (int i = 0; cJSON_IsObject(o) && K[i]; i++) {
        const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, K[i]);
        if (cJSON_IsString(v)) return v;
    }
    return NULL;
}

/* theirs joined into mine: an object's keys I lack, an array's items I lack */
static int join_json(cJSON *mine, const cJSON *theirs)
{
    int added = 0;
    const cJSON *t;
    if (cJSON_IsObject(mine) && cJSON_IsObject(theirs)) {
        cJSON_ArrayForEach(t, theirs) {
            cJSON *m = cJSON_GetObjectItemCaseSensitive(mine, t->string);
            if (!m) { cJSON_AddItemToObject(mine, t->string, cJSON_Duplicate(t, 1)); added++; }
            else if ((cJSON_IsObject(m) && cJSON_IsObject(t)) || (cJSON_IsArray(m) && cJSON_IsArray(t))) added += join_json(m, t);
        }
    } else if (cJSON_IsArray(mine) && cJSON_IsArray(theirs)) {
        cJSON_ArrayForEach(t, theirs) {
            const cJSON *m, *tid = record_id(t); int have = 0;
            cJSON_ArrayForEach(m, mine) {
                const cJSON *mid = record_id(m);
                if (tid && mid ? !strcmp(tid->valuestring, mid->valuestring) : cJSON_Compare(m, t, 1)) { have = 1; break; }
            }
            if (!have) { cJSON_AddItemToArray(mine, cJSON_Duplicate(t, 1)); added++; }
        }
    }
    return added;
}

int app_sync_apply(const char *json, int with_look, char *summary, size_t n)
{
    cJSON *in = cJSON_Parse(json), *it;
    const cJSON *tag = in ? cJSON_GetObjectItemCaseSensitive(in, "_sync") : NULL;
    if (!cJSON_IsString(tag) || strcmp(tag->valuestring, "cuelight") != 0) { cJSON_Delete(in); return -1; }
    const cJSON *signin = cJSON_GetObjectItemCaseSensitive(in, "signin"), *prefs = cJSON_GetObjectItemCaseSensitive(in, "prefs");
    const cJSON *data = cJSON_GetObjectItemCaseSensitive(in, "data"), *addons = cJSON_GetObjectItemCaseSensitive(in, "addons");
    const cJSON *lib = cJSON_GetObjectItemCaseSensitive(in, "library");
    int n_signins = 0, n_prefs = 0, n_data = 0, n_addons = 0, n_titles = 0;
    char *old_key = g_app.auth_key ? strdup(g_app.auth_key) : NULL;

    /* sign-ins: one I lack comes over; with their look, theirs wins */
    cJSON_ArrayForEach(it, signin) {
        if (!it->string || !in_list(SYNC_SIGNIN, it->string)) continue;
        if (!strcmp(it->string, "accounts")) {
            cJSON *mine = store_get_json("accounts");
            if (!cJSON_IsArray(mine)) store_set_json("accounts", cJSON_Duplicate(it, 1));
            else join_json(mine, it);
            continue;
        }
        const char *lead = signin_lead(it->string);
        if (!has_value(cJSON_GetObjectItemCaseSensitive(signin, lead))) continue;
        int mine = has_value(cJSON_GetObjectItemCaseSensitive(store_root(), lead));
        if (mine && !with_look) continue;
        if (!strcmp(it->string, lead) && !cJSON_Compare(cJSON_GetObjectItemCaseSensitive(store_root(), lead), it, 1)) n_signins++;
        store_set_json(it->string, cJSON_Duplicate(it, 1));
    }
    if (with_look) cJSON_ArrayForEach(it, prefs) {
        if (!it->string || in_list(SYNC_DEVICE, it->string) || in_list(SYNC_SIGNIN, it->string) || sync_is_data(it->string)) continue;
        store_set_json(it->string, cJSON_Duplicate(it, 1));
        n_prefs++;
    }
    cJSON_ArrayForEach(it, data) {
        if (!it->string || !sync_is_data(it->string)) continue;
        cJSON *mine = store_get_json(it->string);
        if (!mine) { store_set_json(it->string, cJSON_Duplicate(it, 1)); n_data += cJSON_GetArraySize(it); }
        else n_data += join_json(mine, it);
    }
    store_save();
    load_prefs();

    int switched;
    const char *key = follow_signin(old_key, &switched);
    free(old_key);
    if (key) store_set_int("signinsShared", 1);

    /* addons: theirs I lack join mine, after mine */
    char *add[DEFAULTS_MAX]; int n_add = 0;
    cJSON_ArrayForEach(it, addons) {
        if (!cJSON_IsString(it) || n_add >= DEFAULTS_MAX) continue;
        const char *u = it->valuestring;
        int have = 0, catalog = addon_is_builtin(u) || !strcmp(u, STREMIO_CINEMETA_URL);
        for (int i = 0; i < g_app.n_addons && !have; i++) {
            const char *m = g_app.addons[i].transport_url;
            have = !strcmp(m, u) || (catalog && (addon_is_builtin(m) || !strcmp(m, STREMIO_CINEMETA_URL)));   /* one catalog of titles is enough */
        }
        for (int i = 0; i < g_n_unloaded && !have; i++) have = !strcmp(g_unloaded[i], u);
        if (!have) add[n_add++] = strdup(u);
    }
    n_addons = n_add;
    if (g_app.auth_key) {
        for (int i = 0; i < n_add; i++) {
            if (switched && g_n_merge < DEFAULTS_MAX_MERGE) { g_merge[g_n_merge++] = add[i]; continue; }   /* once the account's list is in */
            install_addon(add[i], 0, 1);
            free(add[i]);
        }
    } else if (n_add) {
        cJSON *arr = cJSON_CreateArray();
        for (int i = 0; i < g_app.n_addons; i++) cJSON_AddItemToArray(arr, cJSON_CreateString(g_app.addons[i].transport_url));
        for (int i = 0; i < g_n_unloaded; i++) cJSON_AddItemToArray(arr, cJSON_CreateString(g_unloaded[i]));
        for (int i = 0; i < n_add; i++) { cJSON_AddItemToArray(arr, cJSON_CreateString(add[i])); free(add[i]); }
        store_set_json("localAddons", arr);
        store_set_json("local_addons", NULL);
        store_save();
    }

    /* the library: a title I lack comes over; one we both have keeps the newer progress */
    if (cJSON_IsArray(lib) && cJSON_GetArraySize(lib) > 0) {
        cJSON *wrap = cJSON_CreateObject();
        cJSON_AddItemReferenceToObject(wrap, "result", (cJSON *)lib);
        char *txt = cJSON_PrintUnformatted(wrap);
        cJSON_Delete(wrap);
        LibraryItem *items = NULL; int ni = 0; ApiError err;
        if (txt && api_parse_library(txt, &items, &ni, &err) == 0) {
            for (int k = 0; k < ni; k++) {
                LibraryItem *t = &items[k];
                if (!t->id) continue;
                LibraryItem *m = app_library_find(t->id);
                if (m && m->mtime_ms >= t->mtime_ms) continue;
                if (m) { LibraryItem x = *m; *m = *t; *t = x; push_library_item(m); }
                else {
                    g_app.library = realloc(g_app.library, sizeof(LibraryItem) * (size_t)(g_app.n_library + 1));
                    g_app.library[g_app.n_library] = *t;
                    memset(t, 0, sizeof *t);
                    push_library_item(&g_app.library[g_app.n_library++]);
                }
                n_titles++;
            }
            library_items_free(items, ni);
            if (n_titles) { save_library_cache(); app_library_flush(1); app_redraw(); }
        } else LOGW("sync: their library could not be read");
        free(txt);
    }
    if (switched) { app_load_addons(); app_load_library(); }   /* the account's, with what came over joined in */
    else if (!g_app.auth_key && n_add) app_load_addons();
    cJSON_Delete(in);

    char parts[5][48]; int np = 0;
    if (n_signins) snprintf(parts[np++], 48, "%d sign-in%s", n_signins, n_signins == 1 ? "" : "s");
    if (n_addons) snprintf(parts[np++], 48, "%d addon%s", n_addons, n_addons == 1 ? "" : "s");
    if (n_titles) snprintf(parts[np++], 48, "%d title%s", n_titles, n_titles == 1 ? "" : "s");
    if (n_data) snprintf(parts[np++], 48, "%d list or rating entr%s", n_data, n_data == 1 ? "y" : "ies");
    if (n_prefs) snprintf(parts[np++], 48, "the look and choices");
    summary[0] = 0;
    for (int i = 0; i < np; i++) {
        size_t L = strlen(summary);
        snprintf(summary + L, n - L, "%s%s", i == 0 ? "" : i == np - 1 ? " and " : ", ", parts[i]);
    }
    if (!np) snprintf(summary, n, "already the same");
    LOGI("sync: took in %d sign-ins, %d addons, %d titles, %d entries%s", n_signins, n_addons, n_titles, n_data, with_look ? ", the look" : "");
    return np;
}

void app_play_link(const char *url)
{
    const char *slash = strrchr(url, '/');
    char name[160];
    snprintf(name, sizeof name, "%s", slash && slash[1] ? slash + 1 : url);
    char *q = strchr(name, '?'); if (q) *q = 0;
    app_play_named(url, name);
}

void app_play_named(const char *url, const char *title)
{
    MetaPreview m = { 0 };
    char name[256];
    snprintf(name, sizeof name, "%s", title);
    m.id = (char *)"link"; m.type = (char *)"movie"; m.name = name;
    PlayCandidate *c = calloc(1, sizeof *c);
    c->url = strdup(url);
    c->label = strdup("Link");
    LOGI("app: playing a link from %.60s", url);
    app_push(screen_player_create(&m, "trailer:link", name, c, 1, 0, 0, NULL, NULL));
}
