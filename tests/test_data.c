/* Tests des données générées (make test) : table des types avec Fée, espèces et formes Méga,
 * attaques, stats et natures, vérifiés sur des valeurs connues de la 9G. */
#include <stdio.h>
#include <string.h>
#include "data.h"
#include "game/battle.h"

static int failures;
#define CHECK(c)                                                        \
  do {                                                                  \
    if (!(c)) printf("ECHEC ligne %d : %s\n", __LINE__, #c), failures++; \
  } while (0)

static bool learns(uint16_t species, uint16_t move) {
  const SpeciesInfo *s = &species_info[species];
  for (uint8_t i = 0; i < s->learn_cnt; i++)
    if (species_learnset(s, i) == move) return true;
  return false;
}

static void test_types(void) {
  CHECK(type_effect(TYPE_FIRE, TYPE_GRASS) == EFF_SUPER);
  CHECK(type_effect(TYPE_WATER, TYPE_FIRE) == EFF_SUPER);
  CHECK(type_effect(TYPE_FIRE, TYPE_WATER) == EFF_HALF);
  CHECK(type_effect(TYPE_ELECTRIC, TYPE_GROUND) == EFF_IMMUNE);
  CHECK(type_effect(TYPE_GROUND, TYPE_FLYING) == EFF_IMMUNE);
  CHECK(type_effect(TYPE_NORMAL, TYPE_GHOST) == EFF_IMMUNE);
  CHECK(type_effect(TYPE_PSYCHIC, TYPE_DARK) == EFF_IMMUNE);
  CHECK(type_effect(TYPE_POISON, TYPE_STEEL) == EFF_IMMUNE);
  CHECK(type_effect(TYPE_DARK, TYPE_STEEL) == EFF_NORMAL);  /* 6G : l'Acier ne résiste plus aux Ténèbres */
  CHECK(type_effect(TYPE_GHOST, TYPE_STEEL) == EFF_NORMAL); /* ni au Spectre */
  CHECK(type_effect(TYPE_DRAGON, TYPE_FAIRY) == EFF_IMMUNE);
  CHECK(type_effect(TYPE_FAIRY, TYPE_DRAGON) == EFF_SUPER && type_effect(TYPE_FAIRY, TYPE_DARK) == EFF_SUPER);
  CHECK(type_effect(TYPE_STEEL, TYPE_FAIRY) == EFF_SUPER && type_effect(TYPE_POISON, TYPE_FAIRY) == EFF_SUPER);
  CHECK(type_effect(TYPE_FIGHTING, TYPE_FAIRY) == EFF_HALF && type_effect(TYPE_BUG, TYPE_FAIRY) == EFF_HALF);
  CHECK(type_effect(TYPE_FAIRY, TYPE_FIRE) == EFF_HALF);
  CHECK(type_effect(TYPE_MYSTERY, TYPE_GHOST) == EFF_NORMAL);
}

static void test_categories(void) {
  CHECK(move_is_special(&move_info[MOVE_SHADOW_BALL]) && move_is_physical(&move_info[MOVE_SHADOW_CLAW]));
  CHECK(move_is_physical(&move_info[MOVE_CRUNCH]) && move_info[MOVE_CRUNCH].effect == EFFECT_T_DEF_DOWN);
  CHECK(move_is_physical(&move_info[MOVE_FIRE_PUNCH]) && move_is_special(&move_info[MOVE_FLAMETHROWER]));
  CHECK(move_is_special(&move_info[MOVE_MOONBLAST]) && move_info[MOVE_MOONBLAST].type == TYPE_FAIRY);
  CHECK(!move_is_special(&move_info[MOVE_SWORDS_DANCE]) && !move_is_physical(&move_info[MOVE_SWORDS_DANCE]));
  CHECK(!move_info[MOVE_OVERHEAT].contact && move_info[MOVE_CLOSE_COMBAT].contact);
  CHECK(move_info[MOVE_FIRE_PUNCH].punch && move_info[MOVE_CRUNCH].bite && move_info[MOVE_LEAF_BLADE].slice);
  CHECK(move_info[MOVE_DARK_PULSE].pulse && move_info[MOVE_BOOMBURST].sound);
}

static void test_species(void) {
  const SpeciesInfo *z = &species_info[SPECIES_CHARIZARD];
  CHECK(!strcmp(species_names[SPECIES_CHARIZARD], "DRACAUFEU"));
  CHECK(!strcmp(species_names[SPECIES_GHOLDENGO], "GROMAGO"));
  CHECK(z->dex == 6 && z->type1 == TYPE_FIRE && z->type2 == TYPE_FLYING && z->ability0 == ABILITY_BLAZE);
  CHECK(z->base[STAT_HP] == 78 && z->base[STAT_SPA] == 109 && z->base[STAT_SPE] == 100 && !z->legendary);
  CHECK(species_info[SPECIES_MIRAIDON].dex == 1008 && species_info[SPECIES_MIRAIDON].legendary);
  CHECK(species_info[SPECIES_BLISSEY].base[STAT_HP] == 255 && species_info[SPECIES_BLISSEY].ability1 == ABILITY_SERENE_GRACE);
  CHECK(species_info[SPECIES_RAYQUAZA].legendary && species_info[SPECIES_RAYQUAZA].tier == 7);
  CHECK(species_info[SPECIES_CLEFABLE].type1 == TYPE_FAIRY && species_info[SPECIES_CLEFABLE].type2 == TYPE_FAIRY);
  CHECK(learns(SPECIES_CHARIZARD, MOVE_FLAMETHROWER) && learns(SPECIES_CHARIZARD, MOVE_PROTECT));
  CHECK(!learns(SPECIES_CHARIZARD, MOVE_SURF));
  int legendaries = 0;
  for (int i = 0; i < NUM_SPECIES; i++) {
    const SpeciesInfo *s = &species_info[i];
    legendaries += s->legendary && i < NUM_BASE_SPECIES;
    CHECK((s->is_mega || s->is_form) == (i >= NUM_BASE_SPECIES));
    CHECK(s->learn_cnt >= 4 && s->learn_ofs + s->learn_cnt <= LEARNSET_SIZE);
    for (uint8_t k = 0; k < s->learn_cnt; k++) CHECK(species_learnset(s, k) > 0 && species_learnset(s, k) < NUM_MOVES);
  }
  CHECK(legendaries == 103 && NUM_BASE_SPECIES == 606);
  /* CLAUDE CODE, le POKéMON secret : la dernière espèce, dessinée pour le jeu */
  const SpeciesInfo *c = &species_info[SPECIES_CLAUDE_CODE];
  CHECK(SPECIES_CLAUDE_CODE == NUM_BASE_SPECIES - 1 && !c->legendary && c->tier == 7 && c->dex == 0);
  CHECK(c->type1 == TYPE_ELECTRIC && c->type2 == TYPE_PSYCHIC && c->ability0 == ABILITY_DOWNLOAD);
  CHECK(learns(SPECIES_CLAUDE_CODE, MOVE_THUNDERBOLT) && !strcmp(species_names_fr[SPECIES_CLAUDE_CODE], "CLAUDE CODE"));
}

/* Les deux langues : chaque nom existe en français et en anglais, L() choisit selon lang. */
static void test_languages(void) {
  CHECK(!strcmp(species_names_fr[SPECIES_CHARIZARD], "DRACAUFEU") && !strcmp(species_names_en[SPECIES_CHARIZARD], "CHARIZARD"));
  CHECK(!strcmp(move_names_en[MOVE_SURF], "SURF") && !strcmp(move_names_en[MOVE_THUNDERBOLT], "THUNDERBOLT"));
  CHECK(!strcmp(item_names_en[ITEM_LEFTOVERS], "LEFTOVERS") && !strcmp(item_names_en[ITEM_HEAVY_BOOTS], "HEAVY-DUTY BOOTS"));
  CHECK(!strcmp(ability_names_en[ABILITY_INTIMIDATE], "INTIMIDATE") && !strcmp(type_names_en[TYPE_FIRE], "FIRE"));
  CHECK(!strcmp(nature_names_en[3], "ADAMANT") && !strcmp(item_names_en[ITEM_CHARIZARDITE_X], "CHARIZARDITE X"));
  for (int i = 1; i < NUM_SPECIES; i++) CHECK(species_names_en[i][0] && species_names_fr[i][0]);
  for (int i = 1; i < NUM_MOVES; i++) CHECK(move_names_en[i][0]);
  for (int i = 1; i < NUM_ITEMS; i++) CHECK(item_names_en[i][0] && item_descs_en[i][0]);
  for (int i = 1; i < NUM_ABILITYS; i++) CHECK(ability_names_en[i][0] && ability_descs_en[i][0]);
  lang = 1;
  CHECK(!strcmp(species_names[SPECIES_GARCHOMP], "GARCHOMP") && !strcmp(L("oui", "yes"), "yes"));
  lang = 0;
  CHECK(!strcmp(species_names[SPECIES_GARCHOMP], "CARCHACROK"));
}

static void test_megas(void) {
  int x = mega_form(SPECIES_CHARIZARD, ITEM_CHARIZARDITE_X), y = mega_form(SPECIES_CHARIZARD, ITEM_CHARIZARDITE_Y);
  CHECK(x > 0 && y > 0 && x != y);
  CHECK(species_info[x].is_mega && species_info[x].mega_base == SPECIES_CHARIZARD && species_info[x].type2 == TYPE_DRAGON);
  CHECK(species_info[x].base[STAT_ATK] == 130 && species_info[x].ability0 == ABILITY_TOUGH_CLAWS);
  CHECK(species_info[y].ability0 == ABILITY_DROUGHT && species_info[y].base[STAT_SPA] == 159);
  CHECK(mega_form(SPECIES_CHARIZARD, ITEM_LEFTOVERS) < 0 && mega_form(SPECIES_VENUSAUR, ITEM_CHARIZARDITE_X) < 0);
  CHECK(item_info[ITEM_CHARIZARDITE_X].effect == ITEM_EFFECT_MEGA_STONE && !strcmp(item_names[ITEM_CHARIZARDITE_X], "DRACAUFITE X"));
  CHECK(mega_form(SPECIES_GARDEVOIR, ITEM_GARDEVOIRITE) > 0 && mega_form(SPECIES_RAYQUAZA, ITEM_NONE) < 0);
  int megas = 0;
  int forms = 0;
  for (int i = NUM_BASE_SPECIES; i < NUM_SPECIES; i++) {
    const SpeciesInfo *s = &species_info[i];
    CHECK(s->is_mega != s->is_form && s->mega_base < NUM_BASE_SPECIES);
    if (s->is_mega) megas++;
    else forms++;
    if (s->is_mega) CHECK(mega_form((uint16_t)s->mega_base, (uint8_t)s->stone) == i);
    else CHECK(item_form((uint16_t)s->mega_base, (uint8_t)s->stone) == i && mega_form((uint16_t)s->mega_base, (uint8_t)s->stone) < 0);
  }
  CHECK(megas == 90 && forms == 57);
  /* les formes à objet : Giratina Originel, Zacian Épée Suprême, Arceus et ses Plaques */
  int g = item_form(SPECIES_GIRATINA_ALTERED, ITEM_GRISEOUS_ORB), z = item_form(SPECIES_ZACIAN_HERO, ITEM_RUSTED_SWORD);
  int fire = item_form(SPECIES_ARCEUS_NORMAL, ITEM_FLAME_PLATE);
  CHECK(g > 0 && species_info[g].base[STAT_ATK] == 120 && z > 0 && species_info[z].type2 == TYPE_STEEL);
  CHECK(fire > 0 && species_info[fire].type1 == TYPE_FIRE && item_info[ITEM_FLAME_PLATE].type == TYPE_FIRE);
  CHECK(!strcmp(item_names[ITEM_GRISEOUS_ORB], "GLOBE PLATINÉ") && *item_descs[ITEM_LEFTOVERS] && *item_descs[ITEM_GRISEOUS_ORB]);
  CHECK(item_form(SPECIES_GIRATINA_ALTERED, ITEM_LEFTOVERS) < 0 && item_form(SPECIES_DIALGA, ITEM_GRISEOUS_ORB) < 0);
}

static void test_moves_items(void) {
  const MoveInfo *f = &move_info[MOVE_FLAMETHROWER];
  CHECK(f->power == 90 && f->accuracy == 100 && f->pp == 15 && f->type == TYPE_FIRE); /* 6G : 95 -> 90 */
  CHECK(f->effect == EFFECT_BURN && f->effect_chance == 10 && !f->contact && !move_is_status(f));
  CHECK(move_priority(&move_info[MOVE_PROTECT]) == 4 && move_priority(&move_info[MOVE_ROAR]) == -6);
  CHECK(move_priority(&move_info[MOVE_EXTREME_SPEED]) == 2 && move_info[MOVE_EXTREME_SPEED].contact);
  CHECK(move_priority(&move_info[MOVE_TRICK_ROOM]) == -7 && move_info[MOVE_TRICK_ROOM].effect == EFFECT_TRICK_ROOM);
  CHECK(move_is_status(&move_info[MOVE_SWORDS_DANCE]) && move_info[MOVE_SWORDS_DANCE].effect == EFFECT_U_ATK_UP2);
  CHECK(move_info[MOVE_LEAF_BLADE].high_crit && move_info[MOVE_HYPER_VOICE].sound);
  CHECK(!strcmp(move_names[MOVE_FLAMETHROWER], "LANCE-FLAMMES"));
  CHECK(item_info[ITEM_CHARCOAL].effect == ITEM_EFFECT_TYPE_BOOST && item_info[ITEM_CHARCOAL].type == TYPE_FIRE);
  CHECK(item_info[ITEM_FAIRY_FEATHER].type == TYPE_FAIRY);
  CHECK(item_info[ITEM_LIFE_ORB].effect == ITEM_EFFECT_LIFE_ORB && item_info[ITEM_LIFE_ORB].rarity == 3);
  CHECK(!strcmp(item_names[ITEM_LEFTOVERS], "RESTES") && !strcmp(ability_names[ABILITY_LEVITATE], "LÉVITATION"));
  CHECK(ability_descs[ABILITY_LEVITATE][0] && !ability_descs[0][0]);
}

static void test_stats(void) {
  Pokemon p;
  memset(&p, 0, sizeof p);
  p.species = SPECIES_CHARIZARD;
  p.nature = 12; /* Sérieux : neutre */
  CHECK(pokemon_stat(&p, STAT_HP) == 138 && pokemon_stat(&p, STAT_SPA) == 114 && pokemon_stat(&p, STAT_SPE) == 105);
  p.nature = 10; /* Timide : +Vitesse, -Attaque */
  CHECK(!strcmp(nature_names[10], "TIMIDE"));
  CHECK(pokemon_stat(&p, STAT_SPE) == 115 && pokemon_stat(&p, STAT_ATK) == 80 && pokemon_stat(&p, STAT_SPA) == 114);
  p.nature = 15; /* Modeste : +Atq. Spé., -Attaque */
  CHECK(!strcmp(nature_names[15], "MODESTE") && pokemon_stat(&p, STAT_SPA) == 125);
  CHECK(pokemon_ability(&p) == ABILITY_BLAZE);
  p.ability = 1; /* talent caché : Force Soleil */
  CHECK(pokemon_ability(&p) == ABILITY_SOLAR_POWER);
  CHECK(!strcmp(type_names[TYPE_ELECTRIC], "ÉLECTRIK") && !strcmp(type_names[TYPE_FAIRY], "FÉE"));
}

int main(void) {
  test_types();
  test_categories();
  test_species();
  test_languages();
  test_megas();
  test_moves_items();
  test_stats();
  printf(failures ? "%d test(s) en échec\n" : "Tous les tests de données passent.\n", failures);
  return failures != 0;
}
