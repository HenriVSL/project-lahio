# KAUPUNKI: architecture

Written by T00 (interfaces + skeleton). `docs/DESIGN.md` is the spec; this file explains how the code is cut
into modules, how data flows through a frame, who owns which files, and the conventions every task follows.
The headers in `src/*.h` are the contract: each one starts with a comment block that documents the module.

## 1. Module map

```
                         main.cpp (T01): WinMain, frame loop, -bench/-shot/-exitafter/-simtest
                                   |
                                game (T08): states, wiring, events -> ui/snd/tools
             +----------+---------+-----------+-------------+------------+
             |          |         |           |             |            |
           menu       ui/text   tools       life         render        snd
           (T10)      (T09)     (T08)       (T11)        (T05)         (T07)
             |          |  \      |           |          /   |   \
             |          |   \     |           +---------+   art  iso
             |          |    \    |                     |   (T05)(T05)
   ------ PORTABLE line (no windows.h; compiles on the Mac with clang -std=c++98) -------------
             |          |      \  |                     |
            save      text      sim (T04) --- net, fields, budget (T04), traffic (T08), politics (M2)
            (T10)     (T09)      |
                                 world (T04) <--- mapgen (T06)        defs (T04): static tables
                                 |
                                util (T01): log, time, prof, rng, hash, strings      common.h (T00)
   Windows-side engine base: plat (window, DirectDraw, pacing, input), gfx (blits, images, VRAM targets),
   config (INI + command line) - T01, ported from ../sandline/src.
```

Rules of the arrows:
- **Portable modules never include Windows-side headers.** Portable: `common util world defs mapgen sim net
  fields budget traffic politics save text tools iso` (`tests/Makefile.mac` compiles exactly these).
  Windows side: `plat gfx config snd art render ui menu game life main`.
- **The simulation never calls render, ui or snd.** It reports through (a) world change notifications
  (`world_add_listener`, section 4) and (b) the `SimEvent` queue (`sim_poll_event`), which game.cpp drains.
- **The UI never changes the simulation.** It reads sim/budget/tools state and returns `UiCmd`s that game.cpp
  executes. game.cpp is the single place where input, tools, sim, render, ui, snd and menu are wired.
- Tools change the world only through `world_*` setters inside `world_batch_begin/end` and pay through
  `budget_spend`, so preview, undo and scripted use (`tools_apply_*`) share one code path.

## 2. Frame (DESIGN 11.3 step 4, 11.5)

```
main loop (T01)                        what happens                                      profiling
--------------------------------------------------------------------------------------------------------
plat_pump(); input_poll()              Windows messages, mouse/keys snapshot             PROF_INPUT
game_update(dt)                        ui_input -> UiCmds; tools_update(ToolInput from   PROF_TOOLS
                                       cam_pick) -> preview/commit; hotkeys; cam_update;
                                       drain sim_poll_event -> ticker/snd/undo-clear;
                                       life_update(dt)
game_draw()                            menu screens, or in play:
   render_update(~3 ms)                  repair dirty/missing chunks (VRAM targets)      PROF_CACHE
   render_world()                        ~25-35 opaque chunk blits                       PROF_WORLD
   life_draw(), tool preview marks       render_sprite / render_tile_mark submissions
   render_flush_sprites()                depth sort, blits, occlusion fix                PROF_SPRITES
   ui_draw()                             2-3 cache blits + tooltip + floating numbers    PROF_UI
game_sim_slice(dt, budget)             sim_frame: whole sim steps while time remains     PROF_SIM
                                       (the blitter executes the queued blits meanwhile)
plat_present()                         Flip, wait for the 60 fps (or 30 fps) deadline    PROF_PRESENT
```

- All blits are **issued before** the simulation slice: the emulated blitter is asynchronous, so the CPU work
  between the last blit and Flip is nearly free (probe 9). `budget = plat_frame_budget_ms() - margin`.
- `sim_frame` always runs at least one due step, so the sim advances even on slow frames; if a speed cannot
  keep up the game runs slower, never skips work (DESIGN 5.1). 30 fps fallback is plat's job (DESIGN 1).
- No allocation per frame anywhere. Every per-frame list (sprites, preview tiles, events, ui commands) is a
  fixed array with a documented cap; overflow is dropped and counted, never reallocated.

## 3. Simulation step (DESIGN 5.3)

One `sim_step()` = one phase; 16 steps a cycle, 4 cycles a month. sim.cpp owns the schedule and calls:

| Phase | Call | Owner |
|---|---|---|
| 0 | date, event bookkeeping | T04 |
| 1-8 | growth scan of 1/8 of the rows (writes lots via `world_lot_*` setters inside a world batch) | T04 |
| 9 | `net_step()` (power + water when dirty or monthly) | T04 |
| 10 | `fields_step_pollution()` | T04 |
| 11 | `fields_step_land_value(cycle)` | T04 |
| 12 | `fields_step_social(cycle)` | T04 |
| 13 | `net_coverage_step(service round robin)` | T04 |
| 14 | `traffic_step()` (bounded slice) | T08 |
| 15 | demand, migration, emigration; month end: `budget_month_end`, `politics_month`, town status, history, stat-log row, `SE_MONTH`/`SE_YEAR` events | T04 |

Deterministic integer code; the sim RNG lives in sim state and in the save. The wall clock only decides how
many steps run per frame. Cosmetic modules (life, ui) use their own `Rng`.

## 4. World changes and the renderer

`world.h` setters call listeners synchronously with an inclusive **tile** rect and a kind mask
(`WCHG_TERRAIN/ROAD/POWER_LINE/ZONE/LOT/NET/FIELDS/FLAGS`). Batches (`world_batch_begin/end`) merge a whole
tool commit or growth slice into one rect per flush. Listeners:
- **render**: converts the rect with `iso_tile_rect_bounds` (footprint diamonds + `ISO_MAX_SPRITE_UP` for tall
  sprites, which covers the old and the new sprite) and marks every touched chunk dirty; NET/FIELDS only matter
  while an overlay showing them is active.
- **sim**: marks networks/coverage dirty (`net_mark_dirty`).
- **ui**: minimap dirty (rebuilt at most once a second; `g_world.serial` is the cheap "anything changed" test).
- **life**: despawns cars on removed roads.
Listeners only record; they never modify the world.

## 5. Art contract (T02/T03 -> T05)

Full text: `src/art.h`. Summary: each generator emits one index header (`art/art_world.h`, `art/art_ui.h`)
of `ArtSheet`/`ArtSpr`/`ArtFont` data; only art.cpp includes them. Sprites are looked up **by name** at init
(`art_find`, `art_lot_sprite`), so regenerating art never breaks compilation; a missing name logs once and
draws a magenta placeholder; `-test art` lists every name the code needs that the sheets lack. Building sprite
stems are the `BuildingDef.key` strings in `defs.cpp` (`house_t2_v3_r1`, `coal_plant_v0_r0`, ...); road pieces
are `<RoadDef.key>_<mask>`; overlays `ovl_<ramp>_<level>`. Anchors are the south vertex of the footprint.

## 6. Ownership

Edit only files your task owns (TASKS.md rules). Need a change elsewhere? Put it in your report.

| Task | Files |
|---|---|
| T00 | `src/common.h`, initial versions of all `src/*.h` and stub `src/*.cpp`, `Makefile`, `tests/Makefile.mac`, `tests/test.h`, `tests/test_main.cpp`, `docs/ARCH.md` |
| T01 engine base | `src/plat.*`, `src/gfx.*`, `src/config.*`, `src/util.*`, `src/main.cpp` |
| T02 world art | `art/gen_art.py`, world `art/*.bmp`, `art/sheet_*.h`, `art/art_world.h`, `art/ART.md` |
| T03 UI art + font | `art/gen_ui.py`, `art/ui_*.bmp`, `art/font_*.bmp`, `art/ui_*.h`, `art/font_*.h`, `art/art_ui.h`, `art/UI.md` |
| T04 simulation core | `src/world.*`, `src/defs.*`, `src/sim.*`, `src/net.*`, `src/fields.*`, `src/budget.*`, `tests/test_sim*.cpp` |
| T05 renderer | `src/iso.*`, `src/render.*`, `src/art.*` |
| T06 map generator | `src/mapgen.*`, `tests/test_mapgen.cpp`, `tools/mapgen_preview.py` |
| T07 sound | `src/snd.*`, `tools/make_sfx.py`, `sfx/` |
| T08 tools + integration | `src/tools.*`, `src/game.*`, `src/traffic.*`, `tests/test_tools*.cpp` |
| T09 UI module | `src/ui.*`, `src/text.*` |
| T10 menus + save | `src/menu.*`, `src/save.*`, `tests/test_save*.cpp` |
| T11 cosmetic life | `src/life.*` |
| Milestone 2 | `src/politics.*` (placeholder now) |

The owner of a module may add functions and fields to its header; **changing or removing an existing
signature/field needs orchestrator approval** (other tasks code against it). Adding a module: add it to
`OBJS` in `Makefile` and, if portable, to `PORTABLE` in `tests/Makefile.mac` (tell the orchestrator).

## 7. Conventions

- **Language**: VC6 C-style C++ (common.h header comment, DESIGN 11.8). Free functions + structs, no classes
  with behaviour, no STL/exceptions/templates/RTTI. `/W3` clean on VC6 and `-Wall` clean on clang.
- **Naming**: functions `module_verb_noun` (`world_set_road`, `render_flush_sprites`); one global per module
  where the header says so (`g_world`, `g_budget`, `g_cam`, `g_cfg`), file statics `s_name`; enums and macros
  `UPPER_CASE` with a module prefix (`TER_`, `ROAD_`, `B_`, `SVC_`, `OVL_`, `WCHG_`, `SE_`, `UC_`...);
  `*_COUNT` ends every id enum. Ids in save files are append-only.
- **Memory**: fixed-size arrays sized by `common.h` constants (`MAP_MAX_*`, `MAX_LOTS`, caps per header).
  Allocate only at init; nothing in per-frame or per-step code. Heap budget 32 MB (probe 9).
- **Integers**: the simulation is integer-only (DESIGN 5.1). Money is whole markka `Money` (s32).
  Tile index `TILE_IDX(x, y)` with stride 256; loops use `g_world.w/h`.
- **Errors**: functions return 0/NULL/`*_MISSING` on failure and log why; nothing aborts. Fatal init errors
  (no DirectDraw, VRAM full while loading art) return 0 up to main, which shows one MessageBox and exits.
  Getters never return NULL where the header says so (placeholder objects instead).
- **Strings**: English, Latin-1 escapes for a/o-umlaut and a-ring (`text.h`), `str_printf` instead of
  snprintf. Display text comes from `text.cpp` or the defs tables, never assembled in render code.
- **Logging** (`util.h`): `log_printf("module: ...")`, one line per event, `warning: `/`error: ` prefixes,
  flushed per line, only when `-log` (or in tests, `tests/out/tests.log`). Never per frame or per tile.

## 8. Debugging from text (user rule)

- **Mac tests** (`make -f tests/Makefile.mac [T=filter]`): `tests/test_*.cpp`, runner in `tests/test_main.cpp`.
  Build towns with `world_*`/`tools_apply_*`, run `sim_run_months`, assert numbers. Write dumps into
  `tests/out/`: `world_dump` (ASCII map with legend), `world_dump_tile`, `sim_dump_report`, stat CSV.
- **Stat log**: `sim_statlog_open(path)` writes a CSV header and one row per month: date, `CityStats`,
  demand values and every demand term, last month's budget lines, per-phase timing avg/max. Used by the Mac
  tests, by `-simtest N` (headless, `game_simtest`) and by `-statlog FILE` during normal play.
- **Reports**: `sim_dump_report(FILE*)`, `mapgen_report()`, `render_stats()` and `render_dump_at()` (what is
  drawn under a screen pixel, for sorting glitches), `bench.txt` from `-bench N` (per PROF_* section avg/max,
  worst frames, blits per frame, VRAM).
- **Determinism**: `world_hash()` / `sim_hash()`: same seed + same scripted actions = same hash.

## 9. Build and test

| What | Command |
|---|---|
| Windows build (VC6 in the VM, no lock needed; `lahio.exe` must not be running) | `~/Documents/86box/samba/vmb/w98 build lahio` |
| Mac tests (portable modules + `tests/*.cpp`) | `make -f tests/Makefile.mac` from the project root |
| Run in the VM (needs the VM lock) | `vmb/w98 run 'M:\dev\lahio\lahio.exe -log -exitafter 4' --show -t 60` |

The T00 skeleton links; the exe (then `kaupunki.exe`, now `lahio.exe`) initialises config/log and the portable modules and exits immediately
(no window until T01). Generated art headers will live in `art/` (`/Iart` is already on the compile line).
