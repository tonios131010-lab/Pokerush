/* Le Carnet : les espèces vues, obtenues et obtenues chromatiques, toutes parties confondues, le
 * meilleur combat atteint, les succès et les paramètres. Rangé dans le script carnet.py (Base64, comme pokemon.py), de taille fixe.
 * Récompense : un objet rare dans le sac au départ d'une partie par tranche de 50 espèces obtenues. */
#ifndef DEX_H
#define DEX_H
#include <stdbool.h>
#include <stdint.h>

#define DEX_FILE "carnet.py"
#define DEX_SPECIES 1024 /* 1 << SPECIES_BITS */

typedef struct {
  uint8_t seen[DEX_SPECIES / 8], owned[DEX_SPECIES / 8], shiny[DEX_SPECIES / 8];
  uint16_t best;  /* meilleur combat atteint */
  uint16_t runs;  /* parties commencées */
  uint16_t champ; /* Maîtres battus */
  uint32_t achievements; /* succès obtenus (bits, game/meta.h) */
  uint8_t anim;          /* animations d'attaque et de K.O. */
  uint8_t fast;          /* vitesse ×2 */
  uint8_t choice;        /* mode Choix : changer de Pokémon quand l'adversaire en envoie un autre */
  uint8_t theme, avatar, bubble; /* personnalisation (débloquée par les succès) */
  uint8_t tuto;          /* tutoriel déjà vu */
  uint8_t singles;       /* combats en simple plutôt qu'en duo (format VGC) */
  uint8_t lang;          /* langue des textes : 0 français, 1 anglais */
  uint8_t reserved[31]; /* pour les versions suivantes : la taille du fichier ne change pas */
} Dex;

extern Dex dex;
void dex_load(void);
bool dex_save(void);
void dex_see(uint16_t species);
void dex_own(uint16_t species, bool shiny);
static inline bool dex_has(const uint8_t *bits, uint16_t s) { return bits[s >> 3] >> (s & 7) & 1; }
int dex_count(const uint8_t *bits);

#endif
