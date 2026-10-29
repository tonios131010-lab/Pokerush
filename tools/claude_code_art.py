#!/usr/bin/env python3
"""Dessine CLAUDE CODE, le POKéMON secret de PokeRush, dans assets/claude_code/ (au format des
graphismes de l'expansion : anim_front.png 64×128 à deux images, back.png 64×64, icon.png 32×64,
normal.pal et shiny.pal).

    python tools/claude_code_art.py

Une étincelle couleur terre cuite à huit branches inégales, avec un écran de terminal pour visage :
« >_ ». Sa 2e image (animation de face) : les branches s'étirent et le curseur clignote. Tout est
calculé (forme en coordonnées polaires, contour, ombrage), rien n'est copié d'ailleurs.
"""
import math
from pathlib import Path

from PIL import Image

OUT = Path(__file__).resolve().parent.parent / "assets" / "claude_code"

NORMAL = [(152, 208, 160), (64, 28, 20), (160, 70, 46), (217, 119, 87), (236, 156, 122), (250, 214, 190),
          (30, 31, 44), (70, 74, 100), (244, 240, 228), (110, 226, 140)]
SHINY = [(152, 208, 160), (24, 28, 72), (60, 70, 170), (100, 120, 230), (150, 170, 245), (210, 220, 255),
         (30, 31, 44), (70, 74, 100), (244, 240, 228), (255, 200, 80)]
OUTLINE, DARK, BASE, LIGHT, SHINE, SCREEN, SCREEN_EDGE, TEXT, PROMPT = range(1, 10)

# icône : couleurs de la palette d'icônes n° 5 de l'expansion (indices de pal5.pal)
ICON_PAL = 5
I_OUT, I_BASE, I_LIGHT, I_SHINE, I_SCREEN, I_TEXT = 15, 10, 13, 7, 14, 3

RAYS = [1.0, 0.8, 0.94, 0.76, 1.0, 0.84, 0.9, 0.78]  # longueurs inégales, comme une étincelle dessinée


def spark(size, cx, cy, core, length, width, turn=0.0, stretch=0.0):
    """Masque de l'étincelle : un cœur rond et huit branches effilées."""
    inside = set()
    for y in range(size):
        for x in range(size):
            dx, dy = x + 0.5 - cx, y + 0.5 - cy
            r = math.hypot(dx, dy)
            if r <= core:
                inside.add((x, y))
                continue
            a = math.atan2(dy, dx)
            for k, f in enumerate(RAYS):
                t = k * math.pi / 4 + turn
                d = (a - t + math.pi) % (2 * math.pi) - math.pi
                lk = (length + stretch) * f
                along, across = r * math.cos(d), abs(r * math.sin(d))
                if 0 < along <= lk and across <= width * (1 - 0.55 * along / lk) + 0.4:
                    inside.add((x, y))
                    break
    return inside


def shade(px, inside, cx, cy, dark, base, light, shine, outline, light_from=(-1, -1)):
    """Contour, ombre du côté opposé à la lumière, reflet du côté de la lumière."""
    lx, ly = light_from
    for (x, y) in inside:
        nb = [(x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)]
        if any(n not in inside for n in nb):
            px[y][x] = outline
            continue
        toward = (x - lx, y - ly) not in inside  # le bord éclairé est juste là
        away = (x + lx, y + ly) not in inside or (x + 2 * lx, y + 2 * ly) not in inside
        dx, dy = x + 0.5 - cx, y + 0.5 - cy
        c = base
        if away:
            c = dark
        elif toward:
            c = light
        if (dx * lx + dy * ly) > 0 and math.hypot(dx, dy) < 9 and (dx * lx + dy * ly) / (math.hypot(dx, dy) + 0.1) > 0.6:
            c = shine if math.hypot(dx, dy) > 4 else light
        px[y][x] = c


def screen(px, x0, y0, w, h, blink, screen_c, edge, text, prompt):
    """Le visage : un écran arrondi avec l'invite « >_ »."""
    for y in range(y0, y0 + h):
        for x in range(x0, x0 + w):
            cx = min(x - x0, x0 + w - 1 - x)
            cy = min(y - y0, y0 + h - 1 - y)
            if cx + cy < 2:
                continue  # coins arrondis
            border = cx == 0 or cy == 0 or cx + cy == 2
            px[y][x] = edge if border else screen_c
    for x in range(x0 + 2, x0 + w - 2):
        px[y0 + 1][x] = edge  # reflet en haut de l'écran
    # « > »
    gx, gy = x0 + 3, y0 + 3
    for k, (dx, dy) in enumerate([(0, 0), (1, 1), (2, 2), (3, 3), (2, 4), (1, 5), (0, 6)]):
        px[gy + dy][gx + dx] = prompt
        px[gy + dy][gx + dx + 1] = prompt
    # « _ » qui clignote
    if not blink:
        for x in range(x0 + 9, x0 + 14):
            px[gy + 6][x] = text
            px[gy + 5][x] = text


def grid(size):
    return [[0] * size for _ in range(size)]


def front(blink):
    px = grid(64)
    cx, cy = 32, 31
    body = spark(64, cx, cy, 15, 29, 7.0, stretch=1.0 if blink else 0.0)
    shade(px, body, cx, cy, DARK, BASE, LIGHT, SHINE, OUTLINE)
    screen(px, 22, 25, 20, 13, blink, SCREEN, SCREEN_EDGE, TEXT, PROMPT)
    return px


def back():
    px = grid(64)
    cx, cy = 32, 34
    body = spark(64, cx, cy, 15, 30, 7.0, turn=math.pi / 8)
    shade(px, body, cx, cy, DARK, BASE, LIGHT, SHINE, OUTLINE, light_from=(1, -1))
    # au dos : un petit voyant d'alimentation
    for y in range(64):
        for x in range(64):
            r = math.hypot(x + 0.5 - cx, y + 0.5 - cy)
            if 4.5 <= r < 6:
                px[y][x] = SCREEN_EDGE
            elif r < 2.5:
                px[y][x] = PROMPT
    return px


def icon(bounce):
    px = grid(32)
    cx, cy = 16, 17 - bounce
    body = spark(32, cx, cy, 6.2, 14.5, 3.0)
    shade(px, body, cx, cy, I_BASE, I_BASE, I_LIGHT, I_SHINE, I_OUT)
    x0, y0 = 11, 14 - bounce
    for y in range(y0, y0 + 7):
        for x in range(x0, x0 + 11):
            corner = (x in (x0, x0 + 10)) and (y in (y0, y0 + 6))
            if not corner:
                px[y][x] = I_SCREEN
    for dx, dy in [(0, 0), (1, 1), (2, 2), (1, 3), (0, 4)]:
        px[y0 + 1 + dy][x0 + 2 + dx] = I_TEXT
    for x in range(x0 + 6, x0 + 9):
        px[y0 + 5][x] = I_TEXT
    return px


def save_png(path, rows, palette):
    h, w = len(rows), len(rows[0])
    im = Image.new("P", (w, h))
    flat = [c for rgb in palette for c in rgb]
    im.putpalette(flat + [0] * (768 - len(flat)))
    im.putdata([v for r in rows for v in r])
    im.save(path)


def save_jasc(path, palette):
    pal = list(palette) + [(0, 0, 0)] * (16 - len(palette))
    path.write_text("JASC-PAL\r\n0100\r\n16\r\n" + "".join(f"{r} {g} {b}\r\n" for r, g, b in pal))


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    save_png(OUT / "anim_front.png", front(False) + front(True), NORMAL)
    save_png(OUT / "back.png", back(), NORMAL)
    exp = Path(__file__).resolve().parent.parent / "pokeemerald-expansion-master" / "pokeemerald-expansion-master"
    lines = (exp / "graphics/pokemon/icon_palettes" / f"pal{ICON_PAL}.pal").read_text().split()
    n = int(lines[2])
    v = [int(t) for t in lines[3:3 + 3 * n]]
    save_png(OUT / "icon.png", icon(0) + icon(1), [tuple(v[i:i + 3]) for i in range(0, 3 * n, 3)])
    save_jasc(OUT / "normal.pal", NORMAL)
    save_jasc(OUT / "shiny.pal", SHINY)
    print("CLAUDE CODE ->", OUT)


if __name__ == "__main__":
    main()
