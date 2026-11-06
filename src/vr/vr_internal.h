#ifndef SNTV_VR_INTERNAL_H
#define SNTV_VR_INTERNAL_H
/* Between vr_openxr.c (the session, the swapchains, the layers) and vr_film.c (what is drawn into
 * them: the film, the room's light, the subtitles). */
#include <stdint.h>
#include "../video/video.h"

typedef struct VrChain {
    uint64_t handle;           /* the XrSwapchain */
    int n, w, h, ever;         /* images, size, and whether one has been released (so the layer has something to show) */
    unsigned fbo[8];
} VrChain;

int  vr_chain_make(VrChain *c, int w, int h);   /* a colour swapchain like the panel's; 0 when made */
int  vr_chain_acquire(VrChain *c);              /* waits for an image and binds it as the draw target: 1 when it is */
void vr_chain_release(VrChain *c);

/* The room around the screen is 1 + 4 surfaces: the wall behind it, the floor, the ceiling, the left and right walls. */
#define VR_ROOM_N 5
#define VR_ROOM_BACK_K 3.6f      /* the wall behind is this many screens wide and tall */
#define VR_ROOM_SPAN_K 2.6f      /* the floor, ceiling and side walls are this many screens across */
#define VR_ROOM_PX 512

/* vr_film.c: called each frame the session draws, with the screen's size in metres and the screen's distance */
void vr_film_step(const VideoInfo *vi, float screen_w, float screen_h, float dist);
VrChain *vr_film_chain(void);                   /* the film drawn by the app (the room's light is on and pictures arrive), else NULL */
VrChain *vr_room_chain(int i);                  /* a surface of the room's light, or NULL while there is none */
float vr_room_length(float dist);               /* how far the floor, ceiling and side walls reach toward you */
void vr_subs_begin(void);                       /* the player screen is about to draw: forget last frame's lines */
void vr_subs_render(void);                      /* after the panel is drawn: the captured lines into their own layer */
int  vr_subs_layer(VrChain **chain, int rect[4], float *cy_frac, float *w_frac);   /* 1 when there are lines on show: the swapchain, the rectangle of it, where its middle is down the canvas and how much of the canvas width it is (0..1) */
int  vr_subs_capture(const char *text, float bottom, int size, int style, float k, float width);   /* ui_subtitle_ex: 1 when taken instead of drawn */
float vr_subs_ratio(void);                      /* how much nearer than the screen they float: 1 on it, smaller closer */

/* The dock under the screen (vr_dock.c): a bar of handles and buttons that fades in when you point near the bottom
 * of the screen, to move and size it the way a window is in other headset apps. */
enum { DOCK_MOVE, DOCK_SMALLER, DOCK_BIGGER, DOCK_NEARER, DOCK_FARTHER, DOCK_CURVE, DOCK_VIEW, DOCK_CINEMA, DOCK_CENTER, DOCK_RESIZE, DOCK_N };
#define DOCK_W 1560              /* the bar's size in canvas pixels; the layer shows just this band of its swapchain */
#define DOCK_H 150
int  vr_dock_at(float u, float v);                         /* the button at that place of the bar (0..1 across and down), or -1 */
void vr_dock_render(VrChain *c, int hover, int pressed, float alpha);   /* the bar into the swapchain */

#endif
