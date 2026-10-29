/* PR Diag : app de diagnostic à lancer sur la calculatrice avant d'aller plus loin. Elle vérifie,
 * de la moins risquée à la plus risquée, chaque partie qui dépend de la vraie machine :
 *   1. lecture seule : en-tête d'Epsilon, système de fichiers, liste des fichiers ;
 *   2. rendu : écran de combat et temps de dessin ;
 *   3. écriture : prdiag.py puis pokemon.py, relus ;
 *   4. touche Maison retenue (appel système 10) ;
 *   5. sortie propre (RAM effacée, appel système 13).
 * OK passe à la suite. Jusqu'à l'étape 4, Maison quitte l'app comme d'habitude. */
#include <eadk.h>
#include <string.h>
#include "assets.h"
#include "core/save.h"
#include "gfx/display.h"
#include "gfx/ui.h"
#include "platform/app.h"
#include "platform/input.h"
#include "platform/storage.h"

const char eadk_app_name[] __attribute__((section(".rodata.eadk_app_name"))) = "PR Diag";
const uint32_t eadk_api_level __attribute__((section(".rodata.eadk_api_level"))) = 0;

#define WHITE 0xFFFF
#define BLACK 0x0000
#define GREEN RGB565(0, 128, 0)
#define RED RGB565(200, 0, 0)
#define BLUE RGB565(0, 64, 160)

static int line;

static void clear(const char *title) {
  eadk_display_push_rect_uniform(eadk_screen_rect, WHITE);
  eadk_display_draw_string(title, (eadk_point_t){8, 6}, true, BLUE, WHITE);
  line = 34;
}

static void print(const char *s, eadk_color_t color) {
  eadk_display_draw_string(s, (eadk_point_t){8, (uint16_t)line}, false, color, WHITE);
  line += 15;
}

/* Petits formats sans printf : nombre décimal ou hexadécimal ajouté à la fin de buf. */
static char *add_str(char *buf, const char *s) { return strcat(buf, s); }
static char *add_num(char *buf, uint32_t v, int base) {
  char tmp[12], *p = tmp + sizeof tmp;
  *--p = 0;
  do *--p = "0123456789ABCDEF"[v % (uint32_t)base]; while (v /= (uint32_t)base);
  if (base == 16) strcat(buf, "0x");
  return strcat(buf, p);
}

static void wait_ok(void) {
  print("", BLACK);
  print("OK : continuer      Maison : quitter", BLUE);
  while (!(input_wait_frame() & KEY_OK)) {}
}

/* Renvoie false si le système de fichiers n'a pas été trouvé : les tests suivants qui écrivent ou
 * font des appels système sont alors sautés. */
static bool page_read(void) {
  char s[64];
  StorageInfo info;
  bool ok = storage_info(&info);
  clear("1/5  Lecture (aucune ecriture)");
  s[0] = 0;
  if (!info.header) {
    print("En-tete d'Epsilon : INTROUVABLE", RED);
    print("=> ne pas continuer, envoyer une photo", RED);
  } else {
    add_num(add_str(s, "En-tete d'Epsilon : "), info.header, 16);
    print(s, GREEN);
    s[0] = 0;
    print(add_str(add_str(s, "Version : "), info.version), BLACK);
  }
  s[0] = 0;
  add_num(add_str(s, "Fichiers en RAM : "), info.address, 16);
  print(s, ok ? GREEN : RED);
  s[0] = 0;
  add_num(add_str(add_num(add_str(s, "Utilise : "), info.used, 10), " / "), info.size, 10);
  add_num(add_str(add_str(s, " octets, "), ""), info.files, 10);
  print(add_str(s, " fichiers"), ok ? BLACK : RED);
  print(info.address >= 0x24000000u ? "Modele : N0120" : "Modele : N0110 / N0115", BLACK);
  print("", BLACK);
  const char *name;
  uint32_t len;
  for (uint16_t i = 0; i < 6 && storage_file(i, &name, &len); i++) {
    s[0] = 0;
    add_num(add_str(add_str(add_str(s, "  "), name), "  "), len, 10);
    print(add_str(s, " o"), BLACK);
  }
  if (info.files > 6) print("  ...", BLACK);
  wait_ok();
  return info.header && ok;
}

static void page_render(void) {
  BattleView v = {.backdrop = BACKDROP_GRASS,
                  .foe = {.species = 2, .name = "TORTANK", .hp = 70, .max_hp = 139},
                  .player = {.species = 1, .name = "DRACAUFEU", .hp = 138, .max_hp = 138},
                  .text = "Que doit faire\nDRACAUFEU ?", .shown = -1, .menu = true, .cursor = MENU_FIGHT};
  uint64_t t0 = eadk_timing_millis();
  for (int i = 0; i < 10; i++) ui_draw_battle(&v);
  uint32_t full = (uint32_t)(eadk_timing_millis() - t0) / 10;
  t0 = eadk_timing_millis();
  for (int i = 0; i < 10; i++) ui_draw_hpbox(&v, false);
  uint32_t box = (uint32_t)(eadk_timing_millis() - t0) / 10;
  char s[64] = "2/5 Ecran : ";
  add_num(add_str(add_num(s, full, 10), " ms, cadre PV : "), box, 10);
  eadk_display_draw_string(add_str(s, " ms"), (eadk_point_t){4, 2}, false, BLACK, WHITE);
  eadk_display_draw_string("OK : continuer", (eadk_point_t){4, 16}, false, BLACK, WHITE);
  while (!(input_wait_frame() & KEY_OK)) {}
}

static void page_write(void) {
  clear("3/5  Ecriture");
  print("Va creer prdiag.py et pokemon.py", BLACK);
  print("(supprimables ensuite dans l'app Python).", BLACK);
  print("OK : ecrire", BLUE);
  while (!(input_wait_frame() & KEY_OK)) {}

  static const char test[] = "\1# Test PokeRush : vous pouvez supprimer ce fichier.\nprint('PokeRush OK')\n";
  uint32_t len;
  bool w = eadk_storage_write("prdiag.py", test, sizeof test);
  const uint8_t *p = eadk_storage_read("prdiag.py", &len);
  bool r = p && len == sizeof test && !memcmp(p, test, sizeof test);
  print(w ? "prdiag.py ecrit : OK" : "prdiag.py ecrit : ECHEC", w ? GREEN : RED);
  print(r ? "prdiag.py relu : OK" : "prdiag.py relu : ECHEC", r ? GREEN : RED);

  Run run, back;
  memset(&run, 0, sizeof run);
  run.battle = 1, run.team.count = 1, run.team.mon[0].species = 1, run.seed = 12345;
  w = save_write(&run, NULL);
  r = save_read(&back, NULL) && !memcmp(&run, &back, sizeof run);
  print(w ? "pokemon.py ecrit : OK" : "pokemon.py ecrit : ECHEC", w ? GREEN : RED);
  print(r ? "pokemon.py relu : OK" : "pokemon.py relu : ECHEC", r ? GREEN : RED);
  print("Apres : ouvrir l'app Python, lancer prdiag.py", BLACK);
  print("=> doit afficher PokeRush OK", BLACK);
  wait_ok();
}

static void page_home(void) {
  clear("4/5  Touche Maison");
  print("Sauvegardez vos scripts avant ce test.", RED);
  print("OK : retenir la touche Maison (svc 10)", BLUE);
  while (!(input_wait_frame() & KEY_OK)) {}
  app_begin();
  clear("4/5  Touche Maison retenue");
  print("Appuyez sur Maison plusieurs fois :", BLACK);
  print("l'app doit rester ouverte et compter.", BLACK);
  print("Puis OK pour quitter proprement.", BLUE);
  int count = 0;
  for (;;) {
    uint16_t k = input_wait_frame();
    if (k & KEY_OK) break;
    if (k & KEY_HOME) {
      char s[32] = "Maison vue : ";
      add_num(s, (uint32_t)++count, 10);
      eadk_display_draw_string(s, (eadk_point_t){8, 120}, true, GREEN, WHITE);
    }
  }
  clear("5/5  Sortie");
  print("RAM de l'app effacee, Maison rendue (svc 13).", BLACK);
  print("Bon si on revient a l'accueil sans redemarrage", BLACK);
  print("et si les scripts sont toujours la.", BLACK);
  eadk_timing_msleep(2500);
}

int main(void) {
  bool found = page_read();
  page_render();
  if (!found) {
    clear("Diagnostic arrete");
    print("Systeme de fichiers introuvable : les tests", RED);
    print("d'ecriture et de la touche Maison sont sautes.", RED);
    print("Rien n'a ete modifie. Envoyer une photo de la page 1.", BLACK);
    eadk_timing_msleep(6000);
    return 0;
  }
  page_write();
  page_home();
  return app_end();
}
