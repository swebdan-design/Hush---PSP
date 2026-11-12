/* HUSH — the Moth: arcade hovercraft flight model and cameras (platform independent). */
#ifndef HUSH_FLIGHT_H
#define HUSH_FLIGHT_H

#include "../core/hmath.h"
#include "world.h"

typedef struct {
    float fwd, strafe;     /* analogue nub, -1..1 (fwd +1 = forward) */
    float yaw;             /* -1 (L) .. 1 (R) */
    float lift;            /* -1 descend .. 1 ascend */
    int boost, brake;
} FlightInput;

typedef struct {
    V3 pos, vel;
    float yaw;             /* radians, 0 = facing +z */
    float yaw_rate;
    float bank, pitch;     /* visual attitude */
    float thrust[4];       /* visual nacelle intensity 0..1 */
    float nacelle_tilt;    /* visual, radians */
    float hull;            /* 0..100 */
    float boost_energy;    /* 0..1 */
    float impact;          /* last impact strength for feedback (decays) */
    float speed;           /* |vel| */
    float hover_time;      /* seconds spent nearly stationary */
    float accum;           /* fixed-step accumulator */
    int boosting;
    int stalled_frames;
    /* upgrades */
    float max_speed, boost_speed, accel_mul, hull_max;
} Craft;

#define CRAFT_RADIUS 3.2f
#define FLIGHT_DT (1.0f / 60.0f)

void craft_init(Craft *c, V3 pos, float yaw);
void craft_apply_upgrades(Craft *c, int boost_level, int hull_level);
/* Advances with fixed 60 Hz sub-steps. Returns accumulated impact damage this call. */
float craft_update(Craft *c, const World *w, const FlightInput *in, float dt);
V3 craft_forward(const Craft *c);
V3 craft_right(const Craft *c);

enum { CAM_CHASE = 0, CAM_COCKPIT, CAM_CINEMATIC, CAM_COUNT };
typedef struct {
    int mode;
    V3 eye, target, up;
    V3 vel_eye;
    float pitch_offset;    /* user camera pitch (d-pad left/right) */
    float fov;
    float orbit;           /* cinematic orbit angle */
    float shake;
} Camera;

void camera_init(Camera *cam, const Craft *c);
void camera_update(Camera *cam, const Craft *c, const World *w, float dt);

#endif
