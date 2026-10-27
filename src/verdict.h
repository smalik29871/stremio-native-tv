/* Shared play verdicts: which files play on which kind of TV. A TV that played a file through
 * (or could not decode its picture or sound) tells the key relay so, under a fingerprint of
 * the file and the kind of TV ("webos9", "android-shield-android-tv"); the sources list asks
 * for the files it shows, and a file that keeps failing on this kind of TV goes down the list
 * before anyone here has to try it. The fingerprint is a hash of what names the file (its
 * hash, its torrent and file, or its file name and size): no link, file name or account goes.
 * Network trouble and an addon's error clip say nothing about the file and are never told. */
#ifndef SNTV_VERDICT_H
#define SNTV_VERDICT_H
#include <stdint.h>

#define VERDICT_KEY 17   /* 16 hex digits and a NUL */

/* The kind of TV this is, set once at start ("" or NULL: this device takes no part). */
void verdict_set_device(const char *device);
const char *verdict_device(void);
/* The fingerprint of a file from what names it; 0 (and "") when nothing does. */
int verdict_key(const char *video_hash, const char *info_hash, int file_idx, const char *filename, uint64_t size, char out[VERDICT_KEY]);
/* Ask the relay about these files (the ones not asked yet), in one request. */
void verdict_want(const char (*keys)[VERDICT_KEY], int n);
/* The score a file gets from what other TVs of this kind saw: below 0 when it keeps failing,
 * a little above when it played, 0 when nothing is known. */
long verdict_adjust(const char *key);
/* Asked and not answered yet, for at most 1.5 s: a start can wait that long for them. */
int verdict_waiting(void);
/* Changes each time answers come, so a list can sort itself again. */
unsigned verdict_generation(void);
/* This TV played the file through (1), or could not decode it (0): told once a session. */
void verdict_note(const char *key, int worked);

#endif
