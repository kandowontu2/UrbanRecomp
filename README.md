# Urban Recomp

<p align="center"><img src="assets/urbanrecomp_logo.png" alt="Urban Recomp" width="320"></p>

Urban Recomp is a static recompilation of the Super Nintendo release of
*SimCity* (1991) onto [snesrecomp](https://github.com/mstan/snesrecomp),
the same general-purpose 65816-to-C framework used by
[MegaManXSNESRecomp](https://github.com/mstan/megamanxsnesrecomp) and the
other snesrecomp game repositories. Everywhere else in this repository it
is simply "the game".

> **Unofficial fan project.** Urban Recomp is not affiliated with, endorsed
> by or sponsored by Electronic Arts, Maxis or Nintendo. SimCity is a
> trademark of Electronic Arts Inc., and Super Nintendo is a trademark of
> Nintendo; they are named only to say which cartridge this project works
> with. The repository contains no ROM data and no game artwork: you must
> supply your own legally obtained copy of the US cartridge. Any file name
> works -- the launcher lets you pick the file, and the tools recognise it
> by its contents.

**Enhanced fork:** this repository builds on
[blackerking/UrbanRecomp](https://github.com/blackerking/UrbanRecomp), preserving
its history, credits and licenses. It adds per-city development speeds (1x,
3x, 5x, 10x, 20x, 50x) with an optional F12 override, faster refresh of changed
power networks, 64-bit population accounting up to 9,999,999,999,999, map
choices through 3840x3200, Journey mode
with population-triggered border expansions, Copy/Paste, zoom and mouse drag
panning. Vulkan composition and native C simulation kernels accelerate large
cities. Music runs on its own thread, with the restored 19-track set bundled
in the portable release. Widescreen status and navigation controls align with
the wider city view. See the [changelog](CHANGELOG.md), [credits](CREDITS.md) and
[performance evidence and remaining limits](docs/GPU_PERFORMANCE.md).

Download the Windows x64 package from
[this fork's releases](https://github.com/kandowontu2/UrbanRecomp/releases).
Beta 17 is one portable Windows EXE; run it and select your own clean US ROM.
Bundled files unpack into a versioned private cache. Saves/settings stay beside
the portable EXE. `--portable-docs` opens the bundled credits and licenses.
The enhancements require the verified US ROM and the supported host execution
path. Native C simulation kernels retain compatibility fallbacks. Beta 17
distributes development across city districts at Normal
speed as well; its time allowance keeps development work bounded per frame.
See the enhancement notes for current behavior and performance limits.
Beta 17 restores original 120x100 terrain and extends its generator to larger
maps at native feature scale. It also adds map numbers 00000–99999,
sharp previews with zoom and drag panning, larger vehicle fleets, responsive
large construction, and zoom-out sufficient to show the complete 3840x3200
map. It also fixes title-click cursor corruption and tool-window backdrops.
Enhanced releases are prereleases; see
[features, controls, save compatibility and testing limits](docs/PC_ENHANCEMENTS.md).

**Adaptive Widescreen, the default renderer:** the shared Mods launcher offers
Fit to window, Fit height, Fit width, and 4:3 through 32:9 presets (21:9 out of
the box) while keeping the full original view visible; switching it off there
returns to the classic widescreen. See [build/run instructions and renderer scope](docs/ADAPTIVE_RENDERER.md)
and [the dependency audit](docs/DEPENDENCY_AUDIT.md). The branch retains current
main's engine pin and host fixes; controls default to the top left.

**Just want to build and play it?** See [SETUP.md](SETUP.md) for a
Windows/Linux quick-start. Contributors should read
[CONTRIBUTING.md](CONTRIBUTING.md) instead. For a consolidated map of
everything reverse-engineered about this ROM so far (WRAM variables,
named routines, patch sites, compressed data regions), see
[docs/ROM_MAP.md](docs/ROM_MAP.md).

**Open threads:** [`docs/OPEN_QUESTIONS.md`](docs/OPEN_QUESTIONS.md) lists what is
left to investigate, with the evidence already gathered for each.

## How it runs

The current local build also offers F12 LAND GENERATION styles: Native,
Procedural (the earlier generator), Islands, Lakes, Rivers and Fractal.
Native remains the default; the choice applies to newly generated maps.
Five-digit map-number editing waits until the mouse leaves the arrow area
before regenerating. Ctrl+wheel zooms around the view's center, and fused
native construction loops accelerate large successful rectangle fills.
Setup-menu lettering changes together with its sprite list to avoid flashes
of mismatched text between pages.

Enhanced Windows releases run compatible ROM-generated C alongside connected
native C simulation routines over snesrecomp's PPU, APU, DMA and cartridge
device models. The host retains original H/V clocks, interrupts and bus order;
Vulkan shares presentation and compute for supported graphics and derived
city fields. Unsupported execution/layouts retain compatibility fallback.
Generated game code and the player's ROM remain private build inputs.

The earlier interpreter and optional AOT/fiber bring-up are documented in
[`docs/AOT_LLE.md`](docs/AOT_LLE.md). Enhanced builds use `SC_PROGRAM=ON` and
`SC_AOT=OFF`; contributor generation instructions are in
[CONTRIBUTING.md](CONTRIBUTING.md).

**No full disassembly.** This project will not produce a complete, labelled
disassembly of the game. Another fan project already works on that:
[Vitor Vilela's SA-1 version](https://www.patreon.com/vitorvilela/posts/simcity-sa-1-112786310).
The original project developed its ROM research independently. This enhanced
fork follows mouse behavior publicly described in
[Vitor Vilela's SA-1 Beta 2 post](https://www.patreon.com/vitorvilela/posts/simcity-sa-1-2-168886217).
No SA-1 patch bytes or third-party ROM data have been imported. Vitor Vilela,
or anyone else, is welcome to bring the improvements made here into their own
work, subject to the licenses of the relevant components.

### Graphics/text export tool

`tools/extract_graphics.py` decompresses and exports the game's font
tileset, scenario tileset, and every advisor/scenario dialog text block
(Nintendo's LC_LZ5-style compression, used throughout this ROM) as PNG
and raw `.bin` files, for mod support. Needs Pillow (`pip install
Pillow`); run with `python tools/extract_graphics.py` from the repo
root once your ROM is in the repository root (any file name). Output goes to
`extracted_assets/` (gitignored -- it's derived from the copyrighted
ROM, so it's never committed). Verified against this project's own ROM:
every dialog text block decompresses and renders as readable English
text.

### Scenario map export tool

`tools/extract_maps.py` decodes every scenario map out of the ROM to
`extracted_assets/maps/` — a raw 24000-byte `.bin` in exactly the layout
the game keeps live at `$7F0200` (120×100 cells, one little-endian 16-bit
tile index each), plus a false-colour `.png` preview. No dependencies; run
`python tools/extract_maps.py` from the repo root with your ROM in the
repository root (any file name). The format is documented in
[`docs/REFERENCE_map_format.md`](docs/REFERENCE_map_format.md).

The last two entries are free play and the tutorial map. Both place zero
buildings, where every real scenario stamps 200+ — the tell that they're
maps you start on rather than cities you inherit. Free play's terrain is
drawn as Mario's face, which is worth knowing before assuming a decode
has gone wrong.

### Post-load power dropout (stock-ROM bug, fixed)

After a load the whole city reads as unpowered for several seconds, and
since the decline logic runs during that window, loading a game actively
costs you population.

Found and fixed by **Truttle1** (<https://www.youtube.com/@Truttle1>),
whose patch is what identified **bit 15 (`$8000`) of each 16-bit map
cell** as the power bit. Measured here to confirm it: loading a scenario
and sampling every 60 frames, powered cells sit at **0 for the first
~400 frames**, then jump to 2888 and settle at 3043 — a ~6.7-second
window with nothing powered.

`FIX POWER ON LOAD` in the F10 menu (**on by default**) marks every cell
powered once the map is in place at `03:c8dd`, letting the game's own
power scan clear whatever is genuinely unpowered on its next pass.

Implemented host-side in C rather than by porting Truttle1's bytes — this
repo doesn't vendor third-party work, and doing it from C needs no free
ROM space. It also avoids a quirk of that patch: because it replaces
`STZ $003a ; RTS` with a 4-byte `JSL`, its `RTL` lands on `03:c8e1` and
runs the SRAM loader a second time (harmless — an idempotent copy that
doesn't touch `$7F0200`, so the power bits survive).

The trigger point `03:c8dd` is **confirmed**: a bsnes exec breakpoint
there fires on loading a saved game, reached via `00:c845` → `03:c8a0`.
This project's own headless harness still cannot drive that path (the
mode-freeze technique doesn't reach it), so the confirmation is from
bsnes, not from `--qualify`.

**Root cause**, found by a bsnes write breakpoint on `$7F0200`: the
routine that makes bit 15 mean "powered" is `03:b152`, which walks all
12000 cells applying a **packed power bitmap held at `$7FA598`** (one bit
per cell, `AND #$7fff` then conditionally `ORA #$8000`). That bitmap is
*not* part of the SRAM save block — the load path restores `$7F5FC0` and
`$7F6560` but nothing at `$7FA598` — so after a load it has to be
recomputed from scratch, and until it is, every cell reads unpowered.
Hence the dropout, and hence why "assume powered until the real scan says
otherwise" is the right shape of fix: `03:b152` corrects it on its next
pass either way.

## Launcher and settings

Started without a ROM argument, the game opens the shared
[recomp-ui](https://github.com/RetroPortingToolKit/recomp-ui) launcher first
(the `recomp-ui` submodule): ROM choice, window size, fullscreen, filtering,
audio, **Widescreen** (Display), **Language** -- English, Deutsch, Francais
(Localization), the **Sylt** scenario (Mods), and the keyboard bindings
(Controller). The choices are saved to `sc-settings.ini`, the bindings to
`keybinds.ini`, both next to where the game runs.

- The ROM is found by its contents, never by its name. Without a ROM
  argument and without the launcher, the game uses the one saved in
  `sc-settings.ini`, else the first `.sfc`/`.smc` file in the working
  directory that is the US image; `SC_LANG=E|F|G|J` picks another region's
  image the same way.
- `--launcher` shows the launcher even with a ROM argument, or after "skip
  the launcher" was ticked.
- Every run that is not `--qualify` applies `sc-settings.ini`: widescreen,
  language and Sylt become `SC_WIDESCREEN`, `SC_TRANSLATION` /
  `SC_PACKET_PATCH` and `SC_NINTH`, each only where the variable is not
  already set by hand. `--no-settings` ignores the file; `--qualify` runs
  always do, so tools and comparisons keep their meaning.
- A language needs `translation_<de|fr>.bin` and
  `translation_<de|fr>_selector.scpk` next to the game; without them it
  starts in English. They carry text and artwork from the German or French
  cartridge, so they are never shipped: with the US ROM and your own German
  or French ROM in the folder, `python tools/make_translations.py de` (or
  `fr`) builds them, byte for byte the files this project was tested with.
- The launcher draws with OpenGL 3.3. Where there is none -- a Hyper-V VM
  without a GPU reports only "GDI Generic" OpenGL 1.1 -- the game skips it and
  starts with the saved settings. (recomp-ui itself does not check and
  crashed there; `src/sc_launcher.c` asks the context for its version first.)

## Saving

Cities saved in the game, and the scenario win marks, live in the cartridge's
battery-backed memory (SRAM). The game keeps it in `urbanrecomp-us.srm` in the
folder it runs from (`-eu`, `-fr`, `-de` or `-jp` with another region's ROM;
German and French run on the US ROM and share its file).

- The file is written half a second after the game stores something, and
  again when the game closes, always through a temporary file, so a crash
  cannot leave half a save. The file as it was at start is kept as
  `urbanrecomp-us.srm.bak`.
- It is the raw 32 KB SRAM image, the format SNES emulators use for `.srm`
  files.
- Save states (Shift+digit) are separate. Loading one does not change the
  saved cities in the file.
- `SC_SRAM_PATH=<file>` uses another file, `SC_SRAM=0` turns saving off.
  `--qualify` runs never read or write it.

## Controls (windowed mode)

The keys below are the defaults `keybinds.ini` gets on the first run; the
launcher's Controller page changes them. `keybinds.ini` stores key POSITIONS
(SDL scancode names), so on a German keyboard SNES Y reads `Z` there -- the
key labelled Y.

| SNES button | Key(s) |
|---|---|
| D-pad | Arrow keys, or U/H/J/K (up/left/down/right) |
| A | S, or right mouse button |
| B | X, or left mouse button |
| X | A |
| Y | Y |
| L | Q |
| R | W |
| Start | Enter |
| Select | B |

The letter bindings follow the **labels on the keyboard**, not QWERTY key
positions, so they are the same keys on a German layout as on a US one. They
are resolved through `SDL_GetScancodeFromKey`, because SNES Y and SNES B now
sit on the keys labelled Y and X — which on QWERTZ are not where a positional
binding would put them.

The right mouse button also pans the map while it is held and moved.
The camera allows extra space above and below the map at every zoom level,
so edge tiles can move clear of the HUD.

Keys that are not SNES buttons:

| Action | Key |
|---|---|
| Pan the free city camera | X + arrow keys |
| Scroll faster (hold) | Ctrl: 3x; Ctrl+Shift: 10x |
| Fast-forward (hold) | Tab; Shift+Tab requests 4x the usual boost |
| Open Save City during play; Back/Close in menus | Escape |
| Back during city setup (including map size and development speed) | Controller X |
| Close full-screen information / confirm selected loan choice | Enter or normal Confirm |
| Reveal hidden 1920x1600 test City 3 on Load City | Ctrl+Shift+tilde |
| Zoom the map in / out (city view) | + / - |
| Save state to slot 1-9/0 | Shift+1 .. Shift+9, Shift+0 |
| Load state from slot 1-9/0 | 1 .. 9, 0 |
| Debug-menu code (controller 2, one shot) | F2 |
| Mouse cursor control (toggle) | F3 |
| Dump WRAM now (`wram_snapshot.bin`) | F4 |
| Debug cheats: No Disasters / Needless Money / Valve Max / Water Reclaim | F5 / F6 / F7 / F8 |
| Fast D-pad cursor (toggle) | F9 |
| Settings menu (toggle) | F10 |

Map **31337** generates dry land with forests at every selectable size.

- **F2** enters the game's hidden debug menu. It plays the documented entry
  code (Left, A, Right, Y, Up, B, Down, X, Select, Start, Start, Select, R, R,
  L, L) on controller 2 in one go, timed automatically, instead of 16
  hand-timed presses. Press it once on the "Goodbye! See you soon"
  quit-confirmation screen (Load/Save/Exit menu -> END). Confirmed working in
  play. Headless: `SC_DEBUG_CODE_AT=<frame>`.
- **F3** lets the host mouse move the game's cursor, ported from the
  community mouse patch (https://github.com/Selicre/simcity-mouse): it drives
  the same WRAM bytes (`$7e01eb`/`$7e01ed`) that patch identified, directly
  from real mouse movement instead of emulating an SNES mouse. Off by default.
  With the mouse buttons as B and A, the mouse alone can point, select and
  pan. It carries the same caveats the patch documents: some jank in menus,
  occasional resets to the origin.
- **F5-F8** flip the game's own debug-menu cheat flags in `$0425` directly --
  the same bits the hidden debug menu sets.
- **F9** makes the cursor move at host speed while a direction is held. The
  game paces its cursor with its own scheduler (the city simulation takes
  whole frames in which the cursor does not move, see
  `docs/INVESTIGATION_cursor_cadence.md`), which no ROM patch can remove. The
  normal D-pad input still reaches the game, so menus work as before. Off by
  default.

### The F10 menu

F10 opens a settings overlay drawn by the host, not by the game. The game is
paused while it is open. **Up/Down** select a row, **Left/Right/Enter** switch
or step a value, or run an action; **F10** closes it. Keys pressed in the menu
are not passed to the game.

**QOL**

| Row | What it does |
|---|---|
| MOUSE CURSOR | Same as F3: the host mouse moves the game's cursor. |
| FAST TICKS | On by default. Removes the short delay the game puts between repeated steps while a button is held, so bulldozing and drawing roads continue smoothly instead of step by step. |
| DRAG TURBO | 1 (off), 2, 3, 4 or 6 game frames per displayed frame while a mouse button is held, for faster building and dragging. |
| PAN SPEED | How many map tiles right-button panning may move per frame (1-8, default 1). |
| MOUSE SPEED | Mouse sensitivity, 50-200 % (default 100). |
| FAST CURSOR | Same as F9. |
| CURSOR SPEED | How far the fast cursor moves per frame (2, 4, 8 or 16 pixels; default 4). |
| REPLAY MENU | On by default. When you pick a scenario you have already won, offers STANDARD (the scenario again, with its goal and disasters) or FREE (its city as free play). |
| FIX POWER ON LOAD | On by default: fixes the stock game's post-load power dropout, see above. |

**CHEATS**

| Row | What it does |
|---|---|
| ALL SCENARIO WON | Marks every scenario as won -- the six, the hidden Las Vegas one, and Sylt when the ninth scenario is on -- so the select screen opens its fourth column (Las Vegas and free play) and shows a win mark on every card. It writes what the game's own save path writes, and only once the game has set up its save memory. It is stored in the save file like a saved city (see Saving), and switching it off does not take the wins back. Headless: `SC_UNLOCK_ALL=1`. |
| CHEAT NO DISASTER, CHEAT MONEY, CHEAT VALVE MAX, CHEAT WATER | The game's own debug-menu cheats, the same bits as F5-F8. Money and Valve Max are confirmed from the code that reads them; the other two bits are not yet. |
| SET POP | Holds the population at a fixed value (off, 0, 2000, 10000, 50000, 100000, 500000, 600000), for testing the milestone messages. |
| SET CLASS | Holds the city class (off, Village, Town, City, Capital, Metropolis, Megalopolis), which is what the milestones actually test. |
| CLR MILESTONE | Re-arms the milestone messages, which the game shows only once each. |

**DISASTER TRIGGER**

| Row | What it does |
|---|---|
| ARM TRIGGERS | Safety catch, off by default: the rows below do nothing until it is on, so a stray selection cannot set off an earthquake. |
| FIRE, FLOOD, PLANE CRASH, TORNADO, EARTHQUAKE, MONSTER | Start that disaster through the game's own code path, the same pending-disaster bits (`$0197`) its disaster page sets. |
| MELTDOWN, UFO | The two scenario-only events. The UFO only appears in a city of at least 84,488 people; in a smaller city nothing happens. |

**STATE**

| Row | What it does |
|---|---|
| SAVE STATE 1, LOAD STATE 1 | Save or load slot 1, the same as Shift+1 and 1. |

## Building

Prerequisites: a `snesrecomp` checkout (pinned submodule, see
CONTRIBUTING.md), Python 3.9+, Rust (for the native analyzer), CMake +
Ninja or Visual Studio, and SDL2 (e.g. via
[vcpkg](https://github.com/microsoft/vcpkg): `vcpkg install sdl2:x64-windows`).

```bash
git clone --recurse-submodules <this repo>
cd UrbanRecomp
bash tools/bootstrap.sh
# put your own legally obtained ROM in the repository root (any file name), then:
bash tools/regen.sh --no-tests
cmake -S . -B build -G Ninja -DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake
cmake --build build --target UrbanRecomp
./build/UrbanRecomp.exe                             # launcher, then the game
./build/UrbanRecomp.exe us.sfc                      # windowed, saved settings
./build/UrbanRecomp.exe us.sfc --qualify 3600       # headless check run
```

A release package is built without the generated banks, since they are
compiled from the ROM, and packed by a script that refuses a binary that
still carries them:

```bash
cmake -S . -B build-release -DSC_AOT=OFF -DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake
cmake --build build-release --config Release --target UrbanRecomp
python tools/package_release.py v1.0.0
```

See CONTRIBUTING.md for the full checkout/build/PR workflow and how this
repository's framework dependency is managed, and
`docs/INVESTIGATION_dpad.md` for the debugging tools built along the way
(env-gated tracing, PC-reachability bitmap diffing, synthetic `--input`
injection, frame dumps).

## License

Urban Recomp's own code, tools, documentation and artwork are under the
[MIT licence](LICENSE). A program built from this repository includes
[snesrecomp](https://github.com/mstan/snesrecomp), which is licensed
PolyForm Noncommercial 1.0.0, so the program itself may be used and passed on
for noncommercial purposes only. All third-party licences are listed in
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). Nothing here grants any
right to the game itself.

The hidden **TEST CITY 3** starts with approximately 62 million residents at
ordinary zone capacities. Its districts contain connected road, rail and power
networks, funded police and fire stations, parks and nuclear power plants;
the civic precinct includes buildable gifts, a stadium and airports, with
seaports along the eastern coast. The usual simulation rules still apply.
Press **Ctrl+Shift+tilde** on **Resume Saved City** to reveal it. If you have
no regular saved cities, the same shortcut on the main menu opens that page.
Selecting City 3 loads its saved SRM record if present, otherwise generates
it from code. **Escape, Save?, Yes** saves this city back to City 3 without
replacing City 1 or 2. Its full map is stored inside the SRM; the original
32 KiB cartridge area remains intact. Keep the whole SRM when backing up
City 3. Save-state loads also retain its hidden-city identity.

Ctrl+mouse-wheel zoom can expose terrain spanning up to **32768x32768** native
pixels, enough to fit the complete 1920×1600 test city inside a widescreen
window. Only the land zooms;
the HUD, minimap, overview panels and menus keep their normal display size.
Expanded cities update across the map instead of sweeping visibly row by row.

Both the yearly and toolbar budget pages accept a freely moving mouse. Click
the visible tax/funding arrows or Go With Figures button. Keyboard and gamepad
navigation retain their original jumps; an idle mouse does not override them.
