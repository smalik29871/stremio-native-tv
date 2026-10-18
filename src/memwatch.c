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
#include <stdlib.h>
#include <string.h>

static float g_since_sample = 99, g_room_for;
static int g_tight_runs, g_toasted, g_reason;   /* reason: 0 normal, 1 small TV, 2 tight at runtime, 3 chosen */
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
    return mem_sample(&m) && m.total_kb < 2000 * 1024;
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

static void apply(int low, int reason)
{
    if (g_app.low_memory == low && g_reason == reason) return;
    g_app.low_memory = low; g_reason = reason;
    image_set_budget(low ? (reason == 2 ? 32u << 20 : 48u << 20) : mem_normal_budget());
    LOGI("memory saver %s (%s): %ld MB total, %ld MB available, app %ld MB", low ? "on" : "off", mem_reason_text(),
         g_last.total_kb / 1024, g_last.avail_kb / 1024, g_last.rss_kb / 1024);
    app_redraw();
}

void mem_apply_mode(void)
{
    mem_sample(&g_last);
    if (g_app.memory_mode == 1) apply(1, 3);
    else if (g_app.memory_mode == 2) apply(0, 0);
    else if (mem_small_tv()) apply(1, 1);
    else apply(0, 0);
    g_tight_runs = 0; g_room_for = 0;
}

void mem_tick(float dt)
{
    g_since_sample += dt;
    if (g_since_sample < 5) return;
    g_since_sample = 0;
    if (!mem_sample(&g_last)) return;
    int level = mem_level(g_last.total_kb, g_last.avail_kb, g_last.rss_kb);
    if (level == 2) {
        if (++g_tight_runs >= 2 && g_app.memory_mode != 2 && g_reason != 2 && g_reason != 3) {
            apply(1, 2);
            if (!g_toasted++) app_toast("Memory is tight: keeping fewer rows and posters");
        } else if (g_reason == 2 && g_tight_runs >= 6) {
            image_set_budget(16u << 20);   /* still tight after half a minute: artwork down to the minimum */
        }
        g_room_for = 0;
    } else {
        g_tight_runs = 0;
        if (level == 0) g_room_for += 5; else g_room_for = 0;
        if (g_reason == 2 && g_room_for >= 120) apply(mem_small_tv(), mem_small_tv() ? 1 : 0);
    }
}

void mem_emergency(void)
{
    LOGW("low-memory warning from the system");
    image_set_budget(16u << 20);
    if (g_app.memory_mode != 2 && g_reason != 3) { apply(1, 2); g_tight_runs = 6; if (!g_toasted++) app_toast("Memory is tight: keeping fewer rows and posters"); }
}

const char *mem_reason_text(void)
{
    switch (g_reason) {
    case 1: return "small TV";
    case 2: return "memory got tight";
    case 3: return "chosen";
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
