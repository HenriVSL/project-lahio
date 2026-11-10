# Project Lähiö 1.2 "Power" (spec)

Source: play-test session 3 (`docs/PLAYTEST.md`). The user's Koivukoski (1996, Regional centre, 58,193 pop, Normal) ran on
**32 coal plants** (14,307 / 16,000 units). Coal plants give 500 units; oil 900 (art = coal alias); peat and nuclear had
no art (variants 0, hidden), and nuclear needed City (100k). Reference saves: `Shared/release/1.1rc1 save/` (AUTO1.KAU).
Small, contained update: three new buildings with art, one decision letter, rebalance, advice lines. Nothing else.

## 1. Goals
- A 60k city runs on **6-10 plants**, not 32. Big plants are cheaper per unit but lumpier (bigger upfront cost, smoke,
  footprint), so the choice is a trade-off, not a strict upgrade.
- Every era has a sensible power option: small coal (1955, village) → large coal station (1962) → oil (1965) → peat (1972,
  domestic fuel) → nuclear (1977, Regional centre).
- The player learns it in game: the Cabinet column suggests the next plant; hindsight line when brownouts hit with only
  small plants; Guide (F4) power page lists the plants and their units.
- **Compatibility:** old 1.0/1.1 saves load unchanged. Existing plant rows (coal, hydro, oil) keep their numbers, so a game
  that never builds the new plants plays exactly like 1.1 (the 1.1 balance runs and world hashes stay the regression
  test, with the bots told not to build new plants in those runs). New ids are **appended** to the building enum
  (before `B_COUNT`) so saved def ids do not shift.

## 2. Plants (starting numbers; B1 tunes them, logs in BALANCE.md)
| Plant | Key | Size | Unlock | Cost | Upkeep/mo | Units | Notes |
|---|---|---|---|---|---|---|---|
| Coal power plant | coal_plant | 2x2 | Village 1955 | 2,500 | 110 | 500 | unchanged |
| Hydro power plant | hydro_plant | 2x2 | rapids | 6,000 | 40 | 600 | unchanged |
| **Coal power station** (new) | coal_station | 3x3 | Town 1962 | ~9,000 | ~230 | **2,000** | heavy smoke (2 chimneys), ~4x coal surcharge from 1974 (scales with units), ~80 jobs |
| Oil power plant | oil_plant | 2x2 | 1965 | 5,000 | 100 | 900 | unchanged numbers; **own art** (tank farm) if VRAM allows, else stays a coal alias |
| **Peat power plant** | peat_plant | 2x2 | Market town 1972 | ~5,000 | ~100 | **1,000** | domestic fuel: no oil index, no coal surcharge; light smoke; Rural bloc approval (small, steady) |
| **Nuclear power plant** | nuclear_plant | 3x3 | **Regional centre 1977** | ~30,000 | ~450 | **5,000** | no smoke, must touch water (BDF_NEEDS_SHORE: cooling), ~150 jobs, the reactor letter (3) |

Unit cost check (upkeep per 1,000 units): small coal 220, station 115, peat ~100, nuclear ~90 (plus the price of the
letter's choice). A 60k town (~16,000 units): 8 stations, or 3 nuclear + 1-2 stations/peat.

## 3. The reactor letter ("Eastinghouse")
The first time the player opens the nuclear plant tool (unlocked), a decision letter arrives before placement (existing
letter/decree UI, ≤ 1 card per game year rule does not block it: it is a build gate, not a random card). The choice is
stored in the save and applies to every nuclear plant in this game. Dry humour, English with no new Finnish words.
1. **Eastern reactor** (VVER from the friendly neighbour): cheapest (cost -30%), paid through the clearing account
   (adds an import to the clearing balance over the build months), Left bloc and Moscow relations pleased, Right grumbles.
2. **Western reactor** ("Eastinghouse Electric", Pittsburgh): full price in markka, Right bloc and business pleased,
   Moscow "expresses interest" (small authority/relations cost), shorter build time.
3. **The Loviisa compromise** (Eastern reactor in a Western containment, Western control room): price between the two,
   both blocs mildly content, longest build time; the newspaper calls it "a very Finnish solution".
The player can re-read the choice in the plant's query panel. Hindsight/news lines may reference it later (one or two).

## 4. Advice, Guide, UI
- Cabinet advice (newspaper): when power headroom < ~10% and a bigger plant is unlocked and affordable, "The Government
  advises the President to build a coal power station" (or peat after an oil shock, nuclear at Regional centre).
- Hindsight line: brownout while the town has ≥ 6 small coal plants and the station was unlocked.
- Guide F4 power page: table of plants (units, unlock). Power plant query panel already shows capacity/left (PL2).
- Tool menu order in the Power group: coal plant, coal station, oil, peat, hydro, nuclear (locked ones show the unlock).

## 5. Art (W/A task)
Same pipeline as before (gen_art, ray-cast G-buffer, Nordic hybrid palette, not too colourful), both zooms, summer + winter
(snow pass, same layout), all rotations the def needs (or BDF_NO_ROTATE if symmetric). Fit the PL4 512x256 pages and the
40 MB heap budget; report VRAM delta.
- **Coal power station 3x3:** brick/concrete turbine hall, boiler house, two tall chimneys (smoke hotspots), coal yard with
  dark heaps, conveyor bridge. Clearly bigger than the 2x2 coal plant.
- **Peat power plant 2x2:** low boiler hall, one chimney, rust-brown peat stacks under a shed roof, conveyor.
- **Nuclear power plant 3x3:** Loviisa-like: tall pale-grey rectangular reactor hall(s) with a domed/rounded containment
  top, long turbine hall, no cooling towers (sea/lake cooled), a small ventilation stack. Must read as "nuclear" at 32x16.
- **Oil power plant 2x2 (stretch):** boiler hall + one chimney + two round oil tanks, replacing the coal alias.

## 6. Out of scope
New power mechanics (grid capacity of lines, power trading), new disasters (no meltdowns), sound, story.
