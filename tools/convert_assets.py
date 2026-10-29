#!/usr/bin/env python3
"""Graphismes -> C pour PokeRush : pixels indexés 4 ou 8 bpp et palettes RGB565 (format : src/gfx/image.h).

    python tools/convert_assets.py assets build/gen

Presque tout vient de pokeemerald-expansion (un seul style, celui d'Émeraude) :
  Pokémon      face (les deux images de anim_front.png), dos, palettes normale et chromatique,
               icône des menus et sa palette ; formes Méga et à objet comprises, dans l'ordre de
               species_info (tools/expansion.py, Expansion.roster) ;
  dresseurs    graphics/trainers/front_pics : les classes ordinaires et les personnages de
               assets/manifest.json, en sprites entiers ;
  polices      latin_normal et latin_small, avec charmap.txt et les avances de src/fonts.c ;
  interface    cadres de PV, barre de PV (son « HP » devient « PV »), Poké Balls de l'équipe,
               symbole Méga, icônes de catégorie, fenêtre standard.
assets/manifest.json garde les fonds de combat et les boîtes de dialogue tirés des planches.

Écrit dans build/gen/ : assets.h, assets.c, et preview.png (tout ce qui a été extrait, pour vérifier
d'un coup d'oeil).
"""
import json
import re
import sys
import unicodedata
import zlib
from collections import Counter
from pathlib import Path

from PIL import Image, ImageDraw

sys.path.insert(0, str(Path(__file__).resolve().parent))
from expansion import Expansion  # noqa: E402

MAX_OFS = 1 << 22  # largeur des champs de Image (src/gfx/image.h)
MAX_PAL = 1 << 11
FRAME, ICON = 64, 32

warnings = []


class AssetError(Exception):
    pass


def warn(msg):
    warnings.append(msg)


def parse_color(s):
    s = s.lstrip("#")
    return tuple(int(s[i:i + 2], 16) for i in (0, 2, 4))


def rgb565(c):
    return (c[0] >> 3) << 11 | (c[1] >> 2) << 5 | c[2] >> 3


def runs(flags):
    """Les suites de False de `flags` : [(début, fin), ...]."""
    out, start = [], None
    for i, f in enumerate(list(flags) + [True]):
        if not f and start is None:
            start = i
        elif f and start is not None:
            out.append((start, i))
            start = None
    return out


def jasc(path):
    lines = Path(path).read_text().split()
    if lines[0] != "JASC-PAL":
        raise AssetError(f"{path} : pas une palette JASC")
    n = int(lines[2])
    v = [int(x) for x in lines[3:3 + 3 * n]]
    return [tuple(v[i:i + 3]) for i in range(0, 3 * n, 3)]


def indexed(path):
    """(largeur, hauteur, indices, palette RGB) d'un PNG en couleurs indexées."""
    im = Image.open(path)
    if im.mode != "P":
        raise AssetError(f"{path} : PNG non indexé ({im.mode})")
    raw = im.getpalette() or []
    pal = [tuple(raw[i:i + 3]) for i in range(0, len(raw), 3)]
    return im.width, im.height, list(im.getdata()), pal


def crop_indices(w, h, idx, x0, y0, x1, y1):
    """Le cadre (x0, y0)-(x1, y1), recadré sur ses pixels non nuls : (indices, w, h, dx, dy) ou None."""
    rows = [idx[y * w + x0:y * w + x1] for y in range(y0, y1)]
    ys = [j for j, r in enumerate(rows) if any(r)]
    if not ys:
        return None
    xs = [i for i in range(x1 - x0) if any(r[i] for r in rows)]
    cx0, cx1, cy0, cy1 = xs[0], xs[-1] + 1, ys[0], ys[-1] + 1
    return [r[cx0:cx1] for r in rows[cy0:cy1]], cx1 - cx0, cy1 - cy0, cx0, cy0


def deflate(data, prefix=b""):
    """Compression deflate brute (RFC 1951, zlib niveau 9), décodée par src/gfx/display.c ; prefix :
    dictionnaire déjà décodé juste avant (la 1re image d'une animation pour la 2e)."""
    if prefix:
        c = zlib.compressobj(9, zlib.DEFLATED, -15, 9, zlib.Z_DEFAULT_STRATEGY, zdict=bytes(prefix))
    else:
        c = zlib.compressobj(9, zlib.DEFLATED, -15, 9)
    return c.compress(bytes(data)) + c.flush()


class Store:
    """Les pixels et les palettes de toutes les images, dans l'ordre où elles sont ajoutées."""

    def __init__(self):
        self.pixels = bytearray()
        self.palettes = []  # couleurs RGB565, par blocs de 16
        self.pal_blocks = {}  # palette -> premier bloc (les palettes identiques sont partagées)
        self.count = 0

    def palette(self, colors, share=True):
        pal = tuple(rgb565(c) for c in colors) + (0,) * (16 - len(colors))
        if share and pal in self.pal_blocks:
            return self.pal_blocks[pal]
        block = len(self.palettes) // 16
        self.pal_blocks.setdefault(pal, block)
        self.palettes.extend(pal)
        return block

    def pixels4(self, rows):
        ofs = len(self.pixels)
        for line in rows:
            line = list(line) + [0] * (len(line) & 1)
            self.pixels.extend(line[i] | line[i + 1] << 4 for i in range(0, len(line), 2))
        if len(self.pixels) > MAX_OFS or len(self.palettes) // 16 > MAX_PAL:
            raise AssetError("trop de pixels ou de palettes pour les champs de Image")
        self.count += 1
        return ofs

    def add_indexed(self, rows, w, h, block, x, y, key=True, pack=False):
        if not (1 <= w < 512 and 1 <= h < 256 and 0 <= x < 512 and 0 <= y < 256):
            raise AssetError(f"{w}x{h} en ({x}, {y}) ne tient pas dans Image")
        if any(v > 15 for r in rows for v in r):
            raise AssetError("indice de couleur > 15 dans une image 4 bpp")
        ofs = self.pixels4(rows)
        packed = 0
        if pack and w <= 64 and h <= 64:  # sprites : compressés s'ils y gagnent
            raw = bytes(self.pixels[ofs:])
            z = deflate(raw)
            if len(z) < len(raw):
                del self.pixels[ofs:]
                self.pixels.extend(z)
                packed = 1
        return dict(ofs=ofs, pal=block, w=w, h=h, x=x, bpp8=0, key=int(key), y=y, z=packed)

    def add_frame2(self, rows, first_rows, w, h, block, x, y):
        """2e image d'une animation : deflate avec la 1re pour dictionnaire."""
        def raw(rs):
            b = bytearray()
            for line in rs:
                line = list(line) + [0] * (len(line) & 1)
                b.extend(line[i] | line[i + 1] << 4 for i in range(0, len(line), 2))
            return b
        ofs = len(self.pixels)
        self.pixels.extend(deflate(raw(rows), prefix=raw(first_rows)))
        if len(self.pixels) > MAX_OFS:
            raise AssetError("trop de pixels pour les champs de Image")
        self.count += 1
        return dict(ofs=ofs, pal=block, w=w, h=h, x=x, bpp8=0, key=1, y=y, z=1)

    def add_rgba(self, img, key, x=0, y=0, what=""):
        """img : RGBA ; key : couleur RGB transparente ou None. 4 bpp si au plus 15 couleurs, sinon 8."""
        w, h = img.size
        data = list(img.getdata())
        opaque = [p[3] >= 128 and (key is None or p[:3] != key) for p in data]
        pixels = [p[:3] for p, o in zip(data, opaque) if o]
        colors = sorted(set(pixels))
        bpp = 4 if len(colors) <= 15 else 8
        if len(colors) > 255:
            raise AssetError(f"{what} : trop de couleurs")
        pos = {c: i + 1 for i, c in enumerate(colors)}
        idx = [pos[p[:3]] if o else 0 for p, o in zip(data, opaque)]
        size = 16 if bpp == 4 else 256
        pal = [key or (0, 0, 0)] + colors
        pal += [(0, 0, 0)] * (size - len(pal))
        if bpp == 4:
            block = self.palette(pal)
            ofs = self.pixels4([idx[r * w:(r + 1) * w] for r in range(h)])
        else:
            block = len(self.palettes) // 16
            self.palettes.extend(rgb565(c) for c in pal)
            ofs = len(self.pixels)
            self.pixels.extend(idx)
            self.count += 1
        return dict(ofs=ofs, pal=block, w=w, h=h, x=x, bpp8=int(bpp == 8), key=int(not all(opaque)), y=y, z=0)


def preview_of(rows, pal):
    h, w = len(rows), len(rows[0])
    im = Image.new("RGBA", (w, h))
    im.putdata([pal[v] + (255,) if v else (0, 0, 0, 0) for r in rows for v in r])
    return im


# --------------------------------------------------------------------------- sortie C


def c_image(d):
    return ("{.ofs = %(ofs)d, .pal = %(pal)d, .w = %(w)d, .h = %(h)d, .x = %(x)d, "
            ".bpp8 = %(bpp8)d, .key = %(key)d, .z = %(z)d, .y = %(y)d}" % d)


def c_array(values, per_line):
    vals = [str(v) for v in values]
    return "\n".join("  " + ",".join(vals[i:i + per_line]) + "," for i in range(0, len(vals), per_line))


def c_str(s):
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'


def ascii_label(s):
    return unicodedata.normalize("NFKD", s).encode("ascii", "ignore").decode()


class Converter:
    def __init__(self, root, out):
        self.root, self.out = root, out
        self.x = Expansion()
        self.gfx = self.x.root / "graphics"
        self.store = Store()
        self.decls, self.defs, self.others = [], [], []

    def emit(self, decl, define):
        self.decls.append(decl)
        self.defs.append(define)

    # ----------------------------------------------------------------------- Pokémon
    def pokemon(self, data_dir):
        roster = self.x.roster(data_dir)
        g = self.x.graphics()
        fronts, fronts2, backs, icons, tiles = [], [], [], [], []
        none = dict(ofs=0, pal=0, w=0, h=0, x=0, bpp8=0, key=0, y=0, z=0)
        icon_pals = [self.store.palette(jasc(self.gfx / "pokemon/icon_palettes" / f"pal{i}.pal")) for i in range(6)]
        for i, (cname, fr, sp, legend, base, stone, kind) in enumerate(roster):
            what = f"{cname} ({fr})"
            try:
                paths = {k: sp[k] if isinstance(sp[k], Path) else g[sp[k]] for k in ("front", "back", "palette", "shiny", "icon")}
            except KeyError as e:
                raise AssetError(f"{what} : graphisme {e} introuvable")
            normal, shiny = jasc(paths["palette"]), jasc(paths["shiny"])
            block = self.store.palette(normal, share=False)
            self.store.palette(shiny, share=False)
            shown = []
            for role, key, out in (("front", "front", fronts), ("back", "back", backs)):
                w, h, idx, _ = indexed(paths[key])
                # face : la 2e image de l'animation (anim_front.png, 64×128), en XOR de la 1re sur le
                # même cadre (src/gfx/display.c gfx_image_xor) : ce qui ne bouge pas vaut 0 et se compresse
                two = role == "front" and h >= 2 * FRAME
                both = [a | b for a, b in zip(idx[:FRAME * w], idx[FRAME * w:2 * FRAME * w])] if two else idx
                c = crop_indices(w, h if not two else FRAME, both, 0, 0, FRAME, min(h, FRAME))
                if c is None:
                    raise AssetError(f"{what} : {role} vide")
                _, cw, ch, dx, dy = c
                rows = [idx[(dy + j) * w + dx:(dy + j) * w + dx + cw] for j in range(ch)]
                out.append(c_image(self.store.add_indexed(rows, cw, ch, block, dx, dy, pack=True)))
                shown.append((preview_of(rows, normal), preview_of(rows, shiny), dx, dy))
                if role == "front":
                    second = [idx[(FRAME + dy + j) * w + dx:(FRAME + dy + j) * w + dx + cw] for j in range(ch)] if two else None
                    if second and second != rows:
                        fronts2.append(c_image(self.store.add_frame2(second, rows, cw, ch, block, dx, dy)))
                    else:
                        fronts2.append(c_image(none))
            w, h, idx, _ = indexed(paths["icon"])
            c = crop_indices(w, h, idx, 0, 0, ICON, ICON)
            rows, cw, ch, dx, dy = c
            ip = sp["icon_pal"] if isinstance(sp["icon_pal"], int) and 0 <= sp["icon_pal"] < 6 else 0
            icons.append(c_image(self.store.add_indexed(rows, cw, ch, icon_pals[ip], dx, dy, pack=True)))
            tiles.append((f"{i} {fr}{' ' + kind if kind else ''}", shown,
                          preview_of(rows, jasc(self.gfx / "pokemon/icon_palettes" / f"pal{ip}.pal")), dx, dy))
        n = len(roster)
        self.decls.append(f"#define ROSTER_COUNT {n}\n"
                          "extern const Image pokemon_front[ROSTER_COUNT]; /* indice = indice d'espèce */\n"
                          "extern const Image pokemon_back[ROSTER_COUNT];\n"
                          "/* 2e image de l'animation de face, sur le cadre de pokemon_front, compressée en s'appuyant sur\n"
                          " * elle (gfx_image_frame2) ; w = 0 : pas d'animation */\n"
                          "extern const Image pokemon_front2[ROSTER_COUNT];\n"
                          "extern const Image pokemon_icon[ROSTER_COUNT];")
        for name, arr in (("pokemon_front", fronts), ("pokemon_front2", fronts2), ("pokemon_back", backs), ("pokemon_icon", icons)):
            self.defs.append(f"const Image {name}[ROSTER_COUNT] = {{\n  " + ",\n  ".join(arr) + ",\n};")
        return tiles

    # ----------------------------------------------------------------------- dresseurs
    def trainers(self, m):
        pics, classes, enum = [], [], []
        folder = self.gfx / "trainers/front_pics"

        def add(file, label):
            w, h, idx, pal = indexed(folder / f"{file}.png")
            c = crop_indices(w, h, idx, 0, 0, FRAME, FRAME)
            rows, cw, ch, dx, dy = c
            block = self.store.palette(pal[:16])
            pics.append(c_image(self.store.add_indexed(rows, cw, ch, block, dx, dy, pack=True)))
            im = Image.new("RGBA", (FRAME, FRAME), (0, 0, 0, 0))
            im.alpha_composite(preview_of(rows, pal), (dx, dy))
            self.others.append((label, im))

        for file, name, gender, rank, name_en in m["trainers"]:
            add(file, file)
            classes.append(f"{{{c_str(name)}, {c_str(name_en)}, '{gender}', {rank}}}")
        for enum_name, file in m["characters"]:
            enum.append(f"TRAINER_PIC_{enum_name} = {len(pics)}")
            add(file, enum_name.lower())
        self.emit(f"#define TRAINER_CLASS_COUNT {len(classes)} /* trainer_pic[0..] : dresseurs ordinaires */\n"
                  f"#define TRAINER_PIC_COUNT {len(pics)}\n"
                  f"enum {{\n  " + ",\n  ".join(enum) + "\n};\n"
                  f"extern const Image trainer_pic[TRAINER_PIC_COUNT];\n"
                  f"extern const TrainerClass trainer_classes[TRAINER_CLASS_COUNT];",
                  f"const Image trainer_pic[TRAINER_PIC_COUNT] = {{\n  " + ",\n  ".join(pics) + ",\n};\n\n"
                  f"const TrainerClass trainer_classes[TRAINER_CLASS_COUNT] = {{\n  " + ",\n  ".join(classes) + ",\n};")

    # ----------------------------------------------------------------------- polices
    def fonts(self):
        charmap = self.x.charmap()
        for name, file, array, line in (("normal", "latin_normal", "gFontNormalLatinGlyphWidths", 16),
                                        ("small", "latin_small", "gFontSmallLatinGlyphWidths", 12)):
            w, h, idx, _ = indexed(self.gfx / "fonts" / f"{file}.png")
            widths = self.x.font_widths(array)
            glyphs = {}
            for ch, code in charmap.items():
                if ch == " ":
                    continue
                gx, gy, adv = code % 16 * 16, code // 16 * 16, widths[code]
                glyphs[ord(ch)] = [[idx[(gy + y) * w + gx + x] if idx[(gy + y) * w + gx + x] in (1, 2) else 0
                                    for x in range(adv)] for y in range(16)]
            bits, table = bytearray(), []
            sample = Image.new("RGBA", (sum(len(g[0]) for g in glyphs.values()), 16), (255, 255, 255, 255))
            sx = 0
            for cp in sorted(glyphs):
                g = glyphs[cp]
                table.append(f"{{0x{cp:04x}, {len(bits)}, {len(g[0])}}}")
                flat = [v for r in g for v in r] + [0] * 3
                bits.extend(flat[i] | flat[i + 1] << 2 | flat[i + 2] << 4 | flat[i + 3] << 6 for i in range(0, len(flat) - 3, 4))
                for y, r in enumerate(g):
                    for x, v in enumerate(r):
                        if v:
                            sample.putpixel((sx + x, y), (48, 48, 48, 255) if v == 1 else (176, 176, 168, 255))
                sx += len(g[0])
            if len(bits) >= 1 << 16 or len(table) > 255:
                raise AssetError(f"police {name} trop grande")
            self.others.append((f"police {name}", sample))
            self.emit(f"extern const Font font_{name};",
                      f"static const Glyph font_{name}_glyphs[{len(table)}] = {{\n" + c_array(table, 8) + "\n};\n"
                      f"static const uint8_t font_{name}_bits[{len(bits)}] = {{\n{c_array(bits, 32)}\n}};\n"
                      f"const Font font_{name} = {{font_{name}_glyphs, font_{name}_bits, {len(table)}, 16, "
                      f"{widths[charmap[' ']]}, {line}}};")

    # ----------------------------------------------------------------------- interface
    def image(self, name, rows, pal, x=0, y=0):
        block = self.store.palette(pal[:16])
        desc = self.store.add_indexed(rows, len(rows[0]), len(rows), block, x, y)
        self.emit(f"extern const Image img_{name};", f"const Image img_{name} = {c_image(desc)};")
        self.others.append((name, preview_of(rows, pal)))

    def cells(self, name, path, cw, ch, count, horizontal=False):
        """Une suite d'images de cw x ch (verticale, ou horizontale), recadrées."""
        w, h, idx, pal = indexed(path)
        block, items = self.store.palette(pal[:16]), []
        for i in range(count):
            x0, y0 = (i * cw, 0) if horizontal else (0, i * ch)
            rows, cw2, ch2, dx, dy = crop_indices(w, h, idx, x0, y0, x0 + cw, y0 + ch)
            items.append(c_image(self.store.add_indexed(rows, cw2, ch2, block, dx, dy)))
            self.others.append((f"{name}[{i}]", preview_of(rows, pal)))
        self.emit(f"extern const Image {name}[{count}];",
                  f"const Image {name}[{count}] = {{\n  " + ",\n  ".join(items) + ",\n};")

    def interface(self):
        bi = self.gfx / "battle_interface"
        # cadre de PV (celui de l'adversaire sert aux deux camps : pas de barre « EXP » sans niveaux)
        w, h, idx, pal = indexed(bi / "healthbox_singles_opponent.png")
        rows, cw, ch, dx, dy = crop_indices(w, h, idx, 0, 0, w, h)
        self.image("hpbox_foe", rows, pal)

        # barre de PV : étiquette (« PV » à la place de « HP »), 48 px de barre vide, bord droit
        w, h, idx, pal = indexed(bi / "hpbar.png")
        src = [idx[y * w:(y + 1) * w] for y in range(1, 7)]
        rows = []
        for r in src:
            label = r[8:24]
            rows.append(label + [r[24]] * 48 + [r[9], r[8]])
        letters = {"P": ["####.", "##.##", "####.", "##..."], "V": ["##.##", "##.##", ".###.", "..#.."]}
        shade = [7, 7, 8, 9]
        for j in range(4):
            for i in range(11, 23):
                rows[1 + j][i - 8] = 6
            for k, ch in enumerate("PV"):
                for i, c in enumerate(letters[ch][j]):
                    if c == "#":
                        rows[1 + j][11 - 8 + k * 6 + i] = shade[j]
        self.image("hpbar", rows, pal)
        self.decls.append("#define HPBAR_FILL_X 16 /* la barre de 48 px dans img_hpbar, lignes 2 et 3 */")
        self.decls.append("#define HPBAR_FILL_W 48")

        self.cells("ball_icon", bi / "ball_display.png", 8, 8, 4, horizontal=True)  # en forme, vide, K.O., statut
        self.cells("mega_icon", bi / "mega_indicator.png", 8, 8, 1)
        self.cells("category_icon", self.gfx / "interface/category_icons.png", 16, 16, 3)  # physique, spéciale, statut

        # fenêtre standard d'Émeraude (9-slice, coins de 8)
        w, h, idx, pal = indexed(self.gfx / "text_window/std.png")
        block = self.store.palette(pal[:16])
        rows = [idx[y * w:(y + 1) * w] for y in range(h)]
        fill = Counter(rows[12][8:16]).most_common(1)[0][0]
        part = lambda x0, y0, pw, ph: c_image(self.store.add_indexed([r[x0:x0 + pw] for r in rows[y0:y0 + ph]], pw, ph,
                                                                     block, 0, 0))
        corners = [part(0, 0, 8, 8), part(16, 0, 8, 8), part(0, 16, 8, 8), part(16, 16, 8, 8)]
        edges = [part(12, 0, 1, 8), part(12, 16, 1, 8), part(0, 12, 8, 1), part(16, 12, 8, 1)]
        self.emit("extern const Nine nine_window;",
                  f"const Nine nine_window = {{\n  {{{', '.join(corners)}}},\n  {{{', '.join(edges)}}},\n"
                  f"  {rgb565(pal[fill])}, 8,\n}};")
        self.others.append(("window", preview_of(rows, pal)))

    # ----------------------------------------------------------------------- planches
    def nine(self, e):
        name = e["name"]
        x, y, w, h = e["rect"]
        img = Image.open(self.root / e["file"]).convert("RGBA").crop((x, y, x + w, y + h))
        c, p = e["corner"], img.load()
        inner = [(i, j) for i in range(c, w - c) for j in range(c, h - c)]
        fill = Counter(p[i, j][:3] for i, j in inner).most_common(1)[0][0]
        col = next(i for i in range(c, w - c) if all(p[i, j][:3] == fill for j in range(c, h - c)))
        row = next(j for j in range(c, h - c) if all(p[i, j][:3] == fill for i in range(c, w - c)))
        parts = [(0, 0, c, c), (w - c, 0, c, c), (0, h - c, c, c), (w - c, h - c, c, c),
                 (col, 0, 1, c), (col, h - c, 1, c), (0, row, c, 1), (w - c, row, c, 1)]
        descs = [c_image(self.store.add_rgba(img.crop((a, b, a + pw, b + ph)), None, 0, 0, name)) for a, b, pw, ph in parts]
        self.others.append((name, img))
        self.emit(f"extern const Nine nine_{name};",
                  f"const Nine nine_{name} = {{\n  {{{', '.join(descs[:4])}}},\n  {{{', '.join(descs[4:])}}},\n"
                  f"  {rgb565(fill)}, {c},\n}};")

    def backdrops(self, e):
        name = e["name"]
        img = Image.open(self.root / e["file"]).convert("RGBA")
        (ox, oy), (cw, ch), (sx, sy), cols = e["origin"], e["cell"], e["step"], e["cols"]
        items, enum = [], []
        for i, label in enumerate(e["labels"]):
            cx, cy = ox + (i % cols) * sx, oy + (i // cols) * sy
            bd = img.crop((cx, cy, cx + cw, cy + ch))
            p = bd.load()
            rows = [Counter(p[x, y][:3] for x in range(cw)).most_common(1)[0][0] for y in range(ch)]
            busy = runs([all(p[x, y][:3] == rows[y] for x in range(cw)) for y in range(ch)])
            while len(busy) > 2:  # une ligne unie au milieu d'une plateforme : on recolle au plus petit écart
                k = min(range(len(busy) - 1), key=lambda i: busy[i + 1][0] - busy[i][1])
                busy[k:k + 2] = [(busy[k][0], busy[k + 1][1])]
            if len(busy) != 2:
                raise AssetError(f"{name} {label} : {len(busy)} groupes de lignes non unies au lieu de 2 plateformes")
            plats = []
            for y0, y1 in busy:
                part = bd.crop((0, y0, cw, y1))
                q = part.load()
                for yy in range(y1 - y0):
                    for xx in range(cw):
                        if q[xx, yy][:3] == rows[y0 + yy]:
                            q[xx, yy] = (0, 0, 0, 0)
                bx0, by0, bx1, by1 = part.getbbox()
                desc = self.store.add_rgba(part.crop((bx0, by0, bx1, by1)), None, bx0, y0 + by0, f"{name} {label}")
                plats.append(c_image(desc))
            items.append("  {{" + ",".join(str(rgb565(c)) for c in rows) + "},\n   " + ",\n   ".join(plats) + "}")
            enum.append(f"BACKDROP_{label.upper()}")
            self.others.append((f"{name} {label}", bd))
        n = len(items)
        self.emit(f"#define BACKDROP_COUNT {n}\nenum {{ {', '.join(enum)} }};\nextern const Backdrop {name}[{n}];",
                  f"const Backdrop {name}[{n}] = {{\n" + ",\n".join(items) + ",\n};")

    # ----------------------------------------------------------------------- aperçu
    def preview(self, path, tiles):
        per_row, tw, th = 8, 2 * FRAME + ICON + 16, 2 * FRAME + 14
        rows = (len(tiles) + per_row - 1) // per_row
        flow, x, y, line_h, width = [], 0, 0, 0, tw * per_row
        for label, im in self.others:
            w, h = max(im.width, 60), im.height + 12
            if x + w > width:
                x, y, line_h = 0, y + line_h + 4, 0
            flow.append((x, y, label, im))
            x, line_h = x + w + 4, max(line_h, h)
        sheet = Image.new("RGBA", (width, rows * th + y + line_h + 8), (160, 160, 168, 255))
        d = ImageDraw.Draw(sheet)
        for i, (label, shown, icon, ix, iy) in enumerate(tiles):
            x0, y0 = (i % per_row) * tw, (i // per_row) * th
            for k, (normal, shiny, dx, dy) in enumerate(shown):
                for r, (im, bg) in enumerate(((normal, (248, 248, 216, 255)), (shiny, (232, 216, 248, 255)))):
                    fx, fy = x0 + 2 + k * (FRAME + 2), y0 + 2 + r * (FRAME + 2)
                    d.rectangle((fx, fy, fx + FRAME - 1, fy + FRAME - 1), fill=bg)
                    sheet.alpha_composite(im, (fx + dx, fy + dy))
            sheet.alpha_composite(icon, (x0 + 2 * FRAME + 8 + ix, y0 + 2 + iy))
            d.text((x0 + 2, y0 + 2 * FRAME + 2), ascii_label(label), fill=(0, 0, 0, 255))
        base = rows * th + 8
        for x, y, label, im in flow:
            sheet.alpha_composite(im, (x, base + y))
            d.text((x, base + y + im.height), ascii_label(label), fill=(0, 0, 0, 255))
        sheet.save(path)


def main(argv):
    if len(argv) != 3:
        print("usage : convert_assets.py <dossier assets> <dossier de sortie>", file=sys.stderr)
        return 2
    root, out = Path(argv[1]), Path(argv[2])
    out.mkdir(parents=True, exist_ok=True)
    m = json.loads((root / "manifest.json").read_text(encoding="utf-8"))
    cv = Converter(root, out)
    tiles = cv.pokemon(root.parent / "data")
    cv.trainers(m)
    cv.fonts()
    cv.interface()
    for e in m["nines"]:
        cv.nine(e)
    for e in m["backdrops"]:
        cv.backdrops(e)

    head = "/* Généré par tools/convert_assets.py : ne pas modifier. */\n"
    (out / "assets.h").write_text(head + "#ifndef ASSETS_H\n#define ASSETS_H\n#include \"gfx/image.h\"\n\n" +
                                  "\n".join(cv.decls) + "\n\n#endif\n", encoding="utf-8")
    st = cv.store
    (out / "assets.c").write_text(
        head + "#include \"assets.h\"\n\n"
        f"const uint16_t image_palettes[{len(st.palettes)}] = {{\n{c_array(st.palettes, 16)}\n}};\n\n"
        f"const uint8_t image_pixels[{len(st.pixels)}] = {{\n{c_array(st.pixels, 32)}\n}};\n\n"
        + "\n\n".join(cv.defs) + "\n", encoding="utf-8")
    cv.preview(out / "preview.png", tiles)
    for w in warnings:
        print("attention :", w)
    print(f"{st.count} images, {len(st.pixels)} octets de pixels, "
          f"{len(st.palettes) // 16} palettes ({len(st.palettes) * 2} octets) -> {out}")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main(sys.argv))
    except AssetError as e:
        print("erreur :", e, file=sys.stderr)
        sys.exit(1)
