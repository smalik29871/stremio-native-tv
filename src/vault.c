#include "vault.h"
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#include <wincrypt.h>
#endif

#define SEALED_PREFIX "sealed1:"
#define FAKE_PREFIX "fake1:"

int vault_is_sealed(const char *t) { return t && (!strncmp(t, SEALED_PREFIX, strlen(SEALED_PREFIX)) || !strncmp(t, FAKE_PREFIX, strlen(FAKE_PREFIX))); }

int vault_is_secret_key(const char *name)
{
    static const char *const TAIL[] = { "Key", "Token", "Secret" };
    size_t n = name ? strlen(name) : 0;
    for (int i = 0; i < 3; i++) {
        size_t t = strlen(TAIL[i]);
        if (n > t && !strcmp(name + n - t, TAIL[i])) return 1;
    }
    return 0;
}

static char *hex(const unsigned char *d, size_t n, const char *prefix)
{
    static const char H[] = "0123456789abcdef";
    size_t p = strlen(prefix);
    char *out = malloc(p + n * 2 + 1);
    if (!out) return NULL;
    memcpy(out, prefix, p);
    for (size_t i = 0; i < n; i++) { out[p + i * 2] = H[d[i] >> 4]; out[p + i * 2 + 1] = H[d[i] & 15]; }
    out[p + n * 2] = 0;
    return out;
}

static unsigned char *unhex(const char *s, size_t *n)
{
    size_t len = strlen(s);
    if (len % 2) return NULL;
    unsigned char *out = malloc(len / 2 + 1);
    if (!out) return NULL;
    for (size_t i = 0; i < len / 2; i++) {
        int v[2];
        for (int k = 0; k < 2; k++) {
            char c = s[i * 2 + k];
            v[k] = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
            if (v[k] < 0) { free(out); return NULL; }
        }
        out[i] = (unsigned char)(v[0] << 4 | v[1]);
    }
    *n = len / 2;
    return out;
}

#ifdef _WIN32
int vault_available(void) { return 1; }

char *vault_seal(const char *plain)
{
    DATA_BLOB in = { (DWORD)strlen(plain), (BYTE *)plain }, out = { 0, NULL };
    if (!CryptProtectData(&in, L"Cuelight", NULL, NULL, NULL, 0, &out)) return NULL;
    char *r = hex(out.pbData, out.cbData, SEALED_PREFIX);
    LocalFree(out.pbData);
    return r;
}

char *vault_open(const char *sealed)
{
    if (!sealed || strncmp(sealed, SEALED_PREFIX, strlen(SEALED_PREFIX))) return NULL;
    size_t n = 0;
    unsigned char *raw = unhex(sealed + strlen(SEALED_PREFIX), &n);
    if (!raw) return NULL;
    DATA_BLOB in = { (DWORD)n, raw }, out = { 0, NULL };
    char *r = NULL;
    if (CryptUnprotectData(&in, NULL, NULL, NULL, NULL, 0, &out)) {
        r = malloc(out.cbData + 1);
        if (r) { memcpy(r, out.pbData, out.cbData); r[out.cbData] = 0; }
        LocalFree(out.pbData);
    }
    free(raw);
    return r;
}
#else
int vault_available(void) { const char *e = getenv("SNTV_FAKE_VAULT"); return e && *e == '1'; }

/* the stand-in for tests: every byte xor 0x5A, as hex (never something to rely on) */
char *vault_seal(const char *plain)
{
    if (!vault_available()) return NULL;
    size_t n = strlen(plain);
    unsigned char *x = malloc(n + 1);
    if (!x) return NULL;
    for (size_t i = 0; i < n; i++) x[i] = (unsigned char)plain[i] ^ 0x5A;
    char *r = hex(x, n, FAKE_PREFIX);
    free(x);
    return r;
}

char *vault_open(const char *sealed)
{
    if (!vault_available() || !sealed || strncmp(sealed, FAKE_PREFIX, strlen(FAKE_PREFIX))) return NULL;
    size_t n = 0;
    unsigned char *raw = unhex(sealed + strlen(FAKE_PREFIX), &n);
    if (!raw) return NULL;
    for (size_t i = 0; i < n; i++) raw[i] ^= 0x5A;
    raw[n] = 0;
    return (char *)raw;
}
#endif
