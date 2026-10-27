#include "log.h"
#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(c) do { if (!(c)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

static void check(const char *message, const char *expected)
{
    LogLine line;
    LOGI("%s", message);
    CHECK(log_snapshot(&line, 1) == 1);
    CHECK(!strcmp(line.text + 9, expected));
}

int main(void)
{
    check("opening https://user:private@media.example/private/key/movie.mkv?signature=secret#fragment", "opening https://media.example*");
    check("{\"uri\":\"https://media.example/secret\"}", "{\"uri\":\"https://media.example*\"}");
    check("http://[::1]:1234/private https://other.example?auth=secret", "http://[::1]:1234* https://other.example*");
    check("token=a token=ab token=abc token=long-value&ok=1", "token=* token=* token=* token=*&ok=1");
    check("network error: timed out (code 300)", "network error: timed out (code 300)");
    check("https://media.example", "https://media.example");
    if (failures) return 1;
    puts("diagnostic redaction checks passed");
    return 0;
}
