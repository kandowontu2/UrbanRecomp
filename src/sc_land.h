#pragma once
#include "sc_world.h"
typedef struct Interp816 Interp816;
bool ScLandOwns(uint16_t pc);
unsigned ScLandStep(ScWorld *world,Interp816 *cpu,uint8_t *ram,unsigned max_cycles);

/* Execute exactly one original instruction, including its zero-clock world
 * hook. Zero leaves state untouched. Pending interrupts retain the CPU path. */
unsigned ScLandInstructionStep(ScWorld *restrict w,Interp816 *restrict c,uint8_t *restrict r);
