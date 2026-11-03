#!/usr/bin/env python3
"""Placeholder art for the Kaupunki render probe (deterministic).

Writes 24-bit uncompressed BMPs into art/ (RGB(255,0,255) = colour key, no anti-aliasing
against the key) and art.h with the sprite rectangles. Two zoom levels: 0 = 64x32 tiles,
1 = 32x16 tiles (drawn natively at half size, not downscaled).

  ter64/ter32.bmp  terrain diamonds, 8 per row, 16 kinds; 5 blocks of 2 rows: block 0 normal,
                   blocks 1..4 the same tiles tinted 50% toward the overlay level colour
  ovl64/ovl32.bmp  4 checker-dithered (50%) overlay diamonds, one per level
  obj64/obj32.bmp  buildings, trees, cars packed into a <=640 (320) wide atlas
  panel.bmp        160x600 UI side panel background
  icons.bmp        16 toolbar icons 32x32 in one 512x32 strip
"""
import os, random, math
from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, 'art')
KEY = (255, 0, 255)
LEVEL = [(0, 200, 0), (220, 220, 0), (255, 128, 0), (255, 0, 0)]


def safe(c):
    r, g, b = [max(0, min(255, int(v))) for v in c]
    if r >= 240 and g < 16 and b >= 240:   # never collide with the key after 565 truncation
        g = 24
    return (r, g, b)


def inside(x, y, hw, hh):
    return abs(x + 0.5 - hw) / hw + abs(y + 0.5 - hh) / hh < 1.0


def tile_ab(x, y, hw, hh):
    a = ((x + 0.5) / hw - 1 + (y + 0.5) / hh) / 2 + 0.5 - 0.5
    b = ((y + 0.5) / hh - (x + 0.5) / hw + 1) / 2
    return a, b


def terrain_pixel(kind, x, y, tw, rng):
    hw, hh = tw // 2, tw // 4
    a, b = tile_ab(x, y, hw, hh)
    n = rng.random()
    light = 1.0 + 0.12 * (b - a)          # light from the upper left
    grass = [(70, 140, 50), (80, 150, 55), (64, 130, 48), (90, 150, 60)]
    if kind < 4:
        c = grass[kind]
        if n < 0.2: c = (c[0] - 15, c[1] - 20, c[2] - 10)
        elif n < 0.3: c = (c[0] + 20, c[1] + 20, c[2] + 5)
    elif kind == 4:
        c = (130, 100, 60) if n < 0.7 else (110, 85, 50)
    elif kind == 5:
        c = (40, 80, 170) if (int((a + b) * 6 + n * 0.6) & 1) else (50, 95, 190)
        light = 1.0
    elif kind in (6, 7, 8):
        along_x = abs(b - 0.5) < 0.3
        along_y = abs(a - 0.5) < 0.3
        road = (kind == 6 and along_x) or (kind == 7 and along_y) or (kind == 8 and (along_x or along_y))
        if road:
            c = (70, 70, 74) if n < 0.8 else (80, 80, 84)
            if kind == 6 and abs(b - 0.5) < 0.035 and (a * 4) % 1 < 0.5: c = (230, 220, 120)
            if kind == 7 and abs(a - 0.5) < 0.035 and (b * 4) % 1 < 0.5: c = (230, 220, 120)
        else:
            c = (100, 140, 70)
    elif kind == 9:
        c = (200, 180, 120) if n < 0.8 else (180, 160, 100)
    elif kind == 10:
        c = (40, 90, 35) if n < 0.6 else (30, 75, 30)
    elif kind == 11:
        c = (150, 150, 150) if n < 0.8 else (130, 130, 130)   # plaza / concrete
    elif kind == 12:
        c = (120, 90, 70) if ((int(a * 4) + int(b * 4)) & 1) else (100, 70, 55)   # farmland
    else:
        c = [(160, 60, 60), (60, 60, 160), (160, 160, 60)][kind - 13]   # zone markers
        if n < 0.5: c = (100, 140, 70)
    return safe((c[0] * light, c[1] * light, c[2] * light))


def make_terrain(tw):
    th = tw // 2
    img = Image.new('RGB', (8 * tw, 10 * th), KEY)
    px = img.load()
    for kind in range(16):
        rng = random.Random(1000 + kind)
        base = []
        for y in range(th):
            row = []
            for x in range(tw):
                row.append(terrain_pixel(kind, x, y, tw, rng) if inside(x, y, tw // 2, th // 2) else None)
            base.append(row)
        for lvl in range(5):
            ox, oy = (kind & 7) * tw, ((kind >> 3) + 2 * lvl) * th
            for y in range(th):
                for x in range(tw):
                    c = base[y][x]
                    if c is None: continue
                    if lvl:
                        t = LEVEL[lvl - 1]
                        c = safe(((c[0] + t[0]) // 2, (c[1] + t[1]) // 2, (c[2] + t[2]) // 2))
                    px[ox + x, oy + y] = c
    return img


def make_overlay(tw):
    th = tw // 2
    img = Image.new('RGB', (4 * tw, th), KEY)
    px = img.load()
    for lvl in range(4):
        for y in range(th):
            for x in range(tw):
                if inside(x, y, tw // 2, th // 2) and ((x + y) & 1) == 0:
                    px[lvl * tw + x, y] = LEVEL[lvl]
    return img


def shade(c, f):
    return safe((c[0] * f, c[1] * f, c[2] * f))


def draw_box(w, h, roof, wall, win_rows, win_col=(250, 230, 140), rng=None):
    """Iso box sprite w x h; footprint diamond w x w/2 at the bottom."""
    img = Image.new('RGB', (w, h), KEY)
    d = ImageDraw.Draw(img)
    q, hf = w // 4, w // 2
    left = [(0, q), (hf - 1, hf - 1), (hf - 1, h - 1), (0, h - 1 - q)]
    right = [(hf, hf - 1), (w - 1, q), (w - 1, h - 1 - q), (hf, h - 1)]
    top = [(hf - 1, 0), (hf, 0), (w - 1, q - 1), (w - 1, q), (hf, hf - 1), (hf - 1, hf - 1), (0, q), (0, q - 1)]
    d.polygon(left, fill=shade(wall, 0.7))
    d.polygon(right, fill=shade(wall, 1.0))
    d.polygon(top, fill=roof)
    if win_rows and h - hf > 8:
        step = max(4, w // 16)
        wall_h = h - hf
        for yy in range(hf + step, h - step, step * 2):
            for xx in range(step // 2, hf - step // 2, step):
                lit = rng.random() < 0.6 if rng else True
                c = win_col if lit else (40, 50, 70)
                # left face: y shifts down by 0.5 per x
                y0 = yy + xx // 2
                if y0 + max(1, step // 2) < h - 1 - (q - xx // 2):
                    d.rectangle([xx, y0, xx + max(0, step // 2 - 1), y0 + max(1, step // 2)], fill=shade(c, 0.8))
                x1 = w - 1 - xx
                y1 = yy + xx // 2
                if y1 + max(1, step // 2) < h - 1 - (q - xx // 2):
                    d.rectangle([x1 - max(0, step // 2 - 1), y1, x1, y1 + max(1, step // 2)], fill=c)
    return img


def draw_tree(w, h, rng):
    img = Image.new('RGB', (w, h), KEY)
    d = ImageDraw.Draw(img)
    s = w / 64.0
    for i in range(3):
        cx = w * (0.3 + 0.2 * i) + rng.randint(-2, 2) * s
        base = h - w // 4 - (6 - 3 * (i & 1)) * s
        tw = 3 * s
        d.rectangle([cx - tw / 2, base - 10 * s, cx + tw / 2, base], fill=(90, 60, 30))
        r = (10 + rng.randint(0, 4)) * s
        cy = base - 10 * s - r * 0.8
        d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=(30, 110 + rng.randint(0, 30), 40))
        d.ellipse([cx - r * 0.6, cy - r * 0.8, cx + r * 0.2, cy], fill=(60, 150, 60))
    return img


def make_objects(z):
    s = 1 if z == 0 else 2
    rng = random.Random(77)
    items = []   # (name, img, foot)
    for i in range(4):
        items.append(('house%d' % i, draw_box(64 // s, 48 // s, [(170, 60, 50), (120, 70, 50), (150, 150, 160), (90, 110, 60)][i],
                                              [(220, 210, 180), (200, 180, 150), (230, 230, 220), (210, 200, 160)][i], 0, rng=rng), 1))
    for i in range(3):
        items.append(('mid%d' % i, draw_box(64 // s, 96 // s, (110, 110, 120), [(180, 170, 150), (150, 160, 180), (170, 140, 120)][i], 1, rng=rng), 1))
    for i in range(3):
        items.append(('tall%d' % i, draw_box(64 // s, 160 // s, (90, 90, 100), [(120, 140, 170), (160, 160, 160), (110, 130, 120)][i], 1, rng=rng), 1))
    for i in range(2):
        items.append(('big%d' % i, draw_box(128 // s, 128 // s, (100, 100, 110), [(170, 120, 90), (130, 150, 170)][i], 1, rng=rng), 2))
    for i in range(3):
        items.append(('tree%d' % i, draw_tree(64 // s, 64 // s, rng), 1))
    for i in range(8):
        col = [(200, 40, 40), (40, 80, 200), (230, 230, 230), (40, 40, 40), (230, 200, 40), (40, 160, 60), (150, 80, 160), (240, 140, 40)][i]
        items.append(('car%d' % i, draw_box(16 // s, 12 // s, shade(col, 1.2), col, 0, rng=rng), 0))
    # shelf packing, width 640/s, tallest first
    W = 640 // s
    order = sorted(range(len(items)), key=lambda k: (-items[k][1].size[1], k))
    rects = [None] * len(items)
    x = y = shelf = 0
    for k in order:
        w, h = items[k][1].size
        if x + w > W:
            y += shelf; x = 0; shelf = 0
        rects[k] = (x, y, w, h)
        x += w; shelf = max(shelf, h)
    H = y + shelf
    img = Image.new('RGB', (W, H), KEY)
    for k, (name, im, foot) in enumerate(items):
        img.paste(im, rects[k][:2])
    return img, [(items[k][0], rects[k], items[k][2]) for k in range(len(items))]


def make_panel():
    img = Image.new('RGB', (160, 600), (0, 0, 0))
    d = ImageDraw.Draw(img)
    for y in range(600):
        v = 60 + (y * 30) // 600
        d.line([(0, y), (159, y)], fill=(v - 20, v - 10, v + 20))
    d.rectangle([0, 0, 159, 599], outline=(140, 150, 180))
    d.rectangle([1, 1, 158, 598], outline=(20, 25, 40))
    d.rectangle([14, 148, 145, 279], fill=(10, 10, 20), outline=(140, 150, 180))
    d.rectangle([6, 286, 153, 590], fill=(30, 35, 55), outline=(90, 100, 130))
    return img


def make_icons():
    img = Image.new('RGB', (512, 32), (0, 0, 0))
    d = ImageDraw.Draw(img)
    cols = [(200, 60, 60), (60, 200, 60), (60, 60, 200), (200, 200, 60), (200, 60, 200), (60, 200, 200), (230, 140, 40), (160, 160, 160)]
    for i in range(16):
        x = i * 32
        d.rectangle([x, 0, x + 31, 31], fill=(110, 115, 130))
        d.line([(x, 0), (x + 31, 0)], fill=(200, 205, 220)); d.line([(x, 0), (x, 31)], fill=(200, 205, 220))
        d.line([(x, 31), (x + 31, 31)], fill=(40, 40, 50)); d.line([(x + 31, 0), (x + 31, 31)], fill=(40, 40, 50))
        c = cols[i & 7]
        if i < 8: d.rectangle([x + 8, 8, x + 23, 23], fill=c)
        else: d.polygon([(x + 16, 6), (x + 26, 24), (x + 6, 24)], fill=c)
    return img


def main():
    os.makedirs(OUT, exist_ok=True)
    hdr = ['/* generated by mkart.py - do not edit */', '#ifndef ART_H', '#define ART_H']
    allrects = []
    for z, tw in ((0, 64), (1, 32)):
        make_terrain(tw).save(os.path.join(OUT, 'ter%d.bmp' % tw))
        make_overlay(tw).save(os.path.join(OUT, 'ovl%d.bmp' % tw))
        img, rects = make_objects(z)
        img.save(os.path.join(OUT, 'obj%d.bmp' % tw))
        allrects.append(rects)
        hdr.append('#define OBJ%d_W %d' % (tw, img.size[0]))
        hdr.append('#define OBJ%d_H %d' % (tw, img.size[1]))
    names = [r[0] for r in allrects[0]]
    hdr.append('#define NOBJ %d' % len(names))
    for i, n in enumerate(names):
        hdr.append('#define OB_%s %d' % (n.upper(), i))
    hdr.append('/* x, y, w, h, footprint (tiles per side; 0 = vehicle) */')
    hdr.append('static const short OBJ_RECT[2][NOBJ][5] = {')
    for z in range(2):
        hdr.append('  {')
        for name, (x, y, w, h), foot in allrects[z]:
            hdr.append('    { %3d, %3d, %3d, %3d, %d },  /* %s */' % (x, y, w, h, foot, name))
        hdr.append('  },')
    hdr.append('};')
    hdr.append('#endif')
    with open(os.path.join(HERE, 'art.h'), 'w', newline='\r\n') as f:
        f.write('\n'.join(hdr) + '\n')
    make_panel().save(os.path.join(OUT, 'panel.bmp'))
    make_icons().save(os.path.join(OUT, 'icons.bmp'))


if __name__ == '__main__':
    main()
