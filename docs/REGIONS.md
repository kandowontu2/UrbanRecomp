# Regional ROMs: what is shared, what is not

Five 512KB images: U, E, F, G, J. The host and tools find each image by its contents (`tools/find_rom.py`), whatever the file is called.

| region | header byte | FNV-1a32 |
|---|---|---|
| Japan | `00` | `ccb8c347` |
| USA | `01` | `ec01686a` |
| Europe | `02` | `b76b1a0d` |
| France | `06` | `e1f99069` |
| Germany | `09` | `aeca7623` |

## They are two code bases, not one with swapped text

```
          COP     NMI
U, J    $8211   $80b2      identical bytes at 00:8211, 00:80b2, 03:b8ae
E, F, G $820d   $80ae      consistently 4 bytes earlier
```

Only 26% of 1KB chunks are byte-identical to U, for both J and E, and the best
whole-ROM shift against U is **+0** -- so E/F/G are a different build rather than
a relocation of the US one.

What *is* shared is data: banks `04`-`08` are largely identical across regions
(36KB runs in `04:8000`-`05:8fff`, and `06:fc00`-`08:b3ff` for J). Banks `00`-`03`
are the code and they genuinely differ. J is closest to U and is the sensible
first target for any per-region recompilation.

## Native execution preserves all five verified regions

Enhanced Beta 19 includes complete compiled per-address profiles for all
five fingerprints above. Each profile covers all 524,288 ROM bytes and
retains ROM aliases, live operands, flags, cycles and memory callback order.
The enhanced gameplay hooks remain US-only. Generate optional regional
profiles with `tools/compile_native_program.py --regional-rom REGION ROM`;
see [native execution](NATIVE_EXECUTION.md) for build and validation details.

## Historical interpreter qualification

All five pass `--qualify 600` with essentially identical counters (592 logic
changes, 174 video changes; G differs by one frame of audio). The interpreter
tier is ROM-agnostic, so `SC_LANG=U|E|F|G|J` is all a language selector needs.

## The AOT tier is US-only, and is now guarded

Generated code carries US addresses and a US dispatch table. `SC_FIBER` on any
other image is refused outright.

> The first version of that guard sat where `SC_FIBER` is parsed and did nothing
> at all, because env parsing runs **before** the ROM is read -- a German ROM ran
> 60 compiled bounces straight past it. It now checks where the fingerprint is
> actually known.

The default AOT path is safe regardless: it reports `bounces=0`, i.e. it is pure
interpreter unless `SC_FIBER` is set.

## US byte-patches must be fingerprint-gated

The cursor-cadence patch tests a **single byte** (`== 0x03`), which cannot
identify a site in a different build -- measured, it was patching **2/2 sites on
every one of E/F/G/J**. Now gated on the US fingerprint, so it reports `0/2`
there.

The view fix tests four bytes (`8f b5 21 7e`, a `STA $7e21b5` long) and already
rejected foreign ROMs on its own ("byte mismatch"). Gated too, for consistency.

The lesson generalises: a byte-signature patch is only as specific as its
signature, and one byte is not a signature.

## Recompiling a non-US ROM: the pipeline works, the cfg is the work

`v2_regen.py` takes `--rom`, so it points at any image. Run against Japan with a
minimal seed cfg (`recomp-j/`, bank declarations plus `auto_vectors`):

```
v2_regen: 16/16 banks emitted        wall-clock 218.5s
=== STUB LINT - 85 stub(s) ===       [BRK: software interrupt] x85
```

Stubs are a hard build error, so nothing is written. The obvious reading is that
Japan is harder to recompile. **It is not** -- the control settles it. The same
seed cfg against the **US** ROM:

```
v2_regen: 16/16 banks emitted        wall-clock 217.9s
=== STUB LINT - 90 stub(s) ===       [BRK: software interrupt] x90
```

The US image produces *more* stubs from the same seed. The stubs measure cfg
completeness, not the ROM. `src/gen` is stub-free only because `recomp/` carries
~200 hand-declared `func` entries plus the exit-M/X directives -- the accumulated
analysis -- which keep the decoder on real code instead of following data.

So per-region recompilation needs per-region analysis, and Japan starts from a
marginally *better* position than the US ROM did. The seed cfgs are kept in
`recomp-j/` as that starting point.

For reference, current US coverage with the full cfg: **1544/1628 variants**
aot_eligible (94.8%), **73364/75333 instructions** (97.4%).

### What a per-region port would need

1. Executed-PC and M/X bitmaps from real play on that image (`SC_MX_BITMAP`) --
   the host already records these for any ROM, since the interpreter is
   region-agnostic.
2. Call-site discovery to seed `func` declarations, the same method
   `recomp/bank00.cfg` documents for the US image.
3. The exit-M/X fixpoint via `tools/mx_exit_*.py`, all of which take the ROM
   path as a constant that would need parameterising (`ROM = 'us.sfc'`).
4. A separate `src/gen-<region>` tree and a build target that links it, plus
   widening the fingerprint guard from one US constant to a per-tree identity.

## Play-tested: Germany

Reported from play on the German image:

- **The F10 MELTDOWN trigger works as intended.** That is the interesting one:
  the trigger pokes `$003e`, `$0040` and `$0c0d`, which are **WRAM**, not ROM.
  It working implies the WRAM layout is shared across regions even though the
  code is not -- which would make much of `docs/ROM_MAP.md` portable. Treat this
  as a strong hint from play rather than a measured fact: an attempt to confirm
  it by dumping those fields at frame 400 was uninformative, because the game is
  still on the attract screen and every field reads 0 in all regions.
- **The View screen works**, without the US `00:c0fb` NOP patch that the US
  build needs. So that bug may be US-specific.
- **Controls are not sluggish**, without the US cursor-cadence patch. So that
  patch is not needed here either.
- **The UFO fires only at higher population**, i.e. the `03:b9b3` gate is
  active -- expected, since the gate-lift is a US ROM byte-patch and is
  correctly declined on this image.

Both US byte-patches decline cleanly (`0/2` and "byte mismatch"), and neither
omission caused a problem.

## Game logic the European build changes, left as in the US

The translation takes text and pictures from the German and French
cartridges, not their code. Where the European build behaves differently,
the US behaviour stays; decided from play, 2026-09-16.

- **When a scenario is judged.** Both builds show the notices "5 years to
  complete scenario" down to "1 year", counted by calendar year against the
  end years at `$03:C5B3`. The US routine `03:C500` then judges the scenario
  itself when the end year arrives (`03:C548`, result in `$0D87`). German and
  French judge in a routine of their own, `03:C571`, called every frame from
  `03:80CA`, when `$0B51` -- apparently the time played, 48 to a year --
  reaches a value per scenario (`$03:C5F7`: 239, 479, 239, 481, 241, 479,
  479, none). So a European scenario can end a week or so earlier or later
  than the US one. Porting it would be new code; not done.
- **The umlaut in a city name.** German names the practice city with an
  U-umlaut, city-name code `$28`, and adds code to draw it (the save list's
  remap at `00:CDE1`, longer sprite tables for `01:A312`). The translation
  writes UEBUNG instead. See "Scenario city names" in `ROM_MAP.md`.
