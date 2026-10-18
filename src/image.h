/* Async artwork loader: disk cache -> HTTP on the pool -> decode + downscale on a
 * dedicated thread -> texture upload on the main thread, LRU eviction by bytes. */
#ifndef SNTV_IMAGE_H
#define SNTV_IMAGE_H
#include <stddef.h>

typedef enum { IMAGE_PLAIN = 0, IMAGE_MIPMAP = 1 } ImageFlags;

int  image_init(const char *cache_dir, size_t budget_bytes);
void image_set_budget(size_t budget_bytes);   /* evicts down to it at once */
void image_shutdown(void);
/* Returns a texture id, or 0 while loading/failed. Requests the image on first call.
 * `age` (optional) receives frames since the texture became ready, for fade-ins. */
unsigned image_get(const char *url, int *w, int *h);
unsigned image_get_ex(const char *url, ImageFlags flags, int *w, int *h, unsigned *age);
void image_prefetch(const char *url);   /* start loading without needing it this frame */
int  image_failed(const char *url);
/* Average colour of a loaded image (saturated a little), 0 if not loaded yet.
 * Used to tint the ambient glow behind the hero, like Apple TV does. */
unsigned image_tint(const char *url);
void image_pump(void);          /* call once per frame on the main thread */
void image_frame_tick(void);    /* advances the LRU clock */
int  image_busy(void);          /* decodes or uploads pending (keeps the loop awake) */
size_t image_cache_bytes(void);

#endif
