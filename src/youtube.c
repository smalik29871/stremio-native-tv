#include "youtube.h"
#include "http.h"
#include "store.h"
#include "log.h"
#include "cJSON.h"
#include "quickjs/quickjs.h"
#include <SDL.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <pthread.h>
#include <dirent.h>

/* The YouTube apps the TV pretends to be, in order (values from yt-dlp 2026.08.19). The
 * visionOS app is yt-dlp's default: its formats need neither the player script nor a
 * proof-of-origin token. YouTube's TV app (Cobalt) is the fallback: no token either, but
 * the player's signature and "n" challenges must be solved. */
typedef struct {
    const char *name, *version, *ua, *make, *model, *os, *os_version;
    int id, needs_js;
    int embedded;   /* asks as a player embedded on another site */
    const char *platform; int sdk;   /* the phone apps: "MOBILE", Android's SDK level */
} Client;
static const Client CLIENTS[] = {
    { "VISIONOS", "1.02", "Mozilla/5.0 (Macintosh; Intel Mac OS X 15_7_3) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/26.0 Safari/605.1.15",
      "Apple", "RealityDevice17,1", "visionOS", "26.5.23O471", 101, 0, 0, NULL, 0 },
    /* YouTube's phone apps, as Nuvio asks them: plain URLs, and iOS adds an HLS playlist */
    { "ANDROID", "20.10.35", "com.google.android.youtube/20.10.35 (Linux; U; Android 14; en_US) gzip",
      NULL, NULL, "Android", "14", 3, 0, 0, "MOBILE", 34 },
    { "IOS", "20.10.1", "com.google.ios.youtube/20.10.1 (iPhone16,2; U; CPU iOS 17_4 like Mac OS X)",
      NULL, "iPhone16,2", "iPhone", "17.4.0.21E219", 5, 0, 0, "MOBILE", 0 },
    { "TVHTML5", "7.20260707.07.00", "Mozilla/5.0 (ChromiumStylePlatform) Cobalt/25.lts.30.1034943-gold (unlike Gecko), Unknown_TV_Unknown_0/Unknown (Unknown, Unknown)",
      NULL, NULL, NULL, NULL, 7, 1, 0 },
    { "WEB_EMBEDDED_PLAYER", "2.20260708.00.00", "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/138.0.0.0 Safari/537.36",
      NULL, NULL, NULL, NULL, 56, 1, 1 },
};
enum { CHUNK = 4 << 20, MAX_BYTES = 200 << 20, MAX_SECONDS = 3600 };

static const char *base_url(void)
{
    const char *b = getenv("SNTV_YT_BASE");   /* tests point this at the fake server */
    return b && *b ? b : "https://www.youtube.com";
}

/* ---- links ------------------------------------------------------------------ */

static int id_char(char c) { return isalnum((unsigned char)c) || c == '-' || c == '_'; }

static int id_at(const char *p, char id[16])
{
    int k = 0;
    while (k < 11 && id_char(p[k])) k++;
    if (k != 11 || id_char(p[11])) return 0;
    memcpy(id, p, 11); id[11] = 0;
    return 1;
}

int youtube_video_id(const char *url, char id[16])
{
    if (!url) return 0;
    const char *h = strstr(url, "://");
    h = h ? h + 3 : url;
    size_t hl = strcspn(h, "/?#");
    char host[96];
    snprintf(host, sizeof host, "%.*s", (int)(hl < 95 ? hl : 95), h);
    for (char *c = host; *c; c++) *c = (char)tolower((unsigned char)*c);
    const char *path = h + hl;
    if (strcmp(host, "youtu.be") == 0 || strcmp(host, "www.youtu.be") == 0) return *path == '/' && id_at(path + 1, id);
    size_t n = strlen(host);
    int yt = !strcmp(host, "youtube.com") || (n > 12 && !strcmp(host + n - 12, ".youtube.com")) || strstr(host, "youtube-nocookie.com");
    if (!yt) return 0;
    static const char *const PREFIX[] = { "/shorts/", "/embed/", "/live/", "/v/", NULL };
    for (int i = 0; PREFIX[i]; i++) if (!strncmp(path, PREFIX[i], strlen(PREFIX[i]))) return id_at(path + strlen(PREFIX[i]), id);
    for (const char *q = strchr(path, '?'); q && *q; q = strchr(q + 1, '&'))
        if (!strncmp(q + 1, "v=", 2)) return id_at(q + 3, id);
    return 0;
}

static void file_name(char *out, size_t n, const char *id, int video, const char *ext)
{
    snprintf(out, n, "yt-%s-%s.%s", id, video ? "video" : "audio", ext);
}

static int sound_exists(const char *name)
{
    char path[600]; struct stat st;
    snprintf(path, sizeof path, "%s/sounds/%s", store_dir(), name);
    return stat(path, &st) == 0 && st.st_size > 0;
}

int youtube_saved(const char *url, int want_video, char *name, size_t n, int *is_video)
{
    char id[16];
    if (!youtube_video_id(url, id)) return 0;
    static const struct { int video; const char *ext; } KINDS[] = { { 1, "mp4" }, { 0, "m4a" }, { 0, "webm" } };
    for (int pass = 0; pass < 2; pass++)
        for (int k = 0; k < 3; k++) {
            if ((KINDS[k].video == want_video) != (pass == 0)) continue;
            file_name(name, n, id, KINDS[k].video, KINDS[k].ext);
            if (sound_exists(name)) { if (is_video) *is_video = KINDS[k].video; return 1; }
        }
    return 0;
}

/* ---- the challenge solver ------------------------------------------------------ */

static char *read_file(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = n >= 0 ? malloc((size_t)n + 1) : NULL;
    if (buf && fread(buf, 1, (size_t)n, f) != (size_t)n) { free(buf); buf = NULL; }
    fclose(f);
    if (buf) { buf[n] = 0; if (len) *len = (size_t)n; }
    return buf;
}

static int write_file(const char *path, const char *data, size_t len)
{
    FILE *f = fopen(path, "wb");
    if (!f) return 0;
    int ok = fwrite(data, 1, len, f) == len;
    return fclose(f) == 0 && ok;
}

static int get(const char *url, const char *headers, long timeout, HttpResponse *r)
{
    HttpOptions o = { .timeout_s = timeout, .no_json_accept = 1, .headers = headers };
    http_request_ex(url, &o, r);
    return r->status == 200;
}

/* yt-dlp's solver: lib (meriyah + astring) and core (jsc). The copy in assets/yt comes
 * first; when YouTube changes and it fails, the newest release from yt-dlp/ejs on GitHub
 * is fetched once into <data>/yt and tried. It runs in QuickJS with no I/O of any kind. */
typedef struct { char *lib, *core; unsigned tag; } Solver;

static unsigned text_tag(const char *s)
{
    unsigned h = 5381;
    for (; *s; s++) h = h * 33 + (unsigned char)*s;
    return h;
}

static int solver_from(const char *dir, Solver *sv)
{
    char p[600];
    snprintf(p, sizeof p, "%.500s/solver.lib.js", dir); sv->lib = read_file(p, NULL);
    snprintf(p, sizeof p, "%.500s/solver.core.js", dir); sv->core = read_file(p, NULL);
    if (sv->lib && sv->core) { sv->tag = text_tag(sv->core); return 1; }
    free(sv->lib); free(sv->core); sv->lib = sv->core = NULL;
    return 0;
}

static void solver_free(Solver *sv) { free(sv->lib); free(sv->core); sv->lib = sv->core = NULL; }

/* assets/yt next to the app (SNTV_YT_SOLVER_DIR for tests), else the working directory. */
static int solver_bundled(Solver *sv)
{
    const char *env = getenv("SNTV_YT_SOLVER_DIR");
    if (env && *env && solver_from(env, sv)) return 1;
    char *base = SDL_GetBasePath(), dir[520];
    snprintf(dir, sizeof dir, "%.500sassets/yt", base ? base : "");
    if (base) SDL_free(base);
    return solver_from(dir, sv) || solver_from("assets/yt", sv);
}

/* The newest release, downloaded once per run (SNTV_YT_SOLVER_URL replaces GitHub in tests). */
static int solver_fetch(Solver *sv)
{
    static volatile int tried;
    char dir[560];
    snprintf(dir, sizeof dir, "%s/yt", store_dir());
    if (tried) return solver_from(dir, sv);
    tried = 1;
    const char *src = getenv("SNTV_YT_SOLVER_URL");
    if (!src || !*src) src = "https://github.com/yt-dlp/ejs/releases/latest/download";
    static const char *const FILES[2][2] = { { "yt.solver.lib.min.js", "solver.lib.js" }, { "yt.solver.core.min.js", "solver.core.js" } };
    for (int i = 0; i < 2; i++) {
        char url[600], path[640];
        HttpResponse r;
        snprintf(url, sizeof url, "%s/%s", src, FILES[i][0]);
        get(url, NULL, 60, &r);
        int ok = r.status == 200 && r.len > 1000 && strstr(r.body, i ? "jsc" : "meriyah");
        snprintf(path, sizeof path, "%s/%s", dir, FILES[i][1]);
        if (ok) ok = write_file(path, r.body, r.len);
        LOGI("youtube: newest solver %s: %s", FILES[i][0], ok ? "saved" : r.status ? "refused" : r.error);
        http_response_free(&r);
        if (!ok) return 0;
    }
    return solver_from(dir, sv);
}

typedef struct { const char *lib, *core, *input; char *out; char err[400]; } JsRun;

static void js_error(JSContext *ctx, char *err, size_t n)
{
    JSValue ex = JS_GetException(ctx);
    const char *msg = JS_ToCString(ctx, ex);
    JSValue stack = JS_GetPropertyStr(ctx, ex, "stack");
    const char *st = JS_IsUndefined(stack) ? NULL : JS_ToCString(ctx, stack);
    snprintf(err, n, "%s%s%.200s", msg ? msg : "JavaScript error", st ? " " : "", st ? st : "");
    if (st) JS_FreeCString(ctx, st);
    if (msg) JS_FreeCString(ctx, msg);
    JS_FreeValue(ctx, stack);
    JS_FreeValue(ctx, ex);
}

static int eval_ok(JSContext *ctx, const char *code, const char *name, char *err, size_t n)
{
    JSValue v = JS_Eval(ctx, code, strlen(code), name, JS_EVAL_TYPE_GLOBAL);
    int ok = !JS_IsException(v);
    if (!ok) js_error(ctx, err, n);
    JS_FreeValue(ctx, v);
    return ok;
}

/* The runtime measures its stack from where it is created, so it lives on this thread. */
static void *js_thread(void *arg)
{
    JsRun *j = arg;
    JSRuntime *rt = JS_NewRuntime();
    if (!rt) { snprintf(j->err, sizeof j->err, "QuickJS could not start"); return NULL; }
    JS_SetMemoryLimit(rt, (size_t)384 << 20);
    JS_SetMaxStackSize(rt, (size_t)12 << 20);
    JSContext *ctx = JS_NewContext(rt);
    JSValue global = JS_GetGlobalObject(ctx);
    JS_SetPropertyStr(ctx, global, "__input", JS_NewString(ctx, j->input));
    JS_FreeValue(ctx, global);
    if (eval_ok(ctx, j->lib, "solver.lib.js", j->err, sizeof j->err)
        && eval_ok(ctx, "Object.assign(globalThis, lib);", "glue", j->err, sizeof j->err)
        && eval_ok(ctx, j->core, "solver.core.js", j->err, sizeof j->err)) {
        static const char RUN[] = "JSON.stringify(jsc(JSON.parse(__input)))";
        JSValue v = JS_Eval(ctx, RUN, sizeof RUN - 1, "run", JS_EVAL_TYPE_GLOBAL);
        if (JS_IsException(v)) js_error(ctx, j->err, sizeof j->err);
        else { const char *s = JS_ToCString(ctx, v); if (s) { j->out = strdup(s); JS_FreeCString(ctx, s); } }
        JS_FreeValue(ctx, v);
    }
    JS_FreeContext(ctx);
    JS_FreeRuntime(rt);
    return NULL;
}

static char *run_solver(const Solver *sv, const char *input_json, char *err, size_t n)
{
    JsRun j = { .lib = sv->lib, .core = sv->core, .input = input_json };
    pthread_attr_t attr;   /* plain pthreads: SDL's stack-size call is newer than some TVs' SDL */
    pthread_t t;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, (size_t)16 << 20);
    if (pthread_create(&t, &attr, js_thread, &j) == 0) pthread_join(t, NULL);
    else snprintf(j.err, sizeof j.err, "no thread for the solver");
    pthread_attr_destroy(&attr);
    if (!j.out) snprintf(err, n, "%s", j.err[0] ? j.err : "the solver gave no answer");
    return j.out;
}

char *youtube_solve(const char *input_json, char *err, size_t n)
{
    Solver sv = { 0 };
    if (!solver_bundled(&sv)) { snprintf(err, n, "the solver scripts are missing (assets/yt)"); return NULL; }
    char *out = run_solver(&sv, input_json, err, n);
    solver_free(&sv);
    return out;
}

/* ---- talking to YouTube -------------------------------------------------------- */

/* ytcfg.set({...}) blocks of a YouTube page, merged. */
static cJSON *page_config(const char *html)
{
    cJSON *cfg = cJSON_CreateObject();
    for (const char *p = strstr(html, "ytcfg.set("); p; p = strstr(p + 1, "ytcfg.set(")) {
        const char *s = p + 10;
        while (*s == ' ') s++;
        if (*s != '{') continue;
        int depth = 0, in_str = 0;
        const char *e = s;
        for (; *e; e++) {
            if (in_str) { if (*e == '\\' && e[1]) e++; else if (*e == '"') in_str = 0; continue; }
            if (*e == '"') in_str = 1;
            else if (*e == '{') depth++;
            else if (*e == '}' && --depth == 0) break;
        }
        if (!*e) break;
        cJSON *part = cJSON_ParseWithLength(s, (size_t)(e - s + 1));
        cJSON *it;
        cJSON_ArrayForEach(it, part) if (it->string && !cJSON_HasObjectItem(cfg, it->string))
            cJSON_AddItemToObject(cfg, it->string, cJSON_Duplicate(it, 1));
        cJSON_Delete(part);
    }
    return cfg;
}

/* "…/s/player/0123abcd/…" or the iframe API's "player\/0123abcd\/". */
static int player_id_in(const char *text, char *id, size_t n)
{
    for (const char *p = strstr(text, "player"); p; p = strstr(p + 1, "player")) {
        const char *q = p + 6;
        if (*q == '\\') q++;
        if (*q != '/') continue;
        q++;
        size_t k = 0;
        while (isxdigit((unsigned char)q[k])) k++;
        if (k < 8 || k + 1 > n || (q[k] != '/' && q[k] != '\\')) continue;
        memcpy(id, q, k); id[k] = 0;
        return 1;
    }
    return 0;
}

static int signature_timestamp(const char *js)
{
    static const char *const KEYS[] = { "signatureTimestamp", "sts", NULL };
    for (int k = 0; KEYS[k]; k++)
        for (const char *p = strstr(js, KEYS[k]); p; p = strstr(p + 1, KEYS[k])) {
            const char *q = p + strlen(KEYS[k]);
            if (p > js && (isalnum((unsigned char)p[-1]) || p[-1] == '_')) continue;
            while (*q == ' ') q++;
            if (*q++ != ':') continue;
            while (*q == ' ') q++;
            int d = 0;
            while (isdigit((unsigned char)q[d])) d++;
            if (d == 5) return atoi(q);
        }
    return 0;
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

/* The decoded value of `key` in "a=1&b=2" (malloc'ed), or NULL. */
static char *param(const char *query, const char *key)
{
    size_t kl = strlen(key);
    for (const char *p = query; p && *p; ) {
        size_t len = strcspn(p, "&");
        if (len > kl && !strncmp(p, key, kl) && p[kl] == '=') {
            char *v = strndup(p + kl + 1, len - kl - 1);
            url_decode(v);
            return v;
        }
        p += len + (p[len] == '&');
    }
    return NULL;
}

/* url with its n= value swapped for `n` (malloc'ed). */
static char *with_n(const char *url, const char *n)
{
    const char *q = strchr(url, '?');
    const char *p = q ? strstr(q, "&n=") : NULL;
    if (!p && q && !strncmp(q + 1, "n=", 2)) p = q;
    if (!p) return strdup(url);
    const char *v = p + 3, *end = v + strcspn(v, "&");
    char enc[512];
    http_url_encode(n, enc, sizeof enc);
    size_t len = (size_t)(v - url) + strlen(enc) + strlen(end) + 1;
    char *out = malloc(len);
    snprintf(out, len, "%.*s%s%s", (int)(v - url), url, enc, end);
    return out;
}

typedef struct {
    char url[700], id[16];
    int video;
    YoutubeCallback cb;
    void *user;
} Save;

typedef struct { const cJSON *fmt; int video; const char *ext; } Pick;

static int itag(const cJSON *f)
{
    const cJSON *i = cJSON_GetObjectItemCaseSensitive(f, "itag");
    return cJSON_IsNumber(i) ? i->valueint : 0;
}

static int mime_is(const cJSON *f, const char *prefix)
{
    const cJSON *m = cJSON_GetObjectItemCaseSensitive(f, "mimeType");
    return cJSON_IsString(m) && !strncmp(m->valuestring, prefix, strlen(prefix));
}

/* A muxed MP4 for video (itag 18 first), else the best AAC audio, else Opus. */
static Pick pick_format(const cJSON *streaming, int want_video)
{
    Pick p = { 0 };
    const cJSON *f;
    if (want_video) {
        cJSON_ArrayForEach(f, cJSON_GetObjectItemCaseSensitive(streaming, "formats"))
            if (mime_is(f, "video/mp4") && (!p.fmt || itag(f) == 18)) p = (Pick){ f, 1, "mp4" };
        if (p.fmt) return p;
    }
    static const char *const AUDIO[] = { "audio/mp4", "audio/webm" };
    for (int a = 0; a < 2 && !p.fmt; a++) {
        double best = -1;
        cJSON_ArrayForEach(f, cJSON_GetObjectItemCaseSensitive(streaming, "adaptiveFormats")) {
            if (!mime_is(f, AUDIO[a]) || cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(f, "isDrc"))) continue;
            const cJSON *br = cJSON_GetObjectItemCaseSensitive(f, "bitrate");
            double b = cJSON_IsNumber(br) ? br->valuedouble : 0;
            if (b > best) { best = b; p = (Pick){ f, 0, a ? "webm" : "m4a" }; }
        }
    }
    return p;
}

static int fail(HttpResponse *r, const char *fmt, const char *detail)
{
    snprintf(r->error, sizeof r->error, fmt, detail ? detail : "");
    r->status = 0;
    return 0;
}

static char *get_player(const char *player_id)
{
    char url[400];
    HttpResponse r;
    snprintf(url, sizeof url, "%s/s/player/%s/player_ias.vflset/en_US/base.js", base_url(), player_id);
    if (!get(url, NULL, 60, &r)) { http_response_free(&r); return NULL; }
    LOGI("youtube: player %s, %zu KB", player_id, r.len >> 10);
    return r.body;
}

/* Prepared players and timestamps of other player versions: YouTube moves on weekly. */
static void prune_players(const char *keep_id)
{
    char dir[560];
    snprintf(dir, sizeof dir, "%s/yt", store_dir());
    DIR *d = opendir(dir);
    if (!d) return;
    for (struct dirent *e; (e = readdir(d)); ) {
        size_t n = strlen(e->d_name);
        int ours = (n > 10 && !strcmp(e->d_name + n - 10, ".solver.js")) || (n > 4 && !strcmp(e->d_name + n - 4, ".sts"));
        if (!ours || !strncmp(e->d_name, keep_id, strlen(keep_id)) || !strncmp(e->d_name, "solver.", 7)) continue;
        char path[880];
        snprintf(path, sizeof path, "%s/%s", dir, e->d_name);
        remove(path);
    }
    closedir(d);
}

/* One solver's answers for the n and signature challenges (either may be NULL). The
 * player is reduced to its solver functions once per solver and kept in <data>/yt, so
 * later links skip parsing megabytes of script. *player_js is downloaded when needed. */
static int solve_with(const Solver *sv, const char *player_id, char **player_js, const char *ch[2], char *answer[2], char *err, size_t en)
{
    char cache[600];
    snprintf(cache, sizeof cache, "%s/yt/%s-%08x.solver.js", store_dir(), player_id, sv->tag);
    char *pre = read_file(cache, NULL);
    if (!pre && !*player_js && !(*player_js = get_player(player_id))) { snprintf(err, en, "YouTube's player could not be loaded"); return 0; }
    cJSON *in = cJSON_CreateObject(), *reqs = cJSON_CreateArray();
    if (pre) { cJSON_AddStringToObject(in, "type", "preprocessed"); cJSON_AddStringToObject(in, "preprocessed_player", pre); }
    else { cJSON_AddStringToObject(in, "type", "player"); cJSON_AddStringToObject(in, "player", *player_js); cJSON_AddTrueToObject(in, "output_preprocessed"); }
    static const char *const KINDS[2] = { "n", "sig" };
    for (int k = 0; k < 2; k++) {
        if (!ch[k]) continue;
        cJSON *r = cJSON_CreateObject(), *list = cJSON_CreateArray();
        cJSON_AddStringToObject(r, "type", KINDS[k]);
        cJSON_AddItemToArray(list, cJSON_CreateString(ch[k]));
        cJSON_AddItemToObject(r, "challenges", list);
        cJSON_AddItemToArray(reqs, r);
    }
    cJSON_AddItemToObject(in, "requests", reqs);
    char *input = cJSON_PrintUnformatted(in);
    cJSON_Delete(in);
    int cached = pre != NULL;
    free(pre);
    Uint32 t0 = SDL_GetTicks();
    char *out = run_solver(sv, input, err, en);
    free(input);
    if (!out) return 0;
    LOGI("youtube: solver answered in %u ms%s", SDL_GetTicks() - t0, cached ? " (player prepared before)" : "");
    cJSON *res = cJSON_Parse(out);
    free(out);
    const cJSON *type = cJSON_GetObjectItemCaseSensitive(res, "type");
    int ok = cJSON_IsString(type) && !strcmp(type->valuestring, "result");
    if (!ok) {
        const cJSON *e = cJSON_GetObjectItemCaseSensitive(res, "error");
        snprintf(err, en, "%.300s", cJSON_IsString(e) ? e->valuestring : "the solver failed");
    }
    const cJSON *keep = cJSON_GetObjectItemCaseSensitive(res, "preprocessed_player");
    if (ok && cJSON_IsString(keep)) { prune_players(player_id); write_file(cache, keep->valuestring, strlen(keep->valuestring)); }
    int idx = 0;
    for (int k = 0; k < 2 && ok; k++) {
        if (!ch[k]) continue;
        const cJSON *resp = cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(res, "responses"), idx++);
        const cJSON *v = cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(resp, "data"), ch[k]);
        if (cJSON_IsString(v)) answer[k] = strdup(v->valuestring);
        else {
            const cJSON *e = cJSON_GetObjectItemCaseSensitive(resp, "error");
            snprintf(err, en, "%s: %.200s", KINDS[k], cJSON_IsString(e) ? e->valuestring : "no answer");
            ok = 0;
        }
    }
    cJSON_Delete(res);
    if (!ok) { free(answer[0]); free(answer[1]); answer[0] = answer[1] = NULL; }
    return ok;
}

/* The bundled solver, then the newest one from yt-dlp when the bundled one fails. */
static int solve(const char *player_id, char **player_js, const char *n, const char *sig, char **n_out, char **sig_out, char *err, size_t en)
{
    const char *ch[2] = { n, sig };
    char *answer[2] = { NULL, NULL };
    Solver sv = { 0 };
    int ok = 0;
    unsigned tried = 0;
    if (solver_bundled(&sv)) { tried = sv.tag; ok = solve_with(&sv, player_id, player_js, ch, answer, err, en); solver_free(&sv); }
    else snprintf(err, en, "the solver scripts are missing (assets/yt)");
    if (!ok && solver_fetch(&sv)) {
        if (sv.tag != tried) {
            char err2[400] = "";
            LOGW("youtube: the bundled solver failed (%s); trying the newest one", err);
            ok = solve_with(&sv, player_id, player_js, ch, answer, err2, sizeof err2);
            if (!ok) snprintf(err, en, "%s", err2);
        }
        solver_free(&sv);
    }
    *n_out = answer[0]; *sig_out = answer[1];
    return ok;
}

static int download(const char *url, const char *headers, const char *path, uint64_t expect, HttpResponse *r)
{
    char part[720];
    snprintf(part, sizeof part, "%s.part", path);
    FILE *f = fopen(part, "wb");
    if (!f) return fail(r, "cannot write %.200s", part);
    uint64_t pos = 0, total = expect;
    for (;;) {
        HttpResponse c;
        HttpOptions o = { .range_start = pos, .range_bytes = CHUNK, .timeout_s = 120, .no_json_accept = 1, .headers = headers };
        http_request_ex(url, &o, &c);
        int whole = c.status == 200, ok = (c.status == 206 || whole) && c.len > 0;
        if (ok && fwrite(c.body, 1, c.len, f) != c.len) ok = 0;
        if (!ok) {
            char why[300];
            snprintf(why, sizeof why, "%ld %.200s", c.status, c.status ? "" : c.error);
            http_response_free(&c); fclose(f); remove(part);
            return fail(r, "the download failed (%.200s)", why);
        }
        pos += c.len;
        if (!total) total = c.total_size;
        http_response_free(&c);
        if (whole || (total && pos >= total) || pos > MAX_BYTES) break;
    }
    if (fclose(f) != 0 || rename(part, path) != 0) { remove(part); return fail(r, "cannot save %.200s", path); }
    LOGI("youtube: saved %llu KB to %s", (unsigned long long)(pos >> 10), path);
    return 1;
}

/* The client's view of the video, or NULL with YouTube's reason (playability) in why. */
static cJSON *player_request(const Client *c, const char *video_id, const char *visitor, int sts, const char *api_key, char *why, size_t wn)
{
    cJSON *req = cJSON_CreateObject();
    cJSON *cl = cJSON_AddObjectToObject(cJSON_AddObjectToObject(req, "context"), "client");
    cJSON_AddStringToObject(cl, "clientName", c->name);
    cJSON_AddStringToObject(cl, "clientVersion", c->version);
    cJSON_AddStringToObject(cl, "userAgent", c->ua);
    if (c->make) cJSON_AddStringToObject(cl, "deviceMake", c->make);
    if (c->model) cJSON_AddStringToObject(cl, "deviceModel", c->model);
    if (c->os) cJSON_AddStringToObject(cl, "osName", c->os);
    if (c->os_version) cJSON_AddStringToObject(cl, "osVersion", c->os_version);
    if (c->platform) cJSON_AddStringToObject(cl, "platform", c->platform);
    if (c->sdk) cJSON_AddNumberToObject(cl, "androidSdkVersion", c->sdk);
    cJSON_AddStringToObject(cl, "hl", "en");
    cJSON_AddStringToObject(cl, "gl", "US");
    cJSON_AddStringToObject(cl, "timeZone", "UTC");
    cJSON_AddNumberToObject(cl, "utcOffsetMinutes", 0);
    if (visitor) cJSON_AddStringToObject(cl, "visitorData", visitor);
    if (c->embedded) cJSON_AddStringToObject(cJSON_AddObjectToObject(cJSON_GetObjectItemCaseSensitive(req, "context"), "thirdParty"), "embedUrl", "https://www.reddit.com/");
    cJSON_AddStringToObject(req, "videoId", video_id);
    cJSON *cpc = cJSON_AddObjectToObject(cJSON_AddObjectToObject(req, "playbackContext"), "contentPlaybackContext");
    cJSON_AddStringToObject(cpc, "html5Preference", "HTML5_PREF_WANTS");
    if (sts) cJSON_AddNumberToObject(cpc, "signatureTimestamp", sts);
    cJSON_AddTrueToObject(req, "contentCheckOk");
    cJSON_AddTrueToObject(req, "racyCheckOk");
    char *body = cJSON_PrintUnformatted(req);
    cJSON_Delete(req);
    char url[300], headers[800];
    snprintf(url, sizeof url, "%s/youtubei/v1/player?prettyPrint=false%s%s", base_url(), api_key ? "&key=" : "", api_key ? api_key : "");
    snprintf(headers, sizeof headers, "User-Agent: %s\nX-YouTube-Client-Name: %d\nX-YouTube-Client-Version: %s\nOrigin: https://www.youtube.com%s%s",
             c->ua, c->id, c->version, visitor ? "\nX-Goog-Visitor-Id: " : "", visitor ? visitor : "");
    HttpResponse pr;
    HttpOptions po = { .post_json = body, .timeout_s = 30, .headers = headers };
    http_request_ex(url, &po, &pr);
    free(body);
    cJSON *resp = pr.status == 200 ? cJSON_Parse(pr.body) : NULL;
    if (!resp) snprintf(why, wn, "YouTube did not answer (%.200s)", pr.status ? "HTTP error" : pr.error);
    http_response_free(&pr);
    const cJSON *play = cJSON_GetObjectItemCaseSensitive(resp, "playabilityStatus");
    const cJSON *status = cJSON_GetObjectItemCaseSensitive(play, "status");
    if (resp && (!cJSON_IsString(status) || strcmp(status->valuestring, "OK"))) {
        const cJSON *reason = cJSON_GetObjectItemCaseSensitive(play, "reason");
        snprintf(why, wn, "YouTube: %.200s", cJSON_IsString(reason) ? reason->valuestring : cJSON_IsString(status) ? status->valuestring : "not playable");
        cJSON_Delete(resp);
        resp = NULL;
    }
    LOGI("youtube: %s client: %s", c->name, resp ? "OK" : why);
    return resp;
}

/* The watch page, as a browser opens it: a visitor id (YouTube distrusts requests
 * without one) and the current player version. */
static void watch_page(const char *id, char *visitor, size_t vn, char *player_id, size_t pn)
{
    char url[300], headers[600];
    HttpResponse page;
    snprintf(url, sizeof url, "%s/watch?v=%s&bpctr=9999999999&has_verified=1", base_url(), id);
    snprintf(headers, sizeof headers, "User-Agent: %s\nAccept-Language: en-US,en;q=0.9\nCookie: SOCS=CAI; PREF=hl=en&tz=UTC", CLIENTS[4].ua);
    if (!get(url, headers, 20, &page)) LOGW("youtube: the watch page answered %ld %s", page.status, page.status ? "" : page.error);
    cJSON *cfg = page.body ? page_config(page.body) : cJSON_CreateObject();
    if (page.body && player_id) player_id_in(page.body, player_id, pn);
    http_response_free(&page);
    const cJSON *vd = cJSON_GetObjectItemCaseSensitive(cfg, "VISITOR_DATA");
    if (!cJSON_IsString(vd)) vd = cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(cfg, "INNERTUBE_CONTEXT"), "client"), "visitorData");
    if (cJSON_IsString(vd)) snprintf(visitor, vn, "%s", vd->valuestring);
    cJSON_Delete(cfg);
    if (!visitor[0]) LOGW("youtube: no visitor id from the watch page");
}

/* Streaming without a download, the way Nuvio plays trailers (its InAppYouTubeExtractor):
 *   1. the watch page as an Android TV browser opens it: YouTube's API key and a visitor id
 *   2. the player API with that key, asked as every client that needs no script
 *   3. the HLS master playlist when any client offers one (every quality, audio with it);
 *      else the best file with video and audio together: tallest, then without an "n"
 *      challenge, MP4 before WebM
 *   4. a googlevideo address that answers: the one given, else its mirrors from "mn"
 * TVs cannot play YouTube's separate video and audio files together, as Nuvio's player does,
 * so those are left out. */
static const char *NUVIO_UA = "Mozilla/5.0 (Linux; Android 12; Android TV) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/133.0.0.0 Safari/537.36";

static void between(const char *html, const char *open, char *out, size_t n)
{
    out[0] = 0;
    const char *p = html ? strstr(html, open) : NULL;   /* "NAME": then the quoted value */
    if (!p) return;
    p += strlen(open);
    while (*p == ' ' || *p == ':') p++;
    if (*p++ != '"') return;
    const char *e = strchr(p, '"');
    if (e && (size_t)(e - p) < n) snprintf(out, n, "%.*s", (int)(e - p), p);
}

/* GET bytes=0-0 in under 2 s */
static int reachable(const char *url)
{
    char hdr[300];
    snprintf(hdr, sizeof hdr, "User-Agent: %s\nAccept-Language: en-US,en;q=0.9", NUVIO_UA);
    HttpResponse r;
    HttpOptions o = { .range_bytes = 1, .timeout_s = 2, .no_json_accept = 1, .headers = hdr };
    http_request_ex(url, &o, &r);
    int ok = r.status >= 200 && r.status < 300;
    http_response_free(&r);
    return ok;
}

/* The URL, or one of its mirrors: rrN---<mn server>.googlevideo.com. NULL when none answers. */
static char *reachable_mirror(const char *url)
{
    if (!strstr(url, "googlevideo.com")) return strdup(url);
    char *mn = param(strchr(url, '?') ? strchr(url, '?') + 1 : "", "mn");
    if (!mn || !strchr(mn, ',')) { free(mn); return reachable(url) ? strdup(url) : NULL; }
    if (reachable(url)) { free(mn); return strdup(url); }
    const char *hs = strstr(url, "://"), *he = hs ? strchr(hs + 3, '/') : NULL;
    const char *dash = hs ? strstr(hs, "---") : NULL;
    const char *dot = dash ? strchr(dash, '.') : NULL;
    char *found = NULL; int idx = 1;
    for (char *srv = strtok(mn, ","); srv && !found && dash && dot && he && dot < he; srv = strtok(NULL, ","), idx++) {
        size_t l = strlen(url) + strlen(srv) + 16;
        char *alt = malloc(l);
        snprintf(alt, l, "%.*srr%d---%s%s", (int)(hs + 3 - url), url, idx, srv, dot);
        if (reachable(alt)) found = alt; else free(alt);
    }
    free(mn);
    return found;
}

/* The formats a DASH manifest needs: video in MP4 (AV1 when this TV decodes it and the
 * picture is above 1080p, else H.264), at or below the resolution cap, and AAC audio,
 * the original track. Each with its byte ranges for the header and the index. */
static cJSON *rep_of(const cJSON *f)
{
    const cJSON *u = cJSON_GetObjectItemCaseSensitive(f, "url"), *mt = cJSON_GetObjectItemCaseSensitive(f, "mimeType");
    const cJSON *ir = cJSON_GetObjectItemCaseSensitive(f, "initRange"), *xr = cJSON_GetObjectItemCaseSensitive(f, "indexRange");
    const cJSON *is = cJSON_GetObjectItemCaseSensitive(ir, "start"), *ie = cJSON_GetObjectItemCaseSensitive(ir, "end");
    const cJSON *xs = cJSON_GetObjectItemCaseSensitive(xr, "start"), *xe = cJSON_GetObjectItemCaseSensitive(xr, "end");
    if (!cJSON_IsString(u) || !cJSON_IsString(mt) || !cJSON_IsString(is) || !cJSON_IsString(ie) || !cJSON_IsString(xs) || !cJSON_IsString(xe)) return NULL;
    const char *cq = strstr(mt->valuestring, "codecs=\"");
    char codecs[48] = "", mime[24] = "";
    if (cq) { cq += 8; size_t l = strcspn(cq, "\""); snprintf(codecs, sizeof codecs, "%.*s", (int)(l < 47 ? l : 47), cq); }
    snprintf(mime, sizeof mime, "%.*s", (int)strcspn(mt->valuestring, ";"), mt->valuestring);
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "url", u->valuestring);
    cJSON_AddStringToObject(o, "mime", mime);
    cJSON_AddStringToObject(o, "codecs", codecs);
    char rg[48];
    snprintf(rg, sizeof rg, "%s-%s", is->valuestring, ie->valuestring); cJSON_AddStringToObject(o, "init", rg);
    snprintf(rg, sizeof rg, "%s-%s", xs->valuestring, xe->valuestring); cJSON_AddStringToObject(o, "index", rg);
    static const char *const NUM[] = { "bitrate", "width", "height", "fps", "audioSampleRate" };
    for (int k = 0; k < 5; k++) {
        const cJSON *v = cJSON_GetObjectItemCaseSensitive(f, NUM[k]);
        double d = cJSON_IsNumber(v) ? v->valuedouble : cJSON_IsString(v) ? atof(v->valuestring) : 0;
        cJSON_AddNumberToObject(o, NUM[k], d);
    }
    return o;
}

static int g_q_cap = 1080, g_q_av1, g_q_4k;
void youtube_set_quality(int res_cap, int av1_ok, int uhd_ok) { g_q_cap = res_cap > 0 ? res_cap : 1080; g_q_av1 = av1_ok; g_q_4k = uhd_ok; }

static cJSON *dash_pick(const cJSON *adaptive, double length_s)
{
    int cap = g_q_cap, av1 = g_q_av1, hd4k = g_q_4k;
    if (!hd4k && cap > 1080) cap = 1080;
    cJSON *video = cJSON_CreateArray(), *audio = NULL;
    double audio_br = 0;
    int want_av1 = 0;
    const cJSON *f;
    /* AV1 only pays off above 1080p, where H.264 is not offered */
    cJSON_ArrayForEach(f, adaptive) {
        const cJSON *mt = cJSON_GetObjectItemCaseSensitive(f, "mimeType"), *h = cJSON_GetObjectItemCaseSensitive(f, "height");
        if (av1 && cJSON_IsString(mt) && strstr(mt->valuestring, "video/mp4") && strstr(mt->valuestring, "av01") && cJSON_IsNumber(h) && h->valueint > 1080 && h->valueint <= cap) want_av1 = 1;
    }
    cJSON_ArrayForEach(f, adaptive) {
        const cJSON *mt = cJSON_GetObjectItemCaseSensitive(f, "mimeType"), *h = cJSON_GetObjectItemCaseSensitive(f, "height");
        if (!cJSON_IsString(mt)) continue;
        const char *m = mt->valuestring;
        if (strstr(m, "video/mp4")) {
            int is_av1 = strstr(m, "av01") != NULL, is_avc = strstr(m, "avc1") != NULL;
            if (want_av1 ? !is_av1 : !is_avc) continue;
            if (!cJSON_IsNumber(h) || h->valueint > cap || h->valueint < 360) continue;
            cJSON *rp = rep_of(f);
            if (rp) cJSON_AddItemToArray(video, rp);
        } else if (strstr(m, "audio/mp4") && strstr(m, "mp4a")) {
            const cJSON *tr = cJSON_GetObjectItemCaseSensitive(f, "audioTrack"), *br = cJSON_GetObjectItemCaseSensitive(f, "bitrate");
            const cJSON *def = cJSON_GetObjectItemCaseSensitive(tr, "audioIsDefault"), *drc = cJSON_GetObjectItemCaseSensitive(f, "isDrc");
            if ((tr && !cJSON_IsTrue(def)) || cJSON_IsTrue(drc)) continue;   /* a dub, or the loudness-squashed copy */
            double b = cJSON_IsNumber(br) ? br->valuedouble : 0;
            if (audio && b <= audio_br) continue;
            cJSON *rp = rep_of(f);
            if (rp) { cJSON_Delete(audio); audio = rp; audio_br = b; }
        }
    }
    if (!cJSON_GetArraySize(video) || !audio) { cJSON_Delete(video); cJSON_Delete(audio); return NULL; }
    cJSON *d = cJSON_CreateObject();
    cJSON_AddItemToObject(d, "video", video);
    cJSON_AddItemToObject(d, "audio", audio);
    cJSON_AddNumberToObject(d, "duration", length_s);
    return d;
}

static void stream_task(HttpResponse *r, void *arg)
{
    Save *s = arg;
    char url[300], headers[400], key[128] = "", visitor[256] = "", why[300] = "";
    HttpResponse page;
    snprintf(url, sizeof url, "%s/watch?v=%s&hl=en", base_url(), s->id);
    snprintf(headers, sizeof headers, "User-Agent: %s\nAccept-Language: en-US,en;q=0.9\nCookie: SOCS=CAI; PREF=hl=en&tz=UTC", NUVIO_UA);   /* past the EU consent page */
    if (!get(url, headers, 20, &page)) LOGW("youtube: the watch page answered %ld %s", page.status, page.status ? "" : page.error);
    between(page.body, "\"INNERTUBE_API_KEY\"", key, sizeof key);
    between(page.body, "\"VISITOR_DATA\"", visitor, sizeof visitor);
    http_response_free(&page);
    if (!key[0]) LOGW("youtube: no API key on the watch page");
    char *hls = NULL, *best = NULL;
    cJSON *adaptive = NULL; double length_s = 0;
    double best_score = -1; int best_n = 1, best_mp4 = 0;
    for (size_t i = 0; i < sizeof CLIENTS / sizeof *CLIENTS; i++) {
        const Client *c = &CLIENTS[i];
        if (c->needs_js) continue;
        char this_why[300] = "";
        cJSON *resp = player_request(c, s->id, visitor[0] ? visitor : NULL, 0, key[0] ? key : NULL, this_why, sizeof this_why);
        if (!why[0]) snprintf(why, sizeof why, "%s", this_why);
        const cJSON *sd = cJSON_GetObjectItemCaseSensitive(resp, "streamingData");
        const cJSON *m = cJSON_GetObjectItemCaseSensitive(sd, "hlsManifestUrl"), *f;
        if (!hls && cJSON_IsString(m) && m->valuestring[0]) { hls = strdup(m->valuestring); LOGI("youtube: %s playlist from the %s client", s->id, c->name); }
        cJSON_ArrayForEach(f, cJSON_GetObjectItemCaseSensitive(sd, "formats")) {
            const cJSON *u = cJSON_GetObjectItemCaseSensitive(f, "url"), *mt = cJSON_GetObjectItemCaseSensitive(f, "mimeType");
            if (!cJSON_IsString(u) || (cJSON_IsString(mt) && !strstr(mt->valuestring, "video/"))) continue;
            const cJSON *h = cJSON_GetObjectItemCaseSensitive(f, "height"), *fps = cJSON_GetObjectItemCaseSensitive(f, "fps");
            const cJSON *br = cJSON_GetObjectItemCaseSensitive(f, "bitrate");
            double score = (cJSON_IsNumber(h) ? h->valuedouble : 0) * 1e9 + (cJSON_IsNumber(fps) ? fps->valuedouble : 0) * 1e6 + (cJSON_IsNumber(br) ? br->valuedouble : 0);
            const char *q = strchr(u->valuestring, '?');
            char *nv = q ? param(q + 1, "n") : NULL;
            int has_n = nv && *nv; free(nv);
            int mp4 = cJSON_IsString(mt) && strstr(mt->valuestring, "mp4") != NULL;
            if (score > best_score || (score == best_score && (best_n > has_n || (best_n == has_n && mp4 > best_mp4)))) {
                free(best); best = strdup(u->valuestring); best_score = score; best_n = has_n; best_mp4 = mp4;
            }
        }
        if (!adaptive && cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(sd, "adaptiveFormats")) > 0) {   /* the first client's, visionOS first as Nuvio prefers */
            adaptive = cJSON_Duplicate(cJSON_GetObjectItemCaseSensitive(sd, "adaptiveFormats"), 1);
            const cJSON *ad = cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(resp, "videoDetails"), "lengthSeconds");
            if (cJSON_IsString(ad)) length_s = atof(ad->valuestring);
            LOGI("youtube: %s separate video and audio from the %s client", s->id, c->name);
        }
        cJSON_Delete(resp);
    }
    /* every way to play it, best first: separate video and audio (DASH, up to 4K), the
     * playlist, a file with sound (360p at most these days) */
    cJSON *out = cJSON_CreateObject();
    cJSON *dash = adaptive ? dash_pick(adaptive, length_s) : NULL;
    if (dash) cJSON_AddItemToObject(out, "dash", dash);
    if (hls) cJSON_AddStringToObject(out, "hls", hls);
    char *file = best ? reachable_mirror(best) : NULL;
    if (file) cJSON_AddStringToObject(out, "file", file);
    LOGI("youtube: %s streams as %s%s%s", s->id, dash ? "DASH, " : "", hls ? "HLS, " : "", file ? "a file" : "nothing else");
    if (dash || hls || file) { r->status = 200; r->body = cJSON_PrintUnformatted(out); r->len = strlen(r->body); }
    else fail(r, "%.250s", why[0] ? why : best ? "no YouTube server answered" : "YouTube offered nothing to stream");
    cJSON_Delete(out); cJSON_Delete(adaptive);
    free(hls); free(best); free(file);
}

static void save_task(HttpResponse *r, void *arg)
{
    Save *s = arg;
    const char *base = base_url();
    char url[800], headers[600], dir[560];
    snprintf(dir, sizeof dir, "%s/yt", store_dir()); mkdir(dir, 0755);
    snprintf(dir, sizeof dir, "%s/sounds", store_dir()); mkdir(dir, 0755);

    char player_id[32] = "", visitor[256] = "";
    watch_page(s->id, visitor, sizeof visitor, player_id, sizeof player_id);

    /* 2. each client in turn until one may play it */
    const Client *c = NULL;
    cJSON *resp = NULL;
    char *player_js = NULL, why[300] = "";
    for (size_t i = 0; i < sizeof CLIENTS / sizeof *CLIENTS && !resp; i++) {
        c = &CLIENTS[i];
        int sts = 0;
        if (c->needs_js) {   /* the signature timestamp, from the player script (kept per version) */
            if (!player_id[0]) {
                HttpResponse ifr;
                snprintf(url, sizeof url, "%s/iframe_api", base);
                if (get(url, NULL, 20, &ifr)) player_id_in(ifr.body, player_id, sizeof player_id);
                http_response_free(&ifr);
            }
            if (!player_id[0]) { if (!why[0]) snprintf(why, sizeof why, "YouTube's player version was not found"); continue; }
            char sts_path[600];
            snprintf(sts_path, sizeof sts_path, "%s/yt/%s.sts", store_dir(), player_id);
            char *sts_text = read_file(sts_path, NULL);
            sts = sts_text ? atoi(sts_text) : 0;
            free(sts_text);
            if (!sts && (player_js || (player_js = get_player(player_id)))) {
                sts = signature_timestamp(player_js);
                char t[16]; snprintf(t, sizeof t, "%d", sts);
                write_file(sts_path, t, strlen(t));
            }
        }
        char this_why[300] = "";
        resp = player_request(c, s->id, visitor[0] ? visitor : NULL, sts, NULL, this_why, sizeof this_why);
        if (!why[0]) snprintf(why, sizeof why, "%s", this_why);   /* the first refusal says the most */
        if (resp) {
            const cJSON *len = cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(resp, "videoDetails"), "lengthSeconds");
            if (cJSON_IsString(len) && atoi(len->valuestring) > MAX_SECONDS) { fail(r, "it is longer than an hour%s", ""); cJSON_Delete(resp); free(player_js); return; }
            if (!pick_format(cJSON_GetObjectItemCaseSensitive(resp, "streamingData"), s->video).fmt) {
                LOGW("youtube: %s client offered no playable format", c->name);
                cJSON_Delete(resp); resp = NULL;
            }
        }
    }
    if (!resp) { fail(r, "%.250s", why[0] ? why : "YouTube offered no playable format"); free(player_js); return; }
    const cJSON *details = cJSON_GetObjectItemCaseSensitive(resp, "videoDetails");
    Pick p = pick_format(cJSON_GetObjectItemCaseSensitive(resp, "streamingData"), s->video);

    /* 4. the format's URL: a plain one, or a signature cipher; both carry the n challenge */
    const cJSON *plain = cJSON_GetObjectItemCaseSensitive(p.fmt, "url");
    const cJSON *cipher = cJSON_GetObjectItemCaseSensitive(p.fmt, "signatureCipher");
    char *media = cJSON_IsString(plain) ? strdup(plain->valuestring) : cJSON_IsString(cipher) ? param(cipher->valuestring, "url") : NULL;
    char *sig = cJSON_IsString(cipher) ? param(cipher->valuestring, "s") : NULL;
    char *sp = cJSON_IsString(cipher) ? param(cipher->valuestring, "sp") : NULL;
    const char *q = media ? strchr(media, '?') : NULL;
    char *n = q && c->needs_js ? param(q + 1, "n") : NULL;   /* other clients' URLs play as they are */
    const cJSON *clen = cJSON_GetObjectItemCaseSensitive(p.fmt, "contentLength");
    uint64_t expect = cJSON_IsString(clen) ? strtoull(clen->valuestring, NULL, 10) : 0;
    const cJSON *title = cJSON_GetObjectItemCaseSensitive(details, "title");
    LOGI("youtube: %s \"%.80s\", itag %d from the %s client%s%s", s->id, cJSON_IsString(title) ? title->valuestring : "",
         itag(p.fmt), c->name,
         sig ? ", ciphered" : "", n ? ", n challenge" : "");
    cJSON_Delete(resp);
    int ok = media != NULL;
    if (!ok) fail(r, "the format had no address%s", "");
    if (ok && (n || sig)) {
        char *n2 = NULL, *sig2 = NULL, err[400] = "";
        ok = solve(player_id, &player_js, n, sig, &n2, &sig2, err, sizeof err);
        if (!ok && sig) fail(r, "the signature could not be solved: %.200s", err);
        else {
            if (!ok) LOGW("youtube: n challenge not solved (%s); trying unsolved, it may be slow or refused", err);
            ok = 1;
            if (n2) { char *m = with_n(media, n2); free(media); media = m; }
            if (sig2) {
                char enc[600];
                http_url_encode(sig2, enc, sizeof enc);
                size_t l = strlen(media) + strlen(enc) + 32;
                char *m = malloc(l);
                snprintf(m, l, "%s&%s=%s", media, sp ? sp : "signature", enc);
                free(media); media = m;
            }
        }
        free(n2); free(sig2);
    }
    free(player_js); free(sig); free(sp); free(n);

    /* 5. the file, in pieces, into the sounds folder */
    if (ok) {
        char name[80], path[700];
        file_name(name, sizeof name, s->id, p.video, p.ext);
        snprintf(path, sizeof path, "%s/sounds/%s", store_dir(), name);
        snprintf(headers, sizeof headers, "User-Agent: %s\nOrigin: https://www.youtube.com", c->ua);
        if (download(media, headers, path, expect, r)) { r->status = 200; r->body = strdup(name); r->len = strlen(name); }
    }
    free(media);
}

/* ---- the main-thread side ------------------------------------------------------ */

static char g_busy[8][24];   /* "id/kind" being saved now, so a second request waits for the first */

static void on_saved(HttpResponse *r, void *user)
{
    Save *s = user;
    const char *name = r->status == 200 && r->body && r->body[0] ? r->body : NULL;
    if (name) LOGI("youtube: %s ready as %s", s->id, name);
    else LOGW("youtube: %s failed: %s", s->id, r->error);
    for (int i = 0; i < 8; i++) if (!strncmp(g_busy[i], s->id, 11)) g_busy[i][0] = 0;
    if (s->cb) s->cb(name, name ? NULL : r->error[0] ? r->error : "it could not be saved", s->user);
    free(s);
}

void youtube_save(const char *url, int video, YoutubeCallback cb, void *user)
{
    char id[16];
    if (!youtube_video_id(url, id)) { if (cb) cb(NULL, "not a YouTube video link", user); return; }
    char key[24];
    snprintf(key, sizeof key, "%s/%d", id, video);
    int slot = -1;
    for (int i = 0; i < 8; i++) {
        if (!strcmp(g_busy[i], key)) { if (cb) cb(NULL, "it is already being saved", user); return; }
        if (!g_busy[i][0] && slot < 0) slot = i;
    }
    if (slot >= 0) snprintf(g_busy[slot], sizeof g_busy[slot], "%s", key);
    Save *s = calloc(1, sizeof *s);
    snprintf(s->url, sizeof s->url, "%s", url);
    snprintf(s->id, sizeof s->id, "%s", id);
    s->video = video; s->cb = cb; s->user = user;
    LOGI("youtube: saving %s (%s) on the TV", id, video ? "video" : "audio");
    http_task_async("youtube.com", save_task, s, on_saved, s);
}

static void on_streamed(HttpResponse *r, void *user)
{
    Save *s = user;
    const char *u = r->status == 200 && r->body && r->body[0] ? r->body : NULL;
    if (!u) LOGW("youtube: %s cannot stream: %s", s->id, r->error);
    if (s->cb) s->cb(u, u ? NULL : r->error[0] ? r->error : "it cannot be streamed", s->user);
    free(s);
}

void youtube_stream(const char *url, YoutubeCallback cb, void *user)
{
    char id[16];
    if (!youtube_video_id(url, id)) { if (cb) cb(NULL, "not a YouTube video link", user); return; }
    Save *s = calloc(1, sizeof *s);
    snprintf(s->url, sizeof s->url, "%s", url);
    snprintf(s->id, sizeof s->id, "%s", id);
    s->cb = cb; s->user = user;
    LOGI("youtube: asking to stream %s", id);
    http_task_async("youtube.com", stream_task, s, on_streamed, s);
}

