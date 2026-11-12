/* HUSH — DSP building blocks shared by the PSP game and the host renderer.
 *
 * Oscillators are band-limited wavetables (mip-mapped by octave, built
 * additively at start-up) read with a 32-bit phase accumulator and linear
 * interpolation. Filters are Zavalishin TPT state-variable filters, which stay
 * stable under fast modulation. Envelopes advance at control rate (blocks of
 * up to HUSH_CTRL samples) and are linearly interpolated inside the block. */
#ifndef HUSH_DSP_H
#define HUSH_DSP_H

#include <stdint.h>
#include "../core/hmath.h"

#define HUSH_CTRL 32          /* control block length in samples */
#define WT_BITS 11
#define WT_SIZE (1 << WT_BITS)
#define WT_MASK (WT_SIZE - 1)
#define WT_LEVELS 10          /* mip levels: 512, 256 ... 1 harmonics */
#define WT_FRAC_BITS (32 - WT_BITS)

enum {
    W_SINE = 0, W_TRI, W_SAW, W_SQUARE, W_PULSE, W_GLASS, W_VOX, W_ORGAN, W_HOLLOW, W_GRIT,
    W_NOISE, W_FM, W_COUNT
};
#define W_TABLED_FIRST W_TRI
#define W_TABLED_COUNT 9 /* tri saw square (pulse uses saw) glass vox organ hollow grit -> see dsp.c map */

extern const char *const hush_wave_names[W_COUNT];

typedef struct {
    float sr, inv_sr;
    float sine[WT_SIZE + 1];
} DspShared;

void dsp_init(float sample_rate);
float dsp_sr(void);
/* Returns the mip table for a wave whose fundamental is f Hz. Never NULL for tabled waves. */
const float *dsp_table(int wave, float freq);
const float *dsp_sine(void);
static inline float dsp_read(const float *t, uint32_t ph) {
    uint32_t i = ph >> WT_FRAC_BITS;
    float f = (float)(ph & ((1u << WT_FRAC_BITS) - 1)) * (1.0f / (float)(1u << WT_FRAC_BITS));
    float a = t[i];
    return a + (t[i + 1] - a) * f;
}
float dsp_midi_hz(float note);
uint32_t dsp_hz_inc(float hz);
/* Fast soft saturation, |out| < 1, approximately tanh for |x| < 2. */
static inline float dsp_sat(float x) {
    if (x > 3.0f) return 1.0f;
    if (x < -3.0f) return -1.0f;
    float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}
static inline uint32_t dsp_noise_u(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; *s = x; return x; }
static inline float dsp_noise(uint32_t *s) { return (float)(int32_t)dsp_noise_u(s) * (1.0f / 2147483648.0f); }

/* ---- State-variable filter ---- */
enum { F_LP = 0, F_BP, F_HP, F_NONE };
typedef struct { float ic1, ic2, a1, a2, a3, k; } Svf;
void svf_set(Svf *f, float hz, float res);
static inline float svf_lp(Svf *f, float x) {
    float v3 = x - f->ic2, v1 = f->a1 * f->ic1 + f->a2 * v3, v2 = f->ic2 + f->a2 * f->ic1 + f->a3 * v3;
    f->ic1 = 2.0f * v1 - f->ic1; f->ic2 = 2.0f * v2 - f->ic2; return v2;
}
static inline float svf_bp(Svf *f, float x) {
    float v3 = x - f->ic2, v1 = f->a1 * f->ic1 + f->a2 * v3, v2 = f->ic2 + f->a2 * f->ic1 + f->a3 * v3;
    f->ic1 = 2.0f * v1 - f->ic1; f->ic2 = 2.0f * v2 - f->ic2; return v1;
}
static inline float svf_hp(Svf *f, float x) {
    float v3 = x - f->ic2, v1 = f->a1 * f->ic1 + f->a2 * v3, v2 = f->ic2 + f->a2 * f->ic1 + f->a3 * v3;
    f->ic1 = 2.0f * v1 - f->ic1; f->ic2 = 2.0f * v2 - f->ic2; return x - f->k * v1 - v2;
}
static inline void svf_reset(Svf *f) { f->ic1 = f->ic2 = 0; }

/* ---- One-pole helpers ---- */
static inline float onepole_coef(float hz) { float c = 1.0f - expf(-H_TAU * hz / dsp_sr()); return h_clampf(c, 0.0f, 1.0f); }

/* ---- ADSR envelope (control rate) ---- */
enum { ENV_IDLE = 0, ENV_ATTACK, ENV_DECAY, ENV_SUSTAIN, ENV_RELEASE };
typedef struct {
    uint8_t stage;
    float val;
    float a_inc;     /* per-sample linear attack increment */
    float d_tau;     /* decay time constant (samples) */
    float sus;
    float r_tau;     /* release time constant (samples) */
} Env;
void env_set(Env *e, float a_s, float d_s, float sus, float r_s);
void env_gate_on(Env *e);
void env_gate_off(Env *e);
/* Advances n samples, returns the value at the end of the block. */
float env_advance(Env *e, int n);

#endif
