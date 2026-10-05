URBAN RECOMP ENHANCED BETA 18 - SHARP PREVIEWS AND TERRAIN STYLES
=============================================================

Run the single portable EXE and choose your own clean US SimCity SNES ROM.
The runtime, assets, documentation, credits and all 19 restored songs are
embedded. They unpack into a versioned private cache under LOCALAPPDATA.
Saves and settings stay beside the portable EXE; existing installations are
not replaced. No ROM or personal saves/settings are included.

F12 LAND GENERATION offers Native (default), Procedural (the earlier
generator), Islands, Lakes, Rivers and Fractal. The saved choice applies
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

Beta 17 restores the original cartridge terrain generator for 120x100.
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
the view's center, in both the city and preview; middle mouse drags.
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

Beta 16: X + arrow keys uses the free camera over the full map,
including after mouse panning. Zoom stays unchanged; movement stops when the
arrows stop. Ctrl scrolls at 3x; Ctrl+Shift scrolls at 10x. Both also apply to
mouse edge scrolling and drag panning. Test City 3 now stays black during
preparation and fades in with the sharp finished view and fixed-size HUD.

Beta 15 additions: city development now runs in bounded batches distributed
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
The default is saved per city. F12 OFF uses that city's default; X1/X2/X3/X5/
X10/X20/X50 temporarily override it. Calendar/budget scheduling remains normal.
Older saves load at 1x and migrate automatically when saved with this build.
Mouse drag panning shows the original-size minimap with the live camera marker.
Fit to Screen increases visible land at the chosen tile scale.
Ctrl+wheel zoom-out now reaches twice as far, exposing terrain spanning up
to 65536 native pixels across the view. The entire 3840x3200 map can fit in
the default widescreen view and Fit to Screen. HUD and menus stay the same size.

Hidden TEST CITY 3: press Ctrl+Shift+tilde on Resume Saved City to reveal
the 1920x1600 developed test city. With no saved cities, the same shortcut
works on the main menu. It starts with about 62 million residents at normal
zone capacities, connected power/road/rail networks, police/fire, parks,
gifts, stadium, airports and coastal seaports. The usual simulation applies.
Select City 3 to load its SRM record or generate it if no record exists.
Escape opens Save?; choose Yes to save City 3 without replacing City 1 or 2.
The hidden city's full map is appended inside the SRM. Keep that entire file
when backing it up. Long-term maximum population is still being tested.

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

Tab          fast-forward (hold); Shift+Tab requests 4x the usual boost
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
F12 / F10    settings menu: development speed, fit to screen, GPU terrain,
             cheats, disaster triggers,
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
UrbanRecomp.exe starten. F10 öffnet das Einstellungsmenü. Deutsch: deutsche
ROM dazulegen und "python tools/make_translations.py de" ausführen, dann im
Launcher die Sprache wählen. Gespeicherte Städte liegen in
urbanrecomp-us.srm.
