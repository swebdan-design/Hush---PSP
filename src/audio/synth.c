/* HUSH — tone and percussion voice rendering. See synth.h. */
#include "synth.h"
#include <string.h>

const char *const hush_drum_names[DR_COUNT] = { "KICK", "SNARE", "HAT", "OPEN HAT", "METAL", "CLICK", "THUD", "RIM" };

void voice_start(Voice *v, const Patch *p, float note, float vel, float pan, uint8_t layer, uint32_t noise_seed) {
    int was_active = v->active && !v->released;
    int was_sounding = v->active;
    float old_pitch = v->pitch, old_amp = v->amp.val, old_gain = v->gain_cur;
    Svf old_f = v->f;
    memset(v, 0, sizeof(*v));
    v->p = *p;
    v->active = 1;
    v->layer = layer;
    v->bus = p->bus;
    v->note_id = (int16_t)(note + 0.5f);
    v->pitch_target = note;
    v->pitch = (p->glide > 0 && was_active) ? old_pitch : note;
    v->glide_coef = p->glide > 0 ? 1.0f - expf(-(float)HUSH_CTRL / (p->glide * dsp_sr())) : 1.0f;
    v->vel = vel;
    float a = (h_clampf(pan, -1.0f, 1.0f) + 1.0f) * 0.25f * H_PI;
    v->pan_l = cosf(a);
    v->pan_r = sinf(a);
    v->noise = noise_seed | 1u;
    v->ph2 = noise_seed * 2654435761u; /* decorrelate osc2 start phase deterministically */
    v->lfo_ph = (float)(noise_seed & 1023) * (1.0f / 1024.0f);
    v->mix_cur = p->mix2;
    env_set(&v->amp, p->a, p->d, p->s, p->r);
    env_set(&v->fenv, p->fa, p->fd, p->fs, p->fr);
    env_gate_on(&v->amp);
    env_gate_on(&v->fenv);
    if (was_sounding) {
        /* graceful retrigger / steal: continue from the current level instead of jumping to zero */
        v->amp.val = old_amp * (old_gain > 0 ? old_gain / h_maxf(vel, 1e-4f) : 0.0f);
        if (v->amp.val > 1.0f) v->amp.val = 1.0f;
        v->gain_cur = vel;
        v->f = old_f;
    }
}

void voice_release(Voice *v) {
    if (!v->active) return;
    v->released = 1;
    env_gate_off(&v->amp);
    env_gate_off(&v->fenv);
}

void voice_render(Voice *v, int n, float layer_gain, float *outL, float *outR,
                  float *sendDL, float *sendDR, float *sendR, float cut_offset) {
    if (!v->active) return;
    const Patch *p = &v->p;
    const float sr = dsp_sr();

    /* control-rate updates */
    v->pitch += (v->pitch_target - v->pitch) * v->glide_coef;
    v->lfo_ph += p->lfo_rate * (float)n / sr;
    if (v->lfo_ph >= 1.0f) v->lfo_ph -= floorf(v->lfo_ph);
    float lfo = dsp_read(dsp_sine(), (uint32_t)(v->lfo_ph * 4294967040.0f));
    float pitch = v->pitch + (p->lfo_pitch * lfo + v->pitch_drift) * 0.01f;
    float f1 = dsp_midi_hz(pitch);
    float f2 = dsp_midi_hz(pitch + p->detune2);
    v->inc1 = dsp_hz_inc(f1);
    v->inc2 = dsp_hz_inc(p->wave1 == W_FM ? f1 * p->fm_ratio : f2);
    v->t1 = dsp_table(p->wave1, f1);
    v->t2 = dsp_table(p->wave2, f2);

    float a0 = v->amp.val;
    float a1 = env_advance(&v->amp, n);
    float fe = env_advance(&v->fenv, n);
    if (v->amp.stage == ENV_IDLE) { v->active = 0; v->level_meter = 0; return; }

    float cut = p->cutoff + p->key_track * (pitch - 60.0f) + p->fenv_amt * fe + p->lfo_cut * lfo + cut_offset;
    if (p->ftype != F_NONE) svf_set(&v->f, dsp_midi_hz(h_clampf(cut, 12.0f, 135.0f)), p->res);
    float mix = h_clampf(p->mix2 + p->lfo_morph * lfo, 0.0f, 1.0f);
    float mix0 = v->mix_cur, dmix = (mix - mix0) / (float)n;
    v->mix_cur = mix;

    float g_target = v->vel * layer_gain;
    float g0 = v->gain_cur * a0, g1 = g_target * a1;
    v->gain_cur = g_target;
    float dg = (g1 - g0) / (float)n;

    float pw = h_clampf(p->pw + p->lfo_pwm * lfo, 0.05f, 0.95f);
    uint32_t pwoff = (uint32_t)(pw * 4294967040.0f);
    float fm_idx = p->fm_index * (0.25f + 0.75f * fe);
    const float *sine = dsp_sine();
    const int w1 = p->wave1, w2 = p->wave2;
    const int use2 = (p->mix2 > 0.0f || p->lfo_morph > 0.0f) && w1 != W_FM;
    const float noise_mix = p->noise;
    const float drive = p->drive, drive_in = 1.0f + drive * 4.0f, drive_out = 1.0f / (1.0f + drive * 0.8f);
    const float pl = v->pan_l, pr = v->pan_r, sd = p->send_delay, sv = p->send_reverb;
    const int ft = p->ftype;
    uint32_t ph1 = v->ph1, ph2 = v->ph2, inc1 = v->inc1, inc2 = v->inc2;
    float g = g0, m = mix0, peak = 0;

    for (int i = 0; i < n; i++) {
        float s1, s2 = 0;
        if (w1 == W_FM) {
            float mod = dsp_read(sine, ph2) * fm_idx;
            float ph = (float)(ph1 >> 8) * (1.0f / 16777216.0f) + mod;
            ph -= dsp_floor(ph);
            uint32_t pi = (uint32_t)(ph * 4294967040.0f);
            s1 = dsp_read(sine, pi);
        } else if (w1 == W_PULSE) {
            s1 = (dsp_read(v->t1, ph1) - dsp_read(v->t1, ph1 + pwoff)) * 0.7f;
        } else if (w1 == W_NOISE) {
            s1 = dsp_noise(&v->noise);
        } else {
            s1 = dsp_read(v->t1, ph1);
        }
        if (use2) {
            if (w2 == W_PULSE) s2 = (dsp_read(v->t2, ph2) - dsp_read(v->t2, ph2 + pwoff)) * 0.7f;
            else if (w2 == W_NOISE) s2 = dsp_noise(&v->noise);
            else s2 = dsp_read(v->t2, ph2);
        }
        ph1 += inc1; ph2 += inc2;
        float x = s1 + (s2 - s1) * m;
        if (noise_mix > 0.0f) x += dsp_noise(&v->noise) * noise_mix;
        if (ft == F_LP) x = svf_lp(&v->f, x);
        else if (ft == F_BP) x = svf_bp(&v->f, x) * 1.6f;
        else if (ft == F_HP) x = svf_hp(&v->f, x);
        if (drive > 0.0f) x = dsp_sat(x * drive_in) * drive_out;
        x *= g;
        g += dg; m += dmix;
        float ax = h_absf(x);
        if (ax > peak) peak = ax;
        float l = x * pl, r = x * pr;
        outL[i] += l; outR[i] += r;
        if (sd != 0.0f) { sendDL[i] += l * sd; sendDR[i] += r * sd; }
        if (sv != 0.0f) sendR[i] += (l + r) * sv;
    }
    v->ph1 = ph1; v->ph2 = ph2;
    v->level_meter = v->level_meter * 0.9f + peak * 0.1f;
    v->age += (uint32_t)n;
}

/* ---------------- Percussion ---------------- */

static const float hat_ratios[6] = { 1.0f, 1.4827f, 1.8005f, 2.5463f, 2.6304f, 3.8969f };

void drum_start(DrumVoice *d, int kind, float vel, float pan, float decay, float tone, uint32_t noise_seed) {
    memset(d, 0, sizeof(*d));
    d->active = 1;
    d->kind = (uint8_t)kind;
    d->vel = vel;
    d->decay = h_maxf(decay, 0.05f);
    d->tone = tone;
    d->noise = noise_seed | 1u;
    float a = (h_clampf(pan, -1.0f, 1.0f) + 1.0f) * 0.25f * H_PI;
    d->pan_l = cosf(a);
    d->pan_r = sinf(a);
    d->amp = 1.0f;
    switch (kind) {
    case DR_SNARE: svf_set(&d->f, 1500.0f + 1200.0f * tone, 0.35f); d->send_reverb = 0.25f; d->send_delay = 0.12f; break;
    case DR_HAT: case DR_OPENHAT: svf_set(&d->f, h_minf(7200.0f + 2000.0f * tone, 0.4f * dsp_sr()), 0.2f);
        svf_set(&d->f2, h_minf(9000.0f, 0.41f * dsp_sr()), 0.5f); d->send_delay = 0.18f; d->send_reverb = 0.08f; break;
    case DR_METAL: svf_set(&d->f, 900.0f + 2500.0f * tone, 0.75f); d->send_reverb = 0.55f; d->send_delay = 0.3f; break;
    case DR_CLICK: svf_set(&d->f, 2500.0f + 3000.0f * tone, 0.6f); d->send_delay = 0.35f; d->send_reverb = 0.1f; break;
    case DR_THUD: svf_set(&d->f, 180.0f, 0.2f); d->send_reverb = 0.6f; d->send_delay = 0.15f; break;
    case DR_RIM: svf_set(&d->f, 1800.0f + 1500.0f * tone, 0.7f); d->send_delay = 0.4f; d->send_reverb = 0.15f; break;
    case DR_KICK: default: svf_set(&d->f, 3000.0f, 0.1f); d->send_reverb = 0.06f; break;
    }
}

void drum_render(DrumVoice *d, int n, float gain, float *outL, float *outR, float *sendDL, float *sendDR, float *sendR) {
    if (!d->active) return;
    const float sr = dsp_sr(), dt = (float)n / sr;
    float t0 = d->t, t1 = d->t + dt;
    d->t = t1;
    float tau, a0, a1;
    const float *sine = dsp_sine();
    switch (d->kind) {
    case DR_KICK: tau = 0.22f * d->decay; break;
    case DR_SNARE: tau = 0.09f * d->decay; break;
    case DR_HAT: tau = 0.025f * d->decay; break;
    case DR_OPENHAT: tau = 0.16f * d->decay; break;
    case DR_METAL: tau = 0.35f * d->decay; break;
    case DR_CLICK: tau = 0.006f * d->decay; break;
    case DR_THUD: tau = 0.45f * d->decay; break;
    case DR_RIM: default: tau = 0.018f * d->decay; break;
    }
    a0 = d->vel * gain * expf(-t0 / tau);
    a1 = d->vel * gain * expf(-t1 / tau);
    if (t0 > tau * 9.0f) { d->active = 0; d->level_meter = 0; return; }
    /* short linear attack avoids clicks on everything but the click itself */
    float atk0 = h_minf(t0 / 0.0015f, 1.0f), atk1 = h_minf(t1 / 0.0015f, 1.0f);
    if (d->kind != DR_CLICK) { a0 *= atk0; a1 *= atk1; }
    float da = (a1 - a0) / (float)n, a = a0;
    float pl = d->pan_l, pr = d->pan_r, peak = 0;

    switch (d->kind) {
    case DR_KICK: case DR_THUD: {
        float base = d->kind == DR_KICK ? 44.0f + 14.0f * d->tone : 38.0f + 10.0f * d->tone;
        float sweep = d->kind == DR_KICK ? 150.0f : 50.0f;
        float tp = d->kind == DR_KICK ? 0.035f : 0.12f;
        uint32_t inc0 = dsp_hz_inc(base + sweep * expf(-t0 / tp));
        uint32_t inc1 = dsp_hz_inc(base + sweep * expf(-t1 / tp));
        int32_t dinc = ((int32_t)inc1 - (int32_t)inc0) / n;
        uint32_t inc = inc0;
        float click = d->kind == DR_KICK ? (t0 < 0.004f ? 0.35f : 0.0f) : 0.12f;
        for (int i = 0; i < n; i++) {
            float x = dsp_read(sine, d->ph);
            d->ph += inc; inc += (uint32_t)dinc;
            if (click > 0) x += svf_lp(&d->f, dsp_noise(&d->noise)) * click;
            x = dsp_sat(x * 1.3f) * a; a += da;
            float ax = h_absf(x); if (ax > peak) peak = ax;
            float l = x * pl, r = x * pr;
            outL[i] += l; outR[i] += r; sendR[i] += (l + r) * d->send_reverb; sendDL[i] += l * d->send_delay; sendDR[i] += r * d->send_delay;
        }
        break;
    }
    case DR_SNARE: case DR_RIM: {
        uint32_t inc = dsp_hz_inc(d->kind == DR_SNARE ? 185.0f + 40.0f * d->tone : 820.0f + 200.0f * d->tone);
        float tone_amt = d->kind == DR_SNARE ? 0.55f * expf(-t0 / 0.03f) : 0.6f;
        for (int i = 0; i < n; i++) {
            float x = dsp_read(sine, d->ph) * tone_amt + svf_bp(&d->f, dsp_noise(&d->noise)) * 1.4f;
            d->ph += inc;
            x *= a; a += da;
            float ax = h_absf(x); if (ax > peak) peak = ax;
            float l = x * pl, r = x * pr;
            outL[i] += l; outR[i] += r; sendR[i] += (l + r) * d->send_reverb; sendDL[i] += l * d->send_delay; sendDR[i] += r * d->send_delay;
        }
        break;
    }
    case DR_HAT: case DR_OPENHAT: {
        uint32_t incs[6];
        float fb = 300.0f + 160.0f * d->tone;
        for (int k = 0; k < 6; k++) incs[k] = dsp_hz_inc(fb * hat_ratios[k]);
        for (int i = 0; i < n; i++) {
            float m = 0;
            for (int k = 0; k < 6; k++) { d->ph3[k] += incs[k]; m += (d->ph3[k] & 0x80000000u) ? 1.0f : -1.0f; }
            float x = m * 0.12f + dsp_noise(&d->noise) * 0.35f;
            x = svf_hp(&d->f, x);
            x = svf_bp(&d->f2, x) * 0.6f + x * 0.7f;
            x *= a; a += da;
            float ax = h_absf(x); if (ax > peak) peak = ax;
            float l = x * pl, r = x * pr;
            outL[i] += l; outR[i] += r; sendR[i] += (l + r) * d->send_reverb; sendDL[i] += l * d->send_delay; sendDR[i] += r * d->send_delay;
        }
        break;
    }
    case DR_METAL: {
        float fc = 210.0f + 340.0f * d->tone;
        uint32_t ic = dsp_hz_inc(fc), im = dsp_hz_inc(fc * 1.4142f), im2 = dsp_hz_inc(fc * 2.756f);
        float idx = 1.8f * expf(-t0 / 0.12f) + 0.35f;
        for (int i = 0; i < n; i++) {
            float mod = dsp_read(sine, d->ph2) * idx + dsp_read(sine, d->ph3[0]) * 0.4f;
            float ph = (float)(d->ph >> 8) * (1.0f / 16777216.0f) + mod;
            ph -= floorf(ph);
            float x = dsp_read(sine, (uint32_t)(ph * 4294967040.0f));
            x = x * 0.6f + svf_bp(&d->f, x) * 0.8f;
            d->ph += ic; d->ph2 += im; d->ph3[0] += im2;
            x *= a; a += da;
            float ax = h_absf(x); if (ax > peak) peak = ax;
            float l = x * pl, r = x * pr;
            outL[i] += l; outR[i] += r; sendR[i] += (l + r) * d->send_reverb; sendDL[i] += l * d->send_delay; sendDR[i] += r * d->send_delay;
        }
        break;
    }
    case DR_CLICK: default: {
        for (int i = 0; i < n; i++) {
            float x = svf_bp(&d->f, dsp_noise(&d->noise)) * 2.0f;
            x *= a; a += da;
            float ax = h_absf(x); if (ax > peak) peak = ax;
            float l = x * pl, r = x * pr;
            outL[i] += l; outR[i] += r; sendR[i] += (l + r) * d->send_reverb; sendDL[i] += l * d->send_delay; sendDR[i] += r * d->send_delay;
        }
        break;
    }
    }
    d->level_meter = d->level_meter * 0.85f + peak * 0.15f;
}
