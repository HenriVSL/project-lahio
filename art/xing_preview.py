#!/usr/bin/env python3
"""xing_preview.py - 1.2 X2: fast iteration on the rail crossings (art_xing.py) without touching the sheets.

    python3 art/xing_preview.py            render the art_xing sprites (both zooms, summer + winter)
    options: -j N workers, --out DIR (default art/preview), --cached (use art/.cache only, no rendering)

Writes <out>/xing_sheet_<z>[_winter].png (the new sprites, 2x / 3x) and the context scenes
<out>/xing_scene_<z>[_winter].png: (A) a railway along X crossed by a gravel road, a street, a main road, the
motorway overpass and a power line (wire_05 over the track), (B) the same with the railway along Y.  Context
sprites (grass, rail_, road autotiles, power_/wire_) come from art/.cache.  Prints the piece KB per sprite."""
import os
import pickle
import sys
import time
import multiprocessing

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

from PIL import Image  # noqa: E402

import gen_art as G  # noqa: E402
import art_xing as X  # noqa: E402


def load_cache(names=None):
    S, W = {64: {}, 32: {}}, {64: {}, 32: {}}
    with open(os.path.join(HERE, '.cache', 'sprites.pickle'), 'rb') as f:
        cache = pickle.load(f)
    with open(os.path.join(HERE, '.cache', 'sprites_winter.pickle'), 'rb') as f:
        wc = pickle.load(f)
    want = ('grass', 'road_', 'grav_', 'main_', 'mway_', 'rail_', 'power_', 'wire_', 'tree_', 'house_t2_v')
    for (name, z), r in cache.items():
        if name.startswith(want):
            im = Image.frombytes('RGB', r[2], r[3])
            S[z][name] = (im, tuple(r[4]), tuple(r[5]))
            wb = wc.get((name, z))
            W[z][name] = (Image.frombytes('RGB', r[2], wb), tuple(r[4]), tuple(r[5])) if wb else S[z][name]
    return S, W


def scene(S, zz, along):
    """railway along `along` ('x' or 'y') at index 7, crossed by gravel / street / main / motorway / power line"""
    zoom = zz / 64.0
    N = 16
    Gd = [['grass%d' % int(G.ihash(i, j, 501) * 3) for j in range(N)] for i in range(N)]
    objs = []
    t = 7
    cross = [(2, 'grav'), (5, 'road'), (8, 'main'), (11, 'mway')]

    def put(i, j, nm):
        if along == 'x':
            Gd[i][j] = nm
        else:
            Gd[j][i] = nm

    def obj(i, j, nm):
        objs.append((nm, i, j, 1, 1) if along == 'x' else (nm, j, i, 1, 1))
    tm = '10' if along == 'x' else '05'      # rail mask along the line
    rm = '05' if along == 'x' else '10'      # road mask across it
    for k in range(N):
        put(k, t, 'rail_' + tm)
    for (c, key) in cross:
        for k in range(N):
            if k != t:
                put(c, k, '%s_%s' % (key, rm))
        if key == 'mway':
            obj(c, t, 'mway_rail_%s' % rm)
        else:
            obj(c, t, '%s_xing_%s' % (key, rm))
    # power line across the track
    for k in range(N):
        obj(14, k, ('wire_' if k == t else 'power_') + rm)
    # a few trees and houses for scale
    for (i, j, nm) in ((0, 3, 'tree_spruce'), (4, 10, 'tree_birch'), (9, 4, 'tree_pine'), (13, 11, 'tree_birch')):
        obj(i, j, nm)
    for (i, j, r) in ((3, 4, 1), (6, 10, 3), (9, 10, 3), (6, 4, 1)):
        obj(i, j, 'house_t2_v%d_r%d' % ((i + j) % 8, r if along == 'x' else (r + 3) % 4))
    canvas, ox, oy = G.compose_map(S, N, Gd, objs, zoom, pad=60)
    return canvas


def detail(S, zz, scale):
    """close-ups: every crossing sprite (both orientations) in a 5x5 patch with its rail and road neighbours"""
    zoom = zz / 64.0
    tiles = []
    N = 5
    for (key, nm) in (('grav', 'grav_xing'), ('road', 'road_xing'), ('main', 'main_xing'), ('mway', 'mway_rail')):
        for rm in ('05', '10'):
            tm = '10' if rm == '05' else '05'
            Gd = [['grass%d' % int(G.ihash(i, j, 502) * 3) for j in range(N)] for i in range(N)]
            for k in range(N):
                if rm == '05':                       # road along Y = column i 2, rail along X = row j 2
                    Gd[2][k] = '%s_%s' % (key, rm)
                    Gd[k][2] = 'rail_' + tm
                else:
                    Gd[k][2] = '%s_%s' % (key, rm)
                    Gd[2][k] = 'rail_' + tm
            objs = [('%s_%s' % (nm, rm), 2, 2, 1, 1)]
            cv, ox, oy = G.compose_map(S, N, Gd, objs, zoom, pad=40)
            tw, th = int(64 * zoom), int(32 * zoom)
            cx, cy = ox, oy + 2 * th + th // 2
            im = cv.crop((cx - int(1.0 * tw), cy - int(1.0 * th) - int(12 * zoom), cx + int(1.0 * tw), cy + int(1.0 * th)))
            tiles.append(im.resize((im.width * scale, im.height * scale), Image.NEAREST))
    w, h = tiles[0].size
    out = Image.new('RGB', (w * 4 + 12, h * 2 + 4), (40, 44, 40))
    for k, t in enumerate(tiles):
        out.paste(t, ((k // 2) * (w + 4), (k % 2) * (h + 4)))
    return out


def main():
    args = sys.argv[1:]
    nproc = int(args[args.index('-j') + 1]) if '-j' in args else multiprocessing.cpu_count()
    out = args[args.index('--out') + 1] if '--out' in args else os.path.join(HERE, 'preview')
    os.makedirs(out, exist_ok=True)
    S, W = load_cache()
    new = {64: [], 32: []}
    if '--cached' not in args:
        jobs = [G.Job(b, r, kind, [(o, 'normal') for o in outs]) for (b, r, kind, outs) in X.extra_jobs()]
        G._JOBS = jobs
        tasks = [(ji, z) for ji in range(len(jobs)) for (z, zz) in G.ZOOMS]
        t0 = time.time()
        ctx = multiprocessing.get_context('fork')
        with ctx.Pool(max(1, min(nproc, len(tasks)))) as pool:
            for rr in pool.imap_unordered(G.run_job, tasks, chunksize=1):
                for r in rr:
                    im = Image.frombytes('RGB', r[2], r[3])
                    imw = Image.frombytes('RGB', r[2], r[6]) if r[6] else im
                    S[r[1]][r[0]] = (im, tuple(r[4]), tuple(r[5]))
                    W[r[1]][r[0]] = (imw, tuple(r[4]), tuple(r[5]))
                    new[r[1]].append(r[0])
        print('rendered %d sprites in %.1fs' % (len(new[64]) + len(new[32]), time.time() - t0))
    else:
        for z in (64, 32):
            new[z] = [n for n in S[z] if '_xing_' in n or n.startswith('mway_rail')]
    for z in (64, 32):
        for D in (S[z], W[z]):
            for (a, t) in X.NAME_ALIASES:
                if t in D:
                    D[a] = D[t]
        hw = z // 2
        tot = 0.0
        for nm in sorted(new[z]):
            im, anc, hot = S[z][nm]
            a = sum((b[2] - b[0]) * (b[3] - b[1]) for b in G.sprite_pieces(nm, im, anc[0], hw)) * 2 / 1024.0
            tot += a
            print('  %d %-14s %dx%d anchor %s  %.1f KB pieces' % (z, nm, im.size[0], im.size[1], anc, a))
        print('  %d total %.1f KB' % (z, tot))
        sc = 3 if z == 64 else 5
        names = sorted(new[z]) + ['rail_10', 'rail_05', 'road_05', 'mway_05']
        G.preview_sheet(S[z], names, scale=sc, width=480 if z == 64 else 240).save(os.path.join(out, 'xing_sheet_%d.png' % z))
        G.preview_sheet(W[z], names, scale=sc, width=480 if z == 64 else 240).save(
            os.path.join(out, 'xing_sheet_%d_winter.png' % z))
        for (D, suf) in ((S[z], ''), (W[z], '_winter')):
            a = scene(D, z, 'x')
            b = scene(D, z, 'y')
            im = Image.new('RGB', (a.width + b.width + 8, max(a.height, b.height)), (40, 44, 40))
            im.paste(a, (0, 0))
            im.paste(b, (a.width + 8, 0))
            if z == 32:
                im = im.resize((im.width * 2, im.height * 2), Image.NEAREST)
            im.save(os.path.join(out, 'xing_scene_%d%s.png' % (z, suf)))
            detail(D, z, 3 if z == 64 else 6).save(os.path.join(out, 'xing_detail_%d%s.png' % (z, suf)))


if __name__ == '__main__':
    main()
