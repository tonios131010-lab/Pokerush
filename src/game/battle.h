/* Moteur de combat 1 contre 1, sans affichage. battle_turn() résout un tour entier et range dans
 * b->events ce qui s'est passé (messages en français, PV, statuts, entrées, Méga-Évolutions, K.O.) ;
 * l'interface les rejoue ensuite avec ses animations. Tout le hasard vient de b->rng : une graine
 * donne toujours le même combat.
 *
 * Règles de la 9G au niveau 50 (pokemon.h) : table des types avec Fée, coups critiques ×1,5, brûlure
 * 1/16, paralysie qui divise la Vitesse par 2, sommeil de 1 à 3 tours, Neige, pièges d'entrée,
 * Distorsion, Vent Arrière, Méga-Évolution (une par combat et par camp), talents et objets de data/.
 *
 * Résonances (propre à PokeRush) : un type que partagent au moins 3 Pokémon d'une équipe « résonne »
 * pendant tout le combat, niveau 1 à 3 pour 3, 4, 5 Pokémon ou plus. Chaque type a son effet, qui
 * profite aux Pokémon de ce type et grandit avec le niveau (resonance_descs) : soin chaque tour pour
 * l'Eau, coups critiques pour la Roche, brûlures pour le Feu…
 *
 * Raids : un légendaire sauvage (foe_name NULL) a 4 fois plus de PV et des stats +25 %.
 *
 * Combats en duo (format VGC) : deux Pokémon de chaque camp sur le terrain. Les « positions » 0 et 1
 * sont le joueur et l'adversaire (les seules en combat simple), 2 et 3 leurs partenaires : le camp
 * d'une position est p & 1, son emplacement p >> 1. Les attaques qui visent plusieurs Pokémon
 * (Séisme, Éboulement…) font 75 % de leurs dégâts à chacun.
 * Côté 0 = le joueur, côté 1 = l'adversaire. */
#ifndef BATTLE_H
#define BATTLE_H
#include <stdbool.h>
#include <stdint.h>
#include "game/pokemon.h"

enum { STATUS_NONE, STATUS_SLEEP, STATUS_POISON, STATUS_BURN, STATUS_FREEZE, STATUS_PARALYSIS, STATUS_TOXIC };
enum { WEATHER_NONE, WEATHER_RAIN, WEATHER_SUN, WEATHER_SAND, WEATHER_SNOW };
/* Champs : pendant 5 tours, pour les Pokémon au sol (pas Vol, Lévitation, Ballon). */
enum { TERRAIN_NONE, TERRAIN_ELECTRIC, TERRAIN_GRASSY, TERRAIN_MISTY, TERRAIN_PSYCHIC };
/* Niveaux de stats : les 5 stats hors PV (ordre STAT_* - 1), puis précision et esquive. */
enum { STAGE_ATK, STAGE_DEF, STAGE_SPE, STAGE_SPA, STAGE_SPD, STAGE_ACC, STAGE_EVA, NUM_STAGES };
enum { SIDE_PLAYER, SIDE_FOE };
#define POSITIONS 4
#define POS_SIDE(p) ((p) & 1)
#define POS_SLOT(p) ((p) >> 1)

/* Un Pokémon de l'équipe pendant le combat : ce qui reste quand il sort du terrain. */
typedef struct {
  Pokemon p;                /* species devient la forme Méga après la Méga-Évolution */
  uint16_t hp, max_hp;
  uint16_t stat[NUM_STATS]; /* stats sans modificateurs */
  uint8_t status;           /* STATUS_* */
  uint8_t status_turns;     /* tours de sommeil restants, ou compteur de Toxik */
  uint8_t pp[MOVES_MAX];
  uint16_t ability;         /* ABILITY_* (Calque, Méga-Évolution, formes peuvent le changer) */
  bool item_lost;           /* objet consommé ou perdu (Délestage) */
  bool ice_broken;          /* Tête de Gel a déjà servi */
} Fighter;

/* Ce qui ne dure que tant que le Pokémon reste sur le terrain. */
typedef struct {
  int8_t stage[NUM_STAGES];
  uint8_t type[2];       /* types actuels (Protéen, Libéro les changent) */
  uint16_t substitute;   /* PV du clone */
  uint8_t confused;      /* tours restants */
  uint8_t taunt;
  uint8_t protect_chain; /* Abri réussis d'affilée */
  uint8_t rampage;       /* tours de Colère restants */
  uint8_t locked_slot;   /* attaque imposée (objets Choix, Colère) : emplacement + 1 */
  uint8_t charging;      /* Lance-Soleil : emplacement + 1 de l'attaque qui se charge */
  uint8_t on_field;      /* tours passés sur le terrain (Début Calme) */
  uint16_t last_move;    /* Métronome : la dernière attaque, et combien de fois de suite */
  uint8_t streak;
  bool recharge, seeded, cursed, flinch, protecting, flash_fire, fresh, moved, fainted, typed, berserk, truant;
} Volatile;

typedef struct {
  Fighter team[TEAM_MAX];
  uint8_t count;
  uint8_t slots;     /* emplacements sur le terrain : 1, ou 2 en duo */
  uint8_t active[2]; /* le Pokémon de chaque emplacement */
  Volatile v[2];
  uint8_t reflect, light_screen, tailwind; /* tours restants */
  uint8_t spikes, toxic_spikes;            /* couches */
  bool stealth_rock;
  bool mega_allowed, mega_used; /* Méga-Anneau ; Méga-Évolution déjà faite ce combat */
  uint8_t resonance[NUM_TYPES]; /* niveau de Résonance de chaque type (0..3) */
} Side;

/* side : la position concernée (0..3). */
enum { EV_TEXT, EV_HP, EV_SWITCH, EV_FAINT, EV_STATUS, EV_MEGA, EV_TERRAIN, EV_MOVE, EV_FORM };
#define BATTLE_TEXT 72
#define BATTLE_EVENTS 120 /* un tour en duo : jusqu'à 4 attaques et leurs suites */

typedef struct {
  uint8_t kind; /* EV_* */
  uint8_t side;
  uint16_t value;         /* EV_HP : nouveaux PV ; EV_SWITCH : indice dans l'équipe ; EV_STATUS : STATUS_* ;
                             EV_MEGA, EV_FORM : la nouvelle forme ; EV_TERRAIN : TERRAIN_* ; EV_MOVE : l'attaque lancée
                             (text[0] : '0' + la position visée, '9' pour une attaque de zone) */
  char text[BATTLE_TEXT]; /* EV_TEXT : deux lignes au plus, séparées par '\n' */
} BattleEvent;

enum { ACT_MOVE, ACT_SWITCH };
typedef struct {
  uint8_t kind;   /* ACT_* */
  uint8_t index;  /* emplacement d'attaque (0..3) ou indice dans l'équipe */
  uint8_t target; /* Relais, Demi-Tour… : le Pokémon qui prend la relève (indice dans l'équipe) */
  bool mega;      /* Méga-Évoluer avant d'attaquer */
  uint8_t aim;    /* duo : la position visée par une attaque à cible unique */
} Action;

enum { BATTLE_ONGOING, BATTLE_WON, BATTLE_LOST };

typedef struct {
  Side side[2];
  uint8_t weather, weather_turns; /* 0 tour = permanent */
  uint8_t trick_room;             /* tours de Distorsion restants */
  uint8_t terrain, terrain_turns; /* TERRAIN_* */
  bool raid;                      /* légendaire sauvage géant */
  bool doubles;                   /* combat en duo : 4 positions */
  int8_t acting, target;          /* l'action en cours : son auteur et sa cible (-1 : aucune) */
  uint8_t spread;                 /* attaque à plusieurs cibles : dégâts ×0,75 */
  uint8_t result;                 /* BATTLE_* */
  uint16_t turn;
  uint32_t rng;
  Action act[POSITIONS]; /* les actions du tour en cours (Coup Bas) */
  const char *foe_name; /* « MARC »… pour les messages */
  uint8_t foe_skill;    /* 0..10 : finesse de l'IA adverse (trainer.h) */
  uint8_t nevents;
  BattleEvent events[BATTLE_EVENTS];
} Battle;

/* Prépare le combat et envoie les premiers Pokémon (événements d'entrée). player_mega,
 * foe_mega : le camp a le droit de Méga-Évoluer. foe_name NULL : un Pokémon sauvage. doubles : en
 * duo, les deux premiers de chaque équipe entrent (il en faut au moins deux de chaque côté). */
void battle_start_ex(Battle *b, const Team *player, const Team *foe, const char *foe_name, uint32_t seed,
                     bool player_mega, bool foe_mega, bool doubles);
void battle_start(Battle *b, const Team *player, const Team *foe, const char *foe_name, uint32_t seed,
                  bool player_mega, bool foe_mega);
/* Résout un tour : une action par position (act[0..1] en simple, act[0..3] en duo). Les événements
 * précédents sont effacés. */
void battle_turn_all(Battle *b, const Action act[POSITIONS]);
void battle_turn(Battle *b, Action player, Action foe);
/* Après un K.O. : la position doit recevoir un autre Pokémon (battle_send). */
bool battle_must_replace(const Battle *b, int pos);
void battle_send(Battle *b, int pos, uint8_t index);
/* Nombre de positions du combat (2 ou 4), et la position est-elle occupée par un Pokémon debout ? */
static inline int battle_positions(const Battle *b) { return b->doubles ? 4 : 2; }

/* Ce que l'interface et l'IA peuvent demander. */
bool battle_can_move(const Battle *b, int side, uint8_t slot); /* PP, Provoc, Choix, Veste de Combat… */
bool battle_can_switch(const Battle *b, int side, uint8_t index);
bool battle_forced_move(const Battle *b, int side); /* recharge, charge, Colère : l'action est imposée */
/* L'attaque vise-t-elle un seul adversaire (à choisir en duo) ? */
bool battle_move_targets_foe(uint16_t move);
/* La forme Méga que le Pokémon actif peut prendre ce tour-ci, ou -1. */
int battle_can_mega(const Battle *b, int side);
static inline Fighter *battle_active(Battle *b, int pos) {
  return &b->side[POS_SIDE(pos)].team[b->side[POS_SIDE(pos)].active[POS_SLOT(pos)]];
}
static inline Volatile *battle_vol(Battle *b, int pos) { return &b->side[POS_SIDE(pos)].v[POS_SLOT(pos)]; }
static inline bool battle_alive(Battle *b, int pos) { return pos < battle_positions(b) && battle_active(b, pos)->hp; }
/* Le Pokémon i de l'équipe est-il sur le terrain ? */
static inline bool battle_on_field(const Side *sd, int i) { return i == sd->active[0] || (sd->slots > 1 && i == sd->active[1]); }

/* Dégâts que ferait l'attaque (sans coup critique), pour un tirage aléatoire roll de 85 à 100 ;
 * *effect reçoit l'efficacité ×16 (0, 4, 8, 16, 32, 64). Sert à l'IA et aux tests. La cible : celle
 * d'en face (battle_estimate), ou la position target. */
uint16_t battle_estimate(const Battle *b, int attacker, uint16_t move, uint8_t roll, uint8_t *effect);
uint16_t battle_estimate_at(const Battle *b, int attacker, int target, uint16_t move, uint8_t roll, uint8_t *effect);
/* Efficacité ×16 du type contre ce Pokémon (types de l'espèce, sans talent). */
uint8_t battle_effectiveness(uint8_t type, const Fighter *f);

/* La forme Méga que l'espèce prend avec cet objet, ou -1. */
int mega_form(uint16_t species, uint8_t item);
/* La forme que l'espèce prend en entrant au combat avec cet objet (Orbe Platiné, Épée Rouillée,
 * Plaques…), ou -1. */
int item_form(uint16_t species, uint8_t item);
/* Niveaux de Résonance d'une équipe (0..3 pour chaque type), nom et effet de chaque Résonance. */
void resonance_levels(const Team *t, uint8_t out[NUM_TYPES]);
extern const char *const resonance_names_fr[NUM_TYPES], *const resonance_names_en[NUM_TYPES];
extern const char *const resonance_descs_fr[NUM_TYPES], *const resonance_descs_en[NUM_TYPES];
#define resonance_names L(resonance_names_fr, resonance_names_en)
#define resonance_descs L(resonance_descs_fr, resonance_descs_en)

#endif
