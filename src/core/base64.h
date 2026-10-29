/* Base64 standard (RFC 4648, avec '=' de bourrage). */
#ifndef BASE64_H
#define BASE64_H
#include <stdint.h>

#define BASE64_LEN(n) (((n) + 2) / 3 * 4)

/* Écrit BASE64_LEN(n) caractères dans dst (sans zéro final) ; renvoie ce nombre. */
uint32_t base64_encode(char *dst, const void *src, uint32_t n);
/* Décode len caractères ; renvoie le nombre d'octets écrits, ou -1 (caractère invalide, plus de max octets). */
int32_t base64_decode(void *dst, uint32_t max, const char *src, uint32_t len);

#endif
