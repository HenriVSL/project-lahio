"""art_models.py - materials, trees, props and every building model of the KAUPUNKI world art (T02).

Conventions (STYLE.md 8): front on +Y; heights in px (STOREY = 10); window rects (s0, s1, t0, t1) in
wall coordinates (s along the wall, t height) per face tag 'y+' (front), 'x+', 'x-', 'y-' (back).
Every builder returns a Model whose solids stay inside its lot (strip sorting) and whose ground
uses the shared tile-periodic grass so lot edges match the plain tiles."""
import math

from artlib import (T, KZ, STOREY, clamp, lerp, ihash, vnoise, vnoise3, local_st, Model, Solid,
                    Convex, box, gable_body, hip_body, roof_slab, gable_roof, mono_body, Cone, Ellipsoid,
                    HeightField)
from art_terrain import g_grass, g_lot, g_water, g_road, g_rail


# =========================================================================== materials
def m_flat(key, delta=0.0):
    def f(g, st):
        return key, delta
    return f


def in_rect(s, t, r):
    return r[0] <= s < r[1] and r[2] <= t < r[3]


def window_px(s, t, r, st, kind='win'):
    """(key, delta) for a point inside window rect r=(s0,s1,t0,t1), or None"""
    s0, s1, t0, t1 = r
    if not (s0 <= s < s1 and t0 <= t < t1):
        return None
    fr = 1.0 if st.zoom >= 1 else 2.0
    if s < s0 + fr * 0.999 or s >= s1 - fr * 0.999 or t < t0 + 1 or t >= t1 - 1:
        return 'trim', 0.0
    w = s1 - s0
    if kind == 'win' and w >= 5 and st.zoom >= 1:
        mid = (s0 + s1) / 2.0
        if mid - 0.5 <= s < mid + 0.5:
            return 'trim', 0.0
    if kind == 'win' and (t1 - t0) >= 7 and st.zoom >= 1:
        tm = t0 + (t1 - t0) * 0.62
        if tm - 0.5 <= t < tm + 0.5:
            return 'trim', 0.0
    if kind == 'tall' and st.zoom >= 1:           # school classroom windows: mullion grid
        if (s - s0) % 3.0 < 0.8 or abs(t - (t0 + (t1 - t0) * 0.7)) < 0.5:
            return 'trim', 0.0
    if st.texture > 0.5 and s < s0 + fr + 1 and t >= t1 - 2:
        return 'glass_lt', -0.6
    return 'glass', 0.0


def wins_row(s0, s1, t0, t1, w, n, skip=()):
    """n windows of width w evenly spread over s0..s1"""
    out = []
    if n <= 0:
        return out
    gap = (s1 - s0 - n * w) / float(n + 1)
    for i in range(n):
        a = s0 + gap + i * (w + gap)
        if any(not (a + w <= k0 or a >= k1) for (k0, k1) in skip):
            continue
        out.append((a, a + w, t0, t1))
    return out


def m_house(g, st):
    """wooden wall: boards, corner boards, windows, plinth, doors"""
    p = g.solid.par
    tg = g.tag
    if tg in ('rf+', 'rf-', 'bot') or tg.startswith('h'):
        return p['wall'], 0.0
    s, t = local_st(g)
    b = g.solid.aabb
    lo, hi = (b[1], b[4]) if tg in ('x+', 'x-') else (b[0], b[3])
    if t < p.get('plinth', 2.5):
        return p.get('plinthkey', 'plinth'), (-0.4 if st.texture > 0.5 and int(s * 0.5 + t) % 5 == 0 else 0.0)
    for r in p.get('doors', {}).get(tg, ()):
        if in_rect(s, t, r):
            if s < r[0] + 1 or s >= r[1] - 1 or t >= r[3] - 1:
                return 'trim', 0.0
            return p.get('door', 'door'), 0.0
    for r in p.get('wins', {}).get(tg, ()):
        w = window_px(s, t, r, st)
        if w:
            if w[0] == 'trim' and p.get('frame'):
                return p['frame'], 0.0
            return w
    cb = 1.0 if st.zoom >= 1 else 2.0
    if p.get('corners', True) and (s < lo + cb or s >= hi - cb):
        return p.get('frame') or 'trim', 0.0
    ze = p.get('eave', 99)
    if p.get('frieze', True) and ze - 1.0 <= t < ze and st.zoom >= 1:
        return p.get('frame') or 'trim', -0.3
    d = 0.0
    if st.boards > 0 and st.zoom >= 1:
        if p.get('clad', 'h') == 'h':
            if int(math.floor(t)) % 3 == 0:
                d = -0.55 * st.boards
        else:
            if int(math.floor(s)) % 3 == 0:
                d = -0.55 * st.boards
    return p['wall'], d


def m_roof(g, st):
    p = g.solid.par
    tg = g.tag
    if tg in ('end-', 'end+', 'fascia'):
        return p.get('edgekey', 'trim'), 0.0
    if tg == 'under':
        return p.get('edgekey', 'trim'), -1.5
    if tg == 'ridge':
        return p.get('key', 'roof_metal'), 0.5
    P = g.P
    axis = p['ridge_axis']
    a = P[0] if axis == 'x' else P[1]
    bb = P[1] if axis == 'x' else P[0]
    key = p.get('key', 'roof_metal')
    if abs(bb - p['mid']) < 1.0:
        return key, 0.8                      # ridge cap
    lad = p.get('ladder')
    if lad is not None and p['sg'] == lad[1] and abs(a - lad[0]) < (0.6 if st.zoom >= 1 else 1.0):
        return 'roof_dark', -0.4              # roof ladder (kattotikkaat) up to the chimney
    sp = p.get('seam', 3.0) if st.zoom >= 1 else p.get('seam', 3.0) * 2
    if p.get('seams', True) and (a % sp) < (1.0 if st.zoom >= 1 else 2.0):
        return key, 0.6
    if key == 'roof_felt' and st.zoom >= 1 and (a % 4.0) < 0.8:   # triangular battens
        return key, 0.5
    return key, 0.0


def m_hiproof(g, st):
    """hipped roof planes of a hip_body (tags hx+/hx-/hy+/hy-) + wall material for the rest"""
    p = g.solid.par
    tg = g.tag
    if tg.startswith('h'):
        key = p.get('roofkey', 'roof_metal')
        P = g.P
        a = P[0] if tg in ('hy+', 'hy-') else P[1]
        sp = 3.0 if st.zoom >= 1 else 6.0
        if p.get('seams', True) and (a % sp) < (1.0 if st.zoom >= 1 else 2.0):
            return key, 0.6
        return key, 0.0
    return p.get('wallmat', m_block)(g, st)


def m_chimney(g, st):
    p = g.solid.par
    if g.tag == 'top':
        return 'cap', -1.0
    if g.P[2] > g.solid.aabb[5] - 1.6:
        return 'cap', 0.0
    key = p.get('key', 'brick')
    if key == 'brick' and st.texture > 0.5 and st.zoom >= 1:
        s, t = local_st(g)
        if int(math.floor(t)) % 2 == 0 and int(math.floor(s + (int(t) // 2) % 2)) % 3 == 0:
            return key, -0.6
    return key, 0.0


def m_block(g, st):
    """masonry / concrete block walls with window grid, panel joints, doors, bands, brickwork"""
    p = g.solid.par
    tg = g.tag
    wall = p.get('wall', 'render_white')
    if tg == 'top':
        b = g.solid.aabb
        P = g.P
        e = p.get('parapet', 1.2)
        if P[0] < b[0] + e or P[0] > b[3] - e or P[1] < b[1] + e or P[1] > b[4] - e:
            return p.get('parapetkey', 'parapet'), 0.3
        d = 0.0
        if st.texture > 0.5 and ihash(int(P[0] / 2), int(P[1] / 2), 3) < 0.15:
            d = -0.4
        return p.get('roofkey', 'bitumen'), d
    if tg in ('rf+', 'rf-') or tg.startswith('h'):
        return p.get('roofkey', 'roof_metal'), 0.0
    s, t = local_st(g)
    b = g.solid.aabb
    lo, hi = (b[1], b[4]) if tg in ('x+', 'x-') else (b[0], b[3])
    if t < p.get('plinth', 3.0):
        return p.get('plinthkey', 'plinth'), 0.0
    for r in p.get('doors', {}).get(tg, ()):
        if in_rect(s, t, r):
            if t >= r[3] - 1:
                return 'trim', 0.0
            if len(r) > 4:
                k = r[4]
                if k in ('firedoor', 'steel', 'door_red') and st.zoom >= 1 and (t % 2.0) < 0.7:
                    return k, -0.5
                return k, 0.0
            return p.get('door', 'door'), 0.0
    for r in p.get('bands', {}).get(tg, ()):
        if in_rect(s, t, r[:4]):
            if len(r) > 5 and r[5] == 'text' and st.zoom >= 1:
                rel_s, rel_t = s - r[0] - 1.5, t - r[2] - (r[3] - r[2]) / 2.0 + 0.8
                if 0 <= rel_t < 1.6 and 0 <= rel_s < (r[1] - r[0] - 3.0):
                    col = int(rel_s)
                    if col % 3 != 2 and ihash(col, int(rel_t * 2), int(r[0])) < 0.8:
                        return 'sign_txt', 0.3
            return r[4], 0.0
    for r in p.get('wins', {}).get(tg, ()):
        w = window_px(s, t, r, st, p.get('wkind', 'block'))
        if w:
            if w[0] == 'trim':
                return p.get('frame') or 'trim', w[1]
            return w
    if p.get('joints') and st.zoom >= 1:
        jh, jv = p['joints']
        z0 = p.get('plinth', 3.0)
        if (t - z0) % jh < 0.999 and t > z0 + 1:
            return wall, -0.7
        if (s - lo) % jv < 0.999:
            return wall, -0.7
    if p.get('brickwork') and st.zoom >= 1:
        row = int(math.floor(t))
        if row % 2 == 0 and int(math.floor(s + (row // 2) % 2 * 1.5)) % 3 == 0:
            return wall, -0.5
    if p.get('speckle') and st.zoom >= 1 and ihash(int(s * 1.7), int(t * 1.7), 13) < 0.12:
        return wall, -0.4
    cb = p.get('corner')
    if cb and (s < lo + 1.0 or s >= hi - 1.0):
        return cb, 0.0
    d = 0.0
    if st.texture > 0.8 and st.zoom >= 1:
        d = (vnoise3(s, t, 0, 3.0, 11) - 0.5) * 0.5
    return wall, d


def m_balcony(g, st):
    p = g.solid.par
    if g.tag == 'top':
        return 'panel_dk', 0.2
    if g.tag == 'bot':
        return 'panel_dk', -1.0
    key = p.get('key', 'balc_white')
    if p.get('rail') and st.zoom >= 1 and g.tag in ('y+', 'x+', 'x-'):
        s, t = local_st(g)
        if (s % 1.5) < 0.6:
            return key, -0.6
    return key, 0.0


def m_brickwall(g, st):
    p = g.solid.par
    tg = g.tag
    if tg == 'top':
        return p.get('roofkey', 'bitumen'), 0.0
    s, t = local_st(g)
    for r in p.get('wins', {}).get(tg, ()):
        if in_rect(s, t, r):
            if s < r[0] + 1 or s >= r[1] - 1 or t < r[2] + 1 or t >= r[3] - 1:
                return ('brick_dk' if p.get('wall', 'brick') == 'brick' else 'steel'), 0.0
            if (s - r[0]) % 3 < 1.0:
                return 'steel', 0.0
            return 'factory_glass', 0.0
    for r in p.get('doors', {}).get(tg, ()):
        if in_rect(s, t, r):
            k = r[4] if len(r) > 4 else 'steel'
            if (t % 2) < 1.0 and st.texture > 0.3:
                return k, -0.4
            return k, 0.0
    key = p.get('wall', 'brick')
    if st.texture > 0.4 and st.zoom >= 1 and key == 'brick':
        row = int(math.floor(t / 1.0))
        if row % 2 == 0 and int(math.floor(s + (row // 2) % 2 * 1.5)) % 3 == 0:
            return 'brick', -0.5
    if t > g.solid.aabb[5] - 1.5:
        return ('brick_dk' if key == 'brick' else key), 0.3
    return key, 0.0


def m_corr(g, st):
    """corrugated steel / vertical board shed walls"""
    p = g.solid.par
    tg = g.tag
    key = p.get('wall', 'corr')
    if tg in ('top', 'rf+', 'rf-'):
        return p.get('roofkey', 'roof_dark'), 0.0
    s, t = local_st(g)
    for r in p.get('doors', {}).get(tg, ()):
        if in_rect(s, t, r):
            k = r[4] if len(r) > 4 else 'steel'
            if st.zoom >= 1 and (t % 2.0) < 0.7:
                return k, -0.5
            return k, 0.0
    for r in p.get('wins', {}).get(tg, ()):
        if in_rect(s, t, r):
            if s < r[0] + 1 or s >= r[1] - 1 or t < r[2] + 1 or t >= r[3] - 1:
                return p.get('frame', 'trim'), 0.0
            return 'factory_glass', 0.0
    if t < p.get('plinth', 1.5):
        return 'concrete_dk', 0.0
    if st.zoom >= 1 and (s % 2.0) < 0.8:
        return key, -0.5
    return key, 0.0


def m_cone_brick(g, st):
    if g.tag == 'cap':
        return 'cap', -1.5
    z = g.P[2]
    top = g.solid.aabb[5]
    if z > top - 3:
        return 'cap', 0.0
    if top - 12 < z < top - 10:
        return 'brick_dk', 0.0
    if st.texture > 0.4 and st.zoom >= 1:
        if int(z) % 2 == 0 and ihash(int(z), int((g.P[0] - g.P[1]) * 1.3), 5) < 0.25:
            return 'brick', -0.5
    return 'brick', 0.0


def m_concrete_round(g, st):
    p = g.solid.par
    key = p.get('key', 'render_white')
    if g.tag == 'cap':
        return p.get('capkey', key), 0.2
    if g.tag == 'capb':
        return p.get('capkey', key), -1.0
    if p.get('ribs') and st.zoom >= 1:
        b = g.solid.c
        ang = math.atan2(g.P[1] - b[1], g.P[0] - b[0])
        if (ang * p['ribs'] / (2 * math.pi)) % 1.0 < 0.22:
            return key, -0.6
    if p.get('band') and p['band'][0] <= g.P[2] < p['band'][1]:
        return p['band'][2], 0.0
    if p.get('wins'):
        b = g.solid.c
        ang = math.atan2(g.P[1] - b[1], g.P[0] - b[0])
        for (z0, z1, n) in p['wins']:
            if z0 <= g.P[2] < z1 and (ang * n / (2 * math.pi)) % 1.0 < 0.35:
                return 'glass', 0.0
    return key, 0.0


def m_leaf(g, st):
    p = g.solid.par
    key = p['key']
    P = g.P
    d = 0.0
    if st.texture > 0.2:
        cell = 1.6 if st.zoom >= 1 else 3.2
        nz = ihash(int(math.floor(P[0] / cell)), int(math.floor(P[1] / cell)), int(math.floor(P[2] / cell)),
                   p.get('seed', 1))
        d = (nz - 0.5) * 1.6 * st.texture
    if p.get('fruit') and st.zoom >= 1:
        if ihash(int(P[0] * 1.1), int(P[1] * 1.1), int(P[2] * 1.1), 99) < 0.07 and g.n[2] > -0.2:
            return p['fruit'], 0.4
    return key, d


def m_trunk(g, st):
    p = g.solid.par
    key = p.get('key', 'bark')
    if key == 'bark_birch':
        z = g.P[2]
        if int(z) % 3 == 0 and ihash(int(z), 3, p.get('seed', 0)) < 0.45:
            return 'bark_dk', 0.0
    return key, 0.0


def m_spruce(g, st):
    p = g.solid.par
    P = g.P
    d = 0.0
    if g.tag == 'side' and st.texture > 0.2:
        b = g.solid.c
        ang = math.atan2(P[1] - b[1], P[0] - b[0])
        nz = ihash(int((ang + 4) * 4.5), int(P[2] / 1.5), p.get('seed', 0))
        d = (nz - 0.5) * 1.4 * st.texture
        if P[2] < b[2] + 1.6:
            d -= 0.8
    return 'spruce', d


def m_rock(g, st):
    P = g.W
    n1 = vnoise(P[0], P[1], 4.0, 21)
    d = (n1 - 0.5) * 0.9 * st.texture - 0.4
    if vnoise(P[0], P[1], 3.0, 22) > 0.70 and st.texture > 0.3:
        return 'lichen', d * 0.5
    if st.texture > 0.5 and ihash(int(P[0] * 1.5), int(P[1] * 1.5), 23) < 0.06:
        return 'rock_dk', 0.0
    return 'rock', d


def m_flag(g, st):
    """Finnish flag on the +X face of a thin box: white with a blue cross"""
    p = g.solid.par
    a = g.P[1] - p['fy0']
    b = g.P[2] - p['fz0']
    if 1.6 <= a < 2.8 or 1.7 <= b < 2.8:
        return 'flag_blue', 0.6
    return 'trim', 0.2


def m_awning(g, st):
    key = g.solid.par.get('key', 'awning')
    if g.tag in ('y+', 'x+', 'x-', 'top'):
        s = g.P[0] if g.tag in ('y+', 'top') else g.P[1]
        if g.tag in ('y+', 'top'):
            return (key if int(s / 2.0) % 2 == 0 else 'trim'), 0.0
        return key, 0.0
    return key, -1.0


def m_saw(g, st):
    if g.tag == 'glass':
        s, t = g.P[1], g.P[2]
        if (s % 4.0) < 1.0 or t < g.solid.aabb[2] + 1.0:
            return 'steel', 0.0
        return 'factory_glass', (0.7 if (s % 4.0) < 2.0 else 0.0)
    if g.tag == 'slope':
        return 'roof_dark', (0.5 if (g.P[1] % 3.0) < 1.0 and st.zoom >= 1 else 0.0)
    if g.tag in ('y+', 'y-'):
        return g.solid.par.get('end', 'brick'), 0.0
    return 'roof_dark', 0.0


def m_timber(g, st):
    """stickered board stacks: light sawn timber, darker sticker lines, end grain on x faces"""
    if g.tag in ('x+', 'x-'):
        s, t = local_st(g)
        if st.zoom >= 1 and (t % 2.0) < 0.6:
            return 'woodpile', 0.0
        return 'logend', 0.0
    if g.tag == 'top':
        return 'timber', 0.3
    s, t = local_st(g)
    if (t % 4.0) < 0.8:
        return 'woodpile', -0.4
    return 'timber', 0.0


def m_logs(g, st):
    """log pile seen as a box: bark on top/sides, round cut ends on +X/-X"""
    if g.tag in (('y+', 'y-') if g.solid.par.get('ends') == 'y' else ('x+', 'x-')):
        s, t = local_st(g)
        cs, ct = (s % 2.4) - 1.2, (t % 2.4) - 1.2
        if cs * cs + ct * ct < 1.1:
            return 'logend', 0.2
        return 'bark', -0.6
    return 'bark', (ihash(int(g.P[0]), int(g.P[1] * 0.8), 41) - 0.5) * 0.8


def m_woodpile(g, st):
    """firewood stack (halkopino): log ends on the long faces"""
    s, t = local_st(g)
    if g.tag in ('y+', 'y-', 'x+', 'x-'):
        if ihash(int(s * 1.4), int(t * 1.4), 43) < 0.5:
            return 'logend', 0.0
        return 'woodpile', -0.3
    return 'woodpile', 0.0


def m_concrete(g, st):
    p = g.solid.par
    key = p.get('key', 'concrete')
    if g.tag == 'top':
        return key, 0.2
    if p.get('joints') and st.zoom >= 1:
        s, t = local_st(g)
        if (s % p['joints']) < 0.8:
            return key, -0.6
    return key, 0.0


# =========================================================================== trees and props
def new_grp():
    Solid._gid[0] += 1
    return Solid._gid[0]


def add_birch(md, x, y, h=30.0, seed=1, crown=1.0, autumn=False):
    key = 'birch_leaf_au' if autumn else 'birch_leaf'
    gp = new_grp()
    md.add(Cone(x, y, 0, h * 0.86, 0.8, 0.45, m_trunk, cap=False, key='bark_birch', seed=seed, grp=gp))
    n = int(34 * crown)
    R = 4.8 * crown
    for i in range(n):
        a = ihash(i, seed, 1) * 2 * math.pi
        u = ihash(i, seed, 3)
        z = h * (0.30 + 0.70 * u)
        prof = math.sin(math.pi * (0.15 + 0.85 * u)) ** 0.8
        r = (0.35 + 0.65 * math.sqrt(ihash(i, seed, 2))) * R * prof
        rr = 1.3 + ihash(i, seed, 4) * 0.8
        z -= (r / R) * 2.5
        md.add(Ellipsoid(x + math.cos(a) * r, y + math.sin(a) * r, z, rr, rr, rr * KZ * 1.15, m_leaf,
                         key=key, seed=seed + i, grp=gp))


def add_spruce(md, x, y, h=40.0, seed=2, r=6.5):
    gp = new_grp()
    md.add(Cone(x, y, 0, h * 0.3, 0.8, 0.6, m_trunk, cap=False, key='bark', grp=gp))
    for i in range(5):
        z0 = h * (0.10 + 0.16 * i)
        z1 = min(h, z0 + h * 0.42)
        rr = r * (1.0 - 0.16 * i) * (0.92 + 0.16 * ihash(i, seed, 5))
        md.add(Cone(x, y, z0, z1, rr, 0.2, m_spruce, cap=True, seed=seed + i, grp=gp))


def add_pine(md, x, y, h=42.0, seed=3):
    gp = new_grp()
    md.add(Cone(x, y, 0, h * 0.84, 0.95, 0.55, m_trunk, cap=False, key='bark_pine', grp=gp))
    for i in range(11):
        a = ihash(i, seed, 1) * 2 * math.pi
        r = math.sqrt(ihash(i, seed, 2)) * 4.8
        z = h * 0.70 + ihash(i, seed, 3) * h * 0.26
        rr = 2.2 + ihash(i, seed, 4) * 1.4
        md.add(Ellipsoid(x + math.cos(a) * r, y + math.sin(a) * r, z, rr * 1.25, rr * 1.25, rr * 0.7 * KZ,
                         m_leaf, key='pine_leaf', seed=seed + i, grp=gp))


def add_apple(md, x, y, h=16.0, seed=4, fruit=True):
    gp = new_grp()
    md.add(Cone(x, y, 0, h * 0.5, 0.8, 0.6, m_trunk, cap=False, key='bark', grp=gp))
    for i in range(14):
        a = ihash(i, seed, 1) * 2 * math.pi
        r = math.sqrt(ihash(i, seed, 2)) * 3.6
        z = h * 0.58 + ihash(i, seed, 3) * h * 0.36
        rr = 1.7 + ihash(i, seed, 4) * 1.0
        md.add(Ellipsoid(x + math.cos(a) * r, y + math.sin(a) * r, z, rr, rr, rr * KZ * 0.85,
                         m_leaf, key='apple_leaf', seed=seed + i, fruit='apple' if fruit else None, grp=gp))


def add_rowan(md, x, y, h=22.0, seed=6):
    """pihlaja: slim trunk, oval crown with red berry clusters"""
    gp = new_grp()
    md.add(Cone(x, y, 0, h * 0.6, 0.6, 0.4, m_trunk, cap=False, key='bark', grp=gp))
    for i in range(16):
        a = ihash(i, seed, 1) * 2 * math.pi
        u = ihash(i, seed, 3)
        r = math.sqrt(ihash(i, seed, 2)) * 3.2 * math.sin(math.pi * (0.2 + 0.8 * u))
        z = h * (0.45 + 0.5 * u)
        rr = 1.4 + ihash(i, seed, 4) * 0.8
        md.add(Ellipsoid(x + math.cos(a) * r, y + math.sin(a) * r, z, rr, rr, rr * KZ, m_leaf,
                         key='rowan_leaf', seed=seed + i, fruit='rowan_berry', grp=gp))


def add_bush(md, x, y, seed=5, key='bush', size=1.0):
    gp = new_grp()
    for i in range(5):
        a = ihash(i, seed, 1) * 2 * math.pi
        r = ihash(i, seed, 2) * 1.8 * size
        rr = (1.5 + ihash(i, seed, 4) * 0.8) * size
        md.add(Ellipsoid(x + math.cos(a) * r, y + math.sin(a) * r, rr * KZ * 0.7 + ihash(i, seed, 3) * 1.2,
                         rr, rr, rr * KZ * 0.8, m_leaf, key=key, seed=seed + i, grp=gp))


def add_hedge(md, x0, y0, x1, y1, h=4.0, key='hedge'):
    md.add(box(x0, y0, 0, x1, y1, h, m_leaf, key=key, seed=7))


def add_fence(md, x0, y0, x1, y1, gaps=(), key='fence', h=3.5, step=2.0):
    """picket fence along a straight axis-aligned line"""
    L = math.hypot(x1 - x0, y1 - y0)
    n = int(L / step)
    dx, dy = (x1 - x0) / L, (y1 - y0) / L
    for i in range(n + 1):
        d = i * step
        if any(a <= d <= b for (a, b) in gaps):
            continue
        cx, cy = x0 + dx * d, y0 + dy * d
        md.add(box(cx - 0.45, cy - 0.45, 0, cx + 0.45, cy + 0.45, h, m_flat(key), detail=True, noshadow=True))
    segs = []
    pos = 0.0
    for (a, b) in sorted(gaps) + [(L, L)]:
        if a > pos:
            segs.append((pos, a))
        pos = b
    for (a, b) in segs:
        ax, ay = x0 + dx * a, y0 + dy * a
        bx, by = x0 + dx * b, y0 + dy * b
        md.add(box(min(ax, bx) - 0.3, min(ay, by) - 0.3, h - 1.3, max(ax, bx) + 0.3, max(ay, by) + 0.3, h - 0.7,
                   m_flat(key, -0.5), detail=True, noshadow=True))


def add_flagpole(md, x, y, h=30.0, flag=False):
    md.add(box(x - 0.55, y - 0.55, 0, x + 0.55, y + 0.55, h, m_flat('trim'), detail=True))
    if flag:
        md.add(box(x - 0.1, y + 0.3, h - 5.5, x + 0.1, y + 5.8, h - 1.0, m_flag, fy0=y + 0.3, fz0=h - 5.5))


def add_woodpile(md, x0, y0, x1, y1, h=4.0):
    md.add(box(x0, y0, 0, x1, y1, h, m_woodpile))
    md.add(box(x0 - 0.4, y0 - 0.4, h, x1 + 0.4, y1 + 0.4, h + 0.8, m_flat('roof_dark')))


def add_bench(md, x, y, axis='x'):
    if axis == 'x':
        md.add(box(x - 2.0, y - 0.7, 1.4, x + 2.0, y + 0.7, 2.0, m_flat('fence_wood'), detail=True))
        md.add(box(x - 2.0, y - 0.9, 2.0, x + 2.0, y - 0.6, 3.6, m_flat('fence_wood', -0.3), detail=True))
    else:
        md.add(box(x - 0.7, y - 2.0, 1.4, x + 0.7, y + 2.0, 2.0, m_flat('fence_wood'), detail=True))
        md.add(box(x - 0.9, y - 2.0, 2.0, x - 0.6, y + 2.0, 3.6, m_flat('fence_wood', -0.3), detail=True))


def add_carpet_rack(md, x, y):
    """mattoteline: two posts and a bar (iconic courtyard prop)"""
    for dx in (-2.5, 2.5):
        md.add(box(x + dx - 0.3, y - 0.3, 0, x + dx + 0.3, y + 0.3, 6.0, m_flat('steel_lt'), detail=True))
    md.add(box(x - 2.8, y - 0.25, 5.5, x + 2.8, y + 0.25, 6.0, m_flat('steel_lt'), detail=True))


def add_sandbox(md, x, y):
    md.add(box(x - 3.0, y - 3.0, 0, x + 3.0, y + 3.0, 1.0, m_flat('fence_wood')))
    md.add(box(x - 2.4, y - 2.4, 0, x + 2.4, y + 2.4, 1.1, m_flat('sandpile', 0.3)))


def add_swings(md, x, y):
    for dx in (-3.0, 3.0):
        md.add(box(x + dx - 0.3, y - 1.5, 0, x + dx + 0.3, y - 1.0, 9.0, m_flat('steel'), detail=True))
        md.add(box(x + dx - 0.3, y + 1.0, 0, x + dx + 0.3, y + 1.5, 9.0, m_flat('steel'), detail=True))
    md.add(box(x - 3.3, y - 0.3, 8.6, x + 3.3, y + 0.3, 9.2, m_flat('steel'), detail=True))
    for dx in (-1.4, 1.4):
        md.add(box(x + dx - 0.6, y - 0.4, 2.2, x + dx + 0.6, y + 0.4, 2.7, m_flat('balc_red'), detail=True))


# --------------------------------------------------------------------------- the Lada
def m_car(g, st):
    body = g.solid.par['body']
    P = g.P
    tg = g.tag
    cy = g.solid.par.get('cy', 16.0)
    dy = abs(P[1] - cy)
    if tg == 'x+':
        if dy > 1.4 and 2.4 <= P[2] < 3.6:
            return 'headlight', 0.6
        if dy <= 1.4 and 2.2 <= P[2] < 3.6:
            return 'cap', 0.0
        return body, 0.0
    if tg == 'x-':
        if dy > 1.5 and 2.6 <= P[2] < 3.7:
            return 'taillight', 0.3
        return body, 0.0
    if tg in ('y+', 'y-'):
        if P[2] < 1.9:
            return body, -1.2
        return body, 0.0
    if tg == 'top':
        return body, 0.2
    return body, -1.0


def m_cabin(g, st):
    body = g.solid.par['body']
    P = g.P
    tg = g.tag
    bp = g.solid.par.get('bp', 15.0)
    if tg == 'top':
        return body, 0.7
    if P[2] < 4.5:
        return body, 0.0
    if tg == 'ws':
        return ('glass_lt', 0.0) if P[2] > 6.3 else ('glass', 0.0)
    if tg == 'rw':
        return 'glass', -0.3
    if tg in ('y+', 'y-'):
        if bp <= P[0] < bp + 1.0:
            return body, 0.0
        return 'glass', -0.4
    return body, 0.0


def add_car(md, cx, cy, colour, heading='x'):
    """VAZ-2101 style three-box saloon centred at (cx, cy), heading +X ('x') or +Y ('y').
    Drawn 14 x 6.2 units, 7.4 px tall: exaggerated like the houses (STYLE.md 9)."""
    body = 'car_' + colour
    gp = new_grp()
    if heading == 'x':
        ox, oy = cx - 16.0, cy - 16.0
        x0, x1, y0, y1 = 9.0 + ox, 23.0 + ox, 12.9 + oy, 19.1 + oy
        for wx in (10.4, 19.2):
            md.add(box(wx + ox, y0 + 0.2, 0.0, wx + ox + 2.4, y1 - 0.2, 2.2, m_flat('tyre'), grp=gp))
        md.add(box(x0, y0, 1.2, x1, y1, 4.2, m_car, body=body, cy=cy, grp=gp))
        for (a, b) in ((x0 - 0.6, x0 + 0.1), (x1 - 0.1, x1 + 0.6)):
            md.add(box(a, y0 - 0.2, 1.5, b, y1 + 0.2, 2.4, m_flat('chrome', 0.5), grp=gp))
        zb, zt = 4.2, 7.4
        k = 1.0 / (zt - zb)
        tk = 0.45 / (zt - zb)
        xr, xf = 12.2 + ox, 18.6 + ox
        pl = [(0, 0, -1, -zb, 'bot'), (0, 0, 1, zt, 'top'),
              (1, 0, k, xf + k * zb, 'ws'), (-1, 0, k, -xr + k * zb, 'rw'),
              (0, 1, tk, y1 - 0.4 + tk * zb, 'y+'), (0, -1, tk, -(y0 + 0.4) + tk * zb, 'y-')]
        md.add(Convex(pl, (xr, y0, zb, xf, y1, zt), m_cabin, body=body, bp=15.0 + ox, grp=gp))
    else:
        # heading +Y: swap roles of the axes; build with explicit planes
        ox, oy = cx - 16.0, cy - 16.0
        y0, y1, x0, x1 = 9.0 + oy, 23.0 + oy, 12.9 + ox, 19.1 + ox
        for wy in (10.4, 19.2):
            md.add(box(x0 + 0.2, wy + oy, 0.0, x1 - 0.2, wy + oy + 2.4, 2.2, m_flat('tyre'), grp=gp))
        md.add(box(x0, y0, 1.2, x1, y1, 4.2, m_flat(body), grp=gp))
        for (a, b) in ((y0 - 0.6, y0 + 0.1), (y1 - 0.1, y1 + 0.6)):
            md.add(box(x0 - 0.2, a, 1.5, x1 + 0.2, b, 2.4, m_flat('chrome', 0.5), grp=gp))
        zb, zt = 4.2, 7.4
        k = 1.0 / (zt - zb)
        tk = 0.45 / (zt - zb)
        yr, yf = 12.2 + oy, 18.6 + oy
        pl = [(0, 0, -1, -zb, 'bot'), (0, 0, 1, zt, 'top'),
              (0, 1, k, yf + k * zb, 'ws'), (0, -1, k, -yr + k * zb, 'rw'),
              (1, 0, tk, x1 - 0.4 + tk * zb, 'x+'), (-1, 0, tk, -(x0 + 0.4) + tk * zb, 'x-')]
        md.add(Convex(pl, (x0, yr, zb, x1, yf, zt), m_cabin_y, body=body, grp=gp))


def m_cabin_y(g, st):
    body = g.solid.par['body']
    P = g.P
    tg = g.tag
    if tg == 'top':
        return body, 0.7
    if P[2] < 4.5:
        return body, 0.0
    if tg in ('ws', 'rw'):
        return ('glass_lt', 0.0) if (tg == 'ws' and P[2] > 6.3) else ('glass', -0.2)
    return 'glass', -0.4


CAR_COLOURS = ['white', 'beige', 'yellow', 'orange', 'lblue', 'dblue', 'red', 'green', 'brown',
               'mustard', 'grey', 'aubergine']


def car_model(colour):
    md = Model('car', ground=None)
    add_car(md, 16.0, 16.0, colour, 'x')
    md.top = 14.0
    return md


# =========================================================================== nature models
def tree_model(kind, seed=1):
    md = Model('tree_' + kind, ground=None)
    if kind == 'birch':
        add_birch(md, 16, 16, h=36, seed=seed)
    elif kind == 'birch_au':
        add_birch(md, 16, 16, h=34, seed=seed, autumn=True)
    elif kind == 'spruce':
        add_spruce(md, 16, 16, h=44, seed=seed)
    elif kind == 'pine':
        add_pine(md, 16, 16, h=46, seed=seed)
    elif kind == 'rowan':
        add_rowan(md, 16, 16, h=24, seed=seed)
    else:
        add_apple(md, 16, 16, h=18, seed=seed)
    return md


def forest(kinds, seed=1, autumn=False):
    md = Model('forest')
    spots = [(9.0, 9.0), (23.0, 8.0), (8.0, 23.0), (22.0, 22.0), (16.0, 15.5)]
    for i, (x, y) in enumerate(spots[:len(kinds)]):
        x += (ihash(i, seed, 1) - 0.5) * 4
        y += (ihash(i, seed, 2) - 0.5) * 4
        x = clamp(x, 6.0, 26.0)
        y = clamp(y, 6.0, 26.0)
        k = kinds[i]
        if k == 'birch':
            add_birch(md, x, y, h=30 + 8 * ihash(i, seed, 3), seed=seed * 10 + i, autumn=autumn and i % 2 == 0)
        elif k == 'spruce':
            add_spruce(md, x, y, h=34 + 10 * ihash(i, seed, 3), seed=seed * 10 + i, r=5.5)
        elif k == 'pine':
            add_pine(md, x, y, h=38 + 8 * ihash(i, seed, 3), seed=seed * 10 + i)
        elif k == 'bush':
            add_bush(md, x, y, seed=seed * 10 + i)
        elif k == 'rowan':
            add_rowan(md, x, y, seed=seed * 10 + i)
    md.ground = lambda g, st: g_forest_floor(g, st, seed)
    return md


def g_forest_floor(g, st, var):
    """forest floor: grass with darker moss patches (interior only, seams match plain grass)"""
    x, y = g.W[0], g.W[1]
    from artlib import edge_window
    if vnoise(x, y, 5.0, 300 + var) * edge_window(x, y) > 0.42:
        return 'grass_dk', -0.2
    return g_grass(g, st, var)


def m_rock2(g, st):
    """silokallio: smooth grey-pink granite, lichen patches, a few dark cracks"""
    P = g.W
    n1 = vnoise(P[0], P[1], 4.0, 21)
    d = (n1 - 0.5) * 0.7 * st.texture - 0.2
    if st.zoom >= 1 and abs(((P[0] * 0.8 + P[1] * 0.35) % 9.0) - 4.5) < 0.35 and vnoise(P[0], P[1], 3.0, 24) > 0.5:
        return 'rock_dk', 0.0
    if vnoise(P[0], P[1], 3.0, 22) > 0.68:
        return 'lichen', d * 0.5
    return 'rock', d


def rock_tile(seed=1):
    """silokallio: low glacier-smoothed granite whalebacks with a boulder, lichen and a pine"""
    md = Model('rock')
    gp = new_grp()
    blobs = [((14.0, 15.0, 11.0, 4.5), (8.0, 23.0, 6.0, 2.8), (23.0, 9.0, 7.0, 3.6), (22.0, 22.0, 5.0, 2.0)),
             ((13.0, 18.0, 10.0, 4.0), (21.0, 11.0, 8.0, 4.2), (8.0, 8.0, 5.5, 2.4), (23.0, 24.0, 4.5, 1.8))][seed % 2]

    def h(x, y):
        v = 0.0
        for (cx, cy, r, a) in blobs:
            dx, dy = (x - cx) / (r * 1.15), (y - cy) / r
            d = dx * dx + dy * dy
            if d < 1:
                v = max(v, a * (1 - d) ** 0.5)
        return v * (0.94 + 0.12 * vnoise(x, y, 4.0, seed + 90))

    md.add(HeightField(1.0, 1.0, 31.0, 31.0, 6.0, h, m_rock2, grp=gp))
    md.add(Ellipsoid(26.0, 25.5, 1.6, 2.4, 2.0, 2.4 * KZ, m_rock2, grp=new_grp()))
    add_bush(md, 27.0, 15.0, seed=seed + 3, size=0.7)
    if seed % 2:
        add_pine(md, 24.5, 6.5, h=26, seed=seed + 40)
    else:
        add_spruce(md, 6.0, 25.0, h=22, seed=seed + 41, r=3.8)
    md.ground = lambda g, st: g_grass(g, st, seed)
    return md


# =========================================================================== houses
def rintama(name, wall='w_yellow', ridge='x', storeys=1.5, chim=0.5, chimkey='brick', porch='gable',
            porch_at=0.22, dormer=False, roof='roof_metal', clad='h', tree='apple', tree_at=(3.5, 28.0),
            shed=True, fence='fence', bush=True, seed=1, door='door', autumn=False, flagpole=None,
            woodpile=False, ladder=True, garage=False, car=None, potato=False, frame=None):
    """rintamamiestalo on a 1x1 lot; canonical front (street side) = +Y.  storeys 1.5 (T2) or 2 (T3)."""
    pl = 2.5
    big = storeys >= 1.75
    if garage:
        if ridge == 'x':
            x0, x1, y0, y1 = 4.5, 22.5, 4.0, 20.0
        else:
            x0, x1, y0, y1 = 5.0, 21.0, 3.5, 20.5
    elif ridge == 'x':
        x0, x1, y0, y1 = 7.0, 25.0, 6.0, 22.0
    else:
        x0, x1, y0, y1 = 8.0, 24.0, 5.0, 22.0
    ze = pl + (9.5 if not big else 18.5)
    half = ((y1 - y0) if ridge == 'x' else (x1 - x0)) / 2.0
    zr = ze + half * KZ * (0.92 if big else 1.0)
    mid = (y0 + y1) / 2.0 if ridge == 'x' else (x0 + x1) / 2.0
    wins = {'x+': [], 'y+': [], 'x-': [], 'y-': []}
    doors = {'x+': [], 'y+': []}
    wt0, wt1 = pl + 2.0, pl + 8.5
    rows = [(wt0, wt1)] + ([(ze - 8.0, ze - 1.5)] if big else [])
    pw, pd = 6.0, 4.5
    pcx = lerp(x0 + 4.5, x1 - 4.5, porch_at)
    px0, px1 = pcx - pw / 2, pcx + pw / 2
    md = Model(name)
    for (t0, t1) in rows:
        if ridge == 'x':
            xs = [x0 + 3.0, (x0 + x1) / 2.0 - 2.5, x1 - 8.0]
        else:
            xs = [x0 + 2.5, x1 - 7.5]
        for sx in xs:
            r = (sx, sx + 5.0, t0, t1)
            wins['y-'].append(r)
            if porch and t0 < 12 and not (r[1] <= px0 - 0.5 or r[0] >= px1 + 0.5):
                continue
            wins['y+'].append(r)
    for (t0, t1) in rows:
        if ridge == 'x':
            ys = [y0 + 2.5, y1 - 7.5]
        else:
            ys = [y0 + 2.5, (y0 + y1) / 2 - 2.5, y1 - 7.5]
        for sy in ys:
            wins['x+'].append((sy, sy + 5.0, t0, t1))
            wins['x-'].append((sy, sy + 5.0, t0, t1))
    for gface in (('x+', 'x-') if ridge == 'x' else ('y+', 'y-')):
        wins[gface].append((mid - 2.0, mid + 2.0, ze + 1.5, ze + 6.5))
    if porch == 'canopy':
        doors['y+'].append((pcx - 1.5, pcx + 1.5, pl, pl + 8.0))
    body = gable_body(x0, y0, x1, y1, 0, ze, zr, ridge, m_house, wall=wall, plinth=pl, wins=wins,
                      doors=doors, clad=clad, eave=ze, door=door, frame=frame)
    md.add(body)
    # chimney position first (the roof ladder runs to it)
    if ridge == 'x':
        cx = lerp(x0 + 3.5, x1 - 3.5, chim)
        cy = mid
    else:
        cx = mid
        cy = lerp(y0 + 3.5, y1 - 3.5, chim)
    lad = (cx, 1) if (ladder and ridge == 'x') else ((cy, 1) if ladder else None)
    rp = dict(key=roof, edgekey=frame or 'trim', seam=3.0, ladder=lad)
    if dormer and ridge == 'x':
        dx0, dx1 = (x0 + x1) / 2.0 + 0.5, (x0 + x1) / 2.0 + 8.5
        if porch and not (dx1 <= px0 or dx0 >= px1):
            dx0, dx1 = x1 - 9.5, x1 - 1.5
        s = (zr - ze) / half
        md.add(roof_slab(x0 - 2, dx0, mid, y1 + 2, zr + 0.2, s, 1.6, 'x', m_roof, **rp))
        md.add(roof_slab(dx1, x1 + 2, mid, y1 + 2, zr + 0.2, s, 1.6, 'x', m_roof, **rp))
        md.add(roof_slab(x0 - 2, x1 + 2, mid, y0 - 2, zr + 0.2, s, 1.6, 'x', m_roof, **rp))
        dze = ze + 6.0
        dzr = dze + 4.0 * KZ
        dw = {'y+': [(dx0 + 1.5, dx1 - 1.5, ze + 0.5, ze + 5.5)]}
        md.add(gable_body(dx0, mid, dx1, y1, ze - 4.0, dze, dzr, 'y', m_house, wall=wall, plinth=-1,
                          wins=dw, clad=clad, eave=dze, frieze=False, frame=frame))
        md.add(gable_roof(dx0, mid, dx1, y1, dze, dzr, 'y', m_roof, ov=1.2, ovg=1.0, th=1.4,
                          key=roof, edgekey=frame or 'trim', seam=3.0))
    else:
        md.add(gable_roof(x0, y0, x1, y1, ze, zr, ridge, m_roof, **rp))
    md.add(box(cx - 1.4, cy - 1.4, ze, cx + 1.4, cy + 1.4, zr + 5.5, m_chimney, key=chimkey))
    door_x = pcx
    front_y = y1
    if porch == 'gable':
        pze, pzr = pl + 8.0, pl + 8.0 + 3.0 * KZ
        pw_ = {'x+': [(y1 + 1.2, y1 + 3.2, pl + 3.0, pl + 6.5)]}
        pdoor = {'y+': [(pcx - 1.5, pcx + 1.5, pl - 0.5, pl + 7.0)]}
        md.add(gable_body(px0, y1 - 1, px1, y1 + pd, 0, pze, pzr, 'y', m_house, wall=wall, plinth=pl - 0.5,
                          wins=pw_, doors=pdoor, clad='v', eave=pze, frieze=False, door=door, frame=frame))
        md.add(gable_roof(px0, y1 - 1, px1, y1 + pd, pze, pzr, 'y', m_roof, ov=0.9, ovg=0.8, th=1.3,
                          key=roof, edgekey=frame or 'trim', seam=3.0))
        front_y = y1 + pd
    elif porch == 'canopy':
        md.add(box(pcx - 2.5, y1, pl + 8.5, pcx + 2.5, y1 + 2.5, pl + 9.5, m_flat('trim')))
    elif porch == 'glazed':     # glazed veranda (lasikuisti) with a shed roof
        pze = pl + 8.5
        gw = {'y+': [(px0 + 0.8, pcx - 1.6, pl + 1.5, pze - 1.0), (pcx + 1.6, px1 - 0.8, pl + 1.5, pze - 1.0)],
              'x+': [(y1 + 0.5, y1 + pd - 0.8, pl + 1.5, pze - 1.0)], 'x-': [(y1 + 0.5, y1 + pd - 0.8, pl + 1.5, pze - 1.0)]}
        pdoor = {'y+': [(pcx - 1.5, pcx + 1.5, pl - 0.5, pl + 7.0)]}
        md.add(box(px0, y1 - 1, 0, px1, y1 + pd, pze, m_house, wall=wall, plinth=pl - 0.5, wins=gw, doors=pdoor,
                   clad='v', eave=pze, frieze=False, door=door, frame=frame))
        md.add(mono_body(px0 - 0.6, y1 - 1, px1 + 0.6, y1 + pd + 0.8, pze, pze + 0.8, pze + 3.0, m_flat(roof, 0.3),
                         rise='y-'))
        front_y = y1 + pd
    md.add(box(door_x - 2.0, front_y, 0, door_x + 2.0, front_y + 1.6, 1.2, m_flat('plinth', 0.3)))
    md.add(box(door_x - 2.0, front_y, 0, door_x + 2.0, front_y + 0.9, 2.2, m_flat('plinth', 0.3)))
    if garage:
        # garage + sauna shed in the back-right corner, car in front of it
        gx0, gx1, gy0, gy1 = 24.2, 31.4, 2.0, 15.5
        gze = 8.5
        gzr = gze + 3.6 * KZ * 0.7
        md.add(gable_body(gx0, gy0, gx1, gy1, 0, gze, gzr, 'y', m_house, wall=garage if isinstance(garage, str) else 'w_red',
                          plinth=0.8, clad='v', eave=gze, frieze=False,
                          doors={'y+': [(gx0 + 1.2, gx1 - 1.2, 0.5, gze - 1.2)]}, door='door',
                          wins={'x+': [(gy0 + 2.5, gy0 + 5.0, 3.0, 6.0)]}))
        md.add(gable_roof(gx0, gy0, gx1, gy1, gze, gzr, 'y', m_roof, ov=0.8, ovg=0.8, th=1.2, key='roof_dark',
                          edgekey='trim', seam=3.0))
        md.add(box(gx0 + 4.5, gy0 + 1.5, gze, gx0 + 5.6, gy0 + 2.6, gzr + 3.0, m_flat('steel')))  # sauna pipe
        if car:
            add_car(md, 27.8, 23.0, car, 'y')
        shed = False
    if shed:
        sx0, sx1, sy0, sy1 = 26.0, 31.0, 13.0, 19.5
        sze = 7.0
        szr = sze + 2.6 * KZ * 0.8
        md.add(gable_body(sx0, sy0, sx1, sy1, 0, sze, szr, 'y', m_house, wall='w_red', plinth=0.8,
                          wins={'x+': [(sy0 + 2.0, sy0 + 4.5, 2.5, 5.5)]}, clad='v', eave=sze, frieze=False,
                          corners=True))
        md.add(gable_roof(sx0, sy0, sx1, sy1, sze, szr, 'y', m_roof, ov=0.8, ovg=0.8, th=1.2, key='roof_dark',
                          edgekey='trim', seam=3.0))
    if woodpile:
        add_woodpile(md, 1.5, 4.0, 4.0, 12.0, 4.0)
    if tree == 'apple':
        add_apple(md, tree_at[0], tree_at[1], seed=seed * 7 + 1)
    elif tree == 'birch':
        add_birch(md, tree_at[0], tree_at[1], h=34, seed=seed * 7 + 2, autumn=autumn)
    elif tree == 'spruce':
        add_spruce(md, tree_at[0], tree_at[1], h=30, seed=seed * 7 + 3, r=5.0)
    elif tree == 'rowan':
        add_rowan(md, tree_at[0], tree_at[1], seed=seed * 7 + 4)
    if bush:
        bx, by = (28.5, 26.5) if tree_at[0] < 16 else (3.5, 27.5)
        if garage:
            bx, by = (3.0, 27.5) if tree_at[0] > 16 else (12.0, 28.5)
        add_bush(md, bx, by, seed=seed * 5 + 3, key='lilac' if seed % 2 else 'bush')
    if flagpole:
        add_flagpole(md, flagpole[0], flagpole[1], 30.0 if not big else 34.0)
    if fence:
        gx = door_x
        add_fence(md, 0.8, 31.2, 31.2, 31.2, gaps=((gx - 2.6 - 0.8, gx + 2.6 - 0.8),) +
                  (((24.0, 31.0),) if garage and car else ()), key=fence)
        add_fence(md, 31.2, 0.8, 31.2, 30.0 if not garage else 16.5, key=fence)
    path = [(door_x - 1.6, front_y, door_x + 1.6, 32.0)]
    gr = []
    if garage:
        gr.append((24.0, 15.5, 31.6, 32.0, 'gravel'))
    if potato:
        gr.append((potato[0], potato[1], potato[2], potato[3], 'potato'))
    md.ground = lambda g, st: g_lot(g, st, paths=path, gravel=gr, var=seed)
    return md


def cottage(name, wall='w_red', roof='roof_felt', seed=1, frame='trim', tree='rowan', porch=True,
            outhouse=True, potato=True, chimkey='brick'):
    """house_t1: small one-storey cottage / barrack (torppa) with an outhouse, woodpile and potato patch"""
    md = Model(name)
    pl = 1.4
    x0, x1, y0, y1 = 9.0, 24.0, 7.0, 18.5
    ze = pl + 8.5
    half = (y1 - y0) / 2.0
    zr = ze + half * KZ * 0.85
    mid = (y0 + y1) / 2.0
    wins = {'y+': [(x0 + 2.5, x0 + 7.0, pl + 2.0, pl + 7.5), (x1 - 6.0, x1 - 2.0, pl + 2.0, pl + 7.5)],
            'y-': [(x0 + 3.0, x0 + 7.0, pl + 2.0, pl + 7.5), (x1 - 7.0, x1 - 3.0, pl + 2.0, pl + 7.5)],
            'x+': [(y0 + 3.5, y0 + 7.5, pl + 2.0, pl + 7.5), (mid - 1.5, mid + 1.5, ze + 1.0, ze + 4.5)],
            'x-': [(y0 + 3.5, y0 + 7.5, pl + 2.0, pl + 7.5)]}
    dx = (x0 + x1) / 2.0 + 0.5
    doors = {'y+': [(dx - 1.5, dx + 1.5, pl, pl + 7.5)]}
    md.add(gable_body(x0, y0, x1, y1, 0, ze, zr, 'x', m_house, wall=wall, plinth=pl, wins=wins, doors=doors,
                      clad='v' if seed % 2 else 'h', eave=ze, frame=frame))
    md.add(gable_roof(x0, y0, x1, y1, ze, zr, 'x', m_roof, ov=1.6, ovg=1.6, th=1.4, key=roof, edgekey=frame,
                      seam=3.0))
    md.add(box(x0 + 9.0, mid - 1.2, ze, x0 + 11.4, mid + 1.2, zr + 4.0, m_chimney, key=chimkey))
    if porch:
        md.add(box(dx - 2.3, y1, pl + 8.0, dx + 2.3, y1 + 2.2, pl + 8.8, m_flat(roof, 0.4)))
    md.add(box(dx - 1.8, y1, 0, dx + 1.8, y1 + 1.4, 1.2, m_flat('plinth', 0.3)))
    if outhouse:
        md.add(mono_body(25.5, 3.0, 29.5, 7.0, 0, 7.0, 8.5, m_house, wall='w_weath', plinth=0.3, clad='v',
                         frieze=False, corners=False, doors={'y+': [(26.5, 28.5, 0.3, 6.0)]}, rise='y-'))
    add_woodpile(md, 25.0, 9.5, 30.0, 12.0, 3.5)
    if tree == 'rowan':
        add_rowan(md, 4.5, 26.0, seed=seed + 50)
    elif tree == 'birch':
        add_birch(md, 4.5, 26.0, h=32, seed=seed + 51)
    else:
        add_spruce(md, 4.5, 26.0, h=28, seed=seed + 52, r=4.5)
    add_fence(md, 0.8, 31.2, 31.2, 31.2, gaps=((dx - 2.5 - 0.8, dx + 2.5 - 0.8),), key='fence_wood', h=3.0, step=2.5)
    gr = []
    if potato:
        gr.append((19.5, 22.5, 30.5, 30.0, 'potato'))
    path = [(dx - 1.4, y1 + 1.4, dx + 1.4, 32.0)]
    md.ground = lambda g, st: g_lot(g, st, paths=path, gravel=gr, var=seed)
    return md


# =========================================================================== apartments
def lamelli(name, kind='gable', wall='render_yel', storeys=3, balc='balc_white', roofkey='roof_metal', seed=1,
            balcs=None, brick=False, frame=None):
    """slab block (lamellitalo) on a 3x1 lot, long side along canonical X, front = +Y"""
    pl = 3.0
    x0, x1, y0, y1 = 3.0, 93.0, 6.0, 26.0
    ze = pl + storeys * STOREY
    md = Model(name, 3, 1)
    wins = {'y+': [], 'x+': []}
    doors = {'y+': []}
    stairs = [x0 + 22.5, x0 + 67.5]
    bay = 5.0
    for k in range(storeys):
        t0 = pl + k * STOREY + 2.5
        t1 = t0 + 5.5
        x = x0 + 3.0
        while x + 3.5 <= x1 - 2.0:
            if all(abs(x + 1.75 - sx) > 4.0 for sx in stairs):
                wins['y+'].append((x, x + 3.5, t0, t1))
            x += bay
        for sy in (y0 + 3.0, y1 - 6.5):
            wins['x+'].append((sy, sy + 3.5, t0, t1))
    for sx in stairs:
        doors['y+'].append((sx - 1.75, sx + 1.75, pl - 0.5, pl + 7.0))
        for k in range(storeys):
            t0 = pl + k * STOREY + 5.0 + 3.0
            wins['y+'].append((sx - 1.25, sx + 1.25, t0, t0 + 4.5))
    wins['y-'] = [r for r in wins['y+'] if all(abs((r[0] + r[1]) / 2 - sx) > 2.5 for sx in stairs)]
    wins['x-'] = list(wins['x+'])
    joints = (STOREY, 10.0) if kind == 'panel' else None
    fr = frame or ('panel_dk' if kind == 'panel' else 'trim')
    if kind == 'gable':
        zr = ze + 4.5
        md.add(gable_body(x0, y0, x1, y1, 0, ze, zr, 'x', m_block, wall=wall, plinth=pl, wins=wins,
                          doors=doors, roofkey=roofkey, frame=fr, brickwork=brick))
        md.add(gable_roof(x0, y0, x1, y1, ze, zr, 'x', m_roof, ov=1.5, ovg=1.5, th=1.4, key=roofkey,
                          edgekey='render_white', seam=3.0))
        for sx in (x0 + 12.0, x1 - 30.0):
            md.add(box(sx, (y0 + y1) / 2 - 1.2, ze, sx + 2.4, (y0 + y1) / 2 + 1.2, zr + 4.0, m_chimney, key='render_grey'))
    else:
        md.add(box(x0, y0, 0, x1, y1, ze + 1.2, m_block, wall=wall, plinth=pl, wins=wins, doors=doors,
                   joints=joints, roofkey='bitumen', frame=fr, speckle=(wall == 'panel_wash')))
        for sx in stairs:
            md.add(box(sx - 3.0, (y0 + y1) / 2 - 3.0, ze, sx + 3.0, (y0 + y1) / 2 + 3.0, ze + 5.0, m_block,
                       wall=wall, plinth=-1, roofkey='bitumen', parapet=0.0))
    bcols = []
    x = x0 + 8.0
    while x + 5.0 <= x1 - 4.0:
        if all(abs(x + 2.5 - sx) > 9.0 for sx in stairs):
            bcols.append(x)
        x += 15.0
    for i, bx in enumerate(bcols):
        key = balc
        if balcs:
            key = balcs[i % len(balcs)]
        for k in range(1, storeys):
            z = pl + k * STOREY
            md.add(box(bx, y1, z - 0.6, bx + 6.0, y1 + 3.0, z + 4.0, m_balcony, key=key, rail=(kind == 'gable')))
    for sx in stairs:
        md.add(box(sx - 3.0, y1, pl + 7.5, sx + 3.0, y1 + 2.5, pl + 8.3, m_flat('panel_dk', 0.5)))
    gr = [(sx - 2.0, y1, sx + 2.0, 32.0, 'gravel') for sx in stairs]
    gr.append((x0 - 2, y1 + 3.5, x1 + 2, y1 + 5.0, 'gravel'))
    add_birch(md, 7.0, 29.5, h=30, seed=seed + 31)
    add_bush(md, 50.0, 29.5, seed=seed + 5)
    add_bush(md, 88.0, 29.0, seed=seed + 9)
    add_pine(md, 92.0, 3.0, h=34, seed=seed + 33) if seed % 2 else None
    md.ground = lambda g, st: g_lot(g, st, gravel=gr, var=seed)
    return md


def lamelli22(name, kind='gable', wall='render_yel', storeys=3, balc='balc_white', roofkey='roof_metal', seed=1,
              shape='L', balcs=None, brick=False):
    """apartment block on a 2x2 lot: an L-shaped pair of slabs (or one slab) around a courtyard with
    pines, a granite hump, a sandbox and the carpet-beating rack (THEME 2.4)"""
    md = Model(name, 2, 2)
    pl = 3.0
    ze = pl + storeys * STOREY
    fr = 'panel_dk' if kind == 'panel' else 'trim'
    joints = (STOREY, 10.0) if kind == 'panel' else None

    def slab(x0, y0, x1, y1, axis, stairs, front):
        wins = {'x+': [], 'x-': [], 'y+': [], 'y-': []}
        doors = {front: []}
        if axis == 'x':
            long_faces, short_faces, lo, hi, slo, shi = ('y+', 'y-'), ('x+', 'x-'), x0, x1, y0, y1
        else:
            long_faces, short_faces, lo, hi, slo, shi = ('x+', 'x-'), ('y+', 'y-'), y0, y1, x0, x1
        for k in range(storeys):
            t0 = pl + k * STOREY + 2.5
            t1 = t0 + 5.5
            s = lo + 3.0
            while s + 3.5 <= hi - 2.0:
                if all(abs(s + 1.75 - sx) > 4.0 for sx in stairs):
                    for f in long_faces:
                        wins[f].append((s, s + 3.5, t0, t1))
                s += 5.0
            for f in short_faces:
                wins[f].append((slo + 3.0, slo + 6.5, t0, t1))
                wins[f].append((shi - 6.5, shi - 3.0, t0, t1))
        for sx in stairs:
            doors[front].append((sx - 1.75, sx + 1.75, pl - 0.5, pl + 7.0))
            for k in range(storeys):
                t0 = pl + k * STOREY + 8.0
                wins[front].append((sx - 1.25, sx + 1.25, t0, t0 + 4.5))
        if kind == 'gable':
            zr = ze + 4.5
            md.add(gable_body(x0, y0, x1, y1, 0, ze, zr, axis, m_block, wall=wall, plinth=pl, wins=wins,
                              doors=doors, roofkey=roofkey, frame=fr, brickwork=brick))
            md.add(gable_roof(x0, y0, x1, y1, ze, zr, axis, m_roof, ov=1.5, ovg=1.5, th=1.4, key=roofkey,
                              edgekey='render_white', seam=3.0))
        else:
            md.add(box(x0, y0, 0, x1, y1, ze + 1.2, m_block, wall=wall, plinth=pl, wins=wins, doors=doors,
                       joints=joints, roofkey='bitumen', frame=fr, speckle=(wall == 'panel_wash')))
            for sx in stairs:
                if axis == 'x':
                    md.add(box(sx - 3.0, (y0 + y1) / 2 - 3.0, ze, sx + 3.0, (y0 + y1) / 2 + 3.0, ze + 5.0, m_block,
                               wall=wall, plinth=-1, roofkey='bitumen', parapet=0.0))
                else:
                    md.add(box((x0 + x1) / 2 - 3.0, sx - 3.0, ze, (x0 + x1) / 2 + 3.0, sx + 3.0, ze + 5.0, m_block,
                               wall=wall, plinth=-1, roofkey='bitumen', parapet=0.0))
        # balconies on the courtyard side
        if axis == 'x':
            bx = lo + 8.0
            i = 0
            while bx + 6.0 <= hi - 4.0:
                if all(abs(bx + 3.0 - sx) > 7.0 for sx in stairs):
                    for k in range(1, storeys):
                        z = pl + k * STOREY
                        key = balcs[i % len(balcs)] if balcs else balc
                        md.add(box(bx, y1, z - 0.6, bx + 6.0, y1 + 3.0, z + 4.0, m_balcony, key=key, rail=kind == 'gable'))
                    i += 1
                bx += 14.0
        else:
            by = lo + 8.0
            i = 0
            while by + 6.0 <= hi - 4.0:
                if all(abs(by + 3.0 - sx) > 7.0 for sx in stairs):
                    for k in range(1, storeys):
                        z = pl + k * STOREY
                        key = balcs[i % len(balcs)] if balcs else balc
                        md.add(box(x0 - 3.0, by, z - 0.6, x0, by + 6.0, z + 4.0, m_balcony, key=key, rail=kind == 'gable'))
                    i += 1
                by += 14.0

    gr = []
    if shape == 'L':
        slab(3.0, 4.0, 61.0, 24.0, 'x', [20.0, 44.0], 'y+')
        slab(41.0, 24.0, 61.0, 58.0, 'y', [44.0], 'x-')
        gr += [(18.0, 24.0, 22.0, 64.0, 'gravel'), (2.0, 46.0, 41.0, 49.5, 'gravel')]
        add_sandbox(md, 12.0, 36.0)
        add_carpet_rack(md, 28.0, 34.0)
        add_pine(md, 6.0, 58.0, h=40, seed=seed + 61)
        add_pine(md, 30.0, 57.0, h=36, seed=seed + 62)
        add_birch(md, 58.0, 61.0, h=30, seed=seed + 63)
        add_bush(md, 36.0, 42.0, seed=seed + 64)
    else:
        slab(3.0, 8.0, 61.0, 28.0, 'x', [20.0, 44.0], 'y+')
        gr += [(18.0, 28.0, 22.0, 64.0, 'gravel'), (2.0, 46.0, 62.0, 49.5, 'gravel')]
        add_sandbox(md, 34.0, 38.0)
        add_carpet_rack(md, 50.0, 37.0)
        add_swings(md, 10.0, 38.0)
        add_pine(md, 6.0, 58.0, h=40, seed=seed + 61)
        add_pine(md, 57.0, 58.0, h=38, seed=seed + 62)
        add_birch(md, 30.0, 58.0, h=30, seed=seed + 63)
    # granite hump (kallio) in the yard
    md.add(HeightField(2.0, 50.0, 26.0, 63.0, 3.5,
                       lambda x, y: 3.5 * max(0.0, 1.0 - ((x - 14.0) / 11.0) ** 2 - ((y - 57.0) / 6.0) ** 2) ** 0.6,
                       m_rock))
    md.ground = lambda g, st: g_lot(g, st, gravel=gr, var=seed)
    return md


# =========================================================================== commercial
def kiosk(name, sign='sign_blue', wall='render_white', seed=1, wooden=False):
    """com_t1_1x1: tiny kiosk with a hatch, roof sign, bench and bus-stop pole"""
    md = Model(name)
    x0, x1, y0, y1 = 10.0, 20.5, 9.0, 17.5
    ze = 9.0
    if wooden:
        md.add(box(x0, y0, 0, x1, y1, ze, m_house, wall=wall, plinth=0.8, clad='v', frieze=False,
                   wins={'y+': [(x0 + 1.2, x1 - 1.2, 3.2, 6.6)], 'x+': [(y0 + 1.5, y1 - 1.5, 3.5, 6.5)]},
                   doors={'x-': [(y0 + 2.0, y0 + 4.5, 0.5, 7.0)]}))
        md.add(gable_roof(x0, y0, x1, y1, ze, ze + 3.5 * KZ * 0.7, 'x', m_roof, ov=1.0, ovg=1.0, th=1.2,
                          key='roof_red', edgekey='trim', seam=3.0))
        md.add(box(x0 - 0.5, y1, 6.5, x1 + 0.5, y1 + 2.4, 7.2, m_awning, key='awning'))
    else:
        md.add(box(x0, y0, 0, x1, y1, ze, m_block, wall=wall, plinth=1.0,
                   wins={'y+': [(x0 + 1.0, x1 - 1.0, 3.0, 6.8)], 'x+': [(y0 + 1.0, y1 - 1.0, 3.0, 6.8)]},
                   doors={'x-': [(y0 + 2.0, y0 + 4.5, 0.5, 7.0)]}, bands={'y+': [(x0, x1, 7.2, ze, sign)],
                                                                          'x+': [(y0, y1, 7.2, ze, sign)]},
                   roofkey='bitumen', parapet=0.6))
        md.add(box(x0 - 0.4, y1, 6.6, x1 + 0.4, y1 + 1.8, 7.2, m_flat(sign, 0.3)))     # hatch canopy
        md.add(box(x0 + 1.5, (y0 + y1) / 2 - 0.6, ze, x1 - 1.5, (y0 + y1) / 2 + 0.6, ze + 3.5, m_block, wall=sign,
                   plinth=-1, bands={'y+': [(x0 + 1.5, x1 - 1.5, ze, ze + 3.5, sign, 'text')]}, roofkey=sign))
    md.add(box(x0 + 1.0, y1, 0, x1 - 1.0, y1 + 1.2, 3.8, m_flat('trim', -0.2)))       # counter / newspaper rack
    add_bench(md, 24.0, 22.0)
    md.add(box(5.45, 25.45, 0, 6.55, 26.55, 13.0, m_flat('steel_lt'), detail=True))     # bus stop pole
    md.add(box(5.6, 25.4, 10.0, 6.4, 26.6, 13.0, m_flat('sign_yel', 0.4), detail=True))
    add_birch(md, 26.0, 7.0, h=28, seed=seed + 70)
    gr = [(0.0, 19.0, 32.0, 32.0, 'asphalt'), (8.0, 7.0, 22.0, 19.0, 'gravel')]
    md.ground = lambda g, st: g_lot(g, st, gravel=gr, var=seed)
    return md


def coop_shop(name, wall='w_yellow', sign='sign_green', roof='roof_metal', seed=1, rendered=False):
    """com_t1_2x1: village co-op shop (osuuskauppa), 1.5 storeys, shop window + sign board + dock"""
    md = Model(name, 2, 1)
    pl = 1.5
    x0, x1, y0, y1 = 6.0, 50.0, 6.0, 22.0
    ze = pl + 14.5
    zr = ze + 8.0 * KZ * 0.8
    mid = (y0 + y1) / 2.0
    wins = {'y+': [(x0 + 3.0, x0 + 13.0, pl + 2.0, pl + 8.5), (x0 + 19.0, x0 + 29.0, pl + 2.0, pl + 8.5),
                   (x1 - 9.0, x1 - 4.5, pl + 2.5, pl + 8.5)],
            'y-': wins_row(x0, x1, pl + 2.5, pl + 8.5, 4.5, 5),
            'x+': [(y0 + 3.0, y0 + 7.5, pl + 2.5, pl + 8.5), (mid - 2.0, mid + 2.0, ze + 1.5, ze + 6.5)],
            'x-': [(y0 + 3.0, y0 + 7.5, pl + 2.5, pl + 8.5), (mid - 2.0, mid + 2.0, ze + 1.5, ze + 6.5)]}
    doors = {'y+': [(x0 + 14.5, x0 + 17.5, pl, pl + 8.0)], 'x+': [(y1 - 7.0, y1 - 2.5, 0.5, pl + 8.0)]}
    if rendered:
        md.add(gable_body(x0, y0, x1, y1, 0, ze, zr, 'x', m_block, wall=wall, plinth=pl, wins=wins, doors=doors,
                          roofkey=roof, bands={'y+': [(x0 + 2.0, x0 + 30.0, pl + 9.3, pl + 12.3, sign, 'text')]}))
    else:
        md.add(gable_body(x0, y0, x1, y1, 0, ze, zr, 'x', m_house, wall=wall, plinth=pl, wins=wins, doors=doors,
                          clad='h', eave=ze))
        md.add(box(x0 + 2.0, y1, pl + 9.2, x0 + 30.0, y1 + 0.6, pl + 12.4, m_block, wall=sign, plinth=-1,
                   bands={'y+': [(x0 + 2.0, x0 + 30.0, pl + 9.2, pl + 12.4, sign, 'text')]}))
    md.add(gable_roof(x0, y0, x1, y1, ze, zr, 'x', m_roof, ov=1.5, ovg=1.5, th=1.4, key=roof, edgekey='trim', seam=3.0))
    md.add(box(x0 + 30.0, mid - 1.3, ze, x0 + 32.6, mid + 1.3, zr + 4.5, m_chimney, key='brick'))
    md.add(box(x1, y1 - 9.0, 0, x1 + 5.0, y1, 3.5, m_concrete))            # loading dock
    md.add(box(x0 + 13.5, y1, 0, x0 + 18.5, y1 + 1.6, 1.0, m_flat('plinth', 0.3)))
    add_flagpole(md, 3.0, 28.5, 32.0, flag=True)
    md.add(box(x1 - 2.0, y1 + 2.5, 0, x1 + 0.5, y1 + 4.5, 4.0, m_flat('balc_red', 0.2)))     # milk-can stand
    add_birch(md, 60.0, 4.5, h=30, seed=seed + 80)
    gr = [(0.0, y1, 64.0, 32.0, 'gravel'), (x1, y0, 64.0, y1, 'gravel')]
    md.ground = lambda g, st: g_lot(g, st, gravel=gr, var=seed)
    return md


def coop_store(name, wall='render_yel', sign='sign_green', roof='roof_metal', seed=1, car='white'):
    """com_t1_2x2: bigger co-op store, 2 storeys, hipped tin roof, shop below and flats above"""
    md = Model(name, 2, 2)
    pl = 1.5
    x0, x1, y0, y1 = 6.0, 54.0, 6.0, 30.0
    ze = pl + 2 * STOREY + 1.0
    zr = ze + 10.0
    w_up = wins_row(x0, x1, pl + STOREY + 3.0, pl + STOREY + 8.5, 4.0, 7)
    wins = {'y+': [(x0 + 3.0, x0 + 14.0, pl + 1.5, pl + 7.8), (x0 + 19.0, x0 + 30.0, pl + 1.5, pl + 7.8),
                   (x0 + 35.0, x0 + 45.0, pl + 1.5, pl + 7.8)] + w_up,
            'y-': w_up + wins_row(x0, x1, pl + 2.5, pl + 8.0, 4.0, 6),
            'x+': wins_row(y0, y1, pl + 2.5, pl + 8.0, 4.0, 3) + wins_row(y0, y1, pl + STOREY + 3.0, pl + STOREY + 8.5, 4.0, 3),
            'x-': wins_row(y0, y1, pl + STOREY + 3.0, pl + STOREY + 8.5, 4.0, 3)}
    doors = {'y+': [(x0 + 15.5, x0 + 18.0, pl, pl + 8.0)], 'x-': [(y0 + 5.0, y0 + 10.0, 0.0, pl + 8.0, 'steel')]}
    bands = {'y+': [(x0 + 2.0, x1 - 2.0, pl + 8.6, pl + STOREY + 1.0, sign, 'text')]}
    md.add(hip_body(x0, y0, x1, y1, 0, ze, zr, m_hiproof, wallmat=m_block, wall=wall, plinth=pl, wins=wins,
                    doors=doors, bands=bands, roofkey=roof, inset=12.0))
    md.add(box(x0 + 10.0, 17.0, ze + 3.0, x0 + 12.6, 19.6, zr + 2.0, m_chimney, key='render_grey'))
    md.add(box(x0 + 36.0, 17.0, ze + 3.0, x0 + 38.6, 19.6, zr + 2.0, m_chimney, key='render_grey'))
    md.add(box(x0 + 1.0, y1, pl + 7.8, x1 - 1.0, y1 + 2.4, pl + 8.5, m_awning, key='awning_g' if sign == 'sign_green' else 'awning'))
    add_car(md, 46.0, 46.0, car, 'x')
    add_flagpole(md, 4.0, 60.0, 34.0, flag=True)
    add_birch(md, 60.0, 8.0, h=32, seed=seed + 81)
    add_bush(md, 4.0, 36.0, seed=seed + 82)
    gr = [(0.0, y1, 64.0, 64.0, 'asphalt'), (0.0, 0.0, x0, y1, 'gravel')]
    md.ground = lambda g, st: g_lot(g, st, gravel=gr, var=seed)
    return md


def corner_shop(name, wall='render_white', awn='awning', sign='sign_red', seed=1):
    """com_t2_1x1: two-storey rendered building, shop on the ground floor, flat above, flat roof"""
    md = Model(name)
    pl = 1.0
    x0, x1, y0, y1 = 6.0, 26.0, 5.0, 23.0
    ze = pl + 2 * STOREY + 1.5
    up = pl + STOREY + 3.0
    wins = {'y+': [(x0 + 1.5, x0 + 8.0, pl + 1.5, pl + 7.8), (x0 + 11.5, x1 - 1.5, pl + 1.5, pl + 7.8)] +
            wins_row(x0, x1, up, up + 5.5, 4.0, 3),
            'x+': [(y0 + 2.0, y0 + 9.0, pl + 1.5, pl + 7.8)] + wins_row(y0, y1, up, up + 5.5, 4.0, 3),
            'y-': wins_row(x0, x1, up, up + 5.5, 4.0, 3), 'x-': wins_row(y0, y1, up, up + 5.5, 4.0, 3)}
    doors = {'y+': [(x0 + 8.4, x0 + 11.0, pl, pl + 7.8)], 'x+': [(y1 - 5.0, y1 - 2.5, pl, pl + 7.8)]}
    bands = {'y+': [(x0 + 1.0, x1 - 1.0, pl + 8.4, pl + STOREY + 1.0, sign, 'text')],
             'x+': [(y0 + 1.0, y1 - 7.0, pl + 8.4, pl + STOREY + 1.0, sign, 'text')]}
    md.add(box(x0, y0, 0, x1, y1, ze, m_block, wall=wall, plinth=pl, wins=wins, doors=doors, bands=bands,
               roofkey='bitumen', frame='trim'))
    md.add(box(x0 - 0.5, y1, pl + 7.6, x1 + 0.5, y1 + 2.6, pl + 8.3, m_awning, key=awn))
    md.add(box(x1 - 7.0, y1, up - 0.6, x1 - 2.0, y1 + 2.2, up + 3.6, m_balcony, key='balc_white', rail=True))
    md.add(box(x0 + 3.0, y0 + 3.0, ze, x0 + 6.0, y0 + 6.0, ze + 3.0, m_flat('steel')))
    md.add(Cone(x1 - 4.0, y0 + 4.0, ze, ze + 7.0, 0.25, 0.25, m_flat('steel_lt'), detail=True))      # TV antenna
    md.add(box(x1 - 6.5, y0 + 3.9, ze + 6.0, x1 - 1.5, y0 + 4.1, ze + 6.4, m_flat('steel_lt'), detail=True))
    md.add(box(28.0, 25.0, 0, 30.5, 27.0, 5.0, m_flat('sign_yel', 0.2)))                              # mailbox post
    gr = [(0.0, y1, 32.0, 32.0, 'asphalt'), (x1, 0.0, 32.0, y1, 'asphalt')]
    md.ground = lambda g, st: g_lot(g, st, gravel=gr, var=seed)
    return md


def shop_row(name, signs=('sign_green', 'sign_red', 'sign_blue'), wall='render_white', seed=1, car='red'):
    """com_t2_2x1: 1960s single-storey shop row, glazed fronts, fascia signs, canopy, parking"""
    md = Model(name, 2, 1)
    pl = 0.8
    x0, x1, y0, y1 = 3.0, 61.0, 4.0, 18.0
    ze = pl + 11.5
    n = len(signs)
    seg = (x1 - x0) / n
    wins = {'y+': [], 'x+': [(y0 + 2.0, y0 + 6.0, pl + 2.0, pl + 7.5)], 'x-': [(y0 + 2.0, y0 + 6.0, pl + 2.0, pl + 7.5)]}
    doors = {'y+': [], 'y-': [(x0 + 5.0, x0 + 9.0, 0, 7.5, 'steel'), (x1 - 9.0, x1 - 5.0, 0, 7.5, 'steel')]}
    bands = {'y+': []}
    for i in range(n):
        a = x0 + i * seg
        wins['y+'].append((a + 1.0, a + seg * 0.62, pl + 1.0, pl + 7.8))
        doors['y+'].append((a + seg * 0.66, a + seg * 0.66 + 2.6, pl, pl + 7.8))
        wins['y+'].append((a + seg * 0.66 + 3.4, a + seg - 1.0, pl + 1.0, pl + 7.8))
        bands['y+'].append((a + 0.6, a + seg - 0.6, pl + 8.8, ze - 0.4, signs[i], 'text'))
    md.add(box(x0, y0, 0, x1, y1, ze, m_block, wall=wall, plinth=pl, wins=wins, doors=doors, bands=bands,
               roofkey='bitumen', frame='steel_lt', wkind='shop'))
    md.add(box(x0, y1, pl + 8.0, x1, y1 + 3.4, pl + 8.7, m_flat('panel_dk', 0.6)))
    for i in range(n + 1):
        px = x0 + 0.6 + i * (seg - 0.04) if i < n else x1 - 0.6
        md.add(box(px - 0.3, y1 + 2.8, 0, px + 0.3, y1 + 3.4, pl + 8.0, m_flat('steel_lt'), detail=True))
    md.add(box(x1 - 10.0, y0 + 3.0, ze, x1 - 5.0, y0 + 7.0, ze + 2.5, m_flat('steel')))
    add_car(md, 18.0, 26.5, car, 'x')
    add_bench(md, 46.0, 25.0)
    gr = [(0.0, y1, 64.0, 32.0, 'asphalt')]
    md.ground = lambda g, st: g_lot(g, st, gravel=gr, var=seed)
    return md


def shops_flats(name, wall='render_yel', balc='balc_orange', signs=('sign_red', 'sign_green', 'sign_blue'),
                awn='awning', seed=1, storeys=4):
    """com_t2_2x2: four-storey block, shops on the ground floor, flats with balconies above"""
    md = Model(name, 2, 2)
    pl = 1.0
    x0, x1, y0, y1 = 4.0, 60.0, 6.0, 28.0
    ze = pl + storeys * STOREY + 1.0
    wins = {'y+': [], 'y-': [], 'x+': [], 'x-': []}
    doors = {'y+': [], 'y-': [(30.0, 33.5, pl, pl + 7.0)]}
    bands = {'y+': [], 'x+': [(y0 + 1.0, y1 - 1.0, pl + 8.6, pl + STOREY, signs[-1], 'text')]}
    n = len(signs)
    seg = (x1 - x0) / n
    for i in range(n):
        a = x0 + i * seg
        wins['y+'].append((a + 1.2, a + seg * 0.6, pl + 1.0, pl + 7.8))
        doors['y+'].append((a + seg * 0.64, a + seg * 0.64 + 2.6, pl, pl + 7.8))
        wins['y+'].append((a + seg * 0.64 + 3.4, a + seg - 1.2, pl + 1.0, pl + 7.8))
        bands['y+'].append((a + 0.8, a + seg - 0.8, pl + 8.6, pl + STOREY, signs[i], 'text'))
    for k in range(1, storeys):
        t0 = pl + k * STOREY + 2.5
        for f, lo, hi, cnt in (('y+', x0, x1, 10), ('y-', x0, x1, 10), ('x+', y0, y1, 4), ('x-', y0, y1, 4)):
            wins[f] += wins_row(lo, hi, t0, t0 + 5.5, 3.6, cnt)
    wins['x+'] += [(y0 + 3.0, y0 + 9.0, pl + 1.0, pl + 7.8)]
    md.add(box(x0, y0, 0, x1, y1, ze, m_block, wall=wall, plinth=pl, wins=wins, doors=doors, bands=bands,
               roofkey='bitumen', frame='trim'))
    md.add(box(x0, y1, pl + 7.8, x1, y1 + 2.6, pl + 8.4, m_awning, key=awn))
    for bx in (x0 + 8.0, x0 + 26.0, x0 + 44.0):
        for k in range(1, storeys):
            z = pl + k * STOREY
            md.add(box(bx, y1, z - 0.6, bx + 7.0, y1 + 3.0, z + 4.0, m_balcony, key=balc))
    md.add(box(29.0, 14.0, ze, 35.0, 20.0, ze + 4.5, m_block, wall=wall, plinth=-1, roofkey='bitumen', parapet=0.0))
    add_car(md, 15.0, 42.0, 'lblue', 'x')
    add_car(md, 44.0, 42.5, 'mustard', 'x')
    add_birch(md, 4.0, 58.0, h=30, seed=seed + 90)
    add_birch(md, 58.0, 58.0, h=28, seed=seed + 91)
    add_bush(md, 30.0, 58.0, seed=seed + 92)
    gr = [(0.0, y1, 64.0, 50.0, 'asphalt'), (0.0, 50.0, 64.0, 53.0, 'gravel')]
    md.ground = lambda g, st: g_lot(g, st, gravel=gr, var=seed)
    return md


# =========================================================================== industry
def workshop(name, wall='w_red', seed=1, corrugated=False):
    """ind_t1_2x2: workshop (konepaja / puusepänverstas): a long shed, a side shed, a yard of
    crates, barrels and timber, a small brick chimney"""
    md = Model(name, 2, 2)
    pl = 1.0
    x0, x1, y0, y1 = 4.0, 46.0, 6.0, 26.0
    ze = pl + 11.0
    zr = ze + 10.0 * KZ * 0.75
    mid = (y0 + y1) / 2.0
    doors = {'y+': [(x0 + 6.0, x0 + 14.0, 0, 10.0, 'door'), (x0 + 26.0, x0 + 34.0, 0, 10.0, 'door')]}
    wins = {'y+': [(x0 + 16.5, x0 + 20.5, pl + 3.0, pl + 8.0), (x0 + 37.0, x0 + 41.0, pl + 3.0, pl + 8.0)],
            'y-': wins_row(x0, x1, pl + 3.0, pl + 8.0, 4.0, 5),
            'x+': [(y0 + 3.0, y0 + 7.0, pl + 3.0, pl + 8.0), (y1 - 7.0, y1 - 3.0, pl + 3.0, pl + 8.0),
                   (mid - 2.0, mid + 2.0, ze + 1.5, ze + 6.0)],
            'x-': [(mid - 2.0, mid + 2.0, ze + 1.5, ze + 6.0)]}
    if corrugated:
        md.add(gable_body(x0, y0, x1, y1, 0, ze, zr, 'x', m_corr, wall='corr', doors={'y+': [(r[0], r[1], r[2], r[3], 'steel') for r in doors['y+']]},
                          wins=wins, frame='steel_lt', roofkey='roof_dark'))
        rk = 'corr'
    else:
        md.add(gable_body(x0, y0, x1, y1, 0, ze, zr, 'x', m_house, wall=wall, plinth=pl, wins=wins,
                          doors={'y+': [(r[0], r[1], r[2], r[3]) for r in doors['y+']]}, clad='v', eave=ze))
        rk = 'roof_metal'
    md.add(gable_roof(x0, y0, x1, y1, ze, zr, 'x', m_roof, ov=1.4, ovg=1.4, th=1.4, key=rk, edgekey='trim', seam=3.0))
    md.add(box(x0 + 21.0, mid - 1.5, ze, x0 + 24.0, mid + 1.5, zr + 9.0, m_chimney, key='brick'))
    # lean-to side shed
    md.add(mono_body(x1, y0 + 2.0, x1 + 12.0, y1 - 4.0, 0, 7.0, 9.5, m_house, wall='w_weath', plinth=0.5, clad='v',
                     frieze=False, corners=False, doors={'x+': [(y0 + 6.0, y0 + 12.0, 0.3, 6.5)]}, rise='x-'))
    md.add(mono_body(x1 - 0.5, y0 + 1.4, x1 + 12.6, y1 - 3.4, 6.6, 7.0, 10.0, m_flat('roof_dark', 0.3), rise='x-'))
    # yard props
    for i in range(4):
        cx = 8.0 + i * 3.4
        md.add(Cone(cx, 40.0, 0, 4.0, 1.4, 1.4, m_flat('steel' if i % 2 else 'rust'), cap=True))      # barrels
    md.add(box(24.0, 36.0, 0, 30.0, 42.0, 4.5, m_flat('woodpile')))            # crates
    md.add(box(25.0, 37.0, 4.5, 29.0, 41.0, 8.0, m_flat('woodpile', 0.3)))
    md.add(box(36.0, 34.0, 0, 58.0, 40.0, 4.0, m_timber))                      # board stack
    md.add(box(36.0, 34.0, 4.0, 58.0, 40.0, 4.6, m_flat('roof_dark')))
    add_birch(md, 4.5, 58.0, h=30, seed=seed + 100)
    add_spruce(md, 58.0, 4.0, h=32, seed=seed + 101, r=4.5)
    gr = [(0.0, y1, 64.0, 64.0, 'gravel'), (x1, 0.0, 64.0, y1, 'gravel')]
    md.ground = lambda g, st: g_lot(g, st, gravel=gr, var=seed, base='grass')
    return md


def sawmill(name, seed=1, shed='w_red', roof='roof_dark'):
    """ind_t1_3x3: sawmill: a long red-ochre saw shed, sawdust burner, stickered timber stacks, log piles"""
    md = Model(name, 3, 3)
    pl = 1.0
    x0, x1, y0, y1 = 6.0, 74.0, 10.0, 30.0
    ze = pl + 12.0
    zr = ze + 10.0 * KZ * 0.8
    mid = (y0 + y1) / 2.0
    wins = {'y+': wins_row(x0, x1, pl + 3.5, pl + 8.5, 4.0, 9, skip=((x0 + 30.0, x0 + 40.0),)),
            'y-': wins_row(x0, x1, pl + 3.5, pl + 8.5, 4.0, 9),
            'x+': [(mid - 2.0, mid + 2.0, ze + 1.5, ze + 6.0)], 'x-': [(mid - 2.0, mid + 2.0, ze + 1.5, ze + 6.0)]}
    doors = {'y+': [(x0 + 30.0, x0 + 40.0, 0, 11.0)], 'x-': [(y0 + 4.0, y1 - 4.0, 2.0, 10.0)]}
    md.add(gable_body(x0, y0, x1, y1, 0, ze, zr, 'x', m_house, wall=shed, plinth=pl, wins=wins, doors=doors,
                      clad='v', eave=ze))
    md.add(gable_roof(x0, y0, x1, y1, ze, zr, 'x', m_roof, ov=1.5, ovg=1.5, th=1.4, key=roof, edgekey='trim', seam=3.0))
    # log conveyor ramp up to the shed gable (log deck)
    md.add(Convex([(-1, 0, 0, -0.5, 'x-'), (1, 0, 0, x0, 'x+'), (0, -1, 0, -(mid - 2.0), 'y-'), (0, 1, 0, mid + 2.0, 'y+'),
                   (0, 0, -1, 0, 'bot'), (-1.6 / 1.0, 0, 1, 1.6 * 0.0 + 1.0, 'top')],
                  (0.5, mid - 2.0, 0, x0, mid + 2.0, 10.0), m_flat('fence_wood')))
    # sawdust burner (cone of rusty steel) and an office hut
    md.add(Cone(84.0, 16.0, 0, 26.0, 6.0, 3.2, m_flat('rust'), cap=True))
    md.add(Cone(84.0, 16.0, 26.0, 30.0, 3.4, 3.4, m_flat('steel'), cap=True))
    md.hot = (84.0, 16.0, 31.0)
    md.add(gable_body(80.0, 74.0, 92.0, 84.0, 0, 9.0, 9.0 + 5.0 * KZ * 0.8, 'x', m_house, wall='w_yellow', plinth=1.0,
                      wins={'y+': [(82.0, 86.0, 3.0, 7.5)], 'x-': [(76.0, 80.0, 3.0, 7.5)]},
                      doors={'y+': [(88.0, 90.5, 1.0, 8.0)]}, clad='h', eave=9.0))
    md.add(gable_roof(80.0, 74.0, 92.0, 84.0, 9.0, 9.0 + 5.0 * KZ * 0.8, 'x', m_roof, ov=0.8, ovg=0.8, th=1.2,
                      key='roof_metal', edgekey='trim', seam=3.0))
    # timber stacks (yellow sawn boards, stickered) in rows, roofed with boards
    stacks = ((8.0, 42.0), (30.0, 42.0), (52.0, 42.0), (8.0, 58.0), (30.0, 58.0), (52.0, 58.0))
    if seed % 2 == 0:
        stacks = ((8.0, 42.0), (30.0, 42.0), (8.0, 58.0), (52.0, 50.0), (30.0, 58.0))
    for (sx, sy) in stacks:
        hh = 7.0 + 3.0 * ihash(sx, sy, 7)
        md.add(box(sx, sy, 0, sx + 16.0, sy + 8.0, hh, m_timber))
        md.add(box(sx - 0.5, sy - 0.5, hh, sx + 16.5, sy + 8.5, hh + 0.8, m_flat('roof_dark', 0.3)))
    # log piles
    md.add(box(4.0, 76.0, 0, 30.0, 88.0, 5.0, m_logs))
    md.add(box(34.0, 78.0, 0, 62.0, 90.0, 4.0, m_logs))
    add_pine(md, 92.0, 92.0, h=40, seed=seed + 110)
    add_pine(md, 74.0, 92.0, h=36, seed=seed + 111)
    gr = [(0.0, 32.0, 96.0, 96.0, 'gravel'), (0.0, 0.0, 96.0, 10.0, 'sawdust'), (74.0, 0.0, 96.0, 32.0, 'sawdust')]
    md.ground = lambda g, st: g_lot(g, st, gravel=gr, var=seed)
    return md


def factory22(name, seed=1, white=False, chimney=84.0):
    """ind_t2_2x2: factory: north-light sawtooth hall, boiler house + chimney (smoke), coal heap, office"""
    md = Model(name, 2, 2)
    hx0, hx1, hy0, hy1 = 4.0, 44.0, 6.0, 46.0
    hw = 15.0
    wins = {'y+': [(hx0 + 4 + 9 * i, hx0 + 10 + 9 * i, 4.0, 11.0) for i in range(4)],
            'x+': [(hy0 + 4 + 9 * i, hy0 + 10 + 9 * i, 4.0, 11.0) for i in range(2)],
            'x-': [(hy0 + 4 + 9 * i, hy0 + 10 + 9 * i, 4.0, 11.0) for i in range(4)],
            'y-': [(hx0 + 4 + 9 * i, hx0 + 10 + 9 * i, 4.0, 11.0) for i in range(4)]}
    doors = {'x+': [(hy0 + 24.0, hy0 + 32.0, 0.0, 10.0, 'steel')]}
    wall = 'render_white' if white else 'brick'
    md.add(box(hx0, hy0, 0, hx1, hy1, hw, m_brickwall, wins=wins, doors=doors, wall=wall))
    tw = 8.0
    for i in range(int((hx1 - hx0) / tw)):
        xa = hx0 + i * tw
        xb = xa + tw
        h = 6.0
        s = h / tw
        pl = [(-1, 0, 0, -xa, 'x-'), (1, 0, 0, xb, 'glass'), (0, -1, 0, -hy0, 'y-'), (0, 1, 0, hy1, 'y+'),
              (0, 0, -1, -hw, 'bot'), (-s, 0, 1, hw - s * xa, 'slope')]
        md.add(Convex(pl, (xa, hy0, hw, xb, hy1, hw + h), m_saw, end=wall))
    md.add(box(46.0, 6.0, 0, 60.0, 24.0, 12.0, m_brickwall, wall=wall,
               wins={'y+': [(49.0, 52.0, 4.0, 9.0), (54.0, 57.0, 4.0, 9.0)]},
               doors={'x+': [(10.0, 15.0, 0.0, 8.0, 'steel')]}))
    if white:
        md.add(Cone(53.0, 13.0, 12.0, chimney, 3.2, 2.4, m_concrete_round, cap=True, key='render_grey',
                    band=(chimney - 4.0, chimney, 'cap')))
    else:
        md.add(Cone(53.0, 13.0, 12.0, chimney, 3.6, 2.3, m_cone_brick, cap=True))
    md.add(Cone(53.0, 13.0, 0.0, 12.5, 4.4, 4.4, m_cone_brick, cap=True))
    md.hot = (53.0, 13.0, chimney + 1.0)
    ow = {'y+': [(32.0 + 5 * i, 35.0 + 5 * i, 3.0 + k * STOREY, 8.0 + k * STOREY) for i in range(5) for k in range(2)],
          'x+': [(52.0 + 4 * i, 55.0 + 4 * i, 3.0 + k * STOREY, 8.0 + k * STOREY) for i in range(2) for k in range(2)],
          'x-': [(52.0 + 4 * i, 55.0 + 4 * i, 3.0 + k * STOREY, 8.0 + k * STOREY) for i in range(2) for k in range(2)]}
    md.add(box(30.0, 50.0, 0, 60.0, 61.0, 2 * STOREY + 2.5, m_block, wall='render_grey' if not white else 'render_yel',
               plinth=1.5, wins=ow, doors={'y+': [(56.0, 58.5, 1.0, 8.0)]}, roofkey='bitumen'))
    md.add(HeightField(46.0, 26.0, 62.0, 44.0, 9.0,
                       lambda x, y: 9.0 * max(0.0, 1.0 - ((x - 54.0) / 7.0) ** 2 - ((y - 35.0) / 8.0) ** 2) ** 0.8,
                       m_flat('coal')))
    add_birch(md, 6.0, 56.0, h=30, seed=seed + 120)
    gr = [(0.0, 46.0, 64.0, 64.0, 'asphalt'), (44.0, 0.0, 64.0, 46.0, 'gravel'), (0.0, 0.0, 64.0, 6.0, 'gravel')]
    md.ground = lambda g, st: g_lot(g, st, gravel=gr, var=seed)
    return md


def factory33(name, seed=1, wall='brick', chimney=92.0):
    white = wall != 'brick'
    """ind_t2_3x3: large factory: a 4-storey mill building, sawtooth hall, water tank, chimney, yard"""
    md = Model(name, 3, 3)
    pl = 1.5
    # 4-storey mill building along X at the back
    mx0, mx1, my0, my1 = 4.0, 64.0, 4.0, 24.0
    ze = pl + 4 * STOREY + 1.0
    mw = {'y+': [], 'y-': [], 'x+': [], 'x-': []}
    for k in range(4):
        t0 = pl + k * STOREY + 2.0
        mw['y+'] += wins_row(mx0, mx1, t0, t0 + 6.5, 3.6, 11)
        mw['y-'] += wins_row(mx0, mx1, t0, t0 + 6.5, 3.6, 11)
        mw['x+'] += wins_row(my0, my1, t0, t0 + 6.5, 3.6, 3)
        mw['x-'] += wins_row(my0, my1, t0, t0 + 6.5, 3.6, 3)
    md.add(box(mx0, my0, 0, mx1, my1, ze, m_brickwall, wall=wall, wins=mw,
               doors={'y+': [(30.0, 36.0, 0, 8.0, 'steel')]}))
    # stair tower with a water tank on top
    md.add(box(64.0, 8.0, 0, 74.0, 20.0, ze + 10.0, m_brickwall, wall=wall,
               wins={'y+': [(67.0, 71.0, z, z + 6.0) for z in (8.0, 20.0, 32.0, 44.0)],
                     'x+': [(12.0, 16.0, z, z + 6.0) for z in (8.0, 20.0, 32.0, 44.0)]}))
    md.add(Cone(69.0, 14.0, ze + 10.0, ze + 18.0, 5.0, 5.0, m_concrete_round, cap=True, key='steel', capkey='roof_dark'))
    # sawtooth hall in front
    hx0, hx1, hy0, hy1 = 4.0, 60.0, 30.0, 70.0
    hw = 14.0
    md.add(box(hx0, hy0, 0, hx1, hy1, hw, m_brickwall, wall=wall,
               wins={'y+': [(hx0 + 4 + 9 * i, hx0 + 10 + 9 * i, 4.0, 11.0) for i in range(6)],
                     'x+': [(hy0 + 4 + 9 * i, hy0 + 10 + 9 * i, 4.0, 11.0) for i in range(4)],
                     'x-': [(hy0 + 4 + 9 * i, hy0 + 10 + 9 * i, 4.0, 11.0) for i in range(4)]},
               doors={'x+': [(hy0 + 12.0, hy0 + 22.0, 0, 10.0, 'steel')]}))
    tw = 8.0
    for i in range(int((hx1 - hx0) / tw)):
        xa = hx0 + i * tw
        xb = xa + tw
        h = 6.0
        s = h / tw
        pl_ = [(-1, 0, 0, -xa, 'x-'), (1, 0, 0, xb, 'glass'), (0, -1, 0, -hy0, 'y-'), (0, 1, 0, hy1, 'y+'),
               (0, 0, -1, -hw, 'bot'), (-s, 0, 1, hw - s * xa, 'slope')]
        md.add(Convex(pl_, (xa, hy0, hw, xb, hy1, hw + h), m_saw, end=wall))
    # boiler house + chimney
    md.add(box(70.0, 30.0, 0, 90.0, 50.0, 16.0, m_brickwall, wall=wall,
               wins={'y+': [(73.0, 77.0, 5.0, 12.0), (83.0, 87.0, 5.0, 12.0)], 'x+': [(34.0, 38.0, 5.0, 12.0)]},
               doors={'x+': [(42.0, 47.0, 0.0, 9.0, 'steel')]}))
    if white:
        md.add(Cone(80.0, 40.0, 16.0, chimney, 3.4, 2.6, m_concrete_round, cap=True, key='render_grey',
                    band=(chimney - 4.0, chimney, 'cap')))
    else:
        md.add(Cone(80.0, 40.0, 16.0, chimney, 4.0, 2.6, m_cone_brick, cap=True))
    md.hot = (80.0, 40.0, chimney + 1.0)
    md.add(HeightField(66.0, 54.0, 94.0, 74.0, 8.0,
                       lambda x, y: 8.0 * max(0.0, 1.0 - ((x - 80.0) / 12.0) ** 2 - ((y - 64.0) / 9.0) ** 2) ** 0.8,
                       m_flat('coal')))
    # gatehouse with clock
    md.add(box(70.0, 80.0, 0, 82.0, 90.0, 9.0, m_block, wall='render_grey', plinth=1.0,
               wins={'y+': [(72.0, 76.0, 3.0, 7.0)], 'x-': [(82.0, 86.0, 3.0, 7.0)]},
               doors={'y+': [(78.0, 80.5, 1.0, 7.5)]}, roofkey='bitumen'))
    add_birch(md, 6.0, 90.0, h=30, seed=seed + 130)
    add_birch(md, 30.0, 92.0, h=28, seed=seed + 131)
    gr = [(0.0, 72.0, 96.0, 96.0, 'asphalt'), (60.0, 24.0, 96.0, 72.0, 'gravel'), (0.0, 24.0, 64.0, 30.0, 'gravel')]
    md.ground = lambda g, st: g_lot(g, st, gravel=gr, var=seed)
    return md


# =========================================================================== services / utilities
def m_beams(g, st):
    """stacked steel stock seen as a box: rolled sections with dark gaps on the long faces, bright cut
    ends on the x faces"""
    p = g.solid.par
    key = p.get('key', 'steel')
    s, t = local_st(g)
    if g.tag in ('x+', 'x-'):
        if st.zoom >= 1 and ((s % 2.0) < 0.6 or (t % 1.6) < 0.5):
            return 'steel', -0.8
        return 'steel_lt', 0.4
    if g.tag == 'top':
        if st.zoom >= 1 and (g.P[1] % 2.0) < 0.6:
            return key, -0.6
        return key, 0.2
    if st.zoom >= 1 and (t % 1.6) < 0.5:
        return key, -0.8
    return key, 0.0


def yard_timber(name, seed=1):
    """ind_yard v0: board yard (lautatarha): three stickered board stacks of different heights under little
    board roofs, a low log pile, a board fence at the back"""
    md = Model(name)
    for (x0, y0, h) in ((3.0, 3.5, 9.0), (3.0, 12.0, 7.0), (17.5, 3.5, 6.0)):
        md.add(box(x0, y0, 0, x0 + 11.5, y0 + 6.5, h, m_timber))
        md.add(box(x0 - 0.3, y0 - 0.3, h, x0 + 11.8, y0 + 6.8, h + 0.6, m_flat('boards', 0.2)))
    md.add(box(4.0, 21.0, 0, 13.5, 25.0, 2.6, m_timber))                      # a fresh, unroofed stack
    md.add(box(18.5, 12.5, 0, 29.5, 22.5, 3.6, m_logs, ends='y'))              # log pile, cut ends to the road
    md.add(box(20.0, 13.5, 3.6, 28.0, 21.0, 6.0, m_logs, ends='y'))
    add_fence(md, 1.0, 1.0, 31.0, 1.0, key='fence_wood', h=4.0, step=1.6)
    add_fence(md, 1.0, 1.0, 1.0, 29.0, key='fence_wood', h=4.0, step=1.6)
    gr = [(1.5, 1.5, 32.0, 32.0, 'gravel'), (16.5, 12.0, 31.0, 23.0, 'sawdust')]
    md.ground = lambda g, st: g_lot(g, st, gravel=gr, var=seed)
    return md


def yard_steel(name, seed=2):
    """ind_yard v1: steel stockyard: an open shed (corrugated back wall, mono roof on posts) with racked bars,
    long rolled sections on trestles in front, a rusty scrap heap at the back"""
    md = Model(name)
    x0, y0, x1, y1 = 2.5, 2.5, 16.5, 13.5
    zlo, zhi = 8.5, 10.5
    md.add(box(x0, y0, 0, x1, y0 + 0.8, zhi - 0.3, m_corr, wall='corr'))      # back wall
    md.add(box(x0, y0, 0, x0 + 0.8, y1, zlo, m_corr, wall='corr'))           # gable-side wall
    for px in (x0 + 0.6, (x0 + x1) / 2.0, x1 - 0.6):
        md.add(box(px - 0.45, y1 - 1.0, 0, px + 0.45, y1 - 0.1, zlo + 0.3, m_flat('steel'), detail=True))
    md.add(box(x1 - 1.0, y0 + 0.8, 0, x1 - 0.1, y0 + 1.7, zlo + 0.3, m_flat('steel'), detail=True))
    md.add(mono_body(x0 - 0.6, y0 - 0.4, x1 + 0.6, y1 + 0.8, zlo, zlo + 0.7, zhi + 0.7, m_corr, wall='corr',
                     roofkey='roof_metal', rise='y-'))
    for (yy, h, key) in ((4.5, 3.5, 'steel'), (8.5, 2.6, 'rust')):            # racked bars under the roof
        md.add(box(4.0, yy, 0, 15.5, yy + 3.0, h, m_beams, key=key))
    # long rolled sections on three trestles along the front (the yard's signature)
    for xx in (4.0, 15.5, 27.0):
        md.add(box(xx, 17.0, 0, xx + 1.0, 25.5, 1.6, m_flat('fence_wood', -0.3)))
    md.add(box(3.0, 17.5, 1.6, 29.5, 21.0, 3.6, m_beams, key='steel_lt'))
    md.add(box(3.0, 21.8, 1.6, 29.5, 25.0, 3.0, m_beams, key='steel'))
    md.add(box(6.0, 18.0, 3.6, 26.0, 20.6, 5.0, m_beams, key='rust'))
    md.add(HeightField(18.5, 3.0, 30.5, 14.5, 5.0,
                       lambda x, y: 5.0 * max(0.0, 1.0 - ((x - 24.5) / 6.0) ** 2 - ((y - 8.7) / 5.5) ** 2) ** 0.7 *
                       (0.75 + 0.25 * ihash(int(x * 1.3), int(y * 1.3), 61)), m_flat('rust')))
    gr = [(0.0, 0.0, 32.0, 32.0, 'gravel'), (1.5, 1.5, 17.5, 15.0, 'concrete')]
    md.ground = lambda g, st: g_lot(g, st, gravel=gr, var=seed)
    return md


def yard_workshop(name, seed=3):
    """ind_yard v2: small brick workshop (pajarakennus): gable roof, double doors to the yard, a brick chimney,
    barrels and crates in front"""
    md = Model(name)
    x0, y0, x1, y1 = 5.0, 4.0, 25.0, 16.0
    ze, zr = 9.0, 9.0 + 6.0 * KZ * 0.8
    wins = {'y+': [(x0 + 12.0, x0 + 16.0, 3.0, 7.0)], 'y-': [(x0 + 3.0, x0 + 7.0, 3.0, 7.0), (x0 + 12.0, x0 + 16.0, 3.0, 7.0)],
            'x+': [(y0 + 3.5, y0 + 8.5, 3.0, 7.0)], 'x-': [(y0 + 3.5, y0 + 8.5, 3.0, 7.0)]}
    doors = {'y+': [(x0 + 3.0, x0 + 9.0, 0.0, 7.5, 'door')]}
    md.add(gable_body(x0, y0, x1, y1, 0, ze, zr, 'x', m_brickwall, wall='brick', wins=wins, doors=doors))
    md.add(gable_roof(x0, y0, x1, y1, ze, zr, 'x', m_roof, ov=1.0, ovg=1.0, th=1.2, key='roof_metal', edgekey='trim',
                      seam=3.0))
    md.add(box(x1 - 5.0, y0 + 2.0, ze, x1 - 2.5, y0 + 4.5, zr + 6.0, m_chimney, key='brick'))
    for i in range(3):
        md.add(Cone(19.0 + i * 3.0, 21.5, 0, 4.0, 1.3, 1.3, m_flat('rust' if i == 1 else 'steel'), cap=True))
    md.add(Cone(20.5, 24.6, 0, 4.0, 1.3, 1.3, m_flat('balc_blue'), cap=True))
    md.add(box(6.0, 20.0, 0, 11.0, 24.0, 3.5, m_flat('woodpile')))           # crates
    md.add(box(6.5, 20.5, 3.5, 10.0, 23.5, 6.5, m_flat('woodpile', 0.3)))
    md.add(box(26.5, 5.0, 0, 30.0, 15.0, 3.0, m_timber))                     # board stack by the gable
    add_fence(md, 1.0, 1.0, 31.0, 1.0, key='steel_lt', h=3.5, step=1.6)
    gr = [(0.0, 16.0, 32.0, 32.0, 'gravel'), (25.5, 2.0, 31.0, 16.0, 'gravel'), (2.0, 2.0, 5.0, 16.0, 'gravel')]
    md.ground = lambda g, st: g_lot(g, st, gravel=gr, var=seed)
    return md


def yard_coal(name, seed=4):
    """ind_yard v3: coal and storage yard: a weathered board storage shed with sliding doors, a coal heap in a
    low concrete bin, a handcart"""
    md = Model(name)
    x0, y0, x1, y1 = 3.0, 3.0, 14.0, 22.0
    ze, zr = 8.0, 8.0 + 5.5 * KZ * 0.8
    md.add(gable_body(x0, y0, x1, y1, 0, ze, zr, 'y', m_house, wall='w_weath', plinth=0.8, clad='v', frieze=False,
                      doors={'x+': [(y0 + 4.0, y0 + 12.0, 0.8, 7.0)]}, wins={'y+': [(x0 + 4.0, x0 + 7.0, 3.5, 6.0)]}))
    md.add(gable_roof(x0, y0, x1, y1, ze, zr, 'y', m_roof, ov=1.0, ovg=1.0, th=1.2, key='roof_dark', edgekey='trim',
                      seam=3.0))
    # bin walls (back and side) + heap
    md.add(box(17.0, 3.0, 0, 30.0, 4.2, 3.5, m_concrete, key='concrete_dk'))
    md.add(box(28.8, 3.0, 0, 30.0, 22.0, 3.5, m_concrete, key='concrete_dk'))
    md.add(HeightField(17.0, 4.2, 28.8, 22.0, 7.0,
                       lambda x, y: 7.0 * max(0.0, 1.0 - ((x - 23.5) / 6.5) ** 2 - ((y - 11.0) / 9.5) ** 2) ** 0.75,
                       m_flat('coal')))
    md.add(box(20.0, 25.0, 0, 24.0, 27.5, 2.6, m_flat('rust')))                # handcart
    md.add(box(19.0, 25.8, 1.2, 20.0, 26.7, 2.8, m_flat('fence_wood'), detail=True))
    add_woodpile(md, 4.0, 25.0, 13.0, 28.0, h=3.5)
    gr = [(0.0, 0.0, 32.0, 32.0, 'gravel'), (16.0, 2.5, 30.5, 23.0, 'dirt')]
    md.ground = lambda g, st: g_lot(g, st, gravel=gr, var=seed)
    return md


def town_hall(name, seed=1):
    """town hall (2x2): red brick, Saynatsalo-like: a two-storey wing around a raised green court,
    a taller council-chamber block with a mono-pitch roof, flagpoles at the steps"""
    md = Model(name, 2, 2)
    pl = 2.0
    ze = pl + 2 * STOREY
    wb = dict(wall='brick', plinth=pl, roofkey='bitumen', frame='trim', brickwork=True, plinthkey='concrete_dk')
    # back wing along X
    w1 = {'y+': wins_row(4.0, 40.0, pl + 3.0, pl + 8.5, 3.0, 6) + wins_row(4.0, 40.0, pl + 13.0, pl + 18.5, 3.0, 6),
          'y-': wins_row(4.0, 40.0, pl + 13.0, pl + 18.5, 3.0, 6), 'x-': wins_row(4.0, 20.0, pl + 13.0, pl + 18.5, 3.0, 2)}
    md.add(box(4.0, 4.0, 0, 40.0, 20.0, ze, m_block, wins=w1, **wb))
    md.add(mono_body(3.4, 3.4, 40.0, 20.6, ze, ze + 1.0, ze + 3.5, m_flat('roof_felt'), rise='y-'))
    # side wing along Y
    w2 = {'x-': wins_row(20.0, 50.0, pl + 13.0, pl + 18.5, 3.0, 5) + wins_row(20.0, 50.0, pl + 3.0, pl + 8.5, 3.0, 5),
          'x+': wins_row(20.0, 50.0, pl + 13.0, pl + 18.5, 3.0, 5), 'y+': wins_row(4.0, 20.0, pl + 13.0, pl + 18.5, 3.0, 2)}
    md.add(box(4.0, 20.0, 0, 20.0, 50.0, ze, m_block, wins=w2, doors={'x+': [(42.0, 46.0, pl, pl + 8.0)]}, **wb))
    md.add(mono_body(3.4, 20.0, 20.6, 50.6, ze, ze + 1.0, ze + 3.5, m_flat('roof_felt'), rise='x-'))
    # council chamber block: taller, closed brick with a high window strip, steep mono-pitch roof
    cw = {'y+': [(43.0, 59.0, ze + 6.0, ze + 10.0)], 'x+': [(8.0, 18.0, ze + 6.0, ze + 10.0)],
          'x-': [(8.0, 18.0, ze + 6.0, ze + 10.0)]}
    md.add(box(42.0, 4.0, 0, 60.0, 22.0, ze + 14.0, m_block, wins=cw,
               doors={'y+': [(48.0, 53.0, pl, pl + 9.0)]}, **wb))
    md.add(mono_body(41.4, 3.4, 60.6, 22.6, ze + 14.0, ze + 14.5, ze + 24.0, m_flat('roof_felt', 0.3), rise='y-'))
    # raised courtyard with lawn, steps
    md.add(box(20.0, 20.0, 0, 42.0, 50.0, 2.0, m_concrete, key='concrete'))
    md.add(box(21.0, 21.0, 2.0, 41.0, 49.0, 2.2, m_flat('grass', 0.2)))
    md.add(box(26.0, 50.0, 0, 36.0, 54.0, 1.0, m_concrete, key='concrete'))
    for fx in (46.0, 52.0, 58.0):
        add_flagpole(md, fx, 34.0, 36.0, flag=(fx == 52.0))
    add_birch(md, 6.0, 58.0, h=32, seed=seed + 140)
    add_birch(md, 56.0, 58.0, h=30, seed=seed + 141)
    add_bush(md, 22.0, 58.0, seed=seed + 142)
    gr = [(0.0, 54.0, 64.0, 64.0, 'gravel'), (24.0, 52.0, 38.0, 64.0, 'gravel'), (42.0, 22.0, 62.0, 54.0, 'gravel')]
    md.ground = lambda g, st: g_lot(g, st, gravel=gr, var=seed)
    return md


def coal_plant(name, seed=1):
    """coal power plant (2x2): brick boiler house, turbine hall with tall windows, two chimneys,
    coal heap with an inclined conveyor, transformer yard"""
    md = Model(name, 2, 2)
    # turbine hall (front, along X)
    tw = {'y+': [(6.0 + 7 * i, 10.0 + 7 * i, 3.0, 17.0) for i in range(5)],
          'x+': [(36.0, 40.0, 3.0, 17.0), (44.0, 48.0, 3.0, 17.0)], 'x-': [(36.0, 40.0, 3.0, 17.0), (44.0, 48.0, 3.0, 17.0)]}
    md.add(gable_body(4.0, 32.0, 42.0, 52.0, 0, 20.0, 25.0, 'x', m_brickwall, wins=tw,
                      doors={'x+': [(40.5, 43.5, 0, 9.0, 'steel')]}))
    md.add(gable_roof(4.0, 32.0, 42.0, 52.0, 20.0, 25.0, 'x', m_roof, ov=0.8, ovg=0.8, th=1.2, key='roof_dark',
                      edgekey='brick_dk', seam=4.0))
    # boiler house (tall, back)
    bw = {'y+': [(8.0, 12.0, 26.0, 34.0), (18.0, 22.0, 26.0, 34.0), (28.0, 32.0, 26.0, 34.0)],
          'x+': [(10.0, 14.0, 6.0, 14.0), (10.0, 14.0, 26.0, 34.0), (20.0, 24.0, 26.0, 34.0)],
          'x-': [(10.0, 14.0, 26.0, 34.0), (20.0, 24.0, 26.0, 34.0)], 'y-': [(8.0, 12.0, 26.0, 34.0), (28.0, 32.0, 26.0, 34.0)]}
    md.add(box(4.0, 4.0, 0, 42.0, 32.0, 40.0, m_brickwall, wins=bw))
    for (cx, cy) in ((14.0, 12.0), (32.0, 12.0)):
        md.add(Cone(cx, cy, 40.0, 112.0, 3.4, 2.4, m_cone_brick, cap=True))
    md.hot = (32.0, 12.0, 113.0)
    # coal heap + conveyor
    md.add(HeightField(44.0, 4.0, 63.0, 30.0, 11.0,
                       lambda x, y: 11.0 * max(0.0, 1.0 - ((x - 54.0) / 9.0) ** 2 - ((y - 16.0) / 11.0) ** 2) ** 0.8,
                       m_flat('coal')))
    C = 2.4 * 54.0 + 8.0
    md.add(Convex([(-1, 0, 0, -42.0, 'x-'), (1, 0, 0, 54.0, 'x+'), (0, -1, 0, -19.0, 'y-'), (0, 1, 0, 22.0, 'y+'),
                   (-2.4, 0, -1, -(C - 4.0), 'bot'), (2.4, 0, 1, C, 'top')], (42.0, 19.0, 0, 54.0, 22.0, 36.0),
                  m_corr, wall='corr', roofkey='corr'))
    for lx in (46.0, 50.5):
        md.add(box(lx - 0.5, 20.0, 0, lx + 0.5, 21.0, C - 4.0 - 2.4 * (lx + 0.5), m_flat('steel'), detail=True))
    # transformer yard
    for i in range(3):
        tx = 46.0 + i * 5.5
        md.add(box(tx, 40.0, 0, tx + 3.0, 44.0, 6.0, m_flat('steel')))
        md.add(box(tx + 1.2, 41.6, 6.0, tx + 1.8, 42.4, 9.0, m_flat('trim'), detail=True))
    for px in (45.0, 62.0):
        md.add(box(px - 0.4, 36.0, 0, px + 0.4, 36.8, 14.0, m_flat('steel_lt'), detail=True))
        md.add(box(px - 0.4, 50.0, 0, px + 0.4, 50.8, 14.0, m_flat('steel_lt'), detail=True))
    md.add(box(44.6, 36.0, 13.0, 62.4, 36.8, 13.8, m_flat('steel_lt'), detail=True))
    md.add(box(44.6, 50.0, 13.0, 62.4, 50.8, 13.8, m_flat('steel_lt'), detail=True))
    add_fence(md, 44.0, 34.5, 63.0, 34.5, key='steel_lt', h=3.0, step=1.6)
    gr = [(0.0, 52.0, 64.0, 64.0, 'asphalt'), (42.0, 0.0, 64.0, 34.0, 'dirt'), (44.0, 34.0, 64.0, 52.0, 'gravel')]
    md.ground = lambda g, st: g_lot(g, st, gravel=gr, var=seed)
    return md


def pump_house(name, seed=1):
    """pumping station (1x1): small rendered pump house with a gable roof, intake pipe, fence"""
    md = Model(name)
    x0, x1, y0, y1 = 8.0, 22.0, 7.0, 19.0
    ze = 10.0
    md.add(gable_body(x0, y0, x1, y1, 0, ze, ze + 6.0 * KZ * 0.8, 'x', m_block, wall='render_grey', plinth=1.5,
                      wins={'x+': [(10.0, 15.0, 3.5, 8.0)], 'x-': [(10.0, 15.0, 3.5, 8.0)],
                            'y-': [(11.0, 15.0, 3.5, 8.0)], 'y+': [(x1 - 6.0, x1 - 2.0, 3.5, 8.0)]},
                      doors={'y+': [(x0 + 2.5, x0 + 5.5, 1.0, 8.5, 'steel')]}, roofkey='roof_metal'))
    md.add(gable_roof(x0, y0, x1, y1, ze, ze + 6.0 * KZ * 0.8, 'x', m_roof, ov=1.0, ovg=1.0, th=1.2, key='roof_metal',
                      edgekey='trim', seam=3.0))
    md.add(box(x1, 11.5, 1.5, 31.5, 14.5, 4.5, m_concrete_pipe))          # intake pipe toward the shore side
    md.add(box(26.0, 10.5, 0, 28.0, 15.5, 6.0, m_concrete, key='concrete_dk'))
    add_fence(md, 1.0, 1.0, 1.0, 30.0, key='steel_lt', h=3.0, step=1.6)
    add_fence(md, 1.0, 30.5, 22.0, 30.5, key='steel_lt', h=3.0, step=1.6, gaps=((8.0, 13.0),))
    add_bush(md, 4.5, 24.0, seed=seed + 150)
    gr = [(x0 + 2.0, y1, x0 + 6.0, 32.0, 'gravel')]
    md.ground = lambda g, st: g_lot(g, st, gravel=gr, var=seed)
    return md


def m_concrete_pipe(g, st):
    if g.tag == 'top':
        return 'concrete', 0.4
    if g.tag in ('y+', 'y-') and abs(g.P[2] - 3.0) < 0.6:
        return 'concrete', 0.2
    return 'concrete_dk', 0.0


def well_house(name, seed=1):
    """groundwater well (1x1, symmetric): round concrete well head with a conical roof, fenced"""
    md = Model(name)
    md.add(Cone(16.0, 16.0, 0, 8.0, 5.5, 5.5, m_concrete_round, cap=False, key='concrete', band=(0.0, 1.5, 'concrete_dk')))
    md.add(Cone(16.0, 16.0, 8.0, 14.0, 7.0, 1.0, m_flat('roof_metal'), cap=True))
    md.add(Cone(16.0, 16.0, 14.0, 16.0, 0.6, 0.6, m_flat('steel'), cap=True))
    md.add(box(15.0, 21.0, 0, 17.0, 21.6, 6.0, m_flat('steel', -0.4)))
    for (ax, ay, bx, by) in ((4.0, 4.0, 28.0, 4.0), (28.0, 4.0, 28.0, 28.0), (4.0, 28.0, 28.0, 28.0), (4.0, 4.0, 4.0, 28.0)):
        add_fence(md, ax, ay, bx, by, key='fence_wood', h=3.0, step=2.0,
                  gaps=((10.0, 14.0),) if (ay == 28.0 and by == 28.0) else ())
    md.ground = lambda g, st: g_lot(g, st, gravel=[(10.0, 10.0, 22.0, 22.0, 'gravel'), (14.0, 22.0, 18.0, 32.0, 'gravel')],
                                    var=seed)
    return md


def water_tower(name, design=0, seed=1):
    """water tower (1x1, symmetric): design 0 mushroom (1950s), 1 saucer on a slim shaft (1960s)"""
    md = Model(name)
    c = 16.0
    if design == 0:
        md.add(Cone(c, c, 0, 58.0, 4.2, 3.4, m_concrete_round, cap=False, key='render_white',
                    wins=[(20.0, 23.0, 3), (40.0, 43.0, 3)]))
        md.add(Cone(c, c, 58.0, 68.0, 3.4, 12.0, m_concrete_round, cap=False, key='render_white'))
        md.add(Cone(c, c, 68.0, 80.0, 12.0, 12.0, m_concrete_round, cap=False, key='render_white', ribs=24,
                    band=(77.0, 80.0, 'render_grey')))
        md.add(Cone(c, c, 80.0, 83.0, 12.4, 10.0, m_concrete_round, cap=True, key='render_grey', capkey='render_grey'))
        md.add(Cone(c, c, 83.0, 86.0, 2.0, 2.0, m_concrete_round, cap=True, key='render_grey'))
    else:
        md.add(Cone(c, c, 0, 62.0, 3.2, 2.6, m_concrete_round, cap=False, key='concrete',
                    wins=[(14.0, 16.0, 2), (30.0, 32.0, 2), (46.0, 48.0, 2)]))
        md.add(Cone(c, c, 62.0, 71.0, 2.6, 13.0, m_concrete_round, cap=False, key='render_white'))
        md.add(Cone(c, c, 71.0, 73.0, 13.0, 13.0, m_concrete_round, cap=False, key='render_white',
                    band=(71.0, 73.0, 'glass')))
        md.add(Cone(c, c, 73.0, 79.0, 13.0, 5.0, m_concrete_round, cap=True, key='render_white', capkey='render_grey'))
        md.add(Cone(c, c, 79.0, 81.0, 1.0, 1.0, m_flat('steel'), cap=True))
    md.add(box(c - 2.0, c + 3.0, 0, c + 2.0, c + 5.0, 6.0, m_flat('door')))
    add_spruce(md, 4.5, 26.0, h=30, seed=seed + 1, r=4.5)
    add_birch(md, 27.0, 5.0, h=28, seed=seed + 2)
    gr = [(c - 2.0, c, c + 2.0, 32.0, 'gravel')]
    md.ground = lambda g, st: g_lot(g, st, gravel=gr, var=seed)
    return md


def fire_station(name, seed=1):
    """fire station (1x1): rendered hall with red garage doors and a slender hose-drying tower"""
    md = Model(name)
    x0, x1, y0, y1 = 4.0, 24.0, 6.0, 22.0
    ze = 13.0
    zr = ze + 8.0 * KZ * 0.6
    md.add(gable_body(x0, y0, x1, y1, 0, ze, zr, 'x', m_block, wall='render_yel', plinth=1.0,
                      wins={'y-': wins_row(x0, x1, 4.0, 9.0, 3.5, 4), 'x+': [(9.0, 13.0, 4.0, 9.0), (15.0, 19.0, 4.0, 9.0)],
                            'x-': [(9.0, 13.0, 4.0, 9.0), (15.0, 19.0, 4.0, 9.0), (12.0, 16.0, ze + 1.5, ze + 5.0)],
                            'y+': [(x0 + 15.5, x0 + 18.5, 4.0, 9.0)]},
                      doors={'y+': [(x0 + 1.5, x0 + 7.0, 0.0, 9.5, 'firedoor'), (x0 + 8.5, x0 + 14.0, 0.0, 9.5, 'firedoor')]},
                      bands={'y+': [(x0 + 1.5, x0 + 14.0, 10.3, 12.3, 'sign_red', 'text')]}, roofkey='roof_metal'))
    md.add(gable_roof(x0, y0, x1, y1, ze, zr, 'x', m_roof, ov=1.0, ovg=1.0, th=1.2, key='roof_metal', edgekey='trim', seam=3.0))
    # hose tower (letkutorni)
    tx0, tx1, ty0, ty1 = 24.5, 30.5, 6.0, 12.0
    th = 52.0
    md.add(box(tx0, ty0, 0, tx1, ty1, th, m_block, wall='render_yel', plinth=1.0, roofkey='bitumen',
               wins={'y+': [(26.0, 29.0, z, z + 4.0) for z in (8.0, 22.0, 36.0)] + [(25.5, 29.5, th - 7.0, th - 2.0)],
                     'x+': [(7.5, 10.5, z, z + 4.0) for z in (15.0, 29.0)] + [(7.0, 11.0, th - 7.0, th - 2.0)],
                     'x-': [(7.0, 11.0, th - 7.0, th - 2.0)], 'y-': [(25.5, 29.5, th - 7.0, th - 2.0)]}))
    md.add(hip_body(tx0 - 0.6, ty0 - 0.6, tx1 + 0.6, ty1 + 0.6, th, th, th + 5.0, m_hiproof, wallmat=m_block,
                    roofkey='roof_metal', wall='render_yel', plinth=-1))
    md.add(Cone(27.5, 9.0, th + 5.0, th + 9.0, 0.25, 0.25, m_flat('steel_lt'), detail=True))
    gr = [(0.0, y1, 26.0, 32.0, 'asphalt'), (24.0, 12.0, 32.0, 32.0, 'gravel')]
    add_bush(md, 29.0, 26.0, seed=seed + 160)
    md.ground = lambda g, st: g_lot(g, st, gravel=gr, var=seed)
    return md


def school(name, seed=1, wall='render_yel'):
    """school (2x2, kansakoulu): two-storey rendered block with tall classroom windows, hipped roof,
    a lower gym wing, a gravel schoolyard with a flagpole"""
    md = Model(name, 2, 2)
    pl = 2.0
    x0, x1, y0, y1 = 4.0, 56.0, 4.0, 20.0
    ze = pl + 2 * STOREY + 3.0
    zr = ze + 9.0
    tw = []
    for k in range(2):
        t0 = pl + k * 11.5 + 2.0
        tw.append((t0, t0 + 8.0))
    wy = []
    for (t0, t1) in tw:
        wy += wins_row(x0, x1, t0, t1, 5.0, 7, skip=((27.0, 33.0),))
    wins = {'y+': wy + [(28.5, 31.5, pl + 13.5, pl + 21.0)], 'y-': [r for r in wy],
            'x+': [(y0 + 4.0, y0 + 9.0, t0, t1) for (t0, t1) in tw], 'x-': [(y0 + 4.0, y0 + 9.0, t0, t1) for (t0, t1) in tw]}
    md.add(hip_body(x0, y0, x1, y1, 0, ze, zr, m_hiproof, wallmat=m_block, wall=wall, plinth=pl, wins=wins,
                    doors={'y+': [(28.0, 32.0, pl, pl + 9.0)]}, roofkey='roof_metal', wkind='tall', inset=9.0))
    md.add(box(28.0 - 1.0, y1, pl + 9.5, 32.0 + 1.0, y1 + 2.5, pl + 10.3, m_flat('trim')))
    md.add(box(26.0, 9.0, ze + 2.0, 28.6, 11.6, zr + 2.0, m_chimney, key='brick'))
    # gym wing (one tall storey) along Y on the left
    gw = {'x+': wins_row(20.0, 44.0, pl + 5.0, pl + 13.0, 3.0, 5), 'x-': wins_row(20.0, 44.0, pl + 5.0, pl + 13.0, 3.0, 5),
          'y+': [(9.0, 15.0, pl + 5.0, pl + 13.0)]}
    md.add(gable_body(4.0, 20.0, 20.0, 44.0, 0, pl + 14.0, pl + 14.0 + 5.0, 'y', m_block, wall=wall, plinth=pl,
                      wins=gw, roofkey='roof_metal'))
    md.add(gable_roof(4.0, 20.0, 20.0, 44.0, pl + 14.0, pl + 19.0, 'y', m_roof, ov=1.0, ovg=1.0, th=1.2,
                      key='roof_metal', edgekey='trim', seam=3.0))
    add_flagpole(md, 40.0, 40.0, 36.0, flag=True)
    for i in range(5):        # bike rack
        md.add(box(46.0 + i * 1.6, 26.0, 0, 46.4 + i * 1.6, 29.0, 2.5, m_flat('steel_lt'), detail=True))
    add_birch(md, 60.0, 60.0, h=32, seed=seed + 170)
    add_birch(md, 6.0, 59.0, h=30, seed=seed + 171)
    add_pine(md, 60.0, 30.0, h=38, seed=seed + 172)
    gr = [(20.0, 20.0, 58.0, 54.0, 'gravel'), (28.0, 54.0, 32.0, 64.0, 'gravel')]
    md.ground = lambda g, st: g_lot(g, st, gravel=gr, var=seed)
    return md


def clinic(name, seed=1):
    """health clinic (1x1): two-storey white rendered block, flat roof, window bands, canopy"""
    md = Model(name)
    pl = 1.0
    x0, x1, y0, y1 = 5.0, 25.0, 5.0, 21.0
    ze = pl + 2 * STOREY + 1.5
    wb = []
    for k in range(2):
        t0 = pl + k * STOREY + 3.0
        wb.append((t0, t0 + 4.5))
    wins = {'y+': [(x0 + 1.5, x0 + 9.0, t0, t1) for (t0, t1) in wb] + [(x0 + 13.0, x1 - 1.5, wb[1][0], wb[1][1])],
            'y-': [(x0 + 1.5, x1 - 1.5, t0, t1) for (t0, t1) in wb],
            'x+': [(y0 + 1.5, y1 - 1.5, t0, t1) for (t0, t1) in wb], 'x-': [(y0 + 1.5, y1 - 1.5, t0, t1) for (t0, t1) in wb]}
    md.add(box(x0, y0, 0, x1, y1, ze, m_block, wall='render_white', plinth=pl, wins=wins,
               doors={'y+': [(x0 + 10.0, x0 + 12.5, pl, pl + 7.5)]},
               bands={'y+': [(x0 + 13.0, x1 - 1.5, pl + 3.0, pl + 7.0, 'sign_blue', 'text')]},
               roofkey='bitumen', wkind='band', frame='panel_dk'))
    md.add(box(x0 + 8.5, y1, pl + 7.8, x0 + 14.0, y1 + 3.0, pl + 8.6, m_flat('panel_dk', 0.6)))
    md.add(box(x0 + 3.0, y0 + 3.0, ze, x0 + 6.0, y0 + 6.0, ze + 2.5, m_flat('steel')))
    md.add(box(27.0, 24.0, 0, 29.0, 24.6, 9.0, m_flat('steel', -0.3), detail=True))
    md.add(box(26.0, 23.9, 6.0, 30.0, 24.7, 9.0, m_flat('sign_blue', 0.4), detail=True))
    add_birch(md, 28.0, 8.0, h=30, seed=seed + 180)
    add_bush(md, 4.0, 26.0, seed=seed + 181)
    gr = [(x0 + 9.5, y1, x0 + 13.0, 32.0, 'gravel'), (16.0, 24.0, 32.0, 32.0, 'asphalt')]
    md.ground = lambda g, st: g_lot(g, st, gravel=gr, var=seed)
    return md


def public_sauna(name, seed=1):
    """public sauna (1x1): dark-stained wooden bath house, tall brick chimney (smoke), woodpile, sign"""
    md = Model(name)
    pl = 1.5
    x0, x1, y0, y1 = 5.0, 25.0, 7.0, 20.0
    ze = pl + 9.0
    zr = ze + 6.5 * KZ * 0.8
    md.add(gable_body(x0, y0, x1, y1, 0, ze, zr, 'x', m_house, wall='w_brown', plinth=pl,
                      wins={'y+': [(x0 + 2.5, x0 + 6.5, pl + 2.5, pl + 7.0), (x1 - 6.5, x1 - 2.5, pl + 5.0, pl + 7.5)],
                            'y-': [(x0 + 3.0, x0 + 7.0, pl + 5.0, pl + 7.5), (x1 - 8.0, x1 - 4.0, pl + 5.0, pl + 7.5)],
                            'x+': [(y0 + 3.0, y0 + 6.0, pl + 5.0, pl + 7.5)], 'x-': [(y0 + 3.0, y0 + 7.0, pl + 2.5, pl + 7.0)]},
                      doors={'y+': [(x0 + 9.0, x0 + 12.0, pl, pl + 7.5)]}, clad='v', eave=ze))
    md.add(gable_roof(x0, y0, x1, y1, ze, zr, 'x', m_roof, ov=1.2, ovg=1.2, th=1.3, key='roof_felt', edgekey='trim', seam=3.0))
    md.add(box(x0 + 8.0, y1, pl + 9.0, x0 + 13.0, y1 + 0.5, ze - 0.2, m_block, wall='sign_yel', plinth=-1,
               bands={'y+': [(x0 + 8.0, x0 + 13.0, pl + 9.0, ze - 0.2, 'sign_yel', 'text')]}))
    md.add(box(x1 - 6.0, y0 + 2.0, 0, x1 - 2.6, y0 + 5.4, zr + 12.0, m_chimney, key='brick'))
    md.hot = (x1 - 4.3, y0 + 3.7, zr + 12.5)
    add_woodpile(md, 26.5, 6.0, 30.5, 18.0, 4.5)
    add_bench(md, 6.0, 26.0)
    add_birch(md, 28.0, 27.0, h=30, seed=seed + 190)
    gr = [(x0 + 8.5, y1, x0 + 12.5, 32.0, 'gravel')]
    md.ground = lambda g, st: g_lot(g, st, gravel=gr, var=seed)
    return md


def park(name, variant=0, seed=1):
    """park (1x1, symmetric art): v0 paths + benches + birches + flowerbed, v1 playground,
    v2 statue on a plinth with a pond"""
    md = Model(name)
    gr = []
    if variant == 0:
        gr += [(14.5, 0.0, 17.5, 32.0, 'gravel'), (0.0, 14.5, 32.0, 17.5, 'gravel'), (12.0, 12.0, 20.0, 20.0, 'flowerbed')]
        add_birch(md, 7.0, 7.0, h=32, seed=seed + 200)
        add_birch(md, 25.0, 25.0, h=30, seed=seed + 201)
        add_rowan(md, 25.0, 7.0, seed=seed + 202)
        add_bush(md, 7.0, 25.0, seed=seed + 203, key='lilac')
        add_bench(md, 22.0, 13.0)
        add_bench(md, 10.0, 19.0)
    elif variant == 1:
        gr += [(4.0, 4.0, 28.0, 28.0, 'sandpile'), (14.0, 28.0, 18.0, 32.0, 'gravel')]
        add_swings(md, 11.0, 10.0)
        add_sandbox(md, 21.0, 20.0)
        md.add(Convex([(-1, 0, 0, -8.0, 'x-'), (1, 0, 0, 16.0, 'x+'), (0, -1, 0, -18.0, 'y-'), (0, 1, 0, 20.0, 'y+'),
                       (0, 0, -1, 0, 'bot'), (0.75, 0, 1, 0.75 * 16.0 + 0.5, 'top')], (8.0, 18.0, 0, 16.0, 20.0, 7.0),
                      m_flat('balc_blue')))
        add_birch(md, 27.0, 5.0, h=32, seed=seed + 204)
        add_pine(md, 5.0, 27.0, h=38, seed=seed + 205)
        add_bench(md, 24.0, 28.5)
    else:
        gr += [(6.0, 6.0, 26.0, 26.0, 'gravel'), (9.0, 17.0, 23.0, 25.0, 'water'), (14.0, 26.0, 18.0, 32.0, 'gravel')]
        md.add(box(8.6, 16.6, 0, 23.4, 17.2, 1.0, m_concrete, key='concrete'))
        md.add(box(8.6, 24.8, 0, 23.4, 25.4, 1.0, m_concrete, key='concrete'))
        md.add(box(8.6, 16.6, 0, 9.2, 25.4, 1.0, m_concrete, key='concrete'))
        md.add(box(22.8, 16.6, 0, 23.4, 25.4, 1.0, m_concrete, key='concrete'))
        md.add(box(13.5, 9.0, 0, 18.5, 14.0, 6.0, m_concrete, key='rock'))
        md.add(Cone(16.0, 11.5, 6.0, 13.0, 1.2, 0.9, m_flat('copper'), cap=True))
        md.add(Ellipsoid(16.0, 11.5, 14.0, 1.1, 1.1, 1.5, m_flat('copper', 0.3)))
        add_birch(md, 4.0, 4.0, h=30, seed=seed + 206)
        add_birch(md, 28.0, 4.0, h=32, seed=seed + 207)
        add_spruce(md, 4.0, 28.0, h=30, seed=seed + 208, r=4.5)
        add_bush(md, 28.0, 28.0, seed=seed + 209, key='lilac')
    md.ground = lambda g, st: g_lot(g, st, gravel=gr, var=seed)
    return md


def element_factory(name, seed=1):
    """element factory (3x3): long corrugated casting hall, outdoor casting beds under a gantry crane,
    stacks of grey wall panels, boiler chimney (smoke), office"""
    md = Model(name, 3, 3)
    hx0, hx1, hy0, hy1 = 4.0, 70.0, 6.0, 34.0
    hz = 18.0
    md.add(gable_body(hx0, hy0, hx1, hy1, 0, hz, hz + 6.0, 'x', m_corr, wall='corr', roofkey='roof_dark',
                      doors={'x+': [(hy0 + 6.0, hy1 - 6.0, 0, 15.0, 'steel')], 'y+': [(20.0, 30.0, 0, 12.0, 'steel')]},
                      wins={'y+': [(hx0 + 3 + 8 * i, hx0 + 8 + 8 * i, 12.0, 16.0) for i in range(8)],
                            'y-': [(hx0 + 3 + 8 * i, hx0 + 8 + 8 * i, 12.0, 16.0) for i in range(8)]},
                      frame='steel_lt'))
    md.add(gable_roof(hx0, hy0, hx1, hy1, hz, hz + 6.0, 'x', m_roof, ov=0.8, ovg=0.8, th=1.2, key='corr', edgekey='steel_lt', seam=2.0))
    # boiler house + steel chimney
    md.add(box(74.0, 8.0, 0, 90.0, 22.0, 14.0, m_brickwall, wins={'y+': [(77.0, 81.0, 5.0, 10.0)]},
               doors={'x+': [(10.0, 15.0, 0, 9.0, 'steel')]}))
    md.add(Cone(86.0, 15.0, 14.0, 62.0, 2.2, 1.8, m_concrete_round, cap=True, key='steel', band=(58.0, 62.0, 'cap')))
    md.hot = (86.0, 15.0, 63.0)
    # casting beds + gantry crane
    for i in range(3):
        md.add(box(8.0, 44.0 + i * 9.0, 0, 62.0, 50.0 + i * 9.0, 1.2, m_concrete, key='concrete', joints=6.0))
    for gx in (6.0, 64.0):
        for gy in (42.0, 70.0):
            md.add(box(gx - 0.8, gy - 0.8, 0, gx + 0.8, gy + 0.8, 24.0, m_flat('balc_mustard'), detail=True))
        md.add(box(gx - 0.8, 41.2, 22.0, gx + 0.8, 70.8, 24.0, m_flat('balc_mustard'), detail=True))
    md.add(box(5.2, 54.0, 23.0, 64.8, 57.0, 25.5, m_flat('balc_mustard', 0.3)))
    md.add(box(30.0, 54.6, 17.0, 31.0, 56.4, 23.0, m_flat('steel'), detail=True))
    # panel stacks (grey precast wall elements standing in racks)
    for (sx, sy) in ((70.0, 44.0), (70.0, 58.0), (8.0, 76.0), (30.0, 76.0), (52.0, 76.0)):
        for k in range(4):
            md.add(box(sx + k * 2.6, sy, 0, sx + k * 2.6 + 1.6, sy + 12.0, 10.0, m_concrete, key='panel', joints=12.0))
    # office
    md.add(box(74.0, 76.0, 0, 92.0, 90.0, 2 * STOREY + 2.0, m_block, wall='panel', plinth=1.0, joints=(STOREY, 6.0),
               wins={'y+': wins_row(74.0, 92.0, 3.5, 8.0, 3.0, 4) + wins_row(74.0, 92.0, 13.5, 18.0, 3.0, 4),
                     'x-': wins_row(76.0, 90.0, 13.5, 18.0, 3.0, 3)},
               doors={'y+': [(80.0, 82.5, 1.0, 8.0)]}, roofkey='bitumen', frame='panel_dk'))
    gr = [(0.0, 36.0, 96.0, 96.0, 'gravel'), (0.0, 0.0, 96.0, 6.0, 'gravel'), (70.0, 6.0, 96.0, 36.0, 'gravel')]
    md.ground = lambda g, st: g_lot(g, st, gravel=gr, var=seed)
    return md


# =========================================================================== construction / ground
def construction(n, m, seed=1):
    """construction stage for an n x m footprint (canonical): bare earth, a concrete foundation
    with a first course of walls, scaffold poles, a site hut, a sand heap and a plank stack"""
    md = Model('build_%dx%d' % (n, m), n, m)
    W, D = n * 32.0, m * 32.0
    fx0, fy0, fx1, fy1 = 5.0, 5.0, W - 9.0, D - 10.0
    if n * m == 1:
        fx0, fy0, fx1, fy1 = 7.0, 6.0, 24.0, 21.0
    md.add(box(fx0, fy0, 0, fx1, fy1, 2.5, m_slab))
    wall_h = 8.0 if n * m > 1 else 5.0
    th = 1.6
    gaps = (fx0 + (fx1 - fx0) * 0.4, fx0 + (fx1 - fx0) * 0.4 + 4.0)
    md.add(box(fx0, fy0, 2.5, fx1, fy0 + th, 2.5 + wall_h, m_block, wall='concrete_dk', plinth=-1, joints=(3.0, 4.0)))
    md.add(box(fx0, fy0, 2.5, fx0 + th, fy1, 2.5 + wall_h, m_block, wall='concrete_dk', plinth=-1, joints=(3.0, 4.0)))
    md.add(box(fx1 - th, fy0, 2.5, fx1, fy1, 2.5 + wall_h * 0.6, m_block, wall='concrete_dk', plinth=-1, joints=(3.0, 4.0)))
    md.add(box(fx0, fy1 - th, 2.5, gaps[0], fy1, 2.5 + wall_h * 0.5, m_block, wall='concrete_dk', plinth=-1, joints=(3.0, 4.0)))
    # scaffold: poles at the corners + along the long sides, two plank levels
    sh = 2.5 + wall_h + 9.0
    poles = []
    step = 8.0
    x = fx0 - 1.5
    while x <= fx1 + 1.6:
        poles.append((x, fy0 - 1.5))
        poles.append((x, fy1 + 1.5))
        x += step
    for (px, py) in poles:
        md.add(box(px - 0.35, py - 0.35, 0, px + 0.35, py + 0.35, sh, m_flat('scaffold'), detail=True))
    for zz in (2.5 + wall_h * 0.5, 2.5 + wall_h + 2.0):
        md.add(box(fx0 - 2.0, fy0 - 2.2, zz, fx1 + 2.0, fy0 - 0.8, zz + 0.6, m_flat('woodpile', 0.3), detail=True))
        md.add(box(fx0 - 2.0, fy1 + 0.8, zz, fx1 + 2.0, fy1 + 2.2, zz + 0.6, m_flat('woodpile', 0.3), detail=True))
    # interior wall stubs, block pallets, cement mixer
    if n * m > 1:
        mx = (fx0 + fx1) / 2.0
        md.add(box(mx - th / 2, fy0, 2.5, mx + th / 2, fy0 + (fy1 - fy0) * 0.6, 2.5 + wall_h * 0.7, m_block,
                   wall='concrete_dk', plinth=-1, joints=(3.0, 4.0)))
        for k in range(min(4, n * m)):
            px = fx0 + 4.0 + k * 7.0
            if px + 4 < fx1 - 2:
                md.add(box(px, fy1 - 8.0, 2.5, px + 4.0, fy1 - 4.5, 2.5 + 2.5 + (k % 2) * 1.5, m_block,
                           wall='concrete', plinth=-1, joints=(1.5, 2.0)))
    md.add(Cone(W - 14.0 if n > 1 else 27.0, 4.5 if n > 1 else 27.0, 1.5, 5.5, 1.6, 1.0, m_flat('balc_orange'), cap=True))
    md.add(box((W - 15.0 if n > 1 else 26.0), (3.6 if n > 1 else 26.1), 0, (W - 13.0 if n > 1 else 28.0),
               (5.4 if n > 1 else 27.9), 1.5, m_flat('steel')))
    # site hut (tyomaakoppi), sand heap, plank stack, cement bags
    hx, hy = W - 7.5, D - 7.0
    md.add(box(hx - 5.5, hy - 3.5, 0, hx + 1.0, hy + 2.5, 7.0, m_house, wall='w_red', plinth=0.5, clad='v', frieze=False,
               wins={'y+': [(hx - 4.0, hx - 1.0, 3.0, 5.5)]}, doors={'x-': [(hy - 2.0, hy + 0.5, 0.5, 6.0)]}))
    md.add(box(hx - 6.0, hy - 4.0, 7.0, hx + 1.5, hy + 3.0, 7.8, m_flat('roof_dark', 0.4)))
    sx, sy = 6.0, D - 6.0
    md.add(HeightField(sx - 5.0, sy - 4.5, sx + 5.0, sy + 4.5, 5.0,
                       lambda x, y: 5.0 * max(0.0, 1.0 - ((x - sx) / 4.5) ** 2 - ((y - sy) / 4.0) ** 2) ** 0.9,
                       m_flat('sandpile')))
    if n * m > 1:
        md.add(box(W * 0.5 - 6.0, D - 6.5, 0, W * 0.5 + 6.0, D - 3.5, 3.0, m_timber))
    md.ground = lambda g, st: g_lot(g, st, gravel=[(1.0, 1.0, W - 1.0, D - 1.0, 'dirt')], var=seed)
    return md


def m_slab(g, st):
    """foundation slab: concrete with a rebar grid where the floor is not yet cast"""
    if g.tag == 'top':
        P = g.P
        if st.zoom >= 1 and ((P[0] % 3.0) < 0.6 or (P[1] % 3.0) < 0.6) and vnoise(P[0], P[1], 8.0, 97, 1e9) > 0.45:
            return 'rust', 0.3
        return 'concrete', 0.3
    return 'concrete_dk', 0.0


def lot_ground(n, m, seed=1):
    """flat lot ground for the overlay mode (DESIGN 11.4): light grass inside a gravel border.
    Symmetric, so one image serves every rotation (aliases in gen_art.py)."""
    md = Model('ground_%dx%d' % (n, m), n, m)
    W, D = n * 32.0, m * 32.0
    gr = [(1.5, 1.5, W - 1.5, D - 1.5, 'lawn')]
    border = [(0.0, 0.0, W, 1.5, 'gravel'), (0.0, D - 1.5, W, D, 'gravel'), (0.0, 0.0, 1.5, D, 'gravel'),
              (W - 1.5, 0.0, W, D, 'gravel')]
    md.ground = lambda g, st: g_lot(g, st, gravel=border + gr, var=seed)
    return md


# =========================================================================== bridges / power
def bridge_model(key, axis):
    """(axis = WORLD axis of the bridge; the model itself is canonical along X and rotated by the caller)"""
    """bridge over water: canonical along X; the deck surface uses the land road/rail ground so the
    markings continue across the tile edges; the deck fascia extends below the water plane so it reads
    as a span above deeper water while the deck top stays level with the land (1 px)."""
    md = Model('%s_bridge' % key)
    md.ground = lambda g, st: g_water(g, st, 0)
    hw = {'grav': 7.5, 'road': 8.5, 'main': 11.0, 'mway': 14.0, 'rail': 8.5}[key]
    c = 16.0
    conn = ('-x', '+x') if axis == 'x' else ('-y', '+y')
    deck = {'grav': 'woodpile', 'road': 'concrete', 'main': 'concrete', 'mway': 'concrete', 'rail': 'steel'}[key]

    def m_deck(g, st):
        if g.tag == 'top':
            if key == 'rail':
                return g_rail(g, st, conn, base=lambda gg, ss: ('ballast', 0.0))
            if key == 'grav':
                q = g.W[0] if axis == 'x' else g.W[1]
                return ('woodpile', -0.5) if (q % 2.0) < 0.6 and st.zoom >= 1 else ('woodpile', 0.2)
            return g_road(g, st, conn, key, base=lambda gg, ss: ('kerb', 0.0))
        if key == 'rail' and g.tag in ('y+', 'y-'):
            s = g.P[0]
            if (s % 4.0) < 0.6 and st.zoom >= 1:
                return 'steel', -0.5
            return 'steel', 0.0
        return deck, (-0.3 if g.P[2] < -1.5 else 0.0)

    z_top = 1.0
    fd = 4.5 if key != 'rail' else 6.5
    md.add(box(0.0, c - hw, -fd, 32.0, c + hw, z_top, m_deck))
    rail_h = 3.2
    post = 'fence_wood' if key == 'grav' else 'steel_lt'
    for side in (-1, 1):
        y = c + side * (hw - 0.5)
        for i in range(9):
            px = 0.5 + i * 3.875
            md.add(box(px - 0.3, y - 0.3, z_top, px + 0.3, y + 0.3, z_top + rail_h, m_flat(post), detail=True, noshadow=True))
        md.add(box(0.0, y - 0.35, z_top + rail_h - 0.7, 32.0, y + 0.35, z_top + rail_h, m_flat(post, 0.3), detail=True))
    if key in ('road', 'main', 'mway'):
        md.add(Cone(16.0, c + hw + 1.2, -6.0, 0.0, 2.0, 2.0, m_flat('concrete_dk'), cap=False, noshadow=True))
    md.top = 8.0
    return md, (0 if axis == 'x' else 1)


def power_model(mask, wires_only=False):
    """wooden pole + crossarms + 3 wires per arm.  X-line wires hang from the upper crossarm, Y-line
    wires from the lower one, so heights always match between neighbours.  wires_only: no pole (a road
    tile under the line), wires sag to the tile centre."""
    md = Model('power', ground=None)
    c = 16.0
    H = 34.0
    zx, zy = H - 2.2, H - 5.6          # insulator tops for X-line / Y-line wires
    has_x = bool(mask & (2 | 8))
    has_y = bool(mask & (1 | 4))
    if not wires_only:
        md.add(Cone(c, c, 0, H, 0.75, 0.6, m_flat('pole'), cap=True, detail=True))
        if has_x or not has_y:
            md.add(box(c - 0.5, c - 4.0, zx - 1.3, c + 0.5, c + 4.0, zx, m_flat('pole', -0.3), detail=True))
            for dy in (-3.2, 3.2):
                md.add(box(c - 0.4, c + dy - 0.4, zx, c + 0.4, c + dy + 0.4, zx + 1.4, m_flat('trim'), detail=True))
            md.add(box(c - 0.4, c - 0.4, H, c + 0.4, c + 0.4, H + 0.6, m_flat('trim'), detail=True))
        if has_y:
            md.add(box(c - 4.0, c - 0.5, zy - 1.3, c + 4.0, c + 0.5, zy, m_flat('pole', -0.3), detail=True))
            for dx in (-3.2, 0.0, 3.2):
                md.add(box(c + dx - 0.4, c - 0.4, zy, c + dx + 0.4, c + 0.4, zy + 1.4, m_flat('trim'), detail=True))
    sag_c = 0.0 if not wires_only else -2.0
    for bit in (1, 2, 4, 8):
        if not mask & bit:
            continue
        for off in (-3.2, 0.0, 3.2):
            if bit in (2, 8):
                zw = (zx + 1.4) if off else (H + 0.6)
                P_edge = (32.0 if bit == 2 else 0.0, c + off, zw - 2.0)
                P_c = (c, c + off, zw + sag_c)
            else:
                zw = zy + 1.4
                P_edge = (c + off, 32.0 if bit == 4 else 0.0, zw - 2.0)
                P_c = (c + off, c, zw + sag_c)
            md.lines.append((P_c, P_edge, 'wire', 0))
    md.top = H + 4.0
    return md

