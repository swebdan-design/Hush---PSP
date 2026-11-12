/* HUSH — world renderer. See render_world.h.
 *
 * Static geometry is tessellated once per district into an 8x8 grid of
 * chunks, sorted by texture, with lighting baked into vertex colours (no
 * hardware lights). Facades are split into panels of at most PANEL metres so
 * the GE's lack of true near-plane clipping never drops a large wall when the
 * camera is close. Chunks are culled against the view frustum and the fog
 * distance; fog also hides the draw limit. */
#include "render_world.h"
#include "textures.h"
#include <pspkernel.h>
#include <psputils.h>
#include <string.h>

#define CH_N 8
#define CH_SIZE (2.0f * WORLD_HALF / CH_N)
#define POOL_VERTS 280000
#define PANEL 34.0f

static VtxTCP __attribute__((aligned(16))) g_pool[POOL_VERTS];
static int g_count[CH_N * CH_N][TEX_COUNT];
static int g_start[CH_N * CH_N][TEX_COUNT];
static int g_cursor[CH_N * CH_N][TEX_COUNT];
static float g_cmin[CH_N * CH_N][3], g_cmax[CH_N * CH_N][3];
static int g_writing, g_total;
static V3 g_grade;
static RenderStats g_stats;
int g_quality = 3;   /* adaptive detail level, see rw_set_quality */

const RenderStats *rw_stats(void) { return &g_stats; }

/* ---------------- tessellation ---------------- */

static int chunk_of(float x, float z) {
    int cx = h_clampi((int)((x + WORLD_HALF) / CH_SIZE), 0, CH_N - 1);
    int cz = h_clampi((int)((z + WORLD_HALF) / CH_SIZE), 0, CH_N - 1);
    return cz * CH_N + cx;
}

static uint32_t shade(V3 n, float y, float ground, float tint, int roof) {
    static const V3 L = { 0.42f, 0.62f, 0.66f };
    float d = h_maxf(0.0f, v3_dot(n, L));
    float k = roof ? 0.55f : 0.62f + 0.38f * d;
    float hf = 0.42f + 0.58f * h_smoothstep(0.0f, 140.0f, y - ground);
    k *= hf * (0.86f + 0.28f * tint);
    int r = (int)(255.0f * h_clampf(k * g_grade.x, 0, 1)), g = (int)(255.0f * h_clampf(k * g_grade.y, 0, 1)), b = (int)(255.0f * h_clampf(k * g_grade.z, 0, 1));
    return RGBA(r, g, b, 255);
}

static void emit(int ch, int tex, V3 p, float u, float v, uint32_t c) {
    if (!g_writing) { g_count[ch][tex]++; return; }
    int i = g_cursor[ch][tex]++;
    if (i >= POOL_VERTS) return;
    VtxTCP *o = &g_pool[i];
    o->u = u; o->v = v; o->color = c; o->x = p.x; o->y = p.y; o->z = p.z;
}

/* quad a,b,c,d counter-clockwise seen from the front */
static void quad(int ch, int tex, V3 a, V3 b, V3 c, V3 d, float ua, float va, float uc, float vc, uint32_t ca, uint32_t cb, uint32_t cc, uint32_t cd) {
    emit(ch, tex, a, ua, va, ca); emit(ch, tex, b, uc, va, cb); emit(ch, tex, c, uc, vc, cc);
    emit(ch, tex, a, ua, va, ca); emit(ch, tex, c, uc, vc, cc); emit(ch, tex, d, ua, vc, cd);
}

/* wall from o along h (unit) for length L, height y0..y1, normal n */
static void wall(int ch, int tex, V3 o, V3 h, float L, float y0, float y1, V3 n, float ground, float tint, float uoff) {
    int cols = (int)ceilf(L / PANEL), rows = (int)ceilf((y1 - y0) / PANEL);
    if (cols < 1) cols = 1;
    if (rows < 1) rows = 1;
    float dl = L / (float)cols, dy = (y1 - y0) / (float)rows;
    for (int r = 0; r < rows; r++) {
        float ya = y0 + dy * (float)r, yb = ya + dy;
        uint32_t c0 = shade(n, ya, ground, tint, 0), c1 = shade(n, yb, ground, tint, 0);
        for (int c = 0; c < cols; c++) {
            float s0 = dl * (float)c, s1 = s0 + dl;
            V3 a = v3_madd(o, h, s0); a.y = ya;
            V3 b = v3_madd(o, h, s1); b.y = ya;
            V3 cc = b; cc.y = yb;
            V3 d = a; d.y = yb;
            quad(ch, tex, a, b, cc, d, uoff + s0 / FACADE_TILE, -ya / FACADE_TILE, uoff + s1 / FACADE_TILE, -yb / FACADE_TILE, c0, c0, c1, c1);
        }
    }
}

/* shopfront band: the 512x64 strip holds four 8 m fronts; v spans the band height */
static void shop_band(int ch, V3 o, V3 h, float L, float y0, float H, float uoff) {
    int cols = (int)ceilf(L / 16.0f);
    if (cols < 1) cols = 1;
    float dl = L / (float)cols;
    uint32_t c = RGBA(235, 232, 240, 255);
    for (int k = 0; k < cols; k++) {
        float s0 = dl * (float)k, s1 = s0 + dl;
        V3 a = v3_madd(o, h, s0); a.y = y0;
        V3 b = v3_madd(o, h, s1); b.y = y0;
        V3 cc = b; cc.y = y0 + H;
        V3 d = a; d.y = y0 + H;
        quad(ch, TEX_SHOP, a, b, cc, d, uoff + s0 / 32.0f, 1.0f, uoff + s1 / 32.0f, 0.0f, c, c, c, c);
    }
}

/* horizontal surface split into <= 20 m cells; up = 1 faces +y */
static void hgrid(int ch, int tex, float x0, float z0, float x1, float z1, float y, int up, uint32_t c) {
    const float CELL = 20.0f, us = 1.0f / FACADE_TILE;
    int nx = (int)ceilf((x1 - x0) / CELL), nz = (int)ceilf((z1 - z0) / CELL);
    if (nx < 1) nx = 1;
    if (nz < 1) nz = 1;
    float dx = (x1 - x0) / (float)nx, dz = (z1 - z0) / (float)nz;
    for (int j = 0; j < nz; j++)
        for (int i = 0; i < nx; i++) {
            float a0 = x0 + dx * (float)i, a1 = a0 + dx, b0 = z0 + dz * (float)j, b1 = b0 + dz;
            if (up) quad(ch, tex, v3(a0, y, b1), v3(a1, y, b1), v3(a1, y, b0), v3(a0, y, b0), a0 * us, b1 * us, a1 * us, b0 * us, c, c, c, c);
            else quad(ch, tex, v3(a0, y, b0), v3(a1, y, b0), v3(a1, y, b1), v3(a0, y, b1), a0 * us, b0 * us, a1 * us, b1 * us, c, c, c, c);
        }
}

static void tess_box(const World *w, const Box *bx) {
    const Aabb *b = &bx->b;
    int ch = chunk_of((b->x0 + b->x1) * 0.5f, (b->z0 + b->z1) * 0.5f);
    if (g_writing) {
        if (b->x0 < g_cmin[ch][0]) g_cmin[ch][0] = b->x0;
        if (b->y0 < g_cmin[ch][1]) g_cmin[ch][1] = b->y0;
        if (b->z0 < g_cmin[ch][2]) g_cmin[ch][2] = b->z0;
        if (b->x1 > g_cmax[ch][0]) g_cmax[ch][0] = b->x1;
        if (b->y1 > g_cmax[ch][1]) g_cmax[ch][1] = b->y1;
        if (b->z1 > g_cmax[ch][2]) g_cmax[ch][2] = b->z1;
    }
    int tex = bx->kind == BOX_SIDEWALK ? TEX_PAVING : bx->tex;
    float tint = (float)bx->tint / 255.0f;
    float uoff = (float)(bx->tint % 8) * 0.125f;
    float g = w->ground_y;
    float y0 = b->y0, y1 = b->y1;
    if (w->water_y > y0 && w->water_y < y1) y0 = h_maxf(y0, w->water_y - 6.0f);
    /* street level: a band of lit shopfronts on the ground floor of city buildings */
    int shops = bx->kind == BOX_BUILDING && w->id != DISTRICT_UNDERTOW && b->y0 <= g + 0.5f && y1 - y0 > 12.0f &&
                (w->id != DISTRICT_WORKS || (bx->tint % 3) == 0) && tex != TEX_METAL;
    if (shops) {
        const float H = 7.0f;
        shop_band(ch, v3(b->x1, 0, b->z1), v3(0, 0, -1), b->z1 - b->z0, y0, H, tint);
        shop_band(ch, v3(b->x0, 0, b->z0), v3(0, 0, 1), b->z1 - b->z0, y0, H, tint + 0.3f);
        shop_band(ch, v3(b->x0, 0, b->z1), v3(1, 0, 0), b->x1 - b->x0, y0, H, tint + 0.55f);
        shop_band(ch, v3(b->x1, 0, b->z0), v3(-1, 0, 0), b->x1 - b->x0, y0, H, tint + 0.8f);
        y0 += H;
    }
    wall(ch, tex, v3(b->x1, 0, b->z1), v3(0, 0, -1), b->z1 - b->z0, y0, y1, v3(1, 0, 0), g, tint, uoff);
    wall(ch, tex, v3(b->x0, 0, b->z0), v3(0, 0, 1), b->z1 - b->z0, y0, y1, v3(-1, 0, 0), g, tint, uoff + 0.5f);
    wall(ch, tex, v3(b->x0, 0, b->z1), v3(1, 0, 0), b->x1 - b->x0, y0, y1, v3(0, 0, 1), g, tint, uoff + 0.25f);
    wall(ch, tex, v3(b->x1, 0, b->z0), v3(-1, 0, 0), b->x1 - b->x0, y0, y1, v3(0, 0, -1), g, tint, uoff + 0.75f);
    /* roof (and an underside for elevated structures), cut into <= 20 m cells: the GE rejects any
     * triangle with a vertex far outside the screen, so long decks must never be one giant quad */
    int rt = tex == TEX_GARDEN ? TEX_GARDEN : (bx->kind == BOX_SIDEWALK ? TEX_PAVING : TEX_ROOF);
    uint32_t rc = shade(v3(0, 1, 0), y1, g, tint, 1);
    if (bx->kind == BOX_SIDEWALK) rc = col_scale(RGBA(255, 255, 255, 255), 0.62f);
    hgrid(ch, rt, b->x0, b->z0, b->x1, b->z1, y1, 1, rc);
    if (b->y0 > g + 2.0f) {
        uint32_t uc = col_scale(shade(v3(0, -1, 0), b->y0, g, tint, 1), 0.6f);
        hgrid(ch, rt, b->x0, b->z0, b->x1, b->z1, b->y0, 0, uc);
    }
}

static void tess_cyl(const World *w, const Cyl *c) {
    int ch = chunk_of(c->x, c->z);
    if (g_writing) {
        float r = h_maxf(c->r0, c->r1);
        g_cmin[ch][0] = h_minf(g_cmin[ch][0], c->x - r); g_cmax[ch][0] = h_maxf(g_cmax[ch][0], c->x + r);
        g_cmin[ch][2] = h_minf(g_cmin[ch][2], c->z - r); g_cmax[ch][2] = h_maxf(g_cmax[ch][2], c->z + r);
        g_cmin[ch][1] = h_minf(g_cmin[ch][1], c->y0); g_cmax[ch][1] = h_maxf(g_cmax[ch][1], c->y1);
    }
    int tex = c->kind == CYL_COOLING ? TEX_CONCRETE : (c->kind == CYL_SPIRE ? TEX_GLASS : TEX_METAL);
    int n = c->sides;
    int rows = (int)ceilf((c->y1 - c->y0) / PANEL);
    if (rows < 1) rows = 1;
    float circ = H_TAU * h_maxf(c->r0, c->r1);
    for (int r = 0; r < rows; r++) {
        float t0 = (float)r / (float)rows, t1 = (float)(r + 1) / (float)rows;
        float ya = c->y0 + (c->y1 - c->y0) * t0, yb = c->y0 + (c->y1 - c->y0) * t1;
        float ra = h_lerpf(c->r0, c->r1, t0), rb = h_lerpf(c->r0, c->r1, t1);
        if (c->kind == CYL_COOLING) { ra -= c->r0 * 0.22f * sinf(H_PI * t0); rb -= c->r0 * 0.22f * sinf(H_PI * t1); }
        for (int i = 0; i < n; i++) {
            float a1 = (float)(i + 1) * H_TAU / (float)n, a0 = (float)i * H_TAU / (float)n;
            V3 nm = v3(cosf((a0 + a1) * 0.5f), 0, sinf((a0 + a1) * 0.5f));
            uint32_t ca = shade(nm, ya, w->ground_y, 0.5f, 0), cb = shade(nm, yb, w->ground_y, 0.5f, 0);
            V3 A = v3(c->x + cosf(a1) * ra, ya, c->z + sinf(a1) * ra);
            V3 B = v3(c->x + cosf(a0) * ra, ya, c->z + sinf(a0) * ra);
            V3 C = v3(c->x + cosf(a0) * rb, yb, c->z + sinf(a0) * rb);
            V3 D = v3(c->x + cosf(a1) * rb, yb, c->z + sinf(a1) * rb);
            float u0 = circ * (float)(i + 1) / (float)n / FACADE_TILE, u1 = circ * (float)i / (float)n / FACADE_TILE;
            quad(ch, tex, A, B, C, D, u0, -ya / FACADE_TILE, u1, -yb / FACADE_TILE, ca, ca, cb, cb);
            if (c->kind == CYL_COOLING) {
                uint32_t ci = col_scale(ca, 0.45f), cj = col_scale(cb, 0.45f);
                quad(ch, tex, B, A, D, C, u1, -ya / FACADE_TILE, u0, -yb / FACADE_TILE, ci, ci, cj, cj);
            }
        }
    }
}

/* Per-object vertex ranges, so geometry near the camera can be re-checked against the guard band. */
typedef struct { int start, count; uint8_t tex; float mn[3], mx[3]; } Range;
#define MAX_RANGES 9000
static Range g_ranges[MAX_RANGES];
static int g_nranges;

static void rec_begin(int ch, int *snap) { for (int t = 0; t < TEX_COUNT; t++) snap[t] = g_cursor[ch][t]; }
static void rec_end(int ch, const int *snap, float x0, float y0, float z0, float x1, float y1, float z1) {
    if (!g_writing) return;
    for (int t = 0; t < TEX_COUNT; t++) {
        int n = g_cursor[ch][t] - snap[t];
        if (n <= 0 || g_nranges >= MAX_RANGES || snap[t] + n > POOL_VERTS) continue;
        Range *r = &g_ranges[g_nranges++];
        r->start = snap[t]; r->count = n; r->tex = (uint8_t)t;
        r->mn[0] = x0; r->mn[1] = y0; r->mn[2] = z0; r->mx[0] = x1; r->mx[1] = y1; r->mx[2] = z1;
    }
}

static void tessellate(const World *w) {
    int snap[TEX_COUNT];
    g_nranges = 0;
    for (int i = 0; i < w->n_boxes; i++) {
        const Aabb *b = &w->boxes[i].b;
        int ch = chunk_of((b->x0 + b->x1) * 0.5f, (b->z0 + b->z1) * 0.5f);
        rec_begin(ch, snap);
        tess_box(w, &w->boxes[i]);
        rec_end(ch, snap, b->x0, b->y0, b->z0, b->x1, b->y1, b->z1);
    }
    for (int i = 0; i < w->n_cyls; i++) {
        const Cyl *c = &w->cyls[i];
        int ch = chunk_of(c->x, c->z);
        float r = h_maxf(c->r0, c->r1);
        rec_begin(ch, snap);
        tess_cyl(w, c);
        rec_end(ch, snap, c->x - r, c->y0, c->z - r, c->x + r, c->y1, c->z + r);
    }
}

void rw_build(const World *w) {
    uint64_t t0 = gfx_ticks_us();
    static const V3 grades[DISTRICT_COUNT] = { {0.80f, 0.86f, 1.0f}, {1.0f, 0.84f, 0.72f}, {0.84f, 0.96f, 0.95f}, {0.66f, 0.82f, 0.78f} };
    g_grade = grades[w->id];
    memset(g_count, 0, sizeof(g_count));
    g_writing = 0;
    tessellate(w);
    int at = 0;
    for (int t = 0; t < TEX_COUNT; t++)
        for (int c = 0; c < CH_N * CH_N; c++) { g_start[c][t] = at; g_cursor[c][t] = at; at += g_count[c][t]; }
    g_total = at;
    for (int c = 0; c < CH_N * CH_N; c++) {
        g_cmin[c][0] = g_cmin[c][1] = g_cmin[c][2] = 1e9f;
        g_cmax[c][0] = g_cmax[c][1] = g_cmax[c][2] = -1e9f;
    }
    g_writing = 1;
    tessellate(w);
    if (g_total > POOL_VERTS) {
        /* never draw past the pool: clamp the counts that overflowed */
        for (int t = 0; t < TEX_COUNT; t++)
            for (int c = 0; c < CH_N * CH_N; c++) {
                if (g_start[c][t] >= POOL_VERTS) g_count[c][t] = 0;
                else if (g_start[c][t] + g_count[c][t] > POOL_VERTS) g_count[c][t] = POOL_VERTS - g_start[c][t];
            }
    }
    sceKernelDcacheWritebackAll();
    g_stats.verts_total = g_total;
    g_stats.build_ms = (float)(gfx_ticks_us() - t0) / 1000.0f;
}

/* ---------------- atmosphere ---------------- */

void rw_atmos(const World *w, const Weather *wx, Atmos *a) {
    static const uint32_t zen[DISTRICT_COUNT] = { RGBA(6, 9, 18, 255), RGBA(12, 8, 8, 255), RGBA(6, 11, 15, 255), RGBA(4, 7, 7, 255) };
    /* neon light pollution: violet-blue over Civic, red smog over the Works, magenta haze in the Stacks */
    static const uint32_t hor[DISTRICT_COUNT] = { RGBA(38, 40, 80, 255), RGBA(72, 36, 38, 255), RGBA(56, 32, 66, 255), RGBA(18, 30, 30, 255) };
    uint32_t z = zen[w->id], h = hor[w->id];
    /* twilight: a bruised violet-amber horizon */
    z = col_lerp(z, RGBA(28, 22, 52, 255), wx->tod * 0.8f);
    h = col_lerp(h, RGBA(92, 62, 78, 255), wx->tod * 0.7f);
    /* fog thickens and lifts the horizon; rain greys it */
    h = col_lerp(h, RGBA(54, 60, 70, 255), wx->fog * 0.45f);
    h = col_lerp(h, RGBA(40, 46, 56, 255), wx->rain * 0.3f);
    a->sky_top = z;
    a->sky_horizon = h;
    a->fog_color = h;
    weather_fog(wx, w->id, &a->fog_near, &a->fog_far);
    static const float draw[4] = { 0.62f, 0.75f, 0.88f, 1.0f };   /* adaptive detail: shorter draw distance */
    a->fog_far *= draw[g_quality];
    a->fog_near *= draw[g_quality];
}

/* ---------------- sky & skyline ---------------- */

/* ---------------- guard-band clipping ----------------
 * The PSP GE discards a whole triangle if any vertex lands outside its 4096 x 4096 drawing area
 * (+-2048 px around our viewport centre), after near-plane clipping. Emulators often don't, so
 * this is invisible on PC but loses ground tiles and long decks on hardware. Risky triangles are
 * clipped here, on the CPU, against planes safely inside that area. */
/* GCC 15 reports a false maybe-uninitialized through the inlined clipper's buffer swaps */
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#include "guardclip.h"
#define CLIP_MAX 9000
static VtxTCP __attribute__((aligned(16))) g_clip[CLIP_MAX];
static int g_nclip;
static GuardClip g_gc;

static void clip_setup(float znear) {
    float tv = tanf(gfx_cam_fov() * 0.5f * H_PI / 180.0f);
    gc_setup(&g_gc, gfx_cam_pos(), gfx_cam_fwd(), gfx_cam_right(), gfx_cam_up(), tv, (float)SCR_W, (float)SCR_H, znear);
}

static int clip_tri(const VtxTCP *a, const VtxTCP *b, const VtxTCP *c) {
    return gc_clip_tri(&g_gc, (const GcVtx *)a, (const GcVtx *)b, (const GcVtx *)c, (GcVtx *)g_clip, &g_nclip, CLIP_MAX);
}

/* dynamic geometry: safe triangles go straight into the batch, risky ones are clipped */
static void dyn_tri(const VtxTCP *a, const VtxTCP *b, const VtxTCP *c) {
    if (clip_tri(a, b, c) && g_nclip + 3 <= CLIP_MAX) { g_clip[g_nclip++] = *a; g_clip[g_nclip++] = *b; g_clip[g_nclip++] = *c; }
}
static void dyn_quad(const VtxTCP *q) { dyn_tri(&q[0], &q[1], &q[2]); dyn_tri(&q[0], &q[2], &q[3]); }

static void clip_flush(void) {
    if (!g_nclip) return;
    VtxTCP *v = (VtxTCP *)sceGuGetMemory(g_nclip * (int)sizeof(VtxTCP));
    memcpy(v, g_clip, sizeof(VtxTCP) * (size_t)g_nclip);
    sceGuDrawArray(GU_TRIANGLES, VT_TCP | GU_TRANSFORM_3D, g_nclip, 0, v);
    g_stats.tris_drawn += g_nclip / 3;
    g_nclip = 0;
}

void rw_set_quality(int q) { g_quality = h_clampi(q, 0, 3); }
int rw_quality(void) { return g_quality; }

static void skyline_ring(V3 eye, float radius, float ybot, float ytop, float uoff, uint32_t col) {
    const int N = 48;
    for (int i = 0; i < N; i++) {
        float a0 = (float)i * H_TAU / (float)N, a1 = (float)(i + 1) * H_TAU / (float)N;
        float x0 = eye.x + cosf(a0) * radius, z0 = eye.z + sinf(a0) * radius, x1 = eye.x + cosf(a1) * radius, z1 = eye.z + sinf(a1) * radius;
        float u0 = uoff + (float)i * 4.0f / (float)N, u1 = uoff + (float)(i + 1) * 4.0f / (float)N;
        VtxTCP q[4] = { {u0, 1.0f, col, x0, ybot, z0}, {u1, 1.0f, col, x1, ybot, z1}, {u1, 0.0f, col, x1, ytop, z1}, {u0, 0.0f, col, x0, ytop, z0} };
        dyn_quad(q);
    }
    clip_flush();
}

void rw_draw_sky(const World *w, const Atmos *a, V3 eye, V3 target, V3 up, float fov) {
    gfx_mode_2d();
    gfx_rect_grad(0, 0, SCR_W, SCR_H * 0.62f, a->sky_top, a->sky_horizon);
    gfx_rect(0, SCR_H * 0.62f - 0.5f, SCR_W, SCR_H * 0.4f, a->sky_horizon);
    /* the distant city: two silhouette rings beyond the fog, never reachable */
    gfx_set_camera(eye, target, up, fov, 2.0f, 2600.0f);
    clip_setup(2.0f);
    sceGuDisable(GU_DEPTH_TEST);
    sceGuDisable(GU_CULL_FACE);
    gfx_blend_alpha();
    gfx_bind(&g_tex.skyline, 1, 1);
    float g = w->ground_y;
    skyline_ring(eye, 2100.0f, g - 60.0f, g + 520.0f, 0.37f, col_lerp(a->sky_horizon, RGBA(70, 76, 90, 160), 0.35f));
    skyline_ring(eye, 1500.0f, g - 60.0f, g + 330.0f, 0.0f, col_lerp(a->fog_color, RGBA(10, 12, 16, 230), 0.25f));
}

/* ---------------- world geometry ---------------- */

static float g_tanv, g_tanh;

static int sphere_visible(V3 c, float r, float far_d) {
    V3 eye = gfx_cam_pos(), f = gfx_cam_fwd(), rt = gfx_cam_right(), up = gfx_cam_up();
    V3 d = v3_sub(c, eye);
    float z = v3_dot(d, f);
    if (z < -r || z > far_d + r) return 0;
    float x = v3_dot(d, rt), y = v3_dot(d, up);
    if (h_absf(x) > z * g_tanh + r * 1.3f) return 0;
    if (h_absf(y) > z * g_tanv + r * 1.3f) return 0;
    return 1;
}

static void ground_plane(const World *w, const Atmos *a, float y, const Tex *t, uint32_t col, float uvscale, float uoff, float voff) {
    const int N = 24;   /* ~77 m tiles, each passed through the guard-band clipper */
    float s = 2.0f * (WORLD_HALF + 300.0f) / (float)N, o = -(WORLD_HALF + 300.0f);
    float far2 = (a->fog_far + 120.0f) * (a->fog_far + 120.0f);
    V3 eye = gfx_cam_pos();
    gfx_bind(t, 1, 1);
    for (int j = 0; j < N; j++)
        for (int i = 0; i < N; i++) {
            float x0 = o + s * (float)i, x1 = x0 + s, z0 = o + s * (float)j, z1 = z0 + s;
            float cx = h_clampf(eye.x, x0, x1) - eye.x, cz = h_clampf(eye.z, z0, z1) - eye.z;
            if (cx * cx + cz * cz > far2) continue;   /* fully fogged */
            VtxTCP q[4] = { {x0 * uvscale + uoff, z1 * uvscale + voff, col, x0, y, z1}, {x1 * uvscale + uoff, z1 * uvscale + voff, col, x1, y, z1},
                            {x1 * uvscale + uoff, z0 * uvscale + voff, col, x1, y, z0}, {x0 * uvscale + uoff, z0 * uvscale + voff, col, x0, y, z0} };
            dyn_quad(q);
            if (g_nclip > CLIP_MAX - 64) clip_flush();
        }
    clip_flush();
    (void)w;
}

static void draw_neon(const World *w, const Atmos *a, float time);
static void draw_lane_marks(const World *w, const Atmos *a);

void rw_draw_world(const World *w, const Atmos *a, float time) {
    float far_d = a->fog_far + 40.0f;
    clip_setup(gfx_cam_znear());
    g_tanv = tanf(gfx_cam_fov() * 0.5f * H_PI / 180.0f);
    g_tanh = g_tanv * (float)SCR_W / (float)SCR_H;
    sceGuEnable(GU_DEPTH_TEST);
    sceGuDepthMask(GU_FALSE);
    sceGuDisable(GU_BLEND);
    gfx_fog(1, a->fog_near, a->fog_far, a->fog_color);
    sceGuDisable(GU_CULL_FACE);
    /* streets */
    ground_plane(w, a, w->ground_y, &g_tex.ground, RGBA(150, 150, 160, 255), 1.0f / 32.0f, 0, 0);
    sceGuEnable(GU_CULL_FACE);
    sceGuFrontFace(GU_CCW);
    g_stats.chunks_drawn = 0;
    g_stats.tris_drawn = 0;
    int vis[CH_N * CH_N];
    for (int c = 0; c < CH_N * CH_N; c++) {
        if (g_cmax[c][0] < g_cmin[c][0]) { vis[c] = 0; continue; }
        V3 ctr = v3((g_cmin[c][0] + g_cmax[c][0]) * 0.5f, (g_cmin[c][1] + g_cmax[c][1]) * 0.5f, (g_cmin[c][2] + g_cmax[c][2]) * 0.5f);
        V3 ext = v3((g_cmax[c][0] - g_cmin[c][0]) * 0.5f, (g_cmax[c][1] - g_cmin[c][1]) * 0.5f, (g_cmax[c][2] - g_cmin[c][2]) * 0.5f);
        vis[c] = sphere_visible(ctr, v3_len(ext), far_d);
        if (vis[c]) g_stats.chunks_drawn++;
    }
    /* objects close to the camera: their risky triangles get clipped replacements */
    static short near_r[512];
    int n_near = 0;
    {
        V3 e = gfx_cam_pos();
        for (int i = 0; i < g_nranges && n_near < 512; i++) {
            const Range *r = &g_ranges[i];
            float dx = h_clampf(e.x, r->mn[0], r->mx[0]) - e.x, dy = h_clampf(e.y, r->mn[1], r->mx[1]) - e.y, dz = h_clampf(e.z, r->mn[2], r->mx[2]) - e.z;
            if (dx * dx + dy * dy + dz * dz < 55.0f * 55.0f) near_r[n_near++] = (short)i;
        }
    }
    g_stats.clipped = 0;
    for (int t = 0; t < TEX_COUNT; t++) {
        gfx_bind(&g_tex.facade[t], 1, 1);
        for (int c = 0; c < CH_N * CH_N; c++) {
            if (!vis[c] || !g_count[c][t]) continue;
            int start = g_start[c][t], n = g_count[c][t];
            while (n > 0) {
                int k = n > 60000 ? 60000 : n;
                sceGuDrawArray(GU_TRIANGLES, VT_TCP | GU_TRANSFORM_3D, k, 0, &g_pool[start]);
                start += k; n -= k;
                g_stats.tris_drawn += k / 3;
            }
        }
        for (int k = 0; k < n_near; k++) {
            const Range *r = &g_ranges[near_r[k]];
            if (r->tex != t) continue;
            for (int v = r->start; v + 2 < r->start + r->count; v += 3) clip_tri(&g_pool[v], &g_pool[v + 1], &g_pool[v + 2]);
            if (g_nclip > CLIP_MAX - 200) { g_stats.clipped += g_nclip / 3; clip_flush(); }
        }
        g_stats.clipped += g_nclip / 3;
        clip_flush();
    }
    sceGuDisable(GU_CULL_FACE);
    sceGuEnable(GU_BLEND);
    gfx_blend_alpha();
    /* water */
    if (w->water_y > w->ground_y) {
        uint32_t wc = col_lerp(RGBA(70, 110, 120, 210), a->fog_color, 0.35f);
        ground_plane(w, a, w->water_y, &g_tex.water, wc, 1.0f / 48.0f, time * 0.01f, time * 0.004f);
    }
    /* billboards and neon signs: emissive, fogged a little less than buildings, two texture passes */
    if (w->n_ads) {
        gfx_fog(1, a->fog_near * 1.6f, a->fog_far * 1.35f, a->fog_color);
        V3 eye = gfx_cam_pos();
        for (int pass = 0; pass < 2; pass++) {
            gfx_bind(pass == 0 ? &g_tex.ads : &g_tex.signs, 1, 0);
            static const float sign_dist[4] = { 140.0f, 190.0f, 250.0f, 300.0f };
            float sd = sign_dist[g_quality];
            for (int i = 0; i < w->n_ads; i++) {
                const Ad *ad = &w->ads[i];
                if ((ad->sheet != 0) != (pass == 1)) continue;
                if (ad->sheet && v3_len2(v3_sub(ad->p, eye)) > sd * sd) continue;
                if (!sphere_visible(ad->p, h_maxf(ad->w, ad->h), far_d)) continue;
                float fl = 1.0f;
                if (ad->flicker) { float ph = fmodf(time * 1.7f + (float)i * 0.37f, 7.0f); fl = (ph > 6.6f && ph < 6.75f) ? 0.15f : (ph > 6.8f && ph < 6.85f ? 0.3f : 1.0f); }
                uint32_t c = col_scale(RGBA(255, 255, 255, 255), fl * 0.97f);
                float u0, u1, v0, v1;
                if (ad->sheet == 0) { int id = ad->ad % 12; u0 = (float)(id % 2) * 0.5f; u1 = u0 + 0.5f; v0 = (float)(id / 2) * (64.0f / 512.0f); v1 = v0 + 64.0f / 512.0f; }
                else if (ad->sheet == 1) { int id = ad->ad % 8; u0 = (float)id * 0.125f; u1 = u0 + 0.125f; v0 = 0.0f; v1 = 0.5f; }
                else { int id = ad->ad % 16; u0 = (float)(id % 4) * 0.25f; u1 = u0 + 0.25f; v0 = 0.5f + (float)(id / 4) * 0.125f; v1 = v0 + 0.125f; }
                V3 hdir = ad->axis ? v3(0, 0, (float)-ad->facing) : v3((float)ad->facing, 0, 0);
                V3 A = v3_madd(ad->p, hdir, -ad->w * 0.5f), B = v3_madd(ad->p, hdir, ad->w * 0.5f);
                V3 Au = A, Bu = B; A.y -= ad->h * 0.5f; B.y -= ad->h * 0.5f; Au.y += ad->h * 0.5f; Bu.y += ad->h * 0.5f;
                VtxTCP q[4] = { {u0, v1, c, A.x, A.y, A.z}, {u1, v1, c, B.x, B.y, B.z}, {u1, v0, c, Bu.x, Bu.y, Bu.z}, {u0, v0, c, Au.x, Au.y, Au.z} };
                dyn_quad(q);
                if (g_nclip > CLIP_MAX - 64) clip_flush();
            }
            clip_flush();
        }
    }
    draw_neon(w, a, time);
    draw_lane_marks(w, a);
}

/* neon tubes: roof outlines and corner strips, additive 1-pixel lines */
static void draw_neon(const World *w, const Atmos *a, float time) {
    if (!w->n_neon) return;
    V3 eye = gfx_cam_pos();
    float far2 = a->fog_far * 1.3f;
    far2 *= far2;
    gfx_no_tex();
    sceGuDepthMask(GU_TRUE);
    gfx_fog(1, a->fog_near * 1.8f, a->fog_far * 1.4f, a->fog_color);
    gfx_blend_add();
    VtxCP *v = (VtxCP *)sceGuGetMemory(w->n_neon * 2 * (int)sizeof(VtxCP));
    int k = 0;
    for (int i = 0; i < w->n_neon; i++) {
        const Neon *n = &w->neon[i];
        V3 mid = v3_scale(v3_add(n->a, n->b), 0.5f);
        if (v3_len2(v3_sub(mid, eye)) > far2) continue;
        float al = 1.0f;
        if (n->flicker && fmodf(time * 2.3f + (float)i * 0.71f, 5.0f) > 4.6f) al = 0.2f;
        uint32_t c = col_alpha(n->color, al);
        v[k].color = c; v[k].x = n->a.x; v[k].y = n->a.y; v[k].z = n->a.z; k++;
        v[k].color = c; v[k].x = n->b.x; v[k].y = n->b.y; v[k].z = n->b.z; k++;
    }
    if (k) sceGuDrawArray(GU_LINES, VT_CP | GU_TRANSFORM_3D, k, 0, v);
    gfx_blend_alpha();
    sceGuDepthMask(GU_FALSE);
}

/* dashed centre lines on the street grid, generated around the camera only when it is low */
static void draw_lane_marks(const World *w, const Atmos *a) {
    if (w->pitch <= 0 || w->water_y > w->ground_y) return;
    V3 eye = gfx_cam_pos();
    if (eye.y - w->ground_y > 90.0f) return;
    const float R = 150.0f, y = w->ground_y + 0.06f;
    int kx0 = (int)floorf((eye.x - R) / w->pitch), kx1 = (int)ceilf((eye.x + R) / w->pitch);
    int kz0 = (int)floorf((eye.z - R) / w->pitch), kz1 = (int)ceilf((eye.z + R) / w->pitch);
    int lines = (kx1 - kx0 + 1) + (kz1 - kz0 + 1);
    int per = (int)(2.0f * R / 9.0f) + 1;
    VtxCP *v = (VtxCP *)sceGuGetMemory(lines * per * 6 * (int)sizeof(VtxCP));
    int k = 0;
    uint32_t c = col_lerp(RGBA(210, 190, 120, 255), a->fog_color, 0.15f);
    for (int axis = 0; axis < 2; axis++) {
        int k0 = axis ? kz0 : kx0, k1 = axis ? kz1 : kx1;
        for (int line = k0; line <= k1; line++) {
            float fixed = (float)line * w->pitch;
            if (h_absf(fixed) > WORLD_HALF - 10) continue;
            float start = floorf(((axis ? eye.x : eye.z) - R) / 9.0f) * 9.0f;
            for (int d = 0; d < per; d++) {
                float s0 = start + (float)d * 9.0f, s1 = s0 + 3.5f;
                V3 A, B, C, D;
                if (axis == 0) { A = v3(fixed - 0.12f, y, s0); B = v3(fixed + 0.12f, y, s0); C = v3(fixed + 0.12f, y, s1); D = v3(fixed - 0.12f, y, s1); }
                else { A = v3(s0, y, fixed - 0.12f); B = v3(s1, y, fixed - 0.12f); C = v3(s1, y, fixed + 0.12f); D = v3(s0, y, fixed + 0.12f); }
                VtxCP q[4] = { {c, A.x, A.y, A.z}, {c, B.x, B.y, B.z}, {c, C.x, C.y, C.z}, {c, D.x, D.y, D.z} };
                v[k++] = q[0]; v[k++] = q[1]; v[k++] = q[2]; v[k++] = q[0]; v[k++] = q[2]; v[k++] = q[3];
            }
        }
    }
    gfx_no_tex();
    sceGuDisable(GU_CULL_FACE);
    gfx_fog(1, a->fog_near, a->fog_far, a->fog_color);
    if (k) sceGuDrawArray(GU_TRIANGLES, VT_CP | GU_TRANSFORM_3D, k, 0, v);
}

/* ---------------- traffic ---------------- */

static VtxCP g_car[36];
static int g_car_built;

static void build_car(void) {
    /* unit box, -0.5..0.5, shaded by face */
    static const float P[8][3] = {{-.5f,-.5f,-.5f},{.5f,-.5f,-.5f},{.5f,.5f,-.5f},{-.5f,.5f,-.5f},{-.5f,-.5f,.5f},{.5f,-.5f,.5f},{.5f,.5f,.5f},{-.5f,.5f,.5f}};
    static const int F[6][4] = {{1,2,6,5},{4,7,3,0},{3,7,6,2},{4,0,1,5},{5,6,7,4},{0,3,2,1}};
    static const float S[6] = {0.8f, 0.7f, 1.0f, 0.35f, 0.9f, 0.6f};
    int n = 0;
    for (int f = 0; f < 6; f++) {
        int idx[6] = {0, 1, 2, 0, 2, 3};
        uint32_t c = col_scale(RGBA(255, 255, 255, 255), S[f]);
        for (int k = 0; k < 6; k++) { const float *p = P[F[f][idx[k]]]; g_car[n].color = c; g_car[n].x = p[0]; g_car[n].y = p[1]; g_car[n].z = p[2]; n++; }
    }
    sceKernelDcacheWritebackAll();
    g_car_built = 1;
}

void rw_draw_traffic(const World *w, const Traffic *t, const Atmos *a, float time) {
    if (!g_car_built) build_car();
    (void)w;
    float far_d = a->fog_far + 20.0f;
    gfx_no_tex();
    sceGuDisable(GU_BLEND);
    sceGuDisable(GU_CULL_FACE);
    gfx_fog(1, a->fog_near, a->fog_far, a->fog_color);
    g_stats.vehicles_drawn = 0;
    for (int i = 0; i < t->n; i++) {
        const Vehicle *v = &t->v[i];
        if (!v->active) continue;
        if (!sphere_visible(v->pos, 12.0f, far_d)) continue;
        V3 sc;
        switch (v->kind) {
        case VK_GROUND: sc = v3(2.2f, 1.4f, 4.6f); break;
        case VK_TRANSPORT: sc = v3(7.0f, 5.0f, 22.0f); break;
        case VK_DRONE: sc = v3(2.4f, 0.9f, 2.4f); break;
        case VK_COURIER: sc = v3(3.6f, 1.8f, 7.5f); break;
        default: sc = v3(3.2f, 1.6f, 6.4f); break;
        }
        float d = v3_dist(v->pos, gfx_cam_pos());
        V3 fwd = v3(sinf(v->yaw), 0, cosf(v->yaw));
        V3 rt = v3(cosf(v->yaw), 0, -sinf(v->yaw));
        if (v->kind != VK_GROUND || d < 160.0f) {
            Mat4 m = mat_trs(v->pos, v->yaw, v->disabled ? 0.6f : 0.0f, v->disabled ? 0.4f : 0.0f, 1.0f);
            m.m[0] *= sc.x; m.m[1] *= sc.x; m.m[2] *= sc.x;
            m.m[4] *= sc.y; m.m[5] *= sc.y; m.m[6] *= sc.y;
            m.m[8] *= sc.z; m.m[9] *= sc.z; m.m[10] *= sc.z;
            gfx_model(&m);
            sceGuColor(v->color);
            VtxCP *cv = (VtxCP *)sceGuGetMemory(36 * sizeof(VtxCP));
            for (int k = 0; k < 36; k++) { cv[k] = g_car[k]; cv[k].color = col_scale(v->color, (float)(g_car[k].color & 0xFF) / 255.0f); }
            sceGuDrawArray(GU_TRIANGLES, VT_CP | GU_TRANSFORM_3D, 36, 0, cv);
            g_stats.vehicles_drawn++;
        }
        /* lights: white/blue heads, red tails, patrol strobes, drone eye */
        float blink = 0.5f + 0.5f * sinf(time * 9.0f + (float)i);
        V3 front = v3_madd(v->pos, fwd, sc.z * 0.5f), back = v3_madd(v->pos, fwd, -sc.z * 0.5f);
        if (v->disabled) { rw_light(v->pos, 2.0f, RGBA(255, 60, 40, 255), 0.4f + 0.6f * blink); continue; }
        if (v->kind == VK_DRONE) { rw_light(v->pos, 2.2f, RGBA(255, 70, 60, 255), 0.7f + 0.3f * blink); continue; }
        rw_light(v3_madd(front, rt, sc.x * 0.35f), 1.6f, RGBA(220, 230, 255, 255), 0.9f);
        rw_light(v3_madd(front, rt, -sc.x * 0.35f), 1.6f, RGBA(220, 230, 255, 255), 0.9f);
        rw_light(v3_madd(back, rt, sc.x * 0.35f), 1.4f, RGBA(255, 40, 30, 255), 0.85f);
        rw_light(v3_madd(back, rt, -sc.x * 0.35f), 1.4f, RGBA(255, 40, 30, 255), 0.85f);
        if (v->kind == VK_PATROL) rw_light(v3_add(v->pos, v3(0, sc.y * 0.6f, 0)), 2.4f, (fmodf(time * 2.0f + (float)i, 1.0f) < 0.5f) ? RGBA(255, 40, 40, 255) : RGBA(40, 90, 255, 255), 1.0f);
        if (v->kind == VK_COURIER) rw_light(v3_add(v->pos, v3(0, sc.y * 0.6f, 0)), 2.0f, RGBA(255, 170, 60, 255), blink);
    }
    gfx_model_identity();
}

/* ---------------- emissive lights ---------------- */

#define MAX_FRAME_LIGHTS 1100
typedef struct { V3 p; float size; uint32_t color; float alpha; } FLight;
static FLight g_fl[MAX_FRAME_LIGHTS];
static int g_nfl;

void rw_light(V3 p, float size, uint32_t color, float alpha) {
    static const int cap[4] = { 380, 600, 850, MAX_FRAME_LIGHTS };
    if (g_nfl >= cap[g_quality] || alpha <= 0.01f) return;
    FLight *l = &g_fl[g_nfl++];
    l->p = p; l->size = size; l->color = color; l->alpha = alpha;
}

static float blink_fn(int kind, float phase, float t) {
    switch (kind) {
    case 0: return 1.0f;
    case 1: { float p = fmodf(t * 0.5f + phase, 1.0f); return p < 0.45f ? 1.0f : 0.08f; }
    case 2: return 0.55f + 0.45f * sinf(t * 2.2f + phase * H_TAU);
    case 3: return fmodf(t * 2.3f + phase, 1.0f) < 0.12f ? 1.0f : 0.1f;
    case 4: return 0.25f + 0.75f * h_maxf(0.0f, sinf(t * 1.3f - phase * 12.0f));
    case 5: return fmodf(t * 0.73f - phase, 1.0f) < 0.18f ? 1.0f : 0.15f;
    case 6: return 0.45f + 0.55f * sinf(t * 2.7f + phase * 20.0f);
    case 7: { float s = sinf(t * 0.35f + phase * 6.0f); return 0.3f + 0.7f * s * s; }
    case 8: return 0.15f + 0.85f * (sinf(t * 0.21f + phase * 3.0f) > 0.85f ? 1.0f : 0.0f);
    }
    return 1.0f;
}

/* A reflection is drawn where the eye-to-mirror-image ray meets the surface, so it sits
 * on the water/street (depth-tested like everything else) rather than below it. */
static void reflect_light(V3 eye, V3 p, float surf, float size, uint32_t c, float alpha) {
    if (eye.y <= surf + 0.2f) return;
    V3 m = p; m.y = 2.0f * surf - p.y;
    float t = (eye.y - surf) / (eye.y - m.y);
    V3 r = v3_lerp(eye, m, t);
    r.y = surf + 0.05f;
    rw_light(r, size * h_clampf(t, 0.15f, 1.0f), c, alpha);
}

void rw_draw_world_lights(const World *w, const Atmos *a, float time, float wet) {
    float far_d = a->fog_far * 1.45f;
    V3 eye = gfx_cam_pos();
    int water = w->water_y > w->ground_y;
    /* rain-slick streets mirror the neon: street-level lights reflect in the asphalt */
    int street_refl = !water && wet > 0.12f && eye.y - w->ground_y < 140.0f && g_quality >= 2;
    for (int i = 0; i < w->n_lights; i++) {
        const Light *l = &w->lights[i];
        float d2 = v3_len2(v3_sub(l->p, eye));
        if (d2 > far_d * far_d) continue;
        float b = blink_fn(l->blink, l->phase, time);
        rw_light(l->p, l->size, l->color, b);
        if (water && l->p.y > w->water_y && d2 < 380.0f * 380.0f && g_nfl < MAX_FRAME_LIGHTS)
            reflect_light(eye, l->p, w->water_y, l->size * 1.3f, l->color, b * 0.28f);
        else if (street_refl && l->p.y < w->ground_y + 40.0f && d2 < 220.0f * 220.0f && g_nfl < MAX_FRAME_LIGHTS)
            reflect_light(eye, l->p, w->ground_y, l->size * 1.5f, l->color, b * 0.3f * wet);
    }
}

void rw_flush_lights(const World *w, const Atmos *a) {
    (void)w;
    if (!g_nfl) return;
    float far_d = a->fog_far * 1.45f;
    V3 eye = gfx_cam_pos(), rt = gfx_cam_right(), up = gfx_cam_up(), fwd = gfx_cam_fwd();
    g_tanv = tanf(gfx_cam_fov() * 0.5f * H_PI / 180.0f);
    g_tanh = g_tanv * (float)SCR_W / (float)SCR_H;
    sceGuEnable(GU_DEPTH_TEST);
    sceGuDepthMask(GU_TRUE);
    sceGuDisable(GU_FOG);
    sceGuDisable(GU_CULL_FACE);
    gfx_blend_add();
    gfx_bind(&g_tex.glow, 1, 0);
    VtxTCP *v = (VtxTCP *)sceGuGetMemory(g_nfl * 6 * (int)sizeof(VtxTCP));
    int k = 0;
    for (int i = 0; i < g_nfl; i++) {
        const FLight *l = &g_fl[i];
        V3 d = v3_sub(l->p, eye);
        float z = v3_dot(d, fwd);
        if (z < 0.5f) continue;
        float dist = v3_len(d);
        if (dist > far_d) continue;
        /* frustum: skip sprites entirely off screen */
        float s = l->size * (1.0f + dist / 160.0f);
        if (h_absf(v3_dot(d, rt)) > z * g_tanh + s || h_absf(v3_dot(d, up)) > z * g_tanv + s) continue;
        /* fill-rate cap: a glow never covers more than ~1/6 of the screen height, however close */
        float smax = z * g_tanv * 0.16f;
        if (s > smax) s = smax;
        float fade = 1.0f - h_smoothstep(far_d * 0.55f, far_d, dist);
        uint32_t c = col_alpha(l->color, l->alpha * fade);
        V3 r = v3_scale(rt, s), u = v3_scale(up, s);
        V3 A = v3_sub(v3_sub(l->p, r), u), B = v3_sub(v3_add(l->p, r), u), C = v3_add(v3_add(l->p, r), u), D = v3_add(v3_sub(l->p, r), u);
        VtxTCP q0 = {0, 1, c, A.x, A.y, A.z}, q1 = {1, 1, c, B.x, B.y, B.z}, q2 = {1, 0, c, C.x, C.y, C.z}, q3 = {0, 0, c, D.x, D.y, D.z};
        v[k++] = q0; v[k++] = q1; v[k++] = q2; v[k++] = q0; v[k++] = q2; v[k++] = q3;
    }
    if (k) sceGuDrawArray(GU_TRIANGLES, VT_TCP | GU_TRANSFORM_3D, k, 0, v);
    g_stats.lights_drawn = k / 6;
    g_nfl = 0;
    sceGuDepthMask(GU_FALSE);
    gfx_blend_alpha();
}

/* ---------------- rain & steam ---------------- */

#define RAIN_MAX 360
static V3 g_rain[RAIN_MAX];
static int g_rain_init;
static uint32_t g_rr = 0x2545F491u;
static float rr01(void) { g_rr ^= g_rr << 13; g_rr ^= g_rr >> 17; g_rr ^= g_rr << 5; return (float)(g_rr >> 8) / 16777216.0f; }

#define STEAM_MAX 110
typedef struct { V3 p, v; float age, life, size; } Puff;
static Puff g_puff[STEAM_MAX];
static float g_steam_acc;

static void spawn_puff(V3 p, V3 v, float life, float size) {
    for (int i = 0; i < STEAM_MAX; i++) if (g_puff[i].life <= 0) { g_puff[i].p = p; g_puff[i].v = v; g_puff[i].age = 0; g_puff[i].life = life; g_puff[i].size = size; return; }
}

void rw_draw_effects(const World *w, const Weather *wx, const Atmos *a, float time, float dt, int rain_density) {
    V3 eye = gfx_cam_pos();
    /* steam: vents (cycled) and cooling towers */
    g_steam_acc += dt;
    while (g_steam_acc > 0.05f) {
        g_steam_acc -= 0.05f;
        for (int i = 0; i < w->n_vents; i++) {
            if (!vent_active(i, time)) continue;
            const Vent *vt = &w->vents[i];
            if (v3_dist(vt->p, eye) > a->fog_far) continue;
            if (rr01() < 0.55f) spawn_puff(v3(vt->p.x + (rr01() - 0.5f) * 4, vt->p.y + 1, vt->p.z + (rr01() - 0.5f) * 4), v3((rr01() - 0.5f) * 2, 18.0f + 8.0f * rr01(), (rr01() - 0.5f) * 2), 3.0f, 5.0f);
        }
        for (int i = 0; i < w->n_cyls; i++) {
            const Cyl *c = &w->cyls[i];
            if (c->kind != CYL_COOLING || rr01() > 0.25f) continue;
            if (v3_dist(v3(c->x, c->y1, c->z), eye) > a->fog_far * 1.2f) continue;
            float ang = rr01() * H_TAU, rad = rr01() * c->r1 * 0.7f;
            spawn_puff(v3(c->x + cosf(ang) * rad, c->y1, c->z + sinf(ang) * rad), v3(1.5f, 5.0f + 3.0f * rr01(), 0.8f), 8.0f, 14.0f);
        }
    }
    sceGuEnable(GU_DEPTH_TEST);
    sceGuDepthMask(GU_TRUE);
    sceGuDisable(GU_CULL_FACE);
    gfx_fog(1, a->fog_near, a->fog_far * 1.2f, a->fog_color);
    gfx_blend_alpha();
    gfx_bind(&g_tex.steam, 1, 0);
    V3 rt = gfx_cam_right(), up = gfx_cam_up();
    VtxTCP *v = (VtxTCP *)sceGuGetMemory(STEAM_MAX * 6 * (int)sizeof(VtxTCP));
    int k = 0;
    uint32_t sc = col_lerp(RGBA(170, 170, 175, 255), a->fog_color, 0.3f);
    for (int i = 0; i < STEAM_MAX; i++) {
        Puff *p = &g_puff[i];
        if (p->life <= 0) continue;
        p->age += dt;
        if (p->age >= p->life) { p->life = 0; continue; }
        p->p = v3_madd(p->p, p->v, dt);
        p->v = v3_scale(p->v, 1.0f - 0.4f * dt);
        float t = p->age / p->life;
        float s = p->size * (0.6f + 1.8f * t);
        uint32_t c = col_alpha(sc, (1.0f - t) * 0.45f * h_minf(t * 6.0f, 1.0f));
        V3 r = v3_scale(rt, s), u = v3_scale(up, s);
        V3 A = v3_sub(v3_sub(p->p, r), u), B = v3_sub(v3_add(p->p, r), u), C = v3_add(v3_add(p->p, r), u), D = v3_add(v3_sub(p->p, r), u);
        VtxTCP q0 = {0, 1, c, A.x, A.y, A.z}, q1 = {1, 1, c, B.x, B.y, B.z}, q2 = {1, 0, c, C.x, C.y, C.z}, q3 = {0, 0, c, D.x, D.y, D.z};
        v[k++] = q0; v[k++] = q1; v[k++] = q2; v[k++] = q0; v[k++] = q2; v[k++] = q3;
    }
    if (k) sceGuDrawArray(GU_TRIANGLES, VT_TCP | GU_TRANSFORM_3D, k, 0, v);

    /* rain: streaks in a box that travels with the camera */
    static const float rq[4] = { 0.4f, 0.6f, 0.8f, 1.0f };
    int n = (int)(wx->rain * (float)RAIN_MAX * (0.35f + 0.22f * (float)rain_density) * rq[g_quality]);
    if (n > RAIN_MAX) n = RAIN_MAX;
    if (!g_rain_init) { for (int i = 0; i < RAIN_MAX; i++) g_rain[i] = v3((rr01() - 0.5f) * 80, (rr01() - 0.5f) * 60, (rr01() - 0.5f) * 80); g_rain_init = 1; }
    if (n > 8) {
        gfx_no_tex();
        sceGuDisable(GU_FOG);
        VtxCP *lv = (VtxCP *)sceGuGetMemory(n * 2 * (int)sizeof(VtxCP));
        V3 fall = v3(3.0f, -34.0f, 1.5f);
        uint32_t c0 = col_alpha(col_lerp(RGBA(150, 170, 200, 255), a->fog_color, 0.2f), 0.0f);
        uint32_t c1 = col_alpha(col_lerp(RGBA(170, 190, 220, 255), a->fog_color, 0.2f), 0.55f);
        for (int i = 0; i < n; i++) {
            V3 *r = &g_rain[i];
            *r = v3_madd(*r, fall, dt);
            if (r->y < -30) { r->y += 60; r->x = (rr01() - 0.5f) * 80; r->z = (rr01() - 0.5f) * 80; }
            /* wrap horizontally around the moving camera */
            V3 p = v3(eye.x + fmodf(r->x - fmodf(eye.x, 80.0f) + 120.0f, 80.0f) - 40.0f, eye.y + r->y, eye.z + fmodf(r->z - fmodf(eye.z, 80.0f) + 120.0f, 80.0f) - 40.0f);
            V3 q = v3_madd(p, fall, 0.035f);
            lv[2 * i].color = c1; lv[2 * i].x = p.x; lv[2 * i].y = p.y; lv[2 * i].z = p.z;
            lv[2 * i + 1].color = c0; lv[2 * i + 1].x = q.x; lv[2 * i + 1].y = q.y; lv[2 * i + 1].z = q.z;
            /* splash light flicker on water */
        }
        sceGuDrawArray(GU_LINES, VT_CP | GU_TRANSFORM_3D, n * 2, 0, lv);
    }
    sceGuDepthMask(GU_FALSE);
}
