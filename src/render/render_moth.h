/* HUSH — the player's craft model. */
#ifndef HUSH_RENDER_MOTH_H
#define HUSH_RENDER_MOTH_H

#include "../game/flight.h"
#include "../platform/psp/gfx.h"

void moth_init(void);
/* draw_body = 0 in cockpit view (only engine glow contributes) */
void moth_draw(const Craft *c, float time, int scheme, int draw_body, float fog_near, float fog_far, uint32_t fog_col);
V3 moth_nose(const Craft *c);

#endif
