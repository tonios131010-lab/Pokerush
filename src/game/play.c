/* La partie : difficulté, graine, starter, combats, butin, draft, checkpoints sauvegardés tous les 10
 * combats, Boss tous les 10 combats, Méga-Anneau au combat 10, légendaire garanti au draft du combat 20,
 * raids de légendaires géants, Maître au combat 100 (cinématique et Hall of Fame), puis Kanto. Mode
 * Nuzlocke au choix (une défaite y termine la partie). Le Carnet, le journal et les succès suivent tout. */
#include "game/play.h"
#include <string.h>
#include "core/dex.h"
#include "core/rng.h"
#include "core/save.h"
#include "data.h"
#include "game/battle.h"
#include "game/fight.h"
#include "game/meta.h"
#include "game/screens.h"
#include "game/trainer.h"
#include "gfx/menu.h"
#include "platform/input.h"
#include "platform/pc.h"

RunStats run_stats;
static Run run;
static bool quit;
static uint32_t rng;

static const char *num(char *buf, unsigned v) { /* buf : 12 octets */
  char *p = buf + 11;
  *p = 0;
  do *--p = (char)('0' + v % 10); while (v /= 10);
  return p;
}

static void own(const Pokemon *p) {
  dex_own(p->species, p->shiny);
  run_stats.shinies += p->shiny;
  if (p->shiny) achieve(ACH_SHINY, &quit);
  if (dex_count(dex.owned) >= 100) achieve(ACH_DEX100, &quit);
}

static void save_all(void) {
  save_write(&run, &run_stats);
  if (run.battle > dex.best) dex.best = run.battle;
  dex_save();
}

/* « ÉQUIPE · EAU II, FEU I » : les Résonances de l'équipe dans le titre. */
static const char *team_title(void) {
  static char title[48];
  static const char *const roman[4] = {"", " I", " II", " III"};
  uint8_t lv[NUM_TYPES];
  resonance_levels(&run.team, lv);
  strcpy(title, L("ÉQUIPE", "TEAM"));
  bool first = true;
  for (int k = 0; k < NUM_TYPES; k++)
    if (lv[k] && strlen(title) + 16 < sizeof title) {
      strcat(strcat(strcat(title, first ? " · " : ", "), type_names[k]), roman[lv[k]]);
      first = false;
    }
  return title;
}

/* Le choix de la récompense est définitif : une seule par victoire. */
static bool final_choice(const char *what) {
  static char text[160];
  strcat(strcpy(text, what), L(" Ce choix est définitif et unique pour cette victoire. Confirmer ?",
                               " This choice is final, and the only one for this victory. Confirm?"));
  return menu_confirm(text, &quit);
}

/* Les Résonances que l'équipe vient de gagner (ou de monter), avec leur effet. */
static void announce_resonances(const uint8_t before[NUM_TYPES]) {
  static const char *const roman[4] = {"", " I", " II", " III"};
  uint8_t after[NUM_TYPES];
  resonance_levels(&run.team, after);
  for (int k = 0; k < NUM_TYPES && !quit; k++) {
    if (after[k] <= before[k]) continue;
    static char text[160];
    strcpy(text, before[k] ? L("La RÉSONANCE monte : ", "RESONANCE up: ") : L("Nouvelle RÉSONANCE ", "New RESONANCE: "));
    strcat(strcat(text, type_names[k]), roman[after[k]]);
    strcat(strcat(strcat(strcat(strcat(text, " ("), resonance_names[k]), L(") : ", "): ")), resonance_descs[k]), ".");
    menu_message(text, &quit);
  }
}

/* Nouvelle attaque (parmi celles que l'espèce peut apprendre) ; false si le joueur revient en arrière. */
static bool change_move(Pokemon *p) {
  uint16_t slots[MOVES_MAX];
  for (uint8_t k = 0; k < MOVES_MAX; k++) slots[k] = pokemon_move(p, k);
  const SpeciesInfo *s = &species_info[p->species];
  static uint16_t ids[128];
  int n = 0;
  for (uint8_t i = 0; i < s->learn_cnt; i++) {
    uint16_t mv = species_learnset(s, i);
    bool known = false;
    for (uint8_t k = 0; k < MOVES_MAX; k++) known |= pokemon_move(p, k) == mv;
    if (!known) ids[n++] = mv;
  }
  for (;;) {
    int slot = screen_moves(L("Oublier quelle attaque ?", "Forget which move?"), slots, MOVES_MAX, &quit);
    if (slot < 0) return false;
    int c = screen_moves(L("Apprendre quelle attaque ?", "Learn which move?"), ids, n, &quit);
    if (quit) return false;
    if (c < 0) continue; /* retour : une autre attaque à oublier */
    static char what[96];
    strcat(strcat(strcat(strcpy(what, move_names[slots[slot]]), " → "), move_names[ids[c]]), ".");
    if (!final_choice(what)) {
      if (quit) return false;
      continue;
    }
    pokemon_set_move(p, (uint8_t)slot, ids[c]);
    return true;
  }
}

/* Une autre nature ; false si le joueur revient en arrière. */
static bool change_nature(Pokemon *p) {
  static char labels[NUM_NATURES][40];
  const char *items[NUM_NATURES];
  const char *const stat[6] = {"", L("ATQ", "ATK"), L("DÉF", "DEF"), L("VIT", "SPE"), L("A.SP", "SPA"), L("D.SP", "SPD")};
  for (int n = 0; n < NUM_NATURES; n++) {
    strcpy(labels[n], nature_names[n]);
    if (n / 5 != n % 5) strcat(strcat(strcat(strcat(labels[n], "  +"), stat[n / 5 + 1]), " -"), stat[n % 5 + 1]);
    items[n] = labels[n];
  }
  Menu m = {.title = L("Quelle nature ?", "Which nature?"), .items = items, .n = NUM_NATURES, .hue = 3, .cursor = p->nature};
  for (;;) {
    int n = menu_run(&m, &quit);
    if (n < 0) return false;
    static char what[64];
    strcat(strcat(strcpy(what, L("Nature ", "Nature: ")), nature_names[n]), ".");
    if (final_choice(what)) return p->nature = (uint8_t)n, true;
    if (quit) return false;
  }
}

/* Entraînement : une autre attaque, une autre nature, ou l'autre talent de l'espèce. Retour remonte
 * d'un cran (jusqu'au choix de la récompense) ; rien ne change avant la confirmation. */
static bool train(void) {
  for (;;) {
    int who = screen_team(L("ENTRAÎNER : quel POKéMON ?", "TRAIN: which POKéMON?"), &run.team, &quit);
    if (who < 0) return false;
    Pokemon *p = &run.team.mon[who];
    const SpeciesInfo *s = &species_info[p->species];
    for (;;) {
      static char talent[48];
      uint16_t other = species_ability(s, !p->ability);
      strcat(strcpy(talent, L("TALENT : ", "ABILITY: ")), ability_names[other]);
      const char *acts[3] = {L("CHANGER UNE ATTAQUE", "CHANGE A MOVE"), L("CHANGER DE NATURE", "CHANGE NATURE"), talent};
      static char talent_why[96];
      strcat(strcat(strcpy(talent_why, L("L'autre talent de l'espèce : ", "The species' other ability: ")), ability_descs[other]), ".");
      const char *why[3] = {L("Oublier une attaque pour une autre que l'espèce peut apprendre.",
                              "Forget a move for another one the species can learn."),
                            L("Une autre nature : +10 % à une stat, -10 % à une autre.",
                              "Another nature: +10 % to one stat, -10 % to another."),
                            talent_why};
      int a = menu_list_desc(species_names[p->species], acts, why, s->ability1 ? 3 : 2, 3, &quit);
      if (a < 0) break; /* retour : un autre Pokémon */
      bool done = false;
      if (a == 0) done = change_move(p);
      if (a == 1) done = change_nature(p);
      if (a == 2) {
        static char what[48];
        strcat(strcpy(what, L("Talent ", "Ability: ")), ability_names[other]), strcat(what, ".");
        done = final_choice(what);
        if (done) p->ability ^= 1;
      }
      if (quit) return false;
      if (done) return true;
    }
    if (quit) return false;
  }
}

/* L'équipe, réordonnable : choisir un Pokémon, puis celui avec qui il échange sa place. */
static void view_team(void) {
  while (!quit) {
    int a = screen_team(team_title(), &run.team, &quit);
    if (a < 0) return;
    int b = screen_team(L("Échanger sa place avec ?", "Swap places with?"), &run.team, &quit);
    if (b < 0 || b == a) continue;
    Pokemon tmp = run.team.mon[a];
    run.team.mon[a] = run.team.mon[b], run.team.mon[b] = tmp;
  }
}

/* Donne un objet du sac à un membre (l'ancien objet retourne dans le sac). */
static bool give_item(void) {
  uint8_t items[BAG_MAX];
  int idx[BAG_MAX], n = 0;
  for (int i = 0; i < BAG_MAX; i++)
    if (run.bag[i]) idx[n] = i, items[n++] = run.bag[i];
  if (!n) return menu_message(L("Le sac est vide.", "The bag is empty."), &quit), false;
  int c = screen_items(L("Donner quel objet ?", "Give which item?"), items, n, &quit);
  if (c < 0) return false;
  int who = screen_team(L("À quel POKéMON ?", "To which POKéMON?"), &run.team, &quit);
  if (who < 0) return false;
  uint8_t old = (uint8_t)run.team.mon[who].item;
  run.team.mon[who].item = run.bag[idx[c]];
  run.bag[idx[c]] = old;
  return true;
}

/* Le menu de préparation. Pendant le choix de la récompense (shift) : Retour ou shift y revient. Après :
 * il s'ouvre seul, EXE lance le combat. */
static void prep_menu(bool after, uint16_t next_battle) {
  const char *items[6] = {L("DONNER UN OBJET", "GIVE AN ITEM"), L("ÉQUIPE (ordre)", "TEAM (order)"), L("CARNET", "POKéDEX"),
                          L("SUCCÈS", "ACHIEVEMENTS"), L("PARAMÈTRES", "SETTINGS"),
                          after ? L("COMBAT SUIVANT ({EXE})", "NEXT BATTLE ({EXE})") : L("RETOUR AU CHOIX", "BACK TO THE REWARD")};
  const char *why[6] = {L("Équiper un POKéMON d'un objet du sac.", "Give a POKéMON an item from the bag."),
                        L("Changer l'ordre de l'équipe : les premiers entrent en combat en premier.",
                          "Change the team order: the first ones go into battle first."),
                        L("Les POKéMON vus et obtenus.", "The POKéMON seen and obtained."),
                        L("Vos succès et ce qu'ils débloquent.", "Your achievements and what they unlock."),
                        L("Langue, combats en duo, animations, vitesse, thème…", "Language, double battles, animations, speed, theme…"),
                        after ? L("En route pour le combat suivant (touche {EXE}).", "On to the next battle ({EXE} key).")
                              : L("Revenir au choix de la récompense.", "Go back to choosing the reward.")};
  static char title[48];
  char n[12];
  strcat(strcpy(title, after ? L("PRÉPARATION · COMBAT ", "GET READY · BATTLE ") : L("MENU · COMBAT ", "MENU · BATTLE ")),
         num(n, next_battle));
  Menu m = {.title = title, .items = items, .descs = why, .n = 6, .hue = 1, .cursor = after ? 5 : 0,
            .alt = !after, .exe = after, .no_back = after,
            .help = after ? L("{OK} ouvrir   {EXE} combat suivant", "{OK} open   {EXE} next battle")
                          : L("{OK} ouvrir   {BACK} ou {ALT} revenir", "{OK} open   {BACK} or {ALT} return")};
  while (!quit) {
    int a = menu_run(&m, &quit);
    if (quit || a == MENU_EXE || a == MENU_BACK || a == MENU_ALT || a == 5) return;
    if (a == 0) give_item();
    if (a == 1) view_team();
    if (a == 2) screen_dex(&quit);
    if (a == 3) screen_achievements(&quit);
    if (a == 4) screen_settings(&quit);
  }
}

/* Recruter une des offres : false si le joueur revient au choix de la récompense. */
static bool recruit(Pokemon *offers, int n, const Trainer *next) {
  static int cursor;
  cursor = cursor < n ? cursor : 0;
  for (;;) {
    int c = screen_draft(offers, n, &run.team, next, &cursor, &quit);
    if (quit) return false;
    if (c == MENU_ALT) {
      prep_menu(false, run.battle + 1);
      continue;
    }
    if (c < 0) return false;
    /* une équipe complète : la recrue prend la place d'un membre ; sinon elle s'ajoute */
    int slot = run.team.count;
    if (run.team.count == TEAM_MAX) {
      slot = screen_team(L("Remplacer qui ?", "Replace whom?"), &run.team, &quit);
      if (slot < 0) continue;
    }
    static char what[96];
    strcpy(what, species_names[offers[c].species]);
    if (slot < run.team.count) strcat(strcat(strcat(what, L(" remplace ", " replaces ")), species_names[run.team.mon[slot].species]), ".");
    else strcat(what, L(" rejoint l'équipe.", " joins the team."));
    if (!final_choice(what)) {
      if (quit) return false;
      continue;
    }
    uint8_t before[NUM_TYPES];
    resonance_levels(&run.team, before);
    run.team.mon[slot] = offers[c];
    if (slot == run.team.count) run.team.count++;
    own(&offers[c]);
    announce_resonances(before);
    if (offers[c].species == SPECIES_CLAUDE_CODE) achieve(ACH_CLAUDE, &quit);
    if (run.team.count == TEAM_MAX) achieve(ACH_FULL_TEAM, &quit);
    return true;
  }
}

/* Après une victoire : 5 Pokémon proposés (un raid vaincu offre son légendaire en premier ; CLAUDE CODE,
 * très rare, est garanti juste après le Hall of Fame). On choisit UNE récompense : recruter ou
 * entraîner. Puis le menu de préparation, jusqu'à EXE. next : le prochain adversaire. */
static void draft(uint16_t won, int bonus, const Trainer *next) {
  Pokemon offers[5];
  uint8_t cap = (uint8_t)(3 + won / 10 > 7 ? 7 : 3 + won / 10);
  bool have_claude = false;
  for (int j = 0; j < run.team.count; j++) have_claude |= run.team.mon[j].species == SPECIES_CLAUDE_CODE;
  int claude = !have_claude && (won == 100 || rng_below(&rng, 500) == 0) ? (won == 100 ? 0 : 4) : -1;
  for (int i = 0; i < 5; i++) {
    bool legend = (won == 20 && i == 0) || (won > 20 && rng_percent(&rng, won / 10));
    uint16_t sp;
    bool ok;
    if (i == claude || (i == 0 && bonus >= 0)) {
      offers[i] = trainer_build(i == claude ? SPECIES_CLAUDE_CODE : (uint16_t)bonus, 0, 8, false, &rng);
      offers[i].shiny = rng_below(&rng, 64) == 0;
      dex_see(offers[i].species);
      continue;
    }
    do {
      sp = (uint16_t)rng_below(&rng, NUM_BASE_SPECIES);
      ok = sp != SPECIES_CLAUDE_CODE && species_info[sp].legendary == legend && (legend || species_info[sp].tier <= cap);
      for (int j = 0; j < i; j++) ok &= offers[j].species != sp;
      for (int j = 0; j < run.team.count; j++) ok &= run.team.mon[j].species != sp;
    } while (!ok);
    offers[i] = trainer_build(sp, 0, 7, false, &rng);
    offers[i].shiny = rng_below(&rng, 512) == 0;
    dex_see(sp);
  }
  if (claude >= 0) menu_message(L("Un signal étrange… Un POKéMON inconnu se cache parmi les offres !",
                                  "A strange signal… An unknown POKéMON hides among the offers!"), &quit);
  pc_music(MUSIC_DRAFT);
  int category = 0;
  bool done = false;
  while (!quit && !done) {
    int c = screen_reward(offers, 5, &run.team, next, won, &category, &quit);
    if (quit) return;
    if (c == MENU_ALT) prep_menu(false, (uint16_t)(won + 1));
    else if (c == 0) done = recruit(offers, 5, next);
    else if (c == 1) done = train();
    else if (c == 2) done = menu_confirm(L("Passer sans rien prendre ?", "Skip without taking anything?"), &quit);
  }
  if (!quit) prep_menu(true, (uint16_t)(won + 1));
}

static bool new_game(bool had_save) {
  if (had_save && !menu_confirm(L("Effacer la partie en cours\net en commencer une autre ?", "Erase the current game\nand start a new one?"), &quit)) return false;
  const char *const diffs[3] = {L("FACILE : 6 POKéMON au 30", "EASY: 6 POKéMON at 30"), L("NORMAL : 6 POKéMON au 20", "NORMAL: 6 POKéMON at 20"),
                                       L("DIFFICILE : 6 POKéMON au 10", "HARD: 6 POKéMON at 10")};
  const char *const diff_why[3] = {
      L("L'équipe se complète au combat 30, les dresseurs sont plus indulgents.", "Your team fills up by battle 30, and trainers go easier on you."),
      L("L'équipe se complète au combat 20 : l'expérience prévue.", "Your team fills up by battle 20: the intended experience."),
      L("L'équipe se complète au combat 10, face à des dresseurs experts qui Méga-Évoluent plus souvent.", "Your team fills up by battle 10, against expert trainers who Mega Evolve more often.")};
  Menu dm = {.title = L("Difficulté", "Difficulty"), .items = diffs, .descs = diff_why, .n = 3, .hue = 1, .cursor = 1};
  int diff = menu_run(&dm, &quit);
  if (diff < 0) return false;
  const char *const modes[2] = {L("MODE NORMAL", "NORMAL MODE"), L("MODE NUZLOCKE (K.O. = PERDU)", "NUZLOCKE (FAINTED = LOST)")};
  const char *const mode_why[2] = {L("Une défaite renvoie au dernier palier sauvegardé (réglable dans les PARAMÈTRES).", "A loss sends you back to the last checkpoint (set in SETTINGS)."),
                                           L("Un POKéMON K.O. quitte l'équipe, et une défaite termine la partie.", "A fainted POKéMON leaves the team, and a loss ends the game.")};
  int mode = menu_list_desc(L("Quel mode de jeu ?", "Which game mode?"), modes, mode_why, 2, 1, &quit);
  if (mode < 0) return false;
  const char *const seeds[2] = {L("GRAINE ALÉATOIRE", "RANDOM SEED"), L("SAISIR UNE GRAINE", "ENTER A SEED")};
  const char *const seed_why[2] = {L("Des dresseurs et des drafts tirés au hasard.", "Random trainers and drafts."),
                                           L("Rejouer une partie précise : sa graine s'affiche au bilan de fin.", "Replay a specific game: its seed is shown in the final summary.")};
  int sm = menu_list_desc(L("Graine de la partie", "Game seed"), seeds, seed_why, 2, 1, &quit);
  if (sm < 0) return false;
  uint32_t seed = rng_next(&rng);
  if (sm == 1 && !seed_entry(&seed, &quit)) return false;
  rng = seed ? seed : 1;
  trainer_difficulty = diff;
  if (!dex.tuto) tutorial(&quit);
  int c = screen_starter(&quit);
  if (c < 0) return false;
  memset(&run, 0, sizeof run);
  memset(&run_stats, 0, sizeof run_stats);
  run_stats.seed = seed;
  run.battle = 1;
  run.flags = (uint16_t)((mode ? RUN_NUZLOCKE : 0) | diff << RUN_DIFF_SHIFT);
  run.team.count = 1;
  run.team.mon[0] = trainer_build(starter_species[c], 0, 8, false, &rng);
  run.team.mon[0].shiny = rng_below(&rng, 256) == 0;
  run.seed = rng;
  own(&run.team.mon[0]);
  /* récompense du Carnet : un objet rare par tranche de 50 espèces obtenues */
  int gifts = dex_count(dex.owned) / 50;
  for (int i = 0; i < gifts && i < 4; i++) {
    uint8_t pick;
    do pick = (uint8_t)(1 + rng_below(&rng, NUM_ITEMS - 1));
    while (item_info[pick].rarity < 2 || item_info[pick].effect == ITEM_EFFECT_MEGA_STONE);
    run.bag[i] = pick;
  }
  if (gifts) menu_message(L("Le CARNET vous offre\ndes objets pour la route !", "The POKéDEX gives you\nitems for the road!"), &quit);
  dex.runs++;
  save_all();
  return true;
}

/* Le butin du dresseur vaincu, s'il y en a, rangé dans le sac. */
static void take_loot(const Trainer *t, uint16_t n) {
  uint8_t loot = trainer_loot(t, n, &run.team, (run.flags & RUN_MEGA) != 0, &rng);
  if (!loot) return;
  static char text[64];
  for (int i = 0; i < BAG_MAX; i++)
    if (!run.bag[i]) {
      run.bag[i] = loot;
      strcat(strcat(strcpy(text, L("Vous trouvez :\n", "You found:\n")), item_names[loot]), " !");
      return menu_message(text, &quit);
    }
  menu_message(L("Le sac est plein :\nl'objet est perdu…", "The bag is full:\nthe item is lost…"), &quit);
}

static void journal(const char *title) {
  static char text[96];
  char a[12], b[12], c[12], d[12];
  strcpy(text, title);
  strcat(strcat(strcat(text, L("  K.O. ", "  KOs ")), num(a, run_stats.kos)), L("\nDégâts ", "\nDamage "));
  strcat(strcat(strcat(text, num(b, run_stats.damage)), L("  Crit. ", "  Crits ")), num(c, run_stats.crits));
  strcat(strcat(text, L("  Méga ", "  Mega ")), num(d, run_stats.megas));
  menu_message(text, &quit);
}

/* Mode Nuzlocke : les Pokémon K.O. pendant le combat quittent l'équipe. pick : l'indice dans
 * l'équipe de chaque Pokémon du combat (duo : les 4 choisis). */
static void bury(const Battle *b, const uint8_t *pick) {
  bool down[TEAM_MAX] = {false};
  for (int k = 0; k < b->side[SIDE_PLAYER].count; k++) down[pick[k]] = !b->side[SIDE_PLAYER].team[k].hp;
  int n = 0;
  for (int i = 0; i < run.team.count; i++)
    if (!down[i]) run.team.mon[n++] = run.team.mon[i];
  if (n < run.team.count) menu_message(L("Mode Nuzlocke : les POKéMON\nK.O. quittent l'équipe…", "Nuzlocke: the fainted\nPOKéMON leave the team…"), &quit);
  run.team.count = (uint8_t)n;
}

static void check_achievements(uint16_t n, const Battle *b) {
  achieve(ACH_FIRST_WIN, &quit);
  if (run_stats.megas) achieve(ACH_MEGA, &quit);
  if (fight_feats & FEAT_MEGA_KO) achieve(ACH_MEGA_KO, &quit);
  if (n >= 10) achieve(ACH_BOSS, &quit);
  if (n >= 50) achieve(ACH_B50, &quit);
  if (n >= 150) achieve(ACH_B150, &quit);
  if (n >= 100) {
    achieve(ACH_CHAMPION, &quit);
    if (run.flags & RUN_NUZLOCKE) achieve(ACH_NUZLOCKE, &quit);
    if (run_difficulty(run.flags) == DIFF_HARD) achieve(ACH_HARD, &quit);
  }
  if (b->raid) achieve(ACH_RAID, &quit);
  if (run_stats.crits >= 50) achieve(ACH_CRITS, &quit);
  if (run_stats.damage >= 20000) achieve(ACH_DAMAGE, &quit);
  for (int k = 0; k < NUM_TYPES; k++)
    if (b->side[SIDE_PLAYER].resonance[k] == 3) achieve(ACH_RESONANCE3, &quit);
}

/* La musique du combat n contre ce dresseur. */
static int battle_music(const Trainer *t, uint16_t n) {
  if (t->wild) return MUSIC_LEGEND;
  if (!t->boss) return MUSIC_TRAINER;
  if (n == 90) return MUSIC_RIVAL;
  if (n >= 96 && n <= 99) return MUSIC_ELITE;
  if (n == 100) return MUSIC_CHAMPION;
  return n > 100 ? MUSIC_KANTO_GYM : MUSIC_GYM;
}

void play_run(uint32_t seed) {
  static Battle battle;
  static Trainer trainer;
  rng = seed ? seed : 1;
  quit = false;
  dex_load();
  settings_apply();
  Run saved;
  RunStats saved_stats;
  bool have = save_read(&saved, &saved_stats);
  int c;
  for (;;) {
    pc_music(MUSIC_TITLE);
    c = screen_title(have ? &saved : NULL, &quit);
    if (quit || c < 0 || c == 5) return; /* 5 : QUITTER (PC) */
    if (c == 0) break;
    if (c == 1 && new_game(have)) break;
    if (c == 2) screen_dex(&quit);
    if (c == 3) screen_achievements(&quit);
    if (c == 4) screen_settings(&quit);
    if (quit) return;
  }
  if (c == 0) run = saved, run_stats = saved_stats, rng = run.seed ? run.seed : rng;
  trainer_difficulty = run_difficulty(run.flags) % 3;
  trainer_make(&trainer, run.battle, &run.team, &rng);

  while (!quit) {
    uint16_t n = run.battle;
    /* duo (format VGC) : on voit l'équipe adverse et on choisit ses 4 ; l'adversaire choisit les siens */
    Team mine = run.team, theirs = trainer.team;
    uint8_t pick[TEAM_MAX];
    for (int i = 0; i < TEAM_MAX; i++) pick[i] = (uint8_t)i;
    bool duo = !dex.singles && !trainer.wild && run.team.count >= 2 && trainer.team.count >= 2;
    pc_music(battle_music(&trainer, n));
    if (duo) {
      int count = screen_preview(&run.team, &trainer, pick, &quit);
      if (quit || !count) return;
      mine.count = (uint8_t)count;
      for (int k = 0; k < count; k++) mine.mon[k] = run.team.mon[pick[k]];
      trainer_bring(&trainer, &mine, 4, &theirs);
    }
    battle_start_ex(&battle, &mine, &theirs, trainer.wild ? NULL : trainer.name, rng_next(&rng),
                    (run.flags & RUN_MEGA) != 0, trainer.mega, duo);
    battle.foe_skill = trainer.skill;
    uint8_t result = fight_run(&battle, &trainer, n);
    if (result == BATTLE_ONGOING) return;
    if (result == BATTLE_LOST) {
      pc_music(MUSIC_NONE);
      dex_save();
      if ((run.flags & RUN_NUZLOCKE) || dex.checkpoint == CHECKPOINT_NEVER) { /* Nuzlocke, ou sans palier : c'est fini */
        hall_of_fame(&run, &run_stats, L("FIN DE LA PARTIE", "GAME OVER"), &quit);
        run.team.count = 0;
        save_all(); /* une équipe vide : plus de partie à continuer */
        return;
      }
      journal(dex.checkpoint == CHECKPOINT_EACH ? L("Défaite... on rejoue ce combat.", "Defeat... let's replay this battle.")
                                                : L("Défaite... retour au palier.", "Defeat... back to the checkpoint."));
      if (!save_read(&run, &run_stats)) return;
      rng ^= rng_next(&rng) + n; /* d'autres dresseurs cette fois */
      trainer_make(&trainer, run.battle, &run.team, &rng);
      continue;
    }
    if (run.flags & RUN_NUZLOCKE) bury(&battle, pick);
    if (n == 10 && !(run.flags & RUN_MEGA)) {
      run.flags |= RUN_MEGA;
      menu_message(L("Vous recevez le MÉGA-ANNEAU !\n{ALT} : Méga-Évolution", "You received the MEGA RING!\n{ALT}: Mega Evolution"), &quit);
    }
    check_achievements(n, &battle);
    if (!trainer.wild) take_loot(&trainer, n);
    if (n == 100) {
      run.flags |= RUN_CHAMPION_BEATEN, dex.champ++;
      pc_music(MUSIC_ENDING);
      ending(&run.team, &quit);
      pc_music(MUSIC_HALL_OF_FAME);
      hall_of_fame(&run, &run_stats, "HALL OF FAME", &quit);
    }
    if (quit) return;
    int bonus = trainer.wild ? trainer.team.mon[0].species : -1;
    trainer_make(&trainer, (uint16_t)(n + 1), &run.team, &rng); /* le prochain : son indice guide le draft */
    draft(n, bonus, &trainer);
    if (quit) return;
    run.battle = (uint16_t)(n + 1);
    /* checkpoint : on repartira d'ici en cas de défaite (tous les 10 combats, ou à chaque combat). Sans
     * palier, la partie est gardée à chaque combat pour la reprendre, mais une défaite la termine. */
    if (n % 10 == 0 || dex.checkpoint != CHECKPOINT_10) {
      run.seed = rng;
      save_all();
    }
    if (n % 10 == 0)
      journal(n == 100 ? L("Vous êtes le MAÎTRE !", "You are the CHAMPION!")
              : dex.checkpoint == CHECKPOINT_10 ? L("Palier sauvegardé.", "Checkpoint saved.")
                                                : L("Partie sauvegardée.", "Game saved."));
  }
}
