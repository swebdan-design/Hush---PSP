/* HUSH — wavetable construction, filters and envelopes. See dsp.h. */
#include "dsp.h"
#include <string.h>

const char *const hush_wave_names[W_COUNT] = {
    "SINE", "TRIANGLE", "SAW", "SQUARE", "PULSE", "GLASS", "VOX", "ORGAN", "HOLLOW", "GRIT", "NOISE", "FM"
};

/* Tabled shapes. W_PULSE is generated from two saw reads, so it has no table. */
enum { T_TRI = 0, T_SAW, T_SQUARE, T_GLASS, T_VOX, T_ORGAN, T_HOLLOW, T_GRIT, T_COUNT };
static const int8_t wave_to_table[W_COUNT] = { -1, T_TRI, T_SAW, T_SQUARE, T_SAW, T_GLASS, T_VOX, T_ORGAN, T_HOLLOW, T_GRIT, -1, -1 };

static float g_sr = 44100.0f, g_inv_sr = 1.0f / 44100.0f;
static float g_sine[WT_SIZE + 1];
static float g_tables[T_COUNT][WT_LEVELS][WT_SIZE + 1];
static int g_built = 0;

float dsp_sr(void) { return g_sr; }
const float *dsp_sine(void) { return g_sine; }

/* Harmonic amplitude (signed: negative = inverted phase) for each shape. */
static float harm_amp(int t, int h) {
    float fh = (float)h;
    switch (t) {
    case T_TRI: return (h & 1) ? (((h >> 1) & 1) ? -1.0f : 1.0f) / (fh * fh) : 0.0f;
    case T_SAW: return 1.0f / fh;
    case T_SQUARE: return (h & 1) ? 1.0f / fh : 0.0f;
    case T_GLASS: {
        /* sparse, bell-ish: fundamental, then a cluster around harmonics 5-9 */
        if (h == 1) return 1.0f;
        if (h == 2) return 0.18f;
        if (h >= 5 && h <= 9) return 0.32f / (1.0f + h_absf(fh - 7.0f));
        if (h == 13 || h == 17) return 0.09f;
        return 0.0f;
    }
    case T_VOX: {
        /* two soft formant humps (approx. 'oo'/'ah' blend) over a 1/h spectrum */
        float f1 = expf(-(fh - 3.0f) * (fh - 3.0f) / 2.5f);
        float f2 = expf(-(fh - 9.0f) * (fh - 9.0f) / 6.0f) * 0.45f;
        return (0.25f / fh + f1 + f2) * (h > 40 ? 0.0f : 1.0f);
    }
    case T_ORGAN: {
        static const float dr[] = {0, 1.0f, 0.7f, 0.45f, 0.5f, 0, 0.3f, 0, 0.25f};
        return h <= 8 ? dr[h] : 0.0f;
    }
    case T_HOLLOW: return (h & 1) ? (h % 3 == 0 ? 0.15f : 1.0f) / fh : 0.0f;
    case T_GRIT: return (1.0f / sqrtf(fh)) * (h > 64 ? 0.5f : 1.0f);
    }
    return 0;
}

/* Per-harmonic phase offsets (in table steps) for the GRIT shape: decorrelated partials
 * give a buzzy, industrial transient character without extra CPU at run time. */
static uint32_t harm_phase(int t, int h) {
    if (t != T_GRIT) return 0;
    uint32_t x = (uint32_t)h * 2654435761u;
    x ^= x >> 13;
    return x & WT_MASK;
}

static void build_table(int t) {
    float norm = 0;
    for (int lev = 0; lev < WT_LEVELS; lev++) {
        int hmax = 512 >> lev;
        float *dst = g_tables[t][lev];
        memset(dst, 0, sizeof(float) * (WT_SIZE + 1));
        for (int h = 1; h <= hmax; h++) {
            float a = harm_amp(t, h);
            if (a == 0.0f) continue;
            uint32_t off = harm_phase(t, h);
            for (int i = 0; i < WT_SIZE; i++) dst[i] += a * g_sine[((uint32_t)h * (uint32_t)i + off) & WT_MASK];
        }
        if (lev == 0) {
            for (int i = 0; i < WT_SIZE; i++) norm = h_maxf(norm, h_absf(dst[i]));
            norm = norm > 0 ? 0.92f / norm : 1.0f;
        }
        for (int i = 0; i < WT_SIZE; i++) dst[i] *= norm;
        dst[WT_SIZE] = dst[0];
    }
}

void dsp_init(float sample_rate) {
    g_sr = sample_rate;
    g_inv_sr = 1.0f / sample_rate;
    if (g_built) return;
    for (int i = 0; i <= WT_SIZE; i++) g_sine[i] = sinf(H_TAU * (float)i / (float)WT_SIZE);
    for (int t = 0; t < T_COUNT; t++) build_table(t);
    g_built = 1;
}

const float *dsp_table(int wave, float freq) {
    if (wave < 0 || wave >= W_COUNT || wave_to_table[wave] < 0) return g_sine;
    float allowed = 0.45f * g_sr / h_maxf(freq, 1.0f);
    int lev = 0;
    while (lev < WT_LEVELS - 1 && (float)(512 >> lev) > allowed) lev++;
    return g_tables[(int)wave_to_table[wave]][lev];
}

float dsp_midi_hz(float note) { return 440.0f * exp2f((note - 69.0f) * (1.0f / 12.0f)); }

uint32_t dsp_hz_inc(float hz) {
    float r = hz * g_inv_sr;
    if (r < 0) r = 0;
    if (r > 0.49f) r = 0.49f;
    return (uint32_t)(r * 4294967296.0f);
}

void svf_set(Svf *f, float hz, float res) {
    float fc = h_clampf(hz, 16.0f, 0.42f * g_sr);
    float g = tanf(H_PI * fc * g_inv_sr);
    float k = 2.0f - 2.0f * h_clampf(res, 0.0f, 0.97f);
    f->k = k;
    f->a1 = 1.0f / (1.0f + g * (g + k));
    f->a2 = g * f->a1;
    f->a3 = g * f->a2;
}

void env_set(Env *e, float a_s, float d_s, float sus, float r_s) {
    float sr = g_sr;
    e->a_inc = 1.0f / h_maxf(a_s * sr, 1.0f);
    e->d_tau = h_maxf(d_s * sr, 1.0f);
    e->sus = h_clampf(sus, 0.0f, 1.0f);
    e->r_tau = h_maxf(r_s * sr, 1.0f);
}

void env_gate_on(Env *e) { e->stage = ENV_ATTACK; }
void env_gate_off(Env *e) { if (e->stage != ENV_IDLE) e->stage = ENV_RELEASE; }

float env_advance(Env *e, int n) {
    switch (e->stage) {
    case ENV_IDLE: e->val = 0; break;
    case ENV_ATTACK:
        e->val += e->a_inc * (float)n;
        if (e->val >= 1.0f) { e->val = 1.0f; e->stage = ENV_DECAY; }
        break;
    case ENV_DECAY:
        e->val = e->sus + (e->val - e->sus) * expf(-(float)n / e->d_tau);
        if (e->val - e->sus < 0.001f) { e->val = e->sus; e->stage = ENV_SUSTAIN; }
        if (e->sus <= 0.0f && e->val < 0.0005f) { e->val = 0; e->stage = ENV_IDLE; }
        break;
    case ENV_SUSTAIN: e->val = e->sus; if (e->sus <= 0.0f) e->stage = ENV_IDLE; break;
    case ENV_RELEASE:
        e->val *= expf(-(float)n / e->r_tau);
        if (e->val < 0.0003f) { e->val = 0; e->stage = ENV_IDLE; }
        break;
    }
    return e->val;
}
