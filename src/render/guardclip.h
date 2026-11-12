/* HUSH — guard-band clipping (pure C, host-testable).
 *
 * The PSP GE clips primitives against the near plane, then throws away the WHOLE
 * primitive if any resulting vertex falls outside its 4096 x 4096 drawing area
 * (here: more than 2048 px from our viewport centre). Large triangles near the
 * camera therefore vanish on real hardware (emulators usually draw them).
 * gc_clip_tri() clips such triangles against a smaller "safe" frustum whose side
 * planes sit at 80 % of the guard band, so everything it emits is accepted. */
#ifndef HUSH_GUARDCLIP_H
#define HUSH_GUARDCLIP_H

#include <stdint.h>
#include "../core/hmath.h"

/* identical layout to VtxTCP (GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF) */
typedef struct { float u, v; uint32_t color; float x, y, z; } GcVtx;
typedef struct { float cx, cy, cz; GcVtx v; } GcCV;
typedef struct { V3 e, f, r, u; float kx, ky, n; } GuardClip;

static inline void gc_setup(GuardClip *g, V3 eye, V3 fwd, V3 right, V3 up, float tan_half_vfov, float scr_w, float scr_h, float znear) {
    g->e = eye; g->f = fwd; g->r = right; g->u = up;
    float th = tan_half_vfov * scr_w / scr_h;
    g->kx = 0.8f * (2048.0f / (scr_w * 0.5f)) * th;
    g->ky = 0.8f * (2048.0f / (scr_h * 0.5f)) * tan_half_vfov;
    g->n = znear * 1.15f;
}

static inline void gc_cvx(const GuardClip *g, GcCV *o, const GcVtx *v) {
    float dx = v->x - g->e.x, dy = v->y - g->e.y, dz = v->z - g->e.z;
    o->cx = dx * g->r.x + dy * g->r.y + dz * g->r.z;
    o->cy = dx * g->u.x + dy * g->u.y + dz * g->u.z;
    o->cz = dx * g->f.x + dy * g->f.y + dz * g->f.z;
    o->v = *v;
}

static inline float gc_pdist(const GuardClip *g, const GcCV *c, int p) {
    switch (p) {
    case 0: return c->cz - g->n;
    case 1: return g->kx * c->cz - c->cx;
    case 2: return g->kx * c->cz + c->cx;
    case 3: return g->ky * c->cz - c->cy;
    default: return g->ky * c->cz + c->cy;
    }
}

static inline uint32_t gc_col_lerp(uint32_t a, uint32_t b, float t) {
    uint32_t o = 0;
    for (int s = 0; s < 32; s += 8) {
        float x = (float)((a >> s) & 0xFF), y = (float)((b >> s) & 0xFF);
        o |= ((uint32_t)(x + (y - x) * t + 0.5f) & 0xFF) << s;
    }
    return o;
}

static inline void gc_lerp(GcCV *o, const GcCV *a, const GcCV *b, float t) {
    o->cx = a->cx + (b->cx - a->cx) * t; o->cy = a->cy + (b->cy - a->cy) * t; o->cz = a->cz + (b->cz - a->cz) * t;
    o->v.x = a->v.x + (b->v.x - a->v.x) * t; o->v.y = a->v.y + (b->v.y - a->v.y) * t; o->v.z = a->v.z + (b->v.z - a->v.z) * t;
    o->v.u = a->v.u + (b->v.u - a->v.u) * t; o->v.v = a->v.v + (b->v.v - a->v.v) * t;
    o->v.color = gc_col_lerp(a->v.color, b->v.color, t);
}

/* Returns 1 if the triangle is already safe (nothing appended). Otherwise appends its clipped
 * fan (nothing if it is entirely outside) to out[*n..] and returns 0. Winding is preserved. */
static inline int gc_clip_tri(const GuardClip *g, const GcVtx *a, const GcVtx *b, const GcVtx *c, GcVtx *out, int *n_out, int cap) {
    GcCV b0[10], b1[10];
    gc_cvx(g, &b0[0], a); gc_cvx(g, &b0[1], b); gc_cvx(g, &b0[2], c);
    int safe = 1;
    for (int p = 0; p < 5 && safe; p++)
        for (int i = 0; i < 3; i++) if (gc_pdist(g, &b0[i], p) < 0.0f) { safe = 0; break; }
    if (safe) return 1;
    GcCV *in = b0, *ob = b1;
    int n = 3;
    for (int p = 0; p < 5; p++) {
        int m = 0;
        for (int i = 0; i < n; i++) {
            const GcCV *A = &in[i], *B = &in[(i + 1) % n];
            float da = gc_pdist(g, A, p), db = gc_pdist(g, B, p);
            if (da >= 0.0f) ob[m++] = *A;
            if ((da >= 0.0f) != (db >= 0.0f) && m < 10) gc_lerp(&ob[m++], A, B, da / (da - db));
        }
        n = m;
        GcCV *t = in; in = ob; ob = t;
        if (n < 3) return 0;
    }
    if (*n_out + (n - 2) * 3 > cap) return 0;
    for (int i = 1; i < n - 1; i++) { out[(*n_out)++] = in[0].v; out[(*n_out)++] = in[i].v; out[(*n_out)++] = in[i + 1].v; }
    return 0;
}

#endif
