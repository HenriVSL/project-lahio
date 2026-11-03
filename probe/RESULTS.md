# Kaupunki render/tech probe: results (2026-11-02)

All numbers come from real VC6-compiled code (`probe.cpp`) running in the 86Box Win98 VM, with nothing else
running, no VNC viewer connected, and the lock held via `samba/vmlock` (session `86box-86-probe`).
Raw output is in `RESULT.TXT`. `RESULT-run1.TXT` holds the first runs, including a free-running blit sweep
that turned out to be unreliable (see "What failed").
Screenshots are in `shots/` (800x600 crops of the Win98 screen).

## Setup

| Item | Value |
|---|---|
| VM | 86Box 6.0, Pentium II 233 (dynarec), 128 MB, 3dfx Voodoo3 (14.2 MB DirectDraw VRAM), Win98 SE, DirectX 6.1 runtime |
| API | DX3-style `IDirectDraw` / `IDirectDrawSurface` (VC6 DX5 headers), `BltFast` + `DDBLTFAST_SRCCOLORKEY`, `Blt` for stretch/ColorFill |
| Format | 16 bpp RGB565 everywhere. 24-bit BMPs loaded with `LoadImage(LR_LOADFROMFILE)` and copied into VRAM surfaces with `GetDC` + `BitBlt`. Magenta becomes exactly 0xF81F (checked by reading pixel 0,0 back: no mismatch) |
| Art | `mkart.py` (deterministic Pillow): 2:1 diamonds 64x32 and 32x16 (16 kinds plus 4 pre-tinted copies), checker overlay diamonds, houses 64x48, mid-rise 64x96, towers 64x160, 2x2 blocks 128x128, trees 64x64, cars 16x12. Every sprite exists at both zooms, drawn natively (not downscaled). `art.h` holds the rectangles |
| Map | 256x256 tiles: road grid every 8 tiles, lakes, buildings on 60% of the free tiles (40% houses, 18% mid, 10% towers, ~8% 2x2, rest trees), smooth overlay field with 5 levels, ~100 cars on the roads near the camera |
| Renderer | Two passes. Pass 1 draws the ground diamonds (flat, so order doesn't matter). Pass 2 draws buildings in painter's order by diagonal row `s = tx+ty`, with the cars bucketed into their row. The camera scrolls 2 px/frame diagonally (120 px/s at 60 fps) |
| Timing | QPC. Each run: 1 s warm-up, then 6 s measured, then the exe exits. `draw` = CPU time issuing the frame's blits; `pres` = Flip/Blt call time (in fullscreen this is mostly waiting for the blitter and the vblank) |

**The 86Box Voodoo3 blitter is asynchronous.** Blit calls only queue work, and the Flip waits until the queue
has drained. So CPU work placed between issuing the blits and calling Flip is free, up to the blitter time.
Fullscreen Flip is locked to the 120 Hz emulated refresh, so frame times snap to multiples of 8.33 ms
(120 / 60 / 40 / 30 fps).

## 1. Resolutions and VRAM

| Mode | Sets? | VRAM free after primary+back | Widest VRAM offscreen surface | Tallest tested |
|---|---|---|---|---|
| 640x480x16 fullscreen | yes | 14034 KB | 640 (800 fails) | 640x2048, 64x4096 ok |
| **800x600x16 fullscreen** | yes | **13258 KB** | **800** (1024 fails) | 800x2048 ok |
| 1024x768x16 fullscreen | yes | 12162 KB | 1024 (1280 fails) | 1024x2048 ok |
| windowed on 800x600 desktop | yes | 14246 KB (minus your back surface) | 800 (= desktop width) | 800x2048 ok |

- Modes up to 2046x1536 are enumerated. 800x600 and 1024x768 set and run with HW blits without problems.
- **The width limit is the mode width** (`CreateSurface` returns E_INVALIDARG 0x80070057). Height has no
  practical limit (64x4096 worked). System-memory surfaces of any width work.
- **VRAM allocation packs exactly:** 1644 64x64 surfaces used exactly 13152 KB, so small surfaces waste no
  stride. Filling VRAM gave 12.0-13.1 MB usable at 800x600 and 11.8-12.2 MB at 1024x768.

## 2. Per-blit cost (`sweep`, 800x600 fullscreen, K blits per frame + Flip, cost = slope between K values)

| Blit | us/blit | Notes |
|---|---|---|
| Flip only | 8.28 ms/frame | 120 Hz |
| BltFast 64x32 keyed (tile) | **11.7** | 175 Mpix/s |
| BltFast 64x32 opaque | 10.3 | keying is nearly free |
| Blt (not BltFast) 64x32 keyed | 12.1 | |
| BltFast 32x16 keyed | **4.9** | small blits cost the per-call overhead |
| BltFast 16x12 car / 8x12 glyph | 4.8 | per-call floor is about 4.5-5 us |
| ColorFill 8x8 / 32x16 | 4.3 / 4.5 | |
| BltFast 64x48 / 64x96 / 64x160 keyed | 17.8 / 33 / 89 | |
| BltFast 128x128 keyed | 136 | |
| BltFast 256x128 opaque (chunk) | 238 | |
| BltFast 512x256 opaque (chunk) | ~1200 | large blits are about 8-15 ns/px |
| full-screen 800x600 opaque blit / ColorFill | ~3.4-3.8 ms | |
| Blt stretch 64x32 to 32x16 keyed | 5.6 | |
| Blt stretch 512x256 to 256x128 | 188 | |
| Blt stretch 256x128 to 512x256 (2x zoom-in) | 812 | |
| BltFast 64x32 from a system-memory surface | ~100 | 8x slower than from VRAM, and one stall of 1.15 s per blit was seen |

**Cost model:** about **4.5-5 us per blit call + about 8 ns per pixel** (roughly 125 Mpix/s fill, keyed or not).
The CPU spends about 5 us issuing each BltFast (6.6 us for a stretched Blt). Small sprites are limited by call
count, big ones by fill rate. This is emulator behaviour; a real Voodoo3 fills much faster, but the per-call
CPU cost will be similar on a real P2.

## 3. Isometric workload: full redraw every frame (fullscreen unless noted)

| Config | Blits/frame (ground / bldg / cars / ui) | fps | CPU draw ms | Notes |
|---|---|---|---|---|
| 640x480, 64x32 | 591 (350/171/66/4) | 56 | 3.1 | blitter-bound |
| **800x600, 64x32** | **874 (531/260/79/4)** | **56-60** | 4.5 | ~2.4 Mpix of fill per frame |
| 800x600, 64x32, ground only | 535 | 60 | 2.8 | |
| 800x600, 64x32, `mode=draw` (no flip, throughput) | 871 | 51 | 19.4 incl. stalls | true cost about 19 ms/frame |
| 800x600, 64x32, windowed (popup) | 872 | 60 | 16.7 | worst frame 345 ms (free-running stall) |
| 640x480 windowed (captioned window) | 591 | 75 | 13.3 | worst 398 ms |
| 1024x768, 64x32 | 1356 (847/412/93/4) | 38 | 7.0 | |
| 800x600, 32x16 pre-scaled | 3031 (1994/939/94/4) | 51 | **15.2** | CPU-bound on call count |
| 1024x768, 32x16 pre-scaled | 4838 | 31 | 24.2 | |
| 800x600, 32x16 via **hardware stretch** of 64 art | 3033 | 48 | 20.1 | Blt costs more CPU than BltFast |

**Full redraw is not viable.** It's borderline at 64x32 (just 60 fps, with the blitter fully busy) and drops to
30-50 fps zoomed out or at 1024x768.

## 4. Chunk caching

The chunk cache is a pool of VRAM surfaces holding pre-rendered map rectangles on a fixed world-pixel grid,
LRU-replaced, rendered when they first become visible.
`cache=1` caches the ground (+ overlay); buildings and cars are drawn every frame.
`cache=2` caches ground + buildings; cars are drawn on top.

| Config (800x600 unless noted) | Blits/frame | fps | CPU draw ms | Chunk renders while scrolling | Cost per chunk render |
|---|---|---|---|---|---|
| cache=1, 256x128 | 368 | 59.5 | 2.0 | 3.7/s | 0.23 ms (40 blits) |
| cache=1, 512x256 | 352 | 59.7 | 1.9 | 0.8/s | 0.76 ms (144 blits) |
| **cache=2, 256x128, 64 px** | **115** | **119.9 (cap)** | 0.64 | 8.3/s | **0.40 ms** (67 blits) |
| cache=2, 512x256, 64 px | 100 | 118.8 (cap) | 0.54 | 2.5/s | 1.2 ms (225 blits) |
| cache=2, 512x256, cars hidden behind buildings (`occl=1`) | 254 | 118.6 (cap) | 1.9 | | |
| cache=2, static camera | 104 | 119.8 | 0.58 | 0 | |
| cache=2, windowed popup (uncapped) | 99 | 168 | 5.9 | | worst frame 1018 ms (free-running stall) |
| cache=2, 32x16, 512x256 | 123 | 119.1 (cap) | 0.66 | 2.5/s | 4.1 ms (800 blits) |
| cache=2, 32x16, 256x128 | 138 | 119.6 (cap) | 0.75 | 8.7/s | 1.2 ms (224 blits) |
| cache=2, zoom-out by stretching 64-px chunks to half | 136 | 117.6 (cap) | 0.93 | 7.7/s | needs **41 512x256 chunks = 10.5 MB** (only 1.7 MB VRAM left) |
| cache=2, 1024x768, 64 px | 118 | 118.7 (cap) | 0.64 | 3.5/s | 1.2 ms |
| cache=2, 1024x768, 32 px | 126 | 118.4 (cap) | 0.69 | 3.5/s | 4.2 ms |
| cache=2, 640x480 | 79 | 119.5 (cap) | 0.45 | | |

- **cache=2 hits the 120 Hz cap in every mode and zoom** and uses under 1 ms of CPU per frame.
  The cars-behind-buildings fix redraws the buildings in front of each car, clipped to the car's rectangle:
  about 1.5-2 small blits per car, still capped.
- Pool sizes: visible chunks + one row/column of margin. At 800x600 that is 41 chunks of 256x128
  (2.6 MB) or 19 chunks of 512x256 (4.9 MB).
- **Player builds something:** re-render the 1-4 chunks under the changed tile's sprite rectangle. That costs
  0.4 ms per 256x128 chunk (64 px) or 1.2 ms (32 px), so up to about 5 ms in the worst case. Do it within
  the frame.
- Chunk surfaces must also obey the width limit (fine at 256 or 512).

## 5. Overlays (no alpha blending available)

| Technique | 800x600 full redraw | Baked into the cache=2 chunks | Look (`shots/`) |
|---|---|---|---|
| **Pre-tinted ground variants** (`ovl=tint`, 4 levels, 50% blend) | 60 fps, **+0 blits** | free | best: smooth, roads and texture stay readable (`b0_tint.png`). Costs 4 extra copies of the ground tiles in VRAM |
| **50% checker-dither diamond, keyed** (`ovl=checker`) | 46 fps, +311 blits | free; chunk render 1.9 ms vs 1.2 ms | nearly as good: the dither reads as a blend at 800x600 (`b0_checker.png`). Needs only one small diamond per level |
| ColorFill dot, 1 rect/tile (`ovl=fill`) | 57 fps, +276 | | blocky rectangles, hides the ground (`b0_fill.png`) |
| ColorFill diamond, 3 rects/tile (`ovl=fill3`) | 53 fps, +827 | | stair-stepped blobs, ugly (`b0_fill3.png`) |

With 60% building coverage the ground overlay is mostly hidden behind buildings (`ovl_tint.png`,
`ovl_checker.png`). In overlay mode, draw buildings low or flat (for example tinted footprint diamonds or roofs only).
That is a design decision, and it also makes the overlay chunks cheaper to render.

## 6. Zoom

- **Pre-scaled art (64x32 and 32x16)** is clean (`shots/z32_pre.png`). It costs 1.25x the VRAM of the 64 art
  alone. With the chunk cache the zoomed-out view runs as fast as zoom 1 (120 cap). Only the chunk render is
  heavier (1.2 ms per 256x128).
- **Hardware stretch** (`Blt` with a smaller destination) is **point-sampled** (no filtering, `ARITHSTRETCHY=0`):
  window rows and the dither alias into noise (`shots/z32_stretch.png`). Per sprite it is as slow as pre-scaled
  art without the cache (48 vs 51 fps, more CPU per call). Stretching cached 64-px chunks needs 4x the chunk VRAM
  (10.5 MB), so it doesn't fit.
- **Magnifying** with stretch (256x128 to 512x256, 0.8 ms) gives clean doubled pixels. That is a cheap optional
  2x "close-up" zoom of cached 64-px chunks, needing only a quarter as many visible chunks.

## 7. UI layer (160 px side panel: background, 16 icons, minimap, 14 text lines; 16 px top bar with 50 chars)

| Mode | UI blits/frame | Cost |
|---|---|---|
| redraw every frame (`ui=1`) | 225 (mostly glyphs) | about 1.1 ms CPU per frame; with cache=2 still 120 fps |
| **compose into VRAM panel/bar surfaces when dirty** (`ui=2`) | **2** | 1.2 ms per recompose (here every 30 frames) |

The minimap rebuild (128x128, software into a system-memory surface plus upload) took 20 ms in the probe because
`conv()` and divisions run per pixel. Use a precomputed colour table and update it incrementally or once per second.

## 8. Mouse picking

A 64x32 "mouse map" lookup (cell = (x>>6, y>>5), then a 5-region mask byte) takes **0.077 us** per pick and gave
**0 mismatches over 1,000,000 random points** against the exact float formula (1.2 us). Picking tiles is trivial.
Picking tall buildings needs a front-to-back test of sprite rectangles plus the keyed mask; that is also cheap.

## 9. Simulation CPU budget (`sim`, same exe, no rendering)

| Kernel | 128x128 | 256x256 |
|---|---|---|
| multi-source BFS distance map over roads (64 sources, 22% roads) | 0.85 ms | 3.4 ms |
| flood fill power connectivity (73% conductive, explicit stack) | 2.4 ms | 9.7 ms |
| box blur 3x3, separable, row order (byte map) | 1.26 ms | 4.9 ms |
| box blur 5x5, separable | 1.29 ms | 5.0 ms |
| box blur 3x3, naive 9-tap | 1.75 ms | 7.0 ms |
| per-tile update (8-byte tile, ~25 ops with branches) | 3.3 ms (201 ns/tile) | 13.7 ms (209 ns/tile) |
| **full tick** (BFS + flood + blur + update) | **7.7 ms** | **31.5 ms** |

Memory: 128 MB VM; Windows reports only ~8 MB physically free at idle because the disk cache fills RAM, and it
gives that back. Allocating and touching 8 MB blocks ran at full speed (about 90 ms per 8 MB) up to 72-80 MB. Past
80 MB it slowed to 25-30 MB/s (paging). **Budget the game at 32 MB or less of heap**; 64 MB is the ceiling.

**CPU/blitter overlap measured:** full redraw plus 10 ms of busy CPU work between issuing the blits and Flip still
ran at 59.5 fps. cache=2 with 8 ms of work ran at 113 fps. At a 60 fps cap the cache=2 frame (cars hidden behind
buildings, cached UI) left **13.4 ms idle CPU per frame**. At a 30 fps cap it left 30.2 ms.

## What failed / gotchas

- **Free-running blit micro-benchmarks are meaningless on 86Box.** Queueing thousands of blits without a Flip
  triggers blitter stalls of 13 ms to over 1 s per blit (see `RESULT-run1.TXT`). Windowed runs with no frame cap show worst
  frames of 0.35-1 s. **Always present every frame and cap the frame rate** (fullscreen Flip does this for you).
- Offscreen VRAM surfaces wider than the display mode fail (E_INVALIDARG), in fullscreen and windowed alike.
- Keyed blits from **system-memory** sources are 8x slower and once stalled for 1.15 s. All art belongs in VRAM.
- Hardware stretch is point-sampled; shrinking looks bad.
- `RESULT.TXT` lines from the screenshot runs (`... cap=60 t=6` / `t=14` with `ovl=`) are **not valid timings**.
  Mac `screencapture` slows 86Box while it runs (for example tint fell from 60 to 26 fps).
- The first run after a while loads art slowly (1.4 s cold over SMB, about 90 ms warm).
- The emulator is not real hardware. A real Voodoo3 fills far faster (the ~8 ns/px will shrink), while the
  ~5 us CPU cost per blit call is a P2-233 driver cost and will roughly hold. Re-check on the X41/T40.

## Recommendation for the Kaupunki renderer

1. **Resolution:** **800x600x16 fullscreen** (DirectDraw exclusive, 1 back buffer + Flip), with the same code
   windowed on an 800x600+ desktop for development (`-window`, frame-capped). 1024x768x16 also runs at the cap
   with the cache, so offer it as an option. Keep 640x480 as a fallback.
2. **Tiles / zoom:** 2:1 diamonds, **64x32 as the main zoom and 32x16 as the zoomed-out view, both pre-scaled**
   (drawn natively in the art pipeline, no runtime shrinking). Optionally add a 2x close-up by hardware-magnifying
   64-px chunks. A third, smaller level (16x8) is possible because chunks hide its blit count, but its chunk renders
   get expensive (~4x 32-px); use 256x128 chunks there.
3. **Caching:** **cache ground + buildings + overlay in VRAM chunks of 256x128** (pool of visible chunks + margin;
   41 chunks = 2.6 MB at 800x600; about 47 at 1024x768). Render missing chunks as they scroll in (0.4 ms each at 64 px,
   1.2 ms at 32 px; limit to a few per frame and keep one ring of margin). On a change, re-render the chunks under the
   changed sprite's rectangle. Each frame: about 25 opaque chunk blits + dynamic sprites + 2 UI blits + Flip.
   512x256 chunks also work (fewer screen blits) but give 1.2-4 ms render spikes and need 4.9 MB.
4. **Dynamic objects** (cars, people, smoke, cursor) are drawn on top of the chunks. Fix occlusion by redrawing the
   buildings in front of each sprite clipped to the sprite rectangle (~2 blits per sprite).
5. **Overlays:** bake into the chunk cache (flush the pool when the overlay changes: the visible ~30 chunks re-render
   in about 12-20 ms, a one-frame hitch). Use **pre-tinted ground variants** if VRAM allows, or the **50% checker-dither
   diamond** (one tiny diamond per level, nearly the same look). Show buildings low/flat in overlay mode. Don't use
   ColorFill shapes.
6. **Blit budget:** CPU about 5 us per BltFast call; emulated blitter about 4.5 us + 8 ns/px.
   Plan **at most ~300 dynamic blits per frame** on top of the chunk layer at 60 fps. Absolute limits measured:
   about 900 blits / 2.4 Mpix per frame at 60 fps, about 2500-3000 small blits at 30 fps (CPU-bound).
   Issue all blits first, then run the simulation slice, then Flip: the blitter works in parallel.
7. **Frame rate:** cap at **60 fps** with QPC (or Flip every second vblank). With the cache that leaves about
   **13 ms of CPU per frame for simulation and logic** (30 ms at a 30 fps cap). Never free-run.
8. **Art atlases:** 24-bit BMP + magenta key, loaded once into **VRAM surfaces at most 640 px wide** (so 640x480 also
   works; 800 if 640x480 is dropped). Any height is fine (tested 4096). VRAM packs exactly, so one atlas per
   building class is fine. **VRAM budget at 800x600:** 13.0 MB free, minus chunk pool 2.6 MB, UI caches + font + minimap
   ~0.3 MB, and ~0.5 MB headroom, leaves **about 9.5 MB for all art**. Both zooms together cost 1.25x the 64-px art, so about
   **7.5 MB of 64-px art** (about 600 sprites the size of a 64x96 mid-rise, or about 60 full 640x96 strips). Pre-tinted
   ground: 4 KB per 64x32 tile per level. At 1024x768 subtract about 1.1 MB more. Keep nothing in system memory
   that is drawn every frame.
9. **UI:** panels, toolbar and text composed from pre-rendered bitmaps + bitmap font **into VRAM cache surfaces only when
   dirty**, so each frame costs 2-3 blits. Never use GDI text per frame.
10. **Simulation:** a full tick on a 256x256 map costs about 31 ms and on 128x128 about 8 ms. Slice it across frames
    (for example 1/8 of the rows per frame for the per-tile update; BFS/flood/blur once per sim month spread over a few
    frames). Use separable row-order blurs and explicit-stack flood fills.
11. **Map size:** **128x128 default, 256x256 maximum.** 256x256 costs only about 2 MB of tile data and renders as fast
    (the cache makes map size irrelevant to rendering), but its sim tick needs slicing over at least 3-4 frames at 60 fps.
    512x512 would need about 125 ms per tick; avoid it.

## Re-running

- Art: `python3 mkart.py` (writes `art/*.bmp`, `art.h`). Build: `~/Documents/86box/samba/w98 build 'kaupunki\probe'`.
- Batches (they take and release the VM lock themselves): `./runbench.sh < batchA.txt` (resolution/zoom),
  `batchB.txt` (chunk caches), `batchC.txt` (overlays, UI, CPU overlap, caps, sim), or
  `./runbench.sh "iso res=800 tile=64 cache=2"`. `./runbench.sh "sweep res=800" "surf res=1024"` for blit costs and surface limits.
- Screenshots: `./shotrun.sh name "iso ..." [name "iso ..."]` writes `shots/name.png` (raw 2x capture including the
  86Box chrome; the crops kept here were cut out with Pillow).
- Options: see the header comment of `probe.cpp`. `iso` writes 3-5 lines per run: fps/timing, blits per frame by
  category, chunk statistics, VRAM, and the chunk re-render cost.
