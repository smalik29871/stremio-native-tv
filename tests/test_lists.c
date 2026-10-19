#include "lists.h"
#include "store.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int failures;
#define CHECK(c) do { if (!(c)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

static MetaPreview title(const char *id, const char *name)
{
    MetaPreview m; memset(&m, 0, sizeof m);
    m.id = (char *)id; m.type = (char *)"movie"; m.name = (char *)name; m.poster = (char *)"http://x/p.png";
    return m;
}

int main(void)
{
    char dir[256];
    snprintf(dir, sizeof dir, "/tmp/sntv-lists-%d", (int)getpid());
    mkdir(dir, 0755);
    store_init(dir);

    lists_set_owner("sam@example.com");
    int v0 = lists_version();
    MetaPreview a = title("tt1", "One"), b = title("tt2", "Two");
    CHECK(lists_toggle(0, &a) == 1 && lists_toggle(0, &b) == 1);
    CHECK(lists_has(0, "tt1") && lists_has(0, "tt2") && !lists_has(1, "tt1"));
    CHECK(lists_count(0) == 2 && lists_version() > v0);
    MetaPreview *m; int n = lists_items(0, &m);
    CHECK(n == 2 && strcmp(m[0].id, "tt2") == 0 && strcmp(m[1].name, "One") == 0);   /* newest first */
    meta_previews_free(m, n);
    CHECK(lists_toggle(0, &a) == 0 && !lists_has(0, "tt1") && lists_count(0) == 1);   /* a second time takes it out */
    CHECK(lists_toggle(LIST_COUNT, &a) == -1 && lists_toggle(-1, &a) == -1);

    /* each profile has its own */
    lists_set_owner("alex@example.com");
    CHECK(!lists_has(0, "tt2") && lists_count(0) == 0);
    lists_set_owner("sam@example.com");
    CHECK(lists_has(0, "tt2"));

    /* a list keeps the newest LIST_MAX */
    char ids[LIST_MAX + 5][16];
    for (int i = 0; i < LIST_MAX + 5; i++) { snprintf(ids[i], 16, "x%d", i); MetaPreview t = title(ids[i], "T"); lists_toggle(3, &t); }
    CHECK(lists_count(3) == LIST_MAX && lists_has(3, ids[LIST_MAX + 4]) && !lists_has(3, ids[0]));

    store_shutdown();
    char cmd[300]; snprintf(cmd, sizeof cmd, "rm -rf %s", dir);
    if (system(cmd) != 0) {}
    if (failures) { fprintf(stderr, "%d failure(s)\n", failures); return 1; }
    puts("lists: ok");
    return 0;
}
