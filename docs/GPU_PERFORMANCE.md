# Vulkan and native C performance work

Enhanced Beta 11 contains work toward steady 60 FPS on a filled Fit to Screen
view at X50 development. That goal is not yet achieved. Tests use the owner's
verified US ROM and isolated copies of their city; ROMs and saves are excluded
from the repository and releases.

## Current execution paths

`SC_LTO=ON` enables compiler-supported link-time optimization for Release and
RelWithDebInfo through the executable's CMake target property. It defaults to
OFF after the timing comparisons below; debug builds and toolchains that fail CMake's
IPO check retain that path automatically. A stale `SC_LTO` cache entry had
previously enabled no compiler flags. The private LTO build now emits real
cross-file optimization flags and matches the non-LTO largest-map replay's
complete saved state and all nine presented images. GPU copy/paste state and
presentation match too.

SDL 3.4 or newer creates a GPU renderer backed by Vulkan. The renderer and
compute compositor share SDL's device and command queue. SPIR-V is embedded
in the executable; shader compilation is offline, so players need no compiler.
Resource or backend failures retain CPU rendering. Explicit SDL software or
other renderer selections also retain that fallback.

The compositor now handles extended terrain and repaired native city pixels,
including tile decoding, roofs, live scanline palettes, brightness, windows,
SNES integer colour math, ranked BG1/BG3 candidates, evaluated objects and
power warnings. Later HUD and pointer writes preserve their existing ordering.
Reusable, cycled transfer/storage buffers feed an output texture that SDL
presents directly. Ordinary frames have no image readback or CPU fence wait.
Eligible city scanlines now delegate the native 256-pixel Mode 1 raster too.
Each row captures three independently scrolled layers as 33 live CHR tile
spans, plus palette/window/math state and the original evaluated sprite row.
Vulkan decodes and ranks those layers directly. Repaired city cells reuse the
same captured BG1/BG3 planes instead of decoding their candidates on the CPU.
Later host HUD/pointer writes keep their ordering. Sprite evaluation, limits,
OAM history, beam events and guest-visible PPU state remain on the CPU.
Native city repair now also runs in Vulkan. Each world-tile span carries its
expected/staged BG words and edit/power ownership flags; the shader replaces
stale city pixels while preserving native backgrounds, objects and HUD clipping.
The host no longer visits every native pixel to decide whether it needs repair.
CPU materialization resolves the same captured metadata. Eligibility excludes
adviser-only screens, forced blank and the map-load hold path.

Extended terrain now carries raw world-cell words instead of host-extracted
CHR plane pairs. Vulkan performs the ROM tile/roof descriptor lookup, palette
and flip extraction, CHR addressing and power-warning ownership lookup. The
host retains a separate immutable VRAM version whenever writes change it;
scanlines name their actual version, so later DMA never alters earlier rows.
Ordinary guest byte-port writes and host artwork writes publish a host-only
revision. Foreign/test PPUs and held maps use an exact live VRAM comparison.
Version storage grows geometrically and only versions used by the current
frame are uploaded. Native staging validation stays bounded to the original
256 columns. CPU fallback resolves the same raw descriptors and captured
versions. Reference capture remains available for exact comparison.

Terrain capture now shares immutable packed city-row spans between scanlines.
The host compares contiguous live bytes against the cached version and appends
a new version only when they change. Base, southeast roof and northwest power
owner rows retain independent offsets and exact map-edge padding. Vulkan
fetches those cells directly; the host prepares repair metadata only for the
original 256-pixel cache. This avoids repeating three world lookups for every
tile of every extended scanline. Held maps and stock WRAM maps use the same
exact comparison, without trusting a pointer or a revision alone. CPU fallback
resolves the same immutable spans. On frames containing only captured city
rows, the GPU upload packs repair metadata into 34 spans per row, instead of
uploading the full canvas width. Mixed/reference frames retain the full format.
The compositor uses eight read-only storage buffers and retains asynchronous
ordinary submission. `SC_CITY_SPANS_REFERENCE=1` selects the preceding raw-cell
capture for a same-source comparison. Tests mutate live city cells between
scanlines, including power bits and invalid descriptors, to verify that later
writes cannot change already captured rows.

The filled 1920x1600/50x Vulkan replay of this capture and compact-upload change
matches all 13,466,240 state bytes and all nine presented images. Opposite-order
clean pairs each contain 1,547 gameplay frames and 394,352 extra development
attempts. Reference/new mean work is 5.825/5.759 ms and 6.280/5.734 ms;
mean host raster is 2.009/1.923 ms and 2.172/1.857 ms. Thus both measured pairs
improve mean time, but the gain varies and late frames remain: reference/new
counts above 16.7 ms are 2/1 and 3/3. New-path maxima are 19.659 and 26.952 ms.
This is useful progress rather than proof of steady 60 FPS. These runs disable
profiling, validation and readback, use a single music worker and a 730x492
logical Fit canvas at scale 3 in a 2557x1480 window. The largest-map fixture is
a synthetic expansion of the saved city, rather than an independently built
1920x1600 city. Renderer tests also cover all map borders, negative extended
coordinates, mid-row changes, snapshot reuse and compact-format eligibility.

Expanded maps now carry host-only revisions for 256-cell regions. Tile setters,
interpreted mapped writes, private construction commits, clipboard paste/connection
joins, native house removal, power publication and full
reset/load/journey changes invalidate the appropriate regions. Each renderer
independently compares and copies changed regions into its history and held-map
snapshot. Quiet frames avoid scanning and copying all 3,072,000 cells. Revisions
are external to the world/save format, and the original full-map scan remains
available as a comparison path. Native stock maps retain their bounded scan.
The settled electrical-network cache also consumes these revisions independently.
It compares electrical connectivity and repairs power flags only in changed regions. Separate
comparison and publication histories preserve power changes made while a network
edit is pending, including an edit undone before the next scheduled refresh.
Changed topology still waits for the selected multiplier's original cadence;
native bitmap ownership during a flood fill is preserved. Stock maps retain
their bounded whole-map path. Atomic mouse construction commits now publish
their changed tile regions after copying out of the private preflight world.
Connectivity comparisons preserve both plant identities and the ROM's
conductivity bit. Ordinary building stages and wire artwork with the same
conductivity do not change any seed, edge, capacity or ordered traversal, so
they can reuse the settled bitmap at the original accelerated refresh cadence.
Reuse also requires the solver's `$b89` traversal predicate to match its last
actual solve. Each equivalent refresh advances the raw tile-ID snapshot;
otherwise a later scratch-only change would introduce a solve that the original
cache would have skipped. The full scratch word matters, including power flags.
Eligible adviser backgrounds and their relocated native panels now use Vulkan
too. Relocated pixels encode their source row and column, so palettes, windows,
BG3 text, sprites and colour math use the original scanline state while the
panel appears at the canvas center. CPU fallback materializes the same pixels.
Menus, map-load holds, unsupported tile modes/mosaic/interlace and
host extraction policies retain the original CPU raster. No guest frame is
replayed to obtain the captured planes.

Native C now replaces the private power interpreter and several hot simulation
operations: ordered electrical-network traversal, vacant/built land-value
cells, pollution classification/statistics, density and pollution smoothing,
crime cells, police/fire diffusion and bounded field copies, terrain-quality
cells, native power-neighbour/search iterations, zone growth scores, zone capacities,
the simulation PRNG, 16/32-bit multiply/divide loops, eight-neighbour housing
probes, bounded population-density scan spans, and accelerated development
batches, including complete ordered growth/decline decisions and empty-house
control. Residential attempts also run the complete ordered house-candidate
search, density adjustments and free-house removal in C, preserving RNG order,
the original byte operands and powered-tile comparisons. Batches yield before remaining tile-update
routines. The nine-cell zone replacement routine also runs in C: it checks
the full footprint before committing row-major tile updates, retaining each
cell's power bit and the final animation bit. It yields between footprint
cells at tight beam deadlines. Native guest registers, scratch and stack bytes, flags, cycle costs
and full map coordinates remain compatible with existing saves.

Common transport neighbour reads now run directly in C. They retain the
temporary byte indexes, reconstructed full map offsets, ten-bit tile IDs,
PHP/PLP flags, popped stack shadows, return registers and exact cycle costs.
Insufficient deadlines and map-edge paths yield to the existing guest code.
When a beam deadline splits a 16-bit multiply iteration, its intermediate
shifts, carries, additions and scratch now resume in C too. Complete iterations
retain their faster bounded loop; the final caller and inline operands keep
their existing handling. This avoids returning to ROM dispatch for the remainder
of an interrupted iteration.

The frame-wait INC/LDA/BEQ loop now executes in bounded C spans. Complete
iterations use one counter-byte update, preserving wrap, the spin seed,
registers, flags and original clocks. Residual instructions resume at their
original boundaries, and spans stop before scanline/HDMA/IRQ events. A ready
byte exit retains the original branch. Pending interrupts, unsafe direct pages
and stopped/waiting CPUs retain their original handling. 24,576 original-ROM
cases cover both CPU modes, zero/nonzero ready states, DP zero/aligned/unaligned,
byte wrap, masked IRQs, short budgets and fused iterations. The largest-map
integration matches every integer save byte, all SPC/DSP state and all nine
presented images. Regrouped floating additions change only the fractional
APU catch-up double, by approximately 2.55e-10 cycles.

Four clean opposite-order runs isolate this wait and smoothing change together.
Each has 1,547 gameplay frames and 394,352 extra development attempts on the
filled 1920x1600 fixture. Mean work is 7.511/6.937 ms reference/native in the
first pair and 7.931/8.176 ms in the second. Frames over 16.7 ms are 8/1 and
49/33 respectively. This does not establish a consistent mean-time gain or
steady 60 FPS. GPU readback, audits and profiling are disabled for these runs.

Adjacent spatial cells and their loop control now execute in bounded C spans.
The span stops before the next beam/HDMA/IRQ event and preserves the original
cycle cost. Coverage diffusion retains 16-bit overflow and the ROM's rounding
carry; replacing it with a conventional average would change the simulation.

Police/fire word-to-byte packing and clearing now use contiguous C spans,
including saturation at 255, exact stack shadows, flags and instruction clocks.
Density smoothing prices the actual selected branches before any writes, so
cells that fit a beam deadline no longer fall back merely because the remaining
budget is below a conservative 350-cycle bound. These remain interruptible at
the original beam boundaries.

Two enlarged-map initialization bugs were also corrected. Land-value scans
clear the high bytes behind the ROM's byte-sized coordinate initializers; stale
scratch previously produced a starting column of 29,952 on a 480-column grid.
Loaded scans recover impossible coordinates while retaining valid coordinates
above 255. Coverage clearing uses full unsigned linear offsets rather than an
unrelated spatial anchor, and the 1920x1600 clear tracks all 96,000 bytes across
the guest X register's 65,536-byte wrap. These corrections intentionally change
the affected simulation behavior. Native/reference optimization comparisons
include the same corrections on both paths.

The housing probe preserves all eight ordered reads, including the original
flat neighbour offsets, scratch/stack bytes, flags and CPU cost. It serves both
stock-map accelerated attempts and enlarged-map simulation. Density scans fuse
classification with full-coordinate loop control. Occupied zones now calculate
capacity, update density and accumulate their city-center coordinates in C,
including free-house probes and the game's counter overflow. The earlier
non-owner spans removed 1,808,281 dispatches from
the filled 960x800/50x replay (9,063,107 to 7,254,826). The complete integer save
and presented Vulkan image match the reference. The fractional APU clock differs
by about 1.1e-12 cycles from regrouped floating additions. Busy frames remain
above the 16.7 ms target; this is not a steady-60-FPS completion claim.
Three further alternating reference/native pairs preserve that state/image
equivalence and the same dispatch reduction. Their median emulation + drawing
+ presentation work is 8.524 ms reference versus 9.839 ms native; host timing
variation therefore does not establish a wall-time improvement for these
kernels. Keep the instruction reduction separate from any FPS claim.

Holding X pauses the native city simulation while navigating. Performance
comparisons for development therefore use an unpressed controller on the same
filled view; X-scroll rendering is validated separately. Navigation arrows
are composed over the full canvas, including rows below the native 224-line
frame. Both horizontal arrows follow its vertical extent, the bottom arrow
follows its lower border, and input mapping uses the same offsets. Hidden
directions remain hidden at map boundaries.

Arithmetic spans use the original unscaled clock, including on enlarged maps.
They execute bounded loop stages within the next beam/IRQ deadline, including
partial 16-bit divide iterations and their store/return stages. The 16-bit
division operand setup now also runs in C when its complete original cost fits:
it reads the caller's three inline byte offsets, preserves stack shadows and
operand read/write order, and leaves unsafe layouts in the original path. The
stack frame and return paths remain compatible; zero
divisors retain the game's result. Tests compare full calls as well as resumable
loop states against the original ROM.

Both six-word additive RNG helpers now run in bounded C spans, including
setup, iterations and the generator return. Unlike accelerated development's
existing whole-generator path, these spans retain the scheduler's clock:
RNG uses its existing map-area scaling, while multiply/divide remains unscaled.
The deadline budget includes the spatial clock remainder. Spans preserve carry
chaining, signed overflow, overlapping scratch layouts and partial iterations.
Power traversal now fuses capacity checks, bitmap marking, branch-stack
push/pop and outer-loop control, with tight-budget fallback before any writes.
A filled-city reference/native replay preserves every integer save byte and
the presented Vulkan image, reducing bank-03 dispatches from 7,248,695 to
6,272,644. Only the fractional APU catch-up double differs, by 2.5e-12 cycles.

The free-house location scoring loop is also native C on enlarged maps,
including accelerated attempts. It preserves the ROM's actual absolute
coordinate operands, ordered reads and stack shadows; it does not replace
them with a different neighbour-selection algorithm. Three alternating
reference/native pairs preserve the complete save and presented image exactly,
including the fractional audio clock. Dispatches fall from 6,272,644 to
5,765,531. Their median emulation + drawing + presentation work is 10.288 ms
reference versus 8.030 ms native. These measurements vary across runs; busy
native emulation frames still reach 18-28 ms, so steady 60 FPS remains unmet.

Land-value cells can now resume in C after an interruptible four-tile statistics
scan. The complete cell and resumed path share the same calculation, including
pollution clamps, scaled city-center falloff, terrain/crime adjustments and
16-bit census overflow. Three alternating RNG/land reference/native replays
preserve integer save state and presented images; their fractional APU clock
differs by 4.5e-13 cycles. Execution dispatches fall from 5,937,363 to 5,099,989.
Median emulation + drawing + presentation work is 7.513 ms versus 7.324 ms.
One native run still has 44-65 ms emulation spikes. The reduced dispatch count
therefore does not establish steady 60 FPS or a reliable wall-time gain.

The tile sweep now fuses ordinary building/terrain classification and loop
control into bounded C spans, including full-coordinate row and map ends.
Zone owners, transport handlers and active disasters remain yield points.

Three whole-field postpasses now use contiguous C arrays: traffic decay,
density expansion and transport totals. Each batch preserves its original
clock and scanline/IRQ deadline. Transport totals use the unscaled clock;
the shared clock classification now supplies both the scheduler and native
span budgets. An initial integration replay caught a wrongly scaled budget;
the corrected replay matches every integer save byte and presented pixel.
Only the fractional APU catch-up double differs, by 4.4e-11 cycles.

Extra development attempts leave the guest beam/calendar clock stationary.
Their C arithmetic and spatial helpers now use complete-helper budgets instead
of the frozen beam's remaining clocks. Original attempts retain their usual
deadline budgets. Replaying this budget change alone preserves the complete
save byte-for-byte. Free-house capacity and attempt setup also run in C.

Word-field clearing and signed decay use bounded contiguous C spans. Linear
word addressing uses unsigned indexes, fixing the upper-half quarter-grid
clear on 960x800 maps. Land-value statistics, resumed calculations, helper
calls/returns and smoothing/coordinate advances now price their actual branch
cost before committing. Resumed smoothing entries also stay in native C when
they fit the remaining deadline; rejected spans leave state untouched. The
remaining post-stencil shifts, clamp and byte store also resume in C from
every original instruction boundary, without rereading the neighbours.
The ordered neighbour sums now resume directly in C at all 29 original
instruction boundaries too, including deadlines shorter than 32 CPU cycles.
They preserve byte carries/overflow, scratch high bytes, direct-page penalties,
full expanded coordinates and exact branch clocks. The complete-cell fast path
is retained; short deadlines no longer force the remaining stencil through
ROM/bus dispatch. A reachable-state oracle checks 31,404 bounded spans against
the original ROM, including 19,599 short native yields on all four expanded
maps. This is a correctness and execution-path result; wall-time gains require
separate clean measurements.

Interactive playback has a dedicated SPC/DSP music thread. It fills the SDL
audio queue independently of city simulation, so a slow game frame does not
stop synthesis. CPU port events retain guest timestamps; protocol reads and
save/load snapshots take the same short-held mutex as the worker. Fast-forward
commands are caught up in bounded slices and stale output is trimmed. Settings
pause stops synthesis; load resets pending ports and queued playback. Headless
and scripted oracle runs retain the deterministic original audio clock unless
the music thread is explicitly enabled.

The power walk preserves the game's seed order, second-neighbour-first DFS,
plant capacities and repeated-visit counting. Stock maps retain the 5,000-entry
stack limit. Enlarged host walks use full-width cell entries and a stack sized
to the map, with 64-bit capacity/use totals. Giant maps publish that host bitmap
on the regular Normal-speed power tick as well as during accelerated refresh
and reload; the remaining guest stack retains its compatibility representation.
Those details
matter when a network exceeds capacity: a different flood order would change
which districts lose power. No GPU readback is introduced into this ordered,
CPU-consumed simulation work.

The beam driver advances between scanout, HDMA, horizontal/vertical IRQ and
line/frame-wrap events. Idle intervals update only the beam position and
controller-read countdown. The original event handlers retain their timing.
The reference two-clock driver remains available for verification.

This is a partial migration to native C, not a fully interpreter-free game.
The guest scheduler, remaining native power scan, transport boundary paths, remaining zone
mutations, construction, menus, budgets and disasters retain compatibility
execution. These remaining paths still need migration and profiling. Measured
city HUD work is much smaller than terrain capture. Enabling the repository's old experimental AOT switch would
bypass required map, input and simulation hooks and is not a supported fix.

## Measurements and limits

The connected transport C refactor removes 2,661,924 main interpreter calls
from the fixed filled Colossal replay. Its direct C control-flow version has
two opposite-order comparisons with essentially unchanged mean work:
5.715/5.723 ms reference/native, then 5.931/5.922 ms. Native runs have zero
and one gameplay frames over 16.7 ms, respectively. This demonstrates
correctness and interpreter removal, rather than a reliable frame-time gain.

The subsequent native smoothing setup and internal C continuation remove
another 777,995 interpreted instructions from that replay. Their two clean
comparisons average 13.371/13.417 ms reference/native, then 11.882/12.627 ms.
Long frames remain; these tests do not establish steady 60 FPS or a speedup.
Both controls show substantially slower unmodified host raster timing than
the earlier transport comparisons. Measurements across those separate test
sets must not be treated as a before/after speed comparison. All pairs use
1,547 gameplay frames and 394,352 extra attempts, without readback/profiling,
with the music worker enabled. The fixture repeats a 960-city into 1920x1600;
it is not the owner's actual latest Colossal save.

The raw-cell/VRAM-version Vulkan refactor has two opposite-order clean
comparisons on the filled 1920x1600 fixture and 730x492 Fit canvas at X50.
Both paths execute exactly 394,352 extra attempts over 1,547 gameplay frames.
Reference/new mean frame work is 6.367/5.647 ms, then 6.383/5.560 ms: an
11-13% reduction. CPU host raster averages 2.426/1.859 and 2.420/1.821 ms,
23-25% less. New-path p99 is 12.892/12.319 ms, with two/one frames above
16.7 ms and maxima 18.135/16.832 ms. The preceding paths have seven/two
late frames. Active-development means are 6.239/6.001 ms on the new path,
with zero/one late frame. Profiling, GPU validation/readback, test jobs and
build jobs are absent from these four serial timing runs. This is a repeatable
gain for this workload, but the remaining late frames still prevent a steady
60 FPS claim. Different sessions' absolute timings must not be mixed to
attribute a gain to this refactor.

The same refactor passes 480 independent CPU/deferred renderer frames across
all five map sizes, 96 native PPU capture cases, and actual Vulkan upload,
compute, readback and presentation comparisons. The complete largest-map
replay matches all 13,466,240 saved bytes (including the fractional APU clock)
and nine presented images against prior capture. A full live-VRAM comparison
checks the production write-revision tracking throughout that replay.

After the land-coordinate and coverage-clear corrections, the 960x800 replay
has 1,362 gameplay frames and 176,008 extra development attempts. Two clean
runs with native packing/clearing and exact density-cell budgets average 6.383
and 6.141 ms of gameplay work, with zero and two frames over 16.7 ms. The
corrections are present in both optimization paths; these counts differ from
the preceding replay of the affected simulation.

A private 1920x1600 stress fixture repeats that city across four quarters with
full-size tile and field row pitches. It has 1,547 gameplay frames and 394,352
extra attempts. The electrical-signature timing pair averages 14.452 ms with
raw-ID solves versus 14.748 ms with equivalent-network reuse. Active-development
work averages 16.580 versus 15.533 ms, but this pair does not establish an
overall speedup. The subsequent transport-neighbour pair averages 14.753 ms
reference versus 14.492 ms native; active frames over 16.7 ms fall from 179 to
141, while an isolated native frame reaches 48.551 ms. Both pairs omit profiling
and readback and use the music worker with dummy audio. They preserve the same
simulation workload. Absolute timings vary substantially between sessions;
none establishes steady 60 FPS on the largest map.

Two opposite-order LTO comparisons preserve the same workload but fail to
establish a benefit. Gameplay mean work is 13.641 ms without LTO versus 13.933
ms with it, then 11.726 ms without versus 13.728 ms with it. Gameplay frames
over 16.7 ms are 355 versus 363 and 203 versus 358. The compiler feature remains
available for further experiments, disabled by default. Correctness validation
does not turn these results into a performance claim.

After wiring both the division setup and resumed multiply entries into the
scheduler, the division-only comparison removes 958,160 bank-03 dispatches
(15,465,554 to 14,507,394). Integer save state and all nine Vulkan replay images
match; only the fractional APU clock differs by 1.2e-11 cycles. A separate
resumed-multiply comparison matches integer state/images too, removing only
885 dispatches in this workload. Neither dispatch count is an FPS result.

The final two clean division comparisons have identical 394,352 extra attempts
and 1,547 gameplay frames. Reference/native gameplay work averages 7.228/8.237
ms, then 9.038/8.358 ms in the opposite order. Native p99 is 15.672/16.782 ms;
11/17 frames exceed 16.7 ms and maximum work is 21.551/23.654 ms. This shows
the current largest-map workload usually fits the frame budget, but neither a
repeatable division-only wall-time gain nor steady 60 FPS is established.

The continuous-city replay now uses the runner's serial-order B input (`0001`)
to dismiss modal advice. Earlier `8000`/`0080` scripts used the wrong button
order and spent most of their time on adviser pages; they are excluded from
continuous-simulation evidence. The corrected 600-to-2400 replay has 1,681
warm frames: 1,378 gameplay, 303 adviser, and 177,282 extra development attempts.

Two opposite-order clean comparisons isolate region tracking from the original
expanded-map scan. On the filled 960x800, X50, 730x492 Fit canvas, gameplay mean
work falls from 10.827 to 8.433 ms and from 10.342 to 7.630 ms (22-26%). Gameplay
host raster means fall from 4.965 to 3.370 ms and from 4.772 to 3.017 ms. Frames
over 16.7 ms fall from 80 to 30 and from 57 to 22. The second native run's 241
accelerated-development frames all fit below 14.357 ms, but other gameplay
frames still reach 39.815 ms. These local measurements show a repeatable gain
for this change, not steady 60 FPS. They omit profiling, auditing, screenshots
and GPU validation, and use the music worker with SDL dummy audio.

GPU repair by itself has not established a wall-time gain. Its clean pair has
similar host raster means (3.440 versus 3.427 ms) and a large isolated emulation
stall in the new path. Keep that correctness/offload result separate from the
measured benefit of region tracking. Full-map/reference comparisons preserve
every serialized byte and all nine replay images; actual Vulkan comparisons
also cover edited tiles, power-only edits, warning removal and CPU fallback.

The latest private adviser-panel comparison (900 replay frames, 781 after
warmup) reduces mean frame work from 8.931 to 6.790 ms, p95 from 13.801 to
9.905 ms and p99 from 27.815 to 18.664 ms. Fifteen native frames still exceed
16.7 ms. The complete saved city and displayed image match exactly. This is
one timing pair, subject to host variation, and includes adviser frames;
it does not establish steady 60 FPS during continuous 50x development.
New frame CSVs separate adviser/gameplay frames and count extra attempts so
paused popup rendering cannot be confused with active simulation throughput.

A subsequent smoothing pair has 628 gameplay frames and 153 adviser frames
after warmup, with the same 61,838 extra attempts. Gameplay work averages
8.871 ms reference versus 7.115 ms native; native p95 is 10.501 ms, p99 is
22.529 ms and maximum is 27.185 ms. All 86 native frames that execute extra
development attempts are below 16.7 ms, but 15 other gameplay frames exceed
it. Integer state and presentation match; the APU fractional clock differs
by 2.2e-11 cycles. This remains a single pair, not proof of steady 60 FPS.

A repeat after the final smoothing finish migration does not establish a
stable wall-time gain: gameplay averages 9.010 ms reference versus 8.847 ms
native, but p95 rises from 12.811 to 13.754 ms and both paths miss 16.7 ms on
17 gameplay frames. Native maximum is 54.206 ms. Its integer save and image
still match, with a 3.5e-11 fractional APU-clock difference. Longer navigation
replays remained mostly on modal adviser pages and are excluded as evidence
for continuous simulation throughput. Remaining stalls require further work.

A preceding 700-frame replay (600 to 1300) on the filled 960x800/50x Fit view
compares the new divide, extra-attempt, decision, occupied-density and field
postpass paths against their reference paths. Integer save state and actual
Vulkan presentation match. Bank-03 execution-loop dispatches fall from
13,027,238 to 8,388,139 (35.6%); this count includes native dispatch boundaries
and must not be described as a measured FPS improvement. The profiled pair's
emulation/drawing/presentation averages are 18.009/1.580/0.638 ms reference and
17.473/1.871/0.691 ms native. Host timing varies substantially, and steady
60 FPS remains unproven.

Clean timing captures now omit bank profiling, render auditing and screenshot
dumps, and record buffered per-frame stage measurements. Longer filled-city
runs expose much larger development bursts than the earlier 240-frame replay.
Do not infer smoothness from a short replay average or a dispatch reduction.
One corrected 900-frame reference/native pair with the music worker enabled
has 781 frames after warmup: mean work falls from 41.898 to 32.288 ms and
95th-percentile work from 149.519 to 102.496 ms. Native work still exceeds
16.7 ms on 386 frames, with a 347.698 ms maximum. This single pair demonstrates
remaining stalls and is insufficient to establish a stable speedup.

The populated 960x800 save was replayed from frame 600 to 840 at X50 with a
730x492 Fit canvas and a 2557x1480 window. The complete viewport contains built
residential/commercial districts. On this machine the Vulkan device is an
NVIDIA GeForce RTX 5070 Ti Laptop GPU.

Two paired comparisons before the later land-value/development/beam changes
reduced emulation + drawing + presentation work from 105.337 to 62.352 ms and
100.437 to 64.592 ms per frame (about 38-41%). Complete guest states and captured
city canvases matched exactly. A third pair was rejected because Windows
clamped one run's window height, producing a different canvas size. Comparing
Direct3D and Vulkan's scaled window pixels also exposes sampling differences;
canvas comparisons and same-backend GPU presentation tests avoid conflating
those with city-rendering errors.

A later replay with the event-driven beam averaged 19.377 ms of emulation,
1.317 ms drawing and 0.419 ms presentation (21.113 ms total). Its complete guest
state, canvas and actual Vulkan presentation matched the earlier Vulkan run
byte for byte. Busy one-second windows still fell below 60 FPS and individual
frames exceeded 70 ms. Host load substantially changes these wall timings;
they include the one-off screenshot cost and are not GPU timestamp measurements.
Neither an average improvement nor the window title proves sustained 60 FPS.

The latest native spatial-span/growth-score replay reduced bank-03 interpreter
dispatches from 21,461,372 to 13,178,723 (38.6%) compared with the preceding
native power-neighbour build. It averaged 12.641 ms emulation, 1.129 ms drawing
and 0.314 ms presentation (14.084 ms total). Quiet windows reached 59.9 FPS;
busy windows still fell below 60 FPS. These are local replay observations,
not a claim that the sustained frame-rate goal is complete.

The subsequent zone-replacement replay used 11,545,266 bank-03 dispatches,
another 1,633,457 (12.4%) fewer than the growth-score build. City canvas and
actual Vulkan presentation remain byte-identical. Wall timings remain variable:
the final run averaged 31.154 ms emulation, 2.340 ms drawing and 0.757 ms
presentation, with CPU raster/compositor work also much slower than the earlier
run. Sustained 60 FPS remains unproven and is contradicted by these busy frames.

Three alternating same-build native-PPU reference/Vulkan pairs preserve the
complete serialized state, city canvas and actual presentation byte for byte.
Median emulation + drawing + presentation work is 12.308 ms with the CPU raster
and 10.099 ms with native Vulkan composition (about 18% lower). One Vulkan run
had a presentation stall and was slower than its paired reference, so this is
a local median rather than a guaranteed speedup. After also deferring fresh
BG1/BG3 candidate decoding, the final replay averaged 6.858 ms emulation,
1.129 ms drawing and 0.294 ms presentation (8.281 ms total). Its busy simulation
frame reached 25.56 ms, and the screenshot frame incurred a 119.81 ms draw cost.
The sustained 60 FPS objective remains incomplete. Eligible city's measured
native CPU pixel cost is zero; the host compositor still takes about 2.2-2.6 ms.

### Connected C land-value continuations

The expanded-city land-value, four-tile statistics and pollution classification
now resume in direct C at 175 original instruction boundaries. Straight-line
and branch edges stay in C control flow; shared coordinate hooks and dynamic
returns retain their existing scheduler behavior. Each instruction is
preflighted before a deadline can reject it. Existing complete-cell kernels
remain the first choice. Complete statistics and classification stages also
fuse into semantic C operations when their exact branch cost fits the budget;
shorter budgets retain the interrupted C path.

The initial connected-family Colossal Vulkan replay matches all integer state
and all nine presented images. The fractional APU clock differs by
1.2278e-11 cycles. Actual main bank03 interpreter instructions fall from
10,173,491 to 9,687,962, and internal compatibility instructions fall from
620,030 to 111,066. Other interpreter paths remain; this is not full interpreter
removal or proof of steady 60 FPS.

Four initial clean runs each perform 394,352 extra attempts across 1,547
gameplay frames. Mean work is 13.061/11.740 ms for reference/native in the first
pair and 6.906/8.608 ms in the reversed pair. The large order-dependent drift
also affects raster timings. The pairs disagree about the performance effect,
so these measurements do not establish a speed improvement. Late frames
remain. They precede the subsequent complete-stage fusion; measurements of
that implementation must be recorded separately.

The subsequent complete-stage fusion replay matches every one of 13,466,240
saved bytes, including fractional APU timing, and all nine presented images
against the unfused C family. Its four clean runs keep the same gameplay frame
and attempt counts. Reference/native mean work is 5.789/5.762 ms in the first
pair and 5.445/5.857 ms in the reversed pair. Native runs still have 3/4 frames
above 16.7 ms, with maxima of 19.224/21.333 ms. There is no consistent overall
speed gain or steady-60-FPS result. A subsequent diagnostic profile moves the
busiest slow phases toward power traversal/publication ($b0/$b1), the outer
spatial loop ($9c) and density ($9a/$9b). Those page counts are host dispatches;
they include native C work and must not be reported as interpreter counts.

## Verification

- Region invalidation tests cover actual placed tiles over stale staging,
  unchanged writes, guest writes across a region boundary, separate renderer
  histories, held-map updates and full reset/load invalidation. Host revisions
  do not alter the world/save encoding. Power, construction, development and
  journey tests pass after wiring all tile writers into invalidation.

- Arithmetic: 43,249 original-ROM loop/call comparisons cover complete CPU/RAM
  state and clocks, random/edge operands, zero divisors, aligned/unaligned direct
  pages, tight budgets, original stack setup/returns and safe fallback.
- Tile sweep: 27,248 cell comparisons and 288 fused spans cover ordinary
  building artwork, original counts/flags/stack bytes, all enlarged map sizes,
  row ends, map ends and bounded budgets. The filled-city replay now uses
  9,063,107 bank-03 dispatches versus 11,545,266 (21.5% fewer). Its integer
  simulation/save state and presentation match; only the serialized audio
  fraction differs by about 3.2e-11 cycles. Wall-clock pairs remain variable,
  so these counts do not establish a sustained frame-rate improvement.
- Music: an independent-worker test covers a 250 ms game-thread stall,
  timestamped port order, pause/reset/restart and shutdown. A real filled-city
  replay produced 8,192 nonzero stereo frames during that stall with no queue
  errors, preserving city RAM, CPU/population/world state and presentation.
  A cold boot passed the real sound-driver upload; its city/menu RAM and
  displayed output match the original audio path. Playback was tested using
  SDL's dummy device; listening on the user's hardware remains unverified.

- Six CTest checks cover viewport geometry, rendering, scrolling, deferred
  terrain, skipped PPU frames and beam event timing.
- 256 complete native Mode 1 frames compare captured composition against both
  original PPU renderers at every pixel. They cover fine scroll, byte/word wrap,
  flips, live VRAM/palettes, low/high OBJ palettes, windows, colour arithmetic,
  brightness and sprite limits. The fast renderer's existing OR window contract
  is preserved separately from the legacy renderer's four logic operations.
- 96 additional native/deferred-repair captures cross the Vulkan upload,
  dispatch, readback oracle and actual nearest/linear SDL presentation, including
  captured-native background reuse and suppression of stale BG1 candidates.
- 96 complete CPU/deferred/Vulkan frames cover Huge/Giant coordinates,
  wide/tall views, live planes/palettes, flips, windows, colour math, fades,
  power warnings and sprite priorities. GPU tests compare every compute pixel
  and actual SDL presentation with nearest and linear filters across resizes.
- The full ROM-backed world suite passes native visits over 48,000/192,000/
  768,000 cells, census/fields, power, coordinate seams, city centers, legacy
  migration and portable saves, plus 15,520 empty-cell and 1,536 vacant-group
  comparisons.
- 30 ordered power comparisons use the original interpreter as an independent
  oracle, plus all map sizes/speed ratios and reload/native-bitmap ownership.
  A 1920x1600 fixture with 70,000 disconnected plant seeds verifies full-width
  traversal, far-edge bitmap publication and restored power flags.
- 2,048 tile pollution classifiers, 648 spatial cells and 768 varied developed
  land cells compare full maps, RAM, registers, flags, stack and original cycles.
- 3,712 capacity/PRNG comparisons and 1,536 accelerated zone batches cover both
  stack layouts, power gating, demand, growth/decline, exact attempt counts and
  capacity recalculation. Whole-zone development tests also cover normal speed,
  every map size, population tallies, calendar and mid-attempt restoration.
- 512 complete residential candidate searches and 1,280 density/removal calls
  preserve full CPU/RAM/world state, exact clocks, PRNG ordering, byte seams,
  map borders and the original powered-house comparisons.
- 1,384 bounded word-field spans verify signed decay thresholds, clear loops,
  index wrap, field ends and immutable fallback. Land statistics pass 1,280
  bounded comparisons, including 556 complete native beginnings.
- Adviser tests include complete relocated-panel frames with independent
  native-pixel expectations, actual Vulkan readback/presentation and CPU
  materialization. Ninety-six native-protocol captures also exercise encoded
  source-row/column relocation and background-policy isolation.
- 648 native power-neighbour/search checks cover full coordinates, map edges,
  conductive/disconnected tiles and existing bitmap bits.
- 11,912 additional bounded power spans compare counter rollover, exhausted
  capacity, bitmap flags, branch-stack control, map borders and short budgets
  against the original ROM, including immutable fallback.
- 137,928 math/RNG comparisons check complete helper calls and resumable spans,
  exact CPU/RAM/cycles, carry/overflow, both caller register widths and
  overlapping RNG scratch layouts. Every fused span in the 1,024 complete
  generator/bounded-random calls is separately checked against original
  instructions at its intermediate yield state and clock budget.
- 2,560 land-value finishes compare complete state at the resumed entry,
  including vacant/developed cells, clamps, high crime, map seams and census
  rollover. The shared full-cell path also passes 768 developed and 1,536
  vacant original-ROM comparisons after the refactor.
- 1,792 house-site spans compare all enlarged sizes, borders and byte seams,
  tile eligibility, count rollover, partial loops and tight beam budgets.
- 72 bounded coverage copies, 5,576 fused spatial spans and 864 zone-growth
  scores compare the original interpreter's full RAM, fields, registers,
  flags, stack and exact cycle costs, including row/map ends and tight budgets.
- 2,048 word-to-byte coverage spans preserve saturation, widths, stack shadows,
  16-bit index seams and exact short budgets. Another 256 clear spans match the
  ROM, and complete clear passes cover all four expanded sizes through the
  65,536-byte boundary. Complete land passes cover dirty byte initializers,
  old-save recovery and valid full coordinates on the three largest grids.
- Regional power-cache tests cover all expanded sizes, metadata-only changes,
  power publication in an independent region during an edit/undo sequence,
  exact selected-speed scheduling and native bitmap ownership. The complete
  power suite also passes after private construction commit invalidation.
- All 1,024 tile IDs are checked against the ordered solver's electrical
  classes on all expanded sizes. Matching conductivity preserves the complete
  bitmap, while changing either plant identity still triggers the scheduled
  solve. Scratch-only changes and later raw-ID edits verify the `$b89` traversal
  dependency, raw snapshot advancement and flagged plant words. Reference paths
  retain the preceding raw-ID behavior. Full 960x800 and 1920x1600 replays
  preserve every serialized byte and all nine presented images.
- 512 transport neighbour calls compare complete RAM, world, registers, flags
  and original cycle costs. They cover all four expanded sizes, byte seams,
  accumulator widths, stack shadows and exact short deadlines. Another 64
  boundary calls verify that yielding leaves all state untouched.
- 1,152 smoothing finishes compare both field directions, every resumable
  finish entry, clamps, alignment, full indexes and original instruction clocks.
- 576 zone-replacement fixtures compare both the whole operation and every
  intermediate C span against the ROM. They cover blocked footprints, power
  preservation, animation bits, overflow, borders/byte seams and nearby stale
  full-coordinate anchors left by nested native scans. A sampled runtime oracle
  also passes the populated-city replay; the non-validation replay preserves
  all integer state and images. Its APU fractional double differs from the
  preceding growth-score build by about 2.3e-13 cycles.
- 912 beam sequences compare arbitrary clock chunks, IRQ modes/positions,
  controller countdown and frame/HDMA events with the two-clock reference.
- Filled-city replays preserve all integer guest state and images. The final
  spatial-span/growth-score replay differs only in the fractional APU catch-up
  double by approximately 3.6e-11 cycles, due to regrouped floating additions;
  executed APU state, master clocks and every other serialized byte match.
  Before those kernels, complete saves were byte-identical. A CPU/Vulkan
  annual-budget return replay also matched; the user's intermittent black
  budget screen remains deferred at their request, not declared fixed.

`SC_WORLD_LAND_CONTINUATION_TEST=1` compares 576 complete land calls and 3,100
interrupted spans against the ROM, covering 172 instruction boundaries, all
four expanded sizes, map edges, coordinate seams, direct-page alignment,
population tally carry and reward/pollution classes. It also checks 24,672
complete statistics/classification stages, including 8,224 immutable deadline
rejections, every ten-bit tile ID and additional full-word inputs. CPU flags,
registers, RAM, world state and original clocks match. These checks are also
part of `SC_WORLD_KERNEL_TEST=1`.

## Developer controls

`SC_LAND_CONTINUATION_REFERENCE=1` disables the connected C land family while
retaining the preceding complete-cell kernels. `SC_LAND_STAGE_REFERENCE=1`
retains the C family but disables complete statistics/classification fusion.
Both controls are developer comparisons and do not alter saved game state.

`SC_PERF=1` reports frame stages, raster/native/host costs, power costs, spatial
kernels and C development batches. Raster and power are subsets of emulation:
do not add them twice. `SC_BANK_PROFILE=1` reports remaining dispatch hot pages
at windowed exit. `SC_NATIVE_DIAG=1` records the real development frame layout.

`SC_GPU_TERRAIN=0`, `SC_NATIVE_SIMULATION=0`, `SC_SPATIAL_KERNELS=0`,
`SC_POWER_REFERENCE=1` and `SC_BEAM_REFERENCE=1` select comparison paths.
`SC_ZONE_REFERENCE=1` retains the interpreted nine-cell replacement routine.
`SC_NATIVE_PPU_REFERENCE=1` retains CPU pixel drawing for eligible city rows.
`SC_MATH_REFERENCE=1` retains interpreted arithmetic loops.
`SC_SPRITE_REFERENCE=1` retains the interpreted counted-sprite emitter.
`SC_SPRITE_DIAG=1` records its first 30 live city record layouts.
`SC_MUL16_REFERENCE=1` retains the preceding complete-iteration multiply path,
isolating native resumptions after partial beam deadlines.
`SC_DIV16_SETUP_REFERENCE=1` retains interpreted 16-bit division operand setup.
`SC_RNG_REFERENCE=1` retains the interpreted generator/bounded-random spans.
`SC_RNG_DRIVER_REFERENCE=1` retains the preceding complete RNG stage fusions
and interpreted residual/caller stages while keeping native division enabled.
`SC_LAND_FINISH_REFERENCE=1` retains the interpreted resumed land-value path.
`SC_DIV16_REFERENCE=1` retains the preceding whole-iteration divide path.
`SC_DIV16_DRIVER_REFERENCE=1` retains the preceding complete-setup/resumable-loop
division path, disabling connected setup continuations, fused iterations and
single-instruction C deadline edges for a matched comparison.
`SC_SWEEP_DRIVER_REFERENCE=1` retains the preceding whole-cell C/interpreter
city sweep, disabling connected control and coordinate-return fusion while
preserving its existing ordinary-cell fusions and full-coordinate hooks.
`SC_EXTRA_BEAM_REFERENCE=1` retains the frozen-beam extra-attempt budgets.
`SC_FREE_HOUSE_REFERENCE=1` retains free-house accelerated capacity/setup.
`SC_ZONE_DECISION_REFERENCE=1` retains growth/decline decision control.
`SC_DENSITY_OWNER_REFERENCE=1` retains occupied-zone density and centroid work.
`SC_FIELD_SWEEP_REFERENCE=1` retains the three whole-field postpasses.
`SC_RESIDENTIAL_REFERENCE=1` retains the interpreted residential candidate,
density-delta and free-house removal paths.
`SC_FIELD_WORD_REFERENCE=1` retains word-field clearing and signed decay.
`SC_LAND_BEGIN_REFERENCE=1` retains interpreted four-tile land statistics.
`SC_LAND_PIPELINE_REFERENCE=1` retains preceding land call/return and budget
selection while preserving the shared calculation and address corrections.
`SC_DIFFUSION_REFERENCE=1` retains preceding smoothing/advance budget selection
and disables the new resumed smoothing entries.
`SC_DENSITY_CELL_REFERENCE=1` retains the conservative density-cell budget.
`SC_COVERAGE_PACK_REFERENCE=1` retains interpreted word-to-byte coverage packing.
`SC_COVERAGE_CLEAR_REFERENCE=1` retains interpreted coverage clearing with the
same full-index corrections.
`SC_POWER_REGIONS_REFERENCE=1` retains whole-map power-cache comparisons and
publication; the network solver and selected refresh cadence are unchanged.
`SC_POWER_ID_REFERENCE=1` retains raw tile-ID comparisons in the regional
cache, isolating electrical connectivity comparisons from region tracking.
`SC_TRANSPORT_NEIGHBOR_REFERENCE=1` retains interpreted transport neighbour
reads, including the existing full-coordinate hooks.
`SC_ADVISOR_GPU_REFERENCE=1` retains CPU expanded adviser backgrounds.
`SC_ADVISOR_PANEL_REFERENCE=1` retains CPU rasterization of native adviser panels.
`SC_GPU_REPAIR_REFERENCE=1` retains the host's native-pixel repair walk.
`SC_MAP_TRACK_REFERENCE=1` retains whole expanded-map history scans and copies.
`SC_TERRAIN_CAPTURE_REFERENCE=1` retains host tile/roof plane extraction instead
of Vulkan's raw-cell/VRAM-version lookup. `SC_VRAM_REVISION_VALIDATE=1` compares
the full live VRAM when a revision would reuse a version, aborting on any missed
write; this is a correctness check, not a gameplay/performance setting.
`SC_RENDER_PROFILE=1` adds optional renderer-stage clocks to the frame CSV.
Row timing includes its nested object and terrain capture; other stages are
separate. These clocks stay disabled during clean timing comparisons.
`SC_POWER_SPAN_REFERENCE=1` retains interpreted power visits and branch-stack
control while keeping the existing native neighbour/search path.
`SC_HOUSE_SITE_REFERENCE=1` retains the interpreted house-location scan.
`SC_HOUSING_REFERENCE=1` retains the interpreted eight-neighbour housing probe.
`SC_DENSITY_SCAN_REFERENCE=1` retains the density scan's separate loop control.
`SC_SWEEP_REFERENCE=1` retains interpreted classification of ordinary building
artwork during the map sweep; zone owners and disaster handlers keep their
existing execution paths in either mode.
`SC_MUSIC_THREAD=0` retains synchronous audio; `1` enables the independent
worker in scripted replays too. `SC_AUDIO_STALL_AT=FRAME` deliberately blocks
the game thread for 250 ms once and logs continued SPC/DSP production.
`SC_ZONE_VALIDATE=N` compares every Nth native replacement span against a private
copy running the original ROM (`1` checks all spans). It copies full world/RAM
state and is intentionally expensive; disable it for performance measurements.
`SC_GPU_VALIDATE=1` deliberately downloads and waits for the GPU to compare
pixels; keep it disabled for gameplay and performance measurements.
`SC_WORLD_KERNEL_TEST=1` and `SC_NATIVE_HELPER_TEST=1` select focused ROM-backed
oracle tests. Test replays use `SC_SCRIPTED_INPUT=1` and private SRAM paths.
`SC_WORLD_HOUSING_TEST=1` compares 1,440 complete housing calls on all five map
sizes, including byte seams and borders. `SC_WORLD_DENSITY_SCAN_TEST=1`
compares 1,800 bounded spans, occupied-zone calls, counter carries, row/map
ends and resumable loop-control entries. `SC_WORLD_FIELD_SWEEP_TEST=1` compares
1,296 bounded postpass spans, byte saturation/decay thresholds, transport
counter overflow, 16-bit index seams and each map's field end.
`SC_WORLD_POWER_VISIT_TEST=1` and `SC_WORLD_HOUSE_SITE_TEST=1` select the focused
power and house-location original-ROM comparisons.
`SC_WORLD_LAND_FINISH_TEST=1` checks resumed cells;
`SC_WORLD_LAND_CELL_TEST=1` checks complete developed/vacant cells.
`SC_WORLD_FIELD_WORD_TEST=1` checks bounded clear/decay spans and index seams.
`SC_WORLD_LAND_BEGIN_TEST=1` checks complete and bounded land statistics.
`SC_WORLD_SPATIAL_BATCH_TEST=1` checks fused cells, resumed smoothing and loop
advances against the original ROM over short and long deadline budgets.
`SC_WORLD_DIFFUSION_FINISH_TEST=1` checks every smoothing finish entry.
`SC_WAIT_REFERENCE=1` retains the original frame-wait interpreter loop.
`UrbanRecompWaitTest <US-ROM>` checks its bounded spans against original opcodes.
`SC_WORLD_INTERPRETER_PROFILE_TEST=1` proves optional helper counters distinguish
original opcodes from C spans without changing CPU/RAM/world state or clocks.
`SC_WORLD_DIFFUSION_STENCIL_TEST=1` checks reachable intermediate neighbour
sums, exact tiny budgets, borders, byte carries and all 29 instruction boundaries.
`SC_DIFFUSION_STENCIL_REFERENCE=1` retains interpreted partial stencils while
leaving the existing whole-cell and finish kernels enabled.
`SC_WORLD_DIFFUSION_BEGIN_TEST=1` compares 1,600 reachable smoothing setups,
including 640 immutable tiny-budget yields, across all expanded map sizes.
When the whole cell does not fit a deadline, its packed-coordinate setup now
resumes directly in C. Bounded compatibility loops also invoke the verified
C stencil, finish, map/field return and land resume helpers when they reach
them, instead of interpreting those helpers internally. Full coordinate
anchors are refreshed before each new cell, including cells reached inside
a fused span. `SC_SPATIAL_CONTINUATION_REFERENCE=1` retains the earlier setup
and internal compatibility dispatch for isolated comparisons.
The filled Colossal Vulkan replay of this continuation change matches every
one of 13,466,240 saved bytes, including fractional audio timing, and all nine
presented images. Actual internal compatibility instructions fall from
1,360,131 to 620,030; main bank03 interpreter calls fall from 10,211,385 to
10,173,491. These are instruction counts, not a frame-rate claim.
`SC_WORLD_COVERAGE_PACK_TEST=1` checks packing thresholds, byte-index wrap,
register widths, partial loops and immutable short-budget fallback.
`SC_WORLD_COVERAGE_CLEAR_TEST=1` checks original-ROM clear spans and all expanded
fields from their real initialization boundary, including the 65,536-byte seam.
`SC_WORLD_COUNTER_TEST=1` runs complete land-value passes with dirty scratch,
old-save recovery and valid full coordinates on Huge, Giant and Colossal maps.
`SC_WORLD_TRANSPORT_NEIGHBOR_TEST=1` checks complete transport neighbour calls,
byte seams, stack/flag preservation and immutable deadline/boundary fallback.
`SC_WORLD_DENSITY_BATCH_TEST=1` checks Colossal density sweeps across consecutive
cells without returning to the main scheduler at every full-word coordinate
hook. The next cell is preflighted before any hook mutates coordinates, so
deadline rejection and the final map cell remain immutable. The original-ROM
oracle covers 672 cases, including 144 fused continuations and 420 short-budget
yields, byte seams, high rows, direct-page alignment and both accumulator widths.
`SC_DENSITY_BATCH_REFERENCE=1` retains the preceding per-cell scheduling path.
The filled Colossal replay matches all integer state and nine presented images;
fractional audio timing differs by 2.2737e-12 cycles. Main-loop dispatches fall
by 376,507 while actual original-interpreter opcode counts remain unchanged.
Opposite-order clean timing pairs do not show a consistent overall performance
improvement; dispatch reduction is not evidence of steady 60 FPS.
`SC_WORLD_TRANSPORT_FAMILY_TEST=1` checks the connected transport search,
route traversal, destination checks, path stack and traffic publication in C.
The original-ROM comparison covers 3,000 complete calls and 5,301 interrupted
spans on all four expanded sizes, including coordinate seams, map corners,
road/rail classes, destination types and traffic saturation. Every compared
CPU register, flag, RAM byte, world byte and original clock matches.
`SC_TRANSPORT_REFERENCE=1` disables this connected C family while retaining
the separately controlled existing neighbour helper. The implementation
resumes at 266 original instruction boundaries, performs direct C/RAM/world
operations and yields at pending interrupt or caller deadline boundaries.
Straight-line and branch edges use direct C control flow; only helper and
dynamic return edges re-enter its program-counter dispatcher. Distinct
CPU/world/RAM/ROM allocations let the compiler eliminate redundant stores
across a complete span while retaining observable interrupted state.
RNG and unsupported movement/border paths still use their scheduler hooks;
this does not mean that the full game is free of interpreter execution.
The filled Colossal Vulkan replay at 50x matches all nine presented images
and all integer/save/SPC/DSP state across the reference and native controls.
Its fractional audio catch-up double differs by 2.7285e-12 cycles due to
regrouping clock additions. This validation uses readback and profiling and
must not be used as a frame-rate measurement. Actual main bank03 interpreter
calls fall from 12,873,309 to 10,211,385 for that fixed replay; the additional
1,360,131 internal compatibility calls remain unchanged.
`SC_POWER_REGIONS_TEST=1` checks metadata-only edits, pending network edits and
undo, independent publication history, all electrical tile classes, scratch
traversal transitions and exact selected-speed refreshes.
`SC_PC_PROFILE_PAGE=HH` reports the busiest instruction addresses on a selected
page when bank profiling is enabled; `SC_BANK_PROFILE_PAGE=HH` selects its bank.
`SC_PERF_FRAME_PATH=FILE` records buffered per-frame stage timings;
`SC_PERF_EXIT_AT=FRAME` exits a private replay without taking a screenshot.
Bank profiles now also report actual main interpreter calls and original
opcodes executed inside bounded spatial helpers. Existing host-loop dispatch
counts include native C spans and omit those internal opcodes; they must not
be described as actual interpreter instructions. These additional diagnostics
are enabled only with bank profiling and stay outside serialized game state.
`SC_BANK_FRAME_PATH=FILE` enables buffered per-frame bank/page/instruction counts
to locate a particular slow frame. Profiling and GPU readback change timing;
neither is enabled in clean performance measurements.

Electrical traversal now has a connected C path covering 117 original
instruction boundaries, including neighbour tests, branch stacks and bitmap
updates. `SC_POWER_CONTINUATION_REFERENCE=1` retains the preceding path for
comparison. The original-ROM oracle checks 200 complete searches, 2,253
interrupted spans and 574 immutable deadline rejections across expanded sizes.
Power publication shares a packed-word implementation that preserves every
tile bit except its power flag and invalidates only changed render chunks.
65,544 scalar comparisons cover partial ranges, unaligned endpoints and full
maps. `SC_POWER_PUBLISH_REFERENCE=1` retains scalar final traversal publication;
construction and refresh still use the shared implementation in both controls.
Semantic neighbour/search kernels now preflight their actual branch cost;
`SC_POWER_BUDGET_REFERENCE=1` retains the earlier worst-case reservation.
The exact-budget ROM oracle covers 1,296 iterations.
The filled Colossal Vulkan replay with all three controls matches integer
state and nine presented images. Fractional APU timing differs by 2.2648e-12
cycles; main bank03 interpreter calls fall from 9,687,962 to 9,474,125.
Four serial clean runs of that implementation at 50x give reference/native
mean work of 7.293/8.094 ms and native/reference of 7.931/7.737 ms. Native runs
have 70 and 41 frames exceeding 16.7 ms. This refactor has not demonstrated a
performance improvement or steady 60 FPS; instruction counts do not prove it.

`src/sc_density.c` now carries density collection, occupied-zone classification,
centroid accounting, field expansion and land-pass initialization through 170
instruction boundaries in C. Shared semantic kernels process complete cells or
field spans when they fit; interrupted instructions retain their ordered stack,
flags, scratch and full-coordinate hooks. Smoothing and arithmetic helpers keep
their scheduler boundaries. Inline division operands are data, excluded from
the instruction graph. Register-width changes are direct C assignments rather
than calls into the CPU flag implementation. This still leaves other game paths
interpreted. `SC_DENSITY_FAMILY_REFERENCE=1` selects the preceding scheduling
path while retaining its existing density kernels.
`SC_WORLD_DENSITY_FAMILY_TEST=1` compares 160 complete collection, classification,
field and centroid stages, 294 interrupted spans and 72 immutable deadline
rejections against the original ROM on all expanded sizes. It observes 145
instruction boundaries, all byte values in full-field publication, index wrap,
32-bit tallies, byte/word scratch and both aligned/misaligned direct pages.
The integration spatial oracle passes 6,392 spans at exact beam budgets.
The connected path retains the existing fused land-cell call at its final
initialization boundary; the earlier version unnecessarily split that call.
The final filled Colossal Vulkan replay matches 13,466,240 integer/state bytes
except the fractional APU double (9.5497e-12 cycles) and matches all nine
presented images. Main bank03 interpreter calls are 9,503,162 versus 9,474,125
in the preceding path, with 111,066 internal calls in both. These counters do
not demonstrate interpreter elimination or a speedup by themselves.
Final opposite-order clean timing pairs at 50x show reference/native means
of 5.471/5.080 ms and native/reference means of 5.555/6.207 ms: about 7–11%
less measured frame work in this fixed replay. Native runs have 0 and 5 frames
over 16.7 ms out of 1,547 gameplay frames; reference runs have 6 and 13.
All runs execute 394,352 extra development attempts. Measurements use a
2557×1480 window, a 730×492 Fit canvas, one music worker, and no profiling,
GPU readback or validation. This is evidence of progress in that workload,
not proof of steady 60 FPS across gameplay or complete interpreter removal.

`src/sc_tile_lookup.c` replaces the complete US bank01 tile graphics lookup
($c772–$c806), including the three graphics-table helpers, with a connected C
path. Common valid-coordinate setup and complete table helpers run as direct
operations; 71 original boundaries allow interruption at the beam/HDMA/IRQ
deadline. Full world coordinates, scratch/stack effects, flags and hardware
multiply bus effects remain intact. The live game uses data bank $00; verified
low-WRAM mirrors and $7e are accepted, while unrelated address spaces fall back.
`SC_TILE_LOOKUP_REFERENCE=1` selects original execution for a same-source control.
An early private build accepted only bank $01 and was inert in the live game;
its identical images and state are not evidence of native execution or speed.

The final `SC_WORLD_TILE_LOOKUP_TEST=1` oracle passes 480 complete calls, 4,144
interrupted spans, 588 immutable deadline rejections and every boundary on all
five maps, including edge/seam coordinates, byte/word entry widths, direct-page
alignment and data banks $00/$01/$7e/$80. A filled 1920×1600 Vulkan replay with
held X+Left matches all integer state across 13,466,240 bytes and all nine images;
the fractional APU difference is -3.6152e-11 cycles. Actual bank01 interpreter
calls fall from 4,495,534 to 1,748,069 (about 61% fewer); bank03 and internal
counts remain unchanged. These are original opcode counters, not host dispatches.

Clean opposite-order 50x timing is mixed: reference/native means are
5.716/5.434 ms, while native/reference means are 6.529/5.242 ms. Native runs
have 4 and 11 frames over 16.7 ms; reference runs have 6 and 2, each over 1,547
gameplay frames with 394,352 extra development attempts. No profiling,
validation, readback, tests or builds ran concurrently with those four serial
measurements. This does not establish a consistent frame-time improvement or
steady 60 FPS. Most worst frames have no extra development attempts and remain
dominated by simulation. Diagnostic bank03 profiling identifies many remaining
original smoothing setup/finish and loop-control instructions; exact live
budgets can be shorter than their fused operations, so their residual paths
need further C conversion. The complete goal remains unverified.

The converted power, density, land, transport and tile-lookup families now
also expose a one-instruction C tier. The scheduler first tries whole cells
and bounded batches. When a short beam deadline prevents a batch, it can
execute one indivisible original instruction in C, using the same event
overshoot rule as the original CPU. Semantic multi-instruction fusions are
excluded from this tier; zero-clock coordinate redirects still precede exactly
one charged instruction. Pending interrupts remain with the original CPU.
The existing bounded APIs keep their deadline and immutable-rejection contract.
`SC_NATIVE_ATOMIC_REFERENCE=1` disables only this tier for a same-source control.

`SC_WORLD_ATOMIC_TEST=1` passes 11,796 individual C/ROM comparisons across
3,013 reachable map/bank/PC boundaries, including all five map sizes, full
coordinate hooks, stack/scratch effects, hardware multiply state and pending
IRQ/NMI rejection. Existing family oracles also pass 480 tile lookups, 200 power
traversals, 160 density stages, 576 land calls, 24,672 fused land stages and
3,000 transport calls. The five generated families and instruction tier
regenerate byte-for-byte from the private generation scripts.

The filled Colossal Vulkan X+Left replay matches all 13,466,240 saved-state
bytes (including the fractional APU clock) and all nine captured images.
Main original-CPU calls change from bank01 1,748,069 to 1,627,414 and bank03
8,028,815 to 7,267,221; bank00 stays 1,725,689 and compatibility-helper calls
stay 109,217. The post-conversion 1,100-frame power profile has only five main
interpreter calls in page $b0, at interrupt boundaries. The separate general
`[PC profile]` includes C dispatches and must not be treated as interpreter
counts; use `[main interpreter PCs]` and the compatibility-helper counters.

Clean opposite-order 50x timing remains mixed: reference/native means are
5.543/5.326 ms, while native/reference means are 6.074/5.898 ms. Native runs
have 3 and 19 frames above 16.7 ms; reference runs have 5 and 20, each over
1,547 gameplay frames and 394,352 extra development attempts. These four runs
are serial, with no validation, profiling, frame capture, concurrent build or
unit test. This proves interpreter removal, but does not prove a consistent
frame-time gain, steady 60 FPS or a complete native game. Power traversal is
now running in C; remaining smoothing/control work and long native simulation
frames require further architectural work. The full objective stays open.

The subsequent profile still finds 800,304 original-CPU calls in smoothing
page $a0. The worst sampled simulation frame, however, spends 22.7 ms in the
now-native power pass with no extra development attempts. These measurements
separate throughput from tail latency: complete smoothing conversion is still
needed, and native power/density stages need less instruction bookkeeping or
whole-stage GPU processing. The diagnostic frame times are not clean benchmark
results. Further work must verify raster and interrupt state as well as city
fields before claiming a 60 FPS improvement.

`src/sc_gpu_fields.c` moves both complete pollution/traffic smoothing fields
to integer Vulkan compute on the renderer's shared device. Each pass uploads
its immutable source once and produces the five-point sum together with exact
cycle and carry/overflow metadata for every cell. Independent slots permit
the alternating fields to run asynchronously. The host polls fences once per
frame and at fused publication boundaries while a result is pending, then
maps a completed download once; ordinary simulation never waits for
the GPU. Until a result is ready, the existing C path continues processing.
Starting another pass while its slot is busy invalidates the older result and
uses C. Reset/load epochs reject results from a previous city even if its world
address and dimensions match. Only resource teardown waits for unfinished work.

The ready result feeds a fused C publication loop in `sc_world_guest.c`.
Complete cells and coordinate transitions execute together within the caller's
beam deadline, retaining stack/scratch writes, field anchors, CPU flags and
exact clocks. Short remainders keep their existing native/CPU continuations.
This replaces stencil arithmetic and repeated dispatch; it does not replace
the entire game interpreter or move the calendar to accelerated time.
`SC_GPU_FIELDS_REFERENCE=1` disables just this backend for a same-source
control. `SC_GPU_FIELDS_VALIDATE=1` compares every completed GPU sample with
the integer reference and rejects a mismatching result.

`UrbanRecompGpuFieldsTest` passes 48 actual Vulkan whole-field cases across all
four expanded map sizes, byte and word phases and zero/full/random patterns, including
every sample of the largest 960×800 field. It also checks reset invalidation
of completed and in-flight jobs. `SC_WORLD_GPU_FIELD_SPAN_TEST=1` passes 1,440
publication spans and 576 immutable yields against the original ROM, covering
row transitions, edges, high indices, direct-page alignment and short budgets.
The final filled 1920×1600 Fit-to-Screen replay at 50× matches all 13,466,240
state bytes including the APU clock and all nine presented images. Over seven
million cells consume the GPU result in this replay. Validation includes GPU
downloads and image readback and is not frame-rate evidence. Clean timing
controls disable validation and image capture; the asynchronous simulation
field download remains part of the measured backend.

Four serial clean GPU-field controls each execute 1,547 gameplay frames and
394,352 extra development attempts. Reference/native mean work is
5.742/5.472 ms in the first pair; native/reference is 4.724/6.280 ms in the
opposite-order pair (about 5–25% less average work). Native runs have 20 and
2 frames over 16.7 ms, compared with 13 and 11 for reference. The longest
native frame takes 55.1 ms and spends 52.5 ms in simulation. Thus this is
evidence of lower average work, not a stable tail-latency improvement or
steady 60 FPS. No tests, builds, validation or frame capture run alongside
these timing controls.

`src/sc_smoothing.c` replaces the complete alternating-field smoothing setup,
cells, loop control and returns ($a02f–$a13a) with connected C execution.
Full GPU/native cells remain fused within a beam budget; the 130 original
instruction boundaries supply bounded continuations and indivisible C
instructions at event deadlines. `SC_SMOOTHING_REFERENCE=1` selects the
preceding implementation for a same-source comparison.

Batching explicitly yields before a new smoothing pass at $a02f/$a0b5.
Connected density calls can enter these routines without returning through
the CPU dispatcher; swallowing that boundary would bypass GPU submission and
invalidation and reuse an older source. The scheduler owns the entry hook.
`SC_WORLD_SMOOTHING_HANDOFF_TEST=1` checks both connected density calls and a
direct return into another pass. `SC_WORLD_SMOOTHING_FAMILY_TEST=1` passes
26,760 original-ROM comparisons, including 6,690 atomic instructions and
13,380 immutable yields, covering all 130 setup/cell/control/return boundaries
on the four expanded sizes and pending IRQ/NMI rejection. The GPU publication
oracle also passes with this family enabled.

The corrected filled Colossal Vulkan replay matches all 13,466,240 state
bytes and all nine presented images. Both controls submit and validate ten
GPU fields and consume 7,263,307 packed cells. Main bank03 original opcode
calls fall from 8,703,566 to 7,737,146 (966,420 fewer); bank00 1,617,994,
bank01 1,290,408 and compatibility-helper 111,066 remain unchanged. These
counters establish native execution, not a complete native game or 60 FPS.

Four subsequent serial clean C-family controls each execute the same 1,547
gameplay frames and 394,352 extra development attempts. Reference/native mean
work is 6.299/5.576 ms; native/reference is 5.808/6.100 ms in the opposite
order (about 5–11% less average work). Frames above 16.7 ms are 8/3 and
11/13 respectively. Both pairs improve the mean and number of late frames,
but the native maximum is still 24.5 ms. Stable 60 FPS and complete interpreter
removal remain unproven. The remaining main bank03 original-call profile is
led by pages $97, $94, $b6, $a3, $82 and $98; future conversion should address
whole zone-growth and simulation routines, alongside native power tail latency.

The shared Vulkan backend now also processes police/fire 16-bit fields in two
additional independent slots. Word input sizes, wrap at 65,536, rounding carry
from the second LSR, final carry and ADC overflow retain integer semantics.
The packed result includes the output value and exact cell clock. A fused C
loop publishes complete cells together with byte-coordinate control; it uses
the same integer calculation while the GPU job is pending. A service pass can
finish inside one emulated frame, so pending fences are also queried once at
each fused publication span, rather than waiting for the next host frame.
There are no per-cell fence calls or ordinary GPU waits. Cache access rejects
an epoch or field-shape change. The hardware test explicitly obtains word
results through publication polling without a host-frame poll.

`src/sc_service.c` converts the complete police/fire smoothing, setup, control
and field-copy routines ($a14d–$a24a) into connected C with 124 original
boundaries and an atomic tier. Full linear field indices survive the 64 KiB
copy crossing. `SC_SERVICE_REFERENCE=1` selects the preceding implementation
and disables word GPU jobs; `SC_GPU_SERVICE_REFERENCE=1` disables only word
GPU compute while keeping the new C family. New pass entries $a14d/$a1cc
retain the scheduler's submission/invalidation boundary.

`SC_WORLD_SERVICE_FAMILY_TEST=1` passes 24,480 original-ROM comparisons,
including 6,120 atomic instructions and 12,240 immutable yields at all 124
boundaries on all expanded maps, including copy seams and pending IRQ/NMI.
The final `SC_WORLD_SERVICE_SPAN_TEST=1` passes 1,536 accepted word publication
spans and 768 immutable yields with both cached and C-generated data.
The handoff test also checks returns into both word services. The filled
1920×1600 Fit-to-Screen 50× Vulkan replay matches all 13,466,240 state bytes,
including the APU clock, and all nine presented frames. It publishes 427,632
service cells, of which 107,540 consume GPU results. Original main bank03
calls decrease from 7,737,146 to 7,549,071 (188,075 fewer); other banks and
111,066 compatibility-helper calls stay unchanged. This is architectural and
correctness evidence; it does not establish steady 60 FPS or a full native game.

Four clean service-family timing controls each execute 1,547 gameplay frames
and 394,352 extra development attempts. Reference/native means are
5.134/5.535 ms; native/reference means are 5.043/5.542 ms in the opposite
order. Late-frame counts are 3/7 and 5/3, and the longest native work is
35.2 ms. The overall effect is mixed. Targeted service frames 1313/1314
improve in both pairs (7.655/6.134 to 6.043/5.647 ms, and 12.981/10.611 to
6.211/5.233 ms of simulation), while native power frames still exceed the
budget. These serial controls have no concurrent test/build, validation or
image capture. GPU field downloads remain part of the measured implementation.

Connected power traversal now reaches whole semantic C operations at nine
entry labels, including capacity accounting, visit marking, branch-stack
hooks and ordered neighbour search. A prepared candidate reads coordinates,
the visited bit and the tile property once, prices the complete operation,
then publishes the original scratch, flags, coordinate anchors and stack
shadows. Several consecutive neighbours can execute within one deadline.
Instruction continuations remain available when a whole operation cannot fit;
the atomic tier still executes exactly one original instruction. No parallel
flood fill changes the original traversal order or capacity behavior.
`SC_POWER_STAGE_REFERENCE=1` disables this whole-operation tier inside the
connected C traversal for a same-source comparison.

The updated original-ROM oracles pass 1,296 neighbour/search iterations and
200 complete traversals, with 2,253 interrupted spans, 574 immutable yields
and 116 reached instruction boundaries. Reusing the preceding preflight
estimate as an exact returned clock exposed a mismatch; candidate pricing now
includes the complete CPY and lookup path and matches the original execution.
The filled 1920×1600 Fit-to-Screen 50× Vulkan replay matches all 13,466,240
state bytes, including APU timing, and all nine presented images. It executes
429,017 whole C power operations accounting for 91,823,625 guest clocks.
Actual main interpreter counts remain unchanged by this optimization of
already-native execution: bank00 1,617,994, bank01 1,290,408, bank03 7,549,071.
Both controls submit, complete and validate nineteen Vulkan fields.

Four subsequent serial timing trials each execute 1,547 gameplay frames and
394,352 extra development attempts, without validation, image capture,
profiling or concurrent task-local tests/builds. No other compiler or game
process was observed during these trials. Reference/native mean work is
5.135/5.003 ms; native/reference in the opposite order is 4.961/5.239 ms,
about 2.6–5.3% less average work. Native p95 is 9.249/9.509 ms versus
10.126/10.247 ms for the corresponding references. Native maximums are
14.755/16.829 ms, with zero/one frames over 16.7 ms; references are
16.456/19.556 ms with zero/one late frames. Matched power frame 961 drops
from 15.764 to 6.552 ms of simulation, then from 12.811 to 6.375 ms in the
opposite-order pair. Other stages still vary; this is not proof of steady
60 FPS. The entire replay also matches the preceding private build's state
and nine images exactly, beyond the new same-source control.

A diagnostic GPU download mode copies completed packed fields into CPU-owned
cached RAM (`SC_GPU_FIELDS_DIRECT_REFERENCE=0`). It passes all 48 hardware
whole-field cases and a full replay with exact state and images. Four serial
timing controls did not demonstrate a benefit: direct/copied means were
5.179/5.638 ms, then copied/direct 8.071/6.097 ms; late-frame counts were
1/4 and 122/13. Another project's Android asset build was observed during
these trials, so they do not isolate all machine load. Direct mapped access
remains the default, avoiding the extra allocation and copy. Earlier isolated
word-GPU controls also showed slower means than the C calculation in both
pairs; neither field offload nor reduced instruction counts establish steady
60 FPS. The complete native game and frame-time requirement remain unfinished.

Connected residential, commercial and industrial growth/decline now execute
in `sc_zoning.c`, including zone quality gates, multi-house removal and tile
mutations. Its 354 original instruction boundaries preserve ordered writes,
flags, stack shadows and guest clocks. External drawing and RNG routines still
return to the scheduler; this does not replace the whole game interpreter.
The preceding implementation remains the default; `SC_ZONING_REFERENCE=0`
opts into this experimental family for a same-source comparison.
Original-ROM checks pass 672 complete zoning scenarios, 12,512 bounded
spans and 6,256 immutable deadline yields. A separate all-boundary oracle passes
5,088 atomic comparisons and 576 immutable mode rejections on all four expanded
map sizes, including pending IRQ/NMI checks. The interrupt test restores its
pre-instruction RAM/world snapshot before checking rejection.

The filled 1920×1600 Fit-to-Screen 50× Vulkan replay matches all integer state
and nine presented images. The only differing state is the APU fractional
accumulator, by 2.28e-13; the replay compares it with the existing 1e-8 tolerance.
Actual bank03 main interpreter calls drop from 7,549,071 to 5,216,980 (30.9%).
The GPU pipeline remains asynchronous: the reference submits/completes 19/19
fields, while native submits/completes 17/15 with two in-flight skips. The
complete final simulation state and displayed images still agree. Reduced
interpreter calls alone do not establish a frame-time improvement.

Four serial timing controls each execute 1,547 gameplay frames and 394,352
extra development attempts. Reference/native mean work is 5.653/6.715 ms;
native/reference in the opposite order is 6.338/6.519 ms. Native p95 is
12.220/11.856 ms versus reference 10.700/11.829 ms; late-frame counts are
5/11 for native and 12/5 for reference. No other compiler/game process was
observed, but the controls vary substantially and do not establish a reliable
gain. The experimental zoning family therefore remains disabled by default.
The measured faster whole-operation power path remains enabled. Stable 60 FPS
and full native game execution are still unfinished.

Zone-quality classification now also has a separate whole-operation C path,
`ScZoningQualityStep`, enabled independently of the experimental connected
family. It reads land value and pollution directly, classifies the result,
and publishes the original flags, full field anchor and popped stack shadow
after one deadline preflight. Its five branch paths retain 56/60/66/72/73
guest clocks. `SC_ZONE_QUALITY_REFERENCE=1` selects the original path.
480 complete original-ROM calls and 1,920 immutable budget/interrupt yields
pass, including thresholds, overflow, coordinate seams and stack aliases.
The full filled 1920×1600 Fit 50× Vulkan replay matches all 13,466,240 state
bytes, including APU timing, and all nine images. Main bank03 interpreter
calls fall by 494,819 to 7,054,252; both controls validate nineteen GPU fields.

Four isolated serial controls do not establish a consistent overall timing
gain: reference/native mean work is 6.751/6.668 ms, then native/reference is
6.363/6.006 ms in the opposite order. Native p95 is 11.613/11.736 ms versus
reference 11.748/10.779 ms; native late-frame counts are 8/9 versus reference
11/2. This is verified interpreter removal with exact behavior, not proof of
steady 60 FPS or a large measured throughput improvement.

The combined feature regression passes clipboard, construction, population,
development, power, Journey, map generation, music, renderer, scrolling and
video checks. Current CPU/Vulkan clipboard replays also copy 160 cells for
$5,555, exclude the reward building, auto-select Paste, retain the off-window
drag release and commit a paste. Their final state agrees byte-for-byte;
the only image difference is at most one channel unit from blending the
translucent outline. Wheel/pinch event handlers remain implemented, with
zoom geometry tested; physical touchpad delivery has not been verified.

The connected RCI artwork redraw now uses `ScWorldGuestZoneArtStep`: native
artwork selection, the ordered nine-cell writer, and stack restoration.
384 complete ROM scenarios, 1,536 bounded native spans and 1,152 immutable
yields pass. A full filled 1920x1600 Fit/50x Vulkan replay matches all
13,466,240 state bytes and all nine captured images. Main bank03 interpreter
calls fall from 7,054,252 to 6,554,343. `SC_ZONE_ART_REFERENCE=1` selects the
preceding implementation for comparison.

Four serial controls show essentially unchanged average gameplay work:
reference/native 5.221/5.225 ms, then native/reference 5.041/5.083 ms.
Native p95 is 10.349/8.610 ms; there are two/one frames above 16.7 ms.
The second native run's 29.902 ms maximum includes a 25.384 ms input stall;
the log identifies `SDL_PollEvent` returning window-exposed event 0x204.
That does not establish which Windows message or driver operation blocked.
Other-project compiler activity was observed during that run, so the
controls are not a fully isolated machine measurement. These results prove
correct interpreter removal, not a substantial overall speed gain or steady
60 FPS. Input stalls and substantial remaining interpreted code are open.

Host-thread sampling subsequently identified the accelerated live population
census as a substantial CPU cost: 764 of 10,683 samples landed in the full
census. Those are sampled locations including waiting time, not an exact
percentage of CPU time. Each refresh previously scanned all 3,072,000 cells
on the largest map.

The live census now retains per-region RCI contributions and exact tile IDs
outside the saved population state. Independent tile revisions select edited
regions; house edits also invalidate centers in neighboring rows and regions.
Power/animation metadata changes retain the existing contributions. Loads,
world changes and map expansion rebuild the derived census. Normal maps keep
the original full scan. Refresh deadlines, native capacity tallies, population
history, guest clocks and save formats are unchanged. The cache uses about
6.1 MiB; allocation failure falls back to the full scan.
`SC_POPULATION_CENSUS_REFERENCE=1` selects that scan for paired comparisons.

Dense/sparse edit, region-seam, neighboring-house, map-edge, metadata,
world-switch and decode/reset comparisons pass on all four expanded sizes.
The full filled 1920x1600 Fit/50x Vulkan replay matches all 13,466,240 saved
state bytes and all nine captures exactly. A synthetic mixed-RCI census
benchmark measures 12.344 ms per full scan, 0.023 ms per unchanged-map refresh
and 0.031 ms per single-edit refresh, averaging 1,623 recounted cells per edit.
This measures the census component only. Overall frame-time comparisons
initially waited while the owner's test copy was running. Four later serial
controls completed 1,547 gameplay frames and 394,352 extra attempts each.
Reference/native pair one measured 7.738/8.662 ms mean work; native/reference
pair two measured 10.716/15.601 ms. Audio, raster, drawing and presentation
also slowed progressively across the sequence, including stages unrelated to
the census. These inconsistent controls do not establish an overall census
speedup or steady 60 FPS. The component gain is proven; the full frame-time
effect remains unproven. Complete native execution also remains unfinished.

The free-house artwork continuation now runs in native C at its quality,
bounded-RNG call, selected-lot placement and stack-restoration boundaries.
It uses the existing full-coordinate writer, preserves flags and stack
shadows, and yields before interrupts, aliasing frames or insufficient clock
budgets. The ROM oracle passes 384 scenarios, 1,568 bounded spans and 2,656
immutable yields. Deterministic CPU and Vulkan filled-city replays each match
all 13,466,240 state bytes and nine captures. The main bank-03 interpreter
count drops from 6,554,343 to 6,391,439; this is native-code coverage evidence,
not proof of steady 60 FPS. CPU correctness replays disable the wall-clock
music worker; its independent timing makes APU state unsuitable for an exact
same-source comparison when software rendering stalls. Music-worker behavior
is checked separately.

The extended city sprite overlay now captures immutable OAM and retained
vehicle records instead of drawing their pixels into a full-width CPU surface.
Vulkan resolves live CHR, flips, palettes, ordering and clipping from spatial
buckets. Eligible frames upload only the original 256 columns of evaluated
native objects. The CPU oracle and allocation/unsupported-state fallback remain
available (`SC_GPU_OBJECT_REFERENCE=1`). The extended BG3 subscreen similarly
uses live scroll/map/CHR descriptors with immutable VRAM, rather than decoding
256 samples per extra scanline (`SC_GPU_SUB_BG_REFERENCE=1` selects the oracle).

The sprite path passes 120 independent CPU/GPU frames covering all eight size
modes, rotated OAM order, both CHR banks, live attribute/CHR edits, retained
vehicles, clipping at the core seam, and HUD/pan exclusions. The filled
1920x1600 Fit/50x X+Left replay matches all 13,466,240 saved state bytes and
four captures exactly with both GPU paths enabled. Validation/readback time is
excluded from performance claims. Two alternating unprofiled sprite-only
controls complete 1,081 gameplay frames and 160,279 extra attempts apiece:
reference/new mean work is 12.186/11.544 ms, then new/reference 11.144/12.143 ms.
Drawing averages 1.389/1.271 ms and 1.281/1.438 ms respectively. Those tests
still have frames over 16.7 ms, and include unrelated input/presentation stalls.
They show a bounded improvement, not sustained 60 FPS or complete interpreter
removal. The additional BG3 path passes 61,920 descriptor rows with all map layouts,
scroll seams, live VRAM and CPU/GPU presentation comparisons. Alternating
combined controls each complete 1,081 gameplay frames and 160,279 attempts:
reference/new means are 8.428/7.636 ms, then new/reference 6.738/6.372 ms.
The opposing pair outcomes do not establish an overall BG3 frame-time gain;
late-frame spikes still exceed the target. Host load and input/presentation
variance remain relevant. The CPU pixel decode removal and fidelity are
verified; steady 60 FPS and complete native execution remain unfinished.

Rebuild both embedded shaders after changing HLSL:

```
python tools/compile_gpu_shaders.py --dxc <path-to-dxc.exe>
```

The offline tool uses Microsoft's DirectX Shader Compiler to emit SPIR-V.
SDL owns device lifetime; the compositor releases its resources before the
renderer. See [SDL GPU renderer](https://wiki.libsdl.org/SDL3/SDL_CreateGPURenderer)
and [compute pipelines](https://wiki.libsdl.org/SDL3/SDL_CreateGPUComputePipeline).
Publishing or installing a release remains subject to the owner's instruction.

## Beta 11 connected development control pipeline

The direct C pipeline now covers connected RCI growth/decline decisions,
quality/RNG continuations, density updates and mature residential/commercial
mergers. Each merger retains the original east-before-south ordering and two
separate redraw calls. Publication yields at beam deadlines and rejects unsafe
CPU/stack layouts or pending interrupts. It is enabled by default on expanded
maps; `SC_ZONE_CONTROL_REFERENCE=1` selects the preceding implementation.
Stock maps retain their ordinary control path.

The ROM oracle passes 672 scenarios, 1,160 bounded spans, 3,128 atomic
instructions and 17,608 immutable yields across all four expanded sizes.
The default accelerated development suite passes 1,536 complete batches,
including all five sizes, RCI types, selected multipliers and power gating.
The filled 1920x1600 Fit/50x CPU and Vulkan replays match all 13,466,240 saved
state bytes and nine images. Main bank03 interpreter calls drop from
6,391,439 to 4,790,700 (25.0%); that count is coverage, not a frame-time claim.

Six serial unprofiled Vulkan controls each process 1,681 gameplay frames and
358,141 extra attempts. Reference/native simulation means are 4.496/3.794 ms,
3.428/3.301 ms and 3.995/3.247 ms. Overall mean frame work in the first two
pairs is 5.836/5.061 ms and 4.496/4.361 ms. The third reference has a separate
4.237 ms mean presentation stall versus 0.200 ms native, so its overall gain
cannot be attributed to this pipeline. Native frames still occasionally exceed
16.7 ms. These controls omit `SC_FAST_FORWARD`: merely setting that existing
diagnostic variable to `0` still activates Tab, so earlier mislabeled runs
are excluded from the normal 50x timing evidence. Complete interpreter removal
and sustained 60 FPS in every heavy phase remain open.

## Local counted-sprite C migration after Beta 11

The clean US counted-sprite emitter (`00:905c..90dc`) now assembles complete
OAM records and high-bit groups directly in C. Its setup, remainder packing,
stack restoration and short deadline continuations also use fixed C edges.
Cartridge reads retain the existing callback bus, including substituted menu
and message records. Unsafe CPU, stack, source or OAM layouts retain the
comparison path. No ROM assets are embedded by this change.

Grouped work stops before scanout/HDMA/IRQ deadlines. When no group fits,
the host executes one fixed C instruction edge with the same publication
and clock order as the original single-instruction deadline overrun. These
supported edges access only cartridge data and WRAM. The original CPU flags,
byte carry/overflow, residual stack bytes and OAM packing remain observable.

`UrbanRecompSpriteTest <clean-US-ROM>` passes 256 complete emissions,
474,656 bounded spans and 16,952 independent single-instruction edges against
the original ROM interpreter. Cases cover 1..128 records, partial deadlines,
both branch outcomes, three mirrored/data-bank layouts, coordinate wraps,
callback-provided records and immutable interrupt/unsafe fallbacks.

Two deterministic Vulkan replays use the filled 1920x1600 city, Fit at
730x492 in a 2557x1480 window, and 50x development. The 1,800-frame scrolling
replay matches nine captures; the 5,400-frame fixed-six-guest-frame Tab
replay matches all three scheduled captures. All integer data in each
13,466,240-byte save matches. The only differing bytes are the fractional
APU accumulator, with differences of -4.55e-13 and -3.90e-11 from regrouped
floating-point additions. GPU pixel validation reports no mismatches.
Music-worker timing is disabled for these exact-state comparisons.

Main bank00 interpreter calls fall from 1,617,994 to 1,557,691 in the short
replay, and from 7,858,093 to 4,830,753 in the long replay (3,027,340 fewer,
38.5%). The long workload includes adviser animation; this is native coverage
evidence, not proof of the same gain during ordinary city development.

Final timing controls disable pixel validation and profiling and enable the
music worker. Four alternating normal-50x controls each complete 1,800 guest
frames, 359,366 extra attempts and 1,681 measured city frames. Reference/native
mean work is 3.213/3.340 ms, then native/reference 3.194/3.097 ms. Those controls
do not establish a city-frame speedup. Each native run has one event/input
stall (42.324 and 32.065 ms respectively); its emulation at those frames is
5.198 and 3.014 ms. The preceding six normal controls had no measured frame
above 16.7 ms, which does not prove that later input stalls cannot occur.

Four alternating adaptive-Tab controls each perform 708,884 extra attempts
and 5,400 or 5,405 actual guest frames. Actual counts come from the runtime,
not the requested stop frame: an adaptive final batch can cross that frame.
City and adviser phases are reported separately. Reference/native adviser
simulation means are 4.169/4.259 ms, then native/reference 3.751/4.616 ms;
these opposing comparisons do not establish a consistent overall gain.
Tab city frames still have 17.767..23.738 ms emulation spikes. Startup and
pacing are included in wall-clock throughput, not in per-frame work means.

This change remains local. Vulkan migration and this C family are verified,
but remaining interpreter execution and sustained 60 FPS are unfinished.
At that stage the CPU still captured three native background layers as 33
decoded tiles per layer per native scanline. The following local migration
removes that decoding from supported Vulkan rows.

## Local native-background Vulkan migration after Beta 11

Supported Mode 1 rows now capture each layer's scroll phase, aligned horizontal
scroll, vertical coordinate, tilemap/CHR bases and map dimensions. Vulkan
resolves tilemap words, vertical flips, 2/4-bit CHR planes and horizontal
flips directly from immutable VRAM versions. This removes the CPU loop over
99 native tiles per scanline (22,176 tiles for a complete 224-line frame).
The existing shared VRAM versions preserve DMA and register changes between
scanlines; the shader never reads end-frame VRAM in place of an earlier row.

The native-row union keeps its existing 202-word GPU ABI. Palette, evaluated
OBJ, windows, color math, brightness, relocated adviser panels and city
repair policies retain their prior capture contracts. Unavailable or
unregistered VRAM versions use the decoded fallback. Unsupported display
modes continue through the existing PPU renderer. Set
`SC_GPU_NATIVE_BG_REFERENCE=1` to select the preceding decoded capture,
`SC_GPU_NATIVE_BG_DIAG=1` to confirm descriptor activation, and
`SC_VRAM_REVISION_VALIDATE=1` to check cached revisions against live bytes.

The independent original-PPU oracle passes 256 complete Mode 1 frames against
both native renderers. Every raw tile and pixel is also compared after all
later VRAM changes, with all 64 combinations of layer map dimensions, scroll
wrap, flips, OAM, windows, color math, palette and brightness changes. The
expanded CPU/Vulkan suite passes 192 decoded/raw native captures, relocated
panels, nearest/linear presentation, and materialized fallback. Its existing
480 terrain frames, 120 extended OBJ frames and 61,920 BG3 descriptor rows
also pass across all five map sizes and wide/tall canvases.

The final filled 1920x1600 Fit/50x replay passes all 1,800 guest frames with
pixel validation and exact VRAM revision checks. Nine images and every byte
of the 13,466,240-byte saved state match. Asynchronous GPU field job readiness
can change diagnostic interpreter call counts while preserving exact state;
those counts are not a native-background performance metric.

Timing uses serial Vulkan runs without pixel validation or profiling, with
the music worker enabled and restored PCM disabled. The initial reference
trial overlapped a validation replay and is excluded; a clean reference
replacement is retained. All normal-50x trials process 1,800 guest frames,
359,366 extra attempts and 1,681 measured city frames. Clean GPU mean frame
work is 3.140/4.024 ms, versus 4.150/3.067 ms for the two clean reference
controls. These variable outcomes do not establish a consistent overall
frame-time gain. One GPU trial has four frames above 16.7 ms, including a
35.877 ms input stall and a separate 19.563 ms emulation spike.

Four adaptive-Tab controls perform 708,884 attempts and 5,400..5,405 actual
guest frames. Their city means are 10.101/10.610 ms (reference/GPU) and
10.074/10.203 ms (GPU/reference). Both paths retain city emulation spikes,
including 28.556 ms with raw backgrounds. A reference adviser presentation
stall of 1,258.873 ms separately distorts that trial's overall mean; it does
not demonstrate an emulation cost or an attributable GPU improvement.

A separate profiled pair measures the CPU native-capture stage at
0.092132/0.062113 ms per city frame (decoded/raw, about 33% lower). Each
processes the same 1,800 guest frames, 359,366 attempts and 1,681 measured
city frames. Profiling overhead makes those runs unsuitable as overall
throughput comparisons. Palette, OBJ and row-state capture still occur on
the CPU. The tile decoding removal is verified; full interpreter removal
and sustained 60 FPS through every heavy phase remain unfinished. Nothing
from this local migration has been published or installed over Beta 11.

## Local connected field-postpass C migration

Live layout diagnostics show the transport-total loop at `03:b676` with
DB=3, DP=1eef, 16-bit indexes and repeated budgets as short as 3..36 CPU
clocks. The prior whole-cell C fusion could not handle many of those slices;
its interrupted remainder, setup and returns still ran in the interpreter.

`sc_postpass.c` now owns traffic decay (`03:88f3..891e`), signed growth
decay (`03:891f..894b`) and transport-total control (`03:b66a..b6bc`).
Complete cells use the existing direct array fusions; interrupted cells,
carry propagation, initialization, loop control, calls and returns use
fixed C continuations without ROM instruction fetch or opcode decoding.
The arithmetic callees retain their separate scheduling boundaries. Original
beam/IRQ deadlines and the original clock classification are unchanged.
Expanded full-index comparisons retain their zero-clock world hooks.

Stock maps use their original WRAM fields and stock bounds. Expanded maps
use the host fields and full indexes, including wrap past 65,535. The
single-instruction C path supplies the original indivisible deadline
overrun after a bounded group no longer fits. Unsupported register/stack
layouts and pending interrupts retain the prior path. The diagnostic
`SC_POSTPASS_REFERENCE=1` selects the preceding whole-cell C/interpreter
implementation, rather than disabling its existing fusions.

`UrbanRecompPostpassTest <clean-US-ROM>` passes 2,160 bounded spans,
363 independently compared instruction edges and five immutable unsafe/
interrupt cases against the original ROM interpreter across all five sizes.
It checks complete CPU, WRAM, host-field and clock state, tiny deadlines,
setup/returns, byte and word decay thresholds, carry overflow, count wrap,
unaligned direct pages and high world indexes. Neighbouring world suites
pass 3,024 whole-field and 2,688 word-field spans. The Journey suite also
passes its font, thresholds, preservation, saves, construction/power and
adviser checks.

The 1,800-frame filled 1920x1600 Fit/50x Vulkan replay matches all nine
images and every integer byte of its 13,466,240-byte save. The fractional
APU clock differs by -3.96e-11 from regrouped floating-point addition.
Music-worker timing and asynchronous GPU fields are disabled for this
exact-state comparison. Main bank03 interpreter calls fall from 4,790,700
to 4,126,760 (663,940 fewer, 13.9%). This is execution-coverage evidence;
it does not establish an equal frame-time gain or complete interpreter
removal.

The 5,400-frame fixed-six-guest-frame Tab replay also matches all three
scheduled images and every integer save byte. Its APU fraction differs
by -3.75e-11. Main bank03 interpreter calls fall from 6,570,235 to
5,889,591 (680,644 fewer, 10.4%). The full target remains unfinished;
other interpreter families and occasional heavy-frame spikes still need
conversion and measurement.

Eight isolated alternating timing controls use Vulkan, asynchronous GPU
fields and the music worker with all 19 restored PCM tracks verified loaded
at 44.1 kHz. Pixel validation and profiling are disabled. A preliminary
harness incorrectly treated `SC_RESTORED_MUSIC` as a Boolean; its log proves
SPC fallback, so its partial results are excluded. That developer setting
is a pack directory (or `0` to disable), and the final controls supply an
explicit path and assert successful loading.

Each normal-50x control completes 1,800 guest frames, 359,366 extra attempts
and 1,681 measured city frames. Mean work is 5.100/5.630 ms (reference/C)
and 5.389/5.622 ms (C/reference). There is no consistent overall gain.
The converted controls still have five and two frames over 16.7 ms, with
22.883 and 17.318 ms maximum emulation time at their worst frames.

Adaptive-Tab controls each perform 708,884 attempts and 5,402..5,405 actual
guest frames. City means improve slightly: 11.399/11.292 ms (reference/C)
and 11.193/11.423 ms (C/reference). Adviser means are 8.627/8.492 and
8.866/8.988 ms. All controls retain heavy city frames; converted emulation
spikes reach 28.967/27.342 ms. All music-worker reports show zero failures.
Actual guest counts and city/adviser phases are retained in the timing
report. Source, changes and the development EXE remain local; Beta 11 is
unchanged. Full interpreter removal and sustained 60 FPS remain open.


## Local connected division C migration after Beta 11

The native arithmetic driver now covers `03:a3cf..a420`: inline operand
setup, ordered stack shadows, interrupted setup instructions, the 16-bit
divide loop, result storage, direct-page restoration and return. The full
setup fusion is retained when it fits. Complete restoring-divide iterations
use arithmetic on words; shorter deadlines retain explicit C continuations.
The single-instruction C entry preserves the original indivisible deadline
overrun. No opcode fetch or interpreter dispatch is used for these supported
edges. The return stops before the caller's hooks and clock classification;
RNG callers keep their separate map-scaled clock. Unsupported layouts and
pending interrupts retain the prior path.

`UrbanRecompMathTest <clean-US-ROM>` passes 137,928 existing original-ROM
comparisons, 137,508 connected bounded spans, 9,822 independently compared
instruction edges and 9,822 immutable interrupt fallbacks. Starting states
for the new comparisons are reached by running the original ROM, including
both entry register widths, unaligned direct pages, low and overlapping
input stacks, zero divisors, virtual inline offsets and a real ROM caller.
Complete CPU/RAM/clocks agree at every compared boundary. Tiny budgets,
setup-to-loop fusion and all epilogue edges are covered.

The preceding implementation can be selected with
`SC_DIV16_DRIVER_REFERENCE=1`. This control retains earlier native arithmetic
and all other local migrations; it does not disable the entire math module.


Final fused-driver Vulkan replays use a filled 1920x1600 Fit view at 50x,
with the music worker and asynchronous GPU fields disabled for deterministic
state comparisons. The 1,800-frame replay matches all nine images and all
integer bytes of its 13,466,240-byte save. Its fractional APU clock differs
by -4.05e-11 from regrouped floating-point addition. Main bank03 interpreter
calls fall from 4,126,760 to 3,313,120 (813,640 fewer, 19.7%).

The 5,400-frame fixed-six-guest-frame Tab replay matches all three scheduled
images and all integer save bytes. Its APU fraction differs by -6.14e-11.
Main bank03 calls fall from 5,889,591 to 4,696,321 (1,193,270 fewer, 20.3%).
Both controls retain the native backgrounds, sprites, postpasses and all
preceding migrations. Execution counts establish greater native coverage;
frame-time gains and sustained 60 FPS require separate timing evidence.


Eight serial alternating timing controls use Vulkan and asynchronous GPU
fields with the dedicated music worker. All 19 restored PCM tracks are
verified loaded at 44.1 kHz, track 5 plays, and every worker report has zero
failures. Pixel validation and profiling are disabled. Each ordinary-50x
control completes 1,800 guest frames, 359,366 extra attempts and 1,681 warm
city frames. Mean work is 5.537/5.622 ms (reference/C) and 5.357/4.341 ms
(C/reference), so there is no consistent overall gain. The converted runs
have ten and one frames over 16.7 ms; their worst emulation times are
20.888 and 15.487 ms. Both reference runs have no warm-city overruns.

Adaptive Tab controls complete 5,400/5,405/5,402/5,400 actual guest frames
and 708,884 extra attempts each. City means improve slightly: 10.303/10.100
ms (reference/C), then 9.743/10.185 ms (C/reference). Adviser means are
4.636/4.982 and 5.466/4.704 ms. The converted city controls still have two
and nineteen frames over 16.7 ms, peaking at 22.957 and 24.196 ms. Those
peaks contain 21.668 and 22.384 ms of emulation, with small input/present
costs; this remains simulation work rather than a presentation-only issue.
Actual guest counts, phase lengths and outliers are retained separately.

These source changes and their development EXE remain local. The published
Beta 11 executable has not changed. Full interpreter removal and sustained
60 FPS are still unfinished; native coverage and mean improvements do not
establish that target.


A fresh owned-main-thread profile (diagnostic overhead, not a timing control)
collects 9,615 samples and 4,694,599 main bank03 interpreter calls. Page 82
now leads with 851,587 calls: `03:82ac` alone has 163,775 interpreted entries,
while each resumed sweep stage at `82ae..82e1` has about 25,283. This directs
the next connected conversion toward city tile-sweep setup/control and its
owner/infrastructure dispatch, preserving the already-native complete-cell
and geometry hooks. The first sampler attempt raced with its owned main
thread's exit and did not produce a sample report; it is excluded. The final
sampler checks thread/process exit, completes normally and closes only its
own handles. The source and development EXE remain unpublished.


## Local connected city sweep C migration after Beta 11

`sc_sweep.c` covers `03:8297..842e`: scan setup, tile load and owner/property
dispatch, conductivity-bit writes, roads/rail/special-building call boundaries,
ordinary tile statistics, coordinate advance, statistic publication and return.
Complete ordinary cells retain the prior array fusion. Interrupted stages use
fixed C control flow without ROM opcode fetch or interpreter decoding. The
expanded native coordinate lookup is joined to its original-clock return when
both edges fit; coordinate reader/writer returns at `84c3`/`84ea` also execute
in C. External development, infrastructure and gift handlers keep their own
scheduler hooks. Stock maps retain the original geometry-helper boundary.

Full-coordinate zero-clock hooks, array row displacement, stack shadows,
byte/word flags and per-byte tile revision publication are preserved. The
single-instruction entry disables fusion and supplies the original indivisible
clock edge at a beam/IRQ deadline. Pending interrupts, unsupported layouts and
unknown instruction addresses retain the prior path. The diagnostic
`SC_SWEEP_DRIVER_REFERENCE=1` selects that preceding path while retaining its
existing complete-cell fusions, all other local C migrations and Vulkan.

`UrbanRecompSweepTest <clean-US-ROM>` passes 4,290 bounded spans, 745
independent original-ROM instruction edges and eight immutable fallbacks,
checking complete CPU, RAM, full-world and clock state on all five sizes.
Cases cover owner zones, infrastructure, gifts, ordinary land, last rows/cells,
high map offsets, unaligned scratch, property dispatch, statistic carry and
native call/return edges. The existing 360 complete-cell span checks also pass.
Their oracle normalization now distinguishes a completed cell's zero-clock
coordinate hook from a resumed span stopping immediately before that hook.
The new independent edge checks preserve the actual before-hook boundary.

The final joined-coordinate build passes two deterministic Vulkan controls on
the filled 1920x1600 Fit fixture at 50x. The 1,800-frame control matches all
nine images and every integer byte of the 13,466,240-byte state. Main bank03
interpreter calls fall from 3,328,112 to 2,486,283 (841,829 fewer, 25.3%). The
5,400-frame fixed-batch Tab control matches all three images and all integer
state bytes; calls fall from 4,715,904 to 3,447,294 (1,268,610 fewer, 26.9%).
The only state differences are fractional APU clock regrouping, +1.30e-11
and +2.02e-11 respectively. These controls retain the preceding native math,
sprites, backgrounds and postpasses; they isolate this connected sweep.
They establish correctness and native coverage, not sustained frame rate.

Eight serial alternating frame-time controls retain asynchronous Vulkan fields
and the dedicated music worker, with profiling and pixel validation disabled.
All 19 restored PCM tracks load at 44.1 kHz, track 5 plays, and all worker
reports have zero failures. Each ordinary-50x run completes 1,800 guest frames
and 359,366 extra attempts. Warm-city mean work is 4.541/3.971 ms
(preceding/converted), then 4.045/3.892 ms (converted/preceding). Converted
p99 work is 9.583 and 9.333 ms, with zero and one warm frames over 16.7 ms;
the second control has a 25.423 ms outlier. The reversed pair prevents a
claim of consistent ordinary-50x improvement from this migration alone.

Adaptive Tab controls complete 5,405/5,405/5,400/5,400 actual guest frames and
708,884 extra attempts each. Warm-city mean work is 10.067/10.242 ms
(preceding/converted), then 9.118/10.154 ms (converted/preceding). Converted
p99 work is 17.433 and 15.722 ms, with eight and four frames over 16.7 ms;
peaks are 27.545 and 19.902 ms. Adviser means are 5.358/5.128 and 4.767/4.500
ms. Actual guest counts, city/adviser phase lengths and six largest city-frame
outliers per control are preserved separately. Native coverage improves,
but overall timing remains mixed and steady 60 FPS is not yet established.

A fresh profile of only the sampler's owned game main thread completes with
9,604 samples, 3,445,751 main bank03 interpreter calls and 149,070 kernel
interpreter calls. The previous page82 leader is removed; page90 now leads
with 615,593 main calls. `9069..907d` contains roughly 34,355 calls per
continuation of the range RNG after its native division helper, and the
`90c5..9136` zone/special-building dispatch adds about 17,134 calls per
frequent stage. Those connected caller/control paths are the next interpreter
coverage targets. Main-loop dispatch/hooks, CPU sprite evaluation and
expanded row composition also remain visible in the samples; counts alone
cannot establish which conversion will remove the frame-time spikes. The
sampler closes all of its own handles and leaves no live measurement process.

## Local connected RNG C migration after the city sweep

`sc_math.c` now connects both additive generator frames at `03:9035..90a6`.
The existing complete setup and six-word state shifts remain fused when they
fit. Shorter budgets execute fixed C continuations for ordered stack/scratch
writes, carry arithmetic, loop control and caller restoration. The bounded
generator's `9069..907d` stages also remain native around its two division
calls. Each division JSR yields to main because its unscaled math clock differs
from the map-scaled RNG clock; no batch crosses that change of clock scale.
The one-instruction entry retains exact original atomic deadline behavior.
No ROM opcode fetch or interpreter decoder runs in these supported stages.

The ROM math oracle passes 143,218 general comparisons, 137,508 connected
division spans, 9,822 independent division edges and 9,822 immutable division
interrupt fallbacks. The added independent RNG oracle passes 118,272 bounded
spans, 8,448 original-ROM instruction edges and 8,448 immutable interrupt
fallbacks. Complete CPU, RAM and clock state match. Initial flags/widths,
aligned and unaligned scratch, scratch overlapping the RNG state array, low
stack layouts, boundary ranges and all setup/epilogue stages are covered.

The 1,800-frame filled 1920x1600 Fit/50x Vulkan control matches all nine
images and every byte of the 13,466,240-byte saved state, including the
fractional APU clock. Main bank03 interpreter calls fall from 2,486,283 to
2,194,248 (292,035 fewer, 11.7%). Other local native migrations are enabled in
both controls. This is a correctness/coverage replay, not a timing result.

The 5,400-frame fixed-six-frame Tab control also matches all three images and
every saved-state byte exactly. Main bank03 calls fall from 3,447,294 to
3,085,803 (361,491 fewer, 10.5%). The saved random generator, city fields,
tile map, scratch/stack state and fractional APU clock all agree. Neither
replay establishes sustained frame rate; isolated restored-music timing is
measured separately.

Eight serial alternating restored-PCM timing controls complete with all 19
tracks loaded at 44.1 kHz, track 5 playing and zero music-worker failures.
GPU fields remain asynchronous; validation and profiling are disabled. Each
ordinary-50x run completes 1,800 guest frames, 359,366 extra attempts and
1,681 warm city frames. Mean work is 2.773/2.555 ms (preceding/converted),
then 2.997/3.427 ms (converted/preceding). Converted p99 work is 6.530 and
9.226 ms, peaks are 11.727 and 12.326 ms, and neither has a warm-city frame
over 16.7 ms. Both alternating comparisons favour the conversion in this
measurement set; this does not prove every city workload meets 60 FPS.

All four adaptive Tab controls complete 5,404 actual guest frames and
708,884 extra attempts. Mean city work is 10.059/9.975 ms
(preceding/converted), then 9.728/10.126 ms (converted/preceding). Converted
p99 work is 17.253 and 16.566 ms, peaks are 21.749 and 23.280 ms, with five
and four city frames above 16.7 ms. Adviser means are 5.028/5.159 and
4.677/4.988 ms. The remaining Tab spikes are retained in separate outlier
reports, including per-frame emulation, drawing, input and presentation.
Full interpreter removal and sustained 60 FPS remain incomplete; this
development executable is local and the published Beta 11 asset is unchanged.

Two fresh owned-main-thread samples investigate the remaining bank00 work
after this conversion. The first collects 9,540 samples: bank00 has 4,830,770
interpreted calls, while the anticipated page8f sprite-template stages are
only about 2,016 calls each. Page93 leads instead. An adaptive page93 sample
then completes with 9,596 samples and 4,835,486 bank00 calls. The frame-wait
continuations at `9311`, `9313` and `9315` account for 679,902, 408,227 and
401,386 interpreted calls respectively (1,489,515 total). Their complete
iterations already use C; deadline edges and the surrounding scheduler remain
the next substantial idle-path conversion target. CPU row composition and
sprite evaluation are also prominent in both samples, so further GPU work
must be assessed against those costs rather than interpreter counts alone.
These profiles carry sampling overhead and are not frame-time controls.
Both samplers complete normally and close only their owned process/thread
handles; no measurement process remains live.


### Connected native frame-wait scheduler (local, 2026-10-03)

`sc_wait.c` now owns all six stages at `00:930d..9317`: byte-mode setup,
ready-byte clearing, entropy-counter increment, readiness load/branch and
return. Complete idle iterations retain the existing C fusion; small budgets
use fixed C instruction continuations. Native and emulation stack returns
retain their different page-wrap behavior. No ROM decoder executes these
supported stages. `SC_WAIT_DRIVER_REFERENCE=1` retains the preceding path.

Main executes the verified WRAM-only wait lane before the gameplay hook
dispatcher. It keeps the next HBlank/line/IRQ budget, a single original edge
at line start, pending interrupt dispatch, beam advancement and APU catch-up.
Address tracing uses the ordinary observer path. This change removes host
work; it does not accelerate the guest calendar or skip entropy increments.
The existing atomic-reference diagnostic also governs the new atomic lane.

The ROM oracle passes the preceding 24,576 wait spans plus 49,152 connected
spans, 3,072 independently compared original instruction edges and 3,072
immutable pending-NMI fallbacks. Complete CPU, RAM and clocks agree. Tests
cover every stage, tiny/large budgets, ready values, counter wrap, aligned and
unaligned direct page, both register-width layouts and both stack modes,
including emulation return wrapping across `$01ff/$0100`.

The filled 1920x1600 Fit/50x Vulkan correctness control passes 1,800 frames:
all nine images and every integer byte of the 13,466,240-byte saved state
match. The only byte differences belong to the fractional APU clock; its
change is -2.274e-13. Main bank00 interpreter calls fall from 1,557,691 to
1,395,119 (162,572 fewer, 10.4%). The fixed-six-frame Tab control passes
5,400 frames with all three images and every saved-state byte exactly equal,
including that fractional clock. Main bank00 calls fall from 4,830,753 to
3,335,291 (1,495,462 fewer, 31.0%). These are coverage/correctness results,
not frame-time measurements.

Eight serial alternating timing runs load all 19 restored PCM tracks at
44.1 kHz, play track 5 and report zero music-worker failures. GPU fields are
asynchronous; validation and profiling are disabled. Each ordinary-50x
control completes 1,800 guest frames and 359,366 extra attempts, with 1,681
warm city frames. Mean work is 4.225/4.492 ms (preceding/converted), then
4.298/5.490 ms (converted/preceding). Converted p99 is 22.261/9.844 ms,
peaks are 57.822/14.456 ms, and the two runs have 37/0 warm city frames above
16.7 ms. Results are mixed; the slower control and its outliers are retained.

Adaptive Tab controls complete 5,401/5,405/5,402/5,401 actual guest frames,
each with 708,884 extra attempts. Mean city work is 11.663/11.044 ms
(preceding/converted), then 10.567/10.561 ms (converted/preceding). Converted
city p99 is 18.473/20.938 ms, peaks are 32.717/35.720 ms, with 14/16 city
frames above 16.7 ms. Adviser means are 7.740/6.236 and 6.434/6.431 ms.
There is no consistent frame-time gain across both alternating pairs.
Per-frame outliers retain input, emulation, draw and presentation costs;
wall-time emulation measurements also include possible OS preemption.

The wait migration remains local. Full interpreter removal, substantial
remaining CPU rendering work and sustained 60 FPS across heavy workloads
are incomplete. A fresh bank00 page92/main-thread profile follows these
isolated controls to select the next native/GPU family. CPU sprite evaluation
still runs even on intermediate Tab frames whose image is never presented;
any refactor must preserve OAM history, range/sliver flags and final-frame
composition rather than simply skipping guest-visible PPU work.


The fresh owned-main-thread profile completes normally with 9,481 samples.
Main bank00 has 3,337,989 interpreted calls; page92 leads with 676,419 and
page8d has 595,731. Page93 now has 165,121 interpreted calls, confirming the
wait edges have left the interpreter. The `928f..92ca` family accounts for
roughly 21,484 calls at each reported stage. Samples still place
`run_one_frame` (465), host `render_row` (388) and `ppu_evaluateSprites` (196)
above the opcode decoder (66). Thus further work must target scheduler and
host rendering costs as well as remaining interpreted families. Sampling
adds overhead and is separate from the frame-time controls. The sampler
closes only its owned process/thread handles; no measurement process remains
live. The public Beta 11 executable's SHA256 is rechecked and unchanged.


### Native OBJ descriptors and vertical OAM cache (local, 2026-10-03)

The eligible Mode 1 native raster now carries sprite sliver descriptors to
Vulkan instead of building every OBJ pixel on the CPU. Original OAM admission,
rotated order, the 32-sprite/34-sliver overflow contract, clipping and fetch
order remain on the game thread. A vertical-membership cache removes the
repeated full 128-slot scan. It compares live OAM, size, interlace and priority
state each line, so DMA, host cursor edits and mid-frame register changes
invalidate it immediately. X, margin hints and hardware admission remain live.

`sc_obj.c` retains up to 1,024 fetched slivers with their original plane
values for CPU shadows. CPU consumers request eight-pixel blocks lazily;
held-map copies and PPU save/reset/free boundaries retain the original buffer
semantics. Undisplayed Tab lines use the same selection/flag path without
constructing unused pixel rows. Unsupported PPU layouts keep the original
sprite evaluator. The original callback remains the correctness oracle.

For complete raw-background snapshots, up to 90 OBJ slivers occupy unused
words in the existing 202-word native-row ABI. Each pair packs signed X,
palette/priority, VRAM row address, horizontal flip and the clipped interval.
Vulkan searches the original winner order, reads immutable VRAM planes and
composes OBJ with native backgrounds, windows and color math. Over-capacity
rows materialize the exact CPU fallback. Source-row relocation and core repair
use the same sprite snapshot. `SC_GPU_NATIVE_OBJ_REFERENCE=1` retains the
preceding CPU object raster for matched controls.

The 256-frame PPU matrix matches both original renderers, including dense
OAM, rotation, both sprite-limit modes, 8-bit Y wrap, flips, clipping,
live VRAM/palette mutations and mid-frame position/size/priority changes.
The 32-case skipped-frame matrix preserves flags, OAM history, brightness and
the next complete displayed frame. The Vulkan matrix also passes 480 terrain
frames, 120 extended OBJ frames (29,640 immutable sprite rows), 192 native
captures, 61,920 BG3 rows and 360 compact uploads. Its raw native cases include
zero to 90 overlapping/clipped OBJ slivers, low/high palettes, relocated native
pages and the 91-sliver fallback across wide/tall layouts.

The final 1,800-frame filled-1920x1600 Fit/50x Vulkan control matches all nine
images and every byte of the 13,466,240-byte saved state, including the APU
fraction. The fixed-six-frame 5,400-frame Tab control matches all three images
and every saved-state byte too. Main bank00 calls are identical in each pair
(1,395,119 and 3,335,291 respectively): the refactor changes rendering while
retaining simulation/interrupt clocks. Pixel validation is enabled for these
replays, so they are correctness proofs rather than timing measurements.

Initial isolated restored-music timing exposed a regression. Stage probes
placed mean native capture at 0.276 ms for the preceding path and 1.032 ms for
the new path. A newly introduced diagnostic-setting lookup was repeated on
every eligible scanline when the diagnostic was absent. The final capture
caches this setting once. Earlier controls, stage probes and their outliers
remain preserved; the final executable is rebuilt and both full correctness
replays pass again after this repair. Final timing is recorded separately.

The final eight isolated restored-music controls use the same filled
1920x1600 city, Fit view, 50x development, asynchronous GPU fields and audible
19-track music worker. Profiling and pixel validation are disabled. Normal
50x warm city means are 3.554/3.543 ms (preceding/converted), then 3.412/3.308
ms (converted/preceding). All 1,681 warm frames in each run stay below 16.7 ms;
converted p99 values are 9.220/9.783 ms and peaks are 11.869/12.151 ms.
The small mean differences are mixed, rather than evidence of a large gain.

Adaptive Tab city means are 9.140/9.278 ms (preceding/converted), then
9.229/9.267 ms (converted/preceding). Actual guest frames are
5,400/5,404/5,404/5,402, each with 708,884 extra development attempts.
Converted city p99 values are 15.760/15.506 ms, peaks are 21.581/16.566 ms,
and city frames above 16.7 ms total 1/0, compared with 4/6 in the controls.
Adviser means are 4.182/4.304 and 4.250/4.477 ms. Tail behavior improves in
this fixture, but the 21.581 ms outlier and mixed means preclude claiming
sustained 60 FPS across all workloads. Every timing run and its largest
per-frame outliers remain preserved beside the earlier regression controls.

The final owned-main-thread sampler exits normally with 9,610 samples.
Remaining costs include `run_one_frame` (387), `render_row` (234), APU work,
GPU submission, world-kernel dispatch and remaining opcode execution (50).
The original sprite evaluator has left the top sampled functions. Profiling
adds overhead and is separate from the timing controls. The next substantial
work is native program execution/dispatch and remaining host row rendering;
full interpreter removal and broad workload proof remain incomplete. These
changes have not been published or installed over a prior build.

### Compatible native program tier (local, 2026-10-03)

`tools/compile_native_program.py` lowers ROM instruction sites to C using the
live host register/bus ABI. It starts from the existing function roots and
architectural vectors, follows candidate control flow through live-width
variants and emits per-address C actions across code banks. Reachability
determines coverage, rather than correctness: each action checks its live
opcode, reads operands through the original bus and evaluates live M/X,
decimal, stack and flag state. No generic opcode decoder runs inside the C
actions. Unsupported, patched and control-state entries return to the
preceding path. A changed opcode is checked through a ROM read before state
is restored for fallback; ordinary bus callbacks retain the original fetch
PC, cycle and write-site ordering.

The generated tier has 29,952 sites: 6,459 in bank00, 7,498 in bank01, 4,660
in bank02, 10,143 in bank03 and 1,192 in bank05. Generation is deterministic:
all eight output files compare byte-for-byte with an independent generation.
Generated code and the ROM stay private. `ScProgramRuntime` is shared across
host/test targets; `SC_PROGRAM=OFF` retains the original build path when the
generated tier is absent or explicitly disabled. This does not enable the
older AOT/fiber path. That path has a different ABI and bypasses required
expanded-world, menu, construction and input hooks.

The host still processes its hooks and beam/APU timing at every original
instruction boundary. Existing high-level native simulation kernels run
first; the program tier compiles their remaining compatible instructions,
including instructions inside the mapped spatial-kernel bus. This is a
foundation for connected C execution with preserved hooks, not a claim that
all residual routines have already become high-level algorithms. Remaining
opcode/interrupt fallback and dispatcher costs must still be removed.

The independent CPU matrix checks all 29,952 sites over 128 register/width/
flag patterns: 3,833,856 edges match every live CPU field, clock and ordered
bus access. Bus checks include callback-observed PC, cycles, write-site,
word-hook claims, both word write orders, decimal arithmetic, all widths,
native/emulation stack wrapping and register transfers. Padding at the end
of the C struct has no CPU meaning and is excluded explicitly; every field
through `cyclesUsed` is compared. Pending interrupts, wait/stop states,
foreign/low-PC banks, disabled execution and opcode mutations exercise
167,553 immutable CPU fallbacks. The changed-opcode fallback performs one
ROM read; it leaves all CPU fields intact before ordinary dispatch.

The 1,800-frame filled-1920x1600 Fit/50x Vulkan replay matches all nine
images and every one of 13,466,240 saved-state bytes. Main interpreter calls
drop from 4,836,151 to 17,568 (99.64%); 111,066 spatial-kernel interpreted
instructions become zero. The program tier executes 4,929,649 C edges.
The fixed-six-frame 5,400-frame Tab replay matches all three images and
every saved-state byte. Main interpreter calls drop from 8,172,421 to
97,243 (98.81%); 148,968 spatial-kernel interpreted instructions become
zero, with 8,224,146 program C edges. `SC_PROGRAM_REFERENCE=1` selects
the preceding path in both matched controls. The cold boot/title and
mouse-operated map-size menu controls also match every pixel/state byte.

The older spatial-kernel test stopped its oracle at an internal cell endpoint
that connected helpers now cross. This failed with the new tier both enabled
and disabled. The oracle now replays the actual bounded clocks returned by
the native span, comparing every continued cell, full world/RAM, stack,
registers and final boundary state against the independent CPU. The first
648 spans and further kernel tests pass with the program tier enabled.
Final timing and broader kernel results are recorded separately; interpreter
coverage alone does not establish a frame-time gain or goal completion.

Kernel validation with the tier enabled completes in two recorded portions.
The first portion passes pollution classification (2,048), bounded kernel
spans (648), developed land (768), power neighbours (1,296), transport calls
(3,000/6,653 intermediate spans), coverage copies (72), fused spatial spans
(7,000), power publications (65,544), complete power traversals (200/2,253
interrupted spans), density stages (160/294 spans), tile lookups (480/4,144
spans), fused land stages (24,672) and land calls (576/3,100 spans). It then
stops on an obsolete assertion expecting interpreted instructions in a now
compiled sample. The corrected profiling check verifies zero interpreter
counts when program C edges execute, and positive counts for the original
compatibility path, while all state/clocks stay unchanged. The remaining
portion passes smoothing setups (1,600), smoothing spans (31,404 over all
29 boundaries), smoothing finishes (1,152), Colossal density batches (672),
zone scores (864), zone replacements (576), and sweep spans (360 over all
enlarged sizes). No original state/cycle oracle is replaced with a native one.

Eight serial restored-music timing controls retain all earlier flags and
exercise the same filled 1920x1600 Fit/50x city. Native-tier normal warm means
are slower in both alternating pairs: 3.155/3.682 ms (preceding/compiled),
then 3.165/2.756 ms (compiled/preceding). Compiled city p99 values are
10.015/8.555 ms, peaks are 47.771/11.170 ms, with 1/0 frames over 16.7 ms.
The large outlier and each per-component cost remain preserved.

Adaptive Tab city means are 8.843/8.919 ms (preceding/compiled), then
9.103/9.837 ms (compiled/preceding). Actual guest counts are
5,404/5,405/5,404/5,404, each with 708,884 extra attempts. Compiled city p99
values are 18.035/17.037 ms, peaks are 23.696/21.935 ms and over-budget frames
total 5/7, compared with 15/4 in the controls. Adviser means are
4.328/3.931 and 4.961/4.826 ms; the second compiled adviser control includes
a 48.754 ms peak. All 19 restored tracks and an active 44.1 kHz worker are
verified in every run, with zero worker failures. These results are mixed,
and normal controls regress. They are preserved rather than promoted as a
performance improvement. The tier remains a local execution foundation;
connected compiled C spans must remove per-instruction dispatcher costs
before this can satisfy the performance goal. No build is published.

The final owned-main-thread sampler exits normally with 9,618 samples.
`run_one_frame` (308) and `ScWorldGuestStep` (132) remain substantial CPU
costs beside host `render_row` (144), APU work and GPU submission. The
original opcode decoder has left the leading sampled functions; generated
`ScProgramBank00` accounts for 35 samples. Kernel interpreter calls remain
zero. This profile supports connecting larger C spans and reducing redundant
hook dispatch/preparation, while preserving every required hook and beam
event. Sampling is separate from the eight timing controls. No measurement
process remains live, and the public Beta 11 executable remains unchanged.


### Connected UI/driver C lane (local 2026-10-03)

The compiled bank 00/01 actions now run in a compact connected host loop.
The admission mask contains the generated ROM instruction sites and performs
no guest bus reads or CPU mutations. Each action still validates its live
opcode. World-coordinate hooks, vehicle anchors, mapped operands and PC
coverage remain per edge. Beam events, DMA, IRQ/NMI checks and APU clock
updates also remain per edge; no scanline or audio event is deferred.

`sc_program_host_boundary` yields to the full gameplay dispatcher before
NMI/title observations, sprite/wait/tile-lookup kernels, decompression,
map generation, music control, gift observations, Journey visits,
construction/paste/census/power publication, and accelerated scroll entry
and return sites. Banks 02/03 retain the complete dispatcher and existing
larger native simulation kernels. PC traces and armed PC captures also
retain the complete path. A live opcode patch admitted by membership is
executed through the original edge if its compiled action declines, without
applying world hooks twice. `SC_PROGRAM_LANE_REFERENCE=1` disables only
this lane for same-binary controls. New bank 00/01 hooks must be added to
its boundary list.

This is a connected **host-loop** span, not a claim that every game routine
has become high-level C or that compiled actions no longer dispatch by PC.
The constant C actions still return to the compact scheduler individually.
This removes repeated full-gameplay dispatch while preserving the machinery
needed to connect larger compiled basic blocks later. The old spatial
counter no longer includes ordinary compiled program actions; separate
program/lane counters report those actions.

The instruction test verifies the admission query is immutable and performs
no bus access across all 29,952 sites, alongside the existing 3,833,856 exact
CPU/bus/cycle comparisons and 167,553 unchanged fallbacks. Paired Vulkan
replays retain every byte of the 13,466,240-byte final state, APU fraction,
and all 9 short/3 long captured images. The 1,800-frame filled Colossal
scroll replay moves 2,617,627 C edges into 53,426 lane entries. The fixed-Tab
5,400-frame adviser replay moves 4,972,583 edges into 95,013 entries.
Main interpreter counts remain identical between controls and kernel
interpreter counts stay zero. Cold boot and the map-size menu also retain
exact state and pixels. All four mouse dialog/toolbox cases (inventory,
left/right gift choices and single-message dismissal) preserve their expected
outcomes and exact final state/pixels between paths.

Eight serial timing runs keep restored music (all 19 tracks at 44.1 kHz,
worker enabled, audible output and zero failures), async GPU fields, the
filled 1920x1600 city, Fit viewport and 50x development. Validation and
sampling are off during timing. Normal city means are 3.200/2.904 ms
(preceding/connected), then 3.504/3.403 ms (connected/preceding). The first
pair improves, the second regresses. Connected normal p99 values are
8.011/9.460 ms, peaks 11.096/56.709 ms, and over-16.7-ms counts 0/1.
Each run has 1,681 warm city frames and 359,366 extra attempts.

Adaptive Tab city means are 8.871/9.801 ms (preceding/connected), then
11.483/11.515 ms (connected/preceding). City frame counts are
334/424/555/576; actual guest counts are 5,404/5,404/5,401/5,404, and each
run has 708,884 extra attempts. Connected city p99 values are
17.163/17.422 ms, peaks 34.313/31.982 ms and over-budget counts 7/20,
compared with 5/8 in controls. Adviser means are 4.281/6.941 and
7.646/8.035 ms, also mixed. Every outlier's input/emulation/draw/presentation
components are preserved. These controls do not establish a consistent
performance gain or sustained 60 FPS. The lane remains local and no
published or previously installed test build is overwritten.


The final owned-main-thread sampler exits normally with 9,635 samples.
The remaining leading game costs include `run_one_frame` (262),
`ScWorldGuestStep` (172), `ScWorldGuestFastStep` (161), host `render_row`
(208), and `ScWorldGuestBegin` (63). `ScProgramBank00` contributes 30,
`ScProgramBank03` 26, `ScProgramStep` 17 and membership admission 3.
The profile records 4,975,405 lane edges in 95,081 entries and zero kernel
interpreter calls. Samples identify work to refactor; they do not substitute
for the separate timing controls. The next target is larger compiled C
execution blocks and explicit world-hook/operand preparation at their real
consumers, followed by native simulation dispatch simplification. All
original event and mapping boundaries must remain observable.


### Direct compiled C blocks (local 2026-10-03)

Local generation now emits 162 UI/driver code pages (72 in bank 00 and
90 in bank 01). Entry uses page/PC selection, then ordinary successors use
C labels and jumps. Live PC checks retain conditional branches, register
width changes, modified operands, calls and page/bank crossings. An
unexpected target returns to page selection. Live opcode validation stays
on its original bus; a patched opcode restores the declined edge and runs
the prepared fallback. A preparation hook that redirects PC executes once,
then the new edge executes without applying that hook again.

`ScProgramRun` retains before/after callbacks at every completed edge.
The host callback keeps the preceding lane's hook exclusions, full world
preparation, coverage, guard accounting, beam events and audio clocks.
Pending control work yields before another C instruction. This changes C
control flow, but does not yet remove per-edge callback/bus/preparation work
or replace all remaining interpreter/control paths. The matched control is
`SC_PROGRAM_BLOCKS_REFERENCE=1`, with the earlier compiled program and
compact lane still enabled. Generated code stays private. Independent
regeneration verifies all 25 files byte-exact.

The original 3,833,856 instruction-edge CPU/bus/cycle checks and 167,553
immutable fallbacks pass. A further 265,951 independent connected spans
compare 2,271,551 retired edges directly against the original CPU, preserving
RAM writes across edges and checking every live field and ordered access.
They exercise all driver/UI instruction sites in 19 register patterns, plus
128-pattern NMI/IRQ/wait/stop yields and preparation redirects. The 128 live
NOP-patch cases retain the preceding scheduler's one additional pure-ROM
probe, while every following CPU/bus/clock effect matches the original CPU.

Paired Vulkan controls retain every byte of the 13,466,240-byte final states,
APU fraction and all 9 short/3 long captured images. Direct blocks execute
2,617,627 edges in the 1,800-frame filled Colossal scroll replay and
4,972,583 edges in the 5,400-frame fixed-Tab adviser replay. Main interpreter
counts, kernel interpreter counts (zero), compiled edge totals and original
clock totals stay identical between controls. Cold boot, the size menu, all
four gift/toolbox mouse cases and their final pixels/state also match.

Eight isolated restored-music timing controls do not show an improvement.
Normal city means are 3.145/3.670 ms (preceding/blocks), then
6.021/5.950 ms (blocks/preceding); emulation means are 2.316/2.710 and
4.720/4.636 ms. Both block means regress. Block p99 values are
10.667/17.094 ms, peaks 45.841/27.031 ms and over-16.7-ms counts 1/20,
compared with 4/17 in the controls. Each has 1,681 warm city frames and
359,366 extra attempts.

Adaptive Tab city means are 11.274/11.624 ms (preceding/blocks), then
11.427/11.298 ms (blocks/preceding), also slower. Block p99 values are
17.340/18.532 ms, peaks 39.258/29.066 ms and over-budget counts 16/29,
compared with 23/19 in controls. City frame counts are 608/609/610/588;
actual guest counts are 5,403/5,400/5,405/5,405 and every run has 708,884
extra attempts. Adviser means are 8.092/8.759 and 8.827/8.395 ms.
All 19 restored tracks at 44.1 kHz, audible worker output and zero worker
failures remain verified. Full per-component outliers are preserved; no
spike is discarded or attributed to the OS without evidence.

The final owned-main-thread sampler exits with 9,479 samples. Host
`render_row` contributes 376 samples, `run_one_frame` 274,
`ScWorldGuestStep` 233, `ScWorldGuestFastStep` 174,
`ScWorldGuestBegin` 91 and the per-edge block retirement callback 46.
This supports reducing actual per-instruction host/bus/preparation work and
moving more supported rendering to GPU, rather than counting removal of PC
dispatch as a speedup. The C blocks are a local execution foundation and
sustained 60 FPS remains unverified. No public/test build is overwritten.

## Shared two-dimensional OBJ grids

Supported extended sprites now share immutable 32-by-32-pixel bucket grids
across scanlines. Vulkan determines vertical admission from the captured
canvas row, preserving wrapped native OAM and unwrapped vehicle coordinates.
Live OAM, size, priority rotation and host geometry edits create a new grid;
earlier rows retain their captured version. Deferred host pixel spans use
contiguous marker fills. `SC_GPU_OBJECT_GRID_REFERENCE=1` retains the preceding
per-row admission path for matched controls.

CPU renderer and terrain checks pass, including 120 extended OBJ frames,
44,799 immutable sprite rows, mid-frame edits and five to six shared grid
versions per edited frame. Actual Vulkan/CPU presentation checks also pass.
Paired filled Colossal scroll and fixed-Tab adviser replays preserve every
byte of their 13,466,240-byte states, the APU fraction and all nine/three images
over 1,800/5,400 frames. Interpreter, compiled C and block edge counts match.
The shader is 108,108 bytes. Evidence and the preserved binary are recorded
locally in `object-grid-native-migration-final.json`.

Timing controls are deferred after the owner's session experienced a graphics
engine timeout while GPU tests were also active. The watchdog report identifies
the timeout, not its initiating cause. Subsequent GPU workloads must run
serially; this phase has no measured speedup or sustained 60 FPS claim.

## Native world preparation

Host dispatch and native simulation helpers now use compiled hook ownership
and cached operand plans. Two conservative 4-KiB bitmaps are generated from
the host coordinate/geometry source, without ROM content. Non-owner PCs avoid
the general hook dispatch. A 4,096-entry operand cache validates all four live
instruction bytes before reuse; register widths, banks, indices, world size,
anchors and bounds remain live. Frequent tile-read/write and field-coordinate
entries execute their C operation directly. The original preparation functions
remain independent oracles; `SC_WORLD_PREPARATION_REFERENCE=1` selects them.

`UrbanRecompWorldPreparationTest` runs without generated game instructions.
It compares 11,116,544 original/cached bindings and 6,553,980 hook cases across
every ROM offset, all map sizes and register widths, live instruction edits,
mirrored banks, truncated ROMs, coordinate seams and map borders. Complete
world/RAM sweeps match. RAM-only scratch clears and GPU submission entries
also receive checks at their exact boundaries, including interrupt suppression.

Filled Colossal Vulkan replays preserve every byte of their 13,466,240-byte
states, APU fraction and all nine/three images over 1,800/5,400 frames. Compiled
C, interpreter and block-edge counts match. Cold boot, size selection, both
gift choices, single-gift dismissal and toolbox selection also match their
reference state and pixels. Escape save/cancel, mouse save at 50×, and complete
1920×1600 reload pass using private saves.

The preliminary host-only timing controls are mixed. City emulation means are
2.102/1.849 ms (reference/cache), then 2.688/2.464 ms (cache/reference): the gain
does not repeat in reverse order. Tab city means improve from 8.842 to 8.254
and from 8.400 to 8.131 ms, but cached peaks remain 18.310/27.917 ms. These
controls predate the direct coordinate entries and native-helper wiring.
Eight further controls exercise native-helper wiring and direct coordinates
with all 19 restored tracks on their audible worker (zero worker failures).
City means are 4.088/4.273 ms (reference/native), then 5.148/5.141 ms
(native/reference); emulation means are 2.977/3.131 and 3.884/3.879 ms. Neither
city mean improves. Native p99 values are 12.050/14.869 ms, peaks
15.146/17.867 ms and over-budget counts 0/3, compared with 0/6 in controls.
All have 1,681 warm city frames and 359,366 extra attempts.

Tab city means improve from 11.453 to 11.076 and from 11.099 to 10.766 ms;
emulation improves from 10.190 to 9.844 and from 9.910 to 9.527 ms. Adviser
means regress from 8.280 to 8.370 and from 8.969 to 9.328 ms. Native city p99
values are 17.735/19.493 ms, peaks 32.904/36.346 ms and over-budget counts
15/27, compared with 20/25 in controls. Guest counts are
5,400/5,402/5,403/5,402, with 708,884 extra attempts in every run. Adaptive Tab
produces 579/603/692/650 warm city frames; these are throughput controls,
separate from the fixed-input state/pixel proofs. No outlier is discarded.

The cached-helper family suite (before the final direct-coordinate
specialization) passes 11,796 independent atomic CPU comparisons, 3,013 reached
boundaries, 672 zoning scenarios, 26,760 smoothing and 24,480 service comparisons,
2,160 postpass spans and 4,290 sweep spans. Final direct coordinate behavior
has its separate exhaustive preparation oracle and full replay proofs above.
An initial three-minute family-test timeout was followed by a completed run
with a sufficient limit. Timing does not establish a consistent overall gain
or sustained 60 FPS. The implementation and its original-path control remain
local development work; no public or owner test executable was replaced.

The final owned-main-thread profile contains 9,603 samples. `run_one_frame`
has 317, `render_row` 218, `ScWorldGuestFastStep` 173,
`ScWorldGuestBeginPrepared.part.0` 141 and the original `ScWorldGuestStep`
136. This points to further work on actual binding/dispatch costs. In
particular, irrelevant opcodes and non-world banks should bypass operand-plan
decoding while retaining the original unmapped descriptor and live footprint
exceptions. This profile is diagnostic evidence, not a matched timing result.

### Early world binding admission

The prepared binder now admits relevant instruction forms and live banks
before consulting the operand-plan cache. Indexed and long accesses outside
the world still return their original unmapped address/byte descriptor.
DB-independent geometry immediates and the eight verified footprint-table
consumers retain their existing exceptions. The original binder remains
unchanged and independent. Additional live DB cases include 00, 01, 7E, 80
and FF, extending the oracle to 11,567,104 binding comparisons; all 6,553,980
hook comparisons and complete world/RAM sweeps remain exact.

Fixed 1,800-frame city-scroll and 5,400-frame Tab/adviser Vulkan replays match
all 13,466,240 state bytes, APU fraction, nine/three images and execution
counts. Boot, size selection, both gift choices, single-gift dismissal and
gift toolbox selection also match. These checks compare with the independent
original hooks, separately from the preceding-cache timing comparison.

Eight serial controls compare the preceding cached binary with this binary;
both use prepared hooks, all 19 restored tracks and an audible music worker
with zero failures. At 50x, city means are 3.928/3.926 ms (preceding/new), then
4.008/2.872 ms (new/preceding); emulation means are 2.884/2.840 and 2.918/2.056
ms. New p99 values are 11.949/11.438 ms, peaks 24.860/16.614 ms and over-budget
counts 2/0, compared with 5/0 for the preceding binary. All four controls have
1,681 warm city frames, 1,800 guest frames and 359,366 extra attempts.

Adaptive Tab city means are 8.445/8.384 ms (preceding/new), then 9.243/9.744 ms
(new/preceding); emulation means are 7.655/7.622 and 8.367/8.788 ms. Adviser
means are 4.100/4.480 and 4.061/4.945 ms. New city p99 values are 15.854/15.308
ms, peaks 26.085/17.639 ms and over-budget counts 2/2, compared with 5/7 in
controls. Guest counts are 5,405/5,400/5,401/5,400, warm city frame counts
373/370/360/392, and all runs have 708,884 extra attempts. No outlier is
discarded. These results do not establish a consistent gain or sustained
60 FPS. Startup-inclusive guest throughput is kept in the evidence report
and is not presented as display FPS. No public or owner test binary was
replaced.

The fresh owned-main-thread diagnostic profile has 9,655 samples, with 1,334
local symbols qualified against COFF file records and matching symbol
addresses. It separates the native families that previously shared the name
`execute`: sweep 29, smoothing 16, tile lookup 13, land 13, transport 12 and
density 12. Other samples include `run_one_frame` 233, `render_row` 112,
`ScWorldGuestFastStep` 138, original `ScWorldGuestStep` 97 and prepared binder
body 48. This run has substantially more OS-wait samples than the preceding
profile, so those counts do not establish a per-function improvement. The
remaining main dispatch/event work, repeated hook preparation in native
families and supported GPU rendering remain the next refactor targets.

## Connected native frame-wait scheduler

During Tab fast-forward, the native wait driver now retains control across its event-bounded
continuations. Each span still advances the real beam and APU before checking
the live CPU, frame target and instruction guard. Exact-event atomic edges,
pending interrupts, invalid layouts, tracing and wait returns retain their
original exits. No scanout, HDMA or audio retirement is deferred. Bank/PC
profiling counts the same entries even when execution no longer returns to
the main gameplay dispatcher. `SC_WAIT_LANE_REFERENCE=1` retains one return
per span; a separately preserved preceding executable also serves as an
independent control.

The initial all-modes prototype's fixed 1,800-frame city-scroll replay has
317,590 native wait spans, with main-dispatch entries reduced from 317,590
to 1,475. The fixed 5,400-frame
Tab/adviser replay has 2,968,069 spans, with entries reduced to 7,999.
All 13,466,240 state bytes, including APU fraction, and all nine/three Vulkan
images match both the preceding-dispatch path and the preserved preceding
binary. Main-interpreter counts, compiled C counts and direct-block edges
are also unchanged; this converts scheduler control flow, not additional
instruction sites. Boot, map selection, both gift choices, single-gift
dismissal and toolbox selection retain exact state and pixels. Escape
open/repeat/cancel/save, F12 close, mouse save at 50x and a full 1920x1600
slot reload pass with private saves.

Eight serial restored-music controls compare this all-modes prototype with
the independent preceding executable. Every run has all 19 PCM tracks and
an audible music worker with zero failures. At 50x without Tab, city means
regress from 3.575 to 3.849 ms and from 3.777 to 3.906 ms; emulation means
are 2.570/2.802 and 2.734/2.846 ms (preceding/new). New p99 values are
11.204/11.499 ms, peaks 15.016/17.280 ms and over-budget counts 0/1, compared
with 0/0 in controls. Each run has 1,681 warm city frames, 1,800 guest frames
and 359,366 extra attempts.

Tab city means improve from 10.553 to 10.033 ms and from 10.673 to 10.006 ms;
emulation improves from 9.426 to 8.961 and from 9.554 to 8.996 ms. Adviser
means improve from 6.122 to 5.413 and from 6.241 to 5.883 ms. New city p99
values are 18.386/17.618 ms, peaks 35.423/32.838 ms and over-budget counts
13/8, compared with 4/9 in controls. Guest counts are 5,404/5,405/5,404/5,400,
warm city frame counts 456/441/428/455, and every run has 708,884 extra
attempts. No outlier is discarded. Lower means do not establish sustained
60 FPS, particularly with these higher peaks.

These results reject enabling the new loop for ordinary play. The final
implementation uses it during Tab and retains the preceding inlined native
driver without Tab. The all-modes prototype and its timing evidence remain
separately preserved; its ordinary-play results are not results for this
final policy.

The final-policy 1,800/5,400-frame Vulkan replays match the independently
preserved preceding executable in all state bytes, APU fraction, nine/three
images and execution counts. Ordinary-play wait entries remain 317,590 for
317,590 spans; Tab has 7,999 entries for 2,968,069 spans. Final-policy boot
and size-selection replays also preserve exact state and pixels. These are
the final policy's correctness checks, separate from the prototype controls.

Eight further serial controls exercise the final policy with the same
preceding binary, 19 restored tracks and audible worker (zero failures).
Ordinary 50x city means are 3.101/3.446 ms (preceding/final), then
4.283/5.384 ms (final/preceding); emulation means are 2.237/2.477 and
3.168/4.093 ms. Final p99 values are 9.039/12.277 ms, peaks 24.323/25.133 ms
and over-budget counts 2/1, compared with 0/2 in controls. Each run retains
1,681 warm city frames, 1,800 guest frames and 359,366 extra attempts.

Final-policy Tab city means are 11.213/10.744 ms (preceding/final), then
10.854/10.854 ms (final/preceding); emulation means are 9.979/9.439 and
9.543/9.566 ms. Adviser means regress from 8.650 to 8.926 and from 9.144 to
9.311 ms. Final city p99 values are 23.040/20.957 ms, peaks 30.639/38.182 ms
and over-budget counts 29/34, compared with 30/24 in controls. Guest counts
are 5,404/5,404/5,403/5,401, warm city frame counts 634/660/702/681, and
every run has 708,884 extra attempts. All startup-inclusive throughput and
component outliers are retained separately. The prototype's Tab mean gain
does not repeat clearly in these final-policy controls. Overall performance
gain and sustained 60 FPS remain unverified; this remains local development
work, with no public or owner executable replaced.

A fresh runtime trace identifies the remaining unsupported adviser page
01:A5xx (74,565 interpreter calls in the adaptive Tab replay), alongside
CPU control-state fallbacks. That coverage and remaining host/GPU work still
need conversion. Fewer dispatcher entries alone do not establish a frame-time
improvement or completion of interpreter removal.

## Native city-command dispatch and CPU control

The city tool driver uses an indexed JSR through a 56-entry ROM table. The
compiler previously followed literal calls and return continuations but
omitted that table's targets. Generation now verifies the clean-US dispatch
instruction and table, admits all 22 distinct command targets, and follows
their reachable width variants. This covers adviser, budget, gift and other
tool paths together, rather than adding only PCs observed in one trace. The
live opcode, operand, width and host-hook checks remain authoritative. The
preceding generated manifest has 31,645 sites and 58,125 width states, with
170 direct C block pages across banks 00/01. Generated ROM content stays local.

Pending NMI/IRQ entry and idle WAI/STP retirement now use native C control
against the existing register/bus ABI. NMI keeps precedence over IRQ, stopped
CPUs retain pending interrupts, and stack pushes, emulation-page wrapping,
status/vector reads, optional word callbacks and original clocks remain in
their original order. Masked-IRQ WAI wakeup that also executes an opcode
declines without changing CPU/bus state. The host still advances beam,
devices and APU at the same retirement boundary. Use
`SC_PROGRAM_CONTROL_REFERENCE=1` to retain the original control path.

The independent pinned interpreter oracle passes 4,050,560 instruction
edges across every generated site, 2,524,959 connected edges in 293,159
spans, and 98,304 interrupt/idle/decline states. The control matrix varies all
status bytes, simultaneous pending sources, waiting/stopped combinations,
native/emulation stack wrapping and absent/declining/accepting word callbacks.
The original interpreter and framework source remain unchanged.

Fixed 1,800-frame scrolling and 5,400-frame Tab/adviser Vulkan replays compare
against independently preserved captures from the preceding wait-policy
executable. All 13,466,240 saved-state bytes, APU fraction and nine/three
images match exactly, without GPU validation mismatches. Ordinary-play main
interpreter calls change from 10,302/5,510/1,756 to 9,576/776/682 in banks
00/01/03, with 1,800 native control retirements. Tab changes
14,954/80,027/2,262 to 10,968/536/849, with 5,400 native control retirements.
Compiled instruction edges rise from 8,224,146 to 8,303,636 in the latter
replay. Kernel interpreter calls remain zero. The remaining main interpreter
calls are still evidence that complete interpreter removal is unfinished.

Boot, map-size selection and all four gift/inventory replays also preserve
exact state and pixels against preceding build captures. Escape open,
repeat, cancel, save-slot and F12-close checks pass, along with mouse saving
at 50x and native slot-1/1920x1600 metadata reload, using private scratch
saves. These correctness results do not establish a frame-time improvement.


## Terrain zoom, distributed scans and native city execution

Terrain magnification now uses fixed-point inverse projection inside the
existing UI-sized CPU/Vulkan output. The tile-span allocation can grow without
expanding the UI framebuffer. Native HUD sampling retains at least the UI row
stride when zooming in. Menus, overview screens and advisers bypass terrain
projection. Mouse world selection, construction/clipboard outlines and pan
motion use the corresponding projection; HUD hitboxes retain their dimensions.
The row ABI adds the inverse step, fractional phase and virtual object origin.

A generated 1920x1600 city has identical CPU/Vulkan output at terrain zoom
1, 0.5, 0.25, 0.7, 1.125 and 2 in controlled 1024x768 windows. Native HUD crops
retain identical pixels across these levels. Main-menu, tax and annual-budget
fixtures retain identical complete images at zoom 1, 0.25 and 2. Renderer tests
check projection/selection agreement, fixed menu pixels and PPU immutability.

Expanded maps use a deterministic, bijective tile permutation that distributes
short batches across all four quarters. Each complete pass visits all 192,000,
768,000 or 3,072,000 cells exactly once. A saved scan flag and physical position
resume the same permutation. Old saves finish their row-order pass before the
next pass switches. This changes spatial update order intentionally; it does
not change calendar speed or replace normal development prerequisites.
`SC_SCAN_ORDER_REFERENCE=1` retains the original order for comparison.

On 960x800 and 1920x1600 maps, a connected native C lane runs spatial tile and
development helpers until an interrupt, event or census boundary. Extra
attempts retain stationary simulation clocks. `SC_CITY_LANE_REFERENCE=1`
retains the preceding dispatcher. With original scan order selected, a
1,200-frame 50x scrolling replay matches an independently preserved preceding
executable: all 13,466,240 saved-state bytes and five images are exact, with
31,292 city-lane entries and 1,773,496 native spans and no Vulkan mismatch.

Four serial 1,200-frame trials with restored music compare the same executable,
city and inputs, using original scan order and a 1024x768 viewport. Reference
warm CPU means are 1.717/2.022 ms versus 1.651/1.669 ms for native execution;
complete work means are 2.189/2.570 versus 2.115/2.182 ms. Each run retires
242,256 extra attempts. The native path has one 36.551 ms outlier, while the
reference maxima are 9.003/14.835 ms; improved average time does not establish
better worst-case latency. These small-window results do not establish
fullscreen or sustained 60 FPS on the largest developed city.

## Power-policy cache and population deltas

The ordered power solver has two outcomes selected by its read-only native
scratch word: traverse the electrical network, or publish generator seeds
alone. The host retains both exact bitmaps for one electrical graph. Changes
to conductivity or either generator identity invalidate both policies.
Ordinary artwork changes can reuse a prior result without changing refresh
cadence, publication ownership or the capacity cutoff's ordered brownouts.
`SC_POWER_POLICY_CACHE_REFERENCE=1` retains the preceding solve behavior.
Tests compare both policies and exhausted networks against the pinned
interpreter oracle on every expanded size, including generator changes.

Population refreshes retain an old tile-ID snapshot and apply old/new capacity
deltas only at affected zone centers. Occupied-house membership changes also
queue adjacent free-zone centers. IDs commit after all deltas, preserving
simultaneous edits across chunk and row boundaries. Full census comparisons
pass for dense/sparse edits, borders, metadata, world switches and reloads on
every expanded size. A focused 1920x1600 benchmark averages 1.1 recalculated
centers per single edit, approximately 0.010 ms per refresh; the independent
full census takes approximately 5.9 ms. The bounded queue adds roughly 12 MiB
to the host census allocation and is never serialized.

Serial filled-view tests use a developed 1920x1600 city, a 2557x1480 window,
Fit scale 3, terrain zoom 0.25, Vulkan and all 19 restored tracks on the music
thread. At 50x, the power cache reduces actual network solves from 29 to 2
with 27 exact reuses over 1,100 guest frames. Tab tests reduce 82 solves to 2
with 80 reuses over approximately 2,700 guest frames. Adaptive Tab pacing and
system variation prevent a repeatable overall frame-time gain claim.

Four balanced 50x census trials isolate this change by disabling the new
power-policy cache and comparing an independent preceding executable. Each
retires 321,685 extra development attempts. Warm complete-work means are
4.404/4.154 ms for the preceding census and 4.978/4.038 ms for zone deltas;
p99 values are 23.009/21.946 and 27.423/21.983 ms. There are 33/35 and 38/37
frames above 16.7 ms respectively. Reducing redundant simulation work is
verified, but these trials do not establish sustained 60 FPS or better
worst-case latency. Interpreter removal and remaining frame stalls are open.

With both optimizations enabled, an independent preceding-build replay at
50x on the same filled-view fixture preserves all 13,466,240 state bytes
and six images exactly, with no Vulkan validation mismatches. It records
two power solves and 27 policy reuses. Restored music is disabled for this
deterministic state comparison; the separate timing runs exercise its thread.

The native animated-CHR dispatcher also uses an indexed JSR table: 24 phases
at 00:8745, called by 00:8741. Generation now verifies that dispatch and admits
all five distinct targets, including its no-op return phase. The manifest
contains 31,749 sites and 58,341 width states. DMA registers, counter updates,
live widths/operands and retirement clocks remain in the C instruction path.
The independent oracle passes 4,063,872 instruction edges, 2,541,890 connected
edges in 295,135 spans, and 98,304 interrupt/idle/decline states. These coverage
and correctness changes do not by themselves establish a frame-time gain.

The animated-CHR replay also preserves all 13,466,240 state bytes and six
images against the independently preserved power/census build, with Vulkan
validation enabled. Main interpreter calls fall from 6,398 to 1,100 in bank
00, with bank 01 unchanged at 617 and kernel bank 03 at zero over 1,100 guest
frames. The remaining bank-00 calls are in the live-patched view routine;
bank-01 calls are on pages 8d, 8e and b6. Complete removal remains unfinished.
Boot and map-size menu replays also retain identical complete state and pixels
against that preceding executable using scratch saves.

## Native UI tables and live view patch

Generation additionally validates the 11-entry syscall, 12-entry UI-reason,
23-entry screen, five-entry ending and two three-entry ordinary-zone tables.
Every distinct handler is a reachability root, including the idle return.
The clean-US view routine's verified four-byte STA patch has explicit native
NOP variants; other unexpected live opcodes retain the decline path. No
runtime opcode decoder is added. The manifest now has 32,294 instruction
sites, 59,511 width states and 171 direct C block pages.

The independent pinned interpreter oracle passes 4,133,632 instruction edges,
2,598,222 connected edges in 302,279 spans, 98,304 control/decline states and
512 patched NOP edges with exact CPU fields, ordered bus reads and clocks.
The 1,100-frame filled-city/50x scrolling replay preserves every one of
13,466,240 state bytes and six images against the independent animated-CHR
executable, with Vulkan validation enabled. Main and kernel 65816 interpreter
calls are both zero in that replay.

An owned-thread locator run with restored music and Tab also records zero
main/kernel interpreter calls across 5,100 guest frames. Its 9,540 samples
identify projected row composition as the largest named executable sample
group (352), followed by the host frame driver (231) and GPU upload/dispatch
(153). Host raster work averages approximately 2.6 ms per warm display frame,
including 1.55 ms for rows. Sampling interferes with timing; this is a locator,
not a controlled performance comparison. Its p99 complete-work time exceeds
49 ms and it contains a long presentation stall. Sustained 60 FPS remains
unproven. No whole-field GPU jobs occur in this fixture interval, so that path
and complete simulation cycles still need broader runtime validation.

`SC_MUSIC_PROFILE=1` measures synchronous SPC protocol catch-up on the game
thread, including its mutex wait, and reports calls above 5 ms plus aggregate
cycles/time at shutdown.
The default music path has no added clock measurements or log output.

Boot and map-size menu state/pixel comparisons pass against the independent
animated-CHR executable after these changes. In a separate 2,700-guest-frame
Tab locator, 2,699 protocol reads spend 434.852 ms total in chip catch-up
(maximum 1.687 ms) and 3.971 ms total waiting for the music mutex (maximum
0.841 ms). The worker reports audible samples and zero failures. This does
not explain the observed 25-52 ms frame spikes in that run, so wholesale audio
thread changes are not justified by this evidence. The next optimization
target remains host row composition and the long native simulation phases.

## Infrastructure phases and projected row spans

A per-frame native-edge locator correlates the long filled-city frames with
infrastructure pages a4/a5/a7, rather than the number of stationary-clock extra
zone attempts. In a 1,100-guest-frame 50x replay, slow frames retire roughly
110,000-150,000 bank-03 edges, largely in those pages. This identifies a
scheduler/handler target; the edge-count instrumentation is not a timing oracle.

The city lane now continues road/rail/bridge upkeep and its field-address
helper, keeping the same zero-clock world hooks, RNG stream and event limits.
The a70c distance probe computes its complete byte arithmetic directly in C,
including the original stack shadows and PLD flags, only when its full cost
fits the next beam deadline. Otherwise the existing native edges resume it.
`SC_INFRASTRUCTURE_REFERENCE=1` disables that probe for diagnosis.
The independent original-routine oracle passes 1,600 calls and 132,767
immutable short-deadline/interrupt fallbacks on all expanded map sizes.

Projected GPU row markers now use contiguous spans separated by the fixed
sidebar. Forced blanking and HUD coverage are checked once per span, retaining
the same marker count and CPU materialization path. Six terrain zoom levels
(1, .5, .25, .7, 1.125 and 2) retain exact CPU/Vulkan pixels. The full filled
1920x1600 replay retains all 13,466,240 state bytes and six complete images
against the independent executable preceding the infrastructure changes;
Vulkan terrain validation reports no mismatches. Renderer, video and terrain
unit suites also pass. Restored-music timing comparisons remain separate from
these deterministic state/pixel tests.

Eight serial ABBA timing runs use the independent preceding native-UI EXE,
restored music on its dedicated worker, the same filled 1920x1600 state,
730x492 UI/.25 terrain zoom and a 2557x1480 window. The paired ordinary-50x
trials each execute 1,100 guest frames and 321,685 extra attempts; the paired
Tab trials each execute 2,700 guest frames and 980,588 extra attempts. Tab
uses a fixed two-guest-frame batch here to compare identical simulation work;
this does not measure the adaptive Tab controller's final pacing behavior.
Pooled warm 50x work averages 4.09 -> 3.75 ms, with frames over 16.7 ms
falling 31 -> 10 across 2,002 warm frames per executable. Fixed-batch Tab
work averages 6.32 -> 5.75 ms, with overruns falling 145 -> 63 across 2,602
warm display frames per executable. No development attempts are removed;
power-policy solve/reuse counts also match. These runs show a modest mean
improvement and fewer long frames, not sustained 60 FPS across complete city
cycles. Whole-field GPU phases are still absent from this fixture interval.


## Inline caller coverage and bounded road artwork

An 18,702-guest-frame filled 1920x1600 replay at 50x development reaches
all four asynchronous GPU field types: density fields 13/14 at 960x800 and
service fields 10/11 at 240x200. Completed GPU jobs match their CPU references.
This wider run exposed 831 main interpreter calls at 339 distinct instruction
addresses, despite the shorter replay recording no interpreter calls.

The program compiler now resumes reachability after the inline operands of
five verified callees: 0098a0 consumes two bytes; 03a2f5, 03a350, 03a3cf and
03a421 consume three. It verifies the callee prefixes in the clean US ROM.
Runtime operand reads, adjusted stack returns and instruction clocks retain
their existing C semantics. This compiles 33,091 sites across 61,488 width
states, with 141 caller continuations checked explicitly. The pinned program
oracle passes 4,235,648 atomic edges, 164,414 immutable fallbacks, 98,304
control cases, 512 patched NOP edges and 2,596,771 connected retired edges.

After the coverage change, the complete replay matches all 13,466,240 saved
state bytes and the final rendered image. Main and kernel interpreter calls
are zero in this replay. This is observed coverage, not proof that every
possible gameplay path or live ROM patch avoids the compatibility interpreter.
`SC_NATIVE_MISSING_PATH` optionally records actual main fallback entry points
to CSV; the default does not open a file or add per-edge tracing.

The road traffic-artwork handler at 03:a503 now calculates the complete result
in C when its original cost fits the beam deadline. It retains the field-anchor
hook, traffic thresholds, stack return shadows, flags, two ordered tile-byte
writes and per-byte render invalidation. Tight deadlines retain interruptible
native instruction edges. The infrastructure oracle passes 1,600 distance
probes, 1,024 road artwork calls covering every traffic byte on all expanded
sizes, and 220,527 immutable deadline/interrupt fallbacks.

The subsequent complete-city replay performs the same 5,422,242 extra zone
attempts across 18,702 guest frames. CPU, RAM, world, population, beam state
and the rendered image match the preceding independent inline-coverage EXE.
The only serialized difference is floating-point accumulation in the APU's
fractional-cycle remainder: approximately 2.54e-11 cycles. All other saved
bytes, including SPC state, match. All four GPU field types validate again;
main and kernel interpreter calls remain zero. Boot and map-size menu state
and pixels also match the independent preceding executable.

The longer replay also exposed an existing dense-city centroid bug: the
occupied-zone count wrapped at 65,535 despite coordinate sums being 32-bit.
The resulting center could leave world bounds and cause a long-run saved
state to fail validation. The full-width correction and save recovery are
described below. Sustained 60 FPS across full cycles and adaptive Tab pacing
remain unproven.


Eight serial ABBA timing runs compare the independent inline-coverage EXE
with the road operation, using the same filled city, 50x speed, Fit view and
19 restored PCM tracks on the music worker. Profiling and field validation
are disabled for timing. Ordinary trials retire 1,100 guest frames and
321,685 extra attempts; fixed two-frame Tab trials retire 2,700 guest frames
and 980,588 extra attempts. Neither development work nor power refresh cadence
is removed. Ordinary warm work averages 4.34 -> 4.44 ms (no mean improvement),
but p99 falls 17.56 -> 13.65 ms and overruns fall 29 -> 7 across 2,002 warm
frames per EXE. Fixed-batch Tab work averages 7.08 -> 5.13 ms, p99 falls
24.63 -> 15.04 ms and overruns fall 121 -> 10 across 2,602 warm frames per EXE.
These short comparisons support fewer hitches and lower Tab work cost; they
do not prove sustained 60 FPS across every full simulation phase or measure
the adaptive Tab controller's final pacing. No build is published by this audit.


The hidden-menu shortcut is visually verified with empty cartridge SRAM and
a cold boot from an actual saved-city SRM, revealing City 3 through the native
fade and Resume initialization. A synthetic main-menu fixture that changes
only the live save-present flag is unsuitable for visual qualification: it
does not contain the native saved-menu setup. Its short map-size tests remain
state/pixel comparisons only. The cold-boot populated-save test displays the
real saved city name and hidden City 3 entry with no old main-menu text.

## Full-width city centers and zoomed toolbox gutter

The dense-city occupied-zone count now carries into its high word at the
unique native iterator boundary, 03:9b5c. The carry requires a real occupied
zone from the live external ROM property table and a zero low word after
that zone's increment. Empty cells do not repeat a carry. A save between the
low-word increment and iterator boundary resumes the carry exactly once;
no new serialized state or changed CPU flags/clocks are required.

The independent 64-bit sum/count oracle passes 426,240 actual native owner
updates on 960x800 and 1920x1600 maps, including all six low-word overflows
and save/resume at each overflow. The prepared host-hook path is checked
on resume. The original-instruction density-family oracle additionally
passes 160 complete stages, 294 interrupted spans, 72 immutable yields and
145 instruction boundaries. Older saves with an impossible derived center
now retain their tiles and fields and invalidate only that derived center,
using the geometric midpoint until the next density pass calculates it.
Both an affected older full-cycle state and the corrected state reload.

A replay from the filled 1920x1600 city through 18,702 guest frames and
5,407,738 stationary-clock extra zone attempts now ends at center (942,798),
with population 48,863,540. Correct land-value distances change the resulting
development, so these counts differ legitimately from the pre-correction
replay. Vulkan density/service fields 10, 11, 13 and 14 all validate against
the CPU references; main and kernel interpreter calls are zero in this run.
The independent CPU replay disables the direct density-owner/scan kernels.
All 13,466,232 integer state bytes match; the APU fractional-cycle remainder
differs by approximately 5.0e-12 cycles. The 2557x1480 final images match
pixel for pixel. These instrumented comparisons establish correctness,
not production timing or sustainable maximum population.

That full-image comparison exposed a zoomed terrain strip left untouched
between the screen edge and the toolbox. The native toolbox begins at x=8,
not x=0: repaired overscan pixels now receive fresh terrain on every frame.
Opaque native UI or sprite content in the edge band still keeps ownership.
Poisoned-pixel regressions cover left/centered viewports, .25/.5/2 zoom,
immediate/deferred composition, fixed toolbox pixels and native edge ink;
PPU state remains immutable. This removes history-dependent CPU pixels and
GPU markers without rescaling the HUD.

## Full-phase readback control and terrain-quality admission

Four serial ABBA trials compare direct mapped GPU results against copying
them once into cached CPU memory. All runs use restored music on the worker,
a filled 1920x1600 city, .25 terrain zoom, Fit view and a fixed six-frame Tab
batch. Each completes 18,702 guest frames and 5,414,745 extra zone attempts,
with all 19 restored tracks loaded and no music-worker failures. Field
validation, instruction tracing and renderer timers are disabled for timing.
Across 6,202 warm display frames per mode, mean work is 12.45 ms direct and
13.50 ms cached; frames exceeding 16.7 ms are 1,095 and 1,533 respectively.
Individual p99 values overlap (29.79/31.90 ms direct, 29.61/33.26 ms cached).
The cached alternative does not show a consistent improvement, so direct
mapped results remain the default. These fixed-batch runs do not establish
adaptive Tab pacing or sustained 60 FPS.

Optional `SC_GPU_FIELDS_PROFILE=1` times allocation, submission and publication
at whole-job boundaries, adding no per-cell timer. A targeted run measures
allocation below .54 ms, submission below .97 ms, and publication below
.003 ms, while game frames 7414/7642 still spend roughly 200/189 ms in native
simulation. This rules out eager GPU resource allocation as the remedy for
those pauses. Page/PC tracing locates approximately 1.25 million native edges
on page a2, including 48,000 complete terrain-quality cell routines at a25c.
The tracing run is a locator and must not be used as production timing.

The terrain-quality fast path incorrectly read its native byte coordinates
as words. Uninitialized adjacent scratch bytes therefore rejected valid
cells and forced their complete arithmetic through individual native edges.
This coarse grid is at most 240x200 on the largest map; byte reads retain
its full supported range. The independent original-instruction oracle passes
96 cells covering all four expanded sizes, first/middle/last coordinates,
aligned/unaligned direct pages, varying flags and dirty scratch high bytes.
Every CPU field, RAM/world byte and original clock matches. Complete-cycle
and matched executable timing verification follow this admission correction.

The corrected complete-city replay still executes 18,702 guest frames and
5,407,738 extra zone attempts. All 22 completed Vulkan jobs validate across
fields 10/11/13/14. The final image and all 13,466,232 integer state bytes
match the independent CPU replay; the APU fractional-cycle difference is
approximately 6.37e-12 cycles. The center remains (942,798), and no main or
kernel interpreter calls are observed.

Four serial ABBA controls then compare the independent preceding EXE against
the admission correction. Each retires 6,504 guest frames and 2,020,368 extra
attempts with restored music on its worker and no worker failures. Background
CPU contention was present, and an independent density oracle overlapped part
of the later timing interval, so these measurements are not an isolated FPS
qualification. The specific native pauses at frames 7414/7642 are 522/512 ms
and 398/447 ms in the two preceding-EXE runs, versus 46/37 ms and 48/40 ms
with the correction. This supports removing the identified per-cell dispatch
pause; it does not establish an overall mean-frame improvement or sustained
60 FPS. Remaining native phases and adaptive Tab pacing still need work.
The post-change density oracle also passes all 160 complete stages, 294
interrupted spans, 72 immutable yields and 145 instruction boundaries.

The main executable now includes the power-traversal API declaration when
reporting its 64-bit stage counters. The missing declaration previously
truncated and sign-extended clock totals above 2^31 in the log. This diagnostic
correction does not change simulation clocks or saved state. No build is
published or substituted into the owner's running installation by this work.

## Bounded field publication and terrain-quality spans

Terrain quality now retires several complete cells and their byte-coordinate
iterators in a bounded C loop. Supported map radii use exact power-of-two
scaling; the centering constants are prepared once per span. Scratch/stack
shadows and CPU registers are published at the end of the span, while every
field cell retains its original value and clock cost. Short budgets keep the
complete-cell/instruction paths. Publication occurs before the caller's next
beam event, with no observable intermediate state crossing an interrupt.

Pollution smoothing and police/fire coverage also publish final CPU/RAM
shadows once per bounded span. Their source fields are immutable within the
span and every destination field write is retained. Unusual direct-page or
stack layouts use the preceding per-cell shadow writes; the deferred path
requires disjoint scratch, stack and low-RAM globals. The GPU dispatch,
nonblocking readback, field arithmetic and update cadence are unchanged.

The original-instruction oracle passes 576 terrain-quality spans, including
final-field boundaries and dirty adjacent coordinate scratch. It also passes
1,440 packed pollution spans with 576 immutable yields and 1,536 GPU/CPU
service spans with 768 immutable yields, across all four expanded map sizes,
both phases/services, aligned/unaligned direct pages and partial budgets.
Terrain-quality scratch and span checks are included in the default world test.

Four serial ABBA microbenchmarks each process 12,288,000 cells per family.
Terrain quality takes 418/392 ms using the preceding complete-cell path,
versus 42/40 ms with bounded publication. Packed service publication takes
58/56 ms with per-cell shadows and 26/27 ms with deferred shadows. Packed
pollution publication takes 67/74 ms and 39/36 ms respectively. Each family
has identical retired guest clocks and span counts in both modes. These are
isolated native publication measurements, not whole-game FPS claims. Optional
`SC_TERRAIN_QUALITY_SPAN_REFERENCE=1` and `SC_FIELD_SHADOW_REFERENCE=1`
retain the respective controls for diagnosis.

Before the publication refactor, a focused ordinary 50x run of a filled
1920x1600 city at .25 terrain zoom, Fit view and a 2557x1480 window retires
1,096 guest/display frames with restored music on its worker. Warm mean work
is 3.74 ms, p99 11.18 ms; 2 of 1,051 warm frames exceed 16.7 ms. Adaptive Tab
covers the same interval in 258 display frames, with mean 7.41 ms, p99 19.90 ms
and 6 of 242 warm frames over budget. A full-cycle adaptive Tab run retires
18,700 guest frames in 3,798 display frames, mean 9.03 ms and p99 18.49 ms,
with 76 warm overruns. It loads all restored tracks and has no music-worker
failures. This establishes useful headroom with occasional overruns; it does
not qualify sustained 60 FPS during all gameplay, scrolling or placement.

An initial fixed-six-frame publication ABBA was highly variable and did not
establish a whole-game improvement; it is not counted as a performance win.
Complete-city state/pixel parity and a fresh adaptive timing run follow the
final bounded-publication changes.

The final publication refactor completes 18,702 guest frames and 5,407,738
extra zone attempts. All 22 Vulkan jobs complete and validate across fields
10/11/13/14. The independent CPU control matches all 13,466,232 integer state
bytes and every pixel of the 2557x1480 output. The APU fractional-cycle delta
is about -6.37e-12 cycles. The center remains (942,798), with no main or
kernel interpreter calls observed. This instrumented fixed-batch replay
qualifies correctness rather than production frame timing.

The final production adaptive Tab sample completes 18,701 guest frames in
4,788 display frames with the restored music worker and zero worker failures.
Warm mean work is 10.06 ms, p99 19.01 ms; 164 of 4,743 warm frames exceed
16.7 ms. This is a separate adaptive sample, not a matched work/host-load
comparison against the preceding 3,798-display-frame run, and it does not
establish an overall FPS improvement. The remaining occasional native-frame
and input spikes still need investigation before qualifying sustained 60 FPS.
No release is published and the owner's existing executable/saves are unchanged.

## Fully funded ordinary-road upkeep

A focused first-stage trace of the filled 1920x1600 city found 122,442 calls
to the ordinary-road upkeep prefix during 1,104 guest frames. That prefix
still retired its loads, funding checks and branches as separate C instruction
edges. Fully funded ordinary roads now price and retire upkeep plus artwork
as one bounded call. Low funding, bridge endpoints and non-road tiles retain
the preceding instruction path. Short deadlines return before any counter,
field, tile or CPU write; the original clock cost remains unchanged.

The original-CPU oracle passes 1,600 distance calls, 2,048 road calls and
365,655 immutable deadline/interrupt/eligibility fallbacks across all four expanded map
sizes. The road cases include every traffic byte, both direct-page alignments,
counter wrap and full-width map coordinates. It compares CPU flags/registers,
RAM, the complete world and guest clocks.

An independent preceding executable and the new executable also replay the
same 50x development and scrolling inputs with Vulkan terrain validation,
.25 terrain zoom, Fit view and a 2557x1480 window. All 13,466,232 integer
state bytes and all six captured images match. The audio fractional-cycle
delta is approximately 7.33e-12 cycles, below the established 1e-9 tolerance
for differently grouped clock retirement. No terrain validation mismatch
occurs. Both paths solve power twice and reuse its policy cache 27 times.
These checks establish correctness, rather than a whole-game FPS claim.

Four serial ABBA trials per mode then compare the independent preceding EXE
with the refactor, with profiling/validation disabled and all 19 restored
tracks on the music worker. Ordinary 50x scrolling retires the same 1,100
guest frames and 321,685 extra zone attempts per trial. Across the two trials
per executable, mean warm frame work is 10.51 ms before and 9.61 ms after
(about 8.6% lower). Frames over 16.7 ms drop from 138 to 82 of 2,002 warm
frames. Individual p99 values fall from 31.24/33.02 ms to 25.45/24.80 ms.

Fixed two-guest-frame Tab trials each retire 2,700 guest frames and 980,588
extra attempts. Mean warm frame work is 15.08 ms before and 14.58 ms after
(about 3.3% lower); overruns drop from 599 to 539 of 2,602 warm frames.
Individual p99 values are 39.22/43.21 ms before and 33.46/35.87 ms after.
All eight trials report zero music-worker failures and identical power-cache
counts within each mode. These matched first-stage workloads support a
modest measured improvement, but remaining overruns and unmeasured full-cycle
phases prevent a sustained-60-FPS qualification. No build is published.

## Connected derived-field execution

The native city loop now includes the density, land-value, crime, pollution,
service and terrain-quality stages at 03:9a93..a299. Each span retains its
prepared world hook, including centroid count carries, expanded coordinate
iterators and GPU submission/invalidation. CPU/beam/audio clocks are retired
at the existing event boundaries. The preceding dispatcher's short-deadline
restriction on occupied-tile pollution classification is also retained.
`SC_FIELD_LANE_REFERENCE=1` restores the preceding admission range for diagnosis.

The final complete-cycle replay retires 18,702 guest frames and 5,407,738
extra development attempts on the filled 1920x1600 city. All 22 Vulkan jobs
complete and validate across fields 10/11/13/14. The independent CPU control
matches all 13,466,232 integer state bytes and every pixel of the 2557x1480
output. The audio fractional-cycle delta is approximately -1.49e-10 cycles,
within the established 1e-9 grouping tolerance. The center remains (942,798).
No main or kernel interpreter calls occur in this replay. These instrumented
results establish correctness; the final executable's matched timing follows
with profiling and validation disabled.

Four serial ABBA trials per mode compare the final EXE with the independent
fully-funded-road baseline over the density/land-value interval. Every fixed
six-frame Tab trial retires 1,098 guest frames; normal presentation retires
1,096. The interval contains no extra zone attempts. All runs use 50x
development, the restored music worker, .25 terrain zoom, Fit view and the
2557x1480 window. No profiling or validation instrumentation is enabled.

Fixed Tab mean warm work falls from 12.61 to 11.06 ms (about 12.3%); overruns
fall from 87 to 57 of 352 warm frames. Individual p99 values are 41.07/38.29 ms
before and 34.11/31.83 ms after. One control skips an in-flight Vulkan job
(17 ready), while the other control and both final runs complete 19 jobs;
this is default asynchronous execution with native fallback, rather than a
fixed GPU-readback microbenchmark. All restored-music workers report zero
failures. Normal-presentation mean warm work is 4.15 ms before and 3.91 ms
after, with p99 values 10.89/9.75 ms and 9.25/9.31 ms respectively. Both
executables have zero overruns in their 2,102 warm normal-presentation frames;
each trial has one cold-interval overrun. These interval results do not
qualify the whole simulation cycle, scrolling or adaptive Tab at sustained
60 FPS. A final full-cycle adaptive sample follows separately.

The final adaptive Tab sample retires 18,701 guest frames in 3,963 display
frames, with 5,414,647 extra attempts, all 22 Vulkan jobs ready and zero
music-worker failures. Warm mean work is 10.31 ms and p99 is 18.45 ms;
116 of 3,934 warm frames exceed 16.7 ms. This separate adaptive workload is
not a matched improvement claim against earlier full-cycle runs. The largest
36.90 ms frame contains 30.61 ms of input handling and 4.90 ms of emulation;
other leading overruns contain about 19-22 ms of native simulation work.
Those remaining input/native spikes still prevent sustained-60-FPS
qualification. No build is published or installed into the owner's session.

## Connected extra-development driver

Stationary-clock extra attempts now keep shared RNG/division continuations,
housing probes and connected zone mutations inside the development driver.
The original random draws, CPU flags, stack/scratch writes and world mutations
retain their order. Each helper keeps its native clock accounting; extra
attempts still leave beam, calendar and music clocks stationary, and the final
PLD/RTS remains with the existing scheduler. Unknown continuations return to
the caller. The beam-clock zoning path retains its preceding default, while
the accelerated driver admits its typed C body without per-edge host dispatch.
`SC_DEVELOPMENT_DRIVER_REFERENCE=1` restores the preceding connections, and an
explicit `SC_ZONING_REFERENCE=1` disables the connected zoning body for either
caller.

The original-ROM oracle passes 3,712 capacity/RNG cases, 512 complete house
candidate searches, 1,280 density/removal cases and 1,920 complete accelerated
attempt groups across Normal and all four expanded map sizes. It checks full
RAM/world contents, CPU state, total original clocks and development counts.
The groups include 49-extra-attempt and interrupted three-attempt sequences,
power and demand boundaries, empty/developed zones and low/high stack frames.

An independent preceding field-lane EXE and the new EXE also replay ordinary
50x development with scrolling and Vulkan terrain validation. Every one of
the 13,466,240 state bytes and all six captured images match exactly. Both
paths solve power twice and reuse its cache 27 times. This comparison uses
the production zoning default, rather than forcing the beam-clock connected
zoning tier on. Complete-cycle and matched timing checks follow separately.

The preceding full-cycle input spike is identified in its existing diagnostic
log: SDL_PollEvent takes 30.5 ms while returning SDL_EVENT_WINDOW_EXPOSED at
guest frame 12668. That sample does not attribute the pause to mouse mapping
or application gesture work. Native simulation spikes remain a separate
optimization target.

The complete-cycle driver replay retires 18,702 guest frames and 5,407,738
extra attempts. All 22 Vulkan jobs complete and validate across fields
10/11/13/14. All 13,466,232 integer state bytes and the complete 2557x1480
image match the independent CPU control; the audio fractional-cycle delta is
about -1.49e-10 cycles. The center remains (942,798), and no main/kernel
interpreter calls occur. Profiling/validation are enabled for this correctness
replay, so its instrumented frame times are not production timing claims.

The first connected-driver timing series did not establish an improvement:
ordinary warm work averaged 4.57 ms before and 5.20 ms after, while fixed
two-frame Tab averaged 6.58 ms before and 7.26 ms after. Both control pairs
drifted substantially during the serial ABBA runs. These results are retained
rather than treating correctness or reduced host dispatch as an FPS gain.

The mutation body now checks emulation, decimal and index-width modes at
span entry. Its instructions cannot enable those modes, so repeated checks
inside the body are unnecessary. Every edge retains stack bounds, instruction
width handling and the caller's deadline. The original-ROM family oracle
passes 672 scenarios, 12,512 bounded spans, 3,128 atomic comparisons and
6,256 immutable rejections, reaching 322 boundaries. The separate atomic
oracle covers all 354 boundaries with 5,088 comparisons and 576 immutable
mode rejections, including IRQ/NMI and all expanded maps.

A subsequent ABBA series with those invariant checks hoisted averages
4.26 ms before and 3.89 ms after in ordinary 50x scrolling (about 8.8%
lower). Both have zero overruns across 2,002 warm frames. Fixed two-frame
Tab averages 5.56 ms before and 5.40 ms after (about 2.9% lower); overruns
are seven before and eight after across 2,602 warm frames. Tab p99 values
are 15.61/14.34 ms before and 11.85/13.05 ms after. Ordinary control means
drift from 4.78 to 3.75 ms, so this remains modest interval evidence, not a
sustained-60-FPS qualification. Every trial uses restored music and reports
zero worker failures.

The accelerated driver also selects its owning helper by continuation
address instead of probing unrelated capacity, transport, housing and
mutation helpers on each segment. Original fallback priority is preserved
where two operations share an entry. The complete-attempt original-ROM
oracle again passes all 1,920 groups after this change. Final saved-state,
pixel and timing comparisons follow separately.

The final keyed-dispatch EXE (SHA256
`2E3C70E8DE845A18BE3FAD745C97D20C59DCD97EBA16892611DB2B9FF90BCF16`)
passes the independent preceding-EXE scrolling replay. All 13,466,232 integer
state bytes match, the APU fractional-cycle delta is zero, and all six
captured images match. Vulkan terrain validation reports no mismatches;
power solves/reuses remain 2/27.

The final eight serial ABBA trials keep profiling and validation disabled,
restore all 19 music tracks and use the production zoning default. Ordinary
scrolling trials each retire 1,100 guest frames and 321,685 extra attempts;
fixed two-frame Tab trials retire 2,700 guest frames and 980,588 extra
attempts. All workers report zero failures. Ordinary warm work averages
3.37 ms before and 3.48 ms after, with one overrun each across 2,002 warm
frames. Thus this series does not establish an ordinary-scrolling gain.
The control's largest 428.65 ms frame contains 420.35 ms of input handling;
its existing diagnostic identifies SDL_PollEvent returning event 0x205.
The measurement is retained, rather than removed from the result.

Fixed Tab warm work averages 6.95 ms before and 5.05 ms after, with overruns
falling from 20 to four across 2,602 warm frames. Individual p99 values
are 12.67/17.15 ms before and 11.82/11.38 ms after. Control means drift
from 5.81 to 8.08 ms, while final means are 5.07/5.03 ms, so the nominal
27.3% average reduction is specific to this measured series. A final run
also has a 57.48 ms input stall. These interval results do not establish
sustained 60 FPS over the complete city cycle or adaptive Tab.

The final complete-cycle replay retires 18,702 guest frames and 5,407,738
extra attempts. It again matches all 13,466,232 integer state bytes and the
complete 2557x1480 image against the independent CPU control. Its APU
fractional-cycle delta is about -1.49e-10 cycles; the center is (942,798).
No main/kernel interpreter calls occur. All 18 ready Vulkan field jobs
validate across fields 10/11/13/14. This asynchronous run submits 20 jobs
and skips two in-flight opportunities; native fallback still produces the
same complete state and image. Profiling and validation are enabled for
this proof, so its frame times are not production performance results.

The final production adaptive-Tab sample, with restored music enabled and
profiling/validation disabled, retires 18,703 guest frames in 3,646 display
frames and executes 5,415,284 extra attempts. Its 3,607 warm frames average
60.03 FPS including measured pacing. Mean active frame work is 10.05 ms,
p99 is 18.20 ms and the maximum is 32.83 ms; 76 warm frames exceed 16.7 ms.
All 22 Vulkan jobs become ready and the music worker reports zero failures.
The slowest frame includes an input-handling pause; other leading frames
contain about 18-22 ms of native simulation. This sample establishes an
average near 60 FPS, with occasional hitches still remaining. Its adaptive
guest grouping differs from preceding samples, so it is not a matched
whole-cycle improvement claim. A separate local test EXE is packaged; no
GitHub release is published and the owner's installed executable is retained.

### Native kernel specialization and complete-cycle comparison

`sc_native_specialize.h` allows optimized compilers to specialize density,
smoothing, police/fire service and zoning execution bodies at their public
bounded-span, atomic and accelerated entry points. Constant mode branches
can be eliminated while instruction clocks, stack checks, immutable rejection
and interrupt deadlines remain intact. `SC_NATIVE_KERNEL_GENERIC` retains
the shared-body reference implementation. This is a compile-time diagnostic,
not a new game option.

The specialized original-ROM oracles pass 160 complete density stages,
294 interrupted density spans and 72 immutable density yields; 24,480
service comparisons, including 6,120 atomic comparisons and 12,240 immutable
yields; 26,760 smoothing comparisons, including 6,690 atomic comparisons
and 13,380 immutable yields; and 672 zoning scenarios, 12,512 bounded spans,
3,128 atomic comparisons and 6,256 immutable yields. Service and smoothing
cover all 124 and 130 respective boundaries; zoning reaches 322. Expanded
maps and IRQ/NMI boundaries are included. The separate accelerated-driver
oracle again passes all 1,920 complete original-ROM groups.

The specialized complete-cycle EXE retires 18,702 guest frames and
5,407,738 extra development attempts. All 13,466,232 integer state bytes
and the complete 2557x1480 image match the independent CPU control. The
APU fractional-cycle difference is about -1.49e-10 cycles. No main or
kernel interpreter calls occur in this replay. Twenty ready Vulkan field
jobs validate; 21 jobs are submitted and one in-flight opportunity is
skipped. These diagnostic runs enable profiling and validation and are
correctness evidence, not production performance measurements.

Four serial production runs compare the preceding keyed-development EXE
with the specialized EXE in control/new/new/control order. They use a fixed
six-guest-frame Tab batch, 50x development, the filled 1920x1600 city, terrain
zoom 0.25, a 2557x1480 window and all 19 restored tracks on the music worker.
Profiling and GPU validation are disabled. Every run retires exactly 18,702
guest frames and 5,414,745 extra attempts. The music-enabled workload count
differs from the music-disabled diagnostic replay, but matches across all
four production trials. Every music worker reports zero failures.

| Warm active-frame work | Preceding EXE | Specialized EXE |
| --- | --- | --- |
| Mean across the two runs | 12.83 ms | 10.60 ms |
| Individual run means | 12.22 / 13.44 ms | 9.45 / 11.75 ms |
| Individual p99 | 32.44 / 33.56 ms | 25.69 / 30.06 ms |
| Frames above 16.7 ms, out of 6,202 | 1,183 | 564 |

The nominal mean reduction is 17.4%. City frames alone average 13.62 ms
before and 11.20 ms after, a 17.8% reduction. Host timing varies substantially
even within each pair; the result describes this measured series. Fixed
six-frame Tab still has overruns and does not establish sustained 60 FPS.
Raw frame CSVs, actual workload counts and all completed runs are retained
under `.local/performance-overhaul/kernel-specialization-full-abba-*`.

The specialized production adaptive-Tab replay retires 18,704 guest frames
in 3,673 display frames and performs 5,415,823 extra attempts. Its 3,642
warm frames average 59.91 FPS including measured pacing, with 9.34 ms mean
active work, 19.29 ms p99 and a 29.97 ms maximum. Ninety-four warm frames
exceed 16.7 ms. Sixteen of 18 submitted field jobs become ready; four
in-flight opportunities use native fallback. The music worker reports zero
failures. Adaptive grouping differs from the preceding sample, so this is
a current smoothness qualification, not a matched speedup claim. Average
presentation remains near 60 FPS; occasional native simulation hitches
still prevent a sustained-60-FPS claim.

### Current input and feature acceptance checks

Actual SDL wheel and pinch events are injected into the application's own
event queue through the inactive-by-default `SC_ZOOM_EVENTS` regression
hook. Five checks cover Ctrl-wheel zoom, wheel without Ctrl, wheel return,
pinch zoom out and pinch return. Each event-driven result matches every
pixel and all city state bytes of an independently configured zoom, with
Vulkan terrain validation enabled. SDL 3.4 pinch scale is the change since
the preceding update. This hook does not move the system pointer or send
input to other applications.

The current clipboard checks cover all five map sizes, whole intersected
ordinary-building footprints, roads/rail/wire/parks, native connectivity,
special-building exclusions, native price glyphs, high totals and atomic
funds rejection. Population checks cover trillion-scale calculation,
reports, history and saves, plus incremental/full census parity on every
size. Pointer mapping, fixed-scale HUD/overview rendering and viewport-arrow
checks pass. Restored music tests cover all 19 selections and a 250 ms game
thread stall, including pause, mute and worker shutdown.

Live Vulkan pan checks confirm screen-following drag direction, the 3x
movement gain, capture through synthetic window exit, clean release and
clipboard preservation followed by successful paste. Relative mode enable
and disable requests are accepted. The hidden, unfocused test window does
not report a physical mouse grab, so this replay alone does not prove
physical confinement in a focused user window.

### Vulkan crime calculation with ordered native publication

The filled-city stage trace identifies recurring slow frames in the crime
routine. A fifth asynchronous field job now calculates crime on Vulkan from
an immutable snapshot of land value, population density and police coverage,
plus the current signed bias. Each result contains its crime value, native
scratch subtraction, exact aligned branch clocks, direct-page surcharge and
the complete input identity. It uses integer arithmetic throughout.

The native C publisher checks the sample's land/density/coverage identity
against the live cell before using it. Bias, dimensions, world identity and
reset/load epoch are checked by the backend. Changed source cells retain the
normal C path. Cell statistics and aggregate overflow remain ordered on the
CPU; each cell and loop advance must fit the original interrupt deadline.
Jobs are polled without waiting, and an unavailable/in-flight result retains
native execution. `SC_GPU_CRIME_REFERENCE=1` disables the new job for a
diagnostic comparison. Terrain and the existing four field jobs still share
the same Vulkan presentation device.

The publication oracle passes 1,152 comparisons against the original ROM,
including 496 GPU-backed spans and 576 short spans. It covers all four
expanded map sizes, row/final-cell seams, aligned and unaligned direct pages,
zero and developed cells, signed-bias/coverage extremes, aggregate wraps,
short budgets and source changes after dispatch. CPU registers, flags, native
scratch, stack shadows, complete world/RAM and clocks match exactly.

The real Vulkan field test also passes all 48 preceding byte/word jobs and
32 new crime fields, comparing every one of 8,160,000 crime samples across
all expanded shapes and eight signed biases. It checks value, scratch,
aligned/unaligned clocks and input identity, and rejects wrong bias, changed
dimensions and reset/load epochs for both completed and in-flight jobs.

The complete-city correctness replay publishes 1,915,267 GPU crime cells.
All 25 submitted field jobs complete and validate, including three 960x800
crime jobs. It retires 18,702 guest frames and 5,407,738 extra attempts. All
13,466,232 integer state bytes and the 2557x1480 image match the independent
CPU control. The APU fractional-cycle delta is approximately -1.67e-10
cycles. No main/kernel interpreter calls occur. Profiling and validation are
enabled for this proof, so its frame times are not performance claims.

Four serial production control/new/new/control runs use the independent
specialized-kernel EXE from before GPU crime. Each run retires exactly 18,702
guest frames and 5,414,745 extra attempts at 50x with fixed six-frame Tab,
terrain zoom 0.25, the filled 1920x1600 city, a 2557x1480 window and all 19
restored music tracks. Profiling and validation are disabled, and every music
worker reports zero failures.

| Warm active-frame work | Before GPU crime | With GPU crime |
| --- | --- | --- |
| Mean across the two runs | 11.60 ms | 11.02 ms |
| Individual run means | 10.84 / 12.35 ms | 10.95 / 11.09 ms |
| Individual p99 | 26.08 / 30.73 ms | 26.19 / 27.29 ms |
| Frames above 16.7 ms, out of 6,202 | 811 | 649 |

The nominal reduction is 5.0%; city frames alone average 12.35 ms before
and 11.69 ms after. The first control is slightly faster than either new
run, and control drift is substantial. This remains modest series-specific
evidence, not proof of a broad speedup or sustained 60 FPS. Raw complete
runs and actual workload counts are retained under
`.local/performance-overhaul/gpu-crime-full-abba-*`.

The production adaptive-Tab sample with GPU crime retires 18,702 guest
frames in 3,540 display frames and performs 5,414,745 extra attempts. Its
3,509 warm frames average 60.04 FPS including measured pacing. Mean active
work is 9.61 ms, p99 is 17.53 ms and the maximum is 24.72 ms; 63 warm frames
exceed 16.7 ms. Twenty-three of 24 submitted field jobs become ready, with
one in-flight opportunity using native fallback. The music worker reports
zero failures. Adaptive grouping differs from preceding samples: these
are current smoothness measurements, not a matched whole-cycle gain claim.
Average presentation is near 60 FPS, with occasional native simulation
hitches still remaining. The preceding delivered single EXE is retained;
this change is in the local worktree and no GitHub build is published.

## Asynchronous Vulkan land tile summaries

The sixth field job reads an immutable full-resolution tile snapshot and a
1,024-entry tile descriptor table. Each compute invocation reduces one 2x2
tile group into density, wrapped pollution, occupancy, the last qualifying
tile and the original aligned/unaligned branch clocks. All arithmetic is
integer. Land-distance contributions, final land-value arithmetic and
aggregate publication remain ordered native C operations.

The publisher checks all four raw tiles, including flags, against the live
map before using a result. Dimensions, world identity and reset/load epoch
must match. In-flight or changed results use the ordinary C path without
waiting. `SC_GPU_LAND_REFERENCE=1` disables this job for diagnostics.

The initial native publication oracle passes 2,160 original-ROM comparisons,
including 432 GPU-backed spans and 1,080 short spans. It covers all four
expanded sizes, row/final seams, direct-page alignment, signed/wrapped
pollution, density clipping, raw flags and changes to each source tile.
Registers, scratch, stack, complete RAM/world state and clocks match.

The extracted CPU tile helpers also pass 24,672 fused land stages and 8,224
immutable deadline rejections, followed by 576 complete calls, 3,100
interrupted spans and 785 immutable yields across 172 instruction
boundaries. Separate developed/vacant checks pass 768 developed cells and
1,536 vacant groups, including flags, overflow, power metadata and seams.

The actual Vulkan test compares 16 land fields containing 4,080,000 summaries
across all expanded shapes and tile categories. It also checks completed and
in-flight reset invalidation and dimension changes; all 48 preceding
byte/word fields and 32 crime fields pass in the same run.

The full-city replay publishes 1,252,245 GPU land summaries and validates
25 completed jobs, including two 960x800 land jobs. One older in-flight
opportunity uses native fallback. It retires 18,702 guest frames and
5,407,738 extra attempts. All 13,466,232 integer state bytes and every pixel
of the 2557x1480 image match the independent CPU control; fractional APU
clock difference is approximately -1.67e-10 cycles. No main/kernel
interpreter calls occur. Profiling and validation are enabled for this
correctness proof, so these runs are not performance measurements.
Artifacts are retained under
`.local/performance-overhaul/complete-cycle-gpu-land-native-hook`.

The initial production control/new/new/control trial does not establish a
speedup: the two controls average 8.85 ms of warm active work and the new
runs average 10.62 ms, with 319 versus 828 frames above 16.7 ms out of 6,202.
Control means drift from 7.79 to 9.91 ms. Every run retires the same 18,702
guest frames and 5,414,745 extra attempts, with all 19 music tracks and zero
worker failures. Results are retained in `gpu-land-full-timing.json`; this
version is not packaged or published as a performance improvement.

The next revision retires the original native finish in the same call when
both halves fit the beam budget, including calls below the old 1,515-cycle
worst-case gate. Otherwise it resumes at the exact mid-cell PC. Its extended
oracle passes 5,760 comparisons, 1,719 GPU-backed spans and 3,600 short spans.
It covers both direct cell entry and the caller, with 700- and 1,000-cycle
budgets in addition to the original budgets. Caller comparisons normalize
the existing zero-clock large-map coordinate hook on both paths before
comparing the prepared boundary, as the preceding spatial oracle does.

The fused-caller full-city replay publishes 1,263,299 GPU land summaries.
Twenty-four completed field jobs validate, including both land jobs; two
in-flight opportunities use native fallback. All 13,466,232 integer bytes
and the 2557x1480 image again match the independent CPU control at 18,702
guest frames and 5,407,738 extra attempts, with no main/kernel interpreter
calls. Fractional APU delta remains approximately -1.67e-10 cycles. This is
the final correctness replay for the fused-caller revision, retained under
`.local/performance-overhaul/complete-cycle-gpu-land-fused-call`.

The fused production ABBA trial also does not establish a gain. Control
means are 8.47/9.18 ms; new means are 10.30/11.10 ms. Controls have 252 warm
frames above 16.7 ms, versus 584 new frames, out of 6,202 each. All workload
counts and music-worker checks match the preceding trial.

A subsequent same-executable disabled/enabled/enabled/disabled comparison
isolates the job switch. Disabled means are 9.52/8.85 ms and enabled means
are 10.89/9.12 ms; pooled means are 9.18/10.00 ms. Slow-frame counts are
214/440 out of 6,202 each. There is still substantial host variation, but
neither comparison supports enabling this job as a performance improvement.
Raw trials are in `gpu-land-fused-call-timing.json` and
`gpu-land-toggle-timing.json`.

The application therefore leaves land submission/publication callbacks
disconnected by default. `SC_GPU_LAND=1` explicitly enables this experimental
job; `SC_GPU_LAND_REFERENCE=1` suppresses it for comparison. Existing terrain,
pollution, service and crime GPU paths remain enabled. The direct backend
tests exercise land independently of the application opt-in. No GitHub
build or portable package includes this experiment yet.

The application's default configuration is separately replayed after the
opt-in change. It submits no land jobs and publishes zero land summaries;
23 completed established field jobs validate, with one in-flight native
fallback. All integer state bytes and rendered pixels match the independent
CPU control at the same 18,702 guest frames and 5,407,738 extra attempts.
No main/kernel interpreter calls occur. Results are retained under
`.local/performance-overhaul/complete-cycle-gpu-land-default`.

The final production adaptive-Tab default sample uses the filled 1920x1600
city, 50x development, terrain zoom 0.25, a 2557x1480 window and all 19 music
tracks. It retires 18,700 guest frames in 3,245 display frames, with 5,413,961
extra attempts and zero music-worker failures. Its 3,219 warm frames average
60.12 FPS including measured pacing. Active work averages 8.29 ms, p99 is
15.81 ms and the maximum is 20.07 ms; 17 frames exceed 16.7 ms. GPU land
publication stays at zero. Adaptive grouping and host performance differ
from prior runs, so this is a current smoothness sample, not a matched
whole-cycle speedup claim. Occasional native simulation hitches remain.
Raw results are in `wide-density-focused-adaptive-land-default` and
`adaptive-land-default-paced.json`. The previously delivered single EXE is
unchanged; no package or release is created during this experiment.

## Native execution with deferred compatibility binding

The optional `SC_DEVELOPMENT_PROFILE=1` records host timings for accelerated
driver operations by entry PC. It changes no guest clocks or state. The
complete-city profile replay matches all 13,466,232 integer bytes and the
2557x1480 image at 18,702 guest frames and 5,407,738 extra attempts. Its
capacity/decision/mutation timings show that those direct helpers alone do
not account for the complete simulation cost. These instrumented times
include timer overhead and host scheduling; they are attribution evidence,
not production performance measurements. Raw records are retained under
`complete-cycle-development-stage-profile`.

The following refactor separates direct C execution from compatibility
memory binding in the accelerated development lane, connected city lane
and bank-03 native dispatcher. Each native admission clears any preceding
binding. Coordinate/submission hooks still run at their original boundaries.
Direct helpers continue to read and publish their native RAM/world data;
bus-backed math arguments remain original ROM/IO accesses. A compiled
opcode or fallback binds its current opcode immediately before execution.
Bus-backed sprite/tile work outside bank 03 retains its existing preparation.
`SC_NATIVE_BIND_REFERENCE=1` retains eager preparation for comparisons.

The full-city correctness replay counts 79,856,332 deferred admissions and
47,439,483 required compatibility bindings, avoiding 32,416,849 preparation
calls. It retires the same 18,702 guest frames and 5,407,738 extra attempts;
all integer state bytes and every image pixel match the independent CPU
control. Fractional APU delta is approximately -1.67e-10 cycles. All 25 GPU
jobs validate, and no main/kernel interpreter calls occur. GPU land remains
disabled by default. Validation/profiling are enabled, so this proof makes
no timing claim. Results are retained under `complete-cycle-native-lazy-binding`.


The production deferred-binding ABBA comparison does not establish a speedup.
The independent pre-refactor control averages 10.53 ms of active work, versus
10.76 ms for deferred binding (about 2.2% higher); 514 versus 545 of 6,202
warm frames exceed 16.7 ms. All four runs retire 18,702 guest frames and
5,414,745 extra attempts with all 19 tracks and zero music-worker failures.
Host variation is material, so fewer preparation calls are a structural
result, not a demonstrated frame-time improvement. Raw matched results are
in `native-lazy-binding-timing.json`.


## Connected road, bridge and rail infrastructure

`sc_infrastructure.c` connects road upkeep, traffic artwork, bridge
opening/closing and footprint publication, rail upkeep/decay and train
initialization, and the seaport tally. Straight-line and internal call/branch
edges use C labels. Original instruction clocks, flags and stack shadows
remain observable at every beam deadline; an atomic instruction entry handles
already-due boundaries. World reads/writes bind only at the actual spatial
access, retaining existing full-coordinate mapping and dirty revisions.
Existing whole-road and bridge-distance fusions stay ahead of this family.
External RNG and coordinate helpers retain their established boundaries.
`SC_INFRASTRUCTURE_REFERENCE=1` disables the new family for diagnostics.

The independent original-ROM family oracle completes 1,536 calls across all
four expanded sizes, comparing 8,792 bounded spans and 1,256 instruction
cases. It explicitly covers complete bridge opening/closing in both
orientations, road and rail decay, and short deadlines. CPU, all RAM,
the complete world and original clocks match. Ten invalid-layout/interrupt
cases reject without mutation. The existing independent road/distance
oracle also passes: 1,600 distance calls, 2,048 road artwork calls and
365,655 immutable deadline/interrupt/eligibility fallbacks.

The filled 1920x1600 complete-city replay retires the same 18,702 guest
frames and 5,407,738 extra attempts as the independent CPU control. All
13,466,232 integer bytes and every pixel of the 2557x1480 image match;
fractional APU delta is approximately -1.48e-10 cycles. Twenty completed GPU
jobs validate, with three in-flight native fallbacks. Main and kernel
interpreter counts are zero. Compiled opcode edges decrease from 73,613,315
to 64,205,524 (9,407,791 fewer), and connected city-lane spans decrease from
63,720,286 to 52,519,961. These profiled correctness results do not establish
production frame-time gains. Records are retained under
`complete-cycle-native-infrastructure` and `native-infrastructure-parity.json`.


The production infrastructure ABBA comparison uses an independent EXE saved
immediately before the new family. All four trials retire 18,702 guest frames
and 5,414,745 extra attempts with a fixed six-frame Tab batch and all 19
music tracks; music-worker failures remain zero. The two control means are
8.00 and 8.96 ms, versus 7.52 and 7.53 ms for the new build. Pooled active
work decreases from 8.48 to 7.52 ms (about 11.3%); frames above 16.7 ms
decrease from 225 to 106 of 6,202 warm frames per build. Both new trials
are faster than either control, but control drift is material: this is a
measured local improvement, not a guarantee on other hosts. New p99 values
are 17.43 and 20.96 ms, and maxima are 38.26 and 36.51 ms; occasional hitches
remain under this fixed-batch workload. Raw results are retained in
`native-infrastructure-timing.json`.


The current production adaptive-Tab sample uses the filled 1920x1600 city,
50x development, terrain zoom 0.25, a 2557x1480 window, Vulkan terrain and
all 19 music tracks. It retires 18,702 guest frames and 5,414,745 extra
attempts in 3,208 display frames. Its 3,174 warm frames average 60.14 FPS
including measured pacing, with active work averaging 8.11 ms, p99 14.64 ms
and maximum 19.55 ms. Four warm frames exceed 16.7 ms. The music worker
reports zero failures; GPU fields submit 24 jobs, complete 23 and retain
one in-flight native fallback. Experimental GPU land stays disabled.
This is a current local smoothness sample, not a matched adaptive speedup
claim or a promise that every frame meets the deadline. Raw records are
in `wide-density-focused-adaptive-native-infrastructure` and
`wide-adaptive-native-infrastructure.json`.
