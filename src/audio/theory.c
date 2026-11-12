/* HUSH — modes, chords and voice leading. See theory.h. */
#include "theory.h"
#include "music.h"
#include <string.h>
#include <stdio.h>

static const Scale scales[MODE_COUNT] = {
    {7, {0, 2, 3, 5, 7, 8, 10}},   /* aeolian */
    {7, {0, 2, 3, 5, 7, 9, 10}},   /* dorian */
    {7, {0, 1, 3, 5, 7, 8, 10}},   /* phrygian */
    {7, {0, 2, 4, 6, 7, 9, 11}},   /* lydian */
    {7, {0, 2, 3, 5, 7, 8, 11}},   /* harmonic minor */
    {5, {0, 3, 5, 7, 10}},         /* minor pentatonic */
    {5, {0, 2, 5, 7, 10}},         /* suspended pentatonic */
    {7, {0, 1, 3, 5, 6, 8, 10}},   /* locrian */
};

const char *const hush_mode_names[MODE_COUNT] = {
    "AEOLIAN", "DORIAN", "PHRYGIAN", "LYDIAN", "HARM MINOR", "MIN PENT", "SUSPENDED", "LOCRIAN"
};
const char *const hush_note_names[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

const Scale *theory_scale(int mode) {
    if (mode < 0 || mode >= MODE_COUNT) mode = 0;
    return &scales[mode];
}

static int floordiv(int a, int b) { int q = a / b; if ((a % b != 0) && ((a < 0) != (b < 0))) q--; return q; }

int theory_degree_note(const Scale *s, int tonic, int d) {
    int oct = floordiv(d, s->n);
    int idx = d - oct * s->n;
    return tonic + oct * 12 + s->pc[idx];
}

int theory_snap(const Scale *s, int tonic_pc, int note) {
    int best = note, bd = 99;
    for (int k = -6; k <= 6; k++) {
        int pc = ((note + k - tonic_pc) % 12 + 12) % 12;
        for (int i = 0; i < s->n; i++)
            if (s->pc[i] == pc) {
                int ad = k < 0 ? -k : k;
                if (ad < bd || (ad == bd && k < 0)) { bd = ad; best = note + k; }
            }
    }
    return best;
}

static int pc_of_degree(const Scale *s, int tonic_pc, int d) {
    return ((theory_degree_note(s, tonic_pc, d)) % 12 + 12) % 12;
}

void theory_build_chord(Chord *c, const Scale *s, int tonic_pc, int degree, float complexity, float tension, float sus_prob, Rng *r) {
    memset(c, 0, sizeof(*c));
    c->degree = degree;
    int root = pc_of_degree(s, tonic_pc, degree);
    c->root_pc = root;
    int third_step = 2;
    int sus = 0;
    if (rng_chance(r, sus_prob)) { sus = rng_chance(r, 0.5f) ? 1 : 3; third_step = sus; }
    int pcs[CHORD_MAX], n = 0;
    pcs[n++] = root;
    pcs[n++] = pc_of_degree(s, tonic_pc, degree + third_step);
    pcs[n++] = pc_of_degree(s, tonic_pc, degree + 4);
    int has7 = 0, has9 = 0, has11 = 0;
    if (rng_chance(r, 0.3f + 0.55f * complexity)) { pcs[n++] = pc_of_degree(s, tonic_pc, degree + 6); has7 = 1; }
    if (n < CHORD_MAX && !sus && rng_chance(r, 0.1f + 0.5f * complexity + 0.2f * tension)) { pcs[n++] = pc_of_degree(s, tonic_pc, degree + 8); has9 = 1; }
    else if (n < CHORD_MAX && rng_chance(r, 0.25f * complexity + 0.3f * tension)) { pcs[n++] = pc_of_degree(s, tonic_pc, degree + 10); has11 = 1; }
    /* tension: occasionally add a semitone-clash colour tone (b9 over root) */
    if (n < CHORD_MAX && tension > 0.5f && rng_chance(r, (tension - 0.5f) * 0.5f)) { pcs[n++] = (root + 1) % 12; has9 = 2; }
    c->n = n;
    for (int i = 0; i < n; i++) c->pcs[i] = (int8_t)pcs[i];
    /* required: third/sus (1), and the 7th if present */
    c->required = (int8_t)(1 << 1);
    if (has7) c->required |= (int8_t)(1 << 3);

    int third = (pcs[1] - root + 12) % 12;
    int fifth = (pcs[2] - root + 12) % 12;
    const char *q = "";
    if (sus == 1) q = "sus2";
    else if (sus == 3) q = "sus4";
    else if (third == 3 && fifth == 6) q = "dim";
    else if (third == 3) q = "m";
    else if (third == 4 && fifth == 8) q = "+";
    const char *ext = "";
    if (has7) {
        int sev = (pcs[3] - root + 12) % 12;
        if (has9 == 1) ext = sev == 11 ? "maj9" : "9";
        else ext = sev == 11 ? "maj7" : "7";
    } else if (has9 == 1) ext = "add9";
    if (has11) ext = has7 ? "11" : "add11";
    if (has9 == 2) ext = "b9";
    snprintf(c->name, sizeof(c->name), "%s%s%s", hush_note_names[root], q, ext);
}

static const float trans7[7][7] = {
    {0.0f, 0.3f, 1.5f, 2.5f, 1.2f, 3.0f, 2.5f},
    {1.5f, 0.0f, 0.8f, 0.8f, 2.5f, 0.8f, 1.5f},
    {1.5f, 0.2f, 0.0f, 2.0f, 0.8f, 2.5f, 1.5f},
    {3.0f, 0.3f, 1.0f, 0.0f, 1.5f, 1.5f, 2.5f},
    {3.0f, 0.3f, 1.0f, 1.5f, 0.0f, 2.0f, 1.0f},
    {2.0f, 0.4f, 1.5f, 2.5f, 1.5f, 0.0f, 3.0f},
    {3.5f, 0.3f, 2.0f, 1.5f, 0.8f, 1.5f, 0.0f},
};
static const float trans5[5][5] = {
    {0.0f, 2.0f, 2.0f, 1.5f, 2.5f},
    {3.0f, 0.0f, 1.5f, 1.0f, 1.5f},
    {2.5f, 1.5f, 0.0f, 2.0f, 1.0f},
    {3.0f, 1.0f, 1.5f, 0.0f, 1.5f},
    {3.0f, 1.0f, 1.5f, 2.0f, 0.0f},
};

int theory_next_degree(const Scale *s, int tonic_pc, int prev, float complexity, Rng *r) {
    float w[7];
    int n = s->n;
    prev = ((prev % n) + n) % n;
    for (int j = 0; j < n; j++) {
        w[j] = n == 7 ? trans7[prev][j] : trans5[prev][j];
        if (n == 7) {
            int root = pc_of_degree(s, tonic_pc, j);
            int fifth = (pc_of_degree(s, tonic_pc, j + 4) - root + 12) % 12;
            if (fifth != 7) w[j] *= 0.12f + complexity * 0.5f;   /* diminished/augmented roots are rare */
            if (j == 1 || j == 2 || j == 4) w[j] *= 0.7f + complexity * 0.8f;
        }
    }
    return rng_weighted(r, w, n);
}

static int nearest_with_pc(int from, int pc, int lo, int hi) {
    int best = -1, bd = 999;
    for (int note = lo; note <= hi; note++) {
        if (((note % 12) + 12) % 12 != pc) continue;
        int d = note > from ? note - from : from - note;
        if (d < bd) { bd = d; best = note; }
    }
    return best;
}

void theory_voice_lead(const Chord *c, const int *prev, int *out, int lo, int hi) {
    int v[VOICING_N];
    int have_prev = 0;
    for (int i = 0; i < VOICING_N; i++) if (prev[i] > 0) have_prev = 1;
    if (!have_prev) {
        /* open initial voicing: root, fifth, third/sus up an octave, then 7th or root */
        int base = lo + ((c->root_pc - lo) % 12 + 12) % 12;
        v[0] = base;
        v[1] = nearest_with_pc(base + 7, c->pcs[2], lo, hi);
        v[2] = nearest_with_pc(base + 15, c->pcs[1], lo, hi);
        v[3] = nearest_with_pc(base + 22, c->n > 3 ? c->pcs[3] : c->pcs[0], lo, hi);
    } else {
        /* each voice moves to the nearest chord tone */
        for (int i = 0; i < VOICING_N; i++) {
            int best = prev[i], bd = 999;
            for (int k = 0; k < c->n; k++) {
                int cand = nearest_with_pc(prev[i], c->pcs[k], lo, hi);
                if (cand < 0) continue;
                int d = cand > prev[i] ? cand - prev[i] : prev[i] - cand;
                if (d < bd) { bd = d; best = cand; }
            }
            v[i] = best;
        }
        /* ensure required tones are present, replacing the cheapest duplicate */
        for (int k = 0; k < c->n; k++) {
            if (!(c->required & (1 << k))) continue;
            int present = 0;
            for (int i = 0; i < VOICING_N; i++) if (((v[i] % 12) + 12) % 12 == c->pcs[k]) present = 1;
            if (present) continue;
            int bi = -1, bc = 999, bn = 0;
            for (int i = 0; i < VOICING_N; i++) {
                int dup = 0;
                for (int j = 0; j < VOICING_N; j++) if (j != i && v[j] % 12 == v[i] % 12) dup = 1;
                int cand = nearest_with_pc(prev[i], c->pcs[k], lo, hi);
                if (cand < 0) continue;
                int cost = (cand > prev[i] ? cand - prev[i] : prev[i] - cand) + (dup ? 0 : 6);
                if (cost < bc) { bc = cost; bi = i; bn = cand; }
            }
            if (bi >= 0) v[bi] = bn;
        }
    }
    /* sort and separate unisons */
    for (int i = 0; i < VOICING_N; i++)
        for (int j = i + 1; j < VOICING_N; j++)
            if (v[j] < v[i]) { int t = v[i]; v[i] = v[j]; v[j] = t; }
    for (int i = 1; i < VOICING_N; i++)
        if (v[i] == v[i - 1]) { if (v[i] + 12 <= hi) v[i] += 12; else if (v[i - 1] - 12 >= lo) v[i - 1] -= 12; }
    for (int i = 0; i < VOICING_N; i++)
        for (int j = i + 1; j < VOICING_N; j++)
            if (v[j] < v[i]) { int t = v[i]; v[i] = v[j]; v[j] = t; }
    memcpy(out, v, sizeof(v));
}
