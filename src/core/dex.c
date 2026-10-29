#include "core/dex.h"
#include <string.h>
#include "core/base64.h"
#include "platform/storage.h"

Dex dex;

typedef struct __attribute__((packed)) {
  char magic[2]; /* "PD" */
  uint8_t version;
  Dex dex;
  uint32_t check;
} DexBlob;

static const char head[] = "# PokeRush : carnet des POKeMON, ne pas modifier.\nDEX = \"";
static const char tail[] = "\"\n";
#define B64 BASE64_LEN(sizeof(DexBlob))
#define FILE_LEN (1 + sizeof head - 1 + B64 + sizeof tail - 1 + 1)

static uint32_t fnv1a(const void *data, uint32_t n) {
  const uint8_t *p = data;
  uint32_t h = 2166136261u;
  while (n--) h = (h ^ *p++) * 16777619u;
  return h;
}

void dex_load(void) {
  memset(&dex, 0, sizeof dex);
  dex.anim = dex.choice = 1;
  uint32_t len;
  const uint8_t *p = eadk_storage_read(DEX_FILE, &len);
  const char *key = "DEX = \"";
  uint32_t klen = (uint32_t)strlen(key), i = 0;
  if (!p) return;
  while (i + klen + B64 < len && memcmp(p + i, key, klen)) i++;
  if (i + klen + B64 >= len) return;
  DexBlob b;
  if (base64_decode(&b, sizeof b, (const char *)p + i + klen, B64) != (int32_t)sizeof b) return;
  if (b.magic[0] != 'P' || b.magic[1] != 'D' || b.version != 2 || b.check != fnv1a(&b, sizeof b - 4)) return;
  dex = b.dex;
}

bool dex_save(void) {
  DexBlob b;
  memset(&b, 0, sizeof b);
  b.magic[0] = 'P', b.magic[1] = 'D', b.version = 2, b.dex = dex;
  b.check = fnv1a(&b, sizeof b - 4);
  static uint8_t file[FILE_LEN];
  uint8_t *p = file;
  *p++ = 1;
  memcpy(p, head, sizeof head - 1), p += sizeof head - 1;
  p += base64_encode((char *)p, &b, sizeof b);
  memcpy(p, tail, sizeof tail - 1), p += sizeof tail - 1;
  *p = 0;
  return eadk_storage_write(DEX_FILE, file, FILE_LEN);
}

void dex_see(uint16_t s) { dex.seen[s >> 3] |= (uint8_t)(1 << (s & 7)); }

void dex_own(uint16_t s, bool shiny) {
  dex_see(s);
  dex.owned[s >> 3] |= (uint8_t)(1 << (s & 7));
  if (shiny) dex.shiny[s >> 3] |= (uint8_t)(1 << (s & 7));
}

int dex_count(const uint8_t *bits) {
  int n = 0;
  for (int i = 0; i < DEX_SPECIES / 8; i++)
    for (uint8_t v = bits[i]; v; v &= (uint8_t)(v - 1)) n++;
  return n;
}
