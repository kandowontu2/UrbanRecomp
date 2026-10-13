# Terraforming Mars

Choose Select Scenario and scroll to the rightmost column. Terraforming Mars
is the lower card. It is available on a fresh save and uses the original
selector, fax briefing, music and confirmation/back controls.

The city begins in April 2050 on a water-free 120x100 Martian map. It has
20 coal power plants, dense industrial factories, a western residential and
commercial colony, roads, electrical crossings, police and fire stations.
The initial treasury is $60,000, tax is 5%, and services are fully funded.
This is an intentionally developed polluted challenge; ordinary Test City 3
continues to start with undeveloped, zero-population zones.

The deadline is April 2055. Hold all of these conditions for twelve consecutive
monthly checks before or at the deadline:

- At least 2,400 surviving ordinary park tiles. The initial city has no natural
  foliage, so existing forests cannot satisfy this objective at the start.
- Average citywide native pollution of 8 or less, measured over the actual
  pollution field. The display rounds the average upward.
- Pollution hot spots of 80 or above cover at most 1% of that field.
- At least 50,000 people, using the same population census as the HUD.
- At least 95% of residential, commercial and industrial zone centers powered.

Parks retain their normal amenity effects; they do not artificially erase
pollution. Replace coal power and demolish or reduce dirty industry, while
preserving electrical connections, housing and jobs. A twelve-month clean streak
cannot be earned by repeatedly checking one month or jumping the date.

The screen's status panel shows each target and the remaining months. Every
park and reduction in pollution contributes to visible progress. Red soil
gradually becomes green; the final completed world restores the exact current
Native seasonal palette and foliage. Colors can regress before success if
the city deteriorates. Dr. Wright announces success or failure, and a victory
receives its own scenario completion mark. City saves retain all progress,
the deadline and clean streak.

## Validation

The generated start measured 471,040 people, no parks, average pollution 118,
1,658 hot cells out of 3,000, and all 736 R/C/I zones powered. Pollution comes
from real coal/factory tiles and the cartridge's scans, not a preset field.

A full five-year test ran the native simulation at normal development speed,
with native growth, commute, demand, taxation, funding and population. Only
presentation waits and disasters were excluded from this repeatable test.
The untouched city and the old 1,200-park cleanup both failed in April 2055.
A paid 2,400-park cleanup passed after twelve months in April 2051, with
67,360 people, average pollution 3, no hot spots, 264/272 powered zones
and $13,195 remaining. Its population declined afterward; this is a proof
of attainable victory conditions in the controlled run, not long-term
optimality or a guarantee of growth. The
original 1,200-park demonstration is no longer a winning route. The
harder rules double both the required parks and the clean period, multiply the
population floor by five, tighten the pollution limit, and require more power
coverage. All conditions must hold together; falling below any one resets the
streak. The starting treasury and five-year deadline are unchanged.

Snapshots and city records now use schema v10, with room for the full
12-month streak. Formats v2 through v9 remain readable. Loading an active v9
Mars city preserves its map, date and money but resets its easier qualification
streak and derived color progress. Previously completed victories stay won.

`UrbanRecompMarsScenarioTest` accepts a private clean US ROM and initialized
128 KiB WRAM fixture. With two arguments it checks goal boundaries, monthly
streaks, the exact deadline, current/legacy snapshots (including 11-month
progress), city-save identity, selector controls, message lifecycle and the isolated Mars completion bit.
Append `240 output.csv` for the untouched five-year run,
`240 output.csv --clean` for the old 1,200-park route (which must now lose), or
`240 output.csv --restore` for a paid, 2,400-park cleanup. These controlled
runs keep native growth and budget rules and exclude modal waits/disasters;
they are not a proof of optimal population or disaster survival.

Terrain tests verify seasonal blends, unchanged building/UI colors, minimap
colors and byte-exact restoration at completion. Existing mouse, renderer and
save-format checks cover the neighboring systems.
