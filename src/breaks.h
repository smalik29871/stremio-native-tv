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
/* The prayer time method in use: the chosen one, or the local authority's for the location. */
int  breaks_method(void);
void breaks_set_location(double lat, double lon, const char *city);
void breaks_detect_location(void);                     /* asks ip-api.com (or SNTV_IPAPI) once */
int  breaks_detecting(void);
double breaks_tz_hours(void);

void breaks_tick(float dt);          /* once per frame */
void breaks_draw_banner(void);       /* the reminder, over whatever screen is up */
void breaks_fire_now(const char *name, const char *detail, int kind);   /* "Try a break now" */
void breaks_skip_today(void);
int  breaks_minute_now(void);        /* local minute of day (SNTV_NOW=HH:MM overrides) */
/* The next prayer of the day, as a line for the screen: "Maghrib in 42 min", "Isha at 9:14 PM",
 * tomorrow's first once today's are past. 0 (and an empty line) when the tradition has no
 * prayer times or the location is not known. */
int  breaks_next_prayer(char *out, size_t n);
void breaks_settings_changed(void);   /* recompute today's times before the next check */
/* A break recording as the TV can play it: a file sent from the phone, a direct link,
 * a YouTube link saved on the TV (youtube.h), or another page link fetched by the home
 * helper. NULL when it cannot play yet. *is_video says whether it has a picture to show. */
const char *breaks_media_url(const char *stored, char *out, size_t n, int *is_video);
int breaks_is_page_link(const char *url);
#endif
