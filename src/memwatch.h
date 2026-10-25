/* Memory watch: detects a tight TV at runtime and drives the memory saver. */
#pragma once
#include <stddef.h>

typedef struct MemSample { long total_kb, avail_kb, rss_kb; } MemSample;

int    mem_sample(MemSample *out);                       /* 0 when /proc is unreadable */
int    mem_level(long total_kb, long avail_kb, long rss_kb); /* 0 plenty, 1 getting low, 2 tight (pure, unit-tested) */
int    mem_small_tv(void);                               /* a 1 GB set: saver on from the start */
size_t mem_normal_budget(void);                          /* artwork budget for this TV's RAM */
void   mem_apply_mode(void);                             /* after g_app.memory_mode changes (or at start) */
int    mem_level_now(void);                              /* mem_level of the last sample (every 5 s) */
int    mem_saver_lasting(void);                          /* the saver is on for good: a 1 GB set, or chosen in Settings */
void   mem_tick(float dt);                               /* every frame; samples every 5 s */
void   mem_emergency(void);                              /* SDL_APP_LOWMEMORY */
void   mem_playback(int playing);                        /* every frame: a video fills the screen (not in a corner) */
const char *mem_reason_text(void);                       /* why the saver is on/off, for Settings */
int    mem_describe(char *buf, size_t n);                /* "1.5 GB in the TV · 210 MB free · app 96 MB · saver on (memory got tight)" */

/* The stages the app steps down through as memory goes: 0 plenty, 1 getting low (poster
 * prefetch, trailers and seek strips paused, the P2P cache halved), 2 tight (the saver,
 * light effects, the plain clock screensaver, a smaller script sandbox). Up after 10 s at
 * the level, back down a stage after 2 min (from tight) or 1 min of room. */
typedef struct MemStageState { int stage; float up_for, down_for; } MemStageState;
int    mem_stage_step(MemStageState *st, int level, float dt_s);   /* pure, unit-tested */
int    mem_stage(void);
void   mem_stage_set(int st);                            /* the watch sets it; memlevel.c keeps it */
void   mem_system_trim(int level);                       /* Android's onTrimMemory levels (any thread) */
void   mem_background(int on);                           /* the app hidden behind the TV's Home: artwork let go */
