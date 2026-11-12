/* HUSH — pedestrian rendering. */
#ifndef HUSH_RENDER_CROWD_H
#define HUSH_RENDER_CROWD_H

#include "../game/crowd.h"
#include "render_world.h"

void rw_draw_crowd(const Crowd *c, const Atmos *a, float time);

#endif
