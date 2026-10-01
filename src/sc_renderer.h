#pragma once
#include "sc_video.h"
#include "sc_selector.h"
#include "sc_titlesign.h"
#include "sc_vehicles.h"
#include "sc_world.h"
#include "sc_population.h"
#include <stdint.h>
#include <stddef.h>
typedef struct Ppu Ppu;
typedef struct ScRenderer {
    ScViewport view;
    ScViewport gameplay_view; /* configured HUD anchor; menus are centered */
    uint32_t *pixels;
    uint32_t *advisor_pixels;
    bool advisor_frame;
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
    bool split_hud, pan_frame;
    bool city_input, pointer_active;
    int pointer_x, pointer_y; /* full canvas position, relative to the native anchor */
    int light_slot, light_x, light_pitch;
    bool scroll_valid;
    int scroll_x, scroll_y, scroll_h, scroll_v, scroll_adjust_x, scroll_adjust_y, scroll_still;
    bool objects_valid;
    int16_t object_raw[128], object_x[128];
    uint16_t object_attr[128];
    uint8_t object_y[128], object_grace[128];
    Ppu *held_ppu;
    uint8_t held_map[SC_WORLD_TILE_BYTES], previous_map[SC_WORLD_TILE_BYTES];
    bool map_valid, map_hold, map_confirmed, map_dark;
    int map_quiet, map_age, held_x, held_y;
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
void ScRendererResetHistory(ScRenderer *r);
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
ScVideoRect ScRendererMinimapView(const ScRenderer *r, const uint8_t *ram);
bool ScRendererCityPoint(const ScRenderer *r, const uint8_t *ram,
                         int x, int y, int *world_x, int *world_y);
/* Draw extended values from the game's live OBJ digit/icon tiles. */
void ScRendererPopulationRow(const ScRenderer *r, const Ppu *ppu, ScViewport view,
                             bool split, int y, uint32_t *out);
