/* Clavier : les touches qui viennent d'être enfoncées (fronts montants), une fois par image. */
#ifndef INPUT_H
#define INPUT_H
#include <stdbool.h>
#include <stdint.h>

enum {
  KEY_LEFT = 1 << 0,
  KEY_UP = 1 << 1,
  KEY_DOWN = 1 << 2,
  KEY_RIGHT = 1 << 3,
  KEY_OK = 1 << 4,   /* OK ou EXE */
  KEY_BACK = 1 << 5, /* Retour */
  KEY_HOME = 1 << 6, /* Maison : quitter */
  KEY_ALT = 1 << 7,  /* shift (ou alpha) : Méga-Évolution, menu du draft */
  KEY_EXE = 1 << 8,  /* EXE seul (il vaut aussi KEY_OK) : combat suivant */
};

/* Lit le clavier : renvoie les touches enfoncées depuis l'appel précédent. */
uint16_t input_poll(void);
/* Attend une image (~16 ms) puis lit le clavier. */
uint16_t input_wait_frame(void);
/* Le nom de la touche à afficher (« OK », « EXE », « ENTRÉE », « A »… selon la calculatrice, le
 * clavier ou la manette du PC). Dans un texte, {OK} {BACK} {ALT} {EXE} l'écrivent (display.c). */
const char *key_label(uint16_t key);

#endif
