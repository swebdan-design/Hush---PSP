/* HUSH — pedestrians walk loops around the raised sidewalks of city blocks,
 * 0.9 m in from the kerb (buildings sit at least 1.5 m back, so paths are clear). */
#include "crowd.h"
#include "../core/rng.h"
#include <string.h>

#define RGBA(r, g, b, a) ((uint32_t)(a) << 24 | (uint32_t)(b) << 16 | (uint32_t)(g) << 8 | (uint32_t)(r))
#define INSET 0.9f

static float perimeter(const Block *b) { return 2.0f * ((b->x1 - b->x0 - 2 * INSET) + (b->z1 - b->z0 - 2 * INSET)); }

static V3 loop_pos(const Block *b, float t, float y, float *yaw, float dir) {
    float x0 = b->x0 + INSET, x1 = b->x1 - INSET, z0 = b->z0 + INSET, z1 = b->z1 - INSET;
    float wx = x1 - x0, wz = z1 - z0, P = 2 * (wx + wz);
    t = fmodf(t, P); if (t < 0) t += P;
    V3 p; float hx, hz;
    if (t < wx) { p = v3(x0 + t, y, z0); hx = 1; hz = 0; }
    else if (t < wx + wz) { p = v3(x1, y, z0 + (t - wx)); hx = 0; hz = 1; }
    else if (t < 2 * wx + wz) { p = v3(x1 - (t - wx - wz), y, z1); hx = -1; hz = 0; }
    else { p = v3(x0, y, z1 - (t - 2 * wx - wz)); hx = 0; hz = -1; }
    *yaw = atan2f(hx * dir, hz * dir);
    return p;
}

static uint32_t xr(Crowd *c) { uint32_t x = c->rng; x ^= x << 13; x ^= x >> 17; x ^= x << 5; c->rng = x; return x; }
static float xf(Crowd *c) { return (float)(xr(c) >> 8) / 16777216.0f; }

void crowd_init(Crowd *c, const World *w) {
    memset(c, 0, sizeof(*c));
    c->rng = w->gen_seed * 2654435761u + 77u;
    if (!c->rng) c->rng = 1;
    if (w->n_blocks == 0) return;
    int want = w->id == DISTRICT_STACKS ? 200 : (w->id == DISTRICT_CIVIC ? 150 : (w->id == DISTRICT_WORKS ? 60 : 0));
    static const uint32_t coats[8] = { RGBA(40, 40, 46, 255), RGBA(70, 60, 52, 255), RGBA(30, 34, 50, 255), RGBA(90, 30, 40, 255),
                                       RGBA(50, 50, 40, 255), RGBA(120, 120, 128, 255), RGBA(20, 20, 22, 255), RGBA(60, 40, 70, 255) };
    static const uint32_t umbs[6] = { RGBA(255, 60, 190, 255), RGBA(60, 230, 255, 255), RGBA(160, 100, 255, 255),
                                      RGBA(255, 175, 60, 255), RGBA(150, 255, 100, 255), RGBA(230, 235, 245, 255) };
    for (int i = 0; i < want && i < MAX_PEDS; i++) {
        Ped *p = &c->p[c->n++];
        p->active = 1;
        p->block = (int16_t)(xr(c) % (uint32_t)w->n_blocks);
        p->t = xf(c) * perimeter(&w->blocks[p->block]);
        p->speed = 1.0f + 0.7f * xf(c);
        p->dir = xf(c) < 0.5f ? -1.0f : 1.0f;
        p->phase = xf(c) * H_TAU;
        p->umbrella = xf(c) < 0.75f;
        p->loiter = xf(c) < 0.18f;
        p->style = (uint8_t)(xr(c) % 4);
        p->coat = coats[xr(c) % 8];
        p->umb = umbs[xr(c) % 6];
        p->chatter = -1;
        p->pos = loop_pos(&w->blocks[p->block], p->t, w->ground_y + 0.35f, &p->yaw, p->dir);
    }
}

void crowd_update(Crowd *c, const World *w, V3 craft, float rain, float dt) {
    float y = w->ground_y + 0.35f;
    c->tick++;
    if (w->n_blocks == 0) return;
    for (int i = 0; i < c->n; i++) {
        Ped *p = &c->p[i];
        if (!p->active) continue;
        /* keep the crowd where the player is: walkers far away reappear on a nearby block */
        if ((i & 15) == (c->tick & 15)) {
            float fx = p->pos.x - craft.x, fz = p->pos.z - craft.z;
            if (fx * fx + fz * fz > 190.0f * 190.0f) {
                for (int tries = 0; tries < 8; tries++) {
                    int bi = (int)(xr(c) % (uint32_t)w->n_blocks);
                    const Block *nb = &w->blocks[bi];
                    float cx = (nb->x0 + nb->x1) * 0.5f - craft.x, cz = (nb->z0 + nb->z1) * 0.5f - craft.z;
                    if (cx * cx + cz * cz < 140.0f * 140.0f) {
                        p->block = (int16_t)bi;
                        p->t = xf(c) * perimeter(nb);
                        break;
                    }
                }
            }
        }
        const Block *b = &w->blocks[p->block];
        /* the Moth's downwash is frightening at head height */
        float dx = craft.x - p->pos.x, dz = craft.z - p->pos.z;
        float near = (dx * dx + dz * dz < 18.0f * 18.0f) && craft.y < y + 14.0f;
        p->flee = h_approach(p->flee, near ? 1.0f : 0.0f, near ? 6.0f : 0.4f, dt);
        if (near && p->wait > 0) p->wait = 0;
        if (near) {
            /* run along the sidewalk away from the craft */
            float yaw_tmp;
            V3 ahead = loop_pos(b, p->t + p->dir * 2.0f, y, &yaw_tmp, p->dir);
            float da = (ahead.x - craft.x) * (ahead.x - craft.x) + (ahead.z - craft.z) * (ahead.z - craft.z);
            float dn = dx * dx + dz * dz;
            if (da < dn) p->dir = -p->dir;
        }
        float spd = p->speed * (1.0f + 2.2f * p->flee);
        if (p->wait > 0) { p->wait -= dt; spd = 0; }
        else if (p->loiter && xf(c) < 0.004f) p->wait = 3.0f + 8.0f * xf(c);   /* window shopping */
        p->t += p->dir * spd * dt;
        p->phase += spd * dt * 4.2f;
        p->pos = loop_pos(b, p->t, y, &p->yaw, p->dir);
        /* umbrellas go up when it rains, mostly */
        p->umbrella = (uint8_t)(rain > 0.15f ? ((i % 4) != 0) : ((i % 9) == 0));
    }
}

int crowd_nearest(const Crowd *c, V3 p, float r) {
    int best = -1;
    float bd = r * r;
    for (int i = 0; i < c->n; i++) {
        if (!c->p[i].active) continue;
        float d = v3_len2(v3_sub(c->p[i].pos, p));
        if (d < bd) { bd = d; best = i; }
    }
    return best;
}
