URBANRECOMP ENHANCED 1.0.2
==========================================================

Enhanced 1.0.2 adds varied Mars rocks, Arctic snow mounds, sparse Desert
vegetation and fires accompanying Basalt lava floods.
Enhanced 1.0 is versioned separately from the upstream UrbanRecomp engine.
Downloads: https://github.com/kandowontu2/UrbanRecomp/releases/tag/enhanced-v1.0.2

Run the single portable EXE and choose your own clean US SimCity SNES ROM.
The runtime, assets, documentation, credits and restored soundtrack are
embedded. They unpack into a versioned private cache under LOCALAPPDATA.
Saves and settings stay beside the portable EXE; existing installations are
not replaced. No ROM or personal saves/settings are included.

At 10 million population, Dr. Wright announces Megagopolos once per city.
Its new city theme is Markify's restored unused Vanilla Lake beta song from
Super Mario Kart. Menu and disaster songs keep their existing assignments.
The reward is saved with each city, and music stays at real-time tempo.
At 100 million, Gigagopolois adds a second Dr. Wright announcement and changes
the city theme to LOOP16B from LOOP816 (2023), by Soyo Oka, artist/composer/publisher.

The game CPU now executes compiled C without a 65816 interpreter in the
release executable. Save/register layouts and device timing are preserved.
Budgets, reports, construction, hidden-city generation, save/load and large
city work were checked against the original CPU. NATIVE_EXECUTION.md and
GPU_PERFORMANCE.md contain build controls, validation and performance limits.

Land-type graphics keep the original terrain textures, connected forest/shore
edges and water animation, with deliberate soil, water/lava and canopy palettes.
No noisy repeated replacement patterns or cut-up forest tiles remain.
The seven existing custom themes have seasonal colors driven by the saved game month,
blending between winter, spring, summer and autumn. Tropical themes stay evergreen,
lava stays hot, and seasonal ice is visual. Pan minimaps follow the current season;
new-map previews show January. Native cartridge seasons remain unchanged.
Moon adds gray lunar terrain with stable year-round colors and no naturally
generated forests. Football stadiums on Mars, Venus and Moon have dome roofs;
their gameplay behavior remains unchanged. Short city notices retain solid
paper, readable text and intact borders over zoomed terrain.

Hidden Test City 3 now asks for map size and development speed when unsaved.
All six sizes start with empty powered R/C/I zones on water-free map 31337,
connected rail/roads, services, parks, 27 distributed gifts, a stadium, airport
and dry-land port. Industry is outside, commerce buffers it, and housing has
local jobs and parks. The placement guide covers every tool and gift.
Its layout follows native transport, growth and employment rules. The largest
layout's ordinary capacity is 511,159,040; this is not a measured stable peak.
Existing City 3 saves load directly and City 1/2 slots remain intact.
PLACEMENT_EFFECTS.md and TEST_CITY_LAYOUT.md explain the source calculations,
natural ten-year measurements and practical limits. The native 120x100 test
reached 310,880 after ten years with no completely empty housing; this is
not a proof of a stable maximum. Existing saved test cities retain their layout.

The calendar now reaches year 999999 with no leading zeroes. The HUD and
budget show the complete year; full years persist in city and snapshot saves.
Existing earlier saves are imported with their native date.

Shift-click the toolbox ? button for an all-15-gifts debug picker, even when
it is dimmed. Click a gift or choose with arrows/Enter and place it normally.
Queued earned gifts are preserved.
The actual Disasters panel has temporary NUKE and UFO buttons in a third row.
Select one and close the panel to run the attack. A nuclear plant is required
for NUKE. Explicit UFO/meltdown commands also work
with NO DISASTER enabled. Automatic disasters keep their normal restrictions.

Held Tab advances six full simulation frames per display update; Shift+Tab
advances 24, and Ctrl+Shift+Tab advances 96. Development, the calendar, demand, services and vehicles speed
up together, while restored music keeps normal tempo. Busy cities no longer
silently truncate fast-forward to one frame. Actual acceleration depends on
city workload and hardware; the title measures speed against the native clock.
Large-map arithmetic/electrical work stays in a connected simulation loop,
with bounded electrical zero-clock batches reducing scanline-boundary overhead.

The map-selection screen has GENERATION arrows for choosing the terrain type.
Changing generation or land type replaces the complete preview and colors
together, retaining preview zoom/pan instead of clearing and rebuilding it.
Click the arrows (or type), or move Up from NEXT and use Left/Right. Down
returns to NEXT. Enter/the confirm button also activates the selected arrow.
The map-screen GENERATION selector offers Native (default), Procedural (the earlier
generator), Islands, Lakes, Rivers, Fractal, Continent, Delta and Atolls. The saved choice applies
to newly generated terrain; existing cities keep their land. Map 31337
remains water-free with every style and size.
Map-number arrows allow repeated clicks across all five digits without
regeneration until the mouse leaves their shared boundary. Five digits
use the original font, No. label and beveled counter from screen entry.
The display-resolution preview stays sharp while NEXT is held, during
number editing, generation and setup transitions. The visible map panel
uses nearest sampling even when optional display smoothing is enabled.
Main menu, map size and development speed publish their new lettering
and sprite positions together, eliminating the one-frame text flash.
Large successful zone fills use fused native construction loops. Costs,
money cheats, terrain restrictions and transport joins remain intact.

The mouse hand waits for menu graphics during the title-screen exit fade,
preventing a corrupted logo tile from appearing at the pointer after a click.

Native generation retains the original cartridge terrain generator for 120x100.
Expanded maps extend its actual river walks, lake/coast brushes, forest
scatter and shoreline fitting across the full world, adding more features
while keeping their original tile sizes. Rivers continue across map bounds;
completed 120x100 map images are not tiled. Small island bays use original
water brushes and shoreline artwork. Saved terrain is preserved.
Five map-number digits offer
00000 through 99999, with mouse/pad arrows and NEXT wrapping after 99999.
The far-right edge now has the same extra panning space as the other borders.
Larger maps add trains, aircraft, ships and helicopters by 120x100 districts,
up to 1,024 extra vehicles of each kind on 3840x3200. Rails, powered airports,
and powered seaports with navigable water determine where they can appear.
Their independent positions and headings use your cartridge's original art.
Map previews draw sharply at display resolution. Ctrl+wheel zooms around
the mouse pointer, in both the city and preview; middle mouse drags.
Click the preview to expand it across the
window, then click again or press Esc to return to map selection.
Tool-window shadows stay over the zoomed city without full-size building
fragments. GO TO MENU restores the main menu, and Resume aligns with the
other choices. Preview textures are cached while the image is unchanged.
Startup/setup/load lists show a separate native mouse hand and retain the
arrow beside the hovered option. Keyboard/gamepad uses native selection jumps.
Also included: cheaper large drag previews, responsive batched construction
(including the money cheat), and sharp city entry across start/load paths.
Very large selections still require processing time; they no longer block
window events throughout placement.

Enhanced fork: https://github.com/kandowontu2/UrbanRecomp
Original project: https://github.com/blackerking/UrbanRecomp

X + arrow keys uses the free camera over the full map,
including after mouse panning. Zoom stays unchanged; movement stops when the
arrows stop. Ctrl scrolls at 3x; Ctrl+Shift scrolls at 10x. Both also apply to
mouse edge scrolling and drag panning. Test City 3 now stays black during
preparation and fades in with the sharp finished view and fixed-size HUD.

City development runs in bounded batches distributed
across districts at every development speed, including Normal. Large cities
no longer wait for a row-by-row sweep to reach their neighborhoods. Original
growth, demand, power and land-value rules remain active; calendar and budget
scheduling are unchanged. Transport-access caches refresh at staggered times
and invalidate after transport edits or zone capacity changes. Live population
also refreshes at Normal speed. Completed changes use the existing save format.
Work is limited per frame to keep input responsive; overloaded machines can
fall below the requested development rate. Large-city frame spikes remain.

Also included: Vulkan presentation/compute, native C simulation kernels,
full maps up to 3840x3200, population up to 9,999,999,999,999, whole-building
Copy/Paste with original-font price/preview, Ctrl-wheel/pinch zoom, threaded
restored music and continuous middle-button drag pan. Gift menus and F12
mouse clicks work; Left decreases and Right increases. The map-size page's
text, selection arrow and hitboxes are inset farther down in the panel.
F12 MUTE CITY WARNINGS suppresses crime, traffic and pollution messages
without altering their simulation; it defaults to OFF.

Select map size before starting a city/Practice: 120x100, 240x200, 480x400,
960x800, 1920x1600 or 3840x3200. Journey starts at Normal map size and expands
at 100,000 and 1,000,000 residents, with Dr. Wright celebrations.
The DEVELOPMENT SPEED page after map size offers 1x, 3x, 5x, 10x, 20x and 50x.
The selected speed is saved per city. Calendar/budget scheduling remains normal.
Older saves load at 1x and migrate automatically when saved with this build.
Mouse drag panning shows the original-size minimap with the live camera marker.
Fit to Screen increases visible land at the chosen tile scale.
Ctrl+wheel zoom-out now reaches twice as far, exposing terrain spanning up
to 65536 native pixels across the view. The entire 3840x3200 map can fit in
the default widescreen view and Fit to Screen. HUD and menus stay the same size.

All 19 restored PCM tracks play with their authored loops and native sound
effects. Credits: Pinci / Church of Kondo (restoration), Relikk (PCM set).
Full project/component credits: CREDITS.md and THIRD_PARTY_NOTICES.md.
Change history: CHANGELOG.md. Software licenses are in the licenses folder.
Run the portable EXE with --portable-docs to open the embedded documents,
or --portable-extract <folder> to extract the full bundle for inspection.

The filled 1920x1600 X50 city averages 60.14 FPS with adaptive Tab pacing
on the tested PC. Native C road/rail/bridge work reduces average processing
time by about 11% in local comparisons. Complete-city state/pixels match,
with zero main/kernel interpreter calls on that replay. Occasional late
frames and compatibility fallback on unsupported paths remain.
Touchpad hardware delivery needs hands-on testing. The reported yearly
black budget popup remains deferred. See GPU_PERFORMANCE.md for evidence.

START
-----

1. Start the portable EXE.
2. Select your own US ROM (.sfc or .smc, any file name) in the launcher.

The launcher lets you pick the ROM and set window size, fullscreen,
widescreen, language, the Sylt scenario and the keys. It needs OpenGL 3.3;
without it the game starts directly with the saved settings
(sc-settings.ini).


CONTROLS (default keys, change them in the launcher)
--------

D-pad        Arrow keys
A / B        S / X        (mouse: right Back / left Select in menus)
X / Y        A / Y
L / R        Q / W
Start        Enter
Select       B

Tab          simulation fast-forward (hold), 6x target; Shift+Tab 24x; Ctrl+Shift+Tab 96x
Escape       Save City during play; Back/Close in menus
Ctrl+Shift+tilde  reveal hidden test City 3 on the load-city page
X + arrows   pan the free city camera
Ctrl         3x scrolling/panning (hold)
Ctrl+Shift   10x scrolling/panning (hold)
+ / -        zoom the map in the city view
Ctrl+wheel   zoom (touchpad pinch events also supported)
Middle mouse hold and drag to pan; pointer is hidden and captured
Shift+1..0   save state to slot 1-0, 1..0 load it
F3           mouse moves the game cursor
F9           fast cursor
F12 / F10    settings menu: fit to screen, GPU terrain, input options, cheats,
             save states -- the game pauses while it is open


SAVING
------

Cities saved in the game are kept in urbanrecomp-us.srm in this folder, a
SRAM image with an optional checked hidden-city trailer. The file as it was at the
last start is kept as urbanrecomp-us.srm.bak. Save states are separate files
(savestate_<digit>.bin) and do not change your saved cities.

Keep urbanrecomp-us.srm.population and urbanrecomp-us.srm.world beside the
SRAM file: they store expanded population/history and large-map data for the
two in-game city slots. Copy all three files together when moving your saves.
Legacy saves remain loadable. Older upstream builds cannot preserve this
fork's extended values or full large-map world; back up saves before switching.


GERMAN AND FRENCH
-----------------

The translations are built from your own German or French cartridge, so they
cannot be shipped. With Python 3.9+ and Pillow (pip install pillow), and the
US ROM plus the German or French ROM in this folder:

    python tools/make_translations.py de
    python tools/make_translations.py fr

Then choose the language in the launcher.


LICENCE
-------

Urban Recomp: MIT (LICENSE.txt). The program includes snesrecomp, which is
licensed PolyForm Noncommercial 1.0.0, so this package may be used and passed
on for noncommercial purposes only. All third-party licences:
THIRD_PARTY_NOTICES.md and the licenses folder.


URBAN RECOMP (DEUTSCH, KURZ)
----------------------------

Eigene US-ROM (.sfc/.smc, beliebiger Name) in diesen Ordner legen und
UrbanRecomp.exe starten. F10 Ã¶ffnet das EinstellungsmenÃ¼. Deutsch: deutsche
ROM dazulegen und "python tools/make_translations.py de" ausfÃ¼hren, dann im
Launcher die Sprache wÃ¤hlen. Gespeicherte StÃ¤dte liegen in
urbanrecomp-us.srm.

Land types: Native, Basalt (lava), Amazon (extra forests), Desert, Mars, Venus,
Arctic, Swamp and Moon. Select LAND TYPE below GENERATION before starting a city;
terrain graphics stay with the saved city. Water/forest construction rules apply.
Four difficulty levels include Super Hard, which starts with Medium's funds
to offset its increased disaster rate. Starting funds double at each larger
map-size step; setup and confirmation show the scaled amounts.
