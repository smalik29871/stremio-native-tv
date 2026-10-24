#include "depth.h"
#include "app.h"
#include "http.h"
#include "image.h"
#include "log.h"
#include "memwatch.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DEPTH_BASE "https://raw.githubusercontent.com/smalik29871/stremio-native-tv/depth-maps"   /* this repository's depth-maps branch */
#define RETRY_S 600

typedef struct { unsigned long long h; DepthInfo info; } Entry;
static Entry *g_map; static size_t g_cap, g_n;
static int g_state;          /* 0 not asked, 1 asking, 2 have the list, 3 failed */
static double g_failed_at;
static int g_off;            /* 0 on, 1 off for memory, 2 off for storage */
static double g_room_since;  /* room again since then (it waits a minute before coming back) */
#define ROOM_S 60

static unsigned long long fnv64(const char *s)
{
    unsigned long long h = 1469598103934665603ull;
    for (; *s; s++) h = (h ^ (unsigned char)*s) * 1099511628211ull;
    return h;
}

static const char *base(void)
{
    const char *e = getenv("SNTV_DEPTH_URL");
    return e && *e ? e : DEPTH_BASE;
}

int depth_key(const char *url, char *key, size_t n)
{
    if (!url || !*url) return 0;
    const char *p;
    if ((p = strstr(url, "image.tmdb.org/t/p/"))) {   /* .../t/p/<size>/<file>.jpg: the file, whatever the size */
        const char *f = strrchr(p, '/'), *dot = f ? strrchr(f, '.') : NULL;
        if (f && dot && dot > f + 1) { snprintf(key, n, "t%.*s", (int)(dot - f - 1), f + 1); return 1; }
    }
    if ((p = strstr(url, "images.metahub.space/background/"))) {   /* .../background/<size>/<tt id>/img */
        const char *tt = strstr(p, "/tt");
        if (tt) { size_t l = strcspn(tt + 1, "/"); snprintf(key, n, "m%.*s", (int)l, tt + 1); return 1; }
    }
    snprintf(key, n, "h%016llx", fnv64(url));
    return 1;
}

static void put(const char *key, float fg, float clock_x)
{
    if (g_n * 2 >= g_cap) {
        size_t cap = g_cap ? g_cap * 2 : 1024;
        Entry *m = calloc(cap, sizeof *m);
        if (!m) return;
        for (size_t i = 0; i < g_cap; i++)
            if (g_map[i].h) { size_t j = g_map[i].h & (cap - 1); while (m[j].h) j = (j + 1) & (cap - 1); m[j] = g_map[i]; }
        free(g_map); g_map = m; g_cap = cap;
    }
    unsigned long long h = fnv64(key) | 1;
    size_t j = h & (g_cap - 1);
    while (g_map[j].h && g_map[j].h != h) j = (j + 1) & (g_cap - 1);
    if (!g_map[j].h) g_n++;
    g_map[j].h = h; g_map[j].info.fg = fg; g_map[j].info.clock_x = clock_x;
}

static const DepthInfo *find(const char *key)
{
    if (!g_cap) return NULL;
    unsigned long long h = fnv64(key) | 1;
    for (size_t j = h & (g_cap - 1); g_map[j].h; j = (j + 1) & (g_cap - 1))
        if (g_map[j].h == h) return &g_map[j].info;
    return NULL;
}

/* Depth wants room: memory short (the saver is on, or the TV is getting low) or the TV's
 * drive nearly full turns it off at once, the list and maps let go; it comes back by
 * itself after a minute with room again. Backdrops just stay still meanwhile. */
static void watch_room(void)
{
    int why = g_app.low_memory || mem_level_now() >= 1 ? 1 : app_storage_low() ? 2 : 0;
    if (why) {
        g_room_since = 0;
        if (g_off == why) return;
        if (!g_off) {
            free(g_map); g_map = NULL; g_cap = g_n = 0;
            if (g_state == 2 || g_state == 3) g_state = 0;   /* asked again when it is back */
            image_forget_prefix(base());
            app_redraw();
        }
        LOGI("depth: off while %s is low; its list and maps let go", why == 1 ? "memory" : "storage");
        g_off = why;
        return;
    }
    if (!g_off) return;
    if (!g_room_since) { g_room_since = g_app.time > 0 ? g_app.time : 0.001; return; }
    if (g_app.time - g_room_since < ROOM_S) return;
    LOGI("depth: back on, there is room again");
    g_off = 0;
}

int depth_off(void) { watch_room(); return g_off; }

static void on_index(HttpResponse *r, void *user)
{
    (void)user;
    if (g_off) { g_state = 0; return; }   /* turned off while it came */
    if (r->status != 200 || !r->body || strncmp(r->body, "# cuelight depth maps", 21) != 0) {
        g_state = 3; g_failed_at = g_app.time;
        LOGI("depth: no list of depth maps (%ld%s%s)", r->status, r->error[0] ? ", " : "", r->error);   /* backdrops just stay still: not a problem to show */
        return;
    }
    for (char *line = r->body, *next; line && *line; line = next) {
        next = strchr(line, '\n');
        if (next) *next++ = 0;
        if (*line == '#') continue;
        char key[96], fg[16], clock[16];
        if (sscanf(line, "%95s %15s %15s", key, fg, clock) != 3) continue;
        float cx = clock[0] == '-' ? -1 : (float)atof(clock);
        put(key, fg[0] == '-' ? -1 : (float)atof(fg), cx > 0.1f && cx < 0.9f ? cx : -1);
    }
    g_state = 2;
    LOGI("depth: %zu backdrops have depth maps", g_n);
    app_redraw();
}

const DepthInfo *depth_info(const char *url)
{
    watch_room();
    if (g_off) return NULL;
    if (g_state == 3 && g_app.time - g_failed_at > RETRY_S) g_state = 0;
    if (g_state == 0) {
        char u[600];
        snprintf(u, sizeof u, "%s/index.txt", base());
        g_state = 1;
        HttpOptions o = { .background = 1, .no_json_accept = 1 };
        http_request_async_ex(u, &o, on_index, NULL);
    }
    char key[96];
    if (g_state != 2 || !depth_key(url, key, sizeof key)) return NULL;
    return find(key);
}

int depth_ready(void) { depth_info(NULL); return g_off || g_state == 2; }   /* off: known now that none will come */

unsigned depth_texture(const char *url)
{
    char key[96], u[700];
    if (!depth_info(url) || !depth_key(url, key, sizeof key)) return 0;
    snprintf(u, sizeof u, "%s/%s.png", base(), key);
    int w, h;
    return image_get(u, &w, &h);
}
