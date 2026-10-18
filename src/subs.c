#include "subs.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static int parse_time(const char *s, double *out)
{
    /* 00:01:02,345  |  00:01:02.345  |  01:02.345 (VTT short form) */
    int h = 0, m = 0, sec = 0, ms = 0;
    int n = sscanf(s, "%d:%d:%d%*[,.]%d", &h, &m, &sec, &ms);
    if (n == 4) { *out = h * 3600.0 + m * 60.0 + sec + ms / 1000.0; return 0; }
    n = sscanf(s, "%d:%d%*[,.]%d", &m, &sec, &ms);
    if (n == 3) { *out = m * 60.0 + sec + ms / 1000.0; return 0; }
    return -1;
}

/* Windows-1252 0x80-0x9F to UTF-8 for files that are not valid UTF-8. */
static const char *CP1252[32] = {
    "\xE2\x82\xAC", "", "\xE2\x80\x9A", "\xC6\x92", "\xE2\x80\x9E", "\xE2\x80\xA6", "\xE2\x80\xA0", "\xE2\x80\xA1",
    "\xCB\x86", "\xE2\x80\xB0", "\xC5\xA0", "\xE2\x80\xB9", "\xC5\x92", "", "\xC5\xBD", "",
    "", "\xE2\x80\x98", "\xE2\x80\x99", "\xE2\x80\x9C", "\xE2\x80\x9D", "\xE2\x80\xA2", "\xE2\x80\x93", "\xE2\x80\x94",
    "\xCB\x9C", "\xE2\x84\xA2", "\xC5\xA1", "\xE2\x80\xBA", "\xC5\x93", "", "\xC5\xBE", "\xC5\xB8" };

static int looks_utf8(const unsigned char *s, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        unsigned char c = s[i];
        if (c < 0x80) continue;
        int extra = (c & 0xE0) == 0xC0 ? 1 : (c & 0xF0) == 0xE0 ? 2 : (c & 0xF8) == 0xF0 ? 3 : -1;
        if (extra < 0) return 0;
        for (int k = 1; k <= extra; k++) if (i + k >= n || (s[i + k] & 0xC0) != 0x80) return 0;
        i += (size_t)extra;
    }
    return 1;
}

static char *clean_text(const char *raw, int is_utf8)
{
    size_t cap = strlen(raw) * 3 + 1;
    char *out = malloc(cap);
    size_t o = 0;
    for (const char *p = raw; *p;) {
        if (*p == '<') {               /* <i>, </font ...>, <c.yellow> */
            const char *e = strchr(p, '>');
            if (e) { p = e + 1; continue; }
        }
        if (*p == '{' && p[1] == '\\') {   /* ASS override tags in SRT: {\an8} */
            const char *e = strchr(p, '}');
            if (e) { p = e + 1; continue; }
        }
        if (*p == '&') {
            static const struct { const char *e; const char *r; } ents[] = {
                { "&amp;", "&" }, { "&lt;", "<" }, { "&gt;", ">" }, { "&nbsp;", " " }, { "&quot;", "\"" }, { "&#39;", "'" }, { NULL, NULL } };
            int hit = 0;
            for (int i = 0; ents[i].e; i++) {
                size_t l = strlen(ents[i].e);
                if (strncmp(p, ents[i].e, l) == 0) { out[o++] = ents[i].r[0]; p += l; hit = 1; break; }
            }
            if (hit) continue;
        }
        if (*p == '\r') { p++; continue; }
        unsigned char c = (unsigned char)*p;
        if (!is_utf8 && c >= 0x80) {
            const char *rep = c < 0xA0 ? CP1252[c - 0x80] : NULL;
            if (rep) { size_t l = strlen(rep); memcpy(out + o, rep, l); o += l; }
            else { out[o++] = (char)(0xC0 | (c >> 6)); out[o++] = (char)(0x80 | (c & 0x3F)); }
            p++;
            continue;
        }
        out[o++] = *p++;
    }
    while (o && (out[o - 1] == '\n' || out[o - 1] == ' ')) o--;
    out[o] = 0;
    return out;
}

int subs_parse(const char *data, size_t len, SubTrack *out)
{
    memset(out, 0, sizeof *out);
    if (!data || !len) return -1;
    char *buf = malloc(len + 1);
    memcpy(buf, data, len);
    buf[len] = 0;
    char *p = buf;
    if ((unsigned char)p[0] == 0xEF && (unsigned char)p[1] == 0xBB && (unsigned char)p[2] == 0xBF) p += 3;
    int is_utf8 = looks_utf8((const unsigned char *)p, strlen(p));

    int cap = 256;
    out->cues = malloc(sizeof(SubCue) * (size_t)cap);
    char *line, *save = p;
    char text[4096];
    double start = 0, end = 0;
    int have_times = 0;
    text[0] = 0;

    #define FLUSH() do { if (have_times && text[0]) { \
            if (out->n == cap) { cap *= 2; out->cues = realloc(out->cues, sizeof(SubCue) * (size_t)cap); } \
            out->cues[out->n].start = start; out->cues[out->n].end = end; \
            out->cues[out->n].text = clean_text(text, is_utf8); out->n++; } \
        have_times = 0; text[0] = 0; } while (0)

    while ((line = save) && *line) {
        char *nl = strchr(line, '\n');
        if (nl) { *nl = 0; save = nl + 1; } else save = line + strlen(line);
        size_t L = strlen(line);
        while (L && (line[L - 1] == '\r' || line[L - 1] == ' ')) line[--L] = 0;
        if (!L) { FLUSH(); continue; }
        const char *arrow = strstr(line, "-->");
        if (arrow) {
            FLUSH();
            const char *a = line;
            while (*a == ' ') a++;
            const char *b = arrow + 3;
            while (*b == ' ') b++;
            if (parse_time(a, &start) == 0 && parse_time(b, &end) == 0 && end > start) have_times = 1;
            continue;
        }
        if (!have_times) continue;   /* cue numbers, WEBVTT header, NOTE blocks */
        size_t cur = strlen(text);
        if (cur + L + 2 < sizeof text) {
            if (cur) text[cur++] = '\n';
            memcpy(text + cur, line, L + 1);
        }
    }
    FLUSH();
    #undef FLUSH
    free(buf);
    if (!out->n) { free(out->cues); out->cues = NULL; return -1; }
    return out->n;
}

void subs_free(SubTrack *t)
{
    for (int i = 0; i < t->n; i++) free(t->cues[i].text);
    free(t->cues);
    memset(t, 0, sizeof *t);
}

int subs_align(const SubTrack *ref, const SubTrack *t, double *offset_out)
{
    if (!ref || !t || ref->n < 3 || t->n < 3) return 0;
    enum { HALF = 300, BINS = 2 * HALF + 1 };   /* ±30 s in 0.1 s steps */
    static int hist[BINS];
    memset(hist, 0, sizeof hist);
    int step = t->n > 400 ? t->n / 400 : 1, samples = 0, j = 0;
    for (int i = 0; i < t->n; i += step) {
        double s = t->cues[i].start;
        samples++;
        while (j < ref->n && ref->cues[j].start < s - 30) j++;
        for (int k = j; k < ref->n && ref->cues[k].start <= s + 30; k++) {
            int bin = (int)floor((ref->cues[k].start - s) * 10 + 0.5) + HALF;
            if (bin >= 0 && bin < BINS) hist[bin]++;
        }
    }
    int best = 0;
    for (int b = 1; b < BINS; b++) if (hist[b] > hist[best]) best = b;
    int support = hist[best] + (best > 0 ? hist[best - 1] : 0) + (best + 1 < BINS ? hist[best + 1] : 0);
    int second = 0;
    for (int b = 0; b < BINS; b++) if (abs(b - best) > 3 && hist[b] > second) second = hist[b];
    if (support < 3 || support < samples * 0.35 || hist[best] * 2 < second * 3) return 0;
    /* refine: the mean of the differences near the winning bin */
    double centre = (best - HALF) / 10.0, sum = 0; int n = 0; j = 0;
    for (int i = 0; i < t->n; i += step) {
        double s = t->cues[i].start;
        while (j < ref->n && ref->cues[j].start < s + centre - 0.35) j++;
        for (int k = j; k < ref->n && ref->cues[k].start <= s + centre + 0.35; k++) { sum += ref->cues[k].start - s; n++; }
    }
    *offset_out = n ? sum / n : centre;
    if (fabs(*offset_out) < 0.15) *offset_out = 0;
    return 1;
}

const char *subs_at(const SubTrack *t, double s)
{
    s -= t->offset;
    /* cues are usually sorted; binary search the last cue starting before s */
    int lo = 0, hi = t->n - 1, best = -1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (t->cues[mid].start <= s) { best = mid; lo = mid + 1; } else hi = mid - 1;
    }
    for (int i = best; i >= 0 && i > best - 4; i--)   /* tolerate slight overlap/disorder */
        if (t->cues[i].start <= s && s < t->cues[i].end) return t->cues[i].text;
    return NULL;
}
