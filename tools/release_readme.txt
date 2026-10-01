URBAN RECOMP ENHANCED - 1.2.0 BETA 1
===================================

Urban Recomp runs the Super Nintendo city builder SimCity (1991) natively on
Windows, with widescreen, a launcher, mouse control and an in-game settings
menu. It is an unofficial fan project, not affiliated with Electronic Arts,
Maxis or Nintendo. It contains no game data: you need your own copy of the
US cartridge.

Enhanced fork, documentation and issues: https://github.com/kandowontu2/UrbanRecomp
Original project and credits: https://github.com/blackerking/UrbanRecomp

F12 development speed: Normal, 2x, 5x, 10x and 50x, with faster power refresh
when accelerated. Calendar and budgets keep their normal schedule.
64-bit calculated population up to 9,999,999,999, playable 240x200 maps
(four times the area), full-widescreen mouse construction, retained off-screen
drags, and corrected widescreen HUD/minimap placement. See PC_ENHANCEMENTS.md.
These features require the verified clean US ROM and the interpreter build.
This first enhanced build is a prerelease; hands-on gameplay testing is ongoing.


START
-----

1. Put your US ROM (.sfc or .smc, any file name) into this folder.
2. Start UrbanRecomp.exe.

The launcher lets you pick the ROM and set window size, fullscreen,
widescreen, language, the Sylt scenario and the keys. It needs OpenGL 3.3;
without it the game starts directly with the saved settings
(sc-settings.ini).


CONTROLS (default keys, change them in the launcher)
--------

D-pad        Arrow keys
A / B        S / X        (mouse: right / left button)
X / Y        A / Y
L / R        Q / W
Start        Enter
Select       B

Tab          fast-forward (hold)
+ / -        zoom the map in the city view
Shift+1..0   save state to slot 1-0, 1..0 load it
F3           mouse moves the game cursor
F9           fast cursor
F12 / F10    settings menu: development speed, large maps, comfort options,
             cheats, disaster triggers,
             save states -- the game pauses while it is open


SAVING
------

Cities saved in the game are kept in urbanrecomp-us.srm in this folder, a
plain SRAM image like other SNES emulators write. The file as it was at the
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
