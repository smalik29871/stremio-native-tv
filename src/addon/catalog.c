#include "catalog.h"
#include "addon.h"
#include "../app.h"
#include "../log.h"
#include "../tmdb.h"
#include "../platform/platform.h"
#include "../../vendor/cJSON.h"
#include <SDL.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SCHEME "cuelight://catalog"
enum { PAGE = 20, LOOKUP_THREADS = 8, SEASONS_PER_ASK = 6 };

int catalog_handles(const char *url) { return url && !strncmp(url, SCHEME, strlen(SCHEME)); }

static const char *img_root(void) { const char *e = getenv("SNTV_TMDB_IMG"); return e && *e ? e : "https://image.tmdb.org/t/p"; }

static void img(char *out, size_t n, const char *size, const cJSON *path)
{
    if (cJSON_IsString(path) && path->valuestring[0]) snprintf(out, n, "%s/%s%s", img_root(), size, path->valuestring);
    else out[0] = 0;
}

typedef struct { int id; const char *name; } Genre;
static const Genre MOVIE_GENRES[] = {
    { 28, "Action" }, { 12, "Adventure" }, { 16, "Animation" }, { 35, "Comedy" }, { 80, "Crime" }, { 99, "Documentary" },
    { 18, "Drama" }, { 10751, "Family" }, { 14, "Fantasy" }, { 36, "History" }, { 27, "Horror" }, { 10402, "Music" },
    { 9648, "Mystery" }, { 10749, "Romance" }, { 878, "Science Fiction" }, { 53, "Thriller" }, { 10752, "War" }, { 37, "Western" }, { 0, NULL },
};
static const Genre TV_GENRES[] = {
    { 10759, "Action & Adventure" }, { 16, "Animation" }, { 35, "Comedy" }, { 80, "Crime" }, { 99, "Documentary" }, { 18, "Drama" },
    { 10751, "Family" }, { 10762, "Kids" }, { 9648, "Mystery" }, { 10764, "Reality" }, { 10765, "Sci-Fi & Fantasy" },
    { 10768, "War & Politics" }, { 37, "Western" }, { 0, NULL },
};
static const char *genre_name(int tv, int id)
{
    for (const Genre *g = tv ? TV_GENRES : MOVIE_GENRES; g->name; g++) if (g->id == id) return g->name;
    return NULL;
}
static int genre_id(int tv, const char *name)
{
    for (const Genre *g = tv ? TV_GENRES : MOVIE_GENRES; g->name; g++) if (!strcmp(g->name, name)) return g->id;
    return 0;
}

/* ---- TMDB on this thread --------------------------------------------------------- */

/* path_q: "/movie/popular?page=1". The key goes on the request only: cache names and logs
 * never carry it. max_age < 0: not cached; 0: kept for good. */
static cJSON *tmdb_json(const char *path_q, long max_age)
{
    char key[96], cache_key[1200], url[1400];
    if (!tmdb_key_copy(key, sizeof key)) return NULL;
    snprintf(cache_key, sizeof cache_key, "%s%s", tmdb_api_base(), path_q);
    if (max_age >= 0) {
        char *hit = app_cache_get(cache_key, max_age);
        cJSON *j = hit ? cJSON_Parse(hit) : NULL;
        free(hit);
        if (j) return j;
    }
    snprintf(url, sizeof url, "%s%s%sapi_key=%s", tmdb_api_base(), path_q, strchr(path_q, '?') ? "&" : "?", key);
    HttpResponse r;
    http_request(url, NULL, &r);
    cJSON *j = r.status == 200 ? cJSON_Parse(r.body) : NULL;
    if (j && max_age >= 0) app_cache_put(cache_key, r.body, r.len);
    if (!j) LOGW("catalog: TMDB %.*s answered %ld %s", (int)strcspn(path_q, "?"), path_q, r.status, r.status ? "" : r.error);
    http_response_free(&r);
    return j;
}

/* ---- IMDb ids for TMDB's titles (what stream addons look titles up by) ---------------- */

typedef struct { int tv, id; char imdb[24]; } Lookup;

static int lookup_main(void *arg)
{
    Lookup *l = arg;
    char path[96];
    snprintf(path, sizeof path, "/%s/%d/external_ids", l->tv ? "tv" : "movie", l->id);
    cJSON *j = tmdb_json(path, 0);   /* an id's IMDb id never changes */
    const cJSON *im = cJSON_GetObjectItemCaseSensitive(j, "imdb_id");
    if (cJSON_IsString(im) && !strncmp(im->valuestring, "tt", 2)) snprintf(l->imdb, sizeof l->imdb, "%s", im->valuestring);
    cJSON_Delete(j);
    return 0;
}

/* Several at once: twenty in a row would be two seconds on a TV's network. */
static void lookup_all(Lookup *l, int n)
{
    for (int at = 0; at < n; at += LOOKUP_THREADS) {
        SDL_Thread *t[LOOKUP_THREADS] = { 0 };
        int k = n - at < LOOKUP_THREADS ? n - at : LOOKUP_THREADS;
        for (int i = 0; i < k; i++) if (!(t[i] = SDL_CreateThread(lookup_main, "catalog", &l[at + i]))) lookup_main(&l[at + i]);
        for (int i = 0; i < k; i++) if (t[i]) SDL_WaitThread(t[i], NULL);
    }
}

/* ---- answers ----------------------------------------------------------------------- */

static void add_extra(cJSON *cat, const char *name, const Genre *options)
{
    cJSON *extra = cJSON_GetObjectItemCaseSensitive(cat, "extra");
    if (!extra) extra = cJSON_AddArrayToObject(cat, "extra");
    cJSON *e = cJSON_CreateObject();
    cJSON_AddStringToObject(e, "name", name);
    if (options) {
        cJSON *o = cJSON_AddArrayToObject(e, "options");
        for (const Genre *g = options; g->name; g++) cJSON_AddItemToArray(o, cJSON_CreateString(g->name));
    }
    cJSON_AddItemToArray(extra, e);
}

static char *manifest(void)
{
    cJSON *m = cJSON_CreateObject();
    cJSON_AddStringToObject(m, "id", "org.cuelight.catalog");
    cJSON_AddStringToObject(m, "version", SNTV_APP_VERSION);
    cJSON_AddStringToObject(m, "name", "Cuelight");
    cJSON_AddStringToObject(m, "description", "Films and series from TMDB, built into Cuelight");
    cJSON *res = cJSON_AddArrayToObject(m, "resources");
    cJSON_AddItemToArray(res, cJSON_CreateString("catalog"));
    cJSON_AddItemToArray(res, cJSON_CreateString("meta"));
    cJSON *types = cJSON_AddArrayToObject(m, "types");
    cJSON_AddItemToArray(types, cJSON_CreateString("movie"));
    cJSON_AddItemToArray(types, cJSON_CreateString("series"));
    cJSON *pre = cJSON_AddArrayToObject(m, "idPrefixes");
    cJSON_AddItemToArray(pre, cJSON_CreateString("tt"));
    cJSON *cats = cJSON_AddArrayToObject(m, "catalogs");
    static const char *const TYPES[] = { "movie", "series" };
    for (int pass = 0; pass < 2; pass++) for (int t = 0; t < 2; t++) {
        cJSON *c = cJSON_CreateObject();
        cJSON_AddStringToObject(c, "type", TYPES[t]);
        cJSON_AddStringToObject(c, "id", pass ? "trending" : "popular");
        cJSON_AddStringToObject(c, "name", pass ? "Trending" : "Popular");
        if (!pass) { add_extra(c, "genre", t ? TV_GENRES : MOVIE_GENRES); add_extra(c, "search", NULL); }
        add_extra(c, "skip", NULL);
        cJSON_AddItemToArray(cats, c);
    }
    char *txt = cJSON_PrintUnformatted(m);
    cJSON_Delete(m);
    return txt;
}

static void url_decode(char *s)
{
    char *o = s;
    for (; *s; s++) {
        if (*s == '%' && isxdigit((unsigned char)s[1]) && isxdigit((unsigned char)s[2])) { char h[3] = { s[1], s[2], 0 }; *o++ = (char)strtol(h, NULL, 16); s += 2; }
        else *o++ = *s == '+' ? ' ' : *s;
    }
    *o = 0;
}

/* "genre=Action&skip=40" -> the value of one name, decoded */
static int extra_value(const char *extra, const char *name, char *out, size_t n)
{
    size_t len = strlen(name);
    for (const char *p = extra; p && *p; ) {
        size_t seg = strcspn(p, "&");
        if (seg > len && !strncmp(p, name, len) && p[len] == '=') {
            snprintf(out, n, "%.*s", (int)(seg - len - 1), p + len + 1);
            url_decode(out);
            return 1;
        }
        p += seg + (p[seg] == '&');
    }
    out[0] = 0;
    return 0;
}

static void year_of(const cJSON *o, int tv, char *out, size_t n)
{
    const cJSON *d = cJSON_GetObjectItemCaseSensitive(o, tv ? "first_air_date" : "release_date");
    snprintf(out, n, "%.4s", cJSON_IsString(d) ? d->valuestring : "");
}

static char *metas(const char *type, const char *id, const char *extra)
{
    int tv = !strcmp(type, "series");
    char search[200], genre[64], skip[16], path[600], enc[400];
    extra_value(extra, "search", search, sizeof search);
    extra_value(extra, "genre", genre, sizeof genre);
    extra_value(extra, "skip", skip, sizeof skip);
    int page = atoi(skip) / PAGE + 1;
    long age = 3 * 3600;
    if (search[0]) {
        http_url_encode(search, enc, sizeof enc);
        snprintf(path, sizeof path, "/search/%s?query=%s&page=%d&include_adult=false", tv ? "tv" : "movie", enc, page);
        age = 24 * 3600;
    } else if (!strcmp(id, "trending")) {
        snprintf(path, sizeof path, "/trending/%s/week?page=%d", tv ? "tv" : "movie", page);
    } else {
        int g = genre[0] ? genre_id(tv, genre) : 0;
        /* TMDB's own "popular" lists are full of talk shows, news and the unreleased */
        int n = snprintf(path, sizeof path, "/discover/%s?sort_by=popularity.desc&include_adult=false&vote_count.gte=%d&page=%d",
                         tv ? "tv" : "movie", tv ? 100 : 200, page);
        if (tv) n += snprintf(path + n, sizeof path - (size_t)n, "&without_genres=10767,10763,10766");
        if (g) snprintf(path + n, sizeof path - (size_t)n, "&with_genres=%d", g);
    }
    cJSON *j = tmdb_json(path, age);
    const cJSON *results = cJSON_GetObjectItemCaseSensitive(j, "results");
    int n = cJSON_IsArray(results) ? cJSON_GetArraySize(results) : 0;
    Lookup *l = calloc((size_t)n + 1, sizeof *l);
    for (int i = 0; i < n; i++) {
        const cJSON *rid = cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(results, i), "id");
        l[i].tv = tv; l[i].id = cJSON_IsNumber(rid) ? rid->valueint : 0;
    }
    lookup_all(l, n);
    cJSON *root = cJSON_CreateObject();
    cJSON *arr = cJSON_AddArrayToObject(root, "metas");
    for (int i = 0; i < n; i++) {
        const cJSON *o = cJSON_GetArrayItem(results, i);
        const cJSON *name = cJSON_GetObjectItemCaseSensitive(o, tv ? "name" : "title");
        if (!l[i].imdb[0] || !cJSON_IsString(name)) continue;   /* no IMDb id: no addon would find a stream for it */
        cJSON *m = cJSON_CreateObject();
        char buf[400], year[8];
        cJSON_AddStringToObject(m, "id", l[i].imdb);
        cJSON_AddStringToObject(m, "type", type);
        cJSON_AddStringToObject(m, "name", name->valuestring);
        img(buf, sizeof buf, "w342", cJSON_GetObjectItemCaseSensitive(o, "poster_path"));
        if (buf[0]) cJSON_AddStringToObject(m, "poster", buf);
        img(buf, sizeof buf, "w1280", cJSON_GetObjectItemCaseSensitive(o, "backdrop_path"));
        if (buf[0]) cJSON_AddStringToObject(m, "background", buf);
        const cJSON *ov = cJSON_GetObjectItemCaseSensitive(o, "overview");
        if (cJSON_IsString(ov) && ov->valuestring[0]) cJSON_AddStringToObject(m, "description", ov->valuestring);
        year_of(o, tv, year, sizeof year);
        if (year[0]) cJSON_AddStringToObject(m, "releaseInfo", year);
        const cJSON *gids = cJSON_GetObjectItemCaseSensitive(o, "genre_ids"), *g;
        cJSON *genres = cJSON_AddArrayToObject(m, "genres");
        cJSON_ArrayForEach(g, gids) { const char *gn = cJSON_IsNumber(g) ? genre_name(tv, g->valueint) : NULL; if (gn) cJSON_AddItemToArray(genres, cJSON_CreateString(gn)); }
        cJSON_AddItemToArray(arr, m);
    }
    free(l);
    int ok = j != NULL;
    cJSON_Delete(j);
    if (!ok) { cJSON_Delete(root); return NULL; }
    LOGI("catalog: %s %s%s%s page %d -> %d titles", type, search[0] ? "search" : id, genre[0] ? ", " : "", genre, page, cJSON_GetArraySize(arr));
    char *txt = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return txt;
}

static int tmdb_id_for(const char *imdb, int tv)
{
    char path[96];
    snprintf(path, sizeof path, "/find/%s?external_source=imdb_id", imdb);
    cJSON *j = tmdb_json(path, 30L * 24 * 3600);
    const cJSON *arr = cJSON_GetObjectItemCaseSensitive(j, tv ? "tv_results" : "movie_results");
    const cJSON *id = cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(arr, 0), "id");
    int v = cJSON_IsNumber(id) ? id->valueint : 0;
    cJSON_Delete(j);
    return v;
}

static void add_names(cJSON *meta, const char *key, const cJSON *crew, const char *const *jobs)
{
    cJSON *arr = NULL;
    const cJSON *c;
    cJSON_ArrayForEach(c, crew) {
        const cJSON *job = cJSON_GetObjectItemCaseSensitive(c, "job"), *nm = cJSON_GetObjectItemCaseSensitive(c, "name");
        if (!cJSON_IsString(job) || !cJSON_IsString(nm)) continue;
        for (int i = 0; jobs[i]; i++) if (!strcmp(job->valuestring, jobs[i])) {
            if (!arr) arr = cJSON_AddArrayToObject(meta, key);
            if (cJSON_GetArraySize(arr) < 4) cJSON_AddItemToArray(arr, cJSON_CreateString(nm->valuestring));
            break;
        }
    }
}

/* The cast the title page shows with pictures (app_extras.cast, as the official apps get) */
static void add_cast(cJSON *meta, const cJSON *cast, int tv)
{
    cJSON *names = cJSON_AddArrayToObject(meta, "cast");
    cJSON *ax = cJSON_AddObjectToObject(meta, "app_extras");
    cJSON *rich = cJSON_AddArrayToObject(ax, "cast");
    const cJSON *c;
    cJSON_ArrayForEach(c, cast) {
        if (cJSON_GetArraySize(rich) >= 12) break;
        const cJSON *nm = cJSON_GetObjectItemCaseSensitive(c, "name");
        if (!cJSON_IsString(nm)) continue;
        const cJSON *ch = cJSON_GetObjectItemCaseSensitive(c, "character");
        if (tv) { const cJSON *roles = cJSON_GetObjectItemCaseSensitive(c, "roles"); ch = cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(roles, 0), "character"); }
        char photo[400];
        img(photo, sizeof photo, "w185", cJSON_GetObjectItemCaseSensitive(c, "profile_path"));
        if (cJSON_GetArraySize(names) < 6) cJSON_AddItemToArray(names, cJSON_CreateString(nm->valuestring));
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "name", nm->valuestring);
        cJSON_AddStringToObject(o, "character", cJSON_IsString(ch) ? ch->valuestring : "");
        cJSON_AddStringToObject(o, "photo", photo);
        cJSON_AddItemToArray(rich, o);
    }
}

static void add_episodes(cJSON *meta, const char *imdb, int tmdb_id, const cJSON *seasons)
{
    cJSON *videos = cJSON_AddArrayToObject(meta, "videos");
    int nums[200], n = 0;
    const cJSON *s;
    cJSON_ArrayForEach(s, seasons) {
        const cJSON *sn = cJSON_GetObjectItemCaseSensitive(s, "season_number");
        if (cJSON_IsNumber(sn) && n < 200) nums[n++] = sn->valueint;
    }
    for (int at = 0; at < n; at += SEASONS_PER_ASK) {   /* several seasons in one answer (TMDB's append_to_response) */
        char path[400];
        int len = snprintf(path, sizeof path, "/tv/%d?append_to_response=", tmdb_id);
        for (int k = at; k < n && k < at + SEASONS_PER_ASK; k++) len += snprintf(path + len, sizeof path - (size_t)len, "%sseason/%d", k > at ? "," : "", nums[k]);
        cJSON *j = tmdb_json(path, -1);
        for (int k = at; j && k < n && k < at + SEASONS_PER_ASK; k++) {
            char key[24];
            snprintf(key, sizeof key, "season/%d", nums[k]);
            const cJSON *eps = cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(j, key), "episodes"), *e;
            cJSON_ArrayForEach(e, eps) {
                const cJSON *en = cJSON_GetObjectItemCaseSensitive(e, "episode_number"), *nm = cJSON_GetObjectItemCaseSensitive(e, "name");
                const cJSON *air = cJSON_GetObjectItemCaseSensitive(e, "air_date"), *ov = cJSON_GetObjectItemCaseSensitive(e, "overview");
                if (!cJSON_IsNumber(en)) continue;
                char id[48], when[40], still[400];
                snprintf(id, sizeof id, "%s:%d:%d", imdb, nums[k], en->valueint);
                cJSON *v = cJSON_CreateObject();
                cJSON_AddStringToObject(v, "id", id);
                if (cJSON_IsString(nm)) cJSON_AddStringToObject(v, "title", nm->valuestring);
                cJSON_AddNumberToObject(v, "season", nums[k]);
                cJSON_AddNumberToObject(v, "episode", en->valueint);
                if (cJSON_IsString(air) && air->valuestring[0]) { snprintf(when, sizeof when, "%.10sT00:00:00.000Z", air->valuestring); cJSON_AddStringToObject(v, "released", when); }
                img(still, sizeof still, "w300", cJSON_GetObjectItemCaseSensitive(e, "still_path"));
                if (still[0]) cJSON_AddStringToObject(v, "thumbnail", still);
                if (cJSON_IsString(ov) && ov->valuestring[0]) cJSON_AddStringToObject(v, "overview", ov->valuestring);
                cJSON_AddItemToArray(videos, v);
            }
        }
        cJSON_Delete(j);
    }
}

static char *meta(const char *type, const char *imdb)
{
    int tv = !strcmp(type, "series");
    int tid = strncmp(imdb, "tt", 2) ? 0 : tmdb_id_for(imdb, tv);
    if (!tid) return NULL;
    char path[300];
    snprintf(path, sizeof path, "/%s/%d?append_to_response=%s&include_image_language=en,null", tv ? "tv" : "movie", tid,
             tv ? "aggregate_credits,videos,images" : "credits,videos,images");
    cJSON *j = tmdb_json(path, 12 * 3600);
    const cJSON *name = cJSON_GetObjectItemCaseSensitive(j, tv ? "name" : "title");
    if (!cJSON_IsString(name)) { cJSON_Delete(j); return NULL; }
    cJSON *root = cJSON_CreateObject();
    cJSON *m = cJSON_AddObjectToObject(root, "meta");
    char buf[400], year[8];
    cJSON_AddStringToObject(m, "id", imdb);
    cJSON_AddStringToObject(m, "type", type);
    cJSON_AddStringToObject(m, "name", name->valuestring);
    img(buf, sizeof buf, "w500", cJSON_GetObjectItemCaseSensitive(j, "poster_path"));
    if (buf[0]) cJSON_AddStringToObject(m, "poster", buf);
    img(buf, sizeof buf, "w1280", cJSON_GetObjectItemCaseSensitive(j, "backdrop_path"));
    if (buf[0]) cJSON_AddStringToObject(m, "background", buf);
    const cJSON *logos = cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(j, "images"), "logos"), *lg, *logo = NULL;
    cJSON_ArrayForEach(lg, logos) {   /* English first, then one without words */
        const cJSON *lang = cJSON_GetObjectItemCaseSensitive(lg, "iso_639_1");
        if (cJSON_IsString(lang) && !strcmp(lang->valuestring, "en")) { logo = lg; break; }
        if (!logo) logo = lg;
    }
    img(buf, sizeof buf, "w500", cJSON_GetObjectItemCaseSensitive(logo, "file_path"));
    if (buf[0]) cJSON_AddStringToObject(m, "logo", buf);
    const cJSON *ov = cJSON_GetObjectItemCaseSensitive(j, "overview");
    if (cJSON_IsString(ov) && ov->valuestring[0]) cJSON_AddStringToObject(m, "description", ov->valuestring);
    year_of(j, tv, year, sizeof year);
    if (year[0]) {
        cJSON_AddStringToObject(m, "year", year);
        if (tv) {
            const cJSON *st = cJSON_GetObjectItemCaseSensitive(j, "status"), *last = cJSON_GetObjectItemCaseSensitive(j, "last_air_date");
            int ended = cJSON_IsString(st) && (!strcmp(st->valuestring, "Ended") || !strcmp(st->valuestring, "Canceled"));
            snprintf(buf, sizeof buf, "%s-%.4s", year, ended && cJSON_IsString(last) ? last->valuestring : "");
            cJSON_AddStringToObject(m, "releaseInfo", buf);
        } else cJSON_AddStringToObject(m, "releaseInfo", year);
        const cJSON *d = cJSON_GetObjectItemCaseSensitive(j, tv ? "first_air_date" : "release_date");
        snprintf(buf, sizeof buf, "%.10sT00:00:00.000Z", d->valuestring);
        cJSON_AddStringToObject(m, "released", buf);
    }
    const cJSON *rt = cJSON_GetObjectItemCaseSensitive(j, "runtime");
    if (tv) rt = cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(j, "episode_run_time"), 0);
    if (cJSON_IsNumber(rt) && rt->valueint > 0) { snprintf(buf, sizeof buf, "%d min", rt->valueint); cJSON_AddStringToObject(m, "runtime", buf); }
    cJSON *genres = cJSON_AddArrayToObject(m, "genres");
    const cJSON *g;
    cJSON_ArrayForEach(g, cJSON_GetObjectItemCaseSensitive(j, "genres")) {
        const cJSON *gn = cJSON_GetObjectItemCaseSensitive(g, "name");
        if (cJSON_IsString(gn)) cJSON_AddItemToArray(genres, cJSON_CreateString(gn->valuestring));
    }
    const cJSON *credits = cJSON_GetObjectItemCaseSensitive(j, tv ? "aggregate_credits" : "credits");
    add_cast(m, cJSON_GetObjectItemCaseSensitive(credits, "cast"), tv);
    static const char *const DIRECTORS[] = { "Director", NULL }, *const WRITERS[] = { "Screenplay", "Writer", "Story", NULL };
    if (!tv) {
        add_names(m, "director", cJSON_GetObjectItemCaseSensitive(credits, "crew"), DIRECTORS);
        add_names(m, "writer", cJSON_GetObjectItemCaseSensitive(credits, "crew"), WRITERS);
    } else {
        cJSON *creators = NULL;
        cJSON_ArrayForEach(g, cJSON_GetObjectItemCaseSensitive(j, "created_by")) {
            const cJSON *nm = cJSON_GetObjectItemCaseSensitive(g, "name");
            if (!cJSON_IsString(nm)) continue;
            if (!creators) creators = cJSON_AddArrayToObject(m, "writer");
            cJSON_AddItemToArray(creators, cJSON_CreateString(nm->valuestring));
        }
    }
    const cJSON *country = cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(j, "production_countries"), 0), "name");
    if (cJSON_IsString(country)) cJSON_AddStringToObject(m, "country", country->valuestring);
    const cJSON *vids = cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(j, "videos"), "results"), *v, *trailer = NULL;
    cJSON_ArrayForEach(v, vids) {   /* an official YouTube trailer, else any trailer there */
        const cJSON *site = cJSON_GetObjectItemCaseSensitive(v, "site"), *ty = cJSON_GetObjectItemCaseSensitive(v, "type");
        if (!cJSON_IsString(site) || strcmp(site->valuestring, "YouTube") || !cJSON_IsString(ty) || strcmp(ty->valuestring, "Trailer")) continue;
        if (!trailer || cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(v, "official"))) trailer = v;
        if (cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(v, "official"))) break;
    }
    const cJSON *yt = cJSON_GetObjectItemCaseSensitive(trailer, "key");
    if (cJSON_IsString(yt)) {
        cJSON *ts = cJSON_AddArrayToObject(m, "trailerStreams"), *t = cJSON_CreateObject();
        cJSON_AddStringToObject(t, "title", "Trailer");
        cJSON_AddStringToObject(t, "ytId", yt->valuestring);
        cJSON_AddItemToArray(ts, t);
    }
    if (tv) add_episodes(m, imdb, tid, cJSON_GetObjectItemCaseSensitive(j, "seasons"));
    else { cJSON *bh = cJSON_AddObjectToObject(m, "behaviorHints"); cJSON_AddStringToObject(bh, "defaultVideoId", imdb); }
    LOGI("catalog: %s %s -> %s%s, %d cast, %d episodes", type, imdb, name->valuestring, cJSON_GetObjectItemCaseSensitive(m, "trailerStreams") ? ", trailer" : "",
         cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(m, "app_extras"), "cast")), cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(m, "videos")));
    cJSON_Delete(j);
    char *txt = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return txt;
}

/* A title's poster, background or logo by IMDb id: the picture itself, from TMDB */
static int art(const char *kind, const char *imdb, HttpResponse *out)
{
    char path[160], file[300] = "";
    snprintf(path, sizeof path, "/find/%s?external_source=imdb_id", imdb);
    cJSON *j = tmdb_json(path, 30L * 24 * 3600);
    const cJSON *hit = cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(j, "movie_results"), 0);
    int tv = 0;
    if (!hit) { hit = cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(j, "tv_results"), 0); tv = 1; }
    const cJSON *id = cJSON_GetObjectItemCaseSensitive(hit, "id");
    const char *size = !strcmp(kind, "poster") ? "w342" : !strcmp(kind, "logo") ? "w500" : "w1280";
    if (!strcmp(kind, "logo") && cJSON_IsNumber(id)) {
        snprintf(path, sizeof path, "/%s/%d/images?include_image_language=en,null", tv ? "tv" : "movie", id->valueint);
        cJSON *im = tmdb_json(path, 30L * 24 * 3600);
        const cJSON *lg, *pick = NULL;
        cJSON_ArrayForEach(lg, cJSON_GetObjectItemCaseSensitive(im, "logos")) {
            const cJSON *lang = cJSON_GetObjectItemCaseSensitive(lg, "iso_639_1");
            if (cJSON_IsString(lang) && !strcmp(lang->valuestring, "en")) { pick = lg; break; }
            if (!pick) pick = lg;
        }
        img(file, sizeof file, size, cJSON_GetObjectItemCaseSensitive(pick, "file_path"));
        cJSON_Delete(im);
    } else if (hit) img(file, sizeof file, size, cJSON_GetObjectItemCaseSensitive(hit, !strcmp(kind, "poster") ? "poster_path" : "backdrop_path"));
    cJSON_Delete(j);
    if (!file[0]) return 0;
    HttpOptions o = { .no_json_accept = 1 };
    http_request_ex(file, &o, out);
    LOGI("catalog: %s for %s from TMDB (%ld)", kind, imdb, out->status);
    return out->status == 200;
}

int catalog_answer(const char *url, HttpResponse *out)
{
    memset(out, 0, sizeof *out);
    Uint32 t0 = SDL_GetTicks();
    char path[1024], type[16] = "", id[256] = "", extra[700] = "";
    snprintf(path, sizeof path, "%s", url + strlen(SCHEME));
    size_t pl = strlen(path);
    if (pl > 5 && !strcmp(path + pl - 5, ".json")) path[pl - 5] = 0;
    char *body = NULL;
    if (!strncmp(path, "/art/", 5) && sscanf(path + 5, "%15[^/]/%255s", type, id) == 2) {   /* a picture: TMDB's bytes as they are */
        if (art(type, id, out)) return 0;
        http_response_free(out);
        memset(out, 0, sizeof *out);
        out->status = 404; out->body = calloc(1, 1);
        snprintf(out->error, sizeof out->error, "no %s on TMDB", type);
        return 0;
    }
    if (!strcmp(path, "/manifest")) body = manifest();
    else if (!strncmp(path, "/catalog/", 9) && sscanf(path + 9, "%15[^/]/%255[^/]/%699s", type, id, extra) >= 2) body = metas(type, id, extra);
    else if (!strncmp(path, "/meta/", 6) && sscanf(path + 6, "%15[^/]/%255s", type, id) == 2) { url_decode(id); body = meta(type, id); }
    out->seconds = (SDL_GetTicks() - t0) / 1000.0;
    if (!body) {
        out->status = 404;
        char key[96];
        snprintf(out->error, sizeof out->error, "%s", tmdb_key_copy(key, sizeof key) ? "TMDB did not answer" : "no TMDB key");
        out->body = calloc(1, 1);
        return 0;
    }
    out->status = 200;
    out->body = body;
    out->len = strlen(body);
    snprintf(out->content_type, sizeof out->content_type, "application/json");
    return 0;
}
