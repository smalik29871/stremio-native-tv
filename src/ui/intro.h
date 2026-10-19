#ifndef SNTV_INTRO_H
#define SNTV_INTRO_H

/* The launch: the Stremio mark grows in at the centre, then flies to its place at the top
 * of the rail as the first screen shows through. About a second; any key skips it. Off
 * with Light effects or without screen transitions. */
void intro_start(void);
int  intro_active(void);
void intro_draw(void);   /* over everything, after the screens */
void intro_skip(void);

#endif
