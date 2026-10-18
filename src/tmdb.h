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
} TmdbInfo;

int  tmdb_enabled(void);
/* NULL without a key or while nothing is known yet (a lookup starts); the app is
 * redrawn when it arrives. `type` is "movie" or "series". */
const TmdbInfo *tmdb_get(const char *imdb_id, const char *type);
#endif
