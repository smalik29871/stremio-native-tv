#include "report.h"
#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(c) do { if (!(c)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

static const char *red(const char *in) { static char out[600]; report_redact(in, out, sizeof out); return out; }

int main(void)
{
    /* a query goes, the address stays */
    CHECK(!strcmp(red("12:00:01 I http: GET https://api.example.com/v1/x?api_key=abcdef123456&q=1 200"), "12:00:01 I http: GET https://api.example.com/v1/x?[removed] 200"));
    /* an addon address that carries a user's key in its path */
    CHECK(!strstr(red("addon: https://torrentio.strem.fun/realdebrid=ABCDEFGHIJKLMNOPQRSTUV/manifest.json"), "ABCDEFGHIJKL"));
    CHECK(strstr(red("addon: https://torrentio.strem.fun/realdebrid=ABCDEFGHIJKLMNOPQRSTUV/manifest.json"), "manifest.json") != NULL);
    /* an e-mail address, and a long token */
    CHECK(!strstr(red("sync: signed in as someone.name@example.org ok"), "someone"));
    CHECK(strstr(red("sync: signed in as someone.name@example.org ok"), "signed in as [removed] ok") != NULL);
    CHECK(!strstr(red("auth: key 0123456789abcdef0123456789abcdef0123 set"), "0123456789abcdef"));
    /* ordinary lines stay as they are: times, versions, names, short ids, addresses without queries */
    const char *plain = "12:00:01 I player: candidate 2/5 Example @4s -> http://192.168.1.4:11470/video/x.mkv (1080p)";
    CHECK(!strcmp(red(plain), plain));
    CHECK(!strcmp(red("12:00:02 W tt0111161:1:4 has 7 new episodes"), "12:00:02 W tt0111161:1:4 has 7 new episodes"));
    if (failures) { fprintf(stderr, "%d failure(s)\n", failures); return 1; }
    printf("report: ok\n");
    return 0;
}
