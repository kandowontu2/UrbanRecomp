URBAN RECOMP ENHANCED BETA 14
===========================================

Run the single portable EXE and choose your own clean US SimCity SNES ROM.
The runtime, assets, documentation, credits and all 19 restored songs are
embedded. They unpack into a versioned private cache under LOCALAPPDATA.
Saves and settings stay beside the portable EXE; existing installations are
not replaced. No ROM or personal saves/settings are included.

Enhanced fork: https://github.com/kandowontu2/UrbanRecomp
Original project: https://github.com/blackerking/UrbanRecomp

Latest additions: much farther terrain zoom-out, with HUD, toolbox and menus
kept at their normal size. Zoom remains unchanged during edge/keyboard
scrolling, toolbar popups, gifts and Dr. Wright messages. The original hand
appears throughout the widened HUD. Mouse budget controls move freely;
keyboard/gamepad navigation retains its original jumps.
Main-menu text is centered and its frame stays the same size during the fade.
Hidden 3. TEST CITY 3 and its pointer align with the numbered save rows;
empty slots show only 1. or 2. Panning has extra top/bottom space at every zoom.

Also included: Vulkan presentation/compute, native C simulation kernels,
full 1920x1600 maps, population up to 9,999,999,999,999, whole-building
Copy/Paste with original-font price/preview, Ctrl-wheel/pinch zoom, threaded
restored music and continuous middle-button drag pan. Gift menus and F12
mouse clicks work; Left decreases and Right increases. The map-size page's
text, selection arrow and hitboxes are inset farther down in the panel.
F12 MUTE CITY WARNINGS suppresses crime, traffic and pollution messages
without altering their simulation; it defaults to OFF.

Select map size before starting a city/Practice: 120x100, 240x200, 480x400,
960x800 or 1920x1600. Journey starts at Normal and expands at 100,000 and
1,000,000 residents, with Dr. Wright celebrations. F12 development speed
is Normal, 2x, 5x, 10x or 50x; calendar/budget scheduling remains normal.
Fit to Screen increases visible land at the chosen tile scale.
Ctrl+wheel zoom-out now exposes terrain spanning up to 32768x32768 native
pixels, enough to fit the entire 1920x1600 test city in a widescreen window.

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

Tab          fast-forward (hold)
Escape       Save City during play; Back/Close in menus
Ctrl+Shift+tilde  reveal hidden test City 3 on the load-city page
Ctrl         3x keyboard/edge scrolling (hold)
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
