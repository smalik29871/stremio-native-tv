#include "store.h"
#include <SDL.h>
#include "log.h"
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static char g_dir[1024];
static cJSON *g_root;

/* The settings writer: one thread, the newest text wins. */
static SDL_Thread *g_writer;
static SDL_mutex *g_wlock;
static SDL_cond *g_wcond;
static char *g_pending;          /* settings text waiting to be written */
static int g_writing, g_wquit;

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

/* Written and on flash before it takes the name: fsync the file, keep the old copy as
 * .bak, rename, fsync the folder so the rename itself survives a power cut. */
static int write_durable(const char *name, const char *data, size_t len)
{
    char path[1200], tmp[1210], bak[1210];
    path_for(name, path, sizeof path);
    snprintf(tmp, sizeof tmp, "%s.writing", path);
    snprintf(bak, sizeof bak, "%s.bak", path);
    int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return -1;
    size_t done = 0;
    while (done < len) { ssize_t w = write(fd, data + done, len - done); if (w <= 0) break; done += (size_t)w; }
    int ok = done == len && fsync(fd) == 0;
    close(fd);
    if (!ok) { remove(tmp); return -1; }
    rename(path, bak);   /* the first save has nothing to keep */
    if (rename(tmp, path) != 0) return -1;
    int dfd = open(g_dir, O_RDONLY);
    if (dfd >= 0) { fsync(dfd); close(dfd); }
    return 0;
}

static int writer_main(void *arg)
{
    (void)arg;
    SDL_LockMutex(g_wlock);
    for (;;) {
        while (!g_pending && !g_wquit) SDL_CondWait(g_wcond, g_wlock);
        if (!g_pending) break;
        char *txt = g_pending;
        g_pending = NULL;
        g_writing = 1;
        SDL_UnlockMutex(g_wlock);
        if (write_durable("settings.json", txt, strlen(txt)) != 0) LOGW("store: failed to save settings");
        free(txt);
        SDL_LockMutex(g_wlock);
        g_writing = 0;
        SDL_CondBroadcast(g_wcond);
    }
    SDL_UnlockMutex(g_wlock);
    return 0;
}

/* settings.json, else the copy before it: a power cut while saving used to leave an
 * empty or cut-off file, and the app started as if new (signed out, setup again). */
static cJSON *load_settings(void)
{
    char *txt = store_read_file("settings.json");
    cJSON *root = txt ? cJSON_Parse(txt) : NULL;
    if (root && cJSON_IsObject(root)) { free(txt); return root; }
    cJSON_Delete(root);
    char *bak = store_read_file("settings.json.bak");
    root = bak ? cJSON_Parse(bak) : NULL;
    if (txt) {   /* keep the damaged one for a look; it is not used */
        char path[1200], damaged[1210];
        path_for("settings.json", path, sizeof path);
        snprintf(damaged, sizeof damaged, "%s.damaged", path);
        rename(path, damaged);
    }
    if (root && cJSON_IsObject(root)) LOGW("store: settings.json was %s (power cut while saving?); restored the copy before it",
                                          !txt ? "missing" : !txt[0] ? "empty" : "damaged");
    else if (txt || bak) { LOGW("store: settings and their backup are unreadable; starting fresh"); cJSON_Delete(root); root = NULL; }
    free(txt); free(bak);
    return root;
}

int store_init(const char *dir)
{
    snprintf(g_dir, sizeof g_dir, "%s", dir);
    size_t n = strlen(g_dir);
    if (n && g_dir[n - 1] == '/') g_dir[n - 1] = 0;
    g_root = load_settings();
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
    if (!g_wlock) { g_wlock = SDL_CreateMutex(); g_wcond = SDL_CreateCond(); }
    SDL_LockMutex(g_wlock);
    free(g_pending);
    g_pending = txt;
    if (!g_writer) g_writer = SDL_CreateThread(writer_main, "store", NULL);
    SDL_CondBroadcast(g_wcond);
    SDL_UnlockMutex(g_wlock);
    if (!g_writer) { store_flush(); return -1; }
    return 0;
}

void store_flush(void)
{
    if (!g_wlock) return;
    SDL_LockMutex(g_wlock);
    if (!g_writer && g_pending) {   /* no thread: write here */
        char *txt = g_pending; g_pending = NULL;
        SDL_UnlockMutex(g_wlock);
        write_durable("settings.json", txt, strlen(txt));
        free(txt);
        return;
    }
    while (g_pending || g_writing) SDL_CondWait(g_wcond, g_wlock);
    SDL_UnlockMutex(g_wlock);
}

void store_shutdown(void)
{
    store_flush();
    if (g_writer) {
        SDL_LockMutex(g_wlock);
        g_wquit = 1;
        SDL_CondBroadcast(g_wcond);
        SDL_UnlockMutex(g_wlock);
        SDL_WaitThread(g_writer, NULL);
        g_writer = NULL;
    }
    cJSON_Delete(g_root);
    g_root = NULL;
}
