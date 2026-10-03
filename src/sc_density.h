#pragma once
#include "sc_world.h"
typedef struct Interp816 Interp816;
bool ScDensityOwns(uint16_t pc);
/* Connected density, centroid and field accounting directly in C. Return original clocks and stop at the caller's beam/IRQ deadline. */
unsigned ScDensityStep(ScWorld *world,Interp816 *cpu,uint8_t *ram,
                         const uint8_t *rom,unsigned max_cycles);

/* Execute exactly one original instruction, including its zero-clock world
 * hook. Zero leaves state untouched. Pending interrupts retain the CPU path. */
unsigned ScDensityInstructionStep(ScWorld *restrict w,Interp816 *restrict c,
    uint8_t *restrict r,const uint8_t *restrict rom);
