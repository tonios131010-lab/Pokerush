/* La version PC jouable (POKERUSH_PC) : une fenêtre Windows à la place de l'écran de la calculatrice,
 * le clavier et la manette à la place de ses touches, le son en plus. eadk_host.c s'en sert quand
 * aucun script de touches n'est donné (SIM_SCRIPT : tests et captures, sans fenêtre). */
#ifndef PC_HOST_H
#define PC_HOST_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Ouvre la fenêtre la première fois (paramètres, manette, son) ; false en mode script. */
bool pc_open(void);
/* Les touches EADK enfoncées (masque 1 << eadk_key_*), clavier et manette réunis. */
uint64_t pc_keys(void);
/* Les messages de Windows, l'image si elle a changé, les vibrations. */
void pc_pump(void);
/* Attend ms millisecondes (une image du jeu) en gardant la fenêtre vivante. */
void pc_sleep(uint32_t ms);
/* Un fichier livré avec le jeu (data\…, à côté de l'exécutable) ou de sauvegarde (%APPDATA%\PokeRush). */
void pc_data_path(char *out, size_t size, const char *name);
void pc_save_path(char *out, size_t size, const char *name);

/* Son (pc_audio.c) : volumes de 0 à 100. */
void pc_audio_init(void);
void pc_audio_volumes(int music, int sfx, int cries);

#endif
