/* Description courte d'une attaque pour le menu des attaques : son effet (avec sa probabilité),
 * sinon sa priorité ou son taux de critique, sinon « Aucun effet ». Une ligne de 27 caractères au plus. */
#ifndef MOVETEXT_H
#define MOVETEXT_H
#include <stdint.h>

#define MOVETEXT_BYTES 48
void move_description(uint16_t move, char out[MOVETEXT_BYTES]);

#endif
