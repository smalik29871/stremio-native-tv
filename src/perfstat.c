/* See perfstat.h. Histograms in whole milliseconds, so a percentile is a short walk
 * and nothing is allocated while frames run. */
#include "perfstat.h"
#include <SDL.h>
#include <stdio.h>
#include <string.h>

enum { BINS = 101, MAX_MARKS = 12, MAX_SCREENS = 16 };   /* bin 100 holds everything >= 100 ms */

typedef struct { unsigned n, bins[BINS]; } Hist;

static struct { char name[24]; Uint32 at; } g_marks[MAX_MARKS];
static int g_n_marks;
static struct { char name[16]; Hist h, total; } g_screens[MAX_SCREENS];
static int g_n_screens;
static Hist g_keys;
static Uint32 g_key_at;   /* 0: no key waiting to be shown */

static void hist_add(Hist *h, float ms)
{
    int b = ms < 0 ? 0 : ms >= BINS - 1 ? BINS - 1 : (int)(ms + 0.5f);
    h->bins[b]++;
    h->n++;
}

static int hist_pct(const Hist *h, float p)
{
    unsigned want = (unsigned)(h->n * p + 0.5f), seen = 0;
    if (want < 1) want = 1;
    for (int b = 0; b < BINS; b++) if ((seen += h->bins[b]) >= want) return b;
    return BINS - 1;
}

void perf_mark(const char *name)
{
    for (int i = 0; i < g_n_marks; i++) if (!strcmp(g_marks[i].name, name)) return;
    if (g_n_marks == MAX_MARKS) return;
    snprintf(g_marks[g_n_marks].name, sizeof g_marks[0].name, "%s", name);
    g_marks[g_n_marks++].at = SDL_GetTicks();
}

void perf_frame(const char *screen, float work_ms, float total_ms)
{
    if (!screen) screen = "?";
    int i = 0;
    while (i < g_n_screens && strcmp(g_screens[i].name, screen)) i++;
    if (i == g_n_screens) {
        if (g_n_screens == MAX_SCREENS) return;
        snprintf(g_screens[i].name, sizeof g_screens[0].name, "%s", screen);
        g_n_screens++;
    }
    hist_add(&g_screens[i].h, work_ms);
    hist_add(&g_screens[i].total, total_ms);
}

void perf_key(void) { if (!g_key_at) g_key_at = SDL_GetTicks(); }

void perf_shown(void)
{
    if (!g_key_at) return;
    hist_add(&g_keys, (float)(SDL_GetTicks() - g_key_at));
    g_key_at = 0;
}

void perf_report(char *out, size_t n)
{
    size_t o = 0;
    out[0] = 0;
#define ADD(...) do { if (o < n) o += (size_t)snprintf(out + o, n - o, __VA_ARGS__); } while (0)
    ADD("Startup:");
    for (int i = 0; i < g_n_marks; i++) ADD("%s %s %.1f s", i ? "," : "", g_marks[i].name, g_marks[i].at / 1000.0);
    if (!g_n_marks) ADD(" not measured");
    ADD("\n");
    if (g_keys.n) ADD("Key to screen: typical %d ms, slow (1 in 20) %d ms, %u keys\n", hist_pct(&g_keys, 0.5f), hist_pct(&g_keys, 0.95f), g_keys.n);
    else ADD("Key to screen: no keys yet\n");
    ADD("Per frame: app work typical / 1 in 20, whole frame typical / 1 in 20, frames:\n");
    for (int i = 0; i < g_n_screens; i++) {
        const Hist *h = &g_screens[i].h, *t = &g_screens[i].total;
        ADD("  %-12s %3d / %3d ms   %3d / %3d ms   %u\n", g_screens[i].name, hist_pct(h, 0.5f), hist_pct(h, 0.95f), hist_pct(t, 0.5f), hist_pct(t, 0.95f), h->n);
    }
#undef ADD
}
