#include "frames.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
#define CHECK(c) do { if (!(c)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

static int reads;
static int file_read(void *ctx, uint64_t off, uint64_t len, uint8_t **buf, size_t *got, uint64_t *total)
{
    FILE *f = fopen(ctx, "rb");
    if (!f) return 0;
    fseek(f, 0, SEEK_END); long size = ftell(f);
    *total = (uint64_t)size;
    if ((long)off >= size) { fclose(f); *buf = NULL; *got = 0; return 0; }
    if (off + len > (uint64_t)size) len = (uint64_t)size - off;
    *buf = malloc(len ? len : 1);
    fseek(f, (long)off, SEEK_SET);
    *got = fread(*buf, 1, len, f);
    fclose(f);
    reads++;
    return 1;
}

/* The picture's average colour, from the BMP (24-bit, rows padded to 4 bytes). */
static void average(const uint8_t *bmp, int rgb[3], int *w, int *h)
{
    *w = bmp[18] | bmp[19] << 8; *h = bmp[22] | bmp[23] << 8;
    int stride = (*w * 3 + 3) & ~3;
    long s[3] = { 0 };
    for (int y = 0; y < *h; y++) for (int x = 0; x < *w; x++) for (int c = 0; c < 3; c++) s[c] += bmp[54 + y * stride + x * 3 + (2 - c)];
    for (int c = 0; c < 3; c++) rgb[c] = (int)(s[c] / (*w * *h));
}

static int near(const int a[3], const int b[3]) { for (int c = 0; c < 3; c++) if (abs(a[c] - b[c]) > 14) return 0; return 1; }

static void check(const char *name, double t, const int want[3])
{
    char path[512], err[200] = "";
    snprintf(path, sizeof path, "%s/media/%s", SNTV_FIXTURES, name);
    size_t len = 0;
    reads = 0;
    uint8_t *bmp = frames_picture(file_read, path, path, t, 160, &len, err, sizeof err);
    if (!bmp) { fprintf(stderr, "%s at %.0f s: %s\n", name, t, err); CHECK(bmp); return; }
    int rgb[3], w, h;
    average(bmp, rgb, &w, &h);
    printf("%-22s %4.1f s: %dx%d, rgb %d %d %d, %d reads\n", name, t, w, h, rgb[0], rgb[1], rgb[2], reads);
    CHECK(w == 160 && h == 90);
    if (want) CHECK(near(rgb, want));
    free(bmp);
}

int main(void)
{
    if (!frames_supported()) { printf("frames: built without FFmpeg, skipped\n"); return 0; }
    /* 20 s, a keyframe every 2 s, the colour turning with time (references from the ffmpeg CLI) */
    static const int AT4[3] = { 99, 124, 0 }, AT10[3] = { 11, 138, 138 }, AT16[3] = { 156, 54, 195 };
    static const char *const FILES[] = { "seek_h264.mkv", "seek_h264.mp4", "seek_hevc10.mkv" };
    for (int i = 0; i < 3; i++) {
        check(FILES[i], 4.0, AT4);
        check(FILES[i], 10.9, AT10);   /* between keyframes: the one before */
        check(FILES[i], 17.0, AT16);
    }
    check("sample_hevc_eac3.mp4", 0.5, NULL);   /* HEVC in MP4 */
    char err[200] = ""; size_t len;
    char path[512];
    snprintf(path, sizeof path, "%s/library.json", SNTV_FIXTURES);
    CHECK(!frames_picture(file_read, path, path, 1, 160, &len, err, sizeof err) && strstr(err, "not a Matroska or MP4"));
    if (failures) fprintf(stderr, "%d failure(s)\n", failures);
    else printf("frames: all checks passed\n");
    return failures != 0;
}
