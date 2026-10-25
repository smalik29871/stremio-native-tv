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
#include "rail.h"
#include "theme.h"
#include "widgets.h"
#include "screen_options.h"
#include <stdio.h>
#include <strings.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* Results sit beside the keyboard while typing and use the full width once it closes. */
enum { LIST_ROW = 58, LIST_MAX = 5, HERO_H = 400, COLS_TYPING = 5, COLS = 7, CELL_W = 196, CELL_H = 294, CELL_GAP = 28, FIELD_Y = 140, FIELD_H = 76 };

/* Results are grouped by type, each group under its own heading and starting a new row. */
enum { SECT_MAX = 4, SECT_H = 56 };
enum { HIT_FIELD = 0x1000, HIT_LIST = 0x2000, HIT_HERO = 0x3000, HIT_HERO_CARD = 0x3100, HIT_CHIP = 0x4000, HIT_GENRE = 0x5000, HIT_DISC = 0x6000 };   /* touch targets; a result is its index */
typedef struct { int row, col; } Cell;
typedef struct { int first_row, n; const char *label; } Section;

typedef struct Search Search;
enum { PICKS_MAX = 32, AI_TITLES = 16 };
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
    int enter_n; float enter_at;   /* when results came: they ease in */
    int grid_focus;
    int want_results;         /* a search was asked for: focus moves to the results when they arrive */
    int keep_recent;          /* ...and goes into the recent searches once it finds something */
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
    char pick_why[AI_TITLES][96];    /* the assistant's reason for each pick, by its rank */
    char refine[4][48]; int n_refine;   /* follow-ups it offers ("Only series", "Newer"), in the left list once it answered */
    char recent[10][96]; int n_recent, recent_focus;   /* shown while the field is empty */
    Cell *cell; int cell_cap;        /* where each result sits, from place() */
    float *row_y; int *row_first, n_rows, rows_cap;
    Section sect[SECT_MAX]; int n_sect;
    int groups_logged;
    char first_logged[64];        /* the leading result last logged */
    char sugg[5][96]; char sugg_sub[5][32]; int n_sugg;   /* titles for what is typed so far, and "Film · 1999" */
    int sugg_focus;               /* -1 on the keys, else the suggestion */
    int tvkb;                     /* typing with the TV's own keyboard, not the app's */
    int hero_btn;                 /* on the top result's buttons (focus_grid 3): which one */
    int ph_filter;                /* upright on a phone: All, Films, Series */
    float ph_y, ph_yv, ph_fy, ph_fh, ph_total;   /* ...the page's scroll, where the focus is on it, how long it is */
    UiPan ph_pan;
    char sugg_logged[96];
    char sugg_for[256]; int sugg_n_res, sugg_lib; unsigned sugg_gen;   /* what the suggestions were worked out for */
};
static void focus_results(Search *s);
static void phone_draw(Search *s);
static int phone_key(Search *s, KeyAction a);

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
    int headed = 1, i = 0;   /* a heading over every group, even one (Movies, Series) */
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
                if (fits && app_title_watched(metas[i].id) && p->rank < 50) p->rank += 50;   /* seen already: after the ones you have not */
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
        if (s->want_results && s->n && !s->typing && s->focus_grid == 0) { focus_results(s); s->want_results = 0; }
        if (s->keep_recent && s->n) { recent_add(s, s->last_query); s->keep_recent = 0; }   /* found late: remembered all the same */
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
    int only = -1;   /* the assistant's titles: the built-in catalog or Cinemeta knows them all, one request each instead of one per addon */
    for (int a = 0; pick && a < g_app.n_addons && only < 0; a++)
        if (addon_is_builtin(g_app.addons[a].transport_url) || (g_app.addons[a].manifest.id && strstr(g_app.addons[a].manifest.id, "cinemeta")))
            for (int c = 0; c < g_app.addons[a].manifest.n_catalogs; c++) if (addon_catalog_extra(&g_app.addons[a].manifest.catalogs[c], "search")) { only = a; break; }
    for (int a = 0; a < g_app.n_addons; a++) {
        const Addon *ad = &g_app.addons[a];
        if (only >= 0 && a != only) continue;
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
            const cJSON *why = cJSON_IsObject(it) ? cJSON_GetObjectItemCaseSensitive(it, "why") : NULL;
            snprintf(s->pick_why[n], sizeof s->pick_why[0], "%s", cJSON_IsString(why) ? why->valuestring : "");
            search_addons(s, name->valuestring, 1, n, cJSON_IsNumber(y) ? y->valueint : 0, kind);
            n++;
        }
    }
    const cJSON *sum = root ? cJSON_GetObjectItemCaseSensitive(root, "summary") : NULL, *ref = root ? cJSON_GetObjectItemCaseSensitive(root, "refine") : NULL, *r1;
    if (n && cJSON_IsString(sum) && sum->valuestring[0]) snprintf(s->ai_note, sizeof s->ai_note, "%s", sum->valuestring);   /* what it understood, over the results */
    s->n_refine = 0;
    cJSON_ArrayForEach(r1, ref) if (cJSON_IsString(r1) && r1->valuestring[0] && s->n_refine < 4) snprintf(s->refine[s->n_refine++], sizeof s->refine[0], "%s", r1->valuestring);
    if (n) LOGI("search: the assistant suggests %d title%s%s%s%s, %d way%s to refine", n, n == 1 ? "" : "s", s->ai_note[0] ? " (" : "", s->ai_note, s->ai_note[0] ? ")" : "", s->n_refine, s->n_refine == 1 ? "" : "s");
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

/* A description rather than a title: three words or more, or two with a word that asks
 * for a kind of thing ("korean thrillers", "like inception", "90s anime"). */
static int looks_like_a_request(const char *q)
{
    static const char *ASKS[] = { "like", "about", "similar", "movies", "movie", "films", "film", "shows", "show", "series", "comedies", "comedy",
        "thriller", "thrillers", "horror", "romance", "romantic", "romcom", "action", "drama", "dramas", "documentary", "documentaries", "anime",
        "cartoon", "cartoons", "bollywood", "tollywood", "kollywood", "kdrama", "korean", "japanese", "hindi", "spanish", "french", "turkish",
        "funny", "scary", "sad", "feelgood", "kids", "family", "best", "top", "classic", "classics", "80s", "90s", "2000s", "70s", "eighties",
        "nineties", "seventies", "starring", "mystery", "crime", "heist", "sci", "fi", "zombie", "space", "war", "western", NULL };
    char w[256];
    words_of(q, w, sizeof w);
    int words = 1, len = (int)strlen(q);
    for (const char *c = w; *c; c++) if (*c == ' ') words++;
    if (words >= 3 && len >= 12) return 1;
    if (words < 2) return 0;
    for (int i = 0; ASKS[i]; i++) if (has_word(w, ASKS[i], strlen(ASKS[i]), 0)) return 1;
    return 0;
}

static void run_query(Search *s)
{
    const char *q = s->kb.text;
    s->want_results = 0; s->keep_recent = 0;   /* the caller asks again when this search should take the focus */
    if (strcmp(q, s->last_query) == 0) return;
    snprintf(s->last_query, sizeof s->last_query, "%s", q);
    s->generation = ++g_search_gen;
    s->pending = 0;
    if (s->ai_req) ai_cancel(s->ai_req);   /* the last question is no longer wanted */
    s->ai_state = 0; s->ai_req = 0; s->ai_note[0] = 0; s->n_refine = 0;
    clear_results(s);
    if (strlen(q) < 2) return;
    if (ai_enabled() && looks_like_a_request(q)) {
        Pending *p = calloc(1, sizeof *p);
        p->owner = s; p->generation = s->generation;
        static const char *SYS = "You recommend films and series to a viewer who describes what they feel like watching. "
            "Keep to every constraint in the request exactly: language or film industry (Bollywood means Hindi films from India, Tollywood Telugu, "
            "a K-drama a Korean series, anime Japanese animation), country, era, genre, and film or series. Never suggest a title that breaks one of them. "
            "Reply with JSON only, no markdown: {\"summary\": \"Hindi comedies from the 1990s\", "
            "\"titles\": [{\"name\": \"title as on IMDb\", \"year\": 2010, \"type\": \"movie\", \"why\": \"a reason in under 10 words\"}], "
            "\"filters\": {\"type\": \"movie\", \"language\": \"hi\", \"country\": \"IN\", \"genres\": [\"Comedy\"], \"from\": 1990, \"to\": 2009}, "
            "\"refine\": [\"Only series\", \"Newer ones\", \"Funnier\", \"More like Andaz Apna Apna\"]} "
            "with up to 16 well-known titles, best match first; type is movie or series. summary says in under 8 words what you understood; "
            "refine gives up to 4 short follow-ups (under 4 words each) the viewer might want next. In filters, language is the ISO 639-1 code of the original "
            "language, country an ISO 3166-1 code, genres only from Action, Adventure, Animation, Comedy, Crime, Documentary, Drama, Family, Fantasy, "
            "History, Horror, Music, Mystery, Romance, Science Fiction, Thriller, War, Western; use null for whatever the request does not say. "
            "If the text is just a title or a person's name, return titles that match it and null filters.";
        s->ai_req = ai_ask(SYS, q, 1300, 1, on_ai_titles, p);
        s->ai_state = s->ai_req ? 1 : 0;
        if (s->ai_req) return;   /* a request: the assistant's titles, not every title with one of its words (they come if it has none) */
        free(p);
    }
    search_addons(s, q, 0, 0, 0, -1);
}

/* Suggestions while typing, as a TV's own keyboard gives: your recent searches that start
 * with what is typed, then the titles the addons found for it (best match first), each
 * once; never just what is already typed. */
/* Suggestions as you type, at once on every letter: titles this TV already knows (your
 * library, the titles on Home's rows) and your recent searches, then what the addons found
 * for the words so far; the closest match first, each title once, never just what is typed. */
typedef struct { const char *name, *sub; int tier, from; } Sugg;

static void sugg_offer(Sugg *c, int *n, int max, const char *name, const char *sub, const char *typed, int from)
{
    if (!name || !*name || *n >= max) return;
    char w[256], o[256], tw[256];
    words_of(name, w, sizeof w); words_of(typed, tw, sizeof tw);
    int tier = match_tier(name, typed);
    for (const char *q = tw; *q && tier > 2; ) {   /* every typed word the start of one of its words: "re" finds Redemption */
        size_t k = strcspn(q, " ");
        if (!has_word(w, q, k, 1)) return;
        q += k + (q[k] == ' ');
    }
    if (tier > 2) tier = 2;
    if (!strcmp(w, tw)) return;
    for (int k = 0; k < *n; k++) { words_of(c[k].name, o, sizeof o); if (!strcmp(o, w)) { if (sub && *sub && !(c[k].sub && *c[k].sub)) c[k].sub = sub; return; } }
    c[(*n)++] = (Sugg){ name, sub, tier, from };
}

static void suggest(Search *s)
{
    const char *t = s->kb.text;
    if (!strcmp(t, s->sugg_for) && s->n == s->sugg_n_res && g_app.library_version == s->sugg_lib && s->generation == s->sugg_gen) return;   /* nothing new to suggest from */
    snprintf(s->sugg_for, sizeof s->sugg_for, "%s", t);
    s->sugg_n_res = s->n; s->sugg_lib = g_app.library_version; s->sugg_gen = s->generation;
    char tw[256];
    words_of(t, tw, sizeof tw);
    Sugg c[96];
    int n = 0;
    static char subs[96][32];
    if (strlen(tw) >= 2) {
        for (int i = 0; i < s->n_recent; i++) sugg_offer(c, &n, 96, s->recent[i], "Recent", t, 0);
        for (int i = 0; i < s->n && i < 96 && n < 96; i++) {   /* what the addons found: kind and year (subs is by result: past the 96th it wrote over other memory) */
            const MetaPreview *m = &s->results[i];
            snprintf(subs[i], sizeof subs[0], "%s%s%s", m->type && !strcmp(m->type, "series") ? "Series" : "Film", m->release_info ? "  \xC2\xB7  " : "", m->release_info ? m->release_info : "");
            sugg_offer(c, &n, 96, m->name, subs[i], t, 1);
        }
        for (int i = 0; i < g_app.n_library && n < 96; i++)   /* known on this TV: no waiting for anyone */
            if (!g_app.library[i].removed && !g_app.library[i].temp)
                sugg_offer(c, &n, 96, g_app.library[i].name, g_app.library[i].type && !strcmp(g_app.library[i].type, "series") ? "Series" : "Film", t, 2);
        const AmbientItem *pool;
        int np = app_ambient_pool(&pool);
        for (int i = 0; i < np && n < 96; i++) sugg_offer(c, &n, 96, pool[i].name, "", t, 3);
    }
    for (int a = 1; a < n; a++)   /* the closest first; a tie keeps the order above */
        for (int b = a; b > 0 && c[b].tier < c[b - 1].tier; b--) { Sugg x = c[b]; c[b] = c[b - 1]; c[b - 1] = x; }
    int k = n < LIST_MAX ? n : LIST_MAX;
    for (int i = 0; i < k; i++) {
        snprintf(s->sugg[i], sizeof s->sugg[0], "%s", c[i].name);
        snprintf(s->sugg_sub[i], sizeof s->sugg_sub[0], "%s", c[i].sub ? c[i].sub : "");
    }
    s->n_sugg = k;
    if (s->sugg_focus >= k) s->sugg_focus = k - 1;
    if (k && strcmp(s->sugg[0], s->sugg_logged)) { snprintf(s->sugg_logged, sizeof s->sugg_logged, "%s", s->sugg[0]); LOGI("search: %d suggestion%s, first \"%s\"", k, k == 1 ? "" : "s", s->sugg[0]); }
}

static int tv_keyboard(void);

static void kb_set(Search *s, const char *text)   /* the compact keyboard, with this text */
{
    keyboard_init(&s->kb, 0, text);
    s->kb.compact = 1;
}

/* The phone page offers the search box while this screen is up (not only while typing), and
 * its QR code shows where there are no results yet. */
static void phone_search_open(Search *s)
{
    phone_prompt("Search", "Type a title, a person, or what you feel like watching", s->kb.text);
    if (s->qr_ready) return;
    char url[96]; uint8_t tmp[qrcodegen_BUFFER_LEN_MAX];
    s->qr_ready = phone_url(url, sizeof url) && qrcodegen_encodeText(url, tmp, s->qr, qrcodegen_Ecc_MEDIUM, qrcodegen_VERSION_MIN, 6, qrcodegen_Mask_AUTO, true);
    snprintf(s->phone_url, sizeof s->phone_url, "%s", s->qr_ready ? url : "");
}

static void open_keyboard(Search *s)
{
    s->typing = 1; s->focus_grid = 0; s->want_results = 0; s->sugg_focus = -1;
    s->tvkb = tv_keyboard();
    if (s->tvkb) { input_text_system(1); LOGI("search: typing with the TV's keyboard"); }
    else input_text(1);
    phone_search_open(s);
}

static void close_keyboard(Search *s)
{
    if (!s->typing) return;
    s->typing = 0;
    if (s->tvkb) { s->tvkb = 0; input_text_system(0); }
    else input_text(0);
    if (SNTV_PHONE) phone_prompt(NULL, NULL, NULL);
    else phone_search_open(s);   /* the phone can still search from here */
}

/* The TV's own keyboard for Search (Settings; on by default on the TV, tests ask for it). */
static int tv_keyboard(void)
{
    if (SNTV_PHONE) return 1;   /* a phone types with its own */
#ifdef SNTV_PLATFORM_WEBOS
    return store_get_int("tvKeyboard", 1);
#else
    return store_get_int("tvKeyboard", 0);   /* the desktop has a real keyboard */
#endif
}

static int cols(const Search *s) { (void)s; return COLS_TYPING; }

/* Layout A: the search bar, a list (suggestions, or recent searches) and the keyboard down the
 * left; the top result as a big card on the right with the rest below it. */
static float left_w(const Search *s) { return keyboard_width_of(&s->kb); }
static float right_x(const Search *s) { return THEME_MARGIN + left_w(s) + 64; }
static float grid_top(void) { return HERO_H + 56; }   /* below the top result, in the right column's scroll */

/* The left list: suggestions for what is typed, else the recent searches (and a row to clear them). */
static int refining(const Search *s) { return !s->typing && s->ai_state == 2 && s->n_refine; }

static int list_n(Search *s)
{
    if (refining(s)) return s->n_refine;
    if (s->kb.text[0]) return s->n_sugg;
    return s->n_recent ? s->n_recent + 1 : 0;
}

static int hero_buttons(const Search *s)   /* Play, Details, Sources for a film; Episodes, Details for a series */
{
    return s->n && s->results[0].type && !strcmp(s->results[0].type, "series") ? 2 : 3;
}

static void focus_results(Search *s)   /* the results take the focus: the top result first */
{
    if (!s->n) return;
    s->focus_grid = 3; s->hero_btn = 0; s->focus_t = 0;
}

static int headed_row(const Search *s, int row)
{
    if (!s->n_sect) return 0;
    for (int k = 0; k < s->n_sect; k++) if (s->sect[k].first_row == row) return 1;
    return 0;
}

static void ph_focus_at(Search *s, int on, float y, float h) { if (on) { s->ph_fy = y; s->ph_fh = h; } }

static void update(Screen *sc, float dt)
{
    Search *s = sc->st;
    if (SNTV_PHONE) {   /* the page follows the focus, the field and the kinds at the top */
        float view = SNTV_UI_H - PHONE_TABS_H, want = s->ph_y;
        if (s->ph_fy + s->ph_fh > want + view - 30) want = s->ph_fy + s->ph_fh - view + 30;
        if (s->ph_fy - 30 < want) want = s->ph_fy - 30;
        if (!s->focus_grid || want < 0) want = 0;
        ui_pan(&s->ph_pan, &s->ph_y, want, s->ph_total - view + 30, dt, 14);
    }
    char got[256];
    if ((s->typing || !SNTV_PHONE) && phone_take(got, sizeof got)) {   /* sent from the phone: search it right away */
        kb_set(s, got);
        close_keyboard(s);
        LOGI("search: \"%s\" from a phone", got);
        s->dirty = 0; run_query(s);
        app_toast("Received from your phone");
        app_redraw();
    }
    place(s, cols(s));
    suggest(s);
    /* the right column scrolls as one: the top result, then the rows under it */
    float target = 0;
    if (s->focus_grid == 1 && s->grid_focus < s->n) {
        int row = s->cell[s->grid_focus].row;
        float see = SNTV_UI_H - FIELD_Y - 40, top = grid_top() + s->row_y[row] - (headed_row(s, row) ? SECT_H : 0);
        float bottom = grid_top() + s->row_y[row] + CELL_H + 90 - see;
        target = bottom > 0 ? bottom : 0;
        if (row > 0 && target < top - 120) target = top - 120;   /* a row down: the one above stays in sight */
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
    ui_hit(x, y, fw, FIELD_H, HIT_FIELD);
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
    float w = keyboard_width_of(&s->kb);
    if (!s->qr_ready) return;
    float qs = 190;
    if (y + qs > SNTV_UI_H - 30) return;
    ui_qr(s->qr, x, y, qs);
    font_draw("Or type on your phone", x + qs + 30, y + 30, FONT_S | FONT_B, THEME_TEXT);
    font_draw_wrapped("Scan the code, type or paste, then Send. It searches straight away.", x + qs + 30, y + 72, FONT_XS, THEME_TEXT_DIM, w - qs - 30, 3);
    font_draw(s->phone_url, x + qs + 30, y + qs - 30, FONT_XS, THEME_TEXT_MUTED);
}

static void cover(int iw, int ih, float w, float h, float *u0, float *v0, float *u1, float *v1)
{
    float ia = ih > 0 ? (float)iw / ih : 1, ba = w / h;
    *u0 = 0; *v0 = 0; *u1 = 1; *v1 = 1;
    if (ia > ba) { float k = ba / ia; *u0 = (1 - k) / 2; *u1 = *u0 + k; }
    else { float k = ia / ba; *v0 = (1 - k) / 2; *v1 = *v0 + k; }
}

static const char *hero_label(const Search *s, int b)
{
    int series = hero_buttons(s) == 2;
    static const char *FILM[] = { "Play", "Details", "Sources" }, *SHOW[] = { "Episodes", "Details" };
    return series ? SHOW[b] : FILM[b];
}

/* The best match, big: its picture, what it is, and Play straight from here. */
static void draw_hero(const Search *s, float x, float y, float w, float h)
{
    const MetaPreview *m = &s->results[0];
    int foc = s->focus_grid == 3;
    font_draw(s->n_picks ? "AI PICK" : "TOP RESULT", x, y - 40, FONT_XS | FONT_B, THEME_TEXT_MUTED);
    char b[512];
    const char *bg = app_art_background(m, b, sizeof b);
    int iw = 0, ih = 0;
    unsigned tex = bg ? image_get(bg, &iw, &ih) : 0;
    if (tex) {
        float u0, v0, u1, v1;
        cover(iw, ih, w, h, &u0, &v0, &u1, &v1);
        gfx_rtex_uv(tex, x, y, w, h, R_PANEL, u0, v0, u1, v1, RGBA(255, 255, 255, 255), 0);
    } else gfx_rrect_grad_h(x, y, w, h, R_PANEL, COLOR_ALPHA(THEME_ACCENT, 0x70), COLOR_ALPHA(THEME_ACCENT2, 0x40));
    gfx_rrect_grad_h(x, y, w * 0.8f, h, R_PANEL, RGBA(0, 0, 0, 0xD8), RGBA(0, 0, 0, 0));   /* the words stay readable on any picture */
    if (foc) gfx_rrect_ring(x, y, w, h, R_PANEL, 3, COLOR_ALPHA(THEME_TEXT, 0x90));
    ui_hit(x, y, w, h, HIT_HERO_CARD);
    float tx = x + 44, ty = y + h - 222;
    font_draw_fit(m->name ? m->name : "", tx, ty, FONT_XL | FONT_B, THEME_TEXT, w - 88);
    char meta[200] = "";
    size_t n = 0;
    const char *kind = m->type && !strcmp(m->type, "series") ? "Series" : m->type && !strcmp(m->type, "movie") ? "Film" : NULL;
    const char *parts[6] = { m->release_info, kind, m->runtime, m->genres.n ? m->genres.v[0] : NULL, m->genres.n > 1 ? m->genres.v[1] : NULL, NULL };
    for (int k = 0; k < 5; k++)
        if (parts[k] && *parts[k] && n < sizeof meta - 8) n += (size_t)snprintf(meta + n, sizeof meta - n, "%s%s", n ? "  \xC2\xB7  " : "", parts[k]);
    if (m->imdb_rating && *m->imdb_rating && n < sizeof meta - 16) snprintf(meta + n, sizeof meta - n, "%sIMDb %s", n ? "  \xC2\xB7  " : "", m->imdb_rating);
    font_draw_fit(meta, tx, ty + 84, FONT_S, THEME_TEXT_DIM, w - 88);
    int rank = s->n_picks && s->pick_rank[0] < 100 ? s->pick_rank[0] % 50 : -1;   /* the assistant's reason for it (TMDB's own fill-ins have none) */
    if (rank >= 0 && rank < AI_TITLES && s->pick_why[rank][0]) font_draw_fit(s->pick_why[rank], tx, ty - 40, FONT_S | FONT_B, THEME_ACCENT2, w - 88);
    if (app_title_watched(m->id)) ui_chip("Watched", x + w - 150, y + 24, COLOR_ALPHA(THEME_PANEL, 0xD0), THEME_OK);
    float bx = tx, by = y + h - 96;
    for (int k = 0; k < hero_buttons(s); k++) {
        float pw = ui_pill(hero_label(s, k), k == 0, ICON_PLAY, bx, by, 64, k == 0 ? PILL_PRIMARY : PILL_SECONDARY, foc && s->hero_btn == k);
        ui_hit(bx, by, pw, 64, HIT_HERO + k);
        bx += pw + 16;
    }
}

/* Down the left: what is typed suggests, else the recent searches (and a row to clear them). */
static float draw_list(const Search *s, float x, float y, float w)
{
    int foc = s->typing ? s->sugg_focus : s->focus_grid == 2 ? s->recent_focus : -1;
    if (refining(s)) {   /* the assistant's follow-ups */
        font_draw("REFINE", x, y + 4, FONT_XS | FONT_B, THEME_TEXT_MUTED);
        float ry = y + 36;
        for (int i = 0; i < s->n_refine; i++, ry += LIST_ROW) {
            int on = foc == i;
            ui_hit(x, ry, w, LIST_ROW - 6, HIT_LIST + i);
            if (on) gfx_rrect(x, ry, w, LIST_ROW - 6, R_KEY, THEME_FOCUS);
            ui_icon(ICON_STAR, x + 26, ry + (LIST_ROW - 6) / 2, 20, on ? THEME_ON_FOCUS : THEME_ACCENT2);
            font_draw_fit(s->refine[i], x + 52, ry + (LIST_ROW - 6 - font_height(FONT_S | FONT_B)) / 2, FONT_S | FONT_B, on ? THEME_ON_FOCUS : THEME_TEXT, w - 64);
        }
        return ry - y;
    }
    int n = s->kb.text[0] ? s->n_sugg : s->n_recent ? s->n_recent + 1 : 0;
    if (!n) return 0;
    if (!s->kb.text[0]) font_draw("RECENT", x, y + 4, FONT_XS | FONT_B, THEME_TEXT_MUTED);
    float ry = y + (s->kb.text[0] ? 0 : 36);
    for (int i = 0; i < n; i++, ry += LIST_ROW) {
        int on = foc == i;
        ui_hit(x, ry, w, LIST_ROW - 6, HIT_LIST + i);
        if (on) gfx_rrect(x, ry, w, LIST_ROW - 6, R_KEY, THEME_FOCUS);
        Color fg = on ? THEME_ON_FOCUS : THEME_TEXT;
        const char *t = s->kb.text[0] ? s->sugg[i] : i < s->n_recent ? s->recent[i] : "Clear recent searches";
        const char *sub = s->kb.text[0] ? s->sugg_sub[i] : NULL;
        float sw = sub && *sub ? font_measure(sub, FONT_XS) + 20 : 0;
        if (!s->kb.text[0] && i < s->n_recent) ui_icon(ICON_CLOCK, x + 26, ry + (LIST_ROW - 6) / 2, 22, on ? THEME_ON_FOCUS : THEME_TEXT_MUTED);
        float lx = x + (s->kb.text[0] ? 18 : 52);
        font_draw_fit(t, lx, ry + (LIST_ROW - 6 - font_height(FONT_S | FONT_B)) / 2, FONT_S | (i < (s->kb.text[0] ? n : s->n_recent) ? FONT_B : 0),
                      i < (s->kb.text[0] ? n : s->n_recent) ? fg : on ? fg : THEME_TEXT_DIM, w - (lx - x) - sw - 12);
        if (sw) font_draw(sub, x + w - sw + 4, ry + (LIST_ROW - 6 - font_height(FONT_XS)) / 2, FONT_XS, on ? THEME_ON_FOCUS : THEME_TEXT_MUTED);
    }
    return ry - y;
}

static void draw(Screen *sc)
{
    Search *s = sc->st;
    if (SNTV_PHONE) { phone_draw(s); return; }
    const MetaPreview *fp = s->focus_grid == 1 && s->grid_focus < s->n ? &s->results[s->grid_focus] : s->focus_grid == 3 && s->n ? &s->results[0] : NULL;
    char buf[512];
    const char *bg = fp ? app_art_background(fp, buf, sizeof buf) : NULL;
    ui_background(bg ? image_tint(bg) : 0);
    if (g_theme.id == THEME_APERTURE) font_draw("Find your next film", THEME_MARGIN, 26, FONT_XL | FONT_ALT, THEME_TEXT);
    else font_draw("Search", THEME_MARGIN, 52, FONT_L | FONT_B, THEME_TEXT);

    /* the left column */
    float x = THEME_MARGIN, y = FIELD_Y, lw = left_w(s);
    draw_field(s, x, y, lw);
    float ly = y + FIELD_H + 20;
    if (s->typing && s->tvkb) {
        font_draw("Typing with the TV's keyboard  \xC2\xB7  Enter searches", x, ly, FONT_XS, THEME_TEXT_MUTED);
        ly += 40;
    }
    ly += draw_list(s, x, ly, lw);
    if (s->typing && !s->tvkb) {
        float ky = ly + 16;
        keyboard_draw(&s->kb, x, ky, s->sugg_focus < 0);
        font_draw(s->n_sugg ? "Done searches  \xC2\xB7  Up for suggestions" : "Done searches  \xC2\xB7  Back closes the keyboard", x, ky + keyboard_height_of(&s->kb) + 18, FONT_XS, THEME_TEXT_MUTED);
        draw_phone(s, x, ky + keyboard_height_of(&s->kb) + 64);
    } else if (!s->typing && !s->focus_grid) {
        font_draw("OK to type  \xC2\xB7  Right for the results", x, ly + 16, FONT_XS, THEME_TEXT_MUTED);
    }

    /* the right column: the top result, then every result by kind */
    float gx = right_x(s), grid_w = SNTV_UI_W - THEME_MARGIN - gx, top = FIELD_Y;
    int nc = cols(s);
    if ((s->pending || s->ai_state == 1) && !s->n) {
        ui_skeleton(gx, top + 40, grid_w, HERO_H, R_PANEL);
        for (int i = 0; i < nc; i++) ui_skeleton(gx + i * (CELL_W + CELL_GAP), top + grid_top(), CELL_W, CELL_H, R_CARD);
        if (s->ai_state == 1) font_draw("Asking the assistant\xE2\x80\xA6", gx, top, FONT_XS | FONT_B, THEME_TEXT_MUTED);
        return;
    }
    if (!s->n && s->qr_ready) {   /* nothing yet: search from a phone instead */
        float cx = gx + grid_w / 2, y = top + 40, qs = 260;
        font_draw_center(strlen(s->last_query) >= 2 ? "Nothing found" : "Find something to watch", cx, y, g_theme.id == THEME_APERTURE ? FONT_XL | FONT_ALT : FONT_L | FONT_B, THEME_TEXT);
        font_draw_center(s->ai_note[0] ? s->ai_note : strlen(s->last_query) >= 2 ? "Try another title, or check your catalog addons." : ai_enabled() ? "Type a title, or describe it: \"something funny with robots\"." : "Results come from every catalog addon that supports search.",
                         cx, y + 58, FONT_S, THEME_TEXT_DIM);
        float py = y + 150;
        ui_panel(cx - qs / 2 - 40, py - 30, qs + 80, qs + 190, R_PANEL, COLOR_ALPHA(THEME_TEXT, 0x0C));
        ui_qr(s->qr, cx - qs / 2, py, qs);
        font_draw_center("Search from your phone", cx, py + qs + 28, FONT_M | FONT_B, THEME_TEXT);
        font_draw_center("Scan, type, Send: it searches here", cx, py + qs + 80, FONT_S, THEME_TEXT_DIM);
        font_draw_center(s->phone_url, cx, py + qs + 120, FONT_XS, THEME_TEXT_MUTED);
        return;
    }
    if (!s->n) {
        float cx = gx + grid_w / 2, cy = top + 260;
        gfx_circle(cx, cy, 80, COLOR_ALPHA(THEME_TEXT, 0x10));
        ui_icon(ICON_SEARCH, cx, cy, 70, THEME_TEXT_MUTED);
        font_draw_center(strlen(s->last_query) >= 2 ? "Nothing found" : "Find something to watch", cx, cy + 120, g_theme.id == THEME_APERTURE ? FONT_XL | FONT_ALT : FONT_L | FONT_B, THEME_TEXT);
        font_draw_center(s->ai_note[0] ? s->ai_note : strlen(s->last_query) >= 2 ? "Try another title, or check your catalog addons." : ai_enabled() ? "Type a title, or describe it: \"something funny with robots\"." : "Results come from every catalog addon that supports search.",
                         cx, cy + 178, FONT_S, THEME_TEXT_DIM);
        return;
    }
    gfx_clip_push(gx - 30, top - 50, grid_w + 60, SNTV_UI_H - top + 50);
    float off = top - s->grid_scroll;
    draw_hero(s, gx, off + 40, grid_w, HERO_H);
    char hdr[160];
    if (s->ai_note[0] && s->n_picks) snprintf(hdr, sizeof hdr, "%s", s->ai_note);
    else snprintf(hdr, sizeof hdr, "%d result%s%s", s->n, s->n == 1 ? "" : "s", s->ai_state == 1 ? "  \xC2\xB7  asking the assistant" : "");
    float hw = font_measure(hdr, FONT_XS | FONT_B);
    font_draw(hdr, gx + grid_w - hw, off, FONT_XS | FONT_B, THEME_TEXT_MUTED);
    if (s->pending || s->ai_state == 1) ui_spinner(gx + grid_w - hw - 28, off + 10, 10);
    float gy = off + grid_top(), row_h = CELL_H + 80, peek_x = 0, peek_y = 0;
    place(s, nc);
    for (int k = 0; k < s->n_sect; k++) {   /* a heading per kind: Movies, Series */
        float hy = gy + s->row_y[s->sect[k].first_row] - SECT_H;
        if (hy > SNTV_UI_H || hy + SECT_H < top - 50) continue;
        char h[64]; snprintf(h, sizeof h, "%s  \xC2\xB7  %d", s->sect[k].label, s->sect[k].n);
        font_draw(h, gx, hy + 6, FONT_M | FONT_B, THEME_TEXT);
    }
    float room = 0; int room_right = 1, room_row = -1;
    if (s->focus_grid == 1 && s->grid_focus < s->n) {
        float sc2 = ui_focus_scale(g_app.fx_light ? 1.0f : s->focus_t), w2 = CELL_W * sc2;
        float fx = gx + s->cell[s->grid_focus].col * (CELL_W + CELL_GAP) - (w2 - CELL_W) / 2;
        room = ui_peek_room(&s->peek, &s->results[s->grid_focus], fx, w2, CELL_H * sc2, gx - 30, gx + grid_w + 30, &room_right);
        room_row = s->cell[s->grid_focus].row;
    }
    ui_list_enter(s->n, &s->enter_n, &s->enter_at);
    for (int pass = 0; pass < 2; pass++)
        for (int i = 0; i < s->n; i++) {
            int r = s->cell[i].row, c = s->cell[i].col;
            float ce = ui_enter(s->enter_at, i);
            float px = gx + c * (CELL_W + CELL_GAP), py = gy + s->row_y[r] + 26 * (1 - ce);
            if (room > 0 && r == room_row && (room_right ? i > s->grid_focus : i < s->grid_focus)) px += room_right ? room : -room;
            if (py > SNTV_UI_H || py + row_h < top - 50) continue;
            int foc = s->focus_grid == 1 && s->grid_focus == i;
            if ((pass == 1) != foc) continue;
            gfx_set_alpha(ce);
            char pb[512];
            if (foc) { peek_x = px; peek_y = py; }
            ui_card(app_art_poster(&s->results[i], pb, sizeof pb), px, py, CELL_W, CELL_H, foc ? (g_app.fx_light ? 1 : s->focus_t) : 0,
                    s->focus_grid == 1 && !foc ? 0.3f : 0, s->results[i].name, 0);
            ui_hit(px, py, CELL_W, CELL_H, i);
            if (i < s->n_picks) { gfx_circle(px + CELL_W - 22, py + 22, 16, COLOR_ALPHA(THEME_PANEL, 0xD0)); ui_icon(ICON_STAR, px + CELL_W - 22, py + 22, 18, THEME_ACCENT2); }
            if (app_title_watched(s->results[i].id)) { gfx_circle(px + 22, py + 22, 16, COLOR_ALPHA(THEME_PANEL, 0xD0)); ui_icon(ICON_CHECK, px + 22, py + 22, 18, THEME_OK); }
            if (g_theme.id == THEME_APERTURE) ui_spaced_caps(s->results[i].name, px, py + CELL_H + (foc ? 24 : 14), FONT_S | FONT_ALT, 3, foc ? THEME_TEXT : THEME_TEXT_DIM, CELL_W);
            else font_draw_fit(s->results[i].name, px, py + CELL_H + (foc ? 24 : 12), FONT_XS | FONT_B, foc ? THEME_TEXT : THEME_TEXT_DIM, CELL_W);
            if (foc && ui_hold_hint(px, py + CELL_H + 52, CELL_W)) {}
            else if (s->results[i].release_info) font_draw(s->results[i].release_info, px, py + CELL_H + (foc ? 50 : 38), FONT_XS, THEME_TEXT_MUTED);
        }
    gfx_set_alpha(1);
    if (s->focus_grid == 1 && s->grid_focus < s->n) {
        float f = g_app.fx_light ? 1 : s->focus_t, sc2 = ui_focus_scale(f), w2 = CELL_W * sc2, h2 = CELL_H * sc2;
        ui_peek_draw(&s->peek, &s->results[s->grid_focus], peek_x - (w2 - CELL_W) / 2, peek_y - (h2 - CELL_H) / 2 - ui_focus_lift(f), w2, h2, gx - 30, gx + grid_w + 30);
    }
    gfx_clip_pop();
}

/* Take a line of the left list: a suggestion or a recent search is searched at once; the
 * last recent row clears them. */
static void take_list(Search *s, int i)
{
    if (refining(s)) {   /* asked again, the request kept: "bollywood comedies, only series" */
        char q[256];
        snprintf(q, sizeof q, "%.180s, %.60s", s->last_query, s->refine[i]);
        for (char *c = q + strlen(s->last_query) + 2; *c; c++) if (*c >= 'A' && *c <= 'Z') *c = (char)(*c + 32);
        LOGI("search: refine \"%s\"", s->refine[i]);
        kb_set(s, q);
        s->dirty = 0; s->focus_grid = 0; run_query(s);
        s->want_results = 1;
        return;
    }
    if (!s->kb.text[0] && i == s->n_recent) { s->n_recent = 0; recent_save(s); s->focus_grid = 0; app_toast("Recent searches cleared"); return; }
    char pick[256];
    snprintf(pick, sizeof pick, "%s", s->kb.text[0] ? s->sugg[i] : s->recent[i]);
    if (s->kb.text[0]) LOGI("search: suggestion \"%s\" taken", pick);
    kb_set(s, pick);
    close_keyboard(s);
    s->dirty = 0; s->focus_grid = 0; run_query(s);
    if (s->n) { focus_results(s); recent_add(s, s->kb.text); }
    else s->want_results = s->keep_recent = 1;   /* the results take the focus when they come */
}

static void searched(Search *s)   /* Done (or the TV keyboard's Enter): search now, the results take the focus */
{
    close_keyboard(s);
    s->dirty = 0; run_query(s);
    if (s->n) { focus_results(s); recent_add(s, s->kb.text); }
    else s->want_results = s->keep_recent = 1;
}

/* The keyboard is open: type, Done searches, Up reaches the suggestions, Right off the edge
 * the top result, Back closes it. */
static int key_typing(Search *s, KeyAction a)
{
    if (a == KEY_BACK) { close_keyboard(s); return 1; }
    if (s->tvkb) {   /* the TV's keyboard types (text()); its Enter comes as OK */
        if (a == KEY_BACKSPACE) { keyboard_key(&s->kb, KEY_BACKSPACE); s->dirty = 1; s->debounce = 0.5f; }
        else if (a == KEY_OK) searched(s);
        else if ((a == KEY_DOWN || a == KEY_RIGHT) && s->n) { close_keyboard(s); focus_results(s); recent_add(s, s->kb.text); }
        return 1;
    }
    if (s->sugg_focus >= 0) {   /* on the suggestions, above the keys */
        if (a == KEY_UP && s->sugg_focus > 0) s->sugg_focus--;
        else if (a == KEY_DOWN) { if (++s->sugg_focus >= s->n_sugg) s->sugg_focus = -1; }
        else if (a == KEY_RIGHT && s->n) { close_keyboard(s); focus_results(s); }
        else if (a == KEY_OK && s->sugg_focus < s->n_sugg) take_list(s, s->sugg_focus);
        return 1;
    }
    int last = (int)(keyboard_width_of(&s->kb) / 80);   /* the right-hand column of keys */
    if (a == KEY_RIGHT && s->kb.col >= last && s->n) { close_keyboard(s); focus_results(s); recent_add(s, s->kb.text); return 1; }
    if (a == KEY_BACKSPACE) { keyboard_key(&s->kb, KEY_BACKSPACE); s->dirty = 1; s->debounce = 0.5f; return 1; }
    KeyboardResult r = keyboard_key(&s->kb, a);
    if (r == KB_LEAVE_UP && s->n_sugg) { s->sugg_focus = s->n_sugg - 1; return 1; }   /* the nearest suggestion first */
    if (r == KB_CHANGED) { s->dirty = 1; s->debounce = 0.35f; }
    else if (r == KB_DONE) searched(s);
    return 1;
}

static void hero_open(Search *s)   /* the top result's buttons */
{
    const MetaPreview *m = &s->results[0];
    int series = hero_buttons(s) == 2;
    recent_add(s, s->last_query);
    LOGI("search: top result %s, %s", m->id, hero_label(s, s->hero_btn));
    if (series || s->hero_btn == 1) { app_push(screen_detail_create(m)); return; }
    if (s->hero_btn == 0) app_push(screen_streams_create_play(m, m->id, NULL));
    else app_push(screen_streams_create(m, m->id, NULL));
}

static int key(Screen *sc, KeyAction a)
{
    Search *s = sc->st;
    app_redraw();
    if (a == KEY_LONG_OK || a == KEY_YELLOW) {
        if (s->focus_grid == 1 && s->grid_focus < s->n) { screen_options_for_title(&s->results[s->grid_focus]); return 1; }
        if (s->focus_grid == 3 && s->n) { screen_options_for_title(&s->results[0]); return 1; }
        if (a == KEY_LONG_OK) return 0;
    }
    if (SNTV_PHONE) { int r = phone_key(s, a); if (r >= 0) return r; }
    if (s->typing) return key_typing(s, a);
    int nl = list_n(s);
    if (!s->focus_grid) {   /* the search bar */
        switch (a) {
        case KEY_OK: open_keyboard(s); return 1;
        case KEY_DOWN: if (nl) { s->focus_grid = 2; s->recent_focus = 0; } else if (s->n) focus_results(s); return 1;
        case KEY_RIGHT: if (s->n) focus_results(s); return 1;
        case KEY_BACKSPACE: open_keyboard(s); return key_typing(s, a);
        case KEY_BACK: return 0;
        default: return 1;
        }
    }
    s->focus_t = 0;
    if (s->focus_grid == 2) {   /* the left list: suggestions, or the recent searches */
        switch (a) {
        case KEY_UP: if (s->recent_focus > 0) s->recent_focus--; else s->focus_grid = 0; return 1;
        case KEY_DOWN: if (s->recent_focus < nl - 1) s->recent_focus++; return 1;
        case KEY_RIGHT: if (s->n) focus_results(s); return 1;
        case KEY_BACK: s->focus_grid = 0; return 1;
        case KEY_OK: if (s->recent_focus < nl) take_list(s, s->recent_focus); return 1;
        default: return 1;
        }
    }
    if (s->focus_grid == 3) {   /* the top result */
        if (!s->n) { s->focus_grid = 0; return 1; }
        switch (a) {
        case KEY_LEFT: if (s->hero_btn > 0) s->hero_btn--; else s->focus_grid = nl ? 2 : 0; return 1;
        case KEY_RIGHT: if (s->hero_btn < hero_buttons(s) - 1) s->hero_btn++; return 1;
        case KEY_UP: s->focus_grid = 0; return 1;
        case KEY_DOWN: place(s, cols(s)); if (s->n) { s->focus_grid = 1; s->grid_focus = 0; } return 1;
        case KEY_OK: hero_open(s); return 1;
        case KEY_BACK: s->focus_grid = 0; return 1;
        default: return 0;
        }
    }
    place(s, cols(s));
    Cell at = s->grid_focus < s->n ? s->cell[s->grid_focus] : (Cell){ 0, 0 };
    switch (a) {
    case KEY_LEFT: if (at.col) s->grid_focus--; else s->focus_grid = nl ? 2 : 0; return 1;
    case KEY_RIGHT: if (s->grid_focus + 1 < s->n && s->cell[s->grid_focus + 1].row == at.row) s->grid_focus++; return 1;
    case KEY_UP: if (at.row) s->grid_focus = in_row(s, at.row - 1, at.col); else { s->focus_grid = 3; s->hero_btn = 0; } return 1;
    case KEY_DOWN:   /* into the next row or group, the nearest card when it is short */
        if (at.row + 1 < s->n_rows) { s->grid_focus = in_row(s, at.row + 1, at.col); return 1; }
        return 0;
    case KEY_OK: if (s->grid_focus < s->n) { recent_add(s, s->last_query); app_push(screen_detail_create(&s->results[s->grid_focus])); } return 1;
    case KEY_BACK: s->focus_grid = 0; return 1;
    default: return 0;
    }
}

/* ---- upright on a phone: the field, the kinds, then one list (what is typed suggests, the
 * recent searches, or the results), the phone's own keyboard ---- */

static int ph_shown(const Search *s, int i)
{
    const char *t = s->results[i].type;
    return !s->ph_filter || (t && !strcmp(t, s->ph_filter == 1 ? "movie" : "series"));
}

static void ph_line(const char *text, const char *sub, Icon icon, float x, float y, float w, float h, int foc, int hit)
{
    if (foc) { ui_focus_under(x - 12, y, w + 24, h, R_PANEL, 1); gfx_rrect(x - 12, y, w + 24, h, R_PANEL, THEME_FOCUS); }
    Color fg = foc ? THEME_ON_FOCUS : THEME_TEXT;
    float tx = x;
    if ((int)icon >= 0) { ui_icon(icon, x + 22, y + h / 2, 30, foc ? fg : THEME_TEXT_MUTED); tx += 62; }
    font_draw_fit(text, tx, y + (h - font_height(FONT_M | FONT_B)) / 2, FONT_M | FONT_B, fg, w - (tx - x) - (sub ? font_measure(sub, FONT_S) + 24 : 0));
    if (sub && *sub) font_draw(sub, x + w - font_measure(sub, FONT_S), y + (h - font_height(FONT_S)) / 2, FONT_S, foc ? fg : THEME_TEXT_MUTED);
    ui_hit(x - 12, y, w + 24, h, hit);
}

/* Upright on a phone with nothing typed: Discover, as phone apps have it: genres that open
 * Explore on them, and the titles Home's rows already brought, as posters (the kind chips apply). */
static const char *const DISC_GENRES[] = { "Action", "Comedy", "Drama", "Thriller", "Sci-Fi", "Horror",
                                           "Romance", "Animation", "Crime", "Documentary", "Family", "Fantasy" };
#define N_DISC_GENRES ((int)(sizeof DISC_GENRES / sizeof DISC_GENRES[0]))
#define DISC_MAX 18

static int disc_titles(const Search *s, const AmbientItem **out, int *idx)
{
    const AmbientItem *pool;
    int np = app_ambient_pool(&pool), n = 0;
    *out = pool;
    for (int i = 0; i < np && n < DISC_MAX; i++) {
        const AmbientItem *a = &pool[i];
        if (!a->poster[0] || !a->type[0]) continue;
        if (s->ph_filter == 1 && strcmp(a->type, "movie")) continue;
        if (s->ph_filter == 2 && strcmp(a->type, "series")) continue;
        idx[n++] = i;
    }
    return n;
}

static float disc_draw(const Search *s, float x, float y, float w)
{
    font_draw("BROWSE BY GENRE", x, y, FONT_XS | FONT_B, THEME_TEXT_MUTED);
    y += 48;
    float cx = x, ch = 72;
    for (int k = 0; k < N_DISC_GENRES; k++) {   /* the chips wrap onto as many lines as they need */
        float cw = font_measure(DISC_GENRES[k], FONT_S | FONT_B) + 56;
        if (cx > x && cx + cw > x + w) { cx = x; y += ch + 16; }
        gfx_rrect(cx, y, cw, ch, ch / 2, COLOR_ALPHA(THEME_TEXT, 0x14));
        gfx_rrect_ring(cx, y, cw, ch, ch / 2, 2, COLOR_ALPHA(THEME_TEXT, 0x1C));
        font_draw(DISC_GENRES[k], cx + 28, y + (ch - font_height(FONT_S | FONT_B)) / 2, FONT_S | FONT_B, THEME_TEXT);
        ui_hit(cx, y, cw, ch, HIT_GENRE + k);
        cx += cw + 16;
    }
    y += ch + 56;
    const AmbientItem *pool;
    int idx[DISC_MAX], n = disc_titles(s, &pool, idx);
    if (!n) return y;
    font_draw("POPULAR NOW", x, y, FONT_XS | FONT_B, THEME_TEXT_MUTED);
    y += 48;
    float gap = 20, cw = (w - 2 * gap) / 3, chh = cw * 1.5f;
    for (int i = 0; i < n; i++) {
        float px = x + (i % 3) * (cw + gap), py = y + (i / 3) * (chh + 76);
        if (py - s->ph_y > SNTV_UI_H || py + chh + 76 - s->ph_y < 0) continue;
        const AmbientItem *a = &pool[idx[i]];
        ui_card(a->poster, px, py, cw, chh, 0, 0, a->name, 0);
        font_draw_fit(a->name, px + 4, py + chh + 12, FONT_XS | FONT_B, THEME_TEXT_DIM, cw - 8);
        ui_hit(px, py, cw, chh + 50, HIT_DISC + idx[i]);
    }
    return y + ((n + 2) / 3) * (chh + 76) + 20;
}

static void phone_draw(Search *s)
{
    float W = SNTV_UI_W, x = 40, w = W - 80;
    gfx_rect(0, 0, W, SNTV_UI_H, THEME_BG);
    ui_pan_area(&s->ph_pan, 0, 0, W, SNTV_UI_H - PHONE_TABS_H, 1);
    gfx_set_translate(0, -s->ph_y);
    font_draw("Search", x, 36, FONT_XL | FONT_B, THEME_TEXT);
    float fy = 130, fh = 100;
    int ffoc = !s->focus_grid;
    gfx_rrect(x, fy, w, fh, fh / 2, COLOR_ALPHA(THEME_TEXT, ffoc ? 0x26 : 0x18));
    if (ffoc && !s->typing) gfx_rrect_ring(x, fy, w, fh, fh / 2, 3, THEME_ACCENT2);
    ui_icon(ICON_SEARCH, x + 52, fy + fh / 2, 36, THEME_TEXT_DIM);
    if (s->kb.text[0]) {
        font_draw_fit(s->kb.text, x + 96, fy + (fh - font_height(FONT_M)) / 2, FONT_M, THEME_TEXT, w - 130);
        if (s->typing && fmodf(g_app.time, 1.0f) < 0.55f) { float cx = x + 96 + font_measure(s->kb.text, FONT_M) + 4; if (cx < x + w - 30) gfx_rect(cx, fy + 28, 3, fh - 56, THEME_ACCENT2); }
        if (s->typing) app_redraw();
    } else font_draw("Movies, series, people\xE2\x80\xA6", x + 96, fy + (fh - font_height(FONT_M)) / 2, FONT_M, THEME_TEXT_MUTED);
    ui_hit(x, fy, w, fh, HIT_FIELD);
    ph_focus_at(s, ffoc, fy, fh);
    float y = fy + fh + 30;
    static const char *const CHIPS[] = { "All", "Films", "Series" };
    float cx = x;
    for (int k = 0; k < 3; k++) {
        float cw = font_measure(CHIPS[k], FONT_S | FONT_B) + 56;
        int on = s->ph_filter == k;
        gfx_rrect(cx, y, cw, 72, 36, on ? THEME_TEXT : COLOR_ALPHA(THEME_TEXT, 0x18));
        font_draw(CHIPS[k], cx + 28, y + (72 - font_height(FONT_S | FONT_B)) / 2, FONT_S | FONT_B, on ? THEME_BG : THEME_TEXT);
        ui_hit(cx, y, cw, 72, HIT_CHIP + k);
        cx += cw + 16;
    }
    y += 110;
    int nl = list_n(s);
    int lfoc = s->typing ? s->sugg_focus : s->focus_grid == 2 ? s->recent_focus : -1;
    if (nl && (s->typing || !s->n || refining(s))) {   /* suggestions, recent searches or the assistant's follow-ups */
        if (!s->kb.text[0] && !refining(s)) { font_draw("RECENT", x, y, FONT_XS | FONT_B, THEME_TEXT_MUTED); y += 44; }
        for (int i = 0; i < nl; i++, y += 100) {
            const char *t = refining(s) ? s->refine[i] : s->kb.text[0] ? s->sugg[i] : i < s->n_recent ? s->recent[i] : "Clear recent searches";
            const char *sub = !refining(s) && s->kb.text[0] ? s->sugg_sub[i] : NULL;
            Icon ic = refining(s) ? ICON_STAR : !s->kb.text[0] && i < s->n_recent ? ICON_CLOCK : (Icon)-1;
            ph_line(t, sub, ic, x, y, w, 88, lfoc == i, HIT_LIST + i);
            ph_focus_at(s, lfoc == i, y, 88);
        }
        y += 20;
    }
    if (!s->typing && !s->kb.text[0] && !s->n && !s->pending && s->ai_state != 1) y = disc_draw(s, x, y + 10, w);
    if ((s->pending || s->ai_state == 1) && !s->n) { ui_spinner(W / 2, y + 60, 26); y += 140; }
    else if (!s->n && strlen(s->last_query) >= 2 && !s->typing) { font_draw_center("Nothing found", W / 2, y + 40, FONT_L | FONT_B, THEME_TEXT); y += 120; }
    if (s->n && !(s->typing && nl)) {
        char hdr[64]; int shown = 0;
        for (int i = 0; i < s->n; i++) shown += ph_shown(s, i);
        snprintf(hdr, sizeof hdr, "%d result%s", shown, shown == 1 ? "" : "s");
        font_draw(hdr, x, y, FONT_XS | FONT_B, THEME_TEXT_MUTED);
        y += 48;
        float rh = 230;
        for (int i = 0; i < s->n; i++) {
            if (!ph_shown(s, i)) continue;
            int foc = s->focus_grid == 1 && s->grid_focus == i;
            ph_focus_at(s, foc, y, rh - 30);
            if (y - s->ph_y < SNTV_UI_H && y + rh - s->ph_y > 0) {
                const MetaPreview *m = &s->results[i];
                if (foc) { ui_focus_under(x - 12, y - 12, w + 24, rh - 6, R_PANEL, 1); gfx_rrect(x - 12, y - 12, w + 24, rh - 6, R_PANEL, COLOR_ALPHA(THEME_TEXT, 0x16)); }
                char pb[512];
                ui_card(app_art_poster(m, pb, sizeof pb), x, y, 136, 204, 0, 0, m->name, 0);
                const char *kind = m->type && !strcmp(m->type, "series") ? "Series" : m->type && !strcmp(m->type, "movie") ? "Film" : NULL;
                char sub[160] = ""; size_t n = 0;
                const char *parts[4] = { kind, m->release_info, m->genres.n ? m->genres.v[0] : NULL, NULL };
                for (int k = 0; k < 3; k++) if (parts[k] && *parts[k]) n += (size_t)snprintf(sub + n, sizeof sub - n, "%s%s", n ? "  \xC2\xB7  " : "", parts[k]);
                if (m->imdb_rating && *m->imdb_rating) snprintf(sub + n, sizeof sub - n, "%sIMDb %s", n ? "  \xC2\xB7  " : "", m->imdb_rating);
                int tl = font_draw_wrapped(m->name ? m->name : "", x + 166, y + 40, FONT_M | FONT_B, THEME_TEXT, w - 166 - 50, 2);
                font_draw_wrapped(sub, x + 166, y + 52 + tl * font_height(FONT_M | FONT_B), FONT_S, THEME_TEXT_DIM, w - 166 - 50, 2);
                ui_icon(ICON_CHEVRON_RIGHT, x + w - 16, y + 102, 26, THEME_TEXT_MUTED);
                ui_hit(x - 12, y - 12, w + 24, rh - 6, i);
            }
            y += rh;
        }
    }
    s->ph_total = y;
    gfx_set_translate(0, 0);
    rail_draw(RAIL_SEARCH, -1);
}

static int phone_key(Search *s, KeyAction a)
{
    if (s->typing || (a != KEY_UP && a != KEY_DOWN && a != KEY_OK && a != KEY_LEFT && a != KEY_RIGHT)) return -1;
    int nl = list_n(s);
    int list_on = nl && (!s->n || refining(s));
    int first = -1, last = -1;
    for (int i = 0; i < s->n; i++) if (ph_shown(s, i)) { if (first < 0) first = i; last = i; }
    if (a == KEY_LEFT || a == KEY_RIGHT) {   /* the kinds */
        int f = s->ph_filter + (a == KEY_RIGHT ? 1 : -1);
        if (f >= 0 && f < 3) { s->ph_filter = f; if (s->focus_grid == 1 && !ph_shown(s, s->grid_focus)) s->grid_focus = first; }
        return 1;
    }
    switch (s->focus_grid) {
    case 0:
        if (a == KEY_OK) { open_keyboard(s); return 1; }
        if (a == KEY_DOWN) { if (list_on) { s->focus_grid = 2; s->recent_focus = 0; } else if (first >= 0) { s->focus_grid = 1; s->grid_focus = first; } }
        return 1;
    case 2:
        if (a == KEY_OK) { if (s->recent_focus < nl) take_list(s, s->recent_focus); return 1; }
        if (a == KEY_UP) { if (s->recent_focus > 0) s->recent_focus--; else s->focus_grid = 0; }
        else if (s->recent_focus < nl - 1) s->recent_focus++;
        else if (first >= 0) { s->focus_grid = 1; s->grid_focus = first; }
        return 1;
    default: {
        if (a == KEY_OK) { if (s->grid_focus < s->n) { recent_add(s, s->last_query); app_push(screen_detail_create(&s->results[s->grid_focus])); } return 1; }
        int dir = a == KEY_DOWN ? 1 : -1, i = s->grid_focus + dir;
        while (i >= 0 && i < s->n && !ph_shown(s, i)) i += dir;
        if (i >= 0 && i < s->n) s->grid_focus = i;
        else if (dir < 0) { if (list_on) { s->focus_grid = 2; s->recent_focus = nl - 1; } else s->focus_grid = 0; }
        s->focus_t = 0;
        return 1;
    }
    }
}

/* Touch: the field opens the keyboard, a key types, a line of the list or a result is taken. */
static int tap(Screen *sc, int id)
{
    Search *s = sc->st;
    if (id >= HIT_CHIP && id < HIT_CHIP + 3) { if (!g_touch_aim) s->ph_filter = id - HIT_CHIP; return 1; }
    if (id >= HIT_GENRE && id < HIT_GENRE + N_DISC_GENRES) {   /* Discover: Explore on that genre */
        if (!g_touch_aim) { LOGI("search: discover %s", DISC_GENRES[id - HIT_GENRE]); app_push(screen_explore_create_genre(DISC_GENRES[id - HIT_GENRE])); }
        return 1;
    }
    if (id >= HIT_DISC && id < HIT_DISC + 0x1000) {   /* Discover: a title Home's rows brought */
        const AmbientItem *pool;
        int np = app_ambient_pool(&pool), i = id - HIT_DISC;
        if (g_touch_aim || i >= np) return 1;
        AmbientItem a = pool[i];   /* the pool may rotate while the page opens */
        MetaPreview m = { .id = a.id, .type = a.type, .name = a.name, .poster = a.poster, .background = a.background };
        LOGI("search: discover opens %s", a.name);
        app_push(screen_detail_create(&m));
        return 1;
    }
    if (keyboard_tap(&s->kb, id)) { if (!s->typing) return 1; s->sugg_focus = -1; return 0; }
    if (id == HIT_FIELD) { if (s->typing) return 1; s->focus_grid = 0; return 0; }
    if (id >= HIT_LIST && id < HIT_LIST + 32) {
        int i = id - HIT_LIST;
        if (s->typing && !s->tvkb) { if (i >= s->n_sugg) return 1; s->sugg_focus = i; return 0; }
        if (s->typing) close_keyboard(s);
        if (i >= list_n(s)) return 1;
        s->focus_grid = 2; s->recent_focus = i;
        return 0;
    }
    if (id < HIT_FIELD && id >= s->n) return 1;
    if (g_touch_aim && id >= HIT_FIELD) return 1;
    if (s->typing) { close_keyboard(s); recent_add(s, s->kb.text); }
    s->focus_t = 0;
    if (id == HIT_HERO_CARD) { recent_add(s, s->last_query); app_push(screen_detail_create(&s->results[0])); return 1; }
    if (id >= HIT_HERO && id < HIT_HERO + 4) { if (id - HIT_HERO >= hero_buttons(s)) return 1; s->focus_grid = 3; s->hero_btn = id - HIT_HERO; return 0; }
    if (id >= HIT_FIELD) return 1;
    s->focus_grid = 1; s->grid_focus = id;
    return 0;
}

/* Typing on a real keyboard (or text from the test driver) opens the on-screen one too. */
static void text(Screen *sc, const char *utf8)
{
    Search *s = sc->st;
    if (!s->typing) open_keyboard(s);
    keyboard_text_input(&s->kb, utf8);
    s->dirty = 1; s->debounce = 0.35f;
    app_redraw();
}

static void resume(Screen *sc)   /* back from a title page or Settings: the phone page is Search's again */
{
    Search *s = sc->st;
    if (!SNTV_PHONE) phone_search_open(s);
}

static void destroy(Screen *sc)
{
    Search *s = sc->st;
    for (int i = 0; i < OPEN_MAX; i++) if (g_open[i] == s) g_open[i] = NULL;   /* its replies still on their way are dropped */
    if (g_person_live == s) g_person_live = NULL;
    if (s->ai_req) ai_cancel(s->ai_req);
    clear_results(s);
    close_keyboard(s);
    phone_prompt(NULL, NULL, NULL);
    free(s->cell); free(s->row_y); free(s->row_first);
    free(s);
    free(sc);
}

Screen *screen_search_create(void)
{
    Screen *sc = calloc(1, sizeof *sc);
    Search *s = calloc(1, sizeof *s);
    kb_set(s, NULL);
    s->sugg_focus = -1;
    recent_load(s);
    for (int i = 0; i < OPEN_MAX; i++) if (!g_open[i]) { g_open[i] = s; break; }   /* (past eight deep, its replies are just dropped) */
    sc->name = "search"; sc->st = s; sc->long_ok = 1;
    sc->update = update; sc->draw = draw; sc->key = key; sc->tap = tap; sc->text = text; sc->destroy = destroy; sc->resume = resume; sc->phone = 1;
    if (!SNTV_PHONE) phone_search_open(s);
    return sc;
}

Screen *screen_search_create_query(const char *q)
{
    Screen *sc = screen_search_create();
    Search *s = sc->st;
    LOGI("search: \"%s\" from a title page", q);
    kb_set(s, q);
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
    kb_set(s, name);
    snprintf(s->last_query, sizeof s->last_query, "%s", name);   /* typing something else searches as usual */
    s->generation = ++g_search_gen;
    s->pending = 1;
    g_person_live = s;
    PersonReq *q = malloc(sizeof *q);
    q->owner = s; q->generation = s->generation;
    tmdb_person_titles(name, person_id, on_person_titles, q);
    return sc;
}
