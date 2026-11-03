"""art_m2_cplx.py - Milestone-2 world art (task M4), group C: the paper mill complex (Kuusikosken Paperi Oy),
the Valtion Metalli works, patron gifts (workers' row houses, church) and the landmarks.

Complex pieces are ordinary <= 3x3 lots with ONE fixed orientation (rots 1; square pieces get r1..r3 aliases,
non-square ones none, so their defs need BDF_NO_ROTATE + BDF_MODULE).  Templates (tile offsets from the
complex's north tile, canonical = world at r0, x to the lower right, y to the lower left):

  Paper mill 5x5, water along the -X side (log pond by the log yard):
     y=0: L L M M M     L pm_logyard 2x2 @ (0,0)     M pm_hall 3x3 @ (2,0)
     y=1: L L M M M     C pm_chipper 2x1 @ (0,2)     B pm_boiler 2x2 @ (0,3) (= heat_plant v0 r0, alias)
     y=2: C C M M M     D pm_digester 2x2 @ (2,3)    G pm_gate 1x1 @ (4,3)   O pm_office 1x1 @ (4,4)
     y=3: B B D D G
     y=4: B B D D O
  Valtion Metalli 5x4, railway within 2 tiles (the siding's open end is its -X end):
     y=0: H H H Y Y     H vm_hall 3x3 @ (0,0)        Y vm_yard 2x2 @ (3,0)
     y=1: H H H Y Y     Z vm_store 2x1 @ (3,2)       S vm_siding 3x1 @ (0,3)
     y=2: H H H Z Z     G vm_gate 2x1 @ (3,3)
     y=3: S S S G G
"""
from art_m2_common import *  # noqa: F401,F403
from art_m2_common import _v, add_colours  # noqa: F401

add_colours({
    'logbark': '#6e5a44', 'marble': '#dcd9cf', 'bronze': '#6b5843', 'vm_tractor': '#a65a3e',
    'wagon_red': '#7a3a2a', 'log_dark': '#55433a', 'redlead': '#8a4a3a', 'crane_grey': '#6f7f88',
    'paper_roll': '#d8d4c6', 'mat_green': '#7d8a5c', 'court_cinder': '#94604a',
})


# =========================================================================== helpers
def cyl_x(x0, x1, cy, cz, r, mat, n=12, **par):
    """horizontal cylinder along X (radius r units, centre height cz px), faceted n-gon"""
    rz = r * KZ
    k = math.cos(math.pi / n)
    pl = [(-1, 0, 0, -x0, 'x-'), (1, 0, 0, x1, 'x+')]
    for i in range(n):
        a = 2 * math.pi * (i + 0.5) / n
        ny, nz = math.cos(a) / r, math.sin(a) / rz
        pl.append((0, ny, nz, k + ny * cy + nz * cz, 'side'))
    return Convex(pl, (x0, cy - r, cz - rz, x1, cy + r, cz + rz), mat, **par)


def cyl_y(y0, y1, cx, cz, r, mat, n=12, **par):
    rz = r * KZ
    k = math.cos(math.pi / n)
    pl = [(0, -1, 0, -y0, 'y-'), (0, 1, 0, y1, 'y+')]
    for i in range(n):
        a = 2 * math.pi * (i + 0.5) / n
        nx, nz = math.cos(a) / r, math.sin(a) / rz
        pl.append((nx, 0, nz, k + nx * cx + nz * cz, 'side'))
    return Convex(pl, (cx - r, y0, cz - rz, cx + r, y1, cz + rz), mat, **par)


def ramp_x(x0, x1, y0, y1, z0, z1, th, mat, **par):
    """inclined slab (conveyor gallery / in-run) from height z0 at x0 to z1 at x1, thickness th px"""
    s = (z1 - z0) / float(x1 - x0)
    pl = [(-1, 0, 0, -x0, 'x-'), (1, 0, 0, x1, 'x+'), (0, -1, 0, -y0, 'y-'), (0, 1, 0, y1, 'y+'),
          (-s, 0, 1, z0 + th - s * x0, 'top'), (s, 0, -1, -(z0 - s * x0), 'bot')]
    return Convex(pl, (x0, y0, min(z0, z1), x1, y1, max(z0, z1) + th), mat, **par)


def m_logpile(g, st):
    """log pile: bark on top/long faces, round cut ends on the X faces (or Y with ends='y')"""
    ends = ('y+', 'y-') if g.solid.par.get('ends') == 'y' else ('x+', 'x-')
    s, t = local_st(g)
    if g.tag in ends:
        cs, ct = (s % 2.2) - 1.1, (t % 2.0) - 1.0
        if cs * cs + ct * ct < 0.95:
            return 'logend', (0.3 if ihash(int(s / 2.2), int(t / 2.0), 7) < 0.5 else -0.1)
        return 'logbark', -0.9
    if g.tag == 'top':
        return 'logbark', (0.4 if (g.P[1 if ends[0] == 'x+' else 0] % 2.2) < 1.1 else -0.2)
    if st.zoom >= 1 and (t % 2.0) < 0.6:
        return 'logbark', -0.6
    return 'logbark', 0.0


def add_logpile(md, x0, y0, x1, y1, h, ends='x', steps=2):
    """stepped log pile (wider at the bottom)"""
    hh = h / float(steps)
    for k in range(steps):
        i = k * 1.6
        md.add(box(x0 + i * (ends == 'y'), y0 + i * (ends == 'x'), k * hh, x1 - i * (ends == 'y'),
                   y1 - i * (ends == 'x'), (k + 1) * hh, m_logpile, ends=ends))


def m_glassband(g, st):
    """continuous glazing strip (skylight sides, foyer)"""
    if g.tag == 'top':
        return 'roof_dark', 0.0
    s, t = local_st(g)
    if (s % 3.0) < 0.6 and st.zoom >= 1:
        return 'steel', 0.0
    return 'factory_glass', (0.6 if (s % 6.0) < 3.0 else 0.0)


def m_marble(g, st):
    """white marble cladding: thin vertical joints, dark strip windows (Finlandia)"""
    p = g.solid.par
    if g.tag == 'top':
        return 'marble', 0.3
    s, t = local_st(g)
    for r in p.get('wins', {}).get(g.tag, ()):
        if in_rect(s, t, r):
            return 'glass', 0.0
    if st.zoom >= 1 and (s % 2.6) < 0.5:
        return 'marble', -0.5
    if st.zoom >= 1 and ihash(int(s * 1.3), int(t * 0.7), 31) < 0.06:
        return 'marble', -0.3
    return 'marble', 0.0


def m_stonewall(g, st):
    """low fieldstone wall (kiviaita)"""
    P = g.P
    h = ihash(int(P[0] * 0.9), int(P[1] * 0.9), int(P[2] * 1.2), 17)
    if g.tag == 'top':
        return 'rock', 0.2 + (h - 0.5) * 0.6
    return ('rock_dk' if h < 0.3 else 'rock'), (h - 0.5) * 0.8


def m_wagon(g, st):
    """boxcar: vertical planks, a sliding door in the middle of the long faces"""
    p = g.solid.par
    if g.tag == 'top':
        return 'roof_dark', 0.2
    s, t = local_st(g)
    b = g.solid.aabb
    if g.tag in ('y+', 'y-'):
        mid = (b[0] + b[3]) / 2.0
        if abs(s - mid) < 3.0:
            if abs(s - mid) > 2.6:
                return 'cap', 0.0
            return p.get('key', 'wagon_red'), -0.3
    if st.zoom >= 1 and (s % 1.6) < 0.5:
        return p.get('key', 'wagon_red'), -0.5
    return p.get('key', 'wagon_red'), 0.0


def add_clock(bands, face, s0, t0, size=3.0):
    """clock face (light square with a dark centre) into a bands dict"""
    c = size / 2.0
    bands.setdefault(face, [])
    bands[face].insert(0, (s0 + c - 0.5, s0 + c + 0.5, t0 + c - 0.5, t0 + c + 0.8, 'cap'))
    bands[face].append((s0, s0 + size, t0, t0 + size, 'trim'))


def add_tractor(md, x, y, heading='x', key='vm_tractor'):
    """small farm tractor (VM Kontio): bonnet, cab, big rear wheels; ~8 x 4.6 units"""
    gp = new_grp()
    if heading == 'x':          # bonnet toward +X
        for wy in (y - 2.3, y + 1.3):
            md.add(box(x - 3.4, wy, 0, x - 0.6, wy + 1.0, 3.8, m_flat('tyre'), grp=gp))
            md.add(box(x + 2.0, wy + 0.2, 0, x + 3.6, wy + 0.8, 2.2, m_flat('tyre'), grp=gp))
        md.add(box(x - 2.6, y - 1.2, 1.2, x + 4.0, y + 1.2, 4.0, m_flat(key), grp=gp))
        md.add(box(x - 3.0, y - 1.4, 4.0, x - 0.4, y + 1.4, 7.8, m_flat('glass', -0.2), grp=gp))
        md.add(box(x - 3.2, y - 1.6, 7.8, x - 0.2, y + 1.6, 8.5, m_flat(key, 0.3), grp=gp))
        md.add(box(x + 2.4, y - 0.3, 4.0, x + 2.9, y + 0.2, 6.5, m_flat('steel'), detail=True, grp=gp))
    else:
        for wx in (x - 2.3, x + 1.3):
            md.add(box(wx, y - 3.4, 0, wx + 1.0, y - 0.6, 3.8, m_flat('tyre'), grp=gp))
            md.add(box(wx + 0.2, y + 2.0, 0, wx + 0.8, y + 3.6, 2.2, m_flat('tyre'), grp=gp))
        md.add(box(x - 1.2, y - 2.6, 1.2, x + 1.2, y + 4.0, 4.0, m_flat(key), grp=gp))
        md.add(box(x - 1.4, y - 3.0, 4.0, x + 1.4, y - 0.4, 7.8, m_flat('glass', -0.2), grp=gp))
        md.add(box(x - 1.6, y - 3.2, 7.8, x + 1.6, y - 0.2, 8.5, m_flat(key, 0.3), grp=gp))
        md.add(box(x - 0.3, y + 2.4, 4.0, x + 0.2, y + 2.9, 6.5, m_flat('steel'), detail=True, grp=gp))


def window_bands(lo, hi, z0, n, h=4.5, gap=STOREY, inset=1.5):
    return [(lo + inset, hi - inset, z0 + k * gap, z0 + k * gap + h) for k in range(n)]


# =========================================================================== paper mill
def pm_logyard(name, seed=1):
    """2x2 @ (0,0): log pond on the -X (water) side with floating logs, mountains of stepped log piles,
    a slewing log crane on a tower, and an inclined conveyor gallery running +X into the machine hall"""
    md = Model(name, 2, 2)
    # floating logs in the pond strip
    for i in range(7):
        yy = 5.0 + i * 8.0 + ihash(i, 3, seed) * 2.0
        xx = 1.0 + ihash(i, 4, seed) * 2.5
        md.add(cyl_y(yy, yy + 6.0 + ihash(i, 5, seed) * 3.0, xx + 1.0, 0.5, 0.6, m_flat('logbark'), n=6))
    # log boom along the pond edge
    md.add(box(6.6, 1.0, 0, 7.2, 63.0, 0.8, m_flat('fence_wood'), detail=True, noshadow=True))
    # stepped log piles, cut ends facing +X / -X
    add_logpile(md, 10.0, 4.0, 34.0, 16.0, 12.0, 'x', 3)
    add_logpile(md, 10.0, 20.0, 34.0, 31.0, 10.0, 'x', 2)
    add_logpile(md, 38.0, 36.0, 61.0, 47.0, 11.0, 'x', 3)
    add_logpile(md, 10.0, 46.0, 30.0, 60.0, 9.0, 'x', 2)
    add_logpile(md, 38.0, 51.0, 61.0, 61.0, 7.0, 'x', 2)
    # slewing crane: lattice tower + jib + counterweight
    cx, cy = 22.0, 39.0
    md.add(box(cx - 1.6, cy - 1.6, 0, cx + 1.6, cy + 1.6, 34.0, m_flat('crane_grey'), detail=True))
    md.add(box(cx - 2.2, cy - 2.2, 34.0, cx + 2.2, cy + 2.2, 38.0, m_block, wall='crane_grey', plinth=-1,
               wins={'y+': [(cx - 1.4, cx + 1.4, 35.0, 37.4)], 'x+': [(cy - 1.4, cy + 1.4, 35.0, 37.4)]},
               roofkey='crane_grey', parapet=0.0))
    md.add(box(cx - 9.0, cy - 0.6, 38.0, cx + 2.0, cy + 0.6, 39.4, m_flat('crane_grey', 0.2), detail=True))
    md.add(box(cx - 0.6, cy - 0.6, 38.0, cx + 0.6, cy + 22.0, 39.4, m_flat('crane_grey', 0.2), detail=True))
    md.add(box(cx - 9.0, cy - 1.4, 34.5, cx - 6.0, cy + 1.4, 38.0, m_flat('concrete_dk')))
    md.add(box(cx - 0.15, cy + 20.5, 22.0, cx + 0.15, cy + 20.8, 38.0, m_flat('cap'), detail=True, noshadow=True))
    # conveyor gallery to the hall (rises along +X, ends on the lot edge where the hall continues it)
    md.add(ramp_x(36.0, 64.0, 24.0, 29.0, 6.0, 22.0, 4.5, m_corr, wall='corr', roofkey='corr'))
    for lx in (42.0, 52.0, 61.0):
        z = 6.0 + (lx - 36.0) * 16.0 / 28.0
        md.add(box(lx - 0.5, 26.0, 0, lx + 0.5, 27.0, z, m_flat('steel'), detail=True))
    md.add(box(32.0, 22.0, 0, 38.0, 31.0, 7.0, m_corr, wall='corr', roofkey='roof_dark'))   # feed hopper
    gr = [(0.0, 0.0, 7.0, 64.0, 'water'), (7.0, 0.0, 64.0, 64.0, 'dirt'), (34.0, 0.0, 64.0, 34.0, 'gravel')]
    md.ground = lambda g, st: g_lot(g, st, gravel=gr, var=seed)
    return md


def pm_hall(name, seed=1):
    """3x3 @ (2,0): white functionalist machine hall (PM1-PM3) along X with a long ridge lantern, a taller
    drying-end block, steam vents (hotspot), and a two-storey finishing / reel store in front with a dock"""
    md = Model(name, 3, 3)
    pl = 2.0
    # main machine hall
    hx0, hx1, hy0, hy1 = 2.0, 70.0, 8.0, 54.0
    hz = 30.0
    w = {'y+': [(hx0 + 3 + 6.5 * i, hx0 + 6.5 + 6.5 * i, 9.0, 25.0) for i in range(10)],
         'y-': [(hx0 + 3 + 6.5 * i, hx0 + 6.5 + 6.5 * i, 9.0, 25.0) for i in range(10)],
         'x-': [(hy0 + 4 + 7 * i, hy0 + 7.5 + 7 * i, 9.0, 25.0) for i in range(6)]}
    md.add(box(hx0, hy0, 0, hx1, hy1, hz, m_block, wall='render_white', plinth=pl, wins=w, wkind='tall',
               frame='render_grey', roofkey='bitumen', parapet=1.0,
               bands={'y+': [(hx0 + 4.0, hx0 + 40.0, hz - 3.6, hz - 0.8, 'sign_blue', 'text')]}))
    # ridge lantern (skylight strip)
    md.add(box(hx0 + 4.0, 26.0, hz, hx1 - 4.0, 36.0, hz + 5.0, m_glassband))
    md.add(box(hx0 + 3.4, 25.4, hz + 5.0, hx1 - 3.4, 36.6, hz + 6.2, m_flat('roof_dark', 0.2)))
    # drying-end block (taller, +X end)
    dx0, dx1 = 70.0, 92.0
    dz = 40.0
    dw = {'y+': window_bands(dx0, dx1, 8.0, 3, 5.0, 11.0), 'x+': window_bands(hy0 - 4, hy1, 8.0, 3, 5.0, 11.0),
          'y-': window_bands(dx0, dx1, 8.0, 3, 5.0, 11.0)}
    md.add(box(dx0, hy0 - 4.0, 0, dx1, hy1, dz, m_block, wall='render_white', plinth=pl, wins=dw, wkind='band',
               frame='render_grey', roofkey='bitumen', parapet=1.0))
    # steam vents on the hall roof + hood on the drying end
    for i, vx in enumerate((14.0, 30.0, 46.0, 60.0)):
        md.add(Cone(vx, 18.0, hz, hz + 9.0 + (i % 2) * 3.0, 1.5, 1.2, m_concrete_round, cap=True, key='steel',
                    band=(hz + 7.0, hz + 12.0, 'cap')))
    md.add(box(76.0, 22.0, dz, 86.0, 34.0, dz + 5.0, m_corr, wall='corr', roofkey='roof_dark'))
    md.add(Cone(81.0, 28.0, dz + 5.0, dz + 14.0, 2.2, 1.8, m_concrete_round, cap=True, key='steel',
                band=(dz + 12.0, dz + 14.0, 'cap')))
    md.hot = (81.0, 28.0, dz + 15.0)
    # finishing / reel store in front (two storeys, loading dock toward +Y)
    fx0, fx1, fy0, fy1 = 4.0, 60.0, 58.0, 84.0
    fz = 2 * STOREY + 3.0
    fw = {'y+': window_bands(fx0, fx1, 4.0, 2, 4.5, STOREY + 1.0)[1:],
          'x-': window_bands(fy0, fy1, 4.0, 2, 4.5, STOREY + 1.0),
          'x+': window_bands(fy0, fy1, 4.0, 2, 4.5, STOREY + 1.0)[1:]}
    doors = {'y+': [(fx0 + 6.0 + 12.0 * i, fx0 + 12.0 + 12.0 * i, 0.5, 9.0, 'steel') for i in range(4)]}
    md.add(box(fx0, fy0, 0, fx1, fy1, fz, m_block, wall='render_white', plinth=pl, wins=fw, doors=doors,
               wkind='band', frame='render_grey', roofkey='bitumen', parapet=1.0))
    md.add(box(fx0, fy1, 0, fx1, fy1 + 4.0, 3.5, m_concrete, key='concrete'))                # dock
    md.add(box(fx0, fy1, 10.0, fx1, fy1 + 4.5, 10.8, m_flat('panel_dk', 0.5)))                 # canopy
    # conveyor stub from the chipper / log yard (continues the gallery from the -X lot edge)
    md.add(ramp_x(0.0, 2.0, 24.0, 29.0, 22.0, 23.2, 4.5, m_corr, wall='corr', roofkey='corr'))
    # stair tower with a flat cap (Sunila touch)
    md.add(box(60.0, 56.0, 0, 68.0, 66.0, 44.0, m_block, wall='render_white', plinth=pl, roofkey='bitumen',
               wins={'y+': [(62.5, 65.5, z, z + 7.0) for z in (8.0, 20.0, 32.0)], 'x+': [(59.0, 63.0, 34.0, 40.0)]},
               frame='render_grey', parapet=0.8))
    md.add(box(59.4, 55.4, 44.0, 68.6, 66.6, 45.2, m_flat('render_grey', 0.3)))
    # paper reels standing on the dock apron
    for i in range(5):
        md.add(Cone(66.0 + i * 5.0, 76.0, 0, 7.0, 2.1, 2.1, m_concrete_round, cap=True, key='paper_roll',
                    capkey='paper_roll'))
    for i in range(3):
        md.add(Cone(68.5 + i * 5.0, 81.5, 0, 7.0, 2.1, 2.1, m_concrete_round, cap=True, key='paper_roll',
                    capkey='paper_roll'))
    gr = [(0.0, 0.0, 96.0, 96.0, 'asphalt'), (0.0, 0.0, 96.0, 6.0, 'gravel'), (62.0, 70.0, 94.0, 90.0, 'concrete')]
    md.ground = lambda g, st: g_lot(g, st, gravel=gr, var=seed)
    return md


def pm_chipper(name, seed=1):
    """2x1 @ (0,2): debarking drum on piers, a corrugated chipper house, a chip heap, and the gallery rising
    along +X toward the hall"""
    md = Model(name, 2, 1)
    md.add(cyl_x(4.0, 30.0, 14.0, 6.0, 4.0, m_flat('rust'), n=12))
    for bx in (6.0, 26.0):
        md.add(box(bx, 10.5, 0, bx + 2.5, 17.5, 2.6, m_concrete, key='concrete_dk'))
    for rx in (10.0, 17.0, 24.0):           # drum tyres
        md.add(cyl_x(rx, rx + 1.0, 14.0, 6.0, 4.4, m_flat('steel'), n=12))
    md.add(box(32.0, 6.0, 0, 46.0, 24.0, 14.0, m_corr, wall='corr', roofkey='roof_dark',
               doors={'y+': [(36.0, 42.0, 0, 8.0, 'steel')]}, wins={'x-': [(10.0, 14.0, 9.0, 12.0)]}))
    md.add(mono_body(31.4, 5.4, 46.6, 24.6, 14.0, 14.4, 16.6, m_flat('roof_dark', 0.2), rise='y-'))
    md.add(ramp_x(46.0, 64.0, 13.0, 18.0, 12.0, 22.0, 4.5, m_corr, wall='corr', roofkey='corr'))
    md.add(box(55.0, 15.0, 0, 56.0, 16.0, 16.0, m_flat('steel'), detail=True))
    md.add(HeightField(48.0, 18.5, 63.0, 31.0, 6.0,
                       lambda x, y: 6.0 * max(0.0, 1.0 - ((x - 55.5) / 7.0) ** 2 - ((y - 25.0) / 6.0) ** 2) ** 0.8,
                       m_flat('sawdust')))
    gr = [(0.0, 0.0, 64.0, 32.0, 'asphalt'), (2.0, 8.0, 32.0, 21.0, 'sawdust')]
    md.ground = lambda g, st: g_lot(g, st, gravel=gr, var=seed)
    return md


def pm_digester(name, seed=1):
    """2x2 @ (2,3): three tall digesters with conical tops, a row of ribbed chip silos, pipe bridges, a
    pump house and a tank"""
    md = Model(name, 2, 2)
    for i, (cx, cy) in enumerate(((12.0, 14.0), (24.0, 14.0), (36.0, 14.0))):
        md.add(Cone(cx, cy, 0, 40.0, 4.2, 4.2, m_concrete_round, cap=False, key='steel_lt', ribs=0,
                    band=(10.0, 11.0, 'steel')))
        md.add(Cone(cx, cy, 40.0, 45.0, 4.2, 1.2, m_flat('steel_lt', 0.2), cap=True))
        md.add(Cone(cx, cy, 45.0, 48.0, 0.6, 0.6, m_flat('steel'), cap=True, detail=True))
    md.add(box(8.0, 13.2, 30.0, 52.0, 14.8, 31.6, m_flat('steel'), detail=True))                # top pipe bridge
    for px in (48.0, 52.0):
        md.add(box(px, 13.2, 0, px + 0.8, 14.8, 30.0, m_flat('steel'), detail=True))
    for i in range(3):                                                                          # chip silos
        cx = 12.0 + i * 12.0
        md.add(Cone(cx, 34.0, 0, 30.0, 5.0, 5.0, m_concrete_round, cap=True, key='concrete', ribs=18,
                    capkey='concrete_dk'))
        md.add(box(cx - 1.0, 34.0, 30.0, cx + 1.0, 36.0, 33.0, m_flat('corr'), detail=True))
    md.add(box(8.0, 33.2, 30.0, 52.0, 35.0, 33.0, m_corr, wall='corr', roofkey='corr'))          # silo gallery
    md.add(box(44.0, 28.0, 0, 60.0, 42.0, 12.0, m_block, wall='render_white', plinth=1.5, roofkey='bitumen',
               wins={'y+': wins_row(44.0, 60.0, 4.0, 9.0, 3.0, 3), 'x+': wins_row(28.0, 42.0, 4.0, 9.0, 3.0, 2)},
               doors={'y+': [(52.0, 55.0, 0.5, 8.0, 'steel')]}, frame='render_grey'))
    md.add(Cone(52.0, 52.0, 0, 14.0, 6.0, 6.0, m_concrete_round, cap=True, key='steel', capkey='steel_lt',
                band=(13.0, 14.0, 'cap')))                                                          # tank
    md.add(box(30.0, 48.0, 8.0, 46.0, 49.6, 9.4, m_flat('steel'), detail=True))                 # low pipe run
    for px in (32.0, 40.0):
        md.add(box(px, 48.2, 0, px + 0.8, 49.2, 8.0, m_flat('steel'), detail=True))
    gr = [(0.0, 0.0, 64.0, 64.0, 'concrete'), (0.0, 44.0, 40.0, 64.0, 'asphalt')]
    md.ground = lambda g, st: g_lot(g, st, gravel=gr, var=seed)
    return md


def pm_gate(name, seed=1):
    """1x1 @ (4,3): rendered gatehouse with a slender clock tower and a barrier"""
    md = Model(name)
    bands = {}
    add_clock(bands, 'y+', 21.5, 32.0, 3.0)
    add_clock(bands, 'x+', 9.5, 32.0, 3.0)
    md.add(box(4.0, 8.0, 0, 18.0, 20.0, 10.0, m_block, wall='render_white', plinth=1.0, roofkey='bitumen',
               wins={'y+': [(6.0, 12.0, 3.5, 7.5)], 'x+': [(10.0, 14.0, 3.5, 7.5)]},
               doors={'y+': [(13.0, 15.5, 0.5, 7.5)]}, frame='render_grey'))
    md.add(box(3.4, 7.4, 10.0, 18.6, 20.6, 10.8, m_flat('render_grey', 0.3)))
    md.add(box(20.0, 7.0, 0, 26.0, 13.0, 38.0, m_block, wall='render_white', plinth=1.0, roofkey='bitumen',
               bands=bands, wins={'y+': [(22.0, 24.0, 12.0, 26.0)]}, frame='render_grey', parapet=0.6))
    md.add(box(19.4, 6.4, 38.0, 26.6, 13.6, 39.0, m_flat('render_grey', 0.3)))
    add_flagpole(md, 28.0, 4.0, 30.0, flag=True)
    md.add(box(18.0, 23.0, 2.6, 30.0, 23.8, 3.4, m_flat('sign_red', 0.3), detail=True))       # barrier
    md.add(box(17.4, 22.6, 0, 18.6, 24.2, 3.6, m_flat('steel_lt'), detail=True))
    gr = [(0.0, 0.0, 32.0, 32.0, 'asphalt')]
    md.ground = lambda g, st: g_lot(g, st, gravel=gr, var=seed)
    return md


def pm_office(name, seed=1):
    """1x1 @ (4,4): three-storey white office with window bands and the company sign"""
    md = Model(name)
    pl = 1.5
    x0, x1, y0, y1 = 4.0, 27.0, 4.0, 22.0
    ze = pl + 3 * STOREY + 1.0
    wb = [(pl + k * STOREY + 3.0, pl + k * STOREY + 7.5) for k in range(3)]
    wins = {'y+': [(x0 + 1.5, x1 - 1.5, a, b) for (a, b) in wb[1:]] + [(x0 + 1.5, x0 + 12.0, wb[0][0], wb[0][1])],
            'x+': [(y0 + 1.5, y1 - 1.5, a, b) for (a, b) in wb],
            'x-': [(y0 + 1.5, y1 - 1.5, a, b) for (a, b) in wb], 'y-': [(x0 + 1.5, x1 - 1.5, a, b) for (a, b) in wb]}
    md.add(box(x0, y0, 0, x1, y1, ze, m_block, wall='render_white', plinth=pl, wins=wins, wkind='band',
               frame='panel_dk', roofkey='bitumen', doors={'y+': [(x0 + 15.0, x0 + 18.0, pl, pl + 7.5)]},
               bands={'y+': [(x0 + 1.5, x1 - 1.5, ze - 2.6, ze - 0.4, 'sign_blue', 'text')]}))
    md.add(box(x0 + 13.5, y1, pl + 8.0, x0 + 20.0, y1 + 3.0, pl + 8.8, m_flat('panel_dk', 0.5)))
    add_birch(md, 28.5, 27.0, h=28, seed=seed + 300)
    add_bush(md, 5.0, 27.0, seed=seed + 301)
    gr = [(0.0, 0.0, 32.0, 2.0, 'asphalt'), (x0 + 14.0, y1, x0 + 19.0, 32.0, 'gravel'), (0.0, 0.0, 2.0, 32.0, 'asphalt')]
    md.ground = lambda g, st: g_lot(g, st, gravel=gr, var=seed)
    return md


# =========================================================================== Valtion Metalli
def vm_hall(name, seed=1):
    """3x3 @ (0,0): red-brick north-light sawtooth assembly hall with a roof sign, a brick chimney (hotspot)
    and a two-storey office wing along the front"""
    md = Model(name, 3, 3)
    hx0, hx1, hy0, hy1 = 3.0, 94.0, 6.0, 70.0
    hw = 16.0
    wins = {'y+': [(hx0 + 4 + 9 * i, hx0 + 10 + 9 * i, 4.0, 12.0) for i in range(10)],
            'x+': [(hy0 + 4 + 9 * i, hy0 + 10 + 9 * i, 4.0, 12.0) for i in range(5)],
            'x-': [(hy0 + 4 + 9 * i, hy0 + 10 + 9 * i, 4.0, 12.0) for i in range(7)]}
    doors = {'x+': [(hy0 + 50.0, hy0 + 60.0, 0, 13.0, 'steel')], 'y+': [(80.0, 90.0, 0, 13.0, 'steel')]}
    md.add(box(hx0, hy0, 0, hx1, hy1, hw, m_brickwall, wins=wins, doors=doors, wall='brick'))
    tw = 7.0
    n = int((hx1 - hx0) / tw)
    for i in range(n):
        xa = hx0 + i * tw
        xb = xa + tw
        h = 6.5
        s = h / tw
        pl = [(-1, 0, 0, -xa, 'x-'), (1, 0, 0, xb, 'glass'), (0, -1, 0, -hy0, 'y-'), (0, 1, 0, hy1, 'y+'),
              (0, 0, -1, -hw, 'bot'), (-s, 0, 1, hw - s * xa, 'slope')]
        md.add(Convex(pl, (xa, hy0, hw, xb, hy1, hw + h), m_saw, end='brick'))
    # roof sign on a steel frame along the front edge
    sx0, sx1 = 14.0, 66.0
    md.add(box(sx0, hy1 - 3.0, hw + 6.5, sx1, hy1 - 2.2, hw + 13.0, m_block, wall='sign_red', plinth=-1,
               bands={'y+': [(sx0, sx1, hw + 6.5, hw + 13.0, 'sign_red', 'text')]}, roofkey='sign_red', parapet=0.0))
    for px in (sx0 + 4.0, (sx0 + sx1) / 2.0, sx1 - 4.0):
        md.add(box(px - 0.3, hy1 - 4.5, hw, px + 0.3, hy1 - 3.0, hw + 7.0, m_flat('steel'), detail=True))
    # chimney (boiler at the back corner)
    md.add(box(80.0, 2.0, 0, 92.0, 6.0, 10.0, m_brickwall, wall='brick'))
    md.add(Cone(88.0, 12.0, hw + 6.0, 86.0, 3.4, 2.2, m_cone_brick, cap=True))
    md.hot = (88.0, 12.0, 87.0)
    # office wing in front (two storeys brick, flat roof)
    ow = {'y+': wins_row(4.0, 44.0, 3.5, 8.5, 3.0, 7) + wins_row(4.0, 44.0, 13.5, 18.5, 3.0, 7),
          'x+': wins_row(74.0, 90.0, 3.5, 8.5, 3.0, 2) + wins_row(74.0, 90.0, 13.5, 18.5, 3.0, 2),
          'x-': wins_row(74.0, 90.0, 13.5, 18.5, 3.0, 2)}
    md.add(box(4.0, 74.0, 0, 44.0, 90.0, 2 * STOREY + 2.0, m_block, wall='brick', brickwork=True, plinth=1.5,
               plinthkey='concrete_dk', wins=ow, doors={'y+': [(22.0, 26.0, 1.0, 8.5)]}, roofkey='bitumen',
               frame='trim'))
    add_birch(md, 50.0, 90.0, h=28, seed=seed + 310)
    gr = [(0.0, 0.0, 96.0, 96.0, 'asphalt'), (44.0, 72.0, 96.0, 96.0, 'gravel')]
    md.ground = lambda g, st: g_lot(g, st, gravel=gr, var=seed)
    return md


def vm_yard(name, seed=1):
    """2x2 @ (3,0): open yard under a gantry crane: rows of new tractors, steel stock, a crane cab"""
    md = Model(name, 2, 2)
    # gantry legs along X at y=6 and y=58, girders on top, bridge
    for gy in (5.0, 59.0):
        for gx in (3.0, 61.0):
            md.add(box(gx - 0.9, gy - 0.9, 0, gx + 0.9, gy + 0.9, 26.0, m_flat('crane_grey'), detail=True))
        md.add(box(2.0, gy - 0.9, 24.0, 62.0, gy + 0.9, 26.5, m_flat('crane_grey', 0.2), detail=True))
    md.add(box(30.0, 4.0, 26.5, 34.0, 60.0, 29.0, m_flat('balc_mustard', 0.3)))
    md.add(box(30.6, 26.0, 21.0, 33.4, 30.0, 26.5, m_flat('balc_mustard')))
    md.add(box(31.7, 27.6, 12.0, 32.3, 28.2, 21.0, m_flat('cap'), detail=True, noshadow=True))
    # tractors in rows (bonnets toward +X), muted orange-red with one grey prototype
    for r, ty in enumerate((12.0, 22.0, 32.0)):
        for c, tx in enumerate((8.0, 20.0, 44.0, 56.0)):
            if r == 2 and c > 1:
                continue
            add_tractor(md, tx, ty, 'x', 'steel_lt' if (r, c) == (1, 3) else 'vm_tractor')
    # steel stock + crates
    md.add(box(40.0, 30.0, 0, 60.0, 34.0, 2.5, m_beams, key='steel_lt'))
    md.add(box(40.0, 35.0, 0, 60.0, 38.5, 2.0, m_beams, key='rust'))
    for i in range(3):
        md.add(box(8.0 + i * 7.0, 44.0, 0, 13.0 + i * 7.0, 50.0, 4.5, m_flat('woodpile')))
    md.add(box(36.0, 44.0, 0, 58.0, 52.0, 6.0, m_corr, wall='corr', roofkey='roof_dark'))       # parts shed
    gr = [(0.0, 0.0, 64.0, 64.0, 'concrete'), (0.0, 54.0, 64.0, 64.0, 'asphalt')]
    md.ground = lambda g, st: g_lot(g, st, gravel=gr, var=seed)
    return md


def vm_store(name, seed=1):
    """2x1 @ (3,2): paper-machine rolls on trestles and a ship section, under an overhead crane rail"""
    md = Model(name, 2, 1)
    for rx in (4.0, 17.0, 30.0):
        md.add(box(rx, 4.0, 0, rx + 1.5, 13.0, 3.0, m_flat('fence_wood', -0.2)))
    md.add(cyl_x(3.0, 33.0, 6.5, 6.3, 2.6, m_flat('paper_roll'), n=12))
    md.add(cyl_x(3.0, 33.0, 11.6, 5.6, 2.0, m_flat('steel_lt'), n=12))
    md.add(cyl_x(3.0, 33.0, 21.0, 2.0, 2.0, m_flat('steel'), n=10))
    # ship section (red lead) on blocks
    md.add(box(38.0, 6.0, 0, 41.0, 26.0, 2.0, m_concrete, key='concrete_dk'))
    md.add(box(55.0, 6.0, 0, 58.0, 26.0, 2.0, m_concrete, key='concrete_dk'))
    md.add(box(37.0, 5.0, 2.0, 60.0, 27.0, 13.0, m_concrete, key='redlead', joints=5.5))
    md.add(box(37.0, 5.0, 13.0, 60.0, 27.0, 14.0, m_flat('steel', 0.2)))
    # overhead crane rail on portal posts (front and back)
    for py in (1.6, 30.4):
        for px in (2.0, 32.0, 62.0):
            md.add(box(px - 0.7, py - 0.7, 0, px + 0.7, py + 0.7, 22.0, m_flat('crane_grey'), detail=True))
        md.add(box(1.0, py - 0.8, 20.5, 63.0, py + 0.8, 22.5, m_flat('crane_grey', 0.2), detail=True))
    md.add(box(44.0, 1.0, 22.5, 47.0, 31.0, 24.5, m_flat('balc_mustard', 0.3)))
    gr = [(0.0, 0.0, 64.0, 32.0, 'concrete')]
    md.ground = lambda g, st: g_lot(g, st, gravel=gr, var=seed)
    return md


def g_siding(g, st, seed=1):
    """lot ground with a single track along X on the railway tile's geometry (centre y = 16), so the stub
    continues a rail_10 tile seamlessly at the -X end"""
    return g_rail(g, st, ('-x', '+x'), base=lambda gg, ss: g_lot(gg, ss, gravel=[(0.0, 0.0, 96.0, 32.0, 'gravel')],
                                                                 var=seed))


def vm_siding(name, seed=1):
    """3x1 @ (0,3): track stub along X (open at the -X end toward the railway) with a brown-red goods wagon,
    a concrete loading platform with a canopy behind the track, a buffer stop at the +X end"""
    md = Model(name, 3, 1)
    # loading platform + canopy at the back (toward the hall)
    md.add(box(10.0, 1.0, 0, 84.0, 7.2, 4.0, m_concrete, key='concrete', joints=6.0))
    for px in (14.0, 34.0, 54.0, 74.0):
        md.add(box(px - 0.4, 1.6, 4.0, px + 0.4, 2.4, 12.0, m_flat('steel'), detail=True))
    md.add(mono_body(10.0, 0.6, 84.0, 8.0, 12.0, 12.2, 13.4, m_flat('roof_dark', 0.3), rise='y-'))
    for i in range(4):
        md.add(box(60.0 + i * 4.0, 3.0, 4.0, 63.0 + i * 4.0, 6.0, 7.0, m_flat('woodpile')))
    md.add(box(16.0, 2.5, 4.0, 30.0, 6.5, 6.5, m_beams, key='steel_lt'))
    # goods wagon (boxcar) on two bogies
    wx0, wx1 = 30.0, 58.0
    for bx in (wx0 + 3.0, wx1 - 7.0):
        md.add(box(bx, 12.6, 0.3, bx + 4.0, 19.4, 2.6, m_flat('tyre')))
    md.add(box(wx0, 11.6, 2.6, wx1, 20.4, 3.4, m_flat('cap')))
    md.add(box(wx0 + 0.5, 11.8, 3.4, wx1 - 0.5, 20.2, 12.5, m_wagon))
    md.add(gable_roof(wx0 + 0.5, 11.8, wx1 - 0.5, 20.2, 12.5, 13.8, 'x', m_roof, ov=0.5, ovg=0.3, th=0.8,
                      key='roof_dark', edgekey='roof_dark', seams=False))
    # buffer stop
    md.add(box(90.0, 12.0, 0, 92.0, 20.0, 3.6, m_flat('buffer_red', 0.2)))
    md.add(box(92.0, 13.0, 0, 95.0, 19.0, 2.0, m_flat('sleeper')))
    md.ground = lambda g, st: g_siding(g, st, seed)
    return md


def vm_gate(name, seed=1):
    """2x1 @ (3,3): two-storey brick gate building with a clock turret, sign band, barrier and flagpoles"""
    md = Model(name, 2, 1)
    pl = 1.5
    x0, x1, y0, y1 = 4.0, 42.0, 5.0, 21.0
    ze = pl + 2 * STOREY + 1.0
    wins = {'y+': wins_row(x0, x1, pl + 3.0, pl + 8.0, 3.0, 6, skip=((19.0, 27.0),)) +
            wins_row(x0, x1, pl + 13.0, pl + 18.0, 3.0, 7),
            'x+': wins_row(y0, y1, pl + 3.0, pl + 8.0, 3.0, 2) + wins_row(y0, y1, pl + 13.0, pl + 18.0, 3.0, 2),
            'x-': wins_row(y0, y1, pl + 13.0, pl + 18.0, 3.0, 2), 'y-': wins_row(x0, x1, pl + 13.0, pl + 18.0, 3.0, 7)}
    md.add(box(x0, y0, 0, x1, y1, ze, m_block, wall='brick', brickwork=True, plinth=pl, plinthkey='concrete_dk',
               wins=wins, doors={'y+': [(20.0, 26.0, 0.5, 9.0)]}, roofkey='bitumen', frame='trim',
               bands={'y+': [(x0 + 2.0, x1 - 2.0, pl + 9.4, pl + 12.0, 'sign_red', 'text')]}))
    bands = {}
    add_clock(bands, 'y+', 21.5, ze + 4.0, 3.0)
    add_clock(bands, 'x+', 9.5, ze + 4.0, 3.0)
    md.add(box(20.0, 8.0, ze, 26.0, 14.0, ze + 10.0, m_block, wall='brick', plinth=-1, bands=bands,
               roofkey='bitumen', parapet=0.0))
    md.add(hip_body(19.4, 7.4, 26.6, 14.6, ze + 10.0, ze + 10.0, ze + 14.0, m_hiproof, wallmat=m_block,
                    roofkey='copper', wall='brick', plinth=-1))
    md.add(Cone(23.0, 11.0, ze + 14.0, ze + 18.0, 0.25, 0.25, m_flat('steel_lt'), detail=True))
    md.add(box(44.0, 24.0, 2.6, 60.0, 24.8, 3.4, m_flat('sign_red', 0.3), detail=True))         # barrier
    md.add(box(43.4, 23.6, 0, 44.6, 25.2, 3.6, m_flat('steel_lt'), detail=True))
    md.add(box(46.0, 6.0, 0, 52.0, 12.0, 8.0, m_block, wall='render_grey', plinth=0.8, roofkey='bitumen',
               wins={'y+': [(47.0, 51.0, 3.0, 6.5)], 'x+': [(7.0, 11.0, 3.0, 6.5)]}))           # porter's booth
    add_flagpole(md, 6.0, 27.0, 30.0, flag=True)
    add_flagpole(md, 12.0, 27.0, 30.0)
    gr = [(0.0, 0.0, 64.0, 32.0, 'asphalt'), (0.0, 21.0, 42.0, 32.0, 'gravel')]
    md.ground = lambda g, st: g_lot(g, st, gravel=gr, var=seed)
    return md


# =========================================================================== patron gifts
def row_houses(name, wall='w_red', roof='roof_metal', seed=1):
    """3x1: wooden workers' row house (tyovaen asunnot), 5 units, low gable tin roof, white trim, a porch
    on BOTH long sides per unit (front doors / back doors), chimneys, woodsheds at both ends, fenced
    gardens on both sides: semantically 180-degree symmetric (rots 2)"""
    md = Model(name, 3, 1)
    pl = 1.5
    x0, x1, y0, y1 = 10.0, 86.0, 9.5, 22.5
    ze = pl + 9.0
    zr = ze + 6.5 * KZ * 0.75
    units = 5
    uw = (x1 - x0) / units
    wins = {'y+': [], 'y-': [], 'x+': [(y0 + 2.5, y0 + 5.5, pl + 2.5, pl + 7.0), (y1 - 5.5, y1 - 2.5, pl + 2.5, pl + 7.0),
                                    (14.5, 17.5, ze + 0.8, ze + 4.5)],
            'x-': [(y0 + 2.5, y0 + 5.5, pl + 2.5, pl + 7.0), (y1 - 5.5, y1 - 2.5, pl + 2.5, pl + 7.0),
                   (14.5, 17.5, ze + 0.8, ze + 4.5)]}
    doors = {'y+': [], 'y-': []}
    for u in range(units):
        a = x0 + u * uw
        wins['y+'] += [(a + 2.0, a + 5.0, pl + 2.5, pl + 7.0), (a + 9.5, a + 12.5, pl + 2.5, pl + 7.0)]
        wins['y-'] += [(a + uw - 5.0, a + uw - 2.0, pl + 2.5, pl + 7.0), (a + uw - 12.5, a + uw - 9.5, pl + 2.5, pl + 7.0)]
        doors['y+'].append((a + 6.0, a + 8.5, pl, pl + 7.5))
        doors['y-'].append((a + uw - 8.5, a + uw - 6.0, pl, pl + 7.5))
    md.add(gable_body(x0, y0, x1, y1, 0, ze, zr, 'x', m_house, wall=wall, plinth=pl, wins=wins, doors=doors,
                      clad='h', eave=ze, door='door'))
    md.add(gable_roof(x0, y0, x1, y1, ze, zr, 'x', m_roof, ov=1.3, ovg=1.3, th=1.3, key=roof, edgekey='trim', seam=3.0))
    mid = (y0 + y1) / 2.0
    for u in range(0, units + 1, 2):
        cxx = x0 + u * uw if 0 < u < units else (x0 + 4.0 if u == 0 else x1 - 4.0)
        md.add(box(cxx - 1.2, mid - 1.2, ze, cxx + 1.2, mid + 1.2, zr + 3.0, m_chimney, key='brick'))
    for u in range(units):                       # porch canopies + steps both sides
        a = x0 + u * uw
        md.add(box(a + 5.4, y1, pl + 7.6, a + 9.1, y1 + 2.2, pl + 8.3, m_flat('trim', 0.2)))
        md.add(box(a + 5.6, y1, 0, a + 8.9, y1 + 1.6, pl, m_flat('plinth', 0.3)))
        b = a + uw - 7.25
        md.add(box(b - 1.85, y0 - 2.2, pl + 7.6, b + 1.85, y0, pl + 8.3, m_flat('trim', 0.2)))
        md.add(box(b - 1.65, y0 - 1.6, 0, b + 1.65, y0, pl, m_flat('plinth', 0.3)))
    # woodsheds at both ends (mirror images)
    for (sx0, sx1) in ((2.0, 8.0), (88.0, 94.0)):
        md.add(box(sx0, 11.0, 0, sx1, 21.0, 6.0, m_house, wall='w_weath', plinth=0.4, clad='v', frieze=False,
                   corners=False, doors={'x+' if sx0 < 50 else 'x-': [(13.0, 16.0, 0.4, 5.0)]}))
        md.add(gable_roof(sx0, 11.0, sx1, 21.0, 6.0, 8.0, 'x', m_roof, ov=0.5, ovg=0.5, th=0.8, key='roof_felt',
                          edgekey='trim', seam=3.0))
    # garden fences along both long edges with a gate per unit, apple trees / rowans both sides
    gaps_f = tuple((x0 + u * uw + 5.0 - 1.0, x0 + u * uw + 9.5 - 1.0) for u in range(units))
    gaps_b = tuple((x1 - x0 - (g[1]) + 9.0 - 1.0 + 1.0, x1 - x0 - g[0] + 9.0 - 1.0 + 1.0) for g in gaps_f)
    add_fence(md, 1.0, 30.6, 95.0, 30.6, gaps=gaps_f, key='fence', h=3.0, step=2.0)
    add_fence(md, 1.0, 1.4, 95.0, 1.4, gaps=gaps_b, key='fence', h=3.0, step=2.0)
    trees = [(24.0, 27.0, 'apple'), (62.0, 27.5, 'rowan'), (72.0, 5.0, 'apple'), (34.0, 4.5, 'rowan')]
    for i, (tx, ty, k) in enumerate(trees):
        (add_apple if k == 'apple' else add_rowan)(md, tx, ty, seed=seed + 320 + i)
    add_bush(md, 46.0, 28.0, seed=seed + 330, size=0.8)
    add_bush(md, 50.0, 4.0, seed=seed + 331, size=0.8)
    gr = []
    for u in range(units):
        a = x0 + u * uw
        gr.append((a + 6.0, y1, a + 8.5, 32.0, 'gravel'))
        gr.append((a + uw - 8.5, 0.0, a + uw - 6.0, y0, 'gravel'))
    gr += [(x0 + 3.0, y1 + 3.0, x0 + 12.0, y1 + 7.5, 'potato'), (x1 - 12.0, y0 - 7.5, x1 - 3.0, y0 - 3.0, 'potato')]
    md.ground = lambda g, st: g_lot(g, st, gravel=gr, var=seed)
    return md


def church(name, seed=1):
    """2x2: white modernist church (Aalto / Ruusuvuori): a nave with a mono-pitch roof rising to the altar
    end, tall slit windows, a low parish wing, a slender freestanding white bell tower with a cross, birches
    and a low fieldstone wall with gateways"""
    md = Model(name, 2, 2)
    pl = 1.5
    # nave along Y, roof rising toward -Y (altar end at the back)
    nx0, nx1, ny0, ny1 = 16.0, 38.0, 10.0, 46.0
    slits = [(ny0 + 4.0 + 4.5 * i, ny0 + 5.4 + 4.5 * i, 6.0, 22.0) for i in range(7)]
    md.add(mono_body(nx0, ny0, nx1, ny1, 0, 22.0, 34.0, m_block, rise='y-', wall='render_white', plinth=pl,
                     wins={'x+': slits, 'x-': slits, 'y+': [(24.0, 30.0, 14.0, 20.0)]},
                     doors={'y+': [(24.5, 29.5, pl, pl + 10.0, 'door')]}, roofkey='roof_metal', frame='render_grey'))
    md.add(mono_body(nx0 - 0.8, ny0 - 0.8, nx1 + 0.8, ny1 + 0.8, 21.6, 22.6, 35.0, m_flat('copper', 0.2), rise='y-'))
    # low parish wing (flat roof) along X at the front right
    md.add(box(38.0, 30.0, 0, 56.0, 46.0, 9.0, m_block, wall='render_white', plinth=pl, roofkey='bitumen',
               wins={'y+': wins_row(38.0, 56.0, 3.5, 7.0, 3.5, 3), 'x+': wins_row(30.0, 46.0, 3.5, 7.0, 3.5, 2)},
               frame='render_grey'))
    # entrance canopy
    md.add(box(22.0, ny1, pl + 10.0, 32.0, ny1 + 4.0, pl + 10.8, m_flat('render_white', 0.4)))
    # freestanding bell tower (front left)
    tx0, ty0 = 6.0, 40.0
    md.add(box(tx0, ty0, 0, tx0 + 6.0, ty0 + 6.0, 74.0, m_block, wall='render_white', plinth=1.0, roofkey='bitumen',
               wins={'y+': [(tx0 + 1.5, tx0 + 4.5, 62.0, 71.0)], 'x+': [(ty0 + 1.5, ty0 + 4.5, 62.0, 71.0)],
                     'x-': [(ty0 + 1.5, ty0 + 4.5, 62.0, 71.0)], 'y-': [(tx0 + 1.5, tx0 + 4.5, 62.0, 71.0)]},
               frame='render_white', parapet=0.6))
    md.add(box(tx0 + 2.6, ty0 + 2.6, 74.0, tx0 + 3.4, ty0 + 3.4, 84.0, m_flat('cap'), detail=True))
    md.add(box(tx0 + 0.9, ty0 + 2.6, 79.5, tx0 + 5.1, ty0 + 3.4, 80.4, m_flat('cap'), detail=True))
    # fieldstone wall around the churchyard with gateways (front and right side)
    for (ax, ay, bx, by) in ((2.0, 2.0, 62.0, 3.6), (2.0, 2.0, 3.6, 62.0)):
        md.add(box(ax, ay, 0, bx, by, 2.6, m_stonewall))
    md.add(box(2.0, 60.4, 0, 22.0, 62.0, 2.6, m_stonewall))
    md.add(box(32.0, 60.4, 0, 62.0, 62.0, 2.6, m_stonewall))
    md.add(box(60.4, 2.0, 0, 62.0, 26.0, 2.6, m_stonewall))
    md.add(box(60.4, 36.0, 0, 62.0, 62.0, 2.6, m_stonewall))
    for i, (bx, by) in enumerate(((8.0, 9.0), (52.0, 9.0), (54.0, 55.0), (12.0, 55.0), (46.0, 22.0))):
        add_birch(md, bx, by, h=30 + 4 * (i % 2), seed=seed + 340 + i)
    gr = [(22.0, ny1, 32.0, 64.0, 'gravel'), (32.0, 46.0, 64.0, 36.0 + 14.0, 'gravel'), (56.0, 26.0, 64.0, 36.0, 'gravel'),
          (4.0, 4.0, 60.0, 60.0, 'lawn')]
    md.ground = lambda g, st: g_lot(g, st, gravel=gr, var=seed)
    return md


# =========================================================================== landmarks
def concert_hall(name, seed=1):
    """2x2: Finlandia-like concert hall: long white marble block with dark strip windows, a tall slanted
    fly tower (mono-pitch) over the auditorium, a glazed foyer at the base, a paved plaza with birches"""
    md = Model(name, 2, 2)
    bx0, bx1, by0, by1 = 4.0, 60.0, 10.0, 36.0
    strips = {'y+': [(bx0 + 3.0, bx0 + 36.0, 13.0, 15.0)], 'x+': [(by0 + 3.0, by1 - 3.0, 13.0, 15.0)],
              'x-': [(by0 + 3.0, by1 - 3.0, 13.0, 15.0)]}
    md.add(box(bx0, by0, 6.0, bx1, by1, 20.0, m_marble, wins=strips))
    md.add(box(bx0 + 1.5, by0 + 1.5, 0, bx1 - 1.5, by1 - 1.5, 6.0, m_glassband))           # foyer glazing
    # fly tower: slanted top rising toward +X, marble
    md.add(mono_body(28.0, by0 + 3.0, 52.0, by1 - 3.0, 20.0, 30.0, 46.0, m_marble, rise='x+'))
    # low wing toward the plaza (congress wing)
    md.add(box(6.0, 36.0, 0, 30.0, 48.0, 12.0, m_marble,
               wins={'y+': [(8.0, 28.0, 6.0, 8.0)], 'x+': [(37.0, 47.0, 6.0, 8.0)]}))
    md.add(box(6.0, 48.0, 0, 30.0, 52.0, 3.0, m_glassband))
    for i, (tx, ty) in enumerate(((40.0, 46.0), (52.0, 54.0), (40.0, 58.0), (56.0, 42.0))):
        add_birch(md, tx, ty, h=28 + 3 * (i % 2), seed=seed + 350 + i)
    gr = [(0.0, 0.0, 64.0, 64.0, 'concrete'), (34.0, 40.0, 62.0, 62.0, 'lawn')]
    md.ground = lambda g, st: g_lot(g, st, gravel=gr, var=seed)
    return md


def tv_tower(name, seed=1):
    """1x1: concrete TV / observation tower (Nasinneula-like): tapering shaft, a glazed observation disc,
    an upper antenna mast with red-white bands; a round entrance pavilion at the foot"""
    md = Model(name)
    c = 16.0
    md.add(Cone(c, c, 0, 6.0, 9.0, 9.0, m_concrete_round, cap=True, key='render_white', capkey='concrete',
                wins=[(1.5, 4.5, 10)]))
    md.add(Cone(c, c, 0, 150.0, 3.4, 2.4, m_concrete_round, cap=False, key='concrete',
                wins=[(40.0, 41.5, 4), (80.0, 81.5, 4), (120.0, 121.5, 4)]))
    md.add(Cone(c, c, 148.0, 152.0, 2.4, 8.5, m_concrete_round, cap=False, key='concrete'))
    md.add(Cone(c, c, 152.0, 158.0, 8.5, 8.5, m_concrete_round, cap=False, key='render_white',
                band=(153.2, 157.0, 'glass')))
    md.add(Cone(c, c, 158.0, 161.0, 8.8, 6.0, m_concrete_round, cap=True, key='render_grey', capkey='render_grey'))
    md.add(Cone(c, c, 161.0, 176.0, 1.6, 1.2, m_concrete_round, cap=True, key='render_grey'))
    for k in range(4):
        z0 = 176.0 + k * 6.0
        md.add(Cone(c, c, z0, z0 + 6.0, 0.7, 0.6, m_flat('buffer_red' if k % 2 == 0 else 'trim'), cap=True,
                    detail=True))
    add_spruce(md, 4.0, 27.0, h=26, seed=seed + 360, r=4.0)
    add_birch(md, 28.0, 4.5, h=24, seed=seed + 361)
    gr = [(c - 2.0, c, c + 2.0, 32.0, 'gravel'), (4.0, 4.0, 28.0, 28.0, 'concrete')]
    md.ground = lambda g, st: g_lot(g, st, gravel=gr, var=seed)
    return md


def ski_jump(name, seed=1):
    """3x1 (fixed orientation): lattice in-run tower at the -X end, the in-run ramp descending along +X on
    trestles to the take-off, a landing hill with summer plastic matting, a judges' tower, a flat outrun"""
    md = Model(name, 3, 1)
    yc0, yc1 = 12.0, 20.0
    # tower
    for (lx, ly) in ((2.0, 10.0), (9.0, 10.0), (2.0, 21.0), (9.0, 21.0)):
        md.add(box(lx, ly, 0, lx + 1.0, ly + 1.0, 64.0, m_flat('steel'), detail=True))
    for z in range(6, 64, 9):
        md.add(box(2.0, 10.0, z, 10.0, 10.8, z + 1.0, m_flat('steel_lt', 0.2), detail=True))
        md.add(box(9.2, 10.0, z, 10.0, 22.0, z + 1.0, m_flat('steel_lt', 0.2), detail=True))
        md.add(box(2.0, 21.2, z, 10.0, 22.0, z + 1.0, m_flat('steel_lt', 0.2), detail=True))
    md.add(box(4.5, 14.5, 0, 6.5, 17.5, 64.0, m_flat('steel', -0.3), detail=True))      # lift shaft
    md.add(box(1.0, 9.0, 64.0, 11.0, 23.0, 70.0, m_block, wall='render_white', plinth=-1, roofkey='roof_dark',
               wins={'y+': [(2.5, 9.5, 65.5, 68.5)], 'x+': [(10.5, 21.5, 65.5, 68.5)]}, parapet=0.4))
    # in-run ramp 70 -> 22 px over x 10 .. 40, then take-off
    md.add(ramp_x(10.0, 40.0, yc0, yc1, 68.0, 24.0, 2.0, m_flat('fence_wood', 0.3)))
    md.add(ramp_x(10.0, 40.0, yc0 - 0.6, yc0, 68.0, 24.0, 3.5, m_flat('trim'), detail=True))
    md.add(ramp_x(10.0, 40.0, yc1, yc1 + 0.6, 68.0, 24.0, 3.5, m_flat('trim'), detail=True))
    for px in (16.0, 24.0, 32.0, 39.0):
        z = 68.0 + (px - 10.0) * (24.0 - 68.0) / 30.0
        md.add(box(px - 0.6, yc0 + 0.5, 0, px + 0.6, yc0 + 1.5, z, m_flat('steel'), detail=True))
        md.add(box(px - 0.6, yc1 - 1.5, 0, px + 0.6, yc1 - 0.5, z, m_flat('steel'), detail=True))
    # landing hill: earth mound 20 px at x=40 falling to 0 at x=88 (summer matting stripe)
    def hill(x, y):
        if x < 40.0:
            return 22.0 if x > 36.0 else 0.0
        u = min(1.0, (x - 40.0) / 48.0)
        return max(0.0, 20.0 * (1.0 - u) ** 1.6) * min(1.0, max(0.0, (min(y, 32.0 - y) - 1.0) / 4.0))

    def m_hill(g, st):
        y = g.P[1]
        if yc0 - 1.0 <= y < yc1 + 1.0:
            return ('mat_green', 0.2 if int(g.P[0] / 2.0) % 2 else -0.1)
        return 'grass', (vnoise(g.W[0], g.W[1], 3.0, 77) - 0.5) * 0.6
    md.add(HeightField(36.0, 1.0, 92.0, 31.0, 22.0, hill, m_hill))
    # judges' tower beside the landing hill
    md.add(box(52.0, 24.0, 0, 58.0, 30.0, 26.0, m_block, wall='render_white', plinth=1.0, roofkey='bitumen',
               wins={'x-': [(25.0, 29.0, 20.0, 24.0)], 'y+': [(53.0, 57.0, 20.0, 24.0)]}))
    add_flagpole(md, 92.0, 4.0, 22.0, flag=True)
    add_flagpole(md, 92.0, 28.0, 22.0, flag=True)
    add_spruce(md, 30.0, 4.0, h=30, seed=seed + 370, r=4.0)
    add_spruce(md, 22.0, 28.0, h=26, seed=seed + 371, r=3.8)
    gr = [(0.0, 0.0, 96.0, 32.0, 'grass'), (86.0, 6.0, 96.0, 26.0, 'gravel'), (0.0, 8.0, 12.0, 24.0, 'gravel')]
    md.ground = lambda g, st: g_lot(g, st, gravel=gr, var=seed)
    return md


def pres_sauna(name, seed=1):
    """2x2: the presidential sauna: a dark log villa with a deep veranda, a separate smoke sauna (no
    chimney, sooty), a boardwalk deck to the 'shore' corner, flagpole with flag, granite and birches"""
    md = Model(name, 2, 2)
    pl = 2.0
    x0, x1, y0, y1 = 10.0, 40.0, 8.0, 26.0
    ze = pl + 9.0
    zr = ze + 9.0 * KZ * 0.85
    wins = {'y+': [(x0 + 3.0, x0 + 7.0, pl + 2.5, pl + 7.0), (x0 + 12.0, x0 + 18.0, pl + 2.5, pl + 7.0),
                   (x1 - 7.0, x1 - 3.0, pl + 2.5, pl + 7.0)],
            'x+': [(y0 + 3.0, y0 + 7.0, pl + 2.5, pl + 7.0), (y1 - 7.0, y1 - 3.0, pl + 2.5, pl + 7.0)],
            'x-': [(y0 + 6.0, y0 + 10.0, pl + 2.5, pl + 7.0)], 'y-': wins_row(x0, x1, pl + 2.5, pl + 7.0, 4.0, 4)}
    md.add(gable_body(x0, y0, x1, y1, 0, ze, zr, 'x', m_house, wall='log_dark', plinth=pl, plinthkey='rock',
                      wins=wins, doors={'y+': [(x0 + 20.0, x0 + 23.0, pl, pl + 7.5)]}, clad='h', eave=ze,
                      frame='trim'))
    md.add(gable_roof(x0, y0, x1, y1, ze, zr, 'x', m_roof, ov=1.6, ovg=1.6, th=1.4, key='roof_felt', edgekey='trim',
                      seam=3.0))
    md.add(box(x0 + 6.0, 15.5, ze, x0 + 8.6, 18.1, zr + 3.0, m_chimney, key='rock'))
    # veranda with railing + its own low roof
    md.add(box(x0 - 1.0, y1, 0, x1 + 1.0, y1 + 7.0, pl, m_flat('fence_wood', 0.3)))
    add_fence(md, x0 - 0.5, y1 + 6.6, x1 + 0.5, y1 + 6.6, gaps=((19.0, 22.0),), key='trim', h=pl + 3.5, step=1.6)
    md.add(mono_body(x0 - 1.0, y1 - 0.5, x1 + 1.0, y1 + 7.4, pl + 7.6, pl + 8.0, ze - 0.4, m_flat('roof_felt', 0.2),
                     rise='y-'))
    for px in (x0, x1):
        md.add(box(px - 0.4, y1 + 6.4, pl, px + 0.4, y1 + 7.2, pl + 7.8, m_flat('trim'), detail=True))
    # smoke sauna (savusauna): small sooty log cabin at the 'shore' corner, boardwalk
    sx0, sy0, sx1, sy1 = 44.0, 40.0, 56.0, 50.0
    md.add(gable_body(sx0, sy0, sx1, sy1, 0, 7.0, 7.0 + 5.0 * KZ * 0.85, 'y', m_house, wall='log_dark', plinth=1.0,
                      plinthkey='rock', clad='h', corners=False, frieze=False,
                      doors={'x-': [(sy0 + 3.5, sy0 + 6.0, 1.0, 6.5)]}, wins={'y+': [(sx0 + 4.5, sx0 + 7.0, 4.0, 5.6)]}))
    md.add(gable_roof(sx0, sy0, sx1, sy1, 7.0, 7.0 + 5.0 * KZ * 0.85, 'y', m_roof, ov=0.8, ovg=0.8, th=1.0,
                      key='cap', edgekey='log_dark', seam=3.0))
    md.add(box(30.0, 44.0, 0, 44.0, 47.0, 1.2, m_flat('fence_wood', 0.3)))                      # boardwalk
    md.add(box(48.0, 50.0, 0, 52.0, 62.0, 1.2, m_flat('fence_wood', 0.3)))                      # jetty deck
    md.add(box(44.0, 58.0, 0, 62.0, 62.5, 1.2, m_flat('fence_wood', 0.3)))
    # flagpole (blue-cross flag), granite, birches
    add_flagpole(md, 6.0, 40.0, 34.0, flag=True)
    md.add(HeightField(14.0, 40.0, 30.0, 56.0, 4.0,
                       lambda x, y: 4.0 * max(0.0, 1.0 - ((x - 21.0) / 7.0) ** 2 - ((y - 48.0) / 7.0) ** 2) ** 0.6,
                       m_rock2))
    for i, (bx, by) in enumerate(((5.0, 5.0), (58.0, 10.0), (56.0, 30.0), (36.0, 56.0), (8.0, 58.0))):
        add_birch(md, bx, by, h=30 + 4 * (i % 2), seed=seed + 380 + i)
    gr = [(x0 + 19.0, y1 + 7.0, x0 + 22.0, 64.0, 'gravel'), (44.0, 50.0, 64.0, 64.0, 'water')]
    md.ground = lambda g, st: g_lot(g, st, gravel=gr, var=seed)
    return md


def friendship_statue(name, seed=1):
    """1x1: friendship monument: a polished granite plinth with a bronze two-figure group (hands joined),
    a round flowerbed and benches"""
    md = Model(name)
    c = 16.0
    md.add(box(c - 4.5, c - 4.5, 0, c + 4.5, c + 4.5, 1.6, m_concrete, key='concrete'))
    md.add(box(c - 3.2, c - 3.2, 1.6, c + 3.2, c + 3.2, 9.0, m_flat('rock_dk', 0.2)))
    for (fx, fy) in ((c - 1.3, c + 0.2), (c + 1.3, c - 0.2)):
        md.add(Cone(fx, fy, 9.0, 13.5, 1.1, 0.9, m_flat('bronze'), cap=True))            # legs / coat
        md.add(Cone(fx, fy, 13.5, 18.0, 1.2, 0.9, m_flat('bronze', 0.2), cap=True))      # torso
        md.add(Ellipsoid(fx, fy, 19.2, 0.8, 0.8, 1.1, m_flat('bronze', 0.4)))            # head
    md.add(box(c - 1.0, c - 0.6, 16.5, c + 1.0, c + 0.6, 17.4, m_flat('bronze', 0.2)))   # joined hands
    add_bench(md, c, 27.0)
    add_bench(md, 27.0, c, axis='y')
    add_birch(md, 4.5, 4.5, h=28, seed=seed + 390)
    add_bush(md, 27.0, 5.0, seed=seed + 391, key='lilac')
    add_bush(md, 5.0, 27.0, seed=seed + 392, key='lilac')
    gr = [(c - 9.0, c - 9.0, c + 9.0, c + 9.0, 'flowerbed'), (c - 6.0, c - 6.0, c + 6.0, c + 6.0, 'gravel'),
          (c - 2.0, c + 9.0, c + 2.0, 32.0, 'gravel'), (c + 9.0, c - 2.0, 32.0, c + 2.0, 'gravel'),
          (c - 2.0, 0.0, c + 2.0, c - 9.0, 'gravel'), (0.0, c - 2.0, c - 9.0, c + 2.0, 'gravel')]
    md.ground = lambda g, st: g_lot(g, st, gravel=gr, var=seed)
    return md


def patron_villa(name, seed=1):
    """2x2: the patron's villa (patruunan kartano, not open to the public): a two-storey pale yellow manor
    with white corners, a hipped tin roof and a columned portico, a gravel carriage circle with a
    flowerbed, a cinder tennis court (senior officials only), an iron fence with a gate, old birches"""
    md = Model(name, 2, 2)
    pl = 2.0
    x0, x1, y0, y1 = 12.0, 48.0, 8.0, 28.0
    ze = pl + 2 * STOREY + 1.5
    wrow = lambda lo, hi, n: wins_row(lo, hi, pl + 3.0, pl + 8.5, 3.0, n) + wins_row(lo, hi, pl + 13.0, pl + 18.5, 3.0, n)  # noqa
    wins = {'y+': [r for r in wrow(x0, x1, 7) if not (26.5 < (r[0] + r[1]) / 2 < 33.5 and r[2] < 10)],
            'y-': wrow(x0, x1, 7), 'x+': wrow(y0, y1, 3), 'x-': wrow(y0, y1, 3)}
    md.add(hip_body(x0, y0, x1, y1, 0, ze, ze + 8.0, m_hiproof, wallmat=m_block, wall='render_yel', plinth=pl,
                    plinthkey='rock', wins=wins, doors={'y+': [(28.5, 31.5, pl, pl + 8.5)]}, roofkey='roof_metal',
                    corner='trim', frame='trim', inset=8.0))
    for cx in (21.0, 33.0):
        md.add(box(cx, 17.0, ze + 2.0, cx + 2.4, 19.4, ze + 11.0, m_chimney, key='render_yel'))
    # portico: four columns, entablature, low pediment
    for cx in (25.0, 28.0, 32.0, 35.0):
        md.add(Cone(cx, y1 + 3.2, pl, pl + 10.0, 0.75, 0.65, m_flat('trim'), cap=True, detail=True))
    md.add(box(23.8, y1, pl + 10.0, 36.2, y1 + 4.4, pl + 11.6, m_flat('trim', 0.2)))
    md.add(gable_body(23.8, y1, 36.2, y1 + 4.4, pl + 11.6, pl + 11.6, pl + 15.0, 'y', m_block, wall='trim', plinth=-1,
                      roofkey='roof_metal'))
    md.add(box(23.0, y1, 0, 37.0, y1 + 5.5, pl, m_flat('plinth', 0.3)))                      # steps
    # tennis court (cinder) with a low fence, at the back left
    md.add(box(2.0, 34.0, 0, 2.5, 60.0, 3.0, m_flat('steel_lt'), detail=True, noshadow=True))
    md.add(box(2.0, 34.0, 0, 22.0, 34.5, 3.0, m_flat('steel_lt'), detail=True, noshadow=True))
    md.add(box(11.7, 36.0, 0, 12.3, 58.0, 1.4, m_flat('trim'), detail=True, noshadow=True))     # net
    # iron fence along the front and right edges with a gate
    add_fence(md, 1.0, 62.6, 63.0, 62.6, gaps=((38.0, 46.0),), key='cap', h=4.0, step=1.4)
    add_fence(md, 62.6, 1.0, 62.6, 63.0, key='cap', h=4.0, step=1.4)
    for i, (bx, by) in enumerate(((6.0, 6.0), (56.0, 8.0), (56.0, 30.0), (30.0, 57.0), (54.0, 56.0))):
        add_birch(md, bx, by, h=34 + 3 * (i % 2), seed=seed + 395 + i)
    add_bush(md, 8.0, 28.0, seed=seed + 401, key='lilac')
    add_bush(md, 50.0, 40.0, seed=seed + 402, key='lilac')
    gr = [(3.0, 35.0, 21.5, 59.5, 'court_cinder'), (36.0, 38.0, 46.0, 48.0, 'flowerbed'), (32.0, 33.0, 50.0, 53.0, 'gravel'),
          (38.0, 53.0, 46.0, 64.0, 'gravel'), (23.0, 28.0, 37.0, 38.0, 'gravel'), (2.0, 2.0, 62.0, 62.0, 'lawn')]
    md.ground = lambda g, st: g_lot(g, st, gravel=gr, var=seed)
    return md


# =========================================================================== table
ENTRIES = [
    ('pm_logyard', (2, 2), 1, [_v(pm_logyard, 'pml')], None),
    ('pm_hall', (3, 3), 1, [_v(pm_hall, 'pmh')], None),
    ('pm_chipper', (2, 1), 1, [_v(pm_chipper, 'pmc')], None),
    ('pm_digester', (2, 2), 1, [_v(pm_digester, 'pmd')], None),
    ('pm_gate', (1, 1), 1, [_v(pm_gate, 'pmg')], None),
    ('pm_office', (1, 1), 1, [_v(pm_office, 'pmo')], None),
    ('vm_hall', (3, 3), 1, [_v(vm_hall, 'vmh')], None),
    ('vm_yard', (2, 2), 1, [_v(vm_yard, 'vmy')], None),
    ('vm_store', (2, 1), 1, [_v(vm_store, 'vms')], None),
    ('vm_siding', (3, 1), 1, [_v(vm_siding, 'vmsd')], None),
    ('vm_gate', (2, 1), 1, [_v(vm_gate, 'vmg')], None),
    ('row_houses', (3, 1), 2, [_v(row_houses, 'rh', wall='w_red', seed=1),
                               _v(row_houses, 'rh', wall='w_yellow', roof='roof_red', seed=2)], None),
    ('church', (2, 2), 1, [_v(church, 'ch')], None),
    ('concert_hall', (2, 2), 1, [_v(concert_hall, 'cc')], None),
    ('tv_tower', (1, 1), 1, [_v(tv_tower, 'tv')], None),
    ('ski_jump', (3, 1), 1, [_v(ski_jump, 'sj')], None),
    ('pres_sauna', (2, 2), 1, [_v(pres_sauna, 'ps')], None),
    ('patron_villa', (2, 2), 1, [_v(patron_villa, 'pv')], None),
    ('friendship_statue', (1, 1), 1, [_v(friendship_statue, 'fst')], None),
]
KEY_ALIASES = [
    ('pm_boiler', [('heat_plant', 0)], 0),     # paper mill boiler house = district heat plant v0 at r0 (fork B)
]
GROUPS = {}
for _e in ENTRIES:
    _k = _e[0]
    GROUPS[_k] = ('5 paper mill' if _k.startswith('pm_') else '5 Valtion Metalli' if _k.startswith('vm_') else
                  '5 patron gifts' if _k in ('row_houses', 'church', 'patron_villa') else '6 landmarks')
