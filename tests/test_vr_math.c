#include "vr/vr_math.h"
#include <math.h>
#include <stdio.h>

static int failures;
#define CHECK(c) do { if (!(c)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)
#define NEAR(a, b) (fabsf((a) - (b)) < 1e-3f)

int main(void)
{
    const float ID[4] = { 0, 0, 0, 1 }, POS[3] = { 0, 0, -2 }, O[3] = { 0, 0, 0 };
    float u, v, d;

    /* straight ahead: the middle of a 2.4 x 1.35 m panel two metres away */
    const float AHEAD[3] = { 0, 0, -1 };
    CHECK(vr_ray_panel(O, AHEAD, POS, ID, 2.4f, 1.35f, &u, &v, &d));
    CHECK(NEAR(u, 0.5f) && NEAR(v, 0.5f) && NEAR(d, 2));

    /* 0.6 m to the right and 0.3375 m up from the middle: a quarter across, a quarter down */
    float to[3] = { 0.6f, 0.3375f, -2 }, len = sqrtf(0.36f + 0.1139f + 4), dir[3] = { to[0] / len, to[1] / len, to[2] / len };
    CHECK(vr_ray_panel(O, dir, POS, ID, 2.4f, 1.35f, &u, &v, &d));
    CHECK(NEAR(u, 0.75f) && NEAR(v, 0.25f));

    /* a ray from a hand off to the side still finds its place */
    const float HAND[3] = { 0.5f, -0.4f, -0.3f };
    float to2[3] = { -0.5f, 0.4f, -1.7f }, l2 = sqrtf(0.25f + 0.16f + 2.89f), dir2[3] = { to2[0] / l2, to2[1] / l2, to2[2] / l2 };
    CHECK(vr_ray_panel(HAND, dir2, POS, ID, 2.4f, 1.35f, &u, &v, &d));
    CHECK(NEAR(u, 0.5f) && NEAR(v, 0.5f));

    /* off the panel, behind the hand, and along its plane: no hit */
    float wide[3] = { 0.8f, 0, -0.6f };
    CHECK(!vr_ray_panel(O, wide, POS, ID, 2.4f, 1.35f, &u, &v, &d));
    const float BACK[3] = { 0, 0, 1 };
    CHECK(!vr_ray_panel(O, BACK, POS, ID, 2.4f, 1.35f, &u, &v, &d));
    const float SIDE[3] = { 1, 0, 0 };
    CHECK(!vr_ray_panel(O, SIDE, POS, ID, 2.4f, 1.35f, &u, &v, &d));

    /* a panel turned 90 degrees about Y, to the user's left, facing them: +X of the world is its -Z */
    const float Y90[4] = { 0, 0.70710678f, 0, 0.70710678f }, LEFT[3] = { -2, 0, 0 };
    float n[3], z[3] = { 0, 0, 1 };
    vr_rotate(Y90, z, n);
    CHECK(NEAR(n[0], 1) && NEAR(n[1], 0) && NEAR(n[2], 0));   /* it faces +X: the viewer at the origin is on that side */
    const float AT[3] = { -1, 0, 0 };
    CHECK(vr_ray_panel(O, AT, LEFT, Y90, 2.4f, 1.35f, &u, &v, &d));
    CHECK(NEAR(u, 0.5f) && NEAR(v, 0.5f) && NEAR(d, 2));

    /* a panel put in front of a head that looks to the right (-X is not it: yaw -90 about Y turns -Z toward +X) */
    const float HEAD[3] = { 0, 1.6f, 0 }, HEADR[4] = { 0, -0.70710678f, 0, 0.70710678f };
    float p[3], q[4];
    vr_panel_in_front(HEAD, HEADR, 2, 0.1f, p, q);
    CHECK(NEAR(p[0], 2) && NEAR(p[1], 1.5f) && NEAR(p[2], 0));
    float hit[3] = { 1, 0, 0 };   /* from the head along its look direction */
    CHECK(vr_ray_panel(HEAD, hit, p, q, 2.4f, 1.35f, &u, &v, &d));
    CHECK(d > 1.9f && d < 2.1f);

    /* looking straight ahead: in front, level, facing back */
    vr_panel_in_front(HEAD, ID, 2, 0, p, q);
    CHECK(NEAR(p[0], 0) && NEAR(p[1], 1.6f) && NEAR(p[2], -2));
    CHECK(NEAR(q[1], 0) && NEAR(fabsf(q[3]), 1));

    /* angles: straight ahead is yaw 0, pitch 0; to the left is positive; up is positive */
    float yw, pt;
    const float FWD[3] = { 0, 0, -1 }, LF[3] = { -1, 0, 0 }, UP[3] = { 0, 1, 0 };
    vr_dir_angles(FWD, &yw, &pt); CHECK(NEAR(yw, 0) && NEAR(pt, 0));
    vr_dir_angles(LF, &yw, &pt); CHECK(NEAR(yw, 1.5707963f) && NEAR(pt, 0));
    vr_dir_angles(UP, &yw, &pt); CHECK(NEAR(pt, 1.5707963f));

    /* a panel from angles matches the yaw-only one, and a pitched one lies over the head */
    float p2[3], q2[4];
    vr_panel_from_angles(HEAD, 0, 0, 2, 0, p2, q2);
    CHECK(NEAR(p2[0], 0) && NEAR(p2[1], 1.6f) && NEAR(p2[2], -2) && NEAR(fabsf(q2[3]), 1));
    vr_panel_from_angles(HEAD, 1.5707963f, 0, 2, 0.1f, p2, q2);   /* to the left */
    vr_panel_in_front(HEAD, Y90, 2, 0.1f, p, q);
    CHECK(NEAR(p2[0], p[0]) && NEAR(p2[1], p[1]) && NEAR(p2[2], p[2]));
    vr_panel_from_angles(HEAD, 0, 1.5707963f, 2, 0, p2, q2);   /* straight overhead, facing down */
    CHECK(NEAR(p2[0], 0) && NEAR(p2[1], 3.6f) && NEAR(p2[2], 0));
    float nrm[3]; vr_rotate(q2, z, nrm);
    CHECK(NEAR(nrm[1], -1));
    vr_panel_from_angles(HEAD, 0, 0.5f, 2, 0, p2, q2);   /* up a little: its normal comes back down toward the head */
    vr_rotate(q2, z, nrm);
    { float tx = HEAD[0] - p2[0], ty = HEAD[1] - p2[1], tz = HEAD[2] - p2[2], tl = sqrtf(tx * tx + ty * ty + tz * tz);
      CHECK(NEAR(nrm[0], tx / tl) && NEAR(nrm[1], ty / tl) && NEAR(nrm[2], tz / tl)); }

    /* a curved screen 2.2 m away, 2.8 m wide: straight ahead is the middle; the edge ray hits its edge */
    const float RAD = 2.2f, CPOS[3] = { 0, 0, -2.2f };
    float ang = 2.8f / RAD, ht = 1.575f;
    CHECK(vr_ray_cylinder(O, FWD, CPOS, ID, RAD, ang, ht, &u, &v, &d));
    CHECK(NEAR(u, 0.5f) && NEAR(v, 0.5f) && NEAR(d, 2.2f));
    float edge = ang / 2 - 0.01f, ed[3] = { sinf(edge), 0, -cosf(edge) };
    CHECK(vr_ray_cylinder(O, ed, CPOS, ID, RAD, ang, ht, &u, &v, &d));
    CHECK(u > 0.98f && u < 1.0f && NEAR(d, 2.2f));   /* the wall is the same distance everywhere from the middle */
    float past[3] = { sinf(edge + 0.1f), 0, -cosf(edge + 0.1f) };
    CHECK(!vr_ray_cylinder(O, past, CPOS, ID, RAD, ang, ht, &u, &v, &d));
    CHECK(!vr_ray_cylinder(O, BACK, CPOS, ID, RAD, ang, ht, &u, &v, &d));
    /* a hand off to the side reaches the same wall */
    const float HD[3] = { 0.3f, -0.3f, -0.3f };
    float tc[3] = { -0.3f, 0.3f, -1.9f }, tcl = sqrtf(0.09f + 0.09f + 3.61f), dc2[3] = { tc[0] / tcl, tc[1] / tcl, tc[2] / tcl };
    CHECK(vr_ray_cylinder(HD, dc2, CPOS, ID, RAD, ang, ht, &u, &v, &d));
    CHECK(u > 0.4f && u < 0.6f);

    /* off the panel the plane's coordinates still come: a metre to the right of the edge of a 2.4 m panel is u = 1.4167 */
    { float to3[3] = { 1.2f + 1.0f, 0, -2 }, l3 = sqrtf(to3[0] * to3[0] + 4), d3[3] = { to3[0] / l3, 0, -2 / l3 };
      CHECK(!vr_ray_panel(O, d3, POS, ID, 2.4f, 1.35f, &u, &v, &d));
      CHECK(vr_ray_plane_uv(O, d3, POS, ID, 2.4f, 1.35f, &u, &v, &d));
      CHECK(NEAR(u, 0.5f + 2.2f / 2.4f) && NEAR(v, 0.5f)); }
    CHECK(!vr_ray_plane_uv(O, BACK, POS, ID, 2.4f, 1.35f, &u, &v, &d));

    if (failures) { fprintf(stderr, "%d failure(s)\n", failures); return 1; }
    printf("vr math: ok\n");
    return 0;
}
