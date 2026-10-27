/* The Movie Database, when the viewer adds their own key (Settings > Accounts):
 * cast photos and age ratings for a title, looked up by IMDb id. Everything is
 * cached on disk; nothing is asked without a key. */
#ifndef SNTV_TMDB_H
#define SNTV_TMDB_H
#include <stddef.h>

typedef struct TmdbCast { char name[64], character[64], photo[320]; int person_id; } TmdbCast;
typedef struct TmdbInfo {
    int state;              /* 0 idle, 1 loading, 2 ready, 3 nothing found / failed */
    char cert[16];          /* "PG-13", "TV-MA", "15", ... or "" */
    TmdbCast cast[12]; int n_cast;
    int tmdb_id;
    int collection_id;      /* the franchise the film belongs to, 0 none */
    char trailer_yt[16];    /* a YouTube trailer TMDB lists (official first), "" none */
    char tagline[160];      /* the line it is sold on ("A signal from the deep."), "" none */
    char collection[96];
    int stinger;            /* a scene during (1) or after (2) the credits, both 3; films, from TMDB's keywords */
} TmdbInfo;

/* A franchise's films in release order (TMDB collections). */
typedef struct TmdbPart { int tmdb_id; char title[96], year[8], poster[320]; } TmdbPart;
typedef struct TmdbCollection { int id, state; char name[96]; TmdbPart parts[16]; int n; } TmdbCollection;

int  tmdb_enabled(void);
int  tmdb_builtin(void);       /* a key was compiled in (CI secret) */
/* The viewer's key (Settings > Accounts), replacing the one in use; NULL or "" removes it. */
void tmdb_key_set(const char *key);
/* The key in use copied out, for network threads (0: none). */
int  tmdb_key_copy(char *buf, size_t n);
const char *tmdb_api_base(void);
/* NULL without a key or while nothing is known yet (a lookup starts); the app is
 * redrawn when it arrives. `type` is "movie" or "series". */
const TmdbInfo *tmdb_get(const char *imdb_id, const char *type);
/* NULL while loading (a fetch starts) or failed; the app is redrawn when it arrives. */
const TmdbCollection *tmdb_collection(int id);
/* An episode's guest stars (who is in this one beyond the regular cast); NULL while
 * loading or when there are none (*n 0). */
const TmdbCast *tmdb_episode_guests(const char *imdb_id, int season, int episode, int *n);
/* The IMDb id of a TMDB film, by callback ("" when unknown). */
typedef void (*TmdbImdbCb)(const char *imdb_id, void *user);
void tmdb_imdb_for(int tmdb_id, TmdbImdbCb cb, void *user);
/* IMDb ids for n TMDB titles (tv[i] 1 for a series), on a network thread: ids found this
 * session at once, the rest in one request through the key relay, or several at a time from
 * TMDB without it; out[i] "" when a title has none or none could be found. `what` names the
 * list in the log line with the time it took. */
enum { TMDB_IMDB_LEN = 16 };
void tmdb_imdb_ids(const int *ids, const int *tv, int n, char (*out)[TMDB_IMDB_LEN], const char *what);
/* A person's films and series, most popular first, with IMDb ids so they open like any
 * title (person_id 0: looked up by name). `cb` gets them on the main thread (n -1: no key
 * or not found). The array is the callee's; copy what is kept. */
#include "addon/addon.h"
typedef void (*TmdbTitlesCb)(const MetaPreview *titles, int n, void *user);
void tmdb_person_titles(const char *name, int person_id, TmdbTitlesCb cb, void *user);
/* Titles TMDB lists for filters (an AI search's: "Bollywood comedies of the 90s"), most
 * popular first, with IMDb ids; same callback contract. Genres by TMDB's English names. */
typedef struct TmdbFilter { int tv; char lang[8], country[8]; char genres[4][24]; int n_genres; int from, to; } TmdbFilter;
void tmdb_discover(const TmdbFilter *f, TmdbTitlesCb cb, void *user);
#endif
