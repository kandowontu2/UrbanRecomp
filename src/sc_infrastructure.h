#pragma once
#include "sc_world.h"
typedef struct Interp816 Interp816;
bool ScInfrastructureOwns(uint16_t pc);
/* Road, bridge and rail upkeep, artwork and decay in C. Preserve original
 * clocks and stop at the caller's beam/IRQ deadline. */
unsigned ScInfrastructureStep(ScWorld *world,Interp816 *cpu,uint8_t *ram,
                              const uint8_t *rom,unsigned max_cycles);
unsigned ScInfrastructureInstructionStep(ScWorld *world,Interp816 *cpu,
                                         uint8_t *ram,const uint8_t *rom);
