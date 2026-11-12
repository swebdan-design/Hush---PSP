/* HUSH — procedurally generated textures (built at start-up, uploaded to VRAM). */
#ifndef HUSH_TEXTURES_H
#define HUSH_TEXTURES_H

#include "../platform/psp/gfx.h"
#include "../game/world.h"

typedef struct {
    Tex facade[TEX_COUNT];
    Tex glow, skyline, water, ground, ads, steam, signs;
} TexSet;

extern TexSet g_tex;
void textures_init(void);

/* world-space size (metres) covered by one facade texture repeat */
#define FACADE_TILE 24.0f

#endif
