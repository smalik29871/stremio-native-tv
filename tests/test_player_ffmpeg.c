#define SDL_MAIN_HANDLED
#include "../src/video/video_ffmpeg.c"

void app_redraw(void) {}
int youtube_id_valid(const char *id) { (void)id; return 0; }

static int failures;
#define CHECK(c) do { if (!(c)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

int main(void)
{
    AVFormatContext fmt = { 0 };
    AVStream stream = { 0 };
    Player pl = { 0 };
    pl.fmt = &fmt;
    stream.time_base = (AVRational){ 1, 90000 };
    fmt.start_time = 3600LL * AV_TIME_BASE;
    CHECK(fabs(ts(&pl, &stream, 3600LL * 90000) - 0) < 0.001);
    CHECK(fabs(ts(&pl, &stream, 3615LL * 90000) - 15) < 0.001);
    CHECK(ts(&pl, &stream, AV_NOPTS_VALUE) == -1);
    fmt.start_time = AV_NOPTS_VALUE;
    CHECK(fabs(ts(&pl, &stream, 15LL * 90000) - 15) < 0.001);

    pl.am = SDL_CreateMutex();
    pl.ai = 0; pl.dev = 1;
    P = &pl; g_paused = 0;
    pl.spec.channels = 2; pl.spec.freq = 48000; pl.spec.samples = 1024;
    Uint8 audio[19200] = { 0 }, played[19200];
    pl.fifo = audio; pl.f_len = sizeof audio; pl.fifo_pts = 100;
    audio_cb(&pl, played, sizeof played);
    pl.clock_at = SDL_GetPerformanceCounter() - SDL_GetPerformanceFrequency() * 10;
    CHECK(fabs(clock_now() - 100.1) < 0.001);
    Uint8 silence[4096];
    memset(silence, 0x7f, sizeof silence);
    audio_cb(&pl, silence, sizeof silence);
    CHECK(fabs(clock_now() - 100.1) < 0.001);
    for (size_t i = 0; i < sizeof silence; i++) CHECK(silence[i] == 0);
    g_paused = 1;
    CHECK(fabs(clock_now() - pl.clock_pts) < 0.001);
    SDL_DestroyMutex(pl.am); P = NULL;
    if (failures) return 1;
    puts("desktop clock and timestamp checks passed");
    return 0;
}
