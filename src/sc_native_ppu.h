#pragma once
#include "sc_terrain.h"
typedef struct Ppu Ppu;
bool ScNativePpuSupported(const Ppu *p);
/* Capture after the normal sprite evaluator, before any guest instruction.
 * This is a scanline snapshot, not a replay of the PPU or an end-frame copy. */
void ScNativePpuCapture(ScTerrainFrame *f,const Ppu *p,unsigned y,unsigned line);
/* Requires rows[y].chr_snapshot to name an immutable live VRAM version.
 * Falls back to the decoded capture if no complete version is available. */
void ScNativePpuCaptureRaw(ScTerrainFrame *f,const Ppu *p,unsigned y,unsigned line);
