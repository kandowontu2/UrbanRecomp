# Performance and GPU investigation

Measurements on 2026-10-02 used the Windows Release interpreter build, the
verified US ROM, and a private saved city at X50. No ROM or save data is
included here. All changes and builds from this investigation are local;
publishing requires the owner's approval.

## What currently runs where

The actual SDL window selected the `direct3d11` renderer on this machine.
Texture presentation, scaling, and the host's drawn construction previews
already use that backend. The original PPU raster, adaptive tile decoding,
colour composition, and native 65816 simulation normally execute on the CPU.
The optional GPU terrain path below moves extended terrain decoding and colour
composition onto that same Direct3D device. Native PPU and simulation work
remain on the CPU.

`SC_PERF=1` reports input, guest execution, audio, drawing, sleeping and
presentation costs. `raster` and `power` are subsets of guest execution, so
do not add them to `emu` again. At X50, the profiled saved city spent about
10–21 ms per displayed frame in guest execution during its active phases,
including about 7 ms of raster work at a 448x224 canvas. Cached power work
was about 0.36–0.61 ms. Some phases exceeded the 16.64 ms display budget.
The quieter adviser/menu phases reached about 60.1 FPS. These are workload
measurements, not a guarantee for a populated Huge city or another computer.

## Retained optimizations

- Decode an unchanged four-plane VRAM row once. Compare both live words on
  each access; DMA, animation, palette changes, and flips remain immediate.
- Avoid colour-window evaluation and channel math when neither is active.
- Cull sprites by rectangle before sampling the two eight-pixel city edges.
- Compare power-network tile IDs exactly using an independent reduction,
  replacing the old serial hash. Ignore metadata and power bits as before;
  use the original solver when the topology changes at its scheduled cadence.

Six alternating before/after pairs replayed 240 frames without display pacing.
Median elapsed time fell from 3.267s to 3.077s, about 5.8%. The complete saved
state and rendered frame matched byte for byte, including the accelerated
attempt counts. Renderer regressions also cover live VRAM changes, flips,
colour math, relocated HUD, cursor repair, and tall/wide canvases. Power tests
cover every map size and development multiplier, nuclear-first connections,
and bitmap ownership during a native scan.

A tighter development dispatcher and whole-program link optimization were
tried and discarded because neither showed a repeatable improvement.

## GPU offloading candidates

The strongest first candidate is adaptive terrain and colour composition.
These produce an image consumed by the GPU and do not require simulation
readback. The recommended prototype would:

1. Keep the original native PPU raster as the reference and fallback.
2. Capture the VRAM, palette, scroll, window, brightness, and sprite state
   actually used by each scanline. End-of-frame state alone is insufficient:
   the game changes registers and data during a frame.
3. Upload changed tile/world data and scanline records through reusable buffers.
   Decode tiles and compose extended terrain in a shader, preserving flips,
   colour math, priorities, power warnings, and HUD/cursor ownership.
4. Present the resulting GPU texture directly. Read pixels back only for
   validation or requested screenshots, not every displayed frame.
5. Compare with CPU output over city edges, Huge coordinates, animated tiles,
   menus, adviser fades, cursor gestures, and portrait/ultrawide canvases.
   Measure total frame latency, upload cost, and GPU timing before enabling it.

[SDL3's GPU API](https://wiki.libsdl.org/SDL3/CategoryGPU) supports graphics and
compute with Vulkan, Metal, and Direct3D 12, separate from the existing Render
API. It requires backend-compatible shaders; its transfer buffers and fences
support asynchronous uploads/readback. A GPU compositor would therefore need
a new backend and shader build path, rather than a switch on `SDL_CreateRenderer`.
For the Windows prototype, SDL also documents access to its
[Direct3D device](https://wiki.libsdl.org/SDL3/SDL_GetRendererProperties) and
[wrapping a native texture](https://wiki.libsdl.org/SDL3/SDL_CreateTextureWithProperties).
These let the implementation retain the existing window/presentation backend.

The spatial smoothing fields are a possible later compute target. Their output
is immediately read by the CPU simulation, so transfers and synchronization
must be measured against the existing equivalent C kernels. Power flood fill,
zone decisions, PRNG, calendar, budgets, and the serial guest CPU are poorer
first targets: preserving their ordering and exact state is essential, and
small GPU jobs with immediate readback may cost more than they save.

## Implemented Windows GPU terrain prototype

**F12 → GPU TERRAIN** enables the optional path. It starts off, and is
session-only. `SC_GPU_TERRAIN=1` enables it for testing; `0` keeps CPU rendering.
It requires SDL3's Direct3D 11 renderer and feature level 11. Unsupported
backends, shader/resource failures or device loss retain/revert to CPU output.
The first activation compiles an embedded original HLSL shader using Windows'
system `d3dcompiler_47.dll`; no downloaded compiler or external shader is needed.

The compositor captures live base/roof plane pairs for each visible eight-pixel
span, together with scanline palette, brightness, windows, fixed colour and
subscreen data. A compute shader decodes the tiles and applies integer SNES
colour arithmetic. Native core pixels, fresh native tile repairs, objects,
power warnings, HUD, adviser pages and pointer repair retain their CPU output.
Those pixels override deferred terrain. Menus and unsupported effects continue
through the existing renderer. Later HUD/cursor writes also override earlier
terrain records, preserving their normal ordering.

Reusable dynamic buffers upload only the visible frame's data. The GPU result
is wrapped in an SDL texture and presented directly; ordinary frames do not
read it back. Compute temporarily unbinds and then restores SDL's graphics
resource bindings, including across resize. CPU decoding of captured records
remains available for fallback and framebuffer screenshots.

Three alternating pairs replayed the same 240 guest/display frames at X50.
Whole-run `SC_PERF` averages for emulation + draw + presentation were:

| Canvas | CPU median | GPU median | Less frame work |
|---|---:|---:|---:|
| 21:9, 448x224 | 10.173 ms | 9.786 ms | 3.8% |
| 32:9, 684x224 | 11.655 ms | 8.894 ms | 23.7% |

These are CPU wall timings around frame stages, not GPU timestamp-query results.
They include uploads and presentation but exclude pacing sleep and the one-time
shader creation in input/setup. Host load affects measurements, and gains depend
on how much extended terrain is visible. Native simulation spikes at X50 can
still miss 60 FPS. Tab continues to use its adaptive spare-time budget.

Validation compares actual GPU readback against captured CPU pixel resolution
when `SC_GPU_VALIDATE=1`. This deliberately waits for the GPU and is disabled
for normal play/benchmarks. The normal-city and far-Huge replays, including a
mouse construction drag outside the window, matched the previous CPU build's
complete saved state and actual SDL-rendered screenshot byte for byte.
The F12 switch and six real window resizes also passed.

`sc_terrain_test` compares 24 complete deferred/CPU frames over Huge coordinates,
wide/tall views, live plane/palette changes, horizontal/vertical flips, window
logic, colour add/subtract/half/clamping, fades and switching back to CPU.
`UrbanRecompGpuTest` sends the same cases through the actual compute shader and
validates every output pixel, then compares actual SDL presentation with both
nearest and linear filtering. The GPU path preserves the launcher's filter
preference. It is an optional desktop target, returning 77
when the backend is unsupported:

```
cmake --build <build-directory> --target UrbanRecompGpuTest
<build-directory>/UrbanRecompGpuTest.exe
```

All prototype builds remain local. No release or repository push is authorized
by this performance work.

## Subsequent Tab rendering optimization

Intermediate Tab frames now omit native background/pixel composition as well
as expanded host composition. They retain the guest beam, IRQ/NMI, APU, full
sprite/sliver evaluation, overflow flags, OAM history and brightness caching.
The final frame always draws completely. Normal play uses the pinned runner's
original scanline renderer. `src/sc_ppu.c` includes that translation unit and
reuses its private sprite evaluator, without modifying the submodule.

Tab's budget now measures only the native image work it can omit, in addition
to host image work. The previous estimate kept charging native raster work to
extra frames, hiding much of the improvement. Actual skipped guest frames still
update the estimate immediately when a simulation phase becomes more expensive.

Four alternating-order pairs, with fixed three-frame batches at X50/21:9,
replayed exactly 240 guest frames and 80 displayed frames. Median emulation +
draw + presentation work fell from **28.189 to 24.303 ms per display batch**,
about **13.8% less**. A far-Huge GPU replay with an off-window construction drag
fell from 33.869 to 25.657 ms (24.2%, one pair). Host load produced considerable
timing variation; these are CPU wall measurements, excluding pacing sleep,
and are not a steady-FPS guarantee.

The separate adaptive held-Tab city replay needed 342 displayed frames for
602 guest frames before this change and 210 for 603 afterwards (the stop
condition permits a final batch to cross the target). The measured average
boost was approximately **1.76x → 2.87x**. The later run's one-second display
windows were 58.9, 59.1 and 59.6 FPS. This is one workload on this machine;
heavy X50 simulation remains CPU work and can exceed the display budget.

All fixed-batch runs matched complete saved states and actual SDL screenshots
byte for byte. The PPU regression compares the pinned full renderer against
skipping over 32 cases: both native renderers, sprite size/priority/interlace,
overflow and unlimited sprites, widened OAM, live VRAM, mid-frame brightness
and forced blank. It verifies an untouched skipped framebuffer and an identical
next full frame. The four CTest tests and GPU differential tests passed.

`tools/test_tab_rendering.py` accepts private `--exe`, `--rom`, `--state`,
`--end-frame` inputs, with optional `--gpu` and `--mouse-drag`. Testing-only
`SC_TAB_TEST_BATCH=1..6` fixes the batch count. `SC_TAB_SKIP_PIXELS=0` restores
the full-raster reference and its budget estimate; neither override is used
in ordinary play. No publishing is part of this local optimization.
