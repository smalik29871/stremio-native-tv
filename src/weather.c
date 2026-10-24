#include "weather.h"
#include "app.h"
#include "http.h"
#include "log.h"
#include "store.h"
#include "../vendor/cJSON.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static Weather g_w;
static int g_busy;
static time_t g_asked;
static double g_lat, g_lon;
static int g_have_place;
static char g_cc[4];

const char *weather_kind_name(int kind)
{
    static const char *const N[] = { "Clear", "Partly cloudy", "Cloudy", "Fog", "Drizzle", "Rain", "Snow", "Thunderstorm" };
    return kind >= 0 && kind <= WX_STORM ? N[kind] : "";
}

/* WMO weather codes, as Open-Meteo gives them */
static int kind_of(int code)
{
    if (code == 0) return WX_CLEAR;
    if (code <= 2) return WX_PARTLY;
    if (code == 3) return WX_CLOUDY;
    if (code == 45 || code == 48) return WX_FOG;
    if (code >= 51 && code <= 57) return WX_DRIZZLE;
    if ((code >= 61 && code <= 67) || (code >= 80 && code <= 82)) return WX_RAIN;
    if ((code >= 71 && code <= 77) || code == 85 || code == 86) return WX_SNOW;
    if (code >= 95) return WX_STORM;
    return WX_CLOUDY;
}

static void on_weather(HttpResponse *r, void *user)
{
    (void)user;
    g_busy = 0;
    cJSON *root = r->status == 200 && r->body ? cJSON_Parse(r->body) : NULL;
    const cJSON *cur = cJSON_GetObjectItemCaseSensitive(root, "current");
    const cJSON *t = cJSON_GetObjectItemCaseSensitive(cur, "temperature_2m"), *c = cJSON_GetObjectItemCaseSensitive(cur, "weather_code"), *d = cJSON_GetObjectItemCaseSensitive(cur, "is_day");
    if (cJSON_IsNumber(t) && cJSON_IsNumber(c)) {
        g_w.known = 1;
        g_w.temp = (float)t->valuedouble;
        g_w.kind = kind_of(c->valueint);
        g_w.is_day = cJSON_IsNumber(d) ? d->valueint : 1;
        LOGI("weather: %.0f%s, %s%s", g_w.temp, g_w.fahrenheit ? "F" : "C", weather_kind_name(g_w.kind), g_w.is_day ? "" : " (night)");
        app_redraw();
    } else LOGW("weather: no answer (%ld)", r->status);
    cJSON_Delete(root);
}

static void ask_weather(void)
{
    const char *base = getenv("SNTV_WEATHER_API");
    char url[400];
    g_w.fahrenheit = !strcmp(g_cc, "US") || !strcmp(g_cc, "LR") || !strcmp(g_cc, "MM");   /* where Fahrenheit is read */
    snprintf(url, sizeof url, "%s/v1/forecast?latitude=%.3f&longitude=%.3f&current=temperature_2m,weather_code,is_day%s",
             base && *base ? base : "https://api.open-meteo.com", g_lat, g_lon, g_w.fahrenheit ? "&temperature_unit=fahrenheit" : "");
    g_busy = 1;
    http_request_async(url, NULL, on_weather, NULL);
}

static void on_place(HttpResponse *r, void *user)
{
    (void)user;
    cJSON *root = r->status == 200 && r->body ? cJSON_Parse(r->body) : NULL;
    const cJSON *lat = cJSON_GetObjectItemCaseSensitive(root, "lat"), *lon = cJSON_GetObjectItemCaseSensitive(root, "lon");
    const cJSON *city = cJSON_GetObjectItemCaseSensitive(root, "city"), *cc = cJSON_GetObjectItemCaseSensitive(root, "countryCode");
    if (cJSON_IsNumber(lat) && cJSON_IsNumber(lon)) {
        g_lat = lat->valuedouble; g_lon = lon->valuedouble; g_have_place = 1;
        snprintf(g_w.place, sizeof g_w.place, "%s", cJSON_IsString(city) ? city->valuestring : "");
        snprintf(g_cc, sizeof g_cc, "%s", cJSON_IsString(cc) ? cc->valuestring : "");
        char keep[160];   /* asked once: kept for the next time */
        snprintf(keep, sizeof keep, "%.3f,%.3f,%s,%s", g_lat, g_lon, g_cc, g_w.place);
        store_set_str("weatherPlace", keep);
        store_save();
        ask_weather();
    } else { g_busy = 0; LOGW("weather: the place could not be found (%ld)", r->status); }
    cJSON_Delete(root);
}

const Weather *weather_now(void)
{
    time_t now = time(NULL);
    if (g_busy || (g_asked && now - g_asked < (g_w.known ? 1800 : 300))) return &g_w;
    g_asked = now;
    if (g_app.break_lat != 0 || g_app.break_lon != 0) {   /* the place set for breaks */
        g_lat = g_app.break_lat; g_lon = g_app.break_lon; g_have_place = 1;
        snprintf(g_w.place, sizeof g_w.place, "%s", g_app.break_city);
        snprintf(g_cc, sizeof g_cc, "%s", g_app.break_country);
    } else if (!g_have_place) {
        const char *keep = store_get_str("weatherPlace", "");
        char cc[4] = "", place[64] = "";
        if (keep[0] && sscanf(keep, "%lf,%lf,%3[^,],%63[^\n]", &g_lat, &g_lon, cc, place) >= 2) {
            g_have_place = 1;
            snprintf(g_cc, sizeof g_cc, "%s", cc); snprintf(g_w.place, sizeof g_w.place, "%s", place);
        }
    }
    if (g_have_place) { ask_weather(); return &g_w; }
    const char *ip = getenv("SNTV_IPAPI");
    g_busy = 1;
    http_request_async(ip && *ip ? ip : "http://ip-api.com/json/?fields=status,city,countryCode,lat,lon", NULL, on_place, NULL);
    return &g_w;
}
