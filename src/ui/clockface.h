/* Clock faces for the Clock screensaver (and their Settings previews): the styles
 * people keep on bedside and TV clocks. */
#ifndef SNTV_CLOCKFACE_H
#define SNTV_CLOCKFACE_H
#include <time.h>

enum { CLOCKFACE_DIGITAL, CLOCKFACE_FLIP, CLOCKFACE_ANALOG, CLOCKFACE_WORDS, CLOCKFACE_RINGS,
       CLOCKFACE_NIXIE, CLOCKFACE_STACKED, CLOCKFACE_SEGMENT, CLOCKFACE_DOTS, CLOCKFACE_NEON,
       CLOCKFACE_ROLL, CLOCKFACE_ORBIT, CLOCKFACE_DAYLIGHT,
       CLOCKFACE_STATION, CLOCKFACE_LIQUID, CLOCKFACE_WEATHER, CLOCKFACE_AURORA,
       CLOCKFACE_PLANETS, CLOCKFACE_PRAYER, CLOCKFACE_HORIZON, CLOCKFACE_ORRERY, CLOCKFACE_COUNT };   /* stored by number: add at the end */
extern const char *const CLOCKFACE_NAMES[CLOCKFACE_COUNT];

/* Centred on (cx, cy); k = 1 fills the 1920x1080 canvas, smaller for a preview.
 * sec is the seconds with their fraction, for hands that sweep. */
void clockface_draw(int face, float cx, float cy, float k, const struct tm *t, double sec);
#endif
