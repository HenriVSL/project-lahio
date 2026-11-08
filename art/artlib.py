"""artlib.py - core of the KAUPUNKI world-art generator (T02).  Python 3 + Pillow, no numpy.

Grown from art/explore/gen_explore.py (frozen reference).  Style C ("Nordic hybrid") only.

  MODELS    every sprite is a small 3-D model: convex solids, cones, ellipsoids, height fields, a
            ground material for its lot and 3-D wire lines.  Units: X, Y in tile units (1 tile =
            32 x 32), Z in screen pixels of height.  Built canonically with the front on +Y.
  G-BUFFER  render_gbuffer() ray-casts the model once per zoom/rotation into per-pixel records
            (material, face tag, canonical point, normal, light incl. ray-traced shadow, depth,
            contact AO).  This is the material/normal G-buffer path a later snow pass can use:
            shade() takes a `mode` and every pixel still knows its normal and material.
  STYLE     shade() maps the G-buffer to RGB565-rounded colours (8-tone ramps, sel-out outlines,
            cool shadows / warm light).  Modes: 'normal', 'ab' (abandoned: boarded/broken windows,
            rust, weeds, desaturated and darkened).
Projection: sx = (X - Y) * zoom, sy = ((X + Y) / 2 - Z) * zoom.  2:1 dimetric (camera 30 deg).
"""
import math
import struct

from PIL import Image

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


def is565(c):
    r, g, b = c
    return to565(c) == (r, g, b) or c == KEY


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


def edge_window(x, y):
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
        if par.pop('detail', False):
            self.cls = 'detail'
        if par.pop('noshadow', False):
            self.shadow = False

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


def hip_body(x0, y0, x1, y1, z0, ze, zr, mat, inset=None, **par):
    """box with a hipped top: ridge along the longer axis, all four roof planes at the same pitch"""
    hx, hy = (x1 - x0) / 2.0, (y1 - y0) / 2.0
    h = min(hx, hy) if inset is None else inset
    s = (zr - ze) / h
    cx, cy = (x0 + x1) / 2.0, (y0 + y1) / 2.0
    pl = [(-1, 0, 0, -x0, 'x-'), (1, 0, 0, x1, 'x+'), (0, -1, 0, -y0, 'y-'), (0, 1, 0, y1, 'y+'),
          (0, 0, -1, -z0, 'bot'),
          (0, s, 1, ze + s * y1, 'hy+'), (0, -s, 1, ze - s * y0, 'hy-'),
          (s, 0, 1, ze + s * x1, 'hx+'), (-s, 0, 1, ze - s * x0, 'hx-')]
    return Convex(pl, (x0, y0, z0, x1, y1, zr), mat, **par)


def roof_slab(a0, a1, mid, edge, zr, slope, th, axis, mat, side_tag='roof', **par):
    """one plane of a gable roof as a thick slab.  a0..a1 along the ridge, the slab runs from the
    ridge line (b=mid, height zr at its underside) to b=edge (edge>mid: +side, else -side)."""
    sg = 1 if edge > mid else -1
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
    pl.append((t1[0], t1[1], 1, zr + th + k * mid, side_tag))
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


def mono_body(x0, y0, x1, y1, z0, zlo, zhi, mat, rise='y-', **par):
    """box with a mono-pitch (shed) top rising toward side `rise`"""
    pl = [(-1, 0, 0, -x0, 'x-'), (1, 0, 0, x1, 'x+'), (0, -1, 0, -y0, 'y-'), (0, 1, 0, y1, 'y+'),
          (0, 0, -1, -z0, 'bot')]
    if rise in ('y-', 'y+'):
        L = y1 - y0
        s = (zhi - zlo) / L
        if rise == 'y-':     # high at y0:  Z <= zhi - s*(y - y0)
            pl.append((0, s, 1, zhi + s * y0, 'top'))
        else:
            pl.append((0, -s, 1, zlo - s * y0, 'top'))
    else:
        L = x1 - x0
        s = (zhi - zlo) / L
        if rise == 'x-':
            pl.append((s, 0, 1, zhi + s * x0, 'top'))
        else:
            pl.append((-s, 0, 1, zlo - s * x0, 'top'))
    return Convex(pl, (x0, y0, z0, x1, y1, zhi), mat, **par)


class Cone(Solid):
    """vertical truncated cone / cylinder: centre cx,cy, radius r0 at z0 -> r1 at z1 (units)"""
    curved = True

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
    """Z <= h(X, Y): marched along the ray (rock outcrops, heaps)"""
    curved = True

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
        h = self.h
        while t <= hi:
            x, y, z = O[0] + t * D[0], O[1] + t * D[1], O[2] + t * D[2]
            if z <= h(x, y) and z > 0.05:
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
    """n x m tiles (canonical), list of solids, ground material, extra 3-D wire lines.
    hot = canonical (X, Y, Z) of the chimney top (smoke hotspot) or None.
    prepare(gb) (optional) runs after the G-buffer is built and before shading (shore cleanup)."""

    def __init__(self, name, n=1, m=1, ground=None, top=None):
        self.name = name
        self.n, self.m = n, m
        self.solids = []
        self.ground = ground            # function(g, style) -> (key, delta) ; None = transparent
        self.lines = []                 # (P0, P1, colour key, sag) 3-D wires drawn after shading
        self.top = top
        self.hot = None
        self.prepare = None

    def add(self, *ss):
        for s in ss:
            if isinstance(s, (list, tuple)):
                self.add(*s)
            else:
                self.solids.append(s)
        return self


class GPix(object):
    __slots__ = ('kind', 'solid', 'tag', 'P', 'W', 'n', 'light', 'shadow', 'depth', 'cls', 'ao',
                 'px', 'py')


def _rot_fwd(rot, x, y):
    for _ in range(rot % 4):
        x, y = y, -x
    return x, y


def _rot_inv(rot, x, y):
    for _ in range(rot % 4):
        x, y = -y, x
    return x, y


def render_gbuffer(model, zoom=1.0, rot=0):
    """ray-cast the model; returns dict(w, h, grid[y][x] of GPix or None, anchor, ...)"""
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
        top = max(top, model.top)
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
    shadow_casters = [s for s in sol if s.shadow]
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
                    g.n = norm3((n[0], n[1], n[2] * KZ))
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
            g.px, g.py = px, py
            ndl = g.n[0] * Lc[0] + g.n[1] * Lc[1] + g.n[2] * Lc[2]
            sh = 0.0
            if ndl > 0:
                P = g.P
                Ps = (P[0] + g.n[0] * 0.05 + Lpx[0] * 0.02, P[1] + g.n[1] * 0.05 + Lpx[1] * 0.02,
                      P[2] + 0.05 + Lpx[2] * 0.02)
                for s in shadow_casters:
                    if s is g.solid and not isinstance(s, (HeightField, Ellipsoid)):
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
    convex = [s for s in sol if isinstance(s, Convex) and s.aabb[2] <= 0.6 and s.cls != 'detail']
    if convex:
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
                    if w <= near:
                        continue
                    Q = (P[0] + dx, P[1] + dy, 0.6)
                    for s in convex:
                        b = s.aabb
                        if not (b[0] <= Q[0] <= b[3] and b[1] <= Q[1] <= b[4]):
                            continue
                        if s.inside(Q):
                            near = max(near, w)
                            break
                g.ao = near
    anchor = (int(round(ox + (wn - wm) * T * zoom)), H)   # south vertex of footprint
    hot = None
    if model.hot is not None:
        w = to_world(model.hot)
        hot = (int(math.floor(ox + (w[0] - w[1]) * zoom)), int(math.floor(oy + ((w[0] + w[1]) / 2.0 - w[2]) * zoom)))
    return dict(w=W, h=H, grid=grid, anchor=anchor, ox=ox, oy=oy, zoom=zoom, model=model,
                rot=rot, wn=wn, wm=wm, to_world=to_world, hot=hot)


# =========================================================================== palette + style C
# base colours = the colour of a face in full light (l ~ 0.8).  New colours go here as a lit base.
PAL_BASE = {
    # terrain: muted moss / olive greens that recede behind the buildings
    'grass': '#75785a', 'grass_dk': '#616548', 'grass_lt': '#878867', 'meadow': '#84835f',
    'flower_y': '#b3a65c', 'flower_w': '#c9c6b4', 'flower_p': '#8f839a',
    'dirt': '#7d6d59', 'dirt_dk': '#665a4a', 'sand': '#b2a688', 'pebble': '#969188',
    'water': '#4a5e6a', 'water_dk': '#3e4f5a', 'water_lt': '#76878e', 'shallow': '#5f6f6d',
    'foam': '#cdd0ca', 'foam_dk': '#9ea6a7',
    'reed': '#8d8e5c', 'reed_dk': '#6f7149',
    'rock': '#8c817c', 'rock_dk': '#6a625f', 'lichen': '#9a9a80',
    'crop': '#b09f6c', 'crop_dk': '#958659', 'plough': '#6c5d4d', 'plough_dk': '#584c40',
    'marsh': '#7a7a58', 'marsh_dk': '#5e6047', 'cotton': '#cfccbd',
    # roads
    'asphalt': '#5b5c5c', 'asph_lt': '#686968', 'asph_dk': '#4c4d4d', 'roadline': '#c4bea0',
    'roadyel': '#c6a648', 'roadwhite': '#cbc8bd',
    'gravel': '#9d9283', 'gravel_lt': '#aea495', 'gravel_dk': '#83796c', 'kerb': '#a29f97',
    'ballast': '#8b857c', 'ballast_dk': '#726d66', 'sleeper': '#5a4a3c', 'rail': '#b8b9b6',
    'buffer_red': '#a8483a',
    # wooden houses: weathered single colours (THEME 2.1 earth pigments, muted)
    'w_yellow': '#c4a15e', 'w_red': '#8a4a3b', 'w_blue': '#93a3ab', 'w_green': '#a0ae93',
    'w_ggreen': '#8d9580', 'w_grey': '#aeada4', 'w_cream': '#d3c69f', 'w_pink': '#c2a294',
    'w_brown': '#6f5240', 'w_weath': '#8e8678',
    'trim': '#dfdacd', 'glass': '#3d454d', 'glass_lt': '#74808a', 'door': '#685444',
    'door_green': '#56624f', 'door_red': '#8a3f33', 'plinth': '#868379',
    'roof_metal': '#767b7c', 'roof_dark': '#55585a', 'roof_red': '#6c463d', 'roof_felt': '#4d4f51',
    'brick': '#8f5643', 'brick_dk': '#704536', 'cap': '#3b3836', 'chim_white': '#cfcbc0',
    # concrete era: slightly warm greys
    'render_white': '#d1cdc1', 'render_yel': '#cdbf9b', 'render_grey': '#a5a299',
    'render_salmon': '#c7a690', 'render_green': '#b5bda4',
    'panel': '#b2ada1', 'panel_dk': '#8a867c', 'panel_wash': '#a39d90', 'bitumen': '#4e4c4a',
    'parapet': '#99968d', 'tile_brown': '#7b5340',
    'balc_orange': '#a8714f', 'balc_blue': '#6b8091', 'balc_green': '#7b8a64', 'balc_white': '#ccc8bc',
    'balc_mustard': '#b0934a', 'balc_red': '#9c5444',
    'sign_green': '#456650', 'sign_red': '#954739', 'sign_blue': '#4c6380', 'sign_yel': '#b89a4a',
    'sign_txt': '#e8e2d2', 'awning': '#9a5140', 'awning_g': '#56705a', 'awning_b': '#5a6f88',
    'factory_glass': '#59646c', 'steel': '#6b6f71', 'steel_lt': '#9da2a5', 'corr': '#8d9295',
    'firedoor': '#9a3a2e', 'copper': '#6f8a7a',
    # nature
    'birch_leaf': '#8c955e', 'birch_leaf_au': '#bb9d52', 'spruce': '#3c5146', 'pine_leaf': '#56654a',
    'apple_leaf': '#65744c', 'apple': '#a64a3c', 'rowan_leaf': '#6f7a4c', 'rowan_berry': '#a4423a',
    'bark_birch': '#dad6cc', 'bark_dk': '#2f2d2b',
    'bark_pine': '#93684d', 'bark': '#5f5241', 'bush': '#5b6849', 'lilac': '#8b84a0',
    'hedge': '#4f5e44',
    'fence': '#d8d4c8', 'fence_wood': '#80705c', 'pole': '#685847', 'wire': '#b9b6aa',
    'coal': '#2f2e2f', 'woodpile': '#8a765d', 'logend': '#c4ab7c', 'timber': '#c8ae7a',
    'sawdust': '#b39a6c', 'flowers': '#bba955', 'flag_blue': '#4f6d96',
    'concrete': '#a9a69e', 'concrete_dk': '#8e8b84', 'sandpile': '#b9a77f', 'scaffold': '#9a8a6c',
    'boards': '#7c7263', 'glass_broken': '#25292d', 'rust': '#76594b', 'grime': '#5d5a52',
    # cars (Lada 1200 style): allowed to be more colourful than the terrain so they read
    'car_white': '#e2ded3', 'car_beige': '#cbb896', 'car_yellow': '#dcc26a', 'car_orange': '#cf7a38',
    'car_lblue': '#8fb0c6', 'car_dblue': '#37507a', 'car_red': '#b03a2e', 'car_green': '#4f7a4c',
    'car_brown': '#78533c', 'car_mustard': '#c9a23a', 'car_grey': '#8d9196', 'car_aubergine': '#6a3c55',
    'tyre': '#262626', 'chrome': '#c9cccc', 'headlight': '#f2eed4', 'taillight': '#b0281e',
    # 1.1 W2 winter (THEME 3.2): used only by the 'winter' shade mode
    'asph_w': '#6c7074', 'spruce_w': '#34483d', 'pine_w': '#4a5946', 'twig': '#7a6d66', 'twig_lt': '#9d938f',
    'reed_w': '#a8996a', 'win_lit': '#d2b06a', 'wire_w': '#5c5a55', 'twig_gap': '#bcb8bb',
}
# explicit 8-tone ramps (dark -> lit) for the winter materials; index 5 = a flat ground pixel in full light
CUSTOM_RAMPS = {
    'snow': ['#8597ae', '#a3b1c5', '#bcc8d8', '#cad4e0', '#d7dee8', '#e3e9ef', '#eceff3', '#f4f6f8'],
    'ice': ['#566e84', '#6b8398', '#7f96aa', '#91a8ba', '#9fb5c6', '#acc1d0', '#b9ccda', '#c6d7e2'],
    'packed': ['#6f6c69', '#84807b', '#98938c', '#a8a39b', '#b6b1a9', '#c2bdb5', '#ccc8c1', '#d6d3cd'],
}


class Style(object):
    def __init__(self, name, levels=8, texture=0.8, gscale=0.45, hue=0.40, shadow_tint='#26304e',
                 light_tint='#fbefd2', outline_col='#1e2230', sat=0.9, contrast=1.1, boards=0.7, hi=0.28):
        self.name = name
        self.levels = levels
        self.hue = hue
        self.shadow_tint, self.light_tint = hexc(shadow_tint), hexc(light_tint)
        self.outline_col = hexc(outline_col)
        self.sat, self.contrast, self.boards, self.hi = sat, contrast, boards, hi
        self.tex_obj, self.tex_ground = texture, texture * gscale
        self.texture = texture
        self.pal = {}
        for k, v in PAL_BASE.items():
            c = hexc(v)
            g = 0.3 * c[0] + 0.59 * c[1] + 0.11 * c[2]
            self.pal[k] = tuple(clamp(g + (x - g) * sat, 0, 255) for x in c)
        self.cache = {}
        self.zoom = 1.0
        self.winter = False         # 1.1 W2: set by shade() for the winter mode (ground functions read it)

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
        if r is None and key in CUSTOM_RAMPS:
            r = [to565(hexc(c)) for c in CUSTOM_RAMPS[key]]
            self.cache[key] = r
        if r is None:
            base = self.pal[key]
            n = self.levels
            r = [to565(self.cont(base, 0.30 + 0.70 * i / (n - 1.0))) for i in range(n)]
            self.cache[key] = r
        return r

    def color(self, key, l, delta, snap=True):
        n = self.levels
        pos = (l - 0.30) / 0.70 * (n - 1)
        if snap:            # flat faces: one tone per face
            pos = math.floor(pos + 0.5)
        pos += delta * (n - 1) / 6.0
        i = int(math.floor(pos + 0.5))
        i = 0 if i < 0 else n - 1 if i > n - 1 else i
        return self.ramp(key)[i]


STYLE_C = Style('C')


def local_st(g):
    """local wall coords: s along the wall, t = height"""
    P = g.P
    if g.tag in ('x+', 'x-'):
        return P[1], P[2]
    return P[0], P[2]


# =========================================================================== style pass
GLASS_KEYS = ('glass', 'glass_lt', 'factory_glass')
ROOF_KEYS = ('roof_metal', 'roof_dark', 'roof_red', 'roof_felt', 'corr')
WALL_TAGS = ('x+', 'x-', 'y+', 'y-')
GRASS_KEYS = ('grass', 'grass_lt', 'grass_dk', 'meadow')
AB_TINT = (104, 106, 96)


def _ab_key(g, key, d, st):
    """abandoned pass on one solid pixel: boards / broken glass, rust, grime"""
    if key in GLASS_KEYS and g.tag in WALL_TAGS:
        s, t = local_st(g)
        h = ihash(int(math.floor(s / 4.5)), int(math.floor(t / 9.0)), g.solid.grp, 31)
        if h < 0.55:
            return 'boards', (-0.7 if int(math.floor(t * (1.0 if st.zoom >= 1 else 0.5))) % 2 == 0 else 0.0)
        if h < 0.9:
            return 'glass_broken', 0.0
        return key, d
    if key in ROOF_KEYS:
        P = g.P
        if vnoise(P[0], P[1], 2.5, 77, 1e9) * vnoise(P[0], P[1], 6.0, 78, 1e9) > 0.42:
            return 'rust', d
        return key, d - 0.2
    if g.tag in WALL_TAGS and g.solid.cls == 'obj':
        s, t = local_st(g)
        if ihash(int(math.floor(s * 0.8)), 5, g.solid.grp) < 0.18 and t < 14 + 10 * ihash(int(s), 6):
            return key, d - 0.45
    return key, d


def _ab_ground(g, key, d, st):
    x, y = g.W[0], g.W[1]
    if key in ('gravel', 'gravel_lt', 'gravel_dk', 'asphalt', 'asph_dk', 'dirt'):
        if vnoise(x, y, 2.5, 88) > 0.58:
            return ('grass_dk' if key != 'asphalt' else 'asph_dk'), d
    if key in GRASS_KEYS:
        h = ihash(int(x * 1.5), int(y * 1.5), 89)
        if h < 0.10:
            return 'meadow', 0.3
        if h < 0.18:
            return 'grass_dk', -0.3
    return key, d


# =========================================================================== winter pass (1.1 W2, DESIGN_1_1 4.2)
# The winter pass only recolours: every pixel the summer pass draws is drawn, no other (identical alpha mask,
# so the summer piece table serves both seasons).  Ground functions in art_terrain read st.winter themselves
# where geometry matters (ploughed roads with banks, ice, fields, rail); every other summer ground key goes
# through winter_ground(); solids go through winter_solid() (up-facing -> snow, foliage, lit windows).
WINTER_KEYS = ('snow', 'ice', 'packed', 'asph_w', 'spruce_w', 'pine_w', 'twig', 'twig_lt', 'twig_gap', 'reed_w',
               'win_lit', 'wire_w')
SNOW_GROUND_D = -0.85      # flat ground snow one tone below a lit flat roof (#d7dee8 vs #e3e9ef): roofs read
SNOWY = ('snow', 'ice', 'packed')
# summer ground key -> snow tone offset (sixths of the ramp); anything not listed and not kept becomes snow 0
SNOW_GROUND = {'grass_dk': -0.35, 'grass_lt': 0.1, 'dirt': -0.3, 'dirt_dk': -0.45, 'crop_dk': -0.3,
               'plough': -0.2, 'plough_dk': -0.45, 'marsh_dk': -0.4, 'kerb': 0.3, 'slash': -0.5,
               'hay_dk': -0.3, 'stump_top': 0.0}
PACKED_GROUND = ('gravel', 'gravel_lt', 'gravel_dk', 'sawdust', 'concrete', 'concrete_dk', 'court_cinder', 'cinder',
                 'sandpile')
KEEP_GROUND = ('water', 'water_lt', 'water_dk', 'foam', 'foam_dk', 'rock_dk', 'rail', 'sleeper', 'steel', 'steel_lt',
               'buffer_red', 'trim', 'ash', 'ash_lt', 'char', 'coal', 'tyre', 'boards', 'asphalt', 'asph_dk',
               'asph_lt', 'bitumen') + WINTER_KEYS
DECIDUOUS = ('birch_leaf', 'birch_leaf_au', 'apple_leaf', 'rowan_leaf', 'bush', 'lilac', 'hedge', 'flowers')
NOSNOW_SOLID = GLASS_KEYS + ('cap', 'coal', 'foam', 'flag_blue', 'fire', 'smoke', 'water', 'water_lt', 'glass_broken',
                             'headlight', 'taillight', 'chrome') + WINTER_KEYS
WINTER_LIT_WINDOWS = True
_unknown_ground = set()


def snow_noise(x, y, st):
    """quiet tile-periodic drift texture for ground snow (sixths of the ramp)"""
    s = st.texture / 0.36 if st.texture > 0 else 0.0
    d = (vnoise(x, y, 8.0, 701) - 0.5) * 0.7 * s
    if vnoise(x, y, 4.0, 702) > 0.74:
        d += 0.45 * s
    return d


def winter_ground(g, key, d, st):
    """generic winter mapping of a summer ground key (the geometry-aware cases are done in art_terrain)"""
    if key in KEEP_GROUND:
        return key, d
    x, y = g.W[0], g.W[1]
    if key in PACKED_GROUND:
        return 'packed', (vnoise(x, y, 3.0, 703) - 0.5) * 0.8 * (st.texture / 0.36)
    if key in ('reed', 'reed_dk'):
        return 'reed_w', -0.3 if key == 'reed_dk' else 0.0
    if key in ('rock', 'lichen'):
        return 'snow', 0.0
    if key not in SNOW_GROUND and key not in PAL_SNOW_OK:
        _unknown_ground.add(key)
    return 'snow', SNOW_GROUND.get(key, 0.0) + snow_noise(x, y, st)


# summer ground keys that silently become plain snow
PAL_SNOW_OK = ('grass', 'meadow', 'flower_y', 'flower_w', 'flower_p', 'sand', 'pebble', 'crop', 'marsh', 'cotton',
               'shallow', 'bush', 'lilac', 'flowers', 'apple_leaf', 'hay', 'woodpile', 'ballast', 'ballast_dk')


def _snow_on(g, key, d, st, lift=0.0):
    """snow on an up-facing solid pixel: seams/texture mostly hidden"""
    return 'snow', d * 0.25 + lift


def winter_solid(g, key, d, st):
    if key in WINTER_KEYS:
        return key, d
    nz = g.n[2]
    P = g.P
    tg = g.tag
    if key in DECIDUOUS:
        if key == 'hedge':
            return ('snow', 0.0) if nz > 0.8 else ('twig', -0.3)
        cell = 1.4 if st.zoom >= 1 else 2.8
        h = ihash(int(math.floor(P[0] / cell)), int(math.floor(P[1] / cell)), int(math.floor(P[2] / cell)),
                  g.solid.par.get('seed', 1), 711)
        if nz > 0.62 and h < 0.22:
            return 'snow', 0.0
        if h > 0.72:                     # bare crown: light gaps between the twigs (airy, not a brown ball)
            return 'twig_gap', 0.0
        return ('twig_lt' if h < 0.45 else 'twig'), (h - 0.5) * 0.6
    if key == 'spruce':
        c = g.solid.c if isinstance(g.solid, Cone) else None
        if tg == 'cap':
            return 'snow', 0.0
        if c is not None:
            ang = math.atan2(P[1] - c[1], P[0] - c[0])
            per = 3.4 if st.zoom >= 1 else 6.8
            j = ihash(int((ang + 4) * 4.0), g.solid.par.get('seed', 0), 712) * 1.4
            band = (P[2] - c[2] + j) % per
            if P[2] > c[2] + 1.4 and band > per * 0.55 and ihash(int((ang + 4) * 7.0), int(P[2] / 1.2), 713) < 0.8:
                return 'snow', -0.3
        return 'spruce_w', d * 0.8
    if key == 'pine_leaf':
        cell = 1.5 if st.zoom >= 1 else 3.0
        h = ihash(int(math.floor(P[0] / cell)), int(math.floor(P[1] / cell)), int(math.floor(P[2] / cell)), 714)
        if nz > 0.62 and h < 0.62:
            return 'snow', -0.2
        return 'pine_w', d
    if key in GLASS_KEYS and tg in WALL_TAGS:
        if WINTER_LIT_WINDOWS and key == 'glass' and g.solid.cls == 'obj':
            s, t = local_st(g)
            if ihash(int(math.floor(s / 6.0)), int(math.floor(t / 10.0)), g.solid.grp, 715) < 0.16:
                return 'win_lit', 0.0
        return key, d
    if key in NOSNOW_SOLID or key.startswith(('car_', 'sign_', 'res_neon')):
        return key, d
    if key == 'wire':
        return 'wire_w', d
    if key in ('rock', 'lichen', 'rock_dk', 'res_granite'):
        if nz > 0.80:
            return 'snow', 0.0
        return ('rock_dk' if key == 'rock_dk' else 'rock'), d - 0.2
    if tg in WALL_TAGS:
        return key, d
    if nz > 0.5:
        return _snow_on(g, key, d, st)
    return key, d


def shade(gb, st=STYLE_C, mode='normal'):
    """G-buffer -> RGB image (key background).  mode: 'normal' | 'ab' (abandoned style pass) | 'winter' |
    'ab_winter' (1.1 W2: the same pixels recoloured for snow; never changes the alpha mask)."""
    st.zoom = gb['zoom']
    W, H, grid, md = gb['w'], gb['h'], gb['grid'], gb['model']
    if md.prepare is not None:
        md.prepare(gb)
    img = Image.new('RGB', (W, H), KEY)
    px_ = img.load()
    keys = [[None] * W for _ in range(H)]
    ab = mode in ('ab', 'ab_winter')
    winter = mode in ('winter', 'ab_winter')
    st.winter = winter
    try:
        for y in range(H):
            row = grid[y]
            for x in range(W):
                g = row[x]
                if g is None:
                    continue
                if g.kind == 'ground':
                    st.texture = st.tex_ground
                    key, d = md.ground(g, st)
                    if ab:
                        key, d = _ab_ground(g, key, d, st)
                    if winter:
                        key, d = winter_ground(g, key, d, st)
                        if key == 'snow':
                            d += SNOW_GROUND_D
                        if key in SNOWY and g.shadow:
                            d += 1.7             # ground shadows on snow: blue #bcc8d8, not the deep tone
                    st.texture = st.tex_obj
                    l = g.light - 0.10 * g.ao * 0.6
                else:
                    key, d = g.solid.mat(g, st)
                    if ab:
                        key, d = _ab_key(g, key, d, st)
                    if winter:
                        key, d = winter_solid(g, key, d, st)
                    l = g.light
                keys[y][x] = key
                snap = g.kind == 'ground' or not g.solid.curved
                px_[x, y] = st.color(key, l, d, snap)
    finally:
        st.winter = False
        st.texture = st.tex_obj
    # sel-out outlines: darkest tone of the local material mixed toward blue-black
    out = []
    thr = 3.0
    LW, LH = gb['wn'] * T, gb['wm'] * T
    edge_eps = 1.6 / gb['zoom']
    for y in range(H):
        for x in range(W):
            g = grid[y][x]
            if g is None or g.kind != 'solid' or g.solid.cls != 'obj':
                continue
            # solids that continue into the neighbour tile (bridge decks) get no outline at the lot edge
            at_edge = (g.W[0] < edge_eps or g.W[0] > LW - edge_eps or g.W[1] < edge_eps or g.W[1] > LH - edge_eps) \
                and g.W[2] < 2.0
            hitn = None
            for (dx, dy, side) in ((1, 0, 'r'), (0, 1, 'b'), (-1, 0, 'l'), (0, -1, 't')):
                xx, yy = x + dx, y + dy
                if not (0 <= xx < W and 0 <= yy < H):
                    if not at_edge:
                        hitn = side
                        break
                    continue
                n = grid[yy][xx]
                if n is None and at_edge:
                    continue
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
        r = st.ramp(keys[y][x])
        c = r[0] if side in ('r', 'b') else r[1]
        px_[x, y] = to565(lerpc(c, st.outline_col, 0.35))
    # wires
    zoom = gb['zoom']
    tw = gb['to_world']
    for (P0, P1, key, sag) in md.lines:
        rp = st.ramp(key)
        col = rp[len(rp) * 2 // 3]
        if winter and key == 'wire':             # light wires vanish against snow: dark in winter
            col = st.ramp('wire_w')[3]
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
    if ab:
        for y in range(H):
            for x in range(W):
                c = px_[x, y]
                if c == KEY:
                    continue
                if winter and keys[y][x] in SNOWY:      # the snow on an abandoned lot stays snow
                    continue
                lum = 0.3 * c[0] + 0.59 * c[1] + 0.11 * c[2]
                cc = tuple(lum + (c[i] - lum) * 0.42 for i in range(3))
                cc = lerpc(cc, AB_TINT, 0.10)
                px_[x, y] = to565(tuple(v * 0.90 for v in cc))
    return img


# =========================================================================== output helpers
def write_bmp(path, img):
    """24-bit BI_RGB bottom-up BMP with a 54-byte header"""
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


def skyline_pack(items, width=640, max_h=2048):
    """bottom-left skyline packer.  items: list of (name, w, h).  Returns list of pages, each a
    list of (name, x, y, w, h); and the page heights.  Deterministic.  M4: tries several item orders
    (height, area, perimeter, width) and keeps the one with the smallest total sheet height (area order
    saved ~470 KB of 64-zoom VRAM over height order once the Milestone-2 buildings were added)."""
    best = None
    for key in (lambda it: (-it[2], -it[1], it[0]), lambda it: (-it[1] * it[2], it[0]),
                lambda it: (-(it[1] + it[2]), it[0]), lambda it: (-it[1], -it[2], it[0])):
        pages, heights = _skyline_pack_order(sorted(items, key=key), width, max_h)
        if best is None or (len(heights), sum(heights)) < (len(best[1]), sum(best[1])):
            best = (pages, heights)
    return best


def _skyline_pack_order(order, width, max_h):
    pages = []
    heights = []
    pending = order
    while pending:
        sky = [(0, width, 0)]          # segments (x, w, y)
        placed = []
        rest = []
        for (name, w, h) in pending:
            best = None
            for i in range(len(sky)):
                x = sky[i][0]
                if x + w > width:
                    break
                # max y over segments covered by [x, x+w)
                y = 0
                j = i
                rem = w
                while rem > 0 and j < len(sky):
                    y = max(y, sky[j][2])
                    rem -= sky[j][1] if j > i else sky[j][1]
                    j += 1
                if rem > 0:
                    continue
                if y + h > max_h:
                    continue
                if best is None or (y + h, x) < (best[1] + h, best[0]):
                    best = (x, y, i)
            if best is None:
                rest.append((name, w, h))
                continue
            x, y, i = best
            placed.append((name, x, y, w, h))
            # update skyline
            nsky = []
            for (sx, sw, syy) in sky:
                ex = sx + sw
                if ex <= x or sx >= x + w:
                    nsky.append((sx, sw, syy))
                    continue
                if sx < x:
                    nsky.append((sx, x - sx, syy))
                if ex > x + w:
                    nsky.append((x + w, ex - (x + w), syy))
            nsky.append((x, w, y + h))
            nsky.sort()
            # merge equal neighbours
            merged = []
            for seg in nsky:
                if merged and merged[-1][2] == seg[2] and merged[-1][0] + merged[-1][1] == seg[0]:
                    merged[-1] = (merged[-1][0], merged[-1][1] + seg[1], seg[2])
                else:
                    merged.append(seg)
            sky = merged
        if not placed:
            raise RuntimeError('sprite too large for a page: %r' % (rest[0],))
        pages.append(placed)
        heights.append(max(p[2] + p[4] for p in placed))
        pending = rest
    return pages, heights


def diamond_inside(px, py, zoom):
    """pixel-centre rule for the 64x32 (zoom 1) / 32x16 (zoom 0.5) tile diamond"""
    tw, th = 64 * zoom, 32 * zoom
    cx, cy = px + 0.5, py + 0.5
    return abs(cx - tw / 2.0) / (tw / 2.0) + abs(cy - th / 2.0) / (th / 2.0) < 1.0
