#pragma once
#include <stdbool.h>
#include <stdint.h>

/* Disposable host clock: native visits establish the Normal cadence. Twice
 * the period retains half-frame averages; credits preserve fractional rates. */
typedef struct {
    uint64_t budget, native_frame, previous_gap, frame, credit;
    int speed;
    bool observed, started;
} ScRefreshClock;
void ScRefreshClockReset(ScRefreshClock *c, unsigned normal_frames);
void ScRefreshClockObserve(ScRefreshClock *c, uint64_t frame);
bool ScRefreshClockDue(ScRefreshClock *c, uint64_t frame, int multiplier);
