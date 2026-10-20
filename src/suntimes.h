/* Sun-based times for a day and place: sunrise, solar noon, sunset, twilight at
 * any depression angle, and the Islamic prayer times with the usual calculation
 * methods (a port of the PrayTimes.org algorithm). All results are local hours
 * (e.g. 17.75 = 17:45); NaN when the event does not happen that day. */
#ifndef SNTV_SUNTIMES_H
#define SNTV_SUNTIMES_H

enum { SUN_MWL, SUN_ISNA, SUN_EGYPT, SUN_MAKKAH, SUN_KARACHI, SUN_TEHRAN, SUN_JAFARI,   /* stored by number: add at the end */
       SUN_DUBAI, SUN_KUWAIT, SUN_QATAR, SUN_SINGAPORE, SUN_UOIF, SUN_TURKEY, SUN_RUSSIA, SUN_JAKIM, SUN_KEMENAG,
       SUN_TUNISIA, SUN_ALGERIA, SUN_MOROCCO, SUN_JORDAN, SUN_METHOD_COUNT };
/* The method the local authority of a country uses ("TR", "GB"...), or -1 when unknown. */
int sun_method_for_country(const char *cc);
/* The same guessed from coordinates alone (a rough map of countries). */
int sun_method_for_place(double lat, double lon);
extern const char *const SUN_METHOD_NAMES[SUN_METHOD_COUNT];

typedef struct SunDay {
    double sunrise, noon, sunset;
    double dawn6, dusk6;           /* civil twilight (6 degrees) */
    double fajr, dhuhr, asr, maghrib, isha, midnight;
} SunDay;

/* tz_hours: the local offset from UTC (e.g. 3 for Riyadh, -5 for New York). */
void sun_compute(int year, int month, int day, double lat, double lon, double tz_hours, int method, int asr_hanafi, SunDay *out);
/* Hours below the horizon for an arbitrary angle: direction -1 morning, +1 evening. */
double sun_angle_time(int year, int month, int day, double lat, double lon, double tz_hours, double angle, int direction);
#endif
