# KAUPUNKI: Game Design Document

Title: Project Lähiö (working title was KAUPUNKI)

Working title. Rev. 1 (2026-11-02, orchestrator 86box-86). This is the spec. When a task agent learns something
that changes the spec, the orchestrator updates this file; agents append notes to `docs/HANDOFF.md`.

Background material (read the parts you need, don't duplicate them here):
- `docs/research/THEME.md`: history, architecture specs with hex colours, satire tone, naming lists.
- `docs/research/DESIGN_LESSONS.md`: genre lessons, feasibility numbers, simulation sketches (sections 3-5).
- `art/explore/STYLE.md`: the art style guide (projection, anchors, sorting, palettes, generator structure).
- `probe/RESULTS.md`: rendering and simulation measurements on the VM.

---

## 0. Summary

An isometric **sandbox city builder** for Windows 98, set in **Finland, 1955 onwards**: post-war reconstruction,
the great migration from the countryside, wooden *rintamamiestalo* suburbs giving way to concrete *lähiö* blocks.
The player is **the President** (name chosen by the player; "Kekkola" in tests), officially above politics, in
practice deciding where every new suburb goes, balancing the big neighbour in the East against trade with the West.

Grid building in the classic tradition (roads, zoning, power, water, services, budget), designed with the lessons of
modern city builders: every number explained, preview before commit and undo after, networks as decisions rather
than chores, aggregate simulation with cosmetic life on top.

### 0.1 User decisions (binding)

| Topic | Decision |
|---|---|
| Mode | **Sandbox only.** No story or campaign mode for now. Themes live in systems (Soviet clearing trade, emigration to Sweden, wood→concrete). |
| President | **Player chooses the name** at New Game (English allowed). Default/test name "Kekkola". Pixel caricature, never photos. |
| Language | **English** UI and text. Finnish only for a few flavour words: house types (*rintamamiestalo*, *lamellitalo*, *pistetalo*), *sauna*, place names. Font must have ä ö å Ä Ö Å. |
| Art | Style **C "Nordic hybrid"** from `art/explore` (muted, desaturated; terrain recedes; buildings read). **Not too colourful.** Cars (Lada 1200 shape) are the colourful exception. |
| UI | **Teak & bakelite** 1960s radio/desk look (as in `art/explore/mockup_C.png`). |
| Winter | **Later** (not in v1.0). Keep the art pipeline able to add a snow pass. |
| Priorities | **Art quality and engine quality first.** Smooth scrolling, no stutter, clean pixels. |

### 0.2 Design pillars

1. **Every number is visible and explained.** Overlays, itemised tooltips, a "why isn't this growing?" checklist.
2. **Aggregate simulation, cosmetic life.** The city is numbers on grids and a road graph; cars and smoke are decoration
   driven by those numbers.
3. **Preview, then undo.** Cost, rejected tiles and projected effects shown while dragging; free undo within the month.
4. **Networks are decisions, not chores.** Draw roads and power lines. Water (later district heat) runs in roads.
5. **Theme is mechanics.** Rural migrants are demand; the unemployed emigrate to Sweden; the element factory caps
   concrete growth; wooden districts burn; clearing trade with the East drives industry; the sauna is a service.
6. **Politics reads the city.** Bloc approval is computed from what you built; decrees change simulation parameters.
   Satire is in the words; the numbers are honest.
7. **Fail softly.** Smoothed demand, slow reversible decline, warnings before crises. Sandbox: no game over.
8. **Respect the machine.** Fixed arrays, integer deterministic simulation, cached world layer, pre-rendered text,
   display-locked frame pacing, `-bench` from day one.

---

## 1. Target machine

**Spec levels (user decision):** the 86Box VM (Pentium II 233) is the **minimum spec** and the development machine.
The **real targets are the IBM ThinkPad X41 and T40** (GHz-class Pentium M, Win98 SE), which have far more CPU. Design
so the game is fully playable at minimum spec. Extra CPU on the targets buys smoothness and larger maps, not required
features. Re-check blit performance on the real machines (their graphics chips differ from the Voodoo3).

**Frame rate:** target **60 fps** (smooth scrolling). If frames overrun because the simulation needs the CPU (e.g.
speed 3 on a big city at minimum spec), the loop **drops to 30 fps automatically** (present every 4th 120 Hz blank,
with hysteresis so it doesn't flicker between rates); INI `fps=60|30|auto` (default auto). 30 fps is acceptable to the user.

Minimum-spec VM: Pentium II 233, 128 MB, Voodoo3 (≈14 MB free VRAM), Win98 SE, DirectX 6.1 runtime, VC6 with DX5-level
headers (`IDirectDraw`/`IDirectDrawSurface`, no D3D needed). See `../sandline/probe/RESULTS.md` and our
`probe/RESULTS.md`. Key rules (CLAUDE.md section 6): hardware blits from VRAM surfaces, never lock VRAM per frame,
blit count is the cost driver (~7.5 µs CPU per blit), QPC timing, frame cap, 16 bpp RGB565 (detect 555), sound
via a worker thread.

**Measured (probe/RESULTS.md):** 640x480, 800x600 and 1024x768 at 16 bpp all run with hardware blits (VRAM free
14.0 / 13.3 / 12.2 MB). Offscreen surfaces may be as wide as the mode, no wider. CPU ≈ 5 µs per BltFast; the emulated
blitter adds ≈ 4.5 µs + 8 ns/px and runs in parallel with the CPU. **A full isometric redraw every frame is not viable**
(874-4838 blits); a **VRAM chunk cache** hits the flip cap at every resolution and zoom with < 1 ms CPU per frame.
A full simulation tick on 128² costs ≈ 7.7 ms (256²: 31.5 ms). Comfortable heap ≤ 32 MB.

---

## 2. Game setup (sandbox)

**New Game screen** (one panel):
- President's name (text field, default "Kekkola"), town name (random from THEME 8.1, editable).
- Map: random seed (shown, editable) or a premade map; map type preset: *Lakeland* (default), *River town*,
  *Coast*, *Forest*. Preview of the generated map (minimap render) with a "Reroll" button.
- Start year: 1955 (default), 1960, 1965, 1970. Later start = more tools unlocked, more starting money.
- Starting funds: Generous / Normal / Austere.
- Toggles: politics on/off, disasters on/off, "all tools unlocked" (pure creative mode).

The map starts empty except terrain, a **national road** (highway link) entering from one edge, optionally a railway
line along another edge, and the **town hall** placed by the player as the first action (or auto-placed near the
road). Everything is sandbox: no goals, no game over. Optional *milestones* (section 7) give structure.

---

## 3. Map and terrain

- **128 x 128 tiles**, flat (Finland is flat; no slope art). Size constants allow 192/256 later (stretch).
- 1 tile = **12 m** (art scale: STYLE.md 3). A 128² map is 1.5 km x 1.5 km; with dense concrete that holds
  ~60,000-100,000 residents, which is the right ceiling (Tampere ~130k in the 1960s).
- Terrain types (4 bits): grass/meadow (3 variants), forest (mixed, spruce, birch, pine), granite outcrop (*kallio*),
  water (lake), river water (with flow direction, for hydro), shore (autotiled from land/water corners), field
  (crop, ploughed), marsh (stretch).
- **Forest** is buildable after clearing (costs money, gives a one-off timber income; Rural bloc dislikes mass
  clearing). Rock is buildable at extra cost (blasting) or left as a park-like feature that raises land value nearby.
- Lakeside raises land value. Rivers with rapids allow a hydro plant.
- **Procedural generator** (`mapgen.cpp`, deterministic from seed): lakes via thresholded smoothed value noise +
  cellular-automata cleanup, optional river carved downhill along a noise "height", forests by noise with
  species by moisture, rock outcrops as small blobs, fields near the national road. Must produce clean shores
  (no single-tile diagonal pinches the shore autotile can't draw).

---

## 4. Building and tools

All tools share one **mouse grammar**: left-drag builds with a live preview (cost, rejected tiles hatched red,
projected effects); right-click cancels; right-click with no tool = query. Edge-scroll (toggle), arrow keys/WASD,
middle-drag to pan.

### 4.1 Roads

| Class | Era | Capacity | Speed | Cost/tile | Notes |
|---|---|---|---|---|---|
| Gravel road (*soratie*) | 1955 | 300 | slow | cheap | dust pollution, cheap upkeep |
| Street (asphalt) | 1955 | 800 | medium | normal | the default |
| Main road (avenue art, 1 tile) | 1960 | 2,000 | medium-fast | high | noise; trees/lamps on art |
| Motorway | 1965 | 4,000 | fast | very high | no zone access; connects only at junctions (v1.0+) |

- Drag tool draws an L-shaped path (Shift = straight only). Dragging a higher class over an existing road
  **upgrades in place** and charges the difference. Bridges over water (straight runs only, max 8 tiles).
- Road pieces are autotiled (straight, curve, T, cross, end) per class. Railway: fixed line from map start in MVP;
  a station building connects it (v1.0).

### 4.2 Zones

Painted with rectangle drags; development is automatic. Zones only accept tiles within **3 tiles of a road**
(rejected tiles shown hatched). Zone colour shows as a subtle ground tint/outline until built.

| Zone | Era | Lot sizes | Tier 1 | Tier 2 | Tier 3 |
|---|---|---|---|---|---|
| **Houses** (low R, the *rintamamiestalo* zone) | 1955 | 1x1 | small cottage / barrack, 3 res. | *rintamamiestalo* 1½ storeys, 6 | 2-storey *rintamamiestalo* with garage/sauna shed, 8 |
| **Apartments** (high R) | 1955 (T1), 1960 element factory (T2+) | 3x1, 2x2 | 3-storey rendered *lamellitalo* (gable roof), 25/tile | 4-5-storey precast *lamellitalo*, 45/tile | 8-storey *pistetalo* / 1970s *elementtitalo*, 70/tile |
| **Commercial** | 1955 | 1x1, 2x1, 2x2 | kiosk / co-op shop, 4 jobs/tile | shop row with flats above, 10 | department store / shopping centre, 20 |
| **Industry** | 1955 | 2x2, 3x3 | workshop / sawmill, 10 jobs/tile | factory (sawtooth roof, brick chimney), 25 | large works, 40 |

- **Density is the zone type**, not a slider: wood and concrete are different worlds, and the transition is the
  story. Bulldozing a wooden district for apartments is meaningful (and political).
- Buildings face the road they were built from (4 rotations; art has all rotations, STYLE.md 4).
- Visual variety: each tier has several variants (colours, porches, chimneys); picked by a hash of the tile so it is
  deterministic.
- v1.0 adds industry *kinds* (sawmill/paper near water and forest, metal works, textiles, later electronics) chosen
  by demand and location; MVP has one generic industry.
- Stretch: *Rivitalo* (row house) zone, agriculture/forestry zones, summer-cottage shore.

### 4.3 Utilities

**Power** (pylons + adjacency): built tiles conduct to their 4 neighbours; power lines on wooden poles bridge gaps
and water. Plants have capacity in "power units" (1 unit ≈ demand of one tier-2 house tile).

| Plant | Era | Size | Capacity | Notes |
|---|---|---|---|---|
| Coal plant | 1955 | 2x2 | 400 | polluting, upkeep |
| Hydro plant | 1955 | 2x2 on river rapids | 600 | river-only, clean, expensive |
| Oil plant | 1965 | 2x2 | 900 | needs oil (clearing trade; 1973 price shock) |
| Peat plant | 1970 | 2x2 | 700 | domestic fuel; Rural bloc likes it |
| Nuclear plant | 1977 | 3x3 | 3,000 | East/West reactor choice event (THEME 6.2 "Eastinghouse") |

Brownout: if demand > supply, tiles furthest (in BFS order) from plants lose power first; overlay shows it.

**Water** (mains inside roads): sources are a **pumping station** on a lake shore (capacity ∝ adjacent water tiles,
max 500 units) and a **groundwater well** (small, anywhere, 150). A lot is watered if it touches a watered road.
A **water tower** (landmark, several modernist designs: saucer, mushroom, cylinder) raises capacity by 30% for
everything within 20 road tiles and is required for Apartments tier 2+ ("water pressure"). v1.0: **sewage
treatment** (lake quality) and **district heating** (heat plant, mains in roads, required for Apartments T3).

### 4.4 Services

Road distance plus capacity (no radius circles, no walkers). Each building: capacity, max road distance, upkeep.

| Service | Era | Size | Covers | Capacity |
|---|---|---|---|---|
| Fire station (VPK hall, hose tower) | 1955 | 1x1 | fire risk; wooden districts | 1,500 res. |
| School (*kansakoulu*) | 1955 | 2x2 | education → tier upgrades, land value | 400 pupils (≈20% of residents) |
| Health clinic | 1955 | 1x1 | health | 2,000 res. |
| **Public sauna** | 1955 | 1x1 | hygiene + happiness; needed for houses T2; obsolete for Apartments T3 (own saunas) | 600 res. |
| Police station | 1960 | 1x1 | crime (v1.0) | 3,000 res. |
| Library | 1960 | 1x1 | culture, intelligentsia bloc | 4,000 res. |
| Post & telephone exchange | 1960 | 1x1 | phone lines; houses T3 needs it (the "phone queue" satire) | 3,000 res. |
| Health centre (*terveyskeskus*) | 1972 | 2x2 | upgraded health | 8,000 res. |
| Park / sports field / ice rink | 1955/60/65 | 1x1-2x2 | land value by **radius** (the one exception: parks are walked to) | — |
| Church (modernist) | 1955 | 2x2 | land value + happiness landmark | — |

Query on a service shows "School: 412/500 pupils" and highlights its catchment.

### 4.5 Special buildings

Town hall (first building; start of the road network), **element factory** (industrial, 3x3, 1960: produces
precast elements; Apartments T2+ growth per month is capped by its output), railway station (v1.0; regional
migrants, freight), harbour (stretch), **presidential sauna** (unique; sauna diplomacy), monuments
(friendship statue from events), landmarks unlocked by milestones: concert hall, TV tower, ski jump.

### 4.6 Bulldoze, undo, query

- Bulldoze: rectangle drag; refunds 25% for recently built; demolishing occupied homes upsets their bloc.
- **Undo/redo** (Ctrl+Z/Y): unlimited within the current game month (command log with inverse ops and refunds;
  cleared at month change because the simulation has moved on).
- **Query** (Q or right-click): a panel with the lot's numbers and the **requirement checklist** for the next tier:
  "✔ Road ✔ Power ✘ Water (no main within reach) ✔ School 312/400 ✘ Land value 31 (needs 40)".

---

## 5. Simulation

### 5.1 Principles

- **Deterministic integer simulation**: fixed-point or natural-unit ints, own xorshift RNG in the save. Same save +
  same inputs = same city (reproducible bugs, `-bench` stable). FPU allowed only for UI/preview.
- **Single thread**, time-sliced inside the main loop (except the sound thread). If a fast speed can't keep up, the
  game runs slower; it never skips simulation.
- **Aggregate**: population and jobs are numbers per lot; no individual citizens.

### 5.2 Data (all fixed-size arrays)

```c
#define MAP_W 128
#define MAP_H 128
typedef struct {            /* 8 bytes per tile */
    unsigned char terrain;  /* low 4 bits type, high 4 bits variant */
    unsigned char road;     /* class (0 none) + autotile index cache, or power-line flag */
    unsigned char zone;     /* zone type 0..4 + flags (power, water, heat reached) */
    unsigned char flags;    /* dirty-render, bulldozable, on-fire, abandoned ... */
    unsigned short lot;     /* index into lots[] (0 = none) */
    unsigned char lv;       /* cached land value 0..255 for this tile */
    unsigned char aux;      /* per-type scratch (e.g. water depth, forest density) */
} Tile;

typedef struct {            /* one per building instance (any size) */
    unsigned char def;      /* building definition id (BuildingDef table) */
    unsigned char x, y;     /* north (back) tile of the footprint */
    unsigned char rot;      /* 0..3 */
    unsigned char tier, variant, status; /* status: building, active, abandoned, burning */
    unsigned short occupants;            /* residents or jobs */
    unsigned short age_months;
    signed char growth;                  /* hysteresis counter */
} Lot;
```
The authoritative Tile/Lot layout (every bit documented) is `src/world.h`.
Field maps on a **64x64 grid (2x2 tiles)**, one byte each: pollution, land value, crime, fire risk, noise, happiness,
traffic. Service coverage per service type: nearest building id + road distance per tile (computed by BFS).
Total simulation memory < 2 MB.

### 5.3 Time

- 1 **step** = 1/16 of a **cycle**; 4 cycles = 1 **month**.
- Speeds: pause, 1 (3 steps/s: a month ≈ 21 s), 2 (6 steps/s), 3 (16 steps/s: a year ≈ 48 s).
- Phase table (one phase per step, so all work is spread evenly):

| Phase | Work |
|---|---|
| 0 | date, ticker queue, cosmetic events |
| 1-8 | growth/decline scan of 1/8 of the rows each |
| 9 | power + water network solve (only if dirty) |
| 10 | pollution emit + decay + 3x3 diffuse + wind drift (prevailing SW wind) |
| 11 | land value (1/4 of the map per cycle) |
| 12 | crime, fire risk, happiness (1/4 of the map per cycle) |
| 13 | service coverage BFS, one service type per cycle (round robin) |
| 14 | traffic slice (MVP: commute distance BFS; v1.0: assignment Dijkstra slice) |
| 15 | demand, migration, emigration; on month end: budget, politics, events, history |

### 5.4 Growth

Per lot (or empty zoned tile) in its row slice:
`score = demand[zone] + landValueTerm + serviceTerm − pollutionTerm − commuteTerm ± 20% noise`.
- `score > +T` and requirements met → build a new lot (choose lot size by contiguous free zoned area and demand) or
  upgrade a tier. New buildings show a short **construction stage** sprite (scaffold/foundation) for 1-2 months.
- `score < −T` for N consecutive checks → lose occupants, then a tier; at 0 occupants → **abandoned** (greyed
  sprite variant). Abandonment is reversible when conditions improve.
- **Tier requirements** (explicit, shown in Query):

| Zone | T2 needs | T3 needs |
|---|---|---|
| Houses | power, water, school, public sauna | + clinic, shop within reach, phone exchange, land value ≥ 40 |
| Apartments | power, water + water tower, element supply, school | + district heating (v1.0; MVP: health clinic), low pollution, land value ≥ 50 |
| Commercial | customers within reach (residents by road distance), land value ≥ 25 | land value ≥ 45, main road access |
| Industry | workers within commute reach, I-demand | power reliability, rail or main road access |

- Early game must be fast: the first houses appear within ~10 s of zoning at speed 2.

### 5.5 Demand (transparent, shown as bars with itemised tooltips)

```
R = openJobs + ruralMigrationPull(year) * attractiveness − vacantHomes − emigrationPressure
C = residents * spendPerResident(era, avgTier) − commercialCapacity
I = clearingQuota + westernOrders + localNeed − industrialCapacity
```
- `attractiveness = 0.5 + 0.5*avgHappiness/100 − taxPenalty`. `ruralMigrationPull` is high 1955-70, lower after.
- Each demand moves at most 1/4 of the way to its target per month (damps oscillation and death spirals).
- **Emigration valve**: unemployed residents and families without housing **emigrate to Sweden** instead of
  rioting. A visible counter ("Emigrated to Sweden: 1,240") plus a ticker line; it hurts the Workers bloc. It's
  historically right (100,000+ left in 1969-70) and a soft, legible failure signal.

### 5.6 Networks and coverage

- Power: one flood fill from all plants over conductive tiles (buildings, power lines) when the network is dirty
  or monthly; brownout by BFS order. Water: BFS from sources over watered road tiles; lots touching watered roads.
- Services: multi-source BFS over road tiles from each building of a type, max distance D; assign each lot to the
  nearest building; `load = Σ demand`; `quality = min(1, capacity/load) × distance falloff`.

### 5.7 Fields

Formulas from DESIGN_LESSONS 4.6, tuned in play. Summary:
- **Pollution**: emit (industry tiers, plants, road flow) → decay ×0.9 → 3x3 blur → wind drift toward NE. Players
  learn to put industry downwind.
- **Land value**: base (lakeside +20, forest edge +5, rock outcrop +5) + services + parks (radius 4) + access −
  pollution − crime − noise − abandoned neighbours; smoothed. Drives tier upgrades **and tax yield**.
- **Fire risk** (MVP, because wooden districts burn): wooden density × (1 − fire coverage) × dry-summer factor.
- **Crime** (v1.0), **happiness** per residential cell (services, land value, pollution, commute, crime, decrees,
  taxes), aggregated per bloc.

### 5.8 Traffic

- **MVP**: connectivity + commute distance. Each 8x8 block's distance to jobs via BFS from job blocks; a flow
  estimate per road tile from BFS predecessor trees drives the traffic overlay and cosmetic car density.
- **v1.0**: road graph (nodes = junctions/ends, edges = runs), 8x8-block origins/destinations, up to 32 destination
  clusters, Dijkstra per cluster with BPR congestion cost, gravity split, method of successive averages over months
  (DESIGN_LESSONS 3.5). Query on a road: "Flow 1,840 / capacity 1,200; 62% from Kivikko to the harbour works".
- **Car ownership** grows from low (1955) to high (1975): traffic grows even when population doesn't (era pressure).
- **Buses** (v1.0): line tool clicking stops; mode split by time vs car ownership.
- **Cosmetic cars**: up to ~150 Lada-style sprites spawned on road tiles with probability ∝ flow, driving tile to tile
  on the right-hand lane, turning at junctions biased by flow. They never decide anything.

### 5.9 Budget

- Monthly ledger. Income: resident tax (∝ residents × land value), business tax, clearing/export share, state
  grants. Expenses: service upkeep (per-service funding slider 50-120%), road/network upkeep, interest.
- Two tax rates in MVP (residents, businesses); v1.0 splits commerce/industry.
- Currency: **markka (mk)**. Flavour event 1963: "New markka: all prices divided by 100" (display only; internal money
  is whole new markka in s32 from the start, old-markka display before 1963 = ×100). Per-tile road/line upkeep is
  defined per 10 tiles (`upkeep10`) to keep integer precision.
- Loans (v1.0): domestic bond (no strings), Western bank loan (cheaper, Eastern trust −), Eastern credit (project-bound,
  very cheap, locks in industrial quota). Negative balance for 6 months → emergency loan at a punitive rate.

---

## 6. Politics and trade (v1.0; can be toggled off)

Design test: it must create a meaningful decision every 5-10 minutes that interacts with the city. Full design:
DESIGN_LESSONS 4.10-4.11 and THEME 6. Summary of what we build:

- **Four blocs** (approval 0-100, computed monthly from the city; weight = population share):
  *Workers* (apartments, industry jobs), *Rural* (wooden houses, forest, farms), *Industrialists* (business tax, power
  reliability, export orders), *Intelligentsia* (schools, libraries, culture, free press). As the city urbanises, weight
  shifts from Rural to Workers: the historical arc emerges from the simulation. **Popularity** = weighted mean.
- **Two external meters**: *Eastern relations* (clearing quota size, oil price/allocation, crisis events) and
  *Western trade* (orders, loans, modern tech unlocks). Many actions trade one for the other ("active neutrality").
- **Authority** (currency of decrees): + popularity/20 per month, cap 30. **Max 5 active decrees**, each with a cost
  and a 12-month cooldown; repeal costs half. Decrees change simulation parameters with explicit trade-offs
  (examples in DESIGN_LESSONS 4.10: housing production programme, rent regulation, medium beer in grocery stores,
  regional subsidy, school reform, smokestack standards, friendship celebrations, **emergency powers law**).
- **Letters** (*the Letter*): a cheap, quick presidential intervention with a cooldown: fast-track a building, soften
  a newspaper headline, push a ministry. Presented as a typed letter with the player's signature.
- **Event cards** (≤ 1 per 1-2 game years, 2-3 choices with explicit effects, wait in the ticker until opened):
  night frost in Eastern relations, a diplomatic note, a general strike, a visiting delegation, a friendship statue,
  the Olympic bid, sauna diplomacy (convert money/time into Eastern trust, diminishing returns).
- **Elections every 6 years** that you never lose: the **margin** is what matters. A thin "151-149" win weakens
  authority for the next term; a landslide strengthens it. The emergency powers law can skip one (Intelligentsia
  and Western trade fall). Satirical, sandbox-friendly, no game over.
- **Clearing trade**: a 5-year agreement lists goods the town's industry can deliver (machinery, prefab houses,
  textiles); it is the main I-demand source. Clearing credit buys only Eastern goods: oil (oil/heat plants), buses,
  materials. The **clearing balance** must stay near zero: a surplus is wasted credit ("stored in the presidential
  sauna"), a deficit lowers Eastern trust. **1973 oil shock**: oil price ×4 in clearing goods; I-demand jumps if you
  can supply it.
- Real people stay generic ("the Ambassador", "the Delegation"); satire targets the system, never war dead,
  evacuees or ordinary people (THEME 6.1, 6.4). No real party names, logos or brands (THEME 8.6 pastiches).

---

## 7. Eras and town status (sandbox structure)

Unlocks use **year + milestone**. The year gives the historical flavour (start year chooses where you begin);
milestones make sure the player earned it. Milestones are **town status** steps, each granted by presidential decree
with a small ceremony (newspaper page, ticker, a reward of money/authority and new tools):

| Status | Population | Unlocks (examples) |
|---|---|---|
| Village (*kylä*) | 0 | gravel road, street, houses, commercial, industry, coal plant, pump, well, fire station, school, clinic, public sauna, park |
| Church village | 500 | church, apartments T1, power line upgrade, sports field |
| Market town (*kauppala*) | 3,000 | main road, library, police, post & telephone, water tower, railway station |
| Town (*kaupunki*) | 10,000 | element factory (if year ≥ 1960), ice rink, department store, buses |
| Regional centre | 40,000 | motorway (≥1965), health centre (≥1972), shopping centre, concert hall |
| City | 100,000 | nuclear plant (≥1977), TV tower, metro debate (stretch) |

| Era | Years | New pressures |
|---|---|---|
| Reconstruction | 1955-59 | basic needs, wooden fires, rural migrants |
| Structural change | 1960-67 | element supply cap, cars appear, concrete suburbs |
| Welfare state | 1968-74 | traffic boom, oil dependence, 1973 oil shock, pollution awareness |
| Late Cold War | 1975-82 | suburb image decline, ageing wooden areas, nuclear choice |
| Renewal | 1983-90 | renovation instead of expansion; 1990: clearing trade ends (soft end-of-era newsreel) |

The game continues indefinitely after 1990 (sandbox). An end-of-era **newsreel** summary is shown in 1990 (and on
demand): population curve, what you built, decrees that defined your era, emigration totals.

---

## 8. UI and UX

### 8.1 Layout (as in `art/explore/mockup_C.png` / `mockup_C_800.png`)

- **Top bar** (dark, ~16 px): date, money (+monthly delta), population, mini R/C/I demand bars, popularity, speed
  buttons, menu. Hover = itemised tooltip; click = the matching panel.
- **Right side panel** (teak veneer, ~100 px): brass name plate (town name), **bakelite tool keys** in a 2-column grid
  (Roads, Houses/Apartments, Commercial, Industry, Power, Water, Services, Parks, Bulldoze, Query; each opens a
  fly-out with the variants: icon + cost + one word), a paper plaque with the President caricature and a short
  quote/advice line, and a brass-framed **minimap** (1 px per tile at 128², click/drag to move the camera, shows the
  current overlay).
- **Bottom ticker** (one line): news and warnings, colour-coded (grey info, amber warning, red urgent, blue political);
  click jumps to the location or opens the event card. Rate-limited: ≤ 1 per topic per month, ≤ 3 queued.
- **Panels** (budget, population/demand, politics, decrees, services, graphs, query): fixed-size, non-modal,
  docked over the right of the map, one at a time, close with Esc/right-click. Style: bakelite frame with paper
  inner area (typed text, rubber-stamp accents for decrees). Must fit 600x420 so 640x480 works.
- **Overlays** on number keys: power, water, pollution, land value, traffic, fire risk, each service, demand
  ("why not growing"), zones. Overlays tint the world (technique from the probe, section 11.4).
- Era-locked tools are visible but greyed, tooltip "Unlocks at: Market town" (free goal-setting).

### 8.2 Hotkeys

| Key | Action | Key | Action |
|---|---|---|---|
| R | roads | 1-9, 0 | overlays / off |
| H, A, C, I | houses, apartments, commercial, industry | B | bulldoze |
| P, W | power, water | Q | query |
| S | services fly-out | Space | pause |
| Ctrl+Z / Ctrl+Y | undo / redo | F1-F3 | speed 1-3 |
| PgUp / PgDn | zoom in / out | M | toggle minimap |
| Home | centre on town hall | J | jump to latest ticker event |
| Ctrl+S / F5 | save / quicksave | Esc | close panel / menu |
| Arrows / WASD | scroll | F12 | screenshot (BMP) |

### 8.3 Feedback

- Floating numbers when building ("−450 mk") and subtle tax "+" puffs at month end (pre-rendered digits).
- **Top-3 problems** box in the population/budget panels, computed ("340 homes without water (Kivikko)").
- **Newspaper front page** each January: 3 headlines generated from the year's biggest changes + one satirical filler;
  full-screen bitmap with pre-rendered text. The masthead is a made-up period paper.
- The President plaque reacts with short lines ("Good, good." / "The people expect results.").

### 8.4 Save, load, settings

Binary chunked versioned file (`KAUP` magic; TILE, LOTS, FLDS, BUDG, POLI, RNG, HIST chunks, RLE): 50-150 KB.
8 slots + yearly autosave (3-file rotation) + quicksave. Saves include the RNG state. `LAHIO.INI`: resolution,
windowed/fullscreen, edge-scroll, scroll speed, ticker verbosity, sound volume, disasters on/off.

---

## 9. Art

Style guide: `art/explore/STYLE.md` (projection, units, light, anchors, strip sorting, palettes, generator). The
production generator `art/gen_art.py` grows out of `art/explore/gen_explore.py` (style C only; the A/B code paths
can go). Rules:

- 2:1 dimetric isometric, **64x32 base tile and native 32x16 zoom-out** (both rendered from the same 3D models, never
  downsampled). Light from upper-left; cool shadows, warm highlights; contact darkening seats buildings.
- **Muted Nordic palette**: grass olive/moss, dark humic lakes, warm concrete greys, earthy single-colour wooden houses
  (ochre, red ochre, faded blue, pale green, cream, grey) with white trim and grey tin roofs. Accents only from
  balcony panels, shop signs, flags and **cars**.
- 24-bit BMP sheets ≤ 640 px wide, RGB(255,0,255) key, colours pre-rounded to RGB565, no anti-aliasing against the
  key; C headers with rects + anchors (south vertex of the footprint).
- All 4 rotations for buildings (front faces the access road).
- Per building type: tier variants, an **abandoned** variant (desaturated, boarded windows, generated by the style
  pass), a **construction** stage (foundation + scaffold), and a fire frame set (stretch).
- Animation (cheap, cosmetic): chimney smoke puffs (sprite frames), water shimmer (2-3 tile variants cycled on
  visible water every ~300 ms), blinking power-plant lights, cars.
- Cars: Lada 1200 (VAZ-2101) silhouette, 9 period colours, 4 directions, both zooms (STYLE.md 9). Later: bus,
  fire engine, ambulance, police car (all generic shapes, no logos).
- UI art: teak/bakelite frame, 13x13 ink pictogram icons on bakelite keys, brass plate, paper panels, the President
  caricature (tall, bald dome, round glasses; pixel art, a few expressions).
- **Font**: hand-drawn proportional pixel font (7 px caps, ä ö å Ä Ö Å, digits, punctuation, § and mk), plus a
  larger headline font for the newspaper and panel titles. Pre-rendered glyph atlas; numbers drawn as digit blits.
- Winter later: generator must keep a material/normal G-buffer path so a snow pass (snow on up-facing surfaces,
  white roofs, frozen water) can be added without new models.

---

## 10. Audio

waveOut (or DirectSound, reusing `../sandline/src/snd.cpp`) on a worker thread; PlaySound is banned (blocks 5-9 ms).
Generated or synthesised SFX (Python, as SIROCCO's `tools/make_sfx.py`): UI click, bakelite key clunk, construction
thud, bulldozer, cash register (tax), ticker chime, typewriter (letters/decrees), rubber stamp, fire bell, newspaper
rustle. Ambient town loop that thickens with population (birds, distant traffic, a sawmill whine near industry).
Music: optional, later (period accordion/tango-flavoured tracker-style loops).

---

## 11. Technical architecture

### 11.1 Engine base

Start from SIROCCO's proven layers (copy, don't share files): `../sandline/src/plat.cpp` (window, DirectDraw setup,
fullscreen/windowed, 565/555 detection, QPC timing, frame cap, lost-surface restore), `gfx.cpp` (blit-oriented
drawing, images split into ≤512 px surfaces, keyed blits, stipple fills, bitmap text), `snd.cpp`, `config.cpp`,
`util.cpp`. Adapt names and resolution. Command line like SIROCCO: `-window`, `-gdi`, `-bench N`, `-shot T file`,
`-exitafter T`, `-seed N`, `-load file`, `-log`, `-fast`, `-test <name>`.

### 11.2 Resolution and zoom

- **Default: fullscreen 800x600x16**, DirectDraw exclusive, 1 back buffer + Flip. Options: **1024x768x16**,
  **640x480x16** (fallback; every panel fits 600x420), and **`-window`** (windowed, for development; frame-capped).
- **Two zoom levels, both pre-rendered by the art pipeline** (no runtime scaling: hardware stretch is point-sampled,
  noisy and no faster): **64x32 = main zoom**, **32x16 = zoomed out**. Stretch: a 2x close-up by magnifying 64-px chunks;
  a 16x8 overview level.
- **Art atlases ≤ 640 px wide** (so 640x480 keeps working), any height. All art lives in VRAM (a keyed blit from
  system memory is 8x slower and can stall). **VRAM art budget at 800x600 ≈ 9.5 MB** for both zooms together
  (≈ 7.5 MB of 64-px art + the 32-px set at 1/4 the size). At 1024x768 subtract ≈ 1.1 MB. The art pipeline reports the
  total VRAM of all sheets and the build fails the check above 9 MB.

### 11.3 Rendering pipeline

1. **Chunk cache.** The static world (ground, roads, buildings, power lines, trees, and the overlay when active) is
   rendered into **256x128 VRAM chunk surfaces**, addressed by world pixel position at the current zoom. A pool of
   visible chunks + one ring of margin (≈ 41 chunks = 2.6 MB at 800x600, ≈ 47 at 1024x768), LRU-recycled.
2. **Chunk rendering** draws every tile/sprite strip overlapping the chunk in painter's order (depth `i + j`, STYLE.md 4
   strip slicing for tall/multi-tile sprites), clipped to the chunk. Cost: 0.4 ms (64 px zoom), 1.2 ms (32 px zoom).
   **Budget: at most ~3 ms of chunk renders per frame**; missing chunks are prefetched in the scroll direction from the
   margin ring, so scrolling never shows holes or hitches. A zoom switch or overlay change flushes the pool (one
   ≈ 15-20 ms frame; acceptable, hide with the next flip).
3. **Dirty updates:** when the simulation or a tool changes a lot/tile, mark the screen rectangle of the old and new
   sprite (including tall parts) and re-render the affected chunks (deferred, within the per-frame budget, nearest to
   the camera first).
4. **Per frame:** ≈ 25-35 opaque chunk blits → dynamic sprites (cars, smoke, fire, cursor, drag preview, floating
   numbers) → **occlusion fix**: for each dynamic sprite, redraw the building strips in front of it, clipped to the
   sprite rectangle (≈ 2 blits per sprite) → UI cache blits → **run the simulation slice** → Flip. The blitter
   works in parallel with the CPU, so issuing all blits *before* the simulation slice makes rendering almost free.
5. **Budget:** at most ≈ 300 dynamic blits per frame at 60 fps (measured absolute limit ≈ 900 blits / 2.4 Mpix).
6. **Frame pacing:** 60 fps cap with QPC / Flip every 2nd blank of 120 Hz. **Never free-run** (86Box stalls 0.35-1 s).
   Leaves ≈ 13 ms CPU per frame for simulation and logic.
7. **Picking:** screen → tile via a 64x32 (and 32x16) lookup mask, 0.08 µs per pick.

### 11.4 Overlays

- Overlays are **baked into the chunk cache** (pool flush on overlay change).
- Technique: the ground under the overlay is drawn with a **50% checker-dither diamond** in one of 8 colour levels on
  top of the normal tile (one tiny keyed diamond per level per zoom; looks nearly as good as pre-tinted tiles and costs
  almost no VRAM). Pre-tinted ground variants are an option later if VRAM allows. No ColorFill shapes (blocky).
- **Overlay mode shows buildings flat**: lots are drawn as their ground footprint (a `lot_ground` sprite the art
  generator emits per building = the lot's ground pass without solids), so the overlay colour is visible; Tab toggles
  "buildings shown" (dither over everything). Network overlays (power, water) colour road/line tiles and lot
  footprints (connected / not connected / brownout).

### 11.5 Main loop

```
while running:
    pump messages, read input (mouse, keys)
    tools: update drag preview (cheap; the expensive part is precomputed by the simulation)
    simulation: run whole steps while sim_budget_ms remains and steps are due (speed-dependent)
    render: world cache repair (dirty + scroll), cache blit, dynamic sprites, overlays, UI
    present (flip/blt), wait for the next blank / frame deadline
```
`prof_begin/prof_end` sections: SIM, TOOLS, CACHE, WORLD, SPRITES, UI, PRESENT; `-bench N` writes `bench.txt`.

### 11.6 Source modules (`src/`)

| File | Contents |
|---|---|
| `common.h` | types, constants (MAP_W, TILE_W...), macros, VC6 notes |
| `plat.*`, `gfx.*`, `snd.*`, `config.*`, `util.*` | engine base (from SIROCCO) |
| `main.cpp` | WinMain, loop, command line, game states (title, new game, play) |
| `art.*` | sheet loading, sprite tables from generated headers, zoom sets |
| `world.*` | Tile/Lot arrays, terrain, lot placement/removal, queries |
| `mapgen.*` | procedural terrain |
| `iso.*` | projection, screen↔tile picking, camera |
| `render.*` | world cache, dirty rects, strip sorting, overlays, cars/smoke drawing |
| `sim.*` | scheduler (phases), growth, demand, migration |
| `net.*` | power/water/heat networks, service coverage BFS |
| `fields.*` | pollution, land value, fire risk, crime, happiness |
| `traffic.*` | commute distance (MVP), road graph assignment (v1.0), cosmetic cars |
| `budget.*` | ledger, taxes, loans |
| `politics.*` | blocs, authority, decrees, letters, events, elections, clearing trade |
| `tools.*` | build tools, drag preview, validation, costs, undo/redo |
| `ui.*` | frame, tool panel, fly-outs, panels, tooltips, ticker, minimap, newspaper |
| `text.*` | string tables (English), name generators, number formatting |
| `save.*` | save/load |
| `defs.*` | data tables: BuildingDef, road classes, services, decrees, events, eras |

### 11.7 Data-driven definitions

`BuildingDef { id, name, zone, tier, size_w, size_h, era_year, status_req, cost, upkeep, capacity, power_use,
water_use, pollution, jobs_or_residents, sprite_base[rot], variants }`, in `defs.cpp` as static tables (see `src/defs.h`).
Display names live in the defs tables (so the sim doesn't depend on text ids); `text.cpp` holds sentences and formatting. No external scripting.

### 11.8 VC6 and code rules

Same as SIROCCO DESIGN 4.12: pre-standard C++ (C-style mostly), `for` scope leak, no `<cstdint>`, declare at block
top, `/W3` clean, Win98 APIs only, optional DLLs loaded at run time. Fixed-size arrays; no allocation per frame.

---

## 12. Roadmap

### 12.1 Milestone 1: vertical slice ("engine and art first")

Prove engine quality, art quality, and the core loop:
- Engine base, fullscreen/windowed, display-locked 60 fps, smooth scrolling at both zooms, minimap.
- Procedural lakeland map; world cache renderer with strip sorting and dirty updates; cosmetic cars and water shimmer.
- Roads (gravel, street) with autotiling and drag preview; bulldoze; undo.
- Zones: houses, apartments T1, commercial, industry, with growth/tiers 1-2 and construction stages.
- Power (coal plant + lines) and water (pump, mains in roads); fire station, school, clinic, public sauna.
- Demand, budget (2 taxes), pollution, land value; overlays for power, water, pollution, land value, services.
- UI frame (top bar, teak panel, fly-outs, ticker, query with checklist), font, save/load, sound basics.
- `-bench`: 60 fps (no frame over 20 ms while scrolling) with a dense 20k-population city at both zooms at
  800x600, and simulation speed 3 keeps up.

### 12.2 Milestone 2: v1.0 ("Presidentti")

Element factory + Apartments T2-T3; tiers 3; traffic assignment with congestion; main roads/motorway; buses; railway
station; police, library, post & telephone, health centre; district heating and sewage; fire risk + fires; politics
(blocs, authority, decrees, letters, events, elections, clearing trade, Eastern/Western meters); eras and town status;
loans; newspaper; graphs; landmark buildings; all rotations and variants; polish, balance, performance pass.

### 12.3 Stretch

Winter season pass; trams/metro debate; rivitalo/agriculture/forestry/summer-cottage zones; water pollution;
ageing housing and renovation; larger maps; scenario-like challenge seeds; music; real-hardware tuning on the X41/T40.

---

## 13. Open questions

- Final game title (working title KAUPUNKI). Candidates to propose to the user later.
- Real-hardware check of the chunk-cache renderer on the X41/T40 later (emulator blitter costs differ).
- Whether 3x1 *lamellitalo* lots complicate placement enough to restrict apartments to 2x2/3x3 (art has 3x1).

---

## 14. Rev 2 (2026-11-03): changes after play-test 1 — binding for Milestone 2

Sources: `docs/PLAYTEST.md` (feedback + decisions), `docs/research/WATER.md`, `docs/research/INDUSTRY.md`,
`docs/BALANCE.md`, `docs/PERF.md`. Where this section conflicts with earlier sections, **this section wins**.

### 14.1 Water: SimCity-style pipes (replaces 4.3 "mains inside roads" and 5.6 water)
Full spec: `docs/research/WATER.md` sections 2-3. Summary: a **pipe tool** (drag like power lines; 3 mk/tile, 15 under
water; runs under anything; unlimited flow); **buildings conduct** water, roads block unless a pipe crosses; zoned
empty tiles conduct (14.2). Each **source** (lakeside pumping station: needs power; well; water tower = 200-unit
source + 16-pipe-tile pressure zone required by Apartments T2+) serves its nearest consumers along the network until
its output is used: that is its **reach**; spare output from other sources on the network flows on; **the furthest go
dry first**. Optional INI/New Game setting "lay pipes with new roads" (default off). Old saves: pipes laid under
existing roads on load. **Visibility:** water overlay (wet/short/dry pipes, watered/dry lots, where new lots would get
water), source hover highlights its area, source query "440 / 440 used, 34 buildings, reach 18 tiles, 9 dry beyond",
top bar/tooltip "Water 3,400 / 4,000 (85%)". Droughts later (seasons).

### 14.2 Conduction and industry gap fix
- **Zoned-but-empty tiles conduct power and water** (SimCity rule).
- **1x1 industrial "yard" lots** (timber stack, steel stock, shed) grow only where no 2x2 fits.

### 14.3 Industry, forestry, farms, complexes (Milestone 2; spec `docs/research/INDUSTRY.md`)
- **Industry kinds** chosen automatically per new lot by site and kind demand: sawmill (forest/lake), engineering
  (fallback), textile, dairy (milk supply), electronics (1965+). Query explains the choice. Wood exports earn
  Western trade; metal/textiles fill the Eastern clearing quota.
- **Forestry zone** (rotation cutting, 48-month regrowth, wood supply as one number + limited imports) and **farm zone**
  (3 tiers; mechanised tier sheds residents; milk for dairies; Rural bloc).
- **Unique complexes**: **paper mill** 5x5 (5,000 mk, 300 → 900 jobs in stages, on rapids/lake), with a ***patruuna***
  (owner) whose mood yields **gifts the player places** (row houses, sauna, church, school) and **demand event cards**;
  company-town rule (≥ 40% of jobs → company pays 20% of school/clinic/fire upkeep); telegraphed risk events (paper
  slump, strike, 1970s modernisation layoffs, 1980s merger). ***Valtion Metalli*** (state metal works): placed by
  presidential decree once politics exists (simple unlock at Market town before), tied to the 5-year clearing
  agreements, output halves when clearing trade ends (1990). Company-town New Game start: later.
- Fictional names only (INDUSTRY.md, THEME 8.6).

### 14.4 UI/UX fixes from play-test 1
- **Zone visibility toggle** that persists across tools (zones visible as tint/outline whatever is selected).
- Tool switching: selecting any tool deselects the previous one (bug: Gravel road → Industry kept the road tool).
- **Mac-friendly zoom**: `+`/`-` (and numpad), `Z` toggles zoom, on-screen zoom buttons; mouse wheel kept; PgUp/PgDn kept.
- **Discoverability**: opening ticker hints ("Right-click any building to see what it needs"), hover label on lots
  (incl. "Town hall"), a short first-minutes hint chain.
- Budget panel shows the **upkeep index**; car count floor scales with car ownership; windowed pause menu works
  within VRAM.

### 14.5 VRAM plan (16 MB is the minimum; user decision)
- **Only the active zoom's world art is resident at every resolution** (T13 made the swap 91/141 ms and keeps pixels in
  system memory). At 800x600 that leaves ≈ 2.5-2.7 MB of new 64-zoom art headroom for Milestone 2.
- Budget check in the art pipeline per zoom (64-zoom sheets ≤ ~9.6 MB at 800x600 incl. existing 7.2 MB; report).
- Drop abandoned sprites for industry kinds (use a tinted generic one) if needed.

### 14.6 Milestone 2 scope (unchanged from 12.2 except as noted)
Element factory + Apartments T2-T3; tier 3 for commercial/industry; traffic assignment with congestion; main roads/
motorway; buses; railway station; police/crime, library, post & telephone (phone queue, tier-3 houses again), health
centre; district heating + sewage; fire risk → fires; **politics** (blocs, authority, decrees, letters, events,
elections with margin, Eastern/Western meters, clearing trade + 1973 oil shock); eras + town status by decree; loans;
newspaper; graphs; landmarks/money sinks; **plus 14.1-14.4**. The game stays a sandbox (no campaign).
