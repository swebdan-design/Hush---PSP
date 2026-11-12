/* HUSH — synthesizer voices.
 *
 * Tone voice: two oscillators (any wave, detune, FM, pulse width), noise mix,
 * SVF filter with its own envelope, amp ADSR, per-voice LFO, glide, soft drive,
 * equal-power pan, and delay/reverb send levels.
 * Drum voice: dedicated percussion models (kick, snare, hats, metal, click, thud). */
#ifndef HUSH_SYNTH_H
#define HUSH_SYNTH_H

#include <stdint.h>
#include "dsp.h"

enum { BUS_PAD = 0, BUS_MAIN, BUS_RADIO, BUS_COUNT };

typedef struct {
    uint8_t wave1, wave2;
    float mix2;          /* 0 = osc1 only, 1 = osc2 only */
    float detune2;       /* semitones */
    float pw;            /* pulse width for W_PULSE, 0.05..0.95 */
    float fm_ratio, fm_index;  /* W_FM: modulator ratio and peak index */
    float noise;         /* noise mix 0..1 */
    uint8_t ftype;
    float cutoff;        /* base cutoff as MIDI note value (e.g. 84 ~ 1 kHz) */
    float res;
    float fenv_amt;      /* semitones of cutoff at full filter envelope */
    float key_track;     /* 0..1 */
    float a, d, s, r;    /* amp ADSR seconds/level */
    float fa, fd, fs, fr;
    float lfo_rate;      /* Hz */
    float lfo_pitch;     /* cents */
    float lfo_cut;       /* semitones */
    float lfo_pwm;       /* pulse width depth */
    float lfo_morph;     /* osc mix depth (wavetable morph) */
    float drive;         /* 0 = clean */
    float glide;         /* seconds */
    float send_delay, send_reverb;
    uint8_t bus;
} Patch;

typedef struct {
    uint8_t active, layer, bus, released;
    int16_t note_id;          /* integer note for display/voice matching */
    Patch p;
    float pitch, pitch_target, glide_coef;
    float pitch_drift;        /* cents, written by the composer's decay process */
    uint32_t ph1, ph2, inc1, inc2;
    const float *t1, *t2;
    float mix_cur;
    Svf f;
    Env amp, fenv;
    float lfo_ph;
    float gain, gain_cur, pan_l, pan_r;
    float vel;
    uint32_t noise;
    uint32_t age;
    float level_meter;        /* recent output level for visualisation */
} Voice;

void voice_start(Voice *v, const Patch *p, float note, float vel, float pan, uint8_t layer, uint32_t noise_seed);
void voice_release(Voice *v);
/* Renders n (<= HUSH_CTRL) samples, adding into L/R of bus buffers and send buffers. */
void voice_render(Voice *v, int n, float layer_gain, float *outL, float *outR,
                  float *sendDL, float *sendDR, float *sendR, float cut_offset);

/* ---- Percussion ---- */
enum { DR_KICK = 0, DR_SNARE, DR_HAT, DR_OPENHAT, DR_METAL, DR_CLICK, DR_THUD, DR_RIM, DR_COUNT };
extern const char *const hush_drum_names[DR_COUNT];

typedef struct {
    uint8_t active, kind, layer;
    float t;                 /* seconds since trigger */
    float vel, pan_l, pan_r, decay, tone;
    uint32_t ph, ph2, ph3[6], noise;
    float pitch_env, amp;
    Svf f, f2;
    float send_delay, send_reverb;
    float level_meter;
} DrumVoice;

void drum_start(DrumVoice *d, int kind, float vel, float pan, float decay, float tone, uint32_t noise_seed);
void drum_render(DrumVoice *d, int n, float gain, float *outL, float *outR, float *sendDL, float *sendDR, float *sendR);

#endif
