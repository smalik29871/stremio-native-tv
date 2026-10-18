#include "font.h"
#include "../log.h"
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { ATLAS = 2048 };
static const int SIZES[] = { FONT_XS, FONT_S, FONT_M, FONT_L, FONT_XL, FONT_TITLE };
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
    unsigned char *ttf;
    unsigned tex;
    Face faces[NSIZES];
} FontFile;

static FontFile g_files[2];
static int g_have_bold;

static unsigned char *read_all(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char *b = malloc((size_t)n);
    if (fread(b, 1, (size_t)n, f) != (size_t)n) { free(b); b = NULL; }
    fclose(f);
    return b;
}

static int load_file(FontFile *ff, const char *path, int bold)
{
    ff->ttf = read_all(path);
    if (!ff->ttf) { LOGE("font: cannot open %s", path); return -1; }
    stbtt_fontinfo info;
    if (!stbtt_InitFont(&info, ff->ttf, stbtt_GetFontOffsetForIndex(ff->ttf, 0))) { LOGE("font: bad font %s", path); return -1; }
    int asc, desc, gap;
    stbtt_GetFontVMetrics(&info, &asc, &desc, &gap);

    unsigned char *bitmap = calloc(ATLAS * ATLAS, 1);
    stbtt_pack_context pc;
    if (!stbtt_PackBegin(&pc, bitmap, ATLAS, ATLAS, 0, 1, NULL)) { free(bitmap); return -1; }
    for (int i = 0; i < NSIZES; i++) {
        Face *fc = &ff->faces[i];
        fc->size = SIZES[i];
        float scale = stbtt_ScaleForPixelHeight(&info, (float)fc->size);
        fc->ascent = asc * scale;
        fc->line_h = (asc - desc + gap) * scale;
        int small = fc->size <= FONT_M;
        int huge = fc->size >= FONT_XL;
        stbtt_PackSetOversampling(&pc, small ? 2 : 1, small ? 2 : 1);
        /* Display sizes and the bold face keep the atlas small: Latin-1 + Extended-A only. */
        fc->nr = (huge || bold) ? 3 : NRANGES;
        stbtt_pack_range pr[NRANGES];
        for (int r = 0; r < fc->nr; r++) {
            int count = RANGES[r][1] - RANGES[r][0] + 1;
            if (r == 1 && (huge || bold)) count = 383 - 160 + 1;
            fc->chars[r] = calloc((size_t)count, sizeof(stbtt_packedchar));
            pr[r].font_size = (float)fc->size;
            pr[r].first_unicode_codepoint_in_range = RANGES[r][0];
            pr[r].array_of_unicode_codepoints = NULL;
            pr[r].num_chars = count;
            pr[r].chardata_for_range = fc->chars[r];
        }
        if (!stbtt_PackFontRanges(&pc, ff->ttf, 0, pr, fc->nr))
            LOGW("font: atlas overflow at size %d (%s)", fc->size, bold ? "bold" : "regular");
    }
    stbtt_PackEnd(&pc);
    ff->tex = gfx_texture_alpha(bitmap, ATLAS, ATLAS);
    free(bitmap);
    return 0;
}

int font_init(const char *regular, const char *bold)
{
    if (load_file(&g_files[0], regular, 0) != 0) return -1;
    g_have_bold = bold && load_file(&g_files[1], bold, 1) == 0;
    if (!g_have_bold) LOGW("font: no bold face, using regular");
    return 0;
}

void font_shutdown(void)
{
    for (int f = 0; f < 2; f++) {
        for (int i = 0; i < NSIZES; i++)
            for (int r = 0; r < NRANGES; r++) { free(g_files[f].faces[i].chars[r]); g_files[f].faces[i].chars[r] = NULL; }
        gfx_texture_destroy(g_files[f].tex);
        free(g_files[f].ttf);
        memset(&g_files[f], 0, sizeof g_files[f]);
    }
}

static const FontFile *file_for(int size) { return (size & FONT_B) && g_have_bold ? &g_files[1] : &g_files[0]; }

static const Face *face_for(int size)
{
    int s = size & 0xFFF;
    const FontFile *ff = file_for(size);
    int best = 0;
    for (int i = 0; i < NSIZES; i++) if (abs(SIZES[i] - s) < abs(SIZES[best] - s)) best = i;
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
        int hi = RANGES[r][1];
        if (r == 1 && fc->nr == 3) hi = 383;
        if (cp >= RANGES[r][0] && cp <= hi) {
            stbtt_packedchar *pc = &fc->chars[r][cp - RANGES[r][0]];
            return (pc->x1 > pc->x0 || cp == ' ' || cp == 160) ? pc : NULL;
        }
    }
    return NULL;
}

float font_height(int size) { return face_for(size)->line_h; }
float font_ascent(int size) { return face_for(size)->ascent; }

static float walk(const char *s, const char *end, float x, float y, int size, Color c, int draw)
{
    const FontFile *ff = file_for(size);
    const Face *fc = face_for(size);
    float scale = (float)(size & 0xFFF) / fc->size;   /* sizes between atlas sizes */
    float pen = 0, base = fc->ascent;
    while (*s && (!end || s < end)) {
        int cp = utf8_next(&s);
        if (cp == '\n') continue;
        if (cp == 0x2018 || cp == 0x2019) cp = cp; /* curly quotes are in range */
        stbtt_packedchar *pcd = lookup(fc, cp);
        if (!pcd) {
            /* common fallbacks for glyphs outside the atlas */
            if (cp == 0x2022 || cp == 0x00B7) pcd = lookup(fc, 0x00B7);
            if (!pcd) continue;   /* emoji, CJK, symbols: skip silently */
        }
        stbtt_aligned_quad q;
        float px = pen, py = base;
        stbtt_GetPackedQuad(pcd, ATLAS, ATLAS, 0, &px, &py, &q, 0);
        if (draw && c)
            gfx_glyph(ff->tex, x + q.x0 * scale, y + q.y0 * scale, (q.x1 - q.x0) * scale, (q.y1 - q.y0) * scale, q.s0, q.t0, q.s1, q.t1, c);
        pen = px;
    }
    return pen * scale;
}

float font_measure(const char *s, int size) { return s ? walk(s, NULL, 0, 0, size, 0, 0) : 0; }
void font_draw(const char *s, float x, float y, int size, Color c) { if (s) walk(s, NULL, x, y, size, c, 1); }

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
    walk(s, NULL, x + o, y + o, size, shadow, 1);
    walk(s, NULL, x - 1, y - 1, size, sh2, 1);
    walk(s, NULL, x + 1, y - 1, size, sh2, 1);
    walk(s, NULL, x - 1, y + 1, size, sh2, 1);
    walk(s, NULL, x, y, size, c, 1);
}

void font_draw_fit(const char *s, float x, float y, int size, Color c, float max_w)
{
    if (!s) return;
    if (font_measure(s, size) <= max_w) { font_draw(s, x, y, size, c); return; }
    const char *ell = "\xE2\x80\xA6";
    float ellw = font_measure(ell, size);
    const char *end = s + strlen(s);
    while (end > s) {
        const char *p = end - 1;
        while (p > s && ((unsigned char)*p & 0xC0) == 0x80) p--;
        end = p;
        if (walk(s, end, 0, 0, size, 0, 0) + ellw <= max_w) break;
    }
    while (end > s && end[-1] == ' ') end--;
    float w = walk(s, end, x, y, size, c, 1);
    font_draw(ell, x + w, y, size, c);
}

int font_draw_wrapped(const char *s, float x, float y, int size, Color c, float max_w, int max_lines)
{
    if (!s) return 0;
    float lh = font_height(size);
    int lines = 0;
    const char *line = s;
    while (*line && lines < max_lines) {
        const char *p = line, *last_space = NULL, *fit_end = NULL;
        while (*p && *p != '\n') {
            const char *q = p;
            utf8_next(&q);
            if (walk(line, q, 0, 0, size, 0, 0) > max_w) break;
            if (*p == ' ') last_space = p;
            fit_end = q;
            p = q;
        }
        const char *end;
        if (!*p || *p == '\n') end = p;
        else if (last_space) end = last_space;
        else end = fit_end ? fit_end : p + 1;
        int last = (lines == max_lines - 1) && *end;
        if (c) {
            if (last) font_draw_fit(line, x, y + lines * lh, size, c, max_w);
            else walk(line, end, x, y + lines * lh, size, c, 1);
        }
        lines++;
        line = end;
        while (*line == ' ' || *line == '\n') line++;
    }
    return lines;
}
