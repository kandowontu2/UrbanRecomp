#pragma once
#include <stdint.h>
typedef struct Interp816 Interp816;
/* Verified US bank-03 arithmetic and additive RNG helpers, in C. Execute complete iterations
 * within the caller's next beam event budget. Return original cycles;
 * zero leaves the current instruction to the compatibility interpreter. */
unsigned ScMathStep(Interp816 *cpu,uint8_t *ram,unsigned max_cycles);
/* Fuse RNG setup, iterations and return within that same clock budget. Stops
 * before a caller or arithmetic-helper entry needs its own scheduler hooks. */
unsigned ScMathBatchStep(Interp816 *cpu,uint8_t *ram,unsigned max_cycles);
