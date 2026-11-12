/* HUSH — sound effects synthesis. */
#include "sfx.h"
#include "dsp.h"
#include <string.h>

void sfx_init(Sfx *s, float sr) {
    memset(s, 0, sizeof(*s));
    s->sr = sr;
    s->master = 0.7f;
    s->noise = 0x9E3779B9u;
    s->key = 9;
}

void sfx_play(Sfx *s, int type) {
    uint32_t h = s->req_head;
    s->req[h & 15] = type;
#if defined(__GNUC__)
    __asm__ __volatile__("" ::: "memory");
#endif
    s->req_head = h + 1;
}

static const float shot_dur[SFX_COUNT] = { 0.06f, 0.12f, 0.1f, 0.7f, 0.45f, 0.6f, 0.5f, 0.35f, 0.6f, 2.5f, 1.2f, 2.0f };

static void start_shot(Sfx *s, int type) {
    int best = 0;
    float bt = -1;
    for (int i = 0; i < SFX_SHOTS; i++) {
        if (s->shots[i].dur <= 0) { best = i; break; }
        float left = s->shots[i].t / s->shots[i].dur;
        if (left > bt) { bt = left; best = i; }
    }
    SfxShot *x = &s->shots[best];
    memset(x, 0, sizeof(*x));
    x->type = type;
    x->dur = shot_dur[type];
    x->gain = 1.0f;
    x->noise = 0x1234567u + (uint32_t)type * 7919u;
}

static float shot_sample(Sfx *s, SfxShot *x, float *pan) {
    const float *sine = dsp_sine();
    float t = x->t, d = x->dur, k = t / d;
    float env, f, y = 0;
    *pan = 0;
    switch (x->type) {
    case SFX_UI_MOVE:
        env = 1.0f - k; f = 1800.0f;
        x->ph += dsp_hz_inc(f); y = dsp_read(sine, x->ph) * env * env * 0.12f; break;
    case SFX_UI_SELECT:
        env = 1.0f - k; f = k < 0.5f ? 880.0f : 1320.0f;
        x->ph += dsp_hz_inc(f); y = dsp_read(sine, x->ph) * env * 0.14f; break;
    case SFX_UI_BACK:
        env = 1.0f - k; f = 660.0f - 300.0f * k;
        x->ph += dsp_hz_inc(f); y = dsp_read(sine, x->ph) * env * 0.12f; break;
    case SFX_SCAN_DONE: {
        env = k < 0.05f ? k / 0.05f : (1.0f - k);
        float f1 = dsp_midi_hz((float)(72 + s->key % 12)), f2 = f1 * 1.5f;
        x->ph += dsp_hz_inc(f1); x->ph2 += dsp_hz_inc(k > 0.25f ? f2 : f1 * 2.0f);
        y = (dsp_read(sine, x->ph) * 0.6f + dsp_read(sine, x->ph2) * 0.4f) * env * 0.16f; break;
    }
    case SFX_IMPACT:
        env = (1.0f - k); env *= env * env;
        x->ph += dsp_hz_inc(70.0f - 30.0f * k);
        y = (dsp_read(sine, x->ph) * 0.7f + dsp_noise(&x->noise) * 0.4f * (1.0f - k)) * env * 0.5f; break;
    case SFX_EMP: {
        env = 1.0f - k;
        float fm = dsp_read(sine, x->ph2) * 6.0f;
        x->ph2 += dsp_hz_inc(31.0f);
        x->ph += dsp_hz_inc(h_maxf(60.0f, 1400.0f * (1.0f - k) + fm * 100.0f));
        y = (dsp_read(sine, x->ph) > 0 ? 1.0f : -1.0f) * env * env * 0.12f + dsp_noise(&x->noise) * env * 0.06f; break;
    }
    case SFX_COLLECT: {
        env = 1.0f - k;
        float n = (float)(76 + s->key % 12) + (k < 0.33f ? 0.0f : (k < 0.66f ? 7.0f : 12.0f));
        x->ph += dsp_hz_inc(dsp_midi_hz(n));
        y = dsp_read(sine, x->ph) * env * 0.14f; *pan = 0.3f; break;
    }
    case SFX_TRANSMISSION:
        env = k < 0.1f ? k / 0.1f : 1.0f - k;
        y = dsp_noise(&x->noise) * env * 0.05f;
        x->ph += dsp_hz_inc(2400.0f); if ((x->ph >> 28) & 1) y *= 0.3f; break;
    case SFX_BOOST:
        env = k < 0.2f ? k / 0.2f : 1.0f - k;
        x->ph += dsp_hz_inc(120.0f + 300.0f * k);
        y = (dsp_noise(&x->noise) * 0.5f + dsp_read(sine, x->ph) * 0.5f) * env * 0.12f; break;
    case SFX_DISCOVERY: {
        env = k < 0.1f ? k / 0.1f : (1.0f - k);
        float f1 = dsp_midi_hz((float)(60 + s->key % 12)), f2 = dsp_midi_hz((float)(67 + s->key % 12));
        x->ph += dsp_hz_inc(f1); x->ph2 += dsp_hz_inc(f2 * 1.003f);
        y = (dsp_read(sine, x->ph) + dsp_read(sine, x->ph2)) * env * 0.08f; *pan = -0.2f; break;
    }
    case SFX_FAIL:
        env = 1.0f - k;
        x->ph += dsp_hz_inc(220.0f - 110.0f * k);
        y = (dsp_read(sine, x->ph) > 0.2f ? 1.0f : -1.0f) * env * 0.06f; break;
    case SFX_COMPLETE: default: {
        env = k < 0.05f ? k / 0.05f : (1.0f - k);
        float f1 = dsp_midi_hz((float)(64 + s->key % 12) + (k > 0.3f ? 5.0f : 0.0f));
        x->ph += dsp_hz_inc(f1); x->ph2 += dsp_hz_inc(f1 * 2.0f);
        y = (dsp_read(sine, x->ph) * 0.7f + dsp_read(sine, x->ph2) * 0.3f) * env * 0.12f; break;
    }
    }
    x->t += 1.0f / s->sr;
    if (x->t >= x->dur) x->dur = 0;
    return y;
}

void sfx_render(Sfx *s, float *out, int n) {
    while (s->req_tail != s->req_head) { start_shot(s, s->req[s->req_tail & 15]); s->req_tail++; }
    const float *sine = dsp_sine();
    float k = 1.0f - expf(-(float)n / (0.08f * s->sr));
    s->eng_s += (h_clampf(s->engine, 0.0f, 1.0f) - s->eng_s) * k;
    s->wind_s += (h_clampf(s->wind, 0.0f, 1.0f) - s->wind_s) * k;
    s->scan_s += (h_clampf(s->scan, 0.0f, 1.0f) - s->scan_s) * k;
    s->pitch_s += (s->engine_pitch - s->pitch_s) * k * 0.5f;
    /* engine: three detuned partials around the key root, slightly rising with thrust */
    float root = (float)(36 + ((s->key % 12) + 12) % 12) + s->pitch_s;
    uint32_t i1 = dsp_hz_inc(dsp_midi_hz(root)), i2 = dsp_hz_inc(dsp_midi_hz(root + 12.03f)), i3 = dsp_hz_inc(dsp_midi_hz(root + 19.0f));
    uint32_t is = dsp_hz_inc(dsp_midi_hz((float)(84 + ((s->key % 12) + 12) % 12)));
    float eg = s->eng_s * 0.07f, wg = s->wind_s * 0.09f, sg = s->scan_s * 0.035f;
    float wc = 0.02f + 0.25f * s->wind_s;
    float m = s->master;
    for (int i = 0; i < n; i++) {
        float e = 0;
        if (eg > 0.0005f) {
            s->eph1 += i1; s->eph2 += i2; s->eph3 += i3;
            e = (dsp_read(sine, s->eph1) + 0.5f * dsp_read(sine, s->eph2) + 0.18f * (dsp_read(sine, s->eph3))) * eg;
        }
        float w = 0;
        if (wg > 0.0005f) {
            float nz = dsp_noise(&s->noise);
            s->wlp += (nz - s->wlp) * wc;
            s->whp += (s->wlp - s->whp) * 0.01f;
            w = (s->wlp - s->whp) * wg * 2.0f;
        }
        float sc = 0;
        if (sg > 0.0005f) {
            s->sph += is;
            s->scan_lfo += 6.0f / s->sr;
            if (s->scan_lfo > 1.0f) s->scan_lfo -= 1.0f;
            sc = dsp_read(sine, s->sph) * sg * (s->scan_lfo < 0.15f ? 1.0f : 0.15f);
        }
        float L = e + w * 0.8f + sc, R = e + w + sc;
        for (int k2 = 0; k2 < SFX_SHOTS; k2++) {
            SfxShot *x = &s->shots[k2];
            if (x->dur <= 0) continue;
            float pan;
            float y = shot_sample(s, x, &pan);
            L += y * (1.0f - pan); R += y * (1.0f + pan);
        }
        out[2 * i] += L * m;
        out[2 * i + 1] += R * m;
    }
}
