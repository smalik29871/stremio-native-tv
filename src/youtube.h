/* YouTube links played without a home helper. The TV asks YouTube for the video the
 * way YouTube's own TV app does, solves the player's URL challenges with yt-dlp's
 * solver (assets/yt, run in the bundled QuickJS), and downloads the audio, or a small
 * video, once into the app's sounds folder. Breaks then play that file, offline too. */
#ifndef SNTV_YOUTUBE_H
#define SNTV_YOUTUBE_H
#include <stddef.h>

/* The 11-character video id of a youtube.com / youtu.be / music / shorts link, else 0. */
int youtube_video_id(const char *url, char id[16]);

/* A saved copy of the link: its name in the sounds folder ("yt-ID-audio.m4a"), the
 * wanted kind first, then the other one. 0 when nothing was saved. */
int youtube_saved(const char *url, int want_video, char *name, size_t n, int *is_video);

/* Download it in the background. `cb` runs on the main thread with the file's name, or
 * NULL and a reason. `cb` may be NULL. */
typedef void (*YoutubeCallback)(const char *name, const char *error, void *user);
void youtube_save(const char *url, int video, YoutubeCallback cb, void *user);

/* A URL the player can open now (an HLS playlist or an MP4 with sound), asked the
 * way Nuvio asks; `cb` gets it as `name`, or NULL and a reason. Nothing is saved. */
void youtube_stream(const char *url, YoutubeCallback cb, void *user);

/* The solver alone: yt-dlp's jsc() input as JSON in, its output JSON out (malloc'ed),
 * or NULL with the reason in err. Blocking; runs QuickJS on a thread with a big stack. */
char *youtube_solve(const char *input_json, char *err, size_t n);

#endif
