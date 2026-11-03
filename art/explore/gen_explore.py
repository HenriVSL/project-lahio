#!/usr/bin/env python3
"""gen_explore.py - parametric isometric art generator for KAUPUNKI (style exploration).

Deterministic, Python 3 + Pillow only (no numpy).  Run:   python3 gen_explore.py [--quick]

Pipeline (see STYLE.md):
  1. MODELS   every sprite is a little 3-D model: convex solids (boxes, gable bodies, roof
              slabs), vertical (truncated) cones, ellipsoids and height fields, plus a ground
              material for its lot.  Units: X, Y in "tile units" (one tile = 32 x 32), Z in
              screen pixels of height.  Built in a canonical orientation, `rot` turns it.
  2. G-BUFFER an orthographic ray caster renders the model once per zoom into per-pixel
              records (material, local coords, light incl. ray-traced shadows, depth, class).
  3. STYLE    a style pass turns the G-buffer into colours: palette per material, light ramp,
              quantisation / ordered dither, outlines, texture density.  Same geometry, three
              looks (A, B, C).
  4. OUTPUT   sheets (<= 640 px wide, 24-bit BMP, key RGB(255,0,255)) + C header, previews and
              640x480 / 800x600 mockups with a period UI and a hand-drawn pixel font.
Projection:  sx = (X - Y) * zoom,  sy = ((X + Y) / 2 - Z) * zoom.  2:1 dimetric, true for a
camera at 30 deg elevation if Z_px = 1.2247 * Z_metric (KZ).
"""
import math
import os
import struct
import sys
import time

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
KEY = (255, 0, 255)
KZ = math.sqrt(1.5)        # pixels of height per metric unit (true 2:1 dimetric)
T = 32.0                   # tile side in world units
STOREY = 10.0              # px per storey (~3 m; 1 m = 2.67 units)
BIG = 1000.0


# =========================================================================== small math
def clamp(v, a=0.0, b=1.0):
    return a if v < a else b if v > b else v


def lerp(a, b, t):
    return a + (b - a) * t


def lerpc(c1, c2, t):
    return (lerp(c1[0], c2[0], t), lerp(c1[1], c2[1], t), lerp(c1[2], c2[2], t))


def norm3(v):
    l = math.sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]) or 1.0
    return (v[0] / l, v[1] / l, v[2] / l)


def hexc(s):
    s = s.lstrip('#')
    return (int(s[0:2], 16), int(s[2:4], 16), int(s[4:6], 16))


def to565(c):
    """round a colour to what RGB565 can show (so previews are honest), never the key"""
    r, g, b = [int(clamp(v, 0, 255) + 0.5) for v in c]
    r = (r >> 3) * 255 // 31
    g = (g >> 2) * 255 // 63
    b = (b >> 3) * 255 // 31
    if r > 240 and g < 12 and b > 240:
        g = 12
    return (r, g, b)


def ihash(*a):
    """deterministic integer hash -> [0,1)"""
    h = 2166136261
    for v in a:
        h ^= (int(v) & 0xffffffff)
        h = (h * 16777619) & 0xffffffff
        h ^= h >> 13
        h = (h * 1274126177) & 0xffffffff
    h ^= h >> 16
    return (h & 0xffffff) / 16777216.0


def vnoise(x, y, cell, seed, period=T):
    """periodic value noise in [0,1]; period in world units (32 = one tile -> seamless)"""
    p = max(1, int(round(period / cell)))
    fx, fy = x / cell, y / cell
    ix, iy = int(math.floor(fx)), int(math.floor(fy))
    tx, ty = fx - ix, fy - iy
    tx = tx * tx * (3 - 2 * tx)
    ty = ty * ty * (3 - 2 * ty)
    a = ihash(ix % p, iy % p, seed)
    b = ihash((ix + 1) % p, iy % p, seed)
    c = ihash(ix % p, (iy + 1) % p, seed)
    d = ihash((ix + 1) % p, (iy + 1) % p, seed)
    return lerp(lerp(a, b, tx), lerp(c, d, tx), ty)


def vnoise3(x, y, z, cell, seed):
    fx, fy, fz = x / cell, y / cell, z / cell
    ix, iy, iz = int(math.floor(fx)), int(math.floor(fy)), int(math.floor(fz))
    tx, ty, tz = fx - ix, fy - iy, fz - iz
    v = 0.0
    for dx in (0, 1):
        for dy in (0, 1):
            for dz in (0, 1):
                w = (tx if dx else 1 - tx) * (ty if dy else 1 - ty) * (tz if dz else 1 - tz)
                v += w * ihash(ix + dx, iy + dy, iz + dz, seed)
    return v


def edge_window(x, y, n=1, m=1):
    """0 on tile edges, 1 in the middle: lets interior-only variation keep seams identical"""
    u = (x % T) / T
    v = (y % T) / T
    return clamp(math.sin(math.pi * u) * 1.6) * clamp(math.sin(math.pi * v) * 1.6)


# light (metric): from the viewer's left/up -> +Y faces ("left" on screen) lit, +X faces dark
LIGHT = norm3((0.30, 0.66, 0.70))
AMBIENT = 0.34


# =========================================================================== solids
class Hit(object):
    __slots__ = ('t', 'n', 'tag', 'solid', 'P')

    def __init__(self, t, n, tag, solid):
        self.t, self.n, self.tag, self.solid = t, n, tag, solid
        self.P = None


class Solid(object):
    """base: aabb in canonical coords, material callback, params"""
    cls = 'obj'
    shadow = True
    curved = False
    _gid = [0]

    def __init__(self, mat, **par):
        self.mat = mat
        self.par = par
        Solid._gid[0] += 1
        self.grp = par.pop('grp', Solid._gid[0])   # outlines are drawn between groups only

    def slab(self, O, D):
        lo, hi = -BIG, BIG
        b = self.aabb
        for k in range(3):
            o, d = O[k], D[k]
            if abs(d) < 1e-12:
                if o < b[k] or o > b[k + 3]:
                    return None
            else:
                t1 = (b[k] - o) / d
                t2 = (b[k + 3] - o) / d
                if t1 > t2:
                    t1, t2 = t2, t1
                if t1 > lo:
                    lo = t1
                if t2 < hi:
                    hi = t2
                if lo > hi:
                    return None
        return lo, hi


class Convex(Solid):
    """intersection of half-spaces  n . P <= d   (P in X, Y, Zpx)"""

    def __init__(self, planes, aabb, mat, **par):
        Solid.__init__(self, mat, **par)
        self.planes = planes          # (nx, ny, nz, d, tag)
        self.aabb = aabb

    def hit(self, O, D, tmin=1e-6):
        lo, hi = -BIG, BIG
        lt = None
        for (nx, ny, nz, d, tag) in self.planes:
            nd = nx * D[0] + ny * D[1] + nz * D[2]
            no = nx * O[0] + ny * O[1] + nz * O[2]
            if abs(nd) < 1e-12:
                if no > d:
                    return None
                continue
            t = (d - no) / nd
            if nd < 0:            # entering
                if t > lo:
                    lo, lt = t, (nx, ny, nz, tag)
            else:
                if t < hi:
                    hi = t
            if lo > hi:
                return None
        if lt is None or lo < tmin:
            return None
        return Hit(lo, (lt[0], lt[1], lt[2]), lt[3], self)

    def inside(self, P):
        for (nx, ny, nz, d, tag) in self.planes:
            if nx * P[0] + ny * P[1] + nz * P[2] > d:
                return False
        return True


def box(x0, y0, z0, x1, y1, z1, mat, **par):
    pl = [(-1, 0, 0, -x0, 'x-'), (1, 0, 0, x1, 'x+'), (0, -1, 0, -y0, 'y-'), (0, 1, 0, y1, 'y+'),
          (0, 0, -1, -z0, 'bot'), (0, 0, 1, z1, 'top')]
    return Convex(pl, (x0, y0, z0, x1, y1, z1), mat, **par)


def _sw(axis, a, b):
    return (a, b) if axis == 'x' else (b, a)


def gable_body(x0, y0, x1, y1, z0, ze, zr, axis, mat, **par):
    """box with a gable top; ridge along `axis` ('x' or 'y') at the middle"""
    if axis == 'x':
        mid, half = (y0 + y1) / 2.0, (y1 - y0) / 2.0
    else:
        mid, half = (x0 + x1) / 2.0, (x1 - x0) / 2.0
    s = (zr - ze) / half
    pl = [(-1, 0, 0, -x0, 'x-'), (1, 0, 0, x1, 'x+'), (0, -1, 0, -y0, 'y-'), (0, 1, 0, y1, 'y+'),
          (0, 0, -1, -z0, 'bot')]
    a1 = _sw(axis, 0, s)
    a2 = _sw(axis, 0, -s)
    pl.append((a1[0], a1[1], 1, zr + s * mid, 'rf+'))
    pl.append((a2[0], a2[1], 1, zr - s * mid, 'rf-'))
    return Convex(pl, (x0, y0, z0, x1, y1, zr), mat, **par)


def roof_slab(a0, a1, mid, edge, zr, slope, th, axis, mat, side_tag='roof', **par):
    """one plane of a gable roof as a thick slab.  a0..a1 along the ridge, the slab runs from the
    ridge line (b=mid, height zr at its underside) to b=edge (edge>mid: +side, else -side)."""
    sg = 1 if edge > mid else -1
    # underside: Z >= zr - slope*sg*(b-mid) ; top: Z <= zr + th - slope*sg*(b-mid)
    na = _sw(axis, 1, 0)
    nb = _sw(axis, 0, 1)
    pl = [(-na[0], -na[1], 0, -a0, 'end-'), (na[0], na[1], 0, a1, 'end+')]
    if sg > 0:
        pl.append((-nb[0], -nb[1], 0, -mid, 'ridge'))
        pl.append((nb[0], nb[1], 0, edge, 'fascia'))
    else:
        pl.append((nb[0], nb[1], 0, mid, 'ridge'))
        pl.append((-nb[0], -nb[1], 0, -edge, 'fascia'))
    k = slope * sg
    t1 = _sw(axis, 0, k)
    # top:  k*b + Z <= zr + th + k*mid
    pl.append((t1[0], t1[1], 1, zr + th + k * mid, side_tag))
    # under: -(k*b + Z) <= -(zr + k*mid)
    pl.append((-t1[0], -t1[1], -1, -(zr + k * mid), 'under'))
    lo_b, hi_b = min(mid, edge), max(mid, edge)
    zlow = zr - slope * abs(edge - mid)
    x0, y0 = _sw(axis, a0, lo_b)
    x1, y1 = _sw(axis, a1, hi_b)
    par.setdefault('ridge_axis', axis)
    par.setdefault('mid', mid)
    par.setdefault('sg', sg)
    return Convex(pl, (x0, y0, zlow - 1, x1, y1, zr + th + 1), mat, **par)


def gable_roof(x0, y0, x1, y1, ze, zr, axis, mat, ov=2.0, ovg=2.0, th=1.6, **par):
    """two roof slabs over a body x0..x1/y0..y1 with eaves at ze and ridge zr"""
    if axis == 'x':
        mid, half, a0, a1 = (y0 + y1) / 2.0, (y1 - y0) / 2.0, x0 - ovg, x1 + ovg
        b0, b1 = y0 - ov, y1 + ov
    else:
        mid, half, a0, a1 = (x0 + x1) / 2.0, (x1 - x0) / 2.0, y0 - ovg, y1 + ovg
        b0, b1 = x0 - ov, x1 + ov
    s = (zr - ze) / half
    zr2 = zr + 0.2
    return [roof_slab(a0, a1, mid, b1, zr2, s, th, axis, mat, **par),
            roof_slab(a0, a1, mid, b0, zr2, s, th, axis, mat, **par)]


class Cone(Solid):
    curved = True
    """vertical truncated cone / cylinder: centre cx,cy, radius r0 at z0 -> r1 at z1 (units)"""

    def __init__(self, cx, cy, z0, z1, r0, r1, mat, cap=True, **par):
        Solid.__init__(self, mat, **par)
        self.c = (cx, cy, z0, z1, r0, r1)
        self.cap = cap
        r = max(r0, r1)
        self.aabb = (cx - r, cy - r, z0, cx + r, cy + r, z1)

    def hit(self, O, D, tmin=1e-6):
        cx, cy, z0, z1, r0, r1 = self.c
        k = (r1 - r0) / (z1 - z0)
        ox, oy = O[0] - cx, O[1] - cy
        # r(z) = r0 + k*(z - z0), z = O2 + t*D2
        rc = r0 + k * (O[2] - z0)
        rd = k * D[2]
        A = D[0] * D[0] + D[1] * D[1] - rd * rd
        B = 2 * (ox * D[0] + oy * D[1] - rc * rd)
        C = ox * ox + oy * oy - rc * rc
        best, bn, btag = None, None, None
        if abs(A) > 1e-12:
            disc = B * B - 4 * A * C
            if disc >= 0:
                sq = math.sqrt(disc)
                for t in sorted(((-B - sq) / (2 * A), (-B + sq) / (2 * A))):
                    if t < tmin:
                        continue
                    z = O[2] + t * D[2]
                    if z0 <= z <= z1 and rc + rd * t >= 0:
                        px, py = ox + t * D[0], oy + t * D[1]
                        r = r0 + k * (z - z0)
                        best, bn, btag = t, (px, py, -r * k), 'side'
                        break
        if self.cap and abs(D[2]) > 1e-12:
            for zc, rr, s in ((z1, r1, 1), (z0, r0, -1)):
                t = (zc - O[2]) / D[2]
                if t >= tmin and (best is None or t < best):
                    px, py = ox + t * D[0], oy + t * D[1]
                    if px * px + py * py <= rr * rr:
                        best, bn, btag = t, (0, 0, s), ('cap' if s > 0 else 'capb')
        if best is None:
            return None
        return Hit(best, bn, btag, self)


class Ellipsoid(Solid):
    curved = True
    def __init__(self, cx, cy, cz, rx, ry, rz, mat, **par):
        Solid.__init__(self, mat, **par)
        self.c = (cx, cy, cz, rx, ry, rz)
        self.aabb = (cx - rx, cy - ry, cz - rz, cx + rx, cy + ry, cz + rz)

    def hit(self, O, D, tmin=1e-6):
        cx, cy, cz, rx, ry, rz = self.c
        ox, oy, oz = (O[0] - cx) / rx, (O[1] - cy) / ry, (O[2] - cz) / rz
        dx, dy, dz = D[0] / rx, D[1] / ry, D[2] / rz
        A = dx * dx + dy * dy + dz * dz
        B = 2 * (ox * dx + oy * dy + oz * dz)
        C = ox * ox + oy * oy + oz * oz - 1
        disc = B * B - 4 * A * C
        if disc < 0:
            return None
        sq = math.sqrt(disc)
        t = (-B - sq) / (2 * A)
        if t < tmin:
            t = (-B + sq) / (2 * A)
            if t < tmin:
                return None
        px, py, pz = ox + t * dx, oy + t * dy, oz + t * dz
        return Hit(t, (px / rx, py / ry, pz / rz), 'surf', self)


class HeightField(Solid):
    curved = True
    """Z <= h(X, Y): marched along the ray (rock outcrops, mounds)"""
    shadow = True

    def __init__(self, x0, y0, x1, y1, hmax, hfun, mat, **par):
        Solid.__init__(self, mat, **par)
        self.h = hfun
        self.aabb = (x0, y0, 0.0, x1, y1, hmax)

    def hit(self, O, D, tmin=1e-6):
        r = self.slab(O, D)
        if r is None:
            return None
        lo, hi = max(r[0], tmin), r[1]
        if lo > hi:
            return None
        step = 0.35 / max(abs(D[0]), abs(D[1]), abs(D[2]))
        t = lo
        prev = None
        h = self.h
        while t <= hi:
            x, y, z = O[0] + t * D[0], O[1] + t * D[1], O[2] + t * D[2]
            if z <= h(x, y) and z > 0.05:
                # refine
                a, b = t - step, t
                for _ in range(6):
                    mm = (a + b) / 2
                    x, y, z = O[0] + mm * D[0], O[1] + mm * D[1], O[2] + mm * D[2]
                    if z <= h(x, y):
                        b = mm
                    else:
                        a = mm
                t = b
                x, y = O[0] + t * D[0], O[1] + t * D[1]
                e = 0.7
                gx = (h(x + e, y) - h(x - e, y)) / (2 * e)
                gy = (h(x, y + e) - h(x, y - e)) / (2 * e)
                return Hit(t, (-gx, -gy, 1.0), 'surf', self)
            t += step
        return None


# =========================================================================== model + G-buffer
class Model(object):
    """n x m tiles (canonical), list of solids, ground material, extra 2-D overlay lines"""

    def __init__(self, name, n=1, m=1, ground=None, ground_par=None, top=None):
        self.name = name
        self.n, self.m = n, m
        self.solids = []
        self.ground = ground            # function(g, style) -> (key, delta) ; None = transparent
        self.gpar = ground_par or {}
        self.lines = []                 # (P0, P1, colour key, sag) 3-D wires drawn after
        self.top = top

    def add(self, *ss):
        for s in ss:
            if isinstance(s, (list, tuple)):
                self.add(*s)
            else:
                self.solids.append(s)
        return self


class GPix(object):
    __slots__ = ('kind', 'solid', 'tag', 'P', 'W', 'n', 'light', 'shadow', 'depth', 'cls', 'ao')


def _rot_fwd(rot, x, y):
    for _ in range(rot % 4):
        x, y = y, -x
    return x, y


def _rot_inv(rot, x, y):
    for _ in range(rot % 4):
        x, y = -y, x
    return x, y


def render_gbuffer(model, zoom=1.0, rot=0):
    """ray-cast the model; returns (w, h, anchor, grid[y][x] of GPix or None)"""
    cn, cm = model.n, model.m
    if rot % 2:
        wn, wm = cm, cn
    else:
        wn, wm = cn, cm
    Wc = (wn * T / 2.0, wm * T / 2.0)      # world lot centre
    Cc = (cn * T / 2.0, cm * T / 2.0)      # canonical lot centre

    def to_world(P):
        x, y = _rot_fwd(rot, P[0] - Cc[0], P[1] - Cc[1])
        return (x + Wc[0], y + Wc[1], P[2])

    def to_canon(P):
        x, y = _rot_inv(rot, P[0] - Wc[0], P[1] - Wc[1])
        return (x + Cc[0], y + Cc[1], P[2])

    def dir_canon(D):
        x, y = _rot_inv(rot, D[0], D[1])
        return (x, y, D[2])

    # screen bbox of everything
    top = 0.0
    sbb = []
    for s in model.solids:
        b = s.aabb
        mnx, mny, mxx, mxy = 1e9, 1e9, -1e9, -1e9
        for x in (b[0], b[3]):
            for y in (b[1], b[4]):
                for z in (b[2], b[5]):
                    w = to_world((x, y, z))
                    sx = (w[0] - w[1])
                    sy = (w[0] + w[1]) / 2.0 - w[2]
                    mnx, mxx = min(mnx, sx), max(mxx, sx)
                    mny, mxy = min(mny, sy), max(mxy, sy)
        sbb.append((mnx, mny, mxx, mxy))
        top = max(top, -mny)
    for (P0, P1, key, sag) in model.lines:
        for P in (P0, P1):
            w = to_world(P)
            top = max(top, -((w[0] + w[1]) / 2.0 - w[2]) + 2)
    if model.top is not None:
        top = model.top
    top = int(math.ceil(top * zoom)) + 1 if top > 0 else 0
    W = int(round((wn + wm) * T * zoom))
    H = int(round((wn + wm) * T / 2 * zoom)) + top
    ox = wm * T * zoom
    oy = top
    Lc = norm3(dir_canon(LIGHT))
    Lpx = (Lc[0], Lc[1], Lc[2] * KZ)       # direction in px space
    Dw = (-1.0, -1.0, -1.0)
    Dc = dir_canon(Dw)
    grid = [[None] * W for _ in range(H)]
    sol = model.solids
    nsol = len(sol)
    for py in range(H):
        sy = (py + 0.5 - oy) / zoom
        for px in range(W):
            sx = (px + 0.5 - ox) / zoom
            X0 = sy + sx / 2.0
            Y0 = sy - sx / 2.0
            Tt = 300.0
            Ow = (X0 + Tt, Y0 + Tt, Tt)
            Oc = to_canon(Ow)
            best = None
            for i in range(nsol):
                b = sbb[i]
                if sx < b[0] - 1 or sx > b[2] + 1 or sy < b[1] - 1 or sy > b[3] + 1:
                    continue
                h = sol[i].hit(Oc, Dc)
                if h is not None and (best is None or h.t < best.t):
                    best = h
            g = None
            ingrd = (0 <= X0 < wn * T) and (0 <= Y0 < wm * T)
            if best is not None and best.t < Tt - 1e-4:
                t = best.t
                P = (Oc[0] + t * Dc[0], Oc[1] + t * Dc[1], Oc[2] + t * Dc[2])
                if P[2] < -0.01 and not ingrd:
                    best = None
                else:
                    g = GPix()
                    g.kind = 'solid'
                    g.solid = best.solid
                    g.tag = best.tag
                    g.P = P
                    g.W = to_world(P)
                    n = best.n
                    nm = norm3((n[0], n[1], n[2] * KZ))
                    g.n = nm
                    g.cls = best.solid.cls
                    g.depth = -t
            if g is None and ingrd and model.ground is not None:
                g = GPix()
                g.kind = 'ground'
                g.solid = None
                g.tag = 'ground'
                g.P = to_canon((X0, Y0, 0.0))
                g.W = (X0, Y0, 0.0)
                g.n = (0.0, 0.0, 1.0)
                g.cls = 'ground'
                g.depth = -Tt
            if g is None:
                continue
            # light + shadow
            ndl = g.n[0] * Lc[0] + g.n[1] * Lc[1] + g.n[2] * Lc[2]
            sh = 0.0
            if ndl > 0:
                P = g.P
                Ps = (P[0] + g.n[0] * 0.05 + Lpx[0] * 0.02, P[1] + g.n[1] * 0.05 + Lpx[1] * 0.02,
                      P[2] + 0.05 + Lpx[2] * 0.02)
                for i in range(nsol):
                    s = sol[i]
                    if not s.shadow or s is g.solid and not isinstance(s, (HeightField, Ellipsoid)):
                        continue
                    if s.slab(Ps, Lpx) is None:
                        continue
                    if s.hit(Ps, Lpx, 0.15) is not None:
                        sh = 1.0
                        break
            g.shadow = sh
            g.light = AMBIENT + (1 - AMBIENT) * max(0.0, ndl) * (1 - 0.82 * sh)
            g.ao = 0.0
            grid[py][px] = g
    # contact darkening: ground pixels directly next to solids
    for py in range(H):
        row = grid[py]
        for px in range(W):
            g = row[px]
            if g is None or g.kind != 'ground':
                continue
            P = g.P
            near = 0.0
            for (dx, dy, w) in ((1.2, 0, 1), (-1.2, 0, 1), (0, 1.2, 1), (0, -1.2, 1),
                                (2.4, 0, .5), (-2.4, 0, .5), (0, 2.4, .5), (0, -2.4, .5)):
                Q = (P[0] + dx, P[1] + dy, 0.6)
                for s in sol:
                    b = s.aabb
                    if not (b[0] <= Q[0] <= b[3] and b[1] <= Q[1] <= b[4] and b[2] <= 0.6):
                        continue
                    if isinstance(s, Convex) and s.inside(Q):
                        near = max(near, w)
                        break
            g.ao = near
    anchor = (int(round(ox + (wn - wm) * T * zoom)), H)   # south vertex of footprint
    return dict(w=W, h=H, grid=grid, anchor=anchor, ox=ox, oy=oy, zoom=zoom, model=model,
                rot=rot, wn=wn, wm=wm, to_world=to_world)


# =========================================================================== styles
BAYER4 = [[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9], [15, 7, 13, 5]]

# base colours = the colour of a face in full light (l ~ 0.8).  Shared by all styles unless a
# style overrides a key.  Hex for STYLE.md.
PAL_BASE = {
    # terrain: muted moss / olive greens that recede behind the buildings
    'grass': '#77795a', 'grass_dk': '#626649', 'grass_lt': '#8a8a68', 'meadow': '#86845f',
    'dirt': '#7d6d59', 'sand': '#b2a688', 'pebble': '#969188',
    'water': '#4b5f6b', 'water_dk': '#3f505b', 'water_lt': '#7a8b91', 'shallow': '#62726f',
    'rock': '#8c817c', 'rock_dk': '#6a625f', 'lichen': '#9a9a80',
    'crop': '#b09f6c', 'crop_dk': '#958659', 'plough': '#6c5d4d', 'plough_dk': '#584c40',
    # roads
    'asphalt': '#5b5c5c', 'asph_lt': '#6b6b6a', 'asph_dk': '#4c4d4d', 'roadline': '#c4bea0',
    'gravel': '#9d9283', 'gravel_lt': '#aea495', 'gravel_dk': '#83796c', 'kerb': '#a29f97',
    # wooden houses: weathered single colours (keltamulta, punamulta, faded blue, pale green, ...)
    'w_yellow': '#c4a15e', 'w_red': '#88483a', 'w_blue': '#8c9ea6', 'w_green': '#9fae96',
    'w_grey': '#aaa9a1', 'w_cream': '#d0c5a3', 'w_pink': '#c2a294',
    'trim': '#dfdacd', 'glass': '#3d454d', 'glass_lt': '#74808a', 'door': '#685444',
    'door_green': '#56624f', 'plinth': '#868379',
    'roof_metal': '#767b7c', 'roof_dark': '#55585a', 'roof_red': '#7a4c41',
    'brick': '#8f5643', 'brick_dk': '#704536', 'cap': '#3b3836', 'chim_white': '#cfcbc0',
    # concrete era: slightly warm greys
    'render_white': '#d1cdc1', 'render_yel': '#cdbf9b', 'render_grey': '#a5a299',
    'panel': '#b5b0a4', 'panel_dk': '#8a867c', 'bitumen': '#4e4c4a', 'parapet': '#99968d',
    'balc_orange': '#a8714f', 'balc_blue': '#6b8091', 'balc_green': '#7b8a64', 'balc_white': '#ccc8bc',
    'sign_green': '#456650', 'sign_red': '#954739', 'sign_txt': '#e8e2d2', 'awning': '#9a5140',
    'factory_glass': '#59646c', 'steel': '#6b6f71',
    # nature
    'birch_leaf': '#8c955e', 'birch_leaf_au': '#bb9d52', 'spruce': '#3c5146', 'pine_leaf': '#56654a',
    'apple_leaf': '#65744c', 'apple': '#a64a3c', 'bark_birch': '#dad6cc', 'bark_dk': '#2f2d2b',
    'bark_pine': '#93684d', 'bark': '#5f5241', 'bush': '#5b6849', 'lilac': '#8b84a0',
    'fence': '#d8d4c8', 'fence_wood': '#80705c', 'pole': '#685847', 'wire': '#b9b6aa',
    'coal': '#2f2e2f', 'woodpile': '#8a765d', 'flowers': '#bba955',
    # cars (Lada 1200 style): allowed to be more colourful than the terrain so they read
    'car_white': '#e2ded3', 'car_beige': '#cbb896', 'car_yellow': '#dcc26a', 'car_orange': '#cf7a38',
    'car_lblue': '#8fb0c6', 'car_dblue': '#37507a', 'car_red': '#b03a2e', 'car_green': '#4f7a4c',
    'car_brown': '#78533c', 'tyre': '#262626', 'chrome': '#c9cccc', 'headlight': '#f2eed4',
    'taillight': '#b0281e',
}


class Style(object):
    def __init__(self, name, desc, levels=7, dither=False, outline='none', texture=1.0,
                 hue=0.25, shadow_tint='#2a3050', light_tint='#fff4dc', over=None,
                 outline_col='#1d1c24', sat=1.0, contrast=1.0, rim=False, boards=1.0, gscale=1.0,
                 hi=0.32):
        self.name, self.desc = name, desc
        self.levels, self.dither, self.outline = levels, dither, outline
        self.texture, self.hue = texture, hue
        self.shadow_tint, self.light_tint = hexc(shadow_tint), hexc(light_tint)
        self.outline_col = hexc(outline_col)
        self.sat, self.contrast, self.rim, self.boards = sat, contrast, rim, boards
        self.tex_obj, self.tex_ground, self.hi = texture, texture * gscale, hi
        self.pal = {}
        for k, v in PAL_BASE.items():
            self.pal[k] = hexc(v)
        for k, v in (over or {}).items():
            self.pal[k] = hexc(v)
        if sat != 1.0:
            for k, c in list(self.pal.items()):
                g = 0.3 * c[0] + 0.59 * c[1] + 0.11 * c[2]
                self.pal[k] = tuple(clamp(g + (x - g) * sat, 0, 255) for x in c)
        self.cache = {}
        self.zoom = 1.0

    def cont(self, base, l):
        """continuous shade of a base colour at light l (0.34 .. 1.0)"""
        lref = 0.80
        l = 0.5 + (l - 0.5) * self.contrast
        if l <= lref:
            t = clamp((l - 0.22) / (lref - 0.22))
            dark = tuple(base[i] * 0.40 * (1 - self.hue) + self.shadow_tint[i] * self.hue for i in range(3))
            return lerpc(dark, base, t)
        t = clamp((l - lref) / (1.0 - lref))
        return lerpc(base, self.light_tint, t * self.hi)

    def ramp(self, key):
        r = self.cache.get(key)
        if r is None:
            base = self.pal[key]
            n = self.levels
            r = [to565(self.cont(base, 0.30 + 0.70 * i / (n - 1.0))) for i in range(n)]
            self.cache[key] = r
        return r

    def color(self, key, l, delta, x, y, snap=True, grad=0.0):
        n = self.levels
        pos = (l - 0.30) / 0.70 * (n - 1)
        if snap:            # flat faces: one tone per face, dither only shows on texture/gradients
            pos = math.floor(pos + 0.5)
        pos += delta * (n - 1) / 6.0 + grad * (n - 1)
        if self.dither:
            pos += (BAYER4[y & 3][x & 3] + 0.5) / 16.0 - 0.5
            i = int(math.floor(pos + 0.5))
        else:
            i = int(math.floor(pos + 0.5))
        i = 0 if i < 0 else n - 1 if i > n - 1 else i
        return self.ramp(key)[i]


STYLES = {
    'A': Style('A', 'Clean flat pixel art: few tones per material, 1 px near-black outline on every '
               'raised object, almost no texture noise.', levels=6, dither=False, outline='dark',
               texture=0.35, gscale=0.8, hue=0.18, boards=0.0, contrast=1.05, outline_col='#24232a'),
    'B': Style('B', 'Soft textured, late-90s isometric: many tones, ordered 4x4 dither, '
               'gradients and texture on every surface, no outlines.', levels=12, dither=True,
               outline='none', texture=1.0, gscale=1.0, hue=0.22, boards=1.0, contrast=0.95),
    'C': Style('C', 'Nordic hybrid: buildings detailed (boards, seams, brick) with a coloured '
               'sel-out outline (darkest tone of the local material), terrain kept flat and quiet '
               'so it recedes, cool hue-shifted shadows and warm light.', levels=8, dither=False,
               outline='sel', texture=0.8, gscale=0.45, hue=0.40, boards=0.7, sat=0.9,
               shadow_tint='#26304e', light_tint='#fbefd2', contrast=1.1, outline_col='#1e2230', hi=0.28),
}


# =========================================================================== materials
def m_flat(key, delta=0.0):
    def f(g, st):
        return key, delta
    return f


def local_st(g):
    """local wall coords: s along the wall, t = height"""
    P = g.P
    tg = g.tag
    if tg in ('x+', 'x-'):
        return P[1], P[2]
    return P[0], P[2]


def tex_amt(st):
    return st.texture


def in_rect(s, t, r):
    return r[0] <= s < r[1] and r[2] <= t < r[3]


def window_px(s, t, r, st, kind='win'):
    """returns (key, delta) for a point inside window rect r=(s0,s1,t0,t1), or None"""
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
    # reflection: upper-left glass pixel lighter in textured styles
    if st.texture > 0.5 and s < s0 + fr + 1 and t >= t1 - 2:
        return 'glass_lt', -0.6
    return 'glass', 0.0


def m_house(g, st):
    """wooden rintamamiestalo wall: boards, corner boards, windows, plinth, doors"""
    p = g.solid.par
    tg = g.tag
    if tg in ('rf+', 'rf-', 'bot'):
        return p['wall'], 0.0
    s, t = local_st(g)
    b = g.solid.aabb
    lo, hi = (b[1], b[4]) if tg in ('x+', 'x-') else (b[0], b[3])
    if t < p.get('plinth', 2.5):
        return 'plinth', (-0.4 if st.texture > 0.5 and int(s * 0.5 + t) % 5 == 0 else 0.0)
    for r in p.get('doors', {}).get(tg, ()):
        if in_rect(s, t, r):
            if s < r[0] + 1 or s >= r[1] - 1 or t >= r[3] - 1:
                return 'trim', 0.0
            return p.get('door', 'door'), 0.0
    for r in p.get('wins', {}).get(tg, ()):
        w = window_px(s, t, r, st)
        if w:
            return w
    cb = 1.0 if st.zoom >= 1 else 2.0
    if p.get('corners', True) and (s < lo + cb or s >= hi - cb):
        return 'trim', 0.0
    # eave frieze board right below the roof on eave walls
    ze = p.get('eave', 99)
    if p.get('frieze', True) and ze - 1.0 <= t < ze and st.zoom >= 1:
        return 'trim', -0.3
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
    sp = p.get('seam', 3.0) if st.zoom >= 1 else p.get('seam', 3.0) * 2
    if p.get('seams', True) and (a % sp) < (1.0 if st.zoom >= 1 else 2.0):
        return key, 0.6 if st.texture > 0.2 else 0.45
    if st.texture > 0.8 and ihash(int(a), int(bb * 0.5), 7) < 0.05:
        return key, -0.3
    return key, 0.0


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
    """masonry / concrete block walls with window grid, panel joints, doors"""
    p = g.solid.par
    tg = g.tag
    wall = p.get('wall', 'render_white')
    if tg == 'top':
        b = g.solid.aabb
        P = g.P
        e = p.get('parapet', 1.2)
        if P[0] < b[0] + e or P[0] > b[3] - e or P[1] < b[1] + e or P[1] > b[4] - e:
            return 'parapet', 0.3
        d = 0.0
        if st.texture > 0.5 and ihash(int(P[0] / 2), int(P[1] / 2), 3) < 0.15:
            d = -0.4
        return p.get('roofkey', 'bitumen'), d
    if tg in ('rf+', 'rf-'):
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
            return p.get('door', 'door'), 0.0
    for r in p.get('wins', {}).get(tg, ()):
        w = window_px(s, t, r, st, p.get('wkind', 'block'))
        if w:
            if w[0] == 'trim':
                return p.get('frame', 'trim'), w[1]
            return w
    for r in p.get('bands', {}).get(tg, ()):
        if in_rect(s, t, r[:4]):
            return r[4], 0.0
    if p.get('joints') and st.zoom >= 1:
        jh, jv = p['joints']
        z0 = p.get('plinth', 3.0)
        if (t - z0) % jh < 0.999 and t > z0 + 1:
            return wall, -0.7
        if (s - lo) % jv < 0.999:
            return wall, -0.7
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
    return p.get('key', 'balc_white'), 0.0


def m_glasswall(g, st):
    p = g.solid.par
    if g.tag in ('x+', 'y+', 'x-', 'y-'):
        s, t = local_st(g)
        if (s % 4) < 1.0 or (t % 4) < 1.0:
            return 'steel', 0.0
        return 'factory_glass', (0.6 if (s % 4) < 2 and (t % 4) > 2.5 else 0.0)
    return 'roof_dark', 0.0


def m_brickwall(g, st):
    p = g.solid.par
    tg = g.tag
    if tg == 'top':
        return p.get('roofkey', 'bitumen'), 0.0
    s, t = local_st(g)
    for r in p.get('wins', {}).get(tg, ()):
        if in_rect(s, t, r):
            if s < r[0] + 1 or s >= r[1] - 1 or t < r[2] + 1 or t >= r[3] - 1:
                return 'brick_dk', 0.0
            if (s - r[0]) % 3 < 1.0:
                return 'steel', 0.0
            return 'factory_glass', 0.0
    for r in p.get('doors', {}).get(tg, ()):
        if in_rect(s, t, r):
            if (t % 2) < 1.0 and st.texture > 0.3:
                return 'steel', -0.4
            return 'steel', 0.0
    if st.texture > 0.4 and st.zoom >= 1:
        row = int(math.floor(t / 1.0))
        if row % 2 == 0 and int(math.floor(s + (row // 2) % 2 * 1.5)) % 3 == 0:
            return 'brick', -0.5
    if t > g.solid.aabb[5] - 1.5:
        return 'brick_dk', 0.3
    return 'brick', 0.0


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
    """leaf clumps: each clump is a small ellipsoid; add jitter so it reads as foliage"""
    p = g.solid.par
    key = p['key']
    P = g.P
    d = 0.0
    if st.texture > 0.2:
        cell = 1.6 if st.zoom >= 1 else 3.2
        nz = ihash(int(math.floor(P[0] / cell)), int(math.floor(P[1] / cell)), int(math.floor(P[2] / cell)), p.get('seed', 1))
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
        # lower rim of each tier darker
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


# =========================================================================== ground materials
def g_grass(g, st, var=0, key='grass'):
    x, y = g.W[0], g.W[1]
    tx = st.texture
    n1 = vnoise(x, y, 8.0, 1)
    n2 = vnoise(x, y, 4.0, 2) - 0.5
    n3 = (vnoise(x, y, 5.0, 40 + var) - 0.5) * edge_window(x, y)
    d = (n1 - 0.5) * 1.3 * tx + n2 * 0.5 * tx + n3 * 1.2 * tx
    ix, iy = int(math.floor(x)) % 32, int(math.floor(y)) % 32
    h = ihash(ix, iy, 5 + (var if 2 < ix < 29 and 2 < iy < 29 else 0))
    if h < 0.05 * tx + 0.015:
        return 'grass_lt', 0.2
    if h > 1 - (0.05 * tx + 0.01):
        return 'grass_dk', -0.2
    return key, d


def g_field(g, st, kind='crop'):
    x, y = g.W[0], g.W[1]
    tx = st.texture
    key, dk = ('crop', 'crop_dk') if kind == 'crop' else ('plough', 'plough_dk')
    m = 1.0 if st.zoom >= 1 else 2.0
    yy = y % T
    # open ditch (sarkaoja) along the middle of every tile: long parallel ditches across a field
    if abs(yy - 16.0) < 0.8 * m:
        return 'water_dk', 0.0
    if abs(yy - 16.0) < 2.0 * m:
        return 'grass_dk', (vnoise(x, y, 2.0, 32) - 0.5) * 0.8 * tx
    if (y % (3.0 * m)) < 1.0 * m:
        return dk, 0.0
    d = (vnoise(x, y, 4.0, 31) - 0.5) * 0.9 * tx
    # 1 unit field-edge strip of grass so fields read as parcels
    if kind != 'crop' and ihash(int(x) % 32, int(y) % 32, 33) < 0.04 * tx:
        return dk, -0.3
    return key, d


def wave_d(x, y, st):
    """ripple highlight: short screen-horizontal dashes, periodic over the tile lattice"""
    if st.texture < 0.1:
        return 0.0
    u = (x + y)             # constant along screen-horizontal lines
    v = (x - y)
    band = (u + 3.0 * vnoise(x, y, 8.0, 51)) % 8.0
    if band < 1.0 and vnoise(x, y, 4.0, 52) > 0.62:
        return 1.0
    return 0.0


def g_water(g, st):
    x, y = g.W[0], g.W[1]
    d = (vnoise(x, y, 8.0, 50) - 0.5) * 0.6 * st.texture
    if wave_d(x, y, st):
        return 'water_lt', 0.0
    return 'water', d


def shore_field(x, y, corners):
    """corners = (n, e, w, s) land flags (n=(0,0), e=(32,0), w=(0,32), s=(32,32))"""
    u, v = (x % T) / T, (y % T) / T
    if x >= T:
        u = 1.0
    if y >= T:
        v = 1.0
    cn, ce, cw, cs = corners
    f = (cn * (1 - u) * (1 - v) + ce * u * (1 - v) + cw * (1 - u) * v + cs * u * v)
    f += (vnoise(x, y, 8.0, 61) - 0.5) * 0.26 + (vnoise(x, y, 4.0, 62) - 0.5) * 0.10
    return f


def g_shore(g, st, corners=(1, 0, 0, 0)):
    x, y = g.W[0], g.W[1]
    f = shore_field(x, y, corners)
    if f > 0.5:
        if f < 0.56:
            if st.texture > 0.3 and ihash(int(x * 1.3), int(y * 1.3), 63) < 0.25:
                return 'pebble', 0.0
            return 'sand', (vnoise(x, y, 3.0, 64) - 0.5) * 0.6 * st.texture
        if f < 0.60:
            return 'grass_dk', 0.0
        return g_grass(g, st)
    if f > 0.44:
        return 'shallow', 0.3
    if f > 0.40 and st.texture > 0.2:
        return 'water_lt', -0.2
    return g_water(g, st)


def road_dist(x, y, conn, kind):
    """distance (units) from the road centre line; conn = set of '-x','+x','-y','+y'"""
    c = 16.0
    if len(conn) == 2 and not (('-x' in conn and '+x' in conn) or ('-y' in conn and '+y' in conn)):
        # curve: circular arc around the tile corner shared by both edges
        cx = 0.0 if '-x' in conn else 32.0
        cy = 0.0 if '-y' in conn else 32.0
        return abs(math.hypot(x - cx, y - cy) - 16.0)
    ds = []
    for e in conn:
        if e in ('-x', '+x'):
            if (e == '-x' and x <= c) or (e == '+x' and x >= c):
                ds.append(abs(y - c))
            else:
                ds.append(math.hypot(x - c, y - c))
        else:
            if (e == '-y' and y <= c) or (e == '+y' and y >= c):
                ds.append(abs(x - c))
            else:
                ds.append(math.hypot(x - c, y - c))
    if not ds:
        return 99.0
    ds.sort()
    if len(ds) > 1:
        a, b = ds[0], ds[1]
        k = 3.0       # smooth union -> filleted corners at T / crossings
        h = clamp(0.5 + 0.5 * (b - a) / k)
        return lerp(b, a, h) - k * h * (1 - h)
    return ds[0]


def g_road(g, st, conn=('-x', '+x'), kind='asphalt'):
    x, y = g.W[0] % T if g.W[0] < T else T - 1e-6, g.W[1] % T if g.W[1] < T else T - 1e-6
    x, y = g.W[0], g.W[1]
    d = road_dist(x, y, conn, kind)
    tx = st.texture
    if kind == 'asphalt':
        hw = 7.0
        if d < hw:
            # centre line on straight pieces only
            straight = len(conn) == 2 and (('-x' in conn and '+x' in conn) or ('-y' in conn and '+y' in conn))
            if straight and d < 0.6 and st.zoom >= 1:
                along = x if '-x' in conn else y
                if (along % 8.0) < 4.0:
                    return 'roadline', 0.0
            nz = (vnoise(x, y, 3.0, 71) - 0.5) * 0.7 * tx
            if tx > 0.5 and vnoise(x, y, 4.0, 72) > 0.86:
                return 'asph_dk', 0.0
            if d > hw - 1.0:
                return 'asph_dk', 0.2
            return 'asphalt', nz
        if d < hw + 1.3:
            return 'gravel', (vnoise(x, y, 2.0, 73) - 0.5) * 0.8 * tx
        return g_grass(g, st)
    else:  # gravel road with wheel ruts and a grassy crown
        hw = 6.5
        if d < hw:
            if d < 0.9 and tx > 0.3:
                return 'gravel_dk', 0.3
            if 3.2 < d < 4.6:
                return 'gravel_dk', 0.0
            nz = (vnoise(x, y, 2.0, 74) - 0.5) * 0.9 * tx
            if tx > 0.3 and ihash(int(x * 1.4), int(y * 1.4), 75) < 0.08:
                return 'gravel_lt', 0.0
            return 'gravel', nz
        if d < hw + 0.8:
            return 'grass_dk', 0.0
        return g_grass(g, st)


def g_lot(g, st, paths=(), gravel=(), base='grass', var=0):
    """house lot: grass + gravel paths (canonical rects x0,y0,x1,y1)"""
    P = g.P
    for r in paths:
        if r[0] <= P[0] < r[2] and r[1] <= P[1] < r[3]:
            return 'gravel', (vnoise(g.W[0], g.W[1], 2.0, 76) - 0.5) * 0.8 * st.texture
    for r in gravel:
        if r[0] <= P[0] < r[2] and r[1] <= P[1] < r[3]:
            key = r[4] if len(r) > 4 else 'gravel'
            return key, (vnoise(g.W[0], g.W[1], 2.5, 77) - 0.5) * 0.7 * st.texture
    return g_grass(g, st, var)


# =========================================================================== tree builders
def new_grp():
    Solid._gid[0] += 1
    return Solid._gid[0]


def add_birch(md, x, y, h=30.0, seed=1, crown=1.0, autumn=False):
    """koivu: white trunk with black marks, airy drooping crown of small leaf clumps"""
    key = 'birch_leaf_au' if autumn else 'birch_leaf'
    gp = new_grp()
    md.add(Cone(x, y, 0, h * 0.86, 0.8, 0.45, m_trunk, cap=False, key='bark_birch', seed=seed, grp=gp))
    n = int(34 * crown)
    R = 4.8 * crown
    for i in range(n):
        a = ihash(i, seed, 1) * 2 * math.pi
        u = ihash(i, seed, 3)                   # 0 bottom .. 1 top of crown
        z = h * (0.30 + 0.70 * u)
        prof = math.sin(math.pi * (0.15 + 0.85 * u)) ** 0.8   # widest low-middle, narrow top
        r = (0.35 + 0.65 * math.sqrt(ihash(i, seed, 2))) * R * prof
        rr = 1.3 + ihash(i, seed, 4) * 0.8
        z -= (r / R) * 2.5                       # outer twigs droop
        md.add(Ellipsoid(x + math.cos(a) * r, y + math.sin(a) * r, z, rr, rr, rr * KZ * 1.15, m_leaf,
                         key=key, seed=seed + i, grp=gp))


def add_spruce(md, x, y, h=40.0, seed=2, r=6.5):
    """kuusi: dark narrow cone in 5 tiers"""
    gp = new_grp()
    md.add(Cone(x, y, 0, h * 0.3, 0.8, 0.6, m_trunk, cap=False, key='bark', grp=gp))
    tiers = 5
    for i in range(tiers):
        z0 = h * (0.10 + 0.16 * i)
        z1 = min(h, z0 + h * 0.42)
        rr = r * (1.0 - 0.16 * i) * (0.92 + 0.16 * ihash(i, seed, 5))
        md.add(Cone(x, y, z0, z1, rr, 0.2, m_spruce, cap=True, seed=seed + i, grp=gp))


def add_pine(md, x, y, h=42.0, seed=3):
    """manty: tall orange-brown trunk, flat umbrella crown"""
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
    """omenapuu: low round crown with red apples"""
    gp = new_grp()
    md.add(Cone(x, y, 0, h * 0.5, 0.8, 0.6, m_trunk, cap=False, key='bark', grp=gp))
    for i in range(14):
        a = ihash(i, seed, 1) * 2 * math.pi
        r = math.sqrt(ihash(i, seed, 2)) * 3.6
        z = h * 0.58 + ihash(i, seed, 3) * h * 0.36
        rr = 1.7 + ihash(i, seed, 4) * 1.0
        md.add(Ellipsoid(x + math.cos(a) * r, y + math.sin(a) * r, z, rr, rr, rr * KZ * 0.85,
                         m_leaf, key='apple_leaf', seed=seed + i, fruit='apple' if fruit else None, grp=gp))


def add_bush(md, x, y, seed=5, key='bush', size=1.0):
    gp = new_grp()
    for i in range(5):
        a = ihash(i, seed, 1) * 2 * math.pi
        r = ihash(i, seed, 2) * 1.8 * size
        rr = (1.5 + ihash(i, seed, 4) * 0.8) * size
        md.add(Ellipsoid(x + math.cos(a) * r, y + math.sin(a) * r, rr * KZ * 0.7 + ihash(i, seed, 3) * 1.2,
                         rr, rr, rr * KZ * 0.8, m_leaf, key=key, seed=seed + i, grp=gp))


def add_fence(md, x0, y0, x1, y1, gaps=(), key='fence', h=3.5, step=2.0):
    """picket fence along a straight line (axis aligned)"""
    L = math.hypot(x1 - x0, y1 - y0)
    n = int(L / step)
    dx, dy = (x1 - x0) / L, (y1 - y0) / L
    for i in range(n + 1):
        d = i * step
        if any(a <= d <= b for (a, b) in gaps):
            continue
        cx, cy = x0 + dx * d, y0 + dy * d
        b = box(cx - 0.45, cy - 0.45, 0, cx + 0.45, cy + 0.45, h, m_flat(key))
        b.cls = 'detail'
        b.shadow = False
        md.add(b)
    # rail
    segs = []
    pos = 0.0
    for (a, b) in sorted(gaps) + [(L, L)]:
        if a > pos:
            segs.append((pos, a))
        pos = b
    for (a, b) in segs:
        ax, ay = x0 + dx * a, y0 + dy * a
        bx, by = x0 + dx * b, y0 + dy * b
        r = box(min(ax, bx) - 0.3, min(ay, by) - 0.3, h - 1.3, max(ax, bx) + 0.3, max(ay, by) + 0.3, h - 0.7,
                m_flat(key, -0.5))
        r.cls = 'detail'
        r.shadow = False
        md.add(r)


# =========================================================================== building generators
def rintama(name, wall='w_yellow', ridge='x', storeys=1.5, chim=0.5, chimkey='brick', porch='gable',
            porch_at=0.22, dormer=False, roof='roof_metal', clad='h', tree='apple', tree_at=(3.5, 28.0),
            shed=True, fence='fence', bush=True, seed=1, door='door', autumn=False):
    """rintamamiestalo on a 1x1 lot; canonical front (street side) = +Y"""
    pl = 2.5
    if ridge == 'x':
        x0, x1, y0, y1 = 7.0, 25.0, 6.0, 22.0
    else:
        x0, x1, y0, y1 = 8.0, 24.0, 5.0, 22.0
    ze = pl + (9.5 if storeys < 1.75 else 19.0)
    half = ((y1 - y0) if ridge == 'x' else (x1 - x0)) / 2.0
    zr = ze + half * KZ                 # 45 degree roof
    mid = (y0 + y1) / 2.0 if ridge == 'x' else (x0 + x1) / 2.0
    wins = {'x+': [], 'y+': [], 'x-': [], 'y-': []}
    doors = {'x+': [], 'y+': []}
    wt0, wt1 = pl + 2.0, pl + 8.5
    rows = [(wt0, wt1)] + ([(ze - 8.0, ze - 1.5)] if storeys >= 1.75 else [])
    # porch geometry first (to keep windows out of it)
    pw, pd = 6.0, 4.5
    pcx = lerp(x0 + 4.5, x1 - 4.5, porch_at)
    px0, px1 = pcx - pw / 2, pcx + pw / 2
    md = Model(name)
    # front (+Y) wall
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
    # side (+X) wall
    for (t0, t1) in rows:
        if ridge == 'x':
            ys = [y0 + 2.5, y1 - 7.5]
        else:
            ys = [y0 + 2.5, (y0 + y1) / 2 - 2.5, y1 - 7.5]
        for sy in ys:
            wins['x+'].append((sy, sy + 5.0, t0, t1))
            wins['x-'].append((sy, sy + 5.0, t0, t1))
    # gable attic windows (both gable ends, so every rotation has one)
    for gface in (('x+', 'x-') if ridge == 'x' else ('y+', 'y-')):
        wins[gface].append((mid - 2.0, mid + 2.0, ze + 1.5, ze + 6.5))
    if porch == 'canopy':
        doors['y+'].append((pcx - 1.5, pcx + 1.5, pl, pl + 8.0))
    body = gable_body(x0, y0, x1, y1, 0, ze, zr, ridge, m_house, wall=wall, plinth=pl, wins=wins,
                      doors=doors, clad=clad, eave=ze, door=door)
    md.add(body)
    # roof (split around a wall dormer if any)
    rp = dict(key=roof, edgekey='trim', seam=3.0)
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
                          wins=dw, clad=clad, eave=dze, frieze=False))
        md.add(gable_roof(dx0, mid, dx1, y1, dze, dzr, 'y', m_roof, ov=1.2, ovg=1.0, th=1.4, **rp))
    else:
        md.add(gable_roof(x0, y0, x1, y1, ze, zr, ridge, m_roof, **rp))
    # chimney
    if ridge == 'x':
        cx = lerp(x0 + 3.5, x1 - 3.5, chim)
        cy = mid
    else:
        cx = mid
        cy = lerp(y0 + 3.5, y1 - 3.5, chim)
    md.add(box(cx - 1.4, cy - 1.4, ze, cx + 1.4, cy + 1.4, zr + 5.5, m_chimney, key=chimkey))
    # porch / entrance
    door_x = pcx
    front_y = y1
    if porch == 'gable':
        pze, pzr = pl + 8.0, pl + 8.0 + 3.0 * KZ
        pw_ = {'x+': [(y1 + 1.2, y1 + 3.2, pl + 3.0, pl + 6.5)]}
        pdoor = {'y+': [(pcx - 1.5, pcx + 1.5, pl - 0.5, pl + 7.0)]}
        md.add(gable_body(px0, y1 - 1, px1, y1 + pd, 0, pze, pzr, 'y', m_house, wall=wall, plinth=pl - 0.5,
                          wins=pw_, doors=pdoor, clad='v', eave=pze, frieze=False, door=door))
        md.add(gable_roof(px0, y1 - 1, px1, y1 + pd, pze, pzr, 'y', m_roof, ov=0.9, ovg=0.8, th=1.3, **rp))
        front_y = y1 + pd
    elif porch == 'canopy':
        c = box(pcx - 2.5, y1, pl + 8.5, pcx + 2.5, y1 + 2.5, pl + 9.5, m_flat('trim'))
        md.add(c)
    # steps
    md.add(box(door_x - 2.0, front_y, 0, door_x + 2.0, front_y + 1.6, 1.2, m_flat('plinth', 0.3)))
    md.add(box(door_x - 2.0, front_y, 0, door_x + 2.0, front_y + 0.9, 2.2, m_flat('plinth', 0.3)))
    # yard: outbuilding (red ochre sauna / woodshed) in the back corner
    if shed:
        sx0, sx1, sy0, sy1 = 26.0, 31.0, 13.0, 19.5
        sze = 7.0
        szr = sze + 2.6 * KZ * 0.8
        md.add(gable_body(sx0, sy0, sx1, sy1, 0, sze, szr, 'y', m_house, wall='w_red', plinth=0.8,
                          wins={'x+': [(sy0 + 2.0, sy0 + 4.5, 2.5, 5.5)]}, clad='v', eave=sze, frieze=False,
                          corners=True))
        md.add(gable_roof(sx0, sy0, sx1, sy1, sze, szr, 'y', m_roof, ov=0.8, ovg=0.8, th=1.2, key='roof_dark',
                          edgekey='trim', seam=3.0))
    # trees and bushes
    if tree == 'apple':
        add_apple(md, tree_at[0], tree_at[1], seed=seed * 7 + 1)
    elif tree == 'birch':
        add_birch(md, tree_at[0], tree_at[1], h=34, seed=seed * 7 + 2, autumn=autumn)
    elif tree == 'spruce':
        add_spruce(md, tree_at[0], tree_at[1], h=30, seed=seed * 7 + 3, r=5.0)
    if bush:
        bx, by = (28.5, 26.5) if tree_at[0] < 16 else (3.5, 27.5)
        add_bush(md, bx, by, seed=seed * 5 + 3, key='lilac' if seed % 2 else 'bush')
    # fence along the street edges with a gate at the path
    if fence:
        gx = door_x
        add_fence(md, 0.8, 31.2, 31.2, 31.2, gaps=((gx - 2.6 - 0.8, gx + 2.6 - 0.8),), key=fence)
        add_fence(md, 31.2, 0.8, 31.2, 30.0, key=fence)
    path = [(door_x - 1.6, front_y, door_x + 1.6, 32.0)]
    md.ground = lambda g, st: g_lot(g, st, paths=path, var=seed)
    return md


def lamelli(name, kind='gable', wall='render_yel', storeys=3, balc='balc_white', roofkey='roof_metal', seed=1):
    """slab block (lamellitalo) on a 3x1 lot, long side along canonical X, front = +Y"""
    pl = 3.0
    x0, x1, y0, y1 = 3.0, 93.0, 6.0, 26.0
    ze = pl + storeys * STOREY
    md = Model(name, 3, 1)
    wins = {'y+': [], 'x+': []}
    doors = {'y+': []}
    bands = {'y+': []}
    stairs = [x0 + 22.5, x0 + 45.0 + 22.5 - 22.5 + 22.5 - 22.5 + 22.5][:1] + [x0 + 67.5]
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
    if kind == 'gable':
        zr = ze + 4.5
        md.add(gable_body(x0, y0, x1, y1, 0, ze, zr, 'x', m_block, wall=wall, plinth=pl, wins=wins,
                          doors=doors, bands=bands, roofkey=roofkey, frame='trim'))
        md.add(gable_roof(x0, y0, x1, y1, ze, zr, 'x', m_roof, ov=1.5, ovg=1.5, th=1.4, key=roofkey,
                          edgekey='render_white', seam=3.0))
    else:
        md.add(box(x0, y0, 0, x1, y1, ze + 1.2, m_block, wall=wall, plinth=pl, wins=wins, doors=doors,
                   bands=bands, joints=joints, roofkey='bitumen', frame='panel_dk' if kind == 'panel' else 'trim'))
        # stair / lift housings on the roof
        for sx in stairs:
            md.add(box(sx - 3.0, (y0 + y1) / 2 - 3.0, ze, sx + 3.0, (y0 + y1) / 2 + 3.0, ze + 5.0, m_block,
                       wall=wall, plinth=-1, roofkey='bitumen', parapet=0.0))
    # balconies on the front between the stairwells
    bcols = []
    x = x0 + 8.0
    while x + 5.0 <= x1 - 4.0:
        if all(abs(x + 2.5 - sx) > 9.0 for sx in stairs):
            bcols.append(x)
        x += 15.0
    for i, bx in enumerate(bcols):
        key = balc
        if kind == 'panel':
            key = ('balc_orange', 'balc_blue', 'balc_green')[i % 3]
        for k in range(1, storeys):
            z = pl + k * STOREY
            md.add(box(bx, y1, z - 0.6, bx + 6.0, y1 + 3.0, z + 4.0, m_balcony, key=key))
    # canopies over doors
    for sx in stairs:
        md.add(box(sx - 3.0, y1, pl + 7.5, sx + 3.0, y1 + 2.5, pl + 8.3, m_flat('panel_dk', 0.5)))
    gr = [(sx - 2.0, y1, sx + 2.0, 32.0, 'gravel') for sx in stairs]
    gr.append((x0 - 2, y1 + 3.5, x1 + 2, y1 + 5.0, 'gravel'))
    add_birch(md, 7.0, 30.0, h=32, seed=seed + 31)
    add_bush(md, 50.0, 29.5, seed=seed + 5)
    add_bush(md, 88.0, 29.0, seed=seed + 9)
    md.ground = lambda g, st: g_lot(g, st, gravel=gr, var=seed)
    return md


def pistetalo(name, wall='render_white', storeys=8, balc='balc_orange', seed=1):
    pl = 3.0
    x0, x1, y0, y1 = 14.0, 50.0, 14.0, 50.0
    ze = pl + storeys * STOREY
    md = Model(name, 2, 2)
    wins = {'y+': [], 'x+': []}
    doors = {'y+': [(30.0, 34.0, pl - 0.5, pl + 7.0)]}
    bands = {'y+': [], 'x+': []}
    for k in range(storeys):
        t0 = pl + k * STOREY + 2.5
        t1 = t0 + 5.5
        for s in (x0 + 3.0, x0 + 8.5, x0 + 22.5, x0 + 28.0):
            if k == 0 and 26 < s + 1.75 < 38:
                continue
            wins['y+'].append((s, s + 4.0, t0, t1))
        for s in (y0 + 3.0, y0 + 8.5, y0 + 14.0, y0 + 19.5, y0 + 25.0):
            wins['x+'].append((s, s + 4.0, t0, t1))
        # staircase windows up the middle of the front
        if k > 0:
            wins['y+'].append((x0 + 16.5, x0 + 19.5, t0 + 1.0, t1 + 1.0))
    wins['y-'] = [r for r in wins['y+'] if not (x0 + 16 < r[0] < x0 + 20)]
    wins['x-'] = list(wins['x+'])
    md.add(box(x0, y0, 0, x1, y1, ze + 1.2, m_block, wall=wall, plinth=pl, wins=wins, doors=doors,
               bands=bands, roofkey='bitumen'))
    md.add(box(26.0, 26.0, ze, 38.0, 36.0, ze + 7.0, m_block, wall=wall, plinth=-1, roofkey='bitumen',
               parapet=0.0, wins={'y+': [(30.0, 33.0, ze + 2.0, ze + 5.0)]}))
    # balcony stacks: front right and side front
    for k in range(1, storeys):
        z = pl + k * STOREY
        md.add(box(x1 - 12.0, y1, z - 0.6, x1 - 3.0, y1 + 3.2, z + 4.0, m_balcony, key=balc))
        md.add(box(x1, y1 - 12.0, z - 0.6, x1 + 3.2, y1 - 3.0, z + 4.0, m_balcony, key=balc))
    md.add(box(28.0, y1, pl + 7.5, 36.0, y1 + 3.0, pl + 8.3, m_flat('panel_dk', 0.5)))
    gr = [(30.0, y1, 34.0, 64.0, 'gravel'), (6.0, 56.0, 58.0, 59.0, 'gravel')]
    add_spruce(md, 6.0, 8.0, h=34, seed=seed + 11, r=5)
    add_birch(md, 58.0, 8.0, h=34, seed=seed + 12)
    add_birch(md, 8.0, 58.0, h=30, seed=seed + 13)
    add_bush(md, 50.0, 57.0, seed=seed + 14)
    add_bush(md, 58.0, 44.0, seed=seed + 15)
    md.ground = lambda g, st: g_lot(g, st, gravel=gr, var=seed)
    return md


def shop(name, seed=1):
    """small co-op store: flat roof, display windows, sign band, awning"""
    pl = 1.0
    x0, x1, y0, y1 = 5.0, 27.0, 5.0, 22.0
    ze = pl + 11.0
    wins = {'y+': [(x0 + 1.5, x0 + 8.5, pl + 1.5, pl + 7.5), (x0 + 13.5, x1 - 1.5, pl + 1.5, pl + 7.5)],
            'x+': [(y0 + 3.0, y0 + 7.0, pl + 3.0, pl + 7.5)]}
    doors = {'y+': [(x0 + 9.5, x0 + 12.5, pl, pl + 7.5)]}
    bands = {'y+': [(x0 + 1.0, x1 - 1.0, pl + 9.0, ze - 0.5, 'sign_green')],
             'x+': [(y0 + 1.0, y1 - 1.0, pl + 9.0, ze - 0.5, 'sign_green')]}
    md = Model(name)
    md.add(box(x0, y0, 0, x1, y1, ze + 1.0, m_shop, wall='render_white', plinth=pl, wins=wins, doors=doors,
               bands=bands, roofkey='bitumen', frame='trim', wkind='shop'))
    md.add(box(x0 - 0.5, y1, pl + 8.0, x1 + 0.5, y1 + 3.0, pl + 8.8, m_awning))
    md.add(box(x1 - 6.0, y0 + 3.0, ze + 1.0, x1 - 2.0, y0 + 7.0, ze + 3.0, m_flat('steel')))
    # flag pole
    md.add(Cone(3.0, 28.5, 0, 26.0, 0.35, 0.3, m_flat('trim'), cap=True))
    md.add(box(3.0, 28.4, 21.0, 3.2, 34.0, 25.5, m_flag))
    md.add(box(x1 + 1.0, y1 + 4.0, 0, x1 + 1.6, y1 + 6.0, 6.0, m_flat('sign_red')))   # kiosk sign post? (mailbox)
    gr = [(0.0, y1, 32.0, 32.0, 'asphalt'), (x1, y0, 32.0, y1, 'gravel')]
    md.ground = lambda g, st: g_lot(g, st, gravel=gr, var=seed)
    return md


def m_flag(g, st):
    P = g.P
    # Finnish flag: white with blue cross (on the flag's +X face)
    a = P[1] - 28.4
    b = P[2] - 21.0
    if 1.6 <= a < 2.8 or 1.7 <= b < 2.8:
        return 'balc_blue', 0.6
    return 'trim', 0.2


def m_awning(g, st):
    if g.tag in ('y+', 'x+', 'x-'):
        s, t = local_st(g)
        if g.tag == 'y+':
            return ('awning' if int(s / 2.0) % 2 == 0 else 'trim'), 0.0
        return 'awning', 0.0
    if g.tag == 'top':
        s = g.P[0]
        return ('awning' if int(s / 2.0) % 2 == 0 else 'trim'), 0.0
    return 'awning', -1.0


def m_shop(g, st):
    p = g.solid.par
    if g.tag in ('y+', 'x+'):
        s, t = local_st(g)
        # sign lettering: a row of light glyph-like blocks inside the sign band (reads as text)
        for r in p['bands'].get(g.tag, ()):
            if in_rect(s, t, r[:4]) and st.zoom >= 1:
                if g.tag == 'y+':
                    rel_s, rel_t = s - r[0] - 2.0, t - r[2] - 0.5
                    if 0 <= rel_t < 1.6 and 0 <= rel_s < (r[1] - r[0] - 4.0):
                        col = int(rel_s)
                        # "OSUUSKAUPPA" -> just a rhythm of 3-wide letters with 1 px gaps
                        if col % 3 != 2 and ihash(col, int(rel_t * 2), 9) < 0.8:
                            return 'sign_txt', 0.3
    return m_block(g, st)


def factory(name, seed=1):
    md = Model(name, 2, 2)
    hx0, hx1, hy0, hy1 = 4.0, 44.0, 6.0, 46.0
    hw = 15.0
    wins = {'y+': [(hx0 + 4 + 9 * i, hx0 + 10 + 9 * i, 4.0, 11.0) for i in range(4)],
            'x+': [(hy0 + 4 + 9 * i, hy0 + 10 + 9 * i, 4.0, 11.0) for i in range(2)]}
    doors = {'x+': [(hy0 + 24.0, hy0 + 32.0, 0.0, 10.0)]}
    md.add(box(hx0, hy0, 0, hx1, hy1, hw, m_brickwall, wins=wins, doors=doors))
    # north-light sawtooth roof: ridges along Y, glazing faces +X
    tw = 8.0
    n = int((hx1 - hx0) / tw)
    for i in range(n):
        xa = hx0 + i * tw
        xb = xa + tw
        h = 6.0
        s = h / tw
        pl = [(-1, 0, 0, -xa, 'x-'), (1, 0, 0, xb, 'glass'), (0, -1, 0, -hy0, 'y-'), (0, 1, 0, hy1, 'y+'),
              (0, 0, -1, -hw, 'bot'), (-s, 0, 1, hw - s * xa, 'slope')]
        md.add(Convex(pl, (xa, hy0, hw, xb, hy1, hw + h), m_saw))
    # boiler house + chimney
    md.add(box(46.0, 6.0, 0, 60.0, 24.0, 12.0, m_brickwall, wins={'y+': [(49.0, 52.0, 4.0, 9.0), (54.0, 57.0, 4.0, 9.0)]},
               doors={'x+': [(10.0, 15.0, 0.0, 8.0)]}))
    md.add(Cone(53.0, 13.0, 12.0, 112.0, 3.6, 2.3, m_cone_brick, cap=True))
    md.add(Cone(53.0, 13.0, 0.0, 12.5, 4.4, 4.4, m_cone_brick, cap=True))
    # office (2 storeys, rendered) at the front
    ow = {'y+': [(32.0 + 5 * i, 35.0 + 5 * i, 3.0 + k * STOREY, 8.0 + k * STOREY) for i in range(5) for k in range(2)],
          'x+': [(52.0 + 4 * i, 55.0 + 4 * i, 3.0 + k * STOREY, 8.0 + k * STOREY) for i in range(2) for k in range(2)]}
    od = {'y+': []}
    md.add(box(30.0, 50.0, 0, 60.0, 61.0, 2 * STOREY + 2.5, m_block, wall='render_grey', plinth=1.5, wins=ow,
               doors=od, roofkey='bitumen'))
    # coal heap
    md.add(HeightField(46.0, 26.0, 62.0, 44.0, 9.0,
                       lambda x, y: 9.0 * max(0.0, 1.0 - ((x - 54.0) / 7.0) ** 2 - ((y - 35.0) / 8.0) ** 2) ** 0.8,
                       m_flat('coal')))
    gr = [(0.0, 46.0, 64.0, 64.0, 'asphalt'), (44.0, 0.0, 64.0, 46.0, 'gravel'), (0.0, 0.0, 64.0, 6.0, 'gravel')]
    md.ground = lambda g, st: g_lot(g, st, gravel=gr, var=seed)
    return md


def m_saw(g, st):
    if g.tag == 'glass':
        s, t = g.P[1], g.P[2]
        if (s % 4.0) < 1.0 or t < g.solid.aabb[2] + 1.0:
            return 'steel', 0.0
        return 'factory_glass', (0.7 if (s % 4.0) < 2.0 else 0.0)
    if g.tag == 'slope':
        return 'roof_dark', (0.5 if (g.P[1] % 3.0) < 1.0 and st.zoom >= 1 else 0.0)
    if g.tag in ('y+', 'y-'):
        return 'brick', 0.0
    return 'roof_dark', 0.0


def water_tower(name, seed=1):
    md = Model(name)
    c = 16.0
    md.add(Cone(c, c, 0, 58.0, 4.2, 3.4, m_concrete_round, cap=False, key='render_white',
                wins=[(20.0, 23.0, 3), (40.0, 43.0, 3)]))
    md.add(Cone(c, c, 58.0, 68.0, 3.4, 12.0, m_concrete_round, cap=False, key='render_white'))
    md.add(Cone(c, c, 68.0, 80.0, 12.0, 12.0, m_concrete_round, cap=False, key='render_white', ribs=24,
                band=(77.0, 80.0, 'render_grey')))
    md.add(Cone(c, c, 80.0, 83.0, 12.4, 10.0, m_concrete_round, cap=True, key='render_grey', capkey='render_grey'))
    md.add(Cone(c, c, 83.0, 86.0, 2.0, 2.0, m_concrete_round, cap=True, key='render_grey'))
    md.add(box(c - 2.0, c + 3.5, 0, c + 2.0, c + 5.0, 6.0, m_flat('door')))
    add_spruce(md, 4.5, 26.0, h=30, seed=seed + 1, r=4.5)
    add_birch(md, 27.0, 5.0, h=28, seed=seed + 2)
    gr = [(c - 2.0, c, c + 2.0, 32.0, 'gravel')]
    md.ground = lambda g, st: g_lot(g, st, gravel=gr, var=seed)
    return md


def power_pole(name, axis='x'):
    """wooden pole + crossarm + 3 wires spanning the tile along canonical X (rot for Y)"""
    md = Model(name, ground=None)
    c = 16.0
    H = 34.0
    md.add(Cone(c, c, 0, H, 0.75, 0.6, m_flat('pole'), cap=True))
    md.add(box(c - 0.5, c - 4.0, H - 3.5, c + 0.5, c + 4.0, H - 2.2, m_flat('pole', -0.3)))
    for dy in (-3.2, 0.0, 3.2):
        zt = H - 2.2 if dy else H + 0.6
        if dy == 0.0:
            md.add(box(c - 0.4, c - 0.4, H, c + 0.4, c + 0.4, zt + 0.6, m_flat('trim')))
        else:
            md.add(box(c - 0.4, c + dy - 0.4, zt, c + 0.4, c + dy + 0.4, zt + 1.4, m_flat('trim')))
        zw = (zt + 1.4) if dy else (zt + 0.6)
        md.lines.append(((0.0, c + dy, zw - 2.0), (c, c + dy, zw), 'wire', 0))
        md.lines.append(((c, c + dy, zw), (T, c + dy, zw - 2.0), 'wire', 0))
    for s in md.solids:
        s.cls = 'detail'
    return md


CAR_COLOURS = ['white', 'beige', 'yellow', 'orange', 'lblue', 'dblue', 'red', 'green', 'brown']
CAR_DIRS = [('px', 0), ('ny', 1), ('nx', 2), ('py', 3)]     # heading +X, -Y, -X, +Y (rot of the model)


def m_car(g, st):
    """lower body of the Lada: bonnet/boot on top, grille + round lamps front, tail lamps rear"""
    body = g.solid.par['body']
    P = g.P
    tg = g.tag
    dy = abs(P[1] - 16.0)
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
    """upright greenhouse: dark glass all round with a B pillar, light roof in body colour"""
    body = g.solid.par['body']
    P = g.P
    tg = g.tag
    if tg == 'top':
        return body, 0.7
    if P[2] < 4.5:
        return body, 0.0
    if tg == 'ws':
        return ('glass_lt', 0.0) if P[2] > 6.3 else ('glass', 0.0)
    if tg == 'rw':
        return 'glass', -0.3
    if tg in ('y+', 'y-'):
        if 15.0 <= P[0] < 16.0:             # B pillar
            return body, 0.0
        return 'glass', -0.4
    return body, 0.0


def car(name, colour):
    """VAZ-2101 style three-box saloon, canonical heading +X, centred on the tile centre.
    Real car ~4.1 x 1.6 x 1.4 m; drawn 14 x 6.2 units, 7.4 px tall: exaggerated like the houses
    so the long bonnet / upright cabin / short boot silhouette survives at 1x."""
    md = Model(name, ground=None)
    body = 'car_' + colour
    gp = new_grp()
    x0, x1, y0, y1 = 9.0, 23.0, 12.9, 19.1
    for wx in (10.4, 19.2):                  # wheels (show dark under the sills)
        md.add(box(wx, y0 + 0.2, 0.0, wx + 2.4, y1 - 0.2, 2.2, m_flat('tyre'), grp=gp))
    md.add(box(x0, y0, 1.2, x1, y1, 4.2, m_car, body=body, grp=gp))
    for (a, b) in ((x0 - 0.6, x0 + 0.1), (x1 - 0.1, x1 + 0.6)):   # chrome bumpers
        md.add(box(a, y0 - 0.2, 1.5, b, y1 + 0.2, 2.4, m_flat('chrome', 0.5), grp=gp))
    zb, zt = 4.2, 7.4
    k = 1.0 / (zt - zb)                      # windscreen / rear window rake: 1 unit over the height
    tk = 0.45 / (zt - zb)                    # tumblehome
    xr, xf = 12.2, 18.6                      # short boot, long bonnet
    pl = [(0, 0, -1, -zb, 'bot'), (0, 0, 1, zt, 'top'),
          (1, 0, k, xf + k * zb, 'ws'), (-1, 0, k, -xr + k * zb, 'rw'),
          (0, 1, tk, y1 - 0.4 + tk * zb, 'y+'), (0, -1, tk, -(y0 + 0.4) + tk * zb, 'y-')]
    md.add(Convex(pl, (xr, y0, zb, xf, y1, zt), m_cabin, body=body, grp=gp))
    md.top = 14.0
    return md


def tree_model(name, kind, seed=1, autumn=False):
    md = Model(name, ground=None)
    if kind == 'birch':
        add_birch(md, 16, 16, h=36, seed=seed, autumn=autumn)
    elif kind == 'spruce':
        add_spruce(md, 16, 16, h=44, seed=seed)
    elif kind == 'pine':
        add_pine(md, 16, 16, h=46, seed=seed)
    else:
        add_apple(md, 16, 16, h=18, seed=seed)
    return md


def forest(name, kinds, seed=1, autumn=False):
    md = Model(name)
    spots = [(9.0, 9.0), (23.0, 8.0), (8.0, 23.0), (22.0, 22.0), (16.0, 15.5)]
    for i, (x, y) in enumerate(spots[:len(kinds)]):
        x += (ihash(i, seed, 1) - 0.5) * 4
        y += (ihash(i, seed, 2) - 0.5) * 4
        k = kinds[i]
        if k == 'birch':
            add_birch(md, x, y, h=30 + 8 * ihash(i, seed, 3), seed=seed * 10 + i, autumn=autumn and i % 2 == 0)
        elif k == 'spruce':
            add_spruce(md, x, y, h=34 + 10 * ihash(i, seed, 3), seed=seed * 10 + i, r=5.5)
        elif k == 'pine':
            add_pine(md, x, y, h=38 + 8 * ihash(i, seed, 3), seed=seed * 10 + i)
        elif k == 'bush':
            add_bush(md, x, y, seed=seed * 10 + i)
    md.ground = lambda g, st: g_grass(g, st, seed)
    return md


def rock_tile(name, seed=1):
    """silokallio: glacier-smoothed granite outcrop with a couple of boulders"""
    md = Model(name)
    gp = new_grp()

    def h(x, y):
        v = 0.0
        for (cx, cy, r, a) in ((15.0, 14.0, 10.0, 9.0), (9.0, 21.0, 6.0, 5.0), (22.0, 10.0, 6.5, 6.0),
                               (21.0, 21.0, 4.5, 3.5)):
            d = ((x - cx) ** 2 + (y - cy) ** 2) / (r * r)
            if d < 1:
                v = max(v, a * (1 - d) ** 0.55)
        return v * (0.92 + 0.16 * vnoise(x, y, 4.0, seed + 90))

    md.add(HeightField(1.0, 1.0, 31.0, 31.0, 10.5, h, m_rock, grp=gp))
    md.add(Ellipsoid(26.0, 25.0, 1.6, 2.6, 2.2, 2.6 * KZ, m_rock, grp=new_grp()))
    md.add(Ellipsoid(6.0, 8.0, 1.2, 2.0, 1.8, 2.0 * KZ, m_rock, grp=new_grp()))
    add_bush(md, 27.0, 15.0, seed=seed + 3, size=0.8)
    md.ground = lambda g, st: g_grass(g, st, seed)
    return md


def terrain(name, fn):
    md = Model(name)
    md.ground = fn
    return md


# =========================================================================== style pass
def shade(gb, st):
    """G-buffer -> RGB image (key background) for one style"""
    st.zoom = gb['zoom']
    W, H, grid, md = gb['w'], gb['h'], gb['grid'], gb['model']
    img = Image.new('RGB', (W, H), KEY)
    px_ = img.load()
    keys = [[None] * W for _ in range(H)]
    for y in range(H):
        row = grid[y]
        for x in range(W):
            g = row[x]
            if g is None:
                continue
            if g.kind == 'ground':
                st.texture = st.tex_ground
                key, d = md.ground(g, st)
                st.texture = st.tex_obj
                l = g.light - 0.10 * g.ao * (1.0 if st.texture > 0.5 else 0.6)
            else:
                key, d = g.solid.mat(g, st)
                l = g.light
                if st.texture > 0.8 and g.tag in ('x+', 'y+', 'x-', 'y-') and g.solid.cls == 'obj':
                    # soft vertical gradient on walls (darker near the ground), B only
                    d += clamp(g.P[2] / 8.0, 0, 1.0) * 0.5 - 0.5
            keys[y][x] = key
            snap = g.kind == 'ground' or not g.solid.curved
            px_[x, y] = st.color(key, l, d, x, y, snap)
    if st.outline != 'none':
        out = []
        thr = 3.0
        for y in range(H):
            for x in range(W):
                g = grid[y][x]
                if g is None or g.kind != 'solid' or g.solid.cls != 'obj':
                    continue
                hitn = None
                for (dx, dy, side) in ((1, 0, 'r'), (0, 1, 'b'), (-1, 0, 'l'), (0, -1, 't')):
                    xx, yy = x + dx, y + dy
                    if not (0 <= xx < W and 0 <= yy < H):
                        hitn = side
                        break
                    n = grid[yy][xx]
                    if n is None or n.kind == 'ground':
                        hitn = side
                        break
                    if n.solid.grp != g.solid.grp and (n.depth < g.depth - thr and n.solid.cls != 'detail' or
                                                       n.depth < g.depth - 8.0):
                        hitn = side
                        break
                if hitn:
                    out.append((x, y, hitn))
        for (x, y, side) in out:
            if st.outline == 'dark':
                px_[x, y] = to565(st.outline_col)
            else:
                r = st.ramp(keys[y][x])
                c = r[0] if side in ('r', 'b', 'x') else r[1]
                px_[x, y] = to565(lerpc(c, st.outline_col, 0.35))
    # wires
    zoom = gb['zoom']
    tw = gb['to_world']
    for (P0, P1, key, sag) in md.lines:
        col = st.ramp(key)[len(st.ramp(key)) * 2 // 3]
        a, b = tw(P0), tw(P1)
        L = int(max(abs(a[0] - b[0]) + abs(a[1] - b[1]), 4) * 2 * zoom)
        last = None
        for i in range(L + 1):
            u = i / float(L)
            X, Y = lerp(a[0], b[0], u), lerp(a[1], b[1], u)
            Z = lerp(a[2], b[2], u) - sag * 4 * u * (1 - u)
            sx = gb['ox'] + (X - Y) * zoom
            sy = gb['oy'] + ((X + Y) / 2.0 - Z) * zoom
            ix, iy = int(math.floor(sx)), int(math.floor(sy))
            if 0 <= ix < W and 0 <= iy < H and (ix, iy) != last:
                px_[ix, iy] = col
                last = (ix, iy)
    return img


def write_bmp(path, img):
    img = img.convert('RGB')
    w, h = img.size
    raw = img.tobytes()
    rowsize = (w * 3 + 3) & ~3
    pad = b'\0' * (rowsize - w * 3)
    out = bytearray()
    for y in range(h - 1, -1, -1):
        row = bytearray(raw[y * w * 3:(y + 1) * w * 3])
        row[0::3], row[2::3] = row[2::3], row[0::3]
        out += row + pad
    hdr = struct.pack('<2sIHHI', b'BM', 54 + len(out), 0, 0, 54)
    info = struct.pack('<IiiHHIIiiII', 40, w, h, 1, 24, 0, len(out), 2835, 2835, 0, 0)
    with open(path, 'wb') as f:
        f.write(hdr + info + bytes(out))


# =========================================================================== the sprite set
SHORE_NAMES = {}


def build_set():
    """name -> (model, rot, group).  group: 'tile' (flat 64x32 ground), 'obj' (sprite with lot
    ground), 'ovl' (keyed overlay without ground)"""
    S = {}

    def put(name, md, rot=0, grp='obj'):
        md.name = name
        S[name] = (md, rot, grp)

    for v in range(3):
        put('grass%d' % v, terrain('grass', (lambda vv: lambda g, st: g_grass(g, st, vv))(v)), grp='tile')
    put('field_crop', terrain('f', lambda g, st: g_field(g, st, 'crop')), grp='tile')
    put('field_plough', terrain('f', lambda g, st: g_field(g, st, 'plough')), grp='tile')
    put('water', terrain('w', g_water), grp='tile')
    for mask in range(1, 15):
        c = (mask & 1, (mask >> 1) & 1, (mask >> 2) & 1, (mask >> 3) & 1)
        put('shore%02d' % mask, terrain('s', (lambda cc: lambda g, st: g_shore(g, st, cc))(c)), grp='tile')
    # roads: asphalt full set, gravel straight + corners + T
    for kind, pre in (('asphalt', 'road'), ('gravel', 'grav')):
        sets = {'x': ('-x', '+x'), 'y': ('-y', '+y'),
                'c_nw_sw': ('-x', '+y'), 'c_nw_ne': ('-x', '-y'), 'c_se_sw': ('+x', '+y'), 'c_se_ne': ('+x', '-y'),
                't_nx': ('+x', '-y', '+y'), 't_px': ('-x', '-y', '+y'), 't_ny': ('-x', '+x', '+y'),
                't_py': ('-x', '+x', '-y'), 'cross': ('-x', '+x', '-y', '+y')}
        for k, conn in sets.items():
            put('%s_%s' % (pre, k), terrain(k, (lambda cc, kk: lambda g, st: g_road(g, st, cc, kk))(conn, kind)),
                grp='tile')
    put('rock', rock_tile('rock', 3))
    put('forest_mixed', forest('f', ['spruce', 'birch', 'pine', 'spruce', 'bush'], 1))
    put('forest_spruce', forest('f', ['spruce', 'spruce', 'spruce', 'spruce', 'spruce'], 2))
    put('forest_birch', forest('f', ['birch', 'birch', 'birch', 'bush'], 3))
    put('forest_pine', forest('f', ['pine', 'pine', 'spruce', 'pine'], 4))
    houses = [
        dict(wall='w_yellow', ridge='x', porch='gable', porch_at=0.2, tree='apple', chim=0.55, seed=1),
        dict(wall='w_red', ridge='y', porch='canopy', porch_at=0.5, tree='birch', chim=0.35, seed=2,
             door='door_green'),
        dict(wall='w_blue', ridge='x', porch='gable', porch_at=0.15, dormer=True, tree='birch',
             tree_at=(29.0, 4.0), chimkey='chim_white', seed=3),
        dict(wall='w_green', ridge='y', porch='gable', porch_at=0.75, tree='apple', chim=0.6, clad='v', seed=4),
        dict(wall='w_grey', ridge='x', porch='canopy', porch_at=0.3, storeys=2, tree='spruce',
             tree_at=(29.0, 4.0), chim=0.7, seed=5, door='door_green'),
        dict(wall='w_cream', ridge='x', porch='gable', porch_at=0.8, dormer=True, tree='apple', chim=0.3,
             clad='v', seed=6, roof='roof_red'),
    ]
    for i, h in enumerate(houses):
        for r in range(4):
            put('house%d_r%d' % (i, r), rintama('house%d' % i, **h), rot=r)
    put('lamelli_gable', lamelli('lg', 'gable', 'render_yel', 3, seed=1))
    put('lamelli_gable_r1', lamelli('lg', 'gable', 'render_yel', 3, seed=1), rot=1)
    put('lamelli_panel', lamelli('lp', 'panel', 'panel', 4, seed=2))
    put('lamelli_panel_r1', lamelli('lp', 'panel', 'panel', 4, seed=2), rot=1)
    put('pistetalo', pistetalo('pt', seed=1))
    put('shop', shop('shop'))
    put('shop_r1', shop('shop'), rot=1)
    put('factory', factory('factory'))
    put('watertower', water_tower('wt'))
    put('power_x', power_pole('px'), grp='ovl')
    put('power_y', power_pole('py'), rot=1, grp='ovl')
    wire = power_pole('pw')
    wire.solids = []
    put('wire_x', wire, grp='ovl')
    for c in CAR_COLOURS:
        for (dn, r) in CAR_DIRS:
            put('car_%s_%s' % (c, dn), car('car', c), rot=r, grp='car')
    for k in ('birch', 'spruce', 'pine', 'apple'):
        put('tree_' + k, tree_model(k, k, seed=7), grp='ovl')
    put('tree_birch_au', tree_model('birch', 'birch', seed=8, autumn=True), grp='ovl')
    return S


def render_all(S, zooms=(1.0, 0.5), only=None, verbose=True):
    G = {}
    t0 = time.time()
    for name, (md, rot, grp) in S.items():
        if only and name not in only:
            continue
        for z in zooms:
            G[(name, z)] = render_gbuffer(md, z, rot)
    if verbose:
        print('  g-buffers: %d in %.1fs' % (len(G), time.time() - t0))
    return G


def key_mask(img):
    """L mask: 255 where not key"""
    w, h = img.size
    m = Image.new('L', (w, h), 0)
    src = img.load()
    mp = m.load()
    for y in range(h):
        for x in range(w):
            if src[x, y] != KEY:
                mp[x, y] = 255
    return m


def pack_sheet(items, width=640):
    """shelf packer.  items: list of (name, img, anchor).  returns sheet img + rect list"""
    items = sorted(items, key=lambda it: (-it[1].height, it[0]))
    x = y = rowh = 0
    rects = []
    for name, img, anc in items:
        w, h = img.size
        if x + w > width:
            x, y, rowh = 0, y + rowh, 0
        rects.append((name, x, y, w, h, anc[0], anc[1]))
        x += w
        rowh = max(rowh, h)
    H = y + rowh
    sheet = Image.new('RGB', (width, H), KEY)
    lookup = dict((it[0], it[1]) for it in items)
    for (name, x, y, w, h, ax, ay) in rects:
        sheet.paste(lookup[name], (x, y))
    return sheet, rects


def write_header(path, prefix, sheets):
    lines = ['/* generated by gen_explore.py - do not edit */',
             '/* rect = x, y, w, h in the sheet; anchor = pixel in the sprite that sits on the SOUTH (bottom)',
             '   vertex of the footprint diamond.  Footprint is n x m tiles (X x Y). */',
             '#ifndef %s_H' % prefix.upper(), '#define %s_H' % prefix.upper(), '',
             'typedef struct { const char *name; short x, y, w, h, ax, ay; } SprRect;', '']
    for sname, rects in sheets:
        lines.append('/* %s */' % sname)
        lines.append('#define %s_COUNT %d' % (sname.upper(), len(rects)))
        lines.append('static const SprRect %s_rects[] = {' % sname)
        for (name, x, y, w, h, ax, ay) in rects:
            lines.append('    { "%s", %d, %d, %d, %d, %d, %d },' % (name, x, y, w, h, ax, ay))
        lines.append('};')
        lines.append('')
    lines.append('#endif')
    with open(path, 'w') as f:
        f.write('\r\n'.join(lines) + '\r\n')


# =========================================================================== pixel font (hand drawn)
# rows 0..6 = cap height (row 6 = baseline), rows 7..8 = descenders.  '#' = ink.
FONT = {
    'A': ['.##.', '#..#', '#..#', '####', '#..#', '#..#', '#..#'],
    'B': ['###.', '#..#', '#..#', '###.', '#..#', '#..#', '###.'],
    'C': ['.##.', '#..#', '#...', '#...', '#...', '#..#', '.##.'],
    'D': ['###.', '#..#', '#..#', '#..#', '#..#', '#..#', '###.'],
    'E': ['####', '#...', '#...', '###.', '#...', '#...', '####'],
    'F': ['####', '#...', '#...', '###.', '#...', '#...', '#...'],
    'G': ['.##.', '#..#', '#...', '#.##', '#..#', '#..#', '.###'],
    'H': ['#..#', '#..#', '#..#', '####', '#..#', '#..#', '#..#'],
    'I': ['###', '.#.', '.#.', '.#.', '.#.', '.#.', '###'],
    'J': ['..##', '...#', '...#', '...#', '#..#', '#..#', '.##.'],
    'K': ['#..#', '#..#', '#.#.', '##..', '#.#.', '#..#', '#..#'],
    'L': ['#...', '#...', '#...', '#...', '#...', '#...', '####'],
    'M': ['#...#', '##.##', '#.#.#', '#.#.#', '#...#', '#...#', '#...#'],
    'N': ['#..#', '##.#', '##.#', '#.##', '#.##', '#..#', '#..#'],
    'O': ['.##.', '#..#', '#..#', '#..#', '#..#', '#..#', '.##.'],
    'P': ['###.', '#..#', '#..#', '###.', '#...', '#...', '#...'],
    'Q': ['.##.', '#..#', '#..#', '#..#', '#..#', '#.#.', '.#.#'],
    'R': ['###.', '#..#', '#..#', '###.', '#.#.', '#..#', '#..#'],
    'S': ['.###', '#...', '#...', '.##.', '...#', '...#', '###.'],
    'T': ['#####', '..#..', '..#..', '..#..', '..#..', '..#..', '..#..'],
    'U': ['#..#', '#..#', '#..#', '#..#', '#..#', '#..#', '.##.'],
    'V': ['#...#', '#...#', '#...#', '.#.#.', '.#.#.', '..#..', '..#..'],
    'W': ['#...#', '#...#', '#...#', '#.#.#', '#.#.#', '##.##', '#...#'],
    'X': ['#...#', '#...#', '.#.#.', '..#..', '.#.#.', '#...#', '#...#'],
    'Y': ['#...#', '#...#', '.#.#.', '..#..', '..#..', '..#..', '..#..'],
    'Z': ['####', '...#', '..#.', '.#..', '#...', '#...', '####'],
    'Ä': ['#..#', '.##.', '#..#', '#..#', '####', '#..#', '#..#'],
    'Ö': ['#..#', '.##.', '#..#', '#..#', '#..#', '#..#', '.##.'],
    'a': ['....', '....', '.##.', '...#', '.###', '#..#', '.###'],
    'b': ['#...', '#...', '###.', '#..#', '#..#', '#..#', '###.'],
    'c': ['...', '...', '.##', '#..', '#..', '#..', '.##'],
    'd': ['...#', '...#', '.###', '#..#', '#..#', '#..#', '.###'],
    'e': ['....', '....', '.##.', '#..#', '####', '#...', '.##.'],
    'f': ['.##', '#..', '###', '#..', '#..', '#..', '#..'],
    'g': ['....', '....', '.###', '#..#', '#..#', '#..#', '.###', '...#', '.##.'],
    'h': ['#...', '#...', '###.', '#..#', '#..#', '#..#', '#..#'],
    'i': ['#', '.', '#', '#', '#', '#', '#'],
    'j': ['.#', '..', '.#', '.#', '.#', '.#', '.#', '.#', '#.'],
    'k': ['#...', '#...', '#..#', '#.#.', '##..', '#.#.', '#..#'],
    'l': ['#.', '#.', '#.', '#.', '#.', '#.', '.#'],
    'm': ['.....', '.....', '####.', '#.#.#', '#.#.#', '#.#.#', '#.#.#'],
    'n': ['....', '....', '###.', '#..#', '#..#', '#..#', '#..#'],
    'o': ['....', '....', '.##.', '#..#', '#..#', '#..#', '.##.'],
    'p': ['....', '....', '###.', '#..#', '#..#', '#..#', '###.', '#...', '#...'],
    'q': ['....', '....', '.###', '#..#', '#..#', '#..#', '.###', '...#', '...#'],
    'r': ['...', '...', '#.#', '##.', '#..', '#..', '#..'],
    's': ['...', '...', '.##', '#..', '.#.', '..#', '##.'],
    't': ['.#.', '.#.', '###', '.#.', '.#.', '.#.', '..#'],
    'u': ['....', '....', '#..#', '#..#', '#..#', '#..#', '.###'],
    'v': ['.....', '.....', '#...#', '#...#', '.#.#.', '.#.#.', '..#..'],
    'w': ['.....', '.....', '#...#', '#...#', '#.#.#', '#.#.#', '.#.#.'],
    'x': ['....', '....', '#..#', '#..#', '.##.', '#..#', '#..#'],
    'y': ['....', '....', '#..#', '#..#', '#..#', '#..#', '.###', '...#', '.##.'],
    'z': ['....', '....', '####', '..#.', '.#..', '#...', '####'],
    'ä': ['#..#', '....', '.##.', '...#', '.###', '#..#', '.###'],
    'ö': ['#..#', '....', '.##.', '#..#', '#..#', '#..#', '.##.'],
    '0': ['.##.', '#..#', '#.##', '##.#', '#..#', '#..#', '.##.'],
    '1': ['.#.', '##.', '.#.', '.#.', '.#.', '.#.', '###'],
    '2': ['.##.', '#..#', '...#', '..#.', '.#..', '#...', '####'],
    '3': ['###.', '...#', '...#', '.##.', '...#', '...#', '###.'],
    '4': ['..#.', '.##.', '#.#.', '#.#.', '####', '..#.', '..#.'],
    '5': ['####', '#...', '###.', '...#', '...#', '#..#', '.##.'],
    '6': ['.##.', '#...', '#...', '###.', '#..#', '#..#', '.##.'],
    '7': ['####', '...#', '..#.', '..#.', '.#..', '.#..', '.#..'],
    '8': ['.##.', '#..#', '#..#', '.##.', '#..#', '#..#', '.##.'],
    '9': ['.##.', '#..#', '#..#', '.###', '...#', '...#', '.##.'],
    '.': ['.', '.', '.', '.', '.', '.', '#'],
    ',': ['.', '.', '.', '.', '.', '.', '#', '#'],
    ':': ['.', '.', '#', '.', '.', '.', '#'],
    '-': ['...', '...', '...', '###', '...', '...', '...'],
    '+': ['...', '...', '.#.', '###', '.#.', '...', '...'],
    '!': ['#', '#', '#', '#', '#', '.', '#'],
    '?': ['.##.', '#..#', '...#', '..#.', '.#..', '....', '.#..'],
    "'": ['#', '#', '.', '.', '.', '.', '.'],
    '"': ['#.#', '#.#', '...', '...', '...', '...', '...'],
    '/': ['...#', '...#', '..#.', '.##.', '.#..', '#...', '#...'],
    '%': ['#...#', '#..#.', '..#..', '.#...', '#..#.', '#...#', '.....'],
    '(': ['.#', '#.', '#.', '#.', '#.', '#.', '.#'],
    ')': ['#.', '.#', '.#', '.#', '.#', '.#', '#.'],
    ' ': ['..', '..', '..', '..', '..', '..', '..'],
}


def text_width(s):
    return sum(len(FONT.get(ch, FONT['?'])[0]) + 1 for ch in s) - 1 if s else 0


def draw_text(img, x, y, s, col, shadow=None):
    """draw string with top-left at (x, y); returns width"""
    px = img.load()
    W, H = img.size
    x0 = x
    for ch in s:
        gl = FONT.get(ch, FONT['?'])
        w = len(gl[0])
        for ry, row in enumerate(gl):
            for rx, c in enumerate(row):
                if c == '#':
                    if shadow is not None and 0 <= x + rx + 1 < W and 0 <= y + ry + 1 < H:
                        px[x + rx + 1, y + ry + 1] = shadow
        for ry, row in enumerate(gl):
            for rx, c in enumerate(row):
                if c == '#' and 0 <= x + rx < W and 0 <= y + ry < H:
                    px[x + rx, y + ry] = col
        x += w + 1
    return x - x0 - 1


# =========================================================================== UI pieces (period look)
UI = {
    'teak': '#6b4a32', 'teak_dk': '#4e3524', 'teak_lt': '#86603f',
    'cream': '#e6dcc2', 'cream_dk': '#c4b896', 'cream_lt': '#f4ecd6',
    'ink': '#2a2622', 'brass': '#b89a5a', 'brass_dk': '#7e6a3c', 'brass_lt': '#dcc488',
    'bar': '#3a3934', 'bar_lt': '#55534b', 'bar_dk': '#262522',
    'red': '#9c3b2e', 'green': '#4f6b48', 'blue': '#3c5468', 'paper': '#ddd3b6',
}
UIC = dict((k, to565(hexc(v))) for k, v in UI.items())

# 13x13 tool icons, drawn on the cream bakelite keys.  letters: k=ink, r=red, g=green, b=blue, w=white-ish
ICONS = {
    'road': ['.............', '.....k.k.....', '....k...k....', '....k.w.k....', '...k.....k...', '...k..w..k...',
             '..k.......k..', '..k...w...k..', '.k.........k.', '.k....w....k.', 'k...........k', '.............',
             '.............'],
    'house': ['......k......', '.....kkk.....', '....kkkkk....', '...kkkkkkk.k.', '..kkkkkkkkkk.', '.kkkkkkkkkkk.',
              '...rrrrrrr...', '...rwwrrrr...', '...rwwrkkr...', '...rrrrkkr...', '...rrrrkkr...', '.............',
              '.............'],
    'shop': ['.............', '.kkkkkkkkkkk.', '.kgggggggggk.', '.kgwgwgwgwgk.', '.kkkkkkkkkkk.', '..k.......k..',
             '..kbbb.bbbk..', '..kbbb.bbbk..', '..kbbb.kkkk..', '..k.....kkk..', '.kkkkkkkkkkk.', '.............',
             '.............'],
    'factory': ['..........k..', '..........k..', '.........kk..', '.........kk..', '.k...k...kk..', '.kk..kk..kk..',
                '.kkk.kkk.kk..', '.kkkkkkkkkkk.', '.kbkbkbkbkkk.', '.kkkkkkkkkkk.', '.kbkbkbkbkkk.', '.kkkkkkkkkkk.',
                '.............'],
    'power': ['......k......', '..kkkkkkkkk..', '..w...k...w..', '......k......', '......k......', '......k......',
              '......k......', '......k......', '......k......', '......k......', '.....kkk.....', '.............',
              '.............'],
    'water': ['......b......', '......b......', '.....bbb.....', '.....bbb.....', '....bbbbb....', '....bbwbb....',
              '...bbbwbbb...', '...bbbbbbb...', '...bbbbbbb...', '....bbbbb....', '.....bbb.....', '.............',
              '.............'],
    'tree': ['......g......', '.....ggg.....', '....ggggg....', '.....ggg.....', '...ggggggg...', '....ggggg....',
             '..ggggggggg..', '...ggggggg...', '.ggggggggggg.', '......k......', '......k......', '.............',
             '.............'],
    'bulldoze': ['.............', '.r.........r.', '..r.......r..', '...r.....r...', '....r...r....', '.....r.r.....',
                 '......r......', '.....r.r.....', '....r...r....', '...r.....r...', '..r.......r..', '.r.........r.',
                 '.............'],
    'service': ['.............', '.....kkk.....', '....k...k....', '....k...k....', '.....kkk.....', '......k......',
                '...rrrrrrr...', '...rrrwrrr...', '...rrwwwrr...', '...rrrwrrr...', '...rrrrrrr...', '.............',
                '.............'],
    'query': ['.............', '....kkkkk....', '...kk...kk...', '...kk...kk...', '........kk...', '.......kk....',
              '......kk.....', '......kk.....', '.............', '......kk.....', '......kk.....', '.............',
              '.............'],
}


def draw_icon(img, x, y, name, scale=1):
    px = img.load()
    cmap = {'k': UIC['ink'], 'r': UIC['red'], 'g': UIC['green'], 'b': UIC['blue'], 'w': UIC['cream_lt']}
    for ry, row in enumerate(ICONS[name]):
        for rx, c in enumerate(row):
            if c in cmap:
                for a in range(scale):
                    for b in range(scale):
                        px[x + rx * scale + a, y + ry * scale + b] = cmap[c]


def fill(img, r, col):
    x0, y0, x1, y1 = r
    px = img.load()
    for y in range(y0, y1):
        for x in range(x0, x1):
            px[x, y] = col


def bevel(img, r, lt, dk):
    x0, y0, x1, y1 = r
    px = img.load()
    for x in range(x0, x1):
        px[x, y0] = lt
        px[x, y1 - 1] = dk
    for y in range(y0, y1):
        px[x0, y] = lt
        px[x1 - 1, y] = dk


def wood(img, r, seed=1):
    """teak veneer: horizontal grain from value noise, quantised to 4 tones"""
    x0, y0, x1, y1 = r
    px = img.load()
    tones = [UIC['teak_dk'], to565(lerpc(hexc(UI['teak_dk']), hexc(UI['teak']), 0.5)), UIC['teak'], UIC['teak_lt']]
    for y in range(y0, y1):
        for x in range(x0, x1):
            n = vnoise(x * 0.08, y * 0.9 + 3.0 * vnoise(x * 0.5, y * 0.05, 1.0, seed + 1, 999), 1.0, seed, 999)
            n += (BAYER4[y & 3][x & 3] / 16.0 - 0.5) * 0.25
            px[x, y] = tones[int(clamp(n * 4, 0, 3.99))]


def button(img, x, y, w, h, icon, label, pressed=False):
    face = UIC['cream_dk'] if pressed else UIC['cream']
    fill(img, (x, y, x + w, y + h), face)
    bevel(img, (x, y, x + w, y + h), UIC['cream_dk'] if pressed else UIC['cream_lt'],
          UIC['cream_lt'] if pressed else UIC['ink'])
    if not pressed:
        bevel(img, (x + 1, y + 1, x + w - 1, y + h - 1), UIC['cream_lt'], UIC['cream_dk'])
    o = 1 if pressed else 0
    draw_icon(img, x + (w - 13) // 2 + o, y + 3 + o, icon)
    tw = text_width(label)
    draw_text(img, x + (w - tw) // 2 + o, y + h - 10 + o, label, UIC['ink'])


def portrait(img, x, y):
    """tiny caricature of the president: bald dome, round glasses, stern smile"""
    rows = [
        '.....kkkkkk.....',
        '...kkssssssk....',
        '..kssssssssssk..',
        '..ksssssssssssk.',
        '.kssssssssssssk.',
        '.ksssssssssssssk',
        '.kssssssssssssk.',
        'kksskkkkskkkksk.',
        'kssk.wwkskwwksk.',
        'kssk.wwkkkwwkssk',
        '.kssskkksskkkssk',
        '.ksssssssssssssk',
        '..ksssssddssssk.',
        '..kssssssssssk..',
        '..ksskkkkkksk...',
        '...kssssssssk...',
        '....kssssssk....',
        '.....kkkkkk.....',
        '...kkwwwwwwkk...',
        '.kkddwwrrwwddkk.',
        'kddddwwrrwwddddk',
        'kddddddrrdddddddk'[:16],
    ]
    cm = {'k': UIC['ink'], 's': to565((214, 170, 140)), 'w': UIC['cream_lt'], 'd': to565((60, 62, 72)),
          'r': UIC['red']}
    px = img.load()
    for ry, row in enumerate(rows):
        for rx, c in enumerate(row):
            if c in cm:
                px[x + rx, y + ry] = cm[c]


# =========================================================================== mockup town
MAPN = 26


def town_map():
    """returns ground[i][j] tile names and objects [(name, i0, j0, n, m)] (n along i/X, m along j/Y)"""
    N = MAPN
    ground = [['grass%d' % int(ihash(i, j, 500) * 3) for j in range(N)] for i in range(N)]
    land = [[1] * (N + 1) for _ in range(N + 1)]       # corner land flags
    for ci in range(N + 1):
        for cj in range(N + 1):
            d = ((ci - 6.0) / 6.8) ** 2 + ((cj - 7.0) / 6.0) ** 2 + (ihash(ci, cj, 501) - 0.5) * 0.25
            if d < 1.0:
                land[ci][cj] = 0
    objs = []
    occ = set()

    def place(name, i0, j0, n=1, m=1):
        for a in range(i0, i0 + n):
            for b in range(j0, j0 + m):
                occ.add((a, b))
        objs.append((name, i0, j0, n, m))

    for i in range(N):
        for j in range(N):
            c = (land[i][j], land[i + 1][j], land[i][j + 1], land[i + 1][j + 1])
            mask = c[0] | c[1] << 1 | c[2] << 2 | c[3] << 3
            if mask == 0:
                ground[i][j] = 'water'
                occ.add((i, j))
            elif mask != 15:
                ground[i][j] = 'shore%02d' % mask
                occ.add((i, j))
    # main asphalt road: along X at j=13, coming in from the lower left along Y at i=3
    for i in range(4, N):
        ground[i][13] = 'road_x'
        occ.add((i, 13))
    for j in range(14, N):
        ground[3][j] = 'road_y'
        occ.add((3, j))
    ground[3][13] = 'road_c_se_sw'
    occ.add((3, 13))
    # gravel residential streets: along Y at i=14, crossed by a street along X at j=19
    ground[14][13] = 'road_t_ny'
    for j in range(14, N):
        ground[14][j] = 'grav_y'
        occ.add((14, j))
    for i in range(8, 23):
        if i != 14:
            ground[i][19] = 'grav_x'
            occ.add((i, 19))
    ground[14][19] = 'grav_cross'
    # houses: fronts face the street (rot 0: +Y, 1: +X, 2: -Y, 3: -X)
    k = 0
    for j in range(15, 25):
        if j == 19:
            continue
        place('house%d_r1' % (k % 6), 13, j)
        place('house%d_r3' % ((k + 3) % 6), 15, j)
        k += 1
    for i in list(range(9, 13)) + list(range(16, 22)):
        place('house%d_r0' % ((k + 1) % 6), i, 18)
        place('house%d_r2' % ((k + 4) % 6), i, 20)
        k += 1
    # power line along the verge south of the main road
    for i in range(5, N):
        if (i, 14) in occ:
            continue
        pole = i % 2 == 0 and not ground[i][14].startswith(('road', 'grav'))
        objs.append(('power_x' if pole else 'wire_x', i, 14, 1, 1))
        occ.add((i, 14))
    objs.append(('wire_x', 14, 14, 1, 1))
    # concrete blocks north of the main road
    place('shop', 13, 12)
    place('lamelli_gable', 15, 12, 3, 1)
    place('lamelli_panel', 18, 12, 3, 1)
    place('pistetalo', 15, 9, 2, 2)
    place('pistetalo', 18, 9, 2, 2)
    place('lamelli_gable_r1', 21, 9, 1, 3)
    def dry(i, j):
        return all(land[a][b] for a in (i, i + 1) for b in (j, j + 1)) and (i, j) not in occ

    for (nm, cands) in (('watertower', [(12, 10), (13, 10), (13, 9), (14, 9), (14, 10)]),
                        ('rock', [(12, 9), (13, 8), (11, 10), (14, 7), (12, 8)]),
                        ('rock', [(14, 8), (15, 7), (10, 11), (11, 11), (15, 6)])):
        for (a, b) in cands:
            if dry(a, b):
                place(nm, a, b)
                break
    # traffic: right-hand traffic, lane offsets in world units
    for (c, i, j, d, off) in (('red', 6, 13, 'px', (4.0, 3.6)), ('lblue', 9, 13, 'nx', (-2.0, -3.6)),
                              ('white', 17, 13, 'px', (0.0, 3.6)), ('yellow', 22, 13, 'nx', (6.0, -3.6)),
                              ('dblue', 25, 13, 'px', (0.0, 3.6)), ('green', 14, 16, 'py', (-3.0, 2.0)),
                              ('beige', 14, 23, 'ny', (3.0, 0.0)), ('brown', 10, 19, 'px', (0.0, 2.5)), ('red', 18, 19, 'nx', (0.0, -2.5)),
                              ('orange', 3, 17, 'ny', (3.6, 0.0)), ('white', 3, 22, 'py', (-3.6, 6.0))):
        objs.append(('car_%s_%s' % (c, d), i, j, 1, 1, off))
    objs.append(('car_orange_px', 13, 12, 1, 1, (-6.0, 11.0)))
    # factory beyond the residential area
    place('factory', 19, 15, 2, 2)
    place('forest_spruce', 18, 15)
    place('forest_birch', 18, 16)
    # fields south-east (long strip parcels with open ditches)
    for i in range(16, N):
        for j in range(21, N):
            if (i, j) not in occ:
                ground[i][j] = 'field_plough' if 19 <= i <= 20 else 'field_crop'
                occ.add((i, j))
    # woods and scattered trees on what is left
    for i in range(N):
        for j in range(N):
            if (i, j) in occ:
                continue
            h = ihash(i, j, 502)
            edge = (i < 3 or j > 22 or (i + j) < 13 or i > 22 or (j < 6 and i > 12))
            if edge and h < 0.75 or h < 0.14:
                kinds = ['forest_mixed', 'forest_spruce', 'forest_birch', 'forest_pine']
                place(kinds[int(ihash(i, j, 503) * 4)], i, j)
            elif h < 0.36:
                tk = ['tree_birch', 'tree_spruce', 'tree_pine', 'tree_birch_au'][int(ihash(i, j, 504) * 4)]
                objs.append((tk, i, j, 1, 1))
    return ground, objs


def compose_map(sprites, ground, objs, zoom=1.0, pad=200):
    """paste ground tiles, then objects cut into 32 px column strips sorted by depth"""
    N = MAPN
    tw, th = int(64 * zoom), int(32 * zoom)
    W = N * tw
    H = N * th + int(pad * zoom)
    oy = int(pad * zoom)
    ox = N * tw // 2
    canvas = Image.new('RGB', (W, H), to565((40, 44, 40)))
    for i in range(N):
        for j in range(N):
            img, m, anc = sprites[ground[i][j]]
            x = ox + (i - j) * tw // 2 - tw // 2
            y = oy + (i + j) * th // 2
            canvas.paste(img, (x, y), m)
    strips = []
    cw = tw // 2
    for ob in objs:
        name, i0, j0, n, mm = ob[:5]
        du, dv = ob[5] if len(ob) > 5 else (0.0, 0.0)     # world-unit offset (cars)
        img, m, anc = sprites[name]
        # south vertex of the footprint
        si, sj = (i0 + 0.5, j0 + 0.5) if name.startswith('car_') else (i0 + n, j0 + mm)
        sx = ox + int((si - sj) * tw // 2) + int(round((du - dv) * zoom))
        sy = oy + int((si + sj) * th // 2) + int(round((du + dv) / 2.0 * zoom))
        x0 = sx - anc[0]
        y0 = sy - anc[1]
        # columns: tile (i, j) covers screen columns k = i - j - 1 and i - j (in half-tile units)
        for k in range(int(math.floor((x0 - ox) / float(cw))), int(math.ceil((x0 + img.width - ox) / float(cw)))):
            best = None
            for a in range(i0, i0 + n):
                for b in range(j0, j0 + mm):
                    if a - b - 1 == k or a - b == k:
                        if best is None or a + b > best:
                            best = a + b
            if best is None:          # strip outside the footprint columns (overhang): use nearest
                best = max(a + b for a in range(i0, i0 + n) for b in range(j0, j0 + mm))
            cx0 = ox + k * cw
            l = max(cx0, x0) - x0
            r = min(cx0 + cw, x0 + img.width) - x0
            if r <= l:
                continue
            strips.append((best, k, name, img.crop((l, 0, r, img.height)), m.crop((l, 0, r, img.height)),
                           (x0 + l, y0)))
    strips.sort(key=lambda s: (s[0], s[1]))
    for (_, _, _, im, mk, pos) in strips:
        canvas.paste(im, pos, mk)
    return canvas, ox, oy


MINI_COL = {'water': 'water', 'shore': 'sand', 'grass': 'grass', 'road': 'asphalt', 'grav': 'gravel',
            'field_crop': 'crop', 'field_plough': 'plough', 'forest': 'spruce', 'house': 'w_red', 'lamelli': 'render_white',
            'pistetalo': 'render_white', 'shop': 'sign_green', 'factory': 'brick', 'watertower': 'render_white',
            'rock': 'rock', 'power': None, 'wire': None, 'tree': 'birch_leaf', 'car': None}


def minimap(img, x0, y0, w, h, ground, objs, centre):
    st = STYLES['C']
    N = MAPN
    cells = {}
    for i in range(N):
        for j in range(N):
            g = ground[i][j]
            k = next((v for kk, v in MINI_COL.items() if g.startswith(kk)), 'grass')
            cells[(i, j)] = k
    for ob in objs:
        name, i0, j0, n, m = ob[:5]
        k = next((v for kk, v in MINI_COL.items() if name.startswith(kk)), None)
        if k:
            for a in range(i0, i0 + n):
                for b in range(j0, j0 + m):
                    cells[(a, b)] = k
    px = img.load()
    cx = x0 + w // 2
    for (i, j), k in cells.items():
        col = st.ramp(k)[st.levels - 3]
        sx = cx + (i - j) * 2
        sy = y0 + 2 + (i + j)
        for (dx, dy) in ((-1, 0), (0, 0), (-2, 1), (-1, 1), (0, 1), (1, 1)):
            if x0 <= sx + dx < x0 + w and y0 <= sy + dy < y0 + h:
                px[sx + dx, sy + dy] = col
    # viewport frame
    ci, cj = centre
    vx, vy = cx + int((ci - cj) * 2), y0 + 2 + int(ci + cj)
    for (a, b) in ((-17, 17), ):
        for xx in range(vx + a, vx + b):
            for yy in (vy - 14, vy + 14):
                if x0 <= xx < x0 + w and y0 <= yy < y0 + h:
                    px[xx, yy] = UIC['cream_lt']
        for yy in range(vy - 14, vy + 15):
            for xx in (vx + a, vx + b):
                if x0 <= xx < x0 + w and y0 <= yy < y0 + h:
                    px[xx, yy] = UIC['cream_lt']


def make_ui(base, W, H, style_name, map_rect, mini=None):
    """top info bar + right teak panel + bottom ticker, period flavour"""
    img = base
    mx0, my0, mx1, my1 = map_rect
    # --- top bar
    fill(img, (0, 0, W, 18), UIC['bar'])
    for x in range(W):
        img.putpixel((x, 0), UIC['bar_lt'])
        img.putpixel((x, 17), UIC['bar_dk'])
    x = 6
    fields = [('Lokakuu 1961', None), ('Rahat', '1 250 000 mk'), ('Väkiluku', '4 812'), ('Tyytyväisyys', '74 %')]
    for i, (a, b) in enumerate(fields):
        if i:
            for y in range(3, 15):
                img.putpixel((x - 5, y), UIC['bar_dk'])
                img.putpixel((x - 4, y), UIC['bar_lt'])
        if b is None:
            # calendar plate
            w = text_width(a) + 8
            fill(img, (x, 3, x + w, 15), UIC['paper'])
            bevel(img, (x, 3, x + w, 15), UIC['cream_lt'], UIC['brass_dk'])
            draw_text(img, x + 4, 5, a, UIC['ink'])
            x += w + 10
        else:
            draw_text(img, x, 5, a + ':', UIC['cream_dk'])
            x += text_width(a + ':') + 4
            draw_text(img, x, 5, b, UIC['cream_lt'], UIC['bar_dk'])
            x += text_width(b) + 10
    # speed indicator lamps at the right of the bar
    for i, on in enumerate((1, 1, 0)):
        cx = W - 14 - i * 9
        fill(img, (cx, 6, cx + 6, 12), UIC['brass_lt'] if on else UIC['bar_dk'])
        bevel(img, (cx, 6, cx + 6, 12), UIC['brass_dk'], UIC['ink'])
    # --- right panel
    px0 = mx1
    wood(img, (px0, 18, W, H), 7)
    for y in range(18, H):
        img.putpixel((px0, y), UIC['ink'])
        img.putpixel((px0 + 1, y), UIC['teak_lt'])
    # brass name plaque
    pw = W - px0 - 12
    fill(img, (px0 + 6, 23, px0 + 6 + pw, 37), UIC['brass'])
    bevel(img, (px0 + 6, 23, px0 + 6 + pw, 37), UIC['brass_lt'], UIC['brass_dk'])
    t = 'KAUPUNKI'
    draw_text(img, px0 + 6 + (pw - text_width(t)) // 2, 27, t, UIC['ink'], UIC['brass_lt'])
    # keys 2 x 5
    tools = [('road', 'Tie'), ('house', 'Asunto'), ('shop', 'Kauppa'), ('factory', 'Tehdas'), ('power', 'Sähkö'),
             ('water', 'Vesi'), ('service', 'Palvelu'), ('tree', 'Puisto'), ('bulldoze', 'Purku'), ('query', 'Tieto')]
    bw, bh = (pw - 4) // 2, 30
    for n, (ic, lab) in enumerate(tools):
        bx = px0 + 6 + (n % 2) * (bw + 4)
        by = 42 + (n // 2) * (bh + 3)
        button(img, bx, by, bw, bh, ic, lab, pressed=(n == 1))
    # zone colour strip for the selected tool (residential)
    by = 42 + 5 * (bh + 3) + 2
    draw_text(img, px0 + 7, by, 'Pientalot', UIC['cream_lt'], UIC['teak_dk'])
    by += 11
    # president plaque
    ph = 48
    fill(img, (px0 + 6, by, px0 + 6 + pw, by + ph), UIC['paper'])
    bevel(img, (px0 + 6, by, px0 + 6 + pw, by + ph), UIC['cream_lt'], UIC['brass_dk'])
    fill(img, (px0 + 9, by + 3, px0 + 29, by + 29), UIC['cream_dk'])
    portrait(img, px0 + 11, by + 5)
    draw_text(img, px0 + 33, by + 4, 'Pres.', UIC['ink'])
    draw_text(img, px0 + 33, by + 14, '"Hyvä,', UIC['red'])
    draw_text(img, px0 + 33, by + 24, 'hyvä!"', UIC['red'])
    draw_text(img, px0 + 10, by + 36, 'Kansa luottaa.', UIC['ink'])
    if mini is not None:
        my = by + ph + 6
        draw_text(img, px0 + 7, my, 'Kartta', UIC['cream_lt'], UIC['teak_dk'])
        my += 11
        mh = min(60, H - my - 8)
        fill(img, (px0 + 6, my, px0 + 6 + pw, my + mh), UIC['bar_dk'])
        minimap(img, px0 + 7, my + 1, pw - 2, mh - 2, mini[0], mini[1], mini[2])
        bevel(img, (px0 + 6, my, px0 + 6 + pw, my + mh), UIC['brass_dk'], UIC['brass_lt'])
    # --- ticker at the bottom of the map
    fill(img, (mx0, H - 14, mx1, H), UIC['bar'])
    for x in range(mx0, mx1):
        img.putpixel((x, H - 14), UIC['bar_lt'])
    draw_text(img, mx0 + 6, H - 11, 'Uutiset: Uusi lähiö valmistui. Presidentti vihki vesitornin.',
              UIC['cream_lt'], UIC['bar_dk'])
    return img


# =========================================================================== main
def shade_set(S, G, st, zoom):
    out = {}
    for name, (md, rot, grp) in S.items():
        gb = G.get((name, zoom))
        if gb is None:
            continue
        img = shade(gb, st)
        m = key_mask(img)
        anc = gb['anchor']
        if grp == 'car':                   # cars anchor on their own ground centre point
            anc = (anc[0], anc[1] - int(16 * gb['zoom']))
        if grp in ('ovl', 'car'):          # overlays are trimmed to their bounding box
            bb = m.getbbox()
            if bb:
                img, m = img.crop(bb), m.crop(bb)
                anc = (anc[0] - bb[0], anc[1] - bb[1])
        out[name] = (img, m, anc)
    return out


def mockup(spr, W=640, H=480, centre=(14.2, 13.8), style_name='A', panel=104):
    ground, objs = town_map()
    canvas, ox, oy = compose_map(spr, ground, objs, 1.0)
    mx0, my0, mx1, my1 = 0, 18, W - panel, H - 14
    ci, cj = centre
    cx = ox + (ci - cj) * 32
    cy = oy + (ci + cj) * 16
    vw, vh = mx1 - mx0, my1 - my0
    view = canvas.crop((int(cx - vw / 2), int(cy - vh / 2), int(cx - vw / 2) + vw, int(cy - vh / 2) + vh))
    img = Image.new('RGB', (W, H), (0, 0, 0))
    img.paste(view, (mx0, my0))
    make_ui(img, W, H, style_name, (mx0, my0, mx1, my1), (ground, objs, centre))
    return img


def preview_sheet(spr, names, bg=(70, 74, 66), cols=None, scale=2):
    imgs = [(n, spr[n]) for n in names if n in spr]
    W = 0
    rows, row, rw, rh = [], [], 0, 0
    for n, (im, m, a) in imgs:
        if rw + im.width + 6 > 640 and row:
            rows.append((row, rh))
            row, rw, rh = [], 0, 0
        row.append((n, im, m))
        rw += im.width + 6
        rh = max(rh, im.height)
    if row:
        rows.append((row, rh))
    H = sum(r[1] + 6 for r in rows)
    out = Image.new('RGB', (640, H), bg)
    y = 0
    for row, rh in rows:
        x = 0
        for n, im, m in row:
            out.paste(im, (x, y + rh - im.height), m)
            x += im.width + 6
        y += rh + 6
    return out.resize((out.width * scale, out.height * scale), Image.NEAREST)


def main():
    quick = '--quick' in sys.argv
    styles = [a for a in sys.argv[1:] if a in STYLES] or list(STYLES)
    S = build_set()
    zooms = (1.0,) if quick else (1.0, 0.5)
    G = render_all(S, zooms)
    for sn in styles:
        st = STYLES[sn]
        t0 = time.time()
        for z in zooms:
            spr = shade_set(S, G, st, z)
            tag = '%s_%d' % (sn, 64 if z == 1.0 else 32)
            if not quick:
                sw = 640 if z == 1.0 else 320
                tiles = [(n, spr[n][0], spr[n][2]) for n in spr if S[n][2] == 'tile']
                objs = [(n, spr[n][0], spr[n][2]) for n in spr if S[n][2] in ('obj', 'ovl')]
                cars = [(n, spr[n][0], spr[n][2]) for n in spr if S[n][2] == 'car']
                sh_t, r_t = pack_sheet(tiles, sw)
                sh_o, r_o = pack_sheet(objs, sw)
                sh_c, r_c = pack_sheet(cars, sw // 2)
                write_bmp(os.path.join(HERE, 'tiles_%s.bmp' % tag), sh_t)
                write_bmp(os.path.join(HERE, 'sprites_%s.bmp' % tag), sh_o)
                write_bmp(os.path.join(HERE, 'cars_%s.bmp' % tag), sh_c)
                write_header(os.path.join(HERE, 'sheet_%s.h' % tag), 'sheet_%s' % tag,
                             [('tiles_%s' % tag, r_t), ('sprites_%s' % tag, r_o), ('cars_%s' % tag, r_c)])
            if z == 1.0:
                if sn == 'C':
                    big = mockup(spr, 800, 600, centre=(14.4, 13.6), style_name=sn, panel=128)
                    big.save(os.path.join(HERE, 'mockup_C_800.png'))
                    write_bmp(os.path.join(HERE, 'mockup_C_800.bmp'), big)
                img = mockup(spr, style_name=sn)
                img.save(os.path.join(HERE, 'mockup_%s.png' % sn))
                write_bmp(os.path.join(HERE, 'mockup_%s.bmp' % sn), img)
                if not quick:
                    names = ['house%d_r0' % i for i in range(6)] + ['house%d_r1' % i for i in range(6)] + \
                        ['lamelli_gable', 'lamelli_panel', 'pistetalo', 'shop', 'factory', 'watertower', 'rock',
                         'forest_mixed', 'forest_spruce', 'forest_birch', 'forest_pine', 'tree_birch', 'tree_spruce',
                         'tree_pine', 'tree_apple', 'tree_birch_au', 'power_x', 'power_y']
                    preview_sheet(spr, names).save(os.path.join(HERE, 'preview_sprites_%s.png' % sn))
                    tn = [n for n in S if S[n][2] == 'tile']
                    preview_sheet(spr, tn).save(os.path.join(HERE, 'preview_tiles_%s.png' % sn))
            else:
                img = mockup_zoom(spr, sn)
                img.save(os.path.join(HERE, 'mockup_%s_zoom32.png' % sn))
        print('  style %s done in %.1fs' % (sn, time.time() - t0))


def mockup_zoom(spr, sn, W=640, H=480):
    ground, objs = town_map()
    canvas, ox, oy = compose_map(spr, ground, objs, 0.5)
    ci, cj = 13.0, 13.0
    cx = ox + (ci - cj) * 16
    cy = oy + (ci + cj) * 8
    vw, vh = W - 104, H - 32
    view = canvas.crop((int(cx - vw / 2), int(cy - vh / 2), int(cx - vw / 2) + vw, int(cy - vh / 2) + vh))
    img = Image.new('RGB', (W, H), (0, 0, 0))
    img.paste(view, (0, 18))
    make_ui(img, W, H, sn, (0, 18, W - 104, H - 14), (ground, objs, (ci, cj)))
    return img


def check():
    """self-check of the written files: format, width, key purity, seamless tiling"""
    import glob
    ok = True
    for f in sorted(glob.glob(os.path.join(HERE, '*.bmp'))):
        d = open(f, 'rb').read()
        sig, _, _, _, off = struct.unpack('<2sIHHI', d[:14])
        isz, w, h, planes, bpp, comp = struct.unpack('<IiiHHI', d[14:34])
        im = Image.open(f).convert('RGB')
        bad = 0
        for (r, g, b) in im.getdata():
            if (r, g, b) != KEY and r >= 248 and g <= 8 and b >= 248:
                bad += 1
        if sig != b'BM' or bpp != 24 or comp != 0 or off != 54 or w > 800 or bad:
            print('  FAIL %s bpp=%d comp=%d w=%d near-key=%d' % (os.path.basename(f), bpp, comp, w, bad))
            ok = False
        if os.path.basename(f).startswith(('tiles', 'sprites', 'cars')) and w > 640:
            print('  FAIL %s wider than 640' % f)
            ok = False
    # seams: 4x4 of each flat tile kind composed edge to edge must contain no key pixel
    sh = Image.open(os.path.join(HERE, 'tiles_C_64.bmp')).convert('RGB')
    hdr = open(os.path.join(HERE, 'sheet_C_64.h')).read()
    import re
    rects = dict((m.group(1), tuple(int(v) for v in m.groups()[1:5]))
                 for m in re.finditer(r'"(\w+)", (\d+), (\d+), (\d+), (\d+)', hdr))
    for nm in ('grass0', 'water', 'road_x', 'grav_y', 'field_crop'):
        x, y, w, h = rects[nm]
        t = sh.crop((x, y, x + w, y + h))
        m = key_mask(t)
        can = Image.new('RGB', (64 * 5, 32 * 5), KEY)
        for i in range(4):
            for j in range(4):
                can.paste(t, (128 + (i - j) * 32, 8 + (i + j) * 16), m)
        hole = sum(1 for yy in range(40, 110) for xx in range(130, 190) if can.getpixel((xx, yy)) == KEY)
        if hole:
            print('  FAIL seam %s: %d key pixels inside a 4x4 patch' % (nm, hole))
            ok = False
    print('  check: %s' % ('PASS' if ok else 'FAIL'))
    return ok


if __name__ == '__main__':
    if '--check' in sys.argv:
        sys.exit(0 if check() else 1)
    main()
    if '--quick' not in sys.argv:
        check()
