#include "sysinfo.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/utsname.h>

static int read_first(const char *path, char *buf, size_t n)
{
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    size_t got = fread(buf, 1, n - 1, f);
    fclose(f);
    buf[got] = 0;
    return got > 0;
}

/* `"key": "9.2.0"` or `KEY="9.2.0"` -> 9.2.0 */
static int value_after(const char *text, const char *key, char *out, size_t n)
{
    const char *p = strstr(text, key);
    if (!p) return 0;
    p += strlen(key);
    while (*p == '"' || *p == ':' || *p == '=' || *p == ' ') p++;
    size_t k = 0;
    while (p[k] && p[k] != '"' && p[k] != '\n' && k + 1 < n) { out[k] = p[k]; k++; }
    out[k] = 0;
    return k > 0;
}

int sysinfo_webos_release(char *out, size_t n, const char **source)
{
    char buf[4096], ver[64];
    *source = "";
    if (read_first("/etc/starfish-release", buf, sizeof buf)) {
        buf[strcspn(buf, "\r\n")] = 0;
        snprintf(out, n, "%s", buf);
        *source = "/etc/starfish-release";
        const char *r = strstr(buf, "Release ");
        return r ? atoi(r + 8) : 0;
    }
    if (read_first("/var/run/nyx/os_info.json", buf, sizeof buf) && value_after(buf, "\"webos_release\"", ver, sizeof ver)) {
        snprintf(out, n, "webOS TV Release %s", ver);
        *source = "/var/run/nyx/os_info.json";
        return atoi(ver);
    }
    if ((read_first("/etc/os-release", buf, sizeof buf) || read_first("/usr/lib/os-release", buf, sizeof buf))
        && strstr(buf, "webOS") && value_after(buf, "VERSION_ID", ver, sizeof ver)) {
        snprintf(out, n, "webOS TV Release %s", ver);
        *source = "os-release";
        return atoi(ver);
    }
    struct utsname u;
    if (uname(&u) == 0) snprintf(out, n, "%s %s %s", u.sysname, u.release, u.machine);
    else out[0] = 0;
    return 0;
}
