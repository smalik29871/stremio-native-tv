#include "app.h"
#include "http.h"
#include "image.h"
#include "log.h"
#include "store.h"
#include "platform/platform.h"
#include "ui/font.h"
#include "ui/gfx.h"
#include "ui/theme.h"
#include "video/video.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
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

static const char *data_dir(void)
{
    static char dir[1024];
    char *pref = SDL_GetPrefPath("stremio", "nativetv");
    if (pref && dir_writable(pref)) { snprintf(dir, sizeof dir, "%s", pref); SDL_free(pref); return dir; }
    if (pref) SDL_free(pref);
    const char *home = getenv("HOME");
    snprintf(dir, sizeof dir, "%s/.%s", home ? home : "/tmp", SNTV_APP_ID);
    mkdir(dir, 0755);
    if (dir_writable(dir)) return dir;
    snprintf(dir, sizeof dir, "/tmp/%s", SNTV_APP_ID);
    mkdir(dir, 0755);
    return dir;
}

/* Artwork budget follows the TV's RAM, like Nuvio's normal/highcache packages. */
static size_t image_budget(void)
{
    FILE *f = fopen("/proc/meminfo", "r");
    long kb = 0;
    if (f) { char line[128]; while (fgets(line, sizeof line, f)) if (sscanf(line, "MemTotal: %ld kB", &kb) == 1) break; fclose(f); }
    size_t mb = kb / 1024;
    if (mb == 0) return 0;                       /* unknown: keep the platform default */
    if (mb < 1024) return 40u << 20;
    if (mb < 2048) return 96u << 20;
    return 192u << 20;
}

/* Bundled Inter (regular + bold) next to the binary, else system fallbacks. */
static int find_fonts(char *regular, char *bold, size_t n)
{
    char *base = SDL_GetBasePath();
    const char *bases[3] = { base ? base : "", "", NULL };
    for (int b = 0; bases[b]; b++) {
        snprintf(regular, n, "%sassets/fonts/Inter-Medium.otf", bases[b]);
        snprintf(bold, n, "%sassets/fonts/Inter-Bold.otf", bases[b]);
        FILE *f = fopen(regular, "rb");
        if (f) { fclose(f); if (base) SDL_free(base); return 0; }
    }
    if (base) SDL_free(base);
    static const char *sys[][2] = {
        { "/usr/share/fonts/opentype/inter/Inter-Medium.otf", "/usr/share/fonts/opentype/inter/Inter-Bold.otf" },
        { "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf" },
        { "/usr/share/fonts/dejavu/DejaVuSans.ttf", "/usr/share/fonts/dejavu/DejaVuSans-Bold.ttf" },
    };
    for (size_t i = 0; i < sizeof sys / sizeof sys[0]; i++) {
        FILE *f = fopen(sys[i][0], "rb");
        if (f) { fclose(f); snprintf(regular, n, "%s", sys[i][0]); snprintf(bold, n, "%s", sys[i][1]); return 0; }
    }
    return -1;
}

static int ambient_due(const Screen *top)
{
    static float idle_after = -1;
    if (idle_after < 0) { const char *s = getenv("SNTV_AMBIENT_SECONDS"); idle_after = s ? (float)atof(s) : 0; }   /* test hook */
    float limit = idle_after > 0 ? idle_after : g_app.ambient_minutes * 60.0f;
    if (!g_app.ambient_minutes && idle_after <= 0) return 0;
    if (g_app.time - g_app.last_input < limit) return 0;
    if (strcmp(top->name, "ambient") == 0 || strcmp(top->name, "login") == 0) return 0;
    VideoInfo vi;
    video_info(&vi);
    if (vi.state == VIDEO_PLAYING || vi.state == VIDEO_LOADING) return 0;   /* full screen or in the corner */
    return 1;
}

/* Dev aid: SNTV_SCREENSHOT=/path/out.ppm dumps the framebuffer after
 * SNTV_SCREENSHOT_FRAMES frames (default 120) and exits. Keys can be scripted with
 * SNTV_KEYS="down,right,ok,..." (one key every 20 frames) for headless UI checks. */
static void dev_hooks(SDL_Window *win, int frame)
{
    static const struct { const char *n; SDL_Keycode k; } keys[] = {
        { "up", SDLK_UP }, { "down", SDLK_DOWN }, { "left", SDLK_LEFT }, { "right", SDLK_RIGHT },
        { "ok", SDLK_RETURN }, { "back", SDLK_ESCAPE }, { "search", SDLK_F3 }, { "red", SDLK_F1 }, { "blue", SDLK_F4 },
        { "play", SDLK_p }, { "stop", SDLK_AUDIOSTOP }, { NULL, 0 } };
    const char *script = getenv("SNTV_KEYS");
    if (script && frame % 20 == 10) {
        int idx = frame / 20;
        char *copy = strdup(script);
        int i = 0;
        for (char *tok = strtok(copy, ","); tok; tok = strtok(NULL, ","), i++) {
            if (i != idx) continue;
            if (strncmp(tok, "t:", 2) == 0) {   /* typed text */
                SDL_Event e; memset(&e, 0, sizeof e);
                e.type = SDL_TEXTINPUT;
                snprintf(e.text.text, sizeof e.text.text, "%s", tok + 2);
                SDL_PushEvent(&e);
                continue;
            }
            for (int k = 0; keys[k].n; k++) if (strcmp(keys[k].n, tok) == 0) {
                SDL_Event e; memset(&e, 0, sizeof e);
                e.type = SDL_KEYDOWN; e.key.keysym.sym = keys[k].k; e.key.keysym.scancode = SDL_SCANCODE_UNKNOWN;
                SDL_PushEvent(&e);
            }
        }
        free(copy);
    }
    const char *shot = getenv("SNTV_SCREENSHOT");
    if (!shot) return;
    const char *fr = getenv("SNTV_SCREENSHOT_FRAMES");
    int target = fr ? atoi(fr) : 120;
    if (frame != target) return;
    int w, h;
    SDL_GL_GetDrawableSize(win, &w, &h);
    unsigned char *px = malloc((size_t)w * h * 4);
    gfx_flush();
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px);
    FILE *f = fopen(shot, "wb");
    if (f) {
        fprintf(f, "P6\n%d %d\n255\n", w, h);
        for (int y = h - 1; y >= 0; y--)
            for (int x = 0; x < w; x++) fwrite(px + ((size_t)y * w + x) * 4, 1, 3, f);
        fclose(f);
        LOGI("screenshot written to %s", shot);
    }
    free(px);
    g_app.quit = 1;
}

int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    int frame = 0;
#ifdef SNTV_PLATFORM_WEBOS
    /* Without these the remote's Back/Exit keys never reach the app. Must be set before the window exists. */
    SDL_SetHint("SDL_WEBOS_ACCESS_POLICY_KEYS_BACK", "true");
    SDL_SetHint("SDL_WEBOS_ACCESS_POLICY_KEYS_EXIT", "true");
    SDL_SetHint("SDL_WEBOS_CURSOR_SLEEP_TIME", "5000");
#endif
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_TIMER) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    const char *dir = data_dir();
    log_init(dir);
    LOGI("%s %s starting, data dir %s", SNTV_APP_NAME, SNTV_APP_VERSION, dir);
    store_init(dir);

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
#if defined(SNTV_PLATFORM_WEBOS) || defined(SNTV_PLATFORM_TIZEN)
    flags |= SDL_WINDOW_FULLSCREEN;
    SDL_DisplayMode dm;
    if (SDL_GetCurrentDisplayMode(0, &dm) == 0) { w = dm.w; h = dm.h; }
#else
    flags |= SDL_WINDOW_RESIZABLE;
#endif
    SDL_Window *win = SDL_CreateWindow(SNTV_APP_NAME, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, w, h, flags);
    if (!win) { LOGE("SDL_CreateWindow: %s", SDL_GetError()); return 1; }
    SDL_GLContext ctx = SDL_GL_CreateContext(win);
    if (!ctx) { LOGE("SDL_GL_CreateContext: %s", SDL_GetError()); return 1; }
    SDL_GL_SetSwapInterval(1);

    if (gfx_init(win) != 0) return 1;
    char font_regular[1200], font_bold[1200];
    if (find_fonts(font_regular, font_bold, sizeof font_regular) != 0 || font_init(font_regular, font_bold) != 0) {
        LOGE("no usable font found");
        return 1;
    }
    if (http_init() != 0) LOGE("network unavailable: libcurl missing");
    image_init(dir, image_budget());
    if (video_init(win) != 0) LOGW("video backend failed to initialise");
    LOGI("video backend: %s", video_backend_name());
    app_init();

    Uint32 prev = SDL_GetTicks();
    while (!g_app.quit) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            Screen *top = app_top();
            if (e.type == SDL_QUIT) g_app.quit = 1;
            else if (e.type == SDL_TEXTINPUT && top && top->text) top->text(top, e.text.text);
            else if (e.type == SDL_KEYDOWN) {
                KeyAction a = input_map(&e);
                g_app.last_input = g_app.time;
                if (a == KEY_EXIT) { g_app.quit = 1; continue; }
                if (a == KEY_NONE) continue;
                if (g_app.pip && (a == KEY_PLAY_PAUSE || a == KEY_PLAY)) { app_pip_restore(); continue; }
                if (g_app.pip && a == KEY_STOP) { app_pip_close(); continue; }
                if (top && top->key && top->key(top, a)) continue;
                if (a == KEY_BACK) app_pop();
            } else if (e.type == SDL_WINDOWEVENT && e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
                video_set_display_rect(0, 0, e.window.data1, e.window.data2);
                if (g_app.pip) player_set_pip(g_app.pip, 1);
                app_redraw();
            } else if (e.type == SDL_WINDOWEVENT && (e.window.event == SDL_WINDOWEVENT_FOCUS_LOST || e.window.event == SDL_WINDOWEVENT_HIDDEN || e.window.event == SDL_WINDOWEVENT_MINIMIZED)) {
                video_app_background();
            } else if (e.type == SDL_WINDOWEVENT && (e.window.event == SDL_WINDOWEVENT_FOCUS_GAINED || e.window.event == SDL_WINDOWEVENT_SHOWN || e.window.event == SDL_WINDOWEVENT_RESTORED)) {
                video_app_foreground();
                app_redraw();
            } else if (e.type == SDL_APP_WILLENTERBACKGROUND) {
                video_app_background();
                store_save();
            } else if (e.type == SDL_APP_DIDENTERFOREGROUND) {
                video_app_foreground();
                app_redraw();
            }
            app_redraw();
        }
        Uint32 now = SDL_GetTicks();
        float dt = (now - prev) / 1000.0f;
        if (dt > 0.1f) dt = 0.1f;
        prev = now;
        g_app.time += dt;

        http_pump();
        image_pump();
        image_frame_tick();
        video_update();

        if (image_busy() || g_app.time < g_app.toast_until) app_redraw();

        Screen *top = app_top();
        if (top && top->update) top->update(top, dt);
        if (g_app.pip) { player_pip_update(g_app.pip, dt); if (g_app.pip && player_pip_done(g_app.pip)) app_pip_close(); }
        top = app_top();   /* update() may have popped or replaced the screen */

        /* Screensaver after a quiet spell, never over a playing video. */
        if (top && ambient_due(top)) { app_push(screen_ambient_create()); top = app_top(); }

        /* Idle frame skipping: when nothing animates or arrives, don't redraw at all.
         * Keeps old TVs cool and leaves the CPU to the network threads. Dev hooks and
         * the first second always render. */
        int must_draw = g_app.need_redraw || frame < 60 || getenv("SNTV_SCREENSHOT");
        if (must_draw) {
            g_app.need_redraw = 0;
            gfx_begin_frame(RGBA(0x08, 0x07, 0x12, 0xFF));
            if (top && top->draw) top->draw(top);
            if (g_app.pip) player_pip_draw(g_app.pip);
            app_draw_toast();
            dev_hooks(win, frame++);
            gfx_end_frame();
            Uint32 spent = SDL_GetTicks() - now;
            app_note_frame((float)spent);
            if (frame == 120) LOGI("perf: %d draw calls, %u ms frame", gfx_draw_calls(), spent);
            if (spent < 16) SDL_Delay(16 - spent);
        } else {
            SDL_Delay(25);   /* poll input/network at 40 Hz while idle */
        }
    }

    app_shutdown();
    video_shutdown();
    image_shutdown();
    http_shutdown();
    font_shutdown();
    gfx_shutdown();
    store_save();
    store_shutdown();
    SDL_GL_DeleteContext(ctx);
    SDL_DestroyWindow(win);
    SDL_Quit();
    log_shutdown();
    return 0;
}
