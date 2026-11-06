#ifndef SNTV_VR_H
#define SNTV_VR_H

/* Cuelight in a headset (Quest 2 first): the app's own screens on a panel floating in front of
 * the user, drawn by the same renderer into an OpenXR swapchain, pointed at with a controller.
 * docs/VR-PLAN.md. Only built with SNTV_VR (the Quest APK); everywhere else these cost nothing. */

typedef struct {
    int have_pointer;          /* a controller's ray meets the panel */
    float x, y;                /* where, 0..1 across and down the panel */
    int down;                  /* the trigger is pressed (edge handling is the caller's) */
    int ok, back, menu;        /* A (or the stick pressed), B, the left menu button: 1 on the frame they were pressed */
    int up_k, down_k, left_k, right_k;   /* the thumbstick pushed a direction (repeating while held): 1 on those frames */
} VrInput;

#ifdef SNTV_VR
int  vr_init(void);                       /* 0 when a headset runtime answered, -1 otherwise (the app runs flat) */
int  vr_active(void);                     /* the session is running and the app is drawing for it */
int  vr_frame_begin(int dirty, int *w, int *h);   /* waits for the frame: -1 not running, 0 nothing to draw, 1 draw the panel now (its target is bound) */
void vr_frame_end(void);                  /* hands the panel and the frame to the compositor */
void vr_input(VrInput *in);               /* the controllers as of this frame */
int  vr_quit_requested(void);
void vr_shutdown(void);
#else
static inline int  vr_init(void) { return -1; }
static inline int  vr_active(void) { return 0; }
static inline int  vr_frame_begin(int dirty, int *w, int *h) { (void)dirty; (void)w; (void)h; return -1; }
static inline void vr_frame_end(void) {}
static inline void vr_input(VrInput *in) { (void)in; }
static inline int  vr_quit_requested(void) { return 0; }
static inline void vr_shutdown(void) {}
#endif

#endif
