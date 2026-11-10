"""art_power.py - Project Lahio 1.2 "Power" world art (task A1, DESIGN_1_2 section 5): the coal power station
(3x3), the peat power plant (2x2), the nuclear power plant (3x3, Loviisa-like) and the oil power plant (2x2, its own
art instead of the old coal-plant alias).  Loaded by gen_art.py like the art_m2_*.py modules (ENTRIES, GROUPS,
HOT2); iterate with `python3 art/power_preview.py` (sheets + scale scenes, summer and winter, both zooms).

Rotation policy (ART.md 3): every model is built point-symmetric about the lot centre (two mirrored units, yards
and sheds at opposite corners, the single chimney of the 2x2 plants at the centre), so `rots 2` (r0 + r1 rendered,
r2/r3 aliases) gives every rotation the code can ask for at half the VRAM; no def needs BDF_NO_ROTATE.

Smoke: md.hot = the first chimney top (header hx, hy as for every smoke building); the coal station's second
chimney is md.hot2 and becomes the zero-cost header alias `<sprite>_hot2` (same pieces, hotspot = second chimney;
see HOT2 below and ART.md 3.2).  The nuclear plant has no hotspot (no smoke)."""
from art_m2_common import *  # noqa: F401,F403
from art_m2_common import _v, add_colours  # noqa: F401

import artlib
from art_m2_ind import m_tank  # noqa: E402

add_colours({
    'peat': '#74503a',          # milled peat: rust-brown (THEME: muted earth)
    'peat_dk': '#5a3f30',
    'clad_ochre': '#a28a60',    # 1970s ribbed steel cladding (peat plant trim)
    'clad_green': '#8a9483',    # 1970s ribbed steel cladding (peat plant)
    'clad_grey': '#8f9696',     # blue-grey ribbed cladding (coal station boiler houses)
    'nuke_white': '#d3d3cc',    # pale grey concrete of the reactor halls
    'nuke_grey': '#b3b4ae',
    'oil_white': '#cfcbbf',     # oil plant render
})
# milled peat stays brown in winter where the camera sees it (it lies under shed roofs); coal already does
if 'peat' not in artlib.NOSNOW_SOLID:
    artlib.NOSNOW_SOLID = artlib.NOSNOW_SOLID + ('peat', 'peat_dk')


# =========================================================================== helpers
def incline(x0, x1, y0, y1, z0, z1, th, axis, mat, **par):
    """inclined slab (conveyor gallery) rising along `axis` from height z0 at the low coordinate (x0 or y0) to z1
    at the high one; th = thickness in px"""
    if axis == 'x':
        s = (z1 - z0) / float(x1 - x0)
        pl = [(-1, 0, 0, -x0, 'x-'), (1, 0, 0, x1, 'x+'), (0, -1, 0, -y0, 'y-'), (0, 1, 0, y1, 'y+'),
              (-s, 0, 1, z0 + th - s * x0, 'top'), (s, 0, -1, -(z0 - s * x0), 'bot')]
    else:
        s = (z1 - z0) / float(y1 - y0)
        pl = [(-1, 0, 0, -x0, 'x-'), (1, 0, 0, x1, 'x+'), (0, -1, 0, -y0, 'y-'), (0, 1, 0, y1, 'y+'),
              (0, -s, 1, z0 + th - s * y0, 'top'), (0, s, -1, -(z0 - s * y0), 'bot')]
    return Convex(pl, (x0, y0, min(z0, z1), x1, y1, max(z0, z1) + th), mat, **par)


def m_gallery(g, st):
    """enclosed conveyor gallery: corrugated sides with a dark window strip, darker roof"""
    key = g.solid.par.get('wall', 'corr')
    if g.tag == 'top':
        return 'roof_dark', 0.0
    if g.tag == 'bot':
        return key, -1.2
    if g.tag in ('y+', 'y-', 'x+', 'x-') and st.zoom >= 1:
        if (g.P[0] + g.P[1]) % 2.0 < 0.8:
            return key, -0.5
    return key, 0.0


def add_conveyor(md, x0, x1, y0, y1, z0, z1, axis, legs=2, wall='corr'):
    """enclosed conveyor bridge (gallery 4 px high) on steel trestles, rising along `axis`"""
    md.add(incline(x0, x1, y0, y1, z0, z1, 4.0, axis, m_gallery, wall=wall))
    for k in range(1, legs + 1):
        f = k / float(legs + 1)
        if axis == 'x':
            lx = x0 + (x1 - x0) * f
            zt = z0 + (z1 - z0) * f
            md.add(box(lx - 0.6, y0 + 0.2, 0, lx + 0.6, y1 - 0.2, zt, m_flat('steel'), detail=True))
        else:
            ly = y0 + (y1 - y0) * f
            zt = z0 + (z1 - z0) * f
            md.add(box(x0 + 0.2, ly - 0.6, 0, x1 - 0.2, ly + 0.6, zt, m_flat('steel'), detail=True))


def heap(cx, cy, rx, ry, h, key='coal'):
    """stockpile: a rounded ridge (superellipse footprint, flat-ish crest) - coal or peat"""
    def f(x, y):
        u = ((x - cx) / rx) ** 2 + ((y - cy) / ry) ** 2
        return h * max(0.0, 1.0 - u) ** 0.65
    return HeightField(cx - rx, cy - ry, cx + rx, cy + ry, h, f, m_flat(key))


def m_heap(key):
    def f(g, st):
        if st.zoom >= 1 and st.texture > 0.3:
            P = g.P
            n = ihash(int(P[0] * 1.3), int(P[1] * 1.3), 31)
            if n < 0.12:
                return key, 0.5
            if n > 0.9:
                return key, -0.5
        return key, 0.0
    return f


def stockpile(cx, cy, rx, ry, h, key='coal'):
    md_f = heap(cx, cy, rx, ry, h, key)
    md_f.mat = m_heap(key)
    return md_f


def add_trafo(md, x, y, axis='x'):
    """transformer: a dark steel tank with radiator fins and three insulators"""
    if axis == 'x':
        md.add(box(x, y, 0, x + 5.0, y + 3.4, 5.5, m_flat('steel')))
        md.add(box(x + 0.6, y - 0.6, 0.8, x + 4.4, y, 4.6, m_flat('steel', -0.5), detail=True))
        for i in range(3):
            md.add(box(x + 1.0 + i * 1.4, y + 1.4, 5.5, x + 1.5 + i * 1.4, y + 2.0, 8.5, m_flat('trim'), detail=True))
    else:
        md.add(box(x, y, 0, x + 3.4, y + 5.0, 5.5, m_flat('steel')))
        md.add(box(x - 0.6, y + 0.6, 0.8, x, y + 4.4, 4.6, m_flat('steel', -0.5), detail=True))
        for i in range(3):
            md.add(box(x + 1.4, y + 1.0 + i * 1.4, 5.5, x + 2.0, y + 1.5 + i * 1.4, 8.5, m_flat('trim'), detail=True))


def add_portal(md, x0, y0, x1, y1, h=15.0):
    """switchyard gantry: two steel masts and a cross beam (axis = the longer side)"""
    md.add(box(x0 - 0.4, y0 - 0.4, 0, x0 + 0.4, y0 + 0.4, h, m_flat('steel_lt'), detail=True))
    md.add(box(x1 - 0.4, y1 - 0.4, 0, x1 + 0.4, y1 + 0.4, h, m_flat('steel_lt'), detail=True))
    md.add(box(min(x0, x1) - 0.4, min(y0, y1) - 0.4, h - 1.0, max(x0, x1) + 0.4, max(y0, y1) + 0.4, h - 0.2,
               m_flat('steel_lt'), detail=True))


def strip_wins(lo, hi, t0, t1, w=2.0, gap=6.0):
    """tall narrow window strips (boiler house facades)"""
    out = []
    a = lo + gap * 0.6
    while a + w <= hi - gap * 0.4:
        out.append((a, a + w, t0, t1))
        a += gap
    return out


def g_coalyard(gravel, yards, seed=1):
    """lot ground: gravel/asphalt rects + coal yards (dark ground blackened by coal dust; winter: grey packed
    snow, dust still showing in patches)"""
    def f(g, st):
        P = g.P
        for (x0, y0, x1, y1) in yards:
            if x0 <= P[0] < x1 and y0 <= P[1] < y1:
                n = vnoise(g.W[0], g.W[1], 3.0, 431 + seed)
                if getattr(st, 'winter', False):
                    if n > 0.62:
                        return 'coal', 0.6
                    return 'packed', -0.9 + (n - 0.5) * 0.8
                if n > 0.55:
                    return 'coal', 0.5 + (n - 0.5) * 0.6
                return 'dirt_dk', -0.3 + (n - 0.5) * 0.6
        return g_lot(g, st, gravel=gravel, var=seed)
    return f


# =========================================================================== coal power station 3x3
CS_CHIMS = ((28.0, 40.0), (68.0, 56.0))     # chimney A (md.hot), chimney B (HOT2)
CS_CHIM_H = 150.0


def mirror_box(md, x0, y0, z0, x1, y1, z1, mat, S=96.0, **par):
    """a prop box and its point-mirrored copy (lot centre S/2); only for materials that ignore the face tags'
    wall coordinates (m_flat, m_corr without windows)"""
    md.add(box(x0, y0, z0, x1, y1, z1, mat, **par))
    md.add(box(S - x1, S - y1, z0, S - x0, S - y0, z1, mat, **par))


def coal_station(name, seed=1):
    """coal_station (3x3): a two-unit coal power station (1960s Finland): a long red-brick turbine hall along Y
    through the middle, two tall boiler houses in ribbed grey cladding beside it (left and right at r0, mirrored
    units), two tall concrete chimneys, coal yards with dark stockpiles at the back and front corners, each feeding
    its boiler house through an enclosed conveyor bridge, rendered office blocks at both hall ends, a transformer
    yard per unit.  Point-symmetric about the lot centre (rots 2)."""
    md = Model(name, 3, 3)
    # turbine hall: brick, tall windows, low-pitch roof along Y
    hx0, hx1, hy0, hy1 = 39.0, 57.0, 20.0, 76.0
    ze, zr = 30.0, 34.0
    tw = {f: strip_wins(hy0, hy1, 4.0, 25.0, w=3.0, gap=6.0) for f in ('x+', 'x-')}
    tw['y+'] = tw['y-'] = strip_wins(hx0, hx1, 4.0, 25.0, w=3.0, gap=6.0)
    md.add(gable_body(hx0, hy0, hx1, hy1, 0, ze, zr, 'y', m_brickwall, wall='brick', wins=tw))
    md.add(gable_roof(hx0, hy0, hx1, hy1, ze, zr, 'y', m_roof, ov=0.8, ovg=0.8, th=1.2, key='roof_dark',
                      edgekey='brick_dk', seam=4.0))
    # two boiler houses (tall, ribbed grey cladding over a concrete base): A front-left, B back-right
    bz = 58.0
    for (bx0, by0, bx1, by1, f_out, f_door, pen) in ((13.0, 48.0, 39.0, 76.0, 'x-', 'y+', (14.0, 50.0)),
                                                       (57.0, 20.0, 83.0, 48.0, 'x+', 'y-', (74.0, 38.0))):
        wins = {f: strip_wins(bx0, bx1, 18.0, bz - 7.0, w=4.0, gap=8.5) for f in ('y+', 'y-')}
        wins['x+'] = wins['x-'] = strip_wins(by0, by1, 18.0, bz - 7.0, w=4.0, gap=8.5)
        md.add(box(bx0, by0, 0, bx1, by1, 14.0, m_block, wall='concrete', plinth=1.0, roofkey='bitumen',
                   wins={f_out: wins_row(by0, by1, 4.0, 10.0, 4.0, 3)},
                   doors={f_door: [((bx0 + bx1) / 2.0 - 3.0, (bx0 + bx1) / 2.0 + 3.0, 0, 11.0, 'steel')]}))
        md.add(box(bx0 + 0.4, by0 + 0.4, 14.0, bx1 - 0.4, by1 - 0.4, bz - 4.0, m_corr, wall='clad_grey', plinth=-1,
                   roofkey='bitumen', frame='steel', wins=wins))
        md.add(box(bx0, by0, bz - 4.0, bx1, by1, bz, m_block, wall='concrete', plinth=-1, roofkey='bitumen',
                   parapet=1.0))
        # roof penthouse at the conveyor head
        md.add(box(pen[0], pen[1], bz, pen[0] + 8.0, pen[1] + 8.0, bz + 6.0, m_corr, wall='corr', plinth=-1,
                   roofkey='roof_dark'))
    # chimneys (concrete, soot band at the top) + flue ducts from the boiler houses
    ch = CS_CHIM_H
    chims = CS_CHIMS
    for (cx, cy) in chims:
        md.add(Cone(cx, cy, 0, ch, 4.4, 2.7, m_concrete_round, cap=True, key='concrete', capkey='cap',
                    band=(ch - 6.0, ch, 'cap')))
        md.add(Cone(cx, cy, 0, 6.0, 5.6, 5.4, m_concrete_round, cap=True, key='concrete_dk'))
    mirror_box(md, 26.5, 43.5, 30.0, 29.5, 48.5, 37.0, m_flat('steel'))          # flue ducts boiler -> chimney
    md.hot = (chims[0][0], chims[0][1], ch + 1.0)
    # coal yards (back and front corners) with stockpiles, conveyor bridges up to the boiler penthouses
    md.add(stockpile(10.0, 11.0, 9.0, 9.0, 12.0))
    md.add(stockpile(30.0, 12.0, 7.5, 8.0, 9.0))
    md.add(stockpile(86.0, 85.0, 9.0, 9.0, 12.0))
    md.add(stockpile(66.0, 84.0, 7.5, 8.0, 9.0))
    add_conveyor(md, 16.5, 19.5, 22.0, 50.0, 4.0, bz + 2.0, 'y', legs=2)      # yard A -> boiler A
    add_conveyor(md, 76.5, 79.5, 46.0, 74.0, bz + 2.0, 4.0, 'y', legs=2)      # yard B -> boiler B
    mirror_box(md, 14.5, 18.0, 0, 21.5, 23.0, 7.0, m_corr, wall='corr', plinth=-1, roofkey='roof_dark')   # hoppers
    # office blocks at both hall ends (rendered, flat roof)
    for (oy0, oy1, face, door) in ((76.0, 92.0, 'y+', 'x+'), (4.0, 20.0, 'y-', 'x-')):
        ow = {face: wins_row(hx0, hx1, 4.0, 8.0, 3.0, 3) + wins_row(hx0, hx1, 14.0, 18.0, 3.0, 3),
              'x+': wins_row(oy0, oy1, 14.0, 18.0, 3.0, 3), 'x-': wins_row(oy0, oy1, 14.0, 18.0, 3.0, 3)}
        md.add(box(hx0 + 1.0, oy0, 0, hx1 - 1.0, oy1, 21.0, m_block, wall='render_white', plinth=1.0,
                   roofkey='bitumen', frame='render_grey', wins=ow,
                   doors={door: [((oy0 + oy1) / 2.0 - 1.5, (oy0 + oy1) / 2.0 + 1.5, 1.0, 8.0)]}))
    # transformer yards (left and right corners) behind a steel fence
    for i in range(2):
        mirror_box(md, 5.0, 79.0 + i * 7.0, 0, 8.4, 84.0 + i * 7.0, 5.5, m_flat('steel'))
        mirror_box(md, 4.4, 79.6 + i * 7.0, 0.8, 5.0, 83.4 + i * 7.0, 4.6, m_flat('steel', -0.5), detail=True)
        for k in range(3):
            mirror_box(md, 6.4, 80.0 + i * 7.0 + k * 1.4, 5.5, 7.0, 80.5 + i * 7.0 + k * 1.4, 8.5, m_flat('trim'),
                       detail=True)
    add_portal(md, 16.0, 78.0, 16.0, 93.0, h=16.0)
    add_portal(md, 80.0, 3.0, 80.0, 18.0, h=16.0)
    add_fence(md, 23.0, 78.0, 23.0, 94.0, key='steel_lt', h=3.0, step=1.6)
    add_fence(md, 73.0, 2.0, 73.0, 18.0, key='steel_lt', h=3.0, step=1.6)
    gr = [(0.0, 0.0, 96.0, 96.0, 'gravel'), (2.0, 77.0, 23.0, 94.0, 'gravel_lt'), (73.0, 2.0, 94.0, 19.0, 'gravel_lt'),
          (36.0, 0.0, 60.0, 96.0, 'asphalt')]
    yards = [(1.0, 1.0, 36.0, 34.0), (60.0, 62.0, 95.0, 95.0)]
    md.ground = g_coalyard(gr, yards, seed)
    return md


# =========================================================================== nuclear power plant 3x3
def m_dome(g, st):
    """containment dome: pale concrete, a darker ring at the springing line, faint ring courses"""
    key = g.solid.par.get('key', 'nuke_white')
    z0 = g.solid.par.get('z0', 0.0)
    dz = g.P[2] - z0
    if dz < 1.2:
        return 'nuke_grey', -0.4
    if st.zoom >= 1 and int(dz) % 5 == 0 and dz < 10:
        return key, -0.3
    return key, 0.2


def reactor(md, x0, y0, x1, y1, wface, seed=1):
    """reactor building: a tall pale rectangular hall with a cylindrical containment rising from it and a dome"""
    hz = 44.0
    cx, cy = (x0 + x1) / 2.0, (y0 + y1) / 2.0
    w = {f: [] for f in ('y+', 'y-', 'x+', 'x-')}
    for f in w:
        lo, hi = (x0, x1) if f in ('y+', 'y-') else (y0, y1)
        w[f] = [((lo + hi) / 2.0 - 5.0, (lo + hi) / 2.0 + 5.0, hz - 7.0, hz - 4.5)]
    md.add(box(x0, y0, 0, x1, y1, hz, m_block, wall='nuke_white', plinth=1.5, roofkey='nuke_grey', parapet=1.0,
               joints=(99.0, 5.0), wins=w, wkind='band', frame='nuke_grey',
               doors={wface: [((x0 + x1) / 2.0 - 3.0 if wface in ('y+', 'y-') else (y0 + y1) / 2.0 - 3.0,
                               (x0 + x1) / 2.0 + 3.0 if wface in ('y+', 'y-') else (y0 + y1) / 2.0 + 3.0,
                               0, 9.0, 'steel')]}))
    r = min(x1 - x0, y1 - y0) / 2.0 - 2.5
    zc = hz + 10.0
    md.add(Cone(cx, cy, hz, zc, r, r, m_concrete_round, cap=False, key='nuke_white', band=(hz, hz + 1.2, 'nuke_grey')))
    md.add(Ellipsoid(cx, cy, zc, r, r, r * KZ * 0.95, m_dome, key='nuke_white', z0=zc))


def nuclear_plant(name, seed=1):
    """nuclear_plant (3x3): Loviisa-like two-unit plant on the shore: two tall pale-grey reactor halls with
    cylindrical containments and domes at opposite corners (left and right at r0), a long pale turbine hall with a
    window band between them, a slim ventilation stack at the centre, pump houses and transformers.  No cooling
    towers (sea / lake cooled).  Point-symmetric about the lot centre (rots 2); no smoke."""
    md = Model(name, 3, 3)
    # turbine hall across the middle (long, flat roof, window band)
    tx0, tx1, ty0, ty1 = 6.0, 90.0, 36.0, 60.0
    tz = 30.0
    band = {f: [(tx0 + 2.0, tx1 - 2.0, tz - 9.0, tz - 5.0)] for f in ('y+', 'y-')}
    band['x+'] = band['x-'] = [(ty0 + 2.0, ty1 - 2.0, tz - 9.0, tz - 5.0)]
    md.add(box(tx0, ty0, 0, tx1, ty1, tz, m_block, wall='render_white', plinth=1.5, roofkey='bitumen', parapet=0.8,
               wkind='band', frame='panel_dk', wins=band, joints=(30.0, 6.0),
               bands={'y+': [(tx0 + 4.0, tx0 + 30.0, 4.0, 7.0, 'balc_blue')],
                      'y-': [(tx1 - 30.0, tx1 - 4.0, 4.0, 7.0, 'balc_blue')]}))
    for (ax, ay) in ((20.0, 44.0), (66.0, 44.0)):
        md.add(box(ax, ay, tz, ax + 10.0, ay + 8.0, tz + 3.0, m_flat('steel')))
    # two reactor buildings: front-left and back-right (point-symmetric)
    reactor(md, 6.0, 62.0, 36.0, 92.0, 'x-', seed)
    reactor(md, 60.0, 4.0, 90.0, 34.0, 'x+', seed)
    # connecting blocks between reactor and turbine hall
    md.add(box(12.0, 58.0, 0, 30.0, 63.0, 22.0, m_block, wall='nuke_grey', plinth=1.0, roofkey='bitumen'))
    md.add(box(66.0, 33.0, 0, 84.0, 38.0, 22.0, m_block, wall='nuke_grey', plinth=1.0, roofkey='bitumen'))
    # ventilation stack at the centre, rising from the turbine hall roof (pale, two dark bands)
    md.add(Cone(48.0, 48.0, tz, 112.0, 2.6, 1.8, m_concrete_round, cap=True, key='nuke_white', capkey='cap',
                band=(104.0, 108.0, 'panel_dk')))
    # auxiliary: pump/intake houses + transformers in the free corners
    md.add(box(44.0, 64.0, 0, 58.0, 76.0, 10.0, m_block, wall='render_white', plinth=1.0, roofkey='bitumen',
               wins={'y+': wins_row(44.0, 58.0, 4.0, 7.0, 3.0, 2), 'x+': wins_row(64.0, 76.0, 4.0, 7.0, 3.0, 2)}))
    md.add(box(38.0, 20.0, 0, 52.0, 32.0, 10.0, m_block, wall='render_white', plinth=1.0, roofkey='bitumen',
               wins={'y-': wins_row(38.0, 52.0, 4.0, 7.0, 3.0, 2), 'x-': wins_row(20.0, 32.0, 4.0, 7.0, 3.0, 2)}))
    for i in range(3):
        add_trafo(md, 64.0 + i * 8.0, 70.0, 'x')
        add_trafo(md, 27.0 - i * 8.0, 22.6, 'x')
    add_portal(md, 63.0, 82.0, 89.0, 82.0, h=17.0)
    add_portal(md, 7.0, 14.0, 33.0, 14.0, h=17.0)
    add_fence(md, 62.0, 66.0, 92.0, 66.0, key='steel_lt', h=3.0, step=1.6)
    add_fence(md, 4.0, 30.0, 34.0, 30.0, key='steel_lt', h=3.0, step=1.6)
    gr = [(0.0, 0.0, 96.0, 96.0, 'concrete'), (40.0, 60.0, 96.0, 96.0, 'gravel'), (0.0, 0.0, 56.0, 36.0, 'gravel')]
    md.ground = lambda g, st: g_lot(g, st, gravel=gr, var=seed)
    return md


# =========================================================================== peat power plant 2x2
def m_peatpile(g, st):
    """milled peat heap: rust brown, darker crumbs, a lighter dry crust on top"""
    P = g.P
    if st.zoom >= 1 and st.texture > 0.3:
        n = ihash(int(P[0] * 1.4), int(P[1] * 1.4), int(P[2]), 37)
        if n < 0.14:
            return 'peat_dk', 0.0
        if n > 0.88:
            return 'peat', 0.6
    return 'peat', 0.2 if g.n[2] > 0.8 else 0.0


def peat_heap(cx, cy, rx, ry, h):
    hf = heap(cx, cy, rx, ry, h, 'peat')
    hf.mat = m_peatpile
    return hf


def add_open_shed(md, x0, y0, x1, y1, zhi, zlo, slope_axis, low_side):
    """open fuel shed: a mono-pitch corrugated roof on steel posts, a low concrete retaining wall on the two
    outer sides; low_side = the outer edge the roof slopes down to ('y+', 'y-', 'x+', 'x-')"""
    if slope_axis == 'y':
        z0, z1 = (zhi, zlo) if low_side == 'y+' else (zlo, zhi)
    else:
        z0, z1 = (zhi, zlo) if low_side == 'x+' else (zlo, zhi)
    md.add(incline(x0 - 0.6, x1 + 0.6, y0 - 0.6, y1 + 0.6, z0, z1, 1.0, slope_axis, m_corr, wall='corr',
                   roofkey='roof_metal'))
    for px in (x0 + 0.6, (x0 + x1) / 2.0, x1 - 0.6):
        for py in (y0 + 0.6, (y0 + y1) / 2.0, y1 - 0.6):
            if px not in (x0 + 0.6, x1 - 0.6) and py not in (y0 + 0.6, y1 - 0.6):
                continue
            if slope_axis == 'y':
                f = (py - y0) / (y1 - y0)
            else:
                f = (px - x0) / (x1 - x0)
            zt = z0 + (z1 - z0) * f
            md.add(box(px - 0.45, py - 0.45, 0, px + 0.45, py + 0.45, zt, m_flat('steel'), detail=True))


def peat_plant(name, seed=1):
    """peat_plant (2x2): 1970s peat-fired plant (turvevoimala): a low boiler hall in ochre ribbed cladding with a
    taller boiler house at its centre and a slender concrete chimney rising from it, two open fuel sheds with
    rust-brown milled peat at opposite corners (left and right at r0), conveyor bridges from the sheds' feed
    hoppers up to the hall.  Point-symmetric about the lot centre (rots 2)."""
    md = Model(name, 2, 2)
    # low hall along X + taller boiler house in the middle
    hx0, hx1, hy0, hy1 = 16.0, 48.0, 23.0, 41.0
    ze, zr = 18.0, 21.0
    hw = {f: strip_wins(hx0, hx1, 6.0, 14.0, w=3.0, gap=7.0) for f in ('y+', 'y-')}
    md.add(gable_body(hx0, hy0, hx1, hy1, 0, ze, zr, 'x', m_corr, wall='clad_green', plinth=1.5, frame='steel',
                      wins=hw, doors={'x+': [(29.0, 35.0, 0, 10.0, 'steel')], 'x-': [(29.0, 35.0, 0, 10.0, 'steel')]}))
    md.add(gable_roof(hx0, hy0, hx1, hy1, ze, zr, 'x', m_roof, ov=0.7, ovg=0.7, th=1.0, key='roof_dark',
                      edgekey='clad_ochre', seam=3.0))
    bw = {f: strip_wins(25.0, 39.0, 19.0, 31.0, w=3.0, gap=5.0) for f in ('y+', 'y-', 'x+', 'x-')}
    md.add(box(25.0, 25.0, 0, 39.0, 39.0, 34.0, m_corr, wall='clad_green', plinth=1.5, frame='steel', roofkey='bitumen',
               wins=bw))
    md.add(box(24.6, 24.6, 34.0, 39.4, 39.4, 35.6, m_flat('clad_ochre')))
    # chimney at the centre (slender concrete, soot band)
    md.add(Cone(32.0, 32.0, 35.0, 92.0, 2.5, 1.9, m_concrete_round, cap=True, key='concrete', capkey='cap',
                band=(88.0, 92.0, 'cap')))
    md.hot = (32.0, 32.0, 93.0)
    # open peat sheds (left corner = front-left, right corner = back-right at r0) with long peat windrows
    add_open_shed(md, 2.0, 43.0, 26.0, 62.0, 21.0, 18.0, 'y', 'y+')
    add_open_shed(md, 38.0, 2.0, 62.0, 21.0, 21.0, 18.0, 'y', 'y-')
    md.add(peat_heap(14.0, 52.5, 11.5, 8.0, 10.5))
    md.add(peat_heap(50.0, 11.5, 11.5, 8.0, 10.5))
    mirror_box(md, 1.5, 61.5, 0, 26.5, 62.5, 3.0, m_flat('concrete'), S=64.0)       # retaining walls
    mirror_box(md, 1.5, 42.5, 0, 2.5, 62.5, 3.0, m_flat('concrete'), S=64.0)
    # feed hoppers + conveyor bridges up into the hall
    mirror_box(md, 27.0, 50.0, 0, 33.0, 56.0, 6.5, m_corr, S=64.0, wall='corr', plinth=-1, roofkey='roof_dark')
    add_conveyor(md, 28.5, 31.5, 39.0, 51.0, 19.0, 6.5, 'y', legs=1)
    add_conveyor(md, 32.5, 35.5, 13.0, 25.0, 6.5, 19.0, 'y', legs=1)
    # peat tipper lorries + a transformer in the other two corners
    for (x0, y0, x1, y1, key) in ((6.0, 6.5, 11.0, 8.5, 'tyre'), (6.0, 13.5, 11.0, 15.5, 'tyre'),
                                  (6.0, 18.5, 11.0, 20.5, 'tyre')):
        mirror_box(md, x0 - 0.2, y0, 0, x1 + 0.2, y1, 2.2, m_flat(key), S=64.0)
    mirror_box(md, 5.8, 17.0, 1.2, 11.2, 22.0, 7.5, m_flat('sign_red'), S=64.0)            # cab
    mirror_box(md, 6.2, 20.4, 4.6, 10.8, 22.05, 6.8, m_flat('glass'), S=64.0)
    mirror_box(md, 5.6, 4.0, 1.4, 11.4, 16.4, 5.2, m_flat('steel'), S=64.0)                # tipper body
    mirror_box(md, 6.2, 4.6, 5.2, 10.8, 15.8, 6.6, m_flat('peat'), S=64.0)                 # load
    mirror_box(md, 14.0, 4.0, 0, 17.4, 9.0, 5.5, m_flat('steel'), S=64.0)                  # transformer
    for k in range(3):
        mirror_box(md, 15.4, 5.0 + k * 1.4, 5.5, 16.0, 5.5 + k * 1.4, 8.5, m_flat('trim'), S=64.0, detail=True)
    gr = [(0.0, 0.0, 64.0, 64.0, 'gravel'), (1.0, 42.0, 27.0, 63.0, 'dirt_dk'), (37.0, 1.0, 63.0, 22.0, 'dirt_dk')]
    md.ground = lambda g, st: g_lot(g, st, gravel=gr, var=seed)
    return md


# =========================================================================== oil power plant 2x2
def oil_tank(md, cx, cy, r=8.5, h=16.0):
    """fuel oil tank: light shell with seam bands, a shallow conical roof, a spiral-ish stair strip and a bund"""
    md.add(Cone(cx, cy, 0, h, r, r, m_tank, cap=True, key='steel_lt'))
    md.add(Cone(cx, cy, h, h + 2.2, r, 1.2, m_flat('steel', 0.4), cap=True))
    md.add(box(cx + r * 0.62, cy + r * 0.62, 0, cx + r * 0.78, cy + r * 0.78, h, m_flat('steel'), detail=True))
    # bund: a low square concrete wall around the tank
    e = r + 2.6
    for (x0, y0, x1, y1) in ((cx - e, cy - e, cx + e, cy - e + 0.9), (cx - e, cy + e - 0.9, cx + e, cy + e),
                             (cx - e, cy - e, cx - e + 0.9, cy + e), (cx + e - 0.9, cy - e, cx + e, cy + e)):
        md.add(box(x0, y0, 0, x1, y1, 2.2, m_flat('concrete'), noshadow=True))


def oil_plant(name, seed=1):
    """oil_plant (2x2): 1960s oil-fired power plant: a pale rendered boiler house with tall window strips and a
    lower turbine hall along Y, a tall slender concrete chimney at the centre, two big fuel-oil tanks in concrete
    bunds at opposite corners (left and right at r0) with pipe bridges to the boiler house, a pump house and a
    transformer.  Point-symmetric about the lot centre (rots 2)."""
    md = Model(name, 2, 2)
    # turbine hall (lower, long along Y) and the boiler house in the middle
    tx0, tx1, ty0, ty1 = 22.0, 42.0, 10.0, 54.0
    tz = 20.0
    tw = {f: strip_wins(ty0, ty1, 5.0, 16.0, w=3.0, gap=6.0) for f in ('x+', 'x-')}
    tw['y+'] = tw['y-'] = wins_row(tx0, tx1, 5.0, 16.0, 3.0, 3)
    md.add(box(tx0, ty0, 0, tx1, ty1, tz, m_block, wall='oil_white', plinth=1.5, roofkey='bitumen', parapet=0.8,
               frame='render_grey', wins=tw, doors={'y+': [(30.0, 34.0, 0, 9.0, 'steel')],
                                                     'y-': [(30.0, 34.0, 0, 9.0, 'steel')]}))
    bw = {f: strip_wins(24.0, 40.0, 22.0, 40.0, w=2.0, gap=4.0) for f in ('x+', 'x-', 'y+', 'y-')}
    md.add(box(24.0, 22.0, 0, 40.0, 42.0, 44.0, m_block, wall='oil_white', plinth=1.5, roofkey='bitumen', parapet=0.8,
               frame='render_grey', wins={'x+': strip_wins(22.0, 42.0, 23.0, 40.0, w=2.0, gap=4.0),
                                          'x-': strip_wins(22.0, 42.0, 23.0, 40.0, w=2.0, gap=4.0),
                                          'y+': bw['y+'], 'y-': bw['y-']},
               bands={'y+': [(25.0, 39.0, 41.0, 43.0, 'balc_blue')], 'y-': [(25.0, 39.0, 41.0, 43.0, 'balc_blue')],
                      'x+': [(23.0, 41.0, 41.0, 43.0, 'balc_blue')], 'x-': [(23.0, 41.0, 41.0, 43.0, 'balc_blue')]}))
    # chimney at the centre: tall slender concrete, soot band
    md.add(Cone(32.0, 32.0, 44.0, 108.0, 2.7, 2.0, m_concrete_round, cap=True, key='concrete', capkey='cap',
                band=(104.0, 108.0, 'cap')))
    md.hot = (32.0, 32.0, 109.0)
    # fuel-oil tanks (left and right corners) + pipe bridges to the boiler house
    oil_tank(md, 11.5, 52.5)
    oil_tank(md, 52.5, 11.5)
    mirror_box(md, 15.0, 44.0, 9.0, 22.0, 45.2, 10.2, m_flat('steel'), S=64.0)          # pipe runs
    mirror_box(md, 15.0, 45.6, 9.0, 22.0, 46.8, 10.2, m_flat('steel_lt'), S=64.0)
    mirror_box(md, 16.0, 44.0, 0, 16.8, 47.0, 9.0, m_flat('steel'), S=64.0, detail=True)
    # pump house + transformer in the free corners
    mirror_box(md, 3.0, 3.0, 0, 15.0, 13.0, 9.0, m_block, S=64.0, wall='render_grey', plinth=1.0, roofkey='bitumen')
    mirror_box(md, 3.0, 17.0, 0, 8.0, 20.4, 5.5, m_flat('steel'), S=64.0)
    gr = [(0.0, 0.0, 64.0, 64.0, 'gravel'), (20.0, 0.0, 44.0, 64.0, 'asphalt')]
    md.ground = lambda g, st: g_lot(g, st, gravel=gr, var=seed)
    return md


# =========================================================================== tables
ENTRIES = [
    ('coal_station', (3, 3), 2, [_v(coal_station, 'cs')], None),
    ('nuclear_plant', (3, 3), 2, [_v(nuclear_plant, 'np')], None),
    ('peat_plant', (2, 2), 2, [_v(peat_plant, 'pp')], None),
    ('oil_plant', (2, 2), 2, [_v(oil_plant, 'op')], None),
]
KEY_ALIASES = []
GROUPS = {'coal_station': '8 power (1.2)', 'nuclear_plant': '8 power (1.2)', 'peat_plant': '8 power (1.2)',
          'oil_plant': '8 power (1.2)'}
# second smoke hotspot per key: canonical (X, Y, Z) of the second chimney top -> alias rows <sprite>_hot2
HOT2 = {'coal_station': (CS_CHIMS[1][0] - CS_CHIMS[0][0], CS_CHIMS[1][1] - CS_CHIMS[0][1], 0.0)}
