#include "breaks.h"
#include "phone.h"
#include "app.h"
#include "audio.h"
#include "http.h"
#include "log.h"
#include "suntimes.h"
#include "youtube.h"
#include "torbox.h"
#include "ui/font.h"
#include "ui/theme.h"
#include "ui/widgets.h"
#include "../vendor/cJSON.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

const char *const BRK_SCHED_NAMES[BRK_SCHED_COUNT] = {
    "Islamic prayer times", "Christian (Angelus)", "Jewish (Shacharit, Mincha, Maariv)", "Hindu (Sandhya)",
    "Sikh (Nitnem)", "Baha'i (obligatory prayer)", "Buddhist (morning, evening)", "My own times only" };

static struct {
    int day_key;                 /* yday of the cached list */
    BreakEvent ev[16]; int n;
    int fired[16];               /* minute keys fired today */
    int skipped_day;             /* breaks skipped for this day */
    int banner_for;              /* minute of the event the banner shows, -1 none */
    float watch_s;               /* continuous watching, for the stretch reminder */
    int detecting;
    int stale;                   /* settings changed: recompute the list, keep what already fired */
} B = { .day_key = -1, .skipped_day = -1, .banner_for = -1 };

static struct tm now_tm(void)
{
    time_t t = time(NULL); struct tm tmv; localtime_r(&t, &tmv);
    const char *o = getenv("SNTV_NOW");
    int hh, mm, after = 0;   /* tests: "12:00", or "12:00+5" (a minute earlier until 5 s after the start) */
    static time_t started;
    if (!started) started = t;
    if (o && sscanf(o, "%d:%d+%d", &hh, &mm, &after) >= 2) {
        int m = hh * 60 + mm - (t - started < after ? 1 : 0);
        if (m < 0) m += 24 * 60;
        tmv.tm_hour = m / 60; tmv.tm_min = m % 60; tmv.tm_sec = time(NULL) % 60;
    }
    return tmv;
}

int breaks_minute_now(void) { struct tm t = now_tm(); return t.tm_hour * 60 + t.tm_min; }
double breaks_tz_hours(void) { struct tm t = now_tm(); return t.tm_gmtoff / 3600.0; }
int breaks_location_known(void) { return g_app.break_lat != 0 || g_app.break_lon != 0; }

int breaks_method(void)
{
    if (g_app.break_method >= 0 && g_app.break_method < SUN_METHOD_COUNT) return g_app.break_method;
    int m = sun_method_for_country(g_app.break_country);
    if (m < 0) m = breaks_location_known() ? sun_method_for_place(g_app.break_lat, g_app.break_lon) : SUN_MWL;
    return m;
}

static int hm(double hours) { if (isnan(hours)) return -1; int m = (int)floor(hours * 60 + 0.5); return ((m % 1440) + 1440) % 1440; }

static void add(BreakEvent *out, int *n, int max, const char *name, const char *detail, int minute, int kind)
{
    if (minute < 0 || *n >= max) return;
    BreakEvent *e = &out[(*n)++];
    snprintf(e->name, sizeof e->name, "%s", name);
    snprintf(e->detail, sizeof e->detail, "%s", detail ? detail : "");
    e->minute = minute; e->kind = kind;
}

static int cmp_ev(const void *a, const void *b) { return ((const BreakEvent *)a)->minute - ((const BreakEvent *)b)->minute; }

int breaks_today(BreakEvent *out, int max)
{
    int n = 0;
    struct tm t = now_tm();
    SunDay d; int have_sun = 0;
    if (breaks_location_known()) {
        sun_compute(t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, g_app.break_lat, g_app.break_lon, breaks_tz_hours(), breaks_method(), g_app.break_asr_hanafi, &d);
        have_sun = 1;
    }
    switch (g_app.break_sched) {
    case BRK_ISLAM:
        if (have_sun) {
            add(out, &n, max, "Fajr", "Dawn prayer", hm(d.fajr), BRK_KIND_PRAYER);
            add(out, &n, max, "Dhuhr", "Noon prayer", hm(d.dhuhr), BRK_KIND_PRAYER);
            add(out, &n, max, "Asr", "Afternoon prayer", hm(d.asr), BRK_KIND_PRAYER);
            add(out, &n, max, "Maghrib", "Sunset prayer", hm(d.maghrib), BRK_KIND_PRAYER);
            add(out, &n, max, "Isha", "Night prayer", hm(d.isha), BRK_KIND_PRAYER);
        }
        break;
    case BRK_CHRISTIAN:
        add(out, &n, max, "Angelus", "Morning", 6 * 60, BRK_KIND_PRAYER);
        add(out, &n, max, "Angelus", "Midday", 12 * 60, BRK_KIND_PRAYER);
        add(out, &n, max, "Angelus", "Evening", 18 * 60, BRK_KIND_PRAYER);
        break;
    case BRK_JEWISH:
        if (have_sun) {
            double hour = (d.sunset - d.sunrise) / 12;   /* a halachic hour */
            double tzeit = sun_angle_time(t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, g_app.break_lat, g_app.break_lon, breaks_tz_hours(), 8.5, 1);
            add(out, &n, max, "Shacharit", "From sunrise", hm(d.sunrise), BRK_KIND_PRAYER);
            add(out, &n, max, "Mincha", "From Mincha Gedola", hm(d.sunrise + 6.5 * hour), BRK_KIND_PRAYER);
            add(out, &n, max, "Maariv", "From nightfall", hm(tzeit), BRK_KIND_PRAYER);
        }
        break;
    case BRK_HINDU:
        if (have_sun) {
            add(out, &n, max, "Pratah Sandhya", "Sunrise", hm(d.sunrise), BRK_KIND_PRAYER);
            add(out, &n, max, "Madhyahnika", "Solar noon", hm(d.noon), BRK_KIND_PRAYER);
            add(out, &n, max, "Sayam Sandhya", "Sunset", hm(d.sunset), BRK_KIND_PRAYER);
        }
        break;
    case BRK_SIKH:
        add(out, &n, max, "Amrit Vela", "Japji Sahib", 4 * 60, BRK_KIND_PRAYER);
        if (have_sun) add(out, &n, max, "Rehras Sahib", "Sunset", hm(d.sunset), BRK_KIND_PRAYER); else add(out, &n, max, "Rehras Sahib", "Evening", 18 * 60 + 30, BRK_KIND_PRAYER);
        add(out, &n, max, "Kirtan Sohila", "Bedtime", 22 * 60, BRK_KIND_PRAYER);
        break;
    case BRK_BAHAI:
        if (have_sun) {
            add(out, &n, max, "Morning prayer", "Sunrise to noon", hm(d.sunrise), BRK_KIND_PRAYER);
            add(out, &n, max, "Noon prayer", "Noon to sunset", hm(d.noon), BRK_KIND_PRAYER);
            add(out, &n, max, "Evening prayer", "Sunset to two hours after", hm(d.sunset), BRK_KIND_PRAYER);
        }
        break;
    case BRK_BUDDHIST:
        add(out, &n, max, "Morning chanting", "Puja", 6 * 60, BRK_KIND_PRAYER);
        add(out, &n, max, "Evening chanting", "Puja", 18 * 60, BRK_KIND_PRAYER);
        break;
    default: break;
    }
    for (int i = 0; i < 3; i++) if (g_app.break_custom[i] >= 0) { char nm[32]; snprintf(nm, sizeof nm, "Break %d", i + 1); add(out, &n, max, nm, "Your time", g_app.break_custom[i], BRK_KIND_CUSTOM); }
    if (g_app.break_bed >= 0) add(out, &n, max, "Bedtime", "Playback stops", g_app.break_bed, BRK_KIND_BEDTIME);
    qsort(out, (size_t)n, sizeof *out, cmp_ev);
    return n;
}

void breaks_describe_today(char *out, size_t n)
{
    BreakEvent ev[16];
    int k = breaks_today(ev, 16);
    out[0] = 0;
    if (!k) { snprintf(out, n, breaks_location_known() || g_app.break_sched == BRK_CUSTOM || g_app.break_sched == BRK_CHRISTIAN ? "Nothing scheduled" : "Set the location first"); return; }
    size_t o = 0;
    for (int i = 0; i < k && o < n; i++)
        o += (size_t)snprintf(out + o, n - o, "%s%s %d:%02d", i ? "  \xC2\xB7  " : "", ev[i].name, ev[i].minute / 60, ev[i].minute % 60);
}

void breaks_set_location(double lat, double lon, const char *city)
{
    g_app.break_lat = lat; g_app.break_lon = lon;
    snprintf(g_app.break_city, sizeof g_app.break_city, "%s", city ? city : "");
    g_app.break_country[0] = 0;   /* typed coordinates: the method comes from the map */
    app_save_prefs();
    B.day_key = -1;
}

void breaks_settings_changed(void) { B.stale = 1; }

static void on_ipapi(HttpResponse *r, void *user)
{
    (void)user;
    B.detecting = 0;
    cJSON *root = r->status == 200 ? cJSON_Parse(r->body) : NULL;
    const cJSON *lat = root ? cJSON_GetObjectItemCaseSensitive(root, "lat") : NULL, *lon = root ? cJSON_GetObjectItemCaseSensitive(root, "lon") : NULL;
    const cJSON *city = root ? cJSON_GetObjectItemCaseSensitive(root, "city") : NULL;
    const cJSON *cc = root ? cJSON_GetObjectItemCaseSensitive(root, "countryCode") : NULL;
    if (cJSON_IsNumber(lat) && cJSON_IsNumber(lon)) {
        breaks_set_location(lat->valuedouble, lon->valuedouble, cJSON_IsString(city) ? city->valuestring : "");
        if (cJSON_IsString(cc)) { snprintf(g_app.break_country, sizeof g_app.break_country, "%s", cc->valuestring); app_save_prefs(); }
        LOGI("breaks: prayer times by %s", SUN_METHOD_NAMES[breaks_method()]);
        app_toast("Location: %s (%.2f, %.2f)", g_app.break_city[0] ? g_app.break_city : "found", lat->valuedouble, lon->valuedouble);
        LOGI("breaks: location from the internet %.3f, %.3f %s", lat->valuedouble, lon->valuedouble, g_app.break_city);
    } else { app_toast("Could not find the location; enter it by hand"); LOGW("breaks: ip-api %ld %s", r->status, r->error); }
    cJSON_Delete(root);
    app_redraw();
}

void breaks_detect_location(void)
{
    if (B.detecting) return;
    B.detecting = 1;
    const char *base = getenv("SNTV_IPAPI");
    http_request_async(base && *base ? base : "http://ip-api.com/json/?fields=status,city,countryCode,lat,lon", NULL, on_ipapi, NULL);
}
int breaks_detecting(void) { return B.detecting; }

/* ---- firing ------------------------------------------------------------------ */

static int watching(void)
{
    Screen *top = app_top();
    if (g_app.pip) return 1;
    return top && strcmp(top->name, "player") == 0;
}

static void refresh_day(void)
{
    struct tm t = now_tm();
    int key = t.tm_yday + (t.tm_year << 9);
    if (key != B.day_key) { B.day_key = key; B.n = breaks_today(B.ev, 16); memset(B.fired, 0, sizeof B.fired); B.banner_for = -1; }
    else if (B.stale) {
        BreakEvent old[16]; int old_fired[16], old_n = B.n;
        memcpy(old, B.ev, sizeof old); memcpy(old_fired, B.fired, sizeof old_fired);
        B.n = breaks_today(B.ev, 16);
        memset(B.fired, 0, sizeof B.fired);
        for (int i = 0; i < B.n; i++)
            for (int k = 0; k < old_n; k++) if (old_fired[k] && strcmp(old[k].name, B.ev[i].name) == 0) B.fired[i] = 1;
        B.banner_for = -1;
    }
    B.stale = 0;
}

void breaks_skip_today(void) { struct tm t = now_tm(); B.skipped_day = t.tm_yday + (t.tm_year << 9); B.banner_for = -1; }

void breaks_fire_now(const char *name, const char *detail, int kind)
{
    Screen *top = app_top();
    if (top && strcmp(top->name, "break") == 0) return;
    app_push(screen_break_create(name, detail, kind));
}

void breaks_tick(float dt)
{
    if (!g_app.breaks_on) { B.watch_s = 0; return; }
    refresh_day();
    Screen *top = app_top();
    int in_break = top && strcmp(top->name, "break") == 0;
    int skipped = B.skipped_day == B.day_key;
    /* stretch reminder: counts only while something plays */
    if (g_app.break_health_min > 0 && watching() && !in_break) {
        B.watch_s += dt;
        if (B.watch_s >= g_app.break_health_min * 60.0f && !skipped) { B.watch_s = 0; breaks_fire_now("Time to stretch", "Stand up, look away from the screen, drink some water", BRK_KIND_HEALTH); return; }
    } else if (!watching()) B.watch_s = 0;
    if (in_break || skipped) { B.banner_for = -1; return; }
    if (!watching() && !g_app.breaks_everywhere) { B.banner_for = -1; return; }
    struct tm t = now_tm();
    double now = t.tm_hour * 60 + t.tm_min + t.tm_sec / 60.0;
    B.banner_for = -1;
    for (int i = 0; i < B.n; i++) {
        BreakEvent *e = &B.ev[i];
        if (B.fired[i]) continue;
        if (now >= e->minute && now < e->minute + 3) {   /* due (a late start still counts for three minutes) */
            B.fired[i] = 1;
            breaks_fire_now(e->name, e->detail, e->kind);
            return;
        }
        if (now >= e->minute - g_app.break_lead_min && now < e->minute) { B.banner_for = i; app_redraw(); break; }
    }
}

void breaks_draw_banner(void)
{
    if (B.banner_for < 0 || B.banner_for >= B.n) return;
    const BreakEvent *e = &B.ev[B.banner_for];
    struct tm t = now_tm();
    double now = t.tm_hour * 60 + t.tm_min + t.tm_sec / 60.0;
    int secs = (int)((e->minute - now) * 60 + 0.5);
    if (secs < 0) secs = 0;
    char text[160];
    if (secs >= 90) snprintf(text, sizeof text, "%s in %d min", e->name, (secs + 30) / 60);
    else snprintf(text, sizeof text, "%s in %d s", e->name, secs);
    const char *sub = e->kind == BRK_KIND_BEDTIME ? "Playback will stop" : "Playback pauses for the break";
    float tw = font_measure(text, FONT_S | FONT_B), sw = font_measure(sub, FONT_XS);
    float w = (tw > sw ? tw : sw) + 120, h = 84, x = (SNTV_UI_W - w) / 2, y = 28;
    if (!g_app.fx_light) gfx_shadow(x, y + 8, w, h, 24, 30, RGBA(0, 0, 0, 0x90));
    ui_panel(x, y, w, h, R_PANEL, COLOR_ALPHA(THEME_PANEL, 0xF2));
    gfx_rrect_ring(x, y, w, h, R_PANEL, 1.5f, THEME_LINE);
    gfx_circle(x + 44, y + h / 2, 22, COLOR_ALPHA(THEME_ACCENT2, 0x40));
    ui_icon(ICON_CLOCK, x + 44, y + h / 2, 24, THEME_TEXT);
    font_draw(text, x + 84, y + 14, FONT_S | FONT_B, THEME_TEXT);
    font_draw(sub, x + 84, y + 48, FONT_XS, THEME_TEXT_MUTED);
    float frac = g_app.break_lead_min > 0 ? 1 - (float)secs / (g_app.break_lead_min * 60) : 1;
    ui_progress(x + 84, y + h - 8, w - 108, 3, frac, THEME_ACCENT);
    app_redraw();
}

/* ---- break recordings ---- */

static int ends_with_ci(const char *s, size_t n, const char *suf)
{
    size_t l = strlen(suf);
    return n >= l && strncasecmp(s + n - l, suf, l) == 0;
}

int breaks_is_page_link(const char *url)
{
    if (!url || strncmp(url, "http", 4) != 0) return 0;
    size_t n = strcspn(url, "?#");
    static const char *const MEDIA[] = { ".mp3", ".m4a", ".aac", ".mp4", ".m4v", ".ogg", ".oga", ".opus", ".wav", ".flac", ".webm", ".mkv", ".m3u8", ".mov", NULL };
    for (int i = 0; MEDIA[i]; i++) if (ends_with_ci(url, n, MEDIA[i])) return 0;
    return 1;
}

static int is_video_name(const char *url)
{
    size_t n = strcspn(url, "?#");
    return ends_with_ci(url, n, ".mp4") || ends_with_ci(url, n, ".m4v") || ends_with_ci(url, n, ".mkv") || ends_with_ci(url, n, ".webm") || ends_with_ci(url, n, ".mov") || ends_with_ci(url, n, ".m3u8");
}

const char *breaks_media_url(const char *stored, char *out, size_t n, int *is_video)
{
    if (is_video) *is_video = 0;
    if (!stored || !*stored) return NULL;
    if (torbox_is_link(stored)) {   /* your own video on TorBox: its permanent link, fresh behind */
        if (is_video) *is_video = g_app.break_video && is_video_name(stored);
        return torbox_resolve(stored, out, n);
    }
    if (strncmp(stored, "sntv-sound://", 13) == 0) {
        if (is_video) *is_video = g_app.break_video && is_video_name(stored);
        return phone_resolve_sound(stored, out, n);
    }
    if (breaks_is_page_link(stored)) {
        char name[80], sound[120]; int saved_video = 0;
        if (youtube_saved(stored, g_app.break_video, name, sizeof name, &saved_video)) {   /* downloaded on the TV */
            if (is_video) *is_video = g_app.break_video && saved_video;
            snprintf(sound, sizeof sound, "sntv-sound://%s", name);
            return phone_resolve_sound(sound, out, n);
        }
        if (!g_app.thumb_server || !*g_app.thumb_server) return NULL;
        char enc[1400]; size_t o = 0;
        for (const unsigned char *p = (const unsigned char *)stored; *p && o + 4 < sizeof enc; p++) {
            if ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') || strchr("-_.~", *p)) enc[o++] = (char)*p;
            else o += (size_t)snprintf(enc + o, sizeof enc - o, "%%%02X", *p);
        }
        enc[o] = 0;
        snprintf(out, n, "%s/media?kind=%s&url=%s", g_app.thumb_server, g_app.break_video ? "video" : "audio", enc);
        if (is_video) *is_video = g_app.break_video;
        return out;
    }
    if (is_video) *is_video = g_app.break_video && is_video_name(stored);
    return stored;
}
