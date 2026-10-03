#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "sc_world.h"

typedef struct ScDevelopment {
    uint16_t entry, end, capacity, skip, attempt, cell, dp, sp;
    int remaining;
    bool repeating;
    uint64_t attempts, extra_attempts;
} ScDevelopment;

/* Called before each bank-03 interpreter opcode in the verified US game.
 * Returns a replacement PC; when it changes the caller must select 16-bit
 * A/X/Y (the same as the handler's REP #$30). Normal is an exact no-op. */
uint16_t ScDevelopmentStep(ScDevelopment *state, uint8_t *ram,
                           uint16_t pc, uint16_t dp, uint16_t sp, int speed);
uint16_t ScDevelopmentStepWorld(ScDevelopment *state, const ScWorld *world, uint8_t *ram,
                           uint16_t pc, uint16_t dp, uint16_t sp, int speed);
void ScDevelopmentReset(ScDevelopment *state);
typedef struct Interp816 Interp816;
/* Native C zone-capacity and simulation random kernels. Return the original
 * cycle cost at the compatibility boundary, or zero for an unhandled entry. */
unsigned ScDevelopmentNativeStep(Interp816 *cpu,uint8_t *ram);
/* Ordered capacity, growth/decline decisions and empty-house control for
 * extra attempts; yield before unmigrated map mutations. */
unsigned ScDevelopmentNativeBatch(ScDevelopment *state,ScWorld *world,
                                 Interp816 *cpu,uint8_t *ram,const uint8_t *rom,size_t rom_size);
