/* HUSH — flight HUD and cockpit instrumentation. */
#include "app.h"
#include <stdio.h>
#include <string.h>

#define C_CYAN RGBA(120, 220, 240, 255)
#define C_DIM RGBA(120, 150, 165, 200)
#define C_AMBER RGBA(255, 180, 80, 255)
#define C_RED RGBA(255, 80, 70, 255)
#define C_WHITE RGBA(225, 232, 240, 255)
#define C_PANEL RGBA(6, 10, 14, 170)

static int hud_mode(void) {
    if (g_app.hud_override >= 0) return g_app.hud_override;
    return g_app.prog.hud_mode;
}

static void compass(float cx, float y, float w, float yaw, int show_marker, float marker_bearing) {
    gfx_rect(cx - w * 0.5f, y, w, 14, RGBA(6, 10, 14, 120));
    float deg = yaw * 180.0f / H_PI;
    static const char *const card[8] = { "N", "NE", "E", "SE", "S", "SW", "W", "NW" };
    for (int i = -60; i <= 60; i += 5) {
        float hd = deg + (float)i;
        float snapped = floorf(hd / 15.0f) * 15.0f;
        (void)snapped;
    }
    for (int k = -12; k <= 12; k++) {
        float tick = floorf(deg / 15.0f) * 15.0f + (float)k * 15.0f;
        float off = (tick - deg) / 120.0f * w;
        if (h_absf(off) > w * 0.5f - 4) continue;
        float x = cx + off;
        int t = ((int)tick % 360 + 360) % 360;
        if (t % 45 == 0) gfx_text(FONT_SMALL, x, y + 1, card[t / 45], t == 0 ? C_AMBER : C_WHITE, ALIGN_CENTER);
        else gfx_line(x, y + 9, x, y + 13, C_DIM);
    }
    gfx_tri(cx - 4, y + 15, cx + 4, y + 15, cx, y + 11, C_CYAN);
    if (show_marker) {
        float diff = h_wrap_angle(marker_bearing - yaw) * 180.0f / H_PI;
        float off = h_clampf(diff / 120.0f * w, -w * 0.5f + 3, w * 0.5f - 3);
        gfx_rect(cx + off - 2, y - 3, 5, 4, C_CYAN);
    }
}

static void radar(float cx, float cy, float r) {
    App *a = &g_app;
    float pts[48];
    for (int i = 0; i < 24; i++) { float an = (float)i * H_TAU / 24.0f; pts[2 * i] = cx + cosf(an) * r; pts[2 * i + 1] = cy + sinf(an) * r; }
    gfx_lines(pts, 24, RGBA(120, 220, 240, 110), 1);
    gfx_line(cx - r, cy, cx + r, cy, RGBA(120, 220, 240, 40));
    gfx_line(cx, cy - r, cx, cy + r, RGBA(120, 220, 240, 40));
    const float range = 320.0f;
    float cy_ = cosf(a->craft.yaw), sy = sinf(a->craft.yaw);
    #define TO_R(px, pz, ox, oy) do { float dx = (px) - a->craft.pos.x, dz = (pz) - a->craft.pos.z; \
        float rx = dz * sy - dx * cy_, rz = dx * sy + dz * cy_; ox = cx + rx / range * r; oy = cy - rz / range * r; } while (0)
    /* traffic */
    for (int i = 0; i < a->traffic.n; i++) {
        const Vehicle *v = &a->traffic.v[i];
        if (!v->active) continue;
        float d = v3_dist_xz(v->pos, a->craft.pos);
        if (d > range) continue;
        float x, y; TO_R(v->pos.x, v->pos.z, x, y);
        uint32_t c = v->mission ? C_AMBER : RGBA(200, 210, 220, 140);
        gfx_rect(x - 1, y - 1, v->mission ? 3 : 2, v->mission ? 3 : 2, c);
    }
    /* landmarks */
    for (int i = 0; i < a->world.n_pois; i++) {
        const Poi *p = &a->world.pois[i];
        if (p->kind != POI_LANDMARK && p->kind != POI_PAD) continue;
        float d = v3_dist_xz(p->p, a->craft.pos);
        float x, y; TO_R(p->p.x, p->p.z, x, y);
        if (d > range) { float k = range / d; x = cx + (x - cx) * k; y = cy + (y - cy) * k; }
        uint32_t c = p->kind == POI_PAD ? RGBA(90, 200, 255, 200) : RGBA(220, 160, 255, 220);
        gfx_tri(x, y - 3, x + 3, y, x - 3, y, c); gfx_tri(x - 3, y, x + 3, y, x, y + 3, c);
    }
    if (a->mode == MODE_PATROL && a->mission.marker_on) {
        float x, y; TO_R(a->mission.marker.x, a->mission.marker.z, x, y);
        float d = v3_dist_xz(a->mission.marker, a->craft.pos);
        if (d > range) { float k = range / d; x = cx + (x - cx) * k; y = cy + (y - cy) * k; }
        float p = 0.5f + 0.5f * sinf(a->time * 6.0f);
        gfx_rect(x - 2, y - 2, 5, 5, col_alpha(C_CYAN, 0.5f + 0.5f * p));
    }
    gfx_tri(cx - 3, cy + 3, cx + 3, cy + 3, cx, cy - 4, C_WHITE);
    #undef TO_R
}

static void bar(float x, float y, float w, float h, float v, uint32_t c) {
    gfx_rect(x, y, w, h, RGBA(255, 255, 255, 25));
    gfx_rect(x, y, w * h_clampf(v, 0, 1), h, c);
}

static void signal_meter(float x, float y, float v) {
    gfx_text(FONT_SMALL, x, y, "SIGNAL", C_DIM, ALIGN_LEFT);
    for (int i = 0; i < 10; i++) {
        float on = v * 10.0f - (float)i;
        uint32_t c = on > 0 ? (i < 6 ? C_CYAN : (i < 8 ? C_AMBER : C_RED)) : RGBA(255, 255, 255, 30);
        float h = 3.0f + (float)i * 1.0f;
        gfx_rect(x + 46 + (float)i * 5, y + 12 - h, 3, h, c);
    }
}

static void objective_marker(void) {
    App *a = &g_app;
    if (a->mode != MODE_PATROL || !a->mission.marker_on || a->mission.state != MS_ACTIVE) return;
    float sx, sy, dep;
    V3 m = a->mission.marker;
    float d = v3_dist(m, a->craft.pos);
    char buf[16];
    snprintf(buf, sizeof(buf), "%dm", (int)d);
    int on = gfx_project(m, &sx, &sy, &dep);
    if (on && sx > 10 && sx < SCR_W - 10 && sy > 20 && sy < SCR_H - 30) {
        float s = 5.0f;
        float pts[8] = { sx, sy - s, sx + s, sy, sx, sy + s, sx - s, sy };
        gfx_lines(pts, 4, C_CYAN, 1);
        gfx_text(FONT_SMALL, sx, sy + 7, buf, C_CYAN, ALIGN_CENTER);
    } else {
        /* off-screen: arrow on the screen edge in the target's direction */
        V3 rel = v3_sub(m, gfx_cam_pos());
        float x = v3_dot(rel, gfx_cam_right()), y = v3_dot(rel, gfx_cam_up());
        float l = sqrtf(x * x + y * y);
        if (l < 1e-3f) return;
        x /= l; y /= l;
        float cx = SCR_W * 0.5f + x * 200.0f, cy = SCR_H * 0.5f - y * 105.0f;
        float ax = cx + x * 8, ay = cy - y * 8;
        gfx_tri(ax, ay, cx - y * 5, cy - x * 5, cx + y * 5, cy + x * 5, C_CYAN);
        gfx_text(FONT_SMALL, cx - x * 14, cy + y * 14 - 5, buf, C_CYAN, ALIGN_CENTER);
    }
}

void hud_draw_transmissions(void) {
    App *a = &g_app;
    if (a->tx_n <= 0) return;
    Transmission *t = &a->tx[0];
    float alpha = h_clampf(t->t * 4.0f, 0, 1) * h_clampf((t->dur - t->t) * 2.0f, 0, 1);
    float y = SCR_H - 66;
    gfx_rect(70, y, 340, 40, col_alpha(RGBA(4, 8, 12, 190), alpha));
    gfx_rect(70, y, 2, 40, col_alpha(C_AMBER, alpha));
    const char *spk = t->speaker ? t->speaker : "TRANSMISSION";
    uint32_t sc = !strcmp(spk, "VALE") || !strcmp(spk, "UNKNOWN") ? RGBA(210, 150, 255, 255) : C_AMBER;
    gfx_text(FONT_SMALL, 78, y + 3, spk, col_alpha(sc, alpha), ALIGN_LEFT);
    int chars = (int)(t->t * 45.0f);
    gfx_text_wrap(FONT_SMALL, 78, y + 14, 326, t->text, col_alpha(C_WHITE, alpha), 2, chars);
}

static void music_line(float x, float y, int align) {
    const MusicViz *z = hush_music_viz(g_app.music);
    char buf[64];
    snprintf(buf, sizeof(buf), "%s  %s  %s", hush_section_names[z->section], z->chord_name, hush_note_names[z->key_center]);
    gfx_text(FONT_SMALL, x, y, buf, RGBA(170, 150, 220, 200), align);
}

static void cockpit_panel(void) {
    App *a = &g_app;
    Craft *c = &a->craft;
    /* canopy struts */
    gfx_tri(0, 0, 60, 0, 0, 120, RGBA(8, 10, 12, 255));
    gfx_tri(SCR_W, 0, SCR_W - 60, 0, SCR_W, 120, RGBA(8, 10, 12, 255));
    gfx_rect(0, 0, SCR_W, 8, RGBA(8, 10, 12, 255));
    gfx_line(60, 0, 0, 120, RGBA(60, 70, 80, 255));
    gfx_line(SCR_W - 60, 0, SCR_W, 120, RGBA(60, 70, 80, 255));
    /* instrument panel */
    float py = SCR_H - 74;
    gfx_rect_grad(0, py, SCR_W, 74, RGBA(18, 20, 24, 250), RGBA(8, 9, 11, 255));
    gfx_line(0, py, SCR_W, py, RGBA(80, 90, 100, 255));
    /* artificial horizon */
    float hx = SCR_W * 0.5f, hy = py + 37, hr = 30;
    gfx_rect(hx - hr, hy - hr, hr * 2, hr * 2, RGBA(10, 30, 40, 255));
    {   /* ground half of the attitude ball (pitch only; roll shown by the line) */
        float gy = h_clampf(hy + c->pitch * 60.0f, hy - hr, hy + hr);
        gfx_rect(hx - hr, gy, hr * 2, hy + hr - gy, RGBA(40, 30, 18, 255));
    }
    float roll = c->bank, pitch = c->pitch * 60.0f;
    float dx = cosf(roll) * hr, dy = sinf(roll) * hr;
    gfx_line(hx - dx, hy + pitch + dy, hx + dx, hy + pitch - dy, C_AMBER);   /* banking right raises the horizon's right end */
    gfx_line(hx - 10, hy, hx - 3, hy, C_WHITE); gfx_line(hx + 3, hy, hx + 10, hy, C_WHITE);
    gfx_rect_outline(hx - hr, hy - hr, hr * 2, hr * 2, RGBA(80, 90, 100, 255));
    /* pitch ladder */
    for (int k = -2; k <= 2; k++) {
        if (!k) continue;
        float ly = hy + pitch + (float)k * 12.0f;
        if (ly < hy - hr + 2 || ly > hy + hr - 2) continue;
        gfx_line(hx - 8, ly, hx + 8, ly, RGBA(120, 220, 240, 110));
    }
    /* speed / altitude readouts */
    char buf[32];
    snprintf(buf, sizeof(buf), "%03d", (int)(c->speed * 3.6f));
    gfx_text(FONT_SMALL, 44, py + 8, "SPD km/h", C_DIM, ALIGN_LEFT);
    gfx_text(FONT_LARGE, 44, py + 19, buf, c->boosting ? C_AMBER : C_CYAN, ALIGN_LEFT);
    float floor_y = a->world.water_y > a->world.ground_y ? a->world.water_y : a->world.ground_y;
    snprintf(buf, sizeof(buf), "%04d", (int)(c->pos.y - floor_y));
    gfx_text(FONT_SMALL, 124, py + 8, "ALT m", C_DIM, ALIGN_LEFT);
    gfx_text(FONT_LARGE, 124, py + 19, buf, C_CYAN, ALIGN_LEFT);
    gfx_text(FONT_SMALL, 44, py + 45, "HULL", C_DIM, ALIGN_LEFT);
    bar(92, py + 49, 104, 4, c->hull / c->hull_max, c->hull < 30 ? C_RED : RGBA(120, 220, 160, 255));
    gfx_text(FONT_SMALL, 44, py + 57, "BOOST", C_DIM, ALIGN_LEFT);
    bar(92, py + 61, 104, 4, c->boost_energy, C_AMBER);
    /* live oscilloscope of the composition */
    const MusicViz *z = hush_music_viz(a->music);
    float ox = 290, oy = py + 8, ow = 120, oh = 40;
    gfx_rect(ox, oy, ow, oh, RGBA(4, 14, 10, 255));
    float pts[2 * 64];
    for (int i = 0; i < 64; i++) {
        float s = z->scopeL[(i * 4) % VIZ_SCOPE] + z->scopeR[(i * 4) % VIZ_SCOPE];
        pts[2 * i] = ox + (float)i * ow / 63.0f;
        pts[2 * i + 1] = oy + oh * 0.5f - h_clampf(s * 1.5f, -1, 1) * oh * 0.45f;
    }
    gfx_lines(pts, 64, RGBA(120, 255, 170, 230), 0);
    gfx_rect_outline(ox, oy, ow, oh, RGBA(80, 90, 100, 255));
    radar(445, py + 37, 28);
    signal_meter(290, py + 54, a->mode == MODE_PATROL ? a->mission.signal : a->bridge.landmark_prox);
}

void hud_draw(void) {
    App *a = &g_app;
    int mode = hud_mode();
    Craft *c = &a->craft;
    if (a->cam.mode == CAM_CINEMATIC) {
        gfx_rect(0, 0, SCR_W, 26, RGBA(0, 0, 0, 255));
        gfx_rect(0, SCR_H - 26, SCR_W, 26, RGBA(0, 0, 0, 255));
    }
    if (a->cam.mode == CAM_COCKPIT && mode != HUD_OFF) cockpit_panel();
    if (mode == HUD_OFF) { hud_draw_transmissions(); return; }
    objective_marker();
    float floor_y = a->world.water_y > a->world.ground_y ? a->world.water_y : a->world.ground_y;
    char buf[96];
    float bearing = 0;
    int has_marker = a->mode == MODE_PATROL && a->mission.marker_on && a->mission.state == MS_ACTIVE;
    if (has_marker) { V3 d = v3_sub(a->mission.marker, c->pos); bearing = atan2f(d.x, d.z); }
    if (a->cam.mode != CAM_COCKPIT) {
        if (mode == HUD_FULL) {
            compass(SCR_W * 0.5f, 6, 200, -c->yaw, has_marker, -bearing);   /* heading grows clockwise */
            /* left: speed, right: altitude */
            snprintf(buf, sizeof(buf), "%03d", (int)(c->speed * 3.6f));
            gfx_rect(8, 112, 54, 30, C_PANEL);
            gfx_text(FONT_SMALL, 12, 114, "SPD km/h", C_DIM, ALIGN_LEFT);
            gfx_text(FONT_LARGE, 12, 123, buf, c->boosting ? C_AMBER : C_CYAN, ALIGN_LEFT);
            snprintf(buf, sizeof(buf), "%04d", (int)(c->pos.y - floor_y));
            gfx_rect(SCR_W - 62, 112, 54, 30, C_PANEL);
            gfx_text(FONT_SMALL, SCR_W - 58, 114, "ALT m", C_DIM, ALIGN_LEFT);
            gfx_text(FONT_LARGE, SCR_W - 58, 123, buf, C_CYAN, ALIGN_LEFT);
            /* bottom left radar, bottom right systems */
            gfx_rect(4, SCR_H - 78, 74, 74, C_PANEL);
            radar(41, SCR_H - 41, 33);
            gfx_rect(SCR_W - 132, SCR_H - 52, 128, 48, C_PANEL);
            signal_meter(SCR_W - 128, SCR_H - 50, a->mode == MODE_PATROL ? a->mission.signal : a->bridge.landmark_prox);
            gfx_text(FONT_SMALL, SCR_W - 128, SCR_H - 34, "HULL", C_DIM, ALIGN_LEFT);
            bar(SCR_W - 90, SCR_H - 31, 80, 4, c->hull / c->hull_max, c->hull < 30 ? C_RED : RGBA(120, 220, 160, 255));
            gfx_text(FONT_SMALL, SCR_W - 128, SCR_H - 22, "BOOST", C_DIM, ALIGN_LEFT);
            bar(SCR_W - 90, SCR_H - 19, 80, 4, c->boost_energy, C_AMBER);
        } else {
            snprintf(buf, sizeof(buf), "%03d km/h   %04d m", (int)(c->speed * 3.6f), (int)(c->pos.y - floor_y));
            gfx_text(FONT_SMALL, SCR_W - 8, SCR_H - 14, buf, C_DIM, ALIGN_RIGHT);
        }
        /* reticle */
        gfx_line(SCR_W * 0.5f - 6, SCR_H * 0.5f, SCR_W * 0.5f - 2, SCR_H * 0.5f, RGBA(255, 255, 255, 90));
        gfx_line(SCR_W * 0.5f + 2, SCR_H * 0.5f, SCR_W * 0.5f + 6, SCR_H * 0.5f, RGBA(255, 255, 255, 90));
    }
    /* objective / district */
    float ty = a->cam.mode == CAM_CINEMATIC ? 30 : 24;
    if (a->mode == MODE_PATROL && a->mission.state == MS_ACTIVE) {
        gfx_text(FONT_SMALL, 10, ty, hush_missions[a->mission.mission].title, C_AMBER, ALIGN_LEFT);
        char ob[96];
        gfx_text(FONT_SMALL, 10, ty + 11, ctl_subst(a->mission.objective, ob, sizeof(ob)), C_WHITE, ALIGN_LEFT);
        if (hush_missions[a->mission.mission].steps[a->mission.step].time_limit > 0) {
            snprintf(buf, sizeof(buf), "TIME %02d:%02d", (int)a->mission.timer / 60, (int)a->mission.timer % 60);
            gfx_text(FONT_SMALL, 10, ty + 22, buf, a->mission.timer < 20 ? C_RED : C_AMBER, ALIGN_LEFT);
        }
        if (a->mission.scan_progress > 0.01f) {
            gfx_text(FONT_SMALL, SCR_W * 0.5f, SCR_H * 0.5f + 16, "SCANNING", C_CYAN, ALIGN_CENTER);
            bar(SCR_W * 0.5f - 40, SCR_H * 0.5f + 28, 80, 3, a->mission.scan_progress, C_CYAN);
        }
    } else if (mode == HUD_FULL) {
        gfx_text(FONT_SMALL, 10, ty, a->mode == MODE_DRIFT ? "DRIFT" : "", C_AMBER, ALIGN_LEFT);
        gfx_text(FONT_SMALL, 10, ty + 11, a->world.name, C_WHITE, ALIGN_LEFT);
        snprintf(buf, sizeof(buf), "%s / %s", weather_names[a->wx.preset], tod_names[a->wx.tod_mode]);
        gfx_text(FONT_SMALL, 10, ty + 22, buf, C_DIM, ALIGN_LEFT);
    }
    if (a->cam.mode != CAM_COCKPIT) music_line(SCR_W - 8, ty, ALIGN_RIGHT);
    /* free scanner feedback */
    if (a->scanning && a->scan_lm >= 0 && a->scan_t > 0) {
        snprintf(buf, sizeof(buf), "IDENTIFYING: %s", hush_landmark_names[a->scan_lm]);
        gfx_text(FONT_SMALL, SCR_W * 0.5f, SCR_H * 0.5f + 16, buf, RGBA(220, 170, 255, 255), ALIGN_CENTER);
        bar(SCR_W * 0.5f - 40, SCR_H * 0.5f + 28, 80, 3, a->scan_t / 2.0f, RGBA(220, 170, 255, 255));
    } else if (a->scanning && a->mode == MODE_DRIFT) {
        gfx_text(FONT_SMALL, SCR_W * 0.5f, SCR_H * 0.5f + 16, a->bridge.nearest_landmark >= 0 ? "SCANNING - FACE THE SOURCE" : "SCANNING - NO TRANSMISSION", C_DIM, ALIGN_CENTER);
    }
    if (a->discovery_t > 0 && a->discovery_lm >= 0) {
        float al = h_clampf(a->discovery_t, 0, 1);
        gfx_rect(0, 64, SCR_W, 30, col_alpha(RGBA(10, 6, 20, 200), al));
        gfx_text(FONT_SMALL, SCR_W * 0.5f, 67, "SONIC LANDMARK", col_alpha(RGBA(220, 170, 255, 255), al), ALIGN_CENTER);
        gfx_text(FONT_SMALL, SCR_W * 0.5f, 79, hush_landmark_names[a->discovery_lm], col_alpha(C_WHITE, al), ALIGN_CENTER);
    }
    if (a->prog.show_fps) {
        const RenderStats *rs = rw_stats();
        snprintf(buf, sizeof(buf), "%2.0f fps cpu %4.1fms gpu %4.1fms tri %d", a->fps, gfx_last_cpu_ms, gfx_last_gpu_wait_ms, rs->tris_drawn);
        gfx_text(FONT_SMALL, 8, SCR_H - 92, buf, RGBA(255, 255, 255, 200), ALIGN_LEFT);
    }
    if (a->state_t < 9.0f && a->mode != MODE_ATTRACT) ctl_hint_panel(SCR_W * 0.5f, SCR_H - 98, h_clampf(9.0f - a->state_t, 0.0f, 1.0f));
    hud_draw_transmissions();
}
