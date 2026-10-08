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
