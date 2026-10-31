/* A link typed without its scheme ("example.com/manifest.json"): https is added. On this network (an address, a
 * port, localhost or .local) the other end is plain http. The settings boxes start empty, so nothing is there to
 * delete first and the scheme is the app's to add. Pure: tests/test_link_scheme.c. */
#ifndef SNTV_LINK_SCHEME_H
#define SNTV_LINK_SCHEME_H
#include <stdio.h>
#include <string.h>

static inline const char *with_scheme(const char *v, char *out, size_t n)
{
    if (!*v || strstr(v, "://")) return v;
    size_t hl = strcspn(v, "/");
    int dotted_digits = hl > 0, dots = 0;
    for (size_t i = 0; i < hl; i++) {
        if (v[i] == '.') dots++;
        else if (!((v[i] >= '0' && v[i] <= '9') || v[i] == ':')) dotted_digits = 0;
    }
    int local = (dotted_digits && dots == 3) || !strncmp(v, "localhost", 9)
             || (hl >= 6 && !strncmp(v + hl - 6, ".local", 6)) || (hl > 6 && strstr(v, ".local:") && strstr(v, ".local:") < v + hl);
    snprintf(out, n, "%s%s", local ? "http://" : "https://", v);
    return out;
}

#endif
