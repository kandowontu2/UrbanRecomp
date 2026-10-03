#pragma once
#include "sc_world.h"
typedef struct Interp816 Interp816;
bool ScTileLookupOwns(uint16_t pc);
/* Tile graphics lookup in C, resumable at the original beam/IRQ boundaries.
 * Returns original CPU clocks; zero means no state or bus changes. */
unsigned ScTileLookupStep(ScWorld *world,Interp816 *cpu,uint8_t *ram,
                         const uint8_t *rom,size_t size,unsigned max_cycles);

/* Execute exactly one original instruction, including its zero-clock world
 * hook. Zero leaves state untouched. Pending interrupts retain the CPU path. */
unsigned ScTileLookupInstructionStep(ScWorld *w,Interp816 *c,uint8_t *r,
    const uint8_t *rom,size_t size);
