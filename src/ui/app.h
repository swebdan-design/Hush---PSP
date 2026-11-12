/* HUSH — application state shared by the PSP screens. */
#ifndef HUSH_APP_H
#define HUSH_APP_H

#include "../game/world.h"
#include "../game/flight.h"
#include "../game/traffic.h"
#include "../game/weather.h"
#include "../game/missions.h"
#include "../game/progress.h"
#include "../game/env_bridge.h"
#include "../game/autopilot.h"
#include "../game/crowd.h"
#include "../audio/music.h"
#include "../audio/sfx.h"
#include "../platform/psp/input.h"
#include "../platform/psp/gfx.h"
#include "../render/render_world.h"

typedef enum {
    ST_TITLE = 0, ST_MENU, ST_MISSIONS, ST_BRIEFING, ST_PLAY, ST_PAUSE, ST_DEBRIEF, ST_FAILED,
    ST_CONSOLE, ST_OPTIONS, ST_CREDITS, ST_LOAD, ST_DRIFT_SETUP, ST_CONTROLS, ST_COUNT
} AppState;

enum { MODE_NONE = 0, MODE_PATROL, MODE_DRIFT, MODE_ATTRACT };

#define TX_QUEUE 6
typedef struct { const char *speaker; char text[200]; float t, dur; } Transmission;

typedef struct {
    AppState state, console_return;
    float state_t, time;
    int mode, quit;
    Input in;
    Progress prog;
    char note[64];
    float note_t;

    World world;
    int world_district;      /* -1 = none loaded */
    Traffic traffic;
    Crowd crowd;
    Weather wx;
    Craft craft;
    Camera cam;
    MissionRun mission;
    EnvBridge bridge;
    Autopilot pilot;
    MusicEnv env;
    Atmos atmos;
    int mission_id;

    Transmission tx[TX_QUEUE];
    int tx_n;

    /* free scanner (landmark discovery) */
    float scan_t;
    int scan_lm;
    int scanning;
    float discovery_t;
    int discovery_lm;

    int menu_sel, menu_top;
    int console_page, console_sel, console_overlay;
    int hud_override;        /* -1 = use options */
    float fps, fps_acc;
    int fps_frames;
    int autotest;
    float emp_flash;

    HushMusic *music;
    Sfx sfx;
} App;

extern App g_app;

/* app.c / main.c */
void app_set_state(AppState s);
void app_note(const char *msg);
void app_push_music(void);           /* sends prog.music to the engine */
void app_save(void);
void app_load_district(int district);
void app_start_drift(void);
void app_start_patrol(int mission);
void app_start_attract(void);
void app_tx(const char *speaker, const char *text);

/* game.c */
void game_update(float dt, int controls_enabled);
void game_render(int with_hud);

/* hud.c */
void hud_draw(void);
void hud_draw_transmissions(void);

/* menus.c */
void menus_update(float dt);
void menus_render(void);
void ui_panel(float x, float y, float w, float h, float alpha);
void ui_header(const char *title, const char *sub);
void ui_footer(const char *hints);

/* controls.c */
enum { CTL_ACT_UP = 0, CTL_ACT_DOWN, CTL_ACT_BOOST, CTL_ACT_BRAKE, CTL_ACT_SCAN, CTL_ACT_EMP };
const char *ctl_name(int action);
char ctl_glyph(int action);
const char *ctl_subst(const char *in, char *buf, int n);   /* replaces [UP] [DOWN] [BOOST] [BRAKE] [SCAN] [EMP] */
void ctl_hint_panel(float centre_x, float y, float alpha);
float ui_glyph(float x, float y, char b, float alpha);       /* X O S T face buttons, u d D-pad, L R */

/* console.c */
void console_update(float dt);
void console_render(void);

#endif
