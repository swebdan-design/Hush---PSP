/* HUSH — the in-flight session shared by Patrol, Drift and the title attract mode. */
#include "app.h"
#include "../render/render_moth.h"
#include "../render/render_crowd.h"
#include <pspgu.h>
#include <string.h>

static float g_complete_timer = -1.0f;
static float g_vent_warn;

static void handle_mission_events(App *a) {
    MissionRun *m = &a->mission;
    for (int i = 0; i < m->n_ev; i++) {
        MissionEvent *e = &m->ev[i];
        switch (e->type) {
        case MEVT_TRANSMISSION: app_tx(e->speaker, e->text); sfx_play(&a->sfx, SFX_TRANSMISSION); break;
        case MEVT_STEP_DONE: sfx_play(&a->sfx, SFX_SCAN_DONE); break;
        case MEVT_COMPLETE:
            sfx_play(&a->sfx, SFX_COMPLETE);
            hush_music_event(a->music, MEV_MISSION_COMPLETE, a->mission_id);
            g_complete_timer = 4.0f;
            break;
        case MEVT_FAILED:
            sfx_play(&a->sfx, SFX_FAIL);
            hush_music_event(a->music, MEV_ALERT, 0);
            app_set_state(ST_FAILED);
            break;
        case MEVT_DISCOVERY:
            if (e->arg >= 0) {
                int fresh = !(a->prog.landmarks & (1u << e->arg));
                progress_discover(&a->prog, e->arg);
                a->discovery_lm = e->arg; a->discovery_t = 5.0f;
                hush_music_event(a->music, MEV_DISCOVERY, e->arg);
                sfx_play(&a->sfx, SFX_DISCOVERY);
                (void)fresh;
            }
            break;
        case MEVT_COLLECT: sfx_play(&a->sfx, SFX_COLLECT); hush_music_event(a->music, MEV_SIGNAL_LOCK, 0); break;
        case MEVT_EMP_HIT: sfx_play(&a->sfx, SFX_EMP); a->emp_flash = 1.0f; break;
        case MEVT_EMP_MISS: sfx_play(&a->sfx, SFX_EMP); a->emp_flash = 0.4f; app_note("EMP OUT OF RANGE"); break;
        case MEVT_WARNING: app_note(e->text); sfx_play(&a->sfx, SFX_SCAN_DONE); break;
        default: break;
        }
    }
}

void game_update(float dt, int controls) {
    App *a = &g_app;
    FlightInput fi;
    MissionInput mi;
    memset(&fi, 0, sizeof(fi));
    memset(&mi, 0, sizeof(mi));
    const Input *in = &a->in;

    if (a->mode == MODE_ATTRACT) autopilot_wander(&a->pilot, &a->world, &a->craft, &fi, dt);
    else if (a->autotest && a->mode == MODE_PATROL) autopilot_mission(&a->pilot, &a->world, &a->traffic, &a->craft, &a->mission, &fi, &mi);
    else if (controls) {
        fi.fwd = in->ly;
        fi.strafe = in->lx;
        fi.yaw = ((in->held & PSP_CTRL_RTRIGGER) ? 1.0f : 0.0f) - ((in->held & PSP_CTRL_LTRIGGER) ? 1.0f : 0.0f);
        uint32_t b_up, b_down, b_boost, b_brake, b_scan, b_act;
        if (a->prog.control_scheme == CTL_FACE) {
            /* right thumb: altitude on the face buttons, so the left thumb never leaves the nub */
            b_up = PSP_CTRL_TRIANGLE; b_down = PSP_CTRL_CROSS; b_boost = PSP_CTRL_SQUARE;
            b_brake = PSP_CTRL_DOWN; b_scan = PSP_CTRL_CIRCLE; b_act = PSP_CTRL_CIRCLE;
            if (in->pressed & PSP_CTRL_UP) {    /* D-pad up cycles the HUD */
                a->prog.hud_mode = (uint8_t)((a->prog.hud_mode + 1) % HUD_COUNT);
                static const char *const hn[HUD_COUNT] = { "HUD: FULL", "HUD: MINIMAL", "HUD: OFF" };
                app_note(hn[a->prog.hud_mode]);
            }
        } else {
            b_up = PSP_CTRL_UP; b_down = PSP_CTRL_DOWN; b_boost = PSP_CTRL_CROSS;
            b_brake = PSP_CTRL_SQUARE; b_scan = PSP_CTRL_TRIANGLE; b_act = PSP_CTRL_CIRCLE;
        }
        float up = (in->held & b_up) ? 1.0f : 0.0f, dn = (in->held & b_down) ? 1.0f : 0.0f;
        fi.lift = a->prog.invert_lift ? dn - up : up - dn;
        fi.boost = (in->held & b_boost) != 0;
        fi.brake = (in->held & b_brake) != 0;
        mi.scan_held = (in->held & b_scan) != 0;
        mi.action_pressed = (in->pressed & b_act) != 0;
        mi.any_input = h_absf(fi.fwd) > 0.05f || h_absf(fi.strafe) > 0.05f || fi.yaw != 0 || fi.lift != 0 || fi.boost || fi.brake;
        if (in->held & PSP_CTRL_LEFT) a->cam.pitch_offset = h_clampf(a->cam.pitch_offset + dt * 0.8f, -0.6f, 0.6f);
        if (in->held & PSP_CTRL_RIGHT) a->cam.pitch_offset = h_clampf(a->cam.pitch_offset - dt * 0.8f, -0.6f, 0.6f);
        if (in->pressed & PSP_CTRL_SELECT) {
            a->cam.mode = (a->cam.mode + 1) % CAM_COUNT;
            static const char *const names[CAM_COUNT] = { "CHASE CAMERA", "COCKPIT", "CINEMATIC" };
            app_note(names[a->cam.mode]);
            sfx_play(&a->sfx, SFX_UI_MOVE);
        }
        if ((in->pressed & b_boost) && a->craft.boost_energy > 0.1f && fi.fwd > 0.1f) sfx_play(&a->sfx, SFX_BOOST);
        if (mi.action_pressed && a->mode == MODE_DRIFT && b_act != b_scan) app_note(a->prog.has_emp ? "EMP DISCHARGED - NOTHING TO DISABLE" : "NO CONTEXT ACTION HERE");
    }

    float dmg = craft_update(&a->craft, &a->world, &fi, dt);
    if (dmg > 1.0f) { sfx_play(&a->sfx, SFX_IMPACT); a->cam.shake = h_maxf(a->cam.shake, h_clampf(dmg / 20.0f, 0.2f, 1.0f)); }
    /* steam vents scald the hull while they fire */
    g_vent_warn = h_maxf(0.0f, g_vent_warn - dt);
    for (int i = 0; i < a->world.n_vents; i++) {
        const Vent *v = &a->world.vents[i];
        if (!vent_active(i, a->time)) continue;
        float dh = v3_dist_xz(a->craft.pos, v->p);
        if (dh < v->r && a->craft.pos.y < v->p.y + v->h) {
            a->craft.hull = h_maxf(0.0f, a->craft.hull - 20.0f * dt);
            a->craft.impact = h_maxf(a->craft.impact, 0.25f);
            if (g_vent_warn <= 0) { app_note("STEAM VENT - HULL DAMAGE"); g_vent_warn = 2.0f; }
        }
    }
    if (a->mode != MODE_PATROL && a->craft.hull < a->craft.hull_max) a->craft.hull = h_minf(a->craft.hull_max, a->craft.hull + 4.0f * dt); /* drift: self-repair */
    traffic_update(&a->traffic, &a->world, dt);
    crowd_update(&a->crowd, &a->world, a->craft.pos, a->wx.rain, dt);
    weather_update(&a->wx, dt);

    if (a->mode == MODE_PATROL) {
        mission_update(&a->mission, &a->world, &a->traffic, &a->craft, &mi, dt);
        handle_mission_events(a);
        if (g_complete_timer > 0) {
            g_complete_timer -= dt;
            if (g_complete_timer <= 0) { g_complete_timer = -1; app_set_state(ST_DEBRIEF); }
        }
    }

    /* free scanner: identify sonic landmarks anywhere (Drift and Patrol) */
    a->scanning = mi.scan_held;
    int mission_scan = a->mode == MODE_PATROL && a->mission.state == MS_ACTIVE &&
                       (hush_missions[a->mission.mission].steps[a->mission.step].type == OB_SCAN || hush_missions[a->mission.mission].steps[a->mission.step].type == OB_LANDMARK);
    if (mi.scan_held && !mission_scan && a->mode != MODE_ATTRACT) {
        float d;
        float range = 110.0f + 45.0f * (float)a->prog.scanner_level;
        int lm = scanner_landmark_in_view(&a->world, &a->craft, range, &d);
        if (lm >= 0 && lm == a->scan_lm) {
            a->scan_t += dt;
            if (a->scan_t >= 2.0f && a->discovery_t <= 0) {
                int fresh = !(a->prog.landmarks & (1u << lm));
                progress_discover(&a->prog, lm);
                a->discovery_lm = lm; a->discovery_t = 5.0f;
                hush_music_event(a->music, MEV_DISCOVERY, lm);
                sfx_play(&a->sfx, SFX_DISCOVERY);
                if (fresh) app_save();
                a->scan_t = -100.0f;
            }
        } else { a->scan_lm = lm; a->scan_t = 0; }
    } else { a->scan_t = 0; a->scan_lm = -1; }
    if (a->discovery_t > 0) a->discovery_t -= dt;
    if (a->emp_flash > 0) a->emp_flash -= dt * 2.0f;

    float pursuit = a->mode == MODE_PATROL ? a->mission.pursuit : 0.0f;
    float signal = a->mode == MODE_PATROL ? a->mission.signal : a->bridge.landmark_prox;
    env_bridge_update(&a->bridge, &a->env, &a->world, &a->craft, &a->traffic, &a->wx, pursuit, mi.scan_held ? 1.0f : 0.0f, signal);
    hush_music_set_env(a->music, &a->env);
    camera_update(&a->cam, &a->craft, &a->world, dt);

    /* engine and wind sounds */
    float th = (a->craft.thrust[0] + a->craft.thrust[1] + a->craft.thrust[2] + a->craft.thrust[3]) * 0.25f;
    a->sfx.engine = a->mode == MODE_ATTRACT ? 0.25f : 0.35f + 0.65f * th;
    a->sfx.engine_pitch = h_clampf(a->craft.speed / 80.0f, 0.0f, 1.0f) * 7.0f;
    a->sfx.wind = h_clampf(a->craft.speed / 85.0f, 0.0f, 1.0f) + a->wx.rain * 0.15f;
    a->sfx.scan = mi.scan_held ? 1.0f : 0.0f;
    a->sfx.key = hush_music_viz(a->music)->key_center;

    /* transmissions */
    if (a->tx_n > 0) {
        a->tx[0].t += dt;
        if (a->tx[0].t > a->tx[0].dur) {
            for (int i = 1; i < a->tx_n; i++) a->tx[i - 1] = a->tx[i];
            a->tx_n--;
        }
    }
}

static void draw_marker_3d(App *a) {
    if (a->mode != MODE_PATROL || !a->mission.marker_on || a->mission.state != MS_ACTIVE) return;
    V3 p = a->mission.marker;
    float pulse = 0.6f + 0.4f * sinf(a->time * 4.0f);
    rw_light(p, 3.5f, RGBA(90, 230, 255, 255), pulse);
    rw_light(v3_add(p, v3(0, 12, 0)), 2.0f, RGBA(90, 230, 255, 255), pulse * 0.6f);
    rw_light(v3_add(p, v3(0, 24, 0)), 1.5f, RGBA(90, 230, 255, 255), pulse * 0.4f);
}

static void draw_scan_beam(App *a) {
    if (!a->scanning || a->cam.mode == CAM_COCKPIT) return;
    V3 nose = moth_nose(&a->craft);
    V3 f = craft_forward(&a->craft);
    gfx_no_tex();
    sceGuDisable(GU_FOG);
    gfx_blend_add();
    const int N = 9;
    VtxCP *v = (VtxCP *)sceGuGetMemory(N * 2 * (int)sizeof(VtxCP));
    float sweep = sinf(a->time * 3.0f) * 0.35f;
    for (int i = 0; i < N; i++) {
        float ang = ((float)i / (float)(N - 1) - 0.5f) * 0.6f + sweep * 0.3f;
        V3 dir = v3(sinf(a->craft.yaw + ang), -0.12f, cosf(a->craft.yaw + ang));
        V3 end = v3_madd(nose, dir, 60.0f);
        (void)f;
        v[2 * i].color = RGBA(90, 220, 255, 140); v[2 * i].x = nose.x; v[2 * i].y = nose.y; v[2 * i].z = nose.z;
        v[2 * i + 1].color = RGBA(90, 220, 255, 0); v[2 * i + 1].x = end.x; v[2 * i + 1].y = end.y; v[2 * i + 1].z = end.z;
    }
    sceGuDrawArray(GU_LINES, VT_CP | GU_TRANSFORM_3D, N * 2, 0, v);
    gfx_blend_alpha();
}

void game_render(int with_hud) {
    App *a = &g_app;
    rw_atmos(&a->world, &a->wx, &a->atmos);
    Camera *cam = &a->cam;
    rw_draw_sky(&a->world, &a->atmos, cam->eye, cam->target, cam->up, cam->fov);
    gfx_set_camera(cam->eye, cam->target, cam->up, cam->fov, 1.0f, a->atmos.fog_far + 80.0f);
    rw_draw_world(&a->world, &a->atmos, a->time);
    rw_draw_traffic(&a->world, &a->traffic, &a->atmos, a->time);
    rw_draw_crowd(&a->crowd, &a->atmos, a->time);
    moth_draw(&a->craft, a->time, a->prog.paint, cam->mode != CAM_COCKPIT, a->atmos.fog_near, a->atmos.fog_far, a->atmos.fog_color);
    rw_draw_world_lights(&a->world, &a->atmos, a->time, a->wx.rain);
    draw_marker_3d(a);
    rw_flush_lights(&a->world, &a->atmos);
    rw_draw_effects(&a->world, &a->wx, &a->atmos, a->time, 1.0f / 30.0f, a->prog.rain_density);
    draw_scan_beam(a);
    gfx_mode_2d();
    if (a->emp_flash > 0) gfx_rect(0, 0, SCR_W, SCR_H, RGBA(150, 220, 255, (int)(90 * a->emp_flash)));
    if (a->craft.hull < a->craft.hull_max * 0.25f && a->mode == MODE_PATROL)
        gfx_rect(0, 0, SCR_W, SCR_H, RGBA(120, 10, 10, (int)(30 + 20 * sinf(a->time * 6.0f))));
    if (with_hud) hud_draw();
}
