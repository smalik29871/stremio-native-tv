/* YouTube links played without a home helper. The TV asks YouTube for the video the
 * way YouTube's own TV app does, solves the player's URL challenges with yt-dlp's
 * solver (assets/yt, run in the bundled QuickJS), and downloads the audio, or a small
 * video, once into the app's sounds folder. Breaks then play that file, offline too. */
#ifndef SNTV_YOUTUBE_H
#define SNTV_YOUTUBE_H
#include <stddef.h>

/* The 11-character video id of a youtube.com / youtu.be / music / shorts link, else 0. */
int youtube_video_id(const char *url, char id[16]);
int youtube_id_valid(const char *id);   /* exactly a video id (11 of A-Z a-z 0-9 - _): what may go into a launch or a player */

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
/* youtube_stream's answer as URLs the player opens, best first (DASH up to 4K, then the
 * playlist, then a file with sound). Returns how many. Defined with the TV's own server. */
enum { YT_PLAY_URLS = 4 };   /* the most ways youtube_player_urls offers */
int youtube_player_urls(const char *answer, const char *id, char out[][4608], int max);
/* The player's verdict on one of those URLs: a way that never starts here goes later. */
void youtube_note_result(const char *url, int played);

/* What this TV takes: the resolution cap, AV1, 4K (from Settings and what it learned). */
void youtube_set_quality(int res_cap, int av1_ok, int uhd_ok);
int  youtube_quality_cap(void);   /* the tallest picture this TV takes (1080 without 4K) */
/* The User-Agent of the YouTube app a googlevideo link was issued to. */
const char *youtube_ua_for(const char *url);

/* What the player opens for a youtube_stream() URL: a playlist as it is, a whole file
 * through the TV's own server in YouTube's 10 MB range pieces (as Nuvio reads it). */
void youtube_player_url(const char *stream_url, char *out, size_t n);

/* The Diagnostics test: each way to get a video, asked for one video, and what came of
 * it. ok: -1 not known yet, 0 refused, 1 works, 2 not needed. */
enum { YT_TEST_STEPS = 6 };   /* visionOS client, its files, TV client, its files, playlist, file */
typedef struct { int ok[YT_TEST_STEPS]; char what[YT_TEST_STEPS][200]; char best[96]; } YoutubeTest;
void youtube_test(const char *id, YoutubeCallback cb, void *user);
const YoutubeTest *youtube_test_report(void);

/* The solver alone: yt-dlp's jsc() input as JSON in, its output JSON out (malloc'ed),
 * or NULL with the reason in err. Blocking; runs QuickJS on a thread with a big stack. */
char *youtube_solve(const char *input_json, char *err, size_t n);

#endif
