#include "store.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int failures;
#define CHECK(c) do { if (!(c)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

static char dir[256];

static void put(const char *name, const char *text)
{
    char path[400];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    FILE *f = fopen(path, "wb");
    fputs(text, f);
    fclose(f);
}

static int exists(const char *name)
{
    char path[400];
    struct stat st;
    snprintf(path, sizeof path, "%s/%s", dir, name);
    return stat(path, &st) == 0;
}

int main(void)
{
    snprintf(dir, sizeof dir, "/tmp/sntv-store-%d", (int)getpid());
    mkdir(dir, 0755);

    /* saves land on disk, and the one before is kept */
    store_init(dir);
    store_set_str("authKey", "first");
    store_save();
    store_flush();
    CHECK(exists("settings.json") && !exists("settings.json.bak"));
    store_set_str("authKey", "second");
    store_set_int("setupDone", 1);
    store_save();
    store_save();   /* coalesced: the newest wins */
    store_shutdown();
    CHECK(exists("settings.json") && exists("settings.json.bak"));
    store_init(dir);
    CHECK(!strcmp(store_get_str("authKey", ""), "second") && store_get_int("setupDone", 0) == 1);
    store_shutdown();

    /* a power cut left settings.json empty: the copy before it comes back */
    put("settings.json", "");
    put("settings.json.bak", "{\"authKey\":\"kept\",\"setupDone\":1}");
    store_init(dir);
    CHECK(!strcmp(store_get_str("authKey", ""), "kept") && store_get_int("setupDone", 0) == 1);
    CHECK(exists("settings.json.damaged"));
    store_shutdown();

    /* cut off half way */
    put("settings.json", "{\"authKey\":\"new\",\"setu");
    store_init(dir);
    CHECK(!strcmp(store_get_str("authKey", ""), "kept"));
    store_shutdown();

    /* both unreadable: a fresh start, not a crash */
    put("settings.json", "garbage");
    put("settings.json.bak", "");
    store_init(dir);
    CHECK(!strcmp(store_get_str("authKey", "none"), "none"));
    store_shutdown();

    /* secrets are sealed on disk and plain in memory (the stand-in cipher; Windows seals with DPAPI) */
    setenv("SNTV_FAKE_VAULT", "1", 1);
    put("settings.json", "{\"authKey\":\"plain-old-key\",\"theme\":3}");   /* an older, unsealed file */
    store_init(dir);
    CHECK(!strcmp(store_get_str("authKey", ""), "plain-old-key"));
    store_set_str("torboxKey", "tb-secret-123");
    store_set_str("userName", "visible-name");
    store_save();
    store_flush();
    {
        char path[400], body[2000] = "";
        snprintf(path, sizeof path, "%s/settings.json", dir);
        FILE *f = fopen(path, "rb");
        if (f) { size_t n = fread(body, 1, sizeof body - 1, f); body[n] = 0; fclose(f); }
        CHECK(!strstr(body, "plain-old-key") && !strstr(body, "tb-secret-123"));   /* neither written as they are */
        CHECK(strstr(body, "fake1:") != NULL);
        CHECK(strstr(body, "visible-name") != NULL);                               /* what is no secret stays readable */
    }
    store_shutdown();
    store_init(dir);
    CHECK(!strcmp(store_get_str("authKey", ""), "plain-old-key") && !strcmp(store_get_str("torboxKey", ""), "tb-secret-123"));
    store_shutdown();
    put("settings.json", "{\"authKey\":\"fake1:zz\",\"torboxKey\":\"fake1:0\"}");   /* sealed for no one here: dropped, not garbage */
    store_init(dir);
    CHECK(!strcmp(store_get_str("authKey", "x"), "") && !strcmp(store_get_str("torboxKey", "x"), ""));
    store_shutdown();
    unsetenv("SNTV_FAKE_VAULT");

    char cmd[300];
    snprintf(cmd, sizeof cmd, "rm -rf %s", dir);
    if (system(cmd) != 0) fprintf(stderr, "could not remove %s\n", dir);
    if (failures) fprintf(stderr, "%d failure(s)\n", failures);
    else printf("store: all checks passed\n");
    return failures != 0;
}
