#include "quality.h"
#include "log.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

const char *const TVCAP_NAMES[TVCAP_COUNT] = { "dts", "truehd", "av1", "dv", "hevc", "4k" };
const char *const QUAL_MODE_NAMES[QUAL_MODE_COUNT] = { "Addon order", "Best for this TV", "Highest quality", "Balanced", "Smallest size" };

/* strcasestr is a GNU extension the TV toolchain does not declare */
static const char *ci_find(const char *hay, const char *needle)
{
    size_t n = strlen(needle);
    if (!n) return hay;
    for (const char *p = hay; *p; p++)
        if (strncasecmp(p, needle, n) == 0) return p;
    return NULL;
}

/* case-insensitive search that only matches whole tokens: "dv" must not hit "dvd" or "hdv" */
static int has_token(const char *hay, const char *needle)
{
    size_t n = strlen(needle);
    for (const char *p = hay; (p = ci_find(p, needle)) != NULL; p++) {
        int before = p == hay ? 0 : (unsigned char)p[-1];
        int after = (unsigned char)p[n];
        if (!isalnum(before) && !isalnum(after)) return 1;
    }
    return 0;
}

static int has(const char *hay, const char *needle) { return ci_find(hay, needle) != NULL; }

void quality_parse(const char *name, const char *title, const char *description, const char *filename,
                   uint64_t size_bytes, StreamTags *out)
{
    memset(out, 0, sizeof *out);
    out->seeders = -1;
    char text[2400];
    snprintf(text, sizeof text, "%s\n%s\n%s\n%s", name ? name : "", title ? title : "", description ? description : "", filename ? filename : "");

    /* the pixel count first: "1080p.UHD.BluRay" (an encode from a UHD disc) and "4K
     * Remastered ... 1080p" are 1080p; "4K"/"UHD" alone mean 2160 */
    if (has(text, "2160p")) out->res = 2160;
    else if (has(text, "1440p")) out->res = 1440;
    else if (has(text, "1080p") || has(text, "1080i")) out->res = 1080;
    else if (has(text, "720p")) out->res = 720;
    else if (has(text, "480p") || has(text, "576p")) out->res = 480;
    else if (has(text, "360p") || has(text, "240p")) out->res = 360;
    else if (has_token(text, "4k") || has_token(text, "uhd")) out->res = 2160;
    else if (has_token(text, "fhd")) out->res = 1080;
    else if (has_token(text, "sd")) out->res = 480;   /* unknown would be scored like 1080p */

    if (has_token(text, "av1")) out->codec = CODEC_AV1;
    else if (has(text, "hevc") || has(text, "x265") || has(text, "h265") || has(text, "h.265")) out->codec = CODEC_HEVC;
    else if (has(text, "x264") || has(text, "h264") || has(text, "h.264") || has_token(text, "avc")) out->codec = CODEC_AVC;
    else if (has(text, "xvid") || has(text, "divx") || has(text, "mpeg-2") || has_token(text, "vc1") || has(text, "vc-1")) out->codec = CODEC_OLD;

    if (has(text, "dolby vision") || has(text, "dovi") || has_token(text, "dv")) out->hdr = 2;
    else if (has_token(text, "hdr") || has(text, "hdr10") || has_token(text, "hlg")) out->hdr = 1;   /* not "HDRip" or "HDTV" */

    out->atmos = has(text, "atmos");
    if (has_token(text, "dts") || has(text, "dts-hd") || has(text, "dts:x") || has(text, "dtsx") || has(text, "dts-x")) out->audio = AUDIO_DTS;
    else if (has(text, "truehd") || has(text, "true-hd")) out->audio = AUDIO_TRUEHD;
    else if (has_token(text, "ddp") || has(text, "eac3") || has(text, "e-ac-3") || has(text, "dd+") || has_token(text, "ddp5") || has(text, "ddp5.1") || has(text, "ddp7.1")) out->audio = AUDIO_DDP;
    else if (has_token(text, "ac3") || has(text, "ac-3") || has_token(text, "dd5") || has(text, "dd5.1") || has(text, "dd2.0")) out->audio = AUDIO_AC3;
    else if (has_token(text, "aac")) out->audio = AUDIO_AAC;
    else if (has_token(text, "opus") || has_token(text, "flac") || has_token(text, "pcm")) out->audio = AUDIO_OTHER;


    if (has(text, "\xE2\x9A\xA1") || has(text, "[rd+]") || has(text, "[tb+]") || has(text, "[ad+]") || has(text, "[pm+]") || has(text, "[dl+]")
        || has(text, "[oc+]") || has(text, "[ed+]") || has(text, "cached") || has(text, "instant")) out->cached = 1;
    if (has(text, "\xE2\x8F\xB3") || has(text, "\xE2\xAC\x87") || has(text, "[rd download]") || has(text, "[tb download]") || has(text, "download only") || has(text, "not cached")
        || has(text, "uncached") || has(text, "[rd]") || has(text, "[tb]") || has(text, "[ad]") || has(text, "[pm]")) out->cached = -1;

    const char *s = strstr(text, "\xF0\x9F\x91\xA4");   /* person emoji precedes the seeder count */
    if (s) { s += 4; while (*s == ' ') s++; if (isdigit((unsigned char)*s)) out->seeders = atoi(s); }

    /* "2.83 GB", "300.08 MB" */
    for (const char *p = text; *p; p++) {
        if (!isdigit((unsigned char)*p) || (p > text && (isalnum((unsigned char)p[-1]) || p[-1] == '.'))) continue;
        char *end;
        double v = strtod(p, &end);
        while (*end == ' ') end++;
        if (!strncasecmp(end, "GB", 2) || !strncasecmp(end, "GiB", 3)) { out->size_gb = v; break; }
        if (!strncasecmp(end, "MB", 2) || !strncasecmp(end, "MiB", 3)) { out->size_gb = v / 1024.0; break; }
        p = end > p ? end - 1 : p;
    }
    if (size_bytes) out->size_gb = size_bytes / 1073741824.0;   /* the addon's exact file size beats one in the text (often the whole torrent's) */

    /* a cinema recording or a screener says so whatever else the name has ("HDTS WEB"); the
     * bare "TS"/"TC" are ambiguous and only count when nothing better is named */
    int cam = has(text, "hdcam") || has(text, "hdts") || has(text, "telesync") || has(text, "telecine") || has(text, "camrip") || has_token(text, "cam")
              || has(text, "dvdscr") || has(text, "bdscr") || has(text, "webscr") || has(text, "screener") || has_token(text, "scr") || has(text, "workprint") || has_token(text, "hdtc")
              || has(text, "predvd") || has(text, "hqcam") || has_token(text, "r5");
    if (cam) out->tier = TIER_CAM;
    else if (has(text, "remux")) out->tier = TIER_REMUX;
    else if (has(text, "bluray") || has(text, "blu-ray") || has(text, "bdrip") || has(text, "brrip") || has(text, "web-dl") || has(text, "webdl") || has_token(text, "web")) out->tier = TIER_WEB;
    else if (has(text, "webrip") || has(text, "hdtv") || has(text, "dvdrip") || has(text, "hdrip")) out->tier = TIER_TV;
    else if (has_token(text, "ts") || has_token(text, "tc")) out->tier = TIER_CAM;
    out->threed = has_token(text, "3d") || has(text, "half-sbs") || has(text, "h-sbs");
    if (out->atmos && out->audio == AUDIO_UNKNOWN)   /* Atmos alone: DD+ on streaming releases, TrueHD on discs */
        out->audio = out->tier == TIER_REMUX || has(text, "bluray") || has(text, "blu-ray") ? AUDIO_TRUEHD : AUDIO_DDP;
    out->dv_fallback = out->hdr == 2 && (has(text, "hdr10") || has_token(text, "hdr") || has(text, "dv.hdr") || has(text, "dv | hdr"));
    out->avc10 = out->codec == CODEC_AVC && (has(text, "10bit") || has(text, "10-bit") || has(text, "hi10"));
    out->hardsub = has_token(text, "hc") || has(text, "hardsub") || has(text, "hardcoded");
}

int quality_cap_ok(const TvProfile *tv, int cap)
{
    int enough = cap == TVCAP_HEVC || cap == TVCAP_4K ? 3 : 2;   /* one bad file is not the TV: HEVC and 4K play on nearly every set here */
    if (tv->fail[cap] > tv->ok[cap] && tv->fail[cap] >= enough) return 0;
    if (tv->ok[cap] > 0) return 1;
    switch (cap) {   /* nothing observed yet: what LG's own tables say */
    case TVCAP_DTS: return tv->webos_major == 0 || tv->webos_major >= 8;   /* dropped on 2020-2022 sets, back on some 2023+ */
    case TVCAP_TRUEHD: return 0;
    case TVCAP_AV1: return tv->webos_major == 0 || tv->webos_major >= 5;
    default: return 1;
    }
}

unsigned quality_needs(const StreamTags *t)
{
    unsigned m = 0;
    if (t->audio == AUDIO_DTS) m |= 1u << TVCAP_DTS;
    if (t->audio == AUDIO_TRUEHD) m |= 1u << TVCAP_TRUEHD;
    if (t->codec == CODEC_AV1) m |= 1u << TVCAP_AV1;
    if (t->hdr == 2 && !t->dv_fallback) m |= 1u << TVCAP_DV;   /* with an HDR10 layer it plays as HDR without DV */
    if (t->codec == CODEC_HEVC) m |= 1u << TVCAP_HEVC;
    if (t->res >= 2160) m |= 1u << TVCAP_4K;
    return m;
}

static double mbits(const StreamTags *t, double runtime_min)
{
    if (t->size_gb <= 0 || runtime_min <= 0) return 0;
    return t->size_gb * 8192.0 / (runtime_min * 60.0);
}

long quality_score(const StreamTags *t, const TvProfile *tv, const QualityPrefs *p, double runtime_min)
{
    long s = 0;
    int cap = p->cap_res ? p->cap_res : 2160;
    int res = t->res ? t->res : 1080;
    int mode = p->mode == QUAL_ADDON_ORDER ? QUAL_BEST : p->mode;
    static const struct { int res, best, balanced, saver; } R[] = {
        { 2160, 400, 260, 100 }, { 1440, 330, 280, 160 }, { 1080, 300, 300, 260 }, { 720, 200, 220, 300 }, { 480, 100, 100, 200 },
        { 360, 40, 40, 120 } };
    for (size_t i = 0; i < sizeof R / sizeof R[0]; i++)
        if (res == R[i].res) s += mode == QUAL_SAVER ? R[i].saver : mode == QUAL_BALANCED ? R[i].balanced : R[i].best;
    if (mode == QUAL_HIGHEST && res >= 2160) s += 150;
    if (!t->res) s -= 40;                              /* unknown resolution: a little suspicious */
    if (res > cap) s -= 300;

    switch (t->codec) {
    case CODEC_HEVC: s += quality_cap_ok(tv, TVCAP_HEVC) ? 20 : -800; break;
    case CODEC_AVC: s += 10; break;
    case CODEC_AV1: s += quality_cap_ok(tv, TVCAP_AV1) ? 10 : -1500; break;
    case CODEC_OLD: s -= 60; break;
    default: break;
    }
    if (t->hdr == 2) s += quality_cap_ok(tv, TVCAP_DV) ? 30 : t->dv_fallback ? 10 : -150;   /* with an HDR10 layer it plays as HDR anyway */
    else if (t->hdr == 1) s += res >= 2160 ? 25 : 10;
    if (t->res >= 2160 && !quality_cap_ok(tv, TVCAP_4K)) s -= 600;

    switch (t->audio) {
    case AUDIO_DTS: s += quality_cap_ok(tv, TVCAP_DTS) ? 0 : -400; break;
    case AUDIO_TRUEHD: s += quality_cap_ok(tv, TVCAP_TRUEHD) ? 0 : -300; break;
    case AUDIO_DDP: case AUDIO_AC3: case AUDIO_AAC: s += 5; break;
    default: break;
    }

    if (t->cached > 0) s += 150;
    else if (t->cached < 0) s -= 700;
    else if (t->seeders >= 0) s += t->seeders < 3 ? -250 : (t->seeders > 200 ? 200 : t->seeders) / 4;

    switch (t->tier) {
    case TIER_CAM: s -= 900; break;
    case TIER_TV: s += 5; break;
    case TIER_WEB: s += 20; break;
    case TIER_REMUX: s += mode == QUAL_HIGHEST ? 40 : 10; break;
    default: break;
    }
    if (t->threed) s -= 500;
    if (t->avc10) s -= 500;      /* plays nowhere on a TV: only a last resort */
    if (t->hardsub) s -= 150;    /* subtitles you can't turn off */

    double mb = mbits(t, runtime_min);
    double limit = mode == QUAL_SAVER ? 8 : mode == QUAL_BALANCED ? 25 : mode == QUAL_HIGHEST ? 120 : 60;
    if (mb > limit) s -= (long)((mb - limit) * 4);
    /* far too small for its resolution: a fake, or squeezed until it looks worse than 1080p */
    if (mb > 0 && ((t->res >= 2160 && mb < 6) || (t->res == 1080 && mb < 1.5))) s -= 250;
    if (mode == QUAL_SAVER) s -= (long)(t->size_gb * 10);
    return s;
}

int quality_warnings(const StreamTags *t, const TvProfile *tv, const QualityPrefs *p, const char **out, int max)
{
    int n = 0;
    #define ADD(s) do { if (n < max) out[n++] = (s); } while (0)
    if (t->codec == CODEC_AV1 && !quality_cap_ok(tv, TVCAP_AV1)) ADD("AV1 video: this TV can't decode it");
    if (t->audio == AUDIO_DTS && !quality_cap_ok(tv, TVCAP_DTS)) ADD("DTS audio: may be silent on this TV");
    if (t->audio == AUDIO_TRUEHD && !quality_cap_ok(tv, TVCAP_TRUEHD)) ADD("TrueHD audio: may be silent on this TV");
    if (t->hdr == 2 && !quality_cap_ok(tv, TVCAP_DV)) ADD("Dolby Vision: colours may look wrong here");
    if (t->cached < 0) ADD("Not cached: needs a download first");
    if (t->tier == TIER_CAM) ADD("Cinema recording");
    if (t->threed) ADD("3D release");
    if (t->avc10) ADD("10-bit H.264: TVs can't decode it");
    if (t->hardsub) ADD("Subtitles burned into the picture");
    if (t->res > (p->cap_res ? p->cap_res : 2160)) ADD("Above your resolution cap");
    if (t->cached == 0 && t->seeders >= 0 && t->seeders < 3) ADD("Very few seeders");
    #undef ADD
    return n;
}

void quality_summary(const StreamTags *t, double runtime_min, char *out, size_t n)
{
    static const char *CODECS[] = { "", "H.264", "HEVC", "AV1", "old codec" };
    static const char *AUDIO[] = { "", "AAC", "Dolby Digital", "Dolby Digital+", "DTS", "TrueHD", "" };
    char res[16] = "";
    if (t->res == 2160) snprintf(res, sizeof res, "4K"); else if (t->res) snprintf(res, sizeof res, "%dp", t->res);
    int o = snprintf(out, n, "%s%s%s%s%s%s%s", res, res[0] && t->codec ? " " : "", CODECS[t->codec],
                     t->hdr == 2 ? " Dolby Vision" : t->hdr == 1 ? " HDR" : "",
                     AUDIO[t->audio][0] ? "  \xC2\xB7  " : "", AUDIO[t->audio], t->atmos ? " Atmos" : "");
    if (o < 0) return;
    size_t u = (size_t)o;
    if (t->cached > 0 && u < n) u += (size_t)snprintf(out + u, n - u, "  \xC2\xB7  cached");
    else if (t->seeders >= 0 && u < n) u += (size_t)snprintf(out + u, n - u, "  \xC2\xB7  %d seeders", t->seeders);
    double mb = mbits(t, runtime_min);
    if (mb > 0 && u < n) snprintf(out + u, n - u, "  \xC2\xB7  %.0f Mbit/s", mb);
    else if (t->size_gb > 0 && u < n) snprintf(out + u, n - u, "  \xC2\xB7  %.1f GB", t->size_gb);
}

void quality_tv_init(TvProfile *tv, const char *release_text)
{
    memset(tv, 0, sizeof *tv);
    if (!release_text) return;
    /* "webOS TV 6.3.0-..." or "6.3.0" or "webOS 23" style strings: first number after "webos" if present */
    const char *p = ci_find(release_text, "webos");
    p = p ? p + 5 : release_text;
    while (*p && !isdigit((unsigned char)*p)) p++;
    if (*p) tv->webos_major = atoi(p);
    if (tv->webos_major >= 20) tv->webos_major = 8;   /* "webOS 23/24" marketing names: current generation */
}

void quality_tv_learn(TvProfile *tv, unsigned needs, int worked)
{
    for (int c = 0; c < TVCAP_COUNT; c++)
        if (needs & (1u << c)) { if (worked) tv->ok[c]++; else tv->fail[c]++; }
}

double quality_runtime_minutes(const char *runtime)
{
    if (!runtime) return 0;
    double mins = 0;
    const char *p = runtime;
    while (*p) {
        if (isdigit((unsigned char)*p)) {
            char *end;
            double v = strtod(p, &end);
            while (*end == ' ') end++;
            if (*end == 'h' || *end == 'H') mins += v * 60; else mins += v;
            p = end;
        } else p++;
    }
    return mins;
}

/* The release group: "Show.S01E02.1080p.WEB.h264-NTb.mkv" -> "NTb", "[SubsPlease] Show - 02"
 * -> "SubsPlease". Episodes from one group share encoding, audio and subtitles. */
void quality_release_group(const char *text, char *out, size_t n)
{
    out[0] = 0;
    if (!text || !*text || n < 2) return;
    if (text[0] == '[') {
        const char *e = strchr(text, ']');
        if (e && e - text > 2 && e - text < 24) snprintf(out, n, "%.*s", (int)(e - text - 1), text + 1);
        return;
    }
    char t[256];
    snprintf(t, sizeof t, "%s", text);
    char *nl = strpbrk(t, "\n\r"); if (nl) *nl = 0;
    char *dot = strrchr(t, '.');   /* the extension */
    if (dot && strlen(dot) <= 5 && (!strcasecmp(dot, ".mkv") || !strcasecmp(dot, ".mp4") || !strcasecmp(dot, ".avi") || !strcasecmp(dot, ".ts") || !strcasecmp(dot, ".m4v"))) *dot = 0;
    char *dash = strrchr(t, '-');
    if (!dash || !dash[1]) return;
    const char *g = dash + 1;
    size_t len = strcspn(g, " .[]()");
    static const char *const NOT_GROUPS[] = { "DL", "Rip", "RIP", "HD", "SD", "UHD", "DV", "HDR", "Atmos", NULL };
    if (len < 2 || len > 14) return;
    for (int i = 0; NOT_GROUPS[i]; i++) if (strlen(NOT_GROUPS[i]) == len && !strncasecmp(g, NOT_GROUPS[i], len)) return;
    int letters = 0;
    for (size_t i = 0; i < len; i++) { if (!isalnum((unsigned char)g[i])) return; if (isalpha((unsigned char)g[i])) letters++; }
    if (!letters) return;
    snprintf(out, n, "%.*s", (int)len, g);
}

/* lowercase words, one space between and at each end: " show s01e05 1080p " */
static void ep_words(const char *in, char *out, size_t n)
{
    size_t o = 0;
    out[o++] = ' ';
    for (const unsigned char *p = (const unsigned char *)in; *p && o + 2 < n; p++) {
        if (*p == '\'') continue;
        if (isalnum(*p) || *p >= 0x80) out[o++] = (char)tolower(*p);
        else if (out[o - 1] != ' ') out[o++] = ' ';
    }
    if (out[o - 1] != ' ') out[o++] = ' ';
    out[o] = 0;
}

/* the episodes a word list names outright: s01e05, s01e05e06, 1x05, "s01 e05",
 * "season 1 episode 5"; *other set when it names one that is not this */
static int names_exact(const char *w, int season, int episode, int *other)
{
    int hit = 0;
    for (const char *p = w + 1; *p; p++) {
        if (p[-1] != ' ') continue;
        int s = -1, e = -1, len = 0;
        if (sscanf(p, "s%de%d%n", &s, &e, &len) == 2 || sscanf(p, "s%d e%d%n", &s, &e, &len) == 2
            || sscanf(p, "season %d episode %d%n", &s, &e, &len) == 2
            || (isdigit((unsigned char)p[0]) && sscanf(p, "%dx%d%n", &s, &e, &len) == 2 && s < 100)) {
            const char *q = p + len;
            int e2 = -1, l2 = 0;
            /* a range written with the letters: "s01e01-e08", "s01e01 s01e08" (a bare "-08" is
             * found in the raw text, quality_episode_score: here "s01e01 12" could be a title) */
            if ((sscanf(q, " e%d%n", &e2, &l2) == 1 || sscanf(q, " s%*de%d%n", &e2, &l2) == 1)
                && (q[l2] == ' ' || !q[l2]) && e2 > e && (season <= 0 || s == season)) {
                if (episode >= e && episode <= e2) hit = 1; else *other = 1;
                continue;
            }
            if ((season <= 0 || s == season) && e == episode) hit = 1;
            else if (season <= 0 || s == season) {   /* s01e04e05: a double episode */
                while (*q == 'e' && sscanf(q, "e%d%n", &e2, &l2) == 1) { if (e2 == episode) hit = 1; q += l2; }
                if (!hit) *other = 1;
            } else *other = 1;
        }
    }
    return hit;
}

/* "S01E01-08" in the raw text (the dash matters, words() drops it): 1 holds the episode,
 * -1 a range without it, 0 none */
static int raw_range(const char *t, int season, int episode)
{
    for (const char *p = t; *p; p++) {
        if ((*p != 's' && *p != 'S') || (p > t && isalnum((unsigned char)p[-1]))) continue;
        int s, a, b, len = 0;
        if (sscanf(p + 1, "%d%*[eE]%d-%d%n", &s, &a, &b, &len) == 3 && !isalnum((unsigned char)p[1 + len]) && b > a && b < 1000 && (season <= 0 || s == season))
            return episode >= a && episode <= b ? 1 : -1;
    }
    return 0;
}

int quality_episode_score(const char *path, const EpisodeHint *e)
{
    if (!path || !e || e->episode <= 0) return 0;
    const char *slash = strrchr(path, '/');
    const char *base = slash ? slash + 1 : path;
    int rr = raw_range(base, e->season, e->episode);
    if (rr) return rr > 0 ? 3 : -1;
    char all[768], file[512];
    ep_words(path, all, sizeof all);
    ep_words(base, file, sizeof file);
    int other = 0;
    /* the file's own name first: a pack's folder "S01E01-E10" names only the first */
    if (names_exact(file, e->season, e->episode, &other)) return 3;
    if (other) return -1;
    if (e->title[0] && strlen(e->title) >= 4) {   /* "05 - The One Where..." */
        char t[200];
        ep_words(e->title, t, sizeof t);
        if (strlen(t) > 5 && strstr(file, t)) return 2;
    }
    /* "Season 1/05 - Name.mkv", "Show S01/Episode 5.mkv", "[Group] Show - 13 [1080p].mkv" */
    int in_season = 0, any_season = 0;   /* a pack with no season named is taken as season 1 */
    for (const char *p = all + 1; *p; p++) {
        int s, len = 0;
        if (p[-1] == ' ' && (sscanf(p, "season %d%n", &s, &len) == 1 || sscanf(p, "s%d %n", &s, &len) == 1)) { any_season = 1; in_season |= s == e->season; }
    }
    for (const char *p = file + 1; *p; p++) {
        if (p[-1] != ' ') continue;
        int num, len = 0;
        if (sscanf(p, "episode %d%n", &num, &len) == 1 || sscanf(p, "ep %d%n", &num, &len) == 1
            || sscanf(p, "ep%d%n", &num, &len) == 1 || sscanf(p, "e%d%n", &num, &len) == 1
            || (isdigit((unsigned char)*p) && sscanf(p, "%d%n", &num, &len) == 1 && len <= 3)) {
            if (p[len] != ' ') continue;   /* "1080p", "x264": not a number on its own */
            if ((num == 2 || num == 5 || num == 7) && (p[len + 1] == '0' || p[len + 1] == '1') && p[len + 2] == ' ') continue;   /* "5.1" sound */
            if (num == e->episode && (in_season || e->season <= 0 || (!any_season && e->season == 1))) return 2;
            if (e->absolute > 0 && num == e->absolute) return 2;
        }
    }
    return 0;
}

/* The episode's file in a season pack, by the names: the one its name says is it, else its
 * one main file; never a file named for another episode, never a guess between look-alikes
 * (those are PICK_ASK: only the AI, reading the list as a person would, may say). */
long quality_pick_episode_file(const EpisodeHint *ep, PackFile *f, int n, long addon_file)
{
    for (int i = 0; i < n && addon_file >= 0; i++) {   /* the addon's file, unless its name says another episode */
        if (f[i].id != addon_file) continue;
        if (quality_episode_score(f[i].name, ep) < 0) {
            LOGI("torbox: the addon's file is another episode (%s); not S%dE%d", f[i].name, ep->season, ep->episode);
            return PICK_NONE;
        }
        return addon_file;
    }
    int best = -1;
    for (int i = 0; i < n; i++) {
        f[i].score = quality_episode_score(f[i].name, ep);
        if (best < 0 || f[i].score > f[best].score || (f[i].score == f[best].score && f[i].size > f[best].size)) best = i;
    }
    if (best < 0) return PICK_NONE;
    int tied = 0, others = 0;
    for (int i = 0; i < n; i++) { tied += f[i].score == f[best].score && f[i].size > f[best].size * 0.25; others += f[i].score < 0; }
    if (f[best].score > 0) {   /* named for it (two copies so named: the larger) */
        LOGI("torbox: the episode's file by its name: %s", f[best].name);
        return f[best].id;
    }
    /* the pack names its episodes and none is this one (or every file is another): not here;
     * one stray file named for another (a preview) does not make it such a pack */
    if (f[best].score < 0 || others >= 2) {
        LOGI("torbox: no file in this torrent is S%dE%d (it names other episodes)", ep->season, ep->episode);
        return PICK_NONE;
    }
    if (tied == 1) {   /* nothing named: the one main file */
        LOGI("torbox: no episode named; its one main file plays: %s", f[best].name);
        return f[best].id;
    }
    if (tied > 1) return PICK_ASK;
    LOGI("torbox: can't tell which file is S%dE%d; leaving this torrent", ep->season, ep->episode);
    return PICK_NONE;
}
