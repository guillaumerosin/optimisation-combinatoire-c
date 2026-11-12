#ifndef JSP_RNG_H
#define JSP_RNG_H

#include <stdint.h>

// xoshiro256** (Blackman et Vigna), initialise par splitmix64. Rapide, de
// bonne qualite, et chaque thread possede le sien : rand() n'offre rien
// de tout cela.
typedef struct {
    uint64_t state[4];
} Rng;

static inline uint64_t splitmix64(uint64_t *x) {
    uint64_t z = (*x += 0x9e3779b97f4a7c15ULL);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    return z ^ (z >> 31);
}

static inline void rng_seed(Rng *rng, uint64_t seed) {
    for (int index = 0; index < 4; ++index) {
        rng->state[index] = splitmix64(&seed);
    }
}

static inline uint64_t rng_rotl(uint64_t x, int k) {
    return (x << k) | (x >> (64 - k));
}

static inline uint64_t rng_next(Rng *rng) {
    uint64_t *s = rng->state;
    const uint64_t result = rng_rotl(s[1] * 5, 7) * 9;
    const uint64_t t = s[1] << 17;
    s[2] ^= s[0];
    s[3] ^= s[1];
    s[1] ^= s[2];
    s[0] ^= s[3];
    s[2] ^= t;
    s[3] = rng_rotl(s[3], 45);
    return result;
}

// Double uniforme dans [0, 1) : les 53 bits de poids fort forment la mantisse.
static inline double rng_unit(Rng *rng) {
    return (double)(rng_next(rng) >> 11) * 0x1.0p-53;
}

// Entier uniforme dans [0, n), par multiplication (biais negligeable).
static inline int rng_below(Rng *rng, int n) {
    return (int)(((rng_next(rng) >> 32) * (uint64_t)n) >> 32);
}

#endif
