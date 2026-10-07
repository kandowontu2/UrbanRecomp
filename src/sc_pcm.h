#pragma once
#include <stdbool.h>
#include <stdint.h>

/* Host soundtrack data, never part of the ROM or serialized guest state. */
typedef struct ScPcmTrack { uint8_t *data; uint32_t frames,loop; } ScPcmTrack;
typedef struct ScPcm {
    ScPcmTrack tracks[22]; /* 20/21 are host-only: never new SPC commands. */
    uint64_t position;
    unsigned track,rate,last_command,city_milestone;
    bool ready,enabled;
} ScPcm;
bool ScPcmLoad(ScPcm *pcm,const char *directory,unsigned output_rate);
void ScPcmDestroy(ScPcm *pcm);
/* Commands 1..19 select the same-numbered PCM and suppress SPC music.
 * Zero is the driver's idle acknowledgement; 20..255 remain SPC controls. */
uint8_t ScPcmCommand(ScPcm *pcm,uint8_t command);
void ScPcmCityMilestone(ScPcm *pcm,unsigned level);
void ScPcmMix(ScPcm *pcm,int16_t *stereo,unsigned frames);
