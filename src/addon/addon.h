/* Stremio addon protocol (https://github.com/Stremio/stremio-addon-sdk/blob/master/docs/protocol.md)
 *
 *   {transport}/manifest.json
 *   {transport}/catalog/{type}/{id}.json
 *   {transport}/catalog/{type}/{id}/{extra}.json      extra = "search=x&skip=100"
 *   {transport}/meta/{type}/{id}.json
 *   {transport}/stream/{type}/{videoId}.json
 *   {transport}/subtitles/{type}/{videoId}/{extra}.json
 */
#ifndef SNTV_ADDON_H
#define SNTV_ADDON_H
#include <stddef.h>
#include <stdint.h>

struct cJSON;

typedef struct StrList { char **v; int n; } StrList;

typedef struct AddonExtra {
    char *name;
    int is_required;
    StrList options;
    int options_limit;
} AddonExtra;

typedef struct AddonCatalog {
    char *type, *id, *name;
    AddonExtra *extra;
    int n_extra;
} AddonCatalog;

typedef struct AddonResource {
    char *name;            /* catalog, meta, stream, subtitles, addon_catalog */
    StrList types;         /* empty => inherit manifest.types */
    StrList id_prefixes;   /* empty => inherit manifest.idPrefixes */
    int has_own_types;
} AddonResource;

typedef struct AddonManifest {
    char *id, *version, *name, *description, *logo, *background;
    StrList types, id_prefixes;
    AddonResource *resources; int n_resources;
    AddonCatalog *catalogs;   int n_catalogs;
    int bh_adult, bh_p2p, bh_configurable, bh_configuration_required;
} AddonManifest;

typedef struct Addon {
    char *transport_url;   /* ".../manifest.json" as installed */
    char *base_url;        /* transport_url without "/manifest.json" */
    AddonManifest manifest;
    int flag_official, flag_protected;
} Addon;

typedef struct MetaPreview {
    char *id, *type, *name, *poster, *poster_shape, *background, *logo;
    char *description, *release_info, *imdb_rating, *runtime;
    StrList genres;
} MetaPreview;

typedef struct MetaVideo {
    char *id, *title, *released, *thumbnail, *overview;
    int season, episode;        /* -1 when absent */
} MetaVideo;

typedef struct MetaItem {
    MetaPreview p;
    MetaVideo *videos; int n_videos;
    char *default_video_id;
    int is_live;
} MetaItem;

#include "../quality.h"

typedef struct Stream {
    char *url, *yt_id, *info_hash, *external_url;
    char *name, *title, *description;
    int file_idx;               /* -1 when absent */
    int not_web_ready;
    char *binge_group, *filename;
    uint64_t video_size;
    char *addon_name;           /* filled by the aggregator, not the parser */
    int addon_order;            /* position of that addon in the user's list */
    StreamTags tags;            /* parsed from name/title (quality.h) */
    long score;                 /* quality_score for the current prefs */
} Stream;

typedef struct Subtitle { char *id, *url, *lang; } Subtitle;

/* ---- parsing (0 on success) ---- */
int  addon_parse_manifest(const char *json, AddonManifest *out);
int  addon_from_descriptor_json(const struct cJSON *descriptor, Addon *out); /* {transportUrl, manifest, flags} */
int  addon_new(const char *transport_url, const char *manifest_json, Addon *out);
void addon_free(Addon *a);
void addon_manifest_free(AddonManifest *m);

int  addon_parse_metas(const char *json, MetaPreview **out, int *n);
int  addon_parse_meta(const char *json, MetaItem *out);
int  addon_parse_streams(const char *json, Stream **out, int *n);
int  addon_parse_subtitles(const char *json, Subtitle **out, int *n);
void meta_preview_free(MetaPreview *m);
void meta_previews_free(MetaPreview *m, int n);
void meta_item_free(MetaItem *m);
void streams_free(Stream *s, int n);
void subtitles_free(Subtitle *s, int n);

/* ---- capability checks ---- */
int addon_supports(const Addon *a, const char *resource, const char *type, const char *id);
const AddonCatalog *addon_find_catalog(const Addon *a, const char *type, const char *id);
const AddonExtra *addon_catalog_extra(const AddonCatalog *c, const char *name);
int addon_catalog_has_required_extra(const AddonCatalog *c);

/* ---- URL builders (return buf) ---- */
char *addon_url_manifest(const char *transport_or_base, char *buf, size_t n);
char *addon_url_catalog(const Addon *a, const char *type, const char *id, const char *extra_query, char *buf, size_t n);
char *addon_url_meta(const Addon *a, const char *type, const char *id, char *buf, size_t n);
char *addon_url_stream(const Addon *a, const char *type, const char *video_id, char *buf, size_t n);
char *addon_url_subtitles(const Addon *a, const char *type, const char *video_id, const char *extra_query, char *buf, size_t n);

/* Builds "k1=v1&k2=v2" with percent-encoded values. kv is NULL-terminated pairs. */
char *addon_extra_query(char *buf, size_t n, const char *const *kv);

/* ---- helpers ---- */
void strlist_free(StrList *l);
int  strlist_contains(const StrList *l, const char *s);

#endif
