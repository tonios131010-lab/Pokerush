/* Éléments d'interface communs aux écrans : badges de type et de statut, barre de PV, sprites de
 * Pokémon (palette chromatique comprise), fenêtres et fonds. Tout se dessine dans le rectangle en
 * cours de gfx_render (display.h). */
#ifndef WIDGETS_H
#define WIDGETS_H
#include <stdbool.h>
#include <stdint.h>
#include "assets.h"
#include "game/pokemon.h"
#include "gfx/display.h"

/* Couleurs du texte, relevées dans Émeraude. */
#define INK_DARK RGB565(72, 72, 72) /* fenêtres claires */
#define INK_DARK_SHADOW RGB565(208, 208, 200)
#define INK_LIGHT RGB565(248, 248, 248) /* boîte de dialogue, bandeaux */
#define INK_LIGHT_SHADOW RGB565(96, 96, 112)
#define INK_BLUE RGB565(48, 80, 200)
#define INK_RED RGB565(216, 48, 32)
#define INK_GREEN RGB565(32, 152, 64)

#define BADGE_H 10
/* Badge de type (couleur du type, nom court) ; renvoie sa largeur. */
int ui_type_badge(int x, int y, uint8_t type);
int ui_type_badge_width(uint8_t type);
/* Badge de type compact, trois lettres (cadres du duo) ; renvoie sa largeur. */
int ui_type_mini(int x, int y, uint8_t type);
/* Pastille de 5×7 de la couleur du type (listes d'attaques). */
void ui_type_dot(int x, int y, uint8_t type);
uint16_t ui_type_color(uint8_t type);
/* Les deux types d'une espèce côte à côte ; renvoie la largeur totale. */
int ui_types(int x, int y, uint16_t species);
/* Badge de statut (SOM, PSN, BRÛ, GEL, PAR, TOX), rien pour STATUS_NONE ; renvoie sa largeur. */
int ui_status_badge(int x, int y, uint8_t status);
/* Pastille d'efficacité (« x2 » en vert, « x0,5 » en orange…) pour une efficacité ×16 (0..64). */
int ui_eff_pill(int x, int y, uint8_t eff);
/* Barre de PV d'Émeraude (étiquette « PV » et barre de 48 px), 66×6. */
void ui_hpbar(int x, int y, uint16_t hp, uint16_t max);
/* Sprite de face ou de dos dans son cadre de 64×64 dont le coin haut gauche est (x, y). */
void ui_pokemon(uint16_t species, bool shiny, bool back, int x, int y);
/* La face du Pokémon, 2e image de son animation (la 1re s'il n'en a pas). */
void ui_pokemon_frame2(uint16_t species, bool shiny, int x, int y);
/* Icône des menus dans son cadre de 32×32. */
void ui_icon(uint16_t species, int x, int y);
/* Personnalisation (succès) : thème des menus (0..7) et boîte de dialogue claire. */
extern uint8_t ui_theme, ui_bubble;
#define THEME_COUNT 9
extern const char *const theme_names_fr[THEME_COUNT], *const theme_names_en[THEME_COUNT];
#define theme_names L(theme_names_fr, theme_names_en)
/* Boîte de dialogue (classique ou claire, selon ui_bubble) et les couleurs de son texte. */
void ui_dialog(int x, int y, int w, int h, uint16_t *ink, uint16_t *shadow);
/* Fenêtre standard d'Émeraude. */
void ui_window(int x, int y, int w, int h);
/* Fond des menus : rayures diagonales douces, teinte selon `hue` (0..3). */
void ui_background(int hue);
/* Bandeau de titre en haut de l'écran. */
void ui_title(const char *title);
/* Barre d'aide en bas de l'écran (« ←→ choisir · OK valider »), FOOTER_H de haut. */
#define FOOTER_H 11
void ui_footer(const char *help);
void ui_ball(int x, int y);
/* Pastille de couleur avec un texte blanc ; renvoie sa largeur. */
int ui_pill(int x, int y, const char *label, uint16_t color);
/* La couleur éclaircie aux deux tiers vers le blanc (fonds de cartes). */
uint16_t ui_pale(uint16_t c);
/* Cadre d'un pixel. */
void ui_frame(int x, int y, int w, int h, uint16_t c);
/* Texte coupé en lignes de `width` px au plus (aux espaces) ; renvoie le nombre de lignes. */
int ui_wrap(const Font *f, int x, int y, int width, const char *s, uint16_t fg, uint16_t shadow);
/* Pareil avec un interligne et un nombre de lignes au plus (la dernière finit par « … » si besoin). */
int ui_wrap_lines(const Font *f, int x, int y, int width, int line, int max, const char *s, uint16_t fg, uint16_t shadow);
/* Le nombre de lignes qu'occuperait le texte, sans le dessiner. */
int ui_wrap_count(const Font *f, int width, const char *s);
/* Petite étoile des Pokémon chromatiques (7×7). */
void ui_shiny_star(int x, int y);
/* Jauge horizontale (stats) : value sur max, w px. */
void ui_gauge(int x, int y, int w, int value, int max, uint16_t color);

#endif
