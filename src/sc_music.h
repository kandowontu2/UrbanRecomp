#pragma once
#include "sc_sdl_compat.h"
#include <stdbool.h>
#include <stdint.h>
typedef struct Apu Apu;
/* The live SPC/DSP and output queue share one short-held mutex. Guest port
 * writes retain their timestamps; the worker independently fills playback. */
bool ScMusicStart(Apu *apu,ScAudio *output);
/* Load optional local tracks before opening audio. All 19 must validate.
 * The worker owns mixing; gameplay only submits commands under its mutex. */
bool ScMusicLoadRestored(const char *directory);
uint8_t ScMusicCommand(uint8_t command);
void ScMusicEnabled(bool enabled);
void ScMusicCityMilestone(unsigned level);
void ScMusicRestoreLocked(uint8_t command,bool enabled);
void ScMusicStop(void);
bool ScMusicRunning(void);
void ScMusicLock(void);
void ScMusicUnlock(void);
void ScMusicSyncLocked(uint64_t guest_cycle);
void ScMusicWrite(uint8_t port,uint8_t value,uint64_t guest_cycle);
void ScMusicGuestFrame(uint64_t guest_cycle,bool paused);
void ScMusicResetLocked(void);
typedef struct ScMusicStats {
    uint64_t cycles,samples,blocks,write_failures,nonzero_samples;
} ScMusicStats;
ScMusicStats ScMusicGetStats(void);
