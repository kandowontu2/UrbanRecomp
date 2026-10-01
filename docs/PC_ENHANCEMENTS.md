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

Extra attempts run as host work without advancing the guest video/audio clock.
Calendar, budgets and disasters are not fast-forwarded. Larger cities at X50
still require more host CPU work and can reduce performance. Normal takes the
unaltered guest path. `SC_DEVELOPMENT_SPEED=1|2|5|10|50` selects the initial value
for testing; the menu setting is otherwise session-only.

At accelerated development speeds, changed electrical networks are checked at
2x, 5x, 10x or 50x the host refresh cadence. The original power flood fill runs
in private CPU/WRAM and commits only its power bitmap and tile power flags.
New connections and disconnections therefore update without waiting for the
next full simulation cycle. Original coal/nuclear capacities and conductive
tile rules still apply; this refresh does not advance time, budgets, demand,
randomness or disaster checks. Unchanged networks skip the extra flood fill.
Normal retains the game's original power schedule.

The development hooks currently support the verified US ROM with the default
interpreter. They do not implement acceleration in the optional `SC_FIBER=1`
execution path or regional ROMs.

The game-selection menu has a **L LARGE MAPS** button. Click it or press **L**
to toggle new maps between **120x100** and **240x200**: twice the width and
height, four times the area. F12 also exposes **LARGE NEW MAPS**. This preference
is saved in `sc-settings.ini`; `SC_LARGE_MAPS=0|1` can override its initial value.
Choose **Start new city** after enabling it. Map numbers and NEXT generate a
continuous full-size landscape; the preview shows the whole landscape at half
scale. Existing cities keep their saved dimensions and scenarios retain their
original dimensions.

Large worlds use host buffers for all 48,000 tiles and their spatial simulation
fields. Tile reads/writes, zone scans, transport, power, construction, disasters,
rendering and camera limits use the expanded dimensions without wrapping native
16-bit byte offsets. The city overview samples the whole world at half scale.
This feature supports the verified US ROM with the default interpreter.

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
240x200 worlds, instead of letting the old marker run outside its frame.
Right/up/down navigation arrows follow the wider canvas. Mouse hit regions
follow the relocated elements through window scaling and DPI conversion.

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
tool. Leaving the playable surface, changing tools, losing focus or loading a
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

Small cities take the original population routine when it can represent the
correct result. At overflow, the host bypasses its narrow arithmetic. Native
population/class gates use a bounded compatibility value, preserving all
original milestone thresholds. The authoritative population is the host value;
the original demand, budget and graph routines retain their native capacity
fields and scales. The extended history is saved, but its graph is not yet
rendered separately from the stock graphs.

Save-state version 4 includes the development context, explicit little-endian
64-bit population state, and full world tiles and spatial fields. Versions
1, 2, 3 and legacy states remain readable. Older executables cannot read version 4.
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
