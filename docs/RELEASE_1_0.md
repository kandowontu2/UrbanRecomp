# UrbanRecomp Enhanced 1.0

The first stable release of UrbanRecomp Enhanced brings together the work from
Enhanced Betas 1–23. It retains the tested Beta 23 gameplay and save formats,
with stable release packaging, version metadata, guides and credits.

## Downloads

- **Windows x64:** `UrbanRecomp-enhanced-v1.0.0-windows-x64-single.exe`.
  One portable EXE contains the runtime, assets, all 21 music tracks, guides,
  credits and licenses. Run it and select your own clean US SimCity SNES ROM.
  Saves and settings stay beside the EXE; `--portable-docs` opens the guides.
- **macOS 11 or newer:** `UrbanRecomp-enhanced-v1.0.0-macos-universal.zip`.
  Includes one self-contained app for Apple Silicon and Intel, with the same
  music and documentation. Copy the app to Applications and choose your ROM.
  The app is ad-hoc signed; if macOS blocks it, use Privacy & Security → Open Anyway.
- SHA-256 checksum files accompany both downloads.

## What is included

- Six map sizes: **120×100, 240×200, 480×400, 960×800, 1920×1600 and 3840×3200**.
  The largest has **1,024 times** the original map's area. Journey mode can
  expand the city as population milestones are reached.
- Nine generation styles: **Native, Procedural, Islands, Lakes, Rivers,
  Fractal, Continent, Delta and Atolls**, with seeds **00000–99999**. Native
  retains the original generator's feature sizes on larger maps; map 31337
  is water-free. Sharp previews support zoom and middle-mouse panning.
- Nine land types: **Native, Basalt, Amazon, Desert, Mars, Venus, Arctic,
  Swamp and Moon**, with seasonal variations. Native is the default. Moon
  grows no natural forests; stadiums have domes on Mars, Venus and Moon.
- Full mouse controls across setup and game screens; free middle-mouse drag
  panning with a live minimap, pointer-centered Ctrl+wheel zoom, fixed-size
  HUD/menus, adaptive widescreen and Fit-to-screen window sizing.
- Gesture construction and building-aware **Copy/Paste** for zones, roads,
  railroads, power lines, parks and ordinary facilities. Partial building
  outlines select the complete building; special gifts remain unique.
- Saved per-city development speeds of **1×, 3×, 5×, 10×, 20× and 50×**;
  Tab, Shift+Tab and Ctrl+Shift+Tab accelerate the complete simulation while
  music retains its normal tempo. Native simulation and Windows Vulkan
  composition accelerate large cities.
- Expanded vehicle fleets, automatic post-load power repair, dates through
  year **999999**, scaled starting funds, four difficulties including Super
  Hard, improved budget/message controls, and manual UFO/nuclear disasters.
- Source-based **Test City 3** for every size, a detailed placement-effects
  guide, a Shift-click debug gift palette, and zoom-aware movable View labels.
- **Megagopolos at 10 million** and **Gigagopolois at 100 million**, with saved
  Dr. Wright announcements and additional city themes.

## Music and credits

The restored SimCity soundtrack is credited to **Pinci / Church of Kondo** and
**Relikk**, with the original MSU-1 reference work by **pev / pepillopev**.
Megagopolos uses **Markify's** restoration of the unused Super Mario Kart Vanilla
Lake theme, composed by **Soyo Oka**. Gigagopolois uses **LOOP16B** from
**LOOP816 (2023)**; **Soyo Oka / 岡素世** is its artist, composer and publisher.
Both additional themes are balanced with the restored city tracks.

Enhanced fork: **kandowontu2**. Upstream UrbanRecomp: **blackerking**.
snesrecomp / recomp-ui: **Matthew Stan / Matthew Stanley**.
Original SimCity: **Nintendo, Maxis and Electronic Arts**.
Full contributor and component notices are bundled and listed in
[CREDITS.md](../CREDITS.md).

## Compatibility and validation

Existing saves remain compatible; the release contains no ROM or personal save
data. Enhanced 1.0 is versioned independently from upstream's release series,
using the tag **`enhanced-v1.0.0`**.

Windows packaging checks every embedded file and music track, the native
runtime, 1.0 product metadata, portable launch and milestone music. The Mac
build checks both architectures, host rendering/input tests, all PCM tracks,
bundle signing and packaged startup. The current gameplay also retains the
Beta 23 milestone, gift, View, camera, renderer and save-reload validation.

Very large cities and aggressive fast-forward remain workload and hardware
dependent. Mac uses Metal presentation with CPU terrain/field composition;
Windows Vulkan compute acceleration is unavailable on Mac. The Mac app is
ad-hoc signed rather than Developer ID signed or notarized. The placement
guide documents natural growth measurements rather than promising a maximum
sustainable population. See the bundled enhancement and performance guides.

This is an unofficial fan project. Your own clean US cartridge ROM is required.
