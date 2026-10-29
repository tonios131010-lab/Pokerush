#!/usr/bin/env python3
"""La version PC de PokeRush (Windows) : build/pc/PokeRush.exe, son dossier data\\ (musiques et cris de
l'expansion) et build/PokeRush-PC.zip, prêt à distribuer.

    python -m pip install ziglang pillow     # une fois : le compilateur (zig cc) et Pillow
    python tools/build_pc.py

Le jeu est le même que sur la calculatrice ; sim/ remplace l'EADK : fenêtre (plein écran), clavier,
manette XInput, son (sim/pc_video.c, sim/pc_audio.c). Les musiques sont les MIDI d'Émeraude, jouées par
le synthétiseur General MIDI de Windows ; chaque morceau a un fichier .map qui dit quels instruments sont
la batterie (« D ») ou le bruit (« N ») de la GBA, d'après son voicegroup.
"""
import math
import os
import re
import shutil
import struct
import subprocess
import sys
import wave
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
from expansion import Expansion  # noqa: E402

OUT = ROOT / "build" / "pc"
GEN = ROOT / "build" / "gen"

# les musiques jouées (sim/pc_audio.c, songs[]) ; les secondes servent si les premières manquent
SONGS = ["mus_title", "mus_poke_center", "mus_vs_trainer", "mus_vs_gym_leader", "mus_vs_rival", "mus_vs_elite_four",
         "mus_vs_champion", "mus_vs_kyogre_groudon", "mus_vs_regi", "mus_rg_vs_gym_leader", "mus_victory_trainer",
         "mus_victory_gym_leader", "mus_victory_league", "mus_hall_of_fame", "mus_credits"]


def zig():
    if os.environ.get("ZIG"):
        return [os.environ["ZIG"]]
    try:
        import ziglang
        return [str(Path(ziglang.__file__).parent / ("zig.exe" if os.name == "nt" else "zig"))]
    except ImportError:
        if shutil.which("zig"):
            return ["zig"]
    sys.exit("zig introuvable : python -m pip install ziglang")


def run(cmd, **kw):
    print(">", " ".join(str(c) for c in cmd[:6]), "..." if len(cmd) > 6 else "")
    subprocess.run([str(c) for c in cmd], check=True, cwd=ROOT, **kw)


def icon():
    """assets/icon.png -> une icône Windows, et la ressource qui la met dans l'exécutable."""
    from PIL import Image
    obj = OUT / "obj"
    obj.mkdir(parents=True, exist_ok=True)
    im = Image.open(ROOT / "assets" / "icon.png").convert("RGBA")
    big = im.resize((256, 256 * im.height // im.width), Image.NEAREST)
    sq = Image.new("RGBA", (256, 256), (0, 0, 0, 0))
    sq.paste(big, (0, (256 - big.height) // 2))
    sq.save(obj / "icon.ico", sizes=[(16, 16), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])
    (obj / "icon.rc").write_text('1 ICON "icon.ico"\n')
    return obj / "icon.rc"


def compile_exe():
    if not (GEN / "data.c").exists() or not (GEN / "assets.c").exists():
        run([sys.executable, "tools/gen_data.py", "data", "build/gen"])
        run([sys.executable, "tools/convert_assets.py", "assets", "build/gen"])
    src = sorted(str(p.relative_to(ROOT)) for p in (ROOT / "src").rglob("*.c"))
    sims = sorted(str(p.relative_to(ROOT)) for p in (ROOT / "sim").glob("*.c"))
    cmd = zig() + ["cc", "-target", "x86_64-windows-gnu", "-std=c11", "-O2", "-DPOKERUSH_PC", "-mno-ms-bitfields",
                   "-Wall", "-Wextra", "-Wno-unused-parameter", "-Isim", "-Isrc", "-Ibuild/gen"]
    cmd += src + ["build/gen/data.c", "build/gen/assets.c"] + sims
    try:
        cmd += [str(icon())]
    except Exception as e:  # sans Pillow : pas d'icône
        print("icône ignorée :", e)
    cmd += ["-luser32", "-lgdi32", "-lwinmm", "-Wl,--subsystem,windows", "-o", str(OUT / "PokeRush.exe")]
    OUT.mkdir(parents=True, exist_ok=True)
    run(cmd)


def voice_kinds(x, voicegroup):
    """128 caractères : D batterie (voice_keysplit_all), N bruit, . le reste."""
    path = x.root / "sound" / "voicegroups" / f"{voicegroup}.inc"
    kinds = ["."] * 128
    if not path.exists():
        return "".join(kinds)
    i = 0
    for line in path.read_text(encoding="utf-8").splitlines():
        s = line.strip()
        if not s.startswith("voice_") or s.startswith("voice_group"):
            continue
        if i < 128:
            kinds[i] = "D" if s.startswith("voice_keysplit_all") else "N" if s.startswith("voice_noise") else "."
        i += 1
    # au-delà du voicegroup (le 127 de certains morceaux) : la batterie, pas le coup de feu du General MIDI
    for k in range(i, 128):
        kinds[k] = "D"
    return "".join(kinds)


def music(x):
    src = x.root / "sound" / "songs" / "midi"
    cfg = (src / "midi.cfg").read_text(encoding="utf-8")
    dst = OUT / "data" / "music"
    dst.mkdir(parents=True, exist_ok=True)
    n = 0
    for name in SONGS:
        mid = src / f"{name}.mid"
        if not mid.exists():
            continue
        shutil.copy(mid, dst / mid.name)
        m = re.search(rf"^{name}\.mid:(.*)$", cfg, re.M)
        opts = m.group(1) if m else ""
        vol = re.search(r"-V(\d+)", opts)
        group = re.search(r"-G_?(\w+)", opts)
        kinds = voice_kinds(x, group.group(1)) if group else "." * 128
        (dst / f"{name}.map").write_text(f"volume={int(vol.group(1)) if vol else 100}\n{kinds}\n")
        n += 1
    print(n, "musiques")


def claude_code_cry(path):
    """Le cri de CLAUDE CODE : des bips d'ordinateur qui montent, calculés."""
    rate, out = 22050, bytearray()
    for k, (f, ms) in enumerate([(660, 70), (990, 60), (0, 30), (880, 60), (1320, 90), (1760, 140)]):
        for i in range(rate * ms // 1000):
            t = i / rate
            env = min(1, i / 200) * (1 - i / (rate * ms / 1000)) ** 0.5
            s = (1 if math.sin(2 * math.pi * f * t) >= 0 else -1) * 0.35 * env if f else 0
            out += struct.pack("<h", int(s * 32767))
    with wave.open(str(path), "wb") as w:
        w.setnchannels(1), w.setsampwidth(2), w.setframerate(rate)
        w.writeframes(bytes(out))


def cries(x):
    src = x.root / "sound" / "direct_sound_samples" / "cries"
    dst = OUT / "data" / "cries"
    dst.mkdir(parents=True, exist_ok=True)
    files = {p.stem for p in src.glob("*.wav")}
    roster = x.roster(ROOT / "data")
    lines = []
    for cname, fr, sp, legend, base, stone, kind in roster:
        if cname == "CLAUDE_CODE":
            claude_code_cry(dst / "claude_code.wav")
            lines.append("claude_code")
            continue
        cands = []
        if sp.get("cry", "").startswith("CRY_"):
            cands.append(sp["cry"][4:].lower())
        parts = cname.lower().split("_")
        cands += ["_".join(parts[:k]) for k in range(len(parts), 0, -1)]
        if base is not None:
            cands.append(roster[base][0].lower())
        name = next((c for c in cands if c in files), None)
        if name and not (dst / f"{name}.wav").exists():
            shutil.copy(src / f"{name}.wav", dst / f"{name}.wav")
        lines.append(name or "-")
    (OUT / "data" / "cries.txt").write_text("\n".join(lines) + "\n")
    print(sum(1 for l in lines if l != "-"), "cris sur", len(lines), "espèces et formes")


def package():
    readme = """PokeRush - version PC (Windows)
==============================

Lancer PokeRush.exe. Les sauvegardes sont dans %APPDATA%\\PokeRush.

Clavier : flèches ou ZQSD/WASD, ENTRÉE valider, ÉCHAP retour, MAJ Méga-Évolution et menu,
ESPACE combat suivant, F11 ou Alt+Entrée plein écran.
Manette (Xbox / XInput) : croix ou stick, A valider, B retour, X / Y / gâchettes Méga et menu,
START combat suivant.

PARAMÈTRES : langue, plein écran, image nette ou étirée, taille de la fenêtre, filtres, boutons
affichés, vibrations, volumes de la musique, des bruitages et des cris.

Projet de fan non officiel. https://github.com/tonios131010-lab/Pokerush
"""
    (OUT / "LISEZ-MOI.txt").write_text(readme, encoding="utf-8")
    zp = ROOT / "build" / "PokeRush-PC.zip"
    with zipfile.ZipFile(zp, "w", zipfile.ZIP_DEFLATED) as z:
        for p in [OUT / "PokeRush.exe", OUT / "LISEZ-MOI.txt"] + sorted((OUT / "data").rglob("*")):
            if p.is_file():
                z.write(p, Path("PokeRush") / p.relative_to(OUT))
    print("->", zp, f"{zp.stat().st_size // 1024} Ko")


def main():
    x = Expansion()
    compile_exe()
    music(x)
    cries(x)
    package()


if __name__ == "__main__":
    main()
