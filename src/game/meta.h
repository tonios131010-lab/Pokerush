/* Autour des parties : succès (et la personnalisation qu'ils débloquent), paramètres, tutoriel,
 * saisie de la graine, bilan de fin de partie (Hall of Fame) et cinématique de victoire. Succès et
 * paramètres vivent dans le Carnet (core/dex.h). */
#ifndef META_H
#define META_H
#include <stdbool.h>
#include <stdint.h>
#include "game/pokemon.h"

enum {
  ACH_FIRST_WIN, ACH_MEGA_KO, ACH_MEGA, ACH_BOSS, ACH_B50, ACH_CHAMPION, ACH_SHINY, ACH_RAID,
  ACH_RESONANCE3, ACH_DEX100, ACH_NUZLOCKE, ACH_HARD, ACH_B150, ACH_CRITS, ACH_DAMAGE, ACH_FULL_TEAM, ACH_CLAUDE,
  ACH_COUNT
};

/* La sauvegarde (PARAMÈTRES) : où renvoie une défaite. La partie en cours est toujours gardée pour la
 * reprendre si l'on quitte ; JAMAIS : une défaite la termine. */
enum { CHECKPOINT_10, CHECKPOINT_EACH, CHECKPOINT_NEVER, CHECKPOINT_COUNT };

/* Débloque un succès (une seule fois) et annonce sa récompense. */
void achieve(int id, bool *quit);
void screen_achievements(bool *quit);
void screen_settings(bool *quit);
/* Applique les paramètres du Carnet (thème, boîte de dialogue). */
void settings_apply(void);
void tutorial(bool *quit);
/* Saisie d'une graine de 8 chiffres hexadécimaux ; false si Retour. */
bool seed_entry(uint32_t *seed, bool *quit);
/* Bilan de fin de partie : l'équipe au Hall of Fame, puis les statistiques. */
void hall_of_fame(const Run *run, const RunStats *stats, const char *title, bool *quit);
/* La cinématique de victoire après le Maître (combat 100). */
void ending(const Team *team, bool *quit);
/* Le sprite du personnage choisi (trainer_pic). */
uint8_t avatar_pic(void);

#endif
