/* Vehicles in the city view's widescreen margin -- see sc_vehicles.c. */
#ifndef SC_VEHICLES_H_INCLUDED
#define SC_VEHICLES_H_INCLUDED

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* True for the guest PCs ScVehicles_OnPc wants to see. Inline, because the
 * interpreter loop asks once per opcode. */
static inline bool ScVehicles_WantsPc(unsigned bank, unsigned pc) {
  if (bank == 0x00)
    return pc == 0xc019 || pc == 0xc0f5 || pc == 0xc154 || pc == 0xbd41 ||
           pc == 0x80c0;
  return bank == 0x01 && (pc == 0xf11a || pc == 0xef29 || pc == 0xef86);
}

/* Call BEFORE the opcode at bank:pc runs, with the CPU's registers. */
void ScVehicles_OnPc(unsigned bank, unsigned pc, uint16_t x, uint16_t y,
                     uint16_t dp, uint8_t db);

/* Forget every margin sprite (a state load, a new game). */
void ScVehicles_Reset(void);
/* Free host cameras need placements outside the native 256x224 rectangle. */
void ScVehicles_FullView(bool enabled);

/* The kept sprites the current frame shows, for a renderer that draws them
 * itself: the OAM slot (its tile, attributes and palette are live there),
 * the screen position (x from 240 up), and the size c019 gives it. Highest
 * slot first, so drawing in order lets the lower slot win, as on the PPU.
 * Returns the count. */
typedef struct { int slot, x, y; bool large; } ScVehicleSprite;
int ScVehicles_Shown(ScVehicleSprite *out, int max);

/* Paint the margin sprites the frame shows into `pixels` (0xAARRGGBB rows,
 * screen x = buffer x), columns [x0, x1) only, rows [0, height). `shade`
 * turns a colour into what the margin shows (dimming); NULL leaves it.
 * Returns the number of pixels painted. */
typedef uint32_t (*ScVehicleShade)(uint32_t argb, void *ctx);
int ScVehicles_Draw(uint8_t *pixels, size_t pitch, int x0, int x1, int height,
                    ScVehicleShade shade, void *ctx);

#endif
