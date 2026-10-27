/* Service keys built into the app by CI (see CMakeLists.txt). They are stored scrambled, so a
 * look through the package's strings does not show them; each is unscrambled in memory the
 * first time it is asked for. The SNTV_<NAME> macros exist only for the keys this build has. */
#ifndef SNTV_BUILTIN_KEYS_H
#define SNTV_BUILTIN_KEYS_H

typedef enum { BK_TMDB, BK_TRAKT_ID, BK_TRAKT_SECRET, BK_SIMKL_ID, BK_MDBLIST, BK_OPENSUBTITLES, BK_COUNT } BuiltinKey;
const char *builtin_key(BuiltinKey k);   /* "" when this build has none */
int builtin_key_present(BuiltinKey k);

#include "builtin_keys_data.h"
#ifdef SNTV_HAVE_TMDB_BUILTIN_KEY
#define SNTV_TMDB_BUILTIN_KEY builtin_key(BK_TMDB)
#endif
#ifdef SNTV_HAVE_TRAKT_CLIENT_ID
#define SNTV_TRAKT_CLIENT_ID builtin_key(BK_TRAKT_ID)
#endif
#ifdef SNTV_HAVE_TRAKT_CLIENT_SECRET
#define SNTV_TRAKT_CLIENT_SECRET builtin_key(BK_TRAKT_SECRET)
#endif
#ifdef SNTV_HAVE_SIMKL_CLIENT_ID
#define SNTV_SIMKL_CLIENT_ID builtin_key(BK_SIMKL_ID)
#endif
#ifdef SNTV_HAVE_MDBLIST_KEY
#define SNTV_MDBLIST_KEY builtin_key(BK_MDBLIST)
#endif
#ifdef SNTV_HAVE_OPENSUBTITLES_KEY
#define SNTV_OPENSUBTITLES_KEY builtin_key(BK_OPENSUBTITLES)
#endif
#endif
