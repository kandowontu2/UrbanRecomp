/* The game's map generation, decompiled to native C.
 *
 * GOAL: generate maps without running guest code, so generation can be
 * changed -- bigger maps, new terrain rules, custom seeds -- rather than only
 * replayed. That means every routine here must reproduce the ROM bit-exactly
 * first; only once a seed yields an identical map does deviating from it mean
 * anything.
 *
 * STATUS: the complete Normal generator matches captured cartridge terrain
 * and final random states. See the detailed verification notes below.
 *
 *   00:824f  the PRNG step -- 14 of 14 sampled transitions reproduced exactly.
 *   00:823e  seed-from-spin -- caught live: $c7 read C4, and the next frame
 *            $59 = 00C4 and $5b = 00C5, exactly LDA $c7 / STA $59 / INC A /
 *            STA $5b. Slot 5 independently shows 0081/0082, the same shape.
 *
 * 03:d840 (map seeding), both feature chains and native shoreline/forest
 * fitting are verified. Expanded worlds extend their bounds and feature
 * counts while Normal retains the cartridge's exact draws and tile results.
 *
 * Map geometry: 120 x 100 = 12000 cells. Confirmed independently by the power
 * bitmap at 03:b0f8, whose CPX #$05dc bounds it at 1500 bytes = 12000 bits.
 *
 * THE MAP IS AT $7F0200 -- bank 7F, not 7E. 01:f8e9, which the generator uses
 * to read a cell, ends:
 *
 *     CLC / ADC $044f      ; index = y*120 + x
 *     ASL A / TAX          ; word array
 *     LDA $7f0200,X
 *     AND #$03ff
 *
 * docs/ROM_MAP.md says the generated map lands at $7e0200; that is a different
 * buffer, and reading it wastes time. Measured on a real terrain state,
 * $7F0200 holds 37 distinct tile values across the 12000 cells -- a plausible
 * terrain vocabulary -- while $7E0200 holds 411, which is far too many to be
 * tiles. Corrected in ROM_MAP.md too. */

#include "sc_mapgen.h"
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

/* Geometry belongs to each generation, so a large map cannot leak its
 * stride into a subsequent stock generation or the standalone helpers. */
static unsigned map_width(const ScMapGenState *st) { return st->width==3840 && st->height==3200 ? 3840 : st->width==1920 && st->height==1600 ? 1920 : st->width==960 && st->height==800 ? 960 : st->width==480 && st->height==400 ? 480 : st->width==240 && st->height==200 ? 240 : 120; }
static unsigned map_height(const ScMapGenState *st) { return st->width==3840 && st->height==3200 ? 3200 : st->width==1920 && st->height==1600 ? 1600 : st->width==960 && st->height==800 ? 800 : st->width==480 && st->height==400 ? 400 : st->width==240 && st->height==200 ? 200 : 100; }
static int map_bounds(const ScMapGenState *st, int x, int y) {
    return x>=0 && y>=0 && (unsigned)x<map_width(st) && (unsigned)y<map_height(st);
}

/* ── PRNG ──────────────────────────────────────────────────────────────────
 *
 * 00:824f, reached through the JSL wrapper at 00:824b:
 *
 *     REP #$20        ; 16-bit A
 *     CLC
 *     LDA $59
 *     STA $5d         ; t   = s0
 *     ADC $5b         ; A   = s0 + s1, carry clear on entry
 *     STA $59         ; s0' = s0 + s1
 *     ADC $5d         ; A   = s0' + t + CARRY from the previous ADC
 *     STA $5b         ; s1' = that
 *     RTS             ; returns s1' in A
 *
 * An additive lagged-Fibonacci-with-carry generator over two 16-bit words. It
 * takes no input, which is what rules out the "checksum/hash" reading an
 * earlier pass of docs/ROM_MAP.md recorded -- it folds nothing in, it only
 * advances state.
 *
 * The carry chaining between the two ADCs is load-bearing: the second one adds
 * the carry out of the first. Measured against the guest, dropping it drops
 * the match from 14 of 14 sampled transitions to 8 of 14 -- so it is wrong,
 * and it is wrong in the worst way, agreeing most of the time. */
void sc_mapgen_prng_seed_from_spin(ScMapGenPrng *p, uint16_t spin_counter) {
    /* 00:823e: LDA $c7 / STA $59 / INC A / STA $5b / INC A / STA $5d.
     * $c7 counts vblank spin iterations, so the seed is how long the player
     * took -- which is what makes generated maps vary at all. */
    p->s0 = spin_counter;
    p->s1 = (uint16_t)(spin_counter + 1u);
    p->t  = (uint16_t)(spin_counter + 2u);
}

/* How many times the PRNG has been stepped. The generation is one synchronous
 * JSL chain from 03:d840, so a full run's step count is a fixed number -- which
 * makes it usable as a fingerprint against a state sampled from the guest. */
unsigned long g_sc_mapgen_prng_steps = 0;
/* Per-routine draw and cell accounting, to match against the guest's own
 * step-vs-cells profile recovered from WRAM dumps. */
unsigned long g_sc_mapgen_phase_steps[5] = {0};
unsigned long g_sc_mapgen_phase_cells[5] = {0};
unsigned long g_sc_mapgen_phase_changed[5] = {0};
unsigned long g_sc_mapgen_phase_cleared[5] = {0};

/* Snapshot the map after an exact number of PRNG draws.
 *
 * Comparing FINAL maps is nearly useless for judging a fix: the moment the
 * draw sequence diverges, every later loop bound is drawn from a different
 * stream and all the totals become chaos, so a change can move the final match
 * by a couple of points for no reason connected to its correctness. The guest
 * dumps, though, carry the PRNG state, so each one is pinned to an exact draw
 * count -- 80, 256, 436, 614, 849, 1265, 1654, 2156 and so on. Snapshotting
 * our map at the same count compares like with like and finds the FIRST
 * divergence, which is the only one that means anything. */
ScMapGenState    *g_sc_mapgen_cur = 0;
unsigned long     g_sc_mapgen_snap_at = 0;
int               g_sc_mapgen_snapped = 0;
uint16_t          g_sc_mapgen_snap[SC_MAPGEN_CELLS];

uint16_t sc_mapgen_prng_step(ScMapGenPrng *p) {
    g_sc_mapgen_prng_steps++;
    if (g_sc_mapgen_snap_at && g_sc_mapgen_prng_steps == g_sc_mapgen_snap_at &&
        g_sc_mapgen_cur && !g_sc_mapgen_snapped) {
        memcpy(g_sc_mapgen_snap, g_sc_mapgen_cur->map, sizeof g_sc_mapgen_snap);
        g_sc_mapgen_snapped = 1;
    }
    const uint16_t t = p->s0;
    p->t = t;

    const uint32_t a = (uint32_t)p->s0 + (uint32_t)p->s1;   /* CLC: no carry in */
    const unsigned carry = (a >> 16) & 1u;
    p->s0 = (uint16_t)a;

    const uint32_t b = (uint32_t)p->s0 + (uint32_t)t + carry;
    p->s1 = (uint16_t)b;

    return p->s1;
}

/* ── Map seeding ───────────────────────────────────────────────────────────
 *
 * 03:d840. Entry has the caller's value in A, which it stores to $59:
 *
 *     STA $59
 *     LDA $0b28 / EOR #$ffff / ROL A x5 / ADC #$1238 / STA $5b
 *     STZ $5d
 *     LDA $0b29 / ASL A / ADC $0b28 / ADC $0b27 / AND #$001f / TAX
 *     JSL $00824b / DEX / BPL      ; steps the PRNG X+1 times, 1..32
 *     JSL $01f1ed                  ; terrain features
 *     JSL $02923f                  ; scratch clear + DMA upload
 *
 * The map is fully determined by the three seed bytes $0b27-$0b29: 03:d873
 * copies them to $0b2a-$0b2c straight afterwards, i.e. the game keeps them as
 * the map's identity.
 *
 * CARRY IS UNDEFINED ON ENTRY here, and both the ROL chain and the ADC #$1238
 * consume it. The ROLs rotate it through bit 0 and the ADC adds it. So the
 * result depends on the caller's carry, which the disassembly alone cannot
 * tell us -- this is the first thing the verification harness has to pin down,
 * and it is why `entry_carry` is a parameter rather than an assumption. */
void sc_mapgen_seed(ScMapGenPrng *p, uint16_t a_on_entry,
                    uint8_t seed0, uint8_t seed1, uint8_t seed2,
                    uint8_t prev0, unsigned entry_carry) {
    p->s0 = a_on_entry;                       /* STA $59 */

    /* THESE ARE WORD READS. 03:d842 onward runs with m=0, so LDA $0b27 fetches
     * $0b27 AND $0b28, and LDA $0b29 fetches $0b29 and $0b2a. $0b2a is the
     * KEPT copy of the previous map's index, so the seeding depends on which
     * map was generated before this one -- generation is not a pure function
     * of the selected index.
     *
     * Reading them as bytes gave the right answer for map 3 by luck (the high
     * halves did not reach the low 5 bits) and the wrong one for map 0, where
     * $0b2a = FF makes the ASL carry out and the pre-step count 2 rather than
     * 1. */
    const uint16_t w27 = (uint16_t)(seed0 | (seed1 << 8));
    const uint16_t w28 = (uint16_t)(seed1 | (seed2 << 8));
    const uint16_t w29 = (uint16_t)(seed2 | (prev0 << 8));

    uint32_t v = (uint16_t)~w28;                  /* LDA $0b28 / EOR #$ffff */
    unsigned c = entry_carry & 1u;
    for (int i = 0; i < 5; i++) {                 /* ROL A x5, through carry */
        const unsigned out = (v >> 15) & 1u;
        v = (uint16_t)((v << 1) | c);
        c = out;
    }
    v = v + 0x1238u + c;                          /* ADC #$1238, carry in */
    p->s1 = (uint16_t)v;

    p->t = 0;                                     /* STZ $5d */

    /* LDA $0b29 / ASL A / ADC $0b28 / ADC $0b27 / AND #$001f.
     * Confirmed a WORD read, not a byte read -- see the note above. */
    uint32_t n = (uint32_t)w29 << 1;              /* LDA $0b29 / ASL A */
    unsigned c2 = (n >> 16) & 1u;                 /* the ASL's carry OUT */
    n = (uint16_t)n;
    n = n + w28 + c2;  c2 = (n >> 16) & 1u;  n = (uint16_t)n;   /* ADC $0b28 */
    n = n + w27 + c2;                        n = (uint16_t)n;   /* ADC $0b27 */
    unsigned steps = (unsigned)(n & 0x1fu) + 1u;         /* DEX/BPL: X+1 times */
    /* SC_MAPGEN_PRESTEP overrides the count. The whole stream shifts with it,
     * so if this reading of $0b29/$0b28/$0b27 is off by even one the map is
     * unrelated to the guest's -- worth being able to sweep rather than
     * assume. */
    { const char *e = getenv("SC_MAPGEN_PRESTEP");
      if (e && *e) steps = (unsigned)strtoul(e, NULL, 0); }

    for (unsigned i = 0; i < steps; i++) sc_mapgen_prng_step(p);
}

/* ── Random below N ────────────────────────────────────────────────────────
 *
 * 01:f877. The generator's range primitive, and the reason the PRNG had to be
 * exact before anything else could be:
 *
 *     REP #$20
 *     INC A / STA $79        ; keep N+1
 *     JSL $00824b            ; one PRNG step, result in A (16-bit)
 *     SEP #$20 / XBA         ; take the HIGH byte of that result
 *     LDA $79 / PHA
 *     LDA $b3 / AND #$7f / STA $b1      ; unrelated: $b1/$b3 housekeeping
 *     PLA  / STA $4202       ; WRMPYA = (N+1) low byte
 *     XBA  / STA $4203       ; WRMPYB = the random byte -> starts the multiply
 *     PHA / PLA / NOP        ; the mandatory 8-cycle wait
 *     LDA $4217 / XBA / LDA $4216 / PHA
 *     ...
 *     PLA / XBA / REP #$20 / AND #$00ff / RTS
 *
 * It runs the SNES hardware multiplier over (N+1) x rand8 and returns the HIGH
 * byte of the 16-bit product -- the standard trick for scaling a byte into
 * 0..N without a divide. The final `AND #$00ff` clears the high half, so the
 * result really is 0..N inclusive.
 *
 * The `$b1`/`$b3` traffic in the middle is not part of the computation; it is
 * preserved here only because reproducing the PRNG consumption exactly is what
 * matters, and that is one step per call. */
uint16_t sc_mapgen_rand_below(ScMapGenPrng *p, uint16_t n) {
    const uint16_t r = sc_mapgen_prng_step(p);       /* JSL $00824b */
    /* THE LOW BYTE, not the high one. Follow the register moves:
     *
     *     SEP #$20      A = R.low,   B = R.high
     *     XBA           A = R.high,  B = R.low
     *     LDA $79       A = n+1,     B = R.low     <- B still holds R.low
     *     PLA / STA $4202                          multiplicand = n+1
     *     XBA           A = R.low                  <- swaps the LOW byte in
     *     STA $4203                                multiplier   = R.low
     *
     * The XBA at $f882 looks like it selects the high byte, but LDA $79
     * overwrites A before the multiply and the second XBA at $f891 brings
     * back what B held, which is the LOW byte. Reading the high byte instead
     * gave centre (74,54) where the guest's $0457/$0459 hold (68,59); the low
     * byte reproduces 68, 59 and the bearing 1 exactly. */
    const unsigned rand8 = r & 0xffu;
    const unsigned mul = (unsigned)(n + 1u) * rand8;   /* 8x8 -> 16 */
    return (uint16_t)(mul >> 8);           /* RDMPYH, then AND #$00ff */
}

/* ── Feature: centre point ─────────────────────────────────────────────────
 *
 * 01:f380, the smallest of the six:
 *
 *     LDA #$0028 / JSR $f877 / CLC / ADC #$0028   ; 40 + rand(0..40)
 *     STA $0457 / STA $043b
 *     LDA #$0021 / JSR $f877 / CLC / ADC #$0021   ; 33 + rand(0..33)
 *     STA $0459 / STA $043d
 *
 * On a 120 x 100 map that is x in 40..80 and y in 33..66 -- a point in the
 * middle third of each axis, written to two pairs of variables at once.
 *
 * NOTE the operand order: the range call happens BEFORE the add, so it draws
 * with N = 40 and N = 33, not with the sum. Two PRNG steps per call. */
static uint16_t native_coordinate(ScMapGenPrng *p,const ScMapGenState *st,unsigned n) {
    return map_width(st)==120?sc_mapgen_rand_below(p,(uint16_t)n):
        (uint16_t)(((uint32_t)sc_mapgen_prng_step(p)*(n+1))>>16);
}

void sc_mapgen_feature_centre(ScMapGenPrng *p, ScMapGenState *st) {
    /* Each value is stored TWICE -- to the saved centre AND to the live walk
     * position:
     *
     *     STA $0457 / STA $043b        STA $0459 / STA $043d
     *
     * Setting only the centre left the first walk starting from whatever
     * $043b/$043d held, which for us was zero: the first walk ran from the
     * top-left corner and stamped a blob there that the ROM never draws. The
     * second and third walks looked right only because $f5b9 resets the
     * position from $0457/$0459 before each of them. */
    st->x0 = (uint16_t)(native_coordinate(p,st,map_width(st)/3) + map_width(st)/3);   /* $0457 */
    st->cur_x = st->x0;                                               /* $043b */
    st->y0 = (uint16_t)(native_coordinate(p,st,map_height(st)/3) + map_height(st)/3);   /* $0459 */
    st->cur_y = st->y0;                                               /* $043d */
}

/* ── Feature: scatter ──────────────────────────────────────────────────────
 *
 * 01:f3a3:
 *
 *     LDA #$0064 / JSR $f877 / CLC / ADC #$0032   ; count = 50 + rand(0..100)
 *     STA $043f
 *   loop:
 *     LDA #$0077 / JSR $f877 / STA $044b          ; x = rand(0..119)
 *     LDA #$0063 / JSR $f877 / STA $044d          ; y = rand(0..99)
 *     JSR $f3d3                                   ; place at (x,y)
 *     DEC $043f / BNE loop
 *     JSR $f502 / JSR $f502
 *
 * 50..150 placements at cells drawn across the FULL map -- 0..119 by 0..99 is
 * exactly the 120 x 100 bounds, which is a useful confirmation of the geometry
 * from a second direction.
 *
 * Three PRNG steps per iteration (one for the count, then two per placement),
 * so the stream position depends on the count drawn first. Getting that order
 * wrong desynchronises everything after it. */
void sc_mapgen_feature_scatter(ScMapGenPrng *p, ScMapGenState *st) {
    unsigned count = (sc_mapgen_rand_below(p, 0x0064) + 0x0032u) * (map_width(st)*map_height(st)/SC_MAPGEN_CELLS);
    st->count = count;
    while (count) {
        st->px = native_coordinate(p,st,map_width(st)-1);   /* $044b, 0..119 */
        st->py = native_coordinate(p,st,map_height(st)-1);   /* $044d, 0..99  */
        sc_mapgen_walk(p, st);   /* $f3d3: each placement spawns a walk */
        count--;
    }
    st->count = 0;
    sc_mapgen_fit_pass(p, st);   /* JSR $f502 */
    sc_mapgen_fit_pass(p, st);   /* JSR $f502 again -- sees the first's output */
}

/* ── Feature: path through the centre ──────────────────────────────────────
 *
 * 01:f5b9:
 *
 *     JSL $00824b / AND #$0003 / STA $045f / STA $0461   ; dir = rand & 3
 *     JSR $f600                                          ; walk that way
 *     LDA $0457 / STA $043b / LDA $0459 / STA $043d      ; back to the centre
 *     LDA $045f / EOR #$0004 / STA $045f / STA $0461     ; dir ^= 4
 *     JSR $f600                                          ; walk the other way
 *     LDA $0457 / STA $043b / LDA $0459 / STA $043d      ; back to the centre
 *
 * Draws from the centre point in one of four directions, then from the same
 * point in the opposite one -- so the feature crosses the middle rather than
 * starting there. `EOR #$0004` is what makes 0..3 and 4..7 opposite halves of
 * an eight-way direction encoding.
 *
 * Note it steps the PRNG DIRECTLY rather than through 01:f877, so this call
 * costs exactly one step regardless of the direction drawn.
 *
 * $0457/$0459 are the centre written by 01:f380, so this feature depends on
 * that one having run. */
/* 01:f843 applied to a probe formed as $043b + offset, in 16-BIT WRAPPING
 * arithmetic, which is the whole point of this helper.
 *
 *     LDA $0453 / BMI fail / CMP #$0078 / BCS fail
 *     LDA $0455 / BMI fail / CMP #$0064 / BCS fail
 *
 * The ROM builds the probe with ADC #$0004 on a 16-bit word, so a walk that
 * has stepped to x = -1 ($FFFF) probes at 3 and PASSES -- it keeps drawing for
 * a few more cells past the left or top edge. Computing the probe as
 * (int)cur_x + 4 instead gives 65539, fails, and stops the walk immediately.
 * That single difference made our walks about half the guest's length. */
static int sc_mapgen_probe_in_bounds(const ScMapGenState *st, uint16_t x, uint16_t y,
                                     uint16_t offx, uint16_t offy) {
    const uint16_t px = (uint16_t)(x + offx);
    const uint16_t py = (uint16_t)(y + offy);
    if (px & 0x8000u) return 0;                   /* BMI */
    if (px >= map_width(st)) return 0;              /* CMP #$0078 / BCS */
    if (py & 0x8000u) return 0;                   /* BMI */
    if (py >= map_height(st)) return 0;              /* CMP #$0064 / BCS */
    return 1;
}

void sc_mapgen_feature_path(ScMapGenPrng *p, ScMapGenState *st) {
    st->dir_base = (uint16_t)(sc_mapgen_prng_step(p) & 0x0003u);
    st->dir_cur = st->dir_base;
    sc_mapgen_path_walk(p, st);                 /* JSR $f600 */
    st->cur_x = st->x0;
    st->cur_y = st->y0;
    st->dir_base ^= 0x0004u;                    /* the exact reverse bearing */
    st->dir_cur = st->dir_base;
    sc_mapgen_path_walk(p, st);                 /* JSR $f600 again */
    st->cur_x = st->x0;
    st->cur_y = st->y0;

    /* 01:f5f2  JSL $00824b / AND #$0003 / STA $045f
     * 01:f5fc  JSR $f647
     *
     * A THIRD walk, which this routine was missing entirely. It is what the
     * cells-per-draw rate was pointing at: the ROM spends about two random
     * numbers per cell in the terrain phase and we were spending one, because
     * a whole walk's worth of reads was absent.
     *
     * Note f5f9 stores ONLY to $045f, the base bearing. $0461, the live one,
     * is not reloaded -- so the third walk starts on whatever bearing the
     * second one happened to end on, not on the fresh base. */
    st->dir_base = (uint16_t)(sc_mapgen_prng_step(p) & 0x0003u);
    sc_mapgen_path_walk_narrow(p, st);          /* JSR $f647 */
}

/* 01:f647 -- the third walk. Identical in shape to $f600 with exactly three
 * differences, all of which matter:
 *
 *     the bounds probe is +3 rather than +4      ($f64d / $f657 ADC #$0003)
 *     it stamps the small disc, not the 9x9      ($f662 JSR $f794)
 *     the bearing resets on 1 in 13, not 1 in 11 ($f677 LDA #$000c)
 *
 * so it lays a narrower path with the smaller brush and wanders slightly
 * further before snapping back to its base bearing. */
void sc_mapgen_path_walk_narrow(ScMapGenPrng *p, ScMapGenState *st) {
    for (;;) {
        if (!sc_mapgen_probe_in_bounds(st, st->cur_x, st->cur_y, 3, 3)) return;
        sc_mapgen_stamp_blob_small(st);                 /* JSR $f794 */

        const uint16_t r = sc_mapgen_prng_step(p);
        if (r & 1u) {                                   /* LSR / BCC skip */
            if (r & 2u) st->dir_cur--;                  /* LSR / BCS -> DEC */
            else        st->dir_cur++;
        }
        if (sc_mapgen_rand_below(p, 0x000c) == 0)       /* 1 in 13 */
            st->dir_cur = st->dir_base;

        sc_mapgen_move(st, st->dir_cur);                /* JSR $f6ae */
    }
}

/* ── Feature: clustered blobs ──────────────────────────────────────────────
 *
 * 01:f311. A nested loop, and the tail is what gives it away:
 *
 *     LDA #$000a / JSR $f877 / INC A          / STA $0445   ; clusters 1..11
 *   outer ($f31d):
 *     LDA #$0063 / JSR $f877 / ADC #$000a     / STA $043f   ; cx = 10 + r(99)
 *     LDA #$0050 / JSR $f877 / ADC #$000a     / STA $0441   ; cy = 10 + r(80)
 *     LDA #$000c / JSR $f877 / INC A / INC A  / STA $0443   ; blobs 2..14
 *   inner ($f342):
 *     LDA #$000c / JSR $f877 / ADC $043f / SBC #$0006 / STA $043b  ; x = cx +r(12)-6
 *     LDA #$000c / JSR $f877 / ADC $0441 / SBC #$0006 / STA $043d  ; y = cy +r(12)-6
 *     JSL $00824b / AND #$0003
 *     BEQ -> JSR $f794    (1 in 4)
 *     else    JSR $f71d   (3 in 4)
 *     DEC $0443 / BNE inner
 *     DEC $0445 / BNE outer
 *
 * So 1..11 clusters, each of 2..14 blobs scattered within +/-6 of the cluster
 * centre, and each blob drawn by one of two routines on a 1-in-4 split. The
 * cluster centres are held away from the edges (10..109 by 10..90 on a 120x100
 * map), which the blob jitter of +/-6 can still push outside -- so whatever
 * $f71d and $f794 do must cope with out-of-range coordinates.
 *
 * PRNG cost: 1 for the cluster count, then per cluster 3 (cx, cy, blob count),
 * then per blob 3 (x, y, and the direct draw for the 1-in-4). The direct draw
 * happens EVERY blob, not only when it branches. */
void sc_mapgen_feature_clusters(ScMapGenPrng *p, ScMapGenState *st) {
    unsigned clusters = (sc_mapgen_rand_below(p, 0x000a) + 1u) * (map_width(st)*map_height(st)/SC_MAPGEN_CELLS);      /* $0445 */
    while (clusters) {
        const uint16_t cx = (uint16_t)(native_coordinate(p,st,map_width(st)-21) + 0x000au);
        const uint16_t cy = (uint16_t)(native_coordinate(p,st,map_height(st)-20) + 0x000au);
        unsigned blobs = sc_mapgen_rand_below(p, 0x000c) + 2u;     /* $0443 */
        st->cx = cx; st->cy = cy;
        while (blobs) {
            st->cur_x = (uint16_t)(cx + sc_mapgen_rand_below(p, 0x000c) - 6u);
            st->cur_y = (uint16_t)(cy + sc_mapgen_rand_below(p, 0x000c) - 6u);
            if ((sc_mapgen_prng_step(p) & 0x0003u) == 0) {
                sc_mapgen_stamp_blob_small(st);   /* $f794: the 6x6 disc */
            } else {
                sc_mapgen_stamp_blob(st);   /* $f71d: the 9x9 disc */
            }
            blobs--;
        }
        clusters--;
    }
}

/* ── The generator itself ──────────────────────────────────────────────────
 *
 * 01:f1ed is a JSL wrapper; 01:f1f1 is the body:
 *
 *     LDA $b3 / AND #$7f / STA $b1        ; clear bit 7 of $b1 while generating
 *     JSL $00824b / AND #$00ff            ; one PRNG step, low byte
 *     CMP #$0056 / BCS +                  ; 0x56 = 86 of 256
 *     JSR $f22c / BRA done                ;   below -> the alternative map
 *   + JSL $0094bc                         ;   at or above -> the feature chain
 *     JSR $f380                           ;     centre point
 *     JSR $f5b9                           ;     path through the centre
 *     JSR $f311
 *     JSR $f444
 *     JSR $f3a3                           ;     scatter
 *   done:
 *     LDA $b3 / ORA #$80 / STA $b1        ; set bit 7 again
 *
 * So 86/256 = 33.6% of maps take the `$f22c` path and the other 66.4% are
 * built from the five features IN THIS ORDER. That ordering matters as much as
 * the routines: each one consumes PRNG steps, so running them in a different
 * order gives a different map from the same seed even if every routine is
 * individually right.
 *
 * The `$b1` bit-7 bracket around the whole thing looks like a
 * generation-in-progress flag; it is reproduced because it is cheap, not
 * because its effect is understood. */
static void generate(ScMapGenPrng *p, ScMapGenState *st, int large) {
    st->width=120u<<large; st->height=100u<<large;
    memset(st->map,0,sizeof st->map);
    g_sc_mapgen_cur = st;
    const unsigned pick = sc_mapgen_prng_step(p) & 0x00ffu;
    if (pick < 0x0056u) {
        sc_mapgen_framed_map(p, st);    /* JSR $f22c -- 33.6% of seeds */
        return;
    }
    /* JSL $0094bc -- clears the map to zero. Done here rather than left to the
     * caller, so a generate() into a dirty buffer is correct: the HLE path
     * hands us whatever the guest's map held. The framed branch does not need
     * it, because $f22c writes every cell before carving. */
    memset(st->map, 0, sizeof st->map);
    /* Snapshot around each routine. The nonzero total alone cannot tell a pass
     * that rewrites existing cells from one that writes nothing at all --
     * which is exactly the open question about $f444. */
    static uint16_t sc_before[SC_MAPGEN_MAX_CELLS];
#define SC_PHASE(slot, call) do { \
    const unsigned long steps_before = g_sc_mapgen_prng_steps; \
    const unsigned cells = map_width(st)*map_height(st); \
    unsigned populated=0, changed=0, cleared=0; \
    memcpy(sc_before,st->map,cells*sizeof *sc_before); \
    call; \
    for (unsigned i=0;i<cells;++i) { \
        if (st->map[i]&0x3ffu) ++populated; \
        if ((st->map[i]&0x3ffu)!=(sc_before[i]&0x3ffu)) { \
            ++changed; if (!(st->map[i]&0x3ffu)) ++cleared; \
        } \
    } \
    g_sc_mapgen_phase_steps[slot]=g_sc_mapgen_prng_steps-steps_before; \
    g_sc_mapgen_phase_cells[slot]=populated; \
    g_sc_mapgen_phase_changed[slot]=changed; \
    g_sc_mapgen_phase_cleared[slot]=cleared; \
} while (0)
    SC_PHASE(0, sc_mapgen_feature_centre(p, st));    /* $f380 */
    SC_PHASE(1, sc_mapgen_feature_path(p, st));      /* $f5b9 */
    SC_PHASE(2, sc_mapgen_feature_clusters(p, st));  /* $f311 */
    SC_PHASE(3, sc_mapgen_shoreline(p, st));         /* $f444 */
    SC_PHASE(4, sc_mapgen_feature_scatter(p, st));   /* $f3a3 */
#undef SC_PHASE
}

void sc_mapgen_generate(ScMapGenPrng *p, ScMapGenState *st) { generate(p,st,0); }
void sc_mapgen_generate_large(ScMapGenPrng *p, ScMapGenState *st) { sc_mapgen_generate_geographic(p,st,1); }
void sc_mapgen_generate_huge(ScMapGenPrng *p, ScMapGenState *st) { sc_mapgen_generate_geographic(p,st,2); }
void sc_mapgen_generate_giant(ScMapGenPrng *p, ScMapGenState *st) { sc_mapgen_generate_geographic(p,st,3); }
void sc_mapgen_generate_mega(ScMapGenPrng *p, ScMapGenState *st) { sc_mapgen_generate_geographic(p,st,5); }
void sc_mapgen_generate_colossal(ScMapGenPrng *p, ScMapGenState *st) { sc_mapgen_generate_geographic(p,st,4); }

/* ── What is decompiled, and what the comparison says ──────────────────
 *
 * NOTE: this block used to be titled "Not yet decompiled" and was never
 * closed -- it ran straight into the next comment, swallowing that header.
 * It also still listed $f3d3, $f502, the $f600 walk and the $f71d/$f794 blob
 * draws as missing, long after all four were written.
 *
 * Everything in the chain is now decompiled:
 *
 *   01:f1ed  generate      split 86/256 = 33.6% to the framed map $f22c
 *   01:f380  centre        01:f877  rand_below     01:f85d  min-of-two
 *   01:f3a3  scatter       incl. $f3d3 placement and $f502
 *   01:f5b9  path          incl. the $f600 walk
 *   01:f311  clusters      incl. the $f71d / $f794 blob draws
 *   01:f444  shoreline     01:f22c  framed map
 *   01:f8e9 / 01:f8af      cell read and write
 *   00:94bc                map clear -- SEP #$20 then 0x5dc0 = 24000 single
 *                          BYTE stores to $7f0200,X, i.e. the whole 12000-word
 *                          map zeroed. Nothing more; our zero-init is
 *                          equivalent. Was the last unknown in the chain.
 *
 * Still genuinely open: the $f2be vertical edge loop inside $f22c is inferred
 * from the horizontal one, not read.
 *
 * ── THE GENERATOR IS EXACT ───────────────────────────────────
 *
 * Verified exact on TWO maps, with different seed bytes and different
 * pre-step counts:
 *
 *     map 3  carry 0, A 5CEE, prev 02   12000/12000, next best 48.1%  chain
 *     map 0  carry 0, A 5CD6, prev FF   12000/12000, next best 57.1%  chain
 *     map 4  carry 0, A 5CF6, prev 02   12000/12000, next best 67.7%  FRAMED
 *
 * For map 3 it also consumes the guest's exact 20373 draws and leaves the PRNG
 * in its exact final state 346D/529F, and every intermediate snapshot across
 * the whole generation -- 16 draws through 20373, including the late phase
 * where the map erodes from 7352 cells back to 6626 -- agrees to 99.9-100%.
 * That residue is not error: the guest dumps are taken at a vblank rather than
 * a draw boundary, so one stamp can be caught half-finished.
 *
 * The two entry A values, 5CEE and 5CD6, are suspiciously close. A is just
 * whatever the caller left in the register, so it is probably a pointer or
 * counter rather than anything meaningful -- but it is not constant, and it
 * cannot be assumed.
 *
 * Both branches are now covered. The framed map was reached by holding $0b27
 * at an index with SC_FREEZE, since nothing on the map screen moves the
 * selection -- Up only triggers generation of whatever is already selected,
 * which is why injecting more presses regenerated the same map.
 *
 * ── WHAT WAS ACTUALLY WRONG ──────────────────────────────────
 *
 * THE RANGE PRIMITIVE READ THE WRONG BYTE. 01:f877 multiplies by the LOW byte
 * of the random word, not the high one, and every loop bound and coordinate in
 * the generator comes through it -- so this one line made every map wrong
 * while leaving all the structure plausible. See the note on the register
 * moves in sc_mapgen_rand_below; the XBA at $f882 looks like it selects the
 * high byte, but LDA $79 overwrites A before the multiply.
 *
 * THE SEED BYTES ARE WORD READS. 03:d842 onward runs with m=0, so LDA $0b27
 * fetches $0b27 and $0b28 together, and LDA $0b29 fetches $0b29 and $0b2a --
 * and $0b2a is the KEPT index of the PREVIOUS map. Generation is therefore not
 * a pure function of the selected index: it depends on which map was generated
 * before it. Reading them as bytes gave the right answer for map 3 by luck,
 * and the wrong one for map 0, where $0b2a = FF makes the ASL carry out and
 * the pre-step count 2 rather than 1.
 *
 * THE FRAMED BRANCH IS NOT A SHORTCUT. 01:f22c ends with JSR $f444 and
 * JSR $f3a3 -- it runs the shoreline and the scatter itself. Stopping at the
 * frame left us emitting 4 distinct values against the guest's 37, and 233
 * draws against thousands. Its vertical edge loop at $f2be was also only a
 * comment saying "the same again", inferred from the horizontal one; it is
 * the same shape but not the same numbers ($006e/$0072 where the horizontal
 * pass uses $005a/$005e, and a limit of $005f rather than $0073).
 *
 * Two more, both real:
 *
 *   01:f380 stores each value TWICE, to the saved centre AND the live walk
 *   position. Setting only the centre left the first walk starting from zero
 *   and stamping a blob in the top-left corner that the ROM never draws.
 *
 *   01:f5b9 makes THREE walks; we had two. The third, $f647, is $f600 with a
 *   +3 probe, the small $f794 disc and a 1-in-13 bearing reset.
 *
 * And 01:f843's probe is 16-bit wrapping: computing it as (int)cur_x + 4 turns
 * a position of -1 ($FFFF) into 65535 and rejects a blob that should clip.
 *
 * ── WHAT MISLED THE SEARCH, AND WHAT FOUND IT ─────────────────────
 *
 * Worth keeping, because the wrong methods were convincing for a long time.
 *
 * COMPARING FINAL MAPS BARELY MEASURES A FIX. Once the draw sequence diverges
 * every later loop bound comes from a different stream, so the number moves a
 * couple of points for reasons unconnected to correctness. Chasing it produced
 * a string of confident and wrong conclusions: that the reference had
 * accumulated several passes, that a pipeline stage was missing, that we
 * under-drew by half (measured at the wrong seed), that the seeding could not
 * be found. Use SC_MAPGEN_SNAP_AT, which compares at an exact draw count.
 *
 * PERCENT-OF-CELLS-IDENTICAL IS USELESS EARLY. At 80 draws the guest has drawn
 * 289 cells of 12000, so a completely wrong map still scores ~96% on the empty
 * ones. Score the drawn cells -- intersection over union -- which separated a
 * real candidate at 43% from noise at 0% where the raw figure read 94-97% for
 * everything.
 *
 * WHAT ACTUALLY FOUND IT was reading the guest's own variables. $0457/$0459
 * hold the centre point: the guest had (68,59) and we had (74,54), which is
 * a two-value comparison that settles in one step what a thousand map
 * diffs could not. When the ROM keeps a value in RAM, read it rather than
 * inferring it from what it eventually draws.
 *
 * The pre-step count is 4, as 03:d85e computes. An earlier sweep favoured 11
 * against a single early dump; that was the vblank slop above, and it
 * disappeared once the range primitive was fixed.

 */




/* ── Cell read and write ───────────────────────────────────────────────────
 *
 * 01:f8e9 reads and 01:f8af writes, and they are exact mirrors:
 *
 *     y * 120 via the hardware multiplier, + x, ASL for the word array
 *     read:   LDA $7f0200,X / AND #$03ff
 *     write:  STA $7f0200,X
 *
 * Note the ASYMMETRY: the read masks to 10 bits, the write does not. So the
 * upper 6 bits are flag space that the generator never sets but the read
 * always discards -- which is consistent with 03:cf82's later copy masking
 * with AND #$03ff as it moves the map on. Anything reproducing this must mask
 * on read only, or it will disagree the moment something else sets a flag. */
uint16_t sc_mapgen_read_cell(const ScMapGenState *st, unsigned x, unsigned y) {
    return (uint16_t)(st->map[y*map_width(st)+x] & 0x03ffu);
}

void sc_mapgen_write_cell(ScMapGenState *st, unsigned x, unsigned y, uint16_t v) {
    st->map[y*map_width(st)+x] = v;      /* unmasked, as the ROM does */
}

/* ── The per-cell draw ─────────────────────────────────────────────────────
 *
 * 01:f7e7, which turns one brush value into one map cell. The whole routine:
 *
 *     PHA / CMP #$0000 / BEQ out         ; brush 0 = outside, draw nothing
 *     x = $0447 + $043b -> $0453,$044f   ; brush offset + blob position
 *     y = $0449 + $043d -> $0455,$0451
 *     JSR $f843 / BCS out                ; off the map, draw nothing
 *     JSR $f8e9                          ; A = the cell that is already there
 *     PLX / CPX #$0002 / BEQ centre      ; brush 2 is the centre, special
 *     CMP #$0001 / BEQ out               ; existing 1 -> leave it
 *     CMP #$0002 / BEQ out               ; existing 2 -> leave it
 *     BNE write
 *   centre:
 *     x == 0 or x == 120 or y == 0 or y == 100 -> LDX #$0001   ; degrade
 *   write:
 *     TXA / JSR $f8af                    ; store
 *
 * Two rules fall out, and both matter for reproducing a map:
 *
 * 1. EXISTING 1 AND 2 ARE PROTECTED. A later blob cannot overwrite an earlier
 *    blob's interior or its centre; it can only write into 0 and 3. So the
 *    order features run in changes the result even when every draw is correct
 *    -- overlapping blobs are decided by who got there first.
 *
 * 2. THE CENTRE MARKER DEGRADES AT THE BORDER. Brush value 2 on the map edge
 *    is written as 1 instead. That is why the border cells never carry the
 *    marker, and it is a rule you would not guess from the brush table alone.
 *
 * With $f8af transcribed the blob path is complete and runnable end to end:
 * cluster -> jittered point -> 9x9 disc -> this conditional write. */
void sc_mapgen_draw_cell(ScMapGenState *st, unsigned brush, int ox, int oy) {
    if (brush == 0) return;                       /* outside the disc */
    /* 16-BIT WRAPPING, as the ROM's ADC does -- see sc_mapgen_probe_in_bounds.
     * Computing these as (int)cur_x + ox turns a blob position of -1 ($FFFF)
     * into 65535 rather than -1, so every cell of a blob straddling the left
     * or top edge is rejected instead of clipped. */
    const uint16_t x = (uint16_t)(st->cur_x + (uint16_t)ox);   /* $0447 + $043b */
    const uint16_t y = (uint16_t)(st->cur_y + (uint16_t)oy);   /* $0449 + $043d */
    if (!sc_mapgen_probe_in_bounds(st, st->cur_x, st->cur_y,
                                   (uint16_t)ox, (uint16_t)oy))
        return;                                   /* JSR $f843 / BCS */

    unsigned value = brush;
    if (brush == 2) {
        /* The centre marker is not placed on the border; it becomes a 1. */
        if (x == 0 || x == map_width(st) || y == 0 || y == map_height(st)) value = 1;
    } else {
        const unsigned existing = sc_mapgen_read_cell(st, x, y);
        if (existing == 1 || existing == 2) return;   /* protected, leave it */
    }
    sc_mapgen_write_cell(st, x, y, (uint16_t)value);
}

/* ── The blob brush ────────────────────────────────────────────────────────
 *
 * 01:f71d, the 3-in-4 draw the clustered-blob feature calls:
 *
 *     LDA #$0008 / STA $0447            ; a = 8
 *   outer ($f725):
 *     LDA #$0008 / STA $0449            ; b = 8
 *   inner ($f72b):
 *     LDA $0449 / STA $4202             ; b
 *     LDA #$09  / STA $4203             ; x 9   -- hardware multiplier again
 *     LDA $4217/$4216 / CLC / ADC $0447 ; index = b*9 + a
 *     TAX / LDA $01f6cc,X / AND #$00ff
 *     JSR $f7e7                         ; draw that value at this offset
 *     DEC $0449 / BPL inner
 *     DEC $0447 / BPL outer
 *
 * So it stamps a 9x9 brush, and the table at $01f6cc IS A CIRCLE -- which is
 * about as clear a confirmation as reverse engineering offers, because the
 * data draws itself:
 *
 *     . . . o o o . . .        0  outside   (24 cells)
 *     . . o # # # o . .        3  rim       (20)
 *     . o # # # # # o .        1  interior  (36)
 *     o # # # # # # # o        2  centre    (1, dead centre)
 *     o # # # 2 # # # o
 *     o # # # # # # # o
 *     . o # # # # # o .
 *     . . o # # # o . .
 *     . . . o o o . . .
 *
 * A radius-4 disc with a distinguished rim and a marked centre. The rim value
 * being separate from the interior is what lets the caller draw a shoreline
 * and a fill in one pass; the single centre cell is presumably where a feature
 * anchor goes.
 *
 * Note the loops run 8 down to 0 INCLUSIVE (BPL, not BNE), so it really is
 * 9x9 and not 8x8 -- and the same is true of $f794's 6x6 below. */
enum { SC_MAPGEN_BRUSH = 9 };
extern const unsigned char sc_mapgen_brush[81];
const unsigned char sc_mapgen_brush[81] = {
    0,0,0,3,3,3,0,0,0,
    0,0,3,1,1,1,3,0,0,
    0,3,1,1,1,1,1,3,0,
    3,1,1,1,1,1,1,1,3,
    3,1,1,1,2,1,1,1,3,
    3,1,1,1,1,1,1,1,3,
    0,3,1,1,1,1,1,3,0,
    0,0,3,1,1,1,3,0,0,
    0,0,0,3,3,3,0,0,0,
};

void sc_mapgen_stamp_blob(ScMapGenState *st) {
    for (int a = 8; a >= 0; a--)
        for (int b = 8; b >= 0; b--) {
            const unsigned char v = sc_mapgen_brush[b * SC_MAPGEN_BRUSH + a];
            sc_mapgen_draw_cell(st, v, a, b);   /* JSR $f7e7 */
        }
}

/* ── Two draws, keep the smaller ───────────────────────────────────────────
 *
 * 01:f85d: JSR $f877 twice with the same N, then CMP and return whichever is
 * smaller. So it is min(rand(0..N), rand(0..N)) -- a distribution skewed
 * toward small values, and TWO PRNG steps per call, not one.
 *
 * Used by 01:f22c to keep its edge blobs near the border while still varying
 * them: a plain uniform draw would scatter them across the band evenly. */
uint16_t sc_mapgen_rand_min2(ScMapGenPrng *p, uint16_t n) {
    const uint16_t a = sc_mapgen_rand_below(p, n);
    const uint16_t b = sc_mapgen_rand_below(p, n);
    return a < b ? a : b;
}

/* ── 01:f22c -- the framed map ─────────────────────────────────────────────
 *
 * The alternative taken by 86/256 of seeds. Where the feature chain grows an
 * organic map, this one BUILDS A FRAME, and it never calls the chain at all:
 *
 *     fill every cell with 1                    ; x 119..0, y 99..0
 *     clear x 5..114, y 5..94 to 0              ; ascending, note, not the
 *                                               ; descending sweep used elsewhere
 *     for x = 0, 2, 4, ... < 115:
 *       y = min2(18)          / stamp the 9x9 disc
 *       y = 90 - min2(18)     / stamp the 9x9 disc
 *       y = 0                 / stamp the 6x6 disc
 *       y = 94                / stamp the 6x6 disc
 *     then the mirror loop for y = 0, 2, 4, ... < 95 down the left and right
 *
 * So: a solid field, a cleared interior, and blobs marched in steps of two
 * along all four edges -- big discs jittered inward by min-of-two, small discs
 * pinned exactly on the edge. That produces a bordered basin rather than the
 * scattered coastline the chain makes, which is presumably the visual
 * distinction between the two kinds of map.
 *
 * PRNG cost: 4 steps per column (two min2 calls), and none for the fixed-y
 * small discs. The fill and clear draw nothing at all.
 *
 * The vertical half of the loop, from $f2be, mirrors this but has not been
 * transcribed line by line -- it is written here from its opening and the
 * symmetry, and is the one part of this routine not read directly. */
static void native_coast(ScMapGenPrng *p, ScMapGenState *st) {
    for (int x = (int)map_width(st) - 1; x >= 0; x--)
        for (int y = (int)map_height(st) - 1; y >= 0; y--)
            sc_mapgen_write_cell(st, (unsigned)x, (unsigned)y, 1);

    for (int x = 5; x < (int)map_width(st)-5; x++)
        for (int y = 5; y < (int)map_height(st)-5; y++)
            sc_mapgen_write_cell(st, (unsigned)x, (unsigned)y, 0);

    /* $f276: along the top and bottom edges. x steps by 2 to CMP #$0073. */
    for (int x = 0; x < (int)map_width(st)-5; x += 2) {
        st->cur_x = (uint16_t)x;
        st->cur_y = sc_mapgen_rand_min2(p, 0x0012);            /* $f28d */
        sc_mapgen_stamp_blob(st);
        st->cur_y = (uint16_t)(map_height(st)-10 - sc_mapgen_rand_min2(p, 0x0012));
        sc_mapgen_stamp_blob(st);
        st->cur_y = 0;      sc_mapgen_stamp_blob_small(st);
        st->cur_y = (uint16_t)(map_height(st)-6); sc_mapgen_stamp_blob_small(st);
    }

    /* $f2be: the same down the left and right edges, axes swapped. This was a
     * comment saying "the same again" -- inferred from the horizontal loop,
     * never read. It is the same SHAPE but not the same numbers: y steps by 2
     * to CMP #$005f, and the far edge is $006e and $0072 where the horizontal
     * pass uses $005a and $005e. The loop is also entered mid-body (BRA $f2d3)
     * so the first iteration runs at y = 0. */
    for (int y = 0; y < (int)map_height(st)-5; y += 2) {
        st->cur_y = (uint16_t)y;                               /* $043d */
        st->cur_x = sc_mapgen_rand_min2(p, 0x0012);            /* $f2d9 */
        sc_mapgen_stamp_blob(st);                              /* $f2df */
        st->cur_x = (uint16_t)(map_width(st)-10 - sc_mapgen_rand_min2(p, 0x0012));
        sc_mapgen_stamp_blob(st);                              /* $f2f3 */
        st->cur_x = 0;      sc_mapgen_stamp_blob_small(st);    /* $f2fc */
        st->cur_x = (uint16_t)(map_width(st)-6); sc_mapgen_stamp_blob_small(st);    /* $f305 */
    }

}
void sc_mapgen_framed_map(ScMapGenPrng *p, ScMapGenState *st) {
    native_coast(p,st);
    /* $f30a / $f30d. The framed branch is NOT a shortcut past the chain: it
     * runs the shoreline and the scatter itself, at the end. Stopping at the
     * frame left us emitting only values 0-3 where the guest has all 37, and
     * 233 draws where it takes thousands. */
    sc_mapgen_shoreline(p, st);            /* JSR $f444 */
    sc_mapgen_feature_scatter(p, st);      /* JSR $f3a3 */
}

/* ── 01:f502 -- the second fitting pass ────────────────────────────────────
 *
 * The scatter feature calls this TWICE in a row. Same sweep shape as $f444 --
 * x 119..0 outer, y 99..0 inner -- but every rule differs:
 *
 *     JSR $f8e9 / CMP #$0014 / BCC next / CMP #$0026 / BCS next
 *                                     ; act on a RANGE, 0x14..0x25, not one value
 *     mask = 0; X = 3..0:
 *       ASL $045d
 *       (nx,ny) = (x,y) + (dx[X], dy[X])
 *       JSR $f843 / BCS +             ; OFF-MAP DOES NOT SET THE BIT
 *       JSR $f8e9 / CMP #$0014 / BCC + / CMP #$0026 / BCS +
 *       INC $045d                     ; neighbour in the SAME class sets it
 *     LDX $045d / LDA $01f4f2,X / BEQ write
 *     JSL $00824b / LSR A / BCS write / ADC #$0009    ; else half the time, +9
 *   write: JSR $f8af
 *
 * Two inversions against $f444, and getting either backwards produces a map
 * that looks fine and is wrong:
 *
 * 1. $f444 sets a mask bit when the neighbour is EMPTY or OFF-MAP. This sets it
 *    when the neighbour is in the SAME class, and off-map sets NOTHING. So the
 *    border reads as open space in one pass and as foreign material in the
 *    other.
 * 2. $f444 skips its variant when the table entry is 1; this skips when the
 *    entry is 0, and its variant is +9, not +8.
 *
 * The table at $01f4f2 is: 00 00 00 16 00 00 14 15 00 1C 00 19 1A 1B 17 18.
 * Ten of the sixteen are 0, i.e. most neighbour patterns write nothing new.
 *
 * Being called twice matters: the pass reads and writes one buffer, so the
 * second run sees the first run's output. */
void sc_mapgen_fit_pass(ScMapGenPrng *p, ScMapGenState *st) {
    static const unsigned char fit2[16] = {
        0x00,0x00,0x00,0x16,0x00,0x00,0x14,0x15,
        0x00,0x1C,0x00,0x19,0x1A,0x1B,0x17,0x18,
    };
    static const int ndx[4] = { -1, 0, +1, 0 };
    static const int ndy[4] = {  0, +1, 0, -1 };
    #define IN_CLASS(v) ((v) >= 0x14u && (v) < 0x26u)

    for (int x = (int)map_width(st) - 1; x >= 0; x--) {
        for (int y = (int)map_height(st) - 1; y >= 0; y--) {
            const unsigned v = sc_mapgen_read_cell(st, (unsigned)x, (unsigned)y);
            if (!IN_CLASS(v)) continue;

            unsigned mask = 0;
            for (int i = 3; i >= 0; i--) {
                const int nx = x + ndx[i], ny = y + ndy[i];
                mask <<= 1;
                if (!map_bounds(st,nx,ny)) continue;   /* off-map: no bit */
                if (IN_CLASS(sc_mapgen_read_cell(st, (unsigned)nx, (unsigned)ny)))
                    mask |= 1u;
            }

            unsigned tile = fit2[mask & 15u];
            if (tile != 0) {
                if ((sc_mapgen_prng_step(p) & 1u) == 0) tile += 9u;   /* BCS skips */
            }
            sc_mapgen_write_cell(st, (unsigned)x, (unsigned)y, (uint16_t)tile);
        }
    }
    #undef IN_CLASS
}

/* ── 01:f444 implemented ───────────────────────────────────────────────────
 *
 * The loop structure, which the earlier note did not have:
 *
 *     LDA #$0077 / STA $043f          ; x = 119, OUTER
 *   x_loop ($f44c):
 *     LDA #$0063 / STA $0441          ; y = 99, INNER
 *   y_loop ($f452):
 *     JSR $f8e9 / CMP #$0003 / BNE next   ; only act on cells that are 3
 *     ... build the 4-bit neighbour mask ...
 *     LDX $045b / LDA $01f434,X
 *     CMP #$0001 / BEQ +                  ; table value 1: no variant
 *     JSL $00824b / LSR A / BCC +         ; else half the time
 *     CLC / ADC #$0008                    ;   use tile+8
 *     + JSR $f8af                         ; write it back
 *   next: DEC $0441 / BPL y_loop
 *         DEC $043f / BPL x_loop
 *
 * So it sweeps every cell and rewrites only the RIM value 3 -- the outline the
 * disc brushes leave -- into a proper edge tile. Interiors (1) and centres (2)
 * are untouched, which is why the brushes bother to distinguish rim from fill
 * in the first place.
 *
 * A mask bit is set when the neighbour is EMPTY (0) **or off the map**, so the
 * map border behaves like open space and coastlines close correctly along the
 * edges. That equivalence is deliberate, not a bounds-check accident.
 *
 * SCAN ORDER MATTERS: x descends in the outer loop and y in the inner, so the
 * sweep is column-major from the bottom-right. Since the pass both reads and
 * writes the same buffer, a cell fitted early is visible to cells fitted later
 * -- run it row-major, or ascending, and the result differs. It also consumes
 * one PRNG step per rim cell whose table entry is not 1, so the order changes
 * the stream as well as the map. */
void sc_mapgen_shoreline(ScMapGenPrng *p, ScMapGenState *st) {
    static const unsigned char fit[16] = {
        0x01,0x07,0x0A,0x09,0x08,0x01,0x0B,0x01,
        0x05,0x04,0x01,0x01,0x06,0x01,0x01,0x01,
    };
    static const int ndx[4] = { -1, 0, +1, 0 };   /* $01f42c */
    static const int ndy[4] = {  0, +1, 0, -1 };  /* $01f430 */

    for (int x = (int)map_width(st) - 1; x >= 0; x--) {
        for (int y = (int)map_height(st) - 1; y >= 0; y--) {
            if (sc_mapgen_read_cell(st, (unsigned)x, (unsigned)y) != 3) continue;

            unsigned mask = 0;
            for (int i = 3; i >= 0; i--) {        /* X = 3..0, ASL before each */
                const int nx = x + ndx[i], ny = y + ndy[i];
                mask <<= 1;
                if (!map_bounds(st,nx,ny) ||
                    sc_mapgen_read_cell(st, (unsigned)nx, (unsigned)ny) == 0)
                    mask |= 1u;                    /* off-map counts as empty */
            }

            unsigned tile = fit[mask & 15u];
            if (tile != 1) {
                if (sc_mapgen_prng_step(p) & 1u) tile += 8u;   /* LSR / BCC */
            }
            sc_mapgen_write_cell(st, (unsigned)x, (unsigned)y, (uint16_t)tile);
        }
    }
}

/* ── The path walk ─────────────────────────────────────────────────────────
 *
 * 01:f600, the walk 01:f5b9 runs twice (out, then back the opposite way):
 *
 *   loop:
 *     x = $043b + 4 / y = $043d + 4 -> $0453,$0455
 *     JSR $f843 / BCS done            ; stop when the DISC would leave the map
 *     JSR $f71d                       ; stamp the 9x9 disc here
 *     JSL $00824b / LSR A / BCC skip  ; bit 0: half the time, no turn
 *       LSR A / BCS -                 ; bit 1: which way
 *       INC $0461 / BRA skip          ;   turn one way
 *     - DEC $0461                     ;   turn the other
 *   skip:
 *     LDA #$000a / JSR $f877 / BNE +  ; 1 in 11
 *     LDA $045f / STA $0461           ;   snap the heading back to the base
 *     + LDA $0461 / JSR $f6ae         ; step one cell
 *     BRA loop
 *
 * So the path is a THICK line -- a 9x9 disc stamped at every step, not a
 * one-cell trail -- that wanders by +/-1 compass point half the time and, one
 * step in eleven, snaps back to the direction it started with. That is what
 * keeps a river meandering without losing its way across the map.
 *
 * Two details worth pinning down:
 *
 * - The bounds check is on (x+4, y+4), the disc's far corner, NOT the walk
 *   position. So it stops while the whole brush still fits, and the path never
 *   gets clipped at the edge.
 * - $045f is the BASE heading and $0461 the current one. 01:f5b9 sets both,
 *   then flips both with EOR #$0004 for the return leg -- so the second pass
 *   wanders around the exact reverse bearing.
 *
 * PRNG cost is 2 per step (the turn draw, and the 1-in-11 via $f877) and the
 * step count is data-dependent, since it runs until the disc would leave the
 * map. */
void sc_mapgen_path_walk(ScMapGenPrng *p, ScMapGenState *st) {
    for (;;) {
        if (!sc_mapgen_probe_in_bounds(st, st->cur_x, st->cur_y, 4, 4)) return;
        sc_mapgen_stamp_blob(st);                       /* JSR $f71d */

        const uint16_t r = sc_mapgen_prng_step(p);
        if (r & 1u) {                                   /* LSR / BCC skip */
            if (r & 2u) st->dir_cur--;                  /* LSR / BCS -> DEC */
            else        st->dir_cur++;                  /*             INC */
        }
        if (sc_mapgen_rand_below(p, 0x000a) == 0)       /* 1 in 11 */
            st->dir_cur = st->dir_base;                 /* LDA $045f / STA $0461 */

        sc_mapgen_move(st, st->dir_cur);                /* JSR $f6ae */
    }
}

/* ── The small brush ───────────────────────────────────────────────────────
 *
 * 01:f794, the 1-in-4 alternative to the 9x9 disc. Identical in shape to
 * 01:f71d but with 5 and 6 where that has 8 and 9, so a 6x6 brush indexed
 * b*6 + a from a table at $01f770. Both index BYTE tables -- neither does the
 * ASL that the map itself needs.
 *
 * That table is a circle too, and a perfectly balanced one:
 *
 *     . . o o . .        0  outside   12 cells
 *     . o # # o .        3  rim       12
 *     o # # # # o        1  interior  12
 *     o # # # # o
 *     . o # # o .
 *     . . o o . .
 *
 * The difference that matters: THERE IS NO CENTRE MARKER. The 9x9 disc has a
 * single value 2 at its centre; this one has none, so a small blob never plants
 * the marker and never triggers the border-degradation rule in 01:f7e7. The
 * two draws are not the same shape at different sizes -- they differ in kind. */
extern const unsigned char sc_mapgen_brush_small[36];
const unsigned char sc_mapgen_brush_small[36] = {
    0,0,3,3,0,0,
    0,3,1,1,3,0,
    3,1,1,1,1,3,
    3,1,1,1,1,3,
    0,3,1,1,3,0,
    0,0,3,3,0,0,
};

void sc_mapgen_stamp_blob_small(ScMapGenState *st) {
    for (int a = 5; a >= 0; a--)
        for (int b = 5; b >= 0; b--)
            sc_mapgen_draw_cell(st, sc_mapgen_brush_small[b * 6 + a], a, b);
}

/* ── The 8-way move ────────────────────────────────────────────────────────
 *
 * 01:f6ae:
 *
 *     AND #$0007 / ASL A / TAX          ; word-indexed
 *     LDA $01f68e,X / ADC $043b / STA $043b     ; x += dx[dir]
 *     LDA $01f69e,X / ADC $043d / STA $043d     ; y += dy[dir]
 *
 * The two 8-entry WORD tables, read from the US ROM:
 *
 *     $01f68e dx: 0000 0001 0001 0001 0000 FFFF FFFF FFFF
 *     $01f69e dy: FFFF FFFF 0000 0001 0001 0001 0000 FFFF
 *
 *     0 N (0,-1)   1 NE (+1,-1)   2 E (+1,0)   3 SE (+1,+1)
 *     4 S (0,+1)   5 SW (-1,+1)   6 W (-1,0)   7 NW (-1,-1)
 *
 * A compass rose from N, clockwise. And it CONFIRMS the reading of 01:f5b9:
 * `dir ^ 4` is exactly the opposite direction here, so that feature's
 * EOR #$0004 really does walk back the way it came. That was inferred from the
 * bit pattern before these tables were read; now it is established. */
static const int kMapGenDx[8] = {  0, +1, +1, +1,  0, -1, -1, -1 };
static const int kMapGenDy[8] = { -1, -1,  0, +1, +1, +1,  0, -1 };

void sc_mapgen_move(ScMapGenState *st, unsigned dir) {
    dir &= 7u;
    st->cur_x = (uint16_t)(st->cur_x + kMapGenDx[dir]);
    st->cur_y = (uint16_t)(st->cur_y + kMapGenDy[dir]);
}

/* ── The walk: what a scattered point actually draws ───────────────────────
 *
 * 01:f3d3, called once per scatter placement. It is not a stamp -- it is a
 * random walk, and that is where the terrain gets its organic shape:
 *
 *     LDA #$0096 / JSR $f877 / ADC #$0032 / STA $0443   ; steps = 50 + r(150)
 *     LDA $044b / STA $043b                             ; start at the
 *     LDA $044d / STA $043d                             ; placement point
 *   loop:
 *     JSL $00824b / AND #$0007 / JSR $f6ae               ; dir = rand & 7, step
 *     LDA $043b / STA $0453 / LDA $043d / STA $0455
 *     JSR $f843 / BCS stop                               ; walked off the map
 *     LDA $043b / STA $044f / LDA $043d / STA $0451
 *     JSR $f8e9 / CMP #$0000 / BNE ...                   ; look at this cell
 *     ...
 *
 * So each of the scatter feature's 50..150 placements spawns a walk of 50..200
 * steps taking random 8-way moves, stopping early if it leaves the map. That
 * composition -- many short random walks from scattered seeds -- is what makes
 * coastlines and rivers look natural rather than blobby, and it explains why
 * the tile fitting in 01:f444 has to run afterwards to tidy the edges.
 *
 * PRNG cost: 1 for the step count, then 1 per step for the direction. The walk
 * can stop early on the bounds check, so THE COST IS DATA-DEPENDENT -- the same
 * property 01:f444 has. Two of the routines now consume a variable number of
 * steps, which means the stream cannot be predicted without running the walk
 * itself.
 *
 * Note the
 * direction here is `& 7` -- eight ways -- while 01:f5b9's path uses `& 3` plus
 * an EOR #$0004 flip. Same encoding, different halves of it. */
void sc_mapgen_walk(ScMapGenPrng *p, ScMapGenState *st) {
    unsigned steps = sc_mapgen_rand_below(p, 0x0096) + 0x0032u;   /* $0443 */
    int x = (int)st->px, y = (int)st->py;                          /* $044b/$044d */
    while (steps) {
        const unsigned dir = sc_mapgen_prng_step(p) & 0x0007u;
        st->cur_x = (uint16_t)x; st->cur_y = (uint16_t)y;
        sc_mapgen_move(st, dir);                 /* JSR $f6ae */
        x = (int16_t)st->cur_x; y = (int16_t)st->cur_y;
        if (!map_bounds(st,x,y)) break;   /* JSR $f843 / BCS */
        /* JSR $f8e9 / CMP #$0000 / BNE + / LDA #$0018 / JSR $f8af
         *
         * THE WALK DRAWS 0x18, and only onto an EMPTY cell. This is where the
         * 0x14..0x25 class comes from -- the one 01:f502 fits and which nothing
         * else in the generator can produce. An earlier version of this file
         * left the write "not decompiled" and concluded a whole pipeline stage
         * was missing; it was this one line. */
        if (sc_mapgen_read_cell(st, (unsigned)x, (unsigned)y) == 0)
            sc_mapgen_write_cell(st, (unsigned)x, (unsigned)y, 0x0018);
        steps--;
    }
    st->cur_x = (uint16_t)x;
    st->cur_y = (uint16_t)y;
}

/* ── Bounds check ──────────────────────────────────────────────────────────
 *
 * 01:f843. Carry SET means out of range, which is the opposite of the usual
 * reading and is what $f444 branches on:
 *
 *     LDA $0453 / BMI out          ; x negative
 *     CMP #$0078 / BCS out         ; x >= 120
 *     LDA $0455 / BMI out          ; y negative
 *     CMP #$0064 / BCS out         ; y >= 100
 *     CLC / RTS                    ; in range
 *   out: SEC / RTS
 *
 * The map is 120 x 100 -- now confirmed a THIRD independent way. The power
 * bitmap at 03:b0f8 bounds it at 12000 bits, the scatter feature draws
 * rand(0..119) by rand(0..99), and here the bounds check itself uses 0x78 and
 * 0x64. Three unrelated routines agreeing is about as settled as this gets.
 *
 * The signed test matters: coordinates reach here already jittered (the
 * clustered-blob feature can push +/-6 past an edge), so negatives are
 * expected, not defensive. */
int sc_mapgen_in_bounds(int x, int y) {
    return x >= 0 && x < SC_MAPGEN_W && y >= 0 && y < SC_MAPGEN_H;
}

/* ── Cell address ──────────────────────────────────────────────────────────
 *
 * 01:f8e9 begins by computing the row offset with the hardware multiplier:
 *
 *     SEP #$20 / REP #$10
 *     LDA $0451 / STA $4202        ; WRMPYA = y
 *     LDA #$78  / STA $4203        ; WRMPYB = 120  -> starts y * 120
 *     PHA / PLA / NOP              ; the mandatory wait
 *     LDA $4217 / XBA / LDA $4216  ; the 16-bit product
 *
 * So the map is row-major with a stride of 120, and the cell index is
 * y * 120 + x. That is the same 8x8 multiplier trick 01:f877 uses for its
 * range draw -- this ROM leans on it wherever a multiply is needed.
 *
 * The remainder of $f8e9 -- adding x and fetching the tile -- is not
 * transcribed yet, which is why this is an index helper and not the classifier
 * $f444 actually calls. */
unsigned sc_mapgen_cell_index(unsigned x, unsigned y) {
    return y * SC_MAPGEN_W + x;
}

/* ── 01:f444 -- the shoreline pass ─────────────────────────────────────────
 *
 * This one is a different kind of routine from the other four, and worth
 * writing down before it is coded, because implementing it as "another feature
 * that scatters things" would be wrong.
 *
 * It is a NEIGHBOURHOOD SCAN -- tile fitting, not placement:
 *
 *     LDA #$0077 / STA $043f      ; 119 -- the full map width
 *     LDA #$0063 / STA $0441      ; 99  -- the full map height
 *     copy both to $044f / $0451
 *     JSR $f8e9 / CMP #$0003 / BNE out      ; classify this cell; only act on 3
 *     STZ $045b                              ; the neighbour mask
 *     LDA #$0003 / STA $0443                 ; four neighbours, X = 3..0
 *   loop ($f472):
 *     ASL $045b                              ; shift the mask up one bit
 *     LDX $0443
 *     LDA $01f42c,X / ADC $043f / STA $0453,$044f    ; neighbour x = x + dx[X]
 *     LDA $01f430,X / ADC $0441 / ...                ; neighbour y = y + dy[X]
 *     JSR $f843 / BCS +                      ; bounds check, carry = out of range
 *     JSR $f8e9 / CMP #$0000 / BNE ++        ; classify the neighbour
 *   + INC $045b                              ; set the low bit of the mask
 *   ++ DEC $0443 / BPL loop                  ; four times
 *     LDX $045b / LDA $01f434,X              ; mask -> tile id, 16-entry table
 *     CMP #$0001 / BEQ +
 *     STA $79 / JSL $00824b / LSR A / LDA $79
 *     BCC + / CLC / ADC #$0008               ; one random bit picks a +8 variant
 *   + ...
 *
 * So: for each cell of a given class, build a 4-bit mask of which neighbours
 * are of another class, look the mask up in a 16-entry table at $01f434, and
 * for most results toss a coin to choose between that tile and tile+8. That is
 * shoreline / edge fitting -- the thing that makes coastlines join up -- with a
 * random variant for visual variety.
 *
 * THE THREE TABLES, read out of the US ROM (LoROM, no copier header):
 *
 *     $01f42c  dx   FF 00 01 00   =  -1  0 +1  0
 *     $01f430  dy   00 01 00 FF   =   0 +1  0 -1
 *
 * so the four neighbours are W, S, E, N in table order. The scan runs X = 3
 * down to 0 (DEC $0443 / BPL) and does ASL $045b BEFORE each possible INC, so
 * the FIRST neighbour visited ends up in the HIGH bit: the mask reads N E S W
 * from bit 3 down.
 *
 *     $01f434  mask -> tile, 16 entries indexed by that mask:
 *
 *       mask 0123456789ABCDEF
 *       tile 01 07 0A 09 08 01 0B 01 05 04 01 01 06 01 01 01
 *
 * Eight of the sixteen map to tile 01, which reads as "nothing special here";
 * the other eight are the genuine edge and corner pieces. Note masks 5 and A --
 * the two diagonal-opposite pairs -- both fall back to 01, which is what a
 * four-neighbour scheme has to do since it cannot express a diagonal.
 *
 * None of the three was in docs/ROM_MAP.md; they are added there too.
 *
 * The PRNG cost is DATA-DEPENDENT here, unlike every other routine so far: one
 * step per cell that reaches the coin toss, i.e. it depends on the map built so
 * far. Any implementation has to reproduce the scan order exactly or the stream
 * desynchronises even with correct tile choices.
 *
 *          01:f22c   86 instructions
 *          01:f444   65
 *
 * 02:923f  zero-fills $7EA400-$7EBFFF (7168 bytes), then sets up the DMA. The
 *          upload side, not generation proper -- a native generator writes the
 *          cells directly and does not need it.
 *
 * ── FIRST COMPARISON AGAINST A REAL MAP ───────────────────────────────────
 *
 * SC_MAPGEN_SELFTEST=<index> runs this generator alone -- no ROM, no
 * emulation -- and writes the 12000 cells to SC_MAPGEN_OUT.
 *
 * For index 0, against savestate_5 read at $7F0200:
 *
 *              ours     golden
 *     value 0   9303      7742
 *     value 1   1987      1654
 *     value 2    218       183
 *     0x18         0       376
 *     0x21         0       365
 *
 * The 0/1/2 counts are the right order of magnitude, and 2 -- the centre
 * marker, written by exactly one rule -- is within 20%. So the blob machinery
 * is doing something close to right.
 *
 * But WE NEVER PRODUCE ANY VALUE ABOVE 3, and the golden map has 37 distinct
 * values including 0x18 and 0x21. Sweeping the two unknown seeding inputs
 * (SC_MAPGEN_CARRY, SC_MAPGEN_A over 8 combinations) changes the counts and
 * even flips which branch is taken -- carry=0 A=0x8570 takes the feature chain
 * where the rest take the framed map -- but none produces a single cell above
 * 3. That is not a tuning gap, it is a missing stage.
 *
 * The contradiction to resolve: 01:f502 fits cells in class 0x14..0x25, and
 * the scatter feature calls it. So by the time it runs, cells in that range
 * must already exist -- yet every write this file makes comes from a brush
 * (1, 2, 3) or a fit table (1, 4..0x13 with the +8 variant). Nothing reaches
 * 0x14.
 *
 * Two candidates, and they are distinguishable:
 *
 *  a) A translation stage between the generator's classes and terrain tile
 *     ids, which the captured map is downstream of. 03:cf82's masked copy is
 *     the obvious suspect, and $0094bc -- called by the dispatcher before the
 *     chain, still not read -- is another.
 *  b) The brushes do not write raw 1/2/3 after all, and 01:f7e7's TXA writes
 *     something this transcription got wrong.
 *
 * $0094bc READ, and it is neither: it is a map clear.
 *
 *     LDX #$0000 / LDY #$5dc0 / LDA #$00
 *     - STA $7f0200,X / INX / DEY / BNE -
 *
 * 24000 bytes, i.e. all 12000 cells zeroed before the chain runs. Useful to
 * know -- the generator starts from a blank map, not whatever was there -- but
 * it is not the missing stage.
 *
 * WHICH LEAVES A MORE LIKELY EXPLANATION, and it is that the COMPARISON was
 * wrong rather than the generator. savestate_5 was taken "right after map
 * loaded" on the map-select flow, and that flow was already established to
 * serve the nine PREBUILT scenario maps -- the bulk 1702+3205 byte write with
 * the PRNG still at 0000/0000 is a decompression, not a generation. So the
 * reference is almost certainly a prebuilt map, and a generated map was never
 * being compared against at all. Different vocabulary is exactly what two
 * different kinds of map should look like.
 *
 * A related puzzle supports this. 01:f502 fits cells in class 0x14..0x25, but
 * nothing in the generator can reach that range: the brushes write 1, 2, 3 and
 * 01:f444's table tops out at 0x0B, so even with its +8 variant the maximum is
 * 0x13. On a freshly generated map 01:f502 therefore does nothing at all --
 * which fits a routine meant for maps that already carry real tile ids.
 *
 * ── A REAL GENERATED MAP, AND THE PIPELINE IT REVEALS ─────────────────────
 *
 * Captured at last: savestate_3 with Up held from frame 60 (inputs are blocked
 * until the screen has rendered, so it must be pressed late). The map seeding
 * is identifiable in the PRNG log because its reseed is NOT consecutive --
 * 5B19/426F -- where 00:823e's spin seed always leaves $59/$5b one apart.
 *
 * GENERATION IS NOT ATOMIC. It is spread over hundreds of frames, which is why
 * every earlier attempt to spot it as a per-frame PRNG burst failed: the
 * per-frame deltas stay small throughout. Watching the distinct-value count
 * instead makes the phases obvious:
 *
 *     frames  85..410   4 distinct (0,1,2,3)   the feature chain
 *     frames 435..560  20 distinct (4..0x13)   the fitting passes
 *     frames 585+      37 distinct             a THIRD stage
 *
 * At f=410, just before the fitting runs, the map holds 0:7898 1:3046 2:397
 * 3:659 -- exactly the four values this file writes. So the chain modelled
 * here IS the first phase, and it ends where the fitting begins.
 *
 * The third stage is the answer to the 0x14 puzzle. 01:f502 fits class
 * 0x14..0x25 and nothing in phases 1 or 2 can reach it -- because those values
 * only appear at f=585, after a stage this file does not model at all. That
 * also means 01:f502 as implemented here can never fire on phase-1 output,
 * which is consistent rather than a bug.
 *
 * Comparison against the real thing, for the same seed byte 03:
 *
 *                golden f=410      ours (carry 0)
 *     value 0        7898              9857
 *     value 1        3046              1591
 *     value 2         397               146
 *     value 3         659                 0
 *
 * So we under-draw by roughly half, and we end with no 3s where the golden
 * still has 659 -- consistent with this file running 01:f444 inside the chain
 * (which consumes 3s) at a point where the ROM has not yet reached it. The
 * ordering is the next thing to check, not the individual routines.
 *
 * ── WHERE THE COMPARISON ACTUALLY STANDS ──────────────────────────────────
 *
 * After the walk's 0x18 write was added, against the golden generated map
 * (savestate_3 + Up, f=685):
 *
 *   - THE VOCABULARY MATCHES EXACTLY: 37 distinct values in both.
 *   - Value 0 lands at 7869 against 7898 -- within 0.4%.
 *   - Most other counts are roughly HALF the golden's (0x04: 35 vs 70,
 *     0x07: 19 vs 35, 0x08: 19 vs 37, 0x18: 351 vs 943).
 *   - 4639 of 12000 cells match, 38.7%.
 *
 * That 38.7% should not be read as "nearly 40% right". The map is dominated by
 * zeros, and two maps with 7869 and 5131 zeros would share about 3364 cells by
 * chance alone. So the agreement is only modestly above coincidence, and
 * cell-for-cell we are NOT close.
 *
 * The factor-of-two pattern suggested the reference had accumulated two
 * generation passes, since the preview regenerates while the button is held.
 * TESTED AND WRONG: SC_MAPGEN_REPEAT=2 drops the match to 27.1% and 3 to
 * 21.6%, and at 2 the fitted values vanish entirely because the second pass
 * takes the framed branch and wipes the walk's output. One pass is right.
 *
 * What remains is almost certainly the seeding. 03:d840's entry carry and
 * entry A are still unknown -- neither can be settled from the disassembly --
 * and without the correct stream start every draw after the first diverges,
 * which produces exactly this: right rules, right vocabulary, wrong map. Pin
 * those two down before touching any of the routines.
 *
 * ── Verification, which comes before any of that ───────────────────────────
 *
 * None of this is worth anything until a seed produces an identical map. The
 * harness to build first:
 *
 *   1. DONE for the PRNG, by SAMPLING rather than tracing. There is no
 *      per-opcode hook in this target: the interp816 core never calls
 *      interp816_opcode_hook, and interp_bridge.c -- which owns
 *      g_interp_bridge_pc_hook -- is not compiled into it. Both were wired up
 *      and produced no output whatever.
 *
 *      What works instead: SC_MAPGEN_VERIFY=1 logs $59/$5b once per frame, and
 *      because the state is only 32 bits, a correct step must join consecutive
 *      samples in a few iterations while a wrong one essentially never does.
 *      That is the check above, and it discriminates: 14/14 against 8/14.
 *   2. Capture the map -- NOT YET DONE, and harder than it looks. What was
 *      learned trying, 2026-08-30:
 *
 *      - **Inputs are blocked until the screen has rendered.** Scripted
 *        `--input` at a fixed early frame is simply swallowed, which made the
 *        same button appear to work sometimes and not others. Press late, or
 *        hold.
 *      - On the map-select screen, `B` (mask 0x0001 here) increments `$0b27`;
 *        it was the only one of nine inputs that touched it.
 *      - **The previews are decompressed, not generated.** Loading the map
 *        screen writes 1702 then 3205 bytes into `$7E0200` across two frames
 *        while the PRNG state is still `0000/0000` -- a bulk load with no
 *        randomness drawn is a prebuilt map being decompressed.
 *      - PRNG bursts of 89-130 steps per frame DO occur later, with direct
 *        writes (seeding) among them, but `$0b27` and `$0b2a` never change
 *        across any of it.
 *
 *      `$0b2a`-`$0b2c` is the marker that matters: 03:d873 copies the seed
 *      there immediately after generating, so while it stays put, the
 *      generation path has NOT run. It never moved in any capture.
 *
 *      Conclusion: map-select shows prebuilt scenario maps (there is a 9-entry
 *      map pointer table at 03:ce70), and procedural generation belongs to the
 *      FREE PLAY path.
 *
 *      Free-play states were then captured (slots 4 and 5) and a real seeding
 *      event caught at the frame boundary -- $c7 = C4 becoming $59/$5b =
 *      00C4/00C5, with $0b2a flipping to FF0000, which only 03:d873 does and
 *      only after generating. So the generation path DID run.
 *
 *      RESOLVED, by game knowledge rather than measurement: there are roughly
 *      1000-2000 maps and EACH INDEX GIVES THE SAME MAP ON EVERY RESTART. So
 *      $0b27-$0b29 is a map NUMBER, not entropy -- which is why B on the map
 *      screen walks it 01, 02, 03 -- and generation is deterministic from it
 *      by construction. One index, one map, forever. That is what makes this
 *      whole exercise checkable.
 *
 *      It also explains the seeding confusion above: 00:823e, seeding from the
 *      $c7 spin counter, is IN-GAME randomness and not the map path. Map
 *      generation goes through 03:d840, which folds the index in. Both were
 *      caught live and read as contradictory until this.
 *
 *      GOLDEN REFERENCE: savestate_5, read at $7F0200 -- 12000 words, each
 *      masked with 0x03FF. It holds 37 distinct tile values, dominated by
 *      000 (7742 cells) and 001 (1654), with 018 and 021 next. savestate_3
 *      holds the same map. savestate_2 has 958 distinct values there, which
 *      is a built-up scenario city rather than raw terrain.
 *
 *      An earlier version of this note pointed at $7E0200 and called ITS
 *      contents the golden map. That was the wrong buffer -- 411 distinct
 *      values, far too many to be tiles.
 *
 *      To extract it:
 *        SC_WRAM_DUMP_PATH=<out> SC_DUMP_AT=5 --load-state savestate_5.bin
 *        then read 12000 words from offset 0x10200 ($7F0200 in a 128K dump,
 *        since $7E0000 is offset 0), masking each with 0x03FF.
 *
 *      The dump is ROM-derived and is deliberately NOT committed.
 *   3. Compare per routine, not just at the end. A whole-map mismatch says
 *      nothing about WHICH of six routines is wrong.
 *
 * Two traps this project has already hit that apply directly here: use
 * tools/dis_mx.py and not dis65816.py, because the latter does not track
 * SEP/REP and will mis-size operands after a width change; and do not trust a
 * harness-side number as if it came from the emulator -- the audio work
 * measured the test harness twice before noticing. */

/* Expanded maps retain the cartridge's geometry vocabulary: its 9x9/6x6
 * water brushes, eight-heading river walks, clustered lakes, column-major
 * shoreline pass and forest walks. Repeat native features over the larger
 * world; never scale their brush footprints or assemble tiled map images. */
static uint32_t geo_hash(uint32_t v) {
    v^=v>>16;v*=0x7feb352du;v^=v>>15;v*=0x846ca68bu;return v^(v>>16);
}
static void native_islands(ScMapGenPrng *p,ScMapGenState *st) {
    unsigned width=map_width(st),height=map_height(st);
    unsigned count=width*height/(SC_MAPGEN_CELLS*4);
    for(unsigned island=0;island<count;++island) {
        int cx=24+native_coordinate(p,st,width-49);
        int cy=24+native_coordinate(p,st,height-49);
        /* Overlapping original round brushes make a small bay. A land core
         * creates an island; mark its new water boundary for the ROM fitter. */
        for(int dy=-12;dy<=12;dy+=4)for(int dx=-12;dx<=12;dx+=4) {
            if(dx*dx+dy*dy>144)continue;
            st->cur_x=(uint16_t)(cx+dx-4);st->cur_y=(uint16_t)(cy+dy-4);
            sc_mapgen_stamp_blob(st);
        }
        int radius=3+sc_mapgen_rand_below(p,3);
        for(int dy=-radius;dy<=radius;++dy)for(int dx=-radius;dx<=radius;++dx)
            if(dx*dx+dy*dy<=radius*radius)
                sc_mapgen_write_cell(st,cx+dx,cy+dy,0);
        for(int dy=-radius-1;dy<=radius+1;++dy)for(int dx=-radius-1;dx<=radius+1;++dx) {
            unsigned x=cx+dx,y=cy+dy,value=sc_mapgen_read_cell(st,x,y);
            if((value==1 || value==2) && (!sc_mapgen_read_cell(st,x-1,y) ||
               !sc_mapgen_read_cell(st,x+1,y) || !sc_mapgen_read_cell(st,x,y-1) ||
               !sc_mapgen_read_cell(st,x,y+1))) sc_mapgen_write_cell(st,x,y,3);
        }
    }
}
void sc_mapgen_generate_geographic(ScMapGenPrng *p,ScMapGenState *st,unsigned size) {
    if(!size) {sc_mapgen_generate(p,st);return;}
    if(size>5)size=5;
    st->width=120u<<size;st->height=100u<<size;
    memset(st->map,0,sizeof st->map);
    g_sc_mapgen_cur=st;
    bool coast=(sc_mapgen_prng_step(p)&255)<0x56;
    if(coast)native_coast(p,st);
    /* The river count follows linear extent: each river continues through
     * the real world, while widths stay at the original brush scale. This
     * avoids either one sparse channel or an area-scaled tangle of rivers. */
    unsigned rivers=1u<<size;
    for(unsigned i=0;i<rivers;++i) {
        if(!i)sc_mapgen_feature_centre(p,st);
        else {
            st->x0=st->cur_x=native_coordinate(p,st,st->width-9);
            st->y0=st->cur_y=native_coordinate(p,st,st->height-9);
        }
        sc_mapgen_feature_path(p,st);
    }
    sc_mapgen_feature_clusters(p,st);
    native_islands(p,st);
    sc_mapgen_shoreline(p,st);
    sc_mapgen_feature_scatter(p,st);
}

/* Alternate procedural terrain retained from Enhanced Beta 16. */
static uint32_t geo_next(uint32_t *s) { *s+=0x9e3779b9u;return geo_hash(*s); }
static int geo_lerp(int a,int b,unsigned t) {
    return a+(int)(((int64_t)(b-a)*t)/65536);
}
static unsigned geo_smooth(unsigned t) {
    return (unsigned)(((uint64_t)t*t*(196608u-2*t))>>32);
}
static unsigned geo_noise(unsigned x,unsigned y,unsigned frequency,uint32_t seed) {
    unsigned ax=x*frequency,ay=y*frequency,ix=ax>>16,iy=ay>>16;
    unsigned tx=geo_smooth(ax&65535),ty=geo_smooth(ay&65535);
    int a=geo_hash(seed+ix*374761393u+iy*668265263u)>>16;
    int b=geo_hash(seed+(ix+1)*374761393u+iy*668265263u)>>16;
    int c=geo_hash(seed+ix*374761393u+(iy+1)*668265263u)>>16;
    int d=geo_hash(seed+(ix+1)*374761393u+(iy+1)*668265263u)>>16;
    return geo_lerp(geo_lerp(a,b,tx),geo_lerp(c,d,tx),ty);
}
static int geo_profile(const int *a,unsigned v) {
    unsigned index=v*8>>16,t=geo_smooth((v*8)&65535);
    return geo_lerp(a[index],a[index+1],t);
}
/* Expanded maps add watersheds at cartridge tile scale. Coordinates here
 * are world cells, never fractions of the complete map: adding land cannot
 * enlarge river widths, lake diameters or forest patches. */
static bool geo_fixed_water(unsigned x,unsigned y,unsigned width,unsigned height,
                            unsigned style,bool vertical,bool coast_side,
                            uint32_t seed,unsigned rough) {
    unsigned major=vertical?y:x,minor=vertical?x:y;
    bool water=false;
    if(style==1 || style==3) {
        unsigned extent=vertical?width:height;
        unsigned edge=coast_side?extent-1-minor:minor;
        unsigned coast=8+geo_noise((uint64_t)major*65536/96,0,1,seed^0x54321u)*12/65536;
        water|=edge<coast;
    }
    if(style==2) {
        unsigned ex=x<width/2?x:width-1-x,ey=y<height/2?y:height-1-y;
        water|=ex<4+rough/6554 || ey<4+rough/6554;
    }
    /* Independently seeded small lakes, rather than one map-sized ellipse. */
    uint32_t lake=geo_hash(seed+(x/64)*374761393u+(y/64)*668265263u);
    if(!water && (lake&1)) {
        int cx=16+(lake>>1)%32,cy=16+(lake>>6)%32;
        int rx=4+(lake>>11)%7,ry=4+(lake>>15)%6;
        int dx=(int)(x%64)-cx,dy=(int)(y%64)-cy;
        if(abs(dx)<=rx+1 && abs(dy)<=ry+1) {
            int64_t ellipse=(int64_t)dx*dx*ry*ry+(int64_t)dy*dy*rx*rx;
            water=ellipse*65536<(int64_t)rx*rx*ry*ry*(48000+rough/2);
        }
    }
    return water;
}
static void geo_disc(uint8_t *classes,unsigned width,unsigned height,int x,int y,int radius) {
    for(int dy=-radius;dy<=radius;++dy)for(int dx=-radius;dx<=radius;++dx) {
        int px=x+dx,py=y+dy;
        if(dx*dx+dy*dy<=radius*radius && px>=0 && py>=0 && px<(int)width && py<(int)height)
            classes[py*width+px]=1;
    }
}
/* Curved reaches use native-sized round brushes. Independent headings,
 * bends and lengths replace the old equally spaced, parallel river bands. */
static void geo_reach(uint8_t *classes,unsigned width,unsigned height,
                      int ax,int ay,int bx,int by,uint32_t seed,int radius) {
    int dx=bx-ax,dy=by-ay,length=abs(dx)+abs(dy);
    if(!length)return;
    int bend_limit=length/2;if(bend_limit>140)bend_limit=140;
    int bend1=(int)(geo_hash(seed)%(2*bend_limit+1))-bend_limit;
    int bend2=(int)(geo_hash(seed+1)%(2*bend_limit+1))-bend_limit;
    int cx=ax+dx/3-dy*bend1/length,cy=ay+dy/3+dx*bend1/length;
    int ex=ax+2*dx/3-dy*bend2/length,ey=ay+2*dy/3+dx*bend2/length;
    for(int i=0;i<=length*2;++i) {
        double t=(double)i/(length*2),u=1-t;
        int x=(int)(u*u*u*ax+3*u*u*t*cx+3*u*t*t*ex+t*t*t*bx+.5);
        int y=(int)(u*u*u*ay+3*u*u*t*cy+3*u*t*t*ey+t*t*t*by+.5);
        geo_disc(classes,width,height,x,y,radius);
    }
}
static void geo_rivers(uint8_t *classes,unsigned width,unsigned height,uint32_t seed,bool vertical) {
    /* One connected crossing supplies a dependable river for every seed. */
    unsigned extent=vertical?height:width;
    int previous=60;
    for(unsigned major=0;major<=extent+32;major+=32) {
        int center=60+((int)geo_noise((uint64_t)major*65536/96,0,1,seed^0x193a5u)-32768)*80/65536;
        if(major)geo_reach(classes,width,height,vertical?previous:(int)major-32,
            vertical?(int)major-32:previous,vertical?center:(int)major,
            vertical?(int)major:center,geo_hash(seed+major),4);
        previous=center;
    }
    for(unsigned gy=0;gy<(height+179)/180;++gy)for(unsigned gx=0;gx<(width+199)/200;++gx) {
        uint32_t rng=geo_hash(seed^gx*374761393u^gy*668265263u);
        unsigned count=1+geo_next(&rng)%3;
        for(unsigned k=0;k<count;++k) {
            int ax=gx*200+geo_next(&rng)%200,ay=gy*180+geo_next(&rng)%180;
            int dx=(int)(geo_next(&rng)%281)-140,dy=(int)(geo_next(&rng)%281)-140;
            if(abs(dx)+abs(dy)<90)dy+=dy<0?-90:90;
            int bx=ax+dx,by=ay+dy;
            uint32_t reach=geo_next(&rng);int radius=3+geo_next(&rng)%3;
            geo_reach(classes,width,height,ax,ay,bx,by,reach,radius);
            if(reach&1)geo_disc(classes,width,height,ax,ay,6+(reach>>8)%5);
            /* Short tributaries meet this reach, rather than running as a
             * second parallel line for the whole length of the map. */
            int length=abs(dx)+abs(dy);
            if(reach&2)geo_reach(classes,width,height,ax,ay,ax-dy*70/length,
                ay+dx*70/length,reach^0x58d3u,3);
        }
    }
}
/* Native-sized island groups sit in irregular bays/lakes, rather than making
 * the complete enlarged map one giant island with a thin perimeter moat. */
static void geo_islands(uint8_t *classes,unsigned width,unsigned height,uint32_t seed,unsigned style) {
    for(unsigned gy=0;gy<(height+159)/160;++gy)for(unsigned gx=0;gx<(width+179)/180;++gx) {
        uint32_t rng=geo_hash(seed^0xa7c913u^gx*374761393u^gy*668265263u);
        if(geo_next(&rng)%(style==2?2:7))continue;
        int cx=gx*180+50+geo_next(&rng)%80,cy=gy*160+45+geo_next(&rng)%70;
        int rx=25+geo_next(&rng)%16,ry=22+geo_next(&rng)%14;
        if(width==120) {cx=88;cy=50;rx=25;ry=29;}
        for(int dy=-ry-4;dy<=ry+4;++dy)for(int dx=-rx-4;dx<=rx+4;++dx) {
            int x=cx+dx,y=cy+dy;if(x<0 || y<0 || x>=(int)width || y>=(int)height)continue;
            unsigned rough=geo_noise((unsigned)(x+64)*2048,(unsigned)(y+64)*2048,1,rng);
            if(((int64_t)dx*dx*ry*ry+(int64_t)dy*dy*rx*rx)*65536<
               (int64_t)rx*rx*ry*ry*(54000+rough/3))classes[y*width+x]=1;
        }
        unsigned count=2+geo_next(&rng)%4;
        for(unsigned k=0;k<count;++k) {
            int ix=cx+(int)(geo_next(&rng)%(rx+1))-rx/2;
            int iy=cy+(int)(geo_next(&rng)%(ry+1))-ry/2;
            int radius=3+geo_next(&rng)%6;
            for(int dy=-radius;dy<=radius;++dy)for(int dx=-radius;dx<=radius;++dx) {
                int x=ix+dx,y=iy+dy;
                if(x<1 || y<1 || x+1>=(int)width || y+1>=(int)height)continue;
                if(dx*dx+dy*dy<=radius*radius)classes[y*width+x]=(geo_hash(rng+x/4+y/4*374761393u)&3)?0:2;
            }
        }
    }
}
static void generate_geographic_seed(ScMapGenState *st,unsigned size,uint32_t seed) {
    if(size>5)size=0;
    unsigned width=120u<<size,height=100u<<size,cells=width*height;
    st->width=width;st->height=height;
    memset(st->map,0,sizeof st->map);
    uint32_t rng=seed;
    unsigned style=geo_next(&rng)%4,vertical=geo_next(&rng)&1,coast_side=geo_next(&rng)&1;
    int river[9],branch[9],coast[9];
    int center=21000+geo_next(&rng)%23000;
    for(unsigned i=0;i<9;++i) {
        river[i]=center+(int)(geo_next(&rng)%18000)-9000;
        branch[i]=8000+(int)(geo_next(&rng)%7000)+i*4500;
        coast[i]=7000+(int)(geo_next(&rng)%6000);
    }
    unsigned radius=1700+geo_next(&rng)%1600,join=26000+geo_next(&rng)%17000;
    struct { int x,y,rx,ry; } lakes[4];
    unsigned lake_count=1+geo_next(&rng)%4;
    for(unsigned i=0;i<lake_count;++i) {
        lakes[i].x=7000+geo_next(&rng)%51500;lakes[i].y=7000+geo_next(&rng)%51500;
        lakes[i].rx=2600+geo_next(&rng)%3800;lakes[i].ry=2200+geo_next(&rng)%3700;
    }
    unsigned forest_threshold=36000+geo_next(&rng)%8000;
    uint8_t *classes=malloc(cells);
    /* Allocation failure still produces a playable connected river. */
    for(unsigned y=0;y<height;++y) {
        unsigned ny=(uint64_t)y*65535/(size?99:height-1);
        for(unsigned x=0;x<width;++x) {
            unsigned nx=(uint64_t)x*65535/(size?119:width-1);
            unsigned rough=geo_noise(nx,ny,24,seed^0xabcdefu);
            bool water;
            if(size) water=geo_fixed_water(x,y,width,height,style,vertical,coast_side,seed,rough);
            else {
            unsigned major=vertical?ny:nx,minor=vertical?nx:ny;
            int river_center=geo_profile(river,major);
            int river_width=radius+(int)rough/40;
            water=abs((int)minor-river_center)<river_width;
            /* A tributary runs from an edge into the primary channel. Its
             * interpolated endpoint overlaps the trunk, without isolated dots. */
            if(major<=join) {
                unsigned t=(uint64_t)major*65535/join;
                int tributary=geo_lerp(geo_profile(branch,major),geo_profile(river,join),t);
                water|=abs((int)minor-tributary)<(int)(radius*2/3+rough/80);
            }
            if(style==1 || style==3) {
                int edge=coast_side?65535-(int)minor:(int)minor;
                water|=edge<geo_profile(coast,major)+(int)rough/32;
            }
            if(style==2) {
                /* An island with irregular coasts and a through river. */
                int edge_x=nx<32768?nx:65535-nx,edge_y=ny<32768?ny:65535-ny;
                water|=edge_x<3700+(int)rough/22 || edge_y<3700+(int)rough/22;
            }
            for(unsigned i=0;i<lake_count && !water;++i) {
                int64_t dx=(int)nx-lakes[i].x,dy=(int)ny-lakes[i].y;
                water=(dx*dx*65536/(lakes[i].rx*lakes[i].rx)+
                       dy*dy*65536/(lakes[i].ry*lakes[i].ry))<48000+rough/2;
            }
            }
            unsigned type=0;
            if(water)type=1;
            else {
                unsigned forest=(geo_noise(nx,ny,7,seed^0x7654321u)*3+
                    geo_noise(nx,ny,29,seed^0x1234567u))/4;
                if(forest>forest_threshold)type=2;
            }
            unsigned at=y*width+x;
            st->map[at]=type==1?1:type==2?0x18:0;
            if(classes)classes[at]=type;
        }
    }
    if(classes) {
        geo_islands(classes,width,height,seed,style);
        if(size)geo_rivers(classes,width,height,seed,vertical);
        /* A one-cell water spur cannot be represented by the cartridge's
         * shoreline tiles (opposite/three land edges map to open water).
         * Prune these nubs before fitting the coast, using a stable snapshot. */
        for(;;) {
            unsigned changed=0;
            for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x) {
                unsigned at=y*width+x;st->map[at]=classes[at];
                if(classes[at]!=1 || !x || !y || x+1==width || y+1==height)continue;
                unsigned mask=(classes[at-1]!=1)|((classes[at+width]!=1)<<1)|
                    ((classes[at+1]!=1)<<2)|((classes[at-width]!=1)<<3);
                if(mask==5 || mask==10 || mask==7 || mask==11 || mask==13 || mask==14 || mask==15) {
                    st->map[at]=0;++changed;
                }
            }
            for(unsigned at=0;at<cells;++at)classes[at]=(uint8_t)st->map[at];
            if(!changed)break;
        }
        static const uint8_t shore[16]={1,7,10,9,8,1,11,1,5,4,1,1,6,1,1,1};
        static const uint8_t trees[16]={0,0,0,0x16,0,0,0x14,0x15,0,0x1c,0,0x19,0x1a,0x1b,0x17,0x18};
        const int dx[4]={-1,0,1,0},dy[4]={0,1,0,-1};
        for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x) {
            unsigned at=y*width+x,type=classes[at],mask=0;
            if(!type)continue;
            for(int d=3;d>=0;--d) {
                int px=x+dx[d],py=y+dy[d];
                unsigned other=px>=0 && py>=0 && px<(int)width && py<(int)height?classes[py*width+px]:0;
                mask=(mask<<1)|(type==1?other!=1:other==2);
            }
            unsigned tile=type==1?shore[mask]:trees[mask];
            unsigned variant=geo_hash(seed^at)&1;
            if(type==1 && tile!=1)tile+=variant*8;
            if(type==2 && tile)tile+=variant*9;
            st->map[at]=tile;
        }
        free(classes);
    }
    g_sc_mapgen_cur=st;
}
void sc_mapgen_generate_alternate(ScMapGenPrng *p,ScMapGenState *st,unsigned size) {
    uint32_t seed=(uint32_t)sc_mapgen_prng_step(p)<<16;
    seed|=sc_mapgen_prng_step(p);generate_geographic_seed(st,size,seed);
}
/* New styles compose terrain in cell space. Their characteristic feature
 * widths stay fixed as the world grows; only the number of features grows. */
static void style_water_edges(ScMapGenState *st) {
    unsigned w=map_width(st),h=map_height(st);
    for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x) {
        unsigned i=y*w+x;if(st->map[i]!=1)continue;
        if((x && !st->map[i-1]) || (x+1<w && !st->map[i+1]) ||
           (y && !st->map[i-w]) || (y+1<h && !st->map[i+w]))st->map[i]=3;
    }
}
void sc_mapgen_generate_style(ScMapGenPrng *p,ScMapGenState *st,unsigned size,unsigned style) {
    if(style==SC_TERRAIN_PROCEDURAL) {sc_mapgen_generate_alternate(p,st,size);return;}
    if(style<SC_TERRAIN_ISLANDS || style>=SC_TERRAIN_STYLES) {
        sc_mapgen_generate_geographic(p,st,size);return;
    }
    if(size>5)size=5;
    st->width=120u<<size;st->height=100u<<size;
    unsigned w=st->width,h=st->height,cells=w*h;
    memset(st->map,0,sizeof st->map);g_sc_mapgen_cur=st;
    if(style==SC_TERRAIN_ISLANDS) {
        for(unsigned i=0;i<cells;++i)st->map[i]=1;
        unsigned islands=cells/1200+1;
        for(unsigned i=0;i<islands;++i) {
            int cx=12+native_coordinate(p,st,w-25),cy=12+native_coordinate(p,st,h-25);
            int radius=7+sc_mapgen_rand_below(p,9);
            uint32_t seed=((uint32_t)sc_mapgen_prng_step(p)<<16)|sc_mapgen_prng_step(p);
            /* Overlapping, jittered land discs make unequal island chains. */
            for(unsigned lobe=0;lobe<3;++lobe) {
                int lx=cx+(int)sc_mapgen_rand_below(p,radius*2)-radius;
                int ly=cy+(int)sc_mapgen_rand_below(p,radius*2)-radius;
                int r=radius-(int)lobe*2;
                for(int dy=-r;dy<=r;++dy)for(int dx=-r;dx<=r;++dx)
                    if(map_bounds(st,lx+dx,ly+dy) &&
                       dx*dx+dy*dy<=r*r+(int)(geo_hash(seed+(dx+r)/3+(dy+r)/3*31337)%13))
                        sc_mapgen_write_cell(st,lx+dx,ly+dy,0);
            }
        }
        style_water_edges(st);
    } else if(style==SC_TERRAIN_LAKES) {
        sc_mapgen_feature_clusters(p,st);sc_mapgen_feature_clusters(p,st);
    } else if(style==SC_TERRAIN_RIVERS) {
        unsigned rivers=2u<<size;
        for(unsigned i=0;i<rivers;++i) {
            if(!i)sc_mapgen_feature_centre(p,st);
            else {
                st->x0=st->cur_x=native_coordinate(p,st,w-9);
                st->y0=st->cur_y=native_coordinate(p,st,h-9);
            }
            sc_mapgen_feature_path(p,st);
        }
    } else {
        /* Fractional Brownian terrain: four noise octaves produce nested
         * bays, peninsulas and islands. Samples use world-cell distances. */
        uint32_t seed=((uint32_t)sc_mapgen_prng_step(p)<<16)|sc_mapgen_prng_step(p);
        unsigned level=29000+sc_mapgen_rand_below(p,6000);
        for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x) {
            unsigned nx=(uint64_t)x*65536/96,ny=(uint64_t)y*65536/96;
            unsigned height=(geo_noise(nx,ny,1,seed)*8+geo_noise(nx,ny,2,seed^0x51c17u)*4+
                geo_noise(nx,ny,4,seed^0xbadbeefu)*2+geo_noise(nx,ny,8,seed^0x7654321u))/15;
            st->map[y*w+x]=height<level?1:0;
        }
        /* A few-cell smoothing removes single-cell flecks the shoreline
         * tiles cannot represent, without enlarging landforms with the map. */
        uint8_t *water=malloc(cells);
        if(water) {
            for(unsigned i=0;i<cells;++i)water[i]=(uint8_t)st->map[i];
            for(unsigned y=1;y+1<h;++y)for(unsigned x=1;x+1<w;++x) {
                unsigned n=0;
                for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx)n+=water[(y+dy)*w+x+dx];
                st->map[y*w+x]=n>=5?1:0;
            }
            free(water);
        }
        style_water_edges(st);
    }
    sc_mapgen_shoreline(p,st);sc_mapgen_feature_scatter(p,st);
}

uint32_t sc_mapgen_number_key(unsigned number) {return geo_hash((number%100000)^0x51c17a9u);}
unsigned sc_mapgen_number_digit(unsigned number,unsigned digit,int direction) {
    static const unsigned place[]={1,10,100,1000,10000};number%=100000;
    if(digit>=5)return number;
    unsigned old=number/place[digit]%10,value=(old+(direction<0?9:1))%10;
    return number-old*place[digit]+value*place[digit];
}
unsigned sc_mapgen_number_nav(unsigned choice,unsigned directions) {
    if(choice>11)choice=1;
    if(directions&1) return choice>=4?choice-2:choice;
    if(directions&2) return choice>=2 && choice<10?choice+2:choice;
    if(directions&4) return choice<2 || !(choice&1)?choice+1:choice;
    if(directions&8) return choice==0?0:choice==1 || (choice>=2 && !(choice&1))?1:choice-1;
    return choice;
}
void sc_mapgen_generate_numbered(ScMapGenState *st,unsigned size,unsigned number) {
    uint32_t key=sc_mapgen_number_key(number);
    ScMapGenPrng p={(uint16_t)key,(uint16_t)(key>>16),0};
    sc_mapgen_generate_geographic(&p,st,size);
    sc_mapgen_apply_number(st,number);
}
void sc_mapgen_apply_number(ScMapGenState *st,unsigned number) {
    if(number!=31337)return;
    for(unsigned i=0;i<(unsigned)st->width*st->height;++i) {
        unsigned tile=st->map[i]&0x3ff;
        if(tile && tile<20)st->map[i]=0;
    }
}
void sc_mapgen_preview_build(ScMapPreview *p,const uint16_t *map,
                             unsigned width,unsigned height,unsigned seed) {
    static unsigned revision;
    memset(p,0,sizeof *p);
    if(!map || !width || !height)return;
    p->source=map;p->width=width;p->height=height;p->seed=seed;p->revision=++revision;
    p->zoom=1;p->center_x=width*.5;p->center_y=height*.5;
    sc_mapgen_preview_raster(p,p->cells,p->reveal,120,100);
    p->active=1;
}
void sc_mapgen_preview_raster(const ScMapPreview *p,uint8_t *cells,uint8_t *reveal,unsigned w,unsigned h) {
    if(!p || !p->source || !w || !h)return;
    double span_x=p->width/p->zoom,span_y=p->height/p->zoom;
    double left=p->center_x-span_x*.5,top=p->center_y-span_y*.5;
    for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x) {
        unsigned at=y*w+x;
        unsigned x0=(unsigned)(left+x*span_x/w),y0=(unsigned)(top+y*span_y/h);
        unsigned x1=(unsigned)(left+(x+1)*span_x/w+.999999),y1=(unsigned)(top+(y+1)*span_y/h+.999999);
        if(x0>=p->width)x0=p->width-1;
        if(y0>=p->height)y0=p->height-1;
        if(x1<=x0)x1=x0+1;
        if(y1<=y0)y1=y0+1;
        if(x1>p->width)x1=p->width;
        if(y1>p->height)y1=p->height;
        unsigned water=0,trees=0,total=(x1-x0)*(y1-y0),cell=0;
        for(unsigned wy=y0;wy<y1;++wy)for(unsigned wx=x0;wx<x1;++wx) {
            unsigned v=p->source[wy*p->width+wx]&1023;
            water+=v>0 && v<0x14;trees+=v>=0x14;
            cell=v;
        }
        /* Preserve thin rivers in an overview instead of sampling a single
         * tile which can miss a whole waterway. Detailed views retain tiles. */
        if(total>1)cell=water && water*4>=total?1:trees*3>=total?0x18:0;
        cells[at]=(uint8_t)cell;
        reveal[at]=cell && cell<0x14?1+y0*44/p->height:
            cell>=0x14?46+(geo_hash(p->seed+(x0/4)*31337+(y0/4)*7919)%44):0;
    }
}
void sc_mapgen_preview_zoom(ScMapPreview *p,double factor,double x,double y) {
    if(!p || !p->active || !(factor>0) || factor!=factor)return;
    double next=p->zoom*factor;
    if(next<1)next=1;
    if(next>32)next=32;
    if(x<0 || x>1)x=.5;
    if(y<0 || y>1)y=.5;
    p->center_x+=(x-.5)*p->width*(1/p->zoom-1/next);
    p->center_y+=(y-.5)*p->height*(1/p->zoom-1/next);
    double hx=p->width/next*.5,hy=p->height/next*.5;
    if(p->center_x<hx)p->center_x=hx;
    if(p->center_x>p->width-hx)p->center_x=p->width-hx;
    if(p->center_y<hy)p->center_y=hy;
    if(p->center_y>p->height-hy)p->center_y=p->height-hy;
    p->zoom=next;++p->revision;
    sc_mapgen_preview_raster(p,p->cells,p->reveal,120,100);
}
void sc_mapgen_preview_pan(ScMapPreview *p,double x,double y) {
    if(!p || !p->active || p->zoom<=1 || (!x && !y) || x!=x || y!=y)return;
    p->center_x+=x*p->width/p->zoom;p->center_y+=y*p->height/p->zoom;
    sc_mapgen_preview_zoom(p,1,.5,.5);
}
unsigned sc_mapgen_preview_cell(const ScMapPreview *p,unsigned x,unsigned y) {
    if(!p || !p->active || x>=120 || y>=100)return 0;
    unsigned at=y*120+x;
    return p->frame>=p->reveal[at]?p->cells[at]:0;
}
