#ifndef SC_PROGRAM_H
#define SC_PROGRAM_H
#include <stdbool.h>
#include <stdint.h>
typedef struct Interp816 Interp816;
/* Caller proves the loaded ROM's original US fingerprint before enabling.
 * Each emitted edge also validates its live opcode. The verified view-cursor
 * NOP patch has explicit C variants; other patched/RAM code yields before
 * mutating state. Host hooks and beam events run between C edges. */
void ScProgramEnable(bool clean_us);
/* Select a generated, fingerprint-verified cartridge profile. US native host
 * enhancements stay separately gated; regional stock code uses compiled C. */
bool ScProgramSelectRom(uint32_t fingerprint);
bool ScProgramAvailable(const Interp816 *cpu);
bool ScProgramBlockAvailable(const Interp816 *cpu);
unsigned ScProgramStep(Interp816 *cpu);
/* Common instruction/control entry for private construction and simulation
 * CPUs as well as the host. Keeps the independent interpreter only as the
 * disabled/unsupported reference path while migration coverage is checked. */
unsigned ScProgramExecute(Interp816 *cpu);
/* Retire pending NMI/IRQ or idle WAI/STP control with the existing CPU ABI.
 * Zero leaves all CPU/bus state unchanged, including masked-IRQ WAI wakeup
 * that needs an opcode edge. The host advances devices after a nonzero cost. */
unsigned ScProgramControlStep(Interp816 *cpu);
uint64_t ScProgramControlCalls(void);
typedef bool (*ScProgramBefore)(void *context,Interp816 *cpu);
typedef bool (*ScProgramAfter)(void *context,Interp816 *cpu,unsigned clocks);
typedef unsigned (*ScProgramFallback)(void *context,Interp816 *cpu);
/* Connected direct C control flow. Before may prepare mapped operands and
 * redirect PC; it must decline at host hooks or pending control work. After
 * observes each completed edge and must advance/check the real event clock.
 * Fallback runs only after preparation, for a changed live opcode/PC. */
unsigned ScProgramRun(Interp816 *cpu,void *context,ScProgramBefore before,
    ScProgramAfter after,ScProgramFallback fallback);
uint64_t ScProgramCalls(void);
uint64_t ScProgramCycles(void);
#ifdef SC_PROGRAM_TIER
unsigned ScProgramBank00(Interp816 *cpu);
unsigned ScProgramBank01(Interp816 *cpu);
unsigned ScProgramBank02(Interp816 *cpu);
unsigned ScProgramBank03(Interp816 *cpu);
unsigned ScProgramBank04(Interp816 *cpu);
unsigned ScProgramBank05(Interp816 *cpu);
#endif
#endif
