/* Pokémon : les espèces (avec leurs formes Méga) et les attaques des 9 générations, en tables const
 * en flash générées par tools/gen_data.py depuis data/ et pokeemerald-expansion ; le Pokémon du
 * joueur et son équipe (en RAM, sauvegardés).
 *
 * Règles : stats, types (avec le type Fée) et attaques de la 9G, catégorie physique / spéciale propre
 * à chaque attaque. Sans niveaux : tout se calcule comme au niveau 50, sans IV ni EV.
 *   PV    = base + 60
 *   autre = (base + 5), ×1,1 ou ×0,9 selon la nature
 *
 * Bit-packing : chaque groupe de champs remplit exactement son conteneur ; `packed` supprime le
 * bourrage entre les groupes, et les _Static_assert figent les tailles. Ne jamais prendre l'adresse
 * d'un champ (non aligné) : passer par les accesseurs. */
#ifndef POKEMON_H
#define POKEMON_H
#include <stdbool.h>
#include <stdint.h>

#ifndef PACKED
#define PACKED __attribute__((packed))
#endif

/* Largeurs des identifiants : elles fixent la taille des structures et le format de la sauvegarde. */
#define SPECIES_BITS 10 /* 1024 entrées au plus : les espèces, puis leurs formes Méga */
#define MOVE_BITS 9    /* 512 attaques au plus ; 0 = emplacement vide */
#define ITEM_BITS 8    /* 256 objets au plus (méga-gemmes comprises) ; 0 = aucun */
#define ABILITY_BITS 9 /* 512 talents au plus ; 0 = aucun */

#define TEAM_MAX 6
#define MOVES_MAX 4
#define BAG_MAX 8

/* Langue des textes : 0 français, 1 anglais (PARAMÈTRES). Chaque texte existe dans les deux langues,
 * côte à côte dans le code : L("français", "English") choisit au moment de l'afficher. Les noms et
 * descriptions générés (espèces, attaques, talents, objets…) suivent par les macros plus bas. */
extern uint8_t lang;
#define L(fr, en) (lang ? (en) : (fr))

/* ------------------------------------------------------------------------- */
/* Types : l'ordre interne d'Émeraude, puis Fée. */

enum {
  TYPE_NORMAL,
  TYPE_FIGHTING,
  TYPE_FLYING,
  TYPE_POISON,
  TYPE_GROUND,
  TYPE_ROCK,
  TYPE_BUG,
  TYPE_GHOST,
  TYPE_STEEL,
  TYPE_MYSTERY, /* ??? : sans faiblesse ni résistance (Lutte, Malédiction) */
  TYPE_FIRE,
  TYPE_WATER,
  TYPE_GRASS,
  TYPE_ELECTRIC,
  TYPE_PSYCHIC,
  TYPE_ICE,
  TYPE_DRAGON,
  TYPE_DARK,
  TYPE_FAIRY,
  NUM_TYPES
};

/* Efficacité, 2 bits par couple (attaque, défense) : table de la 6G et suivantes. */
enum { EFF_IMMUNE, EFF_HALF, EFF_NORMAL, EFF_SUPER };
extern const uint8_t type_chart[(NUM_TYPES * NUM_TYPES + 3) / 4];

static inline uint8_t type_effect(uint8_t atk_type, uint8_t def_type) {
  uint16_t i = (uint16_t)(atk_type * NUM_TYPES + def_type);
  return (uint8_t)((type_chart[i >> 2] >> ((i & 3) * 2)) & 3);
}

/* ------------------------------------------------------------------------- */
/* Statistiques et natures : l'ordre d'Émeraude (Vitesse avant les spéciales). */

enum { STAT_HP, STAT_ATK, STAT_DEF, STAT_SPE, STAT_SPA, STAT_SPD, NUM_STATS };

/* Natures dans l'ordre d'Émeraude : la nature n augmente la stat n / 5 + 1 et baisse la stat
 * n % 5 + 1 ; neutre si les deux coïncident. */
#define NUM_NATURES 25

/* ------------------------------------------------------------------------- */
/* Espèce : 20 octets. Les formes Méga suivent les 150 espèces ; elles partagent les attaques de
 * leur espèce de base et s'obtiennent en combat avec leur méga-gemme. */

typedef struct PACKED {
  uint8_t base[NUM_STATS]; /* stats de base, ordre STAT_* */

  uint32_t dex : 11;      /* n° national (1..1025) */
  uint32_t type1 : 5;     /* TYPE_* */
  uint32_t type2 : 5;     /* = type1 pour un mono-type */
  uint32_t tier : 3;      /* puissance 0..7 : pondère le draft et les équipes des dresseurs */
  uint32_t legendary : 1;
  uint32_t is_mega : 1;
  uint32_t icon_pal : 3;  /* palette de l'icône (0..5) */
  uint32_t is_form : 1;   /* forme à objet (Giratina Originel…) : prise en entrant au combat */
  uint32_t : 2;

  uint32_t ability0 : 9;  /* talent principal */
  uint32_t ability1 : 9;  /* second talent, 0 s'il n'y en a qu'un */
  uint32_t stone : 8;     /* forme Méga ou à objet : l'objet qui la déclenche */
  uint32_t : 6;

  uint32_t learn_ofs : 17; /* début de son movepool dans learnset_bits */
  uint32_t learn_cnt : 7;  /* taille de son movepool */
  uint32_t : 8;

  uint16_t mega_base; /* forme Méga ou à objet : l'espèce de base */
} SpeciesInfo;
_Static_assert(sizeof(SpeciesInfo) == 20, "SpeciesInfo : 20 octets");

/* Tables générées. L'indice d'espèce est aussi l'indice de ses sprites. */
extern const SpeciesInfo species_info[];
/* Noms en majuscules et en UTF-8 : un caractère accentué prend 2 octets. */
#define NAME_BYTES 16
extern const char species_names_fr[][NAME_BYTES], species_names_en[][NAME_BYTES]; /* 12 caractères au plus */
#define species_names L(species_names_fr, species_names_en)
/* Les attaques que chaque espèce peut apprendre, sur 9 bits chacune (MOVE_BITS), à la suite. */
extern const uint8_t learnset_bits[];

static inline uint16_t species_learnset(const SpeciesInfo *s, uint8_t i) {
  uint32_t b = (uint32_t)(s->learn_ofs + i) * MOVE_BITS;
  uint32_t w = learnset_bits[b >> 3] | (uint32_t)learnset_bits[(b >> 3) + 1] << 8;
  return (uint16_t)((w >> (b & 7)) & ((1u << MOVE_BITS) - 1));
}

/* ------------------------------------------------------------------------- */
/* Attaque : 7 octets. Puissance 0 = attaque de statut, 1 = dégâts calculés par l'effet. */

typedef struct PACKED {
  uint32_t power : 8;
  uint32_t accuracy : 7; /* 0..100 ; 0 = ne rate jamais */
  uint32_t type : 5;     /* TYPE_* */
  uint32_t pp : 6;
  uint32_t priority : 4; /* priorité + 8 (de -8 à +7) : lire avec move_priority() */
  uint32_t contact : 1;
  uint32_t special : 1;  /* attaque offensive spéciale (Atq. Spé. contre Déf. Spé.), sinon physique */

  uint16_t effect : 7;        /* EFFECT_* (data.h) */
  uint16_t effect_chance : 7; /* 0..100 ; 0 = toujours */
  uint16_t sound : 1;
  uint16_t high_crit : 1;

  uint8_t punch : 1; /* Poing de Fer */
  uint8_t bite : 1;  /* Prognathe */
  uint8_t slice : 1; /* Tranchant */
  uint8_t pulse : 1; /* Méga Blaster */
  uint8_t spread : 2; /* duo : 0 une cible, 1 les deux adversaires, 2 tous les autres Pokémon */
  uint8_t : 2;
} MoveInfo;
_Static_assert(sizeof(MoveInfo) == 7, "MoveInfo : 7 octets");

extern const MoveInfo move_info[];
#define LABEL_BYTES 20
extern const char move_names_fr[][LABEL_BYTES], move_names_en[][LABEL_BYTES]; /* 15 (16 en anglais) au plus */
#define move_names L(move_names_fr, move_names_en)

static inline int8_t move_priority(const MoveInfo *m) { return (int8_t)(m->priority - 8); }
static inline bool move_is_status(const MoveInfo *m) { return m->power == 0; }
static inline bool move_is_special(const MoveInfo *m) { return m->power && m->special; }
static inline bool move_is_physical(const MoveInfo *m) { return m->power && !m->special; }

/* ------------------------------------------------------------------------- */
/* Objet tenu : 2 octets. Les méga-gemmes ont l'effet ITEM_EFFECT_MEGA_STONE ; l'espèce qu'elles
 * transforment est dans SpeciesInfo.stone de la forme Méga. */

typedef struct PACKED {
  uint16_t effect : 6; /* ITEM_EFFECT_* (data.h) */
  uint16_t type : 5;   /* type renforcé, pour ITEM_EFFECT_TYPE_BOOST */
  uint16_t rarity : 2; /* 0 commun .. 3 rare : chances de butin */
  uint16_t : 3;
} ItemInfo;
_Static_assert(sizeof(ItemInfo) == 2, "ItemInfo : 2 octets");

extern const ItemInfo item_info[];
extern const char item_names_fr[][LABEL_BYTES], item_names_en[][LABEL_BYTES];
extern const char *const item_descs_fr[], *const item_descs_en[]; /* ce que fait l'objet, 36 caractères au plus */
extern const char ability_names_fr[][LABEL_BYTES], ability_names_en[][LABEL_BYTES];
extern const char *const ability_descs_fr[], *const ability_descs_en[]; /* ce que fait le talent, 40 au plus */
extern const char nature_names_fr[NUM_NATURES][LABEL_BYTES], nature_names_en[NUM_NATURES][LABEL_BYTES];
extern const char type_names_fr[NUM_TYPES][LABEL_BYTES], type_names_en[NUM_TYPES][LABEL_BYTES];
#define item_names L(item_names_fr, item_names_en)
#define item_descs L(item_descs_fr, item_descs_en)
#define ability_names L(ability_names_fr, ability_names_en)
#define ability_descs L(ability_descs_fr, ability_descs_en)
#define nature_names L(nature_names_fr, nature_names_en)
#define type_names L(type_names_fr, type_names_en)

/* ------------------------------------------------------------------------- */
/* Pokémon du joueur ou d'un dresseur : 8 octets. Pas de PV ni de PP ici : ils vivent dans l'état
 * du combat, et l'équipe est soignée entre deux combats. */

typedef struct PACKED {
  uint32_t species : 10; /* indice dans species_info (jamais une forme Méga) */
  uint32_t move0 : 9;
  uint32_t move1 : 9;
  uint32_t ability : 1; /* 0 = ability0, 1 = ability1 */
  uint32_t shiny : 1;
  uint32_t : 2;          /* réservé */

  uint32_t move2 : 9;
  uint32_t move3 : 9;
  uint32_t item : 8; /* objet tenu */
  uint32_t nature : 5;
  uint32_t : 1;
} Pokemon;
_Static_assert(sizeof(Pokemon) == 8, "Pokemon : 8 octets");

static inline uint16_t pokemon_move(const Pokemon *p, uint8_t slot) {
  switch (slot) {
    case 0: return p->move0;
    case 1: return p->move1;
    case 2: return p->move2;
    default: return p->move3;
  }
}

static inline void pokemon_set_move(Pokemon *p, uint8_t slot, uint16_t move) {
  switch (slot) {
    case 0: p->move0 = move; break;
    case 1: p->move1 = move; break;
    case 2: p->move2 = move; break;
    default: p->move3 = move; break;
  }
}

static inline const SpeciesInfo *pokemon_species(const Pokemon *p) { return &species_info[p->species]; }

static inline uint16_t species_ability(const SpeciesInfo *s, uint8_t slot) {
  return slot && s->ability1 ? s->ability1 : s->ability0;
}
static inline uint16_t pokemon_ability(const Pokemon *p) { return species_ability(pokemon_species(p), p->ability); }

/* Stat au niveau 50 sans IV ni EV, pour une espèce (forme Méga comprise) et une nature. */
static inline uint16_t species_stat(const SpeciesInfo *s, uint8_t nature, uint8_t stat) {
  uint16_t base = s->base[stat];
  if (stat == STAT_HP) return base + 60;
  uint16_t v = base + 5;
  uint8_t up = nature / 5 + 1, down = nature % 5 + 1;
  if (up != down) {
    if (stat == up) v = v * 11 / 10;
    else if (stat == down) v = v * 9 / 10;
  }
  return v;
}
static inline uint16_t pokemon_stat(const Pokemon *p, uint8_t stat) { return species_stat(pokemon_species(p), p->nature, stat); }

/* ------------------------------------------------------------------------- */
/* Équipe : 49 octets. Les Pokémon valides sont mon[0..count-1], le premier entre en combat. */

typedef struct PACKED {
  Pokemon mon[TEAM_MAX];
  uint8_t count : 3; /* 0..6 */
  uint8_t : 5;
} Team;
_Static_assert(sizeof(Team) == 49, "Team : 49 octets");

/* ------------------------------------------------------------------------- */
/* Partie en cours : ce que la sauvegarde écrit à chaque checkpoint. 63 octets. */

typedef struct PACKED {
  uint32_t seed; /* état du générateur aléatoire au moment du checkpoint */

  uint16_t battle : 10; /* combat en cours, 1..1023 */
  uint16_t flags : 6;   /* RUN_* */

  uint8_t bag[BAG_MAX]; /* objets ramassés pas encore donnés à un Pokémon (0 = vide) */
  Team team;
} Run;
_Static_assert(sizeof(Run) == 63, "Run : 63 octets");

/* Journal de la partie (sauvegardé avec elle) : ce qu'a fait l'équipe du joueur. */
typedef struct PACKED {
  uint32_t damage; /* PV infligés */
  uint16_t kos, crits, megas, shinies;
  uint32_t seed; /* graine de départ de la partie (à partager) */
} RunStats;
_Static_assert(sizeof(RunStats) == 16, "RunStats : 16 octets");

enum {
  RUN_CHAMPION_BEATEN = 1 << 0, /* le combat 100 est gagné : la suite est en mode infini */
  RUN_MEGA = 1 << 1,            /* Méga-Anneau obtenu (combat 10) : Méga-Évolution permise */
  RUN_NUZLOCKE = 1 << 2,        /* mode Nuzlocke : un Pokémon K.O. quitte l'équipe */
  RUN_DIFF_SHIFT = 3,           /* difficulté sur 2 bits : DIFF_* */
};
enum { DIFF_EASY, DIFF_NORMAL, DIFF_HARD };
static inline int run_difficulty(uint16_t flags) { return (flags >> RUN_DIFF_SHIFT) & 3; }

#endif
