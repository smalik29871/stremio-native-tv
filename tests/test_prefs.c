#include "lang.h"
#include "store.h"
#include "trackmem.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int failures;
#define CHECK(c) do { if (!(c)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

int main(void)
{
    CHECK(lang_same("eng", "en"));
    CHECK(lang_same("English", "eng"));
    CHECK(lang_same("ger", "deu") && lang_same("de-AT", "deu"));
    CHECK(lang_same("chi", "zh") && lang_same("pt_BR", "por"));
    CHECK(!lang_same("eng", "spa"));
    CHECK(!lang_same("und", "und") && !lang_same("", "eng") && !lang_same(NULL, "eng"));
    CHECK(lang_same("tlh", "TLH") && !lang_same("tlh", "eng"));   /* unknown codes match only themselves */
    CHECK(!lang_known("und") && !lang_known("") && lang_known("jpn"));
    CHECK(lang_index("en") == 1 && lang_index("") == 0 && lang_index("xx") == 0);
    CHECK(!strcmp(lang_display("ja"), LANG_NAMES[lang_index("jpn")]));
    CHECK(lang_count() > 10 && LANG_CODES[0][0] == 0);

    char dir[] = "/tmp/sntv_prefs_XXXXXX";
    if (!mkdtemp(dir)) return 2;
    CHECK(store_init(dir) == 0);
    TrackMem m;
    CHECK(!trackmem_get("tt1", &m));
    trackmem_set("tt1", "jpn", NULL, SUBS_SRC_ANY);
    CHECK(trackmem_get("tt1", &m) && !strcmp(m.audio, "jpn") && !m.has_subs);
    trackmem_set("tt1", NULL, "eng", SUBS_SRC_ADDON);
    CHECK(trackmem_get("tt1", &m) && !strcmp(m.audio, "jpn") && m.has_subs && !strcmp(m.subs, "eng") && m.subs_src == SUBS_SRC_ADDON);
    trackmem_set("tt1", NULL, "", SUBS_SRC_ANY);   /* subtitles turned off */
    CHECK(trackmem_get("tt1", &m) && m.has_subs && m.subs[0] == 0);
    for (int i = 0; i < 310; i++) { char id[16]; snprintf(id, sizeof id, "s%d", i); trackmem_set(id, "eng", NULL, SUBS_SRC_ANY); }
    CHECK(cJSON_GetArraySize(store_get_json("trackMemory")) == 300);
    store_shutdown();
    /* survives a restart */
    CHECK(store_init(dir) == 0);
    CHECK(trackmem_get("s309", &m) && !strcmp(m.audio, "eng"));
    store_shutdown();
    char cmd[128];
    snprintf(cmd, sizeof cmd, "rm -rf %s", dir);
    if (system(cmd)) {}

    if (failures) { fprintf(stderr, "%d failure(s)\n", failures); return 1; }
    printf("prefs: all checks passed\n");
    return 0;
}
