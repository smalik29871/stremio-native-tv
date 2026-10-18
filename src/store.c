#include "store.h"
#include <SDL.h>
#include "log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char g_dir[1024];
static cJSON *g_root;

static void path_for(const char *name, char *out, size_t n)
{
    snprintf(out, n, "%s/%s", g_dir, name);
}

char *store_read_file(const char *name)
{
    char path[1200];
    path_for(name, path, sizeof path);
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz < 0 || sz > 64L * 1024 * 1024) { fclose(f); return NULL; }
    char *buf = malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t got = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[got] = 0;
    return buf;
}

int store_write_file(const char *name, const char *data, size_t len)
{
    char path[1200], tmp[1210];
    path_for(name, path, sizeof path);
    snprintf(tmp, sizeof tmp, "%s.%lu.tmp", path, (unsigned long)SDL_ThreadID());   /* workers write caches too */
    FILE *f = fopen(tmp, "wb");
    if (!f) return -1;
    size_t wrote = fwrite(data, 1, len, f);
    fclose(f);
    if (wrote != len) { remove(tmp); return -1; }
    return rename(tmp, path);
}

int store_init(const char *dir)
{
    snprintf(g_dir, sizeof g_dir, "%s", dir);
    size_t n = strlen(g_dir);
    if (n && g_dir[n - 1] == '/') g_dir[n - 1] = 0;
    char *txt = store_read_file("settings.json");
    g_root = txt ? cJSON_Parse(txt) : NULL;
    free(txt);
    if (!g_root) g_root = cJSON_CreateObject();
    return 0;
}

const char *store_dir(void) { return g_dir; }
cJSON *store_root(void) { return g_root; }

const char *store_get_str(const char *key, const char *def)
{
    cJSON *v = cJSON_GetObjectItemCaseSensitive(g_root, key);
    return cJSON_IsString(v) ? v->valuestring : def;
}

void store_set_str(const char *key, const char *value)
{
    cJSON_DeleteItemFromObjectCaseSensitive(g_root, key);
    if (value) cJSON_AddStringToObject(g_root, key, value);
}

int store_get_int(const char *key, int def)
{
    cJSON *v = cJSON_GetObjectItemCaseSensitive(g_root, key);
    return cJSON_IsNumber(v) ? v->valueint : def;
}

void store_set_int(const char *key, int value)
{
    cJSON_DeleteItemFromObjectCaseSensitive(g_root, key);
    cJSON_AddNumberToObject(g_root, key, value);
}

void store_set_json(const char *key, cJSON *value_owned)
{
    cJSON_DeleteItemFromObjectCaseSensitive(g_root, key);
    if (value_owned) cJSON_AddItemToObject(g_root, key, value_owned);
}

cJSON *store_get_json(const char *key)
{
    return cJSON_GetObjectItemCaseSensitive(g_root, key);
}

int store_save(void)
{
    char *txt = cJSON_Print(g_root);
    if (!txt) return -1;
    int rc = store_write_file("settings.json", txt, strlen(txt));
    free(txt);
    if (rc != 0) LOGW("store: failed to save settings");
    return rc;
}

void store_shutdown(void)
{
    cJSON_Delete(g_root);
    g_root = NULL;
}
