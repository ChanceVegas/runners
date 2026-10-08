#!/usr/bin/env python3
"""art_gen.py — host-side generator: pixel art -> src/art_data.h (const, in flash).

    python3 tools/art_gen.py            # write src/art_data.h
    python3 tools/art_gen.py --preview  # also write tools/art_preview.png (needs Pillow)

Sprites are palette-indexed (1 byte per pixel, 0 = transparent) and share ONE palette
(PALETTE below, native RGB565 at runtime; sprite.cpp converts it once to band-buffer
order). Hand-drawn sprites are text grids: one character per pixel, '.' = transparent,
other characters = PALETTE keys. Simple geometric sprites are generated in code.
"""
import os, sys

# key: (name, r, g, b)
PALETTE = [
    ('k', 'outline',     22, 22, 30),
    ('g', 'jersey',      30, 170, 80),
    ('G', 'jersey_hi',   90, 225, 130),
    ('d', 'jersey_dk',   18, 110, 52),
    ('s', 'skin',        240, 196, 150),
    ('S', 'skin_dk',     196, 146, 108),
    ('h', 'hair',        110, 60, 20),
    ('H', 'hair_hi',     160, 96, 40),
    ('b', 'shorts',      40, 50, 96),
    ('w', 'white',       255, 255, 255),
    ('W', 'grey_lt',     196, 198, 210),
    ('n', 'bib_red',     222, 40, 40),
    ('o', 'orange',      250, 130, 0),
    ('O', 'orange_lt',   255, 192, 96),
    ('y', 'yellow',      255, 210, 0),
    ('x', 'black',       24, 24, 24),
    ('m', 'metal',       84, 86, 96),
    ('M', 'metal_lt',    140, 144, 156),
    ('c', 'concrete',    58, 62, 72),
    ('C', 'concrete_lt', 112, 116, 128),
    ('e', 'concrete_dk', 38, 40, 48),
    ('u', 'gold',        240, 180, 20),
    ('U', 'gold_lt',     255, 240, 150),
    ('v', 'gold_dk',     176, 118, 10),
    ('a', 'orb',         40, 140, 225),
    ('A', 'orb_lt',      150, 230, 255),
    ('i', 'orb_dk',      22, 76, 160),
    # A2
    ('p', 'shade',       122, 62, 176),
    ('P', 'shade_lt',    176, 118, 226),
    ('q', 'shade_dk',    70, 32, 112),
    ('R', 'brute',       186, 42, 40),
    ('L', 'brute_lt',    234, 96, 82),
    ('D', 'brute_dk',    112, 20, 22),
    ('B', 'bone',        240, 222, 182),
    ('z', 'phantom',     64, 192, 224),
    ('Z', 'phantom_lt',  176, 242, 255),
    ('j', 'phantom_dk',  28, 108, 150),
    # A3: overworld terrain
    ('1', 'grass',       80, 160, 70),
    ('2', 'grass_dk',    62, 138, 56),
    ('3', 'grass_lt',    104, 184, 84),
    ('4', 'tuft',        44, 112, 42),
    ('5', 'flower_pk',   250, 140, 170),
    ('6', 'sand',        220, 200, 140),
    ('7', 'sand_dk',     194, 172, 116),
    ('8', 'sand_lt',     240, 226, 176),
    ('9', 'water',       40, 92, 172),
    ('0', 'water_dk',    30, 72, 150),
    ('!', 'water_lt',    150, 202, 242),
    ('@', 'forest_gnd',  50, 108, 48),
    ('#', 'canopy',      30, 88, 42),
    ('$', 'canopy_lt',   70, 142, 66),
    ('%', 'canopy_dk',   18, 60, 30),
    ('^', 'trunk',       96, 64, 36),
    ('&', 'rock',        132, 130, 126),
    ('*', 'rock_dk',     98, 96, 94),
    ('(', 'rock_lt',     172, 170, 164),
    (')', 'trail',       178, 142, 98),
    ('-', 'trail_dk',    148, 114, 76),
    ('+', 'trail_lt',    204, 170, 124),
]
KEY = {k: i + 1 for i, (k, *_rest) in enumerate(PALETTE)}   # 0 = transparent

# ---- Hand-drawn: runner, seen from behind (20 wide) --------------------------------
UPPER = [  # rows 0-19, shared by the run frames
    ".......kkkkkk.......",
    "......khhhhhhk......",
    ".....khhHHhhhhk.....",
    ".....khHHhhhhhk.....",
    ".....khhhhhhhhk.....",
    ".....kshhhhhhsk.....",
    "......kshhhhsk......",
    ".......kssssk.......",
    "....kkkggggggkkk....",
    "...kgGGggggggGGgk...",
    "..ksgGgggwwgggGgsk..",
    "..ksggggwnnwggggsk..",
    "..ksggggwnnwggggsk..",
    "..ksdgggwwwwgggdsk..",
    "..kSdggggggggggdSk..",
    "...kSddddddddddSk...",
    "...kkbbbbbbbbbbkk...",
    "....kbbbbbbbbbbk....",
    "....kbbbbkkbbbbk....",
    "....kbbbk..kbbbk....",
]
RUN_A_LEGS = [  # left leg lifted, right leg planted
    "....ksssk..kssk.....",
    "....ksssk..kssk.....",
    "....kSssk..kssk.....",
    "....kwwwk..kssk.....",
    "....kWWWk..kssk.....",
    ".....kkk...kssk.....",
    "...........kssk.....",
    "...........kSsk.....",
    "...........kssk.....",
    "...........kssk.....",
    "...........kssk.....",
    "...........kwwk.....",
    "..........kwwwwk....",
    "..........kWWWWk....",
    "...........kkkk.....",
]
RUN_B_LEGS = [  # passing: both legs down
    "....ksssk..ksssk....",
    "....ksssk..ksssk....",
    "....ksssk..ksssk....",
    "....ksssk..ksssk....",
    "....ksssk..ksssk....",
    "....ksssk..ksssk....",
    "....ksssk..ksssk....",
    "....kSssk..kssSk....",
    "....ksssk..ksssk....",
    "....ksssk..ksssk....",
    "....ksssk..ksssk....",
    "....kwwwk..kwwwk....",
    "...kwwwwk..kwwwwk...",
    "...kWWWWk..kWWWWk...",
    "....kkkk....kkkk....",
]
JUMP_LEGS = [  # tucked
    "....ksssskssssk.....",
    "....ksssskssssk.....",
    "....kSsssksssSk.....",
    "....kwwwwkwwwwk.....",
    "....kWWWWkWWWWk.....",
    ".....kkkk.kkkk......",
]
DUCK = [
    "......kkkkkkkk......",
    ".....khhhhhhhhk.....",
    "....khhHHhhhhhhk....",
    "...kkkhhhhhhhhkkk...",
    "..kgGGgkkkkkkgGGgk..",
    ".ksgGgggggggggggGgsk",
    ".ksggggwwnnwwggggsk.",
    ".kSddggggggggggddSk.",
    "..kkbbbbbbbbbbbbkk..",
    "..kbbbbbbbbbbbbbbk..",
    ".ksssskbbbbbbkssssk.",
    ".kssssk......kssssk.",
    ".kSsssk......ksssSk.",
    "kwwwwwk......kwwwwwk",
    "kWWWWWk......kWWWWWk",
    ".kkkkk........kkkkk.",
]
STUMBLE = [
    ".......kkkkkk.......",
    "......khhhhhhk......",
    ".....khhHHhhhhk.....",
    ".....khhhhhhhhk.....",
    "kkk..kshhhhhhsk..kkk",
    "kssk.kkgggggkk.kssk.",
    ".kssskgGGggGGgksssk.",
    "..kkkgggwnnwgggkkk..",
    "....kgggwwwwgggk....",
    "....kdddddddddddk...",
    "....kkbbbbbbbbbkk...",
    ".....kbbbbbbbbbk....",
    ".....kbbbk.kbbbk....",
    "....ksssk...ksssk...",
    "...ksssk.....ksssk..",
    "..ksssk.......ksssk.",
    ".kwwwwk........kwwwk",
    ".kWWWWk........kWWWk",
    "..kkkk..........kkk.",
]

# ---- Generated: obstacles, coin, orb ------------------------------------------------
def blank(w, h):
    return [['.'] * w for _ in range(h)]

def rows(g):
    return [''.join(r) for r in g]

def barrier():                     # 30x12 -> x4 = 120x48 (collision: jump over)
    w, h = 30, 12
    g = blank(w, h)
    for y in range(0, 7):          # board
        for x in range(w):
            if y == 0 or y == 6 or x == 0 or x == w - 1: g[y][x] = 'k'
            elif y == 1: g[y][x] = 'O'
            else: g[y][x] = 'w' if ((x + y) // 3) % 2 else 'o'
    for lx in (4, 23):             # legs + feet
        for y in range(7, h):
            for x in range(lx, lx + 3):
                g[y][x] = 'k' if x in (lx, lx + 2) else 'M'
        for x in range(lx - 1, lx + 4):
            g[h - 1][x] = 'k'
    return rows(g)

def duckbar():                     # 30x38 -> x4 = 120x152; open below 56 px (14 rows)
    w, h, beam_h = 30, 38, 24
    g = blank(w, h)
    for px in (0, w - 3):          # posts
        for y in range(h):
            for x in range(px, px + 3):
                g[y][x] = 'k' if x in (px, px + 2) else 'm'
    for y in range(beam_h):        # beam: yellow/black hazard stripes
        for x in range(3, w - 3):
            if y in (0, beam_h - 1): g[y][x] = 'k'
            else: g[y][x] = 'x' if ((x - y) // 4) % 2 else 'y'
    cx = w // 2                    # white "duck" arrow pointing down, under the stripes
    for i, y in enumerate(range(beam_h - 9, beam_h - 1)):
        half = 5 - i if i < 6 else 0
        if i < 6:
            for x in range(cx - half - 1, cx + half + 1):
                g[y][x] = 'w'
    for y in range(beam_h - 15, beam_h - 9):
        for x in range(cx - 2, cx + 2):
            g[y][x] = 'w'
    return rows(g)

def wall():                        # 30x42 -> x4 = 120x168 (collision: dodge, box 170)
    w, h = 30, 42
    g = blank(w, h)
    for y in range(h):
        for x in range(w):
            if x == 0 or x == w - 1 or y == 0: g[y][x] = 'k'
            elif y <= 3: g[y][x] = 'C'
            elif x >= w - 5: g[y][x] = 'e'
            elif (y % 7 == 0) or ((x + (8 if (y // 7) % 2 else 0)) % 16 == 0): g[y][x] = 'e'
            else: g[y][x] = 'c'
    for y in range(13, 19):        # warning band
        for x in range(4, w - 6):
            g[y][x] = 'k' if y in (13, 18) else ('x' if ((x + y) // 3) % 2 else 'y')
    return rows(g)

def coin():                        # 11x11
    w = h = 11
    g = blank(w, h)
    c = (w - 1) / 2
    for y in range(h):
        for x in range(w):
            d = ((x - c) ** 2 + (y - c) ** 2) ** 0.5
            if d <= 5.4: g[y][x] = 'k'
            if d <= 4.4: g[y][x] = 'v'
            if d <= 3.6: g[y][x] = 'u'
    for y in range(3, 8): g[y][5] = 'v'           # embossed stripe
    for (x, y) in ((3, 3), (4, 2), (3, 4)): g[y][x] = 'U'
    return rows(g)

def orb():                         # 13x13
    w = h = 13
    g = blank(w, h)
    c = (w - 1) / 2
    for y in range(h):
        for x in range(w):
            d = ((x - c) ** 2 + (y - c) ** 2) ** 0.5
            if d <= 6.4: g[y][x] = 'i'
            if d <= 5.4: g[y][x] = 'a'
            if d <= 3.4: g[y][x] = 'A'
    for (x, y) in ((4, 4), (5, 4), (4, 5)): g[y][x] = 'w'
    return rows(g)

# ---- A2: shape helpers (draw filled shapes, then auto-outline in 'k') -----------
def fill_circle(g, cx, cy, r, ch):
    for y in range(len(g)):
        for x in range(len(g[0])):
            if (x - cx) ** 2 + (y - cy) ** 2 <= r * r: g[y][x] = ch

def fill_ellipse(g, cx, cy, rx, ry, ch):
    for y in range(len(g)):
        for x in range(len(g[0])):
            if ((x - cx) / rx) ** 2 + ((y - cy) / ry) ** 2 <= 1.0: g[y][x] = ch

def fill_rect(g, x0, y0, x1, y1, ch):          # inclusive
    for y in range(max(0, y0), min(len(g), y1 + 1)):
        for x in range(max(0, x0), min(len(g[0]), x1 + 1)):
            g[y][x] = ch

def recolor(g, frm, to, pred):
    for y in range(len(g)):
        for x in range(len(g[0])):
            if g[y][x] == frm and pred(x, y): g[y][x] = to

def put(g, pts, ch):
    for (x, y) in pts:
        if 0 <= y < len(g) and 0 <= x < len(g[0]): g[y][x] = ch

def outline(g):
    h, w = len(g), len(g[0])
    src = [r[:] for r in g]
    for y in range(h):
        for x in range(w):
            if src[y][x] != '.': continue
            for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                nx, ny = x + dx, y + dy
                if 0 <= nx < w and 0 <= ny < h and src[ny][nx] not in '.k':
                    g[y][x] = 'k'; break
    return g

# Enemies: BACK view (ahead of you in the Hunt, 2 run frames) and FRONT view (looming
# behind you in the Pursuit). Back 22x28, front 28x32.
def shade_back(frame):
    g = blank(22, 28)
    fill_circle(g, 10.5, 9, 8.5, 'p')
    fill_rect(g, 2, 9, 19, 22, 'p')
    for x in range(2, 20):                         # wavy tail, phase by frame
        if ((x + frame * 3) // 3) % 2 == 0: fill_rect(g, x, 23, x, 25, 'p')
    recolor(g, 'p', 'q', lambda x, y: x >= 15)     # shadow side
    recolor(g, 'p', 'q', lambda x, y: y >= 20 and (x + y) % 5 == 0)
    put(g, [(6, 4), (7, 3), (5, 5), (6, 5), (8, 3)], 'P')   # rim light
    fill_rect(g, 9, 6, 12, 18, 'q')                # hood seam down the back
    return rows(outline(g))

def shade_front():
    g = blank(28, 32)
    fill_circle(g, 13.5, 11, 10, 'p')
    fill_rect(g, 4, 11, 23, 26, 'p')
    for x in range(4, 24):
        if (x // 3) % 2 == 0: fill_rect(g, x, 27, x, 29, 'p')
    fill_rect(g, 0, 15, 4, 17, 'p'); fill_rect(g, 23, 15, 27, 17, 'p')   # arms out
    put(g, [(0, 14), (1, 13), (27, 14), (26, 13)], 'P')                  # claws
    recolor(g, 'p', 'q', lambda x, y: x >= 19 and y > 6)
    put(g, [(7, 4), (8, 3), (6, 5), (9, 3)], 'P')
    fill_ellipse(g, 9.5, 12, 2.6, 3.4, 'w'); fill_ellipse(g, 17.5, 12, 2.6, 3.4, 'w')
    put(g, [(10, 13), (10, 12), (17, 13), (17, 12)], 'x')                # pupils
    put(g, [(6, 7), (7, 8), (8, 8), (12, 9), (15, 9), (19, 8), (20, 8), (21, 7)], 'k')   # angry brows
    for x in range(8, 20):                                               # jagged grin
        g[19 + (x % 2)][x] = 'k'
    put(g, [(9, 19), (11, 19), (13, 19), (15, 19), (17, 19)], 'w')
    return rows(outline(g))

def brute_back(frame):
    g = blank(22, 28)
    fill_rect(g, 2, 7, 19, 18, 'R')                # hulking back
    fill_circle(g, 10.5, 5, 4, 'R')                # head
    put(g, [(4, 2), (3, 1), (3, 0), (5, 3), (17, 2), (18, 1), (18, 0), (16, 3)], 'B')   # horns
    fill_rect(g, 0, 9, 1, 18, 'R'); fill_rect(g, 20, 9, 21, 18, 'R')     # arms
    fill_rect(g, 0, 19, 2, 20, 'D'); fill_rect(g, 19, 19, 21, 20, 'D')   # fists
    la, lb = (25, 22) if frame == 0 else (22, 25)                        # stride
    fill_rect(g, 5, 19, 9, la, 'D'); fill_rect(g, 12, 19, 16, lb, 'D')
    fill_rect(g, 4, la + 1, 9, la + 2, 'k'); fill_rect(g, 12, lb + 1, 17, lb + 2, 'k')
    fill_rect(g, 10, 8, 11, 17, 'D')               # spine
    recolor(g, 'R', 'D', lambda x, y: x >= 17 and y >= 8)
    put(g, [(4, 8), (5, 8), (6, 8), (4, 9)], 'L')
    return rows(outline(g))

def brute_front():
    g = blank(28, 32)
    fill_rect(g, 3, 10, 24, 26, 'R')
    fill_circle(g, 13.5, 8, 6, 'R')
    put(g, [(7, 4), (6, 3), (5, 2), (5, 1), (20, 4), (21, 3), (22, 2), (22, 1)], 'B')
    fill_rect(g, 0, 8, 3, 16, 'R'); fill_rect(g, 24, 8, 27, 16, 'R')     # raised arms
    fill_rect(g, 0, 4, 3, 8, 'D'); fill_rect(g, 24, 4, 27, 8, 'D')       # fists up
    fill_rect(g, 8, 13, 19, 22, 'L')               # chest
    recolor(g, 'R', 'D', lambda x, y: x >= 21 and y > 9)
    put(g, [(10, 7), (11, 7), (16, 7), (17, 7)], 'y')                     # eyes
    put(g, [(9, 5), (10, 6), (12, 6), (15, 6), (17, 6), (18, 5)], 'k')    # brow
    fill_rect(g, 10, 10, 17, 11, 'D')              # mouth
    put(g, [(10, 9), (17, 9), (10, 8), (17, 8)], 'B')                     # tusks
    fill_rect(g, 6, 27, 11, 30, 'D'); fill_rect(g, 16, 27, 21, 30, 'D')   # legs
    return rows(outline(g))

def phantom_back(frame):
    g = blank(22, 28)
    fill_circle(g, 10.5, 9, 8, 'z')
    fill_circle(g, 10.5, 9, 4.5, 'Z')
    for i, tx in enumerate((5, 10, 15)):           # trailing wisps
        for y in range(16, 27):
            x = tx + int(1.5 * ((((y + frame * 2 + i) // 3) % 2) * 2 - 1))
            fill_rect(g, x, y, x + (2 if y < 22 else 1), y, 'z')
    recolor(g, 'z', 'j', lambda x, y: x >= 15 or y >= 22)
    put(g, [(7, 5), (8, 4), (7, 6)], 'w')
    return rows(outline(g))

def phantom_front():
    g = blank(28, 32)
    fill_circle(g, 13.5, 12, 11, 'z')
    for i, tx in enumerate((6, 12, 18)):
        for y in range(21, 31):
            x = tx + (1 if ((y + i) // 3) % 2 else -1)
            fill_rect(g, x, y, x + 3, y, 'z')
    fill_circle(g, 13.5, 12, 6, 'Z')
    recolor(g, 'z', 'j', lambda x, y: x >= 21 or y >= 25)
    fill_ellipse(g, 9.5, 11, 2.2, 3.2, 'j'); fill_ellipse(g, 17.5, 11, 2.2, 3.2, 'j')   # hollow eyes
    put(g, [(9, 10), (17, 10)], 'w')
    fill_ellipse(g, 13.5, 17, 2.5, 1.6, 'j')       # "oo" mouth
    return rows(outline(g))

def runner_down():                 # 36x10 -> x2 = 72x20 (lying on the road)
    g = blank(36, 10)
    fill_rect(g, 1, 5, 4, 8, 'w')                  # shoes
    fill_rect(g, 5, 5, 11, 8, 's')                 # legs
    fill_rect(g, 12, 4, 16, 8, 'b')                # shorts
    fill_rect(g, 17, 2, 27, 8, 'g')                # jersey
    fill_rect(g, 20, 4, 23, 6, 'w'); put(g, [(21, 5), (22, 5)], 'n')
    fill_rect(g, 18, 9, 25, 9, 'S')                # arm
    fill_rect(g, 28, 1, 34, 8, 'h')                # head (hair)
    fill_rect(g, 28, 5, 29, 7, 's')
    put(g, [(30, 2), (31, 2)], 'H')
    return rows(outline(g))

def heart(full):                   # 9x8
    g = blank(9, 8)
    c = 'n' if full else 'm'
    fill_circle(g, 2.5, 2.5, 2.2, c); fill_circle(g, 5.5, 2.5, 2.2, c)
    for i, y in enumerate(range(3, 8)):
        fill_rect(g, i, y, 8 - i, y, c)
    if full: put(g, [(2, 1), (1, 2)], 'L')
    return rows(outline(g))

def shield():                      # 10x11
    g = blank(10, 11)
    fill_rect(g, 1, 1, 8, 5, 'a')
    for i, y in enumerate(range(6, 10)):
        fill_rect(g, 1 + i, y, 8 - i, y, 'a')
    fill_rect(g, 4, 2, 5, 7, 'A')                  # emblem bar
    fill_rect(g, 2, 3, 7, 4, 'A')
    recolor(g, 'a', 'i', lambda x, y: x >= 7)
    return rows(outline(g))

def sneaker(main, light):          # 16x12 side view
    g = blank(16, 12)
    fill_rect(g, 1, 9, 14, 10, 'w')                # sole
    fill_rect(g, 1, 5, 6, 8, main)                 # heel
    for i, y in enumerate(range(5, 9)):            # toe slope
        fill_rect(g, 6, y, 14 - (3 - i) * 2, y, main)
    fill_rect(g, 2, 2, 6, 4, main)                 # collar
    put(g, [(7, 5), (8, 6), (9, 5), (10, 6)], 'w')     # laces
    put(g, [(3, 6), (4, 7), (5, 6), (11, 8), (12, 8)], light)   # swoosh-ish stripe
    return rows(outline(g))

def can(main, light, mark):        # 12x16 drink can
    g = blank(12, 16)
    fill_rect(g, 2, 2, 9, 14, main)
    fill_rect(g, 2, 1, 9, 1, 'M'); fill_rect(g, 2, 15, 9, 15, 'M')
    fill_rect(g, 3, 2, 3, 13, light)               # shine
    fill_rect(g, 2, 6, 9, 10, 'w')                 # label
    put(g, mark, main)
    put(g, [(6, 0)], 'm')                          # tab
    return rows(outline(g))

# drink label marks (inside the label rows 6-10)
MARK_RUSH  = [(4, 8), (5, 7), (6, 8), (7, 7), (5, 9), (6, 9)]         # chevrons
MARK_GUARD = [(5, 7), (6, 7), (5, 8), (6, 8), (4, 7), (7, 7), (5, 9), (6, 9)]   # shield blob
MARK_SURGE = [(6, 6), (5, 7), (6, 8), (5, 9), (4, 10), (7, 7)]        # bolt

# ---- A3: overworld tiles (24x24, fully opaque) and the map avatar ---------------
import random

def tile(base, speck, seed, density=0.08):
    rnd = random.Random(seed)
    g = [[base] * 24 for _ in range(24)]
    for y in range(24):
        for x in range(24):
            r = rnd.random()
            if r < density: g[y][x] = speck[0]
            elif r < density * 2: g[y][x] = speck[1]
    return g, rnd

def grass_tile(v):
    g, rnd = tile('1', '23', 10 + v)
    if v == 1:                                     # flowers
        for (fx, fy, c) in ((5, 6, 'y'), (16, 14, '5'), (10, 18, 'w')):
            put(g, [(fx, fy), (fx + 1, fy), (fx, fy + 1), (fx + 1, fy + 1)], c)
            put(g, [(fx, fy + 2)], '4')
    if v == 2:                                     # grass tufts
        for (tx, ty) in ((4, 15), (14, 7), (17, 19)):
            put(g, [(tx, ty), (tx + 2, ty), (tx + 4, ty), (tx + 1, ty + 1), (tx + 3, ty + 1),
                    (tx + 2, ty + 2)], '4')
    return rows(g)

def sand_tile(v):
    g, rnd = tile('6', '78', 20 + v, 0.07)
    if v == 1:
        fill_ellipse(g, 15, 15, 2.2, 1.6, '7'); put(g, [(14, 14)], '8')   # pebble
    return rows(g)

def water_tile(frame):
    g = [['9'] * 24 for _ in range(24)]
    rnd = random.Random(30)
    for _ in range(9):                             # dark swells
        x, y = rnd.randrange(24), rnd.randrange(24)
        for i in range(5): g[y][(x + i) % 24] = '0'
    for i in range(4):                             # wave crests drift with the frame
        x, y = rnd.randrange(24), rnd.randrange(24)
        x = (x + frame * 3) % 24
        for k in range(4): g[y][(x + k) % 24] = '!'
        g[(y + 1) % 24][(x + 4) % 24] = '!'
    return rows(g)

def forest_tile(v):
    g, rnd = tile('@', '24', 40 + v, 0.05)
    cx = 12 + (2 if v else -1)
    fill_ellipse(g, cx + 1, 20, 7, 2.4, '%')       # ground shadow
    fill_rect(g, cx - 1, 15, cx + 1, 20, '^')      # trunk
    fill_circle(g, cx, 9, 8.4, '#')                # canopy
    recolor(g, '#', '%', lambda x, y: (x - cx) + (y - 9) > 7)
    fill_circle(g, cx - 3, 6, 3.2, '$')
    put(g, [(cx + 3, 4), (cx + 4, 8), (cx - 5, 11)], '$')
    return rows(g)

def rock_tile(v):
    g, rnd = tile('&', '*(', 50 + v, 0.06)
    if v == 1:                                     # boulder
        fill_ellipse(g, 12, 13, 8, 6, '*')
        fill_ellipse(g, 11, 12, 7, 5, '&')
        fill_ellipse(g, 9, 10, 3, 2, '(')
    else:                                          # cracks
        put(g, [(4, 6), (5, 7), (6, 7), (7, 8), (17, 15), (18, 16), (18, 17), (19, 18)], '*')
    return rows(g)

def trail_tile(v):
    g, rnd = tile(')', '-+', 60 + v, 0.09)
    for (px, py) in ((6 + v * 5, 7), (15, 16 - v * 6)):
        put(g, [(px, py), (px + 1, py)], '-')
    return rows(g)

# Map avatar, 3/4 view, 12x15 (drawn 2x): down / up / side (flip for left), 2 walk frames.
AV_HEAD_DOWN = [
    "....kkkk....",
    "...khhhhk...",
    "..khhHhhhk..",
    "..khsssshk..",
    "..ksxssxsk..",
    "...kssssk...",
]
AV_HEAD_UP = [
    "....kkkk....",
    "...khhhhk...",
    "..khhHhhhk..",
    "..khhhhhhk..",
    "..khhhhhhk..",
    "...kshhsk...",
]
AV_HEAD_SIDE = [
    "....kkkk....",
    "...khhhhk...",
    "..khhHhhhk..",
    "..khhhsssk..",
    "..khhssxsk..",
    "...kkssssk..",
]
AV_BODY_FRONT = [
    "..kkggggkk..",
    ".ksgwnnwgsk.",
    ".ksgwwwwgsk.",
    ".kkggggggkk.",
    "..kbbbbbbk..",
]
AV_BODY_BACK = [
    "..kkggggkk..",
    ".ksggggggsk.",
    ".ksgGGGGgsk.",
    ".kkggggggkk.",
    "..kbbbbbbk..",
]
AV_BODY_SIDE = [
    "...kggggk...",
    "...kggggsk..",
    "...kgnngsk..",
    "...kggggkk..",
    "...kbbbbk...",
]
AV_FEET = [[  # frame 0 / 1, front & back
    "..kbbkkbbk..",
    "..kwk..kwk..",
    "...k....k...",
    "............",
], [
    "..kbbkkbbk..",
    "..kwk...kk..",
    "...k...kwk..",
    "........k...",
]]
AV_FEET_SIDE = [[
    "...kbbbbk...",
    "...kwkkwk...",
    "....k..k....",
    "............",
], [
    "...kbbbbk...",
    "..kwk..kwk..",
    "...k....k...",
    "............",
]]

SPRITES = [
    ('RUN_A',   UPPER + RUN_A_LEGS),
    ('RUN_B',   UPPER + RUN_B_LEGS),
    ('JUMP',    UPPER + JUMP_LEGS),
    ('DUCK',    DUCK),
    ('STUMBLE', STUMBLE),
    ('BARRIER', barrier()),
    ('DUCKBAR', duckbar()),
    ('WALL',    wall()),
    ('COIN',    coin()),
    ('ORB',     orb()),
    # A2
    ('SHADE_B0',   shade_back(0)),
    ('SHADE_B1',   shade_back(1)),
    ('SHADE_F',    shade_front()),
    ('BRUTE_B0',   brute_back(0)),
    ('BRUTE_B1',   brute_back(1)),
    ('BRUTE_F',    brute_front()),
    ('PHANTOM_B0', phantom_back(0)),
    ('PHANTOM_B1', phantom_back(1)),
    ('PHANTOM_F',  phantom_front()),
    ('DOWN',       runner_down()),
    ('HEART',      heart(True)),
    ('HEART_EMPTY', heart(False)),
    ('SHIELD',     shield()),
    ('SNEAK_SPRINT', sneaker('R', 'L')),
    ('SNEAK_SPRING', sneaker('g', 'G')),
    ('SNEAK_GRIP',   sneaker('a', 'A')),
    ('CAN_RUSH',  can('o', 'O', MARK_RUSH)),
    ('CAN_GUARD', can('a', 'A', MARK_GUARD)),
    ('CAN_SURGE', can('y', 'U', MARK_SURGE)),
    # A3 map tiles (24x24, opaque) — order matters: overworld indexes them
    ('T_GRASS0', grass_tile(0)), ('T_GRASS1', grass_tile(1)), ('T_GRASS2', grass_tile(2)),
    ('T_SAND0', sand_tile(0)),   ('T_SAND1', sand_tile(1)),
    ('T_WATER0', water_tile(0)), ('T_WATER1', water_tile(1)),
    ('T_FOREST0', forest_tile(0)), ('T_FOREST1', forest_tile(1)),
    ('T_ROCK0', rock_tile(0)),   ('T_ROCK1', rock_tile(1)),
    ('T_TRAIL0', trail_tile(0)), ('T_TRAIL1', trail_tile(1)),
    # A3 map avatar
    ('AV_DOWN0', AV_HEAD_DOWN + AV_BODY_FRONT + AV_FEET[0]),
    ('AV_DOWN1', AV_HEAD_DOWN + AV_BODY_FRONT + AV_FEET[1]),
    ('AV_UP0',   AV_HEAD_UP + AV_BODY_BACK + AV_FEET[0]),
    ('AV_UP1',   AV_HEAD_UP + AV_BODY_BACK + AV_FEET[1]),
    ('AV_SIDE0', AV_HEAD_SIDE + AV_BODY_SIDE + AV_FEET_SIDE[0]),
    ('AV_SIDE1', AV_HEAD_SIDE + AV_BODY_SIDE + AV_FEET_SIDE[1]),
]

def check(name, g):
    w = len(g[0])
    for i, r in enumerate(g):
        assert len(r) == w, f"{name} row {i}: width {len(r)} != {w}: '{r}'"
        for ch in r:
            assert ch == '.' or ch in KEY, f"{name} row {i}: unknown colour '{ch}'"
    assert w <= 255 and len(g) <= 255
    return w, len(g)

def rgb565(r, g, b):
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)

def main():
    out = ["// art_data.h — GENERATED by tools/art_gen.py. Do not edit by hand.",
           "// Palette-indexed sprites (1 byte/pixel, 0 = transparent), one shared palette",
           "// in native RGB565 (sprite.cpp converts it to band-buffer order once).",
           "#pragma once", "#include <stdint.h>", "",
           f"constexpr int ART_PALETTE_N = {len(PALETTE) + 1};",
           "const uint16_t ART_PALETTE[ART_PALETTE_N] = { 0x0000,"]
    for k, name, r, g, b in PALETTE:
        out.append(f"  0x{rgb565(r, g, b):04X},   // {k} {name}")
    out.append("};")
    out.append("")
    out.append("struct ArtSprite { uint8_t w, h; const uint8_t* px; };")
    out.append("")
    total = 0
    for name, g in SPRITES:
        w, h = check(name, g)
        data = [0 if ch == '.' else KEY[ch] for r in g for ch in r]
        total += len(data)
        out.append(f"const uint8_t ART_{name}_PX[{len(data)}] = {{")
        for i in range(0, len(data), 32):
            out.append("  " + ",".join(str(v) for v in data[i:i + 32]) + ",")
        out.append("};")
        out.append(f"const ArtSprite ART_{name} = {{ {w}, {h}, ART_{name}_PX }};")
        out.append("")
    path = os.path.join(os.path.dirname(__file__), "..", "src", "art_data.h")
    with open(path, "w") as f:
        f.write("\n".join(out) + "\n")
    print("wrote", os.path.normpath(path), f"({len(SPRITES)} sprites, {total} bytes)")

    if "--preview" in sys.argv:
        from PIL import Image
        scale, pad = 4, 8
        imgs = []
        for name, g in SPRITES:
            w, h = len(g[0]), len(g)
            im = Image.new("RGB", (w * scale, h * scale), (90, 130, 90))
            for y, r in enumerate(g):
                for x, ch in enumerate(r):
                    if ch == '.': continue
                    _, _, cr, cg, cb = PALETTE[KEY[ch] - 1]
                    for dy in range(scale):
                        for dx in range(scale):
                            im.putpixel((x * scale + dx, y * scale + dy), (cr, cg, cb))
            imgs.append(im)
        W = sum(i.width for i in imgs) + pad * (len(imgs) + 1)
        H = max(i.height for i in imgs) + 2 * pad
        sheet = Image.new("RGB", (W, H), (40, 40, 48))
        x = pad
        for im in imgs:
            sheet.paste(im, (x, H - pad - im.height)); x += im.width + pad
        p = os.path.join(os.path.dirname(__file__), "art_preview.png")
        sheet.save(p)
        print("preview", os.path.normpath(p))

if __name__ == "__main__":
    main()
