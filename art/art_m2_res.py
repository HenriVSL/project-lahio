"""art_m2_res.py - Milestone-2 world art (task M4), group A: apartments T2-T3, commercial T3, civic services.
See gen_art.py "Milestone-2 modules" and ART.md.

Canonical front = +Y (the access road) like every lot.  rots 2 models are semantically 180-degree symmetric
(entrances, paths and balconies on both long sides), rots 1 square models are orientation-free."""
from art_m2_common import *  # noqa: F401,F403
from art_m2_common import _v, add_colours  # noqa: F401

add_colours({
    'res_panel_paint': '#bdb7a8',    # painted precast panel (warm light grey)
    'res_wash': '#aaa293',           # washed concrete (pesubetoni) elementtitalo
    'res_klinker': '#80553f',        # brown ceramic tile panels (klinkkeri)
    'res_klinker_dk': '#634333',
    'res_balc_sky': '#6f93b4',       # 1970s balcony fronts
    'res_balc_olive': '#7a8240',
    'res_balc_mustard': '#c19a3a',
    'res_balc_orange': '#c47a3a',
    'res_stone': '#c4bdae',          # 1950s bank: light stone cladding
    'res_granite': '#7d7672',        # granite plinth
    'res_brass': '#a88d4f',          # brass sign band
    'res_white': '#dcd8cc',          # Aalto-white render (library)
    'res_post': '#c88a3a',           # post sign band (orange-yellow)
    'res_police': '#4f6a9a',         # police blue (lamp, sign)
    'res_amb': '#e0ddd2',            # ambulance body
    'res_cross': '#a8392e',          # red cross / stripe
    'res_neon': '#b4503c',           # rooftop neon sign (unlit red)
})


# =========================================================================== shared pieces
def _gravel(md, rects, seed):
    md.ground = lambda g, st: g_lot(g, st, gravel=rects, var=seed)


def heater_posts(md, x0, x1, y, step=4.0):
    """block-heater posts (lammitystolpat) in a row along X"""
    x = x0
    while x <= x1:
        md.add(box(x - 0.35, y - 0.35, 0, x + 0.35, y + 0.35, 2.6, m_flat('steel_lt'), detail=True, noshadow=True))
        x += step


def slab_x(md, x0, y0, x1, y1, storeys, wall, stairs, pl=3.0, joints=None, speckle=False, frame='trim',
           roofkey='bitumen', brick=False, plinthkey='plinth', lift=True):
    """flat-roof slab block along X, identical long faces (doors + stair strips on both sides)"""
    ze = pl + storeys * STOREY
    wins = {'y+': [], 'y-': [], 'x+': [], 'x-': []}
    doors = {'y+': [], 'y-': []}
    for k in range(storeys):
        t0 = pl + k * STOREY + 2.5
        t1 = t0 + 5.5
        x = x0 + 3.0
        while x + 3.5 <= x1 - 2.0:
            if all(abs(x + 1.75 - sx) > 4.0 for sx in stairs):
                wins['y+'].append((x, x + 3.5, t0, t1))
                wins['y-'].append((x, x + 3.5, t0, t1))
            x += 5.0
        d = y1 - y0
        for f in ('x+', 'x-'):
            wins[f] += [(y0 + 2.5, y0 + 5.5, t0, t1), (y1 - 5.5, y1 - 2.5, t0, t1)] if d >= 14 else \
                [(y0 + d / 2 - 1.5, y0 + d / 2 + 1.5, t0, t1)]
    for sx in stairs:
        for f in ('y+', 'y-'):
            doors[f].append((sx - 1.75, sx + 1.75, pl - 0.5, pl + 7.0))
            for k in range(storeys):
                t0 = pl + k * STOREY + 8.0
                if t0 + 4.5 < ze:
                    wins[f].append((sx - 1.25, sx + 1.25, t0, t0 + 4.5))
    md.add(box(x0, y0, 0, x1, y1, ze + 1.2, m_block, wall=wall, plinth=pl, wins=wins, doors=doors, joints=joints,
               roofkey=roofkey, frame=frame, speckle=speckle, brickwork=brick, plinthkey=plinthkey))
    if lift:
        for sx in stairs:
            md.add(box(sx - 3.0, (y0 + y1) / 2 - 2.5, ze, sx + 3.0, (y0 + y1) / 2 + 2.5, ze + 5.0, m_block,
                       wall=wall, plinth=-1, roofkey=roofkey, parapet=0.0, joints=joints))
    for sx in stairs:                      # entrance canopies, both sides
        md.add(box(sx - 3.0, y1, pl + 7.5, sx + 3.0, y1 + 2.2, pl + 8.3, m_flat('panel_dk', 0.5)))
        md.add(box(sx - 3.0, y0 - 2.2, pl + 7.5, sx + 3.0, y0, pl + 8.3, m_flat('panel_dk', 0.5)))
    return ze


def balc_cols(x0, x1, stairs, step, w, clear):
    out = []
    x = x0 + 6.0
    while x + w <= x1 - 4.0:
        if all(abs(x + w / 2 - sx) > clear for sx in stairs):
            out.append(x)
        x += step
    return out


def slab_balconies(md, xs, y0, y1, storeys, keys, pl=3.0, depth=3.0, w=6.0, rail=False):
    """projecting balcony slabs on both long faces (storeys 1..n-1)"""
    for i, bx in enumerate(xs):
        key = keys[i % len(keys)]
        for k in range(1, storeys):
            z = pl + k * STOREY
            md.add(box(bx, y1, z - 0.6, bx + w, y1 + depth, z + 4.0, m_balcony, key=key, rail=rail))
            md.add(box(bx, y0 - depth, z - 0.6, bx + w, y0, z + 4.0, m_balcony, key=keys[(i + 1) % len(keys)],
                       rail=rail))


def balcony_towers(md, xs, y0, y1, storeys, keys, wallkey, pl=3.0, depth=3.6, w=7.0):
    """stacked balcony towers (parvekepielet): concrete side walls the full height, coloured fronts per storey"""
    ze = pl + storeys * STOREY
    for i, bx in enumerate(xs):
        for (ya, yb, sg) in ((y1, y1 + depth, 1), (y0 - depth, y0, -1)):
            key = keys[(i + (0 if sg > 0 else 1)) % len(keys)]
            for wx in (bx - 0.9, bx + w):
                md.add(box(wx, ya, pl + STOREY - 1.0, wx + 0.9, yb, ze - 0.5, m_concrete, key=wallkey))
            for k in range(1, storeys):
                z = pl + k * STOREY
                fy0, fy1 = (yb - 0.8, yb) if sg > 0 else (ya, ya + 0.8)
                md.add(box(bx, ya, z - 0.6, bx + w, yb, z + 0.2, m_flat('panel_dk', -0.2)))     # floor slab
                md.add(box(bx, fy0, z + 0.2, bx + w, fy1, z + 4.4, m_balcony, key=key))       # front panel


# =========================================================================== apartments
def precast_slab31(name, wall='res_panel_paint', balcs=('balc_mustard', 'balc_white', 'balc_red'), seed=5,
                   storeys=5):
    """apt_t2_3x1 v1: 5-storey precast panel slab, balconies + entrances on both long sides (rots 2)"""
    md = Model(name, 3, 1)
    x0, x1, y0, y1 = 3.0, 93.0, 9.0, 23.0
    stairs = [x0 + 22.5, x0 + 67.5]
    slab_x(md, x0, y0, x1, y1, storeys, wall, stairs, joints=(STOREY, 10.0), frame='panel_dk')
    slab_balconies(md, [6.0, 13.0, 32.0, 39.0, 51.5, 58.5, 77.5, 84.5], y0, y1, storeys, list(balcs), depth=3.0,
                   w=5.5)
    gr = [(sx - 2.0, 0.0, sx + 2.0, 32.0, 'gravel') for sx in stairs]
    gr += [(x0 - 2, y1 + 3.5, x1 + 2, y1 + 5.0, 'gravel'), (x0 - 2, y0 - 5.0, x1 + 2, y0 - 3.5, 'gravel')]
    add_birch(md, 1.8, 29.5, h=28, seed=seed + 31)
    add_bush(md, 49.0, 29.0, seed=seed + 5)
    add_bush(md, 94.0, 3.0, seed=seed + 9)
    heater_posts(md, 60.0, 72.0, 30.5)
    _gravel(md, gr, seed)
    return md


def precast_slab22(name, wall='panel', balcs=('balc_blue', 'balc_mustard'), seed=6, storeys=5):
    """apt_t2_2x2 v1: one 5-storey precast I-slab across the lot, courtyards on both sides (rots 2)"""
    md = Model(name, 2, 2)
    x0, x1, y0, y1 = 3.0, 61.0, 25.0, 39.0
    stairs = [19.0, 45.0]
    slab_x(md, x0, y0, x1, y1, storeys, wall, stairs, joints=(STOREY, 10.0), frame='panel_dk')
    slab_balconies(md, [5.5, 25.0, 32.0, 51.5], y0, y1, storeys, list(balcs), depth=3.0, w=5.5)
    gr = [(sx - 2.0, 0.0, sx + 2.0, 64.0, 'gravel') for sx in stairs]
    gr += [(0.0, 46.0, 64.0, 49.0, 'gravel'), (0.0, 15.0, 64.0, 18.0, 'gravel')]
    # front courtyard: sandbox, carpet rack, birch; back courtyard: swings, pines on granite
    add_sandbox(md, 32.0, 54.0)
    add_carpet_rack(md, 52.0, 55.0)
    add_birch(md, 6.0, 57.0, h=30, seed=seed + 63)
    add_bush(md, 58.0, 60.0, seed=seed + 64)
    add_swings(md, 32.0, 9.0)
    md.add(HeightField(42.0, 2.0, 62.0, 14.0, 3.0,
                       lambda x, y: 3.0 * max(0.0, 1.0 - ((x - 52.0) / 9.0) ** 2 - ((y - 8.0) / 5.0) ** 2) ** 0.6, m_rock))
    add_pine(md, 52.0, 7.0, h=38, seed=seed + 61)
    add_pine(md, 6.0, 6.0, h=36, seed=seed + 62)
    heater_posts(md, 24.0, 40.0, 61.5)
    _gravel(md, gr, seed)
    return md


def element_slab(name, wall='res_wash', balcs=('res_balc_orange', 'res_balc_mustard', 'res_balc_olive'),
                 tower='panel', seed=21, storeys=8, speckle=True, car=('orange', 'lblue')):
    """apt_t3_3x1 elementtitalo: storey-high panels with dark joints, stacked balcony towers on both long
    sides, flat black roof with lift rooms, block-heater posts and parked cars (rots 2)"""
    md = Model(name, 3, 1)
    x0, x1, y0, y1 = 3.0, 93.0, 10.0, 22.0
    stairs = [x0 + 15.0, x0 + 45.0, x0 + 75.0]
    slab_x(md, x0, y0, x1, y1, storeys, wall, stairs, joints=(STOREY, 6.0), speckle=speckle, frame='door')
    xs = [x for x in (x0 + 4.5, x0 + 24.0, x0 + 34.0, x0 + 54.0, x0 + 64.0, x0 + 82.5)]
    balcony_towers(md, xs, y0, y1, storeys, list(balcs), tower, depth=3.4, w=6.5)
    heater_posts(md, 6.0, 30.0, 31.0, 4.0)
    heater_posts(md, 66.0, 90.0, 1.0, 4.0)
    add_car(md, 46.0, 28.6, car[0], 'x')
    add_car(md, 52.0, 3.4, car[1], 'x')
    add_bush(md, 94.5, 29.5, seed=seed + 3, size=0.8)
    add_bush(md, 1.5, 2.5, seed=seed + 4, size=0.8)
    gr = [(0.0, 26.0, 96.0, 32.0, 'asphalt'), (0.0, 0.0, 96.0, 6.0, 'asphalt')]
    gr += [(sx - 2.0, 0.0, sx + 2.0, 32.0, 'gravel') for sx in stairs]
    _gravel(md, gr, seed)
    return md


def point_block(name, wall='render_white', balcs=('res_balc_sky', 'balc_white'), seed=31, storeys=9, brick=False,
                frame='trim'):
    """apt_t3_2x2 pistetalo: square point block, wrap-around corner balconies, stair strip on every face,
    lift machine room; pines and granite in the corners, paths to all four sides (rots 1)"""
    md = Model(name, 2, 2)
    pl = 3.0
    x0, y0, x1, y1 = 19.0, 19.0, 45.0, 45.0
    ze = pl + storeys * STOREY
    wins = {'y+': [], 'y-': [], 'x+': [], 'x-': []}
    doors = {}
    for k in range(storeys):
        t0 = pl + k * STOREY + 2.5
        for f in wins:
            for s in (x0 + 1.5, x0 + 7.6, x1 - 10.6, x1 - 4.5):
                wins[f].append((s, s + 3.0, t0, t0 + 5.5))
    for f in wins:
        doors[f] = [(30.5, 33.5, pl - 0.5, pl + 7.0)]
        for k in range(storeys):
            t0 = pl + k * STOREY + 8.0
            if t0 + 4.5 < ze:
                wins[f].append((30.8, 33.2, t0, t0 + 4.5))
    md.add(box(x0, y0, 0, x1, y1, ze + 1.2, m_block, wall=wall, plinth=pl, wins=wins, doors=doors, roofkey='bitumen',
               frame=frame, brickwork=brick, plinthkey='concrete_dk'))
    md.add(box(28.0, 28.0, ze, 36.0, 36.0, ze + 7.0, m_block, wall=wall, plinth=-1, roofkey='bitumen', parapet=0.0,
               brickwork=brick, wins={'y+': [(31.0, 33.0, ze + 2.0, ze + 5.0)]}))
    md.add(box(25.0, 33.0, ze, 27.0, 35.0, ze + 3.0, m_flat('steel')))
    md.add(Cone(39.0, 25.0, ze + 1.2, ze + 8.0, 0.25, 0.25, m_flat('steel_lt'), detail=True))     # TV antenna
    md.add(box(36.5, 24.9, ze + 6.5, 41.5, 25.1, ze + 6.9, m_flat('steel_lt'), detail=True))
    # corner balconies wrapping each corner, colours alternate by storey
    d, L = 3.0, 7.0
    for k in range(1, storeys):
        z = pl + k * STOREY
        key = balcs[k % len(balcs)]
        for (cx, cy, sx, sy) in ((x1, y1, 1, 1), (x0, y1, -1, 1), (x1, y0, 1, -1), (x0, y0, -1, -1)):
            ax0, ax1 = sorted((cx - sx * L, cx + sx * d))
            ay0, ay1 = sorted((cy, cy + sy * d))
            md.add(box(ax0, ay0, z - 0.6, ax1, ay1, z + 4.0, m_balcony, key=key))
            bx0, bx1 = sorted((cx, cx + sx * d))
            by0, by1 = sorted((cy - sy * L, cy))
            md.add(box(bx0, by0, z - 0.6, bx1, by1, z + 4.0, m_balcony, key=key))
    gr = [(30.0, 0.0, 34.0, 64.0, 'gravel'), (0.0, 30.0, 64.0, 34.0, 'gravel'),
          (17.0, 17.0, 47.0, 47.0, 'gravel')]
    # granite humps with pines / birches in the corners; the front corner stays low (bush + rock)
    for (cx, cy, hh) in ((8.0, 8.0, 4.0), (56.0, 8.0, 3.0), (8.0, 56.0, 3.0), (55.0, 55.0, 2.5)):
        md.add(HeightField(cx - 7.5, cy - 6.0, cx + 7.5, cy + 6.0, hh,
                           (lambda c1, c2, h_: lambda x, y: h_ * max(0.0, 1.0 - ((x - c1) / 7.0) ** 2 -
                                                                       ((y - c2) / 5.5) ** 2) ** 0.6)(cx, cy, hh),
                           m_rock))
    add_pine(md, 5.0, 54.0, h=42, seed=seed + 61)
    add_birch(md, 10.0, 60.0, h=28, seed=seed + 64)
    add_pine(md, 55.0, 5.0, h=40, seed=seed + 63)
    add_pine(md, 60.0, 11.0, h=34, seed=seed + 62)
    add_bush(md, 56.0, 56.0, seed=seed + 65)
    add_carpet_rack(md, 50.0, 22.0)
    add_sandbox(md, 13.0, 47.0)
    _gravel(md, gr, seed)
    return md


# =========================================================================== commercial T3
def bank_stone(name, seed=41):
    """com_t3_1x1 v0: 1950s bank, light stone cladding on a granite plinth, tall banking-hall windows, brass sign"""
    md = Model(name)
    pl = 3.5
    x0, x1, y0, y1 = 6.0, 26.0, 5.0, 22.0
    ze = pl + 3 * STOREY - 1.0
    up = [pl + 11.5, pl + 20.5]
    wins = {'y+': [(x0 + 2.0, x0 + 5.5, pl + 0.5, pl + 7.5), (x1 - 5.5, x1 - 2.0, pl + 0.5, pl + 7.5)],
            'x+': [(y0 + 2.0 + 4.5 * i, y0 + 5.0 + 4.5 * i, pl + 0.5, pl + 7.5) for i in range(3)],
            'x-': [], 'y-': []}
    for t0 in up:
        wins['y+'] += wins_row(x0, x1, t0, t0 + 5.0, 2.6, 4)
        wins['y-'] += wins_row(x0, x1, t0, t0 + 5.0, 2.6, 4)
        wins['x+'] += wins_row(y0, y1, t0, t0 + 5.0, 2.6, 3)
        wins['x-'] += wins_row(y0, y1, t0, t0 + 5.0, 2.6, 3)
    md.add(box(x0, y0, 0, x1, y1, ze, m_block, wall='res_stone', plinth=pl, plinthkey='res_granite', wins=wins,
               doors={'y+': [(14.4, 17.6, pl, pl + 7.5, 'door')]}, roofkey='bitumen', frame='trim',
               bands={'y+': [(x0 + 1.0, x1 - 1.0, pl + 8.0, pl + 10.3, 'res_brass', 'text')],
                      'x+': [(y0 + 1.0, y1 - 1.0, pl + 8.0, pl + 10.3, 'res_brass', 'text')]}, joints=(5.0, 4.0)))
    md.add(box(x0 - 0.5, y0 - 0.5, ze - 1.2, x1 + 0.5, y1 + 0.5, ze, m_flat('res_stone', 0.4)))         # cornice
    md.add(box(13.5, y1, 0, 18.5, y1 + 2.6, 2.0, m_flat('res_granite', 0.3)))                           # steps
    md.add(box(13.5, y1 + 2.6, 0, 18.5, y1 + 4.0, 1.0, m_flat('res_granite', 0.3)))
    md.add(box(x0 + 3.0, y0 + 3.0, ze, x0 + 6.0, y0 + 6.0, ze + 2.5, m_flat('steel')))
    add_flagpole(md, 29.0, 27.0, 30.0, flag=True)
    add_birch(md, 29.0, 6.0, h=28, seed=seed + 1)
    _gravel(md, [(0.0, y1, 32.0, 32.0, 'asphalt'), (x1, 0.0, 32.0, y1, 'gravel')], seed)
    return md


def bank_glass(name, seed=42):
    """com_t3_1x1 v1: 1960s savings bank, glazed ground floor under a thin cantilevered slab, ribbon window
    above, blue sign on the fascia"""
    md = Model(name)
    pl = 1.0
    x0, x1, y0, y1 = 5.0, 25.0, 6.0, 22.0
    ze = pl + 2 * STOREY + 2.0
    rib = [(s, s + 3.0, pl + STOREY + 3.0, pl + STOREY + 7.5) for s in [x0 + 1.5 + 3.0 * i for i in range(5)]]
    ribx = [(s, s + 3.0, pl + STOREY + 3.0, pl + STOREY + 7.5) for s in [y0 + 1.0 + 3.0 * i for i in range(4)]]
    wins = {'y+': [(x0 + 1.0 + 4.5 * i, x0 + 5.5 + 4.5 * i, pl + 0.5, pl + 8.0) for i in range(4) if i != 2] + rib,
            'x+': [(y0 + 1.0 + 4.5 * i, y0 + 5.5 + 4.5 * i, pl + 0.5, pl + 8.0) for i in range(3)] + ribx,
            'y-': rib, 'x-': ribx}
    md.add(box(x0, y0, 0, x1, y1, ze, m_block, wall='render_white', plinth=pl, plinthkey='res_granite', wins=wins,
               doors={'y+': [(x0 + 10.5, x0 + 13.5, pl, pl + 8.0, 'steel')]}, roofkey='bitumen', frame='panel_dk',
               wkind='shop'))
    md.add(box(x0 - 1.0, y0 - 1.0, pl + 8.6, x1 + 2.5, y1 + 2.5, pl + 10.0, m_block, wall='panel_dk', plinth=-1,
               roofkey='panel', parapet=0.0,
               bands={'y+': [(x0 + 2.0, x1 - 2.0, pl + 8.6, pl + 10.0, 'sign_blue', 'text')],
                      'x+': [(y0 + 2.0, y1 - 2.0, pl + 8.6, pl + 10.0, 'sign_blue', 'text')]}))
    md.add(box(x1 + 1.6, y1 + 1.6, 0, x1 + 2.3, y1 + 2.3, pl + 8.6, m_flat('steel_lt'), detail=True))
    md.add(box(x0 + 2.0, y0 + 2.0, ze, x0 + 7.0, y0 + 5.0, ze + 2.0, m_flat('steel')))
    add_car(md, 22.0, 28.5, 'beige', 'x')
    add_bush(md, 29.0, 8.0, seed=seed + 2)
    _gravel(md, [(0.0, 24.5, 32.0, 32.0, 'asphalt'), (27.0, 0.0, 32.0, 24.5, 'gravel')], seed)
    return md


def ribbon(lo, hi, t0, t1, pane=3.0, margin=1.5):
    """continuous horizontal ribbon window as adjacent panes (the 1 px frames read as mullions)"""
    out = []
    s = lo + margin
    while s + pane <= hi - margin + 0.01:
        out.append((s, s + pane, t0, t1))
        s += pane
    return out


def office_band(name, seed=51, storeys=5):
    """com_t3_2x1 v0: virastotalo, concrete office slab with horizontal window ribbons on both long faces,
    entrances with canopies on both sides, plant room (rots 2)"""
    md = Model(name, 2, 1)
    pl = 1.0
    x0, x1, y0, y1 = 4.0, 60.0, 9.0, 23.0
    ze = pl + storeys * STOREY + 1.0
    wins = {'y+': [], 'y-': [], 'x+': [], 'x-': []}
    for k in range(storeys):
        t0 = pl + k * STOREY + 3.0
        t1 = t0 + 4.8
        if k == 0:
            for f in ('y+', 'y-'):
                wins[f] += ribbon(x0, 27.0, t0 - 1.0, t1 + 1.0) + ribbon(37.0, x1, t0 - 1.0, t1 + 1.0)
        else:
            for f in ('y+', 'y-'):
                wins[f] += ribbon(x0, x1, t0, t1)
        for f in ('x+', 'x-'):
            wins[f] += [(y0 + 5.5, y1 - 5.5, t0, t1)]
    doors = {'y+': [(29.0, 35.0, pl, pl + 7.5, 'steel')], 'y-': [(29.0, 35.0, pl, pl + 7.5, 'steel')]}
    md.add(box(x0, y0, 0, x1, y1, ze, m_block, wall='panel', plinth=pl, plinthkey='res_granite', wins=wins,
               doors=doors, roofkey='bitumen', frame='panel_dk', wkind='band', joints=(STOREY, 14.0)))
    for (ya, yb) in ((y1, y1 + 3.0), (y0 - 3.0, y0)):
        md.add(box(27.5, ya, pl + 8.0, 36.5, yb, pl + 9.0, m_block, wall='panel_dk', plinth=-1, roofkey='panel',
                   parapet=0.0, bands={'y+': [(28.0, 36.0, pl + 8.0, pl + 9.0, 'sign_blue', 'text')]}))
    md.add(box(26.0, 13.0, ze, 38.0, 19.0, ze + 4.5, m_block, wall='panel_dk', plinth=-1, roofkey='bitumen', parapet=0.0))
    add_birch(md, 2.0, 29.5, h=28, seed=seed + 1)
    add_birch(md, 62.0, 2.5, h=28, seed=seed + 2)
    add_car(md, 48.0, 28.5, 'grey', 'x')
    _gravel(md, [(0.0, 26.0, 64.0, 32.0, 'asphalt'), (0.0, 0.0, 64.0, 6.0, 'asphalt'),
                 (29.0, 0.0, 35.0, 32.0, 'gravel')], seed)
    return md


def office_brick(name, seed=52, storeys=4):
    """com_t3_2x1 v1: red-brick office with a copper-clad top storey and vertical window strips (rots 2)"""
    md = Model(name, 2, 1)
    pl = 1.5
    x0, x1, y0, y1 = 5.0, 59.0, 9.0, 23.0
    zb = pl + (storeys - 1) * STOREY
    wins = {'y+': [], 'y-': [], 'x+': [], 'x-': []}
    for k in range(storeys - 1):
        t0 = pl + k * STOREY + 2.0
        for f in ('y+', 'y-'):
            wins[f] += [r for r in wins_row(x0, x1, t0, t0 + 6.5, 3.4, 10) if not (k == 0 and 27.0 < r[0] < 36.0)]
        for f in ('x+', 'x-'):
            wins[f] += wins_row(y0, y1, t0, t0 + 6.5, 3.4, 2)
    doors = {'y+': [(30.0, 34.0, pl, pl + 7.5)], 'y-': [(30.0, 34.0, pl, pl + 7.5)]}
    md.add(box(x0, y0, 0, x1, y1, zb, m_block, wall='brick', plinth=pl, plinthkey='res_granite', wins=wins,
               doors=doors, roofkey='bitumen', frame='render_white', brickwork=True, wkind='band'))
    cw = {f: [] for f in ('y+', 'y-', 'x+', 'x-')}
    for f in ('y+', 'y-'):
        cw[f] = ribbon(x0, x1, zb + 2.5, zb + 7.0, pane=3.0, margin=2.0)
    for f in ('x+', 'x-'):
        cw[f] = ribbon(y0, y1, zb + 2.5, zb + 7.0, pane=3.0, margin=2.0)
    md.add(box(x0 - 0.6, y0 - 0.6, zb, x1 + 0.6, y1 + 0.6, zb + STOREY + 0.5, m_block, wall='copper', plinth=-1,
               wins=cw, roofkey='bitumen', frame='copper', joints=(3.5, 3.0)))
    for ya, yb in ((y1, y1 + 2.4), (y0 - 2.4, y0)):
        md.add(box(28.5, ya, pl + 7.8, 35.5, yb, pl + 8.6, m_flat('copper', 0.4)))
    md.add(box(x0 + 4.0, 14.0, zb + STOREY + 0.5, x0 + 7.0, 17.0, zb + STOREY + 4.0, m_chimney, key='brick'))
    add_flagpole(md, 2.0, 29.0, 28.0, flag=True)
    add_bush(md, 62.0, 29.0, seed=seed + 3)
    add_bush(md, 2.0, 3.0, seed=seed + 4)
    _gravel(md, [(0.0, 25.5, 64.0, 32.0, 'asphalt'), (0.0, 0.0, 64.0, 6.5, 'asphalt')], seed)
    return md


def dept_store(name, seed=61, storeys=5, wall='render_white'):
    """com_t3_2x2 v0: tavaratalo, a 5-storey box with glazed shop fronts and awnings on both long sides, window
    ribbons above, a rooftop neon sign frame (fictional pseudo-text) (rots 2)"""
    md = Model(name, 2, 2)
    pl = 1.0
    x0, x1, y0, y1 = 5.0, 59.0, 15.0, 49.0
    ze = pl + storeys * STOREY + 2.0
    wins = {'y+': [], 'y-': [], 'x+': [], 'x-': []}
    for f in ('y+', 'y-'):
        wins[f] += [(x0 + 1.5 + 8.5 * i, x0 + 8.5 + 8.5 * i, pl + 0.8, pl + 7.8) for i in range(6) if i != 3]
    for f in ('x+', 'x-'):
        wins[f] += [(y0 + 2.0 + 8.0 * i, y0 + 8.5 + 8.0 * i, pl + 0.8, pl + 7.8) for i in range(4)]
    for k in range(1, storeys):
        t0 = pl + k * STOREY + 3.0
        for f in ('y+', 'y-'):
            wins[f] += ribbon(x0, x1, t0, t0 + 4.6)
        for f in ('x+', 'x-'):
            wins[f] += ribbon(y0, y1, t0, t0 + 4.6)
    doors = {'y+': [(31.0, 37.0, pl, pl + 7.8, 'steel')], 'y-': [(31.0, 37.0, pl, pl + 7.8, 'steel')]}
    md.add(box(x0, y0, 0, x1, y1, ze, m_block, wall=wall, plinth=pl, plinthkey='res_granite', wins=wins, doors=doors,
               roofkey='bitumen', frame='panel_dk', wkind='band',
               bands={'y+': [(x0 + 1.0, x1 - 1.0, pl + 8.5, pl + STOREY + 1.5, 'sign_red', 'text')],
                      'y-': [(x0 + 1.0, x1 - 1.0, pl + 8.5, pl + STOREY + 1.5, 'sign_red', 'text')]}))
    md.add(box(x0, y1, pl + 7.9, x1, y1 + 2.8, pl + 8.5, m_awning, key='awning'))
    md.add(box(x0, y0 - 2.8, pl + 7.9, x1, y0, pl + 8.5, m_awning, key='awning'))
    # rooftop neon sign: steel frame + red letter board along X
    for px in (14.0, 32.0, 50.0):
        md.add(box(px - 0.4, 31.6, ze, px + 0.4, 32.4, ze + 4.0, m_flat('steel'), detail=True))
    md.add(box(12.0, 31.4, ze + 4.0, 52.0, 32.6, ze + 8.5, m_block, wall='res_neon', plinth=-1, roofkey='steel',
               parapet=0.0, bands={'y+': [(12.0, 52.0, ze + 4.0, ze + 8.5, 'res_neon', 'text')],
                                   'y-': [(12.0, 52.0, ze + 4.0, ze + 8.5, 'res_neon', 'text')]}))
    md.add(box(8.0, 18.0, ze, 16.0, 24.0, ze + 4.0, m_flat('steel')))
    md.add(box(48.0, 40.0, ze, 56.0, 46.0, ze + 4.0, m_flat('steel')))
    add_car(md, 15.0, 58.0, 'red', 'x')
    add_car(md, 49.0, 6.0, 'mustard', 'x')
    add_birch(md, 3.0, 61.0, h=28, seed=seed + 1)
    add_birch(md, 61.0, 3.0, h=28, seed=seed + 2)
    _gravel(md, [(0.0, 52.0, 64.0, 64.0, 'asphalt'), (0.0, 0.0, 64.0, 12.0, 'asphalt'),
                 (0.0, 49.0, 64.0, 52.0, 'kerb'), (0.0, 12.0, 64.0, 15.0, 'kerb')], seed)
    return md


def ostari(name, seed=62):
    """com_t3_2x2 v1: lahio shopping centre (ostari): one storey of shop fronts with fascia signs on all four
    sides under a continuous canopy, an upper office storey in the middle, a clock / sign tower (rots 1)"""
    md = Model(name, 2, 2)
    pl = 0.8
    x0, y0, x1, y1 = 10.0, 10.0, 54.0, 54.0
    ze = pl + 11.0
    signs = ['sign_green', 'sign_red', 'sign_blue', 'sign_yel']
    wins, bands, doors = {}, {}, {}
    for fi, f in enumerate(('y+', 'x+', 'y-', 'x-')):
        wins[f] = []
        bands[f] = []
        doors[f] = [(30.0, 34.0, pl, pl + 7.5, 'steel')]
        for i in range(3):
            a = x0 + 1.0 + i * 14.6
            wins[f].append((a + 0.6, a + 9.0, pl + 0.8, pl + 7.5) if i != 1 else (a + 0.6, a + 3.6, pl + 0.8, pl + 7.5))
            if i == 1:
                wins[f].append((a + 9.4, a + 13.4, pl + 0.8, pl + 7.5))
            bands[f].append((a + 0.4, a + 14.0, pl + 8.6, ze - 0.4, signs[(i + fi) % 4], 'text'))
    md.add(box(x0, y0, 0, x1, y1, ze, m_block, wall='render_yel', plinth=pl, wins=wins, bands=bands, doors=doors,
               roofkey='bitumen', frame='steel_lt', wkind='shop'))
    # continuous canopy on posts
    c = 2.6
    md.add(box(x0 - c, y0 - c, pl + 7.9, x1 + c, y1 + c, pl + 8.5, m_flat('panel_dk', 0.5)))
    for (px, py) in ((x0 - c + 0.6, y0 - c + 0.6), (x1 + c - 0.6, y0 - c + 0.6), (x0 - c + 0.6, y1 + c - 0.6),
                     (x1 + c - 0.6, y1 + c - 0.6), (32.0, y1 + c - 0.6), (x1 + c - 0.6, 32.0)):
        md.add(box(px - 0.3, py - 0.3, 0, px + 0.3, py + 0.3, pl + 7.9, m_flat('steel_lt'), detail=True))
    # upper office storey
    uw = {f: ribbon(20.0, 44.0, ze + 2.5, ze + 7.0) for f in ('y+', 'x+', 'y-', 'x-')}
    md.add(box(20.0, 20.0, ze, 44.0, 44.0, ze + 9.5, m_block, wall='panel', plinth=-1, wins=uw, roofkey='bitumen',
               frame='panel_dk', wkind='band'))
    # clock / sign tower
    tz = ze + 9.5
    ct = {f: [(30.4, 33.6, tz + 13.0, tz + 16.2)] for f in ('y+', 'x+', 'y-', 'x-')}
    md.add(box(29.5, 29.5, tz, 34.5, 34.5, tz + 20.0, m_block, wall='render_white', plinth=-1, roofkey='bitumen',
               wins=ct, frame='res_brass', wkind='clock',
               bands={f: [(29.5, 34.5, tz + 4.0, tz + 10.0, 'sign_red', 'text')] for f in ('y+', 'x+', 'y-', 'x-')}))
    add_car(md, 46.0, 59.5, 'yellow', 'x')
    add_car(md, 59.5, 18.0, 'green', 'y')
    add_birch(md, 3.5, 60.5, h=30, seed=seed + 1)
    add_birch(md, 60.5, 3.5, h=28, seed=seed + 2)
    add_bench(md, 20.0, 59.0)
    _gravel(md, [(0.0, 0.0, 64.0, 64.0, 'asphalt'), (6.0, 6.0, 58.0, 58.0, 'kerb')], seed)
    return md


# =========================================================================== civic services
def police_station(name, seed=71):
    """police (1x1): plain two-storey rendered office (poliisilaitos), blue sign band, blue lamp, patrol car"""
    md = Model(name)
    pl = 1.2
    x0, x1, y0, y1 = 5.0, 25.0, 5.0, 20.0
    ze = pl + 2 * STOREY + 1.0
    up = pl + STOREY + 3.0
    wins = {'y+': [(x0 + 2.0, x0 + 5.5, pl + 2.5, pl + 7.5), (x1 - 5.5, x1 - 2.0, pl + 2.5, pl + 7.5)] +
            wins_row(x0, x1, up, up + 5.0, 3.0, 4),
            'y-': wins_row(x0, x1, pl + 2.5, pl + 7.5, 3.0, 4) + wins_row(x0, x1, up, up + 5.0, 3.0, 4),
            'x+': wins_row(y0, y1, pl + 2.5, pl + 7.5, 3.0, 3) + wins_row(y0, y1, up, up + 5.0, 3.0, 3),
            'x-': wins_row(y0, y1, up, up + 5.0, 3.0, 3)}
    md.add(box(x0, y0, 0, x1, y1, ze, m_block, wall='render_yel', plinth=pl, wins=wins,
               doors={'y+': [(13.5, 16.5, pl, pl + 7.5, 'door')]}, roofkey='bitumen', frame='trim',
               bands={'y+': [(x0 + 1.0, x1 - 1.0, pl + 8.2, pl + 10.4, 'res_police', 'text')]}))
    md.add(box(12.5, y1, pl + 7.6, 17.5, y1 + 2.4, pl + 8.2, m_flat('panel_dk', 0.5)))
    md.add(box(18.4, y1, pl + 5.4, 19.6, y1 + 1.0, pl + 7.0, m_flat('res_police', 0.8), detail=True))   # blue lamp
    md.add(box(x0 + 3.0, y0 + 3.0, ze, x0 + 6.0, y0 + 6.0, ze + 2.5, m_flat('steel')))
    md.add(Cone(x1 - 3.0, y0 + 3.0, ze, ze + 9.0, 0.25, 0.25, m_flat('steel_lt'), detail=True))     # radio mast
    add_car(md, 18.0, 27.5, 'dblue', 'x')
    add_flagpole(md, 28.5, 22.0, 28.0, flag=True)
    add_bush(md, 3.0, 26.0, seed=seed + 1)
    _gravel(md, [(0.0, 23.5, 32.0, 32.0, 'asphalt'), (13.0, y1, 17.0, 23.5, 'gravel')], seed)
    return md


def library(name, seed=72):
    """library (1x1): white modernist (Aalto-like): a low entrance wing and a reading hall with a mono-pitch
    roof rising to a clerestory, big glazed front"""
    md = Model(name)
    pl = 1.0
    # reading hall: mono pitch rising toward -Y, clerestory band on the high side
    hx0, hx1, hy0, hy1 = 12.0, 27.0, 4.0, 22.0
    md.add(mono_body(hx0, hy0, hx1, hy1, 0, 11.0, 19.0, m_block, wall='res_white', plinth=pl, rise='y-',
                     roofkey='copper', frame='trim',
                     wins={'x+': ribbon(hy0, hy1, 4.0, 9.0, pane=2.6, margin=1.5),
                           'y-': ribbon(hx0, hx1, 12.0, 17.0, pane=2.5, margin=1.5),
                           'x-': [(hy0 + 2.0, hy0 + 6.0, 12.0, 16.0)],
                           'y+': [(hx0 + 2.0, hx1 - 2.0, 3.0, 8.5)]}))
    md.add(mono_body(hx0 - 0.6, hy0 - 0.6, hx1 + 0.6, hy1 + 0.6, 18.6, 11.0, 19.4, m_flat('copper', 0.2), rise='y-'))
    # clerestory monitor on the high edge
    md.add(box(hx0, hy0, 19.0, hx1, hy0 + 3.0, 22.0, m_block, wall='res_white', plinth=-1, roofkey='copper',
               parapet=0.0, wins={'y+': ribbon(hx0, hx1, 19.6, 21.6, pane=2.5, margin=1.0),
                                  'x+': [(hy0 + 0.6, hy0 + 2.4, 19.6, 21.6)]}))
    # low entrance / lending wing
    wx0, wx1, wy0, wy1 = 4.0, 12.0, 9.0, 22.0
    md.add(box(wx0, wy0, 0, wx1, wy1, 8.5, m_block, wall='res_white', plinth=pl, roofkey='bitumen', frame='trim',
               wins={'y+': [(wx0 + 1.0, wx0 + 5.0, pl + 1.0, pl + 6.5)], 'x-': ribbon(wy0, wy1, 3.0, 6.5, 2.6)},
               doors={'y+': [(wx0 + 5.5, wx1 - 0.5, pl, pl + 6.5, 'glass')]}))
    md.add(box(wx0 + 4.5, wy1, pl + 6.6, wx1 + 1.0, wy1 + 2.4, pl + 7.3, m_flat('trim', 0.3)))
    md.add(box(hx0 + 1.0, hy1, 9.0, hx1 - 1.0, hy1 + 0.5, 10.8, m_block, wall='sign_green', plinth=-1,
               bands={'y+': [(hx0 + 1.0, hx1 - 1.0, 9.0, 10.8, 'sign_green', 'text')]}))
    add_birch(md, 29.0, 5.0, h=28, seed=seed + 1)
    add_bush(md, 29.0, 27.0, seed=seed + 2)
    add_bench(md, 8.0, 27.0)
    _gravel(md, [(5.0, 22.0, 13.0, 32.0, 'gravel'), (0.0, 24.0, 22.0, 30.0, 'gravel')], seed)
    return md


def post_office(name, seed=73):
    """post & telephone exchange (1x1): two-storey rendered block, orange post sign band, a mailbox by the door,
    a phone booth, a radio mast on the roof (the exchange)"""
    md = Model(name)
    pl = 1.2
    x0, x1, y0, y1 = 4.0, 23.0, 5.0, 21.0
    ze = pl + 2 * STOREY + 1.5
    up = pl + STOREY + 3.0
    wins = {'y+': [(x0 + 1.5, x0 + 7.0, pl + 1.0, pl + 7.5), (x1 - 6.5, x1 - 1.5, pl + 1.0, pl + 7.5)] +
            ribbon(x0, x1, up, up + 4.8, pane=2.7, margin=1.4),
            'y-': ribbon(x0, x1, up, up + 4.8, pane=2.7, margin=1.4) + wins_row(x0, x1, pl + 3.0, pl + 7.0, 2.6, 4),
            'x+': wins_row(y0, y1, pl + 2.5, pl + 7.5, 3.0, 3) + ribbon(y0, y1, up, up + 4.8, pane=2.7, margin=1.4),
            'x-': ribbon(y0, y1, up, up + 4.8, pane=2.7, margin=1.4)}
    md.add(box(x0, y0, 0, x1, y1, ze, m_block, wall='render_grey', plinth=pl, wins=wins, wkind='band',
               doors={'y+': [(x0 + 8.0, x0 + 11.0, pl, pl + 7.5, 'door')]}, roofkey='bitumen', frame='trim',
               bands={'y+': [(x0 + 1.0, x1 - 1.0, pl + 8.2, pl + 10.6, 'res_post', 'text')],
                      'x+': [(y0 + 1.0, y1 - 1.0, pl + 8.2, pl + 10.6, 'res_post', 'text')]}))
    md.add(box(x0 + 13.6, y1, pl + 3.4, x0 + 15.4, y1 + 1.2, pl + 6.0, m_flat('res_post', 0.4)))      # mailbox
    # radio mast (lattice approximated by a slim tapering pole with two cross bars)
    md.add(Cone(x0 + 4.0, y0 + 4.0, ze, ze + 20.0, 0.7, 0.25, m_flat('steel_lt'), detail=True))
    for z in (ze + 9.0, ze + 16.0):
        md.add(box(x0 + 1.5, y0 + 3.8, z, x0 + 6.5, y0 + 4.2, z + 0.5, m_flat('steel_lt'), detail=True))
    md.add(box(x1 - 6.0, y0 + 3.0, ze, x1 - 2.0, y0 + 7.0, ze + 2.5, m_flat('steel')))
    # phone booth
    md.add(box(26.0, 22.0, 0, 29.0, 25.0, 7.0, m_block, wall='sign_red', plinth=0.5, roofkey='sign_red', parapet=0.0,
               wins={'y+': [(26.6, 28.4, 1.5, 6.0)], 'x+': [(22.6, 24.4, 1.5, 6.0)], 'x-': [(22.6, 24.4, 1.5, 6.0)]}))
    add_birch(md, 28.0, 7.0, h=28, seed=seed + 1)
    _gravel(md, [(0.0, 23.0, 32.0, 32.0, 'asphalt'), (x0 + 7.0, y1, x0 + 12.0, 23.0, 'gravel'),
                 (24.5, 20.5, 30.5, 26.5, 'gravel')], seed)
    return md


def ambulance(md, cx, cy, heading='x'):
    """white van with a red stripe and a roof light (STYLE.md 9 scale, like the Lada)"""
    gp = new_grp()
    if heading == 'x':
        x0, x1, y0, y1 = cx - 8.0, cx + 8.0, cy - 3.3, cy + 3.3
    else:
        x0, x1, y0, y1 = cx - 3.3, cx + 3.3, cy - 8.0, cy + 8.0
    for (a, b) in ((0.15, 0.3), (0.7, 0.85)):
        if heading == 'x':
            wx = x0 + (x1 - x0) * a
            md.add(box(wx, y0 + 0.2, 0, wx + 2.4, y1 - 0.2, 2.2, m_flat('tyre'), grp=gp))
        else:
            wy = y0 + (y1 - y0) * a
            md.add(box(x0 + 0.2, wy, 0, x1 - 0.2, wy + 2.4, 2.2, m_flat('tyre'), grp=gp))
    md.add(box(x0, y0, 1.2, x1, y1, 8.2, m_block, wall='res_amb', plinth=-1, roofkey='res_amb', parapet=0.0, grp=gp,
               bands={f: [(-999, 999, 4.0, 5.0, 'res_cross')] for f in ('y+', 'y-', 'x+', 'x-')},
               wins={'x+': [(y0 + 0.6, y1 - 0.6, 5.4, 7.6)]} if heading == 'x' else
               {'y+': [(x0 + 0.6, x1 - 0.6, 5.4, 7.6)]}))
    md.add(box(cx - 1.0, cy - 0.6, 8.2, cx + 1.0, cy + 0.6, 9.0, m_flat('res_police', 0.8), detail=True, grp=gp))


def health_centre(name, seed=81):
    """health centre (2x2, terveyskeskus 1972): two-storey flat-roof block, brick piers between white concrete
    window bands, entrance canopies on both long sides, ambulance bays at both ends, parking (rots 2)"""
    md = Model(name, 2, 2)
    pl = 1.0
    x0, x1, y0, y1 = 10.0, 54.0, 19.0, 45.0
    ze = pl + 2 * STOREY + 2.5
    wins = {'y+': [], 'y-': [], 'x+': [], 'x-': []}
    bands = {'y+': [], 'y-': [], 'x+': [], 'x-': []}
    for k in range(2):
        t0 = pl + k * STOREY + 3.0
        for f in ('y+', 'y-'):
            wins[f] += [r for r in ribbon(x0, x1, t0, t0 + 4.6) if k == 1 or not (27.0 < r[0] < 37.0)]
            bands[f].append((x0, x1, t0 - 1.6, t0, 'panel'))
        for f in ('x+', 'x-'):
            wins[f] += ribbon(y0, y1, t0, t0 + 4.6) if k == 1 else ribbon(y0, y1 - 13.0, t0, t0 + 4.6)
            bands[f].append((y0, y1, t0 - 1.6, t0, 'panel'))
    for f in bands:
        bands[f].append(((x0 if f[0] == 'y' else y0), (x1 if f[0] == 'y' else y1), ze - 2.4, ze, 'panel'))
    doors = {'y+': [(28.5, 35.5, pl, pl + 7.2, 'glass')], 'y-': [(28.5, 35.5, pl, pl + 7.2, 'glass')],
             'x+': [(y1 - 11.0, y1 - 3.0, 0.0, pl + 7.0, 'steel')], 'x-': [(y0 + 3.0, y0 + 11.0, 0.0, pl + 7.0, 'steel')]}
    md.add(box(x0, y0, 0, x1, y1, ze, m_block, wall='brick', plinth=pl, plinthkey='concrete_dk', wins=wins,
               bands=bands, doors=doors, roofkey='bitumen', frame='door', wkind='band', brickwork=True))
    for (ya, yb, sg) in ((y1, y1 + 4.5, 1), (y0 - 4.5, y0, -1)):         # entrance canopies + blue sign
        md.add(box(26.0, ya, pl + 7.4, 38.0, yb, pl + 8.4, m_block, wall='panel', plinth=-1, roofkey='panel',
                   parapet=0.0, bands={'y+' if sg > 0 else 'y-': [(26.5, 37.5, pl + 7.4, pl + 8.4, 'sign_blue', 'text')]}))
        py = yb - 0.5 if sg > 0 else ya + 0.5
        for px in (26.5, 37.5):
            md.add(box(px - 0.3, py - 0.3, 0, px + 0.3, py + 0.3, pl + 7.4, m_flat('steel_lt'), detail=True))
    # ambulance bay canopies at both ends
    for (xa, xb, ya, yb) in ((x1, x1 + 7.0, y1 - 13.0, y1), (x0 - 7.0, x0, y0, y0 + 13.0)):
        md.add(box(xa, ya, pl + 8.0, xb, yb, pl + 9.0, m_flat('panel', 0.3)))
        px = xb - 0.5 if xa == x1 else xa + 0.5
        for py in (ya + 0.5, yb - 0.5):
            md.add(box(px - 0.3, py - 0.3, 0, px + 0.3, py + 0.3, pl + 8.0, m_flat('steel_lt'), detail=True))
    md.add(box(16.0, 25.0, ze, 24.0, 31.0, ze + 4.0, m_flat('steel')))
    md.add(box(40.0, 33.0, ze, 48.0, 39.0, ze + 4.0, m_flat('steel')))
    ambulance(md, x1 + 3.6, y1 - 6.5, 'y')
    add_car(md, 46.0, 57.0, 'white', 'x')
    add_car(md, 14.0, 7.0, 'lblue', 'x')
    add_birch(md, 4.0, 60.0, h=24, seed=seed + 1)
    add_birch(md, 60.0, 4.0, h=24, seed=seed + 2)
    add_bush(md, 20.0, 58.5, seed=seed + 3)
    add_bush(md, 44.0, 5.5, seed=seed + 4)
    _gravel(md, [(0.0, 52.0, 64.0, 64.0, 'asphalt'), (0.0, 0.0, 64.0, 12.0, 'asphalt'),
                 (28.0, 45.0, 36.0, 52.0, 'kerb'), (28.0, 12.0, 36.0, 19.0, 'kerb'),
                 (54.0, 12.0, 64.0, 52.0, 'asphalt'), (0.0, 12.0, 10.0, 52.0, 'asphalt')], seed)
    return md


# =========================================================================== table
ENTRIES = [
    ('apt_t2_3x1', (3, 1), 2, [_v(precast_slab31, 'a2v1')], None),
    ('apt_t2_2x2', (2, 2), 2, [_v(precast_slab22, 'a2bv1')], None),
    ('apt_t3_3x1', (3, 1), 2, [
        _v(element_slab, 'a3', seed=21),
        _v(element_slab, 'a3', wall='res_klinker', balcs=('balc_white', 'res_balc_sky'), tower='res_klinker_dk',
           seed=22, speckle=False, car=('red', 'green')),
    ], 'v0'),
    ('apt_t3_2x2', (2, 2), 1, [
        _v(point_block, 'a3b', seed=31),
        _v(point_block, 'a3b', wall='brick', balcs=('balc_white', 'res_balc_mustard'), seed=32, brick=True),
    ], 'v0'),
    ('com_t3_1x1', (1, 1), 4, [_v(bank_stone, 'b3'), _v(bank_glass, 'b3')], 'v0'),
    ('com_t3_2x1', (2, 1), 2, [_v(office_band, 'o3'), _v(office_brick, 'o3')], 'v0'),
    ('com_t3_2x2', (2, 2), [2, 1], [_v(dept_store, 'd3'), _v(ostari, 'd3')], 'v0'),
    ('police', (1, 1), 4, [_v(police_station, 'po')], None),
    ('library', (1, 1), 4, [_v(library, 'li')], None),
    ('post', (1, 1), 4, [_v(post_office, 'pt')], None),
    ('health_centre', (2, 2), 2, [_v(health_centre, 'hc')], None),
]
KEY_ALIASES = []
GROUPS = {
    'apt_t2_3x1_v1': '1 apartments T2-T3', 'apt_t2_2x2_v1': '1 apartments T2-T3',
    'apt_t3_3x1': '1 apartments T2-T3', 'apt_t3_2x2': '1 apartments T2-T3',
    'com_t3_1x1': '2 commercial T3', 'com_t3_2x1': '2 commercial T3', 'com_t3_2x2': '2 commercial T3',
    'police': '3 services (civic)', 'library': '3 services (civic)', 'post': '3 services (civic)',
    'health_centre': '3 services (civic)',
}
