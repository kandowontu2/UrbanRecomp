#pragma once
#include "sc_world.h"
typedef struct Interp816 Interp816;
bool ScSmoothingOwns(uint16_t pc);
/* Complete alternating-field smoothing directly in C. Return original clocks and stop at the caller's beam/IRQ deadline. */
unsigned ScSmoothingStep(ScWorld *world,Interp816 *cpu,uint8_t *ram,
                         const uint8_t *rom,unsigned max_cycles);

/* One indivisible instruction at a raster deadline; no full-cell fusions. */
unsigned ScSmoothingInstructionStep(ScWorld *world,Interp816 *cpu,uint8_t *ram,const uint8_t *rom);
