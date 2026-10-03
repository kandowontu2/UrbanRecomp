#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "sc_mapgen.h"

enum {
    SC_WORLD_WIDTH = 240, SC_WORLD_HEIGHT = 200,
    SC_WORLD_CELLS = SC_WORLD_WIDTH * SC_WORLD_HEIGHT,
    SC_WORLD_TILE_BYTES = SC_WORLD_CELLS * 2,
    SC_WORLD_MAX_WIDTH = 1920, SC_WORLD_MAX_HEIGHT = 1600,
    SC_WORLD_MAX_CELLS = SC_WORLD_MAX_WIDTH * SC_WORLD_MAX_HEIGHT,
    SC_WORLD_MAX_TILE_BYTES = SC_WORLD_MAX_CELLS * 2,
    SC_WORLD_FIELDS = 19, SC_WORLD_FIELD_BYTES = 768000,
    SC_WORLD_TILE_CHUNK_BYTES = 512,
    SC_WORLD_TILE_CHUNKS = SC_WORLD_MAX_TILE_BYTES / SC_WORLD_TILE_CHUNK_BYTES
};
typedef struct ScWorldField {
    uint16_t base, stock_width, stock_height, width, height;
    uint8_t element_bytes;
} ScWorldField;
typedef struct ScWorld {
    bool active;
    bool huge;
    bool giant;
    bool colossal;
    uint16_t scan_x, scan_y;
    uint16_t center_x,center_y;
    bool center_valid;
    bool journey, journey_announcing;
    uint8_t journey_notice; /* 1 Big, 2 Huge; persisted until Wright dismisses it */
    uint8_t journey_target; /* highest population threshold reached, never regresses */
    int16_t coord[3][2]; /* full coordinates behind native packed-byte proxies */
    uint32_t map_anchor;
    uint32_t bank_anchor[3]; /* rendering/vehicles cannot replace the sim cursor */
    uint32_t field_anchor[3], field_scan; /* full indices behind native X */
    uint8_t tiles[SC_WORLD_MAX_TILE_BYTES];
    uint8_t fields[SC_WORLD_FIELDS][SC_WORLD_FIELD_BYTES];
} ScWorld;

extern const ScWorldField ScWorldFields[SC_WORLD_FIELDS];
static inline unsigned ScWorldScale(const ScWorld *w) { return w && w->colossal?8:w && w->giant?4:w && w->huge?2:1; }
static inline unsigned ScWorldWidth(const ScWorld *w) { return 240*ScWorldScale(w); }
static inline unsigned ScWorldHeight(const ScWorld *w) { return 200*ScWorldScale(w); }
static inline unsigned ScWorldCells(const ScWorld *w) { return ScWorldWidth(w)*ScWorldHeight(w); }
static inline bool ScWorldContains(const ScWorld *w,int x,int y) {
    return x>=0 && y>=0 && (unsigned)x<ScWorldWidth(w) && (unsigned)y<ScWorldHeight(w);
}
void ScWorldReset(ScWorld *world);
void ScWorldGenerate(ScWorld *world, ScMapGenPrng *prng);
void ScWorldGenerateHuge(ScWorld *world, ScMapGenPrng *prng);
void ScWorldGenerateGiant(ScWorld *world, ScMapGenPrng *prng);
void ScWorldGenerateColossal(ScWorld *world, ScMapGenPrng *prng);
bool ScWorldBounds(int x, int y);
uint16_t ScWorldCell(const ScWorld *world, int x, int y);
bool ScWorldPutCell(ScWorld *world, int x, int y, uint16_t tile);
/* Host-only render invalidation, outside serialized simulation state.
 * Every direct tile writer must touch its changed byte range. Multiple
 * renderers independently remember the returned monotonically increasing
 * chunk revisions; observing them never consumes another renderer's edits. */
const uint64_t *ScWorldTileRevisions(const ScWorld *world);
void ScWorldTilesTouch(const ScWorld *world,unsigned offset,unsigned bytes);
/* Apply MSB-first packed power bits to [first,end) cells and invalidate changed
 * chunks once. A null world publishes native WRAM without host revisions. */
void ScWorldPublishPower(ScWorld *world,uint8_t *tiles,const uint8_t *bitmap,unsigned first,unsigned end);
unsigned ScWorldFieldSize(unsigned field);
unsigned ScWorldFieldWidth(const ScWorld *w,unsigned field);
unsigned ScWorldFieldHeight(const ScWorld *w,unsigned field);
unsigned ScWorldFieldSizeWorld(const ScWorld *w,unsigned field);
/* Resolve the original instruction's ARRAY BASE, not the resulting guest
 * address: expanded arrays overlap in guest address space and live separately
 * here. Neighbour bases (e.g. $b16c = $b16e-2) retain their row/column meaning. */
bool ScWorldFieldResolve(uint16_t base, unsigned *field, int *displacement);
size_t ScWorldEncodedSize(void);
bool ScWorldEncode(const ScWorld *world, uint8_t *data, size_t size);
bool ScWorldDecode(ScWorld *world, const uint8_t *data, size_t size);
size_t ScWorldCitiesSize(void);
void ScWorldCitiesInit(uint8_t *data);
bool ScWorldCitiesValid(const uint8_t *data, size_t size);
bool ScWorldCitiesUpgrade(uint8_t *out,const uint8_t *data,size_t size);
bool ScWorldCitySave(uint8_t *data, const uint8_t *sram, unsigned slot, const ScWorld *world);
bool ScWorldCityLoad(ScWorld *world, const uint8_t *data, size_t size, const uint8_t *sram, unsigned slot);
/* The native save codec stores the original-sized top-left region; the host record
 * above stores the complete world. Never enlarge its bank-7f destination. */
void ScWorldMirror(const ScWorld *world, uint8_t *ram);

/* Host-only epoch changes when world data is reset or loaded. Async compute
 * must reject work from an earlier epoch, even at the same address and size. */
uint64_t ScWorldStateEpoch(void);
