/* HUSH — mission runtime. */
#include "missions.h"
#include <stdio.h>
#include <string.h>

static void push(MissionRun *m, int type, int arg, const char *speaker, const char *text) {
    if (m->n_ev >= 8) return;
    MissionEvent *e = &m->ev[m->n_ev++];
    e->type = (uint8_t)type; e->arg = (int8_t)arg; e->speaker = speaker; e->text = text;
}

static const MissionStep *cur(const MissionRun *m) { return &hush_missions[m->mission].steps[m->step]; }

int mission_can_scan(const World *w, const Craft *c, V3 target, float range) {
    V3 eye = v3_add(c->pos, v3(0, 0.5f, 0));
    V3 d = v3_sub(target, eye);
    float dist = v3_len(d);
    if (dist > range) return 0;
    if (dist < 12.0f) return 1;
    V3 f = craft_forward(c);
    V3 dh = v3_norm(v3(d.x, 0, d.z));
    float facing = v3_dot(f, dh);
    /* generous when the target is mostly above/below us */
    float vertical = h_absf(d.y) / dist;
    if (facing < 0.78f && vertical < 0.8f) return 0;
    /* line of sight to a point slightly short of the target (targets sit on geometry) */
    V3 aim = v3_madd(target, v3_norm(d), -4.0f);
    return world_raycast(w, eye, aim) > 0.985f;
}

int scanner_landmark_in_view(const World *w, const Craft *c, float range, float *dist) {
    int best = -1;
    float bd = 1e9f;
    for (int i = 0; i < w->n_pois; i++) {
        const Poi *p = &w->pois[i];
        if (p->kind != POI_LANDMARK) continue;
        float d = v3_dist(p->p, c->pos);
        if (d > range + p->r) continue;
        if (d < p->r || mission_can_scan(w, c, p->p, range + p->r)) { if (d < bd) { bd = d; best = p->landmark; } }
    }
    if (dist) *dist = bd;
    return best;
}

static void set_target(MissionRun *m, const World *w, const char *name) {
    const Poi *p = world_poi(w, name);
    if (p) { m->target = p->p; m->target_r = p->r; m->marker = p->p; m->marker_on = 1; }
    else { m->marker_on = 0; }
}

static void format_objective(MissionRun *m) {
    const MissionStep *s = cur(m);
    int n = 0;
    for (int i = 0; i < 8; i++) if (m->collected & (1 << i)) n++;
    if (s->type == OB_RINGS) n = m->ring;
    snprintf(m->objective, sizeof(m->objective), s->text, n);
}

static void begin_step(MissionRun *m, const World *w, Traffic *t) {
    const MissionStep *s = cur(m);
    m->step_time = 0; m->scan_progress = 0; m->warn_time = 0; m->dock_time = 0; m->listen_time = 0;
    m->timer = s->time_limit;
    m->collected = 0; m->ring = 0; m->vehicle = -1;
    m->marker_on = 0;
    switch (s->type) {
    case OB_FOLLOW: case OB_PURSUE: {
        int kind = s->type == OB_FOLLOW ? 4 : 5;
        int lane = -1;
        for (int i = 0; i < w->n_lanes; i++) if (w->lanes[i].kind == kind) { lane = i; break; }
        if (lane >= 0) {
            m->vehicle = traffic_spawn(t, w, lane, 0.0f, w->lanes[lane].speed, s->type == OB_FOLLOW ? VK_COURIER : VK_DRONE);
            if (m->vehicle >= 0) { m->marker = t->v[m->vehicle].pos; m->marker_on = 1; }
        }
        break;
    }
    case OB_LOCATE: set_target(m, w, s->poi); m->marker_on = 0; break;
    case OB_COLLECT: {
        char nm[24];
        snprintf(nm, sizeof(nm), "%s1", s->poi);
        set_target(m, w, nm);
        break;
    }
    case OB_RINGS: {
        char nm[24];
        snprintf(nm, sizeof(nm), "%s1", s->poi);
        set_target(m, w, nm);
        break;
    }
    default: set_target(m, w, s->poi); break;
    }
    format_objective(m);
    if (s->msg) push(m, MEVT_TRANSMISSION, 0, s->speaker, s->msg);
}

void mission_start(MissionRun *m, int mission, const World *w, Traffic *t, const Craft *c, int scanner_level, int has_emp) {
    (void)c;
    memset(m, 0, sizeof(*m));
    m->mission = h_clampi(mission, 0, MISSION_COUNT - 1);
    m->state = MS_ACTIVE;
    m->scanner_range = 110.0f + 45.0f * (float)scanner_level;
    m->has_emp = has_emp || (hush_missions[m->mission].grants_at_start & RW_EMP);
    begin_step(m, w, t);
}

static void finish_step(MissionRun *m, const World *w, Traffic *t) {
    const MissionStep *s = cur(m);
    if (s->done) push(m, MEVT_TRANSMISSION, 1, s->done_speaker ? s->done_speaker : "SCANNER", s->done);
    push(m, MEVT_STEP_DONE, (int8_t)m->step, NULL, NULL);
    m->step++;
    if (m->step >= hush_missions[m->mission].n_steps) {
        m->state = MS_COMPLETE;
        m->marker_on = 0;
        push(m, MEVT_COMPLETE, (int8_t)m->mission, NULL, NULL);
        return;
    }
    begin_step(m, w, t);
}

static void fail(MissionRun *m, const char *why) {
    m->state = MS_FAILED;
    snprintf(m->fail_reason, sizeof(m->fail_reason), "%s", why);
    push(m, MEVT_FAILED, (int8_t)m->mission, NULL, m->fail_reason);
}

static float signal_strength(const Craft *c, V3 src, float range) {
    V3 d = v3_sub(src, c->pos);
    float dist = v3_len(d);
    float base = h_clampf(1.0f - dist / range, 0.0f, 1.0f);
    V3 dh = v3_norm(v3(d.x, 0, d.z));
    float facing = 0.5f + 0.5f * v3_dot(dh, craft_forward(c));
    return h_clampf(base * (0.55f + 0.45f * facing * facing), 0.0f, 1.0f);
}

void mission_update(MissionRun *m, const World *w, Traffic *t, const Craft *c, const MissionInput *in, float dt) {
    m->n_ev = 0;
    if (m->state != MS_ACTIVE) return;
    const MissionStep *s = cur(m);
    m->step_time += dt;
    m->total_time += dt;
    m->pursuit *= expf(-0.5f * dt);
    if (m->emp_cooldown > 0) m->emp_cooldown -= dt;
    if (c->hull <= 0.0f) { fail(m, "MOTH DISABLED"); return; }
    if (s->time_limit > 0) {
        m->timer -= dt;
        if (m->timer <= 0) { fail(m, "OUT OF TIME"); return; }
    }
    float d = v3_dist(c->pos, m->target);
    m->signal = 0;
    switch (s->type) {
    case OB_GOTO:
        if (d < m->target_r) finish_step(m, w, t);
        break;
    case OB_SCAN: case OB_LANDMARK: {
        int in_zone = s->type == OB_LANDMARK ? d < m->target_r * 0.85f : 1;
        int ok = in->scan_held && (s->type == OB_LANDMARK ? (in_zone || mission_can_scan(w, c, m->target, m->scanner_range)) : mission_can_scan(w, c, m->target, m->scanner_range));
        if (s->type == OB_LANDMARK && in_zone && !in->scan_held) m->signal = 1.0f;
        float need = s->param > 0 ? s->param : 2.0f;
        if (ok) m->scan_progress += dt / need;
        else m->scan_progress = h_maxf(0.0f, m->scan_progress - dt * 0.25f);
        m->signal = h_maxf(m->signal, ok ? 1.0f : signal_strength(c, m->target, 400.0f));
        if (m->scan_progress >= 1.0f) {
            if (s->type == OB_LANDMARK) {
                const Poi *p = world_poi(w, s->poi);
                push(m, MEVT_DISCOVERY, p ? p->landmark : -1, NULL, NULL);
            }
            finish_step(m, w, t);
        }
        break;
    }
    case OB_LOCATE: {
        m->signal = signal_strength(c, m->target, 700.0f);
        if (d < 140.0f) { m->marker = m->target; m->marker_on = 1; }
        if (d < h_maxf(m->target_r, 40.0f)) { push(m, MEVT_WARNING, 0, NULL, "SIGNAL LOCKED"); finish_step(m, w, t); }
        break;
    }
    case OB_FOLLOW: {
        if (m->vehicle < 0) { finish_step(m, w, t); break; }
        Vehicle *v = &t->v[m->vehicle];
        const Lane *l = &w->lanes[v->lane];
        m->marker = v->pos; m->marker_on = 1;
        float dv = v3_dist(c->pos, v->pos);
        m->pursuit = h_maxf(m->pursuit, 0.45f);
        /* the courier waits a little for a lagging tail, never for a close one */
        v->speed = v->base_speed * (dv > s->max_d * 0.8f ? 0.6f : 1.0f);
        if (m->step_time < 4.0f) v->speed = 0.0f;
        if (dv < s->min_d && m->step_time > 6.0f) {
            m->warn_time += dt;
            if (m->warn_time > 4.0f) { fail(m, "SUSPECT ALERTED"); return; }
            snprintf(m->objective, sizeof(m->objective), "TOO CLOSE - DROP BACK");
        } else if (dv > s->max_d) {
            m->warn_time += dt;
            if (m->warn_time > 8.0f) { fail(m, "CONTACT LOST"); return; }
            snprintf(m->objective, sizeof(m->objective), "LOSING CONTACT - CLOSE IN");
        } else {
            m->warn_time = h_maxf(0.0f, m->warn_time - dt * 2.0f);
            format_objective(m);
        }
        m->signal = h_clampf(1.0f - dv / (s->max_d * 1.5f), 0.0f, 1.0f);
        if (v->s >= l->len - 1.0f) { v->active = 0; finish_step(m, w, t); }
        break;
    }
    case OB_PURSUE: {
        if (m->vehicle < 0) { finish_step(m, w, t); break; }
        Vehicle *v = &t->v[m->vehicle];
        m->marker = v->pos; m->marker_on = 1;
        float dv = v3_dist(c->pos, v->pos);
        m->pursuit = 1.0f;
        /* rubber band: the drone keeps the chase alive without being impossible */
        float base = v->base_speed;
        if (m->step_time < 3.0f) v->speed = 0.0f;
        else if (dv < 45.0f) v->speed = base * 1.25f;
        else if (dv > 160.0f) v->speed = base * 0.55f;
        else v->speed = base;
        m->signal = h_clampf(1.0f - dv / 200.0f, 0.0f, 1.0f);
        if (in->action_pressed && m->has_emp && m->emp_cooldown <= 0.0f) {
            m->emp_cooldown = 2.5f;
            if (dv < 38.0f) {
                v->disabled = 1;
                push(m, MEVT_EMP_HIT, 0, NULL, NULL);
                finish_step(m, w, t);
            } else push(m, MEVT_EMP_MISS, 0, NULL, NULL);
        }
        break;
    }
    case OB_COLLECT: case OB_RINGS: {
        int ring = s->type == OB_RINGS;
        char nm[24];
        int best = -1; float bd = 1e9f;
        for (int i = 0; i < s->count; i++) {
            if (ring && i != m->ring) continue;
            if (!ring && (m->collected & (1 << i))) continue;
            snprintf(nm, sizeof(nm), "%s%d", s->poi, i + 1);
            const Poi *p = world_poi(w, nm);
            if (!p) continue;
            float dd = v3_dist(c->pos, p->p);
            if (dd < p->r + (ring ? 2.0f : 4.0f)) {
                if (ring) { m->ring++; push(m, MEVT_COLLECT, (int8_t)i, NULL, NULL); }
                else {
                    m->collected |= 1 << i;
                    push(m, MEVT_COLLECT, (int8_t)i, NULL, NULL);
                    const char *msg = hush_missions[m->mission].collect_msgs[i];
                    if (msg) push(m, MEVT_TRANSMISSION, 2, "VALE", msg);
                }
                format_objective(m);
                continue;
            }
            if (dd < bd) { bd = dd; best = i; }
        }
        int done = ring ? m->ring >= s->count : (m->collected == (1 << s->count) - 1);
        if (done) { finish_step(m, w, t); break; }
        if (best >= 0) {
            snprintf(nm, sizeof(nm), "%s%d", s->poi, best + 1);
            const Poi *p = world_poi(w, nm);
            m->marker = p->p; m->marker_on = 1; m->target = p->p; m->target_r = p->r;
            m->signal = h_clampf(1.0f - bd / 400.0f, 0.0f, 1.0f);
        }
        break;
    }
    case OB_DOCK: {
        float dh = v3_dist_xz(c->pos, m->target);
        float above = c->pos.y - m->target.y;
        if (dh < m->target_r && above < 7.0f && above > -2.0f && c->speed < 4.5f) {
            m->dock_time += dt;
            if (m->dock_time > 1.2f) finish_step(m, w, t);
        } else m->dock_time = 0;
        break;
    }
    case OB_LISTEN: {
        if (d < 40.0f && !in->any_input && c->speed < 2.5f) {
            m->listen_time += dt;
            if (m->listen_time >= s->param) finish_step(m, w, t);
        } else m->listen_time = h_maxf(0.0f, m->listen_time - dt * 2.0f);
        m->signal = h_clampf(m->listen_time / h_maxf(s->param, 1.0f), 0.0f, 1.0f);
        break;
    }
    }
}
