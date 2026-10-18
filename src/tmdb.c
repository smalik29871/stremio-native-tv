#include "tmdb.h"
#include "app.h"
#include "http.h"
#include "log.h"
#include "../vendor/cJSON.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { SLOTS = 24 };
typedef struct { char imdb[24]; char type[12]; TmdbInfo info; unsigned stamp; int tmdb_id; } Slot;
static Slot g_slots[SLOTS];
static unsigned g_clock;

static const char *api_base(void) { const char *e = getenv("SNTV_TMDB_API"); return e && *e ? e : "https://api.themoviedb.org/3"; }
static const char *img_base(void) { const char *e = getenv("SNTV_TMDB_IMG"); return e && *e ? e : "https://image.tmdb.org/t/p/w185"; }

static const char *api_key(void)
{
    if (g_app.tmdb_key && g_app.tmdb_key[0]) return g_app.tmdb_key;
#ifdef SNTV_TMDB_BUILTIN_KEY
    return SNTV_TMDB_BUILTIN_KEY;
#else
    return NULL;
#endif
}
int tmdb_enabled(void) { return api_key() != NULL; }
int tmdb_builtin(void) {
#ifdef SNTV_TMDB_BUILTIN_KEY
    return 1;
#else
    return 0;
#endif
}

static Slot *slot_for(const char *imdb, const char *type)
{
    Slot *oldest = &g_slots[0];
    for (int i = 0; i < SLOTS; i++) {
        if (g_slots[i].imdb[0] && strcmp(g_slots[i].imdb, imdb) == 0 && strcmp(g_slots[i].type, type) == 0) { g_slots[i].stamp = ++g_clock; return &g_slots[i]; }
        if (!g_slots[i].imdb[0]) { oldest = &g_slots[i]; break; }
        if (g_slots[i].stamp < oldest->stamp) oldest = &g_slots[i];
    }
    memset(oldest, 0, sizeof *oldest);
    snprintf(oldest->imdb, sizeof oldest->imdb, "%s", imdb);
    snprintf(oldest->type, sizeof oldest->type, "%s", type);
    oldest->stamp = ++g_clock;
    return oldest;
}

static void parse_details(Slot *s, const char *json)
{
    cJSON *root = cJSON_Parse(json);
    if (!root) { s->info.state = 3; return; }
    int tv = strcmp(s->type, "series") == 0;
    /* cast */
    const cJSON *credits = cJSON_GetObjectItemCaseSensitive(root, tv ? "aggregate_credits" : "credits");
    const cJSON *cast = credits ? cJSON_GetObjectItemCaseSensitive(credits, "cast") : NULL;
    const cJSON *it;
    s->info.n_cast = 0;
    if (cJSON_IsArray(cast)) cJSON_ArrayForEach(it, cast) {
        if (s->info.n_cast >= 12) break;
        const cJSON *name = cJSON_GetObjectItemCaseSensitive(it, "name"), *photo = cJSON_GetObjectItemCaseSensitive(it, "profile_path");
        const cJSON *ch = cJSON_GetObjectItemCaseSensitive(it, "character");
        if (!cJSON_IsString(name)) continue;
        TmdbCast *c = &s->info.cast[s->info.n_cast++];
        snprintf(c->name, sizeof c->name, "%s", name->valuestring);
        if (cJSON_IsString(ch)) snprintf(c->character, sizeof c->character, "%s", ch->valuestring);
        else {   /* aggregate credits: roles[0].character */
            const cJSON *roles = cJSON_GetObjectItemCaseSensitive(it, "roles");
            const cJSON *r0 = cJSON_IsArray(roles) ? cJSON_GetArrayItem(roles, 0) : NULL;
            const cJSON *rc = r0 ? cJSON_GetObjectItemCaseSensitive(r0, "character") : NULL;
            if (cJSON_IsString(rc)) snprintf(c->character, sizeof c->character, "%s", rc->valuestring);
        }
        if (cJSON_IsString(photo) && photo->valuestring[0]) snprintf(c->photo, sizeof c->photo, "%s%s", img_base(), photo->valuestring);
    }
    /* age rating: US first, then GB, then whatever comes first */
    static const char *const PREF[] = { "US", "GB", NULL };
    const cJSON *ratings = cJSON_GetObjectItemCaseSensitive(root, tv ? "content_ratings" : "release_dates");
    const cJSON *results = ratings ? cJSON_GetObjectItemCaseSensitive(ratings, "results") : NULL;
    for (int pass = 0; pass < 3 && !s->info.cert[0] && cJSON_IsArray(results); pass++) {
        cJSON_ArrayForEach(it, results) {
            const cJSON *cc = cJSON_GetObjectItemCaseSensitive(it, "iso_3166_1");
            if (pass < 2 && !(cJSON_IsString(cc) && strcmp(cc->valuestring, PREF[pass]) == 0)) continue;
            if (tv) {
                const cJSON *r = cJSON_GetObjectItemCaseSensitive(it, "rating");
                if (cJSON_IsString(r) && r->valuestring[0]) { snprintf(s->info.cert, sizeof s->info.cert, "%s", r->valuestring); break; }
            } else {
                const cJSON *rds = cJSON_GetObjectItemCaseSensitive(it, "release_dates"), *rd;
                if (cJSON_IsArray(rds)) cJSON_ArrayForEach(rd, rds) {
                    const cJSON *c = cJSON_GetObjectItemCaseSensitive(rd, "certification");
                    if (cJSON_IsString(c) && c->valuestring[0]) { snprintf(s->info.cert, sizeof s->info.cert, "%s", c->valuestring); break; }
                }
                if (s->info.cert[0]) break;
            }
        }
    }
    cJSON_Delete(root);
    s->info.state = 2;
    LOGI("tmdb: %s -> %d cast, rating %s", s->imdb, s->info.n_cast, s->info.cert[0] ? s->info.cert : "none");
}

static void details_url(const Slot *s, int with_key, char *out, size_t n)
{
    int tv = strcmp(s->type, "series") == 0;
    snprintf(out, n, "%s/%s/%d?append_to_response=%s%s%s", api_base(), tv ? "tv" : "movie", s->tmdb_id,
             tv ? "aggregate_credits,content_ratings" : "credits,release_dates", with_key ? "&api_key=" : "", with_key ? api_key() : "");
}

static void on_details(HttpResponse *r, void *user)
{
    Slot *s = user;
    if (r->status == 200) {
        char key[512]; details_url(s, 0, key, sizeof key);
        app_cache_put(key, r->body, r->len);
        parse_details(s, r->body);
    } else { s->info.state = 3; LOGW("tmdb: details %ld %s", r->status, r->error); }
    app_redraw();
}

static void ask_details(Slot *s)
{
    char key[512], url[640];
    details_url(s, 0, key, sizeof key);
    char *cached = app_cache_get(key, 7 * 24 * 3600);
    if (cached) { parse_details(s, cached); free(cached); app_redraw(); return; }
    details_url(s, 1, url, sizeof url);
    http_request_async(url, NULL, on_details, s);
}

static int parse_find(const Slot *s, const char *json)
{
    cJSON *root = cJSON_Parse(json);
    if (!root) return 0;
    const cJSON *arr = cJSON_GetObjectItemCaseSensitive(root, strcmp(s->type, "series") == 0 ? "tv_results" : "movie_results");
    const cJSON *first = cJSON_IsArray(arr) ? cJSON_GetArrayItem(arr, 0) : NULL;
    const cJSON *id = first ? cJSON_GetObjectItemCaseSensitive(first, "id") : NULL;
    int v = cJSON_IsNumber(id) ? id->valueint : 0;
    cJSON_Delete(root);
    return v;
}

static void on_find(HttpResponse *r, void *user)
{
    Slot *s = user;
    if (r->status == 200 && (s->tmdb_id = parse_find(s, r->body)) > 0) {
        char key[256]; snprintf(key, sizeof key, "%s/find/%s", api_base(), s->imdb);
        app_cache_put(key, r->body, r->len);
        ask_details(s);
    } else { s->info.state = 3; app_redraw(); }
}

const TmdbInfo *tmdb_get(const char *imdb_id, const char *type)
{
    if (!tmdb_enabled() || !imdb_id || strncmp(imdb_id, "tt", 2) != 0) return NULL;
    const char *t = type && strcmp(type, "series") == 0 ? "series" : "movie";
    Slot *s = slot_for(imdb_id, t);
    if (s->info.state == 2) return &s->info;
    if (s->info.state != 0) return NULL;
    s->info.state = 1;
    char key[256], url[512];
    snprintf(key, sizeof key, "%s/find/%s", api_base(), imdb_id);
    char *cached = app_cache_get(key, 30 * 24 * 3600);
    if (cached && (s->tmdb_id = parse_find(s, cached)) > 0) { free(cached); ask_details(s); return NULL; }
    free(cached);
    snprintf(url, sizeof url, "%s/find/%s?external_source=imdb_id&api_key=%s", api_base(), imdb_id, api_key());
    http_request_async(url, NULL, on_find, s);
    return NULL;
}
