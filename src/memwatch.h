/* Memory watch: detects a tight TV at runtime and drives the memory saver. */
#pragma once
#include <stddef.h>

typedef struct MemSample { long total_kb, avail_kb, rss_kb; } MemSample;

int    mem_sample(MemSample *out);                       /* 0 when /proc is unreadable */
int    mem_level(long total_kb, long avail_kb, long rss_kb); /* 0 plenty, 1 getting low, 2 tight (pure, unit-tested) */
int    mem_small_tv(void);                               /* a 1 GB set: saver on from the start */
size_t mem_normal_budget(void);                          /* artwork budget for this TV's RAM */
void   mem_apply_mode(void);                             /* after g_app.memory_mode changes (or at start) */
void   mem_tick(float dt);                               /* every frame; samples every 5 s */
void   mem_emergency(void);                              /* SDL_APP_LOWMEMORY */
void   mem_playback(int playing);                        /* every frame: a video fills the screen (not in a corner) */
const char *mem_reason_text(void);                       /* why the saver is on/off, for Settings */
int    mem_describe(char *buf, size_t n);                /* "1.5 GB in the TV · 210 MB free · app 96 MB" */
