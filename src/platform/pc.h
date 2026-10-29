/* Ce que la version PC ajoute à la calculatrice : musique, cris et bruitages, vibrations de la manette,
 * affichage dans une fenêtre (plein écran, mise à l'échelle), noms des touches du clavier ou de la
 * manette, paramètres en plus et une option pour quitter. Le jeu appelle ces fonctions partout ; sur la
 * calculatrice elles ne font rien (platform/pc_none.c), sur PC elles sont dans sim/ (POKERUSH_PC). */
#ifndef PC_H
#define PC_H
#include <stdbool.h>
#include <stdint.h>

enum {
  MUSIC_NONE,
  MUSIC_TITLE,
  MUSIC_DRAFT,     /* récompense, préparation */
  MUSIC_TRAINER,   /* dresseur ordinaire */
  MUSIC_GYM,       /* Champion d'arène de Hoenn */
  MUSIC_RIVAL,     /* FLORA */
  MUSIC_ELITE,     /* Conseil 4 */
  MUSIC_CHAMPION,  /* le Maître */
  MUSIC_LEGEND,    /* raid d'un légendaire géant */
  MUSIC_KANTO_GYM, /* Kanto, après le combat 100 */
  MUSIC_VICTORY_TRAINER,
  MUSIC_VICTORY_GYM,
  MUSIC_VICTORY_LEAGUE,
  MUSIC_HALL_OF_FAME,
  MUSIC_ENDING,
  MUSIC_COUNT
};

enum { SFX_CURSOR, SFX_SELECT, SFX_BACK, SFX_HIT, SFX_HIT_SUPER, SFX_HIT_WEAK, SFX_MEGA, SFX_SHINY, SFX_COUNT };

/* L'écran logique (display.c) est prêt sur le rectangle donné : true si le PC l'affiche lui-même
 * (fenêtre), false pour l'envoyer comme sur la calculatrice. */
bool pc_present(const uint16_t *fb, int x, int y, int w, int h);
void pc_music(int song); /* MUSIC_* ; la même musique continue sans recommencer */
void pc_cry(uint16_t species);
void pc_sfx(int sfx);
void pc_rumble(uint8_t strength, uint16_t ms); /* manette : 0..255, durée en ms */
/* Le nom à afficher pour une touche du jeu (KEY_OK…) selon le clavier ou la manette, ou NULL : celui
 * de la calculatrice. */
const char *pc_key_label(uint16_t key);
/* Les paramètres du PC, à la suite de ceux du jeu : libellé (avec sa valeur), aide, OK les change. */
int pc_setting_count(void);
void pc_setting_line(int i, char *out, int size);
const char *pc_setting_help(int i);
void pc_setting_change(int i);
/* Le PC propose QUITTER à l'écran titre (sur la calculatrice : la touche Maison). */
bool pc_has_quit(void);

#endif
