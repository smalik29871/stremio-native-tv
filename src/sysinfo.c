#include "sysinfo.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "platform/compat.h"
#ifndef _WIN32
#include <sys/utsname.h>
#endif

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
#ifdef _WIN32   /* the real version: GetVersionEx is told an old one without a manifest */
    typedef LONG (WINAPI *RtlGetVersion_t)(OSVERSIONINFOW *);
    OSVERSIONINFOW v = { sizeof v };
    RtlGetVersion_t get = (RtlGetVersion_t)(intptr_t)GetProcAddress(GetModuleHandleA("ntdll.dll"), "RtlGetVersion");
    if (get && get(&v) == 0) snprintf(out, n, "Windows %lu.%lu build %lu", v.dwMajorVersion, v.dwMinorVersion, v.dwBuildNumber);
    else snprintf(out, n, "Windows");
#else
    struct utsname u;
    if (uname(&u) == 0) snprintf(out, n, "%s %s %s", u.sysname, u.release, u.machine);
    else out[0] = 0;
#endif
    return 0;
}

static char g_decoders[200];
static volatile int g_decoders_ready;

/* dlopen("libx.so.N") for the first N in the list that loads (-1 ends it, then the unversioned name). */
static void *open_any(const char *base, const int *majors)
{
    char name[64];
    for (int i = 0;; i++) {
        if (majors[i] >= 0) snprintf(name, sizeof name, "%s.so.%d", base, majors[i]); else snprintf(name, sizeof name, "%s.so", base);
        void *h = dlopen(name, RTLD_LAZY | RTLD_LOCAL);
        if (h || majors[i] < 0) return h;
    }
}

static void append(const char *text)
{
    size_t len = strlen(g_decoders);
    snprintf(g_decoders + len, sizeof g_decoders - len, "%s%s", len ? ", " : "", text);
}

void sysinfo_probe_decoders(void)
{
    static const int AVCODEC[] = { 62, 61, 60, 59, 58, 57, 56, 55, 54, -1 }, OPENH264[] = { 8, 7, 6, 5, 4, -1 }, ZERO[] = { 0, -1 };
    char text[96];
#ifdef SNTV_DESKTOP_PLAYER
    (void)AVCODEC; (void)OPENH264; (void)ZERO; (void)text; (void)open_any;   /* the desktop decodes with its own FFmpeg, not the system's */
    append("FFmpeg (built in)");
    g_decoders_ready = 1;
    return;
#endif
    void *h = open_any("libavcodec", AVCODEC);
    if (h) {
        unsigned (*version)(void) = (unsigned (*)(void))dlsym(h, "avcodec_version");
        void (*register_all)(void) = (void (*)(void))dlsym(h, "avcodec_register_all");
        const void *(*find)(const char *) = (const void *(*)(const char *))dlsym(h, "avcodec_find_decoder_by_name");
        if (register_all) register_all();   /* FFmpeg before 4.0 lists nothing without it */
        unsigned v = version ? version() : 0;
        snprintf(text, sizeof text, "libavcodec %u.%u (%s, %s)", v >> 16, (v >> 8) & 0xFF,
                 find && find("h264") ? "h264" : "no h264", find && find("hevc") ? "hevc" : "no hevc");
        append(text);
        dlclose(h);
    }
    if ((h = open_any("libde265", ZERO))) { append("libde265 (hevc)"); dlclose(h); }
    if ((h = open_any("libopenh264", OPENH264))) { append("OpenH264"); dlclose(h); }
    if ((h = open_any("libgstreamer-1.0", ZERO))) { append("GStreamer"); dlclose(h); }
    if (!g_decoders[0]) append("none found");
    g_decoders_ready = 1;
}

const char *sysinfo_decoders(void) { return g_decoders_ready ? g_decoders : NULL; }
