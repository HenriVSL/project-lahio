#!/usr/bin/env python3
"""power_preview.py - 1.2 A1: fast iteration on the power plant art (art_power.py) without touching the sheets.

    python3 art/power_preview.py                 render every art_power entry (both zooms, summer + winter)
    python3 art/power_preview.py --only nuclear  only keys starting with these prefixes (comma list)
    options: -j N workers, --out DIR (default art/preview)

Writes <out>/power_sheet_<z>[_winter].png (every rendered rotation, 2x / 3x) and the scale scenes
<out>/power_scene_<z>[_winter].png: the new plants in a row along a street between a rintamamiestalo, the 2x2 coal
plant and a lamellitalo (context sprites from art/.cache), the r1 renders in a second row, smoke at every hotspot
(and the coal station's _hot2).  Prints the size of every sprite and the piece KB per key (gen_art.sprite_pieces)."""
import math
import os
import pickle
import sys
import time
import multiprocessing

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

from PIL import Image  # noqa: E402

import gen_art as G  # noqa: E402
import art_power as P  # noqa: E402


def jobs_for(only):
    J = []
    for (key, (n, m), rots, variants, ab) in G._norm_rows(P.ENTRIES):
        for v, bld in enumerate(variants):
            for r in G.rot_plan(n, m, rots[v])[0]:
                J.append(G.Job(bld, r, 'lot', [('%s_v%d_r%d' % (key, v, r), 'normal')]))
    if only:
        J = [j for j in J if any(o[0].startswith(p) for o in j.outs for p in only)]
    return J


def load_cache():
    S, W = {64: {}, 32: {}}, {64: {}, 32: {}}
    cpath = os.path.join(HERE, '.cache', 'sprites.pickle')
    wpath = os.path.join(HERE, '.cache', 'sprites_winter.pickle')
    if not os.path.exists(cpath):
        return S, W
    with open(cpath, 'rb') as f:
        cache = pickle.load(f)
    with open(wpath, 'rb') as f:
        wc = pickle.load(f)
    want = ('grass', 'road_', 'house_t2_v', 'house_t3_v', 'apt_t1_3x1_v0', 'apt_t2_3x1_v0', 'coal_plant_v0',
            'heat_plant_v0', 'tree_', 'water', 'shore', 'power_')
    for (name, z), r in cache.items():
        if name.startswith(want):
            im = Image.frombytes('RGB', r[2], r[3])
            S[z][name] = (im, tuple(r[4]), tuple(r[5]))
            wb = wc.get((name, z))
            W[z][name] = (Image.frombytes('RGB', r[2], wb), tuple(r[4]), tuple(r[5])) if wb else S[z][name]
    return S, W


def scene(S, zz, winter):
    zoom = zz / 64.0
    N = 18
    jrow = 10                                  # street along X; lots in front of it face it (+Y = front)
    Gd = [['grass%d' % int(G.ihash(i, j, 500) * 3) for j in range(N)] for i in range(N)]
    for i in range(N):
        Gd[i][jrow] = 'road_10'
    objs = []
    x = 0
    row = [('house_t2_v0_r0', 1, 1), ('coal_plant_v0_r0', 2, 2), ('coal_station_v0_r0', 3, 3),
           ('peat_plant_v0_r0', 2, 2), ('oil_plant_v0_r0', 2, 2), ('nuclear_plant_v0_r0', 3, 3),
           ('apt_t1_3x1_v0_r0', 3, 1), ('house_t2_v3_r0', 1, 1)]
    for (nm, w, h) in row:
        if nm in S:
            objs.append((nm, x, jrow - h, w, h))
        x += w
    # second row behind: the r1 renders + a lamellitalo + houses
    x = 0
    row2 = [('house_t3_v0_r1', 1, 1), ('apt_t2_3x1_v0_r1', 1, 3), ('coal_station_v0_r1', 3, 3),
            ('peat_plant_v0_r1', 2, 2), ('oil_plant_v0_r1', 2, 2), ('nuclear_plant_v0_r1', 3, 3),
            ('heat_plant_v0_r0', 2, 2), ('house_t2_v1_r0', 1, 1), ('house_t2_v2_r0', 1, 1)]
    for (nm, w, h) in row2:
        if nm in S:
            objs.append((nm, x, jrow - 4 - h, w, h))
        x += w
    # houses across the street
    for i in range(0, N, 1):
        nm = 'house_t2_v%d_r2' % (i % 8)
        if nm in S and i % 3 != 2:
            objs.append((nm, i, jrow + 1, 1, 1))
    # smoke: a frozen column above every hotspot (and _hot2)
    tw, th = int(64 * zoom), int(32 * zoom)
    ox_, oy_ = N * tw // 2, int(260 * zoom)
    sm = []
    for ob in list(objs):
        name, i0, j0, n, m = ob[:5]
        img, anc, hot = S[name]
        hots = [hot] if hot[0] >= 0 else []
        h2 = S.get(name + '_hot2')
        if h2:
            hots.append(h2[2])
        si, sj = i0 + n, j0 + m
        for k, hh in enumerate(hots):
            for f in range(G.SMOKE_FRAMES):
                ff = (f + 2 * k) % G.SMOKE_FRAMES
                dx = int(round(ff * 2.2 * zoom))
                dy = int(round(-ff * 6.0 * zoom))
                sx = ox_ + int(round((si - sj) * tw / 2.0)) + hh[0] - anc[0] + dx
                sy = oy_ + int(round((si + sj) * th / 2.0)) + hh[1] - anc[1] + dy
                sm.append(('smoke%d' % ff, i0, j0, n, m, (0, 0), (sx, sy)))
    canvas, ox, oy = G.compose_map(S, N, Gd, objs + sm, zoom, pad=260)
    # crop: the lots' rows
    x0 = ox + (0 - jrow - 1) * tw // 2
    x1 = ox + (N - (jrow - 9)) * tw // 2
    y0 = oy + 2 * th // 2 - int(200 * zoom)
    y1 = oy + (N + jrow + 2) * th // 2
    return canvas.crop((max(0, x0), max(0, y0), min(canvas.width, x1), min(canvas.height, y1)))


def main():
    args = sys.argv[1:]
    only = args[args.index('--only') + 1].split(',') if '--only' in args else None
    nproc = int(args[args.index('-j') + 1]) if '-j' in args else multiprocessing.cpu_count()
    out = args[args.index('--out') + 1] if '--out' in args else os.path.join(HERE, 'preview')
    os.makedirs(out, exist_ok=True)
    jobs = jobs_for(only)
    G._JOBS = jobs
    tasks = [(ji, z) for ji in range(len(jobs)) for (z, zz) in G.ZOOMS]
    t0 = time.time()
    res = {}
    ctx = multiprocessing.get_context('fork')
    with ctx.Pool(max(1, min(nproc, len(tasks)))) as pool:
        for rr in pool.imap_unordered(G.run_job, tasks, chunksize=1):
            for r in rr:
                res[(r[0], r[1])] = r
    print('rendered %d sprites in %.1fs' % (len(res), time.time() - t0))
    S, W = load_cache()
    for z in (64, 32):
        for k, v in G.procedural(z / 64.0).items():
            if k.startswith('smoke'):
                S[z][k] = v
                W[z][k] = v
    new = {64: [], 32: []}
    for (name, z), r in sorted(res.items()):
        im = Image.frombytes('RGB', r[2], r[3])
        imw = Image.frombytes('RGB', r[2], r[6]) if r[6] else im
        S[z][name] = (im, tuple(r[4]), tuple(r[5]))
        W[z][name] = (imw, tuple(r[4]), tuple(r[5]))
        new[z].append(name)
    # _hot2 rows of the rendered sprites, then the rotation aliases (as the header will have them)
    for z in (64, 32):
        for D in (S[z], W[z]):
            for nm in list(new[z]):
                h2 = G.hot2_point(nm, D[nm], z)
                if h2 is not None:
                    D[nm + '_hot2'] = (D[nm][0], D[nm][1], h2)
            for (key, (n, m), rots, variants, ab) in G._norm_rows(P.ENTRIES):
                for v in range(len(variants)):
                    for (ra, rs) in G.rot_plan(n, m, rots[v])[1].items():
                        src = '%s_v%d_r%d' % (key, v, rs)
                        if src in D:
                            D['%s_v%d_r%d' % (key, v, ra)] = D[src]
                        if src + '_hot2' in D:
                            D['%s_v%d_r%d_hot2' % (key, v, ra)] = D[src + '_hot2']
    tot = {64: 0.0, 32: 0.0}
    for z in (64, 32):
        hw = z // 2
        bykey = {}
        for nm in new[z]:
            im, anc, hot = S[z][nm]
            a = sum((b[2] - b[0]) * (b[3] - b[1]) for b in G.sprite_pieces(nm, im, anc[0], hw)) * 2 / 1024.0
            bykey.setdefault(nm.rsplit('_v', 1)[0], []).append((nm, im.size, a, hot))
        for k in sorted(bykey):
            kb = sum(e[2] for e in bykey[k])
            tot[z] += kb
            print('  %d %-16s %6.1f KB pieces  %s' % (z, k, kb, '  '.join('%s %dx%d hot=%s' % (e[0][len(k) + 1:], e[1][0], e[1][1], e[3])
                                                                       for e in bykey[k])))
        sc = 2 if z == 64 else 3
        G.preview_sheet(S[z], new[z], scale=sc, width=1000 if z == 64 else 520).save(
            os.path.join(out, 'power_sheet_%d.png' % z))
        G.preview_sheet(W[z], new[z], scale=sc, width=1000 if z == 64 else 520).save(
            os.path.join(out, 'power_sheet_%d_winter.png' % z))
        sc2 = 1 if z == 64 else 2
        for (D, suf) in ((S[z], ''), (W[z], '_winter')):
            im = scene(D, z, bool(suf))
            if sc2 > 1:
                im = im.resize((im.width * sc2, im.height * sc2), Image.NEAREST)
            im.save(os.path.join(out, 'power_scene_%d%s.png' % (z, suf)))
    print('total pieces: 64: %.1f KB, 32: %.1f KB' % (tot[64], tot[32]))


if __name__ == '__main__':
    main()
