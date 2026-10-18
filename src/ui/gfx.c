#include "gfx.h"
#include "../log.h"
#include "../platform/platform.h"
#include <GLES2/gl2.h>
#include <stdio.h>
#include <string.h>

static SDL_Window *g_win;
static GLuint g_prog, g_vbo;
static GLint g_a_pos, g_a_uv, g_a_col, g_u_mode, g_u_tex, g_u_proj;
static int g_draw_w, g_draw_h;
static float g_scale, g_off_x, g_off_y;

typedef struct { float x, y, u, v; float r, g, b, a; } Vtx;

static const char *VS =
    "attribute vec2 a_pos; attribute vec2 a_uv; attribute vec4 a_col;\n"
    "uniform mat4 u_proj; varying vec2 v_uv; varying vec4 v_col;\n"
    "void main(){ v_uv=a_uv; v_col=a_col; gl_Position=u_proj*vec4(a_pos,0.0,1.0); }\n";
static const char *FS =
    "precision mediump float;\n"
    "uniform int u_mode; uniform sampler2D u_tex; varying vec2 v_uv; varying vec4 v_col;\n"
    "void main(){\n"
    "  if (u_mode == 0) gl_FragColor = v_col;\n"
    "  else if (u_mode == 1) gl_FragColor = texture2D(u_tex, v_uv) * v_col;\n"
    "  else gl_FragColor = vec4(v_col.rgb, v_col.a * texture2D(u_tex, v_uv).a);\n"
    "}\n";

static GLuint compile(GLenum type, const char *src)
{
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(s, sizeof log, NULL, log);
        LOGE("gfx: shader compile failed: %s", log);
    }
    return s;
}

int gfx_init(SDL_Window *win)
{
    g_win = win;
    GLuint vs = compile(GL_VERTEX_SHADER, VS), fs = compile(GL_FRAGMENT_SHADER, FS);
    g_prog = glCreateProgram();
    glAttachShader(g_prog, vs);
    glAttachShader(g_prog, fs);
    glLinkProgram(g_prog);
    GLint ok = 0;
    glGetProgramiv(g_prog, GL_LINK_STATUS, &ok);
    if (!ok) { LOGE("gfx: program link failed"); return -1; }
    glDeleteShader(vs);
    glDeleteShader(fs);
    g_a_pos = glGetAttribLocation(g_prog, "a_pos");
    g_a_uv = glGetAttribLocation(g_prog, "a_uv");
    g_a_col = glGetAttribLocation(g_prog, "a_col");
    g_u_mode = glGetUniformLocation(g_prog, "u_mode");
    g_u_tex = glGetUniformLocation(g_prog, "u_tex");
    g_u_proj = glGetUniformLocation(g_prog, "u_proj");
    glGenBuffers(1, &g_vbo);
    LOGI("gfx: GL_RENDERER=%s GL_VERSION=%s", (const char *)glGetString(GL_RENDERER), (const char *)glGetString(GL_VERSION));
    return 0;
}

void gfx_shutdown(void)
{
    if (g_vbo) glDeleteBuffers(1, &g_vbo);
    if (g_prog) glDeleteProgram(g_prog);
    g_vbo = g_prog = 0;
}

float gfx_scale(void) { return g_scale; }

void gfx_begin_frame(void)
{
    SDL_GL_GetDrawableSize(g_win, &g_draw_w, &g_draw_h);
    float sx = (float)g_draw_w / SNTV_UI_W, sy = (float)g_draw_h / SNTV_UI_H;
    g_scale = sx < sy ? sx : sy;
    g_off_x = ((float)g_draw_w - SNTV_UI_W * g_scale) * 0.5f;
    g_off_y = ((float)g_draw_h - SNTV_UI_H * g_scale) * 0.5f;

    glViewport(0, 0, g_draw_w, g_draw_h);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_SCISSOR_TEST);
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glClearColor(0.05f, 0.05f, 0.08f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glUseProgram(g_prog);
    /* ortho in logical units, with letterbox offset applied in pixel space */
    float L = -g_off_x / g_scale, T = -g_off_y / g_scale;
    float Rr = L + (float)g_draw_w / g_scale, B = T + (float)g_draw_h / g_scale;
    float proj[16] = {
        2.0f / (Rr - L), 0, 0, 0,
        0, 2.0f / (T - B), 0, 0,
        0, 0, -1, 0,
        -(Rr + L) / (Rr - L), -(T + B) / (T - B), 0, 1 };
    glUniformMatrix4fv(g_u_proj, 1, GL_FALSE, proj);
    glUniform1i(g_u_tex, 0);
    glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
    glEnableVertexAttribArray(g_a_pos);
    glEnableVertexAttribArray(g_a_uv);
    glEnableVertexAttribArray(g_a_col);
    glVertexAttribPointer(g_a_pos, 2, GL_FLOAT, GL_FALSE, sizeof(Vtx), (void *)0);
    glVertexAttribPointer(g_a_uv, 2, GL_FLOAT, GL_FALSE, sizeof(Vtx), (void *)(2 * sizeof(float)));
    glVertexAttribPointer(g_a_col, 4, GL_FLOAT, GL_FALSE, sizeof(Vtx), (void *)(4 * sizeof(float)));
}

void gfx_end_frame(void)
{
    SDL_GL_SwapWindow(g_win);
}

static void quad(float x, float y, float w, float h, float u0, float v0, float u1, float v1,
                 Color c0, Color c1, Color c2, Color c3, int mode, GLuint tex)
{
    Color cs[4] = { c0, c1, c2, c3 };
    float xs[4] = { x, x + w, x, x + w }, ys[4] = { y, y, y + h, y + h };
    float us[4] = { u0, u1, u0, u1 }, vs[4] = { v0, v0, v1, v1 };
    Vtx v[4];
    for (int i = 0; i < 4; i++) {
        v[i].x = xs[i]; v[i].y = ys[i]; v[i].u = us[i]; v[i].v = vs[i];
        v[i].r = ((cs[i] >> 24) & 255) / 255.0f;
        v[i].g = ((cs[i] >> 16) & 255) / 255.0f;
        v[i].b = ((cs[i] >> 8) & 255) / 255.0f;
        v[i].a = (cs[i] & 255) / 255.0f;
    }
    glUniform1i(g_u_mode, mode);
    if (mode) { glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, tex); }
    glBufferData(GL_ARRAY_BUFFER, sizeof v, v, GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

void gfx_rect(float x, float y, float w, float h, Color c)
{ quad(x, y, w, h, 0, 0, 1, 1, c, c, c, c, 0, 0); }

void gfx_rect_outline(float x, float y, float w, float h, float t, Color c)
{
    gfx_rect(x - t, y - t, w + 2 * t, t, c);
    gfx_rect(x - t, y + h, w + 2 * t, t, c);
    gfx_rect(x - t, y, t, h, c);
    gfx_rect(x + w, y, t, h, c);
}

void gfx_gradient_v(float x, float y, float w, float h, Color top, Color bottom)
{ quad(x, y, w, h, 0, 0, 1, 1, top, top, bottom, bottom, 0, 0); }

void gfx_gradient_h(float x, float y, float w, float h, Color left, Color right)
{ quad(x, y, w, h, 0, 0, 1, 1, left, right, left, right, 0, 0); }

void gfx_tex(unsigned tex, float x, float y, float w, float h, Color tint)
{ quad(x, y, w, h, 0, 0, 1, 1, tint, tint, tint, tint, 1, tex); }

void gfx_tex_uv(unsigned tex, float x, float y, float w, float h, float u0, float v0, float u1, float v1, Color tint)
{ quad(x, y, w, h, u0, v0, u1, v1, tint, tint, tint, tint, 1, tex); }

void gfx_glyph(unsigned tex, float x, float y, float w, float h, float u0, float v0, float u1, float v1, Color c)
{ quad(x, y, w, h, u0, v0, u1, v1, c, c, c, c, 2, tex); }

void gfx_video_hole(float x, float y, float w, float h)
{
    /* Overwrite the framebuffer with transparent black so the compositor shows
     * the hardware video plane underneath. Needs an RGBA8 window surface. */
    glBlendFunc(GL_ONE, GL_ZERO);
    gfx_rect(x, y, w, h, RGBA(0, 0, 0, 0));
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
}

/* ---- clipping (stack of 8) ---- */
static struct { int x, y, w, h; } g_clip[8];
static int g_nclip;

static void apply_clip(void)
{
    if (!g_nclip) { glDisable(GL_SCISSOR_TEST); return; }
    glEnable(GL_SCISSOR_TEST);
    glScissor(g_clip[g_nclip - 1].x, g_clip[g_nclip - 1].y, g_clip[g_nclip - 1].w, g_clip[g_nclip - 1].h);
}

void gfx_clip_push(float x, float y, float w, float h)
{
    if (g_nclip >= 8) return;
    int px = (int)(g_off_x + x * g_scale), py = (int)(g_off_y + y * g_scale);
    int pw = (int)(w * g_scale + 0.5f), ph = (int)(h * g_scale + 0.5f);
    int gy = g_draw_h - (py + ph);  /* GL scissor origin is bottom-left */
    if (g_nclip) {   /* intersect with parent */
        int x0 = px > g_clip[g_nclip - 1].x ? px : g_clip[g_nclip - 1].x;
        int y0 = gy > g_clip[g_nclip - 1].y ? gy : g_clip[g_nclip - 1].y;
        int x1 = (px + pw) < (g_clip[g_nclip - 1].x + g_clip[g_nclip - 1].w) ? (px + pw) : (g_clip[g_nclip - 1].x + g_clip[g_nclip - 1].w);
        int y1 = (gy + ph) < (g_clip[g_nclip - 1].y + g_clip[g_nclip - 1].h) ? (gy + ph) : (g_clip[g_nclip - 1].y + g_clip[g_nclip - 1].h);
        px = x0; gy = y0; pw = x1 > x0 ? x1 - x0 : 0; ph = y1 > y0 ? y1 - y0 : 0;
    }
    g_clip[g_nclip].x = px; g_clip[g_nclip].y = gy; g_clip[g_nclip].w = pw; g_clip[g_nclip].h = ph;
    g_nclip++;
    apply_clip();
}

void gfx_clip_pop(void)
{
    if (g_nclip) g_nclip--;
    apply_clip();
}

/* ---- textures ---- */
static unsigned make_tex(GLenum fmt, const uint8_t *px, int w, int h)
{
    GLuint t;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glPixelStorei(GL_UNPACK_ALIGNMENT, fmt == GL_ALPHA ? 1 : 4);
    glTexImage2D(GL_TEXTURE_2D, 0, fmt, w, h, 0, fmt, GL_UNSIGNED_BYTE, px);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return t;
}
unsigned gfx_texture_rgba(const uint8_t *px, int w, int h) { return make_tex(GL_RGBA, px, w, h); }
unsigned gfx_texture_alpha(const uint8_t *px, int w, int h) { return make_tex(GL_ALPHA, px, w, h); }
void gfx_texture_destroy(unsigned tex) { GLuint t = tex; if (t) glDeleteTextures(1, &t); }
