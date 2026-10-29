/* IA des dresseurs. Chaque attaque reçoit une note selon la situation (dégâts estimés, K.O.,
 * statut utile ou non, météo, boosts…) et l'IA change de Pokémon quand le sien est menacé. La
 * finesse vient de b->foe_skill : 0 joue presque au hasard, 10 prend toujours la meilleure note. */
#ifndef AI_H
#define AI_H
#include "game/battle.h"

Action ai_choose(Battle *b, int side);
/* Le Pokémon à envoyer après un K.O. : celui qui s'en sort le mieux contre l'adversaire. */
uint8_t ai_replacement(const Battle *b, int side);

#endif
