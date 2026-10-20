/* Which source suits this TV. Streams are described only by their names
 * ("1080p HEVC DTS 2.8 GB [TB+]"), so the tags are parsed from the text, scored
 * against what the TV can decode, and the TV's profile is corrected by what
 * actually happened: a source that failed teaches the app which tags to avoid,
 * one that played for a while teaches which are fine. */
#ifndef SNTV_QUALITY_H
#define SNTV_QUALITY_H
#include <stddef.h>
#include <stdint.h>

typedef enum { CODEC_UNKNOWN, CODEC_AVC, CODEC_HEVC, CODEC_AV1, CODEC_OLD } VideoCodec;
typedef enum { AUDIO_UNKNOWN, AUDIO_AAC, AUDIO_AC3, AUDIO_DDP, AUDIO_DTS, AUDIO_TRUEHD, AUDIO_OTHER } AudioCodec;
typedef enum { TIER_UNKNOWN, TIER_CAM, TIER_TV, TIER_WEB, TIER_REMUX } SourceTier;

typedef struct StreamTags {
    int res;            /* 2160, 1440, 1080, 720, 480; 0 unknown */
    VideoCodec codec;
    int hdr;            /* 0 none, 1 HDR10(+), 2 Dolby Vision */
    AudioCodec audio;
    int atmos;
    int cached;         /* 1 debrid-cached, -1 needs a download first, 0 unknown */
    int seeders;        /* -1 unknown */
    double size_gb;     /* 0 unknown */
    SourceTier tier;
    int threed;
} StreamTags;

/* Capabilities a source can depend on; the TV profile keeps a belief per entry. */
enum { TVCAP_DTS, TVCAP_TRUEHD, TVCAP_AV1, TVCAP_DV, TVCAP_HEVC, TVCAP_4K, TVCAP_COUNT };
extern const char *const TVCAP_NAMES[TVCAP_COUNT];

typedef struct TvProfile {
    int webos_major;            /* 0 when not an LG TV */
    int ok[TVCAP_COUNT], fail[TVCAP_COUNT];   /* observed plays and failures */
} TvProfile;

enum { QUAL_ADDON_ORDER, QUAL_BEST, QUAL_HIGHEST, QUAL_BALANCED, QUAL_SAVER, QUAL_MODE_COUNT };
extern const char *const QUAL_MODE_NAMES[QUAL_MODE_COUNT];

typedef struct QualityPrefs {
    int mode;           /* QUAL_* */
    int cap_res;        /* 2160, 1080 or 720 */
} QualityPrefs;

void quality_parse(const char *name, const char *title, const char *description, const char *filename,
                   uint64_t size_bytes, StreamTags *out);
/* Higher is better. runtime_min 0 when unknown. Hard problems score below -500. */
long quality_score(const StreamTags *t, const TvProfile *tv, const QualityPrefs *p, double runtime_min);
/* Up to `max` short viewer-facing concerns ("DTS audio: may be silent on this TV"). */
int  quality_warnings(const StreamTags *t, const TvProfile *tv, const QualityPrefs *p, const char **out, int max);
/* One line for the recommendation card: "4K HEVC Dolby Vision  ·  cached  ·  18 Mbit/s" */
void quality_summary(const StreamTags *t, double runtime_min, char *out, size_t n);
unsigned quality_needs(const StreamTags *t);      /* bit per TVCAP_* the source depends on */
int  quality_cap_ok(const TvProfile *tv, int cap);  /* current belief */

/* Profile: starts from the webOS release, then learns. */
void quality_tv_init(TvProfile *tv, const char *release_text);
void quality_tv_learn(TvProfile *tv, unsigned needs, int worked);
/* "Show.S01E02.1080p.WEB.h264-NTb.mkv" -> "NTb"; "" when there is none */
void quality_release_group(const char *text, char *out, size_t n);
/* "25 min", "1h 40min", "95" -> minutes; 0 when unparsable */
double quality_runtime_minutes(const char *runtime);

#endif
