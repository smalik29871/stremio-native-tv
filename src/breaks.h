/* Take breaks: pauses what is playing at times that matter to the viewer — the
 * prayer times of their tradition (computed from the sun for their location),
 * a stretch reminder, bedtime, or their own times — with a reminder banner
 * first and a calm full-screen break, then playback continues by itself. */
#ifndef SNTV_BREAKS_H
#define SNTV_BREAKS_H
#include <stddef.h>

enum { BRK_ISLAM, BRK_CHRISTIAN, BRK_JEWISH, BRK_HINDU, BRK_SIKH, BRK_BAHAI, BRK_BUDDHIST, BRK_CUSTOM, BRK_SCHED_COUNT };
extern const char *const BRK_SCHED_NAMES[BRK_SCHED_COUNT];
enum { BRK_KIND_PRAYER, BRK_KIND_HEALTH, BRK_KIND_BEDTIME, BRK_KIND_CUSTOM, BRK_KIND_TEST };

typedef struct BreakEvent { char name[48]; char detail[64]; int minute; int kind; } BreakEvent;

int  breaks_today(BreakEvent *out, int max);          /* sorted by minute */
void breaks_describe_today(char *out, size_t n);      /* "Fajr 5:38 · Dhuhr 12:24 · ..." */
int  breaks_location_known(void);
void breaks_set_location(double lat, double lon, const char *city);
void breaks_detect_location(void);                     /* asks ip-api.com (or SNTV_IPAPI) once */
int  breaks_detecting(void);
double breaks_tz_hours(void);

void breaks_tick(float dt);          /* once per frame */
void breaks_draw_banner(void);       /* the reminder, over whatever screen is up */
void breaks_fire_now(const char *name, const char *detail, int kind);   /* "Try a break now" */
void breaks_skip_today(void);
int  breaks_minute_now(void);        /* local minute of day (SNTV_NOW=HH:MM overrides) */
#endif
