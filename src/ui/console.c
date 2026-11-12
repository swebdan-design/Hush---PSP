/* HUSH — SIGNAL CONSOLE: the onboard generative synthesizer interface.
 * Every control writes straight into the engine's parameter mailbox; the
 * engine smooths the change, so the composition evolves without restarting.
 * All displays are fed by the audio thread's real output and state. */
#include "app.h"
#include "../audio/dsp.h"
#include <stdio.h>
#include <string.h>

#define C_TXT RGBA(214, 222, 230, 255)
#define C_DIM RGBA(120, 136, 150, 220)
#define C_SEL RGBA(255, 186, 90, 255)
#define C_ACC RGBA(120, 220, 240, 255)
#define C_GRN RGBA(120, 255, 170, 255)

enum { K_F01, K_TEMPO, K_KEY, K_MODE, K_WAVE, K_ROOT, K_SEED, K_PROFILE, K_ACTION, K_SEEDSLOT, K_PRESET, K_JOURNAL };
typedef struct { const char *label; int kind; int index; } Row;

static int g_profile = DISTRICT_STACKS;
static int g_digit = -1;     /* seed digit edit cursor (-1 = off) */

static float *f01(int idx) {
    MusicParams *m = &g_app.prog.music;
    switch (idx) {
    case 0: return &m->master;
    case 1: case 2: case 3: case 4: case 5: case 6: return &m->vol[idx - 1];
    case 7: return &m->delay;
    case 8: return &m->reverb;
    case 9: return &m->density;
    case 10: return &m->darkness;
    case 11: return &m->motion;
    case 12: return &m->filter;
    case 13: return &m->detune;
    case 14: return &m->cutoff;
    case 15: return &m->lfo_rate;
    case 16: return &m->complexity;
    case 17: return &m->rhythm_prob;
    case 18: return &m->seq_variation;
    case 19: return &m->melody_prob;
    case 20: return &m->delay_fb;
    default: return &m->mod_depth;
    }
}

static const Row page_mix[] = { {"MASTER", K_F01, 0}, {"DRONE", K_F01, 1}, {"HARMONY", K_F01, 2}, {"BASS", K_F01, 3}, {"PERCUSSION", K_F01, 4},
                                 {"FRAGMENTS", K_F01, 5}, {"TEXTURE", K_F01, 6}, {"DELAY", K_F01, 7}, {"AMBIENCE", K_F01, 8} };
static const Row page_shape[] = { {"DENSITY", K_F01, 9}, {"DARKNESS", K_F01, 10}, {"MOTION", K_F01, 11}, {"TEMPO", K_TEMPO, 0}, {"KEY", K_KEY, 0},
                                   {"MODE", K_MODE, 0}, {"FILTER", K_F01, 12}, {"PROFILE", K_PROFILE, 0} };
static const Row page_adv[] = { {"PAD WAVE", K_WAVE, 0}, {"DETUNE", K_F01, 13}, {"DRONE ROOT", K_ROOT, 0}, {"CUTOFF", K_F01, 14}, {"LFO RATE", K_F01, 15},
                                 {"HARMONIC COMPLEX.", K_F01, 16}, {"RHYTHM PROB.", K_F01, 17}, {"SEQ. VARIATION", K_F01, 18}, {"MELODY PROB.", K_F01, 19},
                                 {"DELAY FEEDBACK", K_F01, 20}, {"MOD DEPTH", K_F01, 21} };
static Row page_seed[1 + 1 + SAVED_SEEDS + PRESET_BUILTIN_COUNT + USER_PRESETS];
static int n_seed_rows;
static const Row page_journal[] = { {"", K_JOURNAL, 0}, {"", K_JOURNAL, 1}, {"", K_JOURNAL, 2}, {"", K_JOURNAL, 3}, {"", K_JOURNAL, 4} };
static const char *const page_names[5] = { "MIX", "SHAPE", "ADVANCED", "SEEDS & PRESETS", "JOURNAL" };

static void build_seed_page(void) {
    int n = 0;
    page_seed[n].label = "SEED"; page_seed[n].kind = K_SEED; page_seed[n++].index = 0;
    page_seed[n].label = "RANDOMISE SEED"; page_seed[n].kind = K_ACTION; page_seed[n++].index = 0;
    for (int i = 0; i < SAVED_SEEDS; i++) { page_seed[n].label = "SEED SLOT"; page_seed[n].kind = K_SEEDSLOT; page_seed[n++].index = i; }
    for (int i = 0; i < PRESET_BUILTIN_COUNT + USER_PRESETS; i++) { page_seed[n].label = "PRESET"; page_seed[n].kind = K_PRESET; page_seed[n++].index = i; }
    n_seed_rows = n;
}

static const Row *page_rows(int p, int *n) {
    switch (p) {
    case 0: *n = (int)(sizeof(page_mix) / sizeof(Row)); return page_mix;
    case 1: *n = (int)(sizeof(page_shape) / sizeof(Row)); return page_shape;
    case 2: *n = (int)(sizeof(page_adv) / sizeof(Row)); return page_adv;
    case 3: *n = n_seed_rows; return page_seed;
    default: *n = 5; return page_journal;
    }
}

static const char *const lm_desc[LM_COUNT] = {
    "Stacks. Slow distant voices; the pads turn to VOX and the percussion thins.",
    "Works. Mechanical timing patterns of clicks and metal lock to the beat.",
    "Civic Centre. A remembered tune returns and erodes a little with every repeat.",
    "Undertow. A sub-octave drone and long metallic resonance under everything.",
    "Undertow. Layers fall away one by one until almost nothing is left." };

static void push(void) { app_push_music(); }

static int wave_ok(int w) { return w == -1 || (w >= 0 && w < W_COUNT && w != W_NOISE && w != W_FM && (g_app.prog.waves & (1u << w))); }

static void adjust(const Row *r, int d, int fast) {
    MusicParams *m = &g_app.prog.music;
    switch (r->kind) {
    case K_F01: { float *v = f01(r->index); *v = h_clampf(*v + (float)d * (fast ? 0.05f : 0.02f), 0.0f, 1.0f); break; }
    case K_TEMPO: m->tempo = h_clampf(m->tempo + (float)d * (fast ? 4.0f : 1.0f), 50.0f, 140.0f); break;
    case K_KEY: m->key = (m->key + 12 + d) % 12; break;
    case K_MODE: m->mode = ((m->mode + 1 + d + MODE_COUNT + 1) % (MODE_COUNT + 1)) - 1; break;
    case K_WAVE: { int w = m->pad_wave; for (int i = 0; i < W_COUNT + 1; i++) { w += d; if (w < -1) w = W_COUNT - 1; if (w >= W_COUNT) w = -1; if (wave_ok(w)) break; } m->pad_wave = w; break; }
    case K_ROOT: m->drone_root = h_clampi(m->drone_root + d, -7, 7); break;
    case K_SEED:
        if (g_digit >= 0) {
            uint32_t p10 = 1; for (int i = 0; i < g_digit; i++) p10 *= 10;
            uint32_t digit = (m->seed / p10) % 10;
            uint32_t nd = (uint32_t)(((int)digit + d + 10) % 10);
            m->seed = m->seed - digit * p10 + nd * p10;
            if (m->seed > 99999999u) m->seed %= 100000000u;
        } else m->seed = (uint32_t)((int64_t)m->seed + d * (fast ? 37 : 1));
        break;
    case K_PROFILE: g_profile = (g_profile + DISTRICT_COUNT + d) % DISTRICT_COUNT; break;
    default: return;
    }
    push();
}

static int preset_available(int i) {
    if (i < PRESET_BUILTIN_COUNT) return (g_app.prog.presets >> i) & 1;
    return 1;
}

static void activate(const Row *r) {
    App *a = &g_app;
    Progress *p = &a->prog;
    switch (r->kind) {
    case K_SEED: g_digit = g_digit >= 0 ? -1 : 0; break;
    case K_ACTION: p->music.seed = (uint32_t)(gfx_ticks_us() * 2654435761u ^ (uint32_t)a->time * 7919u) % 100000000u; push(); app_note("NEW SEED - THE MUSIC WILL TURN AT THE NEXT BAR"); break;
    case K_SEEDSLOT:
        if (r->index < p->n_seeds) { p->music.seed = p->seeds[r->index]; push(); app_note("SEED RECALLED"); }
        else app_note("EMPTY SLOT - TRIANGLE STORES THE CURRENT SEED");
        break;
    case K_PRESET: {
        if (!preset_available(r->index)) { app_note("LOCKED - FOUND ON PATROL"); sfx_play(&a->sfx, SFX_FAIL); return; }
        if (r->index < PRESET_BUILTIN_COUNT) {
            MusicParams np; hush_preset_get(r->index, &np);
            np.master = p->music.master;
            p->music = np;
        } else {
            int u = r->index - PRESET_BUILTIN_COUNT;
            if (!p->user_used[u]) { app_note("EMPTY - TRIANGLE SAVES THE CURRENT SOUND HERE"); return; }
            p->music = p->user[u];
        }
        push();
        app_note("PRESET LOADED - TRANSITIONING");
        break;
    }
    default: break;
    }
}

static void store(const Row *r) {
    App *a = &g_app;
    Progress *p = &a->prog;
    if (r->kind == K_SEEDSLOT) {
        p->seeds[r->index] = p->music.seed;
        if (r->index >= p->n_seeds) p->n_seeds = (uint8_t)(r->index + 1);
        app_save();
        app_note("SEED STORED");
    } else if (r->kind == K_PRESET && r->index >= PRESET_BUILTIN_COUNT) {
        int u = r->index - PRESET_BUILTIN_COUNT;
        p->user[u] = p->music; p->user_used[u] = 1;
        app_save();
        app_note("USER PRESET SAVED");
    }
}

void console_update(float dt) {
    App *a = &g_app;
    (void)dt;
    if (!n_seed_rows) build_seed_page();
    if (a->in.pressed & (PSP_CTRL_START | PSP_CTRL_CIRCLE)) {
        if (g_digit >= 0 && (a->in.pressed & PSP_CTRL_CIRCLE)) { g_digit = -1; return; }
        sfx_play(&a->sfx, SFX_UI_BACK);
        app_save();
        a->menu_sel = a->console_return == ST_MENU ? 2 : 0;
        app_set_state(a->console_return == ST_PLAY ? ST_PLAY : a->console_return);
        return;
    }
    if (a->in.pressed & PSP_CTRL_RTRIGGER) { a->console_page = (a->console_page + 1) % 5; a->console_sel = 0; g_digit = -1; sfx_play(&a->sfx, SFX_UI_MOVE); }
    if (a->in.pressed & PSP_CTRL_LTRIGGER) { a->console_page = (a->console_page + 4) % 5; a->console_sel = 0; g_digit = -1; sfx_play(&a->sfx, SFX_UI_MOVE); }
    int n;
    const Row *rows = page_rows(a->console_page, &n);
    const Row *r = &rows[h_clampi(a->console_sel, 0, n - 1)];
    if (g_digit >= 0 && r->kind == K_SEED) {
        if (input_rep(&a->in, PSP_CTRL_LEFT)) { g_digit = h_mini(g_digit + 1, 7); sfx_play(&a->sfx, SFX_UI_MOVE); }
        if (input_rep(&a->in, PSP_CTRL_RIGHT)) { g_digit = h_maxi(g_digit - 1, 0); sfx_play(&a->sfx, SFX_UI_MOVE); }
        if (input_rep(&a->in, PSP_CTRL_UP)) { adjust(r, 1, 0); sfx_play(&a->sfx, SFX_UI_MOVE); }
        if (input_rep(&a->in, PSP_CTRL_DOWN)) { adjust(r, -1, 0); sfx_play(&a->sfx, SFX_UI_MOVE); }
        if (a->in.pressed & PSP_CTRL_CROSS) { g_digit = -1; sfx_play(&a->sfx, SFX_UI_SELECT); }
        return;
    }
    int moved = 0;
    if (input_rep(&a->in, PSP_CTRL_UP)) { a->console_sel = (a->console_sel + n - 1) % n; moved = 1; }
    if (input_rep(&a->in, PSP_CTRL_DOWN)) { a->console_sel = (a->console_sel + 1) % n; moved = 1; }
    if (moved) sfx_play(&a->sfx, SFX_UI_MOVE);
    int fast = (a->in.hold_time[__builtin_ctz(PSP_CTRL_LEFT)] > 1.2f) || (a->in.hold_time[__builtin_ctz(PSP_CTRL_RIGHT)] > 1.2f);
    if (input_rep(&a->in, PSP_CTRL_LEFT)) { adjust(r, -1, fast); sfx_play(&a->sfx, SFX_UI_MOVE); }
    if (input_rep(&a->in, PSP_CTRL_RIGHT)) { adjust(r, 1, fast); sfx_play(&a->sfx, SFX_UI_MOVE); }
    if (a->in.pressed & PSP_CTRL_CROSS) { sfx_play(&a->sfx, SFX_UI_SELECT); activate(r); }
    if (a->in.pressed & PSP_CTRL_TRIANGLE) { sfx_play(&a->sfx, SFX_UI_SELECT); store(r); }
    if (a->in.pressed & PSP_CTRL_SQUARE) { const Row rnd = {"", K_ACTION, 0}; sfx_play(&a->sfx, SFX_UI_SELECT); activate(&rnd); }
    /* standalone console: no world, the chosen district profile colours the sound */
    if (!a->console_overlay) {
        MusicEnv e;
        memset(&e, 0, sizeof(e));
        e.active = 0; e.district = g_profile; e.landmark = -1;
        hush_music_set_env(a->music, &e);
    }
}

static void row_value(const Row *r, char *buf, int len, float *barv) {
    MusicParams *m = &g_app.prog.music;
    Progress *p = &g_app.prog;
    *barv = -1;
    switch (r->kind) {
    case K_F01: *barv = *f01(r->index); snprintf(buf, (size_t)len, "%3d", (int)(*barv * 100.0f + 0.5f)); break;
    case K_TEMPO: snprintf(buf, (size_t)len, "%.0f BPM", m->tempo); *barv = (m->tempo - 50.0f) / 90.0f; break;
    case K_KEY: snprintf(buf, (size_t)len, "%s", hush_note_names[m->key]); break;
    case K_MODE: snprintf(buf, (size_t)len, "%s", m->mode < 0 ? "DISTRICT" : hush_mode_names[m->mode]); break;
    case K_WAVE: snprintf(buf, (size_t)len, "%s", m->pad_wave < 0 ? "DISTRICT" : hush_wave_names[m->pad_wave]); break;
    case K_ROOT: snprintf(buf, (size_t)len, "%+d st", m->drone_root); break;
    case K_SEED: snprintf(buf, (size_t)len, "%08u", m->seed); break;
    case K_PROFILE: snprintf(buf, (size_t)len, "%s", g_app.console_overlay ? "(LIVE CITY)" : hush_district_names[g_profile]); break;
    case K_SEEDSLOT: if (r->index < p->n_seeds) snprintf(buf, (size_t)len, "%d: %08u", r->index + 1, p->seeds[r->index]); else snprintf(buf, (size_t)len, "%d: --------", r->index + 1); break;
    case K_PRESET:
        if (r->index < PRESET_BUILTIN_COUNT) snprintf(buf, (size_t)len, "%s", preset_available(r->index) ? hush_preset_names[r->index] : "LOCKED");
        else snprintf(buf, (size_t)len, "USER %d%s", r->index - PRESET_BUILTIN_COUNT + 1, p->user_used[r->index - PRESET_BUILTIN_COUNT] ? "" : " (EMPTY)");
        break;
    default: buf[0] = 0; break;
    }
}

static void viz_panel(float x, float y) {
    App *a = &g_app;
    const MusicViz *z = hush_music_viz(a->music);
    char buf[64];
    /* oscilloscope (both channels) */
    float w = 168, h = 54;
    gfx_rect(x, y, w, h, RGBA(3, 12, 9, 235));
    for (int i = 1; i < 4; i++) gfx_line(x + w * (float)i / 4.0f, y, x + w * (float)i / 4.0f, y + h, RGBA(40, 90, 70, 60));
    gfx_line(x, y + h * 0.5f, x + w, y + h * 0.5f, RGBA(40, 90, 70, 90));
    float pl[2 * 96], pr[2 * 96];
    for (int i = 0; i < 96; i++) {
        int k = (i * VIZ_SCOPE) / 96;
        pl[2 * i] = pr[2 * i] = x + (float)i * w / 95.0f;
        pl[2 * i + 1] = y + h * 0.5f - h_clampf(z->scopeL[k] * 2.2f, -1, 1) * h * 0.46f;
        pr[2 * i + 1] = y + h * 0.5f - h_clampf(z->scopeR[k] * 2.2f, -1, 1) * h * 0.46f;
    }
    gfx_lines(pr, 96, RGBA(120, 200, 255, 140), 0);
    gfx_lines(pl, 96, C_GRN, 0);
    gfx_rect_outline(x, y, w, h, RGBA(70, 110, 100, 255));
    /* stereo peak/rms meters */
    float mx = x + w + 6;
    for (int ch = 0; ch < 2; ch++) {
        float pk = ch ? z->peakR : z->peakL, rm = ch ? z->rmsR : z->rmsL;
        float db = 20.0f * log10f(pk + 1e-5f), rdb = 20.0f * log10f(rm + 1e-5f);
        float fp = h_clampf((db + 48.0f) / 48.0f, 0, 1), fr = h_clampf((rdb + 48.0f) / 48.0f, 0, 1);
        float bx = mx + (float)ch * 9;
        gfx_rect(bx, y, 6, h, RGBA(255, 255, 255, 20));
        gfx_rect(bx, y + h * (1 - fr), 6, h * fr, RGBA(90, 200, 160, 220));
        gfx_rect(bx, y + h * (1 - fp), 6, 2, fp > 0.95f ? RGBA(255, 80, 70, 255) : C_SEL);
    }
    /* voice activity: tone voices coloured by layer pool, then percussion */
    float vy = y + h + 8;
    gfx_text(FONT_SMALL, x, vy, "VOICES", C_DIM, ALIGN_LEFT);
    for (int i = 0; i < 25; i++) {
        uint32_t c = i < 4 ? RGBA(120, 160, 255, 255) : i < 14 ? RGBA(200, 140, 255, 255) : i < 18 ? RGBA(255, 180, 90, 255) : i < 22 ? RGBA(120, 255, 200, 255) : RGBA(170, 170, 170, 255);
        int on = (z->voice_mask >> i) & 1;
        gfx_rect(x + 48 + (float)(i % 13) * 9, vy + (float)(i / 13) * 7, 7, 5, on ? c : RGBA(255, 255, 255, 25));
    }
    for (int i = 0; i < 8; i++) gfx_rect(x + 48 + 13 * 9 + 4 + (float)(i % 4) * 7, vy + (float)(i / 4) * 7, 5, 5, ((z->drum_mask >> i) & 1) ? RGBA(255, 110, 90, 255) : RGBA(255, 255, 255, 25));
    /* layers */
    float ly = vy + 18;
    static const char *const ln[LAYER_COUNT] = { "ATM", "HRM", "PLS", "PRC", "FRG", "DCY" };
    for (int l = 0; l < LAYER_COUNT; l++) {
        float lx = x + (float)l * 31;
        gfx_text(FONT_SMALL, lx, ly, ln[l], C_DIM, ALIGN_LEFT);
        gfx_rect(lx, ly + 12, 24, 3, RGBA(255, 255, 255, 25));
        gfx_rect(lx, ly + 12, 24 * h_clampf(z->layer_gain[l], 0, 1), 3, C_ACC);
        gfx_rect(lx, ly + 16, 24 * h_clampf(z->layer_level[l] * 6.0f, 0, 1), 2, C_GRN);
    }
    /* state */
    float sy = ly + 24;
    snprintf(buf, sizeof(buf), "%s  (%d bars)", hush_section_names[z->section], z->section_bars_left);
    gfx_text(FONT_SMALL, x, sy, buf, C_SEL, ALIGN_LEFT);
    snprintf(buf, sizeof(buf), "CHORD %-8s KEY %s %s", z->chord_name, hush_note_names[z->key_center], hush_mode_names[z->mode]);
    gfx_text(FONT_SMALL, x, sy + 11, buf, C_TXT, ALIGN_LEFT);
    snprintf(buf, sizeof(buf), "%5.1f BPM  BAR %d.%02d  DECAY %2d%%", z->tempo, z->bar, z->step + 1, (int)(z->erosion * 100));
    gfx_text(FONT_SMALL, x, sy + 22, buf, C_TXT, ALIGN_LEFT);
    snprintf(buf, sizeof(buf), "SEED %08u  EVENTS %u", z->seed, z->event_count);
    gfx_text(FONT_SMALL, x, sy + 33, buf, C_DIM, ALIGN_LEFT);
    /* recent events ticker */
    float ey = sy + 46;
    static const char *const abbr[LAYER_COUNT] = { "ATM", "HRM", "PLS", "PRC", "FRG", "DCY" };
    for (int k = 0; k < 4; k++) {
        const VizEvent *e = &z->events[(z->event_head - 1 - k + VIZ_EVENTS * 2) % VIZ_EVENTS];
        if (e->vel == 0 && e->note == 0) continue;
        if (e->layer == LAYER_PERC) snprintf(buf, sizeof(buf), "%s %s", abbr[e->layer], hush_drum_names[e->note % DR_COUNT]);
        else snprintf(buf, sizeof(buf), "%s %s%d", abbr[e->layer], hush_note_names[((e->note % 12) + 12) % 12], e->note / 12 - 1);
        gfx_text(FONT_SMALL, x + (float)(k % 2) * 86, ey + (float)(k / 2) * 11, buf, col_alpha(C_TXT, 1.0f - 0.2f * (float)k), ALIGN_LEFT);
    }
}

void console_render(void) {
    App *a = &g_app;
    if (!n_seed_rows) build_seed_page();
    gfx_rect(0, 0, SCR_W, SCR_H, RGBA(0, 0, 0, a->console_overlay ? 150 : 205));
    /* header with page tabs */
    gfx_rect(0, 0, SCR_W, 30, RGBA(10, 12, 14, 245));
    gfx_text(FONT_LARGE, 10, 6, "SIGNAL CONSOLE", C_TXT, ALIGN_LEFT);
    float tx = 190;
    for (int p = 0; p < 5; p++) {
        const char *nm = p == 3 ? "SEEDS" : page_names[p];
        float wdt = gfx_text_width(FONT_SMALL, nm) + 10;
        int sel = p == a->console_page;
        gfx_rect(tx, 8, wdt, 15, sel ? RGBA(255, 186, 90, 50) : RGBA(255, 255, 255, 12));
        gfx_text(FONT_SMALL, tx + 5, 10, nm, sel ? C_SEL : C_DIM, ALIGN_LEFT);
        tx += wdt + 4;
    }
    gfx_line(0, 30, SCR_W, 30, RGBA(90, 100, 110, 200));
    /* the equipment body */
    gfx_rect(6, 36, 250, 212, RGBA(16, 18, 21, 235));
    gfx_rect_outline(6, 36, 250, 212, RGBA(70, 80, 90, 255));
    for (int i = 0; i < 4; i++) { float sx = i & 1 ? 250.0f : 10.0f, sy = i & 2 ? 242.0f : 40.0f; gfx_rect(sx, sy, 3, 3, RGBA(80, 90, 100, 255)); }
    gfx_rect(262, 36, 212, 212, RGBA(10, 13, 15, 235));
    gfx_rect_outline(262, 36, 212, 212, RGBA(70, 80, 90, 255));
    int n;
    const Row *rows = page_rows(a->console_page, &n);
    if (a->console_page == 4) {
        for (int i = 0; i < LM_COUNT; i++) {
            int found = (a->prog.landmarks >> i) & 1;
            float y = 44 + (float)i * 40;
            gfx_text(FONT_SMALL, 16, y, found ? hush_landmark_names[i] : "UNIDENTIFIED TRANSMISSION", found ? RGBA(220, 170, 255, 255) : C_DIM, ALIGN_LEFT);
            gfx_text_wrap(FONT_SMALL, 16, y + 12, 232, found ? lm_desc[i] : "Hold TRIANGLE near it to identify.", found ? C_TXT : RGBA(90, 98, 108, 220), 2, -1);
        }
    } else {
        int top = h_clampi(a->console_sel - 6, 0, h_maxi(0, n - 13));
        for (int k = 0; k < 13 && top + k < n; k++) {
            int i = top + k;
            const Row *r = &rows[i];
            float y = 42 + (float)k * 15.5f;
            int sel = i == a->console_sel;
            if (sel) gfx_rect(8, y - 2, 246, 14, RGBA(255, 186, 90, 34));
            char val[32];
            float bv;
            row_value(r, val, sizeof(val), &bv);
            int locked = r->kind == K_PRESET && !preset_available(r->index);
            gfx_text(FONT_SMALL, 14, y, r->label, sel ? C_SEL : (locked ? RGBA(90, 98, 108, 220) : C_TXT), ALIGN_LEFT);
            if (bv >= 0) {
                gfx_rect(140, y + 3, 72, 5, RGBA(255, 255, 255, 25));
                gfx_rect(140, y + 3, 72 * bv, 5, sel ? C_SEL : C_ACC);
                gfx_text(FONT_SMALL, 248, y, val, sel ? C_SEL : C_TXT, ALIGN_RIGHT);
            } else {
                gfx_text(FONT_SMALL, 248, y, val, sel ? C_SEL : (locked ? RGBA(90, 98, 108, 220) : C_ACC), ALIGN_RIGHT);
                if (r->kind == K_SEED && g_digit >= 0) {
                    float cw = gfx_text_width(FONT_SMALL, "0");
                    gfx_rect(248 - cw * (float)(g_digit + 1), y + 11, cw - 1, 2, C_SEL);
                }
            }
        }
        if (a->console_page == 2 && rows[a->console_sel].kind == K_WAVE) {
            /* waveform preview of the selected pad wave */
            int wv = a->prog.music.pad_wave < 0 ? W_SAW : a->prog.music.pad_wave;
            const float *t = dsp_table(wv, 110.0f);
            float pts[2 * 48];
            for (int i = 0; i < 48; i++) { pts[2 * i] = 150 + (float)i * 2; pts[2 * i + 1] = 228 - t[(i * WT_SIZE) / 48] * 10.0f; }
            gfx_lines(pts, 48, C_GRN, 0);
        }
    }
    viz_panel(268, 42);
    if (a->note_t > 0) { gfx_rect(0, SCR_H - 34, SCR_W, 15, RGBA(0, 0, 0, 200)); gfx_text(FONT_SMALL, SCR_W * 0.5f, SCR_H - 32, a->note, C_ACC, ALIGN_CENTER); }
    if (a->console_page == 3) ui_footer("L:PAGE R:PAGE X:LOAD T:STORE S:RANDOM O:CLOSE");
    else ui_footer("L:PAGE R:PAGE S:RANDOM-SEED O:CLOSE");
}
