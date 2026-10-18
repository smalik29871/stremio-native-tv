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

static const char *find_font(void)
{
    static const char *candidates[] = {
        "assets/fonts/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/TTF/DejaVuSans.ttf",
        NULL
    };
    static char path[1200];
    char *base = SDL_GetBasePath();
    if (base) {
        snprintf(path, sizeof path, "%sassets/fonts/DejaVuSans.ttf", base);
        SDL_free(base);
        FILE *f = fopen(path, "rb");
        if (f) { fclose(f); return path; }
    }
    for (int i = 0; candidates[i]; i++) {
        FILE *f = fopen(candidates[i], "rb");
        if (f) { fclose(f); return candidates[i]; }
    }
    return NULL;
}

/* Dev aid: SNTV_SCREENSHOT=/path/out.ppm dumps the framebuffer after
 * SNTV_SCREENSHOT_FRAMES frames (default 120) and exits. Keys can be scripted with
 * SNTV_KEYS="down,right,ok,..." (one key every 20 frames) for headless UI checks. */
static void dev_hooks(SDL_Window *win, int frame)
{
    static const struct { const char *n; SDL_Keycode k; } keys[] = {
        { "up", SDLK_UP }, { "down", SDLK_DOWN }, { "left", SDLK_LEFT }, { "right", SDLK_RIGHT },
        { "ok", SDLK_RETURN }, { "back", SDLK_ESCAPE }, { "search", SDLK_F3 }, { NULL, 0 } };
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
    const char *font = find_font();
    if (!font || font_init(font) != 0) { LOGE("no usable TTF font found"); return 1; }
    if (http_init() != 0) LOGE("network unavailable: libcurl missing");
    image_init();
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
                if (a == KEY_EXIT) { g_app.quit = 1; continue; }
                if (a == KEY_NONE) continue;
                if (top && top->key && top->key(top, a)) continue;
                if (a == KEY_BACK) app_pop();
            } else if (e.type == SDL_WINDOWEVENT && e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
                video_set_display_rect(0, 0, e.window.data1, e.window.data2);
            }
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

        Screen *top = app_top();
        if (top && top->update) top->update(top, dt);
        gfx_begin_frame();
        if (top && top->draw) top->draw(top);
        app_draw_toast();
        dev_hooks(win, frame++);
        gfx_end_frame();

        /* Old TVs: don't spin the CPU when vsync isn't honoured. */
        Uint32 spent = SDL_GetTicks() - now;
        if (spent < 16) SDL_Delay(16 - spent);
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
