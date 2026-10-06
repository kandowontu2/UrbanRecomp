# Source-based Test City 3 layout

An unsaved Test City 3 asks for map size and development speed, then builds
an empty city with completed infrastructure. A saved City 3 loads directly.
All six sizes use water-free map 31337, ordinary zone capacities and native
growth rules. This is a measured improvement, not a proof of a global optimum
or a measured stable maximum. Read [PLACEMENT_EFFECTS.md](PLACEMENT_EFFECTS.md)
for every placement tool and gift, exact neighboring effects and source routines.

## Constraints that drive the layout

| Native constraint | Consequence |
| --- | --- |
| Residential score is `min(6000, max(L-P, 0)*32)-3000 + demand`, with demand at most 2000 | Housing below `L-P=32` cannot obtain a positive growth score even at maximum demand. Convert those sites to commerce after computing real fields and placing gifts. |
| Apartment conversion requires density at least 65 and pollution below 129 | Keep local commerce near paired residential lots to bootstrap density, while placing industrial pollution at the outside. Empty housing alone does not create enough density. |
| Commerce uses `64-4*distance + demand`; upgrading C4 to C5 requires land value 128 | Use commerce between housing and industry, and distribute gifts to deficient R/C sites. Peripheral commerce will not necessarily reach full capacity. |
| Transport starts with twelve perimeter probes up to two cells from a zone center, followed by a bounded random walk | Shared rail every seven cells serves both paired 3x3 lots. Nearby local jobs reduce dependence on a long commute to the outer industry. |
| Land value is `4*(34-min(distance,32)) + terrain - previous pollution - crime penalty`, clamped 1..250 | Recalculate the real global center, rather than repeat a fictitious center in every district. Expanded maps scale center distance by width/120; local service and commute ranges remain native. |
| Stadium, airport and seaport lift different positive-demand caps | Include all three, including a dry-land port. The native industrial cap checks that a port exists; navigable water is needed for boat spawning, not for this cap. |
| Nuclear supply is 2000 conductor visits; coal supply is 700 and produces pollution | Two nuclear plants per full 56x56 district, with power crossing rail at offset one and continuing through civic/park parcels. |
| Police/fire coverage depends on power, transport access and funding | Repeat ordinary stations at native service distances. Use finite headquarters where their stronger coverage helps. Retain upkeep. |

The source routines and host equivalents are listed in the placement guide.
Terrain packing has byte saturation and alignment effects; a simplified
circular gift radius is not an accurate replacement for the native calculation.

## Geography, gifts and economy

Industry occupies the global outside band. A commercial band with park/wire
buffers separates it from the residential interior. Inner six-cell parcels have
a paired residential row, local commercial jobs and parks. Sparse roads repeat
every 56 cells; shared rail repeats every seven. Civic facilities replace whole
lots. Boundary fragments are removed instead of leaving partial buildings.

There are 27 gifts: three police headquarters, three fire headquarters, one
mayor's house, one bank, five amusement parks, two zoos, one Mario statue,
one Expo, two windmills, three libraries, three large parks and two train
stations. This uses the ordinary eventual gift quantities available before the
1950 fountain. The test pre-awards them at its empty start and consumes their
native counters to avoid awarding duplicates. Future native milestones remain
active. Casinos add crime, and landfill supplies no useful aura on this dry map,
so neither is placed.

The gift scorer calculates the actual native packed terrain and diffusion
before and after each candidate. It weights residential deficiencies, rewards
crossing useful housing/commerce thresholds, and includes earlier gifts so
redundant overlap has less value. Candidate sampling visits all four grid
alignments. A generation assertion checks its predicted field against the
original terrain routine before any gifts are placed. Gifts remain scarce on
large maps; ordinary parks and services provide the repeated infrastructure.

Every R/C/I lot starts undeveloped, with **zero population**. Taxes start at 3%,
services at full funding, and the treasury at eight million. Native annual
collection clamps the treasury to 999,999; no money or growth cheat is enabled.
The stadium, airport and port use their normal rules.

## Counts and ordinary capacity

Ordinary capacities are R=40, C=5 and I=4 simulation units. HUD capacity is
`800*(R+C) + 640*I`; rare paired towers are excluded. These are upper bounds
for the placed zones, not a prediction that every lot will reach them.

| Map | Residential | Commercial | Industrial | Ordinary capacity |
| --- | ---: | ---: | ---: | ---: |
| 120x100 | 270 | 249 | 104 | 481,760 |
| 240x200 | 978 | 1,143 | 369 | 1,932,960 |
| 480x400 | 3,973 | 4,427 | 1,753 | 7,841,920 |
| 960x800 | 16,272 | 17,158 | 8,075 | 31,912,000 |
| 1920x1600 | 64,585 | 69,367 | 31,965 | 127,619,200 |
| 3840x3200 | 258,751 | 276,253 | 129,931 | 511,159,040 |

At full ordinary capacity, the largest city's jobs total 1,900,989 units,
against a residential workforce of 1,293,755. Jobs exceed workers deliberately:
commerce near the global edges has lower land value and will not all become C5.
This capacity comparison does not bypass normal demand or employment feedback.
All sizes have 27 gifts and one stadium, airport and port. Plant and station
counts scale with the occupied districts; every starting zone has power and
native perimeter transport access.

## Natural growth validation

A controlled 120x100 comparison runs complete original simulation ticks from
an empty city, retaining growth RNG, commuting, demand caps, native upkeep,
services, pollution, power and annual collection. Modal presentation and
disasters are suppressed only for this comparison. It is not a test of disaster
survival. The former layout uses its original 7% tax; the revised layout uses
its intended 3%. The improvement combines geometry, gifts, facilities and tax.

| Layout | Population after ten years | Completely empty residential lots | Housing upgraded beyond free houses |
| --- | ---: | ---: | ---: |
| Former generated city | 77,840 | 215 | 15 |
| Revised generated city | 310,880 | 0 | 230 |

The revised city peaked at **373,920 in year three** and had **310,880 in year
ten**. No completely empty residential zones or unpowered zones remained at
any annual sample. At year ten, 35 residential sites still held free houses,
23 had density below the apartment gate, and 16 had land-minus-pollution below
94. No housing exceeded the pollution-129 barrier. Five initial housing lots
became ordinary native schools/hospitals. Demand and field feedback still
produce later fluctuations and some stagnation; this is not a stable maximum.

The generator was checked at all six sizes: complete footprints, zero initial
census, finite gift inventory, power, transport, global edge industry, fields,
full-world save round trips and preservation of City 1/2. The actual menu flow
was also replayed from an empty SRAM. Existing City 3 saves retain their old
layout; start an unsaved City 3 to use the revised generator.

Largest-map validation also found and fixed the police/fire byte conversion
cursor wrapping at 65,535. Boundary comparisons at 32,766, 65,534, 131,070 and
191,999 agree with original instruction execution; both coverage conversions
finish all 192,000 cells. This matters for calendar progress and later density
updates, not just initial map appearance.

To reproduce growth and placement audits, build `UrbanRecompCityGrowthTest`
with the locally verified clean US ROM and an initialized native city WRAM.
See its usage output. Private ROM/WRAM fixtures are not distributed. Future
layout claims must record natural multi-year results; a capacity count or an
artificially mature fixture cannot establish an optimum.
