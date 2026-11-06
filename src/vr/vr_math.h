#ifndef SNTV_VR_MATH_H
#define SNTV_VR_MATH_H

/* The geometry of a flat panel floating in a headset's space: where a pointing ray meets it.
 * Poses are OpenXR's: a position (metres) and a unit quaternion x y z w. A panel faces its own
 * +Z, its +X is to the right and +Y up; the viewer sits on the +Z side. */

void vr_rotate(const float q[4], const float v[3], float out[3]);

/* 1 when the ray (origin, unit direction) meets the panel in front of it; then u and v are where,
 * from 0 to 1 across and down from its top left, and dist how far along the ray. */
int vr_ray_panel(const float origin[3], const float dir[3], const float pos[3], const float rot[4],
                 float width, float height, float *u, float *v, float *dist);

/* A pose facing the viewer: in front of `head` (position and orientation) by `distance`, level with
 * the head plus `lower` metres down, turned only about the vertical to the way the head looks. */
void vr_panel_in_front(const float head_pos[3], const float head_rot[4], float distance, float lower,
                       float out_pos[3], float out_rot[4]);

#endif
