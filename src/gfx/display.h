/* Moteur de rendu. Le jeu dessine dans un écran logique de 214×160 (la taille de l'écran de la GBA,
 * à 26 colonnes près) gardé en RAM (67 Ko), affiché à l'échelle 1,5 sur les 320×240 de la
 * calculatrice. Agrandir de 1,5 au plus proche voisin ferait des pixels de 1 et 2 px de large, qui
 * déforment le pixel art ; ici, chaque groupe de 2 pixels logiques devient 3 pixels de l'écran : le
 * premier, le mélange 50/50 des deux, le second (« sharp bilinear »). Les contours restent nets et
 * tout est 1,5 fois plus grand qu'au pixel près.
 *
 * gfx_render(x, y, w, h, draw, ctx) limite le dessin au rectangle, appelle draw(ctx), puis envoie le
 * rectangle à l'écran. La fonction de dessin peut tout redessiner : ce qui sort du rectangle est
 * écarté tout de suite. Toutes les coordonnées sont logiques. */
#ifndef DISPLAY_H
#define DISPLAY_H
#include <stdint.h>
#include "gfx/image.h"

#define SCREEN_W 214
#define SCREEN_H 160

#define RGB565(r, g, b) ((uint16_t)(((r) >> 3) << 11 | ((g) >> 2) << 5 | (b) >> 3))

typedef void (*draw_fn)(const void *ctx);

/* Redessine le rectangle (x, y, w, h) de l'écran logique et l'affiche. */
void gfx_render(int x, int y, int w, int h, draw_fn draw, const void *ctx);

void gfx_fill(int x, int y, int w, int h, uint16_t color);
/* Restreint le dessin au rectangle (à l'intérieur de celui en cours), puis rétablit l'ancien. */
void gfx_clip_push(int x, int y, int w, int h);
void gfx_clip_pop(void);
/* Assombrit le rectangle de moitié (ce qui est derrière une fenêtre). */
void gfx_dim(int x, int y, int w, int h);
/* Mélange le rectangle avec une couleur (flash blanc de la Méga-Évolution) : amount de 0 à 4 quarts. */
void gfx_tint(int x, int y, int w, int h, uint16_t color, int amount);
/* Image avec son coin haut gauche en (x, y), sans tenir compte de im->x, im->y. */
void gfx_image(const Image *im, int x, int y);
/* Pareil avec une autre palette de 16 couleurs (Pokémon chromatiques). */
void gfx_image_pal(const Image *im, int x, int y, const uint16_t *pal);
/* La 2e image d'une animation (assets.h pokemon_front2), sur le cadre de la 1re ; w = 0 : la 1re. */
void gfx_image_frame2(const Image *first, const Image *second, int x, int y, const uint16_t *pal);
/* Image dessinée d'une seule couleur (silhouette), indice 0 transparent. */
void gfx_image_flat(const Image *im, int x, int y, uint16_t color);
/* Les lignes [sy, sy + h) de l'image, posées à partir de (x, y). */
void gfx_image_rows(const Image *im, int sy, int h, int x, int y);
/* L'image répétée pour couvrir le rectangle (bords des boîtes extensibles). */
void gfx_image_tiled(const Image *im, int x, int y, int w, int h);
void gfx_nine(const Nine *n, int x, int y, int w, int h);
/* Fond de combat dont la ligne 0 est en y ; il couvre toute la largeur, au-dessus de y en répétant
 * les rayures du haut. Les plateformes sont décalées de (foe_dx, foe_dy) et de player_dx. */
void gfx_backdrop(const Backdrop *bd, int y, int foe_dx, int foe_dy, int player_dx);

/* Texte UTF-8 ; '\n' passe à la ligne. Ne dessine que les n premiers caractères (n < 0 : tous),
 * pour l'effet machine à écrire. Renvoie le x après le dernier caractère. */
int gfx_text_n(const Font *f, int x, int y, const char *s, int n, uint16_t fg, uint16_t shadow);
static inline int gfx_text(const Font *f, int x, int y, const char *s, uint16_t fg, uint16_t shadow) {
  return gfx_text_n(f, x, y, s, -1, fg, shadow);
}
/* Texte agrandi deux fois (titres), sur une seule ligne. */
int gfx_text_big(const Font *f, int x, int y, const char *s, uint16_t fg, uint16_t shadow);
int text_width(const Font *f, const char *s); /* largeur de la plus longue ligne */
int text_length(const char *s);               /* nombre de caractères (pas d'octets) */
/* Écrit un nombre aligné à droite sur `right` ; renvoie le x de son premier chiffre. */
int gfx_number(const Font *f, int right, int y, unsigned v, uint16_t fg, uint16_t shadow);

#endif
