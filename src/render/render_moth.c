/* HUSH — the Moth: a compact four-nacelle vertical-thrust observation craft.
 * Modelled procedurally from primitives with baked lighting; nacelles tilt with
 * thrust, running lights and the strobe blink, thrust glow follows the engines. */
#include "render_moth.h"
#include "render_world.h"
#include <pspkernel.h>
#include <psputils.h>
#include <string.h>

#define BODY_MAX 1200
#define NAC_MAX 300
static VtxCP __attribute__((aligned(16))) g_body[2][BODY_MAX];
static VtxCP __attribute__((aligned(16))) g_nac[2][NAC_MAX];
static int g_nbody, g_nnac;
static VtxCP *g_cur;
static int *g_curn, g_curmax;

static const V3 LDIR = { 0.35f, 0.8f, 0.45f };

static uint32_t lit(uint32_t c, V3 n) {
    float d = h_maxf(0.0f, v3_dot(v3_norm(n), LDIR));
    return col_scale(c, 0.38f + 0.75f * d);
}

static void tri(V3 a, V3 b, V3 c, uint32_t col) {
    if (*g_curn + 3 > g_curmax) return;
    V3 n = v3_cross(v3_sub(b, a), v3_sub(c, a));
    uint32_t lc = lit(col, n);
    VtxCP *v = &g_cur[*g_curn];
    v[0].color = lc; v[0].x = a.x; v[0].y = a.y; v[0].z = a.z;
    v[1].color = lc; v[1].x = b.x; v[1].y = b.y; v[1].z = b.z;
    v[2].color = lc; v[2].x = c.x; v[2].y = c.y; v[2].z = c.z;
    *g_curn += 3;
}
static void quadm(V3 a, V3 b, V3 c, V3 d, uint32_t col) { tri(a, b, c, col); tri(a, c, d, col); }

static void box(V3 c, V3 h, uint32_t col) {
    V3 p[8] = {
        {c.x - h.x, c.y - h.y, c.z - h.z}, {c.x + h.x, c.y - h.y, c.z - h.z}, {c.x + h.x, c.y + h.y, c.z - h.z}, {c.x - h.x, c.y + h.y, c.z - h.z},
        {c.x - h.x, c.y - h.y, c.z + h.z}, {c.x + h.x, c.y - h.y, c.z + h.z}, {c.x + h.x, c.y + h.y, c.z + h.z}, {c.x - h.x, c.y + h.y, c.z + h.z} };
    quadm(p[1], p[2], p[6], p[5], col); quadm(p[4], p[7], p[3], p[0], col);
    quadm(p[3], p[7], p[6], p[2], col); quadm(p[4], p[0], p[1], p[5], col);
    quadm(p[5], p[6], p[7], p[4], col); quadm(p[0], p[3], p[2], p[1], col);
}

/* prism along z with an octagonal section; r0 at z0, r1 at z1, ys = vertical squash */
static void prism(float z0, float z1, float r0, float r1, float ys, float yc, uint32_t col, int cap0, int cap1) {
    const int N = 8;
    for (int i = 0; i < N; i++) {
        float a0 = (float)i * H_TAU / N + H_PI / N, a1 = (float)(i + 1) * H_TAU / N + H_PI / N;
        V3 A = v3(cosf(a0) * r0, yc + sinf(a0) * r0 * ys, z0), B = v3(cosf(a1) * r0, yc + sinf(a1) * r0 * ys, z0);
        V3 C = v3(cosf(a1) * r1, yc + sinf(a1) * r1 * ys, z1), D = v3(cosf(a0) * r1, yc + sinf(a0) * r1 * ys, z1);
        quadm(A, B, C, D, col);
        if (cap0) tri(v3(0, yc, z0), B, A, col);
        if (cap1) tri(v3(0, yc, z1), D, C, col);
    }
}

/* vertical cylinder (nacelle) centred at origin */
static void ring(float r, float y0, float y1, uint32_t outer, uint32_t inner) {
    const int N = 10;
    for (int i = 0; i < N; i++) {
        float a0 = (float)i * H_TAU / N, a1 = (float)(i + 1) * H_TAU / N;
        V3 A = v3(cosf(a0) * r, y0, sinf(a0) * r), B = v3(cosf(a1) * r, y0, sinf(a1) * r);
        V3 C = v3(cosf(a1) * r, y1, sinf(a1) * r), D = v3(cosf(a0) * r, y1, sinf(a0) * r);
        quadm(B, A, D, C, outer);
        V3 Ai = v3_scale(A, 0.8f), Bi = v3_scale(B, 0.8f), Ci = v3_scale(C, 0.8f), Di = v3_scale(D, 0.8f);
        Ai.y = A.y; Bi.y = B.y; Ci.y = C.y; Di.y = D.y;
        quadm(Ai, Bi, Ci, Di, inner);
        quadm(D, C, Ci, Di, outer);      /* top lip */
        quadm(A, Ai, Bi, B, inner);      /* bottom lip */
    }
    /* fan hub and blades */
    float hy = (y0 + y1) * 0.5f;
    for (int b = 0; b < 3; b++) {
        float a = (float)b * H_TAU / 3.0f;
        V3 c = v3(cosf(a) * r * 0.42f, hy, sinf(a) * r * 0.42f);
        box(c, v3(h_absf(cosf(a)) * r * 0.36f + 0.06f, 0.03f, h_absf(sinf(a)) * r * 0.36f + 0.06f), inner);
    }
    box(v3(0, hy, 0), v3(0.15f, 0.12f, 0.15f), outer);
}

static void build_scheme(int s) {
    uint32_t body = s ? RGBA(46, 64, 80, 255) : RGBA(96, 100, 106, 255);
    uint32_t dark = s ? RGBA(22, 30, 40, 255) : RGBA(40, 42, 46, 255);
    uint32_t accent = s ? RGBA(70, 190, 205, 255) : RGBA(210, 145, 40, 255);
    uint32_t glass = RGBA(26, 64, 74, 255);
    g_cur = g_body[s]; g_nbody = 0; g_curn = &g_nbody; g_curmax = BODY_MAX;
    /* fuselage: nose cone, cabin, rear body, tail boom */
    prism(1.9f, 3.0f, 0.82f, 0.32f, 0.72f, 0.0f, body, 0, 1);
    prism(-1.6f, 1.9f, 0.95f, 0.82f, 0.72f, 0.0f, body, 0, 0);
    prism(-2.7f, -1.6f, 0.55f, 0.95f, 0.72f, 0.0f, dark, 1, 0);
    box(v3(0, 0.15f, -3.4f), v3(0.18f, 0.18f, 0.8f), dark);
    box(v3(0, 0.75f, -3.8f), v3(0.06f, 0.6f, 0.35f), accent);                 /* fin */
    box(v3(0, 0.15f, -4.1f), v3(0.9f, 0.05f, 0.25f), dark);                    /* tail plane */
    /* canopy */
    V3 c0 = v3(-0.55f, 0.45f, 1.9f), c1 = v3(0.55f, 0.45f, 1.9f), c2 = v3(0.4f, 0.85f, 0.6f), c3 = v3(-0.4f, 0.85f, 0.6f);
    V3 c4 = v3(-0.5f, 0.52f, -0.4f), c5 = v3(0.5f, 0.52f, -0.4f), cn = v3(0, 0.3f, 2.6f);
    quadm(c0, c1, c2, c3, glass); tri(c0, cn, c1, glass);
    quadm(c3, c2, c5, c4, glass);
    tri(c1, c5, c2, glass); tri(c0, c3, c4, glass);
    /* arms to the nacelles */
    for (int sx = -1; sx <= 1; sx += 2)
        for (int sz = -1; sz <= 1; sz += 2) {
            V3 tip = v3(2.25f * (float)sx, 0.1f, 1.25f * (float)sz);
            V3 mid = v3_scale(tip, 0.5f);
            box(mid, v3(h_absf(tip.x) * 0.5f, 0.09f, 0.14f), dark);
            box(v3(mid.x, 0.2f, mid.z), v3(0.35f, 0.02f, 0.15f), accent);
        }
    /* sensor dome and scanner head under the nose */
    prism(1.0f, 2.0f, 0.3f, 0.22f, 1.0f, -0.75f, dark, 1, 1);
    box(v3(0, -1.0f, 1.5f), v3(0.12f, 0.12f, 0.2f), accent);
    /* skids */
    box(v3(-0.8f, -0.95f, 0.0f), v3(0.05f, 0.05f, 1.5f), dark);
    box(v3(0.8f, -0.95f, 0.0f), v3(0.05f, 0.05f, 1.5f), dark);
    box(v3(-0.8f, -0.7f, 0.9f), v3(0.04f, 0.25f, 0.04f), dark);
    box(v3(0.8f, -0.7f, 0.9f), v3(0.04f, 0.25f, 0.04f), dark);
    box(v3(-0.8f, -0.7f, -0.9f), v3(0.04f, 0.25f, 0.04f), dark);
    box(v3(0.8f, -0.7f, -0.9f), v3(0.04f, 0.25f, 0.04f), dark);
    /* antenna mast */
    box(v3(0.3f, 1.05f, -0.9f), v3(0.02f, 0.4f, 0.02f), dark);
    g_cur = g_nac[s]; g_nnac = 0; g_curn = &g_nnac; g_curmax = NAC_MAX;
    ring(0.78f, -0.45f, 0.45f, body, RGBA(20, 22, 26, 255));
    box(v3(0, 0.47f, 0), v3(0.82f, 0.02f, 0.06f), accent);
}

void moth_init(void) {
    build_scheme(0);
    int nb0 = g_nbody, nn0 = g_nnac;
    build_scheme(1);
    (void)nb0; (void)nn0;
    sceKernelDcacheWritebackAll();
}

static V3 xform(const Mat4 *m, V3 p) {
    return v3(m->m[0] * p.x + m->m[4] * p.y + m->m[8] * p.z + m->m[12],
              m->m[1] * p.x + m->m[5] * p.y + m->m[9] * p.z + m->m[13],
              m->m[2] * p.x + m->m[6] * p.y + m->m[10] * p.z + m->m[14]);
}

void moth_draw(const Craft *c, float time, int scheme, int draw_body, float fog_near, float fog_far, uint32_t fog_col) {
    scheme = scheme ? 1 : 0;
    Mat4 m = mat_trs(c->pos, c->yaw, c->pitch, c->bank, 1.0f);
    if (draw_body) {
        gfx_no_tex();
        sceGuEnable(GU_DEPTH_TEST);
        sceGuDepthMask(GU_FALSE);
        sceGuDisable(GU_BLEND);
        sceGuDisable(GU_CULL_FACE);
        gfx_fog(1, fog_near, fog_far, fog_col);
        gfx_model(&m);
        sceGuDrawArray(GU_TRIANGLES, VT_CP | GU_TRANSFORM_3D, g_nbody, 0, g_body[scheme]);
        for (int sx = -1; sx <= 1; sx += 2)
            for (int sz = -1; sz <= 1; sz += 2) {
                float tilt = c->nacelle_tilt * 0.6f + (sz > 0 ? 0.05f : -0.05f) * c->bank * (float)sx;
                Mat4 local = mat_trs(v3(2.25f * (float)sx, 0.1f, 1.25f * (float)sz), 0.0f, tilt, -c->bank * 0.3f * (float)sx, 1.0f);
                Mat4 nm = mat_mul(&m, &local);
                gfx_model(&nm);
                sceGuDrawArray(GU_TRIANGLES, VT_CP | GU_TRANSFORM_3D, g_nnac, 0, g_nac[scheme]);
            }
        gfx_model_identity();
        sceGuEnable(GU_BLEND);
    }
    /* thrust glow under each nacelle */
    uint32_t tg = scheme ? RGBA(110, 220, 255, 255) : RGBA(255, 165, 80, 255);
    int k = 0;
    for (int sx = -1; sx <= 1; sx += 2)
        for (int sz = -1; sz <= 1; sz += 2) {
            V3 p = xform(&m, v3(2.25f * (float)sx, -0.55f, 1.25f * (float)sz));
            float th = c->thrust[k++];
            float flick = 0.85f + 0.15f * sinf(time * 37.0f + (float)k * 1.7f);
            if (draw_body) rw_light(p, 0.9f + 1.4f * th, tg, (0.35f + 0.65f * th) * flick);
            else rw_light(p, 0.6f + 0.8f * th, tg, 0.2f * th);
        }
    if (!draw_body) return;
    /* running lights: port red, starboard green, white tail strobe, amber scanner */
    rw_light(xform(&m, v3(-3.05f, 0.1f, 1.25f)), 0.55f, RGBA(255, 40, 30, 255), 1.0f);
    rw_light(xform(&m, v3(3.05f, 0.1f, 1.25f)), 0.55f, RGBA(40, 255, 120, 255), 1.0f);
    float strobe = fmodf(time * 1.1f, 1.0f) < 0.06f ? 1.0f : 0.0f;
    rw_light(xform(&m, v3(0, 0.3f, -4.3f)), 1.2f, RGBA(255, 255, 255, 255), strobe);
    rw_light(xform(&m, v3(0, -1.05f, 1.75f)), 0.5f, RGBA(255, 190, 80, 255), 0.6f + 0.4f * sinf(time * 4.0f));
}

V3 moth_nose(const Craft *c) {
    Mat4 m = mat_trs(c->pos, c->yaw, c->pitch, c->bank, 1.0f);
    return xform(&m, v3(0, -1.0f, 1.7f));
}
