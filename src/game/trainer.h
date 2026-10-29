/* Les dresseurs : un nom, un sprite entier d'Émeraude, un style de jeu (archétype) et une
 * équipe construite pour ce style, de plus en plus forte et soignée à mesure que la partie avance.
 * Quelques combats sont fixes, comme dans Émeraude :
 *   1        FLORA, avec le starter que celui du joueur bat ;
 *   10 à 80  les 8 Champions d'arène de Hoenn (un type chacun) ;
 *   96 à 99  le Conseil 4 ;
 *   100      le Maître, avec six légendaires ;
 *   110, 120… les Champions puis le Conseil 4 de Kanto, et Blue au 230 ;
 *   35, 55…  un légendaire sauvage.
 * Les Champions méga-évoluent à partir du combat 20, les dresseurs ordinaires parfois après le 40. */
#ifndef TRAINER_H
#define TRAINER_H
#include <stdbool.h>
#include <stdint.h>
#include "game/pokemon.h"

enum {
  ARCH_BALANCED, ARCH_RAIN, ARCH_SUN, ARCH_SAND, ARCH_SNOW, ARCH_BATON, ARCH_STALL,
  ARCH_PHYSICAL, ARCH_SPECIAL, ARCH_CHOICE, ARCH_MONOTYPE, ARCH_TRICK_ROOM, ARCH_HAZARDS, NUM_ARCHETYPES
};

#define STARTER_COUNT 27
/* Les starters finaux des 9 générations, par trois : Plante, Feu, Eau. */
extern const uint16_t starter_species[STARTER_COUNT];

typedef struct {
  char name[32];      /* « MONTAGNARD LÉO », « ROXANNE »… */
  uint8_t pic;        /* son sprite : indice dans trainer_pic */
  uint8_t archetype;  /* ARCH_* */
  uint8_t type;       /* type favori (annoncé avant le draft), celui de l'équipe pour ARCH_MONOTYPE */
  uint8_t skill;      /* 0..10 : qualité de l'équipe et de l'IA */
  uint8_t backdrop;   /* BACKDROP_* */
  uint8_t loot_bonus; /* Champions, Conseil 4, Maître : un objet à coup sûr */
  bool mega;          /* peut méga-évoluer (un de ses Pokémon tient sa méga-gemme) */
  bool wild;          /* raid : un Pokémon légendaire sauvage géant, sans dresseur */
  bool boss;          /* Boss (tous les 10 combats, Conseil 4) : équipe et stratégie fixes */
  Team team;
} Trainer;

/* Le dresseur du combat n° battle (1, 2, …). player : l'équipe du joueur (pour le combat 1). */
void trainer_make(Trainer *t, uint16_t battle, const Team *player, uint32_t *rng);
/* L'objet lâché par un dresseur vaincu, ou ITEM_NONE ; mega : le joueur a le Méga-Anneau (les
 * méga-gemmes lâchées vont alors à un Pokémon de son équipe qui peut s'en servir). */
uint8_t trainer_loot(const Trainer *t, uint16_t battle, const Team *player, bool mega, uint32_t *rng);
/* Un Pokémon de l'espèce donnée, avec attaques, nature et objet choisis selon l'archétype et le
 * soin (skill) : sert aussi au draft. */
Pokemon trainer_build(uint16_t species, uint8_t archetype, uint8_t skill, bool item, uint32_t *rng);
/* Difficulté (DIFF_*) : taille des équipes, finesse de l'IA, force des Pokémon. */
extern int trainer_difficulty;
/* La méga-gemme de l'espèce (la première s'il y en a deux), ou ITEM_NONE. */
uint8_t species_stone(uint16_t species);
/* Combat en duo : les n Pokémon (au plus) que le dresseur amène contre cette équipe, les deux qui
 * ouvrent le combat en premier. */
void trainer_bring(const Trainer *t, const Team *opponent, int n, Team *out);
/* Un objet qui donne une forme à l'espèce (au hasard s'il y en a plusieurs, le premier si rng est
 * NULL), ou ITEM_NONE. */
uint8_t species_form_item(uint16_t species, uint32_t *rng);

#endif
