/* Memory watch: reads what the TV has left and turns the memory saver on by itself
 * when it gets tight, before the OS kills the app. Hysteresis keeps it from flapping:
 * two tight samples (10 s) turn it on, two minutes of room turn it off again (unless
 * the TV is small to begin with, or the viewer chose Low). SNTV_FAKE_MEM="total,avail"
 * in kB simulates a TV for tests. */
#include "memwatch.h"
#include "app.h"
#include "image.h"
#include "log.h"
#include <stdio.h>
#ifdef __GLIBC__
#include <malloc.h>
#endif
#include <stdlib.h>
#include <string.h>

static float g_since_sample = 99, g_room_for;
enum { WHY_ROOMY, WHY_SMALL_TV, WHY_TIGHT, WHY_CHOSEN };
static int g_tight_runs, g_toasted, g_reason;
static MemSample g_last;

int mem_sample(MemSample *out)
{
    memset(out, 0, sizeof *out);
    const char *fake = getenv("SNTV_FAKE_MEM");
    if (fake && *fake) { sscanf(fake, "%ld,%ld", &out->total_kb, &out->avail_kb); out->rss_kb = 60 * 1024; return out->total_kb > 0; }
    FILE *f = fopen("/proc/meminfo", "r");
    if (!f) return 0;
    char line[160]; long free_kb = 0, cached_kb = 0, buffers_kb = 0;
    while (fgets(line, sizeof line, f)) {
        if (sscanf(line, "MemTotal: %ld kB", &out->total_kb) == 1) continue;
        if (sscanf(line, "MemAvailable: %ld kB", &out->avail_kb) == 1) continue;
        if (sscanf(line, "MemFree: %ld kB", &free_kb) == 1) continue;
        if (sscanf(line, "Cached: %ld kB", &cached_kb) == 1) continue;
        if (sscanf(line, "Buffers: %ld kB", &buffers_kb) == 1) continue;
    }
    fclose(f);
    if (!out->avail_kb) out->avail_kb = free_kb + cached_kb + buffers_kb;   /* old kernels (webOS 3) have no MemAvailable */
    f = fopen("/proc/self/statm", "r");
    if (f) { long pages = 0, rss = 0; if (fscanf(f, "%ld %ld", &pages, &rss) == 2) out->rss_kb = rss * 4; fclose(f); }
    return out->total_kb > 0;
}

int mem_small_tv(void)
{
    MemSample m;
    return mem_sample(&m) && m.total_kb < 1200 * 1024;   /* 1 GB sets; 2 GB ones report 1.6-1.9 GB */
}

size_t mem_normal_budget(void)
{
    MemSample m;
    if (!mem_sample(&m)) return 0;
    size_t mb = (size_t)m.total_kb / 1024;
    if (mb < 1024) return 40u << 20;
    if (mb < 2048) return 96u << 20;
    return 192u << 20;
}

/* Freed memory goes back to the system; glibc otherwise keeps it for the app. */
static void give_back(void)
{
#ifdef __GLIBC__
    malloc_trim(0);
#endif
}

static int g_playing;   /* a video fills the screen: the artwork cache shrinks for the decoder */

static size_t budget(int low, int reason)
{
    size_t b = low ? (reason == WHY_TIGHT ? 32u << 20 : 48u << 20) : mem_normal_budget();
    if (g_playing && b > (24u << 20)) b = 24u << 20;
    return b;
}

static void apply(int low, int reason)
{
    if (g_app.low_memory == low && g_reason == reason) return;
    g_app.low_memory = low; g_reason = reason;
    image_set_budget(budget(low, reason));
    image_set_max_dim(low ? 960 : 0);   /* posters at half size, still sharper than their cards */
    if (low) give_back();
    LOGI("memory saver %s (%s): %ld MB total, %ld MB available, app %ld MB", low ? "on" : "off", mem_reason_text(),
         g_last.total_kb / 1024, g_last.avail_kb / 1024, g_last.rss_kb / 1024);
    app_redraw();
}

void mem_apply_mode(void)
{
    mem_sample(&g_last);
    if (g_app.memory_mode == 1) apply(1, WHY_CHOSEN);
    else if (g_app.memory_mode == 2) apply(0, WHY_ROOMY);
    else if (mem_small_tv()) apply(1, WHY_SMALL_TV);
    else apply(0, WHY_ROOMY);
    g_tight_runs = 0; g_room_for = 0;
}

void mem_tick(float dt)
{
    g_since_sample += dt;
    if (g_since_sample < 5) return;
    g_since_sample = 0;
    if (!mem_sample(&g_last)) return;
    /* a reading in the log when it has moved: if the TV ends the app, the next start shows the last */
    static long logged_rss = -1, logged_avail = -1;
    if (logged_rss < 0 || labs(g_last.rss_kb - logged_rss) >= 32 * 1024 || labs(g_last.avail_kb - logged_avail) >= 128 * 1024) {
        logged_rss = g_last.rss_kb; logged_avail = g_last.avail_kb;
        LOGI("memory: app %ld MB, %ld MB free of %ld MB", g_last.rss_kb / 1024, g_last.avail_kb / 1024, g_last.total_kb / 1024);
    }
    int level = mem_level(g_last.total_kb, g_last.avail_kb, g_last.rss_kb);
    if (level == 2) {
        if (++g_tight_runs >= 2 && g_app.memory_mode != 2 && g_reason != WHY_TIGHT && g_reason != WHY_CHOSEN) {
            apply(1, WHY_TIGHT);
            if (!g_toasted++) app_toast("Memory is tight: keeping fewer rows and posters");
        } else if (g_reason == WHY_TIGHT && g_tight_runs >= 6) {
            image_set_budget(16u << 20);   /* still tight after half a minute: artwork down to the minimum */
        }
        g_room_for = 0;
    } else {
        g_tight_runs = 0;
        if (level == 0) g_room_for += 5; else g_room_for = 0;
        if (g_reason == WHY_TIGHT && g_room_for >= 120) apply(mem_small_tv(), mem_small_tv() ? WHY_SMALL_TV : WHY_ROOMY);
    }
}

int mem_level_now(void) { return mem_level(g_last.total_kb, g_last.avail_kb, g_last.rss_kb); }
int mem_saver_lasting(void) { return g_app.low_memory && (g_reason == WHY_SMALL_TV || g_reason == WHY_CHOSEN); }

void mem_playback(int playing)
{
    /* only where room is short: elsewhere the posters stay for the way back */
    playing = playing && (g_app.low_memory || mem_level(g_last.total_kb, g_last.avail_kb, g_last.rss_kb) >= 1);
    if (playing == g_playing) return;
    g_playing = playing;
    image_set_budget(budget(g_app.low_memory, g_reason));
    if (playing) give_back();
    LOGI("memory: %s", playing ? "artwork trimmed while the video plays" : "artwork budget back after the video");
}

void mem_emergency(void)
{
    LOGW("low-memory warning from the system");
    image_set_budget(16u << 20);
    give_back();
    if (g_app.memory_mode != 2 && g_reason != WHY_CHOSEN) { apply(1, WHY_TIGHT); g_tight_runs = 6; if (!g_toasted++) app_toast("Memory is tight: keeping fewer rows and posters"); }
}

const char *mem_reason_text(void)
{
    switch (g_reason) {
    case WHY_SMALL_TV: return "small TV";
    case WHY_TIGHT: return "memory got tight";
    case WHY_CHOSEN: return "chosen";
    default: return "plenty of room";
    }
}

int mem_describe(char *buf, size_t n)
{
    MemSample m;
    if (!mem_sample(&m)) { snprintf(buf, n, "memory unknown"); return 0; }
    if (m.total_kb >= 1024 * 1024) snprintf(buf, n, "%.1f GB in the TV  \xC2\xB7  %ld MB free  \xC2\xB7  app %ld MB", m.total_kb / 1048576.0, m.avail_kb / 1024, m.rss_kb / 1024);
    else snprintf(buf, n, "%ld MB in the TV  \xC2\xB7  %ld MB free  \xC2\xB7  app %ld MB", m.total_kb / 1024, m.avail_kb / 1024, m.rss_kb / 1024);
    return 1;
}
