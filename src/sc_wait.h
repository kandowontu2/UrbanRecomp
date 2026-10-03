#pragma once
#include <stdint.h>
typedef struct Interp816 Interp816;
/* Verified US frame wait. Preserve the entropy counter, CPU flags and original
 * clocks while stopping before the caller's next scanline/HDMA/IRQ event. */
unsigned ScWaitStep(Interp816 *cpu,uint8_t *ram,unsigned max_cycles);
