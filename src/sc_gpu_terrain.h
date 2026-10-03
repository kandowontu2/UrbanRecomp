#pragma once
#include "sc_renderer.h"
typedef struct SDL_Renderer SDL_Renderer;
typedef struct SDL_Window SDL_Window;
typedef struct SDL_Texture SDL_Texture;
typedef struct ScGpuTerrain ScGpuTerrain;
/* Creates the shared Vulkan presentation/compute device. Explicit alternative
 * SDL drivers and unsupported machines retain the ordinary CPU fallback. */
SDL_Renderer *ScGpuTerrainRenderer(SDL_Window *window);
/* Unsupported renderers return NULL; the CPU renderer remains available. */
ScGpuTerrain *ScGpuTerrainCreate(SDL_Renderer *renderer,bool linear_filter);
SDL_Texture *ScGpuTerrainDraw(ScGpuTerrain *g,const ScRenderer *r);
void ScGpuTerrainDestroy(ScGpuTerrain *g);
