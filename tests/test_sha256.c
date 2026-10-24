#include "sha256.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
#define CHECK(c) do { if (!(c)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

static int is(const char *data, size_t n, const char *want)
{
    char hex[65];
    sha256_hex(data, n, hex);
    if (strcmp(hex, want)) fprintf(stderr, "  got %s\n want %s\n", hex, want);
    return !strcmp(hex, want);
}

int main(void)
{
    /* FIPS 180-2 examples, and the lengths either side of a block's padding */
    CHECK(is("", 0, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
    CHECK(is("abc", 3, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
    const char *two = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    CHECK(is(two, strlen(two), "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"));
    char *a = malloc(1000000);
    memset(a, 'a', 1000000);
    CHECK(is(a, 1000000, "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"));
    CHECK(is(a, 55, "9f4390f8d30c2dd92ec9f095b65e2b9ae9b0a925a5258e241c9f1e910f734318"));
    CHECK(is(a, 56, "b35439a4ac6f0948b6d6f9e3c6af0f5f590ce20f1bde7090ef7970686ec6738a"));
    CHECK(is(a, 64, "ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668eb"));
    free(a);
    if (failures) { fprintf(stderr, "%d failure(s)\n", failures); return 1; }
    printf("sha256: ok\n");
    return 0;
}
