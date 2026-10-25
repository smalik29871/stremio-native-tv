#include "gfx.h"
#include "../log.h"
#include "../platform/platform.h"
#include "../video/video.h"
#include <GLES2/gl2.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    float x, y, u, v, lx, ly;
    GLubyte r, g, b, a;
    float sx, sy, rad, soft, mode, extra;
} Vtx;

enum { MODE_SOLID = 0, MODE_TEX = 1, MODE_GLYPH = 2, MODE_TEX_BIAS = 3, MODE_RRECT = 4,
       MODE_RRECT_TEX = 5, MODE_RING = 6, MODE_TRI = 7, MODE_RRECT_TEX_BIAS = 8, MODE_RRECT_TEX_DUO = 9, MODE_DEPTH = 10 };
enum { MAXQ = 4096 };

static SDL_Window *g_win;
static GLuint g_prog, g_vbo, g_ibo;
static GLint g_a_pos, g_a_uv, g_a_loc, g_a_col, g_a_sdf, g_a_misc, g_u_tex, g_u_proj, g_u_depth;
static int g_draw_w, g_draw_h;
static float g_scale, g_off_x, g_off_y, g_aa;
static float g_tx, g_ty;   /* UI-space translation */
static float g_alpha = 1;  /* fades everything drawn (a row leaving the screen) */
static float g_zs = 1, g_zx, g_zy;   /* scale about (g_zx, g_zy), applied before the translation */
/* The layer: a second shift / scale / fade around everything a screen draws, owned by
 * screen transitions, so a screen resetting its own translate or alpha cannot undo it. */
static float g_ls = 1, g_lx, g_ly, g_ltx, g_lty, g_la = 1;
static Vtx g_v[MAXQ * 4];
static int g_nq, g_calls, g_last_calls;
static struct { int x, y, w, h; } g_clip[8];
static int g_nclip, g_clip_over;   /* pushes past the stack's depth, so pops stay balanced */
static float g_shade, g_heavy, g_last_shade, g_last_heavy;   /* pixels shaded this frame: all, and by blurs, depth and wide shadows */
static GLuint g_cur_tex;
static GLuint g_depth_tex;   /* gfx_depth: textured quads push into the picture by this map */
static float g_depth[5];

static const char *VS =
    "attribute vec2 a_pos;\n attribute vec2 a_uv;\n attribute vec2 a_loc;\n attribute vec4 a_col;\n"
    "attribute vec4 a_sdf;\n attribute vec2 a_misc;\n uniform mat4 u_proj;\n"
    "varying vec2 v_uv;\n varying vec2 v_loc;\n varying vec4 v_col;\n varying vec4 v_sdf;\n varying vec2 v_misc;\n"
    "void main(){ v_uv=a_uv; v_loc=a_loc; v_col=a_col; v_sdf=a_sdf; v_misc=a_misc;\n"
    "  gl_Position=u_proj*vec4(a_pos,0.0,1.0); }\n";

/* v_sdf = (size.x, size.y, radius, soft); v_misc = (mode, extra). */
static const char *FS =
    "#ifdef GL_FRAGMENT_PRECISION_HIGH\n precision highp float;\n#else\n precision mediump float;\n#endif\n"
    "uniform sampler2D u_tex;\n uniform sampler2D u_depth;\n"
    "varying vec2 v_uv;\n varying vec2 v_loc;\n varying vec4 v_col;\n varying vec4 v_sdf;\n varying vec2 v_misc;\n"
    "float sdbox(float inset){ vec2 p=(v_loc-0.5)*v_sdf.xy; vec2 q=abs(p)-(v_sdf.xy*0.5-vec2(inset+v_sdf.z));\n"
    "  return length(max(q,0.0))+min(max(q.x,q.y),0.0)-v_sdf.z; }\n"
    "float cover(float d){ return 1.0-smoothstep(-v_sdf.w,v_sdf.w,d); }\n"
    "void main(){\n"
    "  float m=v_misc.x;\n"
    "  if (m<0.5) gl_FragColor=v_col;\n"
    "  else if (m<1.5) gl_FragColor=texture2D(u_tex,v_uv)*v_col;\n"
    "  else if (m<2.5) gl_FragColor=vec4(v_col.rgb,v_col.a*texture2D(u_tex,v_uv).a);\n"
    "  else if (m<3.5) gl_FragColor=texture2D(u_tex,v_uv,v_misc.y)*v_col;\n"
    "  else if (m<4.5) gl_FragColor=vec4(v_col.rgb,v_col.a*cover(sdbox(v_misc.y)));\n"
    "  else if (m<5.5) { vec4 t=texture2D(u_tex,v_uv)*v_col; gl_FragColor=vec4(t.rgb,t.a*cover(sdbox(0.0))); }\n"
    "  else if (m<6.5) { float d=sdbox(0.0); gl_FragColor=vec4(v_col.rgb,v_col.a*cover(abs(d+v_misc.y*0.5)-v_misc.y*0.5)); }\n"
    "  else if (m<7.5) { vec2 p=v_loc*v_sdf.xy; float w=v_sdf.x, h=v_sdf.y;\n"
    "    vec2 n1=normalize(vec2(h*0.5,-w)); vec2 n2=normalize(vec2(h*0.5,w));\n"
    "    float d=max(-p.x, max(dot(p,n1), dot(p-vec2(0.0,h),n2)));\n"
    "    gl_FragColor=vec4(v_col.rgb,v_col.a*cover(d)); }\n"
    "  else if (m<8.5) { vec4 t=texture2D(u_tex,v_uv,v_misc.y)*v_col; gl_FragColor=vec4(t.rgb,t.a*cover(sdbox(0.0))); }\n"
    /* duotone: luminance maps black -> v_col.rgb; v_misc.y blends back to the real colours */
    "  else if (m<9.5) { vec4 t=texture2D(u_tex,v_uv); float l=dot(t.rgb,vec3(0.299,0.587,0.114));\n"
    "    vec3 duo=mix(vec3(0.02),v_col.rgb,smoothstep(0.05,0.95,l));\n"
    "    gl_FragColor=vec4(mix(duo,t.rgb,v_misc.y),t.a*v_col.a*cover(sdbox(0.0))); }\n"
    /* depth: what shows at v_uv comes from nearer the still point v_sdf.xy, the nearer the
     * further (depth white = near): found in a few steps, as the depth there decides it.
     * v_sdf.z how far in, v_sdf.w the depth that keeps its size; v_misc.y > 0 keeps only
     * what is nearer than it (people in front of the clock) */
    "  else { vec2 c=v_sdf.xy; vec2 q=v_uv;\n"
    "    for (int i=0;i<4;i++) q=c+(v_uv-c)/(1.0+v_sdf.z*(texture2D(u_depth,q).r-v_sdf.w));\n"
    "    vec4 t=texture2D(u_tex,q)*v_col;\n"
    "    if (v_misc.y>0.0) t.a*=smoothstep(v_misc.y-0.035,v_misc.y+0.035,texture2D(u_depth,q).r);\n"
    "    gl_FragColor=t; }\n"
    /* a half step of noise per pixel: a dark gradient or a blurred backdrop on an 8-bit
     * panel shows no bands (needs highp: mediump cannot hold the pixel positions) */
    "#ifdef GL_FRAGMENT_PRECISION_HIGH\n"
    "  gl_FragColor.rgb += (fract(52.9829189*fract(dot(gl_FragCoord.xy,vec2(0.06711056,0.00583715))))-0.5)/255.0;\n"
    "#endif\n"
    "}\n";

Color color_mul_alpha(Color c, float k)
{
    int a = (int)(COLOR_A(c) * k + 0.5f);
    if (a < 0) a = 0;
    if (a > 255) a = 255;
    return COLOR_ALPHA(c, a);
}

Color color_lerp(Color a, Color b, float t)
{
    if (t <= 0) return a;
    if (t >= 1) return b;
    Color out = 0;
    for (int s = 0; s < 32; s += 8) {
        float ca = (float)((a >> s) & 255), cb = (float)((b >> s) & 255);
        out |= (Color)((int)(ca + (cb - ca) * t + 0.5f) & 255) << s;
    }
    return out;
}

static GLuint compile(GLenum type, const char *src)
{
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
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
    if (!ok) {
        char log[1024];
        glGetProgramInfoLog(g_prog, sizeof log, NULL, log);
        LOGE("gfx: program link failed: %s", log);
        return -1;
    }
    glDeleteShader(vs);
    glDeleteShader(fs);
    g_a_pos = glGetAttribLocation(g_prog, "a_pos");
    g_a_uv = glGetAttribLocation(g_prog, "a_uv");
    g_a_loc = glGetAttribLocation(g_prog, "a_loc");
    g_a_col = glGetAttribLocation(g_prog, "a_col");
    g_a_sdf = glGetAttribLocation(g_prog, "a_sdf");
    g_a_misc = glGetAttribLocation(g_prog, "a_misc");
    g_u_tex = glGetUniformLocation(g_prog, "u_tex");
    g_u_proj = glGetUniformLocation(g_prog, "u_proj");
    g_u_depth = glGetUniformLocation(g_prog, "u_depth");
    glGenBuffers(1, &g_vbo);
    glGenBuffers(1, &g_ibo);
    static GLushort idx[MAXQ * 6];
    for (int q = 0; q < MAXQ; q++) {
        GLushort b = (GLushort)(q * 4);
        idx[q * 6 + 0] = b; idx[q * 6 + 1] = b + 1; idx[q * 6 + 2] = b + 2;
        idx[q * 6 + 3] = b + 2; idx[q * 6 + 4] = b + 1; idx[q * 6 + 5] = b + 3;
    }
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g_ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof idx, idx, GL_STATIC_DRAW);
    LOGI("gfx: GL_RENDERER=%s GL_VERSION=%s", (const char *)glGetString(GL_RENDERER), (const char *)glGetString(GL_VERSION));
    return 0;
}

void gfx_shutdown(void)
{
    if (g_vbo) glDeleteBuffers(1, &g_vbo);
    if (g_ibo) glDeleteBuffers(1, &g_ibo);
    if (g_prog) glDeleteProgram(g_prog);
    g_vbo = g_ibo = g_prog = 0;
}

float gfx_scale(void) { return g_scale; }
void gfx_pixels_to_ui(float px, float py, float *x, float *y) { *x = (px - g_off_x) / g_scale; *y = (py - g_off_y) / g_scale; }
void gfx_drawable_size(int *w, int *h) { *w = g_draw_w; *h = g_draw_h; }
void gfx_ui_to_pixels(float x, float y, float w, float h, int *px, int *py, int *pw, int *ph)
{
    *px = (int)(g_off_x + x * g_scale + 0.5f);
    *py = (int)(g_off_y + y * g_scale + 0.5f);
    *pw = (int)(w * g_scale + 0.5f);
    *ph = (int)(h * g_scale + 0.5f);
}
int gfx_draw_calls(void) { return g_last_calls; }
void gfx_frame_fill(float *screens, float *heavy)
{
    float px = g_draw_w > 0 && g_draw_h > 0 ? (float)g_draw_w * g_draw_h : 1;
    *screens = g_last_shade / px; *heavy = g_last_heavy / px;
}

static void flush(void)
{
    if (!g_nq) return;
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(sizeof(Vtx) * 4 * (size_t)g_nq), g_v, GL_STREAM_DRAW);
    glDrawElements(GL_TRIANGLES, g_nq * 6, GL_UNSIGNED_SHORT, 0);
    g_nq = 0;
    g_calls++;
}

int g_ui_w = SNTV_UI_BASE_W, g_ui_h = SNTV_UI_BASE_H;
static int g_handheld;   /* a phone: the screen's width holds 1920 units, so everything is drawn larger */
int g_ui_portrait;
int gfx_handheld(void) { return g_handheld; }
void gfx_set_handheld(int on) { g_handheld = on; }

/* Upright on a phone, units across: as many as make the text a comfortable size there (a
 * unit about 0.62 dp: body text near 15 dp), 600 to 780. The phone's density comes from the
 * app (SNTV_DENSITY, Android's dp scale); without it, 780. SNTV_PHONE_UNITS sets it outright. */
static int phone_units(void)
{
    static int units;
    if (units) return units;
    const char *u = getenv("SNTV_PHONE_UNITS"), *d = getenv("SNTV_DENSITY");
    units = 780;
    if (u && atoi(u) > 0) units = atoi(u);
    else if (d && atof(d) > 0.5) {
        int w = g_draw_w < g_draw_h ? g_draw_w : g_draw_h;   /* the short side: upright, its width */
        units = (int)(w / atof(d) / 0.62f + 0.5f);
        if (units < 600) units = 600;
        if (units > 780) units = 780;
    }
    LOGI("gfx: upright on a phone, %d units across", units);
    return units;
}

void gfx_begin_frame(Color clear)
{
    SDL_GL_GetDrawableSize(g_win, &g_draw_w, &g_draw_h);
    if (g_draw_w > 0 && g_draw_h > 0) {   /* the screen's own shape, never smaller than 16:9's */
        double aspect = (double)g_draw_w / g_draw_h;
        g_ui_w = aspect > 16.0 / 9.0 + 0.01 ? (int)(SNTV_UI_BASE_H * aspect + 0.5) : SNTV_UI_BASE_W;
        g_ui_h = aspect < 16.0 / 9.0 - 0.01 ? (int)(SNTV_UI_BASE_W / aspect + 0.5) : SNTV_UI_BASE_H;
        g_ui_portrait = g_handheld && aspect < 1;
        if (g_ui_portrait) { int pw = phone_units(); g_ui_w = pw; g_ui_h = (int)(pw / aspect + 0.5); }   /* upright: its own layouts */
        else if (g_handheld && g_ui_w > SNTV_UI_BASE_W) {   /* 20:9 is 1920 x 864: no shorter than 820, the layouts' floor */
            g_ui_h = (int)(SNTV_UI_BASE_W / aspect + 0.5);
            if (g_ui_h < 820) g_ui_h = 820;
            g_ui_w = (int)(g_ui_h * aspect + 0.5);
        }
    }
    float sx = (float)g_draw_w / SNTV_UI_W, sy = (float)g_draw_h / SNTV_UI_H;
    g_scale = sx < sy ? sx : sy;
    g_off_x = ((float)g_draw_w - SNTV_UI_W * g_scale) * 0.5f;
    g_off_y = ((float)g_draw_h - SNTV_UI_H * g_scale) * 0.5f;
    g_aa = 0.8f / g_scale;   /* one physical pixel of feathering, in logical units */

    glViewport(0, 0, g_draw_w, g_draw_h);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_SCISSOR_TEST);
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glClearColor(((clear >> 24) & 255) / 255.0f, ((clear >> 16) & 255) / 255.0f, ((clear >> 8) & 255) / 255.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glUseProgram(g_prog);
    float L = -g_off_x / g_scale, T = -g_off_y / g_scale;
    float Rr = L + (float)g_draw_w / g_scale, B = T + (float)g_draw_h / g_scale;
    float proj[16] = {
        2.0f / (Rr - L), 0, 0, 0,
        0, 2.0f / (T - B), 0, 0,
        0, 0, -1, 0,
        -(Rr + L) / (Rr - L), -(T + B) / (T - B), 0, 1 };
    glUniformMatrix4fv(g_u_proj, 1, GL_FALSE, proj);
    glUniform1i(g_u_tex, 0);
    if (g_u_depth >= 0) glUniform1i(g_u_depth, 1);
    glActiveTexture(GL_TEXTURE0);
    glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g_ibo);
    GLint attrs[6] = { g_a_pos, g_a_uv, g_a_loc, g_a_col, g_a_sdf, g_a_misc };
    for (int i = 0; i < 6; i++) if (attrs[i] >= 0) glEnableVertexAttribArray((GLuint)attrs[i]);
    glVertexAttribPointer((GLuint)g_a_pos, 2, GL_FLOAT, GL_FALSE, sizeof(Vtx), (void *)offsetof(Vtx, x));
    glVertexAttribPointer((GLuint)g_a_uv, 2, GL_FLOAT, GL_FALSE, sizeof(Vtx), (void *)offsetof(Vtx, u));
    glVertexAttribPointer((GLuint)g_a_loc, 2, GL_FLOAT, GL_FALSE, sizeof(Vtx), (void *)offsetof(Vtx, lx));
    glVertexAttribPointer((GLuint)g_a_col, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(Vtx), (void *)offsetof(Vtx, r));
    glVertexAttribPointer((GLuint)g_a_sdf, 4, GL_FLOAT, GL_FALSE, sizeof(Vtx), (void *)offsetof(Vtx, sx));
    glVertexAttribPointer((GLuint)g_a_misc, 2, GL_FLOAT, GL_FALSE, sizeof(Vtx), (void *)offsetof(Vtx, mode));
    g_nq = 0;
    g_calls = 0;
    g_shade = g_heavy = 0;
    g_cur_tex = 0;
    g_depth_tex = 0;
}

void gfx_flush(void) { flush(); }

void gfx_depth(unsigned depth_tex, float cx, float cy, float k, float focus, float fg)
{
    if (depth_tex != g_depth_tex) {
        flush();
        if (depth_tex) {
            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, depth_tex);
            glActiveTexture(GL_TEXTURE0);
        }
        g_depth_tex = depth_tex;
    }
    g_depth[0] = cx; g_depth[1] = cy; g_depth[2] = k; g_depth[3] = focus; g_depth[4] = fg;
}

void gfx_end_frame(void)
{
    flush();
    g_last_calls = g_calls;
    g_last_shade = g_shade; g_last_heavy = g_heavy;
    SDL_GL_SwapWindow(g_win);
}

/* Corner order: top-left, top-right, bottom-left, bottom-right. */
static void push(const float px[4], const float py[4], const float uv[8], const Color c[4],
                 float sx, float sy, float rad, float soft, int mode, float extra, GLuint tex)
{
    if (tex && tex != g_cur_tex) {
        flush();
        glBindTexture(GL_TEXTURE_2D, tex);
        g_cur_tex = tex;
    }
    if (g_nq == MAXQ) flush();
    static const float LX[4] = { 0, 1, 0, 1 }, LY[4] = { 0, 0, 1, 1 };
    Vtx *v = &g_v[g_nq * 4];
    int depth = g_depth_tex && mode == MODE_TEX;
    if (depth) mode = MODE_DEPTH;
    for (int i = 0; i < 4; i++) {
        float ix = g_zx + (px[i] - g_zx) * g_zs + g_tx, iy = g_zy + (py[i] - g_zy) * g_zs + g_ty;
        v[i].x = g_lx + (ix - g_lx) * g_ls + g_ltx; v[i].y = g_ly + (iy - g_ly) * g_ls + g_lty;
        v[i].u = uv[i * 2]; v[i].v = uv[i * 2 + 1];
        v[i].lx = LX[i]; v[i].ly = LY[i];
        v[i].r = (GLubyte)(c[i] >> 24); v[i].g = (GLubyte)(c[i] >> 16); v[i].b = (GLubyte)(c[i] >> 8);
        float al = g_alpha * g_la, zs = g_zs * g_ls;
        v[i].a = al < 1 ? (GLubyte)((c[i] & 0xFF) * al + 0.5f) : (GLubyte)c[i];
        v[i].sx = sx * zs; v[i].sy = sy * zs; v[i].rad = rad * zs; v[i].soft = soft * zs;
        v[i].mode = (float)mode; v[i].extra = mode == 4 || mode == 6 ? extra * zs : extra;   /* inset and ring width are lengths */
        if (depth) { v[i].sx = g_depth[0]; v[i].sy = g_depth[1]; v[i].rad = g_depth[2]; v[i].soft = g_depth[3]; v[i].extra = g_depth[4]; }
    }
    {   /* the pixels it covers, inside the clip: the GPU's share of the frame */
        float x0 = v[0].x, x1 = x0, y0 = v[0].y, y1 = y0;
        for (int i = 1; i < 4; i++) { x0 = fminf(x0, v[i].x); x1 = fmaxf(x1, v[i].x); y0 = fminf(y0, v[i].y); y1 = fmaxf(y1, v[i].y); }
        float l = g_off_x + x0 * g_scale, r = g_off_x + x1 * g_scale, t = g_draw_h - (g_off_y + y1 * g_scale), b = g_draw_h - (g_off_y + y0 * g_scale);
        float cl = 0, cb = 0, cr = (float)g_draw_w, ct = (float)g_draw_h;
        if (g_nclip) { cl = (float)g_clip[g_nclip - 1].x; cb = (float)g_clip[g_nclip - 1].y; cr = cl + g_clip[g_nclip - 1].w; ct = cb + g_clip[g_nclip - 1].h; }
        float w = fminf(r, cr) - fmaxf(l, cl), h = fminf(b, ct) - fmaxf(t, cb);
        if (w > 0 && h > 0) {
            g_shade += w * h;
            if (mode == MODE_DEPTH || ((mode == MODE_TEX_BIAS || mode == MODE_RRECT_TEX_BIAS) && extra > 0) || (mode == MODE_RRECT && soft > 8)) g_heavy += w * h;
        }
    }
    g_nq++;
}

static void aquad(float x, float y, float w, float h, float u0, float v0, float u1, float v1,
                  Color c0, Color c1, Color c2, Color c3, float rad, float soft, int mode, float extra, GLuint tex)
{
    if (w <= 0 || h <= 0) return;
    float px[4] = { x, x + w, x, x + w }, py[4] = { y, y, y + h, y + h };
    float uv[8] = { u0, v0, u1, v0, u0, v1, u1, v1 };
    Color c[4] = { c0, c1, c2, c3 };
    push(px, py, uv, c, w, h, rad, soft, mode, extra, tex);
}

static float clamp_r(float r, float w, float h)
{
    float m = (w < h ? w : h) * 0.5f;
    return r < 0 ? 0 : r > m ? m : r;
}

void gfx_rect(float x, float y, float w, float h, Color c) { aquad(x, y, w, h, 0, 0, 1, 1, c, c, c, c, 0, 0, MODE_SOLID, 0, 0); }

void gfx_rect_outline(float x, float y, float w, float h, float t, Color c)
{
    gfx_rect(x - t, y - t, w + 2 * t, t, c);
    gfx_rect(x - t, y + h, w + 2 * t, t, c);
    gfx_rect(x - t, y, t, h, c);
    gfx_rect(x + w, y, t, h, c);
}

void gfx_gradient_v(float x, float y, float w, float h, Color top, Color bottom)
{ aquad(x, y, w, h, 0, 0, 1, 1, top, top, bottom, bottom, 0, 0, MODE_SOLID, 0, 0); }

void gfx_gradient_h(float x, float y, float w, float h, Color left, Color right)
{ aquad(x, y, w, h, 0, 0, 1, 1, left, right, left, right, 0, 0, MODE_SOLID, 0, 0); }

void gfx_tex(unsigned tex, float x, float y, float w, float h, Color tint)
{ aquad(x, y, w, h, 0, 0, 1, 1, tint, tint, tint, tint, 0, 0, MODE_TEX, 0, tex); }

void gfx_tex_uv(unsigned tex, float x, float y, float w, float h, float u0, float v0, float u1, float v1, Color tint)
{ aquad(x, y, w, h, u0, v0, u1, v1, tint, tint, tint, tint, 0, 0, MODE_TEX, 0, tex); }

void gfx_tex_uv4(unsigned tex, float x, float y, float w, float h, float u0, float v0, float u1, float v1,
                 Color tl, Color tr, Color bl, Color br)
{ aquad(x, y, w, h, u0, v0, u1, v1, tl, tr, bl, br, 0, 0, MODE_TEX, 0, tex); }

void gfx_glyph(unsigned tex, float x, float y, float w, float h, float u0, float v0, float u1, float v1, Color c)
{ aquad(x, y, w, h, u0, v0, u1, v1, c, c, c, c, 0, 0, MODE_GLYPH, 0, tex); }

void gfx_tex_blur(unsigned tex, float x, float y, float w, float h, float u0, float v0, float u1, float v1, Color tint, float blur)
{ aquad(x, y, w, h, u0, v0, u1, v1, tint, tint, tint, tint, 0, 0, MODE_TEX_BIAS, blur, tex); }
void gfx_tex_blur4(unsigned tex, float x, float y, float w, float h, float u0, float v0, float u1, float v1, Color tl, Color tr, Color bl, Color br, float blur)
{ aquad(x, y, w, h, u0, v0, u1, v1, tl, tr, bl, br, 0, 0, MODE_TEX_BIAS, blur, tex); }

void gfx_rrect(float x, float y, float w, float h, float r, Color c)
{ aquad(x, y, w, h, 0, 0, 1, 1, c, c, c, c, clamp_r(r, w, h), g_aa, MODE_RRECT, 0, 0); }

void gfx_rrect_grad_v(float x, float y, float w, float h, float r, Color top, Color bottom)
{ aquad(x, y, w, h, 0, 0, 1, 1, top, top, bottom, bottom, clamp_r(r, w, h), g_aa, MODE_RRECT, 0, 0); }

void gfx_rrect_grad_h(float x, float y, float w, float h, float r, Color left, Color right)
{ aquad(x, y, w, h, 0, 0, 1, 1, left, right, left, right, clamp_r(r, w, h), g_aa, MODE_RRECT, 0, 0); }

void gfx_rrect_ring(float x, float y, float w, float h, float r, float t, Color c)
{
    float W = w + 2 * t, H = h + 2 * t;
    aquad(x - t, y - t, W, H, 0, 0, 1, 1, c, c, c, c, clamp_r(r + t, W, H), g_aa, MODE_RING, t, 0);
}

void gfx_shadow(float x, float y, float w, float h, float r, float blur, Color c)
{
    if (blur < 1) blur = 1;
    float W = w + 2 * blur, H = h + 2 * blur;
    aquad(x - blur, y - blur, W, H, 0, 0, 1, 1, c, c, c, c, clamp_r(r, w, h), blur, MODE_RRECT, blur, 0);
}

void gfx_circle(float cx, float cy, float radius, Color c) { gfx_rrect(cx - radius, cy - radius, 2 * radius, 2 * radius, radius, c); }

void gfx_line(float x0, float y0, float x1, float y1, float t, Color c)
{
    float dx = x1 - x0, dy = y1 - y0, len = sqrtf(dx * dx + dy * dy);
    if (len < 0.001f) { gfx_circle(x0, y0, t * 0.5f, c); return; }
    dx /= len; dy /= len;
    float h = t * 0.5f, nx = -dy * h, ny = dx * h;
    float ax = x0 - dx * h, ay = y0 - dy * h, bx = x1 + dx * h, by = y1 + dy * h;
    float px[4] = { ax - nx, bx - nx, ax + nx, bx + nx }, py[4] = { ay - ny, by - ny, ay + ny, by + ny };
    float uv[8] = { 0 };
    Color cs[4] = { c, c, c, c };
    push(px, py, uv, cs, len + t, t, h, g_aa, MODE_RRECT, 0, 0);
}

void gfx_play_triangle(float x, float y, float w, float h, Color c)
{ aquad(x, y, w, h, 0, 0, 1, 1, c, c, c, c, 0, g_aa, MODE_TRI, 0, 0); }

void gfx_rtex_uv(unsigned tex, float x, float y, float w, float h, float r, float u0, float v0, float u1, float v1, Color tint, float blur)
{
    aquad(x, y, w, h, u0, v0, u1, v1, tint, tint, tint, tint, clamp_r(r, w, h), g_aa,
          blur > 0 ? MODE_RRECT_TEX_BIAS : MODE_RRECT_TEX, blur, tex);
}

void gfx_rtex_duo(unsigned tex, float x, float y, float w, float h, float r, float u0, float v0, float u1, float v1, Color ink, float colour)
{
    aquad(x, y, w, h, u0, v0, u1, v1, ink, ink, ink, ink, clamp_r(r, w, h), g_aa, MODE_RRECT_TEX_DUO, colour, tex);
}

/* The desktop's decoded picture fitted into (x, y, w, h), bars round it as a TV's plane has:
 * where it lands, in UI units. 0 when the backend has none (a TV: a hole instead). */
static unsigned video_fit(float x, float y, float w, float h, float *fx, float *fy, float *fw, float *fh)
{
    int tw, th; float ta;
    unsigned t = video_frame_texture(&tw, &th, &ta);
    if (!t || ta <= 0) return 0;
    *fw = w; *fh = w / ta;
    if (*fh > h) { *fh = h; *fw = h * ta; }
    *fx = x + (w - *fw) / 2; *fy = y + (h - *fh) / 2;
    return t;
}

void gfx_video_hole(float x, float y, float w, float h)
{
    float fx, fy, fw, fh;
    unsigned t = video_fit(x, y, w, h, &fx, &fy, &fw, &fh);
    if (t) {   /* the desktop: its picture, drawn here */
        gfx_rect(x, y, w, h, RGBA(0, 0, 0, 0xFF));
        gfx_tex(t, fx, fy, fw, fh, RGBA(0xFF, 0xFF, 0xFF, 0xFF));
        return;
    }
    /* Overwrite the framebuffer with transparent black so the compositor shows
     * the hardware video plane underneath. Needs an RGBA8 window surface. */
    flush();
    glBlendFunc(GL_ONE, GL_ZERO);
    gfx_rect(x, y, w, h, RGBA(0, 0, 0, 0));
    flush();
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
}

/* Lets the video plane show through by a fraction per corner (0..1): what is already
 * drawn here fades toward transparent, so the video blends into the artwork's edges. */
void gfx_video_fade(float x, float y, float w, float h, float tl, float tr, float bl, float br)
{
    float fx, fy, fw, fh;
    unsigned t = video_fit(0, 0, SNTV_UI_W, SNTV_UI_H, &fx, &fy, &fw, &fh);   /* a TV's plane covers the screen */
    if (t) {   /* the desktop: that part of its picture, faded in by as much */
#define W(a) RGBA(0xFF, 0xFF, 0xFF, (int)(255 * (a)))
        gfx_tex_uv4(t, x, y, w, h, (x - fx) / fw, (y - fy) / fh, (x + w - fx) / fw, (y + h - fy) / fh, W(tl), W(tr), W(bl), W(br));
#undef W
        return;
    }
    flush();
    glBlendFuncSeparate(GL_ZERO, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE_MINUS_SRC_ALPHA);
#define F(a) RGBA(0, 0, 0, (int)(255 * (a)))
    aquad(x, y, w, h, 0, 0, 1, 1, F(tl), F(tr), F(bl), F(br), 0, 0, MODE_SOLID, 0, 0);
#undef F
    flush();
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
}

/* ---- clipping (stack of 8) ---- */

static void apply_clip(void)
{
    if (!g_nclip) { glDisable(GL_SCISSOR_TEST); return; }
    glEnable(GL_SCISSOR_TEST);
    glScissor(g_clip[g_nclip - 1].x, g_clip[g_nclip - 1].y, g_clip[g_nclip - 1].w, g_clip[g_nclip - 1].h);
}

void gfx_set_translate(float dx, float dy) { if (dx != g_tx || dy != g_ty) { flush(); g_tx = dx; g_ty = dy; } }
void gfx_get_translate(float *dx, float *dy) { *dx = g_tx; *dy = g_ty; }
void gfx_set_alpha(float a) { g_alpha = a < 0 ? 0 : a > 1 ? 1 : a; }
void gfx_set_scale(float s, float cx, float cy) { if (s != g_zs || cx != g_zx || cy != g_zy) { flush(); g_zs = s; g_zx = cx; g_zy = cy; } }
void gfx_set_layer(float dx, float dy, float s, float cx, float cy, float alpha)
{
    flush();
    g_ltx = dx; g_lty = dy; g_ls = s; g_lx = cx; g_ly = cy; g_la = alpha < 0 ? 0 : alpha > 1 ? 1 : alpha;
}

void gfx_clip_push(float x, float y, float w, float h)
{
    flush();
    x = g_zx + (x - g_zx) * g_zs + g_tx; y = g_zy + (y - g_zy) * g_zs + g_ty; w *= g_zs; h *= g_zs;
    x = g_lx + (x - g_lx) * g_ls + g_ltx; y = g_ly + (y - g_ly) * g_ls + g_lty; w *= g_ls; h *= g_ls;
    if (g_nclip >= 8) { g_clip_over++; return; }
    int px = (int)(g_off_x + x * g_scale), py = (int)(g_off_y + y * g_scale);
    int pw = (int)(w * g_scale + 0.5f), ph = (int)(h * g_scale + 0.5f);
    int gy = g_draw_h - (py + ph);
    if (g_nclip) {
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

/* A rect as it lands on screen: through the zoom, translate and layer transforms, cut to
 * the clip in force. 0 when nothing of it shows (touch targets in scrolled-off rows). */
int gfx_visible_rect(float *x, float *y, float *w, float *h)
{
    float rx = *x, ry = *y, rw = *w, rh = *h;
    rx = g_zx + (rx - g_zx) * g_zs + g_tx; ry = g_zy + (ry - g_zy) * g_zs + g_ty; rw *= g_zs; rh *= g_zs;
    rx = g_lx + (rx - g_lx) * g_ls + g_ltx; ry = g_ly + (ry - g_ly) * g_ls + g_lty; rw *= g_ls; rh *= g_ls;
    if (g_nclip) {
        const float cx = (g_clip[g_nclip - 1].x - g_off_x) / g_scale, cw = g_clip[g_nclip - 1].w / g_scale;
        const float ch = g_clip[g_nclip - 1].h / g_scale, cy = (g_draw_h - g_clip[g_nclip - 1].y - g_clip[g_nclip - 1].h - g_off_y) / g_scale;
        float x0 = rx > cx ? rx : cx, y0 = ry > cy ? ry : cy;
        float x1 = rx + rw < cx + cw ? rx + rw : cx + cw, y1 = ry + rh < cy + ch ? ry + rh : cy + ch;
        rx = x0; ry = y0; rw = x1 - x0; rh = y1 - y0;
    }
    *x = rx; *y = ry; *w = rw; *h = rh;
    return rw > 0 && rh > 0;
}

void gfx_clip_pop(void)
{
    flush();
    if (g_clip_over) { g_clip_over--; return; }
    if (g_nclip) g_nclip--;
    apply_clip();
}

/* ---- textures ---- */
static unsigned make_tex(GLenum fmt, const uint8_t *px, int w, int h, int mip)
{
    flush();
    GLuint t;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glPixelStorei(GL_UNPACK_ALIGNMENT, fmt == GL_ALPHA ? 1 : 4);
    glTexImage2D(GL_TEXTURE_2D, 0, (GLint)fmt, w, h, 0, fmt, GL_UNSIGNED_BYTE, px);
    /* GLES2 only mipmaps power-of-two textures; image.c resamples thumbnails accordingly. */
    if (mip && (w & (w - 1)) == 0 && (h & (h - 1)) == 0) {
        glGenerateMipmap(GL_TEXTURE_2D);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    } else {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    g_cur_tex = 0;
    return t;
}
unsigned gfx_texture_rgba(const uint8_t *px, int w, int h) { return make_tex(GL_RGBA, px, w, h, 0); }
void gfx_texture_update(unsigned tex, const uint8_t *px, int w, int h)
{
    if (!tex) return;
    flush();
    glBindTexture(GL_TEXTURE_2D, tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px);
    g_cur_tex = 0;
}
void gfx_texture_update_rows(unsigned tex, const uint8_t *rows, int w, int y0, int n)
{
    if (!tex || n <= 0) return;
    flush();
    glBindTexture(GL_TEXTURE_2D, tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, y0, w, n, GL_RGBA, GL_UNSIGNED_BYTE, rows);
    g_cur_tex = 0;
}
unsigned gfx_texture_rgba_mip(const uint8_t *px, int w, int h) { return make_tex(GL_RGBA, px, w, h, 1); }
unsigned gfx_texture_alpha(const uint8_t *px, int w, int h) { return make_tex(GL_ALPHA, px, w, h, 0); }

void gfx_texture_destroy(unsigned tex)
{
    GLuint t = tex;
    if (!t) return;
    if (t == g_cur_tex) { flush(); g_cur_tex = 0; }
    glDeleteTextures(1, &t);
}
