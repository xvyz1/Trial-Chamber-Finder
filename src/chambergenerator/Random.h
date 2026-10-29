#ifndef CHAMBER_RANDOM_H
#define CHAMBER_RANDOM_H
#include <stdint.h>

typedef struct { uint64_t s; uint64_t n; } Rng;

#define RNG_MUL 0x5DEECE66DULL
#define RNG_MASK ((1ULL << 48) - 1)

static inline void rng_set_seed(Rng *r, int64_t seed) { r->s = ((uint64_t)seed ^ RNG_MUL) & RNG_MASK; r->n = 0; }

static inline int32_t rng_next(Rng *r, int bits) {
    r->s = (r->s * RNG_MUL + 0xB) & RNG_MASK;
    r->n++;
    return (int32_t)(uint32_t)(r->s >> (48 - bits));
}

static inline int32_t rng_next_int(Rng *r, int32_t n) {
    if ((n & -n) == n) return (int32_t)(((int64_t)n * rng_next(r, 31)) >> 31);
    int32_t bits, val;
    do {
        bits = rng_next(r, 31);
        val = bits % n;
    } while ((int32_t)((uint32_t)bits - (uint32_t)val + (uint32_t)(n - 1)) < 0);
    return val;
}

static inline int64_t rng_next_long(Rng *r) {
    int64_t hi = rng_next(r, 32);
    int64_t lo = rng_next(r, 32);
    return (int64_t)(((uint64_t)hi << 32) + (uint64_t)lo);
}

#define RNG_FAST_MAX 2048
extern uint32_t rng_thr[RNG_FAST_MAX + 1];
void rng_fast_init(void);

static inline int32_t rng_next31(Rng *r) {
    r->s = (r->s * RNG_MUL + 0xB) & RNG_MASK;
    r->n++;
    return (int32_t)(r->s >> 17);
}

extern uint64_t rng_fm[RNG_FAST_MAX + 1];
#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_ARM64)) && !defined(TC_PORTABLE)
#include <intrin.h>
static inline uint32_t fastmod_u32(uint32_t a, uint64_t M, uint32_t d) { return (uint32_t)__umulh(M * a, d); }
#elif defined(__SIZEOF_INT128__) && !defined(TC_PORTABLE)
static inline uint32_t fastmod_u32(uint32_t a, uint64_t M, uint32_t d) {
    return (uint32_t)(((unsigned __int128)(M * a) * d) >> 64);
}
#else
static inline uint32_t fastmod_u32(uint32_t a, uint64_t M, uint32_t d) {
    uint64_t low = M * a;
    return (uint32_t)(((low >> 32) * d + (((low & 0xFFFFFFFFULL) * d) >> 32)) >> 32);
}
#endif

static inline int32_t rng_int_fast(Rng *r, int32_t n) {
    if ((n & -n) == n) return (int32_t)(((int64_t)n * rng_next31(r)) >> 31);
    for (;;) {
        int32_t bits = rng_next31(r);
        if ((uint32_t)bits < rng_thr[n]) return (int32_t)fastmod_u32((uint32_t)bits, rng_fm[n], (uint32_t)n);
    }
}

static inline int64_t jmul(int64_t a, int64_t b) { return (int64_t)((uint64_t)a * (uint64_t)b); }

#endif
