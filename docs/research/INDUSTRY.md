# INDUSTRY: Finnish industry, forests, fields and the company town (research + design)

Status: proposal (research task for PLAYTEST session 1, items 8 and 9). Nothing here is implemented. Extends
DESIGN 4.2 (zones), 4.5 (special buildings), 5.4-5.5 (growth/demand), 6 (clearing trade) once the user approves.

**User wishes (binding):** (1) a 3-wide industrial zone must not leave gaps that break power/water conduction;
(2) industry with Finnish themes of the era: *Valtion Metalli* (state metal works), paper mills, sawmills, fed by the
map's plentiful forest; (3) a use for fields; (4) can the one-big-mill company town (*tehdasyhdyskunta*) be gamified,
or is the generic industrial zone better?

**Short answer:**
1. **Gap:** add a 1x1 industrial **yard** filler lot (timber stack, steel stock, warehouse shed, milk-can shed) that
   grows only where no 2x2 fits, **and** let empty zoned tiles conduct power (SimCity 1989 rule). Next fix round.
2. **Kinds:** the generic industrial zone keeps working with no new decisions. Each new lot gets a **kind** (sawmill,
   engineering works, textile mill, dairy; later electronics), chosen by **site and demand**. Wood kinds earn
   **Western** hard currency, metal and textiles fill the **Eastern clearing** quota. That is the historical split,
   and it ties industry to the East/West balance.
3. **Inputs** are aggregate numbers, never trucks: a painted **forestry zone** (forest tiles harvested in rotation,
   regrowing) gives wood; a painted **farm zone** (farmsteads + fields) gives milk and rural residents. Both are
   single-step chains: forest to mill, field to dairy.
4. **Do both:** generic zones carry the background economy, and an optional unique **mill complex** (paper mill with a
   *patruuna*; *Valtion Metalli* works by decree) anchors a company town, with gifts, demands, a dependency meter and
   telegraphed risk events (paper slump, strike, 1970s modernisation layoffs).

Confidence tags as in WATER.md: **[M]** official/primary, **[C]** several sources agree, **[?]** one source or memory.

---

## 1. Historical facts that drive the design

### 1.1 Forest industry
- **Forest products dominated exports**: well over half in the early 1950s [C], about 56% in 1970 and 52% in 1974
  (Bank of Finland bulletins) [M]. Metal and engineering exports grew fastest (+21% in value in 1970) [M]. Most forest
  exports went **West** (UK, West Germany) for hard currency [C]. The USSR also took paper: in 1989 Finnish firms
  supplied over 70% of Soviet paper imports [M].
- **Sites:** sawmills sat on lakes and river mouths with log ponds. Pulp and paper mills sat by **rapids** (*koski*)
  for water power and process water. Look at the names: Kuusankoski, Valkeakoski, Jämsänkoski, Äänekoski, Mänttä on
  its rapids [C]. Mills polluted lakes. The Water Act of 1961 began to regulate it (THEME 2.12) [C].
- **Log floating** (*uitto*): its golden age lasted until the 1950s. Costs rose about 20-fold after the war, while
  road and rail costs rose by about 15%. From the 1960s log trucks and freight trains took over. The last floating on
  the Kemijoki was in 1991 [C]. Game: floating is a 1955-69 wood source, and trucks replace it after that.
- **Winter logging was farmers' work**: small farmers cut and hauled timber in winter for cash (THEME 5.1) [C], so
  forestry jobs belong to the Rural bloc.

### 1.2 Metal and engineering: Valmet, the model for *Valtion Metalli*
- In 1946 the state's war factories (the Rautpohja artillery works in Jyväskylä, a rifle factory, an aircraft factory
  and shipyards) were merged into the state metal works group (VMT). It became **Valmet in 1951** [C].
- Its work was converting weapons plants to peacetime products. It made **tractors** (Tourula, from 1951/52) [C],
  **paper machines** (Rautpohja, first one delivered in 1953) [C], ships at the Helsinki and Turku yards,
  railway rolling stock, aircraft and rifles [C].
- **War reparations** (1944-52) built this industry: a large share was ships, machines and cables. Very few reparation
  items suited the commercial market [C]. After 1952 the same capacity kept selling **east** under clearing [C].
- Game: *Valtion Metalli* is a state works placed by decree, a clearing-quota machine (tractors, paper machines,
  ships, rolling stock). The paper machines it builds go to the paper mills: a lovely domestic chain to mention in
  ticker text.

### 1.3 Clearing trade with the USSR
- Bilateral trade ran on **long-term framework agreements** (five-year) with annual protocols. The USSR's share was
  about **20-25% of Finland's exports and imports** in the post-war decades, peaking above 30% in some years [C].
- **Finland sold** metal products and machinery (dominant), **ships** (icebreakers and ferries were the speciality),
  paper and wood products, textiles and clothing, footwear, food, electronics [C]. Prefabricated wooden houses were a
  reparations and early clearing item [?].
- **Finland bought** mainly **oil** (the mid-1970s and early-1980s oil prices raised the Soviet share of both imports
  and exports), raw materials, cars [C].
- Food: the dairy co-op Valio (founded 1905 to export butter) sold mostly to the USSR from 1956 to 1991 [?].

### 1.4 Farms and the structural change
- Around 1950 farming and forestry employed nearly half the workforce (THEME 5.1). Farms were small dairy farms of
  5-15 ha with red barns, and the milk-can platform at the gate fed the parish **co-op dairy** [C].
- In the 1960s Finland moved workers out of agriculture **faster than any other Western European country** [C].
  **Mechanisation** (tractors, many from Valmet) cut the labour needed per field.
- **1969 Field Reservation Act** (*pellonvaraus*): farms over 2 ha were paid to stop producing, to cut overproduction.
  It worked, caused a political backlash and a brief rise of a rural populist party [M]. About **80,000 people moved to
  Sweden in 1968-69**, and 300,000 in total after the war, mostly from the rural north and east [C].
- Game: a farm zone that sheds residents when it mechanises turns the structural change into something the player
  watches happen. The *pellonvaraus* event is a satire event that writes itself.

### 1.5 Company towns (*tehdasyhdyskunta*, *patruuna*)
- **Before the welfare state, the company provided healthcare, welfare, housing and education** for its workers
  (Mänttä, Serlachius) [M]. The **church** in Mänttä was a company gift (donated in 1928) [M]. A tennis pavilion was
  for senior officials only [M]. That is the paternalism with class lines the satire should show.
- **Varkaus**: A. Ahlström planned the town around its plants. Alvar Aalto's office designed dozens of plans, mostly
  workers' housing, and the company ran a **factory school** before Varkaus became a municipality [M].
- **Valkeakoski** (first paper mill 1873; workers' houses kept as a museum), **Jämsänkoski** (grew around an 1888
  cellulose factory; separate municipality in 1926) [C]. Kuusankoski (the Kymi company), Äänekoski (famous smell) and
  Mänttä fit the same pattern (THEME 2.9).
- The typical layout was a mill by the rapids, wooden workers' row houses, a manager's villa (*patruunan kartano*), a
  clubhouse, a factory school, a fire brigade, a sauna and a church [C]. The dependency ran both ways: the town lived on
  one employer's paper price.

---

## 2. Lessons from other city builders

| Game | How it does industry | What players liked / disliked | Take for KAUPUNKI |
|---|---|---|---|
| **SimCity 4** | One I-zone. The kind that grows (agriculture, dirty, manufacturing, high-tech) depends on land value, education and pollution. Agriculture is a separate I-type, so farms grow even on high land value [C] | Liked: the city's character emerges from its conditions. Disliked: steering meant hacks such as taxing dirty industry at 20% so high-tech could grow [C] | **Kind chosen by site conditions**, automatic. Give the reason in the query panel so nobody needs the tax hack |
| **Cities: Skylines** Industries DLC | Painted industry areas on resources (forest, farm, ore, oil), processing buildings, warehouses, unique factories, truck logistics | GameSpot 7/10: "extra work with questionable end benefit" [C]. Truck traffic jams are a common complaint. Unique factories and visible chains were liked | Keep the **painted resource area** and **unique factory**; drop trucks and warehouses |
| **Anno** | Production chains are the game | Gorgeous chains, but they are the whole game | **One-step chains only** (forest to mill, field to dairy) |
| **Workers & Resources** | Full logistics, everything hauled | "Fussy and demanding"; a different game (DESIGN_LESSONS 2.4) | **Capacities, not vehicles** (like the element factory) |
| **Banished** | Forester plants, woodcutter cuts within a radius; regrowth | Readable renewable resource; time compressed about 3x | **Forest regrowth with compressed time**, shown per tile |
| **Tropico** | Player places each factory; export prices; plantations by soil | Satire + economy works | Complexes are placed by the player; events speak in satire |

Principles kept (DESIGN_LESSONS 6): every number visible, aggregate simulation, networks as decisions, theme as
mechanics, fail softly. **No resource is ever a hard stop for the I-zone**: if no kind's site rules pass, the lot
becomes an engineering workshop (section 4.3).

---

## 3. Recommendation at a glance

| Piece | What | When |
|---|---|---|
| Gap fix | 1x1 industrial yard filler + empty zoned tiles conduct power (and water once pipes land) | **Next fix round** |
| Industry kinds | sawmill / engineering / textile / dairy (electronics 1965+) chosen by site + kind demand; I-demand split by export category | Milestone 2 |
| Forestry zone | painted on forest; rotation harvest gives wood units; logging camp lot; regrowth | Milestone 2 |
| Farm zone | painted on fields/grass; farmsteads T1-T3 (mechanisation sheds residents); milk for dairies | Milestone 2 |
| Paper mill complex | unique, staged 5x5, *patruuna*, company-town rules, risk events | Milestone 2 (with politics) |
| *Valtion Metalli* works | unique state works by decree, clearing quota anchor | Milestone 2 (with politics) |
| Electronics, shipyard, textile complex, peat bog, lake pollution, company-town start | | Stretch |

---

## 4. The gap fix (next fix round)

### 4.1 Why both parts
Industrial lots are 2x2 or 3x3, so a zone band 3 deep leaves a 1-tile strip, and a 5-deep band leaves one too. Power
conducts only through lots, power lines and one plain road tile (`net.cpp`, `s_def_cond`, `POWER_JUMP_ROADS`), so
the strip cuts the chain. Apartments (3x1, 2x2) can leave the same gaps.
- **Fillers alone** fix industry only, and only once demand builds them (a 1x1 lot needs allowance like any other).
- **Conduction alone** fixes power everywhere at once, but leaves bare strips inside industrial estates.
- **Together:** the network works from the moment you zone, and the estate looks finished.

### 4.2 Empty zoned tiles conduct (SimCity 1989 rule)
- Rule for the manual: *"Zoned land is already serviced: power passes through zoned tiles as if they were built."*
- In `solve_power_full`: `s_cond = 1` also for `!t->lot && zone_conducts(TILE_ZONE(t))`, where Houses, Apartments,
  Commercial and Industry conduct and the new Forestry and Farm zones do **not** (large rural areas must not become
  free power lines). Same test in `live_conductive()` for the incremental path.
- Zoning and dezoning must bump `world_struct_serial()` (check `tools.cpp`; otherwise the incremental power solve
  misses it). Empty zoned tiles get `ZF_POWERED`, so the power overlay shows a zoned block as "connected" before it
  is built. That doubles as a preview.
- **Water** (once WATER.md pipes are implemented): apply the same rule. Zones never cross roads, and water doesn't
  jump roads (WATER Q3), so a pipe is still needed per street side. This only removes the gap problem. Ask the user
  (open question 1).
- Cost: zero CPU (the conduction table is built per solve anyway). Balance: none, because empty tiles use nothing.

### 4.3 The 1x1 industrial yard
- New def `ind_yard_1x1`, "Yard", zone Industry, tier 1, **no upgrade**, 5 jobs, power 1, water 0, pollution 5,
  `BDF_NO_ROTATE` (art is symmetric enough), build 1 month.
- **Filler rule** (in `process_empty`, after the 3x3/2x2 candidates): place a yard only if **none of the four 2x2
  rectangles containing this tile is free zoned land** (so yards never eat space a factory could use) and the tile
  touches an industrial lot or an access road. The 65% "prefer larger" logic stays for the real lots.
- **Variant by neighbour** (hash for ties): next to a sawmill, a *timber stack*; next to engineering, *steel stock
  and scrap*; next to a textile mill, a *bale warehouse*; next to a dairy, a *milk-can shed*; otherwise a *coal heap*
  or *storage shed*. Before kinds exist (fix round) the variant comes from the tile hash.
- Query line: "Yard: storage for the works next door. 5 jobs."
- Yards count in `s_cap_ind` (5 jobs each), so a full 3-deep band adds about 10% capacity. The I-demand absorbs it.

---

## 5. Industry kinds in the generic zone (Milestone 2)

### 5.1 The kinds

Jobs follow the existing tier rhythm (10 / 25 / 40 per tile). "Input" is per month, per lot at full occupancy.

| Kind | Era | Lots (tier: size, art) | Site rule (must) / bonus | Input | Pollution | Exports (DESIGN 6 categories) |
|---|---|---|---|---|---|---|
| **Sawmill** (*saha*) | 1955 | T1 3x3 steam sawmill (**existing** `ind_t1_3x3`); T1 2x2 joinery "prefab house works" (workshop body, timber variant); T2 3x3 sawmill + planing mill + kiln (new) | must: wood supply ≥ 75% after this lot's need / bonus +50 footprint within 2 of water (log pond), +25 forestry zone within 8 | wood = jobs / 10 (T1 3x3: 9 wu) | low (80 / 3x3), **wooden: fire risk** | West: sawn timber 60%; East: **prefab houses** 40% |
| **Engineering works** (*konepaja*) | 1955 | T1 2x2 workshop (**existing**); T2 2x2 sawtooth factory (**existing**); T3 3x3 large works (M2 art planned) | must: none (the **fallback kind**) / bonus +50 rail within 4, +25 main road | none | medium | East: **machinery** 75%; West 25% |
| **Textile & shoe mill** (*kutomo*, *kenkätehdas*) | 1955 | T1 2x2 sewing workshop (workshop variant v1); T2 3x3 multi-storey brick mill (**existing** `ind_t2_3x3`) | must: none / bonus +50 river or rapids within 4 (the Tampere water-power heritage) | none | low (half of engineering) | East: **textiles, clothes, shoes** 80%; West 20% (falls after 1975) |
| **Dairy** (*meijeri*) | 1955 | T1 2x2 co-op dairy (new); T2 2x2 central dairy (new) | must: milk supply ≥ 75% / bonus +25 farm zone within 10 | milk = jobs / 2 (T1: 15 mu) | very low | East: **butter, cheese** 60%; West 40%; local food |
| **Cable & electronics** (*kaapelitehdas*) | 1965, Town | T2 2x2, T3 3x3 (stretch art) | must: school quality ≥ 50 and pollution ≤ 48 at site / bonus +50 station within 6 | none | very low | West 50% (hard currency, tech); East: TVs 50% |

The **paper mill is complex-only** (section 7). A real pulp-and-paper mill employed hundreds and spread over many
hectares, so as a 3x3 zone lot it would be a lie. It would also need art the zone can't afford. Stretch option: a T2
3x3 groundwood mill (*puuhiomo*) kind on shores.

### 5.2 How a kind is chosen (no new decisions for the player)
At growth, in `process_empty`, before choosing the lot size:
1. For each unlocked kind `k` whose **must** rule passes for the candidate footprint:
   `weight_k = max(0, kindDemand_k) × (100 + siteBonus_k) / 100`.
2. Deterministic weighted pick with the sim RNG. If every weight is 0, pick **Engineering** (always allowed). So the
   I-zone never stalls on a resource.
3. The kind fixes the candidate defs (its own sizes and tiers). **The kind is a BuildingDef field**, so a Lot needs no
   new data: `def` already says "sawmill T1 3x3".
4. Upgrades stay within the kind (T1 sawmill to T2 sawmill). Requirements add the kind's input (`REQ_WOOD`,
   `REQ_MILK`, each at 75% supply) to the tier lists, and the "why not growing" checklist shows it.

The player steers by **where** they zone: next to the lake and the forestry zone you get sawmills, along the railway
engineering works, by the rapids textile mills, near farms dairies. That is location-driven specialisation (as in
SimCity 4) without a district brush.
*Option (open question 2):* a per-zone "preferred kind" brush later (it needs a 64x64 cell byte array, 4 KB).

### 5.3 The I-demand split (the total stays the same, so balance survives)
Today: `I = clearing + western + localNeed − industrialCapacity − tax` (sim.cpp `compute_demand`). New: split each
source by the export shares and subtract each kind's capacity:

```
kindDemand_k = (sC_k * clearing + sW_k * western + sL_k * localNeed) / 100 - capacity_k - tax_k
Σ_k kindDemand_k = I        (shares per source sum to 100)
```

| Share (%) | Sawmill | Engineering | Textile | Dairy | Electronics (1965+) |
|---|---|---|---|---|---|
| sC clearing | 20 | 45 | 25 | 10 | 0 → 10 (shift from engineering after 1970) |
| sW western | 45 | 20 | 10 | 15 | 0 → 10 (from sawmill share after 1965) |
| sL local need | 15 | 45 | 15 | 25 | 0 |

- The I tooltip stays itemised as today and gets a second block: "Sawmills +38, Engineering +61, Textiles +12,
  Dairies −4 (milk short)". A kind whose input is short shows a **"wood short −N"** term (shortage in wu × 10 jobs),
  and its share flows to the fallback kind, so total I growth continues.
- When politics lands (DESIGN 6), `clearing` becomes the 5-year agreement's quota **per category**. Engineering fills
  "machinery", sawmills "prefab houses", textiles "textiles", dairies "food". The clearing panel shows deliveries by
  category, and the Western meter grows with sawmill/dairy/electronics output. **Wood earns the West, metal and
  textiles the East**: the player's industrial mix becomes a foreign-policy position.

### 5.4 Export income
Keep the `export` budget line. Make it `Σ jobs_k × rate_k`, rate per 100 jobs per month: sawmill 4, engineering 3,
textile 2, dairy 3, electronics 6. Wood and electronics pay more because they earn hard currency. Recalibrate so a
1970 planner town keeps its current export total (336 mk/month in BALANCE 2): the total must not move.

---

## 6. Resource inputs: forests and fields (Milestone 2)

### 6.1 Wood: the forestry zone (*metsätalous*)
- **Paint** with the zone tool on **forest tiles** only (other tiles are rejected and hatched). There is no road-reach
  limit for painting, but a tile is harvestable only within **6 tiles of an access road** (logging roads). Zoning is
  free; dezoning stops logging.
- **Density** is the existing `Tile.aux` forest density (0-15, set by mapgen). Stages: 12-15 mature, 8-11 young
  forest, 4-7 seedlings, 0-3 clear-cut (stumps).
- **Harvest** (month end, deterministic scan): the town's wood need `D` is cut from mature forestry tiles, **cell by
  cell** so the cuts form visible patches, nearest to the wood users first. Each cut tile yields **12 wu** and drops to
  density 0.
- **Regrowth:** +1 density every 4 months on forestry and plain forest tiles below their mapgen maximum, so it takes
  **48 months from stumps to mature**. Sustainable yield is 12 / 48 = **0.25 wu per forestry tile per month**. The real
  rotation is 60-100 years. Time is compressed about 20x here, like Banished's ages, and the manual says so.
- **Logging camp** (*kämppä*, 1x1 lot, grows like a zone lot on a forestry tile next to a road, one per ~40 forestry
  tiles): it holds the jobs, **1 job per 2 wu harvested** (winter work; Rural bloc). Art: a log cabin with a horse and
  sledge before 1965, with a forestry tractor after.
- **Regional wood** (bought from the parish's farmers, off-map): base 10 wu + 15 with a railway station + 15 by
  **floating** (1955-69, if a wood user touches a water body that reaches the map edge or a river) or by **log
  trucks** (1965+, national road connected). Max 40. Four T1 sawmills can run without cutting a tree on the map, so
  forestry is an expansion choice and not a toll.
- **Supply** `S = harvest capacity + regional`. Users get `min(100%, S / D)` proportionally. Query on any wood user:
  "Wood: 9 wu/month, supply 82% (forestry 34, regional 40, need 90)".
- **Sizes for scale:** a T1 3x3 sawmill needs 9 wu = 36 forestry tiles. A full paper mill (120 wu) needs ~320
  forestry tiles plus regional wood, about a 16x20 patch. Lakeland seed 7 has ~4,800 forest tiles and the Forest
  preset ~7,300, so the maps really can feed a mill. That makes the Forest preset the paper-mill map.
- **Bloc and land value:** the Rural bloc likes forestry jobs and standing forest, and dislikes a **clear-cut share**
  above 25% of the town's forest tiles ("Clear-cuts: 31% of forest"). Clear-cut tiles lose the forest-edge land value
  bonus until they reach density 8. Mass clearing for building keeps its existing one-off timber income (`g_misc`).

### 6.2 Fields: the farm zone (*maatila*)
- **Paint** on field terrain or grass (on forest it costs the normal clearing). Farmsteads need a road within 3, and
  fields may extend up to 8 tiles from the road. Farm zone tiles **do not conduct** power or water.
- **Farmstead** (1x1 lot) grows on a road-reachable farm tile when at least 6 unclaimed farm tiles lie within 3. It
  claims up to 8 of them, and claimed grass becomes **field terrain** (crop/ploughed variants exist), so the
  landscape visibly turns agricultural.

| Tier | Name | Era | Needs | Residents | Milk per claimed field / month |
|---|---|---|---|---|---|
| T1 | Small farm (*pientila*): red cottage, barn | 1955 | road | 5 | 1 |
| T2 | Dairy farm (*maalaistalo*): 2-storey yellow house, red barn, milk-can platform | 1955 | + power (rural electrification), school | 6 | 2 |
| T3 | Mechanised farm: + machine shed, tractor, silo | 1962 | + power, main or street road | **4** | 3 |

- Farm residents are **self-employed** (counted as employed, not in the town job market) and belong to the Rural
  bloc. **T2 to T3 loses 2 residents** but raises milk. The displaced family joins the homeless pool: they look for
  a flat in town or emigrate to Sweden. *The structural change happens on the player's own map.*
- **Farm demand** (shown in the farm tool tooltip and query, not a fourth top-bar bar):
  `F = foodNeed + dairyMilkNeed − milkSupply`, with `foodNeed = 20 + pop / 50` (mu). It stops new farmsteads once
  milk covers the town and its dairies.
- **Milk supply** = claimed fields × tier factor + **regional milk 20 mu** (off-map farms), so one small co-op dairy can
  run with no farm zone.
- **Event 1969, *pellonvaraus*** (card, politics on): "The state offers to pay farmers to leave fields idle."
  Accept: +money per farm, farm tiers drop and fields fallow for 5 years, Rural approval up now and down later, more
  emigration. Decline: Eastern relations neutral, Rural +, the budget pays a subsidy.
- Taxes: farm residents pay the tier-1 house weight (BALANCE 3). Farms are no money machine; they are cheap
  population, a dairy feed and Rural approval.

---

## 7. The mill complex and the company town (Milestone 2)

### 7.1 Generic zone vs complex

| | Generic I-zone with kinds | Unique mill complex |
|---|---|---|
| Player effort | zone and forget | one placement, then decisions via events |
| Reads as | a mixed industrial estate | *the* mill: the town's reason to exist |
| Economy | elastic, follows demand | lumpy: hundreds of jobs at once, dependency risk |
| Theme | structural change, clearing categories | *tehdasyhdyskunta*, *patruuna*, paternalism, strikes |
| Art cost | reuses tier art | one big module set per complex |
| Failure mode | slow, smooth | shocks (slump, strike, layoffs), telegraphed |

**Recommendation: both.** The generic zone is the always-available background economy. Complexes are optional,
unique, staged landmarks that a player chooses for the story and the risk. They are also the money sink BALANCE 5
asks for. A New Game option "**Company town start**" (a mill already standing, 20 company houses) is a stretch goal.

### 7.2 Paper mill complex (private): "Kuusikosken Paperi Oy"
- **Footprint 5x5**, built from **modules** that are ordinary ≤ 3x3 defs (`BCAT_SPECIAL`, new flag `BDF_MODULE`).
  The renderer and `def_selfcheck` (max 3x3) stay unchanged. One fixed orientation (art cost, section 9). Layout:
  ```
  L L M M M      L = log yard 2x3 (log piles, crane)     M = machine hall 3x3 (ridge skylights)
  L L M M M      B = boiler house + tall chimney 2x2      D = digesters and chip silos 2x2
  L L M M M      G = gate, office, clock 1x2
  B B D D G
  B B D D G
  ```
- **Placement:** one side touches lake or river water (log pond, process water); unlocks at **Church village**
  (500), any year; preview shows "Wood here: forestry 0 + regional 40 = 40 / 40 needed for stage 1". **Cost to the
  town 5,000 mk** (site, roads, water rights). The company pays no upkeep, and the town pays none.
- **On rapids** (`BDF_NEEDS_RAPIDS` on any footprint tile): the mill's own hydro station supplies the mill
  and **+60 power units to the grid**. The *koski* in the town's name earns its keep.
- **Stages** (the mill grows with the town; each stage adds modules or activates them, with a ticker and a newspaper line):

| Stage | Trigger | Jobs | Wood | Power | Water | Air pollution | Exports |
|---|---|---|---|---|---|---|---|
| 1 groundwood mill + PM1 | placement (6 months building) | 300 | 40 | 40 | 30 | 250 | West 80 / East 20 |
| 2 pulp mill + PM2 | town ≥ 3,000 and wood ≥ 80 | 600 | 80 | 70 | 60 | 400 | West 75 / East 25 |
| 3 PM3, "the big machine" | year ≥ 1963, town ≥ 6,000, wood ≥ 120, rail station | 900 | 120 | 100 | 90 | 500 | West 70 / East 30 |
| Modernised | 1970s event, choice | 600 | 120 | 100 | 90 | 350 | same output |

- The **smell**: land value −20 within 8 tiles (the Äänekoski joke goes in the ticker: "Residents call it the smell of
  money"). Lake pollution later, with the WATER/sewage work (Water Act 1961 event).
- Jobs enter the job pool like any lot, so R demand jumps with each stage. The complex is **outside** the I-zone
  demand (it has its own contract), but adds +10% of its jobs to Engineering's local need (repair shops and
  subcontractors grow around it).

### 7.3 The *patruuna* and the company-town rules
- **Dependency** `Dep = complex jobs / all jobs`, shown on the complex query and in the top problems when ≥ 40%:
  "Company town: 58% of jobs at Kuusikoski mill".
- **Company town** (Dep ≥ 40%): the company pays **20% of school, clinic and fire upkeep** (they ran factory schools,
  health care and fire brigades). Budget line "Company contribution". Patron gifts and demands are active, and risk
  events hit harder (layoffs scale with Dep).
- **Patron mood** 0-100 (start 60), monthly drift toward a target: +10 business tax ≤ 9%, −10 per point above, +10
  railway station, +5 wood supply 100%, −20 smokestack-standards decree, −10 Water Act enforced, +10 for each placed
  gift (fades). Shown as the patron's portrait expression on the complex panel.
- **Gifts** (mood ≥ 70, at most one per 2 years): the patron *offers*, the player *places* it free within 12 tiles of
  the mill (no auto-placement):
  *workers' row houses* (3x1 wooden, 30 residents, counts as tier 2 with its own sauna shed),
  *company sauna* (a public sauna with zero upkeep while the patron lasts), *clubhouse* (happiness, Workers +),
  *church* (the Mänttä gift: the existing church def, free), *factory school* (school with upkeep paid by the company),
  *the patron's villa* (land value landmark; not open to the public, says the tooltip).
- **Demands** (event cards, 2-3 choices with explicit numbers, at most one a year): "Lower the business tax by one
  point", "Grant the mill a water permit" (output +10%, lake pollution, Intelligentsia −), "The union wants a workers'
  hall on company land" (Workers + / patron −), "Build a rail spur" (station request). Low mood (≤ 30) brings a threat:
  "The board may put the new paper machine in another town" (stage 3 is delayed until mood recovers).
- **Risk events** (always telegraphed 2-3 months ahead in the ticker and newspaper, DESIGN pillar 7):
  - *Paper slump in London* (weighted to 1958, 1967, 1971, 1975-77, ±1 year): output and jobs −30% for 12-18 months.
    The laid-off join the unemployed: they find other jobs or emigrate.
  - *General strike* (March 1956, fixed, 19 days; later strikes random, weighted by the Workers bloc): output 0 for
    one month, and the patron's mood drops.
  - *Modernisation* (1972-78, one card): "Install the new paper machine with 300 fewer jobs?" Accept: jobs 900 → 600,
    output kept, pollution −30%. Refuse: output −20%, and the mill becomes uncompetitive after 1985 (a second card).
  - *Merger* (1980s): the family sells to a Helsinki group, the patron leaves, gifts and contributions stop, the mill
    stays. A soft end of the era.
  - *Mill fire* (log yard; disasters on).
- **Bloc effects:** Industrialists + (mill output), Workers + jobs and − paternalism (mood ≥ 80 costs a little Workers
  approval), Rural + via forestry jobs.

### 7.4 *Valtion Metalli* works (state): "Valtion Metalli, Kaupunki works"
- **Footprint 5x4** of modules, one orientation. It needs a **railway** tile within 2 and **Market town** (3,000).
  ```
  H H H Y Y      H = sawtooth assembly hall 3x3 (north-light roof, red brick)
  H H H Y Y      Y = gantry-crane yard 2x3 (tractors / paper-machine rolls in the open)
  H H H Y Y      S = rail siding with a goods wagon 3x1
  S S S G G      G = gate building with clock 2x1
  ```
- **Placed by presidential decree** (politics on: authority cost 10, Eastern relations +5). Without politics it is a
  normal unique building for 6,000 mk. No wood. Power 60, water 20, pollution 200.
- **Product lines unlock with the years**, each +300 jobs: tractors (1955), paper machines (1960; +10% output to any
  paper mill in the town: "our own machine"), rolling stock and ship sections (1965), up to 1,200 jobs.
- **Contract:** 85% clearing (machinery). Every **five-year agreement** (1956, 1961, ...) changes output by ±25%
  depending on Eastern relations. **1990**: clearing ends, output −50%, and the newsreel closes the era.
- No patron: **the Ministry** sends a director. Gifts are state projects (a vocational school, an ARAVA flats quota of
  free apartment lots). Demands are production targets ("the quota for the anniversary"). Strikes hurt Eastern
  relations as well as output. The same dependency rules apply (company contribution 10%, the state is stingier).

### 7.5 Data
```c
typedef struct {            /* g_complex[MAX_COMPLEX = 4]; save chunk "CPLX" */
    u8  kind;               /* CPX_PAPER, CPX_METAL (stretch: CPX_SHIPYARD, CPX_TEXTILE) */
    u8  x, y, stage;        /* north tile of the template; 0 = building */
    u8  mood;               /* patron / ministry 0..100 */
    u8  flags;              /* rapids hydro, modernised, merged, company-town active */
    s8  output_pct;         /* event modifier, -100..+50 */
    u8  event_months;       /* remaining months of the current event */
    u16 jobs, gift_cooldown;
    u16 lots[6];            /* module lot ids */
} Complex;
```
Module lots point back via the free `Lot.spare` (complex index + 1), so the Lot stays 20 bytes. A query on any module
opens the complex panel.

---

## 8. Names (fictional, THEME 8.6 style; no real brands)

| Kind | Generator / examples |
|---|---|
| Paper company | "`<Lake/Rapids>n Paperi Oy`", "Oy `<Place>` Ab", "`<Place>`n Tehtaat": **Kuusikosken Paperi Oy**, Oy Pohjolan Selluloosa Ab, Koskensaaren Tehtaat, Kaarnalahden Tehtaat, Honkaniemen Puuhiomo |
| Patron | "Patruuna `<first>` `<Swedish-sounding surname>`": Patruuna Albin Hellstedt, Gustaf Ekwall, Carl Lindfors; the villa "Hellstedtin kartano" |
| Sawmill | "`<Place>`n Saha", "`<Place>`n Höyrysaha" (steam sawmill), "`<Place>`n Puutavara Oy": Hirvisaaren Saha, Rantalan Höyrysaha, Lohilahden Puutavara Oy |
| State works | **Valtion Metalli** (VM, "vee-äm"), works name "Valtion Metalli, `<Town>` works"; tractor **VM Kontio 35**, paper machine "VM-PK 3"; satire filler "Valtion Tykkitehdas, now making tractors" |
| Engineering | Pohjolan Konepaja, Suomen Valssi Oy, `<Place>`n Valimo (foundry) |
| Textile / shoes | Koskelan Puuvilla Oy, Kotimaan Kenkä Oy, Hirvikosken Vaatetehdas, Kutomo Aalto |
| Dairy | "`<Place>`n Osuusmeijeri" (co-op), central co-op pastiche **"Maitokunta"**, butter brand *Kultavoi* |
| Electronics | Pohjolan Kaapeli Oy (cable, rubber boots, later TVs: the Nokia joke), Salo-pastiche "Saloria" TVs [tone check] |
| Logging camp | "`<Place>`n kämppä", forestry company "Järviseudun Metsä Oy" |

Mill-town names: the town generator could favour **-koski** names when a paper mill is placed on rapids (ticker:
"Kuusikoski: a mill town is born").

---

## 9. Art needs and VRAM

Assumes HANDOFF's plan: **only the active zoom's sheets are resident**. Today 64-px art is ~7.2 MB and 32-px art
~1.8 MB. With only one zoom resident, the 9 MB limit leaves **~1.9 MB for all new 64-px art** (apartments T3,
commercial T3 and industry T3 also need it), and the 32-px set follows at 1/4. Sizes below are 16-bit; a sprite costs
about 8 KB (1x1), 28 KB (2x2), 45 KB (2x3), 56 KB (3x3) per rotation at 64 px.

| Item | Footprint | Variants × rotations | KB @64 | KB @32 | When |
|---|---|---|---|---|---|
| Industrial yard (timber, steel, bales, milk cans, coal/shed) | 1x1 | 5 × 1 (NO_ROTATE) | 40 | 10 | **fix round** (fits the current 190 KB headroom) |
| Forest stages (stumps, seedlings) | tile | 2 × 2 species looks | 24 | 6 | M2 |
| Logging camp (horse / tractor) | 1x1 | 2 × 1 | 16 | 4 | M2 |
| Farmsteads T1 / T2 / T3 | 1x1 | 2 per tier × 2 rot | 96 | 24 | M2 |
| Co-op dairy T1 | 2x2 | 1 × 4 | 112 | 28 | M2 |
| Paper mill modules (L, M, B, D, G) | 5x5 total | 1 orientation | 190 | 48 | M2 |
| *Valtion Metalli* modules | 5x4 total | 1 orientation | 160 | 40 | M2 |
| Workers' row houses / villa / clubhouse | 3x1 / 2x2 / 2x2 | 2 rot / 1 / 2 | 48 + 28 + 56 | 33 | M2 |
| **M2 "must" subtotal** | | | **~770** | **~190** | |
| Sawmill T2 (planing mill, kiln) | 3x3 | 1 × 4 | 224 | 56 | M2 if room, else stretch |
| Central dairy T2 | 2x2 | 1 × 4 | 112 | 28 | stretch |
| Electronics T2/T3 | 2x2 / 3x3 | 1 × 4 | 336 | 84 | stretch |

Savings to pay for it:
- **Reuse:** the existing sawmill, workshop, factory and 4-storey mill art become the sawmill, engineering
  (T1/T2) and textile T2 kinds. Workshop variant v1 becomes the textile sewing workshop and a timber-stacked variant
  the joinery.
- **Complexes:** one orientation and modules (no full 5x5 sprite in 4 rotations, which would be ~550 KB at 64 px for
  one mill). No abandoned art: a closed mill is drawn without smoke and with dark windows (one palette pass at
  generation, applied only to the hall module, +56 KB if wanted).
- **No abandoned variants for the new industry kinds** (renderer fallback to the normal sprite). Abandoned art is
  the biggest family today (1,937 KB); dropping industry abandoned v0 sprites frees roughly 300 KB.
- Smoke hotspots: boiler chimney (paper), sawdust burner (sawmill T1 exists), dairy chimney.

---

## 10. Simulation changes (summary for the implementing task)

| Area | Change |
|---|---|
| `defs.h/.cpp` | `BuildingDef` + `u8 kind` (IK_NONE, IK_WOOD, IK_METAL, IK_TEXTILE, IK_FOOD, IK_ELEC), `u8 input` (wood/milk), use = per def; new defs (yard, dairy T1/T2, sawmill T2, joinery, farm T1-T3, logging camp, complex modules, gift buildings); flag `BDF_MODULE`; kind export share table; `REQ_WOOD`, `REQ_MILK`, `REQ_RAIL` |
| Zones | `ZONE_FORESTRY` = 5, `ZONE_FARM` = 6 (fits the 3 zone bits); zone tool fly-out gets two keys; paint validation (forest only / field+grass) |
| `net.cpp` | empty zoned tiles conduct (Houses/Apartments/Commercial/Industry only); later the same for water |
| `sim.cpp` | kind pick in `process_empty`; yard filler rule; kind sub-demands (stored for the tooltip); wood/milk supply and allocation at month end; harvest + regrowth (1/4 of the rows per cycle in phase 12's slot: ~16k tiles max, a few hundred µs on the P2); farm claims; farm demand F; complex stage/events/patron monthly |
| `budget.cpp` | export income by kind; "Company contribution" line; complex placement cost |
| `politics.cpp` | clearing categories per kind; patron/ministry cards; *pellonvaraus*; *Valtion Metalli* decree |
| Save | `Tile.aux` already holds density; new chunk `CPLX`; sub-demand smoothing state in `SIMX` |
| Query panel | kind + **why** ("Sawmill: lakeside log pond, wood supply 82%"), input line, export split, jobs, pollution; forestry tile ("Young forest, 7 of 12, mature in 20 months; 6 tiles cut this year"); farm (fields claimed, milk, tier needs); complex panel (stage, jobs, output %, dependency, patron mood, next gift, contribution) |
| Overlays | "Industry kinds" (lot footprints coloured by kind, 5 of the 8 levels); "Forest" (density + forestry zone + clear-cut) |

All integer, deterministic, fixed arrays. Performance: kind pick is O(kinds) per new lot. The harvest scan is the
only new per-tile loop and is time-sliced. Pools are a handful of s32 values.

---

## 11. Balance impact and tests
- **Gap fix:** industry bands fill completely: +10-15% jobs per zoned industrial area (yards at 5 jobs/tile, the
  3-deep strips). The I-demand absorbs it. Re-run `T=balance` and expect slightly earlier plateaus in the
  `industry` style.
- **Kinds:** total I is unchanged by construction (shares sum to 100), and the fallback kind means no stall. Watch the
  export line: recalibrate rates so `planner s7` 1970 exports stay ~336 mk/month.
- **Forestry/farms:** farms add ~0.6 residents per farm tile at tier-1 tax weight (not a money machine). Logging adds
  ~1 job per 8 forestry tiles. Both are small next to a 10k town and matter for the Rural bloc.
- **Complexes:** a stage-2 mill (600 jobs) in a 3,000 town is ~40% of jobs, so the company-town state is intended.
  A slump (−30% of 900 = 270 jobs) must not cost more than ~15% of the population (the job-shock test in BALANCE 2
  shows ~2 residents per job, so −540 in a 6-10k town). The 5,000 mk placement cost plus the contribution (20% of
  school/clinic/fire upkeep ≈ 150-300 mk/month at 5-10k) should net about +200 mk/month at stage 2. Tune.
- **New bot style `mill`:** Lakeland s7, a paper mill at year 3 on the best shore, 150 forestry tiles. Assert stage 3
  by 1970, dependency peak ≥ 40%, a slump drop of < 20% population, recovery within 3 years, no loan spiral.
  Add an assert to `industry`: no industrial tile stays empty for more than 24 months while I > 0.

---

## 12. Milestone placement
- **Next fix round (small, no new systems):** empty zoned tiles conduct power (net.cpp, a few lines + the struct-serial
  check); `ind_yard_1x1` def + filler rule + 5 variants of art (40 + 10 KB); Query line. Optionally, rename the
  existing defs' display names toward their future kinds ("Workshop" → "Engineering workshop").
- **Milestone 2:** kinds + sub-demand + export rates; forestry zone; farm zone + co-op dairy; paper mill complex with
  the patron (needs the event-card UI from politics); *Valtion Metalli* (decree); clearing categories; overlays;
  `mill` bot test.
- **Stretch:** electronics; sawmill T2 / central dairy art; shipyard complex (Coast preset: icebreakers for the East);
  textile complex (River preset); peat bog zone feeding the 1970 peat plant (marsh terrain); lake pollution; the
  "Company town start" New Game option; the preferred-kind brush.

---

## 13. Open questions for the user
1. **Gap fix:** fillers **and** "zoned land conducts power" (recommended)? Should zoned land also conduct **water**
   once pipes exist (recommended; zones never cross roads, so pipes still matter)?
2. **Kind choice:** automatic by site and demand, with the reason shown in Query (recommended), or a player brush to
   force a kind per zone?
3. **Forestry and farm zones** as two new paintable zones, with forest regrowth compressed to 4 years: OK?
4. **Complexes:** generic zones plus optional unique complexes (recommended)? Paper mill first? Should placing one
   cost the town money (5,000 mk proposed)?
5. **Patron:** gifts as free placement offers (recommended) or auto-built by the company? How sharp should the satire
   be (villa "not open to the public", the smell of money)?
6. ***Valtion Metalli***: placed by presidential decree (politics on) or simply unlocked at Market town?
7. A **"Company town start"** New Game option (the mill already standing): worth it later?

---

## 14. Sources
- Valmet / VMT history: [Tourula (Wikipedia)](https://en.wikipedia.org/wiki/Tourula), [Valtra](https://en.wikipedia.org/wiki/Valtra),
  [Valmet journey](https://www.valmet.com/company/journey/), [Metso history](https://www.fundinguniverse.com/company-histories/metso-corporation-history/),
  [Finnish war reparations](https://en.wikipedia.org/wiki/Finnish_war_reparations_to_the_Soviet_Union)
- Trade: [Bank of Finland bulletin 1971/05](https://publications.bof.fi/bitstream/10024/49803/1/Bofbul_1971-05.pdf),
  [BoF bulletin 1975/11](https://publications.bof.fi/bitstream/10024/49418/1/Bofbul_1975-11.pdf),
  [Finnish trade with the USSR: why was it different? (BoF)](https://publications.bof.fi/handle/11111/8667),
  [ETLA dp1079](https://www.etla.fi/wp-content/uploads/2012/09/dp1079.pdf),
  [GlobalSecurity: Finland history](https://www.globalsecurity.org/military/world/europe/fi-history-35.htm),
  [CIA: Finnish trade with the Bloc](https://cia.gov/readingroom/document/cia-rdp79t00935a000400010002-8),
  [CS Monitor 1987](https://www.csmonitor.com/layout/set/amphtml/1987/0918/fecon.html),
  [Holopainen: Finnish forest product exports 1950-61 (BoF)](https://publications.bof.fi/items/21965965-c9f2-4bbd-b752-3110fcec63a5/full)
- Log floating: [Timber rafting tradition (Finnish heritage wiki)](https://wiki.aineetonkulttuuriperinto.fi/wiki/Timber_rafting_tradition),
  [Timber rafting (Wikipedia)](https://en.wikipedia.org/wiki/Timber_rafting), [Silva Fennica 7272](https://www.silvafennica.fi/article/7272)
- Agriculture and migration: [Stenhammar: Essays on Structural and Technological Change](https://helsinkigse.fi/articles/essays-on-structural-and-technological-change),
  [Heikkilä: migration to Sweden](https://www.norden.ee/images/regionaal/info/migra/Elli_Heikkila_78MarchTallinnpdf.pdf),
  [Valio (Wikipedia)](https://en.wikipedia.org/wiki/Valio), [Valio history](https://www.valio.com/history/),
  [Fennia: Finnish dairy industry 1910-1967](https://fennia.journal.fi/article/view/40749)
- Company towns: [Mänttä-Vilppula](https://en.wikipedia.org/wiki/M%C3%A4ntt%C3%A4-Vilppula), [Serlachius: Gösta Serlachius](https://serlachius.fi/?p=2950),
  [Serlachius: splendid places](https://serlachius.fi/?p=4219), [Varkaus cultural heritage](https://varkaus.fi/en/konsti/tunne-kulttuuriperintosi/kulttuuriymparisto),
  [Varkaus and Aalto](https://visit.alvaraalto.fi/en/destinations/varkaus-industrial-history-lake-saimaa-waterways-and-caviar/),
  [Jämsänkoski](https://en.wikipedia.org/wiki/J%C3%A4ms%C3%A4nkoski), [Valkeakoski](https://www.wikipedia.com/wiki/Valkeakoski),
  [Ahlstrom history](https://www.fundinguniverse.com/company-histories/ahlstrom-corporation-history)
- Games: [SimCity 4 (Wikipedia)](https://en.wikipedia.org/wiki/SimCity_4), [StrategyWiki SC4 gameplay](https://strategywiki.org/wiki/SimCity_4/Gameplay),
  [GameSpot: C:S Industries review](https://gamespot.com/reviews/cities-skylines-industries-review-the-up-and-up/1900-6417029/),
  [OpenCritic: C:S Industries](https://opencritic.com/game/6795/cities-skylines-industries/reviews),
  [TheSixthAxis: Industries](https://www.thesixthaxis.com/2018/10/25/bring-some-production-to-your-construction-in-cities-skylines-industries-expansion/);
  plus DESIGN_LESSONS 2.4 (Workers & Resources), 2.5 (Tropico), 2.7 (Banished), 2.8 (Anno)
