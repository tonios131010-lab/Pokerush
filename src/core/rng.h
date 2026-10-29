/* Générateur pseudo-aléatoire xorshift32 : une graine donne toujours la même suite (combats
 * rejouables, tests déterministes). La graine ne doit pas valoir 0. */
#ifndef RNG_H
#define RNG_H
#include <stdbool.h>
#include <stdint.h>

static inline uint32_t rng_next(uint32_t *s) {
  uint32_t x = *s;
  x ^= x << 13, x ^= x >> 17, x ^= x << 5;
  return *s = x;
}
/* 0..n-1 */
static inline uint32_t rng_below(uint32_t *s, uint32_t n) { return (uint32_t)(((uint64_t)rng_next(s) * n) >> 32); }
/* vrai avec une probabilité de p % */
static inline bool rng_percent(uint32_t *s, uint32_t p) { return rng_below(s, 100) < p; }

#endif
