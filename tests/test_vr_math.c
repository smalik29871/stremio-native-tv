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

    if (failures) { fprintf(stderr, "%d failure(s)\n", failures); return 1; }
    printf("vr math: ok\n");
    return 0;
}
