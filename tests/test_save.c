/* Tests de la sauvegarde sur PC (make test) : Base64, fichiers d'Epsilon, pokemon.py.
 * Le système de fichiers est celui du simulateur (sim/eadk_host.c), neuf à chaque test. */
#include <stdio.h>
#include <string.h>
#include "core/base64.h"
#include "core/save.h"
#include "data.h"
#include "platform/storage.h"

uint8_t *sim_storage_area(uint32_t *size);

static int failures;
#define CHECK(c)                                                        \
  do {                                                                  \
    if (!(c)) printf("ECHEC ligne %d : %s\n", __LINE__, #c), failures++; \
  } while (0)

static void fresh_storage(void) {
  uint32_t size;
  uint8_t *s = sim_storage_area(&size);
  memset(s, 0, size); /* (après le premier appel : le fichier storage.bin n'est plus relu) */
}

static Run sample_run(void) {
  Run r;
  memset(&r, 0, sizeof r);
  r.seed = 0xDEADBEEF;
  r.battle = 37;
  r.bag[0] = 5;
  r.team.count = 3;
  for (int i = 0; i < 3; i++) {
    r.team.mon[i].species = (uint8_t)(10 * i + 1);
    r.team.mon[i].nature = (uint8_t)(3 + i);
    pokemon_set_move(&r.team.mon[i], 0, 100 + i);
    pokemon_set_move(&r.team.mon[i], 3, NUM_MOVES - 1);
    r.team.mon[i].item = NUM_ITEMS - 1;
    r.team.mon[i].shiny = i == 1;
  }
  return r;
}

static void test_base64(void) {
  char out[16];
  CHECK(base64_encode(out, "Man", 3) == 4 && !memcmp(out, "TWFu", 4));
  CHECK(base64_encode(out, "Ma", 2) == 4 && !memcmp(out, "TWE=", 4));
  CHECK(base64_encode(out, "M", 1) == 4 && !memcmp(out, "TQ==", 4));
  for (uint32_t n = 0; n < 10; n++) {
    uint8_t in[10], back[10];
    for (uint32_t i = 0; i < n; i++) in[i] = (uint8_t)(i * 77 + 3);
    char text[16];
    uint32_t len = base64_encode(text, in, n);
    CHECK(len == BASE64_LEN(n));
    CHECK(base64_decode(back, sizeof back, text, len) == (int32_t)n && !memcmp(in, back, n));
  }
  uint8_t b[4];
  CHECK(base64_decode(b, sizeof b, "TW#u", 4) < 0);
  CHECK(base64_decode(b, sizeof b, "TWF", 3) < 0);
  CHECK(base64_decode(b, 2, "TWFu", 4) < 0);
}

static void test_storage(void) {
  fresh_storage();
  uint32_t len;
  CHECK(!eadk_storage_read("a.py", &len));
  CHECK(eadk_storage_write("a.py", "\1x=1\n", 6));
  CHECK(eadk_storage_write("b.py", "\1y=2\n", 6));
  const uint8_t *p = eadk_storage_read("a.py", &len);
  CHECK(p && len == 6 && !memcmp(p, "\1x=1\n", 6));
  CHECK(eadk_storage_write("a.py", "\1x=9\n", 6));             /* même taille, au milieu : sur place */
  CHECK(!eadk_storage_write("a.py", "\1x=10\n", 7));           /* autre taille, au milieu : refusé */
  CHECK(eadk_storage_write("b.py", "\1y=200\n", 8));           /* le dernier peut grandir */
  CHECK(eadk_storage_write("b.py", "\1y\n", 4));               /* et rétrécir */
  p = eadk_storage_read("a.py", &len);
  CHECK(p && len == 6 && !memcmp(p, "\1x=9\n", 6));
  p = eadk_storage_read("b.py", &len);
  CHECK(p && len == 4 && !memcmp(p, "\1y\n", 4));
  uint32_t size;
  uint8_t *s = sim_storage_area(&size);
  uint32_t end = (uint32_t)(p + len - s);
  CHECK(s[end] == 0 && s[end + 1] == 0 && s[end + 2] == 0 && s[end + 3] == 0); /* fin propre */
  static uint8_t big[40000];
  CHECK(!eadk_storage_write("c.py", big, sizeof big)); /* plein : refusé, rien n'est abîmé */
  CHECK(eadk_storage_read("b.py", &len) && len == 4);
}

static void test_save(void) {
  fresh_storage();
  Run in = sample_run(), out;
  memset(&out, 0, sizeof out);
  CHECK(!save_read(&out, NULL));
  CHECK(save_write(&in, NULL));
  CHECK(eadk_storage_write("apres.py", "\1z=3\n", 6)); /* pokemon.py n'est plus le dernier */
  CHECK(save_write(&in, NULL));                               /* même taille : réécrit sur place */
  CHECK(save_read(&out, NULL) && !memcmp(&in, &out, sizeof in));
  CHECK(out.team.mon[1].species == 11 && pokemon_move(&out.team.mon[2], 0) == 102 && out.team.mon[1].shiny && out.battle == 37);

  uint32_t len;
  const uint8_t *p = eadk_storage_read(SAVE_FILE, &len);
  CHECK(p && p[0] == 1 && p[len - 1] == 0 && !memcmp(p + 1, "# PokeRush", 10));
  printf("pokemon.py (%u octets) :\n%s\n", (unsigned)len, (const char *)p + 1);

  /* Un caractère abîmé : la somme de contrôle le voit. */
  uint8_t *q = (uint8_t *)p + len - 20;
  uint8_t keep = *q;
  *q = *q == 'A' ? 'B' : 'A';
  CHECK(!save_read(&out, NULL));
  *q = keep;
  CHECK(save_read(&out, NULL));

  /* Une sauvegarde cohérente pour la somme de contrôle mais pas pour le jeu : refusée. */
  Run bad = in;
  bad.team.mon[0].species = 1023;
  CHECK(save_write(&bad, NULL) && !save_read(&out, NULL));
  bad = in;
  bad.team.count = 0;
  CHECK(save_write(&bad, NULL) && !save_read(&out, NULL));
  bad = in;
  bad.team.mon[2].species = NUM_BASE_SPECIES; /* une forme Méga ne se sauvegarde jamais */
  CHECK(save_write(&bad, NULL) && !save_read(&out, NULL));
  /* les 511 attaques remplissent les 9 bits de MOVE_BITS : toute valeur d'attaque est valide */
  _Static_assert(NUM_MOVES == 1 << MOVE_BITS, "attaques");

  /* Un fichier rallongé à la main, au milieu des autres : il garde sa taille. */
  fresh_storage();
  static uint8_t edited[300];
  memset(edited, '#', sizeof edited);
  edited[0] = 1, edited[299] = 0;
  CHECK(eadk_storage_write(SAVE_FILE, edited, sizeof edited));
  CHECK(eadk_storage_write("apres.py", "\1z=3\n", 6));
  CHECK(!save_read(&out, NULL));
  CHECK(save_write(&in, NULL) && save_read(&out, NULL) && !memcmp(&in, &out, sizeof in));
  CHECK(eadk_storage_read(SAVE_FILE, &len) && len == 300);
}

int main(void) {
  test_base64();
  test_storage();
  test_save();
  printf(failures ? "%d test(s) en échec\n" : "Tous les tests passent.\n", failures);
  return failures != 0;
}
