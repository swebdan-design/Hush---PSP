/* HUSH — real-time engine: mailbox, voice management, scheduling, mixing, effects, metering. */
#include "music_internal.h"
#include <string.h>

#if defined(__GNUC__)
#define HUSH_BARRIER() __asm__ __volatile__("" ::: "memory")
#else
#define HUSH_BARRIER()
#endif

static HushMusic g_music;
HushMusic *hush_music_instance(void) { return &g_music; }

static const int layer_vol[LAYER_COUNT] = { VOL_DRONE, VOL_HARMONY, VOL_BASS, VOL_PERC, VOL_FRAG, VOL_TEXTURE };

void hush_music_init(HushMusic *m, float sample_rate, const MusicParams *p) {
    memset(m, 0, sizeof(*m));
    m->sr = sample_rate;
    dsp_init(sample_rate);
    m->mb_params = *p;
    memset(&m->mb_env, 0, sizeof(m->mb_env));
    m->mb_env.landmark = -1;
    m->c.P = *p;
    m->c.E = m->mb_env;
    composer_init(m, p->seed);
    delay_init(&m->delay);
    reverb_init(&m->reverb);
    reverb_set(&m->reverb, 1.35f, 5.0f, 0.5f);
    chorus_init(&m->chorus);
    radio_init(&m->radio);
    master_init(&m->master);
    m->g_master = p->master;
    m->viz_scope_div = h_maxi(1, (int)(sample_rate / 11025.0f + 0.5f));
    m->viz.seed = p->seed;
}

void hush_music_set_params(HushMusic *m, const MusicParams *p) {
    m->mb_seq++;
    HUSH_BARRIER();
    m->mb_params = *p;
    HUSH_BARRIER();
    m->mb_seq++;
}

void hush_music_set_env(HushMusic *m, const MusicEnv *e) {
    m->mb_seq++;
    HUSH_BARRIER();
    m->mb_env = *e;
    HUSH_BARRIER();
    m->mb_seq++;
}

void hush_music_reseed(HushMusic *m, uint32_t seed) {
    m->mb_reseed = seed;
    HUSH_BARRIER();
    m->mb_reseed_seq++;
}

void hush_music_event(HushMusic *m, int type, int arg) {
    m->mb_event_type = type;
    m->mb_event_arg = arg;
    HUSH_BARRIER();
    m->mb_event_seq++;
}

static void read_mailbox(HushMusic *m) {
    for (int tries = 0; tries < 4; tries++) {
        uint32_t s0 = m->mb_seq;
        HUSH_BARRIER();
        if (s0 & 1) continue;
        MusicParams p = m->mb_params;
        MusicEnv e = m->mb_env;
        HUSH_BARRIER();
        if (m->mb_seq != s0) continue;
        if (p.seed != m->c.P.seed) { m->c.pending_seed = p.seed; m->c.reseed_pending = 1; }
        m->c.P = p;
        m->c.E = e;
        break;
    }
    if (m->mb_reseed_seq != m->mb_reseed_seen) {
        m->mb_reseed_seen = m->mb_reseed_seq;
        m->c.pending_seed = m->mb_reseed;
        m->c.P.seed = m->mb_reseed;
        m->c.reseed_pending = 1;
    }
    if (m->mb_event_seq != m->mb_event_seen) {
        m->mb_event_seen = m->mb_event_seq;
        m->c.punct_type = m->mb_event_type;
        m->c.punct_arg = m->mb_event_arg;
    }
}

/* ---------------- event API used by the composer ---------------- */

void eng_log(HushMusic *m, int layer, int note, float vel) {
    Composer *c = &m->c;
    int v = (int)(h_clampf(vel, 0.0f, 1.0f) * 127.0f + 0.5f);
    c->event_hash = h_fnv1a(c->event_hash, (c->step << 4) | (uint32_t)layer);
    c->event_hash = h_fnv1a(c->event_hash, ((uint32_t)(note & 0xFF)) | ((uint32_t)v << 8));
    c->event_count++;
    c->stats.layer_events[layer]++;
    VizEvent *ev = &m->viz.events[m->viz.event_head];
    ev->layer = (uint8_t)layer; ev->note = (int8_t)note; ev->vel = (uint8_t)v; ev->step = c->step;
    m->viz.event_head = (m->viz.event_head + 1) % VIZ_EVENTS;
}

int eng_alloc(HushMusic *m, int first, int count) {
    int best = -1;
    float bl = 1e9f;
    for (int i = first; i < first + count; i++) if (!m->voices[i].active) return i;
    for (int i = first; i < first + count; i++) {
        Voice *v = &m->voices[i];
        float lvl = v->amp.val * v->gain_cur + (v->released ? 0.0f : 10.0f);
        if (lvl < bl) { bl = lvl; best = i; }
    }
    if (best >= 0 && !m->voices[best].released) m->c.stats.voice_steals++;
    return best;
}

static void fire_note(HushMusic *m, int layer, int voice, const Patch *p, int note, float vel, float pan, int32_t gate) {
    Voice *v = &m->voices[voice];
    uint32_t ns = m->c.event_count * 2654435761u + (uint32_t)voice * 40503u + 12345u;
    voice_start(v, p, (float)note, vel, pan, (uint8_t)layer, ns);
    m->gate[voice] = gate;
}

void eng_note(HushMusic *m, int layer, int voice, const Patch *p, int note, float vel, float pan, float gate_steps, float delay_frac) {
    eng_log(m, layer, note, vel);
    if (voice < 0 || voice >= MAX_VOICES) return;
    float sps = m->c.samples_per_step;
    int32_t gate = gate_steps > 0 ? (int32_t)(gate_steps * sps) : 0;
    if (delay_frac <= 0.0f) { fire_note(m, layer, voice, p, note, vel, pan, gate); return; }
    if (m->n_pend >= MAX_PENDING) { fire_note(m, layer, voice, p, note, vel, pan, gate); return; }
    Pending *e = &m->pend[m->n_pend++];
    e->delay = (int32_t)(delay_frac * sps);
    e->type = PEV_NOTE; e->layer = (uint8_t)layer; e->voice = (uint8_t)voice; e->note = (int8_t)note;
    e->vel = vel; e->pan = pan; e->patch = *p; e->gate = gate;
}

void eng_legato(HushMusic *m, int voice, int note, float gate_steps) {
    eng_log(m, m->voices[voice].layer, note, 0.0f);
    Voice *v = &m->voices[voice];
    v->pitch_target = (float)note;
    v->note_id = (int16_t)note;
    m->gate[voice] = (int32_t)(gate_steps * m->c.samples_per_step);
}

void eng_release(HushMusic *m, int voice, float delay_frac) {
    if (voice < 0 || voice >= MAX_VOICES) return;
    if (delay_frac <= 0.0f || m->n_pend >= MAX_PENDING) { voice_release(&m->voices[voice]); m->gate[voice] = 0; return; }
    Pending *e = &m->pend[m->n_pend++];
    e->delay = (int32_t)(delay_frac * m->c.samples_per_step);
    e->type = PEV_RELEASE; e->voice = (uint8_t)voice;
}

void eng_drum(HushMusic *m, int kind, float vel, float pan, float decay, float tone, float delay_frac) {
    eng_log(m, LAYER_PERC, kind, vel);
    if (delay_frac > 0.0f && m->n_pend < MAX_PENDING) {
        Pending *e = &m->pend[m->n_pend++];
        e->delay = (int32_t)(delay_frac * m->c.samples_per_step);
        e->type = PEV_DRUM; e->kind = (uint8_t)kind; e->vel = vel; e->pan = pan; e->aux1 = decay; e->aux2 = tone;
        return;
    }
    DrumVoice *d = &m->drums[m->drum_rr];
    m->drum_rr = (m->drum_rr + 1) % MAX_DRUMS;
    drum_start(d, kind, vel, pan, decay, tone, m->c.event_count * 747796405u + 1u);
}

static void fire_pending(HushMusic *m, Pending *e) {
    switch (e->type) {
    case PEV_NOTE: fire_note(m, e->layer, e->voice, &e->patch, e->note, e->vel, e->pan, e->gate); break;
    case PEV_RELEASE: voice_release(&m->voices[e->voice]); m->gate[e->voice] = 0; break;
    case PEV_DRUM: {
        DrumVoice *d = &m->drums[m->drum_rr];
        m->drum_rr = (m->drum_rr + 1) % MAX_DRUMS;
        drum_start(d, e->kind, e->vel, e->pan, e->aux1, e->aux2, (uint32_t)e->delay * 2891336453u + m->c.event_count);
        break;
    }
    default: break;
    }
}

/* ---------------- rendering ---------------- */

static void update_block_params(HushMusic *m, int n) {
    Composer *c = &m->c;
    const MusicParams *P = &c->P;
    float sr = m->sr;
    float k = 1.0f - expf(-(float)n / (2.5f * sr));
    float kf = 1.0f - expf(-(float)n / (0.4f * sr));
    for (int l = 0; l < LAYER_COUNT; l++) {
        float e = h_clampf(c->energy[l], 0.0f, 1.2f);
        float shape = (l == LAYER_ATMOS || l == LAYER_HARMONY || l == LAYER_DECAY) ? e : (0.55f + 0.45f * h_minf(e, 1.0f));
        float target = h_clampf(P->vol[layer_vol[l]], 0.0f, 1.0f) * shape;
        m->g_layer[l] += (target - m->g_layer[l]) * k;
    }
    m->g_master += (h_clampf(P->master, 0.0f, 1.0f) - m->g_master) * kf;
    float low = h_maxf(0.0f, 0.4f - c->s_alt) * 2.5f;
    m->g_delay += (P->delay * (0.75f + 0.4f * c->s_rain) - m->g_delay) * kf;
    m->g_reverb += (P->reverb * (0.75f + 0.35f * c->s_alt + 0.2f * c->s_rain) - m->g_reverb) * kf;
    float cut_t = (P->filter - 0.5f) * 30.0f - 6.0f * c->s_rain + (c->s_alt - 0.5f) * 4.0f - 6.0f * c->erosion + 4.0f * c->s_pursuit;
    m->g_cut += (cut_t - m->g_cut) * kf;
    float radio_t = 0.2f + 0.65f * c->erosion + 0.08f * (float)h_mini(c->archive_reps, 6) * c->s_landmark[LM_ARCHIVE];
    m->g_radio += (radio_t - m->g_radio) * k;
    float hiss_t = P->vol[VOL_TEXTURE] * (0.0015f + 0.008f * c->erosion + 0.002f * low);
    m->g_hiss += (hiss_t - m->g_hiss) * k;

    /* delay settings follow tempo, darkness, decay and weather */
    float dsteps = c->pb.delay_steps;
    float dt = dsteps * c->samples_per_step;
    while (dt > (float)(DLY_SIZE - 2048)) dt *= 0.5f;
    m->delay.time_target = dt;
    m->delay.fb = h_clampf(0.15f + 0.6f * P->delay_fb + 0.15f * c->erosion + 0.08f * c->s_rain, 0.0f, 0.9f);
    m->delay.lp_coef = onepole_coef(4800.0f - 3000.0f * c->s_dark - 1000.0f * c->s_rain);
    m->delay.wow_depth = 0.3f + 1.6f * c->erosion;

    if (++m->rev_update_ctr >= 48) {
        m->rev_update_ctr = 0;
        float decay = c->pb.rev_decay * (0.6f + 0.8f * P->reverb) * (1.0f + 0.5f * c->s_alt + 0.3f * c->s_landmark[LM_RESERVOIR] + 0.3f * c->s_landmark[LM_CHOIR]);
        for (int i = 0; i < REV_LINES; i++)
            m->reverb.g[i] = powf(10.0f, -3.0f * (float)m->reverb.len[i] / (h_maxf(decay, 0.3f) * sr));
        float damp = h_clampf(c->pb.rev_damp + 0.2f * c->s_dark, 0.0f, 0.95f);
        m->reverb.damp_coef = h_clampf(1.0f - damp, 0.05f, 1.0f) * 0.6f + 0.05f;
    }
}

static void render_block(HushMusic *m, float *outL, float *outR, int n) {
    Composer *c = &m->c;
    memset(m->padL, 0, sizeof(float) * n); memset(m->padR, 0, sizeof(float) * n);
    memset(m->mainL, 0, sizeof(float) * n); memset(m->mainR, 0, sizeof(float) * n);
    memset(m->radL, 0, sizeof(float) * n); memset(m->radR, 0, sizeof(float) * n);
    memset(m->sdL, 0, sizeof(float) * n); memset(m->sdR, 0, sizeof(float) * n); memset(m->srv, 0, sizeof(float) * n);
    update_block_params(m, n);

    float low = h_maxf(0.0f, 0.4f - c->s_alt) * 2.5f;
    uint32_t mask = 0;
    int nact = 0;
    float lay_lvl[LAYER_COUNT] = {0};
    for (int i = 0; i < MAX_VOICES; i++) {
        Voice *v = &m->voices[i];
        if (!v->active) continue;
        if (m->gate[i] > 0) { m->gate[i] -= n; if (m->gate[i] <= 0) { m->gate[i] = 0; voice_release(v); } }
        float g = m->g_layer[v->layer];
        if (i == V_TEX0) g *= 0.05f + 0.85f * c->s_rain + 0.1f * c->s_fog;
        else if (i > V_TEX0) g *= c->pb.hum * (0.45f + 0.55f * h_minf(low, 1.0f)) + 0.05f * c->s_traffic;
        float *bl, *br;
        if (v->bus == BUS_PAD) { bl = m->padL; br = m->padR; }
        else if (v->bus == BUS_RADIO) { bl = m->radL; br = m->radR; }
        else { bl = m->mainL; br = m->mainR; }
        voice_render(v, n, g, bl, br, m->sdL, m->sdR, m->srv, m->g_cut);
        if (v->active) { mask |= 1u << i; nact++; lay_lvl[v->layer] += v->level_meter; }
    }
    uint32_t dmask = 0;
    for (int i = 0; i < MAX_DRUMS; i++) {
        DrumVoice *d = &m->drums[i];
        if (!d->active) continue;
        drum_render(d, n, m->g_layer[LAYER_PERC], m->mainL, m->mainR, m->sdL, m->sdR, m->srv);
        if (d->active) { dmask |= 1u << i; nact++; lay_lvl[LAYER_PERC] += d->level_meter; }
    }
    if ((uint32_t)nact > c->stats.max_voices) c->stats.max_voices = (uint32_t)nact;

    chorus_process(&m->chorus, m->padL, m->padR, 0.12f + 0.4f * c->s_motion, 0.4f + 0.5f * c->P.mod_depth, 0.42f, n);
    radio_process(&m->radio, m->radL, m->radR, m->g_radio, n);

    for (int i = 0; i < n; i++) {
        outL[i] = m->padL[i] + m->mainL[i] + m->radL[i];
        outR[i] = m->padR[i] + m->mainR[i] + m->radR[i];
        m->sdL[i] *= m->g_delay; m->sdR[i] *= m->g_delay;
        m->srv[i] *= m->g_reverb;
    }
    delay_process(&m->delay, m->sdL, m->sdR, outL, outR, m->srv, 0.35f * m->g_reverb + 0.1f, n);
    reverb_process(&m->reverb, m->srv, outL, outR, 0.9f, n);
    master_process(&m->master, outL, outR, m->g_master * 1.12f, m->g_hiss, n);

    /* metering and visualisation (display only) */
    MusicViz *z = &m->viz;
    float pl = z->peakL * 0.97f, pr = z->peakR * 0.97f;
    for (int i = 0; i < n; i++) {
        float al = h_absf(outL[i]), ar = h_absf(outR[i]);
        if (al > pl) pl = al;
        if (ar > pr) pr = ar;
        m->rms_accL += outL[i] * outL[i]; m->rms_accR += outR[i] * outR[i];
        if (++m->rms_n >= (int)m->sr / 20) {
            z->rmsL = sqrtf(m->rms_accL / (float)m->rms_n); z->rmsR = sqrtf(m->rms_accR / (float)m->rms_n);
            m->rms_accL = m->rms_accR = 0; m->rms_n = 0;
        }
    }
    static int scope_cnt = 0, scope_pos = 0;
    for (int i = 0; i < n; i++) {
        if (++scope_cnt >= m->viz_scope_div) {
            scope_cnt = 0;
            z->scopeL[scope_pos] = outL[i]; z->scopeR[scope_pos] = outR[i];
            scope_pos = (scope_pos + 1) % VIZ_SCOPE;
        }
    }
    z->peakL = pl; z->peakR = pr;
    z->voice_mask = mask; z->drum_mask = dmask; z->voices_active = nact;
    for (int l = 0; l < LAYER_COUNT; l++) { z->layer_level[l] = lay_lvl[l]; z->layer_gain[l] = m->g_layer[l]; }
}

void hush_music_render(HushMusic *m, float *out, int frames) {
    float bl[HUSH_CTRL], br[HUSH_CTRL];
    Composer *c = &m->c;
    while (frames > 0) {
        if (c->step_countdown <= 0.0f) {
            read_mailbox(m);
            composer_step(m);
            c->step_countdown += c->samples_per_step;
        }
        int n = HUSH_CTRL;
        if (n > frames) n = frames;
        int to_step = (int)ceilf(c->step_countdown);
        if (to_step < 1) to_step = 1;
        if (n > to_step) n = to_step;
        /* split at the next pending event, fire anything due now */
        for (int i = 0; i < m->n_pend;) {
            if (m->pend[i].delay <= 0) { fire_pending(m, &m->pend[i]); m->pend[i] = m->pend[--m->n_pend]; continue; }
            if (m->pend[i].delay < n) n = m->pend[i].delay;
            i++;
        }
        render_block(m, bl, br, n);
        for (int i = 0; i < n; i++) { out[2 * i] = bl[i]; out[2 * i + 1] = br[i]; }
        for (int i = 0; i < m->n_pend; i++) m->pend[i].delay -= n;
        c->step_countdown -= (float)n;
        out += 2 * n;
        frames -= n;
    }
    /* publish the slower-changing state for the UI */
    MusicViz *z = &m->viz;
    z->section = c->section; z->section_bars_left = c->section_bars_left;
    z->bar = c->bar; z->step = c->step_in_bar;
    z->key_center = ((c->P.key + c->key_offset) % 12 + 12) % 12;
    z->mode = c->mode_cur;
    memcpy(z->chord_name, c->chord.name, sizeof(z->chord_name));
    z->tempo = c->bpm; z->erosion = c->erosion;
    z->event_count = c->event_count; z->event_hash = c->event_hash;
    z->seed = c->seed;
}

void hush_music_render_s16(HushMusic *m, int16_t *out, int frames) {
    float buf[2 * 256];
    while (frames > 0) {
        int n = frames > 256 ? 256 : frames;
        hush_music_render(m, buf, n);
        for (int i = 0; i < 2 * n; i++) {
            float x = buf[i] * 32767.0f;
            if (x > 32767.0f) x = 32767.0f;
            if (x < -32768.0f) x = -32768.0f;
            out[i] = (int16_t)(x < 0 ? x - 0.5f : x + 0.5f);
        }
        out += 2 * n;
        frames -= n;
    }
}

const MusicViz *hush_music_viz(HushMusic *m) { return &m->viz; }
uint32_t hush_music_event_hash(HushMusic *m) { return m->c.event_hash; }
uint32_t hush_music_event_count(HushMusic *m) { return m->c.event_count; }
void hush_music_stats(HushMusic *m, MusicStats *out) { *out = m->c.stats; }
