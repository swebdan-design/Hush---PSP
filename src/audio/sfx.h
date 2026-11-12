/* HUSH — synthesised sound effects mixed alongside the music engine.
 * The Moth's engine hum is tuned to the composition's key centre, so the
 * craft itself is part of the instrument. Single producer (game) / single
 * consumer (audio thread) request ring; no allocation. */
#ifndef HUSH_SFX_H
#define HUSH_SFX_H

#include <stdint.h>

enum { SFX_UI_MOVE = 0, SFX_UI_SELECT, SFX_UI_BACK, SFX_SCAN_DONE, SFX_IMPACT, SFX_EMP, SFX_COLLECT,
       SFX_TRANSMISSION, SFX_BOOST, SFX_DISCOVERY, SFX_FAIL, SFX_COMPLETE, SFX_COUNT };

#define SFX_SHOTS 8
typedef struct {
    int type;
    float t, dur, gain;
    uint32_t ph, ph2, noise;
} SfxShot;

typedef struct {
    /* continuous controls, written by the game thread */
    volatile float engine, engine_pitch, wind, scan, master;
    volatile int key;
    volatile int req[16];
    volatile uint32_t req_head;
    uint32_t req_tail;
    /* audio-thread state */
    float sr, eng_s, wind_s, scan_s, pitch_s;
    uint32_t eph1, eph2, eph3, sph, noise;
    float wlp, whp, scan_lfo;
    SfxShot shots[SFX_SHOTS];
} Sfx;

void sfx_init(Sfx *s, float sample_rate);
void sfx_play(Sfx *s, int type);
/* Adds into an interleaved stereo float buffer. */
void sfx_render(Sfx *s, float *out, int frames);

#endif
