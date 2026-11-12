/* HUSH — effects implementation. See fx.h. */
#include "fx.h"
#include <string.h>

#define DENORM 1e-18f

static inline float rd_interp(const float *buf, uint32_t mask, float pos) {
    /* pos is a non-negative fractional index */
    uint32_t i = (uint32_t)pos;
    float f = pos - (float)i;
    float a = buf[i & mask], b = buf[(i + 1) & mask];
    return a + (b - a) * f;
}

/* ---------------- Dub delay ---------------- */
void delay_init(DubDelay *d) {
    memset(d, 0, sizeof(*d));
    d->time_cur = d->time_target = 0.375f * dsp_sr();
    d->fb = 0.4f;
    d->lp_coef = onepole_coef(2800.0f);
    d->hp_coef = onepole_coef(140.0f);
}

void delay_process(DubDelay *d, const float *inL, const float *inR, float *outL, float *outR, float *revSend, float rev_amt, int n) {
    const float sr = dsp_sr();
    float maxlen = (float)(DLY_SIZE - 4);
    float tt = h_clampf(d->time_target, 16.0f, maxlen - sr * 0.01f);
    /* slow approach: changing tempo produces a gentle tape-style pitch bend instead of a click */
    float k = 1.0f - expf(-(float)n / (0.6f * sr));
    float t0 = d->time_cur;
    d->time_cur += (tt - d->time_cur) * k;
    float dtime = (d->time_cur - t0) / (float)n;
    d->wow_ph += 0.31f * (float)n / sr;
    if (d->wow_ph > 1.0f) d->wow_ph -= 1.0f;
    float wow = sinf(d->wow_ph * H_TAU) * d->wow_depth * sr * 0.002f;
    float fb = h_clampf(d->fb, 0.0f, 0.92f);
    float lpc = d->lp_coef, hpc = d->hp_coef;
    float t = t0;
    for (int i = 0; i < n; i++) {
        float pos = (float)d->w + (float)DLY_SIZE - (t + wow);
        float yl = rd_interp(d->bufL, DLY_MASK, pos);
        float yr = rd_interp(d->bufR, DLY_MASK, pos);
        /* feedback filtering: low-pass then high-pass (one-pole each) */
        d->lpL += (yl - d->lpL) * lpc; d->lpR += (yr - d->lpR) * lpc;
        d->hpL += (d->lpL - d->hpL) * hpc; d->hpR += (d->lpR - d->hpR) * hpc;
        float fl = d->lpL - d->hpL + DENORM, fr = d->lpR - d->hpR + DENORM;
        /* ping-pong: each side feeds the other */
        d->bufL[d->w] = dsp_sat(inL[i] + fr * fb);
        d->bufR[d->w] = dsp_sat(inR[i] + fl * fb);
        d->w = (d->w + 1) & DLY_MASK;
        outL[i] += fl; outR[i] += fr;
        revSend[i] += (fl + fr) * rev_amt;
        t += dtime;
    }
}

/* ---------------- FDN reverb ---------------- */
static const int rev_base[REV_LINES] = { 2213, 2617, 3109, 3557 };
static const int ap_base[2] = { 263, 607 };

void reverb_init(Reverb *r) {
    memset(r, 0, sizeof(*r));
    reverb_set(r, 1.0f, 4.0f, 0.5f);
}

void reverb_set(Reverb *r, float size, float decay_s, float damp) {
    float scale = dsp_sr() / 44100.0f;
    r->size = size;
    r->decay = decay_s;
    for (int i = 0; i < REV_LINES; i++) {
        int L = (int)((float)rev_base[i] * h_clampf(size, 0.3f, 2.2f) * scale);
        r->len[i] = h_clampi(L, 64, REV_MAX - 1);
        r->g[i] = powf(10.0f, -3.0f * (float)r->len[i] / (h_maxf(decay_s, 0.2f) * dsp_sr()));
    }
    for (int i = 0; i < 2; i++) r->ap_len[i] = h_clampi((int)((float)ap_base[i] * scale), 16, AP_MAX - 1);
    r->damp_coef = h_clampf(1.0f - damp, 0.05f, 1.0f) * 0.6f + 0.05f;
}

void reverb_process(Reverb *r, const float *in, float *outL, float *outR, float wet, int n) {
    const float dc = r->damp_coef;
    for (int i = 0; i < n; i++) {
        float x = in[i] * 0.35f;
        /* input diffusion */
        for (int a = 0; a < 2; a++) {
            int idx = r->ap_w[a];
            float buf = r->ap[a][idx];
            float y = -0.6f * x + buf;
            r->ap[a][idx] = x + 0.6f * y;
            r->ap_w[a] = (idx + 1 >= r->ap_len[a]) ? 0 : idx + 1;
            x = y;
        }
        float o[REV_LINES];
        for (int k = 0; k < REV_LINES; k++) {
            uint32_t rp = (r->w + REV_MAX - (uint32_t)r->len[k]) & (REV_MAX - 1);
            float v = r->line[k][rp];
            r->damp[k] += (v - r->damp[k]) * dc;
            o[k] = r->damp[k] * r->g[k];
        }
        /* 4x4 Hadamard (scaled) feedback matrix */
        float h0 = (o[0] + o[1] + o[2] + o[3]) * 0.5f;
        float h1 = (o[0] - o[1] + o[2] - o[3]) * 0.5f;
        float h2 = (o[0] + o[1] - o[2] - o[3]) * 0.5f;
        float h3 = (o[0] - o[1] - o[2] + o[3]) * 0.5f;
        r->line[0][r->w] = x + h0 + DENORM;
        r->line[1][r->w] = x + h1 + DENORM;
        r->line[2][r->w] = h2 - x * 0.5f;
        r->line[3][r->w] = h3 + x * 0.5f;
        r->w = (r->w + 1) & (REV_MAX - 1);
        outL[i] += (o[0] + o[2] * 0.6f) * wet;
        outR[i] += (o[1] + o[3] * 0.6f) * wet;
    }
}

/* ---------------- Chorus ---------------- */
void chorus_init(Chorus *c) { memset(c, 0, sizeof(*c)); }

void chorus_process(Chorus *c, float *L, float *R, float rate, float depth, float mix, int n) {
    const float sr = dsp_sr();
    float base = 0.011f * sr, dep = 0.004f * sr * depth;
    float ph0 = c->ph;
    c->ph += rate * (float)n / sr;
    if (c->ph > 1.0f) c->ph -= 1.0f;
    float m0l = sinf(ph0 * H_TAU), m1l = sinf(c->ph * H_TAU);
    float m0r = cosf(ph0 * H_TAU), m1r = cosf(c->ph * H_TAU);
    float inv = 1.0f / (float)n;
    for (int i = 0; i < n; i++) {
        float t = (float)i * inv;
        float dl = base + dep * (m0l + (m1l - m0l) * t);
        float dr = base + dep * (m0r + (m1r - m0r) * t);
        c->bufL[c->w] = L[i];
        c->bufR[c->w] = R[i];
        float yl = rd_interp(c->bufL, CH_SIZE - 1, (float)c->w + CH_SIZE - dl);
        float yr = rd_interp(c->bufR, CH_SIZE - 1, (float)c->w + CH_SIZE - dr);
        c->w = (c->w + 1) & (CH_SIZE - 1);
        L[i] = L[i] * (1.0f - mix * 0.5f) + yr * mix;  /* cross-feed widens the image */
        R[i] = R[i] * (1.0f - mix * 0.5f) + yl * mix;
    }
}

/* ---------------- Radio degradation ---------------- */
void radio_init(Radio *r) {
    memset(r, 0, sizeof(*r));
    r->noise = 0x1234567u;
    svf_set(&r->hpL, 380.0f, 0.3f); svf_set(&r->hpR, 380.0f, 0.3f);
    svf_set(&r->lpL, 3200.0f, 0.4f); svf_set(&r->lpR, 3200.0f, 0.4f);
}

void radio_process(Radio *r, float *L, float *R, float amount, int n) {
    if (amount <= 0.001f) return;
    float a = h_clampf(amount, 0.0f, 1.0f);
    float levels = 4096.0f * expf(-a * 6.0f) + 6.0f;   /* ~12 bit .. ~3 bit */
    float inv_levels = 1.0f / levels;
    float hold = 1.0f + a * 5.0f;                      /* sample-rate reduction factor */
    float band = h_minf(a * 1.6f, 1.0f);
    float crackle_p = a * a * 0.0009f;
    for (int i = 0; i < n; i++) {
        float xl = L[i], xr = R[i];
        float bl = svf_lp(&r->lpL, svf_hp(&r->hpL, xl));
        float br = svf_lp(&r->lpR, svf_hp(&r->hpR, xr));
        xl += (bl * 1.4f - xl) * band;
        xr += (br * 1.4f - xr) * band;
        r->cnt += 1.0f;
        if (r->cnt >= hold) {
            r->cnt -= hold;
            r->holdL = dsp_floor(xl * levels + 0.5f) * inv_levels;
            r->holdR = dsp_floor(xr * levels + 0.5f) * inv_levels;
            /* the saturator only needs re-evaluating when the held value changes (it divides) */
            r->satL = dsp_sat(r->holdL * 1.3f);
            r->satR = dsp_sat(r->holdR * 1.3f);
        }
        float nz = dsp_noise(&r->noise);
        if ((float)(dsp_noise_u(&r->noise) >> 8) * (1.0f / 16777216.0f) < crackle_p) r->crackle = 0.3f * a * h_signf(nz);
        r->crackle *= 0.82f;
        L[i] = r->satL + r->crackle + nz * 0.004f * a;
        R[i] = r->satR + r->crackle * 0.7f + nz * 0.004f * a;
    }
}

/* ---------------- Master ---------------- */
void master_init(Master *m) { memset(m, 0, sizeof(*m)); m->noise = 0xACE1u; }

void master_process(Master *m, float *L, float *R, float gain, float hiss, int n) {
    const float R_DC = 0.9985f;
    for (int i = 0; i < n; i++) {
        float hz = 0;
        if (hiss > 0.0f) {
            float w = dsp_noise(&m->noise);
            m->hiss_lp += (w - m->hiss_lp) * 0.35f;
            hz = m->hiss_lp * hiss;
        }
        float xl = L[i] + hz, xr = R[i] + hz * 0.9f;
        float yl = xl - m->dcxL + R_DC * m->dcyL; m->dcxL = xl; m->dcyL = yl + DENORM;
        float yr = xr - m->dcxR + R_DC * m->dcyR; m->dcxR = xr; m->dcyR = yr + DENORM;
        /* soft limiter: linear below ~-6 dBFS, smooth saturation above, ceiling ~-0.5 dBFS */
        yl *= gain; yr *= gain;
        float al = h_absf(yl), ar = h_absf(yr);
        if (al > 0.5f) yl = h_signf(yl) * (0.5f + 0.44f * dsp_sat((al - 0.5f) * 2.2f));
        if (ar > 0.5f) yr = h_signf(yr) * (0.5f + 0.44f * dsp_sat((ar - 0.5f) * 2.2f));
        L[i] = yl; R[i] = yr;
    }
}
