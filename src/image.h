/* Async artwork loader: HTTP on the pool, decode + downscale on a dedicated
 * thread, texture upload on the main thread, LRU eviction by byte budget. */
#ifndef SNTV_IMAGE_H
#define SNTV_IMAGE_H

int  image_init(void);
void image_shutdown(void);
/* Returns a texture id, or 0 while loading/failed. Requests the image on first call. */
unsigned image_get(const char *url, int *w, int *h);
int  image_failed(const char *url);
void image_pump(void);          /* call once per frame on the main thread */
void image_frame_tick(void);    /* advances the LRU clock */

#endif
