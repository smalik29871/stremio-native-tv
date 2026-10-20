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
    /* Toronto, 20 October 2026, UTC-4: a phone prayer app on ISNA shows Fajr 06:17, Sunrise 07:34,
     * Dhuhr 13:02, Asr 15:59, Maghrib 18:29, Isha 19:46; adhan gives 06:19 07:38 13:03 15:57 18:26 19:45 */
    sun_compute(2026, 10, 20, 43.6532, -79.3832, -4, SUN_ISNA, 0, &d);
    CHECK(near(d.fajr, 6, 19, 1) && near(d.dhuhr, 13, 3, 1) && near(d.asr, 15, 57, 1));
    CHECK(near(d.maghrib, 18, 26, 1) && near(d.isha, 19, 45, 1));
    /* Manchester in January: Asr uses the noon shadow (adhan: 14:02) */
    sun_compute(2024, 1, 15, 53.4808, -2.2426, 0, SUN_MWL, 0, &d);
    CHECK(near(d.asr, 14, 2, 2));
    /* London in June: no full night; Fajr and Isha keep to angle/60 of the night (adhan, twilight angle) */
    sun_compute(2024, 6, 21, 51.5074, -0.1278, 1, SUN_MWL, 0, &d);
    CHECK(near(d.fajr, 2, 31, 1) && near(d.isha, 23, 27, 1));
    /* Istanbul by Diyanet: its published margins on top of the angles */
    SunDay a, b;
    sun_compute(2024, 3, 20, 41.0082, 28.9784, 3, SUN_TURKEY, 0, &a);
    sun_compute(2024, 3, 20, 41.0082, 28.9784, 3, SUN_MWL, 0, &b);
    CHECK(fabs((a.maghrib - b.maghrib) * 60 - 7) < 0.1 && fabs((a.dhuhr - b.dhuhr) * 60 - 4) < 0.1);
    /* the method follows the place */
    CHECK(sun_method_for_place(43.65, -79.38) == SUN_ISNA);
    CHECK(sun_method_for_place(41.01, 28.98) == SUN_TURKEY);
    CHECK(sun_method_for_place(21.42, 39.83) == SUN_MAKKAH);
    CHECK(sun_method_for_place(25.2, 55.27) == SUN_DUBAI);
    CHECK(sun_method_for_place(51.5, -0.13) == SUN_MWL);
    CHECK(sun_method_for_country("CA") == SUN_ISNA && sun_method_for_country("GB") == SUN_MWL && sun_method_for_country("") == -1);
    /* generic twilight helper: 8.5 degrees in the evening comes after sunset */
    sun_compute(2024, 1, 1, 21.4225, 39.8262, 3, SUN_MWL, 0, &d);
    double tzeit = sun_angle_time(2024, 1, 1, 21.4225, 39.8262, 3, 8.5, 1);
    CHECK(tzeit > d.sunset && tzeit < d.sunset + 1);
    if (failures) { fprintf(stderr, "%d failure(s)\n", failures); return 1; }
    printf("sun: all checks passed\n");
    return 0;
}
