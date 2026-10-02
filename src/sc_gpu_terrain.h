#pragma once
#include "sc_renderer.h"
typedef struct SDL_Renderer SDL_Renderer;
typedef struct SDL_Texture SDL_Texture;
typedef struct ScGpuTerrain ScGpuTerrain;
/* Unsupported renderers return NULL; the CPU renderer remains available. */
ScGpuTerrain *ScGpuTerrainCreate(SDL_Renderer *renderer,bool linear_filter);
SDL_Texture *ScGpuTerrainDraw(ScGpuTerrain *g,const ScRenderer *r);
void ScGpuTerrainDestroy(ScGpuTerrain *g);
