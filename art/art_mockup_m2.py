"""art_mockup_m2.py - the Milestone-2 town mockup (task M4): the 1960s-70s transformation of a lakeland
town.  A paper mill complex by the rapids with its company housing and church, forestry edges with
clear-cuts and a logging camp, a concrete suburb (element blocks, point blocks, health centre, shopping
centre, heat plant) on the main road with a bus at its stop, the old wooden-house district being replaced,
an industrial band along the railway with the station and Valtion Metalli, farms in the south-east.

Called from gen_art.write_previews (G = the gen_art module).  Names that do not exist (yet) are skipped,
so the layout can be drawn with partial art."""
import math

from artlib import ihash

N = 40


class Town(object):
    def __init__(self, G, S):
        self.G, self.S = G, S
        self.ground = [['grass%d' % int(ihash(i, j, 900) * 3) for j in range(N)] for i in range(N)]
        self.occ = set()
        self.objs = []
        self.roads = {}
        self.missing = set()
        self.blocked = []

    def has(self, name):
        if name in self.S:
            return True
        self.missing.add(name)
        return False

    def free(self, i0, j0, n, m):
        return all(0 <= a < N and 0 <= b < N and (a, b) not in self.occ
                   for a in range(i0, i0 + n) for b in range(j0, j0 + m))

    def place(self, name, i0, j0, n=1, m=1, force=False):
        if not self.has(name):
            return False
        if not force and not self.free(i0, j0, n, m):
            self.blocked.append(name)
            return False
        for a in range(i0, i0 + n):
            for b in range(j0, j0 + m):
                self.occ.add((a, b))
        self.objs.append((name, i0, j0, n, m))
        return True

    def lot(self, key, v, r, i0, j0, ab=False):
        fp = self.G.BUILD_FP.get(key, (1, 1))
        n, m = (fp[1], fp[0]) if r % 2 else fp
        return self.place('%s_v%d_%sr%d' % (key, v, 'ab_' if ab else '', r), i0, j0, n, m)

    def road(self, i, j, key):
        if 0 <= i < N and 0 <= j < N:
            self.roads[(i, j)] = key
            self.occ.add((i, j))

    def finish_roads(self):
        G = self.ground
        for (i, j), key in self.roads.items():
            m = 0
            for bit, (di, dj) in ((1, (0, -1)), (2, (1, 0)), (4, (0, 1)), (8, (-1, 0))):
                k2 = self.roads.get((i + di, j + dj))
                if k2 is None or (key == 'rail') != (k2 == 'rail'):
                    continue
                m |= bit
            if G[i][j].startswith(('water', 'river', 'rapids')):
                G[i][j] = '%s_bridge_%02d' % (key, 10 if m & 10 else 5)
            else:
                G[i][j] = '%s_%02d' % (key, m)


def layout(G, S):
    T = Town(G, S)
    g = T.ground
    # ------------------------------------------------------------ the river (along Y) with rapids by the mill
    land = [[1] * (N + 1) for _ in range(N + 1)]
    for cj in range(N + 1):
        w0 = 1 + (1 if 22 <= cj <= 30 else 0)
        w1 = 4 + (1 if cj >= 26 else 0) + (1 if cj <= 3 else 0)
        for ci in range(w0, w1 + 1):
            land[ci][cj] = 0
    for i in range(N):
        for j in range(N):
            c = (land[i][j], land[i + 1][j], land[i][j + 1], land[i + 1][j + 1])
            mask = c[0] | c[1] << 1 | c[2] << 2 | c[3] << 3
            if mask == 0:
                g[i][j] = ('rapids_y_%d' if (8 <= j <= 11 and 2 <= i <= 3) else 'river_y_%d') % 0
                T.occ.add((i, j))
            elif mask != 15:
                g[i][j] = 'shore%02d' % mask
                T.occ.add((i, j))
    # ------------------------------------------------------------ roads and rail
    for i in range(0, N):
        T.road(i, 14, 'main')               # main road along X, bridge over the river
    for j in range(15, N):
        T.road(17, j, 'road')               # street south through the old wooden district
    for j in range(0, 14):
        T.road(17, j, 'road')               # suburb street north
    for i in range(18, N):
        T.road(i, 7, 'road')                # suburb cross street
    for i in range(6, 17):
        T.road(i, 13, 'grav')               # mill access along the mill's front
    for j in range(5, 13):
        T.road(10, j, 'grav')               # company housing lane / logging road north
    for j in range(0, 5):
        T.road(10, j, 'grav')
    for i in range(0, N):
        T.road(i, 25, 'rail')               # railway along X (bridge over the river)
    for i in range(18, N):
        T.road(i, 29, 'road')               # station / farm road
    for j in range(15, 25):
        T.road(29, j, 'road')               # industrial street
    for i in range(6, 17):
        T.road(i, 20, 'grav')               # lane to the sewage works
    # ------------------------------------------------------------ paper mill (5x5) by the rapids, water at -X
    mx, my = 5, 8
    T.lot('pm_logyard', 0, 0, mx, my)
    T.lot('pm_hall', 0, 0, mx + 2, my)
    T.lot('pm_chipper', 0, 0, mx, my + 2)
    T.lot('pm_boiler', 0, 0, mx, my + 3)
    T.lot('pm_digester', 0, 0, mx + 2, my + 3)
    T.lot('pm_gate', 0, 0, mx + 4, my + 3)
    T.lot('pm_office', 0, 0, mx + 4, my + 4)
    # company housing: row houses along the lane (fronts to the lane), the company church, company sauna
    T.lot('row_houses', 0, 1, 11, 9)       # r1: 1x3, front +X... (lane at i=10 is behind: symmetric art)
    T.lot('row_houses', 1, 1, 11, 5)
    T.lot('row_houses', 0, 3, 8, 5)        # r3: front -X? placed west of the lane, 1x3
    T.lot('church', 0, 0, 14, 10)
    T.lot('patron_villa', 0, 0, 14, 4)
    T.lot('row_houses', 1, 0, 11, 7)
    T.lot('row_houses', 0, 2, 13, 12) if False else None
    T.lot('pm_office', 0, 0, 12, 12) if False else None
    T.lot('sauna', 0, 1, 11, 12)
    T.lot('house_t2', 3, 1, 9, 2)
    T.lot('house_t2', 6, 3, 11, 2)
    # ------------------------------------------------------------ forestry edge (north-west, beyond the lane)
    T.lot('logging_camp', 1, 0, 9, 3)
    for (i, j, nm) in ((6, 1, 'forest_cut0'), (7, 1, 'forest_cut1'), (6, 2, 'forest_cut1'), (7, 2, 'forest_cut0'),
                       (8, 1, 'forest_seed0'), (8, 2, 'forest_seed1'), (6, 3, 'forest_seed1'), (7, 3, 'forest_seed0'),
                       (6, 4, 'forest_young0'), (7, 4, 'forest_young1'), (8, 4, 'forest_young0'),
                       (6, 0, 'forest_young1'), (7, 0, 'forest_young0'), (8, 0, 'forest_seed0'), (8, 3, 'forest_cut0')):
        T.place(nm, i, j)
    T.lot('logging_camp', 0, 0, 12, 0)
    # ------------------------------------------------------------ concrete suburb (north of the main road)
    T.lot('apt_t3_3x1', 0, 0, 18, 4)       # element slabs facing the cross street (j = 7)
    T.lot('apt_t3_3x1', 1, 0, 22, 4)
    T.lot('apt_t3_3x1', 0, 2, 18, 8)
    T.lot('apt_t3_2x2', 0, 0, 22, 8)       # point blocks
    T.lot('apt_t3_2x2', 1, 0, 25, 4)
    T.lot('apt_t2_3x1', 1, 0, 27, 6, )
    T.lot('apt_t2_3x1', 1, 2, 24, 8)
    T.lot('apt_t2_2x2', 1, 0, 27, 3)
    T.lot('apt_t2_2x2', 0, 0, 30, 4)
    T.lot('health_centre', 0, 2, 27, 8)
    T.lot('com_t3_2x2', 1, 0, 30, 12)      # ostari by the main road
    T.lot('com_t3_2x2', 0, 0, 25, 11)
    T.lot('bus_stop', 0, 0, 29, 13)
    T.lot('heat_plant', 0, 0, 33, 8)
    T.lot('apt_t3_2x2', 0, 0, 33, 4)
    T.lot('apt_t3_3x1', 1, 2, 30, 8)
    T.lot('school', 0, 2, 22, 11)
    T.lot('apt_t2_3x1', 0, 2, 18, 11)
    T.lot('park', 1, 0, 21, 12)
    T.lot('tv_tower', 0, 0, 36, 2)
    for (i, j) in ((18, 3), (19, 3), (20, 3), (21, 3), (35, 2), (37, 2), (36, 3), (34, 2), (35, 3), (37, 3)):
        T.place('rock%d' % int(ihash(i, j, 901) * 2), i, j)
    # ------------------------------------------------------------ old wooden district south of the main road
    T.lot('apt_t3_3x1', 0, 1, 18, 17)       # a new slab replacing three houses (r1: front to the street)
    T.lot('ind_metal_t3_3x3', 0, 0, 18, 21)
    for j in range(15, 25):
        if j == 20:
            continue
        t = 2 + (j % 3 == 0)
        T.lot('house_t%d' % t, (j * 3) % (6 if t == 3 else 8), 1, 16, j)
        t2 = 2 if j % 4 else 3
        T.lot('house_t%d' % t2, (j * 5 + 1) % (6 if t2 == 3 else 8), 3, 18, j)
    for j in range(21, 25):                 # houses west of the street, backs to the river meadow
        T.lot('house_t%d' % (1 + j % 2), j % 4, 1, 15, j)
    T.place('build_3x1_r1', 15, 21, 1, 3, force=False)
    T.lot('house_t2', 2, 1, 15, 16, ab=True)
    T.lot('house_t2', 5, 1, 14, 18, ab=True)
    T.lot('com_t3_1x1', 0, 0, 19, 15)
    T.lot('com_t3_1x1', 1, 0, 20, 15)
    T.lot('com_t3_2x1', 0, 0, 21, 15)
    T.lot('post', 0, 0, 23, 15)
    T.lot('library', 0, 0, 24, 15)
    T.lot('police', 0, 0, 25, 15)
    T.lot('com_t3_2x1', 1, 0, 26, 15)
    T.lot('com_t2_2x1', 0, 0, 13, 15)
    T.lot('com_t2_1x1', 1, 0, 12, 15)
    for i in range(6, 12):
        if i == 10:
            continue
        T.lot('house_t2', (i * 3) % 8, 0, i, 19)
        T.lot('house_t1', i % 4, 2, i, 21)
    T.lot('sewage_works', 0, 0, 6, 22)
    # ------------------------------------------------------------ industry along the railway
    T.lot('ind_dairy_t1_2x2', 0, 0, 21, 23)
    T.lot('ind_textile_t1_2x2', 0, 1, 23, 23)
    T.lot('ind_elec_t2_2x2', 0, 0, 25, 23)
    T.lot('ind_dairy_t2_2x2', 0, 1, 27, 23)
    T.lot('ind_wood_t2_3x3', 0, 0, 21, 19)
    T.lot('ind_metal_t2_2x2', 0, 0, 24, 21)
    T.lot('ind_yard', 4, 0, 26, 21)
    T.lot('ind_yard', 5, 0, 27, 21)
    T.lot('ind_yard', 1, 0, 28, 22)
    T.lot('element_factory', 0, 0, 26, 18)
    T.lot('ind_t2_3x3', 0, 1, 24, 18) if False else None
    # Valtion Metalli (5x4) with its siding on the rail side
    vx, vy = 30, 21
    T.lot('vm_hall', 0, 0, vx, vy)
    T.lot('vm_yard', 0, 0, vx + 3, vy)
    T.lot('vm_store', 0, 0, vx + 3, vy + 2)
    T.lot('vm_siding', 0, 0, vx, vy + 3)
    T.lot('vm_gate', 0, 0, vx + 3, vy + 3)
    # station: back (-Y) on the railway, front to the station road
    T.lot('station', 0, 0, 20, 26)
    T.lot('bus_depot', 0, 0, 23, 26)
    T.lot('coal_plant', 0, 0, 35, 16)
    # ------------------------------------------------------------ farms (south-east)
    for (i, j, key, v) in ((31, 31, 'farm_t2', 0), (36, 30, 'farm_t3', 0), (27, 34, 'farm_t1', 0), (33, 36, 'farm_t2', 1),
                           (38, 35, 'farm_t1', 1), (24, 31, 'farm_t3', 1)):
        T.lot(key, v, 0, i, j)
    for j in range(30, N):
        T.road(30, j, 'grav')
    for (i, j) in ((29, 31), (31, 32), (37, 31), (28, 35), (34, 36), (24, 32)):
        T.place('field_hay', i, j)
    for i in range(18, N):
        for j in range(30, N):
            if (i, j) not in T.occ and ihash(i, j, 902) < 0.92 and (i + j) % 11 != 0:
                g[i][j] = 'field_plough' if (i // 3 + j // 4) % 3 == 0 else 'field_crop'
                T.occ.add((i, j))
    # ------------------------------------------------------------ vehicles, power line
    T.finish_roads()
    cars = [(12, 14, 'px', (0.0, 4.5), 6), (20, 14, 'nx', (0.0, -4.5), 3), (26, 14, 'px', (0.0, 4.5), 1),
            (34, 14, 'nx', (0.0, -4.5), 4), (17, 10, 'py', (-3.6, 0.0), 0), (17, 18, 'ny', (3.6, 0.0), 9),
            (24, 7, 'px', (0.0, 3.6), 11), (32, 7, 'nx', (0.0, -3.6), 7), (25, 29, 'px', (0.0, 3.6), 10),
            (8, 14, 'nx', (0.0, -4.5), 5)]
    for (i, j, d, off, c) in cars:
        if T.has('car%d_%s' % (c, d)):
            T.objs.append(('car%d_%s' % (c, d), i, j, 1, 1, off))
    if T.has('bus_nx'):
        T.objs.append(('bus_nx', 29, 14, 1, 1, (2.0, -4.5)))     # at the stop (stop lot at 29,13 -> road j=14)
    if T.has('bus_px'):
        T.objs.append(('bus_px', 13, 14, 1, 1, (0.0, 4.5)))
    if T.has('bus_py'):
        T.objs.append(('bus_py', 17, 22, 1, 1, (-3.6, 0.0)))
    # a house fire in the old district (fire frames over a lot)
    if T.has('fire1'):
        T.objs.append(('fire1', 16, 23, 1, 1, (-3.0, -2.0)))
        T.objs.append(('fire2', 16, 23, 1, 1, (3.0, 2.0)))
    # ------------------------------------------------------------ forest on what is left
    kinds = ['forest_mixed%d', 'forest_spruce%d', 'forest_birch%d', 'forest_pine%d']
    for i in range(N):
        for j in range(N):
            if (i, j) in T.occ:
                continue
            h = ihash(i, j, 903)
            edge = (j < 6 and i < 17) or (i > 36) or (j > 30 and i < 18) or (i < 6 and j > 15) or \
                (26 <= j and i < 18) or (j < 3)
            if (edge and h < 0.85) or h < 0.16:
                T.place(kinds[int(ihash(i, j, 904) * 4)] % int(ihash(i, j, 905) * 3), i, j)
            elif h < 0.16:
                T.place('rock%d' % int(ihash(i, j, 906) * 2), i, j)
            elif h < 0.34:
                tk = ['tree_birch', 'tree_spruce', 'tree_pine', 'tree_birch_au', 'tree_rowan'][int(ihash(i, j, 907) * 5)]
                if T.has(tk):
                    T.objs.append((tk, i, j, 1, 1))
    return T


def render(G, S, zoom, W, H, centre, T=None):
    T = T or layout(G, S)
    tw, th = int(64 * zoom), int(32 * zoom)
    pad = 260
    ox_, oy_ = N * tw // 2, int(pad * zoom)
    sm = []
    for e in G.add_smoke(S, N, T.objs, zoom):
        tag, si, sj, ddx, ddy = e[6]
        sx = ox_ + int(round((si - sj) * tw / 2.0)) + ddx
        sy = oy_ + int(round((si + sj) * th / 2.0)) + ddy
        sm.append(e[:6] + ((sx, sy),))
    canvas, ox, oy = G.compose_map(S, N, T.ground, T.objs + sm, zoom, pad=pad)
    ci, cj = centre
    cx = ox + (ci - cj) * tw / 2.0
    cy = oy + (ci + cj) * th / 2.0
    x0, y0 = int(cx - W / 2), int(cy - H / 2)
    return canvas.crop((x0, y0, x0 + W, y0 + H))


def write(G, S64, S32, outdir):
    import os
    T64 = layout(G, S64)
    T32 = layout(G, S32)
    views = [('town_m2_64.png', S64, T64, 1.0, 800, 600, (25.0, 10.5)),          # concrete suburb + bus stop
             ('town_m2_mill_64.png', S64, T64, 1.0, 800, 600, (10.0, 8.0)),     # paper mill + company town
             ('town_m2_ind_64.png', S64, T64, 1.0, 800, 600, (27.0, 23.0)),     # industry, station, VM
             ('town_m2_south_64.png', S64, T64, 1.0, 800, 600, (16.0, 21.0)),   # old district, services, fire
             ('town_m2_farms_64.png', S64, T64, 1.0, 800, 400, (31.0, 33.0)),   # farms
             ('town_m2_32.png', S32, T32, 0.5, 800, 600, (20.0, 17.0)),
             ('town_m2_overview_32.png', S32, T32, 0.5, 1280, 720, (20.0, 18.5))]
    for (nm, S, T, z, W, H, c) in views:
        render(G, S, z, W, H, c, T).save(os.path.join(outdir, nm))
    if T64.missing:
        print('  town_m2 mockup: %d names missing (skipped): %s' % (len(T64.missing), ' '.join(sorted(T64.missing)[:30])))
