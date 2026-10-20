/* Trailers that play by themselves behind the artwork, as Netflix's and Prime's title
 * pages do: after the focus rests on a title, its IMDb trailer starts muted where the
 * backdrop is, the artwork fades away over it, and comes back when it ends. */
#ifndef SNTV_BGTRAILER_H
#define SNTV_BGTRAILER_H

/* IMDb's own trailer for a title ("tt..."): its files, sharpest first. `url`s live only
 * during the callback; n is 0 when IMDb has none or did not answer. */
typedef struct ImdbTrailer { const char *url; int rank; } ImdbTrailer;
typedef void (*ImdbTrailerFn)(const ImdbTrailer *t, int n, long status, void *user);
unsigned imdb_trailer_fetch(const char *id, ImdbTrailerFn cb, void *user);
int imdb_trailer_res(int rank);            /* 1080, 720, 480 or 0 */
const char *imdb_trailer_label(int rank);  /* "1080p", "adaptive"... */

/* From the screen's update, every frame its backdrop shows `id`: the trailer starts
 * after `dwell` seconds and covers the top `height` of the screen (UI pixels). It stops
 * the frame nobody asks for it. */
void bgtrailer_want(const char *id, float height, float dwell);
/* 0..1: how far the trailer has taken the place of `id`'s artwork. */
float bgtrailer_shown(const char *id);
void bgtrailer_frame(float dt);   /* main loop, after the screen's update */
/* Play/Pause while a trailer shows: its sound on or off (1 when it took the key). */
int  bgtrailer_key_sound(void);
/* The sound badge under the clock, while `id`'s trailer shows; with_key names Play/Pause as its switch. */
void bgtrailer_draw_badge(const char *id, int with_key);

#endif
