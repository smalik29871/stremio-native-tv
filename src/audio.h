/* A soft chime through SDL audio, for break reminders. Silent when the platform
 * gives us no audio device (the media pipeline is used for real recordings). */
#ifndef SNTV_AUDIO_H
#define SNTV_AUDIO_H
void audio_init(void);
int  audio_available(void);
void audio_chime(void);        /* two rising notes, about a second */
void audio_shutdown(void);
#endif
