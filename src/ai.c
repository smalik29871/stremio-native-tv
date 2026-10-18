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

typedef struct Req { AiCallback cb; void *user; int json_mode; unsigned id; struct Req *next; } Req;
static Req *g_reqs;

static void req_unlink(Req *q)
{
    for (Req **pp = &g_reqs; *pp; pp = &(*pp)->next) if (*pp == q) { *pp = q->next; break; }
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
        if (r->status == 429) err = "The AI service is busy (rate limit): try again in a minute";
        else if (r->status == 401 || r->status == 403) err = "The AI key was refused: check Settings > API keys";
        else { snprintf(errbuf, sizeof errbuf, "AI error %ld%s%.100s", r->status, cJSON_IsString(m) ? ": " : "", cJSON_IsString(m) ? m->valuestring : ""); err = errbuf; }
    } else {
        snprintf(errbuf, sizeof errbuf, "Could not reach the AI service%s%.100s", r->error[0] ? ": " : "", r->error);
        err = errbuf;
    }
    if (root) cJSON_Delete(root);
    req_unlink(q);
    if (!q->cb) { free(text); free(q); return; }
    if (text) {
        /* fenced answers: ```json ... ``` */
        char *fence = strstr(text, "```");
        if (fence) { char *nl = strchr(fence, '\n'); char *end = nl ? strstr(nl + 1, "```") : NULL; if (nl && end) { *end = 0; memmove(text, nl + 1, strlen(nl + 1) + 1); } }
        q->cb(q->json_mode ? trim_json(text) : text, NULL, q->user);
        free(text);
    } else {
        LOGI("ai: %s", err);
        q->cb(NULL, err, q->user);
    }
    free(q);
}

unsigned ai_ask(const char *system, const char *user_msg, int max_tokens, int json_mode, AiCallback cb, void *user)
{
    if (!ai_enabled()) return 0;
    cJSON *body = cJSON_CreateObject();
    cJSON_AddStringToObject(body, "model", ai_model());
    cJSON *msgs = cJSON_AddArrayToObject(body, "messages");
    if (system && *system) { cJSON *m = cJSON_CreateObject(); cJSON_AddStringToObject(m, "role", "system"); cJSON_AddStringToObject(m, "content", system); cJSON_AddItemToArray(msgs, m); }
    { cJSON *m = cJSON_CreateObject(); cJSON_AddStringToObject(m, "role", "user"); cJSON_AddStringToObject(m, "content", user_msg); cJSON_AddItemToArray(msgs, m); }
    cJSON_AddNumberToObject(body, "temperature", json_mode ? 0.2 : 0.6);
    if (max_tokens > 0) cJSON_AddNumberToObject(body, "max_tokens", max_tokens);
    if (json_mode && g_app.ai_provider != AI_OPENROUTER) {   /* some free routes reject response_format */
        cJSON *rf = cJSON_AddObjectToObject(body, "response_format");
        cJSON_AddStringToObject(rf, "type", "json_object");
    }
    char *json = cJSON_PrintUnformatted(body);
    cJSON_Delete(body);
    char url[1200];
    const char *base = ai_base_url();
    size_t bl = strlen(base);
    snprintf(url, sizeof url, "%s%schat/completions", base, bl && base[bl - 1] == '/' ? "" : "/");
    Req *q = calloc(1, sizeof *q);
    q->cb = cb; q->user = user; q->json_mode = json_mode;
    HttpOptions opt = { 0 };
    opt.timeout_s = 60;
    opt.post_json = json;
    opt.bearer = g_app.ai_key && g_app.ai_key[0] ? g_app.ai_key : NULL;
    unsigned id = http_request_async_ex(url, &opt, on_reply, q);
    q->id = id; q->next = g_reqs; g_reqs = q;
    free(json);
    return id;
}

void ai_cancel(unsigned id)
{
    for (Req *q = g_reqs; q; q = q->next) if (q->id == id) { q->cb = NULL; return; }
}
