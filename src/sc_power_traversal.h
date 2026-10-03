#pragma once
#include "sc_world.h"
typedef struct Interp816 Interp816;
bool ScPowerTraversalOwns(uint16_t pc);
/* Instrument whole C operations reached inside a connected traversal. */
uint64_t ScPowerTraversalStageSpans(void);
uint64_t ScPowerTraversalStageClocks(void);
/* Ordered electrical-network traversal and bitmap updates directly in C. Return original clocks and stop at the caller's beam/IRQ deadline. */
unsigned ScPowerTraversalStep(ScWorld *world,Interp816 *cpu,uint8_t *ram,
                         const uint8_t *rom,unsigned max_cycles);

/* Execute exactly one original instruction, including its zero-clock world
 * hook. Zero leaves state untouched. Pending interrupts retain the CPU path. */
unsigned ScPowerTraversalInstructionStep(ScWorld *restrict w,Interp816 *restrict c,
    uint8_t *restrict r,const uint8_t *restrict rom);
