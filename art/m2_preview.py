#!/usr/bin/env python3
"""m2_preview.py - fast iteration on Milestone-2 art (task M4) without touching the sheets or the cache.

    python3 art/m2_preview.py res                    render every entry of art_m2_res.py (both zooms)
    python3 art/m2_preview.py ind --only ind_dairy   only output names starting with these prefixes
    python3 art/m2_preview.py cplx --zoom 64         one zoom only
    python3 art/m2_preview.py misc --context         also a street scene per key (64 zoom) with M1 houses and
                                                     a lamellitalo from art/.cache for scale
    options: -j N workers, --out DIR (default art/.m2prev)

Writes <out>/<key>_<z>.png (2x / 3x scaled sheets of every rendered sprite of that key) and prints the size of
every sprite and the KB per key (16-bit, rendered sprites only; aliases are free)."""
import os
import pickle
import sys
import time
import multiprocessing

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

from PIL import Image  # noqa: E402

import gen_art as G  # noqa: E402


def module_jobs(mod, only):
    J = []
    keys = []

    def add(build, rot, kind, *outs):
        J.append(G.Job(build, rot, kind, [(o if isinstance(o, tuple) else (o, 'normal')) for o in outs]))

    for (key, (n, m), rots, variants, ab) in G._norm_rows(getattr(mod, 'ENTRIES', [])):
        # variants appended to an existing key are numbered after the existing ones
        base = 0
        for b in G.BUILDINGS:
            if b[0] == key and key in G.M2_KEYS:
                base = len(b[3]) - len(variants)
        keys.append(key)
        for i, bld in enumerate(variants):
            v = base + i
            for r in G.rot_plan(n, m, rots[i])[0]:
                outs = ['%s_v%d_r%d' % (key, v, r)]
                if G.ab_wanted(ab, v):
                    outs.append(('%s_v%d_ab_r%d' % (key, v, r), 'ab'))
                add(bld, r, 'lot', *outs)
    if hasattr(mod, 'extra_jobs'):
        for (b, r, kind, outs) in mod.extra_jobs():
            add(b, r, kind, *outs)
            keys.append(outs[0] if isinstance(outs[0], str) else outs[0][0])
    if only:
        J = [j for j in J if any(o[0].startswith(p) for o in j.outs for p in only)]
    return J


def main():
    args = sys.argv[1:]
    if not args:
        print(__doc__)
        sys.exit(1)
    mods = {'res': G.art_m2_res, 'ind': G.art_m2_ind, 'cplx': G.art_m2_cplx, 'misc': G.art_m2_misc}
    sel = [mods[a] for a in args if a in mods] or list(mods.values())
    only = args[args.index('--only') + 1].split(',') if '--only' in args else None
    zsel = args[args.index('--zoom') + 1] if '--zoom' in args else 'both'
    nproc = int(args[args.index('-j') + 1]) if '-j' in args else multiprocessing.cpu_count()
    out = args[args.index('--out') + 1] if '--out' in args else os.path.join(HERE, '.m2prev')
    os.makedirs(out, exist_ok=True)
    jobs = []
    for m in sel:
        jobs += module_jobs(m, only)
    G._JOBS = jobs
    zooms = [z for z in G.ZOOMS if zsel == 'both' or str(z[1]) == zsel]
    tasks = [(ji, z) for ji in range(len(jobs)) for (z, zz) in zooms]
    t0 = time.time()
    res = {}
    if nproc > 1 and len(tasks) > 1:
        ctx = multiprocessing.get_context('fork')
        with ctx.Pool(min(nproc, len(tasks))) as pool:
            for rr in pool.imap_unordered(G.run_job, tasks, chunksize=1):
                for r in rr:
                    res[(r[0], r[1])] = r
    else:
        for t in tasks:
            for r in G.run_job(t):
                res[(r[0], r[1])] = r
    print('rendered %d sprites in %.1fs' % (len(res), time.time() - t0))
    S = {64: {}, 32: {}}
    for (name, z), r in res.items():
        S[z][name] = (Image.frombytes('RGB', r[2], r[3]), tuple(r[4]), tuple(r[5]))
    procs = {}
    for m in sel:
        if hasattr(m, 'extra_procedural'):
            for (z, zz) in zooms:
                for k, v in m.extra_procedural(z).items():
                    if not only or any(k.startswith(p) for p in only):
                        S[zz][k] = v
                        procs.setdefault(zz, []).append(k)
    # group by key (lots) or by name stem (extras)
    import re
    tot = {64: 0, 32: 0}
    groups = {}
    for zz in S:
        for name in S[zz]:
            km = re.match(r'(.+?)_v\d+_(?:ab_)?r\d$', name)
            k = km.group(1) if km else re.sub(r'[_]?\d+$', '', re.sub(r'_(px|ny|nx|py)$', '', name))
            groups.setdefault(k, set()).add(name)
    for k in sorted(groups):
        names = sorted(groups[k])
        line = []
        for zz in (64, 32):
            kb = sum(S[zz][n][0].width * S[zz][n][0].height * 2 for n in names if n in S[zz]) / 1024.0
            tot[zz] += kb
            if any(n in S[zz] for n in names):
                line.append('%d: %6.1f KB' % (zz, kb))
                sc = 2 if zz == 64 else 3
                G.preview_sheet(S[zz], names, scale=sc, width=900).save(os.path.join(out, '%s_%d.png' % (k, zz)))
        print('%-24s %s   (%d sprites; e.g. %s)' % (k, '  '.join(line), len(names),
              ', '.join('%s %dx%d hot=%s' % (n, S[64][n][0].width, S[64][n][0].height, S[64][n][2])
                        for n in names[:2] if n in S[64])))
    print('total: 64: %.1f KB, 32: %.1f KB' % (tot[64], tot[32]))
    if '--context' in args and 64 in [zz for (z, zz) in zooms]:
        context(S[64], groups, out)


def context(S_new, groups, out):
    """street scene per key at 64 zoom: grass, a street along +Y of the lots (fronts face it), the key's v0 r0
    and r1 next to M1 reference buildings from art/.cache"""
    cpath = os.path.join(HERE, '.cache', 'sprites.pickle')
    S = {}
    if os.path.exists(cpath):
        with open(cpath, 'rb') as f:
            cache = pickle.load(f)
        for (name, z), r in cache.items():
            if z == 64 and name.startswith(('grass', 'road_', 'house_t2_v', 'apt_t1_3x1_v0', 'tree_', 'car')):
                S[name] = (Image.frombytes('RGB', r[2], r[3]), tuple(r[4]), tuple(r[5]))
    S.update(S_new)
    import re
    for k in sorted(groups):
        lots = sorted(n for n in groups[k] if re.match(r'.+_v\d+_r[01]$', n) and n in S_new)
        if not lots:
            continue
        N = 14
        Gd = [['grass%d' % ((i * 7 + j * 3) % 3) if 'grass0' in S else 'grass0' for j in range(N)] for i in range(N)]
        objs = []
        jrow = 9                                # street along X at j = 9; lots in front of it (j < 9)
        for i in range(N):
            if 'road_10' in S:
                Gd[i][jrow] = 'road_10'
        x = 1
        if 'house_t2_v0_r0' in S:
            objs.append(('house_t2_v0_r0', x, jrow - 1, 1, 1))
            objs.append(('house_t2_v3_r0', x, jrow + 1, 1, 1)) if 'house_t2_v3_r0' in S else None
            x += 1
        for n in lots[:6]:
            img = S[n][0]
            nm = re.match(r'(.+)_v\d+_r(\d)$', n)
            fp = G.BUILD_FP.get(nm.group(1), (1, 1))
            r = int(nm.group(2))
            w, h = (fp[1], fp[0]) if r % 2 else fp
            if x + w > N:
                break
            objs.append((n, x, jrow - h, w, h))
            x += w
        if 'apt_t1_3x1_v0_r0' in S and x + 3 <= N:
            objs.append(('apt_t1_3x1_v0_r0', x, jrow - 1, 3, 1))
        canvas, ox, oy = G.compose_map(S, N, Gd, objs, 1.0, pad=260)
        x0 = max(0, ox + (0 - jrow - 2) * 32)
        x1 = min(canvas.width, ox + (x + 3 - (jrow - 4)) * 32)
        y0 = max(0, oy + (0 + jrow - 4) * 16 - 240)
        y1 = min(canvas.height, oy + (x + 3 + jrow + 2) * 16)
        canvas.crop((x0, y0, x1, y1)).save(os.path.join(out, 'ctx_%s.png' % k))


if __name__ == '__main__':
    main()
