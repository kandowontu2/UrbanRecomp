# Enhanced fork changelog

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

- Add full **1920×1600** maps alongside 120×100, 240×200, 480×400 and 960×800.
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

- Middle-button drag moves land with the mouse at 3× speed, hides the pointer
  and uses relative capture so panning continues past the screen edge.
- Correct HUD mouse alignment while Copy/Paste is selected.
- Coal and nuclear plants support multiple placements in one drag.
- Mouse clicks operate F12 options; Left decreases and Right increases.
- Gift choices and single-gift messages accept centered mouse input. Toolbox
  gift hitboxes cover the full 32×32 images.
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
  filled 1920×1600 Fit/50× CPU and Vulkan replays compare states and images.
  Detailed measurements and fallback scope: [GPU_PERFORMANCE.md](docs/GPU_PERFORMANCE.md).

Remaining limits: complete interpreter removal and sustained 60 FPS in every
heavy phase are unfinished. Physical touchpad pinch delivery still needs
hands-on testing. The reported black yearly budget popup remains deferred.

## Earlier enhanced releases

- [Beta 10](https://github.com/kandowontu2/UrbanRecomp/releases/tag/v1.2.0-enhanced.10):
  960×800 maps, native size selection, saved-city geometry/power recovery,
  Fit to Screen and rendering performance work.
- [Beta 9](https://github.com/kandowontu2/UrbanRecomp/releases/tag/v1.2.0-enhanced.9):
  Windows launch without a console.
- [Beta 8](https://github.com/kandowontu2/UrbanRecomp/releases/tag/v1.2.0-enhanced.8):
  map updates, mouse save/gift/confirmation menus.
- [Beta 7](https://github.com/kandowontu2/UrbanRecomp/releases/tag/v1.2.0-enhanced.7):
  Huge-map simulation and smoother adaptive Tab fast-forward.

Full prior release notes and upstream history remain in
[GitHub Releases](https://github.com/kandowontu2/UrbanRecomp/releases) and Git.
