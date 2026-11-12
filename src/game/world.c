/* HUSH — Vesper district generation and collision.
 *
 * Every district is generated deterministically from a fixed seed, so the city
 * is identical on every run and on every platform. Mission-critical places
 * (landmarks, pads, transmitters, routes) are placed explicitly first and the
 * generic street grid is filled around reserved zones, so procedural variety
 * never moves or blocks a story location. */
#include "world.h"
#include "../core/rng.h"
#include <string.h>
#include <stdio.h>

#define RGBA(r, g, b, a) ((uint32_t)(a) << 24 | (uint32_t)(b) << 16 | (uint32_t)(g) << 8 | (uint32_t)(r))

static const char *const district_names[DISTRICT_COUNT] = { "CIVIC CENTRE", "THE WORKS", "THE STACKS", "THE UNDERTOW" };
const char *world_district_name(int d) { return (d >= 0 && d < DISTRICT_COUNT) ? district_names[d] : "?"; }

/* ---------------- builders ---------------- */

typedef struct { float x, z, r; } Reserve;
static Reserve g_res[64];
static int g_nres;

static void reserve(float x, float z, float r) { if (g_nres < 64) { g_res[g_nres].x = x; g_res[g_nres].z = z; g_res[g_nres].r = r; g_nres++; } }
static int reserved(float x0, float z0, float x1, float z1) {
    for (int i = 0; i < g_nres; i++) {
        float cx = h_clampf(g_res[i].x, x0, x1), cz = h_clampf(g_res[i].z, z0, z1);
        float dx = cx - g_res[i].x, dz = cz - g_res[i].z;
        if (dx * dx + dz * dz < g_res[i].r * g_res[i].r) return 1;
    }
    return 0;
}

static int add_box(World *w, float x0, float y0, float z0, float x1, float y1, float z1, int tex, int kind, int tint, int flags) {
    if (w->n_boxes >= MAX_BOXES) return -1;
    Box *b = &w->boxes[w->n_boxes];
    b->b.x0 = h_minf(x0, x1); b->b.x1 = h_maxf(x0, x1);
    b->b.y0 = h_minf(y0, y1); b->b.y1 = h_maxf(y0, y1);
    b->b.z0 = h_minf(z0, z1); b->b.z1 = h_maxf(z0, z1);
    b->tex = (uint8_t)tex; b->kind = (uint8_t)kind; b->tint = (uint8_t)tint; b->flags = (uint8_t)flags;
    return w->n_boxes++;
}

static void add_light(World *w, V3 p, float size, uint32_t color, int kind, int blink, float phase) {
    if (w->n_lights >= MAX_LIGHTS) return;
    Light *l = &w->lights[w->n_lights++];
    l->p = p; l->size = size; l->color = color; l->kind = (uint8_t)kind; l->blink = (uint8_t)blink; l->phase = phase;
}

static void add_cyl(World *w, float x, float z, float r0, float r1, float y0, float y1, int kind, int sides) {
    if (w->n_cyls >= MAX_CYLS) return;
    Cyl *c = &w->cyls[w->n_cyls++];
    c->x = x; c->z = z; c->r0 = r0; c->r1 = r1; c->y0 = y0; c->y1 = y1; c->kind = (uint8_t)kind; c->sides = (uint8_t)sides;
}

static Poi *add_poi(World *w, const char *name, V3 p, float r, int kind, int lm) {
    if (w->n_pois >= MAX_POIS) return NULL;
    Poi *q = &w->pois[w->n_pois++];
    memset(q, 0, sizeof(*q));
    snprintf(q->name, sizeof(q->name), "%s", name);
    q->p = p; q->r = r; q->kind = (uint8_t)kind; q->landmark = (int8_t)lm;
    return q;
}

static void add_ad(World *w, V3 p, float wd, float ht, int axis, int facing, int ad, int flicker) {
    if (w->n_ads >= MAX_ADS) return;
    Ad *a = &w->ads[w->n_ads++];
    a->p = p; a->w = wd; a->h = ht; a->axis = (uint8_t)axis; a->facing = (int8_t)facing; a->ad = (uint8_t)ad; a->flicker = (uint8_t)flicker;
}

static void add_sign(World *w, V3 p, float wd, float ht, int axis, int facing, int sheet, int id, int flicker) {
    if (w->n_ads >= MAX_ADS) return;
    Ad *a = &w->ads[w->n_ads++];
    a->p = p; a->w = wd; a->h = ht; a->axis = (uint8_t)axis; a->facing = (int8_t)facing; a->ad = (uint8_t)id;
    a->flicker = (uint8_t)flicker; a->sheet = (uint8_t)sheet;
}

static void add_neon(World *w, V3 a, V3 b, uint32_t c, int flicker) {
    if (w->n_neon >= MAX_NEON) return;
    Neon *n = &w->neon[w->n_neon++];
    n->a = a; n->b = b; n->color = c; n->flicker = (uint8_t)flicker;
}

static Lane *add_lane(World *w, const V3 *pts, int n, float speed, int kind, int loop, int count) {
    if (w->n_lanes >= MAX_LANES || n < 2) return NULL;
    Lane *l = &w->lanes[w->n_lanes++];
    memset(l, 0, sizeof(*l));
    l->n = h_mini(n, LANE_PTS);
    for (int i = 0; i < l->n; i++) l->pts[i] = pts[i];
    l->speed = speed; l->kind = (uint8_t)kind; l->loop = (uint8_t)loop; l->count = (uint8_t)count;
    float len = 0;
    int segs = l->loop ? l->n : l->n - 1;
    for (int i = 0; i < segs; i++) {
        l->seglen[i] = v3_dist(l->pts[i], l->pts[(i + 1) % l->n]);
        len += l->seglen[i];
    }
    l->len = len;
    return l;
}

static void rect_lane(World *w, float x0, float z0, float x1, float z1, float y, float speed, int kind, int count, int ccw) {
    V3 p[4] = { v3(x0, y, z0), v3(x1, y, z0), v3(x1, y, z1), v3(x0, y, z1) };
    if (ccw) { V3 t = p[1]; p[1] = p[3]; p[3] = t; }
    add_lane(w, p, 4, speed, kind, 1, count);
}

V3 lane_eval(const Lane *l, float s, V3 *tangent) {
    if (l->len <= 0) { if (tangent) *tangent = v3(0, 0, 1); return l->pts[0]; }
    if (l->loop) { s = fmodf(s, l->len); if (s < 0) s += l->len; }
    else s = h_clampf(s, 0.0f, l->len - 0.001f);
    int segs = l->loop ? l->n : l->n - 1;
    for (int i = 0; i < segs; i++) {
        if (s <= l->seglen[i] || i == segs - 1) {
            V3 a = l->pts[i], b = l->pts[(i + 1) % l->n];
            float t = l->seglen[i] > 0 ? h_clampf(s / l->seglen[i], 0.0f, 1.0f) : 0.0f;
            if (tangent) *tangent = v3_norm(v3_sub(b, a));
            return v3_lerp(a, b, t);
        }
        s -= l->seglen[i];
    }
    if (tangent) *tangent = v3(0, 0, 1);
    return l->pts[0];
}

static const uint32_t col_red = RGBA(255, 40, 30, 255), col_amber = RGBA(255, 170, 60, 255), col_white = RGBA(220, 230, 255, 255);
static const uint32_t col_cyan = RGBA(90, 210, 255, 255), col_violet = RGBA(170, 110, 255, 255), col_green = RGBA(120, 255, 170, 255);
static const uint32_t neon_cols[7] = { RGBA(255, 60, 190, 255), RGBA(60, 230, 255, 255), RGBA(160, 100, 255, 255), RGBA(255, 90, 140, 255),
                                       RGBA(150, 255, 100, 255), RGBA(255, 175, 60, 255), RGBA(255, 60, 60, 255) };

/* Street-level and rooftop dressing. Uses its own random stream seeded from the lot
 * position, so adding or changing decor never moves a single building. */
static void decorate(World *w, float x0, float z0, float x1, float z1, float base, float top, float rx0, float rz0, float rx1, float rz1, int district) {
    if (district == DISTRICT_UNDERTOW) return;
    Rng d;
    rng_seed(&d, w->gen_seed ^ ((uint32_t)(int)(x0 * 8.0f) * 73856093u) ^ ((uint32_t)(int)(z0 * 8.0f) * 19349663u), 0xDEC0u);
    float p_blade = district == DISTRICT_STACKS ? 0.55f : (district == DISTRICT_CIVIC ? 0.45f : 0.2f);
    float p_flat = district == DISTRICT_WORKS ? 0.25f : 0.6f;
    float p_roof = district == DISTRICT_STACKS ? 0.45f : (district == DISTRICT_CIVIC ? 0.55f : 0.3f);
    /* hanging blade signs near a corner, sticking out into the street */
    for (int k = 0; k < 2; k++) {
        if (!rng_chance(&d, k == 0 ? p_blade : p_blade * 0.5f)) continue;
        int face = rng_int(&d, 4);
        int id = rng_int(&d, 8);
        float y = base + 6.0f + rng_float(&d) * h_minf(26.0f, top - base - 10.0f);
        if (y > top - 4.0f) continue;
        uint32_t col = neon_cols[id % 7];
        int corner = rng_chance(&d, 0.5f);
        V3 p;
        if (face < 2) {   /* on an x face: blade lies in the XY plane */
            float fx = face == 0 ? x1 + 1.0f : x0 - 1.0f;
            p = v3(fx, y, corner ? z0 + 2.0f : z1 - 2.0f);
            add_sign(w, p, 1.5f, 6.0f, 0, face == 0 ? 1 : -1, 1, id, rng_chance(&d, 0.2f));
        } else {
            float fz = face == 2 ? z1 + 1.0f : z0 - 1.0f;
            p = v3(corner ? x0 + 2.0f : x1 - 2.0f, y, fz);
            add_sign(w, p, 1.5f, 6.0f, 1, face == 2 ? 1 : -1, 1, id, rng_chance(&d, 0.2f));
        }
        add_light(w, p, 4.0f, col, LT_SIGN, 0, 0);
    }
    /* flat shop signs above the storefront band */
    if (rng_chance(&d, p_flat) && top - base > 12.0f) {
        int face = rng_int(&d, 4), id = rng_int(&d, 16);
        float y = base + 8.6f;
        if (face < 2) add_sign(w, v3(face == 0 ? x1 + 0.3f : x0 - 0.3f, y, (z0 + z1) * 0.5f), h_minf(z1 - z0 - 2.0f, 9.0f), 2.6f, 1, face == 0 ? 1 : -1, 2, id, 0);
        else add_sign(w, v3((x0 + x1) * 0.5f, y, face == 2 ? z1 + 0.3f : z0 - 0.3f), h_minf(x1 - x0 - 2.0f, 9.0f), 2.6f, 0, face == 2 ? 1 : -1, 2, id, 0);
    }
    /* neon outline around the roof, sometimes a vertical strip down a corner */
    if (top - base > 40.0f && rng_chance(&d, p_roof)) {
        uint32_t c = neon_cols[rng_int(&d, 7)];
        float y = top + 0.25f;
        add_neon(w, v3(rx0, y, rz0), v3(rx1, y, rz0), c, 0);
        add_neon(w, v3(rx1, y, rz0), v3(rx1, y, rz1), c, 0);
        add_neon(w, v3(rx1, y, rz1), v3(rx0, y, rz1), c, 0);
        add_neon(w, v3(rx0, y, rz1), v3(rx0, y, rz0), c, 0);
        if (rng_chance(&d, 0.35f)) {
            float cx = rng_chance(&d, 0.5f) ? x0 - 0.15f : x1 + 0.15f, cz = rng_chance(&d, 0.5f) ? z0 - 0.15f : z1 + 0.15f;
            float yb = base + 10.0f + rng_float(&d) * 20.0f;
            add_neon(w, v3(cx, yb, cz), v3(cx, h_minf(top - 2.0f, yb + 40.0f + rng_float(&d) * 60.0f), cz), neon_cols[rng_int(&d, 7)], rng_chance(&d, 0.3f));
        }
    }
}

static int near_lane(const World *w, float x, float z, float r) {
    for (int i = 0; i < w->n_lanes; i++) {
        const Lane *l = &w->lanes[i];
        int segs = l->loop ? l->n : l->n - 1;
        for (int k = 0; k < segs; k++) {
            V3 a = l->pts[k], b = l->pts[(k + 1) % l->n];
            float dx = b.x - a.x, dz = b.z - a.z, L2 = dx * dx + dz * dz;
            float t = L2 > 0 ? h_clampf(((x - a.x) * dx + (z - a.z) * dz) / L2, 0.0f, 1.0f) : 0.0f;
            float px = a.x + dx * t - x, pz = a.z + dz * t - z;
            if (px * px + pz * pz < r * r) return 1;
        }
    }
    return 0;
}

/* A generic building with setback tiers and rooftop clutter. */
static void building(World *w, Rng *r, float x0, float z0, float x1, float z1, float base, float h, int tex, int tiers, int district) {
    int tint = rng_int(r, 256);
    float top = base + h;
    float cx0 = x0, cz0 = z0, cx1 = x1, cz1 = z1;
    float y = base;
    for (int t = 0; t < tiers; t++) {
        float frac = tiers == 1 ? 1.0f : (t == 0 ? 0.55f + 0.2f * rng_float(r) : (1.0f - (y - base) / h) * (0.5f + 0.5f * rng_float(r)) + 0.0f);
        float yt = t == tiers - 1 ? top : y + h * frac * (t == 0 ? 1.0f : 0.6f);
        if (yt > top) yt = top;
        add_box(w, cx0, y, cz0, cx1, yt, cz1, tex, t == 0 ? BOX_BUILDING : BOX_TIER, tint, rng_chance(r, 0.3f) ? 2 : 0);
        y = yt;
        float sx = (cx1 - cx0) * (0.1f + 0.12f * rng_float(r)), sz = (cz1 - cz0) * (0.1f + 0.12f * rng_float(r));
        cx0 += sx; cx1 -= sx; cz0 += sz; cz1 -= sz;
        if (cx1 - cx0 < 6 || cz1 - cz0 < 6 || y >= top - 2) break;
    }
    /* roof clutter */
    float rx = (cx0 + cx1) * 0.5f, rz = (cz0 + cz1) * 0.5f;
    if (district == DISTRICT_STACKS && rng_chance(r, 0.35f)) {
        add_box(w, cx0 + 1, y, cz0 + 1, cx1 - 1, y + 1.2f, cz1 - 1, TEX_GARDEN, BOX_ROOF_DETAIL, tint, 1);
        if (rng_chance(r, 0.3f)) add_light(w, v3(rx, y + 2.0f, rz), 1.5f, col_green, LT_WINDOW, 0, 0);
    } else if (rng_chance(r, 0.4f)) {
        float s = 2.0f + 3.0f * rng_float(r);
        add_box(w, rx - s, y, rz - s, rx + s, y + 2.5f + 2.0f * rng_float(r), rz + s, TEX_METAL, BOX_ROOF_DETAIL, tint, 0);
    }
    if (h > 90.0f || rng_chance(r, 0.15f)) {
        float ah = 6.0f + 18.0f * rng_float(r) + (h > 200 ? 20.0f : 0.0f);
        add_box(w, rx - 0.6f, y, rz - 0.6f, rx + 0.6f, y + ah, rz + 0.6f, TEX_METAL, BOX_ROOF_DETAIL, tint, 1);
        add_light(w, v3(rx, y + ah + 0.8f, rz), 3.0f, col_red, LT_AVIATION, 1, rng_float(r));
    }
    decorate(w, x0, z0, x1, z1, base, y, cx0, cz0, cx1, cz1, district);
    if (h > 140.0f) {
        /* corner aviation lights */
        add_light(w, v3(cx0, y + 0.5f, cz0), 2.2f, col_red, LT_AVIATION, 2, rng_float(r));
        add_light(w, v3(cx1, y + 0.5f, cz1), 2.2f, col_red, LT_AVIATION, 2, rng_float(r));
    }
}

/* Fill a street grid: street centre lines at k*pitch; blocks between them. */
static void fill_grid(World *w, Rng *r, int district, float pitch, float street, float base,
                      float (*height_fn)(Rng *, float, float), int (*tex_fn)(Rng *, float, float), int max_tiers, float lot_prob) {
    int kmax = (int)(WORLD_HALF / pitch);
    w->pitch = pitch;
    for (int i = -kmax; i < kmax; i++) {
        for (int j = -kmax; j < kmax; j++) {
            float bx0 = (float)i * pitch + street * 0.5f, bx1 = (float)(i + 1) * pitch - street * 0.5f;
            float bz0 = (float)j * pitch + street * 0.5f, bz1 = (float)(j + 1) * pitch - street * 0.5f;
            /* avenues every third street are wider */
            if (i % 3 == 0) bx0 += street * 0.4f;
            if ((i + 1) % 3 == 0) bx1 -= street * 0.4f;
            if (j % 3 == 0) bz0 += street * 0.4f;
            if ((j + 1) % 3 == 0) bz1 -= street * 0.4f;
            if (bx1 > WORLD_HALF - 10 || bz1 > WORLD_HALF - 10 || bx0 < -WORLD_HALF + 10 || bz0 < -WORLD_HALF + 10) continue;
            if (reserved(bx0, bz0, bx1, bz1)) continue;
            if (district != DISTRICT_UNDERTOW && w->n_blocks < MAX_BLOCKS) {
                Block *bl = &w->blocks[w->n_blocks++];
                bl->x0 = bx0; bl->z0 = bz0; bl->x1 = bx1; bl->z1 = bz1;
                add_box(w, bx0, base, bz0, bx1, base + 0.35f, bz1, TEX_CONCRETE, BOX_SIDEWALK, 60, 0);   /* raised sidewalk */
            }
            int split = rng_int(r, 3); /* 0: whole, 1: split x, 2: quarters */
            float lots[4][4];
            int nl = 0;
            float mx = (bx0 + bx1) * 0.5f + (rng_float(r) - 0.5f) * (bx1 - bx0) * 0.2f;
            float mz = (bz0 + bz1) * 0.5f + (rng_float(r) - 0.5f) * (bz1 - bz0) * 0.2f;
            if (district == DISTRICT_STACKS && split == 2) split = 1;
            if (split == 0) { lots[nl][0] = bx0; lots[nl][1] = bz0; lots[nl][2] = bx1; lots[nl][3] = bz1; nl++; }
            else if (split == 1) {
                lots[nl][0] = bx0; lots[nl][1] = bz0; lots[nl][2] = mx; lots[nl][3] = bz1; nl++;
                lots[nl][0] = mx; lots[nl][1] = bz0; lots[nl][2] = bx1; lots[nl][3] = bz1; nl++;
            } else {
                lots[nl][0] = bx0; lots[nl][1] = bz0; lots[nl][2] = mx; lots[nl][3] = mz; nl++;
                lots[nl][0] = mx; lots[nl][1] = bz0; lots[nl][2] = bx1; lots[nl][3] = mz; nl++;
                lots[nl][0] = bx0; lots[nl][1] = mz; lots[nl][2] = mx; lots[nl][3] = bz1; nl++;
                lots[nl][0] = mx; lots[nl][1] = mz; lots[nl][2] = bx1; lots[nl][3] = bz1; nl++;
            }
            for (int k = 0; k < nl; k++) {
                if (!rng_chance(r, lot_prob)) continue;
                float m = 1.5f + 3.0f * rng_float(r);
                float x0 = lots[k][0] + m, z0 = lots[k][1] + m, x1 = lots[k][2] - m, z1 = lots[k][3] - m;
                if (x1 - x0 < 10 || z1 - z0 < 10) continue;
                float cx = (x0 + x1) * 0.5f, cz = (z0 + z1) * 0.5f;
                float h = height_fn(r, cx, cz);
                int tex = tex_fn(r, cx, cz);
                int tiers = 1 + rng_int(r, max_tiers);
                building(w, r, x0, z0, x1, z1, base, h, tex, tiers, district);
            }
        }
    }
}

/* ---------------- district height/texture functions ---------------- */

static float civic_h(Rng *r, float x, float z) {
    float d = sqrtf(x * x + z * z);
    float core = expf(-(d / 360.0f) * (d / 360.0f));
    return 45.0f + (60.0f + 230.0f * core) * (0.55f + 0.6f * rng_float(r));
}
static int civic_tex(Rng *r, float x, float z) { (void)x; (void)z; float w[3] = {6, 2.5f, 1.5f}; int k = rng_weighted(r, w, 3); return k == 0 ? TEX_GLASS : (k == 1 ? TEX_CONCRETE : TEX_METAL); }
static float stacks_h(Rng *r, float x, float z) {
    (void)x; (void)z;
    float a = rng_float(r), b = rng_float(r), c = rng_float(r);   /* sequenced draws */
    return 70.0f + 140.0f * a * b + 30.0f * c;
}
static int stacks_tex(Rng *r, float x, float z) { (void)x; (void)z; return rng_chance(r, 0.8f) ? TEX_RESIDENTIAL : TEX_CONCRETE; }
static float works_h(Rng *r, float x, float z) { (void)x; (void)z; return 16.0f + 46.0f * rng_float(r); }
static int works_tex(Rng *r, float x, float z) { (void)x; (void)z; return rng_chance(r, 0.6f) ? TEX_INDUSTRIAL : TEX_METAL; }
static float under_h(Rng *r, float x, float z) { (void)x; (void)z; return 30.0f + 90.0f * rng_float(r); }
static int under_tex(Rng *r, float x, float z) { (void)x; (void)z; return rng_chance(r, 0.7f) ? TEX_DERELICT : TEX_CONCRETE; }

/* A landing pad on top of a small MSA tower. Returns roof y. */
static float msa_pad(World *w, float x, float z, float base, float h) {
    add_box(w, x - 14, base, z - 14, x + 14, base + h, z + 14, TEX_CONCRETE, BOX_BUILDING, 40, 0);
    add_box(w, x - 18, base + h, z - 18, x + 18, base + h + 1.5f, z + 18, TEX_METAL, BOX_PAD, 0, 0);
    float y = base + h + 1.5f;
    add_light(w, v3(x - 17, y + 0.5f, z - 17), 2.5f, col_cyan, LT_BEACON, 1, 0.0f);
    add_light(w, v3(x + 17, y + 0.5f, z - 17), 2.5f, col_cyan, LT_BEACON, 1, 0.25f);
    add_light(w, v3(x + 17, y + 0.5f, z + 17), 2.5f, col_cyan, LT_BEACON, 1, 0.5f);
    add_light(w, v3(x - 17, y + 0.5f, z + 17), 2.5f, col_cyan, LT_BEACON, 1, 0.75f);
    add_poi(w, "pad", v3(x, y + 1.0f, z), 12.0f, POI_PAD, -1);
    reserve(x, z, 30);
    return y;
}

/* Explicit building whose roof hosts a mission point. */
static float poi_building(World *w, const char *name, float x, float z, float half, float base, float h, int tex, int kind, uint32_t lc) {
    add_box(w, x - half, base, z - half, x + half, base + h, z + half, tex, BOX_BUILDING, 90, 2);
    float y = base + h;
    if (kind == POI_TRANSMITTER) {
        add_box(w, x - 0.5f, y, z - 0.5f, x + 0.5f, y + 14, z + 0.5f, TEX_METAL, BOX_ROOF_DETAIL, 0, 1);
        add_box(w, x - 3, y + 9, z - 0.3f, x + 3, y + 9.6f, z + 0.3f, TEX_METAL, BOX_ROOF_DETAIL, 0, 1);
        add_light(w, v3(x, y + 15, z), 3.5f, lc, LT_BEACON, 3, 0.3f);
        y += 8;
    } else {
        add_light(w, v3(x, y + 2, z), 3.0f, lc, LT_BEACON, 1, 0.0f);
        y += 2;
    }
    add_poi(w, name, v3(x, y, z), 10.0f, kind, -1);
    reserve(x, z, half + 14);
    return y;
}

/* ---------------- CIVIC CENTRE ---------------- */
static void gen_civic(World *w, Rng *r) {
    w->ground_y = 0; w->water_y = -100; w->skyline = 230;
    /* MSA patrol platform */
    float py = msa_pad(w, -144, 144, 0, 88);
    w->spawn = v3(-144, py + 14, 120); w->spawn_yaw = 0.0f;
    /* THE MEMORY ARCHIVE: windowless monolith */
    float ax = 336, az = -336;
    reserve(ax, az, 75);
    add_box(w, ax - 34, 0, az - 34, ax + 34, 210, az + 34, TEX_METAL, BOX_LANDMARK, 10, 4);
    add_box(w, ax - 38, 0, az - 38, ax + 38, 18, az + 38, TEX_CONCRETE, BOX_LANDMARK, 10, 0);
    for (int i = 0; i < 12; i++) {
        float y = 30.0f + (float)i * 15.0f;
        add_light(w, v3(ax - 34.5f, y, az), 2.0f, col_amber, LT_LANDMARK, 4, (float)i * 0.08f);
        add_light(w, v3(ax + 34.5f, y, az), 2.0f, col_amber, LT_LANDMARK, 4, (float)i * 0.08f + 0.5f);
    }
    add_light(w, v3(ax, 214, az), 8.0f, col_amber, LT_BEACON, 1, 0.1f);
    add_poi(w, "archive", v3(ax, 120, az + 34), 70.0f, POI_LANDMARK, LM_ARCHIVE);
    add_poi(w, "archive_door", v3(ax, 24, az + 46), 14.0f, POI_BUILDING, -1);
    /* rooftop relay mast and the unlicensed rain recorder */
    poi_building(w, "relay", 0 + 48, -96 - 48, 16, 0, 150, TEX_GLASS, POI_TRANSMITTER, col_red);
    poi_building(w, "rain_rec", -288 - 48, -192 + 48, 14, 0, 64, TEX_CONCRETE, POI_SIGNAL, col_violet);
    /* surveillance spires */
    static const float sp[6][2] = {{144, 144}, {-48, -240}, {240, 144}, {-432, 48}, {144, -48}, {48, 432}};
    for (int i = 0; i < 6; i++) {
        float h = 190.0f + 60.0f * rng_float(r);
        add_cyl(w, sp[i][0], sp[i][1], 5.0f, 1.2f, 0, h, CYL_SPIRE, 6);
        add_light(w, v3(sp[i][0], h + 1, sp[i][1]), 4.0f, col_red, LT_AVIATION, 1, rng_float(r));
        add_light(w, v3(sp[i][0], h * 0.6f, sp[i][1]), 2.5f, col_white, LT_BEACON, 2, rng_float(r));
        reserve(sp[i][0], sp[i][1], 8);
    }
    /* elevated ring highway along the third avenues */
    float R = 288, hy = 42;
    add_box(w, -R - 9, hy - 3, -R - 9, R + 9, hy, -R + 9, TEX_CONCRETE, BOX_HIGHWAY, 0, 4);
    add_box(w, -R - 9, hy - 3, R - 9, R + 9, hy, R + 9, TEX_CONCRETE, BOX_HIGHWAY, 0, 4);
    add_box(w, -R - 9, hy - 3, -R + 9, -R + 9, hy, R - 9, TEX_CONCRETE, BOX_HIGHWAY, 0, 4);
    add_box(w, R - 9, hy - 3, -R + 9, R + 9, hy, R - 9, TEX_CONCRETE, BOX_HIGHWAY, 0, 4);
    for (int i = -3; i <= 3; i++) {
        float t = (float)i * 96.0f;
        if (t == -R || t == R) continue;
        add_box(w, t - 2, 0, -R - 2, t + 2, hy - 3, -R + 2, TEX_CONCRETE, BOX_PIPE, 0, 0);
        add_box(w, t - 2, 0, R - 2, t + 2, hy - 3, R + 2, TEX_CONCRETE, BOX_PIPE, 0, 0);
        add_box(w, -R - 2, 0, t - 2, -R + 2, hy - 3, t + 2, TEX_CONCRETE, BOX_PIPE, 0, 0);
        add_box(w, R - 2, 0, t - 2, R + 2, hy - 3, t + 2, TEX_CONCRETE, BOX_PIPE, 0, 0);
    }
    rect_lane(w, -R + 4, -R + 4, R - 4, R - 4, hy + 2.2f, 26.0f, 0, 14, 0);
    rect_lane(w, -R - 4, -R - 4, R + 4, R + 4, hy + 2.2f, 24.0f, 0, 14, 1);
    /* aerial traffic corridors (along streets, above the highway) */
    rect_lane(w, -192, -192, 192, 192, 95, 30, 1, 5, 0);
    rect_lane(w, -384, -96, 96, 384, 135, 34, 1, 5, 1);
    rect_lane(w, -96, -384, 384, 96, 175, 36, 1, 4, 0);
    rect_lane(w, -480, -480, 480, 480, 215, 40, 2, 4, 1);
    rect_lane(w, -288, -96, 0, 192, 70, 22, 3, 2, 0);     /* municipal patrol craft */
    reserve(250, -235, 48);   /* keep the courier approach to the Archive clear */
    /* corporate advertising on the inner avenue */
    for (int i = 0; i < 4; i++) reserve(-96 + (float)i * 192 - 48, -48, 4);
    /* waypoints for the first patrol */
    add_poi(w, "wp_a", v3(-96, 70, 96), 14.0f, POI_GENERIC, -1);
    add_poi(w, "wp_b", v3(96, 60, 0), 14.0f, POI_GENERIC, -1);
    add_poi(w, "wp_c", v3(192, 110, -192), 14.0f, POI_GENERIC, -1);
    add_poi(w, "wp_d", v3(0, 160, -96), 14.0f, POI_GENERIC, -1);
    /* courier route: from the plaza down the avenues to the Archive door */
    V3 cr[7] = { v3(-96, 60, 192), v3(-96, 60, 0), v3(96, 75, 0), v3(96, 75, -192), v3(192, 55, -192), v3(300, 55, -260), v3(ax, 26, az + 46) };
    Lane *l = add_lane(w, cr, 7, 16, 4, 0, 0);
    (void)l;
    add_poi(w, "courier_start", v3(-96, 95, 270), 20.0f, POI_GENERIC, -1);
    fill_grid(w, r, DISTRICT_CIVIC, 96, 26, 0, civic_h, civic_tex, 3, 0.95f);
    /* adverts on tall towers facing the central avenue (x = 0) */
    int ad_id = 0;
    for (int i = 0; i < w->n_boxes && w->n_ads < 14; i++) {
        Box *b = &w->boxes[i];
        if (b->kind != BOX_BUILDING || b->b.y1 < 140) continue;
        float cx = (b->b.x0 + b->b.x1) * 0.5f;
        if (h_absf(b->b.x1 - 0) < 40 || h_absf(b->b.x0 - 0) < 40 || (rng_chance(r, 0.12f))) {
            int face = cx < 0 ? 1 : -1;
            float fx = face > 0 ? b->b.x1 + 0.6f : b->b.x0 - 0.6f;
            add_ad(w, v3(fx, b->b.y1 * 0.62f, (b->b.z0 + b->b.z1) * 0.5f), h_minf(b->b.z1 - b->b.z0 - 6, 34), 26, 1, face, ad_id++ % 8, rng_chance(r, 0.3f));
        }
    }
}

/* ---------------- THE WORKS ---------------- */
static void gen_works(World *w, Rng *r) {
    w->ground_y = 0; w->water_y = -100; w->skyline = 90;
    float py = msa_pad(w, -330, 330, 0, 46);
    w->spawn = v3(-330, py + 14, 306); w->spawn_yaw = 0.0f;
    /* STATION NINE: an abandoned elevated transport station */
    float sx = 220, sz = 220;
    reserve(sx, sz, 80);
    add_box(w, sx - 60, 48, sz - 14, sx + 60, 52, sz + 14, TEX_CONCRETE, BOX_LANDMARK, 0, 0);      /* platform */
    add_box(w, sx - 64, 66, sz - 18, sx + 64, 69, sz + 18, TEX_METAL, BOX_LANDMARK, 0, 0);         /* canopy */
    for (int i = -2; i <= 2; i++) {
        add_box(w, sx + (float)i * 28 - 2, 0, sz - 2, sx + (float)i * 28 + 2, 48, sz + 2, TEX_CONCRETE, BOX_PIPE, 0, 0);
        add_box(w, sx + (float)i * 28 - 0.8f, 52, sz - 15, sx + (float)i * 28 + 0.8f, 66, sz - 13, TEX_METAL, BOX_GANTRY, 0, 0);
        add_box(w, sx + (float)i * 28 - 0.8f, 52, sz + 13, sx + (float)i * 28 + 0.8f, 66, sz + 15, TEX_METAL, BOX_GANTRY, 0, 0);
        add_light(w, v3(sx + (float)i * 28, 64, sz), 2.2f, col_amber, LT_LANDMARK, 5, (float)(i + 2) * 0.2f);
    }
    /* rails leaving the station in both directions */
    add_box(w, sx - 380, 46, sz - 4, sx - 60, 48, sz + 4, TEX_METAL, BOX_HIGHWAY, 0, 4);
    add_box(w, sx + 60, 46, sz - 4, WORLD_HALF, 48, sz + 4, TEX_METAL, BOX_HIGHWAY, 0, 4);
    for (int i = 1; i <= 6; i++) add_box(w, sx - 60 - (float)i * 54 - 2, 0, sz - 2, sx - 60 - (float)i * 54 + 2, 46, sz + 2, TEX_CONCRETE, BOX_PIPE, 0, 0);
    add_poi(w, "station9", v3(sx, 58, sz), 70.0f, POI_LANDMARK, LM_STATION9);
    poi_building(w, "s9_tx1", sx - 150, sz - 90, 10, 0, 28, TEX_INDUSTRIAL, POI_TRANSMITTER, col_amber);
    poi_building(w, "s9_tx2", sx + 110, sz - 120, 10, 0, 36, TEX_INDUSTRIAL, POI_TRANSMITTER, col_amber);
    /* rogue drone route and drop point */
    V3 rr[10] = { v3(-250, 72, 250), v3(-100, 70, 250), v3(-50, 68, 100), v3(100, 70, 120), v3(150, 82, -110), v3(0, 75, -180),
                  v3(-150, 70, -150), v3(-260, 74, 30), v3(-200, 90, 200), v3(-250, 72, 250) };
    add_lane(w, rr, 10, 26, 5, 1, 0);
    add_poi(w, "rogue_start", rr[0], 20.0f, POI_GENERIC, -1);
    /* transports */
    rect_lane(w, -500, -500, 500, 500, 110, 32, 2, 3, 0);
    rect_lane(w, -300, -300, 300, 300, 80, 24, 3, 2, 1);
    /* cooling towers and chimneys */
    static const float ct[6][2] = {{-120, -260}, {-40, -340}, {60, -250}, {-380, -170}, {380, -200}, {-120, 160}};
    for (int i = 0; i < 6; i++) {
        float h = 95.0f + 45.0f * rng_float(r);
        add_cyl(w, ct[i][0], ct[i][1], 34.0f, 22.0f, 0, h, CYL_COOLING, 12);
        add_light(w, v3(ct[i][0] + 22, h, ct[i][1]), 3.0f, col_red, LT_AVIATION, 1, rng_float(r));
        add_poi(w, "cooling", v3(ct[i][0], h + 6, ct[i][1]), 30.0f, POI_GENERIC, -1);
        reserve(ct[i][0], ct[i][1], 40);
    }
    for (int i = 0; i < 10; i++) {
        float x = -400.0f + 85.0f * (float)i + 20.0f * rng_float(r), z = -420.0f + 120.0f * rng_float(r) + (i & 1 ? 500.0f : 0.0f);
        if (reserved(x - 6, z - 6, x + 6, z + 6) || near_lane(w, x, z, 16.0f)) continue;
        float h = 140.0f + 50.0f * rng_float(r);
        add_cyl(w, x, z, 5.0f, 3.5f, 0, h, CYL_CHIMNEY, 8);
        add_light(w, v3(x, h + 1, z), 3.2f, col_red, LT_AVIATION, 1, rng_float(r));
        add_light(w, v3(x, h * 0.5f, z + 5.2f), 2.0f, col_red, LT_WARNING, 2, rng_float(r));
        add_poi(w, "chimney", v3(x, h + 4, z), 4.0f, POI_GENERIC, -1);
        reserve(x, z, 10);
    }
    /* the hazard corridor: pipe canyon with steam vents, checkpoint rings */
    float cz = -40;
    for (int i = 0; i < 8; i++) {
        float x = -360.0f + (float)i * 90.0f;
        float y = 18.0f + 14.0f * (float)(i % 3);
        char nm[16]; snprintf(nm, sizeof(nm), "ring_%d", i + 1);
        add_poi(w, nm, v3(x, y, cz + ((i & 1) ? 10.0f : -10.0f)), 9.0f, POI_RING, -1);
        if (i < 7) {
            if (w->n_vents < MAX_VENTS) { Vent *v = &w->vents[w->n_vents++]; v->p = v3(x + 45, 0, cz + ((i & 1) ? -14.0f : 14.0f)); v->r = 9.0f; v->h = 70.0f; }
            add_light(w, v3(x + 45, 2, cz + ((i & 1) ? -14.0f : 14.0f)), 3.0f, col_amber, LT_WARNING, 2, (float)i * 0.13f);
        }
        add_light(w, v3(x, y + 9, cz), 2.0f, col_amber, LT_BEACON, 1, (float)i * 0.1f);
    }
    for (int s = -1; s <= 1; s += 2) {
        add_box(w, -400, 0, cz + (float)s * 32 - 6, 360, 26, cz + (float)s * 32 + 6, TEX_INDUSTRIAL, BOX_BUILDING, 30, 0);
        add_box(w, -400, 44, cz + (float)s * 26 - 2, 360, 48, cz + (float)s * 26 + 2, TEX_METAL, BOX_PIPE, 0, 0);
    }
    add_box(w, -400, 62, cz - 30, 360, 64, cz - 26, TEX_METAL, BOX_PIPE, 0, 0);
    for (int i = 0; i < 9; i++) reserve(-400.0f + (float)i * 95.0f, cz, 40);
    poi_building(w, "substation", 420, cz, 18, 0, 22, TEX_METAL, POI_GENERIC, col_cyan);
    /* fill: big low industrial lots */
    fill_grid(w, r, DISTRICT_WORKS, 110, 30, 0, works_h, works_tex, 2, 0.85f);
    /* overhead pipe networks between lots */
    for (int i = 0; i < 22; i++) {
        float y = 14.0f + 26.0f * rng_float(r);
        float x = -500.0f + 1000.0f * rng_float(r), z = -500.0f + 1000.0f * rng_float(r);
        float len = 60.0f + 140.0f * rng_float(r);
        if (rng_chance(r, 0.5f)) { if (!reserved(x, z - 2, x + len, z + 2)) add_box(w, x, y, z - 1.6f, x + len, y + 3.2f, z + 1.6f, TEX_METAL, BOX_PIPE, 0, 0); }
        else { if (!reserved(x - 2, z, x + 2, z + len)) add_box(w, x - 1.6f, y, z, x + 1.6f, y + 3.2f, z + len, TEX_METAL, BOX_PIPE, 0, 0); }
    }
    /* scattered vents */
    for (int i = 0; i < 6 && w->n_vents < MAX_VENTS; i++) {
        Vent *v = &w->vents[w->n_vents++];
        float vx = -450.0f + 900.0f * rng_float(r), vz = 120.0f + 300.0f * rng_float(r);
        v->p = v3(vx, 0, vz); v->r = 7.0f; v->h = 55.0f;
    }
}

/* ---------------- THE STACKS ---------------- */
static void gen_stacks(World *w, Rng *r) {
    w->ground_y = 0; w->water_y = -100; w->skyline = 160;
    float py = msa_pad(w, 352, -352, 0, 120);
    w->spawn = v3(352, py + 14, -330); w->spawn_yaw = H_PI;
    /* THE CHOIR TOWER */
    float tx = -224, tz = 224;
    reserve(tx, tz, 60);
    add_box(w, tx - 16, 0, tz - 16, tx + 16, 120, tz + 16, TEX_RESIDENTIAL, BOX_LANDMARK, 200, 2);
    add_cyl(w, tx, tz, 7.0f, 4.0f, 120, 290, CYL_SPIRE, 8);
    for (int i = 0; i < 6; i++) {
        float y = 140.0f + (float)i * 25.0f;
        float s = 16.0f - (float)i * 1.6f;
        add_box(w, tx - s, y, tz - s, tx + s, y + 1.5f, tz + s, TEX_METAL, BOX_LANDMARK, 0, 0);
        for (int k = 0; k < 4; k++) {
            float a = (float)k * H_PI * 0.5f + (float)i * 0.4f;
            add_light(w, v3(tx + cosf(a) * s, y + 2, tz + sinf(a) * s), 2.3f, RGBA(200, 210, 255, 255), LT_LANDMARK, 6, (float)i * 0.15f + (float)k * 0.05f);
        }
    }
    add_light(w, v3(tx, 293, tz), 7.0f, col_white, LT_BEACON, 1, 0.2f);
    add_poi(w, "choir", v3(tx, 200, tz), 70.0f, POI_LANDMARK, LM_CHOIR);
    /* residences under investigation */
    poi_building(w, "res_1", 64 + 32, 0 + 32, 13, 0, 96, TEX_RESIDENTIAL, POI_BUILDING, col_amber);
    poi_building(w, "res_2", -128 + 32, -128 + 32, 13, 0, 132, TEX_RESIDENTIAL, POI_BUILDING, col_amber);
    poi_building(w, "res_3", -256 + 32, 64 + 32, 13, 0, 80, TEX_RESIDENTIAL, POI_BUILDING, col_amber);
    /* illegal rooftop transmitters */
    poi_building(w, "tx_1", 192 + 32, 128 + 32, 12, 0, 150, TEX_RESIDENTIAL, POI_TRANSMITTER, col_violet);
    poi_building(w, "tx_2", -64 + 32, 256 + 32, 12, 0, 110, TEX_RESIDENTIAL, POI_TRANSMITTER, col_violet);
    poi_building(w, "tx_3", -384 + 32, -64 + 32, 12, 0, 170, TEX_RESIDENTIAL, POI_TRANSMITTER, col_violet);
    /* fugitive drone route through the narrow streets */
    V3 fr[12] = { v3(256, 62, -256), v3(128, 62, -256), v3(128, 58, -64), v3(0, 70, -64), v3(0, 60, 128), v3(-128, 58, 128),
                  v3(-128, 80, -128), v3(-256, 62, -128), v3(-256, 58, 0), v3(-64, 66, 0), v3(-64, 72, -256), v3(256, 62, -256) };
    add_lane(w, fr, 12, 22, 5, 1, 0);
    add_poi(w, "fugitive_start", fr[0], 20.0f, POI_GENERIC, -1);
    /* traffic in the canyons */
    rect_lane(w, -256, -256, 256, 256, 64, 18, 1, 4, 0);
    rect_lane(w, -384, -128, 128, 384, 92, 20, 1, 4, 1);
    rect_lane(w, -128, -384, 384, 128, 120, 22, 1, 4, 0);
    rect_lane(w, -512, -512, 512, 512, 190, 30, 2, 3, 1);
    rect_lane(w, -256, -256, 64, 64, 150, 14, 3, 2, 0);
    fill_grid(w, r, DISTRICT_STACKS, 64, 18, 0, stacks_h, stacks_tex, 2, 0.97f);
    /* hanging walkways across streets */
    for (int i = 0; i < 60; i++) {
        int gx = rng_range(r, -8, 8), gz = rng_range(r, -8, 8);
        float x = (float)gx * 64.0f, z = (float)gz * 64.0f + 32.0f;
        float y = 20.0f + 30.0f * rng_float(r);
        if (reserved(x - 12, z - 3, x + 12, z + 3)) continue;
        if (rng_chance(r, 0.5f)) {
            add_box(w, x - 10, y, z - 2, x + 10, y + 2.5f, z + 2, TEX_CONCRETE, BOX_WALKWAY, 0, 0);
            add_light(w, v3(x, y - 0.5f, z), 1.6f, col_amber, LT_STREET, 0, 0);
        } else {
            add_box(w, z - 2, y, x - 10, z + 2, y + 2.5f, x + 10, TEX_CONCRETE, BOX_WALKWAY, 0, 0);
            add_light(w, v3(z, y - 0.5f, x), 1.6f, col_amber, LT_STREET, 0, 0);
        }
    }
    for (int i = 0; i < 6; i++) add_ad(w, v3(-300.0f + 110.0f * (float)i, 40.0f + 30.0f * rng_float(r), 0.0f + 64.0f * (float)(i % 3) - 9.0f), 14, 9, 0, 1, 8 + (i % 4), 1);
}

/* ---------------- THE UNDERTOW ---------------- */
static void gen_undertow(World *w, Rng *r) {
    w->ground_y = -24; w->water_y = 0; w->skyline = 80;
    float py = msa_pad(w, 0, 420, -24, 70);
    w->spawn = v3(0, py + 14, 396); w->spawn_yaw = 0.0f;
    /* THE BLACK RESERVOIR: an open circular basin with resonant pillars */
    float rx = -230, rz = -200, R = 70;
    reserve(rx, rz, R + 20);
    for (int i = 0; i < 20; i++) {
        float a = (float)i * H_TAU / 20.0f;
        float x = rx + cosf(a) * R, z = rz + sinf(a) * R;
        float h = (i % 5 == 3) ? 10.0f : 26.0f;   /* broken sections let you fly in low */
        add_box(w, x - 9, -24, z - 9, x + 9, h, z + 9, TEX_CONCRETE, BOX_LANDMARK, 0, 0);
    }
    for (int i = 0; i < 5; i++) {
        float a = (float)i * H_TAU / 5.0f + 0.3f;
        add_cyl(w, rx + cosf(a) * 28, rz + sinf(a) * 28, 3.0f, 3.0f, -24, 38, CYL_PYLON, 6);
        add_light(w, v3(rx + cosf(a) * 28, 39, rz + sinf(a) * 28), 2.4f, col_cyan, LT_LANDMARK, 7, (float)i * 0.2f);
    }
    add_poi(w, "reservoir", v3(rx, 12, rz), 75.0f, POI_LANDMARK, LM_RESERVOIR);
    /* THE SILENT EXCHANGE: a hollow telecom block you descend into */
    float ex = 250, ez = 250;
    reserve(ex, ez, 80);
    float H = 120, o = 46, s = 22;
    add_box(w, ex - o, -24, ez - o, ex + o, H, ez - s, TEX_DERELICT, BOX_LANDMARK, 0, 0);
    add_box(w, ex - o, -24, ez + s, ex + o, H, ez + o, TEX_DERELICT, BOX_LANDMARK, 0, 0);
    add_box(w, ex - o, -24, ez - s, ex - s, H, ez + s, TEX_DERELICT, BOX_LANDMARK, 0, 0);
    add_box(w, ex + s, -24, ez - s, ex + o, H, ez + s, TEX_DERELICT, BOX_LANDMARK, 0, 0);
    for (int i = 0; i < 4; i++) {
        float a = (float)i * H_PI * 0.5f;
        add_box(w, ex + cosf(a) * 34 - 1, H, ez + sinf(a) * 34 - 1, ex + cosf(a) * 34 + 1, H + 40, ez + sinf(a) * 34 + 1, TEX_METAL, BOX_ROOF_DETAIL, 0, 1);
        add_light(w, v3(ex + cosf(a) * 34, H + 41, ez + sinf(a) * 34), 2.5f, col_red, LT_AVIATION, 1, (float)i * 0.25f);
    }
    for (int i = 0; i < 6; i++) add_light(w, v3(ex + (i & 1 ? 20.5f : -20.5f), 100.0f - (float)i * 16.0f, ez), 1.8f, col_white, LT_LANDMARK, 8, (float)i * 0.17f);
    add_poi(w, "exchange_top", v3(ex, H + 20, ez), 30.0f, POI_GENERIC, -1);
    add_poi(w, "exchange", v3(ex, 40, ez), 60.0f, POI_LANDMARK, LM_EXCHANGE);
    add_poi(w, "exchange_core", v3(ex, 8, ez), 10.0f, POI_SIGNAL, -1);
    /* data fragments drifting in the ruins (close to the water) */
    static const float fp[4][3] = {{-96, 6, 96}, {128, 14, -64}, {-320, 9, 128}, {64, 5, -320}};
    for (int i = 0; i < 4; i++) {
        char nm[16]; snprintf(nm, sizeof(nm), "frag_%d", i + 1);
        add_poi(w, nm, v3(fp[i][0], fp[i][1], fp[i][2]), 8.0f, POI_DATA, -1);
        reserve(fp[i][0], fp[i][2], 22);
    }
    /* power pylons with cables across the flood */
    for (int i = 0; i < 8; i++) {
        float x = -480.0f + 130.0f * (float)i, z = 30.0f + 20.0f * rng_float(r);
        if (reserved(x - 4, z - 4, x + 4, z + 4)) continue;
        add_cyl(w, x, z, 3.0f, 1.0f, -24, 70, CYL_PYLON, 4);
        add_light(w, v3(x, 71, z), 2.4f, col_red, LT_AVIATION, 1, rng_float(r));
        if (i < 7) add_box(w, x, 62, z - 0.4f, x + 130, 63, z + 0.4f, TEX_METAL, BOX_PIPE, 0, 1);
    }
    rect_lane(w, -192, -192, 192, 192, 50, 12, 3, 1, 0);
    fill_grid(w, r, DISTRICT_UNDERTOW, 96, 30, -24, under_h, under_tex, 2, 0.7f);
    /* ruins: knock the tops off some buildings and add jagged stubs */
    int n = w->n_boxes;
    for (int i = 0; i < n; i++) {
        Box *b = &w->boxes[i];
        if (b->kind != BOX_BUILDING || b->tex != TEX_DERELICT || !rng_chance(r, 0.4f)) continue;
        float hx = (b->b.x1 - b->b.x0) * 0.3f;
        add_box(w, b->b.x0, b->b.y1, b->b.z0, b->b.x0 + hx, b->b.y1 + 6 + 10 * rng_float(r), b->b.z0 + hx, TEX_DERELICT, BOX_RUIN, b->tint, 0);
    }
    /* sparse lights: a few lit windows and lamps reflected in the water */
    for (int i = 0; i < 40; i++) {
        float x = -560.0f + 1120.0f * rng_float(r), z = -560.0f + 1120.0f * rng_float(r);
        float ly = 1.0f + 3.0f * rng_float(r);
        uint32_t lc = rng_chance(r, 0.7f) ? col_amber : col_cyan;
        int bl = rng_chance(r, 0.2f) ? 2 : 0;
        float ph = rng_float(r);
        add_light(w, v3(x, ly, z), 1.5f, lc, LT_WATER, bl, ph);
    }
}

/* ---------------- collision grid ---------------- */

static void build_grid(World *w) {
    static uint16_t counts[GRID_N * GRID_N];
    memset(counts, 0, sizeof(counts));
    for (int pass = 0; pass < 2; pass++) {
        if (pass == 1) {
            int acc = 0;
            for (int c = 0; c < GRID_N * GRID_N; c++) { w->cell_start[c] = (uint16_t)acc; acc += counts[c]; counts[c] = 0; }
            w->cell_start[GRID_N * GRID_N] = (uint16_t)acc;
            w->n_refs = acc;
        }
        for (int i = 0; i < w->n_boxes; i++) {
            const Box *b = &w->boxes[i];
            if (b->flags & 1) continue;
            int gx0 = h_clampi((int)((b->b.x0 + WORLD_HALF) / GRID_CELL), 0, GRID_N - 1);
            int gx1 = h_clampi((int)((b->b.x1 + WORLD_HALF) / GRID_CELL), 0, GRID_N - 1);
            int gz0 = h_clampi((int)((b->b.z0 + WORLD_HALF) / GRID_CELL), 0, GRID_N - 1);
            int gz1 = h_clampi((int)((b->b.z1 + WORLD_HALF) / GRID_CELL), 0, GRID_N - 1);
            for (int gz = gz0; gz <= gz1; gz++)
                for (int gx = gx0; gx <= gx1; gx++) {
                    int c = gz * GRID_N + gx;
                    if (pass == 1) {
                        int at = w->cell_start[c] + counts[c];
                        if (at < GRID_MAX_REFS) w->refs[at] = (uint16_t)i;
                    }
                    counts[c]++;
                }
        }
    }
    if (w->n_refs > GRID_MAX_REFS) w->n_refs = GRID_MAX_REFS;
}

static int lane_clear(const World *w, const Lane *l, float r) {
    for (float s2 = 0; s2 < l->len; s2 += 4.0f) {
        V3 tan;
        V3 p = lane_eval(l, s2, &tan);
        V3 side = v3(tan.z, 0, -tan.x);
        for (int o = -1; o <= 1; o++) {
            V3 q = v3_madd(p, side, 3.0f * (float)o);
            if (world_collide_sphere(w, &q, r, NULL)) return 0;
        }
    }
    return 1;
}

/* Ground traffic along the street grid. Candidate rectangles that would clip any
 * structure (highway pillars, station legs, corridor walls...) are rejected. */
static void add_street_lanes(World *w) {
    if (w->id == DISTRICT_UNDERTOW || w->pitch <= 0) return;
    float y = w->ground_y + 0.8f;
    int added = 0;
    for (int k = 1; k <= 5 && added < 5; k++) {
        for (int shift = 0; shift < 3 && added < 5; shift++) {
            float c = (float)(shift == 0 ? 0 : (shift == 1 ? -1 : 1)) * w->pitch;
            float h = (float)k * w->pitch;
            if (h + h_absf(c) > WORLD_HALF - 30.0f) continue;
            if (w->n_lanes >= MAX_LANES) return;
            int before = w->n_lanes;
            rect_lane(w, c - h, -h, c + h, h, y, 13.0f + 2.0f * (float)k, 0, (int)(8.0f * h / w->pitch) + 2, (k + shift) & 1);
            if (w->n_lanes > before && !lane_clear(w, &w->lanes[w->n_lanes - 1], 0.7f)) w->n_lanes--;
            else if (w->n_lanes > before) added++;
        }
    }
}

/* Large holographic billboards on tall towers in every district. */
static void add_city_billboards(World *w) {
    Rng r;
    rng_seed(&r, w->gen_seed, 0xB111u);
    float p = w->id == DISTRICT_CIVIC ? 0.3f : (w->id == DISTRICT_STACKS ? 0.22f : (w->id == DISTRICT_WORKS ? 0.12f : 0.04f));
    int n = w->n_boxes;
    for (int i = 0; i < n && w->n_ads < MAX_ADS - 4; i++) {
        const Box *b = &w->boxes[i];
        if (b->kind != BOX_BUILDING || b->b.y1 - b->b.y0 < 60.0f) continue;
        float roll = rng_float(&r);
        int face = rng_int(&r, 4), id = rng_int(&r, 12), fl = rng_chance(&r, 0.3f);
        float hfrac = 0.45f + 0.3f * rng_float(&r);
        if (roll > p) continue;
        float y = b->b.y0 + (b->b.y1 - b->b.y0) * hfrac;
        if (face < 2) {
            float wd = h_minf(b->b.z1 - b->b.z0 - 6.0f, 30.0f);
            if (wd < 10.0f) continue;
            add_sign(w, v3(face == 0 ? b->b.x1 + 0.6f : b->b.x0 - 0.6f, y, (b->b.z0 + b->b.z1) * 0.5f), wd, wd * 0.5f, 1, face == 0 ? 1 : -1, 0, id, fl);
        } else {
            float wd = h_minf(b->b.x1 - b->b.x0 - 6.0f, 30.0f);
            if (wd < 10.0f) continue;
            add_sign(w, v3((b->b.x0 + b->b.x1) * 0.5f, y, face == 2 ? b->b.z1 + 0.6f : b->b.z0 - 0.6f), wd, wd * 0.5f, 0, face == 2 ? 1 : -1, 0, id, fl);   /* readable from outside: +z face runs +x left-to-right */
        }
    }
}

void world_generate(World *w, int district) {
    memset(w, 0, sizeof(*w));
    g_nres = 0;
    w->id = h_clampi(district, 0, DISTRICT_COUNT - 1);
    w->name = district_names[w->id];
    static const uint32_t seeds[DISTRICT_COUNT] = { 0xC1F1C0u, 0x0E0E5u, 0x57AC5u, 0x0DE7u };
    w->gen_seed = seeds[w->id];
    Rng r;
    rng_seed(&r, w->gen_seed, 99);
    switch (w->id) {
    case DISTRICT_CIVIC: gen_civic(w, &r); break;
    case DISTRICT_WORKS: gen_works(w, &r); break;
    case DISTRICT_STACKS: gen_stacks(w, &r); break;
    default: gen_undertow(w, &r); break;
    }
    /* street lamps along the main avenues for every district */
    for (int i = -6; i <= 6; i++) {
        for (int k = 0; k < 2; k++) {
            float t = (float)i * 96.0f + 48.0f;
            V3 p = k ? v3(t, w->ground_y + 7, -2) : v3(-2, w->ground_y + 7, t);
            if (w->water_y > w->ground_y) p.y = w->water_y + 1.5f;
            add_light(w, p, 1.8f, w->id == DISTRICT_WORKS ? col_amber : RGBA(255, 200, 140, 255), LT_STREET, 0, 0);
        }
    }
    build_grid(w);
    add_city_billboards(w);
    add_street_lanes(w);
}

const Poi *world_poi(const World *w, const char *name) {
    for (int i = 0; i < w->n_pois; i++) if (!strcmp(w->pois[i].name, name)) return &w->pois[i];
    return NULL;
}

int world_landmark_poi(const World *w, int landmark) {
    for (int i = 0; i < w->n_pois; i++) if (w->pois[i].kind == POI_LANDMARK && w->pois[i].landmark == landmark) return i;
    return -1;
}

/* ---------------- collision queries ---------------- */

static int cell_of(float v) { return h_clampi((int)((v + WORLD_HALF) / GRID_CELL), 0, GRID_N - 1); }

int world_collide_sphere(const World *w, V3 *p, float r, V3 *normal) {
    int hit = 0;
    V3 n = v3(0, 0, 0);
    /* ground / water */
    float floor_y = (w->water_y > w->ground_y ? w->water_y : w->ground_y) + r;
    if (p->y < floor_y) { p->y = floor_y; n = v3_add(n, v3(0, 1, 0)); hit = 1; }
    if (p->y > WORLD_CEILING) { p->y = WORLD_CEILING; }
    int gx0 = cell_of(p->x - r), gx1 = cell_of(p->x + r), gz0 = cell_of(p->z - r), gz1 = cell_of(p->z + r);
    for (int gz = gz0; gz <= gz1; gz++)
        for (int gx = gx0; gx <= gx1; gx++) {
            int c = gz * GRID_N + gx;
            for (int k = w->cell_start[c]; k < w->cell_start[c + 1] && k < w->n_refs; k++) {
                const Aabb *b = &w->boxes[w->refs[k]].b;
                float cx = h_clampf(p->x, b->x0, b->x1), cy = h_clampf(p->y, b->y0, b->y1), cz = h_clampf(p->z, b->z0, b->z1);
                float dx = p->x - cx, dy = p->y - cy, dz = p->z - cz;
                float d2 = dx * dx + dy * dy + dz * dz;
                if (d2 >= r * r) continue;
                V3 nn;
                if (d2 > 1e-6f) {
                    float d = sqrtf(d2);
                    nn = v3(dx / d, dy / d, dz / d);
                    *p = v3_madd(*p, nn, r - d);
                } else {
                    /* centre inside: push out along the axis of least penetration */
                    float pen[6] = { p->x - b->x0, b->x1 - p->x, p->y - b->y0, b->y1 - p->y, p->z - b->z0, b->z1 - p->z };
                    int mi = 0;
                    for (int q = 1; q < 6; q++) if (pen[q] < pen[mi]) mi = q;
                    static const float ax[6][3] = {{-1,0,0},{1,0,0},{0,-1,0},{0,1,0},{0,0,-1},{0,0,1}};
                    nn = v3(ax[mi][0], ax[mi][1], ax[mi][2]);
                    *p = v3_madd(*p, nn, pen[mi] + r);
                }
                n = v3_add(n, nn);
                hit = 1;
            }
        }
    for (int i = 0; i < w->n_cyls; i++) {
        const Cyl *c = &w->cyls[i];
        if (p->y < c->y0 - r || p->y > c->y1 + r) continue;
        float t = h_clampf((p->y - c->y0) / h_maxf(c->y1 - c->y0, 1.0f), 0.0f, 1.0f);
        float rad = h_lerpf(c->r0, c->r1, t);
        float dx = p->x - c->x, dz = p->z - c->z;
        float d = sqrtf(dx * dx + dz * dz);
        float lim = rad + r;
        if (d >= lim) continue;
        if (c->kind == CYL_COOLING && d < rad - 4.0f && p->y < c->y1 - 2) {
            /* inside the hollow tower: keep away from the inner wall */
            float in = rad - 4.0f - r;
            if (d > in && d > 1e-4f) { V3 nn = v3(-dx / d, 0, -dz / d); p->x = c->x + dx / d * in; p->z = c->z + dz / d * in; n = v3_add(n, nn); hit = 1; }
            continue;
        }
        if (p->y > c->y1 && d < rad) { p->y = c->y1 + r; n = v3_add(n, v3(0, 1, 0)); hit = 1; continue; }
        if (d < 1e-4f) { dx = 1; d = 1; }
        V3 nn = v3(dx / d, 0, dz / d);
        p->x = c->x + nn.x * lim; p->z = c->z + nn.z * lim;
        n = v3_add(n, nn);
        hit = 1;
    }
    if (hit && normal) *normal = v3_norm(n);
    return hit;
}

static int seg_aabb(V3 a, V3 d, const Aabb *b, float *tmin_out) {
    float tmin = 0, tmax = 1;
    const float o[3] = {a.x, a.y, a.z}, dd[3] = {d.x, d.y, d.z};
    const float lo[3] = {b->x0, b->y0, b->z0}, hi[3] = {b->x1, b->y1, b->z1};
    for (int k = 0; k < 3; k++) {
        if (h_absf(dd[k]) < 1e-8f) { if (o[k] < lo[k] || o[k] > hi[k]) return 0; continue; }
        float inv = 1.0f / dd[k];
        float t0 = (lo[k] - o[k]) * inv, t1 = (hi[k] - o[k]) * inv;
        if (t0 > t1) { float t = t0; t0 = t1; t1 = t; }
        if (t0 > tmin) tmin = t0;
        if (t1 < tmax) tmax = t1;
        if (tmin > tmax) return 0;
    }
    *tmin_out = tmin;
    return 1;
}

float world_raycast(const World *w, V3 a, V3 b) {
    V3 d = v3_sub(b, a);
    float best = 1.0f;
    int gx0 = cell_of(h_minf(a.x, b.x)), gx1 = cell_of(h_maxf(a.x, b.x));
    int gz0 = cell_of(h_minf(a.z, b.z)), gz1 = cell_of(h_maxf(a.z, b.z));
    for (int gz = gz0; gz <= gz1; gz++)
        for (int gx = gx0; gx <= gx1; gx++) {
            int c = gz * GRID_N + gx;
            for (int k = w->cell_start[c]; k < w->cell_start[c + 1] && k < w->n_refs; k++) {
                float t;
                if (seg_aabb(a, d, &w->boxes[w->refs[k]].b, &t) && t < best) best = t;
            }
        }
    float fy = w->water_y > w->ground_y ? w->water_y : w->ground_y;
    if (b.y < fy && a.y > fy) { float t = (a.y - fy) / (a.y - b.y); if (t < best) best = t; }
    return best;
}

float world_enclosure(const World *w, V3 p, float radius) {
    int blocked = 0;
    for (int i = 0; i < 8; i++) {
        float a = (float)i * H_TAU / 8.0f;
        V3 q = v3(p.x + cosf(a) * radius, p.y, p.z + sinf(a) * radius);
        if (world_raycast(w, p, q) < 0.999f) blocked++;
    }
    return (float)blocked / 8.0f;
}

float world_height_at(const World *w, float x, float z) {
    float h = w->water_y > w->ground_y ? w->water_y : w->ground_y;
    int c = cell_of(z) * GRID_N + cell_of(x);
    for (int k = w->cell_start[c]; k < w->cell_start[c + 1] && k < w->n_refs; k++) {
        const Aabb *b = &w->boxes[w->refs[k]].b;
        if (x >= b->x0 && x <= b->x1 && z >= b->z0 && z <= b->z1 && b->y1 > h) h = b->y1;
    }
    return h;
}

uint32_t world_fingerprint(const World *w) {
    uint32_t h = H_FNV_INIT;
    for (int i = 0; i < w->n_boxes; i++) {
        const float *f = &w->boxes[i].b.x0;
        for (int k = 0; k < 6; k++) h = h_fnv1a(h, (uint32_t)(int32_t)floorf(f[k] * 16.0f + 0.5f));
        h = h_fnv1a(h, w->boxes[i].tex | ((uint32_t)w->boxes[i].kind << 8));
    }
    for (int i = 0; i < w->n_lights; i++) { h = h_fnv1a(h, (uint32_t)(int32_t)floorf(w->lights[i].p.x * 16.0f + 0.5f)); h = h_fnv1a(h, (uint32_t)(int32_t)floorf(w->lights[i].p.z * 16.0f + 0.5f)); }
    for (int i = 0; i < w->n_vents; i++) h = h_fnv1a(h, (uint32_t)(int32_t)floorf(w->vents[i].p.x * 16.0f + 0.5f));
    for (int i = 0; i < w->n_pois; i++) h = h_fnv1a(h, (uint32_t)(int32_t)floorf(w->pois[i].p.y * 16.0f + 0.5f));
    return h;
}
