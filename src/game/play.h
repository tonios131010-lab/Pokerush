/* La partie complète : menu de départ (continuer, nouvelle partie, carnet), puis combats et drafts. */
#ifndef PLAY_H
#define PLAY_H
#include <stdint.h>
#include "game/pokemon.h"

void play_run(uint32_t seed);

extern RunStats run_stats; /* journal de la partie en cours */

#endif
