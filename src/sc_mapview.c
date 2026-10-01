/* Host-side map renderer. Port of tools/render_map.py, which was validated
 * against real play before any of this was written -- the Python render and the
 * guest were confirmed to show the same area, and the host output was actually
 * *cleaner* than the guest's after a save-state load (docs/OPEN_QUESTIONS.md
 * E1). Keeping the Python around means this C can be diffed against a known-good
 * implementation rather than only against itself.
 *
 * Chain, from docs/REFERENCE_map_format.md:
 *
 *   v  = cell & 0x03FF                 10-bit tile index
 *   tp = u16(TILE_ADDR + v*2)          background tile
 *   tu = u16(TILU_ADDR + v*2)          overlay tile, 0x300 = empty, drawn -1,-1
 *   CHR: 4bpp SNES, tc & 0x3FF = tile, (tc >> 10) & 7 = palette
 *   palette: BGR555 at $7E2440
 */
#include <string.h>
#include <stdlib.h>

#include "snes/snes.h"
#include "snes/ppu.h"
#include "sc_mapview.h"
#include <stdio.h>

/* ARRAY, not a pointer -- debug_server.h has `extern uint8_t g_ram[]`.
 * Declaring it `uint8_t *g_ram` here linked without complaint (C externs carry
 * no type to the linker) and then read the first 8 bytes of WRAM AS a pointer
 * and dereferenced them. Instant segfault, and no build diagnostic anywhere.
 * Same family as the SDL float-vs-int pointer mismatch: it compiles, it links, it is
 * wrong. */
extern uint8_t g_ram[];     /* 128K WRAM: $7E0000 at 0, $7F0000 at 0x10000 */
extern Ppu     *g_ppu;
extern Snes    *g_snes;

/* WRAM offsets */
#define SC_MAP_OFF   0x010200u   /* $7F0200 */
#define SC_PAL_OFF   0x002440u   /* $7E2440 */
#define SC_SCROLL_X  0x0001bdu
#define SC_SCROLL_Y  0x0001bfu

/* ROM file offsets. USA image -- docs/REFERENCE_map_format.md notes the table
 * address is region-specific, and docs/REGIONS.md measured that E/F/G/J are a
 * different build entirely, so this is gated rather than assumed portable. */
#define SC_TILE_ADDR 0x0156A9u
#define SC_TILU_ADDR (SC_TILE_ADDR - 0x77Cu)

static const ScWorld *s_world;
#define SC_MAP_W (s_world && s_world->active?SC_WORLD_WIDTH:120)
#define SC_MAP_H (s_world && s_world->active?SC_WORLD_HEIGHT:100)

static bool s_rom_is_us;

void ScMapView_SetRomIsUs(bool is_us) { s_rom_is_us = is_us; }
void ScMapView_SetWorld(const ScWorld *world) { s_world=world; }

static uint16_t ram_u16(uint32_t off) {
    return (uint16_t)(g_ram[off] | (g_ram[off + 1] << 8));
}

/* A copy of the map and palette to draw instead of WRAM, while main.c holds
 * the margins on the city being replaced (see host_map_compose). */
static const uint8_t *s_map_src, *s_pal_src;

void ScMapView_SetSource(const uint8_t *map, const uint8_t *pal) {
    s_map_src = map;
    s_pal_src = pal;
}

void ScMapView_Snapshot(uint8_t *map, uint8_t *pal) {
    unsigned bytes=SC_MAP_W*SC_MAP_H*2;
    memcpy(map,s_world && s_world->active?s_world->tiles:g_ram+SC_MAP_OFF,bytes);
    memset(map+bytes,0,SC_MAPVIEW_MAP_BYTES-bytes);
    memcpy(pal, g_ram + SC_PAL_OFF, SC_MAPVIEW_PAL_BYTES);
}

int ScMapView_ChangedCells(const uint8_t *map) {
    int n = 0;
    for (unsigned i = 0; i < SC_MAP_W*SC_MAP_H*2; i += 2) {
        const unsigned a = (unsigned)(map[i] | (map[i + 1] << 8)) & 0x03FFu;
        const unsigned b = (s_world && s_world->active?
          (s_world->tiles[i]|(s_world->tiles[i+1]<<8)):ram_u16(SC_MAP_OFF+i))&0x03ff;
        n += a != b;
    }
    return n;
}

static uint16_t map_u16(uint32_t i) {
    if (s_map_src) return (uint16_t)(s_map_src[i] | (s_map_src[i + 1] << 8));
    if (s_world && s_world->active) return s_world->tiles[i]|(s_world->tiles[i+1]<<8);
    return ram_u16(SC_MAP_OFF + i);
}

static uint16_t pal_u16(uint32_t i) {
    if (s_pal_src) return (uint16_t)(s_pal_src[i] | (s_pal_src[i + 1] << 8));
    return ram_u16(SC_PAL_OFF + i);
}

void ScMapView_GetScroll(int *sx, int *sy) {
    if (sx) *sx = (int16_t)ram_u16(SC_SCROLL_X);
    if (sy) *sy = (int16_t)ram_u16(SC_SCROLL_Y);
}

/* BGR555 -> ARGB8888, through the PPU's master-brightness table.
 *
 * The brightness step is not cosmetic. The guest's own layers are composited by
 * the PPU and therefore dim during a fade; a host map converted raw does not,
 * so a fade left the menu dimming over a map that stayed at full brightness.
 * Worse, the host composite keys transparency on the backdrop colour, which IS
 * computed through brightnessMult -- so as the fade progressed the key drifted
 * out of step with the map and pixels flipped in and out. Reported from play as
 * the VOICE/HISTORY fade showing tiles visibly changing while the end result
 * was still correct.
 *
 * Using the same table the PPU uses keeps both sides on one scale. */
static uint32_t pal_entry(unsigned index) {
    uint16_t v = pal_u16((index & 0xFFu) * 2u);
    const uint8_t *bm = g_ppu->brightnessMult;
    uint32_t r = bm[v & 31u];
    uint32_t g = bm[(v >> 5) & 31u];
    uint32_t b = bm[(v >> 10) & 31u];
    return 0xFF000000u | (r << 16) | (g << 8) | b;
}

/* One 4bpp SNES tile -> 64 palette indices. VRAM is uint16 words; a tile is 32
 * bytes = 16 words, bitplanes 0/1 then 2/3. */
static void decode_tile(unsigned tile, uint8_t out[64]) {
    const uint16_t *vram = g_ppu->vram;
    unsigned base = (tile & 0x3FFu) * 16u;      /* in words */
    if (base + 16u > 0x8000u) { memset(out, 0, 64); return; }
    for (int y = 0; y < 8; y++) {
        uint16_t lo = vram[base + (unsigned)y];        /* planes 0,1 */
        uint16_t hi = vram[base + 8u + (unsigned)y];   /* planes 2,3 */
        uint8_t p0 = (uint8_t)(lo & 0xFF), p1 = (uint8_t)(lo >> 8);
        uint8_t p2 = (uint8_t)(hi & 0xFF), p3 = (uint8_t)(hi >> 8);
        for (int x = 0; x < 8; x++) {
            int bit = 7 - x;
            out[y * 8 + x] = (uint8_t)(((p0 >> bit) & 1)
                                     | (((p1 >> bit) & 1) << 1)
                                     | (((p2 >> bit) & 1) << 2)
                                     | (((p3 >> bit) & 1) << 3));
        }
    }
}

static int s_cell_px = 8;   /* 8 = native */

void ScMapView_SetCellPx(int px) {
    if (px < 1) px = 1;
    if (px > 64) px = 64;
    s_cell_px = px;
}
int ScMapView_GetCellPx(void) { return s_cell_px; }

/* Draw one 8x8 tile scaled to cell_px, nearest-neighbour in both directions so
 * a single path serves zoom-in and zoom-out. Source pixel is chosen per
 * destination pixel, which keeps zoom-out honest: it samples rather than
 * pretending to average, and never reads outside the tile. */
static void blit_tile(uint8_t *out, int pitch, int w, int h,
                      unsigned tc, int px, int py, int cell) {
    uint8_t pix[64];
    decode_tile(tc, pix);
    unsigned pbase = ((tc >> 10) & 7u) * 16u;
    for (int dy = 0; dy < cell; dy++) {
        int oy = py + dy;
        if (oy < 0 || oy >= h) continue;
        int ty = dy * 8 / cell;
        uint32_t *row = (uint32_t *)(out + (size_t)oy * pitch);
        for (int dx = 0; dx < cell; dx++) {
            int ox = px + dx;
            if (ox < 0 || ox >= w) continue;
            uint8_t ci = pix[ty * 8 + (dx * 8 / cell)];
            if (ci == 0) continue;              /* transparent */
            row[ox] = pal_entry(pbase + ci);
        }
    }
}

bool ScMapView_Render(uint8_t *out, int pitch, int cols, int rows,
                      int sx, int sy) {
    if (!out || !g_ppu || !g_snes || !g_snes->cart ||
        !g_snes->cart->rom || !s_rom_is_us) return false;

    const uint8_t *rom = g_snes->cart->rom;
    /* cols/rows are the caller's cell budget at native scale; the actual
     * surface is whatever it asked for, so derive the drawn extent from the
     * current cell size and widen the cell budget to fill it when zoomed out. */
    const int cell = s_cell_px;
    const int w = cols * 8, h = rows * 8;
    cols = (w + cell - 1) / cell;
    rows = (h + cell - 1) / cell;

    for (int y = 0; y < h; y++)
        memset(out + (size_t)y * pitch, 0, (size_t)w * 4);

    /* Backgrounds first, then overlays at -1,-1 so they overlap up and left. */
    for (int pass = 0; pass < 2; pass++) {
        for (int ry = 0; ry < rows; ry++) {
            int my = sy + ry;
            if (my < 0 || my >= SC_MAP_H) continue;
            for (int rx = 0; rx < cols; rx++) {
                int mx = sx + rx;
                if (mx < 0 || mx >= SC_MAP_W) continue;
                unsigned v = map_u16((uint32_t)((my * SC_MAP_W + mx) * 2)) & 0x03FFu;
                if (pass == 0) {
                    unsigned tp = (unsigned)(rom[SC_TILE_ADDR + v * 2] |
                                             (rom[SC_TILE_ADDR + v * 2 + 1] << 8));
                    blit_tile(out, pitch, w, h, tp, rx * cell, ry * cell, cell);
                } else {
                    unsigned tu = (unsigned)(rom[SC_TILU_ADDR + v * 2] |
                                             (rom[SC_TILU_ADDR + v * 2 + 1] << 8));
                    /* SC_ROOF_DIAG: is the overlay pass drawing anything at all,
                     * and what do the cell ids and table entries actually look
                     * like? An earlier note recorded "0 drawn, 31360 skipped". */
                    static int roof_diag = -1;
                    if (roof_diag < 0) {
                        const char *e = getenv("SC_ROOF_DIAG");
                        roof_diag = (e && *e) ? 1 : 0;
                    }
                    if (roof_diag) {
                        static int tot, skip, drawn, vmin = 0x7fff, vmax = -1;
                        static int tumin = 0x7fff, tumax = -1, nf;
                        tot++;
                        if ((int)v < vmin) vmin = (int)v;
                        if ((int)v > vmax) vmax = (int)v;
                        if ((int)(tu & 0x3FFu) < tumin) tumin = (int)(tu & 0x3FFu);
                        if ((int)(tu & 0x3FFu) > tumax) tumax = (int)(tu & 0x3FFu);
                        if ((tu & 0x3FFu) == 0x300u) skip++; else drawn++;
                        if (tot % 20000 == 0)
                            fprintf(stderr,
                                    "[roof] tot=%d drawn=%d skipped=%d  v=%d..%d  tu&3ff=%d..%d\n",
                                    tot, drawn, skip, vmin, vmax, tumin, tumax);
                        (void)nf;
                    }
                    if ((tu & 0x3FFu) == 0x300u) continue;
                    /* One CELL up and left, not one pixel.
                     *
                     * The format note at the top of this file, and
                     * REFERENCE_map_format.md, both say the overlay is drawn at
                     * -1,-1 -- in CELLS. This shifted by cell/8, which at the
                     * native cell size of 8 is a single pixel, leaving every
                     * tall building's upper half 7 px too low. Reported from
                     * play as the roof tiles sitting about a tile below where
                     * they belong. */
                    blit_tile(out, pitch, w, h, tu, rx * cell - cell,
                              ry * cell - cell, cell);
                }
            }
        }
    }
    return true;
}
