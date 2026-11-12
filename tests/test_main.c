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
                if (world_collide_sphere(w, &q, ln->kind == 0 ? 1.5f : 2.0f, NULL)) { if (bad < 3) printf("    lane %d (kind %d) blocked at (%.0f,%.0f,%.0f)\n", l, ln->kind, p.x, p.y, p.z); bad++; }
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
    craft_init(&a, v3(0, 300, 0), 0);
    craft_init(&b, v3(0, 300, 0), 0);
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
        traffic_update(&t, w, dt);
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
    test_saves();
    test_world();
    test_flight();
    test_env_bridge();
    test_missions();
    test_audio();
    if (!quick) test_audio_long();
    printf("\n%d checks passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
