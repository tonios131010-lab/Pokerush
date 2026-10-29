#include "game/ai.h"
#include "core/rng.h"
#include "data.h"

static Fighter *active(Battle *b, int s) { return battle_active(b, s); }
static Volatile *vol(Battle *b, int s) { return battle_vol(b, s); }

/* L'adversaire d'en face pour la position s (l'autre s'il est K.O., en duo). */
static int facing(Battle *b, int s) {
  int f = s ^ 1;
  if (b->doubles && !active(b, f)->hp && active(b, f ^ 2)->hp) f ^= 2;
  return f;
}

static int boosts(const Volatile *v) {
  int sum = 0;
  for (int k = 0; k < STAGE_ACC; k++) sum += v->stage[k];
  return sum;
}

/* Le talent de la cible annule-t-il l'attaque ? */
static bool absorbed(const Fighter *target, const MoveInfo *m) {
  uint16_t a = target->ability;
  uint8_t t = m->type;
  return (t == TYPE_ELECTRIC && (a == ABILITY_VOLT_ABSORB || a == ABILITY_LIGHTNING_ROD || a == ABILITY_MOTOR_DRIVE)) ||
         (t == TYPE_WATER && a == ABILITY_WATER_ABSORB) || (t == TYPE_FIRE && a == ABILITY_FLASH_FIRE) ||
         (t == TYPE_GRASS && a == ABILITY_SAP_SIPPER) || (m->sound && a == ABILITY_SOUNDPROOF);
}

/* Les plus gros dégâts que la position `from` peut faire à la position `to` ce tour-ci. */
static uint16_t best_damage(Battle *b, int from, int to) {
  Fighter *f = active(b, from);
  uint16_t best = 0;
  for (uint8_t k = 0; k < MOVES_MAX; k++) {
    uint16_t move = pokemon_move(&f->p, k);
    if (!move || !f->pp[k] || move_is_status(&move_info[move]) || absorbed(active(b, to), &move_info[move]))
      continue;
    uint8_t eff;
    uint16_t d = battle_estimate_at(b, from, to, move, 92, &eff);
    if (d > best) best = d;
  }
  return best;
}

static bool team_statused(const Side *sd) {
  for (int i = 0; i < sd->count; i++)
    if (sd->team[i].hp && sd->team[i].status) return true;
  return false;
}

/* Les équipiers debout qui ne sont pas sur le terrain. */
static int alive_others(const Side *sd) {
  int n = 0;
  for (int i = 0; i < sd->count; i++) n += !battle_on_field(sd, i) && sd->team[i].hp;
  return n;
}

static bool team_has_ability(const Side *sd, uint8_t a1, uint8_t a2) {
  for (int i = 0; i < sd->count; i++)
    if (sd->team[i].hp && (sd->team[i].ability == a1 || sd->team[i].ability == a2)) return true;
  return false;
}

/* Vitesse moyenne de l'équipe (pour Distorsion et Vent Arrière). */
static int team_speed(const Side *sd) {
  int sum = 0, n = 0;
  for (int i = 0; i < sd->count; i++)
    if (sd->team[i].hp) sum += sd->team[i].stat[STAT_SPE], n++;
  return n ? sum / n : 0;
}

static bool is_boost(uint8_t e) {
  return (e >= EFFECT_U_ATK_UP && e <= EFFECT_SHELL_SMASH) || e == EFFECT_BELLY_DRUM || e == EFFECT_CURSE;
}

/* Note d'une attaque de statut dans la situation présente. */
static int status_score(Battle *b, int s, const MoveInfo *m, bool safe) {
  int o = facing(b, s);
  Fighter *me = active(b, s), *foe = active(b, o);
  Side *ms = &b->side[POS_SIDE(s)], *fs = &b->side[POS_SIDE(o)];
  Volatile *mv = vol(b, s), *fv = vol(b, o);
  int hp_pct = me->hp * 100 / me->max_hp;
  uint8_t e = m->effect;
  if (is_boost(e) && e != EFFECT_BELLY_DRUM) return safe && hp_pct > 55 && boosts(mv) < 4 ? 75 - boosts(mv) * 12 : 2;
  switch (e) {
    case EFFECT_BELLY_DRUM: return safe && hp_pct > 80 && mv->stage[STAGE_ATK] < 2 ? 85 : 0;
    case EFFECT_HEAL_HALF:
    case EFFECT_HEAL_WEATHER: return hp_pct < 45 ? 95 : hp_pct < 65 ? 35 : 0;
    case EFFECT_REST: return hp_pct < 35 || (me->status && hp_pct < 60) ? 85 : 0;
    case EFFECT_SLEEP:
    case EFFECT_PARALYZE:
    case EFFECT_BURN:
    case EFFECT_POISON:
    case EFFECT_BADLY_POISON:
      if (foe->status || fv->substitute) return 0;
      if (e == EFFECT_SLEEP) return 85;
      if (e == EFFECT_BURN) return foe->stat[STAT_ATK] > foe->stat[STAT_SPA] ? 75 : 40;
      if (e == EFFECT_PARALYZE) return foe->stat[STAT_SPE] > me->stat[STAT_SPE] ? 70 : 45;
      return foe->max_hp >= 150 ? 65 : 45;
    case EFFECT_CONFUSE: return fv->confused || fv->substitute ? 0 : 30;
    case EFFECT_LEECH_SEED: return fv->seeded || battle_effectiveness(TYPE_GRASS, foe) == 0 ? 0 : 50;
    case EFFECT_SPIKES: return fs->spikes < 3 && alive_others(fs) ? 30 + 12 * alive_others(fs) : 0;
    case EFFECT_TOXIC_SPIKES: return fs->toxic_spikes < 2 && alive_others(fs) ? 25 + 10 * alive_others(fs) : 0;
    case EFFECT_STEALTH_ROCK: return !fs->stealth_rock && alive_others(fs) ? 45 + 12 * alive_others(fs) : 0;
    case EFFECT_SUBSTITUTE: return !mv->substitute && hp_pct > 55 ? 40 : 0;
    case EFFECT_PROTECT: return mv->protect_chain ? 0 : (fv->seeded || foe->status == STATUS_TOXIC) ? 45 : 10;
    case EFFECT_REFLECT: return ms->reflect ? 0 : foe->stat[STAT_ATK] >= foe->stat[STAT_SPA] ? 40 : 10;
    case EFFECT_LIGHT_SCREEN: return ms->light_screen ? 0 : foe->stat[STAT_SPA] > foe->stat[STAT_ATK] ? 40 : 10;
    case EFFECT_RAIN: return b->weather != WEATHER_RAIN && team_has_ability(ms, ABILITY_SWIFT_SWIM, ABILITY_RAIN_DISH) ? 80 : 10;
    case EFFECT_SUN: return b->weather != WEATHER_SUN && team_has_ability(ms, ABILITY_CHLOROPHYLL, ABILITY_SOLAR_POWER) ? 80 : 10;
    case EFFECT_SAND: return b->weather != WEATHER_SAND && team_has_ability(ms, ABILITY_SAND_RUSH, ABILITY_SAND_FORCE) ? 60 : 8;
    case EFFECT_HAIL: return b->weather != WEATHER_SNOW ? 15 : 0;
    case EFFECT_BATON_PASS: return boosts(mv) >= 2 && alive_others(ms) ? 90 : 0;
    case EFFECT_ROAR:
    case EFFECT_HAZE: return boosts(fv) >= 2 ? 85 : 2;
    case EFFECT_HEAL_BELL: return team_statused(ms) ? 60 : 0;
    case EFFECT_TAUNT: return fv->taunt ? 0 : 20;
    case EFFECT_TRICK_ROOM: return !b->trick_room && team_speed(ms) * 4 < team_speed(fs) * 3 ? 80 : 0;
    case EFFECT_TAILWIND: return !ms->tailwind && alive_others(ms) ? 45 : 0;
    case EFFECT_T_ATK_DOWN2:
    case EFFECT_T_DEF_DOWN2:
    case EFFECT_T_SPD_DOWN2: return safe ? 25 : 5;
  }
  return 5;
}

/* L'équipier qui résiste le mieux aux attaques de l'adversaire (et à son type), ou -1. */
static int best_switch(Battle *b, int s, int *worst_eff) {
  Side *sd = &b->side[POS_SIDE(s)];
  Fighter *foe = active(b, facing(b, s));
  int best = -1, best_eff = 999;
  for (uint8_t i = 0; i < sd->count; i++) {
    if (!battle_can_switch(b, s, i)) continue;
    const Fighter *f = &sd->team[i];
    int eff = 0;
    for (uint8_t k = 0; k < MOVES_MAX; k++) {
      uint16_t mv = pokemon_move(&foe->p, k);
      if (mv && !move_is_status(&move_info[mv])) {
        int e = battle_effectiveness((uint8_t)move_info[mv].type, f);
        if (e > eff) eff = e;
      }
    }
    if (f->hp * 2 < f->max_hp) eff += 16; /* mieux vaut un équipier en forme */
    if (eff < best_eff) best_eff = eff, best = i;
  }
  *worst_eff = best_eff;
  return best;
}

Action ai_choose(Battle *b, int side) {
  int o = facing(b, side);
  Fighter *me = active(b, side), *foe = active(b, o);
  Side *mine = &b->side[POS_SIDE(side)];
  uint8_t skill = b->foe_skill > 10 ? 10 : b->foe_skill;
  uint8_t usable[MOVES_MAX], n = 0;
  bool mega = battle_can_mega(b, side) >= 0; /* l'IA méga-évolue dès qu'elle le peut */
  for (uint8_t k = 0; k < MOVES_MAX; k++)
    if (battle_can_move(b, side, k)) usable[n++] = k;
  if (!n || battle_forced_move(b, side)) return (Action){ACT_MOVE, 0, 0, false, (uint8_t)o};
  if (rng_percent(&b->rng, (10u - skill) * 6u)) return (Action){ACT_MOVE, usable[rng_below(&b->rng, n)], 0, mega, (uint8_t)o};

  uint16_t threat = best_damage(b, o, side);
  if (b->doubles && active(b, o ^ 2)->hp) {
    uint16_t t2 = best_damage(b, o ^ 2, side);
    if (t2 > threat) threat = t2;
  }
  bool slower = me->stat[STAT_SPE] < foe->stat[STAT_SPE];
  if (b->trick_room) slower = !slower;
  bool safe = threat * 3 < me->hp; /* l'adversaire ne fait pas plus d'un tiers des PV restants */

  /* menacé de K.O. sans pouvoir le rendre : changer pour un équipier qui résiste */
  if (skill >= 5 && !mega && threat >= me->hp && slower && best_damage(b, side, o) < foe->hp && !vol(b, side)->substitute) {
    int worst, i = best_switch(b, side, &worst);
    if (i >= 0 && worst <= 8 && rng_percent(&b->rng, skill * 8u)) return (Action){ACT_SWITCH, (uint8_t)i, 0, false, (uint8_t)o};
  }

  int best_score = -1000;
  uint8_t pick = usable[0], aim = (uint8_t)o;
  for (uint8_t j = 0; j < n; j++) {
    uint16_t move = pokemon_move(&me->p, usable[j]);
    const MoveInfo *m = &move_info[move];
    uint8_t e = m->effect;
    int score = -1000, target = o;
    if (move_is_status(m)) {
      score = status_score(b, side, m, safe);
    } else {
      /* chaque adversaire debout (en duo) ; une attaque de zone les touche tous, à 75 % */
      int zone = 0;
      for (int q = side ^ 1; q < battle_positions(b); q += 2) {
        Fighter *t = active(b, q);
        if (!t->hp) continue;
        int sc;
        if (absorbed(t, m)) {
          sc = -50;
        } else {
          uint8_t eff;
          uint32_t d = battle_estimate_at(b, side, q, move, 92, &eff);
          if (b->doubles && m->spread) d = d * 3 / 4;
          sc = (int)(d * 100 / t->hp);
          if (sc > 100) sc = 100;
          sc = sc * (m->accuracy ? m->accuracy : 100) / 100;
          if (d >= t->hp) sc += 60 + (move_priority(m) > 0 && slower ? 40 : 0);
          if (e == EFFECT_RECHARGE && d < t->hp) sc -= 30;
          if (e == EFFECT_EXPLODE) sc = d >= t->hp && alive_others(mine) ? sc : sc / 4;
          if ((e == EFFECT_U_SPA_DOWN2 || e == EFFECT_RECOIL3 || e == EFFECT_RECOIL2) && d < t->hp) sc -= 10;
        }
        zone += sc;
        if (sc > score) score = sc, target = q;
      }
      if (b->doubles && m->spread) {
        score = zone;
        int ally = side ^ 2;
        if (m->spread == 2 && active(b, ally)->hp && !absorbed(active(b, ally), m)) { /* Séisme : le partenaire aussi */
          uint8_t eff;
          uint32_t d = battle_estimate_at(b, side, ally, move, 92, &eff) * 3 / 4;
          score -= (int)(d * 150 / active(b, ally)->hp);
        }
      }
      if (e == EFFECT_FAKE_OUT) score = vol(b, side)->fresh ? score + 40 : -50;
      if (e == EFFECT_SUCKER_PUNCH) score = score * 2 / 3; /* échoue contre une attaque de statut */
      if (e == EFFECT_RAPID_SPIN && (mine->spikes || mine->stealth_rock)) score += 30;
      if (e == EFFECT_DRAIN && me->hp * 2 < me->max_hp) score += 15;
      if (e == EFFECT_PIVOT && !safe && alive_others(mine)) score += 20;
      if (e == EFFECT_CRASH) score = score * 9 / 10;
    }
    /* un peu de hasard, d'autant moins que le dresseur est fort */
    score = score * (100 + (int)rng_below(&b->rng, 4u * (10u - skill) + 1)) / 100;
    if (score > best_score) best_score = score, pick = usable[j], aim = (uint8_t)target;
  }
  return (Action){ACT_MOVE, pick, (uint8_t)ai_replacement(b, side), mega, aim};
}

uint8_t ai_replacement(const Battle *cb, int side) {
  Battle *b = (Battle *)cb;
  const Side *sd = &b->side[POS_SIDE(side)];
  const Fighter *foe = active(b, facing(b, side));
  int best = -1, best_score = -9999;
  for (uint8_t i = 0; i < sd->count; i++) {
    const Fighter *f = &sd->team[i];
    if (!f->hp || battle_on_field(sd, i)) continue;
    /* son meilleur coup contre l'adversaire, moins le pire coup de l'adversaire contre lui */
    int attack = 0, danger = 0;
    for (uint8_t k = 0; k < MOVES_MAX; k++) {
      uint16_t mv = pokemon_move(&f->p, k);
      if (mv && !move_is_status(&move_info[mv])) {
        int e = battle_effectiveness((uint8_t)move_info[mv].type, foe) * move_info[mv].power;
        if (e > attack) attack = e;
      }
      uint16_t fm = pokemon_move(&foe->p, k);
      if (fm && !move_is_status(&move_info[fm])) {
        int e = battle_effectiveness((uint8_t)move_info[fm].type, f) * move_info[fm].power;
        if (e > danger) danger = e;
      }
    }
    int score = attack - danger + f->hp * 100 / f->max_hp;
    if (score > best_score) best_score = score, best = i;
  }
  return (uint8_t)(best < 0 ? sd->active[POS_SLOT(side)] : best);
}
