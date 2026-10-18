/* api.strem.io account API, mirroring stremio-core's APIRequest types.
 * Every call is POST {API_URL}/api/{path} with a JSON body and returns
 * {"result": ...} or {"error": {"code", "message"}}. */
#ifndef SNTV_STREMIO_API_H
#define SNTV_STREMIO_API_H
#include "addon.h"
#include <stdint.h>

struct cJSON;

#define STREMIO_API_URL      "https://api.strem.io"
#define STREMIO_CINEMETA_URL "https://v3-cinemeta.strem.io/manifest.json"
#define STREMIO_OPENSUBS_URL "https://opensubtitles-v3.strem.io/manifest.json"
#define STREMIO_LIBRARY_COLLECTION "libraryItem"
#define STREMIO_LINK_API_URL "https://link.stremio.com/api/v2"

typedef struct LibraryItem {
    char *id, *name, *type, *poster, *poster_shape;
    int removed, temp;
    int64_t ctime_ms, mtime_ms;
    /* state */
    int64_t last_watched_ms;
    uint64_t time_watched, time_offset, overall_time_watched, duration; /* ms */
    int times_watched, flagged_watched, no_notif;
    char *video_id;
} LibraryItem;

typedef struct ApiError { int code; char message[256]; } ApiError;

/* Request bodies (malloc'd JSON; caller frees). Pair with http_request_async. */
char *api_body_login(const char *email, const char *password);
char *api_body_login_with_token(const char *token);
char *api_body_logout(const char *auth_key);
char *api_body_addon_collection_get(const char *auth_key);
char *api_body_addon_collection_set(const char *auth_key, const Addon *addons, int n);
char *api_body_datastore_get(const char *auth_key, const char *collection);
char *api_body_datastore_put(const char *auth_key, const char *collection, const LibraryItem *items, int n);

char *api_body_get_user(const char *auth_key);
/* QR / code sign-in (link.stremio.com): GET create, then poll GET read?code= */
const char *api_url_link_create(void);
const char *api_url_link_read(const char *code, char *buf, size_t n);
int api_parse_link_create(const char *json, char *code, size_t code_n, char *link, size_t link_n, char *qrcode, size_t qr_n, ApiError *err);
/* 0 = linked (auth_key malloc'd), 1 = not yet, -1 = error */
int api_parse_link_read(const char *json, char **auth_key, ApiError *err);
int api_parse_user(const char *json, char **email, ApiError *err);

/* Endpoint URLs */
const char *api_url_login(void);
const char *api_url_login_with_token(void);
const char *api_url_logout(void);
const char *api_url_addon_collection_get(void);
const char *api_url_addon_collection_set(void);
const char *api_url_datastore_get(void);
const char *api_url_datastore_put(void);
const char *api_url_get_user(void);

/* Response parsing. Each returns 0 on success, -1 on transport/JSON failure,
 * and 1 when the API returned {"error": ...} (err is filled). */
int api_parse_result(const char *json, struct cJSON **result_out, ApiError *err); /* caller deletes the *root*, see impl */
int api_parse_auth(const char *json, char **auth_key, char **email, ApiError *err);
int api_parse_addon_collection(const char *json, Addon **addons, int *n, ApiError *err);
int api_parse_library(const char *json, LibraryItem **items, int *n, ApiError *err);

void library_item_free(LibraryItem *it);
void library_items_free(LibraryItem *items, int n);
struct cJSON *library_item_to_json(const LibraryItem *it);

/* Built-in addons used before login (same defaults the official apps ship). */
extern const char *const STREMIO_DEFAULT_ADDON_URLS[];

#endif
