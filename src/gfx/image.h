/* Format des images en flash : le contrat entre tools/convert_assets.py, qui les produit dans
 * build/gen/assets.c, et le moteur de rendu, qui les lit.
 *
 * Pixels indexés, une ligne après l'autre, chaque ligne commençant sur un octet :
 * - 4 bpp : 16 couleurs, deux pixels par octet, celui de gauche dans les 4 bits bas (comme la GBA) ;
 * - 8 bpp : 256 couleurs, pour les rares images qui en ont plus de 15 (fonds de combat).
 * Les palettes sont en RGB565 et rangées par blocs de 16 couleurs ; une palette 8 bpp occupe 16 blocs.
 * Avec `key`, l'indice 0 est la couleur de transparence : il n'est jamais dessiné.
 *
 * Pokémon : la face, le dos et la palette viennent de pokeemerald-expansion. Le bloc de palette
 * im->pal est la palette normale, le bloc suivant la palette chromatique (shiny) : mêmes pixels. */
#ifndef IMAGE_H
#define IMAGE_H
#include <stdint.h>

#ifndef PACKED
#define PACKED __attribute__((packed))
#endif

#define POKEMON_FRAME 64 /* sprites de face et de dos : cadre de 64×64 */
#define ICON_FRAME 32    /* icônes des menus : cadre de 32×32 */

typedef struct PACKED {
  uint32_t ofs : 22; /* premier octet dans image_pixels */
  uint32_t w : 9;
  uint32_t key : 1;  /* 1 = l'indice 0 est transparent */

  uint32_t pal : 11; /* premier bloc de 16 couleurs dans image_palettes */
  uint32_t h : 8;
  uint32_t x : 9;    /* interface : position à l'écran ; sprite : position dans son cadre */
  uint32_t bpp8 : 1; /* 1 = 8 bpp, 0 = 4 bpp */
  uint32_t z : 1;    /* pixels compressés en deflate (sprites de 64×64 au plus), décodés au dessin */
  uint32_t : 2;

  uint8_t y;
} Image;
_Static_assert(sizeof(Image) == 9, "Image : 9 octets");

extern const uint8_t image_pixels[];
extern const uint16_t image_palettes[];

static inline const uint8_t *image_data(const Image *im) { return &image_pixels[im->ofs]; }
static inline const uint16_t *image_palette(const Image *im) { return &image_palettes[im->pal * 16]; }
static inline uint16_t image_stride(const Image *im) { return im->bpp8 ? im->w : (uint16_t)((im->w + 1) >> 1); }

/* Boîte extensible (9-slice) : 4 coins de c×c, 4 bords d'1 px d'épaisseur répétés le long des
 * côtés, et l'intérieur d'une couleur unie. Se dessine à n'importe quelle taille >= 2c. */
typedef struct {
  Image corner[4]; /* haut gauche, haut droit, bas gauche, bas droit */
  Image edge[4];   /* haut et bas (1 × c), gauche et droite (c × 1) */
  uint16_t fill;
  uint8_t c;
} Nine;

/* Fond de combat de Ruby / Saphir (240×112) : chaque ligne est d'une couleur unie, sauf là où sont
 * les plateformes. On garde donc une couleur par ligne et les deux plateformes, transparentes là
 * où elles laissent voir la ligne : le fond s'adapte à la largeur et les plateformes se placent librement. */
#define BACKDROP_W 240
#define BACKDROP_H 112
#define BACKDROP_PERIOD 4 /* les rayures du haut se répètent toutes les 4 lignes */

typedef struct {
  uint16_t rows[BACKDROP_H];
  Image foe;    /* plateforme de l'adversaire ; x, y : sa place dans le fond d'origine */
  Image player; /* plateforme du joueur */
} Backdrop;

/* Police à chasse variable de l'expansion, 2 bits par pixel : 0 transparent, 1 texte, 2 ombre. Les
 * couleurs du texte et de l'ombre sont choisies au dessin. Glyphes triés par point de code Unicode ;
 * w est l'avance (espacement compris), toutes les lignes de la cellule (height) sont gardées. */
typedef struct PACKED {
  uint16_t cp;
  uint16_t ofs; /* premier octet dans les bits de la police ; pixels ligne après ligne */
  uint8_t w;
} Glyph;

typedef struct {
  const Glyph *glyphs;
  const uint8_t *bits;
  uint8_t count;
  uint8_t height; /* lignes de chaque glyphe */
  uint8_t space;  /* avance d'une espace */
  uint8_t line;   /* interligne */
} Font;

/* Classe d'un dresseur ordinaire : son sprite (trainer_pic[i]) et le nom affiché. */
typedef struct {
  const char *name;    /* « MONTAGNARD »… */
  const char *name_en; /* « HIKER »… */
  char gender;      /* 'm' ou 'f' */
  uint8_t rank;     /* 0 débutant, 1 confirmé, 2 expert */
} TrainerClass;

#endif
