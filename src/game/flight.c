/* HUSH — flight model and cameras.
 *
 * The Moth is a vertical-thrust craft: it hovers by default (no stall, no
 * gravity drift when the controls are released) and moves through
 * acceleration with linear drag, so motion has inertia and weight but slow
 * flight stays precise. Physics runs in fixed 60 Hz sub-steps for
 * frame-rate independence and reproducibility. */
#include "flight.h"
#include <string.h>

void craft_init(Craft *c, V3 pos, float yaw) {
    float ms = c->max_speed, bs = c->boost_speed, am = c->accel_mul, hm = c->hull_max;
    memset(c, 0, sizeof(*c));
    c->pos = pos;
    c->yaw = yaw;
    c->max_speed = ms > 0 ? ms : 46.0f;
    c->boost_speed = bs > 0 ? bs : 82.0f;
    c->accel_mul = am > 0 ? am : 1.0f;
    c->hull_max = hm > 0 ? hm : 100.0f;
    c->hull = c->hull_max;
    c->boost_energy = 1.0f;
}

void craft_apply_upgrades(Craft *c, int boost_level, int hull_level) {
    c->max_speed = 46.0f + 4.0f * (float)boost_level;
    c->boost_speed = 82.0f + 12.0f * (float)boost_level;
    c->accel_mul = 1.0f + 0.12f * (float)boost_level;
    c->hull_max = 100.0f + 40.0f * (float)hull_level;
    if (c->hull > c->hull_max) c->hull = c->hull_max;
}

V3 craft_forward(const Craft *c) { return v3(sinf(c->yaw), 0, cosf(c->yaw)); }
/* World is right-handed, Y up: looking along +Z, screen-right is -X. Yaw increases
 * counter-clockwise seen from above, so turning right DEcreases yaw. */
V3 craft_right(const Craft *c) { return v3(-cosf(c->yaw), 0, sinf(c->yaw)); }

static float step(Craft *c, const World *w, const FlightInput *in) {
    const float dt = FLIGHT_DT;
    float damage = 0;
    /* yaw: rate approaches the requested rate, slightly slower at high speed */
    float max_rate = 1.75f - 0.5f * h_clampf(c->speed / 80.0f, 0.0f, 1.0f);
    c->yaw_rate += (in->yaw * max_rate - c->yaw_rate) * (1.0f - expf(-7.0f * dt));
    c->yaw = h_wrap_angle(c->yaw - c->yaw_rate * dt);   /* yaw_rate > 0 = turning right */

    V3 f = craft_forward(c), r = craft_right(c);
    int boost = in->boost && c->boost_energy > 0.02f && in->fwd > 0.1f;
    c->boosting = boost;
    float a_fwd = 30.0f * c->accel_mul * (boost ? 2.1f : 1.0f);
    float a_str = 21.0f * c->accel_mul;
    float a_ver = 18.0f * c->accel_mul;
    V3 acc = v3_add(v3_scale(f, in->fwd * a_fwd), v3_scale(r, in->strafe * a_str));
    acc.y = in->lift * a_ver;
    /* hover stabilisation: horizontal drag, stronger vertical damping, much stronger when braking */
    float kh = in->brake ? 3.6f : 0.75f;
    float kv = in->brake ? 4.5f : 2.2f;
    /* idle settle: with no input, bleed speed a little faster so hovering feels anchored */
    if (h_absf(in->fwd) < 0.05f && h_absf(in->strafe) < 0.05f) kh += 0.55f;
    c->vel.x += (acc.x - c->vel.x * kh) * dt;
    c->vel.z += (acc.z - c->vel.z * kh) * dt;
    c->vel.y += (acc.y - c->vel.y * kv) * dt;
    /* soft speed limit */
    float vmax = boost ? c->boost_speed : c->max_speed;
    float hs = sqrtf(c->vel.x * c->vel.x + c->vel.z * c->vel.z);
    if (hs > vmax) { float k = 1.0f - (1.0f - vmax / hs) * (1.0f - expf(-3.0f * dt)); c->vel.x *= k; c->vel.z *= k; }
    c->vel.y = h_clampf(c->vel.y, -26.0f, 26.0f);
    /* boost energy */
    if (boost) c->boost_energy = h_maxf(0.0f, c->boost_energy - dt * 0.22f);
    else c->boost_energy = h_minf(1.0f, c->boost_energy + dt * 0.1f);

    V3 before = c->pos;
    c->pos = v3_madd(c->pos, c->vel, dt);
    V3 n;
    if (world_collide_sphere(w, &c->pos, CRAFT_RADIUS, &n)) {
        float vn = v3_dot(c->vel, n);
        if (vn < 0) {
            /* remove the into-wall component with a little bounce, keep sliding */
            c->vel = v3_madd(c->vel, n, -vn * 1.3f);
            c->vel = v3_scale(c->vel, 0.92f);
            float hit = -vn;
            if (hit > 9.0f) {
                float dmg = (hit - 9.0f) * 0.9f;
                damage += dmg;
                c->impact = h_maxf(c->impact, h_clampf(hit / 40.0f, 0.0f, 1.0f));
            }
        }
    }
    /* soft district boundary */
    float lim = WORLD_HALF - 20.0f;
    if (c->pos.x > lim) { c->pos.x = lim; c->vel.x = h_minf(c->vel.x, 0) - 2.0f; }
    if (c->pos.x < -lim) { c->pos.x = -lim; c->vel.x = h_maxf(c->vel.x, 0) + 2.0f; }
    if (c->pos.z > lim) { c->pos.z = lim; c->vel.z = h_minf(c->vel.z, 0) - 2.0f; }
    if (c->pos.z < -lim) { c->pos.z = -lim; c->vel.z = h_maxf(c->vel.z, 0) + 2.0f; }
    (void)before;

    c->speed = v3_len(c->vel);
    /* visual attitude: bank into strafe and turns, pitch with fore/aft acceleration */
    float lv = v3_dot(c->vel, r), fv = v3_dot(c->vel, f);
    float bank_t = in->strafe * 0.38f + c->yaw_rate * 0.22f + lv * 0.004f;   /* > 0 = right wing down */
    float pitch_t = in->fwd * 0.16f + (boost ? 0.1f : 0.0f) - (in->brake ? 0.12f * h_clampf(fv / 30.0f, 0.0f, 1.0f) : 0.0f) - in->lift * 0.05f;
    c->bank += (h_clampf(bank_t, -0.7f, 0.7f) - c->bank) * (1.0f - expf(-5.0f * dt));
    c->pitch += (h_clampf(pitch_t, -0.4f, 0.4f) - c->pitch) * (1.0f - expf(-4.0f * dt));
    c->nacelle_tilt += ((in->fwd * 0.5f + (boost ? 0.25f : 0.0f)) - c->nacelle_tilt) * (1.0f - expf(-4.0f * dt));
    float base = 0.35f + 0.25f * h_absf(in->lift);
    c->thrust[0] = h_clampf(base + 0.5f * h_maxf(in->fwd, 0) + (boost ? 0.5f : 0.0f) + 0.2f * in->strafe, 0, 1);
    c->thrust[1] = h_clampf(base + 0.5f * h_maxf(in->fwd, 0) + (boost ? 0.5f : 0.0f) - 0.2f * in->strafe, 0, 1);
    c->thrust[2] = h_clampf(base + 0.3f * h_maxf(-in->fwd, 0) + 0.2f * in->strafe + 0.2f * h_maxf(in->lift, 0), 0, 1);
    c->thrust[3] = h_clampf(base + 0.3f * h_maxf(-in->fwd, 0) - 0.2f * in->strafe + 0.2f * h_maxf(in->lift, 0), 0, 1);
    c->impact *= expf(-3.0f * dt);
    if (c->speed < 3.0f) c->hover_time += dt; else c->hover_time = 0;
    return damage;
}

float craft_update(Craft *c, const World *w, const FlightInput *in, float dt) {
    float dmg = 0;
    c->accum += h_clampf(dt, 0.0f, 0.1f);
    int n = 0;
    while (c->accum >= FLIGHT_DT && n < 6) { dmg += step(c, w, in); c->accum -= FLIGHT_DT; n++; }
    if (n == 6) c->accum = 0;
    c->hull = h_maxf(0.0f, c->hull - dmg);
    return dmg;
}

/* ---------------- cameras ---------------- */

void camera_init(Camera *cam, const Craft *c) {
    memset(cam, 0, sizeof(*cam));
    cam->fov = 62.0f;
    V3 f = craft_forward(c);
    cam->eye = v3_add(v3_madd(c->pos, f, -16.0f), v3(0, 5.5f, 0));
    cam->target = c->pos;
    cam->up = v3(0, 1, 0);
}

void camera_update(Camera *cam, const Craft *c, const World *w, float dt) {
    V3 f = craft_forward(c);
    V3 r = craft_right(c);
    float spd = h_clampf(c->speed / 80.0f, 0.0f, 1.0f);
    cam->shake = h_maxf(cam->shake * expf(-4.0f * dt), c->impact);
    switch (cam->mode) {
    case CAM_COCKPIT: {
        cam->eye = v3_add(v3_madd(c->pos, f, 1.6f), v3(0, 0.9f, 0));
        float p = -c->pitch * 0.5f + cam->pitch_offset;
        V3 look = v3_add(v3_scale(f, cosf(p)), v3(0, sinf(p), 0));
        cam->target = v3_madd(cam->eye, look, 20.0f);
        float b = c->bank * 0.6f;
        cam->up = v3_add(v3_scale(v3(0, 1, 0), cosf(b)), v3_scale(r, sinf(b)));
        cam->fov = 70.0f + 8.0f * spd;
        break;
    }
    case CAM_CINEMATIC: {
        /* slow, lagging observer that drifts around the craft */
        cam->orbit += dt * (0.05f + 0.1f * (1.0f - spd));
        float dist = 30.0f + 14.0f * spd;
        V3 off = v3(sinf(c->yaw + 2.6f + 0.6f * sinf(cam->orbit)) * dist, 7.0f + 6.0f * sinf(cam->orbit * 0.7f), cosf(c->yaw + 2.6f + 0.6f * sinf(cam->orbit)) * dist);
        V3 want = v3_add(c->pos, off);
        cam->eye = v3_lerp(cam->eye, want, 1.0f - expf(-0.9f * dt));
        cam->target = v3_lerp(cam->target, v3_madd(c->pos, c->vel, 0.3f), 1.0f - expf(-2.0f * dt));
        cam->up = v3(0, 1, 0);
        cam->fov = 54.0f;
        break;
    }
    case CAM_CHASE: default: {
        float dist = 15.0f + 7.0f * spd;
        float height = 4.5f + 1.5f * spd + cam->pitch_offset * 12.0f;
        V3 want = v3_add(v3_madd(c->pos, f, -dist), v3(0, height, 0));
        want = v3_madd(want, c->vel, -0.06f);
        cam->eye = v3_lerp(cam->eye, want, 1.0f - expf(-5.5f * dt));
        V3 tgt = v3_add(v3_madd(c->pos, f, 8.0f + 10.0f * spd), v3(0, 1.0f - cam->pitch_offset * 6.0f, 0));
        cam->target = v3_lerp(cam->target, tgt, 1.0f - expf(-9.0f * dt));
        float b = c->bank * 0.25f;
        cam->up = v3_add(v3_scale(v3(0, 1, 0), cosf(b)), v3_scale(r, sinf(b)));
        cam->fov = 62.0f + 10.0f * spd + (c->boosting ? 4.0f : 0.0f);
        break;
    }
    }
    if (cam->mode != CAM_COCKPIT) {
        /* keep the camera out of buildings: pull in along the line of sight */
        V3 from = v3_add(c->pos, v3(0, 1.5f, 0));
        float t = world_raycast(w, from, cam->eye);
        if (t < 1.0f) cam->eye = v3_lerp(from, cam->eye, h_maxf(0.08f, t - 0.06f));
        float fy = (w->water_y > w->ground_y ? w->water_y : w->ground_y) + 1.5f;
        if (cam->eye.y < fy) cam->eye.y = fy;
    }
    if (cam->shake > 0.01f) {
        float s = cam->shake * 0.6f;
        cam->eye.x += sinf(c->pos.x * 13.0f + c->hover_time * 50.0f) * s;
        cam->eye.y += sinf(c->pos.z * 11.0f + c->speed * 3.0f) * s;
    }
}
