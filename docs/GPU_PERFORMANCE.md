# Performance and GPU investigation

Measurements on 2026-10-02 used the Windows Release interpreter build, the
verified US ROM, and a private saved city at X50. No ROM or save data is
included here. All changes and builds from this investigation are local;
publishing requires the owner's approval.

## What currently runs where

The actual SDL window selected the `direct3d11` renderer on this machine.
Texture presentation, scaling, and the host's drawn construction previews
already use that backend. The original PPU raster, adaptive tile decoding,
colour composition, and native 65816 simulation execute on the CPU.
Changing the presentation backend alone cannot move those CPU routines.

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
Batch work and retain resources across frames to avoid driver overhead.

The spatial smoothing fields are a possible later compute target. Their output
is immediately read by the CPU simulation, so transfers and synchronization
must be measured against the existing equivalent C kernels. Power flood fill,
zone decisions, PRNG, calendar, budgets, and the serial guest CPU are poorer
first targets: preserving their ordering and exact state is essential, and
small GPU jobs with immediate readback may cost more than they save.

No new GPU simulation or compositor backend is enabled in this local build.
The investigation identifies a rendering path that can avoid routine readback;
the CPU optimizations above are the implemented performance changes.
