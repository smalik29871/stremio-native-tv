#include "font.h"
#include "../log.h"
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { ATLAS = 2048 };
static const int SIZES[] = { FONT_S, FONT_M, FONT_L, FONT_XL, FONT_TITLE };
#define NSIZES ((int)(sizeof SIZES / sizeof SIZES[0]))

/* Latin-1 Supplement + Latin Extended-A/B cover most European titles.
 * Everything else draws as a placeholder box (no CJK on old TVs' RAM budget). */
static const int RANGES[][2] = { { 32, 126 }, { 160, 591 }, { 0x2010, 0x2027 }, { 0x20AC, 0x20AC } };
#define NRANGES ((int)(sizeof RANGES / sizeof RANGES[0]))

typedef struct {
    int size;
    stbtt_packedchar *chars[NRANGES];
    float ascent, line_h;
} Face;

static Face g_faces[NSIZES];
static unsigned g_atlas_tex;
static unsigned char *g_ttf;

static int size_index(int size)
{
    for (int i = 0; i < NSIZES; i++) if (SIZES[i] == size) return i;
    int best = 0;
    for (int i = 1; i < NSIZES; i++) if (abs(SIZES[i] - size) < abs(SIZES[best] - size)) best = i;
    return best;
}

int font_init(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) { LOGE("font: cannot open %s", path); return -1; }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    g_ttf = malloc((size_t)n);
    if (fread(g_ttf, 1, (size_t)n, f) != (size_t)n) { fclose(f); return -1; }
    fclose(f);

    unsigned char *bitmap = calloc(ATLAS * ATLAS, 1);
    stbtt_pack_context pc;
    if (!stbtt_PackBegin(&pc, bitmap, ATLAS, ATLAS, 0, 1, NULL)) { free(bitmap); return -1; }

    stbtt_fontinfo info;
    stbtt_InitFont(&info, g_ttf, stbtt_GetFontOffsetForIndex(g_ttf, 0));
    int asc, desc, gap;
    stbtt_GetFontVMetrics(&info, &asc, &desc, &gap);

    for (int i = 0; i < NSIZES; i++) {
        Face *fc = &g_faces[i];
        fc->size = SIZES[i];
        float scale = stbtt_ScaleForPixelHeight(&info, (float)fc->size);
        fc->ascent = asc * scale;
        fc->line_h = (asc - desc + gap) * scale;
        /* Small sizes get 2x oversampling and the full repertoire; display sizes
         * keep the atlas small by skipping Latin Extended-B. */
        int big = fc->size >= FONT_L;
        stbtt_PackSetOversampling(&pc, big ? 1 : 2, big ? 1 : 2);
        int nr = big ? 2 : NRANGES;
        stbtt_pack_range pr[NRANGES];
        for (int r = 0; r < NRANGES; r++) {
            int count = RANGES[r][1] - RANGES[r][0] + 1;
            fc->chars[r] = calloc((size_t)count, sizeof(stbtt_packedchar));
            pr[r].font_size = (float)fc->size;
            pr[r].first_unicode_codepoint_in_range = RANGES[r][0];
            pr[r].array_of_unicode_codepoints = NULL;
            pr[r].num_chars = count;
            pr[r].chardata_for_range = fc->chars[r];
        }
        if (big) pr[1].num_chars = 383 - 160 + 1;
        if (!stbtt_PackFontRanges(&pc, g_ttf, 0, pr, nr))
            LOGW("font: atlas overflow at size %d", fc->size);
    }
    stbtt_PackEnd(&pc);
    g_atlas_tex = gfx_texture_alpha(bitmap, ATLAS, ATLAS);
    free(bitmap);
    return 0;
}

void font_shutdown(void)
{
    for (int i = 0; i < NSIZES; i++)
        for (int r = 0; r < NRANGES; r++) { free(g_faces[i].chars[r]); g_faces[i].chars[r] = NULL; }
    gfx_texture_destroy(g_atlas_tex);
    free(g_ttf);
    g_ttf = NULL;
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
    for (int r = 0; r < NRANGES; r++)
        if (cp >= RANGES[r][0] && cp <= RANGES[r][1]) return &fc->chars[r][cp - RANGES[r][0]];
    return NULL;
}

float font_height(int size) { return g_faces[size_index(size)].line_h; }

/* Core layout walker: measures and optionally draws. */
static float walk(const char *s, const char *end, float x, float y, int size, Color c, int draw)
{
    const Face *fc = &g_faces[size_index(size)];
    float pen = x, base = y + fc->ascent;
    while (*s && (!end || s < end)) {
        int cp = utf8_next(&s);
        if (cp == '\n') continue;
        /* Emoji and pictographs (common in stream titles) have no glyphs here; skip them. */
        if (cp >= 0x1F000 || (cp >= 0x2190 && cp <= 0x2BFF) || cp == 0xFE0F || cp == 0x200D) continue;
        stbtt_packedchar *pcd = lookup(fc, cp);
        if (!pcd) {
            float bw = fc->size * 0.5f;
            if (draw) gfx_rect_outline(pen + 2, y + fc->size * 0.2f, bw - 4, fc->size * 0.6f, 1, c);
            pen += bw;
            continue;
        }
        stbtt_aligned_quad q;
        float px = pen, py = base;
        stbtt_GetPackedQuad(pcd, ATLAS, ATLAS, 0, &px, &py, &q, 0);
        if (draw) gfx_glyph(g_atlas_tex, q.x0, q.y0, q.x1 - q.x0, q.y1 - q.y0, q.s0, q.t0, q.s1, q.t1, c);
        pen = px;
    }
    return pen - x;
}

float font_measure(const char *s, int size) { return walk(s, NULL, 0, 0, size, 0, 0); }
void font_draw(const char *s, float x, float y, int size, Color c) { walk(s, NULL, x, y, size, c, 1); }

void font_draw_fit(const char *s, float x, float y, int size, Color c, float max_w)
{
    if (font_measure(s, size) <= max_w) { font_draw(s, x, y, size, c); return; }
    float ell = font_measure("...", size);
    /* binary-ish: shrink by codepoints until it fits */
    const char *end = s + strlen(s);
    while (end > s) {
        const char *p = end - 1;
        while (p > s && ((unsigned char)*p & 0xC0) == 0x80) p--;
        end = p;
        if (walk(s, end, 0, 0, size, 0, 0) + ell <= max_w) break;
    }
    float w = walk(s, end, x, y, size, c, 1);
    font_draw("...", x + w, y, size, c);
}

int font_draw_wrapped(const char *s, float x, float y, int size, Color c, float max_w, int max_lines)
{
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
