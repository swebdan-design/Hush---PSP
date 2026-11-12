/* HUSH — the living city: lane-following traffic (civil, municipal, mission vehicles). */
#ifndef HUSH_TRAFFIC_H
#define HUSH_TRAFFIC_H

#include "world.h"

#define MAX_VEH 72
enum { VK_GROUND = 0, VK_CAR, VK_TRANSPORT, VK_PATROL, VK_COURIER, VK_DRONE, VK_SERVICE };

typedef struct {
    uint8_t active, kind, lane, mission, disabled;
    float s, speed, base_speed;
    float off_x, off_y;          /* lateral and vertical offsets from the lane centre */
    V3 pos, dir;
    float yaw;
    float fall_vel;              /* disabled drones fall */
    uint32_t color;
} Vehicle;

typedef struct {
    Vehicle v[MAX_VEH];
    int n;
    float time;
} Traffic;

void traffic_init(Traffic *t, const World *w);
void traffic_update(Traffic *t, const World *w, float dt);
/* Adds a vehicle under mission control. Returns index or -1. */
int traffic_spawn(Traffic *t, const World *w, int lane, float s, float speed, int kind);
/* Normalised density of vehicles within radius r (0..1). */
float traffic_density(const Traffic *t, V3 p, float r);
int traffic_find_lane(const World *w, int kind);

#endif
