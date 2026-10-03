#pragma once
#include "sc_construction.h"

typedef struct ScClipboard {
    uint16_t *tiles;
    uint8_t *mask;
    int width, height, source_x, source_y;
    unsigned count, excluded;
    uint64_t price;
} ScClipboard;

void ScClipboardClear(ScClipboard *clipboard);
/* Rectangle plus whole intersecting ordinary objects. Reward footprints are
 * holes in the selection: neither their tiles nor their price are copied. */
bool ScClipboardCopy(ScClipboard *clipboard,const uint8_t *ram,const ScWorld *world,
    const uint8_t *rom,size_t size,int x0,int y0,int x1,int y1);
bool ScClipboardFits(const ScClipboard *clipboard,const uint8_t *ram,const ScWorld *world,
    const uint8_t *rom,size_t size,int x,int y);
/* All checks precede writes. Occupied destinations are rejected atomically;
 * terrain can be replaced. The caller refreshes power/census at the idle city
 * boundary after success. No guest scheduler, RNG, gift inventory or clock is
 * changed by this transaction. */
ScBuildResult ScClipboardPaste(const ScClipboard *clipboard,uint8_t *ram,ScWorld *world,
    const uint8_t *rom,size_t size,int x,int y);
