#define _GNU_SOURCE
#include <fenv.h>
/* HUSH host test suite: deterministic audio, mixing limits, world validity,
 * flight, persistence, and an autopilot that plays every Patrol mission. */
#include "../src/audio/music.h"
#include "../src/game/world.h"
#include "../src/game/flight.h"
#include "../src/game/traffic.h"
#include "../src/game/missions.h"
#include "../src/game/progress.h"
#include "../src/game/weather.h"
#include "../src/game/env_bridge.h"
#include "../src/game/autopilot.h"
#include "../src/game/crowd.h"
#include "../src/render/guardclip.h"
#include "../src/game/signals.h"
#include "../src/game/story.h"
#include "../src/audio/dsp.h"
#include "../src/core/rng.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static int g_fail = 0, g_pass = 0;
#define CHECK(cond, ...) do { if (cond) g_pass++; else { g_fail++; printf("  FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

/* ---------------------------------------------------------------- audio */

typedef struct { uint32_t hash, events; double peak, rms; int nan; uint32_t silent_blocks, blocks; } Render;

static Render render(int preset, uint32_t seed, float seconds, int rate, const MusicEnv *env, float *dump, int dump_n) {
    MusicParams p;
    hush_preset_get(preset, &p);
    p.seed = seed;
    HushMusic *m = hush_music_instance();
    hush_music_init(m, (float)rate, &p);
    if (env) hush_music_set_env(m, env);
    Render r = {0};
    static float buf[2048];
    int total = (int)(seconds * rate), done = 0;
    double sum = 0;
    while (done < total) {
        int n = total - done < 1024 ? total - done : 1024;
        hush_music_render(m, buf, n);
        double bp = 0;
        for (int i = 0; i < 2 * n; i++) {
            float x = buf[i];
            if (x != x) { r.nan++; continue; }
            if (fabs(x) > r.peak) r.peak = fabs(x);
            if (fabs(x) > bp) bp = fabs(x);
            sum += (double)x * x;
            if (dump && done * 2 + i < dump_n) dump[done * 2 + i] = x;
        }
        if (bp < 1e-3) r.silent_blocks++;
        r.blocks++;
        done += n;
    }
    r.rms = sqrt(sum / (2.0 * total));
    r.hash = hush_music_event_hash(m);
    r.events = hush_music_event_count(m);
    return r;
}

static void test_audio(void) {
    printf("[audio] determinism, seeds, levels, presets\n");
    MusicEnv env = {0};
    env.active = 1; env.district = DISTRICT_CIVIC; env.altitude = 0.4f; env.speed = 0.5f; env.landmark = -1;
    static float a[44100 * 2 * 4], b[44100 * 2 * 4];
    Render r1 = render(PRESET_PATROL, 4817, 60, 44100, &env, a, 44100 * 8);
    Render r2 = render(PRESET_PATROL, 4817, 60, 44100, &env, b, 44100 * 8);
    CHECK(r1.hash == r2.hash && r1.events == r2.events, "same seed must give same event stream (%08x vs %08x)", r1.hash, r2.hash);
    CHECK(memcmp(a, b, sizeof(float) * 44100 * 8) == 0, "same seed must give bit-identical audio");
    Render r3 = render(PRESET_PATROL, 4818, 60, 44100, &env, NULL, 0);
    CHECK(r3.hash != r1.hash, "different seeds must differ");
    /* events are decided per musical step, independent of the engine sample rate */
    Render r4 = render(PRESET_PATROL, 4817, 60, 22050, &env, NULL, 0);
    CHECK(r4.hash == r1.hash && r4.events == r1.events, "event stream must match at 22050 Hz (PSP rate): %08x/%u vs %08x/%u", r4.hash, r4.events, r1.hash, r1.events);
    CHECK(r1.nan == 0 && r3.nan == 0 && r4.nan == 0, "no NaN samples");
    CHECK(r1.peak < 0.99 && r1.peak > 0.05, "peak in range: %.3f", r1.peak);
    CHECK(r1.rms > 0.01 && r1.rms < 0.3, "rms in range: %.4f", r1.rms);
    CHECK(r1.events > 100, "composition produces events: %u", r1.events);

    /* different environment changes the music deterministically */
    MusicEnv env2 = env; env2.district = DISTRICT_WORKS; env2.altitude = 0.1f; env2.speed = 0.9f;
    Render r5 = render(PRESET_PATROL, 4817, 60, 44100, &env2, NULL, 0);
    CHECK(r5.hash != r1.hash, "district/altitude/speed must change the composition");
    Render r5b = render(PRESET_PATROL, 4817, 60, 44100, &env2, NULL, 0);
    CHECK(r5b.hash == r5.hash, "same seed + same inputs reproduce");

    /* presets differ */
    Render rp[PRESET_BUILTIN_COUNT];
    for (int i = 0; i < PRESET_BUILTIN_COUNT; i++) {
        rp[i] = render(i, 777, 45, 22050, &env, NULL, 0);
        CHECK(rp[i].nan == 0 && rp[i].peak < 0.99, "preset %s level ok (peak %.3f)", hush_preset_names[i], rp[i].peak);
    }
    CHECK(rp[PRESET_PURSUIT].events > rp[PRESET_DRIFT].events, "PURSUIT busier than DRIFT (%u vs %u)", rp[PRESET_PURSUIT].events, rp[PRESET_DRIFT].events);
    for (int i = 0; i < PRESET_BUILTIN_COUNT; i++)
        for (int j = i + 1; j < PRESET_BUILTIN_COUNT; j++) CHECK(rp[i].hash != rp[j].hash, "presets %d and %d differ", i, j);

    /* console mode (no world) still makes music */
    Render rc = render(PRESET_DRIFT, 2146, 60, 22050, NULL, NULL, 0);
    CHECK(rc.events > 20 && rc.rms > 0.005, "console mode produces music (%u events, rms %.4f)", rc.events, rc.rms);

    /* the Silent Exchange makes layers disappear */
    MusicEnv ex = env; ex.district = DISTRICT_UNDERTOW; ex.speed = 0.0f; ex.landmark = LM_EXCHANGE; ex.landmark_prox = 1.0f;
    MusicEnv nx = ex; nx.landmark = -1; nx.landmark_prox = 0;
    Render rex = render(PRESET_PATROL, 99, 90, 22050, &ex, NULL, 0);
    Render rnx = render(PRESET_PATROL, 99, 90, 22050, &nx, NULL, 0);
    CHECK(rex.rms < rnx.rms * 0.7, "Silent Exchange reduces level (%.4f vs %.4f)", rex.rms, rnx.rms);
    CHECK(rex.events < rnx.events, "Silent Exchange thins events (%u vs %u)", rex.events, rnx.events);

    /* hovering vs fast flight: percussion density follows movement */
    MusicEnv hov = env; hov.speed = 0.0f;
    MusicEnv fast = env; fast.speed = 1.0f;
    MusicStats sh, sf;
    render(PRESET_PATROL, 31, 120, 22050, &hov, NULL, 0); hush_music_stats(hush_music_instance(), &sh);
    render(PRESET_PATROL, 31, 120, 22050, &fast, NULL, 0); hush_music_stats(hush_music_instance(), &sf);
    CHECK(sf.layer_events[LAYER_PERC] > sh.layer_events[LAYER_PERC] * 1.3, "fast flight has more percussion (%u vs %u)", sf.layer_events[LAYER_PERC], sh.layer_events[LAYER_PERC]);
}

static void test_audio_long(void) {
    printf("[audio] 30-minute stability, structure and repetition\n");
    MusicParams p;
    hush_preset_get(PRESET_DRIFT, &p);
    p.seed = 2146;
    HushMusic *m = hush_music_instance();
    hush_music_init(m, 22050, &p);
    MusicEnv env = {0};
    env.active = 1; env.district = DISTRICT_STACKS; env.altitude = 0.5f; env.speed = 0.3f; env.landmark = -1;
    hush_music_set_env(m, &env);
    static float buf[4096];
    double peak = 0; int nan = 0;
    /* fingerprint each 8-bar window by its event hash delta to detect literal loops */
    uint32_t window_hash[512]; int nw = 0;
    uint32_t last_hash = 0; int last_bar = 0;
    for (int s = 0; s < 30 * 60; s++) {
        for (int k = 0; k < 22050 / 1050; k++) {
            hush_music_render(m, buf, 1050);
            for (int i = 0; i < 2100; i++) { if (buf[i] != buf[i]) nan++; else if (fabs(buf[i]) > peak) peak = fabs(buf[i]); }
        }
        const MusicViz *z = hush_music_viz(m);
        if (z->bar >= last_bar + 8 && nw < 512) { window_hash[nw++] = z->event_hash ^ last_hash; last_hash = z->event_hash; last_bar = z->bar; }
        if (s == 600) { env.district = DISTRICT_UNDERTOW; env.rain = 0.8f; hush_music_set_env(m, &env); }
        if (s == 1200) { env.district = DISTRICT_CIVIC; env.speed = 0.8f; hush_music_set_env(m, &env); }
    }
    MusicStats st;
    hush_music_stats(m, &st);
    CHECK(nan == 0, "no NaN over 30 minutes");
    CHECK(peak < 0.99, "no clipping over 30 minutes (peak %.3f)", peak);
    CHECK(st.section_changes >= 6, "long-form structure changes sections (%u)", st.section_changes);
    int distinct = 0;
    for (int i = 0; i < SEC_COUNT; i++) if (st.section_hist[i]) distinct++;
    CHECK(distinct >= 4, "at least 4 distinct section types in 30 min (%d)", distinct);
    CHECK(st.chord_changes >= 30, "harmony evolves (%u chord changes)", st.chord_changes);
    CHECK(st.max_voices <= 25 + 8, "voice limit respected (%u)", st.max_voices);
    int dup = 0;
    for (int i = 0; i < nw; i++) for (int j = i + 1; j < nw; j++) if (window_hash[i] == window_hash[j] && window_hash[i] != 0) dup++;
    CHECK(dup <= nw / 10, "no literal 8-bar loops (%d duplicate windows of %d)", dup, nw);
    printf("  30 min: bars %u, sections %u, chords %u, motifs new/recall/var %u/%u/%u, max voices %u, steals %u, %d windows, %d dup\n",
           st.bars, st.section_changes, st.chord_changes, st.motif_new, st.motif_recall, st.motif_variation, st.max_voices, st.voice_steals, nw, dup);
}

/* ---------------------------------------------------------------- world */

static World g_world;

static void test_world(void) {
    printf("[world] generation, routes and points of interest\n");
    for (int d = 0; d < DISTRICT_COUNT; d++) {
        World *w = &g_world;
        world_generate(w, d);
        uint32_t h1 = H_FNV_INIT;
        for (int i = 0; i < w->n_boxes; i++) h1 = h_fnv1a(h1, (uint32_t)(int)(w->boxes[i].b.x0 * 16 + w->boxes[i].b.y1 * 7));
        world_generate(w, d);
        uint32_t h2 = H_FNV_INIT;
        for (int i = 0; i < w->n_boxes; i++) h2 = h_fnv1a(h2, (uint32_t)(int)(w->boxes[i].b.x0 * 16 + w->boxes[i].b.y1 * 7));
        CHECK(h1 == h2, "%s generation deterministic", w->name);
        CHECK(w->n_boxes > 150 && w->n_boxes < MAX_BOXES, "%s has %d boxes", w->name, w->n_boxes);
        CHECK(w->n_refs < GRID_MAX_REFS, "%s grid refs %d fit", w->name, w->n_refs);
        CHECK(world_poi(w, "pad") != NULL, "%s has a pad", w->name);
        /* spawn is free */
        V3 sp = w->spawn;
        CHECK(!world_collide_sphere(w, &sp, CRAFT_RADIUS, NULL), "%s spawn is free", w->name);
        /* every lane sample is clear of geometry */
        int bad = 0, samples = 0;
        for (int l = 0; l < w->n_lanes; l++) {
            const Lane *ln = &w->lanes[l];
            for (float s = 0; s < ln->len; s += 4.0f) {
                V3 p = lane_eval(ln, s, NULL);
                V3 q = p;
                samples++;
                if (world_collide_sphere(w, &q, ln->kind == 0 ? 0.7f : 2.0f, NULL)) { if (bad < 3) printf("    lane %d (kind %d) blocked at (%.0f,%.0f,%.0f)\n", l, ln->kind, p.x, p.y, p.z); bad++; }
            }
        }
        CHECK(bad == 0, "%s: all %d lane samples clear (%d blocked)", w->name, samples, bad);
        int lm = 0;
        for (int i = 0; i < w->n_pois; i++) if (w->pois[i].kind == POI_LANDMARK) lm++;
        CHECK(lm >= 1, "%s has a sonic landmark", w->name);
        printf("  fingerprint %08x\n", world_fingerprint(w));
        printf("  %-13s boxes %4d cyls %2d lights %3d ads %2d lanes %2d pois %2d refs %4d\n", w->name, w->n_boxes, w->n_cyls, w->n_lights, w->n_ads, w->n_lanes, w->n_pois, w->n_refs);
    }
}

/* ---------------------------------------------------------------- flight */

static void test_flight(void) {
    printf("[flight] frame-rate independence, hover, collision\n");
    World *w = &g_world;
    world_generate(w, DISTRICT_CIVIC);
    Craft a, b;
    memset(&a, 0, sizeof(a)); memset(&b, 0, sizeof(b));
    craft_init(&a, v3(0, 440, 0), 0);
    craft_init(&b, v3(0, 440, 0), 0);
    FlightInput in = {0};
    in.fwd = 1.0f; in.yaw = 0.3f;
    for (int i = 0; i < 300; i++) craft_update(&a, w, &in, 1.0f / 60.0f);
    for (int i = 0; i < 150; i++) craft_update(&b, w, &in, 1.0f / 30.0f);
    CHECK(v3_dist(a.pos, b.pos) < 0.5f, "30 fps and 60 fps give the same path (%.3f m apart)", v3_dist(a.pos, b.pos));
    CHECK(a.speed > 30.0f, "craft accelerates (%.1f m/s)", a.speed);
    /* releasing controls: the Moth settles into a hover without losing altitude */
    FlightInput none = {0};
    float y0 = a.pos.y;
    for (int i = 0; i < 600; i++) craft_update(&a, w, &none, 1.0f / 60.0f);
    CHECK(a.speed < 1.0f, "settles to hover (%.2f m/s)", a.speed);
    CHECK(fabsf(a.pos.y - y0) < 1.0f, "holds altitude while hovering");
    /* handedness: "right" on the controls must be right on screen. The renderer's
     * screen-right is cross(forward, up) (see gfx_set_camera). */
    {
        Craft h; memset(&h, 0, sizeof(h));
        craft_init(&h, v3(0, 440, 0), 0.7f);
        Camera cam; camera_init(&cam, &h);
        V3 f0 = v3_norm(v3_sub(cam.target, cam.eye));
        V3 scr_right = v3_norm(v3_cross(f0, v3(0, 1, 0)));
        V3 start = h.pos;
        FlightInput r = {0}; r.strafe = 1.0f;
        for (int i = 0; i < 60; i++) craft_update(&h, w, &r, 1.0f / 60.0f);
        CHECK(v3_dot(v3_sub(h.pos, start), scr_right) > 3.0f, "stick right strafes toward screen-right");
        CHECK(h.bank > 0.05f, "strafing right banks right (bank %.2f)", h.bank);
        craft_init(&h, v3(0, 440, 0), 0.7f);
        FlightInput t = {0}; t.yaw = 1.0f;
        for (int i = 0; i < 30; i++) craft_update(&h, w, &t, 1.0f / 60.0f);
        CHECK(v3_dot(craft_forward(&h), scr_right) > 0.1f, "R trigger turns the nose toward screen-right");
        CHECK(v3_dot(craft_right(&h), v3_norm(v3_cross(craft_forward(&h), v3(0, 1, 0)))) > 0.99f, "craft_right equals forward x up");
    }
    /* fly into a wall: the craft must not pass through */
    Craft c; memset(&c, 0, sizeof(c));
    const Poi *arch = world_poi(w, "archive");
    craft_init(&c, v3(arch->p.x, 100, arch->p.z + 80), H_PI);
    FlightInput go = {0}; go.fwd = 1.0f; go.boost = 1;
    for (int i = 0; i < 600; i++) craft_update(&c, w, &go, 1.0f / 60.0f);
    CHECK(c.pos.z > arch->p.z - 1.0f, "collision stops the craft at the Archive wall (z %.1f)", c.pos.z);
    CHECK(c.hull < c.hull_max, "impact damages the hull (%.1f)", c.hull);
}

/* ---------------------------------------------------------------- saves */

static void test_saves(void) {
    printf("[saves] round trip, corruption, truncation\n");
    Progress p, q;
    progress_defaults(&p);
    progress_reward(&p, RW_STACKS | RW_WAVE_VOX | RW_SCANNER | RW_PRESET_CHOIR);
    p.missions_done = 0x1F; p.music.seed = 123456; p.music.tempo = 101.5f; p.music.mode = MODE_AUTO; p.music.drone_root = -5;
    p.user_used[2] = 1; p.user[2].density = 0.123f; p.seeds[5] = 999; p.n_seeds = 6; p.landmarks = 0x15;
    uint8_t buf[SAVE_MAX_BYTES];
    int n = progress_serialize(&p, buf, sizeof(buf));
    CHECK(n > 16, "serialises (%d bytes)", n);
    const char *err = NULL;
    CHECK(progress_deserialize(&q, buf, n, &err), "round-trip loads (%s)", err ? err : "ok");
    CHECK(q.missions_done == p.missions_done && q.music.seed == p.music.seed && q.music.tempo == p.music.tempo && q.music.mode == MODE_AUTO
          && q.music.drone_root == -5 && q.user[2].density == p.user[2].density && q.seeds[5] == 999 && q.landmarks == 0x15
          && (q.waves & (1u << W_VOX)) && (q.districts & (1u << DISTRICT_STACKS)) && q.scanner_level == 1, "fields survive the round trip");
    uint8_t bad[SAVE_MAX_BYTES];
    memcpy(bad, buf, n); bad[40] ^= 0x5A;
    CHECK(!progress_deserialize(&q, bad, n, &err) && err, "corruption detected (%s)", err ? err : "-");
    CHECK(q.missions_done == 0 && (q.districts == 1), "damaged save falls back to defaults");
    CHECK(!progress_deserialize(&q, buf, n / 2, &err), "truncated save rejected (%s)", err ? err : "-");
    memcpy(bad, buf, n); bad[0] = 'X';
    CHECK(!progress_deserialize(&q, bad, n, &err), "wrong magic rejected");
    CHECK(!progress_deserialize(&q, NULL, 0, &err), "missing save handled");
}

/* ---------------------------------------------------------------- mission bot */

static int run_mission_bot(int mi, float *time_out, float *hull_out) {
    World *w = &g_world;
    const MissionDef *md = &hush_missions[mi];
    world_generate(w, md->district);
    static Traffic t;
    traffic_init(&t, w);
    Craft c; memset(&c, 0, sizeof(c));
    craft_init(&c, w->spawn, w->spawn_yaw);
    craft_apply_upgrades(&c, 1, 1);
    MissionRun m;
    mission_start(&m, mi, w, &t, &c, 1, 1);
    static Autopilot ap;
    autopilot_reset(&ap);
    float dt = 1.0f / 30.0f, tsim = 0;
    while (tsim < 900.0f && m.state == MS_ACTIVE) {
        FlightInput in; MissionInput mi_in;
        autopilot_mission(&ap, w, &t, &c, &m, &in, &mi_in);
        craft_update(&c, w, &in, dt);
        traffic_update(&t, w, dt, c.pos);
        mission_update(&m, w, &t, &c, &mi_in, dt);
        tsim += dt;
    }
    *time_out = tsim;
    *hull_out = c.hull;
    if (m.state != MS_COMPLETE) printf("    mission %d stuck at step %d (%s) state %d reason '%s' pos (%.0f,%.0f,%.0f) target (%.0f,%.0f,%.0f)\n", mi + 1, m.step, md->steps[m.step].text, m.state, m.fail_reason, c.pos.x, c.pos.y, c.pos.z, m.target.x, m.target.y, m.target.z);
    return m.state == MS_COMPLETE;
}

static void test_missions(void) {
    printf("[missions] autopilot plays all %d missions through the real game logic\n", MISSION_COUNT);
    Progress p;
    progress_defaults(&p);
    for (int i = 0; i < MISSION_COUNT; i++) {
        CHECK(progress_mission_unlocked(&p, i), "mission %d unlocked in campaign order", i + 1);
        float tm = 0, hull = 0;
        int ok = run_mission_bot(i, &tm, &hull);
        CHECK(ok, "mission %d '%s' completable", i + 1, hush_missions[i].title);
        printf("  %2d %-20s %-12s %s in %5.0fs (hull %3.0f)\n", i + 1, hush_missions[i].title, world_district_name(hush_missions[i].district), ok ? "COMPLETED" : "FAILED   ", tm, hull);
        if (ok) { p.missions_done |= 1u << i; progress_reward(&p, hush_missions[i].rewards); }
    }
    CHECK(p.districts == 0x0F, "all districts unlocked by the end of the campaign");
    CHECK((p.presets & (1u << PRESET_NULLZONE)) && (p.waves & (1u << W_GLASS)), "final rewards granted");
    /* failure path: hull destroyed fails the mission */
    World *w = &g_world;
    world_generate(w, DISTRICT_CIVIC);
    static Traffic t; traffic_init(&t, w);
    Craft c; memset(&c, 0, sizeof(c)); craft_init(&c, w->spawn, 0);
    MissionRun m; mission_start(&m, 0, w, &t, &c, 0, 0);
    c.hull = 0; MissionInput none = {0};
    mission_update(&m, w, &t, &c, &none, 0.1f);
    CHECK(m.state == MS_FAILED, "destroyed hull fails the mission");
    /* time limit failure */
    craft_init(&c, w->spawn, 0);
    world_generate(w, DISTRICT_WORKS); traffic_init(&t, w);
    mission_start(&m, 6, w, &t, &c, 0, 0);
    m.step = 1; m.timer = 0.05f; m.state = MS_ACTIVE;
    mission_update(&m, w, &t, &c, &none, 0.1f);
    CHECK(m.state == MS_FAILED, "ring time limit fails the mission");
}

static void test_crowd(void) {
    printf("[crowd] pedestrians stay on sidewalks, gather near the player, scatter from the Moth\n");
    World *w = &g_world;
    world_generate(w, DISTRICT_STACKS);
    static Crowd c;
    crowd_init(&c, w);
    CHECK(c.n >= 150, "Stacks has a crowd (%d)", c.n);
    V3 craft = v3(w->blocks[w->n_blocks / 2].x1 + 8.0f, 200.0f, w->blocks[w->n_blocks / 2].z0);
    for (int i = 0; i < 30 * 60; i++) crowd_update(&c, w, craft, 0.8f, 1.0f / 30.0f);
    int bad = 0, near = 0, umb = 0;
    for (int i = 0; i < c.n; i++) {
        V3 head = v3_add(c.p[i].pos, v3(0, 1.2f, 0));
        if (world_collide_sphere(w, &head, 0.25f, NULL)) bad++;
        if (v3_dist_xz(c.p[i].pos, craft) < 200.0f) near++;
        umb += c.p[i].umbrella;
    }
    CHECK(bad == 0, "no pedestrian inside geometry after 60 s (%d)", bad);
    CHECK(near > c.n / 2, "crowd gathers around the player (%d of %d within 200 m)", near, c.n);
    CHECK(umb > c.n / 2, "umbrellas up in the rain (%d)", umb);
    /* bring the Moth down on top of someone: they run */
    Ped *p = &c.p[0];
    V3 low = v3(p->pos.x + 3.0f, p->pos.y + 4.0f, p->pos.z);
    for (int i = 0; i < 30; i++) crowd_update(&c, w, low, 0.0f, 1.0f / 30.0f);
    CHECK(p->flee > 0.5f, "pedestrian flees a low Moth (%.2f)", p->flee);
    world_generate(w, DISTRICT_UNDERTOW);
    crowd_init(&c, w);
    CHECK(c.n == 0, "nobody walks the flooded Undertow");
}

/* Model of the PSP GE rule: near-plane clip, then reject the primitive if any vertex
 * lands more than 2048 px from the viewport centre. Returns 1 if the GE would draw it. */
static int ge_accepts(const GuardClip *g, const GcVtx *t, float fx, float fy, float znear) {
    GcCV c[3];
    for (int i = 0; i < 3; i++) gc_cvx(g, &c[i], &t[i]);
    /* vertices in front of the near plane are checked directly; edges crossing it produce
     * new vertices on the plane, which are checked too */
    for (int i = 0; i < 3; i++) {
        const GcCV *A = &c[i], *B = &c[(i + 1) % 3];
        if (A->cz >= znear) { if (fabsf(A->cx / A->cz * fx) > 2048.0f || fabsf(A->cy / A->cz * fy) > 2048.0f) return 0; }
        if ((A->cz >= znear) != (B->cz >= znear)) {
            float t2 = (znear - A->cz) / (B->cz - A->cz);
            float x = A->cx + (B->cx - A->cx) * t2, y = A->cy + (B->cy - A->cy) * t2;
            if (fabsf(x / znear * fx) > 2048.0f || fabsf(y / znear * fy) > 2048.0f) return 0;
        }
    }
    return 1;
}

static void test_guardclip(void) {
    printf("[guardclip] risky triangles are clipped so the PSP GE accepts them\n");
    const float tv = tanf(62.0f * 0.5f * H_PI / 180.0f), znear = 1.5f;
    const float fy = 136.0f / tv, fx = 240.0f / (tv * 480.0f / 272.0f);
    GuardClip g;
    gc_setup(&g, v3(0, 5, 0), v3(0, 0, 1), v3(-1, 0, 0), v3(0, 1, 0), tv, 480.0f, 272.0f, znear);
    /* the reported bug: a 77 m ground tile under a low camera is rejected outright */
    GcVtx tile[3] = { {0, 0, 0xffffffffu, -40, 0, -20}, {0, 0, 0xffffffffu, 40, 0, -20}, {0, 0, 0xffffffffu, 40, 0, 57} };
    CHECK(!ge_accepts(&g, tile, fx, fy, znear), "unclipped ground tile would be rejected by the GE (reproduces the bug)");
    static GcVtx out[4096];
    int n = 0;
    CHECK(gc_clip_tri(&g, &tile[0], &tile[1], &tile[2], out, &n, 4096) == 0 && n >= 3, "tile is clipped into %d vertices", n);
    int all_ok = 1;
    for (int i = 0; i < n; i += 3) all_ok &= ge_accepts(&g, &out[i], fx, fy, znear);
    CHECK(all_ok, "every clipped triangle is accepted by the GE");
    /* random fuzz: risky triangles near the camera */
    uint32_t s = 12345; int rejected_after = 0, total = 0, lost_visible = 0;
    for (int k = 0; k < 4000; k++) {
        GcVtx t[3];
        for (int i = 0; i < 3; i++) {
            s ^= s << 13; s ^= s >> 17; s ^= s << 5; float a = (float)(s % 2000) / 10.0f - 100.0f;
            s ^= s << 13; s ^= s >> 17; s ^= s << 5; float b = (float)(s % 2000) / 50.0f - 15.0f;
            s ^= s << 13; s ^= s >> 17; s ^= s << 5; float c = (float)(s % 2000) / 10.0f - 60.0f;
            t[i].x = a; t[i].y = b; t[i].z = c; t[i].u = t[i].v = 0; t[i].color = 0xff808080u;
        }
        n = 0;
        if (gc_clip_tri(&g, &t[0], &t[1], &t[2], out, &n, 4096)) { if (!ge_accepts(&g, t, fx, fy, znear)) rejected_after++; total++; continue; }
        for (int i = 0; i < n; i += 3) { total++; if (!ge_accepts(&g, &out[i], fx, fy, znear)) rejected_after++; }
        /* a point of the triangle that is on screen must still be covered: test the centroid */
        GcCV cc; GcVtx cen = { 0, 0, 0, (t[0].x + t[1].x + t[2].x) / 3, (t[0].y + t[1].y + t[2].y) / 3, (t[0].z + t[1].z + t[2].z) / 3 };
        gc_cvx(&g, &cc, &cen);
        int on_screen = cc.cz > znear * 1.2f && fabsf(cc.cx / cc.cz * fx) < 240 && fabsf(cc.cy / cc.cz * fy) < 136;
        if (on_screen && n == 0) lost_visible++;
    }
    CHECK(rejected_after == 0, "fuzz: 0 of %d emitted triangles would be rejected (%d)", total, rejected_after);
    {   /* and every emitted vertex stays within GC_SAFE_SCREENS screens of the centre (no extreme triangles) */
        int far_out = 0;
        s = 777;
        for (int k = 0; k < 4000; k++) {
            GcVtx t[3];
            for (int i = 0; i < 3; i++) {
                s ^= s << 13; s ^= s >> 17; s ^= s << 5; t[i].x = (float)(s % 2000) / 10.0f - 100.0f;
                s ^= s << 13; s ^= s >> 17; s ^= s << 5; t[i].y = (float)(s % 2000) / 50.0f - 15.0f;
                s ^= s << 13; s ^= s >> 17; s ^= s << 5; t[i].z = (float)(s % 2000) / 10.0f - 60.0f;
                t[i].u = t[i].v = 0; t[i].color = 0xff808080u;
            }
            n = 0;
            const GcVtx *src = out;
            int cnt;
            if (gc_clip_tri(&g, &t[0], &t[1], &t[2], out, &n, 4096)) { src = t; cnt = 3; } else cnt = n;
            for (int i = 0; i < cnt; i++) {
                GcCV c; gc_cvx(&g, &c, &src[i]);
                if (c.cz <= 0 || fabsf(c.cx / c.cz * fx) > 240.0f * GC_SAFE_SCREENS * 1.001f || fabsf(c.cy / c.cz * fy) > 136.0f * GC_SAFE_SCREENS * 1.001f) far_out++;
            }
        }
        CHECK(far_out == 0, "fuzz: no emitted vertex lies more than %.0f screens from the centre (%d)", GC_SAFE_SCREENS, far_out);
    }
    CHECK(lost_visible == 0, "fuzz: no on-screen triangle disappears (%d lost)", lost_visible);
}

static void test_signals(void) {
    printf("[signals] 32 hidden transmissions, each found by the autopilot using only the hunt logic\n");
    uint32_t ids = 0;
    for (int d = 0; d < DISTRICT_COUNT; d++) {
        World *w = &g_world;
        world_generate(w, d);
        CHECK(w->n_signals == SIGNALS_PER_DISTRICT, "%s has %d transmissions", w->name, w->n_signals);
        uint32_t mask = 0;
        int found = 0;
        float total_t = 0;
        for (int k = 0; k < w->n_signals; k++) {
            ids |= 1u << w->signals[k].id;
            CHECK(story_intercept[w->signals[k].id] != NULL, "intercept text exists");
            /* start at the pad and fly to where the hunt says the strongest unfound signal is */
            Craft c; memset(&c, 0, sizeof(c));
            craft_init(&c, w->spawn, w->spawn_yaw);
            static Autopilot ap; autopilot_reset(&ap);
            Hunt h; hunt_reset(&h);
            float t = 0;
            int got = -1;
            V3 goal = w->signals[k].p;
            uint32_t others = 0;
            for (int j = 0; j < w->n_signals; j++) if (j != k) others |= 1u << w->signals[j].id;   /* hunt only this one */
            while (t < 240.0f && got < 0) {
                FlightInput in;
                autopilot_fly_to(&ap, w, &c, goal, &in);
                craft_update(&c, w, &in, 1.0f / 30.0f);
                hunt_update(&h, w, &c, others | mask, 1, 1.0f / 30.0f);
                if (h.found >= 0) got = h.found;
                t += 1.0f / 30.0f;
            }
            if (got == (int)w->signals[k].id) { found++; mask |= 1u << got; total_t += t; }
            else printf("    %s signal %d (kind %d at %.0f,%.0f,%.0f) not captured\n", w->name, k, w->signals[k].kind, goal.x, goal.y, goal.z);
        }
        CHECK(found == SIGNALS_PER_DISTRICT, "%s: all %d transmissions reachable and capturable (avg %.0fs)", w->name, found, found ? total_t / found : 0.0f);
        CHECK(hunt_count_district(w, mask) == found, "district count matches");
    }
    CHECK(ids == 0xFFFFFFFFu, "32 distinct global ids (mask %08x)", ids);
    /* the meter rises as you approach and aim */
    World *w = &g_world;
    world_generate(w, DISTRICT_STACKS);
    Craft c; memset(&c, 0, sizeof(c));
    V3 s0 = w->signals[0].p;
    craft_init(&c, v3(s0.x + 200, s0.y, s0.z), 0);
    c.yaw = atan2f(s0.x - c.pos.x, s0.z - c.pos.z);
    Hunt h; hunt_reset(&h);
    hunt_update(&h, w, &c, ~(1u << w->signals[0].id), 1, 0.1f);
    float far_s = h.strength;
    c.pos = v3(s0.x + 60, s0.y, s0.z);
    hunt_update(&h, w, &c, ~(1u << w->signals[0].id), 1, 0.1f);
    CHECK(h.strength > far_s && h.nearest == 0, "strength rises when closer (%.2f -> %.2f)", far_s, h.strength);
    c.yaw += H_PI;
    float aimed = h.strength;
    hunt_update(&h, w, &c, ~(1u << w->signals[0].id), 1, 0.1f);
    CHECK(h.strength < aimed, "facing away weakens it (%.2f -> %.2f)", aimed, h.strength);
    /* collected motifs change the composition, deterministically */
    {
        MusicParams mp; hush_preset_get(PRESET_DRIFT, &mp); mp.seed = 55;
        MusicEnv e = {0}; e.active = 1; e.district = DISTRICT_STACKS; e.landmark = -1; e.altitude = 0.4f; e.speed = 0.3f;
        HushMusic *m = hush_music_instance();
        static float buf[2048];
        uint32_t hashes[3];
        for (int run = 0; run < 3; run++) {
            hush_music_init(m, 22050, &mp); hush_music_set_env(m, &e);
            hush_music_set_collected(m, run == 0 ? 0u : 0x0000FF00u);
            for (int i = 0; i < 22050 * 240 / 1024; i++) hush_music_render(m, buf, 1024);
            hashes[run] = hush_music_event_hash(m);
        }
        CHECK(hashes[1] != hashes[0] && hashes[1] == hashes[2], "collected motifs recur in the music, reproducibly (%08x/%08x/%08x)", hashes[0], hashes[1], hashes[2]);
    }
    /* saves remember finds; old saves without the field still load */
    Progress p, q; progress_defaults(&p); p.signals_found = 0x80000081u;
    uint8_t buf[SAVE_MAX_BYTES]; int n = progress_serialize(&p, buf, sizeof(buf));
    const char *err;
    CHECK(progress_deserialize(&q, buf, n, &err) && q.signals_found == 0x80000081u, "signals saved and loaded");
}

static void test_city_reactions(void) {
    printf("[city] the street reacts to the Moth\n");
    World *w = &g_world;
    world_generate(w, DISTRICT_STACKS);
    static Traffic t;
    traffic_init(&t, w);
    /* find a ground car and park the Moth low in its lane, 15 m ahead */
    int gi = -1;
    for (int i = 0; i < t.n; i++) if (t.v[i].kind == VK_GROUND) { gi = i; break; }
    CHECK(gi >= 0, "street traffic exists in the Stacks");
    if (gi >= 0) {
        Vehicle *v = &t.v[gi];
        V3 moth = v3_add(v3_madd(v->pos, v->dir, 15.0f), v3(0, 3.0f, 0));
        int horns = 0;
        for (int k = 0; k < 30 * 6; k++) {
            traffic_update(&t, w, 1.0f / 30.0f, v3_add(v3_madd(t.v[gi].pos, t.v[gi].dir, 15.0f), v3(0, 3.0f, 0)));
            horns += t.horns; t.horns = 0;
        }
        (void)moth;
        CHECK(t.v[gi].speed < 0.5f, "a car stops for a Moth hovering in its lane (%.1f m/s)", t.v[gi].speed);
        CHECK(horns > 0, "and honks (%d)", horns);
        for (int k = 0; k < 30 * 5; k++) traffic_update(&t, w, 1.0f / 30.0f, v3(0, 440, 0));
        CHECK(t.v[gi].speed > 5.0f, "and drives on once it leaves (%.1f m/s)", t.v[gi].speed);
    }
    /* flying traffic swerves around a Moth in its path */
    int ai = -1;
    for (int i = 0; i < t.n; i++) if (t.v[i].kind == VK_CAR) { ai = i; break; }
    if (ai >= 0) {
        float base_y = w->lanes[t.v[ai].lane].pts[0].y;
        for (int k = 0; k < 30 * 3; k++) traffic_update(&t, w, 1.0f / 30.0f, v3_add(t.v[ai].pos, v3(0, -1.0f - t.v[ai].avoid_y, 0)));
        CHECK(h_absf(t.v[ai].avoid_y) > 4.0f, "a flying car swerves around the Moth (%.1f m, lane at %.0f)", t.v[ai].avoid_y, base_y);
    }
    /* pedestrians stop and look up at a Moth hovering across the street */
    static Crowd c;
    crowd_init(&c, w);
    Ped *p = &c.p[0];
    V3 moth = v3(p->pos.x + 30.0f, p->pos.y + 12.0f, p->pos.z);
    for (int k = 0; k < 30 * 4; k++) crowd_update(&c, w, moth, 0.0f, 1.0f / 30.0f);
    CHECK(c.p[0].watch > 0.6f && c.watchers > 0, "pedestrians stop to watch (%d watching)", c.watchers);
    int photos = 0;
    for (int k = 0; k < 30 * 20; k++) { crowd_update(&c, w, moth, 0.0f, 1.0f / 30.0f); for (int i = 0; i < c.n; i++) photos += c.p[i].photo > 0.11f; }
    CHECK(photos > 0, "some take pictures (%d flashes)", photos);
}

static void test_env_bridge(void) {
    printf("[bridge] world drives music inputs\n");
    World *w = &g_world;
    world_generate(w, DISTRICT_UNDERTOW);
    static Traffic t; traffic_init(&t, w);
    Weather wx; weather_init(&wx, WX_HEAVY_RAIN, TOD_NIGHT, 1);
    Craft c; memset(&c, 0, sizeof(c));
    const Poi *ex = world_poi(w, "exchange");
    craft_init(&c, ex->p, 0);
    EnvBridge b = {0};
    MusicEnv e;
    env_bridge_update(&b, &e, w, &c, &t, &wx, 0, 0, 0);
    CHECK(e.landmark == LM_EXCHANGE && e.landmark_prox > 0.9f, "landmark proximity detected (%d %.2f)", e.landmark, e.landmark_prox);
    CHECK(e.rain > 0.9f && e.district == DISTRICT_UNDERTOW, "weather and district mapped");
    craft_init(&c, v3(0, 440, 0), 0);
    env_bridge_update(&b, &e, w, &c, &t, &wx, 0, 0, 0);
    CHECK(e.altitude > 0.95f, "high altitude maps near 1 (%.2f)", e.altitude);
}

int main(int argc, char **argv) {
    int quick = argc > 1 && !strcmp(argv[1], "--quick");
#ifdef HUSH_FPE
    /* the PSP's FPU traps these by default: model it so a division by zero fails here, not on the console */
    feenableexcept(FE_DIVBYZERO | FE_INVALID | FE_OVERFLOW);
#endif
    test_saves();
    test_world();
    test_flight();
    test_env_bridge();
    test_crowd();
    test_guardclip();
    test_signals();
    test_city_reactions();
    test_missions();
    test_audio();
    if (!quick) test_audio_long();
    printf("\n%d checks passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
