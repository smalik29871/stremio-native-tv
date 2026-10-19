/* The Movie Database, when the viewer adds their own key (Settings > API keys):
 * cast photos and age ratings for a title, looked up by IMDb id. Everything is
 * cached on disk; nothing is asked without a key. */
#ifndef SNTV_TMDB_H
#define SNTV_TMDB_H

typedef struct TmdbCast { char name[64], character[64], photo[320]; } TmdbCast;
typedef struct TmdbInfo {
    int state;              /* 0 idle, 1 loading, 2 ready, 3 nothing found / failed */
    char cert[16];          /* "PG-13", "TV-MA", "15", ... or "" */
    TmdbCast cast[12]; int n_cast;
    int tmdb_id;
    int collection_id;      /* the franchise the film belongs to, 0 none */
    char collection[96];
} TmdbInfo;

/* A franchise's films in release order (TMDB collections). */
typedef struct TmdbPart { int tmdb_id; char title[96], year[8], poster[320]; } TmdbPart;
typedef struct TmdbCollection { int id, state; char name[96]; TmdbPart parts[16]; int n; } TmdbCollection;

int  tmdb_enabled(void);
int  tmdb_builtin(void);       /* a key was compiled in (CI secret) */
/* NULL without a key or while nothing is known yet (a lookup starts); the app is
 * redrawn when it arrives. `type` is "movie" or "series". */
const TmdbInfo *tmdb_get(const char *imdb_id, const char *type);
/* NULL while loading (a fetch starts) or failed; the app is redrawn when it arrives. */
const TmdbCollection *tmdb_collection(int id);
/* The IMDb id of a TMDB film, by callback ("" when unknown). */
typedef void (*TmdbImdbCb)(const char *imdb_id, void *user);
void tmdb_imdb_for(int tmdb_id, TmdbImdbCb cb, void *user);
#endif
