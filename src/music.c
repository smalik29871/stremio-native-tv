#include "music.h"
#include "app.h"
#include "http.h"
#include "log.h"
#include "../vendor/cJSON.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *api(void) { const char *e = getenv("SNTV_ITUNES_API"); return e && *e ? e : "https://itunes.apple.com"; }

enum { M_IDLE, M_SEARCH, M_TRACKS, M_DONE };
typedef struct {
    char id[32];
    int state, n, waiting, year;
    unsigned gen, stamp;
    long ids[MUSIC_ALBUMS];
    MusicAlbum albums[MUSIC_ALBUMS];
} Soundtrack;
static Soundtrack g_st[3];
static unsigned g_gen, g_clock;

typedef struct { unsigned gen; int slot, album; } Req;

/* lower case letters and digits, one space between words: "Breaking Bad (Original...)" -> "breaking bad original" */
static void norm(const char *in, char *out, size_t n)
{
    size_t o = 0;
    int sp = 1;
    for (const unsigned char *p = (const unsigned char *)in; *p && o + 1 < n; p++) {
        if (isalnum(*p)) { out[o++] = (char)tolower(*p); sp = 0; }
        else if (*p == '&' && o + 4 < n) { if (!sp) out[o++] = ' '; memcpy(out + o, "and ", 4); o += 4; sp = 1; }
        else if (!sp) { out[o++] = ' '; sp = 1; }
    }
    while (o && out[o - 1] == ' ') o--;
    out[o] = 0;
}

static int is_soundtrack(const char *name)
{
    static const char *const marks[] = { "soundtrack", "original score", "music from", "original motion picture",
                                         "original television", "original series", "music inspired by", "score from" };
    for (size_t i = 0; i < sizeof marks / sizeof *marks; i++) if (strstr(name, marks[i])) return 1;
    return 0;
}

static void finish(Soundtrack *s)
{
    s->state = M_DONE;
    LOGI("music: %d soundtrack album%s for %s", s->n, s->n == 1 ? "" : "s", s->id);
    app_redraw();
}

static void parse_tracks(MusicAlbum *a, const char *json)
{
    cJSON *root = cJSON_Parse(json), *it;
    const cJSON *res = root ? cJSON_GetObjectItemCaseSensitive(root, "results") : NULL;
    a->n = 0;
    if (cJSON_IsArray(res)) cJSON_ArrayForEach(it, res) {
        const cJSON *kind = cJSON_GetObjectItemCaseSensitive(it, "wrapperType"), *name = cJSON_GetObjectItemCaseSensitive(it, "trackName");
        if (!cJSON_IsString(kind) || strcmp(kind->valuestring, "track") || !cJSON_IsString(name) || a->n >= MUSIC_TRACKS) continue;
        MusicTrack *t = &a->tracks[a->n++];
        snprintf(t->name, sizeof t->name, "%s", name->valuestring);
        const cJSON *no = cJSON_GetObjectItemCaseSensitive(it, "trackNumber"), *disc = cJSON_GetObjectItemCaseSensitive(it, "discNumber"), *ms = cJSON_GetObjectItemCaseSensitive(it, "trackTimeMillis");
        t->no = cJSON_IsNumber(no) ? no->valueint : a->n;
        t->disc = cJSON_IsNumber(disc) ? disc->valueint : 1;
        t->ms = cJSON_IsNumber(ms) ? ms->valueint : 0;
    }
    cJSON_Delete(root);
}

static Soundtrack *live(const Req *q) { Soundtrack *s = &g_st[q->slot]; return s->gen == q->gen ? s : NULL; }

static void one_done(Soundtrack *s)
{
    if (--s->waiting > 0) return;
    int k = 0;   /* an album whose tracks could not be had is left out */
    for (int i = 0; i < s->n; i++) if (s->albums[i].n) { if (k != i) s->albums[k] = s->albums[i]; k++; }
    s->n = k;
    finish(s);
}

static void on_tracks(HttpResponse *r, void *user)
{
    Req *q = user;
    Soundtrack *s = live(q);
    if (s) {
        if (r->status == 200 && r->body) {
            char url[200];
            snprintf(url, sizeof url, "%s/lookup?id=%ld&entity=song", api(), s->ids[q->album]);
            app_cache_put(url, r->body, r->len);
            parse_tracks(&s->albums[q->album], r->body);
        }
        one_done(s);
    }
    free(q);
}

static void ask_tracks(Soundtrack *s, int slot)
{
    s->state = M_TRACKS;
    s->waiting = s->n;
    for (int i = 0; i < s->n; i++) {
        char url[200];
        snprintf(url, sizeof url, "%s/lookup?id=%ld&entity=song", api(), s->ids[i]);
        char *cached = app_cache_get(url, 30 * 24 * 3600);
        if (cached) { parse_tracks(&s->albums[i], cached); free(cached); one_done(s); continue; }
        Req *q = malloc(sizeof *q);
        q->gen = s->gen; q->slot = slot; q->album = i;
        http_request_async(url, NULL, on_tracks, q);
    }
}

/* the store's albums whose name has the title in it and says it is the music of it */
static void parse_search(Soundtrack *s, const char *title, const char *json)
{
    char want[160];
    norm(title, want, sizeof want);
    cJSON *root = cJSON_Parse(json), *it;
    const cJSON *res = root ? cJSON_GetObjectItemCaseSensitive(root, "results") : NULL;
    s->n = 0;
    if (cJSON_IsArray(res) && want[0]) cJSON_ArrayForEach(it, res) {
        if (s->n >= MUSIC_ALBUMS) break;
        const cJSON *name = cJSON_GetObjectItemCaseSensitive(it, "collectionName"), *cid = cJSON_GetObjectItemCaseSensitive(it, "collectionId");
        if (!cJSON_IsString(name) || !cJSON_IsNumber(cid)) continue;
        char got[300];
        norm(name->valuestring, got, sizeof got);
        size_t wl = strlen(want);
        const char *at = strstr(got, want);
        /* the title as whole words ("up" is not in "breakup"), and marked as a soundtrack */
        if (!at || (at > got && at[-1] != ' ') || (at[wl] && at[wl] != ' ') || !is_soundtrack(got)) continue;
        const cJSON *date = cJSON_GetObjectItemCaseSensitive(it, "releaseDate");
        int y = cJSON_IsString(date) ? atoi(date->valuestring) : 0;
        if (s->year && y && y < s->year - 1) continue;   /* an older film's of the same name */
        MusicAlbum *a = &s->albums[s->n];
        memset(a, 0, sizeof *a);
        snprintf(a->title, sizeof a->title, "%s", name->valuestring);
        const cJSON *artist = cJSON_GetObjectItemCaseSensitive(it, "artistName"), *art = cJSON_GetObjectItemCaseSensitive(it, "artworkUrl100"), *link = cJSON_GetObjectItemCaseSensitive(it, "collectionViewUrl");
        if (cJSON_IsString(artist)) snprintf(a->artist, sizeof a->artist, "%s", artist->valuestring);
        if (cJSON_IsString(link)) snprintf(a->link, sizeof a->link, "%s", link->valuestring);
        if (cJSON_IsString(art)) {   /* the cover at 600 px, not the 100 px thumbnail */
            snprintf(a->art, sizeof a->art, "%s", art->valuestring);
            char *sz = strstr(a->art, "100x100");
            if (sz) memcpy(sz, "600x600", 7);
        }
        a->year = y;
        s->ids[s->n++] = (long)cid->valuedouble;
    }
    cJSON_Delete(root);
}

typedef struct { Req q; char title[160], url[600]; } SearchReq;

static void on_search(HttpResponse *r, void *user)
{
    SearchReq *sr = user;
    Soundtrack *s = live(&sr->q);
    if (s) {
        if (r->status == 200 && r->body) {
            app_cache_put(sr->url, r->body, r->len);
            parse_search(s, sr->title, r->body);
            if (s->n) ask_tracks(s, sr->q.slot); else finish(s);
        } else { s->n = 0; finish(s); LOGI("music: the store did not answer for %s (%ld)", s->id, r->status); }
    }
    free(sr);
}

const MusicAlbum *music_albums(const char *id, const char *title, const char *year, int *n)
{
    *n = 0;
    if (!id || !*id || !title || !*title) return NULL;
    int slot = 0;
    for (int i = 0; i < 3; i++) {
        Soundtrack *s = &g_st[i];
        if (s->state != M_IDLE && !strcmp(s->id, id)) {
            s->stamp = ++g_clock;
            if (s->state != M_DONE || !s->n) return NULL;
            *n = s->n;
            return s->albums;
        }
        if (g_st[i].stamp < g_st[slot].stamp) slot = i;
    }
    Soundtrack *s = &g_st[slot];
    memset(s, 0, sizeof *s);
    snprintf(s->id, sizeof s->id, "%s", id);
    s->state = M_SEARCH; s->gen = ++g_gen; s->stamp = ++g_clock;
    s->year = year ? atoi(year) : 0;
    char term[200], enc[400], url[600];
    snprintf(term, sizeof term, "%s soundtrack", title);
    http_url_encode(term, enc, sizeof enc);
    snprintf(url, sizeof url, "%s/search?term=%s&media=music&entity=album&limit=15", api(), enc);
    char *cached = app_cache_get(url, 7 * 24 * 3600);
    if (cached) {
        parse_search(s, title, cached);
        free(cached);
        if (s->n) ask_tracks(s, slot); else finish(s);
        if (s->state == M_DONE && s->n) { *n = s->n; return s->albums; }
        return NULL;
    }
    SearchReq *sr = calloc(1, sizeof *sr);
    sr->q.gen = s->gen; sr->q.slot = slot;
    snprintf(sr->title, sizeof sr->title, "%s", title);
    snprintf(sr->url, sizeof sr->url, "%s", url);
    http_request_async(url, NULL, on_search, sr);
    return NULL;
}
