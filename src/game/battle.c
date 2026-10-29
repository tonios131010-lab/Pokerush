#include "game/battle.h"
#include <string.h>
#include "core/rng.h"
#include "data.h"

static Side *side_of(Battle *b, int s) { return &b->side[POS_SIDE(s)]; }
static Fighter *mon(Battle *b, int s) { return battle_active(b, s); }
static Volatile *vol(Battle *b, int s) { return battle_vol(b, s); }
static int npos(const Battle *b) { return battle_positions(b); }
static bool foes(int p, int q) { return ((p ^ q) & 1) != 0; }

/* Celui d'en face pour la position p : la cible de l'action en cours (ou son auteur), sinon
 * l'adversaire du même emplacement, ou l'autre s'il est K.O. */
static int vs(Battle *b, int p) {
  if (b->acting >= 0) {
    if (p == b->acting && b->target >= 0) return b->target;
    if (p == b->target) return b->acting;
  }
  int f = (p ^ 1) & (npos(b) - 1);
  if (b->doubles && !mon(b, f)->hp && mon(b, f ^ 2)->hp) f ^= 2;
  return f;
}

/* Un adversaire debout de p a-t-il ce talent ? */
static bool foe_has(Battle *b, int p, uint16_t ability) {
  for (int q = 0; q < npos(b); q++)
    if (foes(p, q) && mon(b, q)->hp && mon(b, q)->ability == ability) return true;
  return false;
}
static uint8_t item_effect(const Fighter *f) { return item_info[f->p.item].effect; }
static uint16_t frac(const Fighter *f, uint16_t d) { return f->max_hp / d ? f->max_hp / d : 1; }
static uint16_t clamp16(uint32_t v) { return (uint16_t)(v > 0xFFFF ? 0xFFFF : v); }

/* Le camp dont l'attaque en cours ignore les talents adverses (Brise Moule, Téra-Voltage,
 * Turbo-Brasier), ou -1. */
static int mold = -1;

static bool has_type(Battle *b, int s, uint8_t t) { return vol(b, s)->type[0] == t || vol(b, s)->type[1] == t; }

/* Talent du Pokémon actif du côté s, tel que le voit l'attaque en cours. */
static uint16_t ab(Battle *b, int s) {
  uint16_t a = mon(b, s)->ability;
  if (mold >= 0 && foes(mold, s) && a != ABILITY_FULL_METAL_BODY && a != ABILITY_SHADOW_SHIELD) return ABILITY_NONE;
  return a;
}

static bool grounded(Battle *b, int s);

/* Niveau de la Résonance de type t qui profite au Pokémon actif du côté s (0 s'il n'est pas de ce type). */
static uint8_t res(Battle *b, int s, uint8_t t) { return has_type(b, s, t) ? side_of(b, s)->resonance[t] : 0; }

const char *const resonance_names_fr[NUM_TYPES] = {
    "ROBUSTESSE", "ÉLAN",  "ESQUIVE", "VENIN",   "ANCRAGE",  "TAILLE",  "ESSAIM", "HANTISE",   "BLINDAGE", "",
    "BRASIER",    "SOURCE", "PHOTOSYNTHÈSE", "SURVOLTÉ", "PRESCIENCE", "GIVRE", "ÉCAILLES", "FOURBERIE", "BÉNÉDICTION"};
const char *const resonance_names_en[NUM_TYPES] = {
    "VIGOR", "MOMENTUM", "EVASION", "VENOM", "ANCHOR", "EDGE", "SWARM", "HAUNT", "PLATING", "",
    "BLAZE", "SPRING", "PHOTOSYNTHESIS", "OVERCHARGE", "FORESIGHT", "FROST", "SCALES", "TRICKERY", "BLESSING"};
const char *const resonance_descs_en[NUM_TYPES] = {
    "Max HP +5 % per level",
    "KOs a foe: ATTACK +1 (+2 at III)",
    "Foe accuracy -5 % per level",
    "Attacking: 10 % per level to poison",
    "Ignores hazards, DEFENSE +5 % per level",
    "More frequent critical hits",
    "Attacking: 10 % per level of SPEED +1",
    "Contact taken: attacker loses 1/16 HP per level",
    "DEF and SP. DEF +5 % per level",
    "",
    "Attacking: 10 % per level to burn",
    "Recovers 1/32 HP per level each turn",
    "Drains 10 % of damage per level",
    "SPEED +10 % per level",
    "SP. DEF +10 % per level",
    "Attacking: 10 % per level of foe SPEED -1",
    "Super effective taken: -10 % per level",
    "Critical hits +25 % damage per level",
    "Each turn: 15 % per level to cure status"};
const char *const resonance_descs_fr[NUM_TYPES] = {
    "PV max +5 % par niveau",
    "Met K.O. : ATTAQUE +1 (+2 au III)",
    "Précision ennemie -5 % par niveau",
    "En attaquant : 10 % par niv. d'empoisonner",
    "Ignore les pièges, DÉFENSE +5 % par niv.",
    "Coups critiques plus fréquents",
    "En attaquant : 10 % par niv. de VIT +1",
    "Contact reçu : l'ennemi perd 1/16 PV par niv.",
    "DÉF. et DÉF. SPÉ. +5 % par niveau",
    "",
    "En attaquant : 10 % par niv. de brûler",
    "Récupère 1/32 des PV par niv. chaque tour",
    "Draine 10 % des dégâts par niveau",
    "VITESSE +10 % par niveau",
    "DÉF. SPÉ. +10 % par niveau",
    "En attaquant : 10 % par niv. de VIT ennemie -1",
    "Super efficace reçu : -10 % par niveau",
    "Coups critiques +25 % de dégâts par niveau",
    "Chaque tour : 15 % par niv. de guérir son statut"};

static bool magic_guard(Battle *b, int s) { return mon(b, s)->ability == ABILITY_MAGIC_GUARD; }

/* ------------------------------------------------------------------------- événements */

static BattleEvent *push(Battle *b, uint8_t kind, int s, uint16_t value) {
  if (b->nevents == BATTLE_EVENTS) return NULL;
  BattleEvent *e = &b->events[b->nevents++];
  e->kind = kind, e->side = (uint8_t)s, e->value = value, e->text[0] = 0;
  return e;
}

static void append(char *dst, size_t *n, const char *s) {
  while (*s && *n < BATTLE_TEXT - 1) dst[(*n)++] = *s++;
}

/* Message : {A} = le Pokémon du côté s (« X ennemi », « Foe X » chez l'adversaire), {B} = celui d'en face,
 * {X} = arg. */
static void say(Battle *b, int s, const char *tpl, const char *arg) {
  BattleEvent *e = push(b, EV_TEXT, s, 0);
  if (!e) return;
  size_t n = 0;
  for (const char *p = tpl; *p; p++) {
    if (p[0] == '{' && p[1] && p[2] == '}') {
      if (p[1] == 'X') {
        append(e->text, &n, arg);
      } else {
        int who = p[1] == 'A' ? s : vs(b, s);
        if (lang && POS_SIDE(who) == SIDE_FOE) append(e->text, &n, "Foe "); /* « Foe CHARIZARD » comme sur GBA */
        append(e->text, &n, species_names[mon(b, who)->p.species]);
        if (!lang && POS_SIDE(who) == SIDE_FOE) append(e->text, &n, " ennemi");
      }
      p += 2;
    } else if (n < BATTLE_TEXT - 1) {
      e->text[n++] = *p;
    }
  }
  e->text[n] = 0;
}

static void fail(Battle *b, int s) { say(b, s, L("Mais cela échoue !", "But it failed!"), ""); }
static void ability_says(Battle *b, int s, const char *tpl) { say(b, s, tpl, ability_names[mon(b, s)->ability]); }

/* ------------------------------------------------------------------------- PV */

static void lose_item(Battle *b, int s) {
  mon(b, s)->p.item = ITEM_NONE;
  mon(b, s)->item_lost = true;
}

static void heal(Battle *b, int s, uint16_t amount) {
  Fighter *f = mon(b, s);
  if (!f->hp || f->hp == f->max_hp) return;
  f->hp = (uint16_t)(f->hp + amount > f->max_hp ? f->max_hp : f->hp + amount);
  push(b, EV_HP, s, f->hp);
}

static bool change_stage(Battle *b, int s, int stage, int delta, bool by_foe);
static bool confuse(Battle *b, int s);

static void check_berry(Battle *b, int s) {
  Fighter *f = mon(b, s);
  if (f->hp && f->hp * 2 <= f->max_hp && item_effect(f) == ITEM_EFFECT_SITRUS_BERRY &&
      !foe_has(b, s, ABILITY_UNNERVE)) {
    say(b, s, L("{A} mange sa\n{X} !", "{A} ate its\n{X}!"), item_names[f->p.item]);
    lose_item(b, s);
    heal(b, s, frac(f, 4));
  }
  if (f->hp && f->hp * 4 <= f->max_hp && item_effect(f) == ITEM_EFFECT_PINCH_BERRY &&
      !foe_has(b, s, ABILITY_UNNERVE)) {
    uint8_t stat = (uint8_t)item_info[f->p.item].type; /* STAT_ATK..STAT_SPD */
    say(b, s, L("{A} mange sa\n{X} !", "{A} ate its\n{X}!"), item_names[f->p.item]);
    lose_item(b, s);
    change_stage(b, s, stat - 1, 1, false);
  }
}

/* Perte de PV directe (pas le clone) ; renvoie les PV vraiment perdus. */
static uint16_t hurt(Battle *b, int s, uint16_t dmg) {
  Fighter *f = mon(b, s);
  uint16_t before = f->hp;
  if (dmg > f->hp) dmg = f->hp;
  f->hp = (uint16_t)(f->hp - dmg);
  push(b, EV_HP, s, f->hp);
  check_berry(b, s);
  /* Colérique, Courroupace : passer sous la moitié des PV */
  if ((f->ability == ABILITY_BERSERK || f->ability == ABILITY_ANGER_SHELL) && f->hp && before * 2 > f->max_hp && f->hp * 2 <= f->max_hp)
    vol(b, s)->berserk = true;
  return dmg;
}

/* Annonce le K.O. une seule fois ; vrai si le Pokémon est K.O. */
static bool fainted(Battle *b, int s) {
  Volatile *v = vol(b, s);
  if (mon(b, s)->hp) return false;
  if (!v->fainted) {
    v->fainted = true;
    say(b, s, L("{A}\nest K.O. !", "{A}\nfainted!"), "");
    push(b, EV_FAINT, s, 0);
  }
  return true;
}

static bool team_down(const Side *sd) {
  for (int i = 0; i < sd->count; i++)
    if (sd->team[i].hp) return false;
  return true;
}

static void update_result(Battle *b) {
  if (b->result) return;
  if (team_down(&b->side[SIDE_PLAYER])) b->result = BATTLE_LOST;
  else if (team_down(&b->side[SIDE_FOE])) b->result = BATTLE_WON;
}

/* ------------------------------------------------------------------------- météo, statuts, stats */

static uint8_t weather(Battle *b) {
  for (int s = 0; s < npos(b); s++) {
    uint16_t a = mon(b, s)->ability;
    if (mon(b, s)->hp && (a == ABILITY_CLOUD_NINE || a == ABILITY_AIR_LOCK)) return WEATHER_NONE;
  }
  return b->weather;
}

/* La météo telle que la vit le Pokémon s (Méga Soleil : toujours au soleil pour ses attaques). */
static uint8_t weather_for(Battle *b, int s) {
  return mon(b, s)->ability == ABILITY_MEGA_SOL ? WEATHER_SUN : weather(b);
}

static const char *const weather_start[2][5] = {
    {"", "Il commence à pleuvoir !", "Le soleil brille !", "Une tempête de sable\nse lève !", "Il commence à neiger !"},
    {"", "It started to rain!", "The sunlight got bright!", "A sandstorm\nbrewed!", "It started to snow!"}};
static const char *const weather_end[2][5] = {
    {"", "La pluie s'arrête.", "Le soleil s'affaiblit.", "La tempête de sable\ns'arrête.", "La neige s'arrête."},
    {"", "The rain stopped.", "The sunlight faded.", "The sandstorm\nsubsided.", "The snow stopped."}};

static bool set_weather(Battle *b, int s, uint8_t w) {
  static const uint8_t rock[] = {0, TYPE_WATER, TYPE_FIRE, TYPE_ROCK, TYPE_ICE}; /* Roche Humide, Chaude… */
  if (b->weather == w) return false;
  const Fighter *f = mon(b, s);
  b->weather = w, b->weather_turns = item_effect(f) == ITEM_EFFECT_WEATHER_ROCK && item_info[f->p.item].type == rock[w] ? 8 : 5;
  say(b, s, weather_start[lang][w], "");
  return true;
}

static const char *const status_given[2][7] = {
    {"", "{A} s'endort !", "{A}\nest empoisonné !", "{A} est brûlé !", "{A} est gelé !", "{A} est paralysé !\nIl aura du mal à attaquer !", "{A} est gravement\nempoisonné !"},
    {"", "{A} fell asleep!", "{A}\nwas poisoned!", "{A} was burned!", "{A} was frozen solid!", "{A} is paralyzed!\nIt may be unable to move!", "{A} was badly\npoisoned!"}};

static void set_status(Battle *b, int s, uint8_t st) {
  mon(b, s)->status = st;
  push(b, EV_STATUS, s, st);
}

/* Le statut st peut-il toucher le Pokémon du côté s ? from : le côté qui l'inflige. */
static bool status_allowed(Battle *b, int s, uint8_t st, int from) {
  Fighter *f = mon(b, s);
  uint16_t a = ab(b, s);
  if (f->status || !f->hp) return false;
  if (a == ABILITY_PURIFYING_SALT || a == ABILITY_COMATOSE) return false;
  if (a == ABILITY_LEAF_GUARD && weather(b) == WEATHER_SUN) return false;
  if (b->terrain == TERRAIN_MISTY && grounded(b, s)) return false;
  switch (st) {
    case STATUS_SLEEP:
      return a != ABILITY_INSOMNIA && a != ABILITY_VITAL_SPIRIT && a != ABILITY_SWEET_VEIL &&
             !(b->terrain == TERRAIN_ELECTRIC && grounded(b, s));
    case STATUS_POISON:
    case STATUS_TOXIC:
      return a != ABILITY_IMMUNITY && a != ABILITY_PASTEL_VEIL &&
             ((!has_type(b, s, TYPE_POISON) && !has_type(b, s, TYPE_STEEL)) ||
              (from != s && mon(b, from)->ability == ABILITY_CORROSION));
    case STATUS_BURN:
      return !has_type(b, s, TYPE_FIRE) && a != ABILITY_THERMAL_EXCHANGE && a != ABILITY_WATER_VEIL && a != ABILITY_WATER_BUBBLE;
    case STATUS_FREEZE: return !has_type(b, s, TYPE_ICE) && weather(b) != WEATHER_SUN && a != ABILITY_MAGMA_ARMOR;
    case STATUS_PARALYSIS: return !has_type(b, s, TYPE_ELECTRIC) && a != ABILITY_LIMBER;
  }
  return false;
}

static void cure_with_lum(Battle *b, int s) {
  Fighter *f = mon(b, s);
  if (item_effect(f) != ITEM_EFFECT_LUM_BERRY || (!f->status && !vol(b, s)->confused)) return;
  if (foe_has(b, s, ABILITY_UNNERVE)) return;
  say(b, s, L("{A} mange sa\n{X} et guérit !", "{A} ate its\n{X} and was cured!"), item_names[f->p.item]);
  lose_item(b, s);
  set_status(b, s, STATUS_NONE);
  vol(b, s)->confused = 0;
}

static bool give_status(Battle *b, int s, uint8_t st, int from) {
  if (!status_allowed(b, s, st, from)) return false;
  Fighter *f = mon(b, s);
  set_status(b, s, st);
  /* sommeil : décompté avant d'agir, 2..4 donne 1 à 3 tours endormi */
  f->status_turns = st == STATUS_SLEEP ? (uint8_t)(2 + rng_below(&b->rng, 3)) : st == STATUS_TOXIC ? 1 : 0;
  say(b, s, status_given[lang][st], "");
  if (from != s && mon(b, from)->ability == ABILITY_POISON_PUPPETEER && (st == STATUS_POISON || st == STATUS_TOXIC)) confuse(b, s);
  /* Synchro renvoie brûlure, paralysie et poison */
  if (from != s && f->ability == ABILITY_SYNCHRONIZE && st != STATUS_SLEEP && st != STATUS_FREEZE)
    give_status(b, from, st == STATUS_TOXIC ? STATUS_POISON : st, s);
  cure_with_lum(b, s);
  return true;
}

static bool confuse(Battle *b, int s) {
  Volatile *v = vol(b, s);
  if (v->confused || !mon(b, s)->hp || ab(b, s) == ABILITY_OWN_TEMPO) return false;
  v->confused = (uint8_t)(2 + rng_below(&b->rng, 4));
  say(b, s, L("{A} est confus !", "{A} became confused!"), "");
  cure_with_lum(b, s);
  return true;
}

static const char *const stage_names[2][NUM_STAGES] = {
    {"L'ATTAQUE", "La DÉFENSE", "La VITESSE", "L'ATQ. SPÉ.", "La DÉF. SPÉ.", "La PRÉCISION", "L'ESQUIVE"},
    {"ATTACK", "DEFENSE", "SPEED", "SP. ATK", "SP. DEF", "accuracy", "evasiveness"}};

/* Change un niveau de stat ; by_foe : imposé par l'adversaire (Corps Sain, Miroir Armure…). */
static bool change_stage(Battle *b, int s, int stage, int delta, bool by_foe) {
  Fighter *f = mon(b, s);
  Volatile *v = vol(b, s);
  if (!f->hp || !delta) return false;
  uint16_t a = by_foe ? ab(b, s) : f->ability;
  if (a == ABILITY_CONTRARY) delta = -delta;
  if (f->ability == ABILITY_SIMPLE) delta *= 2;
  if (by_foe && delta < 0) {
    if (v->substitute) return false;
    if (item_effect(f) == ITEM_EFFECT_CLEAR_AMULET) {
      say(b, s, L("{X} de {A}\nempêche la baisse !", "{A}'s {X}\nprevents stat loss!"), item_names[f->p.item]);
      return false;
    }
    if (a == ABILITY_CLEAR_BODY || a == ABILITY_FULL_METAL_BODY || a == ABILITY_WHITE_SMOKE || (a == ABILITY_HYPER_CUTTER && stage == STAGE_ATK) ||
        (a == ABILITY_FLOWER_VEIL && has_type(b, s, TYPE_GRASS)) ||
        (a == ABILITY_BIG_PECKS && stage == STAGE_DEF) || (a == ABILITY_KEEN_EYE && stage == STAGE_ACC)) {
      say(b, s, L("{X} de {A}\nempêche la baisse !", "{A}'s {X}\nprevents stat loss!"), ability_names[a]);
      return false;
    }
    if (a == ABILITY_MIRROR_ARMOR) {
      say(b, s, L("MIROIR ARMURE de {A}\nrenvoie la baisse !", "{A}'s MIRROR ARMOR\nreflected the stat drop!"), "");
      return change_stage(b, vs(b, s), stage, delta, false);
    }
  }
  int old = v->stage[stage], now = old + delta;
  now = now > 6 ? 6 : now < -6 ? -6 : now;
  if (now == old) {
    say(b, s, delta > 0 ? L("{X} de {A}\nne peut plus monter !", "{A}'s {X}\nwon't go higher!") : L("{X} de {A}\nne peut plus baisser !", "{A}'s {X}\nwon't go lower!"), stage_names[lang][stage]);
    return false;
  }
  v->stage[stage] = (int8_t)now;
  say(b, s, delta >= 3 ? L("{X} de {A}\naugmente énormément !", "{A}'s {X}\nrose drastically!") : delta >= 2 ? L("{X} de {A}\naugmente beaucoup !", "{A}'s {X}\nrose sharply!") :
            delta > 0 ? L("{X} de {A}\naugmente !", "{A}'s {X}\nrose!") : delta <= -2 ? L("{X} de {A}\nbaisse beaucoup !", "{A}'s {X}\nharshly fell!") : L("{X} de {A}\nbaisse !", "{A}'s {X}\nfell!"),
      stage_names[lang][stage]);
  if (delta < 0 && by_foe && (f->ability == ABILITY_DEFIANT || f->ability == ABILITY_COMPETITIVE)) {
    ability_says(b, s, L("{X} de {A}\ns'active !", "{A}'s\n{X} activated!"));
    change_stage(b, s, f->ability == ABILITY_DEFIANT ? STAGE_ATK : STAGE_SPA, 2, false);
  }
  if (delta < 0 && item_effect(f) == ITEM_EFFECT_WHITE_HERB) {
    for (int k = 0; k < NUM_STAGES; k++)
      if (v->stage[k] < 0) v->stage[k] = 0;
    say(b, s, L("{A} retrouve ses stats\ngrâce à {X} !", "{A} restored its\nstats with its {X}!"), item_names[f->p.item]);
    lose_item(b, s);
  }
  return true;
}

/* Changements de stats de plusieurs stats d'un coup : ATQ, DÉF, VIT, ATQ.SPÉ, DÉF.SPÉ. */
typedef struct {
  uint8_t effect;
  int8_t d[5];
} Boost;
static const Boost boosts[] = {
    {EFFECT_U_ATK_UP, {1, 0, 0, 0, 0}},       {EFFECT_U_DEF_UP, {0, 1, 0, 0, 0}},       {EFFECT_U_SPA_UP, {0, 0, 0, 1, 0}},
    {EFFECT_U_SPE_UP, {0, 0, 1, 0, 0}},       {EFFECT_U_ATK_UP2, {2, 0, 0, 0, 0}},      {EFFECT_U_DEF_UP2, {0, 2, 0, 0, 0}},
    {EFFECT_U_SPA_UP2, {0, 0, 0, 2, 0}},      {EFFECT_U_SPD_UP2, {0, 0, 0, 0, 2}},      {EFFECT_U_SPE_UP2, {0, 0, 2, 0, 0}},
    {EFFECT_U_ATK_SPE_UP, {1, 0, 1, 0, 0}},   {EFFECT_U_SPA_SPD_UP, {0, 0, 0, 1, 1}},   {EFFECT_U_ATK_DEF_UP, {1, 1, 0, 0, 0}},
    {EFFECT_U_DEF_SPD_UP, {0, 1, 0, 0, 1}},   {EFFECT_U_ATK_SPA_UP, {1, 0, 0, 1, 0}},   {EFFECT_U_SPA_SPD_SPE_UP, {0, 0, 1, 1, 1}},
    {EFFECT_U_ALL_UP, {1, 1, 1, 1, 1}},       {EFFECT_SHELL_SMASH, {2, -1, 2, 2, -1}},  {EFFECT_U_SPA_DOWN, {0, 0, 0, -1, 0}},
    {EFFECT_U_SPA_DOWN2, {0, 0, 0, -2, 0}},   {EFFECT_U_SPE_DOWN, {0, 0, -1, 0, 0}},    {EFFECT_U_ATK_DEF_DOWN, {-1, -1, 0, 0, 0}},
    {EFFECT_U_DEF_SPD_DOWN, {0, -1, 0, 0, -1}}, {EFFECT_U_DEF_DOWN, {0, -1, 0, 0, 0}},
};

static const Boost *boost_of(uint8_t e) {
  for (unsigned i = 0; i < sizeof boosts / sizeof boosts[0]; i++)
    if (boosts[i].effect == e) return &boosts[i];
  return NULL;
}

static bool apply_boost(Battle *b, int s, const Boost *bo, int scale) {
  bool any = false;
  /* les baisses d'abord (Exuviation) : Herbe Blanche les annule ensuite */
  for (int pass = 0; pass < 2; pass++)
    for (int k = 0; k < 5; k++)
      if ((pass == 0) == (bo->d[k] < 0) && bo->d[k]) any |= change_stage(b, s, k, bo->d[k] * scale, false);
  return any;
}

/* Baisses de stats de la cible : effet -> (niveau, delta). */
static bool target_drop(uint8_t e, int *stage, int *delta) {
  static const uint8_t list[][3] = {
      {EFFECT_T_ATK_DOWN, STAGE_ATK, 1},  {EFFECT_T_DEF_DOWN, STAGE_DEF, 1},  {EFFECT_T_SPA_DOWN, STAGE_SPA, 1},
      {EFFECT_T_SPD_DOWN, STAGE_SPD, 1},  {EFFECT_T_SPE_DOWN, STAGE_SPE, 1},  {EFFECT_T_ACC_DOWN, STAGE_ACC, 1},
      {EFFECT_T_ATK_DOWN2, STAGE_ATK, 2}, {EFFECT_T_DEF_DOWN2, STAGE_DEF, 2}, {EFFECT_T_SPD_DOWN2, STAGE_SPD, 2},
      {EFFECT_T_SPE_DOWN2, STAGE_SPE, 2}, {EFFECT_T_SPA_DOWN2, STAGE_SPA, 2},
  };
  for (unsigned i = 0; i < sizeof list / sizeof list[0]; i++)
    if (list[i][0] == e) return *stage = list[i][1], *delta = -list[i][2], true;
  return false;
}

/* ------------------------------------------------------------------------- calculs */

static uint32_t staged(uint32_t v, int stage) { return stage >= 0 ? v * (2 + stage) / 2 : v * 2 / (2 - stage); }

static int best_stat(const Fighter *f) {
  int best = STAT_ATK;
  for (int k = STAT_ATK; k < NUM_STATS; k++)
    if (f->stat[k] > f->stat[best]) best = k;
  return best;
}

/* Paléosynthèse au soleil, Charge Quantique sur Champ Électrifié : la meilleure stat monte. */
static bool paradox(Battle *b, int s) {
  uint16_t x = mon(b, s)->ability;
  return (x == ABILITY_PROTOSYNTHESIS && weather(b) == WEATHER_SUN) || (x == ABILITY_QUARK_DRIVE && b->terrain == TERRAIN_ELECTRIC);
}

static uint32_t speed(Battle *b, int s) {
  Fighter *f = mon(b, s);
  uint32_t v = staged(f->stat[STAT_SPE], vol(b, s)->stage[STAGE_SPE]);
  uint8_t w = weather(b);
  uint16_t a = f->ability;
  if (f->status == STATUS_PARALYSIS && a != ABILITY_QUICK_FEET) v /= 2;
  if (f->status && a == ABILITY_QUICK_FEET) v = v * 3 / 2;
  if (a == ABILITY_SLOW_START && vol(b, s)->on_field < 5) v /= 2;
  if (item_effect(f) == ITEM_EFFECT_CHOICE_SPE) v = v * 3 / 2;
  if ((w == WEATHER_RAIN && a == ABILITY_SWIFT_SWIM) || (w == WEATHER_SUN && a == ABILITY_CHLOROPHYLL) ||
      (w == WEATHER_SAND && a == ABILITY_SAND_RUSH) || (a == ABILITY_UNBURDEN && f->item_lost) ||
      (w == WEATHER_SNOW && a == ABILITY_SLUSH_RUSH) || (b->terrain == TERRAIN_ELECTRIC && a == ABILITY_SURGE_SURFER))
    v *= 2;
  if (paradox(b, s) && best_stat(f) == STAT_SPE) v = v * 3 / 2;
  if (side_of(b, s)->tailwind) v *= 2;
  v = v * (10u + res(b, s, TYPE_ELECTRIC)) / 10;
  return v;
}

/* Efficacité ×16 du type t contre les types (t1, t2) : 0, 4, 8, 16, 32 ou 64. */
static uint8_t type_mult(uint8_t t, uint8_t t1, uint8_t t2) {
  static const uint8_t mult[4] = {0, 2, 4, 8};
  return (uint8_t)(mult[type_effect(t, t1)] * (t2 == t1 ? 4 : mult[type_effect(t, t2)]));
}

uint8_t battle_effectiveness(uint8_t type, const Fighter *f) {
  const SpeciesInfo *sp = &species_info[f->p.species];
  return type_mult(type, (uint8_t)sp->type1, (uint8_t)sp->type2);
}

static bool grounded(Battle *b, int s) {
  uint16_t a = ab(b, s);
  return !has_type(b, s, TYPE_FLYING) && a != ABILITY_LEVITATE && a != ABILITY_EELEVATE &&
         item_effect(mon(b, s)) != ITEM_EFFECT_AIR_BALLOON;
}

static const MoveInfo struggle = {.power = 50, .accuracy = 0, .type = TYPE_MYSTERY, .pp = 1, .priority = 8, .contact = 1};

/* Le type réel de l'attaque (Peau Céleste, Peau Féerique, Draconisation, Hydrovoix). */
static uint8_t move_type(Battle *b, int a, const MoveInfo *m, bool *boosted) {
  uint8_t t = m->type;
  const Fighter *f = mon(b, a);
  uint16_t x = f->ability;
  *boosted = false;
  if (m->effect == EFFECT_JUDGMENT && f->p.item) { /* Jugement, Techno-Buster, Massue Liane… : le type de l'objet */
    const ItemInfo *it = &item_info[f->p.item];
    if ((it->effect == ITEM_EFFECT_FORM_ITEM || it->effect == ITEM_EFFECT_TYPE_BOOST) && it->type != TYPE_MYSTERY) t = (uint8_t)it->type;
  }
  if (x == ABILITY_NORMALIZE && m != &struggle) {
    *boosted = true;
    return TYPE_NORMAL;
  }
  if (t == TYPE_NORMAL && m != &struggle) {
    uint8_t to = x == ABILITY_AERILATE ? TYPE_FLYING : x == ABILITY_PIXILATE ? TYPE_FAIRY : x == ABILITY_DRAGONIZE ? TYPE_DRAGON :
                 x == ABILITY_REFRIGERATE ? TYPE_ICE : x == ABILITY_GALVANIZE ? TYPE_ELECTRIC : t;
    if (to != t) t = to, *boosted = true;
  }
  if (m->sound && x == ABILITY_LIQUID_VOICE) t = TYPE_WATER;
  return t;
}

/* Efficacité de l'attaque contre le Pokémon actif d, talents compris. */
static uint8_t effectiveness(Battle *b, int a, uint8_t type, const MoveInfo *m) {
  int d = vs(b, a);
  Volatile *vd = vol(b, d);
  uint8_t e;
  if (type == TYPE_MYSTERY) return 16;
  uint8_t t1 = vd->type[0], t2 = vd->type[1];
  if (mon(b, a)->ability == ABILITY_SCRAPPY && (type == TYPE_NORMAL || type == TYPE_FIGHTING)) {
    if (t1 == TYPE_GHOST) t1 = t2;
    if (t2 == TYPE_GHOST) t2 = t1;
    if (t1 == TYPE_GHOST) return 16;
  }
  e = type_mult(type, t1, t2);
  if (m->effect == EFFECT_FREEZE_DRY && (t1 == TYPE_WATER || t2 == TYPE_WATER)) e = (uint8_t)(e * 4); /* l'Eau devient faible */
  if (type == TYPE_GROUND && !grounded(b, d)) e = 0;
  return e > 64 ? 64 : e;
}

static bool is_contact(Battle *b, int a, const MoveInfo *m) {
  return m->contact && mon(b, a)->ability != ABILITY_LONG_REACH && item_effect(mon(b, a)) != ITEM_EFFECT_PROTECTIVE_PADS &&
         !(m->punch && item_effect(mon(b, a)) == ITEM_EFFECT_PUNCHING_GLOVE);
}

/* Dégâts fixes : ni coup critique, ni Amour Filial. */
static bool fixed_damage(uint8_t e) { return e == EFFECT_FIXED_LEVEL || e == EFFECT_FIXED_40 || e == EFFECT_SUPER_FANG; }

/* Baie qui divise par deux un coup super efficace de son type (Baie Zalis : tout coup Normal). */
static bool resist_berry(Battle *b, int d, uint8_t type, uint8_t eff) {
  const Fighter *D = mon(b, d);
  return item_effect(D) == ITEM_EFFECT_RESIST_BERRY && item_info[D->p.item].type == type && (eff > 16 || type == TYPE_NORMAL) &&
         !foe_has(b, d, ABILITY_UNNERVE);
}

/* Effets « secondaires » (que Sans Limite supprime et que Sérénité double) : statuts, peur, baisses
 * de la cible, hausses du lanceur. */
static bool is_secondary(uint8_t e) { return e >= EFFECT_BURN && e <= EFFECT_U_ALL_UP; }
static bool sheer(Battle *b, int a, const MoveInfo *m) {
  return mon(b, a)->ability == ABILITY_SHEER_FORCE && is_secondary(m->effect) && !move_is_status(m);
}

static int fainted_allies(Battle *b, int s) {
  int n = 0;
  for (int i = 0; i < side_of(b, s)->count; i++) n += !side_of(b, s)->team[i].hp;
  return n > 5 ? 5 : n;
}

static bool can_lose_item(Battle *b, int s) {
  Fighter *f = mon(b, s);
  return f->p.item && item_effect(f) != ITEM_EFFECT_MEGA_STONE;
}

static uint16_t damage(Battle *b, int a, const MoveInfo *m, bool crit, uint8_t roll, uint8_t *eff) {
  int d = vs(b, a);
  Fighter *A = mon(b, a), *D = mon(b, d);
  Volatile *va = vol(b, a), *vd = vol(b, d);
  uint8_t ie = item_effect(A), w = weather_for(b, a), e = m->effect;
  uint16_t aa = A->ability, da = ab(b, d);
  bool boosted, spec_atk = m->special; /* Choc Psy : attaque spéciale contre la DÉFENSE */
  uint8_t type = move_type(b, a, m, &boosted);
  *eff = effectiveness(b, a, type, m);
  if (!*eff) return 0;
  if (e == EFFECT_FIXED_LEVEL) return 50;
  if (e == EFFECT_FIXED_40) return 40;
  if (e == EFFECT_SUPER_FANG) return (uint16_t)(D->hp / 2 ? D->hp / 2 : 1);

  /* puissance */
  uint32_t power = m->power;
  if (e == EFFECT_FACADE && A->status) power *= 2;
  if (e == EFFECT_ERUPTION) power = 150u * A->hp / A->max_hp ? 150u * A->hp / A->max_hp : 1;
  if (e == EFFECT_SOLAR && w != WEATHER_NONE && w != WEATHER_SUN) power /= 2;
  if (e == EFFECT_HEX && D->status) power *= 2;
  if (e == EFFECT_ACROBATICS && !A->p.item) power *= 2;
  if (e == EFFECT_KNOCK_OFF && can_lose_item(b, d)) power = power * 3 / 2;
  if (e == EFFECT_COLLISION && *eff > 16) power = power * 4 / 3;
  if (A->hp * 3 <= A->max_hp &&
      ((type == TYPE_GRASS && aa == ABILITY_OVERGROW) || (type == TYPE_FIRE && aa == ABILITY_BLAZE) ||
       (type == TYPE_WATER && aa == ABILITY_TORRENT) || (type == TYPE_BUG && aa == ABILITY_SWARM)))
    power = power * 3 / 2;
  if (aa == ABILITY_TECHNICIAN && power <= 60) power = power * 3 / 2;
  if ((aa == ABILITY_IRON_FIST && m->punch) || boosted) power = power * 6 / 5;
  if ((aa == ABILITY_STRONG_JAW && m->bite) || (aa == ABILITY_MEGA_LAUNCHER && m->pulse) ||
      (aa == ABILITY_SHARPNESS && m->slice) || (aa == ABILITY_FIRE_MANE && type == TYPE_FIRE))
    power = power * 3 / 2;
  if (aa == ABILITY_TOUGH_CLAWS && is_contact(b, a, m)) power = power * 13 / 10;
  if (((aa == ABILITY_STEELWORKER || aa == ABILITY_STEELY_SPIRIT) && type == TYPE_STEEL) ||
      (aa == ABILITY_DRAGONS_MAW && type == TYPE_DRAGON) || (aa == ABILITY_ROCKY_PAYLOAD && type == TYPE_ROCK) ||
      (aa == ABILITY_TOXIC_BOOST && !spec_atk && (A->status == STATUS_POISON || A->status == STATUS_TOXIC)) ||
      (aa == ABILITY_FLARE_BOOST && spec_atk && A->status == STATUS_BURN))
    power = power * 3 / 2;
  if ((aa == ABILITY_TRANSISTOR && type == TYPE_ELECTRIC) || (aa == ABILITY_PUNK_ROCK && m->sound)) power = power * 13 / 10;
  if (aa == ABILITY_WATER_BUBBLE && type == TYPE_WATER) power *= 2;
  if (m->punch && ie == ITEM_EFFECT_PUNCHING_GLOVE) power = power * 11 / 10;
  /* Champs : +30 % pour le lanceur au sol ; Brumeux : Dragon /2 ; Herbu : Séisme /2 */
  if (grounded(b, a) && ((b->terrain == TERRAIN_ELECTRIC && type == TYPE_ELECTRIC) ||
                         (b->terrain == TERRAIN_GRASSY && type == TYPE_GRASS) || (b->terrain == TERRAIN_PSYCHIC && type == TYPE_PSYCHIC)))
    power = power * 13 / 10;
  if (grounded(b, d) && b->terrain == TERRAIN_MISTY && type == TYPE_DRAGON) power /= 2;
  if (b->terrain == TERRAIN_GRASSY && (m == &move_info[MOVE_EARTHQUAKE] || m == &move_info[MOVE_BULLDOZE])) power /= 2;
  if (aa == ABILITY_RECKLESS && (e >= EFFECT_RECOIL4 && e <= EFFECT_CRASH)) power = power * 6 / 5;
  if (sheer(b, a, m)) power = power * 13 / 10;
  if (aa == ABILITY_SAND_FORCE && weather(b) == WEATHER_SAND &&
      (type == TYPE_ROCK || type == TYPE_GROUND || type == TYPE_STEEL))
    power = power * 13 / 10;
  if (aa == ABILITY_ANALYTIC && vd->moved) power = power * 13 / 10;
  for (int s = 0; s < npos(b); s++) {
    uint16_t x = mon(b, s)->ability;
    if (mon(b, s)->hp && ((x == ABILITY_DARK_AURA && type == TYPE_DARK) || (x == ABILITY_FAIRY_AURA && type == TYPE_FAIRY))) {
      power = power * 4 / 3;
      break;
    }
  }
  if (aa == ABILITY_SUPREME_OVERLORD) power = power * (10 + (uint32_t)fainted_allies(b, a)) / 10;
  if ((ie == ITEM_EFFECT_TYPE_BOOST || ie == ITEM_EFFECT_FORM_ITEM) && item_info[A->p.item].type == type) power = power * 6 / 5;
  if (ie == ITEM_EFFECT_METRONOME && va->streak) power = power * (10u + 2u * (va->streak > 5 ? 5u : va->streak)) / 10;
  if ((ie == ITEM_EFFECT_MUSCLE_BAND && !spec_atk) || (ie == ITEM_EFFECT_WISE_GLASSES && spec_atk)) power = power * 11 / 10;
  if (da == ABILITY_PURIFYING_SALT && type == TYPE_GHOST) power /= 2;

  /* attaque et défense */
  const Fighter *src = e == EFFECT_FOUL_PLAY ? D : A;
  int sa, sd;
  uint32_t atk;
  if (e == EFFECT_BODY_PRESS) atk = A->stat[STAT_DEF], sa = va->stage[STAGE_DEF];
  else atk = src->stat[spec_atk ? STAT_SPA : STAT_ATK], sa = (e == EFFECT_FOUL_PLAY ? vd : va)->stage[spec_atk ? STAGE_SPA : STAGE_ATK];
  bool phys_def = !spec_atk || e == EFFECT_PSYSHOCK;
  uint32_t def = D->stat[phys_def ? STAT_DEF : STAT_SPD];
  sd = vd->stage[phys_def ? STAGE_DEF : STAGE_SPD];
  if (da == ABILITY_UNAWARE) sa = 0;
  if (aa == ABILITY_UNAWARE) sd = 0;
  if (crit) sa = sa < 0 ? 0 : sa, sd = sd > 0 ? 0 : sd;
  atk = staged(atk, sa);
  def = staged(def, sd);
  if (!spec_atk) {
    if (aa == ABILITY_HUGE_POWER || aa == ABILITY_PURE_POWER) atk *= 2;
    if ((aa == ABILITY_GUTS && A->status) || aa == ABILITY_HUSTLE) atk = atk * 3 / 2;
    if (ie == ITEM_EFFECT_CHOICE_ATK || aa == ABILITY_GORILLA_TACTICS) atk = atk * 3 / 2;
    if (aa == ABILITY_ORICHALCUM_PULSE && weather(b) == WEATHER_SUN) atk = atk * 4 / 3;
    if (aa == ABILITY_FLOWER_GIFT && w == WEATHER_SUN) atk = atk * 3 / 2;
    if (aa == ABILITY_SLOW_START && va->on_field < 5) atk /= 2;
  } else {
    if (ie == ITEM_EFFECT_CHOICE_SPA) atk = atk * 3 / 2;
    if (aa == ABILITY_SOLAR_POWER && w == WEATHER_SUN) atk = atk * 3 / 2;
    if (aa == ABILITY_HADRON_ENGINE) atk = atk * 4 / 3;
  }
  if (paradox(b, a) && best_stat(A) == (spec_atk ? STAT_SPA : STAT_ATK)) atk = atk * 13 / 10;
  if (aa == ABILITY_DEFEATIST && A->hp * 2 <= A->max_hp) atk /= 2;
  /* talents du Fléau : la stat de l'autre baisse */
  if ((!spec_atk && D->ability == ABILITY_TABLETS_OF_RUIN) || (spec_atk && D->ability == ABILITY_VESSEL_OF_RUIN)) atk = atk * 3 / 4;
  if (da == ABILITY_THICK_FAT && (type == TYPE_FIRE || type == TYPE_ICE)) atk /= 2;
  if (type == TYPE_FIRE && va->flash_fire) atk = atk * 3 / 2;
  if (phys_def) {
    if (da == ABILITY_MARVEL_SCALE && D->status) def = def * 3 / 2;
    if (weather(b) == WEATHER_SNOW && has_type(b, d, TYPE_ICE)) def = def * 3 / 2;
    if (da == ABILITY_FUR_COAT) def *= 2;
    if (da == ABILITY_GRASS_PELT && b->terrain == TERRAIN_GRASSY) def = def * 3 / 2;
  } else {
    if (da == ABILITY_FLOWER_GIFT && weather(b) == WEATHER_SUN) def = def * 3 / 2;
    if (weather(b) == WEATHER_SAND && has_type(b, d, TYPE_ROCK)) def = def * 3 / 2;
    if (item_effect(D) == ITEM_EFFECT_ASSAULT_VEST) def = def * 3 / 2;
  }
  if (paradox(b, d) && best_stat(D) == (phys_def ? STAT_DEF : STAT_SPD)) def = def * 13 / 10;
  /* Résonances défensives : Acier, Sol, Psy */
  def = def * (20u + res(b, d, TYPE_STEEL) + (phys_def ? res(b, d, TYPE_GROUND) : 2u * res(b, d, TYPE_PSYCHIC))) / 20;
  if ((phys_def && aa == ABILITY_SWORD_OF_RUIN) || (!phys_def && aa == ABILITY_BEADS_OF_RUIN)) def = def * 3 / 4;
  if (e == EFFECT_EXPLODE) def /= 2;
  if (!def) def = 1;

  uint32_t dmg = 22 * power * atk / def / 50;
  const Side *ds = side_of(b, d);
  if (!crit && aa != ABILITY_INFILTRATOR && ((phys_def && !spec_atk && ds->reflect) || (spec_atk && ds->light_screen)))
    dmg /= 2;
  uint8_t ww = ie == ITEM_EFFECT_UTILITY_UMBRELLA || item_effect(D) == ITEM_EFFECT_UTILITY_UMBRELLA ? WEATHER_NONE : w;
  if ((ww == WEATHER_RAIN && type == TYPE_WATER) || (ww == WEATHER_SUN && type == TYPE_FIRE)) dmg = dmg * 3 / 2;
  if ((ww == WEATHER_RAIN && type == TYPE_FIRE) || (ww == WEATHER_SUN && type == TYPE_WATER)) dmg /= 2;
  dmg += 2;
  if (crit) dmg = (aa == ABILITY_SNIPER ? dmg * 9 / 4 : dmg * 3 / 2) * (4u + res(b, a, TYPE_DARK)) / 4;
  dmg = dmg * roll / 100;
  if (type == va->type[0] || type == va->type[1]) dmg = aa == ABILITY_ADAPTABILITY ? dmg * 2 : dmg * 3 / 2;
  dmg = dmg * *eff / 16;
  if (A->status == STATUS_BURN && !spec_atk && aa != ABILITY_GUTS && e != EFFECT_FACADE) dmg /= 2;
  if ((da == ABILITY_MULTISCALE || da == ABILITY_SHADOW_SHIELD) && D->hp == D->max_hp) dmg /= 2;
  if ((da == ABILITY_FILTER || da == ABILITY_SOLID_ROCK || D->ability == ABILITY_PRISM_ARMOR) && *eff > 16) dmg = dmg * 3 / 4;
  if (aa == ABILITY_TINTED_LENS && *eff < 16) dmg *= 2;
  if (type == TYPE_FIRE && (da == ABILITY_HEATPROOF || da == ABILITY_WATER_BUBBLE)) dmg /= 2;
  if (type == TYPE_FIRE && da == ABILITY_FLUFFY) dmg *= 2;
  if (type == TYPE_FIRE && da == ABILITY_DRY_SKIN) dmg = dmg * 5 / 4;
  if ((da == ABILITY_FLUFFY && is_contact(b, a, m)) || (da == ABILITY_ICE_SCALES && spec_atk) || (da == ABILITY_PUNK_ROCK && m->sound))
    dmg /= 2;
  if (ie == ITEM_EFFECT_EXPERT_BELT && *eff > 16) dmg = dmg * 6 / 5;
  if (ie == ITEM_EFFECT_LIFE_ORB) dmg = dmg * 13 / 10;
  if (*eff > 16) dmg = dmg * (10u - res(b, d, TYPE_DRAGON)) / 10; /* Résonance Dragon */
  if (resist_berry(b, d, type, *eff)) dmg /= 2;
  if (b->spread) dmg = dmg * 3 / 4; /* duo : attaque qui touche plusieurs Pokémon */
  if (!dmg) dmg = 1;
  return clamp16(dmg);
}

uint16_t battle_estimate_at(const Battle *b, int attacker, int target, uint16_t move, uint8_t roll, uint8_t *effect) {
  Battle *bb = (Battle *)b;
  int saved = mold, acting = bb->acting, aimed = bb->target, spread = bb->spread;
  uint16_t x = mon(bb, attacker)->ability;
  mold = x == ABILITY_MOLD_BREAKER || x == ABILITY_TERAVOLT || x == ABILITY_TURBOBLAZE ? attacker : -1;
  bb->acting = (int8_t)attacker, bb->target = (int8_t)target, bb->spread = 0;
  uint16_t d = damage(bb, attacker, &move_info[move], false, roll, effect);
  mold = saved, bb->acting = (int8_t)acting, bb->target = (int8_t)aimed, bb->spread = (uint8_t)spread;
  return d;
}

uint16_t battle_estimate(const Battle *b, int attacker, uint16_t move, uint8_t roll, uint8_t *effect) {
  Battle *bb = (Battle *)b;
  int acting = bb->acting;
  bb->acting = -1;
  int target = vs(bb, attacker);
  bb->acting = (int8_t)acting;
  return battle_estimate_at(b, attacker, target, move, roll, effect);
}

static bool accurate(Battle *b, int a, const MoveInfo *m, uint16_t move) {
  int d = vs(b, a);
  uint8_t w = weather(b);
  if (!m->accuracy || mon(b, a)->ability == ABILITY_NO_GUARD || mon(b, d)->ability == ABILITY_NO_GUARD) return true;
  if ((move == MOVE_THUNDER || move == MOVE_HURRICANE) && w == WEATHER_RAIN) return true;
  if (move == MOVE_BLIZZARD && w == WEATHER_SNOW) return true;
  int eva = mon(b, a)->ability == ABILITY_KEEN_EYE ? 0 : vol(b, d)->stage[STAGE_EVA];
  int st = vol(b, a)->stage[STAGE_ACC] - eva;
  st = st > 6 ? 6 : st < -6 ? -6 : st;
  uint32_t acc = st >= 0 ? m->accuracy * (3u + (uint32_t)st) / 3 : m->accuracy * 3u / (3u - (uint32_t)st);
  if ((move == MOVE_THUNDER || move == MOVE_HURRICANE) && w == WEATHER_SUN) acc = acc * 5 / 10;
  if (mon(b, a)->ability == ABILITY_HUSTLE && !m->special && m->power) acc = acc * 4 / 5;
  if (mon(b, a)->ability == ABILITY_COMPOUND_EYES) acc = acc * 13 / 10;
  if (mon(b, a)->ability == ABILITY_VICTORY_STAR || item_effect(mon(b, a)) == ITEM_EFFECT_WIDE_LENS) acc = acc * 11 / 10;
  if (item_effect(mon(b, d)) == ITEM_EFFECT_BRIGHT_POWDER) acc = acc * 9 / 10;
  if (item_effect(mon(b, a)) == ITEM_EFFECT_ZOOM_LENS && vol(b, d)->moved) acc = acc * 6 / 5;
  acc = acc * (20u - res(b, d, TYPE_FLYING)) / 20;
  uint16_t da = ab(b, d);
  if ((w == WEATHER_SAND && da == ABILITY_SAND_VEIL) || (w == WEATHER_SNOW && da == ABILITY_SNOW_CLOAK)) acc = acc * 4 / 5;
  return rng_below(&b->rng, 100) < acc;
}

/* ------------------------------------------------------------------------- entrées et sorties */

/* Un Pokémon de l'équipe qui peut entrer : debout et pas déjà sur le terrain. */
static bool can_enter(const Side *sd, uint8_t index) {
  return index < sd->count && !battle_on_field(sd, index) && sd->team[index].hp;
}

static void reset_types(Battle *b, int s) {
  const SpeciesInfo *sp = &species_info[mon(b, s)->p.species];
  vol(b, s)->type[0] = (uint8_t)sp->type1, vol(b, s)->type[1] = (uint8_t)sp->type2;
}

static const char *const terrain_start[2][5] = {
    {"", "Le sol se couvre\nd'électricité !", "Le sol se couvre\nd'herbe !", "Le sol se couvre\nde brume !", "Le sol se couvre\nd'énergie psychique !"},
    {"", "An electric current\nruns across the field!", "Grass grew to cover\nthe field!", "Mist swirled around\nthe field!", "The field got\nweird!"}};

/* Graine Électrik, Herbe, Brume, Psy : consommée sur le champ de son type. */
static void seed(Battle *b, int s) {
  static const uint8_t type[] = {0, TYPE_ELECTRIC, TYPE_GRASS, TYPE_FAIRY, TYPE_PSYCHIC};
  Fighter *f = mon(b, s);
  if (!f->hp || !b->terrain || item_effect(f) != ITEM_EFFECT_TERRAIN_SEED || item_info[f->p.item].type != type[b->terrain] ||
      !grounded(b, s))
    return;
  say(b, s, L("{A} utilise sa\n{X} !", "{A} used its\n{X}!"), item_names[f->p.item]);
  lose_item(b, s);
  change_stage(b, s, b->terrain <= TERRAIN_GRASSY ? STAGE_DEF : STAGE_SPD, 1, false);
}

static bool set_terrain(Battle *b, int s, uint8_t t) {
  if (b->terrain == t) return false;
  b->terrain = t;
  b->terrain_turns = item_effect(mon(b, s)) == ITEM_EFFECT_TERRAIN_EXTENDER ? 8 : 5;
  push(b, EV_TERRAIN, s, t);
  say(b, s, terrain_start[lang][t], "");
  for (int k = 0; k < npos(b); k++) seed(b, k);
  return true;
}

/* Les pièges du côté s blessent le Pokémon qui entre ; vrai s'il est K.O. */
static bool hazards(Battle *b, int s) {
  Side *sd = side_of(b, s);
  Fighter *f = mon(b, s);
  if (item_effect(f) == ITEM_EFFECT_HEAVY_BOOTS || res(b, s, TYPE_GROUND)) return false;
  if (sd->stealth_rock && !magic_guard(b, s)) {
    uint8_t e = type_mult(TYPE_ROCK, vol(b, s)->type[0], vol(b, s)->type[1]);
    say(b, s, L("Des pierres pointues\nblessent {A} !", "Pointed stones dug\ninto {A}!"), "");
    hurt(b, s, clamp16((uint32_t)f->max_hp * e / 128 ? (uint32_t)f->max_hp * e / 128 : 1));
    if (fainted(b, s)) return true;
  }
  if (!grounded(b, s)) return false;
  if (sd->spikes && !magic_guard(b, s)) {
    static const uint8_t div[4] = {0, 8, 6, 4};
    say(b, s, L("{A} est blessé\npar les PICOTS !", "{A} is hurt\nby SPIKES!"), "");
    hurt(b, s, frac(f, div[sd->spikes]));
    if (fainted(b, s)) return true;
  }
  if (sd->toxic_spikes) {
    if (has_type(b, s, TYPE_POISON)) {
      sd->toxic_spikes = 0;
      say(b, s, L("{A} absorbe les\nPICS TOXIK !", "{A} absorbed the\nTOXIC SPIKES!"), "");
    } else {
      give_status(b, s, sd->toxic_spikes >= 2 ? STATUS_TOXIC : STATUS_POISON, s);
    }
  }
  return false;
}

/* Talents qui agissent à l'entrée. */
static void entry_ability(Battle *b, int s) {
  Fighter *f = mon(b, s), *foe = mon(b, vs(b, s));
  switch (f->ability) {
    case ABILITY_INTIMIDATE:
      for (int q = 0; q < npos(b); q++) {
        if (!foes(s, q) || !mon(b, q)->hp) continue;
        b->acting = (int8_t)s, b->target = (int8_t)q; /* {B} : celui-ci */
        say(b, s, L("L'INTIMIDATION de {A}\neffraie {B} !", "{A}'s INTIMIDATE\ncuts {B}'s ATTACK!"), "");
        uint16_t fa = mon(b, q)->ability;
        if (fa == ABILITY_GUARD_DOG) change_stage(b, q, STAGE_ATK, 1, false);
        else if (fa == ABILITY_INNER_FOCUS || fa == ABILITY_OWN_TEMPO || fa == ABILITY_SCRAPPY)
          say(b, q, L("{X} de {A}\nempêche la baisse !", "{A}'s {X}\nprevents stat loss!"), ability_names[fa]);
        else change_stage(b, q, STAGE_ATK, -1, true);
        b->acting = b->target = -1;
      }
      break;
    case ABILITY_DRIZZLE: set_weather(b, s, WEATHER_RAIN); break;
    case ABILITY_PRIMORDIAL_SEA:
    case ABILITY_DESOLATE_LAND: /* la météo ne s'arrête plus */
      set_weather(b, s, f->ability == ABILITY_PRIMORDIAL_SEA ? WEATHER_RAIN : WEATHER_SUN);
      b->weather_turns = 0;
      break;
    case ABILITY_SCREEN_CLEANER:
      if (b->side[0].reflect || b->side[0].light_screen || b->side[1].reflect || b->side[1].light_screen)
        ability_says(b, s, L("{X} de {A}\nefface les protections !", "{A}'s {X}\nremoved all screens!"));
      for (int k = 0; k < 2; k++) b->side[k].reflect = b->side[k].light_screen = 0;
      break;
    case ABILITY_SLOW_START: say(b, s, L("{A} ne trouve\npas son rythme !", "{A} can't\nget it going!"), ""); break;
    case ABILITY_SUPERSWEET_SYRUP:
      for (int q = 0; q < npos(b); q++)
        if (foes(s, q) && mon(b, q)->hp) say(b, q, L("Un parfum sucré\nenveloppe {A} !", "A supersweet aroma\nwafts around {A}!"), ""), change_stage(b, q, STAGE_EVA, -1, true);
      break;
    case ABILITY_DROUGHT:
    case ABILITY_ORICHALCUM_PULSE: set_weather(b, s, WEATHER_SUN); break;
    case ABILITY_SAND_STREAM: set_weather(b, s, WEATHER_SAND); break;
    case ABILITY_SNOW_WARNING: set_weather(b, s, WEATHER_SNOW); break;
    case ABILITY_ELECTRIC_SURGE:
    case ABILITY_HADRON_ENGINE: set_terrain(b, s, TERRAIN_ELECTRIC); break;
    case ABILITY_GRASSY_SURGE: set_terrain(b, s, TERRAIN_GRASSY); break;
    case ABILITY_MISTY_SURGE: set_terrain(b, s, TERRAIN_MISTY); break;
    case ABILITY_PSYCHIC_SURGE: set_terrain(b, s, TERRAIN_PSYCHIC); break;
    case ABILITY_TRACE:
      if (foe->hp && foe->ability != ABILITY_TRACE && foe->ability) {
        f->ability = foe->ability;
        say(b, s, L("{A} copie le talent\n{X} !", "{A} traced\nthe ability {X}!"), ability_names[f->ability]);
        entry_ability(b, s);
      }
      break;
    case ABILITY_DOWNLOAD:
      if (foe->hp) {
        uint32_t def = staged(foe->stat[STAT_DEF], vol(b, vs(b, s))->stage[STAGE_DEF]);
        uint32_t spd = staged(foe->stat[STAT_SPD], vol(b, vs(b, s))->stage[STAGE_SPD]);
        change_stage(b, s, def < spd ? STAGE_ATK : STAGE_SPA, 1, false);
      }
      break;
    case ABILITY_INTREPID_SWORD: change_stage(b, s, STAGE_ATK, 1, false); break;
    case ABILITY_DAUNTLESS_SHIELD: change_stage(b, s, STAGE_DEF, 1, false); break;
    case ABILITY_PRESSURE: say(b, s, L("{A} exerce\nune PRESSION !", "{A} is exerting\nits PRESSURE!"), ""); break;
    case ABILITY_UNNERVE: say(b, vs(b, s), L("{A} est tendu et\nne peut plus manger de baies !", "{A} is too nervous\nto eat Berries!"), ""); break;
    case ABILITY_MOLD_BREAKER:
    case ABILITY_TERAVOLT:
    case ABILITY_TURBOBLAZE: ability_says(b, s, L("{A} utilise\n{X} !", "{A} used\n{X}!")); break;
    case ABILITY_AIR_LOCK:
    case ABILITY_CLOUD_NINE: say(b, s, L("Les effets de la météo\ndisparaissent.", "The effects of weather\ndisappeared."), ""); break;
    case ABILITY_DARK_AURA:
    case ABILITY_FAIRY_AURA:
    case ABILITY_SWORD_OF_RUIN:
    case ABILITY_BEADS_OF_RUIN:
    case ABILITY_TABLETS_OF_RUIN:
    case ABILITY_VESSEL_OF_RUIN:
    case ABILITY_MEGA_SOL: ability_says(b, s, L("{X} de {A}\nentre en action !", "{A}'s\n{X} kicks in!")); break;
    case ABILITY_SUPREME_OVERLORD:
      if (fainted_allies(b, s)) say(b, s, L("{A} puise dans la force\nde ses alliés tombés !", "{A} gained strength\nfrom its fallen allies!"), "");
      break;
  }
  if (item_effect(f) == ITEM_EFFECT_AIR_BALLOON) say(b, s, L("{A} flotte grâce\nà son BALLON !", "{A} floats in the air\nwith its AIR BALLOON!"), "");
}

/* Le Pokémon qui quitte le terrain (vivant) : Médic Nature, Régé-Force. */
static void leave(Battle *b, int s) {
  Fighter *out = mon(b, s);
  if (!out->hp) return;
  if (out->ability == ABILITY_NATURAL_CURE && out->status) set_status(b, s, STATUS_NONE);
  if (out->ability == ABILITY_REGENERATOR) {
    out->hp = (uint16_t)(out->hp + out->max_hp / 3 > out->max_hp ? out->max_hp : out->hp + out->max_hp / 3);
    push(b, EV_HP, s, out->hp);
  }
  if (out->status == STATUS_TOXIC) out->status_turns = 1;
}

static void form_change(Battle *b, int s);

static void enter(Battle *b, int s, uint8_t index, bool baton, bool first) {
  Side *sd = side_of(b, s);
  if (!first && mon(b, s)->hp) {
    say(b, s, POS_SIDE(s) == SIDE_PLAYER ? L("{A}, reviens !", "{A}, come back!") : L("{X} rappelle\n{A} !", "{X} withdrew\n{A}!"), b->foe_name);
    leave(b, s);
  }
  Volatile *v = vol(b, s), keep = *v;
  memset(v, 0, sizeof *v);
  if (baton) {
    memcpy(v->stage, keep.stage, sizeof keep.stage);
    v->substitute = keep.substitute, v->confused = keep.confused;
    v->seeded = keep.seeded, v->cursed = keep.cursed;
  }
  sd->active[POS_SLOT(s)] = index;
  v->fresh = true;
  reset_types(b, s);
  Fighter *f = mon(b, s);
  f->ability = pokemon_ability(&f->p); /* oublie un talent copié (Calque) */
  say(b, s, POS_SIDE(s) == SIDE_PLAYER ? L("Vas-y ! {A} !", "Go! {A}!") : L("{X} envoie\n{A} !", "{X} sent\nout {A}!"), b->foe_name);
  push(b, EV_SWITCH, s, index);
  push(b, EV_HP, s, f->hp);
  form_change(b, s);
  if (hazards(b, s)) return;
  seed(b, s);
  entry_ability(b, s);
}

/* Le Pokémon de la position s est-il retenu par le talent d'un adversaire ? */
static bool trapped(const Battle *cb, int s) {
  Battle *b = (Battle *)cb;
  const Fighter *f = mon(b, s);
  const Volatile *me = vol(b, s);
  if (me->type[0] == TYPE_GHOST || me->type[1] == TYPE_GHOST) return false;
  bool ground = me->type[0] != TYPE_FLYING && me->type[1] != TYPE_FLYING && f->ability != ABILITY_LEVITATE &&
                f->ability != ABILITY_EELEVATE && item_info[f->p.item].effect != ITEM_EFFECT_AIR_BALLOON;
  for (int q = 0; q < npos(b); q++) {
    const Fighter *foe = mon(b, q);
    if (!foes(s, q) || !foe->hp) continue;
    if ((foe->ability == ABILITY_SHADOW_TAG && f->ability != ABILITY_SHADOW_TAG) || (foe->ability == ABILITY_ARENA_TRAP && ground) ||
        (foe->ability == ABILITY_MAGNET_PULL && (me->type[0] == TYPE_STEEL || me->type[1] == TYPE_STEEL)))
      return true;
  }
  return false;
}

static int other_alive(Battle *b, int s, int prefer) {
  Side *sd = side_of(b, s);
  if (prefer >= 0 && can_enter(sd, (uint8_t)prefer)) return prefer;
  for (int i = 0; i < sd->count; i++)
    if (can_enter(sd, (uint8_t)i)) return i;
  return -1;
}

/* Hurlement, Draco-Queue : un Pokémon au hasard prend la place de celui du côté d. */
static void force_out(Battle *b, int d) {
  Side *ds = side_of(b, d);
  int n = 0, pick[TEAM_MAX];
  for (int i = 0; i < ds->count; i++)
    if (can_enter(ds, (uint8_t)i)) pick[n++] = i;
  if (!n || !mon(b, d)->hp) return;
  if (ab(b, d) == ABILITY_SUCTION_CUPS) return say(b, d, L("{A} s'accroche\nau sol !", "{A} anchors\nitself!"), "");
  say(b, d, L("{A} est chassé !", "{A} was dragged out!"), "");
  leave(b, d);
  enter(b, d, (uint8_t)pick[rng_below(&b->rng, (uint32_t)n)], false, true);
  vol(b, d)->moved = true;
}

/* ------------------------------------------------------------------------- formes à objet */

int item_form(uint16_t species, uint8_t item) {
  if (!item || item_info[item].effect != ITEM_EFFECT_FORM_ITEM) return -1;
  for (int i = NUM_BASE_SPECIES; i < NUM_SPECIES; i++)
    if (species_info[i].is_form && species_info[i].mega_base == species && species_info[i].stone == item) return i;
  return -1;
}

/* Giratina et l'Orbe Platiné, Zacian et l'Épée Rouillée, Arceus et ses Plaques… : la forme change
 * dès l'entrée sur le terrain, sans compter comme une Méga-Évolution. */
static void form_change(Battle *b, int s) {
  Fighter *f = mon(b, s);
  int form = item_form((uint16_t)f->p.species, (uint8_t)f->p.item);
  if (form < 0 || !f->hp) return;
  say(b, s, L("{X} de {A}\nréagit !", "{A}'s\n{X} is reacting!"), item_names[f->p.item]);
  f->p.species = (uint16_t)form;
  for (int k = STAT_ATK; k < NUM_STATS; k++) {
    f->stat[k] = species_stat(&species_info[form], (uint8_t)f->p.nature, (uint8_t)k);
    if (b->raid && s == SIDE_FOE) f->stat[k] = (uint16_t)(f->stat[k] * 5 / 4);
  }
  f->ability = pokemon_ability(&f->p);
  reset_types(b, s);
  push(b, EV_FORM, s, (uint16_t)form);
  say(b, s, L("{A} change\nde forme !", "{A} changed\nits form!"), "");
}

/* ------------------------------------------------------------------------- Méga-Évolution */

int mega_form(uint16_t species, uint8_t item) {
  if (!item || item_info[item].effect != ITEM_EFFECT_MEGA_STONE) return -1;
  for (int i = NUM_BASE_SPECIES; i < NUM_SPECIES; i++)
    if (species_info[i].mega_base == species && species_info[i].stone == item) return i;
  return -1;
}

int battle_can_mega(const Battle *b, int s) {
  const Side *sd = &b->side[POS_SIDE(s)];
  const Fighter *f = battle_active((Battle *)b, s);
  if (!sd->mega_allowed || sd->mega_used || !f->hp) return -1;
  return mega_form((uint16_t)f->p.species, (uint8_t)f->p.item);
}

static void mega_evolve(Battle *b, int s) {
  int form = battle_can_mega(b, s);
  if (form < 0) return;
  Fighter *f = mon(b, s);
  side_of(b, s)->mega_used = true;
  say(b, s, L("{X} de {A}\nréagit au MÉGA-ANNEAU !", "{A}'s {X} reacts\nto the MEGA RING!"), item_names[f->p.item]);
  f->p.species = (uint16_t)form;
  for (int k = STAT_ATK; k < NUM_STATS; k++) f->stat[k] = species_stat(&species_info[form], (uint8_t)f->p.nature, (uint8_t)k);
  f->ability = (uint16_t)species_info[form].ability0;
  reset_types(b, s);
  push(b, EV_MEGA, s, (uint16_t)form);
  say(b, s, L("{A} méga-évolue !", "{A} Mega Evolved!"), "");
  entry_ability(b, s);
}

/* ------------------------------------------------------------------------- attaques de statut */

static bool blocked_by_substitute(Battle *b, int a) {
  if (!vol(b, vs(b, a))->substitute || mon(b, a)->ability == ABILITY_INFILTRATOR) return false;
  fail(b, a);
  return true;
}

static bool is_powder(uint16_t move) { return move == MOVE_SPORE || move == MOVE_SLEEP_POWDER || move == MOVE_STUN_SPORE; }

/* Attaque de statut visant l'adversaire (et que Miroir Magik renvoie). */
static bool targets_foe(uint8_t e) {
  return (e >= EFFECT_BURN && e <= EFFECT_T_SPA_DOWN2) || e == EFFECT_LEECH_SEED || e == EFFECT_TAUNT || e == EFFECT_ROAR;
}
bool battle_move_targets_foe(uint16_t move) {
  const MoveInfo *m = &move_info[move];
  return move_is_status(m) ? targets_foe(m->effect) : !m->spread;
}

static bool is_hazard(uint8_t e) { return e == EFFECT_SPIKES || e == EFFECT_TOXIC_SPIKES || e == EFFECT_STEALTH_ROCK; }

static void status_move(Battle *b, int a, uint16_t move, const MoveInfo *m, uint8_t bp_target, bool bounced) {
  int d = vs(b, a);
  Fighter *A = mon(b, a), *D = mon(b, d);
  Volatile *va = vol(b, a), *vd = vol(b, d);
  Side *as = side_of(b, a), *ds = side_of(b, d);
  uint8_t e = m->effect;

  if (targets_foe(e) || is_hazard(e)) {
    if (!bounced && ab(b, d) == ABILITY_MAGIC_BOUNCE && D->hp) {
      say(b, d, L("MIROIR MAGIK de {A}\nrenvoie l'attaque !", "{A}'s MAGIC BOUNCE\nbounced the move back!"), "");
      return status_move(b, d, move, m, bp_target, true);
    }
  }
  if (targets_foe(e)) {
    if (!D->hp) return fail(b, a);
    if (vd->protecting) return say(b, d, L("{A} se protège !", "{A} protected itself!"), "");
    if (A->ability == ABILITY_PRANKSTER && has_type(b, d, TYPE_DARK) && !bounced)
      return say(b, a, L("Ça n'affecte pas\n{B}…", "It doesn't affect\n{B}…"), "");
    if (ab(b, d) == ABILITY_GOOD_AS_GOLD) return say(b, d, L("CORPS EN OR protège\n{A} !", "GOOD AS GOLD\nprotects {A}!"), "");
    if (m->sound && ab(b, d) == ABILITY_SOUNDPROOF) return say(b, d, L("{A} n'est pas affecté\npar le son !", "{A} is unaffected\nby sound!"), "");
    if (is_powder(move) && (has_type(b, d, TYPE_GRASS) || ab(b, d) == ABILITY_OVERCOAT || item_effect(D) == ITEM_EFFECT_SAFETY_GOGGLES))
      return say(b, a, L("Ça n'affecte pas\n{B}…", "It doesn't affect\n{B}…"), "");
    if (!accurate(b, a, m, move)) return say(b, d, L("{A} évite l'attaque !", "{A} avoided the attack!"), "");
    if (m->type == TYPE_ELECTRIC && !type_mult(TYPE_ELECTRIC, vd->type[0], vd->type[1])) return say(b, a, L("Ça n'affecte pas\n{B}…", "It doesn't affect\n{B}…"), "");
  }
  int stage, delta;
  if (target_drop(e, &stage, &delta)) {
    if (!blocked_by_substitute(b, a)) change_stage(b, d, stage, delta, true);
    return;
  }
  const Boost *bo = boost_of(e);
  if (bo) {
    int scale = move == MOVE_GROWTH && weather(b) == WEATHER_SUN ? 2 : 1;
    apply_boost(b, a, bo, scale);
    if (e == EFFECT_SHELL_SMASH && item_effect(A) == ITEM_EFFECT_WHITE_HERB) {
      for (int k = 0; k < NUM_STAGES; k++)
        if (va->stage[k] < 0) va->stage[k] = 0;
      say(b, a, L("{A} retrouve ses stats\ngrâce à {X} !", "{A} restored its\nstats with its {X}!"), item_names[A->p.item]);
      lose_item(b, a);
    }
    return;
  }
  switch (e) {
    case EFFECT_BURN:
    case EFFECT_PARALYZE:
    case EFFECT_POISON:
    case EFFECT_BADLY_POISON:
    case EFFECT_SLEEP:
    case EFFECT_FREEZE: {
      static const uint8_t st[] = {STATUS_BURN, STATUS_PARALYSIS, STATUS_POISON, STATUS_TOXIC, STATUS_SLEEP, STATUS_FREEZE};
      if (blocked_by_substitute(b, a)) return;
      if (!give_status(b, d, st[e - EFFECT_BURN], a)) fail(b, a);
      return;
    }
    case EFFECT_CONFUSE:
      if (blocked_by_substitute(b, a)) return;
      if (!confuse(b, d)) fail(b, a);
      return;
    case EFFECT_BELLY_DRUM:
      if (A->hp <= A->max_hp / 2 || va->stage[STAGE_ATK] == 6) return fail(b, a);
      hurt(b, a, A->max_hp / 2);
      va->stage[STAGE_ATK] = 6;
      return say(b, a, L("{A} sacrifie des PV\net monte son ATTAQUE à fond !", "{A} cut its own HP\nand maximized ATTACK!"), "");
    case EFFECT_CURSE:
      if (has_type(b, a, TYPE_GHOST)) {
        if (vd->cursed || !D->hp) return fail(b, a);
        hurt(b, a, A->max_hp / 2);
        vd->cursed = true;
        say(b, a, L("{A} maudit {B} !", "{A} laid a curse\non {B}!"), "");
        fainted(b, a);
      } else {
        change_stage(b, a, STAGE_ATK, 1, false), change_stage(b, a, STAGE_DEF, 1, false);
        change_stage(b, a, STAGE_SPE, -1, false);
      }
      return;
    case EFFECT_HEAL_HALF:
    case EFFECT_HEAL_WEATHER: {
      if (A->hp == A->max_hp) return say(b, a, L("Les PV de {A}\nsont au maximum !", "{A}'s HP\nis full!"), "");
      uint8_t w = weather_for(b, a);
      uint16_t amount = e == EFFECT_HEAL_HALF || w == WEATHER_NONE ? frac(A, 2) :
                        w == WEATHER_SUN ? (uint16_t)(A->max_hp * 2 / 3) : frac(A, 4);
      heal(b, a, amount);
      return say(b, a, L("{A} récupère des PV !", "{A} regained health!"), "");
    }
    case EFFECT_REST:
      if (A->hp == A->max_hp || A->ability == ABILITY_INSOMNIA || A->ability == ABILITY_VITAL_SPIRIT ||
          A->ability == ABILITY_PURIFYING_SALT)
        return fail(b, a);
      set_status(b, a, STATUS_SLEEP);
      A->status_turns = 3; /* décompté avant d'agir : 2 tours endormi */
      heal(b, a, A->max_hp);
      return say(b, a, L("{A} s'endort et\nrécupère tous ses PV !", "{A} slept and\nbecame healthy!"), "");
    case EFFECT_PROTECT:
      /* échoue si l'adversaire a déjà agi, et de plus en plus souvent d'affilée */
      if (rng_below(&b->rng, 1u << (va->protect_chain > 4 ? 4 : va->protect_chain)) || vd->moved) {
        va->protect_chain = 0;
        return fail(b, a);
      }
      va->protect_chain++, va->protecting = true;
      return say(b, a, L("{A} se protège !", "{A} protected itself!"), "");
    case EFFECT_SUBSTITUTE:
      if (va->substitute) return say(b, a, L("{A} a déjà\nun clone !", "{A} already\nhas a substitute!"), "");
      if (A->hp <= A->max_hp / 4) return say(b, a, L("Trop faible pour\ncréer un clone !", "Too weak to make\na substitute!"), "");
      hurt(b, a, frac(A, 4));
      va->substitute = frac(A, 4);
      return say(b, a, L("{A} crée un clone !", "{A} made a substitute!"), "");
    case EFFECT_LEECH_SEED:
      if (has_type(b, d, TYPE_GRASS)) return say(b, a, L("Ça n'affecte pas\n{B}…", "It doesn't affect\n{B}…"), "");
      if (vd->seeded || blocked_by_substitute(b, a)) return vd->seeded ? fail(b, a) : (void)0;
      vd->seeded = true;
      return say(b, d, L("{A} est infecté !", "{A} was seeded!"), "");
    case EFFECT_REFLECT:
      if (as->reflect) return fail(b, a);
      as->reflect = item_effect(A) == ITEM_EFFECT_LIGHT_CLAY ? 8 : 5;
      return say(b, a, a == SIDE_PLAYER ? L("PROTECTION renforce\nvotre équipe !", "REFLECT raised\nyour team's DEFENSE!") : L("PROTECTION renforce\nl'équipe ennemie !", "REFLECT raised the\nfoe team's DEFENSE!"), "");
    case EFFECT_LIGHT_SCREEN:
      if (as->light_screen) return fail(b, a);
      as->light_screen = item_effect(A) == ITEM_EFFECT_LIGHT_CLAY ? 8 : 5;
      return say(b, a, a == SIDE_PLAYER ? L("MUR LUMIÈRE renforce\nvotre équipe !", "LIGHT SCREEN raised\nyour team's SP. DEF!") : L("MUR LUMIÈRE renforce\nl'équipe ennemie !", "LIGHT SCREEN raised the\nfoe team's SP. DEF!"), "");
    case EFFECT_SPIKES:
      if (ds->spikes == 3) return fail(b, a);
      ds->spikes++;
      return say(b, a, d == SIDE_FOE ? L("Des PICOTS entourent\nl'équipe ennemie !", "SPIKES were scattered\naround the foe team!") : L("Des PICOTS entourent\nvotre équipe !", "SPIKES were scattered\naround your team!"), "");
    case EFFECT_TOXIC_SPIKES:
      if (ds->toxic_spikes == 2) return fail(b, a);
      ds->toxic_spikes++;
      return say(b, a, d == SIDE_FOE ? L("Des PICS TOXIK entourent\nl'équipe ennemie !", "TOXIC SPIKES were scattered\naround the foe team!") : L("Des PICS TOXIK entourent\nvotre équipe !", "TOXIC SPIKES were scattered\naround your team!"), "");
    case EFFECT_STEALTH_ROCK:
      if (ds->stealth_rock) return fail(b, a);
      ds->stealth_rock = true;
      return say(b, a, d == SIDE_FOE ? L("Des pierres flottent autour\nde l'équipe ennemie !", "Pointed stones float\naround the foe team!") : L("Des pierres flottent autour\nde votre équipe !", "Pointed stones float\naround your team!"), "");
    case EFFECT_RAIN:
    case EFFECT_SUN:
    case EFFECT_SAND:
    case EFFECT_HAIL:
      if (!set_weather(b, a, (uint8_t)(WEATHER_RAIN + (e - EFFECT_RAIN)))) fail(b, a);
      return;
    case EFFECT_BATON_PASS: {
      int next = other_alive(b, a, bp_target);
      if (next < 0) return fail(b, a);
      enter(b, a, (uint8_t)next, true, false);
      vol(b, a)->moved = true;
      return;
    }
    case EFFECT_ROAR:
      if (other_alive(b, d, -1) < 0) return fail(b, a);
      return force_out(b, d);
    case EFFECT_HAZE:
      memset(va->stage, 0, sizeof va->stage), memset(vd->stage, 0, sizeof vd->stage);
      return say(b, a, L("Les changements de stats\nsont annulés !", "All stat changes\nwere eliminated!"), "");
    case EFFECT_HEAL_BELL:
      for (int i = 0; i < as->count; i++) as->team[i].status = STATUS_NONE;
      push(b, EV_STATUS, a, STATUS_NONE);
      return say(b, a, L("Un carillon apaisant\nretentit !", "A bell chimed!"), "");
    case EFFECT_TAUNT:
      if (vd->taunt) return fail(b, a);
      vd->taunt = 3;
      say(b, d, L("{A} tombe dans\nle piège de PROVOC !", "{A} fell for\nthe TAUNT!"), "");
      if (item_effect(D) == ITEM_EFFECT_MENTAL_HERB) {
        say(b, d, L("{X} de {A}\nla guérit de la PROVOC !", "{A}'s\n{X} cured its TAUNT!"), item_names[D->p.item]);
        lose_item(b, d);
        vd->taunt = 0;
      }
      return;
    case EFFECT_ELECTRIC_TERRAIN:
    case EFFECT_GRASSY_TERRAIN:
    case EFFECT_MISTY_TERRAIN:
    case EFFECT_PSYCHIC_TERRAIN:
      if (!set_terrain(b, a, (uint8_t)(TERRAIN_ELECTRIC + (e - EFFECT_ELECTRIC_TERRAIN)))) fail(b, a);
      return;
    case EFFECT_TRICK_ROOM:
      if (b->trick_room) {
        b->trick_room = 0;
        return say(b, a, L("Les dimensions tordues\nredeviennent normales !", "The twisted dimensions\nreturned to normal!"), "");
      }
      b->trick_room = 5;
      return say(b, a, L("{A} tord\nles dimensions !", "{A} twisted\nthe dimensions!"), "");
    case EFFECT_TAILWIND:
      if (as->tailwind) return fail(b, a);
      as->tailwind = 4;
      return say(b, a, a == SIDE_PLAYER ? L("Un VENT ARRIÈRE souffle\nderrière votre équipe !", "A TAILWIND blew from\nbehind your team!") :
                                          L("Un VENT ARRIÈRE souffle\nderrière l'équipe ennemie !", "A TAILWIND blew from\nbehind the foe team!"), "");
  }
  fail(b, a);
}

/* ------------------------------------------------------------------------- attaques offensives */

/* Talents et objets du défenseur touché par une attaque de contact. */
static void contact_effects(Battle *b, int a) {
  int d = vs(b, a);
  Fighter *A = mon(b, a), *D = mon(b, d);
  uint16_t x = D->ability;
  if (!A->hp) return;
  if ((x == ABILITY_ROUGH_SKIN || x == ABILITY_IRON_BARBS) && !magic_guard(b, a)) {
    say(b, a, L("{A} est blessé par\n{X} !", "{A} was hurt by\n{X}!"), ability_names[x]);
    hurt(b, a, frac(A, 8));
    if (fainted(b, a)) return;
  }
  if (item_effect(D) == ITEM_EFFECT_ROCKY_HELMET && !magic_guard(b, a)) {
    say(b, a, L("{A} est blessé par\nle CASQUE BRUT !", "{A} was hurt by\nthe ROCKY HELMET!"), "");
    hurt(b, a, frac(A, 6));
    if (fainted(b, a)) return;
  }
  if (x == ABILITY_GOOEY || x == ABILITY_TANGLING_HAIR) change_stage(b, a, STAGE_SPE, -1, true);
  if (x == ABILITY_LINGERING_AROMA && A->ability != ABILITY_LINGERING_AROMA) {
    A->ability = ABILITY_LINGERING_AROMA;
    say(b, a, L("L'ODEUR TENACE s'imprègne\nsur {A} !", "A lingering aroma\nclings to {A}!"), "");
  }
  if (res(b, d, TYPE_GHOST) && !magic_guard(b, a)) {
    say(b, a, L("{A} est hanté par\nla RÉSONANCE SPECTRE !", "{A} is haunted by\nthe GHOST RESONANCE!"), "");
    hurt(b, a, (uint16_t)(frac(A, 16) * res(b, d, TYPE_GHOST)));
    if (fainted(b, a)) return;
  }
  if (x == ABILITY_AFTERMATH && !D->hp && !magic_guard(b, a)) {
    say(b, a, L("{A} est pris dans\nle BOOM FINAL !", "{A} was caught\nin the AFTERMATH!"), "");
    hurt(b, a, frac(A, 4));
    if (fainted(b, a)) return;
  }
  if (A->ability == ABILITY_POISON_TOUCH && D->hp && rng_percent(&b->rng, 30)) give_status(b, d, STATUS_POISON, a);
  if (!rng_percent(&b->rng, 30)) return;
  if (x == ABILITY_STATIC) give_status(b, a, STATUS_PARALYSIS, d);
  else if (x == ABILITY_FLAME_BODY) give_status(b, a, STATUS_BURN, d);
  else if (x == ABILITY_POISON_POINT) give_status(b, a, STATUS_POISON, d);
  else if (x == ABILITY_EFFECT_SPORE && !has_type(b, a, TYPE_GRASS) && A->ability != ABILITY_OVERCOAT) {
    static const uint8_t st[3] = {STATUS_POISON, STATUS_PARALYSIS, STATUS_SLEEP};
    give_status(b, a, st[rng_below(&b->rng, 3)], d);
  }
}

/* Talents et objets du défenseur touché (sans clone), encore en vie ou non. */
static void hit_effects(Battle *b, int a, uint8_t type, const MoveInfo *m, bool crit, uint8_t eff) {
  int d = vs(b, a);
  Fighter *D = mon(b, d);
  uint16_t x = D->ability;
  if (item_effect(D) == ITEM_EFFECT_AIR_BALLOON) {
    say(b, d, L("Le BALLON de {A}\néclate !", "{A}'s AIR BALLOON\npopped!"), "");
    lose_item(b, d);
  }
  if (x == ABILITY_TOXIC_DEBRIS && !m->special && side_of(b, a)->toxic_spikes < 2) {
    side_of(b, a)->toxic_spikes++;
    say(b, a, L("Des PICS TOXIK se répandent\nautour de {A} !", "TOXIC SPIKES were scattered\naround {A}!"), "");
  }
  if (x == ABILITY_COTTON_DOWN && mon(b, a)->hp) change_stage(b, a, STAGE_SPE, -1, true);
  if (x == ABILITY_SEED_SOWER) set_terrain(b, d, TERRAIN_GRASSY);
  if (!D->hp) return;
  if (x == ABILITY_COLOR_CHANGE && type != TYPE_MYSTERY && !has_type(b, d, type)) {
    vol(b, d)->type[0] = vol(b, d)->type[1] = type;
    say(b, d, L("{A} devient\nde type {X} !", "{A} transformed\ninto the {X} type!"), type_names[type]);
  }
  if (item_effect(D) == ITEM_EFFECT_KEE_BERRY && item_info[D->p.item].type == m->special) {
    say(b, d, L("{A} mange sa\n{X} !", "{A} ate its\n{X}!"), item_names[D->p.item]);
    lose_item(b, d);
    change_stage(b, d, m->special ? STAGE_SPD : STAGE_DEF, 1, false);
  }
  if (crit && x == ABILITY_ANGER_POINT) {
    vol(b, d)->stage[STAGE_ATK] = 6;
    say(b, d, L("COLÈRE de {A}\nmonte son ATTAQUE à fond !", "{A}'s ANGER POINT\nmaxed its ATTACK!"), "");
  }
  if (x == ABILITY_WEAK_ARMOR && !m->special)
    change_stage(b, d, STAGE_DEF, -1, false), change_stage(b, d, STAGE_SPE, 2, false);
  if (x == ABILITY_JUSTIFIED && type == TYPE_DARK) change_stage(b, d, STAGE_ATK, 1, false);
  if (x == ABILITY_THERMAL_EXCHANGE && type == TYPE_FIRE) change_stage(b, d, STAGE_ATK, 1, false);
  if (x == ABILITY_STAMINA) change_stage(b, d, STAGE_DEF, 1, false);
  if (x == ABILITY_WATER_COMPACTION && type == TYPE_WATER) change_stage(b, d, STAGE_DEF, 2, false);
  if (x == ABILITY_STEAM_ENGINE && (type == TYPE_WATER || type == TYPE_FIRE)) change_stage(b, d, STAGE_SPE, 6, false);
  if (x == ABILITY_SPICY_SPRAY) give_status(b, a, STATUS_BURN, d);
  if (vol(b, d)->berserk && x == ABILITY_BERSERK) {
    vol(b, d)->berserk = false;
    change_stage(b, d, STAGE_SPA, 1, false);
  }
  if (vol(b, d)->berserk && x == ABILITY_ANGER_SHELL) {
    static const Boost shell = {0, {1, -1, 1, 1, -1}};
    vol(b, d)->berserk = false;
    ability_says(b, d, L("{X} de {A}\ns'active !", "{A}'s\n{X} activated!"));
    apply_boost(b, d, &shell, 1);
  }
  if (eff > 16 && item_effect(D) == ITEM_EFFECT_WEAKNESS_POLICY) {
    say(b, d, L("{X} de {A}\ns'active !", "{A}'s\n{X} activated!"), item_names[D->p.item]);
    lose_item(b, d);
    change_stage(b, d, STAGE_ATK, 2, false), change_stage(b, d, STAGE_SPA, 2, false);
  }
  if (D->status == STATUS_FREEZE && type == TYPE_FIRE) {
    set_status(b, d, STATUS_NONE);
    say(b, d, L("{A} dégèle !", "{A} thawed out!"), "");
  }
}

/* Effet secondaire d'une attaque offensive qui a touché (hit_target : pas arrêtée par un clone). */
static void secondary(Battle *b, int a, const MoveInfo *m, bool hit_target) {
  int d = vs(b, a);
  uint8_t e = m->effect;
  const Boost *bo = boost_of(e);
  if (bo && bo->d[0] + bo->d[1] + bo->d[2] + bo->d[3] + bo->d[4] < 0) { /* baisses du lanceur : toujours */
    apply_boost(b, a, bo, 1);
    return;
  }
  if (sheer(b, a, m)) return;
  uint32_t chance = m->effect_chance ? m->effect_chance : 100;
  if (mon(b, a)->ability == ABILITY_SERENE_GRACE) chance *= 2;
  bool lucky = rng_percent(&b->rng, chance);
  if (bo) {
    if (lucky) apply_boost(b, a, bo, 1);
    return;
  }
  if (!hit_target || !mon(b, d)->hp || !lucky) return;
  if (ab(b, d) == ABILITY_SHIELD_DUST || item_effect(mon(b, d)) == ITEM_EFFECT_COVERT_CLOAK) return;
  int stage, delta;
  if (target_drop(e, &stage, &delta)) {
    change_stage(b, d, stage, delta, true);
    return;
  }
  switch (e) {
    case EFFECT_BURN: give_status(b, d, STATUS_BURN, a); break;
    case EFFECT_PARALYZE: give_status(b, d, STATUS_PARALYSIS, a); break;
    case EFFECT_POISON: give_status(b, d, STATUS_POISON, a); break;
    case EFFECT_BADLY_POISON: give_status(b, d, STATUS_TOXIC, a); break;
    case EFFECT_SLEEP: give_status(b, d, STATUS_SLEEP, a); break;
    case EFFECT_FREEZE: give_status(b, d, STATUS_FREEZE, a); break;
    case EFFECT_TRI: {
      static const uint8_t st[3] = {STATUS_BURN, STATUS_PARALYSIS, STATUS_FREEZE};
      give_status(b, d, st[rng_below(&b->rng, 3)], a);
      break;
    }
    case EFFECT_CONFUSE: confuse(b, d); break;
    case EFFECT_FLINCH:
    case EFFECT_FAKE_OUT:
      if (!vol(b, d)->moved && ab(b, d) != ABILITY_INNER_FOCUS) vol(b, d)->flinch = true;
      break;
  }
}

/* Une frappe sur le Pokémon d'en face (ou son clone) ; renvoie les PV qu'il a vraiment perdus. */
static uint16_t strike(Battle *b, int a, uint16_t dmg, bool *hit_target) {
  int d = vs(b, a);
  Volatile *vd = vol(b, d);
  Fighter *D = mon(b, d);
  if (vd->substitute && mon(b, a)->ability != ABILITY_INFILTRATOR) {
    *hit_target = false;
    if (dmg >= vd->substitute) {
      vd->substitute = 0;
      say(b, d, L("Le clone de {A}\ndisparaît !", "{A}'s substitute\nfaded!"), "");
    } else {
      vd->substitute = (uint16_t)(vd->substitute - dmg);
      say(b, d, L("Le clone encaisse\nles dégâts !", "The substitute took\nthe damage!"), "");
    }
    return 0;
  }
  *hit_target = true;
  if (dmg >= D->hp) {
    uint8_t ie = item_effect(D);
    bool full = D->hp == D->max_hp;
    if (full && ab(b, d) == ABILITY_STURDY) {
      dmg = (uint16_t)(D->hp - 1);
      hurt(b, d, dmg);
      say(b, d, L("{A} tient bon\ngrâce à FERMETÉ !", "{A} endured the hit\nthanks to STURDY!"), "");
      return dmg;
    }
    if ((ie == ITEM_EFFECT_FOCUS_SASH && full) || (ie == ITEM_EFFECT_FOCUS_BAND && rng_percent(&b->rng, 10))) {
      dmg = (uint16_t)(D->hp - 1);
      hurt(b, d, dmg);
      say(b, d, L("{A} tient bon grâce\nà {X} !", "{A} hung on\nusing its {X}!"), item_names[D->p.item]);
      if (ie == ITEM_EFFECT_FOCUS_SASH) lose_item(b, d);
      return dmg;
    }
  }
  return hurt(b, d, dmg);
}

/* L'attaque s'écrase (Pied Voltige) : le lanceur perd la moitié de ses PV. */
static void crash(Battle *b, int a, const MoveInfo *m) {
  if (m->effect != EFFECT_CRASH || magic_guard(b, a)) return;
  say(b, a, L("{A} s'écrase au sol !", "{A} kept going\nand crashed!"), "");
  hurt(b, a, frac(mon(b, a), 2));
  fainted(b, a);
}

/* Talent qui absorbe l'attaque ; vrai si elle est absorbée. */
static bool absorb(Battle *b, int a, uint8_t type) {
  int d = vs(b, a);
  Fighter *D = mon(b, d);
  uint16_t x = ab(b, d);
  if (type == TYPE_ELECTRIC && (x == ABILITY_VOLT_ABSORB || x == ABILITY_LIGHTNING_ROD || x == ABILITY_MOTOR_DRIVE)) {
    say(b, d, L("{X} de {A}\nabsorbe l'attaque !", "{A}'s\n{X} absorbed the attack!"), ability_names[x]);
    if (x == ABILITY_VOLT_ABSORB) heal(b, d, frac(D, 4));
    else change_stage(b, d, x == ABILITY_LIGHTNING_ROD ? STAGE_SPA : STAGE_SPE, 1, false);
    return true;
  }
  if ((type == TYPE_WATER && x == ABILITY_STORM_DRAIN)) {
    say(b, d, L("{X} de {A}\nabsorbe l'attaque !", "{A}'s\n{X} absorbed the attack!"), ability_names[x]);
    change_stage(b, d, STAGE_SPA, 1, false);
    return true;
  }
  if ((type == TYPE_WATER && x == ABILITY_DRY_SKIN) || (type == TYPE_GROUND && x == ABILITY_EARTH_EATER)) {
    say(b, d, L("{X} de {A}\nabsorbe l'attaque !", "{A}'s\n{X} absorbed the attack!"), ability_names[x]);
    heal(b, d, frac(D, 4));
    return true;
  }
  if (type == TYPE_WATER && x == ABILITY_WATER_ABSORB) {
    say(b, d, L("{X} de {A}\nabsorbe l'attaque !", "{A}'s\n{X} absorbed the attack!"), ability_names[x]);
    heal(b, d, frac(D, 4));
    return true;
  }
  if (type == TYPE_GRASS && x == ABILITY_SAP_SIPPER) {
    say(b, d, L("{X} de {A}\nabsorbe l'attaque !", "{A}'s\n{X} absorbed the attack!"), ability_names[x]);
    change_stage(b, d, STAGE_ATK, 1, false);
    return true;
  }
  if (type == TYPE_FIRE && x == ABILITY_WELL_BAKED_BODY) {
    say(b, d, L("{X} de {A}\nabsorbe l'attaque !", "{A}'s\n{X} absorbed the attack!"), ability_names[x]);
    change_stage(b, d, STAGE_DEF, 2, false);
    return true;
  }
  if (type == TYPE_FIRE && x == ABILITY_FLASH_FIRE) {
    vol(b, d)->flash_fire = true;
    say(b, d, L("TORCHE renforce les\nattaques Feu de {A} !", "FLASH FIRE powered up\n{A}'s Fire moves!"), "");
    return true;
  }
  return false;
}

static void attack(Battle *b, int a, uint16_t move, const MoveInfo *m, uint8_t pivot_target) {
  int d = vs(b, a);
  Fighter *A = mon(b, a), *D = mon(b, d);
  Volatile *va = vol(b, a), *vd = vol(b, d);
  uint8_t e = m->effect;
  bool boosted;
  uint8_t type = move_type(b, a, m, &boosted);

  if (!D->hp) return fail(b, a);
  if (e == EFFECT_EXPLODE && (ab(b, d) == ABILITY_DAMP || A->ability == ABILITY_DAMP))
    return say(b, a, L("MOITEUR empêche\nl'explosion !", "DAMP prevents\nthe explosion!"), "");
  if (e == EFFECT_FAKE_OUT && !va->fresh) return fail(b, a);
  if (e == EFFECT_SUCKER_PUNCH) {
    const Action *t = &b->act[d];
    uint16_t tm = pokemon_move(&D->p, t->index);
    if (vd->moved || t->kind != ACT_MOVE || !tm || move_is_status(&move_info[tm])) return fail(b, a);
  }
  bool pierce = (A->ability == ABILITY_UNSEEN_FIST || A->ability == ABILITY_PIERCING_DRILL) && is_contact(b, a, m);
  if (vd->protecting && !pierce) {
    say(b, d, L("{A} se protège !", "{A} protected itself!"), "");
    if (e == EFFECT_EXPLODE && b->spread <= 1) hurt(b, a, A->hp), fainted(b, a);
    crash(b, a, m);
    va->rampage = 0;
    return;
  }
  if (!accurate(b, a, m, move)) {
    say(b, d, L("{A} évite l'attaque !", "{A} avoided the attack!"), "");
    if (e == EFFECT_EXPLODE && b->spread <= 1) hurt(b, a, A->hp), fainted(b, a);
    crash(b, a, m);
    va->rampage = 0;
    if (A->hp && item_effect(A) == ITEM_EFFECT_BLUNDER_POLICY) {
      say(b, a, L("{X} de {A}\ns'active !", "{A}'s\n{X} activated!"), item_names[A->p.item]);
      lose_item(b, a);
      change_stage(b, a, STAGE_SPE, 2, false);
    }
    return;
  }
  if (absorb(b, a, type)) return crash(b, a, m);
  if (m->sound && ab(b, d) == ABILITY_SOUNDPROOF) return say(b, d, L("{A} n'est pas affecté\npar le son !", "{A} is unaffected\nby sound!"), "");
  if (e == EFFECT_BRICK_BREAK && (side_of(b, d)->reflect || side_of(b, d)->light_screen)) {
    side_of(b, d)->reflect = side_of(b, d)->light_screen = 0;
    say(b, a, L("{A} brise les murs !", "{A} shattered\nthe barriers!"), "");
  }
  /* Protéen, Libéro : le lanceur prend le type de son attaque (une fois par entrée) */
  if ((A->ability == ABILITY_PROTEAN || A->ability == ABILITY_LIBERO) && !va->typed && type != TYPE_MYSTERY &&
      (va->type[0] != type || va->type[1] != type)) {
    va->type[0] = va->type[1] = type, va->typed = true;
    say(b, a, L("{A} devient\nde type {X} !", "{A} transformed\ninto the {X} type!"), type_names[type]);
  }

  int hits = e == EFFECT_DOUBLE_HIT ? 2 : 1;
  if (e == EFFECT_MULTI_HIT) {
    static const uint8_t table[8] = {2, 2, 2, 3, 3, 3, 4, 5}; /* 3/8, 3/8, 1/8, 1/8 */
    hits = A->ability == ABILITY_SKILL_LINK ? 5 : item_effect(A) == ITEM_EFFECT_LOADED_DICE ? 4 + (int)rng_below(&b->rng, 2) :
                                                  table[rng_below(&b->rng, 8)];
  }
  bool parental = A->ability == ABILITY_PARENTAL_BOND && hits == 1 && e != EFFECT_EXPLODE && !fixed_damage(e);
  if (parental) hits = 2;
  uint32_t dealt = 0;
  bool hit_target = true, crit_any = false;
  uint8_t eff = 16;
  int done = 0;
  for (; done < hits && D->hp && A->hp; done++) {
    uint16_t da = ab(b, d);
    int stage = m->high_crit + (item_effect(A) == ITEM_EFFECT_SCOPE_LENS) + (A->ability == ABILITY_SUPER_LUCK) +
                (res(b, a, TYPE_ROCK) ? (res(b, a, TYPE_ROCK) + 1) / 2 : 0);
    if (stage > 3) stage = 3;
    static const uint8_t odds[3] = {24, 8, 2};
    bool crit = da != ABILITY_SHELL_ARMOR && da != ABILITY_BATTLE_ARMOR && !fixed_damage(e) &&
                (e == EFFECT_ALWAYS_CRIT || (A->ability == ABILITY_MERCILESS && (D->status == STATUS_POISON || D->status == STATUS_TOXIC)) ||
                 stage >= 3 || !rng_below(&b->rng, odds[stage]));
    uint16_t dmg = damage(b, a, m, crit, (uint8_t)(85 + rng_below(&b->rng, 16)), &eff);
    if (!eff) {
      va->rampage = 0;
      say(b, a, L("Ça n'affecte pas\n{B}…", "It doesn't affect\n{B}…"), "");
      return crash(b, a, m);
    }
    if (parental && done == 1) dmg = (uint16_t)(dmg / 4 ? dmg / 4 : 1);
    if (da == ABILITY_ICE_FACE && !D->ice_broken && !m->special && !vd->substitute) {
      D->ice_broken = true, dmg = 0;
      say(b, d, L("TÊTE DE GEL protège\n{A} !", "ICE FACE\nprotects {A}!"), "");
    }
    if (resist_berry(b, d, type, eff) && !vd->substitute) {
      say(b, d, L("{X} réduit les\ndégâts sur {A} !", "{X} weakened the\ndamage to {A}!"), item_names[D->p.item]);
      lose_item(b, d);
    }
    dealt += strike(b, a, dmg, &hit_target);
    if (crit) crit_any = true, say(b, a, L("Coup critique !", "A critical hit!"), "");
    if (hit_target) hit_effects(b, a, type, m, crit, eff);
  }
  if (eff > 16) say(b, a, L("C'est super efficace !", "It's super effective!"), "");
  else if (eff < 16) say(b, a, L("Ce n'est pas très\nefficace…", "It's not very\neffective…"), "");
  if (hits > 1) {
    char n[2] = {(char)('0' + done), 0};
    say(b, a, L("Touché {X} fois !", "Hit {X} time(s)!"), n);
  }
  (void)crit_any;

  /* conséquences pour le lanceur */
  bool guard = magic_guard(b, a);
  if (dealt) {
    if (e == EFFECT_DRAIN) {
      uint16_t amount = (uint16_t)(dealt / 2 ? dealt / 2 : 1);
      if (item_effect(A) == ITEM_EFFECT_BIG_ROOT) amount = (uint16_t)(amount * 13 / 10);
      if (D->ability == ABILITY_LIQUID_OOZE) {
        say(b, a, L("{A} aspire du\nSUINTEMENT !", "{A} sucked up\nthe LIQUID OOZE!"), "");
        hurt(b, a, amount);
      } else {
        heal(b, a, amount);
        say(b, d, L("L'énergie de {A}\nest drainée !", "{A} had its\nenergy drained!"), "");
      }
    }
    static const uint8_t recoil_div[3] = {4, 3, 2};
    if (e >= EFFECT_RECOIL4 && e <= EFFECT_RECOIL2 && A->ability != ABILITY_ROCK_HEAD && !guard && A->hp) {
      uint32_t r = dealt / recoil_div[e - EFFECT_RECOIL4];
      say(b, a, L("{A} se blesse\nau contrecoup !", "{A} is hit\nwith recoil!"), "");
      hurt(b, a, (uint16_t)(r ? r : 1));
    }
    if (item_effect(A) == ITEM_EFFECT_SHELL_BELL) heal(b, a, (uint16_t)(dealt / 8 ? dealt / 8 : 1));
    if (item_effect(A) == ITEM_EFFECT_KINGS_ROCK && hit_target && e != EFFECT_FLINCH && D->hp && !vd->moved &&
        !sheer(b, a, m) && rng_percent(&b->rng, 10))
      vd->flinch = true;
  }
  if (m == &struggle) {
    say(b, a, L("{A} se blesse\nau contrecoup !", "{A} is hit\nwith recoil!"), "");
    hurt(b, a, frac(A, 4));
  }
  if (item_effect(A) == ITEM_EFFECT_LIFE_ORB && dealt && A->hp && !guard && !sheer(b, a, m)) {
    hurt(b, a, frac(A, 10));
    say(b, a, L("{A} est blessé\npar l'ORBE VIE !", "{A} lost some\nof its HP!"), "");
  }
  if (is_contact(b, a, m) && hit_target && dealt) contact_effects(b, a);
  if (hit_target && dealt && A->hp && item_effect(D) == ITEM_EFFECT_JABOCA_BERRY && item_info[D->p.item].type == m->special &&
      !magic_guard(b, a)) {
    say(b, d, L("{A} mange sa\n{X} !", "{A} ate its\n{X}!"), item_names[D->p.item]);
    lose_item(b, d);
    hurt(b, a, frac(A, 8));
  }
  if (dealt && hit_target && A->hp) { /* Résonances offensives */
    if (res(b, a, TYPE_GRASS)) heal(b, a, (uint16_t)(dealt * res(b, a, TYPE_GRASS) / 10 ? dealt * res(b, a, TYPE_GRASS) / 10 : 1));
    if (D->hp && rng_percent(&b->rng, 10u * res(b, a, TYPE_POISON))) give_status(b, d, STATUS_POISON, a);
    if (D->hp && rng_percent(&b->rng, 10u * res(b, a, TYPE_FIRE))) give_status(b, d, STATUS_BURN, a);
    if (D->hp && rng_percent(&b->rng, 10u * res(b, a, TYPE_ICE))) change_stage(b, d, STAGE_SPE, -1, true);
    if (rng_percent(&b->rng, 10u * res(b, a, TYPE_BUG))) change_stage(b, a, STAGE_SPE, 1, false);
  }
  if (dealt || !hit_target) secondary(b, a, m, hit_target);
  if (A->ability == ABILITY_TOXIC_CHAIN && dealt && hit_target && D->hp && rng_percent(&b->rng, 30)) give_status(b, d, STATUS_TOXIC, a);
  if (e == EFFECT_KNOCK_OFF && hit_target && D->hp && can_lose_item(b, d) && D->ability != ABILITY_STICKY_HOLD) {
    say(b, a, L("{A} fait tomber\nl'objet de {B} !", "{A} knocked off\n{B}'s item!"), "");
    lose_item(b, d);
  }
  if (e == EFFECT_RAPID_SPIN && A->hp) {
    Side *as = side_of(b, a);
    if (as->spikes || as->toxic_spikes || as->stealth_rock || va->seeded) say(b, a, L("{A} se libère !", "{A} got free!"), "");
    as->spikes = as->toxic_spikes = 0, as->stealth_rock = false, va->seeded = false;
    change_stage(b, a, STAGE_SPE, 1, false);
  }
  if (e == EFFECT_RECHARGE) va->recharge = true;
  if (e == EFFECT_EXPLODE && b->spread <= 1) hurt(b, a, A->hp);
  bool foe_down = fainted(b, d);
  fainted(b, a);
  if (foe_down && A->hp) {
    if (A->ability == ABILITY_MOXIE) change_stage(b, a, STAGE_ATK, 1, false);
    if (res(b, a, TYPE_FIGHTING)) change_stage(b, a, STAGE_ATK, res(b, a, TYPE_FIGHTING) == 3 ? 2 : 1, false);
    if (A->ability == ABILITY_CHILLING_NEIGH) change_stage(b, a, STAGE_ATK, 1, false);
    if (A->ability == ABILITY_GRIM_NEIGH) change_stage(b, a, STAGE_SPA, 1, false);
    if (A->ability == ABILITY_EELEVATE || A->ability == ABILITY_BEAST_BOOST) { /* la meilleure stat monte */
      int best = STAT_ATK;
      for (int k = STAT_ATK; k < NUM_STATS; k++)
        if (A->stat[k] > A->stat[best]) best = k;
      change_stage(b, a, best - 1, 1, false);
    }
  }
  if (e == EFFECT_DRAG_OUT && hit_target && D->hp && A->hp) force_out(b, d);
  if (hit_target && dealt && D->hp && A->hp && item_effect(D) == ITEM_EFFECT_RED_CARD && other_alive(b, a, -1) >= 0) {
    say(b, d, L("{A} brandit le\n{X} !", "{A} held up\nits {X}!"), item_names[D->p.item]);
    lose_item(b, d);
    force_out(b, a);
    return;
  }
  if (hit_target && dealt && D->hp && item_effect(D) == ITEM_EFFECT_EJECT_BUTTON && other_alive(b, d, -1) >= 0) {
    say(b, d, L("{X} de {A}\ns'active !", "{A}'s\n{X} activated!"), item_names[D->p.item]);
    lose_item(b, d);
    enter(b, d, (uint8_t)other_alive(b, d, -1), false, false);
    vol(b, d)->moved = true;
  }
  if (e == EFFECT_PIVOT && A->hp && dealt) {
    int next = other_alive(b, a, pivot_target);
    if (next >= 0 && !b->result) {
      update_result(b);
      if (!b->result) enter(b, a, (uint8_t)next, false, false), vol(b, a)->moved = true;
    }
  }
}

/* ------------------------------------------------------------------------- un tour */

static uint8_t forced_slot(const Battle *b, int s, uint8_t slot) {
  const Volatile *v = battle_vol((Battle *)b, s);
  if (v->charging) return (uint8_t)(v->charging - 1);
  if (v->locked_slot) return (uint8_t)(v->locked_slot - 1);
  return slot;
}

/* Ce qui peut empêcher d'agir (ou y mettre fin) ; vrai si le Pokémon agit. */
static bool can_act(Battle *b, int s, const MoveInfo *m) {
  Fighter *f = mon(b, s);
  Volatile *v = vol(b, s);
  if (v->recharge) {
    v->recharge = false;
    say(b, s, L("{A} doit se\nrecharger !", "{A} must\nrecharge!"), "");
    return false;
  }
  if (f->status == STATUS_SLEEP) {
    uint8_t steps = f->ability == ABILITY_EARLY_BIRD ? 2 : 1;
    f->status_turns = f->status_turns > steps ? (uint8_t)(f->status_turns - steps) : 0;
    if (f->status_turns) {
      say(b, s, L("{A} dort\nprofondément…", "{A} is fast\nasleep…"), "");
      return false;
    }
    set_status(b, s, STATUS_NONE);
    say(b, s, L("{A} se réveille !", "{A} woke up!"), "");
  }
  if (f->status == STATUS_FREEZE) {
    bool thaw = m && (m->type == TYPE_FIRE && !move_is_status(m) && m->power >= 80);
    if (!thaw && !rng_percent(&b->rng, 20)) {
      say(b, s, L("{A} est gelé !", "{A} was frozen solid!"), "");
      return false;
    }
    set_status(b, s, STATUS_NONE);
    say(b, s, L("{A} dégèle !", "{A} thawed out!"), "");
  }
  if (f->ability == ABILITY_TRUANT) {
    v->truant = !v->truant;
    if (!v->truant) {
      say(b, s, L("{A} paresse !", "{A} is loafing around!"), "");
      return false;
    }
  }
  if (v->flinch) {
    say(b, s, L("{A} a peur et\nn'attaque pas !", "{A} flinched!"), "");
    if (f->ability == ABILITY_STEADFAST) change_stage(b, s, STAGE_SPE, 1, false);
    return false;
  }
  if (v->confused) {
    if (!--v->confused) {
      say(b, s, L("{A} n'est plus confus !", "{A} snapped out\nof confusion!"), "");
    } else {
      say(b, s, L("{A} est confus !", "{A} became confused!"), "");
      if (rng_percent(&b->rng, 33)) {
        uint32_t atk = staged(f->stat[STAT_ATK], v->stage[STAGE_ATK]), def = staged(f->stat[STAT_DEF], v->stage[STAGE_DEF]);
        say(b, s, L("Il se blesse dans\nsa confusion !", "It hurt itself in\nits confusion!"), "");
        hurt(b, s, (uint16_t)(22 * 40 * atk / (def ? def : 1) / 50 + 2));
        fainted(b, s);
        return false;
      }
    }
  }
  if (f->status == STATUS_PARALYSIS && rng_percent(&b->rng, 25)) {
    say(b, s, L("{A} est paralysé !\nIl ne peut pas attaquer !", "{A} is paralyzed!\nIt can't move!"), "");
    return false;
  }
  return true;
}

static int priority(Battle *b, int s, const Action *act);

/* La cible d'une attaque à cible unique : celle choisie, ou l'autre adversaire si elle est tombée. */
static int aim_of(Battle *b, int a) {
  if (!b->doubles) return a ^ 1;
  int d = b->act[a].aim;
  if (d >= npos(b) || !foes(a, d)) d = a ^ 1;
  if (!mon(b, d)->hp && mon(b, d ^ 2)->hp) d ^= 2;
  return d;
}

static void run_move(Battle *b, int a, uint8_t slot, uint8_t bp_target) {
  Fighter *A = mon(b, a);
  Volatile *va = vol(b, a);
  slot = forced_slot(b, a, slot);
  va->moved = true;
  uint16_t move = pokemon_move(&A->p, slot);
  const MoveInfo *m = &move_info[move];
  if (!can_act(b, a, move ? m : NULL)) {
    va->charging = 0, va->rampage = 0;
    uint8_t ie = item_effect(A);
    if (!va->locked_slot || ((ie < ITEM_EFFECT_CHOICE_ATK || ie > ITEM_EFFECT_CHOICE_SPE) && A->ability != ABILITY_GORILLA_TACTICS))
      va->locked_slot = 0;
    return;
  }
  bool continuing = va->charging || va->rampage;
  /* Aucune attaque utilisable (plus de PP, Provoc, Choix bloqué sur une attaque vide) : Lutte. */
  bool usable = false;
  for (uint8_t k = 0; k < MOVES_MAX; k++) usable |= battle_can_move(b, a, k);
  uint16_t x = A->ability;
  mold = x == ABILITY_MOLD_BREAKER || x == ABILITY_TERAVOLT || x == ABILITY_TURBOBLAZE ? a : -1;
  if (!continuing && !usable) {
    say(b, a, L("{A} n'a plus d'attaque !\nIl utilise LUTTE !", "{A} has no moves left!\nIt used STRUGGLE!"), "");
    attack(b, a, 0, &struggle, 0);
    mold = -1;
    return;
  }
  if (!continuing && (!move || !A->pp[slot])) return mold = -1, fail(b, a);
  if (va->taunt && move_is_status(m)) return mold = -1, say(b, a, L("{A} ne peut pas\nutiliser {X} !", "{A} can't use\n{X}!"), move_names[move]);
  if (m->effect != EFFECT_PROTECT) va->protect_chain = 0;

  /* les PP partent au premier tour (charge comprise), deux par deux face à Pression */
  if (!continuing) {
    uint8_t cost = foe_has(b, a, ABILITY_PRESSURE) ? 2 : 1;
    A->pp[slot] = (uint8_t)(A->pp[slot] > cost ? A->pp[slot] - cost : 0);
  }
  if (!va->charging && (m->effect == EFFECT_CHARGE || (m->effect == EFFECT_SOLAR && weather_for(b, a) != WEATHER_SUN))) {
    say(b, a, m->effect == EFFECT_SOLAR ? L("{A} absorbe\nla lumière !", "{A} took\nin sunlight!") : L("{A} s'entoure d'une\nlumière intense !", "{A} became cloaked\nin a harsh light!"), "");
    if (item_effect(A) == ITEM_EFFECT_POWER_HERB) {
      say(b, a, L("{A} est chargé grâce\nà {X} !", "{A} became fully\ncharged due to its {X}!"), item_names[A->p.item]);
      lose_item(b, a);
    } else {
      va->charging = (uint8_t)(slot + 1);
      mold = -1;
      return;
    }
  }
  va->charging = 0;
  uint8_t ie = item_effect(A);
  if (((ie >= ITEM_EFFECT_CHOICE_ATK && ie <= ITEM_EFFECT_CHOICE_SPE) || A->ability == ABILITY_GORILLA_TACTICS) && !va->locked_slot)
    va->locked_slot = (uint8_t)(slot + 1);
  va->streak = va->last_move == move ? (uint8_t)(va->streak + 1) : 0, va->last_move = move;
  if (m->effect == EFFECT_RAMPAGE && !va->rampage) {
    va->rampage = (uint8_t)(2 + rng_below(&b->rng, 2)), va->locked_slot = (uint8_t)(slot + 1);
  }

  int d = aim_of(b, a);
  BattleEvent *ev = push(b, EV_MOVE, a, move);
  if (ev) ev->text[0] = (char)('0' + (m->spread && b->doubles ? 9 : d)); /* la cible, pour l'animation */
  say(b, a, L("{A} utilise\n{X} !", "{A} used\n{X}!"), move_names[move]);
  b->acting = (int8_t)a, b->target = (int8_t)d;
  if (priority(b, a, &b->act[a]) > 0 && (!move_is_status(m) || targets_foe(m->effect)) && mon(b, d)->hp) {
    uint16_t da = ab(b, d);
    if (da == ABILITY_ARMOR_TAIL || da == ABILITY_QUEENLY_MAJESTY || da == ABILITY_DAZZLING ||
        (b->terrain == TERRAIN_PSYCHIC && grounded(b, d))) {
      mold = -1;
      say(b, d, L("{A} est protégé\ndes attaques rapides !", "{A} is protected from\npriority moves!"), "");
      b->acting = b->target = -1;
      return;
    }
  }
  if (move_is_status(m)) {
    status_move(b, a, move, m, bp_target, false);
  } else if (b->doubles && m->spread) { /* attaque de zone : chaque Pokémon visé, à 75 % */
    int list[3], n = 0;
    for (int q = 0; q < npos(b); q++)
      if (q != a && mon(b, q)->hp && (m->spread == 2 || foes(a, q))) list[n++] = q;
    if (!n) fail(b, a);
    for (int k = 0; k < n && mon(b, a)->hp && !b->result; k++) {
      b->target = (int8_t)list[k], b->spread = (uint8_t)(n > 1 ? n - k : 0);
      attack(b, a, move, m, bp_target);
      update_result(b);
    }
    b->spread = 0;
  } else {
    attack(b, a, move, m, bp_target);
  }
  mold = -1;
  b->acting = b->target = -1;
  if (m->sound && A->hp && item_effect(A) == ITEM_EFFECT_THROAT_SPRAY && mon(b, a) == A) {
    say(b, a, L("{X} de {A}\ns'active !", "{A}'s\n{X} activated!"), item_names[A->p.item]);
    lose_item(b, a);
    change_stage(b, a, STAGE_SPA, 1, false);
  }

  if (va->rampage && !--va->rampage) {
    va->locked_slot = 0;
    if (A->hp) {
      say(b, a, L("{A} est épuisé\npar sa colère !", "{A} became confused\ndue to fatigue!"), "");
      confuse(b, a);
    }
  }
}

static int priority(Battle *b, int s, const Action *act) {
  if (act->kind != ACT_MOVE && !battle_forced_move(b, s)) return 0;
  Fighter *f = mon(b, s);
  uint16_t move = pokemon_move(&f->p, forced_slot(b, s, act->index));
  if (!move) return 0;
  const MoveInfo *m = &move_info[move];
  int p = move_priority(m);
  if (f->ability == ABILITY_PRANKSTER && move_is_status(m)) p++;
  if (f->ability == ABILITY_GALE_WINGS && m->type == TYPE_FLYING && f->hp == f->max_hp) p++;
  if (f->ability == ABILITY_TRIAGE && (m->effect == EFFECT_HEAL_HALF || m->effect == EFFECT_HEAL_WEATHER || m->effect == EFFECT_DRAIN))
    p += 3;
  return p;
}

static int first_to_move(Battle *b, const Action act[2]) {
  int p0 = priority(b, 0, &act[0]), p1 = priority(b, 1, &act[1]);
  if (p0 != p1) return p0 > p1 ? 0 : 1;
  bool q0 = (item_effect(mon(b, 0)) == ITEM_EFFECT_QUICK_CLAW && rng_percent(&b->rng, 20)) ||
            (mon(b, 0)->ability == ABILITY_QUICK_DRAW && rng_percent(&b->rng, 30));
  bool q1 = (item_effect(mon(b, 1)) == ITEM_EFFECT_QUICK_CLAW && rng_percent(&b->rng, 20)) ||
            (mon(b, 1)->ability == ABILITY_QUICK_DRAW && rng_percent(&b->rng, 30));
  if (q0 != q1) return q0 ? 0 : 1;
  uint32_t s0 = speed(b, 0), s1 = speed(b, 1);
  if (s0 != s1) return (s0 > s1) != (b->trick_room > 0) ? 0 : 1;
  return (int)rng_below(&b->rng, 2);
}

/* L'ordre d'action des positions : priorité, Vive Griffe et Tir Vif, Vitesse (inversée sous
 * Distorsion). act NULL : à la Vitesse seule (talents d'entrée). */
static void order(Battle *b, const Action *act, uint8_t out[POSITIONS]) {
  if (!b->doubles) {
    int first = act ? first_to_move(b, act) : speed(b, 0) >= speed(b, 1) ? 0 : 1;
    out[0] = (uint8_t)first, out[1] = (uint8_t)(first ^ 1);
    return;
  }
  int prio[POSITIONS];
  bool quick[POSITIONS];
  uint32_t spd[POSITIONS];
  for (int p = 0; p < POSITIONS; p++) {
    Fighter *f = mon(b, p);
    out[p] = (uint8_t)p;
    prio[p] = act ? priority(b, p, &act[p]) : 0;
    quick[p] = act && ((item_effect(f) == ITEM_EFFECT_QUICK_CLAW && rng_percent(&b->rng, 20)) ||
                       (f->ability == ABILITY_QUICK_DRAW && rng_percent(&b->rng, 30)));
    spd[p] = speed(b, p);
  }
  for (int i = 1; i < POSITIONS; i++)
    for (int j = i; j > 0; j--) {
      int p = out[j], q = out[j - 1];
      bool before = prio[p] != prio[q] ? prio[p] > prio[q] : quick[p] != quick[q] ? quick[p] :
                    spd[p] != spd[q] ? (spd[p] > spd[q]) != (b->trick_room > 0) : false;
      if (!before) break;
      out[j] = (uint8_t)q, out[j - 1] = (uint8_t)p;
    }
}

static void end_of_turn(Battle *b, const uint8_t *ord) {
  uint8_t w = weather(b);
  for (int k = 0; k < npos(b); k++) {
    int s = ord[k];
    Fighter *f = mon(b, s);
    Volatile *v = vol(b, s);
    uint16_t x = f->ability;
    if (!f->hp) continue;
    if (w == WEATHER_SAND && !has_type(b, s, TYPE_ROCK) && !has_type(b, s, TYPE_GROUND) && !has_type(b, s, TYPE_STEEL) &&
        x != ABILITY_SAND_VEIL && x != ABILITY_SAND_RUSH && x != ABILITY_SAND_FORCE && x != ABILITY_OVERCOAT &&
        item_effect(f) != ITEM_EFFECT_SAFETY_GOGGLES &&
        !magic_guard(b, s)) {
      say(b, s, L("{A} est blessé par la\ntempête de sable !", "{A} is buffeted\nby the sandstorm!"), "");
      hurt(b, s, frac(f, 16));
      if (fainted(b, s)) continue;
    }
    if (x == ABILITY_DRY_SKIN && (w == WEATHER_RAIN || w == WEATHER_SUN) && !magic_guard(b, s)) {
      if (w == WEATHER_RAIN) heal(b, s, frac(f, 8));
      else hurt(b, s, frac(f, 8));
      if (fainted(b, s)) continue;
    }
    if (b->terrain == TERRAIN_GRASSY && grounded(b, s) && f->hp < f->max_hp) heal(b, s, frac(f, 16));
    if (res(b, s, TYPE_WATER) && f->hp < f->max_hp) {
      heal(b, s, (uint16_t)(frac(f, 32) * res(b, s, TYPE_WATER)));
      say(b, s, L("La RÉSONANCE EAU\nsoigne {A} !", "The WATER RESONANCE\nheals {A}!"), "");
    }
    if (f->status && rng_percent(&b->rng, 15u * res(b, s, TYPE_FAIRY))) {
      set_status(b, s, STATUS_NONE);
      say(b, s, L("La RÉSONANCE FÉE\nguérit {A} !", "The FAIRY RESONANCE\ncures {A}!"), "");
    }
    if (((w == WEATHER_RAIN && x == ABILITY_RAIN_DISH) || (w == WEATHER_SNOW && x == ABILITY_ICE_BODY)) && f->hp < f->max_hp) {
      ability_says(b, s, L("{X} soigne\n{A} !", "{X} restored\n{A}'s HP!"));
      heal(b, s, frac(f, 16));
    }
    if (w == WEATHER_SUN && x == ABILITY_SOLAR_POWER && !magic_guard(b, s)) {
      hurt(b, s, frac(f, 8));
      if (fainted(b, s)) continue;
    }
    uint8_t ie = item_effect(f);
    if ((ie == ITEM_EFFECT_LEFTOVERS || (ie == ITEM_EFFECT_BLACK_SLUDGE && has_type(b, s, TYPE_POISON))) && f->hp < f->max_hp) {
      heal(b, s, frac(f, 16));
      say(b, s, L("{A} récupère un peu\nde PV grâce à {X} !", "{A} restored a little\nHP using its {X}!"), item_names[f->p.item]);
    } else if (ie == ITEM_EFFECT_BLACK_SLUDGE && !has_type(b, s, TYPE_POISON) && !magic_guard(b, s)) {
      say(b, s, L("{A} est blessé\npar la BOUE NOIRE !", "{A} is hurt\nby the BLACK SLUDGE!"), "");
      hurt(b, s, frac(f, 8));
      if (fainted(b, s)) continue;
    }
    if (v->seeded && mon(b, vs(b, s))->hp && !magic_guard(b, s)) {
      uint16_t lost = hurt(b, s, frac(f, 8));
      say(b, s, L("VAMPIGRAINE draine\nl'énergie de {A} !", "{A}'s health is\nsapped by LEECH SEED!"), "");
      heal(b, vs(b, s), lost);
      if (fainted(b, s)) continue;
    }
    if (f->status == STATUS_BURN || f->status == STATUS_POISON || f->status == STATUS_TOXIC) {
      bool poisoned = f->status != STATUS_BURN;
      if (poisoned && x == ABILITY_POISON_HEAL) {
        if (f->hp < f->max_hp) heal(b, s, frac(f, 8)), say(b, s, L("SOIN POISON soigne\n{A} !", "POISON HEAL restores\n{A}'s HP!"), "");
      } else if (!magic_guard(b, s)) {
        uint16_t dmg = f->status == STATUS_BURN ? frac(f, 16) : f->status == STATUS_POISON ? frac(f, 8) :
                       (uint16_t)(f->max_hp * f->status_turns / 16 ? f->max_hp * f->status_turns / 16 : 1);
        say(b, s, f->status == STATUS_BURN ? L("{A} souffre\nde sa brûlure !", "{A} is hurt\nby its burn!") : L("{A} souffre\ndu poison !", "{A} is hurt\nby poison!"), "");
        hurt(b, s, dmg);
        if (fainted(b, s)) continue;
      }
      if (f->status == STATUS_TOXIC && f->status_turns < 15) f->status_turns++;
    }
    if (v->cursed && !magic_guard(b, s)) {
      say(b, s, L("{A} est touché\npar la MALÉDICTION !", "{A} is afflicted\nby the CURSE!"), "");
      hurt(b, s, frac(f, 4));
      if (fainted(b, s)) continue;
    }
    if (f->status && ((x == ABILITY_SHED_SKIN && rng_percent(&b->rng, 33)) || (x == ABILITY_HYDRATION && w == WEATHER_RAIN))) {
      set_status(b, s, STATUS_NONE);
      ability_says(b, s, L("{X} guérit\n{A} !", "{X} cured\n{A}!"));
    }
    if (f->status == STATUS_SLEEP && mon(b, vs(b, s))->ability == ABILITY_BAD_DREAMS && mon(b, vs(b, s))->hp &&
        !magic_guard(b, s)) {
      say(b, s, L("{A} fait un\nmauvais rêve !", "{A} is tormented\nby BAD DREAMS!"), "");
      hurt(b, s, frac(f, 8));
      if (fainted(b, s)) continue;
    }
    if (x == ABILITY_SPEED_BOOST && !v->fresh) change_stage(b, s, STAGE_SPE, 1, false);
    if (!f->status && item_effect(f) == ITEM_EFFECT_TOXIC_ORB) give_status(b, s, STATUS_TOXIC, s);
    if (!f->status && item_effect(f) == ITEM_EFFECT_FLAME_ORB) give_status(b, s, STATUS_BURN, s);
    if (v->taunt && !--v->taunt) say(b, s, L("{A} n'est plus\nprovoqué.", "{A} shook off\nthe TAUNT!"), "");
    if (v->on_field < 255 && ++v->on_field == 5 && x == ABILITY_SLOW_START) say(b, s, L("{A} a trouvé\nson rythme !", "{A} finally got\nits act together!"), "");
  }
  for (int s = 0; s < 2; s++) {
    Side *sd = &b->side[s];
    if (sd->reflect && !--sd->reflect) say(b, s, s == SIDE_PLAYER ? L("Votre PROTECTION\ndisparaît.", "Your team's REFLECT\nwore off!") : L("La PROTECTION ennemie\ndisparaît.", "The foe's REFLECT\nwore off!"), "");
    if (sd->light_screen && !--sd->light_screen)
      say(b, s, s == SIDE_PLAYER ? L("Votre MUR LUMIÈRE\ndisparaît.", "Your team's LIGHT SCREEN\nwore off!") : L("Le MUR LUMIÈRE ennemi\ndisparaît.", "The foe's LIGHT SCREEN\nwore off!"), "");
    if (sd->tailwind && !--sd->tailwind)
      say(b, s, s == SIDE_PLAYER ? L("Votre VENT ARRIÈRE\nretombe.", "Your team's TAILWIND\npetered out!") : L("Le VENT ARRIÈRE ennemi\nretombe.", "The foe's TAILWIND\npetered out!"), "");
    for (int k = 0; k < 2; k++) sd->v[k].flinch = sd->v[k].protecting = sd->v[k].fresh = sd->v[k].moved = sd->v[k].berserk = false;
  }
  if (b->terrain && !--b->terrain_turns) {
    b->terrain = TERRAIN_NONE;
    push(b, EV_TERRAIN, SIDE_PLAYER, TERRAIN_NONE);
    say(b, SIDE_PLAYER, L("Le champ disparaît.", "The terrain returned\nto normal."), "");
  }
  if (b->trick_room && !--b->trick_room) say(b, SIDE_PLAYER, L("Les dimensions tordues\nredeviennent normales !", "The twisted dimensions\nreturned to normal!"), "");
  if (b->weather && b->weather_turns && !--b->weather_turns) {
    say(b, SIDE_PLAYER, weather_end[lang][b->weather], "");
    b->weather = WEATHER_NONE;
  }
}

/* ------------------------------------------------------------------------- interface */

void resonance_levels(const Team *t, uint8_t out[NUM_TYPES]) {
  uint8_t count[NUM_TYPES] = {0};
  for (int i = 0; i < t->count; i++) {
    const SpeciesInfo *s = &species_info[t->mon[i].species];
    int w = item_info[t->mon[i].item].effect == ITEM_EFFECT_RESONANCE_PRISM ? 2 : 1; /* compte double */
    count[s->type1] = (uint8_t)(count[s->type1] + w);
    if (s->type2 != s->type1) count[s->type2] = (uint8_t)(count[s->type2] + w);
  }
  for (int k = 0; k < NUM_TYPES; k++) out[k] = (uint8_t)(count[k] >= 5 ? 3 : count[k] >= 4 ? 2 : count[k] >= 3 ? 1 : 0);
  out[TYPE_MYSTERY] = 0;
}

static void init_side(Side *sd, const Team *t, bool mega) {
  memset(sd, 0, sizeof *sd);
  sd->count = t->count;
  resonance_levels(t, sd->resonance);
  sd->mega_allowed = mega;
  for (int i = 0; i < t->count; i++) {
    Fighter *f = &sd->team[i];
    f->p = t->mon[i];
    for (int k = 0; k < NUM_STATS; k++) f->stat[k] = pokemon_stat(&f->p, (uint8_t)k);
    f->hp = f->max_hp = f->stat[STAT_HP];
    const SpeciesInfo *sp = &species_info[f->p.species];
    if (sd->resonance[TYPE_NORMAL] && (sp->type1 == TYPE_NORMAL || sp->type2 == TYPE_NORMAL))
      f->hp = f->max_hp = (uint16_t)(f->max_hp * (20u + sd->resonance[TYPE_NORMAL]) / 20);
    for (int k = 0; k < MOVES_MAX; k++) {
      uint16_t mv = pokemon_move(&f->p, (uint8_t)k);
      f->pp[k] = mv ? (uint8_t)move_info[mv].pp : 0;
    }
    f->ability = pokemon_ability(&f->p);
  }
}

static void announce_resonance(Battle *b, int s) {
  static const char *const level[4] = {"", " I", " II", " III"};
  for (int k = 0; k < NUM_TYPES; k++) {
    uint8_t l = side_of(b, s)->resonance[k];
    if (!l) continue;
    char text[BATTLE_TEXT];
    size_t n = 0;
    text[0] = 0;
    append(text, &n, type_names[k]);
    append(text, &n, level[l]);
    append(text, &n, " : ");
    append(text, &n, resonance_names[k]);
    text[n] = 0;
    say(b, s, s == SIDE_PLAYER ? L("Votre RÉSONANCE\n{X} !", "Your team's RESONANCE\n{X}!") : L("RÉSONANCE ennemie\n{X} !", "Foe team's RESONANCE\n{X}!"), text);
  }
}

void battle_start(Battle *b, const Team *player, const Team *foe, const char *foe_name, uint32_t seed,
                  bool player_mega, bool foe_mega) {
  battle_start_ex(b, player, foe, foe_name, seed, player_mega, foe_mega, false);
}

void battle_start_ex(Battle *b, const Team *player, const Team *foe, const char *foe_name, uint32_t seed,
                     bool player_mega, bool foe_mega, bool doubles) {
  memset(b, 0, sizeof *b);
  mold = -1;
  b->acting = b->target = -1;
  b->rng = seed ? seed : 1;
  bool wild = !foe_name; /* un Pokémon sauvage, sans dresseur */
  b->foe_name = wild ? L("Le POKéMON", "The POKéMON") : foe_name;
  b->doubles = doubles && !wild && player->count >= 2 && foe->count >= 2;
  init_side(&b->side[SIDE_PLAYER], player, player_mega);
  init_side(&b->side[SIDE_FOE], foe, foe_mega);
  for (int s = 0; s < 2; s++) b->side[s].slots = b->doubles ? 2 : 1, b->side[s].active[1] = b->doubles;
  if (wild) { /* raid : un légendaire géant */
    Fighter *f = &b->side[SIDE_FOE].team[0];
    b->raid = true;
    f->hp = f->max_hp = (uint16_t)(f->max_hp * 4);
    for (int k = STAT_ATK; k < NUM_STATS; k++) f->stat[k] = (uint16_t)(f->stat[k] * 5 / 4);
  }
  for (int s = 0; s < npos(b); s++) reset_types(b, s);
  if (!wild) say(b, SIDE_FOE, L("{X} veut\nse battre !", "{X}\nwould like to battle!"), foe_name);
  announce_resonance(b, SIDE_PLAYER);
  announce_resonance(b, SIDE_FOE);
  /* les talents d'entrée agissent dans l'ordre de Vitesse, une fois tous les Pokémon sur le terrain */
  static const uint8_t intro[POSITIONS] = {1, 3, 0, 2};
  for (int k = 0; k < POSITIONS; k++) {
    int s = intro[k];
    if (s >= npos(b)) continue;
    vol(b, s)->fresh = true;
    say(b, s, POS_SIDE(s) == SIDE_PLAYER ? L("Vas-y ! {A} !", "Go! {A}!") : wild ? L("Un {X} géant\napparaît ! (PV ×4)", "A giant {X}\nappeared! (HP ×4)") : L("{X} envoie\n{A} !", "{X} sent\nout {A}!"),
        wild ? species_names[mon(b, s)->p.species] : foe_name);
    push(b, EV_SWITCH, s, side_of(b, s)->active[POS_SLOT(s)]);
    push(b, EV_HP, s, mon(b, s)->hp);
  }
  for (int k = 0; k < POSITIONS; k++)
    if (intro[k] < npos(b)) form_change(b, intro[k]);
  uint8_t ord[POSITIONS];
  order(b, NULL, ord);
  for (int k = 0; k < npos(b); k++) entry_ability(b, ord[k]);
  update_result(b);
}

void battle_turn_all(Battle *b, const Action act[POSITIONS]) {
  b->nevents = 0;
  if (b->result) return;
  b->turn++;
  int n = npos(b);
  for (int p = 0; p < n; p++) b->act[p] = act[p];
  uint8_t ord[POSITIONS];
  order(b, b->act, ord);
  for (int k = 0; k < n; k++) { /* les changements passent avant les attaques */
    int s = ord[k];
    if (act[s].kind == ACT_SWITCH && mon(b, s)->hp && !battle_forced_move(b, s) && can_enter(side_of(b, s), act[s].index)) {
      enter(b, s, act[s].index, false, false);
      vol(b, s)->moved = true;
    }
  }
  for (int k = 0; k < n; k++) { /* puis les Méga-Évolutions, avant les attaques */
    int s = ord[k];
    if (act[s].kind == ACT_MOVE && act[s].mega && !vol(b, s)->moved && mon(b, s)->hp) mega_evolve(b, s);
  }
  order(b, b->act, ord);
  for (int k = 0; k < n; k++) {
    int s = ord[k];
    bool wants_move = act[s].kind == ACT_MOVE || battle_forced_move(b, s);
    if (wants_move && !vol(b, s)->moved && mon(b, s)->hp && !b->result) run_move(b, s, act[s].index, act[s].target);
    update_result(b);
  }
  if (!b->result) end_of_turn(b, ord);
  update_result(b);
}

void battle_turn(Battle *b, Action player, Action foe) {
  Action act[POSITIONS] = {player, foe, player, foe};
  battle_turn_all(b, act);
}

bool battle_must_replace(const Battle *b, int s) {
  const Side *sd = &b->side[POS_SIDE(s)];
  if (b->result || s >= battle_positions(b) || sd->team[sd->active[POS_SLOT(s)]].hp) return false;
  for (uint8_t i = 0; i < sd->count; i++)
    if (can_enter(sd, i)) return true;
  return false;
}

void battle_send(Battle *b, int s, uint8_t index) {
  b->nevents = 0;
  if (!can_enter(side_of(b, s), index)) return;
  enter(b, s, index, false, true);
  update_result(b);
}

bool battle_can_move(const Battle *b, int s, uint8_t slot) {
  const Fighter *f = battle_active((Battle *)b, s);
  const Volatile *v = battle_vol((Battle *)b, s);
  uint16_t move = pokemon_move(&f->p, slot);
  if (!move || !f->pp[slot]) return false;
  if ((v->taunt || item_info[f->p.item].effect == ITEM_EFFECT_ASSAULT_VEST) && move_is_status(&move_info[move]))
    return false;
  return !v->locked_slot || v->locked_slot == slot + 1;
}

bool battle_can_switch(const Battle *b, int s, uint8_t index) {
  const Side *sd = &b->side[POS_SIDE(s)];
  bool shed = item_info[battle_active((Battle *)b, s)->p.item].effect == ITEM_EFFECT_SHED_SHELL;
  return can_enter(sd, index) && (shed || !trapped(b, s)) && !battle_forced_move(b, s);
}

bool battle_forced_move(const Battle *b, int s) {
  const Volatile *v = battle_vol((Battle *)b, s);
  return v->recharge || v->charging || v->rampage;
}
