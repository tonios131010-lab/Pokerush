#!/usr/bin/env python3
"""data/ + pokeemerald-expansion -> C pour PokeRush (formats : src/game/pokemon.h).

    python tools/gen_data.py data build/gen

data/ dit QUOI prendre et comment l'appeler en français : roster.csv (les 150 espèces), moves.csv
(les attaques et leur effet dans notre moteur), abilities.csv (les talents, et ceux que le moteur
programme), items.csv (les objets tenus). L'expansion donne le reste : stats, types, talents,
formes Méga, valeurs des attaques (9G), attaques apprises dans les 9 générations.

Écrit data.h (identifiants SPECIES_*, MOVE_*, ABILITY_*, ITEM_*, EFFECT_*, ITEM_EFFECT_*), data.c
(les tables const) et data_report.md (tout ce qui a été généré, pour relire les données). Toute
incohérence arrête le script avec un message clair.
"""
import csv
import json
import re
import sys
import unicodedata
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from expansion import Expansion  # noqa: E402

TYPES = ["normal", "fighting", "flying", "poison", "ground", "rock", "bug", "ghost", "steel", "mystery",
         "fire", "water", "grass", "electric", "psychic", "ice", "dragon", "dark", "fairy"]
TYPE_NAMES = ["Normal", "Combat", "Vol", "Poison", "Sol", "Roche", "Insecte", "Spectre", "Acier", "???",
              "Feu", "Eau", "Plante", "Électrik", "Psy", "Glace", "Dragon", "Ténèbres", "Fée"]
TYPE_NAMES_EN = ["Normal", "Fighting", "Flying", "Poison", "Ground", "Rock", "Bug", "Ghost", "Steel", "???",
                 "Fire", "Water", "Grass", "Electric", "Psychic", "Ice", "Dragon", "Dark", "Fairy"]
NATURES_EN = ["Hardy", "Lonely", "Brave", "Adamant", "Naughty", "Bold", "Docile", "Relaxed", "Impish", "Lax",
              "Timid", "Hasty", "Serious", "Jolly", "Naive", "Modest", "Mild", "Quiet", "Bashful", "Rash",
              "Calm", "Gentle", "Sassy", "Careful", "Quirky"]
NATURES = ["Hardi", "Solo", "Brave", "Rigide", "Mauvais", "Assuré", "Docile", "Relax", "Malin", "Lâche",
           "Timide", "Pressé", "Sérieux", "Jovial", "Naïf", "Modeste", "Doux", "Discret", "Pudique", "Foufou",
           "Calme", "Gentil", "Malpoli", "Prudent", "Bizarre"]

# Effets des attaques. L'ordre fixe les valeurs EFFECT_* que le moteur (src/game/battle.c) traite ;
# une attaque de statut applique son effet à coup sûr, une attaque offensive avec la probabilité de
# la colonne "chance" de moves.csv (vide = toujours).
EFFECTS = [
    "hit",
    # statut de la cible
    "burn", "paralyze", "poison", "badly_poison", "sleep", "freeze", "confuse", "flinch", "tri",
    # stats de la cible : -1 ou -2 niveaux
    "t_atk_down", "t_def_down", "t_spa_down", "t_spd_down", "t_spe_down", "t_acc_down",
    "t_atk_down2", "t_def_down2", "t_spd_down2", "t_spe_down2", "t_spa_down2",
    # stats du lanceur
    "u_atk_up", "u_def_up", "u_spa_up", "u_spe_up", "u_atk_up2", "u_def_up2", "u_spa_up2", "u_spd_up2",
    "u_spe_up2", "u_atk_spe_up", "u_spa_spd_up", "u_atk_def_up", "u_def_spd_up", "u_atk_spa_up",
    "u_spa_spd_spe_up", "u_all_up", "shell_smash",
    "u_spa_down", "u_spa_down2", "u_spe_down", "u_def_down", "u_atk_def_down", "u_def_spd_down", "belly_drum", "curse",
    # dégâts particuliers
    "recoil4", "recoil3", "recoil2", "crash", "drain", "multi_hit", "double_hit", "explode", "recharge",
    "solar", "charge", "fixed_level", "fixed_40", "fake_out", "facade", "eruption", "knock_off", "brick_break",
    "rapid_spin", "rampage", "pivot", "drag_out", "psyshock", "foul_play", "body_press", "sucker_punch", "hex",
    "acrobatics", "freeze_dry", "always_crit", "collision", "super_fang", "judgment",
    # attaques de statut
    "heal_half", "heal_weather", "rest", "protect", "substitute", "leech_seed", "reflect", "light_screen",
    "spikes", "toxic_spikes", "stealth_rock", "rain", "sun", "sand", "hail", "baton_pass", "roar", "haze",
    "heal_bell", "taunt", "trick_room", "tailwind", "electric_terrain", "grassy_terrain", "misty_terrain",
    "psychic_terrain",
]
ITEM_EFFECTS = ["none", "leftovers", "choice_atk", "choice_spa", "choice_spe", "life_orb", "focus_sash",
                "focus_band", "shell_bell", "quick_claw", "kings_rock", "scope_lens", "bright_powder",
                "lum_berry", "sitrus_berry", "white_herb", "expert_belt", "muscle_band", "wise_glasses",
                "type_boost", "assault_vest", "rocky_helmet", "black_sludge", "weakness_policy",
                "heavy_boots", "air_balloon", "clear_amulet", "covert_cloak", "loaded_dice",
                "light_clay", "big_root", "wide_lens", "toxic_orb", "flame_orb", "punching_glove", "resonance_prism",
                "terrain_extender", "mega_stone", "form_item", "weather_rock", "terrain_seed", "throat_spray",
                "eject_button", "red_card", "safety_goggles", "utility_umbrella", "protective_pads", "zoom_lens",
                "shed_shell", "metronome", "mental_herb", "power_herb", "blunder_policy", "jaboca_berry", "kee_berry",
                "pinch_berry", "resist_berry"]

# Objets qui changent la forme de leur porteur (expansion : form_change_tables.h)
FORM_ITEMS = {
    "BLUE_ORB": "Orbe Bleu", "RED_ORB": "Orbe Rouge", "ADAMANT_CRYSTAL": "Globe Adamant",
    "LUSTROUS_GLOBE": "Globe Perlé", "GRISEOUS_ORB": "Globe Platiné", "RUSTED_SWORD": "Épée Rouillée",
    "RUSTED_SHIELD": "Boucl. Rouillé", "GRACIDEA": "Gracidée", "REVEAL_GLASS": "Miroir Sacré",
    "PRISON_BOTTLE": "Vase Scellé", "RED_NECTAR": "Nectar Rouge", "YELLOW_NECTAR": "Nectar Jaune",
    "PINK_NECTAR": "Nectar Rose", "PURPLE_NECTAR": "Nectar Violet",
    "FIST_PLATE": "Plaque Poing", "SKY_PLATE": "Plaque Ciel", "TOXIC_PLATE": "Plaque Toxicu",
    "STONE_PLATE": "Plaque Roc", "EARTH_PLATE": "Plaque Terre", "INSECT_PLATE": "Plaque Insecto",
    "SPOOKY_PLATE": "Plaque Fantô", "IRON_PLATE": "Plaque Fer", "FLAME_PLATE": "Plaque Flamme",
    "SPLASH_PLATE": "Plaque Hydro", "MEADOW_PLATE": "Plaque Herbe", "ZAP_PLATE": "Plaque Volt",
    "MIND_PLATE": "Plaque Esprit", "ICICLE_PLATE": "Plaque Glace", "DRACO_PLATE": "Plaque Draco",
    "DREAD_PLATE": "Plaque Ombre", "PIXIE_PLATE": "Plaque Pixie",
    "DOUSE_DRIVE": "Module Aqua", "SHOCK_DRIVE": "Module Choc", "BURN_DRIVE": "Module Pyro",
    "CHILL_DRIVE": "Module Cryo", "WELLSPRING_MASK": "Masque du Puits", "HEARTHFLAME_MASK": "Masque Fourneau",
    "CORNERSTONE_MASK": "Masque Pierre",
}
FORM_DESCS = {"REVEAL_GLASS": "Génies : Forme Totémique", "BLUE_ORB": "Primo-Résurgence de Kyogre",
              "RED_ORB": "Primo-Résurgence de Groudon"}
FORM_DESCS_EN = {"REVEAL_GLASS": "Forces of Nature: Therian Forme", "BLUE_ORB": "Kyogre's Primal Reversion",
                 "RED_ORB": "Groudon's Primal Reversion"}
MEMORY_TYPES = {"FIGHTING": "Combat", "FLYING": "Vol", "POISON": "Poison", "GROUND": "Sol", "ROCK": "Roche",
                "BUG": "Insecte", "GHOST": "Spectre", "STEEL": "Acier", "FIRE": "Feu", "WATER": "Eau",
                "GRASS": "Plante", "ELECTRIC": "Électrik", "PSYCHIC": "Psy", "ICE": "Glace", "DRAGON": "Dragon",
                "DARK": "Ténèbres", "FAIRY": "Fée"}
DRIVE_TYPES = {"DOUSE_DRIVE": "water", "SHOCK_DRIVE": "electric", "BURN_DRIVE": "fire", "CHILL_DRIVE": "ice"}

# Table des types de la 6G et suivantes, attaque -> défense ; le reste est neutre.
SUPER = {
    "fighting": "normal rock steel ice dark", "flying": "fighting bug grass", "poison": "grass fairy",
    "ground": "poison rock steel fire electric", "rock": "flying bug fire ice", "bug": "grass psychic dark",
    "ghost": "ghost psychic", "steel": "rock ice fairy", "fire": "bug steel grass ice", "water": "ground rock fire",
    "grass": "ground rock water", "electric": "flying water", "psychic": "fighting poison",
    "ice": "flying ground grass dragon", "dragon": "dragon", "dark": "ghost psychic",
    "fairy": "fighting dragon dark"}
HALF = {
    "normal": "rock steel", "fighting": "flying poison bug psychic fairy", "flying": "rock steel electric",
    "poison": "poison ground rock ghost", "ground": "bug grass", "rock": "fighting ground steel",
    "bug": "fighting flying poison ghost steel fire fairy", "ghost": "dark", "steel": "steel fire water electric",
    "fire": "rock fire water dragon", "water": "water grass dragon",
    "grass": "flying poison bug steel fire grass dragon", "electric": "grass electric dragon",
    "psychic": "steel psychic", "ice": "steel fire water ice", "dragon": "steel", "dark": "fighting dark fairy",
    "fairy": "poison steel fire"}
IMMUNE = {"normal": "ghost", "fighting": "ghost", "poison": "steel", "ground": "flying", "ghost": "normal",
          "electric": "ground", "psychic": "dark", "dragon": "fairy"}

NAME_BYTES, LABEL_BYTES = 16, 20  # pokemon.h
MAX_NAME, MAX_LABEL = 12, 15
MAX_LABEL_EN = 16  # les noms anglais de l'expansion (« Parabolic Charge »)
NUM_BASE = 150


def die(msg):
    sys.exit("erreur : " + msg)


def read(path):
    with open(path, encoding="utf-8", newline="") as f:
        return list(csv.DictReader(f, delimiter=";"))


def ident(s):
    s = unicodedata.normalize("NFKD", s).encode("ascii", "ignore").decode()
    return re.sub(r"[^A-Za-z0-9]+", "_", s).strip("_").upper()


def c_str(s):
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'


def c_array(values, per_line):
    vals = [str(v) for v in values]
    return "\n".join("  " + ", ".join(vals[i:i + per_line]) + "," for i in range(0, len(vals), per_line))


def exp_type(t):
    """TYPE_FIRE -> indice dans TYPES."""
    name = str(t).replace("TYPE_", "").lower()
    if name not in TYPES:
        die(f"type inconnu de l'expansion : {t}")
    return TYPES.index(name)


def stone_name(base_fr, mega_species):
    """Dracaufeu + SPECIES_CHARIZARD_MEGA_X -> « Dracaufite X » (le nom français perd ses voyelles finales)."""
    root = re.sub(r"[aeiouyéèêëàâ]+$", "", base_fr, flags=re.I)
    suffix = re.search(r"_MEGA_([A-Z])$", mega_species)
    return root + "ite" + (" " + suffix.group(1) if suffix else "")


class Gen:
    def __init__(self, data, font_chars):
        self.data, self.font = data, font_chars

    def label(self, text, limit, nbytes, what):
        up = text.upper()
        if len(text) > limit:
            die(f"{what} : « {text} » dépasse {limit} caractères")
        if len(up.encode()) >= nbytes:
            die(f"{what} : « {text} » dépasse {nbytes - 1} octets en UTF-8")
        missing = sorted({c for c in up if c != " " and c not in self.font})
        if missing:
            die(f"{what} : « {text} » contient {''.join(missing)}, absent de la police")
        return up

    def run(self, out):
        d = self.data
        x = Expansion()
        exp_moves = x.moves()
        learnables, dex_numbers = x.learnables(), x.national_dex()

        # --- talents
        abilities = read(d / "abilities.csv")
        ab_index = {"ABILITY_" + a["id"]: i + 1 for i, a in enumerate(abilities)}
        ab_active = {"ABILITY_" + a["id"] for a in abilities if a["actif"] == "1"}
        ab_names = ["-"] + [self.label(a["nom"], MAX_LABEL, LABEL_BYTES, "talent " + a["id"]) for a in abilities]
        en = x.names_en("src/data/abilities.h")
        ab_names_en = ["-"] + [self.label(en.get("ABILITY_" + a["id"]) or die(f"talent {a['id']} : pas de nom anglais"),
                                          MAX_LABEL_EN, LABEL_BYTES, "talent " + a["id"]) for a in abilities]
        ab_descs, ab_descs_en = [""], [""]
        for a in abilities:
            for text, out_ in ((a["desc"], ab_descs), (a["desc_en"], ab_descs_en)):
                missing = sorted({ch for ch in text if ch != " " and ch not in self.font})
                if missing or len(text) > 40 or not text:
                    die(f"talent {a['id']} : description vide, trop longue ou caractères absents de la police ({''.join(missing)})")
                out_.append(text)

        self.unknown_abilities = {}

        def pick_abilities(sp_name, slots):
            for a in slots:
                if a and a not in ab_index:
                    self.unknown_abilities.setdefault(a, []).append(sp_name)
            slots = [a if a in ab_index else None for a in slots]
            present = [a for a in slots if a]
            if not present:
                return 0, 0
            active = list(dict.fromkeys(a for a in present if a in ab_active))
            if not active:
                return ab_index[present[0]], 0
            first = slots[0] if slots[0] in ab_active else active[0]
            hidden = slots[2] if len(slots) > 2 and slots[2] in ab_active and slots[2] != first else None
            second = hidden or next((a for a in active if a != first), None)
            return ab_index[first], ab_index[second] if second else 0

        # --- attaques
        moves = read(d / "moves.csv")
        missing = [m["id"] for m in moves if "MOVE_" + m["id"] not in exp_moves]
        if missing:
            print("attention : attaques absentes de l'expansion, ignorées :", ", ".join(missing))
        moves = [m for m in moves if "MOVE_" + m["id"] in exp_moves]
        mv_index = {"MOVE_" + m["id"]: i + 1 for i, m in enumerate(moves)}
        if len(mv_index) != len(moves):
            die("moves.csv : identifiant en double")
        mv_names, mv_names_en, mv_info, mv_rows = ["-"], ["-"], ["{0}"], []
        for m in moves:
            what = "attaque " + m["id"]
            e = exp_moves.get("MOVE_" + m["id"])
            if not e:
                die(f"{what} : absente de l'expansion")
            if m["effet"] not in EFFECTS:
                die(f"{what} : effet inconnu {m['effet']} (voir EFFECTS dans tools/gen_data.py)")
            chance = int(m["chance"] or 0)
            cat = str(e["category"])
            status = cat.endswith("STATUS")
            power = 0 if status else int(m["puissance"]) if m.get("puissance") else max(1, int(e["power"]))
            if status and chance:
                die(f"{what} : une attaque de statut n'a pas de probabilité d'effet")
            prio = int(e["priority"])
            acc = int(e["accuracy"]) if int(e["accuracy"]) <= 100 else 0
            row = dict(power=power, type=exp_type(e["type"]), acc=acc, pp=min(63, int(e["pp"])), prio=prio,
                       special=int(cat.endswith("SPECIAL")), contact=int(bool(e["contact"])), sound=int(bool(e["sound"])),
                       crit=int(bool(e["crit"])), punch=int(bool(e["punch"])), bite=int(bool(e["bite"])),
                       slice=int(bool(e["slice"])), pulse=int(bool(e["pulse"])), effect=EFFECTS.index(m["effet"]),
                       chance=chance, spread={"TARGET_BOTH": 1, "TARGET_FOES_AND_ALLY": 2}.get(str(e["target"]), 0))
            mv_rows.append(row)
            mv_names.append(self.label(m["nom"], MAX_LABEL, LABEL_BYTES, what))
            mv_names_en.append(self.label(e["name_en"] or die(f"{what} : pas de nom anglais"), MAX_LABEL_EN, LABEL_BYTES, what))
            mv_info.append(
                "{.power = %(power)d, .accuracy = %(acc)d, .type = %(type)d, .pp = %(pp)d, .priority = %(p8)d, "
                ".contact = %(contact)d, .special = %(special)d, .effect = %(effect)d, .effect_chance = %(chance)d, "
                ".sound = %(sound)d, .high_crit = %(crit)d, .punch = %(punch)d, .bite = %(bite)d, .slice = %(slice)d, "
                ".pulse = %(pulse)d, .spread = %(spread)d}" % dict(row, p8=prio + 8))

        # --- objets (les méga-gemmes s'ajoutent plus bas)
        items = read(d / "items.csv")
        it_names, it_info, it_report, it_descs = ["-"], ["{0}"], [], [""]
        it_names_en, it_descs_en = ["-"], [""]
        it_ids = []
        item_en = x.names_en("src/data/items.h")

        def item_desc(text, what):
            missing = sorted({ch for ch in text.upper() if ch != " " and ch not in self.font})
            if missing or len(text) > 36:
                die(f"{what} : description trop longue ou caractères absents de la police ({''.join(missing)})")
            return text

        def name_en(key, what, given=""):
            """Le nom anglais d'un objet : celui de items.csv, sinon celui de l'expansion (ITEM_…)."""
            return self.label(given or item_en.get(key) or die(f"{what} : pas de nom anglais ({key})"), MAX_LABEL_EN,
                              LABEL_BYTES, what)

        def species_en(i):
            return entries[i][2]["name_en"]

        # les objets avant @mega_stones, puis les méga-gemmes (plus bas), puis ceux d'après : les
        # indices déjà sauvegardés ne bougent pas quand on ajoute des objets
        cut = next((k for k, it in enumerate(items) if it["id"] == "@mega_stones"), len(items))
        items, late_items = items[:cut], items[cut + 1:]

        def add_item(it):
            what = "objet " + it["id"]
            if it["effet"] not in ITEM_EFFECTS:
                die(f"{what} : effet inconnu {it['effet']}")
            # colonne type : le type renforcé, ou un nombre (stat d'une baie, 0 physique / 1 spécial)
            if it["type"] and not it["type"].isdigit() and it["type"] not in TYPES:
                die(f"{what} : type inconnu {it['type']}")
            ty = int(it["type"]) if it["type"].isdigit() else TYPES.index(it["type"]) if it["type"] else 0
            it_names.append(self.label(it["nom"], MAX_LABEL, LABEL_BYTES, what))
            it_names_en.append(name_en("ITEM_" + it["id"].upper(), what, it["nom_en"]))
            it_info.append(f"{{.effect = {ITEM_EFFECTS.index(it['effet'])}, .type = {ty}, .rarity = {int(it['rarete'])}}}")
            it_ids.append(it["id"])
            desc, desc_en = it["desc"], it["desc_en"]
            if it["effet"] == "type_boost":
                desc, desc_en = f"Attaques {TYPE_NAMES[ty]} +20 %", f"{TYPE_NAMES_EN[ty]} moves +20 %"
            elif it["effet"] == "resist_berry":
                desc = f"{TYPE_NAMES[ty]} super efficace : dégâts /2" if ty else "Coup Normal reçu : dégâts /2"
                desc_en = f"Super effective {TYPE_NAMES_EN[ty]}: damage /2" if ty else "Normal hit taken: damage /2"
            if not desc_en:
                die(f"{what} : description anglaise manquante (desc_en)")
            it_descs.append(item_desc(desc, what))
            it_descs_en.append(item_desc(desc_en, what))
            it_report.append((it_names[-1], it["effet"], it["type"], it["rarete"]))

        for it in items:
            add_item(it)

        # --- espèces : les 150, puis leurs formes Méga
        try:
            entries = x.roster(d)  # (nom C, nom affiché, données expansion, légendaire, base, gemme)
        except KeyError as e:
            die(str(e))
        global NUM_BASE
        NUM_BASE = sum(1 for e in entries if e[4] is None)
        for k, (cname, fr, sp, legend, base, stone, kind) in enumerate(entries):
            if kind == "mega":
                name = stone_name(fr, "SPECIES_" + cname)
                it_names.append(self.label(name, MAX_LABEL, LABEL_BYTES, "méga-gemme " + stone))
                it_names_en.append(name_en(stone, "méga-gemme " + stone))
                it_info.append(f"{{.effect = {ITEM_EFFECTS.index('mega_stone')}, .rarity = 3}}")
                it_ids.append(stone[5:].lower())
                it_descs.append(item_desc(f"Méga-Évolution de {fr}", stone))
                it_descs_en.append(item_desc(f"Mega Evolves {species_en(base)}", stone))
                it_report.append((it_names[-1], "mega_stone", "", "3"))
                entries[k] = (cname, fr, sp, legend, base, len(it_names) - 1, kind)
        for it in late_items:
            add_item(it)
        form_item = {}
        for k, (cname, fr, sp, legend, base, stone, kind) in enumerate(entries):
            if kind == "form":
                key = stone[5:]
                if key not in form_item:
                    if key.endswith("_MEMORY"):
                        name = "ROM " + MEMORY_TYPES[key[:-7]]
                    elif key in FORM_ITEMS:
                        name = FORM_ITEMS[key]
                    else:
                        die(f"objet de forme {stone} : nom français inconnu (FORM_ITEMS)")
                    # type renforcé : celui que la forme gagne (Plaques, ROM, Masques), ou celui du Module
                    bt = {exp_type(t) for t in entries[base][2]["types"]}
                    gained = [exp_type(t) for t in sp["types"] if exp_type(t) not in bt]
                    ty = TYPES.index(DRIVE_TYPES[key]) if key in DRIVE_TYPES else gained[0] if gained else TYPES.index("mystery")
                    it_names.append(self.label(name, MAX_LABEL, LABEL_BYTES, "objet de forme " + stone))
                    it_names_en.append(name_en(stone, "objet de forme " + stone))
                    it_info.append(f"{{.effect = {ITEM_EFFECTS.index('form_item')}, .type = {ty}, .rarity = 3}}")
                    it_ids.append(key.lower())
                    typed = key.endswith(("_PLATE", "_MEMORY"))
                    desc = FORM_DESCS.get(key) or (f"{fr} : type {TYPE_NAMES[ty]}" if typed else f"Change la forme de {fr}")
                    en_ = species_en(base)
                    desc_en = FORM_DESCS_EN.get(key) or (f"{en_}: {TYPE_NAMES_EN[ty]} type" if typed else f"Changes {en_}'s form")
                    it_descs.append(item_desc(desc, stone))
                    it_descs_en.append(item_desc(desc_en, stone))
                    it_report.append((it_names[-1], "form_item", TYPES[ty], "3"))
                    form_item[key] = len(it_names) - 1
                entries[k] = (cname, fr, sp, legend, base, form_item[key], kind)
        if len(entries) > 1 << 10:
            die("plus de 1024 espèces et formes (SPECIES_BITS)")
        if len(it_names) > 1 << 8:
            die(f"{len(it_names)} objets : plus que ITEM_BITS ne permet")

        pool, sp_info, sp_names, sp_names_en, sp_enum, report = [], [], [], [], [], []
        learn_of_base = {}
        for i, (cname, fr, sp, legend, base, stone, kind) in enumerate(entries):
            what = f"espèce {cname} {fr}"
            st = [int(v) for v in sp["stats"]]  # PV Atq Déf AtqS DéfS Vit
            if not all(1 <= v <= 255 for v in st):
                die(f"{what} : stat hors de 1..255 : {st}")
            hp, atk, df, spa, spd, spe = st
            emerald = [hp, atk, df, spe, spa, spd]
            t1, t2 = exp_type(sp["types"][0]), exp_type(sp["types"][1])
            a0, a1 = pick_abilities(cname, sp["abilities"])
            if base is None:
                key = cname
                while key not in learnables and "_" in key:  # ZYGARDE_50 -> ZYGARDE
                    key = key.rsplit("_", 1)[0]
                if key not in learnables:
                    die(f"{what} : pas d'attaques apprises dans l'expansion ({key})")
                mine = [m for m in learnables[key] if m in mv_index]
                # attaques offensives puissantes d'abord (le draft les propose dans cet ordre)
                mine.sort(key=lambda m: (-(mv_rows[mv_index[m] - 1]["power"] > 1), -mv_rows[mv_index[m] - 1]["power"], m))
                mine = mine[:127]
                if sum(1 for m in mine if mv_rows[mv_index[m] - 1]["power"]) < 4:
                    die(f"{what} : moins de 4 attaques offensives apprises")
                learn_of_base[i] = (len(pool), len(mine))
                pool += [mv_index[m] for m in mine]
                ofs, cnt = learn_of_base[i]
            else:
                ofs, cnt = learn_of_base[base]
            if len(pool) >= 1 << 17:
                die("learnset_pool trop grand (learn_ofs)")
            bst = sum(st)
            tier = max(0, min(7, (bst - 420) // 35))
            dex = dex_numbers.get(sp["dex"], 0)
            sp_info.append(
                f"{{.base = {{{', '.join(map(str, emerald))}}}, .dex = {dex}, .type1 = {t1}, .type2 = {t2}, "
                f".tier = {tier}, .legendary = {int(legend)}, .is_mega = {int(kind == 'mega')}, .is_form = {int(kind == 'form')}, "
                f".icon_pal = {int(sp['icon_pal']) if isinstance(sp['icon_pal'], int) else 0}, .ability0 = {a0}, "
                f".ability1 = {a1}, .mega_base = {base or 0}, .stone = {stone or 0}, .learn_ofs = {ofs}, .learn_cnt = {cnt}}}")
            sp_names.append(self.label(fr, MAX_NAME, NAME_BYTES, what))
            sp_names_en.append(self.label(sp["name_en"] or die(f"{what} : pas de nom anglais"), MAX_NAME, NAME_BYTES, what))
            sp_enum.append(f"SPECIES_{cname}")
            report.append((i, dex, fr, cname, t1, t2, emerald, bst, tier, legend, base, a0, a1, cnt, ofs))

        # --- attaques apprises : 9 bits chacune, à la suite (pokemon.h, species_learnset)
        if len(mv_names) > 512:
            die("plus de 512 attaques : learnset_bits suppose 9 bits")
        bits = 0
        for k, mv in enumerate(pool):
            bits |= mv << (9 * k)
        packed = list(bits.to_bytes((9 * len(pool) + 7) // 8 + 1, "little"))

        # --- table des types
        chart = []
        for a in TYPES:
            for t in TYPES:
                eff = 2
                if t in SUPER.get(a, "").split():
                    eff = 3
                if t in HALF.get(a, "").split():
                    eff = 1
                if t in IMMUNE.get(a, "").split():
                    eff = 0
                chart.append(eff)
        chart += [0] * (-len(chart) % 4)
        chart_bytes = [chart[i] | chart[i + 1] << 2 | chart[i + 2] << 4 | chart[i + 3] << 6 for i in range(0, len(chart), 4)]
        nat_names = [self.label(n, MAX_LABEL, LABEL_BYTES, "nature") for n in NATURES]
        ty_names = [self.label(n, MAX_LABEL, LABEL_BYTES, "type") for n in TYPE_NAMES]
        nat_names_en = [self.label(n, MAX_LABEL, LABEL_BYTES, "nature") for n in NATURES_EN]
        ty_names_en = [self.label(n, MAX_LABEL, LABEL_BYTES, "type") for n in TYPE_NAMES_EN]

        def enum(prefix, names, first="NONE"):
            items_ = ([f"{prefix}_{first}"] if first else []) + [f"{prefix}_{ident(n)}" for n in names]
            return "enum {\n" + c_array(items_, 5) + f"\n  NUM_{prefix}S\n}};\n"

        head = "/* Généré par tools/gen_data.py depuis data/ et pokeemerald-expansion : ne pas modifier. */\n"
        h = [head, "#ifndef DATA_H\n#define DATA_H\n#include \"game/pokemon.h\"\n\n",
             f"#define NUM_BASE_SPECIES {NUM_BASE} /* les espèces ; les formes Méga suivent */\n",
             f"#define NUM_SPECIES {len(entries)}\n",
             f"#define LEARNSET_SIZE {len(pool)} /* entrées de learnset_pool */\n",
             "enum {\n" + c_array(sp_enum, 4) + "\n};\n\n",
             enum("MOVE", [m["id"] for m in moves]), "\n",
             enum("ABILITY", [a["id"] for a in abilities]), "\n",
             enum("ITEM", it_ids), "\n",
             enum("EFFECT", EFFECTS, None), "\n",
             "enum {\n" + c_array([f"ITEM_EFFECT_{ident(e)}" for e in ITEM_EFFECTS], 4) + "\n};\n\n",
             "_Static_assert(NUM_SPECIES <= 1 << SPECIES_BITS, \"SPECIES_BITS\");\n",
             "_Static_assert(NUM_MOVES <= 1 << MOVE_BITS, \"MOVE_BITS\");\n",
             "_Static_assert(NUM_ITEMS <= 1 << ITEM_BITS, \"ITEM_BITS\");\n",
             "_Static_assert(NUM_ABILITYS <= 1 << ABILITY_BITS, \"ABILITY_BITS\");\n",
             "_Static_assert(NUM_EFFECTS <= 128, \"MoveInfo.effect\");\n",
             f"_Static_assert(ITEM_EFFECT_{ident(ITEM_EFFECTS[-1])} < 64, \"ItemInfo.effect\");\n",
             "\n#endif\n"]
        (out / "data.h").write_text("".join(h), encoding="utf-8")

        def strings(name, size, values):
            return f"const char {name}[][{size}] = {{\n" + c_array([c_str(v) for v in values], 5) + "\n};\n\n"

        def texts(name, values):
            return f"const char *const {name}[] = {{\n" + c_array([c_str(v) for v in values], 3) + "\n};\n\n"

        c = [head, "#include \"data.h\"\n\n",
             "uint8_t lang; /* langue des textes (pokemon.h) */\n\n",
             f"const uint8_t type_chart[{len(chart_bytes)}] = {{\n{c_array(chart_bytes, 16)}\n}};\n\n",
             f"const SpeciesInfo species_info[{len(entries)}] = {{\n  " + ",\n  ".join(sp_info) + ",\n};\n\n",
             strings("species_names_fr", "NAME_BYTES", sp_names),
             strings("species_names_en", "NAME_BYTES", sp_names_en),
             f"const uint8_t learnset_bits[{len(packed)}] = {{\n{c_array(packed, 24)}\n}};\n\n",
             f"const MoveInfo move_info[{len(mv_info)}] = {{\n  " + ",\n  ".join(mv_info) + ",\n};\n\n",
             strings("move_names_fr", "LABEL_BYTES", mv_names),
             strings("move_names_en", "LABEL_BYTES", mv_names_en),
             strings("ability_names_fr", "LABEL_BYTES", ab_names),
             strings("ability_names_en", "LABEL_BYTES", ab_names_en),
             texts("ability_descs_fr", ab_descs),
             texts("ability_descs_en", ab_descs_en),
             f"const ItemInfo item_info[{len(it_info)}] = {{\n  " + ",\n  ".join(it_info) + ",\n};\n\n",
             strings("item_names_fr", "LABEL_BYTES", it_names),
             strings("item_names_en", "LABEL_BYTES", it_names_en),
             texts("item_descs_fr", it_descs),
             texts("item_descs_en", it_descs_en),
             strings("nature_names_fr", "LABEL_BYTES", nat_names),
             strings("nature_names_en", "LABEL_BYTES", nat_names_en),
             strings("type_names_fr", "LABEL_BYTES", ty_names),
             strings("type_names_en", "LABEL_BYTES", ty_names_en)]
        (out / "data.c").write_text("".join(c), encoding="utf-8")

        self.write_report(out / "data_report.md", report, moves, mv_rows, mv_names, it_report, ab_names, pool)
        if self.unknown_abilities:
            top = sorted(self.unknown_abilities.items(), key=lambda kv: -len(kv[1]))
            print("talents absents de abilities.csv (ignorés) :", ", ".join(f"{k[8:]} ({len(v)})" for k, v in top))
        megas = sum(1 for e in entries if e[6] == "mega")
        print(f"{NUM_BASE} espèces ({sum(1 for r in report if r[9] and r[10] is None)} légendaires), {megas} formes Méga, "
              f"{len(entries) - NUM_BASE - megas} formes à objet, "
              f"{len(moves)} attaques, {len(pool)} attaques apprises, {len(abilities)} talents, {len(it_names) - 1} objets -> {out}")

    def write_report(self, path, report, moves, mv_rows, mv_names, it_report, ab_names, pool):
        lines = ["# Données générées (tools/gen_data.py)\n",
                 "Stats dans l'ordre PV / Atq / Déf / AtqS / DéfS / Vit. Rang = (total - 420) / 35, de 0 à 7.\n",
                 "| # | n° | Nom | Types | PV | Atq | Déf | AtqS | DéfS | Vit | Total | Rang | Talents | Attaques |",
                 "|---|---|---|---|---|---|---|---|---|---|---|---|---|---|"]
        for i, dex, fr, cname, t1, t2, base, bst, tier, leg, mbase, a0, a1, cnt, ofs in report:
            hp, atk, df, spe, spa, spd = base
            types = TYPE_NAMES[t1] + (" / " + TYPE_NAMES[t2] if t2 != t1 else "")
            ab = ab_names[a0].title() + (" / " + ab_names[a1].title() if a1 else "")
            name = fr + (" ★" if leg else "") + (f" (Méga {cname.split('MEGA')[-1].strip('_')})" if mbase is not None else "")
            mv = "" if mbase is not None else f"{cnt} : " + ", ".join(mv_names[m].title() for m in pool[ofs:ofs + cnt][:14]) + ("…" if cnt > 14 else "")
            lines.append(f"| {i} | {dex} | {name} | {types} | {hp} | {atk} | {df} | {spa} | {spd} | {spe} | {bst} | {tier} | {ab} | {mv} |")
        lines += ["", "## Attaques\n", "| Nom | Type | Catégorie | Puiss. | Préc. | PP | Prio | Effet | Chance |",
                  "|---|---|---|---|---|---|---|---|---|"]
        for k, (m, r) in enumerate(zip(moves, mv_rows)):
            cat = "statut" if not r["power"] else "spéciale" if r["special"] else "physique"
            lines.append(f"| {mv_names[k + 1].title()} | {TYPE_NAMES[r['type']]} | {cat} | {r['power']} | {r['acc'] or '-'} | "
                         f"{r['pp']} | {r['prio']} | {m['effet']} | {m['chance'] or '-'} |")
        lines += ["", "## Objets\n", "| Nom | Effet | Type | Rareté |", "|---|---|---|---|"]
        for name, eff, typ, rar in it_report:
            lines.append(f"| {name.title()} | {eff} | {typ or '-'} | {rar} |")
        path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def font_chars(data):
    """Les caractères des polices du jeu (charmap de l'expansion), pour vérifier les noms."""
    return set(Expansion().charmap())


def main(argv):
    if len(argv) != 3:
        print("usage : gen_data.py <dossier data> <dossier de sortie>", file=sys.stderr)
        return 2
    data, out = Path(argv[1]), Path(argv[2])
    out.mkdir(parents=True, exist_ok=True)
    Gen(data, font_chars(data)).run(out)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
