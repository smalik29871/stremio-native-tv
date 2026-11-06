/* The headset's own overlays on a desktop: the Cinema sheet and the dock drawn by the same code over a screen of the app
 * (SNTV_VR_PREVIEW=sheet or dock in the environment), to look at them without a headset. Not part of any release build. */
#define SNTV_VR 1   /* this file stands in for the headset side: it sees the real declarations */
#include "vr.h"
#include "vr_internal.h"
#include "../ui/gfx.h"
#include <stdlib.h>
#include <string.h>

/* what the OpenXR side would answer */
int vr_passthrough_available(void) { return 1; }
void vr_recenter(void) {}
const char *vr_video_view_name(void) { return "Flat"; }
const char *vr_video_cycle(void) { return "Flat"; }
int vr_active(void) { return 1; }
int vr_chain_acquire(VrChain *c) { (void)c; return 1; }
void vr_chain_release(VrChain *c) { (void)c; }

void vr_preview_draw(void)
{
    const char *m = getenv("SNTV_VR_PREVIEW");
    if (!m || !*m) return;
    if (!strcmp(m, "sheet")) {
        static int opened;
        if (!opened) { opened = 1; vr_cinema_toggle(); }
        vr_cinema_draw(1.0f, 0);
    } else if (!strcmp(m, "dock")) {
        gfx_set_translate(0, 455);   /* under the picture, where the dock hangs */
        vr_dock_draw(getenv("SNTV_VR_HOVER") ? atoi(getenv("SNTV_VR_HOVER")) : -1, -1, 1.0f);
        gfx_set_translate(0, 0);
        vr_cinema_draw(1.0f, 1);   /* the small way in, at the bottom left */
    }
}
