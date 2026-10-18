/* Reads what a video file really contains from its first couple of megabytes:
 * Matroska (EBML) Tracks, Info, Chapters and colour metadata, or MP4 moov/stsd.
 * Pure parsing over a buffer, so the sources screen can fetch a byte range and
 * check a candidate against the TV before recommending it, and the player can
 * draw chapter marks. */
#ifndef SNTV_PROBE_H
#define SNTV_PROBE_H
#include "quality.h"
#include <stddef.h>
#include <stdint.h>

enum { PROBE_NONE = 0, PROBE_MKV = 1, PROBE_MP4 = 2 };
enum { PROBE_MAX_TRACKS = 24, PROBE_MAX_CHAPTERS = 48 };

typedef struct ProbeTrack {
    int kind;            /* 1 video, 2 audio, 3 subtitles */
    char codec[32];      /* Matroska codec id or MP4 fourcc, as written */
    char lang[8];
    char name[48];
    int channels;
    int forced, dflt;
} ProbeTrack;

typedef struct ProbeChapter { double start_s; char name[48]; } ProbeChapter;

typedef struct ProbeInfo {
    int container;
    int moov_missing;    /* MP4 with its index at the end: nothing to read up front */
    int width, height, bit_depth;
    double fps;
    int hdr;             /* 0 SDR, 1 HDR10/HLG, 2 Dolby Vision */
    VideoCodec vcodec;
    AudioCodec acodec;   /* the default (else first) audio track */
    int audio_channels;
    double duration_s;
    ProbeTrack tracks[PROBE_MAX_TRACKS]; int n_tracks;
    ProbeChapter chapters[PROBE_MAX_CHAPTERS]; int n_chapters;
} ProbeInfo;

/* 1 when a container header was understood (tracks found), else 0. */
int  probe_parse(const uint8_t *data, size_t len, ProbeInfo *out);
/* Replace the name-derived guesses with what the file says. */
void probe_apply(const ProbeInfo *p, StreamTags *t);
/* "HEVC 1080p 10-bit  ·  E-AC-3 5.1  ·  2 subtitle tracks" */
void probe_describe(const ProbeInfo *p, char *out, size_t n);
/* The bitrate in Mbit/s from the file size and the header's duration (0 if unknown). */
double probe_mbits(const ProbeInfo *p, uint64_t file_bytes);

#endif
