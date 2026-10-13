#pragma once
#include "sc_population.h"
typedef struct Interp816 Interp816;
enum {SC_MARS_SCENARIO=9,SC_MARS_START_YEAR=2050,SC_MARS_MONTHS=60,
      SC_MARS_PARKS=2400,SC_MARS_POLLUTION=8,SC_MARS_CLEAN_MONTHS=12,
      SC_MARS_POPULATION=50000,SC_MARS_POWER_PERCENT=95,
      SC_MARS_HOT_PERCENT=1};
typedef struct {
    unsigned parks,pollution,hot_cells,pollution_cells,zones,powered;
    uint64_t population;
    unsigned months_left;
    bool clean;
} ScMarsStats;
bool ScMarsScenarioGenerate(ScWorld *world,ScPopulation *population,uint8_t *ram,
                           const uint8_t *rom,size_t size);
void ScMarsScenarioInspect(const ScWorld *world,const ScPopulation *population,
                          const uint8_t *ram,const uint8_t *rom,ScMarsStats *stats);
/* Observe once per calendar month; repeated visits/fast-forward cannot add time. */
void ScMarsScenarioObserve(ScWorld *world,const ScPopulation *population,const uint8_t *ram,const uint8_t *rom);
void ScMarsScenarioStep(ScWorld *world,const ScPopulation *population,Interp816 *cpu,uint8_t *ram,const uint8_t *rom);
bool ScMarsScenarioMessageRead(const ScWorld *world,uint32_t address,uint8_t *value);
/* Fifth selector column, with Mars below Sylt. Preserve locked vanilla cards. */
void ScMarsScenarioSelectorStep(Interp816 *cpu,uint8_t *ram,bool sylt);
void ScMarsScenarioCard(uint16_t *vram,unsigned map,const uint8_t font[40][8]);
