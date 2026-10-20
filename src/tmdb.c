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
        const cJSON *pid = cJSON_GetObjectItemCaseSensitive(it, "id");
        c->person_id = cJSON_IsNumber(pid) ? pid->valueint : 0;
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
    const cJSON *bc = tv ? NULL : cJSON_GetObjectItemCaseSensitive(root, "belongs_to_collection");
    const cJSON *bid = bc ? cJSON_GetObjectItemCaseSensitive(bc, "id") : NULL, *bname = bc ? cJSON_GetObjectItemCaseSensitive(bc, "name") : NULL;
    if (cJSON_IsNumber(bid) && bid->valueint > 0) {
        s->info.collection_id = bid->valueint;
        snprintf(s->info.collection, sizeof s->info.collection, "%s", cJSON_IsString(bname) ? bname->valuestring : "");
    }
    /* trailers, as Nuvio takes them when the addon lists none: YouTube, type Trailer, official first */
    const cJSON *vids = cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(root, "videos"), "results"), *v;
    for (int pass = 0; pass < 2 && !s->info.trailer_yt[0]; pass++)
        if (cJSON_IsArray(vids)) cJSON_ArrayForEach(v, vids) {
            const cJSON *site = cJSON_GetObjectItemCaseSensitive(v, "site"), *type = cJSON_GetObjectItemCaseSensitive(v, "type");
            const cJSON *key = cJSON_GetObjectItemCaseSensitive(v, "key"), *off = cJSON_GetObjectItemCaseSensitive(v, "official");
            if (!cJSON_IsString(site) || strcmp(site->valuestring, "YouTube") || !cJSON_IsString(type) || strcmp(type->valuestring, "Trailer")) continue;
            if (!cJSON_IsString(key) || strlen(key->valuestring) != 11 || (pass == 0 && !cJSON_IsTrue(off))) continue;
            snprintf(s->info.trailer_yt, sizeof s->info.trailer_yt, "%s", key->valuestring);
            break;
        }
    const cJSON *kws = cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(root, "keywords"), "keywords"), *kw;
    s->info.stinger = 0;
    if (!tv && cJSON_IsArray(kws)) cJSON_ArrayForEach(kw, kws) {
        const cJSON *id = cJSON_GetObjectItemCaseSensitive(kw, "id");
        if (cJSON_IsNumber(id) && id->valueint == 179431) s->info.stinger |= 1;   /* "duringcreditsstinger" */
        if (cJSON_IsNumber(id) && id->valueint == 179430) s->info.stinger |= 2;   /* "aftercreditsstinger" */
    }
    s->info.tmdb_id = s->tmdb_id;
    cJSON_Delete(root);
    s->info.state = 2;
    LOGI("tmdb: %s -> %d cast, rating %s", s->imdb, s->info.n_cast, s->info.cert[0] ? s->info.cert : "none");
}

static void details_url(const Slot *s, int with_key, char *out, size_t n)
{
    int tv = strcmp(s->type, "series") == 0;
    snprintf(out, n, "%s/%s/%d?append_to_response=%s%s%s", api_base(), tv ? "tv" : "movie", s->tmdb_id,
             tv ? "aggregate_credits,content_ratings,videos" : "credits,release_dates,videos,keywords", with_key ? "&api_key=" : "", with_key ? api_key() : "");
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

/* ---- collections ---- */
static TmdbCollection g_coll[4];
static unsigned g_coll_stamp[4];

static int part_cmp(const void *a, const void *b)
{
    const TmdbPart *x = a, *y = b;
    if (!x->year[0] != !y->year[0]) return x->year[0] ? -1 : 1;   /* unreleased last */
    return strcmp(x->year, y->year);
}

static void parse_collection(TmdbCollection *c, const char *json)
{
    cJSON *root = cJSON_Parse(json);
    const cJSON *parts = root ? cJSON_GetObjectItemCaseSensitive(root, "parts") : NULL, *it;
    const cJSON *name = root ? cJSON_GetObjectItemCaseSensitive(root, "name") : NULL;
    char dates[16][12] = {{0}};
    c->n = 0;
    if (cJSON_IsString(name)) snprintf(c->name, sizeof c->name, "%s", name->valuestring);
    if (cJSON_IsArray(parts)) cJSON_ArrayForEach(it, parts) {
        if (c->n >= 16) break;
        const cJSON *id = cJSON_GetObjectItemCaseSensitive(it, "id"), *t = cJSON_GetObjectItemCaseSensitive(it, "title");
        const cJSON *rd = cJSON_GetObjectItemCaseSensitive(it, "release_date"), *pp = cJSON_GetObjectItemCaseSensitive(it, "poster_path");
        if (!cJSON_IsNumber(id) || !cJSON_IsString(t)) continue;
        TmdbPart *pt = &c->parts[c->n];
        memset(pt, 0, sizeof *pt);
        pt->tmdb_id = id->valueint;
        snprintf(pt->title, sizeof pt->title, "%s", t->valuestring);
        /* the full date sorts; the year shows */
        if (cJSON_IsString(rd) && strlen(rd->valuestring) >= 4) snprintf(dates[c->n], sizeof dates[c->n], "%.10s", rd->valuestring);
        if (cJSON_IsString(pp) && pp->valuestring[0]) snprintf(pt->poster, sizeof pt->poster, "%s%s", img_base(), pp->valuestring);
        c->n++;
    }
    /* sort by date, carrying the full date in year[] for the sort, then cut it to the year */
    for (int i = 0; i < c->n; i++) snprintf(c->parts[i].year, sizeof c->parts[i].year, "%.7s", dates[i]);
    qsort(c->parts, (size_t)c->n, sizeof c->parts[0], part_cmp);
    for (int i = 0; i < c->n; i++) c->parts[i].year[4] = 0;
    cJSON_Delete(root);
    c->state = c->n >= 2 ? 2 : 3;
    LOGI("tmdb: collection %d \"%s\" has %d films", c->id, c->name, c->n);
}

static void coll_key(int id, char *out, size_t n) { snprintf(out, n, "%s/collection/%d", api_base(), id); }

static void on_collection(HttpResponse *r, void *user)
{
    TmdbCollection *c = user;
    if (r->status == 200) {
        char key[256]; coll_key(c->id, key, sizeof key);
        app_cache_put(key, r->body, r->len);
        parse_collection(c, r->body);
    } else c->state = 3;
    app_redraw();
}

const TmdbCollection *tmdb_collection(int id)
{
    if (!tmdb_enabled() || id <= 0) return NULL;
    int slot = 0;
    for (int i = 0; i < 4; i++) {
        if (g_coll[i].id == id) { g_coll_stamp[i] = ++g_clock; return g_coll[i].state == 2 ? &g_coll[i] : NULL; }
        if (g_coll_stamp[i] < g_coll_stamp[slot]) slot = i;
    }
    TmdbCollection *c = &g_coll[slot];
    memset(c, 0, sizeof *c);
    c->id = id; c->state = 1; g_coll_stamp[slot] = ++g_clock;
    char key[256], url[512];
    coll_key(id, key, sizeof key);
    char *cached = app_cache_get(key, 7 * 24 * 3600);
    if (cached) { parse_collection(c, cached); free(cached); return c->state == 2 ? c : NULL; }
    snprintf(url, sizeof url, "%s?api_key=%s", key, api_key());
    http_request_async(url, NULL, on_collection, c);
    return NULL;
}

typedef struct { TmdbImdbCb cb; void *user; int id; } ImdbReq;

static void imdb_from(const char *json, char *out, size_t n)
{
    cJSON *root = cJSON_Parse(json);
    const cJSON *v = root ? cJSON_GetObjectItemCaseSensitive(root, "imdb_id") : NULL;
    snprintf(out, n, "%s", cJSON_IsString(v) ? v->valuestring : "");
    cJSON_Delete(root);
}

static void on_imdb(HttpResponse *r, void *user)
{
    ImdbReq *q = user;
    char imdb[24] = "";
    if (r->status == 200) {
        imdb_from(r->body, imdb, sizeof imdb);
        char key[256]; snprintf(key, sizeof key, "%s/movie/%d/external_ids", api_base(), q->id);
        if (imdb[0]) app_cache_put(key, r->body, r->len);
    }
    q->cb(imdb, q->user);
    free(q);
}

void tmdb_imdb_for(int tmdb_id, TmdbImdbCb cb, void *user)
{
    char key[256], url[512], imdb[24] = "";
    snprintf(key, sizeof key, "%s/movie/%d/external_ids", api_base(), tmdb_id);
    char *cached = app_cache_get(key, 90 * 24 * 3600);
    if (cached) { imdb_from(cached, imdb, sizeof imdb); free(cached); cb(imdb, user); return; }
    if (!tmdb_enabled()) { cb("", user); return; }
    ImdbReq *q = malloc(sizeof *q);
    q->cb = cb; q->user = user; q->id = tmdb_id;
    snprintf(url, sizeof url, "%s?api_key=%s", key, api_key());
    http_request_async(url, NULL, on_imdb, q);
}

/* ---- a person's titles ---- */
typedef struct { char name[96]; int id; TmdbTitlesCb cb; void *user; MetaPreview *out; int n; } PersonJob;

static cJSON *get_json(const char *url)
{
    HttpResponse r;
    HttpOptions o = { .timeout_s = 15 };
    http_request_ex(url, &o, &r);
    cJSON *j = r.status == 200 ? cJSON_Parse(r.body) : NULL;
    http_response_free(&r);
    return j;
}

static void person_task(HttpResponse *r, void *arg)
{
    PersonJob *pj = arg;
    char url[600], enc[300];
    int id = pj->id;
    if (!id) {   /* by name: the most popular person called that */
        http_url_encode(pj->name, enc, sizeof enc);
        snprintf(url, sizeof url, "%s/search/person?query=%s&api_key=%s", api_base(), enc, api_key());
        cJSON *j = get_json(url);
        const cJSON *first = cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(j, "results"), 0);
        const cJSON *v = cJSON_GetObjectItemCaseSensitive(first, "id");
        id = cJSON_IsNumber(v) ? v->valueint : 0;
        cJSON_Delete(j);
    }
    if (!id) { r->status = 404; return; }
    snprintf(url, sizeof url, "%s/person/%d/combined_credits?api_key=%s", api_base(), id, api_key());
    cJSON *j = get_json(url);
    const cJSON *cast = cJSON_GetObjectItemCaseSensitive(j, "cast"), *it;
    /* acting roles, the most popular first; talk shows and one-off appearances left out */
    enum { MAXP = 24 };
    const cJSON *pick[MAXP]; double pop[MAXP]; int np = 0;
    cJSON_ArrayForEach(it, cast) {
        const cJSON *mt = cJSON_GetObjectItemCaseSensitive(it, "media_type"), *p = cJSON_GetObjectItemCaseSensitive(it, "popularity");
        const cJSON *eps = cJSON_GetObjectItemCaseSensitive(it, "episode_count"), *g = cJSON_GetObjectItemCaseSensitive(it, "genre_ids"), *gg;
        if (!cJSON_IsString(mt)) continue;
        int tv = !strcmp(mt->valuestring, "tv");
        if (!tv && strcmp(mt->valuestring, "movie")) continue;
        int talk = 0;
        cJSON_ArrayForEach(gg, g) if (cJSON_IsNumber(gg) && (gg->valueint == 10767 || gg->valueint == 10763 || gg->valueint == 10764)) talk = 1;   /* talk, news, reality */
        if (talk || (tv && cJSON_IsNumber(eps) && eps->valueint < 2)) continue;
        double pv = cJSON_IsNumber(p) ? p->valuedouble : 0;
        int k = np < MAXP ? np++ : MAXP - 1;
        if (k == MAXP - 1 && np == MAXP && pv <= pop[k]) continue;
        for (; k > 0 && pop[k - 1] < pv; k--) { pick[k] = pick[k - 1]; pop[k] = pop[k - 1]; }
        pick[k] = it; pop[k] = pv;
    }
    pj->out = calloc((size_t)np + 1, sizeof *pj->out);
    for (int i = 0; i < np; i++) {
        const cJSON *e = pick[i];
        int tv = !strcmp(cJSON_GetObjectItemCaseSensitive(e, "media_type")->valuestring, "tv");
        const cJSON *tid = cJSON_GetObjectItemCaseSensitive(e, "id");
        if (!cJSON_IsNumber(tid)) continue;
        snprintf(url, sizeof url, "%s/%s/%d/external_ids?api_key=%s", api_base(), tv ? "tv" : "movie", tid->valueint, api_key());
        cJSON *x = get_json(url);
        const cJSON *imdb = cJSON_GetObjectItemCaseSensitive(x, "imdb_id");
        if (cJSON_IsString(imdb) && !strncmp(imdb->valuestring, "tt", 2)) {
            MetaPreview *m = &pj->out[pj->n++];
            const cJSON *t = cJSON_GetObjectItemCaseSensitive(e, tv ? "name" : "title");
            const cJSON *d = cJSON_GetObjectItemCaseSensitive(e, tv ? "first_air_date" : "release_date");
            const cJSON *pp = cJSON_GetObjectItemCaseSensitive(e, "poster_path"), *ov = cJSON_GetObjectItemCaseSensitive(e, "overview");
            m->id = strdup(imdb->valuestring);
            m->type = strdup(tv ? "series" : "movie");
            m->name = strdup(cJSON_IsString(t) ? t->valuestring : imdb->valuestring);
            if (cJSON_IsString(d) && strlen(d->valuestring) >= 4) { char y[8]; snprintf(y, sizeof y, "%.4s", d->valuestring); m->release_info = strdup(y); }
            if (cJSON_IsString(pp) && pp->valuestring[0]) { char pu[320]; snprintf(pu, sizeof pu, "https://image.tmdb.org/t/p/w342%s", pp->valuestring); m->poster = strdup(pu); }
            if (cJSON_IsString(ov) && ov->valuestring[0]) m->description = strdup(ov->valuestring);
        }
        cJSON_Delete(x);
    }
    cJSON_Delete(j);
    r->status = 200;
}

static void on_person(HttpResponse *r, void *user)
{
    PersonJob *pj = user;
    LOGI("tmdb: %s has %d titles", pj->name, pj->n);
    pj->cb(pj->out, r->status == 200 ? pj->n : -1, pj->user);
    meta_previews_free(pj->out, pj->n);
    free(pj);
}

void tmdb_person_titles(const char *name, int person_id, TmdbTitlesCb cb, void *user)
{
    if (!tmdb_enabled()) { cb(NULL, -1, user); return; }
    PersonJob *pj = calloc(1, sizeof *pj);
    snprintf(pj->name, sizeof pj->name, "%s", name ? name : "");
    pj->id = person_id; pj->cb = cb; pj->user = user;
    http_task_async("themoviedb.org", person_task, pj, on_person, pj);
}
