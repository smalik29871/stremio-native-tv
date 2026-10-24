/* Seek previews decoded on the TV, with no helper: the video's keyframe index is read
 * from the file (Matroska Cues, MP4 sample tables), only the keyframe at or before the
 * wanted time is downloaded (one range request, ~50-500 KB), decoded with the bundled
 * FFmpeg (H.264, HEVC 8/10-bit) and scaled down to a small picture.
 * Not thread-safe: one caller at a time (phone.c runs it on its frame thread). */
#ifndef SNTV_FRAMES_H
#define SNTV_FRAMES_H
#include <stddef.h>
#include <stdint.h>

/* Bytes [off, off+len) of the video into *buf (malloc'ed, *got bytes, possibly fewer at
 * the end of the file); *total is the file size when known, else 0. 1 on success. */
typedef int (*FramesRead)(void *ctx, uint64_t off, uint64_t len, uint8_t **buf, size_t *got, uint64_t *total);

int frames_supported(void);   /* built with FFmpeg */

/* The keyframe at or before `t` seconds as a 24-bit BMP at most `max_w` pixels wide
 * (malloc'ed, *len bytes), or NULL with the reason in err. `key` names the video for
 * the index cache (the last few videos keep theirs). */
uint8_t *frames_picture(FramesRead rd, void *ctx, const char *key, double t, int max_w, size_t *len, char *err, size_t en);

/* The same over HTTP(S) range requests. */
uint8_t *frames_picture_url(const char *url, double t, int max_w, size_t *len, char *err, size_t en);
void frames_stop(void);   /* reads under way give up: the app is closing */

#endif
