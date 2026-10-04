/* Vehicles in the city view's widescreen margin.
 *
 * The ship, the train, the plane and the game's other moving objects are real
 * objects with map positions, not screen sprites. The game puts them on
 * screen every fourth frame, one group at a time, in 00:bc3f:
 *
 *   00:c0f5 / 00:c154             park the group's OAM slots (Y = $E0; c154
 *                                 falls into c180, which parks 124-127
 *                                 unless $0B03 gives them to an icon)
 *   00:bf80 and 00:be1c           for each live object, one call per 16x16
 *                                 sprite of it to
 *   00:c019                       place a sprite at map cell ($91, $94) plus
 *                                 the object's fine offset ($0A6D,Y / $0A6B,Y),
 *                                 relative to the view's top-left cell
 *                                 ($01BD, $01BF), into slot $0B33 / 4
 *
 * c019 drops every sprite whose cell lies 32 or more columns right of the
 * view's left edge -- screen x 256 and beyond -- and leaves its slot parked.
 * So once an object leaves the authentic 256 columns there is nothing in OAM
 * for any renderer to show, which is why the margin lost the train, the plane
 * and the ship (docs/ROM_MAP.md, "01:f11a").
 *
 * This file keeps what c019 throws away. At c019's entry it works out the
 * same position the game would, and when the game is about to drop the sprite
 * for being right of the view, it records that slot at its true x, 256 or
 * more. The rest follows the game's own bookkeeping:
 *
 *   - the group's park clears its records, so an object that stops being
 *     placed (gone, sunk, landed) disappears with the game's own sprite --
 *     slots 124-127 included when the icon keeps them unparked, as the ship
 *     is not placed then;
 *   - 01:f11a, which shifts every object sprite while the player scrolls
 *     with A held between the four-frame updates, shifts the records by the
 *     same $7C, and 01:ef29 / 01:ef86, the same job for the scroll the
 *     cursor starts at the screen edge, by its own 2 px;
 *   - 00:bd41, where the object at slot 123 moves its sprite (-$91, +6) after
 *     placing it, does the same to that record;
 *   - the full NMI (00:80c0), which DMAs the shadow OAM, snapshots the
 *     records, so the margin shows the same moment as the authentic columns.
 *
 * The sprite's tile, palette, priority and flips are the guest's own: the
 * game writes them for every slot of a live object whether or not it placed
 * it, and they reach OAM with the rest. Its size is c019's rule (16x16 for
 * every slot but $01D8), because a parked slot's size bit is the park's.
 *
 * Drawn by host_map_compose() over the host map, from column 240 on. The
 * sixteen columns left of the edge are the authentic picture's, and a record
 * only reaches them when a pan has shifted a dropped sprite back across the
 * edge: the game places it there itself at its next update, at most three
 * frames on, and until then its slot is parked and draws nothing. Leaving
 * them out cut such a sprite off at the edge for those frames.
 *
 * SC_WS_VEHICLES=0 turns it off; SC_VEHICLE_DIAG=1 logs each record. */
#include "sc_vehicles.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "snes/ppu.h"

extern uint8_t g_ram[];   /* 128K WRAM: $7E0000 at 0, $7F0000 at 0x10000 */
extern Ppu *g_ppu;

enum {
  kFirstSlot = 109,          /* $7E21B4: the object slots, 109..127 */
  kStaleNmis = 600,          /* safety net: a record nothing touched in 10 s */
  kMarginCells = 0x20 + 40,  /* furthest right a record is kept, in cells */
};

typedef struct {
  int16_t x, y;
  uint8_t on, large;
  uint32_t stamp;
} ScMarginSprite;

static ScMarginSprite s_live[128];    /* maintained by the hooks */
static ScMarginSprite s_shown[128];   /* as of the last OAM DMA */
static uint32_t s_nmis;
static bool s_full_view;
void ScVehicles_FullView(bool enabled) {s_full_view=enabled;}

static int diag(void) {
  static int on = -1;
  if (on < 0) { const char *e = getenv("SC_VEHICLE_DIAG"); on = e && *e && *e != '0'; }
  return on;
}

static uint16_t rd16_dp(uint16_t dp, unsigned off) {
  const uint16_t a = (uint16_t)(dp + off);
  return (uint16_t)(g_ram[a] | (g_ram[(uint16_t)(a + 1)] << 8));
}

/* An absolute operand through DBR. Only WRAM answers; anything else is a
 * bank this code never runs with, and reads as "no". */
static bool rd16_abs(uint8_t db, uint16_t adr, uint16_t *out) {
  uint32_t off;
  if (db == 0x7e) off = adr;
  else if (db == 0x7f) off = 0x10000u + adr;
  else if ((db & 0x7f) < 0x40 && adr < 0x1fff) off = adr;
  else return false;
  *out = (uint16_t)(g_ram[off] | (g_ram[(off + 1) & 0x1ffff] << 8));
  return true;
}

static void clear_slots(int lo, int hi) {
  for (int s = lo; s <= hi; s++) s_live[s].on = 0;
}

/* 00:c019, the normal view ($D7 = 0). Mirrors the ROM:
 *
 *   tx = $91 + 2 - $01BD;  < 0 -> dropped (left)
 *   tx -= 2;  < 0 -> placed, X high bit;  < $20 -> placed;  else dropped
 *   x  = tx * 8 + $0A6D,Y
 *   ty = $94 - $01BF;  below -2 or $1D and up -> dropped
 *   y  = (ty - 1) * 8 + $0A6B,Y
 *
 * Only "dropped for being right of the view" makes a record; every other
 * outcome clears the slot's. */
static void on_place(uint16_t y, uint16_t dp, uint8_t db) {
  uint16_t slot_off, view_x, view_y, fine_x, fine_y;
  if (!rd16_abs(db, 0x0b33, &slot_off)) return;
  const int slot = slot_off >> 2;
  if (slot < kFirstSlot || slot > 127) return;
  ScMarginSprite *r = &s_live[slot];
  r->on = 0;
  if (rd16_dp(dp, 0xd7) != 0) return;
  if (!rd16_abs(db, 0x01bd, &view_x) || !rd16_abs(db, 0x01bf, &view_y) ||
      !rd16_abs(db, (uint16_t)(0x0a6d + y), &fine_x) ||
      !rd16_abs(db, (uint16_t)(0x0a6b + y), &fine_y))
    return;
  const int tx = (int16_t)(uint16_t)(rd16_dp(dp, 0x91) + 2 - view_x) - 2;
  if (!s_full_view && (tx < 0x20 || tx >= kMarginCells)) return;
  const int ty = (int16_t)(uint16_t)(rd16_dp(dp, 0x94) - view_y);
  if (!s_full_view && (ty < -2 || ty >= 0x1d)) return;
  r->x = (int16_t)(tx * 8 + (int16_t)fine_x);
  r->y = (int16_t)((ty - 1) * 8 + (int16_t)fine_y);
  r->large = slot_off != 0x01d8;
  r->stamp = s_nmis;
  r->on = 1;
  if (diag())
    fprintf(stderr, "[vehicle] slot %d kept at x=%d y=%d (cell %d,%d)\n",
            slot, r->x, r->y, tx, ty);
}

void ScVehicles_OnPc(unsigned bank, unsigned pc, uint16_t x, uint16_t y,
                     uint16_t dp, uint8_t db) {
  if (bank == 0x01 && pc == 0xf11a) {  /* the A-held scroll's shift */
    const int d = (int8_t)g_ram[(uint16_t)(dp + 0x7c)];
    const bool vertical = (x & 1) != 0;
    for (int s = kFirstSlot; s < 128; s++) {
      if (!s_live[s].on) continue;
      if (vertical) s_live[s].y = (int16_t)(s_live[s].y + d);
      else s_live[s].x = (int16_t)(s_live[s].x + d);
    }
    return;
  }
  if (bank == 0x01) {
    /* 01:ef29 and 01:ef86, the OTHER scroll's shift. Pushing the cursor
     * against the edge scrolls without holding A, and that path does not go
     * through 01:f11a at all: it walks the live objects' slots and adds or
     * subtracts 2 from each sprite's X or Y byte itself. Missing it left a
     * kept vehicle standing still while the map slid under it, until the
     * next placement put it back -- reported from play as the train
     * shivering on its rail. X is the byte's offset in the shadow OAM, so
     * its low two bits say which coordinate this is. */
    const int slot = (int)((x & 0xffffu) >> 2), part = x & 3;
    const int d = pc == 0xef29 ? 2 : -2;
    if (slot < kFirstSlot || slot > 127 || !s_live[slot].on) return;
    if (part == 0) s_live[slot].x = (int16_t)(s_live[slot].x + d);
    else if (part == 1) s_live[slot].y = (int16_t)(s_live[slot].y + d);
    return;
  }
  switch (pc) {
  case 0xc019: on_place(y, dp, db); break;
  case 0xc0f5: clear_slots(109, 112); clear_slots(117, 123); break;
  case 0xc154: clear_slots(113, 116); clear_slots(124, 127); break;
  case 0xbd41:                         /* after STA $91: slot 123's nudge */
    if (s_live[123].on) {
      s_live[123].x = (int16_t)(s_live[123].x - g_ram[(uint16_t)(dp + 0x91)]);
      s_live[123].y = (int16_t)(s_live[123].y + 6);
    }
    break;
  case 0x80c0:                         /* the full NMI: OAM goes out now */
    s_nmis++;
    for (int s = kFirstSlot; s < 128; s++)
      if (s_live[s].on && s_nmis - s_live[s].stamp > kStaleNmis) s_live[s].on = 0;
    memcpy(s_shown, s_live, sizeof s_shown);
    break;
  default: break;
  }
}

void ScVehicles_Reset(void) {
  memset(s_live, 0, sizeof s_live);
  memset(s_shown, 0, sizeof s_shown);
}

int ScVehicles_Shown(ScVehicleSprite *out, int max) {
  int n = 0;
  for (int s = 127; s >= kFirstSlot && n < max; s--) {
    if (!s_shown[s].on) continue;
    out[n].slot = s;
    out[n].x = s_shown[s].x;
    out[n].y = s_shown[s].y;
    out[n].large = s_shown[s].large != 0;
    n++;
  }
  return n;
}

int ScVehicles_Draw(uint8_t *pixels, size_t pitch, int x0, int x1, int height,
                    ScVehicleShade shade, void *ctx) {
  static const uint8_t kSizes[8][2] = {
    {8, 16}, {8, 32}, {8, 64}, {16, 32}, {16, 64}, {32, 64}, {16, 32}, {16, 32}
  };
  if (!g_ppu) return 0;
  if (diag() && getenv("SC_VEHICLE_DIAG")[0] == '2') {
    fprintf(stderr, "[vehicle] draw nmi %u:", (unsigned)s_nmis);
    for (int s = kFirstSlot; s < 128; s++) {
      const uint16_t o = g_ppu->oam[s * 2];
      if (s_shown[s].on) fprintf(stderr, " %d@%d,%d", s, s_shown[s].x, s_shown[s].y);
      else if ((o >> 8) < 0xe0) fprintf(stderr, " %d:oam%d,%d", s, o & 0xff, o >> 8);
    }
    fputc('\n', stderr);
  }
  const uint16_t *vram = g_ppu->vram;
  const uint8_t *bm = g_ppu->brightnessMult;
  int drawn = 0;
  /* Highest slot first: on the PPU a lower OAM index wins. */
  for (int s = 127; s >= kFirstSlot; s--) {
    const ScMarginSprite *r = &s_shown[s];
    if (!r->on) continue;
    const uint16_t oam1 = g_ppu->oam[s * 2 + 1];
    const int size = kSizes[PPU_objSize(g_ppu)][r->large];
    if (r->x >= x1 || r->x + size <= x0) continue;
    const int adr = (oam1 & 0x100) ? PPU_objTileAdr2(g_ppu) : PPU_objTileAdr1(g_ppu);
    const int pal = 0x80 + 16 * ((oam1 >> 9) & 7);
    const bool hflip = (oam1 & 0x4000) != 0, vflip = (oam1 & 0x8000) != 0;
    for (int row = 0; row < size; row++) {
      const int sy = r->y + row;
      if (sy < 0 || sy >= height) continue;
      uint32_t *dst = (uint32_t *)(pixels + (size_t)sy * pitch);
      const int tr = vflip ? size - 1 - row : row;
      for (int col = 0; col < size; col += 8) {
        const int uc = hflip ? size - 1 - col : col;
        const int tile = ((((oam1 & 0xff) >> 4) + (tr >> 3)) << 4) |
                         (((oam1 & 0xf) + (uc >> 3)) & 0xf);
        const int a = adr + tile * 16 + (tr & 7);
        const uint32_t plane = vram[a & 0x7fff] | (uint32_t)vram[(a + 8) & 0x7fff] << 16;
        for (int px = 0; px < 8; px++) {
          const int sx = r->x + col + px;
          if (sx < x0 || sx >= x1) continue;
          const uint32_t bits = plane >> (hflip ? px : 7 - px);
          const int c = (bits & 1) | ((bits >> 7) & 2) | ((bits >> 14) & 4) | ((bits >> 21) & 8);
          if (!c) continue;
          const uint16_t bgr = g_ppu->cgram[pal + c];
          uint32_t argb = 0xff000000u | ((uint32_t)bm[bgr & 31] << 16) |
                          ((uint32_t)bm[(bgr >> 5) & 31] << 8) | bm[(bgr >> 10) & 31];
          dst[sx] = shade ? shade(argb, ctx) : argb;
          drawn++;
        }
      }
    }
  }
  return drawn;
}
