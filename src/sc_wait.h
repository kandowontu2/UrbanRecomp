#pragma once
#include <stdint.h>
#include <stdbool.h>
typedef struct Interp816 Interp816;
/* Verified US frame wait. Preserve the entropy counter, CPU flags and original
 * clocks while stopping before the caller's next scanline/HDMA/IRQ event. */
unsigned ScWaitStep(Interp816 *cpu,uint8_t *ram,unsigned max_cycles);
/* Connected entry, wait and return. The instruction tier never fuses edges. */
bool ScWaitOwns(unsigned pc);
unsigned ScWaitDriverStep(Interp816 *cpu,uint8_t *ram,unsigned max_cycles);
unsigned ScWaitInstructionStep(Interp816 *cpu,uint8_t *ram);
