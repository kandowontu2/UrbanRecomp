# Adaptive Widescreen mod

The built-in **Adaptive Widescreen** mod uses the shared `recomp-ui` Mods
provider, like Super Metroid's custom renderer. Start the executable without
arguments (or with `--mods`), select **Mods**, enable **Adaptive Widescreen**,
choose a view size, and press **Play**. Settings persist in `sc-video.ini`.
Since 2026-09-22 this is Urban Recomp's default renderer: a fresh directory
starts with it enabled at 21:9 (448x224, as wide as the classic widescreen).
Switching it off in Mods (or `Enabled=0` in `sc-video.ini`) returns to the
classic widescreen. New configurations use **Top left** for City controls
position; existing saved Center preferences are respected.

See the [screenshot gallery](screenshots/adaptive-renderer/README.md) for the
Mods controls, adaptive landscape/portrait views, and fixed 21:9 gameplay.

The entire original 256x224 view stays visible in every mode. Terrain and
buildings extend into the extra space; the original controls, text, cursor,
and simulation retain their native coordinates. The original view can be
centered or placed at the top left during gameplay. Top left keeps the toolbar
and status display anchored while added city space grows rightward or downward.
The title, standalone menus and advisor pop-ups stay centered in both modes.
In a wide city view, a continuous header keeps date/menu controls on the left
and population, money and RCI demand at the far right. The navigation minimap
also sits at the right edge, with a camera rectangle scaled to the saved city's
dimensions and current canvas. Its navigation arrows follow the wider canvas,
and mouse targeting follows the moved groups. Native-size rendering retains
its original status layout.
The window is resizable; **F11** toggles
fullscreen. **F10** still opens the game's existing settings menu.

Advisor/tutorial pages move independently of their dimmed city background.
Their native page and portrait pixels are captured from BG3 and the stock
PPU's resolved sprite scanline, then placed at the canvas center. BG1's HUD
and BG2's city remain at the gameplay anchor. The old page's subscreen
occlusion is removed so it leaves no black rectangle behind; transparent
black lettering backed by that occlusion remains part of the opaque page.
Separate budget/statistics, fax, loan and menu screens use a centered view.

| View size | Behavior |
| --- | --- |
| Fit to window | Add columns in a wide window or rows in a tall window |
| Fit height | Keep 224 rows; add columns when the window is wide enough |
| Fit width | Keep 256 columns; add rows when the window is tall enough |
| 4:3 | Original view, corrected pixel aspect |
| 8:7 | Original view, square pixels |
| 16:10 | 308x224 canvas |
| 16:9 | 342x224 canvas |
| 21:9 | 448x224 canvas |
| 32:9 | 684x224 canvas |

Fit height/width add bars where filling the other axis would crop the original
view. Fixed presets also use bars when the window has a different aspect.
Logical pixels use a 7:6 pixel aspect for 4:3 SNES presentation, except 8:7.
Canvas dimensions round outward to even pixels, so some ratios have a tiny
rounding border. Each axis caps at 2048 logical pixels; extreme windows use
bars after reaching that cap. Scaling preserves proportions.

## Build and run

Initialize both shared dependencies, including their nested submodules:

```sh
git submodule update --init --recursive
```

On Windows with CMake, MinGW-w64, Ninja and SDL3 installed under
`C:/msys64/mingw64`, run from PowerShell:

```powershell
.\tools\run_custom_renderer.ps1 -Rom 'C:\Roms\SimCity (USA).sfc'
```

`-BuildOnly` builds without opening a window; `-SkipBuild` starts an existing
build. `-CMake` and `-Toolchain` override the executable and MinGW paths.
The script runs from this checkout so its settings, saves and optional Sylt
assets stay local to it. On other platforms, use CMake's native generator:

```sh
cmake -S . -B build-custom -DCMAKE_BUILD_TYPE=Release
cmake --build build-custom --parallel
ctest --test-dir build-custom --output-on-failure
./build-custom/UrbanRecomp /path/to/simcity.sfc --mods
```

No code generation is needed for the default executable. See
[the dependency audit](DEPENDENCY_AUDIT.md) before using the experimental AOT
targets with the updated engine.

Direct launch examples (CLI overrides last for that launch; Play in the Mods
launcher persists settings):

```sh
UrbanRecomp simcity.sfc --widescreen --aspect Fit --window-size 1280x720
UrbanRecomp simcity.sfc --widescreen --aspect Width --window-size 720x1280
UrbanRecomp simcity.sfc --widescreen --aspect 32:9 --fullscreen
UrbanRecomp simcity.sfc --no-widescreen
```

`--video-config FILE` selects an isolated settings file. Supported CLI aspect
names are `Fit`, `Height`, `Width`, `4:3`, `8:7`, `16:10`, `16:9`, `21:9`, `32:9`.

## Renderer scope

The city reconstruction is for the verified unheadered USA ROM, SHA-256
`e9c0bc05511e05a0d7c3e7cc42e761e1e8e532d46f59b9854b6902e1a2e9dd0a`.
It reads the 120x100 live map at WRAM offset `0x10200`, ROM tile tables at
`0x156a9` and `0x14f2d`, and current PPU CHR, palette and brightness at each
scanline. Roof tiles share BG2 graphics and overlap one whole cell (8 pixels)
up and left. Fine scrolling follows the PPU, including delayed coarse-cell
updates. The outer eight columns of the original city tilemap are CRT
overscan staging space; they are reconstructed from the world map when widened.
Rows with HUD or native sprite content in an edge band retain those pixels.
World bounds show the backdrop. Taller views use the first/last scanline's
PPU state beyond the hardware's vertical range.

Title scenery, main-menu and scenario-selector wood, fax desk and advisor backdrop also extend.
Other menus retain their native contents over repeated background decoration
or the backdrop. Menu content is not spread across an ultrawide display.
Interactions stay within the original game's view. Moving game-emitted sprites
continue through the adaptive margins with OAM priority, palette and tile flips;
parked HUD copies are rejected. The renderer does not create simulation actors
that the guest has not emitted, or extrapolate movement after the guest hides them. Pan normally to interact with a place
first seen in a margin. Other ROM regions and all disaster scenes have not
been qualified for custom rendering.

The renderer never writes WRAM, VRAM, OAM or CGRAM. Native pixels are
copied byte-for-byte except for the city's bare eight-pixel staging columns
and the background revealed when relocating an advisor page. The relocated
page retains its finished native pixels. HUD and sprite pixels remain intact.
It uses its own dynamic
surface, independently of the shared PPU's horizontal widening limit. While
the mod is enabled it supersedes legacy `SC_WIDESCREEN` / `SC_HOST_MAP` paths.
Existing game fixes still run identically with the mod enabled and disabled.

## Validation

ROM-free CTests cover full-view containment, extreme windows, scaling/input
coordinates, all Mods choices, toggle/persistence, tile flips, transparent
overlap, map bounds, PPU immutability, desk periods, title fades/lights,
scenario cards, flat-menu fills, fine/coarse scroll handoff, sprite crossings
and priority, city-load holding, and staging-edge repairs that preserve HUD.
Layout checks also cover independent panel placement, black lettering and
sprite pixels, the old panel's subscreen window, fades and returning to play.

With Python and Pillow, the integration test runs normal inputs through boot,
main menu, scenario selector, populated San Francisco, fax, advisor and a practice city. It checks every frame's CPU,
WRAM and master clock with the mod on/off, and compares the native view
byte-for-byte in periodic captures outside explicitly reported staging-edge
repairs. Each adaptive capture has an audit JSON listing exactly which
eight-pixel bands were corrected on each row. It then pans through all presets, wide,
square and portrait Fit modes, plus top-left positioning.

```sh
python tools/test_adaptive_renderer.py --exe build-custom/UrbanRecomp.exe --rom simcity.sfc --artifacts build-custom/integration
```

The test prints a fresh artifact directory containing logs, PPMs, PNG previews
and a local `stock-route/city.state`. Use that state for the Windows resize
test; it owns and closes only its child process/window:

```sh
python tools/test_adaptive_window.py --exe build-custom/UrbanRecomp.exe --rom simcity.sfc --state ARTIFACTS/stock-route/city.state --artifacts build-custom/integration
```

The independent layout regression checks both wide and portrait windows:

```sh
python tools/test_adaptive_layout.py --exe build-custom/UrbanRecomp.exe --rom simcity.sfc --artifacts build-custom/integration
```

It verifies centered title/menu/fax pages, left gameplay controls, and centered
advisor pages without moving their city/HUD background. A stock-PPU reference
pass hides the foreground and removes the obsolete subscreen occlusion using
`SC_LAYER_MASK=3 SC_SUB_WINDOW_MASK=0`. The latter diagnostic override is
restored immediately after drawing, before guest execution resumes. The
revealed background must match that reference; relocated page pixels must
match the original stock picture. All four runs retain identical guest state.
Audit captures include the relocated page mask. Review the resulting images
as well as the numerical checks.

The resize test covers 16:9, 21:9, 32:9, portrait, square and 4:3 window shapes.
`--screenshots` additionally brings its child forward for desktop screenshots;
the default test does not request foreground focus. Live keyboard/mouse input
is ignored in this scripted test so typing elsewhere cannot change the city.
Review the canvas images for new-world correctness in addition to the
automated size and native-view checks. Pixel equality alone does not establish
that newly exposed scenery is correct.

`tools/test_adaptive_launcher.py` accepts the same arguments as the resize
test. It clicks the actual Mods toggle and 32:9 choice, presses Play, then
checks the persisted settings and launched canvas.

The new local save format preserves PPU CPU-port latches, the host clock and
HDMA walker in addition to the shared machine snapshot. The old unversioned
format omitted those latches: loading it shifted later tile uploads and
corrupted water/shore graphics even with the mod disabled. Current main accepts legacy states with a warning; they still lack those
latches. Use fresh versioned states for rendering comparisons. The regression compares a 300-frame
restore against uninterrupted play, including captured pixels. In-game SRAM
city saves are separate from these local emulator snapshots.


## Regression follow-up (2026-09-19)

The developer's [eleven-item checklist](https://github.com/blackerking/UrbanRecomp/issues/2)
was caused by two gaps: the draft predates main's newer host fixes, and its
separate margin renderer did not inherit the classic renderer's screen-specific
handling. The original tests compared guest state and the native rectangle;
they could pass while the newly exposed picture was wrong. The roof fixture
also encoded a one-pixel offset instead of the format's one-cell offset.

Current main `28c280b` is merged. Its published engine pin `bc2838d`, beam and
audio fixes, translations, SRAM persistence and guarded launcher are retained.
Sylt and Adaptive Widescreen share that launcher. A shared-engine migration is
separate work; this branch no longer drops the game's engine dependencies.

| Report | Resolution |
| --- | --- |
| 1. Title lights | Repeat the detected live OAM light row across the canvas |
| 2. Title fade | Keep title scenery while screen 2 fades; obey live brightness and force blank |
| 3. Scenario cards | Render the actual wide card/name maps once, extend wood outside them |
| 4. Fax | Detect the desk layer and its real 16-column/vertical pattern |
| 5. Scroll seam | Follow fine/coarse scroll and reconstruct the bare staging-edge columns |
| 6. Roofs | Use BG2 graphics and an eight-pixel overlap |
| 7. Moving objects | Decode moving OAM across the canvas, with priority and parked-slot rejection |
| 8. Statistics/tax margins | Use the majority native background, avoiding panel/cursor stripes |
| 9. City load | Hold old map, graphics, palette and camera through dark-to-lit transition |
| 10. No OpenGL | Probe the actual GL version; skip the launcher below desktop GL 3.3 |
| 11. Engine pin | Retain current main's published engine and optional AOT ABI |
| 12. Card pictures in the margin | Lay the synthesised desk only where the guest's own cell draws nothing |

Report 12 was the desk showing through the card pictures in the margin
columns: their dark pixels are colour 0, which the guest's own columns show
as the backdrop, and the selector path put a plank under every pixel before
drawing the guest's layers over it. `guest_cell_inked()` now asks whether the
cell has any ink in CHR: an empty cell -- the shipped map's unused right-hand
columns, which the synthesised desk exists to fill -- keeps its plank, a cell
with art does not. Measured on two selector states, the margins then match a
classic-widescreen render of the same frame to the pixel (763 differing
pixels before, 0 after), and the card pictures read #000000 as they do in the
native columns. Report 13, the margin cards arriving at full brightness
instead of fading in, went with it -- confirmed from play.

Validation uses the expanded CTests, the 16-case game-route suite, six actual
window resizes, actual Mods toggle/choice/Play/persistence, and a no-OpenGL run
using SDL's dummy video/software renderer. A build with the launcher dependency
absent is also checked. AOT and every translated/disaster sequence are not
qualified by these tests. The moving-object regression exercises native OAM
crossings; it does not claim arbitrary offscreen simulation objects exist.

Top-left is the default HUD layout because its controls stay together and the
extra map gets a stable origin. Center remains available under City controls
position. Title/menu centering is independent of that setting. Advisor pages
are separated from the composed picture as described above, so opening a page
does not move the city or toolbar.


The final renderer run (`a9cddae`) completed 13,920 paired CPU/WRAM/clock frame
checks and 142 native-region captures across 16 cases, plus the separate
300-frame save/restore comparison. Capture audits identify the corrected
overscan bands; all other native pixels must match. The final six-window
resize run and screenshots include the left-aligned default. See the
[updated gallery](screenshots/adaptive-renderer/README.md).

The independent-centering follow-up also passes 7,200 paired guest-state
checks and 48 layout captures, including eight advisor captures checked
against the stock page and unoccluded-background references. Both city and
advisor states pass the six actual window-resize shapes. The gallery includes
the centered title, main menu and wide/portrait advisor pages.
