/* SHA-256, to check a downloaded update against the hash its release lists. */
#ifndef SNTV_SHA256_H
#define SNTV_SHA256_H
#include <stddef.h>

/* The digest of data as 64 lower-case hex digits and a NUL. */
void sha256_hex(const void *data, size_t n, char out[65]);
#endif
