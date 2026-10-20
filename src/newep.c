#include "newep.h"
#include "app.h"
#include "http.h"
#include "log.h"
#include "store.h"
#include "../vendor/cJSON.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MAX_SHOWS 80
#define CHECK_EVERY_S (6 * 3600)
#define FIRST_CHECK_S 3.0f    /* once the addons are in, so Continue Watching has it at launch */

typedef struct { NewEp e; char newest_id[128]; int64_t first_ms; } Entry;

static Entry g_found[MAX_SHOWS];
static int g_n;
static unsigned g_gen;
static float g_next_at = FIRST_CHECK_S;

typedef struct { unsigned gen; char id[96], url[1024]; } Req;

/* "2024-05-02T01:00:00.000Z" (or just the date) as ms since 1970, UTC; 0 when unreadable */
static int64_t released_ms(const char *s)
{
    int y, mo, d, h = 0, mi = 0;
    if (!s || sscanf(s, "%d-%d-%d", &y, &mo, &d) != 3) return 0;
    const char *t = strchr(s, 'T');
    if (t) sscanf(t + 1, "%d:%d", &h, &mi);
    y -= mo <= 2;   /* days from the civil date (Howard Hinnant's algorithm) */
    long era = (y >= 0 ? y : y - 399) / 400, yoe = y - era * 400;
    long doy = (153 * (mo + (mo > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    long days = era * 146097 + yoe * 365 + yoe / 4 - yoe / 100 + doy - 719468;
    return ((int64_t)days * 86400 + h * 3600 + mi * 60) * 1000;
}

static int64_t now_ms(void) { return (int64_t)time(NULL) * 1000; }

/* Season and episode of "tt123:2:5"; 0 when the id has none */
static int episode_of(const char *video_id, int *se, int *ep)
{
    const char *c = video_id ? strchr(video_id, ':') : NULL;
    return c && sscanf(c, ":%d:%d", se, ep) == 2;
}

static void forget(const char *id)
{
    for (int i = 0; i < g_n; i++)
        if (!strcmp(g_found[i].e.id, id)) { g_found[i] = g_found[--g_n]; g_app.library_version++; return; }
}

static void consider(const char *id, const MetaItem *m)
{
    const LibraryItem *li = app_library_find(id);
    if (!li) return;
    int ls = 0, le = 0, has_last = episode_of(li->video_id, &ls, &le);
    int64_t now = now_ms(), first = 0, newest = 0;
    const char *first_id = NULL, *newest_id = NULL;
    int count = 0;
    for (int i = 0; i < m->n_videos; i++) {
        const MetaVideo *v = &m->videos[i];
        int64_t at = released_ms(v->released);
        if (!at || at > now || at <= li->last_watched_ms || v->season <= 0) continue;   /* not out yet, seen already, or a special */
        if (has_last && (v->season < ls || (v->season == ls && v->episode <= le))) continue;   /* before where you are */
        count++;
        if (!first || at < first) { first = at; first_id = v->id; }
        if (at >= newest) { newest = at; newest_id = v->id; }
    }
    forget(id);
    if (!count || g_n == MAX_SHOWS) return;
    Entry *e = &g_found[g_n++];
    memset(e, 0, sizeof *e);
    snprintf(e->e.id, sizeof e->e.id, "%s", id);
    snprintf(e->e.video_id, sizeof e->e.video_id, "%s", first_id);
    snprintf(e->newest_id, sizeof e->newest_id, "%s", newest_id);
    e->e.count = count;
    e->e.released_ms = newest;
    e->first_ms = first;
    g_app.library_version++;   /* Continue Watching takes it in */
    LOGI("newep: %s has %d new episode%s from %s", li->name ? li->name : id, count, count == 1 ? "" : "s", first_id);
}

static void on_meta(HttpResponse *r, void *user)
{
    Req *q = user;
    MetaItem m;
    if (q->gen == g_gen && r->status == 200 && r->body && addon_parse_meta(r->body, &m) == 0) {
        app_cache_put(q->url, r->body, r->len);   /* the Calendar opens at once too */
        consider(q->id, &m);
        meta_item_free(&m);
    }
    free(q);
}

/* A series you have watched, kept in the library and not in the middle of an episode */
static int followed(const LibraryItem *li)
{
    return !li->removed && !li->temp && !li->no_notif && li->type && !strcmp(li->type, "series")
        && li->time_offset == 0 && li->last_watched_ms > 0 && (li->times_watched > 0 || li->flagged_watched || li->video_id);
}

static void check_all(void)
{
    g_gen++;
    int asked = 0, cached = 0;
    for (int i = 0; i < g_app.n_library && asked + cached < MAX_SHOWS; i++) {
        const LibraryItem *li = &g_app.library[i];
        if (!followed(li)) continue;
        for (int a = 0; a < g_app.n_addons; a++) {
            if (!addon_supports(&g_app.addons[a], "meta", li->type, li->id)) continue;
            char url[1024];
            addon_url_meta(&g_app.addons[a], li->type, li->id, url, sizeof url);
            char *body = app_cache_get(url, CHECK_EVERY_S);
            MetaItem m;
            if (body && addon_parse_meta(body, &m) == 0) { consider(li->id, &m); meta_item_free(&m); cached++; }
            else {
                Req *q = calloc(1, sizeof *q);
                q->gen = g_gen;
                snprintf(q->id, sizeof q->id, "%s", li->id);
                snprintf(q->url, sizeof q->url, "%s", url);
                http_request_async(url, NULL, on_meta, q);
                asked++;
            }
            free(body);
            break;
        }
    }
    LOGI("newep: checking %d series (%d from the cache)", asked + cached, cached);
}

void newep_tick(void)
{
    static int init;
    if (!init) { init = 1; const char *e = getenv("SNTV_NEWEP_AFTER_S"); if (e) g_next_at = (float)atof(e); }   /* tests */
    if (g_app.time < g_next_at || !g_app.n_library) return;
    if (!g_app.n_addons) { g_next_at = g_app.time + 5; return; }   /* the meta addons are not in yet */
    g_next_at = g_app.time + CHECK_EVERY_S;
    check_all();
}

const NewEp *newep_get(const char *id)
{
    if (!id) return NULL;
    for (int i = 0; i < g_n; i++) {
        const Entry *e = &g_found[i];
        if (strcmp(e->e.id, id)) continue;
        const LibraryItem *li = app_library_find(id);
        if (!li || li->removed || li->time_offset > 0 || li->last_watched_ms >= e->first_ms) return NULL;   /* played since */
        const cJSON *seen = cJSON_GetObjectItemCaseSensitive(store_get_json("newEpSeen"), id);
        if (cJSON_IsString(seen) && !strcmp(seen->valuestring, e->newest_id)) return NULL;   /* put away */
        return &e->e;
    }
    return NULL;
}

void newep_dismiss(const char *id)
{
    for (int i = 0; i < g_n; i++) {
        if (strcmp(g_found[i].e.id, id)) continue;
        cJSON *all = cJSON_Duplicate(store_get_json("newEpSeen"), 1);
        if (!cJSON_IsObject(all)) { cJSON_Delete(all); all = cJSON_CreateObject(); }
        cJSON_DeleteItemFromObjectCaseSensitive(all, id);
        while (cJSON_GetArraySize(all) >= 200) cJSON_DeleteItemFromArray(all, 0);
        cJSON_AddStringToObject(all, id, g_found[i].newest_id);
        store_set_json("newEpSeen", all);
        store_save();
        g_app.library_version++;
        return;
    }
}
