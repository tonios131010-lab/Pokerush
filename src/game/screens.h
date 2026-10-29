/* Les écrans hors combat : titre, choix du starter (les 27 starters des 9 générations), draft,
 * choix d'un membre de l'équipe, d'une attaque, d'un objet, et la fiche d'un Pokémon. Chaque
 * fonction renvoie le choix du joueur, ou -1 s'il revient en arrière (*quit : touche Maison). */
#ifndef SCREENS_H
#define SCREENS_H
#include <stdbool.h>
#include <stdint.h>
#include "game/battle.h"
#include "game/trainer.h"

/* 0 : continuer, 1 : nouvelle partie, 2 : Carnet, 3 : succès, 4 : paramètres. */
int screen_title(const Run *saved, bool *quit);
/* Indice dans starter_species. */
int screen_starter(bool *quit);
/* Le Carnet : les espèces vues, obtenues, chromatiques (core/dex.h). */
void screen_dex(bool *quit);
/* Après une victoire, la récompense : 0 RECRUTER (une des offres), 1 ENTRAÎNER (un membre), 2 NE RIEN
 * PRENDRE (le bouton sous les cartes), MENU_ALT (shift : le menu), -1 (Maison). cursor : le choix sous le
 * curseur, gardé d'un appel à l'autre. */
int screen_reward(const Pokemon *offers, int n, const Team *team, const Trainer *next, uint16_t won, int *cursor, bool *quit);
/* Indice de l'offre choisie, MENU_BACK (retour au choix de la récompense) ou MENU_ALT (le menu) ;
 * cursor : celle sous le curseur au départ, mise à jour. next : le prochain adversaire, annoncé dans le
 * bandeau (ou NULL). */
int screen_draft(const Pokemon *offers, int n, const Team *team, const Trainer *next, int *cursor, bool *quit);
int screen_team(const char *title, const Team *team, bool *quit);
/* Combat en duo : l'équipe adverse (6 Pokémon) face à la vôtre, dont on choisit les 4 qui combattent
 * (tous s'il y en a 4 ou moins), dans l'ordre : les deux premiers ouvrent. order reçoit leurs indices ;
 * renvoie leur nombre (0 : Maison). */
int screen_preview(const Team *mine, const Trainer *foe, uint8_t order[4], bool *quit);
/* Une attaque parmi moves[0..n-1] (p : le Pokémon concerné, pour l'efficacité et le titre). */
int screen_moves(const char *title, const uint16_t *moves, int n, bool *quit);
int screen_items(const char *title, const uint8_t *items, int n, bool *quit);
/* Fiche d'un Pokémon en combat (stats modifiées, statut, PP) ; Gauche / Droite : l'autre camp. */
void screen_summary(Battle *b, bool *quit);
/* Ce que fait un objet. */
const char *item_description(uint8_t item);

#endif
