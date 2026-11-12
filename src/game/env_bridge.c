/* HUSH — world-to-music mapping. All values are normalised here; the
 * composer applies its own smoothing time constants, so nothing jumps. */
#include "env_bridge.h"

void env_bridge_update(EnvBridge *b, MusicEnv *e, const World *w, const Craft *c, const Traffic *t,
                       const Weather *wx, float pursuit, float scan, float signal) {
    e->active = 1;
    e->district = w->id;
    float floor_y = w->water_y > w->ground_y ? w->water_y : w->ground_y;
    e->altitude = h_clampf((c->pos.y - floor_y - 8.0f) / (w->skyline * 1.35f), 0.0f, 1.0f);
    e->speed = h_clampf(c->speed / 70.0f, 0.0f, 1.0f);
    e->rain = wx->rain;
    e->fog = wx->fog;
    e->pursuit = h_clampf(pursuit, 0.0f, 1.0f);
    e->scan = scan;
    e->signal = signal;
    /* enclosure is a few ray casts: refresh it every 8 calls */
    if ((b->counter++ & 7) == 0) b->enclosure = world_enclosure(w, c->pos, 55.0f);
    e->canyon = b->enclosure;
    e->traffic = traffic_density(t, c->pos, 160.0f);
    /* nearest sonic landmark within its influence radius (3x its scan radius) */
    int best = -1;
    float bp = 0;
    for (int i = 0; i < w->n_pois; i++) {
        const Poi *p = &w->pois[i];
        if (p->kind != POI_LANDMARK) continue;
        float reach = p->r * 3.2f;
        float d = v3_dist(c->pos, p->p);
        float prox = h_clampf(1.0f - (d - p->r * 0.4f) / reach, 0.0f, 1.0f);
        if (prox > bp) { bp = prox; best = p->landmark; }
    }
    e->landmark = best;
    e->landmark_prox = bp;
    b->nearest_landmark = best;
    b->landmark_prox = bp;
}
