/* The scenario selector's pins and win marks -- see sc_selector.c. */
#ifndef SC_SELECTOR_H_INCLUDED
#define SC_SELECTOR_H_INCLUDED

#include <stdbool.h>
#include <stdint.h>

typedef struct Ppu Ppu;

/* A ROM byte by SNES address: the host's bus, or the cart image. */
typedef uint8_t (*ScSelRomRead)(void *ctx, uint32_t addr);

typedef struct {
  int x, y;          /* screen position; x may be below 0 or 256 and up */
  int tile, attr;    /* OAM tile byte and attribute byte (bit 0: tile bit 8) */
  bool large;
  bool host;         /* Sylt's: the game never puts it in OAM */
} ScSelSprite;

enum { SC_SEL_MAX_SPRITES = 8 + 8 * 8 + 2 * (1 + 8) };

/* The selector shows on $0A (fade-in), $0B and $0C (fade-out to the fax). */
static inline bool ScSelector_OnScreen(unsigned screen) {
  return screen >= 0x0a && screen <= 0x0c;
}

/* One sprite-text record's sprites at a base position -- the game's $0261,
 * $025D and $025F, expanded the way the emitter at 00:8ea9 does. Returns
 * the count, at most 8. */
int ScSelector_Record(unsigned idx, int base_x, int base_y,
                      ScSelSprite *out, int max, ScSelRomRead rd, void *ctx);

/* The scroll the displayed OAM was built with, or -1 when OAM slot 0 is not
 * the pin record's first sprite (not drawn yet, or another screen's OAM). */
int ScSelector_Scroll(const Ppu *ppu, ScSelRomRead rd, void *ctx);

/* Every pin and win-mark sprite at that scroll: the game's -- the eight pins,
 * and a mark for each of bits 0-7 of `won` -- then, with `sylt`, the ninth
 * card's pin and, for bit 8, its mark. `mars` adds the lower fifth-column
 * card and its bit-9 mark only for hosts supporting that scenario. */
int ScSelector_Sprites(ScSelSprite out[SC_SEL_MAX_SPRITES], int scroll,
                       unsigned won, bool sylt, bool mars, ScSelRomRead rd, void *ctx);

#endif
