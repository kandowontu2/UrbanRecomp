# Urban Recomp Enhanced Beta 3

Huge maps are now **480x400**, twice each dimension of Big (240x200).
Choose Normal, Big or Huge with **L** on the game-selection menu or
**NEW MAP SIZE** in F12 before starting a city. Generation, construction,
simulation fields, power, overview, minimap and saves use the full dimensions.

At accelerated development speeds, population now reflects actual developed
zones at the selected **2x, 5x, 10x or 50x** refresh multiplier. Electricity uses
that selected multiplier relative to its observed native schedule, with
fractional-frame timing retained. Calendar, budgets and disasters keep their
normal timing. Normal preserves the native schedule.

Settled power flags remain consistent during native scans. Completed power
bitmaps publish at safe boundaries, and cached lightning warnings clear on
powered zones and self-powered plants. This fixes the stale warning seen on
newly placed nuclear plants at accelerated speeds.

The population calculation, capacity totals, history and saves retain 64-bit
values up to **9,999,999,999**. The limit does not increase zone density.
Population continues to use the original sprite font. Beta 2's immediate
placement rendering, widescreen mouse and retained off-screen drags are included.

Existing enhanced Big saves and sidecars load and upgrade. Keep `.srm`,
`.srm.population` and `.srm.world` together, and retain a backup before opening
new saves in an older build. Older enhanced builds cannot read this build's
new world payload.

Validation: native-ROM development, population, construction, power and world
tests; mouse coordinate/menu tests; terrain and renderer regressions; older
save migration; and a 7,200-frame Huge-city qualification. A sparse Huge city
with a far-corner nuclear plant ran at approximately 60.1 FPS with SDL software
rendering. Dense Huge cities at 50x may require more CPU time.

Windows x64 interpreter build; requires your own clean US ROM. No ROM or
generated game code is included. Optional fiber execution and regional ROM
acceleration remain unsupported. Live desktop testing of all mouse/modal
behavior remains ongoing.
