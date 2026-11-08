"""art_terrain.py - ground materials: grass, water (shimmer frames), shores (autotile with islet
cleanup), fields, marsh, roads/rail per autotile mask, bridges, lot grounds.  (T02, KAUPUNKI)

Every ground function takes (g, st) where g.W = world point (X, Y, 0) and returns (palette key,
tone delta).  Noise is periodic per tile (vnoise period 32), so edges always match between
neighbours.  Masks: NE=1 (-Y), SE=2 (+X), SW=4 (+Y), NW=8 (-X) (common.h DIR_*)."""
import math

from artlib import T, clamp, lerp, ihash, vnoise, edge_window, Model

ARM = {1: '-y', 2: '+x', 4: '+y', 8: '-x'}


def conn_of(mask):
    return tuple(ARM[b] for b in (1, 2, 4, 8) if mask & b)


# =========================================================================== grass / water
def g_grass(g, st, var=0, key='grass'):
    x, y = g.W[0], g.W[1]
    tx = st.texture
    n1 = vnoise(x, y, 8.0, 1)
    n2 = vnoise(x, y, 4.0, 2) - 0.5
    n3 = (vnoise(x, y, 5.0, 40 + var) - 0.5) * edge_window(x, y)
    d = (n1 - 0.5) * 1.3 * tx + n2 * 0.5 * tx + n3 * 1.2 * tx
    ix, iy = int(math.floor(x)) % 32, int(math.floor(y)) % 32
    inner = 2 < ix < 29 and 2 < iy < 29
    h = ihash(ix, iy, 5 + (var if inner else 0))
    if var == 2:
        # meadow: soft lighter patches + sparse muted flowers, interior only (seams stay identical and
        # no tile-shaped diamond shows up in a field of mixed grass variants)
        if inner:
            f = ihash(ix, iy, 77)
            if f < 0.014:
                return 'flower_y', 0.3
            if f < 0.022:
                return 'flower_w', 0.2
            if f < 0.027:
                return 'flower_p', 0.2
        if vnoise(x, y, 6.0, 46) * edge_window(x, y) > 0.38:
            key = 'meadow' if key == 'grass' else key
    if h < 0.05 * tx + 0.015:
        return 'grass_lt', 0.2
    if h > 1 - (0.05 * tx + 0.01):
        return 'grass_dk', -0.2
    return key, d


def wave_d(x, y, st, frame=0):
    """ripple highlights: short screen-horizontal dashes drifting a little per shimmer frame"""
    if st.texture < 0.1:
        return 0.0
    u = (x + y) + frame * (8.0 / 3.0)
    band = (u + 3.0 * vnoise(x, y, 8.0, 51)) % 8.0
    if band < 1.0 and vnoise(x, y, 4.0, 52) > 0.62:
        return 1.0
    return 0.0


def w_ice(x, y, st):
    """1.1 W2: lake ice (THEME #CADCE6) with wind-drifted snow patches; no shimmer (every frame identical)"""
    s = st.texture / 0.36 if st.texture > 0 else 0.0
    if vnoise(x, y, 8.0, 720) * 0.75 + vnoise(x, y, 4.0, 721) * 0.25 > 0.66:
        return 'ice', 1.3 + (vnoise(x, y, 4.0, 722) - 0.5) * 0.4 * s
    return 'ice', (vnoise(x, y, 8.0, 723) - 0.5) * 0.7 * s


def g_water(g, st, frame=0):
    x, y = g.W[0], g.W[1]
    if getattr(st, 'winter', False):
        return w_ice(x, y, st)
    d = (vnoise(x, y, 8.0, 50) - 0.5) * 0.6 * st.texture
    if wave_d(x, y, st, frame):
        return 'water_lt', 0.0
    return 'water', d


# =========================================================================== rivers + rapids
RAPID_ROCKS = [(10.0, 10.0, 3.0), (21.0, 13.0, 2.4), (13.0, 21.5, 3.2), (23.5, 23.0, 2.0)]


def _river_base(x, y, st):
    """river surface: one tone darker than lake water, quiet noise"""
    return 'water', -0.75 + (vnoise(x, y, 8.0, 54) - 0.5) * 0.5 * st.texture


def g_river(g, st, axis='x', frame=0):
    """river water.  axis 'x'/'y': flow streaks along that axis, frames 0..2 advance them toward +axis
    (play 2..0 for the opposite flow); 'n': non-directional ripples for bends."""
    x, y = g.W[0], g.W[1]
    if axis == 'n':
        if wave_d(x * 1.0, y, st, frame) and vnoise(x, y, 4.0, 55) > 0.6:
            return 'water_lt', -0.5
        return _river_base(x, y, st)
    a, c = (x, y) if axis == 'x' else (y, x)          # a = along the flow, c = across
    w = 0.8 if st.zoom >= 1 else 1.7
    cc = (c + 3.0 * vnoise(x, y, 8.0, 53)) % 32.0
    band = int(cc // 8.0)
    if (cc % 8.0) < w:
        shift = frame * 16.0 / 3.0
        if ((a - shift + ihash(band, axis == 'x', 56) * 16.0) % 16.0) < 5.0:
            return 'water_lt', -0.6
    return _river_base(x, y, st)


def g_rapids(g, st, axis='x', frame=0):
    """rapids (koski): white water churning along the flow axis around granite rocks.  Foam fades out
    toward the tile edges so rapids meet plain river tiles cleanly."""
    x, y = g.W[0], g.W[1]
    for (cx, cy, r) in RAPID_ROCKS:
        dx, dy = (x - cx) / r, (y - cy) / r
        d2 = dx * dx + dy * dy
        if d2 < 1.0:
            nz = math.sqrt(1.0 - d2)
            lam = 0.30 * dx + 0.66 * dy + 0.70 * nz
            if lam > 0.85:
                return 'rock', 0.3
            if lam > 0.5:
                return 'rock_dk', 0.3
            return 'rock_dk', -0.6
        if d2 < 1.5:
            return 'foam', 0.3
    shift = frame * 32.0 / 3.0
    if axis == 'x':
        u, v = x - shift, y * 2.0
    elif axis == 'y':
        u, v = x * 2.0, y - shift
    else:
        u, v = x - shift * 0.5, y + shift * 0.5
    f = vnoise(u, v, 8.0, 57, 32.0 if axis == 'n' else 32.0) * 0.7 + vnoise(x, y, 4.0, 58 + frame) * 0.3
    ew = edge_window(x, y)
    f = f * (0.55 + 0.75 * ew)
    if f > 0.56:
        return 'foam', 0.2
    if f > 0.47:
        return 'foam_dk', 0.0
    if f > 0.40 and ihash(int(x * 1.2), int(y * 1.2), 59 + frame) < 0.5:
        return 'water_lt', -0.3
    return _river_base(x, y, st)


# =========================================================================== shores
def shore_field(x, y, corners):
    """corners = (n, e, w, s) land flags (n=(0,0), e=(32,0), w=(0,32), s=(32,32)).  Bilinear corner
    interpolation + tile-periodic noise.  Diagonal masks (e+w or n+s) get a central land bias so they
    always form one clean isthmus instead of noise-decided pinches."""
    u, v = (x % T) / T, (y % T) / T
    if x >= T:
        u = 1.0
    if y >= T:
        v = 1.0
    cn, ce, cw, cs = corners
    f = (cn * (1 - u) * (1 - v) + ce * u * (1 - v) + cw * (1 - u) * v + cs * u * v)
    if (cn and cs and not ce and not cw) or (ce and cw and not cn and not cs):
        f += 0.22 * math.sin(math.pi * u) * math.sin(math.pi * v)
    f += (vnoise(x, y, 8.0, 61) - 0.5) * 0.24 + (vnoise(x, y, 4.0, 62) - 0.5) * 0.09
    return f


def shore_prepare(corners):
    """returns a Model.prepare hook: classify every ground pixel land/water, then flip small
    connected components that do not touch the tile border (islets / ponds)."""
    def prep(gb):
        grid, W, H = gb['grid'], gb['w'], gb['h']
        land = {}
        for y in range(H):
            for x in range(W):
                g = grid[y][x]
                if g is None:
                    continue
                land[(x, y)] = shore_field(g.W[0], g.W[1], corners) > 0.5
        border = set()
        for (x, y) in land:
            for (dx, dy) in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                if (x + dx, y + dy) not in land:
                    border.add((x, y))
                    break
        seen = set()
        flips = set()
        limit = 90 * gb['zoom'] * gb['zoom'] + 6
        for p in land:
            if p in seen:
                continue
            val = land[p]
            comp = [p]
            seen.add(p)
            i = 0
            touches = False
            while i < len(comp):
                q = comp[i]
                i += 1
                if q in border:
                    touches = True
                for (dx, dy) in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                    r = (q[0] + dx, q[1] + dy)
                    if r in land and r not in seen and land[r] == val:
                        seen.add(r)
                        comp.append(r)
            if not touches and len(comp) <= limit:
                flips.update(comp)
        gb['model'].flips = flips
    return prep


def g_shore(g, st, corners=(1, 0, 0, 0), frame=0, md=None):
    x, y = g.W[0], g.W[1]
    f = shore_field(x, y, corners)
    flipped = md is not None and (g.px, g.py) in getattr(md, 'flips', ())
    if getattr(st, 'winter', False):                     # 1.1 W2: snow on land, dry reeds standing in the shore ice
        if flipped:
            return ('snow', 0.0) if f <= 0.5 else ('ice', 0.6)
        if f > 0.5:
            return ('snow', -0.35 if f < 0.60 else 0.0)
        if f > 0.445:
            if ihash(int(x * 0.9), int(y * 0.9), 65) < 0.16:
                return 'reed_w', 0.0
            return 'ice', 0.7
        if f > 0.40 and st.texture > 0.2:
            if ihash(int(x * 0.8), int(y * 0.8), 66) < 0.10:
                return 'reed_w', -0.4
            return 'ice', 0.35
        return w_ice(x, y, st)
    if flipped:
        return ('sand', 0.0) if f <= 0.5 else ('shallow', 0.3)
    if f > 0.5:
        if f < 0.555:
            if st.texture > 0.3 and ihash(int(x * 1.3), int(y * 1.3), 63) < 0.25:
                return 'pebble', 0.0
            return 'sand', (vnoise(x, y, 3.0, 64) - 0.5) * 0.6 * st.texture
        if f < 0.60:
            return 'grass_dk', 0.0
        return g_grass(g, st)
    if f > 0.445:
        # reeds in the shallows: short vertical olive dashes
        if ihash(int(x * 0.9), int(y * 0.9), 65) < 0.16:
            return 'reed', 0.0
        return 'shallow', 0.3
    if f > 0.40 and st.texture > 0.2:
        if ihash(int(x * 0.8), int(y * 0.8), 66) < 0.10:
            return 'reed_dk', 0.0
        return 'water_lt', -0.2
    return g_water(g, st, frame)


def shore_model(mask, frame=0):
    c = (mask & 1, (mask >> 1) & 1, (mask >> 2) & 1, (mask >> 3) & 1)
    md = Model('shore%02d' % mask)
    md.ground = lambda g, st: g_shore(g, st, c, frame, md)
    md.prepare = shore_prepare(c)
    return md


# =========================================================================== fields / marsh
def g_field(g, st, kind='crop'):
    x, y = g.W[0], g.W[1]
    tx = st.texture
    key, dk = ('crop', 'crop_dk') if kind == 'crop' else ('plough', 'plough_dk')
    m = 1.0 if st.zoom >= 1 else 2.0
    yy = y % T
    if getattr(st, 'winter', False):                     # 1.1 W2: white field, frozen ditch, furrows/stubble rows faintly showing
        if abs(yy - 16.0) < 0.8 * m:
            return 'ice', -1.2
        if abs(yy - 16.0) < 2.0 * m:
            return 'snow', -0.5
        if (y % (3.0 * m)) < 1.0 * m:
            return 'snow', (-0.9 if kind != 'crop' else -0.6)
        return 'snow', (vnoise(x, y, 6.0, 724) - 0.5) * 0.5
    # open ditch (sarkaoja) along the middle of every tile: long parallel ditches across a field
    if abs(yy - 16.0) < 0.8 * m:
        return 'water_dk', 0.0
    if abs(yy - 16.0) < 2.0 * m:
        return 'grass_dk', (vnoise(x, y, 2.0, 32) - 0.5) * 0.8 * tx
    if (y % (3.0 * m)) < 1.0 * m:
        return dk, 0.0
    d = (vnoise(x, y, 4.0, 31) - 0.5) * 0.9 * tx
    if kind != 'crop' and ihash(int(x) % 32, int(y) % 32, 33) < 0.04 * tx:
        return dk, -0.3
    return key, d


def g_marsh(g, st):
    x, y = g.W[0], g.W[1]
    n = vnoise(x, y, 6.0, 91)
    if getattr(st, 'winter', False):                     # 1.1 W2: frozen pools, dry sedge tussocks poking through the snow
        if n < 0.30 * edge_window(x, y) and edge_window(x, y) > 0.3:
            return 'ice', 0.2
        if vnoise(x, y, 3.0, 93) > 0.62:
            ix, iy = int(math.floor(x)) % 32, int(math.floor(y)) % 32
            return ('reed_w', -0.2) if ihash(ix, iy, 725) < 0.45 else ('snow', -0.5)
        return 'snow', (vnoise(x, y, 4.0, 94) - 0.5) * 0.6
    if n < 0.30 * edge_window(x, y) + 0.0 and edge_window(x, y) > 0.3:
        return 'water_dk', 0.2
    ix, iy = int(math.floor(x)) % 32, int(math.floor(y)) % 32
    h = ihash(ix, iy, 92)
    if h < 0.03 and 2 < ix < 29 and 2 < iy < 29:
        return 'cotton', 0.2
    if vnoise(x, y, 3.0, 93) > 0.62:
        return 'marsh_dk', 0.0
    return 'marsh', (vnoise(x, y, 4.0, 94) - 0.5) * 0.8 * st.texture


# =========================================================================== road geometry
def _arm_dist(x, y, e, c=16.0):
    """distance to the centre line of one arm (edge midpoint -> tile centre), rounded at the centre"""
    if e in ('-x', '+x'):
        if (e == '-x' and x <= c) or (e == '+x' and x >= c):
            return abs(y - c)
        return math.hypot(x - c, y - c)
    if (e == '-y' and y <= c) or (e == '+y' and y >= c):
        return abs(x - c)
    return math.hypot(x - c, y - c)


def is_straight(conn):
    return len(conn) == 2 and (('-x' in conn and '+x' in conn) or ('-y' in conn and '+y' in conn))


def is_curve(conn):
    return len(conn) == 2 and not is_straight(conn)


def road_geom(x, y, conn, bulb=0.0):
    """-> (d, along, arm) : d = distance from the road centre line (smooth union at junctions),
    along = coordinate along the centre line (absolute, tile periodic), arm = which arm it lies on
    (None in a junction core or isolated piece)."""
    c = 16.0
    n = len(conn)
    if n == 0:
        return max(0.0, math.hypot(x - c, y - c) - 1.0), 0.0, None
    if is_curve(conn):
        cx = 0.0 if '-x' in conn else 32.0
        cy = 0.0 if '-y' in conn else 32.0
        r = math.hypot(x - cx, y - cy)
        ang = math.atan2(abs(y - cy), abs(x - cx))      # 0 .. pi/2
        # along runs from the X-edge end (ang = pi/2 at the x edge?) -> just use arc length
        return abs(r - 16.0), ang * 16.0, 'arc'
    ds = []
    for e in conn:
        ds.append((_arm_dist(x, y, e), e))
    ds.sort()
    if n == 1:
        d = ds[0][0]
        if bulb > 0:      # turning bulb: radius = half width + bulb
            d = min(d, max(0.0, math.hypot(x - c, y - c) - bulb))
        e = ds[0][1]
        along = x if e in ('-x', '+x') else y
        return d, along, e
    if n == 2:  # straight
        e = ds[0][1]
        along = x if e in ('-x', '+x') else y
        return ds[0][0], along, e
    a, b = ds[0][0], ds[1][0]
    k = 3.0       # smooth union -> filleted corners at T / crossings
    h = clamp(0.5 + 0.5 * (b - a) / k)
    d = lerp(b, a, h) - k * h * (1 - h)
    e = ds[0][1]
    along = x if e in ('-x', '+x') else y
    return d, along, e


def _dash(along, period, on):
    """centred dash pattern: a dash is centred on along = 0 (so every tile edge carries a half dash
    on each side and the pattern continues whichever direction the neighbour runs)"""
    return ((along + on / 2.0) % period) < on


def _core_dist(x, y, arm):
    """distance from the tile centre along an arm"""
    if arm in ('-x', '+x'):
        return abs(x - 16.0)
    return abs(y - 16.0)


ARC_LEN = 8.0 * math.pi      # quarter circle of radius 16


def road_ground(kind, mask):
    """ground function for road class `kind` ('grav', 'road', 'main', 'mway') and mask"""
    conn = conn_of(mask)

    def fn(g, st):
        return g_road(g, st, conn, kind)
    return fn


def w_bank(d, e0, e1, x, y):
    """ploughed snow bank between road-centre distances e0 (road side) and e1 (verge side): a blue-shadowed
    inner foot, a bright crest, then the verge snow"""
    u = (d - e0) / (e1 - e0)
    if u < 0.22:
        return 'snow', -1.6
    if u < 0.65:
        return 'snow', 1.0
    return 'snow', 0.4 + (vnoise(x, y, 2.0, 726) - 0.5) * 0.5


def w_road(x, y, st, kind, d, hw, mid, grass):
    """1.1 W2 winter road surface by distance from the centre line: ploughed asphalt (markings hidden, a
    slush ridge between the lanes, packed snow along the edges) or packed snow with ruts on gravel roads,
    snow banks along both sides (geometry-based, so every autotile piece meets its neighbours)."""
    tx = st.texture / 0.36 if st.texture > 0 else 0.0
    bank = {'grav': (hw - 0.6, hw + 1.6), 'road': (hw - 1.2, hw + 1.6), 'main': (hw - 1.4, hw + 2.4),
            'mway': (hw - 1.3, hw + 1.6)}[kind]
    if d >= bank[1]:
        return grass()
    if d >= bank[0]:
        return w_bank(d, bank[0], bank[1], x, y)
    if kind == 'grav':
        if 3.0 < d < 4.6:
            return 'packed', -0.9
        if d < 0.9:
            return 'packed', 0.4
        return 'packed', (vnoise(x, y, 2.0, 74) - 0.5) * 0.7 * tx
    if kind == 'mway' and mid and d < 2.4:
        if d < (0.6 if st.zoom >= 1 else 1.0) * 0.9:
            return 'steel_lt', 0.4
        return 'snow', 0.3
    if d > bank[0] - 1.2 or (kind != 'mway' and d < (1.0 if st.zoom >= 1 else 1.4)):
        return 'packed', -0.4 + (vnoise(x, y, 2.0, 727) - 0.5) * 0.6 * tx
    if vnoise(x, y, 3.0, 728) > 0.84:
        return 'asph_w', 0.7
    return 'asph_w', (vnoise(x, y, 3.0, 71) - 0.5) * 0.6 * tx


def g_road(g, st, conn, kind, base=None):
    x, y = g.W[0], g.W[1]
    tx = st.texture
    z1 = st.zoom >= 1
    straight = is_straight(conn)
    curve = is_curve(conn)
    n = len(conn)
    winter = getattr(st, 'winter', False)

    def grass():
        return base(g, st) if base else g_grass(g, st)

    if kind == 'grav':
        hw = 6.5
        d, along, arm = road_geom(x, y, conn, bulb=1.0 if n == 1 else 0.0)
        if winter:
            return w_road(x, y, st, kind, d, hw, False, grass)
        if d < hw:
            if d < 0.9 and tx > 0.3 and n <= 2:
                return 'gravel_dk', 0.3
            if 3.2 < d < 4.6 and n <= 2:
                return 'gravel_dk', 0.0
            nz = (vnoise(x, y, 2.0, 74) - 0.5) * 0.9 * tx
            if tx > 0.3 and ihash(int(x * 1.4), int(y * 1.4), 75) < 0.08:
                return 'gravel_lt', 0.0
            return 'gravel', nz
        if d < hw + 0.8:
            return 'grass_dk', 0.0
        return grass()
    if kind == 'road':
        hw = 7.0
        d, along, arm = road_geom(x, y, conn, bulb=2.5 if n == 1 else 0.0)
        if winter:
            return w_road(x, y, st, kind, d, hw, False, grass)
        if d < hw:
            if d < 0.6 and z1 and n >= 1:
                if curve:
                    if _dash(along, ARC_LEN / 3.0, ARC_LEN / 6.0):
                        return 'roadline', 0.0
                elif arm is not None and (straight or _core_dist(x, y, arm) > hw + 2.0):
                    if n != 1 or _core_dist(x, y, arm) > hw + 4.0:
                        if _dash(along, 8.0, 4.0):
                            return 'roadline', 0.0
            nz = (vnoise(x, y, 3.0, 71) - 0.5) * 0.7 * tx
            if tx > 0.5 and vnoise(x, y, 4.0, 72) > 0.86:
                return 'asph_dk', 0.0
            if d > hw - 1.0:
                return 'asph_dk', 0.2
            return 'asphalt', nz
        if d < hw + 1.3:
            return 'gravel', (vnoise(x, y, 2.0, 73) - 0.5) * 0.8 * tx
        return grass()
    if kind == 'main':
        hw = 9.5
        d, along, arm = road_geom(x, y, conn, bulb=2.0 if n == 1 else 0.0)
        if winter:
            return w_road(x, y, st, kind, d, hw, False, grass)
        lw = 0.6 if z1 else 1.0
        if d < hw:
            if hw - 1.9 <= d < hw - 1.9 + lw * 1.4:
                return 'roadwhite', 0.0
            if d < lw and n >= 1:
                if curve:
                    if _dash(along, ARC_LEN / 3.0, ARC_LEN / 4.5):
                        return 'roadyel', 0.0
                elif arm is not None and (straight or _core_dist(x, y, arm) > hw + 2.0):
                    if n != 1 or _core_dist(x, y, arm) > hw + 4.0:
                        if _dash(along, 8.0, 5.0):
                            return 'roadyel', 0.0
            nz = (vnoise(x, y, 3.0, 71) - 0.5) * 0.7 * tx
            if tx > 0.5 and vnoise(x, y, 4.0, 72) > 0.88:
                return 'asph_dk', 0.0
            if d > hw - 0.8:
                return 'asph_dk', 0.2
            return 'asph_lt' if vnoise(x, y, 6.0, 78) > 0.7 else 'asphalt', nz * 0.6
        if d < hw + 1.2:
            return 'kerb', 0.0
        if d < hw + 2.4:
            return 'gravel', (vnoise(x, y, 2.0, 73) - 0.5) * 0.8 * tx
        return grass()
    if kind == 'mway':
        hw = 12.5
        d, along, arm = road_geom(x, y, conn, bulb=1.0 if n == 1 else 0.0)
        lw = 0.6 if z1 else 1.0
        # markings only on the arms outside the junction core (so arms always match straight pieces)
        core = n == 0 or (n != 2 and (arm is None or _core_dist(x, y, arm) <= hw + 0.5))
        if curve:
            core = False
        if winter:
            return w_road(x, y, st, kind, d, hw, not core, grass)
        if d < hw:
            if not core and d < 2.4:
                if d < lw * 0.9:
                    return 'steel_lt', 0.4          # crash barrier in the median
                return 'grass_dk', (vnoise(x, y, 2.0, 79) - 0.5) * 0.6 * tx
            if not core and 2.4 <= d < 2.4 + lw * 1.3:
                return 'roadwhite', 0.0
            if hw - 1.6 <= d < hw - 1.6 + lw * 1.3:
                return 'roadwhite', 0.0
            if not core and abs(d - 7.4) < lw * 0.7 and z1:
                per = 8.0 if not curve else ARC_LEN / 3.0
                if _dash(along, per, per / 2.0):
                    return 'roadwhite', -0.3
            nz = (vnoise(x, y, 3.0, 71) - 0.5) * 0.6 * tx
            if d > hw - 0.7:
                return 'asph_dk', 0.2
            return 'asphalt', nz
        if d < hw + 1.6:
            return 'gravel', (vnoise(x, y, 2.0, 73) - 0.5) * 0.8 * tx
        return grass()
    raise ValueError(kind)


# =========================================================================== railway
def rail_tracks(conn):
    """list of tracks: ('line', axis) edge to edge, ('arc', cx, cy), ('stub', edge), ('short',)"""
    n = len(conn)
    if n == 0:
        return [('short',)]
    if n == 1:
        return [('stub', conn[0])]
    if is_straight(conn):
        return [('line', 'x' if '-x' in conn else 'y')]
    if is_curve(conn):
        return [('arc', 0.0 if '-x' in conn else 32.0, 0.0 if '-y' in conn else 32.0)]
    tr = []
    if '-x' in conn and '+x' in conn:
        tr.append(('line', 'x'))
        other = [e for e in conn if e in ('-y', '+y')]
        for e in other:                                    # turnout toward -x
            tr.append(('arc', 0.0, 0.0 if e == '-y' else 32.0))
    if '-y' in conn and '+y' in conn:
        tr.append(('line', 'y'))
        if not ('-x' in conn and '+x' in conn):
            for e in [e for e in conn if e in ('-x', '+x')]:
                tr.append(('arc', 0.0 if e == '-x' else 32.0, 0.0))
    return tr


def _track_geom(tr, x, y):
    """-> (d, along, endcap) for one track"""
    c = 16.0
    if tr[0] == 'line':
        if tr[1] == 'x':
            return abs(y - c), x, False
        return abs(x - c), y, False
    if tr[0] == 'arc':
        cx, cy = tr[1], tr[2]
        r = math.hypot(x - cx, y - cy)
        ang = math.atan2(abs(y - cy), abs(x - cx))
        if not (0 <= x <= 32 and 0 <= y <= 32):
            return 99.0, 0.0, False
        return abs(r - 16.0), ang * 16.0, False
    if tr[0] == 'stub':
        e = tr[1]
        if e in ('-x', '+x'):
            inside = (x <= c + 3) if e == '-x' else (x >= c - 3)
            return (abs(y - c) if inside else 99.0), x, False
        inside = (y <= c + 3) if e == '-y' else (y >= c - 3)
        return (abs(x - c) if inside else 99.0), y, False
    # short isolated piece along X
    return (abs(y - c) if 6.0 <= x <= 26.0 else 99.0), x, False


def g_rail(g, st, conn, base=None):
    x, y = g.W[0], g.W[1]
    z1 = st.zoom >= 1
    tx = st.texture
    tracks = rail_tracks(conn)
    best = 0
    out = None
    rw = 0.55 if z1 else 1.0
    winter = getattr(st, 'winter', False)
    for tr in tracks:
        d, along, _ = _track_geom(tr, x, y)
        if tr[0] == 'arc':
            sp = ARC_LEN / 8.0
        else:
            sp = 3.2
        if winter:                    # 1.1 W2: dark rails on snow, sleepers as faint dips, a snow-covered bed
            if abs(d - 3.1) < rw:
                pr, res = 4, ('steel', 0.4 if d < 3.1 else -0.6)
            elif d < 2.5:             # the bed between the rails, blown clear: grey ballast and grit
                pr, res = 3, ('packed', -0.6 if _dash(along, sp, 1.4 if z1 else 1.6) else -0.1)
            elif d < 5.4 and _dash(along, sp, 1.4 if z1 else 1.6):
                pr, res = 3, ('snow', -1.1)
            elif d < 7.2:
                pr, res = 2, ('snow', -0.3)
            elif d < 8.6:
                pr, res = 1, ('snow', 0.3)
            else:
                continue
        elif abs(d - 3.1) < rw:
            pr, res = 4, ('rail', 0.6 if d < 3.1 else -0.2)
        elif d < 5.4 and _dash(along, sp, 1.4 if z1 else 1.6):
            pr, res = 3, ('sleeper', 0.0)
        elif d < 7.2:
            pr, res = 2, ('ballast', (vnoise(x, y, 1.5, 81) - 0.5) * 1.0 * tx)
        elif d < 8.6:
            pr, res = 1, ('ballast_dk', 0.0)
        else:
            continue
        if pr > best:
            best, out = pr, res
    # buffer stops at stub ends / isolated piece ends
    for tr in tracks:
        if tr[0] == 'stub':
            e = tr[1]
            if e in ('-x', '+x'):
                bx = 16.0 + (2.0 if e == '-x' else -2.0)
                if abs(x - bx) < 1.6 and abs(y - 16.0) < 4.2:
                    return ('buffer_red', 0.3) if int((y - 11.8) / 1.4) % 2 == 0 else ('trim', 0.0)
            else:
                by = 16.0 + (2.0 if e == '-y' else -2.0)
                if abs(y - by) < 1.6 and abs(x - 16.0) < 4.2:
                    return ('buffer_red', 0.3) if int((x - 11.8) / 1.4) % 2 == 0 else ('trim', 0.0)
        if tr[0] == 'short':
            for bx in (7.0, 25.0):
                if abs(x - bx) < 1.4 and abs(y - 16.0) < 4.2:
                    return ('buffer_red', 0.3) if int((y - 11.8) / 1.4) % 2 == 0 else ('trim', 0.0)
    if out is None:
        return base(g, st) if base else g_grass(g, st)
    return out


def rail_ground(mask):
    conn = conn_of(mask)
    return lambda g, st: g_rail(g, st, conn)


# =========================================================================== lot grounds
def g_lot(g, st, paths=(), gravel=(), base='grass', var=0):
    """lot ground: grass + rectangles (canonical x0,y0,x1,y1[,key]) of gravel / asphalt / dirt"""
    P = g.P
    winter = getattr(st, 'winter', False)
    for r in paths:
        if r[0] <= P[0] < r[2] and r[1] <= P[1] < r[3]:
            if winter:                # 1.1 W2: shovelled paths = packed snow
                return 'packed', 0.3 + (vnoise(g.W[0], g.W[1], 2.0, 76) - 0.5) * 0.6
            return 'gravel', (vnoise(g.W[0], g.W[1], 2.0, 76) - 0.5) * 0.8 * st.texture
    for r in gravel:
        if r[0] <= P[0] < r[2] and r[1] <= P[1] < r[3]:
            key = r[4] if len(r) > 4 else 'gravel'
            if winter and key == 'asphalt':   # ploughed car park / yard, snow piled along its edges
                e = min(P[0] - r[0], r[2] - P[0], P[1] - r[1], r[3] - P[1])
                if e < 1.8:
                    return w_bank(1.8 - e, 0.0, 1.8, g.W[0], g.W[1]) if e > 0.4 else ('snow', 0.6)
                if vnoise(g.W[0], g.W[1], 3.0, 729) > 0.80:
                    return 'packed', -0.6
                return 'asph_w', (vnoise(g.W[0], g.W[1], 2.5, 77) - 0.5) * 0.6
            if winter and key in ('gravel', 'dirt', 'sawdust', 'concrete', 'court_cinder'):
                return 'packed', (vnoise(g.W[0], g.W[1], 2.5, 77) - 0.5) * 0.7
            if key == 'potato':      # vegetable patch: dark soil rows with green tops
                k2 = 'plough' if int(P[1] * (1.0 if st.zoom >= 1 else 0.5)) % 2 == 0 else 'apple_leaf'
                return k2, 0.0
            if key == 'flowerbed':
                h = ihash(int(P[0] * 1.2), int(P[1] * 1.2), 95)
                return ('flower_y' if h < 0.3 else 'lilac' if h < 0.5 else 'flowers' if h < 0.65 else 'bush'), 0.2
            if key == 'water':
                return g_water(g, st, 0)
            if key == 'lawn':
                return g_grass(g, st, var, 'grass_lt')
            return key, (vnoise(g.W[0], g.W[1], 2.5, 77) - 0.5) * 0.7 * st.texture
    if base == 'dirt' and winter:
        return 'packed', (vnoise(g.W[0], g.W[1], 3.0, 96) - 0.5) * 0.7
    if base == 'dirt':
        n = vnoise(g.W[0], g.W[1], 3.0, 96)
        if n > 0.66:
            return 'dirt_dk', 0.0
        return 'dirt', (n - 0.5) * 0.8 * st.texture
    return g_grass(g, st, var)
