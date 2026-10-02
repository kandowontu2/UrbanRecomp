#pragma once
#include <stdbool.h>
#include <stdint.h>

/* A deferred pixel is distinct from both the native PPU's zero alpha and
 * the compositor's opaque alpha. Later HUD/cursor writes override it. */
#define SC_TERRAIN_PIXEL UINT32_C(0x01000000)
typedef struct {uint32_t base,roof,attributes,reserved;} ScTerrainTile;
typedef struct {
    uint32_t phase,core_x,main,sub,window_main,window_sub,windows,logic,bounds;
    uint32_t math,control,fixed;
    uint32_t brightness[32],sub_bg[256];
} ScTerrainRow;
typedef struct {
    unsigned width,height,stride;
    ScTerrainTile *tiles;
    uint32_t *palette;
    ScTerrainRow *rows;
    unsigned deferred;
} ScTerrainFrame;
bool ScTerrainResize(ScTerrainFrame *f,unsigned width,unsigned height);
void ScTerrainDestroy(ScTerrainFrame *f);
uint32_t ScTerrainPixel(const ScTerrainFrame *f,unsigned x,unsigned y);
