#pragma once
#include <stdbool.h>
#include <stdint.h>
typedef struct Ppu Ppu;
enum { SC_OBJ_MAX_SLIVERS=1024, SC_OBJ_GPU_SLIVERS=90 };
/* Original fetch order: signed X/packed palette-priority, VRAM row address,
 * flip and clipped pixel interval. Planes retain CPU shadow semantics even
 * if VRAM changes after this scanline; Vulkan uses its immutable VRAM row. */
typedef struct { uint32_t position,source,planes; } ScObjSliver;
void ScObjBegin(Ppu *p);
void ScObjAppend(uint32_t position,uint32_t source,uint32_t planes);
void ScObjInvalidate(const Ppu *p);
bool ScObjSnapshot(const Ppu *p,const ScObjSliver **slivers,unsigned *count);
unsigned ScObjPixel(const Ppu *p,int x);
void ScObjMaterialize(const Ppu *p);
void ScObjCopyBuffer(Ppu *destination,const Ppu *source);
