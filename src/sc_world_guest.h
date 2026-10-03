#pragma once
#include "sc_world.h"
#include "sc_stencil.h"
typedef struct Interp816 Interp816;
/* Fused smoothing cell/control primitives used by the connected C family. */
unsigned ScWorldGuestSmoothingStageStep(ScWorld *world,Interp816 *cpu,uint8_t *ram,unsigned max_cycles);
unsigned ScWorldGuestServiceStageStep(ScWorld *world,Interp816 *cpu,uint8_t *ram,unsigned max_cycles);
/* Whole ordered power operations for connected C traversal. Zero leaves the
 * CPU, RAM and world untouched; atomic continuations never call this tier. */
unsigned ScWorldGuestPowerStageStep(ScWorld *world,Interp816 *cpu,uint8_t *ram,
    const uint8_t *rom,unsigned max_cycles);
typedef struct ScWorldGuest {
    uint32_t address;
    uint8_t *data;
    unsigned bytes;
    bool mapped;
    uint8_t immediate[2];
    ScWorld *tile_world;
    unsigned tile_offset;
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
/* Added land must not multiply the time spent in the spatial simulation.
 * Retain the guest's calendar, budgets, scheduler and interrupt timing. */
unsigned ScWorldGuestMasterCycles(const ScWorld *world, uint32_t pc,
                                  unsigned master, unsigned *remainder);
/* Same clock classification used for native spans' beam/IRQ budgets. */
unsigned ScWorldGuestClockScale(const ScWorld *world,uint32_t pc);
/* Execute one verified spatial cell in C, returning its original CPU cost.
 * Zero means that the interpreter should execute the current opcode. */
unsigned ScWorldGuestFastStep(ScWorld *world, Interp816 *cpu, uint8_t *ram,
                             const uint8_t *rom, size_t size);

/* Native C spatial fields, coverage, growth scores and power searches, bounded by the
 * next beam event. Tight budgets use an interruptible compatibility path.
 * Original state and cycles are retained; zero falls back. */
unsigned ScWorldGuestKernelStep(ScWorld *world, Interp816 *cpu, uint8_t *ram,
    const uint8_t *rom, size_t size, unsigned max_cycles);
/* Fuse adjacent native cells and their loop control within one beam budget. */
unsigned ScWorldGuestBatchStep(ScWorld *world, Interp816 *cpu, uint8_t *ram,
    const uint8_t *rom, size_t size, unsigned max_cycles);
/* Original eight-neighbour free-house probe. Supports stock and host maps;
 * original stack/scratch bytes and clock are retained within the budget. */
unsigned ScWorldGuestHousingStep(ScWorld *world, Interp816 *cpu,uint8_t *ram,
    unsigned max_cycles);
/* Resumable free-house location scoring loop, preserving the ROM's ordered
 * coordinate reads, stack shadows and bounded instruction clock. */
unsigned ScWorldGuestHouseSiteStep(ScWorld *world, Interp816 *cpu,uint8_t *ram,
    const uint8_t *rom,size_t size,unsigned max_cycles);

/* Optional diagnostics for original opcodes executed INSIDE bounded helpers.
 * Host native-span dispatch counts alone cannot measure interpreter removal. */
void ScWorldGuestInterpreterProfile(bool enabled);
const uint64_t *ScWorldGuestInterpreterCounts(void);

/* Shared native per-direction transport lookup, preserving saved flags. */
unsigned ScWorldGuestTransportNeighborStep(ScWorld *world,Interp816 *cpu,
    uint8_t *ram,unsigned max_cycles);

/* Fuse complete tile statistics or pollution classification when its exact
 * branch cost fits. Interrupted stages remain in the resumable C land CFG. */
unsigned ScWorldGuestLandStageStep(ScWorld *world,Interp816 *cpu,
    uint8_t *ram,unsigned max_cycles);

/* Shared density kernels for the connected, resumable C pass. No recursive
 * entry into the program-counter dispatcher; zero leaves state untouched. */
unsigned ScWorldGuestDensityStageStep(ScWorld *world,Interp816 *cpu,
    uint8_t *ram,const uint8_t *rom,unsigned max_cycles);

/* Exactly one original instruction in a converted C family, with the CPU's
 * indivisible timing rule. Use after bounded batches reject a short deadline.
 * Caller verifies the US ROM; zero leaves CPU, RAM and world untouched. */
unsigned ScWorldGuestInstructionStep(ScWorld *world,Interp816 *cpu,uint8_t *ram,
    const uint8_t *rom,size_t size);

/* Optional asynchronous whole-field compute. Binding does not alter clocks;
 * unavailable/in-flight results retain the CPU path without a GPU wait. */
void ScWorldGuestSetStencilBackend(const ScStencilBackend *backend);
uint64_t ScWorldGuestStencilCells(void);

uint64_t ScWorldGuestServiceCells(void);
uint64_t ScWorldGuestServiceGpuCells(void);

/* Whole RCI artwork selection, ordered footprint update and frame restoration. */
/* Ordered shared zone redraw; keeps each validation/write scheduler boundary. */
unsigned ScWorldGuestZoneReplacementStep(ScWorld *world,Interp816 *cpu,uint8_t *ram,
    const uint8_t *rom,unsigned max_cycles);
unsigned ScWorldGuestZoneArtStep(ScWorld *world,Interp816 *cpu,uint8_t *ram,
    const uint8_t *rom,unsigned max_cycles);
/* Free-house artwork calls, final placement and frame restoration. Shared
 * quality/RNG helpers retain their own native scheduling boundaries. */
unsigned ScWorldGuestHouseArtStep(ScWorld *world,Interp816 *cpu,uint8_t *ram,
    const uint8_t *rom,unsigned max_cycles);
