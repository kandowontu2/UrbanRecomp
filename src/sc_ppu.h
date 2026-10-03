#ifndef SC_PPU_H
#define SC_PPU_H
#include <stdbool.h>
#include <stdint.h>
/* Omit only final background/pixel composition on undisplayed Tab frames. */
void ScPpuSkipPixels(bool skip);
bool ScPpuPixelsSkipped(void);
/* Optional host clock: measure only the image work Tab can omit. */
void ScPpuMeasurePixels(uint64_t (*counter)(void),double milliseconds_per_tick);
double ScPpuPixelMilliseconds(void);
struct Ppu;
uint64_t ScPpuVramRevision(const struct Ppu *ppu);
void ScPpuVramChanged(void);
#endif
