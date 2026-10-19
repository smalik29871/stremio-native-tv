#ifndef SNTV_STORE_H
#define SNTV_STORE_H
#include "../vendor/cJSON.h"

/* Small JSON settings file in the platform's writable app directory.
 * Holds the auth key, cached addon collection and UI preferences. */
int   store_init(const char *dir);
const char *store_dir(void);
cJSON *store_root(void);                 /* mutable; call store_save after edits */
const char *store_get_str(const char *key, const char *def);
void  store_set_str(const char *key, const char *value);  /* NULL deletes */
int   store_get_int(const char *key, int def);
void  store_set_int(const char *key, int value);
void  store_set_json(const char *key, cJSON *value_owned);
cJSON *store_get_json(const char *key);  /* borrowed */
/* Queues the settings for the writer thread: written to flash (fsync) before it replaces
 * settings.json, the previous copy kept as settings.json.bak. A power cut can lose the
 * last seconds of changes, never the file. */
int   store_save(void);
void  store_flush(void);                 /* wait until the queued settings are on flash */
void  store_shutdown(void);              /* flushes */

/* Read/write whole files in the store dir (used for cached JSON responses). */
char *store_read_file(const char *name);         /* malloc'd or NULL */
int   store_write_file(const char *name, const char *data, size_t len);

#endif
