#ifndef SC_CONSTRUCTION_H
#define SC_CONSTRUCTION_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "sc_world.h"

typedef struct { int x, y; } ScBuildCell;
enum { SC_BUILD_MAX = SC_WORLD_MAX_CELLS };
typedef struct {
  unsigned tool, count;
  ScBuildCell cells[SC_BUILD_MAX];
} ScBuildPlan;
typedef enum { SC_BUILD_OK, SC_BUILD_FUNDS, SC_BUILD_INVALID, SC_BUILD_FAULT } ScBuildResult;
bool ScConstructionPlan(ScBuildPlan *plan, unsigned tool, int x0, int y0, int x1, int y1);
bool ScConstructionPlanWorld(ScBuildPlan *plan, const ScWorld *world, unsigned tool,
                             int x0, int y0, int x1, int y1);
/* Run original US placement routines in private WRAM, then commit once.
 * Caller must use the idle city input boundary, never an active sim routine. */
ScBuildResult ScConstructionCommit(uint8_t *ram, const uint8_t *rom, size_t size,
                                  const ScBuildPlan *plan, unsigned *cost);
ScBuildResult ScConstructionCommitWorld(uint8_t *ram, ScWorld *world,
    const uint8_t *rom, size_t size, const ScBuildPlan *plan, unsigned *cost);
/* Resumable atomic placement. The plan remains immutable until Free.
 * Step bounds private CPU work; Finish publishes only a successful transaction. */
typedef struct ScBuildWork ScBuildWork;
ScBuildWork *ScConstructionBegin(const uint8_t *ram,const ScWorld *world,const uint8_t *rom,size_t size,const ScBuildPlan *plan);
bool ScConstructionStep(ScBuildWork *work,unsigned max_operations);
unsigned ScConstructionCompleted(const ScBuildWork *work);
ScBuildResult ScConstructionFinish(ScBuildWork *work,uint8_t *ram,ScWorld *world,unsigned *cost);
void ScConstructionFree(ScBuildWork *work);
/* Original ordered power rules in C; commits power bits only. */
bool ScConstructionRefreshPower(uint8_t *ram, ScWorld *world,
                               const uint8_t *rom, size_t size);
/* Ordered native power kernel, without publishing into live guest state. */
bool ScConstructionPowerBitmap(const uint8_t *ram,const ScWorld *world,
    const uint8_t *rom,size_t size,uint8_t *bitmap,size_t bitmap_size);
/* Interpreter oracle for differential verification of the ordered kernel. */
bool ScConstructionPowerBitmapReference(const uint8_t *ram,const ScWorld *world,
    const uint8_t *rom,size_t size,uint8_t *bitmap,size_t bitmap_size);
/* Initialize generated stock-district spatial fields with the original scans. */
bool ScConstructionPrimeFields(uint8_t *ram,const uint8_t *rom,size_t size);
/* Initialize the actual generated world, including native park/gift diffusion. */
bool ScConstructionPrimeWorldFields(uint8_t *ram,ScWorld *world,const uint8_t *rom,size_t size);
#endif
