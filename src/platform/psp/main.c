/* HUSH — PSP entry point, application state machine and main loop.
 *
 * Built twice: the release EBOOT, and (with -DHUSH_AUTOTEST) a validation
 * EBOOT that drives the real game through a fixed script inside PPSSPP,
 * printing performance figures and saving screenshots. */
#include <pspkernel.h>
#include <pspdisplay.h>
#include <psppower.h>
#include <stdio.h>
#include <string.h>
#include "../../ui/app.h"
#include "../../render/textures.h"
#include "../../render/render_moth.h"
#include "audio_out.h"
#include "save_io.h"
#include "diag.h"
#include <pspfpu.h>

#define HUSH_BUILD "build 2026-11-12 diag"

PSP_MODULE_INFO("HUSH", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);
PSP_HEAP_SIZE_KB(-1024);

App g_app;
static volatile int g_exit_req;

static int exit_cb(int a, int b, void *c) { (void)a; (void)b; (void)c; g_exit_req = 1; return 0; }
static int cb_thread(SceSize a, void *b) {
    (void)a; (void)b;
    int id = sceKernelCreateCallback("hush_exit", exit_cb, NULL);
    sceKernelRegisterExitCallback(id);
    sceKernelSleepThreadCB();
    return 0;
}

/* ---------------- app functions ---------------- */

void app_set_state(AppState s) {
    if (s != g_app.state) { diag_log("state %d -> %d (mode %d, district %d)", (int)g_app.state, (int)s, g_app.mode, g_app.world_district); if (s == ST_PLAY || s == ST_PROLOGUE) diag_arm(16, "gameplay starts"); }
    g_app.state = s;
    g_app.state_t = 0;
}

void app_note(const char *msg) {
    snprintf(g_app.note, sizeof(g_app.note), "%s", msg);
    g_app.note_t = 3.0f;
}

void app_push_music(void) {
    App *a = &g_app;
    hush_music_set_params(a->music, &a->prog.music);
    hush_music_set_collected(a->music, a->prog.signals_found);
    audio_out_set_gain((float)a->prog.music_vol / 8.0f, (float)a->prog.sfx_vol / 7.0f);
}

void app_save(void) {
    char msg[64];
    save_io_store(&g_app.prog, msg, sizeof(msg));
    app_note(msg);
    printf("HUSH: %s\n", msg);
}

void app_tx(const char *speaker, const char *text) {
    App *a = &g_app;
    if (!text) return;
    if (a->tx_n >= TX_QUEUE) { for (int i = 1; i < TX_QUEUE; i++) a->tx[i - 1] = a->tx[i]; a->tx_n--; }
    Transmission *t = &a->tx[a->tx_n++];
    t->speaker = speaker; t->t = 0;
    ctl_subst(text, t->text, sizeof(t->text));
    t->dur = 3.0f + (float)strlen(t->text) / 22.0f;
}

static void loading_screen(const char *what) {
    gfx_begin(RGBA(4, 6, 8, 255));
    gfx_mode_2d();
    gfx_text(FONT_SMALL, SCR_W * 0.5f, SCR_H * 0.5f - 6, what, RGBA(160, 175, 190, 255), ALIGN_CENTER);
    gfx_end(30);
}

void app_load_district(int d) {
    App *a = &g_app;
    if (a->world_district == d) return;
    char buf[48];
    snprintf(buf, sizeof(buf), "APPROACHING %s", world_district_name(d));
    diag_arm(16, "district load");
    diag_log("load district %d (was %d), free %d KB", d, a->world_district, (int)(sceKernelTotalFreeMemSize() / 1024));
    loading_screen(buf);
    uint64_t t0 = gfx_ticks_us();
    world_generate(&a->world, d);
    uint64_t t1 = gfx_ticks_us();
    diag_log("generated: boxes %d lights %d ads %d neon %d signals %d peds-blocks %d in %.0f ms", a->world.n_boxes, a->world.n_lights, a->world.n_ads, a->world.n_neon, a->world.n_signals, a->world.n_blocks, (float)(t1 - t0) / 1000.0f);
    rw_build(&a->world);
    diag_log("mesh built: %d verts in %.0f ms", rw_stats()->verts_total, rw_stats()->build_ms);
    a->world_district = d;
    printf("HUSH: district %s generated in %.1f ms, mesh %d verts built in %.1f ms (boxes %d, lights %d)\n",
           a->world.name, (float)(t1 - t0) / 1000.0f, rw_stats()->verts_total, rw_stats()->build_ms, a->world.n_boxes, a->world.n_lights);
}

static void reset_flight(void) {
    App *a = &g_app;
    traffic_init(&a->traffic, &a->world);
    crowd_init(&a->crowd, &a->world);
    memset(&a->craft, 0, sizeof(a->craft));
    craft_apply_upgrades(&a->craft, a->prog.boost_level, a->prog.hull_level);
    craft_init(&a->craft, a->world.spawn, a->world.spawn_yaw);
    camera_init(&a->cam, &a->craft);
    memset(&a->bridge, 0, sizeof(a->bridge));
    autopilot_reset(&a->pilot);
    a->tx_n = 0;
    a->scan_lm = -1; a->scan_t = 0; a->discovery_t = 0;
    hunt_reset(&a->hunt);
    a->brown_t = 0; a->brown_next = 45.0f; a->scan_acc = 0; a->sig_flash_t = 0;
    a->patrol_cool = 20.0f; a->react_cool = 0;
    a->hud_override = -1;
}

void app_start_drift(void) {
    App *a = &g_app;
    app_load_district(a->prog.drift_district);
    a->mode = MODE_DRIFT;
    reset_flight();
    a->cam.mode = a->prog.cam_mode;
    weather_init(&a->wx, a->prog.drift_weather, a->prog.drift_tod, 0xD71F7u + a->prog.music.seed);
    app_push_music();
    app_set_state(ST_PLAY);
    app_note(a->world.name);
}

void app_start_patrol(int m) {
    App *a = &g_app;
    a->mission_id = m;
    app_load_district(hush_missions[m].district);
    a->mode = MODE_PATROL;
    reset_flight();
    a->cam.mode = a->prog.cam_mode;
    weather_init(&a->wx, m % 3 == 1 ? WX_HEAVY_RAIN : (hush_missions[m].district == DISTRICT_UNDERTOW ? WX_FOG : WX_LIGHT_RAIN), TOD_NIGHT, 0x7A7u + (uint32_t)m);
    mission_start(&a->mission, m, &a->world, &a->traffic, &a->craft, a->prog.scanner_level, a->prog.has_emp);
    app_push_music();
    app_set_state(ST_PLAY);
}

void app_start_attract(void) {
    App *a = &g_app;
    if (a->world_district < 0) app_load_district(DISTRICT_STACKS);
    a->mode = MODE_ATTRACT;
    reset_flight();
    a->craft.pos.y += 40.0f;
    a->cam.mode = CAM_CINEMATIC;
    weather_init(&a->wx, WX_LIGHT_RAIN, TOD_NIGHT, 99);
}

static void on_debrief_enter(void) {
    App *a = &g_app;
    a->prog.missions_done |= 1u << a->mission_id;
    uint32_t fresh = progress_reward(&a->prog, hush_missions[a->mission_id].rewards);
    (void)fresh;
    app_save();
}

/* ---------------- automated validation script ---------------- */
#ifdef HUSH_AUTOTEST
static void trace_ev(uint32_t step, int layer, int note, int vel) { printf("EV %u %d %d %d\n", step, layer, note, vel); }
static int g_shot;
static void shot(const char *name) {
    char path[64];
    snprintf(path, sizeof(path), "hush_shot_%02d_%s.bmp", g_shot++, name);
    int ok = gfx_screenshot(path);
    printf("AUTOTEST: screenshot %s %s at t=%.1f\n", path, ok ? "saved" : "FAILED", g_app.time);
}
static void perf(const char *tag) {
    const RenderStats *rs = rw_stats();
    printf("AUTOTEST: perf %-14s fps %.1f cpu %.2fms gpuwait %.2fms tris %d chunks %d lights %d vehicles %d audio_load %.1f%% audio_late %u/%u freemem %dKB list %dKB peak %dKB\n",
           tag, g_app.fps, gfx_last_cpu_ms, gfx_last_gpu_wait_ms, rs->tris_drawn, rs->chunks_drawn, rs->lights_drawn, rs->vehicles_drawn,
           audio_out_load() * 100.0f, audio_out_underruns(), audio_out_blocks(), (int)(sceKernelTotalFreeMemSize() / 1024), gfx_list_last / 1024, gfx_list_peak / 1024);
}
static int g_step __attribute__((unused));
static int g_horns __attribute__((unused));
static float g_step_t;
#if HUSH_AUTOTEST == 2
/* campaign run: every mission in order, autopiloted, fast-forwarded */
static int g_cm = -1, g_cm_ok;
static void autotest_tick(float dt) {
    App *a = &g_app;
    g_step_t += dt;
    if (g_cm < 0) { if (g_step_t > 2.0f) { g_cm = 0; app_start_patrol(0); g_step_t = 0; printf("AUTOTEST: campaign start\n"); } return; }
    if (a->state == ST_DEBRIEF || a->state == ST_FAILED || g_step_t > 400.0f) {
        int ok = a->state == ST_DEBRIEF;
        g_cm_ok += ok;
        printf("AUTOTEST: mission %2d %-20s %s sim %.0fs hull %.0f fps %.1f tris %d list peak %dKB\n", g_cm + 1, hush_missions[g_cm].title,
               ok ? "COMPLETE" : (a->state == ST_FAILED ? a->mission.fail_reason : "TIMEOUT"), a->mission.total_time, a->craft.hull, a->fps, rw_stats()->tris_drawn, gfx_list_peak / 1024);
        if (g_cm == 3 || g_cm == 8) shot("campaign");
        g_cm++;
        if (g_cm >= MISSION_COUNT) { printf("AUTOTEST: campaign %d/12 complete, missions_done=0x%03x districts=0x%x landmarks=0x%x\n", g_cm_ok, a->prog.missions_done, a->prog.districts, a->prog.landmarks); a->quit = 1; return; }
        app_start_patrol(g_cm);
        g_step_t = 0;
    }
}
#else
static void autotest_tick(float dt) {
    App *a = &g_app;
    Input *in = &a->in;
    g_step_t += dt;
    in->scripted = 1; in->s_held = 0; in->s_lx = 0; in->s_ly = 0;
#define NEXT() do { g_step++; g_step_t = 0; } while (0)
    switch (g_step) {
    case 0: if (g_step_t > 3.0f) { shot("title"); perf("title"); NEXT(); } break;
    case 1: in->s_held = PSP_CTRL_CROSS; if (a->state == ST_MENU) NEXT(); break;
    case 2: if (g_step_t > 1.0f) { shot("main_menu"); NEXT(); } break;
    case 3: a->prog.drift_district = DISTRICT_STACKS; a->prog.drift_weather = WX_HEAVY_RAIN; app_start_drift(); printf("AUTOTEST: drift started\n"); NEXT(); break;
    case 4: in->s_ly = 0.9f; in->s_lx = 0.2f; if (g_step_t > 2) in->s_held = PSP_CTRL_RTRIGGER | PSP_CTRL_TRIANGLE;
        if (g_step_t > 6.0f) { shot("drift_stacks_rain"); perf("drift_stacks"); NEXT(); } break;
    case 5: in->s_ly = 0.6f; in->s_held = PSP_CTRL_CROSS; if (g_step_t > 4.0f) { shot("drift_low"); perf("drift_low"); g_step = 50; g_step_t = 0; } break;
    case 50: {   /* street level: put the Moth in the middle of a Stacks street, nose along the street */
        const Block *bl = &a->world.blocks[a->world.n_blocks / 2 + 7];
        float street = a->world.pitch - (bl->x1 - bl->x0);
        a->craft.pos = v3(bl->x1 + street * 0.5f, a->world.ground_y + 5.0f, (bl->z0 + bl->z1) * 0.5f - 20.0f);
        a->craft.vel = v3(0, 0, 0); a->craft.yaw = 0.0f;
        camera_init(&a->cam, &a->craft);
        a->cam.mode = CAM_CHASE;
        printf("AUTOTEST: street level at (%.0f, %.0f, %.0f), crowd %d\n", a->craft.pos.x, a->craft.pos.y, a->craft.pos.z, a->crowd.n);
        NEXT(); break;
    }
    case 51: in->s_ly = 0.25f; if (g_step_t > 3.0f) { shot("street_chase"); perf("street"); a->cam.mode = CAM_COCKPIT; NEXT(); } break;
    case 52: in->s_ly = 0.2f; if (g_step_t > 2.0f) { shot("street_cockpit"); a->cam.mode = CAM_CINEMATIC; NEXT(); } break;
    case 53: if (g_step_t > 3.0f) { shot("street_cinematic"); a->cam.mode = CAM_CHASE; g_step = 70; g_step_t = 0; } break;
    case 70: {   /* hover across the street from a pedestrian: the street stops and looks */
        const Ped *p = &a->crowd.p[0];
        const Block *pb = &a->world.blocks[p->block];
        V3 out = v3_norm(v3(p->pos.x - (pb->x0 + pb->x1) * 0.5f, 0, p->pos.z - (pb->z0 + pb->z1) * 0.5f));
        a->craft.pos = v3(p->pos.x + out.x * 9.0f, a->world.ground_y + 18.0f, p->pos.z + out.z * 9.0f);
        a->craft.vel = v3(0, 0, 0); a->craft.yaw = atan2f(p->pos.x - a->craft.pos.x, p->pos.z - a->craft.pos.z);
        camera_init(&a->cam, &a->craft);
        g_horns = 0; NEXT(); break;
    }
    case 71: g_horns += a->traffic.horns;
        if (g_step_t > 4.0f) { shot("street_watchers"); printf("AUTOTEST: street reaction watchers %d fleeing %d crowd_murmur %.2f\n", a->crowd.watchers, a->crowd.fleeing, a->sfx.crowd); NEXT(); } break;
    case 72: {   /* signal hunt: start 45 m from an uncaptured transmission, nose pointing away */
        const Signal *sg = &a->world.signals[0];
        a->craft.pos = v3_add(sg->p, v3(0, 2.0f, -45.0f));
        a->craft.vel = v3(0, 0, 0); a->craft.yaw = H_PI;   /* facing -Z, the signal is behind */
        camera_init(&a->cam, &a->craft);
        printf("AUTOTEST: signal hunt from 45 m, signal %d kind %d, found mask 0x%08x\n", sg->id, sg->kind, (unsigned)a->prog.signals_found);
        NEXT(); break;
    }
    case 73: in->s_held = PSP_CTRL_CIRCLE;
        if (g_step_t > 2.0f) { shot("signal_trace"); printf("AUTOTEST: trace strength %.2f bearing %.2f dist %.0f\n", a->hunt.strength, a->hunt.bearing, a->hunt.dist);
            a->craft.pos = v3_add(a->world.signals[0].p, v3(0, 2.0f, -8.0f)); a->craft.vel = v3(0, 0, 0); a->craft.yaw = 0; camera_init(&a->cam, &a->craft); NEXT(); } break;
    case 74: in->s_held = PSP_CTRL_CIRCLE;
        if (a->sig_flash_t > 3.0f && g_step_t > 0.5f && g_step_t < 3.0f) { shot("signal_captured"); printf("AUTOTEST: signal captured in %.1fs, found mask 0x%08x, %d/8 here\n", g_step_t, (unsigned)a->prog.signals_found, hunt_count_district(&a->world, a->prog.signals_found)); g_step_t = 3.0f; }
        if (g_step_t > 5.0f) { if (!a->prog.signals_found) printf("AUTOTEST: signal capture FAILED\n"); g_step = 6; g_step_t = 0; } break;
    case 6: a->console_return = ST_PLAY; a->console_overlay = 1; a->console_page = 0; a->console_sel = 4; app_set_state(ST_CONSOLE); NEXT(); break;
    case 7: if (g_step_t > 0.5f && g_step_t < 1.5f) in->s_held = PSP_CTRL_LEFT; /* reduce percussion */
        if (g_step_t > 3.0f) { shot("console_mix"); printf("AUTOTEST: percussion volume now %.2f\n", a->prog.music.vol[VOL_PERC]); a->console_page = 1; NEXT(); } break;
    case 8: if (g_step_t > 1.0f) { shot("console_shape"); a->console_page = 3; NEXT(); } break;
    case 9: if (g_step_t > 1.0f) { shot("console_seeds"); app_set_state(ST_PLAY); a->cam.mode = CAM_COCKPIT; NEXT(); } break;
    case 10: in->s_ly = 0.7f; if (g_step_t > 3.0f) { shot("cockpit"); perf("cockpit"); a->cam.mode = CAM_CINEMATIC; NEXT(); } break;
    case 11: in->s_ly = 0.5f; if (g_step_t > 3.0f) { shot("cinematic"); a->cam.mode = CAM_CHASE; NEXT(); } break;
    case 12: app_start_patrol(5); printf("AUTOTEST: patrol mission 6 started (autopilot)\n"); NEXT(); break;
    case 13:
        if (g_step_t > 4.0f && g_step_t < 4.2f) { shot("patrol_m6"); perf("patrol"); }
        if (a->state == ST_DEBRIEF) { printf("AUTOTEST: mission 6 COMPLETE in %.1fs hull %.0f\n", a->mission.total_time, a->craft.hull); NEXT(); }
        else if (a->state == ST_FAILED) { printf("AUTOTEST: mission 6 FAILED: %s\n", a->mission.fail_reason); NEXT(); }
        else if (g_step_t > 240.0f) { printf("AUTOTEST: mission 6 TIMEOUT step %d\n", a->mission.step); NEXT(); }
        break;
    case 14: if (g_step_t > 2.0f) { shot("debrief"); a->debrief_page = 1; a->state_t = 0; g_step = 55; g_step_t = 0; } break;
    case 55: if (g_step_t > 4.0f) { shot("operator_log"); a->debrief_page = 0; g_step = 15; g_step_t = 0; } break;
    case 15: {
        Progress before = a->prog; char msg[64];
        int okk = save_io_load(&a->prog, msg, sizeof(msg));
        printf("AUTOTEST: save reload %s (%s) missions 0x%03x->0x%03x districts 0x%x signals 0x%08x->0x%08x\n", okk ? "OK" : "FAILED", msg, before.missions_done, a->prog.missions_done, a->prog.districts, (unsigned)before.signals_found, (unsigned)a->prog.signals_found);
        a->prog.drift_district = DISTRICT_WORKS; a->prog.drift_weather = WX_CLEAR; app_start_drift(); NEXT(); break;
    }
    case 16: in->s_ly = 0.8f; in->s_held = PSP_CTRL_LTRIGGER; if (g_step_t > 5.0f) { shot("drift_works"); perf("drift_works"); a->prog.drift_district = DISTRICT_UNDERTOW; a->prog.drift_weather = WX_FOG; app_start_drift(); NEXT(); } break;
    case 17: in->s_ly = 0.8f; if (g_step_t > 5.0f) { shot("drift_undertow"); perf("drift_undertow"); a->prog.drift_district = DISTRICT_CIVIC; a->prog.drift_weather = WX_LIGHT_RAIN; a->prog.drift_tod = TOD_TWILIGHT; app_start_drift(); NEXT(); } break;
    case 18: in->s_ly = 0.9f; in->s_held = PSP_CTRL_TRIANGLE; if (g_step_t > 6.0f) { shot("drift_civic_twilight"); perf("drift_civic"); a->console_return = ST_MENU; a->console_overlay = 0; app_set_state(ST_CONSOLE); NEXT(); } break;
    case 19: if (g_step_t > 3.0f) { shot("console_standalone"); a->mission_id = 0; app_set_state(ST_BRIEFING); NEXT(); } break;
    case 20: if (g_step_t > 4.0f) { shot("briefing"); start_prologue(ST_CASEFILE); g_step = 60; g_step_t = 0; } break;
    case 60: if (g_step_t > 2.5f) { shot("prologue"); a->prologue_card = 3; a->state_t = 0; NEXT(); } break;
    case 61: if (g_step_t > 2.5f) { shot("prologue_card4"); a->prologue_card = PROLOGUE_CARDS - 1; a->state_t = 99; NEXT(); } break;
    case 62: if (a->state == ST_CASEFILE && g_step_t > 1.0f) { a->menu_sel = 1; NEXT(); } break;
    case 63: if (g_step_t > 1.0f) { shot("casefile"); a->menu_sel = 2 + __builtin_popcount(a->prog.missions_done) - 1; NEXT(); } break;
    case 64: if (g_step_t > 1.0f) { shot("casefile_intercepts"); printf("AUTOTEST: story_flags=%d\n", a->prog.story_flags);
        printf("AUTOTEST: done, %d frames, audio blocks %u late %u\n", gfx_frame_count, audio_out_blocks(), audio_out_underruns()); a->quit = 1; } break;
    }
}
#endif
#endif

/* ---------------- main loop ---------------- */

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    uint32_t fcr_boot = pspFpuGetFCR31();
    /* the PSP FPU traps invalid/divide/overflow by default; PPSSPP does not. Never let a stray NaN halt the console. */
    pspFpuSetEnable(0);
    pspFpuSetFS(1);
    int th = sceKernelCreateThread("hush_cb", cb_thread, 0x11, 0xFA0, 0, 0);
    if (th >= 0) sceKernelStartThread(th, 0, 0);
    scePowerSetClockFrequency(333, 333, 166);

    App *a = &g_app;
    memset(a, 0, sizeof(*a));
    a->world_district = -1;
    a->hud_override = -1;
#ifdef HUSH_AUTOTEST
    a->autotest = HUSH_AUTOTEST;
#endif
    diag_init();
    diag_log("HUSH %s boot: fcr31 %08x -> %08x, free %d KB (largest %d KB)", HUSH_BUILD, (unsigned)fcr_boot, (unsigned)pspFpuGetFCR31(), (int)(sceKernelTotalFreeMemSize() / 1024), (int)(sceKernelMaxFreeMemSize() / 1024));
    gfx_init();
    loading_screen("HUSH");
    input_init(&a->in);
    textures_init();
    moth_init();
    char msg[64];
    int loaded = save_io_load(&a->prog, msg, sizeof(msg));
    printf("HUSH: boot, save: %s\n", msg);
    diag_log("save: %s; vram used %d KB of %d KB; story %d drift d%d w%d t%d detail %d ctl %d signals %08x", msg, gfx_vram_used() / 1024, (int)(sceGeEdramGetSize() / 1024), a->prog.story_flags, a->prog.drift_district, a->prog.drift_weather, a->prog.drift_tod, a->prog.detail, a->prog.control_scheme, (unsigned)a->prog.signals_found);
    (void)loaded;
    a->music = hush_music_instance();
#ifdef HUSH_AUTOTEST
    {   /* cross-platform determinism: same settings as `hush-render --rate 22050 --seconds 60` */
        MusicParams p; hush_preset_get(PRESET_PATROL, &p);
        MusicEnv e; memset(&e, 0, sizeof(e));
        e.active = 1; e.district = DISTRICT_CIVIC; e.altitude = 0.5f; e.speed = 0.25f; e.fog = 0.3f; e.traffic = 0.3f; e.canyon = 0.3f; e.landmark = -1;
        hush_music_init(a->music, 22050.0f, &p);
        hush_music_set_env(a->music, &e);
        hush_event_trace = trace_ev;
        static float buf[2 * 1024];
        uint64_t tb = gfx_ticks_us();
        for (int i = 0; i < 60 * 22050 / 1024 + 1; i++) {
            int n = (i == 60 * 22050 / 1024) ? (60 * 22050) % 1024 : 1024;
            if (n) hush_music_render(a->music, buf, n);
        }
        printf("AUTOTEST: determinism seed=%u events=%u hash=%08x (offline render of 60s took %.0f ms emulated)\n",
               p.seed, hush_music_event_count(a->music), hush_music_event_hash(a->music), (float)(gfx_ticks_us() - tb) / 1000.0f);
        hush_event_trace = 0;
        for (int d = 0; d < DISTRICT_COUNT; d++) { world_generate(&a->world, d); printf("AUTOTEST: world %d fingerprint %08x boxes %d\n", d, world_fingerprint(&a->world), a->world.n_boxes); }
    }
#endif
    uint64_t t0 = gfx_ticks_us();
    hush_music_init(a->music, (float)HUSH_ENGINE_RATE, &a->prog.music);
    printf("HUSH: music engine ready in %.1f ms (seed %u)\n", (float)(gfx_ticks_us() - t0) / 1000.0f, a->prog.music.seed);
    sfx_init(&a->sfx, (float)HUSH_ENGINE_RATE);
    int aud_ok = audio_out_start(a->music, &a->sfx);
    if (!aud_ok) printf("HUSH: audio output FAILED to start\n");
    diag_log("music engine ready (seed %u), audio %s, free %d KB", a->prog.music.seed, aud_ok ? "started" : "FAILED", (int)(sceKernelTotalFreeMemSize() / 1024));
    app_push_music();
    app_start_attract();
    app_set_state(ST_TITLE);

    uint64_t last = gfx_ticks_us();
    float play_acc = 0;
    while (!g_exit_req && !a->quit) {
        uint64_t now = gfx_ticks_us();
        float dt = h_clampf((float)(now - last) / 1e6f, 0.001f, 0.1f);
        last = now;
        a->time += dt;
        a->state_t += dt;
        if (a->note_t > 0) a->note_t -= dt;
        a->fps_acc += dt; a->fps_frames++;
        /* adaptive detail: drop quality when frames run long on this hardware, recover when stable */
        a->frame_avg = a->frame_avg * 0.92f + dt * 0.08f;
        if (a->prog.detail != DETAIL_AUTO) rw_set_quality(a->prog.detail == DETAIL_HIGH ? 3 : (a->prog.detail == DETAIL_MEDIUM ? 2 : 0));
        else if (a->state == ST_PLAY || a->mode == MODE_ATTRACT) {
            if (a->frame_avg > 0.0395f) { a->slow_t += dt; a->fast_t = 0; } else a->slow_t = 0;
            if (a->frame_avg < 0.0350f) a->fast_t += dt; else a->fast_t = 0;
            if (a->slow_t > 1.0f && rw_quality() > 0) { rw_set_quality(rw_quality() - 1); a->slow_t = 0; printf("HUSH: detail lowered to %d (frame %.1f ms)\n", rw_quality(), a->frame_avg * 1000.0f); }
            if (a->fast_t > 10.0f && rw_quality() < 3) { rw_set_quality(rw_quality() + 1); a->fast_t = 0; }
        }
        if (a->fps_acc >= 1.0f) { a->fps = (float)a->fps_frames / a->fps_acc; a->fps_acc = 0; a->fps_frames = 0; }
        diag_frame();
        {   /* once a second: the numbers that matter on real hardware */
            static float acc;
            acc += dt;
            if (acc >= 1.0f) {
                acc = 0;
                diag_log("st %d mode %d d%d fps %.1f frame %.1f ms cpu %.1f gpu %.1f aud %.0f%% late %u/%u q%d list %dKB tris %d", (int)a->state, a->mode, a->world_district, a->fps, a->frame_avg * 1000.0f, gfx_last_cpu_ms, gfx_last_gpu_wait_ms, audio_out_load() * 100.0f, audio_out_underruns(), audio_out_blocks(), rw_quality(), gfx_list_last / 1024, rw_stats()->tris_drawn);
            }
        }
        diag_mark("input");
        input_update(&a->in, dt);
#ifdef HUSH_AUTOTEST
        autotest_tick(dt);
        input_update(&a->in, 0);   /* re-sample with the scripted state */
#endif
        diag_mark("update");
        AppState before = a->state;
        int world_visible = a->world_district >= 0;
        switch (a->state) {
        case ST_PLAY:
            if (a->in.pressed & PSP_CTRL_START) { sfx_play(&a->sfx, SFX_UI_SELECT); a->menu_sel = 0; app_set_state(ST_PAUSE); break; }
            {
                int steps = (a->autotest && a->mode == MODE_PATROL) ? (a->autotest == 2 ? 8 : 4) : 1;   /* validation build fast-forwards missions */
                for (int i = 0; i < steps; i++) game_update(dt, 1);
            }
            play_acc += dt;
            break;
        case ST_CONSOLE:
            console_update(dt);
            if (a->console_overlay) game_update(dt, 0);   /* the city keeps living behind the console */
            else world_visible = 0;
            break;
        case ST_PAUSE: case ST_FAILED: case ST_DEBRIEF:
            menus_update(dt);
            break;
        default:
            if (a->mode == MODE_ATTRACT) game_update(dt, 0);
            menus_update(dt);
            break;
        }
        if (a->state == ST_DEBRIEF && before != ST_DEBRIEF) on_debrief_enter();
        if (play_acc >= 1.0f) { a->prog.play_seconds += (uint32_t)play_acc; play_acc -= (float)(int)play_acc; }

        diag_mark("render");
        gfx_begin(RGBA(4, 6, 9, 255));
        if (world_visible && a->state != ST_CREDITS) game_render(a->state == ST_PLAY || (a->state == ST_CONSOLE && 0));
        else { gfx_mode_2d(); gfx_rect_grad(0, 0, SCR_W, SCR_H, RGBA(8, 10, 16, 255), RGBA(2, 3, 5, 255)); }
        gfx_mode_2d();
        diag_mark("2d");
        if (a->state == ST_CONSOLE) console_render();
        else if (a->state != ST_PLAY) menus_render();
        else if (a->note_t > 0) gfx_text(FONT_SMALL, SCR_W * 0.5f, 52, a->note, RGBA(255, 186, 90, (int)(255 * h_clampf(a->note_t, 0, 1))), ALIGN_CENTER);
        diag_mark("end");
        gfx_end(30);
        diag_mark("frame done");
    }
    audio_out_stop();
    gfx_term();
    printf("HUSH: clean exit\n");
    sceKernelExitGame();
    return 0;
}
