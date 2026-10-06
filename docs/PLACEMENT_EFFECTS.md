# Placement effects and city-growth reference

This is a reference for players **and for future changes to this port**. It
describes the verified US SNES rules, together with the relevant host adaptations.
It is not a guide to SimCity 2000/4 or the NES prototype. Revision: 2026-10-06.

The important distinction is between **power**, **a successful commute**,
**population density**, **land value**, and **global demand**. A building may
help one without helping the others. A rail-adjacent, powered residential zone
can remain empty or stop at eight houses when the other checks fail. Even
maximum native residential demand cannot overcome `land-pollution < 32`;
the test generator rejects those housing sites after placing gifts.

## How the neighborhood calculations work

| Calculation | Actual rule | Source locator |
| --- | --- | --- |
| Transport access | Twelve probes around the footprint, two tiles from a 3x3 zone's center. Finding rail/road is only the start of a commute. | US `03:b26b`; `sc_transport.c` |
| Commute | A randomized walk of at most 30 steps must find an acceptable destination. Junctions, failed branches and backtracking consume this budget. Reachability is not a fixed circle. | US `03:b1a5`, `03:b2d6`, destination tables `03:b3e9`; `sc_transport.c` |
| Residential density | Free houses contribute their occupied-house count. Ordinary residential buildings contribute 16/24/32/40 units. Commercial and industrial units are multiplied by eight. These inputs are multiplied by eight, capped at 254, placed in 2x2 cells, smoothed three times, then doubled. Empty zones contribute zero. | US `03:9ad7`, `03:9bd5`; `sc_density.c` |
| First apartment | Eight occupied houses still need density **at least 65** to become apartments. Pollution **129 or higher** blocks residential growth. | US `03:9495`, `03:94cc` |
| Natural amenities | Nonempty natural terrain and parks, tile IDs `01..27`, contribute 15 per tile to the terrain calculation. They do **not** contribute population density. | `sc_land_summary.h:ScLandSummaryPack`; US `03:9cdf..9fa6` |
| Gift amenities | Gift-art tiles `2bf..353` force the current 2x2 terrain accumulator to 255, except the police/fire HQ center tiles. Other HQ tiles still supply this effect. The initial mayor's-house footprint has fewer qualifying tiles. | Same source; US `03:ae30` upgrades the house |
| Terrain diffusion | The terrain field uses 4x4 map cells and a weighted neighboring-cell calculation. Placement across its boundaries matters. Byte accumulation/order also matters: gifts do not add a uniform amount in a circular radius. | US `03:9fa6`; `sc_world_guest.c:terrain_quality_cell` |
| Land value | For occupied 2x2 cells: clamp `4*(34-min(distance,32)) + terrain - previous pollution - crime penalty` to 1..250. Crime at least 190 costs another 20. | US `03:9e61`, `03:9c11`; `sc_land.c` |
| Residential local score | `min(6000,max(land-pollution,0)*32)-3000`, then add R demand. Thus pollution hurts both land value and the residential score. A failed commute changes the score; no access can cause immediate decline. | US `03:99d2`; `sc_world_guest.c:zone_score` |
| Commercial local score | `64-4*distance`, then add C demand. Land value separately limits building capacity. C5 requires land at least 128 when upgrading from C4. | US `03:9a0e`, `03:a25c`, `03:9567` |
| Industrial local score | Zero with a successful commute; failed commuting supplies a penalty. Add I demand. There is no residential-style land-value reward in this score. | US `03:9a31` |
| Building class | R/C appearance class uses `land-pollution` thresholds 30, 80 and 150. The higher class is relevant to TOP formation; an expensive-looking texture alone is not proof of optimal occupancy. | US `03:9468` |
| TOP buildings | Matching fully grown, high-class R or C neighbors must be adjacent, with center offsets `(3,0)` or `(0,3)`. Diagonal matching zones cannot pair. | US `03:95ac..978a`; `sc_zoning.c` |
| Crime | Up to 300 before service subtraction: `max(128-land,0) + density + casino count`. Subtract funded police coverage; clamp the result to 0..250. Every casino adds one **citywide**. | US `03:9e8e`; `sc_world_guest.c` crime kernels |
| Service coverage | Powered/access-connected normal police/fire stations start at their funded strength, normally 1000. HQs double that. Missing power halves it; missing road/rail halves it again. Three smoothing passes spread strength across 8x8 cells. | US `03:aaeb`, `03:ab0e`, `03:ab67`; `sc_service.c` |
| City center | The mean coordinates of owner-marked buildings, calculated by the density scan. The mayor's house is not a privileged center anchor. Empty zones and civic buildings also participate in this centroid. | US `03:9b0a..9bd4`; `sc_density.c` |

On enlarged maps, the host scales **center distance** by `width/120`. Local
commuting, pollution, density, gift diffusion and service spacing retain their
native tile scale. Enlarging the map does not make one gift or one police
station cover an entire enlarged district.

### Capacity, employment, taxes and facility caps

Ordinary R zones have eight free houses at 20 people each, followed by blocks
of 320/480/640/800 people. Ordinary C zones have five stages of 160 people;
I zones have four. A TOP half holds 960, so a complete R-TOP or C-TOP pair
holds **1920**, not 1960. The HUD includes all three zone classes.
Sources: US `03:842f`, `03:8456`, `03:847a`; `sc_population.c`.

Employment compares residential simulation units divided by eight with C+I
units. At ordinary full capacity this gives:

```
workers = 5 * number_of_R_zones
jobs    = 5 * number_of_C_zones + 4 * number_of_I_zones
```

Those are capacity calculations, not current jobs. Empty commerce supplies no
jobs or density. A city designed only around the equation can fail to get off
the ground. Taxes modify demand for all three zone classes (`03:895f..8b10`).
Lower taxes can aid development, but service and transport upkeep still apply.

The native advice routine sets a positive-demand suppression flag when its
corresponding facility is missing. The thresholds are **500 R, 100 C, or 70 I
simulation units**, not a single total-HUD-population threshold. Stadium,
airport and seaport presence lift the corresponding R/C/I restriction.
Presence is checked separately from vehicle spawning. Sources: US
`03:bf0f..bf63`, `03:8b10`, `03:ab93..abf5`.

## Every ordinary placement item

Pollution figures below are **raw per-tile contributions**, before 2x2
aggregation and smoothing; they are not the final neighboring pollution value.
Native pollution uses two smoothing passes. Sources: US `03:9c11..9fa6`,
`sc_land_summary.h:ScLandTilePollution`.

| Item | Footprint | Effects on neighbors and city | Placement implication |
| --- | --- | --- | --- |
| Bulldozer | Selected tiles/footprint | Removes that item's housing/jobs, amenities, power continuity, transport or service contribution. No independent growth bonus. | Avoid breaking a shared power crossing or the only route to jobs. Removing a gift removes its aura. |
| Road | 1 tile, joins neighbors | Carries commuting and traffic. Clear/light road has zero raw pollution; congested artwork contributes 10 or 25 per tile. Funded upkeep controls deterioration. | Useful access, but avoid congested road beside R/C. A road segment touching a zone is not proof of a successful commute. |
| Railroad | 1 tile, joins neighbors | Carries commuting without road congestion pollution. Ordinary rail has zero raw pollution. No train-station gift is required for it to work. | Preferred local transport for maximum growth. Supply nearby destinations and avoid needlessly long/winding routes. |
| Power line | 1 tile, joins/crosses | Conducts electricity; no housing, jobs, population-density or terrain-value bonus. Proper road/rail crossings conduct power. Ordinary bare rail/road does not. | Connect separated building groups, park breaks and transport crossings. Adjacent zones conduct through their footprints. |
| Park | 1 tile | Natural-amenity contribution 15, no raw pollution or population. Helps land value through the terrain field; no direct police, fire or job effect. | Fill buffers and green plots near weak R/C. It cannot alone satisfy the density-65 apartment gate. |
| Residential | 3x3 | Supplies residents/workers and density once occupied. No raw pollution. Needs power, demand, a successful commute, adequate land and density. | Favor protected inner neighborhoods; arrange same-type pairs. Keep reachable local shops even inside the residential district. |
| Commercial | 3x3 | Supplies jobs, HUD population and a strong density contribution once occupied. No raw pollution. Center distance affects desirability; land value limits capacity. | Use the transition band and local shopping lots among housing. Pair C lots if aiming for C-TOPs. |
| Industrial | 3x3 | Supplies jobs/population and density. Developed industrial artwork contributes 50 per tile; the empty I footprint does not. This depresses nearby land and R growth. | Place on the global outside edge, with a green/commercial buffer toward housing. Do not repeat an industrial corner inside each residential district. |
| Police station | 3x3 | Funded, power/access-dependent crime reduction. No job-zone population or direct R/C demand bonus; no raw pollution. | Cover high-density and low-land-value areas. The native service map, rather than visual distance alone, determines coverage. |
| Fire station | 3x3 | Funded, power/access-dependent fire protection. No direct land-value/density bonus or normal demand increase; no raw pollution. | Cover the developed city and disaster-sensitive facilities. Center-of-block placement without nearby transport loses half strength. |
| Football stadium | 4x4 | Presence removes the R demand cap. Counts without a special waterfront requirement; no raw pollution. Open/closed native variants have different behavior. | Keep one intact and powered. Theme domes on Mars/Venus/Moon alter graphics, not its rules. |
| Seaport | 4x4 | Presence removes the I demand cap. Raw pollution 60 per tile. **Works for this purpose on dry land**; boats separately need suitable edge water. | Put on an industrial edge/corner, away from housing. A water-free test city still needs a seaport. |
| Airport | 6x6 | Presence removes the C demand cap. Airport-class artwork contributes 60 per tile; one footprint tile is outside that classifier. Powered airports can spawn aircraft. | Put at an edge/corner away from R/C. More airports do not multiply the demand-cap benefit. |
| Coal plant | 4x4 | Generates 700 conductor visits of power capacity; raw pollution 60 per tile. No ordinary zone population. | Cheap initial power with a pollution cost. Isolate from housing and ensure the grid reaches it. |
| Nuclear plant | 4x4 | Generates 2000 conductor visits of capacity; zero raw pollution. Native disaster/meltdown rules remain. | Cleaner high-density power. Count conductive tiles/visits, not just owner-zone centers, when checking capacity. |
| Gift selector | Selected gift | Its effect depends on the particular gift below; the toolbox itself contributes nothing. | Choose the gift intentionally; there is no universal employment multiplier. |
| Query / land inspection | No placement | Reports conditions without changing them. | Use density, land, pollution, power and transport together when diagnosing stalled zones. |

Natural forests, water and shores also supply terrain amenity through their
tile IDs. They have no resident capacity. Bulldozing them to bare ground can
reduce nearby land value. Terrain themes and seasons retain these rules;
Moon removes natural forests but player-built parks still work.

## Every gift, including its actual income

All physical gifts below have a 3x3 footprint and the shared gift-amenity
behavior unless stated otherwise. All have zero raw pollution. None creates
ordinary residential/commercial/industrial capacity merely by being a gift.
Annual special income comes from US `03:ae64..ae9d`, not from a guessed bonus.

| Gift | Normal quantity | Neighbor/city effects beyond shared amenities | Annual special income |
| --- | ---: | --- | ---: |
| Mayor's house | 1 | Initial artwork has weaker terrain coverage; native rank-related upgrades expand the qualifying artwork. Does not establish the city center. | $0 |
| Bank | 1 | Enables native borrowing/repayment. No special job or density multiplier. | $0 |
| Amusement park | Shared pool of 5 with casinos | Amenity without the casino's citywide crime penalty. The roads and school/hospital award paths share this pool. | **$200** |
| Casino | Same shared pool | Adds **one citywide crime point per casino**, in addition to its amenities. | **$300** |
| Land reclamation / landfill | Up to 9 | Converts a 3x3 water patch to buildable land; leaves no gift owner or continuing gift aura. Useless on water-free 31337. | $0 |
| Zoo | 2 | Shared amenities; no industrial-pollution removal or employment multiplier. | $100 |
| Police HQ | 3 | Doubles funded police strength before power/access penalties; most artwork also has gift amenities. Counts as a police station. | $0 |
| Fire HQ | 3 | Doubles funded fire strength before power/access penalties; most artwork also has gift amenities. Counts as a fire station. | $0 |
| Fountain | 1, from 1950 | Shared amenities; no power or job production. | $100 |
| Mario statue | 1 | Shared amenities, with no extra resident capacity or independent R demand addition. | $0 |
| Expo | 1 | Shared amenities. Does not erase industrial pollution or supply extra I capacity. | $100 |
| Windmill | 2 | Shared amenities. **Not an electrical generator** in the source. | $100 |
| Library | 3 | Shared amenities. No separate education, worker-skill or density multiplier. | $100 |
| Large park | 3 | Gift amenities, stronger than an ordinary 3x3 cluster of park tiles. | $0 |
| Train station | 2 | Gift amenities. No added rail transport graph or train-capacity multiplier; ordinary connected rails already work. | $100 |

Gift quantities/award state: US `03:c0ad..c392`; artwork and income:
`03:ae25..ae9d`. The generated test city deliberately starts with infrastructure
and the **27 available eventual gifts before 1950**, choosing amusement parks
instead of casinos. These are normal finite award quantities; it is not a city
that earned every gift at zero population. The fountain can arrive later.

Hospitals and schools are native random conversions of residential lots,
not manual placement tools. Their conversion temporarily removes that lot's
residential capacity; these versions have no independent modern education or
health-growth simulation. Their counts participate in native awards. See US
`03:9495..9566`, `03:c1a1..c1ea`.

### Gift placement, rather than gift stacking

A 3x3 gift entirely inside one 4x4 terrain cell wastes potential coverage.
Across both boundaries, it reaches four terrain cells. Native 2x2 accumulation
and tile order also affect the result, so the source calculation and actual
neighboring fields are more reliable than a drawn radius.

In the isolated native placement audit, a typical gift at `(40,40)` produced
terrain-field sum 250 over five nonzero cells. At `(43,43)`, the same footprint
produced 1012 over twelve cells. These are **sums of a terrain field**, not
1012 land-value points for a building, and are not a guarantee in an existing
city. The mayor's initial house differs. The generator selects park plots by
scoring the actual packed/diffused terrain before and after each candidate,
including previous gifts. Sampling visits all four grid alignments; overlapping
auras have diminishing value rather than an invented fixed penalty. HQs retain
perimeter transport access.

## Applying the guide to Test City 3

The generator now uses global edge industry, a greener commercial transition,
and residential inner districts with nearby local commerce. Native 3x3 lots
are paired and surrounded by rail with power crossings. Ordinary police/fire
coverage and nuclear supply repeat at native service distances; scarce gifts
do not repeat thousands of times across large maps. A stadium, dry-land port
and airport lift all three cap conditions. Terrain, pollution, density and
service fields are initialized from the actual finished map, including gifts,
rather than copied from an unrelated repeated prototype.

Test taxes are 3%, with full service funding and native upkeep. R/C/I start
empty with **zero population**, and normal growth/RNG/commuting remain active.
See [TEST_CITY_LAYOUT.md](TEST_CITY_LAYOUT.md) for current counts and measured
longitudinal comparisons. Existing City 3 saves keep their own city layout;
this redesign applies to newly generated test cities.

## What was checked against published SNES advice

The original-SNES recommendations to put pollution on the outside, pair R/C
lots, prefer rail/nuclear power, and keep dry-land ports agree with the
cartridge checks. [Peter's SNES guide](https://peterthedj.com/simcity-snes/).

The hidden 4x4 grid and gift-placement advice are useful; their effects here
were independently measured using native routines.
[Brian Sulpher's SNES strategy guide](https://gamefaqs.gamespot.com/snes/588657-simcity/faqs/20252).

Published claims about depot requirements, universal gift income, service
access and gift-specific education/power bonuses should be checked against
the source instead of applied by analogy to other SimCity versions.
[FatRatKnight's source-oriented gift list](https://gamefaqs.gamespot.com/snes/588657-simcity/faqs/68878),
[PrinceMercury's placement guide](https://gamefaqs.gamespot.com/snes/588657-simcity/faqs/46394),
[Cyan_of_Ages' optimized-city guide](https://gamefaqs.gamespot.com/snes/588657-simcity/faqs/69198).

## Reproducible checks for future development

- Locate the relevant original-US routine in locally generated
  `src/program_gen/sc_program_bank03*.c` (requires the user's verified ROM),
  then check its host adaptations in the files named above. Do not distribute
  ROM data or generated cartridge source with this guide.
- `UrbanRecompCityGrowthTest --effects` audits every ordinary placement and
  all 15 gifts on an otherwise empty stock map, including alignment, terrain,
  raw pollution, native special income and the casino crime increment.
- The same executable runs complete native simulation ticks from an empty
  generated city. Compare multiple years, not an artificially mature fixture:
  population, completely empty houses, low-density gates, pollution, land
  deficits, power, demand and treasury. Native advice-cap checks stay enabled.
- The controlled comparison suppresses modal presentation and disasters only.
  Production UI replays separately verify actual calendar/entry/scheduling.
  It is not a disaster-survival or proof-of-global-optimum test.
- Generation tests cover every size, full footprints, zero initial residents,
  power, native transport access, global industrial placement, finite gifts,
  spatial fields and save-slot preservation.

A theoretical capacity, a successful short growth burst, or a city with
artificially filled commercial buildings cannot establish a sustainable peak.
Record the duration and conditions of every measured population claim.
