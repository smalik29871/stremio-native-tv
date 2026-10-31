/* Links typed without a scheme (src/link_scheme.h). */
#include "link_scheme.h"
#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(c) do { if (!(c)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)
#define IS(in, want) do { char b[200]; CHECK(!strcmp(with_scheme(in, b, sizeof b), want)); } while (0)

int main(void)
{
    IS("v3-cinemeta.strem.io/manifest.json", "https://v3-cinemeta.strem.io/manifest.json");   /* a web address: https */
    IS("example.com", "https://example.com");
    IS("192.168.1.10:11434/v1", "http://192.168.1.10:11434/v1");                              /* on this network: plain http */
    IS("10.0.0.5:11480", "http://10.0.0.5:11480");
    IS("localhost:11434", "http://localhost:11434");
    IS("nas.local", "http://nas.local");
    IS("nas.local:11480/x", "http://nas.local:11480/x");
    IS("https://already.com/a", "https://already.com/a");                                     /* a scheme typed: untouched */
    IS("http://192.168.1.10:11470", "http://192.168.1.10:11470");
    IS("stremio://v3.example/manifest.json", "stremio://v3.example/manifest.json");
    IS("sntv-sound://abc", "sntv-sound://abc");
    IS("", "");                                                                               /* empty stays empty */
    IS("1.2.3.4.example.com/x", "https://1.2.3.4.example.com/x");                             /* digits and dots in a name are not an address */
    IS("example.com/path.local", "https://example.com/path.local");                           /* .local only counts in the host */
    if (failures) return 1;
    puts("link scheme checks passed");
    return 0;
}
