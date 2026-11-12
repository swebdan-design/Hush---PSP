/* HUSH — small math helpers shared by PSP and host builds.
 * Single precision only: the PSP's Allegrex FPU has no double support. */
#ifndef HUSH_HMATH_H
#define HUSH_HMATH_H

#include <math.h>
#include <stdint.h>

#define H_PI 3.14159265358979f
#define H_TAU 6.28318530717959f

static inline float h_clampf(float x, float a, float b) { return x < a ? a : (x > b ? b : x); }
static inline float h_lerpf(float a, float b, float t) { return a + (b - a) * t; }
static inline float h_minf(float a, float b) { return a < b ? a : b; }
static inline float h_maxf(float a, float b) { return a > b ? a : b; }
static inline int h_mini(int a, int b) { return a < b ? a : b; }
static inline int h_maxi(int a, int b) { return a > b ? a : b; }
static inline int h_clampi(int x, int a, int b) { return x < a ? a : (x > b ? b : x); }
static inline float h_absf(float x) { return x < 0 ? -x : x; }
static inline float h_signf(float x) { return x < 0 ? -1.0f : 1.0f; }
static inline float h_smoothstep(float a, float b, float x) {
    float t = h_clampf((x - a) / (b - a), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}
/* Frame-rate independent exponential approach: rate in 1/seconds. */
static inline float h_approach(float cur, float target, float rate, float dt) {
    float k = 1.0f - expf(-rate * dt);
    return cur + (target - cur) * k;
}
static inline float h_wrap_angle(float a) {
    while (a > H_PI) a -= H_TAU;
    while (a < -H_PI) a += H_TAU;
    return a;
}

typedef struct { float x, y, z; } V3;

static inline V3 v3(float x, float y, float z) { V3 r = {x, y, z}; return r; }
static inline V3 v3_add(V3 a, V3 b) { return v3(a.x + b.x, a.y + b.y, a.z + b.z); }
static inline V3 v3_sub(V3 a, V3 b) { return v3(a.x - b.x, a.y - b.y, a.z - b.z); }
static inline V3 v3_scale(V3 a, float s) { return v3(a.x * s, a.y * s, a.z * s); }
static inline V3 v3_madd(V3 a, V3 b, float s) { return v3(a.x + b.x * s, a.y + b.y * s, a.z + b.z * s); }
static inline float v3_dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static inline V3 v3_cross(V3 a, V3 b) { return v3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x); }
static inline float v3_len(V3 a) { return sqrtf(v3_dot(a, a)); }
static inline float v3_len2(V3 a) { return v3_dot(a, a); }
static inline V3 v3_norm(V3 a) { float l = v3_len(a); return l > 1e-6f ? v3_scale(a, 1.0f / l) : v3(0, 0, 0); }
static inline V3 v3_lerp(V3 a, V3 b, float t) { return v3(h_lerpf(a.x, b.x, t), h_lerpf(a.y, b.y, t), h_lerpf(a.z, b.z, t)); }
static inline float v3_dist(V3 a, V3 b) { return v3_len(v3_sub(a, b)); }
static inline float v3_dist_xz(V3 a, V3 b) { float dx = a.x - b.x, dz = a.z - b.z; return sqrtf(dx * dx + dz * dz); }

#endif
