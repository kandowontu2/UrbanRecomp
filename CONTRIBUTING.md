# Contributing

Thank you for helping improve UrbanRecomp. The project keeps ROM data
and generated game code out of Git, so a working checkout has two explicit
inputs: the pinned `snesrecomp` submodule and your own legally obtained ROM.

## Set up a checkout

```bash
git clone --recurse-submodules <this repo's URL>
cd UrbanRecomp
bash tools/bootstrap.sh
```

`tools/bootstrap.sh` is safe to rerun. It synchronizes submodule URLs,
initializes nested submodules, and verifies that `snesrecomp` matches the
gitlink committed by this repository.

Put your own verified US ROM (any file name) in the repository root and generate
the private C sources:

```bash
bash tools/regen.sh --no-tests
```

The ROM and `src/gen/` are ignored and must never be committed.

The enhanced host also has a compatible per-address native C tier. Generate
it directly from the clean US ROM, without changing the framework checkout:

```bash
python tools/compile_native_program.py --rom /path/to/your/us.sfc
```

Its generated `src/program_gen/` files stay local too. When present, CMake's
`SC_PROGRAM=ON` links them through `ScProgramRuntime`. Keep `SC_AOT=OFF` for
the enhanced host: the older AOT/fiber execution path uses a different ABI
and skips required game hooks. This native tier preserves live bus operands,
register widths and original instruction boundaries. Default builds also use
`SC_INTERPRETER_REFERENCE=OFF`: they link the register/save ABI without the
65816 decoder and require the complete generated ROM tier. To build the
independent oracle explicitly, use `SC_INTERPRETER_REFERENCE=ON`.
In that reference build, `SC_PROGRAM_REFERENCE=1`
runs the preceding instruction execution path for matched checks;
`SC_PROGRAM_CONTROL_REFERENCE=1` also restores the original IRQ/NMI/WAI/STP
control path. The generator includes the verified 56-entry city-tool dispatch
table rather than depending on which indirect targets a profiling run visits.
Compile and run
`UrbanRecompProgramTest` with the same ROM to compare every generated site
against the original CPU. Details and limits are in
[GPU_PERFORMANCE.md](docs/GPU_PERFORMANCE.md).

Every ROM byte has a compiled per-address action, covering indirect targets
outside the compact hot-path graph. Optional verified regional profiles use
the same native path; repeat `--regional-rom REGION /path/to/cartridge.sfc`
with `eu`, `fr`, `de` or `jp` when generating. US enhancements retain their
separate fingerprint guards. Unsupported code modifications are diagnosed;
native builds never interpret an uncovered address. See
[native execution and verification](docs/NATIVE_EXECUTION.md).

World-hook ownership is generated from host source by
`tools/compile_world_layout.py`; CMake refreshes it when its source changes.
Run the script with `--check` to verify the checked-in header. Build and run
`UrbanRecompWorldPreparationTest /path/to/your/us.sfc` for the independent
preparation oracle; it does not use generated game instructions. Keep live ROM
operand guards and world/register-dependent binding outside the cache.
`SC_WORLD_PREPARATION_REFERENCE=1` retains original hooks for matched replays
and timing controls.

The connected UI/driver lane in `src/main.c` admits only banks 00/01 and
yields at `sc_program_host_boundary`. When adding a hook in either bank,
add its PC to that boundary function. Keep the per-edge world-coordinate,
operand mapping, coverage, beam and APU work inside the lane. Trace and
PC-triggered capture modes retain the complete dispatcher. Use
`SC_PROGRAM_LANE_REFERENCE=1` to compare against the preceding C tier
without disabling the compiled program itself. Membership generation does
not replace the live opcode validation performed by each C action.

The generator also emits direct C blocks for the driver/UI banks. Their
before/after callbacks retain world preparation and real event retirement;
unexpected live targets yield to page dispatch and patched opcodes retain
the prepared fallback. `SC_PROGRAM_BLOCKS_REFERENCE=1` keeps the preceding
compact C scheduler for same-binary controls. The program test additionally
compares connected spans against the original CPU, including persistent RAM
writes, ordered bus accesses, interrupt/control yields and preparation
redirects. Keep generated block sources private alongside the other ROM
generation outputs. Correctness results do not establish a speed improvement.

## Build and run

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DCMAKE_TOOLCHAIN_FILE=<path-to-vcpkg>/scripts/buildsystems/vcpkg.cmake
cmake --build build --target UrbanRecomp
build/UrbanRecomp.exe                 # windowed
build/UrbanRecomp.exe --qualify 3600  # headless activity qualification
```

`--qualify N` runs N frames with no window and asserts the same generic bar
snesrecomp's own per-game status table uses for a new bring-up: logic state
must keep changing, audio must stay actively producing samples, and rendered
video must not freeze -- see `src/main.c`'s `run_qualification` and
`snesrecomp/cosim/ref_driver.c` (the framework's own game-neutral reference
driver, which this project's qualification check mirrors).

## Change the framework dependency

The `snesrecomp` gitlink is the single source of truth for the framework
revision. Normal game-only changes should leave it untouched. If you find a
genuine framework bug (not specific to this game), fix it upstream in
`snesrecomp` on its own branch, coordinate that separately, and only then
bump this repo's gitlink -- do not carry local patches to the submodule.

## Before opening a pull request

- Rerun `bash tools/bootstrap.sh` and confirm `git submodule status --recursive`
  has no `-`, `+`, or `U` prefix.
- Build the target and run `build/UrbanRecomp.exe --qualify 3600`.
- Run `git status --short` and check for ROMs, generated sources, build
  trees, or unrelated files before staging.
- Keep game-specific addresses and behavior (bank cfgs, `src/variables.h`,
  any future HLE overlays) in this repository. Reusable CPU, mapper,
  coprocessor, diagnostics, and presentation mechanisms belong in
  `snesrecomp` -- see its README's Contributing section.
