#include "../app.h"
#include "../http.h"
#include "../ai.h"
#include "../tmdb.h"
#include "../store.h"
#include "../../vendor/cJSON.h"
#include "../log.h"
#include "../phone.h"
#include "qrcodegen.h"
#include "font.h"
#include "keyboard.h"
#include "theme.h"
#include "widgets.h"
#include "screen_options.h"
#include <stdio.h>
#include <strings.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* Results sit beside the keyboard while typing and use the full width once it closes. */
enum { COLS_TYPING = 3, COLS = 7, CELL_W = 196, CELL_H = 294, CELL_GAP = 28, FIELD_Y = 140, FIELD_H = 76 };

/* Results are grouped by type, each group under its own heading and starting a new row. */
enum { SECT_MAX = 4, SECT_H = 56 };
typedef struct { int row, col; } Cell;
typedef struct { int first_row, n; const char *label; } Section;

typedef struct Search Search;
enum { PICKS_MAX = 32, AI_TITLES = 12 };
typedef struct { Search *owner; unsigned generation; int pick; int rank; char want[160]; int year, kind; } Pending;   /* kind: -1 any, 0 movie, 1 series */
/* Every query of every Search gets its own number, never reused; a reply counts only when
 * its Search is still open and still on that query. A Search under another one (a cast
 * member's films opened from its results) keeps receiving its own answers, and one closed
 * meanwhile is never touched, even if a new Search now sits at the same address. */
static unsigned g_search_gen;
#define OPEN_MAX 8
static Search *g_open[OPEN_MAX];


struct Search {
    Keyboard kb;
    int typing;               /* the keyboard is open; OK on the search bar opens it */
    int focus_grid;           /* 0 the search bar (or the keyboard), 1 results, 2 recent searches */
    uint8_t qr[qrcodegen_BUFFER_LEN_MAX]; int qr_ready; char phone_url[96];   /* or type on the phone */
    MetaPreview *results; int n;
    int grid_focus;
    int want_results;         /* a search was asked for: focus moves to the results when they arrive */
    float debounce;
    int dirty;
    unsigned generation;      /* a new number per query (g_search_gen); stale replies are dropped */
    float focus_t, grid_scroll;
    Peek peek;                /* details beside the focused result */
    int pending;
    char last_query[256];
    unsigned ai_req; int ai_state;   /* 0 off, 1 asking, 2 answered, 3 failed */
    int n_picks;                     /* results at the front that the assistant suggested (and TMDB found for its filters) */
    int pick_rank[PICKS_MAX];        /* the assistant's order for those, so they stay in it */
    char ai_note[160];
    char recent[10][96]; int n_recent, recent_focus;   /* shown while the field is empty */
    Cell *cell; int cell_cap;        /* where each result sits, from place() */
    float *row_y; int *row_first, n_rows, rows_cap;
    Section sect[SECT_MAX]; int n_sect;
    int groups_logged;
    char first_logged[64];        /* the leading result last logged */
};

static Search *g_person_live;   /* the screen a filmography lookup answers to */

static void recent_load(Search *s)
{
    s->n_recent = 0;
    const cJSON *arr = store_get_json("recentSearches"), *it;
    cJSON_ArrayForEach(it, arr) if (cJSON_IsString(it) && s->n_recent < 10) snprintf(s->recent[s->n_recent++], 96, "%s", it->valuestring);
}

static void recent_save(Search *s)
{
    cJSON *arr = cJSON_CreateArray();
    for (int i = 0; i < s->n_recent; i++) cJSON_AddItemToArray(arr, cJSON_CreateString(s->recent[i]));
    store_set_json("recentSearches", arr);
    store_save();
}

/* A search that found something goes to the top of the list (a repeat moves up). */
static void recent_add(Search *s, const char *q)
{
    if (strlen(q) < 2) return;
    int at = -1;
    for (int i = 0; i < s->n_recent; i++) if (strcasecmp(s->recent[i], q) == 0) at = i;
    if (at == 0) return;
    if (at < 0) at = s->n_recent < 10 ? s->n_recent++ : 9;
    memmove(s->recent[1], s->recent[0], (size_t)at * 96);
    snprintf(s->recent[0], 96, "%s", q);
    recent_save(s);
}

static void clear_results(Search *s)
{
    meta_previews_free(s->results, s->n);
    s->results = NULL; s->n = 0; s->grid_focus = 0; s->n_picks = 0; s->groups_logged = 0;
}

static int same_title(const char *a, const char *b)
{
    while (*a && *b) {
        unsigned ca = (unsigned char)*a, cb = (unsigned char)*b;
        if (ca >= 'A' && ca <= 'Z') ca += 32;
        if (cb >= 'A' && cb <= 'Z') cb += 32;
        if (ca != cb) return 0;
        a++; b++;
    }
    return !*b && (!*a || *a == ' ' || *a == ':' || *a == '(');
}

/* The result's year is the one asked for, give or take one (a festival year, a late
 * release); a result or a suggestion without a year passes. */
static int year_fits(const MetaPreview *m, int want)
{
    if (!want || !m->release_info || strlen(m->release_info) < 4) return 1;
    int y = atoi(m->release_info);
    return !y || (y >= want - 1 && y <= want + 1);
}

/* 0 movies, 1 series, 2 anything else */
static int kind_of(const MetaPreview *m)
{
    if (m->type && strcmp(m->type, "movie") == 0) return 0;
    if (m->type && (strcmp(m->type, "series") == 0 || strcmp(m->type, "anime") == 0)) return 1;
    return 2;
}

/* Puts the results after the AI picks in groups by type. A group's place is where its best
 * result was, so the closest match still leads; the order inside a group is kept. */
/* Lower-case words, one space between them: "Undercover Angel (1999)" -> "undercover angel 1999". */
static void words_of(const char *in, char *out, size_t cap)
{
    size_t n = 0;
    for (; *in && n + 1 < cap; in++) {
        unsigned char c = (unsigned char)*in;
        int word = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c >= 0x80;
        if (word) out[n++] = (char)(c >= 'A' && c <= 'Z' ? c + 32 : c);
        else if (n && out[n - 1] != ' ') out[n++] = ' ';
    }
    while (n && out[n - 1] == ' ') n--;
    out[n] = 0;
}

static int has_word(const char *words, const char *w, size_t len, int prefix)   /* w as a whole word (or its start) */
{
    for (const char *p = words; *p; ) {
        size_t k = strcspn(p, " ");
        if ((k == len || (prefix && k > len)) && !strncmp(p, w, len)) return 1;
        p += k + (p[k] == ' ');
    }
    return 0;
}

/* How well a title matches what was typed: 0 the title itself, 1 it starts with it, 2
 * every word in it, 3 some word, 4 none (an addon that answers with its whole catalog). */
static int match_tier(const char *name, const char *query)
{
    char t[256], q[256];
    words_of(name ? name : "", t, sizeof t);
    words_of(query, q, sizeof q);
    if (!q[0]) return 0;
    size_t ql = strlen(q);
    if (!strcmp(t, q)) return 0;
    if (!strncmp(t, q, ql) && t[ql] == ' ') return 1;
    int all = 1, any = 0;
    for (const char *w = q; *w; ) {
        size_t k = strcspn(w, " ");
        int in = has_word(t, w, k, 0), starts = in || has_word(t, w, k, 1);
        all &= in; any |= starts;
        w += k + (w[k] == ' ');
    }
    return all ? 2 : any ? 3 : 4;
}

static void regroup(Search *s)
{
    if (s->n - s->n_picks < 2) return;
    const char *focused = s->grid_focus < s->n ? s->results[s->grid_focus].id : NULL;
    MetaPreview *tmp = malloc(sizeof *tmp * (size_t)s->n);
    unsigned char *tier = malloc((size_t)s->n);
    if (!tmp || !tier) { free(tmp); free(tier); return; }
    /* the best matches first, whichever addon answered first; the group (movies, series)
     * with the best match leads */
    int order[3], n_order = 0, k = 0, count[3] = {0}, best[3] = {9, 9, 9};
    for (int i = s->n_picks; i < s->n; i++) {
        int g = kind_of(&s->results[i]), seen = 0;
        tier[i] = (unsigned char)match_tier(s->results[i].name, s->last_query);
        if (tier[i] < best[g]) best[g] = tier[i];
        for (int j = 0; j < n_order; j++) seen |= order[j] == g;
        if (!seen) order[n_order++] = g;
        count[g]++;
    }
    for (int j = 1; j < n_order; j++)
        for (int i = j; i > 0 && best[order[i]] < best[order[i - 1]]; i--) { int t = order[i]; order[i] = order[i - 1]; order[i - 1] = t; }
    for (int j = 0; j < n_order; j++)
        for (int t = 0; t <= 4; t++)
            for (int i = s->n_picks; i < s->n; i++) if (kind_of(&s->results[i]) == order[j] && tier[i] == t) tmp[k++] = s->results[i];
    free(tier);
    memcpy(&s->results[s->n_picks], tmp, sizeof *tmp * (size_t)k);
    free(tmp);
    if (focused) for (int i = 0; i < s->n; i++) if (s->results[i].id == focused) { s->grid_focus = i; break; }
    const char *lead = s->results[s->n_picks].name ? s->results[s->n_picks].name : "";
    if (strncmp(lead, s->first_logged, sizeof s->first_logged - 1)) {
        snprintf(s->first_logged, sizeof s->first_logged, "%s", lead);
        LOGI("search: \"%s\" first of %d", lead, s->n - s->n_picks);
    }
    int key = count[0] * 10000 + count[1] * 100 + count[2];
    if (n_order > 1 && key != s->groups_logged) {
        s->groups_logged = key;
        LOGI("search: grouped %d movie%s, %d series, %d other", count[0], count[0] == 1 ? "" : "s", count[1], count[2]);
    }
}

/* Rows and headings for the results: a heading per group once there is more than one. */
static void place(Search *s, int nc)
{
    if (s->n > s->cell_cap) { s->cell_cap = s->n + 32; s->cell = realloc(s->cell, sizeof *s->cell * (size_t)s->cell_cap); }
    if (s->n + SECT_MAX > s->rows_cap) {
        s->rows_cap = s->n + SECT_MAX + 32;
        s->row_y = realloc(s->row_y, sizeof *s->row_y * (size_t)s->rows_cap);
        s->row_first = realloc(s->row_first, sizeof *s->row_first * (size_t)s->rows_cap);
    }
    static const char *names[] = { "Movies", "Series", "More" };
    s->n_sect = 0;
    for (int i = 0; i < s->n; i++) {
        const char *label = i < s->n_picks ? "AI picks" : names[kind_of(&s->results[i])];
        if (!s->n_sect || s->sect[s->n_sect - 1].label != label) {
            if (s->n_sect == SECT_MAX) { s->sect[s->n_sect - 1].n++; continue; }
            s->sect[s->n_sect++] = (Section){ 0, 0, label };
        }
        s->sect[s->n_sect - 1].n++;
    }
    int headed = s->n_sect > 1, i = 0;
    float y = 0;
    s->n_rows = 0;
    for (int k = 0; k < s->n_sect; k++) {
        s->sect[k].first_row = s->n_rows;
        if (headed) y += SECT_H;
        for (int j = 0; j < s->sect[k].n; j++, i++) {
            if (j % nc == 0) { s->row_first[s->n_rows] = i; s->row_y[s->n_rows++] = y; y += CELL_H + 80; }
            s->cell[i] = (Cell){ s->n_rows - 1, j % nc };
        }
    }
    s->row_first[s->n_rows] = s->n;
}

/* The result in a row closest to a column. */
static int in_row(const Search *s, int row, int col)
{
    int n = s->row_first[row + 1] - s->row_first[row];
    return s->row_first[row] + (col < n ? col : n - 1);
}

static Search *still_open(Search *s, unsigned generation)
{
    for (int i = 0; i < OPEN_MAX; i++) if (g_open[i] && g_open[i] == s) return s->generation == generation ? s : NULL;
    return NULL;
}

static void on_result(HttpResponse *r, void *user)
{
    Pending *p = user;
    Search *s = still_open(p->owner, p->generation);
    if (!s) { free(p); return; }
    s->pending--;
    MetaPreview *metas; int n;
    if (p->pick) {   /* a title the assistant suggested: keep the one result that is that title, at the front, in the assistant's order */
        if (r->parsed) { ParsedMetas *pm = r->parsed; r->parsed = NULL; metas = pm->m; n = pm->n; free(pm);
            int took = 0;
            for (int i = 0; i < n; i++) {
                int dup = -1;
                for (int j = 0; j < s->n; j++) if (strcmp(s->results[j].id, metas[i].id) == 0) { dup = j; break; }
                int fits = same_title(metas[i].name, p->want) && year_fits(&metas[i], p->year) && (p->kind < 0 || kind_of(&metas[i]) == p->kind);
                int want = dup < 0 && !took && s->n_picks < PICKS_MAX && fits;
                int promote = dup >= s->n_picks && s->n_picks < PICKS_MAX && fits;
                if (want || promote) {
                    MetaPreview item = want ? metas[i] : s->results[dup];
                    if (promote) memmove(&s->results[s->n_picks + 1], &s->results[s->n_picks], sizeof item * (size_t)(dup - s->n_picks));
                    else { s->results = realloc(s->results, sizeof item * (size_t)(s->n + 1)); memmove(&s->results[s->n_picks + 1], &s->results[s->n_picks], sizeof item * (size_t)(s->n - s->n_picks)); s->n++; took = 1; }
                    int pos = s->n_picks;
                    while (pos > 0 && s->pick_rank[pos - 1] > p->rank) { s->results[pos] = s->results[pos - 1]; s->pick_rank[pos] = s->pick_rank[pos - 1]; pos--; }
                    s->results[pos] = item; s->pick_rank[pos] = p->rank; s->n_picks++;
                    if (s->focus_grid && want) s->grid_focus++;
                } else if (dup < 0) meta_preview_free(&metas[i]);
            }
            free(metas);
            regroup(s);
        }
        app_redraw();
        free(p);
        return;
    }
    if (r->parsed) { ParsedMetas *pm = r->parsed; r->parsed = NULL; metas = pm->m; n = pm->n; free(pm);
        s->results = realloc(s->results, sizeof(MetaPreview) * (size_t)(s->n + n));
        for (int i = 0; i < n; i++) {
            int dup = 0;
            for (int j = 0; j < s->n; j++) if (strcmp(s->results[j].id, metas[i].id) == 0) { dup = 1; break; }
            if (dup) meta_preview_free(&metas[i]); else s->results[s->n++] = metas[i];
        }
        free(metas);
        regroup(s);
        if (s->want_results && s->n && !s->typing && s->focus_grid == 0) { s->focus_grid = 1; s->grid_focus = 0; s->focus_t = 0; s->want_results = 0; }
    }
    app_redraw();
    free(p);
}

/* kind 0 or 1: only the movie or the series catalogs, unless no addon has one */
static void search_addons(Search *s, const char *q, int pick, int rank, int year, int kind)
{
    char extra[600];
    const char *kv[] = { "search", q, NULL };
    addon_extra_query(extra, sizeof extra, kv);
    int typed = 0;
    for (int a = 0; a < g_app.n_addons && kind >= 0 && !typed; a++)
        for (int c = 0; c < g_app.addons[a].manifest.n_catalogs; c++) {
            const AddonCatalog *cat = &g_app.addons[a].manifest.catalogs[c];
            if (addon_catalog_extra(cat, "search") && cat->type && !strcmp(cat->type, kind ? "series" : "movie")) { typed = 1; break; }
        }
    for (int a = 0; a < g_app.n_addons; a++) {
        const Addon *ad = &g_app.addons[a];
        if (g_app.hide_adult && ad->manifest.bh_adult) continue;
        for (int c = 0; c < ad->manifest.n_catalogs; c++) {
            const AddonCatalog *cat = &ad->manifest.catalogs[c];
            if (!addon_catalog_extra(cat, "search")) continue;
            if (typed && (!cat->type || strcmp(cat->type, kind ? "series" : "movie"))) continue;
            char url[1200];
            addon_url_catalog(ad, cat->type, cat->id, extra, url, sizeof url);
            Pending *p = calloc(1, sizeof *p);
            p->owner = s; p->generation = s->generation; p->pick = pick; p->rank = rank; p->year = year; p->kind = kind;
            snprintf(p->want, sizeof p->want, "%s", q);
            s->pending++;
            app_fetch_metas(url, 0, on_result, p);
        }
    }
}

/* TMDB's titles for the assistant's filters (language, country, genres, years): put after
 * the assistant's own picks, in TMDB's order (most popular first). */
static void on_discovered(const MetaPreview *titles, int n, void *user)
{
    Pending *p = user;
    Search *s = still_open(p->owner, p->generation);
    free(p);
    if (!s || n <= 0) return;
    int added = 0;
    for (int i = 0; i < n && s->n_picks < PICKS_MAX; i++) {
        int dup = 0;
        for (int j = 0; j < s->n && !dup; j++) dup = !strcmp(s->results[j].id, titles[i].id);
        if (dup) continue;
        s->results = realloc(s->results, sizeof *s->results * (size_t)(s->n + 1));
        memmove(&s->results[s->n_picks + 1], &s->results[s->n_picks], sizeof *s->results * (size_t)(s->n - s->n_picks));
        meta_preview_copy(&s->results[s->n_picks], &titles[i]);
        s->pick_rank[s->n_picks] = 100 + i;
        s->n_picks++; s->n++; added++;
        if (s->focus_grid && s->grid_focus >= s->n_picks - 1) s->grid_focus++;
    }
    LOGI("search: %d more from TMDB for the assistant's filters", added);
    regroup(s);
    app_redraw();
}

static void discover(Search *s, const cJSON *f)
{
    if (!cJSON_IsObject(f) || !tmdb_enabled()) return;
    TmdbFilter tf = { 0 };
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(f, "type");
    tf.tv = cJSON_IsString(v) && !strcmp(v->valuestring, "series");
    v = cJSON_GetObjectItemCaseSensitive(f, "language");
    if (cJSON_IsString(v) && strlen(v->valuestring) == 2) snprintf(tf.lang, sizeof tf.lang, "%s", v->valuestring);
    v = cJSON_GetObjectItemCaseSensitive(f, "country");
    if (cJSON_IsString(v) && strlen(v->valuestring) == 2) snprintf(tf.country, sizeof tf.country, "%s", v->valuestring);
    const cJSON *g, *gs = cJSON_GetObjectItemCaseSensitive(f, "genres");
    cJSON_ArrayForEach(g, gs) if (cJSON_IsString(g) && tf.n_genres < 4) snprintf(tf.genres[tf.n_genres++], sizeof tf.genres[0], "%s", g->valuestring);
    v = cJSON_GetObjectItemCaseSensitive(f, "from"); if (cJSON_IsNumber(v)) tf.from = v->valueint;
    v = cJSON_GetObjectItemCaseSensitive(f, "to"); if (cJSON_IsNumber(v)) tf.to = v->valueint;
    if (!tf.lang[0] && !tf.country[0] && !tf.n_genres) return;   /* nothing to narrow TMDB's list by */
    LOGI("search: the assistant's filters: %s, language %s, country %s, %d genre%s, %d-%d", tf.tv ? "series" : "movies",
         tf.lang[0] ? tf.lang : "any", tf.country[0] ? tf.country : "any", tf.n_genres, tf.n_genres == 1 ? "" : "s", tf.from, tf.to);
    Pending *p = calloc(1, sizeof *p);
    p->owner = s; p->generation = s->generation;
    tmdb_discover(&tf, on_discovered, p);
}

/* "something funny with robots": the assistant names titles, the addons find them */
static void on_ai_titles(const char *text, const char *error, void *user)
{
    Pending *p = user;
    Search *s = still_open(p->owner, p->generation);
    if (!s) { free(p); return; }
    s->ai_req = 0;
    cJSON *root = text ? cJSON_Parse(text) : NULL;
    const cJSON *titles = root ? cJSON_GetObjectItemCaseSensitive(root, "titles") : NULL;
    int n = 0;
    if (cJSON_IsArray(titles)) {
        const cJSON *it;
        cJSON_ArrayForEach(it, titles) {
            const cJSON *name = cJSON_IsObject(it) ? cJSON_GetObjectItemCaseSensitive(it, "name") : it;
            if (!cJSON_IsString(name) || !name->valuestring[0] || n >= AI_TITLES) continue;
            const cJSON *y = cJSON_IsObject(it) ? cJSON_GetObjectItemCaseSensitive(it, "year") : NULL;
            const cJSON *t = cJSON_IsObject(it) ? cJSON_GetObjectItemCaseSensitive(it, "type") : NULL;
            int kind = cJSON_IsString(t) ? (!strcmp(t->valuestring, "movie") ? 0 : !strcmp(t->valuestring, "series") ? 1 : -1) : -1;
            search_addons(s, name->valuestring, 1, n, cJSON_IsNumber(y) ? y->valueint : 0, kind);
            n++;
        }
    }
    discover(s, cJSON_GetObjectItemCaseSensitive(root, "filters"));
    s->ai_state = n ? 2 : 3;
    if (!n) {
        snprintf(s->ai_note, sizeof s->ai_note, "%s", error ? error : "The assistant had no suggestions");
        LOGI("search: no suggestions from the assistant; searching the words instead");
        search_addons(s, s->last_query, 0, 0, 0, -1);
    }
    if (root) cJSON_Delete(root);
    app_redraw();
    free(p);
}

static int looks_like_a_request(const char *q)
{
    int words = 1, len = 0;
    for (; *q; q++, len++) if (*q == ' ') words++;
    return words >= 3 && len >= 12;
}

static void run_query(Search *s)
{
    const char *q = s->kb.text;
    s->want_results = 0;   /* the caller asks again when this search should take the focus */
    if (strcmp(q, s->last_query) == 0) return;
    snprintf(s->last_query, sizeof s->last_query, "%s", q);
    s->generation = ++g_search_gen;
    s->pending = 0;
    if (s->ai_req) ai_cancel(s->ai_req);   /* the last question is no longer wanted */
    s->ai_state = 0; s->ai_req = 0; s->ai_note[0] = 0;
    clear_results(s);
    if (strlen(q) < 2) return;
    if (ai_enabled() && looks_like_a_request(q)) {
        Pending *p = calloc(1, sizeof *p);
        p->owner = s; p->generation = s->generation;
        static const char *SYS = "You recommend films and series to a viewer who describes what they feel like watching. "
            "Keep to every constraint in the request exactly: language or film industry (Bollywood means Hindi films from India, Tollywood Telugu, "
            "a K-drama a Korean series, anime Japanese animation), country, era, genre, and film or series. Never suggest a title that breaks one of them. "
            "Reply with JSON only, no markdown: {\"titles\": [{\"name\": \"title as on IMDb\", \"year\": 2010, \"type\": \"movie\"}], "
            "\"filters\": {\"type\": \"movie\", \"language\": \"hi\", \"country\": \"IN\", \"genres\": [\"Comedy\"], \"from\": 1990, \"to\": 2009}} "
            "with up to 12 well-known titles, best match first; type is movie or series. In filters, language is the ISO 639-1 code of the original "
            "language, country an ISO 3166-1 code, genres only from Action, Adventure, Animation, Comedy, Crime, Documentary, Drama, Family, Fantasy, "
            "History, Horror, Music, Mystery, Romance, Science Fiction, Thriller, War, Western; use null for whatever the request does not say. "
            "If the text is just a title or a person's name, return titles that match it and null filters.";
        s->ai_req = ai_ask(SYS, q, 700, 1, on_ai_titles, p);
        s->ai_state = s->ai_req ? 1 : 0;
        if (s->ai_req) return;   /* a request: the assistant's titles, not every title with one of its words (they come if it has none) */
        free(p);
    }
    search_addons(s, q, 0, 0, 0, -1);
}

static void open_keyboard(Search *s)
{
    s->typing = 1; s->focus_grid = 0; s->want_results = 0;
    input_text(1);
    phone_prompt("Search", "Type a title, a person, or what you feel like watching", s->kb.text);
    char url[96]; uint8_t tmp[qrcodegen_BUFFER_LEN_MAX];
    s->qr_ready = phone_url(url, sizeof url) && qrcodegen_encodeText(url, tmp, s->qr, qrcodegen_Ecc_MEDIUM, qrcodegen_VERSION_MIN, 6, qrcodegen_Mask_AUTO, true);
    snprintf(s->phone_url, sizeof s->phone_url, "%s", s->qr_ready ? url : "");
}

static void close_keyboard(Search *s)
{
    if (!s->typing) return;
    s->typing = 0;
    input_text(0);
    phone_prompt(NULL, NULL, NULL);
}

static int cols(const Search *s) { return s->typing ? COLS_TYPING : COLS; }

static int headed_row(const Search *s, int row)
{
    if (s->n_sect < 2) return 0;
    for (int k = 0; k < s->n_sect; k++) if (s->sect[k].first_row == row) return 1;
    return 0;
}

static void update(Screen *sc, float dt)
{
    Search *s = sc->st;
    char got[256];
    if (s->typing && phone_take(got, sizeof got)) {   /* sent from the phone: search it right away */
        keyboard_init(&s->kb, 0, got);
        close_keyboard(s);
        s->dirty = 0; run_query(s);
        app_toast("Received from your phone");
        app_redraw();
    }
    place(s, cols(s));
    int row = s->grid_focus < s->n ? s->cell[s->grid_focus].row : 0;
    /* keep the row above in view, and the heading over it */
    float target = row > 1 ? s->row_y[row - 1] - (headed_row(s, row - 1) ? SECT_H : 0) : 0;
    if (s->grid_focus < s->n) {   /* and all of the focused row (headings push it lower) */
        float see = SNTV_UI_H - (FIELD_Y + FIELD_H + 40 + 60), bottom = s->row_y[row] + CELL_H + 80 - see;
        if (target < bottom) target = bottom;
    }
    s->grid_scroll = ui_ease(s->grid_scroll, target, NULL, dt, 14);
    s->focus_t = ui_ease(s->focus_t * 100, 100, NULL, dt, 16) / 100;
    ui_peek_step(&s->peek, s->focus_grid == 1 && s->grid_focus < s->n ? &s->results[s->grid_focus] : NULL, dt);
    if (s->dirty) { s->debounce -= dt; if (s->debounce <= 0) { s->dirty = 0; run_query(s); } }
    if (s->pending || s->dirty || s->ai_state == 1) app_redraw();
}

static void draw_field(const Search *s, float x, float y, float fw)
{
    int foc = !s->focus_grid;
    if (g_theme.id == THEME_APERTURE) {
        gfx_rrect(x, y, fw, FIELD_H, 3, COLOR_ALPHA(THEME_TEXT, 0x0C));
        gfx_rrect_ring(x, y, fw, FIELD_H, 3, foc ? 2 : 1, COLOR_ALPHA(THEME_TEXT, foc ? 0xC0 : 0x40));
    } else {
    gfx_rrect(x, y, fw, FIELD_H, R_PANEL, COLOR_ALPHA(THEME_TEXT, foc ? 0x22 : 0x18));
    if (foc) gfx_rrect_ring(x, y, fw, FIELD_H, R_PANEL, 2, s->typing ? COLOR_ALPHA(THEME_TEXT, 0x50) : THEME_ACCENT2);
    }
    ui_icon(ICON_SEARCH, x + 42, y + FIELD_H / 2, 30, THEME_TEXT_DIM);
    const char *tip = foc && !s->typing ? "OK to type" : NULL;
    float tip_w = tip ? font_measure(tip, FONT_XS | FONT_B) + 40 : 0;
    if (tip) font_draw(tip, x + fw - tip_w, y + (FIELD_H - font_height(FONT_XS | FONT_B)) / 2, FONT_XS | FONT_B, THEME_TEXT_MUTED);
    if (s->kb.text[0]) {
        font_draw_fit(s->kb.text, x + 78, y + (FIELD_H - font_height(FONT_M | FONT_B)) / 2, FONT_M | FONT_B, THEME_TEXT, fw - 120 - tip_w);
        if (s->typing && fmodf(g_app.time, 1.0f) < 0.55f) {
            float cx = x + 78 + font_measure(s->kb.text, FONT_M | FONT_B) + 4;
            if (cx < x + fw - 30) gfx_rrect(cx, y + 20, 3, 36, 1.5f, THEME_ACCENT2);
        }
        if (s->typing) app_redraw();
    } else font_draw("Movies, series, people\xE2\x80\xA6", x + 78, y + (FIELD_H - font_height(FONT_M)) / 2, FONT_M, THEME_TEXT_MUTED);
}

/* Under the keyboard: scan to type on the phone instead. */
static void draw_phone(const Search *s, float x, float y)
{
    if (!s->qr_ready) return;
    float qs = 190;
    if (y + qs > SNTV_UI_H - 30) return;
    ui_qr(s->qr, x, y, qs);
    font_draw("Or type on your phone", x + qs + 30, y + 30, FONT_S | FONT_B, THEME_TEXT);
    font_draw_wrapped("Scan the code, type or paste, then Send. It searches straight away.", x + qs + 30, y + 72, FONT_XS, THEME_TEXT_DIM, keyboard_width() - qs - 30, 3);
    font_draw(s->phone_url, x + qs + 30, y + qs - 30, FONT_XS, THEME_TEXT_MUTED);
}

static void draw(Screen *sc)
{
    Search *s = sc->st;
    const MetaPreview *fp = s->focus_grid && s->grid_focus < s->n ? &s->results[s->grid_focus] : NULL;
    char buf[512];
    const char *bg = fp ? app_art_background(fp, buf, sizeof buf) : NULL;
    ui_background(bg ? image_tint(bg) : 0);
    if (g_theme.id == THEME_APERTURE) font_draw("Find your next film", THEME_MARGIN, 26, FONT_XL | FONT_ALT, THEME_TEXT);
    else font_draw("Search", THEME_MARGIN, 52, FONT_L | FONT_B, THEME_TEXT);

    float x = THEME_MARGIN, y = FIELD_Y; int nc = cols(s);
    float fw = s->typing ? keyboard_width() : nc * (CELL_W + CELL_GAP) - CELL_GAP;
    draw_field(s, x, y, fw);
    float gx = x, gy = y + FIELD_H + 40;
    if (s->typing) {
        keyboard_draw(&s->kb, x, y + 108, 1);
        font_draw("Done searches  \xC2\xB7  Back closes the keyboard", x, y + 108 + keyboard_height() + 20, FONT_XS, THEME_TEXT_MUTED);
        draw_phone(s, x, y + 108 + keyboard_height() + 70);
        gx = x + fw + 80; gy = y;
    }
    float grid_w = SNTV_UI_W - THEME_MARGIN - gx;
    if ((s->pending || s->ai_state == 1) && !s->n) {
        for (int i = 0; i < nc * 2; i++) ui_skeleton(gx + (i % nc) * (CELL_W + CELL_GAP), gy + 50 + (i / nc) * (CELL_H + 80), CELL_W, CELL_H, R_CARD);
    } else if (!s->n && !s->kb.text[0] && s->n_recent) {
        font_draw("Recent searches", gx, gy + 4, FONT_S | FONT_B, THEME_TEXT_MUTED);
        float ry = gy + 56;
        for (int i = 0; i <= s->n_recent; i++, ry += 66) {
            int foc = s->focus_grid == 2 && s->recent_focus == i;
            if (foc) gfx_rrect(gx - 16, ry - 6, 640, 58, R_KEY, THEME_FOCUS);
            if (i < s->n_recent) { ui_icon(ICON_CLOCK, gx + 14, ry + 23, 24, foc ? THEME_ON_FOCUS : THEME_TEXT_MUTED); font_draw_fit(s->recent[i], gx + 46, ry + (46 - font_height(FONT_S)) / 2, FONT_S, foc ? THEME_ON_FOCUS : THEME_TEXT, 560); }
            else font_draw("Clear recent searches", gx + 46, ry + (46 - font_height(FONT_S)) / 2, FONT_S, foc ? THEME_ON_FOCUS : THEME_TEXT_DIM);
        }
        if (s->focus_grid != 2) font_draw(s->typing ? "Right from the keyboard picks one" : "Down picks one", gx, ry + 10, FONT_XS, THEME_TEXT_MUTED);
    } else if (!s->n) {
        float cx = gx + grid_w / 2, cy = gy + (s->typing ? 280 : 220);
        gfx_circle(cx, cy, 80, COLOR_ALPHA(THEME_TEXT, 0x10));
        ui_icon(ICON_SEARCH, cx, cy, 70, THEME_TEXT_MUTED);
        font_draw_center(strlen(s->last_query) >= 2 ? "Nothing found" : "Find something to watch", cx, cy + 120, g_theme.id == THEME_APERTURE ? FONT_XL | FONT_ALT : FONT_L | FONT_B, THEME_TEXT);
        font_draw_center(s->ai_note[0] ? s->ai_note : strlen(s->last_query) >= 2 ? "Try another title, or check your catalog addons." : ai_enabled() ? "Type a title, or describe it: \"something funny with robots\"." : "Results come from every catalog addon that supports search.",
                         cx, cy + 178, FONT_S, THEME_TEXT_DIM);
    } else {
        char hdr[160];
        if (s->n_picks) snprintf(hdr, sizeof hdr, "%d AI pick%s first  \xC2\xB7  %d result%s", s->n_picks, s->n_picks == 1 ? "" : "s", s->n, s->n == 1 ? "" : "s");
        else snprintf(hdr, sizeof hdr, "%d result%s%s", s->n, s->n == 1 ? "" : "s", s->ai_state == 1 ? "  \xC2\xB7  asking the assistant" : "");
        font_draw(hdr, gx, gy + 4, FONT_S | FONT_B, THEME_TEXT_MUTED);
        if (s->pending || s->ai_state == 1) ui_spinner(gx + font_measure(hdr, FONT_S | FONT_B) + 40, gy + 18, 14);
    }
    gfx_clip_push(gx - 30, gy + 40, grid_w + 60, SNTV_UI_H - gy - 40);
    float row_h = CELL_H + 80, peek_x = 0, peek_y = 0;
    place(s, nc);
    if (s->n_sect > 1)
        for (int k = 0; k < s->n_sect; k++) {
            float hy = gy + 60 + s->row_y[s->sect[k].first_row] - SECT_H - s->grid_scroll;
            if (hy > SNTV_UI_H || hy + SECT_H < gy) continue;
            char h[64]; snprintf(h, sizeof h, "%s  \xC2\xB7  %d", s->sect[k].label, s->sect[k].n);
            font_draw(h, gx, hy + 6, FONT_M | FONT_B, THEME_TEXT);
        }
    /* details open on the focused card: the others in its row slide aside to make room */
    float room = 0; int room_right = 1, room_row = -1;
    if (s->focus_grid == 1 && s->grid_focus < s->n) {
        float sc = ui_focus_scale(g_app.fx_light ? 1.0f : s->focus_t), w2 = CELL_W * sc;
        float fx = gx + s->cell[s->grid_focus].col * (CELL_W + CELL_GAP) - (w2 - CELL_W) / 2;
        room = ui_peek_room(&s->peek, &s->results[s->grid_focus], fx, w2, CELL_H * sc, gx - 30, gx + grid_w + 30, &room_right);
        room_row = s->cell[s->grid_focus].row;
    }
    for (int pass = 0; pass < 2; pass++)
        for (int i = 0; i < s->n; i++) {
            int r = s->cell[i].row, c = s->cell[i].col;
            float px = gx + c * (CELL_W + CELL_GAP), py = gy + 60 + s->row_y[r] - s->grid_scroll;
            if (room > 0 && r == room_row && (room_right ? i > s->grid_focus : i < s->grid_focus)) px += room_right ? room : -room;
            if (py > SNTV_UI_H || py + row_h < gy) continue;
            int foc = s->focus_grid == 1 && s->grid_focus == i;
            if ((pass == 1) != foc) continue;
            char pb[512];
            if (foc) { peek_x = px; peek_y = py; }
            ui_card(app_art_poster(&s->results[i], pb, sizeof pb), px, py, CELL_W, CELL_H, foc ? (g_app.fx_light ? 1 : s->focus_t) : 0,
                    s->focus_grid == 1 && !foc ? 0.3f : 0, s->results[i].name, 0);
            if (i < s->n_picks) { gfx_circle(px + CELL_W - 22, py + 22, 16, COLOR_ALPHA(THEME_PANEL, 0xD0)); ui_icon(ICON_STAR, px + CELL_W - 22, py + 22, 18, THEME_ACCENT2); }
            if (g_theme.id == THEME_APERTURE) ui_spaced_caps(s->results[i].name, px, py + CELL_H + (foc ? 24 : 14), FONT_S | FONT_ALT, 3, foc ? THEME_TEXT : THEME_TEXT_DIM, CELL_W);
            else font_draw_fit(s->results[i].name, px, py + CELL_H + (foc ? 24 : 12), FONT_XS | FONT_B, foc ? THEME_TEXT : THEME_TEXT_DIM, CELL_W);
            if (foc && ui_hold_hint(px, py + CELL_H + 52, CELL_W)) {}
            else if (s->results[i].release_info) font_draw(s->results[i].release_info, px, py + CELL_H + (foc ? 50 : 38), FONT_XS, THEME_TEXT_MUTED);
        }
    if (s->focus_grid == 1 && s->grid_focus < s->n) {
        float f = g_app.fx_light ? 1 : s->focus_t, sc = ui_focus_scale(f), w2 = CELL_W * sc, h2 = CELL_H * sc;
        ui_peek_draw(&s->peek, &s->results[s->grid_focus], peek_x - (w2 - CELL_W) / 2, peek_y - (h2 - CELL_H) / 2 - ui_focus_lift(f), w2, h2, gx - 30, gx + grid_w + 30);
    }
    gfx_clip_pop();
}

/* The keyboard is open: type, Done searches, Back or Right off the edge closes it. */
static int key_typing(Search *s, KeyAction a)
{
    if (a == KEY_BACK) { close_keyboard(s); return 1; }
    if (a == KEY_RIGHT && s->kb.col == 10 && s->n) { close_keyboard(s); s->focus_grid = 1; s->grid_focus = 0; recent_add(s, s->kb.text); return 1; }
    if (a == KEY_RIGHT && s->kb.col == 10 && !s->kb.text[0] && s->n_recent) { close_keyboard(s); s->focus_grid = 2; s->recent_focus = 0; return 1; }
    if (a == KEY_BACKSPACE) { keyboard_key(&s->kb, KEY_BACKSPACE); s->dirty = 1; s->debounce = 0.5f; return 1; }
    KeyboardResult r = keyboard_key(&s->kb, a);
    if (r == KB_CHANGED) { s->dirty = 1; s->debounce = 0.6f; }
    else if (r == KB_DONE) {
        close_keyboard(s);
        s->dirty = 0; run_query(s);
        if (s->n) { s->focus_grid = 1; s->grid_focus = 0; recent_add(s, s->kb.text); }
        else s->want_results = 1;
    }
    return 1;
}

static int key(Screen *sc, KeyAction a)
{
    Search *s = sc->st;
    app_redraw();
    if (a == KEY_LONG_OK || a == KEY_YELLOW) {
        if (s->focus_grid == 1 && s->grid_focus < s->n) { screen_options_for_title(&s->results[s->grid_focus]); return 1; }
        if (a == KEY_LONG_OK) return 0;
    }
    if (s->typing) return key_typing(s, a);
    if (!s->focus_grid) {   /* the search bar */
        switch (a) {
        case KEY_OK: open_keyboard(s); return 1;
        case KEY_DOWN:
            if (s->n) { s->focus_grid = 1; if (s->grid_focus >= s->n) s->grid_focus = 0; s->focus_t = 0; }
            else if (!s->kb.text[0] && s->n_recent) { s->focus_grid = 2; s->recent_focus = 0; }
            return 1;
        case KEY_BACKSPACE: open_keyboard(s); return key_typing(s, a);
        case KEY_BACK: return 0;
        default: return 1;
        }
    }
    s->focus_t = 0;
    if (s->focus_grid == 2) {   /* the recent searches list */
        switch (a) {
        case KEY_UP: if (s->recent_focus > 0) s->recent_focus--; else s->focus_grid = 0; return 1;
        case KEY_DOWN: if (s->recent_focus < s->n_recent) s->recent_focus++; return 1;   /* the last row clears */
        case KEY_BACK: s->focus_grid = 0; return 1;
        case KEY_OK:
            if (s->recent_focus == s->n_recent) { s->n_recent = 0; recent_save(s); s->focus_grid = 0; app_toast("Recent searches cleared"); return 1; }
            keyboard_init(&s->kb, 0, s->recent[s->recent_focus]);
            s->dirty = 0; s->focus_grid = 0; run_query(s);
            s->want_results = 1;   /* the results take the focus when they come */
            return 1;
        default: return 1;
        }
    }
    place(s, COLS);
    Cell at = s->grid_focus < s->n ? s->cell[s->grid_focus] : (Cell){ 0, 0 };
    switch (a) {
    case KEY_LEFT: if (at.col) s->grid_focus--; return 1;
    case KEY_RIGHT: if (s->grid_focus + 1 < s->n && s->cell[s->grid_focus + 1].row == at.row) s->grid_focus++; return 1;
    case KEY_UP: if (at.row) s->grid_focus = in_row(s, at.row - 1, at.col); else s->focus_grid = 0; return 1;
    case KEY_DOWN:   /* into the next row or group, the nearest card when it is short */
        if (at.row + 1 < s->n_rows) { s->grid_focus = in_row(s, at.row + 1, at.col); return 1; }
        return 0;
    case KEY_OK: if (s->grid_focus < s->n) { recent_add(s, s->last_query); app_push(screen_detail_create(&s->results[s->grid_focus])); } return 1;
    case KEY_BACK: s->focus_grid = 0; return 1;
    default: return 0;
    }
}

/* Typing on a real keyboard (or text from the test driver) opens the on-screen one too. */
static void text(Screen *sc, const char *utf8)
{
    Search *s = sc->st;
    if (!s->typing) open_keyboard(s);
    keyboard_text_input(&s->kb, utf8);
    s->dirty = 1; s->debounce = 0.6f;
    app_redraw();
}

static void destroy(Screen *sc)
{
    Search *s = sc->st;
    for (int i = 0; i < OPEN_MAX; i++) if (g_open[i] == s) g_open[i] = NULL;   /* its replies still on their way are dropped */
    if (g_person_live == s) g_person_live = NULL;
    if (s->ai_req) ai_cancel(s->ai_req);
    clear_results(s);
    close_keyboard(s);
    free(s->cell); free(s->row_y); free(s->row_first);
    free(s);
    free(sc);
}

Screen *screen_search_create(void)
{
    Screen *sc = calloc(1, sizeof *sc);
    Search *s = calloc(1, sizeof *s);
    keyboard_init(&s->kb, 0, NULL);
    recent_load(s);
    for (int i = 0; i < OPEN_MAX; i++) if (!g_open[i]) { g_open[i] = s; break; }   /* (past eight deep, its replies are just dropped) */
    sc->name = "search"; sc->st = s; sc->long_ok = 1;
    sc->update = update; sc->draw = draw; sc->key = key; sc->text = text; sc->destroy = destroy;
    return sc;
}

Screen *screen_search_create_query(const char *q)
{
    Screen *sc = screen_search_create();
    Search *s = sc->st;
    LOGI("search: \"%s\" from a title page", q);
    keyboard_init(&s->kb, 0, q);
    run_query(s);
    s->want_results = 1;   /* the results take the focus when they come */
    return sc;
}

/* A person from a title page: their own films and series from TMDB, not every title that
 * happens to contain their name. Without TMDB it falls back to searching the name. */
typedef struct { Search *owner; unsigned generation; } PersonReq;

static void on_person_titles(const MetaPreview *titles, int n, void *user)
{
    PersonReq *q = user;
    Search *s = q->owner;
    if (s != g_person_live || q->generation != s->generation) { free(q); return; }
    free(q);
    s->pending = 0;
    if (n < 0) { LOGI("search: no filmography from TMDB, searching the name"); s->last_query[0] = 0; run_query(s); s->want_results = 1; return; }
    clear_results(s);
    s->results = calloc((size_t)n + 1, sizeof *s->results);
    for (int i = 0; i < n; i++) meta_preview_copy(&s->results[s->n++], &titles[i]);
    regroup(s);
    snprintf(s->ai_note, sizeof s->ai_note, "Films and series with %s", s->kb.text);
    s->want_results = 1;
    LOGI("search: %d titles with %s", n, s->kb.text);
    app_redraw();
}

Screen *screen_search_create_person(const char *name, int person_id)
{
    Screen *sc = screen_search_create();
    Search *s = sc->st;
    LOGI("search: the films of \"%s\" from a title page", name);
    keyboard_init(&s->kb, 0, name);
    snprintf(s->last_query, sizeof s->last_query, "%s", name);   /* typing something else searches as usual */
    s->generation = ++g_search_gen;
    s->pending = 1;
    g_person_live = s;
    PersonReq *q = malloc(sizeof *q);
    q->owner = s; q->generation = s->generation;
    tmdb_person_titles(name, person_id, on_person_titles, q);
    return sc;
}
