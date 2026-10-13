/* The scenario selector's pins and win marks.
 *
 * The selector draws its sprites afresh every frame in 03:de64, all through
 * the sprite-text emitter (COP 2), which writes each sprite of a record at
 * base + (dx, dy) into the next OAM slot:
 *
 *   record $12 at ($A0 - $16, $60)            the eight pins, from slot 0
 *   record $11 at ($DF10,X - $16, $DF00,X)    the selection bracket, only on
 *                                             the blink's on phase; on the off
 *                                             phase its slots keep their last
 *                                             positions with X bit 8 set, and
 *                                             that is how it hides
 *   record $29 at ($DF30,Y - $16, $DF20,Y)    one win mark per bit of $42,
 *                                             from slot 33
 *
 * A record is two flag bytes, two bits per sprite (bit 0: dx is negative,
 * bit 1: large), then (dx, dy, tile, attr) per sprite; an entry with dx = 0
 * and bit 0 set ends it early (tools/text_tool.py reads them the same way).
 * The record table is at $00:A164.
 *
 * Widescreen shows the cards the authentic view leaves out -- column 0 at the
 * left once the selector scrolls to $50, the Las Vegas column at the right at
 * $00 -- and the game draws their pins and marks as well, but raw OAM X cannot
 * tell them from the hidden bracket. So a renderer takes them from here:
 * every sprite the emitter writes for a pin or a mark, recomputed from the
 * records, at its true position. The scroll comes from the pin record's first
 * sprite, the Las Vegas pin in slot 0, so it is the value the displayed OAM
 * was built with, even mid-scroll.
 *
 * Sylt's card, the ninth, is the host's: no record knows it, so the game gives
 * it neither pin nor mark. Its pin has Rio's colour, since Sylt takes Rio's
 * entries elsewhere too (03:ce8b's seed), at the Las Vegas pin's place one
 * column (80 px) on; its mark is record $29 at Las Vegas's mark base one
 * column on, when bit 8 of $42 -- where the Sylt win is kept -- is set. */
#include "sc_selector.h"

#include "snes/ppu.h"

enum {
  kRecordTable = 0x00a164,
  kPinRecord = 0x12,
  kMarkRecord = 0x29,
  kMarkX = 0x03df30,      /* eight words: each card's mark base */
  kMarkY = 0x03df20,
  kPinBaseX = 0xa0,
  kPinBaseY = 0x60,
  kColumnStep = 80,       /* card pitch; Sylt is column 4 */
  kLasVegas = 6,
};

typedef struct { int dx, dy, tile, attr, large; } Rec;

static int word(ScSelRomRead rd, void *ctx, uint32_t a) {
  return rd(ctx, a) | (rd(ctx, a + 1) << 8);
}

static int record(unsigned idx, Rec out[8], ScSelRomRead rd, void *ctx) {
  const uint32_t p = (uint32_t)word(rd, ctx, kRecordTable + idx * 2u);
  const unsigned flags = (unsigned)word(rd, ctx, p);
  int n = 0;
  for (int i = 0; i < 8; i++) {
    const uint32_t e = p + 2u + (uint32_t)i * 4u;
    const int dx = rd(ctx, e);
    const bool neg = (flags >> (i * 2)) & 1u;
    if (dx == 0 && neg) break;
    out[n].dx = neg ? dx - 256 : dx;
    out[n].dy = (int8_t)rd(ctx, e + 1);
    out[n].tile = rd(ctx, e + 2);
    out[n].attr = rd(ctx, e + 3);
    out[n].large = (flags >> (i * 2 + 1)) & 1u;
    n++;
  }
  return n;
}

static void put(ScSelSprite *s, int x, int y, const Rec *r, bool host);

int ScSelector_Record(unsigned idx, int base_x, int base_y,
                      ScSelSprite *out, int max, ScSelRomRead rd, void *ctx) {
  Rec rec[8];
  const int n = record(idx, rec, rd, ctx);
  int k = 0;
  for (int i = 0; i < n && k < max; i++)
    put(&out[k++], base_x + rec[i].dx, base_y + rec[i].dy, &rec[i], false);
  return k;
}

int ScSelector_Scroll(const Ppu *ppu, ScSelRomRead rd, void *ctx) {
  Rec pins[8];
  if (!ppu || record(kPinRecord, pins, rd, ctx) < 1) return -1;
  const unsigned lo = ppu->oam[0], hi = ppu->oam[1];
  const unsigned x9 = (lo & 0xffu) | ((ppu->highOam[0] & 1u) << 8);
  const unsigned tile = (hi & 0xffu) | (((hi >> 8) & 1u) << 8);
  if (((lo >> 8) & 0xffu) != ((unsigned)(kPinBaseY + pins[0].dy) & 0xffu) ||
      tile != ((unsigned)pins[0].tile | (((unsigned)pins[0].attr & 1u) << 8)))
    return -1;
  return (int)(((unsigned)(kPinBaseX + pins[0].dx) - x9) & 0x1ffu);
}

static void put(ScSelSprite *s, int x, int y, const Rec *r, bool host) {
  s->x = x; s->y = y;
  s->tile = r->tile; s->attr = r->attr;
  s->large = r->large != 0;
  s->host = host;
}

int ScSelector_Sprites(ScSelSprite out[SC_SEL_MAX_SPRITES], int scroll,
                       unsigned won, bool sylt, bool mars, ScSelRomRead rd, void *ctx) {
  Rec pins[8], mark[8];
  const int np = record(kPinRecord, pins, rd, ctx);
  const int nm = record(kMarkRecord, mark, rd, ctx);
  int n = 0;
  for (int i = 0; i < np; i++)
    put(&out[n++], kPinBaseX - scroll + pins[i].dx, kPinBaseY + pins[i].dy,
        &pins[i], false);
  for (int b = 0; b < 8; b++) {
    if (!((won >> b) & 1u)) continue;
    const int bx = word(rd, ctx, kMarkX + (uint32_t)b * 2u) - scroll;
    const int by = word(rd, ctx, kMarkY + (uint32_t)b * 2u);
    for (int i = 0; i < nm; i++)
      put(&out[n++], bx + mark[i].dx, by + mark[i].dy, &mark[i], false);
  }
  if (np < 1) return n;
  /* Pin: Las Vegas's (entry 0) one column on, in Rio's colour -- the entry
   * at +40/+28, which is Rio's card. */
  for (int i = 0; sylt && i < np; i++) {
    if (pins[i].dx != 40 || pins[i].dy != 28) continue;
    Rec pin = pins[i];
    pin.large = pins[0].large;
    put(&out[n++], kPinBaseX - scroll + pins[0].dx + kColumnStep,
        kPinBaseY + pins[0].dy, &pin, true);
    break;
  }
  if (sylt && (won & 0x100u)) {
    const int bx = word(rd, ctx, kMarkX + kLasVegas * 2u) - scroll + kColumnStep;
    const int by = word(rd, ctx, kMarkY + kLasVegas * 2u);
    for (int i = 0; i < nm; i++)
      put(&out[n++], bx + mark[i].dx, by + mark[i].dy, &mark[i], true);
  }
  if(!mars)return n;
  Rec mars_pin=pins[0];
  put(&out[n++],kPinBaseX-scroll+pins[0].dx+kColumnStep,
      kPinBaseY+pins[0].dy+88,&mars_pin,true);
  if(won & 0x200u) {
    int bx=word(rd,ctx,kMarkX+kLasVegas*2u)-scroll+kColumnStep;
    int by=word(rd,ctx,kMarkY+kLasVegas*2u)+88;
    for(int i=0;i<nm;++i)put(&out[n++],bx+mark[i].dx,by+mark[i].dy,&mark[i],true);
  }
  return n;
}
