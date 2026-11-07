/* What the app draws for the headset besides its screens:
 *
 *  - The film itself, when the room's light is on. ExoPlayer decodes into a texture here instead of the
 *    compositor's own surface, so the app has the pictures: it draws them into a swapchain for the screen,
 *    and boils each one down to a 32 x 18 grid of colours (smoothed over time, on the GPU, nothing read back).
 *  - The room: the wall behind the screen, the floor, the ceiling and the two side walls, each a soft glow
 *    taken from the nearest edge of that grid, so the picture's light spills into the room as it would
 *    from a real screen (over your own room too, when the cameras are on).
 *  - The subtitles: the lines the player screen would draw on the panel are caught and drawn into a swapchain
 *    of their own, a layer that floats nearer to you than the screen (so on a bright or a 3D picture they
 *    stay clear of it) and looks the same size.
 *
 * With the room's light off the film goes straight to the compositor as before. If the texture path ever
 * shows no picture it turns itself off and the film goes that way. */
#ifdef SNTV_VR

#include "vr.h"
#include "vr_internal.h"
#include "../app.h"
#include "../log.h"
#include "../platform/platform.h"
#include "../ui/gfx.h"
#include "../ui/font.h"
#include "../ui/widgets.h"
#include <SDL.h>
#include <GLES3/gl3.h>
#include <math.h>
#include <string.h>
#include <stdio.h>

#define GL_TEXTURE_EXTERNAL_OES_ 0x8D65
#define FILM_W 2560
#define FILM_H 1440
#define COL_W 32
#define COL_H 18

static struct {
    int tried, failed, on, have, rooms_ok;
    GLuint ext, vao, vbo;
    GLuint col_tex[2], col_fbo[2]; int col_cur;
    GLuint p_film, p_col, p_room;
    GLint u_film_tm, u_film_tex, u_col_tm, u_col_tex, u_col_prev, u_col_k, u_col_cell, u_room_col, u_room_mode, u_room_geo;
    VrChain film, room[VR_ROOM_N];
    float tm[16];
    unsigned long frames;
    Uint32 col_ms, room_ms, play_ms;
    int room_dirty; float mw, mh, dist; int strength;
} F;

typedef struct { GLint prog, vao, abuf, tex0, tex1, active, fbo, vp[4]; GLboolean blend, scissor, depth, cull; } Saved;

static void save(Saved *s)
{
    glGetIntegerv(GL_CURRENT_PROGRAM, &s->prog);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &s->vao);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &s->abuf);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &s->active);
    glActiveTexture(GL_TEXTURE0); glGetIntegerv(GL_TEXTURE_BINDING_2D, &s->tex0);
    glActiveTexture(GL_TEXTURE1); glGetIntegerv(GL_TEXTURE_BINDING_2D, &s->tex1);
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &s->fbo);
    glGetIntegerv(GL_VIEWPORT, s->vp);
    s->blend = glIsEnabled(GL_BLEND); s->scissor = glIsEnabled(GL_SCISSOR_TEST); s->depth = glIsEnabled(GL_DEPTH_TEST); s->cull = glIsEnabled(GL_CULL_FACE);
}

static void restore(const Saved *s)
{
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, (GLuint)s->tex0);
    glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, (GLuint)s->tex1);
    glActiveTexture((GLenum)s->active);
    glBindVertexArray((GLuint)s->vao);
    glBindBuffer(GL_ARRAY_BUFFER, (GLuint)s->abuf);
    glUseProgram((GLuint)s->prog);
    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)s->fbo);
    glViewport(s->vp[0], s->vp[1], s->vp[2], s->vp[3]);
    if (s->blend) glEnable(GL_BLEND); else glDisable(GL_BLEND);
    if (s->scissor) glEnable(GL_SCISSOR_TEST); else glDisable(GL_SCISSOR_TEST);
    if (s->depth) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    if (s->cull) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
}

/* ---- shaders (GLSL ES 1.00: every GLES context takes them) ------------------------------ */

static const char *VS =
    "attribute vec2 p; varying vec2 v;\n"
    "void main() { v = p * 0.5 + 0.5; gl_Position = vec4(p, 0.0, 1.0); }\n";

/* the film, upright, into the screen's swapchain */
static const char *FS_FILM =
    "#extension GL_OES_EGL_image_external : require\n"
    "#ifdef GL_FRAGMENT_PRECISION_HIGH\nprecision highp float;\n#else\nprecision mediump float;\n#endif\n varying vec2 v; uniform samplerExternalOES tex; uniform mat4 tm;\n"
    "void main() { gl_FragColor = vec4(texture2D(tex, (tm * vec4(v, 0.0, 1.0)).xy).rgb, 1.0); }\n";

/* the picture as a grid of colours: each cell the mean of 36 taps, eased toward the last grid */
static const char *FS_COL =
    "#extension GL_OES_EGL_image_external : require\n"
    "precision mediump float; varying vec2 v; uniform samplerExternalOES tex; uniform sampler2D prev; uniform mat4 tm; uniform float k; uniform vec2 cell;\n"
    "void main() {\n"
    "  vec3 s = vec3(0.0);\n"
    "  for (int i = 0; i < 6; i++) for (int j = 0; j < 6; j++) {\n"
    "    vec2 o = (vec2(float(i), float(j)) + 0.5) / 6.0 - 0.5;\n"
    "    s += texture2D(tex, (tm * vec4(v + o * cell, 0.0, 1.0)).xy).rgb;\n"
    "  }\n"
    "  s /= 36.0;\n"
    "  gl_FragColor = vec4(mix(texture2D(prev, v).rgb, s, k), 1.0);\n"
    "}\n";

/* a surface of the room: mode 0 the wall behind the screen, 1 the floor, 2 the ceiling, 3 the left wall, 4 the right.
 * Light comes from the grid's nearest edge and falls off with the distance from the screen. geo: screen width and
 * height in metres, how far the floor, ceiling and walls reach, and how strong. Premultiplied, over black or the room. */
static const char *FS_ROOM =
    "precision mediump float; varying vec2 v; uniform sampler2D col; uniform float mode; uniform vec4 geo;\n"
    "vec3 edge(vec2 p) { return texture2D(col, clamp(p, vec2(0.02), vec2(0.98))).rgb; }\n"
    "void main() {\n"
    "  float mw = geo.x, mh = geo.y, L = geo.z, st = geo.w;\n"
    "  vec3 c; float f;\n"
    "  if (mode < 0.5) {\n"
    "    float s = (v.x - 0.5) * 3.6, t = (v.y - 0.5) * 3.6;\n"
    "    float dx = max(abs(s) - 0.5, 0.0) * mw, dy = max(abs(t) - 0.5, 0.0) * mh;\n"
    "    f = exp(-length(vec2(dx, dy)) / (0.30 * mw));\n"
    "    if (abs(s) < 0.5 && abs(t) < 0.5) f = 0.0;\n"
    "    c = edge(vec2(clamp(s, -0.5, 0.5) + 0.5, clamp(t, -0.5, 0.5) + 0.5));\n"
    "  } else if (mode < 2.5) {\n"
    "    float x = (v.x - 0.5) * mw * 2.6; float lat = max(abs(x) - mw * 0.5, 0.0);\n"
    "    float d = mode < 1.5 ? (1.0 - v.y) * L : v.y * L;\n"
    "    f = exp(-d / (0.28 * mw)) * exp(-lat / (0.22 * mw));\n"
    "    c = edge(vec2(clamp(x / mw, -0.5, 0.5) + 0.5, mode < 1.5 ? 0.0 : 1.0));\n"
    "  } else {\n"
    "    float y = (v.y - 0.5) * mh * 2.6; float vo = max(abs(y) - mh * 0.5, 0.0);\n"
    "    float d = mode < 3.5 ? (1.0 - v.x) * L : v.x * L;\n"
    "    f = exp(-d / (0.28 * mw)) * exp(-vo / (0.22 * mh));\n"
    "    c = edge(vec2(mode < 3.5 ? 0.0 : 1.0, clamp(y / mh, -0.5, 0.5) + 0.5));\n"
    "  }\n"
    "  float lum = dot(c, vec3(0.3, 0.59, 0.11));\n"
    "  float a = st * f * clamp(lum * 2.2, 0.0, 1.0);\n"
    "  c = clamp(mix(vec3(lum), c, 1.25), 0.0, 1.0);\n"
    "  gl_FragColor = vec4(c * a, a);\n"
    "}\n";

static GLuint compile(GLenum type, const char *src, const char *name)
{
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    GLint ok = 0; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) { char log[512] = ""; glGetShaderInfoLog(s, sizeof log, NULL, log); LOGW("vr: shader %s did not compile: %s", name, log); glDeleteShader(s); return 0; }
    return s;
}

static GLuint program(const char *fs, const char *name)
{
    GLuint v = compile(GL_VERTEX_SHADER, VS, "vertex"), f = compile(GL_FRAGMENT_SHADER, fs, name);
    if (!v || !f) return 0;
    GLuint p = glCreateProgram();
    glAttachShader(p, v); glAttachShader(p, f);
    glBindAttribLocation(p, 0, "p");
    glLinkProgram(p);
    GLint ok = 0; glGetProgramiv(p, GL_LINK_STATUS, &ok);
    glDeleteShader(v); glDeleteShader(f);
    if (!ok) { char log[512] = ""; glGetProgramInfoLog(p, sizeof log, NULL, log); LOGW("vr: program %s did not link: %s", name, log); glDeleteProgram(p); return 0; }
    return p;
}

static int small_target(GLuint *tex, GLuint *fbo)
{
    glGenTextures(1, tex);
    glBindTexture(GL_TEXTURE_2D, *tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, COL_W, COL_H, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glGenFramebuffers(1, fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, *fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, *tex, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) return -1;
    glViewport(0, 0, COL_W, COL_H);
    glClearColor(0, 0, 0, 1); glClear(GL_COLOR_BUFFER_BIT);
    return 0;
}

static int setup(void)
{
    F.tried = 1;
    F.p_film = program(FS_FILM, "film");
    F.p_col = program(FS_COL, "colours");
    F.p_room = program(FS_ROOM, "room");
    if (!F.p_film || !F.p_col || !F.p_room) return -1;
    F.u_film_tm = glGetUniformLocation(F.p_film, "tm"); F.u_film_tex = glGetUniformLocation(F.p_film, "tex");
    F.u_col_tm = glGetUniformLocation(F.p_col, "tm"); F.u_col_tex = glGetUniformLocation(F.p_col, "tex"); F.u_col_prev = glGetUniformLocation(F.p_col, "prev");
    F.u_col_k = glGetUniformLocation(F.p_col, "k"); F.u_col_cell = glGetUniformLocation(F.p_col, "cell");
    F.u_room_col = glGetUniformLocation(F.p_room, "col"); F.u_room_mode = glGetUniformLocation(F.p_room, "mode"); F.u_room_geo = glGetUniformLocation(F.p_room, "geo");

    static const float quad[8] = { -1, -1, 1, -1, -1, 1, 1, 1 };
    glGenVertexArrays(1, &F.vao); glGenBuffers(1, &F.vbo);
    glBindVertexArray(F.vao);
    glBindBuffer(GL_ARRAY_BUFFER, F.vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof quad, quad, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, (const void *)0);

    glGenTextures(1, &F.ext);
    glBindTexture(GL_TEXTURE_EXTERNAL_OES_, F.ext);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES_, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_EXTERNAL_OES_, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES_, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_EXTERNAL_OES_, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    if (small_target(&F.col_tex[0], &F.col_fbo[0]) || small_target(&F.col_tex[1], &F.col_fbo[1])) { LOGW("vr: the colour grid's framebuffer is incomplete"); return -1; }
    if (vr_chain_make(&F.film, FILM_W, FILM_H) != 0) return -1;
    F.rooms_ok = 1;
    for (int i = 0; i < VR_ROOM_N; i++) if (vr_chain_make(&F.room[i], VR_ROOM_PX, VR_ROOM_PX) != 0) { F.rooms_ok = 0; LOGW("vr: no swapchain for the room's surface %d: the film is lit by nothing", i); break; }
    return 0;
}

static void fail(const char *why)
{
    LOGW("vr: the room's light is off: %s", why);
    F.failed = 1;   /* for this run: the choice stays, and the next start tries again */
    if (F.on) { video_vr_tex_drop(); video_vr_direct(); F.on = 0; F.have = 0; }
    app_toast("Room light could not start (%s): the film shows as it is", why);
}

static void enter(void)
{
    Saved sv; save(&sv);
    int bad = (F.tried ? !F.p_film : setup() != 0);
    restore(&sv);
    if (bad) { fail("setup"); return; }
    video_vr_tex_init(F.ext, FILM_W, FILM_H);
    F.on = 1; F.have = 0; F.frames = 0; F.play_ms = 0; F.col_ms = 0; F.room_dirty = 1;
    LOGI("vr: the room's light is on: the film is drawn by the app");
}

static void leave(void)
{
    video_vr_tex_drop();
    video_vr_direct();   /* the compositor's own surface again */
    F.on = 0; F.have = 0;
    LOGI("vr: the room's light is off");
}

static void draw_quad(void) { glBindVertexArray(F.vao); glDrawArrays(GL_TRIANGLE_STRIP, 0, 4); }

static void flat_state(void)
{
    glDisable(GL_BLEND); glDisable(GL_DEPTH_TEST); glDisable(GL_SCISSOR_TEST); glDisable(GL_CULL_FACE);
}

static void film_pass(void)
{
    if (!vr_chain_acquire(&F.film)) return;
    flat_state();
    glUseProgram(F.p_film);
    glUniformMatrix4fv(F.u_film_tm, 1, GL_FALSE, F.tm);
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_EXTERNAL_OES_, F.ext);
    glUniform1i(F.u_film_tex, 0);
    draw_quad();
    vr_chain_release(&F.film);
}

static void colour_pass(float dt)
{
    int next = 1 - F.col_cur;
    glBindFramebuffer(GL_FRAMEBUFFER, F.col_fbo[next]);
    glViewport(0, 0, COL_W, COL_H);
    flat_state();
    glUseProgram(F.p_col);
    glUniformMatrix4fv(F.u_col_tm, 1, GL_FALSE, F.tm);
    glUniform1f(F.u_col_k, dt >= 1.0f ? 1.0f : 1.0f - expf(-7.0f * dt));
    glUniform2f(F.u_col_cell, 1.0f / COL_W, 1.0f / COL_H);
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_EXTERNAL_OES_, F.ext); glUniform1i(F.u_col_tex, 0);
    glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, F.col_tex[F.col_cur]); glUniform1i(F.u_col_prev, 1);
    draw_quad();
    F.col_cur = next;
}

static void room_pass(float mw, float mh, float dist)
{
    float strength = F.strength >= 2 ? 1.0f : 0.55f;
    for (int i = 0; i < VR_ROOM_N; i++) {
        if (!vr_chain_acquire(&F.room[i])) continue;
        flat_state();
        glClearColor(0, 0, 0, 0); glClear(GL_COLOR_BUFFER_BIT);
        glUseProgram(F.p_room);
        glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, F.col_tex[F.col_cur]); glUniform1i(F.u_room_col, 1);
        glUniform1f(F.u_room_mode, (float)i);
        glUniform4f(F.u_room_geo, mw, mh, vr_room_length(dist), strength);
        draw_quad();
        vr_chain_release(&F.room[i]);
    }
    F.room_ms = SDL_GetTicks(); F.room_dirty = 0; F.mw = mw; F.mh = mh; F.dist = dist;
}

void vr_film_step(const VideoInfo *vi, float mw, float mh, float dist)
{
    int light = vr_pref(VR_PREF_LIGHT);
    if (light && !F.failed && !F.on) enter();
    if (!light && F.on) leave();
    if (!F.on) return;
    Uint32 now = SDL_GetTicks();
    Saved sv; save(&sv);
    float m[16];
    if (video_vr_tex_update(m)) {
        memcpy(F.tm, m, sizeof m);
        F.have = 1; F.frames++;
        film_pass();
        if (now - F.col_ms >= 40) {
            colour_pass(F.col_ms ? (now - F.col_ms) / 1000.0f : 1.0f);
            F.col_ms = now; F.room_dirty = 1;
        }
    }
    if (light != F.strength) { F.strength = light; F.room_dirty = 1; }
    if (fabsf(mw - F.mw) > 0.01f || fabsf(mh - F.mh) > 0.01f || fabsf(dist - F.dist) > 0.01f) F.room_dirty = 1;
    if (F.have && F.rooms_ok && F.room_dirty && now - F.room_ms >= 50) room_pass(mw, mh, dist);
    restore(&sv);
    /* a film that plays and shows nothing here: back to the compositor's own surface */
    if (vi->state == VIDEO_PLAYING && vi->width > 0) { if (!F.play_ms) F.play_ms = now; if (F.frames == 0 && now - F.play_ms > 4500) fail("no pictures arrived"); }   /* only a film that is playing shows a picture; a paused one may not yet */
    else F.play_ms = 0;
}

VrChain *vr_film_chain(void) { return F.on && F.have && F.film.handle ? &F.film : NULL; }
VrChain *vr_room_chain(int i) { return F.on && F.have && F.rooms_ok && i >= 0 && i < VR_ROOM_N && vr_pref(VR_PREF_LIGHT) ? &F.room[i] : NULL; }
float vr_room_length(float dist) { return dist + 1.2f; }

/* ---- the subtitles ------------------------------------------------------------------------ */

typedef struct { char text[1024]; float bottom, k, width; int size, style; } SubLine;
static SubLine S[4];
static int S_n, S_rendering, S_visible, S_rect[4];
static float S_cy, S_wf;
static VrChain S_chain;

void vr_subs_begin(void) { S_n = 0; }

int vr_subs_capture(const char *text, float bottom, int size, int style, float k, float width)
{
    if (S_rendering || !vr_active() || vr_pref(VR_PREF_SUBS) == 0) return 0;
    if (S_n < 4) {
        SubLine *l = &S[S_n++];
        snprintf(l->text, sizeof l->text, "%s", text ? text : "");
        l->bottom = bottom; l->size = size; l->style = style; l->k = k; l->width = width;
    }
    return 1;
}

float vr_subs_ratio(void) { int p = vr_pref(VR_PREF_SUBS); return p == 2 ? 0.58f : p == 1 ? 0.78f : 1.0f; }

void vr_subs_render(void)
{
    if (vr_pref(VR_PREF_SUBS) == 0 || S_n == 0) { S_visible = 0; return; }
    float W = (float)SNTV_UI_W, H = (float)SNTV_UI_H;
    if (!S_chain.handle && vr_chain_make(&S_chain, (int)W, (int)H) != 0) { LOGW("vr: no swapchain for the subtitles"); S_visible = 0; return; }
    if (S_chain.w != (int)W || S_chain.h != (int)H) { S_visible = 0; return; }
    /* the band the lines fill (each grows up from its bottom line), centred on the canvas so that it does not
     * matter which way up the runtime counts the rows of the image */
    float top = H, bot = 0;
    for (int i = 0; i < S_n; i++) {
        float lh = ui_subtitle_line_h(S[i].size);
        float t = S[i].bottom - lh * 3.4f * S[i].k - 24, b = S[i].bottom + 32;
        if (t < top) top = t;
        if (b > bot) bot = b;
    }
    if (top < 0) top = 0;
    if (bot > H) bot = H;
    if (bot - top < 80) { float mid = (top + bot) / 2; top = mid - 40; bot = mid + 40; if (top < 0) { top = 0; bot = 80; } if (bot > H) { bot = H; top = H - 80; } }
    int bh = ((int)ceilf(bot - top) + 1) & ~1;
    if (bh > (int)H) bh = (int)H & ~1;
    float center = (top + bot) / 2, shift = H / 2 - center;
    int bw = ((int)(W * 0.84f)) & ~1;
    Saved sv; save(&sv);
    if (vr_chain_acquire(&S_chain)) {
        gfx_begin_frame(RGBA(0, 0, 0, 0));
        glClearColor(0, 0, 0, 0); glClear(GL_COLOR_BUFFER_BIT);   /* see-through: begin_frame clears to opaque */
        gfx_set_translate(0, shift);
        S_rendering = 1;
        for (int i = 0; i < S_n; i++) ui_subtitle_ex(S[i].text, S[i].bottom, S[i].size, S[i].style, S[i].k, S[i].width);
        S_rendering = 0;
        gfx_set_translate(0, 0);
        gfx_end_frame();
        vr_chain_release(&S_chain);
        S_rect[0] = ((int)W - bw) / 2; S_rect[1] = ((int)H - bh) / 2; S_rect[2] = bw; S_rect[3] = bh;
        S_cy = center / H; S_wf = (float)bw / W;
        S_visible = 1;
        static int said;
        if (said < 12) { said++; LOGI("vr: %d subtitle line(s) go to their own layer (band %dx%d, %.0f%% of the way to you)", S_n, bw, bh, (1 - vr_subs_ratio()) * 100); }
    }
    restore(&sv);
}

int vr_subs_layer(VrChain **chain, int rect[4], float *cy_frac, float *w_frac)
{
    if (!S_visible || vr_pref(VR_PREF_SUBS) == 0 || !S_chain.handle) return 0;
    *chain = &S_chain;
    for (int i = 0; i < 4; i++) rect[i] = S_rect[i];
    *cy_frac = S_cy; *w_frac = S_wf;
    return 1;
}

#endif
