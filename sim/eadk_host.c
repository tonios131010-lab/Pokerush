/* L'EADK sur PC. Deux usages :
 *
 * Le jeu sur PC (compilé avec POKERUSH_PC, sans SIM_SCRIPT) : une fenêtre, le clavier, la manette et
 * le son (pc_video.c, pc_audio.c). Les sauvegardes vont dans %APPDATA%\PokeRush.
 *
 * Les tests et les captures (SIM_SCRIPT) : pas de fenêtre ; l'écran est un tableau 320×240 comme celui
 * de la calculatrice, le clavier suit le script et des captures sont écrites (PPM).
 *   SIM_SCRIPT="_*5 shot:debut right ok _*40 shot:attaque home" build/sim/pokerush
 *
 * Chaque mot du script vaut pour une lecture du clavier : left up down right ok back home shift exe, ou
 * _ (rien) ; "*N" le répète N fois ; "shot:nom" écrit SIM_OUT/nom.ppm (SIM_OUT vaut "." par défaut).
 * À la fin du script, la touche Maison est enfoncée puis relâchée en boucle pour quitter (le jeu ne
 * voit que les touches qui viennent d'être enfoncées : un appui pendant une animation serait perdu). */
#include <eadk.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef POKERUSH_PC
#include "pc_host.h"
#endif

static uint16_t screen[EADK_SCREEN_WIDTH * EADK_SCREEN_HEIGHT];
static uint64_t clock_ms, keys;
static const char *script;
static int repeat;
static uint64_t token_keys;

/* Le jeu dans une fenêtre (sinon : un script de touches). */
static bool interactive(void) {
#ifdef POKERUSH_PC
  return pc_open();
#else
  return false;
#endif
}

void eadk_display_push_rect(eadk_rect_t r, const eadk_color_t *pixels) {
  for (int j = 0; j < r.height; j++)
    memcpy(&screen[(r.y + j) * EADK_SCREEN_WIDTH + r.x], pixels + j * r.width, r.width * sizeof *pixels);
}

void eadk_display_push_rect_uniform(eadk_rect_t r, eadk_color_t color) {
  for (int j = 0; j < r.height; j++)
    for (int i = 0; i < r.width; i++) screen[(r.y + j) * EADK_SCREEN_WIDTH + r.x + i] = color;
}

void eadk_display_pull_rect(eadk_rect_t r, eadk_color_t *pixels) {
  for (int j = 0; j < r.height; j++)
    memcpy(pixels + j * r.width, &screen[(r.y + j) * EADK_SCREEN_WIDTH + r.x], r.width * sizeof *pixels);
}

bool eadk_display_wait_for_vblank(void) { return true; }
/* La police du système n'est pas imitée : le texte part dans la console. */
void eadk_display_draw_string(const char *text, eadk_point_t p, bool large, eadk_color_t fg, eadk_color_t bg) {
  if (*text) printf("[ecran %3d,%3d] %s\n", p.x, p.y, text);
}

/* SIM_OUT/<nom><ext> (SIM_OUT vaut "." par défaut). */
static void out_path(char *path, size_t size, const char *name, int len, const char *ext) {
  const char *dir = getenv("SIM_OUT");
  snprintf(path, size, "%s/%.*s%s", dir ? dir : ".", len, name, ext);
}

static void shot(const char *name, int len) {
  char path[512];
  out_path(path, sizeof path, name, len, ".ppm");
  FILE *f = fopen(path, "wb");
  if (!f) return;
  fprintf(f, "P6\n%d %d\n255\n", EADK_SCREEN_WIDTH, EADK_SCREEN_HEIGHT);
  for (int i = 0; i < EADK_SCREEN_WIDTH * EADK_SCREEN_HEIGHT; i++) {
    uint16_t c = screen[i];
    unsigned char rgb[3] = {(unsigned char)((c >> 11) << 3), (unsigned char)(((c >> 5) & 63) << 2),
                            (unsigned char)((c & 31) << 3)};
    fwrite(rgb, 1, 3, f);
  }
  fclose(f);
}

/* Le mot suivant du script : ses touches et son nombre de répétitions. */
static void next_token(void) {
  static const struct { const char *name; eadk_key_t key; } names[] = {
      {"left", eadk_key_left}, {"up", eadk_key_up}, {"down", eadk_key_down}, {"right", eadk_key_right},
      {"ok", eadk_key_ok}, {"back", eadk_key_back}, {"home", eadk_key_home}, {"shift", eadk_key_shift},
      {"exe", eadk_key_exe}};
  for (;;) {
    while (*script == ' ') script++;
    if (!*script) {
      token_keys = token_keys == 1ull << eadk_key_home ? 0 : 1ull << eadk_key_home, repeat = 1;
      return;
    }
    const char *end = script + strcspn(script, " ");
    if (!strncmp(script, "shot:", 5)) {
      shot(script + 5, (int)(end - script - 5));
      script = end;
      continue;
    }
    size_t len = strcspn(script, "* ");
    token_keys = 0;
    for (size_t i = 0; i < sizeof names / sizeof names[0]; i++)
      if (strlen(names[i].name) == len && !strncmp(script, names[i].name, len)) token_keys = 1ull << names[i].key;
    repeat = script[len] == '*' ? atoi(script + len + 1) : 1;
    script = end;
    return;
  }
}

void _eadk_keyboard_scan_do_scan(void) {
#ifdef POKERUSH_PC
  if (interactive()) {
    pc_pump();
    keys = pc_keys();
    return;
  }
#endif
  if (!script) script = getenv("SIM_SCRIPT") ? getenv("SIM_SCRIPT") : "";
  if (repeat <= 0) next_token();
  repeat--;
  keys = token_keys;
}
uint32_t _eadk_keyboard_scan_low(void) { return (uint32_t)keys; }
uint32_t _eadk_keyboard_scan_high(void) { return (uint32_t)(keys >> 32); }

void eadk_timing_usleep(uint32_t us) { eadk_timing_msleep(us / 1000); }
void eadk_timing_msleep(uint32_t ms) {
  clock_ms += ms;
#ifdef POKERUSH_PC
  if (interactive()) pc_sleep(ms);
#endif
}
uint32_t _eadk_timing_millis_low(void) { return (uint32_t)clock_ms; }
uint32_t _eadk_timing_millis_high(void) { return (uint32_t)(clock_ms >> 32); }

/* ------------------------------------------------------------------------- fichiers d'Epsilon
 * Le même format que sur la calculatrice, dans un tampon gardé dans un fichier : storage.bin dans
 * %APPDATA%\PokeRush pour le jeu sur PC, dans SIM_OUT pour les scripts. Un tampon neuf contient déjà un
 * script (exemple.py), pour que la sauvegarde ne soit pas le seul fichier. En mode script, après chaque
 * écriture, les scripts Python sont recopiés en clair dans SIM_OUT/, pour les lire. */
#define SIM_STORAGE 32768
static uint8_t storage[SIM_STORAGE];
static bool storage_loaded;

static void storage_path(char *path, size_t size) {
#ifdef POKERUSH_PC
  if (interactive()) return pc_save_path(path, size, "storage.bin");
#endif
  out_path(path, size, "storage", 7, ".bin");
}

uint8_t *sim_storage_area(uint32_t *size) {
  if (!storage_loaded) {
    storage_loaded = true;
    char path[512];
    storage_path(path, sizeof path);
    FILE *f = fopen(path, "rb");
    if (!f || fread(storage, 1, SIM_STORAGE, f) != SIM_STORAGE) {
      static const char example[] = "exemple.py\0\1print('Bonjour')\n";
      uint16_t n = (uint16_t)(2 + sizeof example);
      memset(storage, 0, SIM_STORAGE);
      storage[0] = (uint8_t)n, storage[1] = (uint8_t)(n >> 8);
      memcpy(storage + 2, example, sizeof example);
    }
    if (f) fclose(f);
  }
  *size = SIM_STORAGE;
  return storage;
}

void sim_storage_commit(void) {
  char path[512];
  storage_path(path, sizeof path);
  FILE *f = fopen(path, "wb");
  if (f) fwrite(storage, 1, SIM_STORAGE, f), fclose(f);
  if (interactive()) return;
  for (uint32_t p = 0; p + 2 <= SIM_STORAGE;) {
    uint16_t n = (uint16_t)(storage[p] | storage[p + 1] << 8);
    if (!n) break;
    const char *name = (const char *)storage + p + 2;
    size_t nlen = strlen(name);
    if (nlen > 3 && !strcmp(name + nlen - 3, ".py")) {
      const uint8_t *content = storage + p + 2 + nlen + 1;
      size_t clen = n - 2 - nlen - 1;
      out_path(path, sizeof path, name, (int)nlen, "");
      if ((f = fopen(path, "wb"))) {
        /* sans l'octet d'état du début ni le zéro de la fin */
        fwrite(content + 1, 1, clen > 2 ? clen - 2 : 0, f);
        fclose(f);
      }
    }
    p += n;
  }
}

uint32_t eadk_random(void) {
  static uint32_t s = 2463534242u;
  static bool seeded;
  if (!seeded) { /* le jeu sur PC : une partie différente à chaque lancement ; les scripts : toujours la même */
    seeded = true;
    if (interactive()) s ^= (uint32_t)time(NULL) * 2654435761u;
  }
  s ^= s << 13, s ^= s >> 17, s ^= s << 5;
  return s;
}

bool eadk_usb_is_plugged(void) { return true; }
void eadk_backlight_set_brightness(uint8_t b) {}
uint8_t eadk_backlight_brightness(void) { return 255; }
bool eadk_battery_is_charging(void) { return false; }
uint8_t eadk_battery_level(void) { return 100; }
float eadk_battery_voltage(void) { return 4.0f; }
const char *eadk_external_data;
size_t eadk_external_data_size;
