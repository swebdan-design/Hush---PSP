/* HUSH — Patrol Mode missions: data-driven objective sequences with real
 * completion and failure conditions. Platform independent (host-tested). */
#ifndef HUSH_MISSIONS_H
#define HUSH_MISSIONS_H

#include "world.h"
#include "flight.h"
#include "traffic.h"

enum { OB_GOTO = 0, OB_SCAN, OB_FOLLOW, OB_PURSUE, OB_LOCATE, OB_COLLECT, OB_RINGS, OB_LANDMARK, OB_DOCK, OB_LISTEN };

/* rewards (bit flags) */
#define RW_SCANNER        (1u << 0)
#define RW_BOOST          (1u << 1)
#define RW_HULL           (1u << 2)
#define RW_EMP            (1u << 3)
#define RW_STACKS         (1u << 4)
#define RW_WORKS          (1u << 5)
#define RW_UNDERTOW       (1u << 6)
#define RW_WAVE_VOX       (1u << 7)
#define RW_WAVE_HOLLOW    (1u << 8)
#define RW_WAVE_GRIT      (1u << 9)
#define RW_WAVE_GLASS     (1u << 10)
#define RW_PRESET_ARCHIVE (1u << 11)
#define RW_PRESET_CHOIR   (1u << 12)
#define RW_PRESET_RESERVOIR (1u << 13)
#define RW_PRESET_NULL    (1u << 14)
#define RW_PAINT          (1u << 15)
#define RW_ENDING         (1u << 16)

typedef struct {
    uint8_t type;
    const char *text;        /* objective line; may contain %d for counts */
    const char *poi;         /* POI name, or prefix for COLLECT/RINGS */
    int count;
    float param;             /* scan seconds / listen seconds */
    float time_limit;        /* 0 = none */
    const char *speaker, *msg;          /* transmission when the step starts */
    const char *done_speaker, *done;    /* transmission when it completes */
    float min_d, max_d;      /* FOLLOW distances */
} MissionStep;

#define MAX_STEPS 8
typedef struct {
    const char *title;
    int district;
    const char *briefing[5];
    MissionStep steps[MAX_STEPS];
    int n_steps;
    const char *debrief[6];
    uint32_t rewards;
    uint32_t grants_at_start;
    const char *collect_msgs[4];  /* per-item transmissions for COLLECT steps */
} MissionDef;

#define MISSION_COUNT 12
extern const MissionDef hush_missions[MISSION_COUNT];

enum { MS_IDLE = 0, MS_ACTIVE, MS_COMPLETE, MS_FAILED };
enum { MEV_NONE = 0, MEVT_TRANSMISSION, MEVT_STEP_DONE, MEVT_COMPLETE, MEVT_FAILED, MEVT_DISCOVERY, MEVT_COLLECT, MEVT_EMP_HIT, MEVT_EMP_MISS, MEVT_WARNING };

typedef struct { uint8_t type; int8_t arg; const char *speaker; const char *text; } MissionEvent;

typedef struct {
    int mission, step, state;
    float step_time, total_time, timer;
    float scan_progress;
    float signal;                /* 0..1 signal strength for HUD / music */
    float pursuit;               /* 0..1 urgency for music */
    float warn_time;             /* FOLLOW: time outside the allowed band */
    int collected;               /* bitmask */
    int ring;
    int vehicle;                 /* traffic index for FOLLOW / PURSUE */
    float emp_cooldown;
    int has_emp;
    float scanner_range;
    V3 marker; int marker_on;
    V3 target; float target_r;
    float dock_time, listen_time;
    char objective[64];
    char fail_reason[48];
    MissionEvent ev[8];
    int n_ev;
} MissionRun;

typedef struct {
    int scan_held;       /* TRIANGLE held */
    int action_pressed;  /* CIRCLE pressed this frame */
    int any_input;       /* any flight control in use (for LISTEN) */
} MissionInput;

void mission_start(MissionRun *m, int mission, const World *w, Traffic *t, const Craft *c, int scanner_level, int has_emp);
void mission_update(MissionRun *m, const World *w, Traffic *t, const Craft *c, const MissionInput *in, float dt);
/* Is a point currently scannable from the craft (range, facing, line of sight)? */
int mission_can_scan(const World *w, const Craft *c, V3 target, float range);
/* Free-flight scanner: nearest landmark within range in view, or -1. */
int scanner_landmark_in_view(const World *w, const Craft *c, float range, float *dist);

#endif
