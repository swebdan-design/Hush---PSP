/* HUSH — autopilot. Strategy: climb to a cruise height clear of everything
 * along the route, traverse, then descend vertically onto a free approach
 * point; follow/pursue targets along their own (collision-free) trail. */
#include "autopilot.h"
#include <string.h>

void autopilot_reset(Autopilot *a) { memset(a, 0, sizeof(*a)); a->last_step = -1; a->rng = 0x51A7u; }

static float route_cruise(const World *w, V3 a, V3 b) {
    float top = h_maxf(a.y, b.y);
    for (int i = 0; i <= 40; i++) {
        V3 p = v3_lerp(a, b, (float)i / 40.0f);
        for (int ox = -1; ox <= 1; ox++)
            for (int oz = -1; oz <= 1; oz++) top = h_maxf(top, world_height_at(w, p.x + (float)ox * 8.0f, p.z + (float)oz * 8.0f));
        for (int c = 0; c < w->n_cyls; c++) {
            const Cyl *cy = &w->cyls[c];
            float dx = p.x - cy->x, dz = p.z - cy->z;
            if (dx * dx + dz * dz < (cy->r0 + 14) * (cy->r0 + 14)) top = h_maxf(top, cy->y1);
        }
    }
    return h_minf(top + 25.0f, WORLD_CEILING - 10.0f);
}

static int point_free(const World *w, V3 p) { V3 q = p; return !world_collide_sphere(w, &q, CRAFT_RADIUS + 1.0f, NULL); }

static V3 approach_point(const World *w, V3 target, float max_r, int need_scan, float range) {
    for (float rad = 0; rad <= max_r; rad += 6.0f)
        for (float dy = -4; dy <= 40; dy += 6.0f)
            for (int k = 0; k < 12; k++) {
                float a = (float)k * H_TAU / 12.0f;
                V3 p = v3(target.x + cosf(a) * rad, target.y + dy, target.z + sinf(a) * rad);
                if (!point_free(w, p)) continue;
                V3 top = v3(p.x, WORLD_CEILING - 12.0f, p.z);
                if (world_raycast(w, top, p) < 0.999f) continue;
                if (need_scan) {
                    Craft c; memset(&c, 0, sizeof(c)); c.pos = p;
                    c.yaw = atan2f(target.x - p.x, target.z - p.z);
                    if (!mission_can_scan(w, &c, target, range)) continue;
                }
                return p;
            }
    return target;
}

static void steer(const Craft *c, V3 goal, FlightInput *in, float max_fwd, int face_goal, V3 face) {
    V3 d = v3_sub(goal, c->pos);
    float dh = sqrtf(d.x * d.x + d.z * d.z);
    V3 look = face_goal ? d : v3_sub(face, c->pos);
    float diff = h_wrap_angle(atan2f(look.x, look.z) - c->yaw);
    in->yaw = h_clampf(-diff * 2.5f, -1.0f, 1.0f);   /* positive input turns right = decreasing yaw */
    V3 f = craft_forward(c), r = craft_right(c);
    V3 dn = dh > 0.01f ? v3_scale(v3(d.x, 0, d.z), 1.0f / dh) : v3(0, 0, 0);
    float spd = h_clampf(dh / 25.0f, 0.0f, 1.0f) * max_fwd;
    float vtoward = v3_dot(c->vel, dn);
    in->brake = (dh < vtoward * 1.1f && dh < 60.0f) ? 1 : 0;
    in->fwd = h_clampf(v3_dot(dn, f) * spd * 1.2f, -1.0f, 1.0f);
    in->strafe = h_clampf(v3_dot(dn, r) * spd * 1.2f, -1.0f, 1.0f);
    in->lift = h_clampf(d.y * 0.15f - c->vel.y * 0.08f, -1.0f, 1.0f);
}

static int nav_to(Autopilot *n, const World *w, const Craft *c, V3 goal, FlightInput *in, int face, V3 facept) {
    if (!n->have_goal || v3_dist(n->goal, goal) > 1.0f) { n->have_goal = 1; n->goal = goal; n->phase = 0; n->cruise = route_cruise(w, c->pos, goal); }
    float dh = v3_dist_xz(c->pos, goal);
    if (dh > 10.0f) {
        if (c->pos.y < n->cruise - 4.0f && n->phase == 0) {
            steer(c, v3(c->pos.x, n->cruise, c->pos.z), in, 0, 0, goal);
            in->fwd = 0; in->strafe = 0; in->lift = 1.0f;
            return 0;
        }
        n->phase = 1;
        steer(c, v3(goal.x, n->cruise, goal.z), in, 1.0f, 1, goal);
        return 0;
    }
    steer(c, goal, in, 0.6f, !face, facept);
    return v3_dist(c->pos, goal) < 3.0f;
}

void autopilot_mission(Autopilot *a, const World *w, const Traffic *t, const Craft *c, const MissionRun *m, FlightInput *in, MissionInput *mi) {
    memset(in, 0, sizeof(*in));
    memset(mi, 0, sizeof(*mi));
    if (m->state != MS_ACTIVE) return;
    const MissionStep *s = &hush_missions[m->mission].steps[m->step];
    if (m->step != a->last_step) {
        a->last_step = m->step; a->have_goal = 0; a->trail_n = 0;
        if (s->type == OB_SCAN) a->ap = approach_point(w, m->target, 70.0f, 1, m->scanner_range * 0.9f);
        else if (s->type == OB_LANDMARK) a->ap = approach_point(w, m->target, m->target_r * 0.7f, 0, 0);
        else if (s->type == OB_DOCK) a->ap = v3_add(m->target, v3(0, 3.0f, 0));
        else if (s->type == OB_LISTEN) a->ap = m->target;
        else if (s->type == OB_GOTO || s->type == OB_LOCATE) a->ap = approach_point(w, m->target, h_maxf(m->target_r - 3.0f, 2.0f), 0, 0);
    }
    switch (s->type) {
    case OB_GOTO: case OB_LOCATE: nav_to(a, w, c, a->ap, in, 0, a->ap); break;
    case OB_SCAN: case OB_LANDMARK:
        if (nav_to(a, w, c, a->ap, in, 1, m->target) || v3_dist(c->pos, a->ap) < 6.0f) {
            float want = atan2f(m->target.x - c->pos.x, m->target.z - c->pos.z);
            in->yaw = h_clampf(-h_wrap_angle(want - c->yaw) * 2.5f, -1, 1);
            mi->scan_held = 1;
        }
        break;
    case OB_DOCK: {
        V3 above = v3(m->target.x, m->target.y + 3.0f, m->target.z);
        nav_to(a, w, c, above, in, 0, above);
        if (v3_dist_xz(c->pos, above) < 4.0f) { in->lift = h_clampf((above.y - c->pos.y) * 0.1f, -0.4f, 0.4f); in->brake = 1; }
        break;
    }
    case OB_LISTEN:
        if (v3_dist(c->pos, a->ap) > 4.0f) nav_to(a, w, c, a->ap, in, 0, a->ap);
        else { memset(in, 0, sizeof(*in)); if (c->speed > 1.0f) in->brake = 1; }
        break;
    case OB_FOLLOW: case OB_PURSUE: {
        if (m->vehicle < 0) break;
        const Vehicle *v = &t->v[m->vehicle];
        if (a->trail_n == 0 || v3_dist(a->trail[(a->trail_n - 1) % 512], v->pos) > 3.0f) a->trail[a->trail_n++ % 512] = v->pos;
        float want_d = s->type == OB_FOLLOW ? 70.0f : 18.0f;
        V3 goal = v->pos;
        int lo = a->trail_n > 512 ? a->trail_n - 512 : 0;
        for (int k = a->trail_n - 1; k >= lo; k--) {
            goal = a->trail[k % 512];
            if (v3_dist(a->trail[k % 512], v->pos) >= want_d) break;
        }
        if (v3_dist(c->pos, v->pos) > 220.0f) nav_to(a, w, c, v->pos, in, 0, v->pos);
        else { steer(c, goal, in, 1.0f, 1, goal); in->boost = s->type == OB_PURSUE && v3_dist(c->pos, v->pos) > 60.0f; }
        if (s->type == OB_PURSUE && v3_dist(c->pos, v->pos) < 30.0f) mi->action_pressed = 1;
        break;
    }
    case OB_COLLECT: case OB_RINGS:
        if (s->type == OB_RINGS) steer(c, m->target, in, 0.9f, 1, m->target);
        else { V3 g = approach_point(w, m->target, 4.0f, 0, 0); nav_to(a, w, c, g, in, 0, g); }
        break;
    }
    mi->any_input = h_absf(in->fwd) > 0.05f || h_absf(in->strafe) > 0.05f || h_absf(in->yaw) > 0.05f || h_absf(in->lift) > 0.05f || in->boost || in->brake;
}

static float arand(Autopilot *a) { a->rng ^= a->rng << 13; a->rng ^= a->rng >> 17; a->rng ^= a->rng << 5; return (float)(a->rng >> 8) / 16777216.0f; }

void autopilot_wander(Autopilot *a, const World *w, const Craft *c, FlightInput *in, float dt) {
    memset(in, 0, sizeof(*in));
    a->wander_t -= dt;
    if (a->wander_t <= 0 || v3_dist(c->pos, a->wander) < 20.0f) {
        /* next sight: a landmark sometimes, otherwise a point above a random street */
        if (arand(a) < 0.3f && w->n_pois) {
            const Poi *p = &w->pois[(int)(arand(a) * (float)w->n_pois) % w->n_pois];
            a->wander = v3(p->p.x, h_maxf(p->p.y + 40.0f, w->skyline * 0.6f), p->p.z + 60.0f);
        } else {
            float x = (arand(a) - 0.5f) * 900.0f, z = (arand(a) - 0.5f) * 900.0f;
            a->wander = v3(x, world_height_at(w, x, z) + 30.0f + 50.0f * arand(a), z);
        }
        a->wander_t = 40.0f;
        a->have_goal = 0;
    }
    nav_to(a, w, c, a->wander, in, 0, a->wander);
    in->fwd *= 0.7f; in->strafe *= 0.7f; in->yaw *= 0.6f; in->boost = 0;
}
