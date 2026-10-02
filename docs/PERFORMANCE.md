# Performance on Windows

## The finding (2026-09-17)

The game ran below full speed on a Hyper-V VM with an i7-14650HX. The cause
was not the VM or the renderer. It was `getenv()` in the two hottest loops of
the host:

| where | calls per frame | since |
|---|---|---|
| `run_one_frame()`, once per guest opcode: `SC_SCEN_DIAG` (and `SC_BRIEF_DIAG` when no translation is loaded) | tens of thousands | f3bb7e4, 2026-09-06 |
| `handle_pos_stuff()`, once per scanline: `SC_WS_DIAG`, `SC_PASS_DIAG` | about 450 | fcb0e54 / e5de60d, late August |

On Windows, `getenv()` takes a lock and scans the whole environment. With
this machine's environment that costs about 1.4 µs a call. Both loops now
read their flags once into statics.

Measured with `--qualify` and German, widescreen and Sylt on:

| | 1800 frames | per frame |
|---|---|---|
| before | 31.7 s | 17.6 ms, below real time |
| after the opcode fix | 5.4 s | 3.0 ms |
| after the scanline fix | about 4.3 s | about 2.4 ms |

The headless figures include `run_qualification()`'s own WRAM and video
hashing, which is about a fifth of the remaining time and does not exist in
play. In a window (`SC_PERF=1`), a frame is now about 2 ms of emulation,
0.02 ms of texture upload and 0.2 ms of present (SDL2 `direct3d`); the rest is
the pacing sleep. The frames are byte-identical to before.

**Rule:** no `getenv()` in anything that runs per opcode, per master cycle or
per scanline. Read the variable once into a static.

## Tools

- `SC_PERF=1` (windowed run): once a second, the frame rate and the average
  and worst milliseconds of each part of a host frame -- input/events,
  emulation, audio, texture upload and draw, pacing sleep, present. It also
  names any `SDL_PollEvent` call that blocks for more than 20 ms.
  During the measurement one second showed a single 535 ms stall in the
  event/input part; three later 20-second runs had none.
- `tools/sample_profile.py`: a sampling profiler that needs no Visual Studio.
  It samples the main thread's instruction pointer about 1000 times a second
  and resolves it through the PDB with DbgHelp. It needs a build with debug
  information:

  ```bash
  MSYS_NO_PATHCONV=1 cmake -S . -B build-prof -DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake \
      "-DCMAKE_C_FLAGS_RELEASE=/O2 /Ob2 /DNDEBUG /Zi" \
      "-DCMAKE_EXE_LINKER_FLAGS_RELEASE=/DEBUG /OPT:REF /OPT:ICF"
  cmake --build build-prof --config Release --target UrbanRecomp
  python tools/sample_profile.py build-prof/Release/UrbanRecomp.exe --qualify 1800
  ```

  `MSYS_NO_PATHCONV=1` matters in Git Bash, which otherwise turns `/O2` into a
  path.

## What is left (profile after the fix, headless)

`run_qualification` hashing (headless only) about 18 %, `handle_pos_stuff`
12 %, `PpuDrawWholeLine` 9 %, the per-opcode hooks in `run_one_frame` 6 %,
BG and sprite drawing about 12 %, `cart_getRomPtr` 4 %, the APU about 8 %.
None of it is needed for 60 fps: a frame uses about an eighth of its budget.

## ar-recomp's recent Windows work, for comparison

ar-recomp (DerrickGold/ar-recomp, up to 2026-09-16) added Graphics API
selection for its SDL3 GPU renderer, coalesced dirty-rect texture uploads for
its 3D towns, and a guard that falls back to software pacing when the
renderer's VSync does not actually pace presents. None of these touch what
was slow here: this host's upload and present together take about 0.2 ms, and
its pacing is a manual deadline (`SDL_Delay` plus a sub-millisecond spin),
which held 60.1 fps in every measured second.

## Enhanced fork at X50 (2026-10-02)

See [new profiling results, CPU optimizations and GPU investigation](GPU_PERFORMANCE.md).
The earlier figures above describe the upstream workload, not accelerated large cities.
