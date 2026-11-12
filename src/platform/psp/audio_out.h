/* HUSH — PSP audio output: a dedicated high-priority thread renders the music
 * engine at 22.05 kHz, mixes effects, upsamples to 44.1 kHz and feeds a
 * blocking stereo PCM channel. */
#ifndef HUSH_AUDIO_OUT_H
#define HUSH_AUDIO_OUT_H

#include "../../audio/music.h"
#include "../../audio/sfx.h"

#define HUSH_ENGINE_RATE 22050
#define HUSH_OUT_RATE 44100

int audio_out_start(HushMusic *m, Sfx *s);
void audio_out_stop(void);
void audio_out_set_gain(float music, float sfx);
/* fraction of real time spent rendering audio (0..1), smoothed */
float audio_out_load(void);
uint32_t audio_out_underruns(void);
uint32_t audio_out_blocks(void);

#endif
