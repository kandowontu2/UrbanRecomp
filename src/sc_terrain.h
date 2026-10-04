#pragma once
#include <stdbool.h>
#include <stdint.h>

/* A deferred pixel is distinct from both the native PPU's zero alpha and
 * the compositor's opaque alpha. Later HUD/cursor writes override it. */
#define SC_TERRAIN_PIXEL UINT32_C(0x01000000)
#define SC_NATIVE_PIXEL UINT32_C(0x02000000)
/* A relocated native pixel retains its source scanline and local column.
 * Twelve row bits cover the maximum canvas; opaque ARGB cannot alias it. */
#define SC_RELOCATED_NATIVE_PIXEL UINT32_C(0x03000000)
#define SC_IS_RELOCATED_NATIVE(p) (((p) >> 24) == 3)
#define SC_OVERLAY_NATIVE_BG UINT32_C(0x40000000)
#define SC_OVERLAY_SKIP_BG1 UINT32_C(0x20000000)
#define SC_ROW_ADVISOR_BACKGROUND UINT32_C(0x100)
#define SC_ROW_ADVISOR_CORE UINT32_C(0x200)
#define SC_ROW_NATIVE_REPAIR UINT32_C(0x400)
#define SC_ROW_STAGING_CHECK UINT32_C(0x800)
#define SC_ROW_CITY_HUD UINT32_C(0x1000)
#define SC_ROW_HUD_TOP UINT32_C(0x2000)
#define SC_ROW_RAW_TERRAIN UINT32_C(0x4000)
#define SC_ROW_CITY_SPANS UINT32_C(0x8000)
#define SC_ROW_GPU_OBJECTS UINT32_C(0x10000)
#define SC_ROW_OBJECT_CORE UINT32_C(0x20000)
#define SC_ROW_GPU_SUB_BG UINT32_C(0x40000)
#define SC_ROW_OBJECT_GRID UINT32_C(0x80000)
#define SC_ROW_CITY_ZOOM UINT32_C(0x100000)
#define SC_TILE_EDITED UINT32_C(0x40000)
#define SC_TILE_POWER_WARNING UINT32_C(0x80000)
#define SC_TILE_VALID UINT32_C(0x100000)
#define SC_TILE_CLEAR_WARNING UINT32_C(0x200000)
#define SC_NATIVE_RAW_BG UINT32_C(4)
#define SC_NATIVE_RAW_OBJ UINT32_C(8)

/* Native Mode 1 layers retain their independent scroll phase and live CHR
 * planes. Thirty-three tiles cover 256 columns at every fine-scroll phase. */
typedef struct {uint32_t planes,attributes;} ScNativeTile;
typedef struct {
    uint32_t phase[3],flags;
    union {
        ScNativeTile tiles[3][33];
        /* Raw layers: aligned horizontal scroll, vertical coordinate,
         * tilemap base, CHR base, and width/height flags. The remaining
         * words are unused. Keeping the stride preserves the GPU ABI. */
        uint32_t layer[3][66];
    };
} ScNativeRow;
/* Repair metadata follows the same eight-pixel world spans as the CHR.
 * expected/staged pack BG2 in the low word and BG1 in the high word. */
typedef struct {uint32_t base,roof,attributes,reserved,expected,staged;} ScTerrainTile;
/* Live native background candidates and evaluated sprite ownership. Native
 * repaired pixels use ranked BG1/BG3; margins use their existing OBJ order. */
typedef struct {uint32_t background,object;} ScTerrainOverlay;
typedef struct {
    uint32_t phase,core_x,main,sub,window_main,window_sub,windows,logic,bounds;
    uint32_t math,control,fixed; /* math's upper bits hold SC_ROW_* host policies */
    /* GPU_SUB_BG uses the first five sub_bg words for BG3's live horizontal
     * phase, vertical coordinate, map/CHR bases and map dimensions. Otherwise
     * this remains the reference decoder's 256 palette-index samples. */
    uint32_t brightness[32],sub_bg[256];
    /* world_y low three bits: CHR row; bits 8..23: signed canvas row for
     * OBJECT_GRID. Keeping the packed row ABI avoids another GPU buffer. */
    uint32_t chr_snapshot,chr_base,warning_base,world_y;
    uint32_t city_base,city_roof,city_owner,reserved; /* reserved: OBJ bucket stream word offset */
    uint32_t zoom_step;
    uint32_t zoom_fraction;
    int32_t zoom_x; /* virtual OBJ canvas coordinate at output column zero */
} ScTerrainRow;
typedef struct {
    unsigned width,height,stride;
    ScTerrainTile *tiles;
    uint32_t *palette;
    ScTerrainRow *rows;
    unsigned deferred;
    ScTerrainOverlay *overlays;
    ScNativeRow *native;
    /* ROM cell descriptors followed by immutable packed VRAM versions. A row
     * names its live version; later DMA cannot change earlier scanlines. */
    uint32_t *resources;
    unsigned resource_capacity,snapshots;
    /* Packed 16-bit immutable city rows, shared by scanlines until the live
     * bytes change. City offsets count cells. OBJ offsets count uint32 words:
     * bucket count, then (record offset, count) per 32px bucket, then six-word
     * records (signed left, size, row, attributes, CHR base, signed clip left).
     * OBJECT_GRID stores eight wrapped Y bands after the column-count word;
     * records store signed top instead of row, and attribute bit 16 selects
     * native wrapped Y. Scanline world_y supplies the signed target row.
     * UINT32_MAX denotes an empty OBJ row. Records retain original draw order. */
    uint32_t *city;
    unsigned city_capacity,city_words;
} ScTerrainFrame;
bool ScTerrainResize(ScTerrainFrame *f,unsigned width,unsigned height);
bool ScTerrainSpanWidth(ScTerrainFrame *f,unsigned width);
void ScTerrainDestroy(ScTerrainFrame *f);
uint32_t ScTerrainPixel(const ScTerrainFrame *f,unsigned x,unsigned y);
uint32_t ScNativePixel(const ScTerrainFrame *f,unsigned x,unsigned y);
ScNativeTile ScTerrainNativeTile(const ScTerrainFrame *f,unsigned y,unsigned layer,unsigned column);
unsigned ScTerrainNativeObject(const ScTerrainFrame *f,unsigned y,unsigned x);
uint32_t ScRelocatedNativePixel(const ScTerrainFrame *f,uint32_t pixel);
ScTerrainTile ScTerrainResolveTile(const ScTerrainFrame *f,unsigned y,ScTerrainTile tile);
ScTerrainTile ScTerrainRawTile(const ScTerrainFrame *f,unsigned y,unsigned column);
