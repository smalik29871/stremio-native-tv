/* Cuelight in a headset: an OpenXR session (the Khronos loader, from the Quest runtime) with one
 * panel, the app's own canvas drawn by gfx.c into a swapchain image and submitted as a quad layer.
 * A controller's aim ray meets the panel (vr_math.c) and the trigger is a tap on it.
 * Written from the OpenXR specification and the Khronos OpenXR-SDK (Apache-2.0); nothing here is
 * from Meta's samples. docs/VR-PLAN.md has the plan and what is checked and not. */
#ifdef SNTV_VR

#include "vr.h"
#include "vr_math.h"
#include "../log.h"
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
    int trig_was[2], pressed_was[4], stick_dir_was;
    double stick_repeat_at;
    int active_hand;
    PFN_xrGetOpenGLESGraphicsRequirementsKHR get_gles_req;
} X;

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
    X.a_click = make_action(XR_ACTION_TYPE_BOOLEAN_INPUT, "ok", "OK (A or X)", 1);
    X.b_click = make_action(XR_ACTION_TYPE_BOOLEAN_INPUT, "back", "Back (B or Y)", 1);
    X.menu_click = make_action(XR_ACTION_TYPE_BOOLEAN_INPUT, "menu", "Menu", 0);

    struct { XrAction a; const char *path; } b[] = {
        { X.aim, "/user/hand/left/input/aim/pose" }, { X.aim, "/user/hand/right/input/aim/pose" },
        { X.trigger, "/user/hand/left/input/trigger/value" }, { X.trigger, "/user/hand/right/input/trigger/value" },
        { X.stick, "/user/hand/left/input/thumbstick" }, { X.stick, "/user/hand/right/input/thumbstick" },
        { X.stick_click, "/user/hand/left/input/thumbstick/click" }, { X.stick_click, "/user/hand/right/input/thumbstick/click" },
        { X.a_click, "/user/hand/right/input/a/click" }, { X.a_click, "/user/hand/left/input/x/click" },
        { X.b_click, "/user/hand/right/input/b/click" }, { X.b_click, "/user/hand/left/input/y/click" },
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
    int has_gles = 0, has_android = 0;
    for (uint32_t i = 0; i < ne; i++) {
        if (!strcmp(ep[i].extensionName, XR_KHR_OPENGL_ES_ENABLE_EXTENSION_NAME)) has_gles = 1;
        if (!strcmp(ep[i].extensionName, XR_KHR_ANDROID_CREATE_INSTANCE_EXTENSION_NAME)) has_android = 1;
    }
    free(ep);
    LOGI("vr: the runtime offers %u extensions (OpenGL ES %s, Android instance %s)", ne, has_gles ? "yes" : "NO", has_android ? "yes" : "NO");
    if (!has_gles || !has_android) return -1;

    const char *want[2] = { XR_KHR_OPENGL_ES_ENABLE_EXTENSION_NAME, XR_KHR_ANDROID_CREATE_INSTANCE_EXTENSION_NAME };
    XrInstanceCreateInfoAndroidKHR ai = { XR_TYPE_INSTANCE_CREATE_INFO_ANDROID_KHR };
    ai.applicationVM = vm; ai.applicationActivity = activity;
    XrInstanceCreateInfo ci = { XR_TYPE_INSTANCE_CREATE_INFO };
    ci.next = &ai;
    snprintf(ci.applicationInfo.applicationName, sizeof ci.applicationInfo.applicationName, "Cuelight");
    snprintf(ci.applicationInfo.engineName, sizeof ci.applicationInfo.engineName, "Cuelight");
    ci.applicationInfo.apiVersion = XR_MAKE_VERSION(1, 0, 0);
    ci.enabledExtensionCount = 2; ci.enabledExtensionNames = want;
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
    vr_panel_in_front(hp, hr, PANEL_DIST, PANEL_LOWER, X.panel_pos, X.panel_rot);
    X.panel_placed = 1;
    LOGI("vr: the panel is %.1f m ahead of you", PANEL_DIST);
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
        if (vr_ray_panel(o, d, X.panel_pos, X.panel_rot, PANEL_M_W, PANEL_M_H, &u, &v, &dist)) { X.input.have_pointer = 1; X.input.x = u; X.input.y = v; }
    }
    /* a trigger pressed hard is down, let go well below it: no flutter at the edge */
    int was = X.trig_was[0];
    X.input.down = X.input.have_pointer && (trig[hand] > (was ? 0.35f : 0.65f));
    X.trig_was[0] = X.input.down;

    /* buttons */
    int pressed[4] = { 0, 0, 0, 0 };   /* ok, back, menu, stick click */
    XrAction acts[4] = { X.a_click, X.b_click, X.menu_click, X.stick_click };
    for (int k = 0; k < 4; k++) {
        for (int h = 0; h < 2; h++) {
            XrActionStateGetInfo gi = { XR_TYPE_ACTION_STATE_GET_INFO };
            gi.action = acts[k]; gi.subactionPath = k == 2 ? XR_NULL_PATH : X.hand[h];
            XrActionStateBoolean bs = { XR_TYPE_ACTION_STATE_BOOLEAN };
            if (XR_SUCCEEDED(xrGetActionStateBoolean(X.session, &gi, &bs)) && bs.isActive && bs.currentState) pressed[k] = 1;
            if (k == 2) break;
        }
    }
    X.input.ok = edge(pressed[0] || pressed[3], &X.pressed_was[0]);
    X.input.back = edge(pressed[1], &X.pressed_was[1]);
    X.input.menu = edge(pressed[2], &X.pressed_was[2]);

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
    XrCompositionLayerQuad q = { XR_TYPE_COMPOSITION_LAYER_QUAD };
    const XrCompositionLayerBaseHeader *layers[1];
    uint32_t n = 0;
    if (X.frame.shouldRender && X.ever_released) {
        q.layerFlags = 0;
        q.space = X.local;
        q.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
        q.subImage.swapchain = X.chain;
        q.subImage.imageRect.offset.x = 0; q.subImage.imageRect.offset.y = 0;
        q.subImage.imageRect.extent.width = PANEL_PX_W; q.subImage.imageRect.extent.height = PANEL_PX_H;
        q.subImage.imageArrayIndex = 0;
        q.pose.orientation.x = X.panel_rot[0]; q.pose.orientation.y = X.panel_rot[1]; q.pose.orientation.z = X.panel_rot[2]; q.pose.orientation.w = X.panel_rot[3];
        q.pose.position.x = X.panel_pos[0]; q.pose.position.y = X.panel_pos[1]; q.pose.position.z = X.panel_pos[2];
        q.size.width = PANEL_M_W; q.size.height = PANEL_M_H;
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
    if (X.session) { if (X.running) xrEndSession(X.session); xrDestroySession(X.session); }
    if (X.inst) xrDestroyInstance(X.inst);
    memset(&X, 0, sizeof X);
}

#endif
