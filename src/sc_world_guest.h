#pragma once
#include "sc_world.h"
typedef struct Interp816 Interp816;
typedef struct ScWorldGuest {
    uint32_t address;
    uint8_t *data;
    unsigned bytes;
    bool mapped;
    uint8_t immediate[2];
} ScWorldGuest;

/* Hooks for the verified US interpreter. Bind one DATA access at a time,
 * never a bank-wide memory range: instruction fetches, graph histories and
 * DMA still access native memory even when host arrays have the same offsets. */
void ScWorldGuestStep(ScWorld *world, Interp816 *cpu, uint8_t *ram);
void ScWorldGuestVehicles(ScWorld *world, Interp816 *cpu, const uint8_t *ram, uint8_t multiply_y);
void ScWorldGuestBegin(ScWorldGuest *guest, ScWorld *world,
                       const Interp816 *cpu, const uint8_t *rom, size_t size);
bool ScWorldGuestRead(const ScWorldGuest *guest, uint32_t address, uint8_t *value);
bool ScWorldGuestWrite(ScWorldGuest *guest, uint32_t address, uint8_t value);
