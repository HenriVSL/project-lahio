# Project Lähiö 1.1 design: "Harder times" (binding for the 1.1 tasks)

Inputs: `PLAN_1_1.md` (agreed plan), DESIGN 0/5/14, SCOPE_REVIEW, BALANCE 8-9, PLAYTEST, THEME 3.2-3.3, DESIGN_LESSONS,
HANDOFF, the 1.0 sources (constant names below are the real ones), plus web research (section 6, cited [n]; single
forum threads, fan wikis and search-only excerpts are marked *(thin)*). Where this file conflicts with DESIGN.md, this
file wins; DESIGN section 15 gets a 10-line summary pointing here.
User feedback: 1.0 "works well on real hardware; it's a bit easy": **no real threats** and **late-game coasting**.
User values: small polished scope, plays muted (no sound work), likes the dry Finnish humour.

**Verdict.** Difficulty is a **preset over about 15 existing constants** (no new systems), plus three new pressures that
show on the map and in the papers: **winter peaks with announced hard winters**, the **1969-70 Sweden migration**, and
**two era expectations** (1975, 1980). Ploughing is a **winter surcharge on road upkeep**, not a service. Snow art reuses
the summer page layout exactly, so winter costs **0 KB extra VRAM** at every resolution and zoom.

## 1. What comparable games teach (research summary)
| Game | What it did | Reception | Rule for us |
|---|---|---|---|
| Cities: Skylines Snowfall | temperature lowers -> electricity/heat demand rises; plough vehicles haul snow to dumps, count set by the road budget; roads never fully block [2] | "when the temperature plummets, demand for electricity soars" [1]; but heating "feels superfluous", winter maps "a mostly visual change" [3] *(thin)*; "steam heating seems unnecessary", "same late game challenges" [4] *(thin)*; ploughs fun to watch, not demanding [5] *(thin)*; players: "ignore the snow stuff, pointless" [6] *(thin)* | Winter must change **a decision** (capacity margin, money), not add a vehicle chore. Heat that just mirrors power is "superfluous": our winter hits heat **and** water **and** the books, all on the existing networks |
| Frostpunk 1/2 | cold is the core threat; a temperature **forecast** lets you prepare; presets Easy-Extreme, weather severity a separate knob; FP2 presets scale resources, weather and "the demands of your city", locked after start [9][10] | Survivor/Extreme added for players who found Hard "too damn easy" [9] *(thin)* | **Telegraph** every cold spell (November forecast); scale weather by level; lock the level at New Game |
| Banished | Easy 6 families + supplies, Medium 5, Hard 4 with no seeds; winter kills the unhoused, firewood/coats [7]; starvation "brings you back down to sustainable levels" [8] *(thin)*; late game loses tension (DESIGN_LESSONS 2.7) | the first winter is the remembered challenge | Winter as a **yearly capacity exam** that grows with the city; failures are recoverable, never a collapse |
| SimCity classic (Micropolis source) | level scales tax income ×1.4/1.2/0.8, road cost ×0.7/0.9/1.2, external market ×1.2/1.1/0.98, shifts the tax-penalty table by +level, disaster chance 1/480, 1/240, 1/60 [11] | the reference "classic difficulty" | Proven levers: **income, upkeep, external demand, tax tolerance, disaster frequency**. We copy the shape, milder |
| SimCity 2000 | Easy $20k, Medium $10k, Hard $10k *bond*; boom / stable / recession economy; disasters by level [12] *(thin)* | start money alone is remembered as the difference | Start money is an early-game lever only: it does nothing against coasting |
| Anno 1800 | granular settings: start capital, income ratio, incidents (fire, riots, illness) frequency by difficulty, "inactive upkeep" off = "significantly easier" [13] | presets + custom liked | Preset levels; every lever named so the player knows what "Hard" means |
| Tropico 6 | sandbox sliders for resources and political challenge; players: "money shouldn't be the only difficulty" [14] *(thin)* | factions ignorable, elections without stakes (SCOPE_REVIEW [5]) | Hard must add **threats with causes** (strikes, frost, emigration), not only thinner wallets |
| Workers & Resources | heating only when seasons are on; without heat workers sicken, leave or die; only some buildings need heat [15] | fussy logistics (SCOPE_REVIEW) | Only apartments/commercial/services draw heat (as 1.0); no fuel logistics |
| Against the Storm | seasonal storms and an impatience bar "poke the player out of the balanced state" (DESIGN_LESSONS 2.9) [16] | praised for no boring late game [16] *(thin)* | Rising, fictionalised pressure: winters, the neighbour's patience, era expectations |
| City-builder forums | late-game boredom = surplus cash and no stakes [17] *(thin)*; disasters disliked because "you just redo your work"; recessions and industry collapse preferred [17] *(thin)* | | Pressure via **budgets, expectations and shocks**, not via destruction |

## 2. Difficulty levels

### 2.1 Principles
- New Game radio **Easy / Normal / Hard**, default **Normal**. Locked after start (as Frostpunk 2 [10]); stored in the SIM
  chunk; **1.0 saves load as Easy**. One table `g_diff[3]` in defs.cpp; code reads `diff()->x`, never `if (hard)`.
- **Easy = 1.0 bit for bit**: every lever below at its 1.0 value, winter cosmetic only. Test: the 1.0 balance runs
  (planner s7 1990: 12,188 pop, 4,171 mk) and world hashes reproduce exactly on Easy.
- **Starting funds stay a separate option** (Generous 50,000 / Normal 25,000 / Austere 10,000, `budget_start_funds`), not
  a difficulty lever: start money only shapes the first five minutes [12][14].
- **Fail softly survives on every level** (pillar 7): `SIM_DECLINE_DEMAND_FLOOR` (-6), the emigration valve, the emergency
  loan (6 per mille, `LOAN_MAX` 40,000) and the bloc floor stay as in 1.0. Hard is harsher, never a death spiral.
- Every changed number is **named where it bites**: the ledger line tooltip ("Service upkeep: 1,240 (Hard: ×115 %)"), the
  demand tooltip, the query checklist, the plant/source query. A Guide (F4) page "Presidency: Normal" lists the table.

### 2.2 Parameter table (T = real threat, C = anti-coasting, E = early game). Values are bot-tuning starting points.
| Lever (1.0 constant) | Easy = 1.0 | Normal | Hard | Shown in | Role |
|---|---|---|---|---|---|
| Service + network upkeep (all BL_SVC/ROADS/NETWORKS/UTILITIES) | ×100 % | ×105 % | ×115 % | ledger line tooltips | C |
| Upkeep index cap (`UPKEEP_INFL_CAP`; +2.5 %/year from 1960) | 130 % (1972) | 145 % (1978) | 160 % (1984) | Budget panel "Upkeep index" | **C** |
| Resident + business tax yield | ×100 % | ×100 % | ×90 % | tax lines | E/C |
| Tax tolerance (`TAX_NEUTRAL`) | 9 % | 9 % | 8 % | R demand tooltip "Taxes" | E |
| Service overtime starts at (`OVERTIME_FREE_PCT`; max 300 %) | 125 % load | 115 % | 105 % | service query "Overtime" | C |
| Emigration of unemployed / homeless per month | 1/8 / 1/4 | 1/6 / 1/3 | 1/5 / 1/3 | Sweden counter, demand tooltip | **T** |
| Decline: bad checks before a tier drop (`SIM_G_DOWN` / `SIM_G_DOWN_UTIL`) | 4 / 8 (1 / 2 months) | 4 / 6 | 3 / 4 | checklist "losing tier in N weeks" | **T** |
| Negative event-card effects (money, approval, meters, residents) | ×100 % | ×115 % | ×140 % | card choice text (already itemised) | T |
| Card gap (`POL_EVENT_MIN/MAX`; still ≤ 1 card per game year) | 12-24 months | 12-20 | 12-16 | - | T |
| Authority income (popularity / N per month; cap 30 unchanged) | /20 | /24 | /30 | desk "Authority +1.6/month" | C |
| Eastern relations drift target (every 4 months, ±1) | 50 | 45 | 40 | Eastern meter tooltip "The neighbour's patience" | **C** |
| Oil/coal shock: rise of the index above 100 (1973 400, 1979 550, coal 180) | ×100 % | ×115 % | ×130 % | Trade panel, BL_FUEL | T |
| Fire ignition (`CIVIC_FIRE_IGNITE_PPB`) / spreads per outbreak | 100 / 3 | 150 / 3 | 200 / 4 | fire-risk overlay legend | T |
| Fire ignition in Dec-Mar (wood stoves) | ×100 % | ×150 % | ×150 % | overlay legend "Winter: stoves" | T |
| Winter load on power + heat use, Dec-Mar | +0 % | +15 % | +20 % | top-bar Power/heat tooltip "Winter peak" | **T/C** |
| Hard-winter chance per winter (+ fixed winter 1986-87) | 0 | 20 % | 33 % | November forecast | **T** |
| Hard winter: extra load / water-source output | - | +15 % / 90 % | +20 % / 85 % | source query "Hard frost -10 %" | **T** |
| Ploughing surcharge on road upkeep, Dec-Mar (hard winter) | 0 | +25 % (+40 %) | +35 % (+55 %) | ledger line "Winter" | C |
| Frost repairs in a hard winter (mk per 10 pipe tiles per month) | 0 | 1 | 2 | ledger line "Winter" | C |
| Sweden 1969-70 pull (per mille of population per month) | 0 | 2 | 4 | R demand term "Sweden is hiring" | **T** |
| Era expectations 1975 / 1980 (2.4) | off | on | on | checklist "From 1975: ..." | **C** |

Disasters off (New Game toggle) also switches off fires (as 1.0) and **hard winters** (normal winter peaks stay).

### 2.3 Which levers matter most
- **Real threats:** winter peak + hard winters (they turn a 90 %-loaded plant into a dark district in January), faster
  decline (a shortage now costs a tier within 4-6 weeks instead of 2 months), Sweden 1969-70 and faster emigration
  (residents visibly leave). Cards and oil shocks add money pain but are already itemised, so scale them gently.
- **Late-game coasting:** the upkeep index cap (costs keep rising until 1978/1984 instead of flattening in 1972), the
  Eastern drift (clearing I-demand, 75-125 % with relations, sags unless the President tends the neighbour), winter peaks
  that grow with the town, and the two era expectations. Tax yield and tax tolerance are early-game levers only.

### 2.4 Anti-coasting pressures (few, readable, each announced a year ahead)
1. **1975 "The welfare state"**: Houses T3 and Apartments T3 also need **library** cover (SVC_LIBRARY, Market town).
2. **1980 "The lähiö question"**: Apartments T2+ also need **clinic** cover (the late-Cold-War suburb-image decline, DESIGN 7).
   Both: January newspaper a year before ("From 1975 families expect a library within reach"), greyed checklist line
   "From 1975: library" in the query; from the year on they block upgrades; standing tiers decline only after a
   **12-month grace**. Checklists stay ≤ 5 lines (merge power + water into "Utilities" if needed).
3. Rising upkeep index, Eastern patience and winter peaks (table). Car growth and the 1986 oil collapse stay as in 1.0.
Nothing else: no ageing housing, no renovation (still deferred).

### 2.5 The great migration to Sweden, 1969-70 (Normal/Hard)
History: 100,000+ left in 1969-70, ~40,000 in 1970 alone; the population fell (THEME 1.1). In game: January 1969 headline
"SWEDISH FACTORIES RECRUIT IN THE MARKET SQUARE: good wages, warm flats". For 24 months an extra R term
`-pop × pull‰ × (3 - reasons) / 3` per month, emigrating employed residents too, where the **reasons to stay** are:
jobs (unemployment ≤ 3 %), homes (no homeless and vacancy ≥ 2 %), contentment (average happiness ≥ 60). Tooltip:
"Sweden is hiring: -36 this month. Reasons to stay: jobs ✔ homes ✘ contentment ✘". The Cabinet box names the missing
reason. Effect sizes (12,000 town, nothing held): Normal ~24/month (~580 over two years), Hard ~48 (~1,150). Emigration's
existing Workers-bloc effect applies. The 1990 newsreel line "Emigration to Sweden" gets a 1969-70 sub-total.

### 2.6 Bot targets per level (M8-style; `T=balance_era`, all 11 styles × s7/c3/r30 × 3 levels, 1955-1990)
| Level | planner / politics | naive (first-time human) | greedy / tax13 | all runs |
|---|---|---|---|---|
| Easy | = 1.0 exactly (BALANCE 9 table; hashes) | = 1.0 | = 1.0 | regression |
| Normal | Town by ≤ 1966; 0 emergency loans; ≥ 1 winter warning per decade it must answer, no tier lost to winter when it reads the forecast; 1990 pop ≥ 90 % of Easy; Sweden loss ≤ 2 % | finishes 1990 with ≤ 2 loans; ≥ 1 visible crisis (yearly pop drop ≥ 3 %) and recovery; 1990 pop ≥ 70 % of Easy; Sweden loss 4-8 % | greedy plateaus below Easy, ≥ 1 loan; tax13 no longer piles > 24 months of expenses | no yearly pop drop > 15 %; ≤ 1 card per year |
| Hard | Town by ≤ 1970; ≤ 1 loan, cleared before 1990; ≥ 1 hard winter that costs it a tier somewhere | ≤ 3 loans (cap); never below its 1965 pop for > 3 years; 1990 pop ≥ 50 % of Easy | greedy declines but never < 50 % of its peak | no yearly pop drop > 25 %; never stuck in debt at `LOAN_MAX` > 5 years |

**Coasting metrics** (new columns in `balance_era_summary.txt`): (a) median treasury 1980-1990 in months of gross expenses:
Normal ≤ 12, Hard ≤ 6 for planner, dense and mill (Easy today: dense/mill far above); (b) "quiet years" 1975-1990 with no
Cabinet warning: Normal ≤ 6 of 15, Hard ≤ 3. Bots need one new habit: the **planner reads the winter forecast** (keeps
supply ≥ forecast peak); the naive bot keeps reacting after 3 months of brownouts (so it meets winter the hard way).

## 3. Winter

### 3.1 Calendar
Winter = **December-March** (4 months: ~16 s of a 48 s year at speed 3, ~85 s at speed 1). `sim_season()` is a pure
function of the date (determinism: the art never feeds the sim). **November**: the forecast (normal or hard), drawn from
the sim RNG at 1 November; fixed hard winter **1986-87** on Normal/Hard (FMI: 2009-10 was the coldest "since the winter
1986-1987" [18]). Normal never rolls a hard winter in the Sweden winters (starting 1968-1970) or the first oil-shock winter
(1973-74), so headline threats don't stack, nor two in a row; Hard does not skip. **1 December** first snow (art swap),
**1 April** thaw (swap back). Easy: snow art and flavour lines only.

### 3.2 What changes (simulated)
- **Winter load** (table): power use and district-heat use ×(100 + winter %) Dec-Mar. Brownouts and heat cuts follow the
  1.0 rules (furthest lots first), then the level's decline speed. Apartments T3 upgrades check heat spare against the
  **winter peak** all year, so blocks don't upgrade in July and fall back in January.
- **Hard winter** ("pakkastalvi"): extra load, water sources (pumping stations, wells; not towers) give 90/85 % output, so a
  network over ~90 % load runs short: lots going dry are reported as **frozen pipes** ("Pipes frozen in Mäntylä: 23
  buildings"), same decline path as a water shortage. Frost repairs booked monthly. Fire ignition ×150 % in every winter (Normal/Hard).
- **Forecast is actionable**: from October the top-bar Power and Water/heat tooltips add "Winter peak (forecast): 1,360 /
  1,300"; advice.cpp ranks "winter peak exceeds supply" as a Cabinet line in October-November.
- **Optional card** (cut first if late): in a hard winter "The coal ship is frozen in at the harbour": pay the Eastern
  premium (money, Eastern +), ration district heat (heat -10 % for the winter, Workers -), buy Western coal (Western +,
  Eastern -). Uses the year's card slot; with politics off nothing happens.

### 3.3 Ploughing: decided
**Default and only rule: a winter surcharge on road upkeep (BL_ROADS share booked on a new ledger line "Winter: ploughing
and frost repairs"), no plough vehicles, no snow dumps, no new service or overlay.** Reasons: CS Snowfall's ploughs were
"not demanding to manage" and its winter "mostly visual" [3][5] *(thin)*, a vehicle service is the CS-chore class SCOPE_REVIEW
rejects, and the money cost scales with the road network, which is exactly the planning lesson (compact towns are cheaper).
Not adopted: a "thrifty ploughing" switch (-50 % surcharge, -15 % winter road capacity). Post-1.1 idea only.

### 3.4 Cosmetic vs simulated
| Simulated (sim, saved, deterministic) | Cosmetic only (render/text) |
|---|---|
| winter load on power and heat; hard-winter roll, extra load, water-source loss, frozen-pipe reports | snow on ground, roofs, trees, roads; lakes frozen (no shimmer); rivers and rapids stay open |
| ploughing surcharge and frost repairs (ledger "Winter") | ice road across a frozen lake in a January filler story; no road, no traffic effect |
| winter fire ignition; T3 heat check against the winter peak | date-field snowflake glyph and "Winter (hard)" tooltip; winter newspaper lines |
| forecast in tooltips/advice; optional coal-ship card | minimap snow palette (if cheap) |

### 3.5 Winter lines (newspaper, ticker, Cabinet; same voice as 1.0: deferential, deadpan, about the system)
- Nov, hard: "The Meteorological Institute expects a hard winter. The Cabinet respectfully notes that last January the power
  works ran at 96 %. Naturally the President is already aware." / Nov, normal: "Winter forecast: ordinary. In this country
  that means the lakes freeze and nobody mentions it."
- Advice: "Cabinet sources observe that the winter peak (1,360) exceeds the power works (1,300). A plant, perhaps."
  / 1 Dec: "First snow. The ploughs are out, and so is the road budget."
- Brownout: "Kivikko sits in the dark by candlelight. Romantic, say the poets. Temporary, says the Cabinet." / Frozen pipes:
  "Pipes frozen in Mäntylä: 23 households fetch water from the neighbours. The neighbours keep count."
- Jan headline, hard: "MINUS 35 IN THE MARKET SQUARE: SAUNAS REPORT RECORD ATTENDANCE". / Ice road: "The ice road to the
  island is open. The ministry reminds drivers that the ministry is not responsible."
- 1 Apr: "The ice is out. The winter cost the town 4,200 mk in ploughing and repairs." / Hindsight: "In hindsight, the
  President met the hard winter of 1966 with 40 units of power to spare. Too few."

## 4. Winter art

### 4.1 What gets a snow version
Terrain (grass, meadow, fields, marsh, rock, forest floor, lot and construction grounds, rubble), shores (ice ledge),
lake water (ice `#CADCE6`, all three shimmer frames identical, so lakes stop shimmering: saves the z32 shimmer CPU),
rivers/rapids (open dark water with icy banks; shimmer kept), trees and forests (spruce dark `#2C3E33` with white caps;
deciduous foliage recoloured to bare grey-brown twigs with snow dabs), every roof and up-facing ledge, roads (asphalt
grey with white verges and banks; gravel = packed snow; markings hidden), rail (snow between rails), bridges.
**Season-invariant** (no winter copy): cars, buses, smoke, overlay diamonds, pipes, zone tints and edges, cursors, power
poles and wires, fire frames. UI: no reskin; one snowflake glyph in the always-resident UI font/icon sheet (< 1 KB).

### 4.2 Generator: a `winter` shade mode (ART.md 6 "Winter later")
`render_gbuffer` already keeps per pixel the face normal, material key and world point. A new `shade()` mode like `ab`:
up-facing normals on ground/roof/foliage materials -> snow ramp (lit `#F4F6F8`, `#E3E9EF`, blue shadow `#BCC8D8`, deep
`#93A2B8`; THEME 3.2), water -> ice, deciduous foliage -> twigs, walls unchanged. **Rules:** (1) the winter pass never
changes the alpha mask, so every sprite keeps its box, anchors, pieces and picking; (2) snow noise is a hash of
sprite-local coordinates, so pieces that are identical in summer stay identical in winter (the dedup survives); (3) colours
pre-rounded to RGB565, key RGB(255,0,255) only where summer has it. Output `world<k>_<z>w.bmp` pages sharing the summer
`sheet_world_<z>.h` (one piece table). `--repack` puts season-invariant sprites on their own pages (no winter copy).
`--check` verifies: same page count and sizes, identical alpha per page, dedup invariant, winter VRAM report.
Mockups first (`preview/town_64_winter.png`, `town_32_winter.png`, overlays and zone view on snow) for user review before
the full run. Low contrast is the art risk: blue shadows and dark walls keep buildings readable; roofs of heated
buildings may melt to wet dark where the model says "chimney" (nice-to-have).

### 4.3 VRAM and memory
- **VRAM: +0 KB at every resolution × zoom.** Only the active season's pages are resident, in the **same surfaces**: the
  swap re-uploads pixels into existing pages (Lock, copy, Unlock once per page per season change; the "never write locked
  video memory per frame" rule is about per-frame writes). No free/re-create, so no heap holes (PL4 lesson). Budget stays
  64-zoom 7,691 KB / 32-zoom 1,918 KB; 1024 close zoom keeps its ~522 KB free (pool 41/48) in winter too.
- **System memory (Plan A):** the winter pixels of the season-variant pages kept in system memory beside the summer ones
  (16-bit: at most +7.7 + 1.9 MB, less the invariant pages). Swapping the inactive zoom's set is a pointer switch. The task
  measures the heap first; Plan A only if the total stays within the 32 MB heap budget (README).
- **Disk/zip:** +31 + 8 page BMPs (~15 MB uncompressed, compresses well).

### 4.4 Swap timing
At the frame after the 1 December / 1 April month step: upload the active zoom's season pages (T13/PL4 measured ~140 ms for
31 uploads at 1024 z64), then flush the chunk cache once (visible chunks render at once, the rest ≤ 4 per frame). Target:
≤ 150 ms hitch on the VM, ≤ 30 ms on the X41/T40. At speed 3 this happens every 16-32 s, so if the VM hitch is visible,
spread the uploads over ≤ 8 frames with chunk renders paused and flush after the last page (never a mixed-season chunk).
Zoom swaps, lost-surface restores (alt-tab) and loading a save in winter upload the current season's set.

### 4.5 Fallbacks
- **Plan B** (heap over budget): read the season pages from disk at the swap into the existing system-memory buffers
  (~1 s on the P2 VM, ~0.1 s on GHz targets), behind the "First snow" / "The ice is out" ticker; measure, log, accept.
- **Plan C**: INI `winter_art=0` (Settings: "Winter scenery") keeps summer art all year; the simulation is unchanged.
- Missing or corrupt winter pages -> log and Plan C, never a fatal art error.

## 5. Scope, risks, tests

### 5.1 Wave A tasks (parallel, per PLAN_1_1 section 3)
**D1** difficulty + era expectations + Sweden (defs table, New Game radio, save field, ledger/tooltip naming, Guide page) |
**W1** winter simulation (season, forecast, load, hard winter, frozen pipes, ledger "Winter", advice/news lines) | **W2**
winter art (shade mode, pages, check, mockups first; engine swap + Plans A/B/C). Wave B: balance (2.6), VM + X41 perf.

### 5.2 Cut order if time runs long (first cut first)
1. coal-ship card; 2. ice-road and heated-roof flavour, minimap snow palette; 3. the 1980 era expectation (keep 1975);
4. winter fire ignition; 5. frost repairs (keep the ploughing surcharge); 6. fixed 1986-87 winter (random only).
**Never cut:** Easy = 1.0, the difficulty table with naming, winter load + announced hard winters, the Sweden years,
snow art at both zooms with 0 KB extra VRAM.

### 5.3 What genre reception warns against (and our guard)
- Winter as decoration or as chores [3][5][6] -> it moves capacity, money and decline; no vehicles to manage.
- Difficulty = only money [12][14] -> threats with visible causes; start funds untouched.
- Disasters that just destroy your work [17] -> no destruction beyond the 1.0 fire rules; shortages are reversible decline.
- Death spirals (Banished) -> spiral guards unchanged on every level; bot guard on yearly pop drop.
- Stacked shocks that read as unfair -> Normal skips hard winters in the Sweden and first oil-shock winters; 1 card/year.
- Hidden numbers (CS2 economy, SCOPE_REVIEW [9]) -> every multiplier named in a tooltip or the ledger.

### 5.4 Test plan
- **Mac:** Easy = 1.0 regression (balance_era hashes and the BALANCE 9 table); 1.0 saves load as Easy; difficulty and season
  save round trips; hard-winter schedule deterministic per seed (and none on Easy or with disasters off); winter load,
  source loss and frozen-pipe events on a fixed town; Sweden term and its three reasons; era expectation grace (no tier
  lost before 12 months); ledger "Winter" sums; card cap ≤ 1/year on every level.
- **Bots:** 11 styles × s7/c3/r30 × Easy/Normal/Hard 1955-1990 with the 2.6 targets and coasting metrics; BALANCE.md
  section 10 records the tuned values (this table's numbers may move; the tooltips read `g_diff`).
- **Art:** `gen_art.py --check` (alpha/dedup/layout identity, VRAM per zoom × season); mockup review by the user.
- **VM:** VC6 /W3 clean; replays = Mac on all three levels; `-bench` 640/800/1024 × z64/z32 × summer/winter (winter must
  not be slower), season-swap time in the log at each resolution, swap during scrolling, zoom swap and alt-tab in winter,
  winter overlays and zone view readable (cropped screenshots). **X41:** same bench, swap timing and VRAM free in winter;
  then the user's play-test on Normal and Hard.

## 6. Sources
- [1] PCGamesN, "Freezing a city to death in Cities: Skylines Snowfall". https://www.pcgamesn.com/cities-skylines/freezing-a-city-to-death-in-cities-skylines-snowfall · [2] Paradox forum, Snowfall dev diary 3 "Snow and heating" (read via search excerpt; page bot-blocked) *(thin)*. https://forum.paradoxplaza.com/forum/developer-diary/cities-skylines-snowfall-dev-diary-3-snow-and-heating.906071/
- [3] GameWatcher, Cities: Skylines Snowfall review (search excerpt) *(thin)*. https://www.gamewatcher.com/cities-skylines-snowfall/review · [4] Softpedia, Snowfall review (search excerpt; 403) *(thin)*. https://www.softpedia.com/reviews/games/pc/cities-skylines-snowfall-review-500586.shtml
- [5] Snowfall reviews on ploughs and heating set-up (TheXboxHub, citiesskylinestips 2023) *(thin)*. https://www.thexboxhub.com/cities-skylines-snowfall-review/ , https://citiesskylinestips.com/?p=319 · [6] Paradox forum dev-diary thread, player replies ("ignore the snow stuff", "money-burning bust") *(thin)*. URL as [2].
- [7] Banished Wiki, Getting Started Guide. https://banished-wiki.com/wiki/Tutorials/Getting_Started_Guide · [8] GOG forum, Banished "they die of starvation" *(thin)*. https://gog.com/forum/banished/they_die_of_starvation
- [9] Frostpunk update 1.1.1 "Extreme difficulty"; Steam discussion on difficulty and weather settings *(thin)*. https://www.frostpunkgame.com/?p=5973 , https://steamcommunity.com/app/323190/discussions/0/1696045708638039753 · [10] Prima Games, Frostpunk 2 difficulty settings explained. https://primagames.com/gaming/best-frostpunk-2-difficulty-settings-citizen-officer-steward-and-captain-differences-explained
- [11] Micropolis (SimCity) source, `simulate.cpp` and `disasters.cpp`. https://github.com/SimHacker/micropolis · [12] DOS Game Club forum, "Hard is... hard... but how hard?" (quotes the SC2000 manual) *(thin)*. https://www.dosgameclub.com/forums/topic/hard-is-hard-but-how-hard/
- [13] Ubisoft, Anno 1800 DevBlog "Have it your way"; Anno Union on incidents. https://www.ubisoft.com/en-au/game/anno/1800/news-updates/5jpMEFTsLQiwlXEjtyYcaR/devblog-have-it-your-way · [14] Tropico 6 Steam discussions (search excerpt; exact thread unverified) *(thin)*. https://steamcommunity.com/app/492720/discussions/0/1644295067080451456
- [15] Workers & Resources wiki, Heating. https://wiki.hoodedhorse.com/Workers_Resources_Soviet_Republic/Heating · [16] DESIGN_LESSONS 2.9; Game Developer, "How Against the Storm managed to mix city-building and roguelite play". https://www.gamedeveloper.com/road-to-igf-2023/how-against-the-storm-managed-to-mix-city-building-and-roguelite-play
- [17] Steam, Cities: Skylines late-game discussion; forum threads on disasters vs recessions *(thin)*. https://steamcommunity.com/app/255710/discussions/0/610575007213507702 · [18] Finnish Meteorological Institute via EurekAlert, winter 2009-10 "coldest since the winter 1986-1987". https://www.eurekalert.org/news-releases/537330
