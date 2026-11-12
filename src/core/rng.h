/* HUSH — deterministic pseudorandom numbers.
 *
 * Generator: PCG32 (O'Neill 2014, "PCG-XSH-RR 64/32"), 64-bit LCG state with
 * multiplier 6364136223846793005 and a per-stream odd increment.
 * Stream derivation: SplitMix64 of (seed, stream id), so that every musical
 * layer owns an independent, reproducible stream. Changing what one layer
 * consumes never shifts another layer's sequence.
 *
 * Everything here is integer arithmetic, so sequences are bit-identical on the
 * PSP (MIPS Allegrex) and on x86-64/ARM hosts. */
#ifndef HUSH_RNG_H
#define HUSH_RNG_H

#include <stdint.h>

typedef struct { uint64_t state, inc; } Rng;

static inline uint64_t h_splitmix64(uint64_t *x) {
    uint64_t z = (*x += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

static inline uint32_t rng_u32(Rng *r) {
    uint64_t old = r->state;
    r->state = old * 6364136223846793005ULL + r->inc;
    uint32_t xorshifted = (uint32_t)(((old >> 18u) ^ old) >> 27u);
    uint32_t rot = (uint32_t)(old >> 59u);
    return (xorshifted >> rot) | (xorshifted << ((32u - rot) & 31u));
}

static inline void rng_seed(Rng *r, uint32_t seed, uint32_t stream) {
    uint64_t sm = ((uint64_t)seed << 32) ^ (uint64_t)stream * 0xD1B54A32D192ED03ULL;
    uint64_t s = h_splitmix64(&sm);
    uint64_t i = h_splitmix64(&sm);
    r->state = 0;
    r->inc = (i << 1u) | 1u;
    rng_u32(r);
    r->state += s;
    rng_u32(r);
}

/* Uniform float in [0,1) with 24 bits of precision (exact in single float). */
static inline float rng_float(Rng *r) { return (float)(rng_u32(r) >> 8) * (1.0f / 16777216.0f); }
static inline float rng_bipolar(Rng *r) { return rng_float(r) * 2.0f - 1.0f; }
/* Integer in [0, n). Uses multiply-shift (no modulo bias worth caring about at these ranges). */
static inline int rng_int(Rng *r, int n) { return n <= 1 ? 0 : (int)(((uint64_t)rng_u32(r) * (uint64_t)n) >> 32); }
static inline int rng_range(Rng *r, int lo, int hi) { return lo + rng_int(r, hi - lo + 1); }
static inline int rng_chance(Rng *r, float p) { return rng_float(r) < p; }
/* Weighted choice over n non-negative weights; returns 0 if all weights are zero. */
static inline int rng_weighted(Rng *r, const float *w, int n) {
    float total = 0;
    for (int i = 0; i < n; i++) total += w[i] > 0 ? w[i] : 0;
    if (total <= 0) return 0;
    float x = rng_float(r) * total;
    for (int i = 0; i < n; i++) {
        float wi = w[i] > 0 ? w[i] : 0;
        if (x < wi) return i;
        x -= wi;
    }
    return n - 1;
}

/* FNV-1a 32-bit hash, used to fingerprint generated event streams. */
static inline uint32_t h_fnv1a(uint32_t h, uint32_t v) {
    for (int i = 0; i < 4; i++) { h ^= (v >> (i * 8)) & 0xFF; h *= 16777619u; }
    return h;
}
#define H_FNV_INIT 2166136261u

#endif
