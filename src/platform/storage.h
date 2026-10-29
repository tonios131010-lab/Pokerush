/* Fichiers d'Epsilon (scripts Python, etc.). L'EADK n'a pas d'API de fichiers : storage.c écrit
 * directement dans le système de fichiers d'Epsilon, qui est en RAM.
 *
 * Il ne déplace jamais un fichier : Epsilon garde un pointeur vers le dernier fichier consulté, qui
 * deviendrait faux. Un fichier est donc réécrit à sa place s'il garde sa taille, agrandi ou rétréci
 * seulement s'il est le dernier, et créé à la fin. */
#ifndef STORAGE_H
#define STORAGE_H
#include <stdbool.h>
#include <stdint.h>

/* Le contenu du fichier `name` ("base.ext"), ou NULL ; *len reçoit sa taille. Pointeur non aligné,
 * valable jusqu'à la prochaine écriture. */
const uint8_t *eadk_storage_read(const char *name, uint32_t *len);

/* Écrit le fichier (le crée s'il n'existe pas). false si le système de fichiers est introuvable ou
 * plein, ou si le fichier existe avec une autre taille sans être le dernier (il faudrait déplacer
 * les suivants). */
bool eadk_storage_write(const char *name, const void *data, uint32_t len);

/* Ce qui a été trouvé, sans rien écrire (app de diagnostic). */
typedef struct {
  uint32_t header;  /* adresse de l'en-tête d'Epsilon en flash (0 sur PC) */
  char version[9];  /* version d'Epsilon */
  uint32_t address; /* tampon des fichiers en RAM */
  uint32_t size, used;
  uint16_t files;
} StorageInfo;
bool storage_info(StorageInfo *info);
/* Le i-ème fichier (nom, taille du contenu) ; false après le dernier. */
bool storage_file(uint16_t i, const char **name, uint32_t *len);

#endif
