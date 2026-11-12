/* HUSH — music theory helpers: modes, scale-derived chords, voice leading. */
#ifndef HUSH_THEORY_H
#define HUSH_THEORY_H

#include <stdint.h>
#include "../core/rng.h"

typedef struct { int n; int8_t pc[7]; } Scale;
const Scale *theory_scale(int mode);

/* Absolute MIDI note of scale degree d (any integer, wraps octaves) relative to tonic midi note. */
int theory_degree_note(const Scale *s, int tonic, int d);
/* Nearest scale tone to a MIDI note (snaps down on ties). */
int theory_snap(const Scale *s, int tonic_pc, int note);

#define CHORD_MAX 5
typedef struct {
    int degree;          /* scale degree of the root */
    int root_pc;         /* absolute pitch class of the root */
    int n;
    int8_t pcs[CHORD_MAX]; /* pitch classes, [0] is root */
    int8_t required;     /* bitmask of pcs indices that must be voiced */
    char name[16];
} Chord;

/* Builds a chord on a scale degree with extensions chosen from rng (complexity, tension 0..1). */
void theory_build_chord(Chord *c, const Scale *s, int tonic_pc, int degree, float complexity, float tension, float sus_prob, Rng *r);
/* Weighted choice of the next root degree given the previous one. */
int theory_next_degree(const Scale *s, int tonic_pc, int prev, float complexity, Rng *r);

#define VOICING_N 4
/* Smooth voice leading: moves prev[] (sorted MIDI notes, or all zero for none) onto chord c. */
void theory_voice_lead(const Chord *c, const int *prev, int *out, int lo, int hi);

#endif
