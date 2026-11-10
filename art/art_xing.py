"""art_xing.py - Project Lahio 1.2 RC2 task X2: things that cross the railway.  Loaded by gen_art.py like the
art_m2_*.py modules (extra_jobs, NAME_ALIASES, REQUIRED, XING_GROUNDS); iterate with `python3 art/xing_preview.py`.

Names (ART.md 2; MM = the axis of the FIRST key in the name, as for <key>_bridge_MM: 05 = along Y (NE-SW),
10 = along X (NW-SE)):
  grav_xing_MM, road_xing_MM, main_xing_MM   level crossing of that road class over a straight rail; MM = the ROAD's
                                             axis (road_xing_05 = street along Y, track along X)
  rail_xing_MM                               MM = the RAIL's axis; header alias of road_xing_<other MM>
  mway_rail_MM                               motorway on a flush concrete overpass deck over the rail; MM = the
                                             MOTORWAY's axis (the track runs under it along the other axis)
All are 'lot'-kind sprites like the bridges: full tile width, taller than the tile (signs, railings), anchor = the
tile's south vertex, their own ground covers the whole diamond (draw them in the object pass like a bridge).

A power line over a rail tile uses the existing wire_MM (no pole, render.cpp draws wire_ on every road-class tile,
rail included): checked in preview/xing_scene_*.png, no rail_power sprite is needed.

Seams: the ground functions are the land road and rail functions themselves, combined by distance, so every tile
edge is pixel-for-pixel the road (road edges) or the rail (rail edges); gen_art --check runs the edge-match test
on XING_GROUNDS against the road/rail autotiles."""
from art_m2_common import *  # noqa: F401,F403
from art_terrain import road_geom, rail_tracks  # noqa: F401
import art_terrain as TR

C = 16.0
# road class geometry (art_terrain.g_road): paved half width, outer edge of the verge, winter plough inner edge
ROADW = {'grav': (6.5, 7.3, 5.9), 'road': (7.0, 8.3, 5.8), 'main': (9.5, 11.9, 8.1)}
RW = (0.55, 1.0)      # rail half width (z64, z32), as g_rail


def _axes(raxis):
    """road conn, rail conn for a road along raxis"""
    if raxis == 'x':
        return ('-x', '+x'), ('-y', '+y')
    return ('-y', '+y'), ('-x', '+x')


def g_xing(g, st, rkey, raxis):
    """level crossing ground: road `rkey` along raxis over a straight rail along the other axis"""
    x, y = g.W[0], g.W[1]
    rconn, tconn = _axes(raxis)
    d_road = abs(y - C) if raxis == 'x' else abs(x - C)     # distance from the road centre line
    d_rail = abs(x - C) if raxis == 'x' else abs(y - C)     # distance from the track centre line
    along_r = y if raxis == 'x' else x                      # coordinate along the track
    hw, verge, wbank = ROADW[rkey]
    z1 = st.zoom >= 1
    rw = RW[0] if z1 else RW[1]
    winter = getattr(st, 'winter', False)
    on_rail = abs(d_rail - 3.1) < rw
    if winter:
        if d_road < wbank:                                 # ploughed carriageway: dark rails, packed snow, grit
            if on_rail:
                return 'steel', 0.4 if d_rail < 3.1 else -0.6
            if d_rail < 4.9:
                return 'packed', (-0.9 if (abs(d_rail - 3.1) < rw + 0.8) else -0.3)
            return TR.g_road(g, st, rconn, rkey)
        if d_rail < 8.6:
            return TR.g_rail(g, st, tconn)
        return TR.g_road(g, st, rconn, rkey)
    if d_road < hw:
        if on_rail:
            return 'rail', 0.6 if d_rail < 3.1 else -0.2
        if d_rail < 3.1 + rw + (1.0 if z1 else 1.4):       # flangeway groove just inside / outside each rail
            if abs(d_rail - 3.1) < rw + (0.75 if z1 else 1.2):
                return 'sleeper', -0.6
        if d_rail < 5.0:                                   # timber crossing planks (along the track)
            k = int(math.floor((d_rail + 0.4) / 1.25))
            seam = ((d_rail + 0.4) % 1.25) < (0.3 if z1 else 0.0)
            if seam and z1:
                return 'sleeper', -0.4
            return 'boards', (0.25 if k % 2 else -0.05) + (vnoise(x, y, 2.0, 931) - 0.5) * 0.5 * st.texture
        if d_rail < 5.6:                                   # asphalt patch against the planks
            return 'asph_dk', 0.0
        k, d = TR.g_road(g, st, rconn, rkey)
        if k in ('roadline', 'roadyel') and d_rail < 8.0:  # no centre line on the crossing
            return 'asphalt', 0.0
        return k, d
    if d_rail < 8.6:
        return TR.g_rail(g, st, tconn)
    return TR.g_road(g, st, rconn, rkey)


# Finnish level-crossing sign (St Andrew's cross, white with a red border), camera-facing pixel stamp on a post
CROSS = {64: ['RR...RR',
              '.WW.WW.',
              '..RWR..',
              '.WW.WW.',
              'RR...RR'],
         32: ['...',          # 32 zoom: a red X on a stamped post (a 0.7-unit post box is thinner than half a
              '...',          # pixel there and the ray caster may miss it); centre row = the cross centre
              '...',
              '...',
              'R.R',
              '.R.',
              'R.R',
              '.P.',
              '.P.',
              '.P.',
              '.P.']}
CROSS_COLS = {'R': ('sign_red', 0.95), 'W': ('trim', 1.0), 'P': ('steel', 0.75)}


def xing_model(rkey, raxis):
    md = Model('%s_xing' % rkey)
    md.ground = lambda g, st: g_xing(g, st, rkey, raxis)
    hw, verge, wbank = ROADW[rkey]
    dr = 7.6                         # sign distance from the track centre (at the ballast edge)
    dq = verge + 1.4                 # ... and from the road centre (beyond the verge)
    # right-hand traffic: the sign stands on the right of the road, before the track, for each direction
    if raxis == 'x':
        spots = ((C - dr, C + dq), (C + dr, C - dq))
    else:
        spots = ((C - dq, C - dr), (C + dq, C + dr))
    H = 9.0
    md.stamps = []
    for (sx, sy) in spots:
        md.add(box(sx - 0.35, sy - 0.35, 0.0, sx + 0.35, sy + 0.35, H, m_flat('steel_lt', -0.2), detail=True))
        md.stamps.append(((sx, sy, H + 0.6), CROSS, CROSS_COLS))
    md.top = 16.0
    return md


# =========================================================================== motorway over the rail
def _wedge(axis, t0, t1, c0, c1, zb, za0, za1, mat, th=None, **par):
    """solid along the motorway `axis` from t0 to t1, across c0..c1, bottom z = zb (or, with th, a bottom parallel
    to the top th px lower), top rising linearly from za0 at t0 to za1 at t1 (ramps, slab, railing top rails)"""
    s = (za1 - za0) / float(t1 - t0)
    if axis == 'x':
        bot = (s, 0, -1, -(za0 - th - s * t0), 'bot') if th else (0, 0, -1, -zb, 'bot')
        pl = [(-1, 0, 0, -t0, 'x-'), (1, 0, 0, t1, 'x+'), (0, -1, 0, -c0, 'y-'), (0, 1, 0, c1, 'y+'),
              (-s, 0, 1, za0 - s * t0, 'top'), bot]
        zlo = min(za0, za1) - th if th else zb
        bb = (t0, c0, zlo, t1, c1, max(za0, za1))
    else:
        bot = (0, s, -1, -(za0 - th - s * t0), 'bot') if th else (0, 0, -1, -zb, 'bot')
        pl = [(-1, 0, 0, -c0, 'x-'), (1, 0, 0, c1, 'x+'), (0, -1, 0, -t0, 'y-'), (0, 1, 0, t1, 'y+'),
              (0, -s, 1, za0 - s * t0, 'top'), bot]
        zlo = min(za0, za1) - th if th else zb
        bb = (c0, t0, zlo, c1, t1, max(za0, za1))
    return Convex(pl, bb, mat, **par)


def mway_rail_model(axis):
    """motorway along `axis` (world) on a short concrete overpass over the rail, which runs under it along the other
    axis.  The carriageway climbs H px on two short ramps (retaining walls) to a slab over the track and comes back
    to ground level at both tile edges, so it is pixel-identical to mway_05/10 there; the track is seen through
    the opening under the slab (shadowed) and on both sides of the deck."""
    md = Model('mway_rail')
    rconn, tconn = (('-x', '+x'), ('-y', '+y')) if axis == 'x' else (('-y', '+y'), ('-x', '+x'))
    hw = 13.2                       # deck half width: carriageway (12.5) + edge beam
    H, TH = 6.0, 1.6                # slab top above the ground, slab thickness (px)
    t1, t2 = 7.5, 24.5              # plateau (the span over the ballast); ramps 0..t1 and t2..32
    deck = []

    def ground(g, st):
        if g.tag == 'deck':                     # deck top (converted in prepare): the land motorway surface
            across = g.W[1] if axis == 'x' else g.W[0]
            if abs(across - C) > 12.5:
                return 'concrete', 0.4
            return TR.g_road(g, st, rconn, 'mway')
        return TR.g_rail(g, st, tconn)
    md.ground = ground

    def prepare(gb):
        # deck tops are shaded as ground (ground texture and winter rules, the slope's own light), so the
        # carriageway matches the land motorway; side faces stay solids (outlines, walls)
        lf = None
        for row in gb['grid']:
            for gp in row:
                if gp is not None and gp.kind == 'ground' and not gp.shadow and lf is None:
                    lf = gp.light                       # light of unshadowed flat ground
        for row in gb['grid']:
            for gp in row:
                if gp is not None and gp.kind == 'solid' and gp.tag == 'top' and gp.solid in deck:
                    gp.kind, gp.solid, gp.tag, gp.cls, gp.ao = 'ground', None, 'deck', 'ground', 0.0
                    if lf is not None and not gp.shadow:
                        gp.light = lf + 0.45 * (gp.light - lf)   # soften the ramps' light: a gentle hump
                elif gp is not None and gp.kind == 'ground' and gp.shadow and lf is not None:
                    gp.light = max(gp.light, 0.62 * lf)          # the track under the slab: shade, not black
    md.prepare = prepare

    def m_wall(g, st):
        P = g.P
        t = P[0] if axis == 'x' else P[1]
        if g.tag == 'bot':
            return 'concrete_dk', -1.0
        if P[2] > H - TH - 0.05 or g.solid.par.get('slab'):
            return 'concrete', 0.3                         # edge beam
        if st.zoom >= 1 and (t % 3.5) < 0.45:
            return 'concrete_dk', -0.4                     # retaining wall formwork joints
        return 'concrete_dk', 0.2

    lo, hi = C - hw, C + hw
    deck.append(_wedge(axis, 0.0, t1, lo, hi, -1.0, 0.0, H, m_wall))
    deck.append(_wedge(axis, t1, t2, lo, hi, H - TH, H, H, m_wall, slab=True))
    deck.append(_wedge(axis, t2, 32.0, lo, hi, -1.0, H, 0.0, m_wall))
    md.add(deck)
    # railings: steel posts on the edge beams + a top rail following the deck profile
    RH = 3.0

    def ztop(t):
        if t < t1:
            return H * t / t1
        if t > t2:
            return H * (32.0 - t) / (32.0 - t2)
        return H
    for side in (-1, 1):
        q = C + side * (hw - 0.45)
        for p in (1.0, t1, C, t2, 31.0):
            x0, y0 = (p, q) if axis == 'x' else (q, p)
            z0 = ztop(p)
            md.add(box(x0 - 0.3, y0 - 0.3, z0, x0 + 0.3, y0 + 0.3, z0 + RH, m_flat('steel_lt'), detail=True,
                       noshadow=True))
        for (a, b) in ((0.0, t1), (t1, t2), (t2, 32.0)):
            md.add(_wedge(axis, a, b, q - 0.35, q + 0.35, None, ztop(a) + RH, ztop(b) + RH,
                          m_flat('steel_lt', 0.3), th=0.7, detail=True, noshadow=True))
    md.top = H + RH + 2.0
    return md



# =========================================================================== gen_art hooks
XING_KEYS = ('grav', 'road', 'main')


def extra_jobs():
    J = []
    for k in XING_KEYS:
        for (ax, mm) in (('y', 5), ('x', 10)):
            J.append(((lambda kk, aa: lambda: xing_model(kk, aa))(k, ax), 0, 'lot', ['%s_xing_%02d' % (k, mm)]))
    for (ax, mm) in (('y', 5), ('x', 10)):
        J.append(((lambda aa: lambda: mway_rail_model(aa))(ax), 0, 'lot', ['mway_rail_%02d' % mm]))
    return J


# rail_xing_MM (MM = rail axis) = the street crossing with the road along the other axis
NAME_ALIASES = [('rail_xing_05', 'road_xing_10'), ('rail_xing_10', 'road_xing_05')]
REQUIRED = ['%s_xing_%02d' % (k, m) for k in XING_KEYS for m in (5, 10)] + \
    ['rail_xing_05', 'rail_xing_10', 'mway_rail_05', 'mway_rail_10']
GROUPS = {}


def xing_grounds():
    """(name, ground fn, road key, road mask, rail mask) for the --check edge-match test"""
    out = []
    for k in XING_KEYS:
        for (ax, mm, tm) in (('y', 5, 10), ('x', 10, 5)):
            out.append(('%s_xing_%02d' % (k, mm), (lambda kk, aa: lambda g, st: g_xing(g, st, kk, aa))(k, ax),
                        k, mm, tm))
    return out
