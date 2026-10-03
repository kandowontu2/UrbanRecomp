#pragma once
#include "sc_world.h"
typedef struct Interp816 Interp816;
bool ScTransportOwns(uint16_t pc);
/* Ordered transport search, route traversal and traffic publication, directly
 * in C. Return original clocks and stop at the caller's beam/IRQ deadline. */
unsigned ScTransportStep(ScWorld *world,Interp816 *cpu,uint8_t *ram,
                         const uint8_t *rom,unsigned max_cycles);

/* Execute exactly one original instruction, including its zero-clock world
 * hook. Zero leaves state untouched. Pending interrupts retain the CPU path. */
unsigned ScTransportInstructionStep(ScWorld *restrict w,Interp816 *restrict c,
    uint8_t *restrict r,const uint8_t *restrict rom);
