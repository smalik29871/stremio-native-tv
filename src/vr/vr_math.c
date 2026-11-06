#include "vr_math.h"
#include <math.h>

void vr_rotate(const float q[4], const float v[3], float out[3])
{
    /* v' = v + 2w(q x v) + 2 q x (q x v) */
    float qx = q[0], qy = q[1], qz = q[2], qw = q[3];
    float cx = qy * v[2] - qz * v[1], cy = qz * v[0] - qx * v[2], cz = qx * v[1] - qy * v[0];
    float dx = qy * cz - qz * cy, dy = qz * cx - qx * cz, dz = qx * cy - qy * cx;
    out[0] = v[0] + 2 * (qw * cx + dx);
    out[1] = v[1] + 2 * (qw * cy + dy);
    out[2] = v[2] + 2 * (qw * cz + dz);
}

static float dot3(const float a[3], const float b[3]) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

int vr_ray_panel(const float origin[3], const float dir[3], const float pos[3], const float rot[4],
                 float width, float height, float *u, float *v, float *dist)
{
    static const float X[3] = { 1, 0, 0 }, Y[3] = { 0, 1, 0 }, Z[3] = { 0, 0, 1 };
    float n[3], ax[3], ay[3];
    vr_rotate(rot, Z, n); vr_rotate(rot, X, ax); vr_rotate(rot, Y, ay);
    float denom = dot3(n, dir);
    if (fabsf(denom) < 1e-5f || width <= 0 || height <= 0) return 0;   /* along the panel */
    float to[3] = { pos[0] - origin[0], pos[1] - origin[1], pos[2] - origin[2] };
    float t = dot3(n, to) / denom;
    if (t <= 0) return 0;   /* behind the hand */
    float p[3] = { origin[0] + t * dir[0] - pos[0], origin[1] + t * dir[1] - pos[1], origin[2] + t * dir[2] - pos[2] };
    float lx = dot3(p, ax), ly = dot3(p, ay);
    float fu = lx / width + 0.5f, fv = 0.5f - ly / height;
    if (dist) *dist = t;
    if (u) *u = fu;
    if (v) *v = fv;
    return fu >= 0 && fu <= 1 && fv >= 0 && fv <= 1;
}

void vr_panel_in_front(const float head_pos[3], const float head_rot[4], float distance, float lower,
                       float out_pos[3], float out_rot[4])
{
    static const float FWD[3] = { 0, 0, -1 };
    float f[3];
    vr_rotate(head_rot, FWD, f);
    float len = sqrtf(f[0] * f[0] + f[2] * f[2]);
    if (len < 1e-4f) { f[0] = 0; f[2] = -1; len = 1; }   /* looking straight up or down: the way it faced before */
    f[0] /= len; f[2] /= len;
    out_pos[0] = head_pos[0] + f[0] * distance;
    out_pos[1] = head_pos[1] - lower;
    out_pos[2] = head_pos[2] + f[2] * distance;
    /* the panel's +Z toward the viewer: opposite of the way the head looks, a turn about +Y */
    float yaw = atan2f(-f[0], -f[2]);   /* the angle taking +Z to (-f) */
    out_rot[0] = 0; out_rot[1] = sinf(yaw / 2); out_rot[2] = 0; out_rot[3] = cosf(yaw / 2);
}
