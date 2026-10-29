#include "core/base64.h"

static const char digits[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

uint32_t base64_encode(char *dst, const void *src, uint32_t n) {
  const uint8_t *s = src;
  char *d = dst;
  for (uint32_t i = 0; i < n; i += 3) {
    uint32_t v = (uint32_t)s[i] << 16 | (i + 1 < n ? s[i + 1] << 8 : 0) | (i + 2 < n ? s[i + 2] : 0);
    *d++ = digits[v >> 18];
    *d++ = digits[(v >> 12) & 63];
    *d++ = i + 1 < n ? digits[(v >> 6) & 63] : '=';
    *d++ = i + 2 < n ? digits[v & 63] : '=';
  }
  return (uint32_t)(d - dst);
}

static int value(char c) {
  if (c >= 'A' && c <= 'Z') return c - 'A';
  if (c >= 'a' && c <= 'z') return c - 'a' + 26;
  if (c >= '0' && c <= '9') return c - '0' + 52;
  return c == '+' ? 62 : c == '/' ? 63 : -1;
}

int32_t base64_decode(void *dst, uint32_t max, const char *src, uint32_t len) {
  uint8_t *d = dst;
  uint32_t out = 0;
  if (len % 4) return -1;
  for (uint32_t i = 0; i < len; i += 4) {
    int pad = (src[i + 3] == '=') + (src[i + 2] == '=');
    if (pad && i + 4 != len) return -1;
    uint32_t v = 0;
    for (int k = 0; k < 4; k++) {
      int x = k >= 4 - pad ? 0 : value(src[i + k]);
      if (x < 0) return -1;
      v = v << 6 | (uint32_t)x;
    }
    for (int k = 0; k < 3 - pad; k++) {
      if (out == max) return -1;
      d[out++] = (uint8_t)(v >> (16 - 8 * k));
    }
  }
  return (int32_t)out;
}
