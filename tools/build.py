#!/usr/bin/env python3
"""Compile PokeRush pour la calculatrice sans make ni shell Unix (pratique sous Windows).

    python tools/build.py          build/pokerush.nwa  (le jeu)
    python tools/build.py diag     build/prdiag.nwa    (l'app de diagnostic, tests/diag.c)

Il faut arm-none-eabi-gcc dans le PATH, Node.js (npx, pour nwlink) et Pillow. Les mêmes options
que le Makefile ; après l'édition de liens, le script fait aussi ce que fait `make check` :
il lie l'app comme la calculatrice (nwlink nwa-bin) et vérifie les budgets de flash et de RAM.
"""
import re
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BUILD = ROOT / "build"
GEN = BUILD / "gen"
NWLINK = ["--yes", "--", "nwlink@1.0.0"]
# N0120 : 2,5 Mo pour les applications d'après l'utilisateur (l'ancienne limite prudente était
# 2 031 616 o) ; on garde ~100 Ko de marge.
FLASH_BUDGET = 2400000
RAM_BUDGET = 131072

CFLAGS = [
    "-std=c11", "-Isrc", "-I" + str(GEN),
    "-Wall", "-Wextra", "-Wno-unused-parameter", "-Wshadow", "-Wstack-usage=2048",
    "-fno-math-errno", "-fsingle-precision-constant",
    "-ffunction-sections", "-fdata-sections", "-fno-unwind-tables", "-fno-asynchronous-unwind-tables",
    "-fno-tree-loop-distribute-patterns",
    "-Os", "-DNDEBUG", "-flto", "-flto-partition=one", "-fno-fat-lto-objects", "-fwhole-program",
    "-fvisibility=internal",
]
LDFLAGS = [
    "-Wl,--relocatable", "-nostartfiles", "--specs=nano.specs",
    "-Wl,-e,main", "-Wl,-u,eadk_app_name", "-Wl,-u,eadk_app_icon", "-Wl,-u,eadk_api_level",
    "-Wl,--gc-sections", "-flinker-output=nolto-rel",
]


def tool(name):
    path = shutil.which(name)
    if not path:
        sys.exit(f"{name} est introuvable : voir les prérequis dans README.md")
    return path


def run(cmd, capture=False):
    if not capture:
        print(">", Path(cmd[0]).name, " ".join(cmd[1:5]), "..." if len(cmd) > 5 else "")
    r = subprocess.run(cmd, cwd=ROOT, capture_output=capture, text=True)
    if r.returncode:
        if capture:
            print(r.stdout, r.stderr)
        sys.exit(f"échec : {Path(cmd[0]).name}")
    return r.stdout if capture else None


def newer(target, sources):
    return not target.exists() or any(s.stat().st_mtime > target.stat().st_mtime for s in sources)


def main(argv):
    diag = len(argv) > 1 and argv[1] == "diag"
    name = "prdiag" if diag else "pokerush"
    gcc, npx = tool("arm-none-eabi-gcc"), tool("npx")
    BUILD.mkdir(exist_ok=True)

    sources = [ROOT / "tools" / "convert_assets.py", ROOT / "tools" / "expansion.py", ROOT / "assets" / "manifest.json",
               ROOT / "data" / "roster.csv"]
    sources += list((ROOT / "assets").rglob("*.png")) + list((ROOT / "assets").rglob("*.gif"))
    if newer(GEN / "assets.c", sources):
        run([sys.executable, "tools/convert_assets.py", "assets", str(GEN)])
    data = [ROOT / "tools" / "gen_data.py", ROOT / "tools" / "expansion.py"] + list((ROOT / "data").glob("*.csv"))
    if newer(GEN / "data.c", data):
        run([sys.executable, "tools/gen_data.py", "data", str(GEN)])

    icon = BUILD / "icon.o"
    if newer(icon, [ROOT / "assets" / "icon.png"]):
        run([npx] + NWLINK + ["png-icon-o", "assets/icon.png", str(icon)])

    # (nwlink met le chemin de eadk.h entre guillemets)
    out = run([npx] + NWLINK + ["eadk-cflags-device"], capture=True)
    eadk = [a.strip('"') for a in re.findall(r'"[^"]*"|\S+', out)]
    src = sorted(p for p in (ROOT / "src").rglob("*.c"))
    if diag:
        src = [s for s in src if s.name != "main.c"] + [ROOT / "tests" / "diag.c"]
    src += [GEN / "assets.c", GEN / "data.c"]

    # Chaque fichier donne un objet LTO ; on ne recompile que ce qui a changé (assets.c est gros).
    headers = list((ROOT / "src").rglob("*.h")) + [GEN / "assets.h", GEN / "data.h"]
    objs = []
    for s in src:
        obj = BUILD / "obj" / (s.stem + ".o")
        obj.parent.mkdir(parents=True, exist_ok=True)
        if newer(obj, [s] + headers + [Path(__file__)]):
            run([gcc] + CFLAGS + eadk + ["-c", str(s), "-o", str(obj)])
        objs.append(str(obj))

    elf, nwa, binf = BUILD / f"{name}.elf", BUILD / f"{name}.nwa", BUILD / f"{name}.bin"
    run([gcc] + CFLAGS + eadk + LDFLAGS + objs + [str(icon), "-o", str(elf), "-Wl,-Map=" + str(BUILD / f"{name}.map")])
    run([tool("arm-none-eabi-strip"), "--strip-unneeded", str(elf), "-o", str(nwa)])

    size = run([tool("arm-none-eabi-size"), str(nwa)], capture=True).splitlines()[1].split()
    ram = int(size[1]) + int(size[2])
    run([npx] + NWLINK + ["nwa-bin", str(nwa), str(binf)])
    flash = binf.stat().st_size
    print(f"FLASH  {flash} / {FLASH_BUDGET} octets" + ("" if flash <= FLASH_BUDGET else "  BUDGET DÉPASSÉ"))
    print(f"RAM    {ram} / {RAM_BUDGET} octets (data + bss)" + ("" if ram <= RAM_BUDGET else "  BUDGET DÉPASSÉ"))
    print(f"\n-> {nwa}")
    return 0 if flash <= FLASH_BUDGET and ram <= RAM_BUDGET else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
