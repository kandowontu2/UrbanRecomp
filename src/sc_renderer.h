#pragma once
#include "sc_video.h"
#include "sc_selector.h"
#include "sc_titlesign.h"
#include "sc_vehicles.h"
#include "sc_world.h"
#include "sc_population.h"
#include "sc_terrain.h"
#include <stdint.h>
#include <stddef.h>
typedef struct Ppu Ppu;
typedef struct {
    const uint8_t *map;
    int first,y;
    unsigned width,height,count,offset;
    bool owner;
} ScCityRowCache;
enum { SC_RENDER_OBJECTS, SC_RENDER_TERRAIN, SC_RENDER_ROWS, SC_RENDER_NATIVE,
       SC_RENDER_REPAIR, SC_RENDER_HUD, SC_RENDER_POINTER, SC_RENDER_TRACK, SC_RENDER_STAGES };
typedef struct ScRenderer {
    /* Optional host-only stage profiling; unset in ordinary gameplay/tests. */
    uint64_t (*measure_clock)(void);
    uint64_t measure_ticks[SC_RENDER_STAGES];
    uint64_t (*vram_revision)(const Ppu *);
    uint64_t captured_vram_revision;
    const Ppu *captured_vram_source;
    ScCityRowCache city_cache[6];
    unsigned city_cache_next;
    /* Immutable two-dimensional OBJ grid, shared by rows until its live
     * admission/attributes change. VRAM remains versioned independently. */
    bool object_grid_valid;
    unsigned object_grid_header,object_grid_key[6];
    uint16_t object_grid_oam[256];
    uint8_t object_grid_high[32],object_grid_y[128],object_grid_grace[128];
    int16_t object_grid_x[128];
    ScVehicleSprite object_grid_vehicles[19];
    ScViewport view;
    ScViewport gameplay_view; /* configured HUD anchor; menus are centered */
    double map_zoom;
    bool zoom_frame, zoom_hud;
    uint32_t *pixels;
    ScTerrainFrame terrain;
    bool defer_terrain;
    bool native_line; /* this row's native pixels were delegated to Vulkan */
    bool reference_terrain; /* exact per-pixel oracle for regression/profiling */
    uint32_t *advisor_pixels;
    bool advisor_frame, city_overlay_frame;
    size_t capacity;
    const uint8_t *rom;
    size_t rom_size;
    bool rom_is_us;
    const ScWorld *world;
    const ScPopulation *population;
    int wood_layer, wood_period;
    uint16_t wood_rows[32];
    bool title_live;
    bool city_frame;
    bool split_hud, pan_frame, mouse_panning, mouse_minimap_frame;
    bool city_input, pointer_active, pointer_hud, pointer_hidden;
    bool clipboard_cursor;
    bool clipboard_font_valid;
    uint8_t clipboard_font[128][16]; /* original 8x8 adviser lettering */
    int pointer_x, pointer_y; /* full canvas position, relative to the native anchor */
    int light_slot, light_x, light_pitch;
    bool scroll_valid;
    bool scroll_repair; /* validate staged city tiles until one complete frame agrees */
    unsigned staged_mismatches;
    int scroll_x, scroll_y, scroll_h, scroll_v, scroll_adjust_x, scroll_adjust_y, scroll_still;
    int native_scroll_x,native_scroll_y;
    double camera_x,camera_y; /* host view offset; guest simulation stays untouched */
    bool objects_valid;
    int16_t object_raw[128], object_x[128];
    uint16_t object_attr[128];
    uint8_t object_y[128], object_grace[128];
    Ppu *held_ppu;
    uint8_t held_map[SC_WORLD_MAX_TILE_BYTES], previous_map[SC_WORLD_MAX_TILE_BYTES];
    uint8_t changed_cells[SC_WORLD_MAX_CELLS]; /* committed cells bypass stale native tile staging */
    uint64_t map_revisions[SC_WORLD_TILE_CHUNKS];
    unsigned map_bytes;
    bool map_valid, map_hold, map_dark, held_large, held_huge, held_giant, held_colossal, held_mega;
    int map_age, held_x, held_y;
    uint8_t repaired_edges[224]; /* per row: bit 0 left 8 px, bit 1 right */
    bool sylt;                   /* the ninth scenario card is on */
    /* The vehicles kept for the margin (src/sc_vehicles.c), handed in by the
     * host before each frame's first line. */
    ScVehicleSprite vehicles[19];
    int vehicle_count;
    ScSelSprite selector[SC_SEL_MAX_SPRITES]; /* pins and marks, this frame */
    int selector_count;
    ScSelSprite sign[SC_SIGN_MAX_SPRITES];    /* the title's SIMCITY sign */
    int sign_count;
    const uint16_t *selector_row; /* render_row's current row, for scenery */
} ScRenderer;
void ScRendererInit(ScRenderer *r, const uint8_t *rom, size_t size, bool is_us);
bool ScRendererResize(ScRenderer *r, ScViewport view);
void ScRendererDestroy(ScRenderer *r);
bool ScRendererDeferTerrain(ScRenderer *r,bool enabled);
bool ScRendererDeferNativeLine(ScRenderer *r,const Ppu *p,const uint8_t *ram,int line);
uint32_t ScRendererPixel(const ScRenderer *r,int x,int y);
uint32_t ScRendererPresentationPixel(const ScRenderer *r,unsigned x,unsigned y,
    unsigned width,unsigned height);
void ScRendererResetHistory(ScRenderer *r);
void ScRendererPan(ScRenderer *r,double dx,double dy);
void ScRendererResetCamera(ScRenderer *r);
/* Only an actual guest city-load entry may freeze the previous terrain. */
void ScRendererBeginMapLoad(ScRenderer *r);
/* Called AFTER the stock PPU has drawn a line, BEFORE the guest advances.
 * Reads only: no guest writes, PPU replay, state forcing, or hidden frames. */
void ScRendererLine(ScRenderer *r, const Ppu *ppu, const uint8_t *ram,
                    int line, const uint32_t *native);
/* Public for ROM-free edge/flip/bounds tests and captured-frame oracles. */
uint32_t ScRendererMapPixel(const ScRenderer *r, const Ppu *ppu,
                            const uint8_t *ram, int world_x, int world_y);
bool ScRendererWindowToGuest(const ScRenderer *r, ScVideoRect destination,
                            int window_w, int window_h, int drawable_w, int drawable_h,
                            double x, double y, int *guest_x, int *guest_y,
                            bool *navigation);
void ScRendererProjectCity(const ScRenderer *r,double *x,double *y);
ScVideoRect ScRendererMinimapView(const ScRenderer *r, const uint8_t *ram);
bool ScRendererCityPoint(const ScRenderer *r, const uint8_t *ram,
                         int x, int y, int *world_x, int *world_y);
/* Draw extended values from the game's live OBJ digit/icon tiles. */
void ScRendererPopulationRow(const ScRenderer *r, const Ppu *ppu, ScViewport view,
                             bool split, int y, uint32_t *out);
void ScRendererClipboardFont(ScRenderer *r,const uint8_t *font,size_t size);
ScVideoRect ScRendererClipboardButton(ScViewport view,unsigned button);
void ScRendererClipboardRow(const ScRenderer *r,const Ppu *ppu,ScViewport view,
    unsigned tool,bool available,uint64_t price,int y,uint32_t *out);
/* Restore the live HUD hand above host-added COPY/PASTE labels. */
void ScRendererHudPointer(ScRenderer *r,const Ppu *ppu);
