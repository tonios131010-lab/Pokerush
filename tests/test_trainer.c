/* Tests des dresseurs et de l'IA (make test) : équipes générées valides à tous les stades de la
 * partie, combats fixes (starter, Champions, Conseil 4, Maître), méga-gemmes, butin, et une IA
 * experte qui bat nettement une IA débutante à équipes égales. */
#include <stdio.h>
#include <string.h>
#include "assets.h"
#include "core/rng.h"
#include "data.h"
#include "game/ai.h"
#include "game/battle.h"
#include "game/trainer.h"

static int failures;
#define CHECK(c)                                                        \
  do {                                                                  \
    if (!(c)) printf("ECHEC ligne %d : %s\n", __LINE__, #c), failures++; \
  } while (0)

static bool learns(uint16_t sp, uint16_t move) {
  const SpeciesInfo *s = &species_info[sp];
  for (uint8_t i = 0; i < s->learn_cnt; i++)
    if (species_learnset(s, i) == move) return true;
  return false;
}

static bool is_type(uint16_t sp, uint8_t t) { return species_info[sp].type1 == t || species_info[sp].type2 == t; }

static void test_teams(void) {
  uint32_t rng = 99;
  static Trainer t;
  int bad_team = 0, bad_move = 0, early_legend = 0, bad_leader = 0, bad_champion = 0, no_attack = 0, bad_mega = 0;
  int megas = 0, shinies = 0, mons = 0;
  for (int seed = 0; seed < 40; seed++)
    for (uint16_t n = 1; n <= 120; n++) {
      trainer_make(&t, n, NULL, &rng);
      if (!t.team.count || t.team.count > TEAM_MAX || !t.name[0] || strlen(t.name) >= sizeof t.name) bad_team++;
      bad_team += t.pic >= TRAINER_PIC_COUNT;
      bool holds_stone = false;
      for (int i = 0; i < t.team.count; i++) {
        const Pokemon *p = &t.team.mon[i];
        bool attack = false;
        mons++, shinies += p->shiny;
        for (int j = 0; j < i; j++) bad_team += t.team.mon[j].species == p->species;
        if (p->species >= NUM_BASE_SPECIES || p->item >= NUM_ITEMS || p->nature >= NUM_NATURES) bad_team++;
        if (p->species == SPECIES_CLAUDE_CODE) bad_team++; /* le POKéMON secret n'est que dans le draft */
        for (uint8_t k = 0; k < MOVES_MAX; k++) {
          uint16_t mv = pokemon_move(p, k);
          if (mv && !learns(p->species, mv)) bad_move++;
          attack |= mv && !move_is_status(&move_info[mv]);
        }
        no_attack += !attack;
        if (item_info[p->item].effect == ITEM_EFFECT_MEGA_STONE) {
          holds_stone = true;
          bad_mega += mega_form(p->species, p->item) < 0 || !t.mega;
        }
        if (n < 50 && !t.wild && species_info[p->species].legendary) early_legend++;
        bool leader = (n % 10 == 0 && n <= 80) || (n >= 96 && n <= 99);
        if (leader && !is_type(p->species, t.type) && !species_info[p->species].legendary) { if (bad_leader++ < 5) printf("  %d %s type %d : %s\n", n, t.name, t.type, species_names[p->species]); }
        if (n == 100 && !species_info[p->species].legendary) bad_champion++;
      }
      if (t.mega && !holds_stone) bad_mega++;
      megas += t.mega;
      if (n == 100 && t.team.count != 6) bad_champion++;
    }
  printf("dresseurs : %d équipes invalides, %d attaques non apprises, %d sans attaque, %d légendaires trop tôt, "
         "%d hors type chez les Champions, %d défauts du Maître, %d méga-gemmes fausses (%d dresseurs méga), "
         "%d chromatiques sur %d\n",
         bad_team, bad_move, no_attack, early_legend, bad_leader, bad_champion, bad_mega, megas, shinies, mons);
  CHECK(!bad_team && !bad_move && !no_attack && !early_legend && !bad_leader && !bad_champion && !bad_mega && megas > 100);

  /* combat 1 : le starter que celui du joueur bat, pour les 27 starters */
  for (int i = 0; i < STARTER_COUNT; i++) {
    Team me;
    memset(&me, 0, sizeof me);
    me.count = 1, me.mon[0].species = starter_species[i];
    trainer_make(&t, 1, &me, &rng);
    const SpeciesInfo *mine = &species_info[starter_species[i]], *theirs = &species_info[t.team.mon[0].species];
    CHECK(t.team.count == 1 && type_effect((uint8_t)mine->type1, (uint8_t)theirs->type1) == EFF_SUPER);
  }
  trainer_make(&t, 100, NULL, &rng);
  printf("combat 100 : %s, ", t.name);
  for (int i = 0; i < t.team.count; i++) printf("%s%s", species_names[t.team.mon[i].species], i + 1 < t.team.count ? ", " : "\n");

  /* butin : une méga-gemme lâchée va toujours à un Pokémon de l'équipe */
  Team me;
  memset(&me, 0, sizeof me);
  me.count = 2, me.mon[0].species = SPECIES_GARDEVOIR, me.mon[1].species = SPECIES_SNORLAX;
  trainer_make(&t, 20, &me, &rng);
  int stones = 0, wrong = 0;
  for (int k = 0; k < 400; k++) {
    uint8_t it = trainer_loot(&t, 20, &me, true, &rng);
    if (item_info[it].effect == ITEM_EFFECT_MEGA_STONE) stones++, wrong += mega_form(SPECIES_GARDEVOIR, it) < 0;
    CHECK(trainer_loot(&t, 20, &me, false, &rng) == ITEM_NONE ||
          item_info[trainer_loot(&t, 20, &me, false, &rng)].effect != ITEM_EFFECT_MEGA_STONE);
  }
  CHECK(stones > 100 && wrong < stones / 2); /* surtout celles de l'équipe */
}

/* La même équipe des deux côtés : l'IA de niveau `a` contre celle de niveau `b`. */
static int duel(uint8_t skill_a, uint8_t skill_b, int battles) {
  static Battle bt;
  static Trainer t;
  uint32_t rng = 4242;
  int wins = 0;
  for (int n = 0; n < battles; n++) {
    trainer_make(&t, (uint16_t)(30 + n % 40), NULL, &rng);
    battle_start(&bt, &t.team, &t.team, "X", rng_next(&rng), t.mega, t.mega);
    for (int turn = 0; turn < 500 && !bt.result; turn++) {
      for (int s = 0; s < 2; s++)
        if (battle_must_replace(&bt, s)) battle_send(&bt, s, ai_replacement(&bt, s));
      if (bt.result) break;
      bt.foe_skill = skill_a;
      Action a = ai_choose(&bt, SIDE_PLAYER);
      bt.foe_skill = skill_b;
      Action c = ai_choose(&bt, SIDE_FOE);
      battle_turn(&bt, a, c);
    }
    wins += bt.result == BATTLE_WON;
  }
  return wins;
}

int main(void) {
  test_teams();
  int expert = duel(10, 0, 400), mirror = duel(10, 10, 400);
  printf("IA experte contre IA débutante : %d victoires sur 400 ; experte contre experte : %d sur 400\n", expert, mirror);
  CHECK(expert > 260);
  printf(failures ? "%d test(s) en échec\n" : "Tous les tests de dresseurs passent.\n", failures);
  return failures != 0;
}
