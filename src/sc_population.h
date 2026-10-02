#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "sc_world.h"

#define SC_POPULATION_MAX UINT64_C(9999999999)
enum { SC_POPULATION_HISTORY = 1200 };
typedef struct ScPopulation {
    uint64_t value, previous, capacity[3]; /* residential, commercial, industrial */
    int64_t change;
    uint64_t history[SC_POPULATION_HISTORY];
    uint32_t history_head, history_count;
    uint8_t tally_active[3];
    bool valid, calculation_wide, live;
} ScPopulation;

void ScPopulationImport(ScPopulation *s, const uint8_t *ram);
void ScPopulationSet(ScPopulation *s, uint8_t *ram, uint64_t value);
void ScPopulationMirror(const ScPopulation *s, uint8_t *ram);
/* Read-only census of the current map. Leaves the native sweep accumulators,
 * calendar/history and guest RAM intact. No multiplier is applied to people. */
bool ScPopulationRefreshLive(ScPopulation *s, const uint8_t *ram, const ScWorld *world,
                             const uint8_t *rom, size_t size);
/* Before the verified US bank-03 opcode. The returned PC remains unchanged
 * on the stock path when its arithmetic can represent the correct result. */
uint16_t ScPopulationStep(ScPopulation *s, uint8_t *ram, uint16_t pc, uint16_t dp);
unsigned ScPopulationClass(uint64_t value);
void ScPopulationReport(const ScPopulation *s, uint8_t *ram, bool change);
/* Explicit little-endian encoding, independent of compiler padding. */
enum { SC_POPULATION_BYTES = 80 + SC_POPULATION_HISTORY * 8 };
void ScPopulationEncode(const ScPopulation *s, uint8_t out[SC_POPULATION_BYTES]);
bool ScPopulationDecode(ScPopulation *s, const uint8_t *data, size_t size);
enum { SC_POPULATION_CITIES_BYTES = 24 + SC_POPULATION_BYTES * 2 };
/* Sidecar entries are bound to the matching native SRAM slot's contents;
 * importing another emulator's save cannot attach stale population metadata. */
void ScPopulationCitiesInit(uint8_t data[SC_POPULATION_CITIES_BYTES]);
bool ScPopulationCitiesValid(const uint8_t *data, size_t size);
void ScPopulationCitySave(uint8_t data[SC_POPULATION_CITIES_BYTES], const uint8_t *sram,
                          unsigned slot, const ScPopulation *s);
bool ScPopulationCityLoad(ScPopulation *s, const uint8_t *data, size_t size,
                          const uint8_t *sram, unsigned slot);
