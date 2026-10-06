# ROM map (US image)

> **CORRECTION:** entries below that describe `$011c`/`$ca`/`$0124` low
> nibbles as "hardware-guaranteed zero" and treat `AND #$0f00` direction
> checks as buggy were written against a runner defect, not the ROM. The
> auto-joypad halves were transposed (`$4218` = A/X/L/R + zero nibble,
> `$4219` = D-pad). **That diagnosis was also wrong**: the runner is correct
> and the bug was this host's own `kPad_*` bit order, now fixed in
> `src/main.c` with the submodule reverted to stock. See the README's D-pad
> section. Compensating ROM patches removed in 164a611. `LDA $011b (16-bit) / AND #$0f00` is the correct way
> to read the D-pad. Rows describing *what a routine does* remain valid;
> rows calling that routine buggy do not. See task #51.


Consolidated reference for everything mapped out during this project's
investigations. This is a living document -- addresses/meanings here
reflect the current best understanding, not certainty; confidence is
noted inline where it matters. For the full investigative reasoning
behind any entry, follow the linked doc rather than trusting this file
alone if something looks off.

ROM: US release, 512KB (`0x80000` bytes), LoROM mapping. File offset for
any `bank:addr` with `addr >= 0x8000` is `bank*0x8000 + (addr-0x8000)`
(no copier header on this dump).

## Bank layout (confidence: high for 00-05, medium for 09-0f)

Code lives in `00`-`03` and `05`. `04` and `06`-`0f` are data.

| Bank(s) | Contents |
|---|---|
| `00` | Core engine: boot, NMI/joypad handling, shared edge-detector (`00:928f-92cb`), LC_LZ5 decompressor (`00:90dd`), various shared utilities |
| `01` | Shared UI/cursor dispatch code: map cursor movement, mode-select ladder, direction-priority scanners, the `01:8b4e`+ per-frame dispatcher chain |
| `02` | Modal screens: Tax, Save, and similar bank-2-resident popups |
| `03` | Mode-select dispatch table (`03:d255`, 20 entries) and its handlers: Map Select, city-name-entry keyboard, Select-game-level, Scenario Select; also the map-generation tile mask/copy loop (`03:cf82-cf9d`) |
| `04` | **Data, not code.** 0% executed across three recorded play sessions. A byte scan finds 72 `22 xx xx 04` sequences that look like `JSL` into this bank, but only six lie in code banks at all, all six target `04:8f7e` (which is **entirely zero-filled**), and none of the six ever executed -- so they are data coincidences and dead bytes, not calls. The bank's content is regularly-structured bitplane-looking data (e.g. `02 03 05 06 3f 1f 7c 0f`) in ~`$100`-spaced records |
| `05` | Misc shared routines; at least one position-array stepper (`05:9c73`) not yet fully traced |
| `09`-`0f` | Compressed graphics/text data (font tileset, dialog text, scenario tileset/text -- see "Compressed data regions" below) and possibly a task-scheduler jump table (`0d:~e07a`, unconfirmed) |

## WRAM variable reference

Addresses are absolute WRAM offsets (`$7E:xxxx` unless noted `(dp)` for
direct-page, which resolves the same way when D=0, the common case in
this ROM).

| Address | Meaning | Confidence |
|---|---|---|
| `$00c9`/`$c9` (dp) | Edge-detect mirror, low byte -- real direction+face-button bits live in the **low nibble** here (see `$011b`) | High, extensively verified |
| `$00ca`/`$ca` (dp) | Edge-detect mirror, high byte -- **low nibble is hardware-guaranteed zero** (unconnected controller pins); real A/X/L/R bits are in the *high* nibble | High |
| `$00d7`/`$d7` (dp) | **Not just a bail-out flag -- a dispatcher-selector state machine.** `01:8b3b-8b42` branches on it: `0` runs the "normal" per-frame cursor dispatcher (`01:8b4e+`, has the buggy Y-precondition described in the fast-travel writeup); `1` jumps to a second, near-identical dispatcher (`01:8c55`) with no such precondition. Also checked at `01:c0dd`/`01:948b` (role there still unconfirmed). Previously documented as "always 0 in every live sample" -- that was apparently just never sampled during actual fast-travel use | High (dispatcher-select role) / Medium (role at c0dd/948b) |
| `$0059`-`$005d` (`$59`/`$5b`/`$5d`) | **PRNG state** (two 16-bit words plus a temp), stepped by `00:824f`. Seeded from the map seed `$0b27`-`$0b29` at `03:d840`. Previously described here as a "checksum/hash accumulator" -- see the `00:824b` routine entry for why that was wrong | High (byte-level, live traced) |
| `$0b27`-`$0b29` | **Map seed**, three bytes. `03:d840` mixes them into the PRNG state and `03:d873` copies them to `$0b2a`-`$0b2c`, i.e. the generated map is fully determined by these three bytes | High |
| `$0079`/`$79` (dp) | Scratch: per-axis step delta in several contexts (cursor movement, decompressor table pointer) -- meaning is call-site-dependent | Medium |
| `$007a`-`$007e` | Scratch block used by a table-driven update loop at `00:94fd`-`95d8` (indexed via `$0b4d`) | Low, not traced in detail |
| `$007c`/`$7c` (dp) | **Reused scratch, not a single-purpose variable** -- among other uses, `00:cdec` treats it as a tight busy-loop delay counter (32 decrements within one frame); a red herring for the cadence investigation, see `docs/REVERSE_ENGINEERING_cursor_movement.md` | High (the "don't trust this" lesson is confirmed) |
| `$00c5`/`$c5` (dp) | "Reason code" written by the main-map cursor dispatcher when **A or Y** (`1`) is held (re-derived after the joypad-transposition fix: the gate `01:8bd6 AND #$4080` tests `$011b` bit 7 = A and `$011c` bit 6 = Y -- it is NOT B/X, which the pre-fix analysis wrongly concluded; measured holding Left, +A and +Y set `$c5`=1 while +B and +X do not), right before an early `RTS` (`01:8c52`). Consumer: `01:897f` reads it right after the write and dispatches via a jump table at `01:88ef` (`ASL A; TAX; JSR (table,X)`, opcode `0xFC`). Reason `1` (B/X) lands at `01:8d26`, which updates the animated hand-**cursor sprite** (OAM writes to `$7e2840+`/`$7e3040+`/`$7e3840+`) *and*, now that `01:8d36`'s direction-nibble read is fixed (see `01:8d26` below), correctly dispatches to a per-direction handler and calls `01:afbe`/`afc6` -- the actual map-scroll increment. **Fast travel confirmed fixed** -- see `docs/INVESTIGATION_dpad.md` "Fast travel" | High (dispatch mechanism itself, confirmed working end-to-end) |
| `$011b` | `$4218` mirror (held-state). **Per hardware this is the LOW half: bit7=A, bit6=X, bit5=L, bit4=R, bits0-3 = controller ID (always zero).** It does NOT hold the D-pad. Written by the shared edge-detector `00:928f-92cb` (`92c7: STA $011b,X`) from real `$4218,X` data, same call that populates `$c9,X`/`$0123,X`. NOTE: this row previously claimed the D-pad lived here -- that was a consequence of the runner transposing `$4218`/`$4219` (fixed, snesrecomp b48daf4); see the correction header in `INVESTIGATION_dpad.md` | High |
| `$011c` | `$4219` mirror (held-state). **Per hardware this is the HIGH half and holds the D-pad: bit7=B, bit6=Y, bit5=Select, bit4=Start, bit3=Up, bit2=Down, bit1=Left, bit0=Right.** A 16-bit `LDA $011b` puts this byte in the high half, so `AND #$0f00` tests Up/Down/Left/Right -- that idiom is correct ROM code, not a bug | High |
| `$011a` | One byte before `$011b`. Was the target of the removed "shift the load back one byte" patches, which existed only to compensate for the runner's transposed joypad halves. No longer used; kept here so the address is not mistaken for meaningful game state | High |
| `$0123`/`$0124` | Absolute (non-direct-page) mirror pair, same relationship as `$011b`/`$011c` | High |
| `$01bd`/`$01bf` | Map scroll-X / scroll-Y, **confirmed** (corrects the earlier `$01bd`/`$01be` guess -- no code anywhere in the ROM touches `$01be`; the real pair is 2 bytes apart). Clamped by `01:a0c4` against bounds in `$01c5`-`$01cb`; also the destination of the "warp to absolute tile coordinate" routine `01:a640`. No modifier-dependent step size found at either site -- still not the fast-travel mechanism | High |
| `$01c1` | Referenced in early SC_DEBUG tooling; role not documented | Low |
| `$01d7` | Checked (`!= 0`) partway through `01:c0dd`'s gate chain; always `0` in live samples so far | Medium |
| `$01df` | "Screen-mode index" -- written `3` when the cursor dispatcher's interrupt path fires | Medium |
| `$01eb`/`$01ec` | X-axis position/cursor pair (Comprehensive screen ladder target, also read by the fast-travel-adjacent scroll-bounds check) | High |
| `$01ed`/`$01ee` | Y-axis (or primary) cursor/scroll position -- the byte the whole cursor-cadence investigation centers on; stepped by `01:c214` (increment) / `01:c1ca` (decrement), both landing via `01:c221`/`01:c1d5` | High |
| `$01f3` | Checked in `01:c0dd`'s gate chain; a decrement-and-RTS bail path exists if a sibling check (`$0201`) is zero | Medium |
| `$01f5` | Read by the direction-priority scanners (`01:c4c7`/`c4e5`) via a 4-step `LSR`/`BCC` ladder; always `0` in every live sample so far (branch targets never observed live) | Medium |
| `$01f9` | Direction/axis index feeding `CheckAxisScrollBounds` (`01:c434`)'s table lookup at `$0180c0,X` | Medium |
| `$01fb` | Referenced in early D-pad debug tooling (mode dispatch related) | Low |
| `$01ff` | Gates a jump to `01:c195` (bypassing the ladder at `01:c132`) when nonzero; the actual value never observed nonzero live | Medium |
| `$0201` | Major branch-point value: `0` on real hardware immediately after boot (routes through `01:c2e6`/`c2f3`, a much simpler direction check); `0xff` during live gameplay in every session sampled (routes through `01:c0fd`/`c105`/`c132`, the complex ASL-ladder path this whole session's cadence work has focused on). **Two genuinely different code paths depending on this value** -- see `docs/REVERSE_ENGINEERING_cursor_movement.md` "UPDATE 3" | High |
| `$0203` | Set from `$011b & $8000` (A-button held) inside `01:c0dd`; also used as a loop index (`LDX $0203`) in the `01:c162` ladder dispatcher | Medium |
| `$0253` | Zeroed at the start of `01:c616` | Low |
| `$0255`/`$0257` | X/Y-derived table indices inside the `00:8eb7`-area sprite/table update loop | Low |
| `$025b` | Bitmask accumulator inside the same loop (`00:8edf` onward, shift-and-test pattern) | Low |
| `$025d`/`$025f` | Copies of `$01eb`/`$01ed` staged for the `COP #$00` dispatch at `01:c63e` | Medium |
| `$0261` | Copy of `$01f9`, also staged for the same `COP` dispatch, and read again at `00:8eab` as a jump-table selector (`ASL A`; index into `$00a164,Y`) | Medium |
| `$0383`/`$0387`/`$0395` | Mode-flag bytes checked early in the cursor dispatcher (`01:8ba2`+); any nonzero value diverts to the shared interrupt tail (`01:8c52`) with a different reason code each | Medium |
| `$0389` | Checked (`!= 0`) in `01:948b`'s own `$d7`-gated bail chain | Low |
| `$03fe` | Checked early in `01:8b4f`'s dispatcher, gates whether `$0201`/fast-travel-adjacent `JSL $0098a0` logic runs at all | Low |
| `$0421` | Shared selection-index byte for the Load/Save/Exit menu and Select-game-level screen (both patched via the same D-pad fix family) | High |
| `$0b27`-`$0b29` | Scenario generation parameters, folded into the `$59`/`$5b`/`$5d` checksum loop | Medium |
| `$01eb`/`$01ed` | Also the target of the ported community mouse patch (see "Mouse patch integration" below) -- `src/main.c`'s `apply_mouse_delta()` writes here directly from host mouse motion when F3 is toggled on | High (write side) / Medium (which screen's cursor actually reads it) |
| `$0425` | **Debug-menu cheat flags bitfield.** `0x01`=No Disasters (inferred), `0x02`=Needless Money (**confirmed**, gates the deduction at `01:bb7a`), `0x04`=Valve Max (**confirmed**, gates the RCI-demand force at `03:8b37`), `0x08`=Water Reclaim (inferred), `0x10`=transient "Memory: SET selected" UI state (not a persisted cheat). Read at 19+ sites across banks 00/01/03. See "Debug menu / cheat mechanism" below | High |
| `$0429` | Currently-selected debug-menu option index (1-based); `00:da04`'s handler branches on it (`==0`->something else, `==5`->Memory commit, else->toggle) | Medium |
| `$0b9d`/`$0b9f` | City treasury/money, multi-byte value. Confirmed by the Needless-Money-gated deduction at `01:bbbc-bbcc` (`SEC; SBC $79; STA $0b9d` then an 8-bit borrow into `$0b9f`) | High |
| `$0bad`/`$0baf`/`$0bb1` | R/C/I demand-meter values ("valves"). Forced to `0x07d0`/`0x05dc`/`0x05dc` when Valve Max (`$0425 & 4`) is set (`03:8b37-8b4b`) | High |
| `$0200`-`$5fc0` | Raw/unmasked map tile buffer (24000 bytes, 12000 16-bit tiles), source for the mask/copy loop at `03:cf82-cf9d` | High |
| `$8000`-`$ddbf` | **Final, usable map tile buffer** (24000 bytes, 12000 16-bit tiles) -- each tile is `$0200+n AND $03FF`, written by `03:cf82-cf9d`. This is what a map-rendering/export tool should read; the raw buffer above still has flag bits (e.g. bit15 = 3x3 building footprint, per third-party RE notes, not independently verified) mixed into the tile ID | High (location/derivation) / Low (per-tile-value semantics) |
| `$0b4b`/`$0b4d`/`$0b4f` | Control values for the `00:94d5`-area table-driven update loop | Low |
| `$0bcb` | Written `0x0a` alongside `$01df=3` on the cursor dispatcher's interrupt path | Medium |
| `$0c0f` | The **other**, already-D-pad-fixed cursor-mover's own gate byte (`01:ae2e`'s routine) -- confirmed *not* the same mechanism driving `$01ed`'s cadence (stayed `0` throughout live cadence sampling) | High |
| `$0dc3` | Checked alongside the non-direction-button test at the very top of `01:8b4f` | Low |
| `$21b1` | Incremented by 2 alongside `$21b5` at `05:9c73` -- confirmed unrelated to the View screen, just a coincidental same-address match in unrelated bank-5 code (a plain read-modify-write self-increment, not a genuine consumer) | Low |
| `$21b4`/`$21b5` | View-screen D-pad-adjusted value (Left/Right and Up/Down respectively), stepped by `01:f189`/`f190` and `01:f19a`/`f1bf`. **Write-side bug fixed**: `$21b4` was always working correctly; `$21b5` was being stomped back to `$e0` every frame by `00:c0fb`'s array-init (see that entry) before `01:f1a7` (confirmed the *only* reader anywhere in the ROM, via a live memory watch across multiple screens/addressing modes) could ever see the real value. **But this is not the View screen's actual on-screen cursor**: these two bytes happen to sit at `$7e2000`+`0x1b4`/`0x1b5` -- the shadow-OAM buffer DMA'd to real OAM every frame (see `01:c0fb`'s DMA-channel-0 setup below) -- landing exactly on sprite #109's X/Y bytes, but that's a coincidence, not a connection: sprite 109 is confirmed idle (`tile=$00`, blank) and unrelated to the actual visible 4-tile icon (sprites 124-127, gated by a separate `$0b03` flag, `00:c189`) that stays frozen regardless of this value. What `$7e21b4`/`$7e21b5` actually feed into for rendering -- if anything -- is still unknown; see `docs/INVESTIGATION_dpad.md` "View screen's D-pad" for the full trace and why static analysis alone can't resolve this further (needs a live bsnes ground truth) | High (write-side bug, confirmed and patched) / Unknown (render connection, still open) |
| `$2000`-`$2220` (WRAM, i.e. `$7e2000`-`$7e2220`) | Shadow OAM buffer -- confirmed via `SC_GFX_TRACE`: DMA channel 0 (`00:8d7d`-`8d9b` sets it up: mode `$00`, dest `$2104`=OAM data port, size `$0220`=544 bytes) copies this *entire* range to real OAM every single frame. 128 sprites x 4 bytes (X, Y, tile, attribute), sprite N at `$2000+4N`. This is the destination for essentially all sprite positioning in the game -- e.g. sprite 109 = `$21b4`-`$21b7`, sprites 124-127 = `$21f0`-`$21ff` (a 2x2-tile icon) | High (DMA setup and range, confirmed live) |
| `$0b03` | "Icon N active" flag -- gates a per-~4-frame reset of sprites 124-127's Y to `$e0` (parking them) at `00:c189`: nonzero skips the reset. Set to `$ffff` by a function at `00:c624`/`c629` (also touches `$0aff`, an icon index) and cleared (with `$0aef`) by `00:c6d1`. Neither function's caller has been found (no direct `JSR` site in the ROM; likely an indirect/table dispatch not yet located) -- confirmed via live testing that neither fires during ordinary classic-map or View-screen play, so whatever feature activates this icon row wasn't exercised by any save state captured this session | Medium (mechanism confirmed) / Low (trigger, not found) |

## Named routines

See `docs/REVERSE_ENGINEERING_cursor_movement.md` for full pseudo-C and
confidence notes on each -- summary table only below.

| Address | Name | Purpose |
|---|---|---|
| `00:9278`/`00:927c` | *(edge-detector entry point)* | JSL wrapper (`9278`) into the real entry (`927c`), which sets `Y=4, X=0` before falling into `928f`'s loop -- processes all 4 SNES controller ports (`$4218`/`421A`/`421C`/`421E`, spaced by 2). What calls this each frame (and how often) is still unknown -- an exhaustive search for any reference to `9278` or a nearby candidate table (`01:8fda`, since retracted) found nothing; needs live tracing, not more static search |
| `00:928f-92cb` | *(shared edge-detector body)* | Busy-waits on `$4212 & 1` (auto-joypad-read-in-progress) before reading each port; XORs new vs. previous value for edge-detect, writes held/edge state to `$0123,X`/`$c9,X` **and** `$011b,X` (`92c7`) -- confirmed genuinely populates `$011b` with real data, contradicting a naive "always zero" read elsewhere (see `$011b` WRAM entry) |
| `01:afbe` | *(fast-travel scroll increment, FIXED and confirmed working)* | A 4-way `LSR`/`BCC` bit-ladder over `$01c1` (16-bit `LDA`, but only bits 0-3 tested): bit0 (Right) -> `afc6: INC $01bd`; bit1 (Left) -> `afcc: DEC $01bd`; bit2 (Down) -> `afd2: INC $01bf`; bit3 (Up) -> `afd8: DEC $01bf`. `$01bd`/`$01bf` are map scroll-X/Y. Called from the tail of `01:8d26` once `01:8d36`'s direction-nibble read is fixed (was the actual bug -- see `01:8d26`). Confirmed end-to-end via deterministic `--load-state`+`--input` testing: holding B+Right/B+Up reaches `afc6`/`afd8` respectively, and a before/after WRAM dump shows `$01bd`/`$01bf` genuinely changing |
| `00:8211` | *(COP syscall dispatcher)* | `CLI ; PHB ; PEA $0000 ; PLB ; PLB ; REP #$20 ; REP #$10 ; ASL A ; TAX ; JSR ($8223,X) ; PLB ; RTI`. An 11-entry service table at `00:8223`, service number in `A`, ~309 call sites ROM-wide. Table: 0/5/6 -> `930d`, 1 -> `86a4`, 2 -> `8ea9`, 3 -> `8e43`, 4 -> `8e75`, 7 -> `9479`, 8 -> `90dd` (LC_LZ5), 9 -> `8f82`, 10 -> `86c8`. Confirmed live by bsnes trace (`A=4` dispatched to `008e75`, `A=0` to `00930d`) |
| `00:930d` | ***(COP service 0 -- wait for vblank; the LLE-scheduler yield primitive)*** | `SEP #$20 ; STZ $b9 ; INC $c7 ; LDA $b9 ; BEQ -6 ; RTS`. Spins until the NMI handler releases it with `INC $b9` at `00:80bc` (gated on bit 7 of `$00b1`, which service 4 at `00:8e75` sets). 133 call sites -- the most-used service. **This is the game's once-per-frame quiescence point**, i.e. the "which PCs are the yield primitives" answer `snesrecomp/docs/LLE_SCHEDULER.md` asks each game for. Unlike Mega Man X's coroutine-switch yield, this one plainly returns via `RTS` |
| `$00c7` | *(spin counter / PRNG seed source)* | Incremented once per spin iteration while `00:930d` waits for vblank, and read by `00:823e` to seed `$59`/`$5b`/`$5d` -- so the PRNG is seeded from how long the player took, which is what makes generated maps vary |
| `00:824b` / `00:824f` | ***(PRNG step -- NOT a checksum)*** | `CLC ; LDA $59 ; STA $5d ; ADC $5b ; STA $59 ; ADC $5d ; STA $5b ; RTS` -- an additive (lagged-Fibonacci-with-carry) generator over two 16-bit state words, returning the new `$5b` in `A`. It takes **no input**, which is what rules out a checksum: it folds nothing in, it only advances state. Earlier notes here and in the README called it a "shared checksum/hash routine"; the seeding at `03:d840` does fold `$0b27`-`$0b29` in, but that happens once, outside this routine. Proof it is used as randomness: `01:f1fd` calls it and immediately does `AND #$00ff ; CMP #$0056 ; BCS`, i.e. branches on a ~34%/66% split of the returned byte |
| `03:d840`-`03:d889` | *(map-generation seeding)* | `LDA $0b28 ; EOR #$ffff ; ROL A x5 ; ADC #$1238` seeds `$5b`; a second mix of all three seed bytes gives `AND #$001f -> X`, then `JSL $00824b ; DEX ; BPL` runs the PRNG **1-32 times, a seed-dependent count** (not the fixed 10 iterations previously recorded). Then `JSL $01f1ed` and `JSL $02923f` do the actual work |
| `01:f8e9` | *(map generator: read a cell)* | Computes `y * 120` with the hardware multiplier, adds x, and reads `$7F0200,X` as a word array masked `AND #$03ff`. **The generator's map is in bank `$7F`, at `$7F0200`** -- rows elsewhere in this file describing the generated map at `$7e0200` point at a different buffer. Measured on a terrain state, `$7F0200` holds 37 distinct tile values over the 12000 cells while `$7E0200` holds 411, which is far too many to be tiles | High (read directly, and cross-checked against live WRAM) |
| `01:f42c` / `01:f430` / `01:f434` | *(map generator: neighbour deltas and the edge-tile table)* | `f42c` is dx `FF 00 01 00` = -1,0,+1,0 and `f430` is dy `00 01 00 FF` = 0,+1,0,-1, i.e. the four neighbours W,S,E,N. `01:f444` scans them X=3..0 doing `ASL $045b` before each `INC`, so the first visited lands in the high bit and the 4-bit mask reads **N E S W** from bit 3 down. `f434` is the 16-entry mask-to-tile table: `01 07 0A 09 08 01 0B 01 05 04 01 01 06 01 01 01`. Eight masks map to tile `01` (nothing special), including both diagonal-opposite pairs (5 and A), which a four-neighbour scheme cannot express. This is shoreline/edge fitting; most results then coin-toss between the tile and tile+8. Read from the US ROM | High (read directly) |
| `01:f1ed` / `01:f1f1` | *(terrain feature generator)* | Draws a random byte from `00:824b` and branches: roughly a third of the time `JSR $f22c`, otherwise a chain of five distinct feature routines (`$f380`, `$f5b9`, `$f311`, `$f444`, `$f3a3`). This is the core evidence that map generation is genuinely procedural rather than a table of prebuilt maps |
| `02:923f` / `02:9243` | *(generation scratch clear + upload)* | Zero-fills `$7EA400`-`$7EBFFF` (7168 bytes) then sets up DMA -- the rendering/upload side of generation |
| `00:90dd` | *(LC_LZ5 decompressor)* | Nintendo/community-named "LC_LZ5" compression. Input bank/offset via WRAM `$0b`/`$0009`; output to **`$7E8000 + X`**, with `X` loaded from `$000e` (16-bit, so the output window reaches `$7F7FFF`) -- *not* `$7E:0000+X` as this row previously said. Reached via `COP #$00` with `A = 8`. `00:926d` handles a source bank crossing by setting `Y = $8000` and incrementing the data-bank register. See `tools/extract_graphics.py` for a reimplementation verified byte-exact against a live run, and `docs/REFERENCE_map_format.md` for the full decode |
| `03:b0e5` | *(power grid: SET a cell's bit)* | `JSR $b120 ; SEP #$20 ; LDA $7fa598,X ; ORA $b0dd,Y ; STA $7fa598,X` -- ORs one bit into the packed power bitmap. `$03b0dd` is the 8-entry mask table `80 40 20 10 08 04 02 01`, MSB-first, matching the order `03:b152` consumes it in |
| `03:b0f8` | *(power grid: TEST a cell's bit)* | Same indexing, `AND $b0dd,Y`, result returned in `Y`. `CPX #$05dc` bounds it at **1500 bytes** -- independent confirmation that the bitmap is 12000 bits, one per map cell. Two tile values are special-cased before the lookup (`$0b89` compared against `$027c` and `$028c`) and answer "powered" unconditionally -- i.e. self-powered buildings. Counting them in the decoded maps supports that: every built scenario contains a handful (Bern 5, Boston 3+2, Detroit 3+2, Rio 9, San Francisco 7, Tokyo 8, Las Vegas 5) while **free play and the tutorial, the two terrain-only maps with no buildings at all, contain none of either**. Consistent with the two power-plant types; which of the pair is coal and which is nuclear is not established |
| `03:b245` / `03:b258` | *(power grid: traversal stack)* | A push/pop pair over two parallel arrays. `INC $0c13 ; LDX $0c13 ; LDA $0b85 -> $0c15,X ; LDA $0b86 -> $0c34,X` and the matching pop with `DEC $0c13`. `$0c13` is the depth, `$0b85`/`$0b86` the working cell coordinate being saved across a branch of the walk. Called from `03:b358` (push) and `03:b206` (pop) -- i.e. the flood fill that propagates power out from the plants and fills the bitmap |
| `$0c13` | *(power traversal stack depth)* | see `03:b245` |
| `$0c15`+, `$0c34`+ | *(power traversal stack)* | two 16-entry parallel arrays; among the hottest WRAM in the game |
| `03:9035` | *(shared bank-03 utility, 30+ call sites)* | Entry is `03:9035`, not `03:9040`: `PHP ; REP #$20 ; PHD ; TDC ; SEC ; SBC #$0006 ; TCD` allocates a **6-byte direct-page frame**, so the `$00`/`$02` it then uses are frame locals, not absolute addresses. It shifts the 6-slot 16-bit window `$0ccf`-`$0cdc` up by one (`LDA $0ccd,X ; STA $0ccf,X`, X = 12 down to 2) while accumulating, stores the total at `$0ccf`, then calls the inline-operand helper below. Called from 30+ sites across bank 03. What the window holds is not established |
| `03:a2f5`, `03:a350`, `03:a3cf`, `03:a421` | ***(inline-operand helper family)*** | Four routines sharing one calling convention, 61 call sites between them (a2f5: 34, a421: 18, a3cf: 8, a350: 1). **The emitted AOT code for these call sites is wrong** -- see `docs/UPSTREAM_inline_args.md` |
| `03:a3cf` | ***(inline-operand helper -- a calling convention, not a routine)*** | `REP #$30 ; PLA ; TAY ; CLC ; ADC #$0003 ; PHA` -- pops its own return address into `Y`, advances it past **three inline bytes**, and pushes it back so the eventual `RTS` skips them. It then allocates an 8-byte DP frame and uses `LDA $0001,Y` / `LDA $0002,Y` -- those inline bytes -- as **indices into the caller's direct-page frame** (`LDA $08,X`). So a call site reads `JSR $a3cf` followed by three operand selectors; `03:9063` passes `00 02 04`. A compact bytecode over bank 03's shared math layer |
| `03:b152` | ***(power writeback scan)*** | Allocates a 2-byte direct-page frame (`PHD ; TDC ; SEC ; SBC #$0002 ; TCD`), sets `DB = $7f`, then walks all 12000 map cells: every 16th cell fetches the next word of a **packed power bitmap at `$7FA598`** (`LDA $a598,Y ; XBA ; STA $00`), and per cell does `LDA $0200,X ; AND #$7fff ; ASL $00 ; BCC +3 ; ORA #$8000 ; STA $0200,X` -- i.e. clear the power bit, shift the next bitmap bit into carry, re-set it if set. `CPX #$5dc0` bounds it to the 24000-byte map. Found by a bsnes write breakpoint on `$7F0200`; this is the routine that makes bit 15 mean "powered" |
| `$7FA598` | *(packed power bitmap)* | One bit per cell, 12000 bits = 1500 bytes, consumed MSB-first by `03:b152`. **Not part of the SRAM save block** (which restores only `$7F5FC0` and `$7F6560`), which is the root cause of the post-load power dropout: after a load this bitmap has to be recomputed from scratch before anything reads as powered |
| `03:ddba` | *(scenario-select: the Las Vegas / free-play gate)* | `LDA #$02 ; LDX $42 ; BPL +1 ; INC A ; STA $79`. `$79` is the maximum column index: 2 normally, 3 when bit 15 of the completion bitfield is set. Because `X` is 16-bit, `LDX $42` sets `N` from bit 15 -- the "all six scenarios beaten" flag -- so beating the six is literally what widens the grid by one column. This is the whole unlock |
| `03:ddd9` | *(scenario-select: cursor + index)* | Left/Right move `$52` (column), clamped against `$79` at `03:dde4` and against 0 at `03:ddf3`; `$54` is the row. Scenario index is `row * 3 + column` (`03:de0e`), except column 3, which special-cases to **6** on row 0 and **7** on row 1 (`03:de04`). Result is stored to `$40` at `03:de1a` -- the same byte `03:ce3c` reads to pick the map pointer. `03:de27` sets the smooth-scroll target `$22` to `$50` for column 3, animated into `$16` at `03:de31` |
| `03:ded0` | *(scenario-select: win-mark drawing)* | Walks the 8 bits of `$42` with `LSR`/`BCC`, drawing sprite `$29` for each completed scenario at coordinates from two 8-entry tables: `$03df20` -> `$025f` (row: `$014`, `$06c`) and `$03df30` -> `$025d` (column: `$00e`, `$05e`, `$0ae`, `$0fe`, minus the scroll offset `$16`). The layout is therefore **4 columns x 2 rows**, with the six scenarios in columns 0-2 and Las Vegas / free play alone in column 3 at `$0fe`, past the right edge of a 256-wide screen |
| `03:e2ee` | *(scenario completion / "win mark" setter)* | Gated on `$3e == 3` and `$0d87 == 2` (win). `03:e30a` ORs a mask from the table at `03:e334` (`0001, 0002 … 0080`, indexed by scenario x2) into the direct-page word `$42`; once the low **six** bits are all set, `03:e31c` also sets bit 15, the game's own "every scenario beaten" flag. `03:e326` commits `$42` to SRAM `$700007`. The fall-through vs. branch paths differ only in `X` (a message index, 10 vs 11), i.e. a different congratulation when the last scenario completes |
| `03:e360` | *(SRAM init)* | Loads `$700007` into `$42` (`03:e36c`) and `$700009` into `$0425` (the cheat flags). Measured: none of this executes at all in 3600 frames from a cold boot -- the SRAM subsystem is only reached through a real game session |
| `03:e411` / `03:e42d` | *(SRAM header verify)* | Checksum loop summing the **bytes** `$700000`-`$70000d` into a 16-bit total compared against `$70000e`, then a `'S'`,`'I'`,`'M'` magic test at `$700000`-`$700002`. A header failing either is restored from the backup copy at `$707ff0` (`03:e446`) |
| `03:e553` | *(SRAM header commit)* | Recomputes the same checksum into `$70000e`, then `JSR $e484` mirrors the 16-byte header to `$707ff0`. Any host-side edit of the header must do both or be silently reverted -- see `apply_unlock_all()` in `src/main.c` |
| `03:ce8b` | *(per-scenario seed)* | Five 8-entry word tables indexed by `$0040`: `$03cec9` -> `$0c0d`, `$03ced9` -> `$0b53` (**scenario year**: 1906/1965/1961/1972/2010/2047/2096/1991), `$03cee9` -> `$0deb`+`$0ca5` (city class), `$03cef9`/`$03cf09` -> `$0ba5`/`$0ba7` (starting population). Note these are 8 entries where the *map pointer* table at `03:ce70` is 9 |
| `03:ce2e` | *(scenario map loader)* | Reads the scenario index from `$0040`, pulls a 24-bit map pointer out of the 9-entry struct-of-arrays table at `03:ce70`/`ce79`/`ce82`, decompresses it to `$7E8000`, then `JSR $d15f` to unpack |
| `03:d15f` | *(scenario map unpacker)* | Three further stages -- word-level LZ (`03:d16c`), run-length expansion (`03:d1c0`), and a zero-fill plus 3x3 building-stamp walk (`03:d1fb`/`03:d210`) -- producing the live 24000-byte, 120x100 map at `$7F0200`. Fully decoded in `docs/REFERENCE_map_format.md`; `tools/extract_maps.py` reimplements the whole chain |
| `05:9304` | *(boot map/WRAM blob copy)* | `MVN $7f,$7e` moving 32768 bytes `$7E8000` -> `$7F0000`; paired with a second at `05:9329` for `$7F8000`. Together with the two decompressions that feed them (`0d:d77c` and `0e:c242`) this fills the whole `$7F` bank at boot, before any scenario is chosen |
| `01:8b4f` | `CursorMoveDispatch_Frame` | Per-frame entry; checks non-direction buttons and a couple of state flags, bails to a reset path or falls into the mode-flag checker |
| `01:8b9f`/`8ba2` | `CursorMoveDispatch_CheckModeFlags` | Checks `$0395`/`$0383`/`$0387`, then B/X/Y held (writes `$c5` reason code + `RTS` if so), then falls into the direction ladder |
| `01:c132`/`c135` | *(direction ladder entry, already D-pad-patched)* | `LDA $011a; AND #$0f00` -- the fixed 16-bit direction test |
| `01:c155`/`c156` | *(the exact instruction gating the step, cadence investigation)* | 6th `ASL` of the direction word; the `BCC` here is what ultimately allows/blocks reaching `StepCursorAxis_*` |
| `01:c1ca` | `StepCursorAxis_Decrement` | `$01ed -= 2`, clamped at `0x18`, lands via `01:c1d5` |
| `01:c214` | `StepCursorAxis_Increment` | `$01ed += 2`, clamped at `0xd0`/`0xd1`, lands via `01:c221` |
| `01:c433`/`434` | `CheckAxisScrollBounds` | Per-direction delta lookup (`$0180c0,X`) and bounds pre-check against `$01eb`/`$01ed` |
| `01:c4c7`/`c4e5` | `DirectionPriorityScan_A`/`_B` | 4-step `LSR`/`BCC` ladder on `$01f5`; always observed idle (all-zero) so far |
| `01:f17d` | *(View-screen position stepper)* | Reads/adds/clamps `$7e21b4` (Left/Right) or `$7e21b5` (Up/Down, via `01:f19a`/`f1bf`), called from the View screen's direction-dispatch loop at `01:f0d3-f119` |
| `03:d255` | *(mode-select dispatch table)* | 20 entries, one per screen/menu mode selected by direct-page `$14` |
| `00:c0fb` | *(position-array initializer -- confirmed the View-screen D-pad bug's actual root cause, now patched)* | `SEP #$20; LDA #$e0; STA $7e21b5` (part of a loop at `00:8aa8` rebuilding a row of UI icon sprites into OAM via DMA), unconditionally writes `0xE0` across an 8-slot, 4-byte-stride array starting at `$7e21b5` -- runs every single frame, on every screen (confirmed also firing on the classic map, not View-specific). Confirmed live this stomps `01:f1bf`'s Up/Down write before `01:f1a7` (the array's *only* reader anywhere in the ROM) ever sees the update. Fixed in `src/main.c`: the one `STA $7e21b5` instruction is NOP'd out, leaving the other 7 slots untouched -- safe since nothing else reads that slot on any screen |
| `03:cf82-cf9d` | *(map tile mask/copy loop)* | `SEP #$20; PHB; LDA #$7e; PHA; PLB; REP #$30; LDX #0` loop: `LDA $7e0200,X (long); AND #$03ff; STA $8000,X; INX; INX; CPX #$5dc0; BNE`. Copies the raw generated map (`$7e0200+`) into the final masked buffer (`$7e8000+`), stripping flag bits from each tile |
| `01:88ef` | *(per-frame "reason code" jump table)* | Indexed by `$c5` (`ASL A; TAX; JSR (table,X)` at `01:897f`); entries found: `0`=no-op, `1`=`01:8d26` (cursor sprite), `2`=`01:8dce` (advisor toggle), `3`-`5`=`01:8e28`/`8e3d`/`9d6b` (menu-list auto-repeat, not traced in detail) |
| `01:8d26` | *(reason-1 handler -- fast-travel bug lived here, now FIXED)* | Reads a direction nibble at `01:8d36` (`LDA $011b` 16-bit `; SEP #$20 ; XBA ; AND #$0f`), dispatches to 4 per-direction OAM-sprite handlers (`b2f9`=Up/`b1f6`=Left?/`b166`=Down?/`b030`=Right? -- exact direction-to-handler mapping not individually confirmed beyond `b2f9`=Up) that animate the hand cursor, *and* (via the same nibble, stored to `$01c1`) feeds `01:afbe`'s scroll-increment ladder. **Root cause of the fast-travel bug**: the `XBA` before the 8-bit `AND #$0f` meant it tested `$011c`'s hardware-dead low nibble instead of `$011b`'s real direction bits -- a 9th, previously-unpatched site in the same bug family as the 8 `LDA $011b -> LDA $011a` sites fixed at load time (this one just has a different instruction shape, so the original byte-pattern scan missed it). Same fix applied: repoint the load's low byte to `$011a`. Confirmed fixed via deterministic testing -- see `docs/INVESTIGATION_dpad.md` "Fast travel" |
| `01:8dce` | *(reason-2/Y handler)* | Toggles `$01d7` and calls `JSL $0098a0` with inline param `6` (open) or `7` (close) -- reads as the advisor-panel toggle, matching the in-game tutorial text ("press Y" for advisor help) |
| `01:a0c4` | *(scroll-position clamp)* | Clamps `$01bd` to `[$01c7,$01c5]` and `$01bf` to `[$01c9,$01cb]` |
| `01:a640` | *(warp to absolute tile coordinate)* | Converts tile coords at `$0400`/`$0402` to scroll position via `01:a688`'s clamp, stores to `$01bd`/`$01bf` -- likely used for camera jumps (disaster alerts, advisor "take me there"), not incremental scrolling |
| `01:8c55-8c8e` | *(second reason-code dispatcher -- role now doubtful, see below)* | Near-identical to `01:8b4f`'s dispatcher, reached via `01:8b42`'s `$d7==1` branch instead of the default `$d7==0` path. Previously guessed (not confirmed) to be "the one that actually handles Y+direction (fast travel)" based on its Y-check (`01:8c75`) lacking the default dispatcher's `$01f5==0` precondition. **Now doubtful**: fast travel turned out to be fixable entirely within the default (`$d7==0`) dispatcher via B/X, not Y/A (see `01:8d26`/ROM_MAP "Fast travel"), and a deterministic scan of all 9 `STA $d7` sites in the ROM found none of them execute even once while holding Y+direction for 300 frames -- so `$d7` may simply never become 1 under these conditions, and this dispatcher's real trigger (if any) is still unknown |
| `01:9f2d` | *(reason-6 handler, dispatcher 2's Y-path)* | Extensive setup (clears `$01c1`/`$01f5`, sets several flags to `$ffff`, sets `$01df=3`), ends in `JMP $9dcc`. Internally also branches on `$d7` (`0`/`1`/`2` sub-states), separate from the top-level `$d7` dispatcher-select role |
| `01:9dcc` | *(possible task-scheduler entry point)* | Writes into tables at `$30c2,X`/`$ef20,X`/`$4420,X` indexed by `$01df` doubled, then returns immediately -- looks like "schedule a deferred task for mode `$01df`" rather than doing the work synchronously. Not yet confirmed as the same mechanism as the bank-`0d` task-scheduler lead from the cadence investigation, but a strong candidate -- worth checking |

## D-pad patch sites (all fixed -- `src/main.c`, search for "dpad fix:")

Full reasoning for each in `docs/INVESTIGATION_dpad.md`. Table here is
just the address list for quick lookup.

| Site | Fix | Screen/context |
|---|---|---|
| `00:bb09`, `00:bc65`, `00:c220`, `01:a7b8`, `01:a7fd`, `01:ae2e`, `01:c132`, `01:c2f3` | `LDA $011b` -> `$011a` | Map scrolling, build cursor, toolbar, in-game menus (batch of 8) |
| `01:cc24`, `01:e8f4` | `LDA $c9(dp)` -> `$c8(dp)` | Mode-select and other list-style menus (edge-triggered) |
| `02:a50c`, `02:ab1f` | `LDA $ca(dp)` -> `$c9(dp)` | Tax modal |
| `03:d33e` | `LDA $ca(dp)` -> `$c9(dp)` | Mode-select (mode 3) |
| `03:d62a`, `03:ddc3`, `03:ddd9` | `LDA $ca(dp)` -> `$c9(dp)` | Scenario Select (mode 5), Save (mode 11) |
| `02:a4ef` | `AND #$fff0` -> `#$ffff` | Tax screen's per-frame cursor-update gate (widened, not shifted) |
| `00:d1b8` | `LDA $c9(dp)` -> `$c8(dp)` | Load/Save/Exit menu |
| `03:d3e2` | `LDA $ca(dp)` -> `$c9(dp)` | Map Select scenario-number picker |
| `03:dad9` | `LDA $0124` -> `$0123` | City-name-entry on-screen keyboard |
| `03:d97b` | `LDA $c9(dp)` -> `$c8(dp)` | Select-game-level (Easy/Medium/Hard) |
| `02:8525`, `02:9f37`, `02:9f43` | `LDA $011c` -> `$011b` | Comprehensive/Information map overlay (scroll + cursor) |
| `01:f0d3` | `LDA $011c` -> `$011b` | View screen (watch icon) |

## Screen-mode dispatch (`$14`) -- and how to force any screen

`03:d289` is `LDA $14 ; ASL ; TAX ; JSR ($d255,X)`: the direct-page byte `$14`
selects a screen handler from a 23-entry table at `03:d255`. (Note `03:d286` is
*mid-instruction* -- the operand of a `REP #$20` -- so tracing that address
catches nothing, which is easy to mistake for "the dispatcher never runs".)

| `$14` | handler | | `$14` | handler |
|---|---|---|---|---|
| 0 | `03:d2b8` | | 12 | `03:df40` |
| 1 | `03:d2c6` *(boot/attract loop sits here)* | | 13 | `03:e1ec` |
| 2 | `03:d304` | | 14 | `03:e246` |
| 3 | `03:d333` | | 15 | `03:e257` |
| 4 | `03:d388` | | 16 | `03:e292` |
| 5 | `03:d3ca` | | 17 | `03:e296` |
| 6 | `03:d88a` | | 18 | `03:d30f` |
| 7 | `03:d8bb` | | 19 | `03:e2df` |
| 8 | `03:d951` | | 20 | `03:e344` |
| 9 | `03:d964` | | 21 | `03:d9eb` |
| 10 | `03:dd52` | | 22 | `03:da26` |
| 11 | **`03:ddb6` scenario select** | | | |

**`SC_FREEZE=14:<mode>` forces any of these to run**, which largely removes the
recurring "there is no save state at that screen" blocker: the handler executes
and its logic can be traced and its WRAM read, without navigating menus or
hand-timing `--input`. Graphics may be wrong (the preceding mode's setup never
ran), but the *logic* is real.

Worked example -- confirming the Las Vegas unlock end to end, from a cold boot,
with no save state at all:

```
SC_FREEZE=14:0b        SC_ADDR_TRACE=03:ddc1   ->  a=0002, x=0000  ($79 = 2)
SC_FREEZE=14:0b,43:80  SC_ADDR_TRACE=03:ddc1   ->  a=0003, x=8000  ($79 = 3)
```

i.e. freezing bit 15 of the completion field really does widen the grid by a
column, observed executing rather than argued from the disassembly. Freezing
`$43` directly also side-steps needing formatted SRAM, which a cold boot does
not have.

## WRAM usage map (from a recorded play session)

`SC_WRAM_MAP=<file>` records, per WRAM byte, whether it was read/written, the
**last** PC to write it, and a saturating write count. Measured over a session
that built a city, went bankrupt, lost a scenario, took a loan and collected
gifts:

- **Only 2,410 of the 8,192 bytes of `$7E0000`-`$7E1FFF` are live** -- i.e. ever
  written again after boot. The other 5,782 are written once by the boot clear
  and never touched. Most of the "variable space" is unused.
- `00:8018`-`00:8037` is that boot clear (`STA $00,X ; INX ; DEY ; BNE`, then a
  second loop over `$7E2000`+). A byte still owned by it is dead space, so
  last-writer is only meaningful where the owner is *not* this routine.
- `$1F00`-`$1FFF` is the stack. `$1E80`-`$1EFF` is a **relocated direct page**:
  several bank-03 routines do `PHD ; TDC ; SEC ; SBC #$0002 ; TCD` to allocate
  DP locals (e.g. `03:b152`, the power scan), so hot bytes there are stack
  frames, not variables.

### Auto-repeat: `$012b,X`, `$0133`, `$0135`

The shared edge detector at `00:929b` maintains a **fourth** per-port array
nobody had documented, and it implements key auto-repeat:

```
LDA $4218,X ; EOR $011b,X ; AND $bf   ; newly-pressed edges
STA $c9,X   ; STA $0123,X
CMP previous held
  changed   -> $012b,X = $0133        ; initial delay
  unchanged -> DEC $012b,X
               on zero: STA $0123,X   ; re-post held state as a fresh edge
                        $012b,X = $0135   ; repeat rate
```

`05:92a2` sets the constants once at boot: `$0133 = 10` frames initial delay,
`$0135 = 4` frames repeat. Confirmed live in save states.

**This does NOT pace the main-map cursor**, which was worth testing rather than
assuming. Holding Right from a loaded map with `$0133`/`$0135` frozen to 1
produces a byte-identical trajectory to the stock values (cursor `$01eb`
128 -> 138 -> 178 -> 218 -> clamp, then scroll `$01bd` +5 per 20 frames). The
cursor steps 2 units *every frame* and reads the **held** state (`$011b`/`$011c`)
directly, so auto-repeat only governs consumers of the **edge** array
(`$0123,X`) -- menus and list navigation. `docs/INVESTIGATION_cursor_cadence.md`'s
conclusion about the map cursor stands.

### Moving-object table, `$02cb`-`$0376`

A **column-major** entity table -- the same struct-of-arrays idiom as the
scenario map pointers at `03:ce70`, and just as invisible to a constant scan.
Field arrays are 16 bytes apart, each holding ~6-8 entries of 2 bytes, indexed
by `Y`. Updated by a family of routines at `03:f26a`-`03:f3e1`, one per field.

`03:f32d` shows the shape: `$0317,Y` is a per-entity countdown (`DEC`), `$0327,Y`
a **signed byte** velocity (sign-extended via `BMI` -> `ORA #$ff00`), and
`$0337,Y` a 16-bit position it accumulates into. `03:f3d0` does the same for
`$02f7`/`$0307` sourced from a ROM table at `$03eb71`. Consistent with the
game's moving objects (vehicles/aircraft/disaster sprites); which entity is
which is not established.

### Other live blocks with a single owner

| block | owner | notes |
|---|---|---|
| `$028b`-`$029a`, `$029b`-`$02aa` | `01:c8a5`, `01:c84b` | two 16-byte UI blocks, very hot |
| `$0c16`-`$0c25`, `$0c35`-`$0c44` | `03:b251`, `03:b257` | 16 bytes each, simulation side |
| `$0cd1`-`$0cdc` | `03:9053` | 12 bytes, saturated write count |
| ~250 bytes | `01:c891`/`01:c896` | largest single UI-owned region |
| ~224 bytes | `00:8aba`/`00:8ac1` | the OAM icon-row rebuild loop (`00:8aa8`) |

`03:a390`-`03:a3ce` is a **32-bit software multiply** (shift-and-add: 32
iterations of `ASL $0c ; ROL $0e ; ROL $10 ; ROL $12` with a conditional
`ADC`), writing its result to `$14,X`/`$16,X`/`$18,X`/`$1a,X` on a relocated
direct page. It is one of the hottest routines in the game -- the simulation's
arithmetic workhorse.

## `$003e` — the game-mode byte, and free play on a beaten scenario

`$003e` decides whether a running city is a *scenario* or ordinary free play.
Measured across the ten save states:

| value | meaning | states |
|---|---|---|
| `1` | free play | 0, 2, 3, 4 |
| `2` | free play, restored from a save slot | 1, 7, 8, 9 |
| `3` | **scenario** | 5, 6 (Las Vegas, `$0040 = 6`) |

Exactly one store to it is reachable in banks 00-07 — `03:ca57`, in the SRAM
load path, from `$70006c`. `03:cd96` is the matching save. Whatever sets it to
3 on a fresh scenario start is not an ordinary `STA`, so host code that wants to
change the mode should wait for the *value* rather than hook a site.

Every reader is in-game simulation logic. **Nothing on the map-load path reads
it** — the map is chosen by `$0040` alone (`03:ce2e`):

| site | test | what it gates |
|---|---|---|
| `03:b863` | `== 3` | scripted-disaster dispatcher (`JSR $b96f` at `03:b86b`) |
| `03:c502` | `== 3` | win/lose objective check, via `$0ccb` |
| `03:e2f5` | `== 3` | win-mark setter (writes `$42`, commits SRAM `$700007`) |
| `03:b916` | `== 1` | free play's own random-disaster threshold, by difficulty `$0b57` |
| `03:c3d5` | `== 1` | simulation branch (`Y` offset into `03:c11a`) |
| `03:c476` | `== 1` | simulation branch (guards the block at `03:c47e`) |

So a scenario map can be played under free-play rules by loading it normally and
then setting `$003e = 1`. That is not a synthetic value — it is what an ordinary
free-play city holds.

### Confirmed by execution

From `savestate_5` (Las Vegas, `$3e = 3`, `$0040 = 6`, `$0c0d = 304`), forcing
the mode to 1 over 300 frames:

| site | scenario mode | forced to free |
|---|---|---|
| `03:b863` gate | EXECUTED | EXECUTED |
| `03:b86b` `JSR $b96f` dispatcher | EXECUTED | **not executed** |

The gate keeps running and the dispatch under it stops, which is the whole
claim. `$0040` stays 6 throughout: the map is untouched, only the rules change.
`03:c50b` (the win-check body) was not reached in **either** run within 300
frames, so that arm is gated on the same byte but is not independently
confirmed here.

Because `03:cd96` writes `$3e` into SRAM, a free replay saved to a slot reloads
as free play. That is intended, but the choice does stick to the save.

### Verified live, and the fixture that made it possible

`savestate_3` and `savestate_4` sit **on the scenario-select screen** (`03:ddb6`
executes on load); `savestate_4` also carries `$42 = 0x807f`, so the win marks
are set. These are the first states that reach the selector at all, and every
claim below is measured from `savestate_4`.

`SC_NINTH`, driving Right (`kPad_Right = 0x0080` — the runner uses **serial**
pad order, so Right is `$0080` and A is `$0100`, not the 16-bit register order):

| | column | `$0040` | `$22` scroll |
|---|---|---|---|
| off | clamps at 3 | 6 | `$50` |
| on | **4** | **8** | **`$a0`** |

Before the off-by-one correction this feature did nothing at all: `$79` stayed
3 because `03:ddc1` fired ahead of its own `STA`.

The replay menu was confirmed in play the same session:

```
[replay] selector reached, $42=0000 col=0 row=0 idx=0
[unlock] scenario win marks $700007: 0000 -> 807f
[replay] B on idx=6 finished=1 $42=807f
[replay] menu opened on beaten scenario 6 ($42=807f)
[replay] menu opened on beaten scenario 2 ($42=807f)
[replay] scenario 2: FREE
[replay] free play engaged on scenario 2 at frame 5150 ($3e 3->1)
```

### `$c9`/`$ca` bit layout, as measured

Reaching FREE required Down then B and both registered, which pins the layout
of the edge-detect high byte `$ca`: **bit 7 = B, bit 3 = Up, bit 2 = Down,
bit 1 = Left, bit 0 = Right** — i.e. `$c9`/`$ca` hold the ordinary 16-bit
joypad word (`$c9` low byte = A/X/L/R plus the four unconnected zero bits,
`$ca` high byte = B/Y/Select/Start/Up/Down/Left/Right). The ROM agrees:
`03:ddc3 LDA $ca; AND #$0f` tests directions, `AND #$0c` Up/Down, `AND #$03`
Left/Right, and `03:de46 LDA $c9(16); AND #$8000` tests B.

This **contradicts** the `$00ca` row in the WRAM table above, which says `$ca`'s
low nibble is hardware-guaranteed zero and puts the direction bits in `$c9`.
Those two rows appear to describe the bytes the other way round. Left in place
rather than rewritten because that row was derived from the `$011b`/`$011c`
mirrors during the joypad-transposition work and may be describing a different
pair; but for `$c9`/`$ca` specifically, the layout above is what actually
behaved correctly in play.

### `$3e = 1` is the practice map, not free play

The first cut of the replay menu set `$003e = 1` and that was wrong. `03:b916`
reads it as:

```
03:b916  LDA $003e ; CMP #$0001 ; BEQ $b967   ; ==1 jumps PAST the threshold
03:b91e  LDA $0b57 ; ASL A ; TAY ; LDA $b969,Y
```

so `== 1` *skips* the difficulty-indexed random-disaster threshold rather than
selecting it, and it also fires the Dr. Wright "let's practice our city
building techniques" intro. That is practice mode. Reported from play as the
advice popup appearing on a freshly started free scenario.

Ordinary free-play cities hold **`$3e = 2`** (save states 1, 7, 8, 9). It fails
every `== 3` scenario gate exactly as 1 does, without the practice behaviour,
so that is what a free replay should use.

Verified end to end from `savestate_4` by scripting B / Down / B on the
selector:

| pick | `$003e` | `$0040` |
|---|---|---|
| STANDARD | 3 (scenario, untouched) | 6 |
| FREE | **2** | 6 (Las Vegas map intact) |

### Open: the scenario briefing fax still shows on a FREE replay

The briefing is fired by the load path and keyed on `$0040`, not on `$3e`, so
it appears identically on STANDARD and FREE starts — captured both ways at
frame 1100 and the two frames are the same fax. Suppressing it for a free
replay needs the fax trigger located first; `$14 = 0x0d` looks like the
message-screen state (`savestate_5` sits in it, and X moves it to `0x0b`) but
that state is presumably shared with ordinary in-play advisor faxes, which a
free city should still get.

### Fax text dropping the first character of every word — a save-state artifact

Captured from `savestate_4` with scripted input, every scenario briefing renders
with the first character of each word missing:

> as egas, he orld's argest ambling ity, as everely amaged y udden ttack f
> nidentified lying bjects.

The stored data is **not** at fault — decoding the shipped tilemaps gives "Las
Vegas, the world's largest gambling city," complete — and it is not the typing
animation either, since frames 1250/1500 and 5200/7000 are pixel identical.

It is also **not** what the player sees. Playing in from a fresh boot, the Sylt
briefing renders every character ("The North Sea has taken the dunes. Storm
surges break..."), reported and screenshotted from play. Every reproduction of
the fault instead loads `savestate_4` and drives the selector with `--input`.

So this is an artifact of the save-state path, not a defect in the fax
renderer. Recorded because it will keep reappearing in captures taken that way
and should not be mistaken for a product bug a third time: an earlier note here
called it a confirmed display bug on the strength of a windowed run, but that
run loaded `savestate_4` as well, so it never tested the thing it claimed to.

## The ninth entry — SYLT

### The map already exists

`03:ce70` is a nine-entry struct-of-arrays and index 8 points at `$0dd131`,
the last blob in the map region:

| idx | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 |
|---|---|---|---|---|---|---|---|---|---|
| ptr | `0d9f23` | `0ce30b` | `0c8f27` | `0cc5a2` | `0ca8e8` | `0d816e` | `0db987` | `0dcb15` | **`0dd131`** |

Loaded through `SC_NINTH` it decompresses to a real 120x100 map: **36 distinct
tiles**, 9831 cells of water against 1386 of the next tile, drawing as a sandy
island with woodland patches, a north-eastern islet and a south-western
sandbar. No roads, no buildings, nothing built anywhere. An undeveloped North
Sea sand island — which is why it can be Sylt without inventing any terrain.

The game treats it as a scenario: it comes up 1991 JAN, $20 000, population 0,
with "5 years to complete scenario". The briefing it shows is free play's
"Welcome to the world of <title>" (index 8 falls off the eight-entry seed
tables, so `SC_NINTH` supplies free play's values).

Note index **7** is already the `Free` card, 1991 — the ninth is genuinely
spare, not a duplicate of it.

### Why the card has to be host-drawn

The selector background is a fixed-width tilemap. Measured by scrolling and
finding the last non-black column:

| `$22` scroll | content ends at world x |
|---|---|
| `$50` (stock column 3) | 335 (fills the screen) |
| `$60` | 351 |
| `$70`, `$80`, `$a0` | **359** — everything past it is black |

So the background stops at world x = 359 and column 3 already views to 335.
That leaves **24 px of slack where a card needs about 70**. `SC_NINTH_SCROLL`
defaulted to `$a0`, which simply scrolled into the black void; `$68` is the
largest scroll with no black margin and is the default now.

A ninth card therefore cannot come from the guest without extending that
tilemap, which is a ROM change. `render_sylt_card()` paints it host-side
instead, over the right edge, using the same overlay route as the replay menu.
With the screen already full of cards it unavoidably overlaps the right edge of
the Las Vegas card — there is no clear space at any scroll.

### Extending the wood, and a keying bug it exposed

The wood is a designed panel, not a tiling texture — autocorrelating the strip
beside the cards found no worthwhile period under 120 px. So
`selector_extend_wood()` does not try to continue the pattern. It mirror-tiles,
taking the source from the **same row** so the grain lines always meet and
alternating direction so there is no hard seam.

The source is the leftmost 16 columns, which is the only span clear of both the
cards and the title on all 224 rows. Measured at scroll `$a0`: the title
reaches x = 196 on its own rows, the rightmost card-free column overall is only
x = 197, and although the worst single row still leaves a 40 px run somewhere,
`x = 0..15` is the only span clear on *every* row.

With the margin filled, `SC_NINTH_SCROLL` goes back to `$a0` and the Sylt card
gets a real fifth column instead of overlapping Las Vegas.

**The keying bug.** The first cut of this filled nothing at all. The test was

```c
while (x >= 0 && row[x] == backdrop) x--;
```

and `backdrop` is built with `0xFF000000` while `ppu_runLine` writes pixels
with the **top byte left at 0** — sampled live: `00310000`, `00522910`. So the
comparison could never be true.

The same mistake was already in `host_map_compose()`:

```c
if (p != s_backdrop_argb && (p & 0x00FFFFFFu) != 0) dst[x] = p;
```

`p != s_backdrop_argb` was **always** true, so the backdrop keying there has
never done anything and only the black test ever ran. That keying was added
specifically so a fade would match, by treating backdrop pixels as transparent
— which is why the fade never came right. Both now compare 24 bits.

Measured effect on the city view: **0 of 57344 pixels change**, because that
screen's backdrop is black and the two tests coincide there. It should differ
during a fade, which is the case it was written for and the one still worth
checking in play.

### The card as real tiles

The ninth card is drawn by the PPU as part of BG1. Everything below was
measured off the live screen with `SC_SELECTOR_PPU=1`, not assumed.

| | |
|---|---|
| layer | mode 0, **BG1 the only layer on the main screen**; tilemap `$3000`, `wide=1` (64 columns), character base `$0000` |
| cards | 8 columns x 9 rows of body at columns 12/22/32 — ten apart, so the fifth lands on **column 42** — rows 5..13, drop shadow of tile `$0010` down the right edge and along the bottom |
| palette | **2**: black / `#94948b` / `#eeeecd` / `#73736a`, exactly the range the shipped card art uses |
| free CHR | tiles `$24b..$2d5` (139) and `$2e0..$3ff` (288) are blank in VRAM **and** unreferenced by the tilemap — 427 slots against the 72 a card needs |

Because BG1 is 2bpp, a tile has four colours and picks one of eight palettes.
Palette 2 already spans black to cream, so a greyscale drawing quantises into
it directly and needs no per-tile palette assignment.

### Scrolling constrains where the card can sit

The tilemap wraps at 64 columns (512 px), so any scroll past 256 would bring
column 0 back around on the right. The ninth column's `$a0` (160) shows columns
20..51, and the stock column-3 `$50` (80) shows 10..41. A card at columns 42..49
is therefore fully visible on the ninth column and entirely off-screen on the
stock one, which is the behaviour wanted, and it lands on the ROM's own
ten-column grid rather than an arbitrary offset.

### Why the whole card comes from the artwork

Card names on the shipped cards are **pre-rendered word strips** living in
VRAM — "San Francisco", "Earthquake", "Bern", "Traffic", "Coastal",
"Flooding" and so on — placed as sprites rather than written into the tilemap
(rows 11..12 of a card are blank cream). There is no "Sylt" strip, and the
strips are whole words rather than glyphs, so one cannot be composed from them
either. Only the year is BG: digits are tiles `$0a0 + d` at palette 2, which is
how "2096" decodes as `08a2 08a0 08a9 08a6`.

So the drawn card supplies its own caption, and the full 64x72 block is
converted rather than just the thumbnail.

**Known gap:** the coloured pin above each shipped card is a sprite too, so the
ninth card has none. Cosmetic, and it would need a free OAM slot plus the
sprite's own tile.

### The selection cursor, and why it missed the ninth card

`03:dea4` draws the blinking green border from two **eight-entry** word tables,
indexed at `03:dea8` by `$0040 * 2`:

| | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
|---|---|---|---|---|---|---|---|---|
| `03:df00` Y | 0027 | 0027 | 0027 | 007f | 007f | 007f | 0027 | 007f |
| `03:df10` X | 0010 | 0060 | 00b0 | 0010 | 0060 | 00b0 | 0100 | 0100 |

Index 8 therefore reads sixteen bytes past `03:df10`, which is the win-mark
table at `03:df20`, and the border lands on the wood instead of the card —
reported from play as the ninth entry never getting the green blinking border.

X is `0010`/`0060`/`00b0` for grid columns 0..2 and `0100` for column 3:
**eighty apart**. Column 4 is therefore `$150` = 336, which is tilemap column
42 x 8 — the position the card already occupies, arrived at independently from
the card spacing. Y is `$27` on the top row.

Supplied at `03:debb`, after `03:deb2`/`03:deb8` have stored x and y, with the
same `SBC $16` scroll subtraction the ROM applies at `03:deb0`.

The win-mark tables at `03:df20`/`03:df30` are eight entries as well, and
`03:ded0` walks only eight bits, so the ROM never marks Sylt. The host adds
Sylt's pin and mark itself (`selector_sylt_sprites()` in src/main.c): the
selector's sprites are sprite-text records -- `$12` the eight pins from
(`$A0`-`$16`, `$60`), `$11` the blinking bracket, `$29` a mark from each
(`$DF30`,Y-`$16`, `$DF20`,Y) -- and Sylt's are Las Vegas's moved one
column, 80 px, right.

### The title's light row cannot be widened by any display setting

The row of blinking lights along the bottom of the title is OBJ, not a
background. Measured at 448 wide (`SC_LAYER_MASK=0x10`, authentic area is
x = 96..351):

| frame | light row spans |
|---|---|
| 1430 | x = 48..335 |
| 1434 | x = 45..335 |
| 1438 | x = 42..335 |

So the row **scrolls left** — its left edge advances 3 px per frame and passes
happily into the left margin — while its right end sits fixed at x = 335, which
is authentic x = 239. The ROM spawns each light there and never places a sprite
further right, because for a 256-wide picture 239 is already near the edge.

Widescreen therefore shows the spawn point 112 px inside the right margin
instead of 16 px from the screen edge, and the lights appear to pop into
existence mid-picture. Reported from play as wanting them to "render before
entering and delete after disappearing" -- the *disappearing* half already works,
because the left margin is drawn.

Two mechanisms were tried and neither applies:

* the ambiguous-band decode (`wsOamRightHintStrict`) makes no difference --
  strict and permissive both give x = 48..335, so these are not sprites parked
  in `[256, 256+extraRight)`;
* `PpuSetWsHudOamShift` anchors *edge-hugging* sprites outward with the margins,
  which for a continuous scrolling row would move only its rightmost members and
  tear a gap in the middle of it.

There is nothing to reveal: the sprites do not exist out there. Closing the gap
means changing where the ROM spawns them -- a host hook on the title's marquee
that widens its spawn X and wrap point, in the same spirit as the ninth-scenario
hooks. That is a behaviour change rather than a presentation one, and is the
only route that can work.

### Not the gradient

The sky gradient was reported as incomplete and is not. It is BG3, whose sky
rows are uniformly tile `$0000` with scattered stars and a silhouette along the
bottom. Unclamping BG2 and BG3 on the title changes the clamp mask from `$0e`
to `$08` and produces **zero** pixel difference at four different scroll
positions, so that change was reverted rather than shipped inert. Confirmed
from play afterwards that the gradient looks correct as it stands.

### Widescreen exposes sprites the ROM parks off-screen

The scenario selector's blinking green cursor shows a second marker in the left
margin at 448 wide. It is not a duplicate: it is the *hidden* phase of the same
cursor.

`03:de9d` toggles `$30` and `03:dea6` skips the draw when it is clear, and the
ROM hides the sprite by parking it at a negative X. Hardware clips that away
entirely. Widescreen renders those columns, so the parked sprite appears.

Measured on `savestate_3`, `$42` forced unlocked, same frames both ways:

| frame | 256 wide | 448 wide |
|---|---|---|
| 1432, 1444 | box at 176..239 | box at 272..335 — 176+96, correct |
| 1436, 1440 | **nothing** (blink off) | **box at 48..71, left margin** |

Reproduces identically with `SC_NINTH=0`, so it is nothing to do with the ninth
entry.

**There is no setting that fixes this without breaking something else.**
`SC_WS_OBJ_CLIP=1` removes it, and also removes the win marks and the cursor
from any card that legitimately sits in a margin -- both are sprites at negative
X, and nothing in OAM distinguishes "parked to hide" from "scrolled out of the
authentic window but genuinely wanted". That is the same trade recorded against
the sprite clip default.

A real fix needs per-slot knowledge. The runner already has the mechanism for
the right-hand band -- `wsOamRightHint`, a bit per OAM slot published by the
game each NMI, with `wsOamRightHintStrict` deciding whether unmarked slots wrap
negative. The left band has no equivalent. Extending it symmetrically, or
letting a host mark slots it knows are parked, would settle this properly and
is worth raising upstream.

### Open: the selector comes back half-loaded from a scenario

Starting a scenario and backing out to the selector leaves the screen's
graphics corrupted: thumbnails garbled, "LasVegas" reading "to.asVegas", "UFO"
reading "MtC", "Monster Attack" and "Traffic" mangled, and the selection border
drawn in blue as well as green. Reported from play as "the sprites are not
loaded".

**Not caused by the ninth entry.** Captured from `savestate_4` by entering Las
Vegas and backing out, with `SC_NINTH` off and on: the two frames differ by
**0 of 516096 pixels**. It reproduces with the feature disabled entirely.

Distinct from the ninth card coming back black, which WAS a ninth-entry bug —
the card's character data was uploaded once behind a static flag, and the
screen's re-entry reloads VRAM over it. Both symptoms appeared together in the
same report, which is why the control mattered.

### The real Sylt map, without patching the ROM

The map is **by lytron**, released 25 October 2014, included with the
author's written permission — see `sylt_graphics/PROVENANCE.md`.

It arrives as an IPS that drops a compressed map at `$108000` and repoints
scenario index **5 — Rio** — at it, so applying it plainly replaces Rio. A
companion patch exists to relocate Rio first. Neither is applied, for two
reasons.

**Patching the ROM would cost four features.** `main()` fingerprints the image
with FNV-1a and sets `s_rom_is_us` on an exact match against the pristine US
ROM. That flag gates the host map renderer (`ScMapView_Render` returns false
without it), `SC_FIBER`, the cursor-cadence patch and the view fix. Any patch
changes the fingerprint and silently loses all four.

**Index 8 is the practice map, not a spare slot.** Overriding its data
unconditionally would hand Sylt to the tutorial.

So the map is decompressed offline by `tools/make_sylt_map.py` and written into
WRAM at run time, and only when the ninth entry was actually confirmed:

| | |
|---|---|
| arm | `03:de4d`, the B-accepted path, sets the flag only when `$52 == 4` and **clears** it for every other choice, so a stale arm can never reach another scenario |
| swap | `03:ce5e`, the `JSR $d15f` — `03:ce2e` has decompressed the map to `$7E8000` and is about to unpack it, so the buffer is replaced with Sylt's own intermediate and the ROM's unpacker does the work |

Verified: Sylt loads 150 distinct tiles (the stock index-8 island has 36) as the
real island — Ellenbogen at the north, the narrow waist, Westerland built up,
and the Hindenburgdamm running east. Starting Las Vegas in the same build logs
no swap at all and loads its own 472-tile map.

The seed for index 8 reproduces what the patch produces: it repoints index 5,
so Sylt inherits **Rio's** entries except where overridden — year `07ff` (2047,
which is what the card says), event `0102`, class `0004` -> `0001`, population
25341 -> 3400.

### Blob sizes, for reference

| | compressed | unpacked |
|---|---|---|
| index 8 island (stock) | 1611 | 2814 |
| index 5 Rio (stock) | 7605 | 10068 |
| index 6 Las Vegas | 4494 | 8030 |
| Sylt (patch) | 4304 | 4034 |
| "Rio on its own Bank" (patch) | 7605 | 10068 |

The last row is the important one: that patch's payload is **byte-identical to
Rio's map in the ROM**, so it is a verbatim copy of copyrighted data and is not
committed, whatever permission covers the Sylt map itself.

### Still missing for a real scenario

Sylt currently has a map, a card and free play's seed. It has no briefing text,
no win condition (`$0ccb` objective index), and no disaster of its own, so the
"5 years to complete scenario" timer counts against nothing.

### `SC_RENDER_DUMP_AT` / `SC_RENDER_DUMP_PATH`

`SC_DUMP_AT` captures `s_video_pixels`, which is the guest frame *before* any
host overlay, so it cannot see the settings menu, the replay box or the Sylt
card. `SC_RENDER_DUMP_AT` captures the renderer instead. It only works in a
real windowed run — `--qualify` never reaches that loop, which is also why
`SC_MENU_PREVIEW` has never worked under it — and it quits after the capture.
Frame numbers are absolute, and `--load-state` restores the saved frame
counter, so a state saved at frame 2919 needs targets past 2919, not past 0.

## Cartridge SRAM layout (`$700000`+)

SRAM is **not** part of `g_ram` -- it lives in the cart model, so WRAM dumps do
not capture it and it has to be read back through the bus. `SC_SRAM_DUMP_PATH`
dumps the 32KB window and prints a decoded header line.

A 16-byte header, then the per-city save block. The header is checksummed and
mirrored, so a host-side edit that updates only the field it cares about will
be reverted at the next verify -- see `03:e411`/`03:e553` above.

| Address | Meaning |
|---|---|
| `$700000`-`$700002` | Magic `'S'`,`'I'`,`'M'` |
| `$700007` (16-bit) | **Scenario completion bitfield.** Bit N = scenario N won (mask table `03:e334`); bits 0-5 the six ordinary scenarios, bit 6 Las Vegas, bit 15 = "all six beaten", set by the game itself at `03:e31c`. Read into `$42` at init |
| `$700009` | Debug-menu cheat flags, mirrored to `$0425` |
| `$70000e` (16-bit) | Checksum: sum of the **bytes** `$700000`-`$70000d` |
| `$700010`-`$700084` | Per-city save fields, copied one by one to WRAM by `03:c8f1` (load) / `03:cc2e` (save). Includes `$700036` -> `$0deb` (city class) and `$700070` -> `$0040` (scenario index) |
| `$700084` +60 bytes | Array -> `$0ced` (30 words) |
| `$7000c0` +1440 bytes | Array -> `$7f5fc0`, i.e. directly after the 24000-byte map |
| `$700660` +1440 bytes | Array -> `$7f6560` |
| `$707ff0`-`$707fff` | Backup copy of the 16-byte header (`03:e484` writes it, `03:e446` restores from it) |

Verified against real game-written SRAM (an in-game save state): magic reads
`SIM` and the stored checksum matches a recomputation exactly.

Two save-file bases beyond the first appear at a stride of `$3ff0` (`$703ff0`
and `$707ff0`); `03:e392`-`03:e402` reads the same field set from two of them,
which looks like a save-slot summary. Not investigated further.

## Compressed data regions (Nintendo LC_LZ5, see `tools/extract_graphics.py`)

| Region | ROM offset | Notes |
|---|---|---|
| Font tileset | `0x04C0FB` | 1bpp after reduction from the stored 2bpp form; ~10KB decompressed |
| Dialog text | `0x07A868`-`0x07DA83` | Plain ASCII on non-JP releases (no compression), one block |
| Scenario tileset | `0x04875C` | ~16KB decompressed |
| Scenario text, group 0 | `0x05BCAD`, 5 packets | |
| Scenario text, group 1 | `0x05EE30`, 12 packets | |

## Debug menu / cheat mechanism (found via published Pro Action Replay codes)

A user-supplied list of published cheat-code addresses (Game Genie/PAR-style
`bank:addr:value`, same LoROM addressing this doc uses throughout) included
one labeled "Enable Debugger" at `01:88e7`. Tracing it forward, cross-checked
against a second, independent source (a fan guide crediting Corey Miller/
"ZaphodBee" describing an in-game debug menu reached via a controller-2 code
at the quit-confirmation screen), both point at the same mechanism:

- `01:88e7-88ee`: boot-time routine, `LDA $700009` (long -- reads one byte
  from cartridge SRAM) `; STA $0425 ; RTS`. The "Enable Debugger" PAR code
  replaces the load with `LDA #$80`, forcing this to look nonzero
  regardless of real SRAM contents.
- `00:da04-da3f`: the in-game menu's option handler. Looks up a per-option
  bitmask from a table at `00:da50` (`01 02 04 08 10 00` for options 1-6),
  XORs it into `$0425` to toggle (options 1-4), and for option 5
  ("Memory"), instead writes `$0425` back out to SRAM `$700009` -- the same
  address the boot loader reads. This is the "Memory: CLR/SET, reset to
  activate" flow the fan guide describes, fully confirmed from ROM bytes
  alone, no live testing needed.
- `$0425`'s individual bits are then read at 19+ sites across banks 00/01/03
  to gate the actual cheats -- see the `$0425` WRAM table entry above for
  the confirmed/inferred bit mapping.

`src/main.c` exposes this two ways: **F2** automates the documented
controller-2 entry sequence (unverified whether this ROM revision even
reads controller 2 -- a separate whole-ROM search for any `$421A`/`$421B`/
`$4016`/`$4017` access found none), and **F5-F8** poke `$0425`'s bits
directly, bypassing both the entry code and the in-game menu navigation
entirely. The direct-poke route is the higher-confidence one since two of
its four bits are independently confirmed against their actual consumers,
not just inferred from the option table's ordering.

## Open investigation threads (see linked docs for full detail)

- **Cursor cadence** (`docs/REVERSE_ENGINEERING_cursor_movement.md`,
  `docs/INVESTIGATION_cursor_cadence.md`): real hardware steps `$01ed`
  every frame while held; this recomp only does so in ~4-frame-active/
  16-frame-idle bursts. Every individual branch condition along the call
  chain has been confirmed to evaluate identically to real hardware --
  the gap is in *how often the dispatcher itself gets invoked*, not a
  wrong decode. Leading candidate: an unconfirmed task-scheduler jump
  table in bank `0d` -- possibly the same mechanism responsible for the
  ~4-frame periodic gating found via the fast-travel thread below, but
  an attempted static identification of that scheduler (a table at
  `01:8fda`) didn't hold up on closer inspection and was retracted --
  not yet resolved either way.
- **Fast travel: FIXED.** Root cause was a 9th, previously-unpatched site
  in the same D-pad "wrong nibble" bug family as the 8 sites already
  fixed at load time: `01:8d36` does `LDA $011b` (16-bit) `; SEP #$20 ;
  XBA ; AND #$0f ; BEQ ...`. The `XBA` swaps A's bytes before the 8-bit
  `AND`, so it tested `$011c`'s hardware-dead low nibble instead of
  `$011b`'s real direction bits -- meaning the `BEQ` always took, so
  `01:8d26` (reached whenever B or X is held, via `$011b AND #$4080` at
  `01:8bd6`) never wrote `$01c1` and never dispatched to any of its 4
  per-direction handlers (`b2f9`/`b1f6`/`b166`/`b030`), for *any* held
  direction. Fixed with the same technique as the other 8 sites (repoint
  the load's low byte from `$011b` to `$011a`). Confirmed end-to-end via
  deterministic testing (a user-captured save state + `--load-state` +
  `--input <frame>:<dur>:<mask>`, holding B+Up/B+Right with zero live-input
  jitter): the direction handlers now fire, `01:afbe`'s bit-ladder now
  runs with real data, and a before/after WRAM dump shows `$01bd`/`$01bf`
  (map scroll-X/Y) genuinely changing. Zero regression on the full
  10800-frame qualify baseline. The earlier "scheduling race" theory
  (`$011b` reading `$0000` at `01:c01e`'s periodic checks) turned out to
  be a live-keyboard-timing artifact, not a real bug: the same
  deterministic test shows `$011b`/`$011c` read correctly at `01:c01e`
  on every single hit once input is held via `--input` instead of a
  physical key. Also corrects an earlier mislabeling: the modifier this
  mechanism actually checks (`$011b AND #$4080`) is **B or X**, not Y/A --
  `$011c` bit 6 is X, not Y (Y is `$011b` bit 6); Y/A do not reach this
  code path at all (confirmed: `$c5` only ever became `0`/no-op or `2`
  /advisor-toggle while holding Y+direction, never `1`). See
  `docs/INVESTIGATION_dpad.md` "Fast travel" for the full writeup and the
  save-state-based testing method that finally cracked it.
- **View screen rendering** (`docs/INVESTIGATION_dpad.md`, "Open item:
  View screen's D-pad"): the write side is fully confirmed and working;
  no renderer/consumer of `$7e21b4`/`$7e21b5` has been found yet.
- **Sound**: no total-failure bug in the underlying DSP simulation (audio
  active ~92% of a simulated minute, confirmed via headless qualify
  mode). Live testing found a real, specific symptom: audio in the
  windowed build lags behind by 1-2 seconds, traced to fast-forward
  batches (`frames_this_iter > 1` in `src/main.c`) leaving several
  frames' worth of DSP output undrained each time, which then plays back
  later as increasingly stale audio -- this includes the *automatic*
  fast-forward (not just Tab-held), which fires any time the LC_LZ5
  decompressor runs, including ordinary dialog/UI popups mid-game, not
  just loading screens. Two attempts to discard that backlog (both a
  hand-rolled fix and the shared runner's own `dsp_trimSamples()`) each
  caused a complete, permanent audio freeze in live testing instead --
  root cause of *that* not found, reverted. The 1-2s delay remains
  unfixed; `SC_AUDIO_DEBUG` (periodic drain-loop stats) was added for the
  next attempt.
- **Widescreen** (`docs/PLAN_widescreen.md`): scoped, not implemented --
  the shared engine already has the rendering machinery; needs game-
  specific BG-layer identification and visual verification.

## Simulation tick, calendar, seasons, population and the annual budget

Recovered with `tools/dis_mx.py` (width-tracking disassembly cross-checked
against the coverage bitmap) plus `SC_WRAM_MAP` write-attribution on a save
state whose city is actually running. Every listing below is fully
`*`-marked — i.e. every instruction quoted was executed in a recorded
session — and no operand byte is marked executed, so the widths are the ones
the CPU used.

### The tick routine, `03:8000`

Entered with `SEP #$20 ; REP #$10`, then `LDA #$03 ; PHA ; PLB` to put DB=3.
It calls a fixed pipeline (`$90a7`, `$c474`, `$b84b`, `$88b4`, `$894c`,
`$821d`, `$8297`, `$addf`) and then advances time:

```
03:8026  INC $0b51                  ; tick counter
03:8029  LDA $0dc7 ; CLC ; ADC $0dc5 ; STA $0dc7
03:8033  LDA $0b51 ; AND #$0003 ; BNE $80b0     ; every 4th tick only:
03:803b  INC $0b55                  ; month
03:803e  LDA $0b55 ; CMP #$000d ; BNE $804f
03:8046  LDA #$0001 ; STA $0b55     ; month wraps 13 -> 1
03:804c  INC $0b53                  ; year
```

| address | meaning |
|---|---|
| `$0b51` | tick counter. Cadence depends on the in-game speed setting — 200 frames per tick on one save state, 125 on another running faster |
| `$0b55` | month, 1..12 |
| `$0b53` | year (1902/1904/1905/1991/… matching the per-scenario seed at `03:ced9`) |
| `$0dc7` | accumulator, `+= $0dc5` every tick |

So **4 ticks = 1 month** and **12 months = 1 year**. That structure is fixed;
the tick *cadence* is not, because the game has a speed setting. On one state
a tick took 200 frames (verified over 9,000 frames: 45 ticks, 1904 → 1905);
on a state running faster, six months passed in 3,000 frames, i.e. 125 frames
per tick.

`$0b53 - 10` is stored to `$0da9` and `$0b55 - 1` to `$0dad` (display
forms), and in month 1 also `$0b53 - 120` to `$0dab`.

### Seasons: two month-indexed tables at `03:8160` and `03:816d`

```
03:8090  LDY $0b55
03:8093  SEP #$20
03:8095  LDA $8160,Y ; BEQ $80b0        ; gate: only on a season boundary
03:809a  LDA $816d,Y ; CMP $0b4d ; BEQ $80b0
03:80a2  STA $0b4d                      ; new season
03:80a7  LDA #$0001 ; STA $0b4b         ; "season changed" flag
03:80ad  STZ $0b4f
```

Both tables are bytes indexed directly by month (index 0 unused):

```
03:8160 gate    00 00 00 01 00 00 01 00 00 01 00 00 01 03
03:816d season  03 03 03 00 00 00 01 01 01 02 02 02 03 03
```

The gate is nonzero only at months **3, 6, 9 and 12**, and `$0b4d` becomes
0, 1, 2, 3 there. So the seasons are Mar–May = 0, Jun–Aug = 1, Sep–Nov = 2,
Dec–Feb = 3. Confirmed dynamically: over 9,000 frames `$0b4d` was written
exactly **4 times**, all from `03:80a5`.

### Population, `03:8196`

```
03:81a3  LDA $0b8f ; CLC ; ADC $0b93
03:81aa  ASL A ; ASL A ; ASL A          ; x8
03:81ad  ADC $0b8b
03:81b0  STA $00
03:81b2  LDA #$0014 ; STA $02           ; x20
03:81b7  JSR $a2f5  [00 02 00]          ; 16x16 -> 32 multiply, inline args
03:81bd  LDA $00 ; STA $0ba5            ; population, low word
03:81c2  LDA $02 ; STA $0ba7            ; population, high word
03:81c7  $0de3:$0de5 = population - $0bcd:$0bcf     ; change since last
```

**`population = (($0b8f + $0b93) * 8 + $0b8b) * 20`**, held as a 32-bit
value in `$0ba5` (low) / `$0ba7` (high).

### Which zone each tally counts

Three sibling accumulators at `03:924f`, `03:92fb`, `03:93b1` each call a
helper that turns the tile index in `$0b89` into a capacity contribution.
The helpers differ only in the class base they subtract:

| helper | tile base | cycle | returns | tally |
|---|---|---|---|---|
| `03:842f` | `#$0099` = 153 | `#$24` = 36 | `Y * 8` | `$0b8b` |
| `03:8456` | `#$0144` = 324 | `#$2d` = 45 | `Y` | `$0b93` |
| `03:847a` | `#$0201` = 513 | `#$24` = 36 | `Y` | `$0b8f` |

Each subtracts its base, returns 0 if the tile is below it, reduces modulo
the cycle and then divides by 9 — i.e. 4 development levels of 9 tiles for
residential and industrial, 5 for commercial.

In tile-index order (153 < 324 < 513) that is **residential, commercial,
industrial**:

| tally | zone |
|---|---|
| `$0b8b` | **residential** |
| `$0b93` | **commercial** |
| `$0b8f` | **industrial** |

Two independent checks agree. The residential helper applies its own `* 8`
where the other two get it in the population formula, so all three end up in
the same units. And in a real city residential dominates: savestate 2 holds
`$0b8b` = 2718 against `$0b93` = 102 and `$0b8f` = 225.

The formula was verified against save states directly — exact on every state
where the tick has actually run (s2: 106,680; s6: 15,320). States that differ
are scenarios still holding the starting population seeded from `03:cef9` /
`03:cf09`, before the first recomputation.

`SC_WRAM_MAP` attributes the two stores to `03:81BF` / `03:81C4`, written 45
times in 45 ticks — population is recomputed every tick.

### The annual budget, `03:8df1`

Called from the tick **only when `$0b55 == 1`** (`03:8087`), i.e. once per
game year.

```
03:8dfe  STZ $0bc1
03:8e01  LDA $0dc3 ; BEQ $8e07 ; RTS    ; re-entrancy guard
...
03:8ec8  LDA #$0001 ; STA $0dc3
03:8ece  LDA $0dc3 ; BNE $8ece          ; spin until the UI clears it
03:8ed6  LDY $0b1d ; BEQ $8ee1
03:8edb  DEC $0b1d ; LDA #$01f4         ; 500 charged while $0b1d counts down
03:8ee2  ADC $0dcf ; ADC $0dd1 ; ADC $0dcd
03:8eeb  STA $00                        ; $00 = total outgoings
03:8eed  LDA $0dc9 ; CLC ; ADC $0dd9 ; SEC ; SBC $00
03:8ef7  STA $0bc1                      ; net balance for the year
03:8efa  treasury($0b9d:$0b9f) += $0dc9:$0dcb, += $0dd9, -= $00
03:8f27  LDA $0b9d ; CMP #$423f ; LDA $0bff ; SBC #$000f ; BCC $8f41
03:8f35  clamp to $000F423F
```

| address | meaning |
|---|---|
| `$0b9d` / `$0b9f` | **treasury, 32-bit**, clamped to `$000F423F` = **999,999** |
| `$0dc9` / `$0dcb` | annual income, 32-bit |
| `$0dd9` | further income term, added separately |
| `$0dcd`, `$0dcf`, `$0dd1` | three outgoing line items |
| `$0bc1` | net balance for the year |
| `$0dc3` | budget-dialog busy flag; set to 1, then spun on until the UI clears it |
| `$0b1d` | counts down; while nonzero, **500 per year** is added to outgoings |
| `$0dd5`, `$0dd7` | derived stats, each clamped to `#$270f` = 9999 |

Three outgoing line items against one tax income is the shape of the game's
budget screen. **`$0b1d` is the bank loan** — confirmed by the user from
play — so `$0b1d` is the number of annual repayments outstanding and 500 is
the yearly instalment.

### Gift-building income: `03:ae61`, `$0ddd` -> `$0dd9`

`$0dd9`, the income term added to the treasury separately from taxes, is a
straight copy of `$0ddd` at `03:8e95`. `$0ddd` is zeroed at `03:8279` and
accumulated one building at a time by `03:ae61`, which is a flat comparison
ladder on the tile index:

```
03:ae61  INC $0c71                       ; count of paying buildings
03:ae64  LDY #$012c ; CMP #$02fe ; BEQ   ; 300/year
03:ae6c  LDY #$00c8 ; CMP #$02ec ; BEQ   ; 200/year
03:ae74  LDY #$0064                      ; 100/year for any of:
         CMP #$02f5 / #$034f / #$033d / #$0334 / #$032b / #$0319
03:ae95  LDY #$0000                      ; everything else pays nothing
03:ae98  $0ddd += Y
```

| payout per year | tile indices |
|---|---|
| 300 | `$02fe` |
| 200 | `$02ec` |
| **100** | `$02f5`, `$0319`, `$032b`, `$0334`, `$033d`, `$034f` |

This matches the reported behaviour that **a casino pays $100 per year**, so
the casino is one of the six tiles in the 100 group. Which building each of
the eight tile indices is has not been established — that wants placing them
in play and watching `$0c71`, not guessing from the ROM.

The `$0dc3` spin at `03:8ece` also explains why the treasury never moves in
an unattended replay: the routine parks there until the budget dialog is
dismissed, so a headless run never reaches the arithmetic. `$0b9d` was
written zero times across a full simulated year.

## The `$c5` reason-code dispatch, `01:897f`

```
01:897f  LDA $c5 ; REP #$10 ; ASL A ; TAX ; JSR ($88ef,X)
```

12 word entries at `01:88ef`, bounded by `01:8907` being its own node. What
each handler *is* is only partly established; the table below separates what
was read from the code from what is confirmed.

| `$c5` | handler | what it does |
|---|---|---|
| 0 | `01:8d25` | bare `RTS` — **idle / no-op state** |
| 1 | `01:8d26` | early-out on `$01f5`, then `JSR $8aa8` and `$01c1` — cursor sprite + direction dispatch (previously established) |
| 2 | `01:8dce` | branches on `$01d7`, clears `$01ff` |
| 3 | `01:8e28` | clears `$0249`, then a second dispatch on `$020d` (`ASL A ; TAX`) |
| 4 | `01:8e3d` | `JSR $8e9b` with a carry result, `JSR $b42a`, also reads `$020d` |
| 5 | `01:9d6b` | `JSR $b143`, then `LDA #$0000 ; COP #$00` — **waits for vblank** via COP service 0 |
| 6 | `01:9f2d` | clears `$0379`, branches on `$d7 == 2` |
| 7 | `01:c529` | early-out on `$01f5`, then reads `$c9` |
| 8 | `01:93a8` | copies `$0111` -> `$0117` and `$03fa` -> `$03fc` (double-buffered UI state) |
| 9 | `01:93a4` | `JSR $a640` then `RTS` — a one-line wrapper that falls into 8's neighbourhood |
| 10 | `01:940f` | **the annual budget dialog** — see below |
| 11 | `01:94e6` | `JSR $9790`, `JSR $9c9b`, clears `$0383` |

`$020d` is a sub-selector shared by reasons 3 and 4.

### Reason 10 closes the budget handshake

The annual budget at `03:8ec8` sets `$0dc3 = 1` and then spins:

```
03:8ec8  LDA #$0001 ; STA $0dc3
03:8ece  LDA $0dc3 ; BNE $8ece      ; parks here until the UI clears it
```

Reason code 10 is the other half. `01:9419` reads `$0dc3` and tests its sign
(`BPL`), gates on `$d7`, `$0195` and `$01d7`, and on the accept path clears it:

```
01:945e  STZ $0dc3
```

`STZ $0dc3` also appears at `02:a311`, `02:a3e5` and `03:c7ba`; `03:8ebd` and
`03:8ecb` are the two stores that raise it. This is why the treasury never
moves in an unattended replay — nothing dismisses the dialog, so `03:8ece`
never releases and the arithmetic after it is never reached.

## `$0195` — the four in-game option toggles

The options screen keeps all four settings as bits of one word at `$0195`,
flipped by the `EOR` at `01:a9c1` (inside `01:a97c`, one of the five `$01df`
UI state handlers). Each bit is tested with its own mask:

| bit | mask | option | test sites | confidence |
|---|---|---|---|---|
| 0 | `$0001` | **auto bulldozing** | `01:bab4`, `01:bad4` | **confirmed** |
| 1 | `$0002` | **auto budget** | `01:9422`, `03:8ec0` | **confirmed** |
| 2 | `$0004` | **auto goto** | `01:8b82`, `01:8c96` | confirmed by elimination |
| 3 | `$0008` | **music on/off** | `00:8087`, `00:c8b1` | strong |

The bit order is the order the options appear on the menu page.

Confirmed against save states captured with known settings:

| state | settings as set in-game | `$0195` |
|---|---|---|
| bulldoze on, budget off, goto on, music on | | `$000d` = `1101` |
| bulldoze off, budget on, goto off, music off | | `$0002` = `0010` |
| **then bulldozing turned back on**, budget still on | | `$0003` = `0011` |
| budget turned off, bulldozing left on | | `$0001` = `0001` |

Turning bulldozing on moved `$0002` -> `$0003`, so **bit 0 is auto
bulldozing**; turning auto budget off moved `$0003` -> `$0001`, so **bit 1 is
auto budget**. Goto and music were only ever changed together in this set, so
bits 2 and 3 are not separated by the states alone — but bit 3 is read in
bank 00's audio setup in 8-bit mode and feeds an `#$81` command byte, which
makes music bit 3 and auto goto bit 2 by elimination, matching the menu
order.

"Auto goto" teleports the view to the event — a traffic jam, for instance —
which is consistent with bit 2's site at `01:8b8a` selecting view mode
`#$0009` in place of `#$00ff`.

Bit 0 sits in the build path and gates on the tile index: `01:babd` compares
against `#$002e` = 46, so the bit permits building over tile classes below
that — terrain, parks, forest, rubble. Bit 2 selects `#$0009` instead of
`#$00ff` as a view/overlay mode (`01:8b8a`). Bit 3 is read in 8-bit mode
during bank 00's audio setup, choosing a value that is then passed with an
`#$81` command byte.

Only bit 1 has been confirmed dynamically; the other three are read from
context. Toggling each option in play and reading `$0195` would settle all
four in one session.

### Auto budget, exactly

```
03:8eba  LDA #$ffff ; STA $0dc3        ; default: negative
03:8ec0  LDA $0195 ; AND #$0002 ; BEQ $8ece
03:8ec8  LDA #$0001 ; STA $0dc3        ; auto budget: positive
03:8ece  LDA $0dc3 ; BNE $8ece         ; spin either way
```

and in reason code 10:

```
01:9419  LDA $0dc3 ; BPL $9467         ; positive -> skip the dialog
...
01:9467  JSL $02a3dc ; JSL $02a64d ; BRA $945e
01:945e  STZ $0dc3                     ; both paths land here
```

So the sign of `$0dc3` is the channel: `$ffff` means "ask the player" and `1`
means "allocate automatically". The manual path shows the dialog via
`COP #$00` service 3 at `01:945a`; the automatic path runs two bank-02
routines and falls into the same clear.

### What was observed with auto budget forced on

Freezing `$0195` to `$02` and replaying savestate 5 for 12,505 frames does
reach the annual budget: `$0dc3` is raised at `03:8ebd`/`03:8ecb`, the spin
releases when `02:a3e8` clears it, and the income terms are computed —
`$0dc9` (tax) written from `03:8eb2`, and **`$0dd9` = 100**, i.e. exactly one
gift building paying 100 a year, which is the reported casino payout observed
live rather than read from the ROM.

That first attempt did **not** reach the treasury update, because freezing
`$0195` to `$02` also forces the other three options off, which is not a
state the game ever produces.

### Verified end to end

Replaying a save state captured in **December with auto budget genuinely
enabled** rolls the year over and runs the whole thing unattended:

| term | address | value |
|---|---|---|
| tax income | `$0dc9` | 151 |
| gift income | `$0dd9` | 0 |
| outgoing 1 | `$0dcd` | 88 |
| outgoing 2 | `$0dcf` | 100 |
| outgoing 3 | `$0dd1` | 0 |
| net | `$0bc1` | `$FFDB` = **-37** |
| treasury | `$0b9d`/`$0b9f` | 2994 -> **2957** |

`151 + 0 - (88 + 100 + 0) = -37`, `$0bc1` holds -37 as a signed word, and the
treasury moves by exactly that. `$0b9d` was written once, from `03:8F1F` —
the store at `03:8f1c` that had until now only been read from the ROM. The
model is confirmed against a running machine.

The three outgoing line items are written by **`02:a65f`, `02:a665` and
`02:a66b`**, inside the `JSL $02a64d` that reason code 10 calls on the
automatic path. So bank 02 holds the funding allocator, and `$0dc3` was
cleared from `01:9461` as predicted.

### The loan instalment, verified

A second state — December 1903, auto budget on, **loan outstanding** — closes
the last term. Replaying it across the year rollover:

| term | address | value |
|---|---|---|
| tax income | `$0dc9` | 604 |
| gift income | `$0dd9` | 0 |
| outgoings | `$0dcd`/`$0dcf`/`$0dd1` | 88 + 100 + 0 = 188 |
| loan instalment | — | 500 |
| net | `$0bc1` | `$FFAC` = **-84** |
| treasury | `$0b9d`/`$0b9f` | 10196 -> **10112** |
| loan counter | `$0b1d` | 21 -> **20** |

`604 + 0 - 188 - 500 = -84`. `$0b1d` was written exactly once, from
`03:8EDE` — the `DEC $0b1d` at `03:8edb` — so one instalment is charged and
one repayment retired per year, as read.

The complete annual equation is therefore

```
treasury += (tax + gift) - (out1 + out2 + out3) - (500 if $0b1d != 0)
$0b1d    -= 1 while nonzero
```

with the treasury then clamped to `$000F423F` = 999,999.

### `$0193` — the game speed

The third settings word, edited by UI page 0 (`01:a886`). Two save states
identical but for the speed setting differ in exactly this byte:

| in-game speed | `$0193` |
|---|---|
| 3/3 | 0 |
| 1/3 | 2 |

so the field counts *down* from fastest; 2/3 = 1 and the menu's 0/3 = 3 follow
by implication but were not captured. `$79`, the menu selection byte, mirrors
it while the page is open.

Both states also tick **zero times in 2,400 frames**, which confirms
separately that the modal handler pauses the simulation while a settings page
is open — the `COP`/poll loop at `01:a886` never returns to the tick.

## The `$01df` UI state machine — five menu handlers

`$01df` selects through two parallel tables, `01:9d1a` (called) and
`01:9d3a` (jumped), at `01:a8e9` and `01:a8f8`. The five handlers share one
shape — a modal loop that waits a frame and polls input:

```
LDA #$0000 ; COP #$00      ; service 0, wait for vblank
JSR $ae26                  ; sample
JSR $aecc                  ; handle; returns carry set when done
BCC <loop>
```

| `$01df` | handler | what it edits |
|---|---|---|
| 0 | `01:a886` | reads `$0193`, calls `$a918` first |
| 1 | `01:a97c` | **the options screen** — reads `$0195` |
| 2 | `01:aa39` | reads `$0197`, writes `$79` |
| 3 | `01:aad5` | plain modal loop, then `CMP #$0008` |
| 4 | `01:ad54` | plain modal loop |

`$0193`, `$0195` and `$0197` are three parallel settings words, one per page.

### `$0195` really does hold exactly four option bits

`01:a97c` renders the options page with

```
01:a97e  LDA $0195      (8-bit)
01:a981  ASL A ; ASL A ; ASL A ; ASL A
01:a985  XBA
```

Four shifts then `XBA` lifts **bits 0-3** into the high byte for the menu
renderer, so the option nibble is exactly four bits wide. That is an
independent confirmation that the four masks found elsewhere (`$0001`,
`$0002`, `$0004`, `$0008`) are the complete set, and matches the four options
the game actually offers.

`01:aa39` does the same with two shifts on `$0197`, so that page carries a
two-bit field, and stores the result to `$79` — the same selection byte the
scenario-unlock work writes.

## `03:a553` — the map cell-pattern rewriter

Found empirically: it is in the 128 addresses that first executed in a session
where a tornado was allowed to run its course, against a union of eight
recorded sessions. 109 of those 128 were in bank 03.

```
03:a553  LDY #$0000
03:a556  LDA $00 ; CLC ; ADC $a6e2,Y ; TAX      ; cell + signed offset
03:a55d  LDA $7f0200,X ; CMP $a6f0,Y ; BNE $a585 ; must match the pattern
03:a566  INY ; INY ; CPY #$000e ; BNE $a556      ; seven cells
03:a56d  <second loop>
03:a577  LDA $a6fe,Y ; STA $7f0200,X             ; write the replacement
03:a585  LDA #$0001 ; RTS
```

Two passes over three parallel 7-entry word tables: **verify all seven cells,
then rewrite all seven**. Nothing is written unless the whole shape matches,
which is what makes it a structure transform rather than a per-tile edit.

| table | contents |
|---|---|
| `$a6e2` offsets | `fe1c fe1e ff0e fffe 00ee 01de 01dc` |
| `$a6f0` expected | `035c 035d 0001 0355 0001 035b 035a` |
| `$a6fe` replacement | `0001 0031 0031 0031 0031 0031 0001` |

The map is at **`$7F0200`**, 120 cells per row at 2 bytes each = 240 bytes per
row, which agrees with the 120x100 layout in `REFERENCE_map_format.md` and
with the address the post-load power fix pokes. The offsets are signed and
decode to a coherent shape:

| offset | rows | cells |
|---|---|---|
| `$fe1c` = -484 | -2 | -2 |
| `$fe1e` = -482 | -2 | -1 |
| `$ff0e` = -242 | -1 | -1 |
| `$fffe` = -2 | 0 | -1 |
| `$00ee` = +238 | +1 | -1 |
| `$01de` = +478 | +2 | -1 |
| `$01dc` = +476 | +2 | -2 |

A five-row vertical strip. The expected tiles are all in the `$03xx` range —
above the industrial zone base of 513, so special/gift structures, the same
band as the buildings in the annual-income ladder — and they are replaced with
`$0001` and `$0031`, i.e. the structure is levelled.

Immediately after, `03:a589` shows the trigger shape:

```
03:a589  CMP #$0354 ; BNE                       ; only for this tile
03:a58e  JSR $907e ; AND #$0003 ; BNE $a585      ; 1-in-4 random
03:a596  JSR $a70c ; CMP #$0015 ; BCC $a585      ; threshold 21
03:a59e  <another offset/expected/replacement triple at $a6b8/$a6c6/...>
```

so `03:907e` is a random source and `03:a70c` yields a value tested against
21. Several such triples sit consecutively in `$a6b8`-`$a70c`.

### Two arms, selected by tile id

The caution above turned out to be right: this is a **table-driven** rewriter
with one arm per structure tile, not a tornado routine. Both arms have the
identical shape —

```
03:a53b  LDA $0b89 ; CMP #$0355 ; BNE $a589     ; arm for tile $0355
03:a543  JSR $907e ; AND #$0003 ; BNE           ; 1-in-4 draw
03:a54b  JSR $a70c ; CMP #$0015 ; BCC           ; threshold 21
03:a553  <verify 7 / rewrite 7 against $a6e2/$a6f0/$a6fe>

03:a589  CMP #$0354 ; BNE                       ; arm for tile $0354
03:a58e  JSR $907e ; AND #$0003 ; BNE
03:a596  JSR $a70c ; CMP #$0015 ; BCC
03:a59e  <verify 7 / rewrite 7 against $a6b8/$a6c6/$a6d4>
```

so `$0b89` — the current tile index, the same variable the zone-tally helpers
decode — picks the pattern. The two triples are 42 bytes each, laid out
consecutively:

| tile | offsets | expected | replacement |
|---|---|---|---|
| `$0354` | `$a6b8` `ff0a fffa fffc fffe 0000 0002 ff12` | `$a6c6` `0356 0357 0001 0354 0001 0359 0358` | `$a6d4` `0001 0030 0030 0030 0030 0030 0001` |
| `$0355` | `$a6e2` `fe1c fe1e ff0e fffe 00ee 01de 01dc` | `$a6f0` `035c 035d 0001 0355 0001 035b 035a` | `$a6fe` `0001 0031 0031 0031 0031 0031 0001` |

The `$0354` offsets are a compact horizontal cluster; the `$0355` offsets span
five rows vertically. Both keep two `$0001` cells as anchors and level the
other five to `$0030` or `$0031`. That reads as the two orientations of one
multi-tile structure, each with its own rubble tile.

This matches the reported behaviour that **the monster does the same thing as
the tornado with different tiles** — the event picks the tile, the tile picks
the table, and the rewriter is shared. It also means enumerating the rest of
the triples would enumerate the destructible structures directly.

### The other map writers during a disaster

Attributing every map-cell write during a tornado replay:

| writer | cells |
|---|---|
| `03:B191` | 19,826 |
| `03:A53A` | 3,474 |
| `03:82F3` | 562 |
| `03:99B5` | 126 |
| `03:84EA` | 12 |

`03:a536` (recorded as `03:A53A`, the instruction after) is a separate,
narrower edit — `AND #$ff0f ; ORA $0b41 ; STA $7f01fe,X` — rewriting the low
nibble of a cell's attribute byte from `$0b41` rather than replacing the cell.

### Determinism

Reloading a save state reproduces the disaster exactly — same location, same
damage — confirmed in play. The recompilation is deterministic on this path,
which is what makes these states usable as regression fixtures.

**The AOT tier is verified on this code.** All four tornado states replayed
900 frames on both tiers give **byte-identical 128 KB WRAM**. That extends the
COP and inline-argument verification onto code that had never executed in any
earlier recording.

### A replayable disaster dataset

Unlike the earlier six, save states captured *during* a running tornado do
advance under headless replay (tick 11 -> 12, 20 -> 22, 33 -> 35 over 900
frames), so a disaster in progress can be stepped and diffed. The earlier
"armed but not yet fired" states all sat in `$01df = 2` and never ticked.

## The monster step, `03:bb6a`, and what it drags in

Isolated by difference: a session in which the monster rampaged executed 36
addresses that nine sessions — including the tornado run — never had. Only two
regions, `03:bb6a-bbb8` and `03:b92e-b93c`.

### The per-tick gate, `03:b92e`

```
03:b92e  JSR $907e ; AND #$0007 ; CMP #$0002 ; BCS $b93e
03:b939  JSR $bb6a                 ; two chances in eight
```

### The step itself

```
03:bb6c  LDA #$0014 ; STA $00
03:bb71  JSR $bc9f ; TAY           ; pick a random map cell, tile id -> Y
03:bb75  LDA $84eb,Y ; AND #$0001 ; BNE      ; per-tile property table, bit 0 = skip
03:bb7d  CPY #$0088 ; BCC          ; only tiles >= $88
03:bb82  LDA #$007f ; STA $7f0200,X          ; stamp tile $7F over the target
03:bb8b  LDA $04 ; STA $0400 ; LDA $05 ; STA $0402   ; remember where
03:bb97  INC $03fe
03:bb9a  LDA #$0020 ; JSR $be04    ; post event $20
03:bba0  LDA #$000b ; JSR $c42a    ; allocate entity type $0B
03:bba6  INC $0c9f
03:bbab  LDA #$21 ; STA $0006
```

### Four things this identifies

**`03:9035` is a bounded random number generator**, not the smoothing window
an earlier note guessed at. `03:bc9f` is simply "pick a random cell":

```
03:bc9f  LDA #$0077 ; JSR $9035 ; STA $04    ; x in 0..119
03:bca7  LDA #$0063 ; JSR $9035 ; STA $05    ; y in 0..99
03:bcaf  LDA $04 ; JSR $849e                 ; (x,y) -> cell index
```

`#$0077` = 119 and `#$0063` = 99 are exactly the 120x100 map bounds, which
also confirms `03:849e` as the coordinate-to-cell-index helper.

**`$0ced` is the moving-object table.** `03:c42a` scans it for a free slot:

```
03:c42a  PHA ; LDX #$0000
03:c430  LDA $0ced,X ; CMP #$ffff ; BEQ <found>
03:c438  TXA ; CLC ; ADC #$0006 ; TAX ; CPX #$003c ; BNE
```

stride 6, limit `$3c` = 60, free marker `$ffff` — **10 slots of 6 bytes**. The
monster is **entity type `$0B`**. This is the entity table listed as an open
thread; the identities can now be read off from each caller's type byte.

**`03:be04` is a one-shot event post**, guarded so only one is pending:

```
03:be04  LDY $0395 ; BNE $be11 ; STA $0397 ; INC $0395
```

The monster posts event `$0020`.

**`$84eb` is a per-tile property table**, indexed by tile id, with bit 0
meaning "not a valid target".

### The sound is still unattributed

The reported difference — the monster makes sounds, the tornado does not —
looked like it would fall out of `JSR $c42a`, but that is the entity
allocator, and `03:be04` is an event post. Neither touches the APU directly.
The sound most likely follows from the entity or the event downstream rather
than from the step, and is not established here.

## The real disaster dispatcher: `03:b8ae` on `$0197`

Not `$0199`. `03:b8ae` is a six-arm ladder over **`$0197`**, each arm calling a
handler and then masking its own bit off:

| bit | mask | handler | clears with |
|---|---|---|---|
| 0 | `$0001` | `03:bbb9` | `$fe` |
| 1 | `$0002` | `03:bc0b` | `$fd` |
| 2 | `$0004` | `03:b9cd` | `$fb` |
| 3 | `$0008` | `03:b9db` | `$f7` |
| 4 | `$0010` | `03:baf5` | `$ef` |
| 5 | `$0020` | `03:ba47` | `$df` |

```
03:b8ae  LDA $0197 ; BEQ $b916          ; nothing pending -> ordinary path
03:b8b3  AND #$0001 ; BEQ ; JSR $bbb9 ; LDA #$00fe ; BRA $b90e
         ... one arm per bit ...
03:b90e  AND $0197 ; STA $0197          ; clear the bit just serviced
```

**`$0197` is also the third settings word** — the page `01:aa39` renders. That
page is the disaster-selection menu: choosing a disaster sets its bit, and
this ladder fires the handler and clears it. An earlier note called `$0197` a
"two-bit field" because `01:aa3b` shifts it left twice before `XBA`; that was
wrong. Two shifts then `XBA` lifts **six** bits into the high byte, exactly as
four shifts lift the four option bits of `$0195`.

### Which are attributed, and how much is still dark

Per-handler coverage over nine recorded sessions, with the two sessions that
deliberately ran one disaster each broken out:

| bit | handler | body executed | tornado session | monster session |
|---|---|---|---|---|
| 0 | `03:bbb9` | 31 / 82 | — | — |
| 1 | `03:bc0b` | 4 / 8 | — | 4 |
| 2 | `03:b9cd` | 4 / 14 | — | — |
| 3 | `03:b9db` | 41 / 108 | **41** | — |
| 4 | `03:baf5` | 47 / 117 | — | — |
| 5 | `03:ba47` | 71 / 174 | 19 | **63** |

So **bit 3 is the tornado and bit 5 is the monster**, attributed because those
sessions ran one disaster deliberately. Bits 0, 2 and 4 have partial coverage
from ordinary play — disasters fire on their own — but nothing says which is
which. Roughly **60% of the six handlers' code has still never executed.**

Attributing the rest needs one session per disaster, each triggering a single
type, so that session's newly executed addresses name its bit the way the
tornado and monster runs did.

### Reading the unattributed arms

> **SUPERSEDED — bit 2 is the plane crash, not the meltdown.** See "Bit 2 is
> the plane crash" further down: `$0a8d` counts **airports**, not nuclear
> plants, and the meltdown is not in the `$0197` ladder at all. The paragraph
> below is kept because its *mechanism* reading is right — the arm really is
> just a guard on `$0a8d`, which is why it shows so few covered bytes — only
> the identification was wrong. Left in place rather than deleted so the
> correction stays visible; the table further down carries the right answer.

**Bit 2 (`03:b9cd`) is the nuclear meltdown.** Its whole body is a guard:

```
03:b9cf  LDA $0a8d ; BEQ $b9da ; ... ; RTS
```

`$0a8d` is the nuclear-plant count — incremented at `03:ac08` when one is
built, decremented at `03:ce47` (never executed in any recording). Measured
across the save states: it is **0 in the Boston state with the plants deleted
and 1 in the Boston states that still have one**, which is exactly the
reported behaviour that removing every nuclear plant removes the disaster.
This is why the arm shows only 4 of 14 bytes covered — in most recordings it
takes the early exit.

**Bit 0 (`03:bbb9`) is a roaming destroyer, like the monster but pickier.**
Same shape as `03:bb6a` — random cell, stamp tile `$7F` — with two
differences: the strength parameter is `#$0028` (40) rather than `#$0014`
(20), and the target must have **property bit 2** set in `$84eb` as well as
not having bit 0:

```
03:bbc4  LDA $84eb,Y ; AND #$0001 ; BNE <skip>    ; never a target
03:bbcc  LDA $84eb,Y ; AND #$0004 ; BEQ <skip>    ; must have this property
```

A per-tile "may catch fire" flag is the natural reading, which would make this
the fire, but that is inference from the shape of the test, not evidence.

**Bit 4 (`03:baf5`) is the earthquake** — confirmed by a session that
triggered only that: 47 of its 117 bytes ran, matching the union exactly,
while bits 0, 2 and 3 stayed at zero. It starts at a stored location rather
than a random one, so `$0ba9`/`$0baa` is the **epicentre**:

```
03:baf7  LDA $0ba9 ; AND #$00ff ; STA $0400
03:bb00  LDA $0baa ; AND #$00ff ; STA $0402       ; a remembered coordinate
03:bb0c  LDA #$000a ; JSR $be04                   ; event $0A
03:bb12  LDA #$000e ; JSR $c42a                   ; entity type $0E
03:bb18  LDA #$015e ; JSR $9035                   ; random 0..350
```

`$0ba9`/`$0baa` is written at `03:9ba0`, `03:9bb8` and `03:c859`, and holds
(56,56) and (60,50) in the captured states. An event that begins somewhere
specific rather than anywhere fits several candidates; it is not settled.

**Bit 1 (`03:bc0b`) is 148 bytes, not 8.** An earlier note here said 8 and
called it a shared tail; that was an artefact of measuring each handler's span
as the distance to the next handler *in the order I happened to list them*
rather than in address order. `03:bc0b` runs to `03:bc9f`, where the
random-cell picker starts. Corrected spans are used in the table below.

### Status

| bit | handler | span | identification |
|---|---|---|---|
| 0 | `03:bbb9` | 82 | roaming destroyer, needs tile property bit 2 — unplaced |
| 1 | `03:bc0b` | 148 | unplaced; runs in most sessions, so not disaster-specific |
| 2 | `03:b9cd` | 14 | **plane crash** — `$0a8d` is the airport count; see the correction below |
| 3 | `03:b9db` | 108 | **tornado** — confirmed by session |
| 4 | `03:baf5` | 117 | **earthquake** — confirmed by session; epicentre `$0ba9`/`$0baa`, entity `$0E` |
| 5 | `03:ba47` | 174 | **monster** — confirmed by session, entity `$0B` |

Reported but not yet placed: **flood** and the **UFO**, which appears in the
Las Vegas scenario rather than in ordinary play. Six bits for
more candidates than that means at least one reported event is not driven by
this ladder — fire spreading tile-to-tile rather than being dispatched once
would be the obvious way that happens.

## `$0b57` — difficulty, and what it actually changes

`$0b57` reads 0 or 1 across every captured save state and does not track city
size (`$0ca5`/`$0deb` do that — both 3 in an 80,000-population city where
`$0b57` is 0). It indexes two tables, in the two places difficulty is
reported to matter.

### Disaster frequency — confirmed and quantified

```
03:b91e  LDA $0b57 ; ASL A ; TAY
03:b923  LDA $b969,Y ; JSR $9035      ; random 0..N
03:b929  CMP #$0000 ; BNE <skip>      ; proceed only on a zero draw
```

`03:9035` is the bounded RNG, so the per-tick chance is **1 in (N+1)**. The
table at `03:b969` holds three sane entries before running into unrelated
bytes, which is what fixes its length at three:

| `$0b57` | N | chance per tick |
|---|---|---|
| 0 | 4800 | 1 in 4801 |
| 1 | 2400 | 1 in 2401 |
| 2 | 1200 | 1 in 1201 |

So each difficulty step **doubles** the disaster rate, 4x from easiest to
hardest. That confirms the reported behaviour that medium and hard throw far
more disasters than easy, and puts a number on it.

### The tax claim does not hold up as stated

The other difficulty-indexed table is at `03:8fe8`, used in the annual budget:

```
03:8e3c  LDA $0e17 ; ASL A ; ADC $0e15 ; STA $00
03:8e45  LDA $0b57 ; ASL A ; TAX ; LDA $8fe8,X ; STA $04
03:8e4f  JSR $a2f5  [00 04 00]        ; multiply
```

with `$8fe8` = `00b3 00e6 0133 ...` = **179, 230, 307**. The multiplier
*increases* with difficulty, which is the opposite direction to "taxes are
lower on higher difficulty". Either this term is not the tax rate — it is fed
by `$0e15`/`$0e17` and multiplied, so it could as easily be a cost or demand
factor — or the widely repeated claim is wrong.

Worth stating plainly because the source was second-hand: the disaster half of
that claim is confirmed in the ROM, the tax half is not, and nothing here
settles which reading of `$8fe8` is right. Watching `$0dc9` (tax income) across
a year on two difficulties with an otherwise identical city would settle it.


### Per-arm coverage by single-disaster session

The attribution method, laid out so it can be checked:

| bit | handler | span | union | tornado | monster | quake | flood |
|---|---|---|---|---|---|---|---|
| 2 | `03:b9cd` | 14 | 4 | 0 | 0 | 0 | 0 |
| 3 | `03:b9db` | 108 | 41 | **41** | 0 | 0 | 0 |
| 5 | `03:ba47` | 174 | 71 | 19 | **63** | 16 | 16 |
| 4 | `03:baf5` | 117 | 47 | 0 | 0 | **47** | 0 |
| 0 | `03:bbb9` | 82 | 31 | 0 | 0 | 0 | 0 |
| 1 | `03:bc0b` | 148 | 60 | 0 | 60 | 60 | 60 |

Bits 3, 4 and 5 each light up in exactly one session, which is what makes
those three attributions solid. Bit 1 runs in three sessions of four, so it is
not disaster-specific whatever it is. **Bit 0 has never run in any of the four
single-disaster sessions** — its 31 covered bytes all come from ordinary play.

The flood session produced **no distinguishing signal at all**: no arm
exclusive to it, and zero first-time-executed addresses in the whole ROM. So
either the flood is not dispatched through this ladder, or it did not fire
during that session. The data cannot tell those apart.

### The flood is not in the ladder

Confirmed behaviourally — a flood started, spread, destroyed a building and
receded — while the same session lit **no arm exclusive to it** and executed
**zero** first-time addresses. So the flood is not dispatched through `$0197`.
Its spread-then-recede behaviour is what a tile-level cellular process looks
like: a tile type that propagates on the per-tick map scan rather than an
event serviced once and cleared.

That is the answer to why six bits cannot cover every reported disaster. At
least one of them is not an event at all.

### What the flood *did* prove: the rewriter is shared demolition

Attributing every map-cell write per session, one writer appears in the flood
session and nowhere else:

| writer | flood | quake | monster |
|---|---|---|---|
| `03:B191` | 19,256 | — | 19,826 |
| `03:A53A` | 3,864 | — | 3,474 |
| **`03:A5C9`** | **10** | 0 | 0 |

`03:a5c9` is not a third arm. It is the `INY` inside the **write loop** of the
tile-`$0354` arm at `03:a59e` — the store itself is `03:a5c5`. Its absence
everywhere else means that in every other recording the arm was entered and
the pattern never fully matched, so the rewrite pass never ran.

The flood is therefore the only recorded event that has actually completed a
`$0354` structure demolition, which matches the building destroyed during it.
It also settles the earlier caution: the cell-pattern rewriter is **shared
demolition machinery invoked by whatever damages a structure**, not a
per-disaster routine. Declining to call `03:a553` "the tornado routine" was
right, and the reason is now evidenced rather than assumed.

### Where that leaves the ladder

Bits 0 (`03:bbb9`) and 1 (`03:bc0b`) remain unattributed, and the flood is out
of the running for both. Remaining candidates are fire, the Las Vegas UFO, and
the plane crash — the last of which cannot fire without a plane, so it may
never appear in a recording at all.


## Disaster attributions, settled in play

All six arms of the `$0197` ladder are now identified, by adding the triggers
to the F10 menu and firing each one in a real session:

| bit | handler | disaster |
|---|---|---|
| 0 | `03:bbb9` | **fire** |
| 1 | `03:bc0b` | **flood** |
| 2 | `03:b9cd` | **plane crash** |
| 3 | `03:b9db` | **tornado** |
| 4 | `03:baf5` | **earthquake** |
| 5 | `03:ba47` | **monster** |

### Three corrections this forces

**Bit 2 is the plane crash, not the nuclear meltdown, so `$0a8d` counts
airports.** The earlier reading was built on a single coincidence: `$0a8d` was
0 in the Boston state and the plants had been deleted there, so the guard
`LDA $0a8d ; BEQ` looked like a nuclear-plant check. That state had no airport
either. One save state agreeing with a hypothesis is not evidence for it when
another variable explains it equally well, and the guard is real — the arm
needs an airport to have anything to crash. `03:ac08 INC $0a8d` is where one
is built.

**The flood *is* in the ladder — bit 1.** An earlier section concluded it was
not, from a session where it fired but lit no arm exclusive to it. That
reasoning was sound but the premise was thin: bit 1 shows 60 of 148 bytes in
three sessions of four, which reads as "not disaster-specific" only if floods
are rare. They are not — the arm was running in most sessions because floods
kept happening. The spread-then-recede behaviour is still real, but it is what
the handler *does* after being dispatched, not evidence against dispatch.

**Bit 0 is fire, as the tile-property reading suggested.** `03:bbb9` requires
its target to carry bit 2 in the per-tile table at `$84eb`, and the
"flammable" guess was right. This is the one inference that survived, and it
was flagged as an inference at the time.

### What still holds

The cell-pattern rewriter at `03:a553` remains shared demolition machinery,
not per-disaster code — that conclusion came from writer attribution rather
than from guessing, and nothing here disturbs it.

## Entity types in the `$0ced` table

Every `JSR $c42a` in bank 03 is preceded by `LDA #imm` giving the type it
allocates. Cross-referencing the call sites against the disaster handlers'
address ranges names most of them:

| type | call site | inside | reading |
|---|---|---|---|
| `$00` | `03:c621`, `03:c66a`, `03:c67f` | — | |
| `$01` | `03:c4c9` | — | **never executed in any recording** |
| `$07` | `03:ba77` | monster arm `03:ba47` | monster |
| `$08` | `03:bdf3` | — | |
| `$0A` | `03:ba37` | tornado arm `03:b9db` | tornado |
| `$0B` | `03:bba0`, `03:bbf2` | `03:bb6a` and fire arm `03:bbb9` | shared effect |
| `$0C` | `03:bc74` | flood arm `03:bc0b` | flood |
| `$0E` | `03:bb12` | earthquake arm `03:baf5` | earthquake |
| `$13` | `03:c3f1` | — | |
| `$14` | `03:bd34` | — | |

### This casts doubt on the "monster step" label

`03:bb6a` was written up above as the monster's step. That is now suspect.
It allocates type `$0B`, and so does the **fire** arm at `03:bbf2` — while the
monster arm proper allocates `$07`. `03:bb6a` is also reached from `03:b939`
under a two-in-eight gate in the `$003e` path rather than from the ladder, and
it is near-identical in shape to the fire handler `03:bbb9` (random cell,
`$84eb` property test, stamp tile `$7F`), differing mainly in its strength
constant.

The likelier reading is that `$0B` is a shared damage/effect entity and
`03:bb6a` is a spread step that fire and the monster both drive, not the
monster specifically. It was attributed by difference against sessions, which
is weaker than attribution by containment — the session evidence only shows
that it ran when the monster did, and the monster and fire were not separated
at the time.

### Resolved: `03:bb6a` is not fire's spread step

The fire-alone session settles it, in favour of the original label. Fire was
triggered and ran — `03:bbb9` executed — while **`03:bb6a` did not execute at
all**, nor did the monster arm `03:ba47`:

| probe | fire-alone session | ever |
|---|---|---|
| `03:bbb9` fire arm | **yes** | yes |
| `03:bb6a` | **no** | yes |
| `03:ba47` monster arm | no | yes |

So `03:bb6a` is not driven by fire. It ran in the monster session and not in a
session where fire ran without a monster, which is now a genuine dissociation
rather than a co-occurrence. The shared type-`$0B` allocation is just both
paths using the same damage/effect entity, which is what `$0B` appearing at
two call sites meant all along.

The doubt was worth raising — the original attribution was by difference and
fire had never been isolated — but the label stands.

## The Las Vegas UFO

Captured with three save states bracketing an attack — before, during, and at
the popup — in a confirmed Las Vegas run (`$0040` = 6, year 2097).

**It is not a `$0197` disaster.** `$0197` reads `$0000` in all three states,
so the six-arm ladder is not involved, which is consistent with all six of its
arms already being accounted for.

**It is entity type `$14`.** Reading the `$0ced` table (10 slots of 6 bytes,
type in the first word) across the three states:

| state | slot 0 | slot 1 |
|---|---|---|
| before | `$0000` | free |
| during | `$0000` | free |
| **popup** | `$0000` | **`$0014`** |

and `03:bd34` — the only `LDA #$0014 ; JSR $c42a` in the bank — executed in
that session:

```
03:bd2b  STZ $0af1
03:bd2e  LDA #$0030 ; JSR $be04     ; post event $30
03:bd34  LDA #$0014 ; JSR $c42a     ; allocate entity type $14
03:bd3a  RTS
```

So the attack is an event post plus an entity allocation, the same shape the
monster and earthquake arms use, but reached from outside the ladder. No
`JSR`/`JSL` anywhere in the ROM targets `03:bd00-bd3a`, so `03:bd2b` is
entered by fall-through or a branch from earlier in the routine; that entry
point is not yet identified.

**Worth noting: the session executed zero first-time addresses.** The UFO path
was already covered by ordinary play in earlier recordings — it had simply
never been *attributed*. That is the same pattern as the earthquake session,
and it is the reason coverage growth stopped being a useful signal several
sessions ago: what is left is naming code that already runs, not finding code
that does not.

### `$0af1` is the "controls disabled" flag, and the spawn is reproducible

The attack replays deterministically from a save state taken before it: run
the "before" state forward and entity `$14` appears between frames 1800 and
3600, with no input at all. That makes it a fixture, not just an observation.

Watching `$0af1` across the replay shows the two phases. Write attribution
names the two instructions responsible — the flag is **set at `03:bce9`** and
**cleared at `03:bd2b`**:

```
03:bcda  LDA $bd4f,Y ; STA $0af7      ; parameter from a table at $bd4f
03:bce0  STZ $0af5
03:bce3  LDA #$8000
03:bce6  STA $0aef
03:bce9  STA $0af1                    ; approach: flag set
03:bcec  INY ; INY ; BRA $bcc5        ; loop
...
03:bd2b  STZ $0af1                    ; arrival: flag cleared
03:bd2e  LDA #$0030 ; JSR $be04       ; post event $30
03:bd34  LDA #$0014 ; JSR $c42a       ; spawn entity $14
```

`$0aef` gets `$8000` here too and is then rewritten every frame from
`00:c71f` — 1,111 writes in 1,200 frames — so it is an animation or countdown
running through the approach, not a flag.

> **Correction.** An earlier revision of this section quoted `03:bd1d`-`bd28`
> for the flag-set, an almost identical block (`LDA #$8000 ; STA $0aef ;
> STA $0af1 ; INY ; BRA`) forty bytes further on. **Those bytes never
> execute.** `dis_mx.py` had said so — none of that range carried the `*`
> execution mark while `03:bd2b` onward did — and the listing was read past
> without checking. The tool exists precisely to catch a plausible-looking
> decode of code that never runs, and it worked; the reader did not. Two
> near-duplicate blocks presumably serve two event kinds, only one of which
> is the UFO.

| frame | `$0af1` | entity slot 1 |
|---|---|---|
| start | 0 | free |
| 300-1800 | **`$8000`** | free |
| 3600+ | 0 | **`$14`** |

`$0af1` is held at `$8000` for the whole approach and cleared as the UFO
spawns — which matches the reported behaviour that **the controls go dead
just before the attack**, and explains `01:898a`'s `LDA $0af1 ; BNE $89e4`
in the reason-code path: nonzero, and the normal input handling is skipped.

`03:bd2b` is not a routine entry. It is the fall-out of a loop that branches
back to `03:bcf0` on the other path, and a PC-history trace catches it
arriving from bank 00 (`00:92b0`), i.e. after a call out and back. The loop's
own entry is still unidentified.

## `03:b16c` — the power-bit application pass

This is the routine behind `03:B191`, which dominates map-cell writes in every
recorded session (19,826 cells during a tornado replay, 19,256 during a
flood). It is not disaster code at all — it is how the power bitmap becomes
per-cell state:

```
03:b16c  TXA ; AND #$001e ; BNE $b17a     ; fetch a new bitmap word every 16 cells
03:b172  LDA $a598,Y ; XBA ; STA $00
03:b17a  LDA #$0000 ; STA $00023f
03:b181  LDA $0200,X ; AND #$7fff         ; map cell, bit 15 cleared
03:b187  ASL $00                          ; shift the bitmap word
03:b189  BCC $b18e ; ORA #$8000           ; carry set -> cell bit 15 = powered
03:b18e  STA $0200,X
03:b191  LDA $00023f ; BEQ $b19b ; ROR $00 ; BRA $b17a
03:b19b  INX ; INX
```

One bitmap bit per cell, walked 16 cells to a word, ORed into **bit 15** of
each map cell. That is exactly the bit the post-load power fix pokes —
`g_ram[0x10201 + i*2] |= 0x80` is `$7F0200 + i*2 + 1` bit 7, i.e. cell bit 15
— so the fix and this pass write the same thing, one from the host and one
from the guest. The bitmap source `$a598,Y` is the `$7FA598` array whose
absence from the SRAM save block caused the power dropout on load.

It also explains why this address swamps every write-attribution table: it
touches all 12,000 cells on every pass, so any per-disaster writer shows up
against a background two orders of magnitude larger.

### The UFO approach loop is reached from here

A PC-history trace catches `03:bcc5` — the loop whose `03:bce9` sets the
approach flag — arriving from this region (`03:b16c`-`03:b1a0`). So the Las
Vegas attack is driven off the per-cell simulation pass rather than a timer.
The exact edge is not established: the trace's last recorded PC before
`03:bcc5` is `03:b195`, whose `BEQ $b19b` does not lead there, so either the
history is not contiguous across the transition or the entry is via a path the
14-entry buffer did not capture. Worth a longer history before asserting it.


### `$01e7` — the View / extra-mode unlock bitfield

A bitfield gating two entries of the `$01fb` UI menu. The menu index is
computed at `01:AAEE` as `selector - 8`, giving 0-7, and two of those eight
are locked:

```
01:AAF9  CMP #$04 ; BNE $ab08
01:AAFE  LDA $01e7 ; AND #$01 ; BEQ $ab2d     ; index 4 needs bit 0
01:AB08  CMP #$07 ; BNE $ab15
01:AB0D  LDA $01e7 ; AND #$02 ; BEQ $ab2d     ; index 7 needs bit 1
```

`$ab2d` is the rejection path -- it jumps straight back to `01:AAD5`, so a
locked entry silently does nothing.

**Index 7 is the View mode** (the tilted Mode 7 map; cf. the `00:C0FB` view
fix this host patches). Identified from play, not statically.

Three ways the bit gets set, and the difference between them explains a
surprise:

| site | what |
|---|---|
| `01:BFF2` | `LDA $01e7 ; ORA #$02` when `$0397 == $0c` — **message/event ID 12** |
| `01:BFD1` | `ORA #$01` when `$0397 == $1f` (31) — unlocks index 4 |
| `03:C687` | `LDA #$0002 ; STA $01e7` — sets View outright, no condition |
| `03:CA3B` | `STA $01e7` from SRAM `$700064,X` — the unlock persists per saved city |

`$0397` is the pending message ID, queued at `03:BE04`
(`LDY $0395 ; BNE ; STA $0397 ; INC $0395`).

**So View is unlocked by the milestone *message*, not by the population
value.** In play it arrives at 50,000 people, but cheating the population
counter does not unlock it, because nothing queues message 12 — confirmed
empirically: the population was cheated past the threshold and View stayed
locked. Scenarios get it free instead, via `03:C687` setting it outright,
which is why View is available in every scenario from the start.

Measured across this repo's save states: `$01e7 = 0x0002` in savestates 3-6
(scenarios, View unlocked), `0x0000` in 0/1/2/7/8 (practice/free play),
`0x0001` in 9.

## FOUND: `$0c0d` is the per-scenario event countdown

The mechanism behind both scenario-scoped events, and it is one routine.

`03:ce8b` seeds `$0c0d` per scenario from the table at `03:cec9` (verified from
the code, not inferred):

| idx | scenario | year | `$0c0d` seed |
|---|---|---|---|
| 0 | San Francisco | 1906 | 10 |
| 1 | Bern | 1965 | 20 |
| 2 | Tokyo | 1961 | 5 |
| 3 | Detroit | 1972 | 3 |
| 4 | **Boston** | 2010 | **1** |
| 5 | Rio | 2047 | 258 |
| 6 | **Las Vegas** | 2096 | **384** |
| 7 | free play | 1991 | 10 |

The year column is the existing `03:ced9` table, and it pins the index mapping
independently.

### `03:b96f` dispatches on it, in two modes

**Mode 1 -- the countdown reaches 1: fire the scenario's signature disaster.**

```
03:b96f  LDA $0c0d ; CMP #$0001 ; BNE $b997
03:b977  LDY $0040
03:b97a  BNE  +           ; idx 0 San Francisco -> JSR $baf5  EARTHQUAKE
03:b981  CPY #$0002       ; idx 2 Tokyo         -> JSR $ba47  MONSTER
03:b98b  CPY #$0004       ; idx 4 Boston        -> JSR $bac1  <-- NOT a ladder arm
```

**Mode 2 -- otherwise, every 16th tick: the recurring events.**

```
03:b997  LDA $0c0d ; BEQ done
03:b99c  AND #$000f ; BNE +      ; only when the low nibble is 0
03:b9a1  LDY $0040
03:b9a4  CPY #$0005       ; idx 5 Rio        -> JSR $bc0b  FLOOD
03:b9ae  CPY #$0006       ; idx 6 Las Vegas  -> population gate -> JSR $bcb8  UFO
03:b9c4  LDY $0c0d ; BEQ done ; DEC $0c0d    ; tick
```

So **`03:bac1` and `03:bcb8` are a seventh and eighth handler**, outside the
six-arm `$0197` ladder entirely. That is why neither could ever be found in it.

### The meltdown: `03:bac1`

Boston seeds `$0c0d = 1`, so mode 1 fires on the **first tick** -- which is
exactly the reported behaviour, "triggered when you open up the scenario".

The handler is a full 120x100 map scan (`CMP #$0078` / `CMP #$0064`) for tile
value **`$027c`**, and on the first match it passes the cell coordinate through
`$0b85` and jumps to `03:bd61`.

`$027c` is one of the two self-powered tiles from `03:b0f8` -- the two power
plant types, where "which of the pair is coal and which is nuclear" was open.
A routine that hunts down `$027c` specifically and detonates it settles it:
**`$027c` is the nuclear plant, `$028c` the coal plant.**

### The UFO: `03:bcb8`

Las Vegas seeds `$0c0d = 384`, so mode 2 fires every 16 ticks for 24 events,
gated on population (`$0ba5`/`$0ba7` vs `$1_4c08`). The handler walks a 9-entry
waypoint path from `03:bd3b`/`03:bd4f`, terminated by `$00ff`, and sets
`$0af1 = $8000` -- the "controls disabled" flag already documented below.

### Confirmed by execution

Save states 3-6 are Las Vegas (`$0040 = 6`, year 2097) sitting at
`$0c0d = 304` -- counted down 80 from 384, and **divisible by 16**, i.e. exactly
on the firing condition. Running `savestate_3` for 4,000 frames and checking the
coverage bitmap:

| site | | |
|---|---|---|
| `03:b96f` | countdown read | EXECUTED |
| `03:b99c` | `AND #$000f` every-16 gate | EXECUTED |
| `03:b9ae` | `CPY #$0006` Las Vegas arm | EXECUTED |
| `03:b9b3` | population gate | EXECUTED |
| `03:b9c1` | `JSR $bcb8` | EXECUTED |
| `03:bcb8` | UFO handler | EXECUTED |
| `03:b9c9` | `DEC $0c0d` | EXECUTED |
| `03:b990` | `JSR $bac1` meltdown call | **not executed** |
| `03:bac1` | meltdown handler | **not executed** |

The negative half is the control: from a Las Vegas state the mechanism fires the
Las Vegas arm and not the Boston one.

### The gate: `$003e == 3`

`03:b96f` is not fallen into — `03:b969` is the RNG threshold **table**
(`12c0`/`0960`/`04b0` = 4800/2400/1200, indexed by difficulty `$0b57`). The
dispatcher is a separate routine with exactly one caller:

```
03:b858  LDA $0425 ; AND #$0001 ; BEQ +     ; No-Disasters cheat -> skip all
03:b863  LDA $003e ; CMP #$0003 ; BNE +
03:b86b  JSR $b96f                          ; <- only when $003e == 3
```

`$003e == 3` is scenario mode (the win-mark setter at `03:e2ee` is gated on the
same thing). This is why save states 0/1/2/7/8/9 never reach the dispatcher at
all: they are free-play cities (years 1900-1904, `$0c0d = 0`), not scenarios.

### The meltdown, confirmed by execution

From `savestate_3` (Las Vegas, so `$003e == 3` already holds), setting
`$0040 = 4` and `$0c0d = 1` runs the whole chain:

| site | | |
|---|---|---|
| `03:b86b` | `JSR $b96f` past the `$3e` gate | EXECUTED |
| `03:b98b` | `CPY #$0004` Boston arm | EXECUTED |
| `03:b990` | `JSR $bac1` | EXECUTED |
| `03:bac1` | meltdown handler | EXECUTED |
| `03:bad1` | `CMP #$027c` nuclear tile | EXECUTED |
| `03:badd` | `JMP $bd61` **detonate** | EXECUTED |

Reaching `03:badd` means the scan **found** a `$027c` tile and detonated it.

This confirms the dispatch and the handler. It does not confirm that the Boston
*map* is what gets destroyed, since the scan ran over the Las Vegas map — but
Boston seeding `$0c0d = 1` means the same path runs there on the first tick.

### The UFO population gate

`03:b9b3` is a 32-bit compare of `($0ba7:$0ba5)` against `$0001_4c08`, so the
UFO needs a population of at least **84,488**. Measured: on a small free-play
city the Las Vegas arm is reached and the gate rejects it before `JSR $bcb8`.

### On a non-Las-Vegas map: damage, but no UFO

Reported from play: triggering the UFO on a practice or ordinary map does
damage but shows no UFO. Measured, and it is **not** a code-path difference.

| | real Las Vegas | forced on free play |
|---|---|---|
| approach loop `03:bcc5` | YES | YES |
| waypoints exhausted `03:bcd5` | YES | YES |
| arrival `03:bd2b` | YES | YES |
| event post `03:bd2e` | YES | YES |
| `LDA #$0014 ; JSR $c42a` | YES | YES |
| renderer `00:c402`-`c752`, 15 sites | YES | **YES, all 15** |

The whole sequence runs, including every site in the bank-00 code that reads
`$0aef`/`$0af1`/`$0af9`. So the logic and the drawing code both execute; what
differs is the data they draw with. The leading explanation is that the UFO
sprite tiles are not in VRAM outside its own scenario — **not established.**

Ruled out: a scenario-keyed graphics load. The only read of `$0040` outside
bank 03 is `08:a62d`, and bank 08 is compressed data — the surrounding
disassembly is `MVN`/`COP`/`WAI` nonsense, so that is a byte coincidence, not
an instruction.

> **Correction: `03:c42a` is not an entity spawner.** It writes the value
> passed in `A` to `$0ced,X`, then `$0b53` (year) and `$0b55` (month) to
> `$0cef,X`/`$0cf1,X` — a **dated message log**, ten six-byte slots, shifted
> down when full. So `LDA #$0014 ; JSR $c42a` posts a dated "UFO" news entry;
> it does not create a sprite. Earlier notes here read it as "spawn entity
> $14" / "allocate entity type $0B", and the moving-object table is a
> different range entirely (`$02cb`-`$0376`). The UFO's visible form comes
> from the approach state (`$0aef`/`$0af5`/`$0af7`/`$0af9`), not from `$c42a`.

**Next step that would settle it:** compare VRAM/OAM during the approach
between the two runs. If OAM carries the sprite but its tiles are absent, the
graphics reading is confirmed.

### Manual activation in the native build (2026-10-06)

The current trigger sets the native branch carry at 03:b9bf while its manual
UFO event is armed. Live ROM patches do not change compiled C branches, which
made the earlier NOP implementation ineffective in native releases. Population
and ROM bytes stay unchanged. The manual dispatcher also passes 03:b85e when
NO DISASTER suppresses automatic events. It returns at 03:b86e after the one
scenario event instead of processing additional random disasters that tick.

The handler retains forced Las Vegas identity throughout the whole approach
because bank 00:c3f8 compares scenario index 6 before drawing the UFO. The native
DEC 03:b9c9 acknowledges completion; only then do mode, scenario and the prior
countdown return. A real windowed native replay now shows the UFO sprite flying
over free play. Zero-population native and interpreter tests reach its approach
state with NO DISASTER enabled, without changing population, cheat or ROM data.

Manual nuclear activation finds the same first 0x27c center as the native scan
using full-width coordinates, then enters 03:bd61. All six sizes have native and
original CPU tests for demolition, radiation, the type-8 dated news entry,
no-plant behavior and scenario restoration. The real Disaster menu now adds
separately rendered temporary NUKE/UFO buttons, selected through 01:aecc and
native bit masks 6/7. It does not extend the checkbox walker into occupied OAM.

### Triggering both from the F10 menu

`MELTDOWN` and `UFO` rows, and the headless twin
`SC_SCENARIO_EVENT=<meltdown|ufo>@<frame>`.

Both set three words together — `$003e = 3`, `$0040` = 4 or 6, and `$0c0d` = 1
or 16 — then **restore `$003e`/`$0040` on the ROM's own first `DEC $0c0d`**.
Two of those words identify the city, so leaving them changed would tell the
game it is in a different scenario and corrupt the win check and the next save.
Restoring on the ROM's decrement is self-timing: simulation ticks are ~160
frames apart and vary with game speed, so a fixed frame delay would be guesswork.

Not a freeze — the values are set once and the ROM consumes them, so execution
stays on paths the game really takes.

## Meltdown and UFO are scenario-driven, not `$0197` bits

Reported from play, and it fits everything measured:

> "Meltdown is triggered when you open up the scenario, and it is more likely
> when you started a difficult game."

So the nuclear meltdown is **not** one of the six `$0197` arms. That resolves
the long-running confusion in this file, where bit 2 was first read as the
meltdown and later corrected to the plane crash — the meltdown was never in
the ladder to be found.

The same is true of the UFO, already established as entity type `$14` and
outside the `$0197` ladder. Both are scenario-scoped events:

| event | scope | mechanism |
|---|---|---|
| six `$0197` arms | any city | disaster-selection page sets a bit, `03:b8ae` services it |
| nuclear meltdown | Boston scenario | set up when the scenario is opened |
| UFO | Las Vegas scenario | entity type `$14` |

The difficulty half of the report is already quantified here: `$0b57` indexes
`03:b969`, and each step **doubles** the per-tick disaster chance (1 in 4801 /
2401 / 1201). Whether that same word also gates the scenario-scoped events, or
only the ladder's spontaneous firing, is not established — the RNG draw at
`03:b91e` is on the ladder path, so on present evidence it scales the six, and
the report's "more likely on hard" may be about those rather than the meltdown
specifically.

### Confirmed: each `$0197` bit drives its own arm

`SC_DISASTER=<bit>@<frame>` (src/main.c) sets one bit headlessly — the same
thing the F10 menu and the game's own disaster page do. One run per bit from
`savestate_9`, diffed against a no-disaster baseline, counting newly executed
addresses inside each handler body:

| bit set | new addresses in its own handler |
|---|---|
| 0 | 33 |
| 1 | 65 |
| 2 | 4 |
| 3 | 41 |
| 4 | 46 |
| 5 | 46 |

Every bit lights its own arm, and bits 3 and 5 reproduce the tornado and
monster attributions that came from hand-played sessions — so the method is
validated against known answers before being trusted on the unknown ones.

Bit 2's mere 4 addresses are the `$0a8d` guard bailing out: `savestate_9` has
no airport, so the plane crash has nothing to crash.

## Putting the meltdown and UFO on the game's own disaster page

The following records the earlier tile/OAM patch experiments. The current
implementation adds a separate host-rendered third row, hooks native confirm
hit-testing at `01:aecc`, and services bits 6/7 only after the panel closes.
It needs no environment option or cartridge patch. The original six buttons
and shared tile/OAM buffers retain their existing contents.

`SC_DISASTER_MENU8=1`. The page is `01:aa39` (screen mode `$01df == 2`), which
walks `$0197` as a checkbox list:

```
01:aa3e  ASL A ; ASL A     ; 2 shifts -> only bits 5..0 reach the walker
01:aa45  LDY #$0005        ; 6 rows
01:aa77  LDA $01a95c,X     ; bit-mask table
```

Two things make this cheap, and both were surprises:

- **The mask table already runs to `$0200`.** Bits 6 and 7 have masks sitting
  at `01:a968`/`01:a96a`, unused.
- **The input path already accepts eight rows.** `01:aa6b CMP #$0008` then
  `SBC #$0008` with only a `BMI` bail, so indices 0-7 pass. Only the *render*
  side is capped at six.

So two byte patches do it: `ASL A ; ASL A` -> `NOP NOP` so all eight bits reach
the walker, and `LDY #$0005` -> `LDY #$0007`.

The new bits are serviced **host-side**, not by extending `03:b8ae`. That
ladder is a fixed chain ending in `PLD`/`RTS` at `03:b914` with no room for two
more arms — and the meltdown and UFO are not ladder disasters anyway, they are
the `$0c0d` scenario events. So the ROM patch only has to make the bits
*settable*; the host reads them and arms the existing verified trigger.

Verified: `SC_DISASTER=6@<frame>` arms the meltdown and `7` the UFO, each
through the full chain and each restoring afterwards. `--qualify` is
byte-identical with and without the patch, so it is inert until a bit is set.

### The page cannot be widened in place: slots 6 and 7 are IN USE

Settled by play, and it is a negative result worth keeping.

Raising the row count (`LDY #$0005` -> `#$0007`, ASLs NOPped) does give the page
eight bits to walk. It also **wrecks the colours on the Speed, Options and
Disasters pages** -- reported as "all colourful even when not selected". A clean
dump shows why: slots 6/7 hold `e0 00 32 80`, byte 3 being a palette/attribute
byte, and slot 8 holds different tiles again (`$35`/`$33`). The buffer at
`$7e2063` is shared with other UI elements, so the extra rows write checkbox
tiles and palettes over them wherever they appear.

Both tile/OAM patch attempts are reverted. Host-side servicing of `$0197` bits
6 and 7 remains. The current third row and F12 actions use the same scenario
hooks; `SC_DISASTER=6/7` also remains usable headlessly.

### How the page is actually drawn

`01:d94f` blits four 16-word rows from ROM tables at `01:d8af`/`d8cf`/`d8ef`/...
into the tilemap at `$7e2440`, slots `$0100`/`$0120`/`$0140`/`$0160`.

Page setup for `$01df==2` (`01:d083`) and `==3` (`01:d0aa`) is byte-for-byte
identical **except** the final call -- `JSR $d94f` vs `JSR $d9ea` -- and both
blit into the same four slots. Only the source tables differ. So adding entries
means authoring new table rows there, not moving bytes in the sprite buffer.

### Two methodology traps, both paid for here

- **A WRAM write landing is not a pixel changing.** Sweeping the row-position
  byte over `$60`-`$88` rendered nothing at any value, while the buffer dutifully
  showed the new bytes. The framebuffer is the oracle.
- **Replaying a save state already parked on a page never re-runs that page's
  setup**, so it is blind to any setup-time change. Several screenshots taken
  that way proved nothing in either direction, including the one that appeared
  to show the layout clone doing nothing.

### Why the two new rows are invisible: they are SPRITES, parked

The patch works — rows 6 and 7 get the unchecked-checkbox tile written, exactly
like rows 0-5. They still do not appear, and the reason is in the addressing.

`01:a918`/`a93a` write at `$7e2063 + row*16`, then `+4`, `+8`, `+12`. A 4-byte
stride inside a 16-byte row is **OAM**: four sprites per row, `X, Y, tile,
attr`. Those routines set only the **tile** byte. Sprite *positions* come from
the page-setup code, which lays out six rows and no more:

```
row 0 @$2061: 40 6c 32 3c | 40 6e 32 2c | 50 8c 32 3c | 50 8e 32 44
row 3 @$2091: 58 a8 32 3c | 58 aa 32 2c | 68 c8 32 3c | 68 ca 32 44
row 6 @$20c1: e0 00 32 80 | e0 00 32 80 | e0 00 32 80 | e0 00 32 80   <- parked
row 7 @$20d1: e0 00 32 80 | e0 20 32 80 | e0 20 32 80 | e0 20 32 80   <- parked
```

`X = $e0` is the off-screen parking position. So the ROM patch correctly draws
two more checkboxes onto sprites nobody ever positioned.

The X values also give away the layout: rows 0-2 at `$40`/`$50`, rows 3-5 at
`$58`/`$68`. It is a **2-column by 3-row grid**, built for exactly six
disasters. Adding two more needs positions assigned for those eight sprites,
plus room in the menu box graphic behind them — this is a layout job, not
another byte patch.

The earlier guess that the rows would be missing *labels* was wrong in detail:
there are no separate label tiles to find. Each row is four sprites, and what
distinguishes one disaster from another is the tile/attr the setup code assigns
— so positioning the new rows means choosing their artwork too.

## The `$0197` ladder, fully attributed

Confirmed by observation: each bit was triggered from the F10 menu on a live
city and the resulting disaster identified on screen.

| bit | handler | disaster |
|---|---|---|
| 0 | `03:bbb9` | **fire** |
| 1 | `03:bc0b` | **flood** |
| 2 | `03:b9cd` | **plane crash** |
| 3 | `03:b9db` | **tornado** |
| 4 | `03:baf5` | **earthquake** |
| 5 | `03:ba47` | **monster** |

Bits 2-5 were fired and watched in one session (frames 72130, 72353, 73004,
73818); bits 3 and 5 reproduced the tornado and monster already attributed from
hand-played single-disaster runs, which is the check that the method is sound.
Bits 0 and 1 carry the same names on the reporter's knowledge of the game
rather than from that particular session.

This closes the attribution that "roughly 60% of the six handlers' code has
never executed" was blocking. It also retires the recurring temptation to look
for the meltdown here: the six are fire, flood, plane crash, tornado,
earthquake and monster, and **the meltdown is not among them** — it is
scenario-scoped, as is the UFO.

### Reconciling "the flood is not in the ladder"

The section above concluded, from a flood that started, spread and receded
while lighting no exclusive arm and executing zero first-time addresses, that
the flood is not dispatched through `$0197`. Bit 1 being the flood does not
overturn that; the two fit together:

- **Bit 1 starts a flood.** The arm at `03:bc0b` is the *initiator* — it seeds
  the event once and the ladder clears the bit.
- **The flood's behaviour is cellular.** Spread and recession happen on the
  per-tick map scan, not through the ladder, which is why a *naturally
  occurring* flood needs no arm and lights none.

So the earlier session watched a flood that was already running, and correctly
observed that its ongoing behaviour is not ladder-driven. The initiator and the
process are separate, and only the initiator is a `$0197` bit. Worth keeping
both readings: "not in the ladder" is right about the spread and wrong only if
read as "no bit starts it".

## `03:B92E` — the spontaneous disaster selector (never executed)

After the difficulty-scaled RNG draw passes (`03:B91E`, 1 in 4801/2401/1201),
a second draw picks *which* disaster. The whole selector at `03:B93E-B966` has
**never executed in any recording**, and it decodes at m=0 — a width-blind read
here produces `BRK` garbage, which is presumably why it stayed dark.

```
03:b92e  JSR $907e ; AND #$07        ; random 0..7
03:b934  CMP #$02 ; BCS $b93e
03:b939  JSR $bb6a                   ; draws 0-1
03:b93e  CMP #$0004 ; BCS $b948
03:b943  JSR $bc0b                   ; draws 2-3   flood
03:b948  CMP #$0005 ; BNE $b952
03:b94d  JSR $b9db                   ; draw  5     tornado
03:b952  CMP #$0006 ; BNE $b95c
03:b957  JSR $baf5                   ; draw  6     earthquake
03:b95c  LDA $0c07 ; CMP #$0050 ; BCC $b967
03:b964  JSR $ba47                   ; draws 4,7   monster, gated
```

| draw | disaster | share |
|---|---|---|
| 0-1 | `03:bb6a` | 2/8 |
| 2-3 | flood | 2/8 |
| 5 | tornado | 1/8 |
| 6 | earthquake | 1/8 |
| 4, 7 | monster, **if `$0c07` >= `$50`** | 2/8 |

Note draw 4 falls through both equality tests and lands on the monster gate, so
the monster gets two draws rather than one.

**`$0c07 >= 80` gates the monster.** That is a threshold on city state the
monster needs before it can appear — the shape of a population or size gate,
though which is not established here.

`03:bb6a` taking draws 0-1 is the only arm of this selector that has ever run,
which fits: an earlier session established it is *not* fire's spread step. Fire
(`03:bbb9`) and the plane crash (`03:b9cd`) are the two ladder handlers this
selector never calls directly, so `03:bb6a` choosing between them is the
obvious hypothesis — and explicitly only a hypothesis.

### What this says about the meltdown

The meltdown is not here either. The spontaneous path can raise flood,
tornado, earthquake, monster and whatever `03:bb6a` picks — six ladder
handlers, no seventh. Combined with the ladder itself being fully attributed,
the meltdown is not reachable by setting any `$0197` bit or by any random
draw, which is consistent with the reported behaviour that it comes with the
scenario.

So a menu trigger for it cannot work the way the six do. It needs whatever the
Boston scenario sets up at load time, and that is the next thing to find —
`03:ce2e` (scenario map loader) and `03:ddb6` (scenario select) are the places
to look.

## The title sequence (`$14 = 1`)

`03:d2c6` is the handler. Per frame it:

  - counts `$6e` down toward `#$e0`, one step per 14 frames (`$3c` is the
    divider) -- the fade/entry timer;
  - `JSL $0593ae`, which is the whole animation;
  - checks `$011b` for a button pair (`AND #$3030`) and, on it, zeros
    `$700000` and `$707ff0` -- the SRAM clear;
  - on `$c9 AND #$9000`, calls `03:e574` and `03:e349` and `INC $14` to leave.

`05:93ae` is a five-entry phase machine: `LDA $30 / ASL / TAX / JSR ($93c1,X)`.
The table at `05:93c1` holds only FIVE addresses -- anything read past entry 4
is code bytes, not handlers.

| `$30` | handler | |
|---|---|---|
| 0 | `05:93cb` | waits on `05:2cc6`, `INC $30` when it returns zero |
| 1 | `05:93d4` | the scroll animation |
| 2 | `05:941a` | |
| 3 | `05:93cb` | same waiter as phase 0 |
| 4 | `05:942e` | |

Phase 1 advances four scroll values at different rates off a frame counter
`$2c`: `$18` every 2 frames, `$1c` every 4, `$20` and `$24` every 8, each
masked to `#$01ff` -- the 512 px width of the 64-column BG1 map. `$2a`
decrements once `$18` passes `#$01e6`. It then calls `05:94be` and `05:952e`,
which set `$025d`/`$025f`/`$0261` and issue `COP #$00` with `A = 2`.

### There is no "the logo has left the screen" signal

Worth stating because it decides how the widescreen artifact can be fixed. The
title sign is ordinary tilemap content: it is drawn once and the phase-1
scroll carries it left, and when it passes x=0 the ROM does nothing at all --
hardware clips at the screen edge, so there is nothing to do. The mask to
`#$01ff` means it eventually wraps back around, which is why it reappears
"when it is needed another time".

So the ROM offers no flag, no counter and no write to hook: any suppression of
the sign in the widescreen margins has to be a host RENDER rule, and it cannot
be driven by anything the game itself knows.

### The intro's animation driver (`05:9603`)

Called first by `05:93ae` every frame, before the phase handler. It is a
table-driven tile-index animator, not a CHR animator: it rewrites the low 10
bits of chosen tilemap CELLS and leaves the upper 6 (palette/priority/flip)
alone.

```
LDX $32 / LDA $9696,X -> $79      ; $79 = the frame's cell list
INX INX / CPX #$0018 / BNE +      ; 12 lists, walked two bytes at a time
  $34 = ($34 + 1) % 3 ; X = 0     ; ...and a 3-step variant counter
STX $32
LDA ($79) / ASL / STA $7f         ; $7f = count * 2
$79 += 2                          ; past the count
$7c = $79 + ($34 + 1) * $7f       ; the variant's value block
loop:
  LDA ($79),Y -> X                ; a cell offset
  LDA $7e2840,X / AND #$fc00 / ORA ($7c),Y / STA $7e2840,X
  Y += 2 / CPY $7f / BNE loop
```

So each list is: a count, then that many cell offsets, then THREE blocks of
that many tile values -- one per variant.

The offsets index a set of shadow tilemaps in WRAM which are DMA'd to VRAM by
the tail of the same routine (`$968a` holds the VRAM destinations, `$9690` the
WRAM sources, `$0147`/`$0167`/`$0177`/`$0187` are the queue slots, `$b7 |= 4`
arms it):

| WRAM shadow | VRAM | what |
|---|---|---|
| `$7E2840` | `$5800` | BG2 tilemap |
| `$7E3040` | `$6000` | BG1 page 0 |
| `$7E3840` | `$6400` | BG1 page 1 |

An offset is therefore decoded as `$7E2840 + X`, and which map it lands in
follows from the `$800` spacing -- offsets past `$800` are BG1, not BG2.

The twelve lists at `05:9696`:

| list | at | cells | what the tiles draw |
|---|---|---|---|
| 0, 5 | `96ae`, `97da` | BG1 r15/r12, 6 | lit windows |
| 1, 6 | `97b0`, `96e0` | BG1 r20, 5 | lit windows (tiles `001`-`023`) |
| 2 | `970a` | BG1 r13-16, 9 | lit windows |
| 3, 4 | `980c`, `9786` | BG1 r17/r12, 5 | lit windows |
| 7, 10 | `9858`, `98b4` | BG2 r15, 7 | skyline with a blinking antenna light |
| 8, 9 | `9836`, `9892` | BG2 r14/r15, 4 | two blinking antenna lights |
| 11 | `9754` | BG1 r16, 6 | lit windows |

### The title sign is NOT in the animation driver

Decoded the CHR for every tile every list writes. All of it is lit windows and
blinking antenna lights; the three variants of lists 7-10 differ by a SINGLE
pixel value, which is the light blinking. There is no lettering anywhere in the
set.

This matters because two attempts to suppress the sign were built on the belief
that tiles `001`..`023` were it. They are list 1 and 6 -- a building's lit
windows -- which is why suppressing that range removed a small building from
the margins and never touched the sign. The mistake originally came from
reading a low-resolution ASCII render, where rows of lit windows look exactly
like letter glyphs.

So the sign is drawn by something else: it is not in the phase machine's
scroll, and not in the per-frame animator. The remaining candidates are OBJ
(the earlier OBJ-clip measurement removed 145 margin samples on the title,
never attributed) and a one-off tilemap write outside this driver.

### The title sign is OBJ (finally located)

**Its animation, decompiled 2026-09-23.** The sign is not placed by hand: it
is animation channel 0 of the player at COP service 9 (`00:8f82`), and the
title's own phase routine `05:942e` drives it -- the title's fifth phase,
`$30` = 4, which the sequence settles into around frame 840 from boot.
Before it the channel holds no sign of its own (record 0 at a stale
position), so anything reading it has to wait for that phase.

| | |
|---|---|
| `$0277` | its x, ten bits. `05:9462` takes one off every *second* frame -- `$2c` bit 0, the same test that steps the skyline's scroll at `05:9460` -- and the phase masks it to `$3FF`, and with bit 9 set it neither steps the channel nor emits anything -- which is a pause in the drawing, not in the travel, since x keeps counting down and 1023 continues smoothly from 0 |
| `$027F` | its y (183; the record's sprites sit 48 and 64 above it) |
| `$026F` | the record the script has reached, `$0267` the frames left on that step, `$026B` the offset into the script |
| `$00:A123` | the script: (frames, record) pairs ending in a 0 that restarts it -- `$32` for `$20`, `$33` and `$34` for `$10` each, `$35` for `$20`, then `$2E` and `$35` alternating every 8. That is the lettering's blink, the seven captured variants |
| `00:8ea9` | the emitter (COP 2) writes the record's six sprites at (`$0277` + dx, `$027F` + dy) from slot 96, taking x modulo 512 into the 9-bit OAM field |

Ten bits of travel through a nine-bit field is why hardware cannot show the
whole crossing: while the sign is still approaching from beyond the right
edge its entries read as far-left ones, and where the game stops emitting
they stay where they were -- the copy that used to hang in the left margin.
`src/sc_titlesign.c` reads that state and reports the six sprites at the
continuous position (x below 512 is itself, above is x - 1024), and runs the
script on by the player's own rule for exactly the frames the game skips --
without that the sign crossed the margin with its lettering frozen. Note the
widths: `$0267` and `$026B` are one byte per channel, `$026F`, `$0277` and
`$027F` words indexed by channel * 2, and x reads as `$FFFF` for the moment
between `05:9462`'s DEC and the phase's mask. It is all taken once a frame
at the full NMI, with the shadow OAM: read at any other point the sign is a
pixel ahead of the game's own copy on some frames and not on others, which
shows as a shiver instead of a slide.

**The shiver in the authentic columns, measured 2026-09-23.** The phase
routine emits before it moves:

```
05:9448  COP #$09   the channel writes the six sprites into the shadow OAM
05:9460  INC $16    the skyline's scroll steps  (only when $2c bit 0 is clear)
05:9462  DEC $0277  and the sign steps with it
```

Both go to the PPU at the next NMI, but the sprites were written before that
pair and the scroll after it, so the board is a frame behind the building it
hangs on. Measured on the title, frame by frame, the two never move together:

```
its building   -1 +0 -1 +0 -1 +0 -1 +0 ...
the billboard  +0 -1 +0 -1 +0 -1 +0 -1 ...
```

-- a pixel of shiver for the whole crossing, hardware included. `$16` and
`$0277` are stepped in the same breath, so the two belong on one frame:
`ScTitleSign_Lag()` reports the step the uploaded copy is behind, and
`title_sign_align()` in `src/main.c` finds those six entries by tile, row and
that older position and moves them the pixel forward, once per frame before
anything reads OAM. Only the sign's 48x32 changes (620 px on the stepping
frames, nothing at all on the others), both renderers get it since both
scan the same OAM, and the margins draw from the same position, so the two
halves meet exactly at the seam. `SC_SIGN_ALIGN=0` restores the lag.

Dumped OAM on the title with the sign stuck at the left edge (`SC_TITLE_DUMP`
now writes OAM and the OBJ tile bases alongside VRAM):

| slot | x | y | tile | size |
|---|---|---|---|---|
| 101 | -34 | 119 | `1ec` | 16 |
| 100 | -18 | 119 | `1ee` | 16 |
| 99 | -2 | 119 | `148` | 16 |
| 98 | -34 | 135 | `18e` | 16 |
| 97 | -18 | 135 | `1cc` | 16 |
| 96 | -2 | 135 | `1ce` | 16 |

Three 16x16 sprites across by two down -- a 48x32 billboard. Decoding those
tiles out of the OBJ CHR (`objTileAdr1=$2000`, `adr2=$3000`) gives a bordered
sign with lettering inside, which is what the seven captured variants are.

So it is NOT a background tile at all. Every earlier attempt aimed at BG1 tile
ranges was aimed at a building's lit windows, and this is why none of them
touched the sign.

The other margin sprites there are slots 125-127 and 57 (tile `120`, 64 px
wide, y=196) -- the row of blinking lights along the bottom -- and a long run of
parked entries at x=-128, y=0, tile 0.

### CORRECTION: the strict left-hint gate is NOT broken

`PpuWidescreenOamLeftHintAllows` exists to do exactly this job: with
`wsOamLeftHintStrict` set, an unhinted sprite lying wholly off-screen-left is
refused. Slots 97, 98, 100 and 101 qualify -- x = -18 and -34 at size 16, so
`x + size <= 0` -- and they are drawn anyway.

An earlier version of this section claimed the gate was failing. That was
wrong, and the way it was wrong is worth keeping.

Instrumenting the predicate to print its decision shows `strict=0` on the
sign's slots -- but ONLY in the first few frames after a save state is loaded.
The host publishes the hint arrays at `vPos == 0`, and a state loaded mid-frame
does not reach that point for a few frames, so those frames render with the
gate disabled. Every `strict=0` reading came from that window.

From BOOT, over 900 frames, there are ZERO gate calls with strict off. With
strict on the gate decides correctly: slot 127 is `hinted=1` (host-placed, so
allowed), slot 57 `straddles` (partly on screen, correctly allowed), and a
sprite lying wholly off-screen-left is blocked.

Two things follow.

There IS a real but narrow bug: **the first frames after a save-state load
render with OAM hints unpublished**, so anything parked off-screen-left draws
into the margins until the host's next `vPos == 0`.

And -- more importantly for anyone measuring here -- **a rendering taken from a
freshly loaded save state is not evidence about normal play**. The sign
appearing in slot 3's margin is at least partly that artifact: by the time the
gate goes strict a few frames later, the sign's slots are no longer at negative
x at all.

## The simulation's data structures, from write attribution

`SC_WRAM_MAP` records the LAST routine to write each WRAM byte. Run over ~12
ticks of a running city (`savestate_0`, 2500 frames), every large written
region in bank `7F` resolves to a single owning routine in bank 03. The sizes
are the interesting part.

| WRAM | size | grid | written by |
|---|---|---|---|
| `$7F0200` | 24000 | 120x100 x2 bytes -- **the city map** | `03:b191` (94%) |
| `$7F6B00` | 3000 | 60x50 | `03:9dc9` |
| `$7F76B8` | 3000 | 60x50 | `03:9f47` |
| `$7F8270` | 3000 | 60x50 | `03:9c89` |
| `$7F8E28` | 3000 | 60x50 | `03:9b87` |
| `$7FA598` | 1500 | | `03:afc5` |
| `$7FAB74` | 750 | 30x25 | `03:a01d` |
| `$7FAFE8` | 195 | 15x13 | `03:9f93` |
| `$7FB0AB` | 195 | 15x13 | `03:9ac8` |
| `$7FB16E` | 390 | | `03:828b` |
| `$7FB2F4` | 390 | | `03:828f` |
| `$7FB47A` | 390 | | `03:a286` |
| `$7FB600` | 3000 | 60x50 | `03:a125` |
| `$7FC1B8` | 3000 | 60x50 | `03:a09f` |
| `$7FCD70` | 750 | 30x25 | `03:9d2e` |
| `$7FD05E` | 390 | | `03:a1a4` |

Six 3000-byte arrays at half resolution, two 750-byte at quarter, two 195-byte
at eighth, four 390-byte. Each has exactly one owner, and the owners are
distinct routines -- so these are separate per-cell layers maintained
independently, not one array written from several places.

The sizes and owners above are MEASURED. What each layer holds is not yet:
a multi-resolution overlay set is how this simulation family is built
(density, traffic, pollution, land value, crime at half res; service coverage
at coarser res), but which array is which has not been established here and
should not be assumed from the resolution alone.

## The tick's pipeline runs twice

`03:8000` is documented above as calling a fixed sequence. Reading it against
`03:88b4` shows the sequence is not flat:

```
03:800a  JSR $90a7
03:800d  JSR $c474
03:8010  JSR $b84b
03:8013  JSR $88b4     ; itself: 894c, 821d, 8297, addf, afb0, b152, addf,
                       ;         9c11, 9e8e, 9ad7, 9c11, 9e8e, 9ad7, 9aa3,
                       ;         clear $0cdd..$0ce6, $0dfb=1, b42f, addf
03:8018  JSR $894c     ; again
03:801b  JSR $821d     ; again
03:801e  JSR $8297     ; again
03:8021  JSR $addf     ; again
```

`03:88b4` is straight-line and ungated, so `894c`/`821d`/`8297`/`addf` run
TWICE per tick and `addf` four times. The triple `9c11`/`9e8e`/`9ad7` is
likewise repeated back to back inside it. Repeated passes over the same data
are how a diffusion step is iterated, which fits the overlay layout above.

`03:b152`, called only from `88b4`, is the map-wide pass: its inner store at
`03:b191` accounts for 94% of the 24000 map bytes and 264,100 writes over the
sampled ticks.

### The half-resolution grid is 60x50, from the code

Not inferred from the 3000-byte size. `03:9f47`'s enclosing loop counts an
inner index to `#$003c` (60) and an outer to `#$0032` (50):

```
03:9f49  INC $08 ; LDA $08 ; CMP #$003c ; BEQ +     ; 60 columns
03:9f52  JMP $9eb0
03:9f55  INC $0a ; LDA $0a ; CMP #$0032             ; 50 rows
```

So the map's 120x100 is halved on both axes, and a coarse cell covers a 2x2
block of tiles.

### Working layers and derived copies

Two of the six 3000-byte arrays are not independent -- they are cheap
transforms of two others, done once per pass:

```
03:9b75  LDA $7fc1b8,X / ASL A / BCC + / LDA #$ff        ; saturating x2
03:9b83  STA $7f8e28,X

03:9c81  LDA $7fb600,X / STA $7f8270,X                   ; plain copy,
03:9c89  STA $0c ...                                     ; accumulating a
                                                         ; 32-bit total in
                                                         ; $00:$02, a maximum
                                                         ; in $1c and a count
                                                         ; in $14
```

| working | derived | transform |
|---|---|---|
| `$7FB600` (`03:a125`) | `$7F8270` (`03:9c89`) | copy, with total/max/count |
| `$7FC1B8` (`03:a09f`) | `$7F8E28` (`03:9b87`) | doubled, saturated at 255 |

So the six half-res arrays are really two working layers, two presentation
copies of them, and two more (`$7F6B00`, `$7F76B8`) that are filled by a
different kind of pass -- see below. That halves the number of distinct
quantities to identify.

### `03:9dc9`'s pass reads the MAP, not another layer

Its loop masks a value to ten bits -- the map cell width -- skips zero, and
branches on tile-value thresholds to accumulate weights:

```
03:9dcc  AND #$03ff        ; a map cell
03:9dcf  BEQ +             ; empty, skip
03:9dd1  CMP #$0028 ; BCS +
03:9dd6  LDA $22 ; ADC #$000f ; STA $22    ; accumulate 15 for this class
```

That is the shape of a map-to-coarse-grid tally: walk the tiles, classify each
by its index, and add a per-class weight into the 60x50 cell that contains it.
The thresholds are the tile taxonomy, so reading them out is the way to learn
what the tile ranges mean -- which is also what the map generator work left
open.

**Disassembly note**: `03:9dc9` is mid-instruction. Starting a listing there
produces plausible nonsense (`BRK`, an absolute-indexed `ADC`); the real
instruction boundary is `03:9dcc`. The write-attribution PC is the address of
the store's NEXT instruction in several of these cases, so treat an attributed
PC as "in this routine", not as an instruction boundary.

### The tile weight ladder, `03:9e0c`

Takes a map cell in `A` and returns a signed weight. Read out in full:

| tile range | weight |
|---|---|
| `$000`-`$03f` | 0 |
| `$040`-`$04f` | 10 |
| `$050`-`$05f` | 25 |
| `$060`-`$07e` | 0 |
| **`$07f` exactly** | 60 |
| `$080`-`$1fc` | 0 |
| `$1fd`-`$244` | 50 |
| `$245`-`$266` | 0 |
| `$267`-`$276` | 60 |
| `$277`-`$286` | 0 |
| `$287`-`$2b9` | 60 |
| `$2ba`-`$363` | 0 |
| **`$364` exactly** | **-40** |
| `$365`+ | 0 |

`$07f` and `$364` are tested first and by equality, so they are single tiles
singled out of ranges that otherwise weigh 0 and 0. `$364` is the only negative
weight in the table.

Two of the branches are easy to misread: at `03:9e45` `Y` is loaded with 60
BEFORE the comparison, and `BCC $9e5c` then throws it away by reloading 0. So
`$245`-`$266` weigh 0 despite the `LDY #$003c` immediately above them, and the
same trick appears again at `03:9e52`.

### What the caller does with it, `03:9dcc`

```
AND #$03ff                 ; a map cell
BEQ out                    ; empty contributes nothing at all
CMP #$0028 ; BCC low       ; below $28:
    $22 += 15              ;   add 15 and stop -- no weight, no count
CMP #$02bf / #$0354        ; inside [$2bf,$354),
CMP #$0307 / #$0310        ;   excluding $307 and $310 exactly:
    $22 = 255              ;   saturate
JSR $9e0c ; $0e += weight  ; every non-empty cell contributes its weight
CMP #$0030 ; BCS +
    INC $10                ; and cells >= $30 are counted
```

So one pass produces three things per coarse cell: a saturating quantity `$22`
that low tiles nudge by 15 and one specific tile band pins to maximum, a
weighted sum `$0e`, and a plain count `$10` of tiles at or above `$30`.

The tile numbers here are the same 10-bit values the map generator writes, so
this ladder is a second, independent source on what the tile ranges MEAN -- the
generator produced values `$00`-`$25` and this classifier treats everything
below `$28` as one class and everything below `$30` as uncounted. Those two
readings agree, which is worth noting because they were derived from completely
different code.

What the three quantities ARE is still not established, and the shape alone
should not be used to name them.

### The overlay layers are selected by `$0d49` (`02:91a0`)

The layers are not anonymous after all -- bank 02, the UI bank, picks between
them from a single view-mode byte:

```
02:919d  LDA $0d49
02:91a0  CMP #$0b / BEQ -> LDA $7f6b00,X
02:91a4  CMP #$0a / BEQ -> LDA $7f76b8,X
02:91a8  CMP #$09 / BEQ -> LDA $7f8270,X
02:91ac  CMP #$08 / BEQ -> LDA $7f99e0,X
02:91b0            else -> LDA $7f8e28,X
```

| `$0d49` | layer read |
|---|---|
| 8 | `$7F99E0` |
| 9 | `$7F8270` |
| 10 | `$7F76B8` |
| 11 | `$7F6B00` |
| anything else | `$7F8E28` |

So four view modes each have their own map and everything else falls back to
one shared layer. `$0d49` is written at `02:84d6`, `02:85d0` and `02:862c`, and
indexes a second table at `02:86a4` that gives a per-mode kind (0..3) --
modes 0-3 kind 1, 4-5 kind 0, 6-7 kind 2, 8-11 kind 0, 12-13 kind 3. The four
overlay modes share kind 0 with modes 4 and 5.

`$7F99E0` is a FIFTH layer that the earlier region scan missed: it begins
exactly where the `$7F6B00` block ends, so it was the boundary rather than a
region. `03:88f3` reads it too. Its owning writer has not been attributed.

### What is now known, and what names them

Measured: five display layers, which view mode selects each, the 60x50 grid,
which two are cheap transforms of working layers, and the tile weight ladder
that feeds one of the passes.

NOT measured: which layer is which quantity. The view modes are contiguous
(8, 9, 10, 11), so they are almost certainly consecutive entries in the game's
own map-view menu -- and the order of that menu names them directly. That is a
question for someone who can read the menu, not something to infer from the
weight table.

### The diffusion step, `03:a0c4`-`a137`

The pass that produces `$7FB600` from `$7FC1B8` is a five-point stencil:

```
for row  $02 = 0..49
 for col $00 = 0..59
    sum = 0
    if col != 0    sum += $7FC1B7,X      ; left    (base-1)
    if col != 59   sum += $7FC1B9,X      ; right   (base+1)
    if row != 0    sum += $7FC17C,X      ; above   (base-60)
    if row != 49   sum += $7FC1F4,X      ; below   (base+60)
                   sum += $7FC1B8,X      ; self
    $7FB600,X = min(250, sum >> 2)
```

Two things worth drawing out.

**It amplifies as well as spreads.** Five terms divided by four: a uniform
neighbourhood comes out 1.25x higher than it went in, and the clamp at 250 is
what stops it running away. This is not an average, it is a
spread-and-grow step with a ceiling.

**The edge handling confirms the grid independently.** `CPY #$003b` (59) is the
last column and `CPY #$0031` (49) the last row, so out-of-bounds neighbours are
dropped rather than wrapped. That is a third measurement of 60x50, after the
loop bounds at `03:9f47` and the 3000-byte array size.

The sum is kept as 16 bits across `$04`/`$05` with `INC $05` on each carry, so
five bytes at 250 cannot overflow it.

### The layer chain so far

```
$7FC1B8  (03:a09f)   a working quantity
   |
   +--> $7FB600  (03:a125)   diffused: 5-point stencil, >>2, clamp 250
   |       |
   |       +--> $7F8270  (03:9c89)  copy, plus 32-bit total, max, count
   |
   +--> $7F8E28  (03:9b87)  doubled, saturated at 255
```

`$7F8E28` is the fallback the UI shows for every view mode outside 8-11, and
`$7F8270` is view mode 9. So one quantity feeds two different views of itself:
raw-doubled, and diffused.

### How much CPU the simulation actually costs (`SC_BANK_PROFILE=1`)

Counted per-bank opcodes over 2500 frames of a running city:

| bank | opcodes | share |
|---|---|---|
| 00 | 6,514,275 | 21.1% |
| 01 | 2,690,673 | 8.7% |
| 02 | 3,086,117 | 10.0% |
| **03** | **18,633,532** | **60.3%** |

and within bank 03, by page:

| page | share of bank 03 | what lives there |
|---|---|---|
| `03:8400` | 25.2% | |
| `03:8300` | 17.7% | |
| `03:8200` | 14.5% | `821d`, `8297` -- two tick pipeline stages |
| `03:b100` | 11.6% | `b152`/`b191` -- the map-wide pass |
| `03:a000` | 8.8% | `a09f`, `a0c4` -- the diffusion kernel |
| `03:a200` | 8.3% | `a29a`, `a2f5` -- index and multiply helpers |
| `03:9d00`, `9b00`, `a100`, `9c00` | 10.1% | the overlay passes |

Those ten pages are 96.2% of bank 03, so **the simulation is about 58% of the
guest's entire opcode workload** (0.603 x 0.962).

### What replacing it would and would not buy

It would NOT make rendering faster. The host already renders every frame well
inside budget -- `SC_FRAME_TIME` reports no frame exceeding the threshold -- so
the guest's opcode count is not what limits the picture.

What it would buy is the thing DRAG TURBO exists to paper over: bank 03 holds
the CPU for about four consecutive frames at a time, and the bank-01 cursor
dispatcher does not run at all during those, giving the 4-on/4-off duty cycle
that makes the cursor and map scroll feel starved. Removing 58% of the guest's
work is removing most of what starves them.

The bar is much higher than the map generator's, though, and worth stating
before anyone starts. The generator was a pure function: one seed in, 12000
cells out, verifiable by comparing a finished map. The simulation is stateful
and continuous -- an error does not show up as a wrong pixel, it shows up as a
city that evolves differently over an hour of play. Any replacement has to be
checked by running both and comparing WRAM tick by tick, and the layers it
maintains are not all identified yet.

### The map cell accessors, `03:849e` (read) and `03:84c4` (write)

The hottest page in the simulation is not a simulation rule at all -- it is
address arithmetic.

```
03:849e  read  cell(x,y) -> A        03:84c4  write cell(x,y) = Y
    ASL A ; STA $0b3f                    (identical arithmetic)
    STZ $0b40                            ...
    LDA #$00 ; XBA          ; y*256      TYA
    PHA ; ASL x4 ; STA $0b3d ; y*16      STA $7f0200,X
    PLA ; XBA ; SEC ; SBC $0b3d
    CLC ; ADC $0b3f         ; + x*2
    TAX ; LDA $7f0200,X
```

Both compute the same index:

```
index = y*256 - y*16 + x*2   =   y*240 + x*2
```

240 is the row stride: 120 cells at 2 bytes each. The 65816 has no addressing
mode for a 240-byte stride, so every single map access pays a shift-and-
subtract sequence plus two scratch stores at `$0b3d`/`$0b3f`.

`03:8400`-`84ff` is **25.2% of all bank-03 opcodes**, and bank 03 is 60.3% of
the guest's total -- so roughly **15% of the entire game's CPU time is spent
computing map cell addresses**.

That is worth knowing for two reasons. It is the strongest single argument for
moving the simulation to native code, where the same index is one multiply the
compiler will strength-reduce to a shift-add and no memory traffic at all. And
it means the profile's hot pages should not be read as "these are the important
rules" -- the top page is plumbing, and the actual per-tick rules sit further
down the list.

`$7F0200` is the same map the generator writes, so the accessors, the tile
weight ladder at `03:9e0c` and `src/sc_mapgen.c` are all three looking at
one array in the same 10-bit format.

### The per-tile attribute table, `03:84eb`

This is the tile taxonomy, and it is a plain byte table indexed by the 10-bit
tile index. `03:8297`'s per-cell loop dispatches on its BITS:

```
03:82dc  LDA $84eb,Y ; AND #$01 ; if set: $7f02f0,X |= $4000 ; JSR $90c5
03:82ff  LDA $84eb,Y ; AND #$20 ; if set: JSR $a73d
03:8310  LDA $84eb,Y ; AND #$40 ; if set: JSR $a493
03:831f  LDA $84eb,Y ; AND #$10 ; if set: JSR $a7da
```

So each bit selects a rule that applies to that tile. Four are identified from
the dispatch above; the rest (`b1`, `b2`, `b3`, `b7`) are consumed further down
the same stage and elsewhere.

The table's shape, by contiguous runs of equal attribute:

| tiles | flags | note |
|---|---|---|
| `$001`-`$013` | `08` | one class |
| `$014`-`$027` | `04` | another |
| `$028`-`$02f` | `00` | no rules at all |
| `$030`-`$03e` | `48`/`44`/`d4` | a 15-tile group |
| `$040`-`$04e` | `48`/`44`/`d4` | the SAME pattern again |
| `$050`-`$05e` | `48`/`44`/`d4` | and again |
| `$060`-`$06e` | `98`/`94`/`b4` | a fourth, different |
| `$070`-`$07e` | `28`/`24` | a fifth |
| `$080`-`$3bd` | `84`, with `85` at intervals | the large region |
| `$354`-`$363` | `40` | an exception inside it |
| `$364`-`$365` | `00` | and another |

Three consecutive 15-tile groups sharing one flag pattern (`$030`, `$040`,
`$050`) line up with the weight ladder giving those same ranges 0, 10 and 25 --
the same structure repeated at three levels.

**The `$080`-`$3bd` region is built from 9-tile groups.** 82 tiles there carry
bit 0, and the spacing between them is exactly 9 in 72 of 81 cases (the
exceptions are 10, 16 x3, 21, 25, 26). Nine tiles with a flag on the first is
what a 3x3 object looks like with its anchor marked, and bit 0 is the one that
sets `$4000` in the map cell and calls `03:90c5` -- i.e. it fires once per
object, not once per tile.

Measured: the table, the bit-to-routine dispatch, the run structure and the
spacing. Inferred: that a 9-run is a 3x3 building and bit 0 marks its anchor.
The inference is strong -- 72 of 81 exact -- but it is still an inference, and
the way to settle it is to read `03:90c5` and see whether it treats the cell as
the corner of a 3x3.

### `03:90c5` -- the once-per-object dispatcher

Reached only from the bit-0 path of the attribute table, so it runs once per
object rather than once per tile. It calls `03:9137` and then routes on the
tile index in `$0b89`:

| tile | goes to | also |
|---|---|---|
| `< $080` | nothing | |
| `$080`-`$128` | `03:937a` | |
| `$129` exactly | `03:91df` | `INC $0e1f` |
| `$132` exactly | `03:9207` | `INC $0e1f` |
| `$137`-`$1f3` | `03:92ce` | |
| `$249`-`$2ba` | `03:aa9f` | |
| `$2bb`-`$375` | `03:ae25` | `INC $0e1f` |
| `$307`, `$310`, `$36b` | `03:aa9f` | singled out by equality |
| `$376`-`$399` | `03:937a` | |
| `>= $39a` | `03:92ce` | |

`03:937a` and `03:92ce` each serve two disjoint ranges, and they sit beside the
three accumulators at `03:924f`/`92fb`/`93b1` this document already links to
the capacity contribution for `$0b89`. `$0e1f` counts objects from three
specific classes.

### `03:9137` -- the neighbour probe, and 3-cell object spacing

It biases the cell index by `-$2d0` and then reads with constant bases, so the
net offsets are what matter:

```
LDA $0b49 ; SEC ; SBC #$02d0 ; TAX
$7F04D6,X  ->  index + 6     ; 3 cells RIGHT   (6 bytes = 3 cells x 2)
$7F04CA,X  ->  index - 6     ; 3 cells LEFT
$7F07A0,X  ->  index + $2d0  ; 3 rows DOWN     (720 = 3 x 240)
```

and it uses them to check whether a partner tile is where it should be --
`$37a` expects `$383` three cells right, `$383` expects `$37a` three cells
left, `$38c` expects `$395` three rows down -- returning early when the pair is
intact.

**Three-cell steps in both axes is object spacing**, which supports the 3x3
reading of the 9-tile attribute runs. Note the scope honestly though: this
routine only does the check for tiles `>= $37a`, so it demonstrates 3-cell
structures for that range rather than proving every 9-run is a 3x3. The
attribute-table spacing (72 of 81 gaps exactly 9) and this are two independent
pieces of evidence pointing the same way, which is stronger than either, and
still short of reading a handler that walks all nine cells.

### The three zone handlers, and how they reach population

`03:922f`, `03:92ce` and `03:937a` are the same routine three times over with
different constants. Each is reached from `03:90c5`'s tile-range dispatch, so
each runs once per object of its class.

| | `03:922f` | `03:92ce` | `03:937a` |
|---|---|---|---|
| locals reserved | 8 | 8 | 10 |
| objects counted in | `$0b91` | `$0b95` | `$0b8d` |
| capacity helper | `03:847a` | `03:8456` | `03:842f` |
| flat capacity above a threshold | -- | `6` for tiles >= `$39a` | `$30` (48) for tiles >= `$376` |
| extra special case | -- | -- | tile `$084` -> `03:9a3e` |
| accumulates into | `$0b8f` | `$0b93` | `$0b8b` |
| accumulator instruction | `03:924f` | `03:92fb` | `03:93b1` |
| constant passed to `03:9035` | -- | `5` | `$23` (35) |

Those three accumulator addresses are the ones this document already listed as
"three sibling accumulators" without saying what they accumulated. They are the
three terms of the population formula recorded above:

```
population = (($0b8f + $0b93) * 8 + $0b8b) * 20
```

so two classes are weighted x8 and one x1.

**The flat capacities are chosen to match after that weighting.** `03:937a`
gives 48 and is weighted x1; `03:92ce` gives 6 and is weighted x8. Both come to
48. That is a useful check on the whole reading -- the formula, the handler
identification and the constants were recovered from three separate places, and
they agree.

All three share one pair of counters: `$0e1b` for objects whose capacity came
out non-zero and `$0e1d` for those that came out zero. So the game separately
tracks how many objects of any class are producing nothing.

### The overview map's per-cell display path (`02:9150`, `02:91d2`)

Reported from play: entering the visual/overview map from the menu takes a long
time, while the graph screen next to it appears instantly.

Two pieces of that path are read:

`02:91d2` converts one layer value to a colour. It halves with rounding five
times (`LSR ; ADC #$00` x5, i.e. divide by 32 rounding up), clamps to 8, and
indexes a table at `$00AA75`. That is cheap.

`02:9150` computes the cell address, and is not cheap. Per cell it masks NMI
(`$b3` -> `$b1`), writes both hardware multiplier ports, burns the mandatory
delay, reads `$4216`/`$4217`, and restores NMI -- the same expensive shape the
map generator's range primitive uses, and roughly 60-80 cycles of overhead
before any actual work.

**This is a candidate for the slowness, NOT a diagnosis.** The arithmetic says
3000 cells of that costs single-digit frames and 12000 costs under a second,
which is not "a huge time" -- so either the loop is larger than the display
grid, or entering the screen recomputes the simulation layers rather than just
drawing them, or the cost is somewhere else entirely. Guessing between those
from the listing is exactly the mistake that cost three reverts on the title.

To settle it: a save state taken immediately before pressing B, then
`SC_BANK_PROFILE=1` across the load. That gives the hot pages directly and
distinguishes "display path" from "recompute the whole simulation", which need
completely different fixes.

**RESOLVED -- and it is none of the three guesses above.** Measured from
`savestate_3.bin` (menu, cursor on the spot, B injected at frame 60):

| frames | what the screen does |
|---|---|
| 60 | B pressed |
| 70 | 11,784 px change -- the menu tears down |
| 70-210 | **nothing. Frozen for 140 frames (~2.4 s).** |
| 210 | 45,191 px change -- the map appears, in a single frame |
| 210+ | 100-300 px per frame -- normal animation |

The map is drawn all at once at the end, so nothing is slow about drawing it.
Isolating the frozen window by subtracting a `--qualify 70` profile from a
`--qualify 210` one:

    frames 70-210: 1,815,038 opcodes over 140 frames
      bank 00  48.4%    bank 02  46.1%    bank 03  5.3%
      = 13k opcodes per frame

Bank 03 -- the simulation -- is **5.3%**, so "entering the screen recomputes
the simulation layers" is wrong. An earlier 900-frame profile put bank 03 at
48.4% with `03:b100` hottest, which looked exactly like recomputation; that
window was mostly *post-load* simulation ticking. Profile the window, not the
session.

**13k opcodes/frame is the CPU running flat out, not idling.** This is worth
stating plainly because the first reading of these numbers got it backwards.
The 65816 runs at 3.58 MHz, so one 60 Hz frame is about 59,600 CPU cycles, and
at ~5 cycles for an average instruction that is roughly **10k instructions per
frame** -- not the "several hundred thousand" a first guess suggests. The
harness agrees independently: `master=89327400` over 250 frames is 357,309
master cycles per frame against the 357,955 a real SNES has. So the guest is
saturated for all 140 frames. The freeze is compute-bound.

Per-page during the freeze (`SC_BANK_PROFILE_PAGE` was added to get this --
the breakdown used to be nailed to bank 03, which is why the load first looked
like a simulation problem):

    bank 00                          bank 02
      00:9100  53.8%  (3318/frame)     02:8b00  35.4%  (2110/frame)
      00:9300  24.5%  (1511/frame)     02:8900  29.0%  (1730/frame)
      00:9200  11.2%   (691/frame)     02:9100  13.3%   (793/frame)

`00:9100`/`00:9200` are an LZ-style decompressor (`AND #$e0 ; CMP #$e0`,
bit-shifting, streaming through `$0000,Y` with a `JSR $926d` refill).
`02:8900` is the VRAM upload: it writes the DMA channel registers
(`$4300`-`$4306`) and the VRAM address (`$2116`/`$2117`), fires the transfer
with `STA $420b`, then calls `JSL $008206`.

`00:8206` is `PHP ; REP #$20 ; LDA #$0000 ; COP #$00 ; PLP ; RTL` -- a COP
syscall. The handler at `00:8211` dispatches through `JSR ($8223,X)`, and
entry 0 is the vblank wait at `00:930d`:

    00:930d  SEP #$20
    00:930f  STZ $b9        ; clear the NMI flag
    00:9311  INC $c7        ; spin...
    00:9313  LDA $b9
    00:9315  BEQ $9311      ; ...until the NMI handler sets it
    00:9317  RTS

**That wait is NOT where the time goes, and a fix aimed at it does nothing.**
Instrumenting entries to `00:930f` over 250 frames of the load counts only
**81 waits in total**, eight of them from the upload sites
(`02:8839`, `02:887e`, `02:88c3`, `02:8908`, twice each). One-DMA-per-vblank
would need ~140. `00:9300`'s 1511 opcodes/frame is the spin *inside* those few
waits, about 12% of the window; the other ~88% is real work.

A hook that collapsed this wait was written, gated on force-blank, and thrown
away: `INIDISP` reads `0f` throughout, so the display is **on at full
brightness** for the whole freeze -- the screen is static, not blanked -- and
the gate never fired. Both halves of that idea were wrong, the premise and the
gate. What makes the intermediate uploads invisible is the layer/tilemap state,
not force-blank.

So the 2.4 seconds is authentic: a real SNES spends the same time, because the
work genuinely costs ~1.8M instructions. The graph screen next door is instant
because it has almost nothing to decompress.

Making it faster therefore means doing the *work* on the host, the way
`src/sc_mapgen.c` replaced the generator -- not adjusting timing. The
target is the decompressor at `00:9100`/`00:9200` (31% of the window) and
whatever `02:8b00` is (35%, and still unidentified -- it holds no
`JSL $008206`, so it is not part of the upload pacing). That is an HLE with the
same bar the generator had to clear: byte-exact output verified against the
guest before it is trusted.

### `02:899b` -- the overview map is software-rendered, not loaded

`02:8b00` was 35% of the load window and unidentified. It is the per-cell tile
classifier, and the routine around it is the whole answer to where the 2.4
seconds goes.

Getting there needed a new tool. `dis_mx.py --starts` fixes one (m,x) for a
whole range, and this code changes width every few instructions: read at
`m1x1`, `02:8b00` disassembles as plausible nonsense (`BRK #$90`, `MVN`,
`ORA [$e0],Y`) and looks like data. `tools/dis_cov.py` decodes each address at
the width it *actually ran at*, taken from an `SC_MX_BITMAP` capture, and the
same bytes become obvious code. The page runs `m0x0` -- 16-bit A and 16-bit
index. **Two separate wrong readings this session came from assuming a width;
the bitmap is ground truth and costs one run.**

The outer loop, `02:899b`:

    02:899b  REP #$30
    02:899d  STZ $0d63          ; cell index
    02:89a0  STZ $0d61          ; row
    02:89a3  STZ $0d5f          ; column          <- row loop
    02:89a6  LDY #$0000                           <- 8-column group
    02:89a9  PHY                                  <- per-cell, 8 times
             JSR $8b34          ; classify -> colour byte in A
    02:89b8  STA $0d57,Y        ; into the 8-byte staging row
    02:89c1  $0d63 += 2         ; cells are words
    02:89c8  CPY #$0008
    02:89cb  BNE $89a9
    02:89cd  JSR $909d          ; transpose the 8 bytes into $7EA000,X
    02:89d6  $0d5f += 8
    02:89dc  CMP #$0078         ; 120 columns
    02:89df  BCC $89a6
    02:89e1  INC $0d61
    02:89e7  CMP #$0064         ; 100 rows
    02:89ea  BCC $89a3
    02:89ec  RTS

120 x 100 = **12,000 cells**, 1,500 transposer calls. The classifier's measured
295,338 opcodes over 12,000 calls is 24.6 each, which is what a call-per-cell
predicts, so the loop and the profile agree.

`02:8b34` reads the city map and classifies the tile:

    02:8b36  LDX $0d63
    02:8b39  LDA $7f0200,X      ; the map, at the documented address
    02:8b3d  AND #$03ff         ; the documented 10-bit tile mask
    02:8b40  TAX                ; then ~300 instructions of ladder

It is a long comparison ladder over tile ids (`$0030`, `$007f`, `$0354`,
`$0355`, `$0364`, `$0365`, `$0080`, `$0137`, `$01f4`, `$0245`, `$0257`,
`$0297`, `$02bb`, `$0356`, `$0366`, `$0376`, `$039a` ...) ending in table
reads -- `$02948e`, `$02937d`, `$029401`, `$0293f1` -- and it branches on
`$0d49`, the overlay selector already documented above. Animated tiles (ids
`$14`-`$25`, when `$3e`==3) add a 4-bit animation counter kept at `$0b3b`,
which the classifier *increments as a side effect*. Some paths use the
hardware divider (`$4204`/`$4206` -> `$4214`/`$4216`) with the NMI-masking
`$b3` -> `$b1` dance, the same expensive shape `02:9150` uses.

`02:909d` is the bitplane transposer. For each of eight output bytes it shifts
one bit out of each of `$0d57`-`$0d5e` and rotates it into A
(`LSR $0d57 ; ROL A` x8), then stores to `$7EA000,X`. That is eight pixel rows
becoming one planar SNES tile. `02:8900` then DMAs `$7E____` to VRAM in 2 KB
chunks.

So **the overview map is not loaded from anywhere. The game software-renders
the entire city into a bitmap, one tile at a time, then uploads it.** That is
what 1.8M instructions buy, and why the graph screen next door is instant.

#### What that means for making it fast

`02:899b` is a clean HLE boundary: one RTS-terminated routine, no arguments.
Its inputs are the map at `$7F0200`, the overlay selector `$0d49`, `$3e`,
`$40`, the animation counter `$0b3b`, and four ROM tables. Its outputs are the
bitmap at `$7EA000`, the loop variables `$0d5f`/`$0d61`/`$0d63`, the staging
bytes `$0d57`-`$0d5e`, and the updated `$0b3b`. Verification is the same bar
the map generator had to clear: run the guest routine, snapshot `$7EA000`
onward, run the HLE from the same state, compare byte for byte.

**Measured ceiling, so the work is not oversold.** The map builder is bank 02's
share of the window: 46%. The other 48% is bank 00, and that one really is a
decompressor -- re-read at its true `m1x0` width it is a 3-bit command / 5-bit
length stream (`AND #$e0` / `AND #$1f`, count in Y, `LDA $0000,Y`, dispatch on
`$00`/`$20`/`$40`), confirming the first reading rather than overturning it.
So HLE-ing `02:899b` alone takes the load from ~140 frames to roughly 76 --
worth having, but it halves the wait rather than removing it. Removing the
wait means doing the decompressor too.

### `00:90dd` -- the stream decompressor, decompiled and replaced

48% of the overview-map load. `src/sc_decomp.c` does the same work on the
host; `SC_DECOMP_FAST` is on by default, `=0` disables it.

The listing came from `tools/dis_cov.py`, and that mattered here: this routine
runs at `m1x0`, and the first attempt to read it assumed `m1x1`. That happened
to be close enough to produce a *nearly* right answer, which is worse than an
obviously wrong one. Use the bitmap.

The format is one command byte `c`:

    c == $FF                  end of stream
    (c & $E0) == $E0          long form:  cmd = (c << 3) & $E0
                                          len = (((c & 3) << 8) | next) + 1
    otherwise                 short form: cmd = c & $E0
                                          len = (c & $1F) + 1

Long form packs its command into bits 4-2, so the `<< 3` lifts those bits into
the position the short form's command already occupies and both feed one
dispatch. Then, for `len` bytes:

| cmd | at | what |
|---|---|---|
| `$00` | `00:9150` | copy `len` bytes straight from the source |
| `$20` | `00:916b` | one byte, repeated |
| `$40` | `00:9187` | two bytes, alternating |
| `$60` | `00:91c8` | one byte, incrementing each time |
| `$80` | `00:91e5` | back-reference, 16-bit offset from the START of the output |
| `$A0` | `00:91e5` | same, each byte XOR `$FF` |
| `$C0` | `00:9245` | back-reference, 8-bit offset back from the write position |
| `$E0` | `00:9245` | same, each byte XOR `$FF` |

Both back-reference forms share one loop at `00:9222` -- `$C0` computes its
read pointer and branches into `$80`'s body -- and they copy a byte at a time
*through the output*, so an overlapping run (offset 1, length 40) legitimately
repeats what it just wrote. Do not turn that into a memmove.

Output goes to `$7E8000,X`, X starting from `$000e`. Source is `DB:Y` from
`$000b`/`$0009`, and `00:926d` handles running off the end of a bank by setting
Y back to **`$8000`**, not `$0000` -- LoROM maps only the upper half of each
bank, so the byte after `$xx:FFFF` is `$(xx+1):8000`.

#### Verification

`SC_DECOMP_VERIFY=1` runs the C into a scratch copy of WRAM, lets the ROM run
untouched, and compares at the RTS. It changes nothing; it only reports. This
exists because of what the map generator cost: that generator looked plausible
and was wrong for a whole session, and what caught it was comparing against the
guest instead of eyeballing the output.

Result, across two unrelated sessions -- the overview-map load and a cold boot
through the intro:

    decomp: verified=13 mismatched=0
    cmd hits  00=6003  20=3705  40=3068  60=19  80=1431  a0=53  c0=2752  e0=0
    bank-wraps=2

Seven of the eight commands and the bank-crossing path are confirmed byte-exact.
**`$E0` has never been observed executing**, so it is unverified, and the fast
path *declines* any stream that uses it -- it decompresses into scratch, checks
the `$E0` counter, and hands the work back to the ROM rather than trusting code
no measurement has confirmed. A `declined` count appears in the report if that
ever fires. It has not yet.

#### Measured effect

Overview-map load, B pressed at frame 60:

| | map appears | frozen for |
|---|---|---|
| before | frame 210 | 150 frames (~2.5 s) |
| after | frame 151 | 91 frames (~1.5 s) |

At frame 161 the accelerated screen differs from the original map by **7 pixels
out of 57,344**, the residue being animation phase rather than content.

The cold boot is a stronger check, because the intro is a long animated
sequence. Cross-correlating the two runs frame by frame, the accelerated one is
**exactly 105 frames ahead throughout, at 0.0% pixel difference at every
matched point** -- identical content, 1.75 s earlier. `qualify` passes both
ways with matching `logic_changes` and `nmi_serviced`.

The other half of the load is the software renderer at `02:899b` (46%), still
interpreted. With both replaced the freeze should be a handful of frames.

#### The decompressor HLE must not skip PCs the game hooks

The first version of the `00:90dd` substitution entered at `90dd` and emulated
the RTS. It was byte-exact and it broke the Sylt scenario, because this project
hooks PCs *inside* that routine: `00:90eb` captures the source address into
`s_sylt_decomp_src`, and `00:9106` -- the `PLB` on the end-of-stream path --
calls `sylt_write_brief_tilemap()`. Jumping from the entry to the return
stepped over both, so Sylt's briefing and map swap silently never happened.
Reported from play; **no automated check could have caught it**, because Sylt is
this project's own addition and nothing in the qualify harness selects it.

The fix generalises, and it is the rule for every HLE here:

* **Enter after the ROM's own prologue**, at `00:90ee` -- DB set from `$000b`,
  X from `$000e`, `$0011` cleared, no stream byte consumed yet -- so `90eb`
  executes natively.
* **Leave by pointing PC at the ROM's own exit**, `00:9106`, instead of
  unwinding the stack by hand. The ROM runs its real `PLB`/`PLP`/`RTS`, the
  stack takes care of itself, and any hooked PC in between still fires.

Before replacing a routine, grep for hooks on PCs inside it. `02:899b` was
checked this way before any code was written: no bank-02 PC is hooked anywhere.

### `02:8b34` -- memoised rather than transcribed

The classifier is ~300 instructions of ladder with a hardware-divider path.
It is **not** ported to C. It is a deterministic function of the 10-bit tile id
plus `$0d49`, `$3e` and `$40`, so there are at most 1024 distinct answers and a
real city uses ~500. The ROM computes each one once; the result is cached; every
repeat skips the ladder.

That is exact *by construction* -- the numbers come from the ROM, not from a
reading of it -- which is a far better bargain than hand-porting a ladder whose
every branch is a chance to be subtly wrong. The map generator is the standing
warning: transcribed by hand, plausible, and wrong for a whole session.

Two things are never cached. Tile ids `$14`-`$25` are animated: `02:8b83`
increments `$0b3b` *as a side effect* and folds it into the table index, so the
answer legitimately differs call to call, and those always run the ROM. And the
whole cache is flushed when `$0d49`, `$3e` or `$40` change, since the ladder
branches on all three.

The skip enters at `8b36`, after the entry `REP #$30`, so the widths are
already what the ROM would leave; it exits via the real `RTS` at `8b96`. Same
shape as the decompressor's `9106` exit, for the same reason.

`SC_MAPCLS_VERIFY=1` caches but still runs the ROM and compares every call:

    mapcls: cached=8899 distinct=504 mismatched=0

8,899 predictions, 504 distinct tiles, zero mismatches. The ~2,600 uncounted
calls are the animated ids, which deliberately run the ROM.

### Where the overview-map load now stands

B pressed at frame 60; the map is drawn in a single frame at the end.

| | map appears | frozen for |
|---|---|---|
| stock | frame 210 | 150 frames (~2.5 s) |
| `00:90dd` on the host | frame 151 | 91 frames (~1.5 s) |
| plus `02:8b34` memoised | frame 129 | 69 frames (~1.15 s) |

**54% of the wait removed, with both substitutions verified against the guest
rather than judged by eye.** At frame 131 the result differs from the stock map
by 204 pixels of 57,344 -- animation phase, not content.

The remaining 69 frames are the parts of `02:899b` still interpreted: the
12,000-iteration outer loop, the bitplane transposer at `02:909d`, the address
calculation at `02:9136` (which uses the hardware multiplier with the NMI-mask
dance, once per 8-cell group), and the animated-tile classifier calls. Removing
those means replacing the whole loop in C, which needs the classifier in C too
-- so the memo cache does not compose with it, and it is a bigger job than
either step so far.

### `01:f11a` -- why moving objects die at the right edge

**Fixed 2026-09-22, and the reading below was only half right.** `01:f11a`
is the scroll shift: while the player pans, it moves every object sprite by
the pan step `$7C` between the game's four-frame updates, and a carry sets
the sprite's X high bit through `00:c22c` -- it hides the sprite, it does not
despawn anything. What really drops an object at the edge is its placement.
The objects are map objects with cell positions, put on screen in groups
every fourth frame by `00:bc3f`:

| object | flag | slots | placed by |
|---|---|---|---|
| `$00` | `$0A91` | 119-122 | `00:bd9c` -> `00:bf80` |
| `$04` | `$0A93` | 123 (one sprite, nudged -`$91`/+6 at `00:bd41`) | `00:bd15` -> `00:bf80` |
| `$08` | `$0A8B` | 109-112 -- slot 109 is the "train" of the report above | `00:bcc4` -> `00:bf80` |
| `$10` the plane | `$0A8D` | 113-116, tiles by heading `$0A9F` | `00:bdc8` -> `00:bf80` |
| `$14` the ship | `$0A95` | 124-127 | `00:bec9` -> `00:bf80` |
| `$18` | -- | 124-127 | `00:c713` -> `00:bf80` |
| the helicopter | `$0A8F` | 117 body, 118 rotor (8x8) | `00:be1c` |

Each group's update first parks its slots (`00:c0f5`, `00:c154`, which falls
into `00:c180` for 124-127), then calls `00:c019` once per 16x16 sprite:
cell (`$91`, `$94`) plus the object's fine offset (`$0A6D,Y`, `$0A6B,Y`),
relative to the view's top-left cell (`$01BD`, `$01BF`). `c019` returns
without writing when the cell is 32 or more columns right of the view -- so
from screen x 256 on, the slot simply stays parked, with its tile and
attributes still written by the object's code.

`src/sc_vehicles.c` keeps those sprites: at `c019`'s entry it computes the
position the game would, records the slot when the game is about to drop it
for being right of the view, follows the park, the scroll shift and the
`bd41` nudge, snapshots at the full NMI (`00:80c0`) with the OAM DMA, and
`host_map_compose()` draws the records into the margin from the guest's own
OAM tile, palette and VRAM. Checked on the ship (savestate 3 of 2026-09-22),
the plane and the helicopter (savestate 2, panned): whole, and moving by the
same 4 px a frame as the map on both sides of the edge while panning.

The same test turned up an older fault: the compositor's leading-edge cover
paints host terrain over the guest's last few columns during a pan, and took
the guest's sprites there with it, so a vehicle came apart at the edge. The
margin OBJ pass is now laid back from where that cover starts.

The original notes follow.

Reported from play: the locomotive and the selector pins are missing "only in
widescreen". That framing is right, and an earlier note here calling it
"culling" was too vague. The mechanism is an **8-bit overflow**, not a clip:

    01:f124  LDA $7e21b5,X     ; sprite Y in shadow OAM
    01:f128  CMP #$e0          ; parked? -> skip
    01:f12c  LDA $7c           ; per-frame delta
    01:f130  LDA $7e21b4,X     ; sprite X -- EIGHT BITS
    01:f135  ADC $7c
    01:f137  STA $7e21b4,X
    01:f13b  BCC $f175         ; no carry: still on screen, done
    01:f13f  JSL $00c22c       ; CARRY: X went past 255 -> despawn

So an object is destroyed at the moment its X would exceed 255. Measured
independently before the code was found: `SC_OAM_TRACK=1` across a 240-frame pan
shows slot 109 walking 200, 204, 208 ... 248, **252** at 4 px a frame and then
ceasing to exist, and of 43 slots seen near the edge not one ever holds an X
through 256.

The ROM's model has no room for the margin. X in shadow OAM is one byte; the
9th bit lives in the separate high-OAM table the ROM manages elsewhere, and
this routine cannot reach it. Nothing in OAM to reveal, so no decode setting,
hint or compositing change can help -- consistent with the marquee-lights
finding above, and with `SC_WS_OBJ_CLIP` and the right-hints both measuring 0 px.

**What a fix would take.** A host hook on the carry path at `01:f13b`: instead
of letting it despawn, keep the object alive and carry its X into the 9th bit
for the width of the margin, then despawn at 256 + extraRight. That is a
behaviour change, not a presentation one -- the same conclusion the marquee
lights reached -- and it has to cope with the ROM continuing to add to a value
it believes is 8-bit.

The selector's missing pins are the same family seen from the other end: the
ROM emits a pin per card it believes is on screen, so the columns widescreen
reveals get none. Recorded already as a known gap for the ninth card; it is the
same for the outer shipped ones.

## The train and the plane: what they actually are

**Correction (2026-09-22).** The plane is not a tile: it is object `$10`, a
four-sprite map object in slots 113-116, and the ship, the helicopter and
slot 109's object are the same kind -- see the table under "`01:f11a`"
above. The animated tile band described below is real; these vehicles are
not part of it.

Asked for as "decomp the train function and the plane function". There is no
such function, and finding that out took ruling out two plausible systems.

### Ruled out: `$0ced` is not a traffic table

`$0ced` is real -- 10 slots of 6 bytes, free marker `$ffff`, allocator
`03:c42a` taking a type in `A` -- but it holds **news characters**, not
vehicles. Every allocation site, with the event each posts through `03:be04`:

| site | type | event | |
|---|---|---|---|
| `03:ba3a` | `$0A` | `$23` | disaster block |
| `03:ba7a` | `$07` | `$09` | |
| `03:bb15` | `$0E` | `$0A` | |
| `03:bba3`, `03:bbf5` | `$0B` | `$20` | the monster, already known |
| `03:bc77` | `$0C` | `$21` | |
| `03:bd37` | `$14` | `$30` | |
| `03:bdf6` | `$08` | `$24` | |
| `03:c3f4` | `$13` | `$31`/`$2f` | population milestone |
| `03:c4cc` | `$01` | `$27`/`$26` | milestone, gated on pop >= `$7530` |
| `03:c624`, `03:c66d`, `03:c682` | `$00` | -- | tutorial setup |

The only per-frame consumer of the type is the drawer at `02:b66a`, which adds
`$10` and uses it as a graphic index. Nothing steps a position. The dialog
block confirms the cast: Bowser, earthquake, fire, flood, plane crash,
tornado, meltdown, shipwreck -- and **no train-crash message at all**.

### Ruled out: the `02:bc8f` sprite cluster

Draws ids `$0D`, `$0E`, `$0F`, `$10`, `$0C` at hardcoded coordinates
(`$80,$80`, `$88`, `$32`, `$74`). A fixed panel, not map objects.

### What they are: animated tiles

Both are **map tiles whose graphic index carries a rolling phase**, not
sprites and not entities. The whole mechanism is `02:8b34`:

```c
unsigned classify(unsigned cell) {
  unsigned tile = map[cell] & 0x03ff;          /* $7f0200,X */
  unsigned cls;
  if      (tile < 0x30)                cls = tile;   /* terrain maps 1:1 */
  else if (tile == 0x7f  ||                          /* monster stamp   */
           tile == 0x364 || tile == 0x365) cls = 0x28;
  else if (tile == 0x354 || tile == 0x355) cls = 0x01;
  else return other_ladder(tile);              /* 02:8b97 */

  if (cls >= 0x14 && cls < 0x26) {             /* the animated band */
    if (mode == 3 && scenario == 7) cls = 0x14;      /* pinned */
    else {
      anim = (anim + 1) & 0x0f;                /* $0b3b, 02:8b83 */
      cls += anim;
    }
  }
  return cls;
}
```

`$0b3b` advances **once per classified cell**, not once per frame. So
consecutive animated cells along a road or a rail receive consecutive frames,
and that rolling phase down a line of tiles is what reads on screen as a
vehicle travelling along it. Nothing holds a train's position because no train
exists as an object: classes `$14`-`$25` are the animated band, and the
apparent motion is an artefact of the counter's phase walking the cells.

**Why this matters for widescreen.** The counter is a side effect of
classification, so the number of cells classified sets the phase. The host map
renderer classifies the margins as well as the guest's columns, so its phase
runs ahead of the guest's, and a vehicle that reads as continuous inside the
authentic 256 need not line up across the boundary. That is the shape of
"shown just one tile and then it disappears suddenly", and it is a phase
problem, not a clipping one -- which is why the OAM work never found it.

### Still open

Which class in `$14`-`$25` is the train and which is the plane. The band is 18
entries and nothing here names them; the map is `tile id -> graphic`, and that
table has not been read. A capture with a train on screen, or the tile set in
`extracted_assets/`, would settle it in one step.

## The main menu, and a retracted claim about `00:98BB`

**Retraction.** An earlier commit described the table at `00:98BB` / `00:98F7`
as the main menu's strip table. It is not, or at least nothing here shows that
it is. It sits immediately after the `RTL` of `00:98A0`, which is what made it
look like that routine's data. Three tests say otherwise: its values run past
511, beyond the 512 tiles of the menu's artwork packet; decoded as indices into
that sheet they spell nothing; and its thirty nine-value runs appear as
consecutive tilemap cells in 2 of 30 cases -- the same 2 in the menu capture,
the in-game capture and the selector capture alike, which is the rate at which
consecutive runs occur by chance, not a match. Its role is unknown.

What is established about the menu:

| | |
|---|---|
| `00:98A0` | Confirmed by disassembly, but it is a state setter, not a drawing routine. Called as `PHP ; JSL $0098A0 ; <a> <b> ; PLP`, it reads the two inline bytes, steps the return address past them, and writes `dp[$03 + a] = b`. The same inline-operand convention as `03:a3cf`. The German build has it at `$009896`. |
| `$05ABF1` | The menu's BG3 tilemap -- 1024/1024 words against a live capture at VRAM `$3000`. **Byte-identical to the German ROM's copy** (`$05BB24`). Not a linear layout: it draws sheet rows in a permuted order, `[32, 96, 48, 112, 64, 128, 80, 144]`, pairing each text row with its shadow row. |
| `$04A571` | The menu's artwork, 4bpp, 16 tiles wide, tile index = row*16 + col. Rendering `$05ABF1` against it reproduces `RESUMESAVED` / `CITYPRACTICEAR` / `SCENARIO`. |
| `$04A65B` | The German counterpart. Rendering the **US** map against it gives clean German words -- `UEBUNGSSPIEL`, `GESPEICHERTE`. |

That last row is the interesting one. Both regions share the map, the German
artwork is correct against it, and yet swapping only the artwork produces
`GSSPIEL`, `STNESCHAUPUBUN`, `LATZE STADTUE` in play -- German words sliced at
English boundaries. So what differs between the regions is not the layout and
not the pixels but **where each line's window onto them starts**, and that is
neither in a packet nor in any text table.

`SC_VRAM_WATCH=<hex word addr>[+<count>]` exists to catch it: a per-frame diff
of a VRAM range with the frame and screen that changed it, plus every layer's
scroll as it moves. `SNESRECOMP_DMA_LOG` would have been the obvious tool and
is inert in this target -- `ppudma_record_dma` is stubbed off the AOT tier.

## A second message table: `00:859E`

Separate from the 53-record dialog block, and not previously mapped. A pointer
table at `00:859E` into a text block in bank `$01` around `$009980`. Bytes are
ASCII biased by `$80`, with a separate large-capital bank: a capital opening a
word is stored as ASCII-`$20`, so `SAVE` reads as `3AVE` under a naive decode.
`$00` is a space. German accents take the lowercase slots -- `d` is a-umlaut,
`t` o-umlaut, `a` u-umlaut, `{` eszett.

It holds the Save/Load and "please wait" text (`ONE MOMENT PLEASE...` /
`BITTE WARTEN...`, `UNABLE TO SAVE.` / `SPEICHERN NICHT MOEGLICH.`,
`SAVE COMPLETED.`, `GOOD BYE.`) and the HUD advisor lines -- traffic jams,
blackouts, fire and police department demands, the scenario countdown.

## Correction: `SC_LABEL_TRACE` never fires in this target

Commit 9ad16f5 added `SC_LABEL_TRACE` and described it as arming cpu_trace's
WRAM watch on the label sprites so that "a hit names the routine". It does arm
-- the watches report ARMED for every slot -- but it never fires, and neither
does anything else built on that machinery. Measured, not assumed: a watch on
`$7E:2000`, sprite 0's shadow slot, which the OAM captures show being written
constantly, produced no hits either.

The reason is structural. All of that instrumentation lives on the
AOT/CpuState write path -- `cpu_write8`/`cpu_write16` in `cpu_state.c` -- and
**this target executes through the interp816 core**, so those functions are
never called. The same applies to the two watchpoints reached by defining
`SNES_COSIM` (`SNESRECOMP_WRITE_WATCH` in `cpu_state.c`, `SNESRECOMP_WRAM_WATCH`
in `WatchdogCheck`): both compile and link here, and both stay silent.
`SNESRECOMP_WLOG_ADDR` fails for a different reason -- `wlog_addr_note_direct`
is live in an AOT build, but nothing in the pinned `snes/` sources calls it, so
the interpreter's writes never reach it.

So there is currently **no hook on the write path this target actually uses**.
Catching the routine that places the building labels needs one added to the
interpreter's own WRAM store in the submodule, reporting `g_interp816_cur_pc`
(interp816.c) -- which is the 65816 PC of the writing instruction, and exactly
the answer wanted. That is a submodule change, so it is left as a decision
rather than made in passing.

Two things from the attempt are worth keeping regardless: the CMake fix that
makes `-DSNESRECOMP_ENABLE_TRACE=ON` link for this target (it was inert here,
it only failed), and the knowledge that `SNES_COSIM` can be turned on for a
diagnostic build with three no-op stubs for the co-simulation entry points.

## Correction: `00:859E` is not a message table

d3d1aa1 recorded a "second message table" with a pointer table at `00:859E`
into a text block in bank `$01`. The text block is real. The pointer table is
not its index.

`$01:859E` is read at `01:B878` and `01:B89D`, both as
`ASL ; TAX ; LDA $01859E,X ; PHA`, and the walk that follows adds byte pairs
to `$0205`/`$0207` and rejects them against `#$78` and `#$64` -- 120 and 100,
the map dimensions. It is a table of **coordinate lists**, and it only looked
like a text index because it points into the same address range the text
happens to occupy. Records 0-5 decoded as sentences by coincidence of
overlap; 6-15 decoded as garbage, which should have been the tell.

What IS established about the status/advisor text:

| | |
|---|---|
| Where | `$009824..$009C9C` in the US, `$00981E..$009CD7` in the German -- about 1.1KB, same region in both |
| Encoding | byte − `$80`, then **two glyph banks**: codes under `$40` are the large bank and mean ASCII − `$20` (`$00` space, `$0E` `.`, `$01` `!`, `$33` `S`), `$41`-`$5A` are ordinary capitals. German accents sit in the lowercase slots (`d` = a-umlaut, `t` = o-umlaut, `a` = u-umlaut, `{` = eszett, `y` = O-umlaut) |
| Contents | the advisor lines (`MORE RESIDENTIAL ZONES NEEDED.`, `BLACKOUTS REPORTED.`, the scenario countdown), plus the Save/Load and shutdown text (`UNABLE TO SAVE.`, `ONE MOMENT PLEASE...`, `GOOD BYE!`) |

Decoded with the banks above the whole block reads cleanly in both languages,
digits and punctuation included, so the encoding is settled.

**How the game indexes it is not.** Not `00:859E`; not a fixed stride (the
gaps between terminators run 20 to 50 bytes); and not by counting `.`, because
the German `yFFENTL. VERKEHRSNETZ MU{ VERBESSERT WERDEN.` carries a period
inside one message -- splitting on terminators yields 38 messages for the US
and 39 for the German, and that extra one is the abbreviation. Translating
this block needs its reader found, the same way the building labels needed
theirs. SC_WRAM_WATCH is the wrong tool here -- this text goes to a tilemap,
not to a WRAM staging buffer -- so the equivalent probe would have to watch
the VRAM the message box draws into.

## `01:8F25` -- the building-label writer, and what limits a label

Decompiled in full, because its record format is the ceiling on how long a
translated building label can be.

```
01:8F25  REP #$30
         LDA $020d ; ASL ; TAX          ; the tool index
         LDA $018FC4,X ; PHA            ; that tool's record pointer
         LDY #$0000 ; TYX               ; Y = record cursor, X = OAM cursor
         PHK ; PLB                      ; DB = $01, the record's bank
 loop1:  LDA ($01,S),Y ; INY ; INY      ; packed position
         CLC ; ADC #$AF07               ; + base: y = $AF (175), x = $07
         STA $7E2088,X                  ; -> OAM slot 34
         LDA ($01,S),Y ; INY ; INY      ; tile + attributes
         STA $7E208A,X
         INX x4 ; CPY #$0014 ; BCC loop1   ; 20 bytes = 5 sprites
         LDX #$0000
 loop2:  ... same, STA $7E2160,X         ; -> OAM slot 88
         INX x4 ; CPY #$003C ; BCC loop2   ; 60 bytes = 15 sprites in total
```

So a record is **60 bytes, fifteen (position, tile) word pairs**: the first
five drive the price line at OAM slots 34-38, the remaining ten the label text
at slots 88-97. Every sprite carries its own x and y, which is why the y IS
the one-or-two-line decision, and why no table of tile runs exists to be
found -- see 0cfa447.

The limits, should a label ever need more room:

| | |
|---|---|
| `CPY #$003C` at `01:8F6F` | the record length. 60 bytes, hardcoded |
| `CPY #$0014` at `01:8F52` | where the price line ends and the text begins |
| ten sprites | the most a label can use, slots 88-97 |
| slots 96-99 | contended. An OAM capture shows another routine rewriting them every frame (the cursor), so in practice a label has **eight** dependable sprites, 88-95, and the shipped records blank the tail with tile `$0BF` |

Raising the ceiling therefore means three things together, not one: a longer
record, the loop bound to match, and OAM slots that nothing else claims. The
first two are easy; the third is the real constraint.

### A closed lead: `01:B79A` is not a text reader

Searching for code referencing the status text block turns up `01:B7B1`,
`LDA $9b51,X ; PHA ; LDA $9b59,X ; PHA ; LDA ($03,S),Y` -- the same
stack-relative walk the label and coordinate tables use, with operands that
land inside the block. It reads neither. `01:B79A` loads `$020d`, indexes
`$018040,X` and `$018051,X`, and bounds-checks the result against `#$0078`
(120, the map width): it is the building-placement coordinate checker, and
its `LDA $9b51,X` resolves through a `DB` of `$00`, not `$01`. Any search for
references into that block has to account for DB before it means anything.

## `05:9653` -- how the main menu places its text

The menu is reachable headlessly: hold ~8-10 seconds at the title, then Start.
That makes it measurable without a capture session, and it settles what
d3d1aa1 could not.

The text is NOT written to VRAM by the drawing code. `00:8D43` is a DMA
uploader -- a pending-mask in `$b7` selects among eight queued transfers, each
with its VMADD in `$0143,X`, source in `$0163,X` and size in `$0183,X` -- so a
VRAM write probe attributes every byte to the instruction that triggered the
transfer. The menu is composed in WRAM first, at `$7E:2840`, and DMA'd from
there.

`SC_WRAM_WATCH` on that buffer names the composer: **`05:9653`**.

```
05:962F  LDA ($79),Y ; ASL ; STA $7f    ; entry count -> byte length
05:9634  INC $79 ; INC $79              ; past the count
05:9638  LDA $79 ; LDX $34
05:963C  CLC ; ADC $7f ; DEX ; BPL      ; skip $34 records of $7f bytes
05:9642  STA $7c                         ; -> the chosen record
05:9647  LDA ($79),Y ; TAX               ; destination offset
05:964A  LDA $7e2840,X ; AND #$fc00      ; keep the attribute bits
05:9651  ORA ($7c),Y ; STA $7e2840,X     ; merge in this record's tile
05:9659  CPY $7f ; BNE                   ; one entry per destination
```

So a block is: **a count, then that many destination offsets, then one record
of tiles per variant**, with `$34` choosing the variant. `$79` is loaded by
`05:9611`; observed values are `$96AE`, `$970A`, `$97B0`, all bank `$05`.

`$05:96AE` reads `06 00` then `0BC2 0BC4 0BC6 0BC8 0BCA 0C0C` then
`0024 0025 0026 0027 0028` -- six destinations and the tile run the probe
caught being written. The structure decodes exactly.

### Why the artwork-only swap failed, and what a fix needs

3fb8871/d3d1aa1 established that the menu tilemap packet is byte-identical
across regions and only the artwork differs, so swapping the artwork put
German pixels at English positions (`GSSPIEL`, `STNESCHAUPUBUN`). The reason
is now visible: the **destination offsets and the tile records live together**
in these bank `$05` blocks, and a longer German word needs both -- more
entries and different tiles.

The blocks at `$96AE`, `$970A` and `$97B0` are identical between the US and
German ROMs, so they are not the words. Of the 133 differing bytes in
`$05:9600-$9C00`, most are 2-byte pointer shifts -- among them `$9126`,
`$913F` and `$9158`, which hold the compressed-packet addresses `$9224`,
`$942B`, `$966B` in the US and `$A15F`, `$A366`, `$A5A6` in the German, the
same packets paired in 3fb8871. The one substantial run, `$05:9B90` for 1110
bytes, contains CODE (`REP #$30 ; LDA $4e ; AND #$00ff`), so it cannot simply
be copied from the donor: the German code sits where it does because its data
moved.

What is left is to find which blocks hold the menu words, which is now a
bounded search of a known structure rather than an open question.

### `05:9653` is language-independent -- so it is not the menu's words

Follow-up measurement on the block structure above, and it narrows rather
than delivers.

`05:960E` is `LDA $9696,X ; STA $79`: the block pointers come from a table at
`$05:9696`, indexed by `$32`. Twelve blocks are used -- `$96AE $96E0 $970A
$9754 $9786 $97B0 $97DA $980C $9836 $9858 $9892 $98B4` -- captured by watching
`$0079` while the menu drew.

**All twelve blocks, and the pointer table itself, are byte-identical between
the US and German ROMs.** So this whole path is language-independent, and the
menu's words cannot come through it, however plainly it writes tiles into the
buffer the menu is DMA'd from.

That leaves the model incomplete rather than finished. The composer merges
tiles into `$7E:2840`, which uploads to VRAM `$5800` -- BG2's map on the menu
screen -- so it does draw part of that screen. Which part is not established:
rendering BG2's map against the CHR bases the sidecar reports does not
reproduce the menu options.

What is now excluded for the menu words: the tilemap packet (identical across
regions, 3fb8871), the artwork alone (swapping it scrambles the text in play,
d3d1aa1), and this composer path (identical across regions). The remaining
candidates are a second composer, or a different variant index reaching
different blocks in the German build -- `$32` and `$34` both come from
somewhere this has not yet traced.

## The main menu: its words are SPRITES (`00:8EA9`, table `$00:A164`)

Settled by a bsnes capture of the German ROM, which is the one thing this
project cannot measure for itself -- the recomp only runs the US image.

The menu's words are **sprites**: 16x16, two characters each. `UB` of
UEBUNGSSPIEL is char 98, `UN` char 100, and the umlaut dots are a separate
8x8 sprite at char 232. Nothing on any background layer carries them. That
retrospectively explains three dead ends, each of which was individually
correct and collectively misleading:

* the menu tilemap packet is byte-identical across regions -- it never
  carried the words (3fb8871)
* swapping the artwork alone scrambles the text -- German pixels landing at
  English sprite positions (d3d1aa1)
* `05:9653`'s composer is language-independent -- it draws the menu's icons
  and numerals, not its options (654b185)

Sprites mean shadow OAM, and `SC_WRAM_WATCH` on `$7E:2000` named the writer
immediately: `00:8EF2` / `00:8F2F` / `00:8F38` -- the **same shared sprite
emitter the building labels use**. Its entry is `00:8EA9`:

```
00:8EA9  REP #$30
         LDA $0261 ; ASL ; TAY
         LDA $a164,Y ; PHA          ; pointer table at $00:A164
         LDY #$0000
         LDX $0253                   ; OAM cursor
         LDA #$0008 ; STA $0251
         LDA ($01,S),Y ; STA $025b   ; flags, then walk the record
```

`$0261` selects the record -- ROM_MAP already noted it being read here "as a
jump-table selector (`ASL A`; index into `$00a164,Y`)" without knowing what
the table was.

**Every** entry of that table differs between the US and German ROMs, and the
records fill `$00:A100-$AFFF` as one continuous data blob. The recompiler
finds no code in `$00:A000-$AFFF` -- zero entry points -- so the region can be
taken from the donor whole. The table lives inside the copied span, so its
pointers stay valid.

Translating the menu therefore needs two things, and the artwork was always
only half:

```
--rom-copy 0x002100-0x003000   the sprite records and their table
--swap 0x04A571                the artwork the records index into
```

Verified by driving to the menu headlessly (Start after ~8-10 seconds at the
title) and rendering the captured OAM against the patched artwork: it reads
`- SCHAUPLAETZE -`, umlaut dots included.

### ...but `--rom-copy 0x002100-0x003000` breaks the title. Do not use it

Reported from play: title screen frozen, tiles corrupt. The recipe above is
wrong, and the reason matters.

`$00:A164` is not the menu's table. It is the **general** sprite-text table,
and `00:8EA9` serves every screen through it -- the first OAM capture of this
project caught `00:8EF2` writing on screen `$00`. Copying `$00:A100-$AFFF`
whole therefore replaces the TITLE's sprite records as well, with German ones
that index German artwork the title never loads. German records against
English tiles: corrupt tiles, and the title sits there.

That the recompiler finds no code in the region was necessary but not
sufficient. The region is shared across screens, and only the menu's half of
it has matching artwork installed.

`--qualify` does not catch this. Headless runs pass 1400 frames and reach the
menu, because the damage is graphical rather than a hang.

A correct fix has to narrow the copy to the records the MENU uses -- which
means finding the `$0261` values for the menu's entries, taking only those
table slots and their records, and relocating the records into free ROM so
their pointers can be repointed without disturbing the rest of the table.
Alternatively the title's own artwork could be swapped so its German records
match, but that widens the change rather than narrowing it.

## `00:8EA9` decompiled -- the shared sprite-text emitter

Every screen's sprite text goes through this: the menu's options, the title,
the building labels' price line. Decompiled in full so a replacement renderer
can be written against it.

```
00:8EA9  REP #$30
         LDA $0261 ; ASL ; TAY
         LDA $a164,Y ; PHA            ; record pointer, pushed for ($01,S),Y
         LDY #$0000
         LDX $0253                     ; OAM cursor, byte offset into shadow OAM
         LDA #$0008 ; STA $0251        ; sprite budget: 8
         LDA ($01,S),Y ; STA $025b     ; flags word
  loop:  LDA ($01,S),Y ; AND #$00ff    ; X byte
         LSR $025b ; BCC ; ORA #$0100  ; X high bit, shifted out of the flags
         CMP #$0100 ; BEQ done         ; terminator: X == 0 with its flag set
         CLC ; ADC $025d               ; + X base
         STA $7e2000,X                 ; OAM X
         ...                            ; high table at $7e2200: X bit 8 + size
         LDA ($01,S),Y ; ADC $025f     ; Y byte + Y base
         STA $7e2001,X                 ; OAM Y
         LDA ($01,S),Y                 ; tile + attributes
         STA $7e2002,X
         INY x2 ; INX x4
         DEC $0251 ; BEQ done ; BRA loop
```

**Record format**: a flags word, then up to eight sprites of
`X byte, Y byte, tile+attr word`. So 34 bytes at most. Ended either by the
budget in `$0251` or by an X of 0 whose flag bit is set.

**Parameters**: `$0261` record index, `$0253` OAM cursor, `$025d` / `$025f`
the X and Y bases, `$0251` the sprite budget.

Verified against a live capture: German record `$10` at `$A594` decodes to
tiles `$0C0`-`$0CE` along y=172, which is exactly the `SCHAUPLAETZE` strip the
OAM watch recorded.

### What this limits, and what it does not

Eight sprites per record is the ceiling, and the words are 16x16 sprites
carrying **two characters each** -- so sixteen characters per menu entry, and
the pairs are pre-rendered, not composable.

Raising `$0251` is NOT safe on its own: US record `$0D` uses all eight sprites
with no terminator, so a larger budget would run it off the end of its data.
Any change to the budget has to come with terminators added to every record
that relies on the count.

The artwork does carry an 8x8 alphabet -- sheet row 0 is `A`-`P` at tiles
0-15, row 1 `Q`-`Z` then `! ? - . ,` at 16-30 -- so per-letter rendering is
possible at 8x8 without any new artwork, at one sprite per character.

### Menu records cannot be copied from a donor at all -- the indices differ

c5665a1 relocated the donor's records for `$0C $0D $0F $10` instead of
copying the whole region, which fixed the title but broke the menu: no
option text, sprites in the wrong places, the title logo gone.

The indices do not mean the same thing in the two builds:

| index | US | German |
|---|---|---|
| `$0C` | 1 sprite, tile `$0C2` | 8 sprites, `$04C $04E ...` |
| `$0D` | 8 sprites, `$0B9 $12F ...` | 8 sprites, `$0AF $0B9 ...` |
| `$0F` | 8 sprites, `$0E0-$0E6` at y=44 | **1 sprite**, `$19E` |
| `$10` | 8 sprites, `$120-$12C` | 8 sprites, `$0C0-$0CE` (SCHAUPLAETZE) |

The German build reassigned them: its title strip lives at `$10` where the US
keeps a different element, and its `$0F` is a single sprite where the US has
an eight-sprite record. Dropping the donor's `$0F` over the US `$0F` replaces
an eight-sprite record with a one-sprite one, which is precisely the missing
logo.

So there is no index-wise correspondence to copy along, and pairing them by
what they draw would be guesswork against artwork that also differs. Both
donor routes are now closed: whole-region (breaks the title) and per-index
(breaks the menu).

**What remains is to author our own records**, keeping the US indices and
their meanings, with text rendered by us. That needs an 8x16 font, and the
8x8 alphabet in the artwork is a different, smaller face -- so the glyphs
have to be harvested from the existing word strips, whose contents are known.
The record format (7dd2072) is fully understood and the filler at `$00:FB4C`
is available to write into, so only the font stands between here and
arbitrary text.

## The menu font: 8x16, and already complete in the artwork

The menu's words are 16x16 sprites carrying **two characters each**, so a
character is 8 wide and 16 tall: two stacked 8x8 tiles, the top at tile `T`
and the bottom at `T+16`, one sheet row lower.

No harvesting from word strips is needed, which was the expectation. The
artwork at `$04A571` already holds the whole uppercase alphabet in exactly
that form:

| rows | tiles | contents |
|---|---|---|
| 0-1 | `$000`-`$00F` / `$010`-`$01F` | `A` to `P` |
| 2-3 | `$020`-`$02D` / `$030`-`$03D` | `Q` to `Z`, then `!` `?` `-` `.` |

And it is the **same face** the strips use, not a lookalike: composing
`RESUME` from these glyphs reproduces the `RESUMESAVED` strip at rows 4-5
byte for byte, all six characters, both halves. Verified rather than eyeballed.

So any string can be composed by copying glyphs into free artwork tiles --
`tools/text_tool.py`'s `menu_compose()` does it -- and `SCHAUPLATZE`,
`UBUNGSSPIEL`, `GESPEICHERTE STADT` and `NEUE STADT` all compose with no
missing characters.

Two things it does not cover. The alphabet has no umlauts or eszett: the game
draws them by placing a separate 8x8 dots sprite above the base letter (the
`Ue` of UEBUNGSSPIEL is char 232, spotted in bsnes), so a record composing
German has to carry that extra sprite too. And the 8x8 alphabet elsewhere in
the sheet is a different, smaller face -- an earlier lead, and the wrong one.

### Still open: which record draws which menu option

`$10` is the title **logo** -- its tiles `$120`-`$12C` are rows 18-19 of the
sheet, which is logo artwork, not text. That is why dropping the German `$10`
(their SCHAUPLAETZE) onto it blanked the logo in play. The records that draw
the option lines have not been identified: an OAM watch reports only changes,
and the menu's sprites are set on screen `$02` and persist unchanged into
`$03`, so a change-triggered capture of `$03` sees nothing.

## The main menu, mapped: `SC_OAM_DUMP`

`SC_OAM_WATCH` reports changes, which is the wrong shape for a screen that is
composed once and then sits there -- the menu sets its sprites on screen `$02`
and they persist unchanged into `$03`, so a change-triggered capture of `$03`
sees nothing. `SC_OAM_DUMP=<path>` with `SC_OAM_DUMP_ON=<hex $14>` and
`SC_OAM_DUMP_WAIT=<frames>` writes all 128 sprites once: index, x (with bit 8
from the high table), y, tile, attributes and size.

With that, the menu:

| element | y | sprites | tiles |
|---|---|---|---|
| Title logo | 23, 39 | 14, 16x16 | `$100`-`$10C`, `$120`-`$12C` |
| cursor / box | 55-71 | 15, 8x8 | `$0b9`, `$10e`-`$13f` |
| practice line | 112 | 5 | `$0c2` `$066` `$068` `$06a` `$06c` |
| start-new-city line | 136 | 7 | `$022` `$06e` `$0c4` `$0c6` `$0c8` `$062` `$064` |
| select-scenario line | 160 | 7 | `$0ca` `$0cc` `$0ce` `$0e0` `$0e2` `$0e4` `$0e6` |

Reading those against the artwork bands -- rows 4-5 `RESUMESAVED`, rows 6-7
`CITYPRACTICEAR`, rows 12-13 `- >T NEW SELECT`, rows 14-15 `SCENARIO` -- each
line is assembled from **character pairs borrowed across bands**: y=160 is
`EL` `EC` `T ` from rows 12-13 then `SC` `EN` `AR` `IO` from rows 14-15.

### Why the tiles cannot simply be overwritten

The bands are **shared between lines**. `$0c8` (` S`) serves y=136 while
`$0ca` onward serves y=160, both out of the same rows 12-13 band. Rewriting a
tile in place would change every line that borrows it.

So composing translations means **authoring new records** that point at freshly
composed tiles, not editing the tiles the shipped records use. Everything for
that is now known: the record format (7dd2072), the 8x16 alphabet and
`menu_compose()` (1a0c97e), the filler at `$00:FB4C`, and the on-screen
geometry above. What must NOT be done is reusing a donor's records -- the
indices mean different things in each build (449bea6).

## `02:BC94` -- the menu's draw caller, and which record gets which base

Traced by watching the emitter's own parameters (`$025d`, `$025f`, `$0261`)
with `SC_WRAM_WATCH`, which names the writing instruction. Everything else in
the log is the emitter writing its own scratch; the caller is bank `$02`.

```
02:BC94  LDA #$0080 ; STA $025d ; STA $025f   ; bases 128, 128
02:BC9D  LDA #$000d ; STA $0261 ; COP         ; record $0D -- box / cursor
02:BCAA  LDA #$0010 ; STA $0261 ; COP         ; record $10 -- title logo
02:BCB7  LDA #$0088 ; STA $025d               ; x base 136
02:BCBD  LDA $44 ; BEQ $bcd0                  ; saved-game flag
02:BCC1    LDA #$000e ; STA $0261 ; COP       ;   record $0E, then falls into
02:BCCE    BRA $bcd6                          ;   $0F as well
02:BCD0  LDA #$0074 ; STA $025f               ; y base 116
02:BCD6  LDA #$000f ; STA $0261 ; COP         ; record $0F -- option lines
02:BCE7  LDA #$32   ; STA $025d               ; x base 50
02:BCF1  LDY $44 ; ... ; LDA $d37c,X ; STA $025f  ; y from a table on $3e/$44
02:BD01  LDA #$0c   ; STA $0261 ; COP         ; record $0C -- cursor arrow
02:BD0D  RTL
```

The emitter is reached by `LDA #$0002 ; COP #$00`, which is the COP dispatch
`$0261` was already noted as feeding.

Bases confirm the geometry exactly. Record `$10` at (128,128): its `y=167`
lands at `(167+128)&255 = 39` and `y=151` at `23` -- the logo's two rows.
Record `$0F` at (136,116): `y=44` lands at `160` (the scenario line) and
`y=252` at `112` (the practice line), so **one record draws two option
lines**, four sprites each.

When `$44` is non-zero -- a saved game exists -- `$0E` is drawn as well and
execution falls through into `$0F`, so both appear.

### Why German still does not fit

`ÜBUNGSSPIEL` and `SCHAUPLÄTZE` are 11 characters each, six 16x16 sprites
apiece, and they share record `$0F`'s budget of eight. Twelve will not fit.

Raising the budget is not available: `$0251` is set to 8 inside the emitter
(`00:8EBA`), and **19 of the records end on that count rather than on a
terminator**, so a larger budget would run every one of them off the end of
its data. Relocating and terminating 19 records is a bigger change than the
problem warrants.

What is left is to restructure which record draws which line -- which is
exactly what the German build did, and why its indices do not match the US
ones. The caller is short, straight-line and now fully mapped, so adding or
re-pointing a call is tractable; it is ROM code patching rather than data,
which is a different risk class from everything done so far.

### Record `$0F` owns SCENARIO and PRACTICE -- proved by marking it

Blanking `$0F` (setting its first sprite's X to 0 with the flag bit, so the
emitter terminates at once) removed all three option lines, but that was
misleading: with `$0F` drawing nothing, the OAM cursor never advances and
later records land in its slots.

Marking instead is decisive. Setting all eight of `$0F`'s tile words to
`$1ff` and snapshotting shows exactly which sprites are its:

```
 29  179 160 $1ff      33  122 112 $1ff
 30  163 160 $1ff      34  106 112 $1ff
 31  147 160 $1ff      35   90 112 $1ff
 32  131 160 $1ff      36   74 112 $1ff
```

Four sprites are `SCENARIO` at y=160 and four are `PRACTICE` at y=112. The
rest of each line -- `SELECT` before the first, `START NEW CITY` entirely, and
the `>` arrow -- comes from records not yet identified.

### And rows 2-3 are NOT free artwork

Composing `UBUNG` and `SZENARIO` into rows 2-3 and repointing `$0F`'s tiles
renders both correctly in play. It also breaks the line above them:
`START NEW CITY` becomes `UNART NEW CITY`, because that line draws tile `$022`
-- inside rows 2-3.

The earlier scan that called rows 2-3 "entirely unreferenced" only walked
records reachable from `$00:A164` with pointers in a plausible range. The
record behind `START NEW CITY` is not among those, so its tiles never entered
the used-set. **A free-space claim about this artwork cannot be made by
scanning the table**; it has to be made from observation -- mark a candidate
tile, run, and see whether anything on any screen changes.

That is the outstanding blocker for composing menu text: not the font, not the
record format, not the caller, all of which are now understood -- but knowing
which tiles are genuinely spare.

### The marker sweep: which artwork tiles are genuinely spare

Scanning the record table cannot answer this -- it called rows 2-3 free and
composing there broke `START NEW CITY`. The answer has to come from what is
actually on screen.

`SC_OAM_DUMP` on every screen that loads `$04A571`, taking the union of the
tiles displayed (16x16 sprites expanded to their four tiles, parked and
off-screen sprites excluded):

| screen | distinct tiles displayed |
|---|---|
| `$01` title | 48 |
| `$02` transition | 53 |
| `$03` menu | 141 |
| `$0f` in game | 7 |
| **union** | **178 of 512** |

Rows 20-31 appear on none of them. That this packet is only resident on the title and menu screens, so that
nothing else could be looking at it, was wrong: the scenario selector and the
new-city screens unpack it too. See "The menu's artwork is shared" below. Rows 30-31 are also blank in the artwork, which makes them the safest
choice of the free bands.

Composing `UBUNG` and `SZENARIO` there and repointing record `$0F` gives, in
play, `> UBUNG` / `START NEW CITY` / `SELECT SZENARIO` -- the two translated
lines correct and the third **intact**, which is the check the rows 2-3
attempt failed.

That was the first pass, and it capped a line at eight characters because it
only repointed the four sprites record `$0F`'s first chunk draws there.
Decompiling the emitter lifted the cap -- see "Translating the menu into any
language" below, where `--menu-text` takes all three lines and 36 characters.

### Why the menu lines are mixed, and where that stops

In play the scenario line reads `SELECT SZENARIO` -- one English word, one
German. Reported from play, and the cause is that a menu LINE is not one
record.

Marking every record in `$00:A164` with a unique tile and snapshotting the
menu attributes each on-screen sprite:

| sprites | record |
|---|---|
| y=112, x 74-122 | `$0F` |
| y=160, x 131-179 | `$0F` |
| y=112, x 50 (the arrow) | `$0C` |
| logo, box | `$10`, `$0E`, and others |
| **y=136 all, and y=160 x 74-106** | **unmarked -- not from the table at all** |

So `START NEW CITY` and `SELECT` are drawn by the same emitter but from
records reached without the table, through a second entry point that takes a
pointer directly. Searching the ROM for their tile sequence finds
`START NEW CITY` at **`$00:A3F4`**, immediately after `$0F`'s record, whose
six sprites decode to screen (154,136) through (74,136) exactly.

`SELECT` was not found the same way: the tile sequence `$0CA $0CC $0CE` does
match at `$00:A5AE`, but that record's sprites decode to (119,32) and (103,32)
under the menu's bases, so it is a different element and the match is
coincidental. Bases vary per call, which makes tile-sequence search
suggestive rather than conclusive.

What this means practically: a record's tiles can be repointed without knowing
its caller -- that is how `$0F` was translated -- so `$00:A3F4` is directly
translatable too. But covering a whole line means finding every record that
contributes to it, and the search has to be confirmed by decoding positions,
not by the tile sequence alone.

**Retracted in part.** "Records reached without the table, through a second
entry point" is wrong: there is no second entry point, and `$A3F4` is not a
record. The emitter runs on past its eight-sprite budget into the next chunk
of the SAME record, so all three lines belong to `$0F`. The next section has
the decompilation.

### The menu's sprite-text emitter, decompiled

`$00:8EA9` is COP function 2, and it is what draws every line of the main
menu. Decompiled in full it settles the questions the tile-sequence searches
above could only guess at.

```
00:8ea9  REP #$30
00:8eab  LDA $0261 / ASL / TAY / LDA $a164,Y / PHA   ; record pointer on the stack
00:8eb4  LDY #$0000
00:8eb7  LDX $0253                                   ; OAM byte cursor
00:8eba  LDA #$0008 / STA $0251                      ; eight sprites a chunk
00:8ec0  LDA ($01,S),Y / STA $025b                   ; the FLAGS word
00:8ec5  INY / INY
00:8ec7  TXA / LSR x4 / AND #$fffe -> $0255          ; high-table word index
00:8ed2  TXA / LSR / AND #$000e   -> $0257           ; which sprite in that word
00:8eda  LDA ($01,S),Y / AND #$00ff                  ; the entry's X byte
00:8edf  LSR $025b / BCC / ORA #$0100                ; flag bit -> X bit 8
00:8ee7  CMP #$0100 / BEQ $8f4d                      ; X=0 with its flag set: end
00:8eec  CLC / ADC $025d                             ; + the X base
00:8ef2  STA $7e2000,X                               ; low byte -> OAM
00:8f00  AND #$0100 ...                              ; bit 8 -> the high table
00:8f11  LSR $025b / BCC / ORA $8f62,Y               ; next flag bit -> size
00:8f19  STA $7e2200,X
00:8f25  LDA ($01,S),Y / CLC / ADC $025f             ; Y byte + the Y base
00:8f36  LDA ($01,S),Y / STA $7e2002,X               ; tile + attribute, verbatim
00:8f42  DEC $0251 / BEQ $8f4a
00:8f47  JMP $8ec7                                   ; next sprite
00:8f4a  JMP $8eba                                   ; budget spent: NEXT CHUNK
```

Three things follow, and each of them was a blocker before.

**A record is a chain of chunks, not a 34-byte blob.** `$8F4A` does not
return. It jumps back to `$8EBA`, which reloads the eight-sprite budget and
reads a *fresh* flags word from the next two bytes. So a record simply carries
on, eight sprites at a time, until a chunk terminates. Record `$0F` is
therefore one record of three chunks -- `$A3CE` (8 sprites), `$A3F0` (8) and
`$A412` (2 and the terminator) -- covering **all eighteen** text sprites of
all three option lines.

That retracts the reading above. `$00:A3F4` is not "a record reached through a
second entry point": there is no second entry point. Nothing points at `$A3F0`
or `$A412` because nothing needs to. The 16-bit value `$A3F4` appears once in
the whole ROM, in unrelated data, which is exactly what the chain predicts.

**The flags word is two bits a sprite, and the first is part of X.** Bit `2i`
is OR'd into the entry's X byte as bit 8 *before* the base is added; bit
`2i+1` is the 16x16 size bit. For an on-screen sprite the first bit has to be
whatever carry the 8-bit sum produces, so it is really a sign extension: entry
byte `$C2` plus base 136 is `$14A`, and only the flag bit, making it `$24A`,
keeps bit 8 of the result clear.

Writing new X bytes and leaving the US flags word alone puts sprites 256 pixels
to the right. That happened on the first run of the new generator: five of the
six sprites of the top line sat at x = 330..410, one of them by luck at 122.
The x-high bit is written from the sum at `$8F00`, so it cannot be left to the
old value.

**The terminator is one byte, not one entry.** `CMP #$0100 / BEQ` at `$8EE7`
tests the X byte together with its flag bit, and the record ends there. Record
`$10` is pointed at `$A41D`, immediately after the `$00` at `$A41C` that ends
the `$A412` chunk -- which is how the one-byte length was confirmed, and it is
also why the pool cannot grow past eighteen in place -- though it can be
grown by moving `$10` out of the way, which a later section does.

### Translating the menu into any language

Because each entry carries its own X, Y and tile word, the eighteen sprites
are a free pool: any of them can be given to any line. The US split of
4 / 7 / 7 is not fixed by anything, and that is what lifts the eight-character
cap. Eighteen 16x16 sprites, two characters each, is 36 characters across the
three lines in any split, and **48** once the pool is grown to 24.

They were located by signature rather than by following pointers: an entry
stores X and Y as offsets from the caller's base (136, 116), so searching the
record region for the three bytes of a sprite seen on screen finds its entry.
Eighteen of the nineteen sprites on the option lines resolve uniquely and
contiguously, at `$00:A3D0` through `$00:A418`. The nineteenth is the cursor
arrow, drawn from base (50, 112) by record `$0C`, and it is left alone.

The generator writes, per publish: every entry's four bytes, every chunk's
flags word, and the composed glyphs. Spare sprites are pointed at a blank
pair, so a short set does not leave a fragment of `SCENARIO` on screen.

```
text_tool.py packets --menu-text "UEBUNGSSPIEL|NEUE STADT|SCHAUPLAETZE"
```

| line | US | sprites |
|---|---|---|
| y=112 | `PRACTICE` | 4 |
| y=136 | `START NEW CITY` | 7 |
| y=160 | `SELECT SCENARIO` | 7 |

German needs 6 / 5 / 6 and leaves one spare; French
(`ENTRAINEMENT` / `NOUVELLE VILLE` / `SCENARIOS`, with the accents) needs
exactly 18 and leaves none.

#### Accents, without a sprite to spend on them

The font has no accented letters, and there is no room to add a sprite for the
marks -- the German cartridge draws the dots of `UEBUNGSSPIEL` as an extra 8x8
sprite at y=104, and this pool has no spare entry for one.

So the mark is composited into the character cell. The cell is 8x16 and the
letters fill all sixteen rows, but they are drawn as a vertical colour ramp,
so two rows can come out of the middle without changing the shape: the squash
drops the rows that differ least from the row above, which lands on the plain
vertical strokes every time. The letter keeps its apex and its base, loses two
rows of ramp, and the mark goes in the space that frees up, shaded like the
rows it replaced. Diaeresis, acute, grave, circumflex, tilde, ring and cedilla
are built this way, covering the Latin-1 letters.

#### What the check has to cover

Two failures earlier in this file came from checking too little, so the
verification is fixed:

1. The title screen's OAM must be **byte-identical** between a plain and a
   patched run. Screens `$01` and `$02` both are.
2. **All three** option lines must be rendered and read, not just the ones
   expected to change. Reading them back from a live VRAM capture and matching
   each 8x16 cell against the font gives `UEBUNGSSPIEL` / `NEUE STADT` /
   `SCHAUPLAETZE` at a bit distance of zero.

The live OAM also confirms the free bands directly, and more cheaply than the
marker sweep did: the highest tile any sprite references on `$01`, `$02` or
`$03` is `$13F`, the last tile of row 19. Rows 20-31 are unused on those
screens -- and only on those. The scenario selector loads the same artwork and
draws its win marks from rows 27 and 29; see "The menu's artwork is shared"
below.

### Growing the pool to 24, and the French import

Eighteen sprites is enough for German. It is not enough for French: the
French cartridge's own wording is `ENTRAINE-TOI` / `NOUVELLE CITE` /
`CHOISIS SCENARIO`, unaccented, and it spends **20** sprites on it -- read
straight off that cartridge by capturing its menu and matching each 8x16 cell
against the font, which returns all three lines at a bit distance of zero.

So the chain grows. `$0F` ends at `$A41C` only because record `$10` starts at
`$A41D`, and `$10` is reached **only** through the table at `$00:A164`. A
record's entries are self-contained -- x, y, tile, attribute, no internal
pointers -- so its whole 61-byte chain copies verbatim into the bank-0 filler
at `$00:FB4C` and the table entry at file `$002184` is repointed at the copy.
That frees `$A41D` onward, and the chain becomes:

| chunk | was | now |
|---|---|---|
| `$A3CE` | 8 sprites | 8 |
| `$A3F0` | 8 sprites | 8 |
| `$A412` | 2 + terminator | 8 |
| `$A434` | record `$10` | flags word + terminator |

**24 sprites, 48 characters.** The filler is the 1140 bytes of `$FF` ending at
the cartridge header, and nothing else in this repo uses it: the one thing
that ever wanted it, Truttle1's powered-cell patch, is implemented host-side
here precisely so that no ROM space is needed.

Record `$10` is the logo, which has broken this screen before, so the check is
the strict one. Against a plain US run the patched menu differs in exactly 25
OAM slots: the 24 pool sprites and the cursor arrow, which moves six slots
later because `$0F` now emits six more sprites before it. Every other slot,
the logo included, is byte-identical, and so is the whole of screens `$01` and
`$02`.

One trap the growth introduced. The sprites past the original eighteen sit on
bytes that used to be record `$10`, so inheriting the attribute byte per
sprite -- which worked while the pool was eighteen -- gives the last sprites of
a long line the wrong palette. French would have drawn its final two sprites
at attribute `$18` instead of `$30`. The pool takes one attribute, from the US
line text, for all of it.

#### A cell can need translating without its entry changing

The donor is free to reuse a tile index for a different glyph, and it does.
San Francisco's first disaster line is tiles `$0BE`..`$0C3` in both ROMs --
`Earthquake` in the US one, `Tremblement` in the French one, at the same
indices. Taking artwork only for cells whose tilemap ENTRY changed left that
line in English while the second line, which the donor does move, came out
French: the card read `Earthquake de terre`.

So the artwork of an unchanged cell is taken too, but only inside the card
rectangles, and only when no cell outside them shares the tile. The screen
around the cards is left alone, and so is Sylt's card, which the host composes
after this packet.

German never showed this, because the German cartridge moves those cells to
different indices: its count of reused tiles is zero. French has six.

One thing the French cards do not get: Sylt's disaster line reads
`Inondation` where Rio's reads `Inondation cotiere`. Sylt takes a single
six-tile strip from the donor, which is a whole word in German
(`Hochwasser`) and only the first line of two in French.

#### Words are packed whole

Both cartridges lay their lines out by word, not by character, and it is worth
copying: a word of n letters takes ceil(n / 2) sprites, and an odd-length word
leaves its last half blank, which *is* the space before the next word. Only
after an even-length word does the space cost anything, and then it costs 8
pixels of position rather than a sprite. `NOUVELLE CITE` is 6 sprites that
way and 7 laid out densely.

Checked against the French cartridge, which is the one that spends carefully:
`NOUVELLE CITE` comes out at offsets 0 16 32 48 72 88, exactly its own, and
`CHOISIS SCENARIO` within a pixel of its own.

#### The French build

```
text_tool.py import  --donor fr.sfc --briefs --out translation_fr.bin
text_tool.py packets --donor fr.sfc --hud \
    --menu-text "ENTRAINE-TOI|NOUVELLE CITE|CHOISIS SCENARIO" \
    --out translation_fr_selector.scpk
```

53 messages and 12 briefing pages, 90 scenario-card cells with none left in
English, 78 building-label tiles and 87 repositioned placement sprites. It
qualifies clean over 1200 frames, and all three menu lines read back out of
live VRAM at a bit distance of zero.

Checked: the menu and the scenario cards, by rendering them from live VRAM
captures -- all ten cards French, all three menu lines at a bit distance of
zero. The building labels were checked a different way, because they are
sprites sliced out of a sheet and reaching the toolbar takes a played game:
for every tile the PATCHED placement records slice, the patched sheet is
compared against the donor's. 83 tiles for French, 92 for German, none
differing.

That check is worth keeping. It is what shows that the `Nuclear` still
sitting in the French sheet is harmless: tiles `$1B0`..`$1B5` are dead space
in the French cartridge, which left the US bytes there, and no French
placement record references them. German does reference them, and translates
them -- which is the same six tiles the `$036200` truncation once lost.

### The message box is 24 characters wide, and German text is written for 25

Reported from play: the Dr. Wright intro came out as `Hallo! Ich bin Dr. Wrigh` /
`tund Du mußt der neue` / `  Bürgermeister sein. Ha` -- every line one
character short of the cartridge's, so the text slides further out of step
with every row.

A message record is a flat grid, not a string with line breaks. The renderer
writes a fixed number of characters, then skips to the next tilemap row, and
the runs of spaces inside a record are what pad each line out to the edge:

```
01:e592  LDA $0397 / ASL / TAX
01:e597  LDA $0fa800,X / TAX          ; the message pointer
01:e59f  LDA #$0018 / STA $79         ; 24 characters a line
01:e5a4  LDA $0f0000,X / AND #$00ff   ; one character
01:e5ab  CMP #$00ff / BEQ             ; $FF ends the record
01:e5b0  ORA #$0800                   ; its tile attribute
01:e5b5  STA $7e3948,X                ; into the tilemap shadow
01:e5bd  DEC $79 / BNE                ; until the line is full
01:e5c1  TYA / CLC / ADC #$0010 / TAY ; then skip 8 words to the next row
01:e5c7  BRA                          ; and start another line
```

24 characters plus 8 words is 32 words, one tilemap row. The German and French
cartridges run the same routine at the same address with `LDA #$0019` and
`ADC #$000E`: 25 plus 7, the same 32 words.

| region | columns | skip |
|---|---|---|
| US, EU | 24 | `$0010` |
| French, German | 25 | `$000E` |

Measured as well as read, because a table like that is worth checking: wrap
every record at each candidate width and count the boundaries that fall inside
a word. On the US records 24 scores 75 of 496 and the next best is 161; on the
French ones 25 scores 4 of 491. The residue is line-end hyphens and the
records that are not prose.

So the fix is two operand bytes, carried as ordinary cart spans in the packet:
`$01:E5A0` and `$01:E5C4`. `--columns N` sets it, and on the donor path it
**defaults to the donor's own measured width**, since the text this packet is
paired with came from that donor. Forgetting a flag is how this would come
back.

The German record 27 wrapped at 24 reproduces the broken screenshot character
for character, and at 25 reproduces the cartridge's own line breaks:

```
Hallo! Ich bin Dr. Wright     Hallo! Ich bin Dr. Wrigh
und Du mußt der neue          tund Du mußt der neue
Bürgermeister sein. Hab'        Bürgermeister sein. Ha
ich recht? Laß uns doch       b' ich recht? Laß uns do
        at 25                          at 24
```

Reflowing the text to 24 instead was the alternative and is worse: it would
have to guess which line-end hyphens are soft (`Ver-` + `binde` is one word,
`Wohn-` before `und` is not), and it adds a line per paragraph to a box of
fixed height.

### The menu's artwork is shared, so its glyphs are applied on the menu only

Reported from play: on the scenario selector, the red X marking a won scenario
came out as coloured fragments of `STADT`.

The sprite artwork at `$09:A571` is not the menu's alone. Tracing every unpack:

| screen | when |
|---|---|
| `$02` | into the menu: from the title, back from the selector (X), back from the new-city screens |
| `$04` | the new-city screens |
| `$0a` | the scenario selector |
| `$12` | into the menu from a city (GOTO MENU); the same loader, `$02:BB23` |

The packet patch matched entries by source address only, so the glyphs composed
for the menu were laid over every one of those unpacks. The selector draws each
win mark as sprite-text record `$29` -- `03:DED0` sets the emitter's base from
`$DF30`/`$DF20` and calls COP 2 with `$0261 = $29` -- and that record's four
sprites are tiles `$1B0 $1B2 $1D0 $1D2`. The generator had written glyphs into
`$1D0` and `$1D2`.

The earlier observation was true as far as it went: rows 20-31 are unused on the
title and menu. What did not follow was that they were free, because the marks
only draw once a scenario has been won and no capture had any wins. Walking every
record in the table with the chunk rule shows the "free" bands referenced
throughout, and only 8 two-by-two units in the whole sheet are referenced by no
record at all, against the 24 the pool needs. Moving the glyphs elsewhere was
never an option.

So a packet entry can now name its screens. SCPK v2 adds a count and a list of
screens after each entry's address, and the runtime applies such an entry only
while `$14` is one of them. An empty list means every screen, which is what a v1
file means, and v1 files still load that way. The menu artwork entry is written
for `$02` and `$12`, the two screens the menu's own loader runs on.

Checked:

- A selector unpacked fresh after the menu had applied its glyphs matches the
  original artwork on all 512 tiles.
- On the `savestate_1` path, where the marks draw (28 sprites, seven wins), the
  patched selector's sprite art and OAM are byte-identical to a run with no
  translation and no packet.
- Entering the new-city screens no longer applies the glyphs.
- The German and French menus still read back at a bit distance of zero, with
  their sprite tables unchanged.

### The accent glyphs moved out of the notice table's punctuation

Reported from play: the in-city "Save completed." dialog ended in a wrong
character.

The accent glyphs of a translated message font went at their CP437 codes,
`$81`..`$9B`, because the US message records never use those codes. That was
true and not enough. The in-city notice table in bank 01, `$01:9824`..`$01:9C9C`
("More Residential zones needed", "Save completed."), draws from the same font
with every character stored as its code plus `$60`, so its space, punctuation
and digits are `$80`..`$9F`. The German blob's accents overwrote `!`, `,`, `.`,
`3`, `4` and `7` there; the wrong last character was the full stop at `$8E`.

Tiles `$E0`..`$FE` of the US font are blank and used by neither the messages
nor that table, so the accents now go there in order, and the translated
records are rewritten to point at them. German's 203 and French's 276 accented
message bytes all moved; the blobs are the same size.

Checked inside a city entered fresh, so the font was unpacked with the new
blob: the notice table's `.` `!` `,` `3` and the message full stop are the
original glyphs, and all 16 German accents sit at `$E0`..`$EF`.

The notice table itself is still English; it is not among the 53 messages.

Scoping to `$02` alone was one screen short. Reported from play: after going
back from a city to the main menu the German lines were missing. Traced in
that session: GOTO MENU passes through game state `$12`, which runs the same
menu loader at `$02:BB27` and unpacks the artwork while `$14` is `$12`, so the
entry skipped it. A save state taken nine frames before that transition
reproduces it headless.

## In-city notices: reader, format, and import

The two-line boxes over the city ("More Residential zones needed.",
"Blackouts reported.", the scenario countdown, "Save completed.") are not among
the 53 messages. Their reader is at `$01:9C9D`, byte-identical in all four
cartridges:

```
01:9c9d  LDA $0381 / ASL / TAX / LDA $0197E3,X / STA $79   string pointer
01:9ca8  LDA #$0610 / STA $7C                              tilemap offset
01:9cad  LDA $0381 / TAX / LDA $0194FB,X / AND #$FF / TAX  width class
01:9cb9  LDA $01978C,X / AND #$FF / SEC / SBC #2 / STA $7F characters a line
01:9cc6  LDA #2 / STA $82                                  two lines
01:9cd2  LDA $010000,X / AND #$FF / ORA #$2C00 / STA $7E3840,Y   byte = tile
```

This settles what the correction above left open: the index is `$0381`, the
pointer table is `$01:97E3` (33 words), and the text is not terminated at all --
each notice is exactly two lines of its class width.

| | |
|---|---|
| width classes `$01:978C` | 14 17 21 25, minus 2 = 12, 15, 19, 23 characters a line; the same in all regions |
| class per notice `$01:94FB` | also read by the box frame at `$01:9797`; German and French change 19 and 16 of them |
| byte | the tile number. The font holds a recoloured copy of its glyphs `$60` up, so a notice byte is the CP437 code plus `$60` (German ue `$E1`, ss `$FB`) |
| written by | `$01:9473`, `$03:B020` (26), `$03:CB37` (31), `$03:CBD5` (30), `$00:D2C7` (32) |

`text_tool.py template` exports them as `notices` (id, width, two lines),
`translate` imports them, and `packets --notices` takes the donor's. The text
runs up to the reader, so a translation cannot stay in place: it goes to the
`$FF` filler at `$01:F924` (1756 bytes) and all 33 pointers and the class table
are rewritten. German needs 1224 bytes, French 1274.

Accented letters need their own slots. The notice copies of the glyphs sit
exactly where the message font's accents now live (`$E0`..`$EF`), so a notice
accent takes a slot from `$F0`..`$FE` and its glyph comes from the donor's own
notice bank, already in the notice colours, as a packet span on the in-city
font `$09:C0FB`.

Checked: the US notices round-trip identically through export and import;
the German and French imports decode back to their cartridges on all 33
notices with identical classes; in a freshly entered German city the five
notice accents are at `$F0`..`$F4`, the notice bank `$80`..`$DF` is untouched
and the 16 message accents are intact. Not yet seen: a notice drawn on
screen -- none fired in 2600 frames of a new city, and nothing headless can
set `$0381`.

## Report screens: budget, evaluation, overview, events

Pictures, not text: one 2048-byte tilemap per screen over a shared 2bpp tile
set, found by matching live captures against every packet in the ROM.

| screen | tilemap | load site |
|---|---|---|
| budget | `$0B:BF0E` | `$02:A36C` |
| evaluation | `$0B:C0C9` | `$02:A441` |
| overview | `$0B:C29F` | `$02:A4A2` |
| events | `$0B:C488` | `$02:B626` |
| tile set | `$09:875C` | `$02:A132` (reports, `$14 = $00`), `$03:DF77` (briefing, `$14 = $0C`) |

The German and French cartridges keep the four screens in the same order,
so their tilemaps are the four 2048-byte packets from the budget map's twin.
Byte agreement alone pairs them wrongly (three US maps matched one German
one), because the donors number their tiles differently.

The tile set cannot be swapped whole. It differs on 714 of 1024 tiles, and
the game draws words from it at runtime at tile numbers fixed in the US code:
the city category is one ("Metropolis" is `$197`..`$19D`, a fragment in the
German set). So the import changes only what the picture changes. A cell's
tile is redrawn in place if no other cell uses it; otherwise an identical
untouched tile is reused, or a free one is taken -- blank, unreferenced by the
four maps, not seen written at runtime, and not on a sheet row holding any
unreferenced artwork, which is where runtime word strips and their padding
live. All entries are scoped to `$14 = $00`, so the briefing keeps its tiles.

`text_tool.py reports --out DIR [--from ROM]` exports the four screens as
PNGs; `packets --reports-from DIR` imports painted ones and `packets
--reports` takes the donor's, colour attributes included.

Checked offline: US export and import changes nothing; the German import
matches the German screens on all 1024 cells of all four, pixels and colour
attributes, uses 60 of 296 free tiles, and leaves every runtime tile and all
264 unreferenced artwork tiles untouched; French uses 83. Not yet seen in
play.

### Correction: menu glyph rows 24-27 were not free

The save list (screen `$11`, through RESUME SAVED CITY) draws its digits from
the menu artwork as 8x8 sprites, tops `$190`..`$199` and bottoms `$1A0`..`$1A9`.
The sweep that called rows 20-31 free never visited that screen, and the ninth
sprite of GESPEICHERTE STADT spilled into the band at `$180`, whose lower half
is `$190`/`$191` -- reported from play as the "1" missing its top. The bands
at rows 24-27 are gone from the list, and the spare menu sprites now share one
blank pair so the saved-game line still fits. Checked against a capture of
the save list: no tile it draws is written any more.

## Map window titles

The map analysis window ("COMPREHENSIVE", "POWER GRID", fourteen maps) draws
its title as four 32x32 sprites over tiles `$100`..`$13F`, and the game copies
the strip for the current map into those tiles when the window opens. The
strips are pre-rendered in the window's graphics packet `$0A:D381` (4bpp,
1024 tiles): fourteen of them, 16 tiles wide and 2 rows tall, at packet tiles
512..959. Located by finding the title tiles of two live captures inside the
packet.

The German and French cartridges keep all fourteen at exactly the same
tiles, GESAMTUEBERBLICK where COMPREHENSIVE is, and every tile either donor
changes lies inside 512..959 (German 383 tiles, French 364). So a title is
translated tile for tile, with no copy table and no layout.

`text_tool.py maptitles --out PNG [--from ROM]` exports the sheet;
`packets --maptitles-from PNG` imports an edited one and `packets --maptitles`
takes the donor's, as a packet entry on `$0A:D381` scoped to `$14 = $00`.
Checked offline: the US export imports to no change, and both German routes
reproduce the German packet exactly.

A trap found on the way: `scan_packets` with a large minimum length does not
step over the shorter packets in front of the one wanted, so a false stream
decoded from inside them can swallow its start. At 32768 this packet was not
found at all; at 512 it is. Scans now use 512 and filter afterwards.

### Open: reported from play on the German build, not yet fixed

Reported after the report screens, notices and map titles went in. Listed
with the lead each one has, where there is one; none is investigated yet.

- **Map select and PLEASE WAIT are English.** Fixed, see "The map select screen" below.
- **Evaluation title misaligned.** Fixed. The year is four big digits drawn by
  `$02:B51F` at row 1, column 6; the German cartridge draws them at column 2,
  left of STATISTISCHE, and that operand is now taken. See "Report cells follow
  the donor" below.
- **Evaluation: Kategorie and Schwierigkeitsgrad come out misspelled.** The
  US code draws the category and level words ("Village", "Easy") at US
  cell positions, and the longer German labels run into those cells. Fixed
  with the word strips and the donor's cells, same section.
- **Evaluation: the runtime digit lands on the % of "% JA" / "% NEIN".** Fixed:
  the German number layout puts those digits one column left, and it is copied.
- **Evaluation: the "($)" after Wert der Stadt shows wrong tiles; overview:
  one wrong tile each in Feuerwehrstationen and Wasserflaechen; Kategorie and
  Schwierigkeitsgrad misspelled.** Fixed: the import had allocated new tiles in
  rows 46-48, and nine slots there (`$2EC`..`$2F1`, `$2F4`, `$304`, `$30B`) are
  where our translation runtime writes the accented briefing glyphs whenever
  this shared set unpacks. Those rows are now excluded.
- **Events: title correct, event lines English.** Fixed for German, see "Event lines and month names" below.
- **Main menu: WEITER over the end of GESPEICHERTE STADT.** Fixed: the NEXT
  button had been taken on every screen, and its tiles are the lower halves
  of the saved-game line's last sprites. See "Tile-for-tile sets" below.
- **Loan screen letter still English** (2026-09-16, savestate 2). Fixed, see
  "The loan letter" below.
- **Widescreen showing the new city before the fade** (2026-09-16, savestate
  3: loading the practice city from the in-city dialog). Fixed in the
  compositor, see `docs/WIDESCREEN_HOST_MAP.md`, "Loading a city".

## The map select screen

The new-city screens (unpacked on `$14 = $04`) are one device on two layers.

| layer | tile set | tilemap | pages |
|---|---|---|---|
| BG1 | `$08:DEA2`, 4bpp | `$0B:9BA4` | 3: the device, "Please wait..." |
| BG3 | `$08:C4DB`, 2bpp | `$0B:A10B` | 4: MAP SELECT; "Enter name of the city"; "Select game level", Easy/Medium/Hard and their funds; "Is this OK?  Yes  No" with the chosen level and funds |

The first version of this section looked only at page 1 of each tilemap and
said both were identical in the German cartridge, and that NEXT was the same
in both. Both were wrong, and the name entry, level select and confirmation
pages stayed English:

- BG1's three pages are identical; 21 tiles differ ("Bitte warten...").
- BG3's first page keeps its layout (LANDKARTEN, 22 tiles). Pages 2-4 are laid
  out anew: "Name der Stadt", "Waehle Schwierigkeitsgrad" with "1 Leicht
  2 Mittel 3 Schwer", "Ist das richtig?  Ja  Nein" -- other cells, other
  tiles, other palettes.
- NEXT is not on either layer. It is a sprite from the menu art `$09:A571`,
  tiles `$172`-`$174`, which German draws as WEITER; it is taken with the
  tile-for-tile sets below.

BG1's later pages also name tiles past the set's 256, from another part of
VRAM. Those cells are left out of the picture (orange) and of the import;
before that, a US picture redrew 195 tiles.

So BG1 imports tile for tile, and BG3 cell by cell like the selector: a
changed cell takes an identical tile if there is one, else a redrawn one --
a tile all of whose cells changed, or a blank tile no page uses -- and its
palette and priority come from the picture's colour ramp. `$08:C4DB` is also
the scenario selector's set, and the selector's entry is not scoped, so the
BG3 entries are scoped to `$04` and write every tile the new pages use, not
just the redrawn ones. The new tilemap goes in whole, also scoped.

The confirmation's first line is not all tilemap. Choosing a level runs mode
`$15`, which writes six BG3 words over the page before it shows:

```
03:d9eb  LDA $0B57 (level) / ASL / TAY
         LDA $DAAF,Y -> $7E41D4    four cells of the level's name
         LDA $DAB5,Y -> $7E41D6
         LDA $DABB,Y -> $7E41D8
         LDA $DAC1,Y -> $7E41DA
         LDA $DAC7,Y -> $7E41DE    the funds' first two digits
         LDA $DACD,Y -> $7E41E0
```

The code is the same in all three cartridges, shifted (German `03:DA0D`,
French `03:DA10`); the 36-byte table after it (German `$03:DAD1`, French
`$03:DAD4`) holds each donor's own tile numbers. With the pages imported and the US table left alone, Leicht came out
right by chance and Mittel and Schwer as fragments, and French showed the
wrong words. Some of the table's tiles are on no page (the 1 of 10, the 5, a
blank), so the import counts the eighteen entries as cells like the pages'
and writes the table back, as cart spans; the AOT code reads it at run time
(`cpu_read16`), so no `force_lle` is needed. The table is found through the
code in front of it.

`text_tool.py mapselect --out FILE` writes one 512x1024 picture: BG3's four
pages on the left in four colours (a cell in a colour ramp has a palette of
its own), BG1's three on the right in sixteen, and under BG1, from cell row 97,
the level table as three rows of six BG3 cells (Easy, Medium, Hard). A picture
from before the table, with those cells orange, keeps the US words and their
tiles. `packets --mapselect` takes the donor's, `--mapselect-from FILE` (or
the graphics folder) a painted one.

Checked offline: the US picture imports to nothing, with or without the table;
the German and French pictures import to exactly the donor routes, and with
the donor route applied the pages and all three table rows draw as the
cartridge does, pixels and attributes. Checked in play on the German- and
French-patched US builds, from a cold boot through name entry, level select
and the confirmation of each level.

## Event lines and month names

Decompiled:

```
02:b2ec  string N at the position of event type T:
         position = $02:BAB9[T], offset = $02:BAD9[N] & $0FFF
02:b328  bytes from $02:B8B4 + offset are TILES; $FE ends the line (the
         caller draws one more a row down), $FF the string; offsets under
         $5E get $100 added -- the large-font word strips, entries 0-12
02:b6d0  month names from $02:B708: 12 x (three tile words, $0FFF), 96 bytes
02:b66a  the events loop: position types 6-15 are the ten rows, string =
         event id + 16
```

Entries 13-15 are Easy/Medium/Hard (the evaluation's level field, 6 cells),
16-36 the events. The small font is A-Z `$00`-`$19`, a-z `$30`-`$49`, digits
`$20`-`$29`, space `$1F`, `, . '` at `$1C`-`$1E`, `$ ? ! " + -` at `$2A`-`$2F`,
`%` at `$4A`. Position types 0-5 are the evaluation's runtime fields (problems,
category, level).

The German cartridge runs a different drawer (`$02:B2F7`): its bytes are ASCII
and CP437 drawn from a second copy of the face at tile `$270` + code, and `$FD`
breaks a line nine cells further left. So its strings are decoded as text and
re-encoded for the US drawer, which starts every line at column 12: 18 cells,
to column 29, two lines. 22 of 24 German entries fit once re-wrapped; two are
shortened (`EVENT_SHORTER`): the "Bevoelkerung erreicht die ...-Marke" lines
become "Bevoelkerung / 30,000 erreicht". `template --from` a donor applies
the same re-wrapping and shortening to lines that do not fit as they stand,
so a template seeded from the German or French cartridge builds unedited
(before, `translate` stopped at German entry 17); lines that fit keep their
breaks, and a US template is unchanged.

Correction: the width was first taken as 17, which cost "Hohe
Luftverschmutzung!" its exclamation mark and French a short form. The US
itself writes "A deluge occurred!" in 18 cells, and column 29 is paper on the
US, German and French events screens alike (compared cell by cell against
column 28), so the lines now run to column 29.

The German list, word strips included, is 630 bytes against the US 523, so it
moves to the `$FF` filler at `$02:FCEC` and the base operand at `$02:B336` is
repointed (which only works on the interpreter -- see the correction below).
Accented letters get glyphs from the donor's `$270` face -- the US letter with
dots -- in free report tiles below `$100`, because a string byte can only name
a tile below `$100`. `packets --events` takes the donor's; `template` exports
`events` and `months` and `translate` imports them.

Checked offline: all 24 entries decode to the intended text, and the months
read JAN FEB MAER APR MAI JUN JUL AUG SEP OKT NOV DEZ. French fits too, with no
short form at 18 cells. Its level names fit as they are, one right-aligned
field of the donor's own width.

### Correction: the report import must never redraw the small font

The event lines, the month names and every number are drawn at runtime from
the small font `$00`-`$5F`. The report import redrew a tile in place whenever a
single map cell used it, and R (`$11`) is used once by a label -- so
"Reaktorunfall" and "APR" lost their R. `$00`-`$5F` are now protected; the
report screens still match the German cartridge on every cell.

### Correction: patched code bytes never reached play

The event import above repointed the list base, an operand inside `$02:B328`,
and was checked offline only. In play it could not have worked: the build
runs `$02:B328` as recompiled C, and the generator writes every operand into
that C as a constant, read from the ROM when `src/gen` was generated. A packet
patch lays its bytes over the cart image at startup, and only the interpreter
tier reads code from there. Data -- the offset table, the list, the month
names -- is read through memory at runtime and was never affected.

So a function whose code a translation changes has to run on the interpreter.
`recomp/bank02.cfg` declares `force_lle` for `$02:B328` (the word drawer: list
base and art threshold) and `$02:B51F` (the report title year). A forced
function needs its exit width declared too: the analyzer cannot see how an
interpreted callee returns, so every caller's continuation goes unproven and
the callers drop to the interpreter as well. Without `exit_mx_at` the forced
functions (with `$02:A66E`, tried and dropped) took some forty more variants
off AOT, as far up as `$00:961C`; with it, exactly the four variants of the
two functions leave, and nothing else in `src/gen` changes.

`src/gen` is rebuilt by `tools/regen.sh`. On Windows run it with
`PYTHON=python` set: the script prefers `python3`, which can be the Microsoft
Store stub, and the failure is easy to miss behind a pipe.

## Report cells follow the donor

What the game prints over the four report screens sits at cells fixed by the
US layout, and the donor places several of them differently to suit its own
labels. Three kinds, all taken by `packets --reports` / `--events`:

**Numbers** are placed by data. The number printer `$02:B266` reads a layout
from `$02:B77C` up to the word list; the German and French cartridges have the
same table, same length, at `$02:B787`. German differs in 18 words -- JA/NEIN
one column left (so the digit no longer covers the `%`), the problem
percentages one left, the overview's right column two right -- French in 9.
The words that differ are copied.

**The title year** is `$02:B51F`: four big-font digits at `LDX #$004C`, row 1
column 6 (`#$004E` when `$01FB = 2`). German draws at `#$0044`, French at
`#$0046`. The operand is found by the code around it in both cartridges and
copied; being code, it relies on the `force_lle` above.

**The loan term** is `$02:A66E`: the two digits of `$0B1D` (years left on the
loan, the "21" in "= 500 x 21" on the bank's page), `ADC #$3C50` into
`$7E2B2A`/`$7E2B6A` for the ones and `$7E2B28`/`$7E2B68` for the tens, column
21 and 20. German draws them at columns 23 and 22 (`2E`/`6E`/`2C`/`6C`), off
its "x" at column 20, which the US columns printed over; French keeps the US
cells. Copied the same way, and `recomp/bank02.cfg` keeps the function on the
interpreter with its two exits (`m1x1` when `$0B1D` is 0, `m0x0` otherwise)
declared.

**The word strips** are entries 0-12 of the word list: the evaluation's
problems (0-6) and city category (7-12); entries 13-15 are the level names.
The US draws the strips as artwork at report tiles `$181`..`$1DC` through the
`$100` rule. The German drawer has no such rule. Its problems are squeezed
lettering at its tiles `$320`..`$35F`, "Megametropole" is art at `$280`..`$28A`,
and "Dorf", "Stadt", "Hauptstadt" and "Metropole" are plain text right-aligned
in eleven cells ("Grossstadt" too, but its sharp s is byte `$9B`, not ASCII, so
it is copied as art). French draws all thirteen as art.

Art entries go first in the list, their glyphs copied into report tile rows
`$18`..`$1D` -- the US strips' own rows, which no map uses (German needs 72
tiles, French 73). Text entries follow in the small font. The threshold
operand at `$02:B32C` is set to where the art ends (`$58` German, `$65` French),
and the donor's first cell for each string type (`$02:BAB9`) comes along: the
German category starts at column 18, four left of the US 22, the problems one
left. Level names keep their spaces and may be as wide as the donor's widest.
The report import no longer lets a static label reuse an unreferenced tile on
those rows, since the strips may be redrawn; that changes nothing for German
today (469 tiles, all four screens still identical to the donor's).

Checked offline by running the US drawer's logic over the patched image: the
nine German art strips match the donor pixel for pixel; the four text strips
and the level names come out in the US small font, the same style as the
donor's face and a few pixels apart on some capitals and digits; the number
layout equals the donor's; the year operand reads `$44`.

Not taken: German also moves the budget's tax rate digits (`STA $7E2B28`.. in
`$02:A66E`) two columns right. Nothing collides there, and it would put one
more function on the interpreter.

### Save states taken on a report screen cannot check these screens

**Update (2026-09-17): the cause is fixed for new states.** The device
snapshot held the PPU's registers and memories but not its CPU-port latches
(the VRAM pointer, VMAIN's increment-on-high bit, the CGRAM/OAM write state),
so a state saved while the game was uploading tiles sent the uploads after
the load to the wrong place. States now carry those latches, the host's
master clock and its HDMA walker behind a versioned header (taken from the
adaptive-renderer PR, blackerking/UrbanRecomp#1). A save/restore at
frame 3300 of a San Francisco run matched the uninterrupted run for 300
frames -- WRAM hash, CPU registers, master clock -- and all 12 captured
pictures byte for byte. Old states still load, with a warning, and behave as
they did; save them again to get the fix. `SC_SAVE_AT`/`SC_SAVE_PATH` and
`SC_STATE_TRACE` under `--qualify` reproduce the check. The text below is
what was measured with the old format.

Loading a state saved while a report screen is open does not redraw it. Frame
0 is the saved picture; within 40 frames the tiles break up, and pad input at
60 and 150 frames changes nothing visible. Plain US, with no translation and
no packet patch, does exactly the same, and so does `SC_FIBER=0`. These fixes
are therefore checked by opening the screens fresh in play. Any test run from
a save state also needs `SC_REPLAY_MENU=0`, or the replay automation takes the
pad within a few seconds.

## In-city panels

The panels the icon bar opens -- GAME SPEED, OPTION, DISASTERS, INFORMATION,
LOAD SAVE -- draw their title strip and icon captions from one 4bpp sheet,
`$0A:A523` (384 tiles, unpacked to `$7E8000` on `$14 = $00`). The window around
them is the same in the German cartridge (its bank 01 row tables, such as the
ones `01:d94f` blits, differ only in pointers); what changes is which sheet
tile lands in which cell.

```
01:d729  REP #$30 ; LDA $01df ; ASL ; TAX
         LDA $03e5cf,X             ; list for page $01df (5-7 reuse page 0's)
         LDA #$03 ; PHA ; PLB      ; the list is read in bank 03
         (cell, sheet tile) word pairs until $FFFF, each
         MVN 32 bytes $7E8000 + tile*32 -> $7EC000 + cell*32
01:d77d  page 3 only: two 3x3 groups, cells from $01:d6f3, the nine sheet
         tiles $15A-$162 from $01:d717, a group copied when its $01e7 bit is clear
```

German keeps the code and the window, redraws 175 sheet tiles and uses longer
lists. Its titles fill all twelve cells of the strip (SPIELGESCHWINDIGKEIT,
AUTO-FUNKTIONEN, KATASTROPHEN, INFORMATIONEN, LADEN SPEICHERN) where the US
leaves the ends of a short title undrawn, and the captions change in their own
cells (KARTE, KURVEN, STEUERN, UMFRAG, GESAMT, MODELL, MUSIK, ZUM MENU, ENDE).
Its lists take 1426 bytes against the US 1362, and bank 03 has 190 free.

So an import writes the table and the lists to bank 0F, into the `$FF` run at
`$0F:9B97`-`$A80F` just before the message block at `$0F:A868`, and repoints the
reader's two operands: the long table address and the `LDA #$03` before `PLB`.
`recomp/bank01.cfg` keeps `$01:D729` on the interpreter for that, with
`exit_mx_at 01d729 0 0`; exactly its two variants leave AOT.

`text_tool.py panels --out PNG [--from ROM]` exports the five pages, 16x12
cells each and stacked, with page 3's nine extra tiles in a band below, in the
sixteen `LABEL_PAL` colours; a cell a page does not draw is solid orange.
`packets --panels-from PNG` (and `translate --panels-from`) imports a painted
sheet, `packets --panels` the donor's. The import rebuilds the lists and the
sheet from the pictures. `$01:D729` is the only code that copies out of the
unpacked sheet -- the `ASL x5 / ADC #$8000` idiom occurs twice in each
cartridge, both inside it -- so every tile but page 3's extras may be
redrawn (the 41 no US list reads are blank), and tiles already holding a
wanted glyph are kept.

Checked offline: the US export imports to no change; the German donor and the
German PNG produce identical patches (344 distinct tiles, 161 redrawn, lists
1442 bytes at `$0F:9C00`), and reading the patched image back through the
relocated reader reproduces every German page and extra exactly. French does
the same with 322 tiles, 123 redrawn, 1394 bytes.

## Graphics: one folder out, one folder in

Every translatable picture has an exporter and an importer, so a language
without a donor cartridge can be painted, and a donor's pictures can be
touched up before they go in:

```
python tools/text_tool.py graphics --out DIR [--from ROM]
python tools/text_tool.py packets   ... --graphics-from DIR
python tools/text_tool.py translate ... --graphics-from DIR
```

`graphics` writes each exporter's file into `DIR` with a `README.txt`; an
import takes whichever files are present. Each also works alone, as
`text_tool.py NAME --out ...` and `--NAME-from`:

| file | what | how it imports |
|---|---|---|
| `reports/` | budget, evaluation, overview, events, the loan letter | redraw, reuse or allocate per cell, with colour attributes |
| `maptitles.png` | map window titles | tile for tile |
| `labels.png` | toolbar building labels | tile for tile, slices fixed |
| `panels.png` | in-city panels, five pages + page 3's extras | lists and sheet rebuilt, lists in bank 0F |
| `mapselect.png` | the new-city display's four pages, Please wait... | BG3 per cell with colour attributes, BG1 tile for tile |
| `strips.png` | evaluation problems and categories | art in report rows `$18`-`$1D`, columns from the picture |
| `accents.png` | accented glyphs: message font, notices, report face, briefing | glyph source instead of a donor |
| `selector.png` | card names, disaster lines, Sylt's line | per cell, with colour attributes |
| `tilesets/` | city zone letters, bank window, graph title, gift signs, RCI meters, PUSH START, NEXT, police and fire stations, toolbar icons, in-city window words | tile for tile |
| `saveload/` | save/load dialog: sheet, prompt runs, month names | sheet up to 176 tiles, runs and months repointed |

Two conventions run through all of them. Colours are palette indices: the
sixteen `LABEL_PAL` colours for 4bpp sets, the first four for 2bpp, and the
grey `REPORT_RAMP` on the report screens; orange means a cell that is not
drawn. And in the tilemap pictures -- reports, selector and map select's
BG3 -- a cell whose
palette or priority differs from the US tilemap is shown in a ramp of its
own (one hue per palette, paler for priority), which the import reads back
as that cell's attribute -- plain pictures from before still import as they
did.

A donor flag (`--reports`, `--panels`, `--mapselect`, `--events`) still wins
over the folder for its own set. What a picture cannot carry stays with the
donor or the US: the report screens' number cells and title year column, and
the text of every string, which lives in the translate JSON.

Checked offline, per set and as a whole:

- A US folder imports to nothing: the packet it builds is byte-identical to
  the plain packet without it.
- German and French donor builds were byte-identical before and after the
  pictures went in, and the reports and selector pictures of both import to
  exactly what the donor routes produce, colour attributes included.
- A German folder reproduces the German cartridge: map select, accents and
  panels identically to the donor routes; the strips drawn through the US
  drawer match every German cell and both columns (as art in 79 tiles, where
  the donor route uses 72 plus text); the selector on all 2048 cells, pixels
  and attributes, with Sylt's line equal to the donor route's.

## Tile-for-tile sets, and what the packet inventory still shows

Every LZ5 packet of the US cartridge was paired with its German counterpart
(same decompressed length, best byte agreement) and the differing tiles
looked at. Six sets differ only in words drawn at the same tile numbers, and
each pairing is confirmed by the code that loads it (the `LDX #addr` / `LDA
#bank` before the unpacking `COP #$00`):

| set | US packet | German | loader US / German | tiles DE / FR |
|---|---|---|---|---|
| city map tiles: zone letters R, C -> W, G | `$07:E584` | `$07:E6E1` | `00:96C2` / `00:96B8` | 17 / 20 |
| report screens BG1: BANK, LOANS, Yes/No, Go With Figures | `$08:E422` | `$08:E630` | `02:A119` / `02:A116` | 68 / 38 |
| graph window title GRAPHS | `$0A:FCE1` | `$0B:8B11` | `02:98F9` / `02:98F9` | 16 / 24 |
| gift building signs | `$0A:C4CF` | `$0A:CB37` | `01:CD6C` / `01:CD6F` | 54 / 64 |
| RCI demand meter, city sprites | `$0A:81E9` | `$0A:8522` | `01:E45C` / `01:E45F` | 3 / 0 |
| RCI demand meter, menu sprites | `$0A:8F68` | `$0A:92A2` | `01:E477` / `01:E47A` | 4 / 0 |
| title: PUSH START, tiles `$118`-`$11F`, `$138`-`$139` | `$07:A680` | `$07:A680` | `05:90B5` / `05:90B5` | 10 / 10 |
| NEXT button, tiles `$172`-`$174`, on `$00` and `$04` | `$09:A571` | `$09:A65B` | `01:A10E` / `01:A149`, and three more | 3 / 3 |
| in-city BG3 set past its font, tiles `$100`-`$27F` (2bpp) | `$09:C0FB` | `$09:C223` | the notices' font | 30 / 15 |
| police and fire stations on the map, PD FD -> PH FH | raw `$05:C000`, four `$1400` frames | same address | -- | 16 / 32 |
| toolbar icons: R C PD FD -> W G PH FH; 10/120 Year | raw `$07:8000`-`$A680` | same address | -- | 21 / 24 |

`text_tool.py tilesets --out DIR [--from ROM]` writes each as a 16-tile-wide
sheet; `packets --tilesets` takes the donor's differing tiles and
`--tilesets-from DIR` (or the graphics folder) painted ones. All but NEXT
have no screen scope: the German art is what the German game shows wherever
these unpack.

The last two are taken only in part. The title set differs in 111 tiles
(French 87), but only "DRUECKE START" ("PRESSE START") is text; the rest is the
German cartridge's trademark sign and copyright lines, which are not ours to
change, and recoloured street lights and filler. The menu art differs in 97
tiles, which are the menu's own words (composed by the menu import, see "The
menu's artwork is shared" above) and the save list's glyphs `$19C` and
`$1AC`; only WEITER (SUITE) is taken. The button is sprite-text record `$2C`,
drawn on the new-city screens and by the in-city screen at `01:9FD1` that
shows the city's name, and it was first scoped to `$04`. Taking it wherever
the art unpacks was wrong: reported from play as WEITER over the end of
GESPEICHERTE STADT, because the menu composer's fourth band is `$160` and its
sprites' lower halves are `$170`-`$17F`, where the saved-game line's last two
sprites land. It is scoped to `$04` and `$00` (the city, where `01:9FD1`
runs) now -- confirmed from play on 2026-09-16, WEITER shows in the city --
and `write_packets` notes any bytes two entries of one packet write
differently on a screen they share. The German title set sits at the
US address, hidden from a packet scan by a false stream in front of it, so a
donor's copy is looked for there first.

The bank set comes with its map. German signs the bank's loan line
RUECKZAHLUNG, nine tiles on `$120`-`$128` and `$130`-`$138`, where the US signs
LOANS in five on `$030`-`$034` and `$040`-`$044`; the report screens' BG1 map
`$0B:B5F3` (German `$0B:C526`) differs in exactly those 18 cells, rows 75-76,
and the German set blanks the LOANS tiles. Taking the set without the map
left an empty box beside "= 500 x 21" (reported from play, 2026-09-22).
`tilesets` writes the map as `bank_map.txt`, one line of hex words per map
row, and the import takes the words that differ from the US; French draws
PRETS on the LOANS tiles and keeps the map. Checked offline: the US folder
imports to no change, the German folder to exactly the donor route.

The in-city set `$09:C0FB` is the notices' font below tile `$100` (German
reorders it, and the notices import writes its accents at `$F0`-`$FE`), and
window words above: TOP (French MAX), R and C on frames, R-1..., LOW MID
UPPER HIGH (ABW. MITTE AUFW. HOCH). Only `$100`-`$27F` is taken, exported as
four colours.

Checked offline: the US sheets import to nothing, the German sheets to exactly
the donor route.

Not taken, and why:

- `$0B:86F7`, the save/load dialog sheet: taken separately, see "The save/load
  dialog" below. (An earlier note here said German calls the sheet's loader
  from one more site, `01:E110`. It does not: that is the clear routine
  `00:CADD`, the German twin of `00:CA9D`, which the US calls at `01:E10D`.)
- `$0B:BCAD` (loaded by `02:B58C`): 94% of its bytes differ from German
  `$0B:CBE6`, and this note said that drawn with each cartridge's own tiles the
  two were the same picture. Wrong: it is the loan screen's letter, English in
  the US and German in the German cartridge, which writes it in ASCII at `$270`
  + code where the US has its own arrangement of the face. Reported from play
  (savestate 2) and taken now, as a fifth report screen -- see "The loan
  letter" below.
- `$0C:87CB` is a 2048-byte tilemap page of the briefing screen, which the
  translation runtime composes from strings; six differing words there are
  not tiles.
- The many small "differences" in banks 00-03 and 0F are false streams --
  code and message text that happen to decode as LZ5.

## The save/load dialog

The dialog's sheet `$0B:86F7` (4bpp, 162 tiles, unpacked to `$7E9000` by
`00:CAE0`) holds the city-category houses of the save list, the name
keyboard's letters, the CANCEL/YES/NO buttons and the prompts. Houses,
keyboard and buttons sit at the same tiles in the German and French sheets:
the houses are built by `01:CEBC` from 4x4 lists at `$01:CD8A` that are
identical in all three cartridges, and the copy routines and their slot
tables in bank 00 (`00:CB12` with `$00:CAFA`, `00:CB44` with `$00:CB32`) are
the same code shifted.

What differs are six operands and one table. Six sites in four dialog
functions copy a prompt as a run of sheet tiles:

```
LDA #$9000 + tile*32 ; LDX #count ; JSR $CB12      (at most 12 tiles)

site      US                          German                       French
00:c7e0   Which to load?   85 x11     Welches Spiel laden?  80 x12  85 x10
00:c8e0   Where to save?   72 x12     Welche Position?     163 x12  72 x9
00:c947   save?           155 x5      Speichern?            72 x8  155 x6
00:c987   Where to save?   72 x12     Welche Position?     163 x12  72 x9
00:c9f5   save?           155 x5      Speichern?            72 x8  155 x6
00:ca35   Where to save?   72 x12     Welche Position?     163 x12  72 x9
```

And the save list's month names are a 36-byte table after `00:cd36`, three
sheet codes a month (tile = code + 96: digits, then A-Z). German writes MAER,
MAI, OKT and DEZ, its A-umlaut being tile 162; French FEV, AVR, AOU, DEC.
Both donors' sheets are 176 tiles long, and German's last run is the
"Welche Position?" prompt and the umlaut. (German also inserts a character
remap into the name drawer, `CMP #$0028 / LDA #$0041` at `00:CDE1`; that is
new code, not taken.)

A packet patch may write past the US length -- the spans land in WRAM, and a
donor's own sheet reaches just as far -- so the import takes the sheet whole,
repoints the six (tile, count) operands and copies the month table.
`recomp/bank00.cfg` keeps the four dialog functions (`00:C7DA`, `00:C8DA`,
`00:C941`, `00:C9EF`) on the interpreter, with their exit widths: `00:c7da`
returns in m0x0 from `00:c82c` and in m0x1 from `00:c8cd`, the rest in m0x0.
Declared that way, the `$01DF` menu handlers that call them stay AOT. 34
variants leave AOT -- the four functions' eight, and the copy routines and
drawing helpers only these dialogs call -- and one other joins it.

`text_tool.py saveload --out DIR` writes `sheet.png` (16 tiles wide, 11 rows,
orange past the end) and `prompts.json` (the six runs and the twelve months as
tile numbers); `packets --saveload` takes the donor's, `--saveload-from DIR`
(or the graphics folder) painted ones. Checked offline: the US folder imports
to nothing; the German and French folders import to exactly the donor routes
(German: 83 tiles differ, six runs, months changed; French: 87), and the runs
read back from the patched image are the donor's.

## What the packet inventory could not see

The inventory above compares LZ5 packets, and play still showed English on
the confirmation page, over the scenario selector, on the toolbar and on the
police and fire stations. None of it is in a packet that differs. Two more
passes found them.

**Code banks, aligned.** Banks 00-03 and 05 of the US and German images were
aligned with a byte diff (difflib) and every non-trivial replacement looked
at; lone operands shifted by the local offset are relocations. Besides the
event strings and relocated tables already handled:

- `03:D9EB`'s level table, see "The map select screen".
- `$03:CF32`, the scenario cities' names, see below.
- `03:C500`, the scenario deadline. The in-city notices "5 years to complete
  scenario" ... "1 year" (notices `$14`-`$16`, `$18`, `$19`) are counted down
  by calendar year against the end years at `$03:C5B3`, and in the US the
  same routine judges the scenario when the end year arrives (`03:C548`,
  result in `$0D87`). German and French keep the countdown but judge in a
  routine of their own (`03:C571`, called from the frame loop at `03:80CA`)
  when `$0B51` -- apparently the time played, 48 to a year -- equals a value
  per scenario (`$03:C5F7`: 239, 479, 239, 481, 241, 479, 479, none). The US
  carries an unused table of 240, 192, ... 0 at `$03:C5CF`. Game logic,
  left as in the US by decision (`docs/REGIONS.md`).
- `$05:C000`-`$FFFF`, four frames of the map's animated tiles: PD and FD on
  the police and fire stations. At the same address in all three, raw.

**Banks at the same address.** Bank 04 is identical; bank 06's differences
are all in the HUD region `--hud` already copies; bank 07 is aligned up to
`$07:A7C0`.

**The toolbar, traced.** The icons stayed English with `--hud` applied. A
VRAM and OAM dump of a city (`SC_VRAM_DUMP`, `SC_OAM_DUMP`, the capture's
bytes being plain VRAM) showed the toolbar as 16x16 sprites on tiles
`$100`-`$12F`, and more than half of those tiles match not the HUD region but
a raw table at `$07:8000`: both states of every icon, copied to VRAM as an
icon is drawn. German changes 21 of its tiles (R C PD FD, and "10 Year" /
"120 Year" of the graph window), French 24. The same dump showed BG3 of the
city as `$09:C0FB`, whose window words differ too.

All three raw sets and the BG3 set are tile-for-tile sets now (the table
above). Checked offline: picture and donor routes agree for US, German and
French, and the patched frames and icon table equal the donor's. Checked in
play (German, San Francisco): toolbar W G PH FH, the police station PH; and
reported from play on 2026-09-16, the graph window's "10 Jahr" / "120 Jahr".

## Scenario city names

Starting a scenario, or the practice map, names the city:

```
03:cf19  LDA $CF32,Y / STA $79      Y = scenario * 2, nine pointers
         copy length + 1 bytes from ($79) to $0B5B
```

The list is CISCO, BERN, TOKYO, DETROIT, BOSTON, RIO, LASVEGAS, FREEDOM and
PRACTICE, the practice map being entry 8, in the city-name codes: 0-9, A-Z
(`$0A`-`$23`), then `,` `.` `-` and space (`$24`-`$27`). The name is drawn as
sprites by `01:A312` (halves from `$03:E57F`/`$03:E5A7`), in the save list by
`00:CD98`, and on the name entry page.

French renames BERN and PRACTICE (BERNE, ENTRAIN). German renames PRACTICE
UBUNG with an U-umlaut, code `$28`, which only new German code can draw: the
save list's remap `CMP #$0028 / LDA #$0041` at `00:CDE1`, and sprite tables
one entry longer. Not taken, so a donor's `$28` comes over as UE: UEBUNG.

The list and the pointers are read at run time (`cpu_read16` in the AOT
code), so a translation is a cart span packing the nine names into the 62
bytes the US names take. `packets --cities` takes the donor's; the translate
JSON has them as `cities`, ids 0-8, 1-8 characters each.

## The scenario selector's title

"-SELECT SCENARIO-" is sprite-text record `$15`, drawn by `03:DD6D` from base
(96, 96): nine 16x16 sprites at y = -84 in the menu's own face, a dash (tile
`$0C0`) at each end. The record table and the code are the same in all
three cartridges; German draws SCHAUPLAETZE on `$0C4`-`$0CE` with an 8x8
sprite for the dots, French CHOISIS SCENARIO on `$0E0`-`$0EE` without the
dashes. Our build showed the US title, because the menu import composes its
glyphs only on `$02` and `$12`.

It is composed like the option lines now: glyphs from the alphabet into
`$0E0`-`$0EE` and `$0C4`-`$0CE` -- the tiles the German and French titles
prove unused elsewhere on the selector -- in an artwork entry scoped to
`$0A`, with record `$15` rebuilt at the end of the bank-0 filler and
repointed, centred where the US title is. An odd last word leaves a blank
half, which the right dash closes over, as German's does. The text is the
third menu line unless `--selector-title` (JSON `selector_title`) says
otherwise; in all three cartridges the two read the same. The US wording
composes nothing. Checked in play: the German and French builds show
SCHAUPLAETZE (the dots squashed into the A, as in the menu) and CHOISIS
SCENARIO over the selector.

## The loan letter

The loan screen (savestate 2) is BG1 art -- the bank front, BANK, LOANS, Ja /
Nein, from `$08:E422` -- with the letter on BG3: the report screens' 2bpp set
`$09:875C` through a three-page tilemap `$0B:BCAD`. Page 0 is the offer, pages
1 and 2 the refusal when the city owes too much, with different buttons. The
US writes the letter in its own arrangement of the small face (A-Z from
`$290`, a-z from `$2C0`); German (`$0B:CBE6`) and French write ASCII at `$270`
+ code in theirs, accented letters above `$2F0`.

It is imported as a fifth report screen, `reports/bank.png` (256x768, the
three pages under one another): the same per-cell redraw, reuse or allocate
into the same set, scoped to `$00` like the others, and the tilemap replaced
whole. Its tiles now count wherever the four screens' do -- the references a
cell may redraw in place, the free tiles the reports and event lines take,
the strip rows -- so no other import can take a letter glyph. German needs 8
new tiles and redraws 8 in place; the rest are glyphs the US set already has.

Checked offline: the US picture imports to nothing, and the German and French
pictures import to exactly the donor routes, with all five screens drawing
every cell as the cartridge does, pixels and attributes. In play the letter
could not be reopened cleanly from savestate 2: re-entering the bank from
there garbles the screen, palette included, in the unpatched US game just the
same (the known trouble with states on report screens). Confirmed from play
on 2026-09-16 with the bank opened normally: the letter reads German.
