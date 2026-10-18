/* Tiny GLES2 2D renderer: solid rects, textured quads, alpha-only glyph quads,
 * scissor clipping, and a "video hole" that punches alpha 0 so the TV's video
 * plane shows through behind the GL surface (webOS/Tizen composite that way). */
#ifndef SNTV_GFX_H
#define SNTV_GFX_H
#include <stdint.h>
#include <SDL.h>

typedef uint32_t Color; /* 0xRRGGBBAA */
#define RGBA(r, g, b, a) ((Color)(((r) << 24) | ((g) << 16) | ((b) << 8) | (a)))
#define COLOR_ALPHA(c, a) (((c) & 0xFFFFFF00u) | ((a) & 0xFFu))

int  gfx_init(SDL_Window *win);
void gfx_shutdown(void);

/* Logical canvas is SNTV_UI_W x SNTV_UI_H; gfx scales to the real drawable. */
void gfx_begin_frame(void);
void gfx_end_frame(void);
float gfx_scale(void);

void gfx_rect(float x, float y, float w, float h, Color c);
void gfx_rect_outline(float x, float y, float w, float h, float t, Color c);
void gfx_gradient_v(float x, float y, float w, float h, Color top, Color bottom);
void gfx_gradient_h(float x, float y, float w, float h, Color left, Color right);
void gfx_tex(unsigned tex, float x, float y, float w, float h, Color tint);
void gfx_tex_uv(unsigned tex, float x, float y, float w, float h, float u0, float v0, float u1, float v1, Color tint);
void gfx_glyph(unsigned alpha_tex, float x, float y, float w, float h, float u0, float v0, float u1, float v1, Color c);
void gfx_video_hole(float x, float y, float w, float h);

void gfx_clip_push(float x, float y, float w, float h);
void gfx_clip_pop(void);

unsigned gfx_texture_rgba(const uint8_t *px, int w, int h);
unsigned gfx_texture_alpha(const uint8_t *px, int w, int h);
void     gfx_texture_destroy(unsigned tex);

#endif
