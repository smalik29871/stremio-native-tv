#include "ai.h"
#include "app.h"
#include "http.h"
#include "log.h"
#include "../vendor/cJSON.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char *const AI_PROVIDER_NAMES[AI_PROVIDER_COUNT] = { "Off", "Groq", "OpenRouter", "Google Gemini", "Custom (OpenAI-compatible)" };

static const char *const PRESET_BASE[AI_PROVIDER_COUNT] = {
    "", "https://api.groq.com/openai/v1", "https://openrouter.ai/api/v1",
    "https://generativelanguage.googleapis.com/v1beta/openai", "" };
static const char *const PRESET_MODEL[AI_PROVIDER_COUNT] = {
    "", "llama-3.3-70b-versatile", "meta-llama/llama-3.3-70b-instruct:free", "gemini-2.5-flash", "" };

const char *ai_key_hint(int provider)
{
    switch (provider) {
    case AI_GROQ: return "Free at console.groq.com > API Keys. Fast; generous daily limits.";
    case AI_OPENROUTER: return "Free at openrouter.ai > Keys. Pick a model ending in :free.";
    case AI_GEMINI: return "Free at aistudio.google.com > Get API key. Best for subtitle translation (large limits).";
    case AI_CUSTOM: return "Any OpenAI-compatible server: an Ollama box on your network (http://192.168.1.10:11434/v1, no key), OpenAI, Mistral...";
    default: return "";
    }
}

const char *ai_base_url(void)
{
    const char *e = getenv("SNTV_AI_API");
    if (e && *e) return e;
    if (g_app.ai_provider == AI_CUSTOM) return g_app.ai_base ? g_app.ai_base : "";
    return PRESET_BASE[g_app.ai_provider < AI_PROVIDER_COUNT ? g_app.ai_provider : 0];
}

const char *ai_model(void)
{
    if (g_app.ai_model && g_app.ai_model[0]) return g_app.ai_model;
    return PRESET_MODEL[g_app.ai_provider < AI_PROVIDER_COUNT ? g_app.ai_provider : 0];
}

int ai_enabled(void)
{
    if (g_app.ai_provider <= AI_OFF || g_app.ai_provider >= AI_PROVIDER_COUNT) return 0;
    const char *base = ai_base_url();
    if (!base || strncmp(base, "http", 4) != 0) return 0;
    if (g_app.ai_provider != AI_CUSTOM && !(g_app.ai_key && g_app.ai_key[0]) && !getenv("SNTV_AI_API")) return 0;
    return ai_model()[0] != 0;
}

typedef struct Req {
    AiCallback cb; void *user; int json_mode;
    unsigned id, first_id;            /* first_id is what the caller holds (a retry gets a new id) */
    char *system, *msg; int max_tokens, retried;
    struct Req *next;
} Req;
static Req *g_reqs;

static void req_unlink(Req *q)
{
    for (Req **pp = &g_reqs; *pp; pp = &(*pp)->next) if (*pp == q) { *pp = q->next; break; }
}

static void req_free(Req *q) { free(q->system); free(q->msg); free(q); }

static void send_req(Req *q);

static void endpoint(const char *path, char *url, size_t n)
{
    const char *base = ai_base_url();
    size_t bl = strlen(base);
    snprintf(url, n, "%s%s%s", base, bl && base[bl - 1] == '/' ? "" : "/", path);
}

/* A chat model from the provider's list: the known good ones first, never speech,
 * embedding, image or guard models. */
static int model_rank(const char *id)
{
    static const char *const SKIP[] = { "whisper", "guard", "tts", "embed", "audio", "image", "vision", "moderation", "playai", "transcribe", "realtime", NULL };
    static const char *const GOOD[] = { "llama-3.3-70b", "gpt-oss-120b", "llama-4-maverick", "gemini-2.5-flash", "llama-4-scout", "gemini-2.0-flash",
                                        "qwen3", "kimi-k2", "deepseek", "mistral-small", "gpt-4o-mini", "gemma", "llama-3.1-8b", "llama", "gemini", "mistral", "gpt", NULL };
    for (int i = 0; SKIP[i]; i++) if (strstr(id, SKIP[i])) return -1;
    for (int i = 0; GOOD[i]; i++) if (strstr(id, GOOD[i])) return 100 - i + (strstr(id, ":free") ? 50 : 0);
    return 0;
}

int ai_models_parse(const char *json, char ids[][96], int max)
{
    cJSON *root = cJSON_Parse(json ? json : "");
    const cJSON *data = root ? cJSON_GetObjectItemCaseSensitive(root, "data") : NULL, *it;
    int n = 0, rank[64];
    cJSON_ArrayForEach(it, data) {
        const cJSON *id = cJSON_GetObjectItemCaseSensitive(it, "id");
        if (!cJSON_IsString(id)) continue;
        const char *name = strncmp(id->valuestring, "models/", 7) == 0 ? id->valuestring + 7 : id->valuestring;   /* Gemini */
        int r = model_rank(name);
        if (r < 0) continue;
        int k = n < max && n < 64 ? n++ : -1;
        if (k < 0) { if (r <= rank[n - 1]) continue; k = n - 1; }
        snprintf(ids[k], 96, "%s", name); rank[k] = r;
        for (; k > 0 && rank[k] > rank[k - 1]; k--) {
            char t[96]; memcpy(t, ids[k], 96); memcpy(ids[k], ids[k - 1], 96); memcpy(ids[k - 1], t, 96);
            int tr = rank[k]; rank[k] = rank[k - 1]; rank[k - 1] = tr;
        }
    }
    if (root) cJSON_Delete(root);
    return n;
}

typedef struct { AiModelsCallback cb; void *user; Req *retry; } ListReq;

static void on_models(HttpResponse *r, void *user)
{
    ListReq *l = user;
    char ids[12][96];
    int n = r->status == 200 ? ai_models_parse(r->body, ids, 12) : 0;
    if (l->retry) {   /* the model was missing: take the best available one and ask again */
        Req *q = l->retry;
        if (n && q->cb) {
            LOGI("ai: model %s not available, switching to %s", ai_model(), ids[0]);
            free(g_app.ai_model); g_app.ai_model = strdup(ids[0]); app_save_prefs();
            app_toast("AI model switched to %s", ids[0]);
            send_req(q);
        } else {
            req_unlink(q);
            if (q->cb) q->cb(NULL, "The AI model is not available: choose one in Settings > API keys > AI model", q->user);
            req_free(q);
        }
    } else if (l->cb) {
        char *v[12]; for (int i = 0; i < n; i++) v[i] = ids[i];
        l->cb(n ? (const char *const *)v : NULL, n, n ? NULL : r->status ? "The provider listed no chat models" : "Could not reach the AI service", l->user);
    }
    free(l);
}

static void list_models(ListReq *l)
{
    char url[1200];
    endpoint("models", url, sizeof url);
    HttpOptions opt = { .timeout_s = 20, .bearer = g_app.ai_key && g_app.ai_key[0] ? g_app.ai_key : NULL };
    http_request_async_ex(url, &opt, on_models, l);
}

void ai_list_models(AiModelsCallback cb, void *user)
{
    ListReq *l = calloc(1, sizeof *l);
    l->cb = cb; l->user = user;
    list_models(l);
}

/* Reasoning models write their thinking first: drop it. */
static void strip_think(char *text)
{
    char *a;
    while ((a = strstr(text, "<think>")) != NULL) {
        char *b = strstr(a, "</think>");
        if (!b) { *a = 0; return; }
        memmove(a, b + 8, strlen(b + 8) + 1);
    }
}

static char *trim_json(char *s)
{
    char *a = strchr(s, '{'), *b = strchr(s, '[');
    char *start = a && (!b || a < b) ? a : b;
    if (!start) return s;
    char close = *start == '{' ? '}' : ']';
    char *end = strrchr(start, close);
    if (end) end[1] = 0;
    return start;
}

static void on_reply(HttpResponse *r, void *user)
{
    Req *q = user;
    char *text = NULL; const char *err = NULL; char errbuf[160];
    cJSON *root = r->status ? cJSON_Parse(r->body ? r->body : "") : NULL;
    if (r->status == 200 && root) {
        const cJSON *choices = cJSON_GetObjectItemCaseSensitive(root, "choices");
        const cJSON *c0 = cJSON_IsArray(choices) ? cJSON_GetArrayItem(choices, 0) : NULL;
        const cJSON *msg = c0 ? cJSON_GetObjectItemCaseSensitive(c0, "message") : NULL;
        const cJSON *content = msg ? cJSON_GetObjectItemCaseSensitive(msg, "content") : NULL;
        if (cJSON_IsString(content) && content->valuestring) text = strdup(content->valuestring);
        else err = "The model sent an empty answer";
    } else if (root) {
        const cJSON *e = cJSON_GetObjectItemCaseSensitive(root, "error");
        const cJSON *m = e ? cJSON_GetObjectItemCaseSensitive(e, "message") : NULL;
        if ((r->status == 404 || r->status == 400) && cJSON_IsString(m) && strstr(m->valuestring, "model") && !q->retried && q->cb) {
            q->retried = 1;
            cJSON_Delete(root);
            ListReq *l = calloc(1, sizeof *l);
            l->retry = q;
            list_models(l);
            return;
        }
        if (r->status == 429) err = "The AI service is busy (rate limit): try again in a minute";
        else if (r->status == 401 || r->status == 403) err = "The AI key was refused: check Settings > API keys";
        else { snprintf(errbuf, sizeof errbuf, "AI error %ld%s%.100s", r->status, cJSON_IsString(m) ? ": " : "", cJSON_IsString(m) ? m->valuestring : ""); err = errbuf; }
    } else {
        snprintf(errbuf, sizeof errbuf, "Could not reach the AI service%s%.100s", r->error[0] ? ": " : "", r->error);
        err = errbuf;
    }
    if (root) cJSON_Delete(root);
    req_unlink(q);
    if (!q->cb) { free(text); req_free(q); return; }
    if (text) {
        strip_think(text);
        /* fenced answers: ```json ... ``` */
        char *fence = strstr(text, "```");
        if (fence) { char *nl = strchr(fence, '\n'); char *end = nl ? strstr(nl + 1, "```") : NULL; if (nl && end) { *end = 0; memmove(text, nl + 1, strlen(nl + 1) + 1); } }
        q->cb(q->json_mode ? trim_json(text) : text, NULL, q->user);
        free(text);
    } else {
        LOGI("ai: %s", err);
        q->cb(NULL, err, q->user);
    }
    req_free(q);
}

static void send_req(Req *q)
{
    cJSON *body = cJSON_CreateObject();
    cJSON_AddStringToObject(body, "model", ai_model());
    cJSON *msgs = cJSON_AddArrayToObject(body, "messages");
    if (q->system && *q->system) { cJSON *m = cJSON_CreateObject(); cJSON_AddStringToObject(m, "role", "system"); cJSON_AddStringToObject(m, "content", q->system); cJSON_AddItemToArray(msgs, m); }
    { cJSON *m = cJSON_CreateObject(); cJSON_AddStringToObject(m, "role", "user"); cJSON_AddStringToObject(m, "content", q->msg); cJSON_AddItemToArray(msgs, m); }
    cJSON_AddNumberToObject(body, "temperature", q->json_mode ? 0.2 : 0.6);
    if (q->max_tokens > 0) cJSON_AddNumberToObject(body, "max_tokens", q->max_tokens);
    if (q->json_mode && g_app.ai_provider != AI_OPENROUTER) {   /* some free routes reject response_format */
        cJSON *rf = cJSON_AddObjectToObject(body, "response_format");
        cJSON_AddStringToObject(rf, "type", "json_object");
    }
    char *json = cJSON_PrintUnformatted(body);
    cJSON_Delete(body);
    char url[1200];
    endpoint("chat/completions", url, sizeof url);
    HttpOptions opt = { .timeout_s = 60, .post_json = json, .bearer = g_app.ai_key && g_app.ai_key[0] ? g_app.ai_key : NULL };
    q->id = http_request_async_ex(url, &opt, on_reply, q);
    free(json);
}

unsigned ai_ask(const char *system, const char *user_msg, int max_tokens, int json_mode, AiCallback cb, void *user)
{
    if (!ai_enabled()) return 0;
    Req *q = calloc(1, sizeof *q);
    q->cb = cb; q->user = user; q->json_mode = json_mode;
    q->system = system ? strdup(system) : NULL;
    q->msg = strdup(user_msg);
    q->max_tokens = json_mode && max_tokens < 1200 ? 1200 : max_tokens;   /* reasoning models think before they answer */
    send_req(q);
    q->first_id = q->id;
    q->next = g_reqs; g_reqs = q;
    return q->first_id;
}

void ai_cancel(unsigned id)
{
    for (Req *q = g_reqs; q; q = q->next) if (q->first_id == id) { q->cb = NULL; return; }
}

int ai_snapshot(AiSnap *s)
{
    memset(s, 0, sizeof *s);
    if (!ai_enabled()) return 0;
    endpoint("chat/completions", s->url, sizeof s->url);
    snprintf(s->model, sizeof s->model, "%s", ai_model());
    snprintf(s->key, sizeof s->key, "%s", g_app.ai_key ? g_app.ai_key : "");
    s->json_format = g_app.ai_provider != AI_OPENROUTER;
    return 1;
}

char *ai_ask_now(const AiSnap *s, const char *system, const char *user_msg, int timeout_s)
{
    if (!s || !s->url[0]) return NULL;
    cJSON *body = cJSON_CreateObject();
    cJSON_AddStringToObject(body, "model", s->model);
    cJSON *msgs = cJSON_AddArrayToObject(body, "messages");
    cJSON *m = cJSON_CreateObject(); cJSON_AddStringToObject(m, "role", "system"); cJSON_AddStringToObject(m, "content", system); cJSON_AddItemToArray(msgs, m);
    m = cJSON_CreateObject(); cJSON_AddStringToObject(m, "role", "user"); cJSON_AddStringToObject(m, "content", user_msg); cJSON_AddItemToArray(msgs, m);
    cJSON_AddNumberToObject(body, "temperature", 0);
    cJSON_AddNumberToObject(body, "max_tokens", 1200);
    if (s->json_format) cJSON_AddStringToObject(cJSON_AddObjectToObject(body, "response_format"), "type", "json_object");
    char *json = cJSON_PrintUnformatted(body);
    cJSON_Delete(body);
    HttpOptions opt = { .timeout_s = timeout_s, .post_json = json, .bearer = s->key[0] ? s->key : NULL };
    HttpResponse r;
    http_request_ex(s->url, &opt, &r);
    free(json);
    char *out = NULL;
    cJSON *root = r.status == 200 && r.body ? cJSON_Parse(r.body) : NULL;
    const cJSON *c0 = cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(root, "choices"), 0);
    const cJSON *content = cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(c0, "message"), "content");
    if (cJSON_IsString(content)) {
        char *text = strdup(content->valuestring);
        strip_think(text);
        out = strdup(trim_json(text));
        free(text);
    } else LOGI("ai: no answer (%ld%s%s)", r.status, r.error[0] ? " " : "", r.error);
    cJSON_Delete(root);
    http_response_free(&r);
    return out;
}
