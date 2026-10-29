# Poke Rush : A pokemon fan game for numworks and pc

This is a combat only pokemon fan game based of Pokémon emerald expanded and inspired of emerald rogue.
There is a lot of content : 600+ pokemons, all megas, simple and duo vgc combats and more.

Roguelike / boss rush Pokémon pour la calculatrice NumWorks N0120, en C11 sur l'EADK. Des combats à
la chaîne (en duo au format VGC, ou en simple), sans overworld ni niveaux, avec 606 Pokémon des 9
générations, leurs formes Méga et les chromatiques. En français et en anglais.

A Pokémon roguelike / boss rush for the NumWorks N0120 calculator, in French and English (switch in
SETTINGS: « Langue / Language »).

Projet de fan non officiel, sans lien avec Nintendo, Game Freak ou The Pokémon Company. Les
graphismes et les données viennent de Pokémon Émeraude via
[pokeemerald-expansion](https://github.com/rh-hideout/pokeemerald-expansion) ; Pokémon est une marque
de Nintendo / Creatures / Game Freak.

## Installer

1. Télécharger `pokerush.nwa` dans les [Releases](../../releases).
2. Brancher la calculatrice (N0120) et l'installer depuis https://my.numworks.com/apps.
3. Lancer PokeRush. La partie se sauvegarde dans le script `pokemon.py` et le Carnet dans `carnet.py`.

## Compiler

Le code lit les données et les graphismes de pokeemerald-expansion, qui n'est pas dans ce dépôt :
télécharger la version 1.17.2 (ou la branche `master` d'octobre 2026) et la décompresser dans
`pokeemerald-expansion-master/pokeemerald-expansion-master/`. Puis, avec les prérequis plus bas :

```
python tools/gen_data.py data build/gen
python tools/convert_assets.py assets build/gen
python tools/build.py
```

## Le jeu

- **Nouvelle partie** : difficulté (Facile, Normal, Difficile : les dresseurs ont 6 Pokémon au combat 30,
  20 ou 10), mode Normal ou **Nuzlocke** (un K.O. fait perdre le Pokémon, une défaite finit la partie),
  **graine** aléatoire ou saisie (même graine, même partie), tutoriel, puis un des 27 starters. La
  rivale FLORA prend le starter que le vôtre bat.
- **Après chaque victoire** : butin, puis **une seule récompense**, au choix : **RECRUTER** un des 5
  Pokémon proposés (il rejoint l'équipe, ou remplace un membre si elle est complète) ou **ENTRAÎNER**
  un membre (attaque, nature, talent). Retour remonte jusqu'à ce choix tant que rien n'est confirmé, et
  une confirmation rappelle que le choix est définitif ; shift ouvre le menu pendant ce temps. Ensuite,
  le **menu de préparation** s'ouvre (objets, ordre de l'équipe, Carnet, Succès, Paramètres) et **EXE**
  lance le combat suivant. Un badge annonce le prochain adversaire : son **type favori**, un Boss ou
  un raid. Une Résonance gagnée est annoncée avec son effet.
- **Boss** tous les 10 combats (Champions de Hoenn, FLORA, Conseil 4, Maître, puis Kanto) : équipe et
  stratégie fixes. **Raids** au hasard après le combat 25 : un légendaire géant (PV ×4, stats +25 %),
  proposé au draft s'il est vaincu.
- **Méga-Évolution** (Méga-Anneau au combat 10, touche shift) ; les méga-gemmes de l'équipe tombent
  souvent, celles d'autres Pokémon parfois. **Chromatiques** très rares.
- **Formes à objet** : Zacian et l'Épée Rouillée, Giratina et le Globe Platiné, Kyogre et l'Orbe Bleu
  (Primo-Résurgence), Arceus et ses 17 Plaques, Silvallié et ses ROM, Genesect, Ogerpon, les génies
  et le Miroir Sacré, Shaymin, Hoopa, Plumeline… La forme se prend en entrant au combat (sans Méga-
  Anneau ni Méga-Évolution consommée) ; les dresseurs la portent, et l'objet de forme d'un Pokémon de
  l'équipe tombe aussi souvent qu'une méga-gemme.
- **Combats en duo (format VGC)**, réglables en simple dans les PARAMÈTRES : juste avant chaque
  combat, un écran « AVANT LE COMBAT » montre les 6 POKéMON adverses et l'on choisit ses 4 (les deux
  premiers ouvrent) ; l'adversaire choisit les siens selon les types en face. 2 contre 2, cible au
  choix, attaques de zone (Séisme, Surf, Éboulement…) à 75 % sur chaque Pokémon touché. Les cadres
  montrent les types (en abrégé) et, pendant le choix d'une attaque, son efficacité sur chaque cible
  (x2, x0,5…). Les raids restent en 1 contre 1.
- **Sprites animés** : la 2e image de l'animation de chaque Pokémon (expansion) joue à son entrée en
  combat, après un changement de forme, et de temps en temps sur la fiche du draft.
- **Résonances** (propre à PokeRush) : un type partagé par 3, 4, 5 Pokémon résonne au niveau I, II, III,
  avec un effet par type : soin chaque tour pour l'Eau, critiques pour la Roche, brûlures pour le Feu,
  drain pour la Plante, Vitesse pour l'Électrik… L'**Écho-Prisme** fait compter son porteur double.
- **Fin de partie** : cinématique après le Maître, Hall of Fame et bilan (aussi à la fin d'un
  Nuzlocke). **Carnet** (vus, obtenus, chromatiques, record ; un objet offert par 50 espèces),
  **17 succès** qui débloquent thèmes, personnages et une boîte de dialogue claire.
- **CLAUDE CODE**, un Pokémon secret propre au jeu (Électrik / Psy, 720 de base, dessiné par
  `tools/claude_code_art.py`) : il se cache très rarement dans le draft (1 sur 500) et y est garanti
  juste après le Hall of Fame (draft du combat 101). Le trouver débloque le succès « Code secret » et
  le thème TERRE CUITE. Aucun dresseur ne l'utilise.
- **Deux langues** dans la même application, au choix dans les PARAMÈTRES (« Langue / Language ») :
  français ou anglais. Les noms anglais (Pokémon, attaques, talents, objets, classes de dresseurs)
  viennent de l'expansion ; chaque texte du code est écrit dans les deux langues, côte à côte :
  `L("français", "English")` (src/game/pokemon.h). Les descriptions de talents et d'objets ont une
  colonne `desc_en` dans data/.
- **Paramètres** : langue, animations d'attaque et de K.O., vitesse ×2, mode Choix ou Défini (changer de
  Pokémon quand l'adversaire en envoie un), thème, personnage, tutoriel. En combat, Retour passe les
  animations ; un compteur de combats reste affiché en haut à droite.

Contenu : 605 espèces complètement évoluées (toutes celles de l'expansion que le moteur sait jouer,
formes régionales comprises) et CLAUDE CODE, 90 formes Méga, 57 formes à objet, 511 attaques (le maximum des 9 bits),
298 talents, 246 objets (baies anti-type et de stats, Roches, Graines, Carton Rouge, Bouton Fuite,
Herbe Pouvoir, Métronome…). Règles de la 9G au niveau 50 : table des types avec Fée, critiques ×1,5,
Neige, Champs, pièges, Distorsion, Vent Arrière, attaques de relais. Sprites compressés (LZ), 2e image
des animations compressés en deflate (la 2e image avec la 1re pour dictionnaire, décodeur dans
src/gfx/display.c) : 1,88 Mo de flash au total avec les deux langues.

Absents volontairement : les Pokémon qui évoluent encore, et ceux que le jeu ne peut pas représenter
(Métamorph, Queulorior, Qulbutoké, Munja, Meltan, Floette Éternelle, Darumacho Transe).

Touches : flèches, OK, EXE (combat suivant ; sinon comme OK), Retour, shift (Méga-Évolution, menu du
draft), Maison pour quitter (la partie reprend au dernier palier).

## Arborescence

```
PokeRush/
├── assets/                 icône, manifest.json (classes de dresseurs, en français et en anglais),
│   │                       planches (fonds, boîtes)
│   └── claude_code/        les graphismes de CLAUDE CODE (faits par tools/claude_code_art.py)
├── data/                   ce que le jeu prend de l'expansion, et ses noms français
│   ├── roster.csv          les espèces (nom français, légendaire) ; ajouter en fin de fichier
│   ├── moves.csv           les attaques, leur effet dans le moteur (puissance imposée en option)
│   ├── abilities.csv       les talents : nom, programmé ou non, description (et desc_en)
│   └── items.csv           les objets tenus, leur description (et nom_en, desc_en) ; @mega_stones marque la place des
│                           méga-gemmes (les objets de forme suivent tout seuls)
├── pokeemerald-expansion-master/   à télécharger : stats, attaques, formes, graphismes
├── tools/
│   ├── expansion.py        lecture de l'expansion (config, espèces, Méga, attaques, polices)
│   ├── gen_data.py         data/ + expansion -> build/gen/data.c/.h, data_report.md
│   ├── convert_assets.py   graphismes -> build/gen/assets.c/.h, preview.png
│   ├── claude_code_art.py  dessine CLAUDE CODE (assets/claude_code/)
│   └── build.py            compilation sans make (Windows)
├── src/
│   ├── main.c
│   ├── platform/           seul code qui touche l'EADK et Epsilon (fichiers, clavier, app)
│   ├── gfx/                display (écran logique ×1,5), widgets, ui (combat), menu
│   ├── game/               pokemon.h, battle (moteur), ai, trainer, fight (combat à l'écran),
│   │                       screens (titre, starter, draft, fiches), play (la partie)
│   └── core/               rng, base64, save
├── sim/                    EADK de PC (fenêtre Windows, script de touches, captures)
└── tests/                  tests sur PC, app de diagnostic, démo des écrans
```

## Rendu

L'écran logique fait 214×160, la taille de celui de la GBA : toute la mise en page est celle
d'Émeraude. Il vit en RAM (67 Ko) et s'affiche à l'échelle 1,5 sur les 320×240 de la calculatrice :
chaque paire de pixels devient trois pixels, le premier, leur mélange 50/50, le second (« sharp
bilinear »). Au plus proche voisin, une échelle de 1,5 ferait des pixels de 1 et 2 px de large qui
déforment le pixel art ; ici les contours restent nets et texte, sprites et menus sont tous 1,5 fois
plus grands. Une animation ne renvoie que son rectangle (barre de PV, texte qui s'écrit).

Tous les graphismes viennent de l'expansion (un seul style) : sprites de face et de dos, palettes
normales et chromatiques, icônes, dresseurs, polices latin_normal et latin_small, cadres de PV, boutons
Méga, icônes de catégorie. Pixels en 4 bpp indexé + palettes RGB565 (`src/gfx/image.h`) ; un
chromatique ne coûte qu'une palette de plus. Après un changement, regarder `build/gen/preview.png`.

## Budgets

| Ressource | Limite | Actuel |
|---|---|---|
| Flash | 2 400 000 o (N0120 : 2,5 Mo) | ~1,88 Mo |
| RAM statique (data + bss) | 131 072 o | ~117 Ko (dont 67 Ko d'écran logique, 15 Ko d'envoi, 10 Ko de sprites décodés) |

## Sauvegarde

`src/core/save.c` écrit la partie (`Run` de `pokemon.h` : équipe, combat, objets, Méga-Anneau) dans
le script `pokemon.py`, visible dans l'app Python :

```python
# PokeRush : sauvegarde automatique, ne pas modifier.
SAVE_DATA = "UFIC776t3iUABQAAAAAAAAABZAAM…"
```

La chaîne est le Base64 de 96 octets : « PR », version (2), `Run` bit-packé (63 o), réserve, somme de
contrôle FNV-1a. Une sauvegarde abîmée, incohérente ou d'une ancienne version est ignorée.

L'EADK n'a pas d'API de fichiers : `src/platform/storage.c` écrit directement dans le système de
fichiers d'Epsilon, en RAM, sans jamais déplacer un fichier. `src/platform/app.c` retient la touche
Maison pendant le jeu et remet la RAM de l'app à zéro en sortant.

## Version PC et tests

`make sim` compile le jeu pour PC avec `sim/` à la place de l'EADK. Sous Windows, une fenêtre :
flèches, Espace = OK, Entrée = EXE, Retour arrière = Retour, Maj ou M = shift, Échap = Maison, F12 =
capture. Avec `SIM_SCRIPT`, le clavier suit un script et des captures PPM sont écrites :

```
SIM_SCRIPT="x*3 shot:titre down x ok x*14 shot:starter" SIM_OUT=. build/sim/pokerush
```

`tests/demo_screens.c` (à la place de `src/main.c`) ouvre directement le draft, l'équipe, les
attaques et un combat du Conseil 4 avec Méga-Évolution.

`make test` : données (table des types, Méga, valeurs 9G), moteur (scénarios et 5000 combats
aléatoires), dresseurs et IA, sauvegarde.

## Prérequis (Windows)

```
winget install Arm.GnuArmEmbeddedToolchain   # arm-none-eabi-gcc + newlib-nano
winget install OpenJS.NodeJS.LTS             # npx nwlink
python -m pip install pillow                 # outils d'assets
```

Sans make : `python tools/build.py` (le jeu) ou `python tools/build.py diag` (l'app de diagnostic
`tests/diag.c` : lecture des fichiers, temps de rendu, écriture, touche Maison). Les `.nwa`
s'installent depuis https://my.numworks.com/apps.
