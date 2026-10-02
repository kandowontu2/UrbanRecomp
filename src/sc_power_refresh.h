#pragma once
#include "sc_refresh_clock.h"
#include "sc_world.h"
#include <stddef.h>

typedef struct {
    ScRefreshClock clock;
    uint8_t bitmap[SC_WORLD_MAX_CELLS/8];
    uint8_t topology_tiles[SC_WORLD_MAX_TILE_BYTES];
    unsigned clock_cells;
    int game_speed;
    bool ready, large, huge;
} ScPowerRefresh;
void ScPowerRefreshReset(ScPowerRefresh *s);
void ScPowerRefreshObserve(ScPowerRefresh *s, uint64_t frame, int game_speed);
/* Only power bits are published. The bitmap remains owned by the native
 * flood fill while bitmap_available is false. Returns true on a new solve. */
bool ScPowerRefreshStep(ScPowerRefresh *s,uint8_t *ram,ScWorld *world,
    const uint8_t *rom,size_t size,uint64_t frame,int multiplier,bool bitmap_available);
