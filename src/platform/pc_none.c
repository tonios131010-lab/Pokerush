/* La calculatrice : rien de ce que la version PC ajoute (son, manette, fenêtre). */
#include "platform/pc.h"

#ifndef POKERUSH_PC
bool pc_present(const uint16_t *fb, int x, int y, int w, int h) { return false; }
void pc_music(int song) {}
void pc_cry(uint16_t species) {}
void pc_sfx(int sfx) {}
void pc_rumble(uint8_t strength, uint16_t ms) {}
const char *pc_key_label(uint16_t key) { return 0; }
int pc_setting_count(void) { return 0; }
void pc_setting_line(int i, char *out, int size) {}
const char *pc_setting_help(int i) { return ""; }
void pc_setting_change(int i) {}
bool pc_has_quit(void) { return false; }
#endif
