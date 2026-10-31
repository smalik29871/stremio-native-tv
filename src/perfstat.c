/* See perfstat.h. Histograms in whole milliseconds, so a percentile is a short walk
 * and nothing is allocated while frames run. */
#include "perfstat.h"
#include "log.h"
#include <SDL.h>
#include <stdio.h>
#include <string.h>

enum { BINS = 101, MAX_MARKS = 12, MAX_SCREENS = 16 };   /* bin 100 holds everything >= 100 ms */

typedef struct { unsigned n, bins[BINS]; float max; } Hist;   /* max: the slowest, exactly (the bins stop at 100) */

static struct { char name[24]; Uint32 at; } g_marks[MAX_MARKS];
static int g_n_marks;
static struct { char name[16]; Hist h, total; unsigned late; double fill_sum; float fill_max; } g_screens[MAX_SCREENS];   /* late: frames that missed a refresh */
static int g_n_screens;
static Hist g_keys;
static Hist g_uploads;   /* the frames that sent pictures to the GPU: ms each */
static double g_upload_bytes;
static Uint32 g_key_at;   /* 0: no key waiting to be shown */

static void hist_add(Hist *h, float ms)
{
    int b = ms < 0 ? 0 : ms >= BINS - 1 ? BINS - 1 : (int)(ms + 0.5f);
    h->bins[b]++;
    h->n++;
    if (ms > h->max) h->max = ms;
}

static int hist_pct(const Hist *h, float p)
{
    unsigned want = (unsigned)(h->n * p + 0.5f), seen = 0;
    if (want < 1) want = 1;
    for (int b = 0; b < BINS; b++) if ((seen += h->bins[b]) >= want) return b;
    return BINS - 1;
}

void perf_upload(float ms, size_t bytes)
{
    hist_add(&g_uploads, ms);
    g_upload_bytes += (double)bytes;
}

void perf_mark(const char *name)
{
    for (int i = 0; i < g_n_marks; i++) if (!strcmp(g_marks[i].name, name)) return;
    if (g_n_marks == MAX_MARKS) return;
    snprintf(g_marks[g_n_marks].name, sizeof g_marks[0].name, "%s", name);
    g_marks[g_n_marks++].at = SDL_GetTicks();
}

void perf_frame(const char *screen, float work_ms, float total_ms, float fill)
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
    if (total_ms > 25) g_screens[i].late++;   /* past a second refresh at 60 Hz (one is 16.7 ms; a frame that waited for it lands near 17) */
    g_screens[i].fill_sum += fill;
    if (fill > g_screens[i].fill_max) g_screens[i].fill_max = fill;
}

void perf_key(void) { if (!g_key_at) g_key_at = SDL_GetTicks(); }

float perf_frame_period_ms(void)
{
    unsigned best = 0, n = 0; int at = 0;
    for (int b = 12; b <= 34; b++) {
        unsigned c = 0;
        for (int i = 0; i < g_n_screens; i++) c += g_screens[i].total.bins[b];
        n += c;
        if (c > best) { best = c; at = b; }
    }
    return n >= 60 && at ? (float)at : 16.7f;
}

void perf_shown(void)
{
    if (!g_key_at) return;
    hist_add(&g_keys, (float)(SDL_GetTicks() - g_key_at));
    g_key_at = 0;
}

/* ---- stream starts ---- */
enum { STARTS = 5, PHASES = 6 };
typedef struct { char how[16]; int n, ok; struct { char name[12]; Uint32 ms; } ph[PHASES]; Uint32 total; } Start;
static Start g_starts[STARTS], g_run;
static int g_n_starts, g_running;
static Uint32 g_run_t0, g_run_last;
static struct {
    double played, buffered, position;
    unsigned stalls, dropped, last_dropped, load, seeks, seek_version;
    Uint32 seek_at, seek_total, seek_max;
    double seek_target;   /* a player that doesn't say when a seek is done: done when it plays on past here */
    int active, started, buffering, seeking;
} g_playback;

void perf_playback_begin(void) { memset(&g_playback, 0, sizeof g_playback); g_playback.active = 1; }
void perf_playback_seek(unsigned presented, int supported, double target)
{
    if (!g_playback.active) return;
    g_playback.seeking = supported ? 1 : 2;   /* 2: told by the position playing on from the target */
    g_playback.seek_version = presented; g_playback.seek_at = SDL_GetTicks(); g_playback.seek_target = target;
}

void perf_playback_sample(double position, float dt, int playing, int buffering, unsigned dropped, unsigned load, unsigned presented)
{
    if (!g_playback.active) return;
    if (load != g_playback.load) { g_playback.load = load; g_playback.last_dropped = 0; g_playback.seeking = 0; }
    if (dropped >= g_playback.last_dropped) g_playback.dropped += dropped - g_playback.last_dropped;
    g_playback.last_dropped = dropped;
    if ((!playing && !buffering) || SDL_GetTicks() - g_playback.seek_at > 15000) g_playback.seeking = 0;
    if ((g_playback.seeking == 1 && presented != g_playback.seek_version)
        || (g_playback.seeking == 2 && playing && !buffering && position > g_playback.seek_target + 0.2 && position < g_playback.seek_target + 5)) {
        Uint32 took = SDL_GetTicks() - g_playback.seek_at;
        if (g_playback.seeking == 2 && g_playback.seeks < 5) LOGI("playback: the seek to %.0f s plays on after %u ms", g_playback.seek_target, (unsigned)took);
        g_playback.seek_total += took; g_playback.seeks++;
        if (took > g_playback.seek_max) g_playback.seek_max = took;
        g_playback.seeking = 0;
    }
    if (playing && !buffering && position > g_playback.position) g_playback.started = 1;
    if (g_playback.started && playing && !buffering) g_playback.played += dt;
    int stalled = g_playback.started && playing && buffering && !g_playback.seeking;
    if (stalled) { g_playback.buffered += dt; if (!g_playback.buffering) g_playback.stalls++; }
    g_playback.buffering = stalled;
    g_playback.position = position;
}

void perf_playback_end(void)
{
    if (!g_playback.active) return;
    g_playback.active = 0;
    LOGI("playback: %.1f s playing, %.1f s buffering, %u stalls, %u dropped frames, %u seeks", g_playback.played, g_playback.buffered, g_playback.stalls, g_playback.dropped, g_playback.seeks);
}

void perf_start_begin(const char *how)
{
    memset(&g_run, 0, sizeof g_run);
    snprintf(g_run.how, sizeof g_run.how, "%s", how);
    g_run_t0 = g_run_last = SDL_GetTicks();
    g_running = 1;
}

void perf_start_phase(const char *phase)
{
    if (!g_running) return;
    Uint32 now = SDL_GetTicks();
    if (g_run.n > 0 && !strcmp(g_run.ph[g_run.n - 1].name, phase)) { g_run.ph[g_run.n - 1].ms += now - g_run_last; g_run_last = now; return; }   /* again: a fallback */
    if (g_run.n == PHASES) return;
    snprintf(g_run.ph[g_run.n].name, sizeof g_run.ph[0].name, "%s", phase);
    g_run.ph[g_run.n++].ms = now - g_run_last;
    g_run_last = now;
}

void perf_start_cancel(void) { g_running = 0; }

static void start_line(const Start *s, char *out, size_t n)
{
    size_t o = (size_t)snprintf(out, n, "%.1f s %s%s:", s->total / 1000.0, s->how, s->ok ? "" : " (failed)");
    for (int i = 0; i < s->n && o < n; i++) o += (size_t)snprintf(out + o, n - o, "%s %s %.1f", i ? "," : "", s->ph[i].name, s->ph[i].ms / 1000.0);
}

void perf_start_done(int ok)
{
    if (!g_running) return;
    perf_start_phase("player");
    g_run.ok = ok;
    g_run.total = SDL_GetTicks() - g_run_t0;
    g_running = 0;
    memmove(g_starts + 1, g_starts, sizeof g_starts[0] * (STARTS - 1));
    g_starts[0] = g_run;
    if (g_n_starts < STARTS) g_n_starts++;
    char line[200];
    start_line(&g_run, line, sizeof line);
    LOGI("start: %s", line);
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
    { float p = perf_frame_period_ms(); ADD("Screen: about %.0f refreshes a second (%.0f ms a frame, from the frame times)\n", 1000.0f / p, p); }
    if (g_keys.n) ADD("Key to screen: typical %d ms, slow (1 in 20) %d ms, slowest %.0f ms, %u keys\n", hist_pct(&g_keys, 0.5f), hist_pct(&g_keys, 0.95f), g_keys.max, g_keys.n);
    else ADD("Key to screen: no keys yet\n");
    if (g_n_starts) {
        ADD("Stream starts, newest first (press to first frame):\n");
        for (int i = 0; i < g_n_starts; i++) { char line[200]; start_line(&g_starts[i], line, sizeof line); ADD("  %s\n", line); }
    } else ADD("Stream starts: none yet\n");
    if (g_playback.started) {
        double total = g_playback.played + g_playback.buffered;
        ADD("Playback%s: %.0f s playing, %.1f s buffering (%.2f%%), %u stalls, %u dropped frames\n", g_playback.active ? " now" : " last session",
            g_playback.played, g_playback.buffered, total > 0 ? g_playback.buffered * 100 / total : 0, g_playback.stalls, g_playback.dropped);
        if (g_playback.seeks) ADD("Seek recovery: average %u ms, slowest %u ms (%u seeks)\n", g_playback.seek_total / g_playback.seeks, g_playback.seek_max, g_playback.seeks);
    }
    if (g_uploads.n) ADD("Pictures to the GPU: %u frames, %.1f MB; typical %d ms, slow (1 in 20) %d ms, slowest %.0f ms in a frame\n", g_uploads.n, g_upload_bytes / 1048576.0, hist_pct(&g_uploads, 0.5f), hist_pct(&g_uploads, 0.95f), g_uploads.max);
    ADD("Per frame: app work typical / 1 in 20, whole frame typical / 1 in 20 / slowest, frames (late: missed a refresh), screens of pixels painted (average / most):\n");
    for (int i = 0; i < g_n_screens; i++) {
        const Hist *h = &g_screens[i].h, *t = &g_screens[i].total;
        ADD("  %-12s %3d / %3d ms   %3d / %3d / %.0f ms   %u (%u late, %.1f%%)   %.1f / %.1f\n", g_screens[i].name, hist_pct(h, 0.5f), hist_pct(h, 0.95f), hist_pct(t, 0.5f), hist_pct(t, 0.95f), t->max,
            h->n, g_screens[i].late, h->n ? 100.0 * g_screens[i].late / h->n : 0, h->n ? g_screens[i].fill_sum / h->n : 0, g_screens[i].fill_max);
    }
#undef ADD
}
