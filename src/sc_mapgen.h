/* The game's map generation, decompiled. See sc_mapgen.c. */
#ifndef SC_MAPGEN_H_INCLUDED
#define SC_MAPGEN_H_INCLUDED

#include <stdint.h>

/* The map is 120 x 100 cells. Confirmed by the power bitmap at 03:b0f8, whose
 * CPX #$05dc bounds it at 1500 bytes = 12000 bits, one per cell. */
enum { SC_MAPGEN_W = 120, SC_MAPGEN_H = 100, SC_MAPGEN_CELLS = 12000 };
enum { SC_MAPGEN_LARGE_W = 240, SC_MAPGEN_LARGE_H = 200, SC_MAPGEN_MAX_CELLS = 12288000 };

/* $59 / $5b / $5d -- two 16-bit state words plus the temp the step writes. */
typedef struct ScMapGenPrng {
    uint16_t s0;   /* $59 */
    uint16_t s1;   /* $5b */
    uint16_t t;    /* $5d */
} ScMapGenPrng;

/* 00:823e -- seed from the vblank spin counter $c7. */
void sc_mapgen_prng_seed_from_spin(ScMapGenPrng *p, uint16_t spin_counter);

/* 00:824f -- one step; returns the new $5b, as the ROM returns it in A. */
extern unsigned long g_sc_mapgen_prng_steps;
extern unsigned long g_sc_mapgen_phase_steps[5];
extern unsigned long g_sc_mapgen_phase_cells[5];
extern unsigned long g_sc_mapgen_phase_changed[5];
extern unsigned long g_sc_mapgen_phase_cleared[5];
/* Snapshot the map after an exact draw count -- see the note in the .c. */
extern struct ScMapGenState *g_sc_mapgen_cur;
extern unsigned long g_sc_mapgen_snap_at;
extern int           g_sc_mapgen_snapped;
extern uint16_t      g_sc_mapgen_snap[SC_MAPGEN_CELLS];
uint16_t sc_mapgen_prng_step(ScMapGenPrng *p);

/* 03:d840 -- fold the three map-seed bytes in and pre-step the PRNG 1..32
 * times. `entry_carry` is a parameter and not an assumption: the ROL chain and
 * the ADC both consume the caller's carry, which the disassembly cannot tell
 * us. The verification harness settles it. */
void sc_mapgen_seed(ScMapGenPrng *p, uint16_t a_on_entry,
                    uint8_t seed0, uint8_t seed1, uint8_t seed2,
                    uint8_t prev0, unsigned entry_carry);

/* Generator working state. Named for the guest variables it mirrors. */
typedef struct ScMapGenState {
    uint16_t x0;      /* $0457 -- centre x, and its copy at $043b */
    uint16_t y0;      /* $0459 -- centre y, and its copy at $043d */
    uint16_t cur_x;   /* $043b -- the walking point */
    uint16_t cur_y;   /* $043d */
    uint16_t count;   /* $043f -- scatter loop counter */
    uint16_t px;      /* $044b -- placement x */
    uint16_t py;      /* $044d -- placement y */
    uint16_t dir_base; /* $045f -- the bearing the walk returns to */
    uint16_t dir_cur;  /* $0461 -- the wandering heading */
    uint16_t cx;      /* $043f -- cluster centre */
    uint16_t cy;      /* $0441 */
    /* The map itself, $7F0200 in the guest: 120 x 100 words, row-major. */
    uint16_t width, height;
    uint16_t map[SC_MAPGEN_MAX_CELLS];
} ScMapGenState;

/* 01:f877 -- 0..n inclusive, via the hardware multiplier. One PRNG step. */
uint16_t sc_mapgen_rand_below(ScMapGenPrng *p, uint16_t n);

/* 01:f380 -- pick a point in the middle third of each axis. Two PRNG steps. */
void sc_mapgen_feature_centre(ScMapGenPrng *p, ScMapGenState *st);

/* 01:f3a3 -- 50..150 placements across the full map. Its per-cell placement
 * ($f3d3) and the two $f502 calls are not decompiled yet. */
void sc_mapgen_feature_scatter(ScMapGenPrng *p, ScMapGenState *st);

/* 01:f5b9 -- walk from the centre one way, then the opposite way. The walk
 * itself ($f600) is not decompiled yet. One PRNG step. */
void sc_mapgen_feature_path(ScMapGenPrng *p, ScMapGenState *st);

/* 01:f311 -- 1..11 clusters of 2..14 blobs each, jittered +/-6 about the
 * cluster centre. The blob draws ($f71d, $f794) are not decompiled. */
void sc_mapgen_feature_clusters(ScMapGenPrng *p, ScMapGenState *st);

/* 01:f1f1 -- the generator. One PRNG step decides the path: 86/256 of maps go
 * to $f22c (not decompiled), the rest are built from the five features in a
 * fixed order. The order is part of the contract, since each consumes PRNG
 * steps. */
void sc_mapgen_generate(ScMapGenPrng *p, ScMapGenState *st);
/* One continuous terrain, twice the width and height. This API prepares the
 * host world; it must not be copied into the stock 24,000-byte WRAM buffer. */
void sc_mapgen_generate_large(ScMapGenPrng *p, ScMapGenState *st);
void sc_mapgen_generate_huge(ScMapGenPrng *p, ScMapGenState *st);
void sc_mapgen_generate_giant(ScMapGenPrng *p, ScMapGenState *st);
void sc_mapgen_generate_mega(ScMapGenPrng *p, ScMapGenState *st);
void sc_mapgen_generate_colossal(ScMapGenPrng *p, ScMapGenState *st);

/* 01:f843 -- the generator's own bounds test. Coordinates arrive jittered and
 * can be negative, so the signed check is load-bearing. */
int sc_mapgen_in_bounds(int x, int y);

/* 01:f8e9's row-offset computation: the map is row-major, stride 120. */
unsigned sc_mapgen_cell_index(unsigned x, unsigned y);

/* 01:f3d3 -- a 50..200 step random 8-way walk from the placement point,
 * stopping early if it leaves the map. Its PRNG cost is data-dependent. */
void sc_mapgen_walk(ScMapGenPrng *p, ScMapGenState *st);

/* 01:f6ae -- step one cell. Directions are a compass rose from N clockwise,
 * so dir ^ 4 is the reverse, which is what 01:f5b9 relies on. */
void sc_mapgen_move(ScMapGenState *st, unsigned dir);

/* 01:f71d -- stamp the 9x9 disc brush at $01f6cc. Values are 0 outside,
 * 3 rim, 1 interior, 2 the single centre cell. */
void sc_mapgen_stamp_blob(ScMapGenState *st);

/* 01:f7e7 -- write one brush value at (cur + offset). Existing 1 and 2 are
 * protected; a centre marker on the border degrades to 1. */
void sc_mapgen_draw_cell(ScMapGenState *st, unsigned brush, int ox, int oy);

/* 01:f8e9 / 01:f8af -- read and write a cell. The read masks to 10 bits and
 * the write does not; that asymmetry is the ROM's, not an oversight. */
uint16_t sc_mapgen_read_cell(const ScMapGenState *st, unsigned x, unsigned y);
void sc_mapgen_write_cell(ScMapGenState *st, unsigned x, unsigned y, uint16_t v);

/* 01:f794 -- the 6x6 disc, drawn 1 time in 4. Unlike the 9x9 it has no centre
 * marker at all. */
void sc_mapgen_stamp_blob_small(ScMapGenState *st);

/* 01:f600 -- stamp a 9x9 disc, wander +/-1 half the time, snap back to the
 * base bearing 1 step in 11, repeat until the disc would leave the map. */
void sc_mapgen_path_walk(ScMapGenPrng *p, ScMapGenState *st);
/* 01:f647 -- the narrow third walk. */
void sc_mapgen_path_walk_narrow(ScMapGenPrng *p, ScMapGenState *st);

/* 01:f444 -- sweep every cell, rewrite rim cells (3) as fitted edge tiles.
 * Column-major from the bottom-right, and it reads the buffer it writes. */
void sc_mapgen_shoreline(ScMapGenPrng *p, ScMapGenState *st);

/* 01:f502 -- second fitting pass over class 0x14..0x25. Unlike f444, a mask
 * bit means the neighbour is the SAME class and off-map sets nothing. Run
 * twice by the scatter feature, and it reads the buffer it writes. */
void sc_mapgen_fit_pass(ScMapGenPrng *p, ScMapGenState *st);

/* 01:f85d -- min of two draws below n. TWO PRNG steps. */
uint16_t sc_mapgen_rand_min2(ScMapGenPrng *p, uint16_t n);

/* 01:f22c -- the framed map: fill, clear the interior, march blobs along all
 * four edges. Taken by 86/256 of seeds instead of the feature chain. */
void sc_mapgen_framed_map(ScMapGenPrng *p, ScMapGenState *st);

/* Geographic generator used by new cities; the stock entry stays available
 * as a cartridge oracle. Size is 0..5 (120x100 through 3840x3200). */
void sc_mapgen_generate_geographic(ScMapGenPrng *p, ScMapGenState *st, unsigned size);
/* Fixed native preview, independent of city dimensions and zoom. Waterways
 * grow first, then forest patches; frame 90 is the exact completed overview. */
typedef struct ScMapPreview {
    uint8_t cells[SC_MAPGEN_CELLS], reveal[SC_MAPGEN_CELLS];
    unsigned frame;
    int active;
} ScMapPreview;
void sc_mapgen_preview_build(ScMapPreview *preview, const uint16_t *map,
                             unsigned width, unsigned height, unsigned seed);
unsigned sc_mapgen_preview_cell(const ScMapPreview *preview, unsigned x, unsigned y);

#endif
