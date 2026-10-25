#include "app.h"
#include "audio.h"
#include "breaks.h"
#include "phone.h"
#include "ui/widgets.h"
#include "ui/rail.h"
#include "ui/island.h"
#include "ui/intro.h"
#include "ui/bgtrailer.h"
#include "http.h"
#include "addon/catalog.h"
#include "tracking.h"
#include "perfstat.h"
#include "relay.h"
#include "image.h"
#include "memwatch.h"
#include "log.h"
#include "store.h"
#include "platform/platform.h"
#include "ui/font.h"
#include "ui/gfx.h"
#include "ui/theme.h"
#include "video/video.h"
#include "newep.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/vfs.h>
#include <signal.h>
#if defined(__linux__) && !defined(__EMSCRIPTEN__) && !defined(__SANITIZE_ADDRESS__)
#define SNTV_CRASH_LOG 1
#include <ucontext.h>
#ifdef __GLIBC__
#include <execinfo.h>
#endif
#endif
#include <SDL.h>
#include <GLES2/gl2.h>

static int dir_writable(const char *dir)
{
    char probe[1200];
    snprintf(probe, sizeof probe, "%s/.probe", dir);
    FILE *f = fopen(probe, "w");
    if (!f) return 0;
    fclose(f);
    remove(probe);
    return 1;
}

/* A folder in RAM (tmpfs/ramfs) is wiped when the TV powers off: sign-in and settings
 * with it. Such a folder is used only when nothing else is writable. */
static int in_ram(const char *dir)
{
    struct statfs fs;
    if (statfs(dir, &fs) != 0) return 0;
    unsigned long type = (unsigned long)(unsigned int)fs.f_type;
    return type == 0x01021994UL || type == 0x858458f6UL;   /* tmpfs, ramfs */
}

static char g_moved[300];   /* said once the log is open: where the data came from */

/* Copies a folder (but not the artwork cache, which refills by itself). 0 when all of it made it. */
static int copy_tree(const char *from, const char *to)
{
    DIR *d = opendir(from);
    if (!d) return -1;
    mkdir(to, 0755);
    int bad = 0;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..") || !strcmp(e->d_name, "img")) continue;
        char a[1200], b[1200];
        snprintf(a, sizeof a, "%s/%s", from, e->d_name);
        snprintf(b, sizeof b, "%s/%s", to, e->d_name);
        struct stat st;
        if (stat(a, &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) { bad |= copy_tree(a, b) != 0; continue; }
        FILE *in = fopen(a, "rb"), *out = in ? fopen(b, "wb") : NULL;
        char buf[16384]; size_t k;
        while (in && out && (k = fread(buf, 1, sizeof buf, in)) > 0) if (fwrite(buf, 1, k, out) != k) { bad = 1; break; }
        if (!in || !out) bad = 1;
        if (in) fclose(in);
        if (out && fclose(out) != 0) bad = 1;
    }
    closedir(d);
    return bad ? -1 : 0;
}

static void remove_tree(const char *dir)
{
    DIR *d = opendir(dir);
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        char a[1200]; snprintf(a, sizeof a, "%s/%s", dir, e->d_name);
        struct stat st;
        if (lstat(a, &st) == 0 && S_ISDIR(st.st_mode)) remove_tree(a); else remove(a);
    }
    closedir(d);
    rmdir(dir);
}

static int has_settings(const char *dir)
{
    char f[1200]; snprintf(f, sizeof f, "%s/settings.json", dir);
    struct stat st;
    return stat(f, &st) == 0;
}

static const char *data_dir(void)
{
    static char dir[1024];
    char ram[1024] = "";
    const char *flash_env = getenv("SNTV_FLASH_ROOTS");   /* tests: "internal,developer" stand-ins, as on webOS 10 */
    char *pref = flash_env ? NULL : SDL_GetPrefPath("stremio", "nativetv");
    if (pref && dir_writable(pref)) {
        snprintf(dir, sizeof dir, "%s", pref); SDL_free(pref);
        if (!in_ram(dir)) return dir;
        snprintf(ram, sizeof ram, "%s", dir);
    } else if (pref) SDL_free(pref);
    char *base = flash_env ? NULL : SDL_GetBasePath();   /* next to the app: where Homebrew installed it */
    if (base) {
        snprintf(dir, sizeof dir, "%sdata", base); SDL_free(base);
        mkdir(dir, 0755);
        if (dir_writable(dir) && !in_ram(dir)) return dir;
    }
    const char *home = getenv("HOME");
    if (!flash_env) {
        snprintf(dir, sizeof dir, "%s/.%s", home ? home : "/tmp", SNTV_APP_ID);
        mkdir(dir, 0755);
        if (dir_writable(dir) && !in_ram(dir)) return dir;
    }
    /* webOS 10 runs apps as their own user, for whom all of the above is read-only or in
     * RAM. Internal storage takes writes and survives power-offs. The developer partition
     * does too, but the installer unpacks updates in its temp folder and Developer Mode's
     * cache clearing empties it: sign-in kept there (0.10.12 to 0.10.17) moves over. */
    char internal[512] = "/media/internal", developer[512] = "/media/developer/temp";
    if (flash_env) sscanf(flash_env, "%511[^,],%511s", internal, developer);
    char old[1024];
    snprintf(old, sizeof old, "%s/%s", developer, SNTV_APP_ID);
    snprintf(dir, sizeof dir, "%s/.%s", internal, SNTV_APP_ID);
    mkdir(internal, 0755);
    mkdir(dir, 0755);
    if (dir_writable(dir) && !in_ram(dir)) {
        if (!has_settings(dir) && has_settings(old)) {
            if (copy_tree(old, dir) == 0) { remove_tree(old); snprintf(g_moved, sizeof g_moved, "data moved here from %s", old); }
            else snprintf(g_moved, sizeof g_moved, "could not copy all of %s; it stays there", old);
        }
        return dir;
    }
    snprintf(dir, sizeof dir, "%s", old);
    mkdir(developer, 0755);
    mkdir(dir, 0755);
    if (dir_writable(dir) && !in_ram(dir)) return dir;
    if (ram[0]) { snprintf(dir, sizeof dir, "%s", ram); return dir; }
    if (dir_writable(dir)) return dir;
    snprintf(dir, sizeof dir, "/tmp/%s", SNTV_APP_ID);
    mkdir(dir, 0755);
    return dir;
}

/* Artwork budget follows the TV's RAM, like Nuvio's normal/highcache packages. */
static size_t image_budget(void) { return mem_normal_budget(); }   /* 0 = unknown: keep the platform default */

/* Bundled Inter (regular + bold) next to the binary, else system fallbacks. */
static int g_pip_focus;   /* the corner player is the focused element */
static int g_dev_hold;    /* SNTV_KEYS "hold": the next OK press counts as held already */

static int ambient_due(const Screen *top)
{
    static float idle_after = -1;
    if (idle_after < 0) { const char *s = getenv("SNTV_AMBIENT_SECONDS"); idle_after = s ? (float)atof(s) : 0; }   /* test hook */
    float limit = idle_after > 0 ? idle_after : g_app.ambient_minutes * 60.0f;
    if (!g_app.ambient_minutes && idle_after <= 0) return 0;
    if (g_app.time - g_app.last_input < limit) return 0;
    /* nothing takes over a video but a break (which counts down and resumes it itself):
     * not while it plays, stalls or changes source, full screen or in the corner; only
     * once the viewer paused it */
    static const char *const NEVER_UNDER[] = { "ambient", "login", "break", "pin", "update" };
    if (player_holds_screen() || streams_starting()) return 0;   /* nor the next episode on its way */
    for (size_t i = 0; i < sizeof NEVER_UNDER / sizeof *NEVER_UNDER; i++) if (strcmp(top->name, NEVER_UNDER[i]) == 0) return 0;
    VideoInfo vi;
    video_info(&vi);
    if (vi.state == VIDEO_PLAYING || vi.state == VIDEO_LOADING) return 0;   /* full screen or in the corner */
    return 1;
}

static void write_ppm(SDL_Window *win, const char *path)
{
    int w, h;
    SDL_GL_GetDrawableSize(win, &w, &h);
    unsigned char *px = malloc((size_t)w * h * 4);
    if (!px) return;
    gfx_flush();
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px);
    FILE *f = fopen(path, "wb");
    if (f) {
        fprintf(f, "P6\n%d %d\n255\n", w, h);
        for (int y = h - 1; y >= 0; y--)
            for (int x = 0; x < w; x++) fwrite(px + ((size_t)y * w + x) * 4, 1, 3, f);
        fclose(f);
    }
    free(px);
}

/* Dev aid: SNTV_SCREENSHOT=/path/out.ppm dumps the framebuffer after
 * SNTV_SCREENSHOT_FRAMES frames (default 120) and exits; SNTV_SCREENSHOT_EVERY=n also
 * writes out.ppm-00010.ppm and on every n frames before (a recording). Keys can be scripted with
 * SNTV_KEYS="down,right,ok,..." (one key every 20 frames) for headless UI checks. */
static void dev_hooks(SDL_Window *win, int frame)
{
    static const struct { const char *n; SDL_Keycode k; } keys[] = {
        { "up", SDLK_UP }, { "down", SDLK_DOWN }, { "left", SDLK_LEFT }, { "right", SDLK_RIGHT },
        { "ok", SDLK_RETURN }, { "back", SDLK_ESCAPE }, { "search", SDLK_F3 }, { "red", SDLK_F1 }, { "blue", SDLK_F4 }, { "yellow", SDLK_F5 },
        { "play", SDLK_p }, { "stop", SDLK_AUDIOSTOP }, { "info", SDLK_HELP }, { "rewind", SDLK_AUDIOREWIND }, { NULL, 0 } };
    static struct { int left; float x, y, dx, dy; Uint32 until; } drag;   /* a scripted drag, one move a frame (a press: until then) */
    if (drag.left > 0) {
        SDL_Event e; memset(&e, 0, sizeof e);
        if (!drag.until || SDL_GetTicks() >= drag.until) drag.left--;
        e.type = drag.left ? SDL_FINGERMOTION : SDL_FINGERUP; e.tfinger.fingerId = 1;
        e.tfinger.x = drag.x + drag.dx * (8 - drag.left) / 8; e.tfinger.y = drag.y + drag.dy * (8 - drag.left) / 8;
        if (!drag.left) { e.tfinger.x = drag.x + drag.dx; e.tfinger.y = drag.y + drag.dy; }
        SDL_PushEvent(&e);
    }
    const char *script = getenv("SNTV_KEYS");
    if (script) {   /* keys still to come: keep drawing, so they come every 20 frames of a steady rate (a bare playing picture draws only a few a second) */
        int n = 1;
        for (const char *c = script; *c; c++) n += *c == ',';
        if (frame / 20 < n || drag.left > 0) app_redraw();
    }
    if (script && frame % 20 == 10) {
        int idx = frame / 20;
        char *copy = strdup(script);
        int i = 0;
        for (char *tok = strtok(copy, ","); tok; tok = strtok(NULL, ","), i++) {
            if (i != idx) continue;
            float tx, ty;
            int taps = sscanf(tok, "dtap:%f:%f", &tx, &ty) == 2 ? 2 : sscanf(tok, "tap:%f:%f", &tx, &ty) == 2 ? 1 : 0;
            for (int k = 1; k < taps; k++) {   /* "dtap": a double tap there */
                int px, py, pw, ph, dw, dh;
                gfx_ui_to_pixels(tx, ty, 0, 0, &px, &py, &pw, &ph);
                gfx_drawable_size(&dw, &dh);
                SDL_Event e; memset(&e, 0, sizeof e);
                e.type = SDL_FINGERDOWN; e.tfinger.fingerId = 1; e.tfinger.x = (float)px / dw; e.tfinger.y = (float)py / dh;
                SDL_PushEvent(&e);
                e.type = SDL_FINGERUP;
                SDL_PushEvent(&e);
            }
            if (taps) {   /* a finger tapping at canvas units x:y */
                int px, py, pw, ph, dw, dh;
                gfx_ui_to_pixels(tx, ty, 0, 0, &px, &py, &pw, &ph);
                gfx_drawable_size(&dw, &dh);
                SDL_Event e; memset(&e, 0, sizeof e);
                e.type = SDL_FINGERDOWN; e.tfinger.fingerId = 1; e.tfinger.x = (float)px / dw; e.tfinger.y = (float)py / dh;
                SDL_PushEvent(&e);
                e.type = SDL_FINGERUP;
                SDL_PushEvent(&e);
                continue;
            }
            float tx2, ty2;
            int press = sscanf(tok, "press:%f:%f", &tx, &ty) == 2;   /* a finger held still at x:y for a second */
            if (press) { tx2 = tx; ty2 = ty; }
            if (press || sscanf(tok, "drag:%f:%f:%f:%f", &tx, &ty, &tx2, &ty2) == 4) {   /* a finger dragged from x:y to x2:y2 (canvas units) */
                int px, py, pw, ph, qx, qy, dw, dh;
                gfx_ui_to_pixels(tx, ty, 0, 0, &px, &py, &pw, &ph);
                gfx_ui_to_pixels(tx2, ty2, 0, 0, &qx, &qy, &pw, &ph);
                gfx_drawable_size(&dw, &dh);
                SDL_Event e; memset(&e, 0, sizeof e);
                e.type = SDL_FINGERDOWN; e.tfinger.fingerId = 1; e.tfinger.x = (float)px / dw; e.tfinger.y = (float)py / dh;
                SDL_PushEvent(&e);
                drag.x = e.tfinger.x; drag.y = e.tfinger.y; drag.dx = (float)(qx - px) / dw; drag.dy = (float)(qy - py) / dh; drag.left = 8; drag.until = press ? SDL_GetTicks() + 1000 : 0;
                continue;
            }
            if (strncmp(tok, "t:", 2) == 0) {   /* typed text */
                SDL_Event e; memset(&e, 0, sizeof e);
                e.type = SDL_TEXTINPUT;
                snprintf(e.text.text, sizeof e.text.text, "%s", tok + 2);
                SDL_PushEvent(&e);
                continue;
            }
            /* "hold" presses OK without letting go, "release" lets go ("wait" spends a step) */
            int hold = strcmp(tok, "hold") == 0, release = strcmp(tok, "release") == 0;
            for (int k = 0; keys[k].n; k++) if (strcmp(keys[k].n, hold || release ? "ok" : tok) == 0) {
                SDL_Event e; memset(&e, 0, sizeof e);
                e.type = release ? SDL_KEYUP : SDL_KEYDOWN; e.key.keysym.sym = keys[k].k; e.key.keysym.scancode = SDL_SCANCODE_UNKNOWN;
                SDL_PushEvent(&e);
                if (!hold && !release) { e.type = SDL_KEYUP; SDL_PushEvent(&e); } else if (hold) g_dev_hold = 1;   /* counts as held at once */
            }
        }
        free(copy);
    }
    const char *shot = getenv("SNTV_SCREENSHOT");
    if (!shot) return;
    const char *fr = getenv("SNTV_SCREENSHOT_FRAMES");
    int target = fr ? atoi(fr) : 120;
    const char *every = getenv("SNTV_SCREENSHOT_EVERY");
    if (every && atoi(every) > 0 && frame < target && frame % atoi(every) == 0) {
        char path[1100]; snprintf(path, sizeof path, "%s-%05d.ppm", shot, frame);
        write_ppm(win, path);
    }
    if (frame != target) return;
    Screen *top = app_top();
    const char *expected = getenv("SNTV_EXPECT_SCREEN");
    if (expected && (!top || strcmp(top->name, expected))) {
        LOGE("UI check: expected %s, got %s", expected, top ? top->name : "none");
        exit(2);
    }
    LOGI("UI check: screen=%s theme=%s", top ? top->name : "none", THEME_NAMES[g_theme.id]);
    const char *expected_video = getenv("SNTV_EXPECT_VIDEO");
    if (expected_video && *expected_video) {
        VideoInfo vi; video_info(&vi);
        VideoState state = !strcmp(expected_video, "paused") ? VIDEO_PAUSED : VIDEO_PLAYING;
        if (vi.state != state) { LOGE("UI check: expected video %s, got state %d", expected_video, vi.state); exit(2); }
    }
    write_ppm(win, shot);
    LOGI("screenshot written to %s", shot);
    g_app.quit = 1;
}

static int g_seen_keyup, g_ok_down, g_ok_long;

/* Touch, for phones and tablets (the app is laid out for a remote): a tap is on what is under
 * the finger, a press held half a second is the hold-OK menu. A drag moves the page, list or
 * row under it with the finger and lets it glide on (ui_pan); where nothing scrolls that way,
 * it moves the focus as it goes, a step each stretch. The system's back gesture arrives as Back. */
static struct {
    int down, mode, steps, caught;   /* mode: 0 not moved yet, 1 a scroller has it, 2 focus steps, 3 the screen's own, 4 nothing (a phone) */
    float x, y, along;               /* where it went down (0..1 of the screen); the drag's travel (canvas units) */
    Uint32 t;
    struct { Uint32 t; float along; } trail[16];   /* the last moves, by the events' own times: the speed it was let go at */
    int n_trail;
    SDL_FingerID id;
    KeyAction step;
    int vertical;
} g_touch;
static void dispatch_key(KeyAction a);

static void touch_to_ui(float fx, float fy, float *ux, float *uy)
{
    int pw, ph;
    gfx_drawable_size(&pw, &ph);
    gfx_pixels_to_ui(fx * pw, fy * ph, ux, uy);
}

/* How fast the finger was going as it lifted: across the last 100 ms of its moves (a finger
 * that stopped before lifting gives about 0). Canvas units a second. */
static float touch_speed(Uint32 now)
{
    int n = g_touch.n_trail < 16 ? g_touch.n_trail : 16;
    if (n < 2) return 0;
    int last = (g_touch.n_trail - 1) % 16, first = last;
    for (int k = 1; k < n; k++) {
        int i = (last - k + 16) % 16;
        if (now - g_touch.trail[i].t > 100) break;
        first = i;
    }
    if (first == last) return 0;
    Uint32 dt = g_touch.trail[last].t - g_touch.trail[first].t;
    if (dt < 8) dt = 8;
    return (g_touch.trail[last].along - g_touch.trail[first].along) * 1000.0f / (float)dt;
}

static Uint32 g_touch_now;   /* the event being handled: its time */

static void touch_motion(float fx, float fy)
{
    if (g_touch.mode == 3) {
        float ux, uy;
        touch_to_ui(fx, fy, &ux, &uy);
        Screen *top = app_top();
        if (top && top->drag) top->drag(top, 1, ux, uy);
        app_redraw();
        return;
    }
    int pw, ph;
    gfx_drawable_size(&pw, &ph);
    float dx = fx - g_touch.x, dy = (fy - g_touch.y) * (pw > 0 ? (float)ph / pw : 9.0f / 16.0f);   /* in screen widths, both ways (held upright too) */
    float dist = SDL_sqrtf(dx * dx + dy * dy);
    if (!g_touch.mode) {
        if (dist < 0.02f) return;   /* still a tap */
        g_touch.vertical = SDL_fabsf(dy) > SDL_fabsf(dx);
        float ux, uy;
        touch_to_ui(g_touch.x, g_touch.y, &ux, &uy);
        Screen *on = app_top();
        if (ui_pan_grab(ux, uy, g_touch.vertical)) { g_touch.mode = 1; LOGI("touch: drag on %s follows the finger %s", on ? on->name : "?", g_touch.vertical ? "up and down" : "across"); }
        else if (SNTV_PHONE) { g_touch.mode = 4; LOGI("touch: drag on %s at %.0f,%.0f: nothing scrolls %s there", on ? on->name : "?", ux, uy, g_touch.vertical ? "up and down" : "across"); }   /* a phone: a drag is never a remote's steps */
        else {
            LOGI("touch: drag on %s at %.0f,%.0f steps the focus (nothing scrolls %s there)", on ? on->name : "?", ux, uy, g_touch.vertical ? "up and down" : "across");
            g_touch.mode = 2;
            g_touch.step = g_touch.vertical ? (dy < 0 ? KEY_DOWN : KEY_UP) : (dx < 0 ? KEY_RIGHT : KEY_LEFT);   /* content follows the finger */
            Screen *top = app_top();
            int id;
            if (top && top->tap && ui_hit_find(ux, uy, top, &id) && !(id >= HIT_RAIL && id < HIT_RAIL + RAIL_COUNT)) {
                g_touch_aim = 1; top->tap(top, id); g_touch_aim = 0;   /* the steps move what it started on */
            }
        }
    }
    float x0, y0, x1, y1;
    touch_to_ui(g_touch.x, g_touch.y, &x0, &y0);
    touch_to_ui(fx, fy, &x1, &y1);
    float along = g_touch.vertical ? y1 - y0 : x1 - x0;
    int i = g_touch.n_trail++ % 16;
    g_touch.trail[i].t = g_touch_now; g_touch.trail[i].along = along;
    g_touch.along = along;
    if (g_touch.mode == 1) { ui_pan_drag(along); return; }
    if (g_touch.mode == 4) return;
    float travel = g_touch.vertical ? SDL_fabsf(dy) : SDL_fabsf(dx);
    int want = (int)(travel / 0.06f) + 1;   /* a step every 6% of the screen's width */
    if (want > 12) want = 12;
    while (g_touch.steps < want) { g_touch.steps++; dispatch_key(g_touch.step); }
    app_redraw();
}

static void touch_event(const SDL_Event *e)
{
    g_touch_now = e->tfinger.timestamp ? e->tfinger.timestamp : SDL_GetTicks();
    if (e->type == SDL_FINGERDOWN && !g_touch.down) {
        memset(&g_touch, 0, sizeof g_touch);
        g_touch.down = 1; g_touch.x = e->tfinger.x; g_touch.y = e->tfinger.y; g_touch.t = g_touch_now;
        g_touch.trail[0].t = g_touch_now; g_touch.trail[0].along = 0; g_touch.n_trail = 1; g_touch.id = e->tfinger.fingerId;
        float ux, uy;
        touch_to_ui(g_touch.x, g_touch.y, &ux, &uy);
        g_app.finger[0] = ux; g_app.finger[1] = uy; g_app.finger_on = 1;   /* the card under it is the one a tap opens */
        g_touch.caught = ui_pan_catch(ux, uy);   /* a finger on a gliding list stops it, and is not a tap */
        Screen *top = app_top();
        if (!g_touch.caught && top && top->drag && top->drag(top, 0, ux, uy)) { g_touch.mode = 3; app_redraw(); }
        return;
    }
    if (!g_touch.down || e->tfinger.fingerId != g_touch.id) return;
    if (e->type == SDL_FINGERMOTION) { touch_motion(e->tfinger.x, e->tfinger.y); return; }
    if (e->type != SDL_FINGERUP) return;
    touch_motion(e->tfinger.x, e->tfinger.y);
    g_app.finger_on = 0;
    g_touch.down = 0;
    g_app.last_input = g_app.time;
    if (g_touch.mode == 3) {
        float ux, uy;
        touch_to_ui(e->tfinger.x, e->tfinger.y, &ux, &uy);
        Screen *top = app_top();
        if (top && top->drag) top->drag(top, 2, ux, uy);
        app_redraw();
        return;
    }
    Uint32 held = g_touch_now - g_touch.t;
    float v = touch_speed(g_touch_now);
    if (v > 14000) v = 14000;
    if (v < -14000) v = -14000;
    static int told;
    if (!told) { told = 1; app_toast("Tap anything to open it  \xC2\xB7  Drag to scroll  \xC2\xB7  Hold for options"); }
    if (g_touch.mode == 1) { ui_pan_release(v); return; }
    if (g_touch.mode == 4) return;   /* a drag over nothing: not a tap either */
    if (g_touch.mode == 2) {   /* a quick flick goes a few steps further */
        float fast = SDL_fabsf(v) / SNTV_UI_W;   /* screen widths a second */
        int more = fast > 0.8f ? (int)(fast * 3) : 0;
        if (more > 10) more = 10;
        while (more-- > 0) dispatch_key(g_touch.step);
        return;
    }
    if (g_touch.caught) { app_redraw(); return; }
    /* a tap: on what is under the finger, where the screen says what that is */
    KeyAction press = held >= 500 ? KEY_LONG_OK : KEY_OK;
    int id;
    float ux, uy;
    touch_to_ui(e->tfinger.x, e->tfinger.y, &ux, &uy);
    ui_pan_keys();   /* what it does may move the focus: the lists follow it again */
    Screen *top = app_top();
    if (top && ui_hit_find(ux, uy, top, &id)) {
        if (id >= HIT_RAIL && id < HIT_RAIL + RAIL_COUNT) { rail_tap(id - HIT_RAIL); app_redraw(); return; }
        if (!top->tap || !top->tap(top, id)) dispatch_key(press);
        app_redraw();
        return;
    }
    if (!top || !top->tap) dispatch_key(press);   /* a screen that does not say where things are: OK on its focus */
}
static int g_backgrounded;   /* hidden or sent to the background (not just an overlay over us) */
static Uint32 g_ok_t0;

/* One key to the top screen, with the corner player's keys first. */
static void dispatch_key(KeyAction a)
{
    Screen *top = app_top();
    if (intro_active()) { intro_skip(); return; }   /* a key during the launch animation only ends it */
    if (island_has_focus()) {   /* the resume card has the focus */
        if (a == KEY_OK || a == KEY_LONG_OK) island_focus_key(1);
        else if (a == KEY_DOWN || a == KEY_BACK || a == KEY_LEFT) island_focus_key(0);
        return;
    }
    if (a == KEY_OK || a == KEY_LONG_OK) g_app.press_at = g_app.time;
    if (a == KEY_BLUE && top && !g_app.pip) {   /* Blue wherever the island shows the resume card opens that title */
        static const char *const OWN_BLUE[] = { "player", "diagnostics", "ambient", "break", "options", "update", "pin", "login", "welcome" };
        int mine = 0;
        for (size_t i = 0; i < sizeof OWN_BLUE / sizeof OWN_BLUE[0]; i++) if (!strcmp(top->name, OWN_BLUE[i])) mine = 1;
        if (!mine && island_key_blue()) return;
    }
    /* the corner player's keys, unless a break, a dialog or the screensaver has the screen
     * (they come first), or a title page's Play plays that title (its own corner video comes back) */
    static const char *const OVER_PIP[] = { "break", "options", "update", "pin", "login", "welcome", "ambient" };
    int pip_keys = g_app.pip != NULL;
    for (size_t i = 0; pip_keys && top && i < sizeof OVER_PIP / sizeof *OVER_PIP; i++) if (!strcmp(top->name, OVER_PIP[i])) pip_keys = 0;
    if (pip_keys && (a == KEY_PLAY_PAUSE || a == KEY_PLAY) && !(top && !strcmp(top->name, "detail"))) { app_pip_restore(); return; }
    if (pip_keys && a == KEY_STOP) { app_pip_close(); return; }
    if (pip_keys && g_pip_focus) {   /* the corner player has the focus: 1 = Full screen, 2 = Close */
        if (a == KEY_OK || a == KEY_LONG_OK) { if (g_pip_focus == 2) app_pip_close(); else app_pip_restore(); }
        else if (a == KEY_LEFT) g_pip_focus = 1;
        else if (a == KEY_RIGHT) g_pip_focus = 2;
        else if (a == KEY_UP || a == KEY_BACK) g_pip_focus = 0;
        app_redraw();
        return;
    }
    if ((a == KEY_PLAY_PAUSE || a == KEY_PLAY || a == KEY_PAUSE) && !(top && !strcmp(top->name, "detail")) && bgtrailer_key_sound()) return;   /* a trailer behind the artwork: its sound (a title page's Play plays the title) */
    if (top && top->key && top->key(top, a)) return;
    if (a == KEY_LONG_OK) { dispatch_key(KEY_OK); return; }
    if (g_app.pip && a == KEY_DOWN) { g_pip_focus = 1; app_redraw(); return; }   /* past the bottom of the screen: the corner */
    if (a == KEY_BACK) app_pop();
}

#ifdef SNTV_CRASH_LOG
/* A crash writes one last line to the log: the signal, where in the code, and which screens
 * were open, so the previous run's log in Diagnostics says what happened. Only calls that are
 * safe inside a signal handler (write, no locks, no allocation); then the default ends the app. */
static void crash_put(int fd, const char *s) { if (write(fd, s, strlen(s)) < 0) {} }
static void crash_hex(int fd, unsigned long v)
{
    char b[2 + 2 * sizeof v + 1], *p = b + sizeof b - 1;
    *p = 0;
    do { *--p = "0123456789abcdef"[v & 15]; v >>= 4; } while (v);
    *--p = 'x'; *--p = '0';
    crash_put(fd, p);
}

static void on_crash(int sig, siginfo_t *si, void *ctx)
{
    int fd = log_fd();
    if (fd >= 0) {
        const char *name = sig == SIGSEGV ? "SIGSEGV" : sig == SIGBUS ? "SIGBUS" : sig == SIGFPE ? "SIGFPE" : sig == SIGILL ? "SIGILL" : "SIGABRT";
        crash_put(fd, "--:--:-- E crash: "); crash_put(fd, name);
        crash_put(fd, " at "); crash_hex(fd, (unsigned long)si->si_addr);
        const ucontext_t *uc = ctx;
#if defined(__arm__)
        crash_put(fd, ", pc "); crash_hex(fd, uc->uc_mcontext.arm_pc);
        crash_put(fd, " lr "); crash_hex(fd, uc->uc_mcontext.arm_lr);
#elif defined(__aarch64__)
        crash_put(fd, ", pc "); crash_hex(fd, uc->uc_mcontext.pc);
#elif defined(__x86_64__)
        crash_put(fd, ", pc "); crash_hex(fd, (unsigned long)uc->uc_mcontext.gregs[16]   /* REG_RIP */);
#else
        (void)uc;
#endif
        crash_put(fd, ", screens");
        for (int i = 0; i < g_app.depth && i < 12; i++) { crash_put(fd, i ? " > " : " "); crash_put(fd, g_app.stack[i] && g_app.stack[i]->name ? g_app.stack[i]->name : "?"); }
#ifdef __GLIBC__
        void *at[24];
        int n = backtrace(at, 24);
        crash_put(fd, ", stack");
        for (int i = 0; i < n; i++) { crash_put(fd, " "); crash_hex(fd, (unsigned long)at[i]); }
#endif
        crash_put(fd, "\n");
        fsync(fd);
    }
    signal(sig, SIG_DFL);
    raise(sig);
}

static void crash_log_install(void)
{
    static char alt[64 * 1024];   /* a stack of its own, so a stack overflow is logged too */
    stack_t ss = { .ss_sp = alt, .ss_size = sizeof alt };
    sigaltstack(&ss, NULL);
#ifdef __GLIBC__
    void *warm[1]; backtrace(warm, 1);   /* loads the unwinder now, not inside the handler */
#endif
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_sigaction = on_crash;
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_RESETHAND;
    sigemptyset(&sa.sa_mask);
    int sigs[] = { SIGSEGV, SIGBUS, SIGFPE, SIGILL, SIGABRT };
    for (size_t i = 0; i < sizeof sigs / sizeof *sigs; i++) sigaction(sigs[i], &sa, NULL);
}
#endif

#ifdef SNTV_PLATFORM_ANDROID
#include "assets_index.h"
/* Inside the APK the bundled files (fonts, the YouTube solver, sounds) are not files the C
 * code can open, so they are copied once per version into internal storage, and the app
 * runs from there: "assets/..." opens as on every other platform. */
static void mkdirs_for(const char *path)
{
    char d[1200];
    snprintf(d, sizeof d, "%s", path);
    for (char *p = d + 1; *p; p++) if (*p == '/') { *p = 0; mkdir(d, 0755); *p = '/'; }
}

static void android_unpack_assets(void)
{
    const char *root = SDL_AndroidGetInternalStoragePath();
    if (!root || chdir(root) != 0) return;
    char have[32] = "";
    FILE *f = fopen("assets/.version", "r");
    if (f) { if (!fgets(have, sizeof have, f)) have[0] = 0; fclose(f); }
    if (!strcmp(have, SNTV_APP_VERSION)) return;
    int n = 0, bad = 0;
    for (int i = 0; ASSET_FILES[i]; i++) {
        SDL_RWops *in = SDL_RWFromFile(ASSET_FILES[i] + strlen("assets/"), "rb");   /* the APK's assets folder is assets/ itself */
        if (!in) { bad++; continue; }
        mkdirs_for(ASSET_FILES[i]);
        FILE *out = fopen(ASSET_FILES[i], "wb");
        char buf[65536]; size_t got;
        while (out && (got = SDL_RWread(in, buf, 1, sizeof buf)) > 0) fwrite(buf, 1, got, out);
        if (out) { fclose(out); n++; } else bad++;
        SDL_RWclose(in);
    }
    if (!bad && (f = fopen("assets/.version", "w"))) { fputs(SNTV_APP_VERSION, f); fclose(f); }
    fprintf(stderr, "assets: %d unpacked to %s, %d failed\n", n, root, bad);
}
#endif

static int g_perf_fill;   /* SNTV_PERF_FILL: every 30th frame's paint in the log (finding what a TV's GPU finds slow) */

int main(int argc, char **argv)
{
    g_perf_fill = getenv("SNTV_PERF_FILL") != NULL;
    (void)argc; (void)argv;
    int frame = 0;
#ifdef SIGPIPE
    signal(SIGPIPE, SIG_IGN);   /* a player hanging up on the phone server must not end the app */
#endif
#ifdef SNTV_PLATFORM_WEBOS
    /* Without these the remote's Back/Exit keys never reach the app. Must be set before the window exists. */
    SDL_SetHint("SDL_WEBOS_ACCESS_POLICY_KEYS_BACK", "true");
    SDL_SetHint("SDL_WEBOS_ACCESS_POLICY_KEYS_EXIT", "true");
    SDL_SetHint("SDL_WEBOS_CURSOR_SLEEP_TIME", "5000");
#endif
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");
#ifdef SNTV_PLATFORM_ANDROID
    SDL_SetHint(SDL_HINT_ANDROID_TRAP_BACK_BUTTON, "1");   /* Back is ours (it closes a screen), not the system's */
    {   /* a phone (CuelightActivity says) also turns upright */
        const char *hh = getenv("SNTV_HANDHELD");
        SDL_SetHint(SDL_HINT_ORIENTATIONS, hh && *hh == '1' ? "LandscapeLeft LandscapeRight Portrait" : "LandscapeLeft LandscapeRight");
    }
#endif

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_TIMER) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
#ifdef SNTV_PLATFORM_ANDROID
    android_unpack_assets();
#endif
    const char *dir = data_dir();
    log_init(dir);
#ifdef SNTV_CRASH_LOG
    crash_log_install();
#endif
    LOGI("%s %s starting, data dir %s", SNTV_APP_NAME, SNTV_APP_VERSION, dir);
    if (g_moved[0]) LOGI("store: %s", g_moved);
    if (in_ram(dir)) LOGW("store: %s is in RAM: sign-in and settings will not survive the TV powering off", dir);
    store_init(dir);
    /* a marker that only a clean exit removes: present at start = the last session crashed */
    char alive[1200]; snprintf(alive, sizeof alive, "%s/session.alive", dir);
    int crashed = 0;
    { FILE *f = fopen(alive, "r"); if (f) { crashed = 1; fclose(f); } f = fopen(alive, "w"); if (f) fclose(f); }
    if (crashed) LOGW("the previous session did not exit cleanly");

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);   /* needed for the video hole */
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    Uint32 flags = SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN;
    int w = 1280, h = 720;
#if defined(SNTV_PLATFORM_WEBOS) || defined(SNTV_PLATFORM_TIZEN) || defined(SNTV_PLATFORM_ANDROID)
#ifdef SNTV_PLATFORM_ANDROID
    if (getenv("SNTV_HANDHELD")) flags |= SDL_WINDOW_RESIZABLE;   /* a phone: between its status and navigation bars (the player hides them) */
    else
#endif
    flags |= SDL_WINDOW_FULLSCREEN;
    SDL_DisplayMode dm;
    if (SDL_GetCurrentDisplayMode(0, &dm) == 0) { w = dm.w; h = dm.h; }
#else
    flags |= SDL_WINDOW_RESIZABLE;
    const char *ws = getenv("SNTV_WINDOW");   /* tests: "2000x900" is a phone's shape, "1440x1080" a tablet's */
    if (ws && sscanf(ws, "%dx%d", &w, &h) != 2) { w = 1280; h = 720; }
#endif
    SDL_Window *win = SDL_CreateWindow(SNTV_APP_NAME, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, w, h, flags);
    if (!win) { LOGE("SDL_CreateWindow: %s", SDL_GetError()); return 1; }
    SDL_GLContext ctx = SDL_GL_CreateContext(win);
    if (!ctx) { LOGE("SDL_GL_CreateContext: %s", SDL_GetError()); return 1; }
    SDL_GL_SetSwapInterval(1);

    if (gfx_init(win) != 0) return 1;
    {   /* a phone (under 9 inches across) draws larger; SNTV_HANDHELD=1 tries it on the desktop */
        const char *hh = getenv("SNTV_HANDHELD");
        int handheld = hh && *hh == '1';
#ifdef SNTV_PLATFORM_ANDROID
        float ddpi = 0;
        if (SDL_GetDisplayDPI(0, &ddpi, NULL, NULL) == 0 && ddpi > 0 && SDL_sqrtf((float)w * w + (float)h * h) / ddpi < 9.0f) handheld = 1;
#endif
        LOGI("screen: %dx%d%s", w, h, handheld ? ", handheld: drawn larger" : "");
        if (handheld) gfx_set_handheld(1);
    }
    perf_mark("window");
    /* the saved theme picks the typefaces; app_init applies its colours */
    if (!store_get_int("lookStreaming", 0) && store_get_int("theme", THEME_STREAM) != THEME_APERTURE) {   /* preserve an explicitly selected prototype */
        store_set_int("lookStreaming", 1);
        store_set_int("theme", THEME_STREAM); store_set_int("themeAccent", 0); store_set_int("themeBg", 0);
        LOGI("theme: switched to the Streaming look");
    }
    int theme = store_get_int("theme", THEME_STREAM);
    theme_apply(theme, store_get_int("themeAccent", 0), store_get_int("themeBg", 0));
    /* the network and the artwork loader first: what Home showed last time is read from
     * the disk cache and decoded on its threads while the fonts load */
    http_set_local(catalog_handles, catalog_answer);   /* the built-in TMDB catalog */
    if (http_init() != 0) LOGE("network unavailable: libcurl missing");
    if (getenv("SNTV_HTTP_PROBE")) {   /* test hook: a 64 KB range request, as the source checks make */
        HttpOptions po = { .range_bytes = 65536, .timeout_s = 20, .no_json_accept = 1 };
        HttpResponse pr;
        http_request_ex(getenv("SNTV_HTTP_PROBE"), &po, &pr);
        LOGI("http probe: status %ld, %zu bytes%s", pr.status, pr.len, pr.cut ? ", cut at its budget" : "");
        http_response_free(&pr);
    }
    {   /* artwork off the developer partition: the installer unpacks updates there, and a
         * full cache there makes the next update fail ("ipk extraction failure") */
        char cache[1024];
        snprintf(cache, sizeof cache, "%s", dir);
        if (!strncmp(dir, "/media/developer/", 17)) {
            char alt[1024];
            snprintf(alt, sizeof alt, "/media/internal/.%s", SNTV_APP_ID);
            mkdir(alt, 0755);
            if (dir_writable(alt) && !in_ram(alt)) {
                snprintf(cache, sizeof cache, "%s", alt);
                char old[1100]; snprintf(old, sizeof old, "%s/img", dir);
                image_drop_old_cache(old);
            }
        }
        image_init(cache, image_budget());
    }
    {
        const cJSON *first = store_get_json("homeFirstView"), *u;
        int n = 0;
        cJSON_ArrayForEach(u, first) if (cJSON_IsString(u) && n++ < 24) image_prefetch(u->valuestring);
        if (n) LOGI("home: warming %d pictures from last time", n);
    }
    font_set_cache_dir(dir);
    if (theme_load_fonts(theme) != 0) {
        LOGE("no usable font found");
        return 1;
    }
    perf_mark("fonts");
    audio_init();
    phone_start();
    if (video_init(win) != 0) LOGW("video backend failed to initialise");
    LOGI("video backend: %s", video_backend_name());
    app_init();
    mem_apply_mode();
    if (profiles_at_start()) app_push(screen_profiles_create());   /* who's watching, before Home */
    intro_start();
    if (crashed && !getenv("SNTV_SCREENSHOT")) app_toast("The app closed unexpectedly last time. Settings > About > Diagnostics, Blue: that session's log");

    Uint32 prev = SDL_GetTicks();
    while (!g_app.quit) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            Screen *top = app_top();
            if (e.type == SDL_QUIT) g_app.quit = 1;
            else if (e.type == SDL_TEXTINPUT && top && top->text) { ui_pan_keys(); top->text(top, e.text.text); }
            else if (e.type == SDL_FINGERDOWN || e.type == SDL_FINGERUP || e.type == SDL_FINGERMOTION) touch_event(&e);
            else if (e.type == SDL_KEYUP) {
                g_seen_keyup = 1;
                SDL_Event d = e; d.type = SDL_KEYDOWN;
                if (input_map(&d) == KEY_OK && g_ok_down) {   /* released: OK, or the options once the hold filled */
                    g_ok_down = 0;
                    g_app.ok_hold = 0;
                    dispatch_key(g_ok_long ? KEY_LONG_OK : KEY_OK);
                }
            }
            else if (e.type == SDL_KEYDOWN) {
                perf_key();
                KeyAction a = input_map(&e);
                g_app.last_input = g_app.time;
                ui_pan_keys();   /* a key: the lists follow the focus again */
                g_app.key_repeat = e.key.repeat != 0;
                if (a == KEY_EXIT) { g_app.quit = 1; continue; }
                if (a == KEY_NONE) {   /* a remote key the app doesn't know yet: what it was, for Diagnostics */
                    if (!e.key.repeat) LOGI("input: unmapped key sym %d scancode %d", (int)e.key.keysym.sym, (int)e.key.keysym.scancode);
                    continue;
                }
                /* a held OK presses once (no double play, no OK landing on the next screen); remotes
                 * that never send releases look like a held key forever, so only those that do */
                if (a == KEY_OK && e.key.repeat && g_seen_keyup) continue;
                /* On screens that offer a long press, OK waits for the release (or the hold).
                 * Remotes that never send releases keep the old instant OK. */
                if (a == KEY_OK && g_seen_keyup && top && top->long_ok && !(g_app.pip && g_pip_focus)) {
                    if (!e.key.repeat) { g_ok_down = 1; g_ok_long = 0; g_ok_t0 = SDL_GetTicks() - (g_dev_hold ? 500 : 0); g_dev_hold = 0; g_app.ok_hold = 0.001f; }
                    continue;
                }
                dispatch_key(a);
            }
            else if (e.type == SDL_WINDOWEVENT && e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
                video_set_display_rect(0, 0, e.window.data1, e.window.data2);
                if (g_app.pip) player_set_pip(g_app.pip, 1);
                app_redraw();
            } else if (e.type == SDL_WINDOWEVENT && e.window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
                /* An overlay took the remote (the TV's own settings, volume, input list):
                 * the app is still on screen, so the video plays on. Progress is saved in
                 * case it turns into Home after all. */
                LOGI("window: focus lost (overlay), playback continues");
                player_save_progress_quiet();
                app_library_flush(1);
                store_save();
                store_flush();
            } else if (e.type == SDL_WINDOWEVENT && (e.window.event == SDL_WINDOWEVENT_HIDDEN || e.window.event == SDL_WINDOWEVENT_MINIMIZED)) {
                LOGI("window: %s, pausing", e.window.event == SDL_WINDOWEVENT_HIDDEN ? "hidden" : "minimized");
                player_save_progress();   /* Home or another app: webOS may end us without notice later */
                video_app_background();
                relay_buffer_close();   /* the buffer's memory back to the TV while we are away */
                g_backgrounded = 1;
                mem_background(1);
                app_library_flush(1);
                store_save();
                store_flush();
            } else if (e.type == SDL_WINDOWEVENT && (e.window.event == SDL_WINDOWEVENT_FOCUS_GAINED || e.window.event == SDL_WINDOWEVENT_SHOWN || e.window.event == SDL_WINDOWEVENT_RESTORED)) {
                if (g_backgrounded) { LOGI("window: back on screen"); video_app_foreground(); g_backgrounded = 0; app_library_foreground(); app_update_foreground(); mem_background(0); }
                app_redraw();
            } else if (e.type == SDL_APP_LOWMEMORY) {
                relay_buffer_close();   /* the first thing to give back: the source plays on directly */
                mem_emergency();
            } else if (e.type == SDL_APP_WILLENTERBACKGROUND) {
                player_save_progress();
                video_app_background();
                relay_buffer_close();
                g_backgrounded = 1;
                mem_background(1);
                app_library_flush(1);
                store_save();
                store_flush();   /* on flash before webOS may end us */
            } else if (e.type == SDL_APP_DIDENTERFOREGROUND) {
                video_app_foreground();
                if (g_backgrounded) { app_update_foreground(); app_library_foreground(); }   /* whichever of these events comes first */
                g_backgrounded = 0;
                mem_background(0);
                app_redraw();
            }
            app_redraw();
        }
        Uint32 now = SDL_GetTicks();
        float dt = (now - prev) / 1000.0f;
        if (dt > 0.1f) dt = 0.1f;
        { static float fixed = -1; if (fixed < 0) { const char *f = getenv("SNTV_FRAME_DT"); fixed = f ? (float)atof(f) : 0; } if (fixed > 0) dt = fixed; }   /* test hook: animations frame by frame */
        prev = now;
        g_app.time += dt;

        /* Holding OK fills the screen's "Hold for options" hint; past 500 ms it reads
         * "Release for options" and the release opens them. A release that never comes
         * (a lost key-up) opens them after 2.5 s. */
        if (g_ok_down) {
            Uint32 held = SDL_GetTicks() - g_ok_t0;
            g_app.ok_hold = held >= 500 ? 1 : 0.001f + held / 500.0f;
            if (held >= 500) g_ok_long = 1;
            if (held >= 2500) { g_ok_down = 0; g_app.ok_hold = 0; dispatch_key(KEY_LONG_OK); }
            app_redraw();
        }
        http_pump();
        tracking_tick();
        { static int was_online = 1; int online = http_online(); if (online && !was_online) app_toast("Back online"); if (online != was_online) app_redraw(); was_online = online; }
        image_pump();
        image_frame_tick();
        video_update();

        if (image_busy() || http_pending_replies() || g_app.time < g_app.toast_until) app_redraw();
        { static time_t minute; time_t m = time(NULL) / (g_app.island_style == ISLAND_SECONDS ? 1 : 60); if (m != minute) { minute = m; app_redraw(); } }   /* the clock moves */

        Screen *top = app_top();
        if (top && top->update) top->update(top, dt);
        bgtrailer_frame(dt);
        if (g_app.pip) { player_pip_update(g_app.pip, dt); if (g_app.pip && player_pip_done(g_app.pip)) app_pip_close(); }
        top = app_top();   /* update() may have popped or replaced the screen */

        if (!g_backgrounded) breaks_tick(dt);   /* behind the TV's Home: no chime, no break */
        newep_tick();
        app_library_flush(0);
        mem_tick(dt);
        app_update_check(dt);
        app_open_links();
        { const Screen *t = app_top(); int playing = t && strcmp(t->name, "player") == 0; app_library_tick(g_app.pip || playing); mem_playback(playing && !g_app.pip); }
        top = app_top();
        /* Screensaver after a quiet spell, never over a playing video. */
        if (top && !g_backgrounded && ambient_due(top)) { app_push(screen_ambient_create()); top = app_top(); }   /* never behind the TV's Home (the fireplace has sound) */

        /* Idle frame skipping: when nothing animates or arrives, don't redraw at all.
         * Keeps old TVs cool and leaves the CPU to the network threads. Dev hooks and
         * the first second always render. */
        int must_draw = g_app.need_redraw || frame < 60 || getenv("SNTV_SCREENSHOT");
        if (must_draw) {
            g_app.need_redraw = 0;
            gfx_begin_frame(g_app.oled ? RGBA(0, 0, 0, 0xFF) : THEME_BG);
            app_draw_screen(top, dt);
            int in_player = top && strcmp(top->name, "player") == 0;
            if (!g_app.pip) g_pip_focus = 0;
            if (g_app.pip && !(top && !strcmp(top->name, "ambient"))) player_pip_draw(g_app.pip, g_pip_focus);   /* not a still frame over the screensaver */
            if (top && strcmp(top->name, "break") != 0 && strcmp(top->name, "pin") != 0) breaks_draw_banner();
            island_draw(in_player, top && !in_player && strcmp(top->name, "ambient") != 0);   /* the player has its clock in its controls */
            intro_draw();
            dev_hooks(win, frame++);
            Uint32 swap_at = SDL_GetTicks();
            gfx_end_frame();
            Uint32 end = SDL_GetTicks(), spent = end - now;
            float fill, heavy;
            gfx_frame_fill(&fill, &heavy);
            perf_frame(top ? top->name : "none", (float)(swap_at - now), (float)spent, fill);
            if (g_perf_fill && frame % 30 == 0) LOGI("perf: %s fills %.2f screens (%.2f blurs, depth, shadows), %d calls, %u ms", top ? top->name : "none", fill, heavy, gfx_draw_calls(), swap_at - now);
            perf_mark("first frame");
            perf_shown();
            app_note_frame((float)spent);
            if (frame == 120) LOGI("perf: %d draw calls, %u ms frame", gfx_draw_calls(), spent);
            /* A swap that waited for the display already paced the frame; sleeping on top of
             * it only delays the next key. Cap the rate only when the swap returned at once. */
            if (end - swap_at < 2 && spent < 16) SDL_Delay(16 - spent);
        } else {
            SDL_WaitEventTimeout(NULL, 25);   /* idle: wake the moment a key arrives, else poll the network at 40 Hz */
        }
        if (g_perf_fill) {   /* how often each screen draws when left alone: idle redraws cost a TV its keys' speed */
            static Uint32 t0; static int n0, f0;
            Uint32 t = SDL_GetTicks();
            if (!t0) { t0 = t; f0 = frame; }
            if (t - t0 >= 2000) { n0 = frame - f0; LOGI("perf: %d frames in %.1f s on %s", n0, (t - t0) / 1000.0, top ? top->name : "none"); t0 = t; f0 = frame; }
        }
    }

    Uint32 closing = SDL_GetTicks();
    relay_buffer_close();   /* its download stops at its next bytes; its memory goes now */
    app_shutdown();
#if defined(SNTV_PLATFORM_ANDROID)
    /* Android keeps the process (and every thread still in it) after SDL_main returns, so a
     * thread finishing a download after its memory is freed brought "Cuelight has stopped"
     * at each close. What must last is written, the watch progress delivered, and the
     * process ends here: the system frees the rest, the player included. */
    http_shutdown();   /* waits for the requests that must reach the server (watch progress) */
    store_save();
    store_shutdown();
    remove(alive);
    LOGI("main: closed in %u ms", SDL_GetTicks() - closing);
    log_shutdown();
    _exit(0);
#endif
    video_shutdown();
    audio_shutdown();
    phone_stop();
    image_shutdown();
    http_shutdown();
    font_shutdown();
    gfx_shutdown();
    store_save();
    store_shutdown();
    remove(alive);
    SDL_GL_DeleteContext(ctx);
    SDL_DestroyWindow(win);
    LOGI("main: closed in %u ms", SDL_GetTicks() - closing);
    SDL_Quit();
    log_shutdown();
    return 0;
}
