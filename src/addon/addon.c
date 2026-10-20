#include "addon.h"
#include "../http.h"
#include "../../vendor/cJSON.h"
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- small helpers ------------------------------------------------------- */

static char *jstr(const cJSON *o, const char *key)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, key);
    if (cJSON_IsString(v)) return strdup(v->valuestring);
    if (cJSON_IsNumber(v)) {    /* imdbRating / releaseInfo sometimes arrive as numbers */
        char tmp[32];
        if (v->valuedouble == (double)v->valueint) snprintf(tmp, sizeof tmp, "%d", v->valueint);
        else snprintf(tmp, sizeof tmp, "%.1f", v->valuedouble);
        return strdup(tmp);
    }
    return NULL;
}

static int jint(const cJSON *o, const char *key, int def)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, key);
    return cJSON_IsNumber(v) ? v->valueint : def;
}

static int jbool(const cJSON *o, const char *key)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, key);
    return cJSON_IsTrue(v) ? 1 : 0;
}

static void jstrlist(const cJSON *o, const char *key, StrList *out)
{
    memset(out, 0, sizeof *out);
    const cJSON *arr = cJSON_GetObjectItemCaseSensitive(o, key);
    if (!cJSON_IsArray(arr)) return;
    int n = cJSON_GetArraySize(arr);
    out->v = calloc((size_t)n + 1, sizeof(char *));
    const cJSON *it;
    cJSON_ArrayForEach(it, arr) {
        if (cJSON_IsString(it)) out->v[out->n++] = strdup(it->valuestring);
    }
}

void strlist_free(StrList *l)
{
    for (int i = 0; i < l->n; i++) free(l->v[i]);
    free(l->v);
    l->v = NULL;
    l->n = 0;
}

int strlist_contains(const StrList *l, const char *s)
{
    for (int i = 0; i < l->n; i++) if (strcmp(l->v[i], s) == 0) return 1;
    return 0;
}

static int has_prefix_in(const StrList *l, const char *id)
{
    for (int i = 0; i < l->n; i++)
        if (strncmp(id, l->v[i], strlen(l->v[i])) == 0) return 1;
    return 0;
}

/* ---- manifest ------------------------------------------------------------ */

static void parse_extra(const cJSON *arr, AddonCatalog *c, const cJSON *catalog_obj)
{
    /* Modern form: extra: [{name, isRequired, options, optionsLimit}]
     * Legacy form: extraSupported: ["search"], extraRequired: ["search"] */
    if (cJSON_IsArray(arr)) {
        int n = cJSON_GetArraySize(arr);
        c->extra = calloc((size_t)n + 1, sizeof(AddonExtra));
        const cJSON *it;
        cJSON_ArrayForEach(it, arr) {
            if (!cJSON_IsObject(it)) continue;
            AddonExtra *e = &c->extra[c->n_extra++];
            e->name = jstr(it, "name");
            e->is_required = jbool(it, "isRequired");
            e->options_limit = jint(it, "optionsLimit", 1);
            jstrlist(it, "options", &e->options);
        }
        return;
    }
    StrList sup, req;
    jstrlist(catalog_obj, "extraSupported", &sup);
    jstrlist(catalog_obj, "extraRequired", &req);
    if (sup.n) {
        c->extra = calloc((size_t)sup.n + 1, sizeof(AddonExtra));
        for (int i = 0; i < sup.n; i++) {
            AddonExtra *e = &c->extra[c->n_extra++];
            e->name = strdup(sup.v[i]);
            e->is_required = strlist_contains(&req, sup.v[i]);
            e->options_limit = 1;
        }
    }
    strlist_free(&sup);
    strlist_free(&req);
}

static int parse_manifest_obj(const cJSON *m, AddonManifest *out)
{
    memset(out, 0, sizeof *out);
    if (!cJSON_IsObject(m)) return -1;
    out->id = jstr(m, "id");
    out->version = jstr(m, "version");
    out->name = jstr(m, "name");
    out->description = jstr(m, "description");
    out->logo = jstr(m, "logo");
    out->background = jstr(m, "background");
    if (!out->id || !out->name) return -1;
    jstrlist(m, "types", &out->types);
    jstrlist(m, "idPrefixes", &out->id_prefixes);

    const cJSON *res = cJSON_GetObjectItemCaseSensitive(m, "resources");
    if (cJSON_IsArray(res)) {
        int n = cJSON_GetArraySize(res);
        out->resources = calloc((size_t)n + 1, sizeof(AddonResource));
        const cJSON *it;
        cJSON_ArrayForEach(it, res) {
            AddonResource *r = &out->resources[out->n_resources];
            if (cJSON_IsString(it)) {
                r->name = strdup(it->valuestring);
            } else if (cJSON_IsObject(it)) {
                r->name = jstr(it, "name");
                jstrlist(it, "types", &r->types);
                jstrlist(it, "idPrefixes", &r->id_prefixes);
                r->has_own_types = cJSON_IsArray(cJSON_GetObjectItemCaseSensitive(it, "types"));
            }
            if (r->name) out->n_resources++;
        }
    }

    const cJSON *cats = cJSON_GetObjectItemCaseSensitive(m, "catalogs");
    if (cJSON_IsArray(cats)) {
        int n = cJSON_GetArraySize(cats);
        out->catalogs = calloc((size_t)n + 1, sizeof(AddonCatalog));
        const cJSON *it;
        cJSON_ArrayForEach(it, cats) {
            if (!cJSON_IsObject(it)) continue;
            AddonCatalog *c = &out->catalogs[out->n_catalogs];
            c->type = jstr(it, "type");
            c->id = jstr(it, "id");
            c->name = jstr(it, "name");
            if (!c->type || !c->id) { free(c->type); free(c->id); free(c->name); memset(c, 0, sizeof *c); continue; }
            if (!c->name) c->name = strdup(c->id);
            parse_extra(cJSON_GetObjectItemCaseSensitive(it, "extra"), c, it);
            out->n_catalogs++;
        }
    }

    const cJSON *bh = cJSON_GetObjectItemCaseSensitive(m, "behaviorHints");
    if (cJSON_IsObject(bh)) {
        out->bh_adult = jbool(bh, "adult");
        out->bh_p2p = jbool(bh, "p2p");
        out->bh_configurable = jbool(bh, "configurable");
        out->bh_configuration_required = jbool(bh, "configurationRequired");
    }
    return 0;
}

int addon_parse_manifest(const char *json, AddonManifest *out)
{
    cJSON *root = cJSON_Parse(json);
    int rc = parse_manifest_obj(root, out);
    cJSON_Delete(root);
    if (rc != 0) addon_manifest_free(out);
    return rc;
}

void addon_manifest_free(AddonManifest *m)
{
    free(m->id); free(m->version); free(m->name); free(m->description);
    free(m->logo); free(m->background);
    strlist_free(&m->types);
    strlist_free(&m->id_prefixes);
    for (int i = 0; i < m->n_resources; i++) {
        free(m->resources[i].name);
        strlist_free(&m->resources[i].types);
        strlist_free(&m->resources[i].id_prefixes);
    }
    free(m->resources);
    for (int i = 0; i < m->n_catalogs; i++) {
        AddonCatalog *c = &m->catalogs[i];
        free(c->type); free(c->id); free(c->name);
        for (int j = 0; j < c->n_extra; j++) {
            free(c->extra[j].name);
            strlist_free(&c->extra[j].options);
        }
        free(c->extra);
    }
    free(m->catalogs);
    memset(m, 0, sizeof *m);
}

static char *base_from_transport(const char *t)
{
    size_t n = strlen(t);
    const char *suffix = "/manifest.json";
    size_t sl = strlen(suffix);
    char *b;
    if (n >= sl && strcmp(t + n - sl, suffix) == 0) {
        b = malloc(n - sl + 1);
        memcpy(b, t, n - sl);
        b[n - sl] = 0;
    } else {
        b = strdup(t);
        n = strlen(b);
        while (n && b[n - 1] == '/') b[--n] = 0;
    }
    return b;
}

int addon_new(const char *transport_url, const char *manifest_json, Addon *out)
{
    memset(out, 0, sizeof *out);
    if (addon_parse_manifest(manifest_json, &out->manifest) != 0) return -1;
    out->manifest_json = strdup(manifest_json);
    out->transport_url = strdup(transport_url);
    out->base_url = base_from_transport(transport_url);
    return 0;
}

int addon_from_descriptor_json(const cJSON *d, Addon *out)
{
    memset(out, 0, sizeof *out);
    const cJSON *t = cJSON_GetObjectItemCaseSensitive(d, "transportUrl");
    const cJSON *m = cJSON_GetObjectItemCaseSensitive(d, "manifest");
    if (!cJSON_IsString(t) || !cJSON_IsObject(m)) return -1;
    if (parse_manifest_obj(m, &out->manifest) != 0) { addon_manifest_free(&out->manifest); return -1; }
    out->transport_url = strdup(t->valuestring);
    out->base_url = base_from_transport(t->valuestring);
    if (addon_manifest_complete(m)) out->manifest_json = cJSON_PrintUnformatted(m);
    const cJSON *flags = cJSON_GetObjectItemCaseSensitive(d, "flags");
    if (cJSON_IsObject(flags)) {
        out->flag_official = jbool(flags, "official");
        out->flag_protected = jbool(flags, "protected");
    }
    return 0;
}

int addon_manifest_complete(const cJSON *m)
{
    return cJSON_IsObject(m) && cJSON_IsString(cJSON_GetObjectItemCaseSensitive(m, "id")) && cJSON_IsString(cJSON_GetObjectItemCaseSensitive(m, "name"))
        && cJSON_IsString(cJSON_GetObjectItemCaseSensitive(m, "version"))
        && cJSON_IsArray(cJSON_GetObjectItemCaseSensitive(m, "resources")) && cJSON_IsArray(cJSON_GetObjectItemCaseSensitive(m, "types"));
}

void addon_json_put(cJSON *obj, const char *key, cJSON *value)
{
    if (cJSON_GetObjectItemCaseSensitive(obj, key)) cJSON_ReplaceItemInObjectCaseSensitive(obj, key, value);
    else cJSON_AddItemToObject(obj, key, value);
}

void addon_manifest_fill(cJSON *m)
{
    if (!cJSON_IsObject(m)) return;
    if (!cJSON_IsString(cJSON_GetObjectItemCaseSensitive(m, "description"))) addon_json_put(m, "description", cJSON_CreateString(""));
    if (!cJSON_IsArray(cJSON_GetObjectItemCaseSensitive(m, "catalogs"))) addon_json_put(m, "catalogs", cJSON_CreateArray());
}

void addon_free(Addon *a)
{
    free(a->manifest_json);
    addon_manifest_free(&a->manifest);
    free(a->transport_url);
    free(a->base_url);
    memset(a, 0, sizeof *a);
}

/* ---- capability checks --------------------------------------------------- */

int addon_supports(const Addon *a, const char *resource, const char *type, const char *id)
{
    if (a->transport_url && strstr(a->transport_url, "/stremio/v1")) return 0;   /* the old protocol (Stremio's first OpenSubtitles): kept for the account, not asked */
    const AddonManifest *m = &a->manifest;
    for (int i = 0; i < m->n_resources; i++) {
        const AddonResource *r = &m->resources[i];
        if (strcmp(r->name, resource) != 0) continue;
        const StrList *types = r->has_own_types ? &r->types : &m->types;
        if (type && types->n && !strlist_contains(types, type)) continue;
        /* idPrefixes are ignored for catalogs; absent prefixes match every id. */
        if (id && strcmp(resource, "catalog") != 0) {
            const StrList *pre = r->id_prefixes.n ? &r->id_prefixes : &m->id_prefixes;
            if (pre->n && !has_prefix_in(pre, id)) continue;
        }
        return 1;
    }
    return 0;
}

const AddonCatalog *addon_find_catalog(const Addon *a, const char *type, const char *id)
{
    for (int i = 0; i < a->manifest.n_catalogs; i++) {
        const AddonCatalog *c = &a->manifest.catalogs[i];
        if (strcmp(c->type, type) == 0 && strcmp(c->id, id) == 0) return c;
    }
    return NULL;
}

const AddonExtra *addon_catalog_extra(const AddonCatalog *c, const char *name)
{
    for (int i = 0; i < c->n_extra; i++)
        if (c->extra[i].name && strcmp(c->extra[i].name, name) == 0) return &c->extra[i];
    return NULL;
}

int addon_catalog_has_required_extra(const AddonCatalog *c)
{
    for (int i = 0; i < c->n_extra; i++) if (c->extra[i].is_required) return 1;
    return 0;
}

/* ---- URL builders -------------------------------------------------------- */

char *addon_url_manifest(const char *t, char *buf, size_t n)
{
    char *b = base_from_transport(t);
    snprintf(buf, n, "%s/manifest.json", b);
    free(b);
    return buf;
}

static char *resource_url(const Addon *a, const char *res, const char *type, const char *id,
                          const char *extra, char *buf, size_t n)
{
    char eid[1024];
    http_url_encode(id, eid, sizeof eid);
    if (extra && *extra) snprintf(buf, n, "%s/%s/%s/%s/%s.json", a->base_url, res, type, eid, extra);
    else snprintf(buf, n, "%s/%s/%s/%s.json", a->base_url, res, type, eid);
    return buf;
}

char *addon_url_catalog(const Addon *a, const char *type, const char *id, const char *extra, char *buf, size_t n)
{ return resource_url(a, "catalog", type, id, extra, buf, n); }

char *addon_url_meta(const Addon *a, const char *type, const char *id, char *buf, size_t n)
{ return resource_url(a, "meta", type, id, NULL, buf, n); }

char *addon_url_stream(const Addon *a, const char *type, const char *video_id, char *buf, size_t n)
{ return resource_url(a, "stream", type, video_id, NULL, buf, n); }

char *addon_url_subtitles(const Addon *a, const char *type, const char *video_id, const char *extra, char *buf, size_t n)
{ return resource_url(a, "subtitles", type, video_id, extra, buf, n); }

char *addon_extra_query(char *buf, size_t n, const char *const *kv)
{
    size_t o = 0;
    buf[0] = 0;
    for (int i = 0; kv[i] && kv[i + 1]; i += 2) {
        char enc[1024];
        http_url_encode(kv[i + 1], enc, sizeof enc);
        int w = snprintf(buf + o, n - o, "%s%s=%s", o ? "&" : "", kv[i], enc);
        if (w < 0 || (size_t)w >= n - o) break;
        o += (size_t)w;
    }
    return buf;
}

/* ---- responses ----------------------------------------------------------- */

static void parse_preview(const cJSON *o, MetaPreview *p)
{
    memset(p, 0, sizeof *p);
    p->id = jstr(o, "id");
    p->type = jstr(o, "type");
    p->name = jstr(o, "name");
    p->poster = jstr(o, "poster");
    p->poster_shape = jstr(o, "posterShape");
    p->background = jstr(o, "background");
    p->logo = jstr(o, "logo");
    p->description = jstr(o, "description");
    p->release_info = jstr(o, "releaseInfo");
    if (!p->release_info) p->release_info = jstr(o, "year");
    p->imdb_rating = jstr(o, "imdbRating");
    p->runtime = jstr(o, "runtime");
    jstrlist(o, "genres", &p->genres);
    jstrlist(o, "cast", &p->cast);
    if (!p->cast.n) {   /* newer Cinemeta: credits_cast [{name, character, photo}] */
        const cJSON *cc = cJSON_GetObjectItemCaseSensitive(o, "credits_cast");
        if (cJSON_IsArray(cc)) {
            int n = cJSON_GetArraySize(cc);
            p->cast.v = calloc((size_t)n + 1, sizeof(char *));
            for (int i = 0; i < n && p->cast.n < 12; i++) {
                const cJSON *nm = cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(cc, i), "name");
                if (cJSON_IsString(nm) && nm->valuestring[0]) p->cast.v[p->cast.n++] = strdup(nm->valuestring);
            }
        }
    }
    {   /* app_extras.cast [{name, character, photo}]: the official apps' cast with pictures */
        const cJSON *ax = cJSON_GetObjectItemCaseSensitive(o, "app_extras");
        const cJSON *ac = ax ? cJSON_GetObjectItemCaseSensitive(ax, "cast") : NULL;
        if (cJSON_IsArray(ac) && cJSON_GetArraySize(ac) > 0) {
            int n = cJSON_GetArraySize(ac);
            strlist_free(&p->cast);
            p->cast.v = calloc((size_t)n + 1, sizeof(char *));
            p->cast_photos.v = calloc((size_t)n + 1, sizeof(char *));
            p->cast_roles.v = calloc((size_t)n + 1, sizeof(char *));
            for (int i = 0; i < n && p->cast.n < 12; i++) {
                const cJSON *it = cJSON_GetArrayItem(ac, i);
                const cJSON *nm = cJSON_GetObjectItemCaseSensitive(it, "name"), *ph = cJSON_GetObjectItemCaseSensitive(it, "photo"), *ch = cJSON_GetObjectItemCaseSensitive(it, "character");
                if (!cJSON_IsString(nm) || !nm->valuestring[0]) continue;
                p->cast.v[p->cast.n++] = strdup(nm->valuestring);
                p->cast_photos.v[p->cast_photos.n++] = strdup(cJSON_IsString(ph) ? ph->valuestring : "");
                p->cast_roles.v[p->cast_roles.n++] = strdup(cJSON_IsString(ch) ? ch->valuestring : "");
            }
        }
    }
    if (!p->genres.n) {
        /* Newer addons move genres into links[category=Genres]. */
        const cJSON *links = cJSON_GetObjectItemCaseSensitive(o, "links");
        if (cJSON_IsArray(links)) {
            int n = cJSON_GetArraySize(links);
            p->genres.v = calloc((size_t)n + 1, sizeof(char *));
            const cJSON *it;
            cJSON_ArrayForEach(it, links) {
                const cJSON *cat = cJSON_GetObjectItemCaseSensitive(it, "category");
                const cJSON *nm = cJSON_GetObjectItemCaseSensitive(it, "name");
                if (cJSON_IsString(cat) && cJSON_IsString(nm) && strcmp(cat->valuestring, "Genres") == 0)
                    p->genres.v[p->genres.n++] = strdup(nm->valuestring);
            }
        }
    }
}

void meta_preview_free(MetaPreview *p)
{
    free(p->id); free(p->type); free(p->name); free(p->poster); free(p->poster_shape);
    free(p->background); free(p->logo); free(p->description); free(p->release_info);
    free(p->imdb_rating); free(p->runtime);
    strlist_free(&p->genres);
    strlist_free(&p->cast);
    strlist_free(&p->cast_photos);
    strlist_free(&p->cast_roles);
    memset(p, 0, sizeof *p);
}

void meta_previews_free(MetaPreview *m, int n)
{
    for (int i = 0; i < n; i++) meta_preview_free(&m[i]);
    free(m);
}

int addon_parse_metas(const char *json, MetaPreview **out, int *n)
{
    *out = NULL; *n = 0;
    cJSON *root = cJSON_Parse(json);
    const cJSON *metas = cJSON_GetObjectItemCaseSensitive(root, "metas");
    if (!cJSON_IsArray(metas)) { cJSON_Delete(root); return -1; }
    int cap = cJSON_GetArraySize(metas);
    MetaPreview *arr = calloc((size_t)cap + 1, sizeof *arr);
    const cJSON *it;
    cJSON_ArrayForEach(it, metas) {
        if (!cJSON_IsObject(it)) continue;
        MetaPreview *p = &arr[*n];
        parse_preview(it, p);
        if (p->id && p->name && p->type) (*n)++; else meta_preview_free(p);
    }
    cJSON_Delete(root);
    *out = arr;
    return 0;
}

int addon_parse_meta(const char *json, MetaItem *out)
{
    memset(out, 0, sizeof *out);
    cJSON *root = cJSON_Parse(json);
    const cJSON *meta = cJSON_GetObjectItemCaseSensitive(root, "meta");
    if (!cJSON_IsObject(meta)) { cJSON_Delete(root); return -1; }
    parse_preview(meta, &out->p);
    if (!out->p.id || !out->p.name) { cJSON_Delete(root); meta_item_free(out); return -1; }

    const cJSON *videos = cJSON_GetObjectItemCaseSensitive(meta, "videos");
    if (cJSON_IsArray(videos)) {
        int cap = cJSON_GetArraySize(videos);
        out->videos = calloc((size_t)cap + 1, sizeof(MetaVideo));
        const cJSON *it;
        cJSON_ArrayForEach(it, videos) {
            if (!cJSON_IsObject(it)) continue;
            MetaVideo *v = &out->videos[out->n_videos];
            v->id = jstr(it, "id");
            v->title = jstr(it, "title");
            if (!v->title) v->title = jstr(it, "name");
            v->released = jstr(it, "released");
            v->thumbnail = jstr(it, "thumbnail");
            v->overview = jstr(it, "overview");
            if (!v->overview) v->overview = jstr(it, "description");
            v->season = jint(it, "season", -1);
            v->episode = jint(it, "episode", -1);
            if (v->episode < 0) v->episode = jint(it, "number", -1);
            if (v->id) out->n_videos++;
            else { free(v->title); free(v->released); free(v->thumbnail); free(v->overview); memset(v, 0, sizeof *v); }
        }
    }
    const cJSON *bh = cJSON_GetObjectItemCaseSensitive(meta, "behaviorHints");
    if (cJSON_IsObject(bh)) {
        out->default_video_id = jstr(bh, "defaultVideoId");
        out->is_live = jbool(bh, "isLive");
    }
    if (out->p.type && strcmp(out->p.type, "tv") == 0) out->is_live = 1;

    /* credits: "director"/"writer" as lists or one string, else links[category] */
    static const struct { const char *key, *cat; size_t off; } CREDITS[] = {
        { "director", "Directors", offsetof(MetaItem, directors) }, { "writer", "Writers", offsetof(MetaItem, writers) },
    };
    for (size_t c = 0; c < sizeof CREDITS / sizeof CREDITS[0]; c++) {
        StrList *l = (StrList *)((char *)out + CREDITS[c].off);
        const cJSON *v = cJSON_GetObjectItemCaseSensitive(meta, CREDITS[c].key);
        if (cJSON_IsArray(v)) jstrlist(meta, CREDITS[c].key, l);
        else if (cJSON_IsString(v) && v->valuestring[0]) { l->v = calloc(2, sizeof(char *)); l->v[l->n++] = strdup(v->valuestring); }
        const cJSON *links = cJSON_GetObjectItemCaseSensitive(meta, "links");
        if (l->n || !cJSON_IsArray(links)) continue;
        strlist_free(l);
        l->v = calloc((size_t)cJSON_GetArraySize(links) + 1, sizeof(char *));
        const cJSON *it;
        cJSON_ArrayForEach(it, links) {
            const cJSON *cat = cJSON_GetObjectItemCaseSensitive(it, "category"), *nm = cJSON_GetObjectItemCaseSensitive(it, "name");
            if (cJSON_IsString(cat) && cJSON_IsString(nm) && nm->valuestring[0] && strcmp(cat->valuestring, CREDITS[c].cat) == 0)
                l->v[l->n++] = strdup(nm->valuestring);
        }
    }
    out->country = jstr(meta, "country");
    out->awards = jstr(meta, "awards");
    out->released = jstr(meta, "released");
    const cJSON *ts = cJSON_GetObjectItemCaseSensitive(meta, "trailerStreams"), *it;
    cJSON_ArrayForEach(it, ts) if (!out->trailer_yt) out->trailer_yt = jstr(it, "ytId");
    ts = cJSON_GetObjectItemCaseSensitive(meta, "trailers");
    cJSON_ArrayForEach(it, ts) if (!out->trailer_yt) out->trailer_yt = jstr(it, "source");
    if (out->trailer_yt && !out->trailer_yt[0]) { free(out->trailer_yt); out->trailer_yt = NULL; }
    cJSON_Delete(root);
    return 0;
}

void meta_item_free(MetaItem *m)
{
    meta_preview_free(&m->p);
    for (int i = 0; i < m->n_videos; i++) {
        MetaVideo *v = &m->videos[i];
        free(v->id); free(v->title); free(v->released); free(v->thumbnail); free(v->overview);
    }
    free(m->videos);
    free(m->default_video_id);
    strlist_free(&m->directors); strlist_free(&m->writers);
    free(m->country); free(m->awards); free(m->released); free(m->trailer_yt);
    memset(m, 0, sizeof *m);
}

int addon_parse_streams(const char *json, Stream **out, int *n)
{
    *out = NULL; *n = 0;
    cJSON *root = cJSON_Parse(json);
    const cJSON *streams = cJSON_GetObjectItemCaseSensitive(root, "streams");
    if (!cJSON_IsArray(streams)) { cJSON_Delete(root); return -1; }
    int cap = cJSON_GetArraySize(streams);
    Stream *arr = calloc((size_t)cap + 1, sizeof *arr);
    const cJSON *it;
    cJSON_ArrayForEach(it, streams) {
        if (!cJSON_IsObject(it)) continue;
        Stream *s = &arr[*n];
        s->url = jstr(it, "url");
        s->yt_id = jstr(it, "ytId");
        s->info_hash = jstr(it, "infoHash");
        s->external_url = jstr(it, "externalUrl");
        s->name = jstr(it, "name");
        s->title = jstr(it, "title");
        s->description = jstr(it, "description");
        s->file_idx = jint(it, "fileIdx", -1);
        const cJSON *src, *sources = cJSON_GetObjectItemCaseSensitive(it, "sources");
        char tr[2048] = "";
        size_t used = 0;
        cJSON_ArrayForEach(src, sources) {   /* "tracker:udp://...": the torrent's own trackers */
            if (!cJSON_IsString(src) || strncmp(src->valuestring, "tracker:", 8) != 0) continue;
            char enc[600];
            http_url_encode(src->valuestring + 8, enc, sizeof enc);
            if (used + strlen(enc) + 5 >= sizeof tr) break;
            used += (size_t)snprintf(tr + used, sizeof tr - used, "%str=%s", used ? "&" : "", enc);
        }
        if (used) s->trackers = strdup(tr);
        const cJSON *bh = cJSON_GetObjectItemCaseSensitive(it, "behaviorHints");
        if (cJSON_IsObject(bh)) {
            s->not_web_ready = jbool(bh, "notWebReady");
            s->binge_group = jstr(bh, "bingeGroup");
            s->filename = jstr(bh, "filename");
            const cJSON *vs = cJSON_GetObjectItemCaseSensitive(bh, "videoSize");
            if (cJSON_IsNumber(vs)) s->video_size = (uint64_t)vs->valuedouble;
        }
        if (s->url || s->yt_id || s->info_hash || s->external_url) (*n)++;
        else { free(s->url); free(s->yt_id); free(s->info_hash); free(s->external_url); free(s->name); free(s->title); free(s->description); free(s->binge_group); free(s->filename); free(s->trackers); memset(s, 0, sizeof *s); }
    }
    cJSON_Delete(root);
    *out = arr;
    return 0;
}

void streams_free(Stream *s, int n)
{
    for (int i = 0; i < n; i++) {
        free(s[i].url); free(s[i].yt_id); free(s[i].info_hash); free(s[i].external_url);
        free(s[i].name); free(s[i].title); free(s[i].description);
        free(s[i].binge_group); free(s[i].filename); free(s[i].addon_name);
        free(s[i].trackers); free(s[i].probe);
    }
    if (n) free(s);
}

int addon_parse_subtitles(const char *json, Subtitle **out, int *n)
{
    *out = NULL; *n = 0;
    cJSON *root = cJSON_Parse(json);
    const cJSON *subs = cJSON_GetObjectItemCaseSensitive(root, "subtitles");
    if (!cJSON_IsArray(subs)) { cJSON_Delete(root); return -1; }
    int cap = cJSON_GetArraySize(subs);
    Subtitle *arr = calloc((size_t)cap + 1, sizeof *arr);
    const cJSON *it;
    cJSON_ArrayForEach(it, subs) {
        if (!cJSON_IsObject(it)) continue;
        Subtitle *s = &arr[*n];
        s->id = jstr(it, "id");
        s->url = jstr(it, "url");
        s->lang = jstr(it, "lang");
        if (s->url) (*n)++; else { free(s->id); free(s->lang); memset(s, 0, sizeof *s); }
    }
    cJSON_Delete(root);
    *out = arr;
    return 0;
}

void subtitles_free(Subtitle *s, int n)
{
    for (int i = 0; i < n; i++) { free(s[i].id); free(s[i].url); free(s[i].lang); }
    free(s);
}
