#include "suntimes.h"
#include <math.h>
#include <stdio.h>

static int failures;
#define CHECK(c) do { if (!(c)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)
static int near(double h, int hh, int mm, int tol_min) { return fabs(h - (hh + mm / 60.0)) * 60 <= tol_min; }

int main(void)
{
    SunDay d;
    /* Makkah, 1 January 2024, UTC+3: sunrise 06:59, sunset 17:51 (timeanddate), Fajr about 05:38 (Umm al-Qura) */
    sun_compute(2024, 1, 1, 21.4225, 39.8262, 3, SUN_MAKKAH, 0, &d);
    CHECK(near(d.sunrise, 6, 59, 4));
    CHECK(near(d.sunset, 17, 51, 4));
    CHECK(near(d.fajr, 5, 38, 6));
    CHECK(d.fajr < d.sunrise && d.sunrise < d.dhuhr && d.dhuhr < d.asr && d.asr < d.maghrib && d.maghrib < d.isha);
    CHECK(near(d.isha, 19, 21, 6));   /* Makkah: Maghrib + 90 min */
    /* New York, 21 June 2024, UTC-4 (EDT): sunrise 05:25, sunset 20:31; ISNA Fajr about 03:48 */
    sun_compute(2024, 6, 21, 40.7128, -74.0060, -4, SUN_ISNA, 0, &d);
    CHECK(near(d.sunrise, 5, 25, 4));
    CHECK(near(d.sunset, 20, 31, 4));
    CHECK(near(d.fajr, 3, 48, 8));
    /* Oslo in June: no astronomical night, yet every time stays finite and ordered */
    sun_compute(2024, 6, 21, 59.91, 10.75, 2, SUN_MWL, 0, &d);
    CHECK(!isnan(d.fajr) && !isnan(d.isha));
    CHECK(fmod(d.sunrise - d.fajr + 24, 24) < 6 && fmod(d.isha - d.sunset + 24, 24) < 6);   /* within hours, across midnight */
    /* Hanafi Asr is later than the standard one */
    SunDay h;
    sun_compute(2024, 1, 1, 21.4225, 39.8262, 3, SUN_MWL, 0, &d);
    sun_compute(2024, 1, 1, 21.4225, 39.8262, 3, SUN_MWL, 1, &h);
    CHECK(h.asr > d.asr + 0.5);
    /* generic twilight helper: 8.5 degrees in the evening comes after sunset */
    double tzeit = sun_angle_time(2024, 1, 1, 21.4225, 39.8262, 3, 8.5, 1);
    CHECK(tzeit > d.sunset && tzeit < d.sunset + 1);
    if (failures) { fprintf(stderr, "%d failure(s)\n", failures); return 1; }
    printf("sun: all checks passed\n");
    return 0;
}
