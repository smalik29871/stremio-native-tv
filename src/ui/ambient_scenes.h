/* Screensaver scenes drawn from a handful of shapes (no video, little GPU), so they
 * run smoothly on old TVs; everything in them keeps moving, which also keeps OLED
 * panels from holding an image. Each draws into a rectangle as a function of time,
 * so the Settings preview shows the same scene, smaller. */
#ifndef SNTV_AMBIENT_SCENES_H
#define SNTV_AMBIENT_SCENES_H

/* Northern lights over a starry sky and a line of pines. */
void ambient_aurora(float x, float y, float w, float h, float t);
/* Flying through a starfield. */
void ambient_warp(float x, float y, float w, float h, float t);
/* A rainy window: the picture (tex, iw x ih; 0 for none) blurred behind the glass, and
 * the drops on it showing it upside down, sharp, as real ones do. */
void ambient_rain(unsigned tex, int iw, int ih, float x, float y, float w, float h, float t, float alpha);
/* A cinema's velvet curtain and a poster frame lit by chasing bulbs; the poster's
 * place comes back in px..ph for the caller to fill. */
void ambient_marquee(float x, float y, float w, float h, float t, float *px, float *py, float *pw, float *ph);

#endif
