/* Sauvegarde de la partie (Run : équipe, combat en cours, objets) dans le script Python pokemon.py,
 * qu'on voit et qu'on peut transférer depuis l'app Python de la calculatrice :
 *
 *   # PokeRush : sauvegarde automatique, ne pas modifier.
 *   SAVE_DATA = "UFIBAQAA…"
 *
 * La chaîne est le Base64 d'un bloc de 96 octets : en-tête, Run bit-packé, réserve, somme de
 * contrôle. Sa taille ne change jamais, si bien que le fichier est toujours réécrit à sa place. */
#ifndef SAVE_H
#define SAVE_H
#include <stdbool.h>
#include "game/pokemon.h"

#define SAVE_FILE "pokemon.py"

/* false si la sauvegarde n'a pas pu être écrite (mémoire pleine, fichier modifié à la main…). */
bool save_write(const Run *run, const RunStats *stats);
/* false s'il n'y a pas de sauvegarde, ou si elle est abîmée ou incohérente : *run est alors intact. */
bool save_read(Run *run, RunStats *stats); /* stats peut être NULL */

#endif
