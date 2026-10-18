#include "audio.h"
#include "log.h"
#include <SDL.h>
#include <math.h>
#include <stdlib.h>

static SDL_AudioDeviceID g_dev;
enum { RATE = 44100 };

void audio_init(void)
{
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) { LOGI("audio: none (%s)", SDL_GetError()); return; }
    SDL_AudioSpec want = { 0 }, have;
    want.freq = RATE; want.format = AUDIO_S16SYS; want.channels = 1; want.samples = 2048;
    g_dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (!g_dev) LOGI("audio: no device (%s)", SDL_GetError());
    else LOGI("audio: %d Hz device for chimes", have.freq);
}

int audio_available(void) { return g_dev != 0; }

void audio_chime(void)
{
    if (!g_dev) return;
    const double notes[2] = { 880.0, 1174.66 };   /* A5, D6 */
    int per = RATE * 0.55, n = per * 2;
    Sint16 *buf = malloc((size_t)n * sizeof *buf);
    if (!buf) return;
    for (int k = 0; k < 2; k++)
        for (int i = 0; i < per; i++) {
            double t = (double)i / RATE, env = exp(-t * 4.5) * (t < 0.01 ? t / 0.01 : 1);
            double v = sin(2 * M_PI * notes[k] * t) * 0.7 + sin(2 * M_PI * notes[k] * 2 * t) * 0.15;
            buf[k * per + i] = (Sint16)(v * env * 9000);
        }
    SDL_ClearQueuedAudio(g_dev);
    SDL_QueueAudio(g_dev, buf, (Uint32)((size_t)n * sizeof *buf));
    SDL_PauseAudioDevice(g_dev, 0);
    free(buf);
}

void audio_shutdown(void) { if (g_dev) SDL_CloseAudioDevice(g_dev); g_dev = 0; }
