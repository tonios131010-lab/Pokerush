/* Un combat à l'écran : les menus du joueur, l'IA en face, et les événements du moteur rejoués avec
 * leurs animations (texte machine à écrire, barres de PV, entrées, K.O.). */
#ifndef FIGHT_H
#define FIGHT_H
#include <stdint.h>
#include "game/battle.h"
#include "game/trainer.h"

/* Joue le combat n° number, déjà préparé par battle_start() avec l'équipe du dresseur t, jusqu'au
 * bout. Renvoie BATTLE_WON ou BATTLE_LOST, ou BATTLE_ONGOING si le joueur a quitté (Maison). */
uint8_t fight_run(Battle *b, const Trainer *t, uint16_t number);

/* Ce qui s'est passé pendant le dernier combat (pour les succès). */
enum { FEAT_MEGA_KO = 1 << 0 };
extern uint8_t fight_feats;

#endif
