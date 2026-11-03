# KAUPUNKI: implementation tasks (Milestone 1: vertical slice)

Read `docs/DESIGN.md` first (section numbers below refer to it). Each task is one focused agent session.
Orchestrator: session 86box-86. Milestone 2 tasks are written after Milestone 1 is playable.

## Rules for every task

- Project: `~/Documents/86box/Shared/dev/lahio/` (= `M:\dev\lahio`). Never edit other projects
  (`dev/sandline`, `dev/runner`, ...); **reading and copying** their code is fine (credit the source in a comment).
  `probe/` and `art/explore/` are frozen reference: read, don't edit.
- Edit only the files your task **owns**. Need a change in a file owned by another task? Describe it in your
  report. Adding functions/fields at the **end** of a header you don't own is allowed only if the task text says
  so; changing an existing signature needs orchestrator approval.
- Build: `~/Documents/86box/samba/vmb/w98 build lahio` (no lock needed). `lahio.exe` must not be running when you
  build (a running exe can't be relinked).
- **Portable simulation code** (`world`, `defs`, `sim`, `net`, `fields`, `budget`, `mapgen`, `traffic` logic, `save`)
  must not include `<windows.h>` and must also compile on the Mac with
  `clang++ -std=c++98 -Wall -Wno-... ` via `tests/Makefile.mac`. Unit tests in `tests/` run on the Mac in seconds:
  use them heavily. VC6 remains the real target: also build it there.
- Anything that opens a window in the VM (running the game, screenshots, VNC) needs the VM lock:
  `~/Documents/86box/samba/vmb/vmlock wait <task-id> "what" 1800`, and `vmlock release <task-id>` right after the batch.
  Other sessions use the VM too: batch runs, keep lock periods short. Before releasing: the exe has exited and the
  desktop is restored.
- Prefer self-terminating runs: `vmb/w98 run 'M:\dev\lahio\lahio.exe -seed 1 -shot 3 shot.bmp -exitafter 4' --show -t 60`,
  then `sips -s format png shot.bmp --out <scratch>/x.png` and look at it. No VNC during fps measurements.
- VC6 rules: DESIGN 11.8. `/W3` clean in your files.
- **Debug from text, not images** (user rule). Logic, balance, simulation, performance and tool behaviour are
  verified with text output: `-log` files, headless Mac tests, `-simtest` style runs that print/CSV-dump stats per
  month (population, demand terms, budget lines, coverage, timings), `bench.txt`. Screenshots are for **visual**
  questions only (art, layout, sorting, UI look), and even then add a text dump when possible (e.g. the chunk/tile
  under a glitch).
- Art quality is a top user priority: for art and rendering tasks, look at preview PNGs/screenshots critically and iterate.
- Minimum spec is the P2-233 VM; real targets (ThinkPad X41/T40) are GHz-class. 60 fps target with automatic 30 fps
  fallback (DESIGN 1).
- **Report** (final message): what you built, how you verified it (commands + results), deviations from DESIGN,
  open issues, and notes for later tasks. The orchestrator copies the notes to `docs/HANDOFF.md`.

Model/effort (user rule): **Opus** for difficult tasks (marked **H+**), effort **high** for most (**H**), **medium**
for easy ones (**M**).

## Dependency graph and waves

```
Wave 0:  T00 interfaces + skeleton (orchestrator-reviewed headers, Makefiles, test harness)
Wave 1 (parallel):  T01 engine base | T02 world art pipeline | T03 UI art + font | T04 simulation core
Wave 2 (parallel):  T05 renderer (T01,T02,T04) | T06 map generator (T04) | T07 sound (T01)
Wave 3 (parallel):  T08 tools + game integration = VERTICAL SLICE (T04,T05,T06) | T09 UI module (T01,T03,T04)
Wave 4 (parallel):  T10 menus, new game, save/load (T08,T09) | T11 cosmetic life: cars, smoke, shimmer (T05,T08)
Wave 5:             T12 balance & feel (all) -> T13 performance pass + bench (all)
```

---

## T00 Interfaces and skeleton (H+)
Owns: `src/common.h`, all `src/*.h` initial versions, `Makefile`, `tests/Makefile.mac`, `tests/test_main.cpp`,
`src/*.cpp` stubs.
- Write every module header from DESIGN 11.6 with documented functions, structs (DESIGN 5.2) and ownership comments
  (`Owner: Txx`). Key contracts: `world.h` (Tile/Lot, placement/removal, dirty-rect callback for the renderer),
  `defs.h` (BuildingDef, road classes, services, eras), `art.h` (sprite lookup by name/id per zoom, anchors),
  `render.h` (chunk cache API: init, set zoom, invalidate rect, draw frame), `sim.h` (step, speed, query results incl.
  the requirement checklist), `tools.h`, `ui.h`.
- Stub `.cpp` files so `vmb/w98 build lahio` links a `lahio.exe` that opens and exits; `tests/Makefile.mac` builds
  and runs an empty test suite on the Mac.
- Deliver a short `docs/ARCH.md`: module diagram, data flow per frame, ownership table.
- The orchestrator reviews the headers before wave 1 starts.

## T01 Engine base (H+)
Owns: `src/plat.*`, `src/gfx.*`, `src/config.*`, `src/util.*`, `src/main.cpp` (loop part).
- Port SIROCCO's `plat`, `gfx`, `config`, `util` (`../sandline/src/`) to our names and modes: fullscreen
  800x600x16 default, `-res 640|800|1024`, `-window`, `-gdi` fallback, 565/555, lost-surface restore.
- Atlas images up to 640 wide, any height, in VRAM; VRAM usage logging; fail loudly if VRAM runs out.
- Main loop per DESIGN 11.3 step 4/6: issue blits → sim slice callback with a time budget → Flip; 60 fps cap with QPC
  (and Flip on every 2nd blank where the refresh is 120 Hz); `prof_*` sections; `-bench N` writes `bench.txt`
  (avg/max frame ms per section, worst frames); `-shot T file`, `-exitafter T`, `-log`, `-fast`.
- Input: mouse (position, buttons, wheel, drag), keys with repeat, edge-scroll helper.
- Test scene: a 64x32 grid of placeholder diamonds scrolling at 60 fps.

## T02 World art pipeline (H+)
Owns: `art/gen_art.py`, world `art/*.bmp`, `art/sheet_*.h`, **`art/art_world.h`** (index header, convention in `src/art.h`), `art/ART.md`.
- Production generator from `art/explore/gen_explore.py`, style C only, both zooms, everything DESIGN 9 needs for
  Milestone 1:
  - terrain: grass ×3, forest lots ×4 species, rock, water + **2-3 shimmer frames**, complete shore autotile (all corner
    combos, no pinch artefacts), fields;
  - roads: gravel and street, full autotile (straight, curves, T, cross, **dead ends**), **bridges**, road+power crossing;
  - power line: straight, corners, T, cross, pole-on-lot;
  - Houses T1-T3 (≥ 6 colour/porch variants for T2-T3, all 4 rotations), Apartments T1 (3x1 or 2x2; ≥ 3 variants, rotations),
    Apartments T2 (for Milestone 2, if VRAM allows), Commercial T1-T2, Industry T1-T2, coal plant, pumping station,
    well, water tower (2 designs), fire station, school, clinic, public sauna, park, town hall;
  - per building: **construction stage**, **abandoned variant** (style pass: desaturate, dark windows/boards),
    **`lot_ground`** footprint sprite (for overlay mode);
  - cars (existing 9 colours × 4 dirs), chimney smoke puff frames, overlay **checker-dither diamonds** (8 levels × a
    few ramps), cursor/selection diamonds (valid/invalid), zone tint diamonds.
- `--check`: BMP format, widths ≤ 640, key purity, seamless tiling, **total VRAM of all sheets ≤ 9 MB** (print it).
- `ART.md`: sprite naming, header format, anchors, how to add a building. Preview PNGs for review.

## T03 UI art + font (H)
Owns: `art/gen_ui.py`, `art/ui_*.bmp`, `art/font_*.bmp`, `art/ui_*.h`, **`art/art_ui.h`**, `art/font_*.h`, `art/UI.md`.
- Teak & bakelite frame (mockup_C), for 800x600 (default), 640x480 and 1024x768: top bar, right tool panel, ticker
  strip, panel window frame (bakelite border, paper interior), buttons (normal/hover/pressed/disabled), fly-out
  backgrounds, tooltips, scrollbars, sliders (tax), checkboxes.
- Icons 13x13 ink pictograms for every tool and overlay in DESIGN 4 and 8.1.
- **Fonts**: proportional body font (7 px caps, ä ö å Ä Ö Å, § mk ✔ ✘, digits, punctuation) in dark ink and light
  variants; a larger headline font (newspaper/panel titles). Glyph atlas + metrics header.
- President caricature portrait (plaque size + panel size; 3-4 expressions). Newspaper masthead.

## T04 Simulation core (H+)
Owns: `src/world.*`, `src/defs.*`, `src/sim.*`, `src/net.*`, `src/fields.*`, `src/budget.*`, `tests/test_sim*.cpp`.
- Portable, deterministic integer simulation per DESIGN 5: data, phase scheduler, growth/decline with tiers and
  requirement checklist, demand with itemised terms, migration + emigration valve, power/water networks with
  brownout, service coverage BFS with capacity/load, pollution/land value/fire risk fields, budget.
- Data tables (`defs.cpp`) for all Milestone-1 buildings, roads and services, values from DESIGN 4.
- Headless test harness: build a scripted town (roads, zones, plant, pump, services), run N years, assert growth,
  tiers, coverage, budget sanity, determinism (same seed → same hash), timing per phase (log). Runs on the Mac.

## T05 Renderer (H+)
Owns: `src/iso.*`, `src/render.*`, `src/art.*`.
- Chunk cache per DESIGN 11.3: 256x128 VRAM chunks, pool + margin ring + LRU, painter's order with strip slicing,
  ≤ 3 ms chunk-render budget per frame with prefetch, dirty-rect invalidation from `world.h`, both zooms, overlay
  baking (11.4) with flat-building mode, occlusion fix for dynamic sprites, picking masks, camera with smooth scroll
  (whole-pixel camera, CLAUDE.md lesson), minimap image builder (1 px per tile, current overlay).
- Verify against `art/explore/mockup_C.png`-like scenes: screenshots at both zooms; `-bench` scroll test.

## T06 Map generator (H)
Owns: `src/mapgen.*`, `tools/mapgen_preview.py` (optional), `tests/test_mapgen.cpp`.
- DESIGN 3: lakes, optional river with rapids, forests by species, rock, fields, national road (+ optional railway
  strip), presets (Lakeland, River town, Coast, Forest). Deterministic from seed. Validate shores (no undrawable
  pinches), buildable land ≥ 55%, the national road connected edge to edge. Mac test writes preview PNGs/PPMs.

## T07 Sound (M)
Owns: `src/snd.*`, `tools/make_sfx.py`, `sfx/`.
- Port SIROCCO's `snd` (DirectSound, worker-free mixing as there) or waveOut thread; synthesise DESIGN 10 effects;
  ambient town loop with population-driven layers. `-test snd`.

## T08 Tools + game integration: VERTICAL SLICE (H+)
Owns: `src/tools.*`, `src/game.*` (game state, wiring sim ↔ render ↔ tools), `src/traffic.*` (MVP commute BFS), `tests/test_tools*.cpp`. (T04 works against the traffic stub until then.)
- All Milestone-1 tools with drag previews (cost, rejected tiles, L-path roads, autotile), bulldoze, undo/redo within
  the month, zone painting with the 3-tile road rule, building placement with rotation facing the road.
- Run the simulation in the frame loop (time-sliced), speeds, pause. Keyboard shortcuts (DESIGN 8.2) that don't
  need UI panels.
- Milestone check: a human can build a growing town on a generated map.

## T09 UI module (H)
Owns: `src/ui.*`, `src/text.*`.
- Frame (top bar, tool panel, fly-outs, ticker, minimap), all composed into VRAM cache surfaces only when dirty
  (probe 7). Tooltips with itemised breakdowns (demand, money). Panels: budget (tax sliders, service funding),
  population/demand (top-3 problems), query (lot numbers + requirement checklist), overlays legend. English
  string table with the Finnish flavour words; name generators (THEME 8).

## T10 Menus, new game, save/load (M)
Owns: `src/menu.*`, `src/save.*`, `tests/test_save*.cpp`.
- Title screen, New Game panel (DESIGN 2: president name entry, town name, seed + reroll preview, start year,
  funds, toggles), pause menu, settings (INI), save/load (DESIGN 8.4) with slots, autosave, quicksave.

## T11 Cosmetic life (H)
Owns: `src/life.*` (cars, smoke, water shimmer scheduling).
- Cars (DESIGN 5.8) driven by flow, right-hand lanes, turning at junctions, both zooms, occlusion via the renderer;
  chimney smoke; water shimmer (re-render visible water chunks on a slow timer, within the chunk budget).

## T12 Balance and feel (H)
- Play-test via scripted runs and screenshots: the first 10 minutes (first houses within ~10 s), growth pacing to
  3,000 and 10,000, budget tension, tier upgrades, emigration valve. Tune `defs.cpp` constants (with T04's tests
  kept green). Write `docs/BALANCE.md`.

## T13 Performance pass (H+)
- `-bench` city at 20k population, both zooms, 800x600 and 1024x768: no frame > 20 ms while scrolling, speed 3 keeps
  up. Profile and fix. Update `docs/PERF.md`.
