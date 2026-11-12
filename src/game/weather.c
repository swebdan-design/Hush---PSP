/* HUSH — weather. AUTO evolves between conditions with a seeded random walk. */
#include "weather.h"
#include "../core/hmath.h"
#include "../audio/music.h"

const char *const weather_names[WX_COUNT] = { "CLEAR", "LIGHT RAIN", "HEAVY RAIN", "FOG", "EVOLVING" };
const char *const tod_names[TOD_COUNT] = { "NIGHT", "TWILIGHT", "SLOW CYCLE" };

static float wrnd(Weather *w) { w->rng ^= w->rng << 13; w->rng ^= w->rng >> 17; w->rng ^= w->rng << 5; return (float)(w->rng >> 8) * (1.0f / 16777216.0f); }

static void targets_for(Weather *w, int p) {
    switch (p) {
    case WX_CLEAR: w->rain_t = 0.0f; w->fog_t = 0.15f; break;
    case WX_LIGHT_RAIN: w->rain_t = 0.4f; w->fog_t = 0.35f; break;
    case WX_HEAVY_RAIN: w->rain_t = 1.0f; w->fog_t = 0.55f; break;
    case WX_FOG: w->rain_t = 0.05f; w->fog_t = 1.0f; break;
    default: break;
    }
}

void weather_init(Weather *w, int preset, int tod_mode, uint32_t seed) {
    w->preset = preset; w->tod_mode = tod_mode;
    w->rng = seed | 1u; w->timer = 0; w->clock = 0;
    targets_for(w, preset == WX_AUTO ? WX_LIGHT_RAIN : preset);
    w->rain = w->rain_t; w->fog = w->fog_t;
    w->tod = tod_mode == TOD_TWILIGHT ? 1.0f : 0.0f;
}

void weather_set_preset(Weather *w, int preset) {
    w->preset = preset;
    if (preset != WX_AUTO) targets_for(w, preset);
    w->timer = 0;
}

void weather_update(Weather *w, float dt) {
    w->clock += dt;
    if (w->preset == WX_AUTO) {
        w->timer -= dt;
        if (w->timer <= 0) {
            float r = wrnd(w);
            int p = r < 0.3f ? WX_CLEAR : (r < 0.6f ? WX_LIGHT_RAIN : (r < 0.82f ? WX_HEAVY_RAIN : WX_FOG));
            targets_for(w, p);
            w->timer = 90.0f + 150.0f * wrnd(w);
        }
    }
    w->rain = h_approach(w->rain, w->rain_t, 0.08f, dt);
    w->fog = h_approach(w->fog, w->fog_t, 0.06f, dt);
    if (w->tod_mode == TOD_CYCLE) w->tod = 0.5f - 0.5f * cosf(w->clock * (H_TAU / 600.0f));
    else w->tod = h_approach(w->tod, w->tod_mode == TOD_TWILIGHT ? 1.0f : 0.0f, 0.2f, dt);
}

void weather_fog(const Weather *w, int district, float *near_d, float *far_d) {
    static const float base_far[DISTRICT_COUNT] = { 640.0f, 520.0f, 520.0f, 380.0f };
    float f = base_far[district < 0 || district >= DISTRICT_COUNT ? 0 : district];
    f *= 1.0f - 0.55f * w->fog - 0.15f * w->rain;
    *far_d = h_maxf(f, 150.0f);
    *near_d = *far_d * (0.12f - 0.08f * w->fog);
}
