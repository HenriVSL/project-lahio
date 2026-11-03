# WATER: SimCity-style pipes for KAUPUNKI (research + design)

Status: **implemented by F1 (Milestone 2)**; approved via DESIGN 14.1. Code: `src/net.cpp` (solve_water,
water queries), `src/tools.cpp` (TOOL_PIPE), `src/world.*` (TF_PIPE, ZF_WATER_NET, WCHG_PIPE, TILE v2 migration),
tests `tests/test_water.cpp`. Deviations from this text: pipe removal is Ctrl+drag with the pipe tool (not
right-drag); the source highlight follows a Query click on the source (no hover yet); no source gauges yet;
pumping stations without power still own their area (output 0, "needs power"); NET chunk v2 saves the results
instead of re-solving (determinism). Original proposal text follows.

**User request (play-test):** pumping stations feed a pipe network, buildings pass water on, and it must be easy to
see how much one pumping station covers and whether there is spare capacity. User's model: *"the water source adds
it to the pipe network; it reaches X distance, and if there is more load the radius it can cover is less."*

**Short answer:** that is how SimCity 4 behaves (pipes shrink their coverage when demand exceeds supply), and roughly
how SimCity 2000 behaves (when water is short, the far edge goes dry first). The user's model maps cleanly onto a
**capacity-limited flood from each source along pipes and buildings**. KAUPUNKI's current solver already allocates
nearest-first. What changes is the graph (player-built pipes plus conducting buildings, instead of roads) and the
**visibility**: each source shows its own reach and spare capacity.

---

## 1. How the classic games did it

Confidence: **[M]** = manual or official, **[C]** = several community sources agree, **[?]** = one source, or
something I remember but could not confirm. Sources are listed in section 9.

### 1.1 Summary table

| | SimCity (1989) / Micropolis | SimCity 2000 (1993) | SimCity 3000 (1999) | SimCity 4 (2003) | Cities: Skylines (2015) |
|---|---|---|---|---|---|
| Water system | **none** [M] (SC2000's manual lists the water layer as new) | pumps, pipes, towers, treatment, desalination [M] | pumps, towers, desalination, treatment, pipes [C] | pumps, towers, treatment, pipes [C] | pumps (+ drain pipes for sewage), towers, pipes [C] |
| Who lays pipes | n/a | player draws **mains** in the underground view; Sims auto-install **local plumbing under every developed building** [M][C] | player draws pipes | player draws pipes | player draws pipes (CS2: pipes come with roads [?]) |
| How water reaches a building | mains connect to local plumbing; **developed zones conduct water through themselves** (a main can run into one edge of a block) [C] | **radius around pipes: about 7 tiles** [C: 1999 review + guide] | **radius around pipes: 6 tiles in the cardinal directions** [C: StrategyWiki + forum "one pipe every 12 tiles covers all"] | coverage area around pipes (shaded in the info view) [C] |
| Source output | pump = `sea_level*5 + fresh_water_neighbours*10 + rain/2` **tiles** per month (x720 = "gallons") [C]; next to fresh water about 2x a land well; salt water gives no bonus [M]; needs power [M] | pump 1,500 m3/month, must touch fresh water; tower 600 anywhere; desalination 5,000 [?] | pump 20,000 m3, tower 2,400 [C]; pumps shut down in heavily polluted ground [C] | pump about 120,000 m3/week capacity [C]; output also depends on the water body |
| Storage | **towers store surplus** (about 400 tiles' worth, "40,000 gallons") to cover drought months [C] | towers are just weak sources [?] | towers are weak sources [C] | towers are standalone sources [?] |
| Seasons | yes: monthly rain changes pump output; droughts drain the towers [C][M] | no [?] | no | no |
| Shortage: who goes dry | water is allocated **from the city centre outward in a diamond** (not from the pumps!); outlying areas go dry first, and pumps at the edge don't help [C] | not documented | **coverage radius of pipes shrinks**; building more pipes shrinks it further [C] | edge of the network loses coverage first [?: water flows outward from the pumps] |
| Pollution | treatment plant (covers about 2,000 tiles) cleans water pollution [C] | treatment plant | water pollution map; treatment plants have a radius [C] | sewage downstream of an intake pollutes it [C] |
| UI | underground view: **blue pipes = wet, black = dry** [C]; water map green/red/blank [C]; query shows pump output; graph shows % unused [C] | water view, query | water data view; pump/tower query | info view: coverage + **"usage / capacity" bars** [C]; red water-drop icons on dry buildings [C] |
| Cost (era money) | pipe $3/tile, pump $100, tower $250, treatment $500, desalination $1,000 [M] | pipe $5/tile, pump $300, tower $150, desalination $1,500, treatment $15,000 [?] | pump §1,400 (§350/month), tower §150 [C] | |

Lincity-NG (as I remember it [?]) moves water as a commodity over the transport network. OpenTTD has no
municipal water: in the sub-tropic climate, desert towns need water delivered as cargo. Neither fits a SimCity-style
network, so I did not research them further.

### 1.2 SimCity 2000 in detail (the user's reference)

- **Two layers of pipe.** "When the Sims build, they install the underground water pipes for their buildings. Your
  only responsibility is to hook the buildings up to the water system" (manual, Water System tool). In the
  underground view, the automatic local pipes look thin. The player's mains look thicker and may run under existing
  buildings. A main only has to reach the local plumbing network: a developed block carries water through itself, much
  like buildings conduct power. Undeveloped zones don't conduct. [M][C]
- **Pumps.** On land a pump works as a well. Next to a lake or river it gives about twice as much. Next to salt water it
  gives no more than a well. Pumps need power. Community formula: `sea_level*5 + fresh_neighbours(0..8)*10 +
  precipitation/2` tiles per month, so placement and terrain matter. Typical guide figure: one pump supports about 17
  tiles on dry land. [M][C]
- **Towers** fill only when the city has a surplus and drain in dry months. They act as a buffer, not a source. Guides
  prefer more pumps, because a long drought empties the towers anyway. [C]
- **Shortage.** Water goes out from the city centre in a diamond (Manhattan distance), whatever the pump positions.
  The diamond's size is proportional to supply, and the far edges go dry. Players found this odd: "adding pumps at
  the edges doesn't fix this". A powered pump with no pipes adds unused capacity and silences the shortage messages
  without watering anything (the "phantom pump" exploit). [C]
- **Visibility.** Blue and black pipes in the underground view make it obvious at a glance where water flows. That is
  the part worth copying.

### 1.3 SimCity 3000 and SimCity 4

- **SC3000** replaced "connect every block" with a **radius around each pipe (about 7 tiles)**, so players lay a
  sparse grid of mains under the roads and "let the zones water themselves". Pumps must touch fresh water; towers
  work anywhere but are weak. [C/?]
- **SC4:** pipes have **no capacity of their own**. They distribute about **6 tiles in the cardinal directions**.
  "If water demand exceeds supply, the pipes will reduce their distribution radius so that not all buildings are
  covered; building more pipes in this situation will only result in a further reduction of the distribution
  radius." Players answered with "double coverage" (every tile within reach of two pipes holds up to about 85% load
  instead of about 50%). The fact that *more pipe makes it worse* shows that the rule is opaque: the player can't see
  the budget being spread thinner. [C]
- Lesson for us: a **radius that shrinks with load** is the right idea (it is the user's model), but it should
  shrink **in a visible place**, around the overloaded source, and laying more pipe should never hurt.

### 1.4 Cities: Skylines (brief)

- Pipes give a coverage area, and a building inside the area of a network that has capacity gets water. The info view
  shows city-wide **usage vs capacity bars** and coverage, and dry buildings get a red drop icon. [C]
- How CS1 distributes water internally (as I remember the modding community describing it [?]): water spreads
  outward from each pump through the pipe grid in "pulses" until the pump's capacity is used up. Power works the same
  way: the lit area visibly stops where capacity runs out. That is the user's model exactly, and the basis of the
  recommendation below.
- Players' complaint: the displayed "capacity" counts pumps that aren't actually delivering anything (nominal
  figures). Our numbers should always be *actual delivered* and *actual output*.

---

## 2. What we keep and what we drop

| Keep | Drop |
|---|---|
| Player-drawn **pipe network** with visible wet/dry state (SC2000 blue/black) | City-centre diamond allocation (SC2000): unintuitive, and it punishes good pump placement |
| **Buildings conduct** water once built (SC2000 local plumbing): one pipe per block, not per house | Pipe radius of 6-7 tiles (SC3000/SC4): with it, conduction is pointless and pipes become a sparse grid chore |
| Pump output from **adjacent fresh water** (SC2000, already ours: 40 per water tile within 2, max 500) | Rain and drought (no weather model in M1; maybe later as a "kuiva kesä" (dry summer) event) |
| **Load shrinks reach** (SC4, CS), but per source, visible, and laying more pipe never hurts | Desalination (we have lakes, not sea) |
| Towers as landmarks with a real role | Phantom-pump style capacity that isn't delivered |
| City-wide usage/capacity readout (CS) | A separate "underground view" mode (the water overlay *is* the pipe view) |

---

## 3. Recommended mechanic for KAUPUNKI

### 3.1 The rule in one paragraph (as the player reads it in the manual/tooltip)

> Water sources (pumping station, well, water tower) push water into the **pipes** and into any **building** that
> touches them; buildings pass water on to the pipes and buildings they touch. Each source waters the **nearest**
> buildings first (distance measured along the pipes) until its output is used up: that distance is its **reach**.
> More load means a shorter reach. Spare water from other sources on the same network then flows on to buildings
> that are still dry, nearest first. Whatever is left beyond is **dry**.

### 3.2 Pipes

- **Pipe tool** (Water group, first button; hotkey W selects it, W again cycles to pump/well/tower as today). Drag
  an L-shaped path exactly like the power-line tool (`tools_apply_line`). Pipes are underground: they can run
  **under grass, roads, rail and existing buildings**, and building on top of a pipe keeps it. Lakes: allowed at
  5x cost (river crossings to a pumping station on the far shore).
- **Cost:** 3 mk per tile (power line is 4, street 12), 15 mk per tile under water; **upkeep 1 mk per 10 tiles per
  month** (same field style as `power_line_upkeep10`). Bulldoze refund like power lines.
- **Removing pipes:** the bulldozer removes pipes **only while the water overlay is on** (otherwise you'd delete
  invisible things). Right-drag with the pipe tool also removes them.
- **Pipes do not run under roads automatically.** Recommendation: no, because that is the very thing the user asked to
  get away from, and auto-pipes would make the pipe layer pointless in a grid town. To keep it from becoming a chore:
  (a) buildings conduct, so one pipe per *pair* of block rows is enough; (b) pumps and wells water the buildings
  they touch without any pipe; (c) the pipe tool turns the water overlay on by itself (SC2000 did the same with
  its underground view) and restores the previous overlay when you leave it.
  *Optional (open question Q2):* a checkbox in the Water panel, "Lay pipes with new roads", default **off**,
  for players who don't want to bother.
- **Pipes carry unlimited flow.** Only sources limit the water. Laying more pipe never reduces anyone's water, which
  avoids the SC4 trap.

### 3.3 Conduction and spread

- **Graph:** tiles with a pipe, plus tiles of lots that conduct. Lots that conduct are every lot **except
  `BDF_NO_CONDUCT`** (parks, sports field, ice rink): the same `s_def_cond` table power uses. Lots under construction
  and abandoned lots conduct too (the plumbing is in the ground). Movement is 4-neighbour, 1 per tile, so walking
  across a 3x3 works costs 3.
- **Roads do not conduct water.** Unlike power, which jumps one plain road tile (`POWER_JUMP_ROADS`), water needs a
  pipe under the street or a pipe crossing it. This one rule is what gives the pipe layer meaning: a pipe under a
  street feeds both rows of lots, and the lots carry water to the rows behind them. Keep it as a constant
  `WATER_JUMP_ROADS 0`. Setting it to 1 makes pipes almost unnecessary inside a town (open question Q3).
- **Wet ground (empty tiles, for zone growth):** an empty tile is watered if one of its 8 neighbours is a **wet**
  pipe or a watered lot. Apartments T1 need water from the first brick, and with this rule they can grow next to a
  pipe or a watered building. Constant `WATER_SPREAD 1` (Chebyshev). Bigger values move toward SC3000 and make
  conduction irrelevant.

### 3.4 Sources

| Source | Output (units) | Placement | Notes |
|---|---|---|---|
| Pumping station | `min(500, 40 x water tiles within 2 of the footprint)` (unchanged) | touching a lake/river shore | **needs power** (SC2000; open question Q4). The placement preview shows "Output here: 440". |
| Groundwater well | 150 (unchanged) | anywhere | no power needed. Early villages: a well among the houses works with no pipe at all. |
| Water tower | **200 "peak buffer"**, but only if its network also contains a pump or well | anywhere on the network | the landmark keeps its job: it adds a modest amount of water where it stands, and its **pressure zone** (buildings within **16 pipe tiles**, measured through pipes and buildings) meets `REQ_WATER_TOWER` (Apartments T2+). Replaces "+30% within 20 road tiles", which nobody could see. |

Units stay as they are (house T2 = 1, lamellitalo 9-12, pistetalo 32, factory 4-8), so BALANCE numbers survive. A
500 pump feeds about 40 lamellitalo or about 500 rintamamiestalo.

### 3.5 Sharing when demand > supply (who goes dry)

Options considered:
- **Spread evenly** (everyone at 85%): unreadable, gives no place to act. Rejected.
- **Priority by building type** (services first): extra rules, low value in M1. Maybe later for clinics.
- **Nearest first, furthest goes dry** (SC4/CS, our power brownout): readable, local, and matches the user's model.
  **Chosen.**

Allocation is in three phases (all integer and deterministic):
1. **Catchments.** A multi-source BFS from all active sources gives every reachable tile its pipe distance and its
   *owner* (nearest source; ties go to the lower source index, FIFO order). Every consumer lot gets the distance
   and owner of its first-reached tile. Sources that meet are in the same **network** (union-find, as today).
2. **Own area first.** For each source, walk its consumers in (distance, lot id) order and serve while
   `use <= left[s]`. The first one that doesn't fit **cuts** that source: everyone further out in its catchment
   stays unserved for now (strict cut, so the boundary is clean, no holes). The source's **own reach** is the distance
   of its last served consumer, and its spare is `left[s]`.
3. **Network spare.** The leftovers of all sources in one network are pooled. Unserved consumers of that network, in
   global (distance, id) order, are served from the pool until it no longer covers the next one (strict cut per
   network). The amount shared out is credited back to the donating sources in proportion to their spare (display
   only).

As a result, dry buildings sit at the **far edge of the overloaded source's catchment**, which is exactly where the
player should build a new pump or well. A lone well on a separate pipe network can't help the town, and its overlay
shows that.

Final per-source **reach** = the distance of the furthest consumer served in its catchment (own or shared). If the
catchment has unserved consumers, the source is **at capacity**.

### 3.6 Visibility (the part the user cares most about)

**A. Water overlay** (button, and automatically with the pipe tool). Buildings are drawn flat, Tab shows them, as with
the other overlays.

| Element | Look |
|---|---|
| Pipe, wet (inside reach) | blue pipe sprite (autotiled, 16 masks) |
| Pipe, connected but beyond reach (short) | red pipe sprite |
| Pipe, not connected to any source | grey pipe sprite |
| Watered building | blue dither on its footprint (`NETC_OK`) |
| Dry building, connected but short | red dither (`NETC_SHORTAGE`) |
| Building with no connection | grey dither (`NETC_NOT_CONNECTED`) |
| Wet ground (empty tile where a building would get water) | faint blue dither (new category `NETC_WET`) |
| Source | its sprite plus a **gauge**: small bar above the building, fill = used/output, green < 80%, amber 80-99%, red = at capacity |

The **reach boundary** is the line where blue meets red. **Hover or select a source** while the overlay is on and its
catchment is highlighted (brighter dither, the rest dimmed). This uses the existing overlay `param`:
`render_set_overlay(OVL_WATER, lot_id, ...)`, and the chunk overlay hash invalidates only the affected chunks.

**B. Source query panel** (click a pump/well/tower):
```
Pumping station                         [at capacity]
Output        440 units  (11 shore tiles)
Used          440  = own area 440, shared 0
Serves        34 buildings, reach 18 tiles
Dry beyond    9 buildings (37 units) - build a pump or well nearby
```
or, with spare:
```
Groundwater well
Output        150 units
Used          96   = own area 61, shared 35 with the network
Spare         54  (about 54 more rintamamiestalo)
Serves        23 buildings, reach 25 tiles (all buildings in its area)
```
Water tower adds: `Pressure zone 16 tiles: 22 apartment buildings (tier 2+ allowed)`.

**C. Building query** (one line, replaces "water / no water"):
- `Water: yes - Pumping station #3, 12 tiles along pipes`
- `Water: no - pipes reach here, but the network is out of water (1,450 used / 1,200 supplied)`
- `Water: no - not connected (no pipe or watered building next to it)`

**D. Top bar / city readout:** `Water 3,400 / 4,000 (85%)`, meaning used / supplied over all networks. Text colour:
normal below 80%, amber 80-100%, red if any building is short. Tooltip: `2 networks - 0 short - 14 not
connected - largest spare: Well #7 (54)`. Clicking it opens the water overlay. The services panel row "Water used /
supplied" stays, now with these exact numbers.

**E. Ticker** (`SE_WATER_SHORTAGE` already exists): `Pumping station at Rantakylä is at capacity: 9 buildings without
water.` The location x,y jumps the camera to the source, not to a random dry lot.

---

## 4. Simulation sketch (fits net.cpp)

### 4.1 Data

- **Pipe bit:** `Tile.road` is full (bits 0-2 class, bit 3 power line, bits 4-7 road mask). Free bits:
  `Tile.flags` bit 6 (`TF_RESERVED6`) and bit 7 (`TF_RESERVED7`), and `Tile.zone` bit 7 (reserved).
  Proposal:
  - `TF_PIPE = 0x40` in `Tile.flags` (keeps `Tile` at 8 bytes; saved with the TILE chunk for free; `world_hash`
    already covers flags).
  - `ZF_WATER_NET = 0x80` in `Tile.zone`: "in a network with a source" (mirrors `ZF_POWER_NET`). Together with
    `ZF_WATERED` (bit 5) this gives the three pipe and lot states: wet = NET+WATERED, short = NET only,
    unconnected = neither.
  - `TF_RESERVED7` stays free (district heating pipes in v1.0, if heating moves to pipes too).
  - The pipe autotile mask is **not stored**. The renderer computes it from the 4 neighbours' `TF_PIPE`, and only in
    the water overlay.
- **Setter:** `world_set_pipe(x, y, on)` notifies a new kind `WCHG_PIPE = 0x100` (kinds are `int` everywhere in
  world.cpp; `WCHG_ALL` becomes `0x1FF`). The renderer treats `WCHG_PIPE` like `WCHG_NET` (overlay check only), so
  the normal view is not re-rendered when pipes change.
- **net.cpp working arrays** (static, as all others):
  `u16 s_wmark[NTILES]` (own generation stamp: must **not** reuse `s_mark`, which holds the power incremental
  state through `s_pgen`), `u16 s_wdist[NTILES]` (pipe distance, `DIST_NONE`), `u8 s_wown[NTILES]` (source index
  into a compact list, `MAX_WSRC = 255`). That is 320 KB at stride 256, which is fine for 128 MB. Per source: `WaterSrc
  { u16 lot; u16 net; s32 output, own_used, shared_used, left; u16 lots, lots_dry, reach, cut; u8 shore_tiles,
  flags; }`. Per lot: `s_lot_wdist`, `s_lot_wsrc` (replace `s_lot_dist`/`s_lot_comp` for water).
- The cell-graph water code goes away (`s_cell_main`, `s_wcell_*`, `s_tcell`, water use of `dilate_cells`).
  Coverage keeps the cell graph.

### 4.2 Solve (pseudo-C, C89 style)

```c
static void solve_water(void)
{
    /* 0. fast path: pipes, conducting lots and sources unchanged -> skip to 3 (allocation only).
          Key = world_struct_serial() (pipe edits must bump it) + a lot-graph serial (lot placed/removed)
          + water_signature() (sources, status, pump power). Tier upgrades change only water_use: fast path. */
    /* 1. seeds: sources in lot id order; skip inactive, and pumps without LF_POWERED (if Q4 = yes).
          Push every footprint tile, dist 0, own = source index. */
    /* 2. FIFO BFS, 4-neighbour, passable = TF_PIPE || (lot && s_def_cond[def]) (|| jump-road if enabled).
          On pop: if tile has a lot not yet seen -> lot dist/own = this tile's. On meeting a tile owned by a
          different source -> uf_meet(net of a, net of b). No distance cap (u16). */
    /* 2b. towers: tower output = 200 only if uf_find(net) contains a pump/well with output > 0.
           Pressure zone: second BFS from active towers over the same graph, cut at 16 (cheap). */
    /* 3. counting sort consumer lots by (dist, id)                                  (existing s_cnt code)
          phase A: per source s: serve while use <= left[s]; first miss -> cut[s] = dist, stop s.
          phase B: pool[net] = sum left[s]; walk the global order again: unserved lot of net n:
                   serve if !cutn[n] && use <= pool[n], else cutn[n] = 1.
          final[s] = min dist of an unserved consumer owned by s (DIST_NONE if none);
          reach[s] = max dist of a served consumer owned by s. */
    /* 4. write bits where changed: lots -> LF_WATERED, ZF_WATERED, ZF_WATER_NET on the footprint;
          pipe tiles -> ZF_WATER_NET if reached and net supply > 0, ZF_WATERED if dist < final[own].
          One world_notify(rect, WCHG_NET) for the changed box. Stats into s_ns[1]. */
}
```
The determinism rules stay as they are: lot id order for seeds, a fixed neighbour order, counting sort with id as tie
breaker, integers only, no floats. `net_water_near(x, y)` becomes the wet-ground check (8 neighbours' `ZF_WATERED`
/ `ZF_WATER_NET`), so it needs no extra storage.

### 4.3 Cost on the P2-233 (128² map)

| Step | Estimate | Basis |
|---|---|---|
| Tile BFS over pipes + conducting lots (about 6-10k tiles in a dense town) | 1.5-2.5 ms | probe: flood 128² ≈ 2.4 ms, road BFS ≈ 0.85 ms |
| Tower pressure BFS (≤ 16 deep) | < 0.3 ms | |
| Counting sort + 2 allocation passes (≤ 4k lots) | 0.3-0.5 ms | today's code does one pass |
| Bit write-back (changed only, full scan of 16k tiles) | 0.5-1 ms | as today's power write-back |
| **Full solve** | **about 3-4 ms** | today's full water solve on a 20k town: 6.2 ms (HANDOFF) |
| Fast path (demand changed only) | about 1 ms | |

**When to re-solve:** dirty on `WCHG_PIPE`, `WCHG_LOT` (placing or removing a lot changes the graph now, not just
the demand), `WCHG_TERRAIN` near a pump (shore tiles), a change in a pump's power state, and monthly. Today's rule of
one solver per `net_step`, alternating with power, stays. Growth places lots every month, so most months take
the full BFS (3-4 ms once a month is fine). Add an incremental "new lot touches exactly one owner" path, like
`solve_power_incremental`, only if the benchmark asks for it.

### 4.4 Interaction with power

- Same conduction table (`s_def_cond`); separate scratch arrays (see 4.1).
- If pumps need power (Q4): the water solve reads the pump lot's `LF_POWERED`. After a power solve,
  `water_signature()` includes each pump's powered bit, so a brownout that hits a pump makes the water solve run again
  on the next turn (`net_step` alternates, so the lag is one sim tick).
- Tier requirements stay as they are: `REQ_WATER` = `LF_WATERED` (lot) or `net_water_near` (empty tile);
  `REQ_WATER_TOWER` = in a tower's pressure zone.

---

## 5. Art and UI needs

**World art (gen_art.py, both zooms):**
- `pipe_<mask>_<state>`: 16 masks (N/E/S/W connection bits, plus mask 0 = a short stub/cap) x 3 states (wet blue,
  short red, dry grey) x 2 zooms = 96 small keyed sprites. A flat 2-px (32 zoom) or 3-px (64 zoom) line from the
  tile centre to each connected edge midpoint, 1-px dark outline, no anti-aliasing against the key, cropped to the
  bounding box. Rough VRAM: about 25 KB at zoom 32 and about 70 KB at zoom 64. Fallback if VRAM is tight: 2 states
  (blue, grey) with "short" shown only by the red dither diamond under it.
- `wgauge_<level>_<col>`: 9 fill levels x 3 colours (green, amber, red), 16x4 px: negligible.
- One new overlay category, `NETC_WET` (faint blue dither diamonds), in the existing 8-level overlay set.
- No new building art: pumping station, well and 2 water tower designs exist.

**UI art (gen_ui.py):** a `pipe` tool icon (the Water group gets 4 entries: pipe, pump, well, tower) and legend swatches
for the 6 overlay states.

**UI code:** the Water group's tool list, cost text "3 mk/tile (15 under water)", the source query panel (3.6 B),
building query line (C), top-bar water readout and tooltip (D), overlay legend, pump placement preview "Output here: N".

**Save format:**
- `TILE` chunk: pipes travel inside `Tile.flags`; bump the chunk version 1 to 2. **Loading a v1 save:** lay
  `TF_PIPE` under every road tile whose class had `RDF_CARRIES_WATER` (gravel, street, main road). Old cities keep
  roughly the same water.
- `NET ` chunk: version 2 with the new per-tile and per-source arrays (or simply don't save them and re-solve on load,
  which the loader already supports: "without it, load falls back to net_solve_all()"). Recommendation: re-solve.
  It is 3-4 ms, and it is deterministic as long as allocation depends only on the world, not on solve history.
  Check that the bit-identical determinism test still passes; if it doesn't, save the per-lot states.

---

## 6. Balance notes

- Early game is unchanged: cottages need no water, and T2 houses need it. The first pipe network costs about
  3 mk x 40 tiles = 120 mk plus a 600 mk pump, a fraction of today's start (25,000).
- The bots in `tests/test_balance.cpp` (planner/greedy/industry) must lay pipes. Simplest rule: a pipe under every
  street they build, plus a pipe from each pump to the nearest street. Then compare `scripts/bal_*` before and after.
- Watch: wells everywhere need no pipes at all (a well touching a block waters the block). That's fine for villages.
  If it becomes the dominant strategy in towns, give wells "shared groundwater" (-25% output per other well within
  6 tiles). Not in the first version.
- The tower changes from +30% to a flat 200 plus a visible pressure zone. Re-check that Apartments T2+ still reach their
  requirement in `town1`.

---

## 7. Migration plan (from mains-in-roads)

| # | Module | Change |
|---|---|---|
| 1 | `src/world.h/.cpp` | `TF_PIPE`, `ZF_WATER_NET`, `world_set_pipe()`, `WCHG_PIPE` (0x100), `WCHG_ALL` 0x1FF; `world_dump` water layer (`net_water_dump`: `-` pipe wet, `x` pipe short, `o` pipe dry, `W` source, `w`/`d` watered/dry lot) for DEBUG FROM TEXT |
| 2 | `src/defs.h/.cpp` | `MiscDef`: `pipe`, `pipe_water`, `pipe_upkeep10`; tower capacity 200; drop `RDF_CARRIES_WATER` from the road table (keep the enum value for the v1 save migration); constants `WATER_SPREAD`, `WATER_JUMP_ROADS`, `TOWER_PRESSURE_REACH`, `TOWER_OUTPUT` |
| 3 | `src/net.h/.cpp` | new `solve_water` (section 4.2); `net_water_source(int lot, WaterSrcInfo *)`; `net_water_tile_state(x, y)`; `net_lot_water_src(lot, &dist)`; `net_water_tower_reach` reimplemented (pressure zone); `net_water_near` = wet ground; remove the cell-graph water code; `s_wmark` separate from `s_mark` |
| 4 | `src/sim.cpp` | world listener: `WCHG_PIPE` → `NET_WATER`, `WCHG_LOT` already marks water; `sim_overlay_value(OVL_WATER, param)`: pipe states, wet ground, highlight of the `param` source's catchment |
| 5 | `src/budget.cpp` | pipe upkeep line (counts from a running tile count, as `world_road_stats` does for lines) |
| 6 | `src/tools.h/.cpp` | `TOOL_PIPE` (L-drag via `tools_apply_line`, preview cost, removal by right-drag); overlay auto-on and restore; bulldozer removes pipes only with the water overlay on |
| 7 | `src/render.cpp` | overlay pass draws `pipe_<mask>_<state>` and source gauges; `WCHG_PIPE` → overlay check, not `CF_DIRTY`; minimap water overlay shows pipes |
| 8 | `src/ui.cpp` | Water group (pipe first), source query panel, building query line, top-bar readout + tooltip, legend, pump output preview, ticker text with source location |
| 9 | `art/` | gen_art.py pipe and gauge sprites + sheet headers; gen_ui.py pipe icon; ART.md entry |
| 10 | `src/save.cpp` | TILE chunk v2; v1 load lays pipes under watered road classes; NET chunk: re-solve |
| 11 | `tests/`, `scripts/` | bots lay pipes; new `test_water`: (a) one pump, a row of houses, demand grows → reach shrinks monotonically; (b) two sources, spill fills the overloaded side; (c) a road gap without a pipe blocks conduction; (d) determinism hash over 3 runs; (e) v1 save loads with water |
| 12 | docs | DESIGN 4.3 / 5.6 / 11.4, HANDOFF (water section), BALANCE (pipe costs, tower), DESIGN_LESSONS 4.5 cross-reference "superseded by WATER.md" |

**Order:** 1-3 + 11 first (headless, testable on the Mac: `tests/Makefile.mac`), then 4-6, then 7-9 (art and UI), and
10 last. The cell-graph code can be deleted once the tests pass.

**Risks:**
- *Chore risk* (DESIGN_LESSONS called the underground layer "the canonical chore"). Mitigations: conduction,
  sources water what they touch, auto overlay, L-drag, cheap pipes, and the optional "pipes with roads" toggle. Watch
  this in the next play-test.
- *Shared scratch:* reusing `s_mark` would silently break the power incremental solver. Use separate arrays.
- *WCHG width:* anything that stores change kinds in a `u8` would drop `WCHG_PIPE`. world.cpp uses `int`; check
  render.cpp, fields.cpp, life.cpp and minimap listeners.
- *Graph churn:* lot placement now dirties the water graph every growth month. Budgeted at 3-4 ms; add the
  incremental path only if needed.
- *Save determinism:* re-solve on load must give the same result as the unbroken run (allocation must not depend
  on history). Verify with the existing round-trip test.
- *Balance drift:* bots and `town1` numbers will move; re-run `scripts/` and update BALANCE.md.
- *Heating (v1.0)* was planned "in roads" like water. Decide whether heat mains share the pipe tool (a second layer,
  `TF_RESERVED7`) or stay in roads.

---

## 8. Open questions for the user

1. **Approve the core rule**: each source waters the nearest buildings along pipes and buildings until its output runs
   out (its reach); spare from other sources on the same network then flows on to the dry buildings. Is that the
   behaviour you meant?
2. **Pipes with new roads:** offer an optional "Lay pipes with new roads" checkbox (default off), or never?
3. **Roads block water** unless a pipe runs under or across them (recommended), or should water jump one road like
   power does (then pipes are only needed to connect sources and distant areas)?
4. **Pumping stations need power** (SC2000 did; wells don't)? Yes is recommended.
5. **Water tower** as a small extra source (200) plus a visible pressure zone (16 pipe tiles) for Apartments T2+, or
   keep the old "+30% to nearby sources"?
6. Later: seasons (a dry-summer event lowering pump output, which would give towers a storage role as in SC2000), or
   keep water static?

---

## 9. Sources

- SimCity 2000 manual (transcription): Water System tool, pumps/pipes/towers/treatment/desalination text:
  https://www.freegameempire.com/games/SimCity-2000/manual ; PDF: https://classicreload.com/sites/default/files/sim-city-2000-manual.pdf
- SC2000 pump formula, tower storage, treatment coverage: https://www.ludo.guide/guide/simcity-2000/water-system
- SC2000 city-centre diamond, water map colours: https://www.ludo.guide/guide/simcity-2000/water-supply ,
  https://www.ludo.guide/guide/simcity-2000/water-system-2
- SC2000 pipes ($3, underground view, blue = flowing): https://www.ludo.guide/guide/simcity-2000/pipes
- SC2000 strategy (blue/black pipes, centre-outward allocation, droughts, towers): "Zen and the Art of SimCity 2000",
  http://www.ibiblio.org/GameBytes/issue18/misc/zen.html
- SC2000 local plumbing vs mains, towers (comp.sys.mac.games): https://groups.google.com/g/comp.sys.mac.games/c/KKNZuiGbNgo
- SC2000 pumps and phantom pump: https://patcoston.com/co/STRAT2.aspx , https://patcoston.com/co/HANDWATR.aspx
- Zones conduct water when developed (forum): https://gog.com/forum/simcity_series/water_pipes
- SC3000 7-tile pipe radius (1999 review): https://gamesfirst.com/articles/1999/sim-city-3000/ ; SC3000 values:
  https://www.ludo.guide/guide/simcity-3000/utilities , https://www.ludo.guide/guide/simcity-3000/water
- SC4 pipe radius, shrinking distribution radius, pump/tower values: https://strategywiki.org/wiki/SimCity_4/Water
  (read via search excerpts; the page is behind a bot check) ; forum spacing and "double coverage":
  https://steamcommunity.com/app/24780/discussions/0/846940248126628637 ; polluted pumps shut down:
  https://steamcommunity.com/app/24780/discussions/0/541907675761198695
- Cities: Skylines pump capacity and nominal-capacity complaint, info view bars:
  https://steamcommunity.com/sharedfiles/filedetails/comments/1115489024 ,
  https://steamcommunity.com/app/255710/discussions/0/611702631214533285
- Water new in SC2000 (not in SimCity 1989): https://en.wikibooks.org/wiki/SimCity/SimCity_2000 ,
  https://simple.wikipedia.com/wiki/SimCity_2000
