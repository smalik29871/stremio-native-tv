#include "update_channel.h"
#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(c) do { if (!(c)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

int main(void)
{
    /* phones and tablets (Android, handheld) read their own file */
    CHECK(!strcmp(update_manifest_file(1, 0, 1), "mobile.json"));
    /* Android TV and Fire TV (not handheld) stay with the shared Android file, as older apps do */
    CHECK(!strcmp(update_manifest_file(1, 0, 0), "android.json"));
    /* the others never read the mobile one, handheld or not */
    CHECK(!strcmp(update_manifest_file(0, 1, 0), "windows.json"));
    CHECK(!strcmp(update_manifest_file(0, 1, 1), "windows.json"));
    CHECK(!strcmp(update_manifest_file(0, 0, 0), "repo.json"));
    CHECK(!strcmp(update_manifest_file(0, 0, 1), "repo.json"));
    /* four families, four files, none shared by two families except TV Android and old apps */
    const char *f[4] = { update_manifest_file(0, 0, 0), update_manifest_file(1, 0, 1), update_manifest_file(1, 0, 0), update_manifest_file(0, 1, 0) };
    for (int i = 0; i < 4; i++) for (int j = i + 1; j < 4; j++) CHECK(strcmp(f[i], f[j]) != 0);
    if (failures) { fprintf(stderr, "%d check(s) failed\n", failures); return 1; }
    puts("update channels: ok");
    return 0;
}
