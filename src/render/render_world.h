/* HUSH — world renderer: static chunked city meshes, sky, distant skyline,
 * water, emissive lights and reflections, adverts, weather particles, traffic. */
#ifndef HUSH_RENDER_WORLD_H
#define HUSH_RENDER_WORLD_H

#include "../game/world.h"
#include "../game/traffic.h"
#include "../game/weather.h"
#include "../platform/psp/gfx.h"

typedef struct {
    uint32_t sky_top, sky_horizon, fog_color;
    float fog_near, fog_far;
} Atmos;

typedef struct {
    int tris_drawn, chunks_drawn, lights_drawn, vehicles_drawn;
    int verts_total, clipped;
    float build_ms;
} RenderStats;

void rw_build(const World *w);
void rw_atmos(const World *w, const Weather *wx, Atmos *out);
/* Draws sky + skyline (call after gfx_begin, before rw_draw_world). */
void rw_draw_sky(const World *w, const Atmos *a, V3 eye, V3 target, V3 up, float fov);
void rw_draw_world(const World *w, const Atmos *a, float time);
void rw_draw_traffic(const World *w, const Traffic *t, const Atmos *a, float time);
/* emissive point lights batched until rw_flush_lights */
void rw_light(V3 p, float size, uint32_t color, float alpha);
void rw_draw_world_lights(const World *w, const Atmos *a, float time, float wet);
void rw_flush_lights(const World *w, const Atmos *a);
void rw_draw_effects(const World *w, const Weather *wx, const Atmos *a, float time, float dt, int rain_density);
const RenderStats *rw_stats(void);
/* adaptive detail 0 (lowest) .. 3 (full): draw distance, light/sign/crowd/rain budgets, reflections */
void rw_set_quality(int q);
int rw_quality(void);

static inline int vent_active(int i, float t) { float p = fmodf(t + (float)i * 1.7f, 6.0f); return p < 2.6f; }

#endif
