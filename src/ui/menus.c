/* HUSH — front end: title, menus, mission flow screens, options, credits. */
#include "app.h"
#include "../platform/psp/save_io.h"
#include <stdio.h>
#include <string.h>

#define C_TXT RGBA(222, 228, 236, 255)
#define C_DIM RGBA(130, 145, 160, 220)
#define C_SEL RGBA(255, 186, 90, 255)
#define C_ACC RGBA(120, 220, 240, 255)
#define C_VIO RGBA(200, 160, 255, 255)

/* ---------------- shared widgets ---------------- */

void ui_panel(float x, float y, float w, float h, float alpha) {
    gfx_rect(x, y, w, h, RGBA(6, 9, 13, (int)(215 * alpha)));
    gfx_rect_outline(x, y, w, h, RGBA(90, 110, 125, (int)(150 * alpha)));
    gfx_rect(x, y, 3, 3, col_alpha(C_SEL, alpha));
    gfx_rect(x + w - 3, y + h - 3, 3, 3, col_alpha(C_SEL, alpha));
}

void ui_header(const char *title, const char *sub) {
    gfx_rect(0, 0, SCR_W, 30, RGBA(4, 6, 9, 220));
    gfx_line(0, 30, SCR_W, 30, RGBA(90, 110, 125, 160));
    gfx_text(FONT_LARGE, 14, 6, title, C_TXT, ALIGN_LEFT);
    if (sub) gfx_text(FONT_SMALL, SCR_W - 14, 11, sub, C_DIM, ALIGN_RIGHT);
}

/* PSP face-button glyphs drawn with primitives */
static float glyph(float x, float y, char b) {
    float cx = x + 5, cy = y + 6;
    switch (b) {
    case 'X': gfx_line(cx - 3, cy - 3, cx + 3, cy + 3, RGBA(140, 180, 255, 255)); gfx_line(cx - 3, cy + 3, cx + 3, cy - 3, RGBA(140, 180, 255, 255)); break;
    case 'O': { float p[16]; for (int i = 0; i < 8; i++) { float a = (float)i * H_TAU / 8; p[2 * i] = cx + cosf(a) * 3.5f; p[2 * i + 1] = cy + sinf(a) * 3.5f; } gfx_lines(p, 8, RGBA(255, 120, 130, 255), 1); break; }
    case 'S': gfx_rect_outline(cx - 3, cy - 3, 6, 6, RGBA(255, 150, 220, 255)); break;
    case 'T': { float p[6] = { cx, cy - 4, cx + 4, cy + 3, cx - 4, cy + 3 }; gfx_lines(p, 3, RGBA(120, 230, 180, 255), 1); break; }
    case 'L': gfx_text(FONT_SMALL, x, y, "L", C_TXT, ALIGN_LEFT); return 8;
    case 'R': gfx_text(FONT_SMALL, x, y, "R", C_TXT, ALIGN_LEFT); return 8;
    default: break;
    }
    return 12;
}

/* hints like "X:SELECT O:BACK" -> glyphs + labels */
void ui_footer(const char *hints) {
    gfx_rect(0, SCR_H - 18, SCR_W, 18, RGBA(4, 6, 9, 220));
    float x = SCR_W - 10;
    /* layout right-to-left: measure first */
    char buf[128];
    snprintf(buf, sizeof(buf), "%s", hints);
    float total = 0;
    char *save = NULL;
    char tmp[128]; snprintf(tmp, sizeof(tmp), "%s", hints);
    for (char *tok = strtok_r(tmp, " ", &save); tok; tok = strtok_r(NULL, " ", &save)) total += 14 + gfx_text_width(FONT_SMALL, tok + 2) + 12;
    x = SCR_W - 10 - total;
    save = NULL;
    for (char *tok = strtok_r(buf, " ", &save); tok; tok = strtok_r(NULL, " ", &save)) {
        x += glyph(x, SCR_H - 15, tok[0]) + 2;
        x += gfx_text(FONT_SMALL, x, SCR_H - 15, tok + 2, C_DIM, ALIGN_LEFT) + 12;
    }
}

static int list_nav(int *sel, int n) {
    App *a = &g_app;
    if (n <= 0) return 0;
    int moved = 0;
    if (input_rep(&a->in, PSP_CTRL_UP)) { *sel = (*sel + n - 1) % n; moved = 1; }
    if (input_rep(&a->in, PSP_CTRL_DOWN)) { *sel = (*sel + 1) % n; moved = 1; }
    if (moved) sfx_play(&a->sfx, SFX_UI_MOVE);
    return moved;
}
static int lr(void) {
    App *a = &g_app;
    int d = 0;
    if (input_rep(&a->in, PSP_CTRL_LEFT)) d = -1;
    if (input_rep(&a->in, PSP_CTRL_RIGHT)) d = 1;
    if (d) sfx_play(&a->sfx, SFX_UI_MOVE);
    return d;
}
static int ok(void) { if (g_app.in.pressed & PSP_CTRL_CROSS) { sfx_play(&g_app.sfx, SFX_UI_SELECT); return 1; } return 0; }
static int back(void) { if (g_app.in.pressed & PSP_CTRL_CIRCLE) { sfx_play(&g_app.sfx, SFX_UI_BACK); return 1; } return 0; }

static void menu_item(float x, float y, const char *label, const char *value, int selected, int enabled) {
    uint32_t c = !enabled ? RGBA(90, 98, 108, 200) : (selected ? C_SEL : C_TXT);
    if (selected) gfx_rect(x - 8, y - 2, 4, 14, C_SEL);
    gfx_text(FONT_SMALL, x, y, label, c, ALIGN_LEFT);
    if (value) {
        char buf[48];
        snprintf(buf, sizeof(buf), selected ? "< %s >" : "%s", value);
        gfx_text(FONT_SMALL, x + 230, y, buf, selected ? C_SEL : C_ACC, ALIGN_RIGHT);
    }
}

/* ---------------- TITLE / MAIN MENU ---------------- */

static const char *const main_items[6] = { "PATROL", "DRIFT", "SIGNAL CONSOLE", "LOAD GAME", "OPTIONS", "CREDITS" };
static const char *const main_desc[6] = {
    "Missions for the Municipal Signal Authority.",
    "Free flight. No objectives. The city plays itself.",
    "The generative synthesizer, without flying.",
    "Resume from the Memory Stick save.",
    "Audio, display and control settings.",
    "Who made this, and with what." };

static void title_update(void) {
    App *a = &g_app;
    if (a->state_t > 0.6f && (a->in.pressed & (PSP_CTRL_CROSS | PSP_CTRL_START | PSP_CTRL_CIRCLE))) {
        sfx_play(&a->sfx, SFX_UI_SELECT);
        a->menu_sel = 0;
        app_set_state(ST_MENU);
    }
}

static void title_render(void) {
    App *a = &g_app;
    float t = a->state_t;
    gfx_rect_grad(0, 0, SCR_W, SCR_H, RGBA(0, 0, 0, 120), RGBA(0, 0, 0, 200));
    float al = h_clampf(t / 2.0f, 0, 1);
    gfx_logo(SCR_W * 0.5f, 74, al);
    gfx_text(FONT_SMALL, SCR_W * 0.5f, 172, "A GENERATIVE CYBERPUNK FLIGHT EXPERIENCE", col_alpha(C_DIM, al), ALIGN_CENTER);
    float blink = 0.4f + 0.6f * (0.5f + 0.5f * sinf(t * 2.5f));
    if (t > 1.5f) gfx_text(FONT_SMALL, SCR_W * 0.5f, 214, "PRESS CROSS", col_alpha(C_TXT, blink * h_clampf(t - 1.5f, 0, 1)), ALIGN_CENTER);
    gfx_text(FONT_SMALL, SCR_W * 0.5f, SCR_H - 16, "VESPER  2146", col_alpha(RGBA(110, 120, 130, 200), al), ALIGN_CENTER);
}

static void main_update(void) {
    App *a = &g_app;
    list_nav(&a->menu_sel, 6);
    if (back()) app_set_state(ST_TITLE);
    if (!ok()) return;
    switch (a->menu_sel) {
    case 0: a->menu_sel = progress_next_mission(&a->prog); app_set_state(ST_MISSIONS); break;
    case 1: a->menu_sel = 0; app_set_state(ST_DRIFT_SETUP); break;
    case 2: a->console_return = ST_MENU; a->console_overlay = 0; a->console_sel = 0; app_set_state(ST_CONSOLE); break;
    case 3: a->menu_sel = 0; app_set_state(ST_LOAD); break;
    case 4: a->menu_sel = 0; app_set_state(ST_OPTIONS); break;
    case 5: app_set_state(ST_CREDITS); break;
    }
}

static void main_render(void) {
    App *a = &g_app;
    gfx_rect_grad(0, 0, SCR_W, SCR_H, RGBA(0, 0, 0, 70), RGBA(0, 0, 0, 170));
    gfx_logo(120, 34, 0.9f);
    for (int i = 0; i < 6; i++) {
        float y = 132 + (float)i * 17;
        int sel = a->menu_sel == i;
        if (sel) gfx_rect(40, y - 3, 190, 16, RGBA(255, 186, 90, 30));
        gfx_text(FONT_LARGE, 52, y - 2, main_items[i], sel ? C_SEL : C_TXT, ALIGN_LEFT);
    }
    ui_panel(262, 146, 200, 60, 1.0f);
    gfx_text_wrap(FONT_SMALL, 272, 154, 182, main_desc[a->menu_sel], C_TXT, 4, -1);
    char buf[64];
    int done = 0;
    for (int i = 0; i < MISSION_COUNT; i++) if (a->prog.missions_done & (1u << i)) done++;
    snprintf(buf, sizeof(buf), "MISSIONS %d/12   LANDMARKS %d/5", done, __builtin_popcount(a->prog.landmarks));
    gfx_text(FONT_SMALL, 272, 214, buf, C_DIM, ALIGN_LEFT);
    if (a->note_t > 0) gfx_text(FONT_SMALL, 272, 226, a->note, C_ACC, ALIGN_LEFT);
    ui_footer("X:SELECT O:BACK");
}

/* ---------------- MISSIONS / BRIEFING ---------------- */

static void missions_update(void) {
    App *a = &g_app;
    list_nav(&a->menu_sel, MISSION_COUNT);
    if (back()) { a->menu_sel = 0; app_set_state(ST_MENU); return; }
    if (ok()) {
        if (!progress_mission_unlocked(&a->prog, a->menu_sel)) { app_note("COMPLETE THE PREVIOUS ASSIGNMENT FIRST"); sfx_play(&a->sfx, SFX_FAIL); return; }
        a->mission_id = a->menu_sel;
        app_set_state(ST_BRIEFING);
    }
}

static void missions_render(void) {
    App *a = &g_app;
    gfx_rect(0, 0, SCR_W, SCR_H, RGBA(0, 0, 0, 160));
    ui_header("PATROL", "MUNICIPAL SIGNAL AUTHORITY");
    int top = h_clampi(a->menu_sel - 5, 0, MISSION_COUNT - 12);
    for (int k = 0; k < 12 && top + k < MISSION_COUNT; k++) {
        int i = top + k;
        float y = 40 + (float)k * 17;
        int unlocked = progress_mission_unlocked(&a->prog, i), done = (a->prog.missions_done >> i) & 1;
        int sel = a->menu_sel == i;
        if (sel) gfx_rect(14, y - 3, 300, 16, RGBA(255, 186, 90, 28));
        char buf[48];
        snprintf(buf, sizeof(buf), "%02d  %s", i + 1, unlocked ? hush_missions[i].title : "- - - - - - - -");
        gfx_text(FONT_SMALL, 24, y, buf, sel ? C_SEL : (unlocked ? C_TXT : RGBA(90, 98, 108, 220)), ALIGN_LEFT);
        if (unlocked) gfx_text(FONT_SMALL, 300, y, world_district_name(hush_missions[i].district), C_DIM, ALIGN_RIGHT);
        if (done) { gfx_line(308, y + 6, 311, y + 9, C_ACC); gfx_line(311, y + 9, 317, y + 2, C_ACC); }
    }
    ui_panel(326, 40, 144, 200, 1.0f);
    const MissionDef *md = &hush_missions[a->menu_sel];
    if (progress_mission_unlocked(&a->prog, a->menu_sel)) {
        gfx_text(FONT_SMALL, 334, 46, md->title, C_SEL, ALIGN_LEFT);
        gfx_text(FONT_SMALL, 334, 58, world_district_name(md->district), C_ACC, ALIGN_LEFT);
        gfx_text_wrap(FONT_SMALL, 334, 74, 130, md->briefing[0], C_TXT, 8, -1);
    } else gfx_text_wrap(FONT_SMALL, 334, 46, 130, "Assignment sealed. Complete the previous patrol.", C_DIM, 4, -1);
    if (a->note_t > 0) gfx_text(FONT_SMALL, 334, 226, a->note, C_SEL, ALIGN_LEFT);
    ui_footer("X:BRIEFING O:BACK");
}

static void briefing_update(void) {
    App *a = &g_app;
    if (back()) { a->menu_sel = a->mission_id; app_set_state(ST_MISSIONS); return; }
    if (ok() && a->state_t > 0.3f) app_start_patrol(a->mission_id);
}

static void briefing_render(void) {
    App *a = &g_app;
    const MissionDef *md = &hush_missions[a->mission_id];
    gfx_rect(0, 0, SCR_W, SCR_H, RGBA(0, 0, 0, 185));
    char sub[40];
    snprintf(sub, sizeof(sub), "ASSIGNMENT %02d / %s", a->mission_id + 1, world_district_name(md->district));
    ui_header(md->title, sub);
    ui_panel(24, 44, 432, 190, 1.0f);
    gfx_text(FONT_SMALL, 36, 52, "BRIEFING", C_SEL, ALIGN_LEFT);
    float y = 70;
    int chars = (int)(a->state_t * 90.0f);
    for (int i = 0; i < 5 && md->briefing[i]; i++) {
        int n = (int)strlen(md->briefing[i]);
        y += 13.0f * (float)gfx_text_wrap(FONT_SMALL, 36, y, 408, md->briefing[i], C_TXT, 3, chars);
        chars -= n;
        if (chars <= 0) break;
        y += 3;
    }
    gfx_text(FONT_SMALL, 36, 210, "OBJECTIVES:", C_DIM, ALIGN_LEFT);
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", md->n_steps);
    gfx_text(FONT_SMALL, 120, 210, buf, C_ACC, ALIGN_LEFT);
    ui_footer("X:LAUNCH O:BACK");
}

/* ---------------- DEBRIEF / FAILED ---------------- */

static void debrief_update(void) {
    App *a = &g_app;
    if (a->state_t < 0.8f) return;
    if (ok()) {
        a->mode = MODE_NONE;
        if (hush_missions[a->mission_id].rewards & RW_ENDING) { app_start_attract(); app_set_state(ST_CREDITS); return; }
        a->menu_sel = progress_next_mission(&a->prog);
        app_start_attract();
        app_set_state(ST_MISSIONS);
    }
}

static void debrief_render(void) {
    App *a = &g_app;
    const MissionDef *md = &hush_missions[a->mission_id];
    gfx_rect(0, 0, SCR_W, SCR_H, RGBA(0, 0, 0, (int)h_minf(200.0f, a->state_t * 300.0f)));
    ui_header("ASSIGNMENT COMPLETE", md->title);
    ui_panel(24, 44, 432, 190, h_clampf(a->state_t * 2.0f, 0, 1));
    float y = 56;
    for (int i = 0; i < 6 && md->debrief[i]; i++) {
        float al = h_clampf(a->state_t * 1.5f - (float)i * 0.4f, 0, 1);
        int reward = strstr(md->debrief[i], "UNLOCKED") || strstr(md->debrief[i], "CLEARED") || strstr(md->debrief[i], "UPGRADED") || strstr(md->debrief[i], "IMPROVED") || strstr(md->debrief[i], "REINFORCED") || strstr(md->debrief[i], "INCREASED");
        y += 13.0f * (float)gfx_text_wrap(FONT_SMALL, 38, y, 404, md->debrief[i], col_alpha(reward ? C_ACC : C_TXT, al), 3, -1) + 4;
    }
    char buf[48];
    snprintf(buf, sizeof(buf), "TIME %d:%02d   HULL %d%%", (int)a->mission.total_time / 60, (int)a->mission.total_time % 60, (int)(100.0f * a->craft.hull / a->craft.hull_max));
    gfx_text(FONT_SMALL, 38, 214, buf, C_DIM, ALIGN_LEFT);
    if (a->note_t > 0) gfx_text(FONT_SMALL, 444, 214, a->note, C_ACC, ALIGN_RIGHT);
    ui_footer("X:CONTINUE");
}

static void failed_update(void) {
    App *a = &g_app;
    list_nav(&a->menu_sel, 2);
    if (ok() && a->state_t > 0.5f) {
        if (a->menu_sel == 0) app_start_patrol(a->mission_id);
        else { a->menu_sel = a->mission_id; app_start_attract(); app_set_state(ST_MISSIONS); }
    }
}

static void failed_render(void) {
    App *a = &g_app;
    gfx_rect(0, 0, SCR_W, SCR_H, RGBA(20, 0, 0, 170));
    ui_panel(120, 80, 240, 110, 1.0f);
    gfx_text(FONT_LARGE, SCR_W * 0.5f, 92, "ASSIGNMENT FAILED", RGBA(255, 110, 100, 255), ALIGN_CENTER);
    gfx_text(FONT_SMALL, SCR_W * 0.5f, 116, a->mission.fail_reason, C_TXT, ALIGN_CENTER);
    menu_item(200, 146, "RETRY", NULL, a->menu_sel == 0, 1);
    menu_item(200, 164, "ABANDON", NULL, a->menu_sel == 1, 1);
    ui_footer("X:SELECT");
}

/* ---------------- PAUSE ---------------- */

enum { P_RESUME, P_CONSOLE, P_CAMERA, P_HUD, P_DISTRICT, P_WEATHER, P_TIME, P_RESTART, P_SAVE, P_QUIT };
static int pause_items(int *ids) {
    App *a = &g_app;
    int n = 0;
    ids[n++] = P_RESUME; ids[n++] = P_CONSOLE; ids[n++] = P_CAMERA; ids[n++] = P_HUD;
    if (a->mode == MODE_DRIFT) { ids[n++] = P_DISTRICT; ids[n++] = P_WEATHER; ids[n++] = P_TIME; }
    if (a->mode == MODE_PATROL) ids[n++] = P_RESTART;
    ids[n++] = P_SAVE; ids[n++] = P_QUIT;
    return n;
}

static void pause_update(void) {
    App *a = &g_app;
    int ids[12], n = pause_items(ids);
    list_nav(&a->menu_sel, n);
    int id = ids[h_clampi(a->menu_sel, 0, n - 1)];
    int d = lr();
    if ((a->in.pressed & PSP_CTRL_START) || back()) { app_set_state(ST_PLAY); return; }
    if (d) {
        if (id == P_CAMERA) { a->cam.mode = (a->cam.mode + CAM_COUNT + d) % CAM_COUNT; a->prog.cam_mode = (uint8_t)a->cam.mode; }
        if (id == P_HUD) a->prog.hud_mode = (uint8_t)((a->prog.hud_mode + HUD_COUNT + d) % HUD_COUNT);
        if (id == P_DISTRICT) a->prog.drift_district = (uint8_t)((a->prog.drift_district + DISTRICT_COUNT + d) % DISTRICT_COUNT);
        if (id == P_WEATHER) { a->prog.drift_weather = (uint8_t)((a->prog.drift_weather + WX_COUNT + d) % WX_COUNT); weather_set_preset(&a->wx, a->prog.drift_weather); }
        if (id == P_TIME) { a->prog.drift_tod = (uint8_t)((a->prog.drift_tod + TOD_COUNT + d) % TOD_COUNT); a->wx.tod_mode = a->prog.drift_tod; }
    }
    if (!ok()) return;
    switch (id) {
    case P_RESUME: app_set_state(ST_PLAY); break;
    case P_CONSOLE: a->console_return = ST_PLAY; a->console_overlay = 1; app_set_state(ST_CONSOLE); break;
    case P_DISTRICT: if (a->prog.drift_district != a->world_district) app_start_drift(); else app_set_state(ST_PLAY); break;
    case P_RESTART: app_start_patrol(a->mission_id); break;
    case P_SAVE: app_save(); break;
    case P_QUIT: a->mode = MODE_NONE; app_start_attract(); a->menu_sel = 0; app_set_state(ST_MENU); break;
    default: break;
    }
}

static void pause_render(void) {
    App *a = &g_app;
    static const char *const cams[CAM_COUNT] = { "CHASE", "COCKPIT", "CINEMATIC" };
    static const char *const huds[HUD_COUNT] = { "FULL", "MINIMAL", "OFF" };
    gfx_rect(0, 0, SCR_W, SCR_H, RGBA(0, 0, 0, 140));
    int ids[12], n = pause_items(ids);
    float h = 30 + 17.0f * (float)n;
    ui_panel(110, 136 - h * 0.5f, 260, h, 1.0f);
    gfx_text(FONT_SMALL, 122, 136 - h * 0.5f + 7, a->mode == MODE_DRIFT ? "DRIFT - PAUSED" : "PATROL - PAUSED", C_DIM, ALIGN_LEFT);
    for (int i = 0; i < n; i++) {
        float y = 136 - h * 0.5f + 24 + 17.0f * (float)i;
        const char *label = "", *val = NULL;
        switch (ids[i]) {
        case P_RESUME: label = "RESUME"; break;
        case P_CONSOLE: label = "SIGNAL CONSOLE"; break;
        case P_CAMERA: label = "CAMERA"; val = cams[a->cam.mode]; break;
        case P_HUD: label = "HUD"; val = huds[a->prog.hud_mode]; break;
        case P_DISTRICT: label = "DISTRICT"; val = world_district_name(a->prog.drift_district); break;
        case P_WEATHER: label = "WEATHER"; val = weather_names[a->prog.drift_weather]; break;
        case P_TIME: label = "TIME OF DAY"; val = tod_names[a->prog.drift_tod]; break;
        case P_RESTART: label = "RESTART ASSIGNMENT"; break;
        case P_SAVE: label = "SAVE GAME"; break;
        case P_QUIT: label = "QUIT TO MENU"; break;
        }
        menu_item(130, y, label, val, a->menu_sel == i, 1);
    }
    if (a->note_t > 0) gfx_text(FONT_SMALL, SCR_W * 0.5f, 136 + h * 0.5f + 6, a->note, C_ACC, ALIGN_CENTER);
    ui_footer("X:SELECT O:RESUME");
}

/* ---------------- DRIFT SETUP ---------------- */

static void drift_update(void) {
    App *a = &g_app;
    list_nav(&a->menu_sel, 6);
    int d = lr();
    Progress *p = &a->prog;
    if (back()) { a->menu_sel = 1; app_set_state(ST_MENU); return; }
    if (d) {
        switch (a->menu_sel) {
        case 0: p->drift_district = (uint8_t)((p->drift_district + DISTRICT_COUNT + d) % DISTRICT_COUNT); break;
        case 1: p->drift_weather = (uint8_t)((p->drift_weather + WX_COUNT + d) % WX_COUNT); break;
        case 2: p->drift_tod = (uint8_t)((p->drift_tod + TOD_COUNT + d) % TOD_COUNT); break;
        case 3: {
            int k = p->drift_preset;
            for (int i = 0; i < PRESET_BUILTIN_COUNT; i++) { k = (k + PRESET_BUILTIN_COUNT + d) % PRESET_BUILTIN_COUNT; if (p->presets & (1u << k)) break; }
            p->drift_preset = (uint8_t)k;
            uint32_t seed = p->music.seed;
            hush_preset_get(k, &p->music);
            p->music.seed = seed;
            app_push_music();
            break;
        }
        case 4: p->music.seed = (uint32_t)((int)p->music.seed + d); app_push_music(); break;
        default: break;
        }
    }
    if (a->menu_sel == 4 && (a->in.pressed & PSP_CTRL_SQUARE)) { p->music.seed = (uint32_t)(gfx_ticks_us() * 2654435761u) % 100000u; app_push_music(); sfx_play(&a->sfx, SFX_UI_SELECT); }
    if (ok() && a->menu_sel == 5) app_start_drift();
}

static void drift_render(void) {
    App *a = &g_app;
    Progress *p = &a->prog;
    gfx_rect(0, 0, SCR_W, SCR_H, RGBA(0, 0, 0, 150));
    ui_header("DRIFT", "NO OBJECTIVES - THE CITY IS THE INSTRUMENT");
    ui_panel(100, 60, 280, 150, 1.0f);
    char seed[16]; snprintf(seed, sizeof(seed), "%u", p->music.seed);
    menu_item(120, 74, "DISTRICT", world_district_name(p->drift_district), a->menu_sel == 0, 1);
    menu_item(120, 92, "WEATHER", weather_names[p->drift_weather], a->menu_sel == 1, 1);
    menu_item(120, 110, "TIME OF DAY", tod_names[p->drift_tod], a->menu_sel == 2, 1);
    menu_item(120, 128, "MUSIC PRESET", hush_preset_names[p->drift_preset], a->menu_sel == 3, 1);
    menu_item(120, 146, "COMPOSITION SEED", seed, a->menu_sel == 4, 1);
    menu_item(120, 174, "BEGIN DRIFT", NULL, a->menu_sel == 5, 1);
    gfx_text_wrap(FONT_SMALL, 110, 222, 270, "All four districts are open in Drift. Hold TRIANGLE near a sonic landmark to log it in the journal.", C_DIM, 2, -1);
    ui_footer(a->menu_sel == 4 ? "S:RANDOM X:SELECT O:BACK" : "X:SELECT O:BACK");
}

/* ---------------- OPTIONS / LOAD / CONTROLS / CREDITS ---------------- */

static void options_update(void) {
    App *a = &g_app;
    Progress *p = &a->prog;
    list_nav(&a->menu_sel, 9);
    int d = lr();
    if (back()) { a->menu_sel = 4; app_set_state(ST_MENU); return; }
    if (d) {
        switch (a->menu_sel) {
        case 0: p->music_vol = (uint8_t)h_clampi(p->music_vol + d, 0, 10); break;
        case 1: p->sfx_vol = (uint8_t)h_clampi(p->sfx_vol + d, 0, 10); break;
        case 2: p->hud_mode = (uint8_t)((p->hud_mode + HUD_COUNT + d) % HUD_COUNT); break;
        case 3: p->cam_mode = (uint8_t)((p->cam_mode + CAM_COUNT + d) % CAM_COUNT); break;
        case 4: p->invert_lift ^= 1; break;
        case 5: p->rain_density = (uint8_t)h_clampi(p->rain_density + d, 0, 3); break;
        case 6: p->show_fps ^= 1; break;
        case 7: p->paint = (uint8_t)((p->paint + 1) % 2); if (!(p->paints & (1u << p->paint))) { p->paint = 0; app_note("COMPLETE RECOVERY TO UNLOCK RAINWATER"); } break;
        default: break;
        }
        app_push_music();
    }
    if (ok()) {
        if (a->menu_sel == 8) { app_set_state(ST_CONTROLS); return; }
        app_save();
    }
}

static void options_render(void) {
    App *a = &g_app;
    Progress *p = &a->prog;
    static const char *const huds[HUD_COUNT] = { "FULL", "MINIMAL", "OFF" };
    static const char *const cams[CAM_COUNT] = { "CHASE", "COCKPIT", "CINEMATIC" };
    static const char *const rain[4] = { "LOW", "MEDIUM", "HIGH", "MAXIMUM" };
    static const char *const paints[2] = { "MSA STANDARD", "RAINWATER" };
    gfx_rect(0, 0, SCR_W, SCR_H, RGBA(0, 0, 0, 160));
    ui_header("OPTIONS", "X: SAVE SETTINGS");
    ui_panel(90, 44, 300, 188, 1.0f);
    char mv[8], sv[8];
    snprintf(mv, sizeof(mv), "%d", p->music_vol); snprintf(sv, sizeof(sv), "%d", p->sfx_vol);
    const char *labels[9] = { "MUSIC VOLUME", "EFFECTS VOLUME", "HUD", "DEFAULT CAMERA", "INVERT ALTITUDE", "RAIN DENSITY", "PERFORMANCE OVERLAY", "MOTH PAINT", "CONTROLS REFERENCE" };
    const char *vals[9] = { mv, sv, huds[p->hud_mode], cams[p->cam_mode], p->invert_lift ? "ON" : "OFF", rain[p->rain_density], p->show_fps ? "ON" : "OFF", paints[p->paint], NULL };
    for (int i = 0; i < 9; i++) menu_item(110, 56 + 19.0f * (float)i, labels[i], vals[i], a->menu_sel == i, 1);
    if (a->note_t > 0) gfx_text(FONT_SMALL, SCR_W * 0.5f, 236, a->note, C_ACC, ALIGN_CENTER);
    ui_footer("X:SAVE O:BACK");
}

static void load_update(void) {
    App *a = &g_app;
    list_nav(&a->menu_sel, 2);
    if (back()) { a->menu_sel = 3; app_set_state(ST_MENU); return; }
    if (!ok()) return;
    if (a->menu_sel == 0) {
        char msg[64];
        int okk = save_io_load(&a->prog, msg, sizeof(msg));
        app_note(msg);
        app_push_music();
        if (okk) { a->menu_sel = progress_next_mission(&a->prog); app_set_state(ST_MISSIONS); }
    } else {
        progress_defaults(&a->prog);
        app_push_music();
        app_save();
        app_note("NEW CAREER STARTED");
    }
}

static void load_render(void) {
    App *a = &g_app;
    Progress *p = &a->prog;
    gfx_rect(0, 0, SCR_W, SCR_H, RGBA(0, 0, 0, 160));
    ui_header("LOAD GAME", "MEMORY STICK: PSP/GAME/HUSH/hush.sav");
    ui_panel(90, 50, 300, 170, 1.0f);
    char buf[80];
    int done = 0;
    for (int i = 0; i < MISSION_COUNT; i++) if (p->missions_done & (1u << i)) done++;
    snprintf(buf, sizeof(buf), "ASSIGNMENTS COMPLETE   %d / 12", done); gfx_text(FONT_SMALL, 110, 62, buf, C_TXT, ALIGN_LEFT);
    snprintf(buf, sizeof(buf), "SONIC LANDMARKS        %d / 5", __builtin_popcount(p->landmarks)); gfx_text(FONT_SMALL, 110, 76, buf, C_TXT, ALIGN_LEFT);
    snprintf(buf, sizeof(buf), "DISTRICTS CLEARED      %d / 4", __builtin_popcount(p->districts)); gfx_text(FONT_SMALL, 110, 90, buf, C_TXT, ALIGN_LEFT);
    snprintf(buf, sizeof(buf), "FLIGHT TIME            %dh %02dm", (int)(p->play_seconds / 3600), (int)(p->play_seconds / 60 % 60)); gfx_text(FONT_SMALL, 110, 104, buf, C_TXT, ALIGN_LEFT);
    snprintf(buf, sizeof(buf), "CURRENT SEED           %u", p->music.seed); gfx_text(FONT_SMALL, 110, 118, buf, C_TXT, ALIGN_LEFT);
    menu_item(110, 150, "LOAD SAVED GAME", NULL, a->menu_sel == 0, 1);
    menu_item(110, 168, "START NEW CAREER", NULL, a->menu_sel == 1, 1);
    if (a->note_t > 0) gfx_text(FONT_SMALL, SCR_W * 0.5f, 200, a->note, C_ACC, ALIGN_CENTER);
    ui_footer("X:SELECT O:BACK");
}

static void controls_render(void) {
    gfx_rect(0, 0, SCR_W, SCR_H, RGBA(0, 0, 0, 190));
    ui_header("CONTROLS", "THE MOTH");
    static const char *const rows[][2] = {
        { "ANALOGUE NUB", "Fly forward / back, strafe left / right" },
        { "L / R", "Turn (yaw)" },
        { "D-PAD UP / DOWN", "Ascend / descend" },
        { "D-PAD LEFT / RIGHT", "Tilt the camera" },
        { "CROSS", "Boost (hold, while moving forward)" },
        { "SQUARE", "Brake and stabilise" },
        { "TRIANGLE", "Scanner (hold)" },
        { "CIRCLE", "Context action / EMP" },
        { "SELECT", "Camera: chase, cockpit, cinematic" },
        { "START", "Pause menu / Signal Console" },
    };
    ui_panel(30, 40, 420, 200, 1.0f);
    for (int i = 0; i < 10; i++) {
        gfx_text(FONT_SMALL, 46, 52 + 18.0f * (float)i, rows[i][0], C_SEL, ALIGN_LEFT);
        gfx_text(FONT_SMALL, 200, 52 + 18.0f * (float)i, rows[i][1], C_TXT, ALIGN_LEFT);
    }
    ui_footer("O:BACK");
}

static const char *const credits_text[] = {
    "HUSH", "A Generative Cyberpunk Flight Experience", "",
    "by Synth Centric", "",
    "DESIGN, WORLD AND GENERATIVE MUSIC SYSTEM", "Synth Centric", "",
    "PROGRAMMING", "Synth Centric, with Claude Code (Anthropic)", "",
    "THE CITY", "Vesper, 2146. Every building generated from a seed.", "",
    "THE MUSIC", "Six interacting generative layers, synthesised in real time", "on the PSP. No recordings. No loops.", "",
    "TOOLS", "PSPDEV / PSPSDK (BSD licence)", "Tested in PPSSPP", "",
    "TYPEFACES", "DejaVu Sans Mono (Bitstream Vera licence)", "Inter (SIL Open Font License 1.1)", "",
    "", "The city never sleeps.", "It has simply forgotten how to listen.", "", "",
    "Thank you for listening.", NULL };

static void credits_update(void) {
    App *a = &g_app;
    if (back() || (ok() && a->state_t > 1.0f)) { a->menu_sel = 5; app_set_state(ST_MENU); }
}

static void credits_render(void) {
    App *a = &g_app;
    gfx_rect(0, 0, SCR_W, SCR_H, RGBA(0, 0, 0, 200));
    float y = SCR_H - a->state_t * 18.0f;
    int n = 0;
    for (int i = 0; credits_text[i]; i++) n++;
    if (y + (float)n * 14.0f < 0) a->state_t = 0;
    for (int i = 0; credits_text[i]; i++) {
        float yy = y + (float)i * 14.0f;
        if (yy < -14 || yy > SCR_H) continue;
        float al = h_clampf(h_minf(yy / 40.0f, (SCR_H - yy) / 40.0f), 0, 1);
        int head = i > 0 && credits_text[i - 1][0] == 0 && credits_text[i][0] && credits_text[i + 1] && credits_text[i + 1][0];
        if (i == 0) gfx_logo(SCR_W * 0.5f, yy - 20, al);
        else gfx_text(i == 1 ? FONT_SMALL : FONT_SMALL, SCR_W * 0.5f, yy + 40, credits_text[i], col_alpha(head ? C_SEL : C_TXT, al), ALIGN_CENTER);
    }
    ui_footer("O:BACK");
}

/* ---------------- dispatch ---------------- */

void menus_update(float dt) {
    (void)dt;
    switch (g_app.state) {
    case ST_TITLE: title_update(); break;
    case ST_MENU: main_update(); break;
    case ST_MISSIONS: missions_update(); break;
    case ST_BRIEFING: briefing_update(); break;
    case ST_DEBRIEF: debrief_update(); break;
    case ST_FAILED: failed_update(); break;
    case ST_PAUSE: pause_update(); break;
    case ST_DRIFT_SETUP: drift_update(); break;
    case ST_OPTIONS: options_update(); break;
    case ST_LOAD: load_update(); break;
    case ST_CONTROLS: if (back() || ok()) { g_app.menu_sel = 8; app_set_state(ST_OPTIONS); } break;
    case ST_CREDITS: credits_update(); break;
    default: break;
    }
}

void menus_render(void) {
    switch (g_app.state) {
    case ST_TITLE: title_render(); break;
    case ST_MENU: main_render(); break;
    case ST_MISSIONS: missions_render(); break;
    case ST_BRIEFING: briefing_render(); break;
    case ST_DEBRIEF: debrief_render(); break;
    case ST_FAILED: failed_render(); break;
    case ST_PAUSE: pause_render(); break;
    case ST_DRIFT_SETUP: drift_render(); break;
    case ST_OPTIONS: options_render(); break;
    case ST_LOAD: load_render(); break;
    case ST_CONTROLS: controls_render(); break;
    case ST_CREDITS: credits_render(); break;
    default: break;
    }
}
