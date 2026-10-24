/* Depth maps of backdrops, made ahead of time (tools/depth) and served beside the app's
 * source: the GPU reads them to push slowly into a still (Home, the Depth screensaver). */
#ifndef SNTV_DEPTH_H
#define SNTV_DEPTH_H
#include <stddef.h>

typedef struct {
    float fg;      /* the depth that parts the people in front from the rest; < 0: none clear */
    float clock_x; /* where a clock's centre sits across the screen (0..1), behind them a little; < 0 none */
} DepthInfo;

/* What is known of the image at `url`: NULL while the list loads, or when it has no map. */
const DepthInfo *depth_info(const char *url);
/* The list of maps has come (depth_info's NULL then means: none for that image). */
int depth_ready(void);
/* Its map as a texture once loaded (and the list says there is one); 0 before. */
unsigned depth_texture(const char *url);
/* The name a map goes by: the TMDB file, metahub's IMDb id, else a hash of the address. */
int depth_key(const char *url, char *key, size_t n);
#endif
