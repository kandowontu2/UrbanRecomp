# Enhanced fork changelog

## 1.2.0 Enhanced Beta 22 — 2026-10-06

- Fix manual UFO and nuclear-meltdown activation for the native compiled build.
  Explicit triggers bypass random-disaster suppression and the UFO population
  gate without editing ROM, population or cheat values, then restore scenario
  identity and the previous event countdown. Search nuclear plants with full
  map coordinates and run the native removal/radiation handler. Add temporary
  NUKE and UFO buttons as a third row in the original Disaster panel, with
  mouse/controller/keyboard selection; attacks start after closing the panel.
- Add Shift-click on the toolbox ? image to open an all-15-gifts debug picker,
  even when dimmed. Use normal gift placement and preserve queued earned gifts.

- Make held Tab advance six complete simulation frames per display update,
  Shift+Tab advance 24, and Ctrl+Shift+Tab advance 96, including calendar, development, demand, services
  and vehicles. Remove adaptive truncation that reduced busy cities to one
  frame and almost no boost. Keep restored music at its real-time tempo and
  report actual simulation speed against the native clock in the window title.
  Actual acceleration remains limited by city workload and hardware.
- Complete the largest map's terrain-quality and service fields with full-width
  coordinates. The former byte iterator wrapped before reaching the 480x400
  field boundary, stalling the calendar and subsequent density recalculation.
  Scale shared arithmetic work by its spatial caller while retaining normal
  timing for global calculations, menus and redraw waits.
- Render the top-toolbar View tool's third world graphics layer at the selected
  city zoom in both CPU and GPU terrain composition.

- Add Moon to LAND TYPE with gray lunar ground and stable year-round colors.
  Remove naturally generated forests in new Moon cities, Practice and Journey
  expansions across all supported sizes; preserve shores and water mechanics.
- Cover both football stadium variants with domes on Mars, Venus and Moon.
  Keep their simulation behavior and other buildings' artwork unchanged.
  Restore native stadium graphics when leaving these themes and before saving
  snapshots, then reapply the selected city's domes on reload.
- Preserve short city notices' native opaque paper, ink and borders over
  repaired/zoomed terrain, including CPU and deferred terrain rendering.

- Rebuild hidden Test City 3 from the source rules: global outside industry,
  green-buffered commerce, paired inner housing with local jobs, 27 finite
  gifts scored by the actual terrain diffusion, and all three demand-cap
  facilities including a dry-land port. Convert housing sites that cannot
  obtain a positive growth score. Keep every R/C/I lot empty at entry, native
  capacities, 3% tax and full service upkeep. Unsaved City 3 asks for size/speed;
  existing saves retain their own layouts and City 1/2 remain intact.
- Add a source-based guide covering every placement tool and all 15 gifts,
  and require natural multi-year validation for future layout changes. In the
  controlled 120x100 test, the revised city has 310,880 people and no completely
  empty housing after ten years; the former layout has 77,840 and 215 empty lots.
  Document conditions and remaining density/land gates without claiming a
  stable maximum or global optimum.
- Fix largest-map police/fire coverage conversion wrapping at 65,535 cells;
  both native conversions now finish all 192,000 cells instead of repeating
  work and delaying development/calendar updates.
- Extend the calendar through year 999999, with no leading zeroes in the HUD
  and budget/Tax date headings. Preserve full dates in city saves and snapshots,
  import older metadata, and retain native month/yearly collection behavior.

## 1.2.0 Enhanced Beta 21 — 2026-10-06

- Changing GENERATION or LAND TYPE keeps the current preview until its complete
  replacement is ready, then publishes the terrain and colors together. Type
  changes preserve preview zoom/pan and no longer clear the map or replay its
  initial generation reveal. New map numbers still build on screen.

- Add four seasonal palettes to all seven custom land types, with monthly blends
  between January/April/July/October anchors driven by the saved city calendar.
  Amazon stays evergreen through wet/dry shifts; Desert has warmer summer sand
  and seasonal scrub; Mars gains winter frost; Venus retains sulfur/acid colors;
  Arctic has a brief summer thaw and deep winter snow; Swamp turns amber in
  autumn and frosts in winter. Basalt vegetation/rock changes while lava stays
  hot. Native seasons remain untouched. Pan minimaps use the current month and
  map-selection previews show the January starting season. Reloads restore the
  correct season without new save fields or changes to simulation rules.

- Rework land-type graphics around the native terrain textures, animated water
  and connected shore/forest masks. Remove noisy repeated tile patterns and
  cut-up vegetation. Use deliberate soil, water and canopy color ramps, with
  textured lava, softer rainforest colors, sand, rust, snow and wetland tones.
  Preserve unrelated red, yellow, orange and metal colors.

- Add a second LAND TYPE row below GENERATION with Native, Basalt, Amazon,
  Desert, Mars, Venus, Arctic and Swamp. Preserve the full map-device frame,
  original lettering, clean beveled arrows, keyboard navigation and mouse hits.
- Add terrain graphic sets for dark volcanic rock/lava, rainforest, dunes,
  Martian soil/rock, sulfur terrain/acid seas, snow/ice and wetlands. Amazon
  adds extra native forest patches. Previews and pan minimaps match the terrain;
  land type stays with the city through saves, reloads and Journey expansions.
  Existing saves retain Native graphics.
- Add Super Hard to a four-row difficulty selector. It starts with Medium's $10,000
  on 120x100 and doubles Hard's random-disaster probability (threshold 600
  rather than 1200, using the original RNG); other rules inherit Hard safely.
- Double starting funds per map-size step, from the original amounts up to
  32 times on 3840x3200. Selection and confirmation display the actual amount.

- Add a GENERATION selector to the map-selection device, using the original
  caption frame, cartridge lettering, beveled arrows and hand cursor. Cycle
  Native, Procedural, Islands, Lakes, Rivers and Fractal with mouse clicks or
  keyboard/gamepad controls, and regenerate the preview for the selected type.
- Preserve the map-selection caption's complete native bevel and background
  priorities, with outward-facing generation arrows and a clear caption gap.
- Remove the decorative SNES emblem from the map-selection header.
- Shift the map-selection caption further left and reserve equal four-pixel
  gaps between generation names and their arrows, including PROCEDURAL.
- Preserve the map-selection device's shaded right frame edge and inset the
  generation controls so the arrow cannot overwrite the border.
- Draw clean side-arrow silhouettes in native highlight/shadow colors, without
  rotated background patches or fragments of neighboring number arrows.
- Rework Fractal with warped coastlines and a map-balanced sea level, avoiding
  almost entirely flooded small maps. Add Continent, Delta and Atolls terrain
  styles to map selection and F12, retaining native shoreline and forest tiles.
- Make Islands landmasses substantially larger and connected, and replace thin
  Atolls rings with broad buildable islands, smaller lagoons and curved sea
  entrances. Check full district footprints across seeds and all map sizes.

## 1.2.0 Enhanced Beta 20 — macOS startup revision — 2026-10-05

- Normalize optional copier headers before native ROM fingerprint checks,
  matching the launcher's verification. Diagnose unreadable/unsupported ROMs
  before startup, show Finder errors and preserve a Mac startup log.
- Add a universal macOS app packaging path for Apple Silicon and Intel,
  including restored music, assets, credits and licenses. Bundled Mac builds
  keep saves/settings in Application Support and locate assets in Resources.

## 1.2.0 Enhanced Beta 20 — 2026-10-05

- Anchor city and map-preview Ctrl+wheel zoom to the actual mouse position,
  preserving the terrain beneath it across widescreen, Fit and high-DPI
  layouts. Keep HUD/menu scale fixed and ignore wheel input in letterboxing.

## 1.2.0 Enhanced Beta 19 — 2026-10-05

- Remove the 65816 interpreter from the release executable. Execute all
  verified ROM entry points through compiled per-address C while retaining
  connected hot paths, original CPU flags, memory callback order, instruction
  clocks, interrupts and register/save-state layouts.
- Include complete native profiles for the USA, Europe, France, Germany and
  Japan cartridges. PC gameplay enhancements remain guarded to the US ROM.
- Guard the US map-generator, decompressor and classifier helper addresses
  against foreign cartridges, fixing Japanese startup corruption caused by
  coincidentally matching addresses.
- Route construction, hidden test-city field initialization, development
  batches and world helpers through native execution, including the private
  construction bus's money-display return variant.
- Keep the original decoder in explicitly selected reference builds for
  independent comparisons. Diagnose uncovered code rather than silently
  interpreting it, and reject reference builds in release packaging.
- Preserve embedded restored music, runtime files, assets, credits and
  licenses in the single EXE. ROMs and personal saves remain external.

## 1.2.0 Enhanced Beta 18 — 2026-10-04

- Keep the display-resolution map preview visible while NEXT is held, map
  numbers are dirty and the selector enters or exits. Latch the actual panel
  before scanout instead of exposing the coarse native preview when the
  screen state changes. Use nearest sampling throughout the map panel even
  when optional display smoothing is enabled.
- Publish setup-menu glyphs at the frame boundary after their matching sprite
  list is assembled. Main menu, map size and development speed no longer show
  a frame combining the previous list's positions with the next page's text.
- Add F12 LAND GENERATION choices: Native (default), the earlier Procedural
  generator, Islands, Lakes, Rivers and Fractal. Remember the selected style
  for newly generated terrain; preserve loaded cities and the dry-map 31337
  exception. New styles retain fixed feature sizes across expanded maps.
- Keep map-number edits pending across mouse clicks and digit-arrow pairs;
  regenerate once the pointer leaves their shared boundary. NEXT and OK
  retain their explicit refresh/confirmation behavior.
- Display all five map digits throughout screen entry and generation, using
  the original No. label, beveled counter cells and compact range lettering.
- Keep Ctrl+wheel zoom centered on the view in both the city and map preview.
- Fuse validated construction footprint writes, common R/C/I site scans and
  neighbours requiring no transport join. Large successful rectangle fills
  retain original costs, cheats, terrain restrictions, joins and atomic commit.
- Use the cartridge's X button for Back on the map-size and development-speed
  pages, matching the remaining city setup screens. Escape also sends Back.
- Let Enter and the normal confirmation control close full-screen reports and
  bank information. Escape closes informational adviser messages and reports;
  on a bank loan choice it selects No before confirming cancellation.
- Make map number 31337 water-free at all six selectable sizes, preserving
  forests and leaving existing saved terrain unchanged.

## 1.2.0 Enhanced Beta 17 — 2026-10-04

- Restore the original cartridge generator for 120x100, preserving its terrain
  and random-state results. Extend the same river walks, lake/coast brushes,
  forest scatter and shoreline fitting across expanded worlds. Increase
  feature counts and distribute starts across the full dimensions while
  retaining native brush sizes. Existing saved terrain is preserved.

- Keep the mouse menu hand hidden during the title-screen exit fade, whose
  sprite bank still contains logo graphics. Show it when the menu is ready,
  preserving title sprites and the native option arrow in both renderers.

- Extend city zoom-out by another factor of two, with up to 65536 native
  pixels of terrain across the view. The complete 3840x3200 map can fit at
  once in the default widescreen view and Fit to Screen; HUD, menus and
  minimap retain their normal size and terrain keeps nearest sampling.

- Scale extra trains, aircraft, ships and helicopters with expanded-map area:
  one eligible vehicle of each kind per 120x100 district, up to 1,024 of each
  on 3840x3200. Trains follow rails; aircraft require powered airports; ships
  require powered seaports and a clear water footprint. Use independent world
  positions and original cartridge artwork, including per-vehicle train/ship
  headings. Cull outside the viewport and bucket sprites for CPU/GPU rendering.
- Extend map selection to five editable digits, 00000 through 99999. NEXT
  wraps after 99999; digit arrows work with mouse, keyboard and controller.
  Retain the original three-digit seed path; the additional digits extend it.
- Add native-scale clusters of small islands inside irregular lakes and bays.
  Use original water brushes and the native shoreline fitter for their bays.
- Give the far-right map boundary the same 64-canvas-pixel scrolling slack
  as the other edges, including the largest map and zoomed/centered layouts.

- Compose tool-window artwork over the projected city instead of copying
  full-size building pixels from the native backdrop. Keep menu shadows on
  the zoomed terrain and preserve CPU/GPU rendering, budget and adviser pages.
- Clear the setup-page state on the GO TO MENU return path, restoring the
  main menu instead of DEVELOPMENT SPEED. Align RESUME SAVED CITY with the
  other main-menu choices.
- Render map-selection previews at display resolution with nearest sampling
  and coverage-aware downsampling. Ctrl+wheel zooms toward the mouse; middle
  mouse drags the preview. Click the preview to expand it across the window;
  click again or press Esc to return. Keep the native buttons and hand size,
  bound the preview camera, and cache unchanged textures during idle frames.

- Animate the map-select overview: waterways appear first, followed by forest
  patches. Begin the reveal after the native waiting panel; show the entire
  selected map at the original overview size. Mouse NEXT/digit clicks refresh
  the preview on release without requiring a move to OK.
- Give startup, map-size, development-speed and saved-city lists a separate
  native mouse hand with an aligned fingertip. Keep the original selection
  arrow beside the hovered option. Other menu hands follow freely across
  naming, difficulty, gifts and city dialogs. Keep native selection,
  D-pad/menu input and keyboard/gamepad jumps. Real pad input takes ownership
  from an idle mouse. Preserve title lights, map digits and scenario pins;
  the scenario selector gains a separate hand that can enter wide margins.

- Replace per-zone drag-preview rectangles with a clipped shared grid; at
  very distant zoom, show an outline instead of a solid subpixel fill. Cache
  stationary selections so large drags do not rebuild or redraw every zone.
- Run mouse construction in resumable private batches at the safe city-input
  boundary, using compiled native C control flow and mapped world helpers.
  Keep window events, rendering and music responsive and use spare frame-pacing
  time to finish sooner. Publish the complete transaction once, preserving
  original placement rules, costs, joins, gifts and the money cheat.
- Reject unaffordable selections as soon as their cost exceeds funds, without
  modifying the live city. Regression tests compare complete RAM/world results
  against the original ROM with and without the money cheat, including 10,000
  free zones, budgeted slices, cancellation and a million-zone selection.
- Extend the sharp city-entry guard to Practice, normal new cities, Journey,
  scenarios and saved cities. Observe the native black-to-bright fade on every
  guest frame, including skipped fast-forward frames, before revealing terrain.


## 1.2.0 Enhanced Beta 16 — 2026-10-04

- Add a native-font DEVELOPMENT SPEED page after map-size selection: 1x, 3x,
  5x, 10x, 20x and 50x. Store each city's default with its map metadata and
  restore it on load. F12 DEVELOPMENT SPEED defaults to OFF, which uses that
  city's speed; explicit overrides include 3x and 20x and do not change its
  saved default. Older cities use 1x. Journey retains the chosen speed through
  both border expansions.
- Add 3840x3200 terrain, construction, simulation fields, camera/minimap bounds
  and saves. Migrate earlier world records and existing saved Test City 3 data.
  Widen generation's scatter counter and move the larger temporary power bitmap
  off the Windows stack.
- Hold Shift+Tab for four times Tab's frame limit and work budget (up to 24
  guest frames per displayed frame). Actual acceleration depends on workload;
  ordinary Tab keeps its six-frame adaptive pacing.
- Show the original-size navigation minimap while middle/right-button drag
  panning. Project the viewport marker from the free camera and selected zoom,
  preserving native tool captions and avoiding changes to guest OAM.
- Route physical X + arrow keys through the free host camera. Reach the full
  map after mouse panning, retain zoom and stop on stationary holds. Consume
  the shortcut until X is released so releasing arrows cannot place a tool.
  Standalone X, menus and gamepad bindings retain their existing behavior.
- Hold Ctrl for 3x scrolling or Ctrl+Shift for 10x scrolling, including keyboard
  camera movement, mouse edge scrolling and middle/right-button drag panning.
- Keep generated and saved Test City 3's preparation frames black until the
  original entry fade begins. Reveal the completed sharp view with its HUD
  during that fade, avoiding a transient blurry city before the blackout.

## 1.2.0 Enhanced Beta 15 — 2026-10-04

- Update RCI zones in batches distributed throughout the city, including
  Normal development speed. Use a map-size-independent cadence so a fully
  built 1920Ã—1600 city no longer waits for a slow moving sweep to reach
  each district. Each stable zone index completes a pass before repeating.
- Bound development work per frame to keep rendering and mouse input
  responsive. Retain the five requested speed multipliers; on overloaded
  machines, spread pending work across frames instead of blocking input.
- Reuse native road-access results, stagger their refreshes, and invalidate
  them after transport edits or zone capacity transitions. Preserve original growth, demand, power and land-value rules,
  keep the native traffic/census bookkeeping, and leave calendar/budget
  scheduling with the original simulation. Refresh live population at
  Normal speed as well.
- Rebuild the disposable zone index after loading or replacing a city;
  completed development remains in the existing city/save format.

## 1.2.0 Enhanced Beta 14 — 2026-10-04

- Center the main-menu text block and move its selection pointer with it.
  Expand the original panel before its entry-fade upload so it keeps one
  height throughout the fade, rather than growing when the fade ends.
- Align hidden "3. TEST CITY 3" with the two native numbered save rows and
  correct its pointer height. Keep its sprites clear of saved City 2's text.
  Empty save slots show only their numbers, without placeholder names/dates.
- Add 64 display-space pixels of camera slack above and below the city at
  every zoom level, allowing edge tiles to move away from the fixed HUD.

- Make both yearly and toolbar budget mouse controls free-moving, with direct
  arrow/button hit areas, gap rejection and native held-click repeat. Retain
  keyboard/gamepad jumps; an idle mouse no longer overrides their positions.
- Increase maximum terrain coverage from 4096 to 32768 native pixels per
  dimension. Allow the complete 1920Ã—1600 test city to fit inside a widescreen
  view; retain sharp original tile sampling and fixed-size UI.
- Preserve city zoom during keyboard navigation, toolbar popups, gift dialogs
  and Dr. Wright messages. Draw popup artwork at its original size over the
  selected city view instead of switching the land back to native scale.
- Use the free host camera for mouse edge scrolling and retain Ctrl's 3Ã— rate.
- Draw the toolbox from its own BG3 and sprite artwork over zoomed land,
  preventing unscaled city tiles and building roofs from leaking around it.
- Show the original hand cursor throughout the full widened HUD, including
  the space between the left toolbar and right-aligned population/money.
- Extend packed projected sprite coordinates to signed 24-bit values, with
  CPU/Vulkan regression coverage beyond the original 16-bit range.

## 1.2.0 Enhanced Beta 13 — 2026-10-04

- Sharpen terrain zoom on Vulkan by sampling the original tile graphics at
  the displayed resolution, instead of shrinking land into the logical canvas
  and enlarging that result. Keep HUD, toolbox, menus and mouse coordinates
  at their existing scale. Preserve nearest-neighbor pixel edges and immutable
  city/graphics snapshots across tile boundaries; cap the output at 4096 per
  dimension. CPU and unsupported-backend rendering retain their existing path.

- Replace mouse pan's SNES directional scrolling with a free host camera.
  Middle-button drag (and right-button drag over land) applies exact world-pixel
  displacement without a queue or inertia. Keep zoom unchanged, stop instantly
  on stationary holds, preserve fractional movement and reach the full map.
  Hide/capture the pointer during a drag and discard relative-mode transition
  deltas. PAN SPEED and MOUSE SPEED adjust sensitivity. Keep guest camera and
  simulation registers unchanged; loading another city resets the view offset.

## 1.2.0 Enhanced Beta 12 — 2026-10-04

- Convert connected road, bridge and rail upkeep to native C, including
  traffic-dependent artwork, funding and decay, bridge footprint changes,
  train initialization and seaport counts. Preserve original RNG order,
  full-world writes and exact frame/IRQ deadlines; keep one-instruction
  handling at beam boundaries. Full-city state and rendering remain exact.
  Local complete-cycle comparisons show about 11% lower average processing
  time and fewer late frames; occasional frame spikes remain.

- Defer compatibility-memory preparation while direct C simulation helpers
  access city data themselves. Bind the original opcode path when needed;
  retain full-coordinate hooks, beam deadlines and bus-backed sprite/tile work.
- Add experimental independent land tile summaries on Vulkan, including density,
  pollution, occupancy and original branch clocks. Check all four raw source
  tiles before publication; retain ordered native land-value arithmetic,
  exact interrupt boundaries and nonblocking C fallback. Keep the job opt-in
  because production comparisons have not shown a performance improvement.
- Calculate independent crime cells on Vulkan, including police coverage,
  signed bias, native scratch values and branch clocks. Publish results in
  native order, reject changed source samples and retain nonblocking C
  fallback, save/load invalidation and original interrupt boundaries.
- Specialize native density, smoothing, police/fire and zone-update kernels
  for their bounded, atomic and accelerated callers. Preserve original
  clocks and interrupt behavior while reducing repeated execution-mode work.
  Complete-cycle comparisons on the filled 1920x1600 city show about 17%
  lower average processing time; occasional frame spikes remain.
- Keep the occupied-zone count at full width when calculating city centers
  on dense large maps. Resume count carries correctly after saves, and
  recover older saves whose derived city center lies outside the map.
- Refresh the land gutter beside the toolbox during terrain zoom instead
  of retaining old pixels or GPU markers. Preserve native UI in that strip.
- Use the native byte coordinates for terrain-quality updates so unused
  adjacent scratch bytes cannot disable the complete-cell C fast path.
- Batch terrain-quality calculations and reduce repeated scratch/register
  publication in GPU pollution and police/fire coverage passes. Keep exact
  interrupt budgets, field values and final native CPU/save state.
- Connect density, land value, crime, pollution and service-field stages to
  the native city loop, retaining GPU submission and original beam deadlines.
- Connect extra development attempts to shared native RNG, housing and zone
  mutation helpers, preserving attempt order, full map writes and city time.
  Dispatch directly to the owning helper and avoid repeating invariant CPU
  mode checks inside connected mutations; retain stack and deadline checks.
- Compile real caller continuations after the game's five inline-argument
  helpers, covering menu and complete-city simulation paths that previously
  fell back to the CPU interpreter.
- Select ordinary road artwork in one bounded C operation, preserving traffic
  thresholds, full map coordinates, tile invalidation and original clocks.
- Retire fully funded ordinary-road upkeep and artwork together in C. Keep
  decay, bridge and short-deadline paths at their original boundaries.
- Keep road, rail and bridge upkeep in the connected city execution path,
  retaining RNG, live map bindings and interrupt/census boundaries. Calculate
  the bridge-distance probe directly in C when it fits before a beam event.
- Prepare contiguous GPU-owned terrain row spans once per HUD/blanking
  boundary instead of repeating those checks for each projected pixel.
- Reuse exact power-network results across native traversal-policy changes
  when conductivity, generator identities and capacity remain unchanged.
  Preserve refresh timing and ordered brownouts; invalidate both cached
  results after an electrical-network change.
- Update population totals from changed zone centers and their affected
  house neighbors instead of recounting surrounding tile chunks. Retain
  simultaneous-edit, reload and full-width population correctness.
- Compile every phase of the native animated-graphics dispatch table to C,
  retaining DMA writes, live operand checks and per-instruction clocks.
- Compile the native UI-reason, syscall, screen, ending and ordinary-zone
  dispatch tables, including idle handlers. Execute the verified view-cursor
  NOP patch natively instead of falling back to the 65816 interpreter.
- Keep the HUD, minimap, overview panels and menus at their normal display
  scale while zoom changes only the city terrain. Match mouse selection,
  outlines and drag pan to the zoomed land; retain CPU/GPU pixel parity.
- Spread expanded-city zone processing across the map instead of scanning
  visibly from top to bottom. Visit every tile once per pass and persist the
  scan position/order so saved cities resume consistently.
- Keep large-city tile and development work in a connected native C execution
  path, retaining clock, interrupt and census boundaries. Controlled 50x tests
  show an improvement. The final filled-city adaptive-Tab sample averages
  60.14 FPS at X50; occasional late frames remain.
- Open hidden City 3 through the native main-menu fade and Load City setup,
  clearing the old menu before displaying save entries, including empty SRAM.
- Add hidden saved City 3 at 1920x1600, revealed with Ctrl+Shift+tilde on
  Load City (also on the main menu when no regular cities exist). Generate
  a connected, developed city with ordinary zone capacities, power plants,
  transport, police/fire, parks, gifts and civic/coastal facilities.
  Save its native city data and full world in a checked SRM trailer while
  preserving the original two cartridge slots; saved City 3 takes precedence
  over generated code on subsequent loads.
- Double maximum zoom-out terrain coverage to 4096x4096 native pixels,
  retaining fixed UI scale, mouse mapping, CPU fallback and GPU support.

- Compile all 56 indirect city-tool dispatch entries to native C, including
  adviser, budget and gifts. Retire interrupt and idle CPU control natively,
  preserving stack/vector bus order, event clocks and independent controls.
- During Tab fast-forward, keep frame-wait continuations in one native C
  scheduler loop across scanout, HDMA and line boundaries. Retain per-span
  audio/beam retirement, interrupt
  exits, exact entropy counters and a preceding-dispatch control.
- Bypass world operand-plan decoding for irrelevant instructions and banks.
  Preserve unmapped access descriptors, live ROM edits, geometry immediates
  and verified footprint-table exceptions; retain original-path controls.
- Compile world-hook ownership, cache live-validated operand plans, and execute
  common native tile/field coordinate entries directly in C. Retain independent
  original hooks for full-state, pixel and timing comparisons.
- Make Escape open the original Save City slot dialog during city play.
  Keep Escape as Back/Close in menus and dialogs, ignore key repeats, and
  restore city rendering through the native menu transition after saving
  or cancelling.
- Share immutable two-dimensional sprite grids across scanlines and move
  vertical sprite admission to Vulkan. Preserve wrapped OAM, unwrapped
  vehicles, live mid-frame edits, overlapping priority and CPU fallback.
- Generate connected C blocks for UI/driver code, linking ordinary successors
  with direct C control flow. Preserve live target checks, opcode patches,
  host preparation and per-edge event retirement; retain matched controls.
- Connect compiled C UI/driver execution across verified host-hook boundaries,
  preserving per-instruction beam, interrupt, audio and expanded-world mapping
  while avoiding the full gameplay dispatcher between those boundaries.
- Add a compatible ROM-to-C program tier for 33,091 instruction sites across
  the code banks. Preserve live operands, bus order, flags, stack and clocks
  while returning at every host hook/interrupt boundary. Keep patched,
  unresolved and control-state fallback explicit; retain local generation.
- Move supported native Mode 1 background tilemap and CHR decoding into
  Vulkan using immutable scanline VRAM/scroll descriptors. Preserve original
  pixels, relocated HUD/adviser panels, color math and decoded fallback.
- Replace the counted-sprite emitter with direct C record assembly and
  resumable deadline continuations, verified against the original ROM CPU.
- Convert connected traffic/growth decay and transport-total postpasses to
  resumable C, including partial cells, setup, loop control and returns on
  stock and expanded maps. Preserve simulation clocks and field publication.
- Convert the connected 16-bit division driver to C, including interrupted
  operand setup, loop and return; fuse complete iterations into word arithmetic
  while retaining exact original clocks and beam/IRQ deadline behavior.
- Convert city tile-sweep setup, owner/infrastructure dispatch, statistics,
  loop control and returns into a connected C path. Join expanded coordinate
  lookup/return and retain all original clocks, map hooks and tile invalidation.
- Convert both additive RNG drivers to resumable C, including tiny-deadline
  setup/loop stages and the bounded generator's continuations around division.
  Preserve the random sequence, stack shadows and restored caller flags.
- Connect frame-wait setup, deadline continuations and return to a native C
  scheduler lane. Preserve counter entropy, interrupts, beam events and audio
  clocks while avoiding the full gameplay dispatcher during verified waits.
- Move supported native OBJ pixel decoding and priority composition to Vulkan
  through immutable scanline sliver descriptors. Keep original OAM selection
  and hardware overflow flags; reconstruct only CPU-requested sprite blocks.
  Intermediate Tab frames retain sprite state without constructing unused
  pixel rows. Preserve held-map snapshots and over-capacity/layout fallback.
  Cache vertical OAM membership in original rotated order, checking live OAM
  and size/priority state each line so DMA and host edits invalidate it.
- Validate the filled 1920x1600 city at X50 with adaptive Tab pacing: 60.14
  FPS across 3,174 warm frames, p99 active work 14.64 ms and four frames over
  16.7 ms. The complete-city comparison matches all 13,466,232 integer state
  bytes and rendered pixels, with zero main/kernel interpreter calls on that
  replay. All 19 restored tracks play with zero music-worker failures.

Remaining limits: occasional frame spikes remain; these measurements apply to
this tested PC/workload rather than every machine or game path. Physical
hardware pinch delivery remains unverified, and the yearly black budget
popup report remains deferred.

## 1.2.0 Enhanced Beta 11 — 2026-10-03

### Performance and rendering

- Vulkan presentation and compute share one SDL GPU device. Terrain, roofs,
  power warnings, native city raster/repair, extended sprites and BG3 use GPU
  composition with CPU fallback. Shaders are embedded; no shader SDK is needed.
- Native C kernels replace substantial interpreted work in development,
  transport, power traversal, density, services, spatial smoothing, tile lookup,
  arithmetic and frame waiting. Connected RCI growth/decline and mature mergers
  now use the verified C control pipeline by default on expanded maps.
- Incremental population census and region revisions avoid repeated whole-map
  scans when city tiles have not changed. Power reconnection and accelerated
  development preserve the chosen multiplier and original calendar schedule.
- Repair stale scrolling tiles, native map staging and pointer/HUD composition.
- Dedicated SPC/DSP music thread; restored stereo music at 44.1 kHz with the
  original sound effects. Music does not follow game-thread stalls.

### Maps, population and building tools

- Add full **1920Ã—1600** maps alongside 120Ã—100, 240Ã—200, 480Ã—400 and 960Ã—800.
  Move the map-size heading, choices, selection arrow and mouse hitboxes down
  16 native pixels inside their original in-game panel.
  Keep the chosen size highlighted throughout the exit fade.
- Calculated, displayed and saved population supports **9,999,999,999,999**.
- Scale the city-center influence radius with expanded map dimensions.
- Copy/Paste includes whole intersected ordinary buildings, roads, railroads,
  power lines, crossings, bridges, parks and terrain. Special/gift buildings
  are excluded. Show the authentic total price and moving outline, and select
  Paste automatically. Rough selections recover whole ordinary footprints.
- Ctrl+wheel zoom and touchpad pinch event support. Fit to Screen increases
  visible land while retaining tile scale. X navigation arrows use view edges.

### Mouse and menus

- Middle-button drag moves land with the mouse at 3Ã— speed, hides the pointer
  and uses relative capture so panning continues past the screen edge.
- Correct HUD mouse alignment while Copy/Paste is selected.
- Coal and nuclear plants support multiple placements in one drag.
- Mouse clicks operate F12 options; Left decreases and Right increases.
- Gift choices and single-gift messages accept centered mouse input. Toolbox
  gift hitboxes cover the full 32Ã—32 images.
- F12 **MUTE CITY WARNINGS** suppresses crime, traffic and pollution banners
  and adviser messages without changing their simulation. Defaults to OFF.

### Distribution and validation

- Single portable Windows EXE bundles runtime libraries, assets, all 19 restored
  tracks, documentation and credits. It unpacks a versioned private cache;
  the player's ROM remains separate. Saves/settings remain beside the portable
  EXE. Existing saves and prior installations are not replaced.
- Add consolidated credits, this changelog and the soundtrack import tool.
- ROM-backed tests cover native execution state, cycles, interrupt yields,
  map-size menu/font, construction, clipboard, population and power. Paired
  filled 1920Ã—1600 Fit/50Ã— CPU and Vulkan replays compare states and images.
  Detailed measurements and fallback scope: [GPU_PERFORMANCE.md](docs/GPU_PERFORMANCE.md).

Remaining limits: complete interpreter removal and sustained 60 FPS in every
heavy phase are unfinished. Physical touchpad pinch delivery still needs
hands-on testing. The reported black yearly budget popup remains deferred.

## Earlier enhanced releases

- [Beta 10](https://github.com/kandowontu2/UrbanRecomp/releases/tag/v1.2.0-enhanced.10):
  960Ã—800 maps, native size selection, saved-city geometry/power recovery,
  Fit to Screen and rendering performance work.
- [Beta 9](https://github.com/kandowontu2/UrbanRecomp/releases/tag/v1.2.0-enhanced.9):
  Windows launch without a console.
- [Beta 8](https://github.com/kandowontu2/UrbanRecomp/releases/tag/v1.2.0-enhanced.8):
  map updates, mouse save/gift/confirmation menus.
- [Beta 7](https://github.com/kandowontu2/UrbanRecomp/releases/tag/v1.2.0-enhanced.7):
  Huge-map simulation and smoother adaptive Tab fast-forward.

Full prior release notes and upstream history remain in
[GitHub Releases](https://github.com/kandowontu2/UrbanRecomp/releases) and Git.
