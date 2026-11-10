#!/usr/bin/env python3
"""gen_ui.py - KAUPUNKI UI art + bitmap fonts (task T03).

Teak & bakelite 1960s radio/desk look (art/explore/mockup_C.png).  Deterministic: same output every run.
Writes (all into art/):
    ui_frame.bmp   frame pieces, 9-slices, buttons, controls, lamps, cursor
    ui_icons.bmp   13x13 ink pictograms (+ greyed "_off" variants)
    ui_pres.bmp    President caricature (plaque + panel size, 4 moods), newspaper masthead, stamps
    font_body.bmp  7 px proportional body font in colour variants (+ shadow / outline variants)
    font_head.bmp  9 px bold headline font (dark, light, red)
    art_ui.h       ArtSpr / ArtGlyph / ArtFont tables exactly as src/art.h specifies
    ui_layout.h    frame layout per resolution (640x480, 800x600, 1024x768) + UI colours for T09
    preview_ui/    review PNGs (frame compositions per resolution, sheets at 3x, newspaper)
Usage:
    python3 art/gen_ui.py            generate everything
    python3 art/gen_ui.py --check    generate, then self-check (BMP format, widths, key purity, 565, glyphs,
                                     icon names listed in UI.md, layout sanity)
Pillow only (no numpy).  BMPs: 24-bit BI_RGB, width <= 640, key RGB(255,0,255), colours pre-rounded to 565.
"""
import os
import sys
import math
import struct
from PIL import Image, ImageFont, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
PREV = os.path.join(HERE, 'preview_ui')
KEY = (255, 0, 255)
ATLAS_W = 640


# =========================================================================== basics
def clamp(v, a=0.0, b=1.0):
    return a if v < a else b if v > b else v


def lerp(a, b, t):
    return a + (b - a) * t


def hexc(s):
    s = s.lstrip('#')
    return (int(s[0:2], 16), int(s[2:4], 16), int(s[4:6], 16))


def to565(c):
    """round a colour to what RGB565 can show (same as gen_explore.py), never the key"""
    r, g, b = [int(clamp(v, 0, 255) + 0.5) for v in c]
    r = (r >> 3) * 255 // 31
    g = (g >> 2) * 255 // 63
    b = (b >> 3) * 255 // 31
    if r > 240 and g < 12 and b > 240:
        g = 12
    return (r, g, b)


def mix(c1, c2, t):
    return to565(tuple(lerp(a, b, t) for a, b in zip(c1, c2)))


def ihash(*a):
    h = 2166136261
    for v in a:
        h ^= (int(v) & 0xffffffff)
        h = (h * 16777619) & 0xffffffff
        h ^= h >> 13
        h = (h * 1274126177) & 0xffffffff
    h ^= h >> 16
    return (h & 0xffffff) / 16777216.0


def pnoise(x, y, cx, cy, seed, px=0, py=0):
    """value noise, cell cx * cy px; px/py = period in cells (0 = none) -> seamless tiling"""
    fx, fy = x / cx, y / cy
    ix, iy = int(math.floor(fx)), int(math.floor(fy))
    tx, ty = fx - ix, fy - iy
    tx = tx * tx * (3 - 2 * tx)
    ty = ty * ty * (3 - 2 * ty)

    def h(i, j):
        if px:
            i %= px
        if py:
            j %= py
        return ihash(i, j, seed)
    return lerp(lerp(h(ix, iy), h(ix + 1, iy), tx), lerp(h(ix, iy + 1), h(ix + 1, iy + 1), tx), ty)


BAYER4 = [[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9], [15, 7, 13, 5]]


# =========================================================================== palette (STYLE.md 6 "UI" + THEME 7.2, muted)
PAL_HEX = {
    # teak veneer (side panel)
    'teak_xdk': '#3a281c', 'teak_dk': '#4e3524', 'teak_md': '#5c3f2b', 'teak': '#6b4a32', 'teak_lt': '#86603f',
    'teak_hl': '#9a7550',
    # cream bakelite (keys)
    'cream_xlt': '#faf4e2', 'cream_lt': '#f4ecd6', 'cream': '#e6dcc2', 'cream_dk': '#c4b896', 'cream_xdk': '#a39878',
    'cream_sh': '#7d7460',
    # lit (selected) bakelite: warm amber glow like a lit radio band key
    'lit_lt': '#f6e2a8', 'lit': '#e8c97e', 'lit_dk': '#c2a25c',
    # dark brown bakelite (window frames, fly-outs)
    'bak_xdk': '#21170f', 'bak_dk': '#2f2118', 'bak': '#3f2d21', 'bak_lt': '#5a4232', 'bak_hl': '#76594a',
    # brass
    'brass_xdk': '#5a4a2a', 'brass_dk': '#7e6a3c', 'brass': '#b89a5a', 'brass_lt': '#dcc488', 'brass_hl': '#f0dfa8',
    # dark top bar / ticker
    'bar_xdk': '#1c1b19', 'bar_dk': '#262522', 'bar': '#3a3934', 'bar_lt': '#55534b', 'bar_hl': '#6d6a60',
    # paper and ink
    'paper_lt': '#ece4cc', 'paper': '#e2d9bf', 'paper_dk': '#d2c7a8', 'card': '#b9ae96', 'tip': '#efe7c9',
    'ink': '#2a2622', 'pencil': '#5a5650', 'grey': '#8f887a',
    # accents (muted)
    'red': '#9c3b2e', 'red_lt': '#d77a62', 'stamp': '#a8382e', 'green': '#4f6b48', 'green_lt': '#9cbc86',
    'blue': '#3c5468', 'blue_lt': '#8fb0cc', 'amber': '#b8862a', 'amber_lt': '#e8bc5c', 'ochre': '#c4a15e',
    'steel': '#767b7c',
    # lamps
    'lamp_off': '#4a463e', 'lamp_off_lt': '#5e5950',
    # skin + suit (portrait)
    'skin_xdk': '#8a5a44', 'skin_dk': '#b47e62', 'skin': '#d6a585', 'skin_lt': '#e8c0a0', 'skin_hl': '#f4dcc4',
    'suit_dk': '#23252b', 'suit': '#33363e', 'suit_lt': '#4a4e58', 'suit_hl': '#626772', 'shirt': '#eee8da',
    'shirt_dk': '#bdb6a6', 'tie': '#5c2a26', 'tie_lt': '#7c3c34', 'hair': '#9a958c', 'hair_dk': '#6e6a63',
    'mouth': '#6a2e28', 'teeth': '#f2ece0',
}
P = dict((k, to565(hexc(v))) for k, v in PAL_HEX.items())


# =========================================================================== image helpers
def new(w, h, col=KEY):
    return Image.new('RGB', (w, h), col)


class Cv(object):
    """tiny pixel canvas over a PIL image (clipped writes)"""

    def __init__(self, img):
        self.img = img
        self.px = img.load()
        self.w, self.h = img.size

    def put(self, x, y, c):
        if 0 <= x < self.w and 0 <= y < self.h:
            self.px[x, y] = c

    def get(self, x, y):
        return self.px[x, y]

    def fill(self, x0, y0, x1, y1, c):
        for y in range(max(0, y0), min(self.h, y1)):
            for x in range(max(0, x0), min(self.w, x1)):
                self.px[x, y] = c

    def hline(self, x0, x1, y, c):
        self.fill(x0, y, x1, y + 1, c)

    def vline(self, x, y0, y1, c):
        self.fill(x, y0, x + 1, y1, c)

    def rect(self, x0, y0, x1, y1, c):
        self.hline(x0, x1, y0, c)
        self.hline(x0, x1, y1 - 1, c)
        self.vline(x0, y0, y1, c)
        self.vline(x1 - 1, y0, y1, c)

    def bevel(self, x0, y0, x1, y1, lt, dk):
        self.hline(x0, x1 - 1, y0, lt)
        self.vline(x0, y0, y1 - 1, lt)
        self.hline(x0 + 1, x1, y1 - 1, dk)
        self.vline(x1 - 1, y0 + 1, y1, dk)

    def blit(self, src, x, y, keyed=True):
        sp = src.load()
        sw, sh = src.size
        for j in range(sh):
            for i in range(sw):
                c = sp[i, j]
                if keyed and c == KEY:
                    continue
                self.put(x + i, y + j, c)

    def grid(self, x, y, rows, cmap):
        for j, row in enumerate(rows):
            for i, ch in enumerate(row):
                if ch in cmap:
                    self.put(x + i, y + j, cmap[ch])


# =========================================================================== sprite registry
SHEETS = ['frame', 'icons', 'pres']
SPR = dict((s, []) for s in SHEETS)          # sheet -> [(name, img, ax, ay, hx, hy)]
NAMES = {}


def add(sheet, name, img, ax=0, ay=0, hx=-1, hy=-1):
    assert name not in NAMES, 'duplicate sprite ' + name
    NAMES[name] = (sheet, img)
    SPR[sheet].append((name, img, ax, ay, hx, hy))
    return img


def get(name):
    return NAMES[name][1]


NINE = ('tl', 't', 'tr', 'l', 'c', 'r', 'bl', 'b', 'br')
SLICES = {}      # base -> (cl, ct, cr, cb, E) for documentation and preview composition
THREES = {}      # base -> (cl, cr, E, h) horizontal 3-slice ; base -> ('v', ct, cb, E, w) vertical


def make9(sheet, base, draw, cl, ct, cr, cb, E):
    """render one box of size (cl+E+cr) x (ct+E+cb) with draw(cv, w, h) and cut 9 pieces.
       Edges (t, b, l, r) and centre (c) are E px long and tile seamlessly (draw must be uniform/periodic E)."""
    W, H = cl + E + cr, ct + E + cb
    img = new(W, H)
    draw(Cv(img), W, H)
    xs = [(0, cl), (cl, cl + E), (cl + E, W)]
    ys = [(0, ct), (ct, ct + E), (ct + E, H)]
    k = 0
    for (y0, y1) in ys:
        for (x0, x1) in xs:
            add(sheet, '%s_%s' % (base, NINE[k]), img.crop((x0, y0, x1, y1)))
            k += 1
    SLICES[base] = (cl, ct, cr, cb, E)


def make3h(sheet, base, draw, cl, cr, E, h):
    W = cl + E + cr
    img = new(W, h)
    draw(Cv(img), W, h)
    add(sheet, base + '_l', img.crop((0, 0, cl, h)))
    add(sheet, base + '_m', img.crop((cl, 0, cl + E, h)))
    add(sheet, base + '_r', img.crop((cl + E, 0, W, h)))
    THREES[base] = ('h', cl, cr, E, h)


def make3v(sheet, base, draw, ct, cb, E, w):
    H = ct + E + cb
    img = new(w, H)
    draw(Cv(img), w, H)
    add(sheet, base + '_t', img.crop((0, 0, w, ct)))
    add(sheet, base + '_m', img.crop((0, ct, w, ct + E)))
    add(sheet, base + '_b', img.crop((0, ct + E, w, H)))
    THREES[base] = ('v', ct, cb, E, w)


def tile_into(cv, piece, x, y, w, h):
    """tile a piece over a rect, clipping the last tile (what T09 does with gfx_blit_part)"""
    pw, ph = piece.size
    yy = 0
    while yy < h:
        xx = 0
        hh = min(ph, h - yy)
        while xx < w:
            ww = min(pw, w - xx)
            cv.blit(piece.crop((0, 0, ww, hh)), x + xx, y + yy)
            xx += ww
        yy += hh


def draw9(cv, base, x, y, w, h):
    cl, ct, cr, cb, E = SLICES[base]
    g = lambda p: get('%s_%s' % (base, p))
    tile_into(cv, g('c'), x + cl, y + ct, w - cl - cr, h - ct - cb)
    tile_into(cv, g('t'), x + cl, y, w - cl - cr, ct)
    tile_into(cv, g('b'), x + cl, y + h - cb, w - cl - cr, cb)
    tile_into(cv, g('l'), x, y + ct, cl, h - ct - cb)
    tile_into(cv, g('r'), x + w - cr, y + ct, cr, h - ct - cb)
    cv.blit(g('tl'), x, y)
    cv.blit(g('tr'), x + w - cr, y)
    cv.blit(g('bl'), x, y + h - cb)
    cv.blit(g('br'), x + w - cr, y + h - cb)


def draw3h(cv, base, x, y, w):
    _, cl, cr, E, h = THREES[base]
    cv.blit(get(base + '_l'), x, y)
    tile_into(cv, get(base + '_m'), x + cl, y, w - cl - cr, h)
    cv.blit(get(base + '_r'), x + w - cr, y)


def draw3v(cv, base, x, y, h):
    _, ct, cb, E, w = THREES[base]
    cv.blit(get(base + '_t'), x, y)
    tile_into(cv, get(base + '_m'), x, y + ct, w, h - ct - cb)
    cv.blit(get(base + '_b'), x, y + h - cb)


# =========================================================================== FONTS
# Body font: hand drawn, proportional, 7 px caps, 2 px descenders (base from gen_explore.py FONT,
# extended to full ASCII + Latin-1/cp1252 extras).  Rows are cap-relative: rows 0..6 = cap height
# (row 6 sits on the baseline), rows 7..8 = descenders.  Lowercase x-height = rows 2..6.
# In the font cell (line height 11) the cap rows land on rows 2..8; rows 0..1 hold capital accents.
BODY = {
    'A': '.##. #..# #..# #### #..# #..# #..#', 'B': '###. #..# #..# ###. #..# #..# ###.',
    'C': '.##. #..# #... #... #... #..# .##.', 'D': '###. #..# #..# #..# #..# #..# ###.',
    'E': '#### #... #... ###. #... #... ####', 'F': '#### #... #... ###. #... #... #...',
    'G': '.##. #..# #... #.## #..# #..# .###', 'H': '#..# #..# #..# #### #..# #..# #..#',
    'I': '### .#. .#. .#. .#. .#. ###', 'J': '...# ...# ...# ...# #..# #..# .##.',
    'K': '#..# #..# #.#. ##.. #.#. #..# #..#', 'L': '#... #... #... #... #... #... ####',
    'M': '#...# ##.## #.#.# #.#.# #...# #...# #...#', 'N': '#..# ##.# ##.# #.## #.## #..# #..#',
    'O': '.##. #..# #..# #..# #..# #..# .##.', 'P': '###. #..# #..# ###. #... #... #...',
    'Q': '.##. #..# #..# #..# #..# #.#. .#.#', 'R': '###. #..# #..# ###. #.#. #..# #..#',
    'S': '.### #... #... .##. ...# ...# ###.', 'T': '##### ..#.. ..#.. ..#.. ..#.. ..#.. ..#..',
    'U': '#..# #..# #..# #..# #..# #..# .##.', 'V': '#...# #...# #...# .#.#. .#.#. ..#.. ..#..',
    'W': '#...# #...# #...# #.#.# #.#.# ##.## #...#', 'X': '#...# #...# .#.#. ..#.. .#.#. #...# #...#',
    'Y': '#...# #...# .#.#. ..#.. ..#.. ..#.. ..#..', 'Z': '#### ...# ...# ..#. .#.. #... ####',
    'a': '.... .... .##. ...# .### #..# .###', 'b': '#... #... ###. #..# #..# #..# ###.',
    'c': '... ... .## #.. #.. #.. .##', 'd': '...# ...# .### #..# #..# #..# .###',
    'e': '.... .... .##. #..# #### #... .##.', 'f': '.## #.. ### #.. #.. #.. #..',
    'g': '.... .... .### #..# #..# #..# .### ...# .##.', 'h': '#... #... ###. #..# #..# #..# #..#',
    'i': '# . # # # # #', 'j': '.# .. .# .# .# .# .# .# #.',
    'k': '#... #... #..# #.#. ##.. #.#. #..#', 'l': '#. #. #. #. #. #. .#',
    'm': '..... ..... ####. #.#.# #.#.# #.#.# #.#.#', 'n': '.... .... ###. #..# #..# #..# #..#',
    'o': '.... .... .##. #..# #..# #..# .##.', 'p': '.... .... ###. #..# #..# #..# ###. #... #...',
    'q': '.... .... .### #..# #..# #..# .### ...# ...#', 'r': '... ... #.# ##. #.. #.. #..',
    's': '... ... .## #.. .#. ..# ##.', 't': '.#. .#. ### .#. .#. .#. ..#',
    'u': '.... .... #..# #..# #..# #..# .###', 'v': '..... ..... #...# #...# .#.#. .#.#. ..#..',
    'w': '..... ..... #...# #...# #.#.# #.#.# .#.#.', 'x': '.... .... #..# #..# .##. #..# #..#',
    'y': '.... .... #..# #..# #..# #..# .### ...# .##.', 'z': '.... .... #### ..#. .#.. #... ####',
    # digits: tabular (all advance 5)
    '0': '.##. #..# #.## ##.# #..# #..# .##.', '1': '.#.. ##.. .#.. .#.. .#.. .#.. ###.',
    '2': '.##. #..# ...# ..#. .#.. #... ####', '3': '###. ...# ...# .##. ...# ...# ###.',
    '4': '..#. .##. #.#. #.#. #### ..#. ..#.', '5': '#### #... ###. ...# ...# #..# .##.',
    '6': '.##. #... #... ###. #..# #..# .##.', '7': '#### ...# ..#. ..#. .#.. .#.. .#..',
    '8': '.##. #..# #..# .##. #..# #..# .##.', '9': '.##. #..# #..# .### ...# ...# .##.',
    # punctuation
    ' ': '.. .. .. .. .. .. ..', '!': '# # # # # . #', '"': '#.# #.# ... ... ... ... ...',
    '#': '..... .#.#. ##### .#.#. ##### .#.#. .....', '$': '..#.. .#### #.#.. .###. ..#.# ####. ..#..',
    '%': '##... ##..# ...#. ..#.. .#... #..## ...##', '&': '.##.. #..#. .##.. .#.## #..#. #..#. .##.#',
    "'": '# # . . . . .', '(': '.# #. #. #. #. #. .# ..', ')': '#. .# .# .# .# .# #. ..',
    '*': '... #.# .#. ### .#. #.# ...', '+': '... ... .#. ### .#. ... ...',
    ',': '. . . . . . # #', '-': '... ... ... ### ... ... ...', '.': '. . . . . . #',
    '/': '..# ..# .#. .#. .#. #.. #..', ':': '. . . # . . #', ';': '.. .. .. .# .. .. .# #.',
    '<': '... ..# .#. #.. .#. ..# ...', '=': '... ... ### ... ### ... ...', '>': '... #.. .#. ..# .#. #.. ...',
    '?': '.##. #..# ...# ..#. .#.. .... .#..', '@': '.###. #...# #.### #.#.# #.##. #.... .###.',
    '[': '## #. #. #. #. #. ## ..', '\\': '#.. #.. .#. .#. .#. ..# ..#', ']': '## .# .# .# .# .# ## ..',
    '^': '.#. #.# ... ... ... ... ...', '_': '.... .... .... .... .... .... .... ####',
    '`': '#. .# .. .. .. .. ..', '{': '..# .#. .#. #.. .#. .#. ..#', '|': '# # # # # # # #',
    '}': '#.. .#. .#. ..# .#. .#. #..', '~': '.... .... .#.# #.#. .... .... ....',
    # GLYPH_* symbols on control codes (text.h)
    '\x01': '..... ....# ...## #.##. ###.. .#... .....',          # check mark
    '\x02': '..... ##.## .###. ..#.. .###. ##.## .....',          # cross
    '\x03': '... ... .#. ### .#. ... ...',                        # bullet
    '\x04': '..#.. .###. #.#.# ..#.. ..#.. ..#.. ..#..',          # arrow up
    '\x05': '..#.. ..#.. ..#.. ..#.. #.#.# .###. ..#..',          # arrow down
    # Latin-1 / cp1252 extras
    '\xa7': '.##. #... .##. #..# .##. ...# .##.',                 # section sign
    '\xb0': '.#. #.# .#. ... ... ... ...',                        # degree
    '\xbd': '#.... #..#. #.#.. .#.## #...# ...#. ..###',          # one half
    '\xb7': '. . . # . . .',                                      # middle dot
    '\xd7': '... ... #.# .#. #.# ... ...',                        # multiplication
    '\xab': '..... ..... .#.#. #.#.. .#.#. ..... .....',          # guillemets
    '\xbb': '..... ..... .#.#. ..#.# .#.#. ..... .....',
    '–': '.... .... .... #### .... .... ....',               # en dash (also minus)
    '—': '...... ...... ...... ###### ...... ...... ......',  # em dash
    '‘': '.# #. #. .. .. .. ..', '’': '.# .# #. .. .. .. ..',
    '“': '.#.# #.#. #.#. .... .... .... ....', '”': '.#.# .#.# #.#. .... .... .... ....',
    '…': '..... ..... ..... ..... ..... ..... #.#.#',          # ellipsis
    '•': '... ... .#. ### .#. ... ...',                      # bullet (cp1252 0x95)
}
# accented letters: base glyph + accent rows at ABSOLUTE cell rows (cell: accents 0..1, caps 2..8)
BODY_ACC = {
    '\xc4': ('A', {0: '#..#'}), '\xd6': ('O', {0: '#..#'}), '\xdc': ('U', {0: '#..#'}),
    '\xc9': ('E', {0: '..#.'}), '\xe4': ('a', {2: '#..#'}), '\xf6': ('o', {2: '#..#'}),
    '\xfc': ('u', {2: '#..#'}), '\xe9': ('e', {1: '..#.', 2: '.#..'}),
    '\xe5': ('a', {0: '.##.', 1: '#..#', 2: '.##.'}),
}
# full-cell overrides (11 rows)
BODY_CELL = {
    '\xc5': '.##. #..# .##. #..# #..# #### #..# #..# #..# .... ....',   # A-ring: ring bottom = A apex
}
BODY_LH, BODY_TOP, BODY_BASE = 11, 2, 9

# Headline font: hand drawn bold grotesque, 9 px caps, 2 px stems, 3 px descenders (newspaper,
# panel titles, brass name plate).  Rows cap-relative (0..8 caps, 9..11 descenders, x-height 3..8).
HEAD = {
    'A': '.####. ##..## ##..## ##..## ###### ##..## ##..## ##..## ##..##',
    'B': '#####. ##..## ##..## ##..## #####. ##..## ##..## ##..## #####.',
    'C': '.####. ##..## ##.... ##.... ##.... ##.... ##.... ##..## .####.',
    'D': '#####. ##..## ##..## ##..## ##..## ##..## ##..## ##..## #####.',
    'E': '##### ##... ##... ##... ####. ##... ##... ##... #####',
    'F': '##### ##... ##... ##... ####. ##... ##... ##... ##...',
    'G': '.####. ##..## ##.... ##.... ##.### ##..## ##..## ##..## .#####',
    'H': '##..## ##..## ##..## ##..## ###### ##..## ##..## ##..## ##..##',
    'I': '## ## ## ## ## ## ## ## ##',
    'J': '...## ...## ...## ...## ...## ...## ##.## ##.## .###.',
    'K': '##..## ##..## ##.##. ####.. ###... ####.. ##.##. ##..## ##..##',
    'L': '##... ##... ##... ##... ##... ##... ##... ##... #####',
    'M': '##...## ###.### ####### ##.#.## ##.#.## ##...## ##...## ##...## ##...##',
    'N': '##...## ###..## ###..## ####.## ##.#### ##..### ##..### ##...## ##...##',
    'O': '.####. ##..## ##..## ##..## ##..## ##..## ##..## ##..## .####.',
    'P': '#####. ##..## ##..## ##..## #####. ##.... ##.... ##.... ##....',
    'Q': '.####. ##..## ##..## ##..## ##..## ##..## ##..## ##..## .####. ....## ......',
    'R': '#####. ##..## ##..## ##..## #####. ####.. ##.##. ##..## ##..##',
    'S': '.####. ##..## ##.... ###... .####. ...### ....## ##..## .####.',
    'T': '###### ..##.. ..##.. ..##.. ..##.. ..##.. ..##.. ..##.. ..##..',
    'U': '##..## ##..## ##..## ##..## ##..## ##..## ##..## ##..## .####.',
    'V': '##..## ##..## ##..## ##..## ##..## ##..## .####. .####. ..##..',
    'W': '##...## ##...## ##...## ##.#.## ##.#.## ####### ####### ##...## #.....#',
    'X': '##..## ##..## .####. .####. ..##.. .####. .####. ##..## ##..##',
    'Y': '##..## ##..## ##..## .####. ..##.. ..##.. ..##.. ..##.. ..##..',
    'Z': '###### ....## ...##. ...##. ..##.. .##... .##... ##.... ######',
    'a': '...... ...... ...... .####. ....## .##### ##..## ##..## .#####',
    'b': '##.... ##.... ##.... #####. ##..## ##..## ##..## ##..## #####.',
    'c': '..... ..... ..... .#### ##... ##... ##... ##... .####',
    'd': '....## ....## ....## .##### ##..## ##..## ##..## ##..## .#####',
    'e': '...... ...... ...... .####. ##..## ###### ##.... ##.... .####.',
    'f': '.### ##.. ##.. #### ##.. ##.. ##.. ##.. ##..',
    'g': '...... ...... ...... .##### ##..## ##..## ##..## ##..## .##### ....## .####.',
    'h': '##.... ##.... ##.... #####. ##..## ##..## ##..## ##..## ##..##',
    'i': '## ## .. ## ## ## ## ## ##',
    'j': '.## .## ... .## .## .## .## .## .## .## ##.',
    'k': '##.... ##.... ##.... ##..## ##.##. ####.. ####.. ##.##. ##..##',
    'l': '## ## ## ## ## ## ## ## ##',
    'm': '........ ........ ........ #######. ##.##.## ##.##.## ##.##.## ##.##.## ##.##.##',
    'n': '...... ...... ...... #####. ##..## ##..## ##..## ##..## ##..##',
    'o': '...... ...... ...... .####. ##..## ##..## ##..## ##..## .####.',
    'p': '...... ...... ...... #####. ##..## ##..## ##..## ##..## #####. ##.... ##....',
    'q': '...... ...... ...... .##### ##..## ##..## ##..## ##..## .##### ....## ....##',
    'r': '..... ..... ..... ##.## ####. ###.. ##... ##... ##...',
    's': '..... ..... ..... .#### ##... .###. ...## ...## ####.',
    't': '.... ##.. ##.. #### ##.. ##.. ##.. ##.. .###',
    'u': '...... ...... ...... ##..## ##..## ##..## ##..## ##..## .#####',
    'v': '...... ...... ...... ##..## ##..## ##..## .####. .####. ..##..',
    'w': '........ ........ ........ ##....## ##....## ##.##.## ##.##.## ######## .##..##.',
    'x': '...... ...... ...... ##..## .####. ..##.. ..##.. .####. ##..##',
    'y': '...... ...... ...... ##..## ##..## ##..## ##..## ##..## .##### ....## .####.',
    'z': '..... ..... ..... ##### ...## ..##. .##.. ##... #####',
    '0': '.####. ##..## ##..## ##.### ###### ###.## ##..## ##..## .####.',
    '1': '..##.. .###.. ####.. ..##.. ..##.. ..##.. ..##.. ..##.. ######',
    '2': '.####. ##..## ....## ....## ...##. ..##.. .##... ##.... ######',
    '3': '.####. ##..## ....## ....## ..###. ....## ....## ##..## .####.',
    '4': '...##. ..###. .####. ##.##. ##.##. ###### ...##. ...##. ...##.',
    '5': '###### ##.... ##.... #####. ....## ....## ....## ##..## .####.',
    '6': '.####. ##..## ##.... #####. ##..## ##..## ##..## ##..## .####.',
    '7': '###### ....## ....## ...##. ...##. ..##.. ..##.. ..##.. ..##..',
    '8': '.####. ##..## ##..## ##..## .####. ##..## ##..## ##..## .####.',
    '9': '.####. ##..## ##..## ##..## .##### ....## ....## ##..## .####.',
    ' ': '... ... ... ... ... ... ... ... ...',
    '.': '.. .. .. .. .. .. .. ## ##', ',': '.. .. .. .. .. .. .. ## ## .# #.',
    ':': '.. .. .. ## ## .. .. ## ##', ';': '.. .. .. ## ## .. .. ## ## .# #.',
    '!': '## ## ## ## ## ## .. ## ##', '?': '.####. ##..## ....## ...##. ..##.. ..##.. ...... ..##.. ..##..',
    "'": '## ## ## .. .. .. .. .. ..', '"': '##.## ##.## ##.## ..... ..... ..... ..... ..... .....',
    '-': '.... .... .... .... #### .... .... .... ....',
    '(': '..## .##. ##.. ##.. ##.. ##.. ##.. .##. ..##', ')': '##.. .##. ..## ..## ..## ..## ..## .##. ##..',
    '/': '....## ....## ...##. ...##. ..##.. .##... .##... ##.... ##....',
    '\xa7': '.####. ##..## ##.... .####. ##..## .####. ....## ##..## .####.',
    '–': '...... ...... ...... ...... ###### ...... ...... ...... ......',
    '—': '.......... .......... .......... .......... ########## .......... .......... .......... ..........',
    '”': '.##.## .##.## .#..#. #..#.. ...... ...... ...... ...... ......',
    '“': '..#..# .#..#. ##.##. ##.##. ...... ...... ...... ...... ......',
    '%': '##...## ##..##. ...##.. ..##... .##.... ##..##. #...##. ....... .......',
}
HEAD_ACC = {
    '\xc4': ('A', {0: '##..##'}), '\xd6': ('O', {0: '##..##'}), '\xdc': ('U', {0: '##..##'}),
    '\xc9': ('E', {0: '..##.'}),
    '\xe4': ('a', {2: '##..##', 3: '##..##'}), '\xf6': ('o', {2: '##..##', 3: '##..##'}),
    '\xfc': ('u', {2: '##..##', 3: '##..##'}), '\xe9': ('e', {2: '...##.', 3: '..##..'}),
    '\xe5': ('a', {1: '..##..', 2: '.#..#.', 3: '..##..'}),
}
HEAD_CELL = {
    '\xc5': '..##.. .#..#. .####. ##..## ##..## ##..## ###### ##..## ##..## ##..## ##..## ...... ...... ......',
}
HEAD_LH, HEAD_TOP, HEAD_BASE = 14, 2, 11

# every font carries this character set
CHARSET = ([chr(c) for c in range(1, 6)] + [chr(c) for c in range(32, 127)] +
           ['\xc4', '\xd6', '\xc5', '\xe4', '\xf6', '\xe5', '\xc9', '\xe9', '\xdc', '\xfc', '\xa7', '\xb0', '\xbd',
            '\xb7', '\xd7', '\xab', '\xbb', '–', '—', '‘', '’', '“', '”', '…',
            '•'])


def char_code(ch):
    if ord(ch) < 32:
        return ord(ch)
    return ch.encode('cp1252')[0]


def rows_of(s):
    return s.split()


def cell_from(base_rows, top, lh):
    w = max(len(r) for r in base_rows)
    rows = ['.' * w] * top + [r.ljust(w, '.') for r in base_rows]
    rows += ['.' * w] * (lh - len(rows))
    return rows[:lh]


def overlay_rows(cell, acc):
    w = len(cell[0])
    out = list(cell)
    for r, pat in acc.items():
        off = (w - len(pat)) // 2
        row = list(out[r])
        for i, c in enumerate(pat):
            if c == '#':
                row[off + i] = '#'
        out[r] = ''.join(row)
    return out


def derive_bold(body_rows):
    """fallback headline glyph from a body glyph: 7 -> 9 rows (rows 1 and 5 doubled), emboldened"""
    cap = body_rows[:7]
    desc = body_rows[7:]
    rmap = [0, 1, 1, 2, 3, 4, 5, 5, 6]
    rows = [cap[i] for i in rmap] + [desc[i] for i in [0, 1, 1][:len(desc) and 3] if i < len(desc)]
    out = []
    for r in rows:
        w = len(r) + 1
        out.append(''.join('#' if (x < len(r) and r[x] == '#') or (x >= 1 and r[x - 1] == '#') else '.'
                           for x in range(w)))
    return out


def build_font_cells(kind):
    cells = {}
    if kind == 'body':
        defs, acc, full, top, lh = BODY, BODY_ACC, BODY_CELL, BODY_TOP, BODY_LH
    else:
        defs, acc, full, top, lh = HEAD, HEAD_ACC, HEAD_CELL, HEAD_TOP, HEAD_LH
    for ch in CHARSET:
        if ch in full:
            cells[ch] = cell_from(rows_of(full[ch]), 0, lh)
        elif ch in acc:
            b, a = acc[ch]
            src = defs.get(b) if b in defs else None
            cells[ch] = overlay_rows(cell_from(rows_of(src), top, lh), a)
        elif ch in defs:
            cells[ch] = cell_from(rows_of(defs[ch]), top, lh)
        elif kind == 'head' and ch in BODY:
            cells[ch] = cell_from(derive_bold(rows_of(BODY[ch])), top, lh)
        else:
            raise SystemExit('font %s: no glyph for %r' % (kind, ch))
    if kind == 'head':
        for ch in ('\x01', '\x02', '\x03', '\x04', '\x05', '•'):
            pass
    return cells


# font variants: (name, kind, colour, effect, effect colour)
FONT_VARIANTS = [
    ('body_dark', 'body', 'ink', None, None),          # on paper / cream bakelite
    ('body_light', 'body', 'cream_lt', None, None),    # on the dark top bar / ticker / bakelite frames
    ('head', 'head', 'ink', None, None),               # panel titles, brass plate, newspaper
    ('head_light', 'head', 'cream_lt', None, None),    # window title bands (dark bakelite)
    ('head_red', 'head', 'stamp', None, None),         # stamps, newspaper accents
    ('body_grey', 'body', 'grey', None, None),         # disabled / secondary on paper and keys
    ('body_red', 'body', 'red', None, None),           # negative money, missing requirement (paper)
    ('body_green', 'body', 'green', None, None),       # positive money, met requirement (paper)
    ('body_amber', 'body', 'amber_lt', None, None),    # ticker warning (on dark)
    ('body_red_lt', 'body', 'red_lt', None, None),     # ticker urgent (on dark)
    ('body_blue_lt', 'body', 'blue_lt', None, None),   # ticker political (on dark)
    ('body_brass', 'body', 'brass_lt', None, None),    # labels on teak (subtle) / top bar captions
    ('body_green_lt', 'body', 'green_lt', None, None), # positive deltas on the dark top bar
    ('body_shadow', 'body', 'cream_lt', 'shadow', 'teak_xdk'),   # labels on teak / over the map
    ('float_light', 'body', 'cream_xlt', 'outline', 'ink'),      # floating numbers over the map
    ('float_green', 'body', 'green_lt', 'outline', 'ink'),
    ('float_red', 'body', 'red_lt', 'outline', 'ink'),
]
FONT_SHEET = {'body': 'font_body', 'head': 'font_head'}


def render_glyph(cell, col, effect, ecol):
    """returns (img, ox, oy, adv) for one glyph cell"""
    w, h = len(cell[0]), len(cell)
    adv = w + 1
    if effect == 'outline':
        img = new(w + 2, h + 2)
        cv = Cv(img)
        for y in range(h):
            for x in range(w):
                if cell[y][x] == '#':
                    for dy in (-1, 0, 1):
                        for dx in (-1, 0, 1):
                            cv.put(x + 1 + dx, y + 1 + dy, ecol)
        for y in range(h):
            for x in range(w):
                if cell[y][x] == '#':
                    cv.put(x + 1, y + 1, col)
        return img, -1, -1, adv
    if effect == 'shadow':
        img = new(w + 1, h + 1)
        cv = Cv(img)
        for y in range(h):
            for x in range(w):
                if cell[y][x] == '#':
                    cv.put(x + 1, y + 1, ecol)
        for y in range(h):
            for x in range(w):
                if cell[y][x] == '#':
                    cv.put(x, y, col)
        return img, 0, 0, adv
    img = new(w, h)
    cv = Cv(img)
    for y in range(h):
        for x in range(w):
            if cell[y][x] == '#':
                cv.put(x, y, col)
    return img, 0, 0, adv


def build_fonts():
    """returns {sheet: (img, [font dict])}; font dict: name, lh, base, glyphs[(code, x, y, w, h, ox, oy, adv, img)]"""
    cells = {'body': build_font_cells('body'), 'head': build_font_cells('head')}
    out = {}
    for kind in ('body', 'head'):
        items = []
        fonts = []
        for (name, k, col, eff, ecol) in FONT_VARIANTS:
            if k != kind:
                continue
            f = {'name': name, 'kind': kind, 'lh': BODY_LH if kind == 'body' else HEAD_LH,
                 'base': BODY_BASE if kind == 'body' else HEAD_BASE, 'glyphs': [], 'effect': eff}
            for ch in CHARSET:
                img, ox, oy, adv = render_glyph(cells[kind][ch], P[col], eff, P[ecol] if ecol else None)
                f['glyphs'].append([char_code(ch), 0, 0, img.size[0], img.size[1], ox, oy, adv, img])
            fonts.append(f)
        # shelf pack all glyphs of all variants (variant by variant, row-wise)
        x = y = rowh = 0
        for f in fonts:
            for g in f['glyphs']:
                w, h = g[3], g[4]
                if x + w > ATLAS_W:
                    x, y, rowh = 0, y + rowh, 0
                g[1], g[2] = x, y
                x += w
                rowh = max(rowh, h)
        H = y + rowh
        sheet = new(ATLAS_W, H)
        for f in fonts:
            for g in f['glyphs']:
                sheet.paste(g[8], (g[1], g[2]))
            f['glyphs'].sort(key=lambda g: g[0])
        out[FONT_SHEET[kind]] = (sheet, fonts)
    return out


FONTS = {}       # name -> font dict (after build)


def text_w(s, font):
    f = FONTS[font]
    lut = f['lut']
    w = 0
    for ch in s:
        g = lut.get(char_code(ch)) if (ord(ch) < 256 or ch in '–—‘’“”…•') \
            else None
        if g is None:
            g = lut[ord('?')]
        w += g[7]
    return max(0, w - 1)


def text(cv, x, y, s, font):
    """draw s with the line top at (x, y) using the generated glyph images (honest preview)"""
    f = FONTS[font]
    lut = f['lut']
    x0 = x
    for ch in s:
        try:
            g = lut.get(char_code(ch))
        except UnicodeEncodeError:
            g = None
        if g is None:
            g = lut[ord('?')]
        cv.blit(g[8], x + g[5], y + g[6])
        x += g[7]
    return x - x0 - 1


# =========================================================================== FRAME PIECES
def teak_texture(w, h, seed=7, period=256):
    """teak veneer: horizontal wavy grain, 5 tones, ordered dither; seamless vertically (period)"""
    img = new(w, h)
    px = img.load()
    tones = [P['teak_xdk'], P['teak_dk'], P['teak_md'], P['teak'], P['teak_lt']]
    pc = lambda cell: period // cell
    for y in range(h):
        for x in range(w):
            warp = pnoise(x, y, 48, 32, seed + 1, 0, pc(32)) * 9.0 + pnoise(x, y, 12, 8, seed + 5, 0, pc(8)) * 2.0
            yy = y + warp
            n = pnoise(x, yy, 72, 2, seed, 0, period // 2) * 0.55
            n += pnoise(x, yy, 20, 4, seed + 2, 0, period // 4) * 0.30
            n += pnoise(x, y, 6, 1, seed + 3, 0, period) * 0.15
            n = (n - 0.5) * 1.5 + 0.5
            n += (BAYER4[y & 3][x & 3] / 16.0 - 0.5) * 0.16
            px[x, y % h] = tones[int(clamp(n * 4.2 - 0.1, 0, 4.0))]
    return img


def bakelite_box(face, lt, dk, outline, shadow=None, inset=False):
    """cream bakelite key: rounded 1 px outline, 2-step bevel. inset=True -> pressed (inverted bevel)"""
    def draw(cv, w, h):
        sw = 1 if shadow else 0
        x1, y1 = w - sw, h - sw
        cv.fill(1, 1, x1 - 1, y1 - 1, face)
        # outline with clipped corners
        cv.hline(1, x1 - 1, 0, outline)
        cv.hline(1, x1 - 1, y1 - 1, outline)
        cv.vline(0, 1, y1 - 1, outline)
        cv.vline(x1 - 1, 1, y1 - 1, outline)
        if not inset:
            cv.hline(1, x1 - 1, 1, lt)
            cv.vline(1, 1, y1 - 1, lt)
            cv.hline(2, x1 - 1, y1 - 2, dk)
            cv.vline(x1 - 2, 2, y1 - 1, dk)
            cv.put(1, y1 - 2, face)
            cv.put(x1 - 2, 1, face)
        else:
            cv.hline(1, x1 - 1, 1, dk)
            cv.vline(1, 1, y1 - 1, dk)
            cv.hline(2, x1 - 1, y1 - 2, lt)
            cv.vline(x1 - 2, 2, y1 - 1, lt)
            cv.put(1, y1 - 2, face)
            cv.put(x1 - 2, 1, face)
        if shadow:
            cv.hline(2, w, h - 1, shadow)
            cv.vline(w - 1, 2, h, shadow)
            cv.put(w - 1, h - 1, KEY)
            cv.put(w - 2, h - 1, shadow)
    return draw


BTN_STATES = {
    # state: face, light, dark, outline
    'normal': ('cream', 'cream_xlt', 'cream_dk', 'ink'),
    'hover': ('cream_lt', 'cream_xlt', 'cream_dk', 'ink'),
    'pressed': ('cream_dk', 'cream', 'cream_xdk', 'ink'),
    'disabled': ('cream_dk', 'cream', 'cream_xdk', 'cream_sh'),
    'selected': ('lit', 'lit_lt', 'lit_dk', 'ink'),
}


def make_frame():
    S = 'frame'
    # --- teak panel texture (tile vertically; crop to panel width) + left edge
    add(S, 'teak_tile', teak_texture(144, 128, period=128))

    def edge(cv, w, h):
        cv.vline(0, 0, h, P['ink'])
        cv.vline(1, 0, h, P['teak_hl'])
        cv.vline(2, 0, h, P['teak_lt'])
    img = new(3, 32)
    edge(Cv(img), 3, 32)
    add(S, 'panel_edge', img)

    # --- top bar (16 px): tile horizontally
    img = new(32, 16, P['bar'])
    cv = Cv(img)
    cv.hline(0, 32, 0, P['bar_hl'])
    cv.hline(0, 32, 1, P['bar_lt'])
    cv.hline(0, 32, 14, P['bar_dk'])
    cv.hline(0, 32, 15, P['bar_xdk'])
    add(S, 'bar_tile', img)
    img = new(2, 12)
    cv = Cv(img)
    cv.vline(0, 0, 12, P['bar_xdk'])
    cv.vline(1, 0, 12, P['bar_lt'])
    add(S, 'bar_sep', img)

    # recessed value well in the bar (3-slice, 12 tall)
    def well(cv, w, h):
        cv.fill(0, 0, w, h, P['bar_dk'])
        cv.hline(0, w, 0, P['bar_xdk'])
        cv.vline(0, 0, h, P['bar_xdk'])
        cv.hline(1, w, h - 1, P['bar_lt'])
        cv.vline(w - 1, 1, h, P['bar_lt'])
    make3h(S, 'bar_well', well, 2, 2, 16, 12)

    # calendar plate (paper with brass rim), 12 tall
    def cal(cv, w, h):
        cv.fill(1, 1, w - 1, h - 1, P['paper_lt'])
        cv.rect(0, 0, w, h, P['brass_dk'])
        cv.hline(1, w - 1, 1, P['cream_xlt'])
        cv.hline(1, w - 1, h - 2, P['paper_dk'])
        cv.put(0, 0, KEY)
        cv.put(w - 1, 0, KEY)
        cv.put(0, h - 1, KEY)
        cv.put(w - 1, h - 1, KEY)
        # (1.1: the binder-ring dots read as a stray ':' at 1x; removed)
    make3h(S, 'cal_plate', cal, 4, 2, 16, 12)

    # demand gauge well (R C I) 19 x 13 + full-height bar columns 4 x 11 (blit parts)
    img = new(19, 13)
    cv = Cv(img)
    cv.fill(0, 0, 19, 13, P['bar_xdk'])
    cv.hline(1, 19, 12, P['bar_lt'])
    cv.vline(18, 1, 13, P['bar_lt'])
    cv.fill(1, 1, 18, 12, P['bar_xdk'])
    for i in range(3):
        cv.fill(2 + i * 5, 1, 2 + i * 5 + 4, 12, mix(P['bar_xdk'], P['bar_dk'], 0.6))
    cv.hline(1, 18, 6, P['bar_hl'])          # zero line
    add(S, 'dem_well', img)
    for nm, c, cl in (('r', 'green', 'green_lt'), ('c', 'blue', 'blue_lt'), ('i', 'amber', 'amber_lt')):
        img = new(4, 11, P[c])
        cv = Cv(img)
        cv.vline(0, 0, 11, P[cl])
        cv.vline(3, 0, 11, mix(P[c], P['ink'], 0.35))
        add(S, 'dem_bar_' + nm, img)

    # speed keys (15 x 12) and menu key on the dark bar: normal / hover / on
    def small_key(state, sym):
        img = new(15, 12)
        cv = Cv(img)
        face, lt, dk, ol = BTN_STATES[{'normal': 'normal', 'hover': 'hover', 'on': 'selected'}[state]]
        if state == 'on':
            bakelite_box(P['lit'], P['lit_lt'], P['lit_dk'], P['bar_xdk'], inset=True)(cv, 15, 12)
        else:
            bakelite_box(P[face], P[lt], P[dk], P['bar_xdk'])(cv, 15, 12)
        o = 1 if state == 'on' else 0
        c = P['ink']
        if sym == 'pause':
            cv.fill(5 + o, 3 + o, 7 + o, 9 + o, c)
            cv.fill(8 + o, 3 + o, 10 + o, 9 + o, c)
        elif sym == 'menu':
            for yy in (3, 5, 7):
                cv.hline(4 + o, 11 + o, yy + o, c)
        else:
            n = int(sym)
            tw = n * 3 + (n - 1)
            x0 = (15 - tw) // 2 + o
            for k in range(n):
                xx = x0 + k * 4
                for j in range(3):
                    cv.vline(xx + j, 3 + j + o, 9 - j + o, c)
        return img
    for sym in ('pause', '1', '2', '3', 'menu'):
        for st in ('normal', 'hover', 'on'):
            if sym == 'menu' and st == 'on':
                st2 = 'pressed'
                img = small_key('on', sym)
                add(S, 'speed_menu_pressed', img)
                continue
            add(S, 'speed_%s_%s' % (sym, st), small_key(st, sym))

    # zoom keys (F2, DESIGN 14.4): 15 x 12 like the speed keys, magnifier with + / -, four states
    # (disabled = already at that zoom).  Suggested place: the top bar left of the speed keys (layout zoom_key).
    def zoom_key(state, sym):
        img = new(15, 12)
        cv = Cv(img)
        face, lt, dk, ol = BTN_STATES[state]
        bakelite_box(P[face], P[lt], P[dk], P['bar_xdk'], inset=(state == 'pressed'))(cv, 15, 12)
        o = 1 if state == 'pressed' else 0
        off = state == 'disabled'
        ink = P['cream_sh'] if off else P['ink']
        glass = P['cream_dk'] if off else P['blue_lt']
        glint = P['cream'] if off else P['cream_xlt']
        lens = ['..kkk..', '.kgggk.', 'kgg.ggk', 'kg...gk', 'kgg.ggk', '.kgggk.', '..kkk..']
        x0, y0 = 3 + o, 2 + o
        for yy, row in enumerate(lens):
            for xx, ch in enumerate(row):
                if ch == 'k':
                    cv.put(x0 + xx, y0 + yy, ink)
                elif ch == 'g' or ch == '.' and 0 < xx < 6 and 0 < yy < 6:
                    cv.put(x0 + xx, y0 + yy, glass)
        cv.put(x0 + 2, y0 + 2, glint)
        cv.hline(x0 + 2, x0 + 5, y0 + 3, ink)                  # minus / plus bar
        if sym == 'in':
            cv.vline(x0 + 3, y0 + 2, y0 + 5, ink)
        for k in range(3):                                     # handle, 2 px thick
            cv.put(x0 + 6 + k, y0 + 5 + k, ink)
            cv.put(x0 + 7 + k, y0 + 5 + k, ink)
        return img
    for sym in ('in', 'out'):
        for st in ('normal', 'hover', 'pressed', 'disabled'):
            add(S, 'btn_zoom_%s_%s' % (sym, st), zoom_key(st, sym))

    # --- ticker strip (14 px) + severity lamps
    img = new(32, 14, P['bar'])
    cv = Cv(img)
    cv.hline(0, 32, 0, P['bar_xdk'])
    cv.hline(0, 32, 1, P['bar_hl'])
    cv.hline(0, 32, 2, P['bar_lt'])
    cv.hline(0, 32, 13, P['bar_dk'])
    add(S, 'ticker_tile', img)
    for nm, c in (('off', 'lamp_off'), ('info', 'cream_dk'), ('warn', 'amber_lt'), ('urgent', 'red_lt'),
                  ('pol', 'blue_lt')):
        img = new(7, 7)
        cv = Cv(img)
        base = P[c]
        dk = mix(base, P['ink'], 0.45)
        rows = ['.kkkkk.', 'kbhbbbk', 'kbbbbbk', 'kbbbbdk', 'kbbbbdk', 'kbbddbk' if False else 'kdbbddk', '.kkkkk.']
        cm = {'k': P['bar_xdk'], 'b': base, 'h': P['cream_xlt'] if nm != 'off' else P['lamp_off_lt'], 'd': dk}
        cv.grid(0, 0, rows, cm)
        add(S, 'lamp_' + nm, img)

    # --- brass name plate 3-slice (16 tall) with screws
    def plate(cv, w, h):
        cv.fill(0, 0, w, h, P['brass'])
        for x in range(w):            # brushed horizontal streaks (periodic over 16 px middle)
            pass
        for y in range(2, h - 2):
            for x in range(2, w - 2):
                t = pnoise(x, y, 16, 1, 31, 0, 0)
                if t > 0.72:
                    cv.put(x, y, P['brass_lt'])
                elif t < 0.2:
                    cv.put(x, y, mix(P['brass'], P['brass_dk'], 0.5))
        cv.rect(0, 0, w, h, P['brass_xdk'])
        cv.hline(1, w - 1, 1, P['brass_hl'])
        cv.vline(1, 1, h - 1, P['brass_hl'])
        cv.hline(1, w - 1, h - 2, P['brass_dk'])
        cv.vline(w - 2, 1, h - 1, P['brass_dk'])
        for c in ((0, 0), (w - 1, 0), (0, h - 1), (w - 1, h - 1)):
            cv.put(c[0], c[1], KEY)
        for sx in (4, w - 5):          # screws
            cv.put(sx, h // 2 - 1, P['brass_dk'])
            cv.put(sx - 1, h // 2, P['brass_dk'])
            cv.put(sx, h // 2, P['brass_xdk'])
            cv.put(sx + 1, h // 2, P['brass_hl'])
            cv.put(sx, h // 2 + 1, P['brass_hl'])
    # middle must tile: draw with uniform streak rows -> regenerate the middle as periodic
    def plate2(cv, w, h):
        plate(cv, w, h)
        for y in range(2, h - 2):
            for x in range(7, w - 7):
                t = pnoise((x - 7) % 16, y, 8, 1, 31, 2, 0)
                c = P['brass']
                if t > 0.70:
                    c = P['brass_lt']
                elif t < 0.22:
                    c = mix(P['brass'], P['brass_dk'], 0.5)
                cv.put(x, y, c)
    make3h(S, 'plate', plate2, 7, 7, 16, 16)

    # --- bakelite buttons / keys: 9-slice per state (corners 3, edges 16); last px row/col = drop shadow
    for st, (face, lt, dk, ol) in BTN_STATES.items():
        inset = st in ('pressed', 'selected')
        make9(S, 'btn_' + st, bakelite_box(P[face], P[lt], P[dk], P[ol], shadow=P['teak_xdk'], inset=inset),
              3, 3, 4, 4, 16)

    # --- window frame (dark bakelite, paper interior): title band 18, sides 5, bottom 6
    def win(cv, w, h):
        cv.fill(0, 0, w, h, P['bak'])
        cv.rect(0, 0, w, h, P['bak_xdk'])
        cv.hline(1, w - 1, 1, P['bak_hl'])
        cv.vline(1, 1, h - 1, P['bak_lt'])
        cv.hline(1, w - 1, h - 2, P['bak_dk'])
        cv.vline(w - 2, 1, h - 1, P['bak_dk'])
        # title band groove
        cv.hline(2, w - 2, 2, P['bak_lt'])
        # paper interior (top 18, left 5, right 5, bottom 6) with an inner shadow
        x0, y0, x1, y1 = 5, 18, w - 5, h - 6
        cv.fill(x0, y0, x1, y1, P['paper'])
        cv.hline(x0 - 1, x1 + 1, y0 - 1, P['bak_xdk'])
        cv.vline(x0 - 1, y0 - 1, y1 + 1, P['bak_xdk'])
        cv.hline(x0 - 1, x1 + 1, y1, P['bak_hl'])
        cv.vline(x1, y0 - 1, y1 + 1, P['bak_hl'])
        cv.hline(x0, x1, y0, P['paper_dk'])
        cv.vline(x0, y0, y1, P['paper_dk'])
        for c in ((0, 0), (w - 1, 0), (0, h - 1), (w - 1, h - 1)):
            cv.put(c[0], c[1], KEY)
    make9(S, 'win', win, 6, 19, 6, 7, 32)
    # paper interior texture tile (subtle fibre), optional instead of a flat fill
    img = new(32, 32, P['paper'])
    cv = Cv(img)
    for y in range(32):
        for x in range(32):
            n = pnoise(x, y, 4, 2, 77, 8, 16)
            if n > 0.78:
                cv.put(x, y, P['paper_lt'])
            elif n < 0.16:
                cv.put(x, y, P['paper_dk'])
    add(S, 'paper_tile', img)

    # close button (13 x 13) in the title band: normal / hover / pressed
    for st in ('normal', 'hover', 'pressed'):
        img = new(13, 13)
        cv = Cv(img)
        face, lt, dk, ol = BTN_STATES[st]
        bakelite_box(P[face], P[lt], P[dk], P['bak_xdk'], inset=(st == 'pressed'))(cv, 13, 13)
        o = 1 if st == 'pressed' else 0
        for i in range(5):
            for d in (0, 1):
                cv.put(4 + i + o + d, 4 + i + o, P['ink'])
                cv.put(8 - i + o + d, 4 + i + o, P['ink'])
        add(S, 'close_' + st, img)

    # inset box on paper (aged paper, pencil border): numbers, graphs, lists
    def inset(cv, w, h):
        cv.fill(0, 0, w, h, P['paper_dk'])
        cv.hline(0, w, 0, P['card'])
        cv.vline(0, 0, h, P['card'])
        cv.hline(0, w, h - 1, P['paper_lt'])
        cv.vline(w - 1, 0, h, P['paper_lt'])
        cv.hline(1, w - 1, 1, mix(P['paper_dk'], P['card'], 0.5))
        cv.vline(1, 1, h - 1, mix(P['paper_dk'], P['card'], 0.5))
    make9(S, 'inset', inset, 2, 2, 1, 1, 16)

    # horizontal rules (tile): engraved rule on paper; thick double rule for the newspaper
    img = new(16, 2)
    cv = Cv(img)
    cv.hline(0, 16, 0, P['card'])
    cv.hline(0, 16, 1, P['paper_lt'])
    add(S, 'rule', img)
    img = new(16, 4)
    cv = Cv(img)
    cv.hline(0, 16, 0, P['ink'])
    cv.hline(0, 16, 1, P['ink'])
    cv.hline(0, 16, 3, P['ink'])
    add(S, 'rule_news', img)

    # tabs (index-card tabs on paper), 3-slice, 14 tall: normal / hover / selected
    def tab(st):
        def draw(cv, w, h):
            face = {'normal': P['paper_dk'], 'hover': P['paper_lt'], 'selected': P['paper']}[st]
            cv.fill(1, 1, w - 1, h, face)
            cv.hline(2, w - 2, 0, P['pencil'])
            cv.vline(0, 2, h, P['pencil'])
            cv.vline(w - 1, 2, h, P['pencil'])
            cv.put(1, 1, P['pencil'])
            cv.put(w - 2, 1, P['pencil'])
            cv.put(0, 0, KEY), cv.put(1, 0, KEY), cv.put(0, 1, KEY)
            cv.put(w - 1, 0, KEY), cv.put(w - 2, 0, KEY), cv.put(w - 1, 1, KEY)
            cv.hline(2, w - 2, 1, P['paper_lt'] if st != 'hover' else P['cream_xlt'])
            if st != 'selected':
                cv.hline(0, w, h - 1, P['pencil'])
        return draw
    for st in ('normal', 'hover', 'selected'):
        make3h(S, 'tab_' + st, tab(st), 3, 3, 16, 14)
    img = new(16, 1, P['pencil'])
    add(S, 'tab_line', img)

    # slider (tax): recessed track 3-slice (7 tall), filled part, knob 9 x 13 (normal / hover / pressed)
    def track(fillc):
        def draw(cv, w, h):
            cv.fill(1, 1, w - 1, h - 1, fillc)
            cv.hline(1, w - 1, 0, P['pencil'])
            cv.vline(0, 1, h - 1, P['pencil'])
            cv.hline(1, w - 1, h - 1, P['paper_lt'])
            cv.vline(w - 1, 1, h - 1, P['paper_lt'])
            cv.hline(1, w - 1, 1, mix(fillc, P['ink'], 0.3))
        return draw
    make3h(S, 'slider_track', track(P['card']), 2, 2, 16, 7)
    make3h(S, 'slider_fill', track(P['amber']), 2, 2, 16, 7)
    for st in ('normal', 'hover', 'pressed'):
        img = new(9, 13)
        cv = Cv(img)
        b = {'normal': 'brass', 'hover': 'brass_lt', 'pressed': 'brass_dk'}[st]
        rows = ['.kkkkkkk.', 'khhhhhhdk', 'khbbbbbdk', 'khbkbkbdk', 'khbbbbbdk', 'khbkbkbdk', 'khbbbbbdk',
                'khbkbkbdk', 'khbbbbbdk', 'khbbbbbdk', '.kbbbbbk.', '..kbbbk..', '...kkk...']
        cm = {'k': P['ink'], 'b': P[b], 'h': P['brass_hl'] if st != 'pressed' else P['brass'],
              'd': P['brass_dk'] if st != 'pressed' else P['brass_xdk']}
        cm2 = dict(cm)
        cv.grid(0, 0, rows, cm2)
        # grip lines
        for yy in (3, 5, 7):
            cv.hline(2, 7, yy, P['brass_dk'] if st != 'pressed' else P['brass_xdk'])
            cv.hline(2, 7, yy + 1, P['brass_hl'] if st != 'pressed' else P['brass'])
        add(S, 'slider_knob_' + st, img)

    # checkbox / radio 11 x 11: off/on x normal/hover/disabled
    for kind in ('check', 'radio'):
        for on in (0, 1):
            for st in ('normal', 'hover', 'disabled'):
                img = new(11, 11)
                cv = Cv(img)
                face = {'normal': P['cream_lt'], 'hover': P['cream_xlt'], 'disabled': P['paper_dk']}[st]
                ol = P['ink'] if st != 'disabled' else P['grey']
                mk = P['ink'] if st != 'disabled' else P['grey']
                if kind == 'check':
                    cv.fill(1, 1, 10, 10, face)
                    cv.rect(0, 0, 11, 11, ol)
                    cv.hline(1, 10, 1, P['cream_dk'])
                    cv.vline(1, 1, 10, P['cream_dk'])
                    if on:
                        for (x, y) in ((2, 5), (3, 6), (4, 7), (5, 6), (6, 5), (7, 4), (8, 3)):
                            cv.put(x, y, mk)
                            cv.put(x, y + 1, mk)
                else:
                    rows = ['...kkkkk...', '..kdddddk..', '.kdfffffffk'[:11], 'kdfffffffk.', 'kdffffffffk',
                            'kdffffffffk', 'kdffffffffk', '.kffffffffk', '.kfffffffk.', '..kfffffk..', '...kkkkk...']
                    rows = ['...kkkkk...', '.kkdddddkk.', '.kdfffffffk'[:10] + 'k', 'kdffffffffk',
                            'kdffffffffk', 'kdffffffffk', 'kdffffffffk', 'kfffffffffk', '.kfffffffk.',
                            '.kkfffffkk.', '...kkkkk...']
                    rows = ['..kkkkkkk..', '.kdddddddk.', 'kdfffffffk'[:0] + 'kdffffffffk', 'kdffffffffk',
                            'kdffffffffk', 'kdffffffffk', 'kdffffffffk', 'kdffffffffk', 'kdffffffffk',
                            '.kfffffffk.', '..kkkkkkk..']
                    cv.grid(0, 0, rows, {'k': ol, 'd': P['cream_dk'], 'f': face})
                    if on:
                        cv.fill(4, 3, 7, 8, mk)
                        cv.fill(3, 4, 8, 7, mk)
                add(S, '%s_%s_%s' % (kind, 'on' if on else 'off', st), img)

    # scroll arrows 11 x 11 (normal / pressed / disabled) + scrollbar track and thumb (vertical 3-slices)
    for d in ('up', 'down', 'left', 'right'):
        for st in ('normal', 'pressed', 'disabled'):
            img = new(11, 11)
            cv = Cv(img)
            face, lt, dk, ol = BTN_STATES[st]
            bakelite_box(P[face], P[lt], P[dk], P[ol], inset=(st == 'pressed'))(cv, 11, 11)
            o = 1 if st == 'pressed' else 0
            c = P['ink'] if st != 'disabled' else P['cream_xdk']
            for k in range(3):
                for t in range(-k, k + 1):
                    if d == 'up':
                        cv.put(5 + t + o, 4 + k + o, c)
                    elif d == 'down':
                        cv.put(5 + t + o, 6 - k + o, c)
                    elif d == 'left':
                        cv.put(4 + k + o, 5 + t + o, c)
                    else:
                        cv.put(6 - k + o, 5 + t + o, c)
            add(S, 'arrow_%s_%s' % (d, st), img)

    def sbtrack(cv, w, h):
        cv.fill(0, 0, w, h, P['paper_dk'])
        cv.vline(0, 0, h, P['card'])
        cv.vline(w - 1, 0, h, P['paper_lt'])
        cv.hline(0, w, 0, P['card'])
        cv.hline(0, w, h - 1, P['paper_lt'])
        for y in range(1, h - 1):
            if y % 2 == 0:
                cv.put(5, y, P['card'])
    make3v(S, 'sbar_track', sbtrack, 2, 2, 16, 11)
    make3v(S, 'sbar_thumb', bakelite_box(P['cream'], P['cream_xlt'], P['cream_dk'], P['ink']), 3, 3, 16, 11)

    # fly-out: dark bakelite frame, cream face (9-slice), row hover bar, pointer arrow, separator
    def fly(cv, w, h):
        cv.fill(0, 0, w, h, P['bak'])
        cv.rect(0, 0, w, h, P['bak_xdk'])
        cv.hline(1, w - 1, 1, P['bak_hl'])
        cv.vline(1, 1, h - 1, P['bak_lt'])
        cv.hline(1, w - 1, h - 2, P['bak_dk'])
        cv.vline(w - 2, 1, h - 1, P['bak_dk'])
        cv.fill(3, 3, w - 3, h - 3, P['cream'])
        cv.hline(3, w - 3, 3, P['cream_dk'])
        cv.vline(3, 3, h - 3, P['cream_dk'])
        for c in ((0, 0), (w - 1, 0), (0, h - 1), (w - 1, h - 1)):
            cv.put(c[0], c[1], KEY)
    make9(S, 'fly', fly, 4, 4, 4, 4, 16)

    def flyrow(face, edge):
        def draw(cv, w, h):
            cv.fill(0, 0, w, h, face)
            cv.hline(1, w - 1, 0, edge)
            cv.hline(1, w - 1, h - 1, edge)
            cv.vline(0, 1, h - 1, edge)
            cv.vline(w - 1, 1, h - 1, edge)
        return draw
    make3h(S, 'flyrow_hover', flyrow(P['lit_lt'], P['lit_dk']), 2, 2, 16, 17)
    make3h(S, 'flyrow_sel', flyrow(P['lit'], P['brass_dk']), 2, 2, 16, 17)
    img = new(6, 11)
    cv = Cv(img)
    for k in range(6):
        cv.vline(k, k, 11 - k, P['bak'])
        cv.put(k, k, P['bak_hl'])
        cv.put(k, 10 - k, P['bak_xdk'])
    add(S, 'fly_arrow', img)
    img = new(16, 2)
    cv = Cv(img)
    cv.hline(0, 16, 0, P['cream_dk'])
    cv.hline(0, 16, 1, P['cream_xlt'])
    add(S, 'fly_sep', img)

    # tooltip: index card (pale), ink border, 1 px offset shadow
    def tip(cv, w, h):
        cv.fill(1, 1, w - 2, h - 2, P['tip'])
        cv.rect(0, 0, w - 1, h - 1, P['ink'])
        cv.hline(2, w, h - 1, P['bar_dk'])
        cv.vline(w - 1, 2, h, P['bar_dk'])
        cv.hline(1, w - 2, 1, P['cream_xlt'])
        cv.put(w - 1, 0, KEY)
        cv.put(w - 1, 1, KEY)
        cv.put(0, h - 1, KEY)
        cv.put(1, h - 1, KEY)
    make9(S, 'tip', tip, 3, 3, 4, 4, 8)

    # President plaque: paper card on teak with brass corner tacks
    def plaque(cv, w, h):
        cv.fill(1, 1, w - 1, h - 1, P['paper'])
        cv.rect(0, 0, w, h, P['teak_xdk'])
        cv.hline(1, w - 1, 1, P['paper_lt'])
        cv.vline(1, 1, h - 1, P['paper_lt'])
        cv.hline(1, w - 1, h - 2, P['card'])
        cv.vline(w - 2, 1, h - 1, P['card'])
        for (x, y) in ((2, 2), (w - 4, 2), (2, h - 4), (w - 4, h - 4)):
            cv.put(x, y, P['brass_lt'])
            cv.put(x + 1, y, P['brass'])
            cv.put(x, y + 1, P['brass'])
            cv.put(x + 1, y + 1, P['brass_xdk'])
    make9(S, 'plaque', plaque, 5, 5, 5, 5, 16)
    # portrait frames (opaque cream_dk window with bevel): small 36 x 44, large 68 x 84
    for nm, (w, h) in (('s', (36, 44)), ('l', (68, 84))):
        img = new(w, h)
        cv = Cv(img)
        cv.fill(0, 0, w, h, P['paper_dk'])
        cv.rect(0, 0, w, h, P['card'])
        cv.hline(0, w, h - 1, P['paper_lt'])
        cv.vline(w - 1, 0, h, P['paper_lt'])
        cv.fill(2, 2, w - 2, h - 2, mix(P['cream_dk'], P['blue_lt'], 0.18))
        cv.rect(1, 1, w - 1, h - 1, P['pencil'])
        add(S, 'pres_frame_' + nm, img)

    # minimap brass bezel (3 px)
    def bezel(cv, w, h):
        cv.fill(0, 0, w, h, P['brass'])
        cv.rect(0, 0, w, h, P['brass_xdk'])
        cv.hline(1, w - 1, 1, P['brass_hl'])
        cv.vline(1, 1, h - 1, P['brass_lt'])
        cv.hline(1, w - 1, h - 2, P['brass_dk'])
        cv.vline(w - 2, 1, h - 1, P['brass_dk'])
        cv.rect(2, 2, w - 2, h - 2, P['brass_xdk'])
        cv.fill(3, 3, w - 3, h - 3, P['bar_xdk'])
        for c in ((0, 0), (w - 1, 0), (0, h - 1), (w - 1, h - 1)):
            cv.put(c[0], c[1], KEY)
    make9(S, 'mmap', bezel, 3, 3, 3, 3, 16)
    # minimap camera rectangle corners (5x5 L brackets, cream) for the view marker
    for nm, rows in (('tl', ['###..', '#....', '#....', '.....', '.....']),
                     ('tr', ['..###', '....#', '....#', '.....', '.....']),
                     ('bl', ['.....', '.....', '#....', '#....', '###..']),
                     ('br', ['.....', '.....', '....#', '....#', '..###'])):
        img = new(5, 5)
        Cv(img).grid(0, 0, rows, {'#': P['cream_xlt']})
        add(S, 'mmap_cam_' + nm, img)

    # speaker grille slot (3-slice, 5 tall): stack with 3 px gaps to fill slack panel space
    def slot(cv, w, h):
        cv.fill(1, 1, w - 1, h - 1, P['teak_xdk'])
        cv.hline(1, w - 1, 0, P['teak_xdk'])
        cv.hline(1, w - 1, h - 1, P['teak_hl'])
        cv.vline(0, 1, h - 1, P['teak_xdk'])
        cv.vline(w - 1, 1, h - 1, P['teak_hl'])
        cv.hline(1, w - 1, 1, P['bar_xdk'])
        cv.fill(2, 2, w - 2, h - 2, mix(P['teak_xdk'], P['bar_xdk'], 0.5))
    make3h(S, 'grille', slot, 3, 3, 16, 5)
    # brass maker's badge for the middle of the grille (drawn with the body font, ink on brass).
    # R1: game name LAEHIOE (was KAUPUNKI); 2 px taller so the capital umlauts (body rows 0-1) sit on the brass
    tw = text_w('L\xc4HI\xd6', 'body_dark')
    bw, bh = tw + 14, 13
    img = new(bw, bh)
    cv = Cv(img)
    plate(cv, bw, bh)
    cv.fill(2, 2, bw - 2, bh - 2, P['brass'])
    text(cv, 7, 2, 'L\xc4HI\xd6', 'body_dark')
    add(S, 'badge', img)

    # cursor (arrow, hotspot = anchor 0,0)
    rows = ['k..........', 'kk.........', 'kwk........', 'kwwk.......', 'kwwwk......', 'kwwwwk.....',
            'kwwwwwk....', 'kwwwwwwk...', 'kwwwwwwwk..', 'kwwwwwwwwk.', 'kwwwwwkkkkk', 'kwwkwwk....',
            'kwk.kwwk...', 'kk..kwwk...', 'k....kwwk..', '.....kwwk..', '......kk...']
    img = new(11, 17)
    Cv(img).grid(0, 0, rows, {'k': P['ink'], 'w': P['cream_xlt']})
    add(S, 'cursor_arrow', img)
    rows = ['....kk....', '...kwwk...', '...kwwk...', '...kwwkkk.', '.kkkwwkwwk', 'kwwkwwkwwk',
            'kwwkwwwwwk', 'kwwwwwwwwk', '.kwwwwwwwk', '.kwwwwwwk.', '..kwwwwwk.', '...kkkkk..']
    img = new(10, 12)
    Cv(img).grid(0, 0, rows, {'k': P['ink'], 'w': P['cream_xlt']})
    add(S, 'cursor_hand', img, 4, 0)


# =========================================================================== ICONS (13 x 13)
# k ink, d pencil grey, w cream highlight, r red, g green, b blue, y amber/ochre, o brass, s steel, e earth
ICON_CMAP = {'k': 'ink', 'd': 'pencil', 'w': 'cream_xlt', 'r': 'red', 'g': 'green', 'b': 'blue', 'y': 'amber',
             'o': 'brass', 's': 'steel', 'e': 'teak', 'l': 'blue_lt', 'c': 'card'}
ICONS = {}


def icon(name, s):
    rows = [r for r in s.strip('\n').split('\n')]
    rows = [r.strip() for r in rows]
    if len(rows) > 13 or any(len(r) > 13 for r in rows):
        print('warning: icon %s larger than 13x13 (cropped)' % name)
    rows = [r[:13] for r in rows[:13]]
    ICONS[name] = [r.ljust(13, '.') for r in rows] + ['.' * 13] * (13 - len(rows))


# ---- tool panel keys (explore set, refined)
icon('key_homes', '''
......k......
.....kkk.....
....kkkkk....
...kkkkkkk.k.
..kkkkkkkkkk.
.kkkkkkkkkkk.
..rrrrrrrrr..
..rwwrrrrrr..
..rwwrrrkkr..
..rrrrrrkkr..
..rrrrrrkkr..
.kkkkkkkkkkk.
''')
icon('key_shops', '''
.kkkkkkkkkkk.
krrwwrrwwrrwk
krrwwrrwwrrwk
.kkkkkkkkkkk.
..k.......k..
..k.bbbb.kk..
..k.bbbb.kk..
..k.bbbb.kk..
..k......kk..
..k......kk..
.kkkkkkkkkkk.
''')
icon('key_industry', '''
..........kk.
..........kk.
.........kkk.
.k...k...kkk.
.kk..kk..kkk.
.kkk.kkk.kkk.
.kkkkkkkkkkk.
.kykykykykkk.
.kkkkkkkkkkk.
.kykykykykkk.
.kkkkkkkkkkk.
''')
icon('key_power', '''
......kk.....
.....kk......
....kk.......
...kkkkkk....
.......kk....
......kk.....
.....kk......
....kkkkkk...
........kk...
.......kk....
......kk.....
.....k.......
''')
icon('key_water', '''
......b......
......b......
.....bbb.....
.....bbb.....
....bbbbb....
....bwbbb....
...bbwbbbb...
...bwbbbbb...
...bbbbbbb...
....bbbbb....
.....bbb.....
''')
icon('key_services', '''
.....kkk.....
....k...k....
....k...k....
.....kkk.....
......k......
...rrrrrrr...
...rrrwrrr...
...rrwwwrr...
...rrrwrrr...
...rrrrrrr...
''')
icon('key_parks', '''
......g......
.....ggg.....
....ggggg....
.....ggg.....
...ggggggg...
....ggggg....
..ggggggggg..
...ggggggg...
.ggggggggggg.
......k......
......k......
''')
icon('key_bulldoze', '''
.......kkkk..
.......kddk..
..kkkkkkddk..
..kyyyyykkk..
.kyyyyyyyyk..
.kyyyyyyyyyk.
kkkkkkkkkkkkk
kdkdkdkdkdkdk
.kkkkkkkkkkk.
''')
icon('key_query', '''
....kkkkk....
...kk...kk...
...kk...kk...
........kk...
.......kk....
......kk.....
......kk.....
.............
......kk.....
......kk.....
''')
icon('key_roads', '''
.....k.k.....
.....k.k.....
....kdwdk....
....kdddk....
...kddwddk...
...kdddddk...
..kdddwdddk..
..kdddddddk..
.kddddwddddk.
.kdddddddddk.
kddddddddddk.
''')

# ---- road classes (icon_r_<RoadDef.key>)
icon('r_grav', '''
.....d.d.....
.....d.d.....
....d.c.d....
....dc.cd....
...d.c.c.d...
...dc.c.cd...
..d.c.c.c.d..
..dc.c.c.cd..
.d.c.c.c.c.d.
.dc.c.c.c.cd.
d.c.c.c.c.c.d
''')
icon('r_road', '''
.....k.k.....
.....k.k.....
....kdwdk....
....kdddk....
...kddwddk...
...kdddddk...
..kdddwdddk..
..kdddddddk..
.kddddwddddk.
.kdddddddddk.
kdddddwddddk.
''')
icon('r_main', '''
.g...k.k...g.
.g...kyk...g.
ggg.kdydk.ggg
.k..kdydk..k.
...kddyddk...
.g.kddyddk.g.
gggkddyddkggg
.kkdddydddkk.
.kddddyddddk.
.kddddyddddk.
kdddddydddddk
''')
icon('r_mway', '''
....k.k.k....
....kdkdk....
...kdwkwdk...
...kddkddk...
..kddwkwddk..
..kdddkdddk..
.kddwdkdwddk.
.kdddkkkdddk.
kdddwdkdwdddk
kddddkkkddddk
kddddkkkddddk
''')
icon('r_rail', '''
.....k.k.....
....eeeee....
.....k.k.....
...eeeeeee...
....k...k....
..eeeeeeeee..
...k.....k...
.eeeeeeeeeee.
..k.......k..
eeeeeeeeeeeee
.k.........k.
''')
icon('powerline', '''
.k.........k.
kkk.......kkk
.kd.......dk.
.k.d.....d.k.
.k..dd.dd..k.
.k....d....k.
.k.........k.
.k.........k.
.k.........k.
.k.........k.
kkk.......kkk
''')

# ---- zones (icon_z_<zone>)
icon('z_houses', '''
......k......
.....kkk.....
....kkkkk....
...kkkkkkk.k.
..kkkkkkkkkk.
.kkkkkkkkkkk.
..yyyyyyyyy..
..ywwyyyyyy..
..ywwyyykky..
..yyyyyykky..
..yyyyyykky..
.kkkkkkkkkkk.
''')
icon('z_apartments', '''
.kkkkkkkkkkk.
.kdddddddddk.
.kdwdwdwdwdk.
.kdddddddddk.
.kdwdwdwdwdk.
.kdddddddddk.
.kdwdwdwdwdk.
.kdddddddddk.
.kdwdwdwdwdk.
.kdddddddddk.
.kdwdddkkddk.
.kdddddkkddk.
kkkkkkkkkkkkk
''')
ICONS['z_commercial'] = ICONS['key_shops']
ICONS['z_industry'] = ICONS['key_industry']
icon('dezone', '''
kk.kk.kk.kk..
k.........k..
.........r...
k.......r.k..
.......r.....
k.....r...k..
.....r.......
k...r.....k..
...r.........
k.r.......k..
.r...........
r.kk.kk.kk...
''')

# ---- buildings (icon_b_<BuildingDef.key>)
icon('b_town_hall', '''
......r......
......rr.....
......k......
.....kkk.....
.....kwk.....
...kkkkkkk...
...kdddddk...
.kkkkkkkkkkk.
.kdkdkdkdkdk.
.kdkdkdkdkdk.
.kdkdkkkdkdk.
.kdddkkkdddk.
kkkkkkkkkkkkk
''')
icon('b_coal_plant', '''
..dd.........
.dddd........
..dd..dd.....
.....dddd....
......dd.....
.......kk....
.......kk....
.k..k..kk....
.kk.kk.kk....
.kkkkkkkkkkk.
.kykykykykyk.
.kkkkkkkkkkk.
.kkkkkkkkkkk.
''')
icon('b_coal_station', '''
.dd....dd....
dddd..dddd...
.dd....dd....
..k.....k....
..k.....k....
..k.....k....
..k.kk..k....
.kkkkkkkkkkk.
.kdkdkkdkdkk.
.kdkdkkdkdkk.
.kkkkkkkkkkk.
.kkkkkkkkkkk.
''')
icon('b_hydro_plant', '''
.............
.kkkkkkkkkkk.
.kdddddddddk.
.kdkdkdkdkdk.
.kkkkkkkkkkk.
..kkkkkkkkk..
..kkkkkkkkk..
...kkkkkkk...
..bbwbbbwbb..
.bbbbbbbbbbb.
bwbbbwbbbwbbb
.bbbbbbbbbbb.
''')
icon('b_oil_plant', '''
.........kk..
.........kk..
.........kk..
...kkkkk.kk..
..kdddddkkk..
..kkkkkkkkk..
..kdddddkkk..
..kdyyydkkk..
..kdddddkkk..
..kdddddkkkk.
..kkkkkkkkkk.
''')
icon('b_peat_plant', '''
.......kk....
.......kk....
.......kk....
.k..k..kk....
.kk.kk.kk....
.kkkkkkkkk...
.kdkdkdkdk...
.kkkkkkkkk.e.
..........eee
........eeeee
.......eeeeee
''')
icon('b_nuclear_plant', '''
..kkkkk......
..kdddk......
...kdk.......
...kdk.......
..kdddk......
..kdddk..kkk.
.kdddddk.kdk.
.kdddddkkkdkk
.kdddddkdkdkk
.kdddddkdkkkk
kkkkkkkkkkkkk
''')
icon('b_pump', '''
.....kkk.....
....kdddk....
...kdddddk...
..kkkkkkkkk..
..kdddddddk..
..kdkkdkkdk..
..kdkkdkkdk..
kkkkkkkkkkkkk
.bbbbbbbbbbb.
bbwbbbbwbbbbb
.bbbbbbbbbbb.
''')
icon('b_well', '''
......k......
....kkkkk....
..kkkkkkkkk..
....k...k....
....k.k.k....
....k.k.k....
....k.b.k....
..kkkkkkkkk..
..kdkdkdkdk..
..kdddddddk..
..kkkkkkkkk..
''')
icon('b_water_tower', '''
..kkkkkkkkk..
.kdddddddddk.
kdddddddddddk
.kkkkkkkkkkk.
...kddddk....
.....kk......
.....kk......
.....kk......
.....kk......
.....kk......
.....kk......
....kkkk.....
''')
ICONS['b_water_tower'][4] = '....kdddk....'
icon('b_fire_station', '''
......r......
.....rr......
.....rrr.r...
....rrrrrr...
...rryrrrr...
...rryyrrrr..
..rrryyyrrr..
..rryyyyyrr..
..rryywyyrr..
...ryywwyr...
....ryyyr....
''')
icon('b_school', '''
.............
.kkkkk.kkkkk.
k.wwwwkwwww.k
k.dddwkwddd.k
k.wwwwkwwww.k
k.dddwkwddd.k
k.wwwwkwwww.k
k.dddwkwddd.k
k.wwwwkwwww.k
kkkkkkkkkkkkk
.....kkk.....
''')
icon('b_clinic', '''
.kkkkkkkkkkk.
.kwwwwwwwwwk.
.kwwwgggwwwk.
.kwwwgggwwwk.
.kwgggggggwk.
.kwgggggggwk.
.kwgggggggwk.
.kwwwgggwwwk.
.kwwwgggwwwk.
.kwwwwwwwwwk.
.kkkkkkkkkkk.
''')
icon('b_sauna', '''
...d..d......
..d..d.......
...d..d......
..d..d..k....
........k....
....kkkkk....
...keeeeek...
..keeeeeeek..
.kkkkkkkkkkk.
..keekkkeek..
..keekkkeek..
..kkkkkkkkk..
''')
icon('b_police', '''
.....kkk.....
..kkkbbbkkk..
.kbbbbbbbbbk.
.kbbbbybbbbk.
.kbbbyyybbbk.
.kbbbbybbbbk.
..kbbbbbbbk..
..kbbbbbbbk..
...kbbbbbk...
....kbbbk....
.....kkk.....
''')
icon('b_library', '''
.............
.kkk.........
.kyk.kk......
.kykkrrk.kkk.
.kykkrrk.kbkk
.kykkrrkkkbk.
.kykkrrkk.kbk
.kykkrrkk.kbk
.kykkrrkk..kb
.kkkkkkkk..kk
kkkkkkkkkkkkk
''')
ICONS['b_library'] = [
    '.............', '.kkk.kkk.....', '.kyk.krk.kkk.', '.kyk.krk.kbk.', '.kyk.krk.kbk.',
    '.kyk.krk.kbk.', '.kwk.kwk.kbk.', '.kyk.krk.kbk.', '.kyk.krk.kbk.', '.kkk.kkk.kkk.',
    'kkkkkkkkkkkkk', '.............', '.............']
icon('b_post', '''
.............
.......oo....
.....oo..o...
....o.....o..
...o..oo..o..
..o..o..o.o..
oo..o....o...
ooo.o...o....
.oo..ooo.....
..o..........
.............
''')
ICONS['b_post'] = [
    '.............', '.............', '....ooooo....', '..oo.....o...', '.o..ooo...o..', '.o.o...o..o..',
    '.o.o...o.o...', 'oo..ooo.oo...', 'ooo....o.....', '.ooooo.......', '..o..........', '.............',
    '.............']
icon('b_health_centre', '''
.kkkkkkkkkkk.
.kdddgggdddk.
.kdddgggdddk.
.kdgggggggdk.
.kdgggggggdk.
.kdddgggdddk.
.kdddgggdddk.
.kkkkkkkkkkk.
.kwkwkwkwkwk.
.kwkwkkkwkwk.
.kkkkkkkkkkk.
''')
icon('b_park', '''
...ggg.......
..ggggg......
..ggggg......
...ggg.......
....k........
....k........
....k........
......kkkkkk.
......kddddk.
.......k..k..
ggggggggggggg
''')
icon('b_sports_field', '''
..kkkkkkkkk..
.kgggggggggk.
kgggggkgggggk
kgggggkgggggk
kggggkkkggggk
kgggkgkgkgggk
kggggkkkggggk
kgggggkgggggk
kgggggkgggggk
.kgggggggggk.
..kkkkkkkkk..
''')
icon('b_ice_rink', '''
.............
..kk.........
..kk.........
..kkk........
..kkkkkk.....
..kkkkkkkk...
...........k.
.kkkkkkkkkkk.
.............
.lllllllllll.
lwllllllwllll
.lllllllllll.
''')
icon('b_church', '''
......k......
.....kkk.....
......k......
.....kkk.....
.....kdk.....
.....kdk.....
....kkdkk....
...kkdddkk...
..kkdddddkk..
.kkdddkdddkk.
.kdddkkkdddk.
.kdddkkkdddk.
kkkkkkkkkkkkk
''')
icon('b_element_factory', '''
kkkkkkkkkkkkk
.k.k.k.k.k.kk
..........k.k
..........k..
..........k..
.dddddd...k..
.dwdwdd...k..
.dddddd...k..
..........k..
.kkkkkkkkkkk.
.kdkdkdkdkdk.
.kkkkkkkkkkk.
''')
icon('b_station', '''
.kkkkkkkkkkk.
kkkkkkkkkkkkk
.k.........k.
.k..kkkkk..k.
.k.kwwwwwk.k.
.k.kwwkwwk.k.
.k.kkkkkkk.k.
.k.kdkkkdk.k.
.k.kkkkkkk.k.
kkkkkkkkkkkkk
.k.k.k.k.k.k.
''')

# ---- overlays (icon_o_<name>, order = OVL_* enum)
icon('o_none', '''
....kkkkk....
..kk.....kk..
.k.........k.
.k........kk.
k........k..k
k.......k...k
k......k....k
k.....k.....k
k....k......k
.k..k.......k
.kkk.......k.
..kk.....kk..
....kkkkk....
''')
icon('o_power', '''
.......yyk...
......yyk....
.....yyk.....
....yyk......
...yyyyyyk...
.......yyk...
......yyk....
.....yyk.....
....yyk......
...yyk.......
...yk........
''')
ICONS['o_water'] = ICONS['key_water']
icon('o_pollution', '''
...dd........
..dddd.dd....
.dddddddddd..
..dddddddd...
....dddd.....
......kk.....
......kk.....
......kk.....
......kk.....
....kkkkkk...
...kkkkkkkk..
''')
icon('o_land_value', '''
.....kkk.....
....kyyyk....
....kkkkk....
...kkkkkkk...
...kyyyyyk...
...kkkkkkk...
..kkkkkkkkk..
..kyyyyyyyk..
..kkkkkkkkk..
.kkkkkkkkkkk.
.kyyyyyyyyyk.
.kkkkkkkkkkk.
''')
icon('o_traffic', '''
...kkkkkkk...
..krrrrrrrk..
..krlllllrk..
.krrlllllrrk.
.krrrrrrrrrk.
kkrrrrrrrrrkk
kwwrrrrrrrwwk
krrrrrrrrrrrk
kkkkkkkkkkkkk
.kk.......kk.
''')
icon('o_fire_risk', '''
......k......
.....kwk.....
.....kwk.....
....kwrwk....
....kwrwk....
...kwrrrwk...
...kwryrwk...
..kwrryrrwk..
..kwryyyrwk..
.kwwwwwwwwwk.
.kkkkkkkkkkk.
''')
icon('o_growth', '''
.............
..gg....gg...
.gggg..gggg..
.ggggg.gggg..
..gggggggg...
....ggg......
......k......
......k......
......k......
....kkkkk....
...keeeeek...
...keeeeek...
....kkkkk....
''')
icon('o_zones', '''
kkkkkkkkkkkkk
kgggggkbbbbbk
kgggggkbbbbbk
kgggggkbbbbbk
kgggggkbbbbbk
kgggggkbbbbbk
kkkkkkkkkkkkk
kyyyyykrrrrrk
kyyyyykrrrrrk
kyyyyykrrrrrk
kyyyyykrrrrrk
kyyyyykrrrrrk
kkkkkkkkkkkkk
''')
ICONS['o_zones'] = ['kkkkkkkkkkkkk', 'kgggggkdddddk', 'kgggggkdddddk', 'kgggggkdddddk', 'kgggggkdddddk',
                    'kgggggkdddddk', 'kkkkkkkkkkkkk', 'kbbbbbkyyyyyk', 'kbbbbbkyyyyyk', 'kbbbbbkyyyyyk',
                    'kbbbbbkyyyyyk', 'kbbbbbkyyyyyk', 'kkkkkkkkkkkkk']
icon('o_catchment', '''
....kkkkk....
..kk.....kk..
.k...ddd...k.
.k..d...d..k.
k..d.....d..k
k..d..r..d..k
k..d.rrr.d..k
k..d..r..d..k
.k..d...d..k.
.k...ddd...k.
..kk.....kk..
....kkkkk....
''')
ICONS['o_traffic'] = ['.............', '...kkkkkkk...', '..klllllllk..', '.kllkllklllk.', '.krrrrrrrrrk.',
                      'krrrrrrrrrrrk', 'kwwrrrrrrrwwk', 'krrrrrrrrrrrk', 'kkkkkkkkkkkkk', '.kkk.....kkk.',
                      '.kkk.....kkk.', '.............', '.............']

# ---- misc / panels / actions
icon('budget', '''
.kkkkkkkkkkk.
.kwwwwwwwwwk.
.kwddddwyywk.
.kwwwwwwwwwk.
.kwddddwyywk.
.kwwwwwwwwwk.
.kwddddwyywk.
.kwwwwwwwwwk.
.kkkkkkkkkkk.
.kwddddwkkwk.
.kwwwwwwwwwk.
.kkkkkkkkkkk.
''')
icon('money', '''
....kkkkk....
...kyyyyyk...
...kkkkkkk...
...kyyyyyk...
..kkkkkkkkk..
..kyyyyyyyk..
.kkkkkkkkkk..
.kyyyyyyyyk..
.kkkkkkkkkk..
''')
ICONS['money'] = ICONS['o_land_value']
ICONS['population'] = ['...kkk.......', '..kdddk......', '..kdddk.kkk..', '...kkk.kdddk.', '..kkkkkkdddk.',
                       '.kdddddkkkk..', '.kdddddkkkkk.', '.kdddddkdddk.', '.kdddddkdddk.', '.kkdddkkdddk.',
                       '..kdddkkdddk.', '..kkkkkkkkk..', '.............']
icon('demand', '''
.............
.........yyy.
.........yyy.
.ggg.....yyy.
.ggg.....yyy.
.ggg.bbb.yyy.
.ggg.bbb.yyy.
.ggg.bbb.yyy.
kkkkkkkkkkkkk
.....rrr.....
.....rrr.....
''')
icon('graphs', '''
k............
k.........r..
k........r.r.
k...r...r....
k..r.r.r.....
k.r...r......
kr...........
k......ggg...
k...ggg...gg.
kgggg........
kkkkkkkkkkkkk
''')
icon('politics', '''
....wwwww....
....wdddw....
....wwwww....
..kkkkkkkkk..
.kkkkkwkkkkk.
.kdddddddddk.
.kdddddddddk.
.kddddoddddk.
.kdddddddddk.
.kdddddddddk.
.kkkkkkkkkkk.
''')
ICONS['politics'] = ['....kkkkk....', '....kwwwk....', '....kwdwk....', '..kkkwwwkkk..', '.kddkkkkkddk.',
                     '.kkkkkkkkkkk.', '.kdddddddddk.', '.kdddoooddddk'[:13], '.kdddoooddddk'[:13],
                     '.kdddddddddk.', '.kdddddddddk.', '.kkkkkkkkkkk.', '.............']
ICONS['politics'][7] = '.kddooooodk..'[:13]
ICONS['politics'][7] = '.kdddoooddk..'
ICONS['politics'][7] = '.kdddooodddk.'
ICONS['politics'][8] = '.kdddooodddk.'
icon('overlays', '''
......k......
....kkdkk....
..kkdddddkk..
kkdddddddddkk
..kkdddddkk..
k...kkdkk...k
.kk...k...kk.
k..kk...kk..k
.kk..kkk..kk.
...kk...kk...
.....kkk.....
''')
ICONS['overlays'] = ['......k......', '....kkwkk....', '..kkwwwwwkk..', 'kkwwwwwwwwwkk', '..kkwwwwwkk..',
                     'k...kkwkk...k', '.kk...k...kk.', 'k.dkk...kkd.k', '.kkddkkkddkk.', '...kkdddkk...',
                     '.....kkk.....', '.............', '.............']
icon('undo', '''
.............
...k.........
..kk.........
.kkkkkkkkk...
kkkkkkkkkkk..
.kkk......kk.
..kk.......k.
...k.......kk
...........kk
..........kk.
.......kkkk..
.......kkk...
''')
icon('redo', '''
.............
.........k...
.........kk..
...kkkkkkkkk.
..kkkkkkkkkkk
.kk......kkk.
.k.......kk..
kk.......k...
kk...........
.kk..........
..kkkk.......
...kkk.......
''')
icon('menu', '''
.............
.............
.kkkkkkkkkkk.
.kkkkkkkkkkk.
.............
.kkkkkkkkkkk.
.kkkkkkkkkkk.
.............
.kkkkkkkkkkk.
.kkkkkkkkkkk.
''')
ICONS['rotate'] = ['....kkkkk....', '..kk.....kk..', '.k.........k.', 'k...........k', 'k...........k',
                   'k...........k', 'k.........kkkkk'[:13], '.k.......kkkkk'[:13], '.k........kkk'[:13],
                   '..kk.......k.', '....kkkkk....', '.............', '.............']
ICONS['rotate'][6] = 'k.......kkkkk'
ICONS['rotate'][7] = '.k.......kkk.'
ICONS['rotate'][8] = '.k........k..'
icon('zoom_in', '''
...kkkkk.....
..kwwwwwk....
.kwwwkwwwk...
.kwwwkwwwk...
.kwkkkkkwk...
.kwwwkwwwk...
.kwwwkwwwk...
..kwwwwwk....
...kkkkkkk...
........kkk..
.........kkk.
..........kk.
''')
icon('zoom_out', '''
...kkkkk.....
..kwwwwwk....
.kwwwwwwwk...
.kwwwwwwwk...
.kwkkkkkwk...
.kwwwwwwwk...
.kwwwwwwwk...
..kwwwwwk....
...kkkkkkk...
........kkk..
.........kkk.
..........kk.
''')
icon('save', '''
.kkkkkkkkk...
.kwwwwwwwkk..
.kwddddddwkk.
.kwwwwwwwwwk.
.kwddddddwwk.
.kwwwwwwwwwk.
.kwddddwwwwk.
.kwwwwwwwwwk.
.kwwwwwwwwwk.
.kkkkkkkkkkk.
''')
ICONS['load'] = ['.............', '.kkkkk.......', 'kyyyyykkkkkk.', 'kyyyyyyyyyyk.', 'kyykkkkkkkkkk',
                 'kyk.......k.k', 'kyk.......k.k'[:13], 'kk.......k..k', 'kk......k...k'[:13], 'kkkkkkkkkkkkk',
                 '.............', '.............', '.............']
ICONS['load'] = ['.............', '.kkkkk.......', 'kyyyyykkkkkk.', 'kyyyyyyyyyyk.', 'kyykkkkkkkkkk',
                 'kyykyyyyyyyyk', 'kykyyyyyyyyk.', 'kykyyyyyyyyk.', 'kkyyyyyyyyk..', 'kkkkkkkkkkk..',
                 '.............', '.............', '.............']
icon('warning', '''
......k......
.....kyk.....
.....kyk.....
....kykyk....
....kykyk....
...kyykyyk...
...kyykyyk...
..kyyyyyyyk..
..kyyykyyyk..
.kyyyyyyyyyk.
.kkkkkkkkkkk.
''')
icon('info', '''
....kkkkk....
..kkbbbbbkk..
.kbbbbwbbbbk.
.kbbbbbbbbbk.
kbbbbwwbbbbbk
kbbbbbwbbbbbk
kbbbbbwbbbbbk
kbbbbbwbbbbbk
.kbbbwwwbbbk.
.kbbbbbbbbbk.
..kkbbbbbkk..
....kkkkk....
''')
icon('letter', '''
.............
.kkkkkkkkkkk.
.kkwwwwwwwkk.
.kwkwwwwwkwk.
.kwwkwwwkwwk.
.kwwwkrkwwwk.
.kwwwwrwwwwk.
.kwwwwwwwwwk.
.kkkkkkkkkkk.
''')
icon('decree', '''
..kkkkkkkk...
.kwwwwwwwwk..
.kwwkkkkwwk..
.kwkwwwwwwk..
.kwwkkkwwwk..
.kwkwwwkwwk..
.kwwkkkwwwk..
.kwwwwwkwwk..
.kwwkkkkwwk..
.kwwwwwwrrr..
.kwwwwwwrrrr.
..kkkkkkkrr..
''')
icon('newspaper', '''
.kkkkkkkkkkk.
.kwwwwwwwwwk.
.kwkkkkkkkwk.
.kwwwwwwwwwk.
.kwddwdddwwk.
.kwddwwwwwwk.
.kwddwdddwwk.
.kwwwwwwwwwk.
.kwdddwdddwk.
.kwwwwwwwwwk.
.kkkkkkkkkkk.
''')
icon('minimap', '''
kkkkkkkkkkkkk
kbbbbgggggggk
kbbbgggggdggk
kbbgggggdgggk
kbggggggdgggk
kggggggdggggk
kddddddddddk.
kgggggdgggggk
kgggggdggbbbk
kggggdggbbbbk
kkkkkkkkkkkkk
''')
ICONS['minimap'][6] = 'kdddddddddddk'
icon('buildings', '''
......k......
.....kwk.....
....kwwwk....
...kwwwwwk...
..kwwwwwwwk..
..kwkkwwkwk..
..kwkkwwwwk..
..kwwwwkkwk..
..kwwwwkkwk..
.kkkkkkkkkkk.
''')
icon('calendar', '''
..k.....k....
.kkkkkkkkkk..
.krrrrrrrrk..
.kkkkkkkkkk..
.kwwwwwwwwk..
.kwkwkwkwwk..
.kwwwwwwwwk..
.kwkwkwkwwk..
.kwwwwwwwwk..
.kkkkkkkkkk..
''')
icon('approval', '''
....kkkkk....
..kkyyyyykk..
.kyyyyyyyyyk.
.kyykyyykyyk.
kyyykyyykyyyk
kyyyyyyyyyyyk
kyykyyyyykyyk
kyyykkkkkyyyk
.kyyyyyyyyyk.
.kyyyyyyyyyk.
..kkyyyyykk..
....kkkkk....
''')
icon('pause', '''
.............
..kkk...kkk..
..kkk...kkk..
..kkk...kkk..
..kkk...kkk..
..kkk...kkk..
..kkk...kkk..
..kkk...kkk..
..kkk...kkk..
''')
icon('close', '''
.............
.kk.......kk.
.kkk.....kkk.
..kkk...kkk..
...kkk.kkk...
....kkkkk....
.....kkk.....
....kkkkk....
...kkk.kkk...
..kkk...kkk..
.kkk.....kkk.
.kk.......kk.
''')
icon('check', '''
.............
...........kk
..........kkk
.........kkk.
........kkk..
.kk....kkk...
.kkk..kkk....
..kkkkkk.....
...kkkk......
....kk.......
''')
ICONS['check'] = [r.replace('k', 'g') for r in ICONS['check']]
icon('cross', '''
.............
.rr.......rr.
.rrr.....rrr.
..rrr...rrr..
...rrr.rrr...
....rrrrr....
.....rrr.....
....rrrrr....
...rrr.rrr...
..rrr...rrr..
.rrr.....rrr.
.rr.......rr.
''')
icon('flag', '''
.k...........
.kwwwbbwwwww.
.kwwwbbwwwww.
.kbbbbbbbbbb.
.kbbbbbbbbbb.
.kwwwbbwwwww.
.kwwwbbwwwww.
.k...........
.k...........
.k...........
.k...........
kkk..........
''')
icon('services', '''
.kkkkkkkkkkk.
.kwwwwwwwwwk.
.kwkwwwkwwwk.
.kwwwwwwwwwk.
.kkkkkkkkkkk.
''')
ICONS['services'] = ICONS['key_services']
icon('trade', '''
.............
..r..........
.rrr.........
rrrrrkkkkk...
.rrr.........
..r..........
.............
.........b...
.........bb..
...kkkkkbbbbb
.........bb..
.........b...
''')
icon('elements', '''
.kkkkkkkkkkk.
.kdddddddddk.
.kdwwdddwwdk.
.kdwwdddwwdk.
.kdddddddddk.
.kkkkkkkkkkk.
.kdddddddddk.
.kdwwdddwwdk.
.kdwwdddwwdk.
.kdddddddddk.
.kkkkkkkkkkk.
''')

# ---- F2 (Milestone 2 fix round): pipe tool, persistent zone view toggle
icon('pipe', '''
.............
.....kkk.....
....krrrk....
.....kkk.....
.kk...k...kk.
kblkkkkkkkblk
kblwwwwwwwblk
kbllllllllblk
kblbbbbbbbblk
kblkkkkkkkblk
.kk.......kk.
''')
icon('zoneview', '''
......k......
.....kkk.....
....kkkkk....
...kkkkkkk...
....kwwwk....
....kwkwk....
....kkkkk....
..gg.....yy..
gg.........yy
..g.......y..
....gg.yy....
......y......
''')


# ---- M4 (Milestone 2 buildings): new building tools b_<key>, complexes, zones, bus line tool
icon('b_heat_plant', """
.dd..........
dddd...r..r..
.dd...r..r...
..kk...r..r..
..kk..r..r...
..kk.........
..kk.........
.kkkkkkkkkkk.
.kdkkdkkdkkk.
.kkkkkkkkkkk.
.krrrrrrrrrk.
.kkkkkkkkkkk.
""")
icon('b_sewage_works', """
....kkkkk....
..kklllllkk..
.kllllllllbk.
.klllkkkllbk.
kllllkwkllllk
kkkkkkkkkkkkk
kllllkwkllllk
.klllkkkllbk.
.kllllllllbk.
..kklllllkk..
....kkkkk....
""")
icon('b_bus_stop', """
.kkkkk.......
.kyyyk.......
.kykyk.......
.kyyyk.......
.kkkkk..kkkkk
...k...kddddk
...k...kl...k
...k...kl...k
...k...kleeek
...k...kl...k
..kkk..kkkkkk
""")
icon('b_bus_depot', """
.....kkk.....
...kkdddkk...
.kkdddddddkk.
kkkkkkkkkkkkk
kdk.......kdk
kdkkkkkkkkkdk
kdklllllllkdk
kdklllllllkdk
kdkrrrrrrrkdk
kdkcycccyckdk
kdkkkkkkkkkdk
kdkk.....kkdk
kkkkkkkkkkkkk
""")
icon('bus_line', """
.kkkkkkkkkkk.
kllklllklllkk
kllklllklllck
kccccccccccck
krrrrrrrrrrrk
kccccccccccck
kkkkkkkkkkkkk
.kek.....kek.
..k.......k..
.............
bb.bb.bb.bb.b
""")
icon('b_paper_mill', """
.w.w.........
wwwww........
.www.........
..kk.........
..kk..kkkkkk.
..kk.kwwwwwwk
.kkkkkwkwkwkk
.kwwwkwwwwwwk
.kwkwkkkkkkkk
.kwwwkkkkkkkk
.............
eyeyeye......
yeyeyey......
""")
icon('b_valtion_metalli', """
.........kkkk
.........k..k
.k..k..k.k..k
.kk.kk.kk...k
.kdkkdkkdk..k
.kddkddkddk.k
kkkkkkkkkkkkk
krrrrrrrrrrrk
krkkrkkrkkrrk
krkkrkkrkkrrk
krrrrrrrrrrrk
kkkkkkkkkkkkk
""")
icon('b_row_houses', """
.............
..k...k...k..
.kkk.kkk.kkk.
kdddkdddkdddk
kkkkkkkkkkkkk
.krrrrrrrrrk.
.krwkrwkrwkk.
.krwkrwkrwkk.
.krrrrrrrrrk.
.kkkkkkkkkkk.
""")
icon('b_concert_hall', """
......kkk....
.....kwwk....
....kwwwk....
...kwwwwk....
..kwwwwwk....
.kkwwwwwkkkkk
kwwwwwwwwwwwk
kwkwkwkwkwkwk
kwwwwwwwwwwwk
kkkkkkkkkkkkk
""")
icon('b_tv_tower', """
......k......
......k......
......r......
.....kwk.....
...kkkkkkk...
..kdddddddk..
...kkkkkkk...
.....kdk.....
.....kdk.....
.....kdk.....
.....kdk.....
....kdddk....
...kkkkkkk...
""")
icon('b_ski_jump', """
k............
kk...........
kdk..........
kddkk........
kd..kkk......
kd....kkb....
kdg......bb..
kdggg........
kdggggg......
kdggggggg....
kdggggggggggg
kkkkkkkkkkkkk
""")
icon('b_pres_sauna', """
wbwww.....d..
bbbbb....d...
wbwww.....d..
k......k.....
k......k.....
k....kkkkk...
k...keeeeek..
k..keeeeeeek.
k.kkkkkkkkkkk
k..keekkkeek.
k..keekkkeek.
kk.kkkkkkkkk.
""")
icon('b_friendship_statue', """
...k.....k...
..kok...kok..
...k.....k...
..kok...kok..
.kookkkkkook.
..kok...kok..
..kok...kok..
..k.k...k.k..
.kkkkkkkkkkk.
.kdddddddddk.
.kdddddddddk.
kkkkkkkkkkkkk
""")
icon('z_forestry', """
....k........
...kgk.......
...kgk.......
..kgggk......
..kgggk......
.kgggggk.....
.kgggggk.....
kgggggggk....
kkkkekkkk.kkk
....e....keek
....e....kyyk
.........kkkk
""")
icon('z_farm', """
....kkkkk....
...krrrrrk...
..krrrrrrrk..
.krrrwwwrrrk.
kkrrrwkwrrrkk
.krrrwwwrrrk.
.krrrkkkrrrk.
.krrrkdkrrrk.
.kkkkkkkkkkk.
.............
yyeyyeyyeyye.
eyyeyyeyyeyy.
""")


# ---- refined icons (auto ink outline around filled shapes, procedural road perspective)
def icon_ol(name, s):
    icon(name, s)
    rows = [list(r) for r in ICONS[name]]
    src = [r[:] for r in rows]
    for y in range(13):
        for x in range(13):
            if src[y][x] != '.':
                continue
            for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                qx, qy = x + dx, y + dy
                if 0 <= qx < 13 and 0 <= qy < 13 and src[qy][qx] not in '.k':
                    rows[y][x] = 'k'
                    break
    ICONS[name] = [''.join(r) for r in rows]


icon_ol('b_post', """
.............
.............
.....ooo.....
....o...o...o
...o.....o.oo
oooo.....oooo
...o.....o.oo
....o...o...o
.....ooo.....
""")
icon_ol('key_power', """
......yyy....
.....yyy.....
....yyy......
...yyyyyyy...
......yyy....
.....yyy.....
....yyy......
...yy........
..y..........
""")
ICONS['o_power'] = ICONS['key_power']
icon_ol('b_sports_field', """
ggggggwgggggg
ggggggwgggggg
wwgggwwwgggww
gwggwgwgwggwg
gwggwgwgwggwg
wwgggwwwgggww
ggggggwgggggg
ggggggwgggggg
""")
icon('o_pollution', """
..ddd.ddd....
.ddddddddd...
.dddddddddd..
..dddddddd...
....dddd.....
.....kk......
.....kk......
.....kk......
.k...kk......
.kk..kk......
.kkkkkkkkk...
.kykykykyk...
.kkkkkkkkk...
""")


def road_icon(name, edge, fillc, centre=None, dots=None):
    rows = [['.'] * 13 for _ in range(11)]
    for r in range(11):
        l, rr = 5 - r // 2, 7 + r // 2
        for x in range(l + 1, rr):
            rows[r][x] = fillc
            if dots and ihash(x, r, 3) < 0.35:
                rows[r][x] = dots
        rows[r][l] = edge
        rows[r][rr] = edge
        if centre and r % 2 == 0 and r > 0:
            rows[r][6] = centre
    ICONS[name] = [''.join(r) for r in rows] + ['.' * 13] * 2


road_icon('r_grav', 'e', 'c', None, 'd')
road_icon('r_road', 'k', 'd', 'w')
ICONS['key_roads'] = ICONS['r_road']

# Aliases: service overlays reuse the building pictograms; tool-key icons for zones etc.
OVL_ICON_ORDER = ['o_none', 'o_power', 'o_water', 'o_pollution', 'o_land_value', 'o_traffic', 'o_fire_risk',
                  'o_svc_fire', 'o_svc_school', 'o_svc_health', 'o_svc_sauna', 'o_svc_police', 'o_svc_library',
                  'o_svc_post', 'o_growth', 'o_zones', 'o_catchment']
for svc, b in (('fire', 'b_fire_station'), ('school', 'b_school'), ('health', 'b_clinic'), ('sauna', 'b_sauna'),
               ('police', 'b_police'), ('library', 'b_library'), ('post', 'b_post')):
    ICONS['o_svc_' + svc] = ICONS[b]
    ICONS['s_' + svc] = ICONS[b]


def centre_icon(rows):
    """centre the drawing's bounding box in the 13 x 13 cell"""
    xs = [x for r in rows for x, c in enumerate(r) if c != '.']
    ys = [y for y, r in enumerate(rows) if any(c != '.' for c in r)]
    if not xs:
        return rows
    x0, x1, y0, y1 = min(xs), max(xs), min(ys), max(ys)
    dx = (13 - (x1 - x0 + 1)) // 2 - x0
    dy = (13 - (y1 - y0 + 1) + 1) // 2 - y0
    out = [['.'] * 13 for _ in range(13)]
    for y, r in enumerate(rows):
        for x, c in enumerate(r):
            if c != '.' and 0 <= x + dx < 13 and 0 <= y + dy < 13:
                out[y + dy][x + dx] = c
    return [''.join(r) for r in out]


def make_icons():
    S = 'icons'
    for name in sorted(ICONS):
        rows = centre_icon([r[:13].ljust(13, '.') for r in ICONS[name][:13]])
        for off in (0, 1):
            img = new(13, 13)
            cv = Cv(img)
            for y, r in enumerate(rows):
                for x, ch in enumerate(r):
                    if ch == '.':
                        continue
                    if not off:
                        c = P[ICON_CMAP[ch]]
                    else:      # greyed: ink -> cream_sh, colours -> cream_xdk, highlights stay light
                        c = P['cream_sh'] if ch == 'k' else P['cream_lt'] if ch == 'w' else P['cream_xdk']
                    cv.put(x, y, c)
            add(S, 'icon_%s%s' % (name, '_off' if off else ''), img)


# =========================================================================== PRESIDENT (procedural caricature)
MOODS = ['content', 'stern', 'delighted', 'worried']


def portrait(W, H, mood):
    """affectionate caricature: very tall bald dome, round glasses, big ears, dark suit.
       Designed in a 64 x 80 unit space, rendered natively at W x H (no downsampling)."""
    s = W / 64.0
    img = new(W, H)
    cv = Cv(img)
    small = W < 48
    CX = 32.0
    # shape masks: 0 bg, 1 head, 2 ear, 3 neck, 4 shirt, 5 suit, 6 tie, 7 lapel
    lab = [[0] * W for _ in range(H)]
    shade = [[0.0] * W for _ in range(H)]
    L = (-0.48, -0.52, 0.71)

    def head_r(v):
        # egg: tall dome, wide temples, narrower jaw
        if v < 30:
            return 16.5 * math.sqrt(max(0.0, 1 - ((30 - v) / 25.0) ** 2.2))
        return 16.5 * math.sqrt(max(0.0, 1 - ((v - 30) / 21.0) ** 2.6)) * (1 - 0.18 * (v - 30) / 21.0)
    for py in range(H):
        for pxl in range(W):
            u = (pxl + 0.5) / s
            v = (py + 0.5) / s
            dx = abs(u - CX)
            # suit with sloped shoulders, lapels, shirt V, tie
            vt = 53.5 + (dx / 27.0) ** 2 * 11.0
            if v >= vt and dx < 31.5:
                vv = v - 53.0
                vw = max(0.0, 6.4 - vv * 0.30)              # half width of the shirt V
                if dx < vw:
                    lab[py][pxl] = 4
                    if dx < 1.3 + max(0.0, vv - 3) * 0.07 and vv > 1.6:
                        lab[py][pxl] = 6
                    shade[py][pxl] = 0.7 - (u - CX) / 30.0
                elif dx < vw + 4.2 and vv < 24:
                    lab[py][pxl] = 7
                    shade[py][pxl] = 0.62 - (u - CX) / 34.0 - vv / 90.0
                else:
                    lab[py][pxl] = 5
                    shade[py][pxl] = 0.52 - (u - CX) / 34.0 - vv / 70.0
            # shirt collar around the neck
            if 50.5 <= v < 55.5 and dx < 7.0 and v >= 50.5 + (7.0 - dx) * 0.25:
                lab[py][pxl] = 4
                shade[py][pxl] = 0.75 - (u - CX) / 30.0
            # neck
            elif 44 < v < 55 and dx < 5.6 and lab[py][pxl] == 0 or (44 < v < 52 and dx < 5.6):
                lab[py][pxl] = 3
                shade[py][pxl] = 0.22 if u > CX + 1 else 0.34
            # ears
            for ex in (CX - 16.8, CX + 16.8):
                if ((u - ex) / 3.2) ** 2 + ((v - 33.5) / 5.2) ** 2 <= 1.0:
                    lab[py][pxl] = 2
                    shade[py][pxl] = 0.55 if ex < CX else 0.3
            r = head_r(v)
            if abs(u - CX) <= r and 5 <= v <= 51:
                lab[py][pxl] = 1
                nx = (u - CX) / 17.0
                ny = (v - 27) / 26.0
                nz = math.sqrt(max(0.0, 1 - nx * nx - ny * ny * 0.7))
                d = -(nx * L[0] + ny * L[1] + nz * L[2])
                shade[py][pxl] = clamp(0.18 + 0.82 * clamp(nx * L[0] + ny * L[1] + nz * L[2]))
    # colours
    skin = [P['skin_xdk'], P['skin_dk'], P['skin'], P['skin_lt'], P['skin_hl']]
    suit = [P['suit_dk'], P['suit'], P['suit_lt'], P['suit_hl']]
    for py in range(H):
        for pxl in range(W):
            k = lab[py][pxl]
            t = shade[py][pxl] + (BAYER4[py & 3][pxl & 3] / 16.0 - 0.5) * (0.05 if small else 0.07)
            if k in (1, 2, 3):
                lv = int(clamp(t * 4.6, 0, 3.0))
                if k == 1 and t > 0.86:
                    lv = 4
                if k == 3:
                    lv = min(lv, 1)
                cv.put(pxl, py, skin[lv])
            elif k == 4:
                cv.put(pxl, py, P['shirt'] if t > 0.35 else P['shirt_dk'])
            elif k == 6:
                cv.put(pxl, py, P['tie_lt'] if t > 0.5 else P['tie'])
            elif k == 5:
                cv.put(pxl, py, suit[int(clamp(t * 2.6, 0, 2))])
            elif k == 7:
                cv.put(pxl, py, suit[int(clamp(t * 2.6 + 1.0, 1, 3))])
    # outline: label boundary against background (and head against ear/neck/suit)
    out = []
    for py in range(H):
        for pxl in range(W):
            k = lab[py][pxl]
            if k == 0:
                continue
            for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                qx, qy = pxl + dx, py + dy
                kk = lab[qy][qx] if 0 <= qx < W and 0 <= qy < H else 0
                if kk == 0 or (k == 1 and kk in (2, 3, 5, 7, 4) and dy >= 0 and not (kk == 2 and small)) or \
                        (k == 2 and kk == 3) or (k in (4, 6) and kk in (5, 7) and not small):
                    out.append((pxl, py))
                    break
    for (x, y) in out:
        cv.put(x, y, P['ink'])
    # lapel edges (suit_dk) and the tie outline
    for py in range(1, H - 1):
        for pxl in range(1, W - 1):
            if lab[py][pxl] == 7 and (lab[py][pxl + 1] == 5 or lab[py][pxl - 1] == 5):
                cv.put(pxl, py, P['suit_dk'])
            if lab[py][pxl] == 4 and (lab[py][pxl + 1] == 7 or lab[py][pxl - 1] == 7) and not small:
                cv.put(pxl, py, P['suit_dk'])

    def U(u):
        return int(round(u * s - 0.5))

    def dot(u, v, c):
        cv.put(U(u), U(v), c)

    def line(u0, v0, u1, v1, c, steps=None):
        n = steps or int(max(abs(u1 - u0), abs(v1 - v0)) * s * 2) + 1
        for i in range(n + 1):
            t = i / float(n)
            dot(lerp(u0, u1, t), lerp(v0, v1, t), c)

    def circle(cu, cv_, r, c, thick=1.0):
        rr = r * s
        for py in range(H):
            for pxl in range(W):
                d = math.hypot(pxl + 0.5 - cu * s, py + 0.5 - cv_ * s)
                if rr - thick * 0.5 - 0.25 <= d <= rr + thick * 0.5 + 0.05:
                    cv.put(pxl, py, c)

    # hair: thin grey fringe above the ears at the back of the head
    for py in range(H):
        for pxl in range(W):
            u = (pxl + 0.5) / s
            v = (py + 0.5) / s
            if lab[py][pxl] == 1 and 25 < v < 34 and abs(u - CX) > head_r(v) - (2.6 if not small else 2.0):
                if (pxl, py) not in out:
                    cv.put(pxl, py, P['hair'] if u < CX else P['hair_dk'])
    # wrinkles on the forehead (subtle), cheek shading
    if not small:
        for v0, w0 in ((17.5, 6.5), (20.5, 7.5)):
            line(CX - w0, v0 + 0.6, CX + w0, v0 + 0.6, P['skin_dk'])
    # glasses
    gl_v = 32.0
    gx = 7.2
    gr = 5.0 if not small else 5.2
    # eyes
    ey = gl_v + 0.4
    if mood == 'delighted':
        for side in (-1, 1):
            cx_ = CX + side * gx
            line(cx_ - 2.2, ey + 0.8, cx_, ey - 0.6, P['ink'])
            line(cx_, ey - 0.6, cx_ + 2.2, ey + 0.8, P['ink'])
    else:
        for side in (-1, 1):
            cx_ = CX + side * gx
            ox = 0.4 if mood != 'worried' else 0.0
            if small:
                dot(cx_ + ox * side * 0 + 0.2, ey, P['ink'])
            else:
                cv.fill(U(cx_ - 0.6), U(ey - 1.0), U(cx_ - 0.6) + 2, U(ey - 1.0) + 2, P['ink'])
                dot(cx_ - 1.6, ey - 1.2, P['skin_hl'])
            if mood == 'worried' and not small:
                dot(cx_, ey + 2.4, P['skin_dk'])
    for side in (-1, 1):
        circle(CX + side * gx, gl_v, gr, P['ink'], 1.0 if small else 1.4)
        # glint
        dot(CX + side * gx - gr * 0.45, gl_v - gr * 0.55, P['cream_xlt'])
    line(CX - gx + gr, gl_v - 0.5, CX + gx - gr, gl_v - 0.5, P['ink'])        # bridge
    line(CX - gx - gr, gl_v - 0.8, CX - 16.0, gl_v - 1.3, P['ink'])           # temples to the ears
    line(CX + gx + gr, gl_v - 0.8, CX + 16.0, gl_v - 1.3, P['ink'])
    # brows (thin, grey) by mood
    by = gl_v - gr - 2.2
    bc = P['hair_dk']
    for side in (-1, 1):
        x_in, x_out = CX + side * (gx - 3.2), CX + side * (gx + 3.4)
        if mood == 'stern':
            line(x_in, by + 1.6, x_out, by - 0.4, bc)
        elif mood == 'worried':
            line(x_in, by - 1.2, x_out, by + 1.0, bc)
        elif mood == 'delighted':
            line(x_in, by - 0.6, CX + side * gx, by - 1.6, bc)
            line(CX + side * gx, by - 1.6, x_out, by - 0.4, bc)
        else:
            line(x_in, by, CX + side * gx, by - 0.8, bc)
            line(CX + side * gx, by - 0.8, x_out, by, bc)
    # nose: long, shadow on the right side
    if small:
        dot(CX + 0.3, 37.5, P['skin_dk'])
        dot(CX + 0.9, 39.6, P['skin_xdk'])
        dot(CX - 0.9, 39.6, P['skin_dk'])
    else:
        line(CX + 0.6, 34.5, CX + 1.8, 39.8, P['skin_dk'])
        line(CX - 1.6, 40.6, CX + 1.8, 40.6, P['skin_xdk'])
        dot(CX - 1.2, 37.0, P['skin_hl'])
        dot(CX - 1.2, 38.2, P['skin_hl'])
    # mouth by mood
    my = 44.6
    mw = 5.6
    mc = P['mouth']
    if mood == 'content':
        line(CX - mw, my - 0.8, CX - 2, my + 0.6, mc)
        line(CX - 2, my + 0.6, CX + 2, my + 0.6, mc)
        line(CX + 2, my + 0.6, CX + mw, my - 0.8, mc)
    elif mood == 'stern':
        line(CX - mw + 0.6, my + 0.4, CX + mw - 0.6, my + 0.4, mc)
        if not small:
            dot(CX - mw + 0.2, my + 1.2, P['skin_dk'])
            dot(CX + mw - 0.2, my + 1.2, P['skin_dk'])
    elif mood == 'delighted':
        if small:
            line(CX - mw, my - 1.4, CX + mw, my - 1.4, mc)
            line(CX - mw + 1.6, my + 1.2, CX + mw - 1.6, my + 1.2, mc)
            line(CX - mw + 1.6, my, CX + mw - 1.6, my, P['teeth'])
        else:
            for v in range(0, 5):
                vv = my - 1.6 + v * 0.9
                half = mw * math.sqrt(max(0.0, 1 - ((vv - (my - 1.6)) / 4.4) ** 2))
                line(CX - half, vv, CX + half, vv, mc)
            line(CX - mw + 1.0, my - 1.0, CX + mw - 1.0, my - 1.0, P['teeth'])
            line(CX - mw - 1.0, my - 2.2, CX - mw - 2.0, my + 0.6, P['skin_dk'])
            line(CX + mw + 1.0, my - 2.2, CX + mw + 2.0, my + 0.6, P['skin_dk'])
    else:   # worried: small wavy down curve
        line(CX - mw + 0.6, my + 1.2, CX - 1.5, my - 0.2, mc)
        line(CX - 1.5, my - 0.2, CX + 1.5, my - 0.2, mc)
        line(CX + 1.5, my - 0.2, CX + mw - 0.6, my + 1.2, mc)
        if not small:
            dot(CX + 9.5, 24.0, P['blue_lt'])                         # a bead of sweat
            dot(CX + 9.5, 25.0, P['blue_lt'])
            dot(CX + 9.0, 25.2, P['blue_lt'])
    # shirt collar points + suit buttons
    if not small:
        line(CX - 1.0, 56.0, CX - 6.0, 59.0, P['shirt_dk'])
        line(CX + 1.0, 56.0, CX + 6.0, 59.0, P['shirt_dk'])
    # dome highlight (shine) - top left of the head
    hx, hy = CX - 6.0, 13.0
    if small:
        dot(hx, hy, P['skin_hl'])
        dot(hx + 1.6, hy - 1.2, P['skin_hl'])
        dot(hx - 1.6, hy + 1.6, P['skin_hl'])
    else:
        for (a, b) in ((0, 0), (1, -1), (2, -1.5), (-1, 1), (-1.6, 2.2), (0.6, 0.4), (3.2, -1.8)):
            dot(hx + a, hy + b, P['skin_hl'])
    return img


def make_pres():
    S = 'pres'
    for m in MOODS:
        add(S, 'pres_s_' + m, portrait(32, 40, m))
        add(S, 'pres_l_' + m, portrait(64, 80, m))
    make_masthead()
    make_stamps()


def find_font(names):
    dirs = ['/System/Library/Fonts/Supplemental', '/System/Library/Fonts', '/Library/Fonts']
    for n in names:
        for d in dirs:
            p = os.path.join(d, n)
            if os.path.exists(p):
                return p
    return None


def make_masthead():
    """newspaper masthead: 'Maakunnan Sanomat' (made-up provincial daily) in a bold Didone, thresholded (no
       anti-aliasing), double rules.  Falls back to the 2x headline font if no system serif is found."""
    W, H = 600, 54
    img = new(W, H)
    cv = Cv(img)
    title = 'Maakunnan Sanomat'
    fp = find_font(['Bodoni 72.ttc', 'Didot.ttc', 'Times New Roman Bold.ttf'])
    done = False
    if fp:
        for idx in (1, 2, 0):
            try:
                f = ImageFont.truetype(fp, 40, index=idx)
            except Exception:
                continue
            name = ' '.join(f.getname()).lower()
            if 'bold' not in name and idx != 0:
                continue
            m = Image.new('L', (W * 2, 80), 0)
            d = ImageDraw.Draw(m)
            d.fontmode = '1'
            d.text((4, 4), title, font=f, fill=255)
            bb = m.getbbox()
            if not bb:
                continue
            m = m.crop(bb)
            if m.size[0] > W - 20:
                continue
            ox = (W - m.size[0]) // 2
            oy = 6 + (34 - m.size[1]) // 2
            mp = m.load()
            for y in range(m.size[1]):
                for x in range(m.size[0]):
                    if mp[x, y] >= 128:
                        cv.put(ox + x, oy + y, P['ink'])
            done = True
            break
    if not done:
        tw = text_w(title, 'head')
        big = new(tw + 2, HEAD_LH)
        text(Cv(big), 0, 0, title, 'head')
        big = big.resize((big.size[0] * 2, big.size[1] * 2), Image.NEAREST)
        cv.blit(big, (W - big.size[0]) // 2, 4)
    # rules
    cv.hline(0, W, 0, P['ink'])
    cv.hline(0, W, 2, P['ink'])
    cv.hline(0, W, H - 12, P['ink'])
    cv.hline(0, W, H - 1, P['ink'])
    # small caps tag line between the rules
    tag = 'INDEPENDENT PROVINCIAL DAILY  \xb7  FOUNDED 1919'
    tw = text_w(tag, 'body_dark')
    text(cv, (W - tw) // 2, H - 12, tag, 'body_dark')
    # ornament dots either side of the title
    for side in (-1, 1):
        x = W // 2 + side * 268
        cv.fill(x - 2, 20, x + 2, 24, P['ink'])
    add('pres', 'masthead', img)


def worn(cv, w, h, seed, amount=0.16):
    """rubber-stamp wear: knock out some ink pixels in clumps (deterministic)"""
    for y in range(h):
        for x in range(w):
            if cv.get(x, y) != KEY:
                n = pnoise(x, y, 3, 3, seed) * 0.7 + ihash(x, y, seed) * 0.3
                if n < amount:
                    cv.put(x, y, KEY)


def stamp(word, sub, seed, big=False):
    f = 'head_red'
    tw = text_w(word, f)
    sw = text_w(sub, 'body_red') if sub else 0
    w = max(tw, sw) + 14
    h = 22 + (11 if sub else 0)
    img = new(w, h)
    cv = Cv(img)
    c = P['stamp']
    cv.rect(0, 0, w, h, c)
    cv.rect(1, 1, w - 1, h - 1, c)
    cv.rect(3, 3, w - 3, h - 3, c)
    for p in ((0, 0), (w - 1, 0), (0, h - 1), (w - 1, h - 1)):
        cv.put(p[0], p[1], KEY)
    text(cv, (w - tw) // 2, 4, word, f)
    if sub:
        text(cv, (w - sw) // 2, 17, sub, 'body_red')
    worn(cv, w, h, seed)
    return img


def stamp_spaced(word, seed, sp):
    """R1: letter-spaced one-word stamp (title screen "PROJECT" above LAEHIOE), same box and wear as stamp()"""
    f = 'head_red'
    tw = sum(text_w(ch, f) + 1 for ch in word) - 1 + sp * (len(word) - 1)
    w, h = tw + 16, 22
    img = new(w, h)
    cv = Cv(img)
    c = P['stamp']
    cv.rect(0, 0, w, h, c)
    cv.rect(1, 1, w - 1, h - 1, c)
    cv.rect(3, 3, w - 3, h - 3, c)
    for p in ((0, 0), (w - 1, 0), (0, h - 1), (w - 1, h - 1)):
        cv.put(p[0], p[1], KEY)
    x = (w - tw) // 2
    for ch in word:
        text(cv, x, 4, ch, f)
        x += text_w(ch, f) + 1 + sp
    worn(cv, w, h, seed)
    return img


def make_stamps():
    add('pres', 'stamp_project', stamp_spaced('PROJECT', 17, 2))
    add('pres', 'stamp_decree', stamp('\xa7 DECREE', 'BY ORDER OF THE PRESIDENT', 5))
    add('pres', 'stamp_approved', stamp('APPROVED', None, 9))
    add('pres', 'stamp_rejected', stamp('REJECTED', None, 13))
    # small round seal with the section sign (17 x 17)
    img = new(17, 17)
    cv = Cv(img)
    for y in range(17):
        for x in range(17):
            d = math.hypot(x - 8, y - 8)
            if 6.6 <= d <= 8.4 or 4.4 <= d <= 5.2:
                cv.put(x, y, P['stamp'])
    g = FONTS['body_red']['lut'][0xa7]
    cv.blit(g[8], 8 - g[3] // 2, 8 - 6)
    worn(cv, 17, 17, 21, 0.1)
    add('pres', 'stamp_seal', img)


# =========================================================================== PACK + WRITE
def pack(items, width=ATLAS_W):
    """MaxRects packer (best short side fit): items [(name, img, ax, ay, hx, hy)] ->
       sheet, rects (name, x, y, w, h, ax, ay, hx, hy, img) sorted by name"""
    order = sorted(items, key=lambda it: (-max(it[1].size), -it[1].size[0] * it[1].size[1], it[0]))
    free = [(0, 0, width, 8192)]
    placed = []
    for (name, img, ax, ay, hx, hy) in order:
        w, h = img.size
        best = None
        for (fx, fy, fw, fh) in free:
            if w <= fw and h <= fh:
                score = (fy + h, min(fw - w, fh - h), fx)
                if best is None or score < best[0]:
                    best = (score, fx, fy)
        assert best is not None, name
        x, y = best[1], best[2]
        placed.append((name, x, y, w, h, ax, ay, hx, hy, img))
        nf = []
        for (fx, fy, fw, fh) in free:
            if x >= fx + fw or x + w <= fx or y >= fy + fh or y + h <= fy:
                nf.append((fx, fy, fw, fh))
                continue
            if x > fx:
                nf.append((fx, fy, x - fx, fh))
            if x + w < fx + fw:
                nf.append((x + w, fy, fx + fw - x - w, fh))
            if y > fy:
                nf.append((fx, fy, fw, y - fy))
            if y + h < fy + fh:
                nf.append((fx, y + h, fw, fy + fh - y - h))
        free = []
        for i, a in enumerate(nf):
            contained = False
            for j, b in enumerate(nf):
                if i != j and b[0] <= a[0] and b[1] <= a[1] and a[0] + a[2] <= b[0] + b[2] and \
                        a[1] + a[3] <= b[1] + b[3] and (a != b or j < i):
                    contained = True
                    break
            if not contained:
                free.append(a)
    H = max(r[2] + r[4] for r in placed)
    sheet = new(width, H)
    for r in placed:
        sheet.paste(r[9], (r[1], r[2]))
    placed.sort(key=lambda r: r[0])
    return sheet, placed


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


def cstr(s):
    return s.replace('\\', '\\\\').replace('"', '\\"')


def write_art_ui_h(sheet_rects, fonts):
    L = ['/* generated by gen_ui.py - do not edit.  KAUPUNKI UI art index (T03), convention in src/art.h.',
         '   Sprite names are documented in art/UI.md; frame layout per resolution is in art/ui_layout.h. */',
         '#ifndef ART_UI_H', '#define ART_UI_H', '']
    order = []
    for sname, bmp, rects in sheet_rects:
        var = 'sheet_' + sname
        order.append((sname, bmp, var, len(rects)))
        L.append('static const ArtSpr %s[] = {' % var)
        for (name, x, y, w, h, ax, ay, hx, hy, _img) in rects:
            L.append('    { "%s", %d, %d, %d, %d, %d, %d, %d, %d },' % (name, x, y, w, h, ax, ay, hx, hy))
        L.append('};')
        L.append('')
    L.append('#define ART_UI_SHEET_COUNT %d' % len(order))
    L.append('static const ArtSheet art_ui_sheets[] = {')
    for (sname, bmp, var, n) in order:
        L.append('    { "art\\\\%s.bmp", ART_ZOOM_ANY, 1, %s, %d },' % (bmp, var, n))
    L.append('};')
    L.append('')
    L.append('/* glyph tables: sorted by code; codes 1..5 = GLYPH_* symbols (text.h); Latin-1 / cp1252 bytes.')
    L.append('   rect = cell incl. effect pixels; (ox, oy) = offset from the pen (line top) to the rect; adv = pen')
    L.append('   advance.  Missing codes: draw "?" (every font has the same character set, see UI.md). */')
    fvars = []
    for f in fonts:
        var = 'glyphs_' + f['name']
        fvars.append((f, var))
        L.append('static const ArtGlyph %s[] = {' % var)
        row = []
        for g in f['glyphs']:
            row.append('{%d,%d,%d,%d,%d,%d,%d,%d}' % (g[0], g[1], g[2], g[3], g[4], g[5], g[6], g[7]))
            if len(row) == 6:
                L.append('    ' + ', '.join(row) + ',')
                row = []
        if row:
            L.append('    ' + ', '.join(row) + ',')
        L.append('};')
    L.append('')
    for f, var in fvars:
        L.append('static const ArtFont font_%s = { "%s", "art\\\\%s.bmp", %d, %d, %s, %d };' % (
            f['name'], f['name'], FONT_SHEET[f['kind']], f['lh'], f['base'], var, len(f['glyphs'])))
    L.append('#define ART_UI_FONT_COUNT %d' % len(fvars))
    L.append('static const ArtFont *const art_ui_fonts[] = {')
    L.append('    ' + ', '.join('&font_' + f['name'] for f, _ in fvars))
    L.append('};')
    L.append('')
    L.append('#endif')
    with open(os.path.join(HERE, 'art_ui.h'), 'w', newline='') as fh:
        fh.write('\r\n'.join(L) + '\r\n')


# =========================================================================== LAYOUT (per resolution)
def layout(W, H):
    top_h, tick_h = 16, 14
    pw = 104 if W < 800 else 144
    px = W - pw
    edge = 3
    cx = px + edge + 4                # content x
    cw = pw - edge - 7                # content width (94): margins 4 left, 3 right
    if pw == 144:
        cx, cw = px + edge + 3, 134   # 3 px margins so the 134 px minimap bezel fits
    Lo = {'W': W, 'H': H}
    Lo['top'] = (0, 0, W, top_h)
    Lo['panel'] = (px, top_h, pw, H - top_h)
    Lo['map'] = (0, top_h, px, H - top_h - tick_h)
    Lo['ticker'] = (0, H - tick_h, px, tick_h)
    y = top_h + (3 if W < 800 else 4)
    Lo['plate'] = (cx, y, cw, 16)
    y += 16 + 3
    kh = {640: 28, 800: 32, 1024: 36}[W]
    gx = 4
    gy = 2 if W == 640 else 3
    kw = (cw - gx) // 2
    Lo['keys'] = (cx, y, kw, kh)
    Lo['key_grid'] = (2, 5, gx, gy)
    y += 5 * kh + 4 * gy + 2
    Lo['tool_label'] = (cx, y, cw, 11)
    y += 11 + 2
    large = W >= 1024
    fw, fh = (68, 84) if large else (36, 44)
    if large:      # big portrait at the left, name beside it, quote (3 lines) under it
        plh = 3 + fh + 3 + 3 * 10 + 3
    else:          # small portrait at the left, name and 2-line quote beside it
        plh = fh + 6
    Lo['plaque'] = (cx, y, cw, plh)
    Lo['portrait_frame'] = (cx + 3, y + 3, fw, fh)
    Lo['portrait'] = (cx + 5, y + 5, fw - 4, fh - 4)
    qx = cx + 3 + fw + 3
    Lo['pres_name'] = (qx, y + 4, cx + cw - 3 - qx, 20)
    if large:
        Lo['quote'] = (cx + 5, y + 3 + fh + 3, cw - 10, 30)
    else:
        Lo['quote'] = (qx, y + 25, cx + cw - 3 - qx, plh - 28)
    Lo['large'] = 1 if large else 0
    y += plh + 3
    mk = 5
    mgap = 2 if W == 640 else 3
    mw = (cw - (mk - 1) * mgap) // mk
    mh = 18 if W == 640 else 20
    Lo['mini'] = (cx + (cw - (mk * mw + (mk - 1) * mgap)) // 2, y, mw, mh)
    Lo['mini_n'] = (mk, mgap)
    y += mh + 4
    mmw = cw if W == 640 else 134
    Lo['mmap_frame'] = (cx + (cw - mmw) // 2, H - 4 - mmw, mmw, mmw)
    f = Lo['mmap_frame']
    Lo['mmap'] = (f[0] + 3, f[1] + 3, f[2] - 6, f[3] - 6)
    Lo['grille'] = (cx, y, cw, f[1] - 4 - y)
    # top bar: speed keys at the right (pause 1 2 3, gap, menu)
    sk = 15
    Lo['menu_key'] = (W - 4 - sk, 2, sk, 12)
    Lo['speed'] = (W - 4 - sk - 6 - 4 * sk - 3, 2, sk, 12)
    Lo['speed_gap'] = 1
    # zoom keys (F2): zoom out, zoom in (x advances by w + speed_gap), 6 px left of the pause key
    Lo['zoom_key'] = (Lo['speed'][0] - 6 - 2 * sk - Lo['speed_gap'], 2, sk, 12)
    return Lo


LAYOUT_FIELDS = [('top', 'top bar'), ('map', 'map view (ui_map_rect)'), ('ticker', 'news ticker strip'),
                 ('panel', 'teak panel incl. 3 px left edge'), ('plate', 'brass name plate (plate 3-slice, 16 tall)'),
                 ('keys', 'first tool key (x, y) and key size (w, h)'),
                 ('tool_label', 'line under the keys: active tool / variant name (body_shadow)'),
                 ('plaque', 'President plaque (plaque 9-slice)'), ('portrait_frame', 'pres_frame_s / pres_frame_l'),
                 ('portrait', 'pres_s_* (32x40) / pres_l_* (64x80)'),
                 ('pres_name', '"President" + player name, 2 lines of body_dark'),
                 ('quote', 'plaque quote, body_red, wrap to w, 10 px lines'),
                 ('mini', 'first mini key (panel shortcuts), size'), ('grille', 'slack area: grille slots'),
                 ('mmap_frame', 'brass minimap bezel (mmap 9-slice)'), ('mmap', 'minimap window inside the bezel'),
                 ('speed', 'first speed key (pause), then 1 2 3 at +w+gap'), ('menu_key', 'menu key')]


def write_layout_h(los):
    L = ['/* generated by gen_ui.py - do not edit.  KAUPUNKI frame layout (T03) for ui.cpp (T09).',
         '   One entry per screen size; rects are x, y, w, h in screen pixels.  See art/UI.md "Composing the frame".',
         '   Not included by art.cpp (plain layout data + colours for ui.cpp). */',
         '#ifndef UI_LAYOUT_H', '#define UI_LAYOUT_H', '',
         'typedef struct { short x, y, w, h; } UilRect;', 'typedef struct {', '    short scr_w, scr_h;']
    for k, doc in LAYOUT_FIELDS:
        L.append('    UilRect %s;%s/* %s */' % (k, ' ' * max(1, 16 - len(k)), doc))
    L += ['    short key_cols, key_rows, key_gap_x, key_gap_y;',
          '    short mini_count, mini_gap;     /* mini keys: w,h from .mini; x advances by w + mini_gap */',
          '    short speed_gap;                /* speed keys: x advances by w + speed_gap */',
          '    short portrait_large;           /* 1: use pres_l_* + pres_frame_l, 0: pres_s_* + pres_frame_s */',
          '    UilRect zoom_key;               /* F2: zoom out key (btn_zoom_out_*), then zoom in at +w+speed_gap */',
          '} UiLayout;', '',
          '#define UI_LAYOUT_COUNT 3', 'static const UiLayout ui_layouts[UI_LAYOUT_COUNT] = {']
    for Lo in los:
        parts = ['%d, %d' % (Lo['W'], Lo['H'])]
        for k, _ in LAYOUT_FIELDS:
            parts.append('{%d,%d,%d,%d}' % Lo[k])
        kg = Lo['key_grid']
        parts.append('%d, %d, %d, %d' % kg)
        parts.append('%d, %d' % Lo['mini_n'])
        parts.append('%d' % Lo['speed_gap'])
        parts.append('%d' % Lo['large'])
        parts.append('{%d,%d,%d,%d}' % Lo['zoom_key'])
        L.append('    { ' + ', '.join(parts) + ' },')
    L += ['};', '',
          '/* UI colours (RGB, already 565-rounded): gfx_rgb(UI_COL_PAPER) etc. for fills between pieces */']
    for k in ('paper', 'paper_lt', 'paper_dk', 'card', 'tip', 'ink', 'pencil', 'grey', 'cream', 'cream_lt',
              'cream_dk', 'bar', 'bar_dk', 'bar_xdk', 'teak', 'teak_dk', 'teak_xdk', 'brass', 'brass_lt',
              'bak', 'red', 'green', 'blue', 'amber', 'amber_lt', 'red_lt', 'blue_lt', 'green_lt', 'lit'):
        c = P[k]
        L.append('#define UI_COL_%-10s %3d, %3d, %3d' % (k.upper(), c[0], c[1], c[2]))
    L += ['', '#endif']
    with open(os.path.join(HERE, 'ui_layout.h'), 'w', newline='') as fh:
        fh.write('\r\n'.join(L) + '\r\n')


# =========================================================================== PREVIEWS
def placeholder_map(w, h):
    """map stand-in for the previews: the approved mockup's map area, tiled if needed"""
    src = os.path.join(HERE, 'explore', 'mockup_C_800.png')
    img = new(w, h, P['green'])
    if os.path.exists(src):
        m = Image.open(src).convert('RGB').crop((0, 18, 676, 586))
        for yy in range(0, h, m.size[1]):
            for xx in range(0, w, m.size[0]):
                img.paste(m, (xx, yy))
    return img


def placeholder_minimap(n):
    img = new(n, n)
    px = img.load()
    for y in range(n):
        for x in range(n):
            u, v = x * 128.0 / n, y * 128.0 / n
            w = pnoise(u, v, 22, 22, 5) + 0.25 * pnoise(u, v, 6, 6, 6)
            c = (75, 95, 107) if w < 0.42 else (98, 102, 73) if pnoise(u, v, 9, 9, 8) < 0.55 else (60, 81, 70)
            if 50 < u < 90 and 40 < v < 85 and (int(u) % 6 == 0 or int(v) % 6 == 0) and w >= 0.42:
                c = (91, 92, 92)
            elif 50 < u < 90 and 40 < v < 85 and w >= 0.42 and ihash(int(u), int(v)) < 0.5:
                c = (196, 161, 94) if ihash(int(u), int(v), 2) < 0.5 else (170, 169, 161)
            px[x, y] = to565(c)
    return img


def wrap(s, font, w):
    out, line = [], ''
    for wd in s.split():
        t = (line + ' ' + wd).strip()
        if text_w(t, font) > w and line:
            out.append(line)
            line = wd
        else:
            line = t
    if line:
        out.append(line)
    return out


def key_face(cv, x, y, w, h, icon_name, label, state):
    draw9(cv, 'btn_' + state, x, y, w, h)
    o = 1 if state in ('pressed', 'selected') else 0
    off = '_off' if state == 'disabled' else ''
    ih = 13
    if label:
        lw = text_w(label, 'body_dark')
        gap = h - 4 - ih - 7
        iy = y + 2 + max(1, gap // 3)
        cv.blit(get('icon_%s%s' % (icon_name, off)), x + (w - 1 - 13) // 2 + o, iy + o)
        text(cv, x + (w - 1 - lw) // 2 + o, y + h - 1 - 11 - 1 + o, label,
             'body_grey' if state == 'disabled' else 'body_dark')
    else:
        cv.blit(get('icon_%s%s' % (icon_name, off)), x + (w - 1 - 13) // 2 + o, y + (h - 1 - 13) // 2 + o)


TOOL_KEYS = [('key_roads', 'Roads'), ('key_homes', 'Homes'), ('key_shops', 'Shops'), ('key_industry', 'Industry'),
             ('key_power', 'Power'), ('key_water', 'Water'), ('key_services', 'Services'), ('key_parks', 'Parks'),
             ('key_bulldoze', 'Bulldoze'), ('key_query', 'Query')]
MINI_KEYS = ['budget', 'population', 'overlays', 'undo', 'redo']


def compose(W, H, extras=True):
    Lo = layout(W, H)
    img = new(W, H, (0, 0, 0))
    cv = Cv(img)
    mx, my, mw, mh = Lo['map']
    img.paste(placeholder_map(mw, mh), (mx, my))
    # --- top bar
    tile_into(cv, get('bar_tile'), 0, 0, W, 16)
    x = 4
    date = 'October 1961'
    pw = text_w(date, 'body_dark') + 10
    draw3h(cv, 'cal_plate', x, 2, pw)
    text(cv, x + 6, 2, date, 'body_dark')
    x += pw + 6
    fields = [('Funds', '12,500 mk', '+450'), ('Population', '4,812', None)]
    for cap, val, delta in fields:
        cv.blit(get('bar_sep'), x, 2)
        x += 6
        text(cv, x, 3, cap, 'body_brass')
        x += text_w(cap, 'body_brass') + 4
        vw = text_w(val, 'body_light')
        draw3h(cv, 'bar_well', x, 2, vw + 8)
        text(cv, x + 4, 2, val, 'body_light')
        x += vw + 8 + 3
        if delta:
            text(cv, x, 3, delta, 'body_green_lt')
            x += text_w(delta, 'body_green_lt') + 3
        x += 4
    cv.blit(get('bar_sep'), x, 2)
    x += 6
    text(cv, x, 3, 'Demand', 'body_brass')
    x += text_w('Demand', 'body_brass') + 4
    cv.blit(get('dem_well'), x, 1)
    for i, (nm, v) in enumerate((('r', 5), ('c', 2), ('i', -3))):
        bar = get('dem_bar_' + nm)
        if v >= 0:
            cv.blit(bar.crop((0, 0, 4, v)), x + 2 + i * 5, 1 + 6 - v)
        else:
            cv.blit(bar.crop((0, 0, 4, -v)), x + 2 + i * 5, 1 + 7)
    x += 19 + 6
    if W >= 800:
        cv.blit(get('bar_sep'), x, 2)
        x += 6
        text(cv, x, 3, 'Approval', 'body_brass')
        x += text_w('Approval', 'body_brass') + 4
        text(cv, x, 3, '74%', 'body_light')
    sx, sy, sw, sh = Lo['speed']
    for i, s in enumerate(('pause', '1', '2', '3')):
        cv.blit(get('speed_%s_%s' % (s, 'on' if s == '1' else ('hover' if s == '2' and extras else 'normal'))),
                sx + i * (sw + Lo['speed_gap']), sy)
    m = Lo['menu_key']
    cv.blit(get('speed_menu_normal'), m[0], m[1])
    zx, zy, zw, zh = Lo['zoom_key']
    cv.blit(get('btn_zoom_out_normal'), zx, zy)
    cv.blit(get('btn_zoom_in_%s' % ('hover' if extras else 'disabled')), zx + zw + Lo['speed_gap'], zy)
    # --- panel
    px_, py_, pw_, ph_ = Lo['panel']
    tile_into(cv, get('teak_tile').crop((0, 0, pw_, 128)), px_, py_, pw_, ph_)
    tile_into(cv, get('panel_edge'), px_, py_, 3, ph_)
    p = Lo['plate']
    draw3h(cv, 'plate', p[0], p[1], p[2])
    town = 'KIVIKOSKI'
    tw = text_w(town, 'head')
    text(cv, p[0] + (p[2] - tw) // 2, p[1] + 2 - 1, town, 'head')
    kx, ky, kw, kh = Lo['keys']
    cols, rows, gx, gy = Lo['key_grid']
    for n, (ic, lab) in enumerate(TOOL_KEYS):
        st = 'selected' if n == 1 else ('hover' if (n == 6 and extras) else ('pressed' if n == 7 and extras else 'normal'))
        if n == 8 and extras and W == 1024:
            st = 'disabled'
        key_face(cv, kx + (n % 2) * (kw + gx), ky + (n // 2) * (kh + gy), kw, kh, ic, lab, st)
    t = Lo['tool_label']
    text(cv, t[0] + 1, t[1], 'Houses: rintamamiestalo' if W > 640 else 'Houses', 'body_shadow')
    pl = Lo['plaque']
    draw9(cv, 'plaque', pl[0], pl[1], pl[2], pl[3])
    pf = Lo['portrait_frame']
    big = Lo['large']
    cv.blit(get('pres_frame_' + ('l' if big else 's')), pf[0], pf[1])
    pp = Lo['portrait']
    cv.blit(get('pres_%s_%s' % ('l' if big else 's', 'content')), pp[0], pp[1])
    q = Lo['pres_name']
    text(cv, q[0], q[1], 'President', 'body_dark')
    text(cv, q[0], q[1] + 10, 'Kekkola', 'body_dark')
    q = Lo['quote']
    for i, ln in enumerate(wrap('\u201dGood, good. The people expect results.\u201d', 'body_red', q[2])[:q[3] // 10]):
        text(cv, q[0], q[1] + i * 10, ln, 'body_red')
    mk = Lo['mini']
    n_, gap = Lo['mini_n']
    for i, ic in enumerate(MINI_KEYS):
        key_face(cv, mk[0] + i * (mk[2] + gap), mk[1], mk[2], mk[3], ic, None,
                 'disabled' if ic == 'redo' else 'normal')
    g = Lo['grille']
    yy = g[1] + 2
    nslots = (g[3] - 4 + 3) // 8
    bdg = get('badge')
    bslot = nslots // 2
    for k in range(nslots):
        if k == bslot and nslots >= 3:
            cv.blit(bdg, g[0] + (g[2] - bdg.size[0]) // 2, yy + 4 - bdg.size[1] // 2)
        else:
            draw3h(cv, 'grille', g[0] + 6, yy, g[2] - 12)
        yy += 8
    f = Lo['mmap_frame']
    draw9(cv, 'mmap', f[0], f[1], f[2], f[3])
    mm = Lo['mmap']
    img.paste(placeholder_minimap(mm[2]), (mm[0], mm[1]))
    cx0, cy0 = mm[0] + mm[2] // 2 - 20, mm[1] + mm[3] // 2 - 15
    for nm, (ox, oy) in (('tl', (0, 0)), ('tr', (36, 0)), ('bl', (0, 26)), ('br', (36, 26))):
        cv.blit(get('mmap_cam_' + nm), cx0 + ox, cy0 + oy)
    # --- ticker
    tx, ty, tw_, th = Lo['ticker']
    tile_into(cv, get('ticker_tile'), tx, ty, tw_, th)
    cv.blit(get('lamp_warn'), tx + 4, ty + 4)
    text(cv, tx + 15, ty + 2, 'Water shortage in M\xe4ntyl\xe4: 340 homes without water.', 'body_amber')
    if W >= 800:
        x2 = tx + 15 + text_w('Water shortage in M\xe4ntyl\xe4: 340 homes without water.', 'body_amber') + 12
        cv.blit(get('lamp_info'), x2, ty + 4)
        text(cv, x2 + 11, ty + 2, 'The President opened the new water tower.', 'body_light')
    if not extras:
        return img, Lo
    # --- panel window
    if W == 640:
        draw_query_panel(cv, 16, 120, 300, 250)
    elif W == 800:
        draw_budget_panel(cv, 300, 150, 340, 300)
    else:
        draw_budget_panel(cv, 300, 150, 340, 300)
        draw_query_panel(cv, 40, 330, 300, 250)
    # --- fly-out next to the Services key
    n = 6
    kx_ = kx + (n % 2) * (kw + gx)
    ky_ = ky + (n // 2) * (kh + gy)
    rows = [('b_fire_station', 'Fire station', '4,000 mk', 0), ('b_school', 'School (kansakoulu)', '9,000 mk', 0),
            ('b_clinic', 'Health clinic', '6,000 mk', 0), ('b_sauna', 'Public sauna', '2,500 mk', 0),
            ('b_police', 'Police station', 'Market town', 1), ('b_library', 'Library', 'Market town', 1),
            ('b_post', 'Post and telephone', 'Market town', 1)]
    fw_ = 8 + 13 + 4 + max(text_w(r[1], 'body_dark') for r in rows) + 10 + max(text_w(r[2], 'body_dark') for r in rows) + 8
    fh_ = 6 + len(rows) * 17 + 6
    fx = px_ - 4 - fw_
    fy = max(18, ky_ - 20)
    draw9(cv, 'fly', fx, fy, fw_, fh_)
    cv.blit(get('fly_arrow'), fx + fw_ - 1, ky_ + kh // 2 - 6)
    for i, (ic, name, cost, locked) in enumerate(rows):
        ry = fy + 6 + i * 17
        if i == 1:
            draw3h(cv, 'flyrow_hover', fx + 4, ry, fw_ - 8)
        cv.blit(get('icon_' + ic + ('_off' if locked else '')), fx + 8, ry + 2)
        text(cv, fx + 8 + 13 + 4, ry + 3, name, 'body_grey' if locked else 'body_dark')
        cw_ = text_w(cost, 'body_dark')
        text(cv, fx + fw_ - 8 - cw_, ry + 3, cost, 'body_grey' if locked else 'body_dark')
        if i == 3:
            tile_into(cv, get('fly_sep'), fx + 6, ry + 16, fw_ - 12, 2)
    # --- tooltip under the Funds field
    tip = ['Funds 12,500 mk', '\x04 Taxes      +1,240', '\x05 Upkeep      –790', '\x03 Projected   +450 / month']
    tw2 = max(text_w(s, 'body_dark') for s in tip) + 12
    th2 = len(tip) * 11 + 8
    draw9(cv, 'tip', 96, 19, tw2, th2)
    for i, s in enumerate(tip):
        text(cv, 102, 22 + i * 11, s, 'body_dark' if i != 2 else 'body_red')
    return img, Lo


def title_band(cv, x, y, w, title, st='normal'):
    text(cv, x + 8, y + 3, title, 'head_light')
    cv.blit(get('close_' + st), x + w - 6 - 13, y + 3)


def draw_budget_panel(cv, x, y, w, h):
    draw9(cv, 'win', x, y, w, h)
    title_band(cv, x, y, w, 'Budget', 'hover')
    ix, iy = x + 6, y + 19
    # tabs
    tx = ix + 4
    for i, t in enumerate(('Taxes', 'Services', 'Loans')):
        tw = text_w(t, 'body_dark') + 12
        draw3h(cv, 'tab_' + ('selected' if i == 0 else 'hover' if i == 2 else 'normal'), tx, iy + 4, tw)
        text(cv, tx + 6, iy + 6, t, 'body_dark')
        tx += tw + 1
    tile_into(cv, get('tab_line'), tx, iy + 4 + 13, x + w - 6 - tx - 4, 1)
    tile_into(cv, get('tab_line'), ix, iy + 4 + 13, 4, 1)
    yy = iy + 24
    for lab, val in (('Residential tax', 9), ('Business tax', 12)):
        text(cv, ix + 6, yy, lab, 'body_dark')
        vs = '%d%%' % val
        text(cv, x + w - 12 - text_w(vs, 'body_dark'), yy, vs, 'body_dark')
        sl_x, sl_w = ix + 6, w - 12 - 12 - 30
        draw3h(cv, 'slider_track', sl_x, yy + 13, sl_w)
        fill_w = int(sl_w * val / 20.0)
        draw3h(cv, 'slider_fill', sl_x, yy + 13, fill_w)
        cv.blit(get('slider_knob_' + ('pressed' if val == 12 else 'normal')), sl_x + fill_w - 4, yy + 10)
        yy += 30
    tile_into(cv, get('rule'), ix + 6, yy, w - 24, 2)
    yy += 6
    text(cv, ix + 6, yy, 'Monthly projection', 'head')
    yy += 15
    draw9(cv, 'inset', ix + 6, yy, w - 24 - 12, 70)
    rowsd = [('Residential taxes', '+1,040'), ('Business taxes', '+200'), ('Service upkeep', '–640'),
             ('Roads and lines', '–150'), ('Balance', '+450')]
    for i, (a, b) in enumerate(rowsd):
        text(cv, ix + 12, yy + 3 + i * 13 if i < 4 else yy + 3 + i * 13, a, 'body_dark')
        f = 'body_red' if b.startswith('–') else 'body_green' if i == 4 else 'body_dark'
        text(cv, ix + w - 24 - 12 - text_w(b, f), yy + 3 + i * 13, b, f)
    # scrollbar on the inset
    sbx = ix + w - 24 - 12 + 6
    cv.blit(get('arrow_up_normal'), sbx, yy)
    draw3v(cv, 'sbar_track', sbx, yy + 11, 70 - 22)
    draw3v(cv, 'sbar_thumb', sbx, yy + 14, 20)
    cv.blit(get('arrow_down_pressed'), sbx, yy + 70 - 11)
    yy += 78
    for i, (lab, st) in enumerate((('Show tax puffs', 'on_normal'), ('Pause on new month', 'off_hover'),
                                   ('Emergency loans', 'off_disabled'))):
        cv.blit(get('check_' + st), ix + 6, yy + i * 14)
        text(cv, ix + 22, yy + i * 14, lab, 'body_grey' if 'disabled' in st else 'body_dark')
    # buttons
    bw = 64
    for i, (lab, st) in enumerate((('Close', 'normal'), ('Apply', 'hover'))):
        bx = x + w - 6 - 6 - (2 - i) * (bw + 4)
        by = y + h - 6 - 6 - 20
        draw9(cv, 'btn_' + st, bx, by, bw, 20)
        lw = text_w(lab, 'body_dark')
        text(cv, bx + (bw - 1 - lw) // 2, by + 4, lab, 'body_dark')
    cv.blit(get('stamp_seal'), x + 14, y + h - 6 - 6 - 18)


def draw_query_panel(cv, x, y, w, h):
    draw9(cv, 'win', x, y, w, h)
    title_band(cv, x, y, w, 'Rintamamiestalo')
    ix, iy = x + 6, y + 19
    cv.blit(get('icon_z_houses'), ix + 6, iy + 6)
    text(cv, ix + 24, iy + 4, 'Kuusikuja 14, M\xe4ntyl\xe4', 'body_dark')
    text(cv, ix + 24, iy + 15, 'Tier 2 of 3 \xb7 6 residents \xb7 built 1958', 'body_grey')
    tile_into(cv, get('rule'), ix + 6, iy + 28, w - 24, 2)
    text(cv, ix + 6, iy + 34, 'To reach tier 3:', 'head')
    lines = [('\x01 Road', 1), ('\x01 Power', 1), ('\x02 Water (no main within reach)', 0),
             ('\x01 School 312/400', 1), ('\x02 Land value 31 (needs 40)', 0), ('\x01 Sauna', 1),
             ('\x02 Phone line (post office)', 0)]
    for i, (s, ok) in enumerate(lines):
        text(cv, ix + 10, iy + 52 + i * 12, s, 'body_green' if ok else 'body_red')
    yy = iy + 52 + len(lines) * 12 + 6
    draw9(cv, 'inset', ix + 6, yy, w - 24, 34)
    text(cv, ix + 12, yy + 4, 'Land value 31  \xb7  Pollution 4  \xb7  Fire risk 22%', 'body_dark')
    text(cv, ix + 12, yy + 17, '\xc5bo-v\xe4g \xe9 \xfc  \xa7 12  –450 mk  \xbd  \xab“Q”\xbb', 'body_dark')
    cv.blit(get('stamp_approved'), x + w - 6 - get('stamp_approved').size[0] - 6, y + h - 6 - 6 - 22)


def preview_sheet(sheet_img, scale=3, bg=(70, 74, 66)):
    w, h = sheet_img.size
    out = new(w, h, bg)
    px = out.load()
    sp = sheet_img.load()
    for y in range(h):
        for x in range(w):
            c = sp[x, y]
            px[x, y] = c if c != KEY else (bg if ((x // 4 + y // 4) & 1) else tuple(min(255, v + 10) for v in bg))
    return out.resize((w * scale, h * scale), Image.NEAREST)


def font_sample():
    lines = [
        ('body_dark', 'The quick brown fox jumps over the lazy dog. 0123456789'),
        ('body_dark', 'RINTAMAMIESTALO lamellitalo pistetalo sauna M\xe4ntyl\xe4 \xc5bo \xd6sterby \xc4\xe4nekoski'),
        ('body_dark', '\x01 Road \x02 Water \x03 bullet \x04 up \x05 down  \xa7 4  12,500 mk  –450  85%  \xbd  \xb0'),
        ('body_dark', '!"#$%&\'()*+,-./:;<=>?@[\\]^_`{|}~ “quote” ”suomi” ‘a’ \xab\xbb \xd7 … \xe9\xc9\xfc\xdc'),
        ('body_grey', 'Unlocks at: Market town (3,000 residents)'),
        ('body_red', '340 homes without water (Kivikko)   –790 mk'),
        ('body_green', '\x01 School 312/400   +1,240 mk'),
        ('head', 'MAAKUNNAN SANOMAT: Town rights granted to Kivikoski!'),
        ('head', 'Budget \xb7 Population \xb7 Query \xb7 \xc4\xd6\xc5 \xe4\xf6\xe5 0123456789 \xa7'),
        ('head_red', '\xa7 DECREE No. 12/1961'),
    ]
    dark = [('body_light', 'October 1961  Funds 12,500 mk  Population 4,812  \xc4\xd6\xc5 \xe4\xf6\xe5'),
            ('body_amber', 'Water shortage in M\xe4ntyl\xe4.'), ('body_red_lt', 'Fire in Kivikko!'),
            ('body_blue_lt', 'A delegation from the East arrives.'), ('body_brass', 'Funds  Population  Demand'),
            ('body_green_lt', '+450 mk'), ('head_light', 'Budget  Rintamamiestalo  \xc5\xc4\xd6')]
    W, H = 640, 14 * len(lines) + 15 * len(dark) + 60
    img = new(W, H, P['paper'])
    cv = Cv(img)
    y = 4
    for f, s in lines:
        text(cv, 4, y, s, f)
        y += FONTS[f]['lh'] + 3
    cv.fill(0, y, W, H, P['bar'])
    y += 4
    for f, s in dark:
        text(cv, 4, y, s, f)
        y += FONTS[f]['lh'] + 2
    # map-over fonts
    mp = placeholder_map(W, 40)
    img.paste(mp.crop((0, 0, W, H - y)), (0, y))
    text(cv, 8, y + 6, '–450 mk', 'float_red')
    text(cv, 80, y + 6, '+120 mk', 'float_green')
    text(cv, 150, y + 6, '4,812', 'float_light')
    text(cv, 210, y + 6, 'Kivikko', 'body_shadow')
    return img.crop((0, 0, W, min(H, y + 24)))


def newspaper(W=640, H=480):
    img = new(W, H, P['paper_lt'])
    cv = Cv(img)
    m = get('masthead')
    cv.blit(m, (W - m.size[0]) // 2, 10)
    y = 10 + m.size[1] + 2
    text(cv, 20, y, 'No. 1 \xb7 Kivikoski, Tuesday 2 January 1962', 'body_dark')
    s = 'Price 15 mk'
    text(cv, W - 20 - text_w(s, 'body_dark'), y, s, 'body_dark')
    y += 13
    tile_into(cv, get('rule_news'), 20, y, W - 40, 4)
    y += 10
    hl = 'TOWN RIGHTS GRANTED TO KIVIKOSKI'
    big = new(text_w(hl, 'head') + 2, HEAD_LH)
    text(Cv(big), 0, 0, hl, 'head')
    big = big.resize((big.size[0] * 2, big.size[1] * 2), Image.NEAREST)
    cv.blit(big, (W - big.size[0]) // 2, y)
    y += big.size[1] + 4
    sub = 'The President signs the decree in person; 10,000 residents celebrate with coffee and pulla'
    text(cv, (W - text_w(sub, 'body_dark')) // 2, y, sub, 'body_dark')
    y += 16
    col_w = (W - 40 - 20) // 3
    heads = ['New water tower opens', 'Element factory planned', 'Sauna queue at record length']
    body = ('The new water tower on Harjukatu was opened on Saturday. Water pressure in the apartment blocks of '
            'M\xe4ntyl\xe4 is now said to be excellent. The President thanked the builders and noted that '
            'the people expect results.').split()
    for c in range(3):
        x0 = 20 + c * (col_w + 10)
        text(cv, x0, y, heads[c], 'head')
        yy = y + 16
        line = ''
        for wd in body * 6:
            t = (line + ' ' + wd).strip()
            if text_w(t, 'body_dark') > col_w:
                text(cv, x0, yy, line, 'body_dark')
                yy += 11
                line = wd
                if yy > H - 60:
                    break
            else:
                line = t
        if c < 2:
            cv.vline(x0 + col_w + 4, y, H - 50, P['card'])
    st = get('stamp_decree')
    cv.blit(st, W - 30 - st.size[0], H - 20 - st.size[1])
    cv.blit(get('pres_l_delighted'), 24, H - 20 - 80)
    return img


def icon_review():
    names = sorted(n[5:] for n in NAMES if n.startswith('icon_') and not n.endswith('_off'))
    cols = 8
    cw, ch = 76, 34
    rows = (len(names) + cols - 1) // cols
    img = new(cols * cw, rows * ch, P['teak'])
    cv = Cv(img)
    for i, n in enumerate(names):
        x, y = (i % cols) * cw, (i // cols) * ch
        draw9(cv, 'btn_normal', x + 4, y + 2, 20, 20)
        cv.blit(get('icon_' + n), x + 7, y + 5)
        draw9(cv, 'btn_disabled', x + 26, y + 2, 20, 20)
        cv.blit(get('icon_' + n + '_off'), x + 29, y + 5)
        draw9(cv, 'btn_selected', x + 48, y + 2, 20, 20)
        cv.blit(get('icon_' + n), x + 52, y + 6)
        text(cv, x + 3, y + 22, n[:14], 'body_light')
    return img.resize((img.size[0] * 3, img.size[1] * 3), Image.NEAREST)


# =========================================================================== UI.md inventory (generated block)
def inventory_md(sheet_rects):
    out = ['<!-- BEGIN GENERATED INVENTORY (gen_ui.py rewrites this block) -->', '']
    for sname, bmp, rects in sheet_rects:
        out.append('**%s.bmp** (%d sprites)' % (bmp, len(rects)))
        out.append('')
        names = [r[0] for r in rects]
        groups = {}
        for n in names:
            stem = n
            for base in sorted(SLICES, key=len, reverse=True):
                if n.startswith(base + '_') and n[len(base) + 1:] in NINE:
                    stem = base + '_{9}'
                    break
            else:
                for base in sorted(THREES, key=len, reverse=True):
                    if n.startswith(base + '_') and n[len(base) + 1:] in ('l', 'm', 'r', 't', 'b'):
                        stem = base + '_{3}'
                        break
            groups.setdefault(stem, []).append(n)
        line = []
        for stem in sorted(groups):
            if stem.endswith('_{9}') or stem.endswith('_{3}'):
                r = [x for x in rects if x[0] == groups[stem][0]][0]
                line.append('`%s`' % stem)
            else:
                r = [x for x in rects if x[0] == stem][0]
                line.append('`%s` %dx%d' % (stem, r[3], r[4]))
        out.append(', '.join(line))
        out.append('')
    out.append('`_{9}` = 9 pieces `_tl _t _tr _l _c _r _bl _b _br`; `_{3}` = 3 pieces (`_l _m _r` horizontal, '
               '`_t _m _b` vertical).')
    out.append('')
    out.append('| 9-slice | left | top | right | bottom | edge/centre tile |')
    out.append('|---|---|---|---|---|---|')
    for b in sorted(SLICES):
        cl, ct, cr, cb, E = SLICES[b]
        out.append('| `%s` | %d | %d | %d | %d | %d |' % (b, cl, ct, cr, cb, E))
    out.append('')
    out.append('| 3-slice | dir | cap 1 | cap 2 | middle tile | thickness |')
    out.append('|---|---|---|---|---|---|')
    for b in sorted(THREES):
        d, c1, c2, E, t = THREES[b]
        out.append('| `%s` | %s | %d | %d | %d | %d |' % (b, 'horizontal' if d == 'h' else 'vertical', c1, c2, E, t))
    out.append('')
    out.append('<!-- END GENERATED INVENTORY -->')
    return '\n'.join(out)


def update_ui_md(sheet_rects):
    p = os.path.join(HERE, 'UI.md')
    if not os.path.exists(p):
        return
    s = open(p).read()
    a = s.find('<!-- BEGIN GENERATED INVENTORY')
    b = s.find('<!-- END GENERATED INVENTORY -->')
    if a < 0 or b < 0:
        return
    b += len('<!-- END GENERATED INVENTORY -->')
    s = s[:a] + inventory_md(sheet_rects) + s[b:]
    open(p, 'w').write(s)


# =========================================================================== CHECK
def read_bmp(path):
    d = open(path, 'rb').read()
    assert d[:2] == b'BM', path
    off, = struct.unpack_from('<I', d, 10)
    hs, w, h, planes, bpp, comp = struct.unpack_from('<IiiHHI', d, 14)
    return w, h, bpp, comp


def check(sheet_rects, fonts):
    problems = []
    total_px = 0
    names = set()
    for sname, bmp, rects in sheet_rects:
        path = os.path.join(HERE, bmp + '.bmp')
        w, h, bpp, comp = read_bmp(path)
        if bpp != 24 or comp != 0:
            problems.append('%s: not 24-bit BI_RGB' % bmp)
        if w > ATLAS_W:
            problems.append('%s: width %d > 640' % (bmp, w))
        im = Image.open(path).convert('RGB')
        px = im.load()
        near = bad565 = 0
        for y in range(h):
            for x in range(w):
                c = px[x, y]
                if c == KEY:
                    continue
                if abs(c[0] - 255) < 40 and c[1] < 40 and abs(c[2] - 255) < 40:
                    near += 1
                if to565(c) != c:
                    bad565 += 1
        if near:
            problems.append('%s: %d near-key pixels' % (bmp, near))
        if bad565:
            problems.append('%s: %d pixels not pre-rounded to 565' % (bmp, bad565))
        total_px += w * h
        for r in rects:
            if r[0] in names:
                problems.append('duplicate name ' + r[0])
            names.add(r[0])
            if r[1] + r[3] > w or r[2] + r[4] > h:
                problems.append('%s outside its sheet' % r[0])
        print('  %-10s %3d x %-4d %4d KB  %d sprites' % (bmp, w, h, w * h * 2 // 1024, len(rects)))
    # fonts: every glyph of the charset present in every font, metrics sane
    need = set(char_code(c) for c in CHARSET)
    for f in fonts:
        codes = set(g[0] for g in f['glyphs'])
        miss = need - codes
        if miss:
            problems.append('font %s misses codes %s' % (f['name'], sorted(miss)))
        for g in f['glyphs']:
            if g[3] < 1 or g[4] < 1 or g[7] < 1:
                problems.append('font %s glyph %d has empty metrics' % (f['name'], g[0]))
        if f['glyphs'] != sorted(f['glyphs'], key=lambda g: g[0]):
            problems.append('font %s not sorted' % f['name'])
    for req in ('body_dark', 'body_light', 'head'):
        if req not in [f['name'] for f in fonts]:
            problems.append('required font %s missing' % req)
    # icons: every icon listed in UI.md; every tool/overlay/building has one
    md = open(os.path.join(HERE, 'UI.md')).read() if os.path.exists(os.path.join(HERE, 'UI.md')) else ''
    icon_names = sorted(n for n in names if n.startswith('icon_') and not n.endswith('_off'))
    for n in icon_names:
        stem = n[5:]
        if stem not in md:
            problems.append('icon %s not listed in UI.md' % n)
    bkeys = ['town_hall', 'coal_plant', 'coal_station', 'hydro_plant', 'oil_plant', 'peat_plant', 'nuclear_plant', 'pump', 'well',
             'water_tower', 'fire_station', 'school', 'clinic', 'sauna', 'police', 'library', 'post',
             'health_centre', 'park', 'sports_field', 'ice_rink', 'church', 'element_factory', 'station']
    want = (['icon_b_' + k for k in bkeys] + ['icon_r_' + k for k in ('grav', 'road', 'main', 'mway', 'rail')] +
            ['icon_z_' + k for k in ('houses', 'apartments', 'commercial', 'industry')] +
            ['icon_' + k for k in OVL_ICON_ORDER] + ['icon_' + k[0] for k in TOOL_KEYS] +
            ['icon_' + k for k in ('powerline', 'dezone', 'key_bulldoze', 'key_query')] +
            ['icon_s_' + s for s in ('fire', 'school', 'health', 'sauna', 'police', 'library', 'post')])
    for n in want:
        if n not in names or n + '_off' not in names:
            problems.append('missing icon ' + n)
    # layout sanity
    for (W, H) in ((640, 480), (800, 600), (1024, 768)):
        Lo = layout(W, H)
        for k, _ in LAYOUT_FIELDS:
            x, y, w, h = Lo[k]
            if w <= 0 or h <= 0 or x < 0 or y < 0 or x + w > W or y + h > H:
                problems.append('layout %dx%d: %s out of screen %r' % (W, H, k, Lo[k]))
        kx, ky, kw, kh = Lo['keys']
        cols, rows, gx, gy = Lo['key_grid']
        if ky + rows * kh + (rows - 1) * gy > Lo['tool_label'][1]:
            problems.append('layout %dx%d: keys overlap the label' % (W, H))
        zx, zy, zw, zh = Lo['zoom_key']
        if zx < 0 or zx + 2 * zw + Lo['speed_gap'] > Lo['speed'][0] - 4:
            problems.append('layout %dx%d: zoom keys overlap the speed keys' % (W, H))
        if Lo['grille'][3] < 0:
            problems.append('layout %dx%d: panel too short' % (W, H))
        if Lo['mmap'][2] < (88 if W == 640 else 128):
            problems.append('layout %dx%d: minimap window too small' % (W, H))
        if kx + 2 * kw + gx > Lo['panel'][0] + Lo['panel'][2]:
            problems.append('layout %dx%d: keys wider than the panel' % (W, H))
        for lab in [t[1] for t in TOOL_KEYS]:
            if text_w(lab, 'body_dark') > kw - 6:
                problems.append('layout %dx%d: key label %s too wide' % (W, H, lab))
    # header ascii
    for hf in ('art_ui.h', 'ui_layout.h'):
        d = open(os.path.join(HERE, hf), 'rb').read()
        if any(b > 127 for b in d):
            problems.append(hf + ' is not ASCII')
    kb = total_px * 2 // 1024
    print('  UI VRAM total: %d KB (%d sheets)' % (kb, len(sheet_rects)))
    print('  sprites: %d   icons: %d (+%d _off)   fonts: %d x %d glyphs' % (
        len(names), len(icon_names), len(icon_names), len(fonts), len(CHARSET)))
    if problems:
        print('CHECK FAILED (%d):' % len(problems))
        for p in problems:
            print('  - ' + p)
        return 1
    print('CHECK OK')
    return 0


# =========================================================================== MAIN
def main():
    os.makedirs(PREV, exist_ok=True)
    fsheets = build_fonts()
    allfonts = []
    for sname in ('font_body', 'font_head'):
        sheet, fonts = fsheets[sname]
        for f in fonts:
            f['lut'] = dict((g[0], g) for g in f['glyphs'])
            FONTS[f['name']] = f
        allfonts += fonts
    allfonts.sort(key=lambda f: [v[0] for v in FONT_VARIANTS].index(f['name']))
    make_frame()
    make_icons()
    make_pres()
    sheet_rects = []
    for s in SHEETS:
        img, rects = pack(SPR[s])
        bmp = 'ui_' + s
        write_bmp(os.path.join(HERE, bmp + '.bmp'), img)
        sheet_rects.append((s, bmp, rects))
        preview_sheet(img).save(os.path.join(PREV, 'sheet_%s_3x.png' % s))
    for sname in ('font_body', 'font_head'):
        sheet, fonts = fsheets[sname]
        write_bmp(os.path.join(HERE, sname + '.bmp'), sheet)
        sheet_rects.append((sname, sname, [(sname + '_sheet', 0, 0, sheet.size[0], sheet.size[1], 0, 0, -1, -1,
                                            None)]))
        preview_sheet(sheet).save(os.path.join(PREV, 'sheet_%s_3x.png' % sname))
    write_art_ui_h(sheet_rects, allfonts)
    los = [layout(640, 480), layout(800, 600), layout(1024, 768)]
    write_layout_h(los)
    update_ui_md(sheet_rects)
    for (W, H) in ((640, 480), (800, 600), (1024, 768)):
        img, Lo = compose(W, H)
        img.save(os.path.join(PREV, 'frame_%d.png' % W))
        clean, _ = compose(W, H, extras=False)
        clean.save(os.path.join(PREV, 'frame_%d_clean.png' % W))
    img, _ = compose(800, 600)
    img.crop((420, 0, 800, 330)).resize((760 * 2 // 2 * 2, 660), Image.NEAREST).save(
        os.path.join(PREV, 'zoom_panel_800_2x.png'))
    font_sample().save(os.path.join(PREV, 'fonts_1x.png'))
    fs = font_sample()
    fs.resize((fs.size[0] * 2, fs.size[1] * 2), Image.NEAREST).save(os.path.join(PREV, 'fonts_2x.png'))
    newspaper().save(os.path.join(PREV, 'newspaper_640.png'))
    pres = new(4 * 72 + 8, 130, P['paper'])
    pc = Cv(pres)
    for i, m in enumerate(MOODS):
        pc.blit(get('pres_frame_l'), 4 + i * 72, 4)
        pc.blit(get('pres_l_' + m), 6 + i * 72, 6)
        pc.blit(get('pres_frame_s'), 20 + i * 72, 92 - 4)
        pc.blit(get('pres_s_' + m), 22 + i * 72, 90 - 4 + 2)
    pres.resize((pres.size[0] * 3, pres.size[1] * 3), Image.NEAREST).save(os.path.join(PREV, 'president_3x.png'))
    icon_review().save(os.path.join(PREV, 'icons_review_3x.png'))
    print('wrote %s' % ', '.join(b for _, b, _ in sheet_rects))
    if '--check' in sys.argv:
        return check(sheet_rects, allfonts)
    return 0


if __name__ == '__main__':
    sys.exit(main())
