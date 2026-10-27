/* See verdict.h. */
#include "verdict.h"
#include "keyrelay.h"
#include "http.h"
#include "log.h"
#include "sha256.h"
#include "../vendor/cJSON.h"
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { SLOTS = 4096, BATCH = 60 };
typedef struct { char key[VERDICT_KEY]; int asked, ok, fail, told; } Slot;   /* told: 1 plays, 2 fails sent */
static Slot g_slots[SLOTS];
static int g_used;
static char g_device[48];
static unsigned g_gen;
static int g_inflight;
static Uint32 g_asked_at;

void verdict_set_device(const char *device)
{
    size_t n = 0;
    for (const char *c = device ? device : ""; *c && n + 1 < sizeof g_device; c++) {   /* lower case, letters, digits and dashes */
        char ch = *c >= 'A' && *c <= 'Z' ? (char)(*c - 'A' + 'a') : *c;
        if ((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9')) g_device[n++] = ch;
        else if (n && g_device[n - 1] != '-') g_device[n++] = '-';
    }
    while (n && g_device[n - 1] == '-') n--;
    g_device[n] = 0;
    if (n) LOGI("verdicts: this is a %s", g_device);
}

const char *verdict_device(void) { return g_device; }

int verdict_key(const char *video_hash, const char *info_hash, int file_idx, const char *filename, uint64_t size, char out[VERDICT_KEY])
{
    char id[600], hex[65];
    out[0] = 0;
    if (video_hash && strlen(video_hash) == 16) snprintf(id, sizeof id, "h:%s", video_hash);
    else if (info_hash && strlen(info_hash) >= 32) snprintf(id, sizeof id, "t:%s:%d", info_hash, file_idx < 0 ? 0 : file_idx);
    else if (filename && *filename && size > 0) snprintf(id, sizeof id, "f:%s:%llu", filename, (unsigned long long)size);
    else return 0;
    for (char *c = id; *c; c++) if (*c >= 'A' && *c <= 'Z') *c = (char)(*c - 'A' + 'a');   /* the same file, however it is cased */
    sha256_hex(id, strlen(id), hex);
    memcpy(out, hex, VERDICT_KEY - 1);
    out[VERDICT_KEY - 1] = 0;
    return 1;
}

static Slot *slot(const char *key, int add)
{
    if (!key || strlen(key) != VERDICT_KEY - 1) return NULL;
    unsigned h = 0;
    for (int i = 0; i < 8; i++) h = h * 16 + (unsigned)(key[i] <= '9' ? key[i] - '0' : (key[i] | 0x20) - 'a' + 10);
    for (int i = 0; i < SLOTS; i++) {
        Slot *s = &g_slots[(h + (unsigned)i) % SLOTS];
        if (!s->key[0]) {
            if (!add || g_used > SLOTS * 3 / 4) return NULL;   /* nearly full: a long session stops asking */
            memcpy(s->key, key, VERDICT_KEY); g_used++;
            return s;
        }
        if (!strcmp(s->key, key)) return s;
    }
    return NULL;
}

static void on_verdicts(HttpResponse *r, void *user)
{
    (void)user;
    if (g_inflight > 0) g_inflight--;
    if (keyrelay_failed(KR_VERDICT, r->status) || r->status != 200 || !r->body) return;
    cJSON *j = cJSON_Parse(r->body);
    int known = 0;
    for (const cJSON *e = j ? j->child : NULL; e; e = e->next) {
        Slot *s = slot(e->string, 0);
        const cJSON *ok = cJSON_GetArrayItem(e, 0), *fail = cJSON_GetArrayItem(e, 1);
        if (!s || !cJSON_IsNumber(ok) || !cJSON_IsNumber(fail)) continue;
        s->ok = ok->valueint; s->fail = fail->valueint; known++;
    }
    cJSON_Delete(j);
    if (known) { g_gen++; LOGI("verdicts: %d of these files were played on a %s before", known, g_device); }
}

static void ask(const char *url)
{
    if (!http_request_async(url, NULL, on_verdicts, NULL)) return;
    g_inflight++; g_asked_at = SDL_GetTicks();
}

void verdict_want(const char (*keys)[VERDICT_KEY], int n)
{
    const char *base = g_device[0] ? keyrelay_base(KR_VERDICT) : NULL;
    if (!base) return;
    char url[400 + BATCH * VERDICT_KEY];
    int len = snprintf(url, sizeof url, "%s?d=%s&k=", base, g_device), asked = 0;
    for (int i = 0; i < n; i++) {
        Slot *s = slot(keys[i], 1);
        if (!s || s->asked) continue;
        s->asked = 1;
        len += snprintf(url + len, sizeof url - (size_t)len, "%s%s", asked ? "," : "", s->key);
        if (++asked == BATCH) { ask(url); asked = 0; len = snprintf(url, sizeof url, "%s?d=%s&k=", base, g_device); }
    }
    if (asked) ask(url);
}

int verdict_waiting(void) { return g_inflight > 0 && SDL_GetTicks() - g_asked_at < 1500; }

long verdict_adjust(const char *key)
{
    const Slot *s = key && key[0] ? slot(key, 0) : NULL;
    if (!s || (!s->ok && !s->fail)) return 0;
    if (s->fail >= 2 && s->fail > 2 * s->ok) return -1200;   /* keeps failing on this kind of TV: as a format it can't play */
    if (s->fail > s->ok) return -300;
    if (!s->fail) return 80;   /* played on this kind of TV, and never failed */
    return 0;
}

unsigned verdict_generation(void) { return g_gen; }

static void on_told(HttpResponse *r, void *user) { (void)user; keyrelay_failed(KR_VERDICT, r->status); }

void verdict_note(const char *key, int worked)
{
    const char *base = g_device[0] && key && key[0] ? keyrelay_base(KR_VERDICT) : NULL;
    Slot *s = base ? slot(key, 1) : NULL;
    if (!s || s->told & (worked ? 1 : 2)) return;
    s->told |= worked ? 1 : 2;
    if (worked) s->ok++; else s->fail++;
    char url[400];
    snprintf(url, sizeof url, "%s/%s?d=%s", base, key, g_device);
    HttpOptions o = { .post_json = worked ? "ok" : "fail", .headers = "Content-Type: text/plain", .timeout_s = 10, .deliver = 1 };
    http_request_async_ex(url, &o, on_told, NULL);
    LOGI("verdicts: told the relay this file %s on a %s", worked ? "plays" : "failed", g_device);
}
