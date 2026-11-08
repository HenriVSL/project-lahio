# KAUPUNKI handoff notes

Collected by the orchestrator from each task agent's final report. DESIGN.md is the spec; this file adds what was
learned while building. Append your own section when you finish a task.

## Phase 1 (research, probe, art exploration)
- Probe (`probe/RESULTS.md`): 800x600x16 fullscreen default; full redraw not viable; 256x128 VRAM chunk cache hits
  the flip cap with < 1 ms CPU. ~5 us CPU per BltFast. Blitter runs in parallel: issue blits, then run the sim
  slice, then Flip. Never free-run (86Box stalls 0.35-1 s). Keyed blits from system memory are 8x slower: all art in VRAM.
  Surfaces as wide as the mode, no wider; atlases <= 640 wide. Screenshot runs (Mac capture) invalidate timings.
- Art (`art/explore/STYLE.md`): style C chosen. Ray-cast G-buffer generator; anchors at the footprint's south vertex;
  32-px strip slicing for sorting; both zooms native.
- Research: `docs/research/THEME.md`, `docs/research/DESIGN_LESSONS.md`.

## T00 interfaces (done 2026-11-03)
- All headers in `src/`, documented with owners; overview, frame/step data flow and ownership in `docs/ARCH.md`.
- Portable set (Mac-testable): common util world defs mapgen sim net fields budget traffic politics save text tools iso.
  `make -f tests/Makefile.mac` from the project root; `vmb/w98 build lahio` for VC6.
- Arrays fixed at 256x256 (stride 256, `TILE_IDX`); playable size `g_world.w/h` (default 128).
- Art is looked up **by name** at init (`art.h` naming contract); a missing name draws a magenta placeholder and logs once.
- Sim talks out only via world notifications + `SimEvent` queue; UI acts only via `UiCmd` queue; `game.cpp` wires.
- Money = whole markka s32. Display names in defs tables. Font symbol glyphs ✔ ✘ • ↑ ↓ on byte codes 1-5 (`GLYPH_*`, text.h).
- Debug from text: `sim_statlog_open` (CSV per month), `sim_dump_report`, `world_dump`, `render_dump_at`, `world_hash`/`sim_hash`.

## T03 UI art + fonts (done) — art/UI.md, art/gen_ui.py
- Sheets: ui_frame (212), ui_icons (200 = 100 icons + `_off`), ui_pres (13), font_body, font_head; ~655 KB VRAM.
  `art_ui.h` (5 sheets, 17 fonts per art.h); `ui_layout.h` = `ui_layouts[3]` frame rects for 640/800/1024 + `UI_COL_*`
  (for ui.cpp only).
- **Side panel is 144 px at 800/1024** (128x128 minimap + bezel), 104 px at 640 with an **88x88 minimap window**:
  T05/T09 render the minimap at 88x88 or show a camera-following window of the map.
- Keys: last row/col is drop shadow (centre content on w-1,h-1); shift +1,+1 when pressed/selected.
  Panel paper area = (W-12) x (H-26). At 640 the map is 536 px wide: panels wider than that cover the tool panel.
- Fonts: body (7 px caps, line 11, baseline 9, tabular digits), head (9 px caps, line 14); ÄÖÅäöå Éé Üü § ° ½ × « »,
  GLYPH 1-5. Build a 256-entry glyph lookup at init; draw `?` for missing codes. Outlined `float_*` fonts for money puffs.
- Icon names: `icon_b_<BuildingDef.key>`, `icon_r_<RoadDef.key>`, `icon_s_<svc>`, `icon_o_<ovl>`, `icon_z_<zone>`.
- President moods (suggested ids): 0 content, 1 stern, 2 delighted, 3 worried; 32x40 and 64x80.
- Newspaper masthead "Maakunnan Sanomat" 600x54; stamps § DECREE / APPROVED / REJECTED / § seal. No typewriter font yet.

## T01 engine base (done)
- Fullscreen 800x600x16 + Flip (wait for every 2nd blank, 119.96 Hz measured; 4th at 30 fps); `-res 640|800|1024`,
  `-window`, `-gdi`; 565/555. Auto 30 fps: 6 heavy slow frames of 60 → 30; 90 light frames + 3 s (doubling backoff) → 60.
  Only *heavy* missed frames count (host load from other sessions causes misses).
- Bench 800 fullscreen: 59.97 fps, max 16.87 ms, sim budget ~14 ms/frame at 60, ~30 ms at 30.
- gfx: one VRAM surface per image (≤ screen width), render targets via `gfx_make_target`, `gfx_set_target` resets
  clip+origin; never copy an `Img` by value. VRAM full → loud error, returns 0 (no sysmem fallback).
  Usable VRAM ≈ reported free − 450 KB. Lost surfaces restored in `plat_pump`; compare `gfx_restore_serial()` each
  frame and re-render targets when it changes. Chunk render with ~60 blits ≈ 0.24 ms.
- Text ≈ 5.5 µs/glyph: compose UI caches rarely. `GfxFont` from T03 ArtGlyph via `gfx_font_init/gfx_font_glyph`;
  transparent cache: `gfx_img_set_keyed(&t,1)` + clear to `gfx_key()`. Custom cursor: `gfx_cursor_set`.
- main owns snd_init/tick/shutdown. Wrap PROF_CACHE/WORLD/SPRITES/UI inside game_draw. If game_init() returns 0, main
  shows the test scene: fatal art/VRAM errors need their own MessageBox/return code. `game_bench_camera(t)` under -bench.
- **Never pass side effects to IMIN/IMAX/ICLAMP macros** (multiple evaluation).
- `-bench N` → bench.txt (percentiles, histogram, worst frames, VRAM); `-showfps`/F11; `-simload MS` to fake sim load.

## T02 world art (done) — art/ART.md, art/gen_art.py (+ artlib.py, art_terrain.py, art_models.py)
- Sheets world0..2_64.bmp + world0_32.bmp (640 wide, ≤2048 tall), `art_world.h`; **VRAM 8.72 MB (limit 9)**.
  `python3 art/gen_art.py` (~20 s, deterministic, runs the check). `art/.cache/` is a render cache (deletable).
- 705 names per zoom: full terrain/shore (+shimmer frames for shores too: cycle shoreNN frames with water0-2),
  grav/road/main/mway/rail × 16 masks + bridges, power_00-15 / wire_01-15, all 27 M1 building keys × variants × 4 rot
  (r0 only: well, water tower, park), 12 car colours × 4 dirs, smoke0-5, 48 overlay diamonds, cursors, zone tints.
- Abandoned: all house variants; apartments/commercial/industry only v0 → renderer falls back to `<key>_v0_ab_rR`
  before the normal sprite. Construction/lot ground per footprint (`build_WxH_rR`, `ground_WxH_rR`).
- **Bridges are taller than a tile: draw them in the object pass**, not the flat ground pass.
- Road on a shore tile = plain road piece. `wire_MM` other than 05/10 have no pole: avoid power lines turning on roads.
- Tallest sprite: coal plant 101 px (within ISO_MAX_SPRITE_UP).
- Missing: river/rapids tiles (no art.h name yet; hydro needs it), fire frames, truss rail bridge, per-key lot ground.
- **Open issue for Milestone 2: VRAM headroom ~290 KB.** Tier-3 buildings won't fit as is. Options to evaluate:
  zoom-specific residency (only the active zoom's sheets in VRAM, swap on zoom change), fewer abandoned sprites,
  era-specific sheets, art in system memory only for chunk renders (chunks are rendered rarely; but sysmem keyed blits
  are 8x slower and stalled once), and the real targets' VRAM (X41/T40 chips differ from the 16 MB Voodoo3).

## T04 simulation core (done) — 18 Mac tests, tests/out/*.txt|csv
- Scripted town: 855 pop 1955 → 2,651 (1957) → 6,896 (1961) → plateau 7.6k when zoned land is built out;
  emigration ~40/month at the plateau (readable signal). Dense test city 22.5k. First house starts 20 steps after zoning.
- Mac timings ×~100 ≈ P2: 3-7 ms per phase in a dense city; the rare full power flood 10-16 ms (split it in T13).
  At speed 3 (16 steps/s) that is ~11% CPU: fine.
- Deviations: power crosses one plain road tile; coverage and water on the 2x2 cell grid; pump = 40 per water tile within
  2 tiles (max 500); tier-1 needs road (apartments also power+water); C/I tier 2 need power; power/water solved on
  alternate cycles; month-end books + date change in phase 0 of the next step; overlay growth categories GROWC_*.
- Balance (for T12): growth pace good (3k by ~year 3); late game too rich (+1,400 mk/month at 7.6k); state grant fades
  out by 4,000 pop; saunas (cap 600) are the most common tier-1 blocker.
- T08: place player buildings LOT_ACTIVE (0 build months); pay with budget_spend; timber = budget_spend(-x, BL_TIMBER);
  net_solve_all() after load; traffic writes FLD_TRAFFIC + road Tile.aux (sim uses its own COV_JOBS commute for now).
- T09: sim_query_lot/tile fill checklist have/need/why; demand terms in people (R)/jobs (C,I) and sum to target;
  budget lines BL_*; sim_top_problems sorted by residents.
- T05: overlay ramps: pollution/fire OVR_BAD, land value OVR_GOOD (level = lv/12), services OVR_COVER, power/water OVR_NET,
  growth GROWC_*, catchment OVR_COVER with the building at level 7. WCHG_FIELDS notified per quarter of the map.
- T10: world/sim save chunks written but untested (save.cpp stub).

## T07 sound (done) — sfx/SFX.md, tools/make_sfx.py
- dsound.dll loaded at run time; silent fallback everywhere. 19 buffers at init, 16 voices (max 3 per sample, steal oldest).
- Effects: SND_CLICK, KEY, PANEL_OPEN, PANEL_CLOSE, BUILD, ROAD, BULLDOZE, CASH, CHIME, WARN, TYPE, STAMP, FIREBELL,
  PAPER, FANFARE, DENY. Ambient: AMB_BIRDS, AMB_TRAFFIC, AMB_INDUSTRY via `snd_ambient(layer, vol)` (1.5 s glide).
  `snd_play_sx(id, vol, screen_x, screen_w)` pans from screen x; `snd_play_at(pan100, dist_tiles)`.
- **Cost: snd_play ≈ 0.17 ms (0.25-1 ms when switching sample)**: a few sounds per frame, not dozens.
- T08/T09: set ambient layers a few times a second from city stats; `snd_stop_all` on pause/load; never call snd_tick
  (main does).
- **T10: add INI keys `sfx_volume` (100) and `ambient_volume` (70) to config.cpp + settings menu** and call
  `snd_set_group_volume`; snd_init currently reads them itself from [Kaupunki].
- Nobody has listened to the sounds in-game yet (user should, at the vertical slice).

## T06 map generator (done) — src/mapgen.*, tests/test_mapgen.cpp, tests/out/mapgen_summary.txt
- Presets Lakeland / River town / Coast / Forest; integer, deterministic (same world_hash on VC6 and clang).
  50 seeds each: buildable ≥ 68%, shore errors 0, road edge-to-edge with exactly 2 edge links, bridges straight ≤ 4
  tiles over lakes (≤ 8 over rivers, never over rapids). River town: always a rapids section (4-9 tiles) + 8-11 hydro sites.
- P2-233 measured: 128² ≈ 0.53 s avg (max 0.72), 256² ≈ 2.2 s.
- **Shore rule (renderer):** shores on LAND tiles, water tiles full water; corner = water if any of the ≤4 tiles at the
  vertex is water (out-of-map = land); bits n=1 (x,y) e=2 (x+1,y) w=4 (x,y+1) s=8 (x+1,y+1); `mapgen_shore_mask(x,y)`:
  15 plain land, 1-14 shore<NN>. Every land tile is in a 2x2 land block; no specks/checkerboards.
- **Rapids/river art missing:** TER_RIVER (variant bits 0-1 flow dir, RIVER_RAPIDS_BIT) is drawn as water until T02
  adds river + rapids sprites (art.h names needed). → small art follow-up task.
- National road = ROAD_MAIN, protected (orchestrator: keep; a pre-existing highway is fine even before main roads unlock).
  Railway shares the road's edge pair and never crosses it (no level-crossing art).
- T08: `mapgen_report()->town_x/town_y` = road tile at the centre of a cleared ~r7 area for the town hall.

## T02b river + rapids art (done)
- `river_<x|y|n>_<0..2>`, `rapids_<x|y|n>_<0..2>` both zooms; VRAM now **8.81 MB (~190 KB headroom)**.
- Frames 0→2 move streaks toward +X/+Y: play 2→0 for flow toward −axis. `n` = non-directional (bends).
  Rapids foam fades to tile edges; rocks fixed across frames. River shores use lake-coloured water (one tone lighter) —
  reads as shallows; no river-specific shore tiles.

## T09 UI module (done) — src/ui.*, src/text.*, tests/out/ui_shots/
- UI cost (P2, 800): 0 recompositions/s idle, 3-5 blits/frame, ui_draw 0.03 ms; recompose top bar 0.67 ms, side panel
  0.7-1.0, tooltip 1.3-1.9, fly-out 2.1, panels 1.6-3.6 ms. UI cache VRAM 481/575/636 KB (640/800/1024).
- Money "12,500 mk" (`TEXT_THOUSANDS` define), old markka ×100 before 1963 (s64), short form "12.5M mk".
- **UiCmds T08 executes:** UC_TOOL(tool,param), UC_SPEED, UC_OVERLAY(ovl,0), UC_TOGGLE_BUILDINGS(1 shown/0 flat), UC_UNDO,
  UC_REDO, UC_CAMERA(tile x,y), UC_SET_TAX(res%,bus%), UC_SET_FUNDING(svc,%), UC_MENU, UC_SAVE, UC_QUICKSAVE, UC_LOAD,
  UC_QUIT (UC_ZOOM in enum, not emitted).
- **Init order: ui_init() before render_init()** (else UI caches fail → 3.5 ms / 500+ blits fallback). Minimap built
  on first ui_update.
- **Per frame:** consumed = ui_input(input_get()) (1 → don't pass mouse to tools); ui_key() before own Esc; drain
  ui_poll_cmd; ui_update(dt); ui_draw() after the world. Also ui_ticker_event(&e) per sim event, ui_set_tool() on
  hotkey tool change, ui_open_query() on TRES_QUERY, ui_float_money() after a commit, cam_set_view(ui_map_rect).
- Open: render_minimap ≈ 24 ms on P2 (asked T05 for an incremental version); 1024 VRAM tight (pool 41/48 chunks).
- main.cpp: one dispatch line for `-test ui` added (approved).

## T05 renderer (done) — src/art.*, src/iso.*, src/render.*, tests/out/t05/
- Bench 60 fps at both zooms, 640/800/1024 (0-3 host hiccups >20 ms per 20 s). Chunk render 0.475 ms (z64) / 1.25 ms
  (z32); ~0.5 chunks/frame while scrolling; ≤ 4 chunks/frame cap; visible missing chunks always rendered at once.
- Zoom switch at 800: one frame (10-22 ms). **At 1024 only one zoom's world art is resident** (auto): zoom switch hitch
  180 ms out / 450 ms in. Art 9688 KB both zooms; pool 35 chunks at 800 (1333 KB left before UI caches), 30 at 640,
  48 at 1024 (500 KB reserve assumes ui_init before render_init; fewer margin chunks degrade gracefully).
- `-test render`: test town (`render_build_test_town`), 4 sim months, cars+smoke demo; honours -bench/-shot/-exitafter/
  -zoom/-cam/-overlay N (N+100 = buildings shown)/-speed 3. Keys O overlay, Tab, D dump, G ghost.
- Minimap incremental: `render_minimap_rows(dst,pitch,w,h,ovl,y0,y1)` (end row), 16 rows ≈ 0.5-1.8 ms; minimap is a 45°
  diamond: map clicks via `render_minimap_tile/_pos`. **Upload the minimap at frame start** (gfx_img_update mid-frame
  waited 15 ms behind queued blits).
- `art_get` returns the resident zoom's copy; UI swatches check `art_resident_zoom()`.
- T08: cam_set_view(map rect) before render_init; zoom via render_set_zoom (swaps art at 1024); previews:
  render_tile_mark(art_find("cur_ok"),x,y), render_lot_ghost(id,x,y,w,h) with rotated footprint; game_bench_camera →
  render_bench_camera. T09: draw UI after render_flush_sprites. T11: render_sprite(id,wx,wy,tx,ty,0) with
  iso_pos_to_world, smoke hotspot art_get(zoom, render_lot_art(lot))->hx/hy (rel. to front tile south vertex),
  render_set_anim_frame every 300 ms; lane offsets/turning reference = demo_update/demo_draw in render.cpp.
- Known glitches: lot on a shore tile covers the shore; green/blue ramp ends faint on grass; buildings-shown overlay
  tints facades; shimmer re-renders whole water chunks; a dirty chunk can be stale for 1-2 frames.
- Wishes: T01 evict/restore image VRAM without freeing pixels (1024 zoom swap); world batches → short rect list instead
  of one bounding rect (T13). VM clock behind the Mac → nmake rebuilds everything every time.

## T11 cosmetic life (done) — src/life.*, src/lifecar.* (portable), tests/test_life.cpp, tests/out/t11/
- game.cpp: life_init() after render_init; life_reset() after new city/load (zoom needs nothing: cars in tile coords);
  life_update(dt) every frame with real dt, also when paused (cars/smoke freeze themselves when sim paused; water
  shimmers); life_draw() between render_world and render_flush_sprites. Optional life_traffic_level() (0..100) for
  the traffic ambient, life_set_enabled, life_stats.
- Lanes 2.5/3.6/4.8/5.0+9.6 units (gravel/street/main/motorway); quarter-arc turns; turns weighted by flow (1.5x
  straight); U-turn at dead ends; leave map at edge links; junction wait; stuck 3 s → ghost 1.5 s (no deadlock).
- Count: T08 flow (already era-scaled) → 1.2 cars/road tile at level 128; fallback estimate from nearby occupants;
  cap 150. 1955 test town with real flow: 2-16 cars (balance question for T12).
- Cost (P2): life 0.14 ms normal, 0.41 ms at 150 cars. **render_flush_sprites ≈ 3.1 ms for ~75 car sprites (~40 µs
  each incl. occlusion blits): T13 target.**
- `-test life` (`-bench` or `-speed 3` = stress: flow 150, year 1972, no pop cap).
- Known: brief overlaps when two cars enter a junction in the same frame. VC6 flags 2 unused locals in traffic.cpp:104 (T08).

## T08 integration / vertical slice (done) — src/tools.*, src/traffic.*, src/game.*, main.cpp, scripts/town1.txt
- No args → title → New town → play; -seed/-preset/-script/-load/-bench/-play start directly. Town hall auto-placed at
  mapgen town_x/y facing the national road (Home centres on it).
- Script language (`-script FILE`, headless with `-fast -script FILE`, same interpreter in Mac tests; `expect STAT OP V`).
  scripts/town1.txt (seed 7 Lakeland, speed 3): pop 1955-59 = 1,077 / 2,124 / 2,833 / 3,661 / 4,752; end 10,317 mk
  (+679/month), Market town, power 1,199/1,200. Identical on Mac, VC6 headless and in game.
- Bench over the 5-year session: 59.80 fps, 99.3% at 60, 0 at 30. Traffic phase 14: 1.4-2.4 ms on P2 (max 5.3).
- Keys: R road (rotates while placing a building), H/A/C/I zones, P power, S cycles services, W cycles water, Space,
  F1-F3, Ctrl+Z/Y, 1-9 overlays (0/same key = off), Tab flat buildings, Home, F12. **WASD doesn't scroll** (W/A/S are
  tool keys): arrows, edge scroll, middle-drag.
- Issues: Ctrl+Z not verified with a real keyboard (vncdo didn't deliver it; UI undo button works). Windowed pause menu
  fails to get VRAM for a 258x128 stipple. Early 22 ms spike = phase 10 pollution terrain recalc on every tree cleared by
  growth (world_lot_place sends WCHG_TERRAIN) → T13. "homeless -1" printed once → T12. Undo can re-place a building
  into a different lot slot.
- traffic_save/traffic_load (chunk TRAF; load after traffic_reset). FLD_TRAFFIC = sum of road levels / 8.
  Car share 25% → 85% 1955-1975. Autosave skipped in script/bench runs.

## T10 menus, new game, save/load (done) — src/menu.*, src/save.*, src/config.*, tests/test_save.cpp, tests/out/t10/
- Format: `KAUP` v1 + chunks (tag, ver, size), END chunk checksum. META, THMB (64x64), TILE, LOTS, SIM, SIMX, BUDG, BUDX,
  FLDS, POLI, NET, TRAF. ~68 KB at 900 pop. P2: save 345 ms, load 69 ms. SAVE1-8.KAU, AUTO1-3.KAU, QUICK.KAU in exe dir.
- Determinism across save/load verified (N = 7/18/30 + 12 months); VC6 hashes = clang hashes. Corrupt files rejected.
- Added (T10-marked) to T04 files: net_save/load (NET), sim_save_ext/load_ext (SIMX), budget_save_ext/load_ext (BUDX);
  sim_init re-registers its world listener every call.
- menu.h top comment = exact call sequence (T08 wired it). Resolution/fullscreen changes apply at next start.
- Open: ui.h lacks exported 9-slice/button helpers (duplicated in menu.cpp); no delete-save button; title backdrop =
  T05 test town (quit to title asks to confirm).

## T12 balance (done) — docs/BALANCE.md, tests/test_balance.cpp (bot players), scripts/bal_*.txt
- Deterministic player bots (planner, greedy-no-services, industry, dense-after-1960, tax 13%/5%) on 3 maps × 20 years;
  each run recorded as a replayable script. Planner: 500 pop at month 7, 3k at 36-42, 10k at 105-141; ≤ 1 emergency loan.
  Greedy plateaus at 5k, all tier 1 (top problem "school"). Tax 13% halves growth; 5% bankrupts.
- First house 32 steps after zoning (5.3 s at speed 2). Shocks recover (industry loss → 9,700 emigrate, back in 4 years).
- **Bug fixed: world.cpp `s_brect[WB_MAX]` overrun** (written at index WB_MAX) → memory corruption in bigger towns.
  "homeless -1" fixed. Death spiral fixed: SIM_DECLINE_DEMAND_FLOOR (-6) for occupied lots. LOAN_RATE 20 → 8‰, LOAN_MAX 40k.
- Constants: cottages need no power/water (tier 2 does); tier 3 no longer needs the post office (no art in M1);
  coal 2,500 mk/110/500; school 100/500; clinic 70/2,500; fire 50/2,500; sauna 400 mk/25/1,500 reach 25; resident tax
  scales with tier (1/4/6; apts 4/6/9) and land value; village grant 400 − pop/8; Normal funds 25k; **upkeep index
  +2.5%/year from 1960, cap 150%**; rural pull 1955-59 80 → 70; TRAFFIC_TRIPS = 5.
- Concerns: built-out town still accumulates 60-120k by 1975; tight 1958-62; tax > 11% steep.
- Follow-ups (not done): **life.cpp car floor of 12 dominates** (17/27/41 cars in 1956/65/75; scale floor with car ownership
  → ~9/29/51); **Query/Budget UI should show the upkeep index**. Milestone 2: phone requirement + post office art, apartment
  T2/T3 art, money sinks (landmarks, decrees, status reward choices), era income (clearing trade, oil shock), Workers
  approval ↔ emigration, politics-on bot.

## X41 real-hardware test (orchestrator, 2026-11-03)
- Velocity9x 0.34.0 (GMA 900) at 800x600x16 fullscreen: DirectDraw reports **5910 KB total / 4973 KB free** → art
  load fails at the 2nd sheet (DDERR_OUTOFVIDEOMEMORY); game shows its error and exits cleanly. 86box-76 investigates
  the driver heap. Panel refresh 49.88 Hz → game correctly falls back to timer pacing.
- User decision: **16 MB VRAM is the design minimum**; a new Win98 PC with native support and ≥ 16 MB arrives ~2026-11-04.
  Focus stays on the VM. (Optional later: system-memory fallback for art when VRAM runs out.)
- Lesson: launch fullscreen apps on real PCs with `samba/x41 start` and read the log afterwards; an interrupted wait
  leaves the game running and holding exclusive mode (DDERR_EXCLUSIVEMODEALREADYSET for the next run).

## T13 performance (done) — docs/PERF.md, tests/test_perf.cpp, tools/gen_bench20k.py, BENCH20K.KAU
- Bench city: `lahio.exe -load BENCH20K.KAU -cam 55 55 -bench 60 -speed 3 [-zoom 1] [-res N]` (~24k pop, 2,300 lots).
- 20k town scrolling at speed 3: 59.9 fps at 640/800/1024, both zooms; speed 3 keeps up. ~10 frames/min reach 25 ms
  with only 1-6 ms CPU (flip wait: emulator/blitter jitter). No sim step > ~13 ms.
- Fixed: pollution 22.7 → 1.5 ms (incremental cells, same hash); car flush 3.1 → 0.43 ms avg; batches = up to 8 rects
  (dirty chunk renders 2.38 → 0.53/s); minimap ~1 ms/frame; autosave 1,090 → 215 ms frame + worker-thread write, waits
  for 0.75 s without scrolling; 1024 zoom switch 91/141 ms; shimmer redraws only water areas.
- Full power+water solve 5.6 + 6.2 ms on 20k town: not split (under frame limit).
- Behaviour: listeners get one call per rect; `-zoom` overrides save zoom with `-load`; `-cam X Y`; `-repairs N` debug.
- Left: 215 ms autosave serialise frame (snapshot + thread encode); blitter budget for chunk renders; z32 shimmer rate.

## F2 art: pipes, yards, zone view, UI bits (done)
- `pipe_<MM>_<wet|short|dry>` (MM 00..15, road mask bits), both zooms; anchor = tile south vertex. Draw after the tile's
  ground + `ovl_` diamond (ART.md 6).
- Yards: key **`ind_yard`**, 1x1, **4 variants** (timber, steel stock + shed, brick workshop, coal heap + shed), 4 rotations,
  abandoned v0 only; ground/build via `ground_1x1_r<R>` / `build_1x1_r<R>`. Def must be `ind_yard`, variants 4, rotating.
- Zone view: `zonev_<Z>` and per-edge `zonev_<Z>_<01|02|04|08>`; **front-edge rule**: after tile T, for its SE (02) and SW
  (04) edges, if T's zone differs from the neighbour's, draw `zonev_<T zone or neighbour zone>_<edge>` (ART.md 6;
  reference zone_layer() in gen_art.py).
- UI: `icon_pipe`, `icon_zoneview` (+`_off`), `btn_zoom_in_*` / `btn_zoom_out_*` (15x12, 4 states); `UiLayout.zoom_key`
  appended (top bar, left of speed keys): ui.cpp top-bar limit → `zoom_key.x - 6`.
- VRAM per zoom: 64-zoom world 7415 KB (limit 9.6 MB → **2,415 KB headroom for M4**), 32-zoom 1825 KB, UI 655 KB.
  `gen_art.py` now exits 1 when its check fails.

## M4 Milestone-2 art (done) — art/ART.md 3.1 "Milestone-2 keys" (the table code tasks must match)
- All 7 groups delivered, both zooms. 64-zoom 9,491 KB (**339 KB headroom**), 32-zoom 2,324 KB, UI 655 KB.
- Keys: apt_t2_* (2 var), apt_t3_3x1 elementtitalo / apt_t3_2x2 pistetalo (2 var); com_t3_1x1 bank / 2x1 office /
  2x2 dept store + ostari; industry kinds (wood/metal/textile/dairy/electronics tiers; several zero-VRAM aliases);
  ind_yard variants 4 → 6; police, library, post, health_centre, heat_plant, sewage_works, station, bus_stop, bus_depot;
  farm_t1-3, logging_camp, forest_cut/seed/young, field_hay; paper mill pm_* pieces (pm_boiler = heat_plant alias);
  Valtion Metalli vm_* pieces; row_houses, church, patron_villa, concert_hall, tv_tower (184 px), ski_jump, pres_sauna,
  friendship_statue; bus_<dir>, fire0-3; 15 new icons (incl. icon_bus_line, icon_z_forestry, icon_z_farm).
- BDF_NO_ROTATE needed: pm_chipper, vm_store, vm_siding, vm_gate, ski_jump; complex pieces BDF_MODULE (offsets in ART.md).
  Station platform on its back (-Y) edge: rotate back to the rail. Post office art exists → phone requirement can return.
- No abandoned art for industry kinds/services/complexes. Symmetric art r0/r1 only (r2/r3 aliases).
- Infra: per-variant rotation policy, key aliases, art_m2_*.py modules, m2_preview.py; artlib skyline_pack tries 4 orders.

## Orchestrator fix: one zoom resident at every resolution (2026-11-03)
- M4 pushed 64-zoom art to 9.5 MB; art_init loaded both zooms → "Could not load the game art" at 800x600.
- art.cpp art_init: pass 0 loads 32-zoom world sheets and evicts each at once (px kept in system memory), pass 1 loads
  64-zoom + UI; s_resident = ZOOM_64. render.cpp render_init: s_residency = 1 when art_resident_zoom() >= 0, so
  render_set_zoom swaps residency (T13: ~91/141 ms). VRAM at 800x600: 64 art 9.5 MB + UI 0.65 + caches/pool.

## M1 politics (done) — src/politics.*, src/poltab.cpp, src/ui_pol.*, tests/test_politics.cpp, tests/out/m1_shots/
- 4 blocs monthly from the city with itemised reasons; weights from housing type/tier/LV + a fading countryside Rural vote
  (s7: Rural 76% → 18%, Workers 14% → 44%). Popularity = weighted mean. Eastern relations scales clearing I-demand
  (75-125%) + plant/pump costs; Western trade scales Western orders; both drift to 50.
- Authority +popularity/20/month, cap 30 (thin win: −1/month, cap 25; landslide +1, cap 35). ≤ 5 decrees, cost,
  12-month cooldown, repeal half, ≤ 1 new decree per 3 months. 6 Letters (1 per 6 months). 15 event cards + campaign card
  (wait in ticker; auto-resolve with first choice after 12 months; 12-24 months apart). Elections every 6 years in January;
  electors = 151 + (share − 40) × 109/35 (cap 280); emergency powers law skips one. Plaque mood follows politics.
- Sim hooks neutral when off: DT_POLICY, ST_POLICY, emigration, pollution, LV growth, happiness, school capacity, LV weight;
  ledger BL_POLITICS. Save chunk POLI v1 (BUDX bumped to v2). Politics off = bit-identical to no-politics (hash 74dc0f2d).
- UI: top-bar approval (§ when a card waits) → "The President's desk" (Overview / Decrees / Letters), event card and
  election night dialogs; ticker items open the desk. New Game toggle on by default; `-politics`; script `pol ...`, `polpanel N`.
- Hooks for M6/M7: PM_CRIME_PCT, PM_ELEMENTS_PCT, politics_meter_add, politics_queue_event, politics_enable_hook_decree
  (town rights, state metal works hidden decrees), politics_status_ceremony.
- Open: (1) **save round-trip divergence at the first power/water step after load (politics off too)** → F1 asked to fix;
  (2) authority too generous (bot at cap; politics-on towns richer) → M8; (3) politics money booked a month late.

## F1 water pipes + conduction (done) — src/net.*, tools pipe tool, tests/test_water.cpp (16 tests)
- Storage: TF_PIPE, ZF_WATER_NET, world_set_pipe (WCHG_PIPE 0x100; WCHG_ALL = 0x1FF), world_pipe_tiles(); TILE chunk v2;
  v1 saves get pipes under all gravel/street/main roads. Zoning/dezoning bumps world_struct_serial; pipes don't.
- Solver: graph = pipes + conducting lots + zoned empty land; roads block unless piped; nearest-first per source with
  strict cut, spare flows on, furthest dry first. Pump needs power ("needs power"); tower 200 only with a producing
  pump/well on its network; tower pressure zone (16 graph tiles) = Apartments T2+ requirement. Fast path when graph
  unchanged. Zoned empty land conducts power too. NET chunk v2 (v1 → full re-solve).
- APIs: net_water_source (WaterSrcInfo), net_lot_water_src, net_water_tile_state/_src, net_water_summary, net_water_dump
  (text map), net_water_mark_rect, NetStats.used.
- Pipe tool TOOL_PIPE: L-drag, 3 mk/tile (15 under water), upkeep 1 per 10 tiles; Ctrl+drag (or bulldozer with water
  overlay) removes; script `pipe X0 Y0 X1 Y1 [remove]`, `pipes_with_roads on|off`; INI `pipes_with_roads` + New Game
  checkbox (default off). W = pipe tool then cycles pump/well/tower; pipe tool shows the water overlay.
- UI: source query "440 / 440 used, serves 34 buildings, reach 18 tiles, 9 dry beyond"; checklist reasons; top-bar
  "Water 33%" (tooltip details; click toggles overlay); shortage ticker jumps to the worst source.
- Fixed: stale power-flood stamps (history-dependent results); incremental power solve falls back to full flood on
  brownout (save/load determinism). VC6 town1 headless = Mac (0 errors, pop 4,698, money 9,608, world 4e4282cb).
- Timings (VM, BENCH20K): 59.8-59.9 fps; network phase avg 1.75 / max 3.08 ms; water fast path 2.40 ms; **graph change
  7.33 ms** (edits only) → M9: time-slice or incremental BFS.
- VRAM 800x600: 64-zoom resident → only **562 KB free** (chunk pool 30/35); 32-zoom resident → 7,730 KB free. Zoom swap
  159 ms in / 39 ms out. Keep an eye on VRAM in F3/M-tasks (UI caches).
- Balance: bots lay pipes; housing expansion fires at R demand ≥ 0; failure/recovery thresholds 85/70%; milestones hold.
- Left for F3: `ovl_3_4` (NETC_WET) grey → faint blue; watered lots green → blue (WATER.md); source hover highlight;
  right-drag removal. BENCH20K regeneration needs pipes in scripts/bench20k.txt.

## VM B (2026-11-03)
- kaupunki has its own VM "Windows 98 B" (computer name WIN98B; agent98 Mac port 9899, VNC 5999). Use `samba/vmb/`
  wrappers (w98, vmlock, shot86.sh, vnc98.sh). VM A (plain tools) belongs to SIROCCO (86box-e8). Same M: share.
- agent98 1.3 on both VMs (2026-11-03): timeouts kill the whole process tree; `vmb/w98 ps` lists processes (slow VM →
  look for WINOA386.MOD); no interactive DOS commands (time, date, pause, choice) via w98 sh.

## M3 industry kinds, forestry, farms (done) — src/industry.*, tests/test_industry.cpp, tests/out/industry_*.txt
- Kinds (table keyed by def id): sawmill, engineering (fallback), textile, dairy, electronics (1965+, Town). Weighted draw
  (sim RNG): kind demand × (100 + 2 × site bonus: lake log pond, forest/forestry near, rail/main road, river, farms, station).
  Musts: wood/milk supply ≥ 75% incl. the new lot; electronics needs school ≥ 50, pollution ≤ 48. Kind demands sum to the
  I target (clearing/western/local shares); unusable demand → engineering.
- Wood: regional 10 (+15 station, +15 floating/trucks, max 40) + forestry; logging camps harvest within 4 tiles, nearest
  first; cut tile = 12 wu, regrows cut → seedlings → young → mature in 48 months (Tile.aux high nibble).
- Farms: claim ≤ 8 fields (grass → field); milk = fields × tier + regional 20; mechanised T3 (1962) needs power + street,
  sheds 2 residents. ZONE_FORESTRY / ZONE_FARM, REQ_WOOD / REQ_MILK, 15 new defs (ART.md 3.1 keys).
- Hooks: politics Rural bloc from farms/camps, clear-cut share > 25% hurts; budget export rate by kind, farm tax; tools
  zone painting + script `forestry`/`farm`, `dump industry`; ui fly-out, query/tile lines, I tooltip by kind; render forest
  stages + field_hay. Save INDU v1; SIM/SIMX v2 store zone/def counts; old generic industry lots → kind defs on load.
- Hooks for M6/M7: `industry_set_ext_wood(slot, wu)`, `industry_wood()`; `industry_kinds()` export_vol/west/east,
  west_total/east_total; `industry_west_pct()`.
- Open: VC6 break in M2's traffic.cpp (`near`/`small` macros) + LB_MAX warning (M2 told); too many yards (70/93 in town1)
  + missing zone_5/6 + zonev_5/6 art (F3 told); no industry-kind overlay; few farms reach T2.

## M2 traffic v1.0, buses, station (done) — src/traffic.*, src/transit.*, src/lifebus.*, tests/test_traffic.cpp
- Graph (junctions, ends, class changes, edge links, node every 6th tile) rebuilt on road change; ≤ 32 job clusters
  (8x8 blocks); per-cluster Dijkstra with BPR; mode split + gravity; two sweeps per pass (exact normalisation); MSA
  (k ≤ 20); fixed work per sim step (`traffic_slice()` in every phase but 14). Outputs: road Tile.aux, traffic + road-noise
  fields, commute per 2x2 cell (sim/fields now use it), rail access, mode shares. Motorways link only at ends/crossings,
  no zone access. New REQ_ROUTE ("Route to jobs"). Road query: flow/capacity %, top source→destination, bus riders.
- Buses: TOOL_BUS_LINE (Roads fly-out): click stops, right-click ends, Ctrl+click deletes; ≤ 8 lines × 16 stops; 300 mk +
  40/stop; 12 mk/bus/month; needs an active bus_depot; stop shelters auto-placed; mode split bus/car/walk by era; buses
  half congestion delay. Script `busline ...`. Station: auto-rotated back to rail, unique; DT_REGIONAL (R) + DT_FREIGHT (I).
- Save TRAF v2 (graph + running pass + lines). VM B BENCH20K: 59.8-60 fps, speed 3 keeps up; phase 14 avg 2.1 / max 3.9 ms;
  graph rebuild 11.8 ms (road edits only), pass start 4-8 ms → M9 split.
- APIs: traffic_rail_access(x,y), traffic_rail_dist, transit_stations(), transit_regional_migrants(pull),
  transit_freight_jobs(exports).
- Open: art draws motorway–street side links that traffic treats as unlinked; bus lines not undoable; bus upkeep on the
  special-buildings ledger line; BENCH20K never congests.

## F3 UX fixes (done) — tests/test_f3.cpp, scripts/f3_*.txt
- Zone view V (render_set_zone_view, INI zone_view); tool keys select their last-used entry (bug fixed, test);
  zoom + / - / numpad / Z + top-bar zoom buttons; new-game "The town hall stands ready, President." + 6-hint chain
  (INI hints=0); hover labels after 0.4 s; budget_upkeep_index() in Budget/Query; car floor 12*own/50.
- VRAM (VM B): 640 free 1436 KB; 800 pool 31/35 free 489 KB; **1024: 64-zoom art cannot load** (world3_64 refused,
  4477 KB free) → degrades to 32-zoom only at 1024 (art_zoom_available; gfx_load_bmp_sys keeps 32-zoom sheets in system
  memory). Needs an art-sheet decision (narrower/shorter sheets) later. Minimap created in ui_init (black-minimap fix).
- Water overlay: wet ground light-blue dither, watered lots blue, source hover highlight; pipe removal stays Ctrl+drag.
- Yards: only where no 2x2 fits after the neighbourhood settles; cap 12% of industrial tiles in 9x9; variant follows
  neighbour kind. town1 22/50 yards (was 70/93). zone_5/6, zonev_5/6, ovl_4_5/6 art added; 64-zoom 9,512 KB.
- Open: windowed pause menu draws top bar/side panel black (M10); phone requirement deferred to M5 (B_POST variants 0).
- Script commands: `zoneview N`, `mouse X Y`, `menu`.

## R1 rename to Project Lähiö (done) — common.h, config.*, menu.cpp, ui.cpp (badge), art/gen_ui.py
- `GAME_TITLE` = `"Project L\xE4hi\xF6"` (Latin-1; window caption, top bar, pause panel); `GAME_TITLE_ASCII` "Project Lahio" for
  logs/bench files; `GAME_TAGLINE`. Window class `Lahio98`. Exe `lahio.exe`, log `lahio.log`, Mac `tests/build/lahio_tests`.
- `LAHIO.INI` `[Lahio]` (`CONFIG_INI_NAME/SECTION` in config.h; snd.cpp uses the macro). Missing LAHIO.INI + existing
  KAUPUNKI.INI: read `[Kaupunki]` once, write LAHIO.INI, log "config: settings migrated from KAUPUNKI.INI ...".
- Title screen: new `stamp_project` (red letter-spaced PROJECT stamp, gen_ui.py `stamp_spaced`), "LÄHIÖ" head font
  letter-spaced +1 px, umlaut dots squared to 2x2 and image cropped to ink, scaled 4x (144x48); tagline in spaced caps;
  window 330x292. Menu plate shows "LÄHIÖ"; grille `badge` now "LÄHIÖ" (37x13, drawn on top of the slots).
- Unchanged on purpose: save magic `KAUP`, `*.KAU`, the default town name "Kaupunki", internal comments.

## Git (2026-11-03)
- Public repo **https://github.com/HenriVSL/project-lahio** (branch main). Commit identity in this repo: "Henri Lavikainen"
  <4943859+HenriVSL@users.noreply.github.com> (no-reply, the profile email is private). Commit messages end with the
  Co-Authored-By line. Agents: commit only when the orchestrator says so; `.gitignore` keeps build/test outputs out.

## Recurring bug class: side effects in IMIN/IMAX/ICLAMP (3rd occurrence, 2026-11-03)
- `ICLAMP(load_s32(sf), ...)` read the save up to 3 times (civic.cpp, fire.cpp, complex.cpp, trade.cpp) → every save
  failed to load; earlier T01 hit `ICLAMP(atoi(argv[++i]))`. **M10: replace the macros in common.h with inline functions**
  (int versions; check for non-int uses first) so the class of bug disappears. Until then: never pass calls or ++ to them.

## M6 complexes (done, trimmed per DESIGN 14.7) — src/complex.*, tests/test_complex.cpp, tests/out/complex_*_s7.txt
- Paper mill "Kuusikosken Paperi Oy": TOOL_BUILD pm_hall, 7 pm_* lots rot 0, 6 months, 5,000 mk, Church village, unique;
  site: ≥ 2 water tiles on the log-pond (−X) side + road within 2; on rapids own turbines (no grid power). Stages
  300/600/900 jobs, wood 40/80/120 (industry_set_ext_wood), town 3,000 / 6,000 + 1963 + station, mood > 30. Smell: LV −20
  within 8. Hooks CPX_DEF_USE in net.cpp/fields.cpp.
- Patron mood with named reasons; ≥ 70 → a gift per 24 months (row houses, sauna, church, school, villa) placed free with
  TOOL_GIFT within 12 tiles. Company-town (complex jobs ≥ 40%) → 20% of school/clinic/fire upkeep (BL_COMPANY).
  3 demand cards + 2 risk events (slump, PM3 modernisation) via politics_queue_event in the random-card slot only.
- Valtion Metalli: hidden decree (politics on) or Market town 6,000 mk; railway within 2; +300 jobs per decade line;
  85% East; halves from 1990.
- APIs: complex_export_east/west, complex_risk_ahead, complex_effluent, politics_set_event_hook. Save CPLX v1.
- Open (M8/M10): company-town state never reached in 12k bot towns (mill 9-21% of jobs); PM3 not reached (wood competition,
  no station); rail-less maps can't have VM/station; complex/gift placement not undoable; fixed names.

## M7 trade & eras (done, thin per DESIGN 14.7) — src/era.*, src/trade.*, src/ui_graph.cpp, src/ui_news.cpp, tests/test_trade.cpp
- Town status by decree DEC_TOWN_RIGHTS (2 authority, auto-offered; sim_grant_status), auto with politics off; ceremony page.
- Era events: devaluations 1957/1967, 1963 new markka joke (×100 display removed), oil shocks 1973/1979, oil collapse 1986,
  Jan 1990 end of clearing + newsreel. Newspaper each January (3 scored headlines + filler; pauses; INI newspaper=0 → ticker).
- Graphs panel 6 tabs (G); Trade panel (T); Newspaper (N). Clearing: 5-year EV_AGREEMENT card (Standard/Modest/Ambitious),
  goods from Eastern exports, yearly review, balance meter exports − oil (5-year average clearing price), surplus lost,
  deficit → Eastern relations −, clearing demand ×90%; oil boom ×150% (1974-80). Western = demand multiplier + devaluations.
  One "Issue a bond" + emergency loan. BL_FUEL ledger line. Landmarks: concert hall, TV tower (Town 1970), ski jump,
  presidential sauna (sauna diplomacy), friendship statue. Oil plant = coal-plant art alias. Save ERA + TRAD.
- pipes-with-roads default ON (INI + New Game); `-script` runs start with it off.
- APIs: trade_set_complex(slot, TG_PAPER|TG_SHIPS, east, west) (M6 not wired yet), era_card_slot_free() (cap not enforced in
  politics_queue_event), trade_state()->oil_index.
- **Open before play-test 2:** late-game books negative (planner −101k by 1990: upkeep index 150% + M5/M6 costs + fuel);
  wire complex exports into trade; enforce the 1-card/year cap centrally; gen_art --check FAIL (M6 module rotations).

## M5 services & housing (done, trimmed) — src/civic.*, src/fire.cpp, tests/test_civic.cpp, tests/out/civic_*.txt
- Element factory pool 300 × staffing × power × PM_ELEMENTS_PCT per month; Apartments T2/T3 upgrades draw from it.
  Apartments T3 needs district heating. Police/library/post/health centre = plain capacity services (crime cut; PM_CRIME_PCT
  is an LV term where police don't cover). Phone requirement for houses T3 back, counted only from 1960.
- Heat plant = source on the pipe graph (nearest-first, strict cut, spare passed on; LF_HEAT/ZF_HEAT); fuel in upkeep,
  `civic_set_fuel_pct` (M7 oil). Water key cycles water → heat → off. Sewage works: capacity in residents on the pipe network;
  untreated → nearest lake cell tint (water overlay), LV "Polluted lake" ≤ −10, happiness ≤ −5.
- Fires: wooden zone lots outside fire cover (quality < 20), never with disasters off, ≤ 3 spreads, rubble (bulldoze or 6 months).
  Own RNG stream. rubble0/1 art; 64-zoom headroom 309 KB. Checklist ≤ 5 lines.
- Deviations: health centre at Town (1972), post office at Church village (1960).
- **Open: Apartments T2/T3 barely grow (1 T2 block by 1975 in bot towns): upgrade needs R allowance +60/+80 in one band.**
  Heat overlay with buildings shown draws nothing (flat view works). Overlay count still > 9.

## P1 pre-play-test integration (done, feature freeze) — tests/test_p1.cpp, tests/test_balance.cpp era runs, scripts/p1_*.txt
- **Apartments T2/T3** (sim.cpp `apt_upgrade_ok`): upgrades of existing blocks follow their own next-tier checklist
  (tower, school, elements in this month's pool; heat + clean air + LV for T3), a full block and the site score with a
  negative R bar ignored, guarded by town vacancy <= 4% (min 120); no R allowance needed. Element factory Market town 1962,
  5,000 mk (was Town 1960, 8,000); apartments T3 LV 40 (was 50). Dense bot s7: first T2 1966, T3 1967; 15 T2 / 19 T3 by 1975.
  Bot fix: modules keep the zone they were built with (dense module 1 = houses); heat plant also for unheated blocks.
- **Economy**: upkeep index cap 130% (1972; was 150% 1980); apartment tax weights 4/5/7 (was 4/6/9). 1955-1990 s7 runs
  (BALANCE.md 8): planner/politics/metal end 7-13k with 0 loans, -130..-140 mk/month in the oil years; dense 107k, mill 64k.
  Early pacing and the 20-year milestones unchanged.
- **Complex exports -> trade**: complex_month calls `trade_set_complex(i, TG_PAPER | TG_SHIPS, east, west)` every month;
  the goods list / clearing meter / Western total now include paper and "ships and paper machines".
- **One card per game year, centrally**: `politics_queue_event` gates every source (random, patron, risk, agreement):
  nothing pending, >= 12 months since the last card left the desk (POLI v2 `card_since`), no election within 6 months;
  random/patron cards also wait for the 12-24 month timer; the agreement card only needs the gap (if it can't go up within
  TRADE_OFFER_MONTHS the Ministry signs Standard, era_card_pending = card on the desk). The campaign card is exempt (part of
  the election). `politics_force_event` for tests and `pol event`. Politics bot: <= 1 card/year + campaign, ~24 in 35 years.
- **gen_art --check PASS**: the checker now expands object-like flag macros (`#define MOD (BDF_MODULE | BDF_NO_ROTATE)`), so
  the fixed-orientation M6 modules need r0 only. VRAM unchanged (64-zoom 9,521 KB, headroom 309 KB).
- **Overlays**: the panel lists 9 + "No overlay": Power, Water and heat (sub-views Water pipes / District heating; sewage =
  lake tint), Pollution, Land value, Traffic, Fire risk, Services (sub-views: the 7 service covers), Why not growing?,
  Zones. Number keys unchanged. The heat overlay draws in both modes (VM shots + `dump ovl17` = 413 tiles = Mac); the M5
  "draws nothing" was not reproducible (the dither tint is invisible in a downscaled screenshot - crop it).
- **common.h**: IMIN/IMAX/ICLAMP/IABS are inline overloads (int, s64, u32, double); mixed s64/int calls are ambiguous by
  design (5 casts added: traffic.cpp x3, trade.cpp, ui_graph.cpp). The macro bug class is gone.
- New script command `dump ovlN FILE` (text map of overlay N's values).
- VM B: VC6 /W3 clean; town1 -fast = Mac (pop 4,963, money 1,522, world 756ae986); bal_planner_s7_era.txt 1955-1990 = Mac
  (pop 11,696, money 12,896); BENCH20K 59.81 fps (1 s min 59.0, 19 missed frames, worst 25.2 ms; speed 3 keeps up; phase 9
  max 15.3 ms); -test menu 800 + 640 and -test ui 800 pass; 1024 starts 32-zoom only with zoom-in disabled.

## PL1 guidance & information (done) — src/advice.*, tests/test_pl1.cpp, tests/out/pl1_shots/, tests/out/pl1_advice.txt
- advice.* (read-only to sim; chunk ADVC via era_save): ranked Cabinet advice with click targets; special edition ≤ 1/year
  (not December, problem ≥ 2 months); hindsight tallies + yearly "In hindsight" + 1990 verdict; progression/unlock
  sentences and building purposes from defs; power network summary.
- Newspaper: clickable "From the Cabinet" box; NEWS_SPECIAL "Extra edition"; ceremony lists unlocks + next goal; newsreel
  "What the history books will say". ERA v2. Ticker: power near capacity, town rights within reach / reminder every 6 months.
- UI: top-bar Power field (click = power overlay; 640 short labels), § for town rights, "Next: X at N (you have M)" with a
  progress bar, fly-out footer with unlock sentence or purpose (POP_H 216), plant query = network load/spare/brownout,
  Services mini key (E), Guide key + F4 (pauses). Hints for Guide/desk/newspaper; status reached → ticker what's new.
- Script: `guide`, `panel N`, `flyout K`, `news special`, `advice`. Saves PT2_1990.KAU / PT2_1992.KAU = user's play-test 2.
- Left for PL3: tall Cabinet box can cut column stories; newsreel "Population: 0 in 1955" (first yearly row empty, M7).

## PL4 performance & art layout (done) — docs/PERF.md section 7, art/ART.md 4/7/8, tools/vramprobe, tests/test_pl4perf.cpp
- Voodoo3 driver = one linear heap + two screen-sized blocks (1024: 9,088 + 2 × 1,536 KB); tall 640x2048 sheets only fit the
  linear heap. Art now = trimmed column pieces on **512x256 pages** (`gen_art.py --repack`), cuts at existing strip
  boundaries, pixel-identical (2,320 sprites). 64-zoom 7,691 KB (31 pages), 32-zoom 1,918 KB (8 pages). art_blit_part.
- VRAM free (64 / 32 zoom): 640 3,253 / 9,026 KB; 800 2,062 / 7,835 (pool 35/35); 1024 522 (pool 41/48) / 6,295.
  At 1024 close zoom every 64 KB of new 64-zoom art costs one chunk.
- Benches VM B (speed 3, `-fps 60` for benches): 59.8-59.9 fps all resolutions/zooms; 1024 z64 now works.
- Phase 9 avg 9.35 → 4.7 ms but **peak ~15 ms remains** (network rebuild + heat/sewage in one step) — **orchestrator: accepted
  for 1.0** (occasional dropped frame at minimum spec only; covered by the 30 fps fallback; ~1.5 ms on GHz targets).
  Phases 0/1/13/15 peak 7-9 ms. Results bit-identical.
- 13 complex-module icons missing in the art selftest (pre-existing; PL3/cleanup).

## PL2 gameplay fixes & balance (done) — tests/test_pl2.cpp, BALANCE.md section 9, tests/out/balance_era_*.txt
- Water tower + element factory unlock at **Church village** (factory from 1960); T2 still needs elements, T3 heat.
- Railway by default for New Game/direct starts (`-rail`/`-norail`; `-script` stays rail-off); rail-less maps/saves:
  Valtion Metalli accepts a main road within 2 tiles, PM3 skips the station; clear reasons; station locked "No railway here".
- Human pacing: naive-human bot; shops can grow in back rows when C bar ≥ 40 and workers free; power/water-only shortage
  drops a tier after 2 months (was 1). Emergency loan 6‰ (was 8) + ~1/60 instalments when treasury ≥ 2,000.
- Overtime upkeep above 125% load (≤ 300%), `budget_overtime_pct(svc)`; apartment tax weights 3/4/5; agreement offer waits
  ≤ 24 months for a card slot; company town = company share of factory jobs ≥ 30%; PM2 wood 70, staffed at 80%;
  bug fix: CityStats.jobs_zone was never filled.
- s7 1955-90: planner Market 1958 / Town 1964, 4,171 mk; dense 15,353 mk (was 106,950), apt T2/T3 66/27; naive Market 1962 /
  Town 1976, 27,095 mk, 1 loan; naive-without-factory Town 1987, 2 loans (recoverable).
- Open: dense c3/r30 rich by 1975 (126k/147k); 13% tax piles money once built out; metal −240/month in oil years.

## PL3 bugs & UX (done) — tests/test_pl3.cpp
- Hover labels hidden under newspaper/Guide, moved beside panels (ui_rect_covered); heat overlay: pipes strong, would-be-heated
  zoned land faint; GAME_VERSION "0.9" on title/pause; newspaper drops low-ranked Cabinet lines to keep stories whole;
  newsreel "founded on bare land in 1955"; motorway autotile follows traffic_link_ok (fixed, not hidden); undo for bus lines
  and gifts (complexes say "(no undo)"); module lots use the complex icon. Windowed pause menu: not reproducible now.
- Left: motorway drag preview may show side joins (cosmetic); no delete-save button; brief junction overlaps; bus upkeep
  ledger line; politics money a month late; a chunk stale 1-2 frames.

## Polish wave verification (orchestrator, 2026-11-04)
- Mac 182/182; gen_art + gen_ui checks pass; VC6 /W3 0 warnings on VM B; town1 headless 0 errors (pop 5,188);
  1955-1990 planner replay (`-seed 7 -preset lakeland`) = Mac exactly (12,188 pop, 4,171 mk).

## W2 winter art (1.1, done) — art/artlib.py, art/art_terrain.py, art/gen_art.py, src/art.*, render.cpp, ART.md 9
- Generator: `winter` / `ab_winter` shade modes on the same G-buffer (alpha identical, enforced); pages
  `art/world<k>_<z>w.bmp` share the summer piece table (`art_world_winter[]` in art_world.h); summer pixels = 1.0
  (one 32-zoom piece split, page sizes unchanged). `--check` verifies alpha identity per page and every winter sprite
  re-assembled from the winter pages; `--mockups` writes the winter previews only. User approved the mockups as is.
- VRAM +0 KB at every resolution x zoom (same surfaces): 64-zoom 7,691 KB, 32-zoom 1,918 KB; 1024 z64 keeps 522 KB free
  in winter, 800 z64 2,062 KB (VM B logs in tests/out/w2/).
- art.cpp: Plan A = per-page delta of the other season (3,887 KB); `art_set_season` in two frames (px swap 37 ms, then
  upload 128-141 ms at z64 / 32 ms at z32), other zoom lazily at its residency; Plan B (process memory > 40 MB,
  VirtualQuery) disk reads ~850 ms; Plan C INI `winter_art=0` (config.cpp) or missing pages. Process memory 19.8 MB
  before art, 30.0 MB with (1.0), 34.0 MB with the delta.
- render.cpp `season_poll()` (one poller of `season_changed()`), `-winter` / `-summer` switches, script `artseason N`
  (game.cpp); after a swap: chunk flush, ghost copies dropped, minimap colours rebuilt (ui.cpp mm_update); lake and
  shore tiles stop shimmering in winter (rivers keep it).
- Open: bridge water is ice also over rivers; no Settings checkbox for "Winter scenery" (INI only); no snowflake
  date glyph (UI sheet untouched).

## W2 winter art (done) — art/ART.md section 9, tests/out/w2/
- Winter pages `art/world<k>_<z>w.bmp` with exactly the summer layout/piece table (checked per page); `art_world_winter[]`;
  `art_set_season` with plans A/B/C; render polls the season, flushes caches, no shimmer on ice; minimap rebuilt after swap;
  INI `winter_art`, `-winter`/`-summer`, script `artseason N`. Summer art pixel-identical to 1.0; VRAM unchanged.
- Swap split over 2 frames (37 ms + 128-141 ms at z64); other zoom swaps on next zoom (zoom-in 174 ms).
- Heap: 30.0 MB (1.0) → 34.0 MB with winter (only differing pixels kept, 3.9 MB). **Orchestrator: ART_HEAP_BUDGET_KB 40 MB
  accepted**; README now says ≤ 40 MB. Strict 32 MB would mean ~850 ms disk reloads per season change.
- Open (polish): bridge over a river shows ice; no Settings checkbox for winter scenery (INI only); no snowflake on the date;
  `winter_art=0` untested on the VM.

## W1 winter simulation (done) — src/season.*, tests/test_winter.cpp, tests/out/winter_summary.txt
- `season_is_winter()` (1 Dec-31 Mar, pure date); `season_changed()` single poller (renderer). BL_WINTER ledger "Winter:
  ploughing and frost repairs". Power/heat allocated against winter use; pumps/wells scaled in hard frost; T3 heat check on
  the peak (`civic_heat_spare_peak()`); fire × season_fire_pct; chunk SEAS v1; ticker forecast/first snow/thaw (all levels,
  flavour), candles/frozen pipes/cold radiators/peaks (Normal/Hard); 1 Oct + 1 Nov ticker warning when the peak > supply.
- All values from D1's `diff()`. Easy: neutral hooks, no RNG, planner replay = 1.0 (12,188 / 4,171).
- Hard winters 1955-89: Normal 18%, Hard 38%, 1986-87 always hard. VM B Normal replay CSV = Mac.
- Not built (cut list): coal-ship card, ice-road filler, minimap snow. Grace years for hard winters → D1 asked.

## D1 difficulty (done) — src/hard.*, g_diff[3]/diff() in defs, BALANCE.md section 10, tests/out/balance_levels_summary*.txt
- SimSettings.difficulty (tests/direct starts Easy; New Game default Normal; `-difficulty easy|normal|hard`); SIM chunk v3;
  1.0 saves load as Easy. Levers wired in budget/sim/politics/trade/fire. Sweden 1969-70 (DT_SWEDEN, chunk HARD), era
  expectations (REQ_LIBRARY 1975 tier-3 homes; clinic for apartments T2+ 1980; shown a year ahead, 12-month grace).
  Guide page 2 "The Presidency"; level in pause menu/date tooltip; lever notes across budget/demand/query/desk/trade panels.
- Deviations: Hard upkeep 105% (spec 115), index cap 150% (160), tax yield 95% (90), overtime from 110% (105); surcharge and
  yield cut phase in 1960-65; towns start at the level's tax tolerance. Grace: no hard winter in a game's first two winters.
- s7 1990: planner Normal 12,268 / 67,886 mk 0 loans, Hard 11,790 / 37,387 3 loans cleared; naive Normal −84k 6 loans,
  Hard at loan cap. **Open for B1:** naive doesn't recover on Normal; quiet years 13/15 (target ≤ 6) → coasting persists;
  Sweden loss 3.1% (target 4-8%); lot-query decline countdown ignores the slower utility-only decline.

## B1 balance across levels + 1.1 polish (done) — BALANCE.md section 11, tests/test_b1.cpp, tests/out/balance_levels_summary*.txt
- Measurement: quiet years now read the Cabinet at the month tick (before the bot answers) by calendar year; year tables
  end with tax / R / winter forecast + peak / dark months / warning kinds. Tuning aids: `LAHIO_B1_ONLY`, `LAHIO_B1_DEBUG`,
  `LAHIO_B1_JITTER` (test-only; single runs are chaotic, check s7/c3/r30 + jitter before believing a number).
- Normal: upkeep x100 % (was 105), index cap 135 % / 1974 (145 / 1978), power/water-only decline 8 weeks (6), emergency
  loan after 9 months, +15,000, instalments only from the month's surplus (`loan_after`, `loan_cushion100`,
  `loan_surplus`; Easy/Hard = 1.0 rule), hard winters 25 % (20). All levels with winter: electric heating +1 point per
  winter from 1975 (`winter_grow_max`, `season_winter_load()`). Sweden squared (one missing reason 1/9, two 4/9), Normal
  5 / Hard 7 per mille. 1985 expectation: tier-3 homes of a Town need its health centre (`REQ_HEALTH_CENTRE`,
  `HX_HCENTRE`, `hard_hcentre_ok()`, one per 20,000; market towns exempt), paper 1984/1985, Cabinet line, checklist.
- Cabinet (advice.cpp, all levels; read-only, Easy hashes unchanged): `ADV_OVERTIME` (biggest fire/police/library/post
  bill + its cure; school/clinic/sauna lines append the bill), `ADV_MONEY_YEAR` (Jan-Feb: last year's books in the red),
  money lines say what a point of tax brings; `budget_overtime_mk()`.
- Lot query decline countdown uses the utility threshold for power/water-only lots (sim.cpp `sim_query_lot`).
- Winter polish: Settings "Winter scenery" checkbox (INI `winter_art`; off = summer art from the next frame, on again =
  snow at once if it was loaded at start, else "(next start)"); bridge water open in winter (art_terrain
  `g_bridge_water`, 5 winter pages regenerated, summer pages and headers byte-identical); snowflake drawn by the date
  Dec-Mar (darker in a hard winter) + "Winter (hard)" in the date tooltip; Guide page names electric heating and the
  Normal loan.
- Season swap: step 2 uploads pages in frames of <= 70 ms (`ART_SWAP_FRAME_MS`, art.cpp; cancel re-uploads pages already
  sent), chunk flush after the last page; minimap colours rebuilt ~6 ms a frame (`render_minimap_prepare`, render.cpp:
  the full rebuild was a 226 ms frame right after the 135 ms upload frame; BENCH20K 800 z64 max frame 238 -> 87 ms).
  VM B benches (PERF.md 8): BENCH20K + PT2_1992 at 800/1024 x z64/z32, 4 swaps each: max frame 75-100 ms, avg 59.2-59.6;
  `winter_art=0` tested: no swaps, 59.82 fps. Settings checkbox seen in the menu self-test (VM B, 7 shots, 0 failures).
- Results s7 1990: planner Normal 12,226 / 57,694 mk, 0 loans, quiet 4 (D1 13); naive Normal 17,244 / -4,438, 4 loans
  (D1 9,773 / -84,328, 6); Sweden naive 5.2 %, planner 1.1 %; Hard planner 2 loans cleared. VM B replays (planner/naive x
  Normal/Hard, Easy planner) = Mac exactly. VC6 /W3 0 warnings.
- Open: naive Normal 3-4 loans (target 2; ends near zero, repaying); mill/metal/politics s7 quiet 10-11 (rich, politics
  off, pre-empt everything; 4-8 on c3/r30); Hard naive/greedy/tax13 and r30 Hard politics/dense at the loan cap.

## B1 balance across levels + 1.1 polish (done) — BALANCE.md §11, PERF.md §8, tests/test_b1.cpp, tests/out/b1/
- Normal: upkeep ×100%, index cap 135% (1974); utility-only decline 8 weeks; emergency loan after 9 months red, +15,000,
  instalments from surplus only. Hard keeps 1.0 loan rule, upkeep 105%, cap 150%.
- Late pressure (Normal/Hard): electric heating +1 pt winter load per winter from 1975; Normal hard winters 25%; 1985
  expectation: tier-3 homes of a Town need its health centre (announced 1984, grace). Cabinet: biggest overtime bill, "books
  closed in the red", tax-point value. Sweden pull squared (5‰ Normal, 7‰ Hard). Decline countdown fixed.
- Winter polish: Settings "Winter scenery" (winter_art), winter_art=0 tested, open water under bridges, snowflake by the date,
  season swap ≤ 70 ms per frame, minimap rebuild spread (~6 ms/frame).
- s7 1990: planner Normal 0 loans, quiet years 4 (was 13); naive Normal −4.4k, 4 loans (target ≤ 2, repaying); planner Hard 2
  loans cleared; mill/metal/politics still 10-11 quiet years on s7. Benches with season swaps: worst 75-100 ms, 59.2-59.6 fps.
- Cosmetic (orchestrator saw): a stray ":" before the date in the top bar (old, since M1).
