#define SDL_MAIN_HANDLED
#include "../src/video/video_webos.c"

int youtube_id_valid(const char *id) { (void)id; return 0; }

static int failures, unloads, subscriptions;
static const char *reply;
static char unloaded[96];
#define CHECK(c) do { if (!(c)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

static const char *payload(LSMessage *m) { (void)m; return reply; }
static void error_init(LSError *e) { memset(e, 0, sizeof *e); }
static void error_free(LSError *e) { (void)e; }
static int call(LSHandle *sh, const char *uri, const char *body, LSFilterFunc fn, void *ctx, LSMessageToken *token, LSError *error)
{
    (void)sh; (void)fn; (void)ctx; (void)token; (void)error;
    if (strstr(uri, "/unload")) {
        cJSON *j = cJSON_Parse(body);
        const cJSON *id = cJSON_GetObjectItemCaseSensitive(j, "mediaId");
        if (cJSON_IsString(id)) snprintf(unloaded, sizeof unloaded, "%s", id->valuestring);
        cJSON_Delete(j); unloads++;
    } else if (strstr(uri, "/subscribe")) subscriptions++;
    return 1;
}

int main(void)
{
    g_lock = SDL_CreateMutex(); g_bus = (LSHandle *)(uintptr_t)1; g_session = 2;
    L.MessageGetPayload = payload; L.ErrorInit = error_init; L.ErrorFree = error_free;
    L.CallOneReply = call; L.Call = call;
    reply = "{\"returnValue\":true,\"mediaId\":\"cancelled-pipeline\"}";
    on_load_reply(NULL, NULL, (void *)(uintptr_t)1);
    CHECK(unloads == 1 && !strcmp(unloaded, "cancelled-pipeline"));
    CHECK(subscriptions == 0 && !g_media_id[0]);
    reply = "{\"returnValue\":true,\"mediaId\":\"current-pipeline\"}";
    on_load_reply(NULL, NULL, (void *)(uintptr_t)2);
    CHECK(unloads == 1 && subscriptions == 1 && !strcmp(g_media_id, "current-pipeline"));
    reply = "{\"returnValue\":false,\"errorCode\":300}";
    on_load_reply(NULL, NULL, (void *)(uintptr_t)1);
    CHECK(unloads == 1 && !strcmp(g_media_id, "current-pipeline"));
    SDL_DestroyMutex(g_lock);
    if (failures) return 1;
    puts("webOS cancelled load checks passed");
    return 0;
}
