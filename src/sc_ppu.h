#ifndef SC_PPU_H
#define SC_PPU_H
#include <stdbool.h>
#include <stdint.h>
/* Omit only final background/pixel composition on undisplayed Tab frames. */
void ScPpuSkipPixels(bool skip);
bool ScPpuPixelsSkipped(void);
/* Native Vulkan lines decode OBJ on the GPU; undisplayed Tab lines retain
 * fetch/overflow state without constructing sprite pixels. */
void ScPpuDeferObjects(bool defer);
void ScPpuSkipObjects(bool skip);
/* Optional host clock: measure only the image work Tab can omit. */
void ScPpuMeasurePixels(uint64_t (*counter)(void),double milliseconds_per_tick);
double ScPpuPixelMilliseconds(void);
struct Ppu;
uint64_t ScPpuVramRevision(const struct Ppu *ppu);
void ScPpuVramChanged(void);
#endif
