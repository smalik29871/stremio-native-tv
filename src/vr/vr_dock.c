/* The dock under the screen: Move and Resize are handles you hold the trigger on and drag; the rest are
 * buttons (the ones that step a size or a distance repeat while held). Drawn by the app's own renderer into a
 * swapchain of its own; vr_openxr.c places it, points at it and acts on it. */
#ifdef SNTV_VR

#include "vr.h"
#include "vr_internal.h"
#include "../platform/platform.h"
#include "../ui/gfx.h"
#include "../ui/font.h"
#include <GLES3/gl3.h>
#include <string.h>

static const struct { const char *label; float w; } B[DOCK_N] = {
    { "Move", 190 }, { "Smaller", 130 }, { "Bigger", 130 }, { "Nearer", 130 }, { "Farther", 130 },
    { "Curve", 120 }, { "View", 120 }, { "Cinema", 140 }, { "Center", 130 }, { "Resize", 180 },
};
#define GAP 8.0f
#define PAD 24.0f

static float bx(int i)
{
    float x = PAD;
    for (int k = 0; k < i; k++) x += B[k].w + GAP;
    /* the buttons fill the bar: spread what is left evenly */
    float total = PAD * 2 - GAP;
    for (int k = 0; k < DOCK_N; k++) total += B[k].w + GAP;
    float extra = (DOCK_W - total) / (DOCK_N - 1);
    return x + extra * i;
}

int vr_dock_at(float u, float v)
{
    float x = u * DOCK_W, y = v * DOCK_H;
    if (y < 0 || y > DOCK_H || x < 0 || x > DOCK_W) return -1;
    for (int i = 0; i < DOCK_N; i++) if (x >= bx(i) - GAP / 2 && x <= bx(i) + B[i].w + GAP / 2) return i;
    return -1;
}

void vr_dock_draw(int hover, int pressed, float alpha)
{
    gfx_set_alpha(alpha);
    float W = (float)SNTV_UI_W, H = (float)SNTV_UI_H;
    float ox = (W - DOCK_W) / 2, oy = (H - DOCK_H) / 2;
    gfx_shadow(ox, oy, DOCK_W, DOCK_H, 44, 26, RGBA(0, 0, 0, 0x90));
    gfx_rrect(ox, oy, DOCK_W, DOCK_H, 44, RGBA(0x12, 0x15, 0x1C, 0xEC));
    for (int i = 0; i < DOCK_N; i++) {
        float x = ox + bx(i), y = oy + 18, w = B[i].w, h = DOCK_H - 36;
        int handle = i == DOCK_MOVE || i == DOCK_RESIZE;
        Color fill = i == pressed ? RGBA(0x7A, 0xA8, 0xFF, 0xFF) : i == hover ? RGBA(0x4F, 0x8C, 0xFF, 0xFF) : handle ? RGBA(0x2C, 0x34, 0x46, 0xFF) : RGBA(0x24, 0x29, 0x35, 0xFF);
        gfx_rrect(x, y, w, h, h / 2, fill);
        const char *label = B[i].label;
        if (i == DOCK_CURVE && vr_pref(VR_PREF_CURVED)) label = "Flat";
        Color fg = i == hover || i == pressed ? RGBA(0xFF, 0xFF, 0xFF, 0xFF) : RGBA(0xD2, 0xD8, 0xE4, 0xFF);
        font_draw_center(label, x + w / 2, y + (h - font_height(FONT_S)) / 2, FONT_S | FONT_B, fg);
        if (handle) {   /* a handle is held and dragged: three small dots say so */
            for (int d = -1; d <= 1; d++) gfx_circle(x + w / 2 + d * 14, y + 14, 3.0f, RGBA(0xFF, 0xFF, 0xFF, 0x70));
        }
    }
    gfx_set_alpha(1);
}

void vr_dock_render(VrChain *c, int hover, int pressed, float alpha)
{
    if (!vr_chain_acquire(c)) return;
    gfx_begin_frame(RGBA(0, 0, 0, 0));
    glClearColor(0, 0, 0, 0); glClear(GL_COLOR_BUFFER_BIT);   /* see-through around the bar */
    vr_dock_draw(hover, pressed, alpha);
    gfx_end_frame();
    vr_chain_release(c);
}

#endif
