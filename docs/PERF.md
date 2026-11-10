# KAUPUNKI performance (T13)

Minimum spec: the 86Box P2-233 + Voodoo3 VM. All numbers from `bench.txt` (fullscreen, 60 fps target, display
locked to the emulated 120 Hz). The ThinkPads (X41/T40, GHz Pentium M) have far more CPU; re-check blits there.

## 1. How to measure

| What | Command (in `M:\dev\lahio`, VM lock held, no VNC) |
|---|---|
| Dense 20k bench town, scrolling | `lahio.exe -load BENCH20K.KAU -cam 55 55 -bench 60 -speed 3 [-zoom 1] [-res 1024\|640] -log` |
| Lake-heavy view (shimmer) | same with `-cam 22 24` |
| Scripted 5-year session | `lahio.exe -seed 7 -preset lakeland -script scripts\town1.txt -log -bench 250` |
| Car stress (occlusion) | `lahio.exe -test life -bench 30` (writes its own bench.txt) |
| Zoom switch timing at 1024 | `lahio.exe -test render -speed 3 -res 1024 -log -exitafter 12` (log: `render: zoom switch ...`) |
| Occluder self-check | `lahio.exe -test render -log -exitafter 10` (log: `occluder self-check ... 0 differ`) |
| Autosave cost | `lahio.exe -load BENCH20K.KAU -speed 3 -log -exitafter 58` (log: `save: autosave ...`) |

- `-bench` camera: a Lissajous path (~330 px/s peak) around the town hall, or around `-cam X Y` (T13).
- `-zoom 1` now also applies after `-load` (the save's zoom used to win).
- bench.txt has a **T13 section** (game mode): sim steps/s vs. what the speed wants ("keeps up"/"BEHIND"),
  avg/max ms of each of the 16 sim phases over the whole run, one forced structural power+water solve, sprites and
  occlusion blits per frame, chunk renders per reason (visible/dirty/overlay/prefetch/shimmer) with avg ms.
- **BENCH20K.KAU** = the dense bench town (Lakeland seed 7, ~24k people at 1967, 2,300 lots, 13 coal plants,
  industry district). Made on the Mac: `python3 tools/gen_bench20k.py` (writes `scripts/bench20k.txt`), then
  `KAUP_BENCHCITY=1 make -f tests/Makefile.mac T=perf_bench` (12 years headless, saves `BENCH20K.KAU`). Save
  files are portable. (`game_build_script_town(1)` = T05's render test town is only ~800 people: not a sim load.)
- `-repairs N` (debug): cap of non-mandatory chunk renders per frame (default 4). A/B 2 vs 4 at z32: no
  difference (10 vs 8 missed frames per minute, noise).
- Mac: `KAUP_SAVETIME=1 make -f tests/Makefile.mac T=perf_save` = per-module save cost.

## 2. Hot spots: before / after (800x600 unless noted)

| # | Hot spot | Before | After | Change |
|---|---|---|---|---|
| 2 | Phase 10 pollution (terrain recalc on every cleared tree) | max 22.2-22.7 ms (every cycle with growth) | max 1.5-1.7 ms, avg 1.3 | `fields_mark_dirty_rect`: only cells near the changed tiles are recomputed (identical results, Mac test) |
| 1 | `render_flush_sprites` | 3.1 ms / 75 cars (T11); 20k z64 avg 0.96 / max 2.66 ms at 22 sprites | life stress 0.43 avg / 1.50 max ms (38 avg, 91 max cars); 20k z64 0.39 / 1.28 at 22 sprites | tight occluder search (`gather_occluders`), self-check 54,711 sprites, 0 differ |
| 4 | World batch = one bounding rect | dirty chunk renders 2.38/s (20k z64) | 0.53/s same scene | short rect list (8, merge within 3 tiles), per-rect kinds |
| 5 | Minimap rebuild | full rebuild in one frame every 3 s: `tools` 13.5-14.5 ms | 8 rows/frame (~1 ms), one upload at frame start; `tools` max 3.7-4.3 ms | ui.cpp uses `render_minimap_rows` |
| 3 | Full power flood | "10-16 ms" (T04 estimate, Mac x100) | measured 5.6 ms power + 6.2 ms water (forced structural solve, 20k town); phase 9 max 6.5 | **not split**: below the ~11 ms per-step limit, so no frame drop |
| 6 | Yearly autosave (20k town) | 1,090 ms in one frame (serialise 373 + write 717 over SMB) | 215 ms in the frame, file write 0.84 s on a worker thread; waits for 0.75 s without camera movement or drag (max 20 s) | sampled RLE mode estimate, Win32 write thread |
| 7 | 1024x768 zoom switch (art residency) | 180 ms out / 450 ms in (T05) | 37+54 = 91 ms out / 128+13 = 141 ms in (set_zoom + flush render) | `gfx_img_evict/restore`: no malloc + 2 sheet copies |
| 8 | Water shimmer | 0.49 ms per render (20k z64) | 0.34 ms (z64), 0.85 (z32); lake view 31-38 renders/s = 11 ms/s (z64), 35 ms/s (z32) | only the box of a chunk's water tiles is redrawn |

Other notes:
- town1 session: the old 26.8 ms sim spike (pollution) is gone (sim max 5.3 ms); the 40 ms `tools` frame at
  t=239 s is the script's final `dump report/map` file writes over SMB (debug commands, not gameplay).
- The minimap upload stays at frame start (ui_update in game_update, before any blit).

## 3. Final bench numbers (dense 20k town, speed 3, scrolling)

| Res / zoom | avg fps | 1 s min | missed / min | max interval | cache avg/max | sprites avg/max | sim avg/max | speed 3 |
|---|---|---|---|---|---|---|---|---|
| 800 z64 | 59.89 | 59.0 | 11 | 25.2 ms | 0.38 / 3.0 | 0.54 / 11.5* | 0.53 / 12.6 | keeps up (16.0/s) |
| 800 z32 | 59.90 | 59.0 | 9 | 27.2 ms | 0.94 / 5.6 | 1.55 / 11.2* | 0.53 / 9.3 | keeps up |
| 1024 z64 | 59.91 | 59.5 | 12 | 25.0 ms | 0.53 / 3.2 | 0.81 / 1.5 | 0.53 / 6.4 | keeps up |
| 1024 z32 | 59.96 | 58.6 | 1 | 39.9 ms | 1.36 / 7.9 | 2.18 / 3.3 | 0.53 / 6.4 | keeps up |
| 640 z64 | 59.92 | 59.5 | 11 | 25.2 ms | 0.32 / 3.0 | 0.36 / 0.9 | 0.53 / 6.6 | keeps up |
| 640 z32 | 59.93 | 59.0 | 9 | 25.0 ms | 0.77 / 4.0 | 1.00 / 1.7 | 0.53 / 12.8 | keeps up |
| town1 800 z64 (250 s) | 59.89 | 58.0 | 10 | 50.0 ms** | 0.33 / 3.7 | 0.14 / 0.9 | 0.25 / 5.3 | keeps up |

(1024/640 runs: 45 s, before the autosave/RLE changes, which don't touch frames. 800 runs: final exe, after T12's
balance changes, 45-110 cars.) *a single 11 ms sprites frame per run whose interval stayed 16.7 ms: a blit
waiting for the busy emulated blitter or a host hiccup. **town1: the only frame over 27 ms is the script's
final file dumps (t=239 s); before T13 the same run had sim 26.8 ms and cache 4.6 ms maxima.

Every frame over 20 ms is a **present-only stall** (work 1-6 ms, then 22-24 ms waiting for the flip = one extra
120 Hz blank): host jitter of the emulator or a blitter backlog after a frame with several chunk renders
(200-560 blits). No sim step and no CPU section exceeds ~13 ms. Sim cost per second at speed 3 (16 steps, one
cycle): ~33 ms = 3% of the CPU; phase averages: growth 1.0, net 1.9, pollution 1.4, land value 5.1, social 1.3,
coverage 3.5, traffic 5.4, demand 3.0-3.2 ms.

## 4. Code changes (T13)

- `world.cpp/h`: batches keep up to 8 rects (merge within 3 tiles; cheapest pair merged when full; slot WB_MAX is
  the temp, array WB_MAX+1 after T12's fix), listeners called once per rect with that rect's kinds.
- `fields.cpp/h`: `fields_mark_dirty_rect` + cell bitmap; `recalc_terrain` only touches marked cells (falls back
  to the whole map for rects over 1/4 of it). sim.cpp's listener passes the rect. Test
  `perf_incremental_fields_match_full` (town1: same sim_hash as whole-map recalcs every step).
- `render.cpp/h`: `gather_occluders` (see the comment there) + `occluders_selfcheck` in `-test render`;
  `render_region` for partial shimmer repaints (water box per chunk); `render_totals` for the bench report;
  `-repairs N`.
- `ui.cpp`: incremental minimap rebuild (8 rows/frame, single upload when complete; overlay switch/first build
  still immediate).
- `gfx.cpp/h`: `gfx_img_evict` / `gfx_img_restore` (VRAM copy freed/re-created, pixels kept); `art.cpp` uses them.
- `save.cpp`: RLE writes straight into the buffer; mode estimate on 32 sampled 512-byte windows for arrays
  > 16 KB (files stay compatible; size 239,159 vs 239,120 bytes); serialise/write times in the log.
- `game.cpp`: autosave deferred to a calm moment + file write on a Win32 thread (no CRT calls on the thread:
  the VC6 static CRT is single-threaded); bench report hooks; `-cam` as bench centre; `-zoom` after `-load`.
- `sim.cpp/h`: whole-run phase timings (`sim_phase_timing_total`).
- `main.cpp`: calls `game_bench_begin/report`.
- `tests/test_perf.cpp`, `tools/gen_bench20k.py`, `scripts/bench20k.txt`, `BENCH20K.KAU`.

## 5. What's left / watch list

- town1 (final exe): 59.89 fps, sim max 5.3 ms (was 26.8), sprites max 0.9 (was 1.6), one `ui` frame of 11 ms
  (single occurrence, interval unaffected).
- Present stalls (~10 missed frames per minute, 25 ms each) are emulator/host jitter and blitter bursts; a
  blitter-pixel budget for chunk renders could shave a few. Not visible as stutter at 59.9 fps averages.
- Serialising the 20k save still costs ~215 ms in one frame (net 1.1, world 0.85, traffic 0.4 ms on the Mac x ~65).
  Could be spread by snapshotting the large arrays with memcpy and encoding on the thread (needs a CRT-free RLE).
- Shimmer at z32 over big lakes: 35 ms/s CPU. Fine at minimum spec; a slower shimmer at z32 would halve it.
- The `tools` section of a scripted big command (40 ms) is file I/O in debug dumps; a player's big drag commit
  (zone 30x30) was not separately profiled.

## 6. ThinkPad notes

- CPU-side costs scale with the Pentium M (~8-10x the P2-233): sim, chunk renders, occlusion and save
  serialisation become small. The **blitter** is the unknown: the Voodoo3 numbers (keyed blits from VRAM,
  ~4.5 us + 8 ns/px) won't hold for the X41's Intel 915GM or the T40's Radeon 7500; measure `-bench` there first,
  especially at z32 (110 sprites, 56 occlusion blits, 40 shimmer repaints/s) and 1024x768.
- VRAM: the X41's shared memory may report a different free amount; the renderer picks per-zoom residency
  automatically when the pool doesn't fit. Zoom swap now costs ~0.1 s at minimum spec.
- Saves go to the exe directory; on a local disk the autosave thread finishes much faster than over SMB.

## 7. PL4 (polish wave, 2026-11-04): art pages, VRAM, sim spikes

Benches: VM B, `-bench 45 -speed 3 -fps 60 -log` (fixed 60 fps policy: in auto mode a load hitch can drop the run
to 30 fps for the rest of it), BENCH20K `-cam 55 55` unless noted. "Before" = exe built at the start of PL4 from the
then-current sources; "after" = PL4 art + CPU changes (plus PL1/PL2 work in progress). The Mac host was busier
during the "before" runs (load avg ~5), which explains part of the present-stall difference.

| Run | avg fps before -> after | 1 s min | missed frames | max interval ms | VRAM free KB | chunk pool |
|---|---|---|---|---|---|---|
| 640 z64 | 59.39 -> 59.89 | 49.3 -> 59.5 | 42 -> 10 | 40.3 -> 25.0 | 1,428 -> **3,253** | 30/30 |
| 640 z32 | 59.34 -> 59.85 | 46.0 -> 59.5 | 31 -> 14 | 108 -> 25.0 | 8,623 -> 9,026 | 30/30 |
| 800 z64 | 58.32 -> 59.86 | 52.1 -> 59.0 | 109 -> 10 | 46.8 -> 25.1 | 493 -> **2,062** | 31/35 -> **35/35** |
| 800 z32 | 59.83 -> 59.86 | 56.6 -> 59.0 | 24 -> 9 | 31.2 -> 33.5 | 7,688 -> 7,835 | 31 -> 35 |
| 1024 z64 | **not available** -> 59.88 | - -> 59.0 | - -> 12 | - -> 25.1 | - -> **522** | - -> 41/48 |
| 1024 z32 | 54.92 -> 59.83 | 42.0 -> 58.5 | 383 -> 15 | 42.7 -> 33.2 | 5,443 -> 6,295 | 48 -> 41 (sized for z64) |
| PT2_1992 800 z64 | 59.80 -> 59.79 | 58.5 -> 59.0 | 15 -> 17 | 34.8 -> 27.3 | 493 -> 2,062 | 31 -> 35 |
| PT2_1992 1024 z64 | not available -> 59.87 | - -> 59.1 | - -> 13 | - -> 25.2 | - -> 522 | 41/48 |

Sim phases (P2 ms, avg / max over the run; BENCH20K 800 z64; PT2_1992 in brackets):

| Phase | Before | After |
|---|---|---|
| 9 (net + heat/sewage) | 9.35 / 15.5 [6.6 / 9.2] | **4.7 / 15.4** [3.6 / 9.5] |
| 10 (pollution; + the moved traffic slice) | 2.6 / 3.9 | 3.8 / 5.9 |
| 11 (land value) | 6.8 / 8.2 [4.4 / 5.1] | **5.0 / 6.5** [1.9 / 2.5] |
| 0 (month-end books) | 2.6 / 13.3 | 2.7 / 7.4-9.8 |
| 1 (growth band; traffic pass start) | 3.9 / 9.8 | 3.9 / 8.8 (16.1 once: first step after loading) |
| 13 (coverage) / 15 (demand) | 4.7 / 7.1, 5.6 / 7.4 | 4.7 / 7.0, 5.6 / 8.7 (unchanged code) |

Art residency (DESIGN 14.5, one zoom resident): 64-zoom world 9,520 -> **7,691 KB**, 32-zoom 2,326 -> 1,918 KB, UI 655 KB.
1024 zoom swap: 41 ms out / 140 ms in (set_zoom incl. 31 page uploads) + 56 / 14 ms flush render. Art load at start
~900 ms (was ~975). Chunk render cost +2-3% (piece loop): z64 prefetch 0.568 -> 0.582 ms, z32 1.47 -> 1.51 ms.

### What changed
- **VRAM heap geometry** (`tools/vramprobe`, results `r640/r800/r1024a.txt`): the Voodoo3 driver gives DirectDraw a
  linear heap plus two screen-sized rectangular heaps (1024x768: 9,088 KB linear + 2 x 1,536 KB). The 640x2048
  sheets could only use the linear heap, so the 4th 64-zoom sheet failed at 1024 with 4,477 KB "free".
- **Art pipeline** (`art/gen_art.py`, ART.md 8): world art is now trimmed *pieces* (columns on the half-tile grid, cut
  only where render.cpp already splits lot strips unless a cut saves > 600 px) on 512x256 *pages* (31 + 8).
  `--repack` re-packs the cache in 15 s. Verified pixel-exact (2,320 sprites, both zooms, same box/anchor/hotspot);
  a 800x600 screenshot of BENCH20K is identical old vs new in the world view.
- **art.cpp/art.h**: ArtPiece/ArtPSpr/ArtPieceSet, runtime pieces for every sprite, `art_blit_part`, `art_pixels`
  assembles multi-piece sprites, `art_opaque` per piece; up to 64 sheets. **render.cpp**: strips and the occlusion fix
  blit through `art_blit_part`; ghost copies are freed before a residency swap (no hole between pages).
- **Sim (results bit-identical, Mac harness `KAUP_PL4PERF=1 make -f tests/Makefile.mac T=pl4perf`, 36 months with
  edits + save round trips on BENCH20K and PT2_1992)**: traffic sweep slice of phase 9 moved to phase 10
  (`traffic_slice_phase`, only when the slice cannot touch the world); union-find clusters and road-tile list in the
  traffic graph rebuild; civic lake update only on terrain flips, heat/sewage BFS passability on demand with stamps,
  heat apply limited to a box; power flood conductivity on demand + bits box; land value `lv_sum` (same terms/order).

### Left (over ~6 ms per step on the P2)
- Phase 9 **max** ~15 ms in a growing 20k town: when growth or an edit changes the conduction graph, the water BFS
  (7.4 ms) or the full power flood (5.7 ms, alternate cycles) runs together with the heat/sewage solves. Splitting it
  needs either an incremental flood (risk: history-dependent brownout order) or moving civic_step to phase 10, which
  changes results when the player edits between the two steps (not done: identical results were required).
- Phase 0 (books + traffic graph rebuild), phase 1 (traffic pass start after a rebuild), 13 (coverage), 15 (demand):
  7-9 ms peaks. They read the world at fixed phases; moving work needs a road/world snapshot.
- Frames: every frame over 20 ms in the after-runs is either a present stall (work 2-6 ms) or one of the sim peaks
  above (25 ms = one extra blank). 1024 z64 has only ~520 KB VRAM left (pool 41/48): fine, but new 64-zoom art now
  costs pool chunks at 1024 first (each 64 KB of art = one chunk).

## 8. 1.1 B1: benches with winter (VM B, 2026-11-08)

`-bench 60 -speed 3 -fps 60 -log` (60 s = about 15 game months: every run crosses 1 December and 1 April twice), logs
and reports in `tests/out/b1/`. W2's swap uploaded all 31 z64 pages in one frame (128-141 ms) and the minimap colour
rebuild followed in one frame (226 ms): BENCH20K 800 z64 max frame 238 ms. Now art.cpp uploads in frames of <= 70 ms
(`ART_SWAP_FRAME_MS`, chunk flush after the last page) and render.cpp rebuilds the minimap colours ~6 ms a frame.

| Run | avg fps | 1 s min | missed | max interval ms | last swap step (log) |
|---|---|---|---|---|---|
| BENCH20K 800 z64 | 59.26 | 49.0 | 34 | **86.5** (was 238) | 63-68 ms |
| BENCH20K 800 z32 | 59.59 | 55.4 | 28 | 75.0 | 32-40 ms |
| PT2_1992 800 z64 | 59.19 | 46.6 | 40 | 86.9 | 6-64 ms |
| PT2_1992 800 z32 | 59.61 | 53.0 | 23 | 91.7 | 38-42 ms |
| BENCH20K 1024 z64 | 59.29 | 48.6 | 31 | 100.0 | 9-69 ms |
| BENCH20K 1024 z32 | 59.59 | 54.1 | 24 | 99.8 | 32-40 ms |
| PT2_1992 1024 z64 | 59.50 | 50.2 | 12 | 94.1 | 56-70 ms |
| PT2_1992 1024 z32 | 59.56 | 52.6 | 32 | 99.9 | 32-40 ms |
| BENCH20K 800 z64, INI `winter_art=0` | 59.82 | 59.0 | - | 25.2 | none ("winter scenery off") |

No frame over 100 ms at a season change (target ~150). The averages sit 0.3-0.6 fps under PL4's 59.8-59.9 because each
run has four swaps (2-3 long frames each plus the minimap rows); without winter art the run is PL4's. Some runs
overlapped a Mac test run (the emulator shares the CPU), so the 1 s minimum is pessimistic.

## 9. 1.2 A1: power plant art (2026-11-10)

Four new plants (coal_station 3x3, nuclear_plant 3x3, peat_plant 2x2, oil_plant 2x2; the oil plant replaces its coal-plant
alias), both zooms, summer + winter, r0 + r1 rendered (r2/r3 aliases), plus 4 zero-cost `coal_station_*_hot2` rows.

| | Before (1.1) | After (1.2 A1) | Delta |
|---|---|---|---|
| 64-zoom world VRAM (resident at z64) | 31 pages, 1,467 pieces, 7,691 KB | 32 pages, 1,500 pieces, **7,947 KB** | **+256 KB** (headroom to 9.6 MB: 1,883 KB) |
| 32-zoom world VRAM (resident at z32) | 8 pages, 1,435 pieces, 1,918 KB | 8 pages, 1,467 pieces, **1,962 KB** | **+44 KB** |
| UI sheets | 655 KB | 655 KB (icon_b_coal_station added in the existing ui_icons rows) | 0 |
| Winter swap | same surfaces | same surfaces (winter pages keep the summer layout, alpha checked) | **0 KB VRAM** |
| Sprite names / pieces (art.cpp limits 2048 / 6144) | 1,631 / 5,106 | 1,649 / 5,256 | +18 / +150 |
| Process memory before art / with art / with winter delta (VM B log) | 19.8 / 30.06 / 33.97 MB | 19.8 / 30.37 / **34.36 MB** | +0.31 / **+0.39 MB** (winter delta 3,887 -> 3,973 KB; budget 40 MB) |
| Art load at start (VM B, warm SMB cache) | 2,006 ms (winter 1,112) | 2,096 ms (winter 1,154) | +90 ms |
| Season swap at z64 800 (log) | px swap 37 ms + upload 128-141 ms | px swap 36.3 ms + upload 134.7 ms in 2 frames | ~0 |

Piece sizes (64 / 32 KB): coal_station 90 / 22, nuclear_plant 77 / 19, oil_plant 44 / 11, peat_plant 41 / 10
(`python3 art/power_preview.py` prints them). At 800x600 z64 the log shows the chunk pool at 35 x 64 KB with 1,806 KB VRAM
free after the art. At 1024 z64 (522 KB free before) the +256 KB costs about 4 chunk-pool chunks (pool ~37/48 instead
of 41/48); not re-benched here (orchestrator: a 1024 z64 bench run if wanted). The first art load after the new BMPs
were written took 30 s (cold SMB cache, 365 ms per page); a second start was back to 23 ms per page.


## 10. 1.2 X2: rail crossings (2026-11-10)

Eight new lot-kind sprites per zoom (`grav_/road_/main_xing_05/10`, `mway_rail_05/10`) plus two zero-cost header aliases
(`rail_xing_05/10`), summer + winter, art/art_xing.py (ART.md 3.3).

| | Before (1.2 A1) | After (1.2 X2) | Delta |
|---|---|---|---|
| 64-zoom world VRAM | 32 pages, 1,500 pieces, 7,947 KB | 32 pages, 1,508 pieces, **7,988 KB** | **+41 KB** (pieces 34 KB; headroom to 9.6 MB 1,842 KB) |
| 32-zoom world VRAM | 8 pages, 1,467 pieces, 1,962 KB | 8 pages, 1,475 pieces, **1,964 KB** | **+2 KB** (pieces 8 KB, fitted in the last page) |
| Page files | 32 + 8 (summer) + the same winter | unchanged count (no new BMP; the RC package stays at 85 art sheets) | 0 |
| Winter | same surfaces | same surfaces (alpha checked) | 0 KB VRAM; winter delta at most +42 KB of system memory |
| Sprite names per zoom / art.cpp selftest names | 1,176 / 1,649 | 1,186 / 1,659 (`-test art`: only the old `icon_b_patron_villa` missing) | +10 |

Every pre-existing sprite (both zooms, both seasons) re-assembled from the new pages equals the old one (box, anchor,
hotspot, pixels: 4,704 compared). VC6 full rebuild on VM B: 0 warnings. Not benched: one extra 1-piece sprite per
crossing tile replaces the rail tile's flat blit cost-wise like a bridge (object pass, 1 blit at both zooms).
