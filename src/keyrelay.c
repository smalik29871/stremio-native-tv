/* See keyrelay.h. */
#include "keyrelay.h"
#include "builtin_keys.h"
#include "log.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static const char *const PATH[KR_COUNT] = { "mdblist", "opensubtitles", "tmdb", "glow", "verdicts" };
static const char *const NAME[KR_COUNT] = { "MDBList", "OpenSubtitles", "TMDB", "the letterbox's colours", "the play verdicts" };
static time_t g_down_until[KR_COUNT];

static const char *relay(void)
{
    const char *e = getenv("SNTV_KEY_RELAY");
    if (e) return e;
#ifdef SNTV_KEY_RELAY_URL
    return SNTV_KEY_RELAY_URL;
#else
    return "";
#endif
}

const char *keyrelay_base(KeyRelaySvc s)
{
    static char base[KR_COUNT][300];
    const char *r = relay();
    if (!r[0] || time(NULL) < g_down_until[s]) return NULL;
    if (!base[s][0]) snprintf(base[s], sizeof base[s], "%s/%s", r, PATH[s]);
    return base[s];
}

int keyrelay_failed(KeyRelaySvc s, long status)
{
    if (status != 0 && status != 429 && status != 502 && status != 503 && status != 504) return 0;
    g_down_until[s] = time(NULL) + 600;
    LOGW("keyrelay: %s through the relay failed (%ld); going direct for ten minutes", NAME[s], status);
    return 1;
}
