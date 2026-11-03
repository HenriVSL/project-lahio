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
