# PC enhancement work

UrbanRecomp Enhanced 1.0 is the first stable release, consolidating Betas 1–23.
The controls and save formats below apply to 1.0. Historical beta measurements
retain their original labels and test conditions.

This fork of [blackerking/UrbanRecomp](https://github.com/blackerking/UrbanRecomp)
contains development-speed, population, mouse and playable large-map
enhancements. Windows packages are available from
[the enhanced fork's releases](https://github.com/kandowontu2/UrbanRecomp/releases).

## Implemented

The map-selection screen's GENERATION arrows select Native (default),
Procedural, Islands, Lakes, Rivers, Fractal, Continent, Delta or Atolls. Move Up from NEXT to the
selector, then use Left/Right to change the type; Down returns to NEXT.
Click either arrow to cycle backward/forward, or click the type to cycle
forward. Enter or the game's confirm button activates the selected arrow.
The preview regenerates and the choice is remembered for new cities.
Type changes retain the visible preview until its complete replacement is ready,
then publish terrain and colors together. Preview zoom/pan stays in place. Only
new map numbers replay the initial on-screen generation reveal.

The map-screen GENERATION selector offers Native (default), Procedural (the earlier
replacement generator), Islands, Lakes, Rivers, Fractal, Continent, Delta or Atolls. The remembered
choice applies to new terrain, including regeneration on the preview screen.
Loaded cities retain their terrain. Fractal uses warped noise, a sea level
balanced against each map, and shoreline fitting for usable land and irregular
bays. Continent favors broad mainland; Delta branches connected rivers into
a coast; Atolls builds broad islands around smaller lagoons, with curved
openings to the sea and room for complete districts. Islands starts with water
and builds large connected landmasses instead of small scattered discs.
Feature sizes stay in world cells as the map grows, adding more islands.
All styles support every size and the water-free map-number 31337 exception.

120x100 maps use the original cartridge generator, including its river walks,
lake clusters, coast brushes, forest scatter and shoreline/tree fitting. The
original three-digit seeding path is preserved; the two added digits extend
its random state. Normal maps no longer pass through replacement geography.

Expanded maps extend those same native routines across the full world.
River walks continue through real map bounds, with more starts distributed
across larger maps. Lake and forest counts scale with area; river widths,
lake brushes and forest walks retain their original tile sizes. Coordinates
use the full map dimensions without a repeated grid of completed 120x100
maps. Small island bays use the original water brushes and shoreline fitter.
These extensions apply from 240x200 through 3840x3200. Existing saves keep
their terrain; the fix changes newly generated maps.

Map selection now offers five editable digits, 00000 through 99999. All five
up/down arrow pairs support mouse and keyboard/controller navigation; NEXT
advances the whole number and wraps after 99999. The cartridge's original
seeding also depends on the previous map selection.

Expanded maps add a visual vehicle fleet by 120x100 districts: Big supports
up to 4 extra vehicles of each kind, Huge 16, Giant 64, Colossal 256 and Mega
1,024. Each district needs rails for a train, a powered airport for aircraft
and helicopters, or a powered seaport with a clear 32-pixel water footprint
for a ship. Trains and ships follow their rail/water routes; aircraft fly
around their airport. Movement pauses with the simulation. These additional
vehicles rebuild from saved infrastructure and use independent world positions
and original ROM artwork. Train and ship headings have their own cartridge
CHR source, independent of the original game's shared vehicle DMA. The fleet
adds visual activity; the existing transport-demand and disaster rules remain.
Only visible sprites reach the compositor, using shared CPU/GPU spatial buckets.

The camera allows 64 canvas pixels of overscan on all four edges. Far-right
tiles on Mega maps can move clear of the viewport at the same distance as the
left edge, including centered layouts and city zoom.

The map-select preview keeps its original 120x100 box while drawing at the
display's pixel resolution. Coverage-aware sampling preserves thin rivers
between the old single-tile samples. It reveals water first, then forest patches over roughly
1.5 seconds after the waiting panel. Its palette comes from the live native
BG2 layer, and the preview does not change city zoom or UI scale.
NEXT regenerates on release; map-number clicks regenerate once the pointer
leaves the shared five-digit arrow area. All five digits appear from entry,
using the original font, counter cells and No. label. Ctrl+wheel zooms around
the mouse pointer in the city view or preview, keeping the HUD at its existing size.
The display-resolution preview remains in place during pending NEXT/number
edits and selector entry/exit frames. The visible panel is identified before
scanout so a changed screen state cannot expose the coarse native preview.
Optional display smoothing is overridden with nearest sampling on this panel.
Hold middle mouse to drag the preview; stationary input does not move it.
Click inside the preview to open
an expanded view across the window; click again or press Esc to return to
map selection. The expanded view shows the complete generated terrain and
keeps the mouse hand at its ordinary UI size. Zoom and pan affect only this
preview, and its camera stays within the map.

Tool-window backgrounds compose the native UI and shadows over the projected
city, without importing unzoomed building fragments. GO TO MENU resets both
setup-page flags, and RESUME SAVED CITY shares the other choices' left edge.

Startup, map-size, development-speed and saved-city lists show a separate
native hand whose fingertip aligns with the mouse. The original selection
arrow stays beside the hovered option. Other menu hands follow the absolute
pointer while the game retains native selections and synthesized pad interaction.
Keyboard/gamepad input restores the native jumping cursor; a stationary mouse
cannot pull it back. Cursor presentation is restored before guest execution,
so it does not alter simulation or saved OAM. Scenario pins and selection
frames stay intact, with a separate native hand across the wide canvas.

Validation: native generation checks cover 40 seeds across all six
sizes, long connected waterways reaching multiple edges, feature coverage
in every quadrant, native tile bounds and preview phases. Matching interior
terrain classes at Big through Mega verify fixed feature scale. Stock fingerprints
remain exact. Renderer tests cover palette, preview borders, unchanged zoom,
wide scenario pointers and PPU immutability. Local native-ROM replays verify
water/forest animation, the largest map preview, menu/name/scenario pointers,
annual and toolbar budget mouse controls, and keyboard takeover. Tests use
private scratch saves.

Additional validation covers all 100,000 number keys, deterministic numbered
samples, all five arrow pairs in the live menu, 99999-to-00000 NEXT wrapping,
island components and native tile vocabulary. Sixteen captured stock terrain
fingerprints and final random states remain unchanged. A live 120x100 menu
replay also matches all 12,000 terrain words against the ROM interpreter.
Fleet tests cover every
expanded size, infrastructure removal, paused positions and viewport culling.
Native-ROM Mega replays verify up to 1,024 vehicles of each kind and thousands
of visible sprite pieces across zoom levels, with CPU/GPU pixel parity. Dedicated
CHR checks cover train/ship row strides and all flips with blank native VRAM.
Largest-map panning checks verify both horizontal limits without changing zoom.

Large drag selections use a clipped grid preview and resumable private placement
batches. Rendering, window events and music continue while the transaction runs;
the city simulation waits at a safe input boundary until the complete result is
ready. Original placement rules, costs and the money cheat are preserved. Spare
frame-pacing time also advances the job. Massive selections still take time:
a local 3840x3200 replay placed 299,547 free residential zones in about 26 seconds,
with presentation around 60 FPS during placement. A matching held-preview replay
reduced average draw/presentation work from roughly 156 ms to 4.5 ms. Timings are
machine-dependent and exclude any promise of instant map-wide construction.

All ordinary city-start and saved-city entry paths keep their temporary setup
image black until the native city fade is ready, preventing the transient blurry
terrain before the completed sharp view. Fast-forward checks the fade on every
guest frame, including frames whose expanded pixels are skipped.

Windows builds launch without a console window. Start `UrbanRecomp.exe`
directly; the optional batch launcher exits immediately after starting it.
Developers can build with `-DSC_CONSOLE=ON` to retain the console. Command-line
arguments, exit codes and diagnostics redirected to files remain available in
the default desktop build.

Enhanced releases generate music on a dedicated sound CPU/DSP
thread. Playback continues during slow simulation frames. Settings pause and
save/load synchronize with that thread; sound commands retain their order.
The portable release includes the restored soundtrack on this worker.

F12 opens the host settings overlay; F10 remains an alias. Development speed
comes from the current city's saved setup choice.
After selecting a map size, the native-font **DEVELOPMENT SPEED** page offers
**1x, 3x, 5x, 10x, 20x and 50x**. That default is saved per city, including save
states, and survives Journey expansions. Older saved cities default to 1x. RCI development runs in batches
spread across the city at every setting, including Normal. One pass visits
the stable zone index before starting another, instead of giving one zone
all its accelerated attempts before moving on. Each frame publishes a batch
of completed decisions. Demand, power and access requirements still apply;
faster development also means faster decline when those requirements are
unmet. This multiplies attempted decisions, not guaranteed population growth.
Work is bounded per frame; CPU limits can reduce the achieved rate at X50.

Largest-map fast-forward keeps arithmetic and electrical traversal inside the
connected native simulation loop. Electrical zero-clock work can run in bounded
batches at scanline/IRQ boundaries instead of paying per-instruction dispatcher
costs. Derived-field timing and the full 6/24/96-frame Tab cohorts are retained;
music stays at real-time tempo. See [performance measurements](GPU_PERFORMANCE.md).


**FIT TO SCREEN** in F12 maximizes the window and enables the adaptive renderer's
Fit aspect. It keeps the current on-screen tile size and adds visible map rows
and columns as the window expands, retaining the original pixel proportions.
Further window resizing also changes visible land at that captured scale.
Population, money, demand, minimap, navigation arrows and mouse hit regions
follow the resulting canvas dimensions.
It also works when switching from the classic renderer. The aspect preference
and captured scale are saved in `sc-video.ini`; maximizing applies to the current
window.

Dense city tiles no longer qualify as the repeating menu desk. That false
match could recenter the native city rectangle, repeat buildings across the
expanded canvas and leave a black rectangle while scrolling. The BG2 city
cache stays on the world rendering path, with either HUD visibility and CPU
or GPU terrain. Regression cases cover all map sizes, both view anchors,
direction changes and transitions between a dense city and the budget panel.

**GPU TERRAIN** in F12 is an optional Windows SDL3/Direct3D 11 acceleration
path, enabled automatically when supported and session-only. It moves extended terrain decoding and
colour composition to the GPU, retaining CPU rendering for native pixels,
HUD, cursor repair and power warnings. Unsupported backends or failures use
the CPU renderer. The 960x800 Fit investigation below reduces repeated tile
lookups, spatial interpreter overhead and cached power work. Gains vary by
viewport and workload; heavy X50 simulation can still miss 60 FPS. See [GPU measurements and tests](GPU_PERFORMANCE.md).

The multiplier applies when the native simulation visits a zone and does not
advance a paused city. Expanded maps now account for their extra spatial work
when advancing the guest clock, so they no longer take four, sixteen or sixty-four
times as long to sweep land and initialize the growth fields. Calendar,
budget, demand, disaster and interrupt instructions retain their timing.
Normal-sized maps keep the original timing path. Native-loop regressions
cover zone sweeps, land value and population density on all four map sizes.
The city-center land-value radius also follows map dimensions: Normal keeps
the original 64-tile distance cap, while Big uses 128 tiles, Huge 256 tiles,
960x800 maps 512 tiles, and 1920x1600 maps 1024 tiles. The original value curve applies at the same
relative distance on each map. Existing cities gain this wider radius when
the simulation next recalculates land value; power, demand, pollution, crime
and transport requirements still apply.
The two hot five-point smoothing kernels execute one cell in C while charging
their original guest cycles. Differential tests compare both kernels against
the ROM across Big/Huge edges, zero and saturated fields, mixed values and
aligned/unaligned direct pages, including scratch memory and CPU flags.
Empty terrain sweeps and non-owner density cells also use equivalent C paths;
15,520 differential cases cover their Big/Huge coordinates, property types,
stack bytes, memory, flags and guest cycles. Terrain field accumulation covers
carry/overflow boundaries as well. Building decisions still use the ROM.

A saved stalled Huge city was reproduced at X50 and the fastest in-game speed.
The previous build remained at population 0 after 6,000 frames. With the fix,
the same city reached 2,380 after 460 frames (about 7.7 seconds), with developed
residential buildings and the original 2,000-person adviser celebration.
Its unpowered Commercial zones remained empty. Zone-only tests also cover
growth and power gating on all sizes, including 960x800 coordinates beyond 255 and spatial indices beyond 65,535.

Development batches run as host work without advancing the guest video/audio
clock. Calendar, budgets and disasters remain with the original dispatcher.
The Normal development interval is 800/400/200 guest frames at the three
in-game simulation speeds, independent of map size. The multiplier adds
fractional attempt credits; a four-millisecond host allowance spreads busy
updates over frames. Opening menus, pausing or loading cannot cause an
unbounded backlog. The native sweep retains census/transport bookkeeping;
private zone calls retain original decisions, RNG and growth/density writes.
Their scratch, stack, iterators and traffic tallies cannot overwrite a
suspended native call. Road-access caches age independently over eight Normal intervals and
invalidate immediately when transport tiles or empty-zone capacity change. A load rebuilds the disposable index.
`SC_DEVELOPMENT_SPEED=0|1|2|3|5|10|20|50` selects the initial value for testing.
`SC_DEVELOPMENT_BATCH_REFERENCE=1` selects the previous scheduler for comparison.

At accelerated speeds, electrical networks refresh at the selected multiplier
of measured native cadence. Live population refresh also runs at Normal speed,
with an eight-frame display refresh between census events. It counts actual
RCI capacity and cannot be replaced with the earlier partial native census.
The original Normal power schedule is retained.

For changed electrical networks, the original power flood fill runs
in private CPU/WRAM and commits only its power bitmap and tile power flags.
New connections and disconnections therefore update without waiting for the
next full simulation cycle. Original coal/nuclear capacities and conductive
tile rules still apply; this refresh does not advance time, budgets, demand,
randomness or disaster checks. Unchanged networks reuse the settled result.
Power flags remain consistent during native scans, and the completed bitmap
is published only at safe boundaries. Cached lightning glyphs disappear when
their zone is powered; coal and nuclear plants retain their original intrinsic
self-power behavior, including immediately after placement.
Newly placed unpowered buildings show the original animated lightning glyph
throughout the expanded viewport without waiting for the native tile cache.
Changing the owner's power flag also refreshes its warning footprint. The
population/power schedulers no longer add an area multiplier to their initial
cadence; tests verify all four accelerated rates on Normal, Big and Huge.

The development hooks currently support the verified US ROM with the default
interpreter. They do not implement acceleration in the optional `SC_FIBER=1`
execution path or regional ROMs.

Choosing **Start new city** or **Practice** opens a dedicated **MAP SIZE**
page with six choices: **120x100**, **240x200**, **480x400**, **960x800**,
**1920x1600**, and **3840x3200**.
It uses the game's original tall menu alphabet and numeral artwork. Mouse,
D-pad and confirmation buttons select a size; right-click/the game cancel
button returns to the main menu. Map size is no longer in F12 or the main
menu's former L toggle. The last selection is remembered in `sc-settings.ini`;
`SC_LARGE_MAPS=0|1|2|3|4|5` supplies a default for testing.

All sizes generate continuous full-size terrain, with independent simulation
fields, construction, power and population calculation. Preview samples the
whole landscape at 1:1, 2:1, 4:1, 8:1, 16:1 or 32:1. Mouse drag panning shows the original-size navigation minimap and a viewport
marker derived from the free camera and terrain zoom. Minimap and camera bounds
use the active saved dimensions. Existing cities keep their saved size;
scenarios retain their original dimensions.

Save loading locks the accepted slot while the native loader is running,
including held clicks and pointer movement. World geometry is restored before
native city initialization, with saved host simulation fields preserved. This
prevents a Huge city from falling back to Normal geometry and displaying black
terrain, duplicate minimaps or black silhouettes at the old arrow positions.

**START NEW JOURNEY** appears directly below **START NEW CITY** in the original
menu lettering, with the scenario option moved down. It always starts on a
Normal 120x100 map, regardless of the new-map preference. Reaching **100,000**
residents unlocks Big 240x200; reaching **1,000,000** unlocks Huge 480x400.
Dr. Wright celebrates each expansion using the original adviser window,
font, animation and fanfare, explaining the new dimensions and available land.
The Journey celebration replaces the ordinary Metropolis visit.

An expansion generates new terrain around all four borders and preserves every
existing city tile and persistent spatial simulation field. The city and camera move
together, so existing buildings stay at the same position in the view. Funds,
calendar and the live simulation random stream are unchanged by the expansion.
Threshold crossings are latched, so a later population drop cannot revoke an
unlocked border. Expansion waits for the end of a complete simulation cycle
and an idle construction/modal boundary. A pending celebration is completed
before the next expansion. Journey progress and pending messages persist in
save states and both native city slots' world sidecars. Ordinary cities and
scenarios do not expand automatically.

Expanded worlds use host buffers for all 48,000, 192,000 or 768,000 tiles and their spatial simulation
fields. Tile reads/writes, zone scans, transport, power, construction, disasters,
rendering and camera limits use the expanded dimensions without wrapping native
16-bit byte offsets or 8-bit coordinates. Power stacks, population density,
city-center calculations and spatial fields also use the full world. The city
overview samples the entire map, and its viewport marker uses those dimensions.
This feature supports the verified US ROM with the supported host execution path.

Huge terrain rendering now checks full coordinates on the native path as well
as the widescreen extension. Crossing coordinates 224/144 or 256 no longer
produces black bands from truncated byte-sized bounds.

Mouse control is enabled by default and can be toggled with F3 or **MOUSE
CURSOR**. Absolute window coordinates are converted to drawable coordinates,
then through the rendered destination rectangle and the renderer's live
screen anchor. This accounts for DPI, scaling, letterboxing, widescreen and
centered standalone screens. The entire adaptive city canvas accepts mouse
input, including both widescreen extensions and taller/centered views. World
coordinates and construction previews use the full canvas rather than the
SNES cursor's byte-sized position. The original cursor sprites follow the
selected tile across the full view. Menus keep their centered native bounds;
letterboxing, HUD controls and terrain outside the world cannot place tiles.

Right-drag in the city holds the game cursor still and scrolls in the direction
of displacement from the press point. Scrolling continues while held; larger
displacements increase the input repetition rate. **PAN SPEED** and **MOUSE
SPEED** adjust this panning response. Sensitivity does not displace an absolute
pointer target. Keyboard directions remain usable while held. Edge scrolling
works at the right and bottom city edges; left and top edges scroll with the
HUD hidden or when a centered view exposes terrain at those edges. Holding a
construction gesture suppresses edge scrolling. A drag captures the mouse and
keeps its last valid endpoint/preview while outside the window, over the HUD,
or outside the map. Re-entry resumes it; releasing outside commits the retained
plan. Right-click cancels it. Camera/tool changes and opening a modal cancel it.

Hold physical **X + arrow keys** to pan the free host camera over the full
map, including after mouse dragging. This retains zoom and stops when the
arrows stop. Releasing the arrows while X remains held cannot place a tool.
Standalone X and gamepad bindings retain their normal behavior.

Hold **Ctrl** for **3x scrolling**, or **Ctrl+Shift** for **10x scrolling**,
including keyboard camera movement, edge scrolling and mouse drag panning.
PAN SPEED and MOUSE SPEED also set drag sensitivity. Ordinary keyboard cursor
movement retains native scroll routines and boundary checks. These movement
modifiers do not fast-forward the city clock or change the chosen zoom.

Adaptive widescreen now keeps the date and tools on the left and places
population, money and RCI demand at the far right, on a continuous header.
The navigation minimap moves to the right edge; its outline projects the
visible city area using the actual map dimensions and canvas size, including
240x200, 480x400 and 960x800 worlds, instead of letting the old marker run outside its frame.
All four X-key navigation arrows follow the expanded canvas, including tall
Fit views and height-only expansion. The bottom arrow renders below the native
screen area, and the left/right arrows move down as that area grows. Hidden
directions stay hidden at city borders. Mouse hit regions
follow the relocated elements through window scaling and DPI conversion.
Tool outlines use the original byte-indexed ROM table for all 15 construction
tools. A hidden minimap also hides its position marker and mouse hit regions.

Game selection, map preview buttons and number arrows, the name keyboard,
difficulty and its confirmation, saved-city slots and scenario cards now use
direct hit regions. Gaps and locked scenarios reject clicks. The amusement-park/
casino gift choice follows the pointer over its two icons, including automatic
gift dialogues. F12 settings support hover and click selection. Native cursor menus (settings, options, tax/budget
and file pages) receive absolute coordinates without added D-pad travel.
Scenario edge scrolling respects the original unlock gate. A stationary
pointer leaves pad selection alone.

On the supported US host path, left-drag roads, rail and power lines creates a line
along the dominant axis. Parks and bulldozing create rectangles; RCI, police
and fire stations create rectangles of non-overlapping 3x3 placements. Yellow
outlines preview the gesture. Release commits it; right-click cancels it.
Coal and nuclear plants use non-overlapping 4x4 placements. Other large
buildings and gifts use the original single-placement controls.

Construction runs the original ROM's placement routines against private WRAM,
including eligibility, bridges, road joins, bulldozing and exact prices. It
preflights every eligible placement with a temporary treasury, rejects the
whole gesture if actual funds are insufficient, and commits once at the city's
idle input boundary. It does not advance the guest calendar or budget clock.
The transaction preserves live stacks, scheduler context, pointer and selected
tool. Changing tools or camera, losing focus, opening a modal or loading a
save state cancels an unfinished gesture. These controls have not yet had
hands-on desktop testing.

Population is calculated with 64-bit arithmetic and capped at
**9,999,999,999,999**. Host capacity accumulators count residential, commercial and
industrial capacity before the guest's 16-bit counters can wrap. The original
formula is preserved: `20 * (residential + 8 * (commercial + industrial))`.
Development retries do not count a zone more than once. Previous population,
signed migration and 1,200 monthly history samples also retain the full value.
The HUD and evaluation report show all ten digits. The cap does not increase
zone density or make the normal map capable of housing ten billion people.
The HUD uses the game's live digit sprites, original palette and person icon
at the same pixel scale as money. Extended values expand leftward in wide
views; narrow views place them beside the date to retain full-size digits.
The empty-city cap screenshot is a forced formatting fixture, not a calculated
population; ordinary gameplay continues to calculate population from zones.

At Normal speed, small cities take the original population routine when it can represent the
correct result. At overflow, the host bypasses its narrow arithmetic. Native
population/class gates use a bounded compatibility value, preserving all
original milestone thresholds. The authoritative population is the host value;
the original demand, budget and graph routines retain their native capacity
fields and scales. The extended history is saved, but its graph is not yet
rendered separately from the stock graphs.

Save-state version 4 includes the development context, explicit little-endian
64-bit population state, and full world tiles and spatial fields. Versions
1, 2, 3 and legacy states remain readable. The current world payload is version
5 and includes dimensions through 1920x1600, full spatial indices and Journey progress.
This build also reads version 2/3/4 world payloads and upgrades their city sidecars. Beta 1/2 executables cannot read the
new world payload; upstream executables cannot read version 4 states.
Beta 3 can read the current map geometry but does not understand Journey
progress, so use Beta 4 or later to continue a Journey.
Normal city saves also write `.srm.population` and `.srm.world` sidecars beside
the SRAM file. They preserve both city slots' full population, capacity totals,
history, map dimensions, tiles and simulation fields. **Keep all three files
together when moving a city save.** Slot-specific hashes reject stale metadata
if another emulator changes a city's native save; world records also checksum
their payloads. Without matching metadata, loading imports the population and
120x100 map available in native SRAM. Stock SRAM remains compatible with the
original game and contains the bounded population and top-left 120x100 region.

The population and development hooks support the verified clean US ROM with
the supported host execution path, not the optional fiber path or regional ROMs.

## Verified

- The native menu emitter draws all four lines from the original ROM font;
  mouse and controller selection cover all five choices, including Resume.
- Journey expands at both thresholds, preserves every old tile and all 17
  persistent spatial fields, translates the camera, and preserves funds,
  calendar and live PRNG state. Both save slots retain progress and messages.
  Original construction and power routines work on newly unlocked Huge land.
- The actual Journey start flow stays Normal with Huge selected in settings.
  Real residential fixtures of 100,000 and 1,000,000 residents trigger both
  expansions; the native Dr. Wright dialogs were rendered and inspected.
- Original terrain-fetch routines pass at 127/128, 223/224, 143/144, 255/256,
  the Huge far corner, negative coordinates and both outer bounds. A rendered
  Huge viewport crossing the former black-band boundaries was inspected.

- Huge generation is deterministic across 16 seeds and preserves all stock
  terrain fingerprints. Native simulation visits all 192,000 cells exactly
  once; density, pollution, land value, fire coverage and city-center routines
  complete across the full map. A full residential fixture calculates
  17,024,000 residents before development, without native counter wrap.
- All normal construction tools work beyond coordinate 255. Power tests cover
  both coordinate-256 seams, disconnection, and original coal/nuclear capacities.
- Population capacity for every RCI center tile matches the original ROM
  routine. Live growth/removal, ten-digit arithmetic, histories and save slots
  pass. Power tests verify every selected multiplier, fractional cadence,
  speed changes and safe bitmap publication during an in-flight native scan.
- Huge save/reload and older Big world/sidecar migration pass. A 7,200-frame
  Huge-city qualification completes multiple native simulation cycles.
- The actual Start New City flow generates a 480x400 map, accepts its preview,
  name and difficulty through mouse controls, and enters a playable city.
- A sparse Huge city with a far-corner nuclear plant renders at approximately
  60.1 FPS under SDL's software renderer. Crowded Huge-city performance remains
  dependent on CPU load, especially at 50x.
- Windows SDL3 executable builds with GCC/Ninja.
- Actual ROM zone routines: Normal produces byte-identical WRAM to the stock
  path; each RCI handler executes 2/5/10/50 attempts, counts the zone once,
  leaves calendar fields unchanged, and restores stack/direct-page registers, and resumes a mid-attempt snapshot
  with byte-identical results.
- Absolute coordinate tests cover every aspect setting, both anchors, multiple
  window sizes, DPI scales 1/2/3, viewport corners and noninteractive margins.
- The existing 3,600-frame headless activity qualification passes.
- Final 3,600-frame qualifications pass for both a stock city and an actual
  newly generated 240x200 city; the latter also commits construction past the
  old boundary with X50 development selected.
- Actual ROM construction tests cover costs, road junctions, skipping
  obstructions, zone spacing, rail, power, parks, bulldozing and byte-identical
  RAM after rejection of an unaffordable gesture.
- A running-city fixture commits six roads for $60 at the normal input
  boundary; the resulting road line was visually inspected after selecting
  the road tool through the game's toolbar.
- Menu tests cover hit regions, gaps, name keys, map arrows, save availability,
  scenario unlocks and pointer/pad coexistence. Game selection, map preview,
  name, difficulty and confirmation layouts were captured from the ROM and
  inspected to establish the hit regions.
- The host menu was rendered and visually inspected.
- Renderer tests cover independent status placement, moved mouse hit regions,
  normal/large minimap coverage at 256/448/684 pixels, far-corner clipping and
  unchanged PPU state. Actual pan and ten-digit population renders were inspected.
- Population tests run the original US calculation routine: byte-identical
  small-city results, counters crossing 65,535, mixed RCI totals, values above
  32-bit range, calculation at the exact cap, negative ten-digit migration,
  class thresholds, history rollover, portable saves and independent SRAM slots.
- Combined population/development tests verify a single host capacity tally per
  zone at every speed and byte-identical restoration during extra attempts.
- Version 3 stores exactly 9,999,999,999, reloads without a population override,
  displays all ten digits and passes a 600-frame qualification. Evaluation
  population and migration fields were captured from the ROM and inspected.
- Terrain tests preserve 16 stock-map fingerprints and verify deterministic
  continuous 240x200 generation, far-bank cell indexing and bounds guards.
- The original city-creation flow generates and starts a 240x200 city. Native
  panning reaches the far corner at camera coordinates (215, 178).
- The original simulation sweep visits all 48,000 cells exactly once, including
  coordinates above 127 and byte offsets above 65,535. Full-world power
  writeback, neighbour field strides and independent rendering anchors pass.
- A full residential-map fixture counts 211,200 capacity before the native
  counter wraps to 14,592, then calculates exactly 4,224,000 residents using
  the original population formula.
- Actual ROM placement routines build all normal tools beyond the old map
  boundaries; insufficient funds leave both WRAM and the full world unchanged.
- Actual ROM city compression and save/load restore full large-map tiles and
  spatial fields through independently bound save slots. Truncated or corrupted
  world records reject without changing the current city.
- The overview terrain bitmap matches an independently downsampled full world.
  All six disaster-trigger qualifications complete on a large map.

Live gameplay alignment, sustained city panning and crowded-city performance
still need hands-on testing. Other narrative dialogue pages retain their
original input behavior.

## Placement responsiveness (Enhanced Beta 2)

The host presents each completed frame before its pacing wait, avoiding the
extra input-to-display delay from waiting with a finished frame queued.
Committed construction and development cells render directly across the native
viewport as well as its margins, with live tile graphics, roof overlays,
native HUD/OBJ priority, windows and color math. They no longer wait for the
original SNES tile cache to refresh. Bulk edits cannot trigger a city-load
freeze; only the actual ROM city-load entry arms the fade hold.

Construction still commits at the safe city input boundary. It does not
accelerate the calendar or budgets. Recorded SDL/software-renderer checks ran
at about 60.1 FPS: a 20-zone drag queued for one frame and committed in about
1 ms; a 1,078-cell large-map bulldoze at 50x development queued for two frames
and committed in about 2.6 ms. These fixtures establish placement latency,
not a guarantee for every GPU or densely populated city. Screenshot capture
adds its own one-off frame cost. `SC_PERF=1` logs queue frames, commit time and
average/maximum frame-stage times for performance diagnosis.

Held **Tab** uses this port's fast-forward loop, independently of Mesen. It
runs six complete simulation frames per display update; **Shift+Tab** runs 24,
and **Ctrl+Shift+Tab** runs 96.
Calendar, development, demand, power, services and vehicles advance together.
The batch no longer collapses to a single frame when a heavy city consumes the
render budget. These are 6x/24x/96x targets: actual acceleration is limited by CPU/GPU
workload, and heavy batches can reduce presentation FPS. The title reports
simulation speed relative to the native clock, measured over real elapsed time.
Intermediate frames retain input, native PPU/APU timing, sprite evaluation,
overflow flags and OAM history while omitting unused native pixels and expanded
image composition. The final frame is rendered in full. Restored music keeps its
independent real-time audio clock and normal tempo; only recent native sound
effects are queued, avoiding accumulated playback behind the picture.
`SC_FAST_FORWARD=1` records the same held-Tab path for dummy-SDL testing.

The largest map's eighth-resolution fields are 480x400. Their native byte
iterators now retain full-width coordinates, allowing terrain quality, police
and fire passes to finish instead of wrapping. Shared native arithmetic helpers
inherit spatial clock scaling from their saved caller; global calculations and
redraw waits retain ordinary timing. This permits later calendar and density
passes without bypassing the original growth rules.

The top-toolbar View tool follows the current city camera and supports mouse
drag/edge panning, keyboard arrows and Ctrl-wheel zoom while active. It does
not enable construction. At reduced zoom, building labels retain their native
font size in spaced groups; hovering a building always shows its label. CPU
and GPU terrain composition share the same label artwork and coordinates.

Camera scrolling now validates the native staging cache throughout the city
viewport, rather than repairing only its outer eight-pixel bands. Stale terrain,
roof and lightning tiles are rebuilt from the live world while keeping the
toolbar, relocated minimap/arrows, higher-priority objects and modal pages.
Validation stops once a complete frame agrees with the staging cache. Checks
run once per eight-pixel span and do not modify guest memory or PPU state.
Regression cases cover Normal/Big/Huge maps, Huge coordinates beyond 255,
fine-scroll reversals, Ctrl-sized steps, centered/tall views, CPU/deferred
rendering and recovery to the native renderer. Windowed X/arrow and pan replays,
including Ctrl and fixed Tab batches, retained identical complete saved states;
CPU and GPU screenshots matched byte for byte.

The rendering path now caches decoded 4-bit tile rows while checking live VRAM
on every access, skips unused colour math, and culls edge sprites before pixel
sampling. Power refresh compares tile IDs directly instead of hashing the
entire map with a serial dependency chain. It still observes the selected speed
and native bitmap ownership. An alternating six-pair 240-frame X50 city replay
measured median work time of 3.267s before and 3.077s after (5.8% reduction).
Complete saved states and rendered frames matched byte for byte. Timings vary
with city workload and host load; this does not guarantee 60 FPS at X50.
See [performance measurements and GPU investigation](GPU_PERFORMANCE.md).

The extended Journey option list and its hand position now remain intact
while a selected option fades out, after the game switches its screen state.

Renderer regressions verify first-frame edited terrain, northwest roofs,
bulldozed terrain, unchanged unedited native pixels, low/high sprite priority,
opaque HUD preservation, immutable PPU state, 5,000-cell edits without a
load hold, and actual load holds releasing after the fade.

## Beta 8 map updates and mouse menus

The original bank-03 building footprint tables contain 240-byte row offsets.
The expanded map bridge now translates the offsets at their verified repair,
residential house growth/removal and destruction consumers, including the
corresponding destruction origin offsets. Previously, these updates could
write fragments 120 tiles apart into unrelated land on Big and Huge maps.
The original CPU still performs the updates; Normal map behavior is unchanged.
ROM-backed regressions check every map cell around 3x3, 4x4 and 6x6 repairs,
house growth/removal and destruction at the far borders of both map sizes.

Native save-slot/Save? and gift-selection loops maintain their own selection
values. Their live input-loop contexts now route pointer hits to those values,
reject gaps and empty gifts, and stop gift-menu clicks from becoming land
construction. Both save slots were clicked through the windowed mouse path and
verified to write SRAM and its population/world sidecars. Gift selection and
a gift placement beyond the original 256-pixel viewport on Huge were also
verified. ROM-backed tests cover all fourteen gifts, their native costs,
consumption, locality and landfill's water requirement. Private construction
now restores the live world's coordinate references as well as its map anchors.

The keyboard END button also confirms the selected difficulty and Yes/No on
the startup confirmation page. Recorded mouse clicks on END advanced through
both pages into a playable city.

The fixes prevent additional displaced tiles. Existing fragments already saved
by an earlier build are retained unless recovered from a known clean copy;
there is no automatic deletion of player buildings. Private test saves and
ROM-derived data are excluded from releases.

## Mouse reference and remaining verification

The mouse target is the behavior described in
[Vitor Vilela's SA-1 Beta 2 post](https://www.patreon.com/vitorvilela/posts/simcity-sa-1-2-168886217):
Most controls are implemented above. Remaining parity work is narrative
dialogue selection and live desktop verification of hover, panning, previews,
cancel gestures and every modal screen. No SA-1 patch bytes or third-party ROM
data have been imported.

## Run the Windows release

Extract the release ZIP, open `UrbanRecomp.exe`, and choose your own clean US
SimCity ROM in the launcher. No ROM or generated game code is distributed.
Use the included `Start-UrbanRecomp.cmd` to launch from the package directory.

For distributable builds, configure CMake with `-DSC_AOT=OFF` and use the
instructions in `SETUP.md`. The tests are `UrbanRecompPointerTest`, `UrbanRecompMouseUiTest`,
`UrbanRecompDevelopmentTest`, `UrbanRecompConstructionTest`, `UrbanRecompPopulationTest`,
`UrbanRecompMapGenTest` and `UrbanRecompWorldTest`. Development, construction,
population and world tests take
the clean US ROM path as their only argument.
For a headless city-state integration check, `SC_BUILD_TEST=frame:tool:x0:y0:x1:y1`
queues a gesture through the same input boundary in `--qualify` mode.
For recorded windowed mouse verification, `SC_MOUSE_INPUT` accepts comma-separated
`frame:canvas_x:canvas_y:SDL_button_mask` events, relative to the first windowed
iteration. `SC_MOUSE_TOOL` selects the tool. The events use the same mapping,
drag lifecycle, preview and commit path as live SDL input and can run with the
dummy SDL video driver. Outside release, re-entry and right-click cancellation
have been checked through this path.

## 960x800 Fit performance and electricity on reload

The city-load power fix now rebuilds the actual network before development
resumes, on every map size and either save slot. It seeds a settled cache for
Normal as well as accelerated development. It no longer temporarily powers
only the original 12,000 cells. The original plant capacities, conductive
rules, disconnected zones and native ownership of an in-flight bitmap remain.
Exact tile-ID comparisons and power-flag publication process multiple cells
per word, preserving every other tile/metadata bit.

A private 960x800 save had one nuclear plant, 306 zone/plant owners and 209
powered owners. Reload recovery and an independent fresh solve reproduced
those 209 flags. Its connected network contained 2,780 conductive cells; the
native flood stopped at 2,001 visits against capacity 2,000. The remaining
warnings include genuine brownouts, requiring another connected plant.
`SC_POWER_DIAG=1` reports plant counts, capacity and visited cells.

Adaptive raster work now shares world-tile lookups and warning classification
across each eight-pixel span. Plain margin colours are calculated once per
palette index per row; GPU-deferred terrain avoids redundant CPU tile decoding.
Live scanline palettes, flips, fades, windows, objects, power warnings and native
staging repair retain their ordering. Held terrain also retains its saved map
dimensions when a different city loads.

Native C paths now cover vacant and developed land-value cells, pollution,
density/pollution smoothing, crime, police/fire coverage diffusion, terrain
quality, ordered power traversal/search, growth scores, nine-cell zone replacement, zone capacities,
the simulation PRNG and batches of accelerated development attempts. Adjacent
spatial cells and their loop control are fused into bounded C spans. Tight beam
budgets and remaining tile mutations retain interruptible compatibility code.
Calendar, budgets and demand keep their normal cadence. The hardware clock
advances between scanline, HDMA, IRQ and wrap events without changing their timing.

The desktop renderer now selects a shared Vulkan compute/presentation device
with SDL 3.4 or newer. Embedded SPIR-V handles terrain, repaired city pixels,
objects and electrical warnings; ordinary frames have no GPU image readback.
Eligible city Mode 1 scanlines now capture native background planes for Vulkan
composition, including candidates used by repaired cells. Native sprite
evaluation, menus, unsupported display modes and some simulation routines remain
on the CPU.
Beta 12's filled 1920x1600 X50 adaptive-Tab sample averages 60.14 FPS;
p99 active work is 14.64 ms, with four of 3,174 warm frames over 16.7 ms.
The complete-city replay matches all integer state bytes and rendered pixels,
with zero main/kernel interpreter calls on that tested path. Other paths retain
compatibility fallback, and occasional frame spikes remain. Reference controls
and measured limits are documented in [GPU_PERFORMANCE.md](GPU_PERFORMANCE.md).

## Interactive zoom and extended population limit

Ctrl + mouse wheel changes the tile scale in the adaptive view without resizing
the window. Pinch/spread events use SDL 3.4 gesture scale where the platform
provides them; Windows touchpads that send Ctrl-wheel use the same wheel path.
Zoom reveals more land when reducing scale and keeps the complete native HUD
visible at the upper limit. Geometry checks cover DPI, centered views, bounds
and pointer mapping. Hardware touchpad behavior has not been verified locally.

Enhanced Beta 17 allows terrain spanning up to 65536 native pixels
across the view, twice the previous limit, separately from the canvas size.
This can show the entire 3840x3200 map in the default widescreen view and
Fit to Screen. HUD, toolbox, overview maps and menus retain their
normal size. Keyboard and mouse edge scrolling, toolbar popups, gifts and
Dr. Wright messages preserve the selected terrain zoom. Mouse edge scrolling
uses the free host camera, including Ctrl's 3x rate. The host camera allows
64 canvas pixels of extra space above and below the city at each zoom level.

Both yearly and toolbar budget panels accept a freely moving mouse. Click the
visible adjustment arrows or Go With Figures; keyboard and gamepad navigation
continue to jump between controls. A stationary mouse does not undo those jumps.
The hand appears across the entire widened header. The zoomed toolbox uses
its own original artwork, preventing land and building sprites from leaking
into its background.

The main menu's centered text and extended panel retain their geometry during
the fade. The mouse hand is hidden while the title screen fades out, until
the menu's sprite graphics are ready; it does not sample the logo's sprite
bank or change the title's OAM. Hidden **3. TEST CITY 3** aligns with the numbered save rows and its
selection pointer. Empty slots display only **1.** or **2.**; existing save
names and dates remain visible. The third row uses unused sprite slots so it
cannot overwrite the second city's label.

Population calculation, negative migration, history, reports, HUD and save
records support 9,999,999,999,999. Existing 64-bit save encoding is retained, so
older city values load without a format migration. Thirteen-digit HUD values
and fourteen-column signed migration use the original game glyphs. Native
compatibility fields retain their safe six-digit mirror. ROM-backed arithmetic
tests exercise the former ten-billion boundary and the new calculation cap,
including mixed capacities, saturation, reports and save-slot round trips.

The city HUD has Copy and Paste buttons in the original adviser font.
Drag a rectangle with Copy; releasing selects Paste automatically. Leaving the
window during a drag retains the last valid selection. Every intersecting
ordinary building is included in full, without recursively pulling in adjacent
buildings. Roads, rails, power lines, crossings, bridges, parks and natural
terrain are included. Reward/gift buildings are excluded completely; their
footprints remain holes in the copy and paste outline.

Paste shows the total construction price using the original HUD number tiles.
Prices come from the ROM's construction table, with bridge surcharges and each
crossing's components counted. Natural terrain is free. Paste replaces terrain;
an occupied destination or insufficient funds rejects the complete transaction.
Practice mode retains free construction. Road, rail and wire joins use the
original connection tables; power and population are refreshed after a paste.
Copying grants no gifts and consumes no gift inventory. Right-click or select
an ordinary palette tool to leave Copy/Paste.

Copy also accepts standalone house artwork and recovers complete ordinary
footprints when a saved city's centre marker has been replaced. Selection
expansion uses the original rectangle, so a rough border does not recursively
pull in a neighboring city block. Reward artwork remains excluded even if
its centre marker is missing. Regression checks select all 13,056 ordinary
artwork cells individually in the supplied saved city and every cell of
mature RCI and larger ordinary footprints across all five map sizes.

Hold the middle mouse button over land and drag to pan. Moving right/down
drags the land right/down, moving the camera in the opposite direction. The
host camera applies exact world-pixel displacement each display frame,
including fractional motion. Stationary holds and releases stop immediately;
there is no scroll queue or inertia. Zoom is preserved. PAN SPEED and MOUSE
SPEED adjust sensitivity; the defaults track the drag one-to-one. Hold Ctrl
for 3x drag displacement or Ctrl+Shift for 10x drag displacement.
Middle-button panning hides the pointer and uses centered relative
mouse capture, so repeated movements continue beyond the screen edge.
Right-button dragging over land uses the same camera when no clipboard tool
is active. Releasing the button, losing focus, opening F12 or disabling mouse input
releases the cursor. Copy/Paste stays selected while panning; construction and
selection drags can still continue outside the window. Movement uses the
full-world compositor, independent of the cartridge's movement steps and
camera bounds, without changing guest camera registers or advancing extra
game frames. Loading another city resets the host view offset. Component and
Vulkan replays cover fractional movement, stationary holds, preserved zoom,
off-window release and the far corner of the 1920x1600 map.

Enhanced Beta 11 includes the fifth 1920x1600 size. ROM-backed clipboard tests cover all five sizes,
complete footprints, reward exclusions, 96 original-routine join comparisons,
power reconnection, mature building capacity, wide totals and atomic rejection.
CPU and Vulkan mouse replays exercise off-window drag release, automatic Paste
selection, the moving outline, price display and a successful paste.

The fifth size uses 32-bit spatial indices and word-sized density counters.
The 480x400 quarter-resolution grid crosses 8-bit coordinates, while growth
and coverage grids cross 65,535-byte offsets. Version 5 world saves preserve
these fields and migrate older city sidecars without changing native SRAM.
Terrain, construction, power, population, minimap and renderer checks cover
far corners of the new size. Journey retains its original expansion thresholds
and stops at 480x400.


The user-provided **MSU1 SimCity (Restored)** soundtrack now replaces all 19
music tracks when a complete PCM set is present at `music/restored` beside the
executable. Import it with `python tools/import_restored_music.py <archive>`;
local CMake builds copy the imported tracks beside the executable. The Beta 11
portable release embeds all 19 tracks, ready to play; the files stay out of Git.
Credits: Pinci / Church of Kondo for restoration and Relikk for the PCM set;
see [CREDITS.md](../CREDITS.md). The game does not need an MSU-patched ROM.
Track numbers and loop points are retained from the pack, following the clean
US music-command mapping verified against the [original MSU-1 patch](https://www.zeldix.net/t1602-simcity).

The dedicated music worker mixes the restored stereo tracks at 44.1 kHz with
native SPC sound effects, resampling only those sound effects. Tracks are
loaded once before playback, avoiding file reads on either gameplay's hot
path or the music worker. The existing in-game music toggle pauses/resumes
PCM playback; F12 pauses the worker. New song requests retain the driver's
normal restart behavior. State loading starts the saved song from its
beginning and uses the verified SPC stop/acknowledgement command to prevent
original music playing underneath it. PCM position is host state, so existing
save formats remain compatible.

Missing or invalid packs retain the original soundtrack. `SC_RESTORED_MUSIC=0`
selects the original soundtrack for developer reference runs; setting it to a
directory selects a different local pack with the same 19 filenames. The
restored pack needs the dedicated worker; disabling it retains native audio.
At 10,000,000 population, Dr. Wright announces **Megagopolos** once per city.
The city theme changes to Markify's restored unused Vanilla Lake beta theme
from Super Mario Kart. Optional `scity-msu1-20.pcm` supplies this additional
loop; the single EXE embeds it. Menu songs, disaster music and fanfares retain
their original assignments. City and state saves preserve the earned milestone
and any unfinished announcement; older saves can earn it normally.
At 100,000,000 population, **Gigagopolois** adds a second announcement and
switches the city theme to **LOOP16B** from **LOOP816 / Soyo Oka (2023)**.
Artist, composer and publisher: Soyo Oka. Optional host track 21 contains the
full arrangement and ending fade. The rewards run in order, even when one
growth update crosses both thresholds, and neither repeats after earning it.
Tests cover all imported tracks and their authored loops, interpolation at
loop seams, mixing saturation, mute/resume, worker pause/reset/shutdown,
44.1 kHz playback through a 250 ms game-thread stall, and a real US-driver
save showing original music silenced while native sound effects remain audible.

The desktop mouse now positions the original HUD hand directly, including
the left toolbar and relocated right header while Copy/Paste is selected.
The hand retains its original 16px tile and stays above host clipboard labels;
HUD hit testing continues to use the native menu coordinates.

F12 options support mouse clicks and distinct Left/Right adjustments. Ordinary
coal and nuclear plants support 4x4-spaced drag building, with the full price
checked before placement. Gift icons accept their whole 32x32 image; adviser
clicks follow the centered panel and can dismiss single-gift messages.

F12's **MUTE CITY WARNINGS** cheat suppresses crime, traffic and pollution
notice banners and adviser visits. It defaults to OFF and is session-only.
The underlying calculations, maps, effects on development and other messages
remain active. Turning it off restores ordinary warning publication.

The map-size page is inset 16 native pixels farther down, including the
selection arrow and mouse hitboxes. All five choices retain the original font.
The size selection remains highlighted through the exit fade, even after the
next screen restores the main-menu action value.

The Windows portable EXE contains the runtime, assets, restored music,
documentation and license notices. On launch it verifies and unpacks a versioned
cache under `%LOCALAPPDATA%/UrbanRecomp/bundles`. Saves, settings and ROM selection
remain in the portable EXE's folder, independent of that cache. It does not
replace earlier installations or migrate their files automatically. Keep all
three city save files together when moving them to a new folder.
`--portable-docs` opens the embedded documentation; `--portable-extract <folder>`
extracts the complete bundled files for inspection. Game arguments are forwarded.


## Hidden test City 3

Press Ctrl+Shift+tilde on Resume Saved City to reveal Test City 3. When no
ordinary saves exist, the shortcut also works on the main menu. An unsaved
City 3 opens MAP SIZE, then DEVELOPMENT SPEED, supporting all six sizes from
120x100 through 3840x3200. It then generates the selected city from code.
A saved City 3 loads its existing size, speed and development state directly.

Generated cities use water-free map 31337. Every residential, commercial and
industrial zone starts empty, with zero population and ordinary zone capacities.
Power, rail, roads, funded police/fire stations, parks, 27 finite gifts,
a stadium, airport and dry-land port are already placed. The source permits
a dry port to lift the industrial demand cap; boats still require water.
Native growth, employment, pollution, crime, demand, disasters and upkeep
remain active. Taxes start at 3%; no money/growth cheat is enabled.

Industry occupies the global outside band, with green-buffered commerce and
inner residential districts. Local commerce helps housing cross the native
density-65 apartment gate. Gifts are scored using native terrain packing and
diffusion, then impossible residential sites are converted to commerce.
See [PLACEMENT_EFFECTS.md](PLACEMENT_EFFECTS.md) for every placement item and
[TEST_CITY_LAYOUT.md](TEST_CITY_LAYOUT.md) for counts, source calculations and
natural ten-year growth measurements. Capacity is an upper bound, not a
promise of complete occupancy or a measured stable maximum.

Escape opens the native save dialog during city play and acts as Back/Close
in menus. Saving City 3 appends its checked full-world record inside the SRM
without replacing the two original cartridge slots. Back up the entire SRM,
together with the normal .world and .population sidecars for Cities 1/2.

Zoom changes terrain scale while HUD, minimap, overview panels and menus keep
their normal size. Expanded-city scans distribute zone visits around the map,
and save/load preserves the scan order and position.

Test City 3 entry keeps the city preparation frames black until the native
entry fade starts and has reached black, including palette-based fades.
The completed view then fades in at the selected zoom with fixed-size HUD.
Both generated and saved City 3 use this presentation gate; save-state loads
reset it. The normal menu exit fade remains visible.


## Land types and city difficulty

The map-selection header has GENERATION and LAND TYPE rows. Use the left/right
arrows or click the value to cycle; keyboard/gamepad Up/Down moves between rows
and the original map controls. Both rows retain the original cartridge lettering
and device frame. Generation determines geography; land type determines its art.

Land types are Native, Basalt (dark volcanic ground and lava), Amazon (rainforest
colors and extra forest patches), Desert (pale sand and dry scrub), Mars (red soil and
rock), Venus (sulfur terrain and acid seas), Arctic (snow and ice), Swamp
(wetland soil and vegetation), and Moon (gray lunar dust and dark ice seas).
Moon generates no natural forests, including Practice and Journey expansions;
parks can still be planted. These are graphical themes using ordinary terrain
rules: lava/acid still has water's construction rules, and themed vegetation
still has forest's rules. Each city saves its type, including Journey expansions.
Older saves use Native. Every new city defaults to Native; loaded cities retain
their saved theme. Opening a new map also clears the previous city's host
camera offset, focus target and zoom.

The themes retain the original terrain pixels, connected shoreline and forest
masks, and animated water. Explicit soil, bank, water and canopy color ramps
replace the earlier noisy eight-pixel patterns, flat lava and clipped vegetation.
Red, yellow, orange, metal and other unrelated palette colors stay native.
The renderer adjusts the small terrain palette rather than rewriting terrain
CHR every frame; CPU and GPU paths use the same themed palette.

Custom land types follow the saved in-game month. Seasonal palette anchors are
January (winter), April (spring), July (summer) and October (autumn); the intervening
months blend toward the next anchor, including December to January. Colors remain
still when the calendar is paused. Development multipliers affect development,
not the seasonal calendar; Tab advances seasons as it advances the game clock.

| Land type | Seasonal appearance |
| --- | --- |
| Native | Original cartridge seasonal colors, untouched |
| Basalt | Fresh ash-green vegetation, autumn olive tones, light winter frost; lava stays hot |
| Amazon | Evergreen rainforest with wet winter/spring and warmer, drier autumn colors |
| Desert | Fresh spring scrub, warm summer sand, dry autumn scrub, cool winter tones |
| Mars | Rusty summer ground and rocks, pale winter frost |
| Venus | Subtle sulfur-ground and acid-sea color shifts, without snow or freezing |
| Arctic | Spring melt, a short summer tundra thaw, autumn cooling, deep winter snow and icy water colors |
| Swamp | Fresh spring greens, mossy summer, amber autumn, frosted winter vegetation and banks |
| Moon | Stable gray lunar dust and dark ice seas throughout the year, without natural forests |

These are visual changes; seasonal ice retains water's construction and transport
rules. Pan minimaps match the current season; map-selection previews show January,
the new city's starting month. Existing city saves keep their date and theme, so
the correct seasonal appearance returns automatically on reload.

Football stadiums on Mars, Venus and Moon use enclosed dome roofs for both
stadium artwork variants. Their normal simulation and capacity remain intact.
Only stadium-owned character graphics change; other buildings keep their art.
Snapshots store native graphics and reapply the city's domes on reload.

Difficulty now uses four vertical rows: Easy, Medium, Hard and Super Hard.
Super Hard starts with the same funds as Medium to offset its increased disaster
rate. Starting funds scale with map dimensions:

| Map | Easy | Medium | Hard | Super Hard |
| --- | ---: | ---: | ---: | ---: |
| 120x100 | $20,000 | $10,000 | $5,000 | $10,000 |
| 240x200 | $40,000 | $20,000 | $10,000 | $20,000 |
| 480x400 | $80,000 | $40,000 | $20,000 | $40,000 |
| 960x800 | $160,000 | $80,000 | $40,000 | $80,000 |
| 1920x1600 | $320,000 | $160,000 | $80,000 | $160,000 |
| 3840x3200 | $640,000 | $320,000 | $160,000 | $320,000 |

Practice uses the Easy amount for its chosen size. Scenarios and loaded city
balances retain their own funds. Super Hard uses Hard's economic rules and halves
its random-disaster threshold from 1200 to 600. The original probability check
uses an inclusive RNG range, so the chances are 1/1201 and 1/601 per check.

## Extended calendar

The host calendar retains years through **999999** for stock and expanded
cities. The HUD uses the original digit font without leading zeroes and moves
the month to fit five/six digits. Budget/Tax date headings also show the full
year. The native month rollover, January collection and simulation clock are
retained. After 65,535, a saturated native year keeps late-game date gates true;
the separate full year continues advancing. At 999999 the year stays at that
limit while months and annual budgets continue.

World metadata version 8 stores the full year in city sidecars, City 3's SRM
record and snapshots. Versions 2 through 7 remain readable; old cities import
their native date. Journey expansions preserve the full year. Tests cover
December/January at 9,999, 65,535 and 999,999, save-slot reloads, metadata
migration, budget digits and unscaled HUD digits/month placement.

## Manual disasters and debug gifts

Shift-click anywhere on the original 16x16 toolbox ? image (guest x32..47,
y158..173) to open all 15 gift choices, including when native availability is
  dimmed. All 15 icons fit in four columns without scrolling. The picker pauses
  the city while music continues, consumes its opening/selection click, and
  accepts mouse, D-pad/arrows and Enter. Selection enters the original gift-tool palette
transition, bypassing only its four-inventory-slot chooser. Gift construction,
price, water-only landfill behavior and inventory consumption remain native.
A full earned-gift queue temporarily lends one slot; placement, cancellation or
opening Save restores that earned gift. Otherwise the debug choice occupies a
free native slot and persists like a queued gift.

The real Disaster panel has a temporary third row labelled NUKE and UFO.
Its buttons use the game's lettering and bevels, with separate artwork rather
than writes into occupied sprite/tilemap slots. Mouse and ordinary native D-pad
movement share the same full-button hitboxes. Native confirm toggles bits 6/7;
back closes the panel normally. The host consumes these bits after closing it.
Disaster triggers are available through this in-game panel; F12 has no disaster
trigger section. Post-load power repair is always enabled. Generation is chosen
on the map screen and development speed comes from city setup/save metadata.

Manual scenario events use instruction-boundary hooks, working in compiled C
and interpreter reference runs. The old live-ROM UFO NOP could not alter a
compiled branch. Explicit triggers bypass the random-disaster gate and UFO's
84,488 population gate without faking population or clearing the saved cheat.
They retain the original attack/animation and restore mode, scenario index and
its previous countdown after the original DEC acknowledges completion.
Manual meltdown finds the native first nuclear center (tile 0x27c) with full map
coordinates, then invokes the original demolition/radiation/news routine.
The no-plant path remains a no-op. Regression runs cover all six map sizes,
including a nuclear plant near the far corner, and check plant removal,
radiation, news and state restoration against both original CPU and native C.
