#include "core/save.h"
#include <stddef.h>
#include <string.h>
#include "data.h"
#include "core/base64.h"
#include "platform/storage.h"

#define SAVE_VERSION 4 /* 4 : espèces sur 10 bits, difficulté, graine */

typedef struct PACKED {
  char magic[2]; /* "PR" */
  uint8_t version;
  Run run;
  RunStats stats;
  uint8_t reserved[10]; /* place pour les versions suivantes, sans changer la taille du fichier */
  uint32_t check;       /* FNV-1a de tout ce qui précède */
} SaveBlob;
_Static_assert(sizeof(SaveBlob) == 96, "SaveBlob : 96 octets, 128 caractères en Base64");

static const char head[] = "# PokeRush : sauvegarde automatique, ne pas modifier.\nSAVE_DATA = \"";
static const char tail[] = "\"\n";
#define B64 BASE64_LEN(sizeof(SaveBlob))
/* Contenu d'un script d'Epsilon : un octet d'état (1 = importable), le texte, un zéro final. */
#define FILE_LEN (1 + sizeof head - 1 + B64 + sizeof tail - 1 + 1)

static uint32_t fnv1a(const void *data, uint32_t n) {
  const uint8_t *p = data;
  uint32_t h = 2166136261u;
  while (n--) h = (h ^ *p++) * 16777619u;
  return h;
}

bool save_write(const Run *run, const RunStats *stats) {
  SaveBlob b;
  memset(&b, 0, sizeof b);
  b.magic[0] = 'P', b.magic[1] = 'R';
  b.version = SAVE_VERSION;
  b.run = *run;
  if (stats) b.stats = *stats;
  b.check = fnv1a(&b, offsetof(SaveBlob, check));

  /* Un fichier plus long (modifié à la main) garde sa taille, complété d'espaces : ainsi il n'a
   * jamais besoin de bouger, même s'il n'est pas le dernier. */
  static uint8_t file[512];
  uint32_t old, len = FILE_LEN;
  if (eadk_storage_read(SAVE_FILE, &old) && old > FILE_LEN && old <= sizeof file) len = old;
  uint8_t *p = file;
  *p++ = 1;
  memcpy(p, head, sizeof head - 1), p += sizeof head - 1;
  p += base64_encode((char *)p, &b, sizeof b);
  memcpy(p, tail, sizeof tail - 1), p += sizeof tail - 1;
  memset(p, ' ', (size_t)(file + len - 1 - p));
  file[len - 1] = 0;
  return eadk_storage_write(SAVE_FILE, file, len);
}

/* Les champs qui serviront d'indices dans les tables doivent rester dans leurs bornes. */
static bool run_valid(const Run *r) {
  if (!r->battle || !r->team.count || r->team.count > TEAM_MAX) return false;
  for (int i = 0; i < r->team.count; i++) {
    const Pokemon *m = &r->team.mon[i];
    if (m->species >= NUM_BASE_SPECIES || m->nature >= NUM_NATURES || m->item >= NUM_ITEMS) return false;
    for (uint8_t k = 0; k < MOVES_MAX; k++)
      if (pokemon_move(m, k) >= NUM_MOVES) return false;
  }
  return true;
}

bool save_read(Run *run, RunStats *stats) {
  uint32_t len;
  const uint8_t *p = eadk_storage_read(SAVE_FILE, &len);
  if (!p) return false;
  const char *key = "SAVE_DATA = \"";
  uint32_t klen = (uint32_t)strlen(key), i = 0;
  while (i + klen + B64 < len && memcmp(p + i, key, klen)) i++;
  if (i + klen + B64 >= len || p[i + klen + B64] != '"') return false;

  SaveBlob b;
  if (base64_decode(&b, sizeof b, (const char *)p + i + klen, B64) != (int32_t)sizeof b) return false;
  if (b.magic[0] != 'P' || b.magic[1] != 'R' || b.version != SAVE_VERSION) return false;
  if (b.check != fnv1a(&b, offsetof(SaveBlob, check)) || !run_valid(&b.run)) return false;
  *run = b.run;
  if (stats) *stats = b.stats;
  return true;
}
