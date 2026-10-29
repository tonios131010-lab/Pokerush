/* Écran de combat à la manière d'Émeraude, sur l'écran logique de 214×160 (display.h) :
 *   y 0..111    la scène : fond, plateformes, Pokémon (adverse en haut à droite, le joueur en bas à
 *               gauche, de dos), cadres de PV avec nom, types, statut et barre de PV ;
 *   y 112..159  la boîte de dialogue, avec le menu d'action à droite quand le joueur choisit.
 * Dans le menu des attaques, une fenêtre à gauche détaille l'attaque sous le curseur (type,
 * catégorie, PP, puissance, précision, efficacité contre l'adversaire, effet, Méga-Évolution).
 *
 * Ce module ne fait qu'afficher une BattleView : fight.c la remplit en rejouant les événements. */
#ifndef UI_H
#define UI_H
#include <stdbool.h>
#include <stdint.h>

#define SCENE_H 112
#define DIALOG_Y SCENE_H
#define DIALOG_H 48

typedef struct {
  uint16_t species; /* indice d'espèce (sprites), forme Méga comprise */
  bool shiny;
  const char *name;
  uint16_t hp, max_hp;
  uint8_t status; /* STATUS_* */
} BattleSide;

/* Le menu d'action, dans l'ordre de lecture de la grille 2×2. */
enum { MENU_FIGHT, MENU_POKEMON, MENU_INFO, MENU_RUN };

/* Une attaque du menu ATTAQUE. */
enum { CATEGORY_PHYSICAL, CATEGORY_SPECIAL, CATEGORY_STATUS };
typedef struct {
  const char *name; /* NULL : emplacement vide */
  const char *desc; /* son effet */
  uint8_t pp, max_pp, type, category;
  uint8_t power, accuracy; /* puissance (0 : aucune, 1 : variable), précision (0 : ne rate jamais) */
  uint8_t eff;             /* efficacité ×16 contre l'adversaire (0..64) ; 255 : sans objet */
} MoveSlot;

/* Positions du combat (game/battle.h) : 0 joueur, 1 adversaire, 2 et 3 leurs partenaires en duo. */
#define VIEW_POS 4

typedef struct {
  uint8_t backdrop; /* BACKDROP_* */
  bool doubles;     /* duo : quatre Pokémon, cadres de PV compacts */
  BattleSide mon[VIEW_POS];
  const char *text; /* message de la boîte de dialogue (2 lignes) */
  int16_t shown;    /* caractères visibles du message ; < 0 : tous */
  bool menu;        /* menu d'action affiché à droite */
  bool moves_menu;  /* à la place : le menu des attaques */
  uint8_t cursor;   /* MENU_*, ou emplacement d'attaque dans le menu des attaques */
  MoveSlot moves[4];
  bool mega_ready, mega_on;     /* Méga-Évolution possible ce tour ; choisie */
  uint8_t foe_count, foe_alive; /* Pokémon de l'adversaire : en tout, et encore en forme */
  bool show_trainer;            /* le dresseur adverse à la place de son Pokémon (début et fin du combat) */
  uint8_t trainer;              /* son sprite (trainer_pic dans assets.h) */
  int8_t choosing;              /* duo : la position qui choisit son action (cadre en couleur), ou -1 */
  int8_t aim;                   /* duo : la cible sous le curseur, ou -1 */
  uint8_t tag[4];               /* duo : efficacité ×16 de l'attaque choisie sur chaque position (255 : rien) */
  uint8_t sparkle[VIEW_POS];    /* étincelles d'un Pokémon chromatique qui entre (images restantes) */
  uint8_t flash;                /* flash blanc de la Méga-Évolution (0..4) */
  uint8_t terrain;              /* TERRAIN_* : teinte le terrain */
  uint16_t number;              /* numéro du combat (compteur en haut à droite) */
  int8_t dx[VIEW_POS], dy[VIEW_POS]; /* décalage des sprites (élan d'une attaque) */
  bool hide[VIEW_POS];               /* sprite caché (clignote quand il est touché) */
  uint8_t cut[VIEW_POS];             /* K.O. : le sprite s'enfonce de cut pixels */
  bool silhouette[VIEW_POS];         /* Méga-Évolution : sprite tout blanc */
  uint8_t ring[VIEW_POS];            /* Méga-Évolution : rayon des anneaux (0 : aucun) */
  bool frame2[VIEW_POS];             /* animation de face : 2e image */
  uint8_t fx_kind, fx_side, fx_to, fx_t; /* effet d'attaque : FX_*, lanceur, cible, avancement 0..16 */
  uint16_t fx_color;
} BattleView;

enum { FX_NONE, FX_ORB, FX_IMPACT, FX_SPARKS };
void ui_draw_battle(const BattleView *v); /* tout l'écran */
void ui_draw_scene(const BattleView *v);  /* la scène seule (animations) */
void ui_draw_hpbox(const BattleView *v, int pos);
void ui_draw_dialog(const BattleView *v); /* boîte de dialogue et fenêtre du menu des attaques */
void ui_draw_text(const BattleView *v);   /* la boîte de dialogue seule (texte qui s'écrit) */

#endif
