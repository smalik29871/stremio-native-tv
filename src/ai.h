/* An optional language-model helper behind any OpenAI-compatible chat endpoint
 * (Groq, OpenRouter, Google Gemini, Ollama on the LAN, OpenAI, ...). Settings >
 * Accounts holds the provider, key and model. Everything here is best-effort: a
 * missing key, a rate limit or a bad answer only means the feature stays quiet. */
#pragma once
#include <stddef.h>

enum { AI_OFF, AI_GROQ, AI_OPENROUTER, AI_GEMINI, AI_CUSTOM, AI_PROVIDER_COUNT };
extern const char *const AI_PROVIDER_NAMES[AI_PROVIDER_COUNT];

int  ai_enabled(void);                 /* a provider is chosen and (where needed) a key is set */
const char *ai_base_url(void);         /* the endpoint in use (preset or custom) */
const char *ai_model(void);            /* the model in use (preset or the user's) */
const char *ai_key_hint(int provider); /* where to get a free key, one line */

/* text is NULL on failure, with a short reason in `error`. Runs on the main thread. */
typedef void (*AiCallback)(const char *text, const char *error, void *user);
/* Returns a request id (0 when AI is off: the callback is not called). `json_mode`
 * asks the model for JSON only (the reply is trimmed to the outermost {...} or [...]). */
unsigned ai_ask(const char *system, const char *user_msg, int max_tokens, int json_mode, AiCallback cb, void *user);
void ai_cancel(unsigned id);

/* For a network thread, which may not read the settings: the endpoint, model and key
 * copied on the main thread (0 when AI is off), then one JSON question asked there,
 * blocking up to timeout_s. The answer is trimmed to its {...} (malloc'd), or NULL. */
typedef struct AiSnap { char url[600], model[128], key[256]; int json_format; } AiSnap;
int   ai_snapshot(AiSnap *s);
char *ai_ask_now(const AiSnap *s, const char *system, const char *user_msg, int timeout_s);

/* The provider's chat models, best first (ids is NULL with an error on failure). */
typedef void (*AiModelsCallback)(const char *const *ids, int n, const char *error, void *user);
void ai_list_models(AiModelsCallback cb, void *user);
int  ai_models_parse(const char *json, char ids[][96], int max);   /* the chat models in a /models reply, best first */
