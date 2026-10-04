#pragma once
#include "sc_development.h"

/* Host-only scheduler. Completed zone attempts are immediately authoritative;
 * the index, route cache and timing credits are disposable on load. */
typedef struct ScDevelopmentBatches ScDevelopmentBatches;
ScDevelopmentBatches *ScDevelopmentBatchesCreate(void);
void ScDevelopmentBatchesDestroy(ScDevelopmentBatches *b);
void ScDevelopmentBatchesReset(ScDevelopmentBatches *b);
void ScDevelopmentBatchesObserve(ScDevelopmentBatches *b,const ScWorld *w,
    const Interp816 *cpu,const uint8_t *ram);
/* Process distant districts, visiting the stable zone index once per pass.
 * clock/frequency impose a host deadline; max_attempts bounds deterministic
 * tests. The native sweep retains census/traffic; this runner owns development. */
unsigned ScDevelopmentBatchesRun(ScDevelopmentBatches *b,ScWorld *w,uint8_t *ram,
    const uint8_t *rom,size_t size,uint64_t frame,int speed,unsigned max_attempts,
    uint64_t (*clock)(void),uint64_t frequency,double milliseconds,
    unsigned (*fallback)(Interp816 *));
unsigned ScDevelopmentBatchesZones(const ScDevelopmentBatches *b);
uint64_t ScDevelopmentBatchesAttempts(const ScDevelopmentBatches *b);
uint64_t ScDevelopmentBatchesPasses(const ScDevelopmentBatches *b);

unsigned ScDevelopmentBatchesLastQuadrants(const ScDevelopmentBatches *b);
