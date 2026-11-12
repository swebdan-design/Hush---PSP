/* HUSH — signal hunting. No markers: the scanner's ping, strength meter and an
 * edge-of-screen chevron are the only guide. Platform independent (host-tested). */
#ifndef HUSH_SIGNALS_H
#define HUSH_SIGNALS_H

#include "world.h"
#include "flight.h"

#define HUNT_RANGE 260.0f
#define HUNT_CAPTURE_R 14.0f
#define HUNT_CAPTURE_TIME 1.6f

typedef struct {
    int nearest;          /* index into w->signals, -1 = none unfound in range */
    float dist;
    float strength;       /* 0..1: distance and facing */
    float bearing;        /* radians, + = to the right of the nose */
    float capture;        /* 0..1 while holding scan inside the capture radius */
    float ping_t;         /* seconds until the next ping */
    int ping;             /* 1 on frames where a ping sounds */
    int found;            /* global signal id captured this frame, -1 otherwise */
} Hunt;

void hunt_reset(Hunt *h);
void hunt_update(Hunt *h, const World *w, const Craft *c, uint32_t found_mask, int scan_held, float dt);
int hunt_count_district(const World *w, uint32_t found_mask);

#endif
