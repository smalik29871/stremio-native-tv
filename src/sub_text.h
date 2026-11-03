/* One cleaner for the text of a subtitle line, whichever way it came (an SRT or VTT file, the film's own track, the
 * desktop player, a translation), so a tag the file wrote never shows on screen:
 *
 *   {\an8}  {\i1}  {\pos(10,20)}  { \an8 }  {\an8 (no end)  &#123;\an8&#125;  the fullwidth braces  {an8}
 *   <i> </font> <c.yellow>   \N (a line break)  \h (a space)  ASS drawings {\p1}...{\p0}
 *
 * A line that asked for the top ({\an7}..{\an9}) is reported, so it is drawn there. Words in braces that are not tags
 * ("{sighs}") and a lone "<" ("<3") stay. In place, UTF-8 safe; the text only ever gets shorter. Pure:
 * tests/test_sub_text.c. */
#ifndef SNTV_SUB_TEXT_H
#define SNTV_SUB_TEXT_H
#include <string.h>

/* a spelling of "{" or "}" (an entity, or the fullwidth sign): the length of it, and which brace; 0 when it is not one */
static inline int subtext_brace(const char *p, char *which)
{
    static const struct { const char *s; char b; } sp[] = {
        { "&#123;", '{' }, { "&#x7B;", '{' }, { "&#x7b;", '{' }, { "&lbrace;", '{' }, { "&lcub;", '{' },
        { "&#125;", '}' }, { "&#x7D;", '}' }, { "&#x7d;", '}' }, { "&rbrace;", '}' }, { "&rcub;", '}' },
        { "\xEF\xBD\x9B", '{' }, { "\xEF\xBD\x9D", '}' }, { NULL, 0 } };   /* U+FF5B, U+FF5D */
    for (int i = 0; sp[i].s; i++) {
        size_t l = strlen(sp[i].s);
        if (!strncmp(p, sp[i].s, l)) { *which = sp[i].b; return (int)l; }
    }
    return 0;
}

/* a spelling of "<" or ">" that a file may use for a tag (the look-alike signs); the length of it, and which; 0 when it is not one */
static inline int subtext_angle(const char *p, char *which)
{
    static const struct { const char *s; char b; } sp[] = {
        { "\xE2\x80\xB9", '<' }, { "\xE2\x80\xBA", '>' },   /* U+2039, U+203A */
        { "\xE3\x80\x88", '<' }, { "\xE3\x80\x89", '>' },   /* U+3008, U+3009 */
        { "\xE2\x9F\xA8", '<' }, { "\xE2\x9F\xA9", '>' },   /* U+27E8, U+27E9 */
        { "\xE2\x8C\xA9", '<' }, { "\xE2\x8C\xAA", '>' },   /* U+2329, U+232A */
        { "\xEF\xBC\x9C", '<' }, { "\xEF\xBC\x9E", '>' },   /* U+FF1C, U+FF1E */
        { NULL, 0 } };
    for (int i = 0; sp[i].s; i++) {
        size_t l = strlen(sp[i].s);
        if (!strncmp(p, sp[i].s, l)) { *which = sp[i].b; return (int)l; }
    }
    return 0;
}

static inline int subtext_letter(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }

/* A tag opening at r ("<i>", "</font color=..>", the same with look-alike signs, or escaped as "&lt;i&gt;"): where it ends
 * (just past its closer), or NULL when it is not a tag ("<3", "1 < 2"). */
static inline const char *subtext_tag_end(const char *r)
{
    char ab = 0;
    int al = subtext_angle(r, &ab);
    const char *q;
    int escaped = !strncmp(r, "&lt;", 4);
    if (*r == '<') q = r + 1;
    else if (al && ab == '<') q = r + al;
    else if (escaped) q = r + 4;
    else return NULL;
    if (*q == '/') q++;
    if (!subtext_letter(*q)) return NULL;
    for (int k = 0; *q && k < 60; k++) {
        char b2 = 0;
        int l2 = subtext_angle(q, &b2);
        if (escaped) { if (!strncmp(q, "&gt;", 4)) return q + 4; }
        else if (*q == '>') return q + 1;
        else if (l2 && b2 == '>') return q + l2;
        if (*q == '<' || (l2 && b2 == '<')) return NULL;   /* another opens first: this one has no end */
        q += l2 ? l2 : 1;
    }
    return NULL;
}

/* the text between braces: is it an override tag ("\an8", "\i1", "an8") rather than a word? */
static inline int subtext_is_tag(const char *c, size_t n, int *top, int *draw)
{
    int has_backslash = 0;
    for (size_t i = 0; i < n; i++) {
        if (c[i] != '\\') continue;
        has_backslash = 1;
        if (i + 3 < n && c[i + 1] == 'a' && c[i + 2] == 'n' && c[i + 3] >= '7' && c[i + 3] <= '9') *top = 1;
        if (i + 2 < n && c[i + 1] == 'p' && c[i + 2] >= '0' && c[i + 2] <= '9') *draw = c[i + 2] != '0';   /* \p1: a drawing starts, \p0: it ends */
    }
    if (has_backslash) return 1;
    size_t s = 0, e = n;
    while (s < e && c[s] == ' ') s++;
    while (e > s && c[e - 1] == ' ') e--;
    if (e - s >= 3 && e - s <= 4 && (c[s] == 'a' || c[s] == 'A') && (c[s + 1] == 'n' || c[s + 1] == 'N') && c[s + 2] >= '1' && c[s + 2] <= '9') {   /* {an8}: the backslash lost */
        if (c[s + 2] >= '7') *top = 1;
        return 1;
    }
    return 0;
}

/* A tag in this raw line that a TV's own subtitle drawing shows as text (the app can draw it clean instead): the look-alike
 * angle signs, a tag written escaped, an SRT tag inside an ASS line (ASS does not read them), an override with no end. */
static inline int subtext_odd(const char *s, int ass)
{
    for (const char *r = s; *r; r++) {
        char ab = 0;
        int al = subtext_angle(r, &ab);
        if (al && ab == '<' && subtext_tag_end(r)) return 1;
        if (!strncmp(r, "&lt;", 4) && subtext_tag_end(r)) return 1;
        if (ass && *r == '<' && subtext_tag_end(r)) return 1;
        if (*r == '{' && r[1] == '\\') {
            const char *e = strchr(r, '}');
            if (!e || e - r > 120) return 1;
        }
    }
    return 0;
}

/* Cleans `s` in place. Returns 1 when the text asked to be drawn at the top. */
static inline int subtext_clean(char *s)
{
    int top = 0, drawing = 0;
    char *w = s;
    for (const char *r = s; *r;) {
        char b;
        int bl = subtext_brace(r, &b);
        char cur = bl ? b : *r;
        int cl = bl ? bl : 1;
        if (cur == '{') {
            /* its end: the next closing brace (as written, or spelled as an entity) within a tag's length */
            const char *q = r + cl;
            const char *close = NULL;
            size_t clen = 1;
            for (size_t k = 0; *q && k < 120; k++) {
                char b2;
                int l2 = subtext_brace(q, &b2);
                if ((l2 && b2 == '}') || (!l2 && *q == '}')) { close = q; clen = l2 ? (size_t)l2 : 1; break; }
                if ((l2 && b2 == '{') || (!l2 && *q == '{')) break;   /* another opens first: this one has no end */
                q += l2 ? l2 : 1;
            }
            if (close) {
                if (subtext_is_tag(r + cl, (size_t)(close - (r + cl)), &top, &drawing)) { r = close + clen; continue; }
            } else {
                const char *t = r + cl;
                while (*t == ' ') t++;
                if (*t == '\\') {   /* "{\an8" with no end: the tag runs to the next space */
                    const char *e = t;
                    while (*e && *e != ' ' && *e != '\n' && e - t < 60) e++;
                    int tp = 0, dr = drawing;
                    subtext_is_tag(t, (size_t)(e - t), &tp, &dr);
                    if (tp) top = 1;
                    drawing = dr;
                    r = e;
                    continue;
                }
            }
        }
        if (drawing) { r += cl; continue; }
        {   /* <i>, </font ...>, <c.yellow>, the look-alike signs and &lt;i&gt;; "<3" stays */
            const char *e = subtext_tag_end(r);
            if (e) { r = e; continue; }
        }
        if (*r == '\\' && (r[1] == 'N' || r[1] == 'n')) { *w++ = '\n'; r += 2; continue; }
        if (*r == '\\' && r[1] == 'h') { *w++ = ' '; r += 2; continue; }
        if (*r == '\r') { r++; continue; }
        if (bl) { *w++ = b; r += bl; continue; }   /* a brace that was only spelled as an entity: the brace, kept as text */
        *w++ = *r++;
    }
    *w = 0;
    /* the edges: lines and spaces a removed tag left behind */
    size_t len = (size_t)(w - s), a = 0;
    while (len && (s[len - 1] == '\n' || s[len - 1] == ' ')) len--;
    while (a < len && (s[a] == '\n' || s[a] == ' ')) a++;
    if (a) memmove(s, s + a, len - a);
    s[len - a] = 0;
    return top;
}

#endif
