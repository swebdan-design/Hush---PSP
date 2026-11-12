/* HUSH — the city of Vesper: district data, deterministic generation and collision.
 * Platform independent; rendering builds meshes from these primitives. */
#ifndef HUSH_WORLD_H
#define HUSH_WORLD_H

#include <stdint.h>
#include "../core/hmath.h"
#include "../audio/music.h"

#define WORLD_HALF 620.0f          /* district spans [-WORLD_HALF, WORLD_HALF] in x and z */
#define WORLD_CEILING 460.0f
#define MAX_BOXES 2000
#define MAX_CYLS 72
#define MAX_LIGHTS 900
#define MAX_ADS 40
#define MAX_LANES 28
#define LANE_PTS 12
#define MAX_POIS 48
#define MAX_VENTS 20
#define GRID_N 32
#define GRID_CELL (2.0f * WORLD_HALF / GRID_N)
#define GRID_MAX_REFS 12000

typedef struct { float x0, y0, z0, x1, y1, z1; } Aabb;

/* facade/texture styles */
enum { TEX_GLASS = 0, TEX_RESIDENTIAL, TEX_INDUSTRIAL, TEX_DERELICT, TEX_CONCRETE, TEX_METAL, TEX_GARDEN, TEX_COUNT };
/* box kinds affect rendering details only */
enum { BOX_BUILDING = 0, BOX_TIER, BOX_ROOF_DETAIL, BOX_HIGHWAY, BOX_WALKWAY, BOX_PIPE, BOX_PAD, BOX_GANTRY, BOX_LANDMARK, BOX_RUIN };

typedef struct {
    Aabb b;
    uint8_t tex, kind, tint, flags;   /* flags bit0: no collision, bit1: lit windows dense, bit2: no roof */
} Box;

enum { CYL_COOLING = 0, CYL_CHIMNEY, CYL_TANK, CYL_SPIRE, CYL_PYLON };
typedef struct { float x, z, r0, r1, y0, y1; uint8_t kind, sides; } Cyl;

enum { LT_AVIATION = 0, LT_WINDOW, LT_BEACON, LT_WARNING, LT_STREET, LT_SIGN, LT_LANDMARK, LT_WATER };
typedef struct { V3 p; float size; uint32_t color; uint8_t kind, blink; float phase; } Light;

typedef struct { V3 p; float w, h; uint8_t axis; int8_t facing; uint8_t ad; uint8_t flicker; } Ad;   /* axis 0: quad in XY plane (faces +/-z), 1: ZY plane */

typedef struct { V3 pts[LANE_PTS]; int n; float speed, len; uint8_t kind, loop, count; float seglen[LANE_PTS]; } Lane;

enum { POI_GENERIC = 0, POI_PAD, POI_TRANSMITTER, POI_SIGNAL, POI_DATA, POI_RING, POI_LANDMARK, POI_BUILDING };
typedef struct { char name[24]; V3 p; float r; uint8_t kind; int8_t landmark; } Poi;

typedef struct { V3 p; float r, h; } Vent;

typedef struct {
    int id;
    const char *name;
    uint32_t gen_seed;
    float ground_y, water_y;     /* water_y < ground_y means no water */
    float skyline;               /* typical roof height used for altitude normalisation */
    V3 spawn; float spawn_yaw;

    Box boxes[MAX_BOXES]; int n_boxes;
    Cyl cyls[MAX_CYLS]; int n_cyls;
    Light lights[MAX_LIGHTS]; int n_lights;
    Ad ads[MAX_ADS]; int n_ads;
    Lane lanes[MAX_LANES]; int n_lanes;
    Poi pois[MAX_POIS]; int n_pois;
    Vent vents[MAX_VENTS]; int n_vents;

    /* collision grid: per cell list of box indices (cyls are tested directly) */
    uint16_t cell_start[GRID_N * GRID_N + 1];
    uint16_t refs[GRID_MAX_REFS];
    int n_refs;
} World;

void world_generate(World *w, int district);
const char *world_district_name(int district);

/* named points of interest used by missions and landmarks */
const Poi *world_poi(const World *w, const char *name);
int world_landmark_poi(const World *w, int landmark);

/* collision: pushes a sphere out of geometry. Returns 1 on contact, writes contact normal. */
int world_collide_sphere(const World *w, V3 *p, float r, V3 *normal);
/* Line of sight / ray: returns fraction [0,1] of segment a->b that is free. */
float world_raycast(const World *w, V3 a, V3 b);
/* Fraction of the 8 compass directions blocked by buildings within radius and above height y. */
float world_enclosure(const World *w, V3 p, float radius);
/* Height of the tallest geometry under (x,z), or ground. */
float world_height_at(const World *w, float x, float z);

/* lane evaluation: position and tangent at arc length s (wraps for loops) */
V3 lane_eval(const Lane *l, float s, V3 *tangent);

#endif
