/* HUSH — traffic simulation. Vehicles follow world lanes with per-vehicle offsets
 * so the corridors read as streams rather than single files. */
#include "traffic.h"
#include "../core/rng.h"
#include <string.h>

#define RGBA(r, g, b, a) ((uint32_t)(a) << 24 | (uint32_t)(b) << 16 | (uint32_t)(g) << 8 | (uint32_t)(r))

static void place(Vehicle *v, const World *w) {
    const Lane *l = &w->lanes[v->lane];
    V3 tan;
    V3 p = lane_eval(l, v->s, &tan);
    V3 side = v3(tan.z, 0, -tan.x);
    v->pos = v3_add(v3_madd(p, side, v->off_x), v3(0, v->off_y, 0));
    v->dir = tan;
    v->yaw = atan2f(tan.x, tan.z);
}

void traffic_init(Traffic *t, const World *w) {
    memset(t, 0, sizeof(*t));
    Rng r;
    rng_seed(&r, w->gen_seed, 7);
    static const uint32_t cols[4] = { RGBA(200, 205, 215, 255), RGBA(90, 95, 110, 255), RGBA(150, 120, 90, 255), RGBA(60, 70, 80, 255) };
    for (int li = 0; li < w->n_lanes; li++) {
        const Lane *l = &w->lanes[li];
        if (!l->loop) continue;
        for (int k = 0; k < l->count && t->n < MAX_VEH - 8; k++) {
            Vehicle *v = &t->v[t->n++];
            memset(v, 0, sizeof(*v));
            v->active = 1;
            v->kind = l->kind == 0 ? VK_GROUND : (l->kind == 2 ? VK_TRANSPORT : (l->kind == 3 ? VK_PATROL : VK_CAR));
            v->lane = (uint8_t)li;
            v->s = l->len * ((float)k + 0.3f * rng_float(&r)) / (float)h_maxi(1, l->count);
            v->base_speed = v->speed = l->speed * (0.85f + 0.3f * rng_float(&r));
            v->off_x = v->kind == VK_GROUND ? (rng_chance(&r, 0.5f) ? -3.0f : 3.0f) : rng_bipolar(&r) * 4.0f;
            v->off_y = v->kind == VK_GROUND ? 0.0f : rng_bipolar(&r) * 3.0f;
            v->color = cols[rng_int(&r, 4)];
            place(v, w);
        }
    }
}

int traffic_spawn(Traffic *t, const World *w, int lane, float s, float speed, int kind) {
    if (t->n >= MAX_VEH || lane < 0 || lane >= w->n_lanes) return -1;
    Vehicle *v = &t->v[t->n];
    memset(v, 0, sizeof(*v));
    v->active = 1; v->kind = (uint8_t)kind; v->lane = (uint8_t)lane; v->mission = 1;
    v->s = s; v->speed = v->base_speed = speed;
    v->color = kind == VK_DRONE ? RGBA(60, 60, 64, 255) : RGBA(30, 32, 40, 255);
    place(v, w);
    return t->n++;
}

int traffic_find_lane(const World *w, int kind) {
    for (int i = 0; i < w->n_lanes; i++) if (w->lanes[i].kind == kind) return i;
    return -1;
}

void traffic_update(Traffic *t, const World *w, float dt, V3 craft) {
    t->time += dt;
    t->patrol_near = -1;
    for (int i = 0; i < t->n; i++) {
        Vehicle *v = &t->v[i];
        if (!v->active) continue;
        if (v->disabled) {
            /* disabled drones drop to the street */
            v->fall_vel += 9.8f * dt;
            v->pos.y -= v->fall_vel * dt;
            float fy = (w->water_y > w->ground_y ? w->water_y : w->ground_y) + 1.0f;
            if (v->pos.y < fy) { v->pos.y = fy; v->fall_vel = 0; }
            continue;
        }
        const Lane *l = &w->lanes[v->lane];
        if (!v->mission) {
            float target = v->base_speed;
            if (v->kind == VK_GROUND) {
                /* follow the car ahead in the same lane */
                for (int j = 0; j < t->n; j++) {
                    const Vehicle *o = &t->v[j];
                    if (j == i || !o->active || o->lane != v->lane || o->off_x != v->off_x) continue;
                    float gap = o->s - v->s;
                    if (l->loop) { if (gap < 0) gap += l->len; }
                    if (gap > 0 && gap < 16.0f) target = h_minf(target, o->speed * h_clampf((gap - 6.0f) / 10.0f, 0.0f, 1.0f));
                }
                /* a Moth hovering in the lane ahead: stop, wait, lean on the horn */
                V3 rel = v3_sub(craft, v->pos);
                float ahead = v3_dot(rel, v->dir);
                float lateral = h_absf(v3_dot(rel, v3(v->dir.z, 0, -v->dir.x)));
                if (ahead > 0.0f && ahead < 22.0f && lateral < 6.0f && craft.y - v->pos.y < 7.0f) {
                    target = 0.0f;
                    v->blocked += dt;
                    v->horn_cd -= dt;
                    if (v->blocked > 1.2f && v->horn_cd <= 0.0f) { t->horns++; t->horn_pos = v->pos; v->horn_cd = 2.5f + (float)(i % 3); }
                } else { v->blocked = 0; v->horn_cd = 0; }
            }
            /* accelerate gently, brake firmly */
            float a = target < v->speed ? 9.0f : 4.0f;
            v->speed += h_clampf(target - v->speed, -a * dt, a * dt);
        }
        v->s += v->speed * dt;
        if (!l->loop && v->s > l->len) v->s = l->len;
        place(v, w);
        if (!v->mission && v->kind != VK_GROUND) {
            /* flying traffic swerves above or below a Moth in its path */
            float d = v3_dist(v->pos, craft);
            float want = 0;
            if (d < 30.0f) want = (v->pos.y >= craft.y ? 1.0f : -1.0f) * (30.0f - d) * 0.45f;
            v->avoid_y = h_approach(v->avoid_y, want, 2.5f, dt);
            v->pos.y += sinf(t->time * 0.7f + (float)i) * 0.4f + v->avoid_y;
            if (v->kind == VK_PATROL && d < 70.0f) t->patrol_near = i;
        }
    }
}

float traffic_density(const Traffic *t, V3 p, float r) {
    float n = 0;
    for (int i = 0; i < t->n; i++) {
        if (!t->v[i].active) continue;
        float d = v3_dist(t->v[i].pos, p);
        if (d < r) n += 1.0f - d / r;
    }
    return h_clampf(n / 4.0f, 0.0f, 1.0f);
}
