# Poke Rush: a Pokémon fan game for NumWorks and PC

This is a combat-only Pokémon fan game based on pokeemerald-expansion and inspired by Emerald Rogue.
There is a lot of content: 600+ Pokémon, all Megas, single and double (VGC) battles, and more.

PokeRush is a roguelike / boss rush for the **NumWorks N0120** graphing calculator (and **N0110**,
see [Compatibility](#compatibility)), written in C11 on the official EADK. No overworld, no levels:
battle after battle, build your team from drafts, beat a Boss every 10 battles, become Champion at
battle 100, then keep going in Kanto. The whole game is playable in **English or French** (switch in
SETTINGS → « Langue / Language »).

> Unofficial fan project, not affiliated with Nintendo, Game Freak, Creatures or The Pokémon Company.
> Pokémon is a trademark of Nintendo / Creatures / Game Freak. Graphics and data come from Pokémon
> Emerald through [pokeemerald-expansion](https://github.com/rh-hideout/pokeemerald-expansion).

## Contents

- [Install](#install)
- [Compatibility](#compatibility)
- [Controls](#controls)
- [How a run works](#how-a-run-works)
- [Features](#features)
- [Game data](#game-data)
- [Building from source](#building-from-source)
- [Project layout](#project-layout)
- [Technical notes](#technical-notes)
- [Contributors](#contributors)

## Install

1. Download `pokerush.nwa` from the [Releases](../../releases) page.
2. Plug your calculator in over USB and install the file from https://my.numworks.com/apps.
3. Start **PokeRush** from the home screen.

Your run is saved automatically in a script named `pokemon.py`, and your POKéDEX, achievements and
settings in `carnet.py` (both visible in the Python app — don't edit them). Pressing Home quits; the
run resumes at the last checkpoint.

## Compatibility

| Model | Status |
|---|---|
| **N0120** | Developed and tested on it. |
| **N0110** | Should work too: same ARM Cortex-M architecture, same EADK app format and same storage system. Not tested yet — feedback welcome. |
| N0100 | Not supported (not enough flash memory for an app of this size). |
| PC (Windows) | A PC build with a window and keyboard controls is included for development (see [Building from source](#building-from-source)). |

The app needs about **1.9 MB** of flash and **117 KB** of RAM.

## Controls

| Key | Action |
|---|---|
| Arrows | Move the cursor |
| OK | Confirm, speed up text |
| EXE | Start the next battle (from the preparation menu); acts as OK elsewhere |
| Back | Cancel / go back, skip battle animations |
| Shift | Mega Evolution (in the move menu); open the menu during the reward choice |
| Home | Quit (the run resumes at the last checkpoint) |

On PC: arrows, Space = OK, Enter = EXE, Backspace = Back, Shift or M = shift, Esc = Home, F12 =
screenshot.

## How a run works

1. **New game**: pick a difficulty (Easy, Normal, Hard: trainers get 6 Pokémon at battle 30, 20 or
   10), a mode (Normal, or **Nuzlocke**: fainted Pokémon leave the team and a loss ends the run), a
   **seed** (random, or typed in to replay the exact same run), then one of the **27 starters** of
   the 9 generations. Your rival MAY picks the starter yours beats.
2. **Before each battle** (doubles): the "BEFORE THE BATTLE" screen shows the opponent's 6 Pokémon;
   choose your 4 and their order — the first two lead. The opponent picks its 4 based on your types.
3. **Battle**: 2 vs 2 in VGC format (or 1 vs 1 in singles, set in SETTINGS).
4. **After each victory**: loot, then **one reward**, your choice:
   - **RECRUIT** one of 5 offered Pokémon — it joins the team, or replaces a member if the team is
     full;
   - **TRAIN** a member — change a move, its nature or its ability.

   You can go back until you confirm; a final confirmation reminds you the choice is final. A badge
   shows the next opponent (favorite type, Boss or raid) so you can prepare.
5. **Preparation menu**: give items, reorder the team, check the POKéDEX, achievements, settings —
   then press **EXE** for the next battle.
6. **Checkpoints** every 10 battles. **Bosses** every 10 battles (Hoenn Gym Leaders, rival MAY,
   Elite Four, Champion WALLACE at battle 100, then the Kanto leaders, Elite Four and BLUE).

## Features

- **Double battles (VGC)**: target selection, spread moves (Earthquake, Surf, Rock Slide, Heat
  Wave…) at 75 % damage on each Pokémon hit. Battle boxes show each Pokémon's types, and while you
  pick a move, its effectiveness on every possible target (x4, x2, x1, x0.5…). Raids stay 1 vs 1.
- **Mega Evolution**: the MEGA RING arrives at battle 10 (Shift in the move menu). Mega Stones of
  your team drop often.
- **Item forms**: Zacian and the Rusted Sword, Giratina and the Griseous Orb, Kyogre / Groudon Primal
  Reversion, Arceus and its 17 Plates, Silvally and its Memories, Genesect, Ogerpon, the Forces of
  Nature and the Reveal Glass, Shaymin, Hoopa, Oricorio…
- **Raids**: after battle 25, a giant legendary sometimes appears (HP ×4, stats +25 %). Beat it and
  it is offered in the next draft.
- **Resonances** (PokeRush original): when 3, 4 or 5 team members share a type, it resonates at
  level I, II or III, with an effect per type — healing for Water, burns for Fire, drain for Grass,
  Speed for Electric, critical hits for Rock… The **Echo Prism** makes its holder count double.
- **Smart AI** that scales with progress: trainers have archetypes (weather, Trick Room, stall,
  hazards, Choice items, Baton Pass…), a favorite type and better moves and items over time.
- **Animated sprites**: the second frame of each Pokémon's animation plays when it enters battle;
  move, faint and Mega Evolution animations. Shiny Pokémon (rare!).
- **POKéDEX** (seen, caught, shiny, best battle; a free item every 50 species caught),
  **17 achievements** unlocking menu themes, characters and a light dialog box.
- **End of run**: ending cutscene after the Champion, Hall of Fame and run summary (also at the end
  of a Nuzlocke).
- **Settings**: language, single / double battles, animations, ×2 speed, Shift / Set battle style,
  theme, character, tutorial.
- **A secret Pokémon**, designed for this game, hides very rarely in the draft… Find it to unlock an
  achievement and a new theme.

## Game data

- **606 species**: every fully evolved Pokémon of the 9 generations that the engine can play
  (regional forms included), plus one original; **90 Mega forms**, **57 item forms**.
- **511 moves**, **298 abilities**, **246 held items** (type-resist and stat Berries, weather Rocks,
  Terrain Seeds, Red Card, Eject Button, Power Herb, Metronome, Choice items…).
- **Gen 9 rules at level 50**: type chart with Fairy, ×1.5 critical hits, Snow, Terrains, entry
  hazards, Trick Room, Tailwind, pivot moves.
- Left out on purpose: Pokémon that still evolve, and those the game can't represent (Ditto,
  Smeargle, Wobbuffet, Shedinja, Meltan, Eternal Floette, Zen Mode Darmanitan).

## Building from source

The tools read data and graphics from **pokeemerald-expansion**, which is not included in this
repository. Download version **1.17.2** (or the `master` branch of October 2026) and extract it to
`pokeemerald-expansion-master/pokeemerald-expansion-master/`.

### Requirements (Windows)

```
winget install Arm.GnuArmEmbeddedToolchain   # arm-none-eabi-gcc + newlib-nano
winget install OpenJS.NodeJS.LTS             # npx nwlink (packages the .nwa)
python -m pip install pillow                 # asset tools
```

Portable versions work too (no admin rights needed): put their `bin` folders in your `PATH`.

### Build

```
python tools/gen_data.py data build/gen          # data/ + expansion -> build/gen/data.c/.h
python tools/convert_assets.py assets build/gen  # graphics -> build/gen/assets.c/.h
python tools/build.py                            # -> build/pokerush.nwa (checks flash and RAM)
```

With a POSIX shell, the `Makefile` does the same: `make`, `make sim` (PC version), `make test`.
`python tools/build.py diag` builds a small diagnostic app (`tests/diag.c`: file system, render
timing, keys).

### PC version and tests

`make sim` builds the game for PC, with `sim/` replacing the EADK. On Windows it opens a window;
with `SIM_SCRIPT` the keyboard follows a script and PPM screenshots are written:

```
SIM_SCRIPT="_*3 shot:title down ok _*14 shot:starter" SIM_OUT=. build/sim/pokerush
```

`make test` runs the PC tests: data (type chart, Megas, Gen 9 values, both languages), battle engine
(scenarios, 5,000 random single battles, 3,000 random double battles, message lengths in both
languages), trainers and AI, save files.

## Project layout

```
PokeRush/
├── assets/                 icon, manifest.json (trainer classes, in French and English), sheets
│   └── claude_code/        graphics of the original Pokémon (made by tools/claude_code_art.py)
├── data/                   what the game takes from the expansion
│   ├── roster.csv          species (French name, legendary); add new ones at the end
│   ├── moves.csv           moves and their effect in the engine
│   ├── abilities.csv       abilities: name, implemented or not, descriptions (French / English)
│   └── items.csv           held items and their descriptions (French / English)
├── tools/
│   ├── expansion.py        reads the expansion (config, species, Megas, moves, fonts, English names)
│   ├── gen_data.py         data/ + expansion -> build/gen/data.c/.h, data_report.md
│   ├── convert_assets.py   graphics -> build/gen/assets.c/.h, preview.png
│   ├── claude_code_art.py  draws the original Pokémon
│   └── build.py            build without make (Windows)
├── src/
│   ├── main.c
│   ├── platform/           the only code touching the EADK (files, keyboard, app)
│   ├── gfx/                display (logical screen ×1.5), widgets, battle UI, menus
│   ├── game/               pokemon.h, battle (engine), ai, trainer, fight (battle on screen),
│   │                       screens (title, starter, draft, summaries), meta, play (the run)
│   └── core/               rng, base64, save, POKéDEX
├── sim/                    PC EADK (Windows window, key scripts, screenshots)
└── tests/                  PC tests, diagnostic app, screen demo
```

## Technical notes

- **Rendering**: a 214×160 logical screen (GBA size, so every layout is Emerald's) kept in RAM and
  shown at ×1.5 on the 320×240 screen with "sharp bilinear" scaling: each pair of pixels becomes
  three (first, 50/50 blend, second), so pixel art stays crisp. Animations only resend their
  rectangle.
- **Graphics**: 4 bpp indexed pixels + RGB565 palettes; a shiny costs one extra palette. Sprites are
  compressed with raw deflate (the 2nd animation frame uses the 1st as a dictionary), decoded by a
  small inflate in `src/gfx/display.c` with a 3-sprite cache.
- **Two languages in one app**: every string is written side by side, `L("français", "English")`
  (`src/game/pokemon.h`); generated tables (species, moves, abilities, items, natures, types) exist
  in both languages, English names coming from the expansion.
- **Save**: the run (team, battle, bag, flags) is bit-packed into 96 bytes, Base64-encoded with a
  FNV-1a checksum in `pokemon.py`. The EADK has no file API: `src/platform/storage.c` writes directly
  into Epsilon's file system in RAM, never moving a file.
- **Budgets**: flash ~1.88 MB of 2.4 MB allowed; static RAM ~117 KB of 128 KB (67 KB logical screen,
  15 KB transfer buffer, 10 KB decoded sprites).

Code comments are in French.

## Contributors

- **[tonios131010-lab](https://github.com/tonios131010-lab)** — idea, game design, testing on
  calculator.
- **[Claude Code](https://claude.com/claude-code)** (Anthropic) — programming, tools, translation and
  the original Pokémon's pixel art.

Thanks to [pokeemerald-expansion](https://github.com/rh-hideout/pokeemerald-expansion) and the pret
decompilation projects, and to Emerald Rogue for the inspiration.
