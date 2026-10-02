# PC enhancement work

This fork of [blackerking/UrbanRecomp](https://github.com/blackerking/UrbanRecomp)
contains development-speed, population, mouse and playable large-map
enhancements. Windows packages are available from
[the enhanced fork's releases](https://github.com/kandowontu2/UrbanRecomp/releases).

## Implemented

F12 opens the host settings overlay; F10 remains an alias. **DEVELOPMENT SPEED**
cycles through Normal, X2, X5, X10 and X50. The extra work repeats the original
residential, commercial and industrial development decisions per simulation
tick. It recalculates capacity after tile changes, while running population
accounting and transport probing only once per zone. Demand, power and access
requirements still apply; faster development also means faster decline when
those requirements are unmet. This is a multiplier of development attempts,
not a promise of a particular population increase.

The multiplier applies when the native simulation visits a zone and does not
advance a paused city. Expanded maps now account for their extra spatial work
when advancing the guest clock: Big and Huge no longer take four or sixteen
times as long to sweep land and initialize the growth fields. Calendar,
budget, demand, disaster and interrupt instructions retain their timing.
Normal-sized maps keep the original timing path. Native-loop regressions
cover zone sweeps, land value and population density on all three map sizes.
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
growth and power gating on all sizes, including Huge coordinates beyond 255.

Extra attempts run as host work without advancing the guest video/audio clock.
Calendar, budgets and disasters are not fast-forwarded. Larger cities at X50
still require more host CPU work and can reduce performance. Normal takes the
unaltered development path. `SC_DEVELOPMENT_SPEED=1|2|5|10|50` selects the initial value
for testing; the menu setting is otherwise session-only.

At accelerated development speeds, population and electrical networks refresh
at the selected 2x, 5x, 10x or 50x multiplier of their measured native cadence.
The scheduler averages alternating native phases and retains fractional-frame
credit; it does not use a fixed, generally faster polling rate. Population is
recounted from actual developed RCI zones, without changing the native partial
simulation tally. The native pre-development census cannot overwrite that
current count. Normal retains the original population and power schedules.

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

The game-selection menu's map-size button, or **L**, cycles **Normal 120x100**,
**Big 240x200**, and **Huge 480x400**. Huge doubles both dimensions of Big:
four times Big's area and sixteen times Normal's area. F12 also exposes
**NEW MAP SIZE**. This preference is saved in `sc-settings.ini`;
`SC_LARGE_MAPS=0|1|2` selects Normal, Big or Huge for testing.
Choose **Start new city** after selecting the size. Map numbers and NEXT
generate a continuous full-size landscape; the preview samples the entire
landscape at 2:1 for Big and 4:1 for Huge. Existing cities keep their saved
dimensions and scenarios retain their original dimensions.

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

Expanded worlds use host buffers for all 48,000 or 192,000 tiles and their spatial simulation
fields. Tile reads/writes, zone scans, transport, power, construction, disasters,
rendering and camera limits use the expanded dimensions without wrapping native
16-bit byte offsets or 8-bit coordinates. Power stacks, population density,
city-center calculations and spatial fields also use the full world. The city
overview samples the entire map, and its viewport marker uses those dimensions.
This feature supports the verified US ROM with the default interpreter.

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

Adaptive widescreen now keeps the date and tools on the left and places
population, money and RCI demand at the far right, on a continuous header.
The navigation minimap moves to the right edge; its outline projects the
visible city area using the actual map dimensions and canvas size, including
240x200 and 480x400 worlds, instead of letting the old marker run outside its frame.
Right/up/down navigation arrows follow the wider canvas. Mouse hit regions
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

On the US interpreter, left-drag roads, rail and power lines creates a line
along the dominant axis. Parks and bulldozing create rectangles; RCI, police
and fire stations create rectangles of non-overlapping 3x3 placements. Yellow
outlines preview the gesture. Release commits it; right-click cancels it.
Large buildings and gifts use the original single-placement controls.

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
**9,999,999,999**. Host capacity accumulators count residential, commercial and
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
3 and includes Huge map coordinates and Journey progress. This build also reads Beta 1/2 world
payloads and upgrades their city sidecars. Beta 1/2 executables cannot read the
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
the default interpreter, not the optional fiber path or regional ROMs.

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
runs up to six guest frames per display update but reserves time for the final
rendered frame, adapting the boost to the city workload. Intermediate frames
retain input and native PPU/APU work while omitting the expanded image
composition. The title shows the measured Tab multiplier. Only the latest
batch audio is queued, avoiding accumulated playback behind the picture.
`SC_FAST_FORWARD=1` records the same held-Tab path for dummy-SDL testing.

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
