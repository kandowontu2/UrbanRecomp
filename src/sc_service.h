#pragma once
#include "sc_world.h"
typedef struct Interp816 Interp816;
bool ScServiceOwns(uint16_t pc);
/* Complete police/fire word-field smoothing directly in C. Return original clocks and stop at the caller's beam/IRQ deadline. */
unsigned ScServiceStep(ScWorld *world,Interp816 *cpu,uint8_t *ram,
                         const uint8_t *rom,unsigned max_cycles);

/* One charged original opcode; no full-cell fusions. */
unsigned ScServiceInstructionStep(ScWorld *world,Interp816 *cpu,uint8_t *ram,const uint8_t *rom);
