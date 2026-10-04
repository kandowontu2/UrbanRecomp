#pragma once
#include "sc_world.h"
typedef struct Interp816 Interp816;
bool ScZoningOwns(uint16_t pc);
/* Connected zone growth and decline directly in C. Return original clocks and stop at the caller's beam/IRQ deadline. */
unsigned ScZoningStep(ScWorld *world,Interp816 *cpu,uint8_t *ram,
                         const uint8_t *rom,unsigned max_cycles);

/* One indivisible original instruction at beam/IRQ deadlines. */
unsigned ScZoningInstructionStep(ScWorld *world,Interp816 *cpu,uint8_t *ram,const uint8_t *rom);

/* Connected growth/mutation body for stationary-clock extra attempts. Explicit
 * SC_ZONING_REFERENCE=1 retains the preceding driver path. */
unsigned ScZoningAcceleratedStep(ScWorld *world,Interp816 *cpu,uint8_t *ram,
                                const uint8_t *rom,unsigned max_cycles);

/* Complete quality classification with one deadline preflight and direct fields. */
unsigned ScZoningQualityStep(ScWorld *world,Interp816 *cpu,uint8_t *ram,unsigned max_cycles);

/* Whole RCI control decisions, with atomic publication at helper boundaries. */
unsigned ScZoningControlStep(ScWorld *world,Interp816 *cpu,uint8_t *ram,
                             const uint8_t *rom,unsigned max_cycles);
