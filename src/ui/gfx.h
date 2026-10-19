/* Batched GLES2 2D renderer.
 *
 * Every primitive is a quad in one big vertex stream; the shader mode and the
 * signed-distance parameters ride in the vertex format, so rounded rects, rings,
 * soft shadows, capsule lines, the play triangle and text all batch together and
 * only a texture change or a clip change splits a draw call. That matters far
 * more than shader cost on TV GPUs.
 *
 * Also: a "video hole" that writes alpha 0 so the TV's video plane shows through. */
#ifndef SNTV_GFX_H
#define SNTV_GFX_H
#include <stdint.h>
#include <SDL.h>

typedef uint32_t Color; /* 0xRRGGBBAA */
#define RGBA(r, g, b, a) ((Color)(((uint32_t)(r) << 24) | ((uint32_t)(g) << 16) | ((uint32_t)(b) << 8) | (uint32_t)(a)))
#define COLOR_ALPHA(c, a) (((c) & 0xFFFFFF00u) | ((uint32_t)(a) & 0xFFu))
#define COLOR_A(c) ((int)((c) & 0xFF))
Color color_mul_alpha(Color c, float k);
Color color_lerp(Color a, Color b, float t);

int  gfx_init(SDL_Window *win);
void gfx_shutdown(void);

/* Logical canvas is SNTV_UI_W x SNTV_UI_H; gfx scales to the real drawable. */
void gfx_begin_frame(Color clear);
void gfx_end_frame(void);
void gfx_flush(void);          /* submit pending quads (e.g. before glReadPixels) */
float gfx_scale(void);
void  gfx_drawable_size(int *w, int *h);
/* UI-canvas rectangle -> window pixels (for the video plane) */
void  gfx_ui_to_pixels(float x, float y, float w, float h, int *px, int *py, int *pw, int *ph);
int  gfx_draw_calls(void);   /* last frame, for the perf overlay */

/* plain */
void gfx_rect(float x, float y, float w, float h, Color c);
void gfx_rect_outline(float x, float y, float w, float h, float t, Color c);
void gfx_gradient_v(float x, float y, float w, float h, Color top, Color bottom);
void gfx_gradient_h(float x, float y, float w, float h, Color left, Color right);
void gfx_tex(unsigned tex, float x, float y, float w, float h, Color tint);
void gfx_tex_uv(unsigned tex, float x, float y, float w, float h, float u0, float v0, float u1, float v1, Color tint);
void gfx_tex_uv4(unsigned tex, float x, float y, float w, float h, float u0, float v0, float u1, float v1,
                 Color tl, Color tr, Color bl, Color br);
void gfx_glyph(unsigned alpha_tex, float x, float y, float w, float h, float u0, float v0, float u1, float v1, Color c);
void gfx_video_hole(float x, float y, float w, float h);

/* anti-aliased shapes */
void gfx_rrect(float x, float y, float w, float h, float r, Color c);
void gfx_rrect_grad_v(float x, float y, float w, float h, float r, Color top, Color bottom);
void gfx_rrect_grad_h(float x, float y, float w, float h, float r, Color left, Color right);
void gfx_rrect_ring(float x, float y, float w, float h, float r, float thickness, Color c);   /* ring just outside the rect */
void gfx_shadow(float x, float y, float w, float h, float r, float blur, Color c);
void gfx_circle(float cx, float cy, float radius, Color c);
void gfx_line(float x0, float y0, float x1, float y1, float thickness, Color c);             /* round caps */
void gfx_play_triangle(float x, float y, float w, float h, Color c);
/* textured with rounded corners; blur > 0 samples lower mips (needs a mipmapped texture) */
void gfx_rtex_uv(unsigned tex, float x, float y, float w, float h, float r,
                 float u0, float v0, float u1, float v1, Color tint, float blur);
/* Two-tone print of a texture: dark -> `ink` by luminance (alpha of ink fades it);
 * colour 0..1 blends back to the real colours (focus). */
void gfx_rtex_duo(unsigned tex, float x, float y, float w, float h, float r,
                  float u0, float v0, float u1, float v1, Color ink, float colour);
void gfx_tex_blur(unsigned tex, float x, float y, float w, float h, float u0, float v0, float u1, float v1, Color tint, float blur);

/* Everything drawn afterwards is shifted by (dx, dy) UI units (screen transitions). */
void gfx_set_translate(float dx, float dy);
/* Multiplies the alpha of everything drawn afterwards (1 = as drawn). */
void gfx_set_alpha(float a);
/* Scales everything drawn afterwards by s about (cx, cy) (zoom transitions); 1 = off. */
void gfx_set_scale(float s, float cx, float cy);
/* The transition layer around a whole screen: shift, scale about (cx, cy) and fade,
 * applied outside the screen's own translate/scale/alpha. gfx_set_layer(0,0,1,0,0,1) clears it. */
void gfx_set_layer(float dx, float dy, float s, float cx, float cy, float alpha);
void gfx_clip_push(float x, float y, float w, float h);
void gfx_clip_pop(void);

unsigned gfx_texture_rgba(const uint8_t *px, int w, int h);
unsigned gfx_texture_rgba_mip(const uint8_t *px, int w, int h);
unsigned gfx_texture_alpha(const uint8_t *px, int w, int h);
void     gfx_texture_destroy(unsigned tex);

#endif
