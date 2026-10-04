#pragma once
#include "sc_population.h"
#include "sc_world.h"
typedef struct {
    uint32_t residential,commercial,industrial,police,fire,plants,stadiums,airports,seaports,rewards;
    uint32_t powered_zones,zones;
    uint64_t population;
} ScTestCityStats;
bool ScTestCityGenerate(ScWorld *world,ScPopulation *population,uint8_t *ram,
    const uint8_t *rom,size_t size,ScTestCityStats *stats);
void ScTestCityInspect(const ScWorld *world,const ScPopulation *population,
    const uint8_t *rom,ScTestCityStats *stats);
size_t ScTestCityRecordSize(void);
bool ScTestCityRecordValid(const uint8_t *data,size_t size);
bool ScTestCityEncode(uint8_t *data,size_t size,const uint8_t *sram,
    const ScWorld *world,const ScPopulation *population);
bool ScTestCityDecode(const uint8_t *data,size_t size,ScWorld *world,ScPopulation *population);
const uint8_t *ScTestCityNativeSave(const uint8_t *data,size_t size);
