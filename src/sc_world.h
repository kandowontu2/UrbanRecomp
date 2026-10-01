#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "sc_mapgen.h"

enum {
    SC_WORLD_WIDTH = 240, SC_WORLD_HEIGHT = 200,
    SC_WORLD_CELLS = SC_WORLD_WIDTH * SC_WORLD_HEIGHT,
    SC_WORLD_TILE_BYTES = SC_WORLD_CELLS * 2,
    SC_WORLD_FIELDS = 19, SC_WORLD_FIELD_BYTES = 20001
};
typedef struct ScWorldField {
    uint16_t base, stock_width, stock_height, width, height;
    uint8_t element_bytes;
} ScWorldField;
typedef struct ScWorld {
    bool active;
    uint32_t map_anchor;
    uint32_t bank_anchor[3]; /* rendering/vehicles cannot replace the sim cursor */
    uint8_t tiles[SC_WORLD_TILE_BYTES];
    uint8_t fields[SC_WORLD_FIELDS][SC_WORLD_FIELD_BYTES];
} ScWorld;

extern const ScWorldField ScWorldFields[SC_WORLD_FIELDS];
void ScWorldReset(ScWorld *world);
void ScWorldGenerate(ScWorld *world, ScMapGenPrng *prng);
bool ScWorldBounds(int x, int y);
uint16_t ScWorldCell(const ScWorld *world, int x, int y);
bool ScWorldPutCell(ScWorld *world, int x, int y, uint16_t tile);
unsigned ScWorldFieldSize(unsigned field);
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
bool ScWorldCitySave(uint8_t *data, const uint8_t *sram, unsigned slot, const ScWorld *world);
bool ScWorldCityLoad(ScWorld *world, const uint8_t *data, size_t size, const uint8_t *sram, unsigned slot);
/* The native save codec stores the original-sized top-left region; the host record
 * above stores the complete world. Never enlarge its bank-7f destination. */
void ScWorldMirror(const ScWorld *world, uint8_t *ram);
