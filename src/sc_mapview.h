/* Host-side city map renderer -- see docs/PLAN_renderer.md Stage 2.
 *
 * Draws the live 120x100 city map from WRAM using tile graphics out of VRAM,
 * entirely host-side. The guest still owns every bit of state; this only draws
 * it differently, which is the state/presentation line the plan sets out.
 *
 * Kept in its own translation unit so main.c stays free of the tile-decode
 * detail, the same reason sc_fiberdrive.c is separate. */
#ifndef SC_MAPVIEW_H_INCLUDED
#define SC_MAPVIEW_H_INCLUDED

#include <stdbool.h>
#include <stdint.h>
#include "sc_world.h"

/* True when the loaded ROM is the one the tile tables were mapped against. */
void ScMapView_SetRomIsUs(bool is_us);
void ScMapView_SetWorld(const ScWorld *world);

/* Render `cols` x `rows` map cells starting at cell (sx, sy) into `out` as
 * ARGB8888 with `pitch` bytes per row. Returns false if the ROM is not the US
 * image (the tile-table addresses are US-specific) or inputs are missing. */
bool ScMapView_Render(uint8_t *out, int pitch, int cols, int rows,
                      int sx, int sy);

/* The map ($7F0200, 120x100 words) and palette ($7E2440, 256 words) as
 * ScMapView_Snapshot copies them. */
#define SC_MAPVIEW_MAP_BYTES SC_WORLD_TILE_BYTES
#define SC_MAPVIEW_PAL_BYTES 512u

/* Draw from these copies instead of WRAM; NULL, NULL draws WRAM again. */
void ScMapView_SetSource(const uint8_t *map, const uint8_t *pal);
void ScMapView_Snapshot(uint8_t *map, uint8_t *pal);
/* How many cells' tile numbers differ between `map` and WRAM. */
int  ScMapView_ChangedCells(const uint8_t *map);

/* Current map scroll in cells, read from $01bd/$01bf. */
void ScMapView_GetScroll(int *sx, int *sy);

/* Pixels per map cell. 8 is native; larger zooms in, smaller zooms out.
 * Only meaningful because the map is drawn host-side -- the guest's own
 * renderer is fixed at 8. */
void ScMapView_SetCellPx(int px);
int  ScMapView_GetCellPx(void);

#endif /* SC_MAPVIEW_H_INCLUDED */
