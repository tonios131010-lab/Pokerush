#include "game/trainer.h"
#include <string.h>
#include "assets.h"
#include "core/rng.h"
#include "data.h"
#include "game/battle.h"

const uint16_t starter_species[STARTER_COUNT] = {
    SPECIES_VENUSAUR,    SPECIES_CHARIZARD,  SPECIES_BLASTOISE, SPECIES_MEGANIUM,  SPECIES_TYPHLOSION, SPECIES_FERALIGATR,
    SPECIES_SCEPTILE,    SPECIES_BLAZIKEN,   SPECIES_SWAMPERT,  SPECIES_TORTERRA,  SPECIES_INFERNAPE,  SPECIES_EMPOLEON,
    SPECIES_SERPERIOR,   SPECIES_EMBOAR,     SPECIES_SAMUROTT,  SPECIES_CHESNAUGHT, SPECIES_DELPHOX,   SPECIES_GRENINJA,
    SPECIES_DECIDUEYE,   SPECIES_INCINEROAR, SPECIES_PRIMARINA, SPECIES_RILLABOOM, SPECIES_CINDERACE,  SPECIES_INTELEON,
    SPECIES_MEOWSCARADA, SPECIES_SKELEDIRGE, SPECIES_QUAQUAVAL,
};

/* ------------------------------------------------------------------------- archétypes */

typedef struct {
  uint8_t weather;   /* WEATHER_* recherchée */
  uint16_t moves[8]; /* attaques favorites */
  uint8_t items[4];  /* objets favoris */
} Style;

static const Style styles[NUM_ARCHETYPES] = {
    [ARCH_BALANCED] = {WEATHER_NONE, {0}, {ITEM_LEFTOVERS, ITEM_LIFE_ORB, ITEM_SITRUS_BERRY, ITEM_LUM_BERRY}},
    [ARCH_RAIN] = {WEATHER_RAIN, {MOVE_RAIN_DANCE, MOVE_SURF, MOVE_HYDRO_PUMP, MOVE_THUNDER, MOVE_WATERFALL, MOVE_HURRICANE},
                   {ITEM_MYSTIC_WATER, ITEM_LEFTOVERS, ITEM_LIFE_ORB}},
    [ARCH_SUN] = {WEATHER_SUN, {MOVE_SUNNY_DAY, MOVE_SOLAR_BEAM, MOVE_FIRE_BLAST, MOVE_FLAMETHROWER, MOVE_SYNTHESIS,
                                MOVE_MORNING_SUN, MOVE_ERUPTION, MOVE_GROWTH}, {ITEM_CHARCOAL, ITEM_MIRACLE_SEED, ITEM_LIFE_ORB}},
    [ARCH_SAND] = {WEATHER_SAND, {MOVE_SANDSTORM, MOVE_EARTHQUAKE, MOVE_ROCK_SLIDE, MOVE_STONE_EDGE},
                   {ITEM_LEFTOVERS, ITEM_SOFT_SAND, ITEM_HARD_STONE, ITEM_CHOICE_BAND}},
    [ARCH_SNOW] = {WEATHER_SNOW, {MOVE_SNOWSCAPE, MOVE_BLIZZARD, MOVE_ICE_BEAM, MOVE_FREEZE_DRY, MOVE_ICICLE_CRASH},
                   {ITEM_NEVER_MELT_ICE, ITEM_LEFTOVERS, ITEM_LIFE_ORB}},
    [ARCH_BATON] = {WEATHER_NONE, {MOVE_BATON_PASS, MOVE_SUBSTITUTE, MOVE_SWORDS_DANCE, MOVE_CALM_MIND, MOVE_AGILITY,
                                   MOVE_DRAGON_DANCE, MOVE_IRON_DEFENSE, MOVE_NASTY_PLOT}, {ITEM_LEFTOVERS, ITEM_LUM_BERRY}},
    [ARCH_STALL] = {WEATHER_NONE, {MOVE_TOXIC, MOVE_SPIKES, MOVE_LEECH_SEED, MOVE_RECOVER, MOVE_SOFT_BOILED, MOVE_ROAR,
                                   MOVE_WILL_O_WISP, MOVE_PROTECT}, {ITEM_LEFTOVERS, ITEM_ROCKY_HELMET, ITEM_BLACK_SLUDGE}},
    [ARCH_PHYSICAL] = {WEATHER_NONE, {MOVE_SWORDS_DANCE, MOVE_DRAGON_DANCE, MOVE_BULK_UP, MOVE_BELLY_DRUM, MOVE_SHELL_SMASH},
                       {ITEM_LIFE_ORB, ITEM_MUSCLE_BAND, ITEM_LUM_BERRY, ITEM_WEAKNESS_POLICY}},
    [ARCH_SPECIAL] = {WEATHER_NONE, {MOVE_CALM_MIND, MOVE_NASTY_PLOT, MOVE_QUIVER_DANCE},
                      {ITEM_LIFE_ORB, ITEM_WISE_GLASSES, ITEM_LEFTOVERS, ITEM_ASSAULT_VEST}},
    [ARCH_CHOICE] = {WEATHER_NONE, {MOVE_U_TURN, MOVE_VOLT_SWITCH, MOVE_FLIP_TURN},
                     {ITEM_CHOICE_BAND, ITEM_CHOICE_SPECS, ITEM_CHOICE_SCARF}},
    [ARCH_MONOTYPE] = {WEATHER_NONE, {0}, {ITEM_LEFTOVERS, ITEM_LIFE_ORB, ITEM_SITRUS_BERRY}},
    [ARCH_TRICK_ROOM] = {WEATHER_NONE, {MOVE_TRICK_ROOM, MOVE_BODY_PRESS, MOVE_GIGA_IMPACT}, {ITEM_LIFE_ORB, ITEM_SITRUS_BERRY}},
    [ARCH_HAZARDS] = {WEATHER_NONE, {MOVE_STEALTH_ROCK, MOVE_SPIKES, MOVE_TOXIC_SPIKES, MOVE_DRAGON_TAIL, MOVE_ROAR,
                                     MOVE_WHIRLWIND, MOVE_RAPID_SPIN}, {ITEM_HEAVY_BOOTS, ITEM_ROCKY_HELMET, ITEM_LEFTOVERS}},
};

static const SpeciesInfo *sp_info(uint16_t sp) { return &species_info[sp]; }
static bool is_type(const SpeciesInfo *s, uint8_t t) { return s->type1 == t || s->type2 == t; }
static bool has_ability(const SpeciesInfo *s, uint16_t a) { return s->ability0 == a || s->ability1 == a; }
static unsigned bulk(const SpeciesInfo *s) { return s->base[STAT_HP] + s->base[STAT_DEF] + s->base[STAT_SPD]; }

static bool learns(const SpeciesInfo *s, uint16_t move) {
  for (uint8_t i = 0; i < s->learn_cnt; i++)
    if (species_learnset(s, i) == move) return true;
  return false;
}

static bool favorite(uint8_t arch, uint16_t move) {
  for (int i = 0; i < 8 && styles[arch].moves[i]; i++)
    if (styles[arch].moves[i] == move) return true;
  return false;
}

uint8_t species_stone(uint16_t species) {
  for (int i = NUM_BASE_SPECIES; i < NUM_SPECIES; i++)
    if (species_info[i].is_mega && species_info[i].mega_base == species) return (uint8_t)species_info[i].stone;
  return ITEM_NONE;
}

void trainer_bring(const Trainer *t, const Team *opponent, int n, Team *out) {
  int score[TEAM_MAX];
  for (int i = 0; i < t->team.count; i++) {
    const Pokemon *p = &t->team.mon[i];
    const SpeciesInfo *s = sp_info(p->species);
    score[i] = s->tier * 10 + (item_info[p->item].effect == ITEM_EFFECT_MEGA_STONE && t->mega ? 40 : 0);
    for (int j = 0; j < opponent->count; j++) { /* ses types contre ceux d'en face */
      const SpeciesInfo *o = sp_info(opponent->mon[j].species);
      for (int k = 0; k < 2; k++) {
        uint8_t a = (uint8_t)(k ? s->type2 : s->type1);
        int e = type_effect(a, (uint8_t)o->type1) * (o->type2 != o->type1 ? type_effect(a, (uint8_t)o->type2) : 2);
        score[i] += e > 4 ? 6 : e < 4 ? -3 : 0;
      }
    }
  }
  out->count = 0;
  bool used[TEAM_MAX] = {false};
  int take = n < t->team.count ? n : t->team.count;
  while (out->count < take) {
    int best = -1;
    for (int i = 0; i < t->team.count; i++)
      if (!used[i] && (best < 0 || score[i] > score[best])) best = i;
    used[best] = true;
    out->mon[out->count++] = t->team.mon[best];
  }
}

uint8_t species_form_item(uint16_t species, uint32_t *rng) {
  uint8_t pick[24], n = 0;
  for (int i = NUM_BASE_SPECIES; i < NUM_SPECIES && n < 24; i++)
    if (species_info[i].is_form && species_info[i].mega_base == species) pick[n++] = (uint8_t)species_info[i].stone;
  return n ? pick[rng ? rng_below(rng, n) : 0] : ITEM_NONE;
}

/* Ce que l'espèce apporte au style ; < 0 : à écarter. */
static int affinity(uint16_t sp, uint8_t arch, uint8_t type) {
  const SpeciesInfo *s = sp_info(sp);
  int a = 0;
  switch (arch) {
    case ARCH_RAIN:
      a = has_ability(s, ABILITY_DRIZZLE) * 8 + has_ability(s, ABILITY_SWIFT_SWIM) * 5 +
          has_ability(s, ABILITY_RAIN_DISH) * 3 + is_type(s, TYPE_WATER) * 2;
      break;
    case ARCH_SUN:
      a = has_ability(s, ABILITY_DROUGHT) * 8 + has_ability(s, ABILITY_CHLOROPHYLL) * 5 +
          has_ability(s, ABILITY_SOLAR_POWER) * 4 + is_type(s, TYPE_FIRE) * 2 + is_type(s, TYPE_GRASS);
      break;
    case ARCH_SAND:
      a = has_ability(s, ABILITY_SAND_STREAM) * 8 + (has_ability(s, ABILITY_SAND_RUSH) || has_ability(s, ABILITY_SAND_FORCE)) * 4 +
          (is_type(s, TYPE_ROCK) || is_type(s, TYPE_GROUND) || is_type(s, TYPE_STEEL)) * 3;
      break;
    case ARCH_SNOW: a = has_ability(s, ABILITY_SNOW_WARNING) * 8 + is_type(s, TYPE_ICE) * 5; break;
    case ARCH_STALL: a = (int)bulk(s) / 60 - 2; break;
    case ARCH_PHYSICAL: a = (s->base[STAT_ATK] >= 110) * 4 + (s->base[STAT_SPE] >= 90); break;
    case ARCH_SPECIAL: a = (s->base[STAT_SPA] >= 110) * 4 + (s->base[STAT_SPE] >= 90); break;
    case ARCH_CHOICE:
      a = (s->base[STAT_SPE] >= 95) * 2 + (s->base[STAT_ATK] >= 110 || s->base[STAT_SPA] >= 110) * 2;
      break;
    case ARCH_TRICK_ROOM:
      if (s->base[STAT_SPE] > 70) return -1;
      a = (s->base[STAT_SPE] <= 50) * 4 + (s->base[STAT_ATK] >= 110 || s->base[STAT_SPA] >= 110) * 3;
      break;
    case ARCH_HAZARDS: a = (int)bulk(s) / 80; break;
    case ARCH_MONOTYPE: return is_type(s, type) ? 6 : -1;
  }
  int fav = 0;
  for (int i = 0; i < 8 && styles[arch].moves[i]; i++) fav += learns(s, styles[arch].moves[i]);
  return a + (fav > 3 ? 3 : fav);
}

/* ------------------------------------------------------------------------- un Pokémon */

/* Valeur d'une attaque offensive pour cette espèce : puissance utile × précision × même type × stat. */
static uint32_t attack_value(const SpeciesInfo *s, uint16_t move) {
  const MoveInfo *m = &move_info[move];
  if (move_is_status(m)) return 0;
  uint32_t p = m->power;
  switch (m->effect) {
    case EFFECT_FIXED_LEVEL:
    case EFFECT_FIXED_40: p = 55; break;
    case EFFECT_MULTI_HIT: p *= 3; break;
    case EFFECT_DOUBLE_HIT: p *= 2; break;
    case EFFECT_RECHARGE:
    case EFFECT_SOLAR:
    case EFFECT_CHARGE: p = p * 2 / 3; break;
    case EFFECT_EXPLODE: p /= 3; break;
    case EFFECT_ERUPTION: p = 120; break;
    case EFFECT_U_SPA_DOWN2:
    case EFFECT_U_ATK_DEF_DOWN:
    case EFFECT_CRASH:
    case EFFECT_RAMPAGE: p = p * 5 / 6; break;
    case EFFECT_FAKE_OUT:
    case EFFECT_RAPID_SPIN: p = 35; break;
    case EFFECT_KNOCK_OFF: p = p * 5 / 4; break;
    case EFFECT_SUCKER_PUNCH: p = p * 3 / 4; break;
    case EFFECT_PIVOT: p = p * 6 / 5; break;
  }
  p = p * (m->accuracy ? m->accuracy : 100) / 100;
  if (is_type(s, m->type)) p = p * 3 / 2;
  uint16_t stat = m->effect == EFFECT_BODY_PRESS ? s->base[STAT_DEF] : m->special ? s->base[STAT_SPA] : s->base[STAT_ATK];
  return p * stat / 100;
}

static bool is_setup(uint8_t e) {
  return (e >= EFFECT_U_ATK_UP2 && e <= EFFECT_SHELL_SMASH) || e == EFFECT_BELLY_DRUM || e == EFFECT_CURSE;
}

static uint32_t status_value(const SpeciesInfo *s, uint16_t move, uint8_t arch) {
  uint8_t e = move_info[move].effect;
  if (favorite(arch, move)) return 110;
  bool phys = s->base[STAT_ATK] >= 95, spec = s->base[STAT_SPA] >= 95;
  switch (e) {
    case EFFECT_U_ATK_UP2:
    case EFFECT_U_ATK_SPE_UP:
    case EFFECT_U_ATK_DEF_UP:
    case EFFECT_BELLY_DRUM:
    case EFFECT_CURSE: return phys ? 65 : 5;
    case EFFECT_U_SPA_SPD_UP:
    case EFFECT_U_SPA_UP2:
    case EFFECT_U_SPA_SPD_SPE_UP: return spec ? 65 : 5;
    case EFFECT_SHELL_SMASH:
    case EFFECT_U_ATK_SPA_UP: return phys || spec ? 55 : 5;
    case EFFECT_HEAL_HALF:
    case EFFECT_HEAL_WEATHER: return bulk(s) >= 260 ? 70 : 35;
    case EFFECT_SLEEP: return 70;
    case EFFECT_PARALYZE:
    case EFFECT_BADLY_POISON: return 45;
    case EFFECT_BURN: return 40;
    case EFFECT_STEALTH_ROCK: return 50;
    case EFFECT_SPIKES:
    case EFFECT_TOXIC_SPIKES:
    case EFFECT_LEECH_SEED: return 40;
    case EFFECT_CONFUSE:
    case EFFECT_REFLECT:
    case EFFECT_LIGHT_SCREEN:
    case EFFECT_SUBSTITUTE:
    case EFFECT_TAILWIND:
    case EFFECT_TAUNT: return 25;
    case EFFECT_REST:
    case EFFECT_ROAR:
    case EFFECT_HEAL_BELL: return 20;
    case EFFECT_TRICK_ROOM: return s->base[STAT_SPE] <= 50 ? 45 : 2;
    default: return 8;
  }
}

static uint8_t booster_for(uint8_t type) {
  for (uint8_t i = 1; i < NUM_ITEMS; i++)
    if (item_info[i].effect == ITEM_EFFECT_TYPE_BOOST && item_info[i].type == type) return i;
  return ITEM_LEFTOVERS;
}

Pokemon trainer_build(uint16_t species, uint8_t archetype, uint8_t skill, bool item, uint32_t *rng) {
  Pokemon p;
  memset(&p, 0, sizeof p);
  p.species = species;
  const SpeciesInfo *s = sp_info(species);
  uint32_t noise = 10u * (uint32_t)(10 - (skill > 10 ? 10 : skill)) + 5;
  uint16_t chosen[MOVES_MAX] = {0};
  int n = 0, statuses = 0, setups = 0;
  int max_status = archetype == ARCH_STALL || archetype == ARCH_BATON || archetype == ARCH_HAZARDS ? 3 :
                   archetype == ARCH_CHOICE ? 0 : 2;
  uint32_t best_attack = 0;
  bool special_main = false;

  while (n < MOVES_MAX) {
    uint32_t best = 0;
    uint16_t pick = 0;
    for (uint8_t i = 0; i < s->learn_cnt; i++) {
      uint16_t mv = species_learnset(s, i);
      const MoveInfo *m = &move_info[mv];
      bool dup = false, same_type = false;
      for (int k = 0; k < n; k++) {
        dup |= chosen[k] == mv;
        same_type |= !move_is_status(&move_info[chosen[k]]) && move_info[chosen[k]].type == m->type;
      }
      if (dup) continue;
      uint32_t v;
      if (move_is_status(m)) {
        if (statuses >= max_status || n == 0 || (is_setup(m->effect) && setups)) continue;
        if (m->effect == EFFECT_PROTECT && archetype != ARCH_STALL) continue;
        v = status_value(s, mv, archetype);
      } else {
        v = attack_value(s, mv);
        if (same_type) v /= 3; /* une deuxième attaque du même type couvre peu */
        if (favorite(archetype, mv)) v = v * 3 / 2;
      }
      v = v * (100 + rng_below(rng, noise)) / 100;
      if (skill <= 2) v = 1 + rng_below(rng, 100) + (n == 0 ? v : 0); /* les débutants choisissent au hasard */
      if (v > best) best = v, pick = mv;
    }
    if (!pick) break;
    const MoveInfo *m = &move_info[pick];
    if (move_is_status(m)) statuses++, setups += is_setup(m->effect);
    else if (attack_value(s, pick) > best_attack) best_attack = attack_value(s, pick), special_main = m->special;
    chosen[n++] = pick;
  }
  for (int k = 0; k < n; k++) pokemon_set_move(&p, (uint8_t)k, chosen[k]);

  /* nature selon le rôle */
  bool fast = s->base[STAT_SPE] >= 90;
  if (skill < 4) p.nature = (uint8_t)rng_below(rng, NUM_NATURES);
  else if (archetype == ARCH_TRICK_ROOM) p.nature = special_main ? 17 : 2; /* Discret / Brave */
  else if (archetype == ARCH_STALL || (statuses >= 2 && bulk(s) >= 260))
    p.nature = special_main ? (s->base[STAT_DEF] > s->base[STAT_SPD] ? 5 : 20) : (s->base[STAT_DEF] > s->base[STAT_SPD] ? 8 : 23);
  else if (special_main) p.nature = fast ? 10 : 15; /* Timide / Modeste */
  else p.nature = fast ? 13 : 3;                   /* Jovial / Rigide */
  p.ability = s->ability1 && rng_percent(rng, 50);

  if (item) {
    const Style *st = &styles[archetype];
    uint8_t it = ITEM_NONE;
    if (st->items[0] && rng_percent(rng, 60)) {
      int k = 0;
      while (k < 4 && st->items[k]) k++;
      it = st->items[rng_below(rng, (uint32_t)k)];
      if (it == ITEM_CHOICE_BAND && special_main) it = ITEM_CHOICE_SPECS;
      if (it == ITEM_CHOICE_SPECS && !special_main) it = ITEM_CHOICE_BAND;
      if (it == ITEM_BLACK_SLUDGE && !is_type(s, TYPE_POISON)) it = ITEM_LEFTOVERS;
      if (it == ITEM_ASSAULT_VEST && statuses) it = ITEM_LEFTOVERS;
    } else if (!statuses && skill >= 6) {
      it = rng_percent(rng, 30) ? ITEM_CHOICE_SCARF : special_main ? ITEM_CHOICE_SPECS : ITEM_CHOICE_BAND;
    } else if (statuses >= 2) {
      it = is_type(s, TYPE_POISON) ? ITEM_BLACK_SLUDGE : ITEM_LEFTOVERS;
    } else if (bulk(s) < 210 && fast) {
      it = ITEM_FOCUS_SASH;
    } else if (skill >= 6) {
      it = rng_percent(rng, 50) ? ITEM_LIFE_ORB : ITEM_EXPERT_BELT;
    } else {
      it = rng_percent(rng, 50) ? booster_for(chosen[0] ? (uint8_t)move_info[chosen[0]].type : (uint8_t)s->type1) : ITEM_SITRUS_BERRY;
    }
    p.item = it;
    /* Giratina, Zacian, Arceus… tiennent l'objet de leur forme */
    uint8_t form = species_form_item(species, rng);
    if (form) p.item = form;
  }
  p.shiny = rng_below(rng, 2048) == 0;
  return p;
}

/* ------------------------------------------------------------------------- une équipe */

typedef struct {
  uint8_t min_tier, max_tier, legend_pct, size, item_pct;
  bool legends_only;
} Rules;

/* Tire une espèce pas encore dans l'équipe, pondérée par son affinité avec le style. Faute de
 * candidate, les règles s'assouplissent dans l'ordre : rang minimal, puis tout le rang, puis le
 * style lui-même (un type sans assez d'espèces) ; jamais l'interdiction des légendaires. */
static uint16_t pick_species(const Trainer *t, const Rules *r, uint32_t *rng) {
  static uint32_t weights[NUM_BASE_SPECIES]; /* (statique : la pile est petite) */
  uint32_t total = 0;
  for (int relax = 0; relax < 4 && !total; relax++) {
    for (int sp = 0; sp < NUM_BASE_SPECIES; sp++) {
      const SpeciesInfo *s = sp_info((uint16_t)sp);
      bool used = false;
      for (int k = 0; k < t->team.count; k++) used |= t->team.mon[k].species == sp;
      int a = relax >= 3 ? 0 : affinity((uint16_t)sp, t->archetype, t->type);
      bool tier_ok = relax >= 2 || (s->tier >= (relax ? 0 : r->min_tier) && s->tier <= r->max_tier);
      /* jamais de légendaire là où il n'est pas permis ; le Maître peut en manquer */
      bool legend_ok = r->legends_only ? s->legendary || relax >= 2 : !s->legendary;
      if (t->mega && a >= 0 && species_stone((uint16_t)sp)) a += 3;
      if (a >= 0 && t->archetype != ARCH_MONOTYPE && is_type(s, t->type)) a += 4; /* type favori */
      /* CLAUDE CODE, le POKéMON secret, n'apparaît que dans le draft */
      weights[sp] = used || a < 0 || !tier_ok || !legend_ok || sp == SPECIES_CLAUDE_CODE ? 0 : (uint32_t)(2 + a);
      total += weights[sp];
    }
  }
  uint32_t x = rng_below(rng, total);
  for (int sp = 0; sp < NUM_BASE_SPECIES; sp++) {
    if (x < weights[sp]) return (uint16_t)sp;
    x -= weights[sp];
  }
  return 0;
}

static void assign_mega(Trainer *t);

static void fill_team(Trainer *t, const Rules *r, uint32_t *rng) {
  t->team.count = 0;
  for (int i = 0; i < r->size; i++) {
    bool legend = r->legends_only || (r->legend_pct && rng_percent(rng, r->legend_pct));
    Rules one = *r;
    if (legend) one.legends_only = true, one.max_tier = 7, one.min_tier = 0;
    uint16_t sp = pick_species(t, &one, rng);
    t->team.mon[t->team.count++] = trainer_build(sp, t->archetype, t->skill, rng_percent(rng, r->item_pct), rng);
  }
  /* style météo : si personne n'a le talent, le premier lance la météo lui-même */
  uint8_t w = styles[t->archetype].weather;
  static const uint16_t weather_move[] = {0, MOVE_RAIN_DANCE, MOVE_SUNNY_DAY, MOVE_SANDSTORM, MOVE_SNOWSCAPE};
  static const uint8_t weather_ability[] = {0, ABILITY_DRIZZLE, ABILITY_DROUGHT, ABILITY_SAND_STREAM, ABILITY_SNOW_WARNING};
  if (w && t->skill >= 4) {
    bool setter = false;
    for (int i = 0; i < t->team.count; i++) setter |= pokemon_ability(&t->team.mon[i]) == weather_ability[w];
    for (int i = 0; i < t->team.count && !setter; i++) {
      Pokemon *p = &t->team.mon[i];
      for (int k = 0; k < MOVES_MAX; k++) setter |= pokemon_move(p, (uint8_t)k) == weather_move[w];
      if (!setter && learns(sp_info(p->species), weather_move[w])) {
        pokemon_set_move(p, 3, weather_move[w]);
        setter = true;
        if (i) { /* il passe en tête */
          Pokemon tmp = t->team.mon[0];
          t->team.mon[0] = *p, *p = tmp;
        }
      }
    }
  }
  if (t->mega) assign_mega(t);
}

/* ------------------------------------------------------------------------- noms, sprites, fonds */

static const char *const male_names[2][21] = {
    {"LÉO", "LUCAS", "HUGO", "NOAH", "ADAM", "LOUIS", "JULES", "TOM", "NATHAN", "PAUL", "ENZO", "THÉO", "SACHA", "YANIS",
     "NOÉ", "ROBIN", "ARTHUR", "GABIN", "AXEL", "MAËL", "BASILE"},
    {"JOEY", "LUKE", "HUGH", "NOAH", "ADAM", "LEWIS", "JACK", "TOM", "NATHAN", "PAUL", "OWEN", "THEO", "SAM", "RYAN",
     "BEN", "ROBIN", "ARTHUR", "GABE", "ALEX", "MILES", "BASIL"}};
static const char *const female_names[2][21] = {
    {"EMMA", "JADE", "LOUISE", "ALICE", "LÉA", "CHLOÉ", "INÈS", "MANON", "ZOÉ", "LOLA", "NINA", "ROSE", "ANNA", "LÉNA",
     "MILA", "ELSA", "IRIS", "CLARA", "CÉLIA", "JULIE", "SARAH"},
    {"EMMA", "JADE", "LUCY", "ALICE", "LEAH", "CHLOE", "IVY", "MAY", "ZOE", "LOLA", "NINA", "ROSE", "ANNA", "LENA",
     "MILA", "ELSA", "IRIS", "CLARA", "CELIA", "JULIE", "SARAH"}};
#define COUNT(a) (sizeof(a) / sizeof((a)[0]))

/* Un dresseur ordinaire au rang voulu : le sprite d'une classe d'Émeraude ou de RF/VF, et son nom
 * suivi d'un prénom du bon genre. */
static void dress(Trainer *t, int rank, uint32_t *rng) {
  uint8_t pick[TRAINER_CLASS_COUNT], n = 0;
  for (uint8_t i = 0; i < TRAINER_CLASS_COUNT; i++)
    if (trainer_classes[i].rank == rank) pick[n++] = i;
  uint8_t c = pick[rng_below(rng, n)];
  t->pic = c;
  char gender = trainer_classes[c].gender;
  const char *first = gender == 'm' ? male_names[lang][rng_below(rng, COUNT(male_names[0]))]
                                     : female_names[lang][rng_below(rng, COUNT(female_names[0]))];
  strcat(strcat(strcpy(t->name, L(trainer_classes[c].name, trainer_classes[c].name_en)), " "), first);
}

static uint8_t backdrop_for(uint8_t type, uint32_t *rng) {
  switch (type) {
    case TYPE_WATER: return rng_percent(rng, 50) ? BACKDROP_SEA : BACKDROP_WATER;
    case TYPE_GRASS:
    case TYPE_BUG: return rng_percent(rng, 50) ? BACKDROP_GRASS2 : BACKDROP_GRASS3;
    case TYPE_GROUND:
    case TYPE_ROCK: return rng_percent(rng, 50) ? BACKDROP_DESERT : BACKDROP_CAVE;
    case TYPE_POISON: return BACKDROP_SWAMP;
    default: {
      static const uint8_t plain[] = {BACKDROP_GRASS, BACKDROP_LAND, BACKDROP_LAND2, BACKDROP_GRASS2};
      return plain[rng_below(rng, sizeof plain)];
    }
  }
}

/* Un Boss tous les 10 combats (et le Conseil 4) : une équipe fixe, dans l'ordre, et la stratégie qui va
 * avec. Le premier membre ouvre ; on en garde plus ou moins selon le combat et la difficulté. */
typedef struct {
  const char *name, *name_en;
  uint8_t pic, type, archetype, backdrop;
  uint16_t team[6];
} Boss;

static const Boss bosses[] = {
    /* Hoenn : combats 10 à 80, FLORA au 90 */
    {"ROXANNE", "ROXANNE", TRAINER_PIC_ROXANNE, TYPE_ROCK, ARCH_HAZARDS, BACKDROP_ARENA1,
     {SPECIES_GIGALITH, SPECIES_CARBINK, SPECIES_ARMALDO, SPECIES_CRADILY, SPECIES_RAMPARDOS, SPECIES_AERODACTYL}},
    {"BASTIEN", "BRAWLY", TRAINER_PIC_BRAWLY, TYPE_FIGHTING, ARCH_PHYSICAL, BACKDROP_ARENA2,
     {SPECIES_HARIYAMA, SPECIES_BRELOOM, SPECIES_HITMONTOP, SPECIES_SAWK, SPECIES_THROH, SPECIES_MEDICHAM}},
    {"VOLTÈRE", "WATTSON", TRAINER_PIC_WATTSON, TYPE_ELECTRIC, ARCH_CHOICE, BACKDROP_ARENA3,
     {SPECIES_MAGNEZONE, SPECIES_LANTURN, SPECIES_GALVANTULA, SPECIES_ZEBSTRIKA, SPECIES_JOLTEON, SPECIES_MANECTRIC}},
    {"ADRIANE", "FLANNERY", TRAINER_PIC_FLANNERY, TYPE_FIRE, ARCH_SUN, BACKDROP_ARENA4,
     {SPECIES_TORKOAL, SPECIES_NINETALES, SPECIES_HEATMOR, SPECIES_FLAREON, SPECIES_ARCANINE, SPECIES_CAMERUPT}},
    {"NORMAN", "NORMAN", TRAINER_PIC_NORMAN, TYPE_NORMAL, ARCH_PHYSICAL, BACKDROP_ARENA5,
     {SPECIES_SLAKING, SPECIES_MILTANK, SPECIES_ZANGOOSE, SPECIES_STOUTLAND, SPECIES_SNORLAX, SPECIES_KANGASKHAN}},
    {"ALIZÉE", "WINONA", TRAINER_PIC_WINONA, TYPE_FLYING, ARCH_BALANCED, BACKDROP_ARENA1,
     {SPECIES_PELIPPER, SPECIES_SKARMORY, SPECIES_SWANNA, SPECIES_TOUCANNON, SPECIES_NOIVERN, SPECIES_ALTARIA}},
    {"LÉVY & TATIA", "TATE & LIZA", TRAINER_PIC_TATE_AND_LIZA, TYPE_PSYCHIC, ARCH_TRICK_ROOM, BACKDROP_ARENA2,
     {SPECIES_BRONZONG, SPECIES_CLAYDOL, SPECIES_REUNICLUS, SPECIES_GOTHITELLE, SPECIES_MALAMAR, SPECIES_SLOWBRO}},
    {"JUAN", "JUAN", TRAINER_PIC_JUAN, TYPE_WATER, ARCH_RAIN, BACKDROP_ARENA3,
     {SPECIES_POLITOED, SPECIES_LUDICOLO, SPECIES_KINGDRA, SPECIES_SHARPEDO, SPECIES_MILOTIC, SPECIES_SWAMPERT}},
    {"RIVALE FLORA", "RIVAL MAY", TRAINER_PIC_MAY, TYPE_NORMAL, ARCH_BALANCED, BACKDROP_GRASS,
     {SPECIES_CROBAT, SPECIES_LUDICOLO, SPECIES_GARDEVOIR, SPECIES_METAGROSS, SPECIES_ALTARIA, SPECIES_BLAZIKEN}},
    /* Conseil 4 : 96 à 99, Maître : 100 */
    {"DAMIEN", "SIDNEY", TRAINER_PIC_SIDNEY, TYPE_DARK, ARCH_PHYSICAL, BACKDROP_ARENA4,
     {SPECIES_MIGHTYENA, SPECIES_SHIFTRY, SPECIES_CACTURNE, SPECIES_CRAWDAUNT, SPECIES_TYRANITAR, SPECIES_ABSOL}},
    {"SPECTRA", "PHOEBE", TRAINER_PIC_PHOEBE, TYPE_GHOST, ARCH_STALL, BACKDROP_ARENA5,
     {SPECIES_SABLEYE, SPECIES_DUSKNOIR, SPECIES_CHANDELURE, SPECIES_MIMIKYU_DISGUISED, SPECIES_GENGAR, SPECIES_BANETTE}},
    {"GLACIA", "GLACIA", TRAINER_PIC_GLACIA, TYPE_ICE, ARCH_SNOW, BACKDROP_ARENA1,
     {SPECIES_ABOMASNOW, SPECIES_GLALIE, SPECIES_WALREIN, SPECIES_FROSLASS, SPECIES_LAPRAS, SPECIES_BAXCALIBUR}},
    {"ARAGON", "DRAKE", TRAINER_PIC_DRAKE, TYPE_DRAGON, ARCH_PHYSICAL, BACKDROP_ARENA2,
     {SPECIES_FLYGON, SPECIES_KINGDRA, SPECIES_HAXORUS, SPECIES_GARCHOMP, SPECIES_DRAGONITE, SPECIES_SALAMENCE}},
    {"Le MAÎTRE MARC", "CHAMPION WALLACE", TRAINER_PIC_WALLACE, TYPE_WATER, ARCH_BALANCED, BACKDROP_ARENA3,
     {SPECIES_LATIAS, SPECIES_LATIOS, SPECIES_DEOXYS_NORMAL, SPECIES_GROUDON, SPECIES_KYOGRE, SPECIES_RAYQUAZA}},
    /* Kanto : 110, 120… puis Blue */
    {"PIERRE", "BROCK", TRAINER_PIC_BROCK, TYPE_ROCK, ARCH_SAND, BACKDROP_ARENA1,
     {SPECIES_TYRANITAR, SPECIES_GOLEM, SPECIES_KABUTOPS, SPECIES_OMASTAR, SPECIES_RHYPERIOR, SPECIES_AERODACTYL}},
    {"ONDINE", "MISTY", TRAINER_PIC_MISTY, TYPE_WATER, ARCH_RAIN, BACKDROP_ARENA2,
     {SPECIES_POLITOED, SPECIES_GOLDUCK, SPECIES_LAPRAS, SPECIES_KINGDRA, SPECIES_GYARADOS, SPECIES_STARMIE}},
    {"MAJOR BOB", "LT. SURGE", TRAINER_PIC_SURGE, TYPE_ELECTRIC, ARCH_CHOICE, BACKDROP_ARENA3,
     {SPECIES_ELECTIVIRE, SPECIES_MAGNEZONE, SPECIES_JOLTEON, SPECIES_ZAPDOS, SPECIES_XURKITREE, SPECIES_RAICHU}},
    {"ERIKA", "ERIKA", TRAINER_PIC_ERIKA, TYPE_GRASS, ARCH_SUN, BACKDROP_ARENA4,
     {SPECIES_VILEPLUME, SPECIES_TANGROWTH, SPECIES_ROSERADE, SPECIES_LILLIGANT, SPECIES_VICTREEBEL, SPECIES_VENUSAUR}},
    {"KOGA", "KOGA", TRAINER_PIC_KOGA, TYPE_POISON, ARCH_STALL, BACKDROP_ARENA5,
     {SPECIES_TOXAPEX, SPECIES_WEEZING, SPECIES_MUK, SPECIES_DRAPION, SPECIES_NIDOKING, SPECIES_CROBAT}},
    {"MORGANE", "SABRINA", TRAINER_PIC_SABRINA, TYPE_PSYCHIC, ARCH_SPECIAL, BACKDROP_ARENA1,
     {SPECIES_ESPEON, SPECIES_HATTERENE, SPECIES_REUNICLUS, SPECIES_GARDEVOIR, SPECIES_MEWTWO, SPECIES_ALAKAZAM}},
    {"AUGUSTE", "BLAINE", TRAINER_PIC_BLAINE, TYPE_FIRE, ARCH_SUN, BACKDROP_ARENA2,
     {SPECIES_RAPIDASH, SPECIES_MAGMORTAR, SPECIES_ARCANINE, SPECIES_ENTEI, SPECIES_MOLTRES, SPECIES_CHARIZARD}},
    {"GIOVANNI", "GIOVANNI", TRAINER_PIC_GIOVANNI, TYPE_GROUND, ARCH_PHYSICAL, BACKDROP_ARENA3,
     {SPECIES_HIPPOWDON, SPECIES_NIDOQUEEN, SPECIES_NIDOKING, SPECIES_RHYPERIOR, SPECIES_GROUDON, SPECIES_GARCHOMP}},
    {"OLGA", "LORELEI", TRAINER_PIC_LORELEI, TYPE_ICE, ARCH_SNOW, BACKDROP_ARENA4,
     {SPECIES_ABOMASNOW, SPECIES_CLOYSTER, SPECIES_LAPRAS, SPECIES_MAMOSWINE, SPECIES_ARTICUNO, SPECIES_KYUREM}},
    {"ALDO", "BRUNO", TRAINER_PIC_BRUNO, TYPE_FIGHTING, ARCH_PHYSICAL, BACKDROP_ARENA5,
     {SPECIES_HITMONLEE, SPECIES_HITMONCHAN, SPECIES_CONKELDURR, SPECIES_MACHAMP, SPECIES_URSHIFU_SINGLE_STRIKE, SPECIES_LUCARIO}},
    {"AGATHA", "AGATHA", TRAINER_PIC_AGATHA, TYPE_GHOST, ARCH_STALL, BACKDROP_ARENA1,
     {SPECIES_MISMAGIUS, SPECIES_DRIFBLIM, SPECIES_AEGISLASH_SHIELD, SPECIES_DRAGAPULT, SPECIES_GIRATINA_ALTERED, SPECIES_GENGAR}},
    {"PETER", "LANCE", TRAINER_PIC_LANCE, TYPE_DRAGON, ARCH_PHYSICAL, BACKDROP_ARENA2,
     {SPECIES_KINGDRA, SPECIES_GYARADOS, SPECIES_SALAMENCE, SPECIES_DRAGONITE, SPECIES_RAYQUAZA, SPECIES_AERODACTYL}},
    {"Le MAÎTRE BLUE", "CHAMPION BLUE", TRAINER_PIC_BLUE, TYPE_NORMAL, ARCH_BALANCED, BACKDROP_ARENA3,
     {SPECIES_EXEGGUTOR, SPECIES_ARCANINE, SPECIES_TYRANITAR, SPECIES_ALAKAZAM, SPECIES_MEWTWO, SPECIES_PIDGEOT}},
};
#define BOSS_COUNT (int)COUNT(bosses)

int trainer_difficulty = DIFF_NORMAL;

/* Le Boss du combat n, ou -1. */
static int boss_for(uint16_t n) {
  if (n % 10 == 0 && n <= 90) return n / 10 - 1;
  if (n >= 96 && n <= 100) return 9 + (n - 96);
  if (n > 100 && n % 10 == 0) return 14 + (n - 110) / 10 % 13;
  return -1;
}


/* Taille d'équipe : 6 au combat 30 (Facile), 20 (Normal) ou 10 (Difficile). */
static uint8_t team_size(uint16_t n) {
  static const uint8_t full_at[3] = {30, 20, 10};
  uint8_t f = full_at[trainer_difficulty];
  return (uint8_t)(n >= f ? 6 : 1 + 5 * (n - 1) / (f - 1));
}

static void character(Trainer *t, const char *name, uint8_t pic) {
  strcpy(t->name, name);
  t->pic = pic;
}

/* Méga-Évolution : le plus fort de ceux qui ont une forme Méga reçoit sa gemme et passe en dernier. */
static void assign_mega(Trainer *t) {
  int ace = -1;
  for (int i = 0; i < t->team.count; i++) {
    uint16_t sp = t->team.mon[i].species;
    if (species_stone(sp) && (ace < 0 || sp_info(sp)->tier >= sp_info(t->team.mon[ace].species)->tier)) ace = i;
  }
  if (ace < 0) {
    t->mega = false;
    return;
  }
  Pokemon *p = &t->team.mon[ace];
  p->item = species_stone(p->species);
  Pokemon tmp = t->team.mon[t->team.count - 1];
  t->team.mon[t->team.count - 1] = *p, *p = tmp;
}

static void make_boss(Trainer *t, int k, uint16_t n) {
  const Boss *b = &bosses[k];
  uint32_t brng = 0x9E3779B9u * (uint32_t)(k + 1); /* toujours la même équipe */
  character(t, L(b->name, b->name_en), b->pic);
  t->type = b->type, t->archetype = b->archetype, t->backdrop = b->backdrop;
  t->skill = 10, t->loot_bonus = 1, t->boss = true;
  t->mega = n >= 20 && b->archetype != ARCH_TRICK_ROOM;
  uint8_t size = n >= 96 ? 6 : (uint8_t)(team_size(n) + 1 > 6 ? 6 : team_size(n) + 1 < 3 ? 3 : team_size(n) + 1);
  /* on garde les premiers, mais le chef (le dernier de la liste) mène toujours la danse */
  t->team.count = 0;
  for (int i = 0; i < size; i++) {
    uint16_t sp = b->team[i == size - 1 ? 5 : i];
    t->team.mon[t->team.count++] = trainer_build(sp, b->archetype, 10, true, &brng);
  }
  if (t->mega) assign_mega(t);
}

void trainer_make(Trainer *t, uint16_t battle, const Team *player, uint32_t *rng) {
  memset(t, 0, sizeof *t);
  uint16_t n = battle ? battle : 1;
  int diff = trainer_difficulty, tier_adj = diff - DIFF_NORMAL;
  int max_tier = 2 + n / 12 + tier_adj, min_tier = n / 20;
  max_tier = max_tier < 1 ? 1 : max_tier > 7 ? 7 : max_tier;
  min_tier = min_tier > max_tier ? max_tier : min_tier;
  Rules r = {.min_tier = (uint8_t)min_tier, .max_tier = (uint8_t)max_tier,
             .legend_pct = (uint8_t)(n < 50 ? 0 : n < 80 ? 10 : 25),
             .item_pct = (uint8_t)(n < 5 ? 0 : 10 + 2 * n > 100 ? 100 : 10 + 2 * n)};
  if (diff == DIFF_EASY) r.item_pct /= 2;
  r.size = team_size(n);
  int skill = 1 + n / 9 + 2 * tier_adj;
  t->skill = (uint8_t)(skill < 0 ? 0 : skill > 10 ? 10 : skill);

  if (n == 1) { /* le starter que celui du joueur bat (Feu contre Plante…) */
    int counter = (int)rng_below(rng, STARTER_COUNT);
    for (int i = 0; i < STARTER_COUNT && player && player->count; i++)
      if (starter_species[i] == player->mon[0].species) counter = i % 3 == 0 ? i + 2 : i - 1;
    t->archetype = ARCH_BALANCED, t->skill = 3;
    character(t, L("RIVALE FLORA", "RIVAL MAY"), TRAINER_PIC_MAY);
    t->team.count = 1;
    t->team.mon[0] = trainer_build(starter_species[counter], ARCH_BALANCED, 5, false, rng);
    t->type = (uint8_t)species_info[t->team.mon[0].species].type1;
    t->backdrop = BACKDROP_GRASS;
    return;
  }
  int k = boss_for(n);
  if (k >= 0) return make_boss(t, k, n);
  if (n >= 25 && rng_percent(rng, 7)) { /* raid : un légendaire sauvage géant, au hasard */
    t->wild = true, t->archetype = ARCH_BALANCED, t->skill = 10;
    t->backdrop = rng_percent(rng, 50) ? BACKDROP_CAVE : BACKDROP_SEA;
    Rules one = r;
    one.legends_only = true, one.size = 1, one.item_pct = 100;
    fill_team(t, &one, rng);
    t->type = (uint8_t)species_info[t->team.mon[0].species].type1;
    strcpy(t->name, species_names[t->team.mon[0].species]);
    return;
  }
  /* un style au hasard ; les plus élaborés n'arrivent qu'avec l'expérience ; toujours un type favori */
  static const uint8_t pool[] = {ARCH_BALANCED, ARCH_BALANCED, ARCH_MONOTYPE, ARCH_PHYSICAL, ARCH_SPECIAL,
                                 ARCH_RAIN,     ARCH_SUN,      ARCH_SAND,     ARCH_SNOW,     ARCH_STALL,
                                 ARCH_BATON,    ARCH_CHOICE,   ARCH_TRICK_ROOM, ARCH_HAZARDS};
  uint32_t span = n < 10 ? 3 : n < 25 ? 5 : n < 40 ? 12 : sizeof pool;
  t->archetype = pool[rng_below(rng, span)];
  static const uint8_t weather_type[] = {0, TYPE_WATER, TYPE_FIRE, TYPE_ROCK, TYPE_ICE};
  t->type = (uint8_t)rng_below(rng, NUM_TYPES);
  if (t->type == TYPE_MYSTERY) t->type = TYPE_FAIRY;
  if (styles[t->archetype].weather) t->type = weather_type[styles[t->archetype].weather];
  if (n > 100) r.legend_pct = 30, t->skill = 10;
  t->mega = n > 100 || (n > 40 && rng_percent(rng, diff == DIFF_HARD ? 50 : 30));
  dress(t, t->skill < 4 ? 0 : t->skill < 8 ? 1 : 2, rng);
  t->backdrop = backdrop_for(t->type, rng);
  fill_team(t, &r, rng);
}

uint8_t trainer_loot(const Trainer *t, uint16_t battle, const Team *player, bool mega, uint32_t *rng) {
  uint32_t pct = t->loot_bonus ? 100 : 20u + battle / 2u > 60u ? 60u : 20u + battle / 2u;
  if (!rng_percent(rng, pct)) return ITEM_NONE;
  /* l'objet de forme d'un Pokémon de l'équipe (Orbe Platiné, Plaques…) : souvent, comme les méga-gemmes */
  if (rng_percent(rng, t->loot_bonus ? 60 : 25)) {
    uint8_t useful[TEAM_MAX], n = 0;
    for (int i = 0; i < player->count; i++) {
      uint8_t form = species_form_item(player->mon[i].species, rng);
      bool held = false;
      for (int j = 0; j < player->count; j++) held |= player->mon[j].item == form;
      if (form && !held) useful[n++] = form;
    }
    if (n) return useful[rng_below(rng, n)];
  }
  if (mega) {
    /* la méga-gemme d'un Pokémon de l'équipe : souvent */
    if (rng_percent(rng, t->loot_bonus ? 60 : 20)) {
      uint8_t useful[TEAM_MAX], n = 0;
      for (int i = 0; i < player->count; i++) {
        uint8_t stone = species_stone(player->mon[i].species);
        bool held = false;
        for (int j = 0; j < player->count; j++) held |= player->mon[j].item == stone;
        if (stone && !held) useful[n++] = stone;
      }
      if (n) return useful[rng_below(rng, n)];
    }
    /* ou celle d'un Pokémon qu'on n'a pas (encore) */
    if (rng_percent(rng, t->loot_bonus ? 25 : 8)) {
      uint8_t pick[NUM_ITEMS], n = 0;
      for (uint8_t i = 1; i < NUM_ITEMS; i++)
        if (item_info[i].effect == ITEM_EFFECT_MEGA_STONE) pick[n++] = i;
      if (n) return pick[rng_below(rng, n)];
    }
  }
  uint32_t max_rarity = battle / 15u + t->loot_bonus > 3u ? 3u : battle / 15u + t->loot_bonus;
  uint8_t pick[NUM_ITEMS];
  uint32_t n = 0;
  for (uint8_t i = 1; i < NUM_ITEMS; i++)
    if (item_info[i].rarity <= max_rarity && item_info[i].effect != ITEM_EFFECT_MEGA_STONE && item_info[i].effect != ITEM_EFFECT_FORM_ITEM)
      pick[n++] = i;
  return n ? pick[rng_below(rng, n)] : ITEM_NONE;
}
