/* Cuelight in a headset: an OpenXR session (the Khronos loader, from the Quest runtime) with one
 * panel, the app's own canvas drawn by gfx.c into a swapchain image and submitted as a quad layer.
 * A controller's aim ray meets the panel (vr_math.c) and the trigger is a tap on it.
 * Written from the OpenXR specification and the Khronos OpenXR-SDK (Apache-2.0); nothing here is
 * from Meta's samples. docs/VR-PLAN.md has the plan and what is checked and not. */
#ifdef SNTV_VR

#include "vr.h"
#include "vr_math.h"
#include "../log.h"
#include "../video/video.h"
#include "vr_internal.h"
#include <ctype.h>
#include <math.h>
#include <SDL.h>
#include <SDL_system.h>
#include <jni.h>
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define XR_USE_PLATFORM_ANDROID
#define XR_USE_GRAPHICS_API_OPENGL_ES
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#define PANEL_PX_W 1920
#define PANEL_PX_H 1080
#define PANEL_M_W 2.8f           /* a metre and a half wide per metre of distance: a cinema screen's worth of a field of view */
#define PANEL_M_H (PANEL_M_W * PANEL_PX_H / PANEL_PX_W)
#define PANEL_DIST 2.2f
#define VIDEO_PX_W 2560          /* the picture's surface: a quad of this size, the compositor scales it to what the eyes get */
#define VIDEO_PX_H 1440
#define PANEL_LOWER 0.15f        /* a little below eye level, where a head rests */
#define GL_FRAMEBUFFER_SRGB_EXT_ 0x8DB9
#define GL_SRGB8_ALPHA8_ 0x8C43
#define GL_RGBA8_ 0x8058
#define MAX_IMAGES 8

static struct {
    XrInstance inst;
    XrSystemId system;
    XrSession session;
    XrSpace local, view;
    XrSwapchain chain;
    int n_images;
    GLuint fbo[MAX_IMAGES];
    XrSwapchainImageOpenGLESKHR images[MAX_IMAGES];
    int running, focused, quit, in_frame, acquired, ever_released, srgb_write_off;
    XrFrameState frame;
    float panel_pos[3], panel_rot[4];
    int panel_placed;
    /* input */
    XrActionSet set;
    XrAction aim, trigger, stick, a_click, b_click, menu_click, stick_click;
    XrPath hand[2];
    XrSpace aim_space[2];
    VrInput input;
    int trig_was[2], pressed_was[6], stick_dir_was;
    double stick_repeat_at;
    int active_hand;
    PFN_xrGetOpenGLESGraphicsRequirementsKHR get_gles_req;
    /* where the panel is and how big: moved by hand (the grip), brought back by the menu button */
    float dist, scale, head_pos[3];
    int grab, grab_was;
    float grab_dist, grab_off[3];   /* how far the screen is from the hand, and where its middle is from the point the hand aims at (so it does not jump to the aim) */
    XrAction x_click, y_click, squeeze;
    int has_equirect, has_cylinder, has_pass, has_hands;
    int64_t sc_format;
    /* the dock under the screen */
    VrChain dock;
    float dock_alpha, rs_x, rs_x0, rs_scale0, two_sep0, two_scale0;
    int dock_hover, dock_pressed, dock_drag, dock_hold, dock_trig_was, dock_q, dock_dh, dock_dp, dock_dc, dock_failed, two;
    Uint32 dock_until;
    double dock_repeat_at;
    /* the control box: in front of you while open */
    VrChain box;
    unsigned box_ver;
    int box_hover_drawn, box_on, box_was_open, box_failed, stick_moved, box_placed;
    float box_pos[3], box_rot[4], box_px, box_py;
    float yaw, pitch;                 /* where the panel is from the head, as angles */
    int size_applied, dist_applied, bed_applied, follow_on, recenter, auto_key;
    /* a room to see (the passthrough camera), made when asked for */
    XrPassthroughFB pt;
    XrPassthroughLayerFB ptl;
    PFN_xrCreatePassthroughFB pt_create;
    PFN_xrDestroyPassthroughFB pt_destroy;
    PFN_xrCreatePassthroughLayerFB ptl_create;
    PFN_xrDestroyPassthroughLayerFB ptl_destroy;
} X;

/* The film: ExoPlayer draws into an Android surface that the compositor shows (no copy through
 * the app), as a quad behind the panel, or as the inside of a sphere for 180 and 360 degree films;
 * 3D films are two halves of the one surface, one for each eye. */
static struct {
    PFN_xrCreateSwapchainAndroidSurfaceKHR create;
    XrSwapchain chain;
    jobject surface;                 /* a global reference to the android.view.Surface */
    int ok;
    int bx, by, bw, bh;              /* where the player screen wants it, in panel pixels */
    int mode;                        /* an index into MODES */
} V;

typedef struct { const char *name; int proj, stereo; } ViewMode;   /* proj: 0 flat 1 180 degrees 2 360; stereo: 0 mono 1 side by side 2 top and bottom */
static const ViewMode MODES[] = {
    { "Flat", 0, 0 }, { "3D side by side", 0, 1 }, { "3D top and bottom", 0, 2 },
    { "180 degrees, 3D side by side", 1, 1 }, { "180 degrees, 3D top and bottom", 1, 2 }, { "180 degrees, flat", 1, 0 },
    { "360 degrees", 2, 0 }, { "360 degrees, 3D top and bottom", 2, 2 }, { "360 degrees, 3D side by side", 2, 1 },
};
#define N_MODES ((int)(sizeof MODES / sizeof MODES[0]))

static int xr_ok(XrResult r, const char *what)
{
    if (XR_SUCCEEDED(r)) return 1;
    char msg[XR_MAX_RESULT_STRING_SIZE] = "?";
    if (X.inst) xrResultToString(X.inst, r, msg);
    LOGW("vr: %s failed: %s (%d)", what, msg, (int)r);
    return 0;
}

static XrPath path_of(const char *s) { XrPath p = XR_NULL_PATH; xrStringToPath(X.inst, s, &p); return p; }

static XrAction make_action(XrActionType type, const char *name, const char *loc, int both_hands)
{
    XrActionCreateInfo ai = { XR_TYPE_ACTION_CREATE_INFO };
    ai.actionType = type;
    snprintf(ai.actionName, sizeof ai.actionName, "%s", name);
    snprintf(ai.localizedActionName, sizeof ai.localizedActionName, "%s", loc);
    ai.countSubactionPaths = both_hands ? 2 : 0;
    ai.subactionPaths = both_hands ? X.hand : NULL;
    XrAction a = XR_NULL_HANDLE;
    xr_ok(xrCreateAction(X.set, &ai, &a), name);
    return a;
}

static int setup_input(void)
{
    X.hand[0] = path_of("/user/hand/left");
    X.hand[1] = path_of("/user/hand/right");
    XrActionSetCreateInfo si = { XR_TYPE_ACTION_SET_CREATE_INFO };
    snprintf(si.actionSetName, sizeof si.actionSetName, "cuelight");
    snprintf(si.localizedActionSetName, sizeof si.localizedActionSetName, "Cuelight");
    if (!xr_ok(xrCreateActionSet(X.inst, &si, &X.set), "xrCreateActionSet")) return -1;
    X.aim = make_action(XR_ACTION_TYPE_POSE_INPUT, "aim", "Aim", 1);
    X.trigger = make_action(XR_ACTION_TYPE_FLOAT_INPUT, "trigger", "Trigger", 1);
    X.stick = make_action(XR_ACTION_TYPE_VECTOR2F_INPUT, "stick", "Thumbstick", 1);
    X.stick_click = make_action(XR_ACTION_TYPE_BOOLEAN_INPUT, "stick_click", "Thumbstick press", 1);
    X.a_click = make_action(XR_ACTION_TYPE_BOOLEAN_INPUT, "ok", "OK (A)", 0);
    X.b_click = make_action(XR_ACTION_TYPE_BOOLEAN_INPUT, "back", "Back (B)", 0);
    X.x_click = make_action(XR_ACTION_TYPE_BOOLEAN_INPUT, "playpause", "Play or pause (X)", 0);
    X.y_click = make_action(XR_ACTION_TYPE_BOOLEAN_INPUT, "view", "Change the view: 3D, 180, 360 (Y)", 0);
    X.squeeze = make_action(XR_ACTION_TYPE_FLOAT_INPUT, "grip", "Grab the screen (grip)", 1);
    X.menu_click = make_action(XR_ACTION_TYPE_BOOLEAN_INPUT, "menu", "Menu", 0);

    struct { XrAction a; const char *path; } b[] = {
        { X.aim, "/user/hand/left/input/aim/pose" }, { X.aim, "/user/hand/right/input/aim/pose" },
        { X.trigger, "/user/hand/left/input/trigger/value" }, { X.trigger, "/user/hand/right/input/trigger/value" },
        { X.stick, "/user/hand/left/input/thumbstick" }, { X.stick, "/user/hand/right/input/thumbstick" },
        { X.stick_click, "/user/hand/left/input/thumbstick/click" }, { X.stick_click, "/user/hand/right/input/thumbstick/click" },
        { X.a_click, "/user/hand/right/input/a/click" }, { X.x_click, "/user/hand/left/input/x/click" },
        { X.b_click, "/user/hand/right/input/b/click" }, { X.y_click, "/user/hand/left/input/y/click" },
        { X.squeeze, "/user/hand/left/input/squeeze/value" }, { X.squeeze, "/user/hand/right/input/squeeze/value" },
        { X.menu_click, "/user/hand/left/input/menu/click" } };
    XrActionSuggestedBinding sb[sizeof b / sizeof b[0]];
    for (size_t i = 0; i < sizeof b / sizeof b[0]; i++) { sb[i].action = b[i].a; sb[i].binding = path_of(b[i].path); }
    XrInteractionProfileSuggestedBinding ib = { XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING };
    ib.interactionProfile = path_of("/interaction_profiles/oculus/touch_controller");
    ib.suggestedBindings = sb; ib.countSuggestedBindings = (uint32_t)(sizeof sb / sizeof sb[0]);
    if (!xr_ok(xrSuggestInteractionProfileBindings(X.inst, &ib), "xrSuggestInteractionProfileBindings")) return -1;
    if (X.has_hands) {   /* hands without controllers: the pinch is the trigger, the hand's aim the ray */
        struct { XrAction a; const char *path; } hb[] = {
            { X.aim, "/user/hand/left/input/aim/pose" }, { X.aim, "/user/hand/right/input/aim/pose" },
            { X.trigger, "/user/hand/left/input/pinch_ext/value" }, { X.trigger, "/user/hand/right/input/pinch_ext/value" } };
        XrActionSuggestedBinding hs[4];
        for (int i = 0; i < 4; i++) { hs[i].action = hb[i].a; hs[i].binding = path_of(hb[i].path); }
        XrInteractionProfileSuggestedBinding hib = { XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING };
        hib.interactionProfile = path_of("/interaction_profiles/ext/hand_interaction_ext");
        hib.suggestedBindings = hs; hib.countSuggestedBindings = 4;
        if (!xr_ok(xrSuggestInteractionProfileBindings(X.inst, &hib), "hand bindings")) X.has_hands = 0;   /* controllers still work */
    }
    return 0;
}

static int attach_input(void)
{
    for (int h = 0; h < 2; h++) {
        XrActionSpaceCreateInfo sp = { XR_TYPE_ACTION_SPACE_CREATE_INFO };
        sp.action = X.aim; sp.subactionPath = X.hand[h]; sp.poseInActionSpace.orientation.w = 1;
        if (!xr_ok(xrCreateActionSpace(X.session, &sp, &X.aim_space[h]), "xrCreateActionSpace")) return -1;
    }
    XrSessionActionSetsAttachInfo at = { XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO };
    at.countActionSets = 1; at.actionSets = &X.set;
    return xr_ok(xrAttachSessionActionSets(X.session, &at), "xrAttachSessionActionSets") ? 0 : -1;
}

static int make_swapchain(void)
{
    uint32_t nf = 0;
    xrEnumerateSwapchainFormats(X.session, 0, &nf, NULL);
    int64_t *formats = nf ? calloc(nf, sizeof *formats) : NULL;   /* the Quest offers 70: sized by what it says, never a guess */
    if (!formats || !xr_ok(xrEnumerateSwapchainFormats(X.session, nf, &nf, formats), "xrEnumerateSwapchainFormats")) { free(formats); return -1; }
    /* The canvas is drawn in sRGB values as it always was. An sRGB swapchain with the sRGB write
     * conversion off keeps those bytes as they are and the compositor reads them as sRGB. */
    int64_t pick = 0; int have_srgb = 0;
    for (uint32_t i = 0; i < nf; i++) { if (formats[i] == GL_SRGB8_ALPHA8_) have_srgb = 1; }
    const char *ext = (const char *)glGetString(GL_EXTENSIONS);
    int can_off = ext && strstr(ext, "GL_EXT_sRGB_write_control") != NULL;
    if (have_srgb && can_off) { pick = GL_SRGB8_ALPHA8_; X.srgb_write_off = 1; }
    else for (uint32_t i = 0; i < nf; i++) if (formats[i] == GL_RGBA8_) pick = GL_RGBA8_;
    if (!pick && nf) pick = formats[0];
    X.sc_format = pick;
    LOGI("vr: swapchain format 0x%llx (%u offered, sRGB write control %s)", (unsigned long long)pick, nf, can_off ? "yes" : "no");
    free(formats);
    if (!pick) { LOGW("vr: the headset offered no usable picture format"); return -1; }

    XrSwapchainCreateInfo ci = { XR_TYPE_SWAPCHAIN_CREATE_INFO };
    ci.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
    ci.format = pick; ci.sampleCount = 1; ci.width = PANEL_PX_W; ci.height = PANEL_PX_H; ci.faceCount = 1; ci.arraySize = 1; ci.mipCount = 1;
    if (!xr_ok(xrCreateSwapchain(X.session, &ci, &X.chain), "xrCreateSwapchain")) return -1;
    uint32_t n = 0;
    xrEnumerateSwapchainImages(X.chain, 0, &n, NULL);
    if (n > MAX_IMAGES) n = MAX_IMAGES;
    for (uint32_t i = 0; i < n; i++) X.images[i].type = XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_ES_KHR;
    if (!xr_ok(xrEnumerateSwapchainImages(X.chain, n, &n, (XrSwapchainImageBaseHeader *)X.images), "xrEnumerateSwapchainImages")) return -1;
    X.n_images = (int)n;
    glGenFramebuffers((GLsizei)n, X.fbo);
    for (uint32_t i = 0; i < n; i++) {
        glBindFramebuffer(GL_FRAMEBUFFER, X.fbo[i]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, X.images[i].image, 0);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) { LOGW("vr: framebuffer %u of the swapchain is incomplete", i); return -1; }
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    LOGI("vr: %u swapchain images, %dx%d", n, PANEL_PX_W, PANEL_PX_H);
    return 0;
}

/* More swapchains like the panel's, for what the app draws besides the screens (the film, the room's light, the subtitles). */
int vr_chain_make(VrChain *c, int w, int h)
{
    memset(c, 0, sizeof *c);
    if (!X.session || !X.sc_format) return -1;
    XrSwapchainCreateInfo ci = { XR_TYPE_SWAPCHAIN_CREATE_INFO };
    ci.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
    ci.format = X.sc_format; ci.sampleCount = 1; ci.width = (uint32_t)w; ci.height = (uint32_t)h; ci.faceCount = 1; ci.arraySize = 1; ci.mipCount = 1;
    XrSwapchain sc = XR_NULL_HANDLE;
    if (!xr_ok(xrCreateSwapchain(X.session, &ci, &sc), "xrCreateSwapchain (extra)")) return -1;
    uint32_t n = 0;
    xrEnumerateSwapchainImages(sc, 0, &n, NULL);
    if (n > MAX_IMAGES) n = MAX_IMAGES;
    XrSwapchainImageOpenGLESKHR img[MAX_IMAGES];
    for (uint32_t i = 0; i < n; i++) { memset(&img[i], 0, sizeof img[i]); img[i].type = XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_ES_KHR; }
    if (!xr_ok(xrEnumerateSwapchainImages(sc, n, &n, (XrSwapchainImageBaseHeader *)img), "xrEnumerateSwapchainImages (extra)")) { xrDestroySwapchain(sc); return -1; }
    GLuint fbo[MAX_IMAGES];
    glGenFramebuffers((GLsizei)n, fbo);
    for (uint32_t i = 0; i < n; i++) {
        glBindFramebuffer(GL_FRAMEBUFFER, fbo[i]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, img[i].image, 0);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) { LOGW("vr: an extra swapchain's framebuffer is incomplete"); glBindFramebuffer(GL_FRAMEBUFFER, 0); xrDestroySwapchain(sc); return -1; }
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    c->handle = (uint64_t)sc; c->n = (int)n; c->w = w; c->h = h;
    for (uint32_t i = 0; i < n; i++) c->fbo[i] = fbo[i];
    return 0;
}

int vr_chain_acquire(VrChain *c)
{
    if (!c || !c->handle) return 0;
    uint32_t idx = 0;
    XrSwapchainImageAcquireInfo ai = { XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO };
    if (!xr_ok(xrAcquireSwapchainImage((XrSwapchain)c->handle, &ai, &idx), "xrAcquireSwapchainImage (extra)")) return 0;
    XrSwapchainImageWaitInfo wi = { XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO };
    wi.timeout = XR_INFINITE_DURATION;
    if (!xr_ok(xrWaitSwapchainImage((XrSwapchain)c->handle, &wi), "xrWaitSwapchainImage (extra)")) {   /* an image taken and not waited for: given back, or no later one can be taken */
        XrSwapchainImageReleaseInfo rl = { XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO };
        xrReleaseSwapchainImage((XrSwapchain)c->handle, &rl);
        return 0;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, c->fbo[idx < (uint32_t)c->n ? idx : 0]);
    glViewport(0, 0, c->w, c->h);
    if (X.srgb_write_off) glDisable(GL_FRAMEBUFFER_SRGB_EXT_);
    return 1;
}

void vr_chain_release(VrChain *c)
{
    if (!c || !c->handle) return;
    glFlush();
    XrSwapchainImageReleaseInfo ri = { XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO };
    if (xr_ok(xrReleaseSwapchainImage((XrSwapchain)c->handle, &ri), "xrReleaseSwapchainImage (extra)")) c->ever = 1;
}

/* The surface the film is decoded into. Its swapchain has no images the app touches: MediaCodec
 * fills it and the compositor reads it. The player gets the Surface object (CuelightPlayer.vrSurface). */
static void make_video_surface(JNIEnv *env)
{
    xrGetInstanceProcAddr(X.inst, "xrCreateSwapchainAndroidSurfaceKHR", (PFN_xrVoidFunction *)&V.create);
    if (!V.create) { LOGW("vr: no video surface function: films will not show in the headset"); return; }
    XrSwapchainCreateInfo ci = { XR_TYPE_SWAPCHAIN_CREATE_INFO };
    ci.sampleCount = 1; ci.width = VIDEO_PX_W; ci.height = VIDEO_PX_H; ci.faceCount = 1; ci.arraySize = 1; ci.mipCount = 1;
    jobject surf = NULL;
    XrResult r = V.create(X.session, &ci, &V.chain, &surf);   /* the fields a surface swapchain ignores are left empty */
    if (XR_FAILED(r)) {   /* a runtime that wants the colour usage named anyway */
        ci.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
        V.chain = XR_NULL_HANDLE; surf = NULL;
        r = V.create(X.session, &ci, &V.chain, &surf);
    }
    if (XR_FAILED(r)) {   /* and one with the fields a surface swapchain ignores left at zero */
        ci.usageFlags = 0; ci.sampleCount = 0; ci.faceCount = 0; ci.arraySize = 0; ci.mipCount = 0;
        V.chain = XR_NULL_HANDLE; surf = NULL;
        r = V.create(X.session, &ci, &V.chain, &surf);
    }
    if (!xr_ok(r, "xrCreateSwapchainAndroidSurfaceKHR") || !surf) { V.chain = XR_NULL_HANDLE; return; }
    V.surface = (*env)->NewGlobalRef(env, surf);
    (*env)->DeleteLocalRef(env, surf);
    V.ok = V.surface != NULL;
    LOGI("vr: the film's surface is ready (%dx%d)", VIDEO_PX_W, VIDEO_PX_H);
}

void *vr_video_surface(void) { return V.ok ? (void *)V.surface : NULL; }
void vr_video_box(int x, int y, int w, int h) { V.bx = x; V.by = y; V.bw = w; V.bh = h; }
int vr_video_mode(void) { return V.mode; }

const char *vr_video_cycle(void)
{
    vr_box_set_tab(vr_box_tab());   /* what the box says of the view changes: it is drawn again */
    for (int i = 1; i <= N_MODES; i++) {
        int m = (V.mode + i) % N_MODES;
        if (MODES[m].proj && !X.has_equirect) continue;   /* this runtime has no 180 or 360 */
        V.mode = m;
        break;
    }
    return MODES[V.mode].name;
}

static int has_word(const char *low, const char *w)
{
    size_t n = strlen(w);
    for (const char *p = low; (p = strstr(p, w)); p++) {
        int before = p == low || !isalnum((unsigned char)p[-1]);
        int after = !isalnum((unsigned char)p[n]);
        if (before && after) return 1;
    }
    return 0;
}

/* What a release is called says what it is: "Film.2019.1080p.HSBS", "Trip_VR180_LR", "Dive 360 TB". */
const char *vr_video_hint(const char *text)
{
    char low[1100]; size_t n = 0;
    for (const char *c = text ? text : ""; *c && n < sizeof low - 1; c++) low[n++] = (char)tolower((unsigned char)*c);
    low[n] = 0;
    for (size_t i = 0; i < n; i++) if (low[i] == '.' || low[i] == '_' || low[i] == '-' || low[i] == '[' || low[i] == ']' || low[i] == '(' || low[i] == ')') low[i] = ' ';
    int p180 = has_word(low, "180") || has_word(low, "vr180") || has_word(low, "180x180");
    int p360 = has_word(low, "360") || has_word(low, "vr360") || has_word(low, "equirectangular");
    int vrish = p180 || p360 || has_word(low, "3d") || has_word(low, "vr");   /* "lr", "ou" and "tb" are also ordinary words and initials */
    int sbs = has_word(low, "sbs") || has_word(low, "hsbs") || has_word(low, "3dh") || (vrish && has_word(low, "lr"));
    int ou = has_word(low, "hou") || has_word(low, "htb") || has_word(low, "3dv") || (has_word(low, "over") && has_word(low, "under")) || (has_word(low, "top") && has_word(low, "bottom")) || (vrish && (has_word(low, "ou") || has_word(low, "tb")));
    if (!X.has_equirect) p180 = p360 = 0;
    int m = 0;   /* Flat */
    if (p360) m = ou ? 7 : sbs ? 8 : 6;
    else if (p180) m = ou ? 4 : sbs ? 3 : 5;
    else if (sbs) m = 1;
    else if (ou) m = 2;
    V.mode = m;
    return MODES[m].name;
}

static int vr_init_inner(void)
{
    JNIEnv *env = (JNIEnv *)SDL_AndroidGetJNIEnv();
    jobject local_activity = (jobject)SDL_AndroidGetActivity();
    JavaVM *vm = NULL;
    if (!env || !local_activity || (*env)->GetJavaVM(env, &vm) != JNI_OK) return -1;
    /* the runtime keeps this past this call (and on another thread): a global reference, not SDL's local one */
    static jobject activity;
    if (!activity) activity = (*env)->NewGlobalRef(env, local_activity);
    (*env)->DeleteLocalRef(env, local_activity);
    if (!activity) return -1;

    PFN_xrInitializeLoaderKHR init_loader = NULL;
    xrGetInstanceProcAddr(XR_NULL_HANDLE, "xrInitializeLoaderKHR", (PFN_xrVoidFunction *)&init_loader);
    if (init_loader) {
        XrLoaderInitInfoAndroidKHR li = { XR_TYPE_LOADER_INIT_INFO_ANDROID_KHR };
        li.applicationVM = vm; li.applicationContext = activity;
        if (!xr_ok(init_loader((const XrLoaderInitInfoBaseHeaderKHR *)&li), "xrInitializeLoaderKHR")) return -1;
    }

    uint32_t ne = 0;
    XrResult r = xrEnumerateInstanceExtensionProperties(NULL, 0, &ne, NULL);
    if (XR_FAILED(r)) { LOGI("vr: no OpenXR runtime here (%d): the app runs flat", (int)r); return -1; }
    XrExtensionProperties *ep = (XrExtensionProperties *)calloc(ne ? ne : 1, sizeof *ep);   /* by the count the runtime gives, never a fixed list */
    if (!ep) return -1;
    for (uint32_t i = 0; i < ne; i++) { ep[i].type = XR_TYPE_EXTENSION_PROPERTIES; ep[i].next = NULL; }
    xrEnumerateInstanceExtensionProperties(NULL, ne, &ne, ep);
    int has_gles = 0, has_android = 0, has_surface = 0, has_ht = 0, has_hi = 0;
    for (uint32_t i = 0; i < ne; i++) {
        if (!strcmp(ep[i].extensionName, XR_KHR_OPENGL_ES_ENABLE_EXTENSION_NAME)) has_gles = 1;
        if (!strcmp(ep[i].extensionName, XR_KHR_ANDROID_CREATE_INSTANCE_EXTENSION_NAME)) has_android = 1;
        if (!strcmp(ep[i].extensionName, XR_KHR_ANDROID_SURFACE_SWAPCHAIN_EXTENSION_NAME)) has_surface = 1;
        if (!strcmp(ep[i].extensionName, XR_KHR_COMPOSITION_LAYER_EQUIRECT2_EXTENSION_NAME)) X.has_equirect = 1;
        if (!strcmp(ep[i].extensionName, XR_KHR_COMPOSITION_LAYER_CYLINDER_EXTENSION_NAME)) X.has_cylinder = 1;
        if (!strcmp(ep[i].extensionName, XR_FB_PASSTHROUGH_EXTENSION_NAME)) X.has_pass = 1;
        if (!strcmp(ep[i].extensionName, XR_EXT_HAND_TRACKING_EXTENSION_NAME)) has_ht = 1;
        if (!strcmp(ep[i].extensionName, XR_EXT_HAND_INTERACTION_EXTENSION_NAME)) has_hi = 1;
    }
    free(ep);
    LOGI("vr: the runtime offers %u extensions (OpenGL ES %s, Android instance %s, video surface %s, 180/360 %s, curved %s, passthrough %s, hands %s)", ne, has_gles ? "yes" : "NO", has_android ? "yes" : "NO", has_surface ? "yes" : "NO", X.has_equirect ? "yes" : "no", X.has_cylinder ? "yes" : "no", X.has_pass ? "yes" : "no", has_ht && has_hi ? "yes" : "no");
    if (!has_gles || !has_android) return -1;

    const char *want[10] = { XR_KHR_OPENGL_ES_ENABLE_EXTENSION_NAME, XR_KHR_ANDROID_CREATE_INSTANCE_EXTENSION_NAME };
    uint32_t n_want = 2;
    if (has_surface) want[n_want++] = XR_KHR_ANDROID_SURFACE_SWAPCHAIN_EXTENSION_NAME;
    if (X.has_equirect) want[n_want++] = XR_KHR_COMPOSITION_LAYER_EQUIRECT2_EXTENSION_NAME;
    if (X.has_cylinder) want[n_want++] = XR_KHR_COMPOSITION_LAYER_CYLINDER_EXTENSION_NAME;
    if (X.has_pass) want[n_want++] = XR_FB_PASSTHROUGH_EXTENSION_NAME;
    if (has_ht && has_hi) { want[n_want++] = XR_EXT_HAND_TRACKING_EXTENSION_NAME; want[n_want++] = XR_EXT_HAND_INTERACTION_EXTENSION_NAME; X.has_hands = 1; }
    XrInstanceCreateInfoAndroidKHR ai = { XR_TYPE_INSTANCE_CREATE_INFO_ANDROID_KHR };
    ai.applicationVM = vm; ai.applicationActivity = activity;
    XrInstanceCreateInfo ci = { XR_TYPE_INSTANCE_CREATE_INFO };
    ci.next = &ai;
    snprintf(ci.applicationInfo.applicationName, sizeof ci.applicationInfo.applicationName, "Cuelight");
    snprintf(ci.applicationInfo.engineName, sizeof ci.applicationInfo.engineName, "Cuelight");
    ci.applicationInfo.apiVersion = XR_MAKE_VERSION(1, 0, 0);
    ci.enabledExtensionCount = n_want; ci.enabledExtensionNames = want;
    if (!xr_ok(xrCreateInstance(&ci, &X.inst), "xrCreateInstance")) return -1;

    XrInstanceProperties ip = { XR_TYPE_INSTANCE_PROPERTIES };
    if (XR_SUCCEEDED(xrGetInstanceProperties(X.inst, &ip))) LOGI("vr: runtime %s %u.%u.%u", ip.runtimeName, XR_VERSION_MAJOR(ip.runtimeVersion), XR_VERSION_MINOR(ip.runtimeVersion), XR_VERSION_PATCH(ip.runtimeVersion));

    XrSystemGetInfo gi = { XR_TYPE_SYSTEM_GET_INFO };
    gi.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    if (!xr_ok(xrGetSystem(X.inst, &gi, &X.system), "xrGetSystem")) return -1;
    XrSystemProperties sp = { XR_TYPE_SYSTEM_PROPERTIES };
    if (XR_SUCCEEDED(xrGetSystemProperties(X.inst, X.system, &sp))) LOGI("vr: system %s", sp.systemName);

    xrGetInstanceProcAddr(X.inst, "xrGetOpenGLESGraphicsRequirementsKHR", (PFN_xrVoidFunction *)&X.get_gles_req);
    if (!X.get_gles_req) return -1;
    XrGraphicsRequirementsOpenGLESKHR rq = { XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_ES_KHR };
    if (!xr_ok(X.get_gles_req(X.inst, X.system, &rq), "xrGetOpenGLESGraphicsRequirementsKHR")) return -1;
    GLint maj = 0, min = 0;
    glGetIntegerv(GL_MAJOR_VERSION, &maj); glGetIntegerv(GL_MINOR_VERSION, &min);
    LOGI("vr: OpenGL ES %d.%d here, the runtime wants %u.%u to %u.%u", maj, min,
         XR_VERSION_MAJOR(rq.minApiVersionSupported), XR_VERSION_MINOR(rq.minApiVersionSupported),
         XR_VERSION_MAJOR(rq.maxApiVersionSupported), XR_VERSION_MINOR(rq.maxApiVersionSupported));

    EGLDisplay dpy = eglGetCurrentDisplay();
    EGLContext ctx = eglGetCurrentContext();
    EGLint cid = 0, nc = 0;
    EGLConfig cfg = NULL;
    eglQueryContext(dpy, ctx, EGL_CONFIG_ID, &cid);
    EGLint attr[] = { EGL_CONFIG_ID, cid, EGL_NONE };
    eglChooseConfig(dpy, attr, &cfg, 1, &nc);
    XrGraphicsBindingOpenGLESAndroidKHR gb = { XR_TYPE_GRAPHICS_BINDING_OPENGL_ES_ANDROID_KHR };
    gb.display = dpy; gb.config = cfg; gb.context = ctx;
    XrSessionCreateInfo si = { XR_TYPE_SESSION_CREATE_INFO };
    si.next = &gb; si.systemId = X.system;
    if (!xr_ok(xrCreateSession(X.inst, &si, &X.session), "xrCreateSession")) return -1;

    XrReferenceSpaceCreateInfo rs = { XR_TYPE_REFERENCE_SPACE_CREATE_INFO };
    rs.poseInReferenceSpace.orientation.w = 1;
    rs.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
    if (!xr_ok(xrCreateReferenceSpace(X.session, &rs, &X.local), "local space")) return -1;
    rs.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
    if (!xr_ok(xrCreateReferenceSpace(X.session, &rs, &X.view), "view space")) return -1;

    if (setup_input() != 0 || attach_input() != 0) return -1;
    if (make_swapchain() != 0) return -1;
    if (has_surface) make_video_surface(env);
    X.dist = PANEL_DIST; X.scale = 1.0f;
    X.size_applied = -2; X.dist_applied = -2; X.bed_applied = -2;   /* nothing applied yet: the saved choices are, on the first frame */
    X.dock_hover = -1; X.dock_pressed = -1; X.dock_hold = -1; X.dock_dh = -2; X.dock_dp = -2; X.dock_dc = -1;

    /* the panel's first place, before the head has been located: straight ahead */
    const float p0[3] = { 0, -PANEL_LOWER, -PANEL_DIST }; memcpy(X.panel_pos, p0, sizeof p0);
    X.panel_rot[0] = X.panel_rot[1] = X.panel_rot[2] = 0; X.panel_rot[3] = 1;
    LOGI("vr: session created, waiting for the headset to be ready");
    return 0;
}

static const float SIZES[4] = { 0.7f, 1.0f, 1.45f, 2.1f };   /* small, cinema, big, huge: a multiple of the first screen's width */

static int locate_head(XrTime t, float pos[3], float dir[3])
{
    XrSpaceLocation loc = { XR_TYPE_SPACE_LOCATION };
    if (!XR_SUCCEEDED(xrLocateSpace(X.view, X.local, t, &loc))) return 0;
    if (!(loc.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) || !(loc.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT)) return 0;
    pos[0] = loc.pose.position.x; pos[1] = loc.pose.position.y; pos[2] = loc.pose.position.z;
    float q[4] = { loc.pose.orientation.x, loc.pose.orientation.y, loc.pose.orientation.z, loc.pose.orientation.w };
    const float fwd[3] = { 0, 0, -1 };
    vr_rotate(q, fwd, dir);
    return 1;
}

/* the panel from the head position, the angles and the distance; lying down it is not lowered */
static void put_panel(void)
{
    vr_panel_from_angles(X.head_pos, X.yaw, X.pitch, X.dist, vr_pref(VR_PREF_BED) ? 0 : PANEL_LOWER, X.panel_pos, X.panel_rot);
}

/* in front of you, wherever you look now (up too, lying down) */
static void place_panel_in_front(XrTime t)
{
    float hp[3], f[3], yaw, pitch;
    if (!locate_head(t, hp, f)) return;
    vr_dir_angles(f, &yaw, &pitch);
    memcpy(X.head_pos, hp, sizeof hp);
    X.yaw = yaw; X.pitch = vr_pref(VR_PREF_BED) ? pitch : 0;
    put_panel();
    X.panel_placed = 1; X.recenter = 0;
    LOGI("vr: the panel is %.1f m ahead of you", X.dist);
}

void vr_recenter(void) { X.recenter = 1; }
void vr_apply_prefs_again(void) { X.size_applied = -2; X.dist_applied = -2; }
int vr_passthrough_available(void) { return X.has_pass; }
const char *vr_video_view_name(void) { return MODES[V.mode].name; }

static float wrap_pi(float a) { while (a > 3.14159265f) a -= 6.2831853f; while (a < -3.14159265f) a += 6.2831853f; return a; }

/* what is chosen in the Cinema sheet, taken up as it changes; and the screen following the head */
static void apply_prefs_and_follow(XrTime t, float dt)
{
    int sz = vr_pref(VR_PREF_SIZE);
    if (sz != X.size_applied) { X.size_applied = sz; if (sz >= 0 && sz < 4) X.scale = SIZES[sz]; else X.auto_key = 0; }
    int dd = vr_pref(VR_PREF_DIST);
    if (dd != X.dist_applied) {
        X.dist_applied = dd; X.dist = dd / 10.0f;
        if (X.dist < 0.8f) X.dist = 0.8f;
        if (X.panel_placed) put_panel();
    }
    int bed = vr_pref(VR_PREF_BED);
    if (bed != X.bed_applied) { X.bed_applied = bed; X.recenter = 1; }
    if (X.recenter) { place_panel_in_front(t); return; }
    int follow = vr_pref(VR_PREF_FOLLOW);
    if (!follow || X.grab || !X.panel_placed) { X.follow_on = 0; return; }
    float hp[3], f[3], hyaw, hpitch;
    if (!locate_head(t, hp, f)) return;
    vr_dir_angles(f, &hyaw, &hpitch);
    float tp = bed ? hpitch : 0;
    float dy = wrap_pi(hyaw - X.yaw), dp = tp - X.pitch;
    int far = fabsf(dy) > 0.6f || fabsf(dp) > 0.5f, near = fabsf(dy) < 0.05f && fabsf(dp) < 0.05f;
    if (follow == 2) X.follow_on = !near;                         /* always: ease after the head */
    else if (!X.follow_on && far) X.follow_on = 1;                /* lazy: only once you have turned well away */
    else if (X.follow_on && near) X.follow_on = 0;
    if (!X.follow_on) return;
    float k = 1.0f - expf(-(follow == 2 ? 5.0f : 2.2f) * dt);
    X.yaw = wrap_pi(X.yaw + dy * k); X.pitch += dp * k;
    memcpy(X.head_pos, hp, sizeof hp);   /* it keeps its distance from you as you move */
    put_panel();
}

static void poll_events(void)
{
    for (;;) {
        XrEventDataBuffer ev = { XR_TYPE_EVENT_DATA_BUFFER };
        XrResult r = xrPollEvent(X.inst, &ev);
        if (r != XR_SUCCESS) return;
        if (ev.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) {
            const XrEventDataSessionStateChanged *e = (const XrEventDataSessionStateChanged *)&ev;
            LOGI("vr: session state %d", (int)e->state);
            switch (e->state) {
            case XR_SESSION_STATE_READY: {
                XrSessionBeginInfo bi = { XR_TYPE_SESSION_BEGIN_INFO };
                bi.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
                if (xr_ok(xrBeginSession(X.session, &bi), "xrBeginSession")) { X.running = 1; X.panel_placed = 0; LOGI("vr: the session is running"); }
                break; }
            case XR_SESSION_STATE_FOCUSED: X.focused = 1; break;
            case XR_SESSION_STATE_VISIBLE: X.focused = 0; break;
            case XR_SESSION_STATE_STOPPING: xrEndSession(X.session); X.running = 0; X.focused = 0; break;
            case XR_SESSION_STATE_EXITING: case XR_SESSION_STATE_LOSS_PENDING: X.quit = 1; X.running = 0; break;
            default: break;
            }
        } else if (ev.type == XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING) X.quit = 1;
        else if (ev.type == XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING) { X.panel_placed = 0; LOGI("vr: recentred, the panel comes back in front of you"); }
    }
}

/* ---- the control box --------------------------------------------------------------------- */

/* how far round the bend of a curved screen reaches: its width over its radius, kept inside what a layer may be (under a full turn) */
static float cyl_angle(void) { float a = PANEL_M_W * X.scale / X.dist; return a > 5.8f ? 5.8f : a; }

static float box_w(void) { return 1.5f; }
static float box_h(void) { return box_w() * (float)VR_BOX_H / (float)VR_BOX_W; }

/* in front of you, a little below your eyes, where it was when it was opened */
static int place_box(XrTime t)
{
    float hp[3], f[3], yaw, pitch;
    if (!locate_head(t, hp, f)) return 0;
    vr_dir_angles(f, &yaw, &pitch);
    vr_panel_from_angles(hp, yaw, 0, 1.35f, 0.18f, X.box_pos, X.box_rot);
    return 1;
}

/* ---- the dock ------------------------------------------------------------------------------ */

static float dock_w(void) { return 0.9f * X.dist; }                        /* the same size to the eye at any distance */
static float dock_h(void) { return dock_w() * (float)DOCK_H / (float)DOCK_W; }
static void dock_pos(float out[3])
{
    static const float AY[3] = { 0, 1, 0 };
    float ay[3]; vr_rotate(X.panel_rot, AY, ay);
    float off = PANEL_M_H * X.scale * 0.5f + 0.10f + dock_h() * 0.5f;     /* just under the screen */
    for (int k = 0; k < 3; k++) out[k] = X.panel_pos[k] - ay[k] * off;
}
static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }

static void set_dist(float d)
{
    X.dist = clampf(d, 0.8f, 8.0f);
    X.stick_moved = 1;   /* the file is written when the steps stop (as with the stick), not at every one */
    if (X.panel_placed) put_panel();
}

static void dock_step(int id)   /* the buttons that step a size or a distance: also what holding them repeats */
{
    switch (id) {
    case DOCK_SMALLER: X.scale = clampf(X.scale * 0.9f, 0.4f, 3.5f); break;
    case DOCK_BIGGER: X.scale = clampf(X.scale * 1.1f, 0.4f, 3.5f); break;
    case DOCK_NEARER: set_dist(X.dist - 0.2f); break;
    case DOCK_FARTHER: set_dist(X.dist + 0.2f); break;
    default: break;
    }
}

static void dock_press(int id, double now)
{
    switch (id) {
    case DOCK_MOVE: X.dock_drag = 1; break;                                      /* carried like a grip does */
    case DOCK_RESIZE: X.dock_drag = 2; X.rs_x0 = X.rs_x; X.rs_scale0 = X.scale; break;
    case DOCK_CURVE: if (X.has_cylinder) vr_pref_set(VR_PREF_CURVED, !vr_pref(VR_PREF_CURVED)); break;
    case DOCK_VIEW: X.input.view = 1; break;                                     /* main cycles the view and says which */
    case DOCK_CINEMA: X.input.menu = 1; break;                                   /* and opens the sheet */
    case DOCK_CENTER: X.recenter = 1; break;
    default: dock_step(id); X.dock_hold = id; X.dock_repeat_at = now + 0.4; break;
    }
}

static int edge(int now, int *was) { int e = now && !*was; *was = now; return e; }

static void sync_input(XrTime t)
{
    memset(&X.input, 0, sizeof X.input);
    if (!X.focused) return;
    XrActiveActionSet as = { X.set, XR_NULL_PATH };
    XrActionsSyncInfo sy = { XR_TYPE_ACTIONS_SYNC_INFO };
    sy.countActiveActionSets = 1; sy.activeActionSets = &as;
    if (!XR_SUCCEEDED(xrSyncActions(X.session, &sy))) return;

    float best_trig = 0; int hand = X.active_hand;
    float trig[2] = { 0, 0 }, sx = 0, sy2 = 0, stk[2][2] = { { 0, 0 }, { 0, 0 } };
    for (int h = 0; h < 2; h++) {
        XrActionStateGetInfo gi = { XR_TYPE_ACTION_STATE_GET_INFO };
        gi.subactionPath = X.hand[h];
        XrActionStateFloat tf = { XR_TYPE_ACTION_STATE_FLOAT };
        gi.action = X.trigger;
        if (XR_SUCCEEDED(xrGetActionStateFloat(X.session, &gi, &tf)) && tf.isActive) trig[h] = tf.currentState;
        if (trig[h] > best_trig + 0.05f) { best_trig = trig[h]; hand = h; }
        XrActionStateVector2f sv = { XR_TYPE_ACTION_STATE_VECTOR2F };
        gi.action = X.stick;
        if (XR_SUCCEEDED(xrGetActionStateVector2f(X.session, &gi, &sv)) && sv.isActive) {
            stk[h][0] = sv.currentState.x; stk[h][1] = sv.currentState.y;
            if (sv.currentState.x * sv.currentState.x + sv.currentState.y * sv.currentState.y > sx * sx + sy2 * sy2) { sx = sv.currentState.x; sy2 = sv.currentState.y; }
        }
    }
    X.active_hand = hand;

    /* the pointer: the active hand's aim ray against the panel */
    X.box_on = 0;
    XrSpaceLocation loc = { XR_TYPE_SPACE_LOCATION };
    if (XR_SUCCEEDED(xrLocateSpace(X.aim_space[hand], X.local, t, &loc)) && (loc.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) && (loc.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT)) {
        float o[3] = { loc.pose.position.x, loc.pose.position.y, loc.pose.position.z };
        float q[4] = { loc.pose.orientation.x, loc.pose.orientation.y, loc.pose.orientation.z, loc.pose.orientation.w };
        const float fwd[3] = { 0, 0, -1 };
        float d[3];
        vr_rotate(q, fwd, d);
        float u, v, dist;
        int hit = (vr_pref(VR_PREF_CURVED) && X.has_cylinder)
            ? vr_ray_cylinder(o, d, X.panel_pos, X.panel_rot, X.dist, cyl_angle(), PANEL_M_H * X.scale, &u, &v, &dist)
            : vr_ray_panel(o, d, X.panel_pos, X.panel_rot, PANEL_M_W * X.scale, PANEL_M_H * X.scale, &u, &v, &dist);
        /* the control box, in front of everything while it is open */
        float bu = 0, bv = 0, bd = 0;
        int on_box = vr_cinema_is_open() && X.box_placed && vr_ray_panel(o, d, X.box_pos, X.box_rot, box_w(), box_h(), &bu, &bv, &bd);
        X.box_on = on_box;
        if (on_box) { X.box_px = ((float)PANEL_PX_W - VR_BOX_W) / 2 + bu * VR_BOX_W; X.box_py = ((float)PANEL_PX_H - VR_BOX_H) / 2 + bv * VR_BOX_H; }
        /* the dock: it shows when the ray is near the bottom of the screen or on it */
        Uint32 nowms = SDL_GetTicks();
        float dp[3], du = 0, dv = 0, dd = 0;
        dock_pos(dp);
        int on_dock = !on_box && (nowms < X.dock_until || X.dock_drag) && vr_ray_panel(o, d, dp, X.panel_rot, dock_w(), dock_h(), &du, &dv, &dd);
        if ((!on_box && hit && v > 0.72f) || on_dock || X.dock_drag) X.dock_until = nowms + 2500;
        X.dock_hover = on_dock ? vr_dock_at(du, dv) : -1;
        if (X.dock_drag == 2) { float ru, rv, rd; if (vr_ray_plane_uv(o, d, dp, X.panel_rot, dock_w(), dock_h(), &ru, &rv, &rd)) X.rs_x = (ru - 0.5f) * dock_w(); }
        else { float ru, rv, rd; if (on_dock && vr_ray_plane_uv(o, d, dp, X.panel_rot, dock_w(), dock_h(), &ru, &rv, &rd)) X.rs_x = (ru - 0.5f) * dock_w(); }
        if (on_box || on_dock || X.dock_drag) hit = 0;   /* the screens do not see a ray that is on the box or the dock */
        if (hit) { X.input.have_pointer = 1; X.input.x = u; X.input.y = v; }
    }
    {   /* the dock's buttons and handles, by the trigger */
        int raw = trig[hand] > (X.dock_trig_was ? 0.35f : 0.65f);
        int pressed_now = raw && !X.dock_trig_was;
        double nowt = SDL_GetTicks() / 1000.0;
        X.dock_trig_was = raw;
        if (pressed_now && X.box_on) { vr_box_tap(X.box_px, X.box_py); int k = vr_cinema_take_key(); if (k) X.input.key = k; }
        else if (pressed_now && X.dock_hover >= 0) dock_press(X.dock_hover, nowt);
        else if (raw && X.dock_hold >= 0 && X.dock_hover == X.dock_hold && nowt >= X.dock_repeat_at) { dock_step(X.dock_hold); X.dock_repeat_at = nowt + 0.09; }
        if (!raw) { X.dock_drag = 0; X.dock_hold = -1; }
        if (X.dock_drag == 2) X.scale = clampf(X.rs_scale0 * expf((X.rs_x - X.rs_x0) * 1.1f), 0.4f, 3.5f);
        X.dock_pressed = X.dock_drag == 1 ? DOCK_MOVE : X.dock_drag == 2 ? DOCK_RESIZE : (raw && X.dock_hover >= 0 ? X.dock_hover : -1);
    }
    /* the grip: take hold of the screen and carry it (the thumbstick then pushes and pulls it, and makes it bigger or smaller) */
    float grip[2] = { 0, 0 };
    for (int h = 0; h < 2; h++) {
        XrActionStateGetInfo gi = { XR_TYPE_ACTION_STATE_GET_INFO };
        gi.action = X.squeeze; gi.subactionPath = X.hand[h];
        XrActionStateFloat gf = { XR_TYPE_ACTION_STATE_FLOAT };
        if (XR_SUCCEEDED(xrGetActionStateFloat(X.session, &gi, &gf)) && gf.isActive) grip[h] = gf.currentState;
    }
    int gh = grip[1] > grip[0] ? 1 : 0;   /* the hand that squeezes harder */
    int want_grab = grip[gh] > (X.grab ? 0.4f : 0.7f);
    if (X.dock_drag == 1) { gh = hand; want_grab = 1; }   /* the dock's Move handle: carried by the hand that holds the trigger */
    /* both grips: pull the hands apart or together to make the screen bigger or smaller */
    if (grip[0] > 0.7f && grip[1] > 0.7f && X.dock_drag != 1) {
        XrSpaceLocation l0 = { XR_TYPE_SPACE_LOCATION }, l1 = { XR_TYPE_SPACE_LOCATION };
        if (XR_SUCCEEDED(xrLocateSpace(X.aim_space[0], X.local, t, &l0)) && XR_SUCCEEDED(xrLocateSpace(X.aim_space[1], X.local, t, &l1))
            && (l0.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) && (l1.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT)) {
            float dx = l0.pose.position.x - l1.pose.position.x, dy = l0.pose.position.y - l1.pose.position.y, dz = l0.pose.position.z - l1.pose.position.z;
            float sep = sqrtf(dx * dx + dy * dy + dz * dz);
            if (!X.two) { X.two = 1; X.two_sep0 = sep > 0.05f ? sep : 0.05f; X.two_scale0 = X.scale; }
            X.scale = clampf(X.two_scale0 * sep / X.two_sep0, 0.4f, 3.5f);
        }
        want_grab = 0;
    } else X.two = 0;
    static Uint32 last_ms;
    Uint32 ms = SDL_GetTicks();
    float dt = last_ms ? (ms - last_ms) / 1000.0f : 0.016f;
    if (dt > 0.1f) dt = 0.1f;
    last_ms = ms;
    if (want_grab) {
        XrSpaceLocation gl = { XR_TYPE_SPACE_LOCATION };
        if (XR_SUCCEEDED(xrLocateSpace(X.aim_space[gh], X.local, t, &gl)) && (gl.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) && (gl.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT)) {
            float o[3] = { gl.pose.position.x, gl.pose.position.y, gl.pose.position.z };
            float q[4] = { gl.pose.orientation.x, gl.pose.orientation.y, gl.pose.orientation.z, gl.pose.orientation.w };
            const float fwd[3] = { 0, 0, -1 };
            float d[3];
            vr_rotate(q, fwd, d);
            if (!X.grab) {   /* the screen stays as far from the hand as it is */
                float dx = X.panel_pos[0] - o[0], dy = X.panel_pos[1] - o[1], dz = X.panel_pos[2] - o[2];
                X.grab_dist = sqrtf(dx * dx + dy * dy + dz * dz);
                for (int k = 0; k < 3; k++) X.grab_off[k] = X.panel_pos[k] - (o[k] + d[k] * X.grab_dist);
                X.grab = 1;
            }
            X.grab_dist += sy2 * 1.6f * dt;   /* the stick up pushes it away, down pulls it in */
            if (X.grab_dist < 0.8f) X.grab_dist = 0.8f;
            if (X.grab_dist > 8.0f) X.grab_dist = 8.0f;
            X.scale *= 1.0f + sx * 0.9f * dt;   /* right bigger, left smaller */
            if (X.scale < 0.4f) X.scale = 0.4f;
            if (X.scale > 3.0f) X.scale = 3.0f;
            float hp[3], hf[3];
            if (locate_head(t, hp, hf)) {   /* the angles and distance from your head, so the screen can keep them when it follows */
                float lower = vr_pref(VR_PREF_BED) ? 0 : PANEL_LOWER;
                float v3[3] = { o[0] + d[0] * X.grab_dist + X.grab_off[0] - hp[0], o[1] + d[1] * X.grab_dist + X.grab_off[1] - hp[1] + lower, o[2] + d[2] * X.grab_dist + X.grab_off[2] - hp[2] };
                float vl = sqrtf(v3[0] * v3[0] + v3[1] * v3[1] + v3[2] * v3[2]);
                if (vl > 0.3f) {
                    for (int k = 0; k < 3; k++) v3[k] /= vl;
                    vr_dir_angles(v3, &X.yaw, &X.pitch);
                    X.dist = vl;
                    memcpy(X.head_pos, hp, sizeof hp);
                    put_panel();
                }
            }
        }
    } else {
        if (X.grab) {   /* let go: the distance it was left at is the one the Cinema sheet shows and the menu button returns to */
            int dd = (int)(X.dist * 10 + 0.5f);
            vr_pref_set(VR_PREF_DIST, dd);
            X.dist_applied = vr_pref(VR_PREF_DIST);
        }
        X.grab = 0;
    }
    X.input.grabbing = X.grab || X.two;
    if (X.grab) { sx = sy2 = 0; }   /* the stick was moving the screen, not the menus */

    /* a trigger pressed hard is down, let go well below it: no flutter at the edge */
    int was = X.trig_was[0];
    X.input.down = X.input.have_pointer && !X.grab && (trig[hand] > (was ? 0.35f : 0.65f));
    X.trig_was[0] = X.input.down;

    /* buttons */
    int pressed[6] = { 0, 0, 0, 0, 0, 0 };   /* ok, back, menu, stick click, play or pause, view */
    XrAction acts[6] = { X.a_click, X.b_click, X.menu_click, X.stick_click, X.x_click, X.y_click };
    for (int k = 0; k < 6; k++) {
        for (int h = 0; h < 2; h++) {
            XrActionStateGetInfo gi = { XR_TYPE_ACTION_STATE_GET_INFO };
            gi.action = acts[k]; gi.subactionPath = k == 3 ? X.hand[h] : XR_NULL_PATH;   /* only the stick press is per hand */
            XrActionStateBoolean bs = { XR_TYPE_ACTION_STATE_BOOLEAN };
            if (XR_SUCCEEDED(xrGetActionStateBoolean(X.session, &gi, &bs)) && bs.isActive && bs.currentState) pressed[k] = 1;
            if (k != 3) break;
        }
    }
    X.input.ok = edge(pressed[0] || pressed[3], &X.pressed_was[0]);
    X.input.back = edge(pressed[1], &X.pressed_was[1]);
    X.input.menu |= edge(pressed[2], &X.pressed_was[2]);   /* (the dock can have set it) */
    X.input.play = edge(pressed[4], &X.pressed_was[3]);
    X.input.view |= edge(pressed[5], &X.pressed_was[4]);

    /* the right thumbstick moves the screen as in other headset apps, with nothing held: up and down push it away and
     * pull it near, right and left make it bigger and smaller (the distance it is left at is kept when the stick rests) */
    {
        float rx = stk[1][0], ry = stk[1][1];
        float dzx = fabsf(rx) > 0.25f ? (fabsf(rx) - 0.25f) / 0.75f * (rx < 0 ? -1.0f : 1.0f) : 0;
        float dzy = fabsf(ry) > 0.25f ? (fabsf(ry) - 0.25f) / 0.75f * (ry < 0 ? -1.0f : 1.0f) : 0;
        if (!X.grab && !X.two && X.dock_drag == 0 && (dzx != 0 || dzy != 0)) {
            if (dzy != 0) { X.dist = clampf(X.dist + dzy * 1.6f * dt, 0.8f, 8.0f); if (X.panel_placed) put_panel(); }
            if (dzx != 0) X.scale = clampf(X.scale * (1.0f + dzx * 0.9f * dt), 0.4f, 3.5f);
            X.stick_moved = 1;
        } else if (X.stick_moved) {
            X.stick_moved = 0;
            vr_pref_set(VR_PREF_DIST, (int)(X.dist * 10 + 0.5f));
            X.dist_applied = vr_pref(VR_PREF_DIST);
        }
    }

    /* the left thumbstick as arrow keys: a push is one press, held it repeats after a pause (and while the screen
     * is being carried the sticks are for it) */
    float lx = X.grab ? 0 : stk[0][0], ly = X.grab ? 0 : stk[0][1];
    int dir = 0;   /* 1 up 2 down 3 left 4 right */
    if (lx * lx + ly * ly > 0.5f * 0.5f) dir = (ly > 0 ? ly : -ly) > (lx > 0 ? lx : -lx) ? (ly > 0 ? 1 : 2) : (lx < 0 ? 3 : 4);
    double now = SDL_GetTicks() / 1000.0;
    int fire = 0;
    if (dir != X.stick_dir_was) { X.stick_dir_was = dir; X.stick_repeat_at = now + 0.4; fire = dir != 0; }
    else if (dir && now >= X.stick_repeat_at) { X.stick_repeat_at = now + 0.12; fire = 1; }
    if (fire) { X.input.up_k = dir == 1; X.input.down_k = dir == 2; X.input.left_k = dir == 3; X.input.right_k = dir == 4; }
}

void vr_screen_step(int id) { dock_step(id); }

int vr_init(void)
{
    int r = vr_init_inner();
    if (r != 0) vr_shutdown();   /* a session left open would keep the headset waiting for frames that never come */
    return r;
}

int vr_active(void) { return X.session != XR_NULL_HANDLE; }
int vr_quit_requested(void) { return X.quit; }
void vr_input(VrInput *in) { *in = X.input; }

/* the room: the headset's cameras behind everything, switched on and off from the Cinema sheet */
static void pass_sync(void)
{
    int want = X.has_pass && vr_pref(VR_PREF_PASS);
    if (want && !X.pt) {
        if (!X.pt_create) {
            xrGetInstanceProcAddr(X.inst, "xrCreatePassthroughFB", (PFN_xrVoidFunction *)&X.pt_create);
            xrGetInstanceProcAddr(X.inst, "xrDestroyPassthroughFB", (PFN_xrVoidFunction *)&X.pt_destroy);
            xrGetInstanceProcAddr(X.inst, "xrCreatePassthroughLayerFB", (PFN_xrVoidFunction *)&X.ptl_create);
            xrGetInstanceProcAddr(X.inst, "xrDestroyPassthroughLayerFB", (PFN_xrVoidFunction *)&X.ptl_destroy);
        }
        if (!X.pt_create || !X.pt_destroy || !X.ptl_create || !X.ptl_destroy) { LOGW("vr: passthrough functions are missing"); vr_pref_set(VR_PREF_PASS, 0); return; }
        XrPassthroughCreateInfoFB pi = { XR_TYPE_PASSTHROUGH_CREATE_INFO_FB };
        pi.flags = XR_PASSTHROUGH_IS_RUNNING_AT_CREATION_BIT_FB;
        if (!xr_ok(X.pt_create(X.session, &pi, &X.pt), "xrCreatePassthroughFB")) { X.pt = XR_NULL_HANDLE; vr_pref_set(VR_PREF_PASS, 0); return; }
        XrPassthroughLayerCreateInfoFB li = { XR_TYPE_PASSTHROUGH_LAYER_CREATE_INFO_FB };
        li.passthrough = X.pt; li.flags = XR_PASSTHROUGH_IS_RUNNING_AT_CREATION_BIT_FB; li.purpose = XR_PASSTHROUGH_LAYER_PURPOSE_RECONSTRUCTION_FB;
        if (!xr_ok(X.ptl_create(X.session, &li, &X.ptl), "xrCreatePassthroughLayerFB")) { X.pt_destroy(X.pt); X.pt = XR_NULL_HANDLE; X.ptl = XR_NULL_HANDLE; vr_pref_set(VR_PREF_PASS, 0); return; }
        LOGI("vr: passthrough is on: the room is behind the screen");
    } else if (!want && X.pt) {
        if (X.ptl) X.ptl_destroy(X.ptl);
        X.pt_destroy(X.pt);
        X.pt = XR_NULL_HANDLE; X.ptl = XR_NULL_HANDLE;
        LOGI("vr: passthrough is off");
    }
}

int vr_frame_begin(int dirty, int *w, int *h)
{
    X.in_frame = 0; X.acquired = 0;
    poll_events();
    if (!X.running) return -1;
    X.frame.type = XR_TYPE_FRAME_STATE;
    if (!xr_ok(xrWaitFrame(X.session, NULL, &X.frame), "xrWaitFrame")) return -1;
    if (!xr_ok(xrBeginFrame(X.session, NULL), "xrBeginFrame")) return -1;
    X.in_frame = 1;
    {   static Uint32 prev_ms; Uint32 ms = SDL_GetTicks(); float fdt = prev_ms ? (ms - prev_ms) / 1000.0f : 0.014f; prev_ms = ms; if (fdt > 0.1f) fdt = 0.1f;
        if (!X.panel_placed) place_panel_in_front(X.frame.predictedDisplayTime);
        apply_prefs_and_follow(X.frame.predictedDisplayTime, fdt);
        { int open = vr_cinema_is_open(); if (open && !X.box_was_open) { if (place_box(X.frame.predictedDisplayTime)) X.box_placed = 1; else open = 0; } if (!open) X.box_placed = 0; X.box_was_open = open; }   /* (tracking lost as it opens: tried again next frame, never shown unplaced) */
        pass_sync(); }
    sync_input(X.frame.predictedDisplayTime);
    if (!X.frame.shouldRender || (!dirty && X.ever_released)) return 0;

    uint32_t idx = 0;
    XrSwapchainImageAcquireInfo ai = { XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO };
    if (!xr_ok(xrAcquireSwapchainImage(X.chain, &ai, &idx), "xrAcquireSwapchainImage")) return 0;
    XrSwapchainImageWaitInfo wi = { XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO };
    wi.timeout = XR_INFINITE_DURATION;
    if (!xr_ok(xrWaitSwapchainImage(X.chain, &wi), "xrWaitSwapchainImage")) return 0;
    X.acquired = 1;
    glBindFramebuffer(GL_FRAMEBUFFER, X.fbo[idx < (uint32_t)X.n_images ? idx : 0]);
    if (X.srgb_write_off) glDisable(GL_FRAMEBUFFER_SRGB_EXT_);
    *w = PANEL_PX_W; *h = PANEL_PX_H;
    return 1;
}

void vr_frame_end(void)
{
    if (!X.in_frame) return;
    if (X.acquired) {
        glFlush();
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        XrSwapchainImageReleaseInfo ri = { XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO };
        if (xr_ok(xrReleaseSwapchainImage(X.chain, &ri), "xrReleaseSwapchainImage")) X.ever_released = 1;
        X.acquired = 0;
    }
    const XrCompositionLayerBaseHeader *layers[16];
    XrCompositionLayerQuad roomq[VR_ROOM_N], subq, dockq;
    uint32_t n = 0;
    XrCompositionLayerQuad vq[2], q = { XR_TYPE_COMPOSITION_LAYER_QUAD };
    XrCompositionLayerEquirect2KHR eq[2];
    XrCompositionLayerCylinderKHR vc[2], pc;
    XrCompositionLayerPassthroughFB pl;
    int curved = vr_pref(VR_PREF_CURVED) && X.has_cylinder;
    if (X.frame.shouldRender && X.pt && X.ptl) {   /* the room, behind everything */
        memset(&pl, 0, sizeof pl);
        pl.type = XR_TYPE_COMPOSITION_LAYER_PASSTHROUGH_FB;
        pl.flags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
        pl.layerHandle = X.ptl;
        layers[n++] = (const XrCompositionLayerBaseHeader *)&pl;
    }
    if (X.frame.shouldRender && X.ever_released) {
        /* behind the panel: the film, where there is one on show */
        VideoInfo vi; video_info(&vi);
        vr_film_step(&vi, PANEL_M_W * X.scale, PANEL_M_H * X.scale, X.dist);   /* the film, the room's light and the subtitles, drawn for this frame */
        const ViewMode *vm = &MODES[V.mode];
        if (vr_pref(VR_PREF_SIZE) == 4 && vi.width > 0 && vi.height > 0) {   /* fit the film: a wide film gets a wider screen, the height stays comfortable */
            int key = vi.width * 7919 + vi.height;
            if (key != X.auto_key) {
                X.auto_key = key;
                float a = (float)vi.width / (float)vi.height;
                float sc = sqrtf(a / 1.7778f) * 1.1f;
                X.scale = sc < 0.9f ? 0.9f : sc > 1.6f ? 1.6f : sc;
            }
        }
        int on_show = (vi.state == VIDEO_PLAYING || vi.state == VIDEO_PAUSED) && vi.width > 0 && vi.height > 0;
        VrChain *fc = vr_film_chain();   /* the app's own drawing of the film (the room's light is on), else the compositor's surface */
        XrSwapchain film_sc = fc ? (XrSwapchain)fc->handle : V.chain;
        int film_ok = fc ? fc->ever : (V.ok && V.chain);
        int swap_eyes = vr_pref(VR_PREF_EYES);
        /* the room's light, behind the screen and the film */
        VrChain *rc0 = vr_room_chain(0);
        if (on_show && rc0 && rc0->ever) {
            float mw = PANEL_M_W * X.scale, mh = PANEL_M_H * X.scale, L = vr_room_length(X.dist);
            static const float AX[3] = { 1, 0, 0 }, AY[3] = { 0, 1, 0 }, AZ[3] = { 0, 0, 1 };
            float ax[3], ay[3], az[3];
            vr_rotate(X.panel_rot, AX, ax); vr_rotate(X.panel_rot, AY, ay); vr_rotate(X.panel_rot, AZ, az);
            for (int i = 0; i < VR_ROOM_N; i++) {
                VrChain *rc = vr_room_chain(i);
                if (!rc || !rc->ever) continue;
                float lq[4] = { 0, 0, 0, 1 }, cen[3] = { X.panel_pos[0], X.panel_pos[1], X.panel_pos[2] }, sw, sh;
                const float H = 0.70710678f;   /* sin and cos of 45 degrees: a quarter turn */
                switch (i) {
                case 0: sw = mw * VR_ROOM_BACK_K; sh = mh * VR_ROOM_BACK_K; for (int k = 0; k < 3; k++) cen[k] -= az[k] * 0.04f; break;   /* the wall behind the screen */
                case 1: lq[0] = -H; lq[3] = H; sw = mw * VR_ROOM_SPAN_K; sh = L; for (int k = 0; k < 3; k++) cen[k] += -ay[k] * mh * 0.5f + az[k] * L * 0.5f; break;   /* the floor: turned to face up */
                case 2: lq[0] = H; lq[3] = H; sw = mw * VR_ROOM_SPAN_K; sh = L; for (int k = 0; k < 3; k++) cen[k] += ay[k] * mh * 0.5f + az[k] * L * 0.5f; break;   /* the ceiling: facing down */
                case 3: lq[1] = H; lq[3] = H; sw = L; sh = mh * VR_ROOM_SPAN_K; for (int k = 0; k < 3; k++) cen[k] += -ax[k] * mw * 0.5f + az[k] * L * 0.5f; break;   /* the left wall: facing in */
                default: lq[1] = -H; lq[3] = H; sw = L; sh = mh * VR_ROOM_SPAN_K; for (int k = 0; k < 3; k++) cen[k] += ax[k] * mw * 0.5f + az[k] * L * 0.5f; break;   /* the right wall */
                }
                const float *a = X.panel_rot;
                XrCompositionLayerQuad *rq = &roomq[i];
                memset(rq, 0, sizeof *rq);
                rq->type = XR_TYPE_COMPOSITION_LAYER_QUAD;
                rq->layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
                rq->space = X.local; rq->eyeVisibility = XR_EYE_VISIBILITY_BOTH;
                rq->subImage.swapchain = (XrSwapchain)rc->handle;
                rq->subImage.imageRect.extent.width = rc->w; rq->subImage.imageRect.extent.height = rc->h;
                rq->pose.orientation.x = a[3] * lq[0] + a[0] * lq[3] + a[1] * lq[2] - a[2] * lq[1];   /* the panel's turn, then the surface's own */
                rq->pose.orientation.y = a[3] * lq[1] - a[0] * lq[2] + a[1] * lq[3] + a[2] * lq[0];
                rq->pose.orientation.z = a[3] * lq[2] + a[0] * lq[1] - a[1] * lq[0] + a[2] * lq[3];
                rq->pose.orientation.w = a[3] * lq[3] - a[0] * lq[0] - a[1] * lq[1] - a[2] * lq[2];
                rq->pose.position.x = cen[0]; rq->pose.position.y = cen[1]; rq->pose.position.z = cen[2];
                rq->size.width = sw; rq->size.height = sh;
                layers[n++] = (const XrCompositionLayerBaseHeader *)rq;
            }
        }
        if (film_ok && on_show) {
            int eyes = vm->stereo ? 2 : 1;
            for (int e = 0; e < eyes; e++) {
                XrSwapchainSubImage si; memset(&si, 0, sizeof si);
                int half = swap_eyes ? 1 - e : e;   /* which half of the picture this eye looks at */
                si.swapchain = film_sc;
                si.imageRect.extent.width = VIDEO_PX_W; si.imageRect.extent.height = VIDEO_PX_H;
                if (vm->stereo == 1) { si.imageRect.extent.width = VIDEO_PX_W / 2; si.imageRect.offset.x = half ? VIDEO_PX_W / 2 : 0; }
                if (vm->stereo == 2) { si.imageRect.extent.height = VIDEO_PX_H / 2; si.imageRect.offset.y = half ? VIDEO_PX_H / 2 : 0; }
                XrEyeVisibility vis = !vm->stereo ? XR_EYE_VISIBILITY_BOTH : (e ? XR_EYE_VISIBILITY_RIGHT : XR_EYE_VISIBILITY_LEFT);
                if (vm->proj) {   /* the inside of half or all of a sphere around where your head was */
                    XrCompositionLayerEquirect2KHR *l = &eq[e];
                    memset(l, 0, sizeof *l);
                    l->type = XR_TYPE_COMPOSITION_LAYER_EQUIRECT2_KHR;
                    l->space = X.local; l->eyeVisibility = vis; l->subImage = si;
                    l->pose.orientation.x = X.panel_rot[0]; l->pose.orientation.y = X.panel_rot[1]; l->pose.orientation.z = X.panel_rot[2]; l->pose.orientation.w = X.panel_rot[3];
                    l->pose.position.x = X.head_pos[0]; l->pose.position.y = X.head_pos[1]; l->pose.position.z = X.head_pos[2];
                    l->radius = 50.0f;
                    l->centralHorizontalAngle = vm->proj == 1 ? 3.14159265f : 6.2831853f;
                    l->upperVerticalAngle = 1.5707963f; l->lowerVerticalAngle = -1.5707963f;
                    layers[n++] = (const XrCompositionLayerBaseHeader *)l;
                } else {   /* a flat screen inside the box the player screen gave, the shape of one eye's picture */
                    float a = (float)vi.width / (float)vi.height;
                    if (vm->stereo == 1 && a >= 3.0f) a *= 0.5f;   /* full width side by side: each half is already a whole picture */
                    if (vm->stereo == 2 && a < 1.2f) a *= 2.0f;    /* full height top and bottom */
                    int bx = V.bx, by = V.by, bw = V.bw, bh = V.bh;
                    if (bw <= 0 || bh <= 0) { bx = 0; by = 0; bw = PANEL_PX_W; bh = PANEL_PX_H; }
                    if (bx < 0) { bw += bx; bx = 0; }
                    if (by < 0) { bh += by; by = 0; }
                    if (bx + bw > PANEL_PX_W) bw = PANEL_PX_W - bx;
                    if (by + bh > PANEL_PX_H) bh = PANEL_PX_H - by;
                    if (bw < 2 || bh < 2) continue;
                    float fw, fh;
                    if ((float)bw / (float)bh > a) { fh = (float)bh; fw = fh * a; } else { fw = (float)bw; fh = fw / a; }
                    float mw = PANEL_M_W * X.scale, mh = PANEL_M_H * X.scale;
                    float ox = ((bx + bw * 0.5f) / PANEL_PX_W - 0.5f) * mw, oy = -((by + bh * 0.5f) / PANEL_PX_H - 0.5f) * mh;
                    static const float AX[3] = { 1, 0, 0 }, AY[3] = { 0, 1, 0 };
                    float ax[3], ay[3];
                    vr_rotate(X.panel_rot, AX, ax); vr_rotate(X.panel_rot, AY, ay);
                    if (curved) {   /* the same rectangle on the bend */
                        float R = X.dist, angFull = cyl_angle();
                        float theta = ((bx + bw * 0.5f) / PANEL_PX_W - 0.5f) * angFull;
                        float qy[4] = { 0, sinf(-theta / 2), 0, cosf(-theta / 2) }, qo[4];
                        const float *a = X.panel_rot;   /* the panel's turn, then the turn about the bend's own vertical */
                        qo[0] = a[3] * qy[0] + a[0] * qy[3] + a[1] * qy[2] - a[2] * qy[1];
                        qo[1] = a[3] * qy[1] - a[0] * qy[2] + a[1] * qy[3] + a[2] * qy[0];
                        qo[2] = a[3] * qy[2] + a[0] * qy[1] - a[1] * qy[0] + a[2] * qy[3];
                        qo[3] = a[3] * qy[3] - a[0] * qy[0] - a[1] * qy[1] - a[2] * qy[2];
                        static const float AZ[3] = { 0, 0, 1 };
                        float az[3]; vr_rotate(X.panel_rot, AZ, az);
                        XrCompositionLayerCylinderKHR *c = &vc[e];
                        memset(c, 0, sizeof *c);
                        c->type = XR_TYPE_COMPOSITION_LAYER_CYLINDER_KHR;
                        c->space = X.local; c->eyeVisibility = vis; c->subImage = si;
                        c->pose.orientation.x = qo[0]; c->pose.orientation.y = qo[1]; c->pose.orientation.z = qo[2]; c->pose.orientation.w = qo[3];
                        c->pose.position.x = X.panel_pos[0] + az[0] * R + ay[0] * oy;   /* the bend's axis: its radius behind the screen, level with the picture */
                        c->pose.position.y = X.panel_pos[1] + az[1] * R + ay[1] * oy;
                        c->pose.position.z = X.panel_pos[2] + az[2] * R + ay[2] * oy;
                        c->radius = R;
                        c->centralAngle = angFull * fw / PANEL_PX_W;
                        c->aspectRatio = (R * c->centralAngle) / (mh * fh / PANEL_PX_H);
                        layers[n++] = (const XrCompositionLayerBaseHeader *)c;
                        continue;
                    }
                    XrCompositionLayerQuad *l = &vq[e];
                    memset(l, 0, sizeof *l);
                    l->type = XR_TYPE_COMPOSITION_LAYER_QUAD;
                    l->space = X.local; l->eyeVisibility = vis; l->subImage = si;
                    l->pose.orientation.x = X.panel_rot[0]; l->pose.orientation.y = X.panel_rot[1]; l->pose.orientation.z = X.panel_rot[2]; l->pose.orientation.w = X.panel_rot[3];
                    l->pose.position.x = X.panel_pos[0] + ax[0] * ox + ay[0] * oy;
                    l->pose.position.y = X.panel_pos[1] + ax[1] * ox + ay[1] * oy;
                    l->pose.position.z = X.panel_pos[2] + ax[2] * ox + ay[2] * oy;
                    l->size.width = mw * fw / PANEL_PX_W; l->size.height = mh * fh / PANEL_PX_H;
                    layers[n++] = (const XrCompositionLayerBaseHeader *)l;
                }
            }
        }
        /* the subtitles, nearer than the screen, the same size to the eye */
        {
            VrChain *sc; int sr[4]; float cyf, wf;
            if (vr_subs_layer(&sc, sr, &cyf, &wf) && sc->ever) {
                static const float AY2[3] = { 0, 1, 0 };
                float ay2[3]; vr_rotate(X.panel_rot, AY2, ay2);
                float mw = PANEL_M_W * X.scale, mh = PANEL_M_H * X.scale, ratio = vr_subs_ratio();
                float on_screen[3] = { X.panel_pos[0] + ay2[0] * -(cyf - 0.5f) * mh, X.panel_pos[1] + ay2[1] * -(cyf - 0.5f) * mh, X.panel_pos[2] + ay2[2] * -(cyf - 0.5f) * mh };
                memset(&subq, 0, sizeof subq);
                subq.type = XR_TYPE_COMPOSITION_LAYER_QUAD;
                subq.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
                subq.space = X.local; subq.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
                subq.subImage.swapchain = (XrSwapchain)sc->handle;
                subq.subImage.imageRect.offset.x = sr[0]; subq.subImage.imageRect.offset.y = sr[1];
                subq.subImage.imageRect.extent.width = sr[2]; subq.subImage.imageRect.extent.height = sr[3];
                subq.pose.orientation.x = X.panel_rot[0]; subq.pose.orientation.y = X.panel_rot[1]; subq.pose.orientation.z = X.panel_rot[2]; subq.pose.orientation.w = X.panel_rot[3];
                for (int k = 0; k < 3; k++) { float p = X.head_pos[k] + (on_screen[k] - X.head_pos[k]) * ratio; if (k == 0) subq.pose.position.x = p; else if (k == 1) subq.pose.position.y = p; else subq.pose.position.z = p; }
                subq.size.width = wf * mw * ratio; subq.size.height = subq.size.width * (float)sr[3] / (float)sr[2];
                layers[n++] = (const XrCompositionLayerBaseHeader *)&subq;
            }
        }
        /* the dock under the screen: it fades in when you point near the bottom of the screen and out after a moment */
        {
            int want = SDL_GetTicks() < X.dock_until || X.dock_drag;
            X.dock_alpha += ((want ? 1.0f : 0.0f) - X.dock_alpha) * 0.22f;
            if (X.dock_alpha < 0.02f) X.dock_alpha = 0;
            if (X.dock_alpha > 0 && !X.dock.handle && !X.dock_failed && vr_chain_make(&X.dock, PANEL_PX_W, PANEL_PX_H) != 0) { X.dock_failed = 1; LOGW("vr: no swapchain for the dock"); }
            if (X.dock_alpha > 0 && X.dock.handle) {
                int q = (int)(X.dock_alpha * 12 + 0.5f), cv = vr_pref(VR_PREF_CURVED);
                if (q != X.dock_q || X.dock_hover != X.dock_dh || X.dock_pressed != X.dock_dp || cv != X.dock_dc) {
                    vr_dock_render(&X.dock, X.dock_hover, X.dock_pressed, q / 12.0f);
                    X.dock_q = q; X.dock_dh = X.dock_hover; X.dock_dp = X.dock_pressed; X.dock_dc = cv;
                }
                if (X.dock.ever) {
                    float dp[3]; dock_pos(dp);
                    memset(&dockq, 0, sizeof dockq);
                    dockq.type = XR_TYPE_COMPOSITION_LAYER_QUAD;
                    dockq.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
                    dockq.space = X.local; dockq.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
                    dockq.subImage.swapchain = (XrSwapchain)X.dock.handle;
                    dockq.subImage.imageRect.offset.x = (PANEL_PX_W - DOCK_W) / 2; dockq.subImage.imageRect.offset.y = (PANEL_PX_H - DOCK_H) / 2;   /* centred: it does not matter which way up the rows run */
                    dockq.subImage.imageRect.extent.width = DOCK_W; dockq.subImage.imageRect.extent.height = DOCK_H;
                    dockq.pose.orientation.x = X.panel_rot[0]; dockq.pose.orientation.y = X.panel_rot[1]; dockq.pose.orientation.z = X.panel_rot[2]; dockq.pose.orientation.w = X.panel_rot[3];
                    dockq.pose.position.x = dp[0]; dockq.pose.position.y = dp[1]; dockq.pose.position.z = dp[2];
                    dockq.size.width = dock_w(); dockq.size.height = dock_h();
                    layers[n++] = (const XrCompositionLayerBaseHeader *)&dockq;
                }
            }
        }
        /* the panel: the app's own screens, see-through where the player left a hole for the picture */
        if (curved) {
            static const float AZ[3] = { 0, 0, 1 };
            float az[3]; vr_rotate(X.panel_rot, AZ, az);
            float R = X.dist;
            memset(&pc, 0, sizeof pc);
            pc.type = XR_TYPE_COMPOSITION_LAYER_CYLINDER_KHR;
            pc.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
            pc.space = X.local; pc.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
            pc.subImage.swapchain = X.chain;
            pc.subImage.imageRect.extent.width = PANEL_PX_W; pc.subImage.imageRect.extent.height = PANEL_PX_H;
            pc.pose.orientation.x = X.panel_rot[0]; pc.pose.orientation.y = X.panel_rot[1]; pc.pose.orientation.z = X.panel_rot[2]; pc.pose.orientation.w = X.panel_rot[3];
            pc.pose.position.x = X.panel_pos[0] + az[0] * R; pc.pose.position.y = X.panel_pos[1] + az[1] * R; pc.pose.position.z = X.panel_pos[2] + az[2] * R;
            pc.radius = R;
            pc.centralAngle = cyl_angle();
            pc.aspectRatio = (float)PANEL_PX_W / (float)PANEL_PX_H;
            layers[n++] = (const XrCompositionLayerBaseHeader *)&pc;
        } else {
            q.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
            q.space = X.local;
            q.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
            q.subImage.swapchain = X.chain;
            q.subImage.imageRect.offset.x = 0; q.subImage.imageRect.offset.y = 0;
            q.subImage.imageRect.extent.width = PANEL_PX_W; q.subImage.imageRect.extent.height = PANEL_PX_H;
            q.subImage.imageArrayIndex = 0;
            q.pose.orientation.x = X.panel_rot[0]; q.pose.orientation.y = X.panel_rot[1]; q.pose.orientation.z = X.panel_rot[2]; q.pose.orientation.w = X.panel_rot[3];
            q.pose.position.x = X.panel_pos[0]; q.pose.position.y = X.panel_pos[1]; q.pose.position.z = X.panel_pos[2];
            q.size.width = PANEL_M_W * X.scale; q.size.height = PANEL_M_H * X.scale;
            layers[n++] = (const XrCompositionLayerBaseHeader *)&q;
        }
        /* the control box: in front of everything while it is open */
        if (vr_cinema_is_open() && X.box_placed) {
            if (!X.box.handle && !X.box_failed && vr_chain_make(&X.box, PANEL_PX_W, PANEL_PX_H) != 0) { X.box_failed = 1; LOGW("vr: no swapchain for the control box"); }
            if (X.box.handle) {
                int hov = X.box_on ? vr_box_hover(X.box_px, X.box_py) : 0;
                if (!X.box.ever || vr_box_version() != X.box_ver || hov != X.box_hover_drawn) {
                    vr_box_render(&X.box, X.box_on ? X.box_px : -1, X.box_on ? X.box_py : -1);
                    X.box_ver = vr_box_version(); X.box_hover_drawn = hov;
                }
                if (X.box.ever) {
                    static XrCompositionLayerQuad bq;
                    memset(&bq, 0, sizeof bq);
                    bq.type = XR_TYPE_COMPOSITION_LAYER_QUAD;
                    bq.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
                    bq.space = X.local; bq.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
                    bq.subImage.swapchain = (XrSwapchain)X.box.handle;
                    bq.subImage.imageRect.offset.x = (PANEL_PX_W - VR_BOX_W) / 2; bq.subImage.imageRect.offset.y = (PANEL_PX_H - VR_BOX_H) / 2;   /* the middle band: it does not matter which way up the rows run */
                    bq.subImage.imageRect.extent.width = VR_BOX_W; bq.subImage.imageRect.extent.height = VR_BOX_H;
                    bq.pose.orientation.x = X.box_rot[0]; bq.pose.orientation.y = X.box_rot[1]; bq.pose.orientation.z = X.box_rot[2]; bq.pose.orientation.w = X.box_rot[3];
                    bq.pose.position.x = X.box_pos[0]; bq.pose.position.y = X.box_pos[1]; bq.pose.position.z = X.box_pos[2];
                    bq.size.width = box_w(); bq.size.height = box_h();
                    layers[n++] = (const XrCompositionLayerBaseHeader *)&bq;
                }
            }
        }
    }
    XrFrameEndInfo fe = { XR_TYPE_FRAME_END_INFO };
    fe.displayTime = X.frame.predictedDisplayTime;
    fe.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    fe.layerCount = n; fe.layers = n ? layers : NULL;
    if (xr_ok(xrEndFrame(X.session, &fe), "xrEndFrame") && n) { static int said; if (!said) { said = 1; LOGI("vr: the first frame is on the panel"); } }
    X.in_frame = 0;
}

void vr_shutdown(void)
{
    if (V.surface) { JNIEnv *je = (JNIEnv *)SDL_AndroidGetJNIEnv(); if (je) (*je)->DeleteGlobalRef(je, V.surface); }
    memset(&V, 0, sizeof V);
    X.pt = XR_NULL_HANDLE; X.ptl = XR_NULL_HANDLE;   /* they go with the session */
    if (X.session) { if (X.running) xrEndSession(X.session); xrDestroySession(X.session); }
    if (X.inst) xrDestroyInstance(X.inst);
    memset(&X, 0, sizeof X);
}

#endif
