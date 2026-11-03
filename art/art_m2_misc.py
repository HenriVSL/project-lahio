"""art_m2_misc.py - Milestone-2 world art (task M4, group D): transport (railway station, bus stop, bus depot,
the bus), the rural edge (farmsteads T1-T3, logging camp, forest regrowth stages, hay field) and fire frames.
See gen_art.py "Milestone-2 modules" and ART.md "Milestone-2 keys"."""
from PIL import Image

from art_m2_common import *  # noqa: F401,F403
from art_m2_common import _v, add_colours  # noqa: F401
from artlib import KEY, to565, hexc, edge_window
import art_terrain as TR

add_colours({
    'bus_cream': '#d8cfb2', 'bus_red': '#a5432f', 'bus_roof': '#c9c2aa',
    'barn_red': '#7c3f33', 'log_wall': '#76604b', 'hay': '#b9a466', 'hay_dk': '#9a8752',
    'tractor_or': '#b56a3c', 'horse': '#6a4936', 'slash': '#6e6048', 'stump_top': '#bfa477',
})

GRP_T = '3 services (transport)'
GRP_R = '4 forestry + farms'
GRP_F = '7 fire + bus'


# =========================================================================== the bus
def m_bus(g, st):
    """generic 1960s rear-engined bus (heading +X in its own frame): cream body, red skirt band, window
    band with pillars, windscreen + destination board at the front, door on the right (+Y) side"""
    p = g.solid.par
    P = g.P
    tg = g.tag
    cx, cy = p['cx'], p['cy']
    z = P[2]
    big = st.zoom >= 1
    if tg == 'top':
        return 'bus_roof', 0.4
    if tg == 'bot':
        return 'tyre', 0.0
    if z < 2.0:
        return 'bus_red', -1.0
    if tg in ('y+', 'y-'):
        u = P[0] - cx                         # -12 (rear) .. +12 (front)
        if tg == 'y+' and 7.4 <= u < 10.2 and z < 9.2:          # door (front right)
            if u < 7.9 or u >= 9.7 or z >= 8.7:
                return 'bus_cream', -0.4
            return 'glass', (0.0 if z > 4.2 else -0.3)
        if 5.6 <= z < 9.0 and -11.2 <= u < 11.3:
            if big and ((u + 11.2) % 3.3) < 0.7:
                return 'bus_cream', -0.2
            return ('glass_lt' if big and z > 8.2 else 'glass'), 0.0
        if 2.2 <= z < 4.6:
            return 'bus_red', 0.0
        if big and 9.4 <= z < 9.9:
            return 'bus_red', 0.2
        return 'bus_cream', 0.0
    if tg == 'x+':                            # front
        v = abs(P[1] - cy)
        if 9.2 <= z < 10.3 and v < 2.6:
            return ('sign_txt' if big and v < 1.8 and z < 10.0 else 'cap'), 0.0
        if 4.8 <= z < 9.1 and v < 3.3:
            return ('glass_lt' if z > 7.6 else 'glass'), 0.0
        if 2.4 <= z < 3.5 and v > 2.0:
            return 'headlight', 0.5
        if 2.0 <= z < 4.6:
            return 'bus_red', 0.0
        return 'bus_cream', 0.0
    if tg == 'x-':                            # rear: engine grille, small rear window
        v = abs(P[1] - cy)
        if 6.6 <= z < 8.8 and v < 2.4:
            return 'glass', -0.2
        if 2.2 <= z < 4.4:
            if v < 2.6 and big and int(z * 2) % 2 == 0:
                return 'cap', 0.0
            return ('taillight' if v > 3.0 else 'bus_red'), 0.0
        return 'bus_cream', 0.0
    return 'bus_cream', -0.5


def add_bus(md, cx, cy):
    """bus centred at (cx, cy) heading +X: 24 x 7.6 units, 11 px tall"""
    gp = new_grp()
    L, Wd = 12.0, 3.8
    for wx in (-8.6, 5.8):
        md.add(box(cx + wx, cy - Wd + 0.3, 0.0, cx + wx + 3.0, cy + Wd - 0.3, 2.6, m_flat('tyre'), grp=gp))
    md.add(box(cx - L, cy - Wd, 1.0, cx + L, cy + Wd, 10.4, m_bus, cx=cx, cy=cy, grp=gp))
    md.add(box(cx - L + 0.8, cy - Wd + 0.6, 10.4, cx + L - 0.8, cy + Wd - 0.6, 11.0, m_flat('bus_roof', 0.6), grp=gp))
    md.add(box(cx + L - 0.2, cy - Wd + 0.2, 1.2, cx + L + 0.4, cy + Wd - 0.2, 2.2, m_flat('chrome', 0.4), grp=gp))


def bus_model():
    md = Model('bus', ground=None)
    add_bus(md, 16.0, 16.0)
    md.top = 16.0
    return md


# =========================================================================== fire frames (procedural)
FIRE_COLS = ['#f6e7a4', '#efc25a', '#e08a36', '#c4552c', '#86301f']   # core .. dark-red rim
FIRE_SMOKE = ['#7a7570', '#5c5854']


def fire_img(zoom, f):
    """flame frame f (0..3): three or four tongues whose heights and sway cycle with f, hot yellow core,
    orange/red body, dark-red rim, a dithered smoke wisp above.  Anchor = base centre."""
    W = int(round(26 * zoom)) + 2
    H = int(round(42 * zoom)) + 2
    img = Image.new('RGB', (W, H), KEY)
    p = img.load()
    cx = W / 2.0
    base = H - 1.0
    ph = f * math.pi / 2.0
    tongues = [(-5.5, 0.62 + 0.18 * math.sin(ph), 4.6, 0.0), (0.0, 0.92 + 0.08 * math.sin(ph + 2.1), 6.0, 1.3),
               (5.0, 0.70 + 0.18 * math.sin(ph + 4.0), 4.8, 2.6), (-2.0, 0.48 + 0.16 * math.sin(ph + 1.0), 3.4, 3.9)]
    Hf = 30.0 * zoom
    inten = {}
    for y in range(H):
        for x in range(W):
            best = 0.0
            for (ox, hh, ww, sp) in tongues:
                th = Hf * hh
                t = (base - (y + 0.5)) / th
                if t < 0 or t > 1:
                    continue
                xc = cx + ox * zoom + 1.6 * zoom * math.sin(t * 3.0 + ph * 1.0 + sp) * t
                hw = ww * zoom * (1 - t) ** 0.75 * (0.55 + 0.45 * math.sin(math.pi * min(1.0, t * 1.4 + 0.3)))
                if hw <= 0.2:
                    continue
                d = abs(x + 0.5 - xc) / hw
                if d < 1.0:
                    best = max(best, (1 - d) * (1 - 0.65 * t))
            if best > 0:
                inten[(x, y)] = best
    for (x, y), v in inten.items():
        edge = any((x + dx, y + dy) not in inten for (dx, dy) in ((1, 0), (-1, 0), (0, 1), (0, -1)))
        if edge:
            c = FIRE_COLS[4]
        else:
            i = 0 if v > 0.62 else 1 if v > 0.42 else 2 if v > 0.24 else 3
            c = FIRE_COLS[i]
        p[x, y] = to565(hexc(c))
    # smoke wisp above (drifts up and right with the frame)
    top = min(y for (x, y) in inten) if inten else H // 2
    lobes = [(cx + (0.5 + f * 0.9) * zoom, top - (1.5 + f * 1.2) * zoom, (2.6 + 0.4 * f) * zoom),
             (cx + (2.5 + f * 0.6) * zoom, top - (5.0 + f * 1.5) * zoom, (2.2 + 0.5 * f) * zoom)]
    for y in range(H):
        for x in range(W):
            if (x, y) in inten:
                continue
            for k, (sx, sy, r) in enumerate(lobes):
                dx, dy = (x + 0.5 - sx) / r, (y + 0.5 - sy) / (r * 0.85)
                d2 = dx * dx + dy * dy
                if d2 < 1.0 and (d2 < 0.45 - 0.15 * k or (x + y + f) % 2 == 0):
                    p[x, y] = to565(hexc(FIRE_SMOKE[0 if dx + dy < 0 else 1]))
                    break
    return img, (W // 2, H - 1)


def extra_procedural(zoom):
    out = {}
    for f in range(4):
        img, a = fire_img(zoom, f)
        out['fire%d' % f] = (img, a, (-1, -1))
    return out


# =========================================================================== props
def add_tractor(md, x, y, heading='x', col='tractor_or'):
    """small open farm/forestry tractor (Valmet-like silhouette) at (x, y): bonnet, seat, big rear wheels"""
    gp = new_grp()
    if heading == 'x':
        md.add(box(x - 3.2, y - 2.2, 0.0, x - 0.6, y - 1.3, 3.6, m_flat('tyre'), grp=gp))
        md.add(box(x - 3.2, y + 1.3, 0.0, x - 0.6, y + 2.2, 3.6, m_flat('tyre'), grp=gp))
        md.add(box(x + 2.2, y - 1.8, 0.0, x + 3.6, y - 1.1, 2.0, m_flat('tyre'), grp=gp))
        md.add(box(x + 2.2, y + 1.1, 0.0, x + 3.6, y + 1.8, 2.0, m_flat('tyre'), grp=gp))
        md.add(box(x - 1.0, y - 1.0, 1.2, x + 4.0, y + 1.0, 3.8, m_flat(col), grp=gp))
        md.add(box(x - 2.6, y - 1.3, 1.6, x - 0.4, y + 1.3, 4.2, m_flat(col, -0.3), grp=gp))
        md.add(box(x - 2.3, y - 0.8, 4.2, x - 1.1, y + 0.8, 5.4, m_flat('tyre', 0.4), grp=gp))
        md.add(box(x + 2.6, y - 0.25, 3.8, x + 3.1, y + 0.25, 6.6, m_flat('steel'), grp=gp, detail=True))
    else:
        md.add(box(x - 2.2, y - 3.2, 0.0, x - 1.3, y - 0.6, 3.6, m_flat('tyre'), grp=gp))
        md.add(box(x + 1.3, y - 3.2, 0.0, x + 2.2, y - 0.6, 3.6, m_flat('tyre'), grp=gp))
        md.add(box(x - 1.8, y + 2.2, 0.0, x - 1.1, y + 3.6, 2.0, m_flat('tyre'), grp=gp))
        md.add(box(x + 1.1, y + 2.2, 0.0, x + 1.8, y + 3.6, 2.0, m_flat('tyre'), grp=gp))
        md.add(box(x - 1.0, y - 1.0, 1.2, x + 1.0, y + 4.0, 3.8, m_flat(col), grp=gp))
        md.add(box(x - 1.3, y - 2.6, 1.6, x + 1.3, y - 0.4, 4.2, m_flat(col, -0.3), grp=gp))
        md.add(box(x - 0.8, y - 2.3, 4.2, x + 0.8, y - 1.1, 5.4, m_flat('tyre', 0.4), grp=gp))
        md.add(box(x - 0.25, y + 2.6, 3.8, x + 0.25, y + 3.1, 6.6, m_flat('steel'), grp=gp, detail=True))


def add_horse(md, x, y, heading='x'):
    """brown work horse (suomenhevonen) facing +X or +Y: legs, barrel body, neck and head"""
    gp = new_grp()
    if heading == 'x':
        for lx in (-2.0, 1.8):
            for ly in (-0.7, 0.7):
                md.add(box(x + lx - 0.35, y + ly - 0.35, 0, x + lx + 0.35, y + ly + 0.35, 3.8, m_flat('horse', -0.4),
                           grp=gp, detail=True))
        md.add(Ellipsoid(x, y, 5.2, 3.2, 1.2, 1.9, m_flat('horse'), grp=gp))
        md.add(Ellipsoid(x + 3.3, y, 7.0, 1.3, 0.7, 1.9, m_flat('horse'), grp=gp))
        md.add(Ellipsoid(x + 4.2, y, 7.6, 1.2, 0.6, 0.9, m_flat('horse', -0.3), grp=gp))
    else:
        for lx in (-0.7, 0.7):
            for ly in (-2.0, 1.8):
                md.add(box(x + lx - 0.35, y + ly - 0.35, 0, x + lx + 0.35, y + ly + 0.35, 3.8, m_flat('horse', -0.4),
                           grp=gp, detail=True))
        md.add(Ellipsoid(x, y, 5.2, 1.2, 3.2, 1.9, m_flat('horse'), grp=gp))
        md.add(Ellipsoid(x, y + 3.3, 7.0, 0.7, 1.3, 1.9, m_flat('horse'), grp=gp))
        md.add(Ellipsoid(x, y + 4.2, 7.6, 0.6, 1.2, 0.9, m_flat('horse', -0.3), grp=gp))


def m_stump(g, st):
    if g.tag == 'cap':
        return 'stump_top', 0.3
    return 'bark', -0.2


def add_stump(md, x, y, r=1.1, h=1.6):
    md.add(Cone(x, y, 0, h, r, r * 0.9, m_stump, cap=True, grp=new_grp()))


def add_milkcans(md, x0, y0, x1, y1):
    """maitolaituri: a raised plank platform on posts with metal milk cans"""
    md.add(box(x0, y0, 0, x1, y1, 3.0, m_flat('fence_wood', -0.4)))
    md.add(box(x0 - 0.2, y0 - 0.2, 3.0, x1 + 0.2, y1 + 0.2, 3.6, m_flat('fence_wood', 0.3)))
    n = int((x1 - x0) / 1.7)
    for i in range(n):
        md.add(Cone(x0 + 1.0 + i * 1.7, (y0 + y1) / 2.0, 3.6, 6.4, 0.65, 0.65, m_flat('steel_lt'), cap=True,
                    detail=True))


def add_haystack(md, x, y, r=2.6, h=6.0):
    """haystack round a drying pole (seiväs)"""
    gp = new_grp()
    md.add(Cone(x, y, 0, h, r, 0.7, m_hay, cap=True, grp=gp))
    md.add(box(x - 0.2, y - 0.2, h, x + 0.2, y + 0.2, h + 1.6, m_flat('pole'), grp=gp, detail=True))


def m_hay(g, st):
    P = g.P
    if st.zoom >= 1 and ihash(int(P[0] * 2.2), int(P[1] * 2.2), int(P[2] * 1.5), 7) < 0.25:
        return 'hay_dk', 0.0
    return 'hay', 0.0


def add_hayrack(md, x0, x1, y, h=5.0):
    """haasia: a long hay-drying rack (posts + hay hung over the rails), along X"""
    gp = new_grp()
    md.add(box(x0, y - 0.7, 0.6, x1, y + 0.7, h, m_hay, grp=gp))
    md.add(box(x0 + 0.3, y - 0.45, h, x1 - 0.3, y + 0.45, h + 0.6, m_flat('hay', 0.3), grp=gp))
    for px in (x0 - 0.3, (x0 + x1) / 2.0, x1 + 0.3):
        md.add(box(px - 0.25, y - 0.25, 0, px + 0.25, y + 0.25, h + 1.2, m_flat('pole'), grp=gp, detail=True))


def barn(md, x0, y0, x1, y1, ridge='x', wall='barn_red', ze=9.0, roof='roof_dark', stone=False, doors=True,
         seed=1):
    """navetta / lato: vertical red boards, white-trimmed door frames, optional fieldstone base"""
    half = ((y1 - y0) if ridge == 'x' else (x1 - x0)) / 2.0
    zr = ze + half * KZ * 0.95
    dd = {}
    if doors:
        if ridge == 'x':
            m = (x0 + x1) / 2.0
            dd = {'y+': [(m - 2.5, m + 2.5, 0.0, 7.0)], 'x+': [((y0 + y1) / 2 - 1.5, (y0 + y1) / 2 + 1.5, ze + 0.5, ze + 4.5)]}
        else:
            m = (y0 + y1) / 2.0
            dd = {'x+': [(m - 2.5, m + 2.5, 0.0, 7.0)], 'y+': [((x0 + x1) / 2 - 1.5, (x0 + x1) / 2 + 1.5, ze + 0.5, ze + 4.5)]}
    wins = {}
    if stone:
        if ridge == 'x':
            wins = {'y+': wins_row(x0, x1, 1.6, 3.4, 2.0, 3, skip=(((x0 + x1) / 2 - 3.5, (x0 + x1) / 2 + 3.5),))}
        else:
            wins = {'x+': wins_row(y0, y1, 1.6, 3.4, 2.0, 3, skip=(((y0 + y1) / 2 - 3.5, (y0 + y1) / 2 + 3.5),))}
    md.add(gable_body(x0, y0, x1, y1, 0, ze, zr, ridge, m_house, wall=wall, plinth=(4.0 if stone else 0.8),
                      plinthkey=('rock' if stone else 'plinth'), doors=dd, wins=wins, clad='v', eave=ze, frieze=False))
    md.add(gable_roof(x0, y0, x1, y1, ze, zr, ridge, m_roof, ov=1.0, ovg=1.0, th=1.2, key=roof, edgekey='trim',
                      seam=3.0))
    return zr


def farmhouse(md, x0, y0, x1, y1, ridge='x', wall='w_yellow', storeys=1, roof='roof_metal', frame='trim',
              chim=0.5, porch=True):
    """farmhouse body (1 or 2 storeys), windows on all sides, chimney, small porch on the +Y/+X face"""
    pl = 1.4
    ze = pl + (8.5 if storeys == 1 else 17.0)
    half = ((y1 - y0) if ridge == 'x' else (x1 - x0)) / 2.0
    zr = ze + half * KZ * 0.9
    rows = [(pl + 2.0, pl + 7.0)] + ([(pl + 10.5, pl + 15.5)] if storeys > 1 else [])
    wins = {'x+': [], 'x-': [], 'y+': [], 'y-': []}
    la, lb = (x0, x1) if ridge == 'x' else (y0, y1)
    sa, sb = (y0, y1) if ridge == 'x' else (x0, x1)
    longf, shortf = (('y+', 'y-'), ('x+', 'x-')) if ridge == 'x' else (('x+', 'x-'), ('y+', 'y-'))
    nl = max(2, int((lb - la) / 5.0))
    for (t0, t1) in rows:
        for f in longf:
            wins[f] += wins_row(la, lb, t0, t1, 3.0, nl)
        for f in shortf:
            wins[f] += wins_row(sa, sb, t0, t1, 3.0, 2)
    mid = (sa + sb) / 2.0
    for f in shortf:
        wins[f].append((mid - 1.5, mid + 1.5, ze + 1.0, ze + 4.5))
    md.add(gable_body(x0, y0, x1, y1, 0, ze, zr, ridge, m_house, wall=wall, plinth=pl, wins=wins, clad='h', eave=ze,
                      frame=frame))
    md.add(gable_roof(x0, y0, x1, y1, ze, zr, ridge, m_roof, ov=1.3, ovg=1.3, th=1.3, key=roof, edgekey=frame,
                      seam=3.0))
    if ridge == 'x':
        cxp, cyp = lerp(x0 + 3, x1 - 3, chim), mid
    else:
        cxp, cyp = mid, lerp(y0 + 3, y1 - 3, chim)
    md.add(box(cxp - 1.2, cyp - 1.2, ze, cxp + 1.2, cyp + 1.2, zr + 4.0, m_chimney, key='brick'))
    if porch:
        if ridge == 'x':
            pc = lerp(x0, x1, 0.62)
            md.add(gable_body(pc - 2.4, y1 - 0.5, pc + 2.4, y1 + 3.0, 0, pl + 7.5, pl + 7.5 + 2.4 * KZ, 'y', m_house,
                              wall=wall, plinth=pl - 0.4, clad='v', eave=pl + 7.5, frieze=False, frame=frame,
                              doors={'y+': [(pc - 1.2, pc + 1.2, pl - 0.4, pl + 6.5)]}))
            md.add(gable_roof(pc - 2.4, y1 - 0.5, pc + 2.4, y1 + 3.0, pl + 7.5, pl + 7.5 + 2.4 * KZ, 'y', m_roof, ov=0.7,
                              ovg=0.6, th=1.1, key=roof, edgekey=frame, seam=3.0))
        else:
            pc = lerp(y0, y1, 0.62)
            md.add(gable_body(x1 - 0.5, pc - 2.4, x1 + 3.0, pc + 2.4, 0, pl + 7.5, pl + 7.5 + 2.4 * KZ, 'x', m_house,
                              wall=wall, plinth=pl - 0.4, clad='v', eave=pl + 7.5, frieze=False, frame=frame,
                              doors={'x+': [(pc - 1.2, pc + 1.2, pl - 0.4, pl + 6.5)]}))
            md.add(gable_roof(x1 - 0.5, pc - 2.4, x1 + 3.0, pc + 2.4, pl + 7.5, pl + 7.5 + 2.4 * KZ, 'x', m_roof, ov=0.7,
                              ovg=0.6, th=1.1, key=roof, edgekey=frame, seam=3.0))


# =========================================================================== farmsteads (orientation-free)
def farm(name, tier=1, var=0, seed=1):
    """farm_t1 pientila, farm_t2 maalaistalo, farm_t3 mechanised farm (1x1, sits among field tiles)"""
    md = Model(name)
    gr = []
    if tier == 1 and var == 0:
        farmhouse(md, 4.0, 5.0, 16.0, 14.0, 'x', wall='w_red', roof='roof_felt', chim=0.4)
        barn(md, 19.0, 3.0, 29.5, 15.0, 'y', wall='w_weath', roof='roof_felt', ze=8.0)
        add_woodpile(md, 4.0, 18.5, 10.0, 20.5, 3.5)
        add_haystack(md, 25.0, 24.0, 2.6, 6.5)
        add_rowan(md, 4.5, 27.0, seed=seed + 11)
        gr = [(3.0, 15.0, 30.0, 21.0, 'dirt'), (13.0, 21.0, 17.0, 32.0, 'gravel'), (17.5, 22.0, 30.5, 30.0, 'potato')]
    elif tier == 1:
        farmhouse(md, 16.0, 4.0, 28.0, 13.0, 'x', wall='w_weath', roof='roof_metal', chim=0.6)
        barn(md, 3.0, 4.0, 13.0, 15.5, 'y', wall='barn_red', roof='roof_felt', ze=7.5)
        md.add(mono_body(4.0, 20.0, 11.0, 27.0, 0, 6.5, 8.0, m_house, wall='w_weath', plinth=0.3, clad='v',
                         frieze=False, corners=False, doors={'x+': [(21.5, 25.5, 0.3, 5.5)]}, rise='x-'))   # hay barn
        add_haystack(md, 24.0, 23.5, 2.4, 6.0)
        add_haystack(md, 28.5, 27.5, 2.0, 5.0)
        add_birch(md, 29.0, 3.5, h=30, seed=seed + 12)
        gr = [(13.0, 13.0, 29.0, 19.0, 'dirt'), (17.0, 19.0, 21.0, 32.0, 'gravel')]
    elif tier == 2 and var == 0:
        farmhouse(md, 3.5, 4.0, 15.5, 13.5, 'x', wall='w_yellow', storeys=2, chim=0.5)
        barn(md, 18.0, 2.5, 30.5, 19.0, 'y', wall='barn_red', ze=9.5, stone=True)
        add_milkcans(md, 24.0, 27.0, 30.0, 29.8)
        add_birch(md, 4.0, 27.0, h=34, seed=seed + 13)
        add_bush(md, 13.0, 27.5, seed=seed + 14)
        add_woodpile(md, 18.5, 21.0, 23.0, 23.0, 3.5)
        gr = [(2.0, 14.0, 31.0, 22.0, 'gravel'), (20.0, 22.0, 24.0, 32.0, 'gravel')]
    elif tier == 2:
        farmhouse(md, 18.5, 3.0, 29.5, 15.0, 'y', wall='w_cream', storeys=2, chim=0.4, roof='roof_red', porch=False)
        barn(md, 2.0, 15.5, 19.0, 26.5, 'x', wall='barn_red', ze=8.5, stone=True)
        add_milkcans(md, 22.0, 27.5, 28.0, 30.2)
        add_haystack(md, 7.0, 7.0, 2.4, 6.0)
        add_rowan(md, 3.0, 3.0, seed=seed + 15)
        gr = [(2.0, 2.0, 18.0, 15.0, 'gravel'), (19.0, 15.0, 30.0, 26.0, 'gravel'), (23.0, 26.0, 27.0, 32.0, 'gravel')]
    elif tier == 3 and var == 0:
        farmhouse(md, 3.5, 3.5, 14.5, 12.5, 'x', wall='w_yellow', storeys=2, chim=0.5)
        barn(md, 17.5, 2.0, 30.5, 16.5, 'y', wall='barn_red', ze=9.5, stone=True)
        md.add(Cone(26.5, 21.5, 0, 27.0, 3.0, 3.0, m_concrete_round, cap=False, key='concrete', band=(0.0, 1.2, 'concrete_dk')))
        md.add(Cone(26.5, 21.5, 27.0, 30.0, 3.3, 0.8, m_flat('roof_metal'), cap=True))                       # silo
        # machine shed: open front, corrugated mono roof, tractor in front
        md.add(box(3.0, 17.0, 0, 15.0, 17.8, 7.0, m_corr, wall='corr'))
        md.add(box(3.0, 17.0, 0, 3.8, 24.0, 6.0, m_corr, wall='corr'))
        md.add(box(14.4, 23.0, 0, 15.0, 23.6, 5.8, m_flat('steel'), detail=True))
        md.add(mono_body(2.6, 16.6, 15.4, 24.4, 5.8, 6.2, 7.6, m_corr, wall='corr', roofkey='roof_metal', rise='y-'))
        add_tractor(md, 9.5, 27.5, 'x')
        gr = [(2.0, 13.0, 31.0, 32.0, 'gravel')]
    else:
        farmhouse(md, 2.5, 3.0, 13.5, 15.0, 'y', wall='w_grey', storeys=2, chim=0.6, roof='roof_dark', porch=False)
        barn(md, 15.5, 2.5, 30.0, 14.0, 'x', wall='barn_red', ze=10.0, stone=True)
        md.add(Cone(28.0, 18.5, 0, 25.0, 3.0, 3.0, m_concrete_round, cap=False, key='render_white', ribs=10))
        md.add(Cone(28.0, 18.5, 25.0, 27.5, 3.3, 0.8, m_flat('roof_metal'), cap=True))
        add_tractor(md, 7.0, 23.0, 'x')
        md.add(box(11.0, 21.0, 0.8, 19.0, 25.0, 3.4, m_flat('fence_wood')))          # trailer with hay
        md.add(box(11.4, 21.3, 3.4, 18.6, 24.7, 6.0, m_hay))
        md.add(box(17.6, 25.0, 0, 18.4, 25.8, 1.0, m_flat('tyre')))
        gr = [(2.0, 15.0, 31.0, 30.0, 'gravel'), (14.0, 30.0, 18.0, 32.0, 'gravel')]
    md.ground = lambda g, st: g_lot(g, st, gravel=gr, var=seed)
    return md


# =========================================================================== logging camp
def logging_camp(name, var=0, seed=1):
    """kämppä: log cabin in the forest; v0 horse + sledge with logs (before 1965), v1 forestry tractor +
    log trailer (1965+); stumps, log pile, spruces at the back"""
    md = Model(name)
    if var == 0:
        x0, y0, x1, y1 = 4.0, 4.0, 17.0, 13.0
        wall, roof = 'log_wall', 'roof_felt'
    else:
        x0, y0, x1, y1 = 4.0, 4.0, 19.0, 12.5
        wall, roof = 'w_red', 'roof_dark'
    ze = 1.2 + 8.0
    zr = ze + (y1 - y0) / 2.0 * KZ * 0.8
    md.add(gable_body(x0, y0, x1, y1, 0, ze, zr, 'x', m_house, wall=wall, plinth=1.2, clad='h', eave=ze,
                      frieze=(var == 1), corners=(var == 1),
                      wins={'y+': [(x0 + 2.0, x0 + 5.0, 3.0, 6.5), (x1 - 5.0, x1 - 2.0, 3.0, 6.5)],
                            'x+': [(y0 + 2.5, y0 + 5.5, 3.0, 6.5)], 'x-': [(y0 + 2.5, y0 + 5.5, 3.0, 6.5)]},
                      doors={'y+': [((x0 + x1) / 2 - 1.3, (x0 + x1) / 2 + 1.3, 1.2, 7.8)]}))
    md.add(gable_roof(x0, y0, x1, y1, ze, zr, 'x', m_roof, ov=1.2, ovg=1.2, th=1.2, key=roof, edgekey='trim', seam=3.0))
    md.add(box(x0 + 3.0, (y0 + y1) / 2 - 1.0, ze, x0 + 5.0, (y0 + y1) / 2 + 1.0, zr + 3.5, m_chimney, key='brick'))
    md.hot = (x0 + 4.0, (y0 + y1) / 2, zr + 4.0)
    add_woodpile(md, x1 + 1.0, 5.0, x1 + 3.5, 11.0, 3.5)
    if var == 0:
        add_horse(md, 11.0, 24.0, 'x')
        md.add(box(1.5, 22.6, 0.4, 8.0, 25.4, 1.2, m_flat('fence_wood')))                  # sledge runners/bed
        md.add(box(2.0, 22.8, 1.2, 7.6, 25.2, 3.6, m_logs, ends='x'))
        md.add(box(8.0, 23.7, 2.6, 9.8, 24.3, 3.0, m_flat('fence_wood'), detail=True))        # shafts
        md.add(box(20.0, 17.0, 0, 30.0, 23.0, 4.0, m_logs, ends='y'))
    else:
        add_tractor(md, 8.0, 24.0, 'x')
        md.add(box(13.0, 22.6, 0.8, 22.0, 25.4, 1.8, m_flat('steel')))                     # log trailer
        md.add(box(13.2, 22.8, 1.8, 21.8, 25.2, 4.8, m_logs, ends='x'))
        md.add(box(21.8, 23.0, 0, 22.6, 25.0, 1.0, m_flat('tyre')))
        md.add(box(20.0, 14.5, 0, 30.5, 21.0, 5.5, m_logs, ends='y'))
        md.add(Cone(27.5, 26.5, 0, 3.6, 1.2, 1.2, m_flat('rust'), cap=True))
        md.add(Cone(29.5, 25.5, 0, 3.6, 1.2, 1.2, m_flat('steel'), cap=True))
    for (sx, sy) in ((26.5, 4.0), (3.0, 29.0), (16.0, 29.5), (29.0, 29.5)):
        add_stump(md, sx, sy)
    add_spruce(md, 28.0, 8.0, h=34, seed=seed + 21, r=4.8)
    add_spruce(md, 2.6, 18.0, h=26, seed=seed + 22, r=3.6)
    gr = [(1.0, 13.0, 31.0, 27.5, 'dirt'), ((x0 + x1) / 2 - 1.5, 13.0, (x0 + x1) / 2 + 1.5, 16.0, 'gravel')]
    md.ground = lambda g, st: g_lot(g, st, gravel=gr, var=seed)
    return md


# =========================================================================== forest regrowth stages
def g_cut(g, st, var):
    """clear-cut floor: grass at the tile edges (seams), churned brown earth and slash inside"""
    x, y = g.W[0], g.W[1]
    e = edge_window(x, y)
    n = vnoise(x, y, 4.0, 310 + var)
    if n * e > 0.40:
        return ('dirt' if vnoise(x, y, 2.0, 312) > 0.5 else 'dirt_dk'), 0.0
    if vnoise(x, y, 3.0, 314 + var) * e > 0.45:
        return 'slash', (ihash(int(x * 1.3), int(y * 1.3), 315) - 0.5) * 0.8
    return g_grass(g, st, var, 'grass_dk' if n * e > 0.3 else 'grass')


def forest_cut(var=0):
    md = Model('forest_cut')
    pts = [(6, 7), (14, 5), (24, 8), (9, 15), (19, 13), (27, 17), (5, 24), (13, 22), (22, 25), (16, 28), (27, 27)]
    for i, (x, y) in enumerate(pts):
        if ihash(i, var, 3) < 0.15:
            continue
        add_stump(md, x + (ihash(i, var, 1) - 0.5) * 2, y + (ihash(i, var, 2) - 0.5) * 2,
                  r=0.9 + 0.5 * ihash(i, var, 4), h=1.2 + 0.8 * ihash(i, var, 5))
    for i, (x, y, rx) in enumerate(((11.0, 10.0, 3.0), (21.0, 20.0, 3.5), (8.0, 19.0, 2.4))):
        md.add(Ellipsoid(x, y, 0.4, rx, rx * 0.6, 1.4, m_flat('slash'), grp=new_grp()))
    if var == 0:
        add_pine(md, 20.0, 9.0, h=40, seed=71)
    else:
        add_pine(md, 9.0, 8.0, h=38, seed=72)
        add_birch(md, 24.0, 21.0, h=30, seed=73)
    add_bush(md, 26.0, 11.0, seed=74 + var, size=0.6)
    md.ground = lambda g, st: g_cut(g, st, var)
    return md


def add_sapling(md, x, y, h, kind, seed, gp=None):
    gp = gp or new_grp()
    if kind == 'spruce':
        md.add(Cone(x, y, 0, h * 0.25, 0.3, 0.25, m_trunk, cap=False, key='bark', grp=gp, detail=True))
        md.add(Cone(x, y, h * 0.15, h, h * 0.24, 0.1, m_spruce, cap=True, seed=seed, grp=gp, detail=True))
    elif kind == 'pine':
        md.add(Cone(x, y, 0, h * 0.5, 0.3, 0.25, m_trunk, cap=False, key='bark_pine', grp=gp, detail=True))
        md.add(Ellipsoid(x, y, h * 0.65, h * 0.22, h * 0.22, h * 0.35, m_leaf, key='pine_leaf', seed=seed, grp=gp, detail=True))
    else:
        md.add(Cone(x, y, 0, h * 0.5, 0.25, 0.2, m_trunk, cap=False, key='bark_birch', grp=gp, detail=True))
        md.add(Ellipsoid(x, y, h * 0.66, h * 0.18, h * 0.18, h * 0.36, m_leaf, key='birch_leaf', seed=seed, grp=gp, detail=True))


def forest_seed(var=0):
    """taimikko: knee-high saplings in brush (density 4..7)"""
    md = Model('forest_seed')
    k = 0
    for i in range(5):
        gp = new_grp()
        for j in range(5):
            x = 4.0 + i * 6.0 + (ihash(i, j, 81 + var) - 0.5) * 3.0
            y = 4.0 + j * 6.0 + (ihash(i, j, 82 + var) - 0.5) * 3.0
            if ihash(i, j, 83 + var) < 0.18:
                continue
            h = 5.0 + 3.5 * ihash(i, j, 84 + var)
            kinds = ('spruce', 'spruce', 'pine', 'birch') if var == 0 else ('pine', 'birch', 'pine', 'spruce')
            add_sapling(md, x, y, h, kinds[int(ihash(i, j, 85) * 4)], 400 + k, gp)
            k += 1
    for (x, y) in ((10.0, 20.0), (22.0, 9.0)):
        add_bush(md, x, y, seed=90 + var, size=0.5)
    add_stump(md, 16.0, 15.0, 1.0, 1.4)
    add_stump(md, 26.0, 26.0, 1.0, 1.2)
    md.ground = lambda g, st: g_forest_floor(g, st, 3 + var)
    return md


def forest_young(var=0):
    """nuori metsä: dense young stand about half the height of a mature forest tile (density 8..11)"""
    md = Model('forest_young')
    spots = [(7.0, 7.0), (16.0, 6.0), (25.0, 8.0), (6.0, 16.0), (16.0, 16.0), (26.0, 17.0), (8.0, 25.0),
             (17.0, 26.0), (25.0, 25.0)]
    kinds = ['spruce', 'birch', 'spruce', 'pine', 'spruce', 'birch', 'spruce', 'pine', 'spruce'] if var == 0 else \
        ['birch', 'pine', 'birch', 'spruce', 'birch', 'pine', 'birch', 'birch', 'spruce']
    for i, (x, y) in enumerate(spots):
        x += (ihash(i, var, 11) - 0.5) * 3.0
        y += (ihash(i, var, 12) - 0.5) * 3.0
        h = 16.0 + 7.0 * ihash(i, var, 13)
        k = kinds[i]
        s = 500 + var * 20 + i
        if k == 'spruce':
            add_spruce(md, x, y, h=h, seed=s, r=3.2)
        elif k == 'pine':
            gp = new_grp()
            md.add(Cone(x, y, 0, h * 0.7, 0.55, 0.4, m_trunk, cap=False, key='bark_pine', grp=gp))
            for j in range(6):
                a = ihash(j, s, 1) * 2 * math.pi
                r = math.sqrt(ihash(j, s, 2)) * 2.4
                md.add(Ellipsoid(x + math.cos(a) * r, y + math.sin(a) * r, h * 0.62 + ihash(j, s, 3) * h * 0.3,
                                 1.8, 1.8, 1.2 * KZ, m_leaf, key='pine_leaf', seed=s + j, grp=gp))
        else:
            add_birch(md, x, y, h=h, seed=s, crown=0.62)
    md.ground = lambda g, st: g_forest_floor(g, st, 5 + var)
    return md


# =========================================================================== hay field
def field_hay():
    """field tile with hay-drying racks (haasia) and pole stacks (seiväs) either side of the ditch"""
    md = Model('field_hay')
    add_hayrack(md, 4.0, 27.0, 7.5, 5.0)
    add_haystack(md, 7.0, 24.0, 2.0, 5.5)
    add_haystack(md, 15.0, 25.0, 2.0, 5.0)
    add_haystack(md, 23.0, 23.5, 2.0, 5.5)
    md.ground = lambda g, st: TR.g_field(g, st, 'crop')
    return md


# =========================================================================== railway station
def station(name, seed=1):
    """small-town VR station (2x2): yellow-ochre wooden station house with white trim, a front cross gable
    (stationmaster's flat), deep eaves on brackets, a platform with a canopy along the BACK edge (-Y: the
    railway runs just outside the lot), a red goods shed with a loading dock, name boards, a semaphore"""
    md = Model(name, 2, 2)
    pl = 1.5
    x0, x1, y0, y1 = 8.0, 45.0, 11.0, 25.0
    ze = pl + 12.0
    zr = ze + (y1 - y0) / 2.0 * KZ * 0.85
    wy = wins_row(x0, x1, pl + 2.5, pl + 8.5, 3.0, 7, skip=((24.5, 29.5),))
    wins = {'y+': [r for r in wy if not (23.0 < r[0] < 31.0)], 'y-': wins_row(x0, x1, pl + 2.5, pl + 8.5, 3.0, 7),
            'x+': wins_row(y0, y1, pl + 2.5, pl + 8.5, 3.0, 2) + [((y0 + y1) / 2 - 1.5, (y0 + y1) / 2 + 1.5, ze + 1.0, ze + 4.5)],
            'x-': wins_row(y0, y1, pl + 2.5, pl + 8.5, 3.0, 2)}
    doors = {'y-': [(18.0, 20.5, pl, pl + 8.0), (34.0, 36.5, pl, pl + 8.0)]}
    md.add(gable_body(x0, y0, x1, y1, 0, ze, zr, 'x', m_house, wall='w_yellow', plinth=pl, wins=wins, doors=doors,
                      clad='h', eave=ze, door='door_green'))
    md.add(gable_roof(x0, y0, x1, y1, ze, zr, 'x', m_roof, ov=2.4, ovg=1.8, th=1.4, key='roof_red', edgekey='trim',
                      seam=3.0))
    # front cross gable (2 storeys) with the main door
    cx0, cx1, cy0, cy1 = 23.5, 31.5, y0 + 2.0, y1 + 2.5
    cze = pl + 18.5
    czr = cze + (cx1 - cx0) / 2.0 * KZ * 1.05
    md.add(gable_body(cx0, cy0, cx1, cy1, 0, cze, czr, 'y', m_house, wall='w_yellow', plinth=pl, clad='v', eave=cze,
                      door='door_green',
                      wins={'y+': [(cx0 + 2.5, cx1 - 2.5, pl + 11.0, pl + 16.5), ((cx0 + cx1) / 2 - 1.2, (cx0 + cx1) / 2 + 1.2, cze + 1.0, cze + 4.5)],
                            'x+': [(y1 - 0.2, y1 + 2.2, pl + 11.0, pl + 16.0)]},
                      doors={'y+': [(cx0 + 2.4, cx1 - 2.4, pl, pl + 8.0)]}))
    md.add(gable_roof(cx0, cy0, cx1, cy1, cze, czr, 'y', m_roof, ov=1.6, ovg=1.2, th=1.4, key='roof_red', edgekey='trim',
                      seam=3.0))
    md.add(box(cx0 - 0.5, cy1 + 0.4, pl + 8.6, cx1 + 0.5, cy1 + 2.4, pl + 9.4, m_flat('trim')))          # door canopy
    # eave brackets on the long walls (decorative, white)
    for bx in (x0 + 2.0, x0 + 7.5, x0 + 13.0, x1 - 13.0, x1 - 7.5, x1 - 2.0):
        for (ya, yb) in ((y1, y1 + 2.0), (y0 - 2.0, y0)):
            md.add(box(bx - 0.3, ya, ze - 3.0, bx + 0.3, yb, ze - 0.2, m_flat('trim'), detail=True))
    md.add(box(x0 + 9.5, cy0 - 0.5, ze, x0 + 11.5, cy0 + 1.5, zr + 4.0, m_chimney, key='brick'))
    md.add(box(x1 - 8.0, (y0 + y1) / 2 - 1.0, ze, x1 - 6.0, (y0 + y1) / 2 + 1.0, zr + 4.0, m_chimney, key='brick'))
    # name boards (bilingual pseudo-text, two rows) on the track side and the gable end
    md.add(box(21.0, y0 - 2.6, ze - 3.6, 33.0, y0 - 2.2, ze - 0.6, m_block, wall='sign_blue', plinth=-1,
               bands={'y-': [(21.0, 33.0, ze - 3.6, ze - 2.1, 'sign_blue', 'text'), (21.0, 33.0, ze - 2.1, ze - 0.6, 'sign_blue', 'text')],
                      'y+': [(21.0, 33.0, ze - 3.6, ze - 2.1, 'sign_blue', 'text'), (21.0, 33.0, ze - 2.1, ze - 0.6, 'sign_blue', 'text')]}))
    md.add(box(x1 + 0.1, (y0 + y1) / 2 - 4.0, ze - 3.2, x1 + 0.5, (y0 + y1) / 2 + 4.0, ze - 0.4, m_block, wall='sign_blue',
               plinth=-1, bands={'x+': [((y0 + y1) / 2 - 4.0, (y0 + y1) / 2 + 4.0, ze - 3.2, ze - 0.4, 'sign_blue', 'text')]}))
    # platform (raised, concrete kerb at the track edge) + canopy on posts in front of the building
    md.add(box(1.0, 0.6, 0, 63.0, 9.0, 1.2, m_concrete, key='concrete', joints=4.0))
    for px in (13.0, 20.0, 27.0, 34.0, 41.0):
        md.add(box(px - 0.35, 3.2, 1.2, px + 0.35, 3.9, 9.5, m_flat('trim'), detail=True))
    md.add(mono_body(9.0, 1.8, 45.0, y0, 9.5, 9.6, 11.0, m_flat('roof_metal', 0.3), rise='y+'))
    md.add(box(9.0, 1.6, 9.0, 45.0, 2.2, 9.6, m_flat('trim')))
    # goods shed with a loading dock on the track side
    gx0, gx1, gy0, gy1 = 48.0, 62.0, 11.0, 23.0
    gze = 9.5
    gzr = gze + (gy1 - gy0) / 2.0 * KZ * 0.6
    md.add(box(gx0 - 0.5, 8.0, 0, gx1 + 0.5, gy0 + 0.5, 3.4, m_concrete, key='concrete_dk'))
    md.add(gable_body(gx0, gy0, gx1, gy1, 3.0, gze + 3.0, gzr + 3.0, 'x', m_house, wall='w_red', plinth=0.6, clad='v',
                      eave=gze + 3.0, frieze=False,
                      doors={'y-': [(52.0, 58.0, 3.2, 10.5)], 'y+': [(52.0, 58.0, 3.2, 10.5)]}))
    md.add(box(gx0, gy0, 0, gx1, gy1, 3.0, m_concrete, key='concrete_dk'))
    md.add(gable_roof(gx0, gy0, gx1, gy1, gze + 3.0, gzr + 3.0, 'x', m_roof, ov=2.2, ovg=1.0, th=1.2, key='roof_dark',
                      edgekey='trim', seam=3.0))
    # semaphore (opastin) at the platform end
    md.add(box(3.6, 2.4, 1.2, 4.4, 3.2, 26.0, m_flat('steel_lt'), detail=True))
    md.add(box(3.5, 3.2, 22.0, 4.5, 7.8, 23.4, m_flat('sign_red', 0.3), detail=True))
    md.add(box(3.4, 2.3, 26.0, 4.6, 3.3, 26.6, m_flat('cap'), detail=True))
    # forecourt: birches, a bench, a bike rack, a waiting car
    add_birch(md, 4.0, 58.0, h=32, seed=seed + 31)
    add_bush(md, 14.0, 56.0, seed=seed + 33)
    add_bush(md, 36.0, 58.0, seed=seed + 34)
    add_bench(md, 16.0, 31.0)
    md.add(box(20.0, 4.0, 1.2, 25.0, 6.5, 3.2, m_flat('fence_wood')))                 # luggage cart
    md.add(box(20.5, 4.4, 3.2, 24.0, 6.1, 5.0, m_flat('woodpile', 0.3)))
    for i in range(5):
        md.add(box(36.0 + i * 1.6, 28.0, 0, 36.4 + i * 1.6, 31.0, 2.5, m_flat('steel_lt'), detail=True))
    add_car(md, 48.0, 44.0, 'lblue', 'y')
    gr = [(0.0, 0.0, 64.0, 10.0, 'asph_dk'), (0.0, 25.0, 64.0, 35.0, 'gravel'), (24.0, 35.0, 31.0, 64.0, 'gravel'),
          (42.0, 35.0, 56.0, 64.0, 'gravel'), (45.0, 9.0, 64.0, 25.0, 'gravel'), (8.0, 40.0, 20.0, 47.0, 'flowerbed')]
    md.ground = lambda g, st: g_lot(g, st, gravel=gr, var=seed)
    return md


# =========================================================================== bus stop
def bus_stop(name, seed=1):
    """pysäkki (1x1): glazed shelter with a mono roof, bench, stop sign pole, litter bin, a birch; the
    paved waiting area is on the front (+Y) edge by the road"""
    md = Model(name)
    x0, x1, y0, y1 = 8.0, 22.0, 20.0, 25.5
    zt = 8.0
    glass = {'y+': wins_row(x0, x1, 3.6, zt - 0.8, 3.6, 3), 'y-': wins_row(x0, x1, 3.6, zt - 0.8, 3.6, 3)}
    md.add(box(x0, y0, 0, x1, y0 + 0.8, zt, m_house, wall='w_green', plinth=0.6, wins=glass, clad='v', eave=zt,
               frieze=False))
    side = {'x+': [(y0 + 1.0, y1 - 2.0, 3.6, zt - 0.8)], 'x-': [(y0 + 1.0, y1 - 2.0, 3.6, zt - 0.8)]}
    md.add(box(x0, y0, 0, x0 + 0.8, y1 - 1.0, zt, m_house, wall='w_green', plinth=0.6, wins=side, clad='v', eave=zt,
               frieze=False))
    md.add(box(x1 - 0.8, y0, 0, x1, y1 - 1.0, zt, m_house, wall='w_green', plinth=0.6, wins=side, clad='v', eave=zt,
               frieze=False))
    md.add(mono_body(x0 - 0.8, y0 - 0.8, x1 + 0.8, y1 + 0.6, zt, zt + 0.6, zt + 1.8, m_flat('roof_metal', 0.2), rise='y-'))
    add_bench(md, (x0 + x1) / 2, y0 + 1.8)
    md.add(box(25.45, 27.45, 0, 26.35, 28.35, 13.0, m_flat('steel_lt'), detail=True))            # stop sign
    md.add(box(25.2, 27.3, 9.6, 26.6, 28.5, 13.0, m_flat('sign_yel', 0.4), detail=True))
    md.add(box(25.3, 27.4, 8.0, 26.5, 28.4, 9.6, m_flat('sign_blue', 0.3), detail=True))     # timetable plate
    md.add(Cone(5.0, 27.5, 0, 3.2, 0.9, 0.9, m_flat('sign_green'), cap=True))                  # litter bin
    add_birch(md, 26.0, 7.0, h=32, seed=seed + 41)
    add_bush(md, 5.0, 11.0, seed=seed + 42)
    gr = [(2.0, 18.5, 30.0, 32.0, 'asphalt'), (13.0, 9.0, 17.0, 18.5, 'gravel')]
    md.ground = lambda g, st: g_lot(g, st, gravel=gr, var=seed)
    return md


# =========================================================================== bus depot
def bus_depot(name, seed=1):
    """linja-autovarikko (2x2): long garage hall with roller doors on BOTH long sides (drive-through, so the
    art is semantically 180-degree symmetric), office and workshop annexes at opposite ends, a bus on each
    apron, fuel pumps"""
    md = Model(name, 2, 2)
    x0, x1, y0, y1 = 8.0, 56.0, 17.0, 47.0
    ze = 13.0
    zr = ze + 5.0
    dd = [(x0 + 3.0 + i * 12.0, x0 + 11.0 + i * 12.0, 0, 10.5, 'steel') for i in range(4)]
    wh = [(x0 + 3.0 + i * 12.0, x0 + 11.0 + i * 12.0, 11.0, 12.6) for i in range(4)]
    md.add(gable_body(x0, y0, x1, y1, 0, ze, zr, 'x', m_block, wall='render_grey', plinth=0.8, roofkey='roof_dark',
                      doors={'y+': dd, 'y-': dd}, wins={'y+': wh, 'y-': wh,
                                                         'x+': wins_row(y0, y1, 4.0, 9.0, 3.0, 4),
                                                         'x-': wins_row(y0, y1, 4.0, 9.0, 3.0, 4)}))
    md.add(gable_roof(x0, y0, x1, y1, ze, zr, 'x', m_roof, ov=0.8, ovg=0.8, th=1.2, key='corr', edgekey='steel_lt', seam=2.0))
    # office (2 storeys) at the +X end of the front side, workshop at the -X end of the back side
    ow = {'y+': wins_row(x1, 63.0, 3.0, 7.5, 2.5, 2) + wins_row(x1, 63.0, 12.0, 16.5, 2.5, 2),
          'x+': wins_row(y0 + 15, y1, 3.0, 7.5, 2.5, 3) + wins_row(y0 + 15, y1, 12.0, 16.5, 2.5, 3)}
    md.add(box(x1, y0 + 15.0, 0, 63.0, y1, 2 * STOREY + 1.0, m_block, wall='render_yel', plinth=0.8, wins=ow,
               doors={'y+': [(x1 + 1.0, x1 + 3.5, 0.8, 8.0)]}, roofkey='bitumen',
               bands={'x+': [(y0 + 16.0, y1 - 1.0, 9.0, 11.0, 'sign_red', 'text')]}))
    md.add(box(1.0, y0, 0, x0, y0 + 15.0, 9.0, m_block, wall='render_yel', plinth=0.8,
               wins={'x-': wins_row(y0, y0 + 15, 3.0, 7.0, 2.5, 3), 'y-': wins_row(1.0, x0, 3.0, 7.0, 2.5, 1)},
               doors={'x-': [(y0 + 6.0, y0 + 9.0, 0.8, 7.5)]}, roofkey='bitumen'))
    add_bus(md, 30.0, 56.5)
    add_bus(md, 34.0, 7.5)
    for (px, py) in ((54.0, 56.0), (10.0, 8.0)):
        md.add(box(px - 0.8, py - 0.6, 0, px + 0.8, py + 0.6, 5.0, m_flat('sign_red', 0.2)))
        md.add(box(px - 1.0, py - 0.8, 5.0, px + 1.0, py + 0.8, 5.8, m_flat('trim')))
    add_birch(md, 4.0, 60.0, h=30, seed=seed + 51)
    add_birch(md, 60.0, 4.0, h=30, seed=seed + 52)
    gr = [(0.0, 47.0, 64.0, 64.0, 'asphalt'), (0.0, 0.0, 64.0, 17.0, 'asphalt'), (0.0, 32.0, 8.0, 47.0, 'gravel'),
          (56.0, 17.0, 64.0, 32.0, 'gravel')]
    md.ground = lambda g, st: g_lot(g, st, gravel=gr, var=seed)
    return md


# =========================================================================== tables
ENTRIES = [
    ('station', (2, 2), 4, [_v(station, 'st')], None),
    ('bus_stop', (1, 1), 4, [_v(bus_stop, 'bs')], None),
    ('bus_depot', (2, 2), 2, [_v(bus_depot, 'bd')], None),
    ('farm_t1', (1, 1), 1, [_v(farm, 'f1', 1, 0, seed=1), _v(farm, 'f1', 1, 1, seed=2)], None),
    ('farm_t2', (1, 1), 1, [_v(farm, 'f2', 2, 0, seed=3), _v(farm, 'f2', 2, 1, seed=4)], None),
    ('farm_t3', (1, 1), 1, [_v(farm, 'f3', 3, 0, seed=5), _v(farm, 'f3', 3, 1, seed=6)], None),
    ('logging_camp', (1, 1), 1, [_v(logging_camp, 'lc', 0, seed=1), _v(logging_camp, 'lc', 1, seed=2)], None),
]
KEY_ALIASES = [('oil_plant', [('coal_plant', 0)])]     # M7: oil power plant reuses the coal plant art (TASKS_M2 M7)
GROUPS = {'station': GRP_T, 'bus_stop': GRP_T, 'bus_depot': GRP_T,
          'farm_t1': GRP_R, 'farm_t2': GRP_R, 'farm_t3': GRP_R, 'logging_camp': GRP_R,
          'forest_cut*': GRP_R, 'forest_seed*': GRP_R, 'forest_young*': GRP_R, 'field_hay*': GRP_R,
          'bus_px*': GRP_F, 'bus_ny*': GRP_F, 'bus_nx*': GRP_F, 'bus_py*': GRP_F,
          'fire0*': GRP_F, 'fire1*': GRP_F, 'fire2*': GRP_F, 'fire3*': GRP_F}

BUS_DIRS = (('px', 0), ('ny', 1), ('nx', 2), ('py', 3))     # = gen_art.DIRS


def extra_jobs():
    J = []
    for (dn, r) in BUS_DIRS:
        J.append((bus_model, r, 'car', ['bus_%s' % dn]))
    for v in range(2):
        J.append(((lambda vv: lambda: forest_cut(vv))(v), 0, 'lot', ['forest_cut%d' % v]))
        J.append(((lambda vv: lambda: forest_seed(vv))(v), 0, 'lot', ['forest_seed%d' % v]))
        J.append(((lambda vv: lambda: forest_young(vv))(v), 0, 'lot', ['forest_young%d' % v]))
    J.append((field_hay, 0, 'lot', ['field_hay']))
    return J
