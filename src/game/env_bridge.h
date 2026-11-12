/* HUSH — translates the world state into music engine inputs ("the city is the instrument"). */
#ifndef HUSH_ENV_BRIDGE_H
#define HUSH_ENV_BRIDGE_H

#include "world.h"
#include "flight.h"
#include "traffic.h"
#include "weather.h"
#include "../audio/music.h"

typedef struct {
    float enclosure;      /* cached canyon value */
    int counter;
    int nearest_landmark;
    float landmark_prox;
} EnvBridge;

void env_bridge_update(EnvBridge *b, MusicEnv *out, const World *w, const Craft *c, const Traffic *t,
                       const Weather *wx, float pursuit, float scan, float signal);

#endif
