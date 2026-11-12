/* HUSH — weather and time of day. Lightweight state that drives both the
 * renderer (fog, rain, sky) and the music engine (rain, fog inputs). */
#ifndef HUSH_WEATHER_H
#define HUSH_WEATHER_H

#include <stdint.h>

enum { WX_CLEAR = 0, WX_LIGHT_RAIN, WX_HEAVY_RAIN, WX_FOG, WX_AUTO, WX_COUNT };
enum { TOD_NIGHT = 0, TOD_TWILIGHT, TOD_CYCLE, TOD_COUNT };
extern const char *const weather_names[WX_COUNT];
extern const char *const tod_names[TOD_COUNT];

typedef struct {
    int preset, tod_mode;
    float rain, fog;          /* current 0..1 */
    float rain_t, fog_t;      /* targets */
    float tod;                /* 0 = deep night, 1 = twilight */
    float timer, clock;
    uint32_t rng;
} Weather;

void weather_init(Weather *w, int preset, int tod_mode, uint32_t seed);
void weather_set_preset(Weather *w, int preset);
void weather_update(Weather *w, float dt);
/* Fog distances for the renderer, given the district's base fog. */
void weather_fog(const Weather *w, int district, float *near_d, float *far_d);

#endif
