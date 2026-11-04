# Project Lähiö (working title KAUPUNKI): Milestone 2 tasks ("Presidentti")

Spec: `docs/DESIGN.md` (**section 14 = rev 2, wins over older sections**), `docs/research/WATER.md`,
`docs/research/INDUSTRY.md`, `docs/BALANCE.md`, `docs/PERF.md`, `docs/HANDOFF.md`. Orchestrator: session 86box-86.
The general rules of `docs/TASKS.md` ("Rules for every task") apply unchanged: own files, VM lock, VC6 rules,
**debug from text**, report format. Milestone 2 adds:

- **Shared files** (`sim.cpp`, `defs.cpp`, `defs.h`, `ui.cpp`, `tools.cpp`, `game.cpp`, `save.cpp`, `Makefile`,
  `tests/Makefile.mac`) are edited by several tasks. Rules: put new systems in **new files** you own (e.g. `trade.cpp`,
  `industry.cpp`, `ui_pol.cpp`); touch shared files only with small, targeted edits (hooks, table rows appended at
  the end of a table/enum); **re-read a shared file right before each edit**; never reformat; run the **full** Mac test
  suite and the VC6 build before finishing.
- Determinism and save round-trip tests must stay green. New state needs a save chunk (save.h API) or deterministic
  recompute on load. Bump chunk versions; old saves should still load where cheap.
- Balance: keep `tests/test_balance.cpp` green or update its milestones with a comment explaining why.
- VRAM: DESIGN 14.5 (only the active zoom resident; per-zoom budget check in the art pipeline).
- **lahio (was kaupunki) uses its own VM: "VM B" (2026-11-03).** Use only `~/Documents/86box/samba/vmb/` wrappers: `vmb/w98 ...`,
  `vmb/vmlock wait|acquire|release|touch|status <task> ...`, `vmb/shot86.sh`, `vmb/vnc98.sh` (Mac ports 9899/5999). Never the
  plain `samba/w98`/`vmlock`/`shot86.sh` (VM A = SIROCCO). Same M: share. B down? See CLAUDE.md section 7.
- **VM rule (WIN98-LINK.md, 2026-11-03):** while anyone else holds the VM lock, run **no agent98 work at all**,
  including `w98 build` and headless `-fast` runs (they make others' benchmarks/play-tests stutter). Take the lock
  for builds and headless runs too (`vmlock wait <task> ...` → work → `vmlock release <task>`) unless it is FREE or yours.

- **Renamed (R1, 2026-11-03):** folder `dev/lahio` (`M:\dev\lahio`), exe `lahio.exe` (`vmb/w98 build lahio`), `LAHIO.INI`
  `[Lahio]`, `lahio.log`, Mac test binary `tests/build/lahio_tests`. Saves keep `KAUP` magic and `*.KAU`.

Model/effort (user rule): **H+** = Opus, high effort; **H** = high; **M** = medium.

```
Wave F  (fix round):     F1 water pipes + conduction (H+)  |  F2 art: pipes, yards, UI bits (H+)
                         -> F3 UX fixes + residency + yard growth (H)            [after F1]
Wave M1 (parallel):      M1 politics core + UI (H+) | M2 traffic congestion, buses, station (H+) |
                         M3 industry kinds, forestry & farm zones (H+) | M4 art: Milestone-2 buildings (H+)
Wave M2 (parallel):      M5 services & housing: element factory, apartments T2-3, C/I T3, police/crime, library,
                         post & phone, health centre, district heating, sewage, fires (H) |
                         M6 complexes: paper mill + patruuna, Valtion Metalli, company town (H+) |
                         M7 trade & eras: clearing/Western trade, oil shock, loans, town status decrees, newspaper,
                         graphs, landmarks/money sinks (H+)
Wave M3:                 M8 balance (bots with politics/industry) (H) -> M9 performance (H+) -> M10 polish +
                         end-to-end VM script runs (H+)  -> USER PLAY-TEST
```

## F1 Water pipes + conduction (H+)
Owns: water parts of `net.*`, pipe tool in `tools.*`, water overlay values, water readouts in ui (small edits),
`tests/test_water*.cpp`. Implement WATER.md sections 2-3 and 7 (migration): pipe bit storage (WATER.md 3), pipe tool
with preview/cost/undo, conduction (buildings + zoned-empty tiles; roads block unless piped; power: zoned-empty tiles
conduct too, DESIGN 14.2), sources with reach/nearest-first/spare-flows-on, pumps need power, water tower source +
pressure zone (Apartments T2+ requirement), checklist reasons, overlay (wet/short/dry pipes; watered/dry lots; future
lots), source query + city readout, "lay pipes with new roads" option, save chunk + old-save migration (pipes under
roads), bots in `test_balance.cpp` learn to lay pipes (keep milestones), Mac tests (reach shrinks with load, furthest
dry first, determinism, save round-trip), VM `-script` check. Art from F2 (until then use placeholder names; art.h rules).

## F2 Art: pipes, yards, UI bits (H+)
Owns: `art/gen_art.py` + art modules, world sheets, `art/art_world.h`, `art/ART.md`; `art/gen_ui.py` for new icons.
Pipes `pipe_<MM>_<wet|short|dry>` (overlay style, both zooms, tiny), 1x1 industrial yards (3+ variants, rotations,
lot ground), zone outline/tint pieces for the persistent zone view, icons: pipe tool, zoom in/out buttons, zone-view
toggle. **Per-zoom VRAM report** (DESIGN 14.5) and `--check`.

## F3 UX fixes + residency + yard growth (H) — after F1
Owns: small edits in ui/tools/game/life/menu/art/sim/defs. DESIGN 14.4 list (zone toggle, tool switching bug, zoom
keys/buttons, hints, hover labels incl. town hall, upkeep index in budget, car floor ∝ car ownership, windowed pause
menu VRAM), DESIGN 14.5 active-zoom residency at all resolutions, 1x1 yard growth rule (only where no 2x2 fits).

## M1 Politics core + UI (H+)
New `politics.cpp` (+ `ui_pol.cpp`): DESIGN 6 (blocs computed from the city, popularity, authority, ≤ 5 active
decrees with cooldowns/repeal, decree table changing sim parameters, Letters, event cards with 2-3 choices and
explicit effects waiting in the ticker, elections every 6 years with margin, emergency powers law, Eastern/Western
meters). Politics toggle in New Game works. Panels: politics overview, decrees, event card, election night. Text in
the period tone (THEME 6). Save chunk. Mac tests + bot runs with politics on.

## M2 Traffic congestion, buses, station (H+)
`traffic.cpp` v1.0: road graph, block O/D, cluster Dijkstra with BPR cost, gravity split, MSA smoothing
(DESIGN 5.8, DESIGN_LESSONS 3.5), time-sliced; main road/motorway semantics; road query "flow/capacity, top sources";
congestion → commute → happiness/land value; bus line tool + stops + mode split by car ownership; railway station
(regional migrants, freight raises I cap). Cars follow congestion. Save chunk. Perf budget on the P2.

## M3 Industry kinds, forestry & farm zones (H+)
New `industry.cpp`: INDUSTRY.md (kind choice by site + kind demand, wood/milk supply numbers, forestry rotation +
regrowth, farm tiers, exports split West/East feeding DESIGN 6 trade terms), query explanations, overlays (wood
supply, farm). defs rows + art names from M4 (placeholders until then).

## M4 Art: Milestone-2 buildings (H+)
All M2 sprites within the per-zoom budget: apartments T2-T3 (+ element factory), commercial T3, industry kinds and
T3, forestry/farm lots, paper mill complex pieces (5x5, fixed orientation), Valtion Metalli, police, library, post &
telephone, health centre, heat plant, sewage works, railway station, bus + bus stop, fire frames, landmarks (concert
hall, TV tower, ski jump, presidential sauna, friendship statue), patron gift buildings (row houses, company church).
Fictional signage only. Preview sheets + town mockup with M2 content.

## Git
The orchestrator commits and pushes (public repo HenriVSL/project-lahio). Agents don't run git commands.

## M5 Services & housing (H+)
Element factory (precast output caps Apartments T2+ growth per month), Apartments T2-T3 and Commercial T3 (ART.md 3.1
keys), police + crime field, library, post & telephone (phone queue satire; houses T3 needs it again: `s_nreq_house[2]`),
health centre (1972), **district heating** (heat plant; heat reaches lots over the existing water pipe network,
capacity-limited nearest-first like water; Apartments T3 requirement), **sewage works** (residents above sewage
capacity pollute lakes: land value/happiness penalty near polluted water), **fires** (fire risk → fire events in
wooden districts, spread to wooden neighbours, fire coverage puts them out; fire0-3 art; rubble), checklist reasons,
overlays (crime, heat, sewage), save chunks, tests, balance kept.

## M6 Complexes (H+)
Paper mill complex (INDUSTRY.md: 5x5 from pm_* modules, fixed orientation, on rapids/lake, 5,000 mk, staged 300 → 900
jobs, wood need via `industry_set_ext_wood`), the *patruuna* (mood from town relations; gifts the player places: row
houses, company church, sauna, school; demand event cards via `politics_queue_event`; company-town rule ≥ 40% of jobs →
company pays 20% of school/clinic/fire upkeep; telegraphed risk events: paper slump, strike, 1970s layoffs, 1980s
merger), *Valtion Metalli* (vm_* modules) placed via a hidden decree (`politics_enable_hook_decree`) or a Market-town
unlock with politics off, tied to the clearing quota (M7 hook), output halves in 1990. Fictional names. Save, tests.

## M7 Trade & eras (H+)
Clearing trade (5-year agreements with goods lists filled by `industry_kinds()` east exports; clearing balance near
zero; surplus wasted / deficit lowers Eastern relations; oil for oil/heat plants bought with clearing credit; **1973 oil
shock**), Western trade (hard currency; orders by `export_west`), loans (domestic bond, Western bank loan, Eastern
credit with strings), **town status by decree** (`politics_status_ceremony`, DESIGN 7 ladder + unlocks), eras
1955-1990 with era events (1963 new markka display, 1990 end of clearing trade + end-of-era newsreel), **newspaper**
each January (3 generated headlines + satire; masthead art exists), **graphs panel** (history), landmarks/money sinks
(concert hall, TV tower, ski jump, presidential sauna with sauna diplomacy, friendship statue). Oil plant may reuse
coal-plant art (alias); peat/nuclear only if art exists, else later. Save, tests, balance.

## M8-M10
Written by the orchestrator after M5-M7 (balance with all systems, performance incl. 1024 64-zoom art layout, polish +
end-to-end VM runs, then the user play-test).

## Polish wave (after user play-test 2, 2026-11-04) — inputs: docs/PLAYTEST.md session 2 (items 1-11), SCOPE_REVIEW 3
Reference save from the user's play-test: `PT2_1992.KAU` (Mustakangas, 1992, Town, 12,933 pop).
- **PL1 Guidance & information (H+):** newspaper as advisor (items 8, 10: Cabinet box, special editions, hindsight,
  1990 verdict, clickable lines), progression visibility (item 1: next status + progress, decree-waiting §, unlock tooltips
  with population/year), milestone hint chain + one-page Guide panel (item 2), power top-bar field + plant query + Services
  key (items 3, 5, 6), heating purpose explained (item 4). Keep the dry humour (item 11).
- **PL2 Gameplay fixes & balance (H+):** apartment T2 gate (item 7a), railway on every map + road fallback for Valtion
  Metalli/station (item 9), human-pacing check (item 7c), dense towns too rich, agreement auto-sign near elections,
  company-town reachability, debt traps.
- **PL4 Performance & art layout (H+):** 1024x768 64-zoom art (sheet layout), water graph 7.3 ms, traffic graph 12 ms,
  phase 9 15 ms spikes, VRAM headroom at 800.
- **PL3 Bugs & UX (H), after PL1:** hover labels over the newspaper, windowed pause menu black panels, heat overlay noise
  on empty zoned land, title "Milestone 1" text → version, motorway art/hide, undo gaps (bus lines, gifts) if cheap,
  remaining items from HANDOFF "Open".
Then: final verification on VM B, commit, morning report for the user.
