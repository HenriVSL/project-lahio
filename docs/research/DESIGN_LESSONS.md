# KAUPUNKI: design lessons from 35 years of city builders

Research input for the Kaupunki game design document. Kaupunki is a grid city builder for Windows 98
(Pentium II 233, 128 MB, Voodoo3, DirectDraw 2D, VC6), set in post-war Finland from about 1955 onward,
with the player as a satirical president. Isometric pixel art.

This document is opinionated on purpose. Every section ends with a "for Kaupunki" verdict. The
sources are at the end (section 8). Where a claim comes from community reverse engineering or forum
reports, not developer documentation, it says so.

Ground rule: **learn from the genre, copy nothing.** No names, UI layouts, formulas, building lists or
text from any specific game. The formulas in section 4 are our own and are meant to be tuned.

---

## 0. Summary in one page

- **Map:** 128x128 tiles by default (16,384 tiles, the size of SimCity 2000). Keep the code sized by
  constants so 192/256 can be tried later. Use flat land with lakes, rivers, islands and forest. Finland
  is flat, which saves us thousands of slope sprites.
- **Simulation:** aggregate, not agent-based. Store 8 bytes per tile, with coarse 2x2 and 4x4 "field
  maps" for pollution, land value, crime and so on. Spread the work over 16 phases, as the 1989 code
  did. Cars and people on screen are **cosmetic sprites driven by the aggregate numbers**.
- **Traffic:** a road graph (intersections = nodes) with periodic shortest-path trip assignment from
  a few dozen origin clusters. Congestion feeds back into costs over several months. It is
  deterministic, explainable and cheap (about 2 ms per frame, amortised).
- **Services:** distance over the road network plus capacity, computed with multi-source BFS. No random
  walkers and no flat circles.
- **Utilities:** power through pylons, which conduct between adjacent buildings (fun, visible, few
  decisions). Water mains are part of every road (no duplicate pipe layer: pipe-laying is a chore).
- **Demand:** transparent. Every R/C/I bar has a tooltip listing the + and - terms in plain numbers.
- **Theme as mechanics:** rural-to-city migration supplies residents. Emigration to Sweden is the
  failure valve. A concrete-element factory caps the growth of apartment blocks. Wooden districts
  burn. The bilateral clearing trade with the Eastern neighbour drives industrial demand and oil.
- **Politics:** blocs whose approval is **computed from the city**, an authority-point budget spent on
  decrees that **change simulation parameters**, rare event cards, and 6-year terms with satirical
  ways out ("emergency law extends the term").
- **Pacing:** era plus milestone unlocks (1955 wooden town, 1960s concrete suburbs, 1973 oil shock,
  1980s renewal). Term goals give structure. A sandbox comes without them.
- **UX:** an 800x600 primary layout that still fits 640x480. Top status strip, left tool column with
  flyouts, bottom ticker, toggleable 128x128 minimap (1 px per tile). Overlays on number keys.
  Unlimited undo within the current month. Drag tools everywhere. **Preview consequences before the
  click.**
- **Rendering:** cache the static world layer in a video-memory surface and redraw only dirty tiles
  and scroll strips. Per frame: 1 cache blit, then about 100-300 sprite blits, then the UI. Blit count
  is our cost driver (about 4-8 µs of CPU per DirectDraw call, measured in this setup).

---

## 1. The classic grid builders: how they really worked

### 1.1 SimCity (1989) / Micropolis: the reference implementation

The original SimCity code was released as open source as **Micropolis**, so we can read it rather than
guess. Chaim Gingold's 2016 *SimCity Reverse Diagrams* and his book *Building SimCity* (MIT Press,
2024) document it in depth.

**Data layout**
- World 120x100 tiles. Each tile is one 16-bit word: 10 bits of tile id plus flag bits (powered,
  conductive, burnable, bulldozable, animated, zone centre).
- Coarser **field maps** at lower resolution:
  - traffic density, pollution, land value, crime and population density at **2x2 tiles per cell**
    (60x50);
  - terrain density at 4x4;
  - police and fire station influence and the "rate of growth" map at **8x8** (`FireRate[x>>3][y>>3]`,
    `RateOGMem`).
- A 3x3 zone is one "zone centre" tile with a population level encoded in which tile graphic it shows.
  **The graphic is the state.** That trick saved memory in 1989; we should not copy it, but it shows
  how little per-tile state a city builder needs.

**Time slicing** (`s_sim.c`)
- `Simulate(Fcycle & 15)` runs **one of 16 phases** per simulation frame:
  - phase 0: date, tax accumulation and the RCI "valves" on every other cycle;
  - phases 1-8: scan one eighth of the map's columns each (zone growth, traffic generation);
  - phase 9: census and taxes;
  - phase 10: decay the traffic map and send messages;
  - phases 11-14: the power, pollution/terrain/land-value, crime and population-density scans;
  - phase 15: fire analysis and disasters.
- Expensive scans run **less often at higher speeds**. At speed 3 the crime scan runs every 18 cycles
  instead of every cycle, so fast mode stays cheap. This is the pattern for us: **a fixed phase
  schedule, with slow fields refreshed on a divisor.**

**RCI demand ("valves")**: a small system-dynamics model.
- `Employment = (commercial + industrial jobs) / (residents/8)` and
  `Migration = residents * (Employment - 1)`.
- Projected commercial demand comes from an "internal market" `(R+C+I)/3.7` times a labour-base ratio.
  Industrial demand is "external": it grows with existing industry times a difficulty factor.
- Each ratio (projected / current, capped at 2) becomes `(ratio-1)*600 + TaxTable[tax+level]` and is
  added to a valve clamped to ±2000 (R) or ±1500 (C, I).
- Caps: without a stadium, port or airport, the matching positive valve is forced to 0. This is the
  famous "your residents demand a stadium" gate. It works as a milestone, but players found it
  arbitrary because the game never explains it.

**Traffic** (`s_traf.c`) is a **random walk, not a route**.
- A zone looks for a road on its 12-tile perimeter, then "drives" up to `MAXDIS = 30` steps. At each
  junction it picks a random direction (not the one it came from), pushes every other position on a
  stack and checks whether a destination zone of the required type borders the road.
- On success it adds **+50 to the 2x2 traffic cell of each visited road tile**. On failure the zone
  records "no route", which stunts its growth.
- Traffic is a decaying heat map of random walks. It is cheap (at most 30 steps per attempt) but not
  causal: cars do not go where people need to go.

**Aged badly**
- Traffic that doesn't follow cause and effect.
- Opaque demand. The bars move and nobody knows why.
- Hidden gates such as the stadium cap.
- Graphic-encoded state, which means "why is this zone not growing?" can't be answered in the UI.
- The tiny 120x100 map.

### 1.2 SimCity 2000 (1993)

From the community's file-format reverse engineering (OpenCity2k/SC2k-docs):
- 128x128 map with a per-tile layer per byte:
  - altitude: 2 bytes;
  - terrain, building, zone, underground (pipes/subway), text label and **bit flags**: 1 byte each.
    The flags are powerable, powered, pipeable, watered, water, salt water and rotate.
- Field maps: traffic, pollution, land value and crime at **64x64 (2x2)**; police, fire, population
  and growth rate at **32x32 (4x4)**.
- About 12 bytes per tile plus about 22 KB of field maps, so a whole city fits in about 220 KB. It ran
  on 486-class machines. The Windows 3.1 version listed a 486 with 8 MB.

Simulation notes (community-measured, Special Edition):
- Power plants have a capacity in **tiles powered** (coal 704 tiles, nuclear 1,776 and so on).
  Utilisation above about 98% asks for more.
- Water is counted in tiles served. Water towers buffer surplus.
- Tax revenue was `rate * population / 75`. Land value did **not** affect taxes, despite the manual.
- Ordinances are ±1% demand nudges.
- Traffic remains density-driven rather than routed. A 2013 experiment that rebuilt a real town in
  both SC2000 and SimCity (2013) found SC2000 put traffic "everywhere", including quiet residential
  streets, and produced **zero traffic on a single road leading to a new town**: no through-traffic
  exists.

What SC2000 got right and we keep:
- Isometric readability, with buildings that grow taller as density rises.
- Utilities shown as their own visible network.
- Ordinances as a policy layer.
- The newspaper as flavour.
- Water/power capacity in "tiles served", an easy unit.

What aged badly:
- **The underground pipe layer.** A second road network you draw blind in another view is the
  canonical chore.
- Terrain slopes made road and zone art explode combinatorially.
- Advisors who only pop up to complain. SimCity 3000 was praised for giving them names and real
  analysis.
- Disasters that wreck a city with no warning.

### 1.3 SimCity 3000 (1999) and SimCity 4 (2003)

SimCity 3000 had a minimum spec of Pentium 166 / 32 MB, so our VM is above its floor.
- Map sizes 64/128/256.
- **Garbage** as a new service.
- Neighbour deals: sell power, water or garbage capacity to neighbours. That is trade as a mechanic,
  and a direct ancestor of our clearing-trade idea.
- **Seven named advisors** plus **petitioners**: citizens asking for an ordinance or a tax change,
  which you may ignore.
- A **news ticker**, which became the template for Cities: Skylines' Chirper.

SimCity 4:
- **Regions** of connected city tiles with shared demand.
- Wealth tiers (low/medium/high wealth R, C and I).
- **Commute-time-based traffic routing.** Players observed it as shortest-route rather than
  congestion-aware: drivers pick a jammed street over an empty avenue two blocks away.
- Much more data per building, a deep set of query panels, and the "mayor mode vs god mode" split.

Aged badly:
- SC4's opaque wealth and demand interplay, so you need a wiki to know why high-wealth R won't grow.
- Commute routing that ignored congestion.
- Load times and the cost of the region model.
- Too many near-identical query panels.

### 1.4 Caesar III (1998) / Pharaoh (1999): walkers and housing evolution

Impressions' model:
- Service buildings emit **walkers**. Random walkers wander a limited distance and serve every house
  they pass. Destination walkers carry goods from building to building.
- Houses **evolve** (tent, shack, ... villa) when their needs are met. The needs are goods, services
  and **local desirability**, computed from nearby buildings: temples help, reservoirs hurt.

Design analysis (Game Wisdom, "Of Walkers and Men"):
- Walkers make the city readable. You *see* the theatre actor pass a house.
- One walker serves many houses, which suits large populations.
- But random walkers make coverage unreliable. A house next to a temple can go unserved, so experts
  build long loops with roadblocks, which is a constraint, not a choice.
- "Smarter" walkers look *worse*, because now they seem to choose wrong.

Keep: **housing evolution through met needs** (the core satisfaction of the genre) and **visible
figures that reflect real state.**

Drop: random-walk coverage. Coverage must be deterministic and shown on an overlay.

### 1.5 Lincity (1995, open source) / Lincity-NG

- Goods move physically along tracks, roads and rail from markets to houses.
- Power runs along lines.
- Resources are finite.
- **Win by sustainability** (renewables and recycling) **or by evacuating the population by
  spaceship.**

Lessons:
- A city builder can have an *ending* that is a thematic statement.
- Physically moving goods on a tile grid is cheap when done as "market range" rather than agents.

### 1.6 OpenTTD (transport)

The best reference for networks on a grid.
- **YAPF pathfinder:** instead of one node per tile, it extends a node along track until the next
  decision point (junction, station, signal) and calls that a **segment**. It **caches segment
  costs** and invalidates the cache with a global "track changed" counter. Road vehicles cache only
  about 8 segments of their route.
- The same idea serves us: **path search over intersections, not tiles.** On a 128² city with about
  3,000 road tiles there are only a few hundred junctions.
- OpenTTD shows that **building a network and watching it flow** is a complete game on its own. The
  joy is in the network, not in the number of agents.
- Its lockstep multiplayer depends on a fully deterministic, integer simulation. We should be
  deterministic too (save/replay bug reproduction and fair scenario scoring).

### 1.7 How they fit on 386/486/Pentium machines: the techniques

1. **A byte or a word per tile per layer**, with fixed-size static arrays. No per-object heap
   allocation.
2. **Coarse field maps** (2x2, 4x4, 8x8) for anything that diffuses or is "area-ish". Smoothing a
   64x64 byte map is about 4k cells: trivial.
3. **Phase schedules:** 1/16 of the work per sim frame, and slow fields on divisors.
4. **Statistical populations.** A zone holds a number, not people.
5. **Cosmetic animation** decoupled from the simulation: cars, smoke, the train sprite.
6. **Graphic-encoded state** (Micropolis): don't. We have 128 MB.
7. **Dirty-rectangle rendering.** Only redraw what changed, because most of a city builder's screen
   is static most of the time.

---

## 2. Modern lessons

### 2.1 Cities: Skylines (2015, Colossal Order, Finnish)

What made it feel modern:
- **Zoning along roads.** Zone cells exist only along road edges, a few cells deep, so you can never
  zone unreachable land.
- **Free-form roads with upgrade-in-place.** Click an existing road with the bigger road tool and it
  is replaced in place, without demolishing.
- **Districts plus policies**, painted with a brush: per-area rules such as a smoke-detector
  subsidy, industry specialisation or a heavy-traffic ban.
- **Milestones by population** that unlock the toolset gradually:
  - start: roads, power, water, basic R/C/I;
  - about 500 people: taxes, loans, education, garbage, health;
  - about 1,000: districts, policies, police and fire;
  - and so on.

  This *teaches* the game and gives early goals.
- **Info views:** one keypress shows any layer as a heat map. This is the genre's single most useful
  UI invention.
- Water, sewage and power as explicit networks with capacity.
- Large scale, with tens of thousands of individually simulated citizens and vehicles.

Player complaints:
- **Traffic AI.** Vehicles pick lanes late, jam at one lane while others stay empty, and
  routing/destination choice was proximity-based (Colossal Order's own words in their CS2 diary).
  Despawning of stuck vehicles hides the real bottleneck. The most-subscribed mod, Traffic Manager:
  President Edition, exists to fix lane choices by hand. Lesson: **if you show individual cars, players
  will judge their intelligence.**
- **Chirper** (the bird feed). Charming at first, then nagging. Turning off its auto-open is a classic
  first settings change and the topic of many threads.
- **Death waves.** A cohort of residents that moved in at the same time gets old and dies at the same
  time, so hearses can't keep up and buildings go abandoned.
- **Late-game sameness.** Once a city is self-sustaining, money stops mattering and the game becomes
  a painting tool.
- **Pipes duplicate roads.** Mods and CS2 both moved toward utilities inside roads.

### 2.2 Cities: Skylines II (2023)

- Pathfinding is now cost-based on the real network. The **cost has four aspects: time, comfort, money
  and behaviour**, weighted by age group. Vehicles use more lanes. Parking is part of route choice.
  Utilities can run inside roads.
- The deeper agent "lifepath" economy, rent and companies **launched with severe performance
  problems** and an economy that players could not understand or trust (mass deaths, wrong rent,
  unprofitable companies). It needed a large "Economy 2.0" rework in mid-2024. Economy 2.0 also made
  the Taxes and Services panels unlock at milestone 1: **show players the controls early.**
- The lesson for us is sharp: **simulation depth that the player cannot see or influence is pure
  cost.** Every system must be visible on an overlay or a tooltip, or it should not exist.

### 2.3 SimCity (2013): the cautionary tale

- The GlassBox engine simulated **agents** (sims, cars, power and water as moving units) on tiny city
  plots (about 2x2 km).
- Agents went to the **nearest** available destination. Sims had no fixed home or job, and fire trucks
  and sewage flowed to the closest target even when it was over capacity. This produced visibly silly
  behaviour and traffic that no road layout could fix.
- Creative director Ocean Quigley later said the always-online tech "got ahead of itself".
- Lesson: an **agent model at a scale you can't afford** forces small maps and dumb agents. Aggregate
  is better.

### 2.4 Workers & Resources: Soviet Republic

The closest theme to ours: Eastern Bloc planning, prefabricated concrete, imports and exports in two
currencies (roubles and dollars).
- In **realistic mode** every road needs workers, vehicles and materials, and every resource is
  mined, moved and processed.
- Critics call it "fussy and demanding but utterly singular". It has about 90% positive Steam reviews
  but a reputation for an extremely cluttered UI and a very steep start.
- Lessons:
  1. Two currencies (Western vs Eastern trade) are thematically gold.
  2. **Construction material as a growth cap** is a great socialist-planning mechanic.
  3. Full logistics is a different game. **Abstract logistics into capacities**, not trucks.

### 2.5 Tropico (1, 2001 onward, the 3-6 series): dictator satire

- **Factions** (communists, capitalists, religious, intellectuals, environmentalists, militarists,
  nationalists, loyalists in Tropico 4). Every citizen sympathises with one or more.
- **Edicts** (some cost money or need buildings) that mostly lean toward one side.
- **Elections** you can win honestly, rig, or abolish. Losing an election or suffering a coup ends
  your rule.
- **Superpower relations** (US vs USSR): aid, threats, invasion.
- Humour comes through a radio DJ and the almanac.

Lessons:
1. A political layer works when **factions read the same city you are building**: a factory pleases
   capitalists and angers environmentalists automatically.
2. **Satire lives in the text and the choices**, not in the numbers. The numbers must be a serious,
   fair system.
3. Elections are a natural term structure with dramatic stakes.

Complaint: later entries feel same-y, and edicts become checklists once you know the dominant set.
Our fix: decree slots, conflicting blocs, and era changes that make yesterday's best decree today's
problem.

### 2.6 Frostpunk (2018): laws as moral pressure

- The **Book of Laws** has two mutually exclusive trees (Adaptation, then Purpose). Each law shows its
  effects on **Hope** and **Discontent**: two meters, deliberately not opposites. A **cooldown**
  between laws means each one is a decision, not a menu click.
- Extreme laws slide toward authoritarian rule, and the ending judges what you did.
- Critics note the laws are transparent, so optimisation is easy, but "mindless optimisation isn't
  without consequence": the cost is narrative and emotional.
- Lessons:
  - **Laws with cooldowns and explicit trade-offs.**
  - Two separate approval meters instead of one "happiness".
  - An **end-of-term judgement** that summarises your choices in a story.
  - A gentle slope toward authoritarianism is *exactly* our satire: the satirical president who
    "temporarily" extends his term.

### 2.7 Banished (2014)

- Every citizen is simulated, with ageing (deliberately about 3 times faster than real life so the
  early game isn't dull).
- Fails through **death spirals**: overexpand, food runs out, workers die, less food, and so on.
- The **late game loses tension** once the town is self-sufficient.
- Players asked for an **age chart** to predict death waves.
- Lessons:
  - If cohorts exist, show their distribution.
  - **Make spirals recoverable** (floors, grace periods, warnings early).
  - Introduce new pressures late.

### 2.8 Anno (1404, 1800, 117)

- **Needs tiers:** each population tier needs more goods and services before it upgrades, and the
  upgraded tier unlocks new buildings.
- The game is a production-chain puzzle with gorgeous, readable isometric buildings.
- Lesson: **housing upgrade tiers are the motivation engine.** Players want to see the wooden house
  turn into a better house. Tie unlocks to *tier counts* ("200 people living in level-3 housing"), not
  just raw population.

### 2.9 Against the Storm (2023): curing late-game boredom structurally

Designer Michał Ogłoziński:
- City builders aim at stability, so designers must either "poke the player out of this balanced
  state or let them get bored".
- Their answer: **short runs** with seasonal disruptions, the city as "your avatar", and a fictional
  Queen whose impatience is two crude bars made engaging by fiction.
- Lessons:
  - **Fiction can make a timer lovable.** Our version is the election clock and the Eastern
    neighbour's patience.
  - Disruptions (seasons, eras, crises) are the late-game fuel.
  - Scenario-length play (one term, 6 game years) is a good unit of play.

### 2.10 Islanders, Townscaper, Dorfromantik: low friction

- **Islanders:**
  - No money. Placing buildings gives points from neighbours, and the **points preview is shown before
    you place**.
  - Point thresholds unlock new building packs. Running out of buildings ends the island.
  - A seven-month student project at HTW Berlin.
- **Townscaper:** no goals at all. Every click makes something pretty, and there is instant undo.
- **Dorfromantik:** tile matching, tiny "quests" on tiles, gentle scoring.

Lessons for a serious builder:
1. **Preview the consequence before the click** (+/- numbers, footprint of effects).
2. Every action should give immediate audiovisual feedback.
3. Small local quests ("this village wants a shop next to it") are better pacing than abstract
   targets.
4. Undo removes fear.
5. Cheap, quick decisions keep players in flow.

### 2.11 Cross-cutting player complaints and our countermeasures

| Complaint (genre-wide) | Seen in | Kaupunki countermeasure |
|---|---|---|
| Traffic AI looks stupid | CS1, CS2, SC2013, SC4 | No individually-reasoned cars; aggregate assignment with congestion feedback; cosmetic cars follow flows; traffic overlay explains *why* (origin/destination share) |
| "Why won't this zone grow?" | all | Query tool shows a checklist: road, power, water, demand, land value, pollution, element supply; first failing item in red |
| Opaque demand | SC1-SC4, CS2 | Demand tooltip lists terms in numbers ("+340 open jobs, +90 rural migrants, -120 no water") |
| Micromanagement/busywork | W&R realistic, SC2000 pipes, CS pipes | Water mains in roads; logistics as capacities; area policies via district brush |
| Nagging notifications | CS Chirper, SC2000 advisors | Rate-limited ticker (at most 1 message per in-game month per topic), severity colours, everything clickable to jump |
| Late-game sameness | CS1, Banished, Tropico | Eras with new problems; ageing housing stock; election terms with goals; oil shock; regional rivals |
| Death spirals | Banished, SC (abandonment) | Smoothed migration, soft floors, early warnings, emergency loans; abandonment is slow and reversible |
| Death waves | CS1, CS2 | No individual ages; population cohorts are smoothed aggregates |
| Disasters as pure punishment | SC series | Disasters are themed, telegraphed, mitigable (fire stations, flood dikes) and toggleable |
| Fear of mistakes | most | Unlimited undo within the month, full refund; bulldoze refund for tiles placed this month |
| Hidden unlock gates | SC1 stadium cap | Every gate is named in the UI ("Needs: Element factory") |
| Simulation you can't see | CS2 | Rule: every simulated quantity has an overlay or tooltip, or it is cut |

---

## 3. Feasibility on our hardware

### 3.1 What we have (measured in this very setup)

From `dev/sandline/probe/RESULTS.md` and `dev/runner/PERF.md`:
- **DirectDraw hardware blits:** about 330 fps windowed for a full 640x480 tile scene of about 350
  blits. The CPU cost is about **2.3-3 ms per frame for about 350 blits**, so roughly **4-8 µs of CPU
  per Blt/BltFast call**. 16x16 tiles (4 times the blits) halved fps, so **the blit count is the cost
  driver.**
- Video memory to video memory is fast. **Locking VRAM is about 9 times slower.** System memory to
  screen copies run at about 50 MB/s, about 10-12 ms for a full 640x480x16 frame.
- The emulated refresh is 120 Hz. Cap the frame rate, because free-running stalls the 86Box blitter.
  QueryPerformanceCounter is fine; Sleep(1) works at about 1 ms.
- At 30 fps with hardware blits, about 90% of the CPU (about 30 ms per frame) is left for logic.
- Voodoo3: 16 MB VRAM, and **source surfaces at most 640 px wide**.

The Pentium II 233 has a 16 KB L1 data cache and 512 KB of L2 cache at half speed. Assume roughly
**100-200 M simple integer operations per second** when the data stays in cache, and much less when it
misses. Keep the hot simulation arrays under about 512 KB.

### 3.2 Frame budget (target)

| Item | Budget per frame @ 60 fps (16.7 ms) |
|---|---|
| Present world cache + scroll strip repair | 0.5-2 ms |
| Animated sprites (cars, smoke, cursor, fire) 100-300 blits | 1-2.5 ms |
| UI (status bar, tool column, ticker, tooltips; all pre-rendered) | 0.5-1 ms |
| Input, tools, preview computation | 0.5 ms |
| **Simulation time slice** | **up to 6-8 ms** (adaptive) |
| Slack (sound thread, Windows, emulator jitter) | 3+ ms |

- Run display-locked at **60 fps (every 2nd blank of 120 Hz)**. When nothing scrolls the frame is
  nearly free.
- The simulation runs in **time slices inside the main loop**: a single thread, deterministic, with
  no locking.
- If a fast game speed can't keep up, **the game slows down rather than skipping simulation**: no
  catch-up spiral.
- Sound runs on a waveOut worker thread, per the house rules.

### 3.3 Map size

| Size | Tiles | Per-tile data (8 B) | Full-scan cost* | Verdict |
|---|---|---|---|---|
| 64x64 | 4,096 | 32 KB | ~2 ms | Too small; fine for tutorial |
| **128x128** | **16,384** | **128 KB** | **~7 ms** (spread over 16 phases: <0.5 ms/frame) | **Default.** SC2000 size; fits L2 with field maps |
| 192x192 | 36,864 | 288 KB | ~16 ms | Plausible "large" option |
| 256x256 | 65,536 | 512 KB | ~28 ms | Stretch; per-tile layer alone fills L2; traffic graph 4x |

\*At about 100 cycles per tile for a growth/effects scan, 233 MHz.

Visual scale:
- With 32x16 isometric tiles, a 128² map is a 4,096x2,048 px diamond, about 6x4 screens at 640x480.
  That is plenty of room for 50,000-150,000 residents.
- For reference, Tampere had about 130,000 people in the mid-1960s. A "big Finnish city" is the right
  ceiling.

### 3.4 Population and agents

- **Population is statistical.** Each zoned lot stores residents (or jobs) as a number. A 1x1 wooden
  house holds 4-8 people, a 3x3 concrete block 150-250.
- **No individual citizens or cars in the simulation.** Individual simulation would cost 50k agents x
  pathfinding. SC2013 shows where that leads.
- Cosmetic agents:
  - up to about 150 car sprites, spawned on road edges with a probability proportional to the edge's
    assigned flow, moving tile to tile along the road. They don't route; they turn randomly while
    biased toward the edge's flow direction;
  - plus pedestrians near shops, smoke from chimneys, a few trains and buses.
  - Cost: about 150 x (move + 1 blit) ≈ 1 ms.
- Optional "named citizen" flavour: sample a random household from a district's statistics for the
  newspaper ("The Virtanen family of Kivikko complains: no school within 2 km"). It feels personal,
  costs nothing and is always consistent with the real numbers.

### 3.5 Pathfinding and traffic: what is affordable

**Per-trip A\*: no.** 50,000 commuters x A* over 3,000 road tiles is billions of operations.

**Recommended: aggregate assignment on a road graph.**
- **Graph:**
  - nodes are intersections, dead ends, bends and zone-access points (merged runs); edges are
    straight runs between them, like YAPF segments;
  - 128² with about 3,000 road tiles gives about 400-1,200 nodes;
  - per edge: length, capacity (from road class), free-flow speed, current flow and cost;
  - a `uint16 edgeOfTile[128*128]` maps tiles to edges (32 KB).
- **Origins and destinations:**
  - aggregate the map into **8x8-tile blocks**, 256 blocks on 128²;
  - each block with residents is an origin with W workers; each block with jobs is a destination with
    J jobs (plus shoppers to commercial blocks);
  - each block attaches to its nearest graph node.
- **Assignment:**
  - Every "traffic cycle" (each in-game month), run **Dijkstra from each destination cluster** (merge
    blocks into at most K = 32 clusters; job areas are lumpy) over the graph with costs
    `t = len/speed * (1 + 0.15*(flow/cap)^4)` (the classic BPR curve; tune it).
  - For each origin block, split its workers over destinations with a **gravity term**
    `share ∝ J_d * exp(-β * cost_od)`.
  - Then add flows to edges by walking the shortest-path tree. Accumulating along a tree is
    **O(N) per source** if you push flow from leaves to root.
  - Smooth into the edge flows with the **method of successive averages** (new = old + (assigned −
    old)/k). Over a few months the network approaches a congestion equilibrium. Drivers *do*
    eventually use the empty avenue.
- **Cost:** K=32 x Dijkstra on about 1,000 nodes with a binary heap, about 15k heap operations x 30
  cycles ≈ 0.5M cycles ≈ **2 ms each, about 64 ms per full assignment.** Spread over 32 frames that is
  **2 ms per frame**, refreshed every 0.5 s of real time. Run it only once per game month, or when the
  network changes.
- **Outputs used by the simulation:**
  - **commute time per block**, which feeds residential happiness and land value;
  - **edge congestion** (traffic overlay, pollution, noise);
  - **access/no-access** (a lot whose block has no route to jobs does not grow);
  - trip shares per mode, once transit exists.
- **Explainability:** the query tool on a road can show "Flow 1,840/h, capacity 1,200. 62% comes from
  Kivikko blocks heading to the harbour industry." We get this from per-source tree accumulations of
  the clicked edge. Store the top 3 contributing clusters per edge.

**Service coverage: multi-source BFS** on the road tile grid (or graph) per service type, limited to a
max distance D.
- About 3,000 road tiles x ~30 cycles ≈ 0.4 ms per service type. Eight types take about 3 ms, rerun
  monthly or on network or building change.
- Result: per tile (2x2 cell), the nearest building id and the distance. A second pass sums the demand
  assigned to each building to compute **load/capacity**.

### 3.6 Memory budget

| Item | Size |
|---|---|
| Tile array 128² x 8 B | 128 KB |
| Field maps 64x64 x 1 B x ~10 (pollution, land value, crime, noise, happiness, desirability, fire risk, traffic, health, education) | 40 KB |
| Service coverage 64x64 x 2 B x 8 types | 64 KB |
| Road graph (≤4k nodes, ≤6k edges) + edgeOfTile | ~150 KB |
| Undo log (ring buffer) | 256 KB |
| History graphs (40 years x 12 months x 32 stats x 4 B) | 60 KB |
| Building definitions, strings, decree/event tables | <1 MB |
| **Simulation total** | **<2 MB** |
| Art in VRAM: primary + back buffer 800x600x16 | 1.9 MB |
| World cache surfaces (2 x (screen + 1 tile border)) | ~2.2 MB |
| Tile/building/UI/font sheets (16-bit, ≤640 px wide) | **8-10 MB available** |
| System-memory copies of art (for restore after mode switch) | ~10 MB |

Memory is not a constraint. VRAM limits the art: about 10 MB is about 5 megapixels of art, which is a
lot of 32x16 tiles (about 300 sheets of 128x128). Use the **lost-surface restore** path
(`IsLost`/`Restore` plus a reload from the system-memory copy) because alt-tab on Win98 loses VRAM.

### 3.7 Rendering an isometric city cheaply

- **Tiles:** 32x16 diamond base at 1x. Buildings can be taller than one tile, drawn upward from the
  footprint. Zoom-in uses **hardware stretched blits** (the Voodoo3 HAL supports BltStretch) of the
  cached world layer at 2x. Zoom-out (16x8) uses a pre-baked half-size art set, or the minimap style.
- **World cache:** draw the static world (terrain, roads, buildings) into an offscreen VRAM surface the
  size of the view plus a margin.
  - Per frame: **one blit** of the cache to the back buffer, then animated sprites, then the UI.
  - When the camera scrolls, ping-pong between two cache surfaces: copy the overlapping part (one
    blit), then redraw only the newly exposed strip (a row or column of tiles).
  - When the simulation changes a tile, mark a **dirty rectangle** and redraw the tiles (and the
    taller neighbours) that overlap it.
  - Result: a typical frame is about 1 + 150 + 30 blits, about 1-1.5 ms. A full redraw (about
    1,000-1,500 blits at 800x600) takes about 6-10 ms and only happens on zoom or overlay switch.
- **Draw order:** diagonals back to front (by x+y). Draw an N x N building when its **front** tile is
  reached, and **slice building sprites into tile-wide vertical strips** so taller buildings never
  overdraw neighbours wrongly. Use square footprints only (1x1, 2x2, 3x3).
- **Overlays:** pre-render translucent-looking **dithered diamond tiles** in 8 colour steps per
  overlay ramp (alpha blending is unavailable or slow). Draw them on the cache when overlay mode is
  on. Never lock VRAM.
- **Terrain:** flat (no heights). Water uses a few animated frames (3-4 palette-like tile variants
  cycled on cosmetic timers, redrawing only visible water tiles every 250 ms). Shorelines are an
  autotile set (16-47 variants).
- **Text:** the pre-rendered bitmap font (house rule). Finnish needs ä, ö, å (and Ä Ö Å). Cache static
  labels as surfaces; the numbers in the status bar are digit blits.

### 3.8 Simulation scheduling (concrete)

- **Base time:** 1 sim step = 1/16 of a cycle. **4 cycles = 1 month** (64 steps).
  - Speed 1: 3 steps/s (month ≈ 21 s, year ≈ 4.3 min).
  - Speed 2: 6 steps/s.
  - Speed 3: 16 steps/s (year ≈ 48 s).
  - Pause.
  - 1955-1990 at speed 2 is about 2.5 h; at speed 3 about 28 min.
- **Phase table** (ours, modelled on the Micropolis structure):

| Phase | Work |
|---|---|
| 0 | date, cosmetic events, ticker queue |
| 1-8 | zone growth/decline scan of 1/8 of rows (16 rows of 128) |
| 9 | utilities: power and water network solve (only if dirty, else skip) |
| 10 | pollution emit + diffuse + wind drift on 64x64 |
| 11 | land value + desirability recompute (1/4 of map per cycle) |
| 12 | crime, fire risk, health fields (1/4 of map per cycle) |
| 13 | service coverage BFS: one service type per cycle (round robin) |
| 14 | traffic assignment slice (a few Dijkstra sources per step, continuing) |
| 15 | demand model, migration, budget (on month boundary), politics, events |

- **Integer and fixed-point math only** in the simulation (16.16, or plain ints in natural units), with
  our own xorshift RNG seeded per save. This keeps the simulation deterministic, so the same save and
  inputs give the same city. That makes bug reports reproducible and scenario scoring fair. The FPU
  is fine for UI and preview.

---

## 4. Recommended design for Kaupunki

**Fiction:** Finland, 1955. Post-war reconstruction is underway, the war reparations to the East were
paid off in 1952, and people are moving from the countryside into towns. You are the President:
officially above day-to-day politics, in practice personally deciding where the new suburb goes. The
satire is gentle and affectionate: sauna diplomacy, letters from Moscow, emergency laws that happen
to extend your term, committee reports, the national obsession with getting a phone line.

> **Naming note:** a recognisable caricature of a real historical head of state is classic satire,
> but to keep the game original and avoid any likeness issues, use a fictional president (e.g. "Uljas
> Kekkola", with round glasses and a high forehead) in a fictional town, and blocs and agencies
> without real party names. Historical events can inspire fictional ones (see 4.10).

### 4.1 The core loop

1. Lay roads and zone land (wooden houses, apartments, shops, industry).
2. Provide power, water and services, so lots grow and **upgrade tiers**.
3. Balance the budget and handle demand from rural migration and export quotas.
4. Political pressure follows from what you built. Spend authority on decrees.
5. New eras unlock new tools and new problems. Survive the next election.

**Moment-to-moment fun:** drag a road, paint a zone, watch wooden houses appear within seconds (the
early game must be fast), see the + numbers float up.

**Minute-to-minute:** fix the reddest overlay.

**Hour-to-hour:** era shifts, terms, a city that visibly changes from wood to concrete.

### 4.2 Feature tiers

#### MVP (vertical slice): "Rintamakylä 1955-1962"

Goal: prove the loop, performance, art style and UI on 128², with one era and no politics beyond
a stub.

| Mechanic | Rationale | Simulation sketch |
|---|---|---|
| **Terrain** (land, lake, river, forest) | Finland is lakes and forest; flat terrain avoids slope art | tile.terrain 4 bits; forest clearing costs money and gives a one-off timber bonus |
| **Roads** (gravel road + street, drag tool, upgrade in place) | The network is the game. Two classes give an early decision (cheap/slow vs paved) | road tiles carry class; graph rebuilt incrementally on edit |
| **Zoning R-low (wooden houses), C (shops), I (sawmill/workshop)** | Minimum RCI triangle; wooden 1x1 lots grow fast for early fun | zone 2 bits + density; lots 1x1 (R), 1x1/2x2 (C), 2x2 (I) |
| **Zone along roads** | Never zone unreachable land | brush accepts tiles with road-distance ≤ 4 (preview shows rejected tiles hatched) |
| **Power** (plant + pylons, building adjacency conducts) | Visible, few decisions, fun to string across a lake | flood fill from plants over conductive tiles; capacity in "tiles served"; brownout shuts lowest-priority tiles first |
| **Water** (pumping station/water tower; mains inside roads) | No pipe chore; still a capacity decision | BFS from sources over road tiles + buildings adjacent to roads |
| **Services:** fire station, school, health clinic, public sauna (!) | Covers safety, education, health and a theme service that drives early tier upgrades | multi-source road BFS with capacity; see 4.5 |
| **Pollution** (air, 64x64, wind drift) | The first spatial trade-off | see 4.6 |
| **Land value** | Drives tier upgrades and taxes | see 4.6 |
| **Demand** (transparent) | The heart of growth | see 4.4 |
| **Budget** (monthly; 2 tax rates: residents, businesses; service upkeep) | Classic, simple | see 4.8 |
| **Housing tiers 1-3** for wooden houses | Anno/Caesar evolution motivation | see 4.3 |
| **Query tool + "why not growing" checklist** | Biggest genre UX fix | per-lot evaluation reused from growth code |
| **Overlays:** power, water, services (each), pollution, land value, demand, traffic | Info views are essential | dithered tiles on the world cache |
| **Undo** (unlimited within the month) | Removes fear | command log with inverse ops and refunds |
| **Save/load, 3 speeds + pause, minimap** | Basics | see 5 |
| **Ticker** (rate-limited) | Feedback channel | see 5.5 |
| **Cosmetic cars** on roads | Life on screen | flow-proportional spawns |

Not in the MVP: traffic congestion (the MVP uses connectivity plus distance only), politics, eras,
transit and disasters.

#### v1.0: "Presidentti 1955-1990"

| Mechanic | Rationale |
|---|---|
| **R-high (concrete apartment blocks)** gated by the **element factory** | The defining 1960s transformation; a thematic growth cap |
| **Traffic assignment with congestion** (4.7) | Road layout decisions start to matter; avenue/main road class |
| **Commercial and industrial tiers**; industry types: sawmill/paper (river, polluting), metal works (export), later electronics (clean) | Variety and an era progression |
| **Political layer** (4.10): blocs, authority, decrees, events, elections | The game's identity |
| **Clearing trade** with the Eastern neighbour + Western trade (4.11) | Thematic economy driver |
| **Eras and unlocks** 1955-1990 (4.12) | Pacing, late-game freshness |
| **Bus lines** (line tool, stops) + **railway station** (regional access) | Transit relieves roads; regional jobs/migrants |
| **Police/crime, library/culture, parks** (no cemeteries: no death simulation) | Services round-out |
| **District heating** (heat plant + mains in roads, needed for tier 3 apartments) | Finnish signature; a second capacity network without new drawing |
| **Disasters (themed, toggle):** wooden-district fire, spring flood, hard-frost winter | Telegraphed and mitigable |
| **Loans** (domestic bonds, Western bank loan, Eastern credit) | Debt with political strings |
| **Campaign:** term goals; **sandbox** with toggles | Structure + freedom |
| **Seasons** (visual winter set; winter raises heating and power demand) | Identity; cheap if art has snow variants |
| **Newspaper** front page each January (summary + satire) | End-of-year feedback |
| **District brush** + area policies | Local control without micromanagement |

#### Stretch

- **Trams/metro**, with a satirical metro-decision event chain.
- **Neighbouring municipalities:** annexation, commuter flows, a rival town.
- **Water pollution of lakes**, sewage treatment.
- **Ageing housing stock:** 1980s renovation cycles, suburb image problems.
- **Scenario editor** and more scenarios.
- **Larger maps** (192/256).
- **Height** only as unbuildable rock hills.
- **Sauna diplomacy mini-screen.**
- **Elections with campaign trail.**
- **Real-hardware tuning** for the ThinkPads.

### 4.3 Zoning and lots

**Zone types** (painted; development is automatic):

| Zone | Era | Lot sizes | Tiers (examples, our own names) | Residents / jobs per tile |
|---|---|---|---|---|
| **Pientalo** (low R: wooden houses) | 1955 | 1x1 | 1 hut/barrack → 2 veterans' house → 3 painted villa with garden | 3 / 6 / 8 people |
| **Kerrostalo** (high R: apartment blocks) | ~1960, needs element factory | 2x2, 3x3 | 1 3-storey brick block → 2 concrete element block → 3 tower block with sauna & lift | 25 / 45 / 70 per tile |
| **Liike** (commercial) | 1955 | 1x1, 2x2 | kiosk/grocer → shop row → department store | 4 / 10 / 20 jobs |
| **Teollisuus** (industry) | 1955 | 2x2, 3x3 | workshop → sawmill/factory → large works | 10 / 25 / 40 jobs |

- **Density is tied to the zone type, not a separate slider.** Wood and concrete are visibly different
  worlds, and the transition *is* the story of the 1960s. Bulldozing a wooden district to zone
  apartments is a meaningful (and political) decision: the press complains, workers cheer.
- **Growth rule** (per lot in its row slice, phase 1-8): compute
  `score = demand[zone] + landValueTerm + serviceTerm − pollutionTerm − commuteTerm`.
  - If `score > threshold` and the requirements are met (road access, power, water, plus for
    apartments element supply and later district heat): grow, by building a new lot or upgrading the
    tier.
  - If `score < −threshold` for N consecutive checks: decline, losing a tier or occupants. Below 0
    occupants the lot becomes **abandoned** (a grey building sprite). Abandoned lots recover
    automatically when conditions improve.
  - Add randomness (±20%) so growth looks organic, not a wave.
- **Tier requirements** (Anno-like, explicit, shown in the query panel):
  - R tier 2: power + water + school coverage.
  - R tier 3: + clinic + shop within reach + land value ≥ 40.
  - Concrete tier 3: + district heating + low pollution.
  - C tiers need customers (residents within road distance) and land value.
  - I tiers need workers in commute reach and export demand.
- Multi-tile lots form when adjacent zoned tiles are all empty or low-tier and the demand is high.
  Choose the lot size by available contiguous area.

### 4.4 Demand (transparent)

Three demand values in "people" or "jobs" units, recomputed monthly (phase 15). They are shown as bars
with **itemised tooltips**.

```
Residential demand  R = openJobs + ruralMigrationPull − vacantHomes − emigrationPressure
  openJobs          = jobsFilled_target − workers               (jobs nobody fills)
  ruralMigrationPull= eraBaseRate(year) * attractiveness         (1955-70 high, 70s lower)
  attractiveness    = 0.5 + 0.5*(avgHappiness/100) − taxPenalty(residentTax)
  emigrationPressure= unemployed * 0.5                           (they leave to Sweden instead)

Commercial demand   C = spending − commercialCapacity
  spending          = residents * spendPerResident(era, avgIncomeTier) + tourists

Industrial demand   I = (clearingQuota + westernOrders + localNeed) − industrialCapacity
  clearingQuota     = f(easternTrust, current 5-year trade agreement)   (see 4.11)
  westernOrders     = f(westernTrade, era)
```

- **Smoothing:** each demand moves at most 1/4 of the way toward its target per month (Micropolis-style
  "valve" behaviour, which damps oscillation and death spirals).
- **The emigration valve** (thematic, historical): the unemployed don't riot, they **emigrate to
  Sweden**. Finnish emigration to Sweden reached tens of thousands a year around 1969-70. A counter in
  the population panel ("Lähteneet Ruotsiin: 1,240") and a ticker message make it a soft, legible
  failure signal. It also hurts the Workers bloc approval.
- Tax effect: each percentage point above a neutral rate reduces attractiveness (R) or spending/orders
  (C/I) by a small factor. The tooltip shows "taxes: −80".

### 4.5 Networks and services

**Power (pylons plus adjacency)**
- Built tiles conduct power to their 4 neighbours; pylons bridge gaps and water.
- Plants have capacity in "tiles served" (our own numbers, e.g. small coal 400, hydro on a river
  rapids 600 (river-adjacent only), oil 900 (needs imported oil, see 4.11), later nuclear-scale 3,000).
- Solve: on the dirty flag, one flood fill from all plants. If demand > supply, deny power to tiles in
  **reverse priority** (industry last-connected first) and show a brownout overlay.
- Cost: one BFS over about 16k tiles, about 2 ms. Only when the network changes or monthly.
- Why adjacency and not "power lines everywhere": stringing pylons across a lake is a nice visual
  decision; wiring every block is busywork.

**Water (mains inside roads)**
- Sources: pumping station on a lake shore (capacity ∝ adjacent water tiles), groundwater well,
  water tower (buffer).
- Every road tile is a main, and a lot is watered if it touches a watered road.
- Long gaps without roads need a trunk main tool (rare).
- v1.0 adds **sewage treatment** (lake water quality) and **district heating** via the same "in road"
  network type, with heat plants of limited capacity. This gives a three-capacity puzzle (power,
  water, heat) without three drawing layers.

**Services: road distance plus capacity**
- Each service building: `type, capacity, maxRoadDistance, upkeep, quality`.
- Multi-source BFS (phase 13) assigns each road-connected lot to its nearest building of that type
  within D. Then `load = Σ demand of assigned lots` (e.g. school demand = children ≈ 20% of
  residents) and `quality = min(1, capacity/load) * distanceFalloff`.
- Overlay: coverage colour. Query on a building: "Koulu: 412/500 oppilasta" (pupils) and its
  catchment highlighted.
- Starting set: fire station, school, clinic, police (v1.0), **public sauna** (1950s; covers hygiene
  and happiness; obsolete for tier-3 apartments that have their own saunas, a cute era mechanic),
  library, park (radius-based desirability, the one exception: parks are walked to), and post/phone
  exchange (a "phone line" waiting-list satire: the tier-3 requirement in the 1960s).
- No walkers. Cosmetic fire engines or ambulances drive along the BFS predecessor path when an event
  occurs (cheap, correct-looking).

### 4.6 Fields: pollution, land value, crime, happiness

All fields are 64x64 bytes (2x2 tiles) unless noted.

**Pollution (phase 10)**
```
emit[c]  = Σ industry tier emissions + power plant + 0.02*edgeFlow (roads) in cell
P'       = clamp( (P + emit) * 0.9 )             // decay
P''      = 3x3 blur of P'  (centre 4/8, edges 1/8 each, integer shifts)
wind     : every other month shift the whole map 1 cell toward NE (prevailing SW wind)
           i.e. sample P''[x-1][y-1] with weight 1/4
```
Players learn to put industry downwind (east and north-east), which is a satisfying, discoverable
rule. Water pollution (stretch) flows downstream along river tiles.

**Land value (phase 11)**
```
LV = base(terrain: lakeside +20, forest edge +5)
   + services  (Σ quality_i * w_i, up to +30)
   + parks     (radius 4, up to +15)
   + access    (commute time to jobs: up to +15)
   − pollution (P/4)  − crime/4  − noise(edgeFlow near) − abandonedNearby*5
LV smoothed: LV = (3*LV_old + LV_new)/4
```
Land value drives tier upgrades, residential tax yield (`tax ∝ residents * (0.5 + LV/100)`) and the
pace of commercial growth. *Unlike* the 1993 classic, it must actually affect taxes, and the tooltip
says so.

**Crime (v1.0, phase 12)**: `crime = densityTerm + unemployment + (100−LV)/4 − policeCoverage*k`.

**Happiness per residential cell (phase 12)**
```
H = 50 + services + LV/5 − pollution/3 − commuteMinutes/2 − crime/4
    + decreeModifiers − taxPenalty + heatOK(winter) ...
```
It is aggregated per **bloc** (who lives where: workers in apartments and near industry, rural
migrants in wooden houses, and so on; see 4.10). The city-wide mean feeds migration attractiveness.

### 4.7 Traffic and transit

- MVP: connectivity plus commute *distance* only. Block-to-job distance comes from the service-style
  multi-source BFS from job blocks. The traffic overlay is "flow estimate = residents whose shortest
  path passes", computed cheaply from BFS predecessor trees.
- v1.0: the graph assignment with congestion from 3.5. Road classes:

| Class | Capacity (veh/h-ish units) | Speed | Cost | Notes |
|---|---|---|---|---|
| Gravel road (soratie) | 300 | slow | cheap | dust pollution, early |
| Street (katu) | 800 | medium | normal | default |
| Main road (pääkatu, 2 tiles wide or 1 tile "avenue" art) | 2,000 | medium-fast | high | noise |
| Motorway (moottoritie), 1965+ | 4,000 | fast | very high | only connects at interchanges; regional link |

- **Upgrade in place:** dragging a higher class over existing road converts it and charges the
  difference.
- **Bus lines** (v1.0): a line tool clicks stops on roads, and the line follows shortest paths between
  stops. Ridership is a mode split per O/D pair (`busShare = f(busTime vs carTime, carOwnership(era))`).
  Car ownership is low in 1955 and rises steeply through the 60s-70s. That is an era mechanic in itself:
  **traffic grows even if population doesn't**, which is historically right and a great late-game
  pressure.
- **Railway station:** connects to the national network. It is the source of regional migrants and
  freight for industry (raises the I-demand cap like the classic "port" gate, but explained).

### 4.8 Budget, taxes, loans

- **Monthly ledger:**
  - income: resident tax, business tax, clearing/export proceeds share, state grants (political);
  - expenses: service upkeep (scaled by a per-service funding slider 50-120%), road and network
    maintenance, interest.
- **Two tax sliders** in the MVP (residents, businesses). v1.0 splits businesses into commerce and
  industry and adds a sales/alcohol tax as a decree.
- **Loans** (v1.0), each with a political flavour:
  - **Domestic bond:** modest rate, no strings.
  - **Western bank loan:** cheaper and bigger, but Eastern trust falls slightly per loan ("Moscow notes
    your growing ties to Western capital").
  - **Eastern credit:** for specific projects (power plant, element factory) with very low interest,
    but it locks you into delivering industrial quota.
- **Bankruptcy:** a negative balance for 6 months triggers a "state receivership" warning, then an
  emergency loan at a punitive rate. Game over only in campaign mode, and only after explicit warnings.
  No sudden deaths.

### 4.9 Disasters (themed, telegraphed, toggleable)

| Disaster | Trigger | Mitigation | Why it's good |
|---|---|---|---|
| **Fire in a wooden district** | Fire risk field (wooden density × low fire coverage × dry summer) | Fire stations, firebreak roads, concrete | Teaches coverage; motivates the wood → concrete transition |
| **Spring flood** (kevättulva) | Lowland tiles by rivers in April; telegraphed in March | Dikes/embankment tool, don't zone flood plain (overlay shows risk) | Spatial planning lesson, seasonal |
| **Hard-frost winter** | Random winter, announced in November | Heat/power capacity margin | Tests capacity planning |
| **Strike** (political, not physical) | Workers' approval low | Decree concessions | Links politics to the city |

No monsters, no meteors.

### 4.10 The presidential/political layer: a real mechanic

Design test: **does it create a meaningful decision at least every 5-10 minutes that interacts with
the city simulation?** If it is only a popularity bar, cut it.

**Blocs** (fictional names, approval 0-100, each computed from the city every month):

| Bloc | Lives/works | Approval rises with | Falls with |
|---|---|---|---|
| **Työväki** (workers/unions) | apartments, industry | jobs, low resident tax, housing supply, public services, buses | unemployment, emigration, rent decrees removed, layoffs |
| **Maalaiset** (rural/agrarian) | wooden houses, outskirts; and the countryside offscreen | regional subsidies, slow forest clearing, low food prices | fast urban growth, bulldozing wooden districts, forest loss |
| **Patruunat** (industrialists) | industry owners, commerce | low business tax, power reliability, export orders, roads | strikes, high business tax, pollution decrees |
| **Sivistyneistö** (intelligentsia/press) | high land-value areas | schools, libraries, culture, free press | censorship-like decrees, ugly concrete, extending your term |

- **Bloc weight** = their share of the population (computed from housing types and jobs). As the city
  urbanises, power shifts from Maalaiset to Työväki. **The historical arc emerges from the simulation.**
- **Popularity** = the weighted mean of approvals.

**External relations** (two meters):
- **Eastern trust (Itänaapurin luottamus):** affects the clearing quota (industrial demand), oil
  price/allocation and crisis events.
- **Western trade (Länsikauppa):** affects Western orders, loans and later tech (TV factory, computers).
- Many actions trade one for the other. Balancing between them ("active neutrality") is the core
  satirical-but-real tension.

**Authority (Arvovalta), the currency of decrees**
- Monthly income: `+ popularity/20` (0-5 per month), capped at 30.
- Spent on **decrees** (one-time cost plus a cooldown of about 12 months between decrees, Frostpunk
  style) and on **event responses**.
- **At most 5 active decrees ("Cabinet slots").** Choosing *which* to keep active is the strategy.
  Repealing costs half the original authority (flip-flopping is not free).

**Decrees** change simulation parameters, never a flat "+happiness". Examples (our own; historical
inspiration in brackets):

| Decree | Effect on simulation | Blocs |
|---|---|---|
| Housing production programme | Apartment growth threshold −30%, element factory output +25%, construction cost −10%, apartment tier quality cap −1 for 5 years | Työväki +, Sivistyneistö −, Maalaiset − |
| Rent regulation | Resident happiness +5, land value growth halved, commercial demand −5% | Työväki ++, Patruunat − |
| Medium beer in grocery stores [1969 reform] | Commercial demand +8%, crime +5%, tax income +3% | Työväki +, Maalaiset −, temperance event chain |
| Regional development subsidy | Rural migration −40% (slower growth), money −X/month, Maalaiset ++ | slows your city on purpose: a real trade-off |
| Comprehensive school reform [1970s] | School capacity +20%, upkeep +30%, education → land value bonus doubled | Sivistyneistö ++, Patruunat − |
| Five-day work week | Commercial demand +10% (leisure), industrial output −5% | Työväki ++, Patruunat − |
| Smokestack standards | Industry pollution −40%, industrial demand −15% | Sivistyneistö +, Patruunat −− |
| Friendship treaty celebrations | Eastern trust +15, Western trade −5, costs money | external balancing |
| Emergency powers law | Postpones the next election by 4 years; Sivistyneistö −30, Western trade −10, authority +10 | **the satirical centrepiece** [the 1973 term extension by emergency law] |

**Events** (cards with 2-3 choices, at most 1 per 1-2 game years, never modal at a bad moment: they
wait in the ticker until opened):
- A "Night frost" in Eastern relations [inspired by the 1958 crisis].
- A diplomatic note [1961].
- A general strike [1956].
- A visiting dignitary, an Olympic bid, a TV broadcast licence, a scandal in the town council.
- Each card states its effects on the meters and the simulation explicitly.
- The **sauna invitation** is a recurring cheap way to convert money and time into Eastern trust or
  bloc approval, with diminishing returns (satire: "the agreements were reached at 90 °C").

**Elections every 6 years** (the presidential term):
- The result is a function of popularity plus bloc-specific turnout plus last-year momentum.
- Options:
  - campaign (spend money and authority);
  - "convince a rival to withdraw" (authority, and Sivistyneistö falls);
  - **emergency law** (see above).
- Losing ends the campaign in campaign mode, with a satirical retirement newsreel; it is optional in
  sandbox.
- Each term also has **term goals** set by the scenario (e.g. "house 20,000 people", "balance the
  clearing account", "no district without a school").

**End-of-game judgement:** a "newsreel" summary in the style of a 1960s cinema newsreel. Population
curve, what you built, which decrees defined your era, emigration totals, the national-myth verdict.
The Frostpunk lesson: a story that reflects the player's choices back at them.

### 4.11 Clearing trade: the thematic economy

- The Eastern neighbour signs a **5-year trade agreement** with you. You are a city, but the fiction
  is that your town's industry is the national showcase.
  - It specifies **export quota categories** your industry can fill (ships/machinery from metal works,
    prefab houses from sawmills, textiles), worth clearing credit.
  - The clearing credit can only buy Eastern goods: **oil (fuels oil power plants and heat plants),
    cheap trams/buses, raw materials**.
- **The industrial demand** model term `clearingQuota` is that agreement. The bigger the quota, the
  more I-demand, as long as trust holds.
- **The clearing balance** must stay near zero:
  - running a big surplus means credit you can't spend (wasted value, and a ticker joke about storing
    roubles in the presidential sauna);
  - running a deficit (importing more oil than you export) drops trust.
- **1973 oil shock** (v1.0 era event): the oil price in clearing goods quadruples. Historically
  Finland responded by exporting much more to the East, which boosted its industry. In-game, I-demand
  jumps if you can supply it, while power and heat costs rise sharply. That is a great late-game
  disruption that rewards a prepared city.
- **Western trade** is the alternative: hard currency, needed for modern tech unlocks (electronics
  industry, computers at the town hall). Its orders are more volatile.

This replaces the generic "external industrial demand" of the classics with a decision space that is
historically grounded, funny and system-driven.

### 4.12 Eras and unlocks (time scale 1955-1990)

Unlocks use **both** the year and milestones. The year gives historical flavour. Milestones (tiered
population counts, not raw population) make sure the player has earned it.

| Era | Years | Theme | Unlocks (examples) | New pressures |
|---|---|---|---|---|
| Reconstruction | 1955-59 | wood, sawmills, rural migrants | gravel roads, streets, wooden houses, shops, sawmill, coal plant, pump, fire station, school, clinic, public sauna | basic needs, wooden fires |
| Structural change | 1960-67 | concrete suburbs, the great migration | element factory, apartment blocks, main roads, buses, district heating, metal works, police, library, TV-licence event | element supply cap, heating capacity, cars appear |
| Welfare state | 1968-74 | schools, health, cars | motorway, comprehensive school reform, health centre, department store, oil plant; 1973 oil shock | traffic boom, oil dependence, pollution awareness (Sivistyneistö turns green) |
| Late Cold War | 1975-82 | stability and sameness | nuclear-scale plant, electronics industry, shopping centre, sewage treatment, tram/metro debate | suburb image decline, ageing wooden areas |
| Renewal | 1983-90 | consumerism | renovation programme, pedestrian streets, computers; 1990: end of clearing trade looms | late-game: renew rather than expand |

A soft end: in 1990 the clearing trade ends (historically in 1991). The campaign ends with the
newsreel; the sandbox continues indefinitely.

### 4.13 Scenarios vs sandbox; win structure

- **Sandbox:** choose a map (procedural lake land or a handmade map), start year, money, toggles
  (disasters, politics on/off, elections on/off).
- **Campaign:** one city from 1955, term by term (5-6 terms). Each term has 2-3 goals plus the
  election. Failing goals lowers popularity; it isn't an instant loss.
- **Scenarios** (short, 1 term each; Against-the-Storm-sized sessions of 30-60 min). Fictional towns,
  historical inspiration:
  - "Rebuild the burned northern town" [Lapland reconstruction].
  - "The lakeside suburb" (house 15,000 in concrete by 1968).
  - "The oil shock" (start 1972 with an oil-dependent city).
  - "The Olympic bid".
  - "The flood year".
- **Score** = population + happiness + legacy points from goals. Shown on a local high-score table.

### 4.14 Early game, late game, death spirals

- **First 5 minutes:**
  - start with a highway link to the national road, a little money and high R demand (rural migrants
    waiting);
  - zone wooden houses and the first homes appear within about 10 seconds;
  - a mini tutorial is a ticker message chain with goals ("Lay a road from the highway", "Zone 20
    houses", "Build a pump").
- **Late game:**
  - era shifts (4.12); car ownership growth; ageing housing (tier quality decays slowly after 25 years
    unless renovated, v1.0+);
  - elections; the external economy (oil shock, trade agreement renegotiations);
  - regional comparisons (stretch).
  - Also **"vanity projects"**: unique buildings unlocked by achievements (a concert hall, a TV tower,
    a presidential summer residence) that provide land value and authority. They are something to
    *want*.
- **Death-spiral guards:**
  - demand smoothing;
  - abandonment slow and reversible;
  - emigration (soft) instead of collapse;
  - an emergency loan;
  - the advisor warns at trend-reversal time ("Unemployment rising three months in a row");
  - a bloc approval floor at 10 (they never fully abandon you);
  - no cohort ageing simulation.

---

## 5. UX on 640x480 / 800x600 with a mouse

### 5.1 How the classics laid it out

- **SimCity 2000:** a menu bar at the top, a **floating vertical tool palette** (two columns of
  icons, many with fly-out sub-menus), separate floating windows for map, budget and graphs, and a
  status line. It was dense, discoverable by hovering, and windowed (slow to manage at 640x480).
- **SimCity 3000:** a **fixed bottom-left control panel** with tool buttons opening sub-palettes,
  RCI meters, view/zoom/rotate controls, and the news ticker along the bottom. It was cleaner and
  "console-like". Advisors and petitioners were reached from the panel.
- **Cities: Skylines:** a bottom toolbar with category tabs, a top-left info-view button,
  speed/date/money at the bottom, and hover tooltips with full detail.
- Common lesson: **a fixed frame, tools grouped by category with fly-outs, overlays one key away.**

### 5.2 Our layout (800x600 primary; also fits 640x480)

```
+------------------------------------------------------------------------------------+
| 1962 Kesä  | 12 430 mk +820 | Asukkaat 8 214 | R▮▮▮ C▮ I▮▮ | Suosio 61% | ▶▶ | ☰ |  16 px status strip
+----+-------------------------------------------------------------------------------+
|ROAD|                                                                               |
|ZONE|                                                                               |
|PWR |                        isometric city view                                    |
|WTR |                 (world cache + sprites + overlay)                             |
|SVC |                                                                               |
|PLC |                                                               +-----------+   |
|TRN |                                                               | minimap   |   |
|DOZ |                                                               | 128x128   |   |
|QRY |                                                               | (toggle M)|   |
|----|                                                               +-----------+   |
|OVL |                                                                               |
|POL |                                                                               |
+----+-------------------------------------------------------------------------------+
| ▸ Sawmill workers threaten strike over pay                  [!] [open]             |  14 px ticker
+------------------------------------------------------------------------------------+
  40 px tool column (32x32 icons)
```

- **Status strip** (always visible): date + season, money + monthly delta, population, mini RCI bars,
  popularity, speed buttons, menu.
  - Hovering any item gives a breakdown tooltip; clicking opens the panel (budget, population,
    demand, politics).
- **Tool column:** 32x32 icons in 10 groups. Clicking opens a **fly-out** to the right with that
  group's tools (icon + cost + 1-word name).
  - Right-click on the column reopens the last tool.
  - Unavailable (era-locked) tools are visible but greyed, with a tooltip saying "Unlocks: 1960 /
    element factory". That's goal-setting for free.
- **Ticker:** one line, rate-limited, colour-coded (grey info, yellow warning, red urgent, blue
  political event). Clicking jumps the camera to the location or opens the event card.
- **Minimap:** 128x128 at 1 px per tile, which fits perfectly. It shows terrain/zones or the current
  overlay, and click-drag moves the camera.
- **Panels:** fixed-size (at most 320x400) **non-modal** panels docked over the right of the view,
  one at a time, closed with right-click or Esc. The game keeps running (except in dialogs that
  explicitly pause: events, elections, save).
- At 640x480: the same frame, 15% less map, and the minimap overlaps more. **Design every panel to
  fit 600x420.**

### 5.3 Mouse grammar (consistent everywhere)

- **Left-drag** builds:
  - roads: L-shaped path preview; Shift for a straight line only;
  - zones: rectangle;
  - pylons: line;
  - bulldozer: rectangle.
- The **preview** shows the cost, the tiles that will be rejected (hatched red) and **projected
  effects**:
  - placing a factory shows its pollution footprint and its land-value hit on neighbouring
    residences;
  - placing a school shows its catchment and expected load;
  - a road shows the connections created.

  This is the Islanders lesson applied to a serious builder.
- **Right-click** cancels the current drag or tool. Right-click on the map with no tool is a query.
- **Middle-drag or edge-scroll** (edge-scroll toggleable) and arrow keys/WASD scroll.
- **Hover** over any lot gives a 1-line tooltip after 400 ms ("Wooden house, tier 2, 6 people,
  needs: clinic").

### 5.4 Hotkeys

| Key | Action | Key | Action |
|---|---|---|---|
| R / Z | road tool / zone fly-out | 1-9, 0 | overlays (power, water, pollution, land value, traffic, services..., off) |
| B | bulldoze | Q | query |
| Space | pause | +/- or F1-F3 | speed |
| Ctrl+Z / Ctrl+Y | undo / redo | Tab | cycle current overlay sub-layer |
| M | minimap | PgUp/PgDn | zoom 1x/2x |
| Home | centre on town hall | Ctrl+S / Ctrl+L | save / load |
| F5 | quicksave | Esc | close panel / menu |
| Arrows/WASD | scroll | J | jump to latest ticker event |

### 5.5 Feedback channels (without nagging)

- **Floating numbers** when building (−450 mk) and on monthly tax collection (subtle + over towns).
  These are pre-rendered digit blits.
- **Ticker** rate limits: at most one message per topic per in-game month, at most 3 queued. The same
  warning doesn't repeat until its condition clears and comes back. Every message is actionable
  (click → location or panel).
- **Advisors:** no talking heads in the MVP. Instead, every panel has a **"Top 3 problems"** box,
  computed: "1. 340 homes without water (Kivikko). 2. School overloaded 140%. 3. Unemployment 6%."
  In v1.0, give them faces and satirical names (ministers who report to the president), drawn once as
  portraits.
- **Newspaper** each January: a front page with 3 headlines generated from the year's biggest stat
  changes plus one satirical filler. It is a full-screen bitmap with pre-rendered headline text, a
  big moment of feedback.
- **Sound:** a construction thud, a cash register for taxes, a ticker chime, an ambient town loop
  that changes with era (accordion → rock'n'roll radio). Use waveOut on a worker thread.

### 5.6 Save/load and settings

- **Save format:** a binary, chunked, versioned file (`KAUP` magic, version, then chunks: TILE, FLDS,
  GRPH, BUDG, POLI, RNG, HIST; the undo log is not saved). RLE-compressed: a 128² city is about 50-150 KB.
- 8 slots + autosave every game year (a 3-file rotation) + quicksave.
- Saves contain the RNG state, so loading reproduces the future exactly (determinism helps debugging).
- `KAUPUNKI.INI`: resolution (640/800), windowed/fullscreen, edge-scroll, ticker verbosity, language
  (FI/EN string tables), disasters on/off.

---

## 6. Ten design principles for Kaupunki

1. **Every number is visible and explained.** Each simulated quantity has an overlay, a tooltip or a
   query line. If we can't show it, we don't simulate it. (The CS2 economy lesson.)
2. **Aggregate simulation, cosmetic life.** The city is numbers on grids and a graph. Cars and people
   on screen are decoration driven by those numbers. Never let a dumb agent be the reason something
   fails.
3. **Preview before commit, undo after.** Show cost, rejected tiles and projected effects while
   dragging, and allow free undo within the month. No fear, no wiki.
4. **Answer "why isn't it growing?" in one click.** A requirement checklist on every lot and an
   itemised demand tooltip.
5. **Networks are decisions, not chores.** Draw roads and pylons. Water and heat follow roads. Logistics
   become capacities (element factory, clearing quota), not trucks.
6. **Theme is mechanics.** Rural migration is demand, emigration to Sweden is failure, concrete
   elements are a growth cap, wooden districts burn, the clearing trade is industry, and saunas are a
   service. The 1960s transformation must be something the player *does*, not wallpaper.
7. **Politics reads the city.** Bloc approval is computed from what you built, and decrees change
   simulation parameters with explicit trade-offs, cooldowns and limited slots. Satire is in the
   words, and the numbers are honest.
8. **Fast early game, new problems late.** The first houses appear within 10 seconds. Eras, elections,
   car growth and the oil shock keep the late game moving. Expansion gives way to renewal.
9. **Fail softly and legibly.** Smoothed demand, slow reversible decline, warnings before crises,
   emergency loans. Never a sudden collapse the player didn't see coming.
10. **Respect the machine.** Fixed arrays, integer and deterministic simulation, a 16-phase schedule, a
    cached world layer, a minimal blit count, pre-rendered text, and a 60 fps display lock with
    adaptive simulation slices. Measure on the VM and keep a `-bench` mode from day one.

---

## 7. Open questions for the GDD

- **Tile size:** 32x16 (SC2000-like scale, more map on screen, cheaper art) vs 64x32 (more detail, 4
  times the art, a smaller view). Recommendation: 32x16 base with a hardware 2x zoom; confirm with an
  art test in `art/explore`.
- **Rotation:** 4 view rotations (SC2000/3000) cost 4 times the building art. Recommendation: **no
  rotation**. Keep tall buildings back-to-front readable with slicing and an optional "lower
  buildings" X-ray key.
- How literal is the president caricature? (See the naming note in 4.)
- Language: Finnish-first with an English string table? The bitmap font needs ÄÖÅäöå either way.
- Do we simulate seasons in v1.0 (winter art doubles the tileset) or stretch?
- Map generation: procedural lake land (cellular-automata lakes + river carving + forest noise) vs
  hand-made maps. Both are cheap; procedural maps add replayability.

---

## 8. Sources

Classic simulation internals
- Micropolis (open-source original SimCity) simulation loop and RCI valves: [s_sim.c](https://github.com/SimHacker/micropolis/blob/master/micropolis-activity/src/sim/s_sim.c)
- Micropolis traffic generator (random walk, MAXDIS 30, +50 density): [s_traf.c](https://github.com/SimHacker/micropolis/blob/master/micropolis-activity/src/sim/s_traf.c)
- Chaim Gingold, *SimCity Reverse Diagrams* (2016): [PDF](https://smalltalkzoo.computerhistory.org/users/Dan/uploads/SimCityReverseDiagrams.pdf)
- Chaim Gingold, *Building SimCity: How to Put the World in a Machine* (MIT Press 2024): [publisher listing](https://mitpressbookstore.mit.edu/book/9780262547482), [LA Review of Books review](https://lareviewofbooks.org/article/seeing-like-a-simulation/)
- SimCity 2000 file format (per-tile layers, 2x2/4x4 field maps): [SC2k-docs, sc2 file spec](https://github.com/OpenCity2k/SC2k-docs/blob/master/sc2%20file%20spec.md)
- SimCity 2000 simulation notes (power/water capacities, taxes): [SC2k-docs, simulation spec](https://github.com/OpenCity2k/SC2k-docs/blob/master/simulation%20spec.md)
- SimCity vs SimCity 2000 traffic experiment: [Game Developer](https://www.gamedeveloper.com/game-platforms/simcity-vs-simcity-2000-my-home-town-s-traffic-problems-modelled)
- SimCity 2000 period requirements (386 floppy → 486 CD; Win 3.1: 486/8 MB): [Vogons thread](https://www.vogons.org/viewtopic.php?p=1294102), [Ludo guide](https://www.ludo.guide/guide/simcity-2000/system-requirements)
- SimCity 3000 requirements (P166/32 MB): [Computer Hope](https://www.computerhope.com/games/games/simcity.htm); advisors/petitioners/ticker: [Wikipedia](https://en.wikipedia.org/wiki/SimCity_3000), [SimCity wiki](https://simcity.fandom.com/wiki/SimCity_3000)
- SimCity 4 routing observations (shortest-route, monorail imbalance): [AnandTech forum](https://forums.anandtech.com/threads/simcity4-deluxe-city-stagnation.1951686)
- Caesar III overview: [Wikipedia](https://en.wikipedia.org/wiki/Caesar_III); walker design analysis: [Game Wisdom, "Of Walkers and Men"](https://game-wisdom.com/guest/of-walkers-and-men-city-building-by-design); housing desirability: [Caesar III Augustus guide](https://www.caesar3augustus.com/book/housingdesirability/start)
- Lincity-NG: [Debian metadata](https://appstream.debian.org/sid/main/metainfo/lincity-ng.html), [StrategyWiki](https://strategywiki.org/wiki/Lincity), [GitHub](https://github.com/lincity-ng/lincity-ng)
- OpenTTD YAPF segments and cost cache: [OpenTTD wiki archive](https://wiki.openttd.org/en/Archive/Manual/Yet%20Another%20Pathfinder), [tt-forums NPF vs YAPF](https://www.tt-forums.net/viewtopic.php?p=602541), [yapf_costcache.hpp](https://docs.openttd.org/source/d9/d05/yapf__costcache_8hpp_source.html)

Modern games
- Cities: Skylines milestones: [Paradox wiki](https://skylines.paradoxwikis.com/Milestones)
- Cities: Skylines II traffic AI (time/comfort/money/behaviour; CS1 "proximity-based"): [Paradox feature page](https://www.paradoxinteractive.com/games/cities-skylines-ii/features/traffic-ai), [Dev diary 2 forum thread](https://forum.paradoxplaza.com/forum/threads/development-diary-2-traffic-ai.1591141/page-5)
- TM:PE despawn and lane issues: [TM:PE wiki, Toggle Despawn](https://github.com/CitiesSkylinesMods/TMPE/wiki/Toggle-Despawn), [Steam workshop](https://steamcommunity.com/sharedfiles/filedetails/comments/1637663252)
- Chirper complaints: [Steam discussion](https://steamcommunity.com/app/255710/discussions/0/613958868364268310)
- Cities: Skylines II launch performance and Economy 2.0: [Dexerto patch 1.1.5f1](https://www.dexerto.com/gaming/cities-skylines-2-patch-notes-1-1-5f1-economy-changes-bug-fixes-more-2794481/), [PCGamesN employment fix](https://www.pcgamesn.com/cities-skylines-2/employment-fix), [Insider Gaming](https://insider-gaming.com/cities-skylines-2-economy-rework-coming-june-24-devs-address-console-version)
- SimCity (2013): [Wikipedia](https://en.wikipedia.org/wiki/SimCity_(2013_video_game)), nearest-destination agents: [Scientific Gamer](https://scientificgamer.com/thoughts-simcity/), Ocean Quigley interview: [SimsCommunity](https://simscommunity.info/2016/05/21/ocean-quigley-opens-up-on-why-simcity-2013-failed/)
- Workers & Resources: Soviet Republic reception: [OpenCritic](https://opencritic.com/game/16932/-/reviews), [Notebookcheck](https://www.notebookcheck.net/Not-for-beginners-Complex-city-builder-90-positive-drops-below-10-on-Steam.1342440.0.html)
- Tropico factions/edicts/elections: [Tropico 4, Wikipedia](https://en.wikipedia.org/wiki/Tropico_4), [Tropico 3 developer interview, WorthPlaying](https://www.worthplaying.com/article/2009/6/4/interviews/62237-tropico-3-developer-interview/), [Tropico 4 interview, Gaming Nexus](https://www.gamingnexus.com/Article/3167/Tropico-4-Interview)
- Frostpunk laws/hope/discontent: [Wikipedia](https://en.wikipedia.org/wiki/Frostpunk), [PC Gamer developer interview](https://www.pcgamer.com/au/frostpunk-developers-on-hope-misery-and-the-ultimately-terrifying-book-of-laws), [Unwinnable essay](https://unwinnable.com/2021/12/10/authoritarianism_in_frostpunk/)
- Banished: [Wikipedia](https://en.wikipedia.org/wiki/Banished_(video_game)), [TV Tropes](https://tvtropes.org/pmwiki/pmwiki.php/Videogame/Banished), [Steam: age chart request](https://steamcommunity.com/app/242920/discussions/0/1694917906659024571)
- Against the Storm design interview: [Game Developer, Road to IGF 2023](https://www.gamedeveloper.com/road-to-igf-2023/how-against-the-storm-managed-to-mix-city-building-and-roguelite-play), [Shacknews GDC 2023](https://shacknews.com/article/135267/against-the-storm-gdc-2023-interview)
- Islanders: [Wikipedia](https://en.wikipedia.org/wiki/Islanders_(video_game)); Townscaper: [Wikipedia](https://en.wikipedia.org/wiki/Townscaper); Dorfromantik: [Wikipedia](https://en.wikipedia.org/wiki/Dorfromantik); Anno 1800: [Wikipedia](https://en.wikipedia.org/wiki/Anno_1800)

Finnish history (theme inspiration)
- Finnish-Soviet bilateral clearing trade 1952-1990: [Matala, Scandinavian Economic History Review](https://ideas.repec.org/a/taf/sehrxx/v70y2022i1p87-107.html)
- Prefab wooden house exports to the USSR: [Archinfo Finland](https://www.archinfo.fi/en/articles/suomesta-vietiin-maailmalle-valtavat-maarat-puutaloja)
- First prefabricated-element suburb (Pihlajamäki): [Docomomo Finland](https://en.docomomo.fi/projects/pihlajamaki-residential-area/)
- (General background, from common knowledge to be verified when writing event texts: the 1956 general
  strike, the 1958 "night frost" and 1961 "note" crises, the 1969 medium-beer reform, the 1973 term
  extension by emergency law, the 1969-70 peak of emigration to Sweden, the 1991 end of clearing
  trade.)

House measurements
- DirectDraw blit costs and fps in this VM: `dev/sandline/probe/RESULTS.md`, `dev/runner/PERF.md`
