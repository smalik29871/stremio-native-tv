/* Watch tracking services, each optional:
 *   - Trakt: signed in with a device code (trakt.tv/activate); playback is scrobbled
 *     (start, pause, and stop at 80% marks it watched) and the watchlist and
 *     recommendations become Home rows.
 *   - SimKL: signed in with a PIN (simkl.com/pin); a title watched past 80% is added
 *     to the history.
 *   - MDBList: an API key; the title page shows Rotten Tomatoes, Metacritic,
 *     Letterboxd and TMDB scores beside IMDb's.
 * The app ids come from the build (CI secrets) or Settings > API keys. Base URLs can be
 * overridden with SNTV_TRAKT_API, SNTV_SIMKL_API and SNTV_MDBLIST_API. */
#ifndef SNTV_TRACKING_H
#define SNTV_TRACKING_H
#include "addon/addon.h"

enum { TRACK_TRAKT, TRACK_SIMKL, TRACK_SERVICES };
extern const char *const TRACK_NAMES[TRACK_SERVICES];

int  tracking_configured(int svc);     /* the app id is known */
int  tracking_connected(int svc);
/* Starts signing in. The sheet shows `code` to type at `url`; tracking_tick() polls. */
int  tracking_connect(int svc, char *code, size_t cn, char *url, size_t un);
void tracking_disconnect(int svc);
const char *tracking_last_error(void);   /* why the last sign-in did not start */
int  tracking_pending(int svc);        /* a sign-in is waiting for the code */
/* Settings > API keys: the app's id (and Trakt's secret), "" keeps the built-in one. */
void tracking_set_app(int svc, const char *id, const char *secret);
const char *tracking_app_id(int svc);

void tracking_tick(void);              /* once a frame: sign-in polls, row refreshes */
void tracking_pull_playback(void);     /* Trakt's resume points into the library (newer ones) */

/* The player's progress; `final` when it pauses, stops or moves on. */
void tracking_progress(const MetaPreview *m, const char *video_id, double pos, double dur, int final);

/* Home rows: 0 Trakt watchlist, 1 Trakt recommendations (*out malloc'd). */
enum { TROW_WATCHLIST, TROW_RECOMMENDED, TROW_COUNT };
extern const char *const TROW_NAMES[TROW_COUNT];
int  tracking_row(int which, MetaPreview **out);
int  tracking_version(void);           /* grows when a row changes */

/* MDBList: "Tomatometer 93%  ·  Popcornmeter 88%  ·  Metacritic 80  ·  Letterboxd 4.1" for an IMDb id, NULL while unknown. */
/* Trakt's numbers for a title (any app id will do, no sign-in): 1 with the people
 * watching it now, all who watched, and the plays; 0 while unknown (the first call asks). */
int  trakt_stats(const char *imdb_id, const char *type, long *watching, long *watchers, long *plays);
/* 1,204 / 23K / 1.4M */
void tracking_count_text(long v, char *out, size_t n);
int  mdblist_enabled(void);
const char *mdblist_line(const char *imdb_id, const char *type);
#endif
