#pragma once
#include "sc_refresh_clock.h"
#include "sc_world.h"
#include <stddef.h>

typedef struct {
    ScRefreshClock clock;
    uint8_t bitmap[SC_WORLD_MAX_CELLS/8];
    /* The native scratch word selects either an ordered network walk or
     * plant seeds alone. Cache both exact results for one electrical graph. */
    uint8_t policy_bitmap[2][SC_WORLD_MAX_CELLS/8];
    bool policy_valid[2];
    uint64_t network_solves,policy_reuses;
    uint8_t topology_tiles[SC_WORLD_MAX_TILE_BYTES];
    uint64_t topology_revisions[SC_WORLD_TILE_CHUNKS];
    uint64_t publication_revisions[SC_WORLD_TILE_CHUNKS];
    uint8_t topology_different[SC_WORLD_TILE_CHUNKS];
    uint8_t ids_different[SC_WORLD_TILE_CHUNKS];
    unsigned different_chunks;
    unsigned different_id_chunks;
    unsigned clock_cells;
    int game_speed;
    bool ready, large, huge;
    bool traversal_allowed;
} ScPowerRefresh;
void ScPowerRefreshReset(ScPowerRefresh *s);
void ScPowerRefreshObserve(ScPowerRefresh *s, uint64_t frame, int game_speed);
/* Rebuild the real network before a loaded city's first development pass.
 * Seeds the settled cache for Normal as well as accelerated development. */
bool ScPowerRefreshRestore(ScPowerRefresh *s,uint8_t *ram,ScWorld *world,
    const uint8_t *rom,size_t size,uint64_t frame);
/* Only power bits are published. The bitmap remains owned by the native
 * flood fill while bitmap_available is false. Returns true on a new solve. */
bool ScPowerRefreshStep(ScPowerRefresh *s,uint8_t *ram,ScWorld *world,
    const uint8_t *rom,size_t size,uint64_t frame,int multiplier,bool bitmap_available);
