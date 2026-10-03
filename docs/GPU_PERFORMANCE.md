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
`SC_MUL16_REFERENCE=1` retains the preceding complete-iteration multiply path,
isolating native resumptions after partial beam deadlines.
`SC_DIV16_SETUP_REFERENCE=1` retains interpreted 16-bit division operand setup.
`SC_RNG_REFERENCE=1` retains the interpreted generator/bounded-random spans.
`SC_LAND_FINISH_REFERENCE=1` retains the interpreted resumed land-value path.
`SC_DIV16_REFERENCE=1` retains the preceding whole-iteration divide path.
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
