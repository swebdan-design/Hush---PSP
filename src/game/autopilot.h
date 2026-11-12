/* HUSH — mission autopilot. Produces controller-equivalent inputs (FlightInput +
 * scan/EMP buttons) to play a Patrol mission through the normal game rules.
 * Used by the host test-suite, by the PSP validation build inside PPSSPP, and
 * as the title screen's attract-mode pilot. It never moves the craft directly. */
#ifndef HUSH_AUTOPILOT_H
#define HUSH_AUTOPILOT_H

#include "missions.h"

typedef struct {
    int phase, have_goal, last_step;
    V3 goal, ap;
    float cruise;
    V3 trail[512];
    int trail_n;
    /* free-roam (attract mode) */
    float wander_t;
    V3 wander;
    uint32_t rng;
} Autopilot;

void autopilot_reset(Autopilot *a);
void autopilot_mission(Autopilot *a, const World *w, const Traffic *t, const Craft *c, const MissionRun *m, FlightInput *in, MissionInput *mi);
/* gentle sightseeing between random rooftops and landmarks */
void autopilot_wander(Autopilot *a, const World *w, const Craft *c, FlightInput *in, float dt);
/* climb, traverse and descend to a point; returns 1 when within 3 m */
int autopilot_fly_to(Autopilot *a, const World *w, const Craft *c, V3 goal, FlightInput *in);

#endif
