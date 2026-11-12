/* HUSH — signal hunting. */
#include "signals.h"
#include <string.h>

void hunt_reset(Hunt *h) { memset(h, 0, sizeof(*h)); h->nearest = -1; h->found = -1; }

int hunt_count_district(const World *w, uint32_t mask) {
    int n = 0;
    for (int i = 0; i < w->n_signals; i++) n += (mask >> w->signals[i].id) & 1;
    return n;
}

void hunt_update(Hunt *h, const World *w, const Craft *c, uint32_t mask, int scan_held, float dt) {
    h->found = -1;
    h->ping = 0;
    int best = -1;
    float bd = HUNT_RANGE;
    for (int i = 0; i < w->n_signals; i++) {
        if ((mask >> w->signals[i].id) & 1) continue;
        float d = v3_dist(w->signals[i].p, c->pos);
        if (d < bd) { bd = d; best = i; }
    }
    h->nearest = best;
    if (best < 0) { h->strength = 0; h->capture = 0; return; }
    V3 d = v3_sub(w->signals[best].p, c->pos);
    V3 dh = v3_norm(v3(d.x, 0, d.z));
    float facing = v3_dot(dh, craft_forward(c));
    float side = v3_dot(dh, craft_right(c));
    h->bearing = atan2f(side, facing);
    h->dist = bd;
    float base = 1.0f - bd / HUNT_RANGE;
    base = base * base;
    float f = 0.5f + 0.5f * facing;
    h->strength = h_clampf(base * (0.45f + 0.55f * f * f) + (bd < 40.0f ? 0.25f : 0.0f), 0.0f, 1.0f);
    if (scan_held) {
        /* Geiger-style: the closer and the better aimed, the faster it ticks */
        h->ping_t -= dt;
        if (h->ping_t <= 0) { h->ping = 1; h->ping_t = h_lerpf(1.3f, 0.11f, h->strength); }
        if (bd < HUNT_CAPTURE_R) {
            h->capture += dt / HUNT_CAPTURE_TIME;
            if (h->capture >= 1.0f) { h->found = w->signals[best].id; h->capture = 0; }
        } else h->capture = h_maxf(0.0f, h->capture - dt);
    } else { h->capture = h_maxf(0.0f, h->capture - dt * 0.5f); h->ping_t = 0; }
}
