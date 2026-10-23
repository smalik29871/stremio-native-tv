/* OpenSubtitles.com, built in: searched by IMDb id (and the episode) plus the file's
 * hash, with release names, so the player can tell which subtitle was made for the
 * same release. Needs an API key (built in from a CI secret, or Settings > Accounts);
 * a free account raises the daily download allowance. SNTV_OPENSUBS_API overrides
 * the base URL (tests). */
#ifndef SNTV_OPENSUBS_H
#define SNTV_OPENSUBS_H
#include <stddef.h>

typedef struct OsSub {
    long file_id;
    char lang[8];           /* canonical 3-letter code */
    char release[160];
    int hash_match;         /* made for this exact file */
    int downloads;
} OsSub;

int  opensubs_enabled(void);
int  opensubs_signed_in(void);
/* Settings: the account (username, password), returns 1 on success; NULL signs out. */
int  opensubs_login(const char *user, const char *password, char *err, size_t en);
const char *opensubs_user(void);

/* Search; `cb` gets the results (owned by the call) on the main thread. imdb is "tt…",
 * season/episode -1 for films, hash "" when unknown, langs "en,pt-br". */
typedef void (*OsSearchCb)(const OsSub *subs, int n, void *user);
void opensubs_search(const char *imdb, int season, int episode, const char *hash, const char *langs, OsSearchCb cb, void *user);

/* The download link for a file (one of the day's downloads), or NULL and a reason. */
typedef void (*OsLinkCb)(const char *url, const char *error, void *user);
void opensubs_link(long file_id, OsLinkCb cb, void *user);

/* How closely a release name matches the video's file name: 0 none .. 100 the same
 * release (group, source, resolution, edition). */
int  opensubs_name_score(const char *release, const char *filename);
#endif
