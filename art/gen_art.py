#!/usr/bin/env python3
"""gen_art.py - KAUPUNKI world-art generator (task T02).  Python 3 + Pillow only (no numpy).

    python3 art/gen_art.py              render everything (both zooms), write sheets, headers,
                                        previews, mockups, then run the check
    python3 art/gen_art.py --check      check the written files only (format, widths, key purity,
                                        565, seams, required names from defs, per-zoom VRAM:
                                        64-zoom world sheets <= 9.6 MB, DESIGN 14.5)
    python3 art/gen_art.py --only house_t2,road_   re-render sprites whose name starts with one of
                                        the prefixes, reuse the rest from art/.cache (iteration)
    options: -j N (worker processes, default = CPU count), --nopreview

    python3 art/gen_art.py --repack     PL4: re-cut + re-pack the cached sprites (art/.cache) into pages,
                                        write sheets + headers, run the check (no rendering, no previews)

Outputs (art/): world<k>_64.bmp / world<k>_32.bmp PAGES (24-bit, <= 512 x 256, key RGB(255,0,255),
colours pre-rounded to RGB565), sheet_world_<z>.h per zoom (pieces + sprites), the index art_world.h
(src/art.h convention), preview/*.png.  See ART.md (section 8: pieces and pages, PL4)."""
import glob
import math
import multiprocessing
import os
import pickle
import re
import struct
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

from PIL import Image  # noqa: E402

import artlib  # noqa: E402
from artlib import (KEY, T, STYLE_C, to565, hexc, ihash, render_gbuffer, shade, write_bmp, key_mask,  # noqa: E402
                    skyline_pack, diamond_inside, Model, Solid, lerpc, clamp)
import art_terrain as TR  # noqa: E402
import art_models as M  # noqa: E402

PROJ = os.path.dirname(HERE)
CACHE = os.path.join(HERE, '.cache')
PREVIEW = os.path.join(HERE, 'preview')
ZOOMS = ((1.0, 64), (0.5, 32))
SHEET_W = 512           # PL4: pages of <= 512 x 256 px. The Voodoo3 driver gives DirectDraw a linear heap plus two
SHEET_MAX_H = 256       # screen-sized rectangular heaps (1024x768 at 1024: 9,088 KB linear + 2 x 1,536 KB): 640x2048
                        # sheets could only use the linear part (world3_64 refused at 1024 with 4,477 KB free).
                        # Small pages fill both and leave no big holes when a zoom swap evicts/reloads them.
PIECE_LAMBDA = 600      # PL4: VRAM px worth one extra blit (~5 us CPU per call ~ 600 px of blitter fill)
LOT_NAME = re.compile(r'(_v\d+_(?:ab_)?r\d$)|(_build_r\d$)|(^build_\d+x\d+_r\d$)')   # drawn in strips by render.cpp
VRAM_LIMIT_64 = int(9.6 * 1024 * 1024)    # DESIGN 14.5: only the active zoom is resident; 64-zoom world sheets
VRAM_LIMIT_32 = VRAM_LIMIT_64             # <= ~9.6 MB at 800x600 (UI + chunk cache + targets take the rest)
ROAD_KEYS = ('grav', 'road', 'main', 'mway')
DIRS = (('px', 0), ('ny', 1), ('nx', 2), ('py', 3))     # car heading +X, -Y, -X, +Y
SMOKE_FRAMES = 6
WATER_FRAMES = 3
DIAGONAL_SHORES = (6, 9)


# =========================================================================== building table
# key: (n, m) canonical footprint (= BuildingDef w x h), rotations, variant builders, abandoned policy.
# ab: 'all' = abandoned art for every variant, 'v0' = only v0 (art_lot_sprite falls back to v0_ab),
#     None = never abandoned (player-placed buildings).
def _v(fn, *a, **k):
    return lambda: fn(*a, **k)


HOUSE_T2 = [
    dict(wall='w_yellow', ridge='x', porch='gable', porch_at=0.2, tree='apple', chim=0.55, seed=1, flagpole=(29.0, 25.0)),
    dict(wall='w_red', ridge='y', porch='canopy', porch_at=0.5, tree='birch', chim=0.35, seed=2, door='door_green',
         woodpile=True),
    dict(wall='w_blue', ridge='x', porch='gable', porch_at=0.15, dormer=True, tree='birch', tree_at=(29.0, 4.0),
         chimkey='chim_white', seed=3),
    dict(wall='w_green', ridge='y', porch='gable', porch_at=0.75, tree='apple', chim=0.6, clad='v', seed=4),
    dict(wall='w_cream', ridge='x', porch='glazed', porch_at=0.3, tree='rowan', chim=0.6, seed=5,
         potato=(18.0, 24.5, 30.5, 30.0)),
    dict(wall='w_grey', ridge='x', porch='gable', porch_at=0.8, dormer=True, tree='apple', chim=0.3, clad='v', seed=6,
         roof='roof_red'),
    dict(wall='w_red', ridge='x', porch='glazed', porch_at=0.7, tree='spruce', tree_at=(3.5, 27.0), chim=0.4, seed=7,
         flagpole=(29.5, 25.0), roof='roof_dark'),
    dict(wall='w_ggreen', ridge='y', porch='canopy', porch_at=0.3, tree='birch', tree_at=(28.5, 4.0), chim=0.65,
         seed=8, roof='roof_felt', woodpile=True),
]
HOUSE_T3 = [
    dict(wall='w_yellow', ridge='x', porch='gable', porch_at=0.2, tree='birch', tree_at=(3.0, 27.5), chim=0.5, seed=11,
         car='red', garage='w_red'),
    dict(wall='w_blue', ridge='x', porch='canopy', porch_at=0.35, tree='apple', tree_at=(3.5, 27.5), chim=0.6, seed=12,
         car='white', garage='w_grey'),
    dict(wall='w_red', ridge='y', porch='gable', porch_at=0.3, tree='rowan', tree_at=(3.0, 27.0), chim=0.4, seed=13,
         car='lblue', garage='w_red'),
    dict(wall='w_cream', ridge='x', porch='glazed', porch_at=0.25, tree='apple', tree_at=(3.5, 27.5), chim=0.55,
         seed=14, car='orange', garage='w_brown'),
    dict(wall='w_grey', ridge='x', porch='gable', porch_at=0.25, tree='spruce', tree_at=(3.0, 27.0), chim=0.45,
         seed=15, car='green', garage='w_red', clad='v'),
    dict(wall='w_ggreen', ridge='y', porch='canopy', porch_at=0.3, tree='birch', tree_at=(3.0, 27.5), chim=0.6,
         seed=16, car='beige', garage='w_red', roof='roof_dark'),
]

BUILDINGS = [
    # key, (n, m), rots, [variant builders], ab policy
    ('house_t1', (1, 1), 4, [
        _v(M.cottage, 'h1', wall='w_red', roof='roof_felt', seed=1, tree='rowan'),
        _v(M.cottage, 'h1', wall='w_weath', roof='roof_metal', seed=2, tree='birch'),
        _v(M.cottage, 'h1', wall='w_yellow', roof='roof_felt', seed=3, tree='spruce'),
        _v(M.cottage, 'h1', wall='w_ggreen', roof='roof_metal', seed=4, tree='rowan', potato=False),
    ], 'all'),
    ('house_t2', (1, 1), 4, [_v(M.rintama, 'h2', **h) for h in HOUSE_T2], 'all'),
    ('house_t3', (1, 1), 4, [_v(M.rintama, 'h3', storeys=2, **h) for h in HOUSE_T3], 'all'),
    ('apt_t1_3x1', (3, 1), 4, [
        _v(M.lamelli, 'a1', 'gable', 'render_yel', 3, 'balc_white', seed=1),
        _v(M.lamelli, 'a1', 'gable', 'render_white', 3, 'balc_blue', seed=2),
        _v(M.lamelli, 'a1', 'gable', 'render_salmon', 3, 'balc_white', seed=3, roofkey='roof_dark'),
    ], 'v0'),
    ('apt_t1_2x2', (2, 2), 4, [
        _v(M.lamelli22, 'a1b', 'gable', 'render_yel', 3, 'balc_white', seed=1, shape='L'),
        _v(M.lamelli22, 'a1b', 'gable', 'render_white', 3, 'balc_blue', seed=2, shape='I'),
        _v(M.lamelli22, 'a1b', 'gable', 'brick', 3, 'balc_white', seed=3, shape='L', brick=True, roofkey='roof_dark'),
    ], 'v0'),
    # apartments T2: v0 here (4 rotations, abandoned v0); M4 appends a symmetric v1 (art_m2_res.py).
    ('apt_t2_3x1', (3, 1), 4, [
        _v(M.lamelli, 'a2', 'panel', 'panel_wash', 5, seed=4, balcs=('balc_orange', 'balc_blue', 'balc_green')),
        # _v(M.lamelli, 'a2', 'panel', 'panel', 5, seed=5, balcs=('balc_mustard', 'balc_white', 'balc_red')),
        # _v(M.lamelli, 'a2', 'panel', 'tile_brown', 5, seed=6, balcs=('balc_white',)),
    ], 'v0'),      # M4: abandoned v0 (the M2 VRAM budget allows it)
    ('apt_t2_2x2', (2, 2), 4, [
        _v(M.lamelli22, 'a2b', 'panel', 'panel_wash', 5, seed=4, shape='L', balcs=('balc_orange', 'balc_green')),
        # _v(M.lamelli22, 'a2b', 'panel', 'panel', 5, seed=5, shape='I', balcs=('balc_blue', 'balc_mustard')),
    ], 'v0'),
    ('com_t1_1x1', (1, 1), 4, [
        _v(M.kiosk, 'k', sign='sign_blue', seed=1),
        _v(M.kiosk, 'k', sign='sign_red', wall='render_yel', seed=2),
        _v(M.kiosk, 'k', wall='w_red', seed=3, wooden=True),
    ], 'v0'),
    ('com_t1_2x1', (2, 1), 4, [
        _v(M.coop_shop, 'cs', wall='w_yellow', sign='sign_green', seed=1),
        _v(M.coop_shop, 'cs', wall='render_white', sign='sign_red', seed=2, rendered=True, roof='roof_dark'),
        _v(M.coop_shop, 'cs', wall='w_red', sign='sign_yel', seed=3),
    ], 'v0'),
    ('com_t1_2x2', (2, 2), 4, [
        _v(M.coop_store, 'cst', wall='render_yel', sign='sign_green', seed=1, car='white'),
        _v(M.coop_store, 'cst', wall='render_white', sign='sign_red', seed=2, car='dblue', roof='roof_dark'),
    ], 'v0'),
    ('com_t2_1x1', (1, 1), 4, [
        _v(M.corner_shop, 'c2', wall='render_white', awn='awning', sign='sign_red', seed=1),
        _v(M.corner_shop, 'c2', wall='render_yel', awn='awning_g', sign='sign_green', seed=2),
        _v(M.corner_shop, 'c2', wall='render_grey', awn='awning_b', sign='sign_blue', seed=3),
    ], 'v0'),
    ('com_t2_2x1', (2, 1), 4, [
        _v(M.shop_row, 'sr', signs=('sign_green', 'sign_red', 'sign_blue'), seed=1, car='red'),
        _v(M.shop_row, 'sr', signs=('sign_yel', 'sign_blue', 'sign_red'), wall='render_grey', seed=2, car='lblue'),
        _v(M.shop_row, 'sr', signs=('sign_red', 'sign_green', 'sign_yel'), wall='render_yel', seed=3, car='orange'),
    ], 'v0'),
    ('com_t2_2x2', (2, 2), 4, [
        _v(M.shops_flats, 'sf', wall='render_yel', balc='balc_orange', seed=1),
        _v(M.shops_flats, 'sf', wall='render_white', balc='balc_blue', signs=('sign_green', 'sign_yel', 'sign_red'),
           awn='awning_b', seed=2),
    ], 'v0'),
    ('ind_t1_2x2', (2, 2), 4, [
        _v(M.workshop, 'w', wall='w_red', seed=1),
        _v(M.workshop, 'w', seed=2, corrugated=True),
    ], 'v0'),
    ('ind_t1_3x3', (3, 3), 4, [_v(M.sawmill, 'saw', seed=1), _v(M.sawmill, 'saw', seed=2, shed='w_weath',
                                                                  roof='roof_metal')], 'v0'),
    ('ind_t2_2x2', (2, 2), 4, [
        _v(M.factory22, 'f', seed=1),
        _v(M.factory22, 'f', seed=2, white=True),
    ], 'v0'),
    ('ind_t2_3x3', (3, 3), 4, [_v(M.factory33, 'f3', seed=1), _v(M.factory33, 'f3', seed=2, wall='render_white')], 'v0'),
    # 1x1 industrial yard fillers (F2, DESIGN 14.2, INDUSTRY.md 4.3): v0 timber (wood kinds), v1 steel stock
    # (engineering/metal), v2 brick workshop (generic), v3 coal + storage shed (generic)
    ('ind_yard', (1, 1), 4, [_v(M.yard_timber, 'y0'), _v(M.yard_steel, 'y1'), _v(M.yard_workshop, 'y2'),
                             _v(M.yard_coal, 'y3')], 'v0'),
    ('town_hall', (2, 2), 4, [_v(M.town_hall, 'th')], None),
    ('coal_plant', (2, 2), 4, [_v(M.coal_plant, 'cp')], None),
    ('pump', (1, 1), 4, [_v(M.pump_house, 'pu')], None),
    ('well', (1, 1), 1, [_v(M.well_house, 'we')], None),
    ('water_tower', (1, 1), 1, [_v(M.water_tower, 'wt', 0), _v(M.water_tower, 'wt', 1)], None),
    ('fire_station', (1, 1), 4, [_v(M.fire_station, 'fs')], None),
    ('school', (2, 2), 4, [_v(M.school, 'sc')], None),
    ('clinic', (1, 1), 4, [_v(M.clinic, 'cl')], None),
    ('sauna', (1, 1), 4, [_v(M.public_sauna, 'sa')], None),
    ('park', (1, 1), 1, [_v(M.park, 'pk', 0), _v(M.park, 'pk', 1), _v(M.park, 'pk', 2)], None),
    ('element_factory', (3, 3), 4, [_v(M.element_factory, 'ef')], None),
]

# --------------------------------------------------------------------------- Milestone-2 modules (M4)
# Each art_m2_*.py module defines ENTRIES (rows like BUILDINGS, but `rots` may be a list per variant),
# optionally KEY_ALIASES, GROUPS {key: group label} for the VRAM report, extra_jobs() (non-lot jobs:
# vehicles, forest stages, tiles) and extra_procedural(zoom) (procedural sprites such as fire frames).
# A row whose key already exists appends its variants to that row (e.g. apt_t2_3x1 v1).
#
# Rotation policy per variant (ART.md 3): 4 = r0..r3 rendered; 2 = r0, r1 rendered and r2/r3 are aliases of
# r0/r1 (the model must be 180-degree symmetric about the lot centre, so the r2 render would be identical);
# 1 = r0 rendered; square footprints get r1..r3 aliases (90-degree symmetric or orientation-free art),
# non-square ones get none (fixed orientation: the def must have BDF_NO_ROTATE).
import art_m2_res  # noqa: E402
import art_m2_ind  # noqa: E402
import art_m2_cplx  # noqa: E402
import art_m2_misc  # noqa: E402
M2_MODULES = [art_m2_res, art_m2_ind, art_m2_cplx, art_m2_misc]
M2_KEYS = set()
KEY_ALIASES = []          # (new key, [(src key, src variant), ...] = new v0, v1, ..., rot offset)
GROUPS = {}


def _norm_rows(rows):
    out = []
    for (key, fp, rots, variants, ab) in rows:
        rl = list(rots) if isinstance(rots, (list, tuple)) else [rots] * len(variants)
        out.append([key, fp, rl, list(variants), ab])
    return out


BUILDINGS = _norm_rows(BUILDINGS)
for _mod in M2_MODULES:
    for _row in _norm_rows(getattr(_mod, 'ENTRIES', [])):
        M2_KEYS.add(_row[0])
        _old = [b for b in BUILDINGS if b[0] == _row[0]]
        if _old:
            if _old[0][1] != _row[1]:
                raise SystemExit('M2 row %s: footprint %s differs from %s' % (_row[0], _row[1], _old[0][1]))
            _old[0][2] += _row[2]
            _old[0][3] += _row[3]
        else:
            BUILDINGS.append(_row)
    KEY_ALIASES += getattr(_mod, 'KEY_ALIASES', [])
    GROUPS.update(getattr(_mod, 'GROUPS', {}))
FOOTPRINTS = sorted(set(tuple(b[1]) for b in BUILDINGS))
BUILD_FP = dict((b[0], tuple(b[1])) for b in BUILDINGS)
for _ka in KEY_ALIASES:
    BUILD_FP[_ka[0]] = BUILD_FP.get(_ka[1][0][0], (1, 1))


def rot_plan(n, m, rots):
    """-> (rotations to render, {alias rot: source rot})"""
    if rots == 4:
        return [0, 1, 2, 3], {}
    if rots == 2:
        return [0, 1], {2: 0, 3: 1}
    return [0], ({1: 0, 2: 0, 3: 0} if n == m else {})


def ab_wanted(ab, v):
    return ab == 'all' or (ab == 'v0' and v == 0)

FORESTS = {
    'mixed': [(['spruce', 'birch', 'pine', 'spruce', 'bush'], 1), (['birch', 'spruce', 'rowan', 'pine', 'spruce'], 5),
              (['pine', 'birch', 'spruce', 'birch'], 9)],
    'spruce': [(['spruce'] * 5, 2), (['spruce'] * 4 + ['bush'], 6), (['spruce'] * 5, 10)],
    'birch': [(['birch', 'birch', 'birch', 'bush'], 3), (['birch'] * 5, 7), (['birch', 'birch', 'rowan', 'birch'], 11)],
    'pine': [(['pine', 'pine', 'spruce', 'pine'], 4), (['pine'] * 4 + ['bush'], 8), (['pine', 'pine', 'pine'], 12)],
}
TREES = [('birch', 7), ('birch_au', 8), ('spruce', 7), ('pine', 7), ('apple', 7), ('rowan', 9)]


# =========================================================================== job list
class Job(object):
    """one G-buffer (model x rotation) shaded into one or more outputs (name, mode)"""
    __slots__ = ('build', 'rot', 'kind', 'outs', 'top')

    def __init__(self, build, rot, kind, outs):
        self.build, self.rot, self.kind, self.outs = build, rot, kind, outs


def terrain_model(fn, name='t'):
    md = Model(name)
    md.ground = fn
    return md


def build_jobs():
    J = []

    def add(build, rot, kind, *outs):
        J.append(Job(build, rot, kind, [(o if isinstance(o, tuple) else (o, 'normal')) for o in outs]))

    # ---------------------------------------------------------------- terrain tiles
    for v in range(3):
        add((lambda vv: lambda: terrain_model(lambda g, st: TR.g_grass(g, st, vv)))(v), 0, 'tile', 'grass%d' % v)
    for f in range(WATER_FRAMES):
        add((lambda ff: lambda: terrain_model(lambda g, st: TR.g_water(g, st, ff)))(f), 0, 'tile', 'water%d' % f)
    for mask in range(1, 15):
        for f in range(WATER_FRAMES):
            nm = 'shore%02d' % mask + ('_%d' % f if f else '')
            add((lambda mm, ff: lambda: TR.shore_model(mm, ff))(mask, f), 0, 'tile', nm)
    for ax in ('x', 'y', 'n'):
        for f in range(WATER_FRAMES):
            add((lambda aa, ff: lambda: terrain_model(lambda g, st: TR.g_river(g, st, aa, ff)))(ax, f), 0, 'tile',
                'river_%s_%d' % (ax, f))
            add((lambda aa, ff: lambda: terrain_model(lambda g, st: TR.g_rapids(g, st, aa, ff)))(ax, f), 0, 'tile',
                'rapids_%s_%d' % (ax, f))
    add(lambda: terrain_model(lambda g, st: TR.g_field(g, st, 'crop')), 0, 'tile', 'field_crop')
    add(lambda: terrain_model(lambda g, st: TR.g_field(g, st, 'plough')), 0, 'tile', 'field_plough')
    add(lambda: terrain_model(TR.g_marsh), 0, 'tile', 'marsh')
    # ---------------------------------------------------------------- roads, rail, bridges, power
    for key in ROAD_KEYS:
        for mask in range(16):
            add((lambda k, mm: lambda: terrain_model(TR.road_ground(k, mm)))(key, mask), 0, 'tile', '%s_%02d' % (key, mask))
    for mask in range(16):
        add((lambda mm: lambda: terrain_model(TR.rail_ground(mm)))(mask), 0, 'tile', 'rail_%02d' % mask)
    for key in ROAD_KEYS + ('rail',):
        for axis, mm in (('y', 5), ('x', 10)):
            md_rot = (lambda k, a: lambda: M.bridge_model(k, a)[0])(key, axis)
            add(md_rot, 0 if axis == 'x' else 1, 'lot', '%s_bridge_%02d' % (key, mm))
    for mask in range(16):
        add((lambda mm: lambda: M.power_model(mm))(mask), 0, 'ovl', 'power_%02d' % mask)
        if mask:
            add((lambda mm: lambda: M.power_model(mm, wires_only=True))(mask), 0, 'ovl', 'wire_%02d' % mask)
    # ---------------------------------------------------------------- nature
    for kind, vs in FORESTS.items():
        for v, (kinds, seed) in enumerate(vs):
            add((lambda kk, ss: lambda: M.forest(kk, ss))(kinds, seed), 0, 'lot', 'forest_%s%d' % (kind, v))
    add(lambda: M.rock_tile(3), 0, 'lot', 'rock0')
    add(lambda: M.rock_tile(4), 0, 'lot', 'rock1')
    for (k, seed) in TREES:
        add((lambda kk, ss: lambda: M.tree_model(kk, ss))(k, seed), 0, 'ovl', 'tree_' + k)
    # ---------------------------------------------------------------- cars
    for ci, c in enumerate(M.CAR_COLOURS):
        for (dn, r) in DIRS:
            add((lambda cc: lambda: M.car_model(cc))(c), r, 'car', 'car%d_%s' % (ci, dn))
    # ---------------------------------------------------------------- buildings
    for (key, (n, m), rots, variants, ab) in BUILDINGS:
        for v, b in enumerate(variants):
            for r in rot_plan(n, m, rots[v])[0]:
                outs = ['%s_v%d_r%d' % (key, v, r)]
                if ab_wanted(ab, v):
                    outs.append(('%s_v%d_ab_r%d' % (key, v, r), 'ab'))
                add(b, r, 'lot', *outs)
    for mod in M2_MODULES:              # vehicles, forest stages, tiles ... (M4)
        if hasattr(mod, 'extra_jobs'):
            for (b, r, kind, outs) in mod.extra_jobs():
                add(b, r, kind, *outs)
    for (n, m) in FOOTPRINTS:     # rotation-independent art: r0 (+ r1 when not square), aliases for the rest
        for r in ((0,) if n == m else (0, 1)):
            add((lambda nn, mm: lambda: M.construction(nn, mm))(n, m), r, 'lot', 'build_%dx%d_r%d' % (n, m, r))
            add((lambda nn, mm: lambda: M.lot_ground(nn, mm))(n, m), r, 'lot', 'ground_%dx%d_r%d' % (n, m, r))
    return J


def aliases():
    """extra names pointing at an existing sprite (same rect)"""
    A = []
    for kind in FORESTS:
        A.append(('forest_%s' % kind, 'forest_%s0' % kind))
    A.append(('rock', 'rock0'))
    for (n, m) in FOOTPRINTS:
        for r in (1, 2, 3):
            t = 0 if n == m else r % 2
            if t != r:
                for kind in ('build', 'ground'):
                    A.append(('%s_%dx%d_r%d' % (kind, n, m, r), '%s_%dx%d_r%d' % (kind, n, m, t)))
    # rotation aliases of symmetric / fixed art (rot_plan)
    abv = {}
    for (key, (n, m), rots, variants, ab) in BUILDINGS:
        for v in range(len(variants)):
            abv[(key, v)] = ab_wanted(ab, v)
            for (r, src) in sorted(rot_plan(n, m, rots[v])[1].items()):
                A.append(('%s_v%d_r%d' % (key, v, r), '%s_v%d_r%d' % (key, v, src)))
                if abv[(key, v)]:
                    A.append(('%s_v%d_ab_r%d' % (key, v, r), '%s_v%d_ab_r%d' % (key, v, src)))
    # key aliases (industry kinds reusing tier art, complex pieces reusing a building): every rotation of the
    # source (after its own rotation aliases) + its abandoned art when it has some
    have = set(n for (n, _) in A) | set('%s_v%d_r%d' % (b[0], v, r) for b in BUILDINGS
                                        for v in range(len(b[3])) for r in rot_plan(b[1][0], b[1][1], b[2][v])[0])
    for ka in KEY_ALIASES:
        new, srcs = ka[0], ka[1]
        off = ka[2] if len(ka) > 2 else None
        for nv, (sk, sv) in enumerate(srcs):
            for r in ((0,) if off is not None else (0, 1, 2, 3)):
                sr = (r + off) % 4 if off is not None else r
                src = '%s_v%d_r%d' % (sk, sv, sr)
                if src in have:
                    A.append(('%s_v%d_r%d' % (new, nv, r), src))
                if abv.get((sk, sv)):
                    A.append(('%s_v%d_ab_r%d' % (new, nv, r), '%s_v%d_ab_r%d' % (sk, sv, sr)))
    return A


def resolve_aliases(A, names):
    """follow alias chains so every alias points at a rendered sprite name"""
    d = dict(A)
    out = []
    for (a, t) in A:
        k = 0
        while t not in names and t in d and k < 8:
            t = d[t]
            k += 1
        out.append((a, t))
    return out


# =========================================================================== rendering (parallel)
_JOBS = None


def run_job(args):
    ji, zoom = args
    job = _JOBS[ji]
    Solid._gid[0] = 0                         # deterministic outline groups and hashes per job
    md = job.build()
    gb = render_gbuffer(md, zoom, job.rot)
    res = []
    for (name, mode) in job.outs:
        img = shade(gb, STYLE_C, mode)
        anc = gb['anchor']
        hot = gb['hot']
        if job.kind == 'car':
            anc = (anc[0], anc[1] - int(16 * zoom))
        if job.kind != 'tile':
            bb = key_mask(img).getbbox()
            if bb:
                if job.kind == 'lot':        # keep full width (strip columns), trim only the top
                    bb = (0, bb[1], img.width, img.height)
                img = img.crop(bb)
                anc = (anc[0] - bb[0], anc[1] - bb[1])
                if hot:
                    hot = (min(max(hot[0] - bb[0], 0), img.width - 1), min(max(hot[1] - bb[1], 0), img.height - 1))
        res.append((name, int(64 * zoom), img.size, img.tobytes(), anc, hot or (-1, -1)))
    return res


def render(jobs, only=None, nproc=None):
    global _JOBS
    _JOBS = jobs
    tasks = []
    for ji, job in enumerate(jobs):
        if only and not any(o[0].startswith(p) for o in job.outs for p in only):
            continue
        for (z, zz) in ZOOMS:
            tasks.append((ji, z))
    # big jobs first for better load balance
    def cost(t):
        job = jobs[t[0]]
        return -(len(job.outs) * (4 if job.kind == 'lot' else 1)) * (4 if t[1] == 1.0 else 1)
    tasks.sort(key=cost)
    out = {}
    t0 = time.time()
    nproc = nproc or multiprocessing.cpu_count()
    if nproc > 1:
        ctx = multiprocessing.get_context('fork')
        with ctx.Pool(nproc) as pool:
            for res in pool.imap_unordered(run_job, tasks, chunksize=1):
                for r in res:
                    out[(r[0], r[1])] = r
    else:
        for t in tasks:
            for r in run_job(t):
                out[(r[0], r[1])] = r
    print('  rendered %d sprites (%d g-buffers) in %.1fs' % (len(out), len(tasks), time.time() - t0))
    return out


# =========================================================================== procedural sprites
OVR_RAMPS = [   # src/defs.h OVR_*: BAD, GOOD, COVER, NET, ZONE, PROBLEM (8 levels / categories each)
    ('bad', ['#6f8f62', '#8f9a5a', '#b0a250', '#c49446', '#c87a3e', '#bf5c38', '#a8423a', '#86303e']),
    ('good', ['#7a5a5a', '#8a6a58', '#9a8458', '#a69c5a', '#8fa45e', '#6fa266', '#4f9a78', '#3f8a8e']),
    ('cover', ['#9a4a3e', '#b0603e', '#c08a44', '#c4ac50', '#a6b058', '#7aa65e', '#5a9a74', '#4a8a96']),
    ('net', ['#5a9a6a', '#b04a3e', '#c8a040', '#6a9ac0', '#7aaad2', '#8a8a86', '#8a8a86', '#8a8a86']),  # F3: 4 = NETC_WET faint blue
    ('zone', ['#8a8a86', '#8fb06a', '#4e8a52', '#5a80b8', '#c8a23e', '#3e5e3a', '#a8925e', '#8a8a86']),  # F3: 5, 6
    ('problem', ['#8c8c8c', '#d2b440', '#4f86c0', '#5ab0b8', '#c87a3e', '#8a6ab0', '#a85a4a', '#c86a8a']),
]
ZONE_TINTS = {1: '#8fb06a', 2: '#4e8a52', 3: '#5a80b8', 4: '#c8a23e',
              5: '#3e5e3a', 6: '#a8925e'}   # F3: M3 zones 5 forestry (muted dark green), 6 farm (muted ochre)


def diamond_img(zoom, fill):
    """fill(px, py, inside_edge) -> colour or None"""
    w, h = int(64 * zoom), int(32 * zoom)
    img = Image.new('RGB', (w, h), KEY)
    p = img.load()
    for y in range(h):
        for x in range(w):
            if not diamond_inside(x, y, zoom):
                continue
            edge = any(not (0 <= x + dx < w and 0 <= y + dy < h and diamond_inside(x + dx, y + dy, zoom))
                       for (dx, dy) in ((1, 0), (-1, 0), (0, 1), (0, -1)))
            c = fill(x, y, edge)
            if c is not None:
                p[x, y] = to565(hexc(c) if isinstance(c, str) else c)
    return img


def smoke_img(zoom, f):
    """puff frame f (0 = fresh, small and dense .. 5 = big, thin): sphere-shaded lobes, dithered fade"""
    r = [2.5, 3.5, 4.6, 5.6, 6.6, 7.6][f] * zoom
    r = max(r, 1.5)
    size = int(math.ceil(r * 2.6)) + 2
    img = Image.new('RGB', (size, size), KEY)
    p = img.load()
    cx = cy = size / 2.0
    lobes = [(0.0, 0.0, r), (-0.45 * r, 0.25 * r, 0.7 * r), (0.5 * r, 0.2 * r, 0.65 * r), (0.1 * r, -0.45 * r, 0.6 * r)]
    tones = ['#e0ded8', '#c4c2bc', '#a3a29c', '#85847f']
    L = (-0.55, -0.65, 0.52)
    for y in range(size):
        for x in range(size):
            best = None
            for (ox, oy, rr) in lobes:
                dx, dy = (x + 0.5 - cx - ox) / rr, (y + 0.5 - cy - oy) / rr
                d2 = dx * dx + dy * dy
                if d2 < 1.0:
                    nz = math.sqrt(1 - d2)
                    lam = dx * L[0] + dy * L[1] + nz * L[2]
                    if best is None or nz > best[1]:
                        best = (lam, nz)
            if best is None:
                continue
            if f >= 3 and (x + y) % 2:
                continue
            if f >= 5 and (x % 2 or y % 2):
                continue
            lam = best[0]
            ti = 0 if lam > 0.55 else 1 if lam > 0.2 else 2 if lam > -0.15 else 3
            if f >= 4:
                ti = min(3, ti + 1)
            p[x, y] = to565(hexc(tones[ti]))
    return img, (int(cx), int(cy))


# ---------------------------------------------------------------- water pipes (F2, WATER.md 5, DESIGN 14.1)
# state -> (outline, shadow, base, highlight, flange).  Muted, but brighter/darker than the NET dither diamonds
# drawn under them, so the 1 px dark outline and the light top line carry them over any overlay colour.
PIPE_TRENCH = '#3b3529'
PIPE_STATES = [
    ('wet', ('#1c2731', '#33587a', '#4f7fa6', '#a6c8e0', '#7aa6c8')),
    ('short', ('#2c1c17', '#7c3a2a', '#b35d3c', '#e8a882', '#cd7f5c')),
    ('dry', ('#242322', '#5e5c58', '#94918a', '#d0ccc2', '#aeaba3')),
]


def pipe_img(zoom, mask, cols):
    """one pipe autotile: a tube from the tile centre to each connected edge midpoint (mask bits NE=1 SE=2
    SW=4 NW=8, same as roads), a collar halfway along every arm, a valve chamber at the centre for bends,
    tees and crosses, a capped end for dead ends.  Returns (img, anchor = south vertex).  Lines follow the
    2:1 axis exactly (same staircase in every tile), so neighbours join pixel-perfectly at the edge midpoints."""
    ol, sh, ba, hl, fl = [to565(hexc(c)) for c in cols]
    tw, th = int(64 * zoom), int(32 * zoom)
    big = zoom >= 1.0
    M = 6
    W, H = tw + 2 * M, th + 2 * M
    half = tw // 2
    arm = tw // 4                               # centre -> edge midpoint in x
    up, dn = (2, 2) if big else (1, 1)          # interior rows yc-up .. yc+dn (5 / 3 px)
    tone = [hl, fl, ba, ba, sh] if big else [hl, ba, sh]

    def yc_x(x):                                # SE/NW axis (+X world): y grows with x
        return th // 2 + (x - half) // 2

    def yc_y(x):                                # NE/SW axis (+Y world): mirror image
        return th // 2 + (half - 1 - x) // 2

    arms = []
    if mask & 2:
        arms.append((list(range(half, half + arm)), yc_x, 'out'))
    if mask & 8:
        arms.append((list(range(half - arm, half)), yc_x, 'in'))
    if mask & 1:
        arms.append((list(range(half, half + arm)), yc_y, 'out'))
    if mask & 4:
        arms.append((list(range(half - arm, half)), yc_y, 'in'))
    n = len(arms)
    role = {}
    for xs, f, side in arms:
        for x in xs:
            yc = f(x)
            for k in range(up + dn + 1):
                role[(x, yc - up + k)] = tone[k]
    outl = set()
    for (x, y) in list(role):
        for dy in (-1, 1):
            if (x, y + dy) not in role:
                outl.add((x, y + dy))
    # collars (and the cap of a dead end): 2 / 1 columns, one px proud above and below, outlined all round
    collars = {}

    def collar(x0, ring, sides=(True, True)):
        """ring = column tones; one px proud above and below; outlined except on a tile-edge side"""
        for i, x in enumerate(range(x0, x0 + len(ring))):
            yc = f(x)
            for k, y in enumerate(range(yc - up - 1, yc + dn + 2)):
                collars[(x, y)] = ring[i] if 0 < k < up + dn + 2 else (fl if k == 0 else sh)
            outl.add((x, yc - up - 2))
            outl.add((x, yc + dn + 2))
        for x, on in ((x0 - 1, sides[0]), (x0 + len(ring), sides[1])):
            if on:
                yc = f(x)
                outl.add((x, yc - up - 1))
                outl.add((x, yc + dn + 1))
    # joint at every edge midpoint: each tile draws its half (64: dark rim + light face, 32: one dark column),
    # the neighbour's arm draws the mirror half, so pipe sections meet in one ring per tile edge
    half_ring = [sh, hl] if big else [sh]
    for xs, f, side in arms:
        if side == 'out':
            collar(xs[-1] - len(half_ring) + 1, half_ring, (True, False))
        else:
            collar(xs[0], half_ring[::-1], (False, True))
    if n == 1:                                  # dead end: cap at the centre end, closed outline beyond it
        xs, f, side = arms[0]
        cap = [sh, hl, sh] if big else [sh]
        x0 = xs[0] if side == 'out' else xs[-1] - len(cap) + 1
        collar(x0, cap)
        xe = x0 - 1 if side == 'out' else x0 + len(cap)
        yc = f(xe)
        for y in range(yc - up - 2, yc + dn + 3):
            outl.add((xe, y))
    img = Image.new('RGB', (W, H), KEY)
    p = img.load()
    # trench: a 50% dither of dark earth 1-2 px around the tube (reads as "buried", lifts it off any dither)
    tr = to565(hexc(PIPE_TRENCH))
    tw_ = 2 if big else 1
    for (x, y) in list(role):
        for dy in range(-tw_ - 1, tw_ + 2):
            q = (x, y + dy)
            if q not in role and q not in outl and (q[0] + q[1]) % 2 == 0:
                p[q[0] + M, q[1] + M] = tr
    for (x, y) in outl:
        if (x, y) not in role and (x, y) not in collars:
            p[x + M, y + M] = ol
    for (x, y), c in role.items():
        p[x + M, y + M] = c
    for (x, y), c in collars.items():
        p[x + M, y + M] = c
    # valve chamber: squat vertical cylinder over the junction (bends, tees, crosses) or alone (00)
    if n != 1 and mask not in (5, 10):
        if big:
            rx, ry, hgt = (5.0, 2.8, 3) if mask else (6.0, 3.2, 4)
        else:
            rx, ry, hgt = (2.6, 1.5, 2) if mask else (3.2, 1.8, 2)
        cx, cy = float(half), th / 2.0          # base ellipse centre; top ellipse hgt px higher
        pts = {}
        for y in range(int(cy - ry - hgt - 3), int(cy + ry + 3)):
            for x in range(int(cx - rx - 2), int(cx + rx + 2)):
                dx = (x + 0.5 - cx) / rx
                tt = (y + 0.5 - (cy - hgt)) / ry
                bb = (y + 0.5 - cy) / ry
                if dx * dx + tt * tt < 1.0:
                    pts[(x, y)] = hl if tt < -0.25 and dx < 0.4 else fl
                elif abs(dx) < 1.0 and (y + 0.5 >= cy - hgt) and dx * dx + bb * bb < 1.0 or \
                        (abs(dx) < 1.0 and cy - hgt <= y + 0.5 <= cy):
                    pts[(x, y)] = sh if dx > 0.45 else ba
        for (x, y) in list(pts):
            for (ex, ey) in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                q = (x + ex, y + ey)
                if q not in pts:
                    p[q[0] + M, q[1] + M] = ol
        for (x, y), c in pts.items():
            p[x + M, y + M] = c
    return img, (M + half, M + th)


# ---------------------------------------------------------------- persistent zone view (F2, DESIGN 14.4)
# Brighter than the ZONE ramp (they must read as lines over grass, roads and lot ground): light dash on the
# tile edge + a darker companion row just inside it.
ZONEV_COLS = {1: ('#bcdc8c', '#4f6f34'), 2: ('#7cc8a4', '#2f6248'), 3: ('#9cbcea', '#34507e'),
              4: ('#ecc865', '#7c5c1c'),
              5: ('#8cae7a', '#2a4426'), 6: ('#dcc896', '#6c5830')}    # F3: forestry, farm (M3 zones 5, 6)
ZONEV_EDGES = (1, 2, 4, 8)                      # NE, SE, SW, NW (mask bits)


def zonev_img(zoom, z, edges):
    """dashed band just inside the tile diamond along the given edges (bit set): light dash rows on the edge,
    one darker row inside them (contrast on light ground)"""
    lt, dk = [to565(hexc(c)) for c in ZONEV_COLS[z]]
    tw, th = int(64 * zoom), int(32 * zoom)
    img = Image.new('RGB', (tw, th), KEY)
    p = img.load()
    period = 8 if zoom >= 1.0 else 4
    wl = 2.0 if zoom >= 1.0 else 1.0            # light rows (64: 2 px, 32: 1 px), then one dark row
    for y in range(th):
        for x in range(tw):
            if not diamond_inside(x, y, zoom):
                continue
            cx, cy = x + 0.5, y + 0.5
            u = (cx - tw / 2.0) / (tw / 2.0)
            v = (cy - th / 2.0) / (th / 2.0)
            # vertical distance (px) to each edge line: NE (u - v = 1), SE (u + v = 1), SW (v - u = 1), NW
            dist = {1: (1 - (u - v)) * th / 2.0, 2: (1 - (u + v)) * th / 2.0,
                    4: (1 - (v - u)) * th / 2.0, 8: (1 + (u + v)) * th / 2.0}
            par = {1: cx - tw / 2.0, 2: cx - tw / 2.0, 4: tw / 2.0 - cx, 8: tw / 2.0 - cx}   # 0..tw/2 along it
            c = None
            for e in ZONEV_EDGES:
                if not edges & e:
                    continue
                d = dist[e]
                t = int(par[e]) % period
                on = 1 <= t <= period - 2
                if d < wl and on:
                    c = lt
                    break
                if d < wl + 1.0 and on and c is None:
                    c = dk
            if c is not None:
                p[x, y] = c
    return img


def procedural(zoom):
    """name -> (img, anchor, hot)"""
    out = {}
    tw, th = int(64 * zoom), int(32 * zoom)
    anc = (tw // 2, th)
    for ri, (rn, cols) in enumerate(OVR_RAMPS):
        for li, c in enumerate(cols):
            if rn == 'net' and li == 4:    # F3: NETC_WET (where a new building would get water): faint, 1 px in 4
                img = diamond_img(zoom, (lambda cc: lambda x, y, e: cc if x % 2 == 0 and y % 2 == 0 else None)(c))
            else:
                img = diamond_img(zoom, (lambda cc: lambda x, y, e: cc if (x + y) % 2 == 0 else None)(c))
            out['ovl_%d_%d' % (ri, li)] = (img, anc, (-1, -1))
    out['cur_ok'] = (diamond_img(zoom, lambda x, y, e: '#e8e2c8' if e else ('#e8e2c8' if (x % 2 == 0 and y % 2 == 0) else None)), anc, (-1, -1))
    out['cur_bad'] = (diamond_img(zoom, lambda x, y, e: '#c8423a' if e or (x + y) % 4 == 0 else None), anc, (-1, -1))
    w = tw

    def sel(x, y, e):
        if e:
            return '#d8b85a'
        # second ring: 1 px inside the edge
        inner = all(diamond_inside(x + dx, y + dy, zoom) and
                    all(diamond_inside(x + dx + ex, y + dy + ey, zoom) for (ex, ey) in ((1, 0), (-1, 0), (0, 1), (0, -1)))
                    for (dx, dy) in ((1, 0), (-1, 0), (0, 1), (0, -1)))
        return None if inner else '#a8883a'
    out['cur_sel'] = (diamond_img(zoom, sel), anc, (-1, -1))
    out['cur_query'] = (diamond_img(zoom, lambda x, y, e: '#7ab0d8' if e else ('#7ab0d8' if (x % 2 == 0 and y % 2 == 0) else None)), anc, (-1, -1))
    for z, c in ZONE_TINTS.items():
        out['zone_%d' % z] = (diamond_img(zoom, (lambda cc: lambda x, y, e: cc if (e and (x + y) % 2 == 0) or
                                                 (x % 2 == 0 and y % 2 == 0) else None)(c)), anc, (-1, -1))
    for f in range(SMOKE_FRAMES):
        img, a = smoke_img(zoom, f)
        out['smoke%d' % f] = (img, a, (-1, -1))
    # water pipes, trimmed to their bounding box (anchor = tile south vertex, may lie outside)
    for (st, cols) in PIPE_STATES:
        for mask in range(16):
            img, a = pipe_img(zoom, mask, cols)
            bb = key_mask(img).getbbox()
            out['pipe_%02d_%s' % (mask, st)] = (img.crop(bb), (a[0] - bb[0], a[1] - bb[1]), (-1, -1))
    # zone view: whole-tile dashed outline + one piece per edge (trimmed)
    for z in ZONEV_COLS:
        for nm, edges in [('zonev_%d' % z, 15)] + [('zonev_%d_%02d' % (z, e), e) for e in ZONEV_EDGES]:
            img = zonev_img(zoom, z, edges)
            bb = key_mask(img).getbbox()
            out[nm] = (img.crop(bb), (anc[0] - bb[0], anc[1] - bb[1]), (-1, -1))
    for mod in M2_MODULES:              # fire frames etc. (M4)
        if hasattr(mod, 'extra_procedural'):
            out.update(mod.extra_procedural(zoom))
    return out


# =========================================================================== sheets + headers
def make_sprites(rendered, zoom_px):
    """name -> (img, anchor, hot) for one zoom"""
    S = {}
    for (name, z), r in rendered.items():
        if z != zoom_px:
            continue
        _, _, size, data, anc, hot = r
        S[name] = (Image.frombytes('RGB', size, data), tuple(anc), tuple(hot))
    for name, v in procedural(1.0 if zoom_px == 64 else 0.5).items():
        S[name] = v
    return S


def sprite_pieces(name, img, ax, hw):
    """PL4: cut one sprite into column pieces, each trimmed to its own opaque box -> [(x0, y0, x1, y1)] in sprite
    px (no pieces for an empty sprite). Cuts lie on the half-tile grid (x = k * hw). render.cpp draws a lot sprite
    (LOT_NAME, ax on the grid, wider than 2 * hw) as one strip per half-tile column with the two centre columns
    merged (lot_pieces), so a cut on a strip boundary costs no extra blit; any other cut (inside the centre pair,
    or in a sprite drawn whole) costs one blit, charged PIECE_LAMBDA px. Minimum-cost cut set by dynamic
    programming; deterministic. The logical sprite (w, h, ax, ay) stays exactly as rendered."""
    w, h = img.size
    px = img.load()
    cols = []
    for cx in range(w):
        t = b = None
        for yy in range(h):
            if px[cx, yy] != KEY:
                if t is None:
                    t = yy
                b = yy
        cols.append((t, b))

    def box(a, b):
        xs = [c for c in range(a, b) if cols[c][0] is not None]
        if not xs:
            return None
        return (xs[0], min(cols[c][0] for c in xs), xs[-1] + 1, max(cols[c][1] for c in xs) + 1)

    def area(a, b):
        bb = box(a, b)
        return 0 if bb is None else (bb[2] - bb[0]) * (bb[3] - bb[1])
    strips = bool(LOT_NAME.search(name)) and ax % hw == 0 and w > 2 * hw
    cuts = sorted(set([0, w] + list(range(hw, w, hw))))
    free = set(c for c in range(hw, w, hw) if c != ax) if strips else set()
    n = len(cuts)
    best = [0] + [None] * (n - 1)
    prev = [0] * n
    for j in range(1, n):
        for i in range(j):
            v = best[i] + area(cuts[i], cuts[j]) + (0 if (i == 0 or cuts[i] in free) else PIECE_LAMBDA)
            if best[j] is None or v < best[j]:
                best[j] = v
                prev[j] = i
    seg = []
    j = n - 1
    while j > 0:
        seg.append((cuts[prev[j]], cuts[j]))
        j = prev[j]
    return [bb for bb in (box(a, b) for (a, b) in reversed(seg)) if bb]


def write_sheets(S, zoom_px):
    """PL4: sprites -> trimmed pieces (identical pieces stored once) -> skyline-packed pages.
    Returns (pages [(sname, img)], placed {piece id: (page, x, y, w, h)}, spr {name: [(piece id, dx, dy)]})."""
    hw = zoom_px // 2
    items, crops, uniq, spr = [], {}, {}, {}
    for n in sorted(S):
        img, anc, hot = S[n]
        pcs = []
        for k, (x0, y0, x1, y1) in enumerate(sprite_pieces(n, img, anc[0], hw)):
            c = img.crop((x0, y0, x1, y1))
            key = (c.size, c.tobytes())
            pid = uniq.get(key)
            if pid is None:
                pid = '%s#%d' % (n, k)
                uniq[key] = pid
                crops[pid] = c
                items.append((pid, x1 - x0, y1 - y0))
            pcs.append((pid, x0, y0))
        spr[n] = pcs
    pages, heights = skyline_pack(items, SHEET_W, SHEET_MAX_H)
    out, placed = [], {}
    for k, (page, ph) in enumerate(zip(pages, heights)):
        img = Image.new('RGB', (SHEET_W, ph), KEY)
        for (pid, x, y, w, h) in page:
            img.paste(crops[pid], (x, y))
            placed[pid] = (k, x, y, w, h)
        sname = 'world%d_%d' % (k, zoom_px)
        write_bmp(os.path.join(HERE, sname + '.bmp'), img)
        out.append((sname, img))
    return out, placed, spr


def write_headers(zsets, alias_list):
    """zsets: [(zoom_px, pages, placed, spr, S)] -> sheet_world_<z>.h (pieces + sprites) + art_world.h"""
    for f in glob.glob(os.path.join(HERE, 'sheet_world*.h')):
        os.remove(f)
    idx = []
    for (zoom_px, pages, placed, spr, S) in zsets:
        base = len(idx)
        idx += [(sname, zoom_px) for (sname, img) in pages]
        rows = [(n, n) for n in spr]
        rows += [(a, t) for (a, t) in resolve_aliases(alias_list, spr) if t in spr]
        rows.sort(key=lambda r: r[0])
        plist, first = [], {}
        for n in sorted(spr):
            first[n] = len(plist)
            for (pid, dx, dy) in spr[n]:
                pg, x, y, w, h = placed[pid]
                plist.append((base + pg, x, y, w, h, dx, dy))
        guard = 'SHEET_WORLD_%d_H' % zoom_px
        L = ['/* generated by gen_art.py - do not edit */',
             '/* %d-zoom world art (PL4 pieces, art.h): %d pages, %d pieces, %d sprite names.' % (zoom_px, len(pages), len(plist), len(rows)),
             '   ArtPiece = page (index into art_world_sheets), x, y, w, h in the page, dx, dy = top-left inside the sprite.',
             '   ArtPSpr = name, w, h, ax, ay (anchor = south vertex of the footprint; cars: ground centre; smoke: puff',
             '   centre), hx, hy (chimney top, -1 = none), first piece, piece count. */',
             '#ifndef %s' % guard, '#define %s' % guard, '',
             'static const ArtPiece art_pieces_%d[] = {' % zoom_px]
        for p in plist:
            L.append('    { %d, %d, %d, %d, %d, %d, %d },' % p)
        L += ['};', '', 'static const ArtPSpr art_sprites_%d[] = {' % zoom_px]
        for (a, t) in rows:
            img, anc, hot = S[t]
            L.append('    { "%s", %d, %d, %d, %d, %d, %d, %d, %d },' % (a, img.width, img.height, anc[0], anc[1], hot[0], hot[1],
                                                                       first[t], len(spr[t])))
        L += ['};', '#define ART_PIECES_%d_COUNT %d' % (zoom_px, len(plist)),
              '#define ART_SPRITES_%d_COUNT %d' % (zoom_px, len(rows)), '', '#endif']
        with open(os.path.join(HERE, 'sheet_world_%d.h' % zoom_px), 'w') as f:
            f.write('\r\n'.join(L) + '\r\n')
    L = ['/* generated by gen_art.py - do not edit */',
         '/* World art index (src/art.h "GENERATED HEADER CONVENTION", PL4 pieces). Included only by art.cpp. */',
         '#ifndef ART_WORLD_H', '#define ART_WORLD_H', '']
    for (zoom_px, pages, placed, spr, S) in zsets:
        L.append('#include "sheet_world_%d.h"' % zoom_px)
    L += ['', 'static const ArtSheet art_world_sheets[] = {']
    for (sname, z) in idx:
        L.append('    { "art\\\\%s.bmp", %s, 1, NULL, 0 },' % (sname, 'ZOOM_64' if z == 64 else 'ZOOM_32'))
    L += ['};', '#define ART_WORLD_SHEET_COUNT %d' % len(idx), '', 'static const ArtPieceSet art_world_sets[] = {']
    for (zoom_px, pages, placed, spr, S) in zsets:
        L.append('    { %s, art_pieces_%d, ART_PIECES_%d_COUNT, art_sprites_%d, ART_SPRITES_%d_COUNT },' %
                 ('ZOOM_64' if zoom_px == 64 else 'ZOOM_32', zoom_px, zoom_px, zoom_px, zoom_px))
    L += ['};', '#define ART_WORLD_SET_COUNT %d' % len(zsets), '', '#endif']
    with open(os.path.join(HERE, 'art_world.h'), 'w') as f:
        f.write('\r\n'.join(L) + '\r\n')


# =========================================================================== previews + mockups
def preview_sheet(S, names, bg=(70, 74, 66), scale=2, width=640, label=None):
    imgs = [(n, S[n]) for n in names if n in S]
    rows, row, rw, rh = [], [], 0, 0
    for n, (im, a, h) in imgs:
        if rw + im.width + 4 > width and row:
            rows.append((row, rh))
            row, rw, rh = [], 0, 0
        row.append((n, im))
        rw += im.width + 4
        rh = max(rh, im.height)
    if row:
        rows.append((row, rh))
    H = sum(r[1] + 4 for r in rows) or 1
    out = Image.new('RGB', (width, H), bg)
    y = 0
    for row, rh in rows:
        x = 0
        for n, im in row:
            out.paste(im, (x, y + rh - im.height), key_mask(im))
            x += im.width + 4
        y += rh + 4
    return out.resize((out.width * scale, out.height * scale), Image.NEAREST)


def compose_map(S, N, ground, objs, zoom=1.0, pad=220):
    """paste ground tiles, then objects cut into half-tile column strips sorted by depth
    (STYLE.md 4 strip slicing; the same rule the renderer uses)"""
    tw, th = int(64 * zoom), int(32 * zoom)
    W = N * tw
    H = N * th + int(pad * zoom)
    oy = int(pad * zoom)
    ox = N * tw // 2
    canvas = Image.new('RGB', (W, H), to565((40, 44, 40)))
    masks = {}

    def mask(name):
        if name not in masks:
            masks[name] = key_mask(S[name][0])
        return masks[name]

    for i in range(N):
        for j in range(N):
            nm = ground[i][j]
            img, anc, hot = S[nm]
            canvas.paste(img, (ox + (i - j) * tw // 2 - anc[0] + tw // 2 - tw // 2, oy + (i + j) * th // 2 + th - anc[1]), mask(nm))
    strips = []
    cw = tw // 2
    for ob in objs:
        name, i0, j0, n, mm = ob[:5]
        du, dv = ob[5] if len(ob) > 5 else (0.0, 0.0)
        img, anc, hot = S[name]
        if name.startswith('car') or name.startswith('smoke'):
            si, sj = i0 + 0.5, j0 + 0.5
        else:
            si, sj = i0 + n, j0 + mm
        sx = ox + int(round((si - sj) * tw / 2.0 + (du - dv) * zoom))
        sy = oy + int(round((si + sj) * th / 2.0 + (du + dv) / 2.0 * zoom))
        if len(ob) > 6:
            sx, sy = ob[6]
        x0 = sx - anc[0]
        y0 = sy - anc[1]
        m = mask(name)
        for k in range(int(math.floor((x0 - ox) / float(cw))), int(math.ceil((x0 + img.width - ox) / float(cw)))):
            best = None
            for a in range(i0, i0 + n):
                for b in range(j0, j0 + mm):
                    if a - b - 1 == k or a - b == k:
                        if best is None or a + b > best:
                            best = a + b
            if best is None:
                best = max(a + b for a in range(i0, i0 + n) for b in range(j0, j0 + mm))
            if name.startswith(('smoke', 'ovl')):
                best = 10000
            elif name.startswith('pipe_'):        # overlay pass, after every dither diamond
                best = 20000
            elif name.startswith('zonev'):        # right after its own tile's lot (front-edge rule, ART.md)
                best = i0 + j0 + 0.5
            cx0 = ox + k * cw
            l = max(cx0, x0) - x0
            r = min(cx0 + cw, x0 + img.width) - x0
            if r <= l:
                continue
            strips.append((best, k, img.crop((l, 0, r, img.height)), m.crop((l, 0, r, img.height)), (x0 + l, y0)))
    strips.sort(key=lambda s: (s[0], s[1]))
    for (_, _, im, mk, pos) in strips:
        canvas.paste(im, pos, mk)
    return canvas, ox, oy


def town_layout():
    """a 34x34 test town using every family of production names"""
    N = 34
    G = [['grass%d' % int(ihash(i, j, 500) * 3) for j in range(N)] for i in range(N)]
    land = [[1] * (N + 1) for _ in range(N + 1)]
    for ci in range(N + 1):
        for cj in range(N + 1):
            d = ((ci - 6.0) / 6.5) ** 2 + ((cj - 6.5) / 5.6) ** 2 + (ihash(ci, cj, 501) - 0.5) * 0.25
            if d < 1.0:
                land[ci][cj] = 0
            if 27 <= ci <= 29 and cj <= 26:          # a strait in the east (bridges)
                land[ci][cj] = 0
    objs = []
    occ = set()

    def place(name, i0, j0, n=1, m=1):
        for a in range(i0, i0 + n):
            for b in range(j0, j0 + m):
                occ.add((a, b))
        objs.append((name, i0, j0, n, m))

    def lot(key, v, r, i0, j0, n, m, ab=False):
        wn, wm = (m, n) if r % 2 else (n, m)
        place('%s_v%d_%sr%d' % (key, v, 'ab_' if ab else '', r), i0, j0, wn, wm)

    for i in range(N):
        for j in range(N):
            c = (land[i][j], land[i + 1][j], land[i][j + 1], land[i + 1][j + 1])
            mask = c[0] | c[1] << 1 | c[2] << 2 | c[3] << 3
            if mask == 0:
                G[i][j] = 'water%d' % 0
                occ.add((i, j))
            elif mask != 15:
                G[i][j] = 'shore%02d' % mask
                occ.add((i, j))
    roads = {}

    def road(i, j, key):
        roads[(i, j)] = key
        occ.add((i, j))

    # main road along X at j=13 (bridge over the strait), coming from the SW along Y at i=3
    for i in range(3, N):
        road(i, 13, 'main')
    for j in range(14, N):
        road(3, j, 'main')
    # railway along X at j=24 east part, bridge over the strait
    for i in range(18, N):
        road(i, 24, 'rail')
    # gravel residential streets
    for j in range(14, N):
        road(14, j, 'grav')
    for i in range(4, 14):
        road(i, 19, 'grav')
    for i in range(15, 24):
        road(i, 19, 'grav')
    for j in range(20, 24):
        road(8, j, 'grav')
    # streets north of the main road
    for j in range(5, 13):
        road(16, j, 'road')
    for i in range(17, 27):
        road(i, 8, 'road')
    for i in range(12, 16):
        road(i, 8, 'road')
    # autotile masks
    for (i, j), key in roads.items():
        m = 0
        for bit, (di, dj) in ((1, (0, -1)), (2, (1, 0)), (4, (0, 1)), (8, (-1, 0))):
            k2 = roads.get((i + di, j + dj))
            if k2 is None:
                continue
            if (key == 'rail') != (k2 == 'rail'):
                continue
            m |= bit
        if G[i][j].startswith('water'):
            G[i][j] = '%s_bridge_%02d' % (key, 10 if m & 10 else 5)
        else:
            G[i][j] = '%s_%02d' % (key, m)
    # houses along the gravel streets (fronts face the street)
    k = 0
    for j in range(14, 33):
        if j in (19, 24):
            continue
        t = 3 if j < 17 else 2 if j < 23 else 1
        place('house_t%d_v%d_r1' % (t, k % (6 if t == 3 else 8 if t == 2 else 4)), 13, j)
        if (15, j) not in occ:
            t2 = 2 if j % 3 else 3
            place('house_t%d_v%d_r3' % (t2, (k + 3) % (6 if t2 == 3 else 8)), 15, j)
        k += 1
    for i in list(range(4, 8)) + list(range(9, 13)):
        place('house_t2_v%d_r0' % ((k + 1) % 8), i, 18)
        place('house_t%d_v%d_r2' % ((2, (k + 4) % 8) if i % 3 else (1, k % 4)), i, 20)
        k += 1
    place('house_t2_v5_ab_r2', 9, 20) if (9, 20) not in occ else None
    for i in (16, 17, 18, 20, 21, 22, 23):
        if (i, 18) not in occ:
            place('house_t2_v%d_r0' % (i % 8), i, 18)
        if (i, 20) not in occ:
            place('house_t3_v%d_r2' % (i % 6), i, 20)
    place('build_1x1_r0', 19, 18)
    place('sauna_v0_r2', 19, 20)
    # apartments / commercial along the streets north of the main road
    lot('apt_t1_3x1', 0, 2, 17, 9, 3, 1)
    lot('apt_t2_3x1', 0, 2, 20, 9, 3, 1)
    lot('apt_t1_2x2', 0, 2, 23, 9, 2, 2)
    lot('com_t2_2x2', 1, 0, 17, 11, 2, 2)
    lot('com_t1_2x1', 0, 0, 19, 11, 2, 1)
    lot('com_t2_1x1', 0, 0, 21, 11, 1, 1)
    lot('com_t1_1x1', 0, 0, 22, 12, 1, 1)
    lot('com_t2_2x1', 0, 0, 19, 12, 2, 1)
    lot('apt_t2_2x2', 0, 0, 17, 6, 2, 2)
    lot('apt_t1_3x1', 1, 0, 19, 7, 3, 1)
    lot('school', 0, 1, 14, 9, 2, 2)
    lot('clinic', 0, 1, 15, 11, 1, 1)
    lot('fire_station', 0, 1, 15, 12, 1, 1)
    lot('town_hall', 0, 0, 12, 10, 2, 2)
    lot('com_t1_2x2', 1, 0, 23, 11, 2, 2)
    lot('apt_t2_3x1', 0, 0, 22, 7, 3, 1)
    lot('park', 0, 0, 12, 12, 1, 1)
    lot('park', 1, 0, 13, 12, 1, 1)
    lot('water_tower', 1, 0, 14, 6, 1, 1)
    lot('pump', 0, 1, 13, 4, 1, 1)
    lot('well', 0, 0, 14, 7, 1, 1)
    # industry east of the residential area, power plant + line
    lot('ind_t2_2x2', 0, 0, 17, 14, 2, 2)
    lot('ind_t1_2x2', 0, 0, 19, 14, 2, 2)
    lot('ind_t2_3x3', 0, 0, 21, 14, 3, 3)
    lot('ind_t1_2x2', 1, 1, 24, 14, 2, 2)
    lot('ind_t1_3x3', 0, 0, 23, 21, 3, 3)
    lot('coal_plant', 0, 0, 30, 14, 2, 2)
    lot('element_factory', 0, 0, 30, 17, 3, 3)
    lot('ind_t2_2x2', 0, 1, 21, 21, 2, 2, ab=True)
    # 1x1 yards in the third row of the industrial band, where no 2x2 fits (DESIGN 14.2)
    for k, i in enumerate((17, 18, 19, 20, 24, 25, 26)):
        if (i, 16) not in occ:
            lot('ind_yard', (k * 3 + 1) % 4, 2, i, 16, 1, 1, ab=(i == 26))
    for k, i in enumerate((21, 22)):
        if (i, 23) not in occ:
            lot('ind_yard', (k + 2) % 4, 1, i, 23, 1, 1)
    for i in range(16, N):
        if (i, 17) in occ:
            continue
        if G[i][17].startswith(('grav', 'road', 'main', 'rail')):
            objs.append(('wire_10', i, 17, 1, 1))
        else:
            objs.append(('power_10', i, 17, 1, 1))
        occ.add((i, 17))
    # fields south-east
    for i in range(16, N):
        for j in range(25, N):
            if (i, j) not in occ:
                G[i][j] = 'field_plough' if 20 <= i <= 21 else 'field_crop'
                occ.add((i, j))
    # cars (right-hand traffic offsets in world units)
    cars = [(6, 13, 'px', (4.0, 4.5), 6), (10, 13, 'nx', (-2.0, -4.5), 4), (18, 13, 'px', (0.0, 4.5), 0),
            (24, 13, 'nx', (6.0, -4.5), 2), (31, 13, 'px', (0.0, 4.5), 5), (14, 16, 'py', (-3.0, 2.5), 7),
            (14, 27, 'ny', (2.5, 0.0), 1), (10, 19, 'px', (0.0, 2.5), 8), (20, 19, 'nx', (0.0, -2.5), 6),
            (3, 18, 'ny', (4.5, 0.0), 3), (3, 25, 'py', (-4.5, 6.0), 9), (20, 8, 'px', (0.0, 3.6), 11),
            (16, 10, 'ny', (3.6, 0.0), 10), (28, 13, 'px', (0.0, 4.5), 4)]
    for (i, j, d, off, c) in cars:
        objs.append(('car%d_%s' % (c, d), i, j, 1, 1, off))
    # forests and trees on what is left
    kinds = ['forest_mixed%d', 'forest_spruce%d', 'forest_birch%d', 'forest_pine%d']
    for i in range(N):
        for j in range(N):
            if (i, j) in occ:
                continue
            h = ihash(i, j, 502)
            edge = (i < 3 or j > 30 or (i + j) < 12 or i > 31 or (j < 5 and i > 14))
            if edge and h < 0.75 or h < 0.12:
                place(kinds[int(ihash(i, j, 503) * 4)] % int(ihash(i, j, 505) * 3), i, j)
            elif h < 0.17:
                place('rock%d' % int(ihash(i, j, 506) * 2), i, j)
            elif h < 0.36:
                tk = ['tree_birch', 'tree_spruce', 'tree_pine', 'tree_birch_au', 'tree_rowan'][int(ihash(i, j, 504) * 5)]
                objs.append((tk, i, j, 1, 1))
    return N, G, objs


def add_smoke(S, N, objs, zoom):
    """smoke puffs above every hotspot sprite (rising column of frames)"""
    tw, th = 64 * zoom, 32 * zoom
    extra = []
    for ob in objs:
        name, i0, j0, n, m = ob[:5]
        img, anc, hot = S[name]
        if hot[0] < 0:
            continue
        # world position of the hotspot = south vertex + (hot - anc)
        si, sj = i0 + n, j0 + m
        for f in range(SMOKE_FRAMES):
            dx = int(round(f * 2.2 * zoom))
            dy = int(round(-f * 6.0 * zoom))
            extra.append(('smoke%d' % f, i0, j0, n, m, (0, 0), ('hot', si, sj, hot[0] - anc[0] + dx, hot[1] - anc[1] + dy)))
    return extra


def lot_tiles(objs):
    """(i, j) -> lot name for every footprint tile of every lot sprite in objs"""
    T = {}
    for ob in objs:
        nm = ob[0]
        if re.match(r'(\w+?)_v\d+_(?:ab_)?r(\d)$', nm):
            for a in range(ob[1], ob[1] + ob[3]):
                for b in range(ob[2], ob[2] + ob[4]):
                    T[(a, b)] = nm
    return T


def to_ground(objs):
    """overlay mode: lots drawn as their flat ground_<W>x<H>_r<R>"""
    out = []
    for ob in objs:
        mm = re.match(r'(\w+?)_v\d+_(?:ab_)?r(\d)$', ob[0])
        if mm:
            n, m = ob[3], ob[4]
            r = int(mm.group(2))
            cn, cm = (m, n) if r % 2 else (n, m)
            out.append(('ground_%dx%d_r%d' % (cn, cm, r),) + tuple(ob[1:]))
        else:
            out.append(ob)
    return out


def water_layer(N, G, objs):
    """mock water network for the overlay preview: pipes under the streets (as an old save migrates) plus a
    few branches; a pump feeds the west, its reach runs out to the east (short), the industrial estate's
    pipes are not connected (dry).  Returns (pipe objs, ovl objs)."""
    lots = lot_tiles(objs)      # industrial estate: its own pipes and lots are not connected to the pump (dry)
    pipes = set()
    for i in range(N):
        for j in range(N):
            if G[i][j].startswith(('grav_', 'road_', 'main_')):
                pipes.add((i, j))
    for j in range(5, 8):                       # pump (13, 4) -> street
        pipes.add((13, j))
    pipes -= set((i, 13) for i in range(16, N))  # main road east of the town: no pipe laid yet
    estate = set((i, 17) for i in range(17, 27))   # the industrial estate's own main: not connected (dry)
    pipes |= estate
    # BFS from the pump over pipes + lots (buildings conduct)
    src = (13, 4)
    dist = {src: 0}
    q = [src]
    k = 0
    while k < len(q):
        c = q[k]
        k += 1
        for (di, dj) in ((0, -1), (1, 0), (0, 1), (-1, 0)):
            n = (c[0] + di, c[1] + dj)
            if n in dist or not (0 <= n[0] < N and 0 <= n[1] < N):
                continue
            if n in estate:
                continue
            if n in pipes or (n in lots and not lots[n].startswith(('ind_', 'coal', 'element'))):
                dist[n] = dist[c] + 1
                q.append(n)
    reach = 17
    pobj, oobj = [], []
    for (i, j) in sorted(pipes):
        m = 0
        for bit, (di, dj) in ((1, (0, -1)), (2, (1, 0)), (4, (0, 1)), (8, (-1, 0))):
            if (i + di, j + dj) in pipes:
                m |= bit
        st = 'dry' if (i, j) not in dist else ('wet' if dist[(i, j)] <= reach else 'short')
        pobj.append(('pipe_%02d_%s' % (m, st), i, j, 1, 1))
    for (i, j), nm in sorted(lots.items()):
        lv = 4 if (i, j) not in dist else (3 if dist[(i, j)] <= reach else 1)
        oobj.append(('ovl_3_%d' % lv, i, j, 1, 1))
    return pobj, oobj


ZONE_OF = (('house_', 1), ('apt_', 2), ('com_', 3), ('ind_', 4), ('element', 4))


def zone_layer(N, G, objs):
    """persistent zone view mock: zone of every lot tile from its key, a few empty zoned tiles (zone_<Z> tint),
    zonev_<Z>_<MM> edge pieces by the front-edge rule (ART.md 6).  Returns objs to add."""
    lots = lot_tiles(objs)
    zone = {}
    for t, nm in lots.items():
        for pre, z in ZONE_OF:
            if nm.startswith(pre):
                zone[t] = z
    occupied = set(lots)
    for ob in objs:
        if ob[0].startswith(('forest', 'rock')):
            occupied.add((ob[1], ob[2]))
    extra = []
    empty = [((i, 21), 1) for i in range(4, 8)] + [((i, 22), 1) for i in range(4, 8)] + \
            [((26, j), 4) for j in range(14, 17)] + [((27, j), 3) for j in range(9, 12)]
    for (t, z) in empty:
        if t not in occupied and not G[t[0]][t[1]].startswith(('water', 'shore', 'grav', 'road', 'main', 'rail')):
            zone[t] = z
            extra.append(('zone_%d' % z, t[0], t[1], 1, 1))
    for i in range(N):
        for j in range(N):
            zt = zone.get((i, j), 0)
            for e, (di, dj) in ((2, (1, 0)), (4, (0, 1))):
                zn = zone.get((i + di, j + dj), 0)
                if zt == zn:
                    continue
                z = zt or zn
                extra.append(('zonev_%d_%02d' % (z, e), i, j, 1, 1))
    return extra


def mockup(S, zoom, W, H, centre, overlay=None):
    """overlay: None (normal view), 'bad' (pollution-style ramp), 'water' (pipes + water dither), 'zones'
    (persistent zone view over the normal view)"""
    N, G, objs = town_layout()
    tw, th = int(64 * zoom), int(32 * zoom)
    ox_, oy_ = N * tw // 2, int(220 * zoom)
    sm = []
    for e in add_smoke(S, N, objs, zoom):
        tag, si, sj, ddx, ddy = e[6]
        sx = ox_ + int(round((si - sj) * tw / 2.0)) + ddx
        sy = oy_ + int(round((si + sj) * th / 2.0)) + ddy
        sm.append(e[:6] + ((sx, sy),))
    if overlay == 'zones':
        objs = objs + zone_layer(N, G, objs)
    elif overlay == 'water':
        pobj, oobj = water_layer(N, G, objs)
        objs = to_ground(objs) + oobj + pobj
        sm = []
    elif overlay:
        objs = to_ground(objs)
        for i in range(N):
            for j in range(N):
                lv = int(clamp((14 - math.hypot(i - 22, j - 16)) / 1.6, 0, 7))
                if not G[i][j].startswith(('water', 'shore')):
                    objs.append(('ovl_0_%d' % lv, i, j, 1, 1))
        sm = []
    canvas, ox, oy = compose_map(S, N, G, objs + sm, zoom)
    ci, cj = centre
    cx = ox + (ci - cj) * tw / 2.0
    cy = oy + (ci + cj) * th / 2.0
    return canvas.crop((int(cx - W / 2), int(cy - H / 2), int(cx - W / 2) + W, int(cy - H / 2) + H))


def write_previews(S64, S32):
    os.makedirs(PREVIEW, exist_ok=True)
    for f in glob.glob(os.path.join(PREVIEW, '*.png')):
        os.remove(f)
    for S, z in ((S64, 64), (S32, 32)):
        sc = 2 if z == 64 else 3
        tiles = [n for n in sorted(S) if S[n][0].size == ((64, 32) if z == 64 else (32, 16)) and not n.startswith(('ovl', 'cur', 'zone'))]
        preview_sheet(S, tiles, scale=sc).save(os.path.join(PREVIEW, 'tiles_%d.png' % z))
        roads = [n for n in sorted(S) if n.startswith(ROAD_KEYS + ('rail', 'power', 'wire'))]
        preview_sheet(S, roads, scale=sc).save(os.path.join(PREVIEW, 'roads_%d.png' % z))
        misc = [n for n in sorted(S) if n.startswith(('ovl', 'cur', 'zone_', 'smoke', 'car', 'tree', 'forest', 'rock'))]
        pz = [n for n in sorted(S) if n.startswith(('pipe_', 'zonev'))]
        preview_sheet(S, pz, scale=sc).save(os.path.join(PREVIEW, 'pipes_zonev_%d.png' % z))
        preview_sheet(S, misc, scale=sc).save(os.path.join(PREVIEW, 'misc_%d.png' % z))
        for (key, fp, rots, vs, ab) in BUILDINGS:
            names = []
            for v in range(len(vs)):
                rr = rot_plan(fp[0], fp[1], rots[v])[0]
                names += ['%s_v%d_r%d' % (key, v, r) for r in rr]
                if ab_wanted(ab, v):
                    names += ['%s_v%d_ab_r%d' % (key, v, r) for r in rr]
            if z == 64 or key.startswith('house'):
                preview_sheet(S, names, scale=sc if z == 64 else 3).save(os.path.join(PREVIEW, 'b_%s_%d.png' % (key, z)))
        bg = [n for n in sorted(S) if n.startswith(('build_', 'ground_'))]
        preview_sheet(S, bg, scale=sc).save(os.path.join(PREVIEW, 'build_ground_%d.png' % z))
    mockup(S64, 1.0, 800, 600, (19.0, 13.5)).save(os.path.join(PREVIEW, 'town_64.png'))
    mockup(S64, 1.0, 800, 600, (11.0, 20.0)).save(os.path.join(PREVIEW, 'town_64_houses.png'))
    mockup(S64, 1.0, 800, 600, (19.0, 13.5), overlay=True).save(os.path.join(PREVIEW, 'town_64_overlay.png'))
    mockup(S32, 0.5, 800, 600, (17.0, 17.0)).save(os.path.join(PREVIEW, 'town_32.png'))
    # F2 previews: pipes in the water overlay, yards in the industrial strip, persistent zone view
    mockup(S64, 1.0, 800, 600, (18.5, 13.0), overlay='water').save(os.path.join(PREVIEW, 'town_64_water.png'))
    mockup(S32, 0.5, 800, 600, (17.0, 15.0), overlay='water').save(os.path.join(PREVIEW, 'town_32_water.png'))
    mockup(S64, 1.0, 800, 400, (19.5, 16.5)).save(os.path.join(PREVIEW, 'yards_64.png'))
    mockup(S64, 1.0, 800, 600, (18.0, 13.0), overlay='zones').save(os.path.join(PREVIEW, 'town_64_zones.png'))
    mockup(S32, 0.5, 800, 600, (16.0, 15.0), overlay='zones').save(os.path.join(PREVIEW, 'town_32_zones.png'))
    # M4: one review sheet per Milestone-2 group (every rendered sprite of the group's keys, both zooms) and the
    # 1960s-70s transformation mockups
    for z, S in ((64, S64), (32, S32)):
        bygrp = {}
        for (key, fp, rots, vs, ab) in BUILDINGS:
            for v in range(len(vs)):
                g = GROUPS.get('%s_v%d' % (key, v)) or (GROUPS.get(key) if key in M2_KEYS else None)
                if not g or (key in M2_KEYS and '%s_v%d' % (key, v) not in GROUPS and key not in GROUPS):
                    continue
                rr = rot_plan(fp[0], fp[1], rots[v])[0]
                names = ['%s_v%d_r%d' % (key, v, r) for r in rr]
                if ab_wanted(ab, v):
                    names += ['%s_v%d_ab_r%d' % (key, v, r) for r in rr]
                bygrp.setdefault(g, []).extend(names)
        for pre, g in GROUPS.items():
            if pre.endswith('*'):
                bygrp.setdefault(g, []).extend(n for n in sorted(S) if n.startswith(pre[:-1]))
        for g, names in sorted(bygrp.items()):
            slug = re.sub(r'[^a-z0-9]+', '_', g.lower()).strip('_')
            preview_sheet(S, names, scale=2 if z == 64 else 3, width=960).save(
                os.path.join(PREVIEW, 'm2_%s_%d.png' % (slug, z)))
    import art_mockup_m2
    art_mockup_m2.write(sys.modules[__name__], S64, S32, PREVIEW)


# =========================================================================== defs parsing (required names)
def _split_top(s):
    out, depth, cur = [], 0, ''
    i = 0
    while i < len(s):
        ch = s[i]
        if ch == '"':
            j = s.index('"', i + 1)
            cur += s[i:j + 1]
            i = j + 1
            continue
        if ch in '({':
            depth += 1
        elif ch in ')}':
            depth -= 1
        if ch == ',' and depth == 0:
            out.append(cur.strip())
            cur = ''
        else:
            cur += ch
        i += 1
    if cur.strip():
        out.append(cur.strip())
    return out


def parse_defs():
    """-> (buildings: list of dict(key, w, h, flags, variants, cat), road keys with bridge flag).
    Expands the ZB/PB style macros textually, so it follows T04's table format changes."""
    hp = os.path.join(PROJ, 'src', 'defs.h')
    cp = os.path.join(PROJ, 'src', 'defs.cpp')
    h = open(hp).read()
    c = open(cp).read()
    c_nc = re.sub(r'/\*.*?\*/', '', c, flags=re.S)
    c_nc = re.sub(r'//[^\n]*', '', c_nc)
    m = re.search(r'typedef struct \{((?:(?!typedef).)*?)\}\s*BuildingDef;', h, re.S)
    body = re.sub(r'/\*.*?\*/', '', m.group(1), flags=re.S)
    fields = []
    for decl in body.split(';'):
        decl = decl.replace('*', ' ').strip()
        if not decl:
            continue
        parts = decl.split(',')
        fields.append(re.findall(r'\w+', parts[0])[-1])
        for p in parts[1:]:
            fields.append(re.findall(r'\w+', p)[-1])
    fi = dict((f, i) for i, f in enumerate(fields))
    macros = {}
    for mm in re.finditer(r'#define\s+(\w+)\(([^)]*)\)((?:[^\n]*\\\n)*[^\n]*)', c_nc):
        macros[mm.group(1)] = ([a.strip() for a in mm.group(2).split(',')], mm.group(3).replace('\\\n', ' '))
    flagv = dict((k, int(v, 16)) for k, v in re.findall(r'(BDF_\w+)\s*=\s*(0x[0-9a-fA-F]+)', h))
    # P1: object-like flag macros in defs.cpp (e.g. M6's `#define MOD (BDF_MODULE | BDF_NO_ROTATE)` for the fixed-
    # orientation complex modules) - without this their BDF_NO_ROTATE was lost and r1-r3 counted as missing
    for om in re.finditer(r'#define\s+(\w+)\s+\(?\s*((?:BDF_\w+\s*\|?\s*)+)\)?\s*$', c_nc, re.M):
        flagv[om.group(1)] = sum(flagv.get(t.strip(), 0) for t in om.group(2).split('|') if t.strip())
    blk = re.search(r'g_bdefs\[B_COUNT\]\s*=\s*\{(.*?)\n\};', c_nc, re.S).group(1)
    rows = _split_top(blk)
    B = []
    for row in rows:
        row = row.strip()
        mm = re.match(r'(\w+)\s*\((.*)\)\s*$', row, re.S)
        if mm and mm.group(1) in macros:
            params, mbody = macros[mm.group(1)]
            args = _split_top(mm.group(2))
            exp = mbody
            for p, a in sorted(zip(params, args), key=lambda t: -len(t[0])):
                exp = re.sub(r'\b%s\b' % re.escape(p), a.replace('\\', '\\\\'), exp)
            row = exp.strip()
        if row.startswith('{'):
            row = row.strip()[1:-1]
        vals = _split_top(row)
        if len(vals) < len(fields):
            continue

        def num(v):
            v = v.strip()
            try:
                return int(v, 0)
            except ValueError:
                return v
        flags = 0
        for t in re.split(r'\s*\|\s*', vals[fi['flags']]):
            t = t.strip()
            if t in flagv:
                flags |= flagv[t]
            elif re.match(r'^(0x)?[0-9a-fA-F]+$', t):
                flags |= int(t, 0)
        B.append(dict(key=vals[fi['key']].strip('"'), w=num(vals[fi['w']]), h=num(vals[fi['h']]), flags=flags,
                      variants=num(vals[fi['variants']]), cat=vals[fi['cat']].strip()))
    rb = re.search(r'g_roads\[ROAD_CLASS_COUNT\]\s*=\s*\{(.*?)\n\};', c_nc, re.S).group(1)
    roads = []
    for row in _split_top(rb):
        vals = _split_top(row.strip()[1:-1])
        if vals and vals[0].strip('"') != 'none':
            roads.append((vals[0].strip('"'), 'RDF_BRIDGE_OK' in row))
    return B, roads


def required_names(B, roads, art_names):
    """the names the code will request (art.h naming rules).  Returns (missing, fallback_used, notes)"""
    req = ['grass0', 'grass1', 'grass2', 'water0', 'field_crop', 'field_plough', 'rock',
           'cur_ok', 'cur_bad', 'cur_sel', 'cur_query']
    req += ['water%d' % f for f in range(1, WATER_FRAMES)]
    req += ['shore%02d' % m for m in range(1, 15)]
    req += ['%s_%s_%d' % (k, a, f) for k in ('river', 'rapids') for a in ('x', 'y', 'n') for f in range(WATER_FRAMES)]
    req += ['forest_%s' % k for k in ('mixed', 'spruce', 'birch', 'pine')]
    req += ['zone_%d' % z for z in range(1, 7)]                       # F3: + 5 forestry, 6 farm
    req += ['zonev_%d' % z for z in range(1, 7)] + ['zonev_%d_%02d' % (z, e) for z in range(1, 7) for e in ZONEV_EDGES]
    req += ['pipe_%02d_%s' % (m, st) for m in range(16) for (st, c) in PIPE_STATES]
    req += ['ovl_%d_%d' % (r, l) for r in range(len(OVR_RAMPS)) for l in range(8)]
    req += ['power_%02d' % m for m in range(16)] + ['wire_%02d' % m for m in (5, 10)]
    req += ['car%d_%s' % (c, d) for c in range(9) for (d, r) in DIRS]
    req += ['smoke0']
    req += [n for n in art_names if n.startswith('tree_')][:1] or ['tree_birch']
    for (k, bridge) in roads:
        req += ['%s_%02d' % (k, m) for m in range(16)]
        if bridge:
            req += ['%s_bridge_05' % k, '%s_bridge_10' % k]
    missing = [n for n in req if n not in art_names]
    fallback = []
    notes = []
    for b in B:
        if b['key'] == 'none':
            continue
        nv = b['variants'] if isinstance(b['variants'], int) else 0
        art_v = 0
        while '%s_v%d_r0' % (b['key'], art_v) in art_names:
            art_v += 1
        if nv != art_v:
            notes.append('%-16s defs variants=%s art variants=%d' % (b['key'], b['variants'], art_v))
        if nv <= 0:
            continue
        rots = [0] if b['flags'] & 0x20 else [0, 1, 2, 3]
        for v in range(nv):
            for r in rots:
                n = '%s_v%d_r%d' % (b['key'], v, r)
                if n not in art_names:
                    missing.append(n)
                if b['cat'] == 'BCAT_ZONE' and '%s_v%d_ab_r%d' % (b['key'], v, r) not in art_names:
                    fallback.append('%s_v%d_ab_r%d' % (b['key'], v, r))
        for r in rots:
            for kind in ('build', 'ground'):
                if '%s_%s_r%d' % (b['key'], kind, r) not in art_names and \
                        '%s_%dx%d_r%d' % (kind, b['w'], b['h'], r) not in art_names:
                    missing.append('%s_%s_r%d (or %s_%dx%d_r%d)' % (b['key'], kind, r, kind, b['w'], b['h'], r))
    return missing, fallback, notes


# =========================================================================== check
def read_pieces():
    """PL4: parse art_world.h + sheet_world_<z>.h -> (pages [(sname, zoom)], {zoom: (pieces, sprites)});
    piece = (page, x, y, w, h, dx, dy), sprite = (name, w, h, ax, ay, hx, hy, first, count)"""
    idx = open(os.path.join(HERE, 'art_world.h')).read()
    pages = [(m.group(1), int(m.group(2))) for m in re.finditer(r'"art\\\\(world\d+_(\d+))\.bmp"', idx)]
    sets = {}
    for z in (64, 32):
        f = os.path.join(HERE, 'sheet_world_%d.h' % z)
        t = open(f).read() if os.path.exists(f) else ''
        pcs = [tuple(int(v) for v in m.groups())
               for m in re.finditer(r'\{ (\d+), (\d+), (\d+), (\d+), (\d+), (\d+), (\d+) \}', t)]
        spr = [(m.group(1),) + tuple(int(v) for v in m.groups()[1:])
               for m in re.finditer(r'\{ "(\w+)", (-?\d+), (-?\d+), (-?\d+), (-?\d+), (-?\d+), (-?\d+), (\d+), (\d+) \}', t)]
        sets[z] = (pcs, spr)
    return pages, sets


def logical_sprite(sheets, pcs, row):
    """PL4: the sprite image re-assembled from its pieces (sheets: page index -> RGB image)"""
    n, w, h, ax, ay, hx, hy, first, cnt = row
    img = Image.new('RGB', (max(w, 1), max(h, 1)), KEY)
    for (pg, x, y, pw, ph, dx, dy) in pcs[first:first + cnt]:
        img.paste(sheets[pg].crop((x, y, x + pw, y + ph)), (dx, dy))
    return img


def read_header_rects():
    """logical sprite rows per zoom: {'world_<z>': [(name, 0, 0, w, h, ax, ay, hx, hy)]} (PL4)"""
    pages, sets = read_pieces()
    return dict(('world_%d' % z, [(r[0], 0, 0) + tuple(r[1:7]) for r in sets[z][1]]) for z in sets)


def seam_test_ground():
    """material-class equality across shared tile edges, evaluated on the ground functions"""
    class G(object):
        pass

    class St(object):
        texture = STYLE_C.tex_ground
        zoom = 1.0
        boards = 0.7

    def cls(k):
        for pre, c in (('grass', 'g'), ('meadow', 'g'), ('flower', 'g'), ('asph', 'a'), ('road', 'a'), ('water', 'w'),
                       ('shallow', 'w'), ('reed', 'w'), ('gravel', 'r'), ('kerb', 'r'), ('ballast', 'b'), ('sleeper', 'b'),
                       ('rail', 'b'), ('sand', 's'), ('pebble', 's'), ('steel', 'x')):
            if k.startswith(pre):
                return c
        return k

    def key_at(fn, x, y):
        g = G()
        g.W = (x, y, 0.0)
        g.P = g.W
        g.px = g.py = -1
        return cls(fn(g, St)[0])

    bad = []
    # roads/rail: tile A at origin, neighbour across its SE (+X) edge and SW (+Y) edge
    fns = {}
    for key in ROAD_KEYS:
        for m in range(16):
            fns[(key, m)] = TR.road_ground(key, m)
    for m in range(16):
        fns[('rail', m)] = TR.rail_ground(m)
    for key in ROAD_KEYS + ('rail',):
        for a in range(16):
            for b in range(16):
                for (abit, bbit, axis) in ((2, 8, 'x'), (4, 1, 'y')):
                    if bool(a & abit) != bool(b & bbit):
                        continue
                    mism = 0
                    for k in range(64):
                        t = (k + 0.5) / 2.0
                        if axis == 'x':
                            ka, kb = key_at(fns[(key, a)], 31.85, t), key_at(fns[(key, b)], 0.15, t)
                        else:
                            ka, kb = key_at(fns[(key, a)], t, 31.85), key_at(fns[(key, b)], t, 0.15)
                        mism += ka != kb
                    if mism > 3:
                        bad.append('%s_%02d|%s_%02d %s-edge: %d/64' % (key, a, key, b, axis, mism))
    # shores: corner flags must agree on the shared edge
    for a in range(16):
        for b in range(16):
            ca = (a & 1, (a >> 1) & 1, (a >> 2) & 1, (a >> 3) & 1)
            cb = (b & 1, (b >> 1) & 1, (b >> 2) & 1, (b >> 3) & 1)
            for axis in ('x', 'y'):
                if axis == 'x' and not (ca[1] == cb[0] and ca[3] == cb[2]):
                    continue
                if axis == 'y' and not (ca[2] == cb[0] and ca[3] == cb[1]):
                    continue
                fa = (lambda cc: lambda g, st: TR.g_shore(g, st, cc) if 0 < sum(cc) < 4 else
                      (TR.g_water(g, st) if sum(cc) == 0 else TR.g_grass(g, st)))(ca)
                fb = (lambda cc: lambda g, st: TR.g_shore(g, st, cc) if 0 < sum(cc) < 4 else
                      (TR.g_water(g, st) if sum(cc) == 0 else TR.g_grass(g, st)))(cb)
                mism = 0
                for k in range(64):
                    t = (k + 0.5) / 2.0
                    if axis == 'x':
                        ka, kb = key_at(fa, 31.9, t), key_at(fb, 0.1, t)
                    else:
                        ka, kb = key_at(fa, t, 31.9), key_at(fb, t, 0.1)
                    lw = lambda k_: 'L' if k_ in ('g', 's') else 'W' if k_ == 'w' else k_
                    mism += lw(ka) != lw(kb)
                if mism > 3:
                    bad.append('shore %02d|%02d %s-edge: %d/64' % (a, b, axis, mism))
    return bad


def check(verbose=True):
    ok = True
    hdr = read_header_rects()
    pages, sets = read_pieces()
    pindex = dict((sn, k) for k, (sn, z) in enumerate(pages))
    total = 0
    names = {64: set(), 32: set()}
    for z in sets:
        names[z] = set(r[0] for r in sets[z][1])
    print('  pages (PL4: <= %dx%d, pieces trimmed per half-tile column):' % (SHEET_W, SHEET_MAX_H))
    for f in sorted(glob.glob(os.path.join(HERE, 'world*_*.bmp')), key=lambda f: (-int(f[:-4].split('_')[-1]), int(re.findall(r'world(\d+)_', f)[0]))):
        sname = os.path.basename(f)[:-4]
        d = open(f, 'rb').read()
        sig, _, _, _, off = struct.unpack('<2sIHHI', d[:14])
        isz, w, h, planes, bpp, comp = struct.unpack('<IiiHHI', d[14:34])
        im = Image.open(f).convert('RGB')
        near = n565 = 0
        for c in im.getdata():
            if c == KEY:
                continue
            if c[0] >= 248 and c[1] <= 8 and c[2] >= 248:
                near += 1
            elif to565(c) != c:
                n565 += 1
        vram = w * abs(h) * 2
        total += vram
        st = 'ok'
        npc = 0
        if sig != b'BM' or bpp != 24 or comp != 0 or off != 54 or w > SHEET_W or abs(h) > SHEET_MAX_H or near or n565:
            st = 'FAIL'
            ok = False
        if sname not in pindex:
            st = 'FAIL (not in art_world.h)'
            ok = False
        else:
            z = int(sname.split('_')[1])
            for r in sets[z][0]:
                if r[0] != pindex[sname]:
                    continue
                npc += 1
                if r[1] < 0 or r[2] < 0 or r[1] + r[3] > w or r[2] + r[4] > abs(h):
                    st = 'FAIL (piece outside)'
                    ok = False
        if verbose:
            print('    %-12s %3dx%-4d bpp=%d %4d pieces %6.0f KB  near-key=%d non565=%d  %s' %
                  (sname, w, abs(h), bpp, npc, vram / 1024.0, near, n565, st))
    for z in sets:                     # pieces inside their sprite; sprites point at valid pieces
        pcs, spr = sets[z]
        bad = [r[0] for r in spr if r[7] + r[8] > len(pcs) or any(
            p[5] < 0 or p[6] < 0 or p[5] + p[3] > r[1] or p[6] + p[4] > r[2] for p in pcs[r[7]:r[7] + r[8]])]
        print('  zoom %d: %d pages, %d pieces, %d sprite names; pieces outside their sprite: %s' %
              (z, sum(1 for (sn, zz) in pages if zz == z), len(pcs), len(spr), ', '.join(bad[:6]) if bad else 'none'))
        if bad:
            ok = False
    # per-zoom VRAM (DESIGN 14.5): only the active zoom's world sheets are resident, UI sheets always are
    zt = {64: 0, 32: 0}
    for f in glob.glob(os.path.join(HERE, 'world*_*.bmp')):
        w, h = Image.open(f).size
        zt[int(os.path.basename(f)[:-4].split('_')[1])] += w * h * 2
    ui = 0
    for f in sorted(glob.glob(os.path.join(HERE, 'ui_*.bmp')) + glob.glob(os.path.join(HERE, 'font_*.bmp'))):
        w, h = Image.open(f).size
        ui += w * h * 2
    print('  VRAM per zoom (16-bit; only the active zoom is resident, DESIGN 14.5):')
    for z, lim in ((64, VRAM_LIMIT_64), (32, VRAM_LIMIT_32)):
        st = 'ok' if zt[z] <= lim else 'FAIL'
        print('    world %d-zoom: %5.0f KB = %.2f MB  (limit %.1f MB, headroom %.0f KB = %.2f MB)  %s' %
              (z, zt[z] / 1024.0, zt[z] / 1048576.0, lim / 1048576.0, (lim - zt[z]) / 1024.0,
               (lim - zt[z]) / 1048576.0, st))
        if zt[z] > lim:
            ok = False
        elif z == 64 and lim - zt[z] < 150 * 1024:
            print('    WARN 64-zoom headroom below the 150 KB reserve (M4 plan)')
    print('    UI sheets + fonts: %4.0f KB (always resident; gen_ui.py)' % (ui / 1024.0))
    print('    resident at 64 zoom: %.0f KB = %.2f MB world + UI; both zooms together: %.2f MB (no longer a limit)'
          % ((zt[64] + ui) / 1024.0, (zt[64] + ui) / 1048576.0, total / 1048576.0))
    # both zooms carry the same names
    only64 = sorted(names[64] - names[32])
    only32 = sorted(names[32] - names[64])
    if only64 or only32:
        print('  FAIL names in one zoom only: 64:%s 32:%s' % (only64[:10], only32[:10]))
        ok = False
    # seamless tiling: 4x4 patches of flat tiles must have no holes
    for z in (64, 32):
        psheets = dict((k, Image.open(os.path.join(HERE, sn + '.bmp')).convert('RGB')) for k, (sn, zz) in enumerate(pages) if zz == z)
        rows = dict((r[0], r) for r in sets[z][1])
        tw, th = z, z // 2
        holes = []
        for nm in ['grass0', 'grass2', 'water1', 'field_crop', 'marsh', 'road_10', 'grav_05', 'main_15', 'rail_10', 'shore05',
                   'river_x_1', 'river_y_2', 'river_n_0', 'rapids_x_0', 'rapids_n_2']:
            t = logical_sprite(psheets, sets[z][0], rows[nm])
            m = key_mask(t)
            can = Image.new('RGB', (tw * 5, th * 5), KEY)
            for i in range(4):
                for j in range(4):
                    can.paste(t, (2 * tw + (i - j) * tw // 2, th // 4 + (i + j) * th // 2), m)
            hole = sum(1 for yy in range(int(th * 1.3), int(th * 3.4)) for xx in range(int(tw * 2.05), int(tw * 2.95))
                       if can.getpixel((xx, yy)) == KEY)
            if hole:
                holes.append('%s:%d' % (nm, hole))
        print('  seams %d: %s' % (z, 'ok (no holes in 4x4 patches)' if not holes else 'FAIL ' + ' '.join(holes)))
        if holes:
            ok = False
    bad = seam_test_ground()
    print('  edge match (ground functions, all autotile pairs): %s' % ('ok' if not bad else 'FAIL %d' % len(bad)))
    for b in bad[:12]:
        print('    ' + b)
    if bad:
        ok = False
    # required names from defs + art.h
    try:
        B, roads = parse_defs()
        for z in (64, 32):
            miss, fb, notes = required_names(B, roads, names[z])
            print('  required names (%d): %d missing%s' % (z, len(miss), (': ' + ', '.join(miss[:20])) if miss else ''))
            if miss:
                ok = False
            if fb and z == 64:
                print('    abandoned via fallback (v0_ab / normal): %d names' % len(fb))
        print('  defs vs art variants:')
        for n in notes:
            print('    ' + n)
        produced = set(b[0] for b in BUILDINGS) | set(ka[0] for ka in KEY_ALIASES)
        keys = set(b['key'] for b in B)
        nodef = sorted(produced - keys)
        if nodef:
            print('    art keys not in defs.cpp yet (%d; ART.md "Milestone-2 keys"): %s' % (len(nodef), ' '.join(nodef)))
    except Exception as e:      # noqa
        print('  WARN could not parse src/defs.*: %r' % (e,))
    # sprite reach above the footprint's north vertex (render.h ISO_MAX_SPRITE_UP = 256 >> zoom) and hotspots
    worst = {64: (0, ''), 32: (0, '')}
    badhot = []
    for sname, rows in hdr.items():
        z = int(sname.split('_')[1])
        for (n, x, y, w, h, ax, ay, hx, hy) in rows:
            if n.startswith(('car', 'smoke')):
                continue
            mm = re.match(r'.*_r(\d)$', n)
            fp = None
            km = re.match(r'(.+?)_v\d+_(?:ab_)?r(\d)$', n)
            if km and km.group(1) in BUILD_FP:
                b1 = BUILD_FP[km.group(1)]
                fp = b1 if int(km.group(2)) % 2 == 0 else (b1[1], b1[0])
            fm = re.match(r'(?:build|ground)_(\d)x(\d)_r(\d)$', n)
            if fm:
                a_, b_, r_ = int(fm.group(1)), int(fm.group(2)), int(fm.group(3))
                fp = (a_, b_) if r_ % 2 == 0 else (b_, a_)
            if fp is None:
                fp = (1, 1)
            up = ay - (fp[0] + fp[1]) * z // 4
            if up > worst[z][0]:
                worst[z] = (up, n)
            if (hx, hy) != (-1, -1) and not (0 <= hx < w and 0 <= hy < h):
                badhot.append(n)
    for z in (64, 32):
        lim = 256 if z == 64 else 128
        print('  tallest reach above the north vertex (%d): %d px (%s), limit %d %s' %
              (z, worst[z][0], worst[z][1], lim, 'ok' if worst[z][0] <= lim else 'FAIL'))
        if worst[z][0] > lim:
            ok = False
    print('  hotspots outside their sprite: %s' % (', '.join(badhot[:8]) if badhot else 'none'))
    if badhot:
        ok = False
    # VRAM by family (piece areas, both zooms; aliases and shared pieces not counted twice) (PL4)
    parea = {}
    for z in sets:
        pcs, spr = sets[z]
        for r in spr:
            parea[('world_%d' % z, r[0])] = (r[7], sum(p[3] * p[4] for p in pcs[r[7]:r[7] + r[8]]))
    fam = {}
    seen = set()
    for sname, rows in hdr.items():
        for (n, x, y, w, h, ax, ay, hx, hy) in rows:
            x, a_ = parea[(sname, n)]
            w, h = a_, 1
            if (sname, x) in seen:
                continue
            seen.add((sname, x))
            km = re.match(r'(.+?)_v\d+_(ab_)?r\d$', n)
            if km:
                f = 'abandoned' if km.group(2) else km.group(1)[:3] if km.group(1)[:3] in ('apt', 'com', 'ind') else \
                    'houses' if km.group(1).startswith('house') else 'services/utilities'
            elif n.startswith(('build_', 'ground_')):
                f = 'construction + lot ground'
            elif n.startswith(ROAD_KEYS + ('rail', 'power', 'wire')):
                f = 'roads/rail/bridges/power'
            elif n.startswith(('pipe_', 'zonev')):
                f = 'pipes + zone view'
            elif n.startswith(('ovl', 'cur', 'zone', 'smoke', 'car')):
                f = 'overlays/cursors/cars/smoke'
            else:
                f = 'terrain/nature'
            fam[f] = fam.get(f, 0) + w * h * 2
    print('  VRAM by family: ' + ', '.join('%s %d KB' % (k, v // 1024) for k, v in sorted(fam.items(), key=lambda kv: -kv[1])))
    # Milestone-2 groups (unique sprite rects, aliases free), per zoom
    grp = {}
    seen = set()
    for sname, rows in hdr.items():
        z = int(sname.split('_')[1])
        for (n, x, y, w, h, ax, ay, hx, hy) in rows:
            x, a_ = parea[(sname, n)]
            w, h = a_, 1
            if (sname, x) in seen:
                continue
            seen.add((sname, x))
            g = None
            km = re.match(r'(.+?)_v(\d+)_(?:ab_)?r\d$', n)
            if km and km.group(1) in M2_KEYS:
                g = GROUPS.get('%s_v%s' % (km.group(1), km.group(2))) or GROUPS.get(km.group(1))
            else:
                for pre, gg in GROUPS.items():
                    if pre.endswith('*') and n.startswith(pre[:-1]):
                        g = gg
            if g:
                grp.setdefault(g, {64: 0, 32: 0})[z] += w * h * 2
    if grp:
        print('  Milestone-2 art by group (sprite area, KB at 64 / 32):')
        t64 = t32 = 0
        for g in sorted(grp):
            print('    %-44s %6.1f / %5.1f' % (g, grp[g][64] / 1024.0, grp[g][32] / 1024.0))
            t64 += grp[g][64]
            t32 += grp[g][32]
        print('    %-44s %6.1f / %5.1f' % ('total', t64 / 1024.0, t32 / 1024.0))
    cnt = dict((z, len(names[z])) for z in names)
    print('  sprite names: 64: %d, 32: %d' % (cnt[64], cnt[32]))
    print('  check: %s' % ('PASS' if ok else 'FAIL'))
    return ok


# =========================================================================== main
def main():
    args = sys.argv[1:]
    if '--check' in args:
        sys.exit(0 if check() else 1)
    only = None
    if '--only' in args:
        only = args[args.index('--only') + 1].split(',')
    nproc = None
    if '-j' in args:
        nproc = int(args[args.index('-j') + 1])
    t0 = time.time()
    jobs = build_jobs()
    print('gen_art: %d jobs' % len(jobs))
    os.makedirs(CACHE, exist_ok=True)
    cpath = os.path.join(CACHE, 'sprites.pickle')
    rendered = {}
    repack = '--repack' in args          # PL4: cached sprites only (re-cut + re-pack, no rendering)
    if (only or repack) and os.path.exists(cpath):
        with open(cpath, 'rb') as f:
            rendered = pickle.load(f)
        valid = set(o[0] for j in jobs for o in j.outs)
        rendered = dict((k, v) for k, v in rendered.items() if k[0] in valid)
    if not repack:
        rendered.update(render(jobs, only, nproc))
        with open(cpath, 'wb') as f:
            pickle.dump(rendered, f, protocol=pickle.HIGHEST_PROTOCOL)
    for f in glob.glob(os.path.join(HERE, 'world*_*.bmp')):
        os.remove(f)
    zsets = []
    SS = {}
    for (z, zz) in ZOOMS:
        S = make_sprites(rendered, zz)
        SS[zz] = S
        pages, placed, spr = write_sheets(S, zz)
        zsets.append((zz, pages, placed, spr, S))
    write_headers(zsets, aliases())
    if '--nopreview' not in args and not repack:
        for zz in SS:
            for (a, t) in resolve_aliases(aliases(), SS[zz]):
                if t in SS[zz]:
                    SS[zz][a] = SS[zz][t]
        write_previews(SS[64], SS[32])
    print('gen_art: done in %.1fs' % (time.time() - t0))
    sys.exit(0 if check() else 1)


if __name__ == '__main__':
    main()
