/* Le système de fichiers d'Epsilon : un tampon en RAM entouré de deux mots magiques, rempli
 * d'enregistrements [taille totale : 2 octets][nom "base.ext" et zéro][contenu], qu'une taille 0
 * termine. Son adresse est donnée par l'en-tête du logiciel (« userland ») d'Epsilon, au début de
 * chacune des deux copies du logiciel en flash externe (64 Kio plus loin sur la N0120 que sur la
 * N0110) : on prend celui dont la zone d'apps contient ce code. */
#include "platform/storage.h"
#include <string.h>

#if PLATFORM_DEVICE
#define USERLAND_MAGIC 0xDEC0EDFEu
#define STORAGE_MAGIC 0xEE0BDDBAu

typedef struct {
  uint32_t magic;
  char version[8];
  uint32_t storage_ram, storage_size;
  uint32_t apps_flash_start, apps_flash_end;
} Userland;

static uint32_t rd32(const volatile uint8_t *p) {
  return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24;
}

/* L'en-tête valide dont la zone d'apps contient ce code, ou NULL. */
static const volatile Userland *userland(void) {
  static const uint32_t slots[] = {0x90010000u, 0x90020000u, 0x90410000u, 0x90420000u};
  uint32_t self = (uint32_t)(uintptr_t)&userland;
  for (unsigned i = 0; i < sizeof slots / sizeof slots[0]; i++) {
    const volatile Userland *u = (const volatile Userland *)(uintptr_t)slots[i];
    if (u->magic != USERLAND_MAGIC || self < u->apps_flash_start || self >= u->apps_flash_end) continue;
    uint32_t ram = u->storage_ram, n = u->storage_size;
    /* la RAM de la N0110 (0x20000000) ou de la N0120 (0x24000000) */
    bool in_ram = (ram >= 0x20000000u && ram + n + 8 <= 0x20040000u) ||
                  (ram >= 0x24000000u && ram + n + 8 <= 0x24080000u);
    if (!in_ram || (ram & 3) || n < 1024) continue;
    const volatile uint8_t *s = (const volatile uint8_t *)(uintptr_t)ram;
    if (rd32(s) != STORAGE_MAGIC || rd32(s + 4 + n) != STORAGE_MAGIC) continue;
    return u;
  }
  return NULL;
}

static uint8_t *storage_area(uint32_t *size) {
  const volatile Userland *u = userland();
  if (!u) return NULL;
  *size = u->storage_size;
  return (uint8_t *)(uintptr_t)(u->storage_ram + 4);
}

static void storage_commit(void) {}

static void header_info(StorageInfo *info) {
  const volatile Userland *u = userland();
  info->header = (uint32_t)(uintptr_t)u;
  for (int i = 0; i < 8; i++) info->version[i] = u ? u->version[i] : 0;
  info->version[8] = 0;
}
#else
/* Sur PC, sim/eadk_host.c fournit le tampon et le garde dans un fichier. */
uint8_t *sim_storage_area(uint32_t *size);
void sim_storage_commit(void);
static uint8_t *storage_area(uint32_t *size) { return sim_storage_area(size); }
static void storage_commit(void) { sim_storage_commit(); }
static void header_info(StorageInfo *info) {
  info->header = 0;
  strcpy(info->version, "PC");
}
#endif

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static void wr16(uint8_t *p, uint32_t v) {
  p[0] = (uint8_t)v;
  p[1] = (uint8_t)(v >> 8);
}

/* Parcourt la liste : renvoie la position de la taille 0 qui la termine, ou -1 si elle est abîmée
 * (on n'écrit alors rien). *found reçoit la position de l'enregistrement `name`, ou -1. */
static int32_t scan(const uint8_t *buf, uint32_t size, const char *name, int32_t *found) {
  uint32_t nlen = (uint32_t)strlen(name) + 1, p = 0;
  *found = -1;
  while (p + 2 <= size) {
    uint16_t n = rd16(buf + p);
    if (!n) return (int32_t)p;
    if (n < 4 || p + n > size) return -1;
    if (*found < 0 && n >= 2 + nlen && !memcmp(buf + p + 2, name, nlen)) *found = (int32_t)p;
    p += n;
  }
  return -1;
}

const uint8_t *eadk_storage_read(const char *name, uint32_t *len) {
  uint32_t size;
  int32_t at;
  const uint8_t *buf = storage_area(&size);
  if (!buf || scan(buf, size, name, &at) < 0 || at < 0) return NULL;
  uint32_t nlen = (uint32_t)strlen(name) + 1;
  *len = rd16(buf + at) - 2 - nlen;
  return buf + at + 2 + nlen;
}

bool eadk_storage_write(const char *name, const void *data, uint32_t len) {
  uint32_t size;
  int32_t at;
  uint8_t *buf = storage_area(&size);
  if (!buf) return false;
  int32_t end = scan(buf, size, name, &at);
  uint32_t nlen = (uint32_t)strlen(name) + 1, total = 2 + nlen + len;
  if (end < 0 || total > 0xFFFF) return false;
  if (at >= 0 && rd16(buf + at) != total) {
    if (at + rd16(buf + at) != end) return false; /* au milieu : il faudrait déplacer les suivants */
    /* le dernier : il est réécrit à sa place, à sa nouvelle taille ; rien ne bouge */
  } else if (at < 0) {
    at = end;
  }
  if (at == end || rd16(buf + at) != total) {
    uint32_t old_end = (uint32_t)end + 2, new_end = (uint32_t)at + total + 2;
    if (new_end > size) return false;
    wr16(buf + at + total, 0); /* la liste reste terminée à chaque instant */
    memcpy(buf + at + 2, name, nlen);
    wr16(buf + at, total);
    if (old_end > new_end) memset(buf + new_end, 0, old_end - new_end); /* place libérée remise à zéro */
  }
  memcpy(buf + at + 2 + nlen, data, len);
  storage_commit();
  return true;
}

bool storage_info(StorageInfo *info) {
  uint32_t size;
  int32_t at;
  const uint8_t *buf = storage_area(&size);
  memset(info, 0, sizeof *info);
  header_info(info);
  if (!buf) return false;
  int32_t end = scan(buf, size, "", &at);
  info->address = (uint32_t)(uintptr_t)buf;
  info->size = size;
  info->used = end < 0 ? size : (uint32_t)end;
  for (uint32_t p = 0; end >= 0 && p < (uint32_t)end; p += rd16(buf + p)) info->files++;
  return end >= 0;
}

bool storage_file(uint16_t i, const char **name, uint32_t *len) {
  uint32_t size;
  int32_t at;
  const uint8_t *buf = storage_area(&size);
  if (!buf) return false;
  int32_t end = scan(buf, size, "", &at);
  for (uint32_t p = 0; end >= 0 && p < (uint32_t)end; p += rd16(buf + p)) {
    if (i--) continue;
    *name = (const char *)buf + p + 2;
    *len = rd16(buf + p) - 2 - (uint32_t)strlen(*name) - 1;
    return true;
  }
  return false;
}
