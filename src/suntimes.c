#include "suntimes.h"
#include <math.h>
#include <stddef.h>

const char *const SUN_METHOD_NAMES[SUN_METHOD_COUNT] = {
    "Muslim World League", "ISNA (North America)", "Egyptian Authority", "Umm al-Qura (Makkah)", "Karachi", "Tehran (Geophysics)", "Jafari (Shia Ithna-Ashari)",
    "Dubai (UAE)", "Kuwait", "Qatar", "Singapore (MUIS)", "France (UOIF)", "Turkey (Diyanet)", "Russia", "Malaysia (JAKIM)", "Indonesia (Kemenag)",
    "Tunisia", "Algeria", "Morocco", "Jordan" };

/* angles in degrees below the horizon (0: by minutes instead); adj: minutes added to
 * fajr, (unused), dhuhr, asr, maghrib, isha, as each authority publishes them */
typedef struct { double fajr, isha, maghrib; int isha_min, maghrib_min, midnight_jafari; int adj[6]; } Method;
static const Method METHODS[SUN_METHOD_COUNT] = {
    [SUN_MWL] = { 18, 17, 0, 0, 0, 0, { 0, 0, 1 } },
    [SUN_ISNA] = { 15, 15, 0, 0, 0, 0, { 0, 0, 1 } },
    [SUN_EGYPT] = { 19.5, 17.5, 0, 0, 0, 0, { 0, 0, 1 } },
    [SUN_MAKKAH] = { 18.5, 0, 0, 90, 0, 0, { 0 } },
    [SUN_KARACHI] = { 18, 18, 0, 0, 0, 0, { 0, 0, 1 } },
    [SUN_TEHRAN] = { 17.7, 14, 4.5, 0, 0, 1, { 0 } },
    [SUN_JAFARI] = { 16, 14, 4, 0, 0, 1, { 0 } },
    [SUN_DUBAI] = { 18.2, 18.2, 0, 0, 0, 0, { 0, 0, 3, 3, 3, 0 } },
    [SUN_KUWAIT] = { 18, 17.5, 0, 0, 0, 0, { 0 } },
    [SUN_QATAR] = { 18, 0, 0, 90, 0, 0, { 0 } },
    [SUN_SINGAPORE] = { 20, 18, 0, 0, 0, 0, { 0, 0, 1 } },
    [SUN_UOIF] = { 12, 12, 0, 0, 0, 0, { 0 } },
    [SUN_TURKEY] = { 18, 17, 0, 0, 0, 0, { 0, 0, 5, 4, 7, 0 } },
    [SUN_RUSSIA] = { 16, 15, 0, 0, 0, 0, { 0 } },
    [SUN_JAKIM] = { 20, 18, 0, 0, 0, 0, { 0, 0, 1 } },
    [SUN_KEMENAG] = { 20, 18, 0, 0, 0, 0, { 0, 0, 1 } },
    [SUN_TUNISIA] = { 18, 18, 0, 0, 0, 0, { 0 } },
    [SUN_ALGERIA] = { 18, 17, 0, 0, 3, 0, { 0 } },
    [SUN_MOROCCO] = { 19, 17, 0, 0, 5, 0, { 0, 0, 5 } },
    [SUN_JORDAN] = { 18, 18, 0, 0, 5, 0, { 0 } },
};

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

/* Asr: the shadow is its object's length (twice, Hanafi) longer than it was at noon,
 * so the noon shadow takes the sun's height at noon */
static double asr_time(const Ctx *c, double factor, double t)
{
    double decl = sun_position(c->jd + mid_day(c, 0.5) / 24).decl;
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
    /* solar hours; each pass takes the sun's place at the time the last one found, so a
     * time far from its first guess (Asr in a northern winter) still lands on the minute */
    double fajr = 5, sunrise = 6, dhuhr = 12, asr = 13, sunset = 18, maghrib = 18, isha = 18, dawn6 = 6, dusk6 = 18;
    for (int pass = 0; pass < 3; pass++) {
#define AT(h, dflt) ((isnan(h) ? (dflt) : (h)) / 24)
        double f = angle_time(&c, m->fajr, AT(fajr, 5), -1);
        double sr = angle_time(&c, 0.833, AT(sunrise, 6), -1);
        double dh = mid_day(&c, AT(dhuhr, 12));
        double as = asr_time(&c, asr_hanafi ? 2 : 1, AT(asr, 13));
        double ss = angle_time(&c, 0.833, AT(sunset, 18), 1);
        double mg = m->maghrib > 0 ? angle_time(&c, m->maghrib, AT(maghrib, 18), 1) : NAN;
        double is = m->isha > 0 ? angle_time(&c, m->isha, AT(isha, 18), 1) : NAN;
        double d6 = angle_time(&c, 6, AT(dawn6, 6), -1), k6 = angle_time(&c, 6, AT(dusk6, 18), 1);
#undef AT
        fajr = f; sunrise = sr; dhuhr = dh; asr = as; sunset = ss; maghrib = mg; isha = is; dawn6 = d6; dusk6 = k6;
    }

    /* to local clock */
    fajr = to_local(&c, fajr); sunrise = to_local(&c, sunrise); dhuhr = to_local(&c, dhuhr); asr = to_local(&c, asr);
    sunset = to_local(&c, sunset); maghrib = to_local(&c, maghrib); isha = to_local(&c, isha);
    dawn6 = to_local(&c, dawn6); dusk6 = to_local(&c, dusk6);
    if (isnan(maghrib)) maghrib = sunset + m->maghrib_min / 60.0;
    if (isnan(isha) && m->isha_min) isha = maghrib + m->isha_min / 60.0;

    /* high latitudes: when twilight lasts too long (or all night), Fajr and Isha keep to
     * a share of the night set by their angle, as most prayer apps do (angle / 60) */
    if (!isnan(sunrise) && !isnan(sunset)) {
        double night = time_diff(sunset, sunrise);
        double fp = night * m->fajr / 60, ip = night * (m->isha > 0 ? m->isha : 18) / 60;
        if (isnan(fajr) || time_diff(fajr, sunrise) > fp) fajr = sunrise - fp;
        if (m->isha > 0 && (isnan(isha) || time_diff(sunset, isha) > ip)) isha = sunset + ip;
        if (m->maghrib > 0 && (isnan(maghrib) || time_diff(sunset, maghrib) > night / 2)) maghrib = sunset + night / 2;
    }
    double noon = dhuhr;
    double midnight = m->midnight_jafari ? sunset + time_diff(sunset, fajr) / 2 : sunset + time_diff(sunset, sunrise) / 2;
    /* the method's own minutes on top (a minute after the meridian, Diyanet's margins) */
    fajr += m->adj[0] / 60.0; dhuhr += m->adj[2] / 60.0;
    asr += m->adj[3] / 60.0; maghrib += m->adj[4] / 60.0; isha += m->adj[5] / 60.0;

    out->sunrise = fixhour(sunrise); out->noon = fixhour(noon); out->sunset = fixhour(sunset);
    out->dawn6 = isnan(dawn6) ? NAN : fixhour(dawn6); out->dusk6 = isnan(dusk6) ? NAN : fixhour(dusk6);
    out->fajr = fixhour(fajr); out->dhuhr = fixhour(dhuhr); out->asr = fixhour(asr);
    out->maghrib = fixhour(maghrib); out->isha = fixhour(isha); out->midnight = fixhour(midnight);
}

int sun_method_for_country(const char *cc)
{
    static const struct { const char *cc; int m; } T[] = {
        { "US", SUN_ISNA }, { "CA", SUN_ISNA }, { "MX", SUN_ISNA },
        { "SA", SUN_MAKKAH }, { "YE", SUN_MAKKAH }, { "BH", SUN_MAKKAH }, { "OM", SUN_MAKKAH },
        { "AE", SUN_DUBAI }, { "KW", SUN_KUWAIT }, { "QA", SUN_QATAR },
        { "EG", SUN_EGYPT }, { "SD", SUN_EGYPT }, { "LY", SUN_EGYPT }, { "SY", SUN_EGYPT }, { "IQ", SUN_EGYPT }, { "LB", SUN_EGYPT }, { "PS", SUN_EGYPT },
        { "PK", SUN_KARACHI }, { "IN", SUN_KARACHI }, { "BD", SUN_KARACHI }, { "AF", SUN_KARACHI }, { "LK", SUN_KARACHI }, { "NP", SUN_KARACHI },
        { "IR", SUN_TEHRAN }, { "TR", SUN_TURKEY }, { "RU", SUN_RUSSIA }, { "FR", SUN_UOIF },
        { "SG", SUN_SINGAPORE }, { "MY", SUN_JAKIM }, { "BN", SUN_SINGAPORE }, { "ID", SUN_KEMENAG },
        { "TN", SUN_TUNISIA }, { "DZ", SUN_ALGERIA }, { "MA", SUN_MOROCCO }, { "JO", SUN_JORDAN },
    };
    if (!cc || !cc[0]) return -1;
    for (size_t i = 0; i < sizeof T / sizeof T[0]; i++) if (cc[0] == T[i].cc[0] && cc[1] == T[i].cc[1]) return T[i].m;
    return SUN_MWL;
}

int sun_method_for_place(double lat, double lon)
{
    /* rough boxes, the smaller countries first; anywhere else the Muslim World League */
    static const struct { double lat0, lat1, lon0, lon1; int m; } B[] = {
        { 1.15, 1.48, 103.6, 104.1, SUN_SINGAPORE }, { 24.4, 26.2, 50.7, 51.7, SUN_QATAR }, { 28.5, 30.1, 46.5, 48.5, SUN_KUWAIT },
        { 22.5, 26.3, 51.5, 56.5, SUN_DUBAI }, { 29.2, 33.4, 34.9, 39.3, SUN_JORDAN }, { 16, 32.2, 34.5, 55.7, SUN_MAKKAH },
        { 22, 31.7, 24.7, 36.9, SUN_EGYPT }, { 35.8, 42.2, 26, 45, SUN_TURKEY }, { 25, 39.8, 44, 63.3, SUN_TEHRAN },
        { 5, 37.5, 60, 92.7, SUN_KARACHI }, { 0.8, 7.4, 99.6, 119.3, SUN_JAKIM }, { -11, 6, 95, 141, SUN_KEMENAG },
        { 30, 37.6, 7.5, 11.6, SUN_TUNISIA }, { 27.6, 36, -17, -1, SUN_MOROCCO }, { 19, 37.1, -8.7, 12, SUN_ALGERIA },
        { 41.3, 51.1, -5.2, 9.6, SUN_UOIF }, { 41, 82, 27, 180, SUN_RUSSIA }, { 14, 83, -170, -50, SUN_ISNA },
    };
    for (size_t i = 0; i < sizeof B / sizeof B[0]; i++)
        if (lat >= B[i].lat0 && lat <= B[i].lat1 && lon >= B[i].lon0 && lon <= B[i].lon1) return B[i].m;
    return SUN_MWL;
}
