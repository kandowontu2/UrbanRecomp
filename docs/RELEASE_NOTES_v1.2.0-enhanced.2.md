# Urban Recomp Enhanced 1.2.0 Beta 2

This update reduces placement and terrain-update latency while retaining the
original 60.1 Hz game timing and Beta 1's development, population, large-map,
widescreen and mouse enhancements.

- Completed frames present before the pacing wait, reducing display latency.
- Committed construction and development render across the whole viewport
  without waiting for the original SNES tile cache. HUD, roofs, sprite priority,
  windows and color math remain intact.
- Large edits no longer trigger the city-load freeze. Actual loads still
  preserve the previous city's graphics until its fade completes.
- The native mouse proxy stays within byte bounds throughout every guest frame,
  including widescreen gestures.
- Performance diagnostics now report construction queue and commit times.

Recorded software-renderer checks held about 60.1 FPS. A 20-zone drag committed
in about 1 ms after one queued frame; a 1,078-cell large-map bulldoze at 50x
development committed in about 2.6 ms after two queued frames. Renderer tests
cover immediate terrain, roof and HUD/OBJ behavior, immutable PPU state, bulk
edits and load fades. The full construction, power, population, development,
world, terrain, pointer and menu checks also pass.

Extract the Windows ZIP and launch `Start-UrbanRecomp.cmd`. Choose your own
clean US SimCity SNES ROM. Keep your existing `.srm`, `.srm.population` and
`.srm.world` files together when moving saves. The package contains no ROM,
generated game code or personal configuration/saves.

This is a prerelease. Physical-display latency and sustained crowded-city
performance still need hands-on verification. Some narrative dialogue pages
retain their original controls. The enhancements use the verified US ROM and
default interpreter path. Native economy/demand and graph scaling retain
their original compatibility limits.
