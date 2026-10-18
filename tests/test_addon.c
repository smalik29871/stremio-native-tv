/* Host-side tests for the addon protocol and api.strem.io parsers.
 * Run: ./test_addon   (fixtures path baked in by CMake) */
#include "addon/addon.h"
#include "addon/stremio_api.h"
#include "cJSON.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
#define CHECK(cond) do { if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)
static int STREQ(const char *a, const char *b) { return a && b && strcmp(a, b) == 0; }

static char *read_fixture(const char *name)
{
    char path[1024];
    snprintf(path, sizeof path, "%s/%s", SNTV_FIXTURES, name);
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "missing fixture %s\n", path); exit(2); }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc((size_t)n + 1);
    if (fread(buf, 1, (size_t)n, f) != (size_t)n) exit(2);
    buf[n] = 0;
    fclose(f);
    return buf;
}

static void test_manifest_and_urls(void)
{
    char *json = read_fixture("cinemeta_manifest.json");
    Addon a;
    CHECK(addon_new("https://v3-cinemeta.strem.io/manifest.json", json, &a) == 0);
    free(json);

    CHECK(STREQ(a.base_url, "https://v3-cinemeta.strem.io"));
    CHECK(STREQ(a.manifest.id, "com.linvo.cinemeta"));
    CHECK(a.manifest.n_resources == 3);
    CHECK(a.manifest.n_catalogs == 4);
    CHECK(a.manifest.types.n == 2 && strlist_contains(&a.manifest.types, "series"));

    /* resource matching: inherits manifest types/idPrefixes for plain strings */
    CHECK(addon_supports(&a, "meta", "movie", "tt0111161"));
    CHECK(!addon_supports(&a, "meta", "movie", "kitsu:123"));
    CHECK(!addon_supports(&a, "stream", "movie", "tt0111161"));
    CHECK(addon_supports(&a, "catalog", "movie", NULL));
    CHECK(!addon_supports(&a, "catalog", "tv", NULL));
    CHECK(addon_supports(&a, "addon_catalog", "all", NULL));
    CHECK(!addon_supports(&a, "addon_catalog", "movie", NULL));

    /* extras: modern and legacy forms */
    const AddonCatalog *top = addon_find_catalog(&a, "movie", "top");
    CHECK(top && top->n_extra == 2 && !addon_catalog_has_required_extra(top));
    const AddonExtra *genre = addon_catalog_extra(top, "genre");
    CHECK(genre && genre->options.n == 2 && STREQ(genre->options.v[1], "Drama") && genre->options_limit == 1);
    const AddonCatalog *year = addon_find_catalog(&a, "movie", "year");
    CHECK(year && addon_catalog_has_required_extra(year));
    const AddonCatalog *search = addon_find_catalog(&a, "movie", "search");
    CHECK(search && search->n_extra == 1 && STREQ(search->extra[0].name, "search") && search->extra[0].is_required);

    char buf[1024], extra[256];
    CHECK(STREQ(addon_url_catalog(&a, "movie", "top", NULL, buf, sizeof buf),
                "https://v3-cinemeta.strem.io/catalog/movie/top.json"));
    const char *kv[] = { "search", "breaking bad", "skip", "100", NULL };
    addon_extra_query(extra, sizeof extra, kv);
    CHECK(strcmp(extra, "search=breaking%20bad&skip=100") == 0);
    CHECK(STREQ(addon_url_catalog(&a, "series", "top", extra, buf, sizeof buf),
                "https://v3-cinemeta.strem.io/catalog/series/top/search=breaking%20bad&skip=100.json"));
    CHECK(STREQ(addon_url_meta(&a, "series", "tt0903747", buf, sizeof buf),
                "https://v3-cinemeta.strem.io/meta/series/tt0903747.json"));
    CHECK(STREQ(addon_url_stream(&a, "series", "tt0903747:1:2", buf, sizeof buf),
                "https://v3-cinemeta.strem.io/stream/series/tt0903747%3A1%3A2.json"));
    CHECK(STREQ(addon_url_manifest("https://x.example/sub/", buf, sizeof buf), "https://x.example/sub/manifest.json"));
    CHECK(STREQ(addon_url_manifest("https://x.example/manifest.json", buf, sizeof buf), "https://x.example/manifest.json"));
    addon_free(&a);

    json = read_fixture("stream_manifest.json");
    CHECK(addon_new("https://streams.example/manifest.json", json, &a) == 0);
    free(json);
    CHECK(addon_supports(&a, "stream", "movie", "tt1"));
    CHECK(addon_supports(&a, "stream", "series", "kitsu:99"));
    CHECK(!addon_supports(&a, "stream", "anime", "kitsu:99"));   /* resource overrides types */
    CHECK(!addon_supports(&a, "stream", "movie", "yt:abc"));
    CHECK(addon_supports(&a, "subtitles", "anime", "anything")); /* no prefixes anywhere */
    CHECK(a.manifest.bh_p2p && a.manifest.bh_configurable && !a.manifest.bh_configuration_required);
    addon_free(&a);
}

static void test_responses(void)
{
    char *json = read_fixture("catalog.json");
    MetaPreview *metas; int n;
    CHECK(addon_parse_metas(json, &metas, &n) == 0);
    free(json);
    CHECK(n == 2);  /* the entry without a name is dropped */
    CHECK(STREQ(metas[0].name, "The Shawshank Redemption") && STREQ(metas[0].imdb_rating, "9.3"));
    CHECK(STREQ(metas[1].imdb_rating, "9.5"));             /* number -> string */
    CHECK(metas[1].genres.n == 1 && STREQ(metas[1].genres.v[0], "Crime")); /* from links */
    meta_previews_free(metas, n);

    json = read_fixture("meta_series.json");
    MetaItem m;
    CHECK(addon_parse_meta(json, &m) == 0);
    free(json);
    CHECK(m.n_videos == 11 && m.videos[0].season == 1 && m.videos[0].episode == 1);
    CHECK(STREQ(m.videos[1].title, "Cat's in the Bag...") && m.videos[10].season == 0);
    CHECK(m.default_video_id == NULL && !m.is_live);
    CHECK(STREQ(m.p.runtime, "49 min"));
    meta_item_free(&m);

    json = read_fixture("streams.json");
    Stream *streams;
    CHECK(addon_parse_streams(json, &streams, &n) == 0);
    free(json);
    CHECK(n == 4);  /* "Useless" has no source */
    CHECK(STREQ(streams[0].url, "https://cdn.example.com/v/abc.mp4") && streams[0].not_web_ready);
    CHECK(streams[0].video_size == 2254857830ull && STREQ(streams[0].binge_group, "example-1080p"));
    CHECK(streams[1].file_idx == 2 && STREQ(streams[1].info_hash, "0123456789abcdef0123456789abcdef01234567"));
    CHECK(STREQ(streams[2].yt_id, "dQw4w9WgXcQ") && streams[2].file_idx == -1);
    CHECK(STREQ(streams[3].external_url, "https://www.netflix.com/title/70143836"));
    streams_free(streams, n);

    CHECK(addon_parse_streams("{\"nope\":1}", &streams, &n) == -1);
    CHECK(addon_parse_streams("not json", &streams, &n) == -1);
}

static void test_api(void)
{
    ApiError err;
    char *json = read_fixture("addon_collection.json");
    Addon *addons; int n;
    CHECK(api_parse_addon_collection(json, &addons, &n, &err) == 0);
    free(json);
    CHECK(n == 2);  /* descriptor without manifest.id is skipped */
    CHECK(addons[0].flag_official && addons[0].flag_protected && !addons[1].flag_official);
    CHECK(STREQ(addons[1].base_url, "https://torrentio.example"));
    CHECK(addon_supports(&addons[1], "stream", "movie", "tt1") && !addon_supports(&addons[0], "stream", "movie", "tt1"));
    for (int i = 0; i < n; i++) addon_free(&addons[i]);
    free(addons);

    json = read_fixture("library.json");
    LibraryItem *items;
    CHECK(api_parse_library(json, &items, &n, &err) == 0);
    free(json);
    CHECK(n == 2);
    CHECK(STREQ(items[0].video_id, "tt0903747:2:3") && items[0].time_offset == 1500000 && items[0].duration == 2940000);
    CHECK(items[0].ctime_ms == 1714557600000ll);           /* ISO string -> ms */
    CHECK(items[0].last_watched_ms == 1714681800000ll);
    CHECK(items[0].mtime_ms == 1714557600000ll);           /* numeric passthrough */
    CHECK(items[1].removed);

    /* round-trip through datastorePut body */
    char *body = api_body_datastore_put("KEY", STREMIO_LIBRARY_COLLECTION, items, 1);
    cJSON *o = cJSON_Parse(body);
    CHECK(o && STREQ(cJSON_GetObjectItem(o, "type")->valuestring, "DatastorePut"));
    cJSON *chg = cJSON_GetArrayItem(cJSON_GetObjectItem(o, "changes"), 0);
    CHECK(chg && STREQ(cJSON_GetObjectItem(chg, "_id")->valuestring, "tt0903747"));
    cJSON *st = cJSON_GetObjectItem(chg, "state");
    CHECK(st && cJSON_GetObjectItem(st, "timeOffset")->valuedouble == 1500000);
    CHECK(cJSON_GetObjectItem(st, "lastWatched")->valuedouble == 1714681800000.0);
    cJSON_Delete(o);
    free(body);
    library_items_free(items, n);

    json = read_fixture("api_error.json");
    char *key = NULL, *email = NULL;
    CHECK(api_parse_auth(json, &key, &email, &err) == 1);
    CHECK(err.code == 3 && STREQ(err.message, "Wrong email or password") && key == NULL);
    free(json);

    CHECK(api_parse_auth("{\"result\":{\"authKey\":\"abc\",\"user\":{\"email\":\"a@b.c\"}}}", &key, &email, &err) == 0);
    CHECK(STREQ(key, "abc") && STREQ(email, "a@b.c"));
    free(key); free(email);

    body = api_body_login("a@b.c", "p\"w");
    o = cJSON_Parse(body);
    CHECK(o && STREQ(cJSON_GetObjectItem(o, "password")->valuestring, "p\"w") && STREQ(cJSON_GetObjectItem(o, "type")->valuestring, "Login"));
    cJSON_Delete(o);
    free(body);

    CHECK(STREQ(api_url_addon_collection_get(), "https://api.strem.io/api/addonCollectionGet"));

    /* link (QR) login */
    char code[32], link[256], qr[256], rb[300];
    CHECK(api_parse_link_create("{\"result\":{\"code\":\"AB12CD\",\"link\":\"https://www.stremio.com/link/AB12CD\",\"qrcode\":\"https://q/x.png\"}}", code, sizeof code, link, sizeof link, qr, sizeof qr, &err) == 0);
    CHECK(STREQ(code, "AB12CD") && STREQ(link, "https://www.stremio.com/link/AB12CD"));
    CHECK(api_parse_link_create("{\"code\":\"ZZ\"}", code, sizeof code, link, sizeof link, qr, sizeof qr, &err) == 0 && STREQ(code, "ZZ"));
    CHECK(api_parse_link_create("{\"error\":{\"message\":\"nope\"}}", code, sizeof code, link, sizeof link, qr, sizeof qr, &err) == -1);
    CHECK(STREQ(api_url_link_read("AB 1", rb, sizeof rb), "https://link.stremio.com/api/v2/read?type=Read&code=AB%201"));
    char *k = NULL;
    CHECK(api_parse_link_read("{\"result\":{\"authKey\":\"KEY\"}}", &k, &err) == 0 && STREQ(k, "KEY"));
    free(k);
    CHECK(api_parse_link_read("{\"result\":{}}", &k, &err) == 1 && k == NULL);
    CHECK(api_parse_link_read("{\"error\":{\"code\":102,\"message\":\"not yet\"}}", &k, &err) == 1);
    CHECK(api_parse_link_read("{\"error\":{\"code\":5,\"message\":\"expired\"}}", &k, &err) == -1);
    char *em = NULL;
    CHECK(api_parse_user("{\"result\":{\"_id\":\"u\",\"email\":\"me@x.y\"}}", &em, &err) == 0 && STREQ(em, "me@x.y"));
    free(em);
}

int main(void)
{
    test_manifest_and_urls();
    test_responses();
    test_api();
    if (failures) { fprintf(stderr, "%d failure(s)\n", failures); return 1; }
    printf("all addon protocol tests passed\n");
    return 0;
}
