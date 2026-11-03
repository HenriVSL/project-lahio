# KAUPUNKI: art style guide (exploration)

One deterministic script, `gen_explore.py` (Python 3 + Pillow, no numpy), makes every file in this folder.

    python3 gen_explore.py            # everything, all 3 styles, both zooms, then self-check (~25 s)
    python3 gen_explore.py --quick C  # 64x32 mockup of one style only (~7 s), for iterating
    python3 gen_explore.py --check    # self-check only: BMP format, widths, key purity, seamless tiles

Don't edit the outputs by hand. Change the generator and run it again.

## 1. Direction (user decisions so far)

* **Muted, desaturated, Nordic.** The user said the art must not be too colourful ("that test green looked
  awful"). Grass and forest are grey-green, olive or moss, never lawn green. Terrain must recede so the
  buildings read. Houses keep one weathered, earthy colour each (ochre, red ochre, faded blue, pale green,
  cream). Concrete greys are slightly warm.
* **Cars are the exception.** They may be more colourful than the terrain, because the contrast helps them
  read (section 9).
* Winter and snow are out of scope for now.
* Art quality matters a lot to the user. Models stay simple; shading, palette and readability do the work.

## 2. Files

| File | What |
|---|---|
| `mockup_{A,B,C}.png/.bmp` | 640x480 town scene + period UI, one per style |
| `mockup_C_800.png/.bmp` | 800x600 version of the recommended style |
| `mockup_{A,B,C}_zoom32.png` | the same town at the 32x16 zoom (native render, not downscaled) |
| `tiles_<S>_<64/32>.bmp` | flat ground tiles (64x32 or 32x16 cells) |
| `sprites_<S>_<64/32>.bmp` | buildings, forest/rock lots, trees, power line (packed, <= 640 / 320 wide) |
| `cars_<S>_<64/32>.bmp` | 9 colours x 4 directions of the Lada-style car |
| `sheet_<S>_<64/32>.h` | C header: name, x, y, w, h, anchor x, anchor y for each sprite in the three sheets |
| `preview_sprites_<S>.png`, `preview_tiles_<S>.png` | 2x review sheets |

All BMPs are 24-bit uncompressed (BI_RGB, 54-byte header, bottom-up). The colour key is exactly **RGB(255,0,255)**.
There is no anti-aliasing anywhere, so edges against the key are hard. Every colour is already rounded to
RGB565, so what you see is what the 16-bit game shows. `to565()` also keeps colours away from the key.

## 3. Projection and units

* **World units:** one tile is 32 x 32 units in X and Y. Z is measured directly in screen pixels of height.
* **Screen:** `sx = (X - Y) * zoom`, `sy = ((X + Y) / 2 - Z) * zoom`.
  * +X runs down-right on screen ("SE"), +Y runs down-left ("SW").
  * Tile (i, j) has its north (top) vertex at `((i - j) * 32, (i + j) * 16)`.
* **True 2:1 dimetric:** the camera is at 30 deg elevation, so 1 metric unit of height = `KZ = sqrt(1.5) = 1.2247 px`.
  The ray caster uses the exact projection, so roof pitches, shadows and normals are geometrically consistent.
* **Scale:** 1 tile = 12 m (1 m = 2.67 units), and 1 storey = `STOREY = 10 px` (about 3 m).
  * Rintamamiestalo: 18 x 16 units (6.8 x 6 m).
  * 45 deg roofs: the ridge rises `half_span * KZ` px.
  * Lamellitalo: 90 x 20 units on a 3x1 lot. Pistetalo: 36 x 36 units on a 2x2 lot.
* **Diamond rasterisation:** a pixel belongs to a tile if its **centre** is strictly inside the diamond.
  * A 64x32 tile has rows of 2, 6, ..., 62, 62, ..., 6, 2 px.
  * Tiles placed on the lattice partition the plane exactly, with no gaps and no overlap. `--check` verifies this.
  * The render probe's `inside()` uses the same rule.
* **Light:** metric direction `(0.30, 0.66, 0.70)`, i.e. from the viewer's upper left.
  * +Y faces ("left" faces) are lit, +X faces ("right" faces) are dark.
  * The brightest surface is a roof plane facing left, then flat tops, left walls, right walls, then shadow.
  * Ambient is 0.34. Shadows are ray-traced inside each sprite and fall up-right, behind the object.
  * Contact darkening is applied next to wall bases. These two seat buildings on the ground.

## 4. Sprites: size, anchor, sorting, naming

* **Flat tiles:** exactly 64x32 (or 32x16), anchor (32, 32) = south vertex.
* **Lot sprites** (buildings, forest, rock): an n x m lot is `32(n+m)` wide and `16(n+m) + top` tall.
  * The sprite includes its own lot ground, rendered with the same grass function as the plain tiles.
  * The lot edges therefore match the neighbouring tiles pixel for pixel: grass texture is periodic per
    tile, and per-variant noise fades to zero at tile edges.
  * Shadows, paths and fences live inside the lot.
* **Overlay sprites** (single trees, power line, cars) have no ground and are trimmed to their bounding box.
* **Anchor:** in every header, `(ax, ay)` is the pixel that sits on the **south vertex of the footprint**.
  * To draw a sprite: `x = south_vertex_x - ax`, `y = south_vertex_y - ay`.
  * The anchor may lie outside a trimmed overlay's rectangle (ay > h).
  * **Cars** anchor on the car's own ground centre point instead (section 9).
* **Sorting tall and multi-tile sprites** (implemented in `compose_map`):
  1. Draw all flat ground tiles first.
  2. Cut every object sprite into vertical **strips 32 px wide**, aligned to half-tile columns (column k
     spans `[32k, 32k+32)` from the map origin).
  3. Give each strip the depth `i + j` of the front-most footprint tile that covers its column.
  4. Sort all strips by (depth, column) and blit.

  This is the classic SimCity-2000 slicing trick. It is correct for any mix of 1x1, 2x2 and 3x1 lots that
  don't overlap. Trees that overhang their lot can still be slightly wrong; keep big crowns inside the lot.
* **Rotation** `rN`: models are built in a canonical orientation with the street-facing **front on +Y**,
  i.e. facing down-left. `r1` = front +X (down-right), `r2` = front -Y (up-right: you see the back),
  `r3` = front -X (up-left: you see the back). Every wall has windows, so all four look complete.
* **Names:**
  * Tiles: `grass0-2`, `water`, `shoreMM`, `field_crop`, `field_plough`.
  * Asphalt roads: `road_x`, `road_y`, `road_c_nw_sw` (curve between the NW and SW edges), `road_t_nx`
    (T with the -X/NW arm missing), `road_cross`, ...
  * Gravel roads: `grav_*`, same scheme.
  * Shore index: `shoreMM` uses the corner land bits n=1, e=2, w=4, s=8.
    Example: `shore01` = only the north corner is land.
  * Lots and buildings: `rock`, `forest_{mixed,spruce,birch,pine}`, `house<0-5>_r<0-3>`, `lamelli_gable[_r1]`,
    `lamelli_panel[_r1]`, `pistetalo`, `shop[_r1]`, `factory`, `watertower`.
  * Overlays: `power_x`, `power_y`, `wire_x` (span without a pole, for road crossings), `tree_{birch,spruce,pine,apple,birch_au}`,
    `car_<colour>_<px|ny|nx|py>`.

## 5. Zoom levels

Both zooms are rendered **natively** from the same models. `render_gbuffer(model, zoom=0.5)` ray-casts at half
resolution; nothing is downsampled, so the colour-key edges stay hard. Materials look at `st.zoom` and drop
or widen details that would alias at 0.5:
* board lines, seams and brick are dropped;
* window frames become 2 units wide;
* field stripes and roof seams double their spacing.

The result is in `mockup_*_zoom32.png`. At 32x16 the town still reads: red/yellow houses, white blocks, the factory chimney.

## 6. Palettes (base colour = a face in full light)

Each style builds a **ramp** of `levels` tones from every base colour with `Style.cont()`:
* Darker tones blend toward `base * 0.40` mixed with a cool shadow tint (`hue` = how much).
* Brighter tones blend toward a warm light tint (`hi` = how much).

A pixel's ramp index comes from its light value plus the material's `delta` (in sixths of the ramp). Flat
faces snap to one tone; only texture and gradients add variation. Style C also multiplies saturation by 0.9.

| Material | Keys and hex |
|---|---|
| Grass (muted moss/olive) | grass `#77795a`, grass_dk `#626649`, grass_lt `#8a8a68`, meadow `#86845f` |
| Ground | dirt `#7d6d59`, sand `#b2a688`, pebble `#969188` |
| Lake | water `#4b5f6b`, water_dk `#3f505b`, water_lt `#7a8b91`, shallow `#62726f` |
| Granite (silokallio) | rock `#8c817c`, rock_dk `#6a625f`, lichen `#9a9a80` |
| Fields | crop `#b09f6c`, crop_dk `#958659`, plough `#6c5d4d`, plough_dk `#584c40` (ditch = water_dk) |
| Asphalt | asphalt `#5b5c5c`, asph_dk `#4c4d4d`, roadline `#c4bea0` |
| Gravel | gravel `#9d9283`, gravel_lt `#aea495`, gravel_dk `#83796c` |
| Wood walls | keltamulta `#c4a15e`, punamulta `#88483a`, blue `#8c9ea6`, green `#9fae96`, grey `#aaa9a1`, cream `#d0c5a3`, pink `#c2a294` |
| House trim / glass | trim `#dfdacd`, glass `#3d454d`, glass_lt `#74808a`, door `#685444`, door_green `#56624f`, plinth `#868379` |
| Roofs | roof_metal `#767b7c` (grey standing-seam tin), roof_dark `#55585a`, roof_red `#7a4c41` |
| Brick | brick `#8f5643`, brick_dk `#704536`, chimney cap `#3b3836`, white chimney `#cfcbc0` |
| Concrete (warm greys) | render_white `#d1cdc1`, render_yel `#cdbf9b`, render_grey `#a5a299`, panel `#b5b0a4`, panel_dk `#8a867c`, bitumen `#4e4c4a`, parapet `#99968d` |
| Balcony panels | orange `#a8714f`, blue `#6b8091`, green `#7b8a64`, white `#ccc8bc` |
| Shop / industry | sign_green `#456650`, sign_red `#954739`, sign_txt `#e8e2d2`, awning `#9a5140`, factory_glass `#59646c`, steel `#6b6f71` |
| Trees | birch leaf `#8c955e`, autumn birch `#bb9d52`, spruce `#3c5146`, pine `#56654a`, apple leaf `#65744c`, apple `#a64a3c`, birch bark `#dad6cc` / marks `#2f2d2b`, pine bark `#93684d`, bark `#5f5241`, bush `#5b6849`, lilac `#8b84a0` |
| Misc | fence `#d8d4c8`, pole `#685847`, wire `#b9b6aa` (light: dark wires vanish over asphalt), coal `#2f2e2f` |
| Cars | see section 9 |
| UI | teak `#6b4a32` / `#4e3524` / `#86603f`, bakelite cream `#e6dcc2` / `#c4b896` / `#f4ecd6`, ink `#2a2622`, brass `#b89a5a`, bar `#3a3934`, paper `#ddd3b6` |

## 7. The three variants

Geometry is identical. Only `Style` parameters differ:

| | A: Clean outline | B: Soft textured | C: Nordic hybrid (**recommended**) |
|---|---|---|---|
| Tones per ramp | 6, hard | 12, ordered 4x4 Bayer dither | 8, hard |
| Outline | 1 px near-black on every raised object (not fences or wires) | none | sel-out: darkest tone of the local material, mixed 35% toward blue-black |
| Building texture | almost none (no boards; faint seams) | full: boards, seams, brick, wall gradient, glass glints | boards, seams, brick, glints; no dither |
| Terrain texture | low | full (busy) | **0.45x of the building texture**: quiet, so it recedes |
| Shadow hue | slight | slight | cool blue shadows, warm highlights |
| Reads at 1x | best silhouettes; looks a bit "toy/mobile" | most late-90s (Caesar III / SC3000); grass and water get noisy, which fights the muted brief | buildings pop against calm ground; outlines feel drawn, not stamped |

**Recommendation: C.**
* It fits the user's direction best: muted ground that recedes, earthy single-colour houses that read clearly,
  and coloured outlines that separate objects without the cartoon look of A.
* It avoids B's dither noise, which also costs nothing extra in 16-bit.
* A remains the fallback if readability at the 32x16 zoom turns out to matter more than charm.

## 8. Generator structure (how to extend it)

1. **Solids** (`Convex`, `Cone`, `Ellipsoid`, `HeightField`) each have `hit(O, D)`, an `aabb`, a material
   callback and params.
   * Helpers build them: `box`, `gable_body` (box with a gable top, convex), `roof_slab` / `gable_roof`
     (thick slabs with overhang; their end and fascia faces get the trim colour, i.e. white barge boards).
   * Trees: `add_birch`, `add_spruce`, `add_pine`, `add_apple`, `add_bush`. A crown is a cluster of small
     shaded ellipsoids, so it reads as leaf clumps.
   * Fences: `add_fence`.
   * `grp` groups solids so outlines are only drawn between objects, never inside a tree crown or a car.
   * `cls='detail'` marks fences and poles, which never get outlines.
2. **Model** = an n x m lot, its solids, `ground(g, st)` (the lot ground material) and 3-D `lines` (wires).
3. **`render_gbuffer(model, zoom, rot)`** ray-casts every pixel in world space. The ray is turned into the
   canonical frame for `rot`. For each pixel it stores: hit solid, face tag, canonical point, metric normal,
   light (lambert, ambient, shadow ray), depth, and a contact-AO term. This is the only expensive step
   (about 9 s for all 248 g-buffers), and it is style-independent.
4. **Materials** `m_*(g, st)` / `g_*(g, st)` return `(palette_key, tone_delta)` from the face tag and local
   coordinates (`local_st`: s along the wall, t = height).
   * Windows, doors, bands, panel joints, seams, boards, plinth and corner boards are rectangles and rules in
     wall coordinates, so any building can list `wins={'y+': [(s0, s1, t0, t1), ...]}` per face.
   * Ground functions take world coordinates and use tile-periodic noise (`vnoise`, period 32), so seams
     always match.
5. **`shade(gbuffer, style)`** maps keys to ramps, quantises or dithers, then adds outlines and wires.
6. **Output:** `build_set()` lists every sprite (`put(name, model, rot, group)`). `pack_sheet()` shelf-packs
   them, `write_header()` writes the C header, and `town_map()` / `compose_map()` / `make_ui()` build the mockups.

**To add a building type:** write `def kiosk(name, ...)` returning a `Model`. Use `box`/`gable_body`/`gable_roof`
with `m_block` or `m_house` and a `wins` dict, give the lot a `ground` (e.g. `g_lot` with gravel rectangles),
then add `put('kiosk', kiosk('kiosk'))` to `build_set()`. Variation is through parameters:
* `rintama()` takes wall colour, ridge axis, 1.5 or 2 storeys, chimney position and colour, porch type and
  position, wall dormer (frontoni), roof colour, cladding direction, yard tree and position, red outbuilding,
  fence and bush. Six presets are in `build_set()`; more cost one line each.
* `lamelli()` takes `kind` (`gable` = 1950s rendered block with a low tin roof, `panel` = 1960s precast
  panels with joints, a flat roof and coloured balcony stacks), storeys, colour and balcony colour.

**Conventions to keep:**
* Front on +Y.
* Heights in px, with `STOREY = 10`.
* Window rects 4-5 units wide, 5.5-6.5 px tall, with a 1 px trim frame.
* Never draw with anti-aliasing.
* New colours go into `PAL_BASE` as a base (lit-face) colour, not as ramp steps.

## 9. Cars: Lada 1200 (VAZ-2101) spec

* **Silhouette:** a boxy three-box saloon.
  * Body: 14 x 6.2 units, z 1.2-4.2 px.
  * Long flat bonnet (4.4 units), upright greenhouse (z 4.2-7.4, 1-unit rake front and rear, slight
    tumblehome), short boot (3.2 units).
  * Dark wheels show under the sills. Chrome bumpers front and rear.
  * Front face: two cream round-ish headlamps with a dark grille between. Rear: red tail lamps.
  * Glass all round is dark, with a B pillar; the windscreen top is lighter; the roof is a light tone of the body colour.
  * This is exaggerated about 1.25x like the houses, so the silhouette survives at 1x: about 22x14 px at 64x32,
    10x7 px at 32x16.
* **Directions:** `px` heading +X (down-right), `ny` -Y (up-right), `nx` -X (up-left), `py` +Y (down-left),
  i.e. rotations 0-3 of one model.
* **Colours** (period palette, deliberately livelier than the terrain): white `#e2ded3`, beige `#cbb896`,
  cream yellow `#dcc26a`, orange `#cf7a38`, light blue `#8fb0c6`, dark blue `#37507a`, red `#b03a2e`,
  green `#4f7a4c`, brown `#78533c`. Shared parts: tyre `#262626`, chrome `#c9cccc`, headlight `#f2eed4`,
  taillight `#b0281e`.
* **Recolouring:**
  * The body uses only the ramp of `car_<colour>`. All 9 x 4 x 2 = 72 sprites are pre-rendered, about 50 KB per style (both zooms).
  * For more colours, add a key to `PAL_BASE` and `CAR_COLOURS`.
  * A runtime palette swap would also work, because each body ramp is unique: replace its 8 tones.
* **Anchor:** `(ax, ay)` is the car's ground centre. Blit at `screen(X, Y) - (ax, ay)`.
* **Traffic:** right-hand traffic; lane centre 3.6 units from the road centre line.
  * On a road along X: heading +X drives at Y + 3.6, heading -X at Y - 3.6.
  * On a road along Y: heading +Y drives at X - 3.6, heading -Y at X + 3.6.
  * Gravel streets: 2.5 units.
* **Sorting:** cars are overlays. Sort them with the tile under their centre; they are low, so strip-slicing isn't needed.

## 10. UI sketch (in the mockups)

The look is a 1960s Finnish radio or office desk: a dark top bar, a teak-veneer side panel, a brass name plate,
cream bakelite keys with 13x13 ink icons, a paper plaque with a tiny satirical president caricature
("Hyvä, hyvä!"), a brass-framed minimap and a news ticker.
* The font (`FONT`) is hand-drawn: 7 px cap height, 2 px descenders, proportional, with Ä Ö ä ö.
  Examples: "Väkiluku", "Rahat", "Lokakuu 1961", "Sähkö".
* Pre-render all text into bitmaps; GDI text is slow in the VM (CLAUDE.md section 6).

## 11. Known limitations and next steps

* Shadows are clipped to their own lot. That is fine, because they fall behind the object.
* Houses facing away (`r2`/`r3`) show their back, with the fence and path behind them.
* Shore tiles: the 14 corner combinations are noise-shaped. Diagonal masks (`shore06`, `shore09`) can make
  small islets.
* Missing pieces: road ends and bridges, power line corners and T pieces, a kiosk, service buildings
  (school, church, fire station), zone-overlay tints, construction stages and animation (chimney smoke,
  water ripples).
