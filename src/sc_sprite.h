#pragma once
#include <stdbool.h>
#include <stdint.h>
typedef struct Interp816 Interp816;

/* Clean US counted sprite emitter. Direct C builds OAM records and their
 * high-bit masks; each span ends at an original instruction boundary and
 * must fit before the caller's next scanline/HDMA/IRQ deadline. */
static inline bool ScSpriteOwns(uint16_t pc) {return pc>=0x905c && pc<=0x90dc;}
unsigned ScSpriteStep(Interp816 *cpu,uint8_t *ram,unsigned max_cycles);
/* One fixed C instruction edge, for the ordinary interpreter's equivalent
 * deadline overrun. No devices are accessed by this sprite routine. */
unsigned ScSpriteInstructionStep(Interp816 *cpu,uint8_t *ram);
