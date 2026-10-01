# Urban Recomp Enhanced 1.2.0 Beta 1

First enhanced prerelease of the fork of
[blackerking/UrbanRecomp](https://github.com/blackerking/UrbanRecomp), based on
upstream 1.1.1. Original history, credits and licenses are retained.

## Changes

- **Development speed:** F12 (F10 also works) offers Normal, 2x, 5x, 10x and
  50x RCI development attempts. Calendar, budgets and disasters keep their
  original schedule.
- **Power connections:** accelerated settings refresh changed electrical
  networks through the original flood fill, preserving coal/nuclear capacity
  and conductive-tile rules without advancing city time.
- **Population:** 64-bit capacity tallies, population calculation, signed
  changes, history and saves support a cap of **9,999,999,999**. The extended
  counter uses the original game digits and person icon. Zone densities still
  determine the population achievable on a given map.
- **Large maps:** the main-menu **L LARGE MAPS** toggle enables **240x200** new
  maps, twice the width and height and four times the area. Construction,
  simulation, power, disasters, overview, cameras and saves use the full world.
- **Mouse:** absolute pointer mapping supports the full adaptive widescreen
  city canvas, DPI/scaling and centered layouts. Roads, rail, power, zones and
  other construction tools use full world coordinates. Right-drag pans;
  right-click cancels construction. Drag previews retain their last valid
  endpoint off-screen, continue on re-entry and commit on outside release.
- **Widescreen layout:** population, money and RCI move to the far-right
  header. Navigation controls follow the expanded view, with a minimap outline
  projected from the actual canvas and world dimensions.

## Download and run

Download `UrbanRecomp-v1.2.0-enhanced.1-windows-x64.zip`, extract it, and open
`UrbanRecomp.exe`. Supply your own clean **US SimCity SNES ROM** in the launcher.
The Windows x64 package bundles SDL3, required compiler runtimes and licenses.
No ROM, generated game code, extracted game graphics, translations, saves or
personal configuration files are included. A SHA-256 checksum is provided.

Keep the `.srm`, `.srm.population` and `.srm.world` save files together when
moving your two saved cities. Back up existing saves before changing versions;
older upstream builds cannot preserve the extended population or full world.

## Validation and current limits

The release build passes video/renderer tests plus pointer, menu, terrain,
development, construction/power, population and full-world ROM tests. Normal
and large city states each completed a 600-frame integration check. Recorded
SDL mouse tests cover far-right construction, outside release, re-entry and
right-click cancellation. The extracted package was checked for bundled DLL
startup and for excluded ROM/runtime artifacts.

This is a **prerelease**. Hands-on mouse/gameplay verification and sustained
large-city performance testing remain. Some narrative dialogue pages retain
their original controls. The enhancements support the verified US ROM on the
default interpreter path; regional ROMs and the optional AOT/fiber execution
path do not implement these enhancements. Native economy/demand fields and
graph scaling retain their original compatibility limits.

Details: [PC enhancements and controls](https://github.com/kandowontu2/UrbanRecomp/blob/v1.2.0-enhanced.1/docs/PC_ENHANCEMENTS.md).
