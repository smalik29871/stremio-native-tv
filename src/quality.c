#include "quality.h"
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

    if (has(text, "2160p") || has_token(text, "4k") || has_token(text, "uhd")) out->res = 2160;
    else if (has(text, "1440p")) out->res = 1440;
    else if (has(text, "1080p") || has_token(text, "fhd") || has(text, "1080i")) out->res = 1080;
    else if (has(text, "720p")) out->res = 720;
    else if (has(text, "480p") || has(text, "576p") || has_token(text, "sd")) out->res = 480;
    else if (has(text, "360p") || has(text, "240p")) out->res = 360;   /* unknown would be scored like 1080p */

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
    if (out->atmos && out->audio == AUDIO_UNKNOWN) out->audio = AUDIO_TRUEHD;   /* Atmos rips are mostly TrueHD */

    if (has(text, "\xE2\x9A\xA1") || has(text, "[rd+]") || has(text, "[tb+]") || has(text, "[ad+]") || has(text, "[pm+]") || has(text, "[dl+]")
        || has(text, "[oc+]") || has(text, "[ed+]") || has(text, "cached") || has(text, "instant")) out->cached = 1;
    if (has(text, "\xE2\x8F\xB3") || has(text, "[rd download]") || has(text, "[tb download]") || has(text, "download only") || has(text, "not cached")
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
    if (out->size_gb <= 0 && size_bytes) out->size_gb = size_bytes / 1073741824.0;

    if (has(text, "remux")) out->tier = TIER_REMUX;
    else if (has(text, "bluray") || has(text, "blu-ray") || has(text, "bdrip") || has(text, "brrip") || has(text, "web-dl") || has(text, "webdl") || has_token(text, "web")) out->tier = TIER_WEB;
    else if (has(text, "webrip") || has(text, "hdtv") || has(text, "dvdrip") || has(text, "hdrip")) out->tier = TIER_TV;
    else if (has(text, "hdcam") || has(text, "hdts") || has(text, "telesync") || has(text, "telecine") || has_token(text, "cam") || has_token(text, "ts") || has_token(text, "tc") || has(text, "camrip")) out->tier = TIER_CAM;
    out->threed = has_token(text, "3d") || has(text, "half-sbs") || has(text, "h-sbs");
}

int quality_cap_ok(const TvProfile *tv, int cap)
{
    if (tv->fail[cap] > tv->ok[cap]) return 0;
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
    if (t->hdr == 2) m |= 1u << TVCAP_DV;
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
    if (t->hdr == 2) s += quality_cap_ok(tv, TVCAP_DV) ? 30 : -150;
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

    double mb = mbits(t, runtime_min);
    double limit = mode == QUAL_SAVER ? 8 : mode == QUAL_BALANCED ? 25 : mode == QUAL_HIGHEST ? 120 : 60;
    if (mb > limit) s -= (long)((mb - limit) * 4);
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
