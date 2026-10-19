#include "font.h"
#include "../log.h"
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <utime.h>

static const int SIZES[] = { FONT_XS, FONT_S, FONT_M, FONT_L, FONT_XL, FONT_TITLE };
static const int SIZES_MONO[] = { FONT_XS, FONT_S, FONT_M };
static const int SIZES_DISPLAY[] = { FONT_L, FONT_XL, FONT_TITLE };
#define NSIZES ((int)(sizeof SIZES / sizeof SIZES[0]))

/* Latin-1 + Latin Extended-A/B cover most European titles; general punctuation
 * gives proper dashes, quotes, bullets and ellipsis. Missing glyphs are skipped. */
static const int RANGES[][2] = { { 32, 126 }, { 160, 591 }, { 0x2010, 0x2027 }, { 0x20AC, 0x20AC }, { 0x2190, 0x2193 } };
#define NRANGES ((int)(sizeof RANGES / sizeof RANGES[0]))

typedef struct {
    int size, nr;
    stbtt_packedchar *chars[NRANGES];
    float ascent, line_h;
} Face;

typedef struct {
    unsigned tex;
    int aw, ah;          /* atlas size */
    int nfaces;
    Face faces[NSIZES];
} FontFile;

enum { F_REGULAR, F_BOLD, F_ALT, F_COUNT };
static FontFile g_files[F_COUNT];
static int g_have[F_COUNT];
static char g_cache_dir[1024];
static char g_digits_ttf[1024];   /* the bold face, for font_digits_load */

static unsigned char *read_all(const char *path, long *len)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char *b = n > 0 ? malloc((size_t)n) : NULL;
    if (b && fread(b, 1, (size_t)n, f) != (size_t)n) { free(b); b = NULL; }
    fclose(f);
    if (len) *len = n;
    return b;
}

/* Glyph count of range r in a face that keeps `nr` ranges (the reduced set stops
 * Latin Extended at U+017F). */
static int range_count(int r, int nr) { return (r == 1 && nr == 3 ? 383 : RANGES[r][1]) - RANGES[r][0] + 1; }

static void free_file(FontFile *ff)
{
    for (int i = 0; i < NSIZES; i++)
        for (int r = 0; r < NRANGES; r++) { free(ff->faces[i].chars[r]); ff->faces[i].chars[r] = NULL; }
    if (ff->tex) gfx_texture_destroy(ff->tex);
    memset(ff, 0, sizeof *ff);
}

/* ---- baked atlas cache -----------------------------------------------------------
 * Baking two 2048² atlases is the single biggest cost before the first frame (about
 * half a second on a TV), so the result is kept on disk: header, face metrics,
 * packed glyph tables, then the alpha bitmap with runs of zeros collapsed. */

#define CACHE_MAGIC 0x53465431u   /* "SFT1" */

static void cache_path(const char *ttf, int role, char *out, size_t n)
{
    struct stat st;
    long long size = 0, mtime = 0;
    if (stat(ttf, &st) == 0) { size = (long long)st.st_size; mtime = (long long)st.st_mtime; }
    unsigned long long h = 1469598103934665603ull;
    for (const char *p = ttf; *p; p++) h = (h ^ (unsigned char)*p) * 1099511628211ull;
    h = (h ^ (unsigned long long)size) * 1099511628211ull;
    h = (h ^ (unsigned long long)mtime) * 1099511628211ull;
    h = (h ^ (unsigned long long)(role + 1)) * 1099511628211ull;
    snprintf(out, n, "%s/fontcache/%016llx.bin", g_cache_dir, h);
}

static int cache_load(FontFile *ff, const char *path)
{
    long len = 0;
    unsigned char *b = read_all(path, &len), *p = b, *end = b + len;
    if (!b) return 0;
#define TAKE(dst, n) do { if (p + (n) > end) goto bad; memcpy((dst), p, (n)); p += (n); } while (0)
    uint32_t magic, hdr[3];
    TAKE(&magic, 4);
    if (magic != CACHE_MAGIC) goto bad;
    TAKE(hdr, sizeof hdr);
    ff->aw = (int)hdr[0]; ff->ah = (int)hdr[1]; ff->nfaces = (int)hdr[2];
    if (ff->aw <= 0 || ff->ah <= 0 || ff->aw > 4096 || ff->ah > 4096 || ff->nfaces <= 0 || ff->nfaces > NSIZES) goto bad;
    for (int i = 0; i < ff->nfaces; i++) {
        Face *fc = &ff->faces[i];
        int32_t m[2]; float f[2];
        TAKE(m, sizeof m); TAKE(f, sizeof f);
        fc->size = m[0]; fc->nr = m[1]; fc->ascent = f[0]; fc->line_h = f[1];
        if (fc->nr <= 0 || fc->nr > NRANGES) goto bad;
        for (int r = 0; r < fc->nr; r++) {
            size_t bytes = sizeof(stbtt_packedchar) * (size_t)range_count(r, fc->nr);
            fc->chars[r] = malloc(bytes);
            TAKE(fc->chars[r], bytes);
        }
    }
    unsigned char *bitmap = malloc((size_t)ff->aw * (size_t)ff->ah);
    size_t o = 0, total = (size_t)ff->aw * (size_t)ff->ah;
    while (o < total && p < end) {
        unsigned char v = *p++;
        if (v) { bitmap[o++] = v; continue; }
        if (p >= end) break;
        size_t run = (size_t)*p++ + 1;
        if (o + run > total) run = total - o;
        memset(bitmap + o, 0, run);
        o += run;
    }
    if (o != total) { free(bitmap); goto bad; }
    ff->tex = gfx_texture_alpha(bitmap, ff->aw, ff->ah);
    free(bitmap);
    free(b);
    return 1;
bad:
#undef TAKE
    free(b);
    for (int i = 0; i < NSIZES; i++) for (int r = 0; r < NRANGES; r++) { free(ff->faces[i].chars[r]); ff->faces[i].chars[r] = NULL; }
    LOGW("font: cache %s unreadable, baking again", path);
    return 0;
}

static void cache_save(const FontFile *ff, const unsigned char *bitmap, const char *path)
{
    if (!g_cache_dir[0]) return;
    char dir[1100];
    snprintf(dir, sizeof dir, "%s/fontcache", g_cache_dir);
    mkdir(dir, 0755);
    char tmp[1200];
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    FILE *f = fopen(tmp, "wb");
    if (!f) return;
    uint32_t magic = CACHE_MAGIC, hdr[3] = { (uint32_t)ff->aw, (uint32_t)ff->ah, (uint32_t)ff->nfaces };
    fwrite(&magic, 4, 1, f);
    fwrite(hdr, sizeof hdr, 1, f);
    for (int i = 0; i < ff->nfaces; i++) {
        const Face *fc = &ff->faces[i];
        int32_t m[2] = { fc->size, fc->nr };
        float fl[2] = { fc->ascent, fc->line_h };
        fwrite(m, sizeof m, 1, f);
        fwrite(fl, sizeof fl, 1, f);
        for (int r = 0; r < fc->nr; r++) fwrite(fc->chars[r], sizeof(stbtt_packedchar), (size_t)range_count(r, fc->nr), f);
    }
    size_t total = (size_t)ff->aw * (size_t)ff->ah;
    for (size_t o = 0; o < total;) {
        if (bitmap[o]) { fputc(bitmap[o++], f); continue; }
        size_t run = 0;
        while (o + run < total && !bitmap[o + run] && run < 256) run++;
        fputc(0, f);
        fputc((int)(run - 1), f);
        o += run;
    }
    int ok = fclose(f) == 0;
    if (ok && rename(tmp, path) == 0) LOGI("font: cached atlas %dx%d in %s", ff->aw, ff->ah, path);
    else remove(tmp);
}

/* ---- baking ---- */

static int bake(FontFile *ff, const unsigned char *ttf, const int *sizes, int nsizes, int reduced, int aw, int ah, unsigned char **out)
{
    stbtt_fontinfo info;
    if (!stbtt_InitFont(&info, ttf, stbtt_GetFontOffsetForIndex(ttf, 0))) return -1;
    int asc, desc, gap;
    stbtt_GetFontVMetrics(&info, &asc, &desc, &gap);
    unsigned char *bitmap = calloc((size_t)aw * (size_t)ah, 1);
    stbtt_pack_context pc;
    if (!stbtt_PackBegin(&pc, bitmap, aw, ah, 0, 1, NULL)) { free(bitmap); return -1; }
    int ok = 1;
    ff->nfaces = nsizes;
    for (int i = 0; i < nsizes && ok; i++) {
        Face *fc = &ff->faces[i];
        fc->size = sizes[i];
        float scale = stbtt_ScaleForPixelHeight(&info, (float)fc->size);
        fc->ascent = asc * scale;
        fc->line_h = (asc - desc + gap) * scale;
        int huge = fc->size >= FONT_XL;
        /* horizontal oversampling keeps small text crisp; vertical adds little and
         * doubled the bake time */
        stbtt_PackSetOversampling(&pc, fc->size <= FONT_M ? 2 : 1, 1);
        /* Display sizes and the secondary faces keep the atlas small: Latin-1 + Extended-A only. */
        fc->nr = (huge || reduced) ? 3 : NRANGES;
        stbtt_pack_range pr[NRANGES];
        for (int r = 0; r < fc->nr; r++) {
            int count = range_count(r, fc->nr);
            fc->chars[r] = calloc((size_t)count, sizeof(stbtt_packedchar));
            pr[r].font_size = (float)fc->size;
            pr[r].first_unicode_codepoint_in_range = RANGES[r][0];
            pr[r].array_of_unicode_codepoints = NULL;
            pr[r].num_chars = count;
            pr[r].chardata_for_range = fc->chars[r];
        }
        if (!stbtt_PackFontRanges(&pc, ttf, 0, pr, fc->nr)) ok = 0;
    }
    stbtt_PackEnd(&pc);
    if (!ok) {
        free(bitmap);
        for (int i = 0; i < NSIZES; i++) for (int r = 0; r < NRANGES; r++) { free(ff->faces[i].chars[r]); ff->faces[i].chars[r] = NULL; }
        return 1;   /* overflow: try a bigger atlas */
    }
    ff->aw = aw; ff->ah = ah;
    *out = bitmap;
    return 0;
}

static int load_file(FontFile *ff, const char *path, int role, const int *sizes, int nsizes)
{
    char cpath[1200];
    cache_path(path, role, cpath, sizeof cpath);
    if (g_cache_dir[0] && cache_load(ff, cpath)) { utime(cpath, NULL); return 0; }   /* in use: tidy.c keeps it */
    unsigned char *ttf = read_all(path, NULL);
    if (!ttf) { LOGE("font: cannot open %s", path); return -1; }
    static const int DIMS[][2] = { { 1024, 1024 }, { 2048, 1024 }, { 2048, 2048 } };
    int first = role == F_ALT ? 0 : 2, rc = -1;
    unsigned char *bitmap = NULL;
    for (int d = first; d < 3; d++) {
        rc = bake(ff, ttf, sizes, nsizes, role != F_REGULAR, DIMS[d][0], DIMS[d][1], &bitmap);
        if (rc <= 0) break;
    }
    free(ttf);
    if (rc != 0) { LOGE("font: could not bake %s", path); return -1; }
    ff->tex = gfx_texture_alpha(bitmap, ff->aw, ff->ah);
    cache_save(ff, bitmap, cpath);
    free(bitmap);
    return 0;
}

void font_set_cache_dir(const char *dir) { snprintf(g_cache_dir, sizeof g_cache_dir, "%s", dir ? dir : ""); }

int font_init(const char *regular, const char *bold, const char *alt, int alt_kind)
{
    font_shutdown();
    snprintf(g_digits_ttf, sizeof g_digits_ttf, "%s", bold ? bold : regular);
    if (load_file(&g_files[F_REGULAR], regular, F_REGULAR, SIZES, NSIZES) != 0) return -1;
    g_have[F_REGULAR] = 1;
    g_have[F_BOLD] = bold && load_file(&g_files[F_BOLD], bold, F_BOLD, SIZES, NSIZES) == 0;
    if (!g_have[F_BOLD]) LOGW("font: no bold face, using regular");
    if (alt && alt_kind == FONT_ALT_MONO)
        g_have[F_ALT] = load_file(&g_files[F_ALT], alt, F_ALT, SIZES_MONO, (int)(sizeof SIZES_MONO / sizeof SIZES_MONO[0])) == 0;
    else if (alt && alt_kind == FONT_ALT_DISPLAY)
        g_have[F_ALT] = load_file(&g_files[F_ALT], alt, F_ALT, SIZES_DISPLAY, (int)(sizeof SIZES_DISPLAY / sizeof SIZES_DISPLAY[0])) == 0;
    return 0;
}

void font_shutdown(void)
{
    for (int f = 0; f < F_COUNT; f++) { free_file(&g_files[f]); g_have[f] = 0; }
}

static const FontFile *file_for(int size)
{
    if ((size & FONT_ALT) && g_have[F_ALT]) return &g_files[F_ALT];
    return (size & FONT_B) && g_have[F_BOLD] ? &g_files[F_BOLD] : &g_files[F_REGULAR];
}

static const Face *face_for(int size)
{
    int s = size & 0xFFF;
    const FontFile *ff = file_for(size);
    int best = 0;
    for (int i = 0; i < ff->nfaces; i++) if (abs(ff->faces[i].size - s) < abs(ff->faces[best].size - s)) best = i;
    return &ff->faces[best];
}

static int utf8_next(const char **s)
{
    const unsigned char *p = (const unsigned char *)*s;
    int cp, extra;
    if (p[0] < 0x80) { cp = p[0]; extra = 0; }
    else if ((p[0] & 0xE0) == 0xC0) { cp = p[0] & 0x1F; extra = 1; }
    else if ((p[0] & 0xF0) == 0xE0) { cp = p[0] & 0x0F; extra = 2; }
    else if ((p[0] & 0xF8) == 0xF0) { cp = p[0] & 0x07; extra = 3; }
    else { (*s)++; return 0xFFFD; }
    p++;
    for (int i = 0; i < extra; i++, p++) {
        if ((*p & 0xC0) != 0x80) { *s = (const char *)p; return 0xFFFD; }
        cp = (cp << 6) | (*p & 0x3F);
    }
    *s = (const char *)p;
    return cp;
}

static stbtt_packedchar *lookup(const Face *fc, int cp)
{
    for (int r = 0; r < fc->nr; r++) {
        int hi = RANGES[r][0] + range_count(r, fc->nr) - 1;
        if (cp >= RANGES[r][0] && cp <= hi) {
            stbtt_packedchar *pc = &fc->chars[r][cp - RANGES[r][0]];
            return (pc->x1 > pc->x0 || cp == ' ' || cp == 160) ? pc : NULL;
        }
    }
    return NULL;
}

float font_height(int size) { return face_for(size)->line_h * (float)(size & 0xFFF) / face_for(size)->size; }
float font_ascent(int size) { return face_for(size)->ascent * (float)(size & 0xFFF) / face_for(size)->size; }

/* Lays out [s, end) and draws it when `draw`. `track` adds space after every glyph
 * (letter spacing, in canvas units). With `max_w` > 0 it stops before the glyph that
 * would cross it and reports where in *stop. Returns the advance. */
static float walk(const char *s, const char *end, float x, float y, int size, Color c, int draw, float track,
                  float max_w, const char **stop)
{
    const FontFile *ff = file_for(size);
    const Face *fc = face_for(size);
    float scale = (float)(size & 0xFFF) / fc->size;   /* sizes between atlas sizes */
    float pen = 0, base = fc->ascent, tr = track / scale;
    while (*s && (!end || s < end)) {
        const char *at = s;
        int cp = utf8_next(&s);
        if (cp == '\n') continue;
        stbtt_packedchar *pcd = lookup(fc, cp);
        if (!pcd) {
            /* common fallbacks for glyphs outside the atlas */
            if (cp == 0x2022 || cp == 0x00B7) pcd = lookup(fc, 0x00B7);
            if (!pcd) continue;   /* emoji, CJK, symbols: skip silently */
        }
        stbtt_aligned_quad q;
        float px = pen, py = base;
        stbtt_GetPackedQuad(pcd, ff->aw, ff->ah, 0, &px, &py, &q, 0);
        if (max_w > 0 && px * scale > max_w) { if (stop) *stop = at; return pen * scale; }
        if (draw && c)
            gfx_glyph(ff->tex, x + q.x0 * scale, y + q.y0 * scale, (q.x1 - q.x0) * scale, (q.y1 - q.y0) * scale, q.s0, q.t0, q.s1, q.t1, c);
        pen = px + tr;
    }
    if (stop) *stop = s;
    return (pen - (pen > 0 ? tr : 0)) * scale;
}

float font_measure(const char *s, int size) { return s ? walk(s, NULL, 0, 0, size, 0, 0, 0, 0, NULL) : 0; }
void font_draw(const char *s, float x, float y, int size, Color c) { if (s) walk(s, NULL, x, y, size, c, 1, 0, 0, NULL); }
float font_measure_tracked(const char *s, int size, float track) { return s ? walk(s, NULL, 0, 0, size, 0, 0, track, 0, NULL) : 0; }
void font_draw_tracked(const char *s, float x, float y, int size, Color c, float track) { if (s) walk(s, NULL, x, y, size, c, 1, track, 0, NULL); }

void font_draw_center(const char *s, float cx, float y, int size, Color c)
{
    if (!s) return;
    font_draw(s, cx - font_measure(s, size) / 2, y, size, c);
}

void font_draw_shadow(const char *s, float x, float y, int size, Color c, Color shadow)
{
    if (!s) return;
    float o = (size & 0xFFF) >= FONT_L ? 3 : 2;
    Color sh2 = color_mul_alpha(shadow, 0.5f);
    walk(s, NULL, x + o, y + o, size, shadow, 1, 0, 0, NULL);
    walk(s, NULL, x - 1, y - 1, size, sh2, 1, 0, 0, NULL);
    walk(s, NULL, x + 1, y - 1, size, sh2, 1, 0, 0, NULL);
    walk(s, NULL, x - 1, y + 1, size, sh2, 1, 0, 0, NULL);
    walk(s, NULL, x, y, size, c, 1, 0, 0, NULL);
}

/* One pass to find the cut (it used to re-measure from the start for every glyph removed). */
void font_draw_fit(const char *s, float x, float y, int size, Color c, float max_w)
{
    if (!s) return;
    const char *end = NULL;
    walk(s, NULL, 0, 0, size, 0, 0, 0, max_w, &end);
    if (!*end) { font_draw(s, x, y, size, c); return; }
    const char *ell = "\xE2\x80\xA6";
    float avail = max_w - font_measure(ell, size);
    if (avail <= 0) end = s;
    else walk(s, NULL, 0, 0, size, 0, 0, 0, avail, &end);
    while (end > s && end[-1] == ' ') end--;
    float w = walk(s, end, x, y, size, c, 1, 0, 0, NULL);
    font_draw(ell, x + w, y, size, c);
}

int font_draw_wrapped(const char *s, float x, float y, int size, Color c, float max_w, int max_lines)
{
    if (!s) return 0;
    float lh = font_height(size);
    int lines = 0;
    const char *line = s;
    while (*line && lines < max_lines) {
        /* what fits on this line, then back up to the last space inside it */
        const char *nl = strchr(line, '\n'), *fit = NULL;
        walk(line, nl, 0, 0, size, 0, 0, 0, max_w, &fit);
        const char *end = fit;
        if (*fit && fit != nl) {
            const char *sp = NULL;
            for (const char *p = line; p < fit; p++) if (*p == ' ') sp = p;
            if (sp) end = sp;
            else if (fit == line) { end = line; utf8_next(&end); }   /* one glyph wider than the line */
        }
        int last = (lines == max_lines - 1) && *end;
        if (c) {
            if (last) font_draw_fit(line, x, y + lines * lh, size, c, max_w);
            else walk(line, end, x, y + lines * lh, size, c, 1, 0, 0, NULL);
        }
        lines++;
        line = end;
        while (*line == ' ' || *line == '\n') line++;
    }
    return lines;
}

/* ---- clock digits: 0-9 and ':' baked big, only while a screensaver clock shows ---- */

enum { DIGITS_PX = 400, DIGITS_AW = 2048, DIGITS_AH = 1024 };   /* 2 MB while a clock shows */
static unsigned g_dtex;
static stbtt_packedchar g_dch[11];   /* '0'..'9', ':' */
static float g_dcap;   /* the height of '0' at DIGITS_PX */

int font_digits_load(void)
{
    if (g_dtex) return 0;
    unsigned char *ttf = g_digits_ttf[0] ? read_all(g_digits_ttf, NULL) : NULL;
    if (!ttf) return -1;
    stbtt_fontinfo info;
    unsigned char *bitmap = calloc(DIGITS_AW * DIGITS_AH, 1);
    stbtt_pack_context pc;
    int ok = bitmap && stbtt_InitFont(&info, ttf, stbtt_GetFontOffsetForIndex(ttf, 0)) && stbtt_PackBegin(&pc, bitmap, DIGITS_AW, DIGITS_AH, 0, 1, NULL);
    if (ok) {
        ok = stbtt_PackFontRange(&pc, ttf, 0, DIGITS_PX, '0', 11, g_dch);   /* '0'..':' are adjacent */
        stbtt_PackEnd(&pc);
        g_dcap = -g_dch[0].yoff;
    }
    if (ok) g_dtex = gfx_texture_alpha(bitmap, DIGITS_AW, DIGITS_AH);
    free(bitmap);
    free(ttf);
    if (!g_dtex) { LOGW("font: no big clock digits"); return -1; }
    return 0;
}

void font_digits_free(void)
{
    if (g_dtex) gfx_texture_destroy(g_dtex);
    g_dtex = 0;
}

static float digits_walk(const char *s, float x, float y, float px, Color c, int draw)
{
    float scale = px / DIGITS_PX, pen = 0;
    for (; *s; s++) {
        if (*s < '0' || *s > ':') continue;
        stbtt_aligned_quad q;
        float qx = pen, qy = g_dcap;   /* the top of the digits at y */
        stbtt_GetPackedQuad(&g_dch[*s - '0'], DIGITS_AW, DIGITS_AH, 0, &qx, &qy, &q, 0);
        if (draw) gfx_glyph(g_dtex, x + q.x0 * scale, y + q.y0 * scale, (q.x1 - q.x0) * scale, (q.y1 - q.y0) * scale, q.s0, q.t0, q.s1, q.t1, c);
        pen = qx;
    }
    return pen * scale;
}

float font_digits_measure(const char *s, float px)
{
    if (!g_dtex) return font_measure(s, (int)px | FONT_B);
    return digits_walk(s, 0, 0, px, 0, 0);
}

void font_digits_draw(const char *s, float x, float y, float px, Color c)
{
    if (!g_dtex) { font_draw(s, x, y - (font_height((int)px | FONT_B) - px) * 0.5f, (int)px | FONT_B, c); return; }   /* no atlas: the scaled text face */
    digits_walk(s, x, y, px, c, 1);
}

float font_digits_height(float px) { return g_dtex ? g_dcap * px / DIGITS_PX : px * 0.72f; }
