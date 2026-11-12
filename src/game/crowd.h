/* HUSH — street-level pedestrians. Platform independent (host-tested). */
#ifndef HUSH_CROWD_H
#define HUSH_CROWD_H

#include "world.h"

#define MAX_PEDS 220

typedef struct {
    uint8_t active, umbrella, style, loiter;
    int16_t block;
    float t, speed, dir;        /* position along the block's sidewalk loop (m), walking speed, +1/-1 */
    float phase;                /* walk-cycle phase */
    float wait;                 /* seconds left standing still */
    float flee;                 /* 0..1 alarm when the Moth comes close */
    V3 pos;
    float yaw;
    uint32_t coat, umb;
    int16_t chatter;            /* index into the overheard-line pool, -1 none */
} Ped;

typedef struct {
    Ped p[MAX_PEDS];
    int n;
    uint32_t rng;
    uint32_t tick;
} Crowd;

void crowd_init(Crowd *c, const World *w);
/* rain 0..1 raises umbrellas; the craft's position scatters nearby walkers */
void crowd_update(Crowd *c, const World *w, V3 craft, float rain, float dt);
/* nearest pedestrian within r of p (for overheard chatter); -1 if none */
int crowd_nearest(const Crowd *c, V3 p, float r);

#endif
