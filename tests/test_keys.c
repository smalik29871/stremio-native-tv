/* Built-in keys: what CMake scrambles comes back exactly, the plain text is not among the stored
 * bytes, and a key this build lacks reads as empty. */
#include "builtin_keys.h"
#include <stdio.h>
#include <string.h>

#define TEST_KEY "test-key-0123456789abcdef"

int main(void)
{
    int fails = 0;
    if (strcmp(builtin_key(BK_TMDB), TEST_KEY) != 0) { printf("FAIL: unscrambled key differs\n"); fails++; }
    if (strcmp(builtin_key(BK_TMDB), TEST_KEY) != 0) { printf("FAIL: second read differs\n"); fails++; }
    if (sizeof sntv_key_TMDB_BUILTIN_KEY != strlen(TEST_KEY)) { printf("FAIL: stored length\n"); fails++; }
    for (size_t i = 0; i + 4 <= sizeof sntv_key_TMDB_BUILTIN_KEY; i++)   /* no run of the plain text survives */
        if (!memcmp(sntv_key_TMDB_BUILTIN_KEY + i, TEST_KEY + i, 4)) { printf("FAIL: plain bytes at %zu\n", i); fails++; break; }
    if (!builtin_key_present(BK_TMDB) || builtin_key_present(BK_MDBLIST)) { printf("FAIL: presence\n"); fails++; }
    if (strcmp(builtin_key(BK_MDBLIST), "") != 0) { printf("FAIL: an absent key is not empty\n"); fails++; }
    if (!fails) printf("builtin keys: ok\n");
    return fails != 0;
}
