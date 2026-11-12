/* HUSH — the generative composer.
 *
 * All musical decisions are taken on 16th-note step boundaries, using
 *   - the composition seed (split into independent PCG32 streams per concern),
 *   - per-step smoothed copies of the player's parameters and the world inputs,
 *   - the composer's own long-form state (section, chord, motifs, patterns).
 * Nothing here depends on the audio sample rate, so the same seed and the same
 * input sequence produce the same event stream on the PSP (22.05 kHz engine)
 * and on the host renderer (44.1 kHz). The engine converts steps to samples. */
#include "music_internal.h"
#include <string.h>

/* ---------------- helpers ---------------- */

static float qz(float x) { return (float)(int)(h_clampf(x, 0.0f, 1.0f) * 256.0f + 0.5f) * (1.0f / 256.0f); }

static float step_seconds(const Composer *c) { return 60.0f / (c->bpm * 4.0f); }

static float smooth_k(const Composer *c, float tau_s) {
    /* 1 - e^-x by a short series using only + - * /, so the result is bit-identical
     * on every IEEE-754 platform (libm expf differs between newlib and glibc). x < ~0.2 here. */
    float x = step_seconds(c) / h_maxf(tau_s, 0.01f);
    if (x > 0.5f) x = 0.5f;
    return x * (1.0f - x * (0.5f - x * (0.16666667f - x * 0.041666668f)));
}

static int tonic_pc(const Composer *c) { return ((c->P.key + c->key_offset) % 12 + 12) % 12; }
static const Scale *cur_scale(const Composer *c) { return theory_scale(c->mode_cur); }

static int target_mode(const Composer *c) {
    if (c->P.mode >= 0 && c->P.mode < MODE_COUNT) return c->P.mode;
    int m = hush_profiles[c->district_major].mode;
    /* darker settings lean the automatic mode toward phrygian/locrian colours */
    if (c->s_dark > 0.82f && m == MODE_AEOLIAN) m = MODE_PHRYGIAN;
    if (c->s_pursuit > 0.6f && (m == MODE_DORIAN || m == MODE_AEOLIAN)) m = MODE_PHRYGIAN;
    return m;
}

static void euclid(float *lane, int k, int n, int rot, float v) {
    if (k <= 0) return;
    for (int i = 0; i < n; i++) {
        if (((i * k) % n) < k) lane[(i + rot) % 16] = h_maxf(lane[(i + rot) % 16], v);
    }
}

static int pick_style(const float *w, int n, Rng *r) { return rng_weighted(r, w, n); }

/* ---------------- section structure ---------------- */

static const float sect_table[SEC_COUNT][LAYER_COUNT] = {
    /*           atmos harm pulse perc frag decay */
    /* EMERGE */ {1.00f, 0.55f, 0.25f, 0.12f, 0.45f, 0.80f},
    /* ACCUM  */ {1.00f, 0.85f, 0.70f, 0.60f, 0.60f, 0.65f},
    /* SUSTAIN*/ {0.90f, 1.00f, 1.00f, 1.00f, 0.70f, 0.55f},
    /* FRAGM. */ {0.80f, 0.60f, 0.55f, 0.65f, 1.00f, 0.75f},
    /* DISSOL.*/ {1.00f, 0.70f, 0.20f, 0.15f, 0.50f, 0.95f},
    /* SILENCE*/ {0.35f, 0.15f, 0.00f, 0.00f, 0.22f, 0.55f},
    /* RENEW  */ {1.00f, 0.90f, 0.45f, 0.35f, 0.65f, 0.60f},
};
static const float sect_next[SEC_COUNT][SEC_COUNT] = {
    /* from EMERGE */ {0.0f, 3.0f, 1.0f, 1.0f, 0.0f, 0.3f, 0.0f},
    /* ACCUM */       {0.0f, 0.0f, 3.0f, 1.5f, 1.0f, 0.0f, 0.3f},
    /* SUSTAIN */     {0.0f, 0.5f, 0.0f, 2.0f, 2.0f, 0.2f, 1.0f},
    /* FRAGMENT */    {0.0f, 1.0f, 1.0f, 0.0f, 2.0f, 0.5f, 1.0f},
    /* DISSOLVE */    {1.5f, 0.0f, 0.0f, 0.5f, 0.0f, 1.5f, 1.5f},
    /* SILENCE */     {2.5f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.5f},
    /* RENEW */       {1.0f, 2.0f, 1.5f, 0.5f, 0.0f, 0.0f, 0.0f},
};
static const int sect_len[SEC_COUNT][2] = { {8, 16}, {8, 24}, {16, 32}, {8, 24}, {8, 16}, {4, 12}, {8, 16} };

static void enter_section(Composer *c, int s) {
    c->section = s;
    float scale = 1.3f - 0.6f * c->s_motion - 0.4f * c->s_pursuit;
    int lo = sect_len[s][0], hi = sect_len[s][1];
    int bars = rng_range(&c->r_struct, lo, hi);
    bars = (int)((float)bars * h_clampf(scale, 0.5f, 1.4f));
    bars = h_maxi(4, (bars + 2) & ~3); /* phrase-aligned (multiple of 4 bars) */
    c->section_bars_left = c->section_len = bars;
    for (int l = 0; l < LAYER_COUNT; l++) c->sect_mult[l] = sect_table[s][l];
    c->stats.section_changes++;
    c->stats.section_hist[s]++;
}

static void next_section(Composer *c) {
    float w[SEC_COUNT];
    for (int i = 0; i < SEC_COUNT; i++) w[i] = sect_next[c->section][i];
    float calm = 1.0f - c->s_pursuit;
    w[SEC_SILENCE] *= calm * (0.6f + 1.4f * c->pb.bias[LAYER_DECAY] * (1.0f - c->s_density));
    w[SEC_DISSOLVE] *= 0.4f + 0.6f * calm;
    w[SEC_ACCUMULATE] *= 0.6f + c->s_density + c->s_pursuit;
    w[SEC_SUSTAIN] *= 0.6f + c->s_density + c->s_pursuit;
    w[SEC_FRAGMENT] *= 0.6f + c->s_motion;
    int s = rng_weighted(&c->r_struct, w, SEC_COUNT);
    if (s == c->section) s = (s + 1) % SEC_COUNT;
    enter_section(c, s);
    if (s == SEC_RENEW && rng_chance(&c->r_struct, 0.2f + 0.45f * c->s_motion)) {
        /* move the tonal centre; tends to come home again later */
        static const int moves[] = { 5, -5, 3, -3, 7, -2, 2 };
        int to = c->key_offset != 0 && rng_chance(&c->r_struct, 0.6f) ? 0 : moves[rng_int(&c->r_struct, 7)];
        if (to > 7) to -= 12;
        if (to < -7) to += 12;
        c->key_offset_pending = to;
    }
}

/* ---------------- pattern generation ---------------- */

static void gen_bass(Composer *c) {
    BassPattern *b = &c->bass;
    memset(b, 0, sizeof(*b));
    Rng *r = &c->r_pulse;
    int st = c->bass_style;
    for (int i = 0; i < 16; i++) {
        float p;
        switch (st) {
        case BS_DRIVING: p = (i == 0 || i == 3 || i == 6 || i == 10 || i == 12) ? 0.95f : 0.45f; break;
        case BS_DUB: p = i == 0 ? 0.95f : ((i == 3 || i == 6 || i == 10 || i == 11 || i == 14) ? 0.45f : 0.04f); break;
        case BS_DEEP: p = i == 0 ? 0.85f : (i == 8 ? 0.2f : 0.0f); break;
        default: p = (i == 0 || i == 8) ? 0.95f : ((i % 4) == 0 ? 0.55f : ((i % 2) == 0 ? 0.45f : 0.1f)); break;
        }
        if (!rng_chance(r, p)) continue;
        b->on[i] = 1;
        b->accent[i] = (i % 4 == 0) || rng_chance(r, 0.15f);
        float wd[6] = {6.0f, 1.5f, 1.0f, 0.6f, 0.4f, 0.4f};  /* root, fifth, octave, third, seventh, passing */
        if (st == BS_DRIVING) { wd[0] = 8; wd[3] = 0.2f; }
        if (st == BS_DEEP) { wd[1] = 2; wd[2] = 0.2f; wd[5] = 0; }
        int k = rng_weighted(r, wd, 6);
        static const int8_t degs[6] = {0, 4, 0, 2, 6, 1};
        b->deg[i] = degs[k];
        b->oct[i] = k == 2 ? 1 : 0;
        b->len[i] = (uint8_t)(st == BS_DEEP ? 14 : (st == BS_DUB ? rng_range(r, 2, 5) : (st == BS_DRIVING ? 1 : rng_range(r, 1, 3))));
        b->slide[i] = st != BS_DRIVING && rng_chance(r, 0.18f);
    }
    b->on[0] = 1; b->deg[0] = 0; b->oct[0] = 0; b->accent[0] = 1;
    if (!b->len[0]) b->len[0] = 2;
}

static void mutate_bass(Composer *c) {
    Rng *r = &c->r_pulse;
    int n = (int)(c->s_seqvar * 3.0f + c->s_motion * 1.5f + rng_float(r));
    for (int k = 0; k < n; k++) {
        int i = 1 + rng_int(r, 15);
        if (c->bass.on[i] && rng_chance(r, 0.5f)) c->bass.on[i] = 0;
        else {
            c->bass.on[i] = 1;
            static const int8_t degs[5] = {0, 4, 6, 2, 3};
            c->bass.deg[i] = degs[rng_int(r, 5)];
            c->bass.len[i] = (uint8_t)rng_range(r, 1, 3);
            c->bass.oct[i] = rng_chance(r, 0.15f);
        }
    }
}

static void gen_perc(Composer *c) {
    Rng *r = &c->r_perc;
    memset(c->lanes, 0, sizeof(c->lanes));
    float *K = c->lanes[DR_KICK].vel, *S = c->lanes[DR_SNARE].vel, *H = c->lanes[DR_HAT].vel, *O = c->lanes[DR_OPENHAT].vel;
    float *M = c->lanes[DR_METAL].vel, *C = c->lanes[DR_CLICK].vel, *T = c->lanes[DR_THUD].vel, *R = c->lanes[DR_RIM].vel;
    switch (c->perc_style) {
    case PS_INDUSTRIAL:
        euclid(K, rng_range(r, 3, 5), 16, 0, 0.9f);
        { int k = rng_range(r, 2, 4); int rot = rng_int(r, 16); euclid(M, k, 16, rot, 0.7f); }
        S[12] = 0.65f; S[7] = 0.25f;
        for (int i = 0; i < 16; i++) H[i] = rng_chance(r, 0.6f) ? 0.35f + 0.3f * rng_float(r) : 0.0f;
        euclid(C, 7, 16, rng_int(r, 4), 0.5f);
        T[0] = 0.35f;
        break;
    case PS_BROKEN:
        K[0] = 1.0f; K[10] = 0.8f; K[rng_chance(r, 0.5f) ? 7 : 13] = 0.35f;
        S[4] = 0.9f; S[12] = 0.9f; S[7] = 0.22f; S[9] = 0.22f; S[15] = 0.28f;
        for (int i = 0; i < 16; i += 2) H[i] = 0.7f;
        for (int i = 1; i < 16; i += 2) H[i] = 0.25f;
        O[6] = 0.3f; R[11] = 0.3f; R[3] = 0.15f;
        break;
    case PS_SPARSE:
        T[0] = 0.6f; T[rng_chance(r, 0.5f) ? 10 : 8] = 0.15f;
        M[rng_int(r, 16)] = 0.15f;
        euclid(C, 2, 16, rng_int(r, 16), 0.2f);
        H[rng_int(r, 16)] = 0.1f;
        break;
    case PS_GEOMETRIC: default:
        K[0] = 1.0f; K[8] = 1.0f; K[4] = 0.7f; K[12] = 0.7f;
        H[2] = H[6] = H[10] = H[14] = 0.8f;
        for (int i = 1; i < 16; i += 2) H[i] = 0.15f;
        euclid(R, 3, 16, rng_int(r, 16), 0.55f);
        euclid(C, 5, 16, rng_int(r, 16), 0.4f);
        S[12] = 0.3f; O[14] = 0.3f;
        break;
    }
    c->displace = 0;
    c->halftime = 0;
}

static void mutate_perc(Composer *c) {
    Rng *r = &c->r_perc;
    /* rotate one auxiliary lane, add or remove a ghost note */
    int lane = rng_chance(r, 0.5f) ? DR_CLICK : (rng_chance(r, 0.5f) ? DR_RIM : DR_METAL);
    float tmp[16];
    int rot = 1 + rng_int(r, 3);
    for (int i = 0; i < 16; i++) tmp[(i + rot) % 16] = c->lanes[lane].vel[i];
    memcpy(c->lanes[lane].vel, tmp, sizeof(tmp));
    int g = rng_int(r, 16);
    if (c->lanes[DR_SNARE].vel[g] > 0 && c->lanes[DR_SNARE].vel[g] < 0.5f) c->lanes[DR_SNARE].vel[g] = 0;
    else if (c->lanes[DR_SNARE].vel[g] == 0 && c->perc_style != PS_GEOMETRIC) c->lanes[DR_SNARE].vel[g] = 0.2f;
    if (c->section == SEC_FRAGMENT || rng_chance(r, 0.12f * c->s_motion)) {
        /* broken rhythm: displace kick and snare, sometimes half-time */
        c->displace = rng_chance(r, 0.6f) ? rng_range(r, 1, 3) : 0;
        c->halftime = rng_chance(r, 0.3f);
    } else {
        c->displace = 0;
        c->halftime = 0;
    }
}

static void gen_arp(Composer *c) {
    Rng *r = &c->r_pulse;
    c->arp_k = (uint8_t)rng_range(r, 3, 7);
    c->arp_rot = (uint8_t)rng_int(r, 16);
    c->arp_order = (uint8_t)rng_int(r, 3);
}

/* ---------------- motifs ---------------- */

static void gen_motif(Composer *c, Motif *mo) {
    Rng *r = &c->r_frag;
    memset(mo, 0, sizeof(*mo));
    mo->n = (uint8_t)rng_range(r, 3, 6);
    int d = c->chord.degree + (rng_chance(r, 0.5f) ? 2 : (rng_chance(r, 0.5f) ? 4 : 0));
    static const int8_t steps[7] = {-2, -1, 1, 2, 3, -3, 0};
    static const float sw[7] = {1.0f, 3.0f, 3.0f, 1.5f, 0.5f, 0.5f, 0.5f};
    static const uint8_t durs[6] = {2, 3, 4, 4, 6, 8};
    for (int i = 0; i < mo->n; i++) {
        mo->deg[i] = (int8_t)d;
        mo->dur[i] = durs[rng_int(r, 6)];
        mo->vel[i] = (uint8_t)rng_range(r, 60, 100);
        d += steps[rng_weighted(r, sw, 7)];
        if (d > 12) d -= 3;
        if (d < -3) d += 3;
    }
    mo->dur[mo->n - 1] = (uint8_t)(mo->dur[mo->n - 1] + 4);
    float w[FI_COUNT];
    for (int i = 0; i < FI_COUNT; i++) w[i] = hush_profiles[c->district_major].frag_inst[i];
    mo->inst = (uint8_t)rng_weighted(r, w, FI_COUNT);
}

static void vary_motif(Composer *c, const Motif *src, Motif *dst) {
    Rng *r = &c->r_frag;
    *dst = *src;
    int kind = rng_int(r, 6);
    switch (kind) {
    case 0: { int t = rng_chance(r, 0.5f) ? rng_range(r, 1, 2) : -rng_range(r, 1, 2); for (int i = 0; i < dst->n; i++) dst->deg[i] = (int8_t)(dst->deg[i] + t); break; }
    case 1: for (int i = 0; i < dst->n; i++) dst->deg[i] = (int8_t)(2 * src->deg[0] - src->deg[i]); break;          /* inversion */
    case 2: for (int i = 0; i < dst->n; i++) { dst->deg[i] = src->deg[src->n - 1 - i]; dst->dur[i] = src->dur[src->n - 1 - i]; } break; /* retrograde */
    case 3: for (int i = 0; i < dst->n; i++) dst->dur[i] = (uint8_t)h_mini(src->dur[i] * 2, 16); break;   /* augmentation */
    case 4: if (dst->n > 2) dst->n = (uint8_t)(dst->n - rng_range(r, 1, h_mini(2, dst->n - 2))); break;    /* truncation */
    default: /* ornament: insert a passing tone */
        if (dst->n < 8 && dst->n > 1) {
            int at = 1 + rng_int(r, dst->n - 1);
            for (int i = dst->n; i > at; i--) { dst->deg[i] = dst->deg[i - 1]; dst->dur[i] = dst->dur[i - 1]; dst->vel[i] = dst->vel[i - 1]; }
            dst->deg[at] = (int8_t)((dst->deg[at - 1] + dst->deg[at + 1]) / 2 + (rng_chance(r, 0.5f) ? 1 : 0));
            dst->dur[at] = 2; dst->vel[at] = 55; dst->n++;
        }
        break;
    }
}

/* ---------------- harmony ---------------- */

static void start_pads(HushMusic *m) {
    Composer *c = &m->c;
    for (int i = 0; i < VOICING_N; i++) if (c->pad_voice[i] >= 0) eng_release(m, c->pad_voice[i], 0);
    if (c->energy[LAYER_HARMONY] < 0.1f) { for (int i = 0; i < VOICING_N; i++) c->pad_voice[i] = -1; return; }
    Patch p;
    int vox = c->s_landmark[LM_CHOIR] > 0.3f || c->P.pad_wave == W_VOX;
    patch_pad(&p, c, vox);
    for (int i = 0; i < VOICING_N; i++) {
        int v = eng_alloc(m, V_HARM0, V_HARM_N);
        float pan = ((float)i - 1.5f) * 0.45f;
        eng_note(m, LAYER_HARMONY, v, &p, c->voicing[i], 0.23f + 0.03f * (float)(i == 3), pan, 0, (float)i * 0.25f);
        c->pad_voice[i] = v;
    }
}

static void start_drones(HushMusic *m) {
    Composer *c = &m->c;
    for (int i = 0; i < V_ATMOS_N; i++) if (m->voices[V_ATMOS0 + i].active && !m->voices[V_ATMOS0 + i].released) eng_release(m, V_ATMOS0 + i, 0);
    int root = tonic_pc(c) + c->P.drone_root;
    int note = 33 + ((root - 33) % 12 + 12) % 12;      /* MIDI 33..44 */
    if (c->s_dark > 0.7f && note > 38) note -= 12;
    if (note < 26) note += 12;
    static const int ints[4] = {7, 12, 5, 19};
    float wi[4] = {3.0f, 1.5f, 1.0f, 0.6f};
    int second = note + ints[rng_weighted(&c->r_atmos, wi, 4)];
    c->drone_notes[0] = note; c->drone_notes[1] = second;
    Patch p;
    patch_drone(&p, c, 0);
    eng_note(m, LAYER_ATMOS, eng_alloc(m, V_ATMOS0, V_ATMOS_N), &p, note, 0.26f, -0.15f, 0, 0);
    patch_drone(&p, c, 1);
    eng_note(m, LAYER_ATMOS, eng_alloc(m, V_ATMOS0, V_ATMOS_N), &p, second, 0.17f, 0.25f, 0, 0.5f);
    c->drone_bars = rng_range(&c->r_atmos, 24, 64);
}

static void change_chord(HushMusic *m, int first) {
    Composer *c = &m->c;
    int retrig_drone = first;
    /* pending tonal moves happen only on chord boundaries */
    if (c->key_offset_pending != c->key_offset) { c->key_offset = c->key_offset_pending; retrig_drone = 1; c->stats.key_changes++; }
    int tm = target_mode(c);
    if (tm != c->mode_cur) { c->mode_cur = tm; retrig_drone = 1; }
    const Scale *s = cur_scale(c);
    int deg;
    if (first) deg = 0;
    else if (c->punct_type == MEV_MISSION_COMPLETE) { deg = 0; c->punct_type = 0; }
    else deg = theory_next_degree(s, tonic_pc(c), c->chord.degree, c->s_complex, &c->r_harm);
    float tension = h_clampf(c->s_pursuit * 0.8f + c->s_dark * 0.3f, 0.0f, 1.0f);
    float sus = 0.12f + 0.25f * (1.0f - c->s_dark) + 0.2f * c->s_alt;
    theory_build_chord(&c->chord, s, tonic_pc(c), deg, c->s_complex, tension, sus, &c->r_harm);
    int lo = 50 - (int)(c->s_dark * 4.0f), hi = 76 - (int)(c->s_dark * 4.0f);
    theory_voice_lead(&c->chord, first ? (int[VOICING_N]){0, 0, 0, 0} : c->voicing, c->voicing, lo, hi);
    /* harmonic rhythm: slower when high or hovering, faster with motion or pursuit */
    float bars = 4.0f * (1.0f + 0.9f * c->s_alt) * (1.0f + 0.6f * (1.0f - c->s_speed)) * (1.3f - 0.6f * c->s_motion) * (1.0f - 0.45f * c->s_pursuit);
    float wsel[3] = {1.0f, 2.0f, 1.0f};
    int jitter = rng_weighted(&c->r_harm, wsel, 3) - 1;
    int b = (int)(bars + 0.5f);
    b = b <= 1 ? 1 : (b <= 3 ? 2 : (b <= 6 ? 4 : (b <= 12 ? 8 : 16)));
    if (jitter > 0 && b < 16) b *= 2;
    if (jitter < 0 && b > 1) b /= 2;
    c->chord_bars_left = b;
    c->stats.chord_changes++;
    if (retrig_drone) start_drones(m);
    start_pads(m);
}

/* ---------------- control smoothing ---------------- */

static void update_controls(HushMusic *m) {
    Composer *c = &m->c;
    const MusicEnv *e = &c->E;
    const MusicParams *P = &c->P;
    float alt = 0.5f, spd = 0.25f, rain = 0, pur = 0, trf = 0.3f, can = 0.3f, sig = 0, scan = 0, fog = 0.3f;
    if (e->active) {
        alt = qz(e->altitude); spd = qz(e->speed); rain = qz(e->rain); pur = qz(e->pursuit);
        trf = qz(e->traffic); can = qz(e->canyon); sig = qz(e->signal); scan = qz(e->scan); fog = qz(e->fog);
    }
    float k2 = smooth_k(c, 2.5f), k6 = smooth_k(c, 6.0f), k12 = smooth_k(c, 12.0f), k25 = smooth_k(c, 25.0f);
    c->s_alt += (alt - c->s_alt) * k6;
    c->s_speed += (spd - c->s_speed) * k2;
    c->s_speed_slow += (spd - c->s_speed_slow) * k25;
    c->s_rain += (rain - c->s_rain) * k12;
    c->s_pursuit += (pur - c->s_pursuit) * k2;
    c->s_traffic += (trf - c->s_traffic) * k6;
    c->s_canyon += (can - c->s_canyon) * k6;
    c->s_signal += (sig - c->s_signal) * k2;
    c->s_scan += (scan - c->s_scan) * k2;
    c->s_fog += (fog - c->s_fog) * k12;
    int dist = h_clampi(e->district, 0, DISTRICT_COUNT - 1);
    for (int d = 0; d < DISTRICT_COUNT; d++) c->s_district[d] += ((d == dist ? 1.0f : 0.0f) - c->s_district[d]) * k12;
    for (int l = 0; l < LM_COUNT; l++) {
        float t = (e->active && e->landmark == l) ? qz(e->landmark_prox) : 0.0f;
        c->s_landmark[l] += (t - c->s_landmark[l]) * k6;
    }
    c->s_density += (qz(P->density) - c->s_density) * k2;
    c->s_dark += (qz(P->darkness) - c->s_dark) * k6;
    c->s_motion += (qz(P->motion) - c->s_motion) * k6;
    c->s_complex += (qz(P->complexity) - c->s_complex) * k6;
    c->s_rhythm += (qz(P->rhythm_prob) - c->s_rhythm) * k2;
    c->s_melody += (qz(P->melody_prob) - c->s_melody) * k2;
    c->s_seqvar += (qz(P->seq_variation) - c->s_seqvar) * k6;
    profile_blend(c->s_district, &c->pb);

    int maj = 0;
    for (int d = 1; d < DISTRICT_COUNT; d++) if (c->s_district[d] > c->s_district[maj]) maj = d;
    c->district_major = maj;

    /* tempo */
    float bpm_t = h_clampf(P->tempo, 50.0f, 140.0f) * c->pb.tempo_mul * (1.0f + 0.1f * c->s_pursuit);
    bpm_t = (float)(int)(bpm_t * 4.0f + 0.5f) * 0.25f;
    c->bpm += (bpm_t - c->bpm) * smooth_k(c, 4.0f);

    /* combined layer energy */
    float high = h_maxf(0.0f, c->s_alt - 0.45f) * 1.8f;
    float low = h_maxf(0.0f, 0.4f - c->s_alt) * 2.5f;
    float dens = 0.45f + 1.1f * c->s_density;
    float en[LAYER_COUNT];
    en[LAYER_ATMOS] = 0.9f + 0.25f * high;
    en[LAYER_HARMONY] = 0.8f + 0.3f * high - 0.15f * c->s_pursuit;
    en[LAYER_PULSE] = dens * (0.5f + 0.45f * c->s_speed + 0.6f * c->s_pursuit + 0.3f * low - 0.45f * high + 0.15f * c->s_traffic);
    en[LAYER_PERC] = dens * (0.6f * c->s_rhythm + 0.15f) * (0.3f + 0.7f * c->s_speed_slow + 0.25f * c->s_speed + 0.7f * c->s_pursuit + 0.2f * c->s_canyon - 0.45f * high);
    en[LAYER_FRAG] = (0.55f + 0.45f * c->s_melody) * (0.8f + 0.4f * c->s_scan + 0.3f * c->s_signal);
    en[LAYER_DECAY] = 0.6f + 0.4f * low + 0.3f * c->s_rain;
    /* landmarks */
    float ch = c->s_landmark[LM_CHOIR], st = c->s_landmark[LM_STATION9], ar = c->s_landmark[LM_ARCHIVE];
    float rs = c->s_landmark[LM_RESERVOIR], ex = c->s_landmark[LM_EXCHANGE];
    en[LAYER_PERC] *= (1.0f - 0.6f * ch) * (1.0f + 0.6f * st) * (1.0f - 0.5f * rs);
    en[LAYER_HARMONY] *= (1.0f + 0.4f * ch) * (1.0f - 0.4f * rs);
    en[LAYER_FRAG] *= 1.0f + 0.8f * ar + 0.5f * ch;
    en[LAYER_PULSE] *= (1.0f - 0.4f * ch) * (1.0f + 0.3f * st);
    /* Silent Exchange: layers disappear one by one as you approach */
    en[LAYER_PERC] *= h_clampf(1.0f - ex * 2.5f, 0.0f, 1.0f);
    en[LAYER_PULSE] *= h_clampf(1.0f - (ex - 0.15f) * 2.2f, 0.0f, 1.0f);
    en[LAYER_HARMONY] *= h_clampf(1.0f - (ex - 0.35f) * 2.0f, 0.0f, 1.0f);
    en[LAYER_ATMOS] *= h_clampf(1.0f - (ex - 0.55f) * 1.6f, 0.08f, 1.0f);
    en[LAYER_FRAG] *= h_clampf(1.0f - (ex - 0.75f) * 2.0f, 0.3f, 1.0f);
    for (int l = 0; l < LAYER_COUNT; l++) {
        float target = h_clampf(en[l] * c->sect_mult[l] * c->pb.bias[l], 0.0f, 1.5f);
        c->energy[l] += (target - c->energy[l]) * smooth_k(c, 3.0f);
    }

    /* decay / erosion process */
    c->erosion += (c->erosion_target - c->erosion) * smooth_k(c, 18.0f);
    for (int l = 0; l < LAYER_COUNT; l++) c->drift[l] += (c->drift_target[l] - c->drift[l]) * smooth_k(c, 8.0f);
}

/* ---------------- bar-level decisions ---------------- */

static void bar_tick(HushMusic *m) {
    Composer *c = &m->c;
    int first = c->bar == 0 && c->step == 0;

    if (c->reseed_pending && !first) {
        c->reseed_pending = 0;
        composer_init(m, c->pending_seed);
        first = 1;
    }
    if (first) {
        enter_section(c, SEC_EMERGE);
        c->mode_cur = target_mode(c);
        c->bass_style = pick_style(hush_profiles[c->district_major].bass_style, BS_COUNT, &c->r_pulse);
        c->perc_style = pick_style(hush_profiles[c->district_major].perc_style, PS_COUNT, &c->r_perc);
        gen_bass(c); gen_perc(c); gen_arp(c);
        for (int i = 0; i < 3; i++) gen_motif(c, &c->motifs[i]);
        c->n_motifs = 3;
        change_chord(m, 1);
        Patch p;
        if (!m->voices[V_TEX0].active || m->voices[V_TEX0].released) {
            patch_texture(&p, c, 0);
            eng_note(m, LAYER_DECAY, V_TEX0, &p, 72, 0.5f, 0.0f, 0, 0);
        }
        c->hum_district = -1;
        c->stats.bars++;
        return;
    }
    c->stats.bars++;

    /* section structure */
    if (--c->section_bars_left <= 0) next_section(c);

    /* district style changes take effect on phrase boundaries */
    if ((c->bar % 4) == 0) {
        const DistrictProfile *dp = &hush_profiles[c->district_major];
        if (c->bar % 16 == 0 || dp->perc_style[c->perc_style] < 1.0f) {
            int ps = pick_style(dp->perc_style, PS_COUNT, &c->r_perc);
            if (ps != c->perc_style || c->bar % 32 == 0) { c->perc_style = ps; gen_perc(c); }
            else mutate_perc(c);
        } else mutate_perc(c);
        if (dp->bass_style[c->bass_style] < 1.0f || c->bar % 32 == 0) {
            c->bass_style = pick_style(dp->bass_style, BS_COUNT, &c->r_pulse);
            /* pattern memory: sometimes return to an earlier bassline */
            if (c->bass_mem_n > 0 && rng_chance(&c->r_pulse, 0.25f)) c->bass = c->bass_mem[rng_int(&c->r_pulse, c->bass_mem_n)];
            else gen_bass(c);
        } else mutate_bass(c);
        if (c->bar % 8 == 0) {
            c->bass_mem[c->bass_mem_n < 4 ? c->bass_mem_n++ : rng_int(&c->r_pulse, 4)] = c->bass;
            gen_arp(c);
        }
    }
    /* texture hum follows the district: crossfade between the two hum voices */
    if (c->hum_district != c->district_major) {
        Patch p;
        patch_texture(&p, c, 1);
        if (c->hum_district >= 0) eng_release(m, V_TEX0 + 1 + c->hum_voice, 0);
        c->hum_voice ^= 1;
        eng_note(m, LAYER_DECAY, V_TEX0 + 1 + c->hum_voice, &p, 36, 0.5f, 0.0f, 0, 0);
        c->hum_district = c->district_major;
    }

    /* harmony */
    if (--c->chord_bars_left <= 0 || c->punct_type == MEV_MISSION_COMPLETE) change_chord(m, 0);
    if (--c->drone_bars <= 0) start_drones(m);
    /* occasional high shimmer partial from the atmosphere layer */
    if (c->bar % 4 == 2 && rng_chance(&c->r_atmos, 0.12f + 0.25f * c->s_alt + 0.2f * c->s_landmark[LM_CHOIR])) {
        Patch p;
        patch_drone(&p, c, 2);
        int n = 72 + ((c->chord.pcs[rng_int(&c->r_atmos, c->chord.n)] - 72) % 12 + 12) % 12;
        float pan = rng_bipolar(&c->r_atmos) * 0.8f;
        float gate = 16.0f * (float)rng_range(&c->r_atmos, 4, 10);
        eng_note(m, LAYER_ATMOS, eng_alloc(m, V_ATMOS0, V_ATMOS_N), &p, n, 0.07f, pan, gate, 0);
    }
    /* Black Reservoir: sub-octave drone beneath everything */
    if (c->s_landmark[LM_RESERVOIR] > 0.35f && c->bar % 8 == 0) {
        Patch p;
        patch_sub(&p, c);
        eng_note(m, LAYER_ATMOS, eng_alloc(m, V_ATMOS0, V_ATMOS_N), &p, c->drone_notes[0] - 12, 0.35f * c->s_landmark[LM_RESERVOIR], 0.0f, 16.0f * 7.0f, 0);
    }

    /* fragments: decide on a phrase */
    if (!c->phrase_active) {
        if (c->phrase_rest_bars > 0) c->phrase_rest_bars--;
        else {
            float lmp = 0; int lm = -1;
            for (int l = 0; l < LM_COUNT; l++) if (c->s_landmark[l] > lmp) { lmp = c->s_landmark[l]; lm = l; }
            float p = c->s_melody * 0.45f * c->energy[LAYER_FRAG] + 0.5f * lmp;
            if (c->punct_type == MEV_DISCOVERY || c->punct_type == MEV_SIGNAL_LOCK || c->punct_type == MEV_TRANSMISSION) p = 1.0f;
            if (rng_chance(&c->r_frag, p)) {
                uint32_t col = m->collected;
                if (c->punct_type == MEV_TRANSMISSION && c->punct_arg >= 0 && c->punct_arg < 32) {
                    /* a transmission just captured: play its motif now */
                    signal_motif(c->punct_arg, &c->phrase);
                    c->stats.motif_recall++;
                } else if (col && rng_chance(&c->r_frag, 0.22f)) {
                    /* a collected transmission returns, woven into the composition */
                    int cnt = __builtin_popcount(col), pick = rng_int(&c->r_frag, cnt), id = 0;
                    for (int b = 0; b < 32; b++) if ((col >> b) & 1) { if (pick-- == 0) { id = b; break; } }
                    signal_motif(id, &c->phrase);
                    c->stats.motif_recall++;
                } else if ((lm >= 0 && lmp > 0.25f && rng_chance(&c->r_frag, 0.35f + 0.6f * lmp)) || (c->punct_type == MEV_DISCOVERY && c->punct_arg >= 0 && c->punct_arg < LM_COUNT)) {
                    int which = c->punct_type == MEV_DISCOVERY ? c->punct_arg : lm;
                    c->phrase = *landmark_motif(which);
                    if (which == LM_ARCHIVE) c->archive_reps++;
                    c->stats.motif_recall++;
                } else {
                    float w3[3] = { 0.35f + 0.3f * c->s_motion, 0.3f, 0.35f };
                    if (c->n_motifs == 0) { w3[1] = 0; w3[2] = 0; }
                    int kind = rng_weighted(&c->r_frag, w3, 3);
                    if (kind == 0) {
                        Motif nm; gen_motif(c, &nm);
                        c->motifs[c->motif_next] = nm;
                        c->motif_next = (c->motif_next + 1) % 8;
                        if (c->n_motifs < 8) c->n_motifs++;
                        c->phrase = nm; c->stats.motif_new++;
                    } else if (kind == 1) {
                        c->phrase = c->motifs[rng_int(&c->r_frag, c->n_motifs)]; c->stats.motif_recall++;
                    } else {
                        vary_motif(c, &c->motifs[rng_int(&c->r_frag, c->n_motifs)], &c->phrase); c->stats.motif_variation++;
                    }
                    if (c->punct_type == MEV_SIGNAL_LOCK) c->phrase.inst = FI_RADIO;
                }
                if (c->punct_type == MEV_DISCOVERY || c->punct_type == MEV_SIGNAL_LOCK || c->punct_type == MEV_TRANSMISSION) c->punct_type = 0;
                c->phrase_active = 1;
                c->phrase_note = 0;
                c->phrase_step_left = rng_int(&c->r_frag, 3) * 2;
            }
        }
    }
    if (c->s_landmark[LM_ARCHIVE] < 0.05f) c->archive_reps = 0;

    /* decay process targets */
    static const float sect_erosion[SEC_COUNT] = {0.1f, 0.0f, 0.0f, 0.2f, 0.35f, 0.25f, 0.05f};
    c->erosion_target = h_clampf(0.06f + 0.3f * c->s_dark * c->s_dark + c->pb.erosion + sect_erosion[c->section]
                                 + 0.4f * c->s_landmark[LM_EXCHANGE] + 0.1f * (float)h_mini(c->archive_reps, 5) * c->s_landmark[LM_ARCHIVE]
                                 + rng_bipolar(&c->r_decay) * 0.05f, 0.0f, 1.0f);
    for (int l = 0; l < LAYER_COUNT; l++) c->drift_target[l] = rng_bipolar(&c->r_decay) * (2.0f + 16.0f * c->erosion);

    if (c->punct_type == MEV_ALERT) {
        eng_drum(m, DR_METAL, 0.7f, -0.5f, 2.0f, 0.2f, 0);
        eng_drum(m, DR_METAL, 0.5f, 0.5f, 2.0f, 0.6f, 0.5f);
        c->punct_type = 0;
    }
}

/* ---------------- per-step events ---------------- */

static void step_harmony(HushMusic *m) {
    Composer *c = &m->c;
    int s = c->step_in_bar;
    /* dub chord stabs on off-beats, fed into the delay */
    float e = c->energy[LAYER_HARMONY] * c->energy[LAYER_PULSE];
    float p = c->pb.stab_prob * e * (0.4f + c->s_density);
    if ((s == 2 || s == 6 || s == 10 || s == 14 || s == 7) && p > 0.02f && rng_chance(&c->r_harm, p * (s == 7 ? 0.3f : 1.0f))) {
        if (rng_chance(&c->r_decay, c->erosion * 0.3f)) return; /* eroded away */
        Patch pt;
        patch_stab(&pt, c);
        for (int i = 1; i < VOICING_N; i++) {
            int v = eng_alloc(m, V_HARM0, V_HARM_N);
            eng_note(m, LAYER_HARMONY, v, &pt, c->voicing[i], 0.11f, ((float)i - 2.0f) * 0.3f, 1.0f, 0);
        }
    }
}

static void step_pulse(HushMusic *m) {
    Composer *c = &m->c;
    int s = c->step_in_bar;
    float e = c->energy[LAYER_PULSE];
    const Scale *sc = cur_scale(c);
    int tpc = tonic_pc(c);
    /* bass */
    if (c->bass.on[s] && e > 0.08f) {
        int core = c->bass.accent[s] || s == 0;
        float p = core ? h_clampf(e * 2.0f, 0.0f, 1.0f) : h_clampf(e * 1.1f - 0.1f, 0.0f, 1.0f);
        if (c->s_landmark[LM_EXCHANGE] > 0.3f) p *= 0.5f;
        if (rng_chance(&c->r_pulse, p)) {
            int base = 28 + ((tpc - 28) % 12 + 12) % 12;  /* tonic in MIDI 28..39 */
            int note = theory_degree_note(sc, base, c->chord.degree + c->bass.deg[s]) + 12 * c->bass.oct[s];
            while (note > 47) note -= 12;
            while (note < 26) note += 12;
            float vel = (c->bass.accent[s] ? 0.42f : 0.32f) * (0.8f + 0.4f * h_minf(e, 1.0f));
            if (c->bass.slide[s] && m->voices[V_BASS].active && !m->voices[V_BASS].released) eng_legato(m, V_BASS, note, (float)c->bass.len[s]);
            else {
                Patch p2; patch_bass(&p2, c, c->bass_style);
                eng_note(m, LAYER_PULSE, V_BASS, &p2, note, vel, 0.0f, (float)c->bass.len[s], 0);
            }
            c->bass_last_note = note;
        }
    }
    /* arpeggiated sequence */
    if (e > 0.3f && c->district_major != DISTRICT_UNDERTOW) {
        int k = h_clampi((int)c->arp_k + (int)((c->s_density - 0.5f) * 4.0f), 2, 9);
        int pos = (s + 16 - c->arp_rot) % 16;
        if (((pos * k) % 16) < k && rng_chance(&c->r_pulse, h_clampf((e - 0.3f) * 1.6f, 0.0f, 0.9f))) {
            if (!rng_chance(&c->r_decay, c->erosion * 0.4f)) {
                int idx;
                if (c->arp_order == 0) idx = c->arp_idx % VOICING_N;
                else if (c->arp_order == 1) idx = VOICING_N - 1 - (c->arp_idx % VOICING_N);
                else idx = rng_int(&c->r_pulse, VOICING_N);
                c->arp_idx++;
                Patch p; patch_arp(&p, c);
                int note = c->voicing[idx] + 12;
                if (note > 88) note -= 12;
                /* draws are taken into locals: argument evaluation order is unspecified in C */
                float vel = 0.16f + 0.06f * rng_float(&c->r_pulse);
                float pan = rng_bipolar(&c->r_pulse) * 0.6f;
                eng_note(m, LAYER_PULSE, eng_alloc(m, V_ARP0, V_ARP_N), &p, note, vel, pan, 1.0f, 0);
            }
        }
    }
}

static const float drum_level[DR_COUNT] = {0.85f, 0.5f, 0.22f, 0.18f, 0.32f, 0.3f, 0.75f, 0.28f};

static void step_perc(HushMusic *m) {
    Composer *c = &m->c;
    float e = c->energy[LAYER_PERC];
    float st9 = c->s_landmark[LM_STATION9];
    if (e < 0.04f && st9 < 0.1f) return;
    int s = c->step_in_bar;
    float swing = c->pb.swing + (c->perc_style == PS_BROKEN ? 0.12f : 0.0f);
    float delay = (s & 1) ? swing : 0.0f;
    int fill = (c->bar % 8) == 7 && s >= 12 && e > 0.5f;
    for (int d = 0; d < DR_COUNT; d++) {
        int idx = s;
        if (d == DR_KICK || d == DR_SNARE) {
            idx = (s + 16 - c->displace) % 16;
            if (c->halftime) idx = (idx / 2) + ((c->bar & 1) ? 8 : 0);
        }
        float v = c->lanes[d].vel[idx];
        if (d == DR_METAL) v = h_maxf(v, ((((s + 3) * 5) % 16) < 5 ? 0.6f : 0.0f) * st9 + c->pb.metal * 0.05f * (float)(s % 4 == 3));
        if (d == DR_CLICK) v = h_maxf(v, ((s * 7) % 16 < 7 ? 0.5f : 0.0f) * st9);
        if (fill && (d == DR_SNARE || d == DR_CLICK)) v = h_maxf(v, 0.35f);
        if (v <= 0.0f) continue;
        float p;
        if (v >= 0.9f) p = h_clampf(e * 2.2f - 0.2f, 0.0f, 1.0f);      /* structural hits */
        else p = h_clampf(v * (e * 1.4f), 0.0f, 0.95f);
        if (d == DR_HAT || d == DR_OPENHAT) p *= h_clampf((e - 0.2f) * 2.0f, 0.0f, 1.0f);
        if (!rng_chance(&c->r_perc, p)) continue;
        float vel = drum_level[d] * (0.65f + 0.35f * v) * (0.85f + 0.3f * rng_float(&c->r_perc));
        float pan = 0;
        if (d == DR_HAT || d == DR_OPENHAT) pan = 0.25f;
        else if (d == DR_RIM || d == DR_CLICK) pan = rng_bipolar(&c->r_perc) * 0.6f;
        else if (d == DR_METAL) pan = rng_bipolar(&c->r_perc) * 0.8f;
        float decay = 1.0f + (d == DR_METAL ? c->pb.metal : 0.0f) + c->s_landmark[LM_RESERVOIR] * 2.0f;
        float tone = d == DR_METAL ? 0.2f + 0.6f * rng_float(&c->r_perc) : 0.5f - 0.3f * c->s_dark;
        if (rng_chance(&c->r_decay, c->erosion * 0.25f) && d != DR_KICK) continue;
        eng_drum(m, d, vel, pan, decay, tone, delay + (d == DR_HAT ? rng_float(&c->r_perc) * 0.04f : 0.0f));
    }
}

static void step_frag(HushMusic *m) {
    Composer *c = &m->c;
    if (!c->phrase_active) return;
    if (c->phrase_step_left > 0) { c->phrase_step_left--; return; }
    if (c->phrase_note >= c->phrase.n) {
        c->phrase_active = 0;
        c->phrase_rest_bars = 1 + (int)((float)rng_int(&c->r_frag, 4) * (1.6f - c->s_melody));
        return;
    }
    int i = c->phrase_note++;
    c->phrase_step_left = h_maxi(1, c->phrase.dur[i]) - 1;
    float drop = c->erosion * 0.35f + (c->phrase.inst == FI_RADIO ? 0.1f * (float)h_mini(c->archive_reps, 6) * c->s_landmark[LM_ARCHIVE] : 0.0f);
    if (rng_chance(&c->r_decay, drop)) return;
    const Scale *sc = cur_scale(c);
    int tonic = 60 + tonic_pc(c);
    if (tonic > 66) tonic -= 12;
    int note = theory_degree_note(sc, tonic, c->phrase.deg[i]);
    while (note > 88) note -= 12;
    while (note < 55) note += 12;
    Patch p;
    patch_frag(&p, c, c->phrase.inst);
    float vel = (float)c->phrase.vel[i] / 127.0f * 0.42f;
    float gate = (float)h_maxi(1, c->phrase.dur[i]) * 0.8f;
    eng_note(m, LAYER_FRAG, eng_alloc(m, V_FRAG0, V_FRAG_N), &p, note, vel, rng_bipolar(&c->r_frag) * 0.5f, gate, 0);
}

/* ---------------- public (internal) entry points ---------------- */

void composer_init(HushMusic *m, uint32_t seed) {
    Composer *c = &m->c;
    MusicParams P = c->P;
    MusicEnv E = c->E;
    int keep_smoothed = c->bpm > 0;
    Composer old = *c;
    memset(c, 0, sizeof(*c));
    c->P = P; c->E = E;
    c->seed = seed;
    rng_seed(&c->r_struct, seed, 1);
    rng_seed(&c->r_harm, seed, 2);
    rng_seed(&c->r_pulse, seed, 3);
    rng_seed(&c->r_perc, seed, 4);
    rng_seed(&c->r_frag, seed, 5);
    rng_seed(&c->r_decay, seed, 6);
    rng_seed(&c->r_atmos, seed, 7);
    rng_seed(&c->r_misc, seed, 8);
    for (int i = 0; i < VOICING_N; i++) c->pad_voice[i] = -1;
    if (keep_smoothed) {
        /* re-seeding mid-flight keeps the world/parameter state so nothing jumps */
        c->bpm = old.bpm;
        c->s_alt = old.s_alt; c->s_speed = old.s_speed; c->s_speed_slow = old.s_speed_slow; c->s_rain = old.s_rain;
        c->s_pursuit = old.s_pursuit; c->s_traffic = old.s_traffic; c->s_canyon = old.s_canyon; c->s_signal = old.s_signal;
        c->s_scan = old.s_scan; c->s_fog = old.s_fog;
        memcpy(c->s_district, old.s_district, sizeof(c->s_district));
        memcpy(c->s_landmark, old.s_landmark, sizeof(c->s_landmark));
        c->s_density = old.s_density; c->s_dark = old.s_dark; c->s_motion = old.s_motion; c->s_complex = old.s_complex;
        c->s_rhythm = old.s_rhythm; c->s_melody = old.s_melody; c->s_seqvar = old.s_seqvar;
        memcpy(c->energy, old.energy, sizeof(c->energy));
        c->stats = old.stats;
        c->event_hash = old.event_hash; c->event_count = old.event_count;
        c->step = old.step; c->bar = old.bar;
        for (int i = 0; i < VOICING_N; i++) c->pad_voice[i] = old.pad_voice[i];
        memcpy(c->voicing, old.voicing, sizeof(c->voicing));
    } else {
        c->bpm = h_clampf(P.tempo, 50.0f, 140.0f);
        int dist = h_clampi(E.district, 0, DISTRICT_COUNT - 1);
        c->s_district[dist] = 1.0f;
        c->district_major = dist;
        c->s_alt = 0.5f; c->s_speed = c->s_speed_slow = 0.25f; c->s_traffic = 0.3f; c->s_canyon = 0.3f;
        c->s_density = P.density; c->s_dark = P.darkness; c->s_motion = P.motion; c->s_complex = P.complexity;
        c->s_rhythm = P.rhythm_prob; c->s_melody = P.melody_prob; c->s_seqvar = P.seq_variation;
        for (int l = 0; l < LAYER_COUNT; l++) c->energy[l] = 0.3f;
        c->event_hash = H_FNV_INIT;
        profile_blend(c->s_district, &c->pb);
        c->bpm = h_clampf(P.tempo, 50.0f, 140.0f) * c->pb.tempo_mul;
    }
    c->samples_per_step = composer_samples_per_step(m);
    c->step_countdown = 0;
}

float composer_samples_per_step(HushMusic *m) {
    return m->sr * 60.0f / (h_maxf(m->c.bpm, 30.0f) * 4.0f);
}

void composer_step(HushMusic *m) {
    Composer *c = &m->c;
    update_controls(m);
    if (c->step_in_bar == 0) bar_tick(m);
    step_harmony(m);
    step_pulse(m);
    step_perc(m);
    step_frag(m);
    /* decay: apply slow pitch drift to sustained layers */
    for (int i = 0; i < MAX_VOICES; i++) {
        Voice *v = &m->voices[i];
        if (!v->active) continue;
        if (v->layer == LAYER_ATMOS || v->layer == LAYER_HARMONY || v->layer == LAYER_FRAG) v->pitch_drift = c->drift[v->layer];
    }
    c->step++;
    c->step_in_bar++;
    if (c->step_in_bar >= 16) { c->step_in_bar = 0; c->bar++; }
    c->samples_per_step = composer_samples_per_step(m);
}
