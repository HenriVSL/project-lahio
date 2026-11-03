"""art_m2_ind.py - Milestone-2 world art (task M4, group B): industry kinds, district heating, sewage works,
two extra 1x1 yard variants.  See gen_art.py "Milestone-2 modules" and ART.md "Milestone-2 keys".

Rotation policy: rots 2 = the model is semantically 180-degree symmetric about the lot centre (doors, paths and
yards reach both long sides, props at mirrored corners), so r2/r3 reuse the r0/r1 renders; rots 1 (sewage
works) = 90-degree symmetric.  Front = +Y at r0 like every lot (both long sides are fronts for rots 2)."""
from art_m2_common import *  # noqa: F401,F403
from art_m2_common import _v, add_colours  # noqa: F401

add_colours({
    'ind_tractor': '#a65a3e',      # muted "Kontio" orange-red
    'ind_stainless': '#b9bdbc',    # milk silos / tanks
    'ind_milkcan': '#c6c8c4',
    'ind_cable': '#7a5c42',        # wooden cable drums
    'ind_bale': '#c9bfa6',         # cotton bales in sacking
    'ind_sludge': '#565a4c',       # clarifier water (greyish green, dull)
    'ind_oiltank': '#c4c3bb',
})


# =========================================================================== helpers
def lantern_hall(md, x0, y0, x1, y1, hw, wall='brick', pitch=4.0, wins=None, doors=None, roof='roof_dark',
                 lantern=True):
    """long industrial hall along X: brick body, low gable roof, a glazed monitor (lantern) along the ridge"""
    mid = (y0 + y1) / 2.0
    zr = hw + pitch
    md.add(gable_body(x0, y0, x1, y1, 0, hw, zr, 'x', m_brickwall, wall=wall, wins=wins or {}, doors=doors or {}))
    md.add(gable_roof(x0, y0, x1, y1, hw, zr, 'x', m_roof, ov=0.8, ovg=0.8, th=1.2, key=roof,
                      edgekey='brick_dk' if wall == 'brick' else 'trim', seam=4.0))
    if lantern:
        lw = max(3.0, (y1 - y0) * 0.14)
        lx0, lx1 = x0 + 5.0, x1 - 5.0
        lz0, lz1 = zr - 1.0, zr + 4.6
        lwins = [(lx0 + 1.0 + 4.0 * i, lx0 + 3.6 + 4.0 * i, lz0 + 0.6, lz1 - 0.3)
                 for i in range(int((lx1 - lx0 - 1.0) / 4.0))]
        md.add(box(lx0, mid - lw, lz0, lx1, mid + lw, lz1, m_corr, wall='steel', plinth=-1, frame='steel',
                   wins={'y+': lwins, 'y-': lwins}))
        md.add(gable_roof(lx0, mid - lw, lx1, mid + lw, lz1, lz1 + 1.6, 'x', m_roof, ov=0.7, ovg=0.5, th=0.9,
                          key=roof, edgekey='steel_lt', seam=4.0))


def hall_wins(x0, x1, z0, z1, w=4.0, gap=7.0):
    out = []
    x = x0 + 3.0
    while x + w <= x1 - 2.0:
        out.append((x, x + w, z0, z1))
        x += gap
    return out


def add_truck(md, cx, cy, heading='x', body='w_red', cab='sign_blue', tank=False, load=None):
    """small period lorry (Sisu-like), about 18 x 6 units; heading +X or +Y (cab at the + end)"""
    gp = new_grp()
    L, Wd = 9.0, 3.0
    if heading == 'x':
        for wx in (-6.5, -3.5, 5.0):
            md.add(box(cx + wx - 1.1, cy - Wd + 0.2, 0, cx + wx + 1.1, cy + Wd - 0.2, 2.2, m_flat('tyre'), grp=gp))
        md.add(box(cx + 3.0, cy - Wd, 1.2, cx + L, cy + Wd, 7.5, m_flat(cab), grp=gp))
        md.add(box(cx + 6.6, cy - Wd + 0.4, 4.6, cx + L + 0.05, cy + Wd - 0.4, 6.8, m_flat('glass'), grp=gp))
        if tank:
            md.add(box(cx - L, cy - Wd + 0.3, 1.4, cx + 2.6, cy + Wd - 0.3, 2.2, m_flat('steel'), grp=gp))
            md.add(Ellipsoid(cx - 3.2, cy, 5.0, 6.0, 2.8, 2.8 * KZ * 0.9, m_flat(body), grp=gp))
        else:
            md.add(box(cx - L, cy - Wd, 1.4, cx + 2.6, cy + Wd, 3.2, m_flat('fence_wood'), grp=gp))
            if load == 'cans':
                for i in range(4):
                    for j in (-1.5, 1.5):
                        md.add(Cone(cx - 7.5 + i * 2.6, cy + j, 3.2, 6.4, 1.0, 0.8, m_flat('ind_milkcan'), cap=True,
                                    grp=gp))
            elif load == 'drums':
                md.add(Cone(cx - 3.2, cy, 3.2, 5.6, 2.6, 2.6, m_flat('ind_cable'), cap=True, grp=gp))
    else:
        for wy in (-6.5, -3.5, 5.0):
            md.add(box(cx - Wd + 0.2, cy + wy - 1.1, 0, cx + Wd - 0.2, cy + wy + 1.1, 2.2, m_flat('tyre'), grp=gp))
        md.add(box(cx - Wd, cy + 3.0, 1.2, cx + Wd, cy + L, 7.5, m_flat(cab), grp=gp))
        md.add(box(cx - Wd + 0.4, cy + 6.6, 4.6, cx + Wd - 0.4, cy + L + 0.05, 6.8, m_flat('glass'), grp=gp))
        if tank:
            md.add(box(cx - Wd + 0.3, cy - L, 1.4, cx + Wd - 0.3, cy + 2.6, 2.2, m_flat('steel'), grp=gp))
            md.add(Ellipsoid(cx, cy - 3.2, 5.0, 2.8, 6.0, 2.8 * KZ * 0.9, m_flat(body), grp=gp))
        else:
            md.add(box(cx - Wd, cy - L, 1.4, cx + Wd, cy + 2.6, 3.2, m_flat('fence_wood'), grp=gp))
            if load == 'cans':
                for i in range(4):
                    for j in (-1.5, 1.5):
                        md.add(Cone(cx + j, cy - 7.5 + i * 2.6, 3.2, 6.4, 1.0, 0.8, m_flat('ind_milkcan'), cap=True,
                                    grp=gp))


def add_tractor(md, cx, cy, heading='x'):
    """small farm tractor (muted orange-red), big rear wheels"""
    gp = new_grp()
    if heading == 'x':
        md.add(box(cx - 2.0, cy - 1.4, 1.4, cx + 4.0, cy + 1.4, 4.4, m_flat('ind_tractor'), grp=gp))
        md.add(box(cx - 2.6, cy - 1.6, 4.0, cx - 0.4, cy + 1.6, 7.6, m_flat('ind_tractor', 0.3), grp=gp))
        for s in (-1, 1):
            md.add(box(cx - 3.2, cy + s * 2.6 - 0.8, 0, cx - 0.2, cy + s * 2.6 + 0.8, 4.0, m_flat('tyre'), grp=gp))
            md.add(box(cx + 2.6, cy + s * 1.9 - 0.5, 0, cx + 4.2, cy + s * 1.9 + 0.5, 2.2, m_flat('tyre'), grp=gp))
    else:
        md.add(box(cx - 1.4, cy - 2.0, 1.4, cx + 1.4, cy + 4.0, 4.4, m_flat('ind_tractor'), grp=gp))
        md.add(box(cx - 1.6, cy - 2.6, 4.0, cx + 1.6, cy - 0.4, 7.6, m_flat('ind_tractor', 0.3), grp=gp))
        for s in (-1, 1):
            md.add(box(cx + s * 2.6 - 0.8, cy - 3.2, 0, cx + s * 2.6 + 0.8, cy - 0.2, 4.0, m_flat('tyre'), grp=gp))
            md.add(box(cx + s * 1.9 - 0.5, cy + 2.6, 0, cx + s * 1.9 + 0.5, cy + 4.2, 2.2, m_flat('tyre'), grp=gp))


def m_roll(g, st):
    """paper-machine roll / cable drum lying along X: steel shell, darker bands"""
    key = g.solid.par.get('key', 'steel_lt')
    if g.tag in ('x+', 'x-'):
        return 'steel', -0.4
    if st.zoom >= 1 and (g.P[0] % 6.0) < 0.8:
        return key, -0.6
    return key, 0.2 if g.tag == 'top' else 0.0


def m_clarifier(g, st):
    """round settling basin: concrete rim, dull water surface with faint concentric ripples"""
    if g.tag == 'cap':
        cx, cy = g.solid.c[0], g.solid.c[1]
        r = math.hypot(g.P[0] - cx, g.P[1] - cy)
        R = g.solid.c[5]
        if r > R - 1.1:
            return 'concrete', 0.3
        if st.zoom >= 1 and (r % 3.0) < 0.5:
            return 'ind_sludge', 0.5
        return 'ind_sludge', 0.0
    return 'concrete_dk' if g.P[2] < 1.0 else 'concrete', 0.0


def m_tank(g, st):
    """vertical tank: light shell, a darker band of rivet seams, cap slightly darker"""
    p = g.solid.par
    key = p.get('key', 'ind_oiltank')
    if g.tag == 'cap':
        return key, -0.3
    if st.zoom >= 1 and (g.P[2] % 5.0) < 0.5:
        return key, -0.5
    return key, 0.0


# =========================================================================== textile T1
def textile_t1(name, seed=1):
    """ind_textile_t1_2x2: sewing workshop / shoe factory: a two-storey red-brick block with rows of big steel
    windows, entrance stair towers on both long sides, a small boiler chimney on the ridge"""
    md = Model(name, 2, 2)
    x0, x1, y0, y1 = 5.0, 59.0, 20.0, 44.0
    pl = 1.5
    ze = pl + 2 * 9.5 + 2.0
    zr = ze + 5.0
    wins = {}
    for f in ('y+', 'y-'):
        wins[f] = []
        for k in range(2):
            t0 = pl + 2.0 + k * 9.5
            wins[f] += [r for r in hall_wins(x0, x1, t0, t0 + 6.5, w=5.0, gap=7.0) if not (27.0 < r[1] and r[0] < 37.0)]
    for f in ('x+', 'x-'):
        wins[f] = []
        for k in range(2):
            t0 = pl + 2.0 + k * 9.5
            wins[f] += [(y0 + 4.0, y0 + 9.0, t0, t0 + 6.5), (y1 - 9.0, y1 - 4.0, t0, t0 + 6.5)]
    md.add(gable_body(x0, y0, x1, y1, 0, ze, zr, 'x', m_brickwall, wall='brick', wins=wins))
    md.add(gable_roof(x0, y0, x1, y1, ze, zr, 'x', m_roof, ov=1.0, ovg=1.0, th=1.2, key='roof_metal',
                      edgekey='brick_dk', seam=3.0))
    # stair / entrance towers on both long sides (rendered, with a fascia sign)
    for (ty0, ty1, face) in ((y1, y1 + 4.0, 'y+'), (y0 - 4.0, y0, 'y-')):
        md.add(box(28.0, ty0, 0, 36.0, ty1, ze + 3.0, m_block, wall='render_white', plinth=1.0, roofkey='bitumen',
                   parapet=0.6, doors={face: [(30.5, 33.5, 1.0, 8.0)]},
                   wins={face: [(31.0, 33.0, 11.0, 16.0)]},
                   bands={face: [(28.5, 35.5, ze - 4.0, ze - 0.5, 'sign_blue', 'text')]}))
    # small boiler chimney at the ridge centre
    md.add(box(31.0, 30.5, zr - 2.0, 33.6, 33.5, zr + 9.0, m_chimney, key='brick'))
    md.hot = (32.3, 32.0, zr + 9.5)
    # yards: bales / crates at mirrored corners, birches at the side corners
    for (bx, by) in ((6.0, 6.0), (50.0, 51.0)):
        md.add(box(bx, by, 0, bx + 8.0, by + 6.0, 3.5, m_flat('ind_bale')))
        md.add(box(bx + 1.0, by + 0.8, 3.5, bx + 6.0, by + 5.2, 6.5, m_flat('ind_bale', 0.3)))
    add_birch(md, 59.0, 8.0, h=26, seed=seed + 300)
    add_birch(md, 5.0, 57.0, h=26, seed=seed + 301)
    gr = [(0.0, y1, 64.0, 64.0, 'asphalt'), (0.0, 0.0, 64.0, y0, 'asphalt'), (28.0, 0.0, 36.0, 64.0, 'gravel')]
    md.ground = lambda g, st: g_lot(g, st, gravel=gr, var=seed)
    return md


# =========================================================================== sawmill T2
def sawmill_t2(name, seed=1):
    """ind_wood_t2_3x3: sawmill + planing mill: a long red-ochre saw/planing hall across the middle with a
    boiler house and brick chimney at its centre, two blocks of drying kilns and two board-stack yards at
    opposite corners, log piles between them"""
    md = Model(name, 3, 3)
    pl = 1.0
    x0, x1, y0, y1 = 8.0, 88.0, 38.0, 58.0
    ze = pl + 12.0
    zr = ze + 10.0 * KZ * 0.7
    wins = {f: hall_wins(x0, x1, pl + 3.5, pl + 8.5, w=4.0, gap=7.0) for f in ('y+', 'y-')}
    wins = {f: [r for r in v if not (38.0 < r[1] and r[0] < 58.0)] for f, v in wins.items()}
    doors = {'y+': [(14.0, 22.0, 0, 10.5), (74.0, 82.0, 0, 10.5)], 'y-': [(14.0, 22.0, 0, 10.5), (74.0, 82.0, 0, 10.5)],
             'x+': [(y0 + 6.0, y1 - 6.0, 1.0, 10.0)], 'x-': [(y0 + 6.0, y1 - 6.0, 1.0, 10.0)]}
    md.add(gable_body(x0, y0, x1, y1, 0, ze, zr, 'x', m_house, wall='w_red', plinth=pl, wins=wins, doors=doors,
                      clad='v', eave=ze))
    md.add(gable_roof(x0, y0, x1, y1, ze, zr, 'x', m_roof, ov=1.4, ovg=1.4, th=1.3, key='roof_dark', edgekey='trim',
                      seam=3.0))
    # boiler house (brick, flat) straddling the hall at the centre + chimney
    md.add(box(40.0, 35.0, 0, 56.0, 61.0, zr + 1.5, m_brickwall, wall='brick',
               wins={'y+': [(43.0, 46.0, 6.0, 12.0), (50.0, 53.0, 6.0, 12.0)],
                     'y-': [(43.0, 46.0, 6.0, 12.0), (50.0, 53.0, 6.0, 12.0)]},
               doors={'y+': [(46.5, 49.5, 0, 8.0, 'steel')], 'y-': [(46.5, 49.5, 0, 8.0, 'steel')]}))
    md.add(Cone(48.0, 48.0, zr + 1.5, 80.0, 3.4, 2.3, m_cone_brick, cap=True))
    md.hot = (48.0, 48.0, 81.0)
    # drying kilns (concrete chambers with steel doors), opposite corners
    for (kx0, ky0, face) in ((54.0, 6.0, 'y+'), (6.0, 68.0, 'y-')):
        for i in range(3):
            a = kx0 + i * 12.0
            md.add(box(a, ky0, 0, a + 11.0, ky0 + 22.0, 9.0, m_brickwall, wall='brick',
                       doors={face: [(a + 2.0, a + 9.0, 0.0, 7.5, 'steel')]}))
            md.add(gable_roof(a, ky0, a + 11.0, ky0 + 22.0, 9.0, 11.5, 'y', m_roof, ov=0.4, ovg=0.4, th=1.0,
                              key='roof_dark', edgekey='brick_dk', seam=3.0))
            vy = ky0 + 11.0
            md.add(box(a + 4.0, vy - 1.5, 10.5, a + 7.0, vy + 1.5, 15.0, m_corr, wall='corr', plinth=-1))
            md.add(box(a + 3.4, vy - 2.1, 15.0, a + 7.6, vy + 2.1, 15.8, m_flat('roof_dark', 0.3)))
    # board stacks (stickered, yellow) with board roofs: the other two corners
    for (sx0, sy0) in ((4.0, 4.0), (60.0, 70.0)):
        for (dx, dy) in ((0.0, 0.0), (17.0, 0.0), (0.0, 12.0), (17.0, 12.0)):
            hh = 6.5 + 3.0 * ihash(sx0 + dx, sy0 + dy, 9)
            md.add(box(sx0 + dx, sy0 + dy, 0, sx0 + dx + 14.5, sy0 + dy + 8.5, hh, m_timber))
            md.add(box(sx0 + dx - 0.4, sy0 + dy - 0.4, hh, sx0 + dx + 14.9, sy0 + dy + 8.9, hh + 0.7,
                       m_flat('boards', 0.2)))
    # log piles along the hall ends (cut ends toward the hall)
    md.add(box(1.0, 36.0, 0, 7.0, 60.0, 4.5, m_logs))
    md.add(box(89.0, 36.0, 0, 95.0, 60.0, 4.5, m_logs))
    md.add(box(40.0, 66.0, 0, 54.0, 76.0, 3.5, m_logs, ends='y'))
    md.add(box(42.0, 20.0, 0, 56.0, 30.0, 3.5, m_logs, ends='y'))
    add_birch(md, 92.0, 4.0, h=26, seed=seed + 310)
    add_birch(md, 4.0, 92.0, h=26, seed=seed + 311)
    gr = [(0.0, 0.0, 96.0, 96.0, 'gravel'), (6.0, 32.0, 90.0, 64.0, 'sawdust')]
    md.ground = lambda g, st: g_lot(g, st, gravel=gr, var=seed)
    return md


# =========================================================================== engineering works T3
def metal_t3(name, seed=1):
    """ind_metal_t3_3x3: large engineering works: two long red-brick halls with lantern roofs, each with a
    rendered three-storey office head at opposite ends, a crane yard between them under a yellow gantry,
    a boiler house + chimney at the centre, tractors and paper-machine rolls in the yard"""
    md = Model(name, 3, 3)
    hw = 15.0
    pl = 1.0
    for (hx0, hx1, hy0, hy1, out_face, ox0, ox1) in ((4.0, 78.0, 4.0, 30.0, 'y-', 78.0, 92.0),
                                                       (18.0, 92.0, 66.0, 92.0, 'y+', 4.0, 18.0)):
        in_face = 'y+' if out_face == 'y-' else 'y-'
        hwins = hall_wins(hx0, hx1, 4.0, 11.5, w=4.0, gap=6.0)
        w = {out_face: hwins, in_face: [r for r in hwins if r[1] < 38.0 or r[0] > 58.0],
             'x+': hall_wins(hy0, hy1, 4.0, 11.5, w=4.0, gap=7.0), 'x-': hall_wins(hy0, hy1, 4.0, 11.5, w=4.0, gap=7.0)}
        d = {in_face: [(40.0, 50.0, 0, 12.0, 'steel')], out_face: [(hx0 + 14.0, hx0 + 22.0, 0, 10.0, 'steel')]}
        lantern_hall(md, hx0, hy0, hx1, hy1, hw, wall='brick', pitch=5.0, wins=w, doors=d)
        # office head (rendered, flat roof, sign band on the yard side)
        oz = pl + 3 * STOREY + 1.0
        ow = {}
        for f, lo, hi, n in (('x+', hy0, hy1, 4), ('x-', hy0, hy1, 4), ('y+', ox0, ox1, 2), ('y-', ox0, ox1, 2)):
            ow[f] = []
            for k in range(3):
                ow[f] += wins_row(lo, hi, pl + k * STOREY + 3.0, pl + k * STOREY + 8.0, 3.2, n)
        md.add(box(ox0, hy0 - 1.0, 0, ox1, hy1 + 1.0, oz, m_block, wall='render_yel', plinth=pl, roofkey='bitumen',
                   frame='trim', wins=ow, doors={in_face: [(ox0 + 5.5, ox0 + 8.5, pl, pl + 7.5)]},
                   bands={in_face: [(ox0 + 1.0, ox1 - 1.0, oz - 3.6, oz - 0.6, 'sign_red', 'text')]}))
    # crane yard: gantry legs + runway beams along X at the yard edges, two bridges with hooks
    for gx in (8.0, 88.0):
        for gy in (34.0, 62.0):
            md.add(box(gx - 0.7, gy - 0.7, 0, gx + 0.7, gy + 0.7, 22.0, m_flat('balc_mustard'), detail=True))
    for gy in (34.0, 62.0):
        md.add(box(7.0, gy - 0.8, 20.5, 89.0, gy + 0.8, 22.5, m_flat('balc_mustard'), detail=True))
    for bx in (26.0, 70.0):
        md.add(box(bx - 1.5, 33.0, 22.5, bx + 1.5, 63.0, 24.5, m_flat('balc_mustard', 0.3)))
        md.add(box(bx - 0.3, 47.6, 12.0, bx + 0.3, 48.4, 22.5, m_flat('steel'), detail=True))
        md.add(box(bx - 1.2, 47.0, 10.5, bx + 1.2, 49.0, 12.0, m_flat('steel_lt'), detail=True))
    # boiler house + chimney at the centre of the yard
    md.add(box(43.0, 43.0, 0, 53.0, 53.0, 10.0, m_brickwall, wall='brick',
               wins={'y+': [(46.5, 49.5, 3.5, 8.0)], 'y-': [(46.5, 49.5, 3.5, 8.0)]},
               doors={'x+': [(46.0, 50.0, 0, 7.0, 'steel')], 'x-': [(46.0, 50.0, 0, 7.0, 'steel')]}))
    md.add(Cone(48.0, 48.0, 10.0, 84.0, 3.4, 2.2, m_cone_brick, cap=True))
    md.hot = (48.0, 48.0, 85.0)
    # products: tractors in a row, paper-machine rolls on trestles (mirrored about the centre)
    for (tx, ty) in ((10.0, 40.0), (16.0, 40.0), (22.0, 40.0), (86.0, 56.0), (80.0, 56.0), (74.0, 56.0)):
        add_tractor(md, tx, ty, 'y')
    for (rx0, ry) in ((58.0, 40.0), (18.0, 56.0)):
        for xx in (rx0, rx0 + 18.0):
            md.add(box(xx, ry - 2.0, 0.0, xx + 1.0, ry + 2.0, 1.4, m_flat('fence_wood')))
        md.add(box(rx0 - 1.0, ry - 2.2, 1.4, rx0 + 20.0, ry + 2.2, 5.6, m_roll, key='steel_lt'))
    gr = [(0.0, 30.0, 96.0, 66.0, 'concrete'), (0.0, 0.0, 96.0, 4.0, 'asphalt'), (0.0, 92.0, 96.0, 96.0, 'asphalt')]
    md.ground = lambda g, st: g_lot(g, st, gravel=gr, var=seed)
    return md


# =========================================================================== dairies
def dairy_t1(name, seed=1):
    """ind_dairy_t1_2x2: co-op dairy (osuusmeijeri): white-rendered two-storey process block with a hipped tin
    roof, one-storey wings with milk-reception platforms on both long sides (cans), brick chimney, milk lorry"""
    md = Model(name, 2, 2)
    pl = 1.5
    # main block (2 storeys) in the middle
    x0, x1, y0, y1 = 16.0, 48.0, 20.0, 44.0
    ze = pl + 2 * STOREY + 1.0
    wn = wins_row(x0, x1, pl + 3.0, pl + 8.0, 4.0, 5) + wins_row(x0, x1, pl + STOREY + 3.0, pl + STOREY + 8.0, 4.0, 5)
    wx = wins_row(y0, y1, pl + STOREY + 3.0, pl + STOREY + 8.0, 4.0, 3)
    md.add(hip_body(x0, y0, x1, y1, 0, ze, ze + 7.0, m_hiproof, wallmat=m_block, wall='render_white', plinth=pl,
                    wins={'y+': wn, 'y-': wn, 'x+': wx, 'x-': wx}, roofkey='roof_metal', inset=8.0,
                    bands={'y+': [(x0 + 4.0, x1 - 4.0, pl + 8.8, pl + STOREY + 1.6, 'sign_blue', 'text')],
                           'y-': [(x0 + 4.0, x1 - 4.0, pl + 8.8, pl + STOREY + 1.6, 'sign_blue', 'text')]}))
    # one-storey wings at both ends (boiler room / butter room), flat roofs
    for (a, b) in ((4.0, 16.0), (48.0, 60.0)):
        md.add(box(a, 24.0, 0, b, 40.0, pl + 9.0, m_block, wall='render_white', plinth=pl, roofkey='bitumen',
                   wins={'y+': [(a + 3.0, b - 3.0, pl + 3.0, pl + 7.0)], 'y-': [(a + 3.0, b - 3.0, pl + 3.0, pl + 7.0)],
                         'x+' if a > 30 else 'x-': [(27.0, 31.0, pl + 3.0, pl + 7.0), (33.0, 37.0, pl + 3.0, pl + 7.0)]}))
    # milk reception platforms (concrete, with cans) on both long sides
    for (py0, py1, cy) in ((44.0, 49.0, 46.5), (15.0, 20.0, 17.5)):
        md.add(box(20.0, py0, 0, 44.0, py1, 3.2, m_concrete, key='concrete'))
        md.add(box(20.0, py0 - 0.2 if py0 > 30 else py0, 7.5, 44.0, py1 + 0.2, 8.2, m_flat('panel_dk', 0.6)))
        for i in range(6):
            cx = 22.0 + i * 1.9 + (0.0 if py0 > 30 else 10.0)
            md.add(Cone(cx, cy, 3.2, 6.6, 0.85, 0.7, m_flat('ind_milkcan'), cap=True, detail=True))
    # chimney at the centre
    md.add(box(30.6, 30.6, ze + 3.0, 33.4, 33.4, ze + 22.0, m_chimney, key='brick'))
    md.hot = (32.0, 32.0, ze + 22.5)
    add_truck(md, 52.0, 55.0, 'x', cab='sign_blue', load='cans')
    add_truck(md, 12.0, 9.0, 'x', cab='sign_blue', load='cans')
    add_birch(md, 59.0, 6.0, h=26, seed=seed + 320)
    add_birch(md, 5.0, 58.0, h=26, seed=seed + 321)
    gr = [(0.0, 49.0, 64.0, 64.0, 'gravel'), (0.0, 0.0, 64.0, 15.0, 'gravel'), (2.0, 15.0, 62.0, 49.0, 'gravel')]
    md.ground = lambda g, st: g_lot(g, st, gravel=gr, var=seed)
    return md


def dairy_t2(name, seed=1):
    """ind_dairy_t2_2x2: central dairy: a long white three-storey process hall with window bands, a row of
    stainless milk silos at the middle, a boiler chimney, tanker lorries at both loading sides"""
    md = Model(name, 2, 2)
    pl = 1.0
    ze = pl + 3 * STOREY
    # two process blocks at the ends, silos between
    for (a, b) in ((3.0, 23.0), (41.0, 61.0)):
        w = []
        for k in range(3):
            t0 = pl + k * STOREY + 3.0
            w.append((a + 1.5, b - 1.5, t0, t0 + 4.0))
        wx = []
        for k in range(3):
            t0 = pl + k * STOREY + 3.0
            wx.append((16.0, 48.0, t0, t0 + 4.0))
        md.add(box(a, 14.0, 0, b, 50.0, ze, m_block, wall='render_white', plinth=pl, roofkey='bitumen',
                   wkind='band', frame='panel_dk', wins={'y+': w, 'y-': w, 'x+': wx, 'x-': wx},
                   doors={'y+': [(a + 7.0, a + 13.0, 0, 7.5, 'steel')], 'y-': [(a + 7.0, a + 13.0, 0, 7.5, 'steel')]}))
    # low link + silos
    md.add(box(23.0, 22.0, 0, 41.0, 42.0, pl + 9.0, m_block, wall='render_white', plinth=pl, roofkey='bitumen',
               wins={'y+': [(25.0, 39.0, 3.5, 7.0)], 'y-': [(25.0, 39.0, 3.5, 7.0)]}))
    for (sx, sy) in ((27.5, 27.0), (36.5, 27.0), (27.5, 37.0), (36.5, 37.0)):
        md.add(Cone(sx, sy, pl + 9.0, ze + 4.0, 3.8, 3.8, m_tank, cap=True, key='ind_stainless'))
        md.add(Cone(sx, sy, ze + 4.0, ze + 6.0, 3.8, 1.0, m_tank, cap=True, key='ind_stainless'))
    md.add(box(25.0, 31.6, ze + 3.0, 39.0, 32.4, ze + 4.2, m_flat('steel_lt'), detail=True))   # catwalk
    # boiler chimney (steel) on the x- block
    md.add(Cone(9.0, 32.0, ze, ze + 26.0, 1.9, 1.6, m_concrete_round, cap=True, key='steel',
                band=(ze + 22.0, ze + 26.0, 'cap')))
    md.hot = (9.0, 32.0, ze + 27.0)
    md.add(box(52.0, 30.0, ze, 56.0, 34.0, ze + 3.0, m_flat('steel')))
    add_truck(md, 46.0, 57.0, 'x', body='ind_stainless', cab='sign_blue', tank=True)
    add_truck(md, 18.0, 7.0, 'x', body='ind_stainless', cab='sign_blue', tank=True)
    gr = [(0.0, 50.0, 64.0, 64.0, 'asphalt'), (0.0, 0.0, 64.0, 14.0, 'asphalt')]
    md.ground = lambda g, st: g_lot(g, st, gravel=gr, var=seed)
    return md


# =========================================================================== electronics
def elec_t2(name, seed=1):
    """ind_elec_t2_2x2: 1965+ cable and electronics works: a three-storey concrete block with continuous window
    bands and a glazed stair core, a low production hall wing, cable drums in the yard"""
    md = Model(name, 2, 2)
    pl = 1.0
    x0, x1, y0, y1 = 4.0, 60.0, 22.0, 42.0
    ze = pl + 3 * STOREY + 1.0
    wb = []
    for k in range(3):
        t0 = pl + k * STOREY + 3.0
        wb.append((x0 + 1.5, 28.0, t0, t0 + 4.5))
        wb.append((36.0, x1 - 1.5, t0, t0 + 4.5))
    wx = [(y0 + 1.5, y1 - 1.5, pl + k * STOREY + 3.0, pl + k * STOREY + 7.5) for k in range(3)]
    sp = {}
    for f, lo, hi in (('y+', x0, x1), ('y-', x0, x1), ('x+', y0, y1), ('x-', y0, y1)):
        sp[f] = [(lo, hi, pl + k * STOREY + 7.5, pl + k * STOREY + 10.5 + (0.6 if k == 2 else 0.0), 'balc_blue')
                 for k in range(3)]
    md.add(box(x0, y0, 0, x1, y1, ze, m_block, wall='panel', plinth=pl, roofkey='bitumen', wkind='band',
               frame='panel_dk', wins={'y+': wb, 'y-': wb, 'x+': wx, 'x-': wx}, bands=sp))
    # glazed stair core through the middle (both sides)
    md.add(box(28.5, y0 - 2.0, 0, 35.5, y1 + 2.0, ze + 4.0, m_block, wall='render_white', plinth=pl,
               roofkey='bitumen', parapet=0.5,
               wins={'y+': [(29.5, 34.5, 2.0, ze + 2.0)], 'y-': [(29.5, 34.5, 2.0, ze + 2.0)]},
               bands={'x+': [(y0 + 2.0, y1 - 2.0, ze - 3.5, ze + 2.5, 'sign_red', 'text')],
                      'x-': [(y0 + 2.0, y1 - 2.0, ze - 3.5, ze + 2.5, 'sign_red', 'text')]}))
    for (ax, ay) in ((8.0, 25.5), (51.0, 25.5)):
        md.add(box(ax, ay, ze, ax + 5.0, ay + 13.0, ze + 2.5, m_flat('steel')))
    # cable drums (wooden reels standing on edge) in both yards
    for (dx, dy) in ((8.0, 50.0), (15.0, 52.0), (49.0, 12.0), (42.0, 10.0)):
        md.add(Cone(dx, dy, 0, 2.2, 3.4, 3.4, m_flat('ind_cable'), cap=True))
        md.add(Cone(dx, dy, 2.2, 3.6, 1.6, 1.6, m_flat('steel'), cap=True))
    add_truck(md, 47.0, 54.0, 'x', cab='sign_red', load='drums')
    add_truck(md, 17.0, 8.0, 'x', cab='sign_red', load='drums')
    add_birch(md, 59.0, 6.0, h=24, seed=seed + 330)
    add_birch(md, 4.0, 58.0, h=24, seed=seed + 331)
    gr = [(0.0, 44.0, 64.0, 64.0, 'asphalt'), (0.0, 0.0, 64.0, 20.0, 'asphalt')]
    md.ground = lambda g, st: g_lot(g, st, gravel=gr, var=seed)
    return md


# =========================================================================== district heating plant
def heat_plant(name, seed=1):
    """heat_plant (2x2): district heating plant (lampolaitos): a tall brick boiler house with steel-framed
    windows, a lower pump hall around it, one tall brick chimney, two oil tanks and a fuel heap at opposite
    corners.  Also the paper mill's boiler piece (pm_boiler = alias of v0 r0)."""
    md = Model(name, 2, 2)
    # boiler house (tall) in the middle
    bx0, bx1, by0, by1 = 18.0, 46.0, 18.0, 46.0
    bh = 34.0
    w = {f: [(22.0, 26.0, 18.0, 30.0), (30.0, 34.0, 18.0, 30.0), (38.0, 42.0, 18.0, 30.0)] for f in ('y+', 'y-', 'x+', 'x-')}
    md.add(box(bx0, by0, 0, bx1, by1, bh, m_brickwall, wall='brick', wins=w))
    # pump / turbine hall wings along both long sides
    for (wy0, wy1, face) in ((46.0, 54.0, 'y+'), (10.0, 18.0, 'y-')):
        md.add(box(12.0, wy0, 0, 52.0, wy1, 13.0, m_brickwall, wall='brick',
                   wins={face: hall_wins(12.0, 52.0, 3.5, 10.0, w=4.0, gap=7.0)[:2] +
                         hall_wins(12.0, 52.0, 3.5, 10.0, w=4.0, gap=7.0)[-2:]},
                   doors={face: [(29.0, 35.0, 0.0, 9.0, 'steel')]}))
    # chimney at the centre, rising from the boiler house roof
    md.add(Cone(32.0, 32.0, bh, 108.0, 3.6, 2.4, m_cone_brick, cap=True))
    md.hot = (32.0, 32.0, 109.0)
    # roof plant
    md.add(box(21.0, 21.0, bh, 26.0, 26.0, bh + 3.0, m_flat('steel')))
    md.add(box(38.0, 38.0, bh, 43.0, 43.0, bh + 3.0, m_flat('steel')))
    # oil tanks (two corners) and a fuel heap (the other two)
    for (tx, ty) in ((54.0, 10.0), (10.0, 54.0)):
        md.add(Cone(tx, ty, 0, 10.0, 6.5, 6.5, m_tank, cap=True, key='ind_oiltank'))
        md.add(Cone(tx, ty, 10.0, 11.5, 6.5, 2.0, m_tank, cap=True, key='ind_oiltank'))
    for (hx, hy) in ((8.0, 8.0), (56.0, 56.0)):
        md.add(HeightField(hx - 7.0, hy - 7.0, hx + 7.0, hy + 7.0, 6.0,
                           (lambda cx, cy: lambda x, y: 6.0 * max(0.0, 1.0 - ((x - cx) / 6.5) ** 2 -
                                                                   ((y - cy) / 6.5) ** 2) ** 0.8)(hx, hy),
                           m_flat('coal')))
    gr = [(0.0, 0.0, 64.0, 64.0, 'gravel'), (1.0, 1.0, 15.0, 15.0, 'dirt'), (49.0, 49.0, 63.0, 63.0, 'dirt')]
    md.ground = lambda g, st: g_lot(g, st, gravel=gr, var=seed)
    return md


# =========================================================================== sewage works
def sewage_works(name, seed=1):
    """sewage_works (2x2): four round clarifier basins with scraper bridges around a small rendered blower
    house; gravel paths, steel fence (90-degree symmetric)"""
    md = Model(name, 2, 2)
    R = 10.5
    for (cx, cy) in ((15.0, 15.0), (49.0, 15.0), (15.0, 49.0), (49.0, 49.0)):
        md.add(Cone(cx, cy, 0, 2.8, R, R, m_clarifier, cap=True))
        # scraper bridge: radius beam from the centre to the rim, centre post
        md.add(Cone(cx, cy, 2.8, 4.6, 1.0, 1.0, m_flat('steel'), cap=True, detail=True))
        dx = 1 if cx > 32 else -1
        md.add(box(min(cx, cx + dx * (R - 0.5)), cy - 0.7, 3.6, max(cx, cx + dx * (R - 0.5)), cy + 0.7, 4.6,
                   m_flat('steel_lt'), detail=True))
    # blower / pump house at the centre
    md.add(box(26.0, 26.0, 0, 38.0, 38.0, 8.5, m_block, wall='render_white', plinth=1.0, roofkey='bitumen',
               wins={f: [(28.0, 31.0, 3.5, 6.5), (33.0, 36.0, 3.5, 6.5)] for f in ('x+', 'x-', 'y+', 'y-')},
               doors={'y+': [(30.5, 33.5, 1.0, 7.0, 'steel')]}))
    md.add(box(30.0, 30.0, 8.5, 34.0, 34.0, 10.5, m_flat('steel')))
    # fence along the edges (with a gate gap at every edge midpoint)
    for (ax, ay, bx, by) in ((1.0, 1.0, 63.0, 1.0), (63.0, 1.0, 63.0, 63.0), (1.0, 63.0, 63.0, 63.0), (1.0, 1.0, 1.0, 63.0)):
        add_fence(md, ax, ay, bx, by, key='steel_lt', h=3.0, step=1.8, gaps=((28.0, 34.0),))
    gr = [(0.0, 0.0, 64.0, 64.0, 'grass'), (29.0, 0.0, 35.0, 64.0, 'gravel'), (0.0, 29.0, 64.0, 35.0, 'gravel'),
          (25.0, 25.0, 39.0, 39.0, 'gravel')]
    md.ground = lambda g, st: g_lot(g, st, gravel=[r for r in gr if r[4] != 'grass'], var=seed)
    return md


# =========================================================================== yard variants
def yard_bales(name, seed=5):
    """ind_yard v4: bale warehouse: a long red-brick storehouse with a loading door, cotton bales in sacking
    stacked on pallets in front, a hand trolley"""
    md = Model(name)
    x0, y0, x1, y1 = 3.0, 3.0, 29.0, 14.0
    ze, zr = 8.5, 8.5 + 5.5 * KZ * 0.8
    md.add(gable_body(x0, y0, x1, y1, 0, ze, zr, 'x', m_brickwall, wall='brick',
                      wins={'y-': [(8.0, 11.0, 3.5, 6.5), (20.0, 23.0, 3.5, 6.5)], 'x+': [(6.5, 9.5, 3.5, 6.5)],
                            'x-': [(6.5, 9.5, 3.5, 6.5)], 'y+': [(22.0, 25.0, 3.5, 6.5)]},
                      doors={'y+': [(7.0, 14.0, 0.0, 7.0, 'door')]}))
    md.add(gable_roof(x0, y0, x1, y1, ze, zr, 'x', m_roof, ov=0.8, ovg=0.8, th=1.1, key='roof_metal',
                      edgekey='brick_dk', seam=3.0))
    for (bx, by, nz) in ((4.0, 18.0, 2), (12.0, 18.0, 1), (4.0, 24.0, 1), (20.0, 19.0, 2)):
        md.add(box(bx, by, 0, bx + 6.5, by + 4.5, 0.8, m_flat('fence_wood')))
        for k in range(nz):
            md.add(box(bx + 0.3, by + 0.3, 0.8 + k * 3.0, bx + 6.2, by + 4.2, 3.6 + k * 3.0,
                       m_flat('ind_bale', 0.2 if k else 0.0)))
    md.add(box(15.0, 25.0, 0, 17.0, 28.0, 2.2, m_flat('steel')))
    gr = [(0.0, 15.0, 32.0, 32.0, 'gravel'), (1.0, 1.0, 31.0, 15.0, 'gravel')]
    md.ground = lambda g, st: g_lot(g, st, gravel=gr, var=seed)
    return md


def yard_milk(name, seed=6):
    """ind_yard v5: milk-can shed: an open board shed on posts with a tin roof, a raised can platform with rows
    of steel milk cans, crates of empty bottles"""
    md = Model(name)
    x0, y0, x1, y1 = 4.0, 4.0, 22.0, 14.0
    md.add(box(x0, y0, 0, x1, y0 + 0.8, 8.0, m_house, wall='w_weath', plinth=0.3, clad='v', frieze=False,
               corners=False))
    md.add(box(x0, y0, 0, x0 + 0.8, y1, 7.0, m_house, wall='w_weath', plinth=0.3, clad='v', frieze=False,
               corners=False))
    for px in (x1 - 0.6,):
        md.add(box(px - 0.4, y1 - 0.8, 0, px + 0.4, y1, 7.0, m_flat('fence_wood'), detail=True))
    md.add(mono_body(x0 - 0.6, y0 - 0.4, x1 + 0.6, y1 + 0.8, 7.0, 7.6, 9.2, m_corr, wall='corr', roofkey='roof_metal',
                     rise='y-'))
    # can platform in front (concrete) with cans
    md.add(box(5.0, 17.0, 0, 27.0, 23.0, 3.0, m_concrete, key='concrete'))
    for i in range(8):
        for j in range(2):
            md.add(Cone(6.6 + i * 2.6, 18.6 + j * 2.8, 3.0, 6.4, 0.95, 0.75, m_flat('ind_milkcan'), cap=True, detail=True))
    for i in range(3):                                                # cans under the shed
        md.add(Cone(8.0 + i * 3.0, 9.0, 0, 3.4, 0.95, 0.75, m_flat('ind_milkcan'), cap=True, detail=True))
    md.add(box(24.0, 6.0, 0, 28.0, 10.0, 2.5, m_flat('woodpile')))
    md.add(box(24.5, 6.5, 2.5, 27.5, 9.5, 4.6, m_flat('woodpile', 0.3)))
    gr = [(0.0, 15.0, 32.0, 32.0, 'gravel'), (2.0, 2.0, 30.0, 15.0, 'gravel')]
    md.ground = lambda g, st: g_lot(g, st, gravel=gr, var=seed)
    return md


# =========================================================================== table
ENTRIES = [
    ('ind_textile_t1_2x2', (2, 2), 2, [_v(textile_t1, 'tx1')], None),
    ('ind_wood_t2_3x3', (3, 3), 2, [_v(sawmill_t2, 'sw2')], None),
    ('ind_metal_t3_3x3', (3, 3), 2, [_v(metal_t3, 'mt3')], None),
    ('ind_dairy_t1_2x2', (2, 2), 2, [_v(dairy_t1, 'dy1')], None),
    ('ind_dairy_t2_2x2', (2, 2), 2, [_v(dairy_t2, 'dy2')], None),
    ('ind_elec_t2_2x2', (2, 2), 2, [_v(elec_t2, 'el2')], None),
    ('heat_plant', (2, 2), 2, [_v(heat_plant, 'hp')], None),
    ('sewage_works', (2, 2), 1, [_v(sewage_works, 'sw')], None),
    ('ind_yard', (1, 1), 4, [_v(yard_bales, 'y4'), _v(yard_milk, 'y5')], 'v0'),
]
KEY_ALIASES = [
    ('ind_wood_t1_3x3', [('ind_t1_3x3', 0), ('ind_t1_3x3', 1)]),
    ('ind_wood_t1_2x2', [('ind_t1_2x2', 0)]),
    ('ind_metal_t1_2x2', [('ind_t1_2x2', 1)]),
    ('ind_metal_t2_2x2', [('ind_t2_2x2', 0), ('ind_t2_2x2', 1)]),
    ('ind_textile_t2_3x3', [('ind_t2_3x3', 0), ('ind_t2_3x3', 1)]),
    ('ind_t3_3x3', [('ind_metal_t3_3x3', 0)]),
    ('ind_t3_2x2', [('ind_elec_t2_2x2', 0)]),
]
GROUPS = {
    'ind_textile_t1_2x2': '2 industry kinds', 'ind_wood_t2_3x3': '2 industry kinds',
    'ind_metal_t3_3x3': '2 industry kinds', 'ind_dairy_t1_2x2': '2 industry kinds',
    'ind_dairy_t2_2x2': '2 industry kinds', 'ind_elec_t2_2x2': '2 industry kinds',
    'ind_yard_v4': '2 industry kinds', 'ind_yard_v5': '2 industry kinds',
    'heat_plant': '3 services (utilities)', 'sewage_works': '3 services (utilities)',
}
