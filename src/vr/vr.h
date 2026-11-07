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
    int play, view;            /* X: play or pause; Y: the next way to look at the film (3D, 180, 360) */
    int grabbing;              /* a grip is closed on the screen: the hand is carrying it */
    int key;                   /* a control box button asks for this key (a KeyAction: Back, Search, seek...), 0 none */
    int up_k, down_k, left_k, right_k;   /* the thumbstick pushed a direction (repeating while held): 1 on those frames */
} VrInput;

/* What the Cinema sheet keeps (vr.cfg): */
enum { VR_PREF_CURVED, VR_PREF_SIZE, VR_PREF_FOLLOW, VR_PREF_BED, VR_PREF_PASS, VR_PREF_DIST, VR_PREF_LIGHT, VR_PREF_SUBS, VR_PREF_EYES, VR_PREF_COUNT };

#ifdef SNTV_VR
int  vr_init(void);                       /* 0 when a headset runtime answered, -1 otherwise (the app runs flat) */
int  vr_active(void);                     /* the session is running and the app is drawing for it */
int  vr_frame_begin(int dirty, int *w, int *h);   /* waits for the frame: -1 not running, 0 nothing to draw, 1 draw the panel now (its target is bound) */
void vr_frame_end(void);                  /* hands the panel and the frame to the compositor */
void vr_input(VrInput *in);               /* the controllers as of this frame */
int  vr_quit_requested(void);
void *vr_video_surface(void);             /* the android.view.Surface the film is decoded into (a JNI global reference), NULL when there is none */
void vr_video_box(int x, int y, int w, int h);   /* where the player screen wants the picture, in panel pixels */
const char *vr_video_hint(const char *text);     /* what was loaded (its title or address): 3D, 180 and 360 tags in it choose the view; the view's name */
const char *vr_video_cycle(void);         /* the next view; its name */
const char *vr_video_view_name(void);     /* the view now */
void vr_recenter(void);                   /* the screen comes back in front of you */
int  vr_passthrough_available(void);
int  vr_pref(int which);                  /* a Cinema choice (VR_PREF_*) */
void vr_pref_set(int which, int value);
void vr_subs_begin(void);                 /* the player screen is about to draw */
void vr_subs_render(void);                /* after it has: the subtitle lines into their own layer */
int  vr_subs_capture(const char *text, float bottom, int size, int style, float k, float width);   /* ui_subtitle_ex: 1 when taken */
int  vr_cinema_is_open(void);             /* the control box (the left menu button) is open */
void vr_cinema_toggle(void);
void vr_cinema_close(void);
void vr_box_draw(float px, float py);     /* the box into the middle band of the canvas; where the pointer is (canvas units, -1 none) */
int  vr_box_tap(float x, float y);        /* canvas units; 1 when it was on a control (which has done its part) */
int  vr_box_hover(float x, float y);      /* a number for the control there (0 none), to know when the highlight moved */
unsigned vr_box_version(void);            /* changes when what the box shows does */
void vr_box_set_tab(int t);
int  vr_cinema_take_key(void);            /* the key a button asked for (0 none), taken */
void vr_screen_step(int id);              /* the screen smaller, bigger, nearer, farther (DOCK_* of vr_internal.h) */
void vr_shutdown(void);
#else
static inline int  vr_init(void) { return -1; }
static inline int  vr_active(void) { return 0; }
static inline int  vr_frame_begin(int dirty, int *w, int *h) { (void)dirty; (void)w; (void)h; return -1; }
static inline void vr_frame_end(void) {}
static inline void vr_input(VrInput *in) { (void)in; }
static inline int  vr_quit_requested(void) { return 0; }
static inline void *vr_video_surface(void) { return 0; }
static inline void vr_video_box(int x, int y, int w, int h) { (void)x; (void)y; (void)w; (void)h; }
static inline const char *vr_video_hint(const char *text) { (void)text; return ""; }
static inline const char *vr_video_cycle(void) { return ""; }
static inline int  vr_cinema_is_open(void) { return 0; }
static inline void vr_cinema_toggle(void) {}
static inline void vr_cinema_close(void) {}
static inline int  vr_pref(int which) { (void)which; return 0; }
static inline void vr_subs_begin(void) {}
static inline void vr_subs_render(void) {}
static inline int  vr_subs_capture(const char *t, float b, int s, int st, float k, float w) { (void)t; (void)b; (void)s; (void)st; (void)k; (void)w; return 0; }
static inline void vr_shutdown(void) {}
#endif

#endif
