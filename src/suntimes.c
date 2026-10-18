#include "suntimes.h"
#include <math.h>

const char *const SUN_METHOD_NAMES[SUN_METHOD_COUNT] = {
    "Muslim World League", "ISNA (North America)", "Egyptian Authority", "Umm al-Qura (Makkah)", "Karachi", "Tehran (Geophysics)", "Jafari (Shia Ithna-Ashari)" };

typedef struct { double fajr, isha, maghrib; int isha_min, maghrib_min, midnight_jafari; } Method;
static const Method METHODS[SUN_METHOD_COUNT] = {
    { 18, 17, 0, 0, 0, 0 }, { 15, 15, 0, 0, 0, 0 }, { 19.5, 17.5, 0, 0, 0, 0 }, { 18.5, 0, 0, 90, 0, 0 },
    { 18, 18, 0, 0, 0, 0 }, { 17.7, 14, 4.5, 0, 0, 1 }, { 16, 14, 4, 0, 0, 1 } };

#define DEG(x) ((x) * 180.0 / M_PI)
#define RAD(x) ((x) * M_PI / 180.0)
static double dsin(double d) { return sin(RAD(d)); }
static double dcos(double d) { return cos(RAD(d)); }
static double dtan(double d) { return tan(RAD(d)); }
static double darcsin(double x) { return DEG(asin(x)); }
static double darccos(double x) { return DEG(acos(x)); }
static double darctan2(double y, double x) { return DEG(atan2(y, x)); }
static double darccot(double x) { return DEG(atan2(1.0, x)); }
static double fixangle(double a) { a = fmod(a, 360.0); return a < 0 ? a + 360 : a; }
static double fixhour(double a) { a = fmod(a, 24.0); return a < 0 ? a + 24 : a; }

static double julian(int year, int month, int day)
{
    if (month <= 2) { year -= 1; month += 12; }
    double A = floor(year / 100.0), B = 2 - A + floor(A / 4);
    return floor(365.25 * (year + 4716)) + floor(30.6001 * (month + 1)) + day + B - 1524.5;
}

typedef struct { double decl, eqt; } SunPos;
static SunPos sun_position(double jd)
{
    double D = jd - 2451545.0;
    double g = fixangle(357.529 + 0.98560028 * D), q = fixangle(280.459 + 0.98564736 * D);
    double L = fixangle(q + 1.915 * dsin(g) + 0.020 * dsin(2 * g));
    double e = 23.439 - 0.00000036 * D;
    double RA = darctan2(dcos(e) * dsin(L), dcos(L)) / 15;
    SunPos p = { darcsin(dsin(e) * dsin(L)), q / 15 - fixhour(RA) };
    return p;
}

typedef struct { double jd, lat, lon, tz; } Ctx;

static double mid_day(const Ctx *c, double t) { return fixhour(12 - sun_position(c->jd + t).eqt); }

/* day portion (0..1) in, hours (solar) out */
static double angle_time(const Ctx *c, double angle, double t, int dir)
{
    double decl = sun_position(c->jd + t).decl, noon = mid_day(c, t);
    double x = (-dsin(angle) - dsin(decl) * dsin(c->lat)) / (dcos(decl) * dcos(c->lat));
    if (x < -1 || x > 1) return NAN;
    double tt = darccos(x) / 15;
    return noon + (dir < 0 ? -tt : tt);
}

static double asr_time(const Ctx *c, double factor, double t)
{
    double decl = sun_position(c->jd + t).decl;
    double angle = -darccot(factor + dtan(fabs(c->lat - decl)));
    return angle_time(c, angle, t, 1);
}

static double to_local(const Ctx *c, double solar) { return isnan(solar) ? NAN : solar + c->tz - c->lon / 15; }
static double time_diff(double a, double b) { return fixhour(b - a); }

double sun_angle_time(int year, int month, int day, double lat, double lon, double tz, double angle, int direction)
{
    Ctx c = { julian(year, month, day) - lon / (15 * 24), lat, lon, tz };
    double t = direction < 0 ? 6.0 / 24 : 18.0 / 24;
    return fixhour(to_local(&c, angle_time(&c, angle, t, direction)));
}

void sun_compute(int year, int month, int day, double lat, double lon, double tz, int method, int asr_hanafi, SunDay *out)
{
    if (method < 0 || method >= SUN_METHOD_COUNT) method = SUN_MWL;
    const Method *m = &METHODS[method];
    Ctx c = { julian(year, month, day) - lon / (15 * 24), lat, lon, tz };
    double fajr = angle_time(&c, m->fajr, 5.0 / 24, -1);
    double sunrise = angle_time(&c, 0.833, 6.0 / 24, -1);
    double dhuhr = mid_day(&c, 12.0 / 24);
    double asr = asr_time(&c, asr_hanafi ? 2 : 1, 13.0 / 24);
    double sunset = angle_time(&c, 0.833, 18.0 / 24, 1);
    double maghrib = m->maghrib > 0 ? angle_time(&c, m->maghrib, 18.0 / 24, 1) : NAN;
    double isha = m->isha > 0 ? angle_time(&c, m->isha, 18.0 / 24, 1) : NAN;
    double dawn6 = angle_time(&c, 6, 6.0 / 24, -1), dusk6 = angle_time(&c, 6, 18.0 / 24, 1);

    /* to local clock */
    fajr = to_local(&c, fajr); sunrise = to_local(&c, sunrise); dhuhr = to_local(&c, dhuhr); asr = to_local(&c, asr);
    sunset = to_local(&c, sunset); maghrib = to_local(&c, maghrib); isha = to_local(&c, isha);
    dawn6 = to_local(&c, dawn6); dusk6 = to_local(&c, dusk6);
    if (isnan(maghrib)) maghrib = sunset + m->maghrib_min / 60.0;
    if (isnan(isha) && m->isha_min) isha = maghrib + m->isha_min / 60.0;

    /* high latitudes: twilight that never ends is capped at the middle of the night */
    if (!isnan(sunrise) && !isnan(sunset)) {
        double night = time_diff(sunset, sunrise), portion = night / 2;
        if (isnan(fajr) || time_diff(fajr, sunrise) > portion) fajr = sunrise - portion;
        if (isnan(isha) || time_diff(sunset, isha) > portion) isha = sunset + portion;
        if (isnan(maghrib) || time_diff(sunset, maghrib) > portion) maghrib = sunset + portion;
    }
    dhuhr += 1.0 / 60;   /* a minute past the meridian, as the apps do */
    double midnight = m->midnight_jafari ? sunset + time_diff(sunset, fajr) / 2 : sunset + time_diff(sunset, sunrise) / 2;

    out->sunrise = fixhour(sunrise); out->noon = fixhour(dhuhr - 1.0 / 60); out->sunset = fixhour(sunset);
    out->dawn6 = isnan(dawn6) ? NAN : fixhour(dawn6); out->dusk6 = isnan(dusk6) ? NAN : fixhour(dusk6);
    out->fajr = fixhour(fajr); out->dhuhr = fixhour(dhuhr); out->asr = fixhour(asr);
    out->maghrib = fixhour(maghrib); out->isha = fixhour(isha); out->midnight = fixhour(midnight);
}
