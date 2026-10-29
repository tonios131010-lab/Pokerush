/* Tests du moteur de combat (make test) : dégâts calculés à la main, scénarios précis (priorité,
 * statuts, talents, Méga-Évolution, pièges, Distorsion, relais, Résonance), et des milliers de
 * combats aléatoires complets qui doivent tous se terminer sans incohérence. */
#include <stdio.h>
#include <string.h>
#include "core/rng.h"
#include "data.h"
#include "game/battle.h"

static int failures;
#define CHECK(c)                                                        \
  do {                                                                  \
    if (!(c)) printf("ECHEC ligne %d : %s\n", __LINE__, #c), failures++; \
  } while (0)

static Battle b;

static Pokemon mk(uint16_t species, uint16_t m0, uint16_t m1, uint16_t m2, uint16_t m3, uint8_t item) {
  Pokemon p;
  memset(&p, 0, sizeof p);
  p.species = species, p.nature = 12, p.item = item;
  pokemon_set_move(&p, 0, m0), pokemon_set_move(&p, 1, m1), pokemon_set_move(&p, 2, m2), pokemon_set_move(&p, 3, m3);
  return p;
}

static Team team1(Pokemon p) {
  Team t;
  memset(&t, 0, sizeof t);
  t.mon[0] = p, t.count = 1;
  return t;
}

static bool said(const char *needle) {
  for (int i = 0; i < b.nevents; i++)
    if (b.events[i].kind == EV_TEXT && strstr(b.events[i].text, needle)) return true;
  return false;
}

static int said_at(const char *needle) {
  for (int i = 0; i < b.nevents; i++)
    if (b.events[i].kind == EV_TEXT && strstr(b.events[i].text, needle)) return i;
  return -1;
}

static void dump(void) {
  for (int i = 0; i < b.nevents; i++)
    if (b.events[i].kind == EV_TEXT) printf("    %s\n", b.events[i].text);
}

static Action use(uint8_t slot) { return (Action){ACT_MOVE, slot, 0, false, 1}; }
static void start(const Team *a, const Team *c, uint32_t seed) { battle_start(&b, a, c, "Le DRESSEUR", seed, false, false); }

static void test_damage(void) {
  Team me = team1(mk(SPECIES_CHARIZARD, MOVE_FLAMETHROWER, MOVE_EARTHQUAKE, 0, 0, 0));
  Team foe = team1(mk(SPECIES_VENUSAUR, MOVE_GIGA_DRAIN, 0, 0, 0, 0));
  start(&me, &foe, 1);
  uint8_t eff;
  /* (22 × 90 × 114 / 105) / 50 + 2 = 44 ; ×1,5 (même type) ; ×2 (Feu contre Plante) */
  CHECK(battle_estimate(&b, SIDE_PLAYER, MOVE_FLAMETHROWER, 100, &eff) == 132 && eff == 32);
  CHECK(battle_estimate(&b, SIDE_PLAYER, MOVE_FLAMETHROWER, 85, &eff) == 110);
  CHECK(said("Le DRESSEUR veut") && said("envoie\nFLORIZARRE ennemi") && said("Vas-y ! DRACAUFEU !"));

  Team bird = team1(mk(SPECIES_ZAPDOS, MOVE_THUNDERBOLT, 0, 0, 0, 0));
  start(&me, &bird, 1);
  CHECK(battle_estimate(&b, SIDE_PLAYER, MOVE_EARTHQUAKE, 100, &eff) == 0 && eff == 0);
  battle_turn(&b, use(1), use(0));
  CHECK(said("Ça n'affecte pas"));

  /* Fée : Dracolosse (Dragon) ne touche pas Mélodelfe ; Pouvoir Lunaire est super efficace */
  Team drag = team1(mk(SPECIES_DRAGONITE, MOVE_OUTRAGE, 0, 0, 0, 0));
  Team fairy = team1(mk(SPECIES_CLEFABLE, MOVE_MOONBLAST, 0, 0, 0, 0));
  start(&drag, &fairy, 2);
  CHECK(battle_estimate(&b, SIDE_PLAYER, MOVE_OUTRAGE, 100, &eff) == 0 && eff == 0);
  CHECK(battle_estimate(&b, SIDE_FOE, MOVE_MOONBLAST, 100, &eff) > 0 && eff == 32);
}

static void test_priority_and_protect(void) {
  /* Arcanin (Vit 95) contre Mewtwo (Vit 130) : Vit.Extrême passe en premier */
  Team me = team1(mk(SPECIES_ARCANINE, MOVE_EXTREME_SPEED, 0, 0, 0, 0));
  Team foe = team1(mk(SPECIES_MEWTWO, MOVE_PROTECT, MOVE_CALM_MIND, 0, 0, 0)); /* sans dégâts : pas de K.O. au hasard */
  start(&me, &foe, 7);
  battle_turn(&b, use(0), use(1));
  int first_arcanine = said_at("ARCANIN utilise"), first_mewtwo = said_at("MEWTWO ennemi utilise");
  CHECK(first_arcanine >= 0 && first_mewtwo > first_arcanine);
  uint16_t before = battle_active(&b, SIDE_FOE)->hp;
  battle_turn(&b, use(0), use(0)); /* Abri (+4) passe avant et bloque */
  CHECK(said("MEWTWO ennemi se protège") && battle_active(&b, SIDE_FOE)->hp == before);
}

static void test_trick_room(void) {
  /* Distorsion : le plus lent agit en premier pendant 5 tours */
  Team slow = team1(mk(SPECIES_SNORLAX, MOVE_TRICK_ROOM, MOVE_BODY_SLAM, 0, 0, 0));
  Team fast = team1(mk(SPECIES_JOLTEON, MOVE_CALM_MIND, 0, 0, 0, 0));
  start(&slow, &fast, 3);
  battle_turn(&b, use(0), use(0));
  CHECK(b.trick_room == 4 && said("tord"));
  battle_turn(&b, use(1), use(0));
  CHECK(said_at("RONFLEX utilise") >= 0 && said_at("RONFLEX utilise") < said_at("VOLTALI ennemi utilise"));
}

static void test_status(void) {
  /* Cage-Éclair contre un type Sol, Toxik contre un type Acier : sans effet */
  Team me = team1(mk(SPECIES_RAICHU, MOVE_THUNDER_WAVE, MOVE_TOXIC, 0, 0, 0));
  Team ground = team1(mk(SPECIES_EXCADRILL, MOVE_ROAR, 0, 0, 0, 0));
  start(&me, &ground, 3);
  battle_turn(&b, use(0), use(0));
  CHECK(battle_active(&b, SIDE_FOE)->status == STATUS_NONE && said("Ça n'affecte pas"));
  battle_turn(&b, use(1), use(0));
  CHECK(battle_active(&b, SIDE_FOE)->status == STATUS_NONE);
  /* Spore endort à coup sûr ; pas les types Plante */
  Team shroom = team1(mk(SPECIES_BRELOOM, MOVE_SPORE, 0, 0, 0, 0));
  Team target = team1(mk(SPECIES_SNORLAX, MOVE_BODY_SLAM, 0, 0, 0, 0));
  start(&shroom, &target, 5);
  battle_turn(&b, use(0), use(0));
  if (battle_active(&b, SIDE_FOE)->status != STATUS_SLEEP) dump();
  CHECK(battle_active(&b, SIDE_FOE)->status == STATUS_SLEEP && said("s'endort"));
  bool status_event = false;
  for (int i = 0; i < b.nevents; i++) status_event |= b.events[i].kind == EV_STATUS && b.events[i].value == STATUS_SLEEP;
  CHECK(status_event);
  Team grass = team1(mk(SPECIES_VENUSAUR, MOVE_GIGA_DRAIN, 0, 0, 0, 0));
  start(&shroom, &grass, 5);
  battle_turn(&b, use(0), use(0));
  CHECK(battle_active(&b, SIDE_FOE)->status == STATUS_NONE);
}

static void test_abilities_items(void) {
  /* Intimidation à l'entrée, Crachin (5 tours), Restes */
  Team me = team1(mk(SPECIES_GYARADOS, MOVE_DRAGON_DANCE, 0, 0, 0, ITEM_LEFTOVERS));
  Team foe = team1(mk(SPECIES_KYOGRE, MOVE_CALM_MIND, 0, 0, 0, 0));
  start(&me, &foe, 9);
  CHECK(b.side[SIDE_FOE].v[0].stage[STAGE_ATK] == -1 && said("INTIMIDATION"));
  CHECK(b.weather == WEATHER_RAIN && b.weather_turns == 5 && said("pleuvoir"));
  battle_turn(&b, use(0), use(0));
  CHECK(b.side[SIDE_PLAYER].v[0].stage[STAGE_ATK] == 1 && b.side[SIDE_PLAYER].v[0].stage[STAGE_SPE] == 1);
  CHECK(b.side[SIDE_FOE].v[0].stage[STAGE_SPA] == 1 && said("augmente"));
  /* Ceinture Force : survit à 1 PV depuis ses PV max */
  Team sd = team1(mk(SPECIES_TYRANITAR, MOVE_CRUNCH, 0, 0, 0, 0));
  Team sash = team1(mk(SPECIES_ALAKAZAM, MOVE_CALM_MIND, 0, 0, 0, ITEM_FOCUS_SASH));
  start(&sd, &sash, 11);
  for (int t = 0; t < 4 && !b.result; t++) battle_turn(&b, use(0), use(0));
  CHECK(said("tient bon") && battle_active(&b, SIDE_FOE)->p.item == ITEM_NONE);
  /* Lévitation : le Sol ne touche pas ; Absorb Eau soigne */
  Team quake = team1(mk(SPECIES_GARCHOMP, MOVE_EARTHQUAKE, MOVE_SURF, 0, 0, 0));
  Team ghost = team1(mk(SPECIES_GENGAR, MOVE_SHADOW_BALL, 0, 0, 0, 0));
  start(&quake, &ghost, 13);
  uint8_t eff;
  CHECK(battle_estimate(&b, SIDE_PLAYER, MOVE_EARTHQUAKE, 100, &eff) > 0); /* Ectoplasma n'a plus Lévitation */
  Team lati = team1(mk(SPECIES_LATIOS, MOVE_PSYCHIC, 0, 0, 0, 0));
  start(&quake, &lati, 13);
  battle_turn(&b, use(0), use(0));
  CHECK(said("Ça n'affecte pas") && battle_active(&b, SIDE_FOE)->hp == battle_active(&b, SIDE_FOE)->max_hp);
}

static void test_mega(void) {
  Team me = team1(mk(SPECIES_CHARIZARD, MOVE_FLARE_BLITZ, 0, 0, 0, ITEM_CHARIZARDITE_X));
  Team foe = team1(mk(SPECIES_SNORLAX, MOVE_AMNESIA, 0, 0, 0, 0));
  battle_start(&b, &me, &foe, "X", 21, false, false);
  CHECK(battle_can_mega(&b, SIDE_PLAYER) < 0); /* pas de Méga-Anneau */
  battle_start(&b, &me, &foe, "X", 21, true, false);
  int x = battle_can_mega(&b, SIDE_PLAYER);
  CHECK(x == mega_form(SPECIES_CHARIZARD, ITEM_CHARIZARDITE_X));
  Action mega = {ACT_MOVE, 0, 0, true, 1};
  battle_turn(&b, mega, use(0));
  const Fighter *f = battle_active(&b, SIDE_PLAYER);
  CHECK(f->p.species == x && f->ability == ABILITY_TOUGH_CLAWS && b.side[SIDE_PLAYER].v[0].type[1] == TYPE_DRAGON);
  CHECK(said("méga-évolue") && said_at("méga-évolue") < said_at("utilise"));
  CHECK(b.side[SIDE_PLAYER].mega_used && battle_can_mega(&b, SIDE_PLAYER) < 0);
  bool ev = false;
  for (int i = 0; i < b.nevents; i++) ev |= b.events[i].kind == EV_MEGA && b.events[i].value == x;
  CHECK(ev);
  CHECK(f->stat[STAT_ATK] == species_stat(&species_info[x], 12, STAT_ATK) && f->max_hp == 138);
}

static void test_hazards_and_pivot(void) {
  /* Piège de Roc : Dracaufeu (×4 Roche) perd la moitié de ses PV en entrant */
  Team setter = team1(mk(SPECIES_GARGANACL, MOVE_STEALTH_ROCK, MOVE_ROAR, 0, 0, 0));
  Team two;
  memset(&two, 0, sizeof two);
  two.mon[0] = mk(SPECIES_SNORLAX, MOVE_AMNESIA, 0, 0, 0, 0);
  two.mon[1] = mk(SPECIES_CHARIZARD, MOVE_FLAMETHROWER, 0, 0, 0, 0);
  two.count = 2;
  start(&setter, &two, 17);
  battle_turn(&b, use(0), use(0));
  CHECK(b.side[SIDE_FOE].stealth_rock && said("pierres"));
  battle_turn(&b, use(1), use(0)); /* Hurlement : Dracaufeu entre et prend les pierres */
  const Fighter *z = &b.side[SIDE_FOE].team[1];
  CHECK(b.side[SIDE_FOE].active[0] == 1 && z->hp == z->max_hp - z->max_hp / 2);
  /* Demi-Tour : frappe puis laisse la place */
  Team pivot;
  memset(&pivot, 0, sizeof pivot);
  pivot.mon[0] = mk(SPECIES_SCIZOR, MOVE_U_TURN, 0, 0, 0, 0);
  pivot.mon[1] = mk(SPECIES_BLISSEY, MOVE_SOFT_BOILED, 0, 0, 0, 0);
  pivot.count = 2;
  Team wall = team1(mk(SPECIES_SNORLAX, MOVE_AMNESIA, 0, 0, 0, 0));
  start(&pivot, &wall, 19);
  Action u = {ACT_MOVE, 0, 1, false, 1};
  battle_turn(&b, u, use(0));
  CHECK(b.side[SIDE_PLAYER].active[0] == 1 && said("reviens") && battle_active(&b, SIDE_FOE)->hp < battle_active(&b, SIDE_FOE)->max_hp);
}

static void test_resonance(void) {
  Team water, plain;
  memset(&water, 0, sizeof water);
  uint8_t sp[4] = {SPECIES_BLASTOISE, SPECIES_GYARADOS, SPECIES_VAPOREON, SPECIES_CHARIZARD};
  for (int i = 0; i < 4; i++) water.mon[i] = mk(sp[i], MOVE_SURF, 0, 0, 0, 0);
  water.count = 4;
  uint8_t lv[NUM_TYPES];
  resonance_levels(&water, lv);
  CHECK(lv[TYPE_WATER] == 1 && lv[TYPE_FIRE] == 0 && lv[TYPE_FLYING] == 0);
  water.mon[3] = mk(SPECIES_SWAMPERT, MOVE_SURF, 0, 0, 0, 0);
  resonance_levels(&water, lv);
  CHECK(lv[TYPE_WATER] == 2);
  (void)plain;
  /* Résonance Eau : soin à chaque tour */
  for (int i = 0; i < 4; i++) pokemon_set_move(&water.mon[i], 0, MOVE_CALM_MIND);
  Team target = team1(mk(SPECIES_SNORLAX, MOVE_AMNESIA, 0, 0, 0, 0));
  start(&water, &target, 23);
  CHECK(said("EAU II : SOURCE"));
  Fighter *f = battle_active(&b, SIDE_PLAYER);
  f->hp = (uint16_t)(f->max_hp / 2);
  uint16_t before = f->hp;
  battle_turn(&b, use(0), use(0));
  CHECK(said("RÉSONANCE EAU") && f->hp == before + f->max_hp / 32 * 2);
}

static void test_terrain_and_new(void) {
  /* Créa-Élec à l'entrée : Champ Électrifié, attaques Électrik +30 %, pas de sommeil au sol */
  Team koko = team1(mk(SPECIES_TAPU_KOKO, MOVE_THUNDERBOLT, 0, 0, 0, 0));
  Team target = team1(mk(SPECIES_SNORLAX, MOVE_SING, 0, 0, 0, 0));
  start(&koko, &target, 31);
  CHECK(b.terrain == TERRAIN_ELECTRIC && said("électricité"));
  uint8_t eff;
  uint16_t boosted = battle_estimate(&b, SIDE_PLAYER, MOVE_THUNDERBOLT, 100, &eff);
  b.terrain = TERRAIN_NONE;
  uint16_t plain = battle_estimate(&b, SIDE_PLAYER, MOVE_THUNDERBOLT, 100, &eff);
  CHECK(boosted > plain);
  /* Absentéisme : Monaflèmit n'agit qu'un tour sur deux */
  Team slaking = team1(mk(SPECIES_SLAKING, MOVE_BULK_UP, 0, 0, 0, 0));
  Team calm = team1(mk(SPECIES_SNORLAX, MOVE_AMNESIA, 0, 0, 0, 0));
  start(&slaking, &calm, 33);
  battle_turn(&b, use(0), use(0));
  battle_turn(&b, use(0), use(0));
  CHECK(said("paresse") && b.side[SIDE_PLAYER].v[0].stage[STAGE_ATK] == 1);
  /* Écho-Prisme : le porteur compte double pour les Résonances */
  Team t;
  memset(&t, 0, sizeof t);
  t.mon[0] = mk(SPECIES_BLASTOISE, MOVE_SURF, 0, 0, 0, ITEM_RESONANCE_PRISM);
  t.mon[1] = mk(SPECIES_VAPOREON, MOVE_SURF, 0, 0, 0, 0);
  t.count = 2;
  uint8_t lv[NUM_TYPES];
  resonance_levels(&t, lv);
  CHECK(lv[TYPE_WATER] == 1);
  /* légendaire sauvage : pas de dresseur dans les messages */
  battle_start(&b, &koko, &target, NULL, 35, false, false);
  CHECK(said("géant") && !said("veut") && b.raid && battle_active(&b, SIDE_FOE)->max_hp > 400);
}

/* ------------------------------------------------------------------------- combats aléatoires */

static uint32_t fuzz_rng = 12345;

static Team random_team(void) {
  Team t;
  memset(&t, 0, sizeof t);
  t.count = (uint8_t)(1 + rng_below(&fuzz_rng, TEAM_MAX));
  for (int i = 0; i < t.count; i++) {
    Pokemon *p = &t.mon[i];
    p->species = (uint16_t)rng_below(&fuzz_rng, NUM_BASE_SPECIES);
    p->nature = (uint8_t)rng_below(&fuzz_rng, NUM_NATURES);
    p->ability = (uint8_t)rng_below(&fuzz_rng, 2);
    p->item = (uint8_t)rng_below(&fuzz_rng, NUM_ITEMS);
    const SpeciesInfo *s = &species_info[p->species];
    for (uint8_t k = 0; k < MOVES_MAX; k++) pokemon_set_move(p, k, species_learnset(s, (uint8_t)rng_below(&fuzz_rng, s->learn_cnt)));
  }
  return t;
}

static Action random_action(int s) {
  Side *sd = &b.side[s & 1];
  if (!battle_forced_move(&b, s) && rng_percent(&fuzz_rng, 10)) {
    uint8_t i = (uint8_t)rng_below(&fuzz_rng, sd->count);
    if (battle_can_switch(&b, s, i)) return (Action){ACT_SWITCH, i, 0, false, 0};
  }
  uint8_t slots[MOVES_MAX], n = 0;
  for (uint8_t k = 0; k < MOVES_MAX; k++)
    if (battle_can_move(&b, s, k)) slots[n++] = k;
  return (Action){ACT_MOVE, n ? slots[rng_below(&fuzz_rng, n)] : 0, (uint8_t)rng_below(&fuzz_rng, sd->count),
                  rng_percent(&fuzz_rng, 50), (uint8_t)rng_below(&fuzz_rng, 4)};
}

static bool sane(void) {
  for (int s = 0; s < 2; s++) {
    if (b.side[s].active[0] >= b.side[s].count) return false;
    for (int i = 0; i < b.side[s].count; i++) {
      const Fighter *f = &b.side[s].team[i];
      if (f->hp > f->max_hp || f->p.species >= NUM_SPECIES) return false;
      for (int k = 0; k < NUM_STAGES; k++)
        if (b.side[s].v[0].stage[k] < -6 || b.side[s].v[0].stage[k] > 6) return false;
    }
  }
  return b.nevents <= BATTLE_EVENTS;
}

static void test_fuzz(int battles) {
  int unfinished = 0, insane = 0, full_log = 0, megas = 0;
  long turns = 0;
  for (int n = 0; n < battles; n++) {
    Team a = random_team(), c = random_team();
    /* une méga-gemme utile de temps en temps */
    for (int i = 0; i < a.count; i++)
      for (int m = NUM_BASE_SPECIES; m < NUM_SPECIES && rng_percent(&fuzz_rng, 30); m++)
        if (species_info[m].mega_base == a.mon[i].species) a.mon[i].item = species_info[m].stone;
    battle_start(&b, &a, &c, "Le DRESSEUR", rng_next(&fuzz_rng), true, true);
    int t = 0;
    while (!b.result && t++ < 1000) {
      for (int s = 0; s < 2; s++)
        while (battle_must_replace(&b, s)) {
          uint8_t i = (uint8_t)rng_below(&fuzz_rng, b.side[s].count);
          if (b.side[s].team[i].hp) battle_send(&b, s, i);
        }
      if (b.result) break;
      battle_turn(&b, random_action(SIDE_PLAYER), random_action(SIDE_FOE));
      if (!sane()) insane++;
      if (b.nevents == BATTLE_EVENTS) full_log++;
    }
    megas += b.side[0].mega_used;
    turns += t;
    if (!b.result) {
      if (unfinished++ < 3) printf("  combat %d sans fin\n", n), dump();
    }
  }
  printf("%d combats aléatoires, %.1f tours en moyenne, %d Méga-Évolutions, %d sans fin, %d incohérents, %d journaux pleins\n",
         battles, (double)turns / battles, megas, unfinished, insane, full_log);
  CHECK(!unfinished && !insane && full_log < battles / 500);
}

/* Les messages, en français puis en anglais, avec les noms les plus longs : aucun n'est coupé par la
 * taille d'un événement (BATTLE_TEXT). */
static void test_message_length(void) {
  for (int l = 0; l < 2; l++) {
    lang = (uint8_t)l;
    int cut = 0, longest = 0;
    for (int n = 0; n < 1500; n++) {
      Team a = random_team(), c = random_team();
      battle_start(&b, &a, &c, l ? "POKéMON BREEDER BASIL" : "TOPDRESSEUSE CÉLIA", rng_next(&fuzz_rng), true, true);
      for (int t = 0; !b.result && t < 200; t++) {
        for (int i = 0; i < b.nevents; i++) {
          int len = (int)strlen(b.events[i].text);
          if (len > longest) longest = len;
          if (b.events[i].kind == EV_TEXT && len >= BATTLE_TEXT - 1) {
            if (cut++ < 3) printf("  coupé : %s\n", b.events[i].text);
          }
        }
        b.nevents = 0;
        for (int s = 0; s < 2; s++)
          while (battle_must_replace(&b, s)) {
            uint8_t i = (uint8_t)rng_below(&fuzz_rng, b.side[s].count);
            if (b.side[s].team[i].hp) battle_send(&b, s, i);
          }
        if (b.result) break;
        battle_turn(&b, random_action(SIDE_PLAYER), random_action(SIDE_FOE));
      }
    }
    printf("messages en %s : le plus long fait %d octets, %d coupés\n", l ? "anglais" : "français", longest, cut);
    CHECK(!cut);
  }
  lang = 0;
}

/* Formes à objet, nouvelles attaques et nouveaux objets. */
static void test_forms(void) {
  /* Zacian et l'Épée Rouillée : forme Épée Suprême dès l'entrée, sans Méga-Anneau */
  Team me = team1(mk(SPECIES_ZACIAN_HERO, MOVE_PLAY_ROUGH, 0, 0, 0, ITEM_RUSTED_SWORD));
  Team foe = team1(mk(SPECIES_SNORLAX, MOVE_AMNESIA, 0, 0, 0, 0));
  start(&me, &foe, 5);
  const Fighter *f = battle_active(&b, SIDE_PLAYER);
  int crowned = item_form(SPECIES_ZACIAN_HERO, ITEM_RUSTED_SWORD);
  CHECK(f->p.species == crowned && b.side[SIDE_PLAYER].v[0].type[1] == TYPE_STEEL && said("change\nde forme"));
  CHECK(f->stat[STAT_ATK] == species_stat(&species_info[crowned], 12, STAT_ATK) && !b.side[SIDE_PLAYER].mega_used);
  bool ev = false;
  for (int i = 0; i < b.nevents; i++) ev |= b.events[i].kind == EV_FORM && b.events[i].value == crowned;
  CHECK(ev);

  /* Arceus et la Plaque Flamme : type Feu, Jugement de type Feu */
  Team arceus = team1(mk(SPECIES_ARCEUS_NORMAL, MOVE_JUDGMENT, 0, 0, 0, ITEM_FLAME_PLATE));
  Team grass = team1(mk(SPECIES_VENUSAUR, MOVE_GIGA_DRAIN, 0, 0, 0, 0));
  start(&arceus, &grass, 5);
  uint8_t eff;
  battle_estimate(&b, SIDE_PLAYER, MOVE_JUDGMENT, 100, &eff);
  CHECK(eff == 32 && b.side[SIDE_PLAYER].v[0].type[0] == TYPE_FIRE);

  /* Croc Fatal : la moitié des PV restants */
  Team fang = team1(mk(SPECIES_RATICATE_ALOLA, MOVE_SUPER_FANG, 0, 0, 0, 0));
  start(&fang, &foe, 5);
  CHECK(battle_estimate(&b, SIDE_PLAYER, MOVE_SUPER_FANG, 100, &eff) == battle_active(&b, SIDE_FOE)->hp / 2);

  /* Baie Chocco : un coup Feu super efficace divisé par deux, une fois */
  Team fire = team1(mk(SPECIES_CHARIZARD, MOVE_FLAMETHROWER, 0, 0, 0, 0));
  start(&fire, &grass, 5);
  uint16_t full = battle_estimate(&b, SIDE_PLAYER, MOVE_FLAMETHROWER, 100, &eff);
  Team berry = team1(mk(SPECIES_VENUSAUR, MOVE_GIGA_DRAIN, 0, 0, 0, ITEM_OCCA_BERRY));
  start(&fire, &berry, 5);
  uint16_t half = battle_estimate(&b, SIDE_PLAYER, MOVE_FLAMETHROWER, 100, &eff);
  CHECK(half < full && half >= full / 2 - 2 && half <= full / 2 + 2);
  battle_turn(&b, use(0), use(0));
  CHECK(said("réduit les") && battle_active(&b, SIDE_FOE)->p.item == ITEM_NONE);
}

/* Duo : deux Pokémon par camp, attaques de zone à 75 %, cibles. */
static void test_doubles(void) {
  Team me, foe;
  memset(&me, 0, sizeof me), memset(&foe, 0, sizeof foe);
  me.mon[0] = mk(SPECIES_GARCHOMP, MOVE_EARTHQUAKE, MOVE_ROCK_SLIDE, MOVE_DRAGON_CLAW, 0, 0);
  me.mon[1] = mk(SPECIES_SKARMORY, MOVE_ROOST, 0, 0, 0, 0); /* Vol : le Séisme de son partenaire ne le touche pas */
  me.mon[2] = mk(SPECIES_BLISSEY, MOVE_SOFT_BOILED, 0, 0, 0, 0);
  me.count = 3;
  foe.mon[0] = mk(SPECIES_HEATRAN, MOVE_PROTECT, 0, 0, 0, 0);
  foe.mon[1] = mk(SPECIES_TYRANITAR, MOVE_PROTECT, 0, 0, 0, 0);
  foe.count = 2;
  battle_start_ex(&b, &me, &foe, "X", 3, false, false, true);
  CHECK(b.doubles && battle_positions(&b) == 4 && battle_active(&b, 2)->p.species == SPECIES_SKARMORY &&
        battle_active(&b, 3)->p.species == SPECIES_TYRANITAR);
  uint8_t eff;
  uint16_t one = battle_estimate_at(&b, 0, 1, MOVE_DRAGON_CLAW, 100, &eff);
  CHECK(one > 0 && battle_estimate_at(&b, 0, 3, MOVE_DRAGON_CLAW, 100, &eff) > 0);
  CHECK(move_info[MOVE_EARTHQUAKE].spread == 2 && move_info[MOVE_ROCK_SLIDE].spread == 1 && !move_info[MOVE_DRAGON_CLAW].spread);
  /* Surf : une cible qui dépend de la configuration de l'expansion (B_UPDATED_MOVE_DATA), évaluée */
  CHECK(move_info[MOVE_SURF].spread == 2 && move_info[MOVE_DISCHARGE].spread == 2 && move_info[MOVE_HEAT_WAVE].spread == 1);
  /* Séisme : les deux adversaires perdent des PV, le partenaire Vol est épargné */
  Action act[POSITIONS] = {{ACT_MOVE, 0, 0, false, 1}, {ACT_MOVE, 1, 0, false, 0}, {ACT_MOVE, 0, 0, false, 1},
                           {ACT_MOVE, 1, 0, false, 0}};
  uint16_t h1 = battle_active(&b, 1)->hp, h3 = battle_active(&b, 3)->hp;
  battle_turn_all(&b, act);
  CHECK(battle_active(&b, 1)->hp < h1 && battle_active(&b, 3)->hp < h3 && battle_active(&b, 2)->hp == battle_active(&b, 2)->max_hp);
  /* Dracogriffe vise la position choisie */
  Action aim[POSITIONS] = {{ACT_MOVE, 2, 0, false, 3}, {ACT_MOVE, 1, 0, false, 0}, {ACT_MOVE, 0, 0, false, 1},
                           {ACT_MOVE, 1, 0, false, 0}};
  h1 = battle_active(&b, 1)->hp, h3 = battle_active(&b, 3)->hp;
  battle_turn_all(&b, aim);
  CHECK(battle_active(&b, 1)->hp == h1 && (battle_active(&b, 3)->hp < h3 || !battle_active(&b, 3)->hp));
}

static void test_fuzz_doubles(int battles) {
  int unfinished = 0, insane = 0;
  long turns = 0;
  for (int n = 0; n < battles; n++) {
    Team a = random_team(), c = random_team();
    if (a.count < 2) a.count = 2, a.mon[1] = a.mon[0];
    if (c.count < 2) c.count = 2, c.mon[1] = c.mon[0];
    if (a.count > 4) a.count = 4;
    if (c.count > 4) c.count = 4;
    battle_start_ex(&b, &a, &c, "Le DRESSEUR", rng_next(&fuzz_rng), true, true, true);
    int t = 0;
    while (!b.result && t++ < 1000) {
      for (int s = 0; s < POSITIONS; s++)
        for (int guard = 0; battle_must_replace(&b, s) && guard < 50; guard++) {
          uint8_t i = (uint8_t)rng_below(&fuzz_rng, b.side[s & 1].count);
          if (b.side[s & 1].team[i].hp) battle_send(&b, s, i);
        }
      if (b.result) break;
      Action act[POSITIONS];
      for (int p = 0; p < POSITIONS; p++) act[p] = random_action(p);
      battle_turn_all(&b, act);
      for (int s = 0; s < 2; s++)
        for (int k = 0; k < 2; k++) {
          const Side *sd = &b.side[s];
          if (sd->active[k] >= sd->count || (sd->active[0] == sd->active[1])) insane++;
          for (int st = 0; st < NUM_STAGES; st++)
            if (sd->v[k].stage[st] < -6 || sd->v[k].stage[st] > 6) insane++;
        }
    }
    turns += t;
    if (!b.result && unfinished++ < 3) printf("  duo %d sans fin\n", n), dump();
  }
  printf("%d combats en duo, %.1f tours en moyenne, %d sans fin, %d incohérents\n", battles, (double)turns / battles,
         unfinished, insane);
  CHECK(!unfinished && !insane);
}

int main(void) {
  test_damage();
  test_priority_and_protect();
  test_trick_room();
  test_status();
  test_abilities_items();
  test_mega();
  test_hazards_and_pivot();
  test_resonance();
  test_terrain_and_new();
  test_forms();
  test_doubles();
  test_fuzz(5000);
  test_fuzz_doubles(3000);
  test_message_length();
  printf(failures ? "%d test(s) en échec\n" : "Tous les tests de combat passent.\n", failures);
  return failures != 0;
}
