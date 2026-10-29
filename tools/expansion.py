"""Lecture des données de pokeemerald-expansion (espèces, formes Méga, attaques, attaques apprises,
chemins des graphismes), avec la configuration de l'expansion : les valeurs qui dépendent de la
génération (« B_UPDATED_MOVE_DATA >= GEN_6 ? 90 : 95 ») sont évaluées comme le jeu le ferait,
c'est-à-dire en génération 9 par défaut.

Utilisé par tools/gen_data.py et tools/convert_assets.py.
"""
import json
import re
from pathlib import Path

DEFAULT_ROOT = Path(__file__).resolve().parent.parent / "pokeemerald-expansion-master" / "pokeemerald-expansion-master"

# ------------------------------------------------------------------------- évaluation d'expressions C

_TOKEN = re.compile(r"\s*(0x[0-9A-Fa-f]+|\d+|[A-Za-z_]\w*|>=|<=|==|!=|&&|\|\||[-+*/%()?:<>!,])")


class Evaluator:
    """Évalue une expression C entière ; un identifiant inconnu vaut son propre nom (TYPE_FIRE…)."""

    def __init__(self, defines):
        self.defines = defines

    def __call__(self, expr):
        self.toks = [t for t in _TOKEN.findall(expr) if t]
        self.i = 0
        v = self.ternary()
        return v

    def peek(self):
        return self.toks[self.i] if self.i < len(self.toks) else None

    def take(self, tok=None):
        t = self.peek()
        if tok and t != tok:
            raise ValueError(f"attendu {tok}, trouvé {t}")
        self.i += 1
        return t

    def ternary(self):
        cond = self.logic_or()
        if self.peek() == "?":
            self.take("?")
            a = self.ternary()
            self.take(":")
            b = self.ternary()
            return a if cond else b
        return cond

    def logic_or(self):
        v = self.logic_and()
        while self.peek() == "||":
            self.take()
            w = self.logic_and()
            v = 1 if (v or w) else 0
        return v

    def logic_and(self):
        v = self.compare()
        while self.peek() == "&&":
            self.take()
            w = self.compare()
            v = 1 if (v and w) else 0
        return v

    def compare(self):
        v = self.add()
        while self.peek() in (">=", "<=", "==", "!=", "<", ">"):
            op, w = self.take(), self.add()
            v = int({">=": v >= w, "<=": v <= w, "==": v == w, "!=": v != w, "<": v < w, ">": v > w}[op])
        return v

    def add(self):
        v = self.mul()
        while self.peek() in ("+", "-"):
            op, w = self.take(), self.mul()
            v = v + w if op == "+" else v - w
        return v

    def mul(self):
        v = self.unary()
        while self.peek() in ("*", "/", "%"):
            op, w = self.take(), self.unary()
            v = v * w if op == "*" else v // w if op == "/" else v % w
        return v

    def unary(self):
        t = self.peek()
        if t == "!":
            self.take()
            return 0 if self.unary() else 1
        if t == "-":
            self.take()
            return -self.unary()
        return self.primary()

    def primary(self):
        t = self.take()
        if t == "(":
            v = self.ternary()
            self.take(")")
            return v
        if t[0].isdigit():
            return int(t, 0)
        if self.peek() == "(":  # appel de macro : on ne garde que le premier argument
            self.take("(")
            depth, args, cur = 1, [], []
            while depth:
                x = self.take()
                if x == "(":
                    depth += 1
                elif x == ")":
                    depth -= 1
                    if not depth:
                        break
                if x == "," and depth == 1:
                    args.append(cur)
                    cur = []
                else:
                    cur.append(x)
            args.append(cur)
            return t
        return self.ident(t)

    def ident(self, name):
        if name in ("TRUE",):
            return 1
        if name in ("FALSE", "NULL"):
            return 0
        m = re.fullmatch(r"GEN_(\d)", name)
        if m:
            return int(m.group(1))
        if name in self.defines:
            d = self.defines[name]
            if isinstance(d, str):
                return Evaluator(self.defines)(d) if d.strip() else 1
            return d
        return name


def load_defines(root):
    """Les #define de la configuration de l'expansion (include/config/*.h et constants/global.h)."""
    defines = {"GEN_LATEST": 9}
    files = list((root / "include" / "config").glob("*.h")) + [root / "include" / "constants" / "global.h"]
    for f in files:
        for m in re.finditer(r"^\s*#define\s+(\w+)\s+([^\n]*?)\s*(//.*)?$", f.read_text(encoding="utf-8"), re.M):
            if m.group(1) not in defines:
                defines[m.group(1)] = m.group(2)
    return defines


def split_args(s):
    """Découpe « a, f(b, c), d » au niveau supérieur."""
    out, depth, cur = [], 0, ""
    for ch in s:
        if ch == "(":
            depth += 1
        elif ch == ")":
            depth -= 1
        if ch == "," and depth == 0:
            out.append(cur.strip())
            cur = ""
        else:
            cur += ch
    if cur.strip():
        out.append(cur.strip())
    return out


def field(block, name):
    """Le texte d'un champ « .name = valeur, » (valeur éventuellement sur plusieurs lignes)."""
    m = re.search(r"\n\s*\." + name + r"\s*=\s*", block)
    if not m:
        return None
    i, depth, start = m.end(), 0, m.end()
    while i < len(block):
        ch = block[i]
        if ch in "({":
            depth += 1
        elif ch in ")}":
            if depth == 0:
                break
            depth -= 1
        elif ch == "," and depth == 0:
            break
        i += 1
    return block[start:i].strip()


# Espèces propres au jeu, absentes de l'expansion : leurs graphismes sont dans assets/ (dessinés par
# tools/claude_code_art.py), sous forme de chemins plutôt que de symboles de l'expansion.
CUSTOM_ART = Path(__file__).resolve().parent.parent / "assets" / "claude_code"
CUSTOM_SPECIES = {
    "SPECIES_CLAUDE_CODE": {
        "types": ["TYPE_ELECTRIC", "TYPE_PSYCHIC"], "abilities": ["ABILITY_DOWNLOAD", "ABILITY_ADAPTABILITY", None],
        "stats": [100, 120, 100, 155, 110, 135], "legendary": False, "mega": False, "dex": None, "name_en": "Claude Code",
        "front": CUSTOM_ART / "anim_front.png", "back": CUSTOM_ART / "back.png", "palette": CUSTOM_ART / "normal.pal",
        "shiny": CUSTOM_ART / "shiny.pal", "icon": CUSTOM_ART / "icon.png", "icon_pal": 5, "forms": None, "weight": 0,
        "evolves": False,
    },
}
CUSTOM_LEARNSETS = {
    "CLAUDE_CODE": ["MOVE_THUNDERBOLT", "MOVE_THUNDER", "MOVE_PSYCHIC", "MOVE_PSYSHOCK", "MOVE_FOCUS_BLAST",
                    "MOVE_SHADOW_BALL", "MOVE_ICE_BEAM", "MOVE_ENERGY_BALL", "MOVE_FLASH_CANNON", "MOVE_DAZZLING_GLEAM",
                    "MOVE_AURA_SPHERE", "MOVE_DRAGON_PULSE", "MOVE_VOLT_SWITCH", "MOVE_DISCHARGE", "MOVE_TRI_ATTACK",
                    "MOVE_NASTY_PLOT", "MOVE_CALM_MIND", "MOVE_RECOVER", "MOVE_PROTECT", "MOVE_TRICK_ROOM",
                    "MOVE_EXPANDING_FORCE", "MOVE_HYPER_BEAM", "MOVE_EARTH_POWER", "MOVE_HEAT_WAVE"],
}


def literal(value):
    """Le texte anglais d'un champ nom : _("Pound"), COMPOUND_STRING("Pound"), ITEM_NAME("Leftovers"),
    HANDLE_EXPANDED_MOVE_NAME("ThunderPunch", "Thunder Punch") (le nom long)."""
    found = re.findall(r'"((?:[^"\\]|\\.)*)"', value or "")
    return found[-1] if found else None


def strip_conditionals(text, ev):
    """Garde les branches #if / #else actives selon la configuration."""
    out, stack = [], []
    for line in text.split("\n"):
        s = line.strip()
        if s.startswith("#if"):
            if s.startswith("#ifdef") or s.startswith("#ifndef"):
                name = s.split()[1]
                val = (name in ev.defines) == s.startswith("#ifdef")
            else:
                try:
                    val = bool(ev(s[3:]))
                except Exception:
                    val = True
            parent = all(x[0] for x in stack)
            stack.append([val and parent, val])
            continue
        if s.startswith("#elif"):
            parent = all(x[0] for x in stack[:-1])
            done = stack[-1][1]
            try:
                val = bool(ev(s[5:]))
            except Exception:
                val = False
            stack[-1] = [parent and not done and val, done or val]
            continue
        if s.startswith("#else"):
            parent = all(x[0] for x in stack[:-1])
            stack[-1] = [parent and not stack[-1][1], True]
            continue
        if s.startswith("#endif"):
            stack.pop()
            continue
        if all(x[0] for x in stack):
            out.append(line)
    return "\n".join(out)


class Expansion:
    def __init__(self, root=DEFAULT_ROOT):
        self.root = Path(root)
        self.defines = load_defines(self.root)
        self.ev = Evaluator(self.defines)
        self._species = None
        self._moves = None

    # --------------------------------------------------------------- espèces
    def species(self):
        if self._species is not None:
            return self._species
        text = ""
        for g in range(1, 10):
            text += (self.root / "src/data/pokemon/species_info" / f"gen_{g}_families.h").read_text(encoding="utf-8") + "\n"
        text = strip_conditionals(text, self.ev)
        text = self._expand_species_macros(text)
        for m in re.finditer(r"^\s*#define\s+(\w+)\s+(.*)$", text, re.M):
            self.defines.setdefault(m.group(1), m.group(2))
        out = {}
        for m in re.finditer(r"\[(SPECIES_\w+)\]\s*=\s*\{(.*?)\n    \},", text, re.S):
            name, b = m.group(1), "\n" + m.group(2)
            if name in out:
                continue
            def resolve(s):  # CLEFAIRY_FAMILY_TYPES -> { TYPE_FAIRY, TYPE_FAIRY }
                s = (s or "").strip()
                while s in self.defines:
                    s = self.defines[s].strip()
                return s

            t = resolve(field(b, "types"))
            m_types = re.search(r"MON_TYPES\((.*)\)", t)
            inner = m_types.group(1) if m_types else t.strip("{} ")
            try:
                types = [self.ev(a) for a in split_args(inner)]
            except Exception:
                continue
            ab = [a.strip() for a in resolve(field(b, "abilities")).strip("{} ").split(",")]
            raw = [field(b, k) for k in ("baseHP", "baseAttack", "baseDefense", "baseSpAttack", "baseSpDefense", "baseSpeed")]
            if None in raw:
                continue  # entrée sans stats (espèce fictive)
            stats = [self.ev(v) for v in raw]
            out[name] = {
                "types": types if len(types) == 2 else types * 2,
                "abilities": [a if a != "ABILITY_NONE" else None for a in ab],
                "stats": stats,
                "legendary": bool(re.search(r"\.is(Legendary|Mythical|RestrictedLegendary|UltraBeast) = TRUE", b)),
                "mega": bool(re.search(r"\.isMegaEvolution = TRUE", b)),
                "dex": field(b, "natDexNum"),
                "name_en": literal(field(b, "speciesName")),
                "front": field(b, "frontPic"),
                "back": field(b, "backPic"),
                "palette": field(b, "palette"),
                "shiny": field(b, "shinyPalette"),
                "icon": field(b, "iconSprite"),
                "icon_pal": self.ev(field(b, "iconPalIndex") or "0"),
                "forms": field(b, "formChangeTable"),
                "weight": self.ev(field(b, "weight") or "0"),
                "evolves": bool(re.search(r"\.evolutions\s*=\s*EVOLUTION\(\s*\{", b)),
            }
        out.update(CUSTOM_SPECIES)
        self._species = out
        return out

    @staticmethod
    def _expand_species_macros(text):
        """Les espèces décrites par une macro à paramètres (Arceus, Silvally :
        « [SPECIES_ARCEUS_FIRE] = ARCEUS_SPECIES_INFO(TYPE_FIRE, Fire, 3), ») deviennent des blocs
        ordinaires."""
        macros = {}
        for m in re.finditer(r"#define\s+(\w+)(?:\(([^)]*)\))?[ \t]*\\\n((?:.*\\\n)*.*)", text):
            body = re.sub(r"\\\n", "\n", m.group(3))
            params = [a.strip() for a in m.group(2).split(",")] if m.group(2) is not None else []
            macros[m.group(1)] = (params, body)

        def expand(m):
            name, args = m.group(2), [a.strip() for a in split_args(m.group(3) or "")]
            if name not in macros or not macros[name][1].lstrip().startswith("{"):
                return m.group(0)
            params, body = macros[name]
            for p_, a in zip(params, args):
                body = re.sub(r"\b" + p_ + r"\b", a, body)
            body = re.sub(r"\s*##\s*", "", body).rstrip().rstrip(",")
            body = re.sub(r"\}\s*$", "", body.strip())
            return f"\n    [{m.group(1)}] =\n{body}\n    }},"

        def inner(m):  # macro de champs dans un bloc : « FLORGES_MISC_INFO(Red, 0), »
            name = m.group(2)
            if name not in macros or macros[name][1].lstrip().startswith("{"):
                return m.group(0)
            params, body = macros[name]
            for p_, a in zip(params, [a.strip() for a in split_args(m.group(3))]):
                body = re.sub(r"\b" + p_ + r"\b", a, body)
            return re.sub(r"\s*##\s*", "", body).rstrip().rstrip(",") + ","

        text = re.sub(r"\s*\[(SPECIES_\w+)\]\s*=\s*([A-Z]\w*_INFO)(?:\((.*?)\))?,", expand, text)
        for _ in range(3):
            text = re.sub(r"^([ \t]+)([A-Z][A-Z0-9_]*)\(([^()\n]*)\),?[ \t]*$", inner, text, flags=re.M)
        return text

    def item_forms(self):
        """{espèce de base: [(forme, objet ITEM_…, déclencheur)]} : les formes que prend une espèce qui
        tient un objet (Giratina, Arceus, Silvally…), ou en entrant au combat (Zacian, Zamazenta, les
        Primo-Résurgences)."""
        text = (self.root / "src/data/pokemon/form_change_tables.h").read_text(encoding="utf-8")
        tables = {}
        for m in re.finditer(r"(s\w+FormChangeTable)\[\]\s*=\s*\{(.*?)\n\};", text, re.S):
            tables[m.group(1)] = re.findall(r"\{(FORM_CHANGE_\w+),\s*(SPECIES_\w+)(?:,\s*(ITEM_\w+))?", m.group(2))
        out = {}
        for name, e in self.species().items():
            tbl = tables.get(e["forms"] or "")
            if not tbl:
                continue
            for method, form, item in tbl:
                if form == name or not item or item == "ITEM_NONE":
                    continue
                if method in ("FORM_CHANGE_ITEM_HOLD", "FORM_CHANGE_BATTLE_SWITCH", "FORM_CHANGE_BATTLE_PRIMAL_REVERSION",
                              "FORM_CHANGE_BEGIN_BATTLE", "FORM_CHANGE_ITEM_USE"):
                    lst = out.setdefault(name, [])
                    if all(f != form for f, _, _ in lst):
                        lst.append((form, item, method))
        return out

    def megas(self):
        """{espèce de base: [(espèce Méga, objet), …]}"""
        text = (self.root / "src/data/pokemon/form_change_tables.h").read_text(encoding="utf-8")
        out = {}
        for m in re.finditer(r"FORM_CHANGE_BATTLE_MEGA_EVOLUTION_ITEM,\s+(SPECIES_\w+),\s*(ITEM_\w+)", text):
            base = re.sub(r"_MEGA(_\w)?$", "", m.group(1))
            out.setdefault(base, []).append((m.group(1), m.group(2)))
        return out

    # --------------------------------------------------------------- attaques
    def moves(self):
        if self._moves is not None:
            return self._moves
        text = strip_conditionals((self.root / "src/data/moves_info.h").read_text(encoding="utf-8"), self.ev)
        out = {}
        for m in re.finditer(r"\[(MOVE_\w+)\]\s*=\s*\{(.*?)\n    \},", text, re.S):
            b = "\n" + m.group(2)

            def num(k, default="0"):
                v = field(b, k)
                return self.ev(v) if v else self.ev(default)

            cat = field(b, "category") or ""
            out[m.group(1)] = {
                "power": num("power"), "type": self.ev(field(b, "type") or "TYPE_NORMAL"),
                "accuracy": num("accuracy"), "pp": num("pp"), "priority": num("priority"),
                "category": self.ev(cat) if cat else "DAMAGE_CATEGORY_STATUS",
                "contact": num("makesContact"), "sound": num("soundMove"), "crit": num("criticalHitStage"),
                "punch": num("punchingMove"), "bite": num("bitingMove"), "slice": num("slicingMove"),
                "pulse": num("pulseMove"), "effect": field(b, "effect"), "body": b,
                "name_en": literal(field(b, "name")),
                "target": self.ev(field(b, "target") or "TARGET_SELECTED"),
            }
        self._moves = out
        return out

    def names_en(self, path):
        """{ABILITY_… ou ITEM_…: nom anglais} d'après src/data/abilities.h ou items.h."""
        text = strip_conditionals((self.root / path).read_text(encoding="utf-8"), self.ev)
        out = {}
        for m in re.finditer(r"\[((?:ABILITY|ITEM)_\w+)\]\s*=\s*\{(.*?)\n    \},", text, re.S):
            name = literal(field("\n" + m.group(2), "name"))
            if name:
                out.setdefault(m.group(1), name)
        return out

    def national_dex(self):
        """{NATIONAL_DEX_…: numéro} d'après l'énumération de include/constants/pokedex.h."""
        text = (self.root / "include/constants/pokedex.h").read_text(encoding="utf-8")
        block = re.search(r"enum\s+NationalDexOrder\s*\{(.*?)\}", text, re.S) or re.search(r"enum\s*\{(\s*NATIONAL_DEX_NONE.*?)\}", text, re.S)
        out, n = {}, 0
        for name in re.findall(r"(NATIONAL_DEX_\w+)", re.sub(r"//[^\n]*", "", block.group(1))):
            out.setdefault(name, n)
            n += 1
        return out

    def learnables(self):
        """{NOM: [MOVE_…]} : tout ce que l'espèce peut apprendre (niveau, CT, tuteurs, œufs)."""
        out = json.loads((self.root / "src/data/pokemon/all_learnables.json").read_text(encoding="utf-8"))
        out.update(CUSTOM_LEARNSETS)
        return out

    # --------------------------------------------------------------- le roster du jeu
    def roster(self, data_dir):
        """Les entrées de species_info, dans l'ordre du jeu : les espèces de data/roster.csv, puis
        leurs formes Méga, puis leurs formes à objet (Giratina Originel, Zacian Épée Suprême…).
        [(nom sans SPECIES_, nom français, données, légendaire, indice de l'espèce de base ou None,
        objet ITEM_… qui déclenche la forme ou None, "mega" / "form" / None)]"""
        import csv
        with open(Path(data_dir) / "roster.csv", encoding="utf-8", newline="") as f:
            rows = list(csv.DictReader(f, delimiter=";"))
        sp, megas = self.species(), self.megas()
        out = []
        missing = [r["espece"] for r in rows if "SPECIES_" + r["espece"] not in sp]
        # le jeu n'a que des Pokémon complètement évolués
        young = [r["espece"] for r in rows if "SPECIES_" + r["espece"] in sp and sp["SPECIES_" + r["espece"]]["evolves"]]
        if (missing or young) and not getattr(self, "_warned", False):
            if missing:
                print("attention : espèces absentes de l'expansion, ignorées :", ", ".join(missing))
            if young:
                print("attention : espèces pas complètement évoluées, ignorées :", ", ".join(young))
            self._warned = True
        rows = [r for r in rows if "SPECIES_" + r["espece"] in sp and not sp["SPECIES_" + r["espece"]]["evolves"]]
        for r in rows:
            out.append((r["espece"], r["nom"], sp["SPECIES_" + r["espece"]], r["legendaire"] == "1", None, None, None))
        for i, r in enumerate(rows):
            for mega, stone in megas.get("SPECIES_" + r["espece"], []):
                if mega in sp:
                    out.append((mega[8:], r["nom"], sp[mega], r["legendaire"] == "1", i, stone, "mega"))
        keys = {r["espece"] for r in rows}
        forms = self.item_forms()
        for i, r in enumerate(rows):
            if r["espece"].startswith("DEOXYS"):
                continue  # la Météorite fait tourner les formes : pas une forme « tenue »
            for form, item, _ in forms.get("SPECIES_" + r["espece"], []):
                if form in sp and form[8:] not in keys and not form.endswith("_TERA"):
                    out.append((form[8:], r["nom"], sp[form], r["legendaire"] == "1", i, item, "form"))
        return out

    # --------------------------------------------------------------- polices
    def charmap(self):
        """{caractère Unicode: indice du glyphe} pour les polices latines (charmap.txt)."""
        out = {}
        for line in (self.root / "charmap.txt").read_text(encoding="utf-8").split("\n"):
            if line.startswith("@ Hiragana"):
                break
            m = re.match(r"'(\\?.)'\s*=\s*([0-9A-F]{2})\s*(@.*)?$", line)
            if m:
                ch = m.group(1)[-1]
                out.setdefault(ch, int(m.group(2), 16))
        out.pop("$", None)  # FF : fin de chaîne
        out.pop("~", None)  # espace insécable
        out.update({"\"": out["”"], "↑": 0x79, "↓": 0x7A, "←": 0x7B, "→": 0x7C})
        return out

    def font_widths(self, array):
        """Avances des 512 glyphes d'une police (src/fonts.c)."""
        text = (self.root / "src/fonts.c").read_text(encoding="utf-8")
        m = re.search(array + r"\[\]\s*=\s*\{(.*?)\};", text, re.S)
        return [int(v) for v in re.findall(r"\d+", m.group(1))]

    # --------------------------------------------------------------- graphismes
    def graphics(self):
        """{symbole C: chemin du fichier source} (le premier, sans les variantes GBA)."""
        text = (self.root / "src/data/graphics/pokemon.h").read_text(encoding="utf-8")
        out = {}
        for m in re.finditer(r"(g\w+)\[\]\s*=\s*INC\w+\(\"([^\"]+)\"", text):
            out.setdefault(m.group(1), self.root / m.group(2))
        return out
