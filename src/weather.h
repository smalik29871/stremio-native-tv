/* The weather where the TV is, for the Weather clock: Open-Meteo's current conditions
 * (free, no key), asked every half hour. The place is the one set for breaks, else
 * found once from the internet address. */
#ifndef SNTV_WEATHER_H
#define SNTV_WEATHER_H

enum { WX_CLEAR, WX_PARTLY, WX_CLOUDY, WX_FOG, WX_DRIZZLE, WX_RAIN, WX_SNOW, WX_STORM };

typedef struct {
    int known;            /* 0 until the first answer */
    float temp;           /* in the units below */
    int fahrenheit, kind, is_day;
    char place[64];
} Weather;

/* Now; starts or renews the lookups by itself. */
const Weather *weather_now(void);
const char *weather_kind_name(int kind);
#endif
