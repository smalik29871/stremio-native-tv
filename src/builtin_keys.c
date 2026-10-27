/* See builtin_keys.h. */
#include "builtin_keys.h"
#include <SDL.h>
#include <string.h>

typedef struct { const unsigned char *bytes; size_t n; int salt; } Scrambled;

static Scrambled scrambled(BuiltinKey k)
{
    Scrambled s = { NULL, 0, 0 };
    switch (k) {
#ifdef SNTV_HAVE_TMDB_BUILTIN_KEY
    case BK_TMDB: s.bytes = sntv_key_TMDB_BUILTIN_KEY; s.n = sizeof sntv_key_TMDB_BUILTIN_KEY; s.salt = sntv_salt_TMDB_BUILTIN_KEY; break;
#endif
#ifdef SNTV_HAVE_TRAKT_CLIENT_ID
    case BK_TRAKT_ID: s.bytes = sntv_key_TRAKT_CLIENT_ID; s.n = sizeof sntv_key_TRAKT_CLIENT_ID; s.salt = sntv_salt_TRAKT_CLIENT_ID; break;
#endif
#ifdef SNTV_HAVE_TRAKT_CLIENT_SECRET
    case BK_TRAKT_SECRET: s.bytes = sntv_key_TRAKT_CLIENT_SECRET; s.n = sizeof sntv_key_TRAKT_CLIENT_SECRET; s.salt = sntv_salt_TRAKT_CLIENT_SECRET; break;
#endif
#ifdef SNTV_HAVE_SIMKL_CLIENT_ID
    case BK_SIMKL_ID: s.bytes = sntv_key_SIMKL_CLIENT_ID; s.n = sizeof sntv_key_SIMKL_CLIENT_ID; s.salt = sntv_salt_SIMKL_CLIENT_ID; break;
#endif
#ifdef SNTV_HAVE_MDBLIST_KEY
    case BK_MDBLIST: s.bytes = sntv_key_MDBLIST_KEY; s.n = sizeof sntv_key_MDBLIST_KEY; s.salt = sntv_salt_MDBLIST_KEY; break;
#endif
#ifdef SNTV_HAVE_OPENSUBTITLES_KEY
    case BK_OPENSUBTITLES: s.bytes = sntv_key_OPENSUBTITLES_KEY; s.n = sizeof sntv_key_OPENSUBTITLES_KEY; s.salt = sntv_salt_OPENSUBTITLES_KEY; break;
#endif
    default: break;
    }
    return s;
}

int builtin_key_present(BuiltinKey k) { return scrambled(k).n > 0; }

const char *builtin_key(BuiltinKey k)
{
    static char plain[BK_COUNT][160];
    static int done[BK_COUNT];
    static SDL_SpinLock lock;   /* the network threads ask too */
    if ((int)k < 0 || k >= BK_COUNT) return "";
    SDL_AtomicLock(&lock);
    if (!done[k]) {
        Scrambled s = scrambled(k);
        size_t n = s.n < sizeof plain[k] - 1 ? s.n : sizeof plain[k] - 1;
        for (size_t i = 0; i < n; i++) plain[k][i] = (char)(s.bytes[i] ^ (unsigned char)((s.salt + i * 29 + 0xA5) % 256));
        plain[k][n] = 0;
        done[k] = 1;
    }
    SDL_AtomicUnlock(&lock);
    return plain[k];
}
