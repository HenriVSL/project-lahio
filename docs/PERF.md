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
