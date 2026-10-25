/* Async artwork loader: disk cache -> HTTP on the pool -> decode + downscale on a
 * dedicated thread -> texture upload on the main thread, LRU eviction by bytes. */
#ifndef SNTV_IMAGE_H
#define SNTV_IMAGE_H
#include <stddef.h>

typedef enum { IMAGE_PLAIN = 0, IMAGE_MIPMAP = 1, IMAGE_FULL = 2 } ImageFlags;   /* FULL: up to 1920 px, for artwork filling the screen */

int  image_init(const char *cache_dir, size_t budget_bytes);
/* A cache folder used before (deleted on the loader's thread). Call before image_init. */
void image_drop_old_cache(const char *dir);
void image_set_budget(size_t budget_bytes);   /* evicts down to it at once */
/* The largest side a decoded picture keeps (0: the platform's); the memory saver lowers it. */
void image_set_max_dim(int px);
int image_memory_low(void);   /* artwork is being decoded smaller to save memory */
void image_shutdown(void);
/* Returns a texture id, or 0 while loading/failed. Requests the image on first call.
 * `age` (optional) receives frames since the texture became ready, for fade-ins. */
unsigned image_get(const char *url, int *w, int *h);
unsigned image_get_ex(const char *url, ImageFlags flags, int *w, int *h, unsigned *age);
void image_prefetch(const char *url);   /* start loading without needing it this frame */
int  image_failed(const char *url);
unsigned image_peek(const char *url, int *w, int *h);   /* the texture if already loaded; never starts a request */
unsigned image_peek_ex(const char *url, ImageFlags flags, int *w, int *h);
int  image_pending(const char *url);   /* being fetched or decoded now */
/* Average colour of a loaded image (saturated a little), 0 if not loaded yet.
 * Used to tint the ambient glow behind the hero, like Apple TV does. */
unsigned image_tint(const char *url);
/* Where the picture sits inside black bars baked into a loaded image (x0, y0, x1, y1 as
 * fractions; 0 0 1 1 when it has none). 0 while it is not loaded. */
int image_content_box(const char *url, float box[4]);
void image_pump(void);          /* call once per frame on the main thread */
void image_frame_tick(void);    /* advances the LRU clock */
int  image_busy(void);          /* decodes or uploads pending (keeps the loop awake) */
size_t image_cache_bytes(void);
void image_forget_prefix(const char *prefix);   /* loaded textures from addresses starting so, freed now */

#endif
