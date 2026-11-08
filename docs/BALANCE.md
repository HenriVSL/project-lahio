# KAUPUNKI balance (T12, Milestone 1)

How the vertical slice is tuned, how to re-check it, and what is still open. All numbers come from headless
text runs on the Mac (same interpreter as the game's `-script`), never from screenshots.

## 1. How to run it

```
make -f tests/Makefile.mac T=balance        # 8 tests, ~2.5 s: 14 bot runs of 20 years + extra scenarios
```
Outputs in `tests/out/`:
- `balance_summary.txt`: one line per style x map (pop at years 1/3/5/10/20, months to 500/3,000/10,000,
  money at year 2 / end / minimum, emergency loans, operating net at years 10 and 20, worst yearly pop drop).
- `balance_<style>_<map>.txt`: milestones, the service buildings at the end, and a year table: pop, money,
  operating net per month (12-month mean, construction and status rewards excluded), demand bars R/C/I,
  jobs, unemployed, emigrated that year, homeless, house lots T1/T2/T3, apartment lots, commercial and
  industry lots T1/T2+, abandoned, status, traffic (mean level on town roads / max), top problem (lots).
- `bal_<style>_<map>.txt`: the run recorded as a replayable script (snapshots in `scripts/`). Replay in the
  VM with `lahio.exe -seed 7 -preset lakeland -script scripts\bal_planner_s7.txt -fast`; it writes the
  monthly CSV (`statlog`) and a final `dump report`. Replay on the Mac reproduces the test run exactly.

### The players are bots, not fixed scripts
Fixed scripts broke on every constant change (a build denied for 200 mk shifted the whole run). The test
bots look at the city each month like a player and issue ordinary tool commands through
`tools_script_line` (the mouse's code path):
- utilities ahead of demand: a coal plant when power demand >= 80% of supply or lots are short, a well
  when water demand >= 80%;
- services by load (>= 90% of capacity) and by the top problems (a sauna/school/clinic in the
  neighbourhood the problem names); first clinic at 700 residents, first fire station at 900;
- a new neighbourhood module (29 x 22 tiles, streets every 7 rows, power lines along them, school +
  sauna + 2 wells, a commercial strip) when fewer than 50 house tiles are left and cash >= 6,000;
  the second industrial band when I demand > 20; a commercial block when C demand stays > 35;
  a water tower at market town; parks only when cash > 30,000; reserves 1,000 mk after services.

Styles: **planner** (above), **greedy** (zones, power, water, never a service), **industry** (the third
neighbourhood zoned industry, the estate early), **dense** (neighbourhoods built from 1960 zoned as
apartments), **tax13 / tax5** (planner at 13% / 5%). Maps: **s7** Lakeland seed 7 (the town1 map),
**c3** Coast seed 3, **r30** River town seed 30; module origins per map in `tests/test_balance.cpp`.

## 2. Results (final constants, Normal funds, start 1955)

Milestones (months from the start; goal: first houses ~10 s at speed 2 = 60 steps, 500 in ~1 year, 3,000 in
3-4 years, 10,000 in 8-12 years):

| run | first lot | 500 | 3,000 | 10,000 | money y2 | min money | loans | end money | opnet y10 / y20 |
|---|---|---|---|---|---|---|---|---|---|
| planner s7 | 32 steps (5.3 s) | 7 | 36 | 141 | 10,873 | -77 | 0 | 60,445 | +428 / +253 |
| planner c3 | 32 | 7 | 40 | 105 | 11,506 | -695 | 1 | 118,706 | +569 / +504 |
| planner r30 | 32 | 7 | 42 | 126 | 11,877 | -1,129 | 1 | 97,934 | +319 / +655 |
| greedy s7 / c3 / r30 | 32 | 8-10 | 81-94 | never | ~18,000 | 12,400+ | 0 | 15-37k | +19..+113 / -96..+119 |
| industry s7 | 32 | 7 | 36 | (8.9k plateau) | 10,873 | -77 | 0 | 46,057 | +432 / +127 |
| dense s7 / c3 / r30 | 32 | 7 | 36-42 | -, 110, 132 | ~11,000 | -1,129 | 0-1 | 50-108k | +74..+445 / +143..+445 |
| tax13 s7 | 32 | 8 | 83 | never (5.9k) | 16,227 | 374 | 0 | 11,578 | -140 / +18 |
| tax5 s7 | 32 | 6 | 27 | 60 | 1,488 | -168k | 3 (cap) | -168,329 | -1,316 / -1,449 |

Year table, planner s7 (condensed; `balance_planner_s7.txt` has every year):

| year | pop | money | opnet/m | R/C/I | unemp | emig | houses T1/T2/T3 | com | ind | status | traffic avg/max |
|---|---|---|---|---|---|---|---|---|---|---|---|
| 1956 | 900 | 15,788 | -102 | 21/3/-11 | 0 | 0 | 136/50/33 | 28 | 8 | church village | 1.2/31 |
| 1957 | 1,705 | 10,873 | 0 | 6/37/0 | 0 | 7 | 151/32/135 | 49 | 15 | church village | 2.2/43 |
| 1958 | 3,007 | 7,729 | -241 | 12/5/1 | 0 | 0 | 356/68/207 | 90 | 24 | market town | 2.9/50 |
| 1960 | 4,637 | 422 | +99 | 4/67/-11 | 79 | 141 | 294/51/435 | 94 | 39 | market town | 5.6/91 |
| 1962 | 5,619 | 2,423 | +378 | 3/78/3 | 145 | 329 | 98/29/649 | 94 | 48 | market town | 7.9/92 |
| 1965 | 8,254 | 2,505 | +428 | 3/75/22 | 160 | 336 | 163/45/944 | 129 | 53 | market town | 10.9/96 |
| 1970 | 11,756 | 35,286 | +655 | 7/79/-1 | 117 | 398 | 16/47/1432 | 169 | 53 | town | 16.6/160 |
| 1975 | 11,756 | 60,445 | +253 | 0/79/-1 | 117 | 360 | 16/47/1432 | 169 | 53 | town | 20.4/196 |

Budget of that town (last month of the year): 1956 income 530 (res 124, business 95, export 23, grant 288)
vs 555 upkeep; 1960: 1,641 vs 1,430; 1970: 4,547 (res 3,012, business 1,199, export 336) vs 3,882 (services
1,837, utilities 1,544, roads and lines 467, special 34), i.e. 0.39 mk income and 0.33 mk upkeep per resident.
Service buildings at 11,756: 8 schools (1 per 1,470), 9 saunas (1 per 1,300), 4 clinics and 4 fire stations
(1 per 2,940), 10 coal plants, 26 wells.

Greedy s7: all 1,656 house lots stay tier 1 (top problem "school" on every lot), 4,968 residents in 1970,
money flat at ~20,000, operating net -96..+113 (not a money machine). It reaches market town in year 7.

Scenarios (`balance_first_minutes`, `balance_failure_and_recovery`):
- **First minutes** (one street off the national road at the town site, 120 house tiles, a corner shop,
  workshops across the road; month 2 a plant + 2 wells, month 3 a school and sauna, year 2 a second
  street): first lot after 32 steps, money after street + zones 24,596 (plant + pump + school affordable),
  pop 498 at month 12 and 1,038 at month 24, money 24,681 at month 24 (+166/month).
  A houses-only start without any jobs stalls at ~200-300 and sends people to Sweden (readable).
- **Job shock** (1967, every industry lot bulldozed and the estate dezoned): pop 10,577 -> 4,648 in two
  years, 9,718 emigrated to Sweden, 0 abandoned lots, no spiral (the equilibrium is ~2 x jobs + the rural
  pull). Re-zoning the estate: back to 10,791 in four years.
- **Power cut** (all plants bulldozed for a year): every tier-3 house falls to tier 1 within two months
  (10,791 -> 4,509 residents); rebuilding: 7,287 / 9,295 / 10,080 / 10,354 after 6 / 12 / 24 / 36 months.
- Tax: 11% (manual check) reaches 3,000 later (52 months) but 10,000 sooner (128) and ends twice as rich;
  13% halves growth and ends poorer than 9%; 5% doubles early growth and goes bankrupt.

## 3. Constants changed (old -> new) and why

`src/defs.cpp`
| item | old | new | why |
|---|---|---|---|
| Cottage (house T1) power / water use | 1 / 1 | 0 / 0 | a 1955 cottage village needs no plant; tier 2 ("power, water") is the step that needs utilities. Cottages were power hogs (1 unit per 3 residents) and early brownouts stopped tier 2 everywhere |
| Coal plant cost / upkeep / capacity | 3,000 / 80 / 400 | 2,500 / 110 / 500 | affordable early (goal 1); ~1 plant per 1,200 residents; power is ~40% of a big town's upkeep |
| Well / pump upkeep | 6 / 15 | 6 / 20 | |
| School upkeep / capacity | 50 / 400 pupils | 100 / 500 pupils (2,500 residents) | one per neighbourhood of ~2,500 (goal 5); services were too cheap to matter (late-game richness) |
| Clinic upkeep / capacity | 40 / 2,000 | 70 / 2,500 | same rhythm |
| Fire station upkeep / capacity | 30 / 1,500 | 50 / 2,500 | same rhythm |
| Public sauna cost / upkeep / capacity / reach | 300 / 15 / 600 / 15 road tiles | 400 / 25 / 1,500 / 25 | the most common tier-1 blocker (T04): 600 meant a sauna per 100 houses; now 1-2 per neighbourhood |
| Houses tier 3 requirements | 8 items incl. phone | 7 (no REQ_PHONE) | the post office has no art in M1 (variants 0, tool hidden): tier 3 was unreachable. Re-add in M2 |

`src/budget.cpp` (+ two defines in `budget.h`)
| item | old | new | why |
|---|---|---|---|
| Resident tax | occ x (50 + LV) x tax / 4,000 | occ x (30 + LV) x weight x tax / 17,000, weight houses T1/T2/T3 = 1/4/6, apartments 4/6/9 | per-capita income now depends on tier and land value (cottage ~0.04 mk/month, tier 3 ~0.27 at 9%): services pay back through upgrades, a service-less cottage sprawl doesn't |
| State grant | 200 - pop/20 | 400 - pop/8 (fades at 3,200) | carries the cottage-village phase |
| Normal / Austere starting funds | 20,000 / 8,000 | 25,000 / 10,000 | the first neighbourhood + plant + water cost ~10,000 |
| Upkeep index | none | 100% to 1959, +2.5%/year from 1960, cap 150% (1980) | the welfare-state years get dearer: a built-out town no longer piles up money unless it keeps developing or taxes more |
| LOAN_RATE (budget.h) | 20 per mille/month | 8 | 20 compounded into a 1.3 million mk debt spiral |
| LOAN_MAX | none | 40,000 | no new emergency loan above this debt; the treasury stays negative instead |

`src/sim.cpp`: rural pull 1955-59 80 -> 70 (500 at month 7 instead of 5-6); **death-spiral guard**: an
active lot's growth score never sees a demand term below -6 (`SIM_DECLINE_DEMAND_FLOOR`). Negative demand
still stops new building (allowance), but no longer empties existing lots by itself. Without it a power
shortage in the dense run started vacancy -> negative R -> decline -> more vacancy (and the same for jobs
through I/C) and took the town from 1,700 to 0 with 928 abandoned lots and a 475,000 mk loan.

`src/traffic.cpp`: `TRAFFIC_TRIPS` = 5 (road trips per worker per day on the road tree; was 1 = commuters
only). A 9,000 town's busiest street went from level 17 to ~85-95 of 128; 1975 trunks congest (160-255).

## 4. Bug fixes

- **world.cpp memory corruption** (T04 file, one-line fix): `batch_add` writes `s_brect[WB_MAX]` as the
  "one slot too many" temporary, but the array had WB_MAX entries. With more than 8 dirty rects in one
  growth batch (every bigger town) it overwrote the next globals; in the Mac replay harness that was the
  budget (taxes in the thousands, service funding 0). Found with AddressSanitizer; now `s_brect[WB_MAX + 1]`.
  The whole test suite is clean under ASan + UBSan. **The VC6 build may have been corrupting whatever
  follows `s_brect` in memory.**
- **"homeless -1"**: the month-end emigration can shrink the homeless pool below the cycle's planned
  re-housing; move-in now takes `min(planned, pool)`. No negative homeless in any CSV.
- Death spiral and loan spiral: see section 3.

## 5. Remaining concerns

- **Built-out plateau still accumulates money** (60-120k by 1975 at +250..+650/month): the bots stop at four
  neighbourhoods. A real player keeps expanding; the upkeep index slowly closes the gap (150% in 1980).
  M2 needs money sinks (landmarks, decrees) more than a tighter formula.
- **Mid-game crunch**: 1958-1962 the planner sits at a few hundred mk (one bot dips to -1,129 and takes
  one emergency loan). That is intended tension, but a careless player can stall there for a year.
- **Persistent unemployment / emigration at the plateau** (100-240 unemployed, 300-700 emigrants a year)
  with C demand ~80: the bots don't zone enough commercial. Readable, but the C demand bar stays high.
- **Tax curve** is steep above +2 points (13% halves growth); 11% is a fair "slower but richer" choice.
- **Power cut** knocks every tier-3 house to tier 1 in two months (decline steps every 4 bad checks);
  recovery takes 1-3 years. Consider slower decline for utilities (SIM_G_DOWN 4 -> 8) in M2.
- **Cosmetic cars** (life.cpp, T11): `FLOW_FLOOR 12` dominates the car count, so the view shows ~17 cars in
  1956, ~27 in 1965, ~41 in 1975 (estimate from the life formula with the new flows). For "plenty in 1965"
  scale the floor with ownership (`12 * lc_own_pct(year) / 50`): ~9 / ~29 / ~51.
- Saunas end up 1 per 1,300 because of reach (25 road tiles), schools 1 per 1,500-2,000 (tier 3 = 20%
  pupils of 8 residents per tile); clinic/fire 1 per 2,900. Water: wells only (bots): 26 wells for 12k.
- `scripts/town1.txt` (T08) over-builds services and ends at 887 mk; any tighter economy will break its
  "0 errors" test again (its year-2 well was moved to year 3).
- Not verified in the VM this time (the lock was held by another session): run
  `lahio.exe -seed 7 -preset lakeland -script scripts\bal_planner_s7.txt -fast` and compare
  `bal_planner_s7.csv` with `tests/out/balance_planner_s7.txt` (VC6 = clang was verified by T08/T10).

## 6. Notes for other tasks

- **T09 UI**: Query's `LotInfo.upkeep` is the base upkeep x funding; the budget lines include the upkeep
  index (`upkeep_index()` in budget.cpp, static). Export it (e.g. `budget_upkeep_index()`) and show
  "wage level 125%" in the budget panel; the resident tax tooltip should mention the tier weights.
- **T11 life**: see the car floor above.
- **T13**: balance runs are cheap (14 x 20 years in 2.5 s on the Mac); traffic levels are now 5x higher,
  which doesn't change traffic cost (same BFS), only `FLD_TRAFFIC` values (road pollution up to x1.25).

## 7. Recommendations for Milestone 2 (politics, eras)

- Re-add REQ_PHONE for houses tier 3 together with the post office art, and give apartments tier 2/3 art:
  apartments T1 only (25 residents/tile) are the dense ceiling today.
- Money sinks for built-out towns: landmarks (concert hall, TV tower), decrees with monthly costs, the
  presidential sauna. Make the status rewards decrees with a choice (money vs authority).
- Era income: `spend_pct` grows with the era; let business tax and export grow with the clearing trade
  (oil shock) so the upkeep index has an income counterpart and the 1980s don't turn into pure austerity.
- Elections: tie Workers approval to the emigration counter (already a readable number) and Rural approval
  to tier-1 houses being bulldozed.
- Keep the bot test: add a "politics on" style and assert that decrees never make the planner's books
  structurally negative at 9%.

## 8. P1 pre-play-test pass (2026-11-03): 1955-1990 runs

`make -f tests/Makefile.mac T=balance_era` (~5 s) plays planner, politics, dense, mill and metal 1955-1990 on s7 and
writes `tests/out/balance_era_<style>_s7.txt`: a yearly row (pop, money, operating net, emergency loan outstanding
(taken), house tiers, **apartment tiers 1/2/3**, commercial / industrial lots, status, cards that went up that year +
campaign cards, upkeep index, fuel, element output, clearing balance, headline), the yearly ledger per line, the
apartment blockers at the end and the civic/era/trade dumps. The planner's run is also recorded as
`scripts/bal_planner_s7_era.txt` (VM replay = Mac: 1990 pop 11,696, money 12,896). The dense run saves its 1990 town as
`tests/out/P1_DENSE.KAU` (heat-overlay / lahio screenshots).

| constant | old | new | why |
|---|---|---|---|
| Apartments T2/T3 upgrade (sim.cpp `apt_upgrade_ok`) | needed R allowance >= +60/+80 in one band | own checklist + full block + site score (a negative R bar doesn't count) + town vacancy <= 4% (min 120); the element pool is the cap | late towns never have that allowance: 1 T2 block by 1975 |
| Element factory (defs.cpp) | Town, 1960, 8,000 mk | Market town, 1962, 5,000 mk | towns below 10,000 (most dense towns) never got one |
| Apartments T3 land value | 50 | 40 | towers stood on cheap edge land; heat is the real T3 gate |
| Upkeep index cap (budget.cpp `UPKEEP_INFL_CAP`) | 150% (1980) | 130% (1972) | M5-M7 costs + fuel: built-out planner lost ~900 mk/month in the 1980s (-101k, 3 loans) |
| Apartment tax weights T1/T2/T3 | 4/6/9 | 4/5/7 | with flats now upgrading, dense towns piled up 180k+ by 1975 |

Results (s7, Normal funds; money in mk on 1 January; L = emergency loans taken; apartments T2/T3):

| run | 1960 | 1965 | 1970 | 1975 | 1980 | 1985 | 1990 | min | L | apt T2/T3 1975 |
|---|---|---|---|---|---|---|---|---|---|---|
| planner | 798 | 2,574 | 40,761 | 30,142 | 21,523 | 13,026 | 12,896 | 52 | 0 | - (houses only) |
| politics | 3,717 | 26,082 | 42,506 | 29,652 | 11,763 | 4,902 | 7,110 | 44 | 0 | - |
| dense | 798 | 8,343 | 37,747 | 48,234 | 68,945 | 87,108 | 106,950 | 81 | 0 | 15/19 (first T2 1966, T3 1967) |
| mill | 1,537 | 33,599 | 52,047 | 54,765 | 56,287 | 57,328 | 64,473 | 139 | 0 | - |
| metal | 1,836 | 4,676 | 27,011 | 31,385 | 23,252 | 15,289 | 13,510 | 61 | 0 | - |

A built-out planner town runs about -140 mk/month during the oil years (fuel ~210 mk/month 1975-86) and turns positive
after the 1986 oil collapse. Dense (apartment) towns stay richer: fewer service buildings per resident. Cards: at most one
per year from all sources (+ the election campaign card in election years), ~24 in 35 years for the politics bot.
The 20-year milestones (section 2) are unchanged up to 1972; 1973-75 rows are slightly better (index 130 vs 137.5).

## 9. PL2 polish pass (2026-11-04): play-test 2 items 7, 9, 10

New bot style **naive** (`tests/test_balance.cpp` `bot_naive`): a first-time human as in PLAYTEST 2 item 7. Houses and
apartments in turn, a small shop corner per neighbourhood (another small block after a year of C demand > 60), a plant
only after 3 months of brownouts, a well after 2 dry months, services only after the checklist names them for 100+
residents for 3 months or a service runs full for 6 months, the element factory 18 months after blocks start waiting for
it, politics on (cards answered after 3 months with the first choice, town rights signed a year late, no other decree),
one tax notch when the treasury has been empty for 3 months. `balance_naive_skips_element_factory` plays the same
player without the factory at all. All 11 styles now also run 1955-1990 (`tests/out/balance_era_summary.txt`).

The same naive bot on the **pre-PL2 rules** (scratch copy, planner/dense reproduce section 8 exactly): Market town 1964,
never Town (6,800 plateau), 4 emergency loans (46,700 owed), 313 months in debt, -6,615 mk in 1990. The first, stingier
version of the bot matched the play-test even closer: 4,900 plateau, -214k by 1990.

| change | old -> new | why |
|---|---|---|
| Water tower (defs.cpp) | Market town -> Church village | apartments T2 need its pressure; a T1-block town could not reach 3,000 |
| Element factory | Market town 1962 -> Church village 1960 | the other T2 need (the pool stays the lahio's cap: T2 still takes elements) |
| Shop customers (sim.cpp REQ_CUSTOMERS) | nearest-shop residents only | also met while C bar >= 40 (keep 20) and jobs filled >= 97%: back-row shops grow when shoppers can't find shops (human towns were job-limited at C +60..+80) |
| Utility-only decline (sim.cpp) | 4 bad checks | 8 (`SIM_G_DOWN_UTIL`): a brownout costs growth, not a district of tiers in one month |
| Service overtime (budget.cpp) | none | upkeep x (load/capacity - 25%), 100..300%: skipping the second fire station no longer saves money (dense/naive piled 130-250k) |
| Apartment tax weights | 4/5/7 | 3/4/5: rented flats; a block needs half the power, pipes and streets per resident |
| Emergency loan (budget.cpp/h) | 8 per mille, repaid only above 15,000 | 6 per mille, + an instalment of 1/60 (min 100) whenever the treasury holds 2,000+ |
| Agreement card (trade.cpp, politics.cpp) | Ministry signs after 6 months waiting | with politics on it waits up to 24 months and has the next card slot: 6 of 6 reach the desk (was 3 of 6) |
| Company town (complex.cpp/h) | complex jobs >= 40% of ALL jobs | >= 30% of FACTORY jobs (industry + logging + complexes); a paper company also counts the wood industry and the logging |
| PM2 (complex.cpp) | wood 80, staffed = 90% filled | wood 70, staffed = 80% (labour-short towns fill every employer alike) |
| `CityStats.jobs_zone` (sim.cpp) | never filled (always 0) | filled in aggregate: strike / trade-fair cards and the era's industry history work |

1955-1990 on s7 (January values; L = emergency loans, deepest debt, months with a loan or a negative treasury):

| style | 1960 | 1965 | 1975 | 1990 | money 1975 | 1990 | min | L | church/market/town | apt T2/T3 1975 |
|---|---|---|---|---|---|---|---|---|---|---|
| planner | 5,810 | 12,260 | 12,188 | 12,188 | 27,899 | 4,171 | 31 | 0 | 1956/1958/1964 | - |
| politics | 6,045 | 12,222 | 12,228 | 12,228 | 30,648 | 7,100 | 303 | 0 | 1956/1958/1962 | - |
| dense | 5,810 | 15,897 | 19,587 | 18,974 | 28,200 | 15,353 | 81 | 0 | 1956/1958/1964 | 66/27 |
| mill | 6,126 | 11,991 | 11,991 | 11,991 | 60,123 | 70,507 | 15 | 0 | 1956/1958/1963 | - |
| metal | 5,256 | 12,302 | 12,230 | 12,230 | 27,928 | -375 | -667 | 1 (10.7k) | 1956/1958/1965 | - |
| **naive** | 2,992 | 4,554 | 10,400 | 18,120 | 673 | 27,095 | -5,491 | 1 (10.4k) | 1957/1962/1976 | 56/0 |
| naive, no factory | | 5,482 | 7,879 | 10,389 | 12,111 | 6,218 | -1,811 | 2 (13.1k) | -/1962/1987 | 0/0 |
| greedy | 2,154 | 4,335 | 4,968 | 4,968 | 15,784 | 1,183 | -412 | 1 | 1956/1962/- | - |
| tax13 | 1,963 | 5,144 | 10,089 | 11,931 | 42,986 | 351,659 | 42 | 0 | 1956/1962/1975 | - |

Open: dense on c3/r30 (25-28k on 4 modules) still ends the 20-year run at 126-147k: half the plants and pipes per
resident. Levers if wanted: apartment power/water use, or district-heat fees. tax13 piles money once built out (the
money-for-growth trade; exempt from the 150k cap). Metal's oil-years deficit (-240/month) takes one late loan.

## 10. 1.1 difficulty levels (D1, 2026-11-08; B1 tunes across levels)

`make -f tests/Makefile.mac T=balance_levels` (~20 s; `LAHIO_LEVELS_ALL=1` adds c3/r30 x 3 levels, ~90 s) plays all 11
styles 1955-1990 on s7 at Normal and Hard: `tests/out/balance_levels_summary.txt` (one row per style x level, the
DESIGN_1_1 2.6 targets that are missed), `tests/out/balance_era_<style>_s7_<level>.txt` (year tables), replay scripts
`bal_<planner|naive>_s7_era_<level>.txt` (VM: `-seed 7 -preset lakeland -difficulty LEVEL -script ... -fast`). Easy = the
1.0 runs: `balance_era_1955_1990` checks the 11 v1.0 world hashes (planner 12,188 / 4,171 mk).

D1 changes against the design table (defs.cpp `g_diff`): **Hard upkeep x105 % (design 115), index cap 150 % / 1980 (160 /
1984), tax yield 95 % (90), overtime from 110 % (105)**; upkeep surcharge and yield cut **phase in 1960-65**; a new town's
taxes start at the level's tolerance (Hard 8 %); **no hard winter in a game's first two winters** (season.cpp). Reason:
with the design values every bot but politics/metal ended Hard stuck at the emergency-loan cap for 10-25 years.

Bot habits on Normal/Hard only (Easy keeps the 1.0 bots): each January a loss with < 6 months of expenses (or a negative
treasury) -> +1 % tax (max 13), a surplus with 18 (Hard 9) months banked -> -1 % (not below the tolerance); plants for the
winter peak (`season_peak_pct`), heat plants for the heat peak, wells for a forecast hard frost; no new neighbourhood in
October-November without a winter reserve; libraries/clinics a year before the expectations; libraries where the
checklist names them. Results s7 (VM B replays = Mac exactly for planner/naive x Normal/Hard and the Easy planner):

| style | Easy 1990 pop / mk | Normal | Hard |
|---|---|---|---|
| planner | 12,188 / 4,171, 0 L | 12,268 / 67,886, 0 L, Town 1965 | 11,790 / 37,387, 3 L (cleared), Town 1966 |
| politics | 12,228 / 7,100 | 12,066 / 46,331, 0 L | 12,020 / 19,089, 2 L, Town 1977 |
| dense | 18,974 / 15,353 | 17,951 / 57,949, 0 L | 15,167 / 20,331, 4 L |
| mill / rural | 11,991 / 12,222 | 12,038 / 11,684, <= 1 L | 11,870 / 11,813, 2-3 L |
| naive | 18,120 / 27,095, 1 L | 9,773 / -84,328, 6 L | 7,474 / -248,491, 5 L, at the cap |
| greedy / tax13 | 4,968 / 11,931 | 4,965 / 11,948 | 4,956 / 5,164, at the cap |

Open for B1: the naive bot (first-time human) does not recover on Normal or Hard (it expands with emergency-loan cash
and skimps services: overtime); industry/metal/greedy/tax13 sit at the loan cap on Hard; quiet years (no Cabinet warning
>= 500) Normal planner 13 of 15 (target <= 6): the planner-style bots coast on Normal after raising taxes; Sweden loss
naive Normal 3.1 % (target 4-8: sweden_permille 3?).
