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
    float grab_dist;
    XrAction x_click, y_click, squeeze;
    int has_equirect;
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
    int has_gles = 0, has_android = 0, has_surface = 0;
    for (uint32_t i = 0; i < ne; i++) {
        if (!strcmp(ep[i].extensionName, XR_KHR_OPENGL_ES_ENABLE_EXTENSION_NAME)) has_gles = 1;
        if (!strcmp(ep[i].extensionName, XR_KHR_ANDROID_CREATE_INSTANCE_EXTENSION_NAME)) has_android = 1;
        if (!strcmp(ep[i].extensionName, XR_KHR_ANDROID_SURFACE_SWAPCHAIN_EXTENSION_NAME)) has_surface = 1;
        if (!strcmp(ep[i].extensionName, XR_KHR_COMPOSITION_LAYER_EQUIRECT2_EXTENSION_NAME)) X.has_equirect = 1;
    }
    free(ep);
    LOGI("vr: the runtime offers %u extensions (OpenGL ES %s, Android instance %s, video surface %s, 180/360 %s)", ne, has_gles ? "yes" : "NO", has_android ? "yes" : "NO", has_surface ? "yes" : "NO", X.has_equirect ? "yes" : "no");
    if (!has_gles || !has_android) return -1;

    const char *want[4] = { XR_KHR_OPENGL_ES_ENABLE_EXTENSION_NAME, XR_KHR_ANDROID_CREATE_INSTANCE_EXTENSION_NAME };
    uint32_t n_want = 2;
    if (has_surface) want[n_want++] = XR_KHR_ANDROID_SURFACE_SWAPCHAIN_EXTENSION_NAME;
    if (X.has_equirect) want[n_want++] = XR_KHR_COMPOSITION_LAYER_EQUIRECT2_EXTENSION_NAME;
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

    /* the panel's first place, before the head has been located: straight ahead */
    const float p0[3] = { 0, -PANEL_LOWER, -PANEL_DIST }; memcpy(X.panel_pos, p0, sizeof p0);
    X.panel_rot[0] = X.panel_rot[1] = X.panel_rot[2] = 0; X.panel_rot[3] = 1;
    LOGI("vr: session created, waiting for the headset to be ready");
    return 0;
}

static void place_panel_in_front(XrTime t)
{
    XrSpaceLocation loc = { XR_TYPE_SPACE_LOCATION };
    if (!XR_SUCCEEDED(xrLocateSpace(X.view, X.local, t, &loc))) return;
    if (!(loc.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) || !(loc.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT)) return;
    float hp[3] = { loc.pose.position.x, loc.pose.position.y, loc.pose.position.z };
    float hr[4] = { loc.pose.orientation.x, loc.pose.orientation.y, loc.pose.orientation.z, loc.pose.orientation.w };
    vr_panel_in_front(hp, hr, X.dist, PANEL_LOWER, X.panel_pos, X.panel_rot);
    memcpy(X.head_pos, hp, sizeof hp);
    X.panel_placed = 1;
    LOGI("vr: the panel is %.1f m ahead of you", X.dist);
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
    float trig[2] = { 0, 0 }, sx = 0, sy2 = 0;
    for (int h = 0; h < 2; h++) {
        XrActionStateGetInfo gi = { XR_TYPE_ACTION_STATE_GET_INFO };
        gi.subactionPath = X.hand[h];
        XrActionStateFloat tf = { XR_TYPE_ACTION_STATE_FLOAT };
        gi.action = X.trigger;
        if (XR_SUCCEEDED(xrGetActionStateFloat(X.session, &gi, &tf)) && tf.isActive) trig[h] = tf.currentState;
        if (trig[h] > best_trig + 0.05f) { best_trig = trig[h]; hand = h; }
        XrActionStateVector2f sv = { XR_TYPE_ACTION_STATE_VECTOR2F };
        gi.action = X.stick;
        if (XR_SUCCEEDED(xrGetActionStateVector2f(X.session, &gi, &sv)) && sv.isActive && (sv.currentState.x * sv.currentState.x + sv.currentState.y * sv.currentState.y) > sx * sx + sy2 * sy2) { sx = sv.currentState.x; sy2 = sv.currentState.y; }
    }
    X.active_hand = hand;

    /* the pointer: the active hand's aim ray against the panel */
    XrSpaceLocation loc = { XR_TYPE_SPACE_LOCATION };
    if (XR_SUCCEEDED(xrLocateSpace(X.aim_space[hand], X.local, t, &loc)) && (loc.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) && (loc.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT)) {
        float o[3] = { loc.pose.position.x, loc.pose.position.y, loc.pose.position.z };
        float q[4] = { loc.pose.orientation.x, loc.pose.orientation.y, loc.pose.orientation.z, loc.pose.orientation.w };
        const float fwd[3] = { 0, 0, -1 };
        float d[3];
        vr_rotate(q, fwd, d);
        float u, v, dist;
        if (vr_ray_panel(o, d, X.panel_pos, X.panel_rot, PANEL_M_W * X.scale, PANEL_M_H * X.scale, &u, &v, &dist)) { X.input.have_pointer = 1; X.input.x = u; X.input.y = v; }
    }
    /* the grip: take hold of the screen and carry it (the thumbstick then pushes and pulls it, and makes it bigger or smaller) */
    float grip[2] = { 0, 0 };
    for (int h = 0; h < 2; h++) {
        XrActionStateGetInfo gi = { XR_TYPE_ACTION_STATE_GET_INFO };
        gi.action = X.squeeze; gi.subactionPath = X.hand[h];
        XrActionStateFloat gf = { XR_TYPE_ACTION_STATE_FLOAT };
        if (XR_SUCCEEDED(xrGetActionStateFloat(X.session, &gi, &gf)) && gf.isActive) grip[h] = gf.currentState;
    }
    int gh = grip[1] > grip[0] ? 1 : 0;
    int want_grab = grip[gh] > (X.grab ? 0.4f : 0.7f);
    static Uint32 last_ms;
    Uint32 ms = SDL_GetTicks();
    float dt = last_ms ? (ms - last_ms) / 1000.0f : 0.016f;
    if (dt > 0.1f) dt = 0.1f;
    last_ms = ms;
    if (want_grab) {
        XrSpaceLocation gl = { XR_TYPE_SPACE_LOCATION };
        XrSpaceLocation hl = { XR_TYPE_SPACE_LOCATION };
        if (XR_SUCCEEDED(xrLocateSpace(X.aim_space[gh], X.local, t, &gl)) && (gl.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) && (gl.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT)) {
            float o[3] = { gl.pose.position.x, gl.pose.position.y, gl.pose.position.z };
            float q[4] = { gl.pose.orientation.x, gl.pose.orientation.y, gl.pose.orientation.z, gl.pose.orientation.w };
            const float fwd[3] = { 0, 0, -1 };
            float d[3];
            vr_rotate(q, fwd, d);
            if (!X.grab) {   /* the screen stays as far from the hand as it is */
                float dx = X.panel_pos[0] - o[0], dy = X.panel_pos[1] - o[1], dz = X.panel_pos[2] - o[2];
                X.grab_dist = sqrtf(dx * dx + dy * dy + dz * dz);
                X.grab = 1;
            }
            X.grab_dist += sy2 * 1.6f * dt;   /* the stick up pushes it away, down pulls it in */
            if (X.grab_dist < 0.8f) X.grab_dist = 0.8f;
            if (X.grab_dist > 8.0f) X.grab_dist = 8.0f;
            X.scale *= 1.0f + sx * 0.9f * dt;   /* right bigger, left smaller */
            if (X.scale < 0.4f) X.scale = 0.4f;
            if (X.scale > 3.0f) X.scale = 3.0f;
            for (int k = 0; k < 3; k++) X.panel_pos[k] = o[k] + d[k] * X.grab_dist;
            if (XR_SUCCEEDED(xrLocateSpace(X.view, X.local, t, &hl)) && (hl.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT)) {   /* and it faces you, upright */
                float fx = X.panel_pos[0] - hl.pose.position.x, fz = X.panel_pos[2] - hl.pose.position.z;
                float yaw = atan2f(-fx, -fz);
                X.panel_rot[0] = 0; X.panel_rot[1] = sinf(yaw / 2); X.panel_rot[2] = 0; X.panel_rot[3] = cosf(yaw / 2);
                X.head_pos[0] = hl.pose.position.x; X.head_pos[1] = hl.pose.position.y; X.head_pos[2] = hl.pose.position.z;
                X.dist = sqrtf(fx * fx + fz * fz);   /* where the menu button brings it back to */
                if (X.dist < 0.8f) X.dist = 0.8f;
            }
        }
    } else X.grab = 0;
    X.input.grabbing = X.grab;
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
    X.input.menu = edge(pressed[2], &X.pressed_was[2]);
    X.input.play = edge(pressed[4], &X.pressed_was[3]);
    X.input.view = edge(pressed[5], &X.pressed_was[4]);

    /* the thumbstick as arrow keys: a push is one press, held it repeats after a pause */
    int dir = 0;   /* 1 up 2 down 3 left 4 right */
    if (sx * sx + sy2 * sy2 > 0.5f * 0.5f) dir = (sy2 > 0 ? sy2 : -sy2) > (sx > 0 ? sx : -sx) ? (sy2 > 0 ? 1 : 2) : (sx < 0 ? 3 : 4);
    double now = SDL_GetTicks() / 1000.0;
    int fire = 0;
    if (dir != X.stick_dir_was) { X.stick_dir_was = dir; X.stick_repeat_at = now + 0.4; fire = dir != 0; }
    else if (dir && now >= X.stick_repeat_at) { X.stick_repeat_at = now + 0.12; fire = 1; }
    if (fire) { X.input.up_k = dir == 1; X.input.down_k = dir == 2; X.input.left_k = dir == 3; X.input.right_k = dir == 4; }
}

int vr_init(void)
{
    int r = vr_init_inner();
    if (r != 0) vr_shutdown();   /* a session left open would keep the headset waiting for frames that never come */
    return r;
}

int vr_active(void) { return X.session != XR_NULL_HANDLE; }
int vr_quit_requested(void) { return X.quit; }
void vr_input(VrInput *in) { *in = X.input; }

int vr_frame_begin(int dirty, int *w, int *h)
{
    X.in_frame = 0; X.acquired = 0;
    poll_events();
    if (!X.running) return -1;
    X.frame.type = XR_TYPE_FRAME_STATE;
    if (!xr_ok(xrWaitFrame(X.session, NULL, &X.frame), "xrWaitFrame")) return -1;
    if (!xr_ok(xrBeginFrame(X.session, NULL), "xrBeginFrame")) return -1;
    X.in_frame = 1;
    if (!X.panel_placed) place_panel_in_front(X.frame.predictedDisplayTime);
    sync_input(X.frame.predictedDisplayTime);
    if (X.input.menu) place_panel_in_front(X.frame.predictedDisplayTime);   /* the left menu button: the panel comes back in front of you */
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
    const XrCompositionLayerBaseHeader *layers[4];
    uint32_t n = 0;
    XrCompositionLayerQuad vq[2], q = { XR_TYPE_COMPOSITION_LAYER_QUAD };
    XrCompositionLayerEquirect2KHR eq[2];
    if (X.frame.shouldRender && X.ever_released) {
        /* behind the panel: the film, where there is one on show */
        VideoInfo vi; video_info(&vi);
        const ViewMode *vm = &MODES[V.mode];
        if (V.ok && V.chain && (vi.state == VIDEO_PLAYING || vi.state == VIDEO_PAUSED) && vi.width > 0 && vi.height > 0) {
            int eyes = vm->stereo ? 2 : 1;
            for (int e = 0; e < eyes; e++) {
                XrSwapchainSubImage si; memset(&si, 0, sizeof si);
                si.swapchain = V.chain;
                si.imageRect.extent.width = VIDEO_PX_W; si.imageRect.extent.height = VIDEO_PX_H;
                if (vm->stereo == 1) { si.imageRect.extent.width = VIDEO_PX_W / 2; si.imageRect.offset.x = e ? VIDEO_PX_W / 2 : 0; }
                if (vm->stereo == 2) { si.imageRect.extent.height = VIDEO_PX_H / 2; si.imageRect.offset.y = e ? VIDEO_PX_H / 2 : 0; }
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
        /* the panel: the app's own screens, see-through where the player left a hole for the picture */
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
    if (X.session) { if (X.running) xrEndSession(X.session); xrDestroySession(X.session); }
    if (X.inst) xrDestroyInstance(X.inst);
    memset(&X, 0, sizeof X);
}

#endif
