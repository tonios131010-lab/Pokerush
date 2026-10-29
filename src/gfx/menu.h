/* Menus en liste, dans le style des fenêtres d'Émeraude : un bandeau de titre, la liste à gauche
 * (avec l'icône de chaque Pokémon s'il y en a), et à droite une fenêtre de détail que dessine
 * l'appelant pour la ligne sous le curseur. Haut / Bas pour choisir, OK pour valider, Retour pour
 * annuler. */
#ifndef MENU_H
#define MENU_H
#include <stdbool.h>
#include <stdint.h>

/* Détail de la ligne i dans la fenêtre (x, y, w, h) ; ligne i dessinée par l'appelant (après l'icône). */
typedef void (*menu_detail_fn)(const void *ctx, int i, int x, int y, int w, int h);
typedef void (*menu_row_fn)(const void *ctx, int i, int x, int y, int w);

typedef struct {
  const char *title;
  const char *const *items; /* libellés (si row est NULL) */
  int n;
  const uint16_t *icons;  /* espèce de chaque ligne (icônes), ou NULL */
  menu_detail_fn detail; /* fenêtre de droite, ou NULL : la liste prend toute la largeur */
  menu_row_fn row;       /* contenu d'une ligne, ou NULL : son libellé */
  const void *ctx;
  int hue;    /* teinte du fond (widgets.h) */
  int cursor; /* ligne sous le curseur au départ */
  int list_w; /* largeur de la liste quand il y a un détail (0 : 104) */
  bool detail_below; /* le détail dans une fenêtre sous la liste (toute la largeur) */
  const char *const *descs; /* sans detail : une phrase par ligne, montrée sous la liste */
  const char *help;         /* la barre d'aide du bas (NULL : « OK choisir · RETOUR annuler ») */
  bool alt;                 /* shift ferme le menu (MENU_ALT) */
  bool exe;                 /* EXE ferme le menu (MENU_EXE) au lieu de valider la ligne */
  bool no_back;             /* Retour ne ferme pas le menu */
} Menu;

enum { MENU_BACK = -1, MENU_ALT = -2, MENU_EXE = -3 };

/* La ligne choisie, ou MENU_BACK (Retour, ou Maison : *quit passe à vrai), MENU_ALT, MENU_EXE. */
int menu_run(Menu *m, bool *quit);
/* Menu simple sans détail. */
int menu_list(const char *title, const char *const *items, int n, int hue, bool *quit);
/* Menu simple avec une phrase d'explication pour chaque ligne. */
int menu_list_desc(const char *title, const char *const *items, const char *const *descs, int n, int hue, bool *quit);
/* Une question oui / non dans une petite fenêtre (vrai = oui). Le texte va à la ligne de lui-même
 * (3 lignes au plus). */
bool menu_confirm(const char *question, bool *quit);
/* Un message dans une fenêtre, jusqu'à OK ou Retour (3 lignes au plus). */
void menu_message(const char *text, bool *quit);

#endif
